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
#include "rolltui/c/rolltui_widget_list.h"

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
  RolltuiStr key;
} ListEvent;

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
  size_t tree_cap;

  RolltuiStr cursor_key;
  size_t cursor;
  size_t top;
  size_t anchor; /* where a range of marks began */
  int has_anchor;
  RolltuiMap marks; /* key -> non-NULL */
  RolltuiMap state; /* tree: key -> 1 opened, 2 closed, overriding `open_depth` */
  int sort_col, sort_rev;
  RolltuiStr filter;
  int filtering; /* typing the filter: printable keys go to it */

  ListEvent events[EVENT_CAP];
  size_t ev_head, ev_n;

  RolltuiRect area;
  int header_y, body_y, body_h, foot_y;
  int mark_w, number_w;
  int col_x[MAX_COLS], col_w[MAX_COLS], col_on[MAX_COLS];
  RolltuiListRow tmp;
  RolltuiStr scratch_a, scratch_b;
  RolltuiDrawScratch* draw;
  RolltuiUnicodeScratch* u;
  int dragging;
};

/* ---- small things ------------------------------------------------------------------------------ */

static void fetch(RolltuiList* l, size_t src) {
  rolltui_str_clear(&l->tmp.key);
  rolltui_str_list_clear(&l->tmp.cells);
  l->tmp.role = 0;
  l->tmp.depth = 0;
  l->tmp.busy = 0;
  l->tmp.busy_since_ms = 0;
  if (l->row_fn) l->row_fn(l->ctx, src, &l->tmp);
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
static size_t shown_n(const RolltuiList* l) { return l->ordered ? l->order_n : l->src_n; }
static size_t source_of(const RolltuiList* l, size_t i) { return l->ordered ? l->order[i] : i; }

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
  ++l->ev_n;
}

/* ---- lifetime ---------------------------------------------------------------------------------- */

RolltuiList* rolltui_list_new(void) {
  RolltuiList* l = (RolltuiList*)rolltui_mem_alloc(sizeof *l);
  memset(l, 0, sizeof *l);
  l->sort_col = -1;
  l->stale = 1;
  l->draw = rolltui_draw_scratch_new();
  l->u = rolltui_u_scratch_new();
  return l;
}

void rolltui_list_free(RolltuiList* l) {
  size_t i;
  if (!l) return;
  if (l->free_ctx) l->free_ctx(l->ctx);
  for (i = 0; i < l->col_n; ++i) rolltui_str_free(&l->cols[i].title);
  for (i = 0; i < EVENT_CAP; ++i) rolltui_str_free(&l->events[i].key);
  rolltui_mem_free(l->order);
  rolltui_mem_free(l->depth);
  rolltui_mem_free(l->kids);
  rolltui_mem_free(l->later);
  rolltui_mem_free(l->bars);
  rolltui_str_free(&l->cursor_key);
  rolltui_str_free(&l->filter);
  rolltui_map_release(&l->marks);
  rolltui_map_release(&l->state);
  rolltui_str_free(&l->tmp.key);
  rolltui_str_list_release(&l->tmp.cells);
  rolltui_str_free(&l->scratch_a);
  rolltui_str_free(&l->scratch_b);
  rolltui_draw_scratch_free(l->draw);
  rolltui_u_scratch_free(l->u);
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
  if (o->tree != l->opt.tree || o->open_depth != l->opt.open_depth) l->stale = 1;
  if (o->tree && l->sort_col >= 0) { /* a tree is in its own order */
    l->sort_col = -1;
    l->stale = 1;
  }
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
  const void* v = rolltui_map_get(&l->state, sp(key), key->n);
  if (v) return v == (const void*)1;
  return l->depth[src] < l->opt.open_depth;
}

/* ---- the filter -------------------------------------------------------------------------------- */

static int contains(const RolltuiStr* hay, const RolltuiStr* needle) {
  size_t i;
  if (needle->n == 0) return 1;
  if (hay->n < needle->n) return 0;
  for (i = 0; i + needle->n <= hay->n; ++i)
    if (memcmp(hay->p + i, needle->p, needle->n) == 0) return 1;
  return 0;
}

