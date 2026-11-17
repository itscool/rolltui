/* rolltui/c/rolltui_widget_list.c — A LIST THE HOST FEEDS BY INDEX, which is also a table and a tree.
 *
 * THE HOST HOLDS THE DATA. The list asks how many rows there are and, for a row, its key, its cells, its role and its depth. A
 * plain list asks only for the rows it draws, so a hundred thousand rows cost what a screenful does. Sorting, filtering and a tree
 * need every row once, when the data or the question changes (`rolltui_windows_list_refresh`, or the count moving), never per frame.
 *
 * A ROW IS ITS KEY. The cursor, the marks and which tree nodes are open are kept by key, so they follow a row through a refresh, a
 * sort or a filter; the cursor keeps its place on screen when its row moved.
 *
 * A TREE is rows in tree order, each with its depth: a parent before its children. A node opens and closes; a filter keeps the
 * parents of what it matches, and opens them. Guide lines draw the shape: a row's ancestor that has a later sibling draws a bar.
 */
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_widget_list.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"

#define MAX_COLS 16
#define MAX_DEPTH 31
#define EVENT_CAP 32
#define COL_GAP 2   /* cells between two columns */
#define FIRST_MIN 8 /* the first column's least, before a column on the right goes */
#define ELLIPSIS "\xe2\x80\xa6"

typedef struct ListEvent {
  unsigned char kind;
  RolltuiStr key, before;
} ListEvent;

#define EDGE_MS 50     /* a drag held past the view's edge scrolls a row this often */
#define LOADING_MS 100 /* "loading" waits this long: rows that come at once never flash it */

struct RolltuiList {
  RolltuiListCountFn count_fn;
  RolltuiListRowFn row_fn;
  void* ctx;
  void (*free_ctx)(void*);
  RolltuiListOptions opt;
  RolltuiListColumn cols[MAX_COLS];
  size_t col_n;
  int seen_w[MAX_COLS]; /* the widest cell each column has shown: a measured column only grows, so it does not jitter */

  /* WHAT IS SHOWN, in order: indexes of source rows. `ordered` 0 is the identity over `src_n`, nothing fetched. */
  size_t* order;
  size_t order_n, order_cap;
  int ordered;
  size_t src_n;
  int stale; /* the data, the filter, the sort or a node's state changed: the order is built again */

  /* THE TREE, per source row: its depth, whether the rows after it are its children, whether it has a later sibling, and for each
   * level above it whether the ancestor at that level has one (bit k), which is whether level k draws a bar on this row */
  unsigned char* depth;
  unsigned char* kids;
  unsigned char* later;
  unsigned int* bars;
  unsigned int* nkids; /* how many children each row has */
  size_t tree_cap;

  RolltuiStr cursor_key;
  size_t cursor;
  size_t top;
  size_t anchor; /* where a range of marks began */
  int has_anchor;
  RolltuiMap marks; /* key -> non-NULL */
  RolltuiMap state; /* tree: key -> 1 opened, 2 closed, overriding `open_depth` */
  int sort_col, sort_rev;
  int col_cursor; /* the column the column cursor is on (`column_cursor`) */
  RolltuiStr filter;
  int count; /* a count for this key that names a row (`rolltui_list_set_count`); 0: none */
  RolltuiStr scored; /* the filter the cursor was last put on the best match for: moving it after that is the person's */
  RolltuiFilter* compiled; /* OWNED: `filter` made once (`compiled`) */
  RolltuiStr kept; /* the filter as it was kept when typing into it began: Escape goes back to it first */
  RolltuiStr compiled_for;
  int filtering; /* typing the filter: printable keys go to it */

  ListEvent events[EVENT_CAP];
  size_t ev_head, ev_n;

  /* VISUAL: a range from row `visual_at` (key `visual_key`) to the cursor, marked over the marks as they were (`visual_before`) */
  int visual;
  size_t visual_at;
  RolltuiStr visual_key;
  RolltuiMap visual_before;
  /* LOADING: the host's rows are still coming (`rolltui_list_set_loading`), since `loading_ms`; `now_ms` the last frame's clock */
  int loading;
  unsigned long long loading_ms, now_ms, base_ms;
  /* CHANGES (`changes_ms`): each row's hash at the last rebuild, and when a changed row changed (ms after `base_ms`, + 1) */
  RolltuiMap seen, changed;
  int seen_any;
  /* FROZEN: the rows as they were, read in place of the host's */
  RolltuiListRow* frozen;
  size_t frozen_n;
  int is_frozen;
  /* A DRAG (`dragging` 1 from a row, 2 moving it): `drag_from` and `drag_to` are shown rows; `edge` -1 above the view, 1 below */
  size_t drag_from, drag_to;
  int edge;
  unsigned long long edge_ms;

  RolltuiRect area;
  int header_y, body_y, body_h, foot_y;
  int mark_w, number_w;
  int col_x[MAX_COLS], col_w[MAX_COLS], col_on[MAX_COLS];
  RolltuiListRow tmp;
  RolltuiDrawScratch* draw;
  int dragging;
  unsigned char shape; /* the cursor row's (ROLLTUI_CURSOR_*); not FILL: `area` is a cell in from each side, `full` the row */
  RolltuiRect full;
};

/* ---- small things ------------------------------------------------------------------------------ */

static const char* sp(const RolltuiStr* s);
static void fetch(RolltuiList* l, size_t src) {
  rolltui_str_clear(&l->tmp.key);
  rolltui_str_list_clear(&l->tmp.cells);
  l->tmp.role = 0;
  l->tmp.depth = 0;
  l->tmp.busy = 0;
  l->tmp.busy_since_ms = 0;
  l->tmp.styled = 0;
  l->tmp.swatch = 0;
  l->tmp.span_n = 0;
  l->tmp.gutter[0] = 0;
  l->tmp.gutter_role = 0;
  memset(l->tmp.cell_role, 0, sizeof l->tmp.cell_role);
  if (l->is_frozen) { /* the row as it was when the list was paused */
    if (src < l->frozen_n) {
      const RolltuiListRow* r = &l->frozen[src];
      RolltuiStr key = l->tmp.key;
      RolltuiStrListRaw cells = l->tmp.cells;
      size_t k;
      l->tmp = *r;
      l->tmp.key = key;
      l->tmp.cells = cells;
      rolltui_str_set(&l->tmp.key, sp(&r->key), r->key.n);
      for (k = 0; k < r->cells.n; ++k) rolltui_str_list_add(&l->tmp.cells, sp(&r->cells.v[k]), r->cells.v[k].n);
    }
  } else if (l->row_fn) {
    l->row_fn(l->ctx, src, &l->tmp);
  }
  if (l->tmp.key.n == 0) { /* no key of its own: its index is */
    char buf[32];
    const int n = snprintf(buf, sizeof buf, "#%zu", src);
    rolltui_str_set(&l->tmp.key, buf, (size_t)n);
  }
}

static const char* cell(const RolltuiListRow* r, size_t k, size_t* n) {
  if (k < r->cells.n && r->cells.v[k].p) {
    *n = r->cells.v[k].n;
    return r->cells.v[k].p;
  }
  *n = 0;
  return "";
}

static const char* sp(const RolltuiStr* s) { return s->p ? s->p : ""; }
static size_t src_count(RolltuiList* l) { return l->is_frozen ? l->frozen_n : l->count_fn ? l->count_fn(l->ctx) : 0; }
static size_t shown_n(const RolltuiList* l) { return l->ordered ? l->order_n : l->src_n; }
static size_t source_of(const RolltuiList* l, size_t i) { return l->ordered ? l->order[i] : i; }
/* Row `i` of what is shown is a section heading (`headings`): the cursor passes over it. */
static int heading_at(const RolltuiList* l, size_t i) {
  if (!(l->opt.tree && l->opt.headings && l->depth)) return 0;
  if (i >= (l->ordered ? l->order_n : l->src_n)) return 0; /* nothing shown there (yet) */
  return l->depth[source_of(l, i)] == 0;
}

static void push_event(RolltuiList* l, unsigned char kind, const RolltuiStr* key) {
  ListEvent* e;
  if (l->ev_n == EVENT_CAP) { /* full: the oldest goes */
    rolltui_str_clear(&l->events[l->ev_head].key);
    l->ev_head = (l->ev_head + 1) % EVENT_CAP;
    --l->ev_n;
  }
  e = &l->events[(l->ev_head + l->ev_n) % EVENT_CAP];
  e->kind = kind;
  rolltui_str_set(&e->key, key ? sp(key) : "", key ? key->n : 0);
  rolltui_str_clear(&e->before);
  ++l->ev_n;
}

/* ---- lifetime ---------------------------------------------------------------------------------- */

RolltuiList* rolltui_list_new(void) {
  RolltuiList* l = (RolltuiList*)rolltui_mem_alloc(sizeof *l);
  memset(l, 0, sizeof *l);
  l->sort_col = -1;
  l->stale = 1;
  l->draw = rolltui_draw_scratch_new();
  return l;
}

void rolltui_list_free(RolltuiList* l) {
  size_t i;
  if (!l) return;
  if (l->free_ctx) l->free_ctx(l->ctx);
  for (i = 0; i < l->col_n; ++i) rolltui_str_free(&l->cols[i].title);
  for (i = 0; i < EVENT_CAP; ++i) {
    rolltui_str_free(&l->events[i].key);
    rolltui_str_free(&l->events[i].before);
  }
  rolltui_list_freeze(l, 0);
  rolltui_str_free(&l->visual_key);
  rolltui_map_release(&l->visual_before);
  rolltui_map_release(&l->seen);
  rolltui_map_release(&l->changed);
  rolltui_mem_free(l->order);
  rolltui_mem_free(l->depth);
  rolltui_mem_free(l->kids);
  rolltui_mem_free(l->later);
  rolltui_mem_free(l->bars);
  rolltui_mem_free(l->nkids);
  rolltui_str_free(&l->cursor_key);
  rolltui_str_free(&l->filter);
  rolltui_str_free(&l->scored);
  rolltui_filter_free(l->compiled);
  rolltui_str_free(&l->kept);
  rolltui_str_free(&l->compiled_for);
  rolltui_map_release(&l->marks);
  rolltui_map_release(&l->state);
  rolltui_str_free(&l->tmp.key);
  rolltui_str_list_release(&l->tmp.cells);
  rolltui_draw_scratch_free(l->draw);
  rolltui_mem_free(l);
}

void rolltui_list_set_source(RolltuiList* l, RolltuiListCountFn count, RolltuiListRowFn row, void* ctx, void (*free_ctx)(void*)) {
  if (l->free_ctx && l->ctx != ctx) l->free_ctx(l->ctx);
  l->count_fn = count;
  l->row_fn = row;
  l->ctx = ctx;
  l->free_ctx = free_ctx;
  l->stale = 1;
}

int rolltui_list_has_source(const RolltuiList* l) { return l && l->count_fn && l->row_fn; }

void rolltui_list_set_options(RolltuiList* l, const RolltuiListOptions* o) {
  if (o->tree != l->opt.tree || o->open_depth != l->opt.open_depth || o->close_over != l->opt.close_over) l->stale = 1;
  l->opt = *o;
}

void rolltui_list_set_columns(RolltuiList* l, const RolltuiListColumn* cols, size_t n) {
  size_t i;
  for (i = 0; i < l->col_n; ++i) rolltui_str_free(&l->cols[i].title);
  if (!cols) n = 0;
  if (n > MAX_COLS) n = MAX_COLS;
  for (i = 0; i < n; ++i) {
    l->cols[i] = cols[i];
    memset(&l->cols[i].title, 0, sizeof l->cols[i].title);
    rolltui_str_set(&l->cols[i].title, sp(&cols[i].title), cols[i].title.n);
    l->seen_w[i] = 0;
  }
  l->col_n = n;
  if (l->sort_col >= (int)n) l->sort_col = -1;
  l->stale = 1;
}

