// rolltui/EffectsCpp.cpp — THE C++ SIDE OF THE EFFECTS ENGINE, behind the same boundary as
// `c/rolltui_effects.c` (rolltui/c/rolltui_effects.h, Phase 15 m2). One CMake flag picks
// which links; both satisfy `rolltui/tests/effects_test.cpp`, which asserts the two
// properties over EVERY registered kind including one that deliberately lies about its
// width, so a behavioural difference is a test failure on the day it appears.
//
// This is the code Phase 12 m6 wrote, moved behind the boundary, with the three things the
// boundary forced and nothing else: the kind table is resolved rather than handed out as a
// callable, the per-mark resolution and glyph buffers live in the caller's scratch, and the
// registry OWNS a host's context and must be told how to release it.
#include "rolltui/c/rolltui_effects.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Unicode.hpp"
#include "rolltui/c/rolltui_lifetime.h"

namespace {

// What one spec's kind resolved to, for one mark. `builtin` is an index into the closed
// table below, or -1 for a host kind; both -1 and a null `fn` mean "nothing answers".
struct Resolved {
  int builtin = -1;
  RolltuiEffectFn fn = nullptr;
  void* ctx = nullptr;
};

}  // namespace

// The caller's working memory, in the shape C++ keeps it. Same two roles as the C: a
// grapheme buffer for a glyph kind measuring its own frame, and the per-mark resolution,
// which is loop-invariant over a mark's cells and must not be recomputed per cell.
struct RolltuiEffectScratch {
  std::vector<rolltui::unicode::Grapheme> gs;
  std::vector<Resolved> res;
};

