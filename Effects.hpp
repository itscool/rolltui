#pragma once
//
// rolltui/Effects.hpp — MOTION IS THE THEME'S, A STATE IS THE WIDGET'S (plan/phase-12.md,
// milestone 6; designed 2026-09-02 with the user and reproduced in plan/phase-10.md).
//
// A WIDGET NEVER ANIMATES. It MARKS a span of cells with a STATE — `waiting` (no
// progress information), `streaming` (bytes arriving), `progress` (a fraction), `flash`
// (a moment's attention) — and stops there. The THEME maps state → effect, so a mono
// theme can say `waiting` is an ASCII spinner while a truecolor theme says braille, and
// a theme that maps NOTHING is a still UI: same app, same widget code, no motion. That
// last one is also the degrade rung for a dumb terminal, which is why it is the default
// rather than a switch.
//
// AN EFFECT IS A PURE FUNCTION — `(elapsed, cell index, span length, fraction, base
// style) → (glyph override, style override)` — which is what makes a frame with motion
// in it a golden frame like any other: `rolltui-studio --frame WxH --tick N` renders the
// frame AT elapsed N and prints the same bytes every time.
//
// THE TWO PROPERTIES, and they hold for EVERY kind, built-in or a host's, because the
// APPLIER enforces them rather than trusting the function (rolltui/tests/effects_test.cpp
// asserts both over every registered kind):
//   1. AN EFFECT NEVER CHANGES A SPAN'S WIDTH. A glyph override lands only when its
//      display width equals the width of the cell it would replace; otherwise it is
//      dropped and COUNTED (EffectReport::glyphs_refused), never written. Wrapping,
//      selection offsets and the frame diff all depend on a cell's width, so this is a
//      guarantee and not a convention a host's callback could break.
//   2. AN EFFECT NEVER WRITES OUTSIDE ITS SPAN. The function returns a value for ONE
//      cell and cannot address any other; the applier is the only writer and it walks
//      only the span's own cells. A 2-cell glyph whose second half lies outside the span
//      is skipped WHOLE — writing its continuation would be a write outside the span,
//      and leaving the halves in two styles would be a seam.
//
// THE TICK RUNS ONLY WHILE SOMETHING IS MARKED. `effect_tick_ms` answers nullopt for a
// frame with no marks, and for one whose marks the theme maps to nothing or to nothing
// that MOVES (`period_ms` 0 — a progress bar is a still picture of a number, and a
// number changing is already a redraw). `poll_timeout_ms` is the whole of a host's
// decision, so "an idle screen costs nothing" is one function to check rather than a
// habit in three event loops.
//
// A KIND IS RESOLVED THROUGH TWO RUNGS, the same shape as a widget kind (Layout.hpp) and
// a menu file (Widgets.hpp): the library's CLOSED table first and never shadowed, then
// whatever a HOST registered. And, exactly as for a widget kind, WHETHER A KIND EXISTS IS
// A HOST FACT — the theme loader does not judge a kind name (a theme file is read long
// before a host has registered anything), so an unknown kind is named by the APPLIER, in
// `EffectReport::unknown_kinds`, where a host can say it out loud. It is never a silent
// nothing.
//
// WHAT AN EFFECT MAY NOT DO: invent a colour. Every built-in picks between the cell's
// base style and the style of a ROLE the theme named, or toggles an attribute. There is
// no blending, and that is deliberate three times over — the library's "no colour
// literal outside Theme.cpp" grep control stays green, a theme keeps sole authorship of
// its palette, and an effect still reads at every colour depth including mono, where the
// roles differ by attributes alone.
//
// THE BUILT-IN KINDS, all DATA in the theme file (Theme.hpp has the format):
//   spinner   the frames cycle on the span's FIRST `w` cells (w = the frames' width)
//   ellipsis  the same cycle on the span's LAST `w` cells — one mechanism, two anchors,
//             because the anchor is the whole of the difference and a theme needs a name
//             for each
//   bar       cells before `fraction` of the span take roles[0]; still (period_ms 0)
//   pulse     the WHOLE span takes roles[step % n] — a two-role list breathes, a
//             four-role one ramps; it never shows the base style
//   shimmer   a window of `width` cells sweeps the span, taking roles[0]; the rest is base
//   gradient  the roles laid ALONG the span and rotated one step per tick
//   blink     the whole span is roles[0] for the first half of each period and the BASE
//             for the second — the one kind that shows the base, which is what makes it
//             read as attention rather than as colour
//
// PHASE 15 m2 — THE ENGINE IS BEHIND A C BOUNDARY. The kinds, the registry, the applier
// and the tick live in `rolltui/c/rolltui_effects.h`, in one of two implementations chosen
// by `-DROLLTUI_C` (`EffectsCpp.cpp` or `c/rolltui_effects.c`); this header is the
// vocabulary, the theme's data and the C++ shape. `EffectCell` and `EffectOut` ARE the C
// structs (one definition, Phase 14 m2's rule), so a host's kind reads and writes exactly
// the bytes the applier does.
//
// TWO THINGS A CALLER CAN SEE, both forced by the boundary rather than chosen:
//   - `effect_kind()` became `effect_kind_resolves()`. A resolved kind is now a function
//     and a context inside the registry rather than an object with an address, so there is
//     nothing to hand back a pointer to — and every caller only ever asked "does this
//     resolve?" anyway. Same shape as `Frame::marks()` becoming count-plus-index in m2.
//   - `EffectOut::glyph` is an inline buffer with a stated cap, set through `set_glyph()`
//     and read through `glyph_view()`. An override past the cap is refused and counted in
//     `glyphs_refused`, exactly as one of the wrong width is — see the C header for why a
//     cap is right here and a spill is right for a `Cell`.
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Style.hpp"
#include "rolltui/c/rolltui_effects.h"