void rolltui_list_refresh(RolltuiList* l) { l->stale = 1; }

/* ---- the tree ---------------------------------------------------------------------------------- */

static void tree_grow(RolltuiList* l, size_t n) {
  if (n <= l->tree_cap) return;
  rolltui_mem_free(l->nkids);
  l->nkids = (unsigned int*)rolltui_mem_alloc(n * sizeof *l->nkids);
  rolltui_mem_free(l->depth);
  rolltui_mem_free(l->kids);
  rolltui_mem_free(l->later);
  rolltui_mem_free(l->bars);
  l->tree_cap = n;
  l->depth = (unsigned char*)rolltui_mem_alloc(n);
  l->kids = (unsigned char*)rolltui_mem_alloc(n);
  l->later = (unsigned char*)rolltui_mem_alloc(n);
  l->bars = (unsigned int*)rolltui_mem_alloc(n * sizeof *l->bars);
}

/* Whether the node at source row `src` (keyed `key`) is open: what the person said, else whether it is shallower than `open_depth`. */
static int is_open(const RolltuiList* l, size_t src, const RolltuiStr* key) {
  const void* v;
  if (l->opt.headings) return 1; /* a group is always open */
  v = rolltui_map_get(&l->state, sp(key), key->n);
  if (v) return v == (const void*)1;
  if (l->opt.close_over && l->nkids && l->nkids[src] > l->opt.close_over) return 0; /* too many to open unasked */
  return l->depth[src] < l->opt.open_depth;
}

/* ---- the filter -------------------------------------------------------------------------------- */

/* THE QUERY, MADE ONCE: the library's filter (`rolltui_filter_new`), remade only when the text changes. */
static const RolltuiFilter* compiled(RolltuiList* l, const RolltuiStr* q) {
  if (!l->compiled || !rolltui_str_eq(&l->compiled_for, sp(q), q->n)) {
    rolltui_filter_free(l->compiled);
    l->compiled = rolltui_filter_new(sp(q), q->n);
    rolltui_str_set(&l->compiled_for, sp(q), q->n);
  }
  return l->compiled;
}

/* Every term of the query matches the row, each within one of its cells (`f1` does not find `file2` beside `10`); a `!` term in
 * none of them. */
static int row_matches(RolltuiList* l, const RolltuiListRow* r, const RolltuiStr* q) {
  const RolltuiFilter* f = compiled(l, q);
  const size_t terms = rolltui_filter_terms(f);
  size_t i, k;
  for (i = 0; i < terms; ++i) {
    int hit = 0;
    for (k = 0; k < r->cells.n && !hit; ++k) hit = rolltui_filter_term(f, i, sp(&r->cells.v[k]), r->cells.v[k].n, NULL);
    if (rolltui_filter_term_negated(f, i) ? hit : !hit) return 0;
  }
  return 1;
}

/* How well a matching row matches: each term's best score over the row's cells, added up. */
static int row_score(RolltuiList* l, const RolltuiListRow* r, const RolltuiStr* q) {
  const RolltuiFilter* f = compiled(l, q);
  const size_t terms = rolltui_filter_terms(f);
  size_t i, k;
  int total = 0;
  for (i = 0; i < terms; ++i) {
    int best = 0;
    if (rolltui_filter_term_negated(f, i)) continue;
    for (k = 0; k < r->cells.n; ++k) {
      int sc = 0;
      if (rolltui_filter_term(f, i, sp(&r->cells.v[k]), r->cells.v[k].n, &sc) && sc > best) best = sc;
    }
    total += best;
  }
  return total;
}

/* A NEW QUERY PUTS THE CURSOR ON ITS BEST MATCH, the order kept: "word left" lands on "move one word left", not on the first row
 * whose letters happen to spell it. Only when the query changes, so a cursor moved after typing stays where it was put. */
static void cursor_to_best(RolltuiList* l) {
  size_t k, best = l->cursor;
  int best_s = -1;
  if (l->filter.n == 0) {
    rolltui_str_clear(&l->scored);
    return;
  }
  if (rolltui_str_eq(&l->scored, sp(&l->filter), l->filter.n)) return;
  rolltui_str_set(&l->scored, sp(&l->filter), l->filter.n);
  for (k = 0; k < l->order_n; ++k) {
    int s;
    if (heading_at(l, k)) continue;
    fetch(l, source_of(l, k));
    if (!row_matches(l, &l->tmp, &l->filter)) continue; /* a parent kept only to show where a match is */
    s = row_score(l, &l->tmp, &l->filter);
    if (s > best_s) best_s = s, best = k;
  }
  l->cursor = best;
  if (best_s >= 0) { /* the row it is on now is the one it follows */
    fetch(l, source_of(l, best));
    if (!rolltui_str_eq(&l->cursor_key, sp(&l->tmp.key), l->tmp.key.n)) {
      rolltui_str_set(&l->cursor_key, sp(&l->tmp.key), l->tmp.key.n);
      push_event(l, ROLLTUI_LIST_EVENT_MOVED, &l->cursor_key);
    }
  }
}

/* ---- sorting ----------------------------------------------------------------------------------- */

typedef struct SortItem {
  size_t src;
  RolltuiStr k;
  double num;
  int has_num;
} SortItem;

/* A number at the start of `s`, with a size's suffix (K, M, G, T, by 1024) or an age's (s, m, h, d) read: what a column of sizes or
 * ages orders by. 0 when it does not start with one. */
static int number_of(const char* s, size_t n, double* out) {
  size_t i = 0;
  double v = 0, frac = 0.1;
  int digits = 0, neg = 0;
  while (i < n && s[i] == ' ') ++i;
  if (i < n && s[i] == '-') neg = 1, ++i;
  while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0'), ++digits;
  if (i < n && s[i] == '.') {
    ++i;
    while (i < n && s[i] >= '0' && s[i] <= '9') v += (s[i++] - '0') * frac, frac /= 10, ++digits;
  }
  if (!digits) return 0;
  while (i < n && s[i] == ' ') ++i;
  if (i < n) {
    switch (s[i]) {
      case 'K': case 'k': v *= 1024.0; break;
      case 'M': v *= 1024.0 * 1024.0; break;
      case 'G': case 'g': v *= 1024.0 * 1024.0 * 1024.0; break;
      case 'T': case 't': v *= 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
      case 'm': v *= 60.0; break;
      case 'h': v *= 3600.0; break;
      case 'd': v *= 86400.0; break;
      default: break;
    }
  }
  *out = neg ? -v : v;
  return 1;
}

static int item_cmp(const SortItem* a, const SortItem* b, int numeric, int rev) {
  int c;
  if (numeric && (!a->has_num || !b->has_num)) { /* a cell that is not a number goes after every number, whichever way */
    if (a->has_num != b->has_num) return a->has_num ? -1 : 1;
    c = rolltui_natural_compare(sp(&a->k), a->k.n, sp(&b->k), b->k.n);
  } else if (numeric) {
    c = a->num < b->num ? -1 : a->num > b->num ? 1 : 0;
  } else {
    c = rolltui_natural_compare(sp(&a->k), a->k.n, sp(&b->k), b->k.n);
  }
  if (rev) c = -c;
  if (c == 0) c = a->src < b->src ? -1 : a->src > b->src ? 1 : 0; /* stable: the host's own order breaks a tie, either way */
  return c;
}

static void merge_sort(SortItem* v, SortItem* tmp, size_t n, int numeric, int rev) {
  size_t mid, i, j, k;
  if (n < 2) return;
  mid = n / 2;
  merge_sort(v, tmp, mid, numeric, rev);
  merge_sort(v + mid, tmp, n - mid, numeric, rev);
  i = 0, j = mid, k = 0;
  while (i < mid && j < n) tmp[k++] = item_cmp(&v[j], &v[i], numeric, rev) < 0 ? v[j++] : v[i++];
  while (i < mid) tmp[k++] = v[i++];
  while (j < n) tmp[k++] = v[j++];
  memcpy(v, tmp, n * sizeof *v);
}

/* ---- building what is shown ------------------------------------------------------------------- */

static void push_order(RolltuiList* l, size_t src) {
  l->order = (size_t*)rolltui_grow(l->order, &l->order_cap, l->order_n + 1, sizeof *l->order);
  l->order[l->order_n++] = src;
}

static int key_is(RolltuiList* l, size_t i, const RolltuiStr* key) {
  fetch(l, source_of(l, i));
  return rolltui_str_eq(&l->tmp.key, sp(key), key->n);
}

/* Finds the cursor's row again by its key, and keeps it on the same screen row when it moved. */
static void find_cursor(RolltuiList* l, size_t old_cursor, size_t old_top) {
  const size_t n = shown_n(l);
  size_t i;
  if (n == 0) {
    l->cursor = 0;
    l->top = 0;
    return;
  }
  if (l->cursor_key.n) {
    if (old_cursor < n && key_is(l, old_cursor, &l->cursor_key)) { /* the fast way first: the row it was on */
      l->cursor = old_cursor;
      return;
    }
    for (i = 0; i < n; ++i)
      if (key_is(l, i, &l->cursor_key)) {
        const size_t screen_row = old_cursor >= old_top ? old_cursor - old_top : 0;
        l->cursor = i;
        l->top = i >= screen_row ? i - screen_row : 0; /* the row it followed stays where the eye was */
        return;
      }
  }
  l->cursor = old_cursor < n ? old_cursor : n - 1; /* its row went: the one that took its place */
  fetch(l, source_of(l, l->cursor));
  rolltui_str_set(&l->cursor_key, sp(&l->tmp.key), l->tmp.key.n);
}

/* THE SIBLINGS from `from` to `to` (each one, then its subtree: `end` is where a row's subtree ends), sorted by the sort column when
 * `sort_here`, each followed by its own children sorted the same way: a pre-order sequence, written at `seq[*pos]`. */
static void emit_level(RolltuiList* l, size_t from, size_t to, const size_t* end, size_t* seq, size_t* pos, int sort_here) {
  const int numeric = l->cols[l->sort_col].sort == ROLLTUI_LIST_SORT_NUMBER;
  size_t n = 0, j, k;
  SortItem* v;
  SortItem* tmp;
  for (j = from; j < to; j = end[j]) ++n;
  if (n == 0) return;
  v = (SortItem*)rolltui_mem_alloc(n * sizeof *v);
  tmp = (SortItem*)rolltui_mem_alloc(n * sizeof *tmp);
  memset(v, 0, n * sizeof *v);
  for (j = from, k = 0; j < to; j = end[j], ++k) {
    size_t cn;
    const char* c;
    v[k].src = j;
    if (!sort_here) continue;
    fetch(l, j);
    c = cell(&l->tmp, (size_t)l->sort_col, &cn);
    rolltui_str_set(&v[k].k, c, cn);
    v[k].has_num = numeric && number_of(c, cn, &v[k].num);
  }
  if (sort_here) merge_sort(v, tmp, n, numeric, l->sort_rev);
  for (k = 0; k < n; ++k) {
    const size_t r = v[k].src;
    rolltui_str_free(&v[k].k);
    seq[(*pos)++] = r;
    if (end[r] > r + 1) emit_level(l, r + 1, end[r], end, seq, pos, 1);
  }
  rolltui_mem_free(v);
  rolltui_mem_free(tmp);
}

