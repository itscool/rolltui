/* rolltui/c/rolltui_widget_picker.c — the column browser behind `filepicker`. The rules and the shape
 * are in the header; what is below is the widget. */
#include "rolltui/c/rolltui_widget_picker.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_preview.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_syntax.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_widgets.h"

/* ---- one column: a directory, its entries, and where the eye is ------------------------- */
struct FolderRead;
typedef struct Column {
  RolltuiStr dir;
  /* BEING READ on a worker (the job's, BORROWED), since `load_since_ms`: drawn "loading…" from 100 ms, filled when the read is back */
  struct FolderRead* load;
  unsigned long long load_since_ms;
  RolltuiDirList entries; /* OWNED; released with the column */
  size_t sel;
  size_t top;      /* the first visible row: this column's own vertical scroll */
  int width;       /* derived from the content, clamped */
  size_t hidden_n; /* how many entries the dotfile rule hid */
  RolltuiStr error; /* opendir failed: a NOTE, never a crash */
  /* WATCHING: the folder as it was when read, so a later look can tell that it moved; the pass that read it (a column
   * read in this pass is not looked at again in it); and how far the slow scan of a big folder has got. */
  RolltuiFileSig sig;
  long long sig_read_secs;
  unsigned long long read_pass;
  size_t scan_at;
} Column;

struct RolltuiPicker {
  RolltuiPickerOptions opt;
  int hover_on, hover_x, hover_y; /* the pointer, as the last draw was handed it */
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
  RolltuiStr jump;               /* the letters typed in quick succession: a name's start to jump to */
  unsigned long long jump_ms;    /* when the last of them was typed */
  size_t faded_cells;
  long long drag_col;            /* a drag on a divider's thumb: which column; -1 = none */
  int drag_grab;                 /* where on the thumb the press landed */
  unsigned long long cursor_since_ms, opened_since_ms;
  size_t opened_col, opened_sel;
  /* the outcome, until a host takes it */
  unsigned char event_kind, event_inverse;
  RolltuiStr event_path;
  /* WHAT CHANGED, told once each, in order: the cursor's entry and the focused folder, as they last were */
  struct {
    unsigned char kind;
    RolltuiStr path;
  } changes[8];
  size_t change_head, change_n;
  RolltuiStr seen_entry, seen_folder;
  /* THE FOLDERS VISITED, oldest first, and where in them the picker is; `previous` the one last left */
  RolltuiStr* visited;
  size_t visited_n, visited_cap, visited_at;
  RolltuiStr previous;
  int walking; /* going back or forward: what is visited is not recorded again */
  /* THE FILE PREVIEW: the module that draws what a file holds, the path it was last pointed at (so a frame that
   * moved nothing reads nothing), whether the keys are in it, and where it was drawn (for the pointer). */
  RolltuiPreview* pv; /* BORROWED from `cache`: the preview shown */
  /* PREVIEWS KEPT: the one shown and the files beside it, read ahead while workers are lent, so moving back or on draws at once. OWNED,
   * each made when first needed; `used` is when each was last wanted. */
  RolltuiPreview* cache[4];
  unsigned long long cache_used[4], cache_tick;
  RolltuiSyntax* syn; /* OWNED, made when a file is first previewed with colour on; the preview BORROWS it, so it is freed after the preview */
  RolltuiStr pv_path;
  /* A HOST'S PREVIEWER, asked before the built-in one: what it filled (OWNED), whether it took this path, where it is scrolled
   * to (a line of `host_text`, or with a `host_bound`, a place of the previewer's own) */
  RolltuiMap marked; /* full path -> non-NULL: the entries marked (`marks`) */
  RolltuiPickerDecorFn decor_fn;
  void* decor_ctx;
  int aw;   /* ambiguous-width glyphs two cells wide, as the last draw was told: the preview's sideways reach is measured with it */
  int lazy; /* the person's keys moved the cursor: a folder beside it is read on the workers (a host's call reads at once) */
  /* A COMMAND AS THE PREVIEWER: its own arguments (OWNED), the run for the file previewed (OWNED), and whether one is due to start
   * (from `cmd_asked_ms` + PREVIEW_COMMAND_MS: where the cursor rests). Its output is `host_text`, with `host_on` set. */
  char** cmd;
  size_t cmd_n;
  RolltuiChild* child;
  int cmd_due;
  unsigned long long cmd_asked_ms;
  RolltuiPreviewerFn host_fn;
  void* host_ctx;
  RolltuiAnsiText* host_text;
  int host_on;
  size_t host_offset, host_bound;
  int pv_focus;
  int pv_zoom;         /* the preview has the whole picker: the columns are not drawn */
  RolltuiRect pv_rect; /* w == 0: not on screen */
  /* WATCHING: when the next look at the disk is due (0: not started), and the pass counter that tells a column read a
   * moment ago from one that has waited. */
  unsigned long long watch_next_ms, watch_pass;
  /* CALLER-FILLED scratch the draw reuses frame to frame, so a steady frame allocates nothing */
  RolltuiStr s1, s2, s3;
  RolltuiTasks* tasks; /* BORROWED: what the preview reads on; NULL reads on this thread */
};

static int preview_shown(const RolltuiPicker* p);
static void sync_preview(RolltuiPicker* p);

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
  rolltui_text_cut(p->draw, s, n, cells, ROLLTUI_CUT_END, 0, out);
}