namespace rolltui {

class Frame;   // Screen.hpp — the applier's target
struct Theme;  // Theme.hpp — where the state → effect map lives

// ---- what a WIDGET says ---------------------------------------------------------------

enum class EffectState : std::uint8_t { None, Waiting, Streaming, Progress, Flash, count_ };
inline constexpr std::size_t kEffectStateCount = static_cast<std::size_t>(EffectState::count_);
std::string_view effect_state_name(EffectState s);           // "none" | "waiting" | ...
EffectState effect_state_from_name(std::string_view name);   // EffectState::count_ when unknown

// One marked span: `cells` cells from (x, y) on ONE row. `since_ms` is when the span
// entered the state — its own phase, so a spinner starts at its first frame when the turn
// starts; 0 means "no epoch of its own", and every span of that state then moves together
// off the shared clock, which is what a widget with nothing to remember should do.
struct Mark {
  int x = 0, y = 0, cells = 0;
  EffectState state = EffectState::None;
  std::uint64_t since_ms = 0;
  double fraction = 0;  // Progress: 0..1, clamped by the applier
  bool operator==(const Mark&) const = default;
};

// ---- what a THEME says ------------------------------------------------------------------

// ONE SPEC, AS THE THEME OWNS IT (Phase 15 m3). This is `RolltuiEffectSpec` — the struct the
// applier reads and a host's kind is handed — not a copy of its type: the m2 boundary lent a
// C++ `EffectSpec` through a view built in `fill_view`, and closing that seam meant the view
// BECOMING the definition, which is what its own header said would happen.
//
// So a spec is a BORROW of the `EffectMap` that owns it, valid until that map next changes,
// and it is READ here and WRITTEN through `EffectMap::add` below. `roles` is one byte per
// entry and `role_count` is never zero — a spec with no roles of its own borrows the map's
// fallback, which the map was handed once rather than each rung guessing at it.
using EffectSpec = RolltuiEffectSpec;

// The theme's whole answer: what each state looks like. Empty for a state the theme did
// not name — a still UI, the degrade rung.
//
// OWNED (CLAUDE.md's fourth strategy), through a `unique_ptr` with a deleter that calls the
// C free — the same shape `Frame` and `KeyDecoder` use for their handles.
class EffectMap {
 public:
  struct Handle {
    void operator()(RolltuiEffectMap* p) const { rolltui_effect_map_free(p); }
  };
  // THE FALLBACK ROLE IS HANDED OVER ONCE, HERE. It is the only place in the library that
  // says which role an effect with none of its own picks, and it is on this side because a
  // role is the styling vocabulary and the C names none of it.
  EffectMap() : m_(rolltui_effect_map_new(kEffectStateCount, static_cast<unsigned char>(Role::accent_1))) {}
  EffectMap(const EffectMap& o) : m_(rolltui_effect_map_clone(o.m_.get())) {}
  EffectMap& operator=(const EffectMap& o) {
    if (this != &o) m_.reset(rolltui_effect_map_clone(o.m_.get()));
    return *this;
  }
  EffectMap(EffectMap&&) = default;
  EffectMap& operator=(EffectMap&&) = default;

  bool empty() const { return rolltui_effect_map_empty(m_.get()) != 0; }
  bool operator==(const EffectMap& o) const { return rolltui_effect_map_equal(m_.get(), o.m_.get()) != 0; }
  void clear() { rolltui_effect_map_clear(m_.get()); }

  std::size_t count(EffectState s) const { return rolltui_effect_map_count(m_.get(), index(s)); }
  // A BORROW, valid until this map next changes.
  const EffectSpec& at(EffectState s, std::size_t i) const { return *rolltui_effect_map_at(m_.get(), index(s), i); }

