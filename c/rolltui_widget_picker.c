/* rolltui/c/rolltui_widget_picker.c — the column browser behind `filepicker`. The rules and the shape
 * are in the header; what is below is the widget. */
#include "rolltui/c/rolltui_widget_picker.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_widgets.h"

/* ---- one column: a directory, its entries, and where the eye is ------------------------- */
typedef struct Column {
  RolltuiStr dir;
  RolltuiDirList entries; /* OWNED; released with the column */
  size_t sel;
  size_t top;      /* the first visible row: this column's own vertical scroll */
  int width;       /* derived from the content, clamped */
  size_t hidden_n; /* how many entries the dotfile rule hid */
  RolltuiStr error; /* opendir failed: a NOTE, never a crash */
} Column;

struct RolltuiPicker {
  RolltuiPickerOptions opt;
  RolltuiUnicodeScratch* u;  /* OWNED: the fit/width scratch */
  RolltuiDrawScratch* draw;  /* OWNED: the cluster walk a put_text needs, reused every frame */
  RolltuiRect inner;
  Column* cols; /* GROWING, AMORTISED (strategy 2): columns come and go with every move */
  size_t n, cap;
  size_t focus_col;
  RolltuiStr root;
  /* WHERE THE CURSOR WAS, PER FOLDER, for the session: dir -> an OWNED RolltuiStr* holding the
   * entry's name. Keyed by the folder rather than by depth, so "no longer valid" needs no stack
   * to pop: another folder is another key, and a name that is gone falls back to the top. */
  RolltuiMap remembered;
  /* THE HORIZONTAL SCROLL IS ONE NUMBER: column 0's left edge relative to the inner rect, never
   * positive; `scroll_target` is where the anchor rule says it belongs, and the slide toward it
   * is `advance`'s. */
  int scroll_x, scroll_target, scroll_from;
  unsigned long long scroll_start_ms;
  int scrolling;
  int anchored_once;             /* a real anchor has run against a known window */
  unsigned long long now_ms;     /* 0 = headless, nothing moves */
  size_t faded_cells;
  long long drag_col;            /* a drag on a divider's thumb: which column; -1 = none */
  int drag_grab;                 /* where on the thumb the press landed */
  unsigned long long cursor_since_ms, opened_since_ms;
  size_t opened_col, opened_sel;
  /* the outcome, until a host takes it */
  unsigned char event_kind, event_inverse;
  RolltuiStr event_path;
  /* CALLER-FILLED scratch the draw reuses frame to frame, so a steady frame allocates nothing */
  RolltuiStr s1, s2, s3;
};

#define SCROLL_MS 120ULL
#define MAX_COLUMN_WIDTH 28 /* a column is never wider; the LAST slot is always this wide */
#define OPENED_MS 1300ULL   /* the burst on a chosen file is over by then */
#define FADE_CELLS 12

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

/* ---- text measured and cut to a column's width ------------------------------------------ */
static int width_of(RolltuiPicker* p, const char* s, size_t n) { return rolltui_u_display_width(p->u, s, n, 0); }
static size_t prefix_bytes(RolltuiPicker* p, const char* s, size_t n, int cells) {
  return rolltui_u_fit(p->u, s, n, cells, 0, NULL);
}
/* The byte offset that drops AT LEAST `cells` from the left, and how many were dropped (one more
 * than asked when a wide glyph straddles the cut: a glyph is never split). */
static size_t skip_bytes(RolltuiPicker* p, const char* s, size_t n, int cells, int* dropped) {
  size_t at = prefix_bytes(p, s, n, cells);
  *dropped = width_of(p, s, at);
  if (*dropped < cells && at < n) {
    at = prefix_bytes(p, s, n, cells + 1);
    *dropped = width_of(p, s, at);
  }
  return at;
}
/* `s`, or a prefix of it with a single-cell ellipsis, fitting `cells` — into `out`. */
static void fit_into(RolltuiPicker* p, const char* s, size_t n, int cells, RolltuiStr* out) {
  rolltui_str_clear(out);
  if (cells <= 0) return;
  if (width_of(p, s, n) <= cells) { rolltui_str_set(out, s, n); return; }
  {
    const size_t keep = prefix_bytes(p, s, n, cells - 1);
    rolltui_str_set(out, s, keep);
    rolltui_str_append(out, "\xE2\x80\xA6", 3);
  }
}

/* ---- columns -------------------------------------------------------------------------------- */
static void column_release(Column* c) {
  rolltui_str_free(&c->dir);
  rolltui_dir_list_release(&c->entries);
  rolltui_str_free(&c->error);
  memset(c, 0, sizeof *c);
}
static void cols_resize(RolltuiPicker* p, size_t n) {
  while (p->n > n) column_release(&p->cols[--p->n]);
}
static Column* cols_push(RolltuiPicker* p) {
  p->cols = (Column*)rolltui_grow_zeroed(p->cols, &p->cap, p->n + 1, sizeof *p->cols);
  memset(&p->cols[p->n], 0, sizeof p->cols[p->n]);
  return &p->cols[p->n++];
}
static Column* focused(RolltuiPicker* p) { return p->focus_col < p->n ? &p->cols[p->focus_col] : NULL; }
static const RolltuiDirEntry* entry_at(const Column* c, size_t i) { return i < c->entries.n ? &c->entries.v[i] : NULL; }
static const RolltuiDirEntry* selected(RolltuiPicker* p) {
  Column* c = focused(p);
  return c ? entry_at(c, c->sel) : NULL;
}
static int rows_visible(const RolltuiPicker* p) { return p->inner.h > 1 ? p->inner.h - 1 : (p->inner.h > 0 ? p->inner.h : 0); }

/* `dir` + "/" + `name`, with the root's slash not doubled. */
static void join(const RolltuiStr* dir, const RolltuiStr* name, RolltuiStr* out) {
  if (dir->n == 0) { rolltui_str_set(out, name->p ? name->p : "", name->n); return; } /* the top: its one entry is the root itself */
  rolltui_str_set(out, dir->p ? dir->p : "", dir->n);
  if (!(dir->n == 1 && dir->p && dir->p[0] == '/')) rolltui_str_append(out, "/", 1);
  rolltui_str_append(out, name->p ? name->p : "", name->n);
}

/* A FOLDER, OR A LINK TO ONE. The reader describes the link itself (a browser shows what is on
 * disk), so a symlinked folder reads as "not a directory" and could never be entered — `/var`
 * on macOS, every `node_modules/.bin`. Entering follows the link; the column is still named by
 * the path a person walked, never by where the link went. */
/* A BUNDLE — a directory named `.app` — is a LEAF to a browser: what it holds is an
 * application's own business, and Enter on it opens the application. */
static int is_bundle(const RolltuiDirEntry* e) {
  return e->is_dir && e->name.n > 4 && e->name.p && memcmp(e->name.p + e->name.n - 4, ".app", 4) == 0;
}
static int folder_like(RolltuiPicker* p, const Column* c, const RolltuiDirEntry* e) {
  struct stat st;
  if (is_bundle(e)) return 0;
  if (e->is_dir) return 1;
  if (!S_ISLNK(e->mode)) return 0;
  join(&c->dir, &e->name, &p->s3);
  return stat(p->s3.p, &st) == 0 && S_ISDIR(st.st_mode);
}
static int selected_is_folder(RolltuiPicker* p) {
  Column* c = focused(p);
  const RolltuiDirEntry* e = selected(p);
  return c && e && folder_like(p, c, e);
}
static void selected_path(RolltuiPicker* p, RolltuiStr* out) {
  Column* c = focused(p);
  if (!c) { rolltui_str_set(out, p->root.p ? p->root.p : "", p->root.n); return; }
  if (c->sel >= c->entries.n) { rolltui_str_set(out, c->dir.p ? c->dir.p : "", c->dir.n); return; }
  join(&c->dir, &c->entries.v[c->sel].name, out);
}

