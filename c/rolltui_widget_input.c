/* rolltui/c/rolltui_widget_input.c — the C side of the input widget. See rolltui_widget_input.h; the
 * rules are rolltui/Input.hpp's. */
#include "rolltui/c/rolltui_widget_input.h"
#include "rolltui/c/rolltui_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_syntax.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/testkit/testctl.h"

/* How long an open "ordinary editing" group stays open with no further edit before the next
 * one is treated as a fresh group instead of a continuation (Input.hpp's UNDO). */
#define UNDO_GROUP_TIMEOUT_MS 700
#define UNDO_LIMIT 200

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static size_t zmin(size_t a, size_t b) { return a < b ? a : b; }

/* ---- the state --------------------------------------------------------------------------- */

/* One grapheme's place in the wrap. */
typedef struct FlowCell {
  int row, col, width;
} FlowCell;

/* A whole {text, caret, selection} the undo stack puts back. Cheap, and there is no inverse
 * edit to get wrong — only a value to restore (Input.hpp's UNDO). */
typedef struct Snapshot {
  RolltuiStr text;
  size_t caret;
  RolltuiInputSelectionRaw sel;
} Snapshot;

struct RolltuiInput {
  RolltuiStr text;
  RolltuiUnicodeGrapheme* g; /* one per grapheme of `text` */
  size_t g_len, g_cap;
  size_t caret;
  RolltuiInputSelectionRaw sel;
  int goal_col, has_goal;
  RolltuiInputOptionsRaw opt;
  int prompt_w;

  /* the undo stack: whole snapshots, and the C says out loud that each one owns a copy of
   * the entire text — which `std::vector<InputSnapshot>` did and never mentioned */
  Snapshot* undo;
  size_t undo_len, undo_cap, undo_at;
  int undo_pending;
  unsigned long long undo_last_ms, now_ms;

  RolltuiStr* hist;
  size_t hist_len, hist_cap, hist_pos;
  size_t hist_saved; /* entries [0, hist_saved) are the file's, as last loaded or saved */
  RolltuiStr draft;
  size_t draft_prefix; /* how much of the draft, from its start, an entry must begin with (`history_prefix`): the caret's offset */

  /* the kill buffer and what the last command did with it: a run of kills is one kill */
  RolltuiStr kill;
  int last_kill; /* 1 after a forward kill, -1 after a backward one, 0 after anything else */
  int overwrite;

  /* the highlighted runs, rebuilt when the text or the language changes */
  RolltuiSyntax* syntax; /* BORROWED */
  RolltuiHighlight* hl;
  int hl_lang, hl_dirty;
  RolltuiStr hl_name;

  /* the wrap flow, rebuilt when `dirty` */
  FlowCell* cells;
  size_t cells_cap;
  size_t* row_end;
  size_t row_end_len, row_end_cap;
  int end_row, end_col, rows;

  RolltuiRect area;
  int width, top, dirty;
  int indent;   /* the prompt and the gutter: where every row's text begins */
  int gutter_w; /* the line numbers' column, its trailing space included; 0 without them */
  int scroll_x; /* ONE-ROW mode: cells slid off the left so the caret is in view */

  struct {
    int active;
    size_t begin, end;
    int x, y;
  } drag;
  struct {
    unsigned long long at_ms;
    int x, y, count;
  } click;

  RolltuiCopyFn copy_fn;
  void* copy_ctx;

  RolltuiUnicodeScratch* u; /* WORKING MEMORY, one role: the grapheme walk and word ranges */
};

/* ---- options ----------------------------------------------------------------------------- */

void rolltui_input_options_init(RolltuiInputOptionsRaw* o) {
  memset(o, 0, sizeof *o);
  o->tab_width = 4;
  o->prompt_role = ROLLTUI_ROLE_DEFAULT_PROMPT;
  o->multi_click_ms = 400;
  o->history_limit = 1000;
  o->history_prefix = 1;
  o->insert_typed = 1;
  rolltui_str_set(&o->prompt, "> ", 2);
}

void rolltui_input_options_release(RolltuiInputOptionsRaw* o) {
  rolltui_str_free(&o->prompt);
  rolltui_str_free(&o->placeholder);
  rolltui_str_free(&o->language);
}

void rolltui_input_options_copy(RolltuiInputOptionsRaw* to, const RolltuiInputOptionsRaw* from) {
  if (to == from) return;
  rolltui_str_set(&to->prompt, from->prompt.p, from->prompt.n);
  rolltui_str_set(&to->placeholder, from->placeholder.p, from->placeholder.n);
  to->ambiguous_wide = from->ambiguous_wide;
  to->tab_width = from->tab_width;
  to->inset = from->inset;
  to->prompt_role = from->prompt_role;
  to->multi_click_ms = from->multi_click_ms;
  to->history_limit = from->history_limit;
  to->single_line = from->single_line;
  to->wrap_words = from->wrap_words;
  to->wrap_column = from->wrap_column;
  to->line_numbers = from->line_numbers;
  to->auto_indent = from->auto_indent;
  rolltui_str_set(&to->language, from->language.p, from->language.n);
  to->secret = from->secret;
  to->refuse_empty = from->refuse_empty;
  to->history_prefix = from->history_prefix;
  to->insert_typed = from->insert_typed;
}

int rolltui_input_options_equal(const RolltuiInputOptionsRaw* a, const RolltuiInputOptionsRaw* b) {
  return a->ambiguous_wide == b->ambiguous_wide && a->tab_width == b->tab_width && a->inset == b->inset &&
         a->prompt_role == b->prompt_role && a->multi_click_ms == b->multi_click_ms &&
         a->history_limit == b->history_limit && a->single_line == b->single_line &&
         a->wrap_words == b->wrap_words && a->wrap_column == b->wrap_column && a->line_numbers == b->line_numbers &&
         a->auto_indent == b->auto_indent && a->secret == b->secret && a->refuse_empty == b->refuse_empty &&
         a->history_prefix == b->history_prefix && a->insert_typed == b->insert_typed &&
         rolltui_str_eq(&a->language, b->language.p, b->language.n) &&
         rolltui_str_eq(&a->prompt, b->prompt.p, b->prompt.n) &&
         rolltui_str_eq(&a->placeholder, b->placeholder.p, b->placeholder.n);
}

/* ---- snapshots --------------------------------------------------------------------------- */

static void snapshot_of(const RolltuiInput* in, Snapshot* s) {
  rolltui_str_set(&s->text, in->text.p, in->text.n);
  s->caret = in->caret;
  s->sel = in->sel;
}

static int snapshot_equal(const Snapshot* a, const Snapshot* b) {
  return a->caret == b->caret && a->sel.anchor == b->sel.anchor && a->sel.head == b->sel.head &&
         a->sel.active == b->sel.active && rolltui_str_eq(&a->text, b->text.p, b->text.n);
}

static void snapshot_copy(Snapshot* to, const Snapshot* from) {
  rolltui_str_set(&to->text, from->text.p, from->text.n);
  to->caret = from->caret;
  to->sel = from->sel;
}

/* Pushes a snapshot as the new current value; the redo branch is dropped. */
static void undo_commit(RolltuiInput* in, const Snapshot* v) {
  size_t i;
  for (i = in->undo_at + 1; i < in->undo_len; ++i) rolltui_str_free(&in->undo[i].text);
  in->undo_len = in->undo_at + 1;
  in->undo = (Snapshot*)rolltui_grow_zeroed(in->undo, &in->undo_cap, in->undo_len + 1, sizeof *in->undo);
  snapshot_copy(&in->undo[in->undo_len], v);
  ++in->undo_len;
  if (in->undo_len > UNDO_LIMIT) {
    rolltui_str_free(&in->undo[0].text);
    memmove(in->undo, in->undo + 1, (in->undo_len - 1) * sizeof *in->undo);
    --in->undo_len;
    memset(&in->undo[in->undo_len], 0, sizeof *in->undo);
  }
  in->undo_at = in->undo_len - 1;
}

static void undo_reset(RolltuiInput* in, const Snapshot* baseline) {
  size_t i;
  for (i = 0; i < in->undo_len; ++i) rolltui_str_free(&in->undo[i].text);
  in->undo_len = 0;
  in->undo = (Snapshot*)rolltui_grow_zeroed(in->undo, &in->undo_cap, 1, sizeof *in->undo);
  snapshot_copy(&in->undo[0], baseline);
  in->undo_len = 1;
  in->undo_at = 0;
}

static void undo_release(RolltuiInput* in) {
  size_t i;
  for (i = 0; i < in->undo_len; ++i) rolltui_str_free(&in->undo[i].text);
  rolltui_mem_free(in->undo);
  in->undo = NULL;
  in->undo_len = in->undo_cap = in->undo_at = 0;
}

/* ---- content ----------------------------------------------------------------------------- */

static size_t snap(const RolltuiInput* in, size_t pos) {
  size_t lo = 0, hi = in->g_len;
  pos = zmin(pos, in->text.n);
  /* the first grapheme whose offset is >= pos */
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (in->g[mid].offset < pos) lo = mid + 1;
    else hi = mid;
  }
  return lo == in->g_len ? in->text.n : in->g[lo].offset;
}

static size_t prev_boundary(const RolltuiInput* in, size_t pos) {
  size_t lo = 0, hi = in->g_len;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (in->g[mid].offset < pos) lo = mid + 1;
    else hi = mid;
  }
  return lo == 0 ? 0 : in->g[lo - 1].offset;
}

