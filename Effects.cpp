// rolltui/Effects.cpp — THE C++ SHAPE of the effects engine. See Effects.hpp for the
// contract and the rules; the engine itself is behind `rolltui/c/rolltui_effects.h`, in
// `EffectsCpp.cpp` or `c/rolltui_effects.c`, one CMake flag apart (plan/phase-15.md m2).
//
// Three things live here and nowhere else:
//   - the STATE vocabulary (`none`/`waiting`/…), which the boundary deliberately does not
//     know: a mark carries its state as the int the frame stored and never interpreted, so
//     the C indexes the per-state arrays with it and the names stay in one language;
//   - `fill_view`, the ONE place a `EffectSpec` is lent to the boundary;
//   - the registration trampoline, which is where a host's `std::function` becomes a
//     function and a context the registry can own and release.
#include "rolltui/Effects.hpp"

#include <algorithm>
#include <array>
#include <memory>

#include "rolltui/Scratch.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

namespace {

constexpr std::array<std::string_view, kEffectStateCount> kStateNames = {"none", "waiting", "streaming", "progress", "flash"};

// The role a spec with no roles of its own picks. It is substituted HERE, into the view,
// rather than defaulted in the engine: `RolltuiEffectSpec::role_count` is then never zero
// and no rung of the C has an opinion about which role is the fallback, which is what keeps
// the styling vocabulary in one language (rolltui/c/rolltui_effects.h).
constexpr unsigned char kFallbackRole = static_cast<unsigned char>(Role::accent_1);
constexpr unsigned char kFallbackRoles[] = {kFallbackRole};

// Frame `i` of a spec's cycle, as a BORROW out of the owning spec's own string. This is the
// one accessor on the boundary, and it exists because a `std::vector<std::string>` has no
// contiguous (pointer, length) array to lend.
const char* spec_frame(const void* owner, std::size_t i, std::size_t* len) {
  const std::string& f = static_cast<const EffectSpec*>(owner)->frames[i];
  *len = f.size();
  return f.data();
}

// THE ONE PLACE A SPEC IS LENT. Everything here is a borrow of `s`, valid for exactly as
// long as `s` is — which is the whole of one `apply_effects` call.
void fill_view(const EffectSpec& s, RolltuiEffectSpec& v) {
  v.kind = s.kind.data();
  v.kind_len = s.kind.size();
  // `std::vector<Role>` is one byte per entry (Role is uint8_t-backed), so this lends the
  // theme's own array rather than copying it.
  static_assert(sizeof(Role) == 1, "a role must be one byte for the boundary to read the theme's array directly");
  v.roles = s.roles.empty() ? kFallbackRoles : reinterpret_cast<const unsigned char*>(s.roles.data());
  v.role_count = s.roles.empty() ? 1 : s.roles.size();
  v.frame_count = s.frames.size();
  v.owner = &s;
  v.frame = spec_frame;
  v.period_ms = s.period_ms;
  v.width = s.width;
  v.steps = s.steps;
  v.backward = static_cast<unsigned char>(s.backward);
  v.unresolved = 0;
}

// A whole EffectMap, lent. INLINE with a stated SPILL (CLAUDE.md's first strategy): twelve
// specs covers every theme anybody has written — the shipped ones map four states with one
// spec each, and the widest maps two — so `apply_effects` allocates NOTHING per frame, and
// a theme with more is handled rather than assumed away. The same shape `ChildScratch`
// uses in Layout.cpp, for the same reason.
class SpecViews {
 public:
  explicit SpecViews(const EffectMap& m) {
    for (std::size_t i = 0; i < kEffectStateCount; ++i) n_ += m.for_state(static_cast<EffectState>(i)).size();
    if (n_ > kInline) spill_.resize(n_);
    std::size_t at = 0;
    for (std::size_t i = 0; i < kEffectStateCount; ++i) {
      const std::vector<EffectSpec>& specs = m.for_state(static_cast<EffectState>(i));
      first_[i] = at;
      count_[i] = specs.size();
      for (const EffectSpec& s : specs) fill_view(s, data()[at++]);
    }
  }
  RolltuiEffectSpec* data() { return n_ > kInline ? spill_.data() : inline_; }
  const RolltuiEffectSpec* data() const { return n_ > kInline ? spill_.data() : inline_; }
  std::size_t size() const { return n_; }
  const std::size_t* first() const { return first_; }
  const std::size_t* count() const { return count_; }