namespace {

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

std::string_view kind_of(const RolltuiEffectSpec& s) { return std::string_view(s.kind, s.kind_len); }

// Which of `steps` pictures of one period `elapsed` falls in. A still spec (period 0) is
// always its first picture — which is what makes "a theme that maps nothing moving" a
// legible still frame rather than a blank one.
int step_index(const RolltuiEffectSpec& s, std::uint64_t elapsed, int steps) {
  if (steps <= 1 || s.period_ms <= 0) return 0;
  const std::uint64_t period = static_cast<std::uint64_t>(s.period_ms);
  const std::uint64_t phase = elapsed % period;
  int i = static_cast<int>(phase * static_cast<std::uint64_t>(steps) / period);
  if (i >= steps) i = steps - 1;
  return s.backward ? steps - 1 - i : i;
}

// `role_count` is never zero (rolltui_effects.h), so there is no fallback role to name
// here — which is what keeps the styling vocabulary in one place.
RolltuiStyle role_style(const RolltuiStyle* styles, const RolltuiEffectSpec& s, std::size_t i) {
  return styles[s.roles[i % s.role_count]];
}

// The shimmer's sweeping window, in cells.
int sweep_width(const RolltuiEffectSpec& s, int length) {
  if (s.width > 0) return s.width;
  return std::max(1, length / 3);
}

std::string_view frame_of(const RolltuiEffectSpec& s, std::size_t i) { return s.frame(i); }

int frame_width(std::string_view frame, bool amb) { return rolltui::unicode::display_width(frame, amb); }

// The `col`-th CELL of one frame string, when the frame is more than one cell wide (an
// ellipsis's ".. "). Empty when the column is past the frame's end, or lands on the second
// half of a wide glyph.
std::string_view frame_cell(RolltuiEffectScratch& sc, std::string_view frame, int col, bool amb) {
  rolltui::unicode::graphemes_into(frame, amb, sc.gs);
  int at = 0;
  for (const rolltui::unicode::Grapheme& g : sc.gs) {
    if (g.width <= 0) continue;
    if (at == col) return frame.substr(g.offset, g.length);
    at += g.width;
    if (at > col) return {};  // the column is the second half of a wide glyph: leave it
  }
  return {};
}

void set_glyph(RolltuiEffectOut& out, std::string_view g) {
  out.has_glyph = 1;
  out.glyph_len = g.size();
  if (!g.empty() && g.size() <= ROLLTUI_EFFECT_GLYPH_MAX) std::memcpy(out.glyph, g.data(), g.size());
}

// ---- rung 1: the built-in kinds ---------------------------------------------------------
// Each one is the whole of its rule. None of them names a colour: they pick between the base
// style and a role the theme named (Effects.hpp, "what an effect may not do").

void glyph_cycle(RolltuiEffectScratch& sc, const RolltuiEffectSpec& s, const RolltuiEffectCell& in,
                 RolltuiEffectOut& out, bool at_end) {
  if (s.frame_count == 0) return;
  const int w = frame_width(frame_of(s, 0), in.ambiguous_wide != 0);
  if (w <= 0 || w > in.length) return;
  const int start = at_end ? in.length - w : 0;
  if (in.index < start || in.index >= start + w) return;
  const std::string_view frame = frame_of(s, static_cast<std::size_t>(step_index(s, in.elapsed_ms, static_cast<int>(s.frame_count))));
  const std::string_view g = frame_cell(sc, frame, in.index - start, in.ambiguous_wide != 0);
  if (g.empty()) return;
  set_glyph(out, g);
}

void kind_spinner(RolltuiEffectScratch& sc, const RolltuiEffectSpec& s, const RolltuiStyle*,
                  const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  glyph_cycle(sc, s, in, out, false);
}
void kind_ellipsis(RolltuiEffectScratch& sc, const RolltuiEffectSpec& s, const RolltuiStyle*,
                   const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  glyph_cycle(sc, s, in, out, true);
}

void kind_bar(RolltuiEffectScratch&, const RolltuiEffectSpec& s, const RolltuiStyle* styles,
              const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  const int filled = static_cast<int>(std::lround(clamp01(in.fraction) * in.length));
  if (in.index >= filled) return;
  out.has_style = 1;
  out.style = role_style(styles, s, 0);
}

void kind_pulse(RolltuiEffectScratch&, const RolltuiEffectSpec& s, const RolltuiStyle* styles,
                const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  const int n = std::max<int>(1, static_cast<int>(s.role_count));
  out.has_style = 1;
  out.style = role_style(styles, s, static_cast<std::size_t>(step_index(s, in.elapsed_ms, n)));
}

void kind_blink(RolltuiEffectScratch&, const RolltuiEffectSpec& s, const RolltuiStyle* styles,
                const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  if (step_index(s, in.elapsed_ms, 2) != 0) return;  // the base half: the one kind that shows it
  out.has_style = 1;
  out.style = role_style(styles, s, 0);
}

void kind_shimmer(RolltuiEffectScratch&, const RolltuiEffectSpec& s, const RolltuiStyle* styles,
                  const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  const int w = sweep_width(s, in.length);
  const int pos = step_index(s, in.elapsed_ms, in.length + w);
  if (in.index > pos || in.index <= pos - w) return;
  out.has_style = 1;
  out.style = role_style(styles, s, 0);
}

void kind_gradient(RolltuiEffectScratch&, const RolltuiEffectSpec& s, const RolltuiStyle* styles,
                   const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  const int n = std::max<int>(1, static_cast<int>(s.role_count));
  const int band = in.length > 0 ? in.index * n / in.length : 0;
  out.has_style = 1;
  out.style = role_style(styles, s, static_cast<std::size_t>(band + step_index(s, in.elapsed_ms, n)));
}

struct Builtin {
  std::string_view name;
  void (*fn)(RolltuiEffectScratch&, const RolltuiEffectSpec&, const RolltuiStyle*, const RolltuiEffectCell&,
             RolltuiEffectOut&);
};
constexpr Builtin kBuiltins[] = {
    {"spinner", kind_spinner}, {"ellipsis", kind_ellipsis}, {"bar", kind_bar},
    {"pulse", kind_pulse},     {"shimmer", kind_shimmer},   {"gradient", kind_gradient},
    {"blink", kind_blink},
};
constexpr std::size_t kBuiltinCount = sizeof(kBuiltins) / sizeof(kBuiltins[0]);

int builtin_index(std::string_view name) {
  for (std::size_t i = 0; i < kBuiltinCount; ++i)
    if (kBuiltins[i].name == name) return static_cast<int>(i);
  return -1;
}

// ---- rung 2: the kinds a HOST registered -------------------------------------------------
// THE ONLY PROCESS-WIDE STATE IN THIS MODULE, and the reason it is the first ported one to
// meet `rolltui::shutdown()`. The map owns the name; the entry owns the host's context, and
// must be TOLD how to release it — a `void*` has no destructor, which is the ownership the
// boundary made explicit.

struct HostKind {
  RolltuiEffectFn fn = nullptr;
  void* ctx = nullptr;
  void (*free_ctx)(void*) = nullptr;
};

std::map<std::string, HostKind, std::less<>>& registry() {
  static std::map<std::string, HostKind, std::less<>> r;
  return r;
}
std::mutex& registry_mu() {
  static std::mutex m;
  return m;
}
bool& releaser_registered() {
  static bool b = false;
  return b;
}

// Rung 1 first and never shadowed, then the host's — the resolution order the whole library
// uses for a widget kind, a menu file and an effect kind alike. Called with the lock held.
bool resolve(std::string_view name, Resolved& out) {
  out = Resolved{};
  out.builtin = builtin_index(name);
  if (out.builtin >= 0) return true;
  const auto it = registry().find(name);
  if (it == registry().end()) return false;
  out.fn = it->second.fn;
  out.ctx = it->second.ctx;
  return true;
}

void call_kind(RolltuiEffectScratch& sc, const Resolved& r, const RolltuiEffectSpec& spec, const RolltuiStyle* styles,
               const void* host, const RolltuiEffectCell& in, RolltuiEffectOut& out) {
  if (r.builtin >= 0)
    kBuiltins[r.builtin].fn(sc, spec, styles, in, out);
  else
    r.fn(r.ctx, &spec, styles, host, &in, &out);
}

}  // namespace

