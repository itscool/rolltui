//
// terminal_test.cpp — Terminal on a real pty pair: the mode bytes it writes, the
// termios flags it sets and restores, the size it reads, events decoded from bytes
// written to the master, SIGWINCH → Resize, and — in a forked child killed by
// SIGTERM — the restore bytes reaching the master before the child dies. Real
// processes and real signals, the way roll's manager_test does it.
//
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_keys.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_terminal.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_termfacts.h"  // INTERNAL: this test opts in

using namespace rolltui_test;
using namespace testkit;

namespace {

// Reads from `fd` until `want` has been seen or `ms` elapse; returns what arrived.
std::string read_until(int fd, const std::string& want, int ms) {
  std::string got;
  int waited = 0;
  while (waited <= ms) {
    pollfd p{fd, POLLIN, 0};
    int r = ::poll(&p, 1, 20);
    if (r > 0 && (p.revents & POLLIN)) {
      char buf[1024];
      ssize_t k = ::read(fd, buf, sizeof buf);
      if (k > 0) got.append(buf, static_cast<std::size_t>(k));
      if (k <= 0 && !(p.revents & POLLIN)) break;
    } else {
      waited += 20;
    }
    if (!want.empty() && got.find(want) != std::string::npos) break;
  }
  return got;
}

// term_event_to_string mirrors rolltui::to_string(Event) (rolltui/Keys.cpp) one level
// down, over RolltuiTermEvent directly — the same display vocabulary keys_test.cpp
// reproduces for RolltuiEvent, plus the RESIZE kind only the terminal ever produces
// (rolltui/c/rolltui_terminal.h rule 4).
std::string term_event_to_string(const RolltuiTermEvent& e) {
  // THE LIBRARY'S TitleCase names, not a hand-copy of them. There were three
  // copies of this 28-entry table and no source: the lowercase half was already in C, the
  // TitleCase half was in `Keys.cpp`, and nothing said the two spellings were deliberate.
  static const char* mouse_kinds[] = {"Press", "Release", "Drag", "Move", "WheelUp", "WheelDown", "WheelLeft", "WheelRight", "DoubleClick"};
  switch (e.kind) {
    case ROLLTUI_TERM_EVENT_MOUSE: {
      std::string s = "Mouse ";
      if (e.mouse.ctrl) s += "Ctrl+";
      if (e.mouse.alt) s += "Alt+";
      if (e.mouse.shift) s += "Shift+";
      s += mouse_kinds[static_cast<int>(e.mouse.kind)];
      if (e.mouse.button) s += " " + std::to_string(e.mouse.button);
      return s + " @" + std::to_string(e.mouse.x) + "," + std::to_string(e.mouse.y);
    }
    case ROLLTUI_TERM_EVENT_PASTE:
      return "Paste(" + std::to_string(e.text_len) + " bytes)";
    case ROLLTUI_TERM_EVENT_RESIZE:
      return "Resize " + std::to_string(e.w) + "x" + std::to_string(e.h);
    default: {
      std::string s;
      if (e.key.ctrl) s += "Ctrl+";
      if (e.key.alt) s += "Alt+";
      if (e.key.shift) s += "Shift+";
      if (e.key.key == ROLLTUI_KEY_CHAR) {
        char buf[4];
        const std::size_t n = rolltui_u_append_utf8(e.key.ch, buf);
        s.append(buf, n);
      } else {
        s += rolltui_key_display_name(static_cast<unsigned char>(e.key.key), nullptr);
      }
      if (e.key.key == ROLLTUI_KEY_UNKNOWN) s += "(" + std::string(e.text ? e.text : "", e.text ? e.text_len : 0) + ")";
      return s;
    }
  }
}

void collect_term(void* ctx, const RolltuiTermEvent* e) {
  static_cast<std::vector<std::string>*>(ctx)->push_back(term_event_to_string(*e));
}

// Polls and joins the events the same way rolltui::to_string(Event) joined a
// std::vector<Event> in the original — " | " between them, "" for none.
std::string names(RolltuiTerminal* t, int timeout_ms) {
  std::vector<std::string> ev;
  rolltui_terminal_poll(t, timeout_ms, collect_term, &ev);
  std::string s;
  for (const std::string& x : ev) s += (s.empty() ? "" : " | ") + x;
  return s;
}

std::string_view sequence_of(const char* (*get)(const RolltuiTerminal*, std::size_t*), const RolltuiTerminal* t) {
  std::size_t n = 0;
  const char* p = get(t, &n);
  return {p, n};
}

}  // namespace


// ============================ A FAKE TERMINAL ============================
// What the library asks a terminal and what a terminal says back, on a real pty pair. The child
// process HOLDS THE MASTER and answers each question it recognises, in the order asked, from a
// script; the parent is the application, on the slave, exactly as in production. (The library's
// exchange blocks until Primary DA arrives, so the answers cannot come from the same process.)
struct Script {
  bool answers = true;                       // false: a terminal that says nothing at all
  std::string kitty, modkeys;                // replies to `CSI ? u` and `CSI ? 4 m`
  std::string bg;                            // the reply to OSC 11
  std::string bg_later;                      // ...to the SECOND OSC 11 (the background re-check), if different
  std::string sgr;                           // the DCS reply to DECRQSS
  std::string version;                       // the DCS reply to XTVERSION
  int cpr_col = 2;                           // where the cursor is after the glyph; 0: no answer
  std::string da1 = "\x1b[?1;2c";
  std::string after;                         // written on its own 150 ms after the first DA1 is answered (a LATE reply)
  int idle_ms = 350;                         // how long the terminal lingers once nothing more is coming
};

