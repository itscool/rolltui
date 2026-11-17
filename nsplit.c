/* nsplit.c — see nsplit.h. No dependency beyond the standard library; nothing here names rolltui. */
#include "nsplit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int nsplit_imax(int a, int b) { return a > b ? a : b; }

/* `d.fraction * extent + d.cells`, floored — the one rounding rule every size in this library
 * resolves through, so a fixed share, a fill share (weight turned into a fraction of what is
 * left) and a seam inset all agree on the same pixel/cell an edge lands on. */
static int resolve_dim(double fraction, int cells, int extent) {
  return (int)floor(fraction * (double)extent + 1e-6) + cells;
}

/* ---- growth: the one place this library allocates, amortised doubling like everything else
 * this shape gets used for. Never shrinks — a `detach` leaves `cap` where it was, the same
 * "keep the buffer, the next fill is free" rule a reusable buffer always follows. */
static int ensure_cap(NSplitNode*** arr, size_t* cap, size_t need) {
  size_t want;
  NSplitNode** grown;
  if (need <= *cap) return 1;
  want = *cap ? *cap * 2 : 4;
  if (want < need) want = need;
  grown = (NSplitNode**)realloc(*arr, want * sizeof **arr);
  if (!grown) return 0;
  *arr = grown;
  *cap = want;
  return 1;
}

void nsplit_attach(NSplitNode* parent, size_t index, NSplitNode* node) {
  size_t i;
  if (index > parent->n) index = parent->n;
  if (!ensure_cap(&parent->children, &parent->cap, parent->n + 1)) return;
  for (i = parent->n; i > index; --i) parent->children[i] = parent->children[i - 1];
  parent->children[index] = node;
  ++parent->n;
}

NSplitNode* nsplit_detach(NSplitNode* parent, size_t index) {
  NSplitNode* out;
  size_t i;
  if (index >= parent->n) return NULL;
  out = parent->children[index];
  for (i = index; i + 1 < parent->n; ++i) parent->children[i] = parent->children[i + 1];
  --parent->n;
  return out;
}

void nsplit_reparent(NSplitNode* old_parent, size_t old_index, NSplitNode* new_parent, size_t new_index) {
  NSplitNode* node = nsplit_detach(old_parent, old_index);
  if (node) nsplit_attach(new_parent, new_index, node);
}

void nsplit_release(NSplitNode* root, void (*destroy)(NSplitNode* n)) {
  size_t i;
  for (i = 0; i < root->n; ++i) nsplit_release(root->children[i], destroy);
  free(root->children);
  root->children = NULL;
  root->n = root->cap = 0;
  destroy(root);
}

/* ---- resolve ------------------------------------------------------------------------------ */

/* The per-container scratch, INLINE with a stated spill: `resolve_node` recurses, so a shared
 * reused buffer would alias across depth — each frame of the recursion needs its own. Matches
 * the inline-then-spill shape rolltui's own `place()` used before this was extracted from it. */
#define NSPLIT_INLINE 12

typedef struct nsplit_slot {
  const NSplitNode* node;
  int size;
} nsplit_slot;

static void resolve_node(const NSplitNode* n, NSplitRect box, NSplitSink emit, void* ctx);

/* A child shown in a parent whose inner box is `pw` by `ph`: visible, and the parent big enough for it. */
static int child_shown(const NSplitNode* c, int pw, int ph) {
  return c->visible && (c->parent_min_w <= 0 || pw >= c->parent_min_w) && (c->parent_min_h <= 0 || ph >= c->parent_min_h);
}

static int clamp_share(const NSplitSize* s, int v) {
  if (s->max > 0 && v > s->max) v = s->max;
  if (s->min > 0 && v < s->min) v = s->min;
  return v < 0 ? 0 : v;
}

/* A GRID: the children in reading order, each at the first free cell its span fits, on `cols` equal columns and as many equal rows as
 * the placement reaches; a child's rect is the cells it spans, less the gap. */
