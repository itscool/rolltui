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

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/c/rolltui_theme.h" /* rolltui_parse_osc11_reply */

/* The process-wide restore, which a fatal signal reaches, lives with the operating system's side
 * (rolltui_os.h): the terminal it names and the bytes that leave it are published at each entry. */
void rolltui_terminal_restore_now(void) { rolltui_os_tty_restore_now(); }

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
  RolltuiOsTty* os; /* the input, the output, the saved mode and the wake channel */
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
  unsigned char protocol;
  int have_tio;
  /* ---- what the terminal is (rolltui_termfacts.h) ---- */
  RolltuiTermFacts facts;
  RolltuiTermEnv env;
  RolltuiTermCacheEntry entry; /* what is remembered for this fingerprint; `probed` 0: nothing yet */
  RolltuiStr cache_path;       /* OWNED; empty: nothing is read or written */
  char fingerprint[512];
  int plan;                    /* PLAN_*: which questions the next entry asks */
  int timeout_ms;              /* how long the first exchange waits: longer over ssh */
  int multiplexed;
  int depth_override;          /* -1: none; else a ROLLTUI_DEPTH_* a host said outright */
  unsigned char protocol_on;   /* the keyboard protocol is currently enabled on the terminal */
  unsigned char pops;          /* bit 0: kitty's pop is in leave_seq, bit 1: modifyOtherKeys's */
  unsigned char revalidate;    /* a remembered background is to be re-checked at the next poll */
  unsigned char facts_seen;    /* a host has read the facts */
  unsigned char reported_mode; /* the mode it read */
  unsigned char facts_dirty;   /* the mode changed after it read: a FACTS event is owed */
  unsigned char publishing;    /* this terminal's facts are the process-wide ones (publish_depth) */
  unsigned char facts_events;  /* the host asked to be told of a change (RolltuiTerminalOptions::facts_events) */
};

enum { PLAN_KEYBOARD = 0, PLAN_FULL = 1, PLAN_CACHED = 2 };

static void seq_append(char* seq, size_t* len, const char* lit) {
  size_t n = strlen(lit);
  /* TERM_SEQ_MAX is sized against the closed set of options plus one protocol pop; a
   * caller-supplied string never reaches here. */
  memcpy(seq + *len, lit, n);
  *len += n;
}

/* Publishes `t`'s CURRENT leave sequence to the signal-safe restore path. */
static void publish_leave(const RolltuiTerminal* t) { rolltui_os_tty_publish(t->os, t->leave_seq, t->leave_len); }

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
    const unsigned long long now = rolltui_os_monotonic_ms();
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

static void run_negotiation(RolltuiTerminal* t);
static void init_facts(RolltuiTerminal* t, const RolltuiTerminalOptions* opts);
static void on_reply(void* ctx, const char* bytes, size_t len);

static void term_enter(RolltuiTerminal* t, int handle_signals) {
  if (t->entered) return;
  t->entered = 1;
  t->have_tio = rolltui_os_tty_raw(t->os);
  publish_leave(t);
  rolltui_os_tty_listen_resize(t->os);
  rolltui_terminal_write(t, t->enter_seq, t->enter_len);
  run_negotiation(t); /* ask the terminal what it can deliver and what it is, before anything is typed */
  if (handle_signals) rolltui_os_tty_install_handlers();
}

static void term_leave(RolltuiTerminal* t) {
  if (!t->entered) return;
  t->entered = 0;
  rolltui_terminal_write(t, t->leave_seq, t->leave_len);
  if (t->have_tio) rolltui_os_tty_restore(t->os);
  t->protocol_on = 0; /* the pop just went down: a resume enables it again */
  rolltui_os_tty_withdraw();
}

/* ---- lifetime ---------------------------------------------------------------------------- */

RolltuiTerminal* rolltui_terminal_new(int in_fd, int out_fd, RolltuiTerminalOptions opts) {
  RolltuiTerminal* t = (RolltuiTerminal*)rolltui_mem_alloc(sizeof *t);
  memset(t, 0, sizeof *t);
  t->os = rolltui_os_tty_open(in_fd, out_fd);
  t->tty = rolltui_os_tty_is_tty(t->os);
  t->w = 80;
  t->h = 24;
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
  rolltui_key_decoder_set_reply_sink(t->decoder, on_reply, t);
  init_facts(t, &opts);
  term_enter(t, opts.handle_signals);
  rolltui_terminal_refresh_size(t);
  return t;
}

