#ifndef ROLLTUI_C_STR_H
#define ROLLTUI_C_STR_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_str.h — an owned string and an owned pointer array.
 *
 * These are the two containers the library holds its own long-lived data in: a layout node's
 * names, a menu item's, the child arrays of the recursive trees, and the map keying a widget by
 * its content. They exist as NAMED types built from the closed allocation set in
 * `rolltui_alloc.h`, rather than as `char* p; size_t n, cap;` triples open-coded per module.
 *
 *   RolltuiStr       an owned, NUL-terminated byte string. GROWING EXACT: an assign knows its
 *                    final size, and a name is written once and read many times.
 *   RolltuiPtrVec    an owned array of `void*` slots. GROWING AMORTISED: it is APPENDED to
 *                    (children, layers, rows) and its final size is not known in advance.
 *
 * ---- ONE DEFINITION, TWO LANGUAGES -----------------------------------------------------
 *
 * `rolltui::Str` and `rolltui::PtrVec` ARE these structs, so a layout node has one layout and
 * both languages compile it. Under `__cplusplus` they additionally carry the five special
 * members, and those members CALL THE C FUNCTIONS BELOW: there is one implementation of
 * "release this buffer" and the C++ destructor is a caller of it. Two languages managing one
 * buffer with two mechanisms would be a double-free waiting to happen; two languages calling
 * one mechanism is what the boundary is for.
 *
 * The C++ side also keeps `.empty()`, `.size()`, `==` against a `string_view` and assignment
 * from one, so `n.id == "input"` compiles and means what it reads as.
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


/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
void rolltui_ptrvec_clear(RolltuiPtrVec* a); /* keeps the array; the caller owns the elements */

/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
void rolltui_str_append_str(RolltuiStr* s, const RolltuiStr* o);

void rolltui_str_list_clear(RolltuiStrList* l); /* n = 0; every entry's buffer is KEPT for reuse */
RolltuiStr* rolltui_str_list_add(RolltuiStrList* l, const char* s, size_t len);


/* ---- INTERNAL: no consumer, host suite or roll test reaches these, and no public shape
 * needs them. ---- */
/* A BORROW of the bytes, never NULL: the empty string reads back as "" with `*len` 0, so a
 * caller never branches on NULL to print a name. */
const char* rolltui_str_get(const RolltuiStr* s, size_t* len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_STR_H */
