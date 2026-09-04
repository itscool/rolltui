/* rolltui/c/rolltui_layout_tree.c — see rolltui_layout_tree.h. Compiled into BOTH
 * configurations: this is the DATA the two placement implementations walk, and two copies
 * of it would be two things that can disagree about what a node is. */
#include "rolltui/c/rolltui_layout_tree.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

/* ---- the child list ---------------------------------------------------------------------- */

size_t rolltui_node_list_count(const RolltuiNodeList* l) { return l->n; }

RolltuiLayoutNode* rolltui_node_list_at(const RolltuiNodeList* l, size_t i) {
  return i < l->n ? l->v[i] : NULL;
}

void rolltui_node_list_push(RolltuiNodeList* l, RolltuiLayoutNode* n) {
  /* GROWING, AMORTISED (rolltui_alloc.h strategy 2): a child list is appended to and its
   * final length is a file's business, not this function's. */
  l->v = (RolltuiLayoutNode**)rolltui_grow(l->v, &l->cap, l->n + 1, sizeof *l->v);
  l->v[l->n++] = n;
}

RolltuiLayoutNode* rolltui_node_list_add(RolltuiNodeList* l) {
  RolltuiLayoutNode* n = rolltui_layout_node_new();
  rolltui_node_list_push(l, n);
  return n;
}

void rolltui_node_list_insert(RolltuiNodeList* l, size_t i, RolltuiLayoutNode* n) {
  if (i > l->n) i = l->n;
  l->v = (RolltuiLayoutNode**)rolltui_grow(l->v, &l->cap, l->n + 1, sizeof *l->v);
  memmove(l->v + i + 1, l->v + i, (l->n - i) * sizeof *l->v);
  l->v[i] = n;
  l->n++;
}

void rolltui_node_list_remove(RolltuiNodeList* l, size_t i) {
  if (i >= l->n) return;
  rolltui_layout_node_free(l->v[i]);
  memmove(l->v + i, l->v + i + 1, (l->n - i - 1) * sizeof *l->v);
  l->n--;
}

void rolltui_node_list_clear(RolltuiNodeList* l) {
  size_t i;
  for (i = 0; i < l->n; ++i) rolltui_layout_node_free(l->v[i]);
  l->n = 0; /* the ARRAY stays — the reuse `clear()` was shipped four times not doing */
}

void rolltui_node_list_release(RolltuiNodeList* l) {
  rolltui_node_list_clear(l);
  rolltui_mem_free(l->v);
  l->v = NULL;
  l->cap = 0;
}

void rolltui_node_list_copy(RolltuiNodeList* to, const RolltuiNodeList* from) {
  size_t i;
  if (to == from) return;
  rolltui_node_list_clear(to);
  for (i = 0; i < from->n; ++i) {
    RolltuiLayoutNode* c = rolltui_node_list_add(to);
    rolltui_layout_node_copy(c, from->v[i]);
  }
}

/* ---- the node ----------------------------------------------------------------------------- */

void rolltui_layout_node_init(RolltuiLayoutNode* n) {
  memset(n, 0, sizeof *n);
  /* The three fields whose zero is NOT the default. Stated here rather than left to a
   * `= {0}` at each call site, which is exactly the unasked question this port removes. */
  n->visible = 1;
  n->background = ROLLTUI_ROLE_DEFAULT_BACKGROUND;
  n->size.fill = 1;
  n->size.weight = 1;
}

RolltuiLayoutNode* rolltui_layout_node_new(void) {
  RolltuiLayoutNode* n = (RolltuiLayoutNode*)rolltui_mem_alloc(sizeof *n);
  rolltui_layout_node_init(n);
  return n;
}

void rolltui_layout_node_release(RolltuiLayoutNode* n) {
  if (!n) return;
  rolltui_str_free(&n->id);
  rolltui_str_free(&n->content);
  rolltui_str_free(&n->title);
  rolltui_node_list_release(&n->children);
  rolltui_layout_node_init(n);
}

void rolltui_layout_node_free(RolltuiLayoutNode* n) {
  if (!n) return;
  rolltui_layout_node_release(n);
  rolltui_mem_free(n);
}

