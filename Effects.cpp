// rolltui/Effects.cpp — THE C++ SHAPE of the effects engine. See Effects.hpp for the
// contract and the rules; the engine itself is behind `rolltui/c/rolltui_effects.h`, in
// `EffectsCpp.cpp` or `c/rolltui_effects.c`, one CMake flag apart (plan/phase-15.md m2).
//
// Two things live here and nowhere else:
//   - the STATE vocabulary (`none`/`waiting`/…), which the boundary deliberately does not
//     know: a mark carries its state as the int the frame stored and never interpreted, so
//     the C indexes the per-state arrays with it and the names stay in one language;
//   - the registration trampoline, which is where a host's `std::function` becomes a
//     function and a context the registry can own and release.
//
// **IT USED TO BE THREE.** `fill_view` — the one place an `EffectSpec` was lent to the
// boundary — and `SpecViews`, its twelve-inline-with-a-spill container, are gone with m3:
// the theme owns its specs in C now, so there is no second owner to bridge to and the map
// IS what the applier reads. That deletion is the m2 seam closing, and the arithmetic is
// in plan/phase-15.md rather than here.
#include "rolltui/Effects.hpp"

#include <algorithm>
#include <array>
#include <memory>

#include "rolltui/Scratch.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

namespace {

// THE TRAMPOLINE. A host's kind is a `std::function` taking C++ references; the registry
// holds a function pointer and a `void*`. `ctx` is the owned `EffectFn` and `host` is the
// `Theme*` the applier was called with — the C stores both and dereferences neither. The
// spec is the theme's own, handed straight through: since m3 there is no view to unwrap.
void call_host_kind(void* ctx, const RolltuiEffectSpec* spec, const RolltuiStyle*, const void* host,
                    const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (*static_cast<const EffectFn*>(ctx))(*spec, *static_cast<const Theme*>(host), *in, *out);
}

// …and its release. The registry owns the `EffectFn` from the moment registration
// succeeds; taking it back into a `unique_ptr` is what destroys it, so there is no
// hand-rolled `delete` anywhere in this path.
void free_host_kind(void* ctx) { const std::unique_ptr<EffectFn> owned(static_cast<EffectFn*>(ctx)); }

RolltuiEffectScratch* scratch() {
  static thread_local ThreadHandle<RolltuiEffectScratch, rolltui_effect_scratch_new, rolltui_effect_scratch_free> h;
  return h.get();
}

}  // namespace

// PHASE 17 m2a: the five names are `ROLLTUI_EFFECT_STATE_LIST`'s (rolltui_effects.h). That
// X-macro was added on 2026-09-05 for exactly this reason — "so a C consumer could reach
// neither and every one that needed them copied the list" — and this file, which was the
// original, kept its own array anyway. A vocabulary is not moved until its first owner stops
// spelling it.
static_assert(static_cast<unsigned char>(EffectState::None) == ROLLTUI_EFFECT_STATE_NONE &&
                  static_cast<unsigned char>(EffectState::Flash) == ROLLTUI_EFFECT_STATE_FLASH &&
                  kEffectStateCount == ROLLTUI_EFFECT_STATE_COUNT,
              "rolltui::EffectState and ROLLTUI_EFFECT_STATE_LIST must be the same vocabulary in the same order");

std::string_view effect_state_name(EffectState s) {
  std::size_t len = 0;
  const char* p = rolltui_effect_state_name(static_cast<unsigned char>(s), &len);
  return {p, len};
}

EffectState effect_state_from_name(std::string_view name) {
  const int i = rolltui_effect_state_from_name(name.data(), name.size());
  return i < 0 ? EffectState::count_ : static_cast<EffectState>(i);
}

bool is_builtin_effect_kind(std::string_view name) {
  return rolltui_effect_is_builtin(name.data(), name.size()) != 0;
}

bool register_effect_kind(std::string name, EffectFn fn, std::string* why) {
  auto fail = [&](std::string reason) {
    if (why) *why = std::move(reason);
    return false;
  };
  // OWNED by the registry from the moment `register` returns OK, and still ours until
  // then — which is why `release()` is on the success path only.
  std::unique_ptr<EffectFn> held = std::make_unique<EffectFn>(std::move(fn));
  const int code = rolltui_effect_register(name.data(), name.size(), call_host_kind, held.get(), free_host_kind);
  switch (code) {
    case ROLLTUI_EFFECT_OK:
      held.release();
      return true;
    case ROLLTUI_EFFECT_NO_NAME:
      return fail("an effect kind needs a name");
    case ROLLTUI_EFFECT_NO_FN:
      return fail("effect kind '" + name + "': no function");
    case ROLLTUI_EFFECT_IS_BUILTIN:
      return fail("'" + name + "' is one of the library's own effect kinds");
    default:
      return fail("effect kind '" + name + "' is already registered");
  }
}

void clear_registered_effect_kinds() { rolltui_effect_clear_registered(); }

std::vector<std::string> effect_kind_names() {
  std::vector<std::string> out;
  const std::size_t n = rolltui_effect_kind_count();
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t len = 0;
    const char* p = rolltui_effect_kind_name(i, &len);
    out.emplace_back(p, len);
  }
  return out;
}

bool effect_kind_resolves(std::string_view name) {
  return rolltui_effect_kind_resolves(name.data(), name.size()) != 0;
}

int effect_steps(const EffectSpec& spec, int length) { return rolltui_effect_steps(&spec, length); }

namespace {

// Named, never silently still. The applier calls this once per mark for a kind nothing
// answers for; the names are deduplicated HERE, where they are already strings, so the
// ordinary case — every kind resolves — costs not one byte.
void note_unknown_kind(void* ctx, const char* kind, std::size_t len) {
  std::vector<std::string>& out = *static_cast<std::vector<std::string>*>(ctx);
  const std::string_view name(kind, len);
  if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
}

}  // namespace

EffectReport apply_effects(Frame& f, const Theme& theme, std::uint64_t now_ms, bool ambiguous_wide) {
  EffectReport rep;
  // A frame with nothing marked, or a theme that maps nothing, is the state roll is in
  // almost always — and it costs nothing at all (Phase 13's "never be blind", applied to
  // the path Phase 12 m6 added).
  if (f.mark_count() == 0 || theme.effects.empty()) return rep;
  RolltuiEffectReport r{};
  rolltui_effects_apply(f.handle(), scratch(), theme.styles.data(), &theme, theme.effects.handle(), now_ms,
                        ambiguous_wide, &r, note_unknown_kind, &rep.unknown_kinds);
  rep.marks_drawn = r.marks_drawn;
  rep.cells_touched = r.cells_touched;
  rep.glyphs_refused = r.glyphs_refused;
  return rep;
}

std::optional<int> effect_tick_ms(const Frame& f, const Theme& theme) {
  if (f.mark_count() == 0 || theme.effects.empty()) return std::nullopt;
  const int ms = rolltui_effects_tick_ms(f.handle(), theme.effects.handle());
  return ms > 0 ? std::optional<int>(ms) : std::nullopt;
}

int poll_timeout_ms(const Frame& f, const Theme& theme, int idle_ms) {
  const std::optional<int> tick = effect_tick_ms(f, theme);
  if (!tick) return idle_ms;
  return idle_ms <= 0 ? *tick : std::min(idle_ms, *tick);
}

}  // namespace rolltui
