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
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_image.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_render.h" /* an inline region: drawing from a row, committing lines */
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
  int shaped;       /* a frame set the cursor's shape: leaving puts the terminal's own back */
  int tty;
  int entered;
  int w, h;
  unsigned char m_alt, m_mouse, m_hover, m_paste, m_hide; /* the modes the enter and leave sequences are built from */
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
  unsigned char away;          /* the window lost the person's focus, by the last focus report: 1 away, 0 focused */
  unsigned char resize_owed;   /* entered again after a stop: the next poll reports the size, as after a resize */
  unsigned char facts_events;  /* the host asked to be told of a change (RolltuiTerminalOptions::facts_events) */
  /* ---- an INLINE region (RolltuiTerminalOptions::inline_rows): drawn below the prompt, in the
   * normal screen, its rows found from where the cursor was ---- */
  int inl;
  int top;           /* the region's first row on screen, 0-based */
  int shown;         /* how many rows of it are on screen */
  int park_x, park_y; /* where the cursor was left after the last frame, region-relative */
  int dirty;         /* the region is erased and drawn whole at the next present */
  int sync_open;     /* a commit opened a synchronized frame the next present closes */
  int* extent;       /* each row's drawn width: its last cell that is not a default blank, plus one */
  size_t extent_cap;
  /* ---- pictures (rolltui_frame_image) ---- */
  /* Sent to the terminal, the placement it last had, and the present that last drew it. At most KITTY_KEEP are kept: the oldest is
   * deleted from the terminal, so its store stays small and it never evicts a picture this list still counts as sent. */
  struct KittyImage { unsigned int id; int cols, rows; unsigned long long drawn; } * kitty;
  /* THE TEMP FILES kitty was sent pixels in, the last eight (as many pictures as it keeps): kitty deletes each as it reads it;
   * what is left (a terminal that took the name and not the protocol) goes when the ninth comes, and all of it on leaving */
  RolltuiStr kitty_files[8];
  size_t kitty_file_at;
  size_t kitty_n, kitty_cap;
  unsigned long long presents;
  struct ShownImage { unsigned int id; RolltuiRect r; } * pics;   /* drawn outside the grid, and still there */
  size_t pics_n, pics_cap;
  /* AN OUTSIDE PROGRAM'S PICTURES (`rolltui_frame_outside_picture`): those the host was told to draw and has not been told to clear,
   * each with its cells in the frame and on the screen; those to be told to draw once this present's bytes are written; the host's
   * function */
  struct OutsidePicture { unsigned int id; RolltuiRect r, screen; } * outs, *outs_due;
  size_t outs_n, outs_cap, due_n, due_cap;
  RolltuiOutsidePictureFn outside_fn;
  void* outside_user;
  /* PICTURES HELD before they are sent (`picture_delay_ms`): each one's id and cells, whether it is an outside program's, since
   * when, and the present that last saw it */
  struct HeldPicture {
    unsigned int id;
    RolltuiRect r;
    unsigned char outside;
    unsigned long long since, seen;
  } * held;
  size_t held_n, held_cap;
  int hold_ms;
  /* A CELL'S PIXELS: as the host set them (0 not set), and as the terminal reported them at the start (CSI 16 t), which is trusted
   * over the window's size for the session when the two disagreed */
  int cell_set_w, cell_set_h;
  int cell_report_w, cell_report_h;
  unsigned char cell_report_trusted;
  /* ---- the window's title (rolltui_terminal_set_title) ---- */
  RolltuiStr title;            /* what the app set; empty: the terminal's own */
  unsigned char title_pushed;  /* the terminal's own title is on its stack, to be put back on leaving */
  /* ---- the window's name in tmux (rolltui_terminal_set_window_name) ---- */
  RolltuiStr wname;            /* what the app set */
  RolltuiStr wname_was;        /* the window's name before, put back on leaving */
  unsigned char wname_set;     /* the window carries the app's name now */
  unsigned char wname_auto;    /* `automatic-rename` was on, and is put back */
  unsigned char wname_known;   /* what the window was is known */
  unsigned char wname_how;     /* how it was last set: 1 the escape, 2 the command */
  RolltuiStr last_copy;        /* what was last copied through this terminal: the clipboard read when no tool can */
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

/* AN INLINE REGION'S MOUSE: the terminal reports screen rows, and the region starts at `top`. A
 * press outside the region is on the person's history, not on this program, and is dropped. */
static int inline_mouse(const RolltuiTerminal* t, RolltuiTermEvent* ev) {
  int y;
  if (!t->inl || ev->kind != ROLLTUI_EVENT_MOUSE) return 1;
  y = ev->mouse.y - t->top;
  if (y < 0 || y >= t->shown) return 0;
  ev->mouse.y = y;
  return 1;
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
  ev.focused = e->focused;
  if (ec->t && !inline_mouse(ec->t, &ev)) return; /* outside an inline region: not this program's */
  if (ec->t && e->kind == ROLLTUI_EVENT_FOCUS) {
    /* A REPORT OF WHAT IS ALREADY SO IS NOT AN EVENT: a terminal answers mode 1004 with the focus it has, and repeats it. */
    if ((e->focused == 0) == (ec->t->away != 0)) return;
    ec->t->away = e->focused ? 0 : 1;
    if (ec->t->publishing) rolltui_termfacts_set_active_focused(e->focused);
  }
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
static void refresh_cell_pixels(RolltuiTerminal* t);
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
  if (t->title.n) { /* back from a program that had the terminal: the title it set goes, this app's comes back */
    RolltuiStr again = t->title;
    memset(&t->title, 0, sizeof t->title);
    rolltui_terminal_set_title(t, again.p, again.n);
    rolltui_str_free(&again);
  }
  if (t->wname.n && !t->wname_set) { /* and the window's name */
    RolltuiStr again = t->wname;
    memset(&t->wname, 0, sizeof t->wname);
    rolltui_terminal_set_window_name(t, again.p, again.n);
    rolltui_str_free(&again);
  }
}

/* SYNCHRONIZED OUTPUT: a terminal that has mode 2026 is sent each frame between its begin and end,
 * in the same write, and draws it whole. One that does not is never sent the mode. */
#define SYNC_BEGIN "\x1b[?2026h"
#define SYNC_END "\x1b[?2026l"