static size_t next_boundary(const RolltuiInput* in, size_t pos) {
  size_t lo = 0, hi = in->g_len;
  /* the first grapheme whose offset is > pos */
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (pos < in->g[mid].offset) hi = mid;
    else lo = mid + 1;
  }
  return lo == in->g_len ? in->text.n : in->g[lo].offset;
}

static size_t line_start(const RolltuiInput* in, size_t pos) {
  size_t i;
  if (pos == 0) return 0;
  for (i = pos; i-- > 0;)
    if (in->text.p[i] == '\n') return i + 1;
  return 0;
}

static size_t line_end(const RolltuiInput* in, size_t pos) {
  size_t i;
  for (i = pos; i < in->text.n; ++i)
    if (in->text.p[i] == '\n') return i;
  return in->text.n;
}

/* Re-walks the graphemes and re-snaps every position. The one place `g` is filled. */
static void retext(RolltuiInput* in, const char* t, size_t len, size_t caret) {
  rolltui_str_set(&in->text, t, len);
  /* GROWING, AMORTISED: there can be no more clusters than bytes. */
  in->g = (RolltuiUnicodeGrapheme*)rolltui_grow(in->g, &in->g_cap, len ? len : 1, sizeof *in->g);
  in->g_len = rolltui_u_graphemes(in->u, in->text.p, in->text.n, in->opt.ambiguous_wide, in->g);
  in->caret = snap(in, caret);
  if (in->sel.active) {
    in->sel.anchor = snap(in, in->sel.anchor);
    in->sel.head = snap(in, in->sel.head);
  }
  in->dirty = 1;
  in->hl_dirty = 1;
}

/* What may enter the text: CR LF and CR become LF; '\n' and '\t' pass; every other control
 * character (and DEL) is dropped. Into a caller's buffer, which must hold `len` bytes — a
 * sanitise never grows its input. */
static size_t sanitise(const char* in, size_t len, char* out, int drop_newlines) {
  size_t i, n = 0;
  for (i = 0; i < len; ++i) {
    const unsigned char c = (unsigned char)in[i];
    if (c == '\r') {
      if (!drop_newlines) out[n++] = '\n';
      if (i + 1 < len && in[i + 1] == '\n') ++i;
      continue;
    }
    if (c == '\n') {
      if (!drop_newlines) out[n++] = '\n';
      continue;
    }
    if (c == '\t' || (c >= 0x20 && c != 0x7F)) out[n++] = (char)c;
  }
  return n;
}

RolltuiInput* rolltui_input_new(void) {
  RolltuiInput* in = (RolltuiInput*)rolltui_mem_alloc(sizeof *in);
  Snapshot base;
  memset(in, 0, sizeof *in);
  rolltui_input_options_init(&in->opt);
  in->prompt_w = 2;
  in->indent = 2;
  in->width = 80;
  in->rows = 1;
  in->dirty = 1;
  in->click.x = -1;
  in->click.y = -1;
  in->hl_lang = -1;
  in->hl_dirty = 1;
  in->u = rolltui_u_scratch_new();
  memset(&base, 0, sizeof base);
  undo_reset(in, &base);
  return in;
}

void rolltui_input_free(RolltuiInput* in) {
  size_t i;
  if (!in) return;
  rolltui_str_free(&in->text);
  rolltui_mem_free(in->g);
  rolltui_input_options_release(&in->opt);
  undo_release(in);
  for (i = 0; i < in->hist_len; ++i) rolltui_str_free(&in->hist[i]);
  rolltui_mem_free(in->hist);
  rolltui_str_free(&in->draft);
  rolltui_str_free(&in->kill);
  rolltui_highlight_free(in->hl);
  rolltui_str_free(&in->hl_name);
  rolltui_mem_free(in->cells);
  rolltui_mem_free(in->row_end);
  rolltui_u_scratch_free(in->u);
  rolltui_mem_free(in);
}

void rolltui_input_set_copy(RolltuiInput* in, RolltuiCopyFn fn, void* ctx) {
  in->copy_fn = fn;
  in->copy_ctx = ctx;
}

const char* rolltui_input_text(const RolltuiInput* in, size_t* len) { return rolltui_str_get(&in->text, len); }

size_t rolltui_input_caret(const RolltuiInput* in) { return in->caret; }

void rolltui_input_selection(const RolltuiInput* in, RolltuiInputSelectionRaw* out) { *out = in->sel; }

const char* rolltui_input_selected_text(const RolltuiInput* in, size_t* len) {
  const size_t b = in->sel.anchor < in->sel.head ? in->sel.anchor : in->sel.head;
  const size_t e = in->sel.anchor < in->sel.head ? in->sel.head : in->sel.anchor;
  if (!in->sel.active || b == e) {
    *len = 0;
    return "";
  }
  *len = e - b;
  return in->text.p + b;
}

void rolltui_input_set_text(RolltuiInput* in, const char* text, size_t len) {
  Snapshot s;
  char* buf = NULL;
  size_t n = 0;
  memset(&in->sel, 0, sizeof in->sel);
  in->has_goal = 0;
  if (len) {
    buf = (char*)rolltui_mem_alloc(len);
    n = sanitise(text, len, buf, 0);
  }
  retext(in, buf, n, n);
  rolltui_mem_free(buf);
  /* A bulk replace is a fresh document, not an edit: it resets the WHOLE undo stack to the
   * new text as the baseline (Input.hpp's UNDO), discarding any open group. */
  memset(&s, 0, sizeof s);
  snapshot_of(in, &s);
  undo_reset(in, &s);
  rolltui_str_free(&s.text);
  in->undo_pending = 0;
}

void rolltui_input_clear(RolltuiInput* in) {
  rolltui_input_set_text(in, NULL, 0);
  in->hist_pos = in->hist_len;
  rolltui_str_clear(&in->draft);
}

static void close_group(RolltuiInput* in);

static void place(RolltuiInput* in, size_t pos, int extend) {
  close_group(in); /* a caret/selection move with no text change closes any open group */
  in->last_kill = 0;
  pos = snap(in, pos);
  if (extend) {
    if (!in->sel.active) in->sel.anchor = in->caret;
    in->sel.head = pos;
    in->sel.active = 1;
  } else {
    memset(&in->sel, 0, sizeof in->sel);
  }
  in->caret = pos;
}

void rolltui_input_set_caret(RolltuiInput* in, size_t byte, int extend) {
  place(in, byte, extend);
  in->has_goal = 0;
}

void rolltui_input_select_all(RolltuiInput* in) {
  if (in->text.n == 0) return;
  close_group(in);
  in->sel.anchor = 0;
  in->sel.head = in->text.n;
  in->sel.active = 1;
  in->caret = in->text.n;
  in->has_goal = 0;
}

void rolltui_input_clear_selection(RolltuiInput* in) { memset(&in->sel, 0, sizeof in->sel); }

/* ---- editing ----------------------------------------------------------------------------- */

static void erase_range(RolltuiInput* in, size_t b, size_t e) {
  size_t caret = in->caret;
  char* t;
  size_t n;
  b = zmin(b, in->text.n);
  e = zmin(e, in->text.n);
  if (b >= e) return;
  /* The edited text is built in a scratch buffer and handed to retext, because retext
   * REPLACES the text and cannot read from it while it does. */
  n = in->text.n - (e - b);
  t = (char*)rolltui_mem_alloc(n ? n : 1);
  memcpy(t, in->text.p, b);
  memcpy(t + b, in->text.p + e, in->text.n - e);
  if (caret >= e) caret -= (e - b);
  else if (caret > b) caret = b;
  memset(&in->sel, 0, sizeof in->sel);
  retext(in, t, n, caret);
  rolltui_mem_free(t);
  in->has_goal = 0;
}

int rolltui_input_erase_selection(RolltuiInput* in) {
  const size_t b = in->sel.anchor < in->sel.head ? in->sel.anchor : in->sel.head;
  const size_t e = in->sel.anchor < in->sel.head ? in->sel.head : in->sel.anchor;
  if (!in->sel.active || b == e) {
    memset(&in->sel, 0, sizeof in->sel);
    return 0;
  }
  erase_range(in, b, e);
  return 1;
}

/* The mechanics of insert(), with no undo bookkeeping. */
static void raw_insert(RolltuiInput* in, const char* utf8, size_t len) {
  char* s;
  size_t n;
  char* t;
  if (len == 0) {
    rolltui_input_erase_selection(in);
    return;
  }
  s = (char*)rolltui_mem_alloc(len);
  n = sanitise(utf8, len, s, in->opt.single_line);
  rolltui_input_erase_selection(in);
  if (n == 0) {
    rolltui_mem_free(s);
    return;
  }
  t = (char*)rolltui_mem_alloc(in->text.n + n);
  if (in->caret) memcpy(t, in->text.p, in->caret); /* an empty input's text is NULL */
  memcpy(t + in->caret, s, n);
  if (in->text.n > in->caret) memcpy(t + in->caret + n, in->text.p + in->caret, in->text.n - in->caret);
  memset(&in->sel, 0, sizeof in->sel);
  retext(in, t, in->text.n + n, in->caret + n);
  rolltui_mem_free(t);
  rolltui_mem_free(s);
  in->has_goal = 0;
}