/* THE READ IS THE LIBRARY'S. What is left here is two choices: it describes the LINK rather
 * than what it points at, and it keeps the count of what it hid so a note can say so. */
static void read_column(RolltuiPicker* p, Column* c) {
  RolltuiStr err;
  const int flags = (p->opt.hidden ? ROLLTUI_DIR_HIDDEN : 0) | ROLLTUI_DIR_LINKS | (p->opt.reversed ? ROLLTUI_DIR_REVERSED : 0);
  memset(&err, 0, sizeof err);
  rolltui_str_clear(&c->error);
  /* THE TOP COLUMN holds one entry, the root itself, so the root can be CHOSEN like any other
   * folder — selected in the column to its left, then taken — rather than being the one place
   * the cursor can only be in. Nothing is read for it. */
  if (c->dir.n == 0) {
    size_t i;
    for (i = 0; i < c->entries.n; ++i) rolltui_str_free(&c->entries.v[i].name);
    c->entries.v = (RolltuiDirEntry*)rolltui_grow_zeroed(c->entries.v, &c->entries.cap, 1, sizeof *c->entries.v);
    memset(&c->entries.v[0], 0, sizeof c->entries.v[0]);
    rolltui_str_set(&c->entries.v[0].name, p->root.p ? p->root.p : "/", p->root.n ? p->root.n : 1);
    c->entries.v[0].is_dir = 1;
    c->entries.n = 1;
    c->entries.hidden_n = 0;
    c->hidden_n = 0;
    return;
  }
  if (!rolltui_dir_read(c->dir.p ? c->dir.p : "/", c->dir.n, p->opt.sort, flags, &c->entries, &err))
    rolltui_str_set(&c->error, err.p ? err.p : "", err.n);
  c->hidden_n = c->entries.hidden_n;
  rolltui_str_free(&err);
}

/* ---- the two facts a row may carry after its name ---------------------------------------- */
#define SIZE_CELLS 5     /* "3.2M", right-aligned, a space before it */
#define MODIFIED_CELLS 8 /* "14 Sep" this year, "Sep 2024" before it */
static int show_size(const RolltuiPicker* p) {
  return p->opt.show_size == ROLLTUI_SHOW_ALWAYS || (p->opt.show_size == ROLLTUI_SHOW_WITH_SORT && p->opt.sort == ROLLTUI_SORT_SIZE);
}
static int show_modified(const RolltuiPicker* p) {
  return p->opt.show_modified == ROLLTUI_SHOW_ALWAYS || (p->opt.show_modified == ROLLTUI_SHOW_WITH_SORT && p->opt.sort == ROLLTUI_SORT_MODIFIED);
}
/* How many cells the extra columns take, spaces before each included. */
/* The size and date columns, each a space then its cells, and ONE CELL OF MARGIN after the last
 * — the names sit one in from the left, and a date jammed against the divider reads as part of
 * the next column. */
static int extras_width(const RolltuiPicker* p) {
  const int w = (show_size(p) ? 1 + SIZE_CELLS : 0) + (show_modified(p) ? 1 + MODIFIED_CELLS : 0);
  return w ? w + 1 : 0;
}
static void size_text(long long n, char* out, size_t cap) {
  const char* unit = "";
  double v = (double)n;
  if (v >= 1024.0) { v /= 1024.0; unit = "K"; }
  if (v >= 1024.0) { v /= 1024.0; unit = "M"; }
  if (v >= 1024.0) { v /= 1024.0; unit = "G"; }
  if (!*unit) snprintf(out, cap, "%lld", n);
  else if (v < 10.0) snprintf(out, cap, "%.1f%s", v, unit);
  else snprintf(out, cap, "%.0f%s", v, unit);
}
static void modified_text(long long when, char* out, size_t cap) {
  time_t t = (time_t)when, now = time(NULL);
  struct tm tm_when, tm_now;
  if (when <= 0) { snprintf(out, cap, "-"); return; }
  localtime_r(&t, &tm_when);
  localtime_r(&now, &tm_now);
  if (tm_when.tm_year == tm_now.tm_year) strftime(out, cap, "%e %b", &tm_when);
  else strftime(out, cap, "%b %Y", &tm_when);
  /* `%e` pads with a space: the column is right-aligned, so the pad is dropped */
  if (out[0] == ' ') memmove(out, out + 1, strlen(out));
}

static void measure_width(RolltuiPicker* p, Column* c) {
  int longest = 0;
  size_t i;
  for (i = 0; i < c->entries.n; ++i) {
    const RolltuiDirEntry* e = &c->entries.v[i];
    longest = imax(longest, width_of(p, e->name.p ? e->name.p : "", e->name.n) + (e->is_dir && !is_bundle(e) ? 2 : 0));
  }
  /* ONE CAP, whatever the columns show: the size and date fit INSIDE it, taking their cells
   * from the names, so a column is never wider than the last slot is and nothing jumps when
   * they appear or go. The floor keeps a few cells for the name beside them. */
  c->width = imin(MAX_COLUMN_WIDTH, imax(imax(12, extras_width(p) + 6), longest + 2 + extras_width(p)));
  /* A COLUMN THAT COULD NOT BE OPENED IS AS WIDE AS ITS REASON, up to the window: it is the last
   * column and the anchor puts it at the right edge, so a name-sized width would leave
   * "cannot ope…" of a message whose whole point is the path. */
  if (c->error.n)
    c->width = imax(c->width, imin(p->inner.w > 0 ? p->inner.w : 80, width_of(p, c->error.p, c->error.n) + 2));
}

/* ---- the memory --------------------------------------------------------------------------- */
static void remember(RolltuiPicker* p, const Column* c) {
  RolltuiStr* kept;
  if (c->sel >= c->entries.n) return;
  kept = (RolltuiStr*)rolltui_map_get(&p->remembered, c->dir.p, c->dir.n);
  if (!kept) {
    kept = (RolltuiStr*)rolltui_mem_alloc(sizeof *kept);
    memset(kept, 0, sizeof *kept);
    rolltui_map_put(&p->remembered, c->dir.p, c->dir.n, kept);
  }
  rolltui_str_set(kept, c->entries.v[c->sel].name.p, c->entries.v[c->sel].name.n);
}
static void recall(RolltuiPicker* p, Column* c) {
  const RolltuiStr* kept = (const RolltuiStr*)rolltui_map_get(&p->remembered, c->dir.p, c->dir.n);
  size_t i;
  if (!kept) return;
  for (i = 0; i < c->entries.n; ++i)
    if (rolltui_str_eq(&c->entries.v[i].name, kept->p, kept->n)) { c->sel = i; return; }
}

/* ---- scrolling, vertical ------------------------------------------------------------------ */
/* TWO CLAMPS, because they answer two questions. `clamp_scroll` keeps the SELECTION on screen
 * and is what a key or a click that moved it calls. `clamp_top` keeps the top IN RANGE and is
 * what the per-frame layout calls: a column scrolled away from its selection by the wheel or
 * its thumb stays there until the selection moves — a frame must not scroll it back. */
