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
#include <string>
#include <string_view>
extern "C" {
#endif

/* ---- the owned string ------------------------------------------------------------------ */

typedef struct RolltuiStr {
  char* p ROLLTUI_DEFAULT(nullptr); /* NUL-terminated when non-NULL; NULL is the empty string */
  size_t n ROLLTUI_DEFAULT(0);      /* bytes, not counting the NUL */
  size_t cap ROLLTUI_DEFAULT(0);    /* bytes allocated, including room for the NUL */

#ifdef __cplusplus
  RolltuiStr() = default;
  RolltuiStr(const RolltuiStr& o) { assign(o.view()); }
  RolltuiStr(RolltuiStr&& o) noexcept : p(o.p), n(o.n), cap(o.cap) { o.p = nullptr; o.n = o.cap = 0; }
  RolltuiStr(std::string_view s) { assign(s); }  // NOLINT(google-explicit-constructor)
  RolltuiStr(const char* s) { assign(s ? std::string_view(s) : std::string_view()); }  // NOLINT
  RolltuiStr(const std::string& s) { assign(s); }  // NOLINT(google-explicit-constructor)
  ~RolltuiStr();
  RolltuiStr& operator=(const RolltuiStr& o) {
    if (this != &o) assign(o.view());
    return *this;
  }
  RolltuiStr& operator=(RolltuiStr&& o) noexcept;
  RolltuiStr& operator=(std::string_view s) {
    assign(s);
    return *this;
  }
  RolltuiStr& operator=(const char* s) { return *this = (s ? std::string_view(s) : std::string_view()); }
  RolltuiStr& operator=(const std::string& s) { return *this = std::string_view(s); }

  void assign(std::string_view s);
  // APPEND, which a streaming entry does every token. `rolltui_str_append` grows exactly, so
  // a long stream reallocs per token — the same policy `std::string` hides behind doubling.
  // The transcript's own text is the markdown store's, not this; an entry's is written by a
  // host, and the honest answer for a host is the strategy it can see.
  RolltuiStr& operator+=(std::string_view s);
  RolltuiStr& operator+=(const char* s) { return *this += (s ? std::string_view(s) : std::string_view()); }
  RolltuiStr& operator+=(char c) { return *this += std::string_view(&c, 1); }
  std::string_view view() const { return std::string_view(p ? p : "", n); }
  operator std::string_view() const { return view(); }  // NOLINT(google-explicit-constructor)
  const char* c_str() const { return p ? p : ""; }
  const char* data() const { return p ? p : ""; }
  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  void clear();  // keeps the buffer — the reuse this type exists for
  std::string str() const { return std::string(view()); }
  // The three read-only `string_view` operations callers actually reach for. They return
  // VIEWS, never new strings — which is the borrow this type is for, said once here rather
  // than spelled out at twenty call sites.
  static constexpr std::size_t npos = std::string_view::npos;
  std::size_t find(std::string_view s, std::size_t pos = 0) const { return view().find(s, pos); }
  std::size_t find(char c, std::size_t pos = 0) const { return view().find(c, pos); }
  std::string_view substr(std::size_t pos, std::size_t count = npos) const { return view().substr(pos, count); }
  bool operator==(std::string_view o) const { return view() == o; }
  bool operator==(const char* o) const { return view() == (o ? std::string_view(o) : std::string_view()); }
  bool operator==(const RolltuiStr& o) const { return view() == o.view(); }
  // Written out rather than left to the `string_view` conversion, which would make
  // `str == some_std_string` AMBIGUOUS (two equally good conversions) at every call site.
  bool operator==(const std::string& o) const { return view() == o; }
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

// The two concatenations the library's report-building actually writes. `std::string` has
// no `operator+` for a `string_view`, so without these every `where + ": " + n.id` in a
// loader would have to spell a conversion — which would make this port's diff about
// spelling instead of about ownership.
inline std::string operator+(const std::string& a, const RolltuiStr& b) {
  std::string s = a;
  s.append(b.view());
  return s;
}
inline std::string operator+(const RolltuiStr& a, const char* b) {
  std::string s(a.view());
  s += b;
  return s;
}

inline RolltuiStr::~RolltuiStr() { rolltui_str_free(this); }
inline void RolltuiStr::assign(std::string_view s) { rolltui_str_set(this, s.data(), s.size()); }
inline RolltuiStr& RolltuiStr::operator+=(std::string_view s) {
  rolltui_str_append(this, s.data(), s.size());
  return *this;
}
inline void RolltuiStr::clear() { rolltui_str_clear(this); }
inline RolltuiStr& RolltuiStr::operator=(RolltuiStr&& o) noexcept {
  if (this != &o) rolltui_str_move(this, &o);
  return *this;
}
#endif

#endif /* ROLLTUI_C_STR_H */