static void window_name_restore(RolltuiTerminal* t);
static void outside_clear_all(RolltuiTerminal* t);
static void term_leave(RolltuiTerminal* t) {
  if (!t->entered) return;
  outside_clear_all(t); /* an outside program's picture goes with the screen: the host clears it while that screen is still up */
  t->entered = 0;
  if (t->inl && t->tty) { /* the region goes; the cursor waits where it began, for the prompt */
    char b[32];
    const int n = snprintf(b, sizeof b, "\x1b[%d;1H\x1b[J", t->top + 1);
    if (n > 0) rolltui_terminal_write(t, b, (size_t)n);
    t->shown = 0;
    t->dirty = 1;
  }
  rolltui_terminal_write(t, t->leave_seq, t->leave_len);
  {
    size_t f;
    for (f = 0; f < 8; ++f) /* the temp files kitty did not take */
      if (t->kitty_files[f].n) {
        remove(t->kitty_files[f].p);
        rolltui_str_clear(&t->kitty_files[f]);
      }
  }
  if (t->kitty_n) { /* the pictures sent go with this program: a terminal keeps them until told */
    RolltuiStr del;
    size_t i;
    memset(&del, 0, sizeof del);
    for (i = 0; i < t->kitty_n; ++i) rolltui_image_kitty_delete(t->kitty[i].id, t->multiplexed, &del);
    rolltui_terminal_write(t, del.p, del.n);
    rolltui_str_free(&del);
    t->kitty_n = 0;
  }
  t->pics_n = 0; /* a picture outside the grid does not outlive the screen it was drawn on */
  if (t->title_pushed) { /* an empty title, then the terminal's own popped back: where popping is not known, it is at least cleared */
    rolltui_terminal_write(t, "\x1b]2;\x1b\\\x1b[23;0t", sizeof "\x1b]2;\x1b\\\x1b[23;0t" - 1);
    t->title_pushed = 0;
  }
  window_name_restore(t);
  if (t->sync_open) { /* a commit's frame, with no present after it: the leaving is its end */
    rolltui_terminal_write(t, SYNC_END, sizeof SYNC_END - 1);
    t->sync_open = 0;
  }
  if (t->have_tio) rolltui_os_tty_restore(t->os);
  t->protocol_on = 0; /* the pop just went down: a resume enables it again */
  rolltui_os_tty_withdraw();
}

/* ---- the modes entering turns on and leaving turns off ------------------------------------- */

/* Built in the SAME final byte order the C++ `insert(0, ...)` chain produced: enter_seq appends in option order; leave_seq is
 * attributes-off first, then each mode's "off" in the REVERSE of enter_seq's order — undo last-enabled-first, with attributes-off
 * always leading regardless. Built again when a mode changes at run time, so a suspend and resume keep it. */
static void build_modes(RolltuiTerminal* t) {
  t->enter_len = 0;
  t->leave_len = 0;
  t->enter_seq[0] = 0;
  t->leave_seq[0] = 0;
  if (t->m_alt) seq_append(t->enter_seq, &t->enter_len, "\x1b[?1049h");
  if (t->m_mouse) seq_append(t->enter_seq, &t->enter_len, "\x1b[?1000h\x1b[?1002h\x1b[?1006h");
  if (t->m_mouse && t->m_hover) seq_append(t->enter_seq, &t->enter_len, "\x1b[?1003h");
  if (t->m_paste) seq_append(t->enter_seq, &t->enter_len, "\x1b[?2004h");
  if (t->m_hide) seq_append(t->enter_seq, &t->enter_len, "\x1b[?25l");
  /* FOCUS and LIGHT-OR-DARK, reported as they change (modes 1004 and 2031). A terminal that has neither ignores the modes. */
  seq_append(t->enter_seq, &t->enter_len, "\x1b[?1004h\x1b[?2031h");
  seq_append(t->leave_seq, &t->leave_len, "\x1b[0m"); /* attributes off before anything else */
  seq_append(t->leave_seq, &t->leave_len, "\x1b[?2031l\x1b[?1004l");
  if (t->m_hide) seq_append(t->leave_seq, &t->leave_len, "\x1b[?25h");
  if (t->m_paste) seq_append(t->leave_seq, &t->leave_len, "\x1b[?2004l");
  if (t->m_mouse && t->m_hover) seq_append(t->leave_seq, &t->leave_len, "\x1b[?1003l");
  if (t->m_mouse) seq_append(t->leave_seq, &t->leave_len, "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
  if (t->m_alt) seq_append(t->leave_seq, &t->leave_len, "\x1b[?1049l");
}

void rolltui_terminal_set_mouse(RolltuiTerminal* t, int mouse, int hover) {
  if (!t) return;
  mouse = mouse ? 1 : 0;
  hover = hover ? 1 : 0;
  if (mouse == t->m_mouse && hover == t->m_hover) return;
  if (t->tty && t->entered) { /* off what was on, then on what now is */
    if (t->m_mouse && t->m_hover && !(mouse && hover)) rolltui_os_tty_write(t->os, "\x1b[?1003l", sizeof "\x1b[?1003l" - 1);
    if (t->m_mouse && !mouse) rolltui_os_tty_write(t->os, "\x1b[?1006l\x1b[?1002l\x1b[?1000l", sizeof "\x1b[?1006l\x1b[?1002l\x1b[?1000l" - 1);
    if (mouse && !t->m_mouse) rolltui_os_tty_write(t->os, "\x1b[?1000h\x1b[?1002h\x1b[?1006h", sizeof "\x1b[?1000h\x1b[?1002h\x1b[?1006h" - 1);
    if (mouse && hover && !(t->m_mouse && t->m_hover)) rolltui_os_tty_write(t->os, "\x1b[?1003h", sizeof "\x1b[?1003h" - 1);
  }
  t->m_mouse = (unsigned char)mouse;
  t->m_hover = (unsigned char)hover;
  build_modes(t);
}

int rolltui_terminal_mouse(const RolltuiTerminal* t) { return t && t->m_mouse; }

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
  if (opts.inline_rows > 0) { /* inline: the normal screen, never the alternate one */
    t->inl = 1;
    t->dirty = 1;
    opts.alt_screen = 0;
  }
  t->m_alt = opts.alt_screen ? 1 : 0;
  t->m_mouse = opts.mouse ? 1 : 0;
  t->m_hover = opts.hover ? 1 : 0;
  t->m_paste = opts.bracketed_paste ? 1 : 0;
  t->m_hide = opts.hide_cursor ? 1 : 0;
  build_modes(t);
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
    rolltui_termfacts_set_active_split(-1);
    rolltui_termfacts_set_active_erase(-1);
    rolltui_termfacts_set_active_mode(-1);
    rolltui_termfacts_set_active_images(0, 0, 0);
    rolltui_termfacts_set_active_focused(1);
  }
  rolltui_os_tty_close(t->os); /* after leave: the restore bytes went down it */
  rolltui_key_decoder_free(t->decoder);
  rolltui_str_free(&t->cache_path);
  {
    size_t f;
    for (f = 0; f < 8; ++f) rolltui_str_free(&t->kitty_files[f]);
  }
  rolltui_mem_free(t->held);
  rolltui_str_free(&t->title);
  rolltui_str_free(&t->wname);
  rolltui_str_free(&t->wname_was);
  rolltui_str_free(&t->last_copy);
  rolltui_mem_free(t->kitty);
  rolltui_mem_free(t->pics);
  rolltui_mem_free(t->outs);
  rolltui_mem_free(t->outs_due);
  rolltui_mem_free(t->queued);
  rolltui_mem_free(t->queued_text);
  rolltui_mem_free(t->extent);
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
  refresh_cell_pixels(t);
  if (t->publishing) rolltui_termfacts_set_active_images(t->facts.images, t->facts.cell_w, t->facts.cell_h);
}

void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len) { rolltui_os_tty_write(t->os, bytes, len); }

void rolltui_terminal_wake(RolltuiTerminal* t) { rolltui_os_tty_wake(t->os); }

void rolltui_terminal_flush_input(RolltuiTerminal* t) {
  if (!t) return;
  rolltui_os_tty_flush_input(t->os);
  rolltui_key_decoder_reset(t->decoder); /* half a sequence already read goes too */
}

/* ---- the title and the working directory ---------------------------------------------------- */

