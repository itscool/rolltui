/* rolltui/c/rolltui_terminal.c — the C side of the terminal. See rolltui_terminal.h for the
 * boundary's rules and rolltui/Terminal.hpp for the thin C++ shape every existing caller
 * still writes against; rolltui/tests/terminal_test.cpp (unchanged by this port) is the
 * oracle, on a real pty pair.
 *
 * Everything per-handle allocates through the closed set in rolltui_alloc.h. The only
 * process-wide state is the active terminal's restore data, readable from a signal
 * handler — the same shape rolltui_keys.c's active-protocol scalar already uses, one level
 * up in stakes: a signal can land between any two statements here, not just between calls.
 */
#include "rolltui/c/rolltui_terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_theme.h" /* rolltui_parse_osc11_reply */

/* ---- process-wide restore state, readable from a signal handler -----------------------------
 *
 * WHY `_Atomic` AND NOT `volatile sig_atomic_t` FOR THE SCALARS: both are C11-conforming
 * ways to touch a static object from `on_fatal_signal`/`on_winch` (the standard's signal
 * clause permits a lock-free atomic object exactly as it permits `volatile sig_atomic_t`,
 * and every scalar below is lock-free on any toolchain this builds on — the same fact
 * `rolltui_keys.c`'s `g_protocol` already leans on). Two things tip it to `_Atomic` here
 * rather than splitting the choice per variable:
 *   1. `g_handlers_installed` needs an atomic READ-MODIFY-WRITE (test-and-set, so two
 *      Terminals entering back to back install the signal handlers exactly once between
 *      them), and `sig_atomic_t` guarantees plain loads/stores only — no exchange. One
 *      mechanism for every variable in this block is simpler to read than two.
 *   2. `g_have_tio`'s SEQ_CST store is what makes the plain write to `g_saved_tio` just
 *      above it visible-before a reader that observes `g_have_tio` true — a real
 *      happens-before edge in the C11 memory model. `volatile` alone promises no such
 *      ordering; it only forbids the compiler from eliding or reordering the volatile
 *      access itself.
 * `g_leave` (the buffer) stays a plain array: it is fully overwritten by memcpy before
 * `g_leave_len` publishes the new length, so `rolltui_terminal_restore_now` — which only
 * ever reads bytes `[0, g_leave_len)` — can never observe a torn write, whichever length
 * (old or new) it happens to read. */
static _Atomic int g_out_fd = -1;
static _Atomic int g_wake_fd = -1;
static char g_leave[256];
static _Atomic size_t g_leave_len = 0;
static struct termios g_saved_tio;
static _Atomic int g_have_tio = 0;
static _Atomic int g_handlers_installed = 0;

static void write_all_fd(int fd, const char* p, size_t n) {
  while (n > 0) {
    ssize_t k = write(fd, p, n);
    if (k < 0) {
      if (errno == EINTR) continue;
      return;
    }
    p += k;
    n -= (size_t)k;
  }
}

static void on_fatal_signal(int sig) {
  rolltui_terminal_restore_now();
  signal(sig, SIG_DFL);
  raise(sig);
}

static void on_winch(int sig) {
  (void)sig;
  int fd = g_wake_fd;
  if (fd >= 0) {
    char c = 'w';
    ssize_t r = write(fd, &c, 1);
    (void)r;
  }
}

void rolltui_terminal_restore_now(void) {
  int fd = g_out_fd;
  if (fd < 0) return;
  write_all_fd(fd, g_leave, g_leave_len);
  if (g_have_tio) tcsetattr(fd, TCSANOW, &g_saved_tio);
  g_out_fd = -1;
}

/* ---- the handle -----------------------------------------------------------------------------
 *
 * `queued`/`queued_text`: GROWING, AMORTISED (rolltui_alloc.h strategy 2), one buffer per
 * ROLE — events and the text bytes they borrow from, so an Unknown key's raw bytes or a
 * paste's contents queued during `negotiate_keyboard`/`query_background` (bytes that arrive
 * before the reply those functions wait for, i.e. somebody already typing) survive to the
 * next `rolltui_terminal_poll`. `text_off` into `queued_text` rather than a raw pointer: a
 * later queue entry can grow (and so move) the arena before an earlier entry is drained, and
 * an offset survives a realloc where a pointer would dangle. Cleared by resetting the two
 * lengths to 0, never by freeing — the rule for a buffer that gets reused. */