void rolltui_terminal_free(RolltuiTerminal* t) {
  if (!t) return;
  term_leave(t);
  if (t->publishing) { /* nothing is being drawn to any more */
    rolltui_termfacts_set_active(-1, 0);
    rolltui_termfacts_set_active_wide(-1);
    rolltui_termfacts_set_active_mode(-1);
  }
  rolltui_os_tty_close(t->os); /* after leave: the restore bytes went down it */
  rolltui_key_decoder_free(t->decoder);
  rolltui_str_free(&t->cache_path);
  rolltui_mem_free(t->queued);
  rolltui_mem_free(t->queued_text);
  rolltui_mem_free(t);
}

/* ---- geometry ---------------------------------------------------------------------------- */

int rolltui_terminal_is_tty(const RolltuiTerminal* t) { return t->tty; }
int rolltui_terminal_width(const RolltuiTerminal* t) { return t->w; }
int rolltui_terminal_height(const RolltuiTerminal* t) { return t->h; }

void rolltui_terminal_refresh_size(RolltuiTerminal* t) {
  int w = 0, h = 0;
  if (rolltui_os_tty_size(t->os, &w, &h)) {
    t->w = w;
    t->h = h;
  }
}

void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len) { rolltui_os_tty_write(t->os, bytes, len); }

void rolltui_terminal_wake(RolltuiTerminal* t) { rolltui_os_tty_wake(t->os); }

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

/* A deadline on the steady clock, and the milliseconds left until it. */
static unsigned long long deadline_from(int timeout_ms) { return rolltui_os_monotonic_ms() + (unsigned long long)timeout_ms; }
static long ms_until(const unsigned long long* deadline) {
  const unsigned long long now = rolltui_os_monotonic_ms();
  return now >= *deadline ? 0 : (long)(*deadline - now);
}

/* Waits for input until `left` runs out and reads it: bytes read, 0 to try again, -1 to stop. */
static long read_until(RolltuiTerminal* t, long left, char* b, size_t cap) {
  const int r = rolltui_os_tty_wait(t->os, (int)left, 0);
  if (r == ROLLTUI_OS_TTY_AGAIN) return 0;
  if (r <= 0) return -1;
  {
    const long k = rolltui_os_tty_read(t->os, b, cap);
    return k > 0 ? k : -1;
  }
}

/* ---- what the terminal is: the facts, remembered ------------------------------------------- */

static int same_colour(RolltuiStyleColorRaw a, RolltuiStyleColorRaw b) {
  return a.kind == b.kind && a.r == b.r && a.g == b.g && a.b == b.b && a.index == b.index;
}

/* PUBLISHES WHAT THE TERMINAL IS — its depth as the ceiling `rolltui_swap_present` holds every host to, its
 * glyph width for every width the library computes, its light or dark for a theme that says auto
 * (rolltui_termfacts.h) — so a host that never asks is still right. Only for a real terminal: a pipe is
 * nothing to be held to, and a golden test wants the depth it chose. */
static void publish_depth(RolltuiTerminal* t) {
  int forced;
  if (!t->tty) return;
  forced = t->depth_override >= 0 || t->facts.depth_source == ROLLTUI_FACT_FORCED;
  rolltui_termfacts_set_active(t->depth_override >= 0 ? t->depth_override : t->facts.depth, forced);
  rolltui_termfacts_set_active_wide(t->facts.ambiguous_wide);
  rolltui_termfacts_set_active_mode(t->facts.mode);
  t->publishing = 1;
}

/* Everything the environment says, the remembered answers for THIS fingerprint if there are any
 * and they are fresh, and — from those — WHICH QUESTIONS the first exchange must ask. No byte goes
 * to the terminal here; `run_negotiation` does the asking, inside `term_enter`, so a resume does
 * not ask twice. */
