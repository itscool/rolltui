//
// process_test.cpp — OTHER PROGRAMS: their coloured output read as styled lines, run under pipes and a pty, stopped, waited for and
// let go; the clipboard; the window's title and the working directory; an `output:` window following a program as it writes.
//
#include <fcntl.h>
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
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_os.h" /* INTERNAL: a program handed the terminal, run directly */
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read cells back */
#include "rolltui_test.hpp"

using testkit::check;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

std::string line(const RolltuiAnsiText* t, std::size_t i) {
  std::size_t n = 0;
  const char* p = rolltui_ansi_line(t, i, &n);
  return std::string(p, n);
}

struct Ansi {
  RolltuiAnsiText* t = rolltui_ansi_new();
  Ansi() = default;
  Ansi(const Ansi&) = delete;
  ~Ansi() { rolltui_ansi_free(t); }
  void feed(std::string_view s) { rolltui_ansi_feed(t, s.data(), s.size()); }
};

struct Frame {
  RolltuiFrame* f;
  Frame(int w, int h) : f(rolltui_frame_new(w, h, RolltuiStyle{})) {}
  Frame(const Frame&) = delete;
  ~Frame() { rolltui_frame_free(f); }
  RolltuiCell cell(int x, int y) const {
    RolltuiCell c{};
    rolltui_frame_cell(f, x, y, &c);
    return c;
  }
  std::string glyph(int x, int y) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_glyph(f, x, y, &n);
    return std::string(p, n);
  }
  std::string row(int y, int w) const {
    std::string r;
    for (int x = 0; x < w; ++x) r += glyph(x, y);
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
  }
};

bool indexed(const RolltuiStyleColorRaw& c, int i) { return static_cast<int>(c.kind) == 1 && c.index == i; }