struct Heard {
  std::string bytes;  // everything the library wrote to the terminal, in order
  bool asked_da() const { return bytes.find("\x1b[c") != std::string::npos; }
  bool asked(const std::string& what) const { return bytes.find(what) != std::string::npos; }
  std::size_t count(const std::string& what) const {
    std::size_t n = 0, at = 0;
    while ((at = bytes.find(what, at)) != std::string::npos) { ++n; at += what.size(); }
    return n;
  }
};

void fake_serve(int master, int report_fd, const Script& sc);

struct FakeTerminal {
  int master = -1, slave = -1;
  int report[2] = {-1, -1};
  pid_t child = -1;

  bool start(const Script& sc) {
    winsize ws{};
    ws.ws_row = 24;
    ws.ws_col = 80;
    if (openpty(&master, &slave, nullptr, nullptr, &ws) != 0) return false;
    if (::pipe(report) != 0) return false;
    child = ::fork();
    if (child == 0) {
      ::close(slave);
      ::close(report[0]);
      fake_serve(master, report[1], sc);
      ::close(report[1]);
      ::_exit(0);
    }
    ::close(master);
    ::close(report[1]);
    master = -1;
    return child > 0;
  }

};

// The child's loop, as a free function so the struct above can stay a plain description.
void fake_serve(int master, int report_fd, const Script& sc) {
  std::string seen;
  std::size_t processed = 0;
  int osc11_queries = 0;
  bool first_da_answered = false;
  auto ms_now = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  long long quiet_since = ms_now(), after_due = -1;
  bool after_sent = sc.after.empty();
  for (;;) {
    pollfd p{master, POLLIN, 0};
    const int r = ::poll(&p, 1, 20);
    if (r > 0 && (p.revents & POLLIN)) {
      char b[1024];
      const ssize_t k = ::read(master, b, sizeof b);
      if (k <= 0) break;
      seen.append(b, static_cast<std::size_t>(k));
      quiet_since = ms_now();
      // Every question found in what just arrived, in the order it was asked.
      struct Q { std::size_t at; const char* tok; std::size_t len; };
      static const Q qs[] = {{0, "\x1b[?u", 4}, {0, "\x1b[?4m", 5}, {0, "\x1b]11;?\x1b\\", 8}, {0, "\x1bP$qm\x1b\\", 6},
                             {0, "\x1b[>0q", 5}, {0, "\x1b[6n", 4}, {0, "\x1b[c", 3}};
      std::vector<std::pair<std::size_t, int>> found;
      for (int qi = 0; qi < 7; ++qi) {
        std::size_t at = processed;
        while ((at = seen.find(qs[qi].tok, at)) != std::string::npos) {
          found.emplace_back(at, qi);
          at += qs[qi].len;
        }
      }
      std::sort(found.begin(), found.end());
      std::string out;
      for (const auto& f : found) {
        switch (f.second) {
          case 0: out += sc.kitty; break;
          case 1: out += sc.modkeys; break;
          case 2: out += (++osc11_queries >= 2 && !sc.bg_later.empty()) ? sc.bg_later : sc.bg; break;
          case 3: out += sc.sgr; break;
          case 4: out += sc.version; break;
          case 5: if (sc.cpr_col > 0) out += "\x1b[1;" + std::to_string(sc.cpr_col) + "R"; break;
          case 6:
            out += sc.da1;
            if (!first_da_answered) {
              first_da_answered = true;
              after_due = ms_now() + 150;
            }
            break;
        }
      }
      processed = seen.size();
      if (sc.answers && !out.empty()) (void)!::write(master, out.data(), out.size());
    }
    if (!after_sent && after_due > 0 && ms_now() >= after_due) {
      (void)!::write(master, sc.after.data(), sc.after.size());
      after_sent = true;
      quiet_since = ms_now();
    }
    if (ms_now() - quiet_since > sc.idle_ms && after_sent) break;
  }
  (void)!::write(report_fd, seen.data(), seen.size());
}

// Reads what the child heard, waits for it, and releases everything.
Heard finish(FakeTerminal& ft) {
  Heard h;
  char b[4096];
  ssize_t k;
  while ((k = ::read(ft.report[0], b, sizeof b)) > 0) h.bytes.append(b, static_cast<std::size_t>(k));
  ::close(ft.report[0]);
  int status = 0;
  ::waitpid(ft.child, &status, 0);
  if (ft.slave >= 0) ::close(ft.slave);
  return h;
}

// Sets exactly the variables the library reads about a terminal, and none of the person's own.
void set_env(const std::vector<std::pair<const char*, const char*>>& vars) {
  for (const char* name : {"COLORTERM", "TERM", "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "LC_TERMINAL", "LC_TERMINAL_VERSION",
                           "COLORFGBG", "ROLL_COLOR_DEPTH", "ROLL_AMBIGUOUS_WIDE", "ROLL_TERM_PROBE", "TMUX", "STY", "SSH_CONNECTION"})
    ::unsetenv(name);
  for (const auto& v : vars) ::setenv(v.first, v.second, 1);
}

std::vector<std::string> g_scratch_dirs;  // every directory this run made, removed when it ends

std::string make_dir(const char* tag) {
  std::string t = std::string("/tmp/rolltui_terminal_") + tag + "_XXXXXX";
  char* p = ::mkdtemp(t.data());
  if (p) g_scratch_dirs.push_back(p);
  return p ? std::string(p) : std::string();
}

bool file_exists(const std::string& path) { return ::access(path.c_str(), F_OK) == 0; }

double g_new_ms = 0;  // how long `rolltui_terminal_new` took the last time, in milliseconds