static void clamp_top(const RolltuiPicker* p, Column* c) {
  const int vis = rows_visible(p);
  const size_t max_top = vis > 0 && c->entries.n > (size_t)vis ? c->entries.n - (size_t)vis : 0;
  if (c->top > max_top) c->top = max_top;
}
static void clamp_scroll(const RolltuiPicker* p, Column* c) {
  const int vis = rows_visible(p);
  if (vis <= 0) { c->top = 0; return; }
  if (c->sel < c->top) c->top = c->sel;
  if (c->sel >= c->top + (size_t)vis) c->top = c->sel - (size_t)vis + 1;
  if (c->entries.n <= (size_t)vis) c->top = 0;
}
static void scroll_column(RolltuiPicker* p, size_t ci, size_t first) {
  Column* c = &p->cols[ci];
  const size_t vis = (size_t)rows_visible(p);
  const size_t max_top = c->entries.n > vis ? c->entries.n - vis : 0;
  c->top = first > max_top ? max_top : first;
}

/* ---- widths and the anchor ---------------------------------------------------------------- */
static int column_has_folder(RolltuiPicker* p, const Column* c) {
  size_t i;
  for (i = 0; i < c->entries.n; ++i)
    if (folder_like(p, c, &c->entries.v[i])) return 1;
  return 0;
}
/* ONE WIDTH PER COLUMN, read by the anchor, the draw, the hit test and the scroll alike. The
 * LAST SLOT is `MAX_COLUMN_WIDTH` wide and whoever occupies it is drawn that wide — the
 * preview, or the focused column when it is a leaf, since nothing can come after it. A focused
 * column with folders keeps its content width, because a slot is reserved after it and
 * widening it would move it, which is the one thing the reserve exists to prevent. */
static int shown_width(RolltuiPicker* p, size_t ci) {
  const int last = ci + 1 == p->n;
  const int preview = ci == p->focus_col + 1;
  const int focused_leaf = ci == p->focus_col && !column_has_folder(p, &p->cols[ci]);
  return last && (preview || focused_leaf) ? imax(MAX_COLUMN_WIDTH, p->cols[ci].width) : p->cols[ci].width;
}
/* Column `ci`'s left edge, in the inner rect's x, at the CURRENT scroll (mid-slide included). */
static int column_x(RolltuiPicker* p, size_t ci) {
  int x = p->inner.x + p->scroll_x;
  size_t j;
  for (j = 0; j < ci && j < p->n; ++j) x += shown_width(p, j) + 1;
  return x;
}
/* THE ANCHOR RULE, as a target. When every column fits, nothing scrolls and the columns pack
 * from the left; otherwise the column to the RIGHT of the focus ends at the right edge, with
 * the last slot reserved at the maximum width while a folder could be selected, so the focused
 * column never shuffles under the eye as the cursor moves. Entering a leaf keeps the columns
 * where they are, and the focused column always starts on screen. */
static void retarget(RolltuiPicker* p) {
  int total = 0, target = 0, slot;
  size_t j;
  if (p->inner.w <= 0) return; /* no window yet: the first layout anchors */
  for (j = 0; j < p->n; ++j) total += shown_width(p, j) + 1;
  total = total > 0 ? total - 1 : 0;
  slot = p->n != 0 && (p->focus_col + 1 < p->n || column_has_folder(p, &p->cols[p->focus_col]));
  if (p->n != 0 && p->focus_col + 1 == p->n && slot) total += 1 + MAX_COLUMN_WIDTH;
  if (p->anchored_once && p->n != 0 && p->focus_col + 1 == p->n && !slot) {
    int focus_x = p->scroll_target;
    for (j = 0; j < p->focus_col; ++j) focus_x += shown_width(p, j) + 1;
    if (focus_x >= 0 && focus_x < p->inner.w) return;
  }
  p->anchored_once = 1;
  if (total > p->inner.w && p->n != 0) {
    const size_t last = p->focus_col + 1 < p->n ? p->focus_col + 1 : p->n - 1;
    int right_end = 0, focus_start = 0;
    for (j = 0; j <= last; ++j) right_end += shown_width(p, j) + 1;
    right_end -= 1;
    if (last == p->focus_col && slot) right_end += 1 + MAX_COLUMN_WIDTH;
    target = imin(0, p->inner.w - right_end);
    for (j = 0; j < p->focus_col; ++j) focus_start += shown_width(p, j) + 1;
    if (focus_start + target < 0) target = -focus_start;
  }
  if (target == p->scroll_target) return;
  p->scroll_target = target;
  if (p->now_ms == 0 || !p->opt.motion) { p->scroll_x = target; p->scrolling = 0; return; }
  p->scroll_from = p->scroll_x;
  p->scroll_start_ms = p->now_ms;
  p->scrolling = 1;
}
/* Where the slide has got to at `now_ms`. Ease-out: fast away, settling into place. */
static void advance(RolltuiPicker* p) {
  unsigned long long t;
  double u, eased;
  if (!p->scrolling) return;
  t = p->now_ms >= p->scroll_start_ms ? p->now_ms - p->scroll_start_ms : SCROLL_MS;
  if (t >= SCROLL_MS || p->now_ms == 0) { p->scroll_x = p->scroll_target; p->scrolling = 0; return; }
  u = (double)t / (double)SCROLL_MS;
  eased = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);
  p->scroll_x = p->scroll_from + (int)lround((p->scroll_target - p->scroll_from) * eased);
}

/* ---- moving --------------------------------------------------------------------------------- */
static void eye_moved(RolltuiPicker* p) { p->cursor_since_ms = p->now_ms; }
/* The column to the right of the focused one exists exactly when a directory is selected. */
static void open_selected(RolltuiPicker* p) {
  Column* c;
  cols_resize(p, p->focus_col + 1);
  if (!selected_is_folder(p)) return;
  selected_path(p, &p->s1);
  c = cols_push(p);
  rolltui_str_set(&c->dir, p->s1.p, p->s1.n);
  read_column(p, c);
  measure_width(p, c);
  recall(p, c);
  clamp_scroll(p, c);
}
static void set_root(RolltuiPicker* p, const char* path, size_t len) {
  Column* c;
  rolltui_str_set(&p->root, path, len);
  cols_resize(p, 0);
  p->focus_col = 0;
  /* Column 0 is the top — the root as its one entry — and column 1 the root's listing, which
   * is where the cursor starts. */
  c = cols_push(p);
  rolltui_str_clear(&c->dir);
  read_column(p, c);
  measure_width(p, c);
  open_selected(p);
  if (p->n > 1) p->focus_col = 1;
  open_selected(p);
  eye_moved(p);
  retarget(p);
}
static void move_by(RolltuiPicker* p, long long delta) {
  Column* c = focused(p);
  long long at;
  if (!c || c->entries.n == 0) return;
  at = (long long)c->sel + delta;
  if (at < 0) at = 0;
  if (at > (long long)c->entries.n - 1) at = (long long)c->entries.n - 1;
  if ((size_t)at != c->sel) eye_moved(p);
  c->sel = (size_t)at;
  remember(p, c);
  clamp_scroll(p, c);
  open_selected(p);
  retarget(p);
}
static void select_row(RolltuiPicker* p, size_t i) {
  Column* c = focused(p);
  if (!c || i >= c->entries.n) return;
  if (i != c->sel) eye_moved(p);
  c->sel = i;
  remember(p, c);
  clamp_scroll(p, c);
  open_selected(p);
  retarget(p);
}
static void into(RolltuiPicker* p) {
  if (!selected_is_folder(p)) return;
  open_selected(p);
  if (p->focus_col + 1 < p->n) {
    ++p->focus_col;
    open_selected(p); /* the NEW focus's own preview, so the column to its right is never empty */
    eye_moved(p);
    retarget(p);
  }
}
static void out(RolltuiPicker* p) {
  size_t slash, i;
  Column* c;
  if (p->focus_col > 0) {
    --p->focus_col;
    cols_resize(p, p->focus_col + 2 <= p->n ? p->focus_col + 2 : p->n);
    eye_moved(p);
    retarget(p);
    return;
  }
  /* At the leftmost column, going out means the parent directory becomes the new root — the one
   * place a path is followed upward, and it never leaves the file system's root. */
  if (p->root.n <= 1 || !p->root.p) return;
  slash = p->root.n;
  while (slash > 0 && p->root.p[slash - 1] != '/') --slash;
  if (slash == 0) return;
  rolltui_str_set(&p->s2, p->root.p + slash, p->root.n - slash); /* the child's name */
  if (slash == 1) rolltui_str_set(&p->s1, "/", 1);
  else rolltui_str_set(&p->s1, p->root.p, slash - 1);
  set_root(p, p->s1.p, p->s1.n);
  c = focused(p);
  if (!c) return;
  for (i = 0; i < c->entries.n; ++i)
    if (rolltui_str_eq(&c->entries.v[i].name, p->s2.p, p->s2.n)) c->sel = i;
  remember(p, c);
  clamp_scroll(p, c);
  open_selected(p);
  retarget(p);
}

