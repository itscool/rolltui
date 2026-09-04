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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Keys.hpp"
#include "rolltui/Style.hpp"

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

  // Waits up to timeout_ms (-1: forever) for input, a resize or a wake(); decodes what
  // arrived. A lone ESC that nothing follows within the timeout is delivered as Escape.
  std::vector<Event> poll(int timeout_ms);

  // Makes a blocked poll() return now, with whatever events are pending (possibly
  // none). Thread-safe and async-signal-safe: one byte on the self-pipe. A frontend
  // whose view changes on another thread uses it instead of a short poll timeout.
  void wake();

  // Writes every byte (loops on partial writes and EINTR).
  void write(std::string_view bytes);

  // WHICH KEYBOARD PROTOCOL THIS TERMINAL SPEAKS (Phase 12 m3), asked of the terminal
  // rather than assumed of its name. enter() calls it once, so a host gets it for free;
  // a host that swaps terminals under one process calls it again.
  //
  // The exchange, and it is the terminal's answer in both directions:
  //   →  CSI ? u        kitty: "which enhancement flags are set?"
  //   →  CSI ? 4 m      xterm (XTQUERYMODIFIERS): "what is modifyOtherKeys?"
  //   →  CSI c          Primary DA — every terminal answers this one, which is what
  //                     makes a NEGATIVE answer definitive instead of a timeout guess.
  // A kitty-capable terminal replies `CSI ? flags u` and an xterm-capable one
  // `CSI > 4 ; value m`, both BEFORE the DA reply; a terminal that speaks neither sends
  // only the DA. Whatever else arrived is somebody typing and is kept for the next
  // poll(), exactly as query_background does — nothing is lost.
  //
  // The answer degrades to Legacy on every uncertainty: a pipe, no reply, a reply we do
  // not recognise, a timeout. That direction is deliberate — a wrong Legacy answer
  // refuses a chord that would have worked and says so out loud, while a wrong Kitty
  // answer accepts one that silently never fires, which is the defect this whole
  // milestone is about. ROLLTUI_KEY_PROTOCOL=legacy|modifyOtherKeys|kitty overrides the
  // query, for a terminal that lies in either direction.
  //
  // Enabling is part of asking: kitty gets `CSI > 1 u` pushed (popped by `CSI < 1 u` on
  // the way out), modifyOtherKeys gets `CSI > 4 ; 2 m` (reset by `CSI > 4 m`), and both
  // pops join leave_ so every exit path — including a fatal signal — undoes them.
  KeyProtocol negotiate_keyboard(int timeout_ms = 80);
  KeyProtocol key_protocol() const { return protocol_; }

  // Asks the terminal for its background colour (OSC 11, milestone 11's light/dark
  // auto-detect) and waits up to timeout_ms for the reply. Bytes that arrive and are
  // not the reply (a user already typing) are kept and delivered by the next poll();
  // nothing is lost. nullopt on a pipe, on no answer in time (a terminal that does not
  // implement OSC 11 sends nothing), or on an unparseable answer — the caller treats
  // every nullopt as "dark". Call before the event loop, once.
  std::optional<Color> query_background(int timeout_ms);

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
  std::vector<Event> queued_;  // decoded during a query; handed out by the next poll()
  int wake_[2] = {-1, -1};  // self-pipe: SIGWINCH handler writes, poll() reads
  KeyProtocol protocol_ = KeyProtocol::Legacy;  // until the terminal says otherwise
  TerminalOptions opts_;
  // OWNED (Phase 13 m2). A pimpl for the one platform type this header must not name;
  // `unique_ptr` rather than the raw pointer + hand-rolled `delete` it was until m2, which
  // was the library's ONLY hand-rolled ownership and the only thing an exception between
  // the constructor's body and its destructor could have leaked.
  struct Saved;
  std::unique_ptr<Saved> saved_;
};

}  // namespace rolltui
