#ifndef ROLLTUI_C_TERMINAL_H
#define ROLLTUI_C_TERMINAL_H
/*
 * rolltui/c/rolltui_terminal.h — THE TERMINAL, as C (Phase 17 m1).
 *
 * The one place the library touches a file descriptor: raw mode, the alternate screen,
 * mouse mode, bracketed paste, the window size, SIGWINCH, and restore-on-every-exit-path.
 * Everything above it (Screen, Keys, widgets) is pure; everything here is bookkeeping that
 * has to be right once.
 *
 *   RolltuiTerminal* t = rolltui_terminal_new(STDIN_FILENO, STDOUT_FILENO, opts);  // enters
 *   for (;;) { rolltui_terminal_poll(t, 100, on_event, ctx); ... }  // keys, mouse, paste, resize
 *   rolltui_terminal_write(t, bytes, len);
 *   rolltui_terminal_free(t);  // restores; so does a fatal signal (SIGINT/TERM/HUP/QUIT) —
 *                              // restore first, then re-raise with the default disposition,
 *                              // so the process dies as it would have but never leaves the
 *                              // user staring at an alternate screen with a raw tty.
 *
 * Raw mode here means cfmakeraw: ISIG is off, so Ctrl-C arrives as a key event
 * (plan/phase-9.md: Ctrl-C cancels a turn, twice exits) and SIGINT only ever comes from
 * outside. Ctrl-Z likewise. The restore path is async-signal-safe: precomputed bytes
 * written with write(2) and a tcsetattr of the saved termios.
 *
 * Tested on a real pty pair (rolltui/tests/terminal_test.cpp, unchanged by this port): mode
 * bytes, termios flags, size, events from bytes written to the master, SIGWINCH -> Resize,
 * and a forked child killed by SIGTERM whose restore bytes are seen by the parent.
 *
 * THIS WAS THE EASIEST PORT IN THE SET, AND FOR A REASON WORTH RECORDING (m1's retraction of
 * m5e's closing sentence): `Terminal.cpp` made 43 POSIX C calls already — write x10, read x7,
 * poll x7, signal x5, tcsetattr x3, fcntl x3, sigaction x2, ioctl x2, close x2, plus
 * tcgetattr, isatty, cfmakeraw. It was a C++ wrapper around a C API; touching a file
 * descriptor is the strongest argument FOR C in this library, not an exception to it. What
 * moved was the `std::string` / `std::vector` / `std::chrono` / `std::atomic` / `std::optional`
 * around those calls, never the calls themselves.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14/15 and none new:
 *   1. **THE TERMINAL IS AN OPAQUE HANDLE, OWNED by the caller.** `rolltui::Terminal` holds
 *      one and does the RAII (`rolltui/Terminal.hpp`, kept as the thin C++ shape every
 *      existing caller already writes against — Phase 17 m2 is what deletes that shim, not
 *      this milestone).
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function. Events are reported
 *      through a sink callback, the same shape `rolltui_key_decoder_feed` already uses;
 *      the background-colour query and the negotiated protocol travel through out-params
 *      and a return code, not a struct.
 *   3. **TEXT OUT IS A BORROW WITH A STATED WINDOW.** An event's `text` (an Unknown key's
 *      raw bytes, or a paste's contents) is valid for exactly the duration of the `emit`
 *      call it arrives in — same rule as `rolltui_key_decoder_feed`. The entered/left
 *      escape sequences are a BORROW into the handle, valid until the handle is freed or
 *      `rolltui_terminal_negotiate_keyboard` is called again (the one thing that can grow
 *      the leave sequence after construction, to add a keyboard-protocol pop).
 *   4. **A RESIZE IS NOT DECODED FROM BYTES, SO IT HAS NO KIND IN `rolltui_keys.h` —
 *      THE TERMINAL MAKES IT.** `RolltuiTermEvent` is this module's own envelope, one kind
 *      wider than `RolltuiEvent`: the same key/mouse/paste vocabulary the decoder emits,
 *      plus RESIZE, which only `rolltui_terminal_poll` ever produces (from SIGWINCH, never
 *      from the wire). Keeping it here rather than widening the shared `RolltuiEvent`
 *      mirrors the C++ split exactly: `Keys.hpp`'s `Event` variant carries `ResizeEvent`
 *      as a fourth alternative that Keys.hpp itself defines, not one `KeyDecoder` ever
 *      produces.
 *
 * WHAT THIS FILE DOES NOT DO, on purpose: parse `ROLLTUI_KEY_PROTOCOL`'s string value. The
 * three protocol NAMES ("legacy" / "modifyOtherKeys" / "kitty") are vocabulary that
 * `rolltui/Keys.cpp`'s own header comment already keeps out of the C layer, on purpose, for
 * a config file and a `--keys` flag to spell — so `rolltui_terminal_negotiate_keyboard`
 * takes an already-resolved override (a protocol byte, or negative for "ask the terminal")
 * rather than an environment variable, and the C++ shim reads the environment and calls
 * `parse_key_protocol` before crossing over. One string parser, in one language, is the
 * point; duplicating it here would be a second place for the three spellings to drift.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- options, defined once and compiled by both languages ------------------------------ */