/* ---- the public half the adapter calls ---------------------------------------------------- */
RolltuiPicker* rolltui_picker_new(void) {
  RolltuiPicker* p = (RolltuiPicker*)rolltui_mem_alloc(sizeof *p);
  memset(p, 0, sizeof *p);
  rolltui_picker_options_init(&p->opt);
  p->u = rolltui_u_scratch_new();
  p->draw = rolltui_draw_scratch_new();
  p->drag_col = -1;
  p->opened_col = (size_t)-1;
  return p;
}
void rolltui_picker_free(RolltuiPicker* p) {
  size_t i;
  if (!p) return;
  cols_resize(p, 0);
  rolltui_mem_free(p->cols);
  for (i = 0; i < rolltui_map_count(&p->remembered); ++i) {
    RolltuiStr* s = (RolltuiStr*)rolltui_map_value_at(&p->remembered, i);
    rolltui_str_free(s);
    rolltui_mem_free(s);
  }
  rolltui_map_release(&p->remembered);
  rolltui_str_free(&p->root);
  rolltui_str_free(&p->event_path);
  rolltui_str_free(&p->s1);
  rolltui_str_free(&p->s2);
  rolltui_str_free(&p->s3);
  rolltui_u_scratch_free(p->u);
  rolltui_draw_scratch_free(p->draw);
  rolltui_mem_free(p);
}
void rolltui_picker_options_init(RolltuiPickerOptions* o) {
  memset(o, 0, sizeof *o);
  o->hidden = 1;
  o->sort = ROLLTUI_SORT_NAME;
  o->motion = 1;
  o->dividers = 1;
  o->take_folders = 0;
}
void rolltui_picker_set_options(RolltuiPicker* p, const RolltuiPickerOptions* o) {
  const int reread = o->hidden != p->opt.hidden || o->sort != p->opt.sort || o->reversed != p->opt.reversed;
  const int remeasure = o->show_size != p->opt.show_size || o->show_modified != p->opt.show_modified;
  p->opt = *o;
  if (reread) rolltui_picker_reload(p);
  else if (remeasure) { size_t i; for (i = 0; i < p->n; ++i) measure_width(p, &p->cols[i]); retarget(p); }
}
const RolltuiPickerOptions* rolltui_picker_options(const RolltuiPicker* p) { return &p->opt; }
void rolltui_picker_set_now(RolltuiPicker* p, unsigned long long now_ms) { p->now_ms = now_ms; }

/* GO TO A PATH WITH ITS ANCESTORS SHOWING: from `/` down to `path`, each column with the next
 * component selected. The walk IS a visit — each selection goes into the memory — and a
 * component that cannot be entered ends it with one column that says why, in full. */
void rolltui_picker_go_to(RolltuiPicker* p, const char* path, size_t len) {
  size_t at;
  RolltuiStr full;
  memset(&full, 0, sizeof full);
  if (len == 0 || path[0] != '/') {
    /* relative: from the working directory, which is where "." means */
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) rolltui_str_set(&full, cwd, strlen(cwd));
    else rolltui_str_set(&full, "/", 1);
    if (len && !(len == 1 && path[0] == '.')) { rolltui_str_append(&full, "/", 1); rolltui_str_append(&full, path, len); }
  } else {
    rolltui_str_set(&full, path, len);
  }
  while (full.n > 1 && full.p[full.n - 1] == '/') full.p[--full.n] = '\0';
  set_root(p, "/", 1);
  at = 1;
  while (at <= full.n) {
    const char* rest = full.p + at;
    const char* slash = memchr(rest, '/', full.n - at);
    const size_t plen = slash ? (size_t)(slash - rest) : full.n - at;
    Column* c;
    size_t i;
    int found = 0;
    at = slash ? at + plen + 1 : full.n + 1;
    if (plen == 0) continue;
    c = focused(p);
    if (!c) break;
    for (i = 0; i < c->entries.n; ++i)
      if (rolltui_str_eq(&c->entries.v[i].name, rest, plen)) { c->sel = i; found = 1; break; }
    if (found) remember(p, c);
    /* A FILE AT THE END OF THE PATH IS SELECTED, NOT ENTERED: `dirk notes/todo.txt` lands on
     * that file with its folder's column open, which is where a name a person typed leads. */
    if (found && !folder_like(p, c, &c->entries.v[c->sel])) {
      clamp_scroll(p, c);
      open_selected(p);
      break;
    }
    if (!found) {
      Column* bad;
      cols_resize(p, p->focus_col + 1);
      bad = cols_push(p);
      rolltui_str_set(&bad->dir, full.p, full.n);
      read_column(p, bad);
      measure_width(p, bad);
      ++p->focus_col;
      break;
    }
    clamp_scroll(p, c);
    open_selected(p);
    if (p->focus_col + 1 >= p->n) break;
    ++p->focus_col;
    open_selected(p);
  }
  rolltui_str_free(&full);
  eye_moved(p);
  retarget(p);
}

/* Re-read every column in place, keeping the selection BY NAME rather than by index, so a sort
 * change or a dotfile toggle does not move the eye to a different file. */
void rolltui_picker_reload(RolltuiPicker* p) {
  size_t i, j;
  for (i = 0; i < p->n; ++i) {
    Column* c = &p->cols[i];
    if (c->sel < c->entries.n) rolltui_str_set(&p->s1, c->entries.v[c->sel].name.p, c->entries.v[c->sel].name.n);
    else rolltui_str_clear(&p->s1);
    read_column(p, c);
    measure_width(p, c);
    c->sel = 0;
    for (j = 0; j < c->entries.n; ++j)
      if (rolltui_str_eq(&c->entries.v[j].name, p->s1.p ? p->s1.p : "", p->s1.n)) c->sel = j;
    clamp_scroll(p, c);
  }
}

void rolltui_picker_layout(RolltuiPicker* p, RolltuiRect inner) {
  const int was_h = p->inner.h;
  const int resized = inner.h != was_h;
  size_t i;
  p->inner = inner;
  /* A NEW HEIGHT brings every selection back on screen — a column opened before any layout had
   * no rows to clamp against, and a resize can push a selection out. The same height keeps each
   * column where the wheel or its thumb left it. */
  for (i = 0; i < p->n; ++i) {
    if (resized) clamp_scroll(p, &p->cols[i]); else clamp_top(p, &p->cols[i]);
    if (p->cols[i].error.n) measure_width(p, &p->cols[i]); /* sized to the window, known only here */
  }
  retarget(p);
  advance(p);
}

/* ---- the fade at the left edge ------------------------------------------------------------ */
/* A column partly off the left edge is a column the eye has left behind, and cutting it dead at
 * the border reads as a tear: its cells are pulled toward the panel's background, most at the
 * border and none a dozen cells in, on a cosine that bottoms one step above nothing. ONLY IN
 * 24-BIT COLOUR: a blend is a colour the theme did not name. */