typedef struct QueuedEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  size_t text_off, text_len;
} QueuedEvent;

#define TERM_SEQ_MAX 256 /* generous over the ~55-byte worst case every option can add */

struct RolltuiTerminal {
  int last_press_x, last_press_y, last_press_button; /* the press a second one is paired with */
  unsigned long long last_press_ms;                   /* 0: none */
  int in_fd, out_fd;
  int owned_fd; /* a reopened terminal (see reopen_if_unpollable), closed at free; else -1 */
  int tty;
  int entered;
  int w, h;
  char enter_seq[TERM_SEQ_MAX];
  size_t enter_len;
  char leave_seq[TERM_SEQ_MAX];
  size_t leave_len;
  RolltuiKeyDecoder* decoder;
  QueuedEvent* queued;
  size_t queued_len, queued_cap;
  char* queued_text;
  size_t queued_text_len, queued_text_cap;
  int wake_r, wake_w; /* self-pipe: SIGWINCH handler writes, poll() reads */
  unsigned char protocol;
  int have_tio;
  struct termios saved_tio;
};

static void seq_append(char* seq, size_t* len, const char* lit) {
  size_t n = strlen(lit);
  /* TERM_SEQ_MAX is sized against the closed set of options plus one protocol pop; a
   * caller-supplied string never reaches here. */
  memcpy(seq + *len, lit, n);
  *len += n;
}

/* Publishes `t`'s CURRENT leave sequence to the signal-safe restore path: memcpy first, THEN
 * the length, so a fatal signal landing between the two still sees a complete (if shorter)
 * sequence rather than a torn one — see the comment on the globals above. */
static void publish_leave(const RolltuiTerminal* t) {
  size_t n = t->leave_len < sizeof g_leave ? t->leave_len : sizeof g_leave;
  memcpy(g_leave, t->leave_seq, n);
  g_leave_len = n;
}

/* ---- queuing leftover bytes from negotiate_keyboard/query_background --------------------- */

typedef struct QueueCtx {
  RolltuiTerminal* t;
} QueueCtx;

static void queue_from_decoder(void* ctx, const RolltuiEvent* e) {
  RolltuiTerminal* t = ((QueueCtx*)ctx)->t;
  size_t tlen = e->text ? e->text_len : 0;
  size_t off = t->queued_text_len;
  if (tlen) {
    t->queued_text = rolltui_grow(t->queued_text, &t->queued_text_cap, t->queued_text_len + tlen, 1);
    memcpy(t->queued_text + t->queued_text_len, e->text, tlen);
    t->queued_text_len += tlen;
  }
  t->queued = rolltui_grow(t->queued, &t->queued_cap, t->queued_len + 1, sizeof *t->queued);
  QueuedEvent* qe = &t->queued[t->queued_len++];
  qe->kind = e->kind;
  qe->key = e->key;
  qe->mouse = e->mouse;
  qe->text_off = off;
  qe->text_len = tlen;
}

/* Adapts a decoder event straight to the caller's sink, for the live (non-queued) path:
 * `RolltuiTermEvent` is a strict widening of `RolltuiEvent` (rule 4), so this is a copy and
 * never a decode. `text` stays the SAME borrow the decoder handed over — valid for exactly
 * this call, transitively, since nothing stores it in between. */
typedef struct EmitCtx {
  RolltuiTermEventFn fn;
  void* ctx;
  RolltuiTerminal* t; /* for the double-click pairing; NULL where there is no terminal */
} EmitCtx;

static unsigned long long monotonic_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL;
}

/* A DOUBLE-CLICK IS MADE HERE, from two presses. The decoder sees bytes and knows no clock; this
 * is the one place that has both the event and the time. The press itself is delivered first,
 * as it arrives; the DoubleClick follows it. Pairing is by button and cell, within
 * ROLLTUI_DOUBLE_CLICK_MS, and the pair is spent once made so a third press starts afresh. */