void test_ansi() {
  {
    Ansi a;
    a.feed("plain\n");
    check(rolltui_ansi_line_count(a.t) == 1 && line(a.t, 0) == "plain", "a line ending in a newline is one line");
    a.feed("open");
    check(rolltui_ansi_line_count(a.t) == 2 && line(a.t, 1) == "open", "the line being written counts once it has text");
  }
  {
    Ansi a;
    a.feed("\x1b[1;31mred\x1b[0m done");
    Frame f(20, 1);
    RolltuiStyle base{};
    base.fg.kind = decltype(base.fg.kind)(1);
    base.fg.index = 7;
    rolltui_ansi_draw(a.t, f.f, RolltuiRect{0, 0, 20, 1}, 0, 0, 1, base, 0);
    check(line(a.t, 0) == "red done", "SGR is read and not shown");
    check(indexed(f.cell(0, 0).style.fg, 1) && f.cell(0, 0).style.bold, "red and bold where the program said");
    check(indexed(f.cell(4, 0).style.fg, 7) && !f.cell(4, 0).style.bold, "after a reset, the base's colour: the theme's");
  }
  {
    Ansi a;
    a.feed("\x1b[3");
    a.feed("2mgr");
    a.feed("een\xc3");
    a.feed("\xa9");
    Frame f(10, 1);
    rolltui_ansi_draw(a.t, f.f, RolltuiRect{0, 0, 10, 1}, 0, 0, 1, RolltuiStyle{}, 0);
    check(line(a.t, 0) == "green\xc3\xa9" && indexed(f.cell(0, 0).style.fg, 2),
          "a sequence and a character split across feeds are joined");
  }
  {
    Ansi a;
    a.feed("\x1b[38;5;208mo\x1b[38;2;10;20;30mt\x1b[48:2::1:2:3mx\x1b[94mb");
    Frame f(10, 1);
    rolltui_ansi_draw(a.t, f.f, RolltuiRect{0, 0, 10, 1}, 0, 0, 1, RolltuiStyle{}, 0);
    const RolltuiCell t = f.cell(1, 0), x = f.cell(2, 0);
    check(indexed(f.cell(0, 0).style.fg, 208), "256 colours");
    check(static_cast<int>(t.style.fg.kind) == 2 && t.style.fg.r == 10 && t.style.fg.g == 20 && t.style.fg.b == 30, "24-bit colour");
    check(static_cast<int>(x.style.bg.kind) == 2 && x.style.bg.b == 3, "the colon form, with its colour-space slot");
    check(indexed(f.cell(3, 0).style.fg, 12), "a bright colour (94) is the sixteen's upper half");
  }
  {
    Ansi a;
    a.feed("10%\r50%\r100%\nnext\r\nlast");
    check(line(a.t, 0) == "100%" && line(a.t, 1) == "next" && line(a.t, 2) == "last",
          "a carriage return writes the line again; CR LF is a newline");
    a.feed("\x1b[2Kgone\x1b[2Kkept");
    check(line(a.t, 2) == "kept", "erase-in-line clears the line being written");
  }
  {
    Ansi a;
    a.feed("N\bNA\bA_\bm\n");
    Frame f(10, 1);
    rolltui_ansi_draw(a.t, f.f, RolltuiRect{0, 0, 10, 1}, 0, 0, 1, RolltuiStyle{}, 0);
    check(line(a.t, 0) == "NAm" && f.cell(0, 0).style.bold && f.cell(1, 0).style.bold && f.cell(2, 0).style.underline,
          "backspace overstrike, a man page's: X\\bX bold, _\\bX underlined");
  }
  {
    Ansi a;
    a.feed("a\tb\x1b[2Ac\x1b]0;a title\x07" "d\x1bP1$r\x1b\\" "e\x1b(Bf\n");
    check(line(a.t, 0) == "a       bcdef", "a tab reaches the next stop of 8; cursor moves, OSC, DCS and designations are swallowed");
  }
  {
    Ansi a;
    rolltui_ansi_set_limit(a.t, 3);
    a.feed("0\n1\n2\n3\n4\n");
    check(rolltui_ansi_line_count(a.t) == 3 && line(a.t, 0) == "2" && line(a.t, 2) == "4", "a capped text keeps the newest lines");
    for (int i = 5; i < 300; ++i) a.feed(std::to_string(i) + "\n");
    check(rolltui_ansi_line_count(a.t) == 3 && line(a.t, 0) == "297" && line(a.t, 2) == "299",
          "…however many times the ring goes round");
  }
  {
    Ansi a;
    a.feed("abcdefghijklmnopqrstuvwxy\nz\n");
    check(rolltui_ansi_rows(a.t, 10, 0) == 4 && rolltui_ansi_width(a.t) == 25, "rows wrapped at 10, and the widest line's width");
    Frame w(10, 2);
    rolltui_ansi_draw(a.t, w.f, RolltuiRect{0, 0, 10, 2}, 1, 0, 1, RolltuiStyle{}, 0);
    check(w.row(0, 10) == "klmnopqrst" && w.row(1, 10) == "uvwxy", "wrapped, `first` counts rows: the line's second row first");
    Frame c(10, 2);
    rolltui_ansi_draw(a.t, c.f, RolltuiRect{0, 0, 10, 2}, 0, 5, 0, RolltuiStyle{}, 0);
    check(c.row(0, 10) == "fghijklmno" && c.row(1, 10).empty(), "clipped, `left` columns are scrolled off");
  }
  {
    Ansi a;
    a.feed("\x1b[7mr");
    Frame f(4, 1);
    RolltuiStyle base{};
    base.fg.kind = decltype(base.fg.kind)(1);
    base.fg.index = 7;
    base.bg.kind = decltype(base.bg.kind)(1);
    base.bg.index = 0;
    rolltui_ansi_draw(a.t, f.f, RolltuiRect{0, 0, 4, 1}, 0, 0, 1, base, 0);
    check(indexed(f.cell(0, 0).style.fg, 0) && indexed(f.cell(0, 0).style.bg, 7), "reverse trades the colours, the base's included");
  }
}