static RolltuiStyle faded(RolltuiStyle st, RolltuiStyleColor ground, double keep) {
  RolltuiStyle out;
  rolltui_style_fade(&st, ground, keep, &out);
  return out;
}
static double keep_at(const RolltuiRect* r, int sx) {
  const double t = (sx - r->x + 0.5) / FADE_CELLS;
  const double u = t < 0 ? 0.0 : t > 1 ? 1.0 : t;
  const double eased = 0.5 - 0.5 * cos(u * 3.14159265358979323846);
  const double floor_ = 1.0 / FADE_CELLS;
  return floor_ + (1.0 - floor_) * eased;
}

/* the draw's per-column state, so the helpers below read one struct instead of nine parameters */
typedef struct Draw {
  RolltuiPicker* p;
  RolltuiFrame* f;
  RolltuiDrawScratch* ds;
  RolltuiRect r;
  RolltuiStyleColor ground;
  int clipped;
} Draw;

/* one grapheme at a time, each in its own colour by its distance from the edge */
static void put_faded(Draw* d, int at, int y, const char* text, size_t n, RolltuiStyle st, int room) {
  size_t from = 0;
  int used = 0;
  while (from < n && used < room) {
    size_t to = prefix_bytes(d->p, text + from, n - from, 1) + from;
    int w = 1;
    if (to == from) { to = prefix_bytes(d->p, text + from, n - from, 2) + from; w = 2; }
    if (to == from) break;
    if (used + w > room) break;
    rolltui_frame_put_text(d->f, d->ds, at + used, y, text + from, to - from, faded(st, d->ground, keep_at(&d->r, at + used)), w, 0, 0);
    d->p->faded_cells += (size_t)w;
    used += w;
    from = to;
  }
}
static void put_clipped(Draw* d, int at, int y, const char* text, size_t n, RolltuiStyle st, int room) {
  const int drop = d->r.x - at;
  int dropped = 0;
  size_t from;
  if (drop <= 0) {
    if (d->clipped) put_faded(d, at, y, text, n, st, room);
    else rolltui_frame_put_text(d->f, d->ds, at, y, text, n, st, room, 0, 0);
    return;
  }
  if (drop >= room) return;
  from = skip_bytes(d->p, text, n, drop, &dropped);
  put_faded(d, at + dropped, y, text + from, n - from, st, room - dropped);
}
static void fill_row(Draw* d, int fx, int y, int w, RolltuiStyle st) {
  RolltuiRect one;
  int k;
  if (!d->clipped) {
    RolltuiRect row = {fx, y, w, 1};
    rolltui_frame_fill(d->f, d->ds, row, st, NULL, 0);
    return;
  }
  for (k = 0; k < w; ++k) {
    one.x = fx + k; one.y = y; one.w = 1; one.h = 1;
    rolltui_frame_fill(d->f, d->ds, one, faded(st, d->ground, keep_at(&d->r, fx + k)), NULL, 0);
  }
}

/* THE MARKS COVER THE LETTERS AND NOTHING ELSE — not the chevron, not a space: a spark in empty
 * space is a spark on nothing. Each run of letters is its own span. */
static void mark_words(Draw* d, const char* name, size_t n, int x, int cw, int y, int state, unsigned long long since,
                       int opened_now, unsigned long long opened_since) {
  int cell = 0;
  size_t at = 0;
  while (at < n) {
    size_t next = prefix_bytes(d->p, name + at, n - at, 1) + at;
    int w = 1, letter;
    if (next == at) { next = prefix_bytes(d->p, name + at, n - at, 2) + at; w = 2; }
    if (next == at) break;
    letter = !(next - at == 1 && (name[at] == ' ' || name[at] == '\t'));
    if (letter) {
      int run = w, sx, lx, lw;
      size_t stop = next;
      while (stop < n && name[stop] != ' ' && name[stop] != '\t') {
        size_t n2 = prefix_bytes(d->p, name + stop, n - stop, 1) + stop;
        int w2 = 1;
        if (n2 == stop) { n2 = prefix_bytes(d->p, name + stop, n - stop, 2) + stop; w2 = 2; }
        if (n2 == stop) break;
        run += w2;
        stop = n2;
      }
      sx = x + 1 + cell;
      lx = imax(sx, d->r.x);
      lw = imin(sx + run, x + cw) - lx;
      if (lw > 0) {
        rolltui_frame_mark(d->f, lx, y, lw, state, since, 0);
        if (opened_now) rolltui_frame_mark(d->f, lx, y, lw, ROLLTUI_EFFECT_STATE_PICKER_OPENED, opened_since, 0);
      }
      cell += run;
      at = stop;
      continue;
    }
    cell += w;
    at = next;
  }
}

