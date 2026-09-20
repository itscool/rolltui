//
// dirktui_watch_test.cpp — THE SHIPPED dirktui, ON A PTY, WITH THE DISK CHANGING UNDER IT.
//
// `watch_test.cpp` holds the picker to its edge cases by calling the look itself. What it cannot prove is the wiring: that
// the frame clock reaches the picker, that the picker's look is due while nothing is being pressed, that a host asked to
// wait for input does not sleep past it, and that what the look finds is drawn. Only a program's own loop shows that, so
// this starts the real binary, plays its terminal, changes files while it sits idle, and reads what it draws.
//
// No key is pressed between the changes. Anything the screen learns, it learns by itself.
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "rolltui_test.hpp"

using namespace testkit;

#if !defined(DIRKTUI_PRODUCT_BIN)
#error "the dirktui binary must be named"
#endif

namespace {
namespace fs = std::filesystem;

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void put(const fs::path& p, const std::string& text, bool append = false) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, append ? std::ios::app : std::ios::trunc) << text;
}

struct Screen {
  int master = -1;
  pid_t pid = -1;
  std::string all;  // everything the program has written, in order

  void pump(int ms) {
    const long long end = now_ms() + ms;
    while (now_ms() < end) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 25) > 0 && (p.revents & POLLIN)) {
        char b[8192];
        const ssize_t k = ::read(master, b, sizeof b);
        if (k > 0) all.append(b, static_cast<std::size_t>(k));
        else if (k <= 0) return;
      }
    }
  }
  // Waits for `needle` to be written AFTER this call began, and says how long it took (-1: it never came).
  long long wait_for(const std::string& needle, int budget_ms) {
    const std::size_t from = all.size();
    const long long began = now_ms();
    while (now_ms() - began < budget_ms) {
      pump(50);
      if (all.find(needle, from) != std::string::npos) return now_ms() - began;
    }
    return -1;
  }
};

}  // namespace

int main() {
  char tmpl[] = "/tmp/rolltui_dirk_watch_XXXXXX";
  const char* base = ::mkdtemp(tmpl);
  if (!base) return report("dirktui_watch_test");
  const fs::path root(base);
  const fs::path tree = root / "tree", cfg = root / "cfg";
  put(tree / "a.txt", "a\n");
  put(tree / "b.txt", "b\n");
  put(tree / "c.txt", "c\n");
  put(tree / "sub" / "inner.txt", "inner\n");
  std::string log;
  for (int i = 0; i < 5; ++i) log += "log line " + std::to_string(i) + "\n";
  put(tree / "log.txt", log);
  put(cfg / "rolltui" / "dirktui" / "settings.json", "{ \"preview\": \"right\", \"motion\": false }");

  winsize ws{};
  ws.ws_row = 30;
  ws.ws_col = 120;
  Screen s;
  s.pid = ::forkpty(&s.master, nullptr, nullptr, &ws);
  if (s.pid == 0) {
    ::chdir(tree.c_str());
    ::setenv("ROLL_CONFIG_DIR", cfg.c_str(), 1);
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::setenv("LANG", "en_US.UTF-8", 1);
    const std::string target = (tree / "log.txt").string();
    ::execl(DIRKTUI_PRODUCT_BIN, "dirktui", target.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  check(s.pid > 0, "dirktui started on a pty");
  if (s.pid <= 0) return report("dirktui_watch_test");

  // ---- THE BASELINE: it is showing the file the cursor is on -------------------------------------------------------
  check(s.wait_for("log line 4", 8000) >= 0, "dirktui shows the file it was pointed at, its text in the preview");

  // ---- A FILE APPEARS: nothing is pressed ------------------------------------------------------------------------------
  put(tree / "zzz_new.txt", "ZZZ_NEIGHBOUR_CONTENT\n");
  const long long appeared = s.wait_for("zzz_new.txt", 6000);
  check(appeared >= 0, "a file created in the folder on screen appears on its own (after " + std::to_string(appeared) + " ms, with no key pressed)");
  check(appeared < 4000, "…within a few frames of the look's interval, not whenever the next key happens");

  // ---- THE PREVIEWED FILE GROWS --------------------------------------------------------------------------------------------
  put(tree / "log.txt", "APPENDED_LINE_XYZ\n", true);
  check(s.wait_for("APPENDED_LINE_XYZ", 6000) >= 0, "text appended to the file in the preview appears in it");

  // ---- AND IS REWRITTEN ---------------------------------------------------------------------------------------------------------
  put(tree / "log.txt", "REWRITTEN_CONTENT\n");
  check(s.wait_for("REWRITTEN_CONTENT", 6000) >= 0, "the file rewritten is shown as it now is");

  // ---- THE SELECTED FILE GOES: the cursor falls to its neighbour and the preview follows it ---------------------------------------
  fs::remove(tree / "log.txt");
  check(s.wait_for("ZZZ_NEIGHBOUR_CONTENT", 6000) >= 0, "the file under the cursor is deleted: the preview shows what the cursor fell to");

  // ---- THE FOLDER ON SCREEN IS EMPTIED and refilled, and dirktui is still alive -----------------------------------------------------
  const std::size_t before_empty = s.all.size();
  for (const char* n : {"a.txt", "b.txt", "c.txt", "zzz_new.txt"}) fs::remove(tree / n);
  fs::remove_all(tree / "sub");
  check(s.wait_for("(empty)", 6000) >= 0 && s.all.find("(empty)", before_empty) != std::string::npos, "a folder emptied under it says (empty)");
  put(tree / "phoenix.txt", "risen\n");
  check(s.wait_for("phoenix.txt", 6000) >= 0, "…and a file put back is listed");

  // ---- LEAVE ---------------------------------------------------------------------------------------------------------------------------
  (void)!::write(s.master, "\x11", 1);  // Ctrl-Q
  int status = 0;
  bool exited = false;
  for (int i = 0; i < 60 && !exited; ++i) {
    s.pump(50);
    exited = ::waitpid(s.pid, &status, WNOHANG) == s.pid;
  }
  if (!exited) {
    ::kill(s.pid, SIGKILL);
    ::waitpid(s.pid, &status, 0);
  }
  check(exited, "Ctrl-Q leaves: a program that watches the disk still quits when asked");
  std::error_code ec;
  fs::remove_all(root, ec);
  return report("dirktui_watch_test");
}