static void build_tree(RolltuiList* l) {
  unsigned int lines = 0;
  unsigned char seen[MAX_DEPTH + 2];
  unsigned char* keep = NULL;
  const size_t n = l->src_n;
  size_t* end = (size_t*)rolltui_mem_alloc((n ? n : 1) * sizeof *end);
  size_t* seq = NULL; /* the rows in the order shown, sorted within each level; NULL: the host's order */
  size_t i, p;
  tree_grow(l, n ? n : 1);
  for (i = 0; i < n; ++i) {
    fetch(l, i);
    l->depth[i] = l->tmp.depth > MAX_DEPTH ? MAX_DEPTH : l->tmp.depth;
  }
  for (i = 0; i < n; ++i) l->kids[i] = i + 1 < n && l->depth[i + 1] > l->depth[i];
  { /* WHERE EACH SUBTREE ENDS: the first row after it no deeper than it */
    size_t nearest[MAX_DEPTH + 2];
    unsigned d;
    for (d = 0; d < MAX_DEPTH + 2; ++d) nearest[d] = n;
    for (i = n; i-- > 0;) {
      size_t e = n;
      for (d = 0; d <= l->depth[i]; ++d)
        if (nearest[d] < e) e = nearest[d];
      end[i] = e;
      nearest[l->depth[i]] = i;
    }
  }
  { /* each row's children, counted in one pass: the last row seen at each depth is the parent of the next one deeper */
    size_t last[MAX_DEPTH + 2];
    memset(last, 0xff, sizeof last); /* no row seen at any depth yet: (size_t)-1, never a parent */
    memset(l->nkids, 0, n * sizeof *l->nkids);
    for (i = 0; i < n; ++i) {
      const unsigned d = l->depth[i];
      if (d > 0 && d <= MAX_DEPTH + 1) {
        const size_t q = last[d - 1];
        if (q < i) ++l->nkids[q];
      }
      if (d <= MAX_DEPTH + 1) last[d] = i;
    }
  }
  if (l->sort_col >= 0 && (size_t)l->sort_col < l->col_n && n > 1) { /* SORTED WITHIN EACH LEVEL: a node keeps its children */
    p = 0;
    seq = (size_t*)rolltui_mem_alloc(n * sizeof *seq);
    emit_level(l, 0, n, end, seq, &p, !l->opt.headings); /* headings stay in the host's order; their rows sort */
  }
  /* A LATER SIBLING, going backwards over what is shown: a row at depth d has one when a row at depth d comes after it before a
   * shallower one. */
  memset(seen, 0, sizeof seen);
  for (p = n; p-- > 0;) {
    const size_t r = seq ? seq[p] : p;
    const unsigned d = l->depth[r];
    unsigned k;
    l->later[r] = seen[d];
    seen[d] = 1;
    for (k = d + 1; k <= MAX_DEPTH; ++k) seen[k] = 0;
  }
  for (p = 0; p < n; ++p) { /* forward: the bars this row's ancestors draw */
    const size_t r = seq ? seq[p] : p;
    const unsigned d = l->depth[r];
    l->bars[r] = lines & ((1u << d) - 1u);
    if (l->later[r]) lines |= 1u << d;
    else lines &= ~(1u << d);
    lines &= (d + 1 >= 32) ? ~0u : (1u << (d + 1)) - 1u;
  }
  if (l->filter.n) { /* what matches, and its ancestors: shown, and open */
    keep = (unsigned char*)rolltui_mem_alloc(n ? n : 1);
    memset(keep, 0, n ? n : 1);
    for (i = 0; i < n; ++i) {
      fetch(l, i);
      if (row_matches(l, &l->tmp, &l->filter)) {
        size_t j = i;
        unsigned d = l->depth[i];
        keep[i] = 1;
        while (j-- > 0 && d > 0)
          if (l->depth[j] < d) {
            if (keep[j]) break; /* an ancestor already kept has its own kept */
            keep[j] = 2;
            d = l->depth[j];
          }
      }
    }
  }
  for (p = 0; p < n;) {
    const size_t r = seq ? seq[p] : p;
    int open;
    if (keep && !keep[r]) {
      ++p;
      continue;
    }
    push_order(l, r);
    fetch(l, r);
    open = keep ? 1 : is_open(l, r, &l->tmp.key);
    ++p;
    if (!open && l->kids[r]) p += end[r] - r - 1; /* closed: what is under it is not shown */
  }
  rolltui_mem_free(keep);
  rolltui_mem_free(seq);
  rolltui_mem_free(end);
}

static void sort_order(RolltuiList* l) {
  const int numeric = l->cols[l->sort_col].sort == ROLLTUI_LIST_SORT_NUMBER;
  SortItem* v = (SortItem*)rolltui_mem_alloc(l->order_n * sizeof *v);
  SortItem* tmp = (SortItem*)rolltui_mem_alloc(l->order_n * sizeof *tmp);
  size_t i;
  memset(v, 0, l->order_n * sizeof *v);
  for (i = 0; i < l->order_n; ++i) {
    size_t cn;
    const char* c;
    fetch(l, l->order[i]);
    c = cell(&l->tmp, (size_t)l->sort_col, &cn);
    v[i].src = l->order[i];
    rolltui_str_set(&v[i].k, c, cn);
    v[i].has_num = numeric && number_of(c, cn, &v[i].num);
  }
  merge_sort(v, tmp, l->order_n, numeric, l->sort_rev);
  for (i = 0; i < l->order_n; ++i) {
    l->order[i] = v[i].src;
    rolltui_str_free(&v[i].k);
  }
  rolltui_mem_free(v);
  rolltui_mem_free(tmp);
}

/* A ROW THAT LEFT THE DATA IS NOT MARKED: what the marks say, and the footer counts, is what the host still has. Read only while
 * something is marked, and only until every mark is found. */
static int is_marked(const RolltuiList* l, const RolltuiStr* key);
static void prune_marks(RolltuiList* l) {
  RolltuiMap keep;
  size_t i;
  const size_t want = rolltui_map_count(&l->marks);
  if (want == 0) return;
  memset(&keep, 0, sizeof keep);
  for (i = 0; i < l->src_n && rolltui_map_count(&keep) < want; ++i) {
    fetch(l, i);
    if (is_marked(l, &l->tmp.key)) rolltui_map_put(&keep, sp(&l->tmp.key), l->tmp.key.n, (void*)1);
  }
  rolltui_map_release(&l->marks);
  l->marks = keep;
}

/* A row's hash: its key, its cells and its role, so a row that says something else reads as changed. */
static uintptr_t row_hash(const RolltuiListRow* r) {
  unsigned long long h = 1469598103934665603ull;
  size_t i, k;
  for (i = 0; i < r->key.n; ++i) h = (h ^ (unsigned char)r->key.p[i]) * 1099511628211ull;
  for (k = 0; k < r->cells.n; ++k) {
    h = (h ^ 0x1f) * 1099511628211ull;
    for (i = 0; i < r->cells.v[k].n; ++i) h = (h ^ (unsigned char)r->cells.v[k].p[i]) * 1099511628211ull;
  }
  h = (h ^ r->role) * 1099511628211ull;
  return (uintptr_t)h | 1u; /* never NULL: a map value of NULL is no value */
}

static unsigned long long rel_now(const RolltuiList* l) { return (l->now_ms > l->base_ms ? l->now_ms - l->base_ms : 0) + 1; }

/* WHAT CHANGED since the last rebuild: a row whose hash differs, or that is new, is noted with when. The first rebuild notes none. */
static void note_changes(RolltuiList* l) {
  RolltuiMap now_seen;
  const unsigned long long at = rel_now(l);
  size_t i;
  memset(&now_seen, 0, sizeof now_seen);
  if (l->now_ms == 0) rolltui_map_clear(&l->changed); /* no clock: changed means changed at this refresh */
  for (i = 0; i < l->src_n; ++i) {
    uintptr_t h;
    fetch(l, i);
    h = row_hash(&l->tmp);
    if (l->seen_any && rolltui_map_get(&l->seen, sp(&l->tmp.key), l->tmp.key.n) != (void*)h)
      rolltui_map_put(&l->changed, sp(&l->tmp.key), l->tmp.key.n, (void*)(uintptr_t)at);
    rolltui_map_put(&now_seen, sp(&l->tmp.key), l->tmp.key.n, (void*)h);
  }
  rolltui_map_release(&l->seen);
  l->seen = now_seen;
  l->seen_any = 1;
}

/* How long row `key` is still drawn as changed: milliseconds left, 0 when it is not. */
static unsigned long long change_left(const RolltuiList* l, const RolltuiStr* key) {
  const void* v;
  unsigned long long at, nowr;
  if (!l->opt.changes_ms) return 0;
  v = rolltui_map_get(&l->changed, sp(key), key->n);
  if (!v) return 0;
  if (l->now_ms == 0) return 1;
  at = (unsigned long long)(uintptr_t)v;
  nowr = rel_now(l);
  return nowr < at + l->opt.changes_ms ? at + l->opt.changes_ms - nowr : 0;
}

static void rebuild(RolltuiList* l) {
  const size_t old_cursor = l->cursor, old_top = l->top;
  size_t i;
  l->src_n = src_count(l);
  prune_marks(l);
  if (l->opt.changes_ms && !l->is_frozen) note_changes(l);
  l->order_n = 0;
  l->ordered = l->opt.tree || l->sort_col >= 0 || l->filter.n > 0;
  l->stale = 0;
  if (l->opt.tree) {
    build_tree(l);
  } else if (l->ordered) {
    for (i = 0; i < l->src_n; ++i) {
      if (l->filter.n) {
        fetch(l, i);
        if (!row_matches(l, &l->tmp, &l->filter)) continue;
      }
      push_order(l, i);
    }
    if (l->sort_col >= 0 && l->order_n > 1) sort_order(l);
  }
  find_cursor(l, old_cursor, old_top);
  cursor_to_best(l);
  if (heading_at(l, l->cursor)) { /* never on a heading: the first row after it, else before */
    const size_t n = shown_n(l);
    size_t k = l->cursor;
    while (k < n && heading_at(l, k)) ++k;
    if (k == n)
      for (k = l->cursor; k > 0 && heading_at(l, k); --k) {
      }
    if (k < n && !heading_at(l, k)) l->cursor = k;
  }
}

static void ensure(RolltuiList* l) {
  const size_t n = src_count(l);
  if (l->stale || n != l->src_n) rebuild(l);
}

/* ---- moving ------------------------------------------------------------------------------------ */

static void clamp_view(RolltuiList* l) {
  const size_t n = shown_n(l);
  const size_t h = (size_t)(l->body_h > 0 ? l->body_h : 1);
  size_t off = (size_t)(l->opt.scrolloff > 0 ? l->opt.scrolloff : 0);
  if (off > (h - 1) / 2) off = (h - 1) / 2; /* a margin past half the view centres the cursor */
  if (n == 0) {
    l->top = 0;
    return;
  }
  if (l->cursor >= n) l->cursor = n - 1;
  if (l->cursor < l->top + off) l->top = l->cursor > off ? l->cursor - off : 0;
  if (l->cursor + off >= l->top + h) l->top = l->cursor + off + 1 - h;
  if (n <= h) l->top = 0;
  else if (l->top > n - h) l->top = n - h;
}

static void note_cursor(RolltuiList* l) {
  if (shown_n(l) == 0) return;
  fetch(l, source_of(l, l->cursor));
  if (!rolltui_str_eq(&l->cursor_key, sp(&l->tmp.key), l->tmp.key.n)) {
    rolltui_str_set(&l->cursor_key, sp(&l->tmp.key), l->tmp.key.n);
    push_event(l, ROLLTUI_LIST_EVENT_MOVED, &l->cursor_key);
  }
}