static void init_facts(RolltuiTerminal* t, const RolltuiTerminalOptions* opts) {
  t->depth_override = -1;
  t->facts_events = opts->facts_events;
  t->plan = PLAN_KEYBOARD;
  t->timeout_ms = 80;
  rolltui_termenv_read(&t->env);
  rolltui_termfacts_from_env(&t->env, &t->facts);
  t->multiplexed = rolltui_termenv_multiplexed(&t->env);
  /* Over ssh a reply takes a network round trip, and 80 ms is a timeout that a working terminal
   * across a slow link would miss — after which the keyboard is judged Legacy and the answers
   * remembered as "silent". Wait for the wire when there is one. */
  if (t->env.ssh_connection) t->timeout_ms = 400;
  {
    /* WHICH BUILD IS ASKING, so a new release asks again without any host saying so: the
     * executable's own name, size and modification time, then whatever the host adds. */
    char exe[256], key[512];
    rolltui_termfacts_exe_identity(exe, sizeof exe);
    snprintf(key, sizeof key, "%s%s%s", exe, (exe[0] && opts->app_key && *opts->app_key) ? "|" : "",
             opts->app_key ? opts->app_key : "");
    rolltui_termfacts_fingerprint(&t->env, key, t->fingerprint, sizeof t->fingerprint);
  }
  if (!opts->no_cache) rolltui_termcache_path(opts->cache_dir, &t->cache_path);
  if (!t->tty || opts->no_probe || t->env.no_probe) return; /* the keyboard question alone, exactly as before */
  t->plan = PLAN_FULL;
  /* Answers behind a multiplexer are used but never remembered or trusted: the terminal it is
   * attached to can change without one byte of the environment changing. */
  if (!t->multiplexed && t->cache_path.n && rolltui_termcache_load(t->cache_path.p, t->fingerprint, &t->entry) &&
      rolltui_termcache_fresh(&t->entry, (long long)time(NULL)))
    t->plan = PLAN_CACHED;
  else
    memset(&t->entry, 0, sizeof t->entry);
}

/* The remembered answers become the facts, and a remembered background is re-checked at the
 * first poll — which is what makes it safe to have skipped the question. */
static void adopt_cache(RolltuiTerminal* t) {
  RolltuiTermFacts* f = &t->facts;
  const RolltuiTermCacheEntry* e = &t->entry;
  f->remembered = 1;
  f->responsive = e->responsive;
  if (!e->responsive) return;
  if (e->has_wide && f->ambiguous_source != ROLLTUI_FACT_FORCED) {
    f->ambiguous_wide = e->ambiguous_wide;
    f->ambiguous_source = ROLLTUI_FACT_CACHE;
  }
  if (e->has_depth && f->depth_source != ROLLTUI_FACT_FORCED) {
    f->depth = e->depth;
    f->depth_source = ROLLTUI_FACT_CACHE;
  }
  if (e->has_background) {
    f->background = e->background;
    f->has_background = 1;
    f->mode = rolltui_mode_for_background(e->background);
    f->mode_source = ROLLTUI_FACT_CACHE;
  }
  if (e->name[0]) snprintf(f->name, sizeof f->name, "%s", e->name);
  t->revalidate = e->answers_background;
}

/* Writes what THIS exchange learned under this fingerprint. */
static void remember(RolltuiTerminal* t, const RolltuiTermReplies* rep) {
  RolltuiTermCacheEntry e;
  if (t->multiplexed || !t->cache_path.n || !t->tty) return;
  memset(&e, 0, sizeof e);
  e.probed = (long long)time(NULL);
  e.responsive = t->facts.responsive;
  e.keyboard = t->facts.keyboard;
  if (rep->cpr && rep->cpr_col >= 2) {
    e.has_wide = 1;
    e.ambiguous_wide = t->facts.ambiguous_wide;
  }
  if (t->facts.depth_source == ROLLTUI_FACT_PROBE) {
    e.has_depth = 1;
    e.depth = t->facts.depth;
  }
  e.answers_background = rep->has_bg ? 1 : 0;
  if (rep->has_bg) {
    e.has_background = 1;
    e.background = rep->bg;
  }
  snprintf(e.name, sizeof e.name, "%s", t->facts.name);
  t->entry = e;
  rolltui_termcache_store(t->cache_path.p, t->fingerprint, &e);
}

/* A background reply that arrived AFTER the exchange — the re-check of a remembered one, or a slow
 * terminal's late answer. It updates the facts and the memory, and owes the host a FACTS event
 * only when it changes what the host was told. */
static void late_background(RolltuiTerminal* t, RolltuiStyleColorRaw c) {
  RolltuiTermFacts* f = &t->facts;
  const unsigned char mode = (unsigned char)rolltui_mode_for_background(c);
  f->background = c;
  f->has_background = 1;
  f->mode = mode;
  f->mode_source = ROLLTUI_FACT_PROBE;
  if (t->publishing) rolltui_termfacts_set_active_mode(mode);
  if (t->facts_events && t->facts_seen && mode != t->reported_mode) t->facts_dirty = 1;
  if (!t->multiplexed && t->cache_path.n && t->entry.probed &&
      (!t->entry.has_background || !same_colour(t->entry.background, c))) {
    t->entry.has_background = 1;
    t->entry.background = c;
    t->entry.answers_background = 1;
    rolltui_termcache_store(t->cache_path.p, t->fingerprint, &t->entry);
  }
}