 private:
  static constexpr std::size_t kInline = 12;
  std::size_t n_ = 0;
  RolltuiEffectSpec inline_[kInline]{};
  std::vector<RolltuiEffectSpec> spill_;
  std::size_t first_[kEffectStateCount]{}, count_[kEffectStateCount]{};
};

// THE TRAMPOLINE. A host's kind is a `std::function` taking C++ references; the registry
// holds a function pointer and a `void*`. `ctx` is the owned `EffectFn`, `spec->owner` is
// the owning `EffectSpec` the view was built from, and `host` is the `Theme*` the applier
// was called with — the C stores all three and dereferences none of them.
void call_host_kind(void* ctx, const RolltuiEffectSpec* spec, const RolltuiStyle*, const void* host,
                    const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (*static_cast<const EffectFn*>(ctx))(*static_cast<const EffectSpec*>(spec->owner),
                                       *static_cast<const Theme*>(host), *in, *out);
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

std::string_view effect_state_name(EffectState s) {
  const std::size_t i = static_cast<std::size_t>(s);
  return i < kEffectStateCount ? kStateNames[i] : kStateNames[0];
}

EffectState effect_state_from_name(std::string_view name) {
  for (std::size_t i = 0; i < kEffectStateCount; ++i)
    if (kStateNames[i] == name) return static_cast<EffectState>(i);
  return EffectState::count_;
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

int effect_steps(const EffectSpec& spec, int length) {
  RolltuiEffectSpec v;
  fill_view(spec, v);
  return rolltui_effect_steps(&v, length);
}

EffectReport apply_effects(Frame& f, const Theme& theme, std::uint64_t now_ms, bool ambiguous_wide) {
  EffectReport rep;
  // A frame with nothing marked, or a theme that maps nothing, is the state roll is in
  // almost always — and it costs nothing at all, not even the views (Phase 13's "never be
  // blind", applied to the path Phase 12 m6 added).
  if (f.mark_count() == 0 || theme.effects.empty()) return rep;
  SpecViews views(theme.effects);
  RolltuiEffectReport r{};
  rolltui_effects_apply(f.handle(), scratch(), theme.styles.data(), &theme, views.data(), views.first(),
                        views.count(), kEffectStateCount, now_ms, ambiguous_wide, &r);
  rep.marks_drawn = r.marks_drawn;
  rep.cells_touched = r.cells_touched;
  rep.glyphs_refused = r.glyphs_refused;
  // Named, never silently still. The flag rides on the view so that reporting an unknown
  // kind costs no second buffer; the names are deduplicated here, where they are strings.
  for (std::size_t i = 0; i < views.size(); ++i) {
    if (!views.data()[i].unresolved) continue;
    std::string kind(views.data()[i].kind, views.data()[i].kind_len);
    if (std::find(rep.unknown_kinds.begin(), rep.unknown_kinds.end(), kind) == rep.unknown_kinds.end())
      rep.unknown_kinds.push_back(std::move(kind));
  }
  return rep;
}

std::optional<int> effect_tick_ms(const Frame& f, const Theme& theme) {
  if (f.mark_count() == 0 || theme.effects.empty()) return std::nullopt;
  SpecViews views(theme.effects);
  const int ms = rolltui_effects_tick_ms(f.handle(), views.data(), views.first(), views.count(), kEffectStateCount);
  return ms > 0 ? std::optional<int>(ms) : std::nullopt;
}

int poll_timeout_ms(const Frame& f, const Theme& theme, int idle_ms) {
  const std::optional<int> tick = effect_tick_ms(f, theme);
  if (!tick) return idle_ms;
  return idle_ms <= 0 ? *tick : std::min(idle_ms, *tick);
}

}  // namespace rolltui
