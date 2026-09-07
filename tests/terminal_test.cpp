//
// terminal_test.cpp — Terminal on a real pty pair: the mode bytes it writes, the
// termios flags it sets and restores, the size it reads, events decoded from bytes
// written to the master, SIGWINCH → Resize, and — in a forked child killed by
// SIGTERM — the restore bytes reaching the master before the child dies. Real
// processes and real signals, the way roll's manager_test does it.
//
#include <csignal>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

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

using namespace rolltui_test;

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
  static const char* mouse_kinds[] = {"Press", "Release", "Drag", "Move", "WheelUp", "WheelDown", "WheelLeft", "WheelRight"};
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

int main() {
  int master = -1, slave = -1;
  winsize ws{};
  ws.ws_row = 24;
  ws.ws_col = 80;
  check(openpty(&master, &slave, nullptr, nullptr, &ws) == 0, "openpty");
  if (master < 0) return report("rolltui terminal_test");

  // ---- in-process: modes, termios, size, events ----------------------------------
  std::string enter, leave;
  {
    RolltuiTerminalOptions o;
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
    (void)!::write(master, "\x1b[A", 3);
    check(names(t, 500) == "Up", "bytes on the master decode to Up");
    (void)!::write(master, "\x1b", 1);
    check(names(t, 500) == "Escape", "a lone ESC resolves to Escape after the short wait");
    (void)!::write(master, "\x03", 1);
    check(names(t, 500) == "Ctrl+c", "Ctrl-C arrives as a key, not a signal");
    check(names(t, 30).empty(), "poll times out empty");
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
    o.handle_signals = false;
    RolltuiTerminal* t = rolltui_terminal_new(p[0], p[1], o);
    check(rolltui_terminal_is_tty(t) == 0 && rolltui_terminal_width(t) == 80 && rolltui_terminal_height(t) == 24,
          "a pipe is not a tty; size falls back to 80x24");
    ::close(p[0]);
    ::close(p[1]);
    rolltui_terminal_free(t);
  }
  ::close(master);
  ::close(slave);
  return report("rolltui terminal_test");
}
