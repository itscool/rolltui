#ifndef ROLLTUI_C_MENU_TREE_H
#define ROLLTUI_C_MENU_TREE_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_menu_tree.h — THE MENU TREE, AS DATA (Phase 15 m5).
 *
 * An item is an action, a submenu, a toggle, a choice or a typed input field. What each one
 * MEANS is stated in `rolltui/Menu.hpp` and asserted in `rolltui/tests/menu_test.cpp`; none
 * of it is repeated here. Like the split tree (`rolltui_layout_tree.h`) this file is in BOTH
 * configurations, because it is DATA both implementations of the widget walk.
 *
 * ---- WHAT THE C++ LEFT IMPLICIT ---------------------------------------------------------
 *
 * A `MenuItem` owns SIX `std::string`s (id, label, action_name, shortcut, value, and the
 * spec's validator and hint make eight) plus a `std::vector<MenuItem>` of children. Its
 * defaulted `operator==` is a deep tree compare. `set_options(id, std::vector<MenuItem>)`
 * takes a whole subtree BY VALUE. None of that was decided; it is what you type — and the
 * design editor rebuilds a menu tree on every keystroke, so it is a real per-keystroke cost
 * nobody had a reason to look at.
 *
 * ---- THE SECOND OWNED-POINTER LIST, AND WHY IT IS NOT A THIRD INVENTION ------------------
 *
 * `RolltuiMenuItemList` is the same shape as `RolltuiNodeList`: an owned array of pointers to
 * individually allocated items, so a child's address is stable. Both are laid out exactly as
 * `RolltuiPtrVec` (`rolltui_str.h`) and both delegate their mechanics to it — a C++ template
 * would have made them one type, and in C the honest answer is one MECHANISM with two typed
 * faces rather than two copies of the mechanism. That asymmetry is the same one m3 measured
 * on `PresetStore` and it belongs in m6's verdict, not in a workaround here.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif
size_t rolltui_menu_list_count(const RolltuiMenuItemList* l);
RolltuiMenuItem* rolltui_menu_list_at(const RolltuiMenuItemList* l, size_t i);


/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
void rolltui_input_spec_init(RolltuiInputSpec* s);
void rolltui_input_spec_release(RolltuiInputSpec* s);

void rolltui_menu_item_release(RolltuiMenuItem* it); /* everything below and inside; leaves it clean */

RolltuiMenuItem* rolltui_menu_item_new(void);
void rolltui_menu_item_free(RolltuiMenuItem* it); /* a no-op on NULL */

void rolltui_menu_list_copy(RolltuiMenuItemList* to, const RolltuiMenuItemList* from);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MENU_TREE_H */