void rolltui_picker_draw(RolltuiPicker* p, RolltuiFrame* f, const RolltuiStyle* styles,
                         const RolltuiScrollbarGlyphs* glyphs, int ambiguous_wide) {
  Draw d;
  size_t ci;
  int first_drawn = 0;
  const RolltuiStyle text = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  const RolltuiStyle dim = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT_MUTED);
  /* THE HEAD ROW IS A TITLE ROW: one background across every column, the panel's, so the row
   * that does not scroll reads as the row that names the columns rather than as their first line. */
  RolltuiStyle head = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_LABEL);
  const RolltuiStyle here = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_MENU_SELECTED);
  const RolltuiStyle trail = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_SELECTION);
  const RolltuiStyle err_style = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_ERROR);
  const RolltuiStyle border = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BORDER);
  const RolltuiStyle bar_role = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_SCROLLBAR);
  const int still = !p->opt.motion;
  p->faded_cells = 0;
  d.p = p;
  d.f = f;
  d.ds = p->draw;
  d.r = p->inner;
  d.ground = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND)->bg;
  head.bg = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_PANEL_BACKGROUND)->bg;
  if (d.r.w <= 0 || d.r.h <= 0) return;
  d.clipped = 0;
  /* THE TITLE ROW'S BAND, laid column by column below (so a column clipped at the left edge
   * fades, band and name alike); what is right of the last column is laid here, unclipped. */
  {
    int band_from = d.r.x;
    if (p->n) { const size_t last = p->n - 1; band_from = imax(d.r.x, column_x(p, last) + shown_width(p, last) + 1); }
    d.clipped = 0;
    if (band_from < d.r.x + d.r.w) fill_row(&d, band_from, d.r.y, d.r.x + d.r.w - band_from, head);
  }
  for (ci = 0; ci < p->n; ++ci) {
    const Column* c = &p->cols[ci];
    const int x = column_x(p, ci);
    const int sw = shown_width(p, ci);
    const int rows = rows_visible(p);
    int cw, row;
    size_t slash;
    if (x >= d.r.x + d.r.w) break;
    if (x + sw <= d.r.x) continue; /* wholly off the left edge */
    cw = imin(sw, d.r.x + d.r.w - x);
    if (cw <= 0) break;
    /* THE LEFTMOST COLUMN ON SCREEN FADES AT THE EDGE whenever there is more to its left —
     * clipped or not, so the cue that ancestors continue is always there — and only the TOP
     * column, which has nothing to its left, is drawn whole. */
    d.clipped = x < d.r.x || (!first_drawn && ci > 0);
    first_drawn = 1;
    /* This column's cells of the title row's band, and the margin after it: faded with the
     * column when it is clipped at the left edge, cell by cell, as its rows are. */
    { const int bx = imax(x, d.r.x); fill_row(&d, bx, d.r.y, imin(x + sw + 1, d.r.x + d.r.w) - bx, head); }
    /* THE DIVIDER: a hairline in the one-cell margin after this column, the full height, in the
     * border colour — chrome, so it fades with a clipped column. After the LAST column too, when
     * there is room right of it: the empty stretch is the slot the next column will take, and a
     * divider already standing where that column's edge will be means nothing jumps when it
     * opens. And THIS COLUMN'S OWN THUMB ON IT, over the rows, in the window's capsule by the
     * window's arithmetic, so every column says where it is scrolled, in place. */
    if (p->opt.dividers) {
      const int dx = x + sw;
      if (dx >= d.r.x && dx < d.r.x + d.r.w) {
        RolltuiStyle line = border, bar = bar_role, ls, bs;
        RolltuiScrollExtent e;
        RolltuiScrollThumb t;
        int y;
        line.bg = d.ground;
        ls = d.clipped ? faded(line, d.ground, keep_at(&d.r, dx)) : line;
        for (y = d.r.y; y < d.r.y + d.r.h; ++y) {
          /* ON THE TITLE ROW the divider sits on the title's own band, so the band runs
           * unbroken across the columns; the window's border, not this widget's, is the one
           * line that keeps its own ground. */
          RolltuiStyle at_y = ls;
          if (y == d.r.y) { RolltuiStyle on_band = line; on_band.bg = head.bg; at_y = d.clipped ? faded(on_band, d.ground, keep_at(&d.r, dx)) : on_band; }
          rolltui_frame_put_text(f, d.ds, dx, y, "\xE2\x94\x82", 3, at_y, 1, 0, 0);
        }
        e.first = c->top;
        e.visible = (size_t)rows;
        e.total = c->entries.n;
        if (rows > 0 && rolltui_scroll_thumb(&e, rows, &t)) {
          int i;
          if (bar.bg.kind == 0) bar.bg = d.ground;
          bs = d.clipped ? faded(bar, d.ground, keep_at(&d.r, dx)) : bar;
          for (i = 0; i < t.length; ++i) {
            const int ty = d.r.y + 1 + t.offset + i;
            const char* cell;
            if (ty >= d.r.y + d.r.h) break;
            if (t.length == 1) cell = ambiguous_wide ? glyphs->ascii_single : glyphs->single;
            else if (i == 0) cell = ambiguous_wide ? glyphs->ascii_top : glyphs->top;
            else if (i == t.length - 1) cell = ambiguous_wide ? glyphs->ascii_bottom : glyphs->bottom;
            else cell = ambiguous_wide ? glyphs->ascii_middle : glyphs->middle;
            if (!cell[0]) cell = ambiguous_wide ? "#" : "\xE2\x96\x88";
            rolltui_frame_put_text(f, d.ds, dx, ty, cell, strlen(cell), bs, 1, ambiguous_wide, 0);
          }
        }
      }
    }
    /* The column's own head: the directory's last component, one in like the rows. */
    slash = c->dir.n;
    while (slash > 0 && c->dir.p[slash - 1] != '/') --slash;
    if (c->dir.n == 0) rolltui_str_clear(&p->s1); /* the top: unnamed, uncounted */
    else if (c->dir.n == 1 && c->dir.p[0] == '/') fit_into(p, "/", 1, cw - 1, &p->s1);
    else fit_into(p, c->dir.p + slash, c->dir.n - slash, cw - 1, &p->s1);
    put_clipped(&d, x + 1, d.r.y, p->s1.p ? p->s1.p : "", p->s1.n, head, cw - 1);
    /* THE COUNT, right-aligned on the head: how many entries this column holds (dotfiles as the
     * setting says), where the eye already is — a status line's "entries N" said it once for
     * one column and could not be clicked for anything. Only when it fits after the name. */
    {
      char num[32];
      const int nl = snprintf(num, sizeof num, "%zu %s", c->entries.n, c->entries.n == 1 ? "entry" : "entries");
      const int name_w = width_of(p, p->s1.p ? p->s1.p : "", p->s1.n);
      /* Said in words — a bare number beside a name is not obviously a count — and only whole:
       * a column too narrow for the words shows no count rather than a number. */
      if (c->dir.n != 0 && nl > 0 && 1 + name_w + 1 + nl <= cw - 1) put_clipped(&d, x + cw - 1 - nl, d.r.y, num, (size_t)nl, head, nl);
    }
    for (row = 0; row < rows; ++row) {
      const size_t i = c->top + (size_t)row;
      const int y = d.r.y + 1 + row;
      const RolltuiDirEntry* e;
      int is_sel, is_focus_col, is_trail;
      RolltuiStyle st;
      if (y >= d.r.y + d.r.h) break;
      if (i >= c->entries.n) break;
      e = &c->entries.v[i];
      is_sel = i == c->sel;
      is_focus_col = ci == p->focus_col;
      /* A TRAIL ROW — the selection in a column LEFT of the focus, the path we came down. The
       * column to the RIGHT is a preview the cursor has not entered and draws no selection. With
       * motion on, neither selection is a block of background: the theme's effect on the NAME is
       * the marker. With motion off both are highlights, as a still screen needs. */
      is_trail = is_sel && ci < p->focus_col;
      st = is_sel && is_focus_col && still ? here : is_trail && still ? trail : (e->is_dir ? text : (e->unreadable ? dim : text));
      /* A directory is marked with a trailing chevron rather than a colour, so the shape
       * survives `mono` and a colour-blind reader alike. */
      rolltui_str_set(&p->s2, e->name.p ? e->name.p : "", e->name.n);
      if (e->is_dir && !is_bundle(e)) rolltui_str_append(&p->s2, " \xE2\x80\xBA", 4);
      fit_into(p, p->s2.p ? p->s2.p : "", p->s2.n, cw - 1 - extras_width(p), &p->s1);
      if (is_sel && (is_focus_col || is_trail)) {
        const int fx = imax(x, d.r.x);
        const unsigned long long opened_age = p->now_ms >= p->opened_since_ms ? p->now_ms - p->opened_since_ms : 0;
        const int opened_now = ci == p->opened_col && i == p->opened_sel && opened_age < OPENED_MS;
        size_t name_n = p->s1.n;
        if (still) fill_row(&d, fx, y, x + cw - fx, st);
        if (e->is_dir && name_n >= 4 && memcmp(p->s1.p + name_n - 4, " \xE2\x80\xBA", 4) == 0) name_n -= 4;
        mark_words(&d, p->s1.p ? p->s1.p : "", name_n, x, cw, y,
                   is_focus_col ? ROLLTUI_EFFECT_STATE_PICKER_CURSOR : ROLLTUI_EFFECT_STATE_PICKER_TRAIL,
                   is_focus_col ? p->cursor_since_ms : 0, opened_now, p->opened_since_ms);
      }
      put_clipped(&d, x + 1, y, p->s1.p ? p->s1.p : "", p->s1.n, st, cw - 1 - extras_width(p));
      if (extras_width(p) > 0) {
        /* THE FACTS, right-aligned at the column's edge, muted unless the row is the cursor's:
         * a folder has no size to say; a "-" keeps the column's shape. */
        char buf[32];
        int ex = x + cw - extras_width(p);
        /* On a highlighted row — the cursor's, or a trail selection with motion off — the facts
         * sit on the row's own ground: muted lettering, the highlight's background, so the block
         * runs the row's whole width. */
        RolltuiStyle fs = is_sel && is_focus_col && still ? st : dim;
        if (is_trail && still) fs.bg = st.bg;
        if (show_size(p)) {
          int wdt;
          if (e->is_dir) snprintf(buf, sizeof buf, "-");
          else size_text(e->size, buf, sizeof buf);
          wdt = (int)strlen(buf);
          put_clipped(&d, ex + 1 + (SIZE_CELLS - imin(wdt, SIZE_CELLS)), y, buf, (size_t)imin(wdt, SIZE_CELLS), fs, SIZE_CELLS);
          ex += 1 + SIZE_CELLS;
        }
        if (show_modified(p)) {
          int wdt;
          modified_text(e->modified, buf, sizeof buf);
          wdt = (int)strlen(buf);
          put_clipped(&d, ex + 1 + (MODIFIED_CELLS - imin(wdt, MODIFIED_CELLS)), y, buf, (size_t)imin(wdt, MODIFIED_CELLS), fs, MODIFIED_CELLS);
        }
      }
    }
    if (c->entries.n == 0 && d.r.h > 1) {
      /* A DIRECTORY THAT COULD NOT BE OPENED MUST NOT LOOK LIKE AN EMPTY ONE, and an error is not
       * a filename: it gets the rest of the panel, which is space no name needed. */
      const int failed = c->error.n != 0;
      const int room = failed ? (d.r.x + d.r.w - (x + 1)) : (cw - 1);
      if (failed) fit_into(p, c->error.p, c->error.n, room, &p->s1);
      else fit_into(p, "(empty)", 7, room, &p->s1);
      put_clipped(&d, x + 1, d.r.y + 1, p->s1.p ? p->s1.p : "", p->s1.n, failed ? err_style : dim, room);
    }
  }
}