static void visual_apply(RolltuiList* l);
static void move_to(RolltuiList* l, long long to) {
  const long long n = (long long)shown_n(l);
  if (n == 0) return;
  if (l->opt.wrap && to >= n && (long long)l->cursor == n - 1) to = 0;
  else if (l->opt.wrap && to < 0 && l->cursor == 0) to = n - 1;
  if (to < 0) to = 0;
  if (to >= n) to = n - 1;
  if (heading_at(l, (size_t)to)) { /* a heading is passed over, the way the move was going; at an end, the other way */
    const long long dir = to >= (long long)l->cursor ? 1 : -1;
    long long k = to;
    while (k >= 0 && k < n && heading_at(l, (size_t)k)) k += dir;
    if (k < 0 || k >= n)
      for (k = to; k >= 0 && k < n && heading_at(l, (size_t)k); k -= dir) {
      }
    if (k < 0 || k >= n) return; /* nothing but headings */
    to = k;
  }
  l->cursor = (size_t)to;
  clamp_view(l);
  note_cursor(l);
  if (l->visual) visual_apply(l);
}

static int is_marked(const RolltuiList* l, const RolltuiStr* key) { return rolltui_map_get(&l->marks, sp(key), key->n) != NULL; }

static void set_mark(RolltuiList* l, const RolltuiStr* key, int on) {
  if (on) rolltui_map_put(&l->marks, sp(key), key->n, (void*)1);
  else rolltui_map_take(&l->marks, sp(key), key->n);
}

static void mark_range(RolltuiList* l, size_t a, size_t b) {
  size_t i;
  if (a > b) {
    const size_t t = a;
    a = b;
    b = t;
  }
  for (i = a; i <= b && i < shown_n(l); ++i) {
    if (heading_at(l, i)) continue; /* a heading is not a row to mark */
    fetch(l, source_of(l, i));
    set_mark(l, &l->tmp.key, 1);
  }
}

/* ---- a visual range --------------------------------------------------------------------------- */

static void copy_marks(RolltuiMap* to, const RolltuiMap* from) {
  size_t i;
  rolltui_map_clear(to);
  for (i = 0; i < rolltui_map_count(from); ++i) {
    size_t kn;
    const char* k = rolltui_map_key_at(from, i, &kn);
    rolltui_map_put(to, k, kn, (void*)1);
  }
}

/* The range's far end as a shown row: where it was, else wherever its key is now. */
static size_t visual_anchor(RolltuiList* l) {
  const size_t n = shown_n(l);
  size_t i;
  if (l->visual_at < n && key_is(l, l->visual_at, &l->visual_key)) return l->visual_at;
  for (i = 0; i < n; ++i)
    if (key_is(l, i, &l->visual_key)) return l->visual_at = i;
  return l->visual_at = l->cursor; /* gone: the range begins again here */
}

/* THE MARKS ARE what they were, and every row from the range's far end to the cursor. */
static void visual_apply(RolltuiList* l) {
  copy_marks(&l->marks, &l->visual_before);
  mark_range(l, visual_anchor(l), l->cursor);
  push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
}

static void visual_begin(RolltuiList* l) {
  l->visual = 1;
  l->visual_at = l->cursor;
  fetch(l, source_of(l, l->cursor));
  rolltui_str_set(&l->visual_key, sp(&l->tmp.key), l->tmp.key.n);
  copy_marks(&l->visual_before, &l->marks);
  visual_apply(l);
}

/* Ends the range: `keep` 1 keeps what it marked, 0 puts the marks back as they were. */
static void visual_end(RolltuiList* l, int keep) {
  if (!keep) {
    copy_marks(&l->marks, &l->visual_before);
    push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
  }
  l->visual = 0;
  rolltui_map_clear(&l->visual_before);
}

static void set_node(RolltuiList* l, size_t src, int open) {
  fetch(l, src);
  if (!l->kids[src]) return;
  rolltui_map_put(&l->state, sp(&l->tmp.key), l->tmp.key.n, open ? (void*)1 : (void*)2);
  push_event(l, open ? ROLLTUI_LIST_EVENT_OPENED : ROLLTUI_LIST_EVENT_CLOSED, &l->tmp.key);
  l->stale = 1;
}

static void set_all_nodes(RolltuiList* l, int open) {
  size_t i;
  for (i = 0; i < l->src_n; ++i) {
    if (!l->kids[i]) continue;
    fetch(l, i);
    rolltui_map_put(&l->state, sp(&l->tmp.key), l->tmp.key.n, open ? (void*)1 : (void*)2);
  }
  l->stale = 1;
}

/* ---- the actions -------------------------------------------------------------------------------- */

static int is(const char* a, size_t n, const char* name) { return name && strlen(name) == n && memcmp(a, name, n) == 0; }

static void sorted(RolltuiList* l) {
  l->stale = 1;
  push_event(l, ROLLTUI_LIST_EVENT_SORTED, NULL);
}

/* ROWS MOVE in a list that moves them: not sorted (the sort says where a row goes), not a tree (a row has its parent). */
static int can_reorder(const RolltuiList* l) { return l->opt.reorder && l->sort_col < 0 && !l->opt.tree && shown_n(l) > 1; }

/* Shown row `from` goes to where shown row `to` is: just before the row now at `to` moving up, after it moving down. */
static void push_reorder(RolltuiList* l, size_t from, size_t to) {
  const size_t n = shown_n(l);
  ListEvent* e;
  RolltuiStr before;
  memset(&before, 0, sizeof before);
  if (from >= n || to >= n || from == to) return;
  if (to > from) {
    if (to + 1 < n) {
      fetch(l, source_of(l, to + 1));
      rolltui_str_set(&before, sp(&l->tmp.key), l->tmp.key.n);
    }
  } else {
    fetch(l, source_of(l, to));
    rolltui_str_set(&before, sp(&l->tmp.key), l->tmp.key.n);
  }
  fetch(l, source_of(l, from));
  push_event(l, ROLLTUI_LIST_EVENT_REORDER, &l->tmp.key);
  e = &l->events[(l->ev_head + l->ev_n - 1) % EVENT_CAP];
  rolltui_str_set(&e->before, sp(&before), before.n);
  rolltui_str_free(&before);
}

void rolltui_list_sort(RolltuiList* l, int col, int reverse) {
  if (!l) return;
  if (col >= (int)l->col_n) col = -1;
  if (col == l->sort_col && (reverse ? 1 : 0) == l->sort_rev) return;
  l->sort_col = col < 0 ? -1 : col;
  l->sort_rev = col >= 0 && reverse ? 1 : 0;
  sorted(l);
}

int rolltui_list_sort_state(const RolltuiList* l, int* col, int* reverse) {
  if (col) *col = l ? l->sort_col : -1;
  if (reverse) *reverse = l ? l->sort_rev : 0;
  return l && l->sort_col >= 0;
}

size_t rolltui_list_cursor_column(const RolltuiList* l) { return l && l->col_cursor > 0 ? (size_t)l->col_cursor : 0; }

int rolltui_list_typing(const RolltuiList* l) { return l && l->filtering; }

int rolltui_list_cursor_y(const RolltuiList* l) {
  const size_t n = shown_n(l);
  if (n == 0 || l->cursor < l->top || l->cursor >= l->top + (size_t)(l->body_h > 0 ? l->body_h : 0)) return -1;
  return l->body_y + (int)(l->cursor - l->top);
}

int rolltui_list_column_x(const RolltuiList* l, size_t k) {
  return k < l->col_n && l->col_on[k] ? l->col_x[k] : -1;
}

int rolltui_list_column_width(const RolltuiList* l, size_t k) {
  return k < l->col_n && l->col_on[k] ? l->col_w[k] : 0;
}

int rolltui_list_footer(RolltuiList* l, RolltuiStr* out) {
  char buf[96];
  int n = 0;
  size_t marked, shown;
  ensure(l); /* asked before the frame lays it out: the rows as they are now */
  marked = rolltui_map_count(&l->marks);
  shown = shown_n(l);
  rolltui_str_clear(out);
  if (shown > 0 && (shown > (size_t)(l->body_h > 0 ? l->body_h : 0) || l->filter.n)) {
    size_t at = l->cursor + 1, rows = shown, i;
    if (l->opt.tree && l->opt.headings) { /* the rows, not the headings */
      rows = 0, at = 0;
      for (i = 0; i < shown; ++i)
        if (!heading_at(l, i)) {
          ++rows;
          if (i <= l->cursor) at = rows;
        }
    }
    if (rows > 0) n = snprintf(buf, sizeof buf, "%zu of %zu", at, rows);
  }
  if (marked) n += snprintf(buf + n, sizeof buf - (size_t)n, "%s%zu marked", n ? " \xc2\xb7 " : "", marked);
  if (l->visual) n += snprintf(buf + n, sizeof buf - (size_t)n, "%svisual", n ? " \xc2\xb7 " : "");
  if (l->loading && shown > 0) n += snprintf(buf + n, sizeof buf - (size_t)n, "%sloading\xe2\x80\xa6", n ? " \xc2\xb7 " : "");
  if (l->is_frozen) n += snprintf(buf + n, sizeof buf - (size_t)n, "%spaused", n ? " \xc2\xb7 " : "");
  if (n <= 0) return 0;
  rolltui_str_set(out, buf, (size_t)n);
  return 1;
}

void rolltui_list_set_count(RolltuiList* l, int count) { l->count = count > 0 ? count : 0; }

static void filter_changed(RolltuiList* l);

int rolltui_list_has_back(const RolltuiList* l) {
  return l->visual || l->dragging == 2 || l->filtering || l->filter.n || rolltui_map_count(&l->marks) != 0;
}

int rolltui_list_back(RolltuiList* l) {
  if (l->dragging == 2) { /* a move under way: the row stays where it was */
    l->dragging = 0;
    return 1;
  }
  if (l->visual) { /* the range goes, and the marks are what they were before it */
    visual_end(l, 0);
    return 1;
  }
  if (l->filtering && l->kept.n && !rolltui_str_eq(&l->filter, sp(&l->kept), l->kept.n)) { /* back to the filter that was kept */
    l->filtering = 0;
    rolltui_str_set(&l->filter, sp(&l->kept), l->kept.n);
    filter_changed(l);
    return 1;
  }
  if (l->filtering || l->filter.n) { /* one level at a time: the filter, then the marks */
    l->filtering = 0;
    if (l->filter.n) {
      rolltui_str_clear(&l->filter);
      l->stale = 1;
      push_event(l, ROLLTUI_LIST_EVENT_FILTERED, &l->filter);
    }
    return 1;
  }
  if (rolltui_map_count(&l->marks)) {
    rolltui_map_clear(&l->marks);
    push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
    return 1;
  }
  return 0;
}

