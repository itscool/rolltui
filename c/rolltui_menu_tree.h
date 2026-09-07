#ifndef ROLLTUI_C_MENU_TREE_H
#define ROLLTUI_C_MENU_TREE_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
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


/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
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
