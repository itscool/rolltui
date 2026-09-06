#ifndef ROLLTUI_C_STR_H
#define ROLLTUI_C_STR_H
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
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
#include <cstring>
extern "C" {
#endif

/* ---- the owned string ------------------------------------------------------------------ */

typedef struct RolltuiStr {
  char* p ROLLTUI_DEFAULT(nullptr); /* NUL-terminated when non-NULL; NULL is the empty string */
  size_t n ROLLTUI_DEFAULT(0);      /* bytes, not counting the NUL */
  size_t cap ROLLTUI_DEFAULT(0);    /* bytes allocated, including room for the NUL */

#ifdef __cplusplus
  // THE C++ SHAPE, cut to the user's rule (Phase 19 m2, 2026-09-06): a member may name rolltui's
  // own types and the C standard's, never a std:: container or view. What a host wants as a
  // std::string it converts in its own file, at the site that wants it. COPY IS DELETED:
  // `RolltuiStr a = b;` was a deep copy in C++ and a shallow alias in C — Phase 16 m6's
  // double-free, one type over — and the one spelling is `a.assign(b)`, which is
  // `rolltui_str_set`. MOVE stays: it is `rolltui_str_move` as a member. THE DESTRUCTOR STAYS:
  // RAII is the language's ownership model, `rolltui_str_free` is public and the C consumer
  // proves the pair, so it hides nothing. Every other member is one C function with `this`.
  RolltuiStr() = default;
  RolltuiStr(const RolltuiStr&) = delete;
  RolltuiStr& operator=(const RolltuiStr&) = delete;
  RolltuiStr(RolltuiStr&& o) noexcept : p(o.p), n(o.n), cap(o.cap) { o.p = nullptr; o.n = o.cap = 0; }
  RolltuiStr& operator=(RolltuiStr&& o) noexcept;
  RolltuiStr(const char* s) { assign(s); }  // NOLINT(google-explicit-constructor)
  ~RolltuiStr();
  RolltuiStr& operator=(const char* s) { assign(s); return *this; }
  void assign(const char* s, std::size_t len);
  void assign(const char* s) { assign(s, s ? std::strlen(s) : 0); }
  void assign(const RolltuiStr& o) { assign(o.p, o.n); }
  void append(const char* s, std::size_t len);
  void append(const RolltuiStr& o) { append(o.p, o.n); }
  RolltuiStr& operator+=(const char* s) { append(s, s ? std::strlen(s) : 0); return *this; }
  RolltuiStr& operator+=(char c) { append(&c, 1); return *this; }
  const char* c_str() const { return p ? p : ""; }
  const char* data() const { return p ? p : ""; }
  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  void clear();  // keeps the buffer — the reuse this type exists for
  bool eq(const char* s, std::size_t len) const;
  bool operator==(const char* o) const { return eq(o, o ? std::strlen(o) : 0); }
  bool operator==(const RolltuiStr& o) const { return eq(o.p, o.n); }
#endif
} RolltuiStr;

/* Replaces the contents. `s` may be NULL only when `len` is 0. Keeps the buffer when it
 * already fits, which is what makes a re-assigned name free after the first. */
void rolltui_str_set(RolltuiStr* s, const char* text, size_t len);
/* Appends. GROWING EXACT is still the right strategy: a name is built from a handful of
 * pieces, not a stream (that is what the markdown store's pools are for). */
void rolltui_str_append(RolltuiStr* s, const char* text, size_t len);
void rolltui_str_append_str(RolltuiStr* s, const RolltuiStr* o);
/* Empties WITHOUT releasing — `clear()` as a reset was shipped four times in Phase 13 and
 * is the one this file will not repeat (CLAUDE.md's design lens). */
void rolltui_str_clear(RolltuiStr* s);
/* Releases the buffer and zeroes the struct. Safe on a zeroed struct and on NULL. */
void rolltui_str_free(RolltuiStr* s);
/* Takes `from`'s buffer, releasing whatever `to` held. `from` is left empty and owning
 * nothing — the move the C++ side spells with `&&`, written down so the C has it too. */
void rolltui_str_move(RolltuiStr* to, RolltuiStr* from);
int rolltui_str_eq(const RolltuiStr* s, const char* text, size_t len);
/* A BORROW of the bytes, never NULL: the empty string reads back as "" with `*len` 0, so a
 * caller never branches on NULL to print a name. */
const char* rolltui_str_get(const RolltuiStr* s, size_t* len);

/* ---- the owned pointer array ----------------------------------------------------------- */
/* Deliberately NOT a generic vector over an element size: every array in this port holds
 * POINTERS to things it owns, and one shape that says so is worth more than a `void*`-and-
 * stride container that could hold anything and therefore states nothing. A caller frees
 * the elements; this frees the array. */

typedef struct RolltuiPtrVec {
  void** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiPtrVec;

void rolltui_ptrvec_push(RolltuiPtrVec* a, void* p);
/* Inserts at `i`, clamped to the end. */
void rolltui_ptrvec_insert(RolltuiPtrVec* a, size_t i, void* p);
/* Removes and RETURNS the element at `i` — the caller then owns it. NULL when out of range. */
void* rolltui_ptrvec_take(RolltuiPtrVec* a, size_t i);
void rolltui_ptrvec_clear(RolltuiPtrVec* a); /* keeps the array; the caller owns the elements */
void rolltui_ptrvec_free(RolltuiPtrVec* a);  /* releases the array; the caller owns the elements */


/* ---- the string SINK, for any function whose result is N strings ---------------------------
 * One `put` call per string, into whatever the caller is collecting. It lives here because
 * this is the string module; it was declared in `rolltui_presets.h` until Phase 17 m3, when
 * `rolltui_menu.h`'s tree walks needed the same shape and writing it twice would have been the
 * duplication rule firing. `s` is a BORROW valid for the call only. */
typedef void (*RolltuiPutFn)(void* ctx, const char* s, size_t len);

/* MANY STRINGS OUT, into a buffer the caller owns and reuses — the same shape `RolltuiStr` is
 * for ONE string, one dimension up, and the answer to the question `rolltui.h` rule 3 had for
 * text and did not have for lists (Phase 17 m4b). Zero-initialise; `_release` frees everything
 * and zeroes it; in C++ the destructor does that. `_add` appends a copy and returns a BORROW of
 * the stored entry, valid until the next `_add`. */
typedef struct RolltuiStrList {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiStrList() = default;
  RolltuiStrList(const RolltuiStrList&) = delete;
  RolltuiStrList& operator=(const RolltuiStrList&) = delete;
  ~RolltuiStrList();
  const RolltuiStr* begin() const { return v; }
  const RolltuiStr* end() const { return v + n; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
  const RolltuiStr& operator[](size_t i) const { return v[i]; }
#endif
} RolltuiStrList;

void rolltui_str_list_release(RolltuiStrList* l);
void rolltui_str_list_clear(RolltuiStrList* l); /* n = 0; every entry's buffer is KEPT for reuse */
RolltuiStr* rolltui_str_list_add(RolltuiStrList* l, const char* s, size_t len);
/* `RolltuiPutFn`-shaped over `_add`: pass this as `put` and the `RolltuiStrList*` as `ctx`. */
void rolltui_str_list_put(void* ctx, const char* s, size_t len);

/* THE BRIDGE from the sink shape to the buffer shape, so a caller who wants a `RolltuiStr`
 * out of a function that still takes a `RolltuiPutFn` writes no lambda: pass this as `put`
 * and the `RolltuiStr*` as `ctx`. APPENDS (it does not clear), so a multi-`put` walk
 * concatenates; clear the target first if that is not what you want.
 *
 * IT IS A BRIDGE AND NOT A BLESSING OF THE SINK SHAPE (Phase 17 m4b): a function whose result
 * the library ALREADY HAS takes the `RolltuiStr*` (one string) or the `RolltuiStrList*` (many)
 * directly, and every public one that took a sink was converted. `RolltuiPutFn` survives as an
 * INTERNAL plumbing shape — the library streams into its own `Buf` through it — and as the
 * type of the descriptor hooks a DOMAIN supplies, which is a decision going IN and not a result
 * coming out. `public_header_test` asserts no public function hands a result back through it. */
void rolltui_str_put(void* ctx, const char* s, size_t len);

#ifdef __cplusplus
} /* extern "C" */

namespace rolltui {
// The two names the rest of the library writes. `Str` is a value with one owner; `PtrVec`
// is deliberately NOT wrapped in RAII here, because every user of it owns elements of a
// different type and the FREEING of those elements is the thing that must stay visible at
// the owner (a layout node frees child nodes, `Windows` frees widgets). A RAII wrapper
// would have to take a deleter, which is a `std::function` at a place this port exists to
// remove one from.
using Str = RolltuiStr;
using PtrVec = RolltuiPtrVec;
}  // namespace rolltui

inline RolltuiStr::~RolltuiStr() { rolltui_str_free(this); }
inline RolltuiStrList::~RolltuiStrList() { rolltui_str_list_release(this); }
inline void RolltuiStr::assign(const char* s, std::size_t len) { rolltui_str_set(this, s, len); }
inline void RolltuiStr::append(const char* s, std::size_t len) { rolltui_str_append(this, s, len); }
inline bool RolltuiStr::eq(const char* s, std::size_t len) const { return rolltui_str_eq(this, s, len) != 0; }
inline void RolltuiStr::clear() { rolltui_str_clear(this); }
inline RolltuiStr& RolltuiStr::operator=(RolltuiStr&& o) noexcept {
  if (this != &o) rolltui_str_move(this, &o);
  return *this;
}
#endif

#endif /* ROLLTUI_C_STR_H */