/* ---- the mouse ---------------------------------------------------------------------------- */
static void extent_of(const RolltuiPicker* p, size_t ci, RolltuiScrollExtent* e) {
  e->first = p->cols[ci].top;
  e->visible = (size_t)rows_visible(p);
  e->total = p->cols[ci].entries.n;
}
/* Which column's DIVIDER a cell is on: the one-cell margin after column `ci`, over its rows. */
static long long divider_at(RolltuiPicker* p, int mx, int my) {
  size_t ci;
  if (!p->opt.dividers || my < p->inner.y + 1 || my >= p->inner.y + 1 + rows_visible(p)) return -1;
  for (ci = 0; ci + 1 < p->n; ++ci)
    if (mx == column_x(p, ci) + shown_width(p, ci) && mx >= p->inner.x) return (long long)ci;
  return -1;
}
/* Which column a cell is IN, for the wheel: its rows or its divider. */
static long long column_at(RolltuiPicker* p, int mx) {
  size_t ci;
  for (ci = 0; ci < p->n; ++ci) {
    const int x = column_x(p, ci);
    if (mx >= x && mx <= x + shown_width(p, ci) && mx >= p->inner.x) return (long long)ci;
  }
  return -1;
}

static void take(RolltuiPicker* p) {
  selected_path(p, &p->event_path);
  p->event_kind = ROLLTUI_PICKER_EVENT_TAKEN;
  p->event_inverse = 0;
  /* the burst on the chosen row, for its moment */
  p->opened_since_ms = p->now_ms;
  p->opened_col = p->focus_col;
  p->opened_sel = focused(p) ? focused(p)->sel : 0;
}
/* Enter: a file is taken; a folder is taken or entered by the option. */
static void accept(RolltuiPicker* p) {
  if (selected_is_folder(p) && !p->opt.take_folders) { into(p); return; }
  take(p);
}

/* The code point of a name a typed key is matched against, ASCII case folded: the first — or,
 * for a dotfile, the one after the dot, so `g` reaches `.gitignore` among the g's — unless the
 * key IS the dot, which cycles the dotfiles themselves. */
static RolltuiCodepoint name_head(const RolltuiStr* name, RolltuiCodepoint want) {
  RolltuiDecodedChar d;
  size_t at = 0;
  if (name->n == 0 || !name->p) return 0;
  if (want != '.' && name->p[0] == '.' && name->n > 1) at = 1;
  rolltui_u_decode_one(name->p, name->n, at, &d);
  return d.cp >= 'A' && d.cp <= 'Z' ? d.cp - 'A' + 'a' : d.cp;
}
static int type_to_jump(RolltuiPicker* p, RolltuiCodepoint ch, int reverse) {
  Column* c = focused(p);
  size_t n, k, start;
  const RolltuiCodepoint want = ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
  int on_one;
  if (!c || (n = c->entries.n) == 0) return 1;
  on_one = c->sel < n && name_head(&c->entries.v[c->sel].name, want) == want;
  if (!reverse) {
    start = on_one ? c->sel + 1 : 0;
    for (k = 0; k < n; ++k) { const size_t i = (start + k) % n; if (name_head(&c->entries.v[i].name, want) == want) { select_row(p, i); return 1; } }
  } else {
    start = on_one ? (c->sel + n - 1) % n : n - 1;
    for (k = 0; k < n; ++k) { const size_t i = (start + n - k) % n; if (name_head(&c->entries.v[i].name, want) == want) { select_row(p, i); return 1; } }
  }
  return 1;
}

static int action_is(const char* a, size_t n, const char* name) {
  return name && strlen(name) == n && memcmp(a, name, n) == 0;
}