/* ---- columns -------------------------------------------------------------------------------- */
static void column_release(Column* c) {
  rolltui_str_free(&c->dir);
  rolltui_dir_list_release(&c->entries);
  rolltui_str_free(&c->error);
  memset(c, 0, sizeof *c);
}
static void drop_load(Column* c);
static void cols_resize(RolltuiPicker* p, size_t n) {
  while (p->n > n) {
    drop_load(&p->cols[p->n - 1]);
    column_release(&p->cols[--p->n]);
  }
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
/* THE TITLE BAND is one row, or two when the picker says what the cursor is on (`info`) and has the height for it: at least
 * three rows of list are kept. */
static int head_rows(const RolltuiPicker* p) { return p->opt.info && p->inner.h >= 5 ? 2 : 1; }
static int rows_visible(const RolltuiPicker* p) {
  const int hr = head_rows(p);
  return p->inner.h > hr ? p->inner.h - hr : (p->inner.h > 0 ? p->inner.h : 0);
}

/* PATHS. On macOS and Linux a path starts at `/`. On Windows it starts at a drive's root, `C:/`;
 * a path is normalised to forward slashes on the way in (rolltui_picker_go_to). */
static size_t root_len(const char* p, size_t n) {
#if defined(_WIN32)
  if (n >= 2 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') return n >= 3 && p[2] == '/' ? 3 : 2;
#endif
  return n >= 1 && p[0] == '/' ? 1 : 0;
}
static int is_root(const char* p, size_t n) { return p && n > 0 && root_len(p, n) == n; }

/* `dir` + "/" + `name`, with the root's slash not doubled. */
static void join(const RolltuiStr* dir, const RolltuiStr* name, RolltuiStr* out) {
  if (dir->n == 0) { rolltui_str_set(out, name->p ? name->p : "", name->n); return; } /* the top: its one entry is the root itself */
  rolltui_str_set(out, dir->p ? dir->p : "", dir->n);
  if (!is_root(dir->p, dir->n)) rolltui_str_append(out, "/", 1);
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
  RolltuiOsStat st;
  if (is_bundle(e)) return 0;
  if (e->is_dir) return 1;
  if (!ROLLTUI_OS_IS_LINK(e->mode)) return 0;
  join(&c->dir, &e->name, &p->s3);
  return rolltui_os_stat(p->s3.p, 1, &st) == 0 && ROLLTUI_OS_IS_DIR(st.mode);
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
  /* DESCRIBED BEFORE IT IS READ, so a change made while it is being read is seen at the next look rather than trusted. */
  c->read_pass = p->watch_pass;
  c->scan_at = 0;
  c->sig_read_secs = (long long)time(NULL);
  if (c->dir.n) rolltui_filesig_read(c->dir.p, &c->sig);
  else memset(&c->sig, 0, sizeof c->sig);
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
  struct tm tm_when, tm_now;
  if (when <= 0 || !rolltui_os_localtime(when, &tm_when) || !rolltui_os_localtime((long long)time(NULL), &tm_now)) {
    snprintf(out, cap, "-");
    return;
  }
  if (tm_when.tm_year == tm_now.tm_year) strftime(out, cap, "%d %b", &tm_when);
  else strftime(out, cap, "%b %Y", &tm_when);
  /* the day without its leading zero: the column is right-aligned */
  if (out[0] == '0') memmove(out, out + 1, strlen(out));
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
/* Entry `e` of `c` is the one the memory holds for its folder: the one last used there. */
static int last_used(const RolltuiPicker* p, const Column* c, const RolltuiDirEntry* e) {
  const RolltuiStr* kept = c->dir.n ? (const RolltuiStr*)rolltui_map_get(&p->remembered, c->dir.p, c->dir.n) : NULL;
  return kept && e && rolltui_str_eq(&e->name, kept->p, kept->n);
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
  size_t off = (size_t)(p->opt.scrolloff > 0 ? p->opt.scrolloff : 0);
  if (vis <= 0) { c->top = 0; return; }
  if (off > (size_t)(vis - 1) / 2) off = (size_t)(vis - 1) / 2; /* a margin past half the view centres the cursor */
  if (c->sel < c->top + off) c->top = c->sel > off ? c->sel - off : 0;
  if (c->sel + off >= c->top + (size_t)vis) c->top = c->sel + off + 1 - (size_t)vis;
  if (c->entries.n > (size_t)vis && c->top > c->entries.n - (size_t)vis) c->top = c->entries.n - (size_t)vis;
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
/* WHAT THE SLOT AFTER THE FOCUS IS RESERVED AT. A column is never wider than `MAX_COLUMN_WIDTH`, and the slot
 * is that wide — unless a file preview is on, when it is half the picker: the columns anchor to the left of it,
 * a folder under the cursor takes the first cells of it as always, and a file takes all of it. */
/* A PANE'S SHARE of the width when the options give it one (`ratio_*`): its part of the width less the dividers between the three; 0
 * when it has none. */
static int ratio_width(const RolltuiPicker* p, int share) {
  const int sum = p->opt.ratio_parent + p->opt.ratio_current + p->opt.ratio_next;
  const int room = p->inner.w - 2;
  if (share <= 0 || sum <= 0 || room <= 0) return 0;
  return imax(4, room * share / sum);
}
static int slot_width(const RolltuiPicker* p) {
  const int by_ratio = ratio_width(p, p->opt.ratio_next);
  if (by_ratio) return by_ratio;
  return p->opt.preview ? imax(MAX_COLUMN_WIDTH, p->inner.w / 2) : MAX_COLUMN_WIDTH;
}

/* ONE WIDTH PER COLUMN, read by the anchor, the draw, the hit test and the scroll alike. The
 * LAST SLOT is `MAX_COLUMN_WIDTH` wide and whoever occupies it is drawn that wide — the
 * preview, or the focused column when it is a leaf, since nothing can come after it. A focused
 * column with folders keeps its content width, because a slot is reserved after it and
 * widening it would move it, which is the one thing the reserve exists to prevent. */
static int shown_width(RolltuiPicker* p, size_t ci) {
  const int last = ci + 1 == p->n;
  const int preview = ci == p->focus_col + 1;
  { /* the panes given shares: each ancestor a parent's, the cursor's column its own, the one beside it the next's */
    const int by_ratio = ratio_width(p, ci < p->focus_col ? p->opt.ratio_parent : ci == p->focus_col ? p->opt.ratio_current : p->opt.ratio_next);
    if (by_ratio) return by_ratio;
  }
  /* (With a file preview on, the slot after the focus is the preview's whether or not there are folders here, so
   * no column is ever the last thing on screen.) */
  const int focused_leaf = ci == p->focus_col && !p->opt.preview && !p->opt.info && !column_has_folder(p, &p->cols[ci]);
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
  const int slot_w = slot_width(p);
  size_t j;
  if (p->inner.w <= 0) return; /* no window yet: the first layout anchors */
  for (j = 0; j < p->n; ++j) total += shown_width(p, j) + 1;
  total = total > 0 ? total - 1 : 0;
  slot = p->n != 0 && (p->focus_col + 1 < p->n || p->opt.preview || p->opt.info || column_has_folder(p, &p->cols[p->focus_col]));
  if (p->n != 0 && p->focus_col + 1 == p->n && slot) total += 1 + slot_w;
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
    if (last == p->focus_col && slot) right_end += 1 + slot_w;
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

/* ---- a folder read on a worker ------------------------------------------------------------- */
#define FOLDER_WAIT_MS 30 /* how long a move waits for the read, so a folder read quickly is there in the same frame */
#define FOLDER_LOADING_MS 100
/* A FOLDER BEING READ for the column beside the cursor. OWNED by its job, freed in its `done`. `p` is NULL once nothing wants it (the
 * column went, the picker was freed); `taken` once a move waited for it and the column has what it read. */
typedef struct FolderRead {
  RolltuiPicker* p;
  RolltuiTasks* tasks;
  unsigned int job;
  RolltuiStr dir;
  int sort, flags, ok;
  RolltuiDirList list;
  RolltuiStr err;
  RolltuiMutex m;
  int finished; /* under `m`: the worker is done with it */
  int taken;
} FolderRead;

static void folder_work(void* ctx, const RolltuiJob* job) {
  FolderRead* r = (FolderRead*)ctx;
  if (!rolltui_job_cancelled(job)) r->ok = rolltui_dir_read(r->dir.p ? r->dir.p : "/", r->dir.n, r->sort, r->flags, &r->list, &r->err);
  rolltui_mutex_lock(&r->m);
  r->finished = 1;
  rolltui_mutex_unlock(&r->m);
}

static void open_selected(RolltuiPicker* p);
static void retarget(RolltuiPicker* p);
static void sync_preview(RolltuiPicker* p);
static void note_changes(RolltuiPicker* p);

/* WHAT WAS READ, into its column: the listing, or why it could not be. */
static void adopt_folder(RolltuiPicker* p, Column* c, FolderRead* r) {
  rolltui_dir_list_release(&c->entries);
  c->entries = r->list;
  memset(&r->list, 0, sizeof r->list);
  rolltui_str_clear(&c->error);
  if (!r->ok) rolltui_str_set(&c->error, r->err.p ? r->err.p : "", r->err.n);
  c->hidden_n = c->entries.hidden_n;
  c->load = NULL;
  measure_width(p, c);
  recall(p, c);
  clamp_scroll(p, c);
}

static void folder_done(void* ctx, int cancelled) {
  FolderRead* r = (FolderRead*)ctx;
  RolltuiPicker* p = r->p;
  if (p && !r->taken) {
    size_t ci;
    for (ci = 0; ci < p->n; ++ci)
      if (p->cols[ci].load == r) {
        if (cancelled) p->cols[ci].load = NULL; /* the workers went: left as it is, and read again by the watcher */
        else adopt_folder(p, &p->cols[ci], r);
        if (!cancelled && ci == p->focus_col) { /* the person went into it while it was read: what is beside the cursor now */
          p->lazy = 1;
          open_selected(p);
          p->lazy = 0;
          sync_preview(p);
          note_changes(p);
        }
        retarget(p);
        break;
      }
  }
  rolltui_dir_list_release(&r->list);
  rolltui_str_free(&r->err);
  rolltui_str_free(&r->dir);
  rolltui_mutex_destroy(&r->m);
  rolltui_mem_free(r);
}

static void drop_load(Column* c) {
  if (!c->load) return;
  c->load->p = NULL; /* its `done` frees it */
  rolltui_tasks_cancel(c->load->tasks, c->load->job);
  c->load = NULL;
}

/* COLUMN `c` READ ON A WORKER, waited for a moment: 1 when the read was started (and may be back already). */
static int read_column_later(RolltuiPicker* p, Column* c) {
  FolderRead* r;
  int waited;
  if (!p->tasks || !p->lazy || rolltui_tasks_workers(p->tasks) == 0 || c->dir.n == 0) return 0;
  r = (FolderRead*)rolltui_mem_alloc(sizeof *r);
  memset(r, 0, sizeof *r);
  r->p = p;
  r->tasks = p->tasks;
  rolltui_str_set(&r->dir, c->dir.p, c->dir.n);
  r->sort = p->opt.sort;
  r->flags = (p->opt.hidden ? ROLLTUI_DIR_HIDDEN : 0) | ROLLTUI_DIR_LINKS | (p->opt.reversed ? ROLLTUI_DIR_REVERSED : 0);
  rolltui_mutex_init(&r->m);
  /* DESCRIBED BEFORE IT IS READ, as `read_column` does */
  c->read_pass = p->watch_pass;
  c->scan_at = 0;
  c->sig_read_secs = (long long)time(NULL);
  rolltui_filesig_read(c->dir.p, &c->sig);
  c->load = r;
  c->load_since_ms = p->now_ms;
  r->job = rolltui_tasks_start(p->tasks, folder_work, folder_done, r);
  for (waited = 0; waited < FOLDER_WAIT_MS; ++waited) {
    int done;
    rolltui_mutex_lock(&r->m);
    done = r->finished;
    rolltui_mutex_unlock(&r->m);
    if (done) {
      r->taken = 1;
      adopt_folder(p, c, r);
      return 1;
    }
    rolltui_os_sleep_ms(1);
  }
  return 1;
}

/* ---- moving --------------------------------------------------------------------------------- */
static void eye_moved(RolltuiPicker* p) { p->cursor_since_ms = p->now_ms; }
/* The column to the right of the focused one exists exactly when a directory is selected. */
static void open_selected(RolltuiPicker* p) {
  Column* c;
  if (p->lazy && selected_is_folder(p) && p->focus_col + 1 < p->n) { /* the person's move: the folder already beside is kept */
    selected_path(p, &p->s1);
    if (rolltui_str_eq(&p->cols[p->focus_col + 1].dir, p->s1.p, p->s1.n)) {
      cols_resize(p, p->focus_col + 2);
      return;
    }
  }
  cols_resize(p, p->focus_col + 1);
  if (!selected_is_folder(p)) return;
  selected_path(p, &p->s1);
  c = cols_push(p);
  rolltui_str_set(&c->dir, p->s1.p, p->s1.n);
  if (read_column_later(p, c)) return;
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
  if (p->opt.wrap && (delta == 1 || delta == -1)) { /* past an end, a step goes round to the other */
    if (at < 0) at = (long long)c->entries.n - 1;
    else if (at > (long long)c->entries.n - 1) at = 0;
  }
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
  /* Right on a FILE, with a preview showing, enters it: the keys go to the file's contents. */
  if (!selected_is_folder(p)) { if (preview_shown(p)) p->pv_focus = 1; return; }
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
  if (!p->root.p || p->root.n == 0 || is_root(p->root.p, p->root.n)) return;
  slash = p->root.n;
  while (slash > 0 && p->root.p[slash - 1] != '/') --slash;
  if (slash == 0) return;
  rolltui_str_set(&p->s2, p->root.p + slash, p->root.n - slash); /* the child's name */
  if (slash <= root_len(p->root.p, p->root.n)) rolltui_str_set(&p->s1, p->root.p, root_len(p->root.p, p->root.n));
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

/* ---- watching --------------------------------------------------------------------------------- */
/* WHAT IS SHOWN IS WATCHED: every folder that has its insides on screen, and the file that is being previewed. Not by
 * asking the operating system to tell us (an fd per folder and per file, a different mechanism on each system, and a
 * file changed in place is not a change to its folder anyway) but by LOOKING, cheaply and on a clock: a `stat` of each
 * folder shown and of the rows on screen. A folder that moved is read again, a column at a time, keeping the cursor and
 * the scroll where they were BY NAME, and whatever was drawn from what is gone is drawn again from what is there. */
#define WATCH_MS 500ULL      /* between looks */
#define WATCH_MAX_MS 10000ULL
#define SCAN_ROWS 512        /* beyond the rows on screen, how many a folder sorted by size or date is checked for per look */

/* Whether a row is other than it was when its folder was read: gone, a different kind of thing, another size, another
 * date or mode. The reader describes the LINK and so does this. */
static int entry_moved(RolltuiPicker* p, const Column* c, const RolltuiDirEntry* e) {
  RolltuiOsStat st;
  join(&c->dir, &e->name, &p->s3);
  if (rolltui_os_stat(p->s3.p, 0, &st) != 0) return !e->unreadable;
  if (e->unreadable) return 1;
  return (ROLLTUI_OS_IS_DIR(st.mode) ? 1 : 0) != e->is_dir || (ROLLTUI_OS_IS_DIR(st.mode) ? 0 : st.size) != e->size ||
         st.mtime != e->modified || st.mode != e->mode;
}

/* Whether a column needs reading again. The folder first (a file added, removed or renamed in it moves its stamp), then
 * the rows on screen, which is where a file that grew in place shows. When the order depends on what a row holds (by
 * size, by date) a row off screen can move the rows on it, so the rest are looked at too, a slice per look. */
static int column_stale(RolltuiPicker* p, Column* c) {
  RolltuiFileSig now;
  size_t first, last, n = c->entries.n;
  if (c->dir.n == 0) return 0;
  rolltui_filesig_read(c->dir.p, &now);
  if (!rolltui_filesig_same(&c->sig, &now)) return 1;
  if (!now.ok) return 0;                                       /* could not be read then, cannot now: as it was */
  if (rolltui_filesig_racy(&c->sig, c->sig_read_secs)) return 1; /* written too lately to be sure a second write shows */
  first = c->top < n ? c->top : n;
  last = first + (size_t)rows_visible(p);
  if (last > n) last = n;
  {
    size_t i;
    for (i = first; i < last; ++i)
      if (entry_moved(p, c, &c->entries.v[i])) return 1;
  }
  /* the selection, which the wheel may have scrolled off screen */
  if (c->sel < n && (c->sel < first || c->sel >= last) && entry_moved(p, c, &c->entries.v[c->sel])) return 1;
  if ((p->opt.sort == ROLLTUI_SORT_SIZE || p->opt.sort == ROLLTUI_SORT_MODIFIED) && n > last - first) {
    size_t done = 0, k = c->scan_at < n ? c->scan_at : 0;
    while (done < SCAN_ROWS && done < n) {
      if ((k < first || k >= last) && entry_moved(p, c, &c->entries.v[k])) { c->scan_at = k; return 1; }
      k = k + 1 < n ? k + 1 : 0;
      ++done;
    }
    c->scan_at = k;
  }
  return 0;
}

/* READ A COLUMN AGAIN, and put the eye back: the same entry selected BY NAME, and if that entry is gone the one that
 * now stands where it stood (the next, or the last when it was the last); the same entry at the top of the window, so a
 * file added above the screen does not move what is on it. A selection that was on screen stays on screen; one the wheel
 * had scrolled away from is left where the wheel put it, but never past the end. */
static void column_refresh(RolltuiPicker* p, size_t ci) {
  Column* c = &p->cols[ci];
  RolltuiStr sel_name, top_name;
  const size_t old_sel = c->sel, old_top = c->top;
  const int vis = rows_visible(p);
  const int was_visible = vis > 0 && old_sel >= old_top && old_sel < old_top + (size_t)vis;
  size_t i;
  int found = 0, top_found = 0;
  memset(&sel_name, 0, sizeof sel_name);
  memset(&top_name, 0, sizeof top_name);
  if (old_sel < c->entries.n) rolltui_str_set(&sel_name, c->entries.v[old_sel].name.p, c->entries.v[old_sel].name.n);
  if (old_top < c->entries.n) rolltui_str_set(&top_name, c->entries.v[old_top].name.p, c->entries.v[old_top].name.n);
  read_column(p, c);
  measure_width(p, c);
  for (i = 0; i < c->entries.n; ++i) {
    if (!found && sel_name.n && rolltui_str_eq(&c->entries.v[i].name, sel_name.p, sel_name.n)) { c->sel = i; found = 1; }
    if (!top_found && top_name.n && rolltui_str_eq(&c->entries.v[i].name, top_name.p, top_name.n)) { c->top = i; top_found = 1; }
  }
  if (!found) c->sel = c->entries.n ? (old_sel < c->entries.n ? old_sel : c->entries.n - 1) : 0;
  /* A reader at the very top of the list stays at the top, so what arrives above the old first entry — a new file that
   * sorts first, one that grew to be the largest — is on screen; anyone lower keeps the same entry at the top of the
   * window, and nothing on it moves. */
  if (old_top == 0) c->top = 0;
  else if (!top_found) c->top = old_top;
  clamp_top(p, c);
  if (was_visible) clamp_scroll(p, c);
  if (c->sel < c->entries.n) remember(p, c);
  if (ci == p->focus_col && (!found || c->sel != old_sel)) eye_moved(p);
  rolltui_str_free(&sel_name);
  rolltui_str_free(&top_name);
}

/* THE COLUMNS AFTER A ONE THAT MOVED must still be what its selection says: the column right of it lists the selected
 * folder. When the selection no longer names a folder (deleted, renamed, replaced by a file) they go, and the eye
 * comes back to the deepest column still standing — which then opens its own selection, as moving the cursor would. */
static void chain_repair(RolltuiPicker* p, size_t ci) {
  Column* c = &p->cols[ci];
  if (ci + 1 < p->n) {
    const RolltuiDirEntry* e = entry_at(c, c->sel);
    if (e && folder_like(p, c, e)) {
      join(&c->dir, &e->name, &p->s1);
      if (rolltui_str_eq(&p->cols[ci + 1].dir, p->s1.p, p->s1.n)) return; /* still the one it lists: its own look comes next */
    }
    cols_resize(p, ci + 1);
    if (p->focus_col > ci) p->focus_col = ci;
    p->pv_focus = 0;
  }
  if (p->focus_col == ci) open_selected(p);
}

int rolltui_picker_refresh(RolltuiPicker* p) {
  size_t ci;
  int changed = 0;
  ++p->watch_pass;
  for (ci = 1; ci < p->n; ++ci) {
    Column* c = &p->cols[ci];
    if (c->load || c->read_pass == p->watch_pass || !column_stale(p, c)) continue;
    changed = 1;
    column_refresh(p, ci);
    chain_repair(p, ci);
  }
  /* WHAT A LINK POINTS AT can go, or come, without its own folder's stamp moving (the link is still there, unchanged): so
   * every column is checked against what the selection before it says, whether or not anything was read. */
  for (ci = 1; ci < p->n; ++ci) {
    const size_t n_before = p->n, focus_before = p->focus_col;
    chain_repair(p, ci);
    if (p->n != n_before || p->focus_col != focus_before) changed = 1;
  }
  if (changed) {
    if (p->drag_col >= (long long)p->n) p->drag_col = -1;
    retarget(p);
  }
  sync_preview(p);
  if (p->pv && p->pv_path.n && rolltui_preview_refresh(p->pv)) changed = 1;
  return changed;
}

/* The look, on a clock: at most every WATCH_MS, and never spending more than a twentieth of the time looking, so a
 * slow disk or a huge folder backs the interval off rather than eating the frame. Nothing runs headless (the clock is 0):
 * a frame that is a golden is a still. */
static unsigned long long mono_ms(void) { return rolltui_os_monotonic_ms(); }
static void watch_tick(RolltuiPicker* p) {
  unsigned long long began, spent, wait;
  if (p->opt.no_watch || p->now_ms == 0 || p->n == 0) return;
  if (p->watch_next_ms > p->now_ms + WATCH_MAX_MS) p->watch_next_ms = 0; /* the clock went somewhere else */
  if (p->watch_next_ms == 0) { p->watch_next_ms = p->now_ms + WATCH_MS; return; }
  if (p->now_ms < p->watch_next_ms) return;
  began = mono_ms();
  rolltui_picker_refresh(p);
  spent = mono_ms() - began;
  wait = spent * 20 > WATCH_MS ? (spent * 20 < WATCH_MAX_MS ? spent * 20 : WATCH_MAX_MS) : WATCH_MS;
  p->watch_next_ms = p->now_ms + wait;
}

/* ---- the file preview ------------------------------------------------------------------------ */
/* Whether the cursor is on a FILE the preview should show, and if so its path in `out`. A folder, a bundle and
 * an entry that could not even be described are not: the first has a column of its own, the others nothing
 * worth reading. Only the LAST column can have a preview (a file selected in an earlier one has been walked
 * past). */
static int preview_target(RolltuiPicker* p, RolltuiStr* out) {
  Column* c = focused(p);
  const RolltuiDirEntry* e;
  if (!p->opt.preview || !c || p->focus_col + 1 != p->n) return 0;
  e = entry_at(c, c->sel);
  if (!e || e->is_dir || e->unreadable || folder_like(p, c, e)) return 0;
  join(&c->dir, &e->name, out);
  return 1;
}
/* POINTS THE PREVIEW AT WHAT THE CURSOR IS ON, and lets go of it when the cursor is on anything else. Reads
 * nothing on a frame that moved nothing: the path is compared first. Moving to another file also hands the keys
 * back to the list — they were in the file that is no longer there. */
static void sync_syntax(RolltuiPicker* p) {
  if (p->opt.no_syntax) {
    rolltui_preview_set_syntax(p->pv, NULL);
    return;
  }
  if (!p->syn) p->syn = rolltui_syntax_new_standard(NULL); /* the languages are parsed once, when the first file is looked at */
  rolltui_preview_set_syntax(p->pv, p->syn);
}
#define PREVIEW_COMMAND_MS 30
/* A COMMAND LET GO: stopped on a worker when there is one, since a program slow to stop would hold the frame. */
typedef struct Retired {
  RolltuiChild* c;
  int freed;
} Retired;
static void retired_work(void* ctx, const RolltuiJob* job) {
  Retired* r = (Retired*)ctx;
  (void)job;
  rolltui_child_free(r->c);
  r->freed = 1;
}
static void retired_done(void* ctx, int cancelled) {
  Retired* r = (Retired*)ctx;
  (void)cancelled;
  if (!r->freed) rolltui_child_free(r->c); /* the workers went before it ran */
  rolltui_mem_free(r);
}
static void retire_command(RolltuiPicker* p) {
  p->cmd_due = 0;
  if (!p->child) return;
  if (p->tasks && rolltui_tasks_workers(p->tasks) > 0) {
    Retired* r = (Retired*)rolltui_mem_alloc(sizeof *r);
    r->c = p->child;
    r->freed = 0;
    rolltui_tasks_start(p->tasks, retired_work, retired_done, r);
  } else {
    rolltui_child_free(p->child);
  }
  p->child = NULL;
}

static RolltuiPreview* preview_for(RolltuiPicker* p, const char* path, size_t len, const RolltuiPreview* keep, int* kept);

/* THE COMMAND STARTED for the file previewed, the preview's size after its own arguments. One that cannot start gives the file back to
 * the built-in preview. */
static void start_command(RolltuiPicker* p, RolltuiRect r) {
  const char** argv = (const char**)rolltui_mem_alloc((p->cmd_n + 4) * sizeof *argv);
  char wbuf[16], hbuf[16];
  RolltuiChildOptions o;
  size_t i;
  const int w = r.w > 0 ? r.w : 80, h = r.h > 0 ? r.h : 24;
  p->cmd_due = 0;
  snprintf(wbuf, sizeof wbuf, "%d", w);
  snprintf(hbuf, sizeof hbuf, "%d", h);
  for (i = 0; i < p->cmd_n; ++i) argv[i] = p->cmd[i];
  argv[p->cmd_n] = p->pv_path.p ? p->pv_path.p : "";
  argv[p->cmd_n + 1] = wbuf;
  argv[p->cmd_n + 2] = hbuf;
  argv[p->cmd_n + 3] = NULL;
  memset(&o, 0, sizeof o);
  o.argv = argv;
  o.mode = ROLLTUI_CHILD_PTY;
  o.cols = w;
  o.rows = h;
  p->child = rolltui_child_spawn(&o, NULL);
  rolltui_mem_free((void*)argv);
  if (!p->child) { /* the built-in preview, then */
    int kept = 0;
    p->host_on = 0;
    p->pv = preview_for(p, p->pv_path.p ? p->pv_path.p : "", p->pv_path.n, NULL, &kept);
  }
}

/* THE COMMAND'S TURN at a frame, in `r`: started once the cursor has rested, and what it wrote since read, as far as `r` shows. */
static void step_command(RolltuiPicker* p, RolltuiRect r) {
  if (p->cmd_due && (p->now_ms == 0 || p->now_ms >= p->cmd_asked_ms + PREVIEW_COMMAND_MS)) start_command(p, r);
  if (p->child && p->host_text) rolltui_child_pump(p->child, p->host_text, p->host_offset + (size_t)(r.h > 0 ? r.h : 24) + 1);
}

void rolltui_picker_set_preview_command(RolltuiPicker* p, const char* const* argv) {
  size_t i, n = 0;
  if (!p) return;
  retire_command(p);
  for (i = 0; i < p->cmd_n; ++i) rolltui_mem_free(p->cmd[i]);
  rolltui_mem_free(p->cmd);
  p->cmd = NULL;
  p->cmd_n = 0;
  while (argv && argv[n]) ++n;
  if (n) {
    p->cmd = (char**)rolltui_mem_alloc(n * sizeof *p->cmd);
    for (i = 0; i < n; ++i) {
      const size_t len = strlen(argv[i]);
      p->cmd[i] = (char*)rolltui_mem_alloc(len + 1);
      memcpy(p->cmd[i], argv[i], len + 1);
    }
    p->cmd_n = n;
  }
  rolltui_str_clear(&p->pv_path); /* asked again at the next frame */
  p->host_on = 0;
}

/* THE HOST'S PREVIEWER asked for the preview's path at `offset`, into its text, from empty. */
static void ask_host(RolltuiPicker* p, size_t offset) {
  if (!p->host_text) p->host_text = rolltui_ansi_new();
  rolltui_ansi_clear(p->host_text);
  p->host_bound = 0;
  p->host_offset = offset;
  p->host_on = p->host_fn(p->host_ctx, p->pv_path.p ? p->pv_path.p : "", p->pv_path.n, offset, p->host_text, &p->host_bound) != 0;
}

/* THE PREVIEW KEPT FOR `path`, else the one wanted longest ago (never `keep`) pointed at it. `*kept` 1 when it was already there. */
static RolltuiPreview* preview_for(RolltuiPicker* p, const char* path, size_t len, const RolltuiPreview* keep, int* kept) {
  size_t i, pick = 4;
  *kept = 0;
  for (i = 0; i < 4; ++i)
    if (p->cache[i] && rolltui_preview_path_is(p->cache[i], path, len)) {
      p->cache_used[i] = ++p->cache_tick;
      *kept = 1;
      return p->cache[i];
    }
  for (i = 0; i < 4; ++i) {
    if (p->cache[i] == keep && keep) continue;
    if (!p->cache[i]) {
      pick = i;
      break;
    }
    if (pick == 4 || p->cache_used[i] < p->cache_used[pick]) pick = i;
  }
  if (pick == 4) pick = 0;
  if (!p->cache[pick]) p->cache[pick] = rolltui_preview_new();
  p->cache_used[pick] = ++p->cache_tick;
  rolltui_preview_set_tasks(p->cache[pick], p->tasks);
  rolltui_preview_set_now(p->cache[pick], p->now_ms);
  if (!p->syn && !p->opt.no_syntax) p->syn = rolltui_syntax_new_standard(NULL); /* before the read, which colours what it reads */
  if (p->syn && !p->opt.no_syntax) rolltui_preview_set_syntax(p->cache[pick], p->syn);
  rolltui_preview_set_path(p->cache[pick], path, len);
  return p->cache[pick];
}

/* THE FILES EITHER SIDE OF THE CURSOR, read ahead on the workers lent: there by the time the cursor is. Never without workers, where
 * a read would hold up the frame. */
static void read_ahead(RolltuiPicker* p) {
  Column* c = focused(p);
  int d;
  if (!p->tasks || !c || p->host_fn) return;
  for (d = -1; d <= 1; d += 2) {
    const long long at = (long long)c->sel + d;
    const RolltuiDirEntry* e;
    int kept;
    if (at < 0 || (size_t)at >= c->entries.n) continue;
    e = entry_at(c, (size_t)at);
    if (!e || e->is_dir || e->unreadable || folder_like(p, c, e)) continue;
    join(&c->dir, &e->name, &p->s2);
    (void)preview_for(p, p->s2.p, p->s2.n, p->pv, &kept);
  }
}

static void sync_preview(RolltuiPicker* p) {
  if (preview_target(p, &p->s3)) {
    if (!rolltui_str_eq(&p->pv_path, p->s3.p, p->s3.n)) {
      int kept = 0;
      rolltui_str_set(&p->pv_path, p->s3.p, p->s3.n);
      p->host_on = 0;
      retire_command(p);
      if (p->host_fn) ask_host(p, 0);
      if (!p->host_on && p->cmd_n) { /* the command's: started where the cursor rests */
        if (!p->host_text) p->host_text = rolltui_ansi_new();
        rolltui_ansi_clear(p->host_text);
        p->host_bound = 0;
        p->host_offset = 0;
        p->host_on = 1;
        p->cmd_due = 1;
        p->cmd_asked_ms = p->now_ms;
      }
      if (p->host_on) { /* the host's: nothing read here, the shown preview a blank one */
        if (!p->pv) p->pv = preview_for(p, "", 0, NULL, &kept);
      } else {
        p->pv = preview_for(p, p->s3.p, p->s3.n, NULL, &kept);
        if (kept) rolltui_preview_refresh(p->pv); /* kept: read again if the file moved since */
      }
      p->pv_focus = 0;
      p->pv_zoom = 0;
      read_ahead(p);
    }
    rolltui_preview_set_tasks(p->pv, p->tasks);
    rolltui_preview_set_now(p->pv, p->now_ms);
    rolltui_preview_set_layout(p->pv, p->opt.preview_tab, p->opt.preview_wrap);
    sync_syntax(p);
    return;
  }
  p->host_on = 0;
  retire_command(p);
  if (p->pv_path.n) { /* nothing shown; what was read stays kept for the cursor coming back */
    rolltui_str_clear(&p->pv_path);
    p->pv = NULL;
  }
  p->pv_focus = 0;
  p->pv_zoom = 0;
}
static int preview_shown(const RolltuiPicker* p) { return p->pv && p->pv_path.n && p->pv_rect.w > 0; }

/* THE PREVIEW SCROLLED: `by` lines (or places of the host previewer's own), or to an end (`edge` -1 the start, 1 the end). */
static void preview_scroll(RolltuiPicker* p, long long by, int edge) {
  if (p->host_on) {
    const size_t most = p->host_bound ? p->host_bound - 1 : (rolltui_ansi_line_count(p->host_text) ? rolltui_ansi_line_count(p->host_text) - 1 : 0);
    long long at = (long long)p->host_offset + by;
    if (edge < 0) at = 0;
    if (edge > 0) at = (long long)most;
    if (at < 0) at = 0;
    if ((size_t)at > most) at = (long long)most;
    if (p->host_bound && (size_t)at != p->host_offset) ask_host(p, (size_t)at); /* a place of its own: asked again there */
    else p->host_offset = (size_t)at;
    return;
  }
  if (edge) rolltui_preview_scroll_edge(p->pv, edge > 0);
  else rolltui_preview_scroll_by(p->pv, by);
}

/* THE PREVIEW DRAWN: the host's text, else the built-in preview. */
static void draw_preview(RolltuiPicker* p, RolltuiFrame* f, RolltuiRect r, const RolltuiStyle* styles, int aw) {
  if (p->host_on) step_command(p, r);
  if (p->host_on) {
    RolltuiStyle base = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
    RolltuiStyle ground;
    rolltui_frame_ground(f, &ground);
    base.bg = ground.bg;
    rolltui_ansi_draw(p->host_text, f, r, p->host_bound ? 0 : p->host_offset, 0, 1, base, aw);
    return;
  }
  rolltui_preview_draw(p->pv, f, r, styles, aw, p->pv_focus);
}

void rolltui_picker_set_decor(RolltuiPicker* p, RolltuiPickerDecorFn fn, void* ctx) {
  if (!p) return;
  p->decor_fn = fn;
  p->decor_ctx = ctx;
}

size_t rolltui_picker_marked(const RolltuiPicker* p, RolltuiStrListRaw* out) {
  size_t i;
  if (!p) return 0;
  if (out)
    for (i = 0; i < rolltui_map_count(&p->marked); ++i) {
      size_t kl = 0;
      const char* k = rolltui_map_key_at(&p->marked, i, &kl);
      rolltui_str_list_add(out, k, kl);
    }
  return rolltui_map_count(&p->marked);
}

/* The entry under the cursor marked (`on` 1), unmarked (0) or turned over (-1). */
static void mark_selected(RolltuiPicker* p, int on) {
  RolltuiStr path;
  memset(&path, 0, sizeof path);
  selected_path(p, &path);
  if (path.n) {
    const int is = rolltui_map_get(&p->marked, path.p, path.n) != NULL;
    if (on < 0) on = !is;
    if (on) rolltui_map_put(&p->marked, path.p, path.n, (void*)1);
    else rolltui_map_take(&p->marked, path.p, path.n);
  }
  rolltui_str_free(&path);
}

void rolltui_picker_set_previewer(RolltuiPicker* p, RolltuiPreviewerFn fn, void* ctx) {
  if (!p) return;
  p->host_fn = fn;
  p->host_ctx = ctx;
  rolltui_str_clear(&p->pv_path); /* asked again at the next frame */
  p->host_on = 0;
}

/* ---- what the cursor is on, said without asking ------------------------------------------------------ */
/* The entry under the cursor, when the listing described it. An entry that could not be described has no mode to say, and
 * neither has the top column's one entry (the root, which nothing was read for). */
static const RolltuiDirEntry* info_entry(RolltuiPicker* p) {
  const RolltuiDirEntry* e = selected(p);
  return e && !e->unreadable && e->mode != 0 ? e : NULL;
}
/* The second row of the title band: the mode, then when it was last written, cut down to what fits — the time to the minute,
 * the date alone, the mode alone — and never a word cut in half. Empty when even the mode does not fit. */
static void info_line(const RolltuiDirEntry* e, int cells, char* out, size_t cap) {
  char mode[11], when[24];
  out[0] = '\0';
  if (cells < 10) return;
  rolltui_fileinfo_mode_text(e->mode, mode);
  if (cells >= 10 + 1 + 16) {
    rolltui_fileinfo_when_text(e->modified, 0, when, sizeof when);
    snprintf(out, cap, "%s %s", mode, when);
  } else if (cells >= 10 + 1 + 10) {
    rolltui_fileinfo_when_text(e->modified, 1, when, sizeof when);
    snprintf(out, cap, "%s %s", mode, when);
  } else {
    snprintf(out, cap, "%s", mode);
  }
}
/* Hands the preview its caption for the file under the cursor, or takes it away when the band has only one row. */
static void preview_caption(RolltuiPicker* p, int cells) {
  const RolltuiDirEntry* e = head_rows(p) == 2 ? info_entry(p) : NULL;
  char line[48];
  if (!p->pv) return;
  if (!e) { rolltui_preview_set_info(p->pv, "", 0); return; }
  info_line(e, cells, line, sizeof line);
  rolltui_preview_set_info(p->pv, line, strlen(line));
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
  size_t ci, i;
  if (!p) return;
  rolltui_ansi_free(p->host_text);
  rolltui_child_free(p->child);
  for (ci = 0; ci < p->cmd_n; ++ci) rolltui_mem_free(p->cmd[ci]);
  rolltui_mem_free(p->cmd);
  for (ci = 0; ci < 8; ++ci) rolltui_str_free(&p->changes[ci].path);
  for (ci = 0; ci < p->visited_n; ++ci) rolltui_str_free(&p->visited[ci]);
  rolltui_mem_free(p->visited);
  rolltui_str_free(&p->seen_entry);
  rolltui_str_free(&p->seen_folder);
  rolltui_str_free(&p->previous);
  cols_resize(p, 0);
  rolltui_mem_free(p->cols);
  for (i = 0; i < rolltui_map_count(&p->remembered); ++i) {
    RolltuiStr* s = (RolltuiStr*)rolltui_map_value_at(&p->remembered, i);
    rolltui_str_free(s);
    rolltui_mem_free(s);
  }
  rolltui_map_release(&p->remembered);
  rolltui_map_release(&p->marked);
  rolltui_str_free(&p->root);
  rolltui_str_free(&p->jump);
  rolltui_str_free(&p->event_path);
  {
    size_t ci2;
    for (ci2 = 0; ci2 < 4; ++ci2) rolltui_preview_free(p->cache[ci2]);
  }
  rolltui_syntax_free(p->syn);
  rolltui_str_free(&p->pv_path);
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
  o->highlight = 1;
  o->dividers = 1;
  o->take_folders = 0;
}
void rolltui_picker_set_options(RolltuiPicker* p, const RolltuiPickerOptions* o) {
  const int reread = o->hidden != p->opt.hidden || o->sort != p->opt.sort || o->reversed != p->opt.reversed;
  const int remeasure = o->show_size != p->opt.show_size || o->show_modified != p->opt.show_modified;
  const int previewing = o->preview != p->opt.preview || o->info != p->opt.info || o->ratio_parent != p->opt.ratio_parent ||
                         o->ratio_current != p->opt.ratio_current || o->ratio_next != p->opt.ratio_next;
  p->opt = *o;
  if (reread) rolltui_picker_reload(p);
  else if (remeasure) { size_t i; for (i = 0; i < p->n; ++i) measure_width(p, &p->cols[i]); retarget(p); }
  if (previewing) { p->anchored_once = 0; retarget(p); }
  sync_preview(p);
}
const RolltuiPickerOptions* rolltui_picker_options(const RolltuiPicker* p) { return &p->opt; }
void rolltui_picker_set_now(RolltuiPicker* p, unsigned long long now_ms) {
  size_t i;
  p->now_ms = now_ms;
  for (i = 0; i < 4; ++i)
    if (p->cache[i]) rolltui_preview_set_now(p->cache[i], now_ms);
}
void rolltui_picker_set_tasks(RolltuiPicker* p, RolltuiTasks* tasks) {
  size_t i;
  p->tasks = tasks;
  for (i = 0; i < 4; ++i) /* the kept previews too: none holds workers the host has let go */
    if (p->cache[i]) rolltui_preview_set_tasks(p->cache[i], tasks);
}

/* GO TO A PATH WITH ITS ANCESTORS SHOWING: from `/` down to `path`, each column with the next
 * component selected. The walk IS a visit — each selection goes into the memory — and a
 * component that cannot be entered ends it with one column that says why, in full. */
static void note_changes(RolltuiPicker* p);
static int walk(RolltuiPicker* p, int step);
void rolltui_picker_go_to(RolltuiPicker* p, const char* path, size_t len) {
  size_t at;
  RolltuiStr full;
  const int lazy = p->lazy;
  p->lazy = 0;
  memset(&full, 0, sizeof full);
  if (len == 0 || root_len(path, len) == 0) {
    /* relative: from the working directory, which is where "." means */
    char cwd[4096];
    if (rolltui_os_getcwd(cwd, sizeof cwd) == 0) rolltui_str_set(&full, cwd, strlen(cwd));
    else rolltui_str_set(&full, "/", 1);
    if (len && !(len == 1 && path[0] == '.')) { rolltui_str_append(&full, "/", 1); rolltui_str_append(&full, path, len); }
  } else {
    rolltui_str_set(&full, path, len);
  }
#if defined(_WIN32)
  {
    size_t k;
    for (k = 0; k < full.n; ++k)
      if (full.p[k] == '\\') full.p[k] = '/';
    if (root_len(full.p, full.n) == 2) { /* `C:` alone: its root is `C:/` */
      RolltuiStr fixed;
      memset(&fixed, 0, sizeof fixed);
      rolltui_str_set(&fixed, full.p, 2);
      rolltui_str_append(&fixed, "/", 1);
      rolltui_str_append(&fixed, full.p + 2, full.n - 2);
      rolltui_str_free(&full);
      full = fixed;
    }
  }
#endif
  while (full.n > root_len(full.p, full.n) && full.p[full.n - 1] == '/') full.p[--full.n] = '\0';
  set_root(p, full.p, root_len(full.p, full.n) ? root_len(full.p, full.n) : 1);
  at = root_len(full.p, full.n) ? root_len(full.p, full.n) : 1;
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
  p->lazy = lazy;
}

/* Re-read every column in place, keeping each selection BY NAME, and what is shown right of a column consistent with
 * what that column now selects — after an option changed (sort, dotfiles) a selected folder may have gone from the list. */
void rolltui_picker_reload(RolltuiPicker* p) {
  size_t ci;
  ++p->watch_pass;
  if (p->n) measure_width(p, &p->cols[0]);
  for (ci = 1; ci < p->n; ++ci) {
    if (p->cols[ci].load) drop_load(&p->cols[ci]); /* read again below, with the options now set */
    if (p->cols[ci].read_pass == p->watch_pass) continue;
    column_refresh(p, ci);
    chain_repair(p, ci);
  }
  if (p->drag_col >= (long long)p->n) p->drag_col = -1;
  retarget(p);
  sync_preview(p);
}

void rolltui_picker_layout(RolltuiPicker* p, RolltuiRect inner) {
  const int was_h = p->inner.h;
  const int resized = inner.h != was_h;
  size_t i;
  p->inner = inner;
  watch_tick(p);
  /* A NEW HEIGHT brings every selection back on screen — a column opened before any layout had
   * no rows to clamp against, and a resize can push a selection out. The same height keeps each
   * column where the wheel or its thumb left it. */
  for (i = 0; i < p->n; ++i) {
    if (resized) clamp_scroll(p, &p->cols[i]); else clamp_top(p, &p->cols[i]);
    if (p->cols[i].error.n) measure_width(p, &p->cols[i]); /* sized to the window, known only here */
  }
  retarget(p);
  advance(p);
  sync_preview(p);
}

/* ---- the fade at the left edge ------------------------------------------------------------ */
/* A column partly off the left edge is a column the eye has left behind, and cutting it dead at
 * the border reads as a tear: its cells are pulled toward the panel's background, most at the
 * border and none a dozen cells in, on a cosine that bottoms one step above nothing. ONLY IN
 * 24-BIT COLOUR: a blend is a colour the theme did not name. */
static RolltuiStyle faded(RolltuiStyle st, RolltuiStyleColorRaw ground, double keep) {
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
  RolltuiStyleColorRaw ground;
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

void rolltui_picker_set_hover(RolltuiPicker* p, int on, int x, int y) {
  p->hover_on = on != 0;
  p->hover_x = x;
  p->hover_y = y;
}

void rolltui_picker_draw(RolltuiPicker* p, RolltuiFrame* f, const RolltuiStyle* styles,
                         const RolltuiScrollbarGlyphs* glyphs, int ambiguous_wide, int focused) {
  Draw d;
  size_t ci;
  p->aw = ambiguous_wide;
  int first_drawn = 0;
  const RolltuiStyle text = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  const RolltuiStyle dim = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT_MUTED);
  /* THE HEAD ROW IS A TITLE ROW: one background across every column, the panel's, so the row
   * that does not scroll reads as the row that names the columns rather than as their first line. */
  RolltuiStyle head = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_LABEL);
  const RolltuiStyle here =
      *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, focused ? ROLLTUI_ROLE_MENU_SELECTED : ROLLTUI_ROLE_SELECTION);
  const RolltuiStyle trail = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_SELECTION);
  const RolltuiStyle hover = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_HOVER);
  const RolltuiStyle err_style = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_ERROR);
  const RolltuiStyle border = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BORDER);
  const RolltuiStyle bar_role = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_SCROLLBAR);
  const int show_highlight = p->opt.highlight;
  const int hr = head_rows(p);
  const unsigned char shape = glyphs && !ambiguous_wide ? glyphs->cursor : ROLLTUI_CURSOR_FILL;
  p->faded_cells = 0;
  d.p = p;
  d.f = f;
  d.ds = p->draw;
  d.r = p->inner;
  d.ground = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND)->bg;
  head.bg = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_PANEL_BACKGROUND)->bg;
  if (d.r.w <= 0 || d.r.h <= 0) return;
  d.clipped = 0;
  /* ZOOMED: the preview has the whole picker, and the columns are not drawn at all */
  if (p->pv_zoom && p->pv && p->pv_path.n) {
    preview_caption(p, d.r.w - 2);
    draw_preview(p, f, d.r, styles, ambiguous_wide);
    p->pv_rect = d.r;
    return;
  }
  /* THE TITLE ROW'S BAND, laid column by column below (so a column clipped at the left edge
   * fades, band and name alike); what is right of the last column is laid here, unclipped. */
  {
    int band_from = d.r.x;
    if (p->n) { const size_t last = p->n - 1; band_from = imax(d.r.x, column_x(p, last) + shown_width(p, last) + 1); }
    d.clipped = 0;
    if (band_from < d.r.x + d.r.w) {
      int b;
      for (b = 0; b < hr; ++b) fill_row(&d, band_from, d.r.y + b, d.r.x + d.r.w - band_from, head);
    }
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
    {
      const int bx = imax(x, d.r.x);
      int b;
      for (b = 0; b < hr; ++b) fill_row(&d, bx, d.r.y + b, imin(x + sw + 1, d.r.x + d.r.w) - bx, head);
    }
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
          if (y < d.r.y + hr) { RolltuiStyle on_band = line; on_band.bg = head.bg; at_y = d.clipped ? faded(on_band, d.ground, keep_at(&d.r, dx)) : on_band; }
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
            const int ty = d.r.y + hr + t.offset + i;
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
    else if (is_root(c->dir.p, c->dir.n)) fit_into(p, c->dir.p, c->dir.n, cw - 1, &p->s1);
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
    /* THE SECOND ROW OF THE BAND, at the head of the column that lists what the cursor is on: that folder's own mode and time,
     * which is what the eye is asking about when it rests on a folder. */
    if (hr == 2 && ci == p->focus_col + 1 && selected_is_folder(p)) {
      const RolltuiDirEntry* e = info_entry(p);
      if (e) {
        char line[48];
        RolltuiStyle cap = dim;
        cap.bg = head.bg;
        info_line(e, cw - 1, line, sizeof line);
        if (line[0]) put_clipped(&d, x + 1, d.r.y + 1, line, strlen(line), cap, cw - 1);
      }
    }
    for (row = 0; row < rows; ++row) {
      const size_t i = c->top + (size_t)row;
      const int y = d.r.y + hr + row;
      const RolltuiDirEntry* e;
      int is_sel, is_focus_col, is_trail, is_hover;
      RolltuiStyle st;
      if (y >= d.r.y + d.r.h) break;
      if (i >= c->entries.n) break;
      e = &c->entries.v[i];
      is_sel = i == c->sel;
      is_focus_col = ci == p->focus_col;
      /* A TRAIL ROW — the selection in a column LEFT of the focus, the path we came down. The
       * column to the RIGHT is a preview the cursor has not entered and draws no selection.
       * `highlight` draws both as a block of background; independently, the host's own glow
       * effect (marked below regardless) may also be lit. A host is expected to keep at least
       * one of the two on, or neither row reads as the cursor's. */
      /* the folder the cursor is on, to the right, marks the entry last used in it, as the path behind is marked */
      is_trail = is_sel && (ci < p->focus_col || (ci == p->focus_col + 1 && last_used(p, c, e)));
      /* THE ENTRY UNDER THE POINTER is the one a press would select, by `handle_inner`'s own test: this column's cells, this row.
       * The cursor's row keeps its own look; a trail row under the pointer shows the hover until the pointer leaves. */
      is_hover = p->hover_on && !(is_sel && is_focus_col) && p->hover_y == y && p->hover_x >= x && p->hover_x < x + sw &&
                 p->hover_x >= d.r.x && !(preview_shown(p) && p->hover_x >= p->pv_rect.x && p->hover_x < p->pv_rect.x + p->pv_rect.w &&
                                          p->hover_y >= p->pv_rect.y && p->hover_y < p->pv_rect.y + p->pv_rect.h);
      RolltuiPickerDecor decor;
      int is_marked = 0, cell_w = 0;
      st = is_hover ? hover
                    : is_sel && is_focus_col && show_highlight ? here : is_trail && show_highlight ? trail : (e->is_dir ? text : (e->unreadable ? dim : text));
      if (is_sel && is_focus_col && show_highlight && !is_hover && shape != ROLLTUI_CURSOR_FILL)
        rolltui_cursor_text_style(&here, d.ground, shape, &st);
      /* A directory is marked with a trailing chevron rather than a colour, so the shape
       * survives `mono` and a colour-blind reader alike. */
      { /* AN ENTRY'S OWN MARKS: the host's decor, and whether it is marked */
        RolltuiStr full;
        memset(&decor, 0, sizeof decor);
        memset(&full, 0, sizeof full);
        if (p->decor_fn || (p->opt.marks && rolltui_map_count(&p->marked))) join(&c->dir, &e->name, &full);
        if (p->decor_fn) p->decor_fn(p->decor_ctx, full.p ? full.p : "", full.n, e->is_dir, &decor);
        is_marked = p->opt.marks && full.n && rolltui_map_get(&p->marked, full.p, full.n) != NULL;
        rolltui_str_free(&full);
        decor.cell[sizeof decor.cell - 1] = 0;
        decor.gutter[sizeof decor.gutter - 1] = 0;
        cell_w = decor.cell[0] ? width_of(p, decor.cell, strlen(decor.cell)) + 1 : 0;
        if (cell_w > cw / 2) cell_w = 0; /* a name keeps most of its column */
        if (decor.role && !is_sel && !is_hover) {
          st = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, decor.role);
          st.bg = d.ground;
        }
        if (is_marked) { /* bold in the mark's colour, so it reads with no colour too */
          st.bold = 1;
          if (!(is_sel && is_focus_col)) st.fg = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_ACCENT_1)->fg;
        }
      }
      rolltui_str_set(&p->s2, e->name.p ? e->name.p : "", e->name.n);
      if (e->is_dir && !is_bundle(e)) rolltui_str_append(&p->s2, " \xE2\x80\xBA", 4);
      fit_into(p, p->s2.p ? p->s2.p : "", p->s2.n, cw - 1 - extras_width(p) - cell_w - (shape != ROLLTUI_CURSOR_FILL), &p->s1);
      if (is_sel && (is_focus_col || is_trail)) {
        const int fx = imax(x, d.r.x);
        const unsigned long long opened_age = p->now_ms >= p->opened_since_ms ? p->now_ms - p->opened_since_ms : 0;
        const int opened_now = ci == p->opened_col && i == p->opened_sel && opened_age < OPENED_MS;
        size_t name_n = p->s1.n;
        if (show_highlight || is_hover) fill_row(&d, fx, y, x + cw - fx, st);
        if (is_focus_col && focused) rolltui_frame_set_cursor(f, x + 1, y, 0); /* hidden, where a screen reader looks */
        if (is_focus_col && show_highlight && !is_hover && shape != ROLLTUI_CURSOR_FILL && fx == x) {
          const RolltuiRect band = {x, y, cw, 1};
          rolltui_frame_cursor_ends(f, band, here, d.ground, shape);
        }
        if (e->is_dir && name_n >= 4 && memcmp(p->s1.p + name_n - 4, " \xE2\x80\xBA", 4) == 0) name_n -= 4;
        mark_words(&d, p->s1.p ? p->s1.p : "", name_n, x, cw, y,
                   is_focus_col ? ROLLTUI_EFFECT_STATE_PICKER_CURSOR : ROLLTUI_EFFECT_STATE_PICKER_TRAIL,
                   is_focus_col ? p->cursor_since_ms : 0, opened_now, p->opened_since_ms);
      }
      if (is_hover && !(is_sel && (is_focus_col || is_trail))) /* those two rows were filled above */
        fill_row(&d, imax(x, d.r.x), y, x + cw - imax(x, d.r.x), st);
      put_clipped(&d, x + 1, y, p->s1.p ? p->s1.p : "", p->s1.n, st, cw - 1 - extras_width(p) - cell_w);
      if (is_marked || decor.gutter[0]) { /* the cell before the name: the host's glyph, else the mark's ▌ */
        RolltuiStyle gs = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, decor.gutter[0] && decor.gutter_role ? decor.gutter_role : ROLLTUI_ROLE_ACCENT_1);
        const char* g = decor.gutter[0] ? decor.gutter : ambiguous_wide ? "*" : "\xE2\x96\x8C";
        gs.bg = st.bg.kind ? st.bg : d.ground;
        if (x >= d.r.x) rolltui_frame_put_text(f, d.ds, x, y, g, strlen(g), gs, 1, ambiguous_wide, 0);
      }
      if (cell_w) { /* the host's cell, right of the name and before the facts */
        RolltuiStyle ks = decor.cell_role ? *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, decor.cell_role) : dim;
        ks.bg = st.bg.kind ? st.bg : d.ground;
        put_clipped(&d, x + cw - extras_width(p) - cell_w + 1, y, decor.cell, strlen(decor.cell), ks, cell_w - 1);
      }
      if (extras_width(p) > 0) {
        /* THE FACTS, right-aligned at the column's edge, muted unless the row is the cursor's:
         * a folder has no size to say; a "-" keeps the column's shape. */
        char buf[32];
        int ex = x + cw - extras_width(p);
        /* On a highlighted row — the cursor's, or a trail selection — the facts sit on the
         * row's own ground: muted lettering, the highlight's background, so the block runs the
         * row's whole width. */
        RolltuiStyle fs = is_sel && is_focus_col && show_highlight ? st : dim;
        if ((is_trail && show_highlight) || is_hover) fs.bg = st.bg;
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
    if (c->load && c->entries.n == 0 && d.r.h > 1) {
      if (p->now_ms >= c->load_since_ms + FOLDER_LOADING_MS) { /* a read that is taking a while says so */
        fit_into(p, "loading" "\xE2\x80\xA6", sizeof "loading" "\xE2\x80\xA6" - 1, cw - 1, &p->s1);
        put_clipped(&d, x + 1, d.r.y + hr, p->s1.p ? p->s1.p : "", p->s1.n, dim, cw - 1);
      }
    } else if (c->entries.n == 0 && d.r.h > 1) {
      /* A DIRECTORY THAT COULD NOT BE OPENED MUST NOT LOOK LIKE AN EMPTY ONE, and an error is not
       * a filename: it gets the rest of the panel, which is space no name needed. */
      const int failed = c->error.n != 0;
      const int room = failed ? (d.r.x + d.r.w - (x + 1)) : (cw - 1);
      if (failed) fit_into(p, c->error.p, c->error.n, room, &p->s1);
      else fit_into(p, "(empty)", 7, room, &p->s1);
      put_clipped(&d, x + 1, d.r.y + hr, p->s1.p ? p->s1.p : "", p->s1.n, failed ? err_style : dim, room);
    }
  }
  /* THE FILE PREVIEW takes the slot after the focused column — what is left of the width once the column and its
   * divider are past — and is drawn over the title band's tail. */
  p->pv_rect.w = 0;
  if (p->pv && p->pv_path.n && p->n && p->focus_col + 1 == p->n) {
    const int x0 = column_x(p, p->focus_col) + shown_width(p, p->focus_col) + 1;
    const int w = d.r.x + d.r.w - x0;
    if (w >= 12 && x0 >= d.r.x) {
      RolltuiRect r;
      r.x = x0;
      r.y = d.r.y;
      r.w = w;
      r.h = d.r.h;
      preview_caption(p, r.w - 2);
      draw_preview(p, f, r, styles, ambiguous_wide);
      p->pv_rect = r;
    }
  }
  /* NO PREVIEW ON SCREEN AND THE CURSOR ON A FILE: the slot right of it is a small pane of its own, a column that holds no
   * list — the file's name in its head with its size beside it (unless a size column says it already), and under it the same
   * second row a folder's column has. */
  if (hr == 2 && p->pv_rect.w == 0 && p->n && p->focus_col + 1 == p->n && !selected_is_folder(p)) {
    const RolltuiDirEntry* e = info_entry(p);
    const int x0 = column_x(p, p->focus_col) + shown_width(p, p->focus_col) + 1;
    const int w = imin(MAX_COLUMN_WIDTH, d.r.x + d.r.w - x0);
    if (e && w >= 12 && x0 >= d.r.x) {
      char line[48], size[24];
      RolltuiStyle cap = dim;
      int size_w = 0, room = w - 1;
      cap.bg = head.bg;
      d.clipped = 0;
      size[0] = '\0';
      if (!e->is_dir && !show_size(p)) {
        rolltui_fileinfo_size_text(e->size, size, sizeof size);
        size_w = (int)strlen(size);
        if (1 + width_of(p, e->name.p ? e->name.p : "", e->name.n) + 2 + size_w + 1 > w) size_w = 0; /* only when whole */
      }
      if (size_w) {
        room = w - 1 - (size_w + 2);
        put_clipped(&d, x0 + w - 1 - size_w, d.r.y, size, (size_t)size_w, head, size_w);
      }
      fit_into(p, e->name.p ? e->name.p : "", e->name.n, room, &p->s1);
      put_clipped(&d, x0 + 1, d.r.y, p->s1.p ? p->s1.p : "", p->s1.n, head, room);
      info_line(e, w - 1, line, sizeof line);
      if (line[0]) put_clipped(&d, x0 + 1, d.r.y + 1, line, strlen(line), cap, w - 1);
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
  if (!p->opt.dividers || my < p->inner.y + head_rows(p) || my >= p->inner.y + head_rows(p) + rows_visible(p)) return -1;
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

/* WHETHER A NAME STARTS WITH `pre`, case and accents aside: the name itself, or for a dotfile what follows the dot, so `g` reaches
 * `.gitignore` among the g's, unless `pre` starts with the dot, which goes through the dotfiles themselves. */
static int name_starts(const RolltuiStr* name, const char* pre, size_t pn) {
  size_t at = 0, len = 0, skip = 0;
  if (name->n == 0 || !name->p || pn == 0) return 0;
  if (pre[0] != '.' && name->p[0] == '.' && name->n > 1) skip = 1;
  return rolltui_text_find(name->p + skip, name->n - skip, pre, pn, 0, &at, &len) && at == 0;
}

#define JUMP_MS 1000 /* letters typed within this of each other are one name's start */

/* TYPE TO JUMP. Letters typed in quick succession are the start of a name, and the cursor goes to the first name from here that
 * begins with them; a letter typed alone, or the same letter again, goes through the names beginning with it, Shift backwards. */
static int type_to_jump(RolltuiPicker* p, RolltuiCodepoint ch, int reverse) {
  Column* c = focused(p);
  size_t n, k, start, i, cps = 0;
  char buf[8];
  int same = 1, on_one;
  const RolltuiCodepoint want = rolltui_u_fold(ch);
  const size_t bl = rolltui_u_append_utf8(want, buf);
  if (!c || (n = c->entries.n) == 0) return 1;
  if (!(p->now_ms && p->jump.n && p->now_ms - p->jump_ms <= JUMP_MS && !reverse)) rolltui_str_clear(&p->jump);
  rolltui_str_append(&p->jump, buf, bl);
  p->jump_ms = p->now_ms;
  for (i = 0; i < p->jump.n; ++i) { /* one letter, or the same letter over again */
    if ((p->jump.p[i] & 0xC0) != 0x80) ++cps;
    if (i + bl <= p->jump.n && (i % bl) == 0 && memcmp(p->jump.p + i, buf, bl) != 0) same = 0;
  }
  if (p->jump.n % bl) same = 0;
  if (cps > 1 && !same) { /* a name's start: the first from here that begins with it */
    for (k = 0; k < n; ++k) {
      i = (c->sel + k) % n;
      if (name_starts(&c->entries.v[i].name, p->jump.p, p->jump.n)) {
        select_row(p, i);
        return 1;
      }
    }
    return 1;
  }
  on_one = c->sel < n && name_starts(&c->entries.v[c->sel].name, buf, bl);
  if (!reverse) {
    start = on_one ? c->sel + 1 : 0;
    for (k = 0; k < n; ++k) { i = (start + k) % n; if (name_starts(&c->entries.v[i].name, buf, bl)) { select_row(p, i); return 1; } }
  } else {
    start = on_one ? (c->sel + n - 1) % n : n - 1;
    for (k = 0; k < n; ++k) { i = (start + n - k) % n; if (name_starts(&c->entries.v[i].name, buf, bl)) { select_row(p, i); return 1; } }
  }
  return 1;
}

static int action_is(const char* a, size_t n, const char* name) {
  return name && strlen(name) == n && memcmp(a, name, n) == 0;
}

static int handle_inner(RolltuiPicker* p, const RolltuiEvent* e, const RolltuiBindings* b,
                        const RolltuiPickerActions* a) {
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent* m = &e->mouse;
    const int k = m->kind;
    long long dc;
    size_t ci;
    /* THE GEOMETRY A POINTER IS TESTED AGAINST IS THE ONE ON SCREEN NOW: a slide the last key
     * began has moved on since the last frame. */
    advance(p);
    /* THE PREVIEW IS A PLACE THE POINTER CAN BE: the wheel over it scrolls the file, a press in it puts the keys there. */
    if (preview_shown(p) && m->x >= p->pv_rect.x && m->x < p->pv_rect.x + p->pv_rect.w && m->y >= p->pv_rect.y &&
        m->y < p->pv_rect.y + p->pv_rect.h) {
      if (k == 4) { preview_scroll(p, -3, 0); return 1; }
      if (k == 5) { preview_scroll(p, 3, 0); return 1; }
      if (k == 6 || k == 7) {
        if (!p->host_on) rolltui_preview_scroll_sideways(p->pv, k == 6 ? -4 : 4, p->aw);
        return 1;
      }
      if ((k == 0 || k == 8) && m->button == 1) { p->pv_focus = 1; return 1; }
      if (k == 1 || k == 2) return 1;
    }
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
        scroll_column(p, ci, rolltui_scroll_first_for_cell(&ex, rows_visible(p), m->y - (p->inner.y + head_rows(p)) - p->drag_grab));
      }
      return 1;
    }
    if (k == 1 /* Release */) { const int was = p->drag_col >= 0; p->drag_col = -1; return was; }
    /* ONLY THE LEFT BUTTON CHOOSES, as in every widget: a right, middle, back or forward press is the host's. */
    if ((k != 0 /* Press */ && k != 8 /* DoubleClick */) || m->button != 1) return 0;
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
      cell = m->y - (p->inner.y + head_rows(p));
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
        const int row = m->y - p->inner.y - head_rows(p);
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
    /* THE KEYS ARE IN THE PREVIEW: the moving keys scroll the file instead of the list, and Left or Escape are the
     * way back to the list (Escape goes back one place, it does not leave the picker). Anything else is what it
     * always is — Enter still takes the file — and typing a letter is the list's again. */
    if (act && len && preview_shown(p) && !p->host_on && (action_is(act, len, a->preview_left) || action_is(act, len, a->preview_right)))
      return rolltui_preview_scroll_sideways(p->pv, action_is(act, len, a->preview_left) ? -8 : 8, p->aw) || 1;
    if (p->pv_focus && preview_shown(p)) {
      if (act && len) {
        const long long page = p->host_on && p->host_bound ? 1 : imax(1, p->pv_rect.h - 1); /* a place of its own pages one at a time */
        if (action_is(act, len, a->up)) { preview_scroll(p, -1, 0); return 1; }
        if (action_is(act, len, a->down)) { preview_scroll(p, 1, 0); return 1; }
        if (action_is(act, len, a->page_up)) {
          if (p->host_on) preview_scroll(p, -page, 0);
          else rolltui_preview_scroll_page(p->pv, -1);
          return 1;
        }
        if (action_is(act, len, a->page_down)) {
          if (p->host_on) preview_scroll(p, page, 0);
          else rolltui_preview_scroll_page(p->pv, 1);
          return 1;
        }
        if (action_is(act, len, a->first)) { preview_scroll(p, 0, -1); return 1; }
        if (action_is(act, len, a->last)) { preview_scroll(p, 0, 1); return 1; }
        /* RIGHT GOES DEEPER, LEFT AND ESCAPE STEP BACK, one place at a time: the list, the file beside it, the file
         * with the whole picker */
        if (action_is(act, len, a->out) || action_is(act, len, a->cancel)) {
          if (p->pv_zoom) p->pv_zoom = 0;
          else p->pv_focus = 0;
          return 1;
        }
        if (action_is(act, len, a->into)) { p->pv_zoom = 1; return 1; }
      } else if (e->key.key == ROLLTUI_KEY_CHAR && !e->key.ctrl && !e->key.alt && e->key.ch >= 0x20 && e->key.ch != 0x7f) {
        p->pv_focus = 0;
        p->pv_zoom = 0;
      }
    }
    if ((!act || len == 0) && e->key.key == ROLLTUI_KEY_CHAR && !e->key.ctrl && !e->key.alt && e->key.ch >= 0x20 && e->key.ch != 0x7f)
      return type_to_jump(p, e->key.ch, e->key.shift || (e->key.ch >= 'A' && e->key.ch <= 'Z'));
    if (!act || len == 0) return 0;
    if (action_is(act, len, a->up)) move_by(p, -1);
    else if (action_is(act, len, a->down)) move_by(p, 1);
    else if (action_is(act, len, a->page_up)) move_by(p, -page);
    else if (action_is(act, len, a->page_down)) move_by(p, page);
    else if (p->opt.marks && action_is(act, len, a->mark)) { mark_selected(p, -1); move_by(p, 1); }
    else if (p->opt.marks && (action_is(act, len, a->mark_up) || action_is(act, len, a->mark_down))) {
      mark_selected(p, 1);
      move_by(p, action_is(act, len, a->mark_up) ? -1 : 1);
      mark_selected(p, 1);
    } else if (p->opt.marks && action_is(act, len, a->unmark_all)) {
      if (!rolltui_map_count(&p->marked)) return 0;
      rolltui_map_clear(&p->marked);
    }
    else if (action_is(act, len, a->half_up)) move_by(p, -imax(1, rows_visible(p) / 2));
    else if (action_is(act, len, a->half_down)) move_by(p, imax(1, rows_visible(p) / 2));
    else if (action_is(act, len, a->back) || action_is(act, len, a->forward)) {
      if (!walk(p, action_is(act, len, a->back) ? -1 : 1)) return 0;
    } else if (action_is(act, len, a->previous)) {
      RolltuiStr to;
      if (!p->previous.n) return 0;
      memset(&to, 0, sizeof to);
      rolltui_str_set(&to, p->previous.p, p->previous.n);
      rolltui_picker_go_to(p, to.p, to.n);
      rolltui_str_free(&to);
    }
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

/* ---- what changed, and the folders visited --------------------------------------------------- */

static void push_change(RolltuiPicker* p, unsigned char kind, const RolltuiStr* path) {
  size_t at;
  if (p->change_n == 8) { /* full: the oldest goes */
    p->change_head = (p->change_head + 1) % 8;
    --p->change_n;
  }
  at = (p->change_head + p->change_n) % 8;
  p->changes[at].kind = kind;
  rolltui_str_set(&p->changes[at].path, path->p ? path->p : "", path->n);
  ++p->change_n;
}

/* A FOLDER VISITED, after any the picker had gone back past: going forward from here means this one. */
static void visit(RolltuiPicker* p, const RolltuiStr* dir) {
  size_t k;
  if (p->walking || !dir->n) return;
  if (p->visited_n && rolltui_str_eq(&p->visited[p->visited_at], dir->p, dir->n)) return;
  for (k = p->visited_at + 1; k < p->visited_n; ++k) rolltui_str_free(&p->visited[k]); /* the ones gone back past */
  p->visited_n = p->visited_n ? p->visited_at + 1 : 0;
  if (p->visited_n == 64) { /* the oldest goes */
    rolltui_str_free(&p->visited[0]);
    memmove(p->visited, p->visited + 1, 63 * sizeof *p->visited);
    p->visited_n = 63;
  }
  p->visited = (RolltuiStr*)rolltui_grow(p->visited, &p->visited_cap, p->visited_n + 1, sizeof *p->visited);
  memset(&p->visited[p->visited_n], 0, sizeof p->visited[p->visited_n]);
  rolltui_str_set(&p->visited[p->visited_n], dir->p, dir->n);
  p->visited_at = p->visited_n++;
}

/* Back (`step` -1) or forward (1) through the folders visited. 0 when there is none that way. */
static int walk(RolltuiPicker* p, int step) {
  RolltuiStr to;
  if (!p->visited_n) return 0;
  if (step < 0 && p->visited_at == 0) return 0;
  if (step > 0 && p->visited_at + 1 >= p->visited_n) return 0;
  p->visited_at = step < 0 ? p->visited_at - 1 : p->visited_at + 1;
  memset(&to, 0, sizeof to);
  rolltui_str_set(&to, p->visited[p->visited_at].p, p->visited[p->visited_at].n);
  p->walking = 1;
  rolltui_picker_go_to(p, to.p, to.n);
  p->walking = 0;
  rolltui_str_free(&to);
  return 1;
}

/* WHAT CHANGED since it was last told: the focused folder, and the entry the cursor is on. The folder left is `previous`. */
static void note_changes(RolltuiPicker* p) {
  const Column* c = focused(p);
  RolltuiStr entry;
  if (c && !rolltui_str_eq(&p->seen_folder, c->dir.p ? c->dir.p : "", c->dir.n)) {
    if (p->seen_folder.n) rolltui_str_set(&p->previous, p->seen_folder.p, p->seen_folder.n);
    rolltui_str_set(&p->seen_folder, c->dir.p ? c->dir.p : "", c->dir.n);
    visit(p, &p->seen_folder);
    if (p->opt.report_changes) push_change(p, ROLLTUI_PICKER_EVENT_FOLDER, &p->seen_folder);
  }
  memset(&entry, 0, sizeof entry);
  selected_path(p, &entry);
  if (!rolltui_str_eq(&p->seen_entry, entry.p ? entry.p : "", entry.n)) {
    rolltui_str_set(&p->seen_entry, entry.p ? entry.p : "", entry.n);
    if (entry.n && p->opt.report_changes) push_change(p, ROLLTUI_PICKER_EVENT_MOVED, &entry);
  }
  rolltui_str_free(&entry);
}

int rolltui_picker_handle(RolltuiPicker* p, const RolltuiEvent* e, const RolltuiBindings* b,
                          const RolltuiPickerActions* a) {
  int consumed;
  p->lazy = 1;
  consumed = handle_inner(p, e, b, a);
  p->lazy = 0;
  sync_preview(p); /* a key that moved the cursor has changed what the preview is of */
  note_changes(p);
  return consumed;
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
  if (p->event_kind == ROLLTUI_PICKER_EVENT_NONE && p->change_n) { /* what changed, oldest first, once the outcomes are taken */
    const size_t i = p->change_head;
    if (out) {
      out->kind = p->changes[i].kind;
      out->inverse = 0;
      rolltui_str_set(&out->path, p->changes[i].path.p ? p->changes[i].path.p : "", p->changes[i].path.n);
    }
    p->change_head = (p->change_head + 1) % 8;
    --p->change_n;
    return 1;
  }
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
  out->wake_ms = p->opt.no_watch || p->now_ms == 0 || p->n == 0 ? 0
                 : p->watch_next_ms > p->now_ms ? (int)(p->watch_next_ms - p->now_ms < WATCH_MAX_MS ? p->watch_next_ms - p->now_ms : WATCH_MAX_MS)
                                                : 1;
  {
    const int pv_wake = p->pv ? rolltui_preview_wake_ms(p->pv) : 0;
    size_t ci;
    if (pv_wake > 0 && (out->wake_ms == 0 || pv_wake < out->wake_ms)) out->wake_ms = pv_wake;
    if (p->cmd_due && p->now_ms) { /* the command starts when the cursor has rested */
      const int due = p->cmd_asked_ms + PREVIEW_COMMAND_MS > p->now_ms ? (int)(p->cmd_asked_ms + PREVIEW_COMMAND_MS - p->now_ms) : 1;
      if (out->wake_ms == 0 || due < out->wake_ms) out->wake_ms = due;
    }
    if (p->child && rolltui_child_wait_ms(p->child) > 0) { /* and what it writes is drawn as it arrives */
      const int due = rolltui_child_wait_ms(p->child);
      if (out->wake_ms == 0 || due < out->wake_ms) out->wake_ms = due;
    }
    for (ci = 0; ci < p->n; ++ci) /* "loading…" is drawn when it is due */
      if (p->cols[ci].load && p->now_ms && p->cols[ci].load_since_ms + FOLDER_LOADING_MS > p->now_ms) {
        const int due = (int)(p->cols[ci].load_since_ms + FOLDER_LOADING_MS - p->now_ms);
        if (out->wake_ms == 0 || due < out->wake_ms) out->wake_ms = due;
      }
  }
  out->preview = (unsigned char)(!preview_shown(p) ? 0 : p->pv_zoom ? 3 : p->pv_focus ? 2 : 1);
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
  const Column* c;
  if (preview_shown(p) && p->host_on) { /* a host's or a command's text: its lines, or the previewer's own places */
    out->first = p->host_offset;
    out->visible = p->host_bound ? 1 : (size_t)(p->pv_rect.h > 0 ? p->pv_rect.h : 1);
    out->total = p->host_bound ? p->host_bound : rolltui_ansi_line_count(p->host_text);
    return 1;
  }
  if (preview_shown(p)) return rolltui_preview_scroll_extent(p->pv, out); /* the file on screen is what the bar is of */
  c = bar_column((RolltuiPicker*)p);
  if (!c) return 0;
  out->first = c->top;
  out->visible = (size_t)rows_visible(p);
  out->total = c->entries.n;
  return 1;
}
int rolltui_picker_scroll_to(RolltuiPicker* p, size_t first) {
  Column* c;
  if (preview_shown(p) && p->host_on) {
    const size_t before = p->host_offset;
    preview_scroll(p, (long long)first - (long long)p->host_offset, 0);
    return p->host_offset != before;
  }
  if (preview_shown(p)) return rolltui_preview_scroll_to(p->pv, first);
  c = bar_column(p);
  if (!c) return 0;
  scroll_column(p, (size_t)(c - p->cols), first);
  return 1;
}
int rolltui_picker_scrolling(const RolltuiPicker* p) { return p->scrolling; }
int rolltui_picker_preview_focused(const RolltuiPicker* p) { return preview_shown(p) && p->pv_focus; }
size_t rolltui_picker_faded_cells(const RolltuiPicker* p) { return p->faded_cells; }
