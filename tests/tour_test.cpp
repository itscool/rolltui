//
// tour_test.cpp — `rolltui-tour`, run on a pty as a person runs it: the table, its footer and the dashboard drawn; the settings
// opened with F2 and closed with Escape; the page of Markdown shown by a click on its tab; a theme chosen; and Ctrl-Q leaving the
// terminal as it was found. The same file is the web demo, so what this holds is what the page shows.
//
#include <filesystem>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <cstdlib>
#include <string>

#include "rolltui_test.hpp"
#include "vt_model.hpp"

#ifndef ROLLTUI_TOUR_BIN
#error "ROLLTUI_TOUR_BIN must name the rolltui-tour binary"
#endif

using testkit::check;

namespace {

struct Pty {
  int master = -1;
  pid_t pid = -1;
  vt::Screen screen{120, 34};
  void pump(int ms) {
    const std::uint64_t end = testkit::mono_ms() + static_cast<std::uint64_t>(ms);
    while (testkit::mono_ms() < end) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0 && (p.revents & POLLIN)) {
        char b[16384];
        const ssize_t k = ::read(master, b, sizeof b);
        if (k <= 0) return;
        screen.feed(std::string(b, static_cast<std::size_t>(k)));
      }
    }
  }
  bool shown(const std::string& text, int budget_ms) {
    const std::uint64_t began = testkit::mono_ms();
    while (testkit::mono_ms() - began < static_cast<std::uint64_t>(budget_ms)) {
      pump(40);
      if (!screen.find(text).empty()) return true;
    }
    return false;
  }
  bool gone(const std::string& text, int budget_ms) {
    const std::uint64_t began = testkit::mono_ms();
    while (testkit::mono_ms() - began < static_cast<std::uint64_t>(budget_ms)) {
      pump(40);
      if (screen.find(text).empty()) return true;
    }
    return false;
  }
  void send(const std::string& s) { (void)!::write(master, s.data(), s.size()); }
};

}  // namespace

int main() {
  char tmpl[] = "/tmp/rolltui_tour_test_XXXXXX";
  const char* dir = ::mkdtemp(tmpl);
  if (dir) ::setenv("ROLL_CONFIG_DIR", dir, 1); /* nothing here may touch the person's own configuration */
  /* A RUN LEAVES NOTHING: the folder and what was written into it go however main returns */
  struct RemoveAtEnd {
    std::string dir;
    ~RemoveAtEnd() {
      std::error_code ec;
      if (!dir.empty()) std::filesystem::remove_all(dir, ec);
    }
  } remove_at_end{dir ? dir : ""};
  winsize ws{};
  ws.ws_row = 34;
  ws.ws_col = 120;
  Pty t;
  t.pid = ::forkpty(&t.master, nullptr, nullptr, &ws);
  if (t.pid == 0) {
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::execl(ROLLTUI_TOUR_BIN, ROLLTUI_TOUR_BIN, static_cast<char*>(nullptr));
    ::_exit(127);
  }
  check(t.pid > 0 && t.shown("kernel_task", 5000), "the tour starts on a pty and draws its table");
  check(t.shown("1 of 72", 3000), "…with the list's footer in its border");
  check(t.shown("cpu", 3000) && t.shown("network", 3000), "…and the dashboard beside it, at 120 columns");
  check(t.shown("F2 settings", 3000), "…and the status line");
  t.send("\x1bOQ"); /* F2 */
  check(t.shown("Light or dark", 3000), "F2 opens the settings");
  t.send("\x1b");
  check(t.gone("Light or dark", 3000), "Escape closes them");
  const auto tab = t.screen.find("Read me");
  check(!tab.empty(), "the page of Markdown is a tab in the title");
  if (!tab.empty()) {
    const std::string at = std::to_string(tab[0].first + 2) + ";" + std::to_string(tab[0].second + 1);
    t.send("\x1b[<0;" + at + "M\x1b[<0;" + at + "m");
    check(t.shown("A terminal UI library in C", 3000), "a click on its title shows it, from the top");
    check(t.gone("1 of 72", 1000), "…and the list's footer goes with the list");
  }
  t.send("\x1bOQ\r\x1b[B\r"); /* the themes in order, from the one shown: default, then ember */
  check(t.shown("theme ember", 3000), "a theme chosen in the settings is the theme the status line names");
  t.send("\x11"); /* Ctrl-Q */
  int status = 0;
  std::string rest;
  check(rolltui_test::end_pty_child(t.master, t.pid, 3000, &status, &rest) && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "Ctrl-Q quits");
  check(rest.find("\x1b[?1049l") != std::string::npos, "…leaving the alternate screen");
  return testkit::report("tour_test");
}