// Pumps `c` into `t` until it has closed its output and ended, or `ms` pass.
bool drain(RolltuiChild* c, RolltuiAnsiText* t, int ms = 5000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    rolltui_child_pump(c, t, 0);
    if (rolltui_child_drained(c) && rolltui_child_done(c, nullptr)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

RolltuiChild* spawn(std::vector<const char*> argv, unsigned char mode = ROLLTUI_CHILD_PIPES, const char* const* env = nullptr,
                    const char* cwd = nullptr, int cols = 80, int rows = 24, std::string* why = nullptr) {
  argv.push_back(nullptr);
  RolltuiChildOptions o{};
  o.argv = argv.data();
  o.mode = mode;
  o.env = env;
  o.cwd = cwd;
  o.cols = cols;
  o.rows = rows;
  RolltuiStr err;
  RolltuiChild* c = rolltui_child_spawn(&o, &err);
  if (why) *why = std::string(err.data(), err.size());
  return c;
}

void test_children() {
  {
    Ansi a;
    RolltuiChild* c = spawn({"echo", "hello"});
    check(c != nullptr, "echo starts, found on PATH");
    int st = -1;
    check(c && drain(c, a.t) && rolltui_child_done(c, &st) && st == 0 && line(a.t, 0) == "hello",
          "its output is read, it ends with 0 [" + line(a.t, 0) + "]");
    check(rolltui_child_wait_ms(c) == -1, "done and drained: the render need not wake for it");
    rolltui_child_free(c);
  }
  {
    Ansi a;
    RolltuiChild* c = spawn({"sh", "-c", "printf '\\033[32mok\\033[0m\\n'; echo oops >&2; exit 3"});
    int st = -1;
    drain(c, a.t);
    Frame f(10, 2);
    rolltui_ansi_draw(a.t, f.f, RolltuiRect{0, 0, 10, 2}, 0, 0, 1, RolltuiStyle{}, 0);
    check(rolltui_child_done(c, &st) && st == 3, "its exit status");
    check(line(a.t, 0) == "ok" && indexed(f.cell(0, 0).style.fg, 2) && line(a.t, 1) == "oops", "colour kept, stderr in the same stream");
    rolltui_child_free(c);
  }
  {
    std::string why;
    check(spawn({"rolltui-no-such-program"}, ROLLTUI_CHILD_PIPES, nullptr, nullptr, 80, 24, &why) == nullptr &&
              why == "not found: rolltui-no-such-program",
          "a program that is not there is refused by name [" + why + "]");
  }
  {
    Ansi a;
    RolltuiChild* c = spawn({"cat"});
    check(rolltui_child_write(c, "abc\n", 4) == 1, "its input is written");
    rolltui_child_close_input(c);
    check(drain(c, a.t) && line(a.t, 0) == "abc", "…and the end of its input lets it end");
    rolltui_child_free(c);
  }
  {
    Ansi a;
    RolltuiChild* c = spawn({"sh", "-c", "test -t 1 && echo tty || echo pipe; stty size"}, ROLLTUI_CHILD_PTY, nullptr, nullptr, 50, 7);
    drain(c, a.t);
    check(line(a.t, 0) == "tty" && line(a.t, 1) == "7 50", "PTY: the program sees a terminal of the asked size [" + line(a.t, 1) + "]");
    rolltui_child_free(c);
  }
  {
    Ansi a;
    RolltuiChild* c = spawn({"sh", "-c", "read x; stty size"}, ROLLTUI_CHILD_PTY, nullptr, nullptr, 50, 7);
    rolltui_child_resize(c, 30, 5);
    rolltui_child_write(c, "go\n", 3);
    drain(c, a.t);
    bool resized = false;
    for (std::size_t i = 0; i < rolltui_ansi_line_count(a.t); ++i) resized |= line(a.t, i) == "5 30";
    check(resized, "…and resized, it sees the new size");
    rolltui_child_free(c);
  }
  {
    RolltuiChild* c = spawn({"sleep", "30"});
    const auto t0 = std::chrono::steady_clock::now();
    rolltui_child_stop(c);
    int st = -1;
    check(rolltui_child_done(c, &st) && st == 128 + 15, "stopped: SIGTERM ends it, 128 + 15 [" + std::to_string(st) + "]");
    check(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2), "…at once");
    rolltui_child_free(c);
  }
  {
    Ansi a;
    RolltuiChild* c = spawn({"sh", "-c", "i=0; while [ $i -lt 20000 ]; do echo line $i; i=$((i+1)); done"});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rolltui_child_pump(c, a.t, 10);
    const std::size_t first = rolltui_ansi_line_count(a.t);
    check(first >= 10 && first < 20000 && !rolltui_child_done(c, nullptr),
          "a view of 10 lines reads about that much and the program waits on its full pipe [" + std::to_string(first) + "]");
    check(drain(c, a.t, 20000) && rolltui_ansi_line_count(a.t) == 20000 && line(a.t, 19999) == "line 19999",
          "…and the rest is read when more is wanted");
    rolltui_child_free(c);
  }
  {
    char tmpl[] = "/tmp/rolltui_process_XXXXXX";
    const char* dir = mkdtemp(tmpl);
    const char* env[] = {"ROLLTUI_TEST_VAR=from the host", nullptr};
    RolltuiChild* c = spawn({"sh", "-c", "echo \"$ROLLTUI_TEST_VAR\" > here.txt"}, ROLLTUI_CHILD_QUIET, env, dir);
    int st = -1;
    check(c && rolltui_child_wait(c, &st) && st == 0, "QUIET: it runs on the null device, and can be waited for");
    std::ifstream in(std::string(dir) + "/here.txt");
    std::string got;
    std::getline(in, got);
    check(got == "from the host", "…in the folder asked for, with the environment's additions [" + got + "]");
    rolltui_child_free(c);
    std::remove((std::string(dir) + "/here.txt").c_str());
    rmdir(dir);
  }
  {
    RolltuiStr err;
    check(rolltui_open_with_system("/rolltui/no/such/file", 22, &err) == 0 && err.size() > 0,
          "the system's opener refuses a file that is not there, and says so [" + std::string(err.data(), err.size()) + "]");
  }
}

// What the master side of a pty has been sent, waiting up to `ms` for more.
std::string read_master(int master, int ms = 300) {
  std::string out;
  for (;;) {
    pollfd p{master, POLLIN, 0};
    if (::poll(&p, 1, ms) <= 0) break;
    char buf[4096];
    const ssize_t n = ::read(master, buf, sizeof buf);
    if (n <= 0) break;
    out.append(buf, static_cast<std::size_t>(n));
    ms = 50;
  }
  return out;
}

void test_clipboard_title_cwd() {
  char tmpl[] = "/tmp/rolltui_clip_XXXXXX";
  const char* dir = mkdtemp(tmpl);
  const std::string file = std::string(dir) + "/clip.txt";
  const std::string cmd = "cat > '" + file + "'";
  setenv("ROLLTUI_CLIPBOARD", cmd.c_str(), 1);
  check(rolltui_clipboard_copy(nullptr, "copied text", 11) == 1, "with no terminal, the native tool takes it");
  {
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    check(ss.str() == "copied text", "…which `$ROLLTUI_CLIPBOARD` names, fed the text [" + ss.str() + "]");
  }
  int master = -1, slave = -1;
  winsize ws{};
  ws.ws_row = 10;
  ws.ws_col = 40;
  check(openpty(&master, &slave, nullptr, nullptr, &ws) == 0, "openpty");
  RolltuiTerminalOptions o{};
  o.alt_screen = 0;
  o.mouse = 0;
  o.bracketed_paste = 0;
  o.no_probe = 1;
  o.no_cache = 1;
  RolltuiTerminal* t = rolltui_terminal_new(slave, slave, o);
  read_master(master);
  rolltui_clipboard_copy(t, "hi", 2);
  const std::string osc52 = read_master(master);
  check(osc52.find("\x1b]52;c;aGk=\x07") != std::string::npos, "OSC 52 carries the text in base64");
  std::string big(60000, 'x');
  rolltui_clipboard_copy(t, big.data(), big.size());
  check(read_master(master).find("\x1b]52") == std::string::npos, "…and is not sent above 74994 encoded bytes");
  setenv("TMUX", "/tmp/tmux-test,1,0", 1);
  rolltui_clipboard_copy(t, "hi", 2);
  const std::string tmux = read_master(master);
  check(tmux.find("\x1bPtmux;\x1b\x1b]52;c;aGk=\x07\x1b\\") != std::string::npos, "inside tmux it is passed through, its ESC doubled");
  unsetenv("TMUX");
  rolltui_terminal_set_title(t, "my \x07title", 10);
  const std::string title = read_master(master);
  check(title.find("\x1b[22;0t\x1b]2;my title\x1b\\") != std::string::npos,
        "the title: the terminal's own pushed first, a control character dropped");
  rolltui_terminal_set_title(t, "my \x07title", 10);
  check(read_master(master, 100).empty(), "…and only a change is written");
  rolltui_terminal_report_cwd(t, "/tmp/a b", 8);
  const std::string cwd = read_master(master);
  check(cwd.find("\x1b]7;file://") != std::string::npos && cwd.find("/tmp/a%20b\x1b\\") != std::string::npos,
        "OSC 7 reports the folder, percent-encoded [" + cwd.substr(0, 40) + "]");
  // GNU SCREEN passes OSC 52 through in pieces, each its own DCS
  setenv("STY", "1234.pts-0.host", 1);
  rolltui_clipboard_copy(t, "hi", 2);
  check(read_master(master).find("\x1bP\x1b]52;c;aGk=\x07\x1b\\") != std::string::npos, "inside GNU screen OSC 52 is passed through as a DCS");
  {
    const std::string long_text(600, 'y'); /* 800 base64 bytes: two pieces */
    rolltui_clipboard_copy(t, long_text.data(), long_text.size());
    const std::string seen = read_master(master);
    check(seen.find("\x1b\\\x1bP") != std::string::npos && seen.find("\x07\x1b\\") != std::string::npos,
          "…in pieces screen passes on as they come, the long one too");
  }
  unsetenv("STY");
  // KITTY takes OSC 52 of any size
  setenv("KITTY_WINDOW_ID", "1", 1);
  {
    std::string seen; /* drained as it is written: 80 KB is more than a pty holds unread */
    std::atomic<bool> stop{false};
    std::thread drain([&] {
      char b[8192];
      while (!stop.load()) {
        pollfd pf{master, POLLIN, 0};
        if (::poll(&pf, 1, 20) > 0) {
          const ssize_t k = ::read(master, b, sizeof b);
          if (k > 0) seen.append(b, static_cast<std::size_t>(k));
        }
      }
    });
    rolltui_clipboard_copy(t, big.data(), big.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;
    drain.join();
    check(seen.find("\x1b]52;c;") != std::string::npos && seen.size() > 80000,
          "drawn on kitty, a copy past 74994 encoded bytes is still sent [" + std::to_string(seen.size()) + " bytes]");
  }
  unsetenv("KITTY_WINDOW_ID");
  // READING the clipboard: a tool's answer, else what was last copied here
  setenv("ROLLTUI_CLIPBOARD_PASTE", "printf 'from the tool'", 1);
  RolltuiStr pasted{};
  check(rolltui_clipboard_paste(t, &pasted) == 1 && std::string(pasted.p ? pasted.p : "", pasted.n) == "from the tool",
        "the clipboard is read through the tool that answers");
  setenv("ROLLTUI_CLIPBOARD_PASTE", "exit 1", 1);
  rolltui_clipboard_copy(t, "copied here", 11);
  read_master(master);
  check(rolltui_clipboard_paste(t, &pasted) == 1 && std::string(pasted.p ? pasted.p : "", pasted.n) == "copied here",
        "…and where no tool can (over ssh), what this program last copied");
  rolltui_str_free(&pasted);
  unsetenv("ROLLTUI_CLIPBOARD_PASTE");
  // THE WINDOW'S NAME IN TMUX: the escape where tmux takes it, its command where not, the old name put back on leaving
  const std::string fake = std::string(dir) + "/bin";
  mkdir(fake.c_str(), 0700);
  const std::string tlog = std::string(dir) + "/tmux.log";
  {
    std::ofstream sh(fake + "/tmux");
    sh << "#!/bin/sh\necho \"$*\" >> \"$TMUX_LOG\"\ncase \"$1\" in\n  display-message) echo oldname ;;\n"
          "  show-window-options) case \"$*\" in *allow-rename*) echo \"$FAKE_ALLOW\" ;; *automatic-rename*) echo on ;; esac ;;\nesac\nexit 0\n";
  }
  chmod((fake + "/tmux").c_str(), 0700);
  const std::string old_path = getenv("PATH") ? getenv("PATH") : "";
  setenv("PATH", (fake + ":" + old_path).c_str(), 1);
  setenv("TMUX_LOG", tlog.c_str(), 1);
  setenv("TMUX", "/tmp/tmux-test,1,0", 1);
  setenv("TMUX_PANE", "%3", 1);
  setenv("FAKE_ALLOW", "on", 1);
  check(rolltui_terminal_set_window_name(t, "dirk", 4) == 1 && read_master(master).find("\x1bkdirk\x1b\\") != std::string::npos,
        "inside tmux the window is named by the escape tmux takes for it");
  setenv("FAKE_ALLOW", "off", 1);
  check(rolltui_terminal_set_window_name(t, "dirk 2", 6) == 2, "…and with allow-rename off, by tmux's own command, not refused unsaid");
  rolltui_terminal_free(t); /* still inside tmux, as a program leaving is */
  unsetenv("TMUX");
  {
    std::ifstream in(tlog);
    const std::string log((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(log.find("rename-window -t %3 dirk 2") != std::string::npos, "(the command named the window) [" + log + "]");
    check(log.find("rename-window -t %3 oldname") != std::string::npos && log.find("set-window-option -t %3 automatic-rename on") != std::string::npos,
          "leaving puts back the name the window had, and tmux naming it by itself");
  }
  setenv("PATH", old_path.c_str(), 1);
  unsetenv("TMUX_LOG");
  unsetenv("TMUX_PANE");
  unsetenv("FAKE_ALLOW");
  std::remove(tlog.c_str());
  std::remove((fake + "/tmux").c_str());
  rmdir(fake.c_str());
  check(read_master(master).find("\x1b[23;0t") != std::string::npos, "leaving puts the terminal's own title back");
  ::close(master);
  ::close(slave);
  unsetenv("ROLLTUI_CLIPBOARD");
  std::remove(file.c_str());
  rmdir(dir);
}

void test_output_window() {
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
  static const char kScreen[] = R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "log", "content": "output:build" } ] } })";
  RolltuiLayout* l = rolltui_load_layout_text(kScreen, sizeof kScreen - 1, defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(ctx());
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  Ansi a;
  rolltui_windows_bind_output(w, "build", 5, a.t);
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  auto paint = [&](std::vector<std::string>& rows) {
    const RolltuiRect area{0, 0, 20, 4};
    RolltuiWidgetEnv env{};
    rolltui_context_set_env(ctx(), &env);
    rolltui_windows_sync(w, stack);
    rolltui_windows_layout(w, stack, area);
    Frame f(20, 4);
    struct Slot {
      RolltuiWindows* w;
      RolltuiStyle* s;
    } slot{w, styles};
    rolltui_window_stack_compose(
        stack, f.f, area, styles, rolltui_layout_default_roles(),
        [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* s = static_cast<Slot*>(c);
          rolltui_windows_draw(s->w, rn, fr, s->s, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    rows.clear();
    for (int y = 0; y < 4; ++y) rows.push_back(f.row(y, 20));
  };
  std::vector<std::string> rows;
  for (int i = 0; i < 10; ++i) a.feed("line " + std::to_string(i) + "\n");
  paint(rows);
  check(rows[3].find("line 9") != std::string::npos && rows[0].find("line 6") != std::string::npos,
        "an output window shows the end of what was written [" + rows[0] + " .. " + rows[3] + "]");
  a.feed("line 10\n");
  paint(rows);
  check(rows[3].find("line 10") != std::string::npos, "…and follows it as it grows");
  RolltuiEvent up{};
  up.kind = ROLLTUI_EVENT_MOUSE;
  up.mouse.kind = RolltuiMouseEvent::Kind::WheelUp;
  rolltui_windows_handle(w, "log", 3, &up);
  paint(rows);
  const std::string held = rows[0];
  a.feed("line 11\n");
  paint(rows);
  check(rows[0] == held && rows[3].find("line 11") == std::string::npos, "scrolled back, it holds still while the program writes");
  RolltuiEvent end{};
  end.kind = ROLLTUI_EVENT_KEY;
  end.key.key = ROLLTUI_KEY_END;
  rolltui_windows_handle(w, "log", 3, &end);
  a.feed("line 12\n");
  paint(rows);
  check(rows[3].find("line 12") != std::string::npos, "back at the end, it follows again");
  rolltui_compose_scratch_free(cs);
  rolltui_effect_map_free(fx);
  rolltui_window_stack_free(stack);
  rolltui_windows_free(w);
  rolltui_layout_free(l);
}

// PROGRAMS RUN INTO OUTPUT WINDOWS: sized to the window, merged a line at a time, stopped with their window; a view's ways
struct OutScreen {
  RolltuiLayout* l = nullptr;
  RolltuiWindows* w = nullptr;
  RolltuiWindowStack* stack = nullptr;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = nullptr;
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  int width, height;
  OutScreen(const char* layout, int wd, int ht) : width(wd), height(ht) {
    w = rolltui_windows_new(ctx());
    stack = rolltui_window_stack_new();
    fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    load(layout);
  }
  void load(const char* layout) {
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
    RolltuiLayout* next = rolltui_load_layout_text(layout, std::strlen(layout), defaults, nd, rolltui_layout_default_hooks(), &rep);
    rolltui_layout_report_release(&rep);
    rolltui_window_stack_set_base(stack, rolltui_layout_base(next));
    if (l) rolltui_layout_free(l);
    l = next;
  }
  OutScreen(const OutScreen&) = delete;
  ~OutScreen() {
    rolltui_compose_scratch_free(cs);
    rolltui_effect_map_free(fx);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(w);
    rolltui_layout_free(l);
  }
  std::vector<std::string> paint(Frame** keep = nullptr) {
    const RolltuiRect area{0, 0, width, height};
    RolltuiWidgetEnv env{};
    rolltui_context_set_env(ctx(), &env);
    rolltui_windows_sync(w, stack);
    rolltui_windows_autosize(w, stack, area);
    rolltui_windows_layout(w, stack, area);
    Frame* f = new Frame(width, height);
    struct Slot {
      RolltuiWindows* w;
      RolltuiStyle* s;
    } slot{w, styles};
    rolltui_window_stack_compose(
        stack, f->f, area, styles, rolltui_layout_default_roles(),
        [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* sl = static_cast<Slot*>(c);
          rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    std::vector<std::string> rows;
    for (int y = 0; y < height; ++y) rows.push_back(f->row(y, width));
    if (keep) *keep = f;
    else delete f;
    return rows;
  }
  /* painted until `done` says so, or the time runs out */
  std::vector<std::string> paint_until(const std::function<bool(const std::vector<std::string>&)>& done) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(5000 * rolltui_test::kClockScale));
    std::vector<std::string> rows = paint();
    while (!done(rows) && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      rows = paint();
    }
    return rows;
  }
};

static std::string joined(const std::vector<std::string>& rows) {
  std::string all;
  for (const std::string& r : rows) all += r + "\n";
  return all;
}

void test_output_programs() {
  // A PROGRAM ON A PTY THAT WROTE AND NO ONE READ: stopping it never waits for ever (macOS holds its exit until the pty drains)
  {
    const char* argv[] = {"/bin/sh", "-c", "i=0; while [ $i -lt 200 ]; do echo unread line $i; i=$((i+1)); done; exit 0", nullptr};
    RolltuiChildOptions o{};
    o.argv = argv;
    o.mode = ROLLTUI_CHILD_PTY;
    RolltuiChild* c = rolltui_child_spawn(&o, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto t0 = std::chrono::steady_clock::now();
    rolltui_child_free(c);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(c != nullptr && ms < static_cast<long long>(3000 * rolltui_test::kClockScale),
          "a program on a pseudo-terminal whose output no one read is let go at once [" + std::to_string(ms) + " ms]");
  }
  // ONE PROGRAM, on a pty the window's size; the footer says how it ended
  {
    OutScreen s(R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "log", "content": "output:run", "border": "rounded" } ] } })", 40, 8);
    s.paint();
    const char* argv[] = {"/bin/sh", "-c", "echo \"size $(stty size)\"; echo [ -t 1 ] > /dev/null; [ -t 1 ] && echo tty; echo done", nullptr};
    RolltuiChildOptions o{};
    o.argv = argv;
    o.mode = ROLLTUI_CHILD_PTY;
    RolltuiStr err{};
    check(rolltui_windows_run_output(s.w, "run", 3, &o, nullptr, 0, &err) == 1, "a program runs into an output window [" + std::string(err.p ? err.p : "", err.n) + "]");
    rolltui_str_free(&err);
    std::vector<std::string> rows = s.paint_until([&](const std::vector<std::string>&) { return !rolltui_windows_output_running(s.w, "run", 3, nullptr); });
    rows = s.paint();
    const std::string all = joined(rows);
    check(all.find("size 6 36") != std::string::npos && all.find("tty") != std::string::npos,
          "…on a pseudo-terminal the size of the window's content, inside its border and padding [" + all + "]");
    int status = -9;
    check(!rolltui_windows_output_running(s.w, "run", 3, &status) && status == 0, "…and it ended, 0 [" + std::to_string(status) + "]");
    check(rows.back().find("exited 0") != std::string::npos && rows.back().find("following") != std::string::npos,
          "its footer says it is following, and how the program ended [" + rows.back() + "]");
    rolltui_windows_output_mark(s.w, "run", 3, "again", 5);
    rows = s.paint();
    bool rule = false;
    for (const std::string& r : rows) rule = rule || (r.find("again") != std::string::npos && r.find("\xe2\x94\x80\xe2\x94\x80") != std::string::npos);
    check(rule, "a mark is a rule across the window with its label");
  }
  // TWO PROGRAMS MERGED: each line whole, led by its program's label
  {
    OutScreen s(R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "log", "content": "output:both" } ] } })", 40, 10);
    s.paint();
    const char* web[] = {"/bin/sh", "-c", "printf 'GET /\\n'; sleep 0.05; printf 'GET /a'; sleep 0.1; printf 'bout\\n'", nullptr};
    const char* db[] = {"/bin/sh", "-c", "sleep 0.02; printf 'SELECT 1\\n'; sleep 0.05; printf 'SELECT 2\\n'", nullptr};
    RolltuiChildOptions a{}, b{};
    a.argv = web;
    b.argv = db;
    rolltui_windows_run_output(s.w, "both", 4, &a, "web", ROLLTUI_ROLE_ACCENT_1, nullptr);
    rolltui_windows_run_output(s.w, "both", 4, &b, "db", ROLLTUI_ROLE_ACCENT_2, nullptr);
    s.paint_until([&](const std::vector<std::string>&) { return !rolltui_windows_output_running(s.w, "both", 4, nullptr); });
    const std::vector<std::string> rows = s.paint();
    const std::string all = joined(rows);
    check(all.find("web GET /about") != std::string::npos && all.find("db  SELECT 1") != std::string::npos &&
              all.find("db  SELECT 2") != std::string::npos,
          "two programs merged into one window: each line whole, led by its program's label [" + all + "]");
  }
  // A HOST'S TEXT: each line's time in a gutter, a filter that keeps the lines it matches and lights the matches
  {
    OutScreen s(R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "log", "content": "output:log" } ] } })", 40, 6);
    Ansi text;
    rolltui_windows_bind_output(s.w, "log", 3, text.t);
    const unsigned long long at = 1700000000000ULL;
    rolltui_ansi_set_stamp(text.t, at, 0);
    text.feed("alpha one\nbeta two\nalpha three\n");
    RolltuiOutputOptions oo{};
    oo.timestamps = 1;
    oo.filter = "alpha";
    rolltui_windows_set_output_options(s.w, "log", 3, &oo);
    Frame* f = nullptr;
    const std::vector<std::string> rows = s.paint(&f);
    struct tm tm {};
    const time_t secs = static_cast<time_t>(at / 1000ULL);
    localtime_r(&secs, &tm);
    char hms[16];
    std::strftime(hms, sizeof hms, "%H:%M:%S", &tm);
    const std::string all = joined(rows);
    check(rows[0] == std::string(hms) + " alpha one" && rows[1] == std::string(hms) + " alpha three" && all.find("beta") == std::string::npos,
          "timestamps lead each line, and a filter keeps only the lines it matches [" + all + "]");
    const RolltuiCell lit = f->cell(9, 0), plain = f->cell(15, 0);
    check(lit.style.bold && lit.style.underline && !plain.style.underline, "…its matches bold and underlined, the rest as it was");
    delete f;
  }
  // THE WINDOW GONE: the program it showed is stopped
  {
    OutScreen s(R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "log", "content": "output:long" } ] } })", 40, 6);
    s.paint();
    const char* argv[] = {"/bin/sh", "-c", "echo started; exec sleep 30", nullptr};
    RolltuiChildOptions o{};
    o.argv = argv;
    rolltui_windows_run_output(s.w, "long", 4, &o, nullptr, 0, nullptr);
    s.paint_until([&](const std::vector<std::string>& rows) { return joined(rows).find("started") != std::string::npos; });
    check(rolltui_windows_output_running(s.w, "long", 4, nullptr) == 1, "a program runs while its window shows it");
    s.load(R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "other", "content": "text:nothing here" } ] } })");
    s.paint();
    int status = 0;
    check(rolltui_windows_output_running(s.w, "long", 4, &status) == 0 && status == -1, "…and is stopped when that window leaves the layout");
  }
  // NO PSEUDO-TERMINAL: pipes, the size in COLUMNS and LINES
  {
    OutScreen s(R"({ "resizable": false, "name": "o", "root": { "column": [ { "id": "log", "content": "output:pipe" } ] } })", 40, 6);
    s.paint();
    rolltui_os_child_refuse_pty(1);
    const char* argv[] = {"/bin/sh", "-c", "echo \"$COLUMNS x $LINES\"; [ -t 1 ] && echo tty || echo pipe", nullptr};
    RolltuiChildOptions o{};
    o.argv = argv;
    o.mode = ROLLTUI_CHILD_PTY;
    check(rolltui_windows_run_output(s.w, "pipe", 4, &o, nullptr, 0, nullptr) == 1, "a program asking for a pseudo-terminal there is none of still starts");
    rolltui_os_child_refuse_pty(0);
    s.paint_until([&](const std::vector<std::string>&) { return !rolltui_windows_output_running(s.w, "pipe", 4, nullptr); });
    const std::string all = joined(s.paint());
    check(all.find("40 x 6") != std::string::npos && all.find("pipe") != std::string::npos,
          "…on pipes, told the window's size in COLUMNS and LINES [" + all + "]");
  }
}

}  // namespace