/* `tmux <args>`, its answer in `out` (may be NULL) without its last newline: 1 when it ran and said 0, within a second. */
static int tmux_says(const char* const* args, RolltuiStr* out) {
  const char* argv[12];
  RolltuiChildOptions o;
  RolltuiChild* c;
  size_t i = 0;
  int st = -1, waited = 0;
  argv[i++] = "tmux";
  while (args[i - 1] && i < 11) argv[i] = args[i - 1], ++i;
  argv[i] = NULL;
  memset(&o, 0, sizeof o);
  o.argv = argv;
  o.mode = ROLLTUI_CHILD_PIPES;
  c = rolltui_child_spawn(&o, NULL);
  if (!c) return 0;
  rolltui_child_close_input(c);
  if (out) rolltui_str_clear(out);
  for (;;) {
    char buf[256];
    const size_t n = rolltui_child_read(c, buf, sizeof buf);
    if (n && out) rolltui_str_append(out, buf, n);
    if (!n && rolltui_child_drained(c) && rolltui_child_done(c, &st)) break;
    if (!n) {
      if (waited >= 1000) break; /* a tmux that does not answer is not waited on */
      rolltui_os_sleep_ms(2);
      waited += 2;
    }
  }
  rolltui_child_free(c);
  if (out)
    while (out->n && (out->p[out->n - 1] == '\n' || out->p[out->n - 1] == '\r')) out->p[--out->n] = 0;
  return st == 0;
}

static int in_tmux(void) {
  const char* v = getenv("TMUX");
  return v && *v;
}

/* THE WINDOW AS IT WAS before this app named it: its name and whether tmux names it by itself. */
static void window_name_learn(RolltuiTerminal* t) {
  const char* pane = getenv("TMUX_PANE");
  RolltuiStr v;
  if (t->wname_known || !in_tmux()) return;
  memset(&v, 0, sizeof v);
  {
    const char* a[] = {"display-message", "-p", "-t", pane ? pane : "", "#W", NULL};
    if (!pane) a[2] = "#W", a[3] = NULL;
    if (tmux_says(a, &v)) rolltui_str_set(&t->wname_was, v.p ? v.p : "", v.n);
  }
  {
    const char* a[] = {"show-window-options", "-v", "-t", pane ? pane : "", "automatic-rename", NULL};
    if (!pane) a[2] = "automatic-rename", a[3] = NULL;
    t->wname_auto = (unsigned char)(tmux_says(a, &v) && v.n == 2 && memcmp(v.p, "on", 2) == 0);
  }
  rolltui_str_free(&v);
  t->wname_known = 1;
}

/* WHETHER TMUX TAKES THE ESCAPE: its `allow-rename`, the window's own or else the global one. */
static int tmux_allows_rename(void) {
  const char* pane = getenv("TMUX_PANE");
  RolltuiStr v;
  int on = 0;
  memset(&v, 0, sizeof v);
  {
    const char* a[] = {"show-window-options", "-v", "-t", pane ? pane : "", "allow-rename", NULL};
    if (!pane) a[2] = "allow-rename", a[3] = NULL;
    if (!tmux_says(a, &v) || v.n == 0) {
      const char* g[] = {"show-window-options", "-gv", "allow-rename", NULL};
      tmux_says(g, &v);
    }
  }
  on = v.n == 2 && memcmp(v.p, "on", 2) == 0;
  rolltui_str_free(&v);
  return on;
}

static void window_name_restore(RolltuiTerminal* t) {
  const char* pane = getenv("TMUX_PANE");
  if (!t->wname_set || !in_tmux() || !t->wname_known) {
    t->wname_set = 0;
    return;
  }
  {
    const char* a[] = {"rename-window", "-t", pane ? pane : "", t->wname_was.p ? t->wname_was.p : "", NULL};
    if (!pane) a[1] = t->wname_was.p ? t->wname_was.p : "", a[2] = NULL;
    tmux_says(a, NULL);
  }
  if (t->wname_auto) { /* a rename turns it off; it was on */
    const char* a[] = {"set-window-option", "-t", pane ? pane : "", "automatic-rename", "on", NULL};
    if (!pane) a[1] = "automatic-rename", a[2] = "on", a[3] = NULL;
    tmux_says(a, NULL);
  }
  t->wname_set = 0;
}

int rolltui_terminal_set_window_name(RolltuiTerminal* t, const char* name, size_t len) {
  const char* sty = getenv("STY");
  const int tmux = in_tmux();
  RolltuiStr clean;
  size_t i;
  int how = 0;
  if (!t || (!tmux && !(sty && *sty))) return 0;
  if (!name) len = 0;
  if (t->wname_set && rolltui_str_eq(&t->wname, name, len)) return t->wname_how; /* only what changed is set */
  rolltui_str_set(&t->wname, name, len);
  if (!t->tty || !t->entered) return 0; /* set when the terminal is entered */
  memset(&clean, 0, sizeof clean);
  for (i = 0; i < len; ++i) { /* a control character would end the escape early */
    const unsigned char c = (unsigned char)name[i];
    if (c >= 0x20 && c != 0x7F) rolltui_str_append(&clean, &name[i], 1);
  }
  if (tmux) window_name_learn(t);
  if (!tmux || tmux_allows_rename()) { /* the escape tmux and screen take for a window's name */
    RolltuiStr seq;
    memset(&seq, 0, sizeof seq);
    rolltui_str_append(&seq, "\x1bk", 2);
    rolltui_str_append(&seq, clean.p ? clean.p : "", clean.n);
    rolltui_str_append(&seq, "\x1b\\", 2);
    rolltui_terminal_write(t, seq.p, seq.n);
    rolltui_str_free(&seq);
    how = 1;
  } else { /* `allow-rename` is off: the escape would be refused without a word, the command is not */
    const char* pane = getenv("TMUX_PANE");
    const char* a[] = {"rename-window", "-t", pane ? pane : "", clean.p ? clean.p : "", NULL};
    if (!pane) a[1] = clean.p ? clean.p : "", a[2] = NULL;
    how = tmux_says(a, NULL) ? 2 : 0;
  }
  t->wname_set = how != 0 && tmux;
  t->wname_how = (unsigned char)how;
  rolltui_str_free(&clean);
  return how;
}

void rolltui_terminal_remember_copy(RolltuiTerminal* t, const char* text, size_t len) {
  if (t) rolltui_str_set(&t->last_copy, text ? text : "", text ? len : 0);
}
int rolltui_terminal_last_copy(const RolltuiTerminal* t, RolltuiStr* out) {
  if (!t || !t->last_copy.n) return 0;
  rolltui_str_set(out, t->last_copy.p, t->last_copy.n);
  return 1;
}

void rolltui_terminal_set_title(RolltuiTerminal* t, const char* text, size_t len) {
  RolltuiStr seq;
  size_t i;
  if (!t) return;
  if (!text) len = 0;
  if (rolltui_str_eq(&t->title, text, len) && (t->title_pushed || len == 0)) return; /* only what changed is written */
  rolltui_str_set(&t->title, text, len);
  if (!t->tty || !t->entered) return; /* written when the terminal is entered */
  memset(&seq, 0, sizeof seq);
  if (!t->title_pushed) { /* the terminal's own title, kept on its stack until this app leaves */
    rolltui_str_append(&seq, "\x1b[22;0t", sizeof "\x1b[22;0t" - 1);
    t->title_pushed = 1;
  }
  rolltui_str_append(&seq, "\x1b]2;", sizeof "\x1b]2;" - 1);
  for (i = 0; i < len; ++i) { /* a control character in a title would end the sequence early: it goes */
    const unsigned char c = (unsigned char)text[i];
    if (c >= 0x20 && c != 0x7F) rolltui_str_append(&seq, &text[i], 1);
  }
  rolltui_str_append(&seq, "\x1b\\", sizeof "\x1b\\" - 1);
  rolltui_terminal_write(t, seq.p, seq.n);
  rolltui_str_free(&seq);
}

