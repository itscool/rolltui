#pragma once
//
// rolltui/Terminal.hpp — THE C++ SHAPE of the terminal. See rolltui/c/rolltui_terminal.h for
// the boundary's rules, the negotiation protocol and the restore-on-every-exit-path design —
// none of it is repeated here, because the rules are the same in both languages and a second
// copy is a second thing to drift.
//
// PHASE 17 m1 — THE TERMINAL IS BEHIND A C BOUNDARY. This header is the vocabulary and the
// RAII shape every existing caller already writes against; nothing a caller does changed.
// One thing is worth knowing: `TerminalOptions` IS `RolltuiTerminalOptions` (one definition,
// Phase 14 m2's rule), so its five flags are `unsigned char` rather than `bool` — the same
// trade `rolltui_style.h` records for a Style's attribute bits, and for the same reason. A
// call site that assigns a `bool` to one (`opts.handle_signals = false;`) is unaffected.
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
// from outside. Ctrl-Z likewise.
//
// Tested on a real pty pair (rolltui/tests/terminal_test.cpp, unchanged by this port): mode
// bytes, termios flags, size, events from bytes written to the master, SIGWINCH → Resize,
// and a forked child killed by SIGTERM whose restore bytes are seen by the parent.
//
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "rolltui/Keys.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/c/rolltui_terminal.h"

namespace rolltui {

using TerminalOptions = RolltuiTerminalOptions;

class Terminal {
 public:
  Terminal(int in_fd, int out_fd, const TerminalOptions& opts = {});
  ~Terminal() = default; // the handle's deleter does the restore; see Handle below
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  bool is_tty() const { return rolltui_terminal_is_tty(h_.get()) != 0; }
  int width() const { return rolltui_terminal_width(h_.get()); }
  int height() const { return rolltui_terminal_height(h_.get()); }
  void refresh_size() { rolltui_terminal_refresh_size(h_.get()); } // ioctl; also called on SIGWINCH

  // Waits up to timeout_ms (-1: forever) for input, a resize or a wake(); decodes what
  // arrived. A lone ESC that nothing follows within the timeout is delivered as Escape.
  std::vector<Event> poll(int timeout_ms);

  // Makes a blocked poll() return now, with whatever events are pending (possibly
  // none). Thread-safe and async-signal-safe: one byte on the self-pipe. A frontend
  // whose view changes on another thread uses it instead of a short poll timeout.
  void wake() { rolltui_terminal_wake(h_.get()); }

  // Writes every byte (loops on partial writes and EINTR).
  void write(std::string_view bytes) { rolltui_terminal_write(h_.get(), bytes.data(), bytes.size()); }

  // WHICH KEYBOARD PROTOCOL THIS TERMINAL SPEAKS (Phase 12 m3) — see rolltui_terminal.h for
  // the negotiation exchange and the deliberate degrade-to-Legacy direction. The one thing
  // that stays in C++: the three PROTOCOL NAMES ("legacy"/"modifyOtherKeys"/"kitty") that
  // ROLLTUI_KEY_PROTOCOL spells (rolltui/Keys.cpp keeps that vocabulary out of the C layer
  // on purpose), so this reads the environment and hands the C an already-resolved override.
  KeyProtocol negotiate_keyboard(int timeout_ms = 80) {
    int forced = -1;
    if (const char* env = std::getenv("ROLLTUI_KEY_PROTOCOL"))
      if (const std::optional<KeyProtocol> p = parse_key_protocol(env)) forced = static_cast<int>(*p);
    return static_cast<KeyProtocol>(rolltui_terminal_negotiate_keyboard(h_.get(), timeout_ms, forced));
  }
  KeyProtocol key_protocol() const { return static_cast<KeyProtocol>(rolltui_terminal_key_protocol(h_.get())); }

  // Asks the terminal for its background colour (OSC 11, milestone 11's light/dark
  // auto-detect) and waits up to timeout_ms for the reply. Bytes that arrive and are
  // not the reply (a user already typing) are kept and delivered by the next poll();
  // nothing is lost. nullopt on a pipe, on no answer in time (a terminal that does not
  // implement OSC 11 sends nothing), or on an unparseable answer — the caller treats
  // every nullopt as "dark". Call before the event loop, once.
  std::optional<Color> query_background(int timeout_ms) {
    Color c{};
    if (!rolltui_terminal_query_background(h_.get(), timeout_ms, &c)) return std::nullopt;
    return c;
  }

  // The bytes that entered and will leave the modes, for tests and for `--frame`
  // tooling that wants to reproduce a session without a tty. A BORROW into the handle,
  // valid until this Terminal is destroyed or negotiate_keyboard() is called again (the
  // one thing that can grow the leave sequence after construction).
  std::string_view enter_sequence() const {
    size_t n = 0;
    const char* p = rolltui_terminal_enter_sequence(h_.get(), &n);
    return {p, n};
  }
  std::string_view leave_sequence() const {
    size_t n = 0;
    const char* p = rolltui_terminal_leave_sequence(h_.get(), &n);
    return {p, n};
  }

  // Async-signal-safe: restores the active terminal (if any). Called by the signal
  // handlers; public so a host with its own crash handler can call it too.
  static void restore_now() { rolltui_terminal_restore_now(); }

 private:
  // OWNED, LONG-LIVED (CLAUDE.md's strategy 4): one handle, one owner, freed by the
  // deleter below — the same shape `KeyDecoder::Handle` and four others already use.
  struct Handle {
    void operator()(RolltuiTerminal* p) const { rolltui_terminal_free(p); }
  };
  std::unique_ptr<RolltuiTerminal, Handle> h_;
};

}  // namespace rolltui