// A terminal on the fake's slave. `body` runs while it is alive; what the fake heard is returned.
template <class Body>
Heard with_terminal(const Script& sc, const std::vector<std::pair<const char*, const char*>>& env, RolltuiTerminalOptions o, Body body) {
  FakeTerminal ft;
  Heard none;
  if (!ft.start(sc)) return none;
  set_env(env);
  o.handle_signals = false;  // this is a test process, not an application
  const auto t0 = std::chrono::steady_clock::now();
  RolltuiTerminal* t = rolltui_terminal_new(ft.slave, ft.slave, o);
  g_new_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  body(t);
  rolltui_terminal_free(t);
  return finish(ft);
}

const Script kGhostty = [] {
  Script s;
  s.kitty = "\x1b[?0u";
  s.bg = "\x1b]11;rgb:1e1e/1e1e/2e2e\x1b\\";
  s.sgr = "\x1bP1$r0;48:2::1:2:3m\x1b\\";
  s.version = "\x1bP>|ghostty 1.1.3\x1b\\";
  s.cpr_col = 2;
  return s;
}();

// Apple Terminal on macOS 15: answers Primary DA and nothing else.
const Script kAppleSequoia = [] {
  Script s;
  s.cpr_col = 0;
  return s;
}();

RolltuiTermFacts facts_of(RolltuiTerminal* t) {
  RolltuiTermFacts f;
  rolltui_terminal_facts(t, &f);
  return f;
}

long long ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}