/* ---- undo (see UNDO in Input.hpp) -------------------------------------------------------- */

static void close_group(RolltuiInput* in) {
  Snapshot s;
  if (!in->undo_pending) return;
  memset(&s, 0, sizeof s);
  snapshot_of(in, &s);
  undo_commit(in, &s);
  rolltui_str_free(&s.text);
  in->undo_pending = 0;
}

#define EDIT_ORDINARY 0
#define EDIT_ATOMIC 1

/* Called after every mutating primitive with the snapshot taken just before it ran. */
static void note_edit(RolltuiInput* in, int kind, const Snapshot* pre) {
  Snapshot post;
  unsigned long long elapsed;
  int continues;
  memset(&post, 0, sizeof post);
  snapshot_of(in, &post);
  if (snapshot_equal(&post, pre)) { /* nothing changed: not an edit worth recording */
    rolltui_str_free(&post.text);
    return;
  }
  elapsed = in->now_ms >= in->undo_last_ms ? in->now_ms - in->undo_last_ms : UNDO_GROUP_TIMEOUT_MS + 1;
  /* ON = the group never times out, so every ordinary edit for the rest of the session
   * continues the SAME undo step. Undo still works and the stack is still consistent - one
   * ctrl-Z just throws away an hour of typing instead of the last word. */
  continues = kind == EDIT_ORDINARY && in->undo_pending &&
              (testkit_ctl_on("input.undo_group_never_closes") || elapsed <= UNDO_GROUP_TIMEOUT_MS);
  if (!continues) {
    if (in->undo_pending) {
      undo_commit(in, pre); /* an open group closes as a REAL step */
    } else if (!snapshot_equal(&in->undo[in->undo_at], pre)) {
      /* No group was open, yet the live state drifted from the last checkpoint — one or more
       * caret/selection moves happened since. A move is never its own undo step, so this
       * folds the drift into the existing checkpoint rather than committing a new one. */
      snapshot_copy(&in->undo[in->undo_at], pre);
    }
  }
  if (kind == EDIT_ATOMIC) {
    undo_commit(in, &post);
    in->undo_pending = 0;
  } else {
    in->undo_pending = 1;
    in->undo_last_ms = in->now_ms;
  }
  rolltui_str_free(&post.text);
}

/* The pre-snapshot every mutating primitive takes. Two lines, twice per call, and the C
 * cannot hide either — which is the milestone's point, since each one is a copy of the
 * whole text. */
#define WITH_PRE(in, body)              \
  do {                                  \
    Snapshot pre;                       \
    memset(&pre, 0, sizeof pre);        \
    snapshot_of((in), &pre);            \
    body;                               \
    rolltui_str_free(&pre.text);        \
  } while (0)

void rolltui_input_insert(RolltuiInput* in, const char* utf8, size_t len) {
  in->last_kill = 0;
  WITH_PRE(in, {
    const int replace = in->sel.active && in->sel.anchor != in->sel.head;
    raw_insert(in, utf8, len);
    note_edit(in, replace ? EDIT_ATOMIC : EDIT_ORDINARY, &pre);
  });
}

/* A TYPED character: inserted, or in overwrite mode put in place of the grapheme at the caret (never a newline: a line does not
 * swallow the next one). One ordinary edit either way, so a run of typing undoes as one. */
static void typed(RolltuiInput* in, const char* utf8, size_t len) {
  const int has_sel = in->sel.active && in->sel.anchor != in->sel.head;
  in->last_kill = 0;
  if (!in->overwrite || has_sel || in->caret >= in->text.n || in->text.p[in->caret] == '\n') {
    rolltui_input_insert(in, utf8, len);
    return;
  }
  WITH_PRE(in, {
    erase_range(in, in->caret, next_boundary(in, in->caret));
    raw_insert(in, utf8, len);
    note_edit(in, EDIT_ORDINARY, &pre);
  });
}

void rolltui_input_set_overwrite(RolltuiInput* in, int on) { in->overwrite = on != 0; }
int rolltui_input_overwrite(const RolltuiInput* in) { return in->overwrite; }

void rolltui_input_preview_insert(const RolltuiInput* in, const char* utf8, size_t len, RolltuiStr* out) {
  const size_t b = in->sel.anchor < in->sel.head ? in->sel.anchor : in->sel.head;
  const size_t e = in->sel.anchor < in->sel.head ? in->sel.head : in->sel.anchor;
  const int has_sel = in->sel.active && b != e;
  const size_t cut_b = has_sel ? b : in->caret;
  const size_t cut_e = has_sel ? e : in->caret;
  char* s = NULL;
  size_t n = 0;
  if (len) {
    s = (char*)rolltui_mem_alloc(len);
    n = sanitise(utf8, len, s, in->opt.single_line);
  }
  rolltui_str_clear(out);
  rolltui_str_append(out, in->text.p, cut_b);
  rolltui_str_append(out, s, n);
  rolltui_str_append(out, in->text.p + cut_e, in->text.n - cut_e);
  rolltui_mem_free(s);
}

void rolltui_input_erase_backward(RolltuiInput* in) {
  in->last_kill = 0;
  WITH_PRE(in, {
    if (rolltui_input_erase_selection(in)) {
      note_edit(in, EDIT_ATOMIC, &pre);
    } else if (in->caret != 0) {
      erase_range(in, prev_boundary(in, in->caret), in->caret);
      note_edit(in, EDIT_ORDINARY, &pre);
    }
  });
}

void rolltui_input_erase_forward(RolltuiInput* in) {
  in->last_kill = 0;
  WITH_PRE(in, {
    if (rolltui_input_erase_selection(in)) {
      note_edit(in, EDIT_ATOMIC, &pre);
    } else if (in->caret < in->text.n) {
      erase_range(in, in->caret, next_boundary(in, in->caret));
      note_edit(in, EDIT_ORDINARY, &pre);
    }
  });
}

static int is_space_at(const RolltuiInput* in, size_t pos) {
  return pos < in->text.n && (in->text.p[pos] == ' ' || in->text.p[pos] == '\t' || in->text.p[pos] == '\n');
}

size_t rolltui_input_word_left_of(const RolltuiInput* in, size_t pos) {
  size_t p = zmin(pos, in->text.n), b = 0, e = 0;
  if (in->opt.secret) return line_start(in, p); /* a secret's spaces are not spelled out by where a word stops */
  while (p > 0 && is_space_at(in, prev_boundary(in, p))) p = prev_boundary(in, p);
  if (p == 0) return 0;
  rolltui_u_word_range(in->u, in->text.p, in->text.n, prev_boundary(in, p), &b, &e);
  return b;
}

size_t rolltui_input_word_right_of(const RolltuiInput* in, size_t pos) {
  size_t p = zmin(pos, in->text.n), b = 0, e = 0;
  if (in->opt.secret) return line_end(in, p);
  while (p < in->text.n && is_space_at(in, p)) p = next_boundary(in, p);
  if (p >= in->text.n) return in->text.n;
  rolltui_u_word_range(in->u, in->text.p, in->text.n, p, &b, &e);
  return e;
}

/* WHAT A KILL TAKES goes to the kill buffer, which `yank` puts back. A run of kills is one kill: a forward one adds to the end of what
 * the last took, a backward one to its front, so killing three words one at a time yanks back as the three. Anything else ends the run.
 * A secret's text is never kept. `dir` is 1 forward, -1 backward. */
static void kill_range(RolltuiInput* in, size_t b, size_t e, int dir) {
  const int run = in->last_kill != 0;
  b = zmin(b, in->text.n);
  e = zmin(e, in->text.n);
  if (b < e && !in->opt.secret) {
    if (!run) rolltui_str_clear(&in->kill);
    if (dir > 0 || !run) {
      rolltui_str_append(&in->kill, in->text.p + b, e - b);
    } else {
      RolltuiStr front;
      memset(&front, 0, sizeof front);
      rolltui_str_set(&front, in->text.p + b, e - b);
      rolltui_str_append(&front, in->kill.p, in->kill.n);
      rolltui_str_set(&in->kill, front.p, front.n);
      rolltui_str_free(&front);
    }
  }
  memset(&in->sel, 0, sizeof in->sel);
  erase_range(in, b, e);
  in->last_kill = dir;
}

/* The selection's bounds when there is one; 0 when there is none. */
static int selected(const RolltuiInput* in, size_t* b, size_t* e) {
  *b = in->sel.anchor < in->sel.head ? in->sel.anchor : in->sel.head;
  *e = in->sel.anchor < in->sel.head ? in->sel.head : in->sel.anchor;
  return in->sel.active && *b != *e;
}