static void resolve_grid(const NSplitNode* n, NSplitRect inner, nsplit_slot* slot, size_t visible, NSplitSink emit, void* ctx) {
  const int fit_rows = n->cell_h > 0 ? nsplit_imax((inner.h + n->gap) / (n->cell_h + n->gap), 1) : 1;
  const int cols = n->cols > 0 ? n->cols : n->cell_h > 0 ? (int)((visible + (size_t)fit_rows - 1) / (size_t)fit_rows) : 1;
  const int gap = n->gap;
  int* rc = (int*)malloc(visible * 4 * sizeof *rc); /* each child's row, column, rows and columns */
  unsigned char* taken = NULL;
  int rows = 0, cap_rows = 0;
  size_t i;
  if (!rc) return;
  for (i = 0; i < visible; ++i) {
    const NSplitNode* c = slot[i].node;
    int sr = c->span_r ? c->span_r : 1, sc = c->span_c ? c->span_c : 1, r, col, found = 0;
    if (sc > cols) sc = cols;
    for (r = 0; !found; ++r) {
      if (r + sr > cap_rows) { /* room for the rows this span would reach */
        const int want = (r + sr) * 2 + 4;
        unsigned char* grown = (unsigned char*)realloc(taken, (size_t)want * (size_t)cols);
        if (!grown) {
          free(rc);
          free(taken);
          return;
        }
        memset(grown + (size_t)cap_rows * (size_t)cols, 0, (size_t)(want - cap_rows) * (size_t)cols);
        taken = grown;
        cap_rows = want;
      }
      for (col = 0; col + sc <= cols && !found; ++col) {
        int y, x, free_ = 1;
        for (y = r; y < r + sr && free_; ++y)
          for (x = col; x < col + sc && free_; ++x) free_ = !taken[(size_t)y * (size_t)cols + (size_t)x];
        if (!free_) continue;
        for (y = r; y < r + sr; ++y)
          for (x = col; x < col + sc; ++x) taken[(size_t)y * (size_t)cols + (size_t)x] = 1;
        rc[i * 4] = r, rc[i * 4 + 1] = col, rc[i * 4 + 2] = sr, rc[i * 4 + 3] = sc;
        if (r + sr > rows) rows = r + sr;
        found = 1;
      }
    }
  }
  for (i = 0; i < visible; ++i) { /* a cell's edges by the one rounding rule, the gap kept between cells */
    const int r = rc[i * 4], c = rc[i * 4 + 1], sr = rc[i * 4 + 2], sc = rc[i * 4 + 3];
    const int w_all = inner.w - gap * (cols - 1), h_all = inner.h - gap * (rows - 1);
    NSplitRect cell;
    const int x0 = resolve_dim((double)c / cols, 0, w_all > 0 ? w_all : 0) + gap * c;
    const int x1 = resolve_dim((double)(c + sc) / cols, 0, w_all > 0 ? w_all : 0) + gap * (c + sc - 1);
    const int y0 = resolve_dim((double)r / rows, 0, h_all > 0 ? h_all : 0) + gap * r;
    const int y1 = resolve_dim((double)(r + sr) / rows, 0, h_all > 0 ? h_all : 0) + gap * (r + sr - 1);
    cell.x = inner.x + x0;
    cell.y = inner.y + y0;
    cell.w = nsplit_imax(x1 - x0, 0);
    cell.h = nsplit_imax(y1 - y0, 0);
    resolve_node(slot[i].node, cell, emit, ctx);
  }
  free(rc);
  free(taken);
}