void rolltui_terminal_report_cwd(RolltuiTerminal* t, const char* path, size_t len) {
  static const char hex[] = "0123456789ABCDEF";
  RolltuiStr seq;
  char host[256];
  size_t i;
  if (!t || !t->tty || !path || len == 0) return;
  memset(&seq, 0, sizeof seq);
  if (rolltui_os_host_name(host, sizeof host) != 0) host[0] = 0;
  rolltui_str_append(&seq, "\x1b]7;file://", sizeof "\x1b]7;file://" - 1);
  rolltui_str_append(&seq, host, strlen(host));
  if (path[0] != '/') rolltui_str_append(&seq, "/", 1); /* C:/Users/... is /C:/Users/... in a URL */
  for (i = 0; i < len; ++i) {
    const unsigned char c = (unsigned char)path[i];
    if (c == '\\') {
      rolltui_str_append(&seq, "/", 1);
    } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
               c == '.' || c == '~' || c == ':') {
      rolltui_str_append(&seq, (const char*)&path[i], 1);
    } else {
      char pct[3] = {'%', hex[c >> 4], hex[c & 15]};
      rolltui_str_append(&seq, pct, 3);
    }
  }
  rolltui_str_append(&seq, "\x1b\\", sizeof "\x1b\\" - 1);
  rolltui_terminal_write(t, seq.p, seq.n);
  rolltui_str_free(&seq);
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
  rolltui_termfacts_set_active_split(t->facts.split_clusters);
  /* GNU screen, unless a tmux inside it is what this program talks to, fills an erase with the default background */
  rolltui_termfacts_set_active_erase(!(t->env.sty && *t->env.sty && !(t->env.tmux && *t->env.tmux)));
  rolltui_termfacts_set_active_mode(t->facts.mode);
  rolltui_termfacts_set_active_images(t->facts.images, t->facts.cell_w, t->facts.cell_h);
  if (!t->publishing) rolltui_termfacts_set_active_focused(1); /* entered: the person is typing into it */
  t->publishing = 1;
}

/* A cell's size in pixels, from the window's, where the terminal gives it. */
/* A CELL'S PIXELS: the host's word (`rolltui_terminal_set_cell_pixels`); else the window's size divided by its cells, read again at
 * every resize, unless the terminal's own report (CSI 16 t) disagreed with it at the start, when the report is taken for the session. */
static void refresh_cell_pixels(RolltuiTerminal* t) {
  int xp = 0, yp = 0;
  if (t->cell_set_w > 0 && t->cell_set_h > 0) {
    t->facts.cell_w = (unsigned short)t->cell_set_w;
    t->facts.cell_h = (unsigned short)t->cell_set_h;
    return;
  }
  if (t->cell_report_trusted) {
    t->facts.cell_w = (unsigned short)t->cell_report_w;
    t->facts.cell_h = (unsigned short)t->cell_report_h;
    return;
  }
  if (t->w > 0 && t->h > 0 && rolltui_os_tty_pixels(t->os, &xp, &yp)) {
    t->facts.cell_w = (unsigned short)(xp / t->w);
    t->facts.cell_h = (unsigned short)(yp / t->h);
  } else if (t->cell_report_w > 0) { /* the window says nothing: the report */
    t->facts.cell_w = (unsigned short)t->cell_report_w;
    t->facts.cell_h = (unsigned short)t->cell_report_h;
  }
}

/* THE CELL'S REPORT, from the first exchange: taken where the window's size gave none, and for the session where it disagreed with
 * the window's (a window size that is not the truth). */
static void take_cell_report(RolltuiTerminal* t, const RolltuiTermReplies* rep) {
  int xp = 0, yp = 0;
  if (rep->cell_px_w <= 0 || rep->cell_px_h <= 0) return;
  t->cell_report_w = rep->cell_px_w;
  t->cell_report_h = rep->cell_px_h;
  if (t->w > 0 && t->h > 0 && rolltui_os_tty_pixels(t->os, &xp, &yp) && xp > 0 && yp > 0) {
    const int iw = xp / t->w, ih = yp / t->h;
    t->cell_report_trusted = iw != rep->cell_px_w || ih != rep->cell_px_h;
  }
  refresh_cell_pixels(t);
  if (t->publishing) rolltui_termfacts_set_active_images(t->facts.images, t->facts.cell_w, t->facts.cell_h);
}

void rolltui_terminal_set_cell_pixels(RolltuiTerminal* t, int w, int h) {
  if (!t) return;
  t->cell_set_w = w > 0 && h > 0 ? w : 0;
  t->cell_set_h = w > 0 && h > 0 ? h : 0;
  refresh_cell_pixels(t);
  if (t->publishing) rolltui_termfacts_set_active_images(t->facts.images, t->facts.cell_w, t->facts.cell_h);
}

/* Everything the environment says, the remembered answers for THIS fingerprint if there are any
 * and they are fresh, and — from those — WHICH QUESTIONS the first exchange must ask. No byte goes
 * to the terminal here; `run_negotiation` does the asking, inside `term_enter`, so a resume does
 * not ask twice. */