  // A spec is BUILT rather than handed over whole, because its three arrays are
  // variable-length and building is what keeps them the map's allocations. `add` returns
  // the new spec's index for the two fillers.
  std::size_t add(EffectState s, std::string_view kind, int period_ms = 800, int width = 0, int steps = 0,
                  bool backward = false) {
    return rolltui_effect_map_add(m_.get(), index(s), kind.data(), kind.size(), period_ms, width, steps, backward);
  }
  void add_frame(EffectState s, std::size_t i, std::string_view frame) {
    rolltui_effect_map_add_frame(m_.get(), index(s), i, frame.data(), frame.size());
  }
  void add_role(EffectState s, std::size_t i, Role r) {
    rolltui_effect_map_add_role(m_.get(), index(s), i, static_cast<unsigned char>(r));
  }

  const RolltuiEffectMap* handle() const { return m_.get(); }

 private:
  static std::size_t index(EffectState s) {
    return static_cast<std::size_t>(s) < kEffectStateCount ? static_cast<std::size_t>(s) : 0;
  }
  std::unique_ptr<RolltuiEffectMap, Handle> m_;
};

// ---- the pure function ------------------------------------------------------------------

// ONE DEFINITION (Phase 14 m2's rule, applied in m2 of this phase): both are declared in
// `rolltui/c/rolltui_effects.h` and compiled by both languages, so there is nothing to
// convert at the seam and nothing to drift.
//
//   EffectCell  everything a kind is allowed to know about the cell it answers for:
//               elapsed_ms (since the span entered the state), index (0-based, within the
//               span), length (the span, in cells), fraction (Progress: 0..1), base (the
//               cell's style as drawn — a stacked kind sees the previous one's), and
//               ambiguous_wide, so a kind measures its own frames the way the applier will.
//   EffectOut   what it may answer; anything it does not set is left as drawn. A glyph is
//               written with `set_glyph()` and must be the same display width as the cell
//               it lands on.
using EffectCell = RolltuiEffectCell;
using EffectOut = RolltuiEffectOut;

using EffectFn = std::function<void(const EffectSpec&, const Theme&, const EffectCell&, EffectOut&)>;

// Rung 2. Refused, with `why` set, when the name is one of the library's (rung 1 is never
// shadowed), when it is empty, or when it is already registered — the same rule, and the
// same two guards, as Windows::register_kind.
bool register_effect_kind(std::string name, EffectFn fn, std::string* why = nullptr);
void clear_registered_effect_kinds();  // tests, and a host tearing down
// Every kind name that resolves right now, in RESOLUTION ORDER: the library's, then the
// host's. What the properties are asserted over.
std::vector<std::string> effect_kind_names();
// False when nothing answers for `name` — which is a HOST fact and never a theme error.
// (Phase 15 m2: this was `const EffectFn* effect_kind(...)`. A resolved kind is a function
// and a context inside the registry now, not an object with an address, and every caller
// only ever asked whether it resolved.)
bool effect_kind_resolves(std::string_view name);
// The library's closed table, for a test and for anything that must refuse to shadow it.
bool is_builtin_effect_kind(std::string_view name);

// How many distinct pictures one period of `spec` has over a span `length` cells long —
// the spinner's frame count, the pulse's role count, a sweep's cell count. `spec.steps`
// overrides it, which is how a host's own kind says how finely it moves.
int effect_steps(const EffectSpec& spec, int length);


// ---- applying, and the tick -------------------------------------------------------------

struct EffectReport {
  int marks_drawn = 0;      // marks the theme had an effect for
  int cells_touched = 0;
  int glyphs_refused = 0;   // overrides dropped because the width did not match (property 1)
  std::vector<std::string> unknown_kinds;  // named, never silently still
  bool clean() const { return glyphs_refused == 0 && unknown_kinds.empty(); }
};

// THE ONE PLACE AN EFFECT IS APPLIED. Called by a host after the whole screen has composed
// — after a modal's overlay, so what an effect sees is what the reader sees — and before
// the frame diff. `now_ms` is any monotonic millisecond clock; the golden harness hands it
// a fixed number, which is the whole of `--tick N`.
EffectReport apply_effects(Frame& f, const Theme& theme, std::uint64_t now_ms, bool ambiguous_wide = false);

// The interval at which this frame must be redrawn for its motion, or NULLOPT when
// nothing is marked, when the theme maps nothing to what is marked, or when nothing
// mapped MOVES. Clamped to at least 16 ms so a long span cannot ask for a wakeup per
// millisecond.
std::optional<int> effect_tick_ms(const Frame& f, const Theme& theme);

// A host's whole decision: `idle_ms` when nothing moves, else the shorter of the two.
int poll_timeout_ms(const Frame& f, const Theme& theme, int idle_ms);

}  // namespace rolltui
