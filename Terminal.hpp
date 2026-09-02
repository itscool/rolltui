#pragma once
//
// rolltui/Terminal.hpp — the one place the library touches a file descriptor: raw
// mode, the alternate screen, mouse mode, bracketed paste, the window size, SIGWINCH,
// and restore-on-every-exit-path. Everything above it (Screen, Keys, widgets) is
// pure; everything here is bookkeeping that has to be right once.
//
//   Terminal t(STDIN_FILENO, STDOUT_FILENO, opts);   // enters: raw + modes
//   for (;;) for (const Event& e : t.poll(100)) ...  // keys, mouse, paste, resize
//   t.write(render_diff(...));
//   // ~Terminal restores; so does a fatal signal (SIGINT/TERM/HUP/QUIT) — restore
//   // first, then re-raise with the default disposition, so the process dies as it
//   // would have but never leaves the user staring at an alternate screen with a
//   // raw tty.
//
// Raw mode here means cfmakeraw: ISIG is off, so Ctrl-C arrives as a key event
// (plan/phase-9.md: Ctrl-C cancels a turn, twice exits) and SIGINT only ever comes
// from outside. Ctrl-Z likewise. The restore path is async-signal-safe: precomputed
// bytes written with write(2) and a tcsetattr of the saved termios.
//
// Tested on a real pty pair (rolltui/tests/terminal_test.cpp): mode bytes, termios
// flags, size, events from bytes written to the master, SIGWINCH → Resize, and a
// forked child killed by SIGTERM whose restore bytes are seen by the parent.
//
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Keys.hpp"

namespace rolltui {

struct TerminalOptions {
  bool alt_screen = true;
  bool mouse = true;             // SGR 1006 + button + drag reporting
  bool bracketed_paste = true;
  bool hide_cursor = true;
  bool handle_signals = true;    // restore-and-reraise on SIGINT/SIGTERM/SIGHUP/SIGQUIT
};

class Terminal {
 public:
  Terminal(int in_fd, int out_fd, const TerminalOptions& opts = {});
  ~Terminal();
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  bool is_tty() const { return tty_; }
  int width() const { return w_; }
  int height() const { return h_; }
  void refresh_size();  // ioctl; also called on SIGWINCH

  // Waits up to timeout_ms (-1: forever) for input or a resize; decodes what arrived.
  // A lone ESC that nothing follows within the timeout is delivered as Escape.
  std::vector<Event> poll(int timeout_ms);

  // Writes every byte (loops on partial writes and EINTR).
  void write(std::string_view bytes);

  // The bytes that entered and will leave the modes, for tests and for `--frame`
  // tooling that wants to reproduce a session without a tty.
  const std::string& enter_sequence() const { return enter_; }
  const std::string& leave_sequence() const { return leave_; }

  // Async-signal-safe: restores the active terminal (if any). Called by the signal
  // handlers; public so a host with its own crash handler can call it too.
  static void restore_now();

 private:
  void enter();
  void leave();
  int in_, out_;
  bool tty_ = false, entered_ = false;
  int w_ = 80, h_ = 24;
  std::string enter_, leave_;
  KeyDecoder decoder_;
  int wake_[2] = {-1, -1};  // self-pipe: SIGWINCH handler writes, poll() reads
  TerminalOptions opts_;
  struct Saved;
  Saved* saved_ = nullptr;
};

}  // namespace rolltui
