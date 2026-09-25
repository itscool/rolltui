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

static void resolve_node(const NSplitNode* n, NSplitRect box, NSplitSink emit, void* ctx) {
  NSplitRect inner;
  nsplit_slot inline_slots[NSPLIT_INLINE];
  nsplit_slot* slot = inline_slots;
  nsplit_slot* spill = NULL;
  size_t visible = 0, i, w;
  int row, extent, prev_edge = 0, fixed_total = 0, weight_total = 0;
  int remainder, cum_w = 0, prev_fill_edge = 0, pos = 0;
  double cum_fraction = 0.0;
  int cum_cells = 0;

  if (!n->visible) return;

  inner.x = box.x + n->seam;
  inner.y = box.y + n->seam;
  inner.w = box.w - 2 * (int)n->seam;
  inner.h = box.h - 2 * (int)n->seam;
  if (inner.w < 0) inner.w = 0;
  if (inner.h < 0) inner.h = 0;

  emit(ctx, n, box, inner);

  if (n->n == 0) return; /* a leaf: nothing to divide among children it does not have */

  row = n->axis != 0;
  for (i = 0; i < n->n; ++i)
    if (n->children[i]->visible) ++visible;
  if (visible == 0) return;
  if (visible > NSPLIT_INLINE) {
    spill = (nsplit_slot*)malloc(visible * sizeof *spill);
    if (!spill) return; /* nothing sound to do; a partial resolve beats a crash */
    slot = spill;
  }
  w = 0;
  for (i = 0; i < n->n; ++i)
    if (n->children[i]->visible) {
      slot[w].node = n->children[i];
      slot[w].size = 0;
      ++w;
    }
  extent = row ? inner.w : inner.h;

  /* Fixed children: edges of the cumulative (fraction*extent + cells) sum. */
  for (i = 0; i < visible; ++i) {
    int edge;
    if (slot[i].node->size.fill) {
      weight_total += nsplit_imax(slot[i].node->size.weight, 1);
      continue;
    }
    cum_fraction += slot[i].node->size.fraction;
    cum_cells += slot[i].node->size.cells;
    edge = resolve_dim(cum_fraction, cum_cells, extent);
    slot[i].size = nsplit_imax(edge - prev_edge, 0);
    prev_edge = nsplit_imax(edge, prev_edge);
    fixed_total += slot[i].size;
  }
  /* Fills: cumulative weight edges over whatever is left. */
  remainder = nsplit_imax(extent - fixed_total, 0);
  for (i = 0; i < visible; ++i) {
    int edge;
    if (!slot[i].node->size.fill) continue;
    cum_w += nsplit_imax(slot[i].node->size.weight, 1);
    edge = resolve_dim((double)cum_w / (double)weight_total, 0, remainder);
    slot[i].size = edge - prev_fill_edge;
    prev_fill_edge = edge;
  }
  /* Positions, one after another, clipped to the extent in order. */
  for (i = 0; i < visible; ++i) {
    NSplitRect r;
    if (pos + slot[i].size > nsplit_imax(extent, 0)) slot[i].size = nsplit_imax(nsplit_imax(extent, 0) - pos, 0);
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
    pos += slot[i].size;
  }
  free(spill); /* NULL in the steady case: the inline array was the whole of it */
}

void nsplit_resolve(const NSplitNode* root, NSplitRect box, NSplitSink emit, void* ctx) {
  resolve_node(root, box, emit, ctx);
}