void rolltui_input_kill_word_backward(RolltuiInput* in) {
  WITH_PRE(in, {
    size_t b = 0; size_t e = 0;
    if (selected(in, &b, &e)) kill_range(in, b, e, -1);
    else kill_range(in, rolltui_input_word_left_of(in, in->caret), in->caret, -1);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

void rolltui_input_kill_word_forward(RolltuiInput* in) {
  WITH_PRE(in, {
    size_t b = 0; size_t e = 0;
    if (selected(in, &b, &e)) kill_range(in, b, e, 1);
    else kill_range(in, in->caret, rolltui_input_word_right_of(in, in->caret), 1);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

void rolltui_input_kill_to_line_start(RolltuiInput* in) {
  WITH_PRE(in, {
    kill_range(in, line_start(in, in->caret), in->caret, -1);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

void rolltui_input_kill_to_line_end(RolltuiInput* in) {
  WITH_PRE(in, {
    kill_range(in, in->caret, line_end(in, in->caret), 1);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

const char* rolltui_input_killed(const RolltuiInput* in, size_t* len) { return rolltui_str_get(&in->kill, len); }

void rolltui_input_yank(RolltuiInput* in) {
  in->last_kill = 0;
  if (in->kill.n == 0) return;
  WITH_PRE(in, {
    raw_insert(in, in->kill.p, in->kill.n);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

/* [a1, e1) and [a2, e2) change places (a1 <= e1 <= a2 <= e2), the caret after both. */
static void swap_ranges(RolltuiInput* in, size_t a1, size_t e1, size_t a2, size_t e2) {
  const size_t n = in->text.n;
  char* t;
  size_t k = 0;
  if (!(a1 < e1 && e1 <= a2 && a2 < e2 && e2 <= n)) return;
  t = (char*)rolltui_mem_alloc(n);
  memcpy(t + k, in->text.p, a1), k += a1;
  memcpy(t + k, in->text.p + a2, e2 - a2), k += e2 - a2;
  memcpy(t + k, in->text.p + e1, a2 - e1), k += a2 - e1;
  memcpy(t + k, in->text.p + a1, e1 - a1), k += e1 - a1;
  memcpy(t + k, in->text.p + e2, n - e2);
  memset(&in->sel, 0, sizeof in->sel);
  retext(in, t, n, e2);
  rolltui_mem_free(t);
  in->has_goal = 0;
}

void rolltui_input_transpose_chars(RolltuiInput* in) {
  const size_t ls = line_start(in, in->caret);
  size_t a, b, c;
  in->last_kill = 0;
  if (in->caret == ls) return; /* nothing before the caret on its line: a line never trades with the one above */
  if (in->caret >= in->text.n || in->text.p[in->caret] == '\n') { /* at a line's end: the last two */
    c = in->caret;
    b = prev_boundary(in, c);
    if (b == ls) return;
    a = prev_boundary(in, b);
  } else {
    a = prev_boundary(in, in->caret);
    b = in->caret;
    c = next_boundary(in, b);
  }
  WITH_PRE(in, {
    swap_ranges(in, a, b, b, c);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

/* The end of the word that starts at `b`. */
static size_t word_end_at(const RolltuiInput* in, size_t b) {
  size_t wb = 0, we = 0;
  rolltui_u_word_range(in->u, in->text.p, in->text.n, b, &wb, &we);
  return we;
}

void rolltui_input_transpose_words(RolltuiInput* in) {
  size_t e2, b2, b1;
  in->last_kill = 0;
  if (in->opt.secret || in->text.n == 0) return;
  e2 = rolltui_input_word_right_of(in, in->caret);
  b2 = rolltui_input_word_left_of(in, e2);
  b1 = rolltui_input_word_left_of(in, b2);
  if (b1 == b2) { /* the caret is at or in the first word: it trades with the next */
    const size_t e3 = rolltui_input_word_right_of(in, e2);
    const size_t b3 = rolltui_input_word_left_of(in, e3);
    if (b3 <= b2 || e3 <= e2) return;
    WITH_PRE(in, {
      swap_ranges(in, b2, e2, b3, e3);
      note_edit(in, EDIT_ATOMIC, &pre);
    });
    return;
  }
  WITH_PRE(in, {
    swap_ranges(in, b1, word_end_at(in, b1), b2, e2);
    note_edit(in, EDIT_ATOMIC, &pre);
  });
}

void rolltui_input_change_case(RolltuiInput* in, int how) {
  size_t b, e;
  const int has_sel = selected(in, &b, &e);
  RolltuiStr out;
  in->last_kill = 0;
  if (!has_sel) {
    b = in->caret;
    e = rolltui_input_word_right_of(in, in->caret);
  }
  if (b >= e) return;
  memset(&out, 0, sizeof out);
  rolltui_u_case(in->u, in->text.p + b, e - b, how, &out);
  WITH_PRE(in, {
    const RolltuiInputSelectionRaw keep = in->sel;
    const size_t n = in->text.n - (e - b) + out.n;
    char* t = (char*)rolltui_mem_alloc(n ? n : 1);
    memcpy(t, in->text.p, b);
    memcpy(t + b, out.p, out.n);
    memcpy(t + b + out.n, in->text.p + e, in->text.n - e);
    memset(&in->sel, 0, sizeof in->sel);
    retext(in, t, n, b + out.n);
    rolltui_mem_free(t);
    if (has_sel) { /* the selection stays on what it changed */
      in->sel.active = 1;
      in->sel.anchor = keep.anchor < keep.head ? b : b + out.n;
      in->sel.head = keep.anchor < keep.head ? b + out.n : b;
      in->caret = in->sel.head;
    }
    note_edit(in, EDIT_ATOMIC, &pre);
  });
  rolltui_str_free(&out);
}

static void apply_snapshot(RolltuiInput* in, const Snapshot* s) {
  in->has_goal = 0;
  in->sel = s->sel;
  retext(in, s->text.p, s->text.n, s->caret); /* re-snaps caret and, if active, the selection */
}

int rolltui_input_undo(RolltuiInput* in) {
  close_group(in);
  in->last_kill = 0;
  if (in->undo_at == 0) return 0;
  --in->undo_at;
  apply_snapshot(in, &in->undo[in->undo_at]);
  return 1;
}

int rolltui_input_redo(RolltuiInput* in) {
  close_group(in);
  in->last_kill = 0;
  if (in->undo_at + 1 >= in->undo_len) return 0;
  ++in->undo_at;
  apply_snapshot(in, &in->undo[in->undo_at]);
  return 1;
}

int rolltui_input_can_undo(const RolltuiInput* in) { return in->undo_pending || in->undo_at > 0; }
int rolltui_input_can_redo(const RolltuiInput* in) {
  return !in->undo_pending && in->undo_at + 1 < in->undo_len;
}

/* ---- motions ----------------------------------------------------------------------------- */

void rolltui_input_move_left(RolltuiInput* in, int extend) {
  const size_t b = in->sel.anchor < in->sel.head ? in->sel.anchor : in->sel.head;
  if (!extend && in->sel.active && in->sel.anchor != in->sel.head) {
    rolltui_input_set_caret(in, b, 0);
    return;
  }
  rolltui_input_set_caret(in, in->caret == 0 ? 0 : prev_boundary(in, in->caret), extend);
}

void rolltui_input_move_right(RolltuiInput* in, int extend) {
  const size_t e = in->sel.anchor < in->sel.head ? in->sel.head : in->sel.anchor;
  if (!extend && in->sel.active && in->sel.anchor != in->sel.head) {
    rolltui_input_set_caret(in, e, 0);
    return;
  }
  rolltui_input_set_caret(in, next_boundary(in, in->caret), extend);
}

void rolltui_input_move_word_left(RolltuiInput* in, int extend) {
  rolltui_input_set_caret(in, rolltui_input_word_left_of(in, in->caret), extend);
}
void rolltui_input_move_word_right(RolltuiInput* in, int extend) {
  rolltui_input_set_caret(in, rolltui_input_word_right_of(in, in->caret), extend);
}
void rolltui_input_move_line_start(RolltuiInput* in, int extend) {
  rolltui_input_set_caret(in, line_start(in, in->caret), extend);
}
void rolltui_input_move_line_end(RolltuiInput* in, int extend) {
  rolltui_input_set_caret(in, line_end(in, in->caret), extend);
}

/* ---- the wrap flow ----------------------------------------------------------------------- */

static void row_end_push(RolltuiInput* in, size_t v) {
  in->row_end = (size_t*)rolltui_grow(in->row_end, &in->row_end_cap, in->row_end_len + 1, sizeof *in->row_end);
  in->row_end[in->row_end_len++] = v;
}

/* How many digits the gutter's widest number has, and so how wide the gutter is: the digits and a space. */
static int gutter_for(const RolltuiInput* in) {
  size_t lines = 1, i;
  int digits = 1;
  if (!in->opt.line_numbers || in->opt.single_line) return 0;
  for (i = 0; i < in->text.n; ++i) lines += in->text.p[i] == '\n';
  while (lines >= 10) lines /= 10, ++digits;
  return digits + 1;
}

/* A grapheme's width in the flow: a tab reaches the next stop, a secret is one mask cell (or nothing). */
static int flow_width(const RolltuiInput* in, const RolltuiUnicodeGrapheme* g, char c0, int col, int tab) {
  if (in->opt.secret == ROLLTUI_INPUT_SHOW_NOTHING) return 0;
  if (in->opt.secret) return 1;
  return c0 == '\t' ? tab - ((col - in->indent) % tab) : g->width;
}

/* The column a row may not pass: the area's width less the insets, or the wrap column when nearer. */
static int flow_cap(const RolltuiInput* in, int width, int column) {
  int cap = imax(width - 2 * in->opt.inset, in->indent + 1);
  if (in->opt.single_line) return 0x3fffffff;
  if (column > 0 && in->indent + column < cap) cap = in->indent + column;
  return cap;
}

#define NO_BREAK ((size_t)-1)

/* Fills the flow arrays for a row that may not pass `cap`. The only caller that does not write into the input's own arrays is
 * `rows_for(other_width)`, which asks for the row COUNT alone — so this returns it and takes a flag saying whether to keep what it
 * computed. WORD WRAP: a row may start after a space; when a grapheme would pass the cap and such a start lies on the row, the row
 * ends there and the word after it is flowed again from the next row's start. */
static int build_flow(RolltuiInput* in, int cap, int keep) {
  const int indent = in->indent;
  const int tab = imax(in->opt.tab_width, 1);
  int row = 0, col = indent;
  size_t i, row_start = 0, brk = NO_BREAK;
  in->cells = (FlowCell*)rolltui_grow(in->cells, &in->cells_cap, in->g_len ? in->g_len : 1, sizeof *in->cells);
  in->row_end_len = 0;
  for (i = 0; i < in->g_len; ++i) {
    const RolltuiUnicodeGrapheme* g = &in->g[i];
    char c0 = in->text.p[g->offset];
    int w;
    if (c0 == '\n') {
      in->cells[i].row = row;
      in->cells[i].col = col;
      in->cells[i].width = 0;
      row_end_push(in, g->offset);
      ++row;
      col = indent;
      row_start = i + 1;
      brk = NO_BREAK;
      continue;
    }
    w = flow_width(in, g, c0, col, tab);
    /* ON = the break rule is a no-op, so a run of text never wraps at the edge: every
     * grapheme keeps its column and the flow reports one row for a line of any length.
     * Nothing overflows a buffer - the caret arithmetic and the drawn rows simply disagree
     * with each other about where the text is. */
    if (!testkit_ctl_on("input.cell_wrap_never_breaks") && w > 0 && col + w > cap && col > indent) {
      if (in->opt.wrap_words && brk != NO_BREAK && brk > row_start && brk <= i && c0 != ' ' && c0 != '\t') {
        i = brk; /* the word that would not fit starts the next row */
        g = &in->g[i];
        c0 = in->text.p[g->offset];
      }
      row_end_push(in, g->offset);
      ++row;
      col = indent;
      row_start = i;
      brk = NO_BREAK;
      w = flow_width(in, g, c0, col, tab);
    }
    in->cells[i].row = row;
    in->cells[i].col = col;
    in->cells[i].width = w;
    col += w;
    if (c0 == ' ' || c0 == '\t') brk = i + 1;
  }
  if (col >= cap && col > indent) { /* a full row: the end of the text starts the next */
    row_end_push(in, in->text.n);
    ++row;
    col = indent;
  }
  row_end_push(in, in->text.n);
  if (keep) {
    in->end_row = row;
    in->end_col = col;
    in->rows = row + 1;
  }
  return row + 1;
}

/* The prompt and the gutter, measured again: where every row's text begins. */
static void measure_indent(RolltuiInput* in) {
  in->gutter_w = gutter_for(in);
  in->indent = in->prompt_w + in->gutter_w;
}

static void ensure(RolltuiInput* in) {
  if (!in->dirty) return;
  measure_indent(in);
  build_flow(in, flow_cap(in, in->width, in->opt.wrap_column), 1);
  in->dirty = 0;
}

void rolltui_input_set_options(RolltuiInput* in, const RolltuiInputOptionsRaw* o) {
  const int retab = o->ambiguous_wide != in->opt.ambiguous_wide;
  if (!rolltui_str_eq(&in->opt.language, o->language.p, o->language.n)) in->hl_dirty = 1;
  rolltui_input_options_copy(&in->opt, o);
  in->prompt_w = rolltui_u_display_width(in->u, in->opt.prompt.p, in->opt.prompt.n, in->opt.ambiguous_wide);
  if (retab)
    in->g_len = rolltui_u_graphemes(in->u, in->text.p, in->text.n, in->opt.ambiguous_wide, in->g);
  in->dirty = 1;
}

const RolltuiInputOptionsRaw* rolltui_input_options(const RolltuiInput* in) { return &in->opt; }

int rolltui_input_rows_for(const RolltuiInput* in, int width) {
  RolltuiInput* m = (RolltuiInput*)in; /* the flow arrays are this object's own scratch */
  if (width == in->width) {
    ensure(m);
    return in->rows;
  }
  {
    /* A different width: compute the count, then mark the kept flow stale so the next
     * `ensure` rebuilds it at the real width. Cheaper than a second set of arrays, and it
     * says out loud that this object has ONE flow. */
    int n;
    measure_indent(m);
    n = build_flow(m, flow_cap(m, width, m->opt.wrap_column), 0);
    m->dirty = 1;
    return n;
  }
}

void rolltui_input_wrapped_text(RolltuiInput* in, int column, RolltuiStr* out) {
  size_t r, begin = 0;
  rolltui_str_clear(out);
  measure_indent(in);
  build_flow(in, column > 0 ? in->indent + column : flow_cap(in, in->width, in->opt.wrap_column), 0);
  for (r = 0; r < in->row_end_len; ++r) {
    const size_t end = in->row_end[r];
    const int hard = end < in->text.n && in->text.p[end] == '\n';
    const int last = r + 1 == in->row_end_len;
    size_t keep = end;
    if (end < begin) continue;
    if (!hard && !last) /* a soft break: the spaces before it are where the row was broken, not text */
      while (keep > begin && (in->text.p[keep - 1] == ' ' || in->text.p[keep - 1] == '\t')) --keep;
    rolltui_str_append(out, in->text.p + begin, keep - begin);
    if (last) break;
    if (hard) {
      rolltui_str_append(out, "\n", 1);
      begin = end + 1;
    } else {
      if (end >= in->text.n) break; /* a full last row: nothing follows it */
      rolltui_str_append(out, "\n", 1);
      begin = end;
    }
  }
  in->dirty = 1; /* the kept flow is the drawn width's: rebuilt at the next `ensure` */
}

int rolltui_input_rows(const RolltuiInput* in) {
  ensure((RolltuiInput*)in);
  return in->rows;
}

void rolltui_input_layout(RolltuiInput* in, RolltuiRect area) {
  int h, cr, row, col;
  in->area.x = area.x + in->opt.inset;
  in->area.y = area.y;
  in->area.w = imax(area.w - 2 * in->opt.inset, 0);
  in->area.h = area.h;
  if (area.w != in->width) {
    in->width = area.w;
    in->dirty = 1;
  }
  ensure(in);
  h = imax(in->area.h, 1);
  rolltui_input_cell_of(in, in->caret, &row, &col);
  cr = row;
  if (cr < in->top) in->top = cr;
  if (cr >= in->top + h) in->top = cr - h + 1;
  in->top = iclamp(in->top, 0, imax(in->rows - h, 0));
}

int rolltui_input_top_row(const RolltuiInput* in) { return in->top; }

void rolltui_input_cell_of(const RolltuiInput* in, size_t offset, int* row, int* col) {
  RolltuiInput* m = (RolltuiInput*)in;
  size_t lo = 0, hi;
  ensure(m);
  offset = snap(in, offset);
  if (offset >= in->text.n) {
    *row = in->end_row;
    *col = in->end_col;
    return;
  }
  hi = in->g_len;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (in->g[mid].offset < offset) lo = mid + 1;
    else hi = mid;
  }
  *row = in->cells[lo].row;
  *col = in->cells[lo].col;
}

/* The position on `row` at column `col`: the grapheme covering it, the first grapheme right
 * of it (a column over the prompt / indent), else the row's end. */
static size_t pos_at(RolltuiInput* in, int row, int col) {
  size_t i;
  ensure(in);
  row = iclamp(row, 0, in->rows - 1);
  for (i = 0; i < in->g_len; ++i) {
    const FlowCell* c = &in->cells[i];
    if (c->row != row) continue;
    if (col < c->col) return in->g[i].offset;
    if (col < c->col + imax(c->width, 1)) return in->g[i].offset;
  }
  return in->row_end[(size_t)row];
}

int rolltui_input_move_up(RolltuiInput* in, int extend) {
  int row, col, goal;
  ensure(in);
  rolltui_input_cell_of(in, in->caret, &row, &col);
  if (row == 0) return 0;
  goal = in->has_goal ? in->goal_col : col;
  place(in, pos_at(in, row - 1, goal), extend);
  in->goal_col = goal;
  in->has_goal = 1;
  return 1;
}

int rolltui_input_move_down(RolltuiInput* in, int extend) {
  int row, col, goal;
  ensure(in);
  rolltui_input_cell_of(in, in->caret, &row, &col);
  if (row >= in->rows - 1) return 0;
  goal = in->has_goal ? in->goal_col : col;
  place(in, pos_at(in, row + 1, goal), extend);
  in->goal_col = goal;
  in->has_goal = 1;
  return 1;
}

int rolltui_input_hit(const RolltuiInput* in, int x, int y, size_t* begin, size_t* end) {
  RolltuiInput* m = (RolltuiInput*)in;
  int row, col;
  size_t i, e;
  if (in->area.w <= 0 && in->area.h <= 0) return 0;
  ensure(m);
  row = iclamp(in->top + (y - in->area.y), 0, in->rows - 1);
  col = x - in->area.x + (in->opt.single_line ? in->scroll_x : 0);
  for (i = 0; i < in->g_len; ++i) {
    const FlowCell* c = &in->cells[i];
    int newline;
    if (c->row != row) continue;
    newline = in->text.p[in->g[i].offset] == '\n';
    if (c->width == 0 && !newline) continue; /* draws nothing, so never "under" a pointer */
    if (col < c->col || (!newline && col < c->col + imax(c->width, 1))) {
      *begin = in->g[i].offset;
      *end = (newline || col < c->col) ? in->g[i].offset : in->g[i].offset + in->g[i].length;
      return 1;
    }
    if (newline) break;
  }
  e = in->row_end[(size_t)row];
  *begin = e;
  *end = e;
  return 1;
}

/* ---- history ----------------------------------------------------------------------------- */

void rolltui_input_push_history(RolltuiInput* in, const char* entry, size_t len) {
  if (in->opt.secret) len = 0; /* a secret is never kept */
  if (len && (in->hist_len == 0 || !rolltui_str_eq(&in->hist[in->hist_len - 1], entry, len))) {
    in->hist = (RolltuiStr*)rolltui_grow_zeroed(in->hist, &in->hist_cap, in->hist_len + 1, sizeof *in->hist);
    rolltui_str_set(&in->hist[in->hist_len], entry, len);
    ++in->hist_len;
    while (in->opt.history_limit > 0 && in->hist_len > in->opt.history_limit) {
      rolltui_str_free(&in->hist[0]);
      memmove(in->hist, in->hist + 1, (in->hist_len - 1) * sizeof *in->hist);
      --in->hist_len;
      memset(&in->hist[in->hist_len], 0, sizeof *in->hist);
      if (in->hist_saved > 0) --in->hist_saved; /* the file's oldest went first */
    }
  }
  in->hist_pos = in->hist_len;
  rolltui_str_clear(&in->draft);
}

size_t rolltui_input_history_count(const RolltuiInput* in) { return in->hist_len; }

const char* rolltui_input_history_at(const RolltuiInput* in, size_t i, size_t* len) {
  if (i >= in->hist_len) {
    if (len) *len = 0;
    return "";
  }
  return rolltui_str_get(&in->hist[i], len);
}

size_t rolltui_input_history_cursor(const RolltuiInput* in) { return in->hist_pos; }

/* An entry Up or Down may land on: one that begins with the text before the caret when browsing began (`history_prefix`), and not
 * the text already shown, so a key press always changes something. */
static int recallable(const RolltuiInput* in, size_t i) {
  const RolltuiStr* h = &in->hist[i];
  const size_t n = in->draft_prefix;
  if (rolltui_str_eq(h, in->text.p, in->text.n)) return 0;
  if (!in->opt.history_prefix || n == 0) return 1;
  return h->n >= n && memcmp(h->p, in->draft.p, n) == 0;
}

int rolltui_input_history_prev(RolltuiInput* in) {
  size_t j;
  if (in->hist_len == 0) return 0;
  if (in->hist_pos >= in->hist_len) {
    in->hist_pos = in->hist_len;
    rolltui_str_set(&in->draft, in->text.p, in->text.n);
    in->draft_prefix = in->caret;
  }
  for (j = in->hist_pos; j-- > 0;)
    if (recallable(in, j)) {
      in->hist_pos = j;
      rolltui_input_set_text(in, in->hist[j].p, in->hist[j].n);
      return 1;
    }
  return 0;
}

int rolltui_input_history_next(RolltuiInput* in) {
  size_t j;
  if (in->hist_pos >= in->hist_len) return 0;
  for (j = in->hist_pos + 1; j < in->hist_len; ++j)
    if (recallable(in, j)) {
      in->hist_pos = j;
      rolltui_input_set_text(in, in->hist[j].p, in->hist[j].n);
      return 1;
    }
  in->hist_pos = in->hist_len;
  rolltui_input_set_text(in, in->draft.p, in->draft.n);
  return 1;
}

void rolltui_input_history_clear(RolltuiInput* in) {
  size_t i;
  for (i = 0; i < in->hist_len; ++i) rolltui_str_free(&in->hist[i]);
  in->hist_len = in->hist_pos = in->hist_saved = 0;
  rolltui_str_clear(&in->draft);
}

/* The entries a history file holds, appended to `into` (a run of the same entry kept once). 0 when the file is there and is not a
 * JSON array of strings. */
static int read_history(const char* path, size_t path_len, RolltuiInput* into, RolltuiStr* err) {
  RolltuiStr bytes, perr;
  RolltuiJsonValue* v;
  size_t i, n;
  int ok = 1;
  memset(&bytes, 0, sizeof bytes);
  memset(&perr, 0, sizeof perr);
  if (!rolltui_preset_read_file(path, path_len, rolltui_str_put, &bytes)) return 1; /* no file: no history yet */
  v = rolltui_json_parse(bytes.p ? bytes.p : "", bytes.n, &perr);
  if (!v || !rolltui_json_is_array(v)) {
    if (err) {
      rolltui_str_set(err, path, path_len);
      rolltui_str_append(err, ": not a history file (a JSON array of strings)", 47);
    }
    ok = 0;
  } else {
    n = rolltui_json_array_size(v);
    for (i = 0; i < n; ++i) {
      size_t len = 0;
      const char* t = rolltui_json_as_string(rolltui_json_array_at(v, i), NULL, 0, &len);
      if (t && len) rolltui_input_push_history(into, t, len);
    }
  }
  rolltui_json_free(v);
  rolltui_str_free(&bytes);
  rolltui_str_free(&perr);
  return ok;
}

int rolltui_input_history_load(RolltuiInput* in, const char* path, size_t path_len, RolltuiStr* err) {
  rolltui_input_history_clear(in);
  if (!read_history(path, path_len, in, err)) return 0;
  in->hist_saved = in->hist_len;
  return 1;
}

int rolltui_input_history_save(RolltuiInput* in, const char* path, size_t path_len, RolltuiStr* err) {
  RolltuiInput* merged = rolltui_input_new();
  RolltuiJsonValue* arr;
  RolltuiStr out;
  size_t i;
  int ok;
  merged->opt.history_limit = in->opt.history_limit;
  if (!read_history(path, path_len, merged, err)) {
    rolltui_input_free(merged);
    return 0;
  }
  for (i = in->hist_saved; i < in->hist_len; ++i) rolltui_input_push_history(merged, in->hist[i].p, in->hist[i].n);
  arr = rolltui_json_array();
  for (i = 0; i < merged->hist_len; ++i) rolltui_json_array_push(arr, rolltui_json_string(merged->hist[i].p, merged->hist[i].n));
  memset(&out, 0, sizeof out);
  rolltui_json_dump(arr, 1, &out);
  rolltui_str_append(&out, "\n", 1);
  ok = rolltui_preset_write_file_atomic(path, path_len, out.p, out.n, err);
  if (ok) { /* this input now holds what the file holds: another instance's entries too */
    rolltui_input_history_clear(in);
    for (i = 0; i < merged->hist_len; ++i) rolltui_input_push_history(in, merged->hist[i].p, merged->hist[i].n);
    in->hist_saved = in->hist_len;
  }
  rolltui_str_free(&out);
  rolltui_json_free(arr);
  rolltui_input_free(merged);
  return ok;
}

/* ---- events ------------------------------------------------------------------------------ */

static void unit_around(const RolltuiInput* in, size_t off, int word, size_t* b, size_t* e) {
  off = zmin(off, in->text.n);
  if (word) {
    rolltui_u_word_range(in->u, in->text.p, in->text.n, off, b, e);
    return;
  }
  *b = line_start(in, off);
  *e = line_end(in, off);
}

static void fire_copy(RolltuiInput* in) {
  size_t n = 0;
  const char* p = rolltui_input_selected_text(in, &n);
  if (in->opt.secret) return; /* a secret never reaches the clipboard */
  if (in->copy_fn) in->copy_fn(in->copy_ctx, p, n);
}

static void drag_to(RolltuiInput* in, int x, int y) {
  size_t b, e;
  close_group(in); /* a drag only moves the selection: closes any open group */
  in->drag.x = x;
  in->drag.y = y;
  if (!rolltui_input_hit(in, x, y, &b, &e)) return;
  if (in->click.count >= 2) unit_around(in, b, in->click.count == 2, &b, &e);
  if (b < in->drag.begin) {
    in->sel.anchor = in->drag.end;
    in->sel.head = b;
  } else {
    in->sel.anchor = in->drag.begin;
    in->sel.head = e > in->drag.end ? e : in->drag.end;
  }
  in->sel.active = 1;
  in->caret = in->sel.head;
}

static unsigned char handle_mouse(RolltuiInput* in, const RolltuiMouseEvent* m, unsigned long long now_ms) {
  switch (m->kind) {
    case 0: { /* Press */
      size_t b, e;
      int paired;
      if (m->button != 1) return ROLLTUI_INPUT_IGNORED;
      if (!rolltui_input_hit(in, m->x, m->y, &b, &e)) return ROLLTUI_INPUT_IGNORED;
      in->has_goal = 0;
      close_group(in); /* a press only moves the caret/selection */
      if (m->shift) {  /* extend from the anchor (or the caret), the pointer's glyph included */
        const size_t anchor = in->sel.active ? in->sel.anchor : in->caret;
        place(in, b >= anchor ? e : b, 1);
        memset(&in->click, 0, sizeof in->click);
        in->click.x = -1;
        in->click.y = -1;
        in->drag.active = 1;
        in->drag.begin = anchor;
        in->drag.end = anchor;
        in->drag.x = m->x;
        in->drag.y = m->y;
        return ROLLTUI_INPUT_HANDLED;
      }
      paired = in->click.count > 0 && now_ms >= in->click.at_ms &&
               now_ms - in->click.at_ms <= in->opt.multi_click_ms && abs(m->x - in->click.x) <= 1 &&
               m->y == in->click.y;
      in->click.count = paired ? in->click.count + 1 : 1;
      if (in->click.count > 3) in->click.count = 1;
      in->click.at_ms = now_ms;
      in->click.x = m->x;
      in->click.y = m->y;
      if (in->click.count >= 2) unit_around(in, b, in->click.count == 2, &b, &e);
      in->drag.active = 1;
      in->drag.begin = b;
      in->drag.end = e;
      in->drag.x = m->x;
      in->drag.y = m->y;
      if (in->click.count >= 2 && b != e) {
        in->sel.anchor = b;
        in->sel.head = e;
        in->sel.active = 1;
        in->caret = e;
      } else {
        memset(&in->sel, 0, sizeof in->sel);
        in->caret = b;
      }
      return ROLLTUI_INPUT_HANDLED;
    }
    case 2: /* Drag */
      if (!in->drag.active) return ROLLTUI_INPUT_IGNORED;
      drag_to(in, m->x, m->y);
      return ROLLTUI_INPUT_HANDLED;
    case 1: /* Release */
      if (!in->drag.active) return ROLLTUI_INPUT_IGNORED;
      /* A release where the pointer already is changes nothing (a double-click's word is not
       * narrowed to the cell under the button). */
      if (m->x != in->drag.x || m->y != in->drag.y) drag_to(in, m->x, m->y);
      in->drag.active = 0;
      if (!in->sel.active || in->sel.anchor == in->sel.head) {
        memset(&in->sel, 0, sizeof in->sel);
        return ROLLTUI_INPUT_HANDLED;
      }
      fire_copy(in);
      return ROLLTUI_INPUT_HANDLED;
    default:
      return ROLLTUI_INPUT_IGNORED;
  }
}

/* The command table, in the order `RolltuiInputActions` declares its members — so the
 * lookup is one loop over a struct read as an array of `const char*`, and adding an action
 * means adding a field and a case rather than remembering a third place. */
#define CMD_COUNT ((int)(sizeof(RolltuiInputActions) / sizeof(const char*)))

static int command_of(const RolltuiInputActions* a, const char* action, size_t len) {
  const char* const* names = (const char* const*)a;
  int i;
  for (i = 0; i < CMD_COUNT; ++i)
    if (names[i] && strlen(names[i]) == len && memcmp(names[i], action, len) == 0) return i;
  return -1;
}

enum {
  CMD_SUBMIT = 0, CMD_NEWLINE, CMD_BACKSPACE, CMD_DELETE, CMD_KILL_WORD_BACK, CMD_KILL_WORD_FWD,
  CMD_KILL_LINE_START, CMD_KILL_LINE_END, CMD_LEFT, CMD_RIGHT, CMD_WORD_LEFT, CMD_WORD_RIGHT,
  CMD_LINE_START, CMD_LINE_END, CMD_UP, CMD_DOWN, CMD_SEL_LEFT, CMD_SEL_RIGHT, CMD_SEL_WORD_LEFT,
  CMD_SEL_WORD_RIGHT, CMD_SEL_LINE_START, CMD_SEL_LINE_END, CMD_SEL_UP, CMD_SEL_DOWN, CMD_SELECT_ALL,
  CMD_CLEAR_SEL, CMD_COPY, CMD_EOF, CMD_UNDO, CMD_REDO, CMD_YANK, CMD_TRANSPOSE_CHARS, CMD_TRANSPOSE_WORDS,
  CMD_UPCASE_WORD, CMD_DOWNCASE_WORD, CMD_CAPITALIZE_WORD, CMD_OVERWRITE
};

/* Only spaces, tabs and newlines: what `refuse_empty` will not submit. */
static int blank(const RolltuiInput* in) {
  size_t i;
  for (i = 0; i < in->text.n; ++i)
    if (in->text.p[i] != ' ' && in->text.p[i] != '\t' && in->text.p[i] != '\n') return 0;
  return 1;
}

/* A newline, carrying the indent of the line it breaks when `auto_indent` asks for it. */
static void newline(RolltuiInput* in) {
  const size_t ls = line_start(in, in->caret);
  size_t e = ls;
  RolltuiStr nl;
  if (!in->opt.auto_indent) {
    rolltui_input_insert(in, "\n", 1);
    return;
  }
  while (e < in->caret && (in->text.p[e] == ' ' || in->text.p[e] == '\t')) ++e;
  memset(&nl, 0, sizeof nl);
  rolltui_str_set(&nl, "\n", 1);
  rolltui_str_append(&nl, in->text.p + ls, e - ls);
  rolltui_input_insert(in, nl.p, nl.n);
  rolltui_str_free(&nl);
}

static unsigned char run_command(RolltuiInput* in, int cmd) {
  switch (cmd) {
    case CMD_SUBMIT: return in->opt.refuse_empty && blank(in) ? ROLLTUI_INPUT_HANDLED : ROLLTUI_INPUT_SUBMIT;
    case CMD_NEWLINE: newline(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_BACKSPACE: rolltui_input_erase_backward(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_DELETE: rolltui_input_erase_forward(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_KILL_WORD_BACK: rolltui_input_kill_word_backward(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_KILL_WORD_FWD: rolltui_input_kill_word_forward(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_KILL_LINE_START: rolltui_input_kill_to_line_start(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_KILL_LINE_END: rolltui_input_kill_to_line_end(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_LEFT: rolltui_input_move_left(in, 0); return ROLLTUI_INPUT_HANDLED;
    case CMD_RIGHT: rolltui_input_move_right(in, 0); return ROLLTUI_INPUT_HANDLED;
    case CMD_WORD_LEFT: rolltui_input_move_word_left(in, 0); return ROLLTUI_INPUT_HANDLED;
    case CMD_WORD_RIGHT: rolltui_input_move_word_right(in, 0); return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_LEFT: rolltui_input_move_left(in, 1); return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_RIGHT: rolltui_input_move_right(in, 1); return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_WORD_LEFT: rolltui_input_move_word_left(in, 1); return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_WORD_RIGHT: rolltui_input_move_word_right(in, 1); return ROLLTUI_INPUT_HANDLED;
    case CMD_LINE_START:
      if (in->text.n == 0) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_start(in, 0);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_LINE_END:
      if (in->text.n == 0) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_end(in, 0);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_LINE_START:
      if (in->text.n == 0) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_start(in, 1);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_LINE_END:
      if (in->text.n == 0) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_end(in, 1);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_UP:
      if (!rolltui_input_move_up(in, 0)) rolltui_input_history_prev(in);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_DOWN:
      if (!rolltui_input_move_down(in, 0)) rolltui_input_history_next(in);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_UP: rolltui_input_move_up(in, 1); return ROLLTUI_INPUT_HANDLED;
    case CMD_SEL_DOWN: rolltui_input_move_down(in, 1); return ROLLTUI_INPUT_HANDLED;
    case CMD_SELECT_ALL: rolltui_input_select_all(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_CLEAR_SEL:
      if (!in->sel.active || in->sel.anchor == in->sel.head) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_clear_selection(in);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_COPY:
      if (!in->sel.active || in->sel.anchor == in->sel.head || in->opt.secret) return ROLLTUI_INPUT_IGNORED;
      fire_copy(in);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_EOF:
      if (in->text.n == 0) return ROLLTUI_INPUT_EOF;
      rolltui_input_erase_forward(in);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_UNDO: return rolltui_input_undo(in) ? ROLLTUI_INPUT_HANDLED : ROLLTUI_INPUT_IGNORED;
    case CMD_REDO: return rolltui_input_redo(in) ? ROLLTUI_INPUT_HANDLED : ROLLTUI_INPUT_IGNORED;
    case CMD_YANK:
      if (in->kill.n == 0) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_yank(in);
      return ROLLTUI_INPUT_HANDLED;
    case CMD_TRANSPOSE_CHARS: rolltui_input_transpose_chars(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_TRANSPOSE_WORDS: rolltui_input_transpose_words(in); return ROLLTUI_INPUT_HANDLED;
    case CMD_UPCASE_WORD: rolltui_input_change_case(in, ROLLTUI_CASE_UPPER); return ROLLTUI_INPUT_HANDLED;
    case CMD_DOWNCASE_WORD: rolltui_input_change_case(in, ROLLTUI_CASE_LOWER); return ROLLTUI_INPUT_HANDLED;
    case CMD_CAPITALIZE_WORD: rolltui_input_change_case(in, ROLLTUI_CASE_TITLE); return ROLLTUI_INPUT_HANDLED;
    case CMD_OVERWRITE: in->overwrite = !in->overwrite; return ROLLTUI_INPUT_HANDLED;
    default: return ROLLTUI_INPUT_IGNORED;
  }
}

int rolltui_input_perform(RolltuiInput* in, const RolltuiInputActions* actions, const char* action, size_t len,
                          unsigned long long now_ms) {
  const int cmd = actions && action ? command_of(actions, action, len) : -1;
  in->now_ms = now_ms;
  return cmd < 0 ? ROLLTUI_INPUT_IGNORED : run_command(in, cmd);
}

static unsigned char handle_key(RolltuiInput* in, const RolltuiChord* k, const RolltuiBindings* b,
                                const RolltuiInputActions* actions) {
  size_t alen = 0;
  const char* action;
  int cmd;
  /* Text is text: a printable character without Ctrl or Alt inserts and is never an action — unless typing is off (`insert_typed`),
   * when it is looked up like any other key and does nothing unbound. */
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && in->opt.insert_typed) {
    char buf[4];
    size_t n;
    if (k->ch < 0x20 || k->ch == 0x7F) return ROLLTUI_INPUT_IGNORED;
    n = rolltui_u_append_utf8(k->ch, buf);
    typed(in, buf, n);
    return ROLLTUI_INPUT_HANDLED;
  }
  action = rolltui_bindings_action_for(b, k, "input", 5, &alen);
  if (!action) return ROLLTUI_INPUT_IGNORED;
  cmd = command_of(actions, action, alen);
  if (cmd < 0) return ROLLTUI_INPUT_IGNORED;
  return run_command(in, cmd);
}

unsigned char rolltui_input_handle(RolltuiInput* in, const RolltuiEvent* e, const RolltuiBindings* bindings,
                                   const RolltuiInputActions* actions, unsigned long long now_ms) {
  in->now_ms = now_ms;
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    in->last_kill = 0;
    WITH_PRE(in, {
      raw_insert(in, e->text, e->text_len);
      note_edit(in, EDIT_ATOMIC, &pre); /* a paste is its own group, never merged */
    });
    return ROLLTUI_INPUT_HANDLED;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE) return handle_mouse(in, &e->mouse, now_ms);
  if (e->kind == ROLLTUI_EVENT_KEY) return handle_key(in, &e->key, bindings, actions);
  return ROLLTUI_INPUT_IGNORED;
}

/* ---- drawing ----------------------------------------------------------------------------- */

/* ONE-ROW mode: how many cells the row is slid left so the caret is in the area — kept on the
 * object, so a press after the draw maps back through the same shift. 0 when rows wrap. */
static int view_shift(RolltuiInput* in) {
  int row, col;
  const int w = in->area.w;
  if (!in->opt.single_line) return 0;
  if (w <= in->indent) { in->scroll_x = 0; return 0; }
  rolltui_input_cell_of(in, in->caret, &row, &col);
  if (col - in->scroll_x >= w) in->scroll_x = col - w + 1;
  if (col - in->scroll_x < in->indent) in->scroll_x = col - in->indent;
  if (in->scroll_x < 0) in->scroll_x = 0;
  return in->scroll_x;
}

void rolltui_input_set_syntax(RolltuiInput* in, RolltuiSyntax* syntax) {
  if (in->syntax != syntax) in->hl_dirty = 1;
  in->syntax = syntax;
}

/* The highlighted runs for the text as it is now: rebuilt only when the text, the language or the set of languages changed. */
static const RolltuiHighlight* highlight(RolltuiInput* in) {
  if (!in->opt.language.n || !in->syntax || in->opt.secret) return NULL;
  if (in->hl_dirty || !rolltui_str_eq(&in->hl_name, in->opt.language.p, in->opt.language.n)) {
    rolltui_str_set(&in->hl_name, in->opt.language.p, in->opt.language.n);
    in->hl_lang = rolltui_syntax_find_name(in->syntax, in->opt.language.p, in->opt.language.n);
    if (in->hl_lang >= 0) {
      if (!in->hl) in->hl = rolltui_highlight_new();
      if (rolltui_highlight_run(in->hl, in->syntax, in->hl_lang, in->text.p ? in->text.p : "", in->text.n) < 0) in->hl_lang = -1;
    }
    in->hl_dirty = 0;
  }
  return in->hl_lang >= 0 ? in->hl : NULL;
}

/* The style a highlighted class draws in: its role, with the weight or slant the class asks for on top. */
static RolltuiStyle class_style(const RolltuiStyle* styles, unsigned char cls, unsigned char base) {
  RolltuiStyle st = styles[rolltui_syntax_role(cls, base)];
  const unsigned char flags = rolltui_syntax_flags(cls);
  if (flags & ROLLTUI_SYN_BOLD) st.bold = 1;
  if (flags & ROLLTUI_SYN_ITALIC) st.italic = 1;
  return st;
}

void rolltui_input_draw(const RolltuiInput* in, RolltuiFrame* f, RolltuiDrawScratch* draw,
                        const RolltuiStyle* styles, const RolltuiInputRoles* roles, int focused) {
  RolltuiInput* m = (RolltuiInput*)in;
  const RolltuiStyle prompt = styles[in->opt.prompt_role];
  const RolltuiStyle txt = styles[roles->text];
  const RolltuiStyle sel = styles[roles->selection];
  const RolltuiStyle ph = styles[roles->placeholder];
  const RolltuiStyle num = styles[roles->line_number];
  const RolltuiRect a = in->area;
  const int h = imax(a.h, 0);
  const int aw = in->opt.ambiguous_wide;
  const size_t sb = in->sel.anchor < in->sel.head ? in->sel.anchor : in->sel.head;
  const size_t se = in->sel.anchor < in->sel.head ? in->sel.head : in->sel.anchor;
  const int has_sel = in->sel.active && sb != se;
  /* A mask glyph is East Asian Ambiguous: where the terminal draws those two cells wide, an asterisk keeps the mask one cell. */
  const char* mask = aw ? "*" : "\xe2\x80\xa2";
  const size_t mask_len = strlen(mask);
  const RolltuiHighlight* hl;
  const RolltuiSyntaxRun* runs = NULL;
  size_t run_n = 0, run_at = 0, line = 0, line_begin = 0;
  size_t i;
  int shift;
  ensure(m);
  shift = view_shift(m);
  hl = highlight(m);
  if (hl) run_n = rolltui_highlight_line(hl, 0, &runs);
#define VISIBLE(r) ((r) >= in->top && (r) < in->top + h)
  if (VISIBLE(0))
    rolltui_frame_put_text(f, draw, a.x, a.y, in->opt.prompt.p, in->opt.prompt.n, prompt, imax(a.w, 0), aw, 0);
  if (in->text.n == 0 && VISIBLE(0) && in->opt.placeholder.n)
    rolltui_frame_put_text(f, draw, a.x + in->indent, a.y, in->opt.placeholder.p, in->opt.placeholder.n, ph,
                           imax(a.w - in->indent, 0), aw, 0);
  if (in->gutter_w > 0) { /* each line's number on its first row, right-aligned against the gutter's space */
    int row = 0;
    size_t ln = 1, k;
    for (k = 0;; ++k) {
      if (VISIBLE(row)) {
        char buf[24];
        const int n = snprintf(buf, sizeof buf, "%zu", ln);
        const int x = a.x + in->prompt_w + in->gutter_w - 1 - n;
        if (n > 0 && x >= a.x) rolltui_frame_put_text(f, draw, x, a.y + (row - in->top), buf, (size_t)n, num, n, aw, 0);
      }
      while (k < in->g_len && in->text.p[in->g[k].offset] != '\n') ++k;
      if (k >= in->g_len) break;
      row = in->cells[k].row + 1;
      ++ln;
    }
  }
  for (i = 0; i < in->g_len; ++i) {
    const FlowCell* c = &in->cells[i];
    int y, x, k;
    size_t off;
    int in_sel;
    RolltuiStyle st;
    char c0;
    off = in->g[i].offset;
    c0 = in->text.p[off];
    st = txt;
    if (hl) { /* the run covering this grapheme, walked forward as the graphemes are */
      while (run_at < run_n && line_begin + runs[run_at].end <= off) ++run_at;
      if (run_at < run_n && line_begin + runs[run_at].begin <= off) st = class_style(styles, runs[run_at].cls, roles->text);
    }
    if (c0 == '\n') {
      ++line;
      line_begin = off + 1;
      run_at = 0;
      run_n = hl ? rolltui_highlight_line(hl, line, &runs) : 0;
    }
    if (!VISIBLE(c->row)) continue;
    y = a.y + (c->row - in->top);
    x = a.x + c->col - shift;
    if (c->col - shift < in->indent) continue; /* slid under the prompt */
    in_sel = has_sel && off >= sb && off < se;
    if (in_sel) st = sel;
    if (c0 == '\n') { /* a selected newline shows as one highlighted cell */
      if (in_sel && c->col < a.w) rolltui_frame_put(f, x, y, " ", 1, 1, st, 0);
      continue;
    }
    if (c->width <= 0 || c->col - shift + c->width > a.w) continue; /* nothing to draw, or the area's edge */
    if (in->opt.secret) {
      rolltui_frame_put(f, x, y, mask, mask_len, 1, st, 0);
      continue;
    }
    if (c0 == '\t') {
      for (k = 0; k < c->width && c->col - shift + k < a.w; ++k) rolltui_frame_put(f, x + k, y, " ", 1, 1, st, 0);
      continue;
    }
    rolltui_frame_put(f, x, y, in->text.p + off, in->g[i].length, c->width, st, 0);
  }
  if (focused) {
    int row, col;
    rolltui_input_cell_of(in, in->caret, &row, &col);
    if (VISIBLE(row)) rolltui_frame_set_cursor(f, a.x + imin(col - shift, imax(a.w - 1, 0)), a.y + (row - in->top), 1);
  }
#undef VISIBLE
}