// A SIGTERM TO THIS PROGRAM WHILE IT HAS HANDED A PROGRAM THE TERMINAL reaches that program, and then ends this one
static void test_term_passed_on() {
  char tmpl[] = "/tmp/rolltui_term_XXXXXX";
  const std::string dir = mkdtemp(tmpl);
  const std::string mark = dir + "/got";
  const std::string script = "trap 'echo term > \"" + mark + "\"; exit 7' TERM; echo ready > \"" + mark + ".ready\"; while :; do sleep 0.05; done";
  const pid_t app = fork();
  if (app == 0) {
    const char* argv[] = {"/bin/sh", "-c", script.c_str(), nullptr};
    int st = -1;
    const int devnull = open("/dev/null", O_RDWR);
    rolltui_os_child_wait_terminal(argv, nullptr, devnull, devnull, &st, nullptr);
    _exit(50); /* not reached: the SIGTERM is raised again once the program has ended */
  }
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(5000 * rolltui_test::kClockScale));
  struct stat sb {};
  while (stat((mark + ".ready").c_str(), &sb) != 0 && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  kill(app, SIGTERM);
  int st = 0;
  pid_t got = 0;
  while ((got = waitpid(app, &st, WNOHANG)) == 0 && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  if (got == 0) {
    kill(app, SIGKILL);
    waitpid(app, &st, 0);
  }
  std::ifstream in(mark);
  std::string word;
  in >> word;
  check(word == "term", "a SIGTERM while a program has the terminal is passed on to it [" + word + "]");
  check(got == app && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM, "…and once it has ended, this program ends by the same signal");
  std::remove(mark.c_str());
  std::remove((mark + ".ready").c_str());
  rmdir(dir.c_str());
}

int main() {
  test_term_passed_on();
  rolltui_context_set_library_defaults(ctx());
  test_ansi();
  test_children();
  test_clipboard_title_cwd();
  test_output_window();
  test_output_programs();
  return testkit::report("process_test");
}
