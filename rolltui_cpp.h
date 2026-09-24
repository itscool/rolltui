#ifndef ROLLTUI_CPP_H
#define ROLLTUI_CPP_H
/*
 * rolltui/rolltui_cpp.h — the C++ half of every type `rolltui.h` declares as plain C, so
 * `rolltui.h` itself can be just types, data and functions.
 *
 * C++ cannot add a method to a struct from a second header — a method, including a
 * destructor, has to be declared inside the struct's own original definition; there is no
 * reopening a class later. So a type's C++ half moves here in one of two shapes:
 *
 *   a free function      for anything that needs no membership at all: every `operator==`
 *                         here is one, in the global namespace so argument-dependent lookup
 *                         finds it — the same reason `str.hpp` puts RolltuiStr's `operator+=`/
 *                         `operator+` there rather than in `namespace rolltui`.
 *   rename + derive       for anything that DOES need member syntax (a static factory, an
 *                         instance method, a destructor). The plain-data struct keeps living in
 *                         `rolltui.h` under a `Raw` suffix (`RolltuiFoo` -> `RolltuiFooRaw`);
 *                         this file declares `class RolltuiFoo : public RolltuiFooRaw { ... };`
 *                         under the ORIGINAL name, in the global namespace, so it reclaims the
 *                         identifier every existing call site already uses. Public single
 *                         inheritance over a POD base with no virtual functions costs nothing —
 *                         identical `sizeof`, identical layout — and a `RolltuiFoo*` converts
 *                         implicitly to `RolltuiFooRaw*`, so a C function's existing signature
 *                         (`void rolltui_thing(..., RolltuiFooRaw* out)`) keeps compiling at
 *                         every call site with no change beyond this file being `#include`d.
 *
 * NOT `RolltuiStr` ITSELF, and not `str.hpp`'s job. `RolltuiStr`'s own minimal RAII stays
 * directly on the struct in `rolltui.h`, the same way it already did before this file existed —
 * it is the one type with far too much reach (hundreds of call sites) to move under this same
 * pass, and `str.hpp` already sits beside it as a separate, established convenience layer
 * (`StrView`, `StrVec`) rather than a replacement for its own destructor. This file complements
 * `str.hpp`; it does not merge with it or touch it.
 *
 * NOT the enum-class-vs-byte fields either (`RolltuiMouseEvent::kind` and its seven siblings):
 * those are a DATA FIELD typed differently per language, not a bolted-on method, and no
 * mechanism here moves a field's declared type without either duplicating it or turning a
 * direct field read into a method call everywhere for no real gain. They stay exactly as
 * `rolltui.h` already declares them.
 */

#include "rolltui/rolltui.h"

/* ---- pure operators: every field compared, no membership needed --------------------------- */

constexpr bool operator==(const RolltuiMouseEvent& a, const RolltuiMouseEvent& b) {
  return a.kind == b.kind && a.x == b.x && a.y == b.y && a.button == b.button && a.ctrl == b.ctrl &&
         a.alt == b.alt && a.shift == b.shift;
}

constexpr bool operator==(const RolltuiStyle& a, const RolltuiStyle& b) {
  return a.fg == b.fg && a.bg == b.bg && a.bold == b.bold && a.italic == b.italic && a.underline == b.underline &&
         a.dim == b.dim && a.reverse == b.reverse;
}

inline bool operator==(const RolltuiMenuOptions& a, const RolltuiMenuOptions& b) {
  return a.ambiguous_wide == b.ambiguous_wide && a.inset == b.inset;
}

inline bool operator==(const RolltuiTranscriptOptions& a, const RolltuiTranscriptOptions& b) {
  return a.ambiguous_wide == b.ambiguous_wide && a.tab_width == b.tab_width && a.gap == b.gap &&
         a.inset == b.inset && a.wheel_lines == b.wheel_lines && a.code_fold_over_lines == b.code_fold_over_lines &&
         a.code_cap_lines == b.code_cap_lines && a.multi_click_ms == b.multi_click_ms;
}

constexpr bool operator==(const RolltuiTextPos& a, const RolltuiTextPos& b) {
  return a.entry == b.entry && a.offset == b.offset && a.length == b.length;
}

constexpr bool operator==(const RolltuiFindMatch& a, const RolltuiFindMatch& b) {
  return a.entry == b.entry && a.offset == b.offset && a.length == b.length;
}

inline bool operator==(const RolltuiPlacement& a, const RolltuiPlacement& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.anchor == b.anchor && a.clamp == b.clamp &&
         a.edge_margin == b.edge_margin && a.min_w == b.min_w && a.min_h == b.min_h && a.max_w == b.max_w &&
         a.max_h == b.max_h;
}

/* ---- RolltuiRect: every instance method here is a free function too, not just its operator==,
 * because a layout node's `outer`/`inner` are RolltuiRect FIELDS — `rn.outer.contains(x, y)` calls a
 * method on a NESTED field, whose static type is RolltuiRect regardless of what wraps it. A
 * method declared on a derived class would not be visible through that field; a free function is. */
inline bool operator==(const RolltuiRect& a, const RolltuiRect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
inline bool contains(const RolltuiRect& r, int px, int py) {
  return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}
inline bool empty(const RolltuiRect& r) { return r.w <= 0 || r.h <= 0; }
inline RolltuiRect intersect(const RolltuiRect& a, const RolltuiRect& o) {
  int r[4];
  rolltui_rect_intersect(a.x, a.y, a.w, a.h, o.x, o.y, o.w, o.h, r);
  return RolltuiRect{r[0], r[1], r[2], r[3]};
}

#endif /* ROLLTUI_CPP_H */
