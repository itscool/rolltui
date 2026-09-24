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

#include <cstddef>
#include <cstring>
#include <utility>

#include "rolltui/rolltui.h"

/* Forward: RolltuiStyle's operator== below compares nested RolltuiStyleColorRaw fields, and needs
 * this visible at its own definition (an ordinary function's body resolves names where it is
 * written, unlike a template's two-phase lookup) — the real definition is with the rest of
 * RolltuiStyleColor's C++ half, further down. */
bool operator==(const RolltuiStyleColorRaw& a, const RolltuiStyleColorRaw& b);

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

/* ---- rename + derive: static factories and instance methods, none of them ever called on a
 * NESTED field of another struct (checked per type before it landed here — a false assumption
 * would show up as a compile error at the one real call site, not silently). */

class RolltuiStyleColor : public RolltuiStyleColorRaw {
 public:
  RolltuiStyleColor() = default;
  constexpr RolltuiStyleColor(const RolltuiStyleColorRaw& r) : RolltuiStyleColorRaw(r) {}  // NOLINT(google-explicit-constructor)
  static constexpr RolltuiStyleColor none() { return {}; }
  static constexpr RolltuiStyleColor indexed(unsigned char i) {
    RolltuiStyleColor c;
    c.kind = Kind::Indexed;
    c.index = i;
    return c;
  }
  static constexpr RolltuiStyleColor rgb(unsigned char red, unsigned char green, unsigned char blue) {
    RolltuiStyleColor c;
    c.kind = Kind::Rgb;
    c.r = red;
    c.g = green;
    c.b = blue;
    return c;
  }
};
inline bool operator==(const RolltuiStyleColorRaw& a, const RolltuiStyleColorRaw& b) {
  return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

class RolltuiCell : public RolltuiCellRaw {
 public:
  RolltuiCell() = default;
  static constexpr unsigned char kInlineGlyph = ROLLTUI_CELL_INLINE_GLYPH;
  static constexpr unsigned char kSpilled = ROLLTUI_CELL_SPILLED;
  bool spilled() const { return len == kSpilled; }
};
inline bool operator==(const RolltuiCellRaw& a, const RolltuiCellRaw& b) {
  if (a.link != b.link || a.style != b.style || a.len != b.len || a.width != b.width ||
      a.continuation != b.continuation)
    return false;
  for (unsigned char i = 0; i < ROLLTUI_CELL_INLINE_GLYPH; ++i)
    if (a.bytes[i] != b.bytes[i]) return false;
  return true;
}

class RolltuiInputSelection : public RolltuiInputSelectionRaw {
 public:
  RolltuiInputSelection() = default;
  size_t begin() const { return anchor < head ? anchor : head; }
  size_t end() const { return anchor < head ? head : anchor; }
  bool empty() const { return !active || anchor == head; }
};
inline bool operator==(const RolltuiInputSelectionRaw& a, const RolltuiInputSelectionRaw& b) {
  return a.anchor == b.anchor && a.head == b.head && a.active == b.active;
}

/* `range_in` is dropped: declared with no definition anywhere and no caller — the real work
 * already happens through `rolltui_selection_range_in` (c/rolltui_widget_transcript.h). */
class RolltuiSelection : public RolltuiSelectionRaw {
 public:
  RolltuiSelection() = default;
  bool empty() const { return !active; }
  RolltuiTextPos first() const { return rolltui_text_pos_less(&head, &anchor) ? head : anchor; }
  RolltuiTextPos last() const { return rolltui_text_pos_less(&head, &anchor) ? anchor : head; }
};

/* RolltuiEffectOut/RolltuiEffectSpec stay their ORIGINAL, unrenamed selves — free functions only,
 * no derive — because both cross a real plugin boundary: a host's own `RolltuiEffectFn`
 * implementation receives them as `const RolltuiEffectSpec*`/`RolltuiEffectOut*` callback
 * PARAMETERS (rolltui.h's own typedef), and calls a method through that pointer with `->`, not
 * `.` — `s->role(0)`, `out->set_glyph(...)` (tests/effects_test.cpp's `host_sweep_kind`). The
 * pointer's static type is fixed by the callback signature; a method on a derived class would
 * not be reachable through it, the same reason RolltuiRect's methods are free functions above. */
inline void set_glyph(RolltuiEffectOut& out, const char* g, std::size_t n) {
  out.has_glyph = 1;
  out.glyph_len = n;
  if (n <= ROLLTUI_EFFECT_GLYPH_MAX && n != 0) std::memcpy(out.glyph, g, n);
}
inline std::size_t roles_size(const RolltuiEffectSpec& s) { return s.role_count; }
inline unsigned char role(const RolltuiEffectSpec& s, std::size_t i) { return s.roles[i % s.role_count]; }

class RolltuiSplitSize : public RolltuiSplitSizeRaw {
 public:
  RolltuiSplitSize() = default;
  constexpr RolltuiSplitSize(const RolltuiSplitSizeRaw& r) : RolltuiSplitSizeRaw(r) {}  // NOLINT(google-explicit-constructor)
  static constexpr RolltuiSplitSize fixed(RolltuiDim d) {
    RolltuiSplitSize s{};
    s.fill = 0;
    s.weight = 1;
    s.dim = d;
    return s;
  }
  static constexpr RolltuiSplitSize filling(int w = 1) {
    RolltuiSplitSize s{};
    s.fill = 1;
    s.weight = w;
    return s;
  }
};
inline bool operator==(const RolltuiSplitSizeRaw& a, const RolltuiSplitSizeRaw& b) {
  return a.fill == b.fill && a.weight == b.weight && a.dim == b.dim;
}

class RolltuiInputOptions : public RolltuiInputOptionsRaw {
 public:
  /* The default prompt: the one special member `RolltuiInputOptions` had beyond field defaults. */
  RolltuiInputOptions() { rolltui_str_set(&prompt, "> ", 2); }
  /* `operator==` is dropped here too: declared with no definition anywhere and no caller. */
};

class RolltuiInputSpec : public RolltuiInputSpecRaw {
 public:
  RolltuiInputSpec() = default;
  RolltuiInputSpec clone() const {
    RolltuiInputSpec out;
    rolltui_input_spec_copy(&out, this);
    return out;
  }
};
inline bool operator==(const RolltuiInputSpecRaw& a, const RolltuiInputSpecRaw& b) {
  return rolltui_input_spec_equal(&a, &b) != 0;
}

/* RolltuiMenuItem/RolltuiMenuItemList stay their ORIGINAL, unrenamed selves — not touched at all.
 * `RolltuiMenuItem::children` IS a `RolltuiMenuItemList`, and `.children.push_back(...)`/`.size()`/
 * `.empty()`/`.clear()`/iteration are called on that NESTED field at ~80 call sites across
 * tools/ and tests/ — converting all of them to free-function syntax is a much bigger, riskier
 * mechanical change than this pass is for, and RolltuiMenuItemList's element type BEING
 * RolltuiMenuItem means the two cannot be split apart from each other either. Left as they were. */

class RolltuiNote : public RolltuiNoteRaw {
 public:
  // No constructor, destructor or assignment beyond the converting ones below: `text` (a `RolltuiStr`) already has correct copy, move
  // and destroy.
  RolltuiNote() = default;
  // Implicit from a C string on purpose: a host with no motion to report writes `return "working";`. Anything else sets the text by
  // pointer and length.
  RolltuiNote(const char* t) { text.assign(t); }  // NOLINT(google-explicit-constructor)
  RolltuiNote(const char* t, std::size_t n, rolltui::EffectState s, unsigned long long since = 0) {
    state = s;
    since_ms = since;
    text.assign(t, n);
  }
  // Text only: the state and its clock reset, which is what "a plain note" means.
  RolltuiNote& set(const char* t, std::size_t n) {
    text.assign(t, n);
    state = static_cast<rolltui::EffectState>(0);
    since_ms = 0;
    return *this;
  }
  RolltuiNote& operator=(const char* t) { return set(t, t ? std::strlen(t) : 0); }
};

/* ---- rename + derive: the destructor-bearing "out-param" types, smallest blast radius first. --
 * A `RolltuiPresetList list; store_list(&list);` pattern already covers every real call site — a
 * local declared as the derived type, its address upcast implicitly to the C function's
 * `...Raw*` parameter, no other change needed. */

class RolltuiMenuActionList : public RolltuiMenuActionListRaw {
 public:
  RolltuiMenuActionList() = default;
  RolltuiMenuActionList(const RolltuiMenuActionList&) = delete;
  RolltuiMenuActionList& operator=(const RolltuiMenuActionList&) = delete;
  ~RolltuiMenuActionList() { rolltui_menu_action_list_release(this); }
  const RolltuiMenuAction* begin() const { return v; }
  const RolltuiMenuAction* end() const { return v + n; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
};

class RolltuiDocEntry : public RolltuiDocEntryRaw {
 public:
  RolltuiDocEntry() = default;
  // COPY IS DELETED: spell it `clone()` (`rolltui_doc_entry_copy`). Move and the destructor stay.
  RolltuiDocEntry(const RolltuiDocEntry&) = delete;
  RolltuiDocEntry(RolltuiDocEntry&& o) noexcept
      : RolltuiDocEntryRaw{std::move(o.id),
                           o.version,
                           std::move(o.text),
                           o.markdown,
                           o.role,
                           std::move(o.prefix),
                           o.prefix_role,
                           o.foldable,
                           std::move(o.summary),
                           o.folded,
                           o.state,
                           o.progress,
                           o.state_since_ms} {}
  RolltuiDocEntry& operator=(const RolltuiDocEntry&) = delete;
  RolltuiDocEntry& operator=(RolltuiDocEntry&& o) noexcept {
    if (this != &o) {
      id = std::move(o.id);
      version = o.version;
      text = std::move(o.text);
      markdown = o.markdown;
      role = o.role;
      prefix = std::move(o.prefix);
      prefix_role = o.prefix_role;
      foldable = o.foldable;
      summary = std::move(o.summary);
      folded = o.folded;
      state = o.state;
      progress = o.progress;
      state_since_ms = o.state_since_ms;
    }
    return *this;
  }
  ~RolltuiDocEntry() = default;
  RolltuiDocEntry clone() const {
    RolltuiDocEntry out;
    rolltui_doc_entry_copy(&out, this);
    return out;
  }
};

/* Every element `v[i]` is heap-allocated by `rolltui_document_add`, a C function, as a plain
 * `RolltuiDocEntryRaw` — the `static_cast<RolltuiDocEntry*>` below reads it through the derived
 * type without having constructed one there. Sound because RolltuiDocEntry adds no data member
 * and no virtual function over its base: the two have identical layout, and every member this
 * class calls through the cast is non-virtual, so no vtable or object-identity check is ever
 * involved — the same reasoning that makes the whole rename+derive mechanism zero-cost applies
 * here too, just applied to an element the C side allocated rather than one this file did. */
class RolltuiDocument : public RolltuiDocumentRaw {
 public:
  struct iterator {
    RolltuiDocEntryRaw** p;
    RolltuiDocEntry& operator*() const { return *static_cast<RolltuiDocEntry*>(*p); }
    RolltuiDocEntry* operator->() const { return static_cast<RolltuiDocEntry*>(*p); }
    iterator& operator++() {
      ++p;
      return *this;
    }
    bool operator==(const iterator& o) const { return p == o.p; }
  };
  struct const_iterator {
    RolltuiDocEntryRaw* const* p;
    const RolltuiDocEntry& operator*() const { return *static_cast<const RolltuiDocEntry*>(*p); }
    const RolltuiDocEntry* operator->() const { return static_cast<const RolltuiDocEntry*>(*p); }
    const_iterator& operator++() {
      ++p;
      return *this;
    }
    bool operator==(const const_iterator& o) const { return p == o.p; }
  };

  RolltuiDocument() = default;
  RolltuiDocument(const RolltuiDocument&) = delete;  /* clone() is the spelling */
  RolltuiDocument(RolltuiDocument&& o) noexcept : RolltuiDocumentRaw{o.v, o.n, o.cap} {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiDocument& operator=(const RolltuiDocument&) = delete;
  RolltuiDocument& operator=(RolltuiDocument&& o) noexcept {
    if (this != &o) {
      rolltui_document_release(this);
      v = o.v;
      n = o.n;
      cap = o.cap;
      o.v = nullptr;
      o.n = o.cap = 0;
    }
    return *this;
  }
  ~RolltuiDocument() { rolltui_document_release(this); }
  RolltuiDocument clone() const {
    RolltuiDocument out;
    rolltui_document_copy(&out, this);
    return out;
  }

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiDocEntry& operator[](std::size_t i) { return *static_cast<RolltuiDocEntry*>(v[i]); }
  const RolltuiDocEntry& operator[](std::size_t i) const { return *static_cast<const RolltuiDocEntry*>(v[i]); }
  RolltuiDocEntry& back() { return *static_cast<RolltuiDocEntry*>(v[n - 1]); }
  const RolltuiDocEntry& back() const { return *static_cast<const RolltuiDocEntry*>(v[n - 1]); }
  iterator begin() { return {v}; }
  iterator end() { return {v + n}; }
  const_iterator begin() const { return {v}; }
  const_iterator end() const { return {v + n}; }
  void push_back(RolltuiDocEntry&& e) { *static_cast<RolltuiDocEntry*>(rolltui_document_add(this)) = std::move(e); }
  void push_back(const RolltuiDocEntry& e) { rolltui_doc_entry_copy(rolltui_document_add(this), &e); }
  void clear() { rolltui_document_clear(this); }
  /* Trims or grows, KEEPING the storage past the end: a transcript that trims and refills wants the entries back. */
  void resize(std::size_t k) {
    while (n > k) rolltui_doc_entry_release(v[--n]);
    while (n < k) rolltui_document_add(this);
  }
};

#endif /* ROLLTUI_CPP_H */