int main() {
  // NOTHING HERE MAY TOUCH THE PERSON'S OWN CONFIGURATION: a terminal remembers what it learned in the
  // rolltui config directory, and a test run that left "this terminal is silent" there would be
  // believed by the next real launch. The legacy checks below also opt out of remembering, because
  // they are about what the terminal does when it ASKS.
  const std::string config_dir = make_dir("config");
  ::setenv("ROLL_CONFIG_DIR", config_dir.c_str(), 1);
  int master = -1, slave = -1;
  winsize ws{};
  ws.ws_row = 24;
  ws.ws_col = 80;
  check(openpty(&master, &slave, nullptr, nullptr, &ws) == 0, "openpty");
  if (master < 0) return report("rolltui_terminal_test");

  // ---- in-process: modes, termios, size, events ----------------------------------
  std::string enter, leave;
  {
    RolltuiTerminalOptions o;
    o.no_cache = true;
    o.handle_signals = false;  // the parent must keep its own dispositions for the fork test
    RolltuiTerminal* t = rolltui_terminal_new(slave, slave, o);
    enter = std::string(sequence_of(rolltui_terminal_enter_sequence, t));
    leave = std::string(sequence_of(rolltui_terminal_leave_sequence, t));
    check(rolltui_terminal_is_tty(t) != 0, "the slave is a tty");
    check(rolltui_terminal_width(t) == 80 && rolltui_terminal_height(t) == 24,
          "size read from the pty (" + std::to_string(rolltui_terminal_width(t)) + "x" + std::to_string(rolltui_terminal_height(t)) + ")");
    std::string got = read_until(master, enter, 500);
    check(got.find("\x1b[?1049h") != std::string::npos && got.find("\x1b[?1006h") != std::string::npos &&
              got.find("\x1b[?2004h") != std::string::npos && got.find("\x1b[?25l") != std::string::npos,
          "enter: alt screen, SGR mouse, bracketed paste, cursor hidden");
    termios tio{};
    check(tcgetattr(slave, &tio) == 0 && !(tio.c_lflag & ICANON) && !(tio.c_lflag & ECHO) && !(tio.c_lflag & ISIG),
          "raw mode: ICANON, ECHO and ISIG off (Ctrl-C is a key)");
    // HANDING THE TERMINAL TO A CHILD AND TAKING IT BACK: suspend is leave without free — the
    // tty is cooked again and the alternate screen left — and resume is enter again.
    rolltui_terminal_suspend(t);
    got = read_until(master, leave, 500);
    check(got.find("\x1b[?1049l") != std::string::npos && tcgetattr(slave, &tio) == 0 && (tio.c_lflag & ICANON) && (tio.c_lflag & ECHO),
          "suspend: the alternate screen is left and the tty is cooked, for a child to use");
    rolltui_terminal_resume(t);
    got = read_until(master, enter, 500);
    check(got.find("\x1b[?1049h") != std::string::npos && tcgetattr(slave, &tio) == 0 && !(tio.c_lflag & ICANON),
          "resume: the alternate screen and raw mode are back");
    (void)!::write(master, "\x1b[A", 3);
    check(names(t, 500) == "Up", "bytes on the master decode to Up");
    (void)!::write(master, "\x1b", 1);
    check(names(t, 500) == "Escape", "a lone ESC resolves to Escape after the short wait");
    (void)!::write(master, "\x03", 1);
    check(names(t, 500) == "Ctrl+c", "Ctrl-C arrives as a key, not a signal");
    check(names(t, 30).empty(), "poll times out empty");
    // A DOUBLE-CLICK IS THE TERMINAL'S: two presses on one cell close together deliver both
    // presses and then a DoubleClick; two presses on different cells never do.
    write(master, "\x1b[<0;10;5M\x1b[<0;10;5m\x1b[<0;10;5M", 10 * 3);
    {
      const std::string got = names(t, 500);
      check(got == "Mouse Press 1 @9,4 | Mouse Release 1 @9,4 | Mouse Press 1 @9,4 | Mouse DoubleClick 1 @9,4",
            "two presses on one cell within the window: both presses, then a DoubleClick after the second [" + got + "]");
    }
    write(master, "\x1b[<0;12;5M\x1b[<0;14;5M", 10 * 2);
    {
      const std::string got = names(t, 500);
      check(got == "Mouse Press 1 @11,4 | Mouse Press 1 @13,4", "…two presses on different cells: no DoubleClick [" + got + "]");
    }
    // OSC 11: the query goes to the master; a reply written there (with a key typed
    // ahead of it) comes back as a colour, and the key is not lost.
    (void)!::write(master, "q\x1b]11;rgb:1414/1616/1a1a\x1b\\", 1 + 5 + 18 + 2);
    RolltuiStyleColor bg{};
    const bool have_bg = rolltui_terminal_query_background(t, 500, &bg) != 0;
    check(read_until(master, "\x1b]11;?", 500).find("\x1b]11;?\x1b\\") != std::string::npos, "the OSC 11 query reaches the master");
    check(have_bg && bg == RolltuiStyleColor::rgb(0x14, 0x16, 0x1a), "the reply parses to the background colour");
    check(names(t, 500) == "q", "a key typed before the reply is delivered by the next poll, not lost");
    check(!rolltui_terminal_query_background(t, 50, &bg), "no reply within the timeout: nullopt (the caller treats it as dark)");
    check(read_until(master, "\x1b]11;?", 500).find("\x1b]11;?") != std::string::npos, "…after asking");
    rolltui_terminal_write(t, "xyz", 3);
    check(read_until(master, "xyz", 500).find("xyz") != std::string::npos, "write reaches the master");
    ws.ws_col = 100;
    ws.ws_row = 30;
    ioctl(slave, TIOCSWINSZ, &ws);
    raise(SIGWINCH);
    std::string ev = names(t, 500);
    check(ev == "Resize 100x30", "SIGWINCH → Resize with the new size (got " + ev + ")");
    check(rolltui_terminal_width(t) == 100, "size refreshed");
    rolltui_terminal_free(t);
  }
  {
    std::string got = read_until(master, "\x1b[?1049l", 500);
    check(got.find("\x1b[?1049l") != std::string::npos && got.find("\x1b[?25h") != std::string::npos &&
              got.find("\x1b[0m") == 0,
          "leave: attributes reset first, cursor shown, alt screen off");
    termios tio{};
    check(tcgetattr(slave, &tio) == 0 && (tio.c_lflag & ICANON) && (tio.c_lflag & ECHO),
          "termios restored on destruction");
  }

  // ---- forked child killed by SIGTERM restores before dying ----------------------
  {
    pid_t pid = fork();
    if (pid == 0) {
      RolltuiTerminalOptions o;
      o.no_cache = true;
      RolltuiTerminal* t = rolltui_terminal_new(slave, slave, o);
      rolltui_terminal_write(t, "READY", 5);
      kill(getpid(), SIGTERM);
      pause();
      _exit(3);
    }
    std::string got = read_until(master, "\x1b[?1049l", 2000);
    int status = 0;
    waitpid(pid, &status, 0);
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM, "the child died by SIGTERM (re-raised, default disposition)");
    std::size_t ready = got.find("READY");
    std::size_t off = got.find("\x1b[?1049l");
    check(ready != std::string::npos && off != std::string::npos && off > ready,
          "…and its restore bytes reached the terminal after its last write");
    termios tio{};
    check(tcgetattr(slave, &tio) == 0 && (tio.c_lflag & ICANON), "…and the tty is cooked again");
  }

  // ---- a non-tty is tolerated (pipes: the probation harness) ---------------------
  {
    int p[2];
    check(::pipe(p) == 0, "pipe");
    RolltuiTerminalOptions o;
    o.no_cache = true;
    o.handle_signals = false;
    RolltuiTerminal* t = rolltui_terminal_new(p[0], p[1], o);
    check(rolltui_terminal_is_tty(t) == 0 && rolltui_terminal_width(t) == 80 && rolltui_terminal_height(t) == 24,
          "a pipe is not a tty; size falls back to 80x24");
    ::close(p[0]);
    ::close(p[1]);
    rolltui_terminal_free(t);
  }
  // ---- a descriptor opened from /dev/tty is reopened by its real name --------------------
  // On Darwin, poll(2) on the `/dev/tty` alias answers POLLNVAL, so a host that opens it (the
  // ordinary way to draw on the terminal while stdout carries an answer) would never see a key
  // and would leave without its restore bytes landing. The child below owns the slave as its
  // controlling terminal, opens `/dev/tty` exactly as such a host does, and must get Enter
  // through the ordinary poll and leave the terminal restored.
  {
    int sig[2];
    check(::pipe(sig) == 0, "pipe");
    pid_t pid = fork();
    if (pid == 0) {
      close(sig[0]);
      ::close(master);  // a session leader exiting while it holds its own terminal's MASTER never finishes
      setsid();
      ioctl(slave, TIOCSCTTY, 0);
      signal(SIGTTOU, SIG_IGN);       // taking the foreground from a fresh session is otherwise a stop
      tcsetpgrp(slave, getpgrp());
      dup2(slave, 0);
      dup2(slave, 1);
      dup2(slave, 2);
      ::close(slave);
      const int tty = open("/dev/tty", O_RDWR | O_CLOEXEC);
      if (tty < 0) _exit(4);
      RolltuiTerminalOptions o;
      o.no_cache = true;
      RolltuiTerminal* t = rolltui_terminal_new(tty, tty, o);
      rolltui_terminal_write(t, "READY", 5);
      int got_enter = 0;
      for (int i = 0; i < 40 && !got_enter; ++i) {
        std::vector<std::string> ev;
        rolltui_terminal_poll(t, 50, collect_term, &ev);
        for (const std::string& e : ev)
          if (e == "Enter") got_enter = 1;
      }
      rolltui_terminal_free(t);
      close(tty);
      const char* word = got_enter ? "GOT" : "NOT";
      write(sig[1], word, 3);
      _exit(got_enter ? 0 : 1);
    }
    close(sig[1]);
    std::string got = read_until(master, "READY", 2000);
    // The parent's own copy of the slave is released before the child exits: a session leader
    // leaving its controlling terminal waits for the last reference, and this case is last.
    ::close(slave);
    write(master, "\r", 1);
    char word[4] = {0};
    read(sig[0], word, 3);
    close(sig[0]);
    // The restore bytes were written before the word; read them, then release the MASTER too:
    // the child is a session leader, and its exit revokes its terminal, which waits for the
    // last holder of either side. Only then wait for it.
    got += read_until(master, "\x1b[?1049l", 2000);
    ::close(master);
    int status = 0;
    waitpid(pid, &status, 0);
    check(std::string(word) == "GOT" && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Enter reaches a terminal opened from /dev/tty through the ordinary poll [" + std::string(word) + "]");
    check(got.find("\x1b[?1049l") != std::string::npos && got.find("\x1b[?1006l") != std::string::npos,
          "…and its restore bytes (alt screen off, mouse off) reach the terminal on the way out");
  }


  // ============================ WHAT THE TERMINAL IS ============================
  // Asked in the SAME exchange as the keyboard, remembered per terminal, re-checked when remembered,
  // and never allowed to turn a terminal's answer into a keystroke.
  struct Cap {
    std::vector<int> kinds;
    int keys = 0, facts = 0;
  };
  auto cap_cb = [](void* ctx, const RolltuiTermEvent* e) {
    Cap* c = static_cast<Cap*>(ctx);
    c->kinds.push_back(e->kind);
    if (e->kind == ROLLTUI_TERM_EVENT_KEY) ++c->keys;
    if (e->kind == ROLLTUI_TERM_EVENT_FACTS) ++c->facts;
  };
  const std::vector<std::pair<const char*, const char*>> kGhosttyEnv = {{"TERM", "xterm-256color"}};

  {
    const std::string dir = make_dir("facts");
    RolltuiTerminalOptions o;
    o.cache_dir = dir.c_str();
    o.app_key = "test-1";
    const std::string cache_file = dir + "/terminal-facts.json";

    // ---- A terminal that answers everything, in ONE exchange
    RolltuiTermFacts a{};
    const Heard ha = with_terminal(kGhostty, kGhosttyEnv, o, [&](RolltuiTerminal* t) { a = facts_of(t); });
    const double full_ms = g_new_ms;
    check(ha.count("\x1b[c") == 1, "the keyboard AND every fact are asked in ONE exchange: one Primary DA, once");
    check(ha.asked("\x1b[?u") && ha.asked("\x1b]11;?") && ha.asked("\x1bP$qm") && ha.asked("\x1b[>0q") && ha.asked("\xE2\x96\x88\x1b[6n"),
          "…which carries the keyboard, background, colour-depth, version and glyph-width questions");
    check(a.depth == ROLLTUI_DEPTH_TRUECOLOR && a.depth_source == ROLLTUI_FACT_PROBE,
          "the terminal CONFIRMED 24-bit, so that is what it is drawn to, though the environment only said 256");
    check(a.mode == ROLLTUI_MODE_DARK && a.mode_source == ROLLTUI_FACT_PROBE && a.has_background &&
              a.background == RolltuiStyleColor::rgb(0x1e, 0x1e, 0x2e),
          "its background was read and is dark");
    check(a.ambiguous_wide == 0 && a.ambiguous_source == ROLLTUI_FACT_PROBE, "an ambiguous glyph took one cell, and that was measured");
    check(a.keyboard == ROLLTUI_PROTOCOL_KITTY && a.responsive && !a.remembered, "it speaks kitty, it answered, and nothing was remembered yet");
    check(std::string(a.name) == "ghostty 1.1.3", "it said who it is [" + std::string(a.name) + "]");
    check(file_exists(cache_file), "what it said was remembered");

    // ---- The next run on the SAME terminal asks nothing, and re-checks the background after the first frame
    RolltuiTermFacts b{};
    Cap cb;
    const auto t_first = std::chrono::steady_clock::now();
    const Heard hb = with_terminal(kGhostty, kGhosttyEnv, o, [&](RolltuiTerminal* t) {
      b = facts_of(t);
      for (int i = 0; i < 6; ++i) rolltui_terminal_poll(t, 40, cap_cb, &cb);
    });
    (void)t_first;
    const double cached_ms = g_new_ms;
    std::printf("  [time] terminal_new against a terminal that answers: fresh %.2f ms (one exchange), remembered %.2f ms\n", full_ms, cached_ms);
    check(!hb.asked_da() && !hb.asked("\x1bP$qm") && !hb.asked("\x1b[6n"), "a remembered terminal is asked NOTHING at entry");
    check(cached_ms < 40, "…so entering it costs no wait at all (" + std::to_string(cached_ms) + " ms)");
    check(hb.asked("\x1b[>1u"), "…yet its keyboard protocol is enabled (it was remembered too)");
    check(hb.count("\x1b]11;?") == 1, "…and its background is re-checked once, at the first poll, after the first frame");
    check(b.remembered == 1 && b.depth == ROLLTUI_DEPTH_TRUECOLOR && b.depth_source == ROLLTUI_FACT_CACHE && b.keyboard == ROLLTUI_PROTOCOL_KITTY &&
              b.mode == ROLLTUI_MODE_DARK && b.mode_source == ROLLTUI_FACT_CACHE && b.responsive && b.ambiguous_source == ROLLTUI_FACT_CACHE,
          "…and the facts say they were remembered, and from where");
    check(cb.keys == 0 && cb.facts == 0, "the re-check agreed with what was remembered: no key events, and no FACTS event to report");

    // ---- A remembered background that has gone STALE is corrected, and the host is told
    Script flipped = kGhostty;
    flipped.bg = "\x1b]11;rgb:ffff/ffff/ffff\x1b\\";  // the person switched the terminal to a light theme
    // A host that did NOT ask for the event is never sent one — but the fact is corrected all the same, and
    // so is the mode a theme follows, which is the point: forgetting to listen costs a repaint, not correctness.
    {
      const std::string quiet_dir = make_dir("quiet");
      RolltuiTerminalOptions oq;
      oq.cache_dir = quiet_dir.c_str();
      with_terminal(kGhostty, kGhosttyEnv, oq, [](RolltuiTerminal*) {});  // remembers DARK
      Cap cq;
      RolltuiTermFacts q2{};
      int active_mode = -2;
      with_terminal(flipped, kGhosttyEnv, oq, [&](RolltuiTerminal* t) {
        (void)facts_of(t);
        for (int i = 0; i < 10; ++i) rolltui_terminal_poll(t, 40, cap_cb, &cq);
        q2 = facts_of(t);
        active_mode = rolltui_termfacts_active_mode();
      });
      check(cq.facts == 0 && cq.keys == 0, "no FACTS event goes to a host that did not ask for one [" + std::to_string(cq.facts) + "]");
      check(q2.mode == ROLLTUI_MODE_LIGHT && active_mode == ROLLTUI_MODE_LIGHT,
            "…yet the mode a theme follows was corrected all the same (the library applies it, the host is not needed)");
    }
    o.facts_events = 1;
    RolltuiTermFacts c1{}, c2{};
    Cap cc;
    const Heard hc = with_terminal(flipped, kGhosttyEnv, o, [&](RolltuiTerminal* t) {
      c1 = facts_of(t);
      for (int i = 0; i < 10; ++i) rolltui_terminal_poll(t, 40, cap_cb, &cc);
      c2 = facts_of(t);
    });
    check(c1.mode == ROLLTUI_MODE_DARK && c1.mode_source == ROLLTUI_FACT_CACHE, "started from the remembered (dark) background");
    check(cc.facts == 1 && cc.keys == 0, "the re-check found it light: ONE FACTS event, and no stray keys [" + std::to_string(cc.facts) + "/" + std::to_string(cc.keys) + "]");
    check(c2.mode == ROLLTUI_MODE_LIGHT && c2.mode_source == ROLLTUI_FACT_PROBE, "…and reading the facts again says light, from the terminal");
    (void)hc;
    RolltuiTermFacts d1{};
    with_terminal(flipped, kGhosttyEnv, o, [&](RolltuiTerminal* t) { d1 = facts_of(t); });
    check(d1.mode == ROLLTUI_MODE_LIGHT && d1.mode_source == ROLLTUI_FACT_CACHE, "…and the correction was remembered for the next run");

    // ---- Every trigger for asking again
    {
      Heard h = with_terminal(kGhostty, {{"TERM", "xterm-256color"}, {"TERM_PROGRAM_VERSION", "2.0"}}, o, [](RolltuiTerminal*) {});
      check(h.asked_da(), "a new TERM_PROGRAM_VERSION asks again");
      RolltuiTerminalOptions o2 = o;
      o2.app_key = "test-2";
      h = with_terminal(kGhostty, kGhosttyEnv, o2, [](RolltuiTerminal*) {});
      check(h.asked_da(), "a new release of the program asks again");
      h = with_terminal(kGhostty, {{"TERM", "xterm-256color"}, {"SSH_CONNECTION", "10.0.0.2 5000 10.0.0.9 22"}}, o, [](RolltuiTerminal*) {});
      check(h.asked_da(), "arriving over ssh asks again");
      h = with_terminal(kGhostty, kGhosttyEnv, o, [](RolltuiTerminal*) {});
      check(!h.asked_da(), "…while the original terminal is still remembered through all of that");
    }
  }

  // ---- The case that started it: Apple Terminal on macOS 15
  {
    const std::string dir = make_dir("apple");
    RolltuiTerminalOptions o;
    o.cache_dir = dir.c_str();
    // A COLORTERM exported by a shell rc file, which is the trap: the environment says 24-bit.
    const std::vector<std::pair<const char*, const char*>> env = {
        {"TERM", "xterm-256color"}, {"TERM_PROGRAM", "Apple_Terminal"}, {"TERM_PROGRAM_VERSION", "455.1"}, {"COLORTERM", "truecolor"}};
    RolltuiTermFacts f{};
    std::string first, forced, back, raw_request, after_free;
    RolltuiSwap* swap_outside = nullptr;
    RolltuiDrawScratch* d_outside = nullptr;
    RolltuiStyle st_outside{};
    st_outside.fg = RolltuiStyleColor::rgb(200, 100, 50);
    const Heard heard_apple = with_terminal(kAppleSequoia, env, o, [&](RolltuiTerminal* t) {
      f = facts_of(t);
      RolltuiSwap* swap = rolltui_swap_new(10, 2, RolltuiStyle{});
      RolltuiDrawScratch* d = rolltui_draw_scratch_new();
      RolltuiStyle st{};
      st.fg = RolltuiStyleColor::rgb(200, 100, 50);
      st.bg = RolltuiStyleColor::rgb(20, 22, 26);
      auto paint = [&] {
        RolltuiFrame* fr = rolltui_swap_begin(swap, 10, 2, RolltuiStyle{});
        rolltui_frame_put_text(fr, d, 0, 0, "hi", 2, st, 10, 0, 0);
        RolltuiStr out{};
        rolltui_terminal_present(t, swap, &out);
        const std::string got(out.p ? out.p : "", out.n);
        rolltui_str_free(&out);
        return got;
      };
      first = paint();
      {
        // A host that never called `rolltui_terminal_present` and never read a fact: the depth it hard-codes is a request.
        swap_outside = rolltui_swap_new(10, 2, RolltuiStyle{});
        d_outside = rolltui_draw_scratch_new();
        RolltuiFrame* fr = rolltui_swap_begin(swap_outside, 10, 2, RolltuiStyle{});
        rolltui_frame_put_text(fr, d_outside, 0, 0, "hi", 2, st_outside, 10, 0, 0);
        RolltuiStr out{};
        rolltui_swap_present(swap_outside, ROLLTUI_DEPTH_TRUECOLOR, &out);
        raw_request.assign(out.p ? out.p : "", out.n);
        rolltui_str_free(&out);
      }
      rolltui_terminal_set_depth(t, ROLLTUI_DEPTH_TRUECOLOR);  // a person who insists
      rolltui_swap_invalidate(swap);
      forced = paint();
      rolltui_terminal_set_depth(t, -1);
      rolltui_swap_invalidate(swap);
      back = paint();
      rolltui_draw_scratch_free(d);
      rolltui_swap_free(swap);
    });
    {
      RolltuiFrame* fr = rolltui_swap_begin(swap_outside, 10, 2, RolltuiStyle{});
      rolltui_frame_put_text(fr, d_outside, 0, 0, "hi", 2, st_outside, 10, 0, 0);
      rolltui_swap_invalidate(swap_outside);
      RolltuiStr out{};
      rolltui_swap_present(swap_outside, ROLLTUI_DEPTH_TRUECOLOR, &out);
      after_free.assign(out.p ? out.p : "", out.n);
      rolltui_str_free(&out);
      rolltui_draw_scratch_free(d_outside);
      rolltui_swap_free(swap_outside);
    }
    check(f.depth == ROLLTUI_DEPTH_ANSI256 && f.depth_source == ROLLTUI_FACT_ENV && f.responsive && f.keyboard == ROLLTUI_PROTOCOL_LEGACY,
          "Apple Terminal 455 that answers only Primary DA is a 256-colour terminal, though COLORTERM says truecolor");
    check(!heard_apple.asked("\x1bP$qm") && !heard_apple.asked("\x1b[>0q") && heard_apple.asked("\x1b]11;?") && heard_apple.asked("\x1b[6n"),
          "…and it is NOT sent the colour-depth or version questions (its depth is decided, so they could only go wrong), though it is still asked its background and glyph width");
    check(first.find("38;5;") != std::string::npos && first.find("38;2;") == std::string::npos && first.find("48;2;") == std::string::npos,
          "…and what is drawn to it is PALETTE colour: no 38;2 or 48;2 for it to misread as stray attributes");
    check(raw_request.find("38;5;") != std::string::npos && raw_request.find("38;2;") == std::string::npos && raw_request.find("48;2;") == std::string::npos,
          "…and a host that NEVER LEARNED the terminal has a depth, and hard-codes truecolor into rolltui_swap_present, is drawn to it correctly all the same");
    check(after_free.find("38;2;200;100;50") != std::string::npos,
          "…once the terminal is freed nothing is held to any more: a golden test gets exactly the depth it asks for");
    check(forced.find("38;2;200;100;50") != std::string::npos, "…unless a person says truecolor outright, which is honoured");
    check(back.find("38;5;") != std::string::npos && back.find("38;2;") == std::string::npos, "…and clearing that goes back to what was detected");
  }

  // ---- A terminal that says nothing: found out once, then not waited for
  {
    const std::string dir = make_dir("silent");
    RolltuiTerminalOptions o;
    o.cache_dir = dir.c_str();
    Script silent;
    silent.answers = false;
    silent.idle_ms = 100;
    RolltuiTermFacts f1{}, f2{};
    const Heard h1 = with_terminal(silent, {{"TERM", "xterm-256color"}}, o, [&](RolltuiTerminal* t) { f1 = facts_of(t); });
    const double first_ms = g_new_ms;
    const Heard h2 = with_terminal(silent, {{"TERM", "xterm-256color"}}, o, [&](RolltuiTerminal* t) { f2 = facts_of(t); });
    const double second_ms = g_new_ms;
    std::printf("  [time] a terminal that answers NOTHING: first launch %.1f ms, every launch after %.2f ms\n", first_ms, second_ms);
    check(h1.asked_da() && !f1.responsive && f1.depth == ROLLTUI_DEPTH_ANSI256 && f1.depth_source == ROLLTUI_FACT_ENV,
          "a terminal that answers nothing is unresponsive, and the environment's guess stands");
    check(first_ms >= 75, "…after waiting for it (" + std::to_string(first_ms) + " ms)");
    check(!h2.asked_da() && f2.remembered == 1 && !f2.responsive, "the next run does not ask a terminal it knows is silent");
    check(second_ms < 20, "…so it does not wait for it either (" + std::to_string(second_ms) + " ms after " + std::to_string(first_ms) + ")");
  }

  // ---- Opting out, and the multiplexer that must not be trusted
  {
    const std::string dir = make_dir("optout");
    RolltuiTerminalOptions o;
    o.cache_dir = dir.c_str();
    o.no_probe = true;
    Heard h = with_terminal(kGhostty, kGhosttyEnv, o, [&](RolltuiTerminal* t) {
      RolltuiTermFacts f = facts_of(t);
      check(f.depth == ROLLTUI_DEPTH_ANSI256 && f.depth_source == ROLLTUI_FACT_ENV && !f.remembered && f.mode_source == ROLLTUI_FACT_DEFAULT,
            "no_probe: the facts are the environment's and nothing else");
    });
    check(h.bytes.find("\x1b[?u\x1b[?4m\x1b[c") != std::string::npos && !h.asked("\x1b]11;?") && !h.asked("\x1bP$qm"),
          "…and only the keyboard question is asked, byte for byte what it always was");
    check(!file_exists(dir + "/terminal-facts.json"), "…and nothing is written");

    RolltuiTerminalOptions o2;
    o2.cache_dir = dir.c_str();
    o2.no_cache = true;
    h = with_terminal(kGhostty, kGhosttyEnv, o2, [&](RolltuiTerminal* t) {
      check(std::string(rolltui_terminal_cache_path(t)).empty(), "no_cache: there is no cache path");
    });
    check(h.asked("\x1bP$qm") && !file_exists(dir + "/terminal-facts.json"), "…the terminal is still asked, and nothing is remembered");

    // Behind tmux the answers are used but never remembered or trusted.
    RolltuiTerminalOptions o3;
    o3.cache_dir = dir.c_str();
    const std::vector<std::pair<const char*, const char*>> tmux_env = {{"TERM", "tmux-256color"}, {"TMUX", "/tmp/tmux-501/default,1,0"}};
    RolltuiTermFacts f{};
    h = with_terminal(kGhostty, tmux_env, o3, [&](RolltuiTerminal* t) { f = facts_of(t); });
    check(f.depth == ROLLTUI_DEPTH_TRUECOLOR && f.depth_source == ROLLTUI_FACT_PROBE, "behind tmux the terminal's answers are used");
    check(!file_exists(dir + "/terminal-facts.json"), "…but not remembered: the terminal it is attached to can change without the environment changing");
    h = with_terminal(kGhostty, tmux_env, o3, [](RolltuiTerminal*) {});
    check(h.asked_da(), "…so it asks again next time");
  }

  // ---- Nothing to wire: the width and the mode are applied by the library too
  {
    Script wide = kGhostty;
    wide.cpr_col = 3;                                             // an ambiguous glyph took TWO cells here
    wide.bg = "\x1b]11;rgb:f5f5/f5f5/f5f5\x1b\\";                 // and its background is light
    RolltuiTerminalOptions o;
    o.no_cache = true;
    int while_alive_width = -1, while_alive_mode = -2;
    with_terminal(wide, kGhosttyEnv, o, [&](RolltuiTerminal*) {
      // No `rolltui_terminal_facts`, no flag: this host read NOTHING.
      while_alive_width = rolltui_u_codepoint_width(0x2588, 0);
      while_alive_mode = rolltui_termfacts_active_mode();
    });
    check(while_alive_width == 2, "a glyph the terminal draws two cells wide is measured as two, though the host never asked and passed zero");
    check(while_alive_mode == ROLLTUI_MODE_LIGHT, "…and a theme that says auto follows the terminal's light background, though the host never asked");
    check(rolltui_u_codepoint_width(0x2588, 0) == 1 && rolltui_termfacts_active_mode() == -1, "…and both are withdrawn when the terminal is freed");
  }

  // ---- ROLL_TERM_PROBE=0: the escape hatch for a terminal that mishandles a question
  {
    RolltuiTerminalOptions o;
    o.no_cache = true;
    const Heard h = with_terminal(kGhostty, {{"TERM", "xterm-256color"}, {"ROLL_TERM_PROBE", "0"}}, o, [&](RolltuiTerminal* t) {
      const RolltuiTermFacts f = facts_of(t);
      check(f.depth == ROLLTUI_DEPTH_ANSI256 && f.depth_source == ROLLTUI_FACT_ENV && !f.remembered, "ROLL_TERM_PROBE=0: the facts are the environment's");
    });
    check(h.bytes.find("\x1b[?u\x1b[?4m\x1b[c") != std::string::npos && !h.asked("\x1b]11;?") && !h.asked("\x1bP$qm") && !h.asked("\x1b[6n"),
          "…and only the keyboard question is asked");
    ::unsetenv("ROLL_TERM_PROBE");
  }

  // ---- Reprobing, and a late reply
  {
    const std::string dir = make_dir("reprobe");
    RolltuiTerminalOptions o;
    o.cache_dir = dir.c_str();
    RolltuiTermFacts f{};
    int answered = 0;
    const Heard h = with_terminal(kGhostty, kGhosttyEnv, o, [&](RolltuiTerminal* t) {
      answered = rolltui_terminal_reprobe(t, 500);
      f = facts_of(t);
    });
    check(answered == 1 && f.responsive && f.depth == ROLLTUI_DEPTH_TRUECOLOR, "reprobe asks again and the terminal answers");
    check(h.count("\x1b[c") == 2, "…as a second exchange");
    check(h.count("\x1b[>1u") == 1 && h.count("\x1b[<1u") == 1,
          "…which does NOT push the keyboard protocol a second time (one push, one pop)");
    check(!h.bytes.substr(h.bytes.find("\x1b[c") + 3).empty() && h.count("\x1b[?u") == 1,
          "…and does not ask the keyboard question again");
  }
  {
    // A slow terminal's late answer, and a reply that arrives when nothing is waiting for it: never keys.
    Script late = kGhostty;
    late.after = "\x1b]11;rgb:ffff/ffff/ffff\x07\x1bP>|late\x1b\\\x1b[?1;2c";
    RolltuiTerminalOptions o;
    o.no_cache = true;
    o.facts_events = 1;
    RolltuiTermFacts f1{}, f2{};
    Cap cl;
    with_terminal(late, kGhosttyEnv, o, [&](RolltuiTerminal* t) {
      f1 = facts_of(t);
      for (int i = 0; i < 12; ++i) rolltui_terminal_poll(t, 40, cap_cb, &cl);
      f2 = facts_of(t);
    });
    check(cl.keys == 0, "a terminal's late reply is never delivered as keystrokes (" + std::to_string(cl.keys) + " key events)");
    check(cl.facts == 1 && f1.mode == ROLLTUI_MODE_DARK && f2.mode == ROLLTUI_MODE_LIGHT,
          "…but a late BACKGROUND report that changes the mode is reported once, as a FACTS event");
  }

  for (const std::string& d : g_scratch_dirs) {
    const std::string rm = "rm -rf '" + d + "'";
    (void)!std::system(rm.c_str());
  }
  return report("rolltui_terminal_test");
}