static void emit_from_decoder(void* vctx, const RolltuiEvent* e) {
  EmitCtx* ec = (EmitCtx*)vctx;
  RolltuiTermEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = e->kind;
  ev.key = e->key;
  ev.mouse = e->mouse;
  ev.text = e->text;
  ev.text_len = e->text_len;
  ec->fn(ec->ctx, &ev);
  if (ec->t && e->kind == ROLLTUI_EVENT_MOUSE && e->mouse.kind == 0 /* Press */) {
    RolltuiTerminal* t = ec->t;
    const unsigned long long now = monotonic_ms();
    const int same = t->last_press_ms != 0 && e->mouse.x == t->last_press_x && e->mouse.y == t->last_press_y &&
                     e->mouse.button == t->last_press_button && now - t->last_press_ms <= ROLLTUI_DOUBLE_CLICK_MS;
    if (same) {
      RolltuiTermEvent dbl = ev;
      dbl.mouse.kind = 8; /* DoubleClick */
      ec->fn(ec->ctx, &dbl);
      t->last_press_ms = 0;
    } else {
      t->last_press_ms = now;
      t->last_press_x = e->mouse.x;
      t->last_press_y = e->mouse.y;
      t->last_press_button = e->mouse.button;
    }
  }
}

/* ---- enter / leave ----------------------------------------------------------------------- */

static void term_enter(RolltuiTerminal* t, int handle_signals) {
  if (t->entered) return;
  t->entered = 1;
  if (t->tty && tcgetattr(t->out_fd, &t->saved_tio) == 0) {
    t->have_tio = 1;
    struct termios raw = t->saved_tio;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(t->out_fd, TCSANOW, &raw);
    g_saved_tio = t->saved_tio; /* plain struct copy, ordered before the flag below */
    g_have_tio = 1;
  }
  publish_leave(t);
  g_out_fd = t->out_fd;
  g_wake_fd = t->wake_w;
  rolltui_terminal_write(t, t->enter_seq, t->enter_len);
  rolltui_terminal_negotiate_keyboard(t, 80, -1); /* ask the terminal what it can deliver,
                                                    * before anything is typed */
  if (handle_signals && !atomic_exchange(&g_handlers_installed, 1)) {
    signal(SIGINT, on_fatal_signal);
    signal(SIGTERM, on_fatal_signal);
    signal(SIGHUP, on_fatal_signal);
    signal(SIGQUIT, on_fatal_signal);
  }
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_winch;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGWINCH, &sa, NULL);
}

static void term_leave(RolltuiTerminal* t) {
  if (!t->entered) return;
  t->entered = 0;
  rolltui_terminal_write(t, t->leave_seq, t->leave_len);
  if (t->have_tio) tcsetattr(t->out_fd, TCSANOW, &t->saved_tio);
  g_out_fd = -1;
  g_wake_fd = -1;
  g_have_tio = 0;
}

/* ---- lifetime ---------------------------------------------------------------------------- */

/* A DESCRIPTOR THAT CANNOT BE POLLED IS REOPENED BY ITS REAL NAME. On Darwin, poll(2) on a
 * descriptor opened from the `/dev/tty` alias answers POLLNVAL every time, while read(2) on
 * it works — so a host that opened `/dev/tty` (the ordinary way to draw on the terminal while
 * stdout carries an answer) gets a loop that never sees a key and a negotiation that blocks
 * in read until one arrives by luck. The same device reached through its own name
 * (`/dev/ttys003`, what ttyname(3) gives for the alias) polls normally. Detected rather than
 * assumed, with a zero-timeout poll, so a descriptor that polls is never touched; the
 * reopened one is OWNED here and closed at `rolltui_terminal_free`. */
static int reopen_if_unpollable(int fd) {
  struct pollfd p;
  int std_fd;
  p.fd = fd;
  p.events = POLLIN;
  p.revents = 0;
  if (poll(&p, 1, 0) < 0 || !(p.revents & POLLNVAL)) return -1;
  if (!isatty(fd)) return -1;
  /* ttyname(3) of the alias answers "/dev/tty" again, so the real name has to come from a
   * descriptor that was opened by it: the controlling terminal is the one the standard
   * descriptors sit on, whichever of them is still a terminal. */
  for (std_fd = 0; std_fd < 3; ++std_fd) {
    const char* name = isatty(std_fd) ? ttyname(std_fd) : NULL;
    if (name && strcmp(name, "/dev/tty") != 0) return open(name, O_RDWR | O_CLOEXEC);
  }
  return -1;
}