int rolltui_list_perform(RolltuiList* l, const RolltuiListActions* a, const char* act, size_t n) {
  const long long page = l->body_h > 1 ? l->body_h - 1 : 1;
  size_t count, src;
  int node;
  ensure(l);
  count = shown_n(l);
  src = count ? source_of(l, l->cursor) : 0;
  node = l->opt.tree && count && l->kids[src];
  if (is(act, n, a->up) || is(act, n, a->down)) {
    move_to(l, (long long)l->cursor + (is(act, n, a->up) ? -1 : 1));
    l->has_anchor = 0;
    return 1;
  }
  if (is(act, n, a->page_up)) { move_to(l, (long long)l->cursor - page); return 1; }
  if (is(act, n, a->page_down)) { move_to(l, (long long)l->cursor + page); return 1; }
  if (is(act, n, a->first) || is(act, n, a->last)) { /* after a count, that row: `5 G` is the fifth */
    if (l->count > 0) move_to(l, (long long)l->count - 1);
    else move_to(l, is(act, n, a->first) ? 0 : (long long)count - 1);
    return 1;
  }
  if (is(act, n, a->percent)) {
    if (l->count <= 0 || count == 0) return 0;
    move_to(l, (long long)((count - 1) * (size_t)(l->count > 100 ? 100 : l->count) / 100));
    return 1;
  }
  {
    const long long half = l->body_h > 1 ? l->body_h / 2 : 1, h = l->body_h > 0 ? l->body_h : 1;
    if (is(act, n, a->half_up)) { move_to(l, (long long)l->cursor - half); return 1; }
    if (is(act, n, a->half_down)) { move_to(l, (long long)l->cursor + half); return 1; }
    if (is(act, n, a->view_top) || is(act, n, a->view_middle) || is(act, n, a->view_bottom)) {
      const long long last = (long long)l->top + h - 1 < (long long)count - 1 ? (long long)l->top + h - 1 : (long long)count - 1;
      move_to(l, is(act, n, a->view_top) ? (long long)l->top : is(act, n, a->view_bottom) ? last : ((long long)l->top + last) / 2);
      return 1;
    }
    if (is(act, n, a->scroll_up) || is(act, n, a->scroll_down)) { /* the view moves a row; the cursor stays while it is in view */
      const size_t at = l->cursor;
      if (is(act, n, a->scroll_up) && l->top == 0) return 0;
      if (is(act, n, a->scroll_down) && l->top + (size_t)h >= count) return 0;
      rolltui_list_scroll_to(l, is(act, n, a->scroll_up) ? l->top - 1 : l->top + 1);
      (void)at;
      return 1;
    }
  }
  if (is(act, n, a->back)) return rolltui_list_back(l);
  if (is(act, n, a->filter)) {
    l->filtering = 1;
    rolltui_str_set(&l->kept, sp(&l->filter), l->filter.n); /* what Escape goes back to */
    return 1;
  }
  if (count == 0) return 0;
  fetch(l, src);
  if (is(act, n, a->activate)) {
    if (node && !l->opt.activate_nodes) { /* a node opens or closes */
      set_node(l, src, !is_open(l, src, &l->tmp.key));
      return 1;
    }
    push_event(l, ROLLTUI_LIST_EVENT_ACTIVATE, &l->tmp.key);
    return 1;
  }
  if (l->opt.marks) {
    if (is(act, n, a->mark)) {
      set_mark(l, &l->tmp.key, !is_marked(l, &l->tmp.key));
      push_event(l, ROLLTUI_LIST_EVENT_MARKED, &l->tmp.key);
      move_to(l, (long long)l->cursor + 1); /* on to the next, as a mark key does everywhere */
      l->has_anchor = 0;
      return 1;
    }
    if (is(act, n, a->mark_up) || is(act, n, a->mark_down)) {
      if (!l->has_anchor) l->anchor = l->cursor, l->has_anchor = 1;
      move_to(l, (long long)l->cursor + (is(act, n, a->mark_up) ? -1 : 1));
      mark_range(l, l->anchor, l->cursor);
      push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
      return 1;
    }
    if (is(act, n, a->visual)) { /* a range that follows the cursor; again keeps it */
      if (l->visual) visual_end(l, 1);
      else visual_begin(l);
      return 1;
    }
    if (is(act, n, a->visual_swap)) {
      size_t far;
      if (!l->visual) return 0;
      far = visual_anchor(l);
      l->visual_at = l->cursor;
      fetch(l, source_of(l, l->cursor));
      rolltui_str_set(&l->visual_key, sp(&l->tmp.key), l->tmp.key.n);
      move_to(l, (long long)far);
      return 1;
    }
    if (is(act, n, a->mark_all) || is(act, n, a->invert_marks)) {
      const int invert = is(act, n, a->invert_marks);
      size_t i;
      for (i = 0; i < count; ++i) {
        if (heading_at(l, i)) continue;
        fetch(l, source_of(l, i));
        set_mark(l, &l->tmp.key, invert ? !is_marked(l, &l->tmp.key) : 1);
      }
      push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
      return 1;
    }
    if (is(act, n, a->unmark_all)) {
      if (rolltui_map_count(&l->marks) == 0) return 0;
      rolltui_map_clear(&l->marks);
      push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
      return 1;
    }
  }
  if (l->opt.tree) {
    if (is(act, n, a->open)) {
      if (!node) return 0;
      if (is_open(l, src, &l->tmp.key) || l->filter.n) move_to(l, (long long)l->cursor + 1); /* open already: into its first child */
      else set_node(l, src, 1);
      return 1;
    }
    if (is(act, n, a->close)) {
      size_t i;
      if (node && is_open(l, src, &l->tmp.key) && !l->filter.n) {
        set_node(l, src, 0);
        return 1;
      }
      for (i = l->cursor; i-- > 0;) /* closed, or a leaf: up to its parent */
        if (l->depth[source_of(l, i)] < l->depth[src]) {
          move_to(l, (long long)i);
          return 1;
        }
      return 0;
    }
    if (is(act, n, a->open_all) || is(act, n, a->close_all)) {
      set_all_nodes(l, is(act, n, a->open_all));
      return 1;
    }
    if (is(act, n, a->open_deep)) { /* the node and every node under it */
      const unsigned d = l->depth[src];
      size_t j;
      if (!node) return 0;
      set_node(l, src, 1);
      for (j = src + 1; j < l->src_n && l->depth[j] > d; ++j)
        if (l->kids[j]) {
          fetch(l, j);
          rolltui_map_put(&l->state, sp(&l->tmp.key), l->tmp.key.n, (void*)1);
        }
      l->stale = 1;
      return 1;
    }
  }
  if (is(act, n, a->move_up) || is(act, n, a->move_down)) { /* the host moves its rows; the cursor follows the key */
    const int up = is(act, n, a->move_up);
    if (!can_reorder(l) || (up ? l->cursor == 0 : l->cursor + 1 >= count)) return 0;
    push_reorder(l, l->cursor, up ? l->cursor - 1 : l->cursor + 1);
    return 1;
  }
  if (l->col_n) {
    if (!l->opt.tree && l->opt.column_cursor && (is(act, n, a->open) || is(act, n, a->close))) { /* the column cursor */
      int k = l->col_cursor;
      const int step = is(act, n, a->open) ? 1 : -1;
      do k += step;
      while (k >= 0 && k < (int)l->col_n && !l->col_on[k]);
      if (k < 0 || k >= (int)l->col_n) return 0;
      l->col_cursor = k;
      return 1;
    }
    if (is(act, n, a->sort) && l->opt.column_cursor && !l->opt.tree) { /* the cursor's column, then the other way, then the host's order */
      if (l->sort_col != l->col_cursor) l->sort_col = l->col_cursor, l->sort_rev = 0;
      else if (!l->sort_rev) l->sort_rev = 1;
      else l->sort_col = -1, l->sort_rev = 0;
      sorted(l);
      return 1;
    }
    if (is(act, n, a->sort)) { /* the next column, then the host's own order */
      l->sort_col = l->sort_col + 1 >= (int)l->col_n ? -1 : l->sort_col + 1;
      l->sort_rev = 0;
      sorted(l);
      return 1;
    }
    if (is(act, n, a->reverse) && l->sort_col >= 0) {
      l->sort_rev = !l->sort_rev;
      sorted(l);
      return 1;
    }
  }
  return 0;
}

/* ---- the filter, typed -------------------------------------------------------------------------- */

static void filter_changed(RolltuiList* l) {
  l->stale = 1;
  push_event(l, ROLLTUI_LIST_EVENT_FILTERED, &l->filter);
}

void rolltui_list_set_filter(RolltuiList* l, const char* q, size_t n) {
  if (!q) n = 0;
  rolltui_str_set(&l->kept, q ? q : "", n); /* the host's filter is the one kept: Escape clears it */
  if (rolltui_str_eq(&l->filter, q ? q : "", n)) return;
  rolltui_str_set(&l->filter, q ? q : "", n);
  filter_changed(l);
}

static int filter_key(RolltuiList* l, const RolltuiChord* k) {
  if (k->key == ROLLTUI_KEY_ENTER) { /* kept: the keys are the list's again */
    l->filtering = 0;
    return 1;
  }
  if (k->key == ROLLTUI_KEY_BACKSPACE) {
    size_t n = l->filter.n;
    if (n == 0) {
      l->filtering = 0;
      return 1;
    }
    --n;
    while (n > 0 && ((unsigned char)l->filter.p[n] & 0xC0) == 0x80) --n;
    l->filter.n = n;
    filter_changed(l);
    return 1;
  }
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && k->ch >= 0x20 && k->ch != 0x7F) {
    char buf[4];
    rolltui_str_append(&l->filter, buf, rolltui_u_append_utf8(k->ch, buf));
    filter_changed(l);
    return 1;
  }
  return 0; /* the arrows still move the cursor while typing */
}

/* ---- what the host asks ------------------------------------------------------------------------- */

int rolltui_list_event(RolltuiList* l, RolltuiListEvent* out) {
  ListEvent* e;
  if (l->ev_n == 0) return 0;
  e = &l->events[l->ev_head];
  if (out) {
    out->kind = e->kind;
    rolltui_str_set(&out->key, sp(&e->key), e->key.n);
    rolltui_str_set(&out->before, sp(&e->before), e->before.n);
  }
  l->ev_head = (l->ev_head + 1) % EVENT_CAP;
  --l->ev_n;
  return 1;
}

int rolltui_list_cursor(RolltuiList* l, RolltuiStr* key) {
  ensure(l);
  if (shown_n(l) == 0) return 0;
  if (key) {
    fetch(l, source_of(l, l->cursor));
    rolltui_str_set(key, sp(&l->tmp.key), l->tmp.key.n);
  }
  return 1;
}

size_t rolltui_list_mark_matching(RolltuiList* l, const char* query, size_t len, int on) {
  RolltuiFilter* f;
  size_t i, k, touched = 0;
  if (!l || !query || len == 0) return 0;
  ensure(l);
  f = rolltui_filter_new(query, len);
  for (i = 0; i < l->src_n; ++i) {
    const size_t terms = rolltui_filter_terms(f);
    size_t t;
    int ok = 1;
    fetch(l, i);
    for (t = 0; t < terms && ok; ++t) {
      int hit = 0;
      for (k = 0; k < l->tmp.cells.n && !hit; ++k) hit = rolltui_filter_term(f, t, sp(&l->tmp.cells.v[k]), l->tmp.cells.v[k].n, NULL);
      ok = rolltui_filter_term_negated(f, t) ? !hit : hit;
    }
    if (!ok || (l->opt.headings && l->depth && l->depth[i] == 0)) continue;
    set_mark(l, &l->tmp.key, on);
    ++touched;
  }
  rolltui_filter_free(f);
  if (touched) push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
  return touched;
}

int rolltui_list_set_open(RolltuiList* l, const char* key, size_t len, int open) {
  size_t i;
  if (!l || !key || !l->opt.tree) return 0;
  ensure(l);
  for (i = 0; i < l->src_n; ++i) {
    fetch(l, i);
    if (!rolltui_str_eq(&l->tmp.key, key, len)) continue;
    if (!l->kids[i]) return 0;
    set_node(l, i, open);
    return 1;
  }
  return 0;
}

void rolltui_list_set_loading(RolltuiList* l, int loading) {
  if (!l) return;
  if (loading && !l->loading) l->loading_ms = l->now_ms; /* 0 before the first frame: the first tick starts the wait */
  l->loading = loading ? 1 : 0;
}

