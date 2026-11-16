//
// dirktui_hover_test.cpp — THE SHIPPED dirktui, ON A PTY: THE ENTRY UNDER THE POINTER IS LIT, AND THE CURSOR STAYS PUT.
//
// dirktui asks the terminal to report the pointer as it moves (mode 1003). A report of it resting on an entry repaints that row in
// the theme's `hover` role; a report elsewhere puts the row back. The screen is read through a small model of the terminal: where
// the cursor is sent, what is printed there, and the background each cell is printed on.
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

using namespace testkit;
namespace fs = std::filesystem;

#if !defined(DIRKTUI_PRODUCT_BIN)
#error "the dirktui binary must be named"
#endif

namespace {

constexpr int kW = 100, kH = 30;

// What the terminal shows: a glyph and the background it was printed on, per cell.
struct Cell {
  std::string glyph = " ";
  std::string bg;  // "r;g;b", empty for the terminal's own
};

struct Model {
  std::vector<Cell> cells = std::vector<Cell>(kW * kH);
  int row = 0, col = 0;
  std::string bg;

  void sgr(const std::string& params) {
    std::vector<int> p;
    std::size_t i = 0;
    while (i <= params.size()) {
      const std::size_t j = params.find_first_of(";:", i);
      const std::string part = params.substr(i, j == std::string::npos ? std::string::npos : j - i);
      p.push_back(part.empty() ? 0 : std::atoi(part.c_str()));
      if (j == std::string::npos) break;
      i = j + 1;
    }
    for (std::size_t k = 0; k < p.size(); ++k) {
      if (p[k] == 0 || p[k] == 49) bg.clear();
      else if ((p[k] == 38 || p[k] == 48) && k + 1 < p.size() && p[k + 1] == 2 && k + 4 < p.size()) {
        if (p[k] == 48) bg = std::to_string(p[k + 2]) + ";" + std::to_string(p[k + 3]) + ";" + std::to_string(p[k + 4]);
        k += 4;
      } else if ((p[k] == 38 || p[k] == 48) && k + 1 < p.size() && p[k + 1] == 5 && k + 2 < p.size()) {
        if (p[k] == 48) bg = "i" + std::to_string(p[k + 2]);
        k += 2;
      }
    }
  }
  void feed(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      if (c == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {
        std::size_t j = i + 2;
        while (j < s.size() && (s[j] < 0x40 || s[j] > 0x7e)) ++j;
        if (j >= s.size()) return;
        const std::string params = s.substr(i + 2, j - i - 2);
        const char fin = s[j];
        if (fin == 'm' && (params.empty() || params[0] != '>')) sgr(params);
        else if (fin == 'H' || fin == 'f') {
          const std::size_t semi = params.find(';');
          row = (params.empty() ? 1 : std::atoi(params.c_str())) - 1;
          col = (semi == std::string::npos ? 1 : std::atoi(params.c_str() + semi + 1)) - 1;
        } else if (fin == 'G') col = (params.empty() ? 1 : std::atoi(params.c_str())) - 1;
        else if (fin == 'C') col += params.empty() ? 1 : std::atoi(params.c_str());
        i = j + 1;
        continue;
      }
      if (c == 0x1b && i + 1 < s.size() && (s[i + 1] == ']' || s[i + 1] == 'P' || s[i + 1] == '_')) {  // a string: to BEL or ST
        std::size_t j = i + 2;
        while (j < s.size() && s[j] != '\x07' && !(s[j] == 0x1b && j + 1 < s.size() && s[j + 1] == '\\')) ++j;
        i = j < s.size() && s[j] == '\x07' ? j + 1 : j + 2;
        continue;
      }
      if (c == 0x1b) { i += 2; continue; }
      if (c == '\r') { col = 0; ++i; continue; }
      if (c == '\n') { ++row; ++i; continue; }
      if (c < 0x20) { ++i; continue; }
      std::size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
      if (row >= 0 && row < kH && col >= 0 && col < kW) cells[row * kW + col] = Cell{s.substr(i, n), bg};
      ++col;
      i += n;
    }
  }
  // The first cell of `text` on the screen; false when it is not there.
  bool find(const std::string& text, int& x, int& y) const {
    for (y = 0; y < kH; ++y) {
      std::string line;
      std::vector<int> at;
      for (int c = 0; c < kW; ++c) {
        at.push_back(static_cast<int>(line.size()));
        line += cells[y * kW + c].glyph;
      }
      const std::size_t k = line.find(text);
      if (k == std::string::npos) continue;
      for (x = 0; x < kW; ++x)
        if (at[x] == static_cast<int>(k)) return true;
    }
    return false;
  }
  const std::string& bg_at(int x, int y) const { return cells[y * kW + x].bg; }
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
  std::ofstream(cfg / "rolltui" / "dirktui" / "update.json") << "{ \"latest\": \"\", \"checked_ms\": 1000000000000000, \"update_since_ms\": 0 }\n";
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
