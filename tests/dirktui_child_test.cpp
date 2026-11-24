//
// dirktui_child_test.cpp — THE SHIPPED dirktui, ON A PTY: A PROGRAM IT RUNS ON THE TERMINAL HAS CTRL-C TO ITSELF.
//
// With `exec` set to run, Enter on a script runs it here, on this terminal, in its ordinary mode. Ctrl-C then signals the whole
// foreground group, dirktui included. The script must stop and dirktui must not: it takes the terminal back and draws again.
#include <poll.h>
#include <signal.h>
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

#include "rolltui_test.hpp"

using namespace testkit;
namespace fs = std::filesystem;

#if !defined(DIRKTUI_PRODUCT_BIN)
#error "the dirktui binary must be named"
#endif

namespace {

struct Screen {
  int master = -1;
  pid_t pid = -1;
  std::string all;  // everything the program has written, in order

  void pump(int ms) {
    const std::uint64_t end = mono_ms() + static_cast<std::uint64_t>(ms);
    while (mono_ms() < end) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 25) > 0 && (p.revents & POLLIN)) {
        char b[8192];
        const ssize_t k = ::read(master, b, sizeof b);
        if (k > 0) all.append(b, static_cast<std::size_t>(k));
        else return;
      }
    }
  }
  // Waits for `needle` to be written after `from`; false when it never came.
  bool wait_for(const std::string& needle, std::size_t from, int budget_ms) {
    const std::uint64_t began = mono_ms();
    while (mono_ms() - began < static_cast<std::uint64_t>(budget_ms)) {
      pump(50);
      if (all.find(needle, from) != std::string::npos) return true;
    }
    return false;
  }
  bool alive() {
    int st = 0;
    return ::waitpid(pid, &st, WNOHANG) == 0;
  }
};

}  // namespace

int main() {
  char tmpl[] = "/tmp/rolltui_dirk_child_XXXXXX";
  const char* base = ::mkdtemp(tmpl);
  if (!base) return report("dirktui_child_test");
  const fs::path root(base), tree = root / "tree", cfg = root / "cfg";
  fs::create_directories(tree);
  fs::create_directories(cfg / "rolltui" / "dirktui");
  std::ofstream(cfg / "rolltui" / "dirktui" / "settings.json") << "{ \"exec\": \"run\", \"motion\": false }";
  const fs::path script = tree / "slow.sh";
  std::ofstream(script) << "#!/bin/sh\necho SLOW_STARTED\nsleep 6\necho SLOW_FINISHED\n";
  ::chmod(script.c_str(), 0755);

  winsize ws{};
  ws.ws_row = 30;
  ws.ws_col = 100;
  Screen s;
  s.pid = ::forkpty(&s.master, nullptr, nullptr, &ws);
  if (s.pid == 0) {
    if (::chdir(tree.c_str()) != 0) ::_exit(127); /* the program would run somewhere else */
    ::setenv("ROLL_CONFIG_DIR", cfg.c_str(), 1);
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::setenv("LANG", "en_US.UTF-8", 1);
    ::execl(DIRKTUI_PRODUCT_BIN, "dirktui", tree.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  check(s.pid > 0 && s.wait_for("slow.sh", 0, 4000), "dirktui started on a pty and lists the script");

  (void)!::write(s.master, "\r", 1);
  const bool started = s.wait_for("SLOW_STARTED", 0, 4000);
  check(started, "Enter on the script runs it here, on the terminal");

  const std::size_t at_interrupt = s.all.size();
  (void)!::write(s.master, "\x03", 1);  // Ctrl-C, while the script has the terminal
  const bool redrawn = s.wait_for("slow.sh", at_interrupt, 4000);
  s.pump(300);
  check(started && s.alive() && redrawn, "Ctrl-C stops the script, and dirktui takes the terminal back and draws its listing again");
  check(s.all.find("SLOW_FINISHED") == std::string::npos, "…the script did stop: it never reached its last line");

  (void)!::write(s.master, "\x11", 1);  // Ctrl-Q
  check(rolltui_test::end_pty_child(s.master, s.pid, 3000), "…and it still quits when asked");
  std::error_code ec;
  fs::remove_all(root, ec);
  return report("dirktui_child_test");
}