/* The attribute bits are `unsigned char` and not `bool` for `rolltui_style.h`'s reason: C's
 * `_Bool` and C++'s `bool` are the same byte on every toolchain this will ever see, and that
 * is exactly the "layout-compatible by fiat" this project keeps being burned by — one type
 * in both languages, nothing to assume. */
typedef struct RolltuiTerminalOptions {
  unsigned char alt_screen ROLLTUI_DEFAULT(1);
  unsigned char mouse ROLLTUI_DEFAULT(1);        /* SGR 1006 + button + drag reporting */
  unsigned char bracketed_paste ROLLTUI_DEFAULT(1);
  unsigned char hide_cursor ROLLTUI_DEFAULT(1);
  unsigned char handle_signals ROLLTUI_DEFAULT(1); /* restore-and-reraise on INT/TERM/HUP/QUIT */
} RolltuiTerminalOptions;

typedef struct RolltuiTerminal RolltuiTerminal;

/* ---- lifetime --------------------------------------------------------------------------- */
/* OWNED by the caller. Enters immediately (raw mode, alt screen, the rest of `opts`) and
 * negotiates the keyboard protocol before returning, exactly as the C++ constructor did.
 * Never returns NULL: an allocation failure aborts inside rolltui::mem. */
RolltuiTerminal* rolltui_terminal_new(int in_fd, int out_fd, RolltuiTerminalOptions opts);
/* Restores the terminal (see rolltui_terminal_restore_now) and releases everything `t` holds. */
void rolltui_terminal_free(RolltuiTerminal* t);

/* ---- geometry --------------------------------------------------------------------------- */
int rolltui_terminal_is_tty(const RolltuiTerminal* t);
int rolltui_terminal_width(const RolltuiTerminal* t);
int rolltui_terminal_height(const RolltuiTerminal* t);
void rolltui_terminal_refresh_size(RolltuiTerminal* t); /* ioctl; also called on SIGWINCH */

/* ---- events ----------------------------------------------------------------------------- */
/* The same three kinds `rolltui_keys.h` defines, plus RESIZE — see rule 4 above. */
#define ROLLTUI_TERM_EVENT_KEY ROLLTUI_EVENT_KEY
#define ROLLTUI_TERM_EVENT_MOUSE ROLLTUI_EVENT_MOUSE
#define ROLLTUI_TERM_EVENT_PASTE ROLLTUI_EVENT_PASTE
#define ROLLTUI_TERM_EVENT_RESIZE 3

/* ONE event. `text` is a BORROW valid only for the `emit` call (rule 3): an Unknown key's
 * raw bytes (kind KEY, key UNKNOWN) or a paste's contents (kind PASTE). NULL otherwise.
 * `w`/`h` are set only for kind RESIZE. */
typedef struct RolltuiTermEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  const char* text;
  size_t text_len;
  int w, h;
} RolltuiTermEvent;

/* Called once per event, in order. The C++ side appends to its `std::vector<Event>`, the
 * same shape `RolltuiEventFn` already has one layer down. */
typedef void (*RolltuiTermEventFn)(void* ctx, const RolltuiTermEvent* e);

/* Waits up to timeout_ms (-1: forever) for input, a resize or rolltui_terminal_wake(); reports
 * whatever decoded (possibly nothing). A lone ESC that nothing follows within the timeout is
 * delivered as Escape. Bytes left over from a `negotiate_keyboard`/`query_background` call
 * that ran before this one are delivered first, before anything this call reads itself. */
void rolltui_terminal_poll(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx);

/* Makes a blocked poll() return now, with whatever events are pending (possibly none).
 * Thread-safe and async-signal-safe: one byte on the self-pipe. A frontend whose view
 * changes on another thread uses it instead of a short poll timeout. */