void rolltui_list_freeze(RolltuiList* l, int frozen) {
  size_t i;
  if (!l || (frozen ? 1 : 0) == l->is_frozen) return;
  if (frozen) {
    const size_t n = l->count_fn ? l->count_fn(l->ctx) : 0;
    l->frozen = (RolltuiListRow*)rolltui_mem_alloc((n ? n : 1) * sizeof *l->frozen);
    memset(l->frozen, 0, (n ? n : 1) * sizeof *l->frozen);
    for (i = 0; i < n; ++i) {
      size_t k;
      fetch(l, i);
      l->frozen[i] = l->tmp;
      memset(&l->frozen[i].key, 0, sizeof l->frozen[i].key);
      memset(&l->frozen[i].cells, 0, sizeof l->frozen[i].cells);
      rolltui_str_set(&l->frozen[i].key, sp(&l->tmp.key), l->tmp.key.n);
      for (k = 0; k < l->tmp.cells.n; ++k) rolltui_str_list_add(&l->frozen[i].cells, sp(&l->tmp.cells.v[k]), l->tmp.cells.v[k].n);
    }
    l->frozen_n = n;
    l->is_frozen = 1;
  } else {
    for (i = 0; i < l->frozen_n; ++i) {
      rolltui_str_free(&l->frozen[i].key);
      rolltui_str_list_release(&l->frozen[i].cells);
    }
    rolltui_mem_free(l->frozen);
    l->frozen = NULL;
    l->frozen_n = 0;
    l->is_frozen = 0;
  }
  l->stale = 1;
}

static void drag_step(RolltuiList* l, long long to);
int rolltui_list_tick(RolltuiList* l, unsigned long long now_ms) {
  int want = -1;
  if (!l) return -1;
  l->now_ms = now_ms;
  if (now_ms == 0) return -1; /* no clock: nothing waits on one */
  if (l->base_ms == 0) l->base_ms = now_ms;
  if (l->loading && l->loading_ms == 0) l->loading_ms = now_ms;
  if (l->loading && now_ms < l->loading_ms + LOADING_MS) want = (int)(l->loading_ms + LOADING_MS - now_ms);
  if (l->dragging && l->edge) { /* held past the edge: a row every EDGE_MS */
    unsigned long long steps;
    if (l->edge_ms == 0) l->edge_ms = now_ms;
    steps = (now_ms - l->edge_ms) / EDGE_MS;
    l->edge_ms += steps * EDGE_MS;
    if (steps > shown_n(l)) steps = shown_n(l); /* a long wait reaches the end, no further */
    if (steps) {
      const size_t at = l->dragging == 2 ? l->drag_to : l->cursor;
      drag_step(l, (long long)at + l->edge * (long long)steps);
    }
    {
      const int e = (int)(l->edge_ms + EDGE_MS - now_ms);
      if (want < 0 || e < want) want = e;
    }
  }
  if (l->opt.changes_ms && rolltui_map_count(&l->changed)) { /* the soonest changed row to settle; the settled ones go */
    size_t i = 0;
    const unsigned long long nowr = rel_now(l);
    while (i < rolltui_map_count(&l->changed)) {
      const unsigned long long at = (unsigned long long)(uintptr_t)rolltui_map_value_at(&l->changed, i);
      if (nowr >= at + l->opt.changes_ms) {
        rolltui_map_remove_at(&l->changed, i);
        continue;
      }
      if (want < 0 || (int)(at + l->opt.changes_ms - nowr) < want) want = (int)(at + l->opt.changes_ms - nowr);
      ++i;
    }
  }
  return want;
}

int rolltui_list_can(RolltuiList* l, const RolltuiListActions* a, const char* act, size_t n) {
  if (!l || !act) return 0;
  if (is(act, n, a->back)) return rolltui_list_has_back(l);
  if (is(act, n, a->visual_swap)) return l->visual;
  if (is(act, n, a->move_up) || is(act, n, a->move_down)) return can_reorder(l);
  return 1;
}

int rolltui_list_reveal(RolltuiList* l, const char* key, size_t len) {
  size_t i;
  if (!l || !key) return 0;
  ensure(l);
  for (i = 0; i < l->src_n; ++i) {
    fetch(l, i);
    if (!rolltui_str_eq(&l->tmp.key, key, len)) continue;
    if (l->opt.tree && l->depth) { /* its parents opened, nearest first */
      size_t j = i;
      unsigned d = l->depth[i];
      while (j-- > 0 && d > 0)
        if (l->depth[j] < d) {
          d = l->depth[j];
          fetch(l, j);
          rolltui_map_put(&l->state, sp(&l->tmp.key), l->tmp.key.n, (void*)1);
        }
      l->stale = 1;
      ensure(l);
    }
    rolltui_list_set_cursor(l, key, len);
    return 1;
  }
  return 0;
}

void rolltui_list_set_cursor(RolltuiList* l, const char* key, size_t n) {
  size_t i;
  if (!key) return;
  ensure(l);
  for (i = 0; i < shown_n(l); ++i) {
    fetch(l, source_of(l, i));
    if (rolltui_str_eq(&l->tmp.key, key, n)) {
      move_to(l, (long long)i);
      return;
    }
  }
}

size_t rolltui_list_marked(const RolltuiList* l, RolltuiStrListRaw* keys) {
  const size_t n = rolltui_map_count(&l->marks);
  size_t i;
  if (keys) {
    rolltui_str_list_clear(keys);
    for (i = 0; i < n; ++i) {
      size_t kn = 0;
      const char* k = rolltui_map_key_at(&l->marks, i, &kn);
      rolltui_str_list_add(keys, k, kn);
    }
  }
  return n;
}

/* ---- layout ------------------------------------------------------------------------------------- */

static int digits_of(size_t n) {
  int d = 1;
  while (n >= 10) n /= 10, ++d;
  return d;
}

static int sort_arrow_w(const RolltuiList* l, int k) { return l->sort_col == k ? 2 : 0; }

void rolltui_list_set_cursor_shape(RolltuiList* l, unsigned char shape) {
  if (l) l->shape = shape <= ROLLTUI_CURSOR_EDGES ? shape : ROLLTUI_CURSOR_FILL;
}

void rolltui_list_layout(RolltuiList* l, RolltuiRect area, int aw) {
  const int show_header = !l->opt.hide_header && l->col_n > 0;
  const int show_foot = l->filtering || l->filter.n > 0;
  int k, used, r;
  ensure(l);
  l->full = area;
  if (l->shape != ROLLTUI_CURSOR_FILL && !aw && area.w > 4) area.x += 1, area.w -= 2; /* the ends' cells */
  l->area = area;
  l->header_y = show_header && area.h > 0 ? area.y : -1;
  l->body_y = area.y + (show_header ? 1 : 0);
  l->body_h = area.h - (show_header ? 1 : 0) - (show_foot ? 1 : 0);
  if (l->body_h < 0) l->body_h = 0;
  l->foot_y = show_foot && area.h > 1 ? area.y + area.h - 1 : -1;
  l->mark_w = l->opt.marks || l->opt.gutter ? 2 : 0;
  l->number_w = l->opt.line_numbers ? digits_of(shown_n(l)) + 1 : 0;
  clamp_view(l);
  if (l->col_n == 0) return;
  /* A MEASURED COLUMN is as wide as the widest cell it has shown, measured here before the rows are drawn: the first frame is right. */
  for (r = 0; r < l->body_h && l->top + (size_t)r < shown_n(l); ++r) {
    fetch(l, source_of(l, l->top + (size_t)r));
    for (k = 1; k < (int)l->col_n; ++k) {
      size_t cn;
      const char* c;
      int w;
      if (l->cols[k].width > 0) continue;
      c = cell(&l->tmp, (size_t)k, &cn);
      w = rolltui_frame_text_width(l->draw, c, cn, aw);
      if (k < 8 && (l->tmp.swatch & (1u << k))) w += 3; /* its swatch and a space */
      if (w > l->seen_w[k]) l->seen_w[k] = w;
    }
  }
  /* THE COLUMNS THAT FIT: those the screen is wide enough for, each its width; the first takes what is left, and while it would be
   * narrower than FIRST_MIN a column goes from the right. */
  used = l->mark_w + l->number_w;
  for (k = 0; k < (int)l->col_n; ++k) {
    l->col_on[k] = l->cols[k].min_screen <= 0 || area.w >= l->cols[k].min_screen;
    l->col_w[k] = 0;
    if (k == 0 || !l->col_on[k]) continue;
    l->col_w[k] = l->cols[k].width > 0 ? l->cols[k].width : l->seen_w[k];
    if (l->cols[k].width <= 0 && l->cols[k].max_share && l->col_w[k] > area.w * l->cols[k].max_share / 100)
      l->col_w[k] = area.w * l->cols[k].max_share / 100;
    if (!l->opt.hide_header) {
      const int tw = rolltui_frame_text_width(l->draw, sp(&l->cols[k].title), l->cols[k].title.n, aw) + sort_arrow_w(l, k);
      if (l->col_w[k] < tw) l->col_w[k] = tw;
    }
    used += l->col_w[k] + COL_GAP;
  }
  l->col_on[0] = 1;
  for (k = (int)l->col_n - 1; k > 0 && area.w - used < FIRST_MIN; --k)
    if (l->col_on[k] && !l->cols[k].keep) {
      l->col_on[k] = 0;
      used -= l->col_w[k] + COL_GAP;
    }
  l->col_w[0] = area.w - used;
  {
    int x = area.x + l->mark_w + l->number_w;
    for (k = 0; k < (int)l->col_n; ++k) {
      if (!l->col_on[k]) continue;
      l->col_x[k] = x;
      x += l->col_w[k] + COL_GAP;
    }
  }
}

/* ---- drawing ------------------------------------------------------------------------------------ */

/* A CELL'S SPANS, each in its role over what the row drew: a set colour replaces, an attribute is added. */
static void draw_spans(RolltuiList* l, RolltuiFrame* f, int x, int y, size_t cellk, const char* c, size_t cn, int max,
                       const RolltuiStyle* styles, int aw) {
  size_t i;
  for (i = 0; i < l->tmp.span_n && i < ROLLTUI_LIST_SPAN_MAX; ++i) {
    const RolltuiListSpan* sp = &l->tmp.spans[i];
    size_t b = sp->begin, e = sp->end;
    int x0, w;
    RolltuiRect r;
    RolltuiStyle st;
    if (sp->cell != cellk || b >= e || b >= cn) continue;
    if (e > cn) e = cn;
    x0 = rolltui_frame_text_width(l->draw, c, b, aw);
    w = rolltui_frame_text_width(l->draw, c + b, e - b, aw);
    if (x0 >= max) continue;
    if (x0 + w > max) w = max - x0;
    st = styles[sp->role < ROLLTUI_ROLE_COUNT ? sp->role : 0];
    st.bg.kind = 0; /* the row's ground stays: a span colours its letters */
    r.x = x + x0, r.y = y, r.w = w, r.h = 1;
    rolltui_frame_tint(f, r, st);
  }
}

/* WHAT THE FILTER MATCHED in a cell, lit bold and underlined, so it reads in every theme, the one with no colours included. */
static void light_matches(RolltuiList* l, RolltuiFrame* f, int x, int y, const char* c, size_t cn, int max, int aw) {
  size_t b[24], e[24], n, i;
  RolltuiStyle lit;
  if (!l->filter.n || max <= 0) return;
  n = rolltui_filter_spans(compiled(l, &l->filter), c, cn, b, e, 24);
  memset(&lit, 0, sizeof lit);
  lit.bold = 1;
  lit.underline = 1;
  for (i = 0; i < n; ++i) {
    const int x0 = rolltui_frame_text_width(l->draw, c, b[i], aw);
    int w = rolltui_frame_text_width(l->draw, c + b[i], e[i] - b[i], aw);
    RolltuiRect r;
    if (x0 >= max) continue;
    if (x0 + w > max) w = max - x0;
    r.x = x + x0, r.y = y, r.w = w, r.h = 1;
    rolltui_frame_tint(f, r, lit);
  }
}