extern "C" {

// OWNED, through a `unique_ptr` released into the caller's hands and taken back when it
// comes home — the same idiom `WrapCpp.cpp` uses.
RolltuiEffectScratch* rolltui_effect_scratch_new(void) { return std::make_unique<RolltuiEffectScratch>().release(); }

void rolltui_effect_scratch_free(RolltuiEffectScratch* s) {
  const std::unique_ptr<RolltuiEffectScratch> owned(s);
}

int rolltui_effect_is_builtin(const char* name, size_t len) { return builtin_index(std::string_view(name, len)) >= 0; }

int rolltui_effect_register(const char* name, size_t name_len, RolltuiEffectFn fn, void* ctx,
                            void (*free_ctx)(void*)) {
  const std::string_view n(name, name_len);
  if (n.empty()) return ROLLTUI_EFFECT_NO_NAME;
  if (!fn) return ROLLTUI_EFFECT_NO_FN;
  // Rung 1 is never shadowed — the same guard, for the same reason, as a widget kind.
  if (builtin_index(n) >= 0) return ROLLTUI_EFFECT_IS_BUILTIN;
  const std::lock_guard<std::mutex> lock(registry_mu());
  if (registry().count(n)) return ROLLTUI_EFFECT_DUPLICATE;
  if (!releaser_registered()) {
    // Registered where the retained thing is MADE, not in a central list (Lifetime.hpp).
    // The flag is cleared again by `clear`, so a registry emptied by `shutdown()` and then
    // used again says so again — a releaser registered twice is idempotent, and a missing
    // one is a leak, so the duplicate is the safe direction.
    releaser_registered() = true;
    rolltui_on_shutdown(rolltui_effect_clear_registered);
  }
  registry().emplace(std::string(n), HostKind{fn, ctx, free_ctx});
  return ROLLTUI_EFFECT_OK;
}

void rolltui_effect_clear_registered(void) {
  // The table is taken OUT under the lock and released outside it, so a host's own
  // destructor never runs while this library holds a mutex it might want.
  std::map<std::string, HostKind, std::less<>> taken;
  {
    const std::lock_guard<std::mutex> lock(registry_mu());
    taken.swap(registry());
    releaser_registered() = false;
  }
  for (const auto& [name, k] : taken)
    if (k.free_ctx) k.free_ctx(k.ctx);
}

size_t rolltui_effect_kind_count(void) {
  const std::lock_guard<std::mutex> lock(registry_mu());
  return kBuiltinCount + registry().size();
}

const char* rolltui_effect_kind_name(size_t i, size_t* len) {
  if (i < kBuiltinCount) {
    *len = kBuiltins[i].name.size();
    return kBuiltins[i].name.data();
  }
  const std::lock_guard<std::mutex> lock(registry_mu());
  std::size_t at = i - kBuiltinCount;
  for (const auto& [name, k] : registry()) {
    if (at == 0) {
      *len = name.size();
      return name.data();
    }
    --at;
  }
  *len = 0;
  return "";
}

int rolltui_effect_kind_resolves(const char* name, size_t len) {
  const std::string_view n(name, len);
  if (builtin_index(n) >= 0) return 1;
  const std::lock_guard<std::mutex> lock(registry_mu());
  Resolved r;
  return resolve(n, r) ? 1 : 0;
}

// ---- what a THEME carries, owned in C++ ---------------------------------------------------
// THE m2 SEAM, CLOSED (rolltui_effects.h). The same map, in the shape this side has for a
// thing that owns strings and arrays: the store is `std::string`s and `std::vector`s, and
// the VIEW beside it is the borrows a reader is handed. The two are rebuilt together after
// every change, in one place, so a pointer can never be stale by omission.
struct RolltuiEffectMap {
  struct SpecStore {
    std::string kind;
    std::vector<std::string> frames;
    std::vector<unsigned char> roles;
  };
  std::size_t states = 0;
  unsigned char fallback = 0;
  std::vector<std::vector<SpecStore>> store;
  std::vector<std::vector<RolltuiEffectFrame>> frame_views;  // one per (state, spec), flattened per state
  std::vector<std::vector<RolltuiEffectSpec>> view;

  void refresh(std::size_t s) {
    // The frame views of every spec of this state, in one array per state, so a spec's
    // `frames` pointer is stable for as long as the state is not changed.
    std::size_t total = 0;
    for (const SpecStore& sp : store[s]) total += sp.frames.size();
    frame_views[s].assign(total, RolltuiEffectFrame{nullptr, 0});
    std::size_t at = 0;
    for (std::size_t i = 0; i < store[s].size(); ++i) {
      const SpecStore& sp = store[s][i];
      RolltuiEffectSpec& v = view[s][i];
      v.kind = sp.kind.data();
      v.kind_len = sp.kind.size();
      v.roles = sp.roles.empty() ? &fallback : sp.roles.data();
      v.role_count = sp.roles.empty() ? 1 : sp.roles.size();
      v.own_role_count = sp.roles.size();
      v.frames = frame_views[s].data() + at;
      v.frame_count = sp.frames.size();
      for (const std::string& f : sp.frames) frame_views[s][at++] = RolltuiEffectFrame{f.data(), f.size()};
    }
  }
};

RolltuiEffectMap* rolltui_effect_map_new(size_t states, unsigned char fallback_role) {
  std::unique_ptr<RolltuiEffectMap> m = std::make_unique<RolltuiEffectMap>();
  m->states = states;
  m->fallback = fallback_role;
  m->store.resize(states);
  m->frame_views.resize(states);
  m->view.resize(states);
  return m.release();
}

void rolltui_effect_map_free(RolltuiEffectMap* m) {
  const std::unique_ptr<RolltuiEffectMap> owned(m);  // takes it back, and frees it on the way out
}

void rolltui_effect_map_clear(RolltuiEffectMap* m) {
  if (!m) return;
  for (std::size_t s = 0; s < m->states; ++s) {
    m->store[s].clear();
    m->view[s].clear();
    m->frame_views[s].clear();
  }
}

size_t rolltui_effect_map_count(const RolltuiEffectMap* m, size_t state) {
  return (m && state < m->states) ? m->store[state].size() : 0;
}

const RolltuiEffectSpec* rolltui_effect_map_at(const RolltuiEffectMap* m, size_t state, size_t i) {
  if (!m || state >= m->states || i >= m->view[state].size()) return nullptr;
  return &m->view[state][i];
}

int rolltui_effect_map_empty(const RolltuiEffectMap* m) {
  if (!m) return 1;
  for (std::size_t s = 0; s < m->states; ++s)
    if (!m->store[s].empty()) return 0;
  return 1;
}

size_t rolltui_effect_map_add(RolltuiEffectMap* m, size_t state, const char* kind, size_t kind_len, int period_ms,
                              int width, int steps, int backward) {
  if (!m || state >= m->states) return 0;
  m->store[state].push_back(RolltuiEffectMap::SpecStore{std::string(kind, kind_len), {}, {}});
  RolltuiEffectSpec v{};
  v.period_ms = period_ms;
  v.width = width;
  v.steps = steps;
  v.backward = static_cast<unsigned char>(backward != 0);
  m->view[state].push_back(v);
  m->refresh(state);
  return m->store[state].size() - 1;
}

void rolltui_effect_map_add_frame(RolltuiEffectMap* m, size_t state, size_t i, const char* bytes, size_t len) {
  if (!m || state >= m->states || i >= m->store[state].size()) return;
  m->store[state][i].frames.emplace_back(bytes, len);
  m->refresh(state);
}

void rolltui_effect_map_add_role(RolltuiEffectMap* m, size_t state, size_t i, unsigned char role) {
  if (!m || state >= m->states || i >= m->store[state].size()) return;
  m->store[state][i].roles.push_back(role);
  m->refresh(state);
}

RolltuiEffectMap* rolltui_effect_map_clone(const RolltuiEffectMap* m) {
  if (!m) return nullptr;
  RolltuiEffectMap* out = rolltui_effect_map_new(m->states, m->fallback);
  for (std::size_t s = 0; s < m->states; ++s)
    for (std::size_t i = 0; i < m->store[s].size(); ++i) {
      const RolltuiEffectMap::SpecStore& sp = m->store[s][i];
      const RolltuiEffectSpec& v = m->view[s][i];
      const std::size_t k = rolltui_effect_map_add(out, s, sp.kind.data(), sp.kind.size(), v.period_ms, v.width,
                                                   v.steps, v.backward);
      for (const std::string& f : sp.frames) rolltui_effect_map_add_frame(out, s, k, f.data(), f.size());
      for (unsigned char r : sp.roles) rolltui_effect_map_add_role(out, s, k, r);
    }
  return out;
}

int rolltui_effect_map_equal(const RolltuiEffectMap* a, const RolltuiEffectMap* b) {
  if (a == b) return 1;
  if (!a || !b || a->states != b->states) return 0;
  for (std::size_t s = 0; s < a->states; ++s) {
    if (a->store[s].size() != b->store[s].size()) return 0;
    for (std::size_t i = 0; i < a->store[s].size(); ++i) {
      const RolltuiEffectMap::SpecStore& x = a->store[s][i];
      const RolltuiEffectMap::SpecStore& y = b->store[s][i];
      const RolltuiEffectSpec& vx = a->view[s][i];
      const RolltuiEffectSpec& vy = b->view[s][i];
      if (x.kind != y.kind || x.frames != y.frames || x.roles != y.roles) return 0;
      if (vx.period_ms != vy.period_ms || vx.width != vy.width || vx.steps != vy.steps || vx.backward != vy.backward)
        return 0;
    }
  }
  return 1;
}

int rolltui_effect_steps(const RolltuiEffectSpec* spec, int length) {
  if (spec->steps > 0) return spec->steps;
  const int len = std::max(1, length);
  const std::string_view kind = kind_of(*spec);
  if (kind == "spinner" || kind == "ellipsis") return std::max<int>(1, static_cast<int>(spec->frame_count));
  if (kind == "pulse" || kind == "gradient") return std::max<int>(1, static_cast<int>(spec->role_count));
  if (kind == "blink") return 2;
  if (kind == "shimmer") return len + sweep_width(*spec, len);
  if (kind == "bar") return 1;
  return len;  // a host's own kind, until its theme row says otherwise
}

void rolltui_effects_apply(RolltuiFrame* f, RolltuiEffectScratch* sc, const RolltuiStyle* styles, const void* host,
                           const RolltuiEffectMap* map, unsigned long long now_ms, int ambiguous_wide,
                           RolltuiEffectReport* rep, RolltuiEffectUnknownFn on_unknown, void* unknown_ctx) {
  *rep = RolltuiEffectReport{0, 0, 0};
  const int fw = rolltui_frame_width(f), fh = rolltui_frame_height(f);
  const std::size_t marks = rolltui_frame_mark_count(f);
  for (std::size_t im = 0; im < marks; ++im) {
    int mx = 0, my = 0, cells = 0, state = 0;
    unsigned long long since = 0;
    double fraction = 0;
    rolltui_frame_mark_at(f, im, &mx, &my, &cells, &state, &since, &fraction);
    if (cells <= 0 || state <= 0) continue;  // state 0 is None, and is never recorded anyway
    if (my < 0 || my >= fh) continue;
    const std::size_t n_specs = rolltui_effect_map_count(map, static_cast<std::size_t>(state));
    if (n_specs == 0) continue;
    // Each spec's kind is resolved ONCE PER MARK, not once per cell: the lookup is
    // loop-invariant, it is a string compare against the closed table, and for a HOST kind
    // it takes the registry's mutex — none of which belongs inside a per-cell draw loop.
    sc->res.resize(n_specs);
    {
      const std::lock_guard<std::mutex> lock(registry_mu());
      for (std::size_t k = 0; k < n_specs; ++k) {
        const RolltuiEffectSpec* spec = rolltui_effect_map_at(map, static_cast<std::size_t>(state), k);
        if (!resolve(kind_of(*spec), sc->res[k]) && on_unknown) on_unknown(unknown_ctx, spec->kind, spec->kind_len);
      }
    }
    bool any = false;
    const std::uint64_t elapsed = now_ms >= since ? now_ms - since : 0;
    for (int i = 0; i < cells; ++i) {
      const int x = mx + i;
      if (x < 0 || x >= fw) continue;
      RolltuiCell cell;
      rolltui_frame_cell(f, x, my, &cell);  // a COPY: the handle lends no reference into itself
      // Property 2, at its two edges: a continuation cell belongs to the glyph before it,
      // and a 2-cell glyph whose second half is outside the span is skipped WHOLE.
      if (cell.continuation) continue;
      if (cell.width == 2 && i + 1 >= cells) continue;
      RolltuiEffectCell in;
      in.elapsed_ms = elapsed;
      in.index = i;
      in.length = cells;
      in.fraction = clamp01(fraction);
      in.base = cell.style;
      in.ambiguous_wide = static_cast<unsigned char>(ambiguous_wide != 0);
      RolltuiEffectOut out;
      for (std::size_t k = 0; k < n_specs; ++k) {
        const Resolved& r = sc->res[k];
        if (r.builtin < 0 && !r.fn) continue;  // unknown: named above, and it draws nothing
        RolltuiEffectOut one;
        call_kind(*sc, r, *rolltui_effect_map_at(map, static_cast<std::size_t>(state), k), styles, host, in, one);
        // Stacking: the glyph comes from whichever kind last set one, the style likewise,
        // and a later kind sees the earlier one's style as its base — so "glyph from one,
        // colour from another" is the ordinary case rather than a special one.
        if (one.has_style) {
          out.has_style = 1;
          out.style = one.style;
          in.base = one.style;
        }
        if (one.has_glyph) {
          out.has_glyph = 1;
          out.glyph_len = one.glyph_len;
          if (one.glyph_len > 0 && one.glyph_len <= ROLLTUI_EFFECT_GLYPH_MAX)
            std::memcpy(out.glyph, one.glyph, one.glyph_len);
        }
      }
      if (!out.has_glyph && !out.has_style) continue;
      const RolltuiStyle style = out.has_style ? out.style : cell.style;
      if (out.has_glyph) {
        // PROPERTY 1, and it is enforced here rather than trusted: a glyph of the wrong
        // width is dropped and counted, never written. A glyph past the cap is refused the
        // same way and counted in the same number — its width is not knowable from the
        // bytes that fit (rolltui_effects.h).
        const int w = out.glyph_len <= ROLLTUI_EFFECT_GLYPH_MAX
                          ? frame_width(std::string_view(out.glyph, out.glyph_len), ambiguous_wide != 0)
                          : -1;
        if (w != cell.width) {
          ++rep->glyphs_refused;
          out.has_glyph = 0;
        }
      }
      if (out.has_glyph) {
        rolltui_frame_put(f, x, my, out.glyph, out.glyph_len, cell.width, style, cell.link);
      } else {
        rolltui_frame_set_style(f, x, my, style);
        // inside the span, by the guard above
        if (cell.width == 2 && x + 1 < fw) rolltui_frame_set_style(f, x + 1, my, style);
      }
      ++rep->cells_touched;
      any = true;
    }
    if (any) ++rep->marks_drawn;
  }
}

int rolltui_effects_tick_ms(const RolltuiFrame* f, const RolltuiEffectMap* map) {
  int best = 0;
  const std::size_t marks = rolltui_frame_mark_count(f);
  for (std::size_t im = 0; im < marks; ++im) {
    int mx = 0, my = 0, cells = 0, state = 0;
    unsigned long long since = 0;
    double fraction = 0;
    rolltui_frame_mark_at(f, im, &mx, &my, &cells, &state, &since, &fraction);
    if (cells <= 0 || state <= 0) continue;
    const std::size_t n_specs = rolltui_effect_map_count(map, static_cast<std::size_t>(state));
    for (std::size_t k = 0; k < n_specs; ++k) {
      const RolltuiEffectSpec& s = *rolltui_effect_map_at(map, static_cast<std::size_t>(state), k);
      if (s.period_ms <= 0) continue;  // a still effect asks for no wakeup
      if (!rolltui_effect_kind_resolves(s.kind, s.kind_len)) continue;  // nor one that cannot draw
      const int steps = std::max(1, rolltui_effect_steps(&s, cells));
      const int ms = std::max(16, s.period_ms / steps);
      if (best == 0 || ms < best) best = ms;
    }
  }
  return best;
}

}  // extern "C"
