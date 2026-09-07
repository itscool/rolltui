/* rolltui/c/rolltui_layout_tree.c — see rolltui_layout_tree.h. Compiled into BOTH
 * configurations: this is the DATA the two placement implementations walk, and two copies
 * of it would be two things that can disagree about what a node is. */
#include "rolltui/c/rolltui_layout_tree.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_terminal.h"

/* ---- the child list ---------------------------------------------------------------------- */

/* THE MECHANICS ARE `RolltuiPtrVec`'s (rolltui_str.h), not a second copy of them: this list
 * has that layout by construction and the cast is what says so. Every array in this port
 * holds pointers to things it owns, so one mechanism with typed faces is the C answer to
 * what a C++ template would have made one type. */
void rolltui_node_list_push(RolltuiNodeList* l, RolltuiLayoutNode* n) {
  rolltui_ptrvec_push((RolltuiPtrVec*)l, n);
}

RolltuiLayoutNode* rolltui_node_list_add(RolltuiNodeList* l) {
  RolltuiLayoutNode* n = rolltui_layout_node_new();
  rolltui_node_list_push(l, n);
  return n;
}

void rolltui_node_list_insert(RolltuiNodeList* l, size_t i, RolltuiLayoutNode* n) {
  rolltui_ptrvec_insert((RolltuiPtrVec*)l, i, n);
}

void rolltui_node_list_remove(RolltuiNodeList* l, size_t i) {
  rolltui_layout_node_free((RolltuiLayoutNode*)rolltui_ptrvec_take((RolltuiPtrVec*)l, i));
}

void rolltui_node_list_clear(RolltuiNodeList* l) {
  size_t i;
  for (i = 0; i < l->n; ++i) rolltui_layout_node_free(l->v[i]);
  l->n = 0; /* the ARRAY stays — the reuse `clear()` was shipped four times not doing */
}

void rolltui_node_list_release(RolltuiNodeList* l) {
  rolltui_node_list_clear(l);
  rolltui_ptrvec_free((RolltuiPtrVec*)l);
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

/* ---- the node ---------------------------------------------------------------------------- */

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

/* ---- a layer ----------------------------------------------------------------------------- */

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

/* ---- popups: an owned array of Layer values ---------------------------------------------- */
/* GROWING AMORTISED (rolltui_alloc.h strategy 2), exactly as RolltuiWindowStack's own `layers`
 * array already does it: a Layer is trivially relocatable (every owned byte is behind a
 * pointer elsewhere), so `rolltui_grow_zeroed` may move the whole array with one realloc. */

RolltuiLayer* rolltui_layer_list_add(RolltuiLayerList* l) {
  l->v = (RolltuiLayer*)rolltui_grow_zeroed(l->v, &l->cap, l->n + 1, sizeof *l->v);
  rolltui_layer_init(&l->v[l->n]);
  return &l->v[l->n++];
}

void rolltui_layer_list_remove(RolltuiLayerList* l, size_t i) {
  if (i >= l->n) return;
  rolltui_layer_release(&l->v[i]);
  memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof *l->v);
  --l->n;
}

void rolltui_layer_list_remove_id(RolltuiLayerList* l, const char* id, size_t len) {
  size_t i;
  for (i = 0; i < l->n; ++i)
    if (rolltui_str_eq(&l->v[i].id, id, len)) {
      rolltui_layer_list_remove(l, i);
      return;
    }
}

void rolltui_layer_list_clear(RolltuiLayerList* l) {
  size_t i;
  for (i = 0; i < l->n; ++i) rolltui_layer_release(&l->v[i]);
  l->n = 0; /* the array stays, same reuse rule as every other _clear here */
}

void rolltui_layer_list_release(RolltuiLayerList* l) {
  rolltui_layer_list_clear(l);
  rolltui_mem_free(l->v);
  l->v = NULL;
  l->cap = 0;
}

void rolltui_layer_list_copy(RolltuiLayerList* to, const RolltuiLayerList* from) {
  size_t i;
  if (to == from) return;
  rolltui_layer_list_clear(to);
  to->v = (RolltuiLayer*)rolltui_grow_zeroed(to->v, &to->cap, from->n, sizeof *to->v);
  for (i = 0; i < from->n; ++i) {
    rolltui_layer_init(&to->v[i]);
    rolltui_layer_copy(&to->v[i], &from->v[i]);
  }
  to->n = from->n;
}

int rolltui_layer_list_equal(const RolltuiLayerList* a, const RolltuiLayerList* b) {
  size_t i;
  if (a->n != b->n) return 0;
  for (i = 0; i < a->n; ++i)
    if (!rolltui_layer_equal(&a->v[i], &b->v[i])) return 0;
  return 1;
}