/* The decoder's sink for a reply that is not a key. Only an OSC 11 answer changes anything here. */
static void on_reply(void* ctx, const char* bytes, size_t len) {
  RolltuiTerminal* t = (RolltuiTerminal*)ctx;
  RolltuiTermReplies rep;
  char tmp[512];
  if (len == 0 || len >= sizeof tmp) return;
  memcpy(tmp, bytes, len);
  memset(&rep, 0, sizeof rep);
  rolltui_termreplies_take(tmp, len, 0, &rep);
  if (rep.has_bg) late_background(t, rep.bg);
}

/* ONE EXCHANGE. The keyboard question (unless the protocol is forced), the questions in `want`,
 * and Primary DA behind all of them, in a single write; replies are read until DA arrives or the
 * deadline passes. What is left in the buffer is somebody typing and goes to the decoder. A
 * question the terminal did not answer is unknown, never "no" — DA arriving proves it had every
 * chance to. `*rep_out` gets what was heard, for the caller that remembers it. */
static unsigned char negotiate_batch(RolltuiTerminal* t, int timeout_ms, int forced_protocol, unsigned want,
                                     RolltuiTermReplies* rep_out) {
  RolltuiTermReplies rep;
  unsigned asked = 0;
  memset(&rep, 0, sizeof rep);
  t->protocol = ROLLTUI_PROTOCOL_LEGACY; /* the conservative answer, and the one every failure keeps */
  if (forced_protocol >= 0) t->protocol = (unsigned char)forced_protocol;
  if (t->tty) {
    asked = want & ~(unsigned)ROLLTUI_TERMQ_KEYBOARD;
    if (forced_protocol < 0) asked |= ROLLTUI_TERMQ_KEYBOARD;
  }
  if (asked) {
    char q[512];
    const size_t qlen = rolltui_termprobe_queries(asked, q, sizeof q);
    char* buf = NULL;
    size_t buf_len = 0, buf_cap = 0;
    unsigned long long deadline = deadline_from(timeout_ms);
    rolltui_terminal_write(t, q, qlen);
    while (!rep.da1) {
      long left = ms_until(&deadline);
      char b[512];
      long k;
      if (left <= 0) break;
      k = read_until(t, left, b, sizeof b);
      if (k == 0) continue;
      if (k < 0) break;
      buf = (char*)rolltui_grow(buf, &buf_cap, buf_len + (size_t)k, 1);
      memcpy(buf + buf_len, b, (size_t)k);
      buf_len += (size_t)k;
      buf_len = rolltui_termreplies_take(buf, buf_len, (asked & ROLLTUI_TERMQ_WIDTH) != 0, &rep);
    }
    /* What is left is input, not a reply: decode it for the next poll(). */
    if (buf_len) {
      QueueCtx qc;
      qc.t = t;
      rolltui_key_decoder_feed(t->decoder, buf, buf_len, queue_from_decoder, &qc);
    }
    rolltui_mem_free(buf);
    if (forced_protocol < 0) {
      if (rep.key_last == 2) t->protocol = ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS; /* CSI > 4 ; value m */
      else if (rep.key_last == 1) t->protocol = ROLLTUI_PROTOCOL_KITTY;        /* CSI ? flags u */
    }
    t->facts.responsive = rep.da1 ? 1 : 0;
    rolltui_termfacts_apply_replies(&t->facts, &rep, asked);
  }
  t->facts.keyboard = t->protocol;
  if (rep_out) *rep_out = rep;
  /* Turn on what was found, and make sure every exit path turns it back off. The pop is
   * APPENDED to leave_seq rather than prepended so the bytes already copied into the
   * signal handler's fixed buffer keep their offsets: a fatal signal landing between the
   * memcpy and the length store then still writes a complete, valid, shorter sequence.
   * Only ever onto a real terminal: with forced_protocol set there may be no tty at all,
   * and writing mode bytes down a pipe would land them in somebody's captured frame.
   * Enabled once per entry, and popped once for the life of the handle: a second exchange
   * (a reprobe) must not push a second flag the leave sequence never pops. */
  if (t->tty && !t->protocol_on && t->protocol == ROLLTUI_PROTOCOL_KITTY) {
    rolltui_terminal_write(t, "\x1b[>1u", 5); /* push the disambiguate flag */
    if (!(t->pops & 1)) seq_append(t->leave_seq, &t->leave_len, "\x1b[<1u"); /* pop it */
    t->pops |= 1;
    t->protocol_on = 1;
  } else if (t->tty && !t->protocol_on && t->protocol == ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS) {
    /* Mode 1, not 2: "encode only keys with modifiers that produce non-standard results",
     * which is exactly what encode_key models. Mode 2 also escapes keys that would produce
     * a printable character, and its shift handling is the part xterm's own documentation
     * declines to pin down. */
    rolltui_terminal_write(t, "\x1b[>4;1m", 7);
    if (!(t->pops & 2)) seq_append(t->leave_seq, &t->leave_len, "\x1b[>4m"); /* no value: back to initial state */
    t->pops |= 2;
    t->protocol_on = 1;
  }
  if (t->entered) publish_leave(t);
  rolltui_key_set_active_protocol(t->protocol);
  return t->protocol;
}

