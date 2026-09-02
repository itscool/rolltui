//
// terminal_test.cpp — Terminal on a real pty pair: the mode bytes it writes, the
// termios flags it sets and restores, the size it reads, events decoded from bytes
// written to the master, SIGWINCH → Resize, and — in a forked child killed by
// SIGTERM — the restore bytes reaching the master before the child dies. Real
// processes and real signals, the way roll's manager_test does it.
//
#include <csignal>
#include <cstring>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "rolltui/Terminal.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
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

std::string names(const std::vector<Event>& v) {
  std::string s;
  for (const Event& e : v) s += (s.empty() ? "" : " | ") + to_string(e);
  return s;
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
    TerminalOptions o;
    o.handle_signals = false;  // the parent must keep its own dispositions for the fork test
    Terminal t(slave, slave, o);
    enter = t.enter_sequence();
    leave = t.leave_sequence();
    check(t.is_tty(), "the slave is a tty");
    check(t.width() == 80 && t.height() == 24, "size read from the pty (" + std::to_string(t.width()) + "x" + std::to_string(t.height()) + ")");
    std::string got = read_until(master, enter, 500);
    check(got.find("\x1b[?1049h") != std::string::npos && got.find("\x1b[?1006h") != std::string::npos &&
              got.find("\x1b[?2004h") != std::string::npos && got.find("\x1b[?25l") != std::string::npos,
          "enter: alt screen, SGR mouse, bracketed paste, cursor hidden");
    termios tio{};
    check(tcgetattr(slave, &tio) == 0 && !(tio.c_lflag & ICANON) && !(tio.c_lflag & ECHO) && !(tio.c_lflag & ISIG),
          "raw mode: ICANON, ECHO and ISIG off (Ctrl-C is a key)");
    (void)!::write(master, "\x1b[A", 3);
    check(names(t.poll(500)) == "Up", "bytes on the master decode to Up");
    (void)!::write(master, "\x1b", 1);
    check(names(t.poll(500)) == "Escape", "a lone ESC resolves to Escape after the short wait");
    (void)!::write(master, "\x03", 1);
    check(names(t.poll(500)) == "Ctrl+c", "Ctrl-C arrives as a key, not a signal");
    check(t.poll(30).empty(), "poll times out empty");
    t.write("xyz");
    check(read_until(master, "xyz", 500).find("xyz") != std::string::npos, "write reaches the master");
    ws.ws_col = 100;
    ws.ws_row = 30;
    ioctl(slave, TIOCSWINSZ, &ws);
    raise(SIGWINCH);
    std::string ev = names(t.poll(500));
    check(ev == "Resize 100x30", "SIGWINCH → Resize with the new size (got " + ev + ")");
    check(t.width() == 100, "size refreshed");
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
      TerminalOptions o;
      Terminal t(slave, slave, o);
      t.write("READY");
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
    TerminalOptions o;
    o.handle_signals = false;
    Terminal t(p[0], p[1], o);
    check(!t.is_tty() && t.width() == 80 && t.height() == 24, "a pipe is not a tty; size falls back to 80x24");
    ::close(p[0]);
    ::close(p[1]);
  }
  ::close(master);
  ::close(slave);
  return report("rolltui terminal_test");
}