static void init_facts(RolltuiTerminal* t, const RolltuiTerminalOptions* opts) {
  t->depth_override = -1;
  t->facts_events = opts->facts_events;
  t->hold_ms = opts->picture_delay_ms < 0 ? 0 : opts->picture_delay_ms == 0 ? 60 : opts->picture_delay_ms;
  t->plan = PLAN_KEYBOARD;
  t->timeout_ms = 80;
  rolltui_termenv_read(&t->env);
  rolltui_termfacts_from_env(&t->env, &t->facts);
  if (!t->env.force_console && !t->facts.console && rolltui_os_tty_is_console(t->os)) { /* the console under another TERM */
    t->facts.console = 1;
    if (t->facts.depth_source != ROLLTUI_FACT_FORCED && t->facts.depth > ROLLTUI_DEPTH_ANSI16) t->facts.depth = ROLLTUI_DEPTH_ANSI16;
  }
  if (opts->images >= ROLLTUI_IMAGES_BLOCKS && opts->images <= ROLLTUI_IMAGES_SIXEL) { /* the host's word, which is the person's */
    t->facts.images = opts->images;
    t->facts.images_source = ROLLTUI_FACT_FORCED;
  }
  t->multiplexed = rolltui_termenv_multiplexed(&t->env);
  if (t->env.tmux && t->tty) { /* BEHIND TMUX: the terminal outside it and whether escapes reach it, as tmux says */
    const char* pane = getenv("TMUX_PANE");
    const char* a[] = {"display-message", "-p", "-t", pane && *pane ? pane : "", ROLLTUI_TMUX_FACTS_FORMAT, NULL};
    const char* b[] = {"display-message", "-p", ROLLTUI_TMUX_FACTS_FORMAT, NULL};
    RolltuiStr said;
    memset(&said, 0, sizeof said);
    if (tmux_says(pane && *pane ? a : b, &said)) rolltui_termfacts_apply_tmux(&t->facts, said.p, said.n);
    rolltui_str_free(&said);
  }
  /* Over ssh a reply takes a network round trip, and 80 ms is a timeout that a working terminal
   * across a slow link would miss — after which the keyboard is judged Legacy and the answers
   * remembered as "silent". Wait for the wire when there is one. */
  if (t->env.ssh_connection) t->timeout_ms = 400;
  /* Inside tmux or screen every answer comes back through it, a hop more: the wait is longer, and the reply that ends it (DA1, which
   * every terminal and multiplexer answers) still ends it as soon as it comes. */
  else if (t->multiplexed) t->timeout_ms = 200;
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
  if (e->has_split) f->split_clusters = e->split_clusters;
  if (e->has_sync) f->sync_output = e->sync_output;
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
  if (e->has_images && f->images_source == ROLLTUI_FACT_DEFAULT) { /* how it draws pictures, as it said the first time */
    f->images = e->images;
    f->images_source = ROLLTUI_FACT_CACHE;
  }
  if (e->cell_w && e->cell_h) { /* its cell report: taken where the window says nothing, and over it where the window was wrong */
    t->cell_report_w = e->cell_w;
    t->cell_report_h = e->cell_h;
    t->cell_report_trusted = e->cell_fake;
    refresh_cell_pixels(t);
  }
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
  if (rep->cpr2 && rep->cpr2_col >= 2) {
    e.has_split = 1;
    e.split_clusters = t->facts.split_clusters;
  }
  if (rep->has_sync) {
    e.has_sync = 1;
    e.sync_output = t->facts.sync_output;
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
  if (t->facts.images_source == ROLLTUI_FACT_PROBE) { /* learned from it: its name, its Primary DA, kitty's query */
    e.has_images = 1;
    e.images = t->facts.images;
  }
  e.cell_fake = t->cell_report_trusted;
  e.cell_w = (unsigned short)t->cell_report_w;
  e.cell_h = (unsigned short)t->cell_report_h;
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
  /* LIGHT OR DARK, CHANGED while the program runs (mode 2031): `CSI ? 997 ; 1 n` dark, `; 2 n` light */
  if (len == 9 && memcmp(tmp, "\x1b[?997;", 7) == 0 && tmp[8] == 'n' && (tmp[7] == '1' || tmp[7] == '2')) {
    const unsigned char mode = tmp[7] == '1' ? ROLLTUI_MODE_DARK : ROLLTUI_MODE_LIGHT;
    t->facts.mode = mode;
    t->facts.mode_source = ROLLTUI_FACT_PROBE;
    if (t->publishing) rolltui_termfacts_set_active_mode(mode);
    if (t->facts_events && mode != t->reported_mode) t->facts_dirty = 1;
    /* …AND THE BACKGROUND ASKED AGAIN: its answer, when it comes, decides (`late_background`), as it did at the start */
    if (t->tty) {
      static const char kAskBackground[] = "\x1b]11;?\x1b\\";
      rolltui_terminal_write(t, kAskBackground, sizeof kAskBackground - 1);
    }
    return;
  }
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
    if (t->inl && (asked & ROLLTUI_TERMQ_WIDTH)) asked |= ROLLTUI_TERMQ_RELATIVE; /* row 1 is not the region's */
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
      buf_len = rolltui_termreplies_take(buf, buf_len, (asked & (ROLLTUI_TERMQ_WIDTH | ROLLTUI_TERMQ_ANCHOR)) != 0, &rep);
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
    /* push disambiguate (1) and alternate keys (4): each chord whole, and the key's place on a US layout, which matches a binding a
     * chord on another layout would not; every key as an escape (8) is not asked, so typed text stays text */
    rolltui_terminal_write(t, "\x1b[>5u", 5);
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
static void inline_anchor(RolltuiTerminal* t);

/* THE QUESTIONS THE FULL EXCHANGE ADDS: a cell's pixels; and, for a terminal whose name did not say how it draws pictures (and that no
 * multiplexer stands in front of), whether it speaks kitty's protocol. Their answers are remembered with the rest. */
static unsigned fresh_questions(const RolltuiTerminal* t) {
  unsigned q = ROLLTUI_TERMQ_PIXELS;
  if (t->facts.images_source == ROLLTUI_FACT_DEFAULT && !t->multiplexed) q |= ROLLTUI_TERMQ_GRAPHICS;
  return q;
}

static void run_negotiation(RolltuiTerminal* t) {
  RolltuiTermReplies rep;
  memset(&rep, 0, sizeof rep);
  if (t->inl && t->tty) inline_anchor(t); /* before anything is drawn: where the region starts */
  if (t->plan == PLAN_FULL) {
    unsigned want = ROLLTUI_TERMQ_ALL | fresh_questions(t);
    /* Where the depth is already decided by what the terminal is KNOWN not to do, its colour-depth and
     * version questions have nothing to add and only a chance to be mishandled. */
    if (rolltui_termenv_no_24bit_known(&t->env)) want &= ~(unsigned)(ROLLTUI_TERMQ_SGR | ROLLTUI_TERMQ_VERSION);
    negotiate_batch(t, t->timeout_ms, -1, want, &rep);
    take_cell_report(t, &rep);
    remember(t, &rep);
  } else if (t->plan == PLAN_CACHED) {
    negotiate_batch(t, t->timeout_ms, t->entry.keyboard, 0, &rep); /* a terminal remembered is asked nothing */
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

static unsigned char present_depth(const RolltuiTerminal* t) {
  return (unsigned char)(t->depth_override >= 0 ? t->depth_override : t->facts.depth);
}

/* What the renderer may send besides the depth: an underline's shapes and colour where the terminal draws them; the cursor's shape;
 * the console's own glyphs in place of those its font lacks. */
static unsigned char sgr_extras(const RolltuiTerminal* t) {
  if (t->facts.console) return ROLLTUI_RENDER_CONSOLE;
  return (unsigned char)((rolltui_termfacts_underline_shapes(&t->env, t->facts.name) ? ROLLTUI_SGR_EXTENDED : 0) | ROLLTUI_RENDER_CARET);
}

/* ---- the inline region ---------------------------------------------------------------------
 * Drawn in the normal screen from the row the cursor was on, `shown` rows tall: the height of the
 * frame presented. Its top is a screen row, known exactly because nothing but this program writes
 * to the terminal while it runs; it moves only when this program moves it (room made at the
 * bottom, rows committed above) and is asked again after a resize or a resume. */

static void put_row(RolltuiStr* out, int row, const char* then) {
  char b[48];
  const int n = snprintf(b, sizeof b, "\x1b[%d;1H%s", row + 1, then);
  if (n > 0) rolltui_str_append(out, b, (size_t)n);
}

/* Whether the terminal rewraps its lines when it narrows. Nearly every one does; xterm, st and the
 * Linux console cut them instead. */
static int rewraps(const RolltuiTerminal* t) {
  const char* term = t->env.term ? t->env.term : "";
  if (strncmp(term, "linux", 5) == 0 || strcmp(term, "st") == 0 || strncmp(term, "st-", 3) == 0) return 0;
  return strncmp(t->facts.name, "XTerm", 5) != 0;
}

/* WHERE THE REGION STARTS: the cursor's row, asked. A cursor that is not at the start of its line
 * (something printed without a newline) moves to the next one first, so nothing already there is
 * drawn over. A terminal that does not answer gets the bottom row. */
static void inline_anchor(RolltuiTerminal* t) {
  RolltuiTermReplies rep;
  int row, col;
  memset(&rep, 0, sizeof rep);
  rolltui_terminal_refresh_size(t);
  negotiate_batch(t, t->timeout_ms, t->protocol, ROLLTUI_TERMQ_ANCHOR, &rep);
  row = rep.cpr ? rep.cpr_row : t->h;
  col = rep.cpr ? rep.cpr_col : 1;
  if (col > 1) {
    rolltui_terminal_write(t, "\r\n", 2);
    if (row < t->h) ++row; /* on the bottom row the screen scrolled instead */
  }
  t->top = row - 1;
  t->shown = 0;
  t->dirty = 1;
  t->park_x = t->park_y = 0;
}

/* AFTER A RESIZE, where the region is now. With the cursor parked at the region's first cell the
 * answer is exact, whatever the terminal rewrapped. With it at a caret on row `park_y`, the rows
 * above it that were wider than the new width have each wrapped to more rows on a terminal that
 * rewraps, and so has the caret's own row before it. */
static void inline_reanchor(RolltuiTerminal* t, int old_w) {
  RolltuiTermReplies rep;
  int g = 0, y;
  memset(&rep, 0, sizeof rep);
  negotiate_batch(t, t->timeout_ms, t->protocol, ROLLTUI_TERMQ_ANCHOR, &rep);
  t->dirty = 1;
  if (!rep.cpr) return; /* no answer: the region stays where it was believed to be */
  if (t->w < old_w && t->w > 0 && rewraps(t)) {
    for (y = 0; y < t->park_y && y < t->shown; ++y)
      if (t->extent[y] > t->w) g += (t->extent[y] - 1) / t->w;
    g += t->park_x / t->w;
  }
  t->top = rep.cpr_row - 1 - t->park_y - g;
  if (t->top < 0) t->top = 0;
}

/* BEFORE A FRAME: the region placed. When its height changed, or something moved it, room is made
 * below it (at the bottom the screen scrolls, and what was above goes into the history), the old
 * region is erased, and the frame is drawn whole. */
static void inline_place(RolltuiTerminal* t, RolltuiSwap* s, RolltuiStr* out) {
  int h = rolltui_frame_height(rolltui_swap_pending(s));
  int k;
  if (h > t->h) h = t->h;
  if (h == t->shown && !t->dirty) return;
  if (t->top + h > t->h) {
    put_row(out, t->h - 1, "");
    for (k = t->top + h - t->h; k > 0; --k) rolltui_str_append(out, "\n", 1);
    t->top = t->h - h;
    if (t->top < 0) t->top = 0;
  }
  put_row(out, t->top, "\x1b[J");
  rolltui_swap_invalidate(s);
  t->shown = h;
  t->dirty = 0;
}

/* AFTER A FRAME: where the cursor is left, and each row's drawn width, for the next resize. A hidden
 * cursor waits at the region's first cell, the one place a resize cannot make ambiguous. */
static void inline_park(RolltuiTerminal* t, const RolltuiSwap* s, RolltuiStr* out) {
  const RolltuiFrame* f = rolltui_swap_front(s);
  int cx = 0, cy = 0, vis = 0, y;
  rolltui_frame_cursor(f, &cx, &cy, &vis);
  if (vis) {
    t->park_x = cx;
    t->park_y = cy;
  } else {
    if (cx != 0 || cy != 0) put_row(out, t->top, ""); /* the frame left it elsewhere */
    t->park_x = t->park_y = 0;
  }
  t->extent = (int*)rolltui_grow(t->extent, &t->extent_cap, (size_t)(t->shown > 0 ? t->shown : 1), sizeof *t->extent);
  for (y = 0; y < t->shown; ++y) t->extent[y] = rolltui_render_row_extent(f, y);
}

void rolltui_terminal_commit(RolltuiTerminal* t, const RolltuiFrame* lines) {
  RolltuiStr out;
  int rows;
  if (!t->inl || !t->tty || !lines || rolltui_frame_height(lines) <= 0) return;
  memset(&out, 0, sizeof out);
  if (t->facts.sync_output && !t->sync_open) { /* closed by the next present, which redraws the region */
    rolltui_str_append(&out, SYNC_BEGIN, sizeof SYNC_BEGIN - 1);
    t->sync_open = 1;
  }
  put_row(&out, t->top, "\x1b[J"); /* the lines take the region's place; it is drawn again below them */
  rows = rolltui_render_lines(lines, (unsigned char)(rolltui_termfacts_clamp_depth(present_depth(t)) | sgr_extras(t)), t->w, 1, &out);
  t->top += rows;
  if (t->top > t->h - 1) t->top = t->h - 1; /* past the bottom the screen scrolled */
  t->shown = 0;
  t->dirty = 1;
  rolltui_terminal_write(t, out.p, out.n);
  rolltui_str_free(&out);
}

/* ---- pictures at present -------------------------------------------------------------------- */

/* Every cell of an outside-the-grid picture is still its own blank: nothing has been drawn over it this frame. */
static int image_intact(const RolltuiFrame* f, const RolltuiFrameImage* p) {
  int x, y;
  for (y = p->cells.y; y < p->cells.y + p->cells.h; ++y)
    for (x = p->cells.x; x < p->cells.x + p->cells.w; ++x) {
      RolltuiCellRaw c;
      if (x < 0 || y < 0 || x >= rolltui_frame_width(f) || y >= rolltui_frame_height(f)) return 0;
      memset(&c, 0, sizeof c);
      rolltui_frame_cell(f, x, y, &c);
      if (c.len != 1 || c.bytes[0] != ' ' || memcmp(&c.style, &p->ground, sizeof c.style) != 0) return 0;
    }
  return 1;
}

static int rect_same(RolltuiRect a, RolltuiRect b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

/* BEFORE THE DIFF: a kitty picture's pixels the first time it is drawn and its placement whenever its cells change size, so the
 * placeholder cells the diff writes find it; and an outside-the-grid picture no longer there, or no longer whole, has its cells
 * forgotten, so the diff writes what they hold now over it. */
#define KITTY_KEEP 8

/* A PICTURE DUE: it has stayed in the same cells for the hold (or there is none). The first sight of it starts the clock. */
static int picture_due(RolltuiTerminal* t, unsigned int id, RolltuiRect r, int outside) {
  const unsigned long long now = rolltui_os_monotonic_ms();
  size_t k;
  if (t->hold_ms <= 0) return 1;
  for (k = 0; k < t->held_n; ++k)
    if (t->held[k].id == id && t->held[k].outside == outside && rect_same(t->held[k].r, r)) {
      if (now < t->held[k].since + (unsigned long long)t->hold_ms) {
        t->held[k].seen = t->presents;
        return 0;
      }
      t->held[k] = t->held[--t->held_n]; /* due, and sent now: held no longer */
      return 1;
    }
  t->held = (struct HeldPicture*)rolltui_grow(t->held, &t->held_cap, t->held_n + 1, sizeof *t->held);
  t->held[t->held_n].id = id;
  t->held[t->held_n].r = r;
  t->held[t->held_n].outside = (unsigned char)(outside != 0);
  t->held[t->held_n].since = now;
  t->held[t->held_n].seen = t->presents;
  ++t->held_n;
  return 0;
}
/* The held pictures this present did not draw: gone before they were due, and never sent. */
static void drop_unseen_held(RolltuiTerminal* t) {
  size_t k = 0;
  while (k < t->held_n) {
    if (t->held[k].seen != t->presents) t->held[k] = t->held[--t->held_n];
    else ++k;
  }
}
int rolltui_terminal_pending_ms(const RolltuiTerminal* t) {
  const unsigned long long now = rolltui_os_monotonic_ms();
  size_t k;
  int soonest = -1;
  if (!t) return -1;
  for (k = 0; k < t->held_n; ++k) {
    const unsigned long long due = t->held[k].since + (unsigned long long)t->hold_ms;
    const int ms = due > now ? (int)(due - now) : 1;
    if (soonest < 0 || ms < soonest) soonest = ms;
  }
  return soonest;
}

static void images_before(RolltuiTerminal* t, RolltuiSwap* s, const RolltuiFrame* next, RolltuiStr* out) {
  const size_t n = rolltui_frame_image_count(next);
  size_t i, k;
  ++t->presents;
  for (i = 0; i < n; ++i) {
    const RolltuiFrameImage* p = rolltui_frame_image_at(next, i);
    const unsigned int id = rolltui_image_id(p->img);
    if (p->protocol != ROLLTUI_IMAGES_KITTY) continue;
    for (k = 0; k < t->kitty_n && t->kitty[k].id != id; ++k) {
    }
    if (k == t->kitty_n && !picture_due(t, id, p->cells, 0)) continue; /* held: its cells say nothing until its pixels go */
    if (k == t->kitty_n) {
      t->kitty = (struct KittyImage*)rolltui_grow(t->kitty, &t->kitty_cap, t->kitty_n + 1, sizeof *t->kitty);
      t->kitty[k].id = id;
      t->kitty[k].cols = t->kitty[k].rows = -1;
      ++t->kitty_n;
      { /* by file where kitty is on this machine */
        RolltuiStr* slot = &t->kitty_files[t->kitty_file_at % 8];
        if (slot->n) remove(slot->p); /* the oldest: read long since, or never going to be */
        rolltui_str_clear(slot);
        rolltui_image_kitty_upload(p->img, t->multiplexed, !t->env.ssh_connection, out, slot);
        if (slot->n) ++t->kitty_file_at;
      }
    }
    t->kitty[k].drawn = t->presents;
    if (t->kitty[k].cols != p->cells.w || t->kitty[k].rows != p->cells.h) {
      rolltui_image_kitty_place(id, p->cells.w, p->cells.h, t->multiplexed, out);
      t->kitty[k].cols = p->cells.w;
      t->kitty[k].rows = p->cells.h;
    }
  }
  while (t->kitty_n > KITTY_KEEP) { /* the least recently drawn of those not drawn now goes from the terminal */
    size_t old = t->kitty_n;
    for (k = 0; k < t->kitty_n; ++k)
      if (t->kitty[k].drawn != t->presents && (old == t->kitty_n || t->kitty[k].drawn < t->kitty[old].drawn)) old = k;
    if (old == t->kitty_n) break; /* every one is on screen */
    rolltui_image_kitty_delete(t->kitty[old].id, t->multiplexed, out);
    t->kitty[old] = t->kitty[--t->kitty_n];
  }
  if (!rolltui_swap_front(s)) { /* drawn whole: nothing drawn outside the grid survives it */
    t->pics_n = 0;
    outside_clear_all(t);
    return;
  }
  for (k = 0; k < t->outs_n;) { /* an outside program's picture no longer where it was drawn: the host clears it, then its cells */
    int kept = 0;
    for (i = 0; i < n && !kept; ++i) {
      const RolltuiFrameImage* p = rolltui_frame_image_at(next, i);
      kept = p->outside == t->outs[k].id && rect_same(p->cells, t->outs[k].r) && image_intact(next, p);
    }
    if (kept) {
      ++k;
      continue;
    }
    {
      const struct OutsidePicture gone = t->outs[k];
      t->outs[k] = t->outs[--t->outs_n];
      rolltui_swap_forget_rect(s, gone.r);
      if (t->outside_fn) t->outside_fn(t->outside_user, ROLLTUI_OUTSIDE_CLEAR, gone.id, gone.screen);
    }
  }
  for (k = 0; k < t->pics_n;) {
    int kept = 0;
    for (i = 0; i < n && !kept; ++i) {
      const RolltuiFrameImage* p = rolltui_frame_image_at(next, i);
      kept = !p->outside && rolltui_image_id(p->img) == t->pics[k].id && rect_same(p->cells, t->pics[k].r) && image_intact(next, p);
    }
    if (kept) {
      ++k;
      continue;
    }
    rolltui_swap_forget_rect(s, t->pics[k].r);
    t->pics[k] = t->pics[--t->pics_n];
  }
}

/* AN OUTSIDE PROGRAM'S PICTURE whose cells are whole, that the host has not been told to draw where it is, and that has stayed
 * there for the hold: told once this present's bytes are written (`outside_tell_due`). */
static void outside_after(RolltuiTerminal* t, const RolltuiFrame* drawn, const RolltuiFrameImage* p, int origin) {
  struct OutsidePicture o;
  size_t k;
  if (!t->outside_fn || !image_intact(drawn, p)) return;
  for (k = 0; k < t->outs_n; ++k)
    if (t->outs[k].id == p->outside && rect_same(t->outs[k].r, p->cells)) return;
  if (!picture_due(t, p->outside, p->cells, 1)) return;
  o.id = p->outside;
  o.r = p->cells;
  o.screen = p->cells;
  o.screen.y += origin;
  t->outs = (struct OutsidePicture*)rolltui_grow(t->outs, &t->outs_cap, t->outs_n + 1, sizeof *t->outs);
  t->outs[t->outs_n++] = o;
  t->outs_due = (struct OutsidePicture*)rolltui_grow(t->outs_due, &t->due_cap, t->due_n + 1, sizeof *t->outs_due);
  t->outs_due[t->due_n++] = o;
}
/* The host told to draw, after the frame is on the screen; then the cursor, which the drawing moved, is put back as the frame left
 * it, with the colours reset. */
static void outside_tell_due(RolltuiTerminal* t, const RolltuiFrame* drawn, int origin) {
  size_t k;
  int cx = 0, cy = 0, vis = 0, len;
  char back[48];
  if (!t->due_n) return;
  for (k = 0; k < t->due_n; ++k)
    if (t->outside_fn) t->outside_fn(t->outside_user, ROLLTUI_OUTSIDE_DRAW, t->outs_due[k].id, t->outs_due[k].screen);
  t->due_n = 0;
  rolltui_frame_cursor(drawn, &cx, &cy, &vis);
  len = snprintf(back, sizeof back, "\x1b[0m\x1b[?25l\x1b[%d;%dH%s", origin + cy + 1, cx + 1, vis ? "\x1b[?25h" : "");
  if (len > 0) rolltui_terminal_write(t, back, (size_t)len);
}
/* Every outside program's picture drawn is told to clear: the screen is about to be written whole, or left. */
static void outside_clear_all(RolltuiTerminal* t) {
  while (t->outs_n) {
    const struct OutsidePicture gone = t->outs[--t->outs_n];
    if (t->outside_fn) t->outside_fn(t->outside_user, ROLLTUI_OUTSIDE_CLEAR, gone.id, gone.screen);
  }
  t->due_n = 0;
}
void rolltui_terminal_set_outside_pictures(RolltuiTerminal* t, RolltuiOutsidePictureFn fn, void* user) {
  if (!t) return;
  t->outside_fn = fn;
  t->outside_user = user;
  if (!fn) t->outs_n = t->due_n = 0;
}

/* AFTER THE DIFF: an outside-the-grid picture whose cells are whole and that is not on the screen already is drawn, the cursor kept
 * where the diff left it. */
static void images_after(RolltuiTerminal* t, const RolltuiFrame* drawn, int origin, RolltuiStr* out) {
  const size_t n = rolltui_frame_image_count(drawn);
  size_t i, k;
  for (i = 0; i < n; ++i) {
    const RolltuiFrameImage* p = rolltui_frame_image_at(drawn, i);
    const unsigned int id = rolltui_image_id(p->img);
    char at[32];
    int len;
    if (p->outside) {
      outside_after(t, drawn, p, origin);
      continue;
    }
    if (p->protocol != ROLLTUI_IMAGES_ITERM2 && p->protocol != ROLLTUI_IMAGES_SIXEL) continue;
    if (!image_intact(drawn, p)) continue; /* something is drawn over it: it waits until that goes */
    for (k = 0; k < t->pics_n && !(t->pics[k].id == id && rect_same(t->pics[k].r, p->cells)); ++k) {
    }
    if (k < t->pics_n) continue;
    if (!picture_due(t, id, p->cells, 0)) continue; /* held: sent once it has stayed */
    len = snprintf(at, sizeof at, "\x1b" "7\x1b[%d;%dH", origin + p->cells.y + 1, p->cells.x + 1);
    if (len > 0) rolltui_str_append(out, at, (size_t)len);
    if (p->protocol == ROLLTUI_IMAGES_ITERM2) rolltui_image_iterm2(p->img, p->cells.w, p->cells.h, t->multiplexed, out);
    else rolltui_image_sixel(p->img, p->px_w, p->px_h, out);
    rolltui_str_append(out, "\x1b" "8", 2);
    t->pics = (struct ShownImage*)rolltui_grow(t->pics, &t->pics_cap, t->pics_n + 1, sizeof *t->pics);
    t->pics[t->pics_n].id = id;
    t->pics[t->pics_n].r = p->cells;
    ++t->pics_n;
  }
}

static int resume_if_stopped(RolltuiTerminal* t);

void rolltui_terminal_present(RolltuiTerminal* t, RolltuiSwap* s, RolltuiStr* scratch) {
  const int resumed = resume_if_stopped(t); /* continued since the last poll: entered again first, and drawn whole */
  const unsigned char depth = present_depth(t);
  const int sync = t->tty && t->facts.sync_output;
  const int inl = t->inl && t->tty;
  const int opened = t->sync_open;
  const RolltuiFrame* next = rolltui_swap_pending(s);
  size_t lead;
  if (resumed) rolltui_swap_invalidate(s);
  rolltui_str_clear(scratch);
  if (sync && !opened) rolltui_str_append(scratch, SYNC_BEGIN, sizeof SYNC_BEGIN - 1);
  if (t->tty) images_before(t, s, next, scratch);
  if (inl) inline_place(t, s, scratch);
  if (next && !t->shaped && !t->facts.console && rolltui_frame_cursor_shape(next)) { /* the shape is put back when the program leaves */
    t->shaped = 1;
    seq_append(t->leave_seq, &t->leave_len, "\x1b[0 q");
    publish_leave(t);
  }
  lead = scratch->n;
  rolltui_swap_present_at(s, (unsigned char)(depth | sgr_extras(t)), inl ? t->top : 0, inl, scratch);
  if (t->tty) images_after(t, next, inl ? t->top : 0, scratch); /* `next` is the frame just drawn, now the baseline */
  if (t->tty) drop_unseen_held(t);
  if (inl && scratch->n > lead) inline_park(t, s, scratch);
  if (sync && !opened && scratch->n == sizeof SYNC_BEGIN - 1) rolltui_str_clear(scratch); /* nothing changed: nothing is sent */
  else if (sync) rolltui_str_append(scratch, SYNC_END, sizeof SYNC_END - 1);
  t->sync_open = 0;
  if (scratch->n) rolltui_terminal_write(t, scratch->p, scratch->n);
  if (t->tty) outside_tell_due(t, next, inl ? t->top : 0);
}

int rolltui_terminal_reprobe(RolltuiTerminal* t, int timeout_ms) {
  RolltuiTermReplies rep;
  memset(&rep, 0, sizeof rep);
  if (!t->tty) return 0;
  if (t->cache_path.n) rolltui_termcache_forget(t->cache_path.p, t->fingerprint);
  /* The keyboard is not asked again: it was settled at entry and enabling it twice would push a
   * flag nothing pops. Everything else is. */
  {
    unsigned want;
    if (t->facts.images_source == ROLLTUI_FACT_CACHE) t->facts.images_source = ROLLTUI_FACT_DEFAULT; /* asked again, not remembered */
    want = (ROLLTUI_TERMQ_ALL & ~(unsigned)ROLLTUI_TERMQ_KEYBOARD) | fresh_questions(t);
    if (rolltui_termenv_no_24bit_known(&t->env)) want &= ~(unsigned)(ROLLTUI_TERMQ_SGR | ROLLTUI_TERMQ_VERSION);
    negotiate_batch(t, timeout_ms, t->protocol, want, &rep);
  }
  t->facts.remembered = 0;
  take_cell_report(t, &rep);
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

/* BACK FROM A STOP SENT FROM OUTSIDE: the handler left the terminal as it was found; it is entered again whole, and the screen
 * repainted as after a resize. Asked by the poll and by the present, whichever comes first after the continue, so no frame is ever
 * written to the screen the shell has back. 1 when it entered again. */
static int resume_if_stopped(RolltuiTerminal* t) {
  if (!rolltui_os_tty_take_resumed() || !t->entered) return 0;
  t->entered = 0;
  t->protocol_on = 0; /* the handler's leaving popped the keyboard flags */
  term_enter(t, 0);
  rolltui_terminal_refresh_size(t);
  t->resize_owed = 1;
  return 1;
}

static void poll_inner(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx) {
  resume_if_stopped(t);
  if (t->resize_owed) {
    RolltuiTermEvent ev;
    t->resize_owed = 0;
    memset(&ev, 0, sizeof ev);
    ev.kind = ROLLTUI_TERM_EVENT_RESIZE;
    ev.w = t->w;
    ev.h = t->h;
    emit(ctx, &ev);
  }
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
      if (inline_mouse(t, &ev)) emit(ctx, &ev);
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
      const int old_w = t->w;
      rolltui_terminal_refresh_size(t);
      if (t->inl && t->tty) inline_reanchor(t, old_w);
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