int rolltui_picker_handle(RolltuiPicker* p, const RolltuiEvent* e, const RolltuiBindings* b,
                          const RolltuiPickerActions* a) {
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent* m = &e->mouse;
    const int k = m->kind;
    long long dc;
    size_t ci;
    /* THE GEOMETRY A POINTER IS TESTED AGAINST IS THE ONE ON SCREEN NOW: a slide the last key
     * began has moved on since the last frame. */
    advance(p);
    if (k == 4 /* WheelUp */ || k == 5 /* WheelDown */) {
      /* THE COLUMN UNDER THE POINTER, not the focused one: every column scrolls on its own. */
      long long at = column_at(p, m->x);
      long long top;
      if (at < 0) at = focused(p) ? (long long)p->focus_col : -1;
      if (at < 0) return 0;
      top = (long long)p->cols[(size_t)at].top + (k == 4 ? -3 : 3);
      scroll_column(p, (size_t)at, (size_t)(top < 0 ? 0 : top));
      return 1;
    }
    /* A DRAG ON A DIVIDER'S THUMB, from the press that grabbed it to the release. */
    if (k == 2 /* Drag */ && p->drag_col >= 0) {
      ci = (size_t)p->drag_col;
      if (ci < p->n) {
        RolltuiScrollExtent ex;
        extent_of(p, ci, &ex);
        scroll_column(p, ci, rolltui_scroll_first_for_cell(&ex, rows_visible(p), m->y - (p->inner.y + 1) - p->drag_grab));
      }
      return 1;
    }
    if (k == 1 /* Release */) { const int was = p->drag_col >= 0; p->drag_col = -1; return was; }
    if (k != 0 /* Press */ && k != 8 /* DoubleClick */) return 0;
    dc = divider_at(p, m->x, m->y);
    if (dc >= 0) {
      /* ON THE THUMB: grab it where it was pressed. ON THE TRACK: bring the thumb's middle to the
       * pointer and hold it, so a click jumps and a drag that starts on the track still drags. */
      RolltuiScrollExtent ex;
      RolltuiScrollThumb t;
      int cell;
      ci = (size_t)dc;
      extent_of(p, ci, &ex);
      if (!rolltui_scroll_thumb(&ex, rows_visible(p), &t)) return 1; /* nothing to scroll: the press is spent */
      cell = m->y - (p->inner.y + 1);
      if (cell >= t.offset && cell < t.offset + t.length) p->drag_grab = cell - t.offset;
      else {
        p->drag_grab = t.length / 2;
        scroll_column(p, ci, rolltui_scroll_first_for_cell(&ex, rows_visible(p), cell - p->drag_grab));
      }
      p->drag_col = dc;
      return 1;
    }
    /* Which column was clicked, and which row in it — the widget's own hit test. */
    for (ci = 0; ci < p->n; ++ci) {
      const int x = column_x(p, ci);
      const int cw = shown_width(p, ci);
      if (m->x >= x && m->x < x + cw && m->x >= p->inner.x) {
        const int row = m->y - p->inner.y - 1;
        /* A PRESS ON EMPTY SPACE below a column's entries is nobody's: it chooses nothing, so it
         * moves nothing — the focus and the columns to the right stay as they were. A press on
         * an entry selects it; one on the head row is the folder's name, and focuses it. */
        if (row >= 0 && p->cols[ci].top + (size_t)row >= p->cols[ci].entries.n) return 1;
        if (row < 0) { rolltui_picker_focus_column(p, ci); return 1; } /* the head: as its breadcrumb part */
        p->focus_col = ci;
        cols_resize(p, ci + 1);
        if (row >= 0) select_row(p, p->cols[ci].top + (size_t)row);
        else open_selected(p);
        retarget(p);
        /* A DOUBLE-CLICK IS ENTER ON THAT ROW — the terminal pairs the presses, the widget acts. */
        if (k == 8 && row >= 0) accept(p);
        return 1;
      }
    }
    return 0;
  }
  if (e->kind != ROLLTUI_EVENT_KEY || !b || !a) return 0;
  {
    size_t len = 0;
    const char* act = rolltui_bindings_action_for(b, &e->key, "picker", 6, &len);
    const int page = imax(1, rows_visible(p) - 1);
    /* TYPE TO JUMP: a bare printable key nothing is bound to lands on the next entry in the focused
     * column whose name starts with it — the first when the cursor is not on one, the one after
     * when it is, wrapping; a shifted key (an upper-case letter) goes backwards, starting from the
     * end. A key no name starts with is consumed and moves nothing. */
    if ((!act || len == 0) && e->key.key == ROLLTUI_KEY_CHAR && !e->key.ctrl && !e->key.alt && e->key.ch >= 0x20 && e->key.ch != 0x7f)
      return type_to_jump(p, e->key.ch, e->key.shift || (e->key.ch >= 'A' && e->key.ch <= 'Z'));
    if (!act || len == 0) return 0;
    if (action_is(act, len, a->up)) move_by(p, -1);
    else if (action_is(act, len, a->down)) move_by(p, 1);
    else if (action_is(act, len, a->page_up)) move_by(p, -page);
    else if (action_is(act, len, a->page_down)) move_by(p, page);
    else if (action_is(act, len, a->first)) select_row(p, 0);
    else if (action_is(act, len, a->last)) { Column* c = focused(p); if (c && c->entries.n) select_row(p, c->entries.n - 1); }
    else if (action_is(act, len, a->into)) into(p);
    else if (action_is(act, len, a->out)) out(p);
    else if (action_is(act, len, a->take)) accept(p);
    else if (action_is(act, len, a->cancel)) { p->event_kind = ROLLTUI_PICKER_EVENT_CANCELLED; rolltui_str_clear(&p->event_path); }
    else if (action_is(act, len, a->copy)) { selected_path(p, &p->event_path); p->event_kind = ROLLTUI_PICKER_EVENT_COPY; p->event_inverse = 0; }
    else if (action_is(act, len, a->copy_inverse)) { selected_path(p, &p->event_path); p->event_kind = ROLLTUI_PICKER_EVENT_COPY; p->event_inverse = 1; }
    else return 0;
    return 1;
  }
}

void rolltui_picker_focus_column(RolltuiPicker* p, size_t column) {
  if (p->n == 0) return;
  p->focus_col = column < p->n ? column : p->n - 1;
  /* AS LEFT DOES: the focused column keeps its preview, the columns deeper than that go. */
  cols_resize(p, p->focus_col + 2 <= p->n ? p->focus_col + 2 : p->n);
  eye_moved(p);
  retarget(p);
}

int rolltui_picker_event(RolltuiPicker* p, RolltuiPickerEvent* out) {
  if (p->event_kind == ROLLTUI_PICKER_EVENT_NONE) return 0;
  if (out) {
    out->kind = p->event_kind;
    out->inverse = p->event_inverse;
    rolltui_str_set(&out->path, p->event_path.p ? p->event_path.p : "", p->event_path.n);
  }
  p->event_kind = ROLLTUI_PICKER_EVENT_NONE; /* collected once: a host asking every frame must not act twice */
  return 1;
}
void rolltui_picker_event_release(RolltuiPickerEvent* e) {
  rolltui_str_free(&e->path);
  e->kind = ROLLTUI_PICKER_EVENT_NONE;
  e->inverse = 0;
}
int rolltui_picker_selected(const RolltuiPicker* p, RolltuiStr* path, int* is_dir) {
  RolltuiPicker* mp = (RolltuiPicker*)p;
  if (!focused(mp)) return 0;
  selected_path(mp, path);
  if (is_dir) *is_dir = selected(mp) ? selected_is_folder(mp) : 1;
  return 1;
}
int rolltui_picker_dir(const RolltuiPicker* p, RolltuiStr* out) {
  Column* c = focused((RolltuiPicker*)p);
  if (!c) return 0;
  rolltui_str_set(out, c->dir.p ? c->dir.p : "", c->dir.n);
  return 1;
}
void rolltui_picker_status(const RolltuiPicker* p, RolltuiPickerStatus* out) {
  const Column* c = focused((RolltuiPicker*)p);
  out->entries = c ? c->entries.n : 0;
  out->hidden = c ? c->hidden_n : 0;
  out->column = p->n ? p->focus_col + 1 : 0;
  out->columns = p->n;
  out->moving = (unsigned char)(p->scrolling != 0);
  if (p->n && p->cols[0].error.n) rolltui_str_set(&out->error, p->cols[0].error.p, p->cols[0].error.n);
  else rolltui_str_clear(&out->error);
}
void rolltui_picker_status_release(RolltuiPickerStatus* s) { rolltui_str_free(&s->error); }
size_t rolltui_picker_hidden_count(const RolltuiPicker* p) {
  Column* c = focused((RolltuiPicker*)p);
  return c ? c->hidden_n : 0;
}
/* THE WINDOW'S OWN BAR, in the border, says where ONE column is scrolled: with dividers, the last
 * column — and only while that column has no divider of its own on screen, since a divider
 * carries the column's thumb and a second bar on the border beside it would say the same thing
 * twice; without dividers, the focused column. */
static Column* bar_column(RolltuiPicker* p) {
  if (p->opt.dividers) {
    const size_t last = p->n ? p->n - 1 : 0;
    int dx;
    if (!p->n) return NULL;
    dx = column_x(p, last) + shown_width(p, last);
    if (dx >= p->inner.x && dx < p->inner.x + p->inner.w) return NULL; /* its divider is there: the thumb is on it */
    return &p->cols[last];
  }
  return focused(p);
}
int rolltui_picker_scroll_extent(const RolltuiPicker* p, RolltuiScrollExtent* out) {
  const Column* c = bar_column((RolltuiPicker*)p);
  if (!c) return 0;
  out->first = c->top;
  out->visible = (size_t)rows_visible(p);
  out->total = c->entries.n;
  return 1;
}
int rolltui_picker_scroll_to(RolltuiPicker* p, size_t first) {
  Column* c = bar_column(p);
  if (!c) return 0;
  scroll_column(p, (size_t)(c - p->cols), first);
  return 1;
}
int rolltui_picker_scrolling(const RolltuiPicker* p) { return p->scrolling; }
size_t rolltui_picker_faded_cells(const RolltuiPicker* p) { return p->faded_cells; }