/* `s` in at most `max` cells, cut at an ellipsis where it does not fit. Returns the cells used. */
static int put_cut(RolltuiList* l, RolltuiFrame* f, int x, int y, const char* s, size_t n, RolltuiStyle st, int max, int aw) {
  return rolltui_frame_put_text_cut(f, l->draw, x, y, s, n, st, max, ROLLTUI_CUT_END, aw, 0);
}

/* The row's guides: a bar where an ancestor has a later sibling, its own branch, then a node's opener. Returns the cells used. */
static int draw_guides(RolltuiList* l, RolltuiFrame* f, int x, int y, int max, size_t src, int open, RolltuiStyle st, int aw) {
  const unsigned d = l->depth[src];
  const int used = 2 * (int)d + 2;
  if (l->opt.headings) return 0; /* groups under headings: no lines, no openers */
  unsigned k;
  /* where an ambiguous glyph is two cells the line-drawing ones are too: ASCII keeps every row on the same columns */
  const char* bar = aw ? "|" : "\xe2\x94\x82";  /* │ */
  const char* mid = aw ? "|" : "\xe2\x94\x9c";  /* ├ */
  const char* end = aw ? "`" : "\xe2\x94\x94";  /* └ */
  const char* dash = aw ? "-" : "\xe2\x94\x80"; /* ─ */
  if (used > max) return 0;
  for (k = 1; k < d; ++k)
    if (l->bars[src] & (1u << k)) rolltui_frame_put(f, x + 2 * (int)(k - 1), y, bar, strlen(bar), 1, st, 0);
  if (d > 0) {
    const char* b = l->later[src] ? mid : end;
    rolltui_frame_put(f, x + 2 * (int)(d - 1), y, b, strlen(b), 1, st, 0);
    rolltui_frame_put(f, x + 2 * (int)(d - 1) + 1, y, dash, strlen(dash), 1, st, 0);
  }
  if (l->kids[src]) {
    const char* g = aw ? (open ? "-" : "+") : (open ? "\xe2\x96\xbe" : "\xe2\x96\xb8"); /* ▾ ▸ */
    rolltui_frame_put(f, x + 2 * (int)d, y, g, strlen(g), 1, st, 0);
  }
  return used;
}

static void draw_header(RolltuiList* l, RolltuiFrame* f, const RolltuiStyle* styles, const RolltuiListRoles* roles, int aw) {
  RolltuiStyle hs = styles[roles->header], ground;
  int k;
  rolltui_frame_ground(f, &ground);
  rolltui_style_on_window(&hs, styles, ground.bg);
  hs.bold = 1; /* a header reads as one in every theme, mono included */
  for (k = 0; k < (int)l->col_n; ++k) {
    const RolltuiStr* t = &l->cols[k].title;
    const int arrow = sort_arrow_w(l, k);
    int x = l->col_x[k], tw;
    if (!l->col_on[k]) continue;
    tw = rolltui_frame_text_width(l->draw, sp(t), t->n, aw);
    if (l->cols[k].align == ROLLTUI_LIST_ALIGN_RIGHT && tw + arrow < l->col_w[k]) x += l->col_w[k] - tw - arrow;
    {
      RolltuiStyle ts = hs; /* the sorted column underlined, the column cursor's reversed: both read with no colour */
      ts.underline = k == l->sort_col;
      ts.reverse = l->opt.column_cursor && k == l->col_cursor;
      x += put_cut(l, f, x, l->header_y, sp(t), t->n, ts, l->col_w[k] - arrow, aw);
    }
    if (arrow) {
      const char* g = aw ? (l->sort_rev ? "v" : "^") : (l->sort_rev ? "\xe2\x96\xbc" : "\xe2\x96\xb2"); /* ▼ ▲ */
      rolltui_frame_put(f, x + 1, l->header_y, g, strlen(g), 1, hs, 0);
    }
  }
}

static void draw_empty(RolltuiList* l, RolltuiFrame* f, RolltuiStyle muted, int aw) {
  char msg[160];
  int n, w;
  if (l->body_h <= 0) return;
  if (l->loading) { /* still coming: a spinner and "loading", once it has taken long enough to say so */
    static const char kLoading[] = "loading\xe2\x80\xa6";
    int x;
    const int y = l->body_y + (l->body_h - 1) / 2;
    if (l->now_ms && l->now_ms < l->loading_ms + LOADING_MS) return;
    w = 2 + rolltui_frame_text_width(l->draw, kLoading, sizeof kLoading - 1, aw);
    x = l->area.x + (l->area.w > w ? (l->area.w - w) / 2 : 0);
    rolltui_spinner_draw(f, x, y, muted, l->loading_ms);
    put_cut(l, f, x + 2, y, kLoading, sizeof kLoading - 1, muted, l->area.w - (x + 2 - l->area.x), aw);
    return;
  }
  if (l->filter.n)
    n = snprintf(msg, sizeof msg, "no rows match \xe2\x80\x9c%.*s\xe2\x80\x9d", (int)(l->filter.n > 100 ? 100 : l->filter.n), l->filter.p);
  else n = snprintf(msg, sizeof msg, "no rows");
  if (n < 0) return;
  if (n >= (int)sizeof msg) n = (int)sizeof msg - 1;
  w = rolltui_frame_text_width(l->draw, msg, (size_t)n, aw);
  put_cut(l, f, l->area.x + (l->area.w > w ? (l->area.w - w) / 2 : 0), l->body_y + (l->body_h - 1) / 2, msg, (size_t)n, muted, l->area.w,
          aw);
}

void rolltui_list_draw(RolltuiList* l, RolltuiFrame* f, const RolltuiStyle* styles, const RolltuiListRoles* roles, int focused,
                       int hover_y, int aw) {
  const RolltuiRect a = l->area;
  const size_t n = shown_n(l);
  RolltuiStyle ground, muted = styles[roles->text_muted];
  int r, k;
  if (a.w <= 0 || a.h <= 0) return;
  rolltui_frame_ground(f, &ground);
  rolltui_style_on_window(&muted, styles, ground.bg);
  if (l->header_y >= 0) draw_header(l, f, styles, roles, aw);
  if (focused && n && l->cursor >= l->top && l->cursor < l->top + (size_t)l->body_h) /* hidden, on the cursor's row: a screen reader's */
    rolltui_frame_set_cursor(f, a.x, l->body_y + (int)(l->cursor - l->top), 0);
  if (n == 0) draw_empty(l, f, muted, aw); /* nothing to show says so, rather than a blank that reads as broken */
  for (r = 0; r < l->body_h && l->top + (size_t)r < n; ++r) {
    const size_t i = l->top + (size_t)r;
    const int moving = l->dragging == 2 && l->drag_to != l->drag_from;
    /* A ROW BEING MOVED is drawn where it would land, the rows between closing up behind it */
    const size_t at = !moving ? i
                      : i == l->drag_to ? l->drag_from
                      : l->drag_from < l->drag_to && i >= l->drag_from && i < l->drag_to ? i + 1
                      : l->drag_from > l->drag_to && i > l->drag_to && i <= l->drag_from ? i - 1
                                                                                            : i;
    const size_t src = source_of(l, at);
    const int y = l->body_y + r;
    const int is_cursor = l->dragging == 2 ? i == l->drag_to : i == l->cursor;
    const int lit = is_cursor || y == hover_y;
    RolltuiStyle st, guide;
    int marked, x = a.x, open = 0;
    fetch(l, src);
    if (heading_at(l, i)) { /* A SECTION HEADING: its name, bold, then a rule to the edge, as a menu's section is drawn */
      RolltuiStyle hs = styles[roles->heading ? roles->heading : roles->header];
      const char* dash = aw ? "-" : "\xe2\x94\x80"; /* ─ */
      size_t cn;
      const char* c = cell(&l->tmp, 0, &cn);
      int used, rx;
      hs.bg = ground.bg;
      hs.bold = 1;
      used = put_cut(l, f, a.x, y, c, cn, hs, a.w, aw);
      for (rx = a.x + used + 1; rx < a.x + a.w; ++rx) rolltui_frame_put(f, rx, y, dash, strlen(dash), 1, muted, 0);
      continue;
    }
    marked = l->opt.marks && is_marked(l, &l->tmp.key);
    if (l->opt.tree && l->kids[src]) open = l->filter.n > 0 || is_open(l, src, &l->tmp.key);
    st = styles[l->tmp.role ? l->tmp.role : roles->text];
    if (is_cursor) st = styles[focused ? roles->cursor : roles->cursor_away];
    else if (y == hover_y) st = styles[roles->hover];
    if (!lit && !l->tmp.role) st.bg = ground.bg; /* plain text sits on its window, whatever the text role's own ground */
    else if (!lit) rolltui_style_on_window(&st, styles, ground.bg);
    if (marked) { /* a marked row: bold in the mark's colour, and the mark column says so without colour */
      st.bold = 1;
      if (!is_cursor && styles[roles->mark].fg.kind) st.fg = styles[roles->mark].fg;
    }
    if (l->opt.changes_ms && change_left(l, &l->tmp.key)) { /* appeared or changed at the last refresh: bold and italic say it */
      st.bold = 1;
      st.italic = 1;
      if (!is_cursor && roles->changed && styles[roles->changed].fg.kind) st.fg = styles[roles->changed].fg;
    }
    if (l->opt.fade && !lit) { /* fainter the further from the cursor, as the eye's focus falls off */
      const size_t d = i > l->cursor ? i - l->cursor : l->cursor - i;
      const size_t pct = d * l->opt.fade > 70 ? 70 : d * l->opt.fade;
      RolltuiStyle faded;
      rolltui_style_fade(&st, ground.bg, 1.0 - (double)pct / 100.0, &faded);
      st = faded;
    }
    if (is_cursor && l->full.w != a.w) { /* the cursor's shape: its ends in the cells kept clear */
      const RolltuiRect band = {l->full.x, y, l->full.w, 1};
      const RolltuiStyle cur = st;
      rolltui_cursor_text_style(&cur, ground.bg, l->shape, &st);
      rolltui_frame_cursor_ends(f, band, cur, ground.bg, l->shape);
    }
    if (lit && !(is_cursor && l->shape == ROLLTUI_CURSOR_EDGES && l->full.w != a.w)) {
      const RolltuiRect band = {a.x, y, a.w, 1};
      rolltui_frame_fill(f, l->draw, band, st, NULL, 0);
    }
    guide = lit ? st : muted;
    guide.bold = 0;
    if (l->mark_w) {
      if (l->tmp.gutter[0]) { /* the row's own mark: copied, cut, tagged */
        RolltuiStyle gs = styles[l->tmp.gutter_role < ROLLTUI_ROLE_COUNT ? l->tmp.gutter_role : 0];
        rolltui_style_on(&gs, st.bg.kind ? st.bg : ground.bg);
        rolltui_frame_put_text(f, l->draw, x, y, l->tmp.gutter, strnlen(l->tmp.gutter, sizeof l->tmp.gutter), gs, 1, aw, 0);
      } else if (marked) {
        RolltuiStyle ms = styles[roles->mark];
        rolltui_style_on(&ms, st.bg.kind ? st.bg : ground.bg);
        rolltui_frame_put(f, x, y, aw ? "*" : "\xe2\x96\x8c", aw ? 1 : 3, 1, ms, 0); /* ▌ */
      }
      x += l->mark_w;
    }
    if (l->number_w) {
      char num[24];
      size_t shown_num = i + 1;
      int nl;
      if (l->opt.line_numbers == 2 && !is_cursor) shown_num = i > l->cursor ? i - l->cursor : l->cursor - i;
      nl = snprintf(num, sizeof num, "%*zu", l->number_w - 1, shown_num);
      if (nl > 0) rolltui_frame_put_text(f, l->draw, x, y, num, (size_t)nl, is_cursor ? st : guide, l->number_w, aw, 0);
      x += l->number_w;
    }
    if (l->col_n == 0) { /* no columns: the row's first cell is the whole row */
      size_t cn;
      const char* c = cell(&l->tmp, 0, &cn);
      int max = a.x + a.w - x - (l->tmp.busy ? 2 : 0);
      if (l->opt.tree) {
        const int g = draw_guides(l, f, x, y, max, src, open, guide, aw);
        x += g;
        max -= g;
      }
      put_cut(l, f, x, y, c, cn, st, max, aw);
      draw_spans(l, f, x, y, 0, c, cn, max, styles, aw);
      light_matches(l, f, x, y, c, cn, max, aw);
    } else {
      for (k = 0; k < (int)l->col_n; ++k) {
        size_t cn;
        const char* c = cell(&l->tmp, (size_t)k, &cn);
        int cx = l->col_x[k], cw = l->col_w[k];
        if (!l->col_on[k]) continue;
        if (k > 0 && (size_t)k >= l->tmp.cells.n) break; /* a row with fewer cells: its last took the width of the rest */
        if ((size_t)k + 1 == l->tmp.cells.n && l->tmp.cells.n < l->col_n) cw = a.x + a.w - cx;
        if (k == 0 && l->opt.tree) {
          const int g = draw_guides(l, f, cx, y, cw, src, open, guide, aw);
          cx += g;
          cw -= g;
        }
        if (k == 0 && l->tmp.busy) cw -= 2;
        if (cw <= 0) continue;
        if (l->cols[k].align == ROLLTUI_LIST_ALIGN_RIGHT) {
          const int w = rolltui_frame_text_width(l->draw, c, cn, aw);
          if (w < cw) cx += cw - w;
        }
        {
          RolltuiStyle cs = st;
          RolltuiStyleColorRaw sw;
          if (k < 8 && (l->tmp.styled & (1u << k))) { /* a role's own sample, on every row */
            cs = styles[l->tmp.cell_role[k] < ROLLTUI_ROLE_COUNT ? l->tmp.cell_role[k] : 0];
            if (cs.bg.kind == 0) cs.bg = lit ? st.bg : ground.bg;
          } else if (l->cols[k].role && !lit && !l->tmp.role) { /* a column of its own kind: keys in the shortcut role */
            cs = styles[l->cols[k].role];
            cs.bg = ground.bg;
            if (marked) cs.bold = 1;
          }
          if (k < 8 && (l->tmp.swatch & (1u << k)) && cw >= 4 && rolltui_color_parse(c, cn, &sw)) { /* the colour, then its name */
            RolltuiStyle ss = cs;
            ss.bg = sw;
            rolltui_frame_put_text(f, l->draw, cx, y, "  ", 2, ss, 2, aw, 0);
            cx += 3;
            cw -= 3;
          }
          if (is_cursor && l->opt.column_cursor && k == l->col_cursor) cs.underline = 1; /* the cell the column cursor is on */
          put_cut(l, f, cx, y, c, cn, cs, cw, aw);
          draw_spans(l, f, cx, y, (size_t)k, c, cn, cw, styles, aw);
          light_matches(l, f, cx, y, c, cn, cw, aw);
        }
      }
    }
    if (l->tmp.busy) { /* what the row is doing is running */
      const int bx = l->col_n ? l->col_x[0] + l->col_w[0] - 1 : a.x + a.w - 1;
      rolltui_spinner_draw(f, bx, y, st, l->tmp.busy_since_ms);
    }
  }
  if (l->foot_y >= 0) { /* THE FILTER, as typed */
    RolltuiStyle ps = styles[roles->prompt], ts = styles[roles->text];
    const RolltuiRect band = {a.x, l->foot_y, a.w, 1};
    int used;
    rolltui_style_on(&ps, ground.bg);
    rolltui_style_on(&ts, ground.bg);
    rolltui_frame_fill(f, l->draw, band, ts, NULL, 0);
    used = rolltui_frame_put_text(f, l->draw, a.x, l->foot_y, "/", 1, ps, a.w, aw, 0);
    used += rolltui_frame_put_text(f, l->draw, a.x + used, l->foot_y, sp(&l->filter), l->filter.n, ts, a.w - used, aw, 0);
    if (l->filtering && focused && used < a.w) rolltui_frame_set_cursor(f, a.x + used, l->foot_y, 1);
  }
}