/* THE FIRST EXCHANGE OF AN ENTRY, by plan: everything when nothing is remembered, nothing when
 * everything is (the keyboard protocol is remembered too, and re-checking the background is
 * deferred to the first poll), and the keyboard alone on a resume or when a host opted out. */
static void run_negotiation(RolltuiTerminal* t) {
  RolltuiTermReplies rep;
  memset(&rep, 0, sizeof rep);
  if (t->plan == PLAN_FULL) {
    unsigned want = ROLLTUI_TERMQ_ALL;
    /* Where the depth is already decided by what the terminal is KNOWN not to do, its colour-depth and
     * version questions have nothing to add and only a chance to be mishandled. */
    if (rolltui_termenv_no_24bit_known(&t->env)) want &= ~(unsigned)(ROLLTUI_TERMQ_SGR | ROLLTUI_TERMQ_VERSION);
    negotiate_batch(t, t->timeout_ms, -1, want, &rep);
    remember(t, &rep);
  } else if (t->plan == PLAN_CACHED) {
    negotiate_batch(t, t->timeout_ms, t->entry.keyboard, 0, &rep);
    adopt_cache(t);
  } else {
    negotiate_batch(t, t->timeout_ms, -1, 0, &rep);
  }
  t->plan = PLAN_KEYBOARD;
  publish_depth(t);
}

unsigned char rolltui_terminal_negotiate_keyboard(RolltuiTerminal* t, int timeout_ms, int forced_protocol) {
  return negotiate_batch(t, timeout_ms, forced_protocol, 0, NULL);
}

void rolltui_terminal_facts(RolltuiTerminal* t, RolltuiTermFacts* out) {
  *out = t->facts;
  if (t->depth_override >= 0) {
    out->depth = (unsigned char)t->depth_override;
    out->depth_source = ROLLTUI_FACT_FORCED;
  }
  t->facts_seen = 1;
  t->reported_mode = t->facts.mode;
}

void rolltui_terminal_set_depth(RolltuiTerminal* t, int depth) {
  t->depth_override = (depth >= 0 && depth < ROLLTUI_DEPTH_COUNT) ? depth : -1;
  publish_depth(t);
}

void rolltui_terminal_present(RolltuiTerminal* t, RolltuiSwap* s, RolltuiStr* scratch) {
  const unsigned char depth = (unsigned char)(t->depth_override >= 0 ? t->depth_override : t->facts.depth);
  rolltui_str_clear(scratch);
  rolltui_swap_present(s, depth, scratch);
  if (scratch->n) rolltui_terminal_write(t, scratch->p, scratch->n);
}

int rolltui_terminal_reprobe(RolltuiTerminal* t, int timeout_ms) {
  RolltuiTermReplies rep;
  memset(&rep, 0, sizeof rep);
  if (!t->tty) return 0;
  if (t->cache_path.n) rolltui_termcache_forget(t->cache_path.p, t->fingerprint);
  /* The keyboard is not asked again: it was settled at entry and enabling it twice would push a
   * flag nothing pops. Everything else is. */
  {
    unsigned want = ROLLTUI_TERMQ_ALL & ~(unsigned)ROLLTUI_TERMQ_KEYBOARD;
    if (rolltui_termenv_no_24bit_known(&t->env)) want &= ~(unsigned)(ROLLTUI_TERMQ_SGR | ROLLTUI_TERMQ_VERSION);
    negotiate_batch(t, timeout_ms, t->protocol, want, &rep);
  }
  t->facts.remembered = 0;
  remember(t, &rep);
  publish_depth(t);
  return t->facts.responsive;
}

