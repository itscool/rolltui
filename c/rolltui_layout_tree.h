#ifndef ROLLTUI_C_LAYOUT_TREE_H
#define ROLLTUI_C_LAYOUT_TREE_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_layout_tree.h — THE SPLIT TREE, AS DATA (Phase 15 m5).
 *
 * A window, a row, a column, a placed layer. What each one MEANS is stated in
 * `rolltui/Layout.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it is
 * repeated here. This file answers one question the C++ never had to: **who owns a
 * child?**
 *
 * ---- WHY THIS IS IN BOTH CONFIGURATIONS ------------------------------------------------
 *
 * The same reason `rolltui_md_lines.c` is (Phase 15 m4): **it is DATA both implementations
 * of the placement algorithm fill and read**, and two copies of a data structure are two
 * things that can disagree about what a node is. The flag chooses the ALGORITHM
 * (`rolltui_layout.h`), never the shape of what it walks.
 *
 * ---- THE LIFETIME THE C++ LEFT IMPLICIT, WHICH IS THIS MILESTONE'S SUBJECT --------------
 *
 * `std::vector<Node> children` is a recursive owning tree, and nobody ever decided that:
 * it is what you type. Four consequences nobody was asked about either —
 *
 *   1. **`WindowStack::set_base(const Layer&)` DEEP-COPIES A WHOLE TREE** on every layout
 *      hot-reload, and `push(Layer)` takes one BY VALUE, so opening a popup copies its
 *      tree twice on the way in. Three `std::string`s and a vector per node, per copy.
 *   2. **A node is not stable.** `children.push_back` reallocates, so every `const Node*`
 *      into a sibling array — which is what `place()` and `find()` hand out — is a pointer
 *      into storage a later edit may move. It has never bitten because nothing edits during
 *      a resolve, which is a fact about call order rather than about the type.
 *   3. **`operator==` is a deep tree compare** the compiler wrote, used by the preset
 *      store's "(modified)" check on every keystroke of the design editor.
 *   4. **Every node carries three `std::string`s** whose bytes are almost always under
 *      sixteen and therefore never reach the heap — which is exactly why nobody looked.
 *
 * The C answers all four out loud: a child is an OWNED, INDIVIDUALLY ALLOCATED node whose
 * address never moves, the list holds pointers, and copying a tree is a function with a
 * name (`rolltui_layout_node_copy`) rather than a `=`.
 *
 * ---- ONE DEFINITION ---------------------------------------------------------------------
 *
 * `rolltui::Node`, `rolltui::Layer`, `rolltui::Dim`, `rolltui::Placement` and
 * `rolltui::SplitSize` ARE these structs, the Phase 14 rule. The C++ side adds the five
 * special members and the two dozen accessors host code already writes, and every one of
 * them calls the C functions below — one implementation of "release this subtree", with the
 * destructor as a caller of it.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Takes ownership of `n`. */
void rolltui_node_list_push(RolltuiNodeList* l, RolltuiLayoutNode* n);

void rolltui_layer_list_remove(RolltuiLayerList* l, size_t i); /* frees it, shifts the rest down */

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
void rolltui_layout_node_release(RolltuiLayoutNode* n);

void rolltui_layout_node_free(RolltuiLayoutNode* n);

void rolltui_node_list_copy(RolltuiNodeList* to, const RolltuiNodeList* from);

void rolltui_layer_init(RolltuiLayer* l);
void rolltui_layer_release(RolltuiLayer* l);

void rolltui_layer_list_copy(RolltuiLayerList* to, const RolltuiLayerList* from);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LAYOUT_TREE_H */
