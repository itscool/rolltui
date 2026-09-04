// rolltui/Effects.cpp — see Effects.hpp.
#include "rolltui/Effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>

#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

namespace {

constexpr std::array<std::string_view, kEffectStateCount> kStateNames = {"none", "waiting", "streaming", "progress", "flash"};

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Which of `steps` pictures of one period `elapsed` falls in. A still spec (period 0)
// is always its first picture — which is what makes "a theme that maps nothing moving"
// a legible still frame rather than a blank one.
int step_index(const EffectSpec& s, std::uint64_t elapsed, int steps) {
  if (steps <= 1 || s.period_ms <= 0) return 0;
  const std::uint64_t period = static_cast<std::uint64_t>(s.period_ms);
  const std::uint64_t phase = elapsed % period;
  int i = static_cast<int>(phase * static_cast<std::uint64_t>(steps) / period);
  if (i >= steps) i = steps - 1;
  return s.backward ? steps - 1 - i : i;
}

const Style& role_style(const Theme& theme, const std::vector<Role>& roles, std::size_t i) {
  return theme.style(roles.empty() ? Role::accent_1 : roles[i % roles.size()]);
}

// The shimmer's sweeping window, in cells.
int sweep_width(const EffectSpec& s, int length) {
  if (s.width > 0) return s.width;
  return std::max(1, length / 3);
}

// The `col`-th CELL of one frame string, when the frame is more than one cell wide
// (an ellipsis's ".. "). Empty when the column is past the frame's end.
std::string frame_cell(std::string_view frame, int col, bool amb) {
  int at = 0;
  for (const unicode::Grapheme& g : unicode::graphemes(frame, amb)) {
    if (g.width <= 0) continue;
    if (at == col) return std::string(frame.substr(g.offset, g.length));
    at += g.width;
    if (at > col) return {};  // the column is the second half of a wide glyph: leave it
  }
  return {};
}

int frame_width(std::string_view frame, bool amb) { return unicode::display_width(frame, amb); }

// ---- the built-in kinds ---------------------------------------------------------------
// Each one is the whole of its rule. None of them names a colour: they pick between the
// base style and a role the theme named (Effects.hpp, "what an effect may not do").

void glyph_cycle(const EffectSpec& s, const EffectCell& in, EffectOut& out, bool at_end) {
  if (s.frames.empty()) return;
  const int w = frame_width(s.frames[0], in.ambiguous_wide);
  if (w <= 0 || w > in.length) return;
  const int start = at_end ? in.length - w : 0;
  if (in.index < start || in.index >= start + w) return;
  const std::string& frame = s.frames[static_cast<std::size_t>(step_index(s, in.elapsed_ms, static_cast<int>(s.frames.size())))];
  std::string g = frame_cell(frame, in.index - start, in.ambiguous_wide);
  if (g.empty()) return;
  out.has_glyph = true;
  out.glyph = std::move(g);
}

void kind_spinner(const EffectSpec& s, const Theme&, const EffectCell& in, EffectOut& out) { glyph_cycle(s, in, out, false); }
void kind_ellipsis(const EffectSpec& s, const Theme&, const EffectCell& in, EffectOut& out) { glyph_cycle(s, in, out, true); }

void kind_bar(const EffectSpec& s, const Theme& theme, const EffectCell& in, EffectOut& out) {
  const int filled = static_cast<int>(std::lround(clamp01(in.fraction) * in.length));
  if (in.index >= filled) return;
  out.has_style = true;
  out.style = role_style(theme, s.roles, 0);
}

void kind_pulse(const EffectSpec& s, const Theme& theme, const EffectCell& in, EffectOut& out) {
  const int n = std::max<int>(1, static_cast<int>(s.roles.size()));
  out.has_style = true;
  out.style = role_style(theme, s.roles, static_cast<std::size_t>(step_index(s, in.elapsed_ms, n)));
}

void kind_blink(const EffectSpec& s, const Theme& theme, const EffectCell& in, EffectOut& out) {
  if (step_index(s, in.elapsed_ms, 2) != 0) return;  // the base half: the one kind that shows it
  out.has_style = true;
  out.style = role_style(theme, s.roles, 0);
}

void kind_shimmer(const EffectSpec& s, const Theme& theme, const EffectCell& in, EffectOut& out) {
  const int w = sweep_width(s, in.length);
  const int pos = step_index(s, in.elapsed_ms, in.length + w);
  if (in.index > pos || in.index <= pos - w) return;
  out.has_style = true;
  out.style = role_style(theme, s.roles, 0);
}

void kind_gradient(const EffectSpec& s, const Theme& theme, const EffectCell& in, EffectOut& out) {
  const int n = std::max<int>(1, static_cast<int>(s.roles.size()));
  const int band = in.length > 0 ? in.index * n / in.length : 0;
  out.has_style = true;
  out.style = role_style(theme, s.roles, static_cast<std::size_t>(band + step_index(s, in.elapsed_ms, n)));
}

struct Builtin {
  std::string_view name;
  void (*fn)(const EffectSpec&, const Theme&, const EffectCell&, EffectOut&);
};
constexpr Builtin kBuiltins[] = {
    {"spinner", kind_spinner}, {"ellipsis", kind_ellipsis}, {"bar", kind_bar},
    {"pulse", kind_pulse},     {"shimmer", kind_shimmer},   {"gradient", kind_gradient},
    {"blink", kind_blink},
};

// Rung 2. A map rather than the layout registry's vector because a kind is looked up per
// CELL: the applier asks once per span, but a host stacking three kinds asks three times
// a frame per mark, and an ordered map keeps that a compare-and-descend.
std::map<std::string, EffectFn, std::less<>>& registry() {
  static std::map<std::string, EffectFn, std::less<>> r;
  return r;
}
std::mutex& registry_mu() {
  static std::mutex m;
  return m;
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
  for (const Builtin& b : kBuiltins)
    if (b.name == name) return true;
  return false;
}

bool register_effect_kind(std::string name, EffectFn fn, std::string* why) {
  auto fail = [&](std::string reason) {
    if (why) *why = std::move(reason);
    return false;
  };
  if (name.empty()) return fail("an effect kind needs a name");
  if (!fn) return fail("effect kind '" + name + "': no function");
  // Rung 1 is never shadowed — the same guard, for the same reason, as a widget kind.
  if (is_builtin_effect_kind(name)) return fail("'" + name + "' is one of the library's own effect kinds");
  std::lock_guard<std::mutex> lock(registry_mu());
  if (registry().count(name)) return fail("effect kind '" + name + "' is already registered");
  registry().emplace(std::move(name), std::move(fn));
  return true;
}

void clear_registered_effect_kinds() {
  std::lock_guard<std::mutex> lock(registry_mu());
  registry().clear();
}

std::vector<std::string> effect_kind_names() {
  std::vector<std::string> out;
  for (const Builtin& b : kBuiltins) out.emplace_back(b.name);
  std::lock_guard<std::mutex> lock(registry_mu());
  for (const auto& [name, fn] : registry()) out.push_back(name);
  return out;
}

const EffectFn* effect_kind(std::string_view name) {
  static const std::array<EffectFn, sizeof(kBuiltins) / sizeof(kBuiltins[0])> builtins = [] {
    std::array<EffectFn, sizeof(kBuiltins) / sizeof(kBuiltins[0])> a;
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = kBuiltins[i].fn;
    return a;
  }();
  for (std::size_t i = 0; i < builtins.size(); ++i)
    if (kBuiltins[i].name == name) return &builtins[i];
  std::lock_guard<std::mutex> lock(registry_mu());
  auto it = registry().find(name);
  return it == registry().end() ? nullptr : &it->second;
}

int effect_steps(const EffectSpec& spec, int length) {
  if (spec.steps > 0) return spec.steps;
  const int len = std::max(1, length);
  if (spec.kind == "spinner" || spec.kind == "ellipsis") return std::max<int>(1, static_cast<int>(spec.frames.size()));
  if (spec.kind == "pulse" || spec.kind == "gradient") return std::max<int>(1, static_cast<int>(spec.roles.size()));
  if (spec.kind == "blink") return 2;
  if (spec.kind == "shimmer") return len + sweep_width(spec, len);
  if (spec.kind == "bar") return 1;
  return len;  // a host's own kind, until its theme row says otherwise
}

EffectReport apply_effects(Frame& f, const Theme& theme, std::uint64_t now_ms, bool ambiguous_wide) {
  EffectReport rep;
  auto name_unknown = [&](const std::string& kind) {
    if (std::find(rep.unknown_kinds.begin(), rep.unknown_kinds.end(), kind) == rep.unknown_kinds.end())
      rep.unknown_kinds.push_back(kind);
  };
  // Each spec's kind is resolved ONCE PER MARK, not once per cell. The lookup is
  // loop-invariant, it is a string compare against the closed table, and for a HOST kind
  // it takes the registry's mutex — none of which belongs inside a per-cell draw loop.
  // `resolved` is declared here and cleared per mark so it allocates at most once per
  // call and NOT AT ALL when nothing is marked, which is the state roll is in almost
  // always (Phase 13's "never be blind", applied to the path this milestone added).
  std::vector<const EffectFn*> resolved;
  for (const Mark& m : f.marks()) {
    if (m.cells <= 0 || m.state == EffectState::None) continue;
    if (m.y < 0 || m.y >= f.height()) continue;
    const std::vector<EffectSpec>& specs = theme.effects.for_state(m.state);
    if (specs.empty()) continue;
    resolved.clear();
    for (const EffectSpec& s : specs) {
      const EffectFn* fn = effect_kind(s.kind);
      if (!fn || !*fn) name_unknown(s.kind);  // named once per mark, not once per cell
      resolved.push_back(fn && *fn ? fn : nullptr);
    }
    bool any = false;
    const std::uint64_t elapsed = now_ms >= m.since_ms ? now_ms - m.since_ms : 0;
    for (int i = 0; i < m.cells; ++i) {
      const int x = m.x + i;
      if (x < 0 || x >= f.width()) continue;
      const Cell& cell = f.at(x, m.y);
      // Property 2, at its two edges: a continuation cell belongs to the glyph before it,
      // and a 2-cell glyph whose second half is outside the span is skipped WHOLE.
      if (cell.continuation) continue;
      if (cell.width == 2 && i + 1 >= m.cells) continue;
      EffectCell in;
      in.elapsed_ms = elapsed;
      in.index = i;
      in.length = m.cells;
      in.fraction = clamp01(m.fraction);
      in.base = cell.style;
      in.ambiguous_wide = ambiguous_wide;
      EffectOut out;
      for (std::size_t k = 0; k < specs.size(); ++k) {
        const EffectFn* fn = resolved[k];
        if (!fn) continue;  // an unknown kind: already named above, and it draws nothing
        const EffectSpec& s = specs[k];
        EffectOut one;
        (*fn)(s, theme, in, one);
        // Stacking: the glyph comes from whichever kind last set one, the style likewise,
        // and a later kind sees the earlier one's style as its base — so "glyph from one,
        // colour from another" is the ordinary case rather than a special one.
        if (one.has_style) {
          out.has_style = true;
          out.style = one.style;
          in.base = one.style;
        }
        if (one.has_glyph) {
          out.has_glyph = true;
          out.glyph = std::move(one.glyph);
        }
      }
      if (!out.has_glyph && !out.has_style) continue;
      const Style style = out.has_style ? out.style : cell.style;
      if (out.has_glyph) {
        // PROPERTY 1, and it is enforced here rather than trusted: a glyph of the wrong
        // width is dropped and counted, never written.
        const int w = frame_width(out.glyph, ambiguous_wide);
        if (w != cell.width) {
          ++rep.glyphs_refused;
          out.has_glyph = false;
        }
      }
      if (out.has_glyph) {
        f.put(x, m.y, out.glyph, cell.width, style, cell.link);
      } else {
        f.set_style(x, m.y, style);
        if (cell.width == 2 && x + 1 < f.width()) f.set_style(x + 1, m.y, style);  // inside the span, by the guard above
      }
      ++rep.cells_touched;
      any = true;
    }
    if (any) ++rep.marks_drawn;
  }
  return rep;
}

std::optional<int> effect_tick_ms(const Frame& f, const Theme& theme) {
  std::optional<int> best;
  for (const Mark& m : f.marks()) {
    if (m.cells <= 0 || m.state == EffectState::None) continue;
    for (const EffectSpec& s : theme.effects.for_state(m.state)) {
      if (s.period_ms <= 0) continue;              // a still effect asks for no wakeup
      if (const EffectFn* fn = effect_kind(s.kind); !fn || !*fn) continue;  // nor one that cannot draw
      const int steps = std::max(1, effect_steps(s, m.cells));
      const int ms = std::max(16, s.period_ms / steps);
      if (!best || ms < *best) best = ms;
    }
  }
  return best;
}

int poll_timeout_ms(const Frame& f, const Theme& theme, int idle_ms) {
  const std::optional<int> tick = effect_tick_ms(f, theme);
  if (!tick) return idle_ms;
  return idle_ms <= 0 ? *tick : std::min(idle_ms, *tick);
}

}  // namespace rolltui