/* ---- the mouse -------------------------------------------------------------------------------- */

/* The shown row a drag is over, and whether it is past the view's top (-1) or bottom (1). */
static size_t drag_row(const RolltuiList* l, int y, int* edge) {
  const size_t n = shown_n(l);
  long long row = y - l->body_y;
  size_t i;
  *edge = y < l->body_y ? -1 : y >= l->body_y + l->body_h ? 1 : 0;
  if (row < 0) row = 0;
  if (row >= l->body_h) row = l->body_h > 0 ? l->body_h - 1 : 0;
  i = l->top + (size_t)row;
  return n == 0 ? 0 : i >= n ? n - 1 : i;
}

/* A drag carried one row on: the moved row's place, or the cursor and the range it marks. */
static void drag_step(RolltuiList* l, long long to) {
  const long long n = (long long)shown_n(l);
  if (n == 0) return;
  if (to < 0) to = 0;
  if (to >= n) to = n - 1;
  if (l->dragging == 2) {
    const size_t h = (size_t)(l->body_h > 0 ? l->body_h : 1);
    l->drag_to = (size_t)to;
    if (l->drag_to < l->top) l->top = l->drag_to;
    if (l->drag_to >= l->top + h) l->top = l->drag_to + 1 - h;
    return;
  }
  if (!l->opt.marks) return;
  move_to(l, to);
  mark_range(l, l->anchor, l->cursor);
  push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
}

static int handle_mouse(RolltuiList* l, const RolltuiMouseEvent* m) {
  const size_t n = shown_n(l);
  const RolltuiRect a = l->area;
  if (m->kind == 1) { /* release: a moved row goes where it was let go */
    const int was = l->dragging;
    if (was == 2 && l->drag_to != l->drag_from) {
      push_reorder(l, l->drag_from, l->drag_to);
      move_to(l, (long long)l->drag_to);
    }
    l->dragging = 0;
    l->edge = 0;
    return was != 0;
  }
  if (m->kind == 2 && l->dragging) { /* a drag, wherever the pointer went: past an edge it goes on scrolling (`rolltui_list_tick`) */
    int edge;
    const size_t i = drag_row(l, m->y, &edge);
    if (edge != l->edge) l->edge_ms = 0;
    l->edge = edge;
    drag_step(l, (long long)i);
    return 1;
  }
  if (m->x < a.x || m->x >= a.x + a.w) return 0;
  if (m->kind == 0 && m->button == 1 && m->y == l->header_y) { /* a column's title sorts by it; again, the other way */
    int k;
    for (k = 0; k < (int)l->col_n; ++k)
      if (l->col_on[k] && m->x >= l->col_x[k] && m->x < l->col_x[k] + l->col_w[k]) {
        if (l->sort_col == k) l->sort_rev = !l->sort_rev;
        else l->sort_col = k, l->sort_rev = 0;
        sorted(l);
        return 1;
      }
    return 1;
  }
  if (m->kind == 2) return 0; /* a pointer moving with no drag under way is the hover's */
  if (m->y < l->body_y || m->y >= l->body_y + l->body_h) return 0;
  if ((m->kind == 0 || m->kind == 8) && m->button != 1) return 0; /* only the left button chooses; the others are the host's */
  if (m->kind == 0 || m->kind == 8) {
    const size_t i = l->top + (size_t)(m->y - l->body_y);
    size_t src;
    if (i >= n) return 1; /* below the last row: nobody's, and it moves nothing */
    if (heading_at(l, i)) return 1; /* a heading names its rows; it is not one */
    src = source_of(l, i);
    if (m->kind == 8) { /* the terminal's double click is Enter on that row */
      fetch(l, src);
      if (!(l->opt.tree && l->kids[src] && !l->opt.activate_nodes)) push_event(l, ROLLTUI_LIST_EVENT_ACTIVATE, &l->tmp.key);
      return 1;
    }
    move_to(l, (long long)i);
    l->anchor = i;
    l->has_anchor = 1;
    l->dragging = 1;
    l->edge = 0;
    fetch(l, src);
    if (can_reorder(l) && m->x >= a.x + l->mark_w && (!l->opt.marks || is_marked(l, &l->tmp.key))) { /* this drag moves the row */
      l->dragging = 2;
      l->drag_from = l->drag_to = i;
    }
    if (l->opt.marks && m->x < a.x + l->mark_w) { /* a press in the mark column marks */
      fetch(l, src);
      set_mark(l, &l->tmp.key, !is_marked(l, &l->tmp.key));
      push_event(l, ROLLTUI_LIST_EVENT_MARKED, &l->tmp.key);
      return 1;
    }
    if (l->opt.tree && l->kids[src]) { /* a press on the opener opens or closes */
      const int ox = (l->col_n ? l->col_x[0] : a.x + l->mark_w + l->number_w) + 2 * (int)l->depth[src];
      if (m->x >= ox && m->x < ox + 2) {
        fetch(l, src);
        set_node(l, src, !is_open(l, src, &l->tmp.key));
      }
    }
    return 1;
  }
  return 0;
}

int rolltui_list_handle(RolltuiList* l, const RolltuiEvent* e, const RolltuiBindings* b, const RolltuiListActions* a) {
  ensure(l);
  if (e->kind == ROLLTUI_EVENT_MOUSE) return handle_mouse(l, &e->mouse);
  if (e->kind == ROLLTUI_EVENT_PASTE && l->filtering) {
    size_t i;
    for (i = 0; i < e->text_len; ++i) /* one line: a line break or a tab in what was pasted is a space */
      rolltui_str_append(&l->filter, e->text[i] == '\n' || e->text[i] == '\r' || e->text[i] == '\t' ? " " : e->text + i, 1);
    filter_changed(l);
    return 1;
  }
  if (e->kind != ROLLTUI_EVENT_KEY) return 0;
  if (l->filtering && filter_key(l, &e->key)) return 1;
  if (!b || !a) return 0;
  {
    size_t alen = 0;
    const char* act = rolltui_bindings_action_for(b, &e->key, "list", 4, &alen);
    return act && alen ? rolltui_list_perform(l, a, act, alen) : 0;
  }
}

int rolltui_list_scroll_extent(RolltuiList* l, RolltuiScrollExtent* out) {
  out->first = l->top;
  out->visible = (size_t)(l->body_h > 0 ? l->body_h : 0);
  out->total = shown_n(l);
  return 1;
}

int rolltui_list_scroll_to(RolltuiList* l, size_t first) {
  const size_t n = shown_n(l), h = (size_t)(l->body_h > 0 ? l->body_h : 1);
  size_t off = (size_t)(l->opt.scrolloff > 0 ? l->opt.scrolloff : 0);
  if (off > (h - 1) / 2) off = (h - 1) / 2;
  l->top = n > h ? (first > n - h ? n - h : first) : 0;
  /* the cursor comes along inside the margin, so the view stays where it was driven */
  if (n && l->top > 0 && l->cursor < l->top + off) l->cursor = l->top + off < n ? l->top + off : n - 1;
  else if (n && l->top + h < n && l->cursor + off >= l->top + h) l->cursor = l->top + h > off + 1 ? l->top + h - off - 1 : 0;
  else if (n && l->cursor < l->top) l->cursor = l->top;
  else if (n && l->cursor >= l->top + h) l->cursor = l->top + h - 1 < n ? l->top + h - 1 : n - 1;
  note_cursor(l);
  return 1;
}