void rolltui_layout_node_copy(RolltuiLayoutNode* to, const RolltuiLayoutNode* from) {
  if (to == from) return;
  to->kind = from->kind;
  rolltui_str_set(&to->id, from->id.p, from->id.n);
  rolltui_str_set(&to->content, from->content.p, from->content.n);
  rolltui_str_set(&to->title, from->title.p, from->title.n);
  to->border = from->border;
  to->focusable = from->focusable;
  to->visible = from->visible;
  to->background = from->background;
  to->size = from->size;
  rolltui_node_list_copy(&to->children, &from->children);
}

static int str_equal(const RolltuiStr* a, const RolltuiStr* b) {
  return a->n == b->n && (a->n == 0 || memcmp(a->p, b->p, a->n) == 0);
}

static int split_size_equal(const RolltuiSplitSize* a, const RolltuiSplitSize* b) {
  return a->fill == b->fill && a->weight == b->weight && a->dim.fraction == b->dim.fraction &&
         a->dim.cells == b->dim.cells;
}

int rolltui_layout_node_equal(const RolltuiLayoutNode* a, const RolltuiLayoutNode* b) {
  size_t i;
  if (a == b) return 1;
  if (a->kind != b->kind || a->border != b->border || a->focusable != b->focusable ||
      a->visible != b->visible || a->background != b->background)
    return 0;
  if (!str_equal(&a->id, &b->id) || !str_equal(&a->content, &b->content) || !str_equal(&a->title, &b->title))
    return 0;
  if (!split_size_equal(&a->size, &b->size)) return 0;
  if (a->children.n != b->children.n) return 0;
  for (i = 0; i < a->children.n; ++i)
    if (!rolltui_layout_node_equal(a->children.v[i], b->children.v[i])) return 0;
  return 1;
}

/* ---- a layer -------------------------------------------------------------------------------- */

void rolltui_layer_init(RolltuiLayer* l) {
  memset(l, 0, sizeof *l);
  rolltui_layout_node_init(&l->root);
  l->placement.w.fraction = 1;
  l->placement.h.fraction = 1;
  l->placement.clamp = 1;
}

void rolltui_layer_release(RolltuiLayer* l) {
  if (!l) return;
  rolltui_str_free(&l->id);
  rolltui_str_free(&l->focus);
  rolltui_layout_node_release(&l->root);
  rolltui_layer_init(l);
}

void rolltui_layer_copy(RolltuiLayer* to, const RolltuiLayer* from) {
  if (to == from) return;
  rolltui_str_set(&to->id, from->id.p, from->id.n);
  rolltui_str_set(&to->focus, from->focus.p, from->focus.n);
  to->placement = from->placement;
  to->modal = from->modal;
  rolltui_layout_node_copy(&to->root, &from->root);
}

void rolltui_layer_move(RolltuiLayer* to, RolltuiLayer* from) {
  if (to == from) return;
  rolltui_layer_release(to);
  /* A shallow steal of every buffer: the node's three strings and its child array move with
   * the struct, which is what makes a popup push cost nothing after the layer was built. */
  memcpy(to, from, sizeof *to);
  rolltui_layer_init(from);
}

int rolltui_layer_equal(const RolltuiLayer* a, const RolltuiLayer* b) {
  if (a == b) return 1;
  if (!str_equal(&a->id, &b->id) || !str_equal(&a->focus, &b->focus)) return 0;
  if (a->modal != b->modal) return 0;
  if (memcmp(&a->placement, &b->placement, sizeof a->placement) != 0) {
    /* A field compare rather than the memcmp's answer: padding inside the struct is not
     * part of a placement, and a raw compare would make two equal placements differ. */
    const RolltuiPlacement* p = &a->placement;
    const RolltuiPlacement* q = &b->placement;
    const RolltuiOptDim* pm = &p->min_w;
    const RolltuiOptDim* qm = &q->min_w;
    int i;
    if (p->x.fraction != q->x.fraction || p->x.cells != q->x.cells) return 0;
    if (p->y.fraction != q->y.fraction || p->y.cells != q->y.cells) return 0;
    if (p->w.fraction != q->w.fraction || p->w.cells != q->w.cells) return 0;
    if (p->h.fraction != q->h.fraction || p->h.cells != q->h.cells) return 0;
    if (p->anchor != q->anchor || p->clamp != q->clamp) return 0;
    for (i = 0; i < 4; ++i) {
      if (pm[i].present != qm[i].present) return 0;
      if (pm[i].present && (pm[i].d.fraction != qm[i].d.fraction || pm[i].d.cells != qm[i].d.cells)) return 0;
    }
  }
  return rolltui_layout_node_equal(&a->root, &b->root);
}