const char* rolltui_terminal_cache_path(const RolltuiTerminal* t) { return t->cache_path.n ? t->cache_path.p : ""; }

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
  unsigned long long deadline;
  if (!t->tty || !out) return 0;
  /* DECSC, home, one ambiguous glyph (U+2588), CPR, erase the line, DECRC. */
  rolltui_terminal_write(t, "\x1b" "7\x1b[1;1H\xE2\x96\x88\x1b[6n\x1b[1;1H\x1b[K\x1b" "8", 20);
  deadline = deadline_from(timeout_ms);
  for (;;) {
    long left = ms_until(&deadline);
    long k;
    char b[512];
    size_t at, end, i;
    if (left <= 0) break;
    k = read_until(t, left, b, sizeof b);
    if (k == 0) continue;
    if (k < 0) break;
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

int rolltui_terminal_query_background(RolltuiTerminal* t, int timeout_ms, RolltuiStyleColorRaw* out) {
  if (!t->tty) return 0;
  rolltui_terminal_write(t, "\x1b]11;?\x1b\\", 8); /* ESC ] 1 1 ; ? ESC \ */
  char* buf = NULL;
  size_t buf_len = 0, buf_cap = 0;
  int found = 0;
  unsigned long long deadline = deadline_from(timeout_ms);
  for (;;) {
    long left = ms_until(&deadline);
    if (left <= 0) break;
    char b[512];
    long k = read_until(t, left, b, sizeof b);
    if (k == 0) continue;
    if (k < 0) break;
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

static void poll_inner(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx) {
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
  const int n = rolltui_os_tty_wait(t->os, timeout_ms, 1);
  if (n < 0) return; /* interrupted or failed: whatever was queued is already reported above */
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
  {
    /* Every resize is reported, even when the size reads back the same: the frame may still need
     * a full repaint (some terminals clear on a font change). A plain wake reports nothing. */
    if (n & ROLLTUI_OS_TTY_RESIZED) {
      rolltui_terminal_refresh_size(t);
      RolltuiTermEvent ev;
      memset(&ev, 0, sizeof ev);
      ev.kind = ROLLTUI_TERM_EVENT_RESIZE;
      ev.w = t->w;
      ev.h = t->h;
      emit(ctx, &ev);
    }
  }
  if (n & ROLLTUI_OS_TTY_INPUT) {
    char buf[4096];
    const long k = rolltui_os_tty_read(t->os, buf, sizeof buf);
    if (k > 0) {
      EmitCtx ec;
      ec.fn = emit;
      ec.ctx = ctx;
      ec.t = t;
      rolltui_key_decoder_feed(t->decoder, buf, (size_t)k, emit_from_decoder, &ec);
      /* A lone ESC (or a sequence cut by the read boundary) waits for the next read, but
       * not forever: give it 30 ms and then resolve it. */
      if (rolltui_key_decoder_pending(t->decoder)) {
        if (rolltui_os_tty_wait(t->os, 30, 0) & ROLLTUI_OS_TTY_INPUT) {
          const long k2 = rolltui_os_tty_read(t->os, buf, sizeof buf);
          if (k2 > 0) rolltui_key_decoder_feed(t->decoder, buf, (size_t)k2, emit_from_decoder, &ec);
        }
        if (rolltui_key_decoder_pending(t->decoder)) rolltui_key_decoder_flush(t->decoder, emit_from_decoder, &ec);
      }
    }
  }
}

void rolltui_terminal_poll(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx) {
  /* A REMEMBERED BACKGROUND IS RE-CHECKED HERE, at the first poll — which a host reaches after its
   * first frame is on the screen, so the question costs the person nothing they can see. The
   * answer arrives as an OSC reply the decoder hands to `on_reply`; if it says the background is
   * not what was remembered, a FACTS event is owed below. */
  if (t->revalidate) {
    t->revalidate = 0;
    if (t->tty) rolltui_terminal_write(t, "\x1b]11;?\x1b\\", 8);
  }
  poll_inner(t, timeout_ms, emit, ctx);
  if (t->facts_dirty) {
    RolltuiTermEvent ev;
    t->facts_dirty = 0;
    memset(&ev, 0, sizeof ev);
    ev.kind = ROLLTUI_TERM_EVENT_FACTS;
    emit(ctx, &ev);
  }
}