RolltuiTerminal* rolltui_terminal_new(int in_fd, int out_fd, RolltuiTerminalOptions opts) {
  RolltuiTerminal* t = (RolltuiTerminal*)rolltui_mem_alloc(sizeof *t);
  memset(t, 0, sizeof *t);
  t->in_fd = in_fd;
  t->out_fd = out_fd;
  {
    const int r = reopen_if_unpollable(in_fd);
    if (r >= 0) {
      t->owned_fd = r;
      t->in_fd = r;
      if (out_fd == in_fd) t->out_fd = r;
    } else {
      t->owned_fd = -1;
    }
  }
  t->tty = isatty(out_fd) != 0;
  t->w = 80;
  t->h = 24;
  t->wake_r = t->wake_w = -1;
  int wake[2];
  if (pipe(wake) == 0) {
    fcntl(wake[0], F_SETFL, O_NONBLOCK);
    fcntl(wake[1], F_SETFL, O_NONBLOCK);
    t->wake_r = wake[0];
    t->wake_w = wake[1];
  }
  /* Built in the SAME final byte order the C++ `insert(0, ...)` chain produced: enter_seq
   * appends in option order; leave_seq is attributes-off first, then each mode's "off" in
   * the REVERSE of enter_seq's order — undo last-enabled-first, with attributes-off always
   * leading regardless. */
  if (opts.alt_screen) seq_append(t->enter_seq, &t->enter_len, "\x1b[?1049h");
  if (opts.mouse) seq_append(t->enter_seq, &t->enter_len, "\x1b[?1000h\x1b[?1002h\x1b[?1006h");
  if (opts.bracketed_paste) seq_append(t->enter_seq, &t->enter_len, "\x1b[?2004h");
  if (opts.hide_cursor) seq_append(t->enter_seq, &t->enter_len, "\x1b[?25l");
  seq_append(t->leave_seq, &t->leave_len, "\x1b[0m"); /* attributes off before anything else */
  if (opts.hide_cursor) seq_append(t->leave_seq, &t->leave_len, "\x1b[?25h");
  if (opts.bracketed_paste) seq_append(t->leave_seq, &t->leave_len, "\x1b[?2004l");
  if (opts.mouse) seq_append(t->leave_seq, &t->leave_len, "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
  if (opts.alt_screen) seq_append(t->leave_seq, &t->leave_len, "\x1b[?1049l");
  t->decoder = rolltui_key_decoder_new();
  term_enter(t, opts.handle_signals);
  rolltui_terminal_refresh_size(t);
  return t;
}

void rolltui_terminal_free(RolltuiTerminal* t) {
  if (!t) return;
  term_leave(t);
  if (t->owned_fd >= 0) close(t->owned_fd); /* after leave: the restore bytes went down it */
  if (t->wake_r >= 0) close(t->wake_r);
  if (t->wake_w >= 0) close(t->wake_w);
  rolltui_key_decoder_free(t->decoder);
  rolltui_mem_free(t->queued);
  rolltui_mem_free(t->queued_text);
  rolltui_mem_free(t);
}

/* ---- geometry ---------------------------------------------------------------------------- */

int rolltui_terminal_is_tty(const RolltuiTerminal* t) { return t->tty; }
int rolltui_terminal_width(const RolltuiTerminal* t) { return t->w; }
int rolltui_terminal_height(const RolltuiTerminal* t) { return t->h; }

void rolltui_terminal_refresh_size(RolltuiTerminal* t) {
  struct winsize ws;
  memset(&ws, 0, sizeof ws);
  if (ioctl(t->out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    t->w = ws.ws_col;
    t->h = ws.ws_row;
  }
}

void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len) { write_all_fd(t->out_fd, bytes, len); }

void rolltui_terminal_wake(RolltuiTerminal* t) {
  if (t->wake_w < 0) return;
  char c = 'k';
  ssize_t r = write(t->wake_w, &c, 1); /* non-blocking; a full pipe already wakes */
  (void)r;
}

void rolltui_terminal_suspend(RolltuiTerminal* t) { term_leave(t); }
void rolltui_terminal_resume(RolltuiTerminal* t) { term_enter(t, 0); /* the handlers, if asked for, are installed */ }

const char* rolltui_terminal_enter_sequence(const RolltuiTerminal* t, size_t* len) {
  *len = t->enter_len;
  return t->enter_seq;
}
const char* rolltui_terminal_leave_sequence(const RolltuiTerminal* t, size_t* len) {
  *len = t->leave_len;
  return t->leave_seq;
}

/* ---- keyboard protocol negotiation ------------------------------------------------------- */

/* One CSI sequence starting at `pos`, or 0 if the bytes there are not a complete one. */
static size_t csi_span(const char* s, size_t n, size_t pos) {
  if (pos + 1 >= n || s[pos] != '\x1b' || s[pos + 1] != '[') return 0;
  size_t i = pos + 2;
  while (i < n && (unsigned char)s[i] >= 0x30 && (unsigned char)s[i] <= 0x3F) ++i;
  while (i < n && (unsigned char)s[i] >= 0x20 && (unsigned char)s[i] <= 0x2F) ++i;
  if (i >= n) return 0;
  return ((unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7E) ? i + 1 - pos : 0;
}

/* now-to-deadline in milliseconds, CLOCK_MONOTONIC — the same clock std::chrono::steady_clock
 * maps to on Darwin, and for the same reason: immune to a wall-clock step. */
static struct timespec deadline_from(int timeout_ms) {
  struct timespec d;
  clock_gettime(CLOCK_MONOTONIC, &d);
  d.tv_sec += timeout_ms / 1000;
  d.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
  if (d.tv_nsec >= 1000000000L) {
    d.tv_sec += 1;
    d.tv_nsec -= 1000000000L;
  }
  return d;
}
static long ms_until(const struct timespec* deadline) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (long)((deadline->tv_sec - now.tv_sec) * 1000 + (deadline->tv_nsec - now.tv_nsec) / 1000000);
}

unsigned char rolltui_terminal_negotiate_keyboard(RolltuiTerminal* t, int timeout_ms, int forced_protocol) {
  t->protocol = ROLLTUI_PROTOCOL_LEGACY; /* the conservative answer, and the one every failure keeps */
  if (forced_protocol >= 0) {
    t->protocol = (unsigned char)forced_protocol;
  } else if (t->tty) {
    rolltui_terminal_write(t,
                           "\x1b[?u"  /* kitty: which enhancement flags are set? */
                           "\x1b[?4m" /* xterm XTQUERYMODIFIERS: what is modifyOtherKeys? */
                           "\x1b[c", /* Primary DA: the terminator every terminal answers */
                           4 + 5 + 3);
    char* buf = NULL;
    size_t buf_len = 0, buf_cap = 0;
    int saw_da = 0;
    struct timespec deadline = deadline_from(timeout_ms);
    while (!saw_da) {
      long left = ms_until(&deadline);
      if (left <= 0) break;
      struct pollfd one;
      one.fd = t->in_fd;
      one.events = POLLIN;
      one.revents = 0;
      int n = poll(&one, 1, (int)left);
      if (n < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (n == 0) break;
      char b[512];
      ssize_t k = read(t->in_fd, b, sizeof b);
      if (k <= 0) break;
      buf = rolltui_grow(buf, &buf_cap, buf_len + (size_t)k, 1);
      memcpy(buf + buf_len, b, (size_t)k);
      buf_len += (size_t)k;
      saw_da = 0;
      for (size_t i = 0; i + 2 < buf_len; ++i) {
        size_t len = csi_span(buf, buf_len, i);
        if (len && buf[i + 2] == '?' && buf[i + len - 1] == 'c') saw_da = 1;
      }
    }
    /* Split the replies we asked for from everything else, which is somebody typing. */
    char* rest = NULL;
    size_t rest_len = 0, rest_cap = 0;
    for (size_t i = 0; i < buf_len;) {
      size_t len = csi_span(buf, buf_len, i);
      if (!len) {
        rest = rolltui_grow(rest, &rest_cap, rest_len + 1, 1);
        rest[rest_len++] = buf[i];
        ++i;
        continue;
      }
      char final = buf[i + len - 1];
      char lead = len > 2 ? buf[i + 2] : '\0';
      if (final == 'u' && lead == '?') t->protocol = ROLLTUI_PROTOCOL_KITTY;              /* CSI ? flags u */
      else if (final == 'm' && lead == '>') t->protocol = ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS; /* CSI > 4 ; value m */
      else if (!(final == 'c' && lead == '?')) {                                          /* not a reply: input */
        rest = rolltui_grow(rest, &rest_cap, rest_len + len, 1);
        memcpy(rest + rest_len, buf + i, len);
        rest_len += len;
      }
      i += len;
    }
    if (rest_len) {
      QueueCtx qc;
      qc.t = t;
      rolltui_key_decoder_feed(t->decoder, rest, rest_len, queue_from_decoder, &qc);
    }
    rolltui_mem_free(rest);
    rolltui_mem_free(buf);
  }
  /* Turn on what was found, and make sure every exit path turns it back off. The pop is
   * APPENDED to leave_seq rather than prepended so the bytes already copied into the
   * signal handler's fixed buffer keep their offsets: a fatal signal landing between the
   * memcpy and the length store then still writes a complete, valid, shorter sequence.
   * Only ever onto a real terminal: with forced_protocol set there may be no tty at all,
   * and writing mode bytes down a pipe would land them in somebody's captured frame. */
  if (t->tty && t->protocol == ROLLTUI_PROTOCOL_KITTY) {
    rolltui_terminal_write(t, "\x1b[>1u", 5); /* push the disambiguate flag */
    seq_append(t->leave_seq, &t->leave_len, "\x1b[<1u"); /* pop it */
  } else if (t->tty && t->protocol == ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS) {
    /* Mode 1, not 2: "encode only keys with modifiers that produce non-standard results",
     * which is exactly what encode_key models. Mode 2 also escapes keys that would produce
     * a printable character, and its shift handling is the part xterm's own documentation
     * declines to pin down. */
    rolltui_terminal_write(t, "\x1b[>4;1m", 7);
    seq_append(t->leave_seq, &t->leave_len, "\x1b[>4m"); /* no value: back to initial state */
  }
  if (t->entered) publish_leave(t);
  rolltui_key_set_active_protocol(t->protocol);
  return t->protocol;
}

unsigned char rolltui_terminal_key_protocol(const RolltuiTerminal* t) { return t->protocol; }

/* ---- background colour ------------------------------------------------------------------- */

/* First byte of `needle` in s[from, n), or (size_t)-1. Hand-rolled rather than a libc
 * extension (`memmem` is BSD/Darwin-only and nothing else in this library reaches for it). */
static size_t find_sub(const char* s, size_t n, const char* needle, size_t needle_len, size_t from) {
  if (needle_len == 0 || needle_len > n) return (size_t)-1;
  for (size_t i = from; i + needle_len <= n; ++i)
    if (memcmp(s + i, needle, needle_len) == 0) return i;
  return (size_t)-1;
}
static size_t find_char(const char* s, size_t n, char c, size_t from) {
  for (size_t i = from; i < n; ++i)
    if (s[i] == c) return i;
  return (size_t)-1;
}

/* HOW WIDE THIS TERMINAL DRAWS AN AMBIGUOUS GLYPH, asked rather than assumed.
 *
 * East Asian AMBIGUOUS characters are one cell on most terminals and two on others, and nothing
 * in the environment says which. Guessing wrong splits a two-cell glyph across a one-cell track
 * or leaves a hole: the border set, the block elements and the scrollbar thumb are all in that
 * class. It is not a preference, so it is not a setting to type — it is a fact, and a terminal
 * that answers CPR will tell you.
 *
 * Draw one ambiguous glyph at a known column, ask where the cursor ended up, put it back. A
 * reply of column 3 means the glyph took two cells, column 2 means one. Anything else — no
 * reply, a terminal that does not answer, a timeout — leaves `*out` untouched, so the caller's
 * own default stands and a silent terminal never becomes a wrong answer.
 *
 * Drawn inside a save/restore pair and erased, and every host repaints the whole screen on its
 * first frame regardless, so nothing of this survives into the session. */
int rolltui_terminal_query_ambiguous_wide(RolltuiTerminal* t, int timeout_ms, int* out) {
  char* buf = NULL;
  size_t buf_len = 0, buf_cap = 0;
  int found = 0;
  struct timespec deadline;
  if (!t->tty || !out) return 0;
  /* DECSC, home, one ambiguous glyph (U+2588), CPR, erase the line, DECRC. */
  rolltui_terminal_write(t, "\x1b" "7\x1b[1;1H\xE2\x96\x88\x1b[6n\x1b[1;1H\x1b[K\x1b" "8", 20);
  deadline = deadline_from(timeout_ms);
  for (;;) {
    long left = ms_until(&deadline);
    struct pollfd one;
    int n;
    ssize_t k;
    char b[512];
    size_t at, end, i;
    if (left <= 0) break;
    one.fd = t->in_fd;
    one.events = POLLIN;
    one.revents = 0;
    n = poll(&one, 1, (int)left);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    k = read(t->in_fd, b, sizeof b);
    if (k <= 0) break;
    buf = rolltui_grow(buf, &buf_cap, buf_len + (size_t)k, 1);
    memcpy(buf + buf_len, b, (size_t)k);
    buf_len += (size_t)k;
    at = find_sub(buf, buf_len, "\x1b[", 2, 0);
    if (at == (size_t)-1) continue;
    end = (size_t)-1;
    for (i = at + 2; i < buf_len; ++i)
      if (buf[i] == 'R') { end = i; break; }
    if (end == (size_t)-1) continue;
    {
      /* ESC [ row ; col R — only the column matters. */
      size_t semi = (size_t)-1;
      for (i = at + 2; i < end; ++i)
        if (buf[i] == ';') { semi = i; break; }
      if (semi != (size_t)-1) {
        int col = 0;
        for (i = semi + 1; i < end; ++i) {
          if (buf[i] < '0' || buf[i] > '9') { col = 0; break; }
          col = col * 10 + (buf[i] - '0');
        }
        if (col >= 2) {
          *out = col >= 3 ? 1 : 0;
          found = 1;
        }
      }
    }
    memmove(buf + at, buf + end + 1, buf_len - (end + 1));
    buf_len -= (end + 1 - at);
    break;
  }
  if (buf_len) {
    QueueCtx qc;
    qc.t = t;
    rolltui_key_decoder_feed(t->decoder, buf, buf_len, queue_from_decoder, &qc);
  }
  rolltui_mem_free(buf);
  return found;
}

int rolltui_terminal_query_background(RolltuiTerminal* t, int timeout_ms, RolltuiStyleColor* out) {
  if (!t->tty) return 0;
  rolltui_terminal_write(t, "\x1b]11;?\x1b\\", 8); /* ESC ] 1 1 ; ? ESC \ */
  char* buf = NULL;
  size_t buf_len = 0, buf_cap = 0;
  int found = 0;
  struct timespec deadline = deadline_from(timeout_ms);
  for (;;) {
    long left = ms_until(&deadline);
    if (left <= 0) break;
    struct pollfd one;
    one.fd = t->in_fd;
    one.events = POLLIN;
    one.revents = 0;
    int n = poll(&one, 1, (int)left);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    char b[512];
    ssize_t k = read(t->in_fd, b, sizeof b);
    if (k <= 0) break;
    buf = rolltui_grow(buf, &buf_cap, buf_len + (size_t)k, 1);
    memcpy(buf + buf_len, b, (size_t)k);
    buf_len += (size_t)k;
    size_t at = find_sub(buf, buf_len, "\x1b]11;", 5, 0);
    if (at == (size_t)-1) continue;
    /* A complete reply ends in ST (ESC \) or BEL; wait for it. */
    size_t st = find_sub(buf, buf_len, "\x1b\\", 2, at + 5);
    size_t bel = find_char(buf, buf_len, '\a', at + 5);
    size_t end = (size_t)-1, end_len = 0;
    if (st != (size_t)-1 && (bel == (size_t)-1 || st < bel)) {
      end = st;
      end_len = 2;
    } else if (bel != (size_t)-1) {
      end = bel;
      end_len = 1;
    }
    if (end == (size_t)-1) continue;
    found = rolltui_parse_osc11_reply(buf + at, end + end_len - at, out);
    /* Drop the reply from `buf`; whatever remains (before and after) is input. */
    memmove(buf + at, buf + end + end_len, buf_len - (end + end_len));
    buf_len -= (end + end_len - at);
    break;
  }
  /* Whatever else arrived is input, not the reply: decode it for the next poll(). */
  if (buf_len) {
    QueueCtx qc;
    qc.t = t;
    rolltui_key_decoder_feed(t->decoder, buf, buf_len, queue_from_decoder, &qc);
  }
  rolltui_mem_free(buf);
  return found;
}

/* ---- poll -------------------------------------------------------------------------------- */

void rolltui_terminal_poll(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx) {
  /* Decoded during a query (negotiate_keyboard/query_background); handed out by the next
   * poll(), BEFORE anything below — even if the syscall poll fails, whatever was already
   * queued is still reported (see the C++ original's identical ordering). */
  if (t->queued_len) {
    for (size_t i = 0; i < t->queued_len; ++i) {
      const QueuedEvent* qe = &t->queued[i];
      RolltuiTermEvent ev;
      memset(&ev, 0, sizeof ev);
      ev.kind = qe->kind;
      ev.key = qe->key;
      ev.mouse = qe->mouse;
      ev.text_len = qe->text_len;
      ev.text = qe->text_len ? t->queued_text + qe->text_off : NULL;
      emit(ctx, &ev);
    }
    t->queued_len = 0;      /* capacity KEPT: a reset is not a free (CLAUDE.md, Phase 13) */
    t->queued_text_len = 0;
    timeout_ms = 0; /* deliver now; pick up anything else already waiting */
  }
  struct pollfd fds[2];
  fds[0].fd = t->in_fd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  fds[1].fd = t->wake_r;
  fds[1].events = POLLIN;
  fds[1].revents = 0;
  int n = poll(fds, t->wake_r >= 0 ? 2 : 1, timeout_ms);
  if (n < 0) return; /* EINTR or otherwise: whatever was queued is already reported above */
  if (n == 0) {       /* timeout: a pending ESC is the Escape key */
    if (rolltui_key_decoder_pending(t->decoder)) {
      EmitCtx ec;
      ec.fn = emit;
      ec.ctx = ctx;
      ec.t = t;
      rolltui_key_decoder_flush(t->decoder, emit_from_decoder, &ec);
    }
    return;
  }
  if (fds[1].revents & POLLIN) {
    char buf[64];
    int winch = 0;
    ssize_t k;
    while ((k = read(t->wake_r, buf, sizeof buf)) > 0)
      for (ssize_t i = 0; i < k; ++i) winch |= (buf[i] == 'w'); /* 'k' is a plain wake() */
    /* Every SIGWINCH is reported, even when the size reads back the same: the frame may
     * still need a full repaint (some terminals clear on a font change). */
    if (winch) {
      rolltui_terminal_refresh_size(t);
      RolltuiTermEvent ev;
      memset(&ev, 0, sizeof ev);
      ev.kind = ROLLTUI_TERM_EVENT_RESIZE;
      ev.w = t->w;
      ev.h = t->h;
      emit(ctx, &ev);
    }
  }
  if (fds[0].revents & (POLLIN | POLLHUP)) {
    char buf[4096];
    ssize_t k = read(t->in_fd, buf, sizeof buf);
    if (k > 0) {
      EmitCtx ec;
      ec.fn = emit;
      ec.ctx = ctx;
      ec.t = t;
      rolltui_key_decoder_feed(t->decoder, buf, (size_t)k, emit_from_decoder, &ec);
      /* A lone ESC (or a sequence cut by the read boundary) waits for the next read, but
       * not forever: give it 30 ms and then resolve it. */
      if (rolltui_key_decoder_pending(t->decoder)) {
        struct pollfd one;
        one.fd = t->in_fd;
        one.events = POLLIN;
        one.revents = 0;
        if (poll(&one, 1, 30) > 0 && (one.revents & POLLIN)) {
          ssize_t k2 = read(t->in_fd, buf, sizeof buf);
          if (k2 > 0) rolltui_key_decoder_feed(t->decoder, buf, (size_t)k2, emit_from_decoder, &ec);
        }
        if (rolltui_key_decoder_pending(t->decoder)) rolltui_key_decoder_flush(t->decoder, emit_from_decoder, &ec);
      }
    }
  }
}
