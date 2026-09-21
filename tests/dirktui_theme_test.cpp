//
// dirktui_theme_test.cpp — THE SHIPPED dirktui, ON A PTY: LIGHT OR DARK AND COLOUR DEPTH ARE THE PERSON'S, AND A THEME NEVER FORCES THEM.
//
// Choosing "light" and then another theme used to put the person back on "auto", because every theme file said `"mode": "auto"` and
// loading one replaced the whole working value. It also froze the colours: choosing light made the working copy differ from its
// preset, so the file was written as a SNAPSHOT of the colours, and a theme improved in a later release never reached that person.
// `presets_test` holds the store to this in process; this holds the running program to it, keys and all, and reads the file it leaves.
//
// (A `--frame` run does not autosave, so the real binary on a pty is what shows what is written.)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

// Plays a terminal for one run of dirktui: starts it, presses the keys (a name, or several), lets it settle, and ends it.
void run_keys(const fs::path& cfg, const fs::path& tree, const std::vector<std::string>& keys) {
  winsize ws{};
  ws.ws_row = 34;
  ws.ws_col = 110;
  int master = -1;
  const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
  if (pid == 0) {
    ::chdir(tree.c_str());
    ::setenv("ROLL_CONFIG_DIR", cfg.c_str(), 1);
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::setenv("LANG", "en_US.UTF-8", 1);
    ::execl(DIRKTUI_PRODUCT_BIN, "dirktui", tree.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  auto pump = [&](int ms) {
    const long long end = now_ms() + ms;
    while (now_ms() < end) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 25) > 0 && (p.revents & POLLIN)) {
        char b[8192];
        if (::read(master, b, sizeof b) <= 0) return;
      }
    }
  };
  pump(1300);
  for (const std::string& k : keys) {
    const char* seq = k == "F2" ? "\x1bOQ" : k == "Down" ? "\x1b[B" : k == "Up" ? "\x1b[A" : k == "Enter" ? "\r" : k == "Esc" ? "\x1b" : "";
    if (*seq) { const ssize_t w = ::write(master, seq, std::strlen(seq)); (void)w; }
    pump(280);
  }
  pump(400);
  ::kill(pid, SIGKILL);
  int st = 0;
  ::waitpid(pid, &st, 0);
  ::close(master);
}

std::vector<std::string> rep(std::vector<std::string> v, const std::string& k, int n) {
  for (int i = 0; i < n; ++i) v.push_back(k);
  return v;
}

}  // namespace

int main() {
  char tmpl[] = "/tmp/rolltui_dirk_theme_XXXXXX";
  const char* base = ::mkdtemp(tmpl);
  if (!base) return report("dirktui_theme_test");
  const fs::path root(base), tree = root / "tree", cfg = root / "cfg";
  fs::create_directories(tree);
  std::ofstream(tree / "a.txt") << "a\n";
  fs::create_directories(cfg / "rolltui" / "dirktui");
  std::ofstream(cfg / "rolltui" / "dirktui" / "settings.json") << "{ \"motion\": false }";
  const fs::path working = cfg / "rolltui" / "theme.working.json";

  // The settings menu opens on its first row; the rows are: dotfiles, sort, key bindings, THEME, LIGHT OR DARK, COLOURS.
  const auto to_theme = rep({"F2"}, "Down", 3);
  const auto to_mode = rep({"F2"}, "Down", 4);
  const auto to_depth = rep({"F2"}, "Down", 5);

  // ---- CHOOSING LIGHT: the person's setting, kept beside a pointer to the theme; the colours are NOT copied out ---------------
  auto keys = to_mode;
  for (const char* k : {"Enter", "Down", "Down", "Enter"}) keys.push_back(k);  // the list is auto, dark, light
  run_keys(cfg, tree, keys);
  std::string file = slurp(working);
  check(has(file, "\"preset\": \"default\"") && has(file, "\"mode\": \"light\"") && has(file, "\"follows_origin\": true") && !has(file, "\"colours\""),
        "choosing light writes a POINTER to the theme with the setting beside it, and no copy of the colours to freeze [" + file + "]");

  // ---- CHOOSING ANOTHER THEME: the setting stays ------------------------------------------------------------------------------
  keys = to_theme;
  for (const char* k : {"Enter", "Down", "Enter"}) keys.push_back(k);  // the list is default, then the others in order
  run_keys(cfg, tree, keys);
  file = slurp(working);
  check(!has(file, "\"preset\": \"default\"") && has(file, "\"follows_origin\": true"), "choosing another theme changes the theme the file follows [" + file + "]");
  check(has(file, "\"mode\": \"light\""), "…and light STAYS: no theme puts a person back on auto");
  keys = to_theme;
  for (const char* k : {"Enter", "Down", "Down", "Enter"}) keys.push_back(k);
  run_keys(cfg, tree, keys);
  file = slurp(working);
  check(has(file, "\"mode\": \"light\"") && has(file, "\"follows_origin\": true") && !has(file, "\"colours\""), "…and again for a third theme [" + file + "]");

  // ---- COLOUR DEPTH IS THE SAME ------------------------------------------------------------------------------------------------------
  keys = to_depth;
  for (const char* k : {"Enter", "Down", "Down", "Enter"}) keys.push_back(k);  // auto, truecolor, 256
  run_keys(cfg, tree, keys);
  file = slurp(working);
  check(has(file, "\"depth\": \"256\"") && has(file, "\"mode\": \"light\""), "colour depth is remembered beside light [" + file + "]");
  keys = to_theme;
  for (const char* k : {"Enter", "Down", "Enter"}) keys.push_back(k);
  run_keys(cfg, tree, keys);
  file = slurp(working);
  check(has(file, "\"depth\": \"256\"") && has(file, "\"mode\": \"light\""), "…and a theme chosen after it keeps both [" + file + "]");

  // ---- AUTO IS THE LIBRARY'S, NOT SOMETHING A FILE SAYS: choosing it writes nothing --------------------------------------------------
  keys = to_mode;
  for (const char* k : {"Enter", "Up", "Up", "Enter"}) keys.push_back(k);  // back to auto
  run_keys(cfg, tree, keys);
  file = slurp(working);
  check(!has(file, "\"mode\"") && has(file, "\"depth\": \"256\""), "choosing auto again removes the key: it is the default, and is never written [" + file + "]");

  fs::remove_all(root);
  return report("dirktui_theme_test");
}