static void resolve_node(const NSplitNode* n, NSplitRect box, NSplitSink emit, void* ctx) {
  NSplitRect inner;
  nsplit_slot inline_slots[NSPLIT_INLINE];
  nsplit_slot* slot = inline_slots;
  nsplit_slot* spill = NULL;
  size_t visible = 0, i, w;
  int row, extent, prev_edge = 0, fixed_total = 0, weight_total = 0;
  int remainder, pos = 0;
  double cum_fraction = 0.0;
  int cum_cells = 0;
  const int inset = (int)n->seam + (int)n->pad;

  if (!n->visible) return;

  inner.x = box.x + inset;
  inner.y = box.y + inset;
  inner.w = box.w - 2 * inset;
  inner.h = box.h - 2 * inset;
  if (inner.w < 0) inner.w = 0;
  if (inner.h < 0) inner.h = 0;

  emit(ctx, n, box, inner);

  if (n->n == 0) return; /* a leaf: nothing to divide among children it does not have */

  for (i = 0; i < n->n; ++i)
    if (child_shown(n->children[i], inner.w, inner.h)) ++visible;
  if (visible == 0) return;
  if (visible > NSPLIT_INLINE) {
    spill = (nsplit_slot*)malloc(visible * sizeof *spill);
    if (!spill) return; /* nothing sound to do; a partial resolve beats a crash */
    slot = spill;
  }
  w = 0;
  for (i = 0; i < n->n; ++i)
    if (child_shown(n->children[i], inner.w, inner.h)) {
      slot[w].node = n->children[i];
      slot[w].size = 0;
      ++w;
    }
  if (n->axis == 2) {
    resolve_grid(n, inner, slot, visible, emit, ctx);
    free(spill);
    return;
  }
  row = n->axis == 1 && !(n->stack_below > 0 && inner.w < n->stack_below); /* a row narrower than its line stacks */
  extent = (row ? inner.w : inner.h) - (int)n->gap * (int)(visible - 1);
  if (extent < 0) extent = 0;

  /* Fixed children: edges of the cumulative (fraction*extent + cells) sum, each held to its bounds. */
  for (i = 0; i < visible; ++i) {
    int edge;
    if (slot[i].node->size.fill) {
      weight_total += nsplit_imax(slot[i].node->size.weight, 1);
      continue;
    }
    cum_fraction += slot[i].node->size.fraction;
    cum_cells += slot[i].node->size.cells;
    edge = resolve_dim(cum_fraction, cum_cells, extent);
    slot[i].size = clamp_share(&slot[i].node->size, nsplit_imax(edge - prev_edge, 0));
    prev_edge = nsplit_imax(edge, prev_edge);
    fixed_total += slot[i].size;
  }
  /* Fills: cumulative weight edges over whatever is left; a fill held at a bound gives the rest to the others, again, until none is
   * past its own (at most once a fill). */
  remainder = nsplit_imax(extent - fixed_total, 0);
  {
    unsigned char held_inline[NSPLIT_INLINE];
    unsigned char* held = visible <= NSPLIT_INLINE ? held_inline : (unsigned char*)malloc(visible); /* NULL: bounds, not passed on */
    size_t pass;
    if (held) memset(held, 0, visible);
    for (pass = 0; pass <= visible; ++pass) {
      int left = remainder, wt = 0, cum_w = 0, prev_fill_edge = 0, moved = 0;
      for (i = 0; i < visible; ++i) {
        if (!slot[i].node->size.fill) continue;
        if (held && held[i]) left -= slot[i].size;
        else wt += nsplit_imax(slot[i].node->size.weight, 1);
      }
      if (left < 0) left = 0;
      for (i = 0; i < visible; ++i) {
        int edge, share, bounded;
        if (!slot[i].node->size.fill || (held && held[i])) continue;
        cum_w += nsplit_imax(slot[i].node->size.weight, 1);
        edge = resolve_dim(wt ? (double)cum_w / (double)wt : 0.0, 0, left);
        share = edge - prev_fill_edge;
        prev_fill_edge = edge;
        bounded = clamp_share(&slot[i].node->size, share);
        slot[i].size = bounded;
        if (bounded != share && held) {
          held[i] = 1;
          moved = 1;
        }
      }
      if (!moved) break;
    }
    if (held && held != held_inline) free(held);
    (void)weight_total;
  }
  /* Positions, one after another with the gap between, clipped to the extent in order. */
  {
    const int full = row ? inner.w : inner.h;
    for (i = 0; i < visible; ++i) {
      NSplitRect r;
      if (pos + slot[i].size > nsplit_imax(full, 0)) slot[i].size = nsplit_imax(nsplit_imax(full, 0) - pos, 0);
      if (row) {
        r.x = inner.x + pos;
        r.y = inner.y;
        r.w = slot[i].size;
        r.h = inner.h;
      } else {
        r.x = inner.x;
        r.y = inner.y + pos;
        r.w = inner.w;
        r.h = slot[i].size;
      }
      resolve_node(slot[i].node, r, emit, ctx);
      pos += slot[i].size + (int)n->gap;
    }
  }
  free(spill); /* NULL in the steady case: the inline array was the whole of it */
}

void nsplit_resolve(const NSplitNode* root, NSplitRect box, NSplitSink emit, void* ctx) {
  resolve_node(root, box, emit, ctx);
}