void rolltui_terminal_wake(RolltuiTerminal* t);

/* Writes every byte (loops on partial writes and EINTR). */
void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len);

/* ---- keyboard protocol (Phase 12 m3) ----------------------------------------------------- */
/*
 * WHICH KEYBOARD PROTOCOL THIS TERMINAL SPEAKS, asked of the terminal rather than assumed of
 * its name. `rolltui_terminal_new` calls this once, so a host gets it for free; a host that
 * swaps terminals under one process calls it again.
 *
 * The exchange, and it is the terminal's answer in both directions:
 *   ->  CSI ? u        kitty: "which enhancement flags are set?"
 *   ->  CSI ? 4 m      xterm (XTQUERYMODIFIERS): "what is modifyOtherKeys?"
 *   ->  CSI c          Primary DA — every terminal answers this one, which is what makes a
 *                      NEGATIVE answer definitive instead of a timeout guess.
 * A kitty-capable terminal replies `CSI ? flags u` and an xterm-capable one
 * `CSI > 4 ; value m`, both BEFORE the DA reply; a terminal that speaks neither sends only
 * the DA. Whatever else arrived is somebody typing and is kept for the next
 * `rolltui_terminal_poll`, exactly as query_background does — nothing is lost.
 *
 * The answer degrades to Legacy on every uncertainty: a pipe, no reply, a reply not
 * recognised, a timeout. That direction is deliberate — a wrong Legacy answer refuses a
 * chord that would have worked and says so out loud, while a wrong Kitty answer accepts one
 * that silently never fires, which is the defect this whole milestone is about.
 * `forced_protocol` is a ROLLTUI_PROTOCOL_* value to use unconditionally instead of asking
 * (negative: ask the terminal) — the C++ shim passes the resolved value of
 * `ROLLTUI_KEY_PROTOCOL`, for a terminal that lies in either direction.
 *
 * Enabling is part of asking: kitty gets `CSI > 1 u` pushed (popped by `CSI < 1 u` on the
 * way out), modifyOtherKeys gets `CSI > 4 ; 2 m` (reset by `CSI > 4 m`), and both pops are
 * APPENDED to the leave sequence rather than prepended, so bytes already published to the
 * signal-safe restore path (rule 3) keep their offsets: a fatal signal landing mid-publish
 * still writes a complete, valid, shorter sequence. Only ever onto a real terminal: with
 * `forced_protocol` set there may be no tty at all, and writing mode bytes down a pipe would
 * land them in somebody's captured frame.
 *
 * Sets the process-wide active protocol (`rolltui_key_set_active_protocol`) before
 * returning, and returns it as a ROLLTUI_PROTOCOL_* byte.
 */
unsigned char rolltui_terminal_negotiate_keyboard(RolltuiTerminal* t, int timeout_ms, int forced_protocol);
unsigned char rolltui_terminal_key_protocol(const RolltuiTerminal* t);

/* ---- background colour (milestone 11's light/dark auto-detect) -------------------------- */
/* Asks the terminal for its background colour (OSC 11) and waits up to timeout_ms for the
 * reply, writing it to `*out`. Returns 0 on a pipe, on no answer in time (a terminal that
 * does not implement OSC 11 sends nothing), or on an unparseable answer — the caller treats
 * every 0 as "dark". Bytes that arrive and are not the reply (a user already typing) are
 * kept and delivered by the next `rolltui_terminal_poll`; nothing is lost. Call before the
 * event loop, once. */
int rolltui_terminal_query_background(RolltuiTerminal* t, int timeout_ms, RolltuiStyleColor* out);

/* ---- the entered/left sequences ---------------------------------------------------------- */
/* The bytes that entered/will leave the modes, as BORROWS (rule 3), for tests and for
 * `--frame` tooling that wants to reproduce a session without a tty. */
const char* rolltui_terminal_enter_sequence(const RolltuiTerminal* t, size_t* len);
const char* rolltui_terminal_leave_sequence(const RolltuiTerminal* t, size_t* len);

/* ---- signal-safe restore ------------------------------------------------------------------ */
/* Async-signal-safe: restores whichever terminal most recently entered (if any) — a plain
 * process-wide scalar, not a parameter, because a signal handler cannot be handed one.
 * Called by this file's own SIGINT/TERM/HUP/QUIT/WINCH handlers; public so a host with its
 * own crash handler can call it too. */
void rolltui_terminal_restore_now(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_TERMINAL_H */