/* Whether one term is in one of the row's cells: fuzzily, or as written (case aside). A term is matched within a cell, never across two:
 * `f1` does not find `file2` beside `10`. */
static int term_in_row(RolltuiList* l, const RolltuiListRow* r, const char* t, size_t tn, int exact) {
  size_t k;
  if (exact) rolltui_u_case(l->u, t, tn, ROLLTUI_CASE_FOLD, &l->scratch_b);
  for (k = 0; k < r->cells.n; ++k) {
    const RolltuiStr* c = &r->cells.v[k];
    if (exact) {
      rolltui_u_case(l->u, sp(c), c->n, ROLLTUI_CASE_FOLD, &l->scratch_a);
      if (contains(&l->scratch_a, &l->scratch_b)) return 1;
    } else if (rolltui_fuzzy_match(t, tn, sp(c), c->n, NULL, NULL, 0)) {
      return 1;
    }
  }
  return 0;
}

/* Every term of the query matches the row: a plain term fuzzily, `'term` as written (case aside), `!term` not at all. */
static int row_matches(RolltuiList* l, const RolltuiListRow* r, const RolltuiStr* q) {
  size_t i;
  int ok = 1;
  for (i = 0; i < q->n && ok;) {
    size_t e;
    int neg = 0, exact = 0;
    while (i < q->n && q->p[i] == ' ') ++i;
    if (i >= q->n) break;
    e = i;
    while (e < q->n && q->p[e] != ' ') ++e;
    if (q->p[i] == '!') neg = 1, ++i;
    if (i < e && q->p[i] == '\'') exact = 1, ++i;
    if (i < e) {
      const int hit = term_in_row(l, r, q->p + i, e - i, exact || neg);
      ok = neg ? !hit : hit;
    }
    i = e;
  }
  return ok;
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

static void build_tree(RolltuiList* l) {
  unsigned int lines = 0;
  unsigned char seen[MAX_DEPTH + 2];
  unsigned char* keep = NULL;
  size_t i;
  tree_grow(l, l->src_n ? l->src_n : 1);
  for (i = 0; i < l->src_n; ++i) {
    fetch(l, i);
    l->depth[i] = l->tmp.depth > MAX_DEPTH ? MAX_DEPTH : l->tmp.depth;
  }
  /* A LATER SIBLING, going backwards: a row at depth d has one when a row at depth d comes after it before a shallower one. */
  memset(seen, 0, sizeof seen);
  for (i = l->src_n; i-- > 0;) {
    const unsigned d = l->depth[i];
    unsigned k;
    l->later[i] = seen[d];
    seen[d] = 1;
    for (k = d + 1; k <= MAX_DEPTH; ++k) seen[k] = 0;
    l->kids[i] = i + 1 < l->src_n && l->depth[i + 1] > d;
  }
  for (i = 0; i < l->src_n; ++i) { /* forward: the bars this row's ancestors draw */
    const unsigned d = l->depth[i];
    l->bars[i] = lines & ((1u << d) - 1u);
    if (l->later[i]) lines |= 1u << d;
    else lines &= ~(1u << d);
    lines &= (d + 1 >= 32) ? ~0u : (1u << (d + 1)) - 1u;
  }
  if (l->filter.n) { /* what matches, and its ancestors: shown, and open */
    keep = (unsigned char*)rolltui_mem_alloc(l->src_n ? l->src_n : 1);
    memset(keep, 0, l->src_n ? l->src_n : 1);
    for (i = 0; i < l->src_n; ++i) {
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
  for (i = 0; i < l->src_n;) {
    int open;
    if (keep && !keep[i]) {
      ++i;
      continue;
    }
    push_order(l, i);
    fetch(l, i);
    open = keep ? 1 : is_open(l, i, &l->tmp.key);
    ++i;
    if (!open && l->kids[i - 1]) { /* closed: what is under it is not shown */
      const unsigned d = l->depth[i - 1];
      while (i < l->src_n && l->depth[i] > d) ++i;
    }
  }
  rolltui_mem_free(keep);
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

static void rebuild(RolltuiList* l) {
  const size_t old_cursor = l->cursor, old_top = l->top;
  size_t i;
  l->src_n = l->count_fn ? l->count_fn(l->ctx) : 0;
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
}

static void ensure(RolltuiList* l) {
  const size_t n = l->count_fn ? l->count_fn(l->ctx) : 0;
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

static void move_to(RolltuiList* l, long long to) {
  const long long n = (long long)shown_n(l);
  if (n == 0) return;
  if (l->opt.wrap && to >= n && (long long)l->cursor == n - 1) to = 0;
  else if (l->opt.wrap && to < 0 && l->cursor == 0) to = n - 1;
  if (to < 0) to = 0;
  if (to >= n) to = n - 1;
  l->cursor = (size_t)to;
  clamp_view(l);
  note_cursor(l);
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
    fetch(l, source_of(l, i));
    set_mark(l, &l->tmp.key, 1);
  }
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

int rolltui_list_back(RolltuiList* l) {
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
  if (is(act, n, a->first)) { move_to(l, 0); return 1; }
  if (is(act, n, a->last)) { move_to(l, (long long)count - 1); return 1; }
  if (is(act, n, a->back)) return rolltui_list_back(l);
  if (is(act, n, a->filter)) {
    l->filtering = 1;
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
    if (is(act, n, a->mark_all) || is(act, n, a->invert_marks)) {
      const int invert = is(act, n, a->invert_marks);
      size_t i;
      for (i = 0; i < count; ++i) {
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
  } else if (l->col_n) {
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

void rolltui_list_layout(RolltuiList* l, RolltuiRect area, int aw) {
  const int show_header = !l->opt.hide_header && l->col_n > 0;
  const int show_foot = l->filtering || l->filter.n > 0;
  int k, used, r;
  ensure(l);
  l->area = area;
  l->header_y = show_header && area.h > 0 ? area.y : -1;
  l->body_y = area.y + (show_header ? 1 : 0);
  l->body_h = area.h - (show_header ? 1 : 0) - (show_foot ? 1 : 0);
  if (l->body_h < 0) l->body_h = 0;
  l->foot_y = show_foot && area.h > 1 ? area.y + area.h - 1 : -1;
  l->mark_w = l->opt.marks ? 2 : 0;
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
    if (!l->opt.hide_header) {
      const int tw = rolltui_frame_text_width(l->draw, sp(&l->cols[k].title), l->cols[k].title.n, aw) + sort_arrow_w(l, k);
      if (l->col_w[k] < tw) l->col_w[k] = tw;
    }
    used += l->col_w[k] + COL_GAP;
  }
  l->col_on[0] = 1;
  for (k = (int)l->col_n - 1; k > 0 && area.w - used < FIRST_MIN; --k)
    if (l->col_on[k]) {
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

/* `s` in at most `max` cells, cut at an ellipsis where it does not fit. Returns the cells used. */
static int put_cut(RolltuiList* l, RolltuiFrame* f, int x, int y, const char* s, size_t n, RolltuiStyle st, int max, int aw) {
  const int ew = rolltui_u_display_width(l->u, ELLIPSIS, 3, aw);
  int cells = 0, used;
  size_t fit;
  if (max <= 0 || n == 0) return 0;
  if (rolltui_frame_text_width(l->draw, s, n, aw) <= max || max <= ew) return rolltui_frame_put_text(f, l->draw, x, y, s, n, st, max, aw, 0);
  fit = rolltui_u_fit(l->u, s, n, max - ew, aw, &cells);
  used = rolltui_frame_put_text(f, l->draw, x, y, s, fit, st, cells, aw, 0);
  return used + rolltui_frame_put_text(f, l->draw, x + used, y, ELLIPSIS, 3, st, ew, aw, 0);
}

/* The row's guides: a bar where an ancestor has a later sibling, its own branch, then a node's opener. Returns the cells used. */
static int draw_guides(RolltuiList* l, RolltuiFrame* f, int x, int y, int max, size_t src, int open, RolltuiStyle st, int aw) {
  const unsigned d = l->depth[src];
  const int used = 2 * (int)d + 2;
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
  RolltuiStyle hs = styles[roles->header];
  int k;
  hs.bold = 1; /* a header reads as one in every theme, mono included */
  for (k = 0; k < (int)l->col_n; ++k) {
    const RolltuiStr* t = &l->cols[k].title;
    const int arrow = sort_arrow_w(l, k);
    int x = l->col_x[k], tw;
    if (!l->col_on[k]) continue;
    tw = rolltui_frame_text_width(l->draw, sp(t), t->n, aw);
    if (l->cols[k].align == ROLLTUI_LIST_ALIGN_RIGHT && tw + arrow < l->col_w[k]) x += l->col_w[k] - tw - arrow;
    x += put_cut(l, f, x, l->header_y, sp(t), t->n, hs, l->col_w[k] - arrow, aw);
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
  rolltui_style_on(&muted, ground.bg);
  if (l->header_y >= 0) draw_header(l, f, styles, roles, aw);
  if (n == 0) draw_empty(l, f, muted, aw); /* nothing to show says so, rather than a blank that reads as broken */
  for (r = 0; r < l->body_h && l->top + (size_t)r < n; ++r) {
    const size_t i = l->top + (size_t)r;
    const size_t src = source_of(l, i);
    const int y = l->body_y + r;
    const int is_cursor = i == l->cursor;
    const int lit = is_cursor || y == hover_y;
    RolltuiStyle st, guide;
    int marked, x = a.x, open = 0;
    fetch(l, src);
    marked = l->opt.marks && is_marked(l, &l->tmp.key);
    if (l->opt.tree && l->kids[src]) open = l->filter.n > 0 || is_open(l, src, &l->tmp.key);
    st = styles[l->tmp.role ? l->tmp.role : roles->text];
    if (is_cursor) st = styles[focused ? roles->cursor : roles->cursor_away];
    else if (y == hover_y) st = styles[roles->hover];
    if (!lit) rolltui_style_on(&st, ground.bg);
    if (marked) { /* a marked row: bold in the mark's colour, and the mark column says so without colour */
      st.bold = 1;
      if (!is_cursor && styles[roles->mark].fg.kind) st.fg = styles[roles->mark].fg;
    }
    if (lit) {
      const RolltuiRect band = {a.x, y, a.w, 1};
      rolltui_frame_fill(f, l->draw, band, st, NULL, 0);
    }
    guide = lit ? st : muted;
    guide.bold = 0;
    if (l->mark_w) {
      if (marked) {
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
    } else {
      for (k = 0; k < (int)l->col_n; ++k) {
        size_t cn;
        const char* c = cell(&l->tmp, (size_t)k, &cn);
        int cx = l->col_x[k], cw = l->col_w[k];
        if (!l->col_on[k]) continue;
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
        put_cut(l, f, cx, y, c, cn, st, cw, aw);
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

static int handle_mouse(RolltuiList* l, const RolltuiMouseEvent* m) {
  const size_t n = shown_n(l);
  const RolltuiRect a = l->area;
  if (m->kind == 1) { /* release */
    const int was = l->dragging;
    l->dragging = 0;
    return was;
  }
  if (m->x < a.x || m->x >= a.x + a.w) return 0;
  if (m->kind == 0 && m->button == 1 && m->y == l->header_y && !l->opt.tree) { /* a column's title sorts by it; again, the other way */
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
  if (m->kind == 2) { /* a drag from a row marks the rows it passes over */
    long long row = m->y - l->body_y;
    size_t i;
    if (!l->dragging || !l->opt.marks || n == 0) return l->dragging;
    if (row < 0) row = 0;
    i = l->top + (size_t)row;
    if (i >= n) i = n - 1;
    move_to(l, (long long)i);
    mark_range(l, l->anchor, i);
    push_event(l, ROLLTUI_LIST_EVENT_MARKED, NULL);
    return 1;
  }
  if (m->y < l->body_y || m->y >= l->body_y + l->body_h) return 0;
  if ((m->kind == 0 || m->kind == 8) && m->button != 1) return 0; /* only the left button chooses; the others are the host's */
  if (m->kind == 0 || m->kind == 8) {
    const size_t i = l->top + (size_t)(m->y - l->body_y);
    size_t src;
    if (i >= n) return 1; /* below the last row: nobody's, and it moves nothing */
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
