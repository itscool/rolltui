//
// dirktui_hover_test.cpp — THE SHIPPED dirktui, ON A PTY: THE ENTRY UNDER THE POINTER IS LIT, AND THE CURSOR STAYS PUT.
//
// dirktui asks the terminal to report the pointer as it moves (mode 1003). A report of it resting on an entry repaints that row in
// the theme's `hover` role; a report elsewhere puts the row back. The screen is read through the terminal model the renderer's own
// suites replay into (vt_model.hpp): what is printed where, and the background each cell is printed on.
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rolltui_test.hpp"
#include "vt_model.hpp" /* INTERNAL through the model: this suite is in ROLLTUI_INTERNAL_OPT_IN */

using namespace testkit;
namespace fs = std::filesystem;

#if !defined(DIRKTUI_PRODUCT_BIN)
#error "the dirktui binary must be named"
#endif

namespace {

constexpr int kW = 100, kH = 30;

// What the terminal shows, through vt_model.hpp: the shared model, measuring glyphs as the library does, fed as the pty delivers.
struct Model {
  vt::Screen screen{kW, kH};
  void feed(const std::string& s) { screen.feed(s); }
  // The first cell of `text` on the screen; false when it is not there.
  bool find(const std::string& text, int& x, int& y) const {
    const auto at = screen.find(text);
    if (at.empty()) return false;
    x = at.front().first, y = at.front().second;
    return true;
  }
  // The background a cell is printed on, "r;g;b"; empty for the terminal's own.
  std::string bg_at(int x, int y) const {
    const RolltuiStyleColorRaw& c = screen.at(x, y).style.bg;
    if (c.kind != RolltuiStyleColorRaw::Kind::Rgb) return "";
    return std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b);
  }
};

struct Pty {
  int master = -1;
  pid_t pid = -1;
  std::string all;
  Model screen;

  void pump(int ms) {
    const std::uint64_t end = mono_ms() + static_cast<std::uint64_t>(ms);
    while (mono_ms() < end) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 25) > 0 && (p.revents & POLLIN)) {
        char b[8192];
        const ssize_t k = ::read(master, b, sizeof b);
        if (k <= 0) return;
        const std::string got(b, static_cast<std::size_t>(k));
        all += got;
        screen.feed(got);
      }
    }
  }
  bool wait_for(const std::string& needle, int budget_ms) {
    const std::uint64_t began = mono_ms();
    while (mono_ms() - began < static_cast<std::uint64_t>(budget_ms)) {
      pump(50);
      if (all.find(needle) != std::string::npos) return true;
    }
    return false;
  }
  void move_to(int x, int y) {  // a report of the pointer moving, no button held: SGR button 35, 1-based cells
    const std::string m = "\x1b[<35;" + std::to_string(x + 1) + ";" + std::to_string(y + 1) + "M";
    (void)!::write(master, m.data(), m.size());
  }
};

}  // namespace

int main() {
  char tmpl[] = "/tmp/rolltui_dirk_hover_XXXXXX";
  const char* base = ::mkdtemp(tmpl);
  if (!base) return report("dirktui_hover_test");
  const fs::path root(base), tree = root / "tree", cfg = root / "cfg";
  fs::create_directories(tree);
  fs::create_directories(cfg / "rolltui" / "dirktui");
  std::ofstream(cfg / "rolltui" / "dirktui" / "settings.json") << "{ \"motion\": false }";
  // An update check already made: the run reaches no network.
  fs::create_directories(cfg / "state" / "dirktui"); /* the update check's state, under the scratch home */
  std::ofstream(cfg / "state" / "dirktui" / "update.json") << "{ \"latest\": \"\", \"checked_ms\": 1000000000000000, \"update_since_ms\": 0 }\n";
  for (const char* name : {"alpha.txt", "beta.txt", "gamma.txt"}) std::ofstream(tree / name) << name << "\n";

  winsize ws{};
  ws.ws_row = kH;
  ws.ws_col = kW;
  Pty t;
  t.pid = ::forkpty(&t.master, nullptr, nullptr, &ws);
  if (t.pid == 0) {
    ::chdir(tree.c_str());
    ::setenv("ROLL_CONFIG_DIR", cfg.c_str(), 1);
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::setenv("LANG", "en_US.UTF-8", 1);
    ::execl(DIRKTUI_PRODUCT_BIN, "dirktui", tree.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  check(t.pid > 0 && t.wait_for("gamma.txt", 4000), "dirktui started on a pty and lists the three files");
  t.pump(400);
  check(t.all.find("\x1b[?1003h") != std::string::npos, "it asks the terminal to report the pointer as it moves");

  int gx = -1, gy = -1, ax = -1, ay = -1;
  const bool found = t.screen.find("gamma.txt", gx, gy) && t.screen.find("alpha.txt", ax, ay);
  check(found, "the model of the screen finds both entries [gamma at " + std::to_string(gx) + "," + std::to_string(gy) + "]");
  if (found) {
    const std::string plain = t.screen.bg_at(gx, gy), cursor = t.screen.bg_at(ax, ay);
    // The shipped `default` theme's `hover`, dark and light: a lift of the panel toward the ink.
    auto is_hover = [](const std::string& bg) { return bg == "53;56;62" || bg == "191;195;202"; };
    t.move_to(gx + 2, gy);
    t.pump(400);
    check(is_hover(t.screen.bg_at(gx, gy)) && !is_hover(plain),
          "a pointer resting on gamma.txt repaints its row in hover [" + plain + " -> " + t.screen.bg_at(gx, gy) + "]");
    check(t.screen.bg_at(ax, ay) == cursor, "…and the cursor's row, alpha.txt, is as it was: the pointer moves nothing");
    t.move_to(kW - 3, kH - 3);
    t.pump(400);
    check(t.screen.bg_at(gx, gy) == plain, "the pointer moved off it, the row is put back [" + t.screen.bg_at(gx, gy) + "]");
  }

  (void)!::write(t.master, "\x11", 1);  // Ctrl-Q
  int status = 0;
  std::string rest;
  check(rolltui_test::end_pty_child(t.master, t.pid, 3000, &status, &rest), "it quits when asked");
  check(rest.find("\x1b[?1003l") != std::string::npos, "…and gives the pointer reports back on the way out");
  std::error_code ec;
  fs::remove_all(root, ec);
  return report("dirktui_hover_test");
}
