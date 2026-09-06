#ifndef ROLLTUI_C_STR_H
#define ROLLTUI_C_STR_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_str.h — AN OWNED STRING AND AN OWNED ARRAY, ONCE (Phase 15 m5).
 *
 * m5 is the milestone where the C has to HOLD the library's own long-lived data: a layout
 * node's three names, a menu item's five, the child arrays of two recursive trees, and the
 * `Windows` map that keys a widget by its content. `std::string` and `std::vector` did all
 * of that in the C++ and did it INVISIBLY, which is the whole finding of Phase 14's design
 * lens — *a default is an answer to a question nobody asked*.
 *
 * ---- WHY THIS FILE EXISTS RATHER THAN THE ALTERNATIVE ---------------------------------
 *
 * The alternative was eight independently written `char* p; size_t n, cap;` triples, which
 * is precisely what `rolltui_alloc.h`'s own header records happening in m2/m3 with growth
 * and what Phase 13 found seven times in the C++. So this is the same fix one level up: a
 * NAMED type built out of the closed allocation set, rather than the set open-coded per
 * module.
 *
 *   RolltuiStr       an owned, NUL-terminated byte string. Strategy 3, GROWING EXACT: an
 *                    assign knows its size, and a name is written once and read forever.
 *   RolltuiPtrVec    an owned array of `void*` slots. Strategy 2, GROWING AMORTISED: it is
 *                    APPENDED to (children, layers, rows), and its final size is not known.
 *
 * ---- ONE DEFINITION, AND WHAT THAT COSTS IN C++ ----------------------------------------
 *
 * `rolltui::Str` and `rolltui::PtrVec` ARE these structs — the Phase 14 rule, so a layout
 * node has one layout and both languages compile it. In C++ they additionally carry the
 * five special members, and **those call exactly the C functions below**: there is one
 * implementation of "release this buffer", and the C++ destructor is a caller of it, the
 * same shape `Frame` and `Bindings` already use one level up with a `unique_ptr` deleter.
 * Two languages managing one buffer with two mechanisms is the thing that would be wrong;
 * two languages calling one mechanism is what a boundary is for.
 *
 * The C++ side keeps `.empty()`, `.size()`, `==` against a `string_view` and assignment
 * from one, so a host that writes `n.id == "input"` still says what it said. That is not
 * politeness to hosts: it is what keeps the DIFF of this port about ownership rather than
 * about spelling, which is the only way its line ratio means anything (plan/phase-15.md).
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif
void rolltui_ptrvec_push(RolltuiPtrVec* a, void* p);
/* Inserts at `i`, clamped to the end. */
void rolltui_ptrvec_insert(RolltuiPtrVec* a, size_t i, void* p);
/* Removes and RETURNS the element at `i` — the caller then owns it. NULL when out of range. */
void* rolltui_ptrvec_take(RolltuiPtrVec* a, size_t i);
void rolltui_ptrvec_free(RolltuiPtrVec* a);  /* releases the array; the caller owns the elements */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_STR_H */
