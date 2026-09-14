//
// effects_test.cpp — MOTION IS THE THEME'S, A STATE IS THE WIDGET'S (rolltui/Effects.hpp),
// and this file is where that claim stops being a paragraph.
//
// The two properties are asserted OVER EVERY REGISTERED KIND — the seven built-ins plus
// two host kinds registered here, one well-behaved and one that deliberately misbehaves:
//
//   1. an effect never changes a span's WIDTH
//   2. an effect never writes OUTSIDE its span
//
// The misbehaving kind is the point of the sweep and not an extra case: it returns a
// two-cell glyph for a one-cell cell, an empty one, and a style — and the frame outside
// its span, and every cell width inside it, must come out identical anyway. That is the
// difference between a guarantee and a convention, and it is what makes "a host may
// register a kind" safe to offer at all.
//
// The tick rule ("no wakeups with no marks") is asserted as a property of the pure
// function BOTH hosts route through, over every built-in theme including the two that map
// every state — so it cannot be true only for the theme that happens to be loaded.
//
// calls the C API (rolltui/c/rolltui_effects.h, rolltui_screen.h,
// rolltui_frame_ops.h, rolltui_render.h, rolltui_theme.h, rolltui_json.h, all reached
// through rolltui/rolltui.h) directly for the frame, the theme and the effects engine. There is
// no C++ wrapper layer: this suite calls the C API the same way a host does.
//
// `Role` (Style.hpp) and `EffectState`/`effect_state_name`/`effect_state_from_name`
// (Effects.hpp) have NO C form at all and are stated to stay in ONE language, permanently
// (Effects.cpp's own comment: "the boundary deliberately does not know [the state
// vocabulary]... so the C indexes the per-state arrays with it and the names stay in one
// language" — the same is true of a role's NAME, rolltui_style.h's header comment: "a C
// file names no role"). Both are reproduced below exactly as
// rolltui/tests/theme_test.cpp already reproduces `Role` independently. `EffectMap`
// (Effects.hpp) was a thin `unique_ptr<RolltuiEffectMap>` RAII shape over
// `rolltui_effect_map_*`; it is reproduced the same way, as a local holder over the same C
// calls its methods forwarded to verbatim — a theme's own effects map still needs an
// owner, and the boundary rule is "working memory is a caller-owned handle", not "no
// handle at all". Everything else this file touches — the frame, the registry, the
// applier, the tick, the theme loader/dumper, the json tree — was already a direct C call
// before this pass and is unchanged.
//
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_effects.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_frame_ops.h"  /* INTERNAL: shade, the frame-level half of "a mark never paints a popup" */

#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_theme.h"  // INTERNAL: this test opts in

using namespace rolltui_test;
using namespace testkit;

namespace {

// THE ROLE ORDER, FROM THE LIBRARY — expanded from `ROLLTUI_ROLE_LIST`, not
// reproduced. This was one of SIX verbatim copies of the 49 names IN ORDER, each written when
// the role vocabulary was still C++ and a converted suite had no way to ask for it. A style
// table is indexed by this ordinal, so the order is ABI and a copy of it is a copy of ABI.
enum class Role : unsigned char {
#define ROLLTUI_TEST_ROLE_(lower, UPPER) lower,
  ROLLTUI_ROLE_LIST(ROLLTUI_TEST_ROLE_)
#undef ROLLTUI_TEST_ROLE_
  count_
};
constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::count_);
static_assert(static_cast<unsigned char>(Role::text) == ROLLTUI_ROLE_DEFAULT_TEXT,
              "the C side's default entry role must be Role::text");
static_assert(static_cast<unsigned char>(Role::background) == ROLLTUI_ROLE_DEFAULT_BACKGROUND,
              "the C side's default node background must be Role::background");
static_assert(static_cast<unsigned char>(Role::prompt) == ROLLTUI_ROLE_DEFAULT_PROMPT,
              "the C side's default input prompt role must be Role::prompt");

// THE ROLE NAMES, FROM THE LIBRARY. This was a verbatim copy of the 49 names when this file
// was converted, on the precedent of the one in `theme_test.cpp` commented "reproduced" —
// which was itself a defect, deleted the same day along with the rule that made it look
// necessary ("a C file names no role"). `ROLLTUI_ROLE_LIST` in `rolltui/c/rolltui_style.h` is
// the one list now, and this asks the library for it.
const std::array<const char*, kRoleCount>& kRoleNamesTable() {
  static const std::array<const char*, kRoleCount> t = [] {
    std::array<const char*, kRoleCount> a{};
    for (std::size_t i = 0; i < kRoleCount; ++i)
      a[i] = rolltui_role_name(static_cast<unsigned char>(i), nullptr);
    return a;
  }();
  return t;
}

// `rolltui::Style` (Style.hpp) was a one-definition alias over the same C struct --
// reproduced verbatim; there was never a second definition to convert away from.
using Style = RolltuiStyle;

// DERIVED from `ROLLTUI_EFFECT_STATE_LIST`. This file used to declare the enum by
// hand next to a verbatim copy of the names, on the rule that "names stay in one language" —
// the rule that has since been reversed, because a vocabulary the C refuses to carry does not
// disappear, it relocates into every caller that cannot reach it.
enum class EffectState : std::uint8_t {
#define ROLLTUI_EFFECT_STATE_CPP_(lower, UPPER, Camel) Camel = ROLLTUI_EFFECT_STATE_##UPPER,
  ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_EFFECT_STATE_CPP_)
#undef ROLLTUI_EFFECT_STATE_CPP_
  count_ = ROLLTUI_EFFECT_STATE_COUNT
};
inline constexpr std::size_t kEffectStateCount = static_cast<std::size_t>(EffectState::count_);

// THE EFFECT-STATE NAMES, FROM THE LIBRARY — same story as the roles above. The copy this
// replaces carried the comment "names stay in one language", which was the rule at the time
// and is what made the duplication look correct.
inline std::string_view effect_state_name(EffectState s) {
  std::size_t len = 0;
  const char* n = rolltui_effect_state_name(static_cast<unsigned char>(s), &len);
  return std::string_view(n, len);
}

inline EffectState effect_state_from_name(std::string_view name) {
  const int i = rolltui_effect_state_from_name(name.data(), name.size());
  return i < 0 ? EffectState::count_ : static_cast<EffectState>(i);
}

// ---- mirrors rolltui::EffectMap (Effects.hpp): OWNED, through a unique_ptr with a deleter
// that calls the C free -- the same RAII shape the real class used, over the same
// rolltui_effect_map_* calls its methods forwarded to verbatim. ----
class EffectMap {
 public:
  struct Handle {
    void operator()(RolltuiEffectMap* p) const { rolltui_effect_map_free(p); }
  };
  // THE FALLBACK ROLE IS HANDED OVER ONCE, HERE -- same as the real class: it is the only
  // place that says which role an effect with none of its own picks.
  EffectMap() : m_(rolltui_effect_map_new(kEffectStateCount, static_cast<unsigned char>(Role::accent_1))) {}
  // ADOPTS an already-built map (the theme loader/built-in filler hand one back in C).
  explicit EffectMap(RolltuiEffectMap* adopt) noexcept : m_(adopt) {}
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
  const RolltuiEffectSpec& at(EffectState s, std::size_t i) const { return *rolltui_effect_map_at(m_.get(), index(s), i); }

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

  void set_jitter(EffectState st, std::size_t i, int j) {
    rolltui_effect_map_set_jitter(m_.get(), index(st), i, j);
  }

  const RolltuiEffectMap* handle() const { return m_.get(); }

 private:
  static std::size_t index(EffectState s) {
    return static_cast<std::size_t>(s) < kEffectStateCount ? static_cast<std::size_t>(s) : 0;
  }
  std::unique_ptr<RolltuiEffectMap, Handle> m_;
};

// ---- mirrors rolltui::Theme (Theme.hpp) and rolltui::json::Value (Json.hpp): the Theme
// struct itself, its load report, its mode enum, the built-in cache, the vocab table, the
// JSON loader and dumper -- and, nested so every `json::`-qualified call site below stays
// unchanged text, json::Value/parse/dump. ----
struct Theme {
  std::string name;
  std::array<RolltuiStyle, kRoleCount> styles{};
  EffectMap effects;
  const RolltuiStyle& style(Role r) const {
    return *rolltui_theme_style(styles.data(), styles.size(), static_cast<unsigned char>(r));
  }
};

struct ThemeLoadReport {
  std::string error;
  std::vector<std::string> missing_roles;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && missing_roles.empty() && unknown_keys.empty() && bad_values.empty(); }
};

enum class ThemeMode : unsigned char { Dark, Light };

// Mirrors rolltui::builtin_theme's cache (Theme.cpp): filled once from the C built-ins,
// keyed by name -- this file's Theme really has effects, so the adopted RolltuiEffectMap*
// is kept this time (unlike the lighter mirrors in theme_test.cpp/menu_test.cpp).
const Theme* builtin_theme(std::string_view name) {
  static const std::vector<std::pair<std::string, Theme>> cache = [] {
    std::vector<std::pair<std::string, Theme>> v;
    const std::size_t n = rolltui_theme_builtin_count();
    v.reserve(n);  // pointer stability: builtin_theme() hands back &t into this vector
    for (std::size_t i = 0; i < n; ++i) {
      const char* nm = rolltui_theme_builtin_name(i);
      Theme t;
      t.name = nm;
      if (RolltuiEffectMap* m = rolltui_theme_builtin_fill(nm, std::strlen(nm), t.styles.data(), t.styles.size()))
        t.effects = EffectMap(m);  // ADOPTS; NULL only on a role-count mismatch
      v.emplace_back(nm, t);
    }
    return v;
  }();
  for (const auto& [n, t] : cache)
    if (n == name) return &t;
  return nullptr;
}
std::vector<std::string_view> builtin_theme_names() {
  std::vector<std::string_view> out;
  const std::size_t n = rolltui_theme_builtin_count();
  for (std::size_t i = 0; i < n; ++i) out.push_back(rolltui_theme_builtin_name(i));
  return out;
}

// Mirrors Theme.cpp's VocabTables/theme_vocab(): the pointer tables the C loader/dumper
// take once per call. `kRoleNames` is already `const char* const*`-shaped, so only the
// state-name table needs building.
const RolltuiThemeVocab& theme_vocab() {
  static const struct VocabTables {
    std::array<const char*, kEffectStateCount> state_names{};
    RolltuiThemeVocab vocab{};
    VocabTables() {
      for (std::size_t i = 0; i < kEffectStateCount; ++i)
        state_names[i] = effect_state_name(static_cast<EffectState>(i)).data();
      vocab.role_names = kRoleNamesTable().data();
      vocab.role_count = kRoleCount;
      vocab.text_role = static_cast<std::size_t>(Role::text);
      vocab.state_names = state_names.data();
      vocab.state_count = kEffectStateCount;
      vocab.fallback_effect_role = static_cast<unsigned char>(Role::accent_1);
    }
  } t;
  return t.vocab;
}

// ---- mirrors rolltui::json::Value (Json.hpp): an OWNED tree over the C API, since Value's
// own C++ shape is explicitly NOT ported to C (rolltui_json.h's own header comment). NESTED
// in a `json` namespace so every `json::`-qualified call site below is unchanged text --
// same mirror rolltui/tests/theme_test.cpp has independently, plus `.has()`, which this
// file's file-format test needs. ----
namespace json {

class Value {
 public:
  Value() : v_(rolltui_json_null()) {}
  explicit Value(RolltuiJsonValue* owned) : v_(owned ? owned : rolltui_json_null()) {}
  Value(const Value& o) : v_(rolltui_json_clone(o.v_)) {}
  Value& operator=(const Value& o) {
    if (this != &o) {
      rolltui_json_free(v_);
      v_ = rolltui_json_clone(o.v_);
    }
    return *this;
  }
  Value(Value&& o) noexcept : v_(o.v_) { o.v_ = nullptr; }
  Value& operator=(Value&& o) noexcept {
    if (this != &o) {
      rolltui_json_free(v_);
      v_ = o.v_;
      o.v_ = nullptr;
    }
    return *this;
  }
  ~Value() { rolltui_json_free(v_); }

  bool is_null() const { return rolltui_json_is_null(v_) != 0; }
  bool is_bool() const { return rolltui_json_is_bool(v_) != 0; }
  bool is_number() const { return rolltui_json_is_number(v_) != 0; }
  bool is_string() const { return rolltui_json_is_string(v_) != 0; }
  bool is_array() const { return rolltui_json_is_array(v_) != 0; }
  bool is_object() const { return rolltui_json_is_object(v_) != 0; }

  double as_number(double def = 0) const { return rolltui_json_as_number(v_, def); }
  bool as_bool(bool def = false) const { return rolltui_json_as_bool(v_, def) != 0; }
  std::string as_string(std::string_view def = "") const {
    std::size_t n = 0;
    const char* p = rolltui_json_as_string(v_, def.data(), def.size(), &n);
    return std::string(p, n);
  }

  Value get(std::string_view key) const { return Value(rolltui_json_clone(rolltui_json_get(v_, key.data(), key.size()))); }
  Value operator[](std::string_view key) const { return get(key); }
  bool has(std::string_view key) const { return rolltui_json_has(v_, key.data(), key.size()) != 0; }

  std::size_t array_size() const { return rolltui_json_array_size(v_); }
  Value array_at(std::size_t i) const { return Value(rolltui_json_clone(rolltui_json_array_at(v_, i))); }

  // TAKES OWNERSHIP of child, matching rolltui_json_set exactly.
  Value& set(std::string_view key, Value child) {
    rolltui_json_set(v_, key.data(), key.size(), child.release());
    return *this;
  }

  const RolltuiJsonValue* handle() const { return v_; }
  RolltuiJsonValue* release() {
    RolltuiJsonValue* p = v_;
    v_ = rolltui_json_null();
    return p;
  }

 private:
  RolltuiJsonValue* v_;
};

Value parse(std::string_view text, std::string& error) {
  RolltuiStr err{};
  RolltuiJsonValue* v = rolltui_json_parse(text.data(), text.size(), &err);
  error = str_of(err);
  rolltui_str_free(&err);
  return Value(v);
}
std::string dump(const Value& v, int indent = 2) {
  RolltuiStr out{};
  rolltui_json_dump(v.handle(), indent, &out);
  std::string result = str_of(out);
  rolltui_str_free(&out);
  return result;
}

}  // namespace json

// Mirrors rolltui::load_theme(string_view, ...) and rolltui::load_theme(const json::Value&,
// ...) (Theme.cpp): both delegate to theme_from_c_root, the ONE difference being where the
// tree comes from. The json::Value overload is SIMPLER than the shim: no json::Value<->C
// conversion is needed at all, since this mirror's Value already IS a RolltuiJsonValue*.
std::optional<Theme> theme_from_c_root(const RolltuiJsonValue* root_c, ThemeMode mode, ThemeLoadReport& report) {
  report = ThemeLoadReport{};
  Theme t;
  RolltuiStr name{};
  RolltuiThemeReport rep{};
  RolltuiEffectMap* eff = rolltui_theme_load(root_c, static_cast<int>(mode), &theme_vocab(), t.styles.data(), &name, &rep);
  report.error = str_of(rep.error);
  for (std::size_t i = 0; i < rep.missing_roles_n; ++i) report.missing_roles.push_back(str_of(rep.missing_roles[i]));
  for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) report.unknown_keys.push_back(str_of(rep.unknown_keys[i]));
  for (std::size_t i = 0; i < rep.bad_values_n; ++i) report.bad_values.push_back(str_of(rep.bad_values[i]));
  rolltui_theme_report_release(&rep);
  if (!eff) {
    rolltui_str_free(&name);
    return std::nullopt;
  }
  t.name = str_of(name);
  rolltui_str_free(&name);
  t.effects = EffectMap(eff);  // ADOPTS
  return t;
}
std::optional<Theme> load_theme(std::string_view json_text, ThemeMode mode, ThemeLoadReport& report) {
  RolltuiStr err{};
  RolltuiJsonValue* root_c = rolltui_json_parse(json_text.data(), json_text.size(), &err);
  if (!root_c) {
    report = ThemeLoadReport{};
    report.error = str_of(err);
    rolltui_str_free(&err);
    return std::nullopt;
  }
  rolltui_str_free(&err);
  std::optional<Theme> t = theme_from_c_root(root_c, mode, report);
  rolltui_json_free(root_c);
  return t;
}
std::optional<Theme> load_theme(const json::Value& root, ThemeMode mode, ThemeLoadReport& report) {
  return theme_from_c_root(root.handle(), mode, report);
}

// Mirrors rolltui::theme_to_json (Theme.cpp), minus the "meta" this fixture's themes never
// carry (Theme::meta is a json::Value field this mirror does not reproduce, same as
// theme_test.cpp's).
std::string theme_to_json(const Theme& theme) {
  json::Value root(rolltui_json_object());
  root.set("name", json::Value(rolltui_json_string(theme.name.data(), theme.name.size())));
  RolltuiJsonValue* c = rolltui_theme_dump(theme.styles.data(), theme.effects.handle(), nullptr, nullptr, &theme_vocab());
  root.set("roles", json::Value(rolltui_json_clone(rolltui_json_get(c, "roles", 5))));
  const RolltuiJsonValue* fx = rolltui_json_get(c, "effects", 7);
  if (!rolltui_json_is_null(fx)) root.set("effects", json::Value(rolltui_json_clone(fx)));
  rolltui_json_free(c);
  return json::dump(root, 2) + "\n";
}
// Mirrors rolltui::theme_pair_to_json_value (Theme.cpp), minus "meta" for the same reason.
json::Value theme_pair_to_json_value(const Theme& dark, const Theme& light, std::string_view name) {
  json::Value root(rolltui_json_object());
  root.set("name", json::Value(rolltui_json_string(name.data(), name.size())));
  RolltuiJsonValue* c = rolltui_theme_dump(dark.styles.data(), dark.effects.handle(), light.styles.data(),
                                          light.effects.handle(), &theme_vocab());
  root.set("roles", json::Value(rolltui_json_clone(rolltui_json_get(c, "roles", 5))));
  const RolltuiJsonValue* fx = rolltui_json_get(c, "effects", 7);
  if (!rolltui_json_is_null(fx)) root.set("effects", json::Value(rolltui_json_clone(fx)));
  rolltui_json_free(c);
  return root;
}

// ---- mirrors the one Unicode.hpp function this file used, NESTED so every
// `unicode::`-qualified call site below is unchanged text. ----
namespace unicode {
RolltuiUnicodeScratch* scratch() {
  static std::unique_ptr<RolltuiUnicodeScratch, void (*)(RolltuiUnicodeScratch*)> s(rolltui_u_scratch_new(),
                                                                                    rolltui_u_scratch_free);
  return s.get();
}
int display_width(std::string_view utf8, bool ambiguous_wide = false) {
  return rolltui_u_display_width(scratch(), utf8.data(), utf8.size(), ambiguous_wide);
}
}  // namespace unicode

// ---- the frame: OWNED, an explicit new/free pair, the same RAII shape rolltui::Frame
// gave a caller before the port (Screen.cpp WAS this mapping). ----
using FramePtr = std::unique_ptr<RolltuiFrame, void (*)(RolltuiFrame*)>;
FramePtr new_frame(int w, int h, RolltuiStyle fill = {}) {
  return FramePtr(rolltui_frame_new(w, h, fill), rolltui_frame_free);
}

RolltuiDrawScratch* draw_scratch() {
  static std::unique_ptr<RolltuiDrawScratch, void (*)(RolltuiDrawScratch*)> s(rolltui_draw_scratch_new(),
                                                                              rolltui_draw_scratch_free);
  return s.get();
}
RolltuiEffectScratch* effect_scratch() {
  static std::unique_ptr<RolltuiEffectScratch, void (*)(RolltuiEffectScratch*)> s(rolltui_effect_scratch_new(),
                                                                                  rolltui_effect_scratch_free);
  return s.get();
}

RolltuiCell cell_at(const RolltuiFrame* f, int x, int y) {
  RolltuiCell c{};
  rolltui_frame_cell(f, x, y, &c);
  return c;
}
std::string_view glyph_at(const RolltuiFrame* f, int x, int y) {
  std::size_t n = 0;
  const char* p = rolltui_frame_glyph(f, x, y, &n);
  return std::string_view(p, n);
}
std::string frame_to_text(const RolltuiFrame* f) {
  RolltuiStr s;
  rolltui_frame_to_text(f, &s);
  return str_of(s);
}

// ---- the host's two kinds, and a no-op placeholder for the refusal cases. A host kind
// registered from C crosses as {function pointer, void* ctx, void (*free_ctx)(void*)}
// (rolltui_effects.h); none of the three below needs a context. `styles` is the theme's
// own role table — the SAME array `Theme::style(Role)` indexes — so a kind reaches a
// role's Style directly through it rather than through `host` (which the C dereferences
// only if a host's own callback chooses to, and neither of these does). ----
void noop_kind(void*, const RolltuiEffectSpec*, const RolltuiStyle*, const void*, const RolltuiEffectCell*,
              RolltuiEffectOut*) {}

void host_sweep_kind(void*, const RolltuiEffectSpec* s, const RolltuiStyle* styles, const void*,
                     const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  if ((in->index + static_cast<int>(in->elapsed_ms / 100)) % 2) return;
  out->has_style = 1;
  out->style = styles[s->role(0)];  // never empty: the map substitutes its fallback
  out->set_glyph("#", 1);
}

// THE MISBEHAVING ONE. It tries every way a callback could corrupt a frame that the
// signature allows: a glyph twice as wide as the cell, an empty glyph, and a style.
void wide_liar_kind(void*, const RolltuiEffectSpec*, const RolltuiStyle* styles, const void*,
                    const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  out->has_style = 1;
  out->style = styles[static_cast<unsigned char>(Role::error)];
  out->set_glyph(std::string_view((in->index % 2) ? "" : "\xE4\xBD\xA0").data(), std::string_view((in->index % 2) ? "" : "\xE4\xBD\xA0").size());  // 0 cells / 2 cells
}

// ---- rung 2, and the registry vocabulary — the direct C calls Effects.cpp's shim made
// on a caller's behalf; a caller now makes them itself. ----
// rung 2 is a CONTEXT's, so this suite registers into one session that every shim
// below resolves against. The helper is `rolltui_test.hpp`'s — see the note there for why a
// suite's session is not shaped like a host's.
RolltuiContext* test_ctx() { return rolltui_test::test_context(); }
bool register_effect_kind(std::string_view name, RolltuiEffectFn fn, std::string* why) {
  auto fail = [&](std::string reason) {
    if (why) *why = std::move(reason);
    return false;
  };
  const int code = rolltui_effect_register(test_ctx(), name.data(), name.size(), fn, nullptr, nullptr);
  switch (code) {
    case ROLLTUI_EFFECT_OK:
      return true;
    case ROLLTUI_EFFECT_NO_NAME:
      return fail("an effect kind needs a name");
    case ROLLTUI_EFFECT_NO_FN:
      return fail("effect kind '" + std::string(name) + "': no function");
    case ROLLTUI_EFFECT_IS_BUILTIN:
      return fail("'" + std::string(name) + "' is one of the library's own effect kinds");
    default:
      return fail("effect kind '" + std::string(name) + "' is already registered");
  }
}
void clear_registered_effect_kinds() { rolltui_effect_clear_registered(test_ctx()); }
std::vector<std::string> effect_kind_names() {
  std::vector<std::string> out;
  const std::size_t n = rolltui_effect_kind_count(test_ctx());
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t len = 0;
    const char* p = rolltui_effect_kind_name(test_ctx(), i, &len);
    out.emplace_back(p, len);
  }
  return out;
}
bool effect_kind_resolves(std::string_view name) {
  return rolltui_effect_kind_resolves(test_ctx(), name.data(), name.size()) != 0;
}
bool is_builtin_effect_kind(std::string_view name) { return rolltui_effect_is_builtin(name.data(), name.size()) != 0; }

// ---- applying, and the tick — the direct C calls, over a Theme's OWN EffectMap
// (theme.effects.handle()/.empty(), both read-only calls on the local EffectMap above). ----
struct EffectReport {
  int marks_drawn = 0;
  int cells_touched = 0;
  int glyphs_refused = 0;
  std::vector<std::string> unknown_kinds;
  bool clean() const { return glyphs_refused == 0 && unknown_kinds.empty(); }
};
void note_unknown_kind(void* ctx, const char* kind, std::size_t len) {
  std::vector<std::string>& out = *static_cast<std::vector<std::string>*>(ctx);
  const std::string_view name(kind, len);
  if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
}
EffectReport apply_effects(RolltuiFrame* f, const Theme& theme, std::uint64_t now_ms, bool ambiguous_wide = false) {
  EffectReport rep;
  if (rolltui_frame_mark_count(f) == 0 || theme.effects.empty()) return rep;
  RolltuiEffectReport r{};
  rolltui_effects_apply(test_ctx(), f, effect_scratch(), theme.styles.data(), nullptr, theme.effects.handle(), now_ms,
                        ambiguous_wide, &r, note_unknown_kind, &rep.unknown_kinds);
  rep.marks_drawn = r.marks_drawn;
  rep.cells_touched = r.cells_touched;
  rep.glyphs_refused = r.glyphs_refused;
  return rep;
}
std::optional<int> effect_tick_ms(const RolltuiFrame* f, const Theme& theme) {
  if (rolltui_frame_mark_count(f) == 0 || theme.effects.empty()) return std::nullopt;
  const int ms = rolltui_effects_tick_ms(test_ctx(), f, theme.effects.handle());
  return ms > 0 ? std::optional<int>(ms) : std::nullopt;
}
int poll_timeout_ms(const RolltuiFrame* f, const Theme& theme, int idle_ms) {
  const std::optional<int> tick = effect_tick_ms(f, theme);
  if (!tick) return idle_ms;
  return idle_ms <= 0 ? *tick : std::min(idle_ms, *tick);
}

// A theme mapping ONE state to one spec, so a kind can be exercised on its own. A spec is
// BUILT INTO the map now (the theme owns its specs in C), so these helpers
// take what a spec is made of rather than a spec.
Theme theme_with(EffectState state, std::string_view kind, int period_ms = 800,
                 std::vector<std::string> frames = {}, std::vector<Role> roles = {}, int width = 0) {
  Theme t = *builtin_theme("default-dark");
  t.effects.clear();
  const std::size_t i = t.effects.add(state, kind, period_ms, width);
  for (const std::string& f : frames) t.effects.add_frame(state, i, f);
  for (Role r : roles) t.effects.add_role(state, i, r);
  return t;
}

// Every kind, with a theme that gives it what it needs — the sweep's table. A kind with
// no frames or no roles is a legitimate theme file (and must not crash), but it would
// also do nothing, which is not what the properties need to be tested against.
struct KindCase {
  std::string kind;
  Theme theme;
};

std::vector<KindCase> kind_cases() {
  std::vector<KindCase> out;
  for (const std::string& name : effect_kind_names()) {
    // one cell each: what a well-formed theme file carries
    std::vector<std::string> frames = {"a", "b", "c"};
    if (name == "wide-liar") frames = {"\xE4\xBD\xA0"};  // 2 cells — refused, never written
    out.push_back({name, theme_with(EffectState::Waiting, name, 400, frames,
                                    {Role::accent_1, Role::accent_2, Role::error})});
  }
  return out;
}

struct CellShot {
  std::string text;
  std::uint8_t width;
  bool continuation;
  Style style;
  std::uint32_t link;
  bool operator==(const CellShot&) const = default;
};

std::vector<CellShot> shoot(const RolltuiFrame* f) {
  std::vector<CellShot> out;
  const int h = rolltui_frame_height(f), w = rolltui_frame_width(f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const RolltuiCell c = cell_at(f, x, y);  // BY VALUE (Phase 14 m2): the frame lends no reference
      out.push_back({std::string(glyph_at(f, x, y)), c.width, c.continuation != 0, c.style, c.link});
    }
  return out;
}

// A frame with a known pattern, a run of text on row 1 including a WIDE glyph, and
// sentinel text on every other row.
FramePtr make_frame(int w, int h, const Theme& theme) {
  FramePtr f = new_frame(w, h, theme.style(Role::background));
  for (int y = 0; y < h; ++y) {
    const std::string_view row = "0123456789abcdefghij";
    rolltui_frame_put_text(f.get(), draw_scratch(), 0, y, row.data(), row.size(), theme.style(Role::text), w, 0, 0);
  }
  if (h > 1) {
    const std::string_view row2 = "ab\xE4\xBD\xA0"  // two narrow, one wide (2 cells)
                                  "cdefghij";
    rolltui_frame_put_text(f.get(), draw_scratch(), 2, 1, row2.data(), row2.size(), theme.style(Role::md_code_block),
                           w, 0, 0);
  }
  return f;
}

}  // namespace

int main() {
  // ---- the vocabulary --------------------------------------------------------------
  {
    check(effect_state_from_name("waiting") == EffectState::Waiting && effect_state_name(EffectState::Flash) == "flash",
          "state names round-trip");
    check(effect_state_from_name("nope") == EffectState::count_, "an unknown state name is count_, not a silent 'none'");
    EffectMap empty;
    check(empty.empty() && empty.count(EffectState::Waiting) == 0, "a theme that maps nothing is empty by construction");
  }

  // ---- the two rungs ---------------------------------------------------------------
  {
    const std::vector<std::string> names = effect_kind_names();
    for (const char* k : {"spinner", "ellipsis", "bar", "pulse", "shimmer", "gradient", "blink"})
      check(effect_kind_resolves(k) && is_builtin_effect_kind(k), std::string("built-in kind '") + k + "' resolves");
    check(names.size() == 7, "the library's table is CLOSED at seven kinds (" + std::to_string(names.size()) + ")");
    std::string why;
    check(!register_effect_kind("spinner", noop_kind, &why) && why.find("library's own") != std::string::npos,
          "registering a LIBRARY kind is refused by name [" + why + "]");
    check(!register_effect_kind("", noop_kind, &why), "an empty kind name is refused");
    check(!effect_kind_resolves("confetti"), "an unregistered name resolves to nothing (a HOST fact, not a theme error)");
  }

  // ---- the host's two kinds: one well-behaved, one that lies ------------------------
  {
    std::string why;
    // A well-behaved host kind: one cell wide, a role it was handed, no colour of its own.
    check(register_effect_kind("host-sweep", host_sweep_kind, &why), "a host registers its own kind [" + why + "]");
    check(register_effect_kind("wide-liar", wide_liar_kind, &why),
          "…and a kind that LIES about its width, which is the sweep's control");
    check(!register_effect_kind("wide-liar", noop_kind, &why), "a second registration of the same name is refused");
    check(effect_kind_names().size() == 9, "both appear after the library's seven, in resolution order");
  }

  // ---- RUNG 2 IS A SESSION'S ------------------------------------------
  // The same proof `c_consumer_test` makes for widget kinds, made here because an effect
  // kind is registered through an INTERNAL header that the pure-C consumer does not include.
  // Two contexts, one registration, and the library's own rung answering identically in both.
  {
    RolltuiContext* a = rolltui_context_new();
    RolltuiContext* b = rolltui_context_new();
    check(rolltui_effect_register(a, "gauge", 5, noop_kind, nullptr, nullptr) == ROLLTUI_EFFECT_OK,
          "a kind registers in the FIRST of two sessions");
    check(rolltui_effect_kind_resolves(a, "gauge", 5) != 0, "…and the session that registered it resolves it");
    check(rolltui_effect_kind_resolves(b, "gauge", 5) == 0,
          "…AND THE SECOND DOES NOT SEE IT AT ALL — the effect registries are separate");
    check(rolltui_effect_kind_count(a) == rolltui_effect_kind_count(b) + 1,
          "…one row in the first, none in the second");
    check(rolltui_effect_kind_resolves(a, "spinner", 7) != 0 && rolltui_effect_kind_resolves(b, "spinner", 7) != 0,
          "…while the library's own rung answers the same in both, because rung 1 is not a session's");
    // FREED IN SEQUENCE, never asserted while both are alive: the allocator counters are a
    // process-wide atomic SUM (contract point 6), so "this context holds nothing" is a claim
    // about the last one standing.
    rolltui_context_free(b);
    check(rolltui_effect_kind_resolves(a, "gauge", 5) != 0, "…and freeing one leaves the other's registry intact");
    rolltui_context_free(a);
    check(rolltui_effect_kind_resolves(nullptr, "gauge", 5) == 0 &&
              rolltui_effect_kind_resolves(nullptr, "spinner", 7) != 0,
          "…and a NULL context is a session with no host kinds: rung 1 answers, rung 2 is empty");
  }

  // ---- RUNG 2 FOR STATES: a host names what its widget marks with -----------------------
  // The library's six states are a transcript's. A host whose widget has states of its own
  // registers them by name, marks with the index it is handed, and a theme file maps them under
  // `effects` exactly as it maps the library's — read against the SESSION's vocabulary.
  {
    RolltuiContext* a = rolltui_context_new();
    int folder = -1, again = -1, none = -1;
    check(rolltui_effect_state_register(a, "dirk.folder", 11, &folder) == ROLLTUI_EFFECT_OK &&
              folder == static_cast<int>(ROLLTUI_EFFECT_STATE_COUNT),
          "a host state is registered and handed the first index past the library's six");
    check(rolltui_effect_state_register(a, "waiting", 7, &none) == ROLLTUI_EFFECT_IS_BUILTIN && none == -1,
          "…a library state's name is refused: rung 1 is never shadowed");
    check(rolltui_effect_state_register(a, "none", 4, &none) == ROLLTUI_EFFECT_IS_BUILTIN,
          "…and so is \"none\", which is rung 1's too");
    check(rolltui_effect_state_register(a, "dirk.folder", 11, &again) == ROLLTUI_EFFECT_DUPLICATE && again == folder,
          "…a second registration is refused and says which index the name already has");
    check(rolltui_effect_state_resolve(a, "dirk.folder", 11) == folder && rolltui_effect_state_resolve(a, "flash", 5) == ROLLTUI_EFFECT_STATE_FLASH &&
              rolltui_effect_state_resolve(a, "dirk.nope", 9) == -1,
          "resolution walks both rungs and answers -1 for neither");
    check(rolltui_effect_state_count(a) == ROLLTUI_EFFECT_STATE_COUNT + 1, "the session's state count is the library's plus the host's");
    const RolltuiThemeVocab* v = rolltui_theme_vocab(a);
    check(v->state_count == ROLLTUI_EFFECT_STATE_COUNT + 1 && std::string(v->state_names[folder]) == "dirk.folder" &&
              std::string(v->state_names[ROLLTUI_EFFECT_STATE_WAITING]) == "waiting" && v->role_count == ROLLTUI_ROLE_COUNT,
          "the session's vocabulary names the host state after the library's, roles untouched");
    RolltuiContext* b = rolltui_context_new();
    check(rolltui_theme_vocab(b) == rolltui_theme_default_vocab() && rolltui_effect_state_resolve(b, "dirk.folder", 11) == -1,
          "…and a session that registered nothing has the default vocabulary and cannot see another's state");

    // A THEME FILE maps the host state by name — through the session's vocabulary, and NOT
    // through the default one, where the same key is unknown.
    const char* text = R"({ "name": "fx", "roles": { "text": { "fg": "none" } },
      "effects": { "dirk.folder": { "kind": "gauge", "roles": ["accent_1"], "period_ms": 500 } } })";
    RolltuiStr err{};
    RolltuiJsonValue* root = rolltui_json_parse(text, std::strlen(text), &err);
    check(root != nullptr, "the theme text parses");
    RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
    RolltuiStr name{};
    RolltuiThemeReport rep{};
    RolltuiEffectMap* m = rolltui_theme_load(root, ROLLTUI_MODE_DARK, rolltui_theme_vocab(a), styles, &name, &rep);
    check(m != nullptr && rolltui_effect_map_count(m, static_cast<std::size_t>(folder)) == 1 && rep.unknown_keys_n == 0,
          "a theme read against the session's vocabulary maps the host state");
    rolltui_str_free(&name);
    rolltui_theme_report_release(&rep);
    RolltuiThemeReport rep2{};
    RolltuiEffectMap* m2 = rolltui_theme_load(root, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), styles, &name, &rep2);
    check(m2 != nullptr && rep2.unknown_keys_n == 1 && std::string(rep2.unknown_keys[0].c_str()).find("dirk.folder") != std::string::npos,
          "…and the same file read against the DEFAULT vocabulary reports the state as an unknown key — the control");
    rolltui_str_free(&name);
    rolltui_theme_report_release(&rep2);
    rolltui_json_free(root);

    // THE MERGE: an app's own mapping file onto a map built without its states — the map is
    // widened, the rows added, the theme's own rows kept.
    const char* app_file = R"({ "effects": { "dirk.folder": { "kind": "gauge", "role": "accent_2" }, "nosuch": { "kind": "blink" } } })";
    RolltuiThemeReport rep3{};
    check(rolltui_theme_effects_merge(m2, app_file, std::strlen(app_file), rolltui_theme_vocab(a), &rep3) == 1 &&
              rolltui_effect_map_count(m2, static_cast<std::size_t>(folder)) == 1 && rolltui_effect_map_count(m2, ROLLTUI_EFFECT_STATE_WAITING) == 0,
          "a mapping file merges onto a six-state map: widened to the session's states, the row added");
    check(rep3.unknown_keys_n == 1 && std::string(rep3.unknown_keys[0].c_str()).find("nosuch") != std::string::npos,
          "…a state nobody registered is reported by name, and the rest still lands");
    rolltui_theme_report_release(&rep3);
    // A MERGED ROW REPLACES the map's for that state. A mapping file says what a state looks
    // like, and two answers would draw both, one under the other — which is what happened the
    // day the picker's states became the library's and every shipped theme mapped them: the
    // theme's shimmer drew under the app's glow. A state the file does not name keeps its row.
    RolltuiThemeReport rep4{};
    const char* app_file2 = R"({ "effects": { "dirk.folder": { "kind": "pulse", "role": "accent_1" } } })";
    check(rolltui_theme_effects_merge(m, app_file2, std::strlen(app_file2), rolltui_theme_vocab(a), &rep4) == 1 &&
              rolltui_effect_map_count(m, static_cast<std::size_t>(folder)) == 1 &&
              std::string(rolltui_effect_map_at(m, static_cast<std::size_t>(folder), 0)->kind,
                          rolltui_effect_map_at(m, static_cast<std::size_t>(folder), 0)->kind_len) == "pulse",
          "…and merged onto a map that already has the state, the file's row REPLACES it rather than stacking under it");
    rolltui_theme_report_release(&rep4);
    RolltuiThemeReport rep5{};
    check(rolltui_theme_effects_merge(m, "{ not json", 10, rolltui_theme_vocab(a), &rep5) == 0 && rep5.error.n != 0,
          "…and an unparseable file is refused with the parse error, never half-applied");
    rolltui_theme_report_release(&rep5);
    rolltui_effect_map_free(m);
    rolltui_effect_map_free(m2);
    rolltui_context_free(b);
    rolltui_context_free(a);
  }

  // ---- THE TWO PROPERTIES, over every registered kind -------------------------------
  {
    int refused_total = 0, kinds = 0;
    for (const KindCase& kc : kind_cases()) {
      ++kinds;
      // Span geometries, degenerate ones included: one cell; a span over the wide glyph;
      // a span ENDING on the wide glyph's first half; a span running off the right edge;
      // a span on a row that does not exist; a negative x.
      struct Span { int x, y, cells; };
      const Span spans[] = {{2, 1, 1}, {2, 1, 8}, {0, 1, 20}, {3, 1, 2}, {4, 1, 1}, {14, 1, 40}, {2, 99, 5}, {-3, 1, 6}, {2, 1, 0}};
      for (const Span& sp : spans) {
        for (std::uint64_t tick : {0ull, 137ull, 400ull, 999ull}) {
          for (double frac : {0.0, 0.37, 1.0}) {
            const Theme& theme = kc.theme;
            FramePtr f = make_frame(20, 3, theme);
            const std::vector<CellShot> before = shoot(f.get());
            rolltui_frame_mark(f.get(), sp.x, sp.y, sp.cells, static_cast<int>(EffectState::Waiting), 0, frac);
            const EffectReport rep = apply_effects(f.get(), theme, tick);
            refused_total += rep.glyphs_refused;
            const std::vector<CellShot> after = shoot(f.get());
            const std::string where = kc.kind + " span(" + std::to_string(sp.x) + "," + std::to_string(sp.y) + "," +
                                      std::to_string(sp.cells) + ") t=" + std::to_string(tick);
            check_quiet(before.size() == after.size(), where + ": the grid keeps its size");
            for (int y = 0; y < rolltui_frame_height(f.get()); ++y)
              for (int x = 0; x < rolltui_frame_width(f.get()); ++x) {
                const std::size_t i = static_cast<std::size_t>(y * rolltui_frame_width(f.get()) + x);
                const bool inside = y == sp.y && x >= sp.x && x < sp.x + sp.cells;
                if (!inside) {
                  // PROPERTY 2: nothing outside the span changed, at all.
                  check_quiet(before[i] == after[i],
                              where + ": cell (" + std::to_string(x) + "," + std::to_string(y) + ") outside the span is untouched");
                  continue;
                }
                // PROPERTY 1: the cell's width and its glyph's width are what they were.
                check_quiet(before[i].width == after[i].width && before[i].continuation == after[i].continuation,
                            where + ": cell (" + std::to_string(x) + "," + std::to_string(y) + ") keeps its width");
                check_quiet(after[i].continuation || unicode::display_width(after[i].text) == after[i].width,
                            where + ": cell (" + std::to_string(x) + "," + std::to_string(y) + ")'s glyph fills exactly its cells");
              }
            // Every row still measures the frame's width — the property wrap depends on.
            for (int y = 0; y < rolltui_frame_height(f.get()); ++y) {
              int cells = 0;
              for (int x = 0; x < rolltui_frame_width(f.get()); ++x) cells += cell_at(f.get(), x, y).width;  // a continuation cell is 0
              check_quiet(cells == rolltui_frame_width(f.get()),
                          where + ": row " + std::to_string(y) + " still measures " + std::to_string(rolltui_frame_width(f.get())));
            }
          }
        }
      }
    }
    check(kinds == 9, "the sweep ran over every registered kind, built-in and host's (" + std::to_string(kinds) + ")");
    check(refused_total > 0, "…and the lying kind's overrides were REFUSED and counted (" + std::to_string(refused_total) + ")");
  }

  // ---- the lying kind changes no glyph at all --------------------------------------
  {
    const Theme theme = theme_with(EffectState::Waiting, "wide-liar");
    FramePtr f = make_frame(20, 3, theme);
    const std::string before = frame_to_text(f.get());
    rolltui_frame_mark(f.get(), 2, 1, 8, static_cast<int>(EffectState::Waiting), 0, 0);
    const EffectReport rep = apply_effects(f.get(), theme, 250);
    check(frame_to_text(f.get()) == before, "a kind that lies about width writes no glyph anywhere");
    check(rep.glyphs_refused > 0 && !rep.clean(), "…and the report says so rather than the frame looking fine");
    check(rep.cells_touched > 0, "…while its STYLE still landed: only the illegal half was dropped");
  }

  // ---- determinism, and that the effect does something ------------------------------
  {
    const Theme theme = *builtin_theme("default-dark");
    auto at = [&](std::uint64_t tick) {
      FramePtr f = make_frame(20, 3, theme);
      rolltui_frame_mark(f.get(), 2, 1, 8, static_cast<int>(EffectState::Waiting), 0, 0);
      apply_effects(f.get(), theme, tick);
      return frame_to_text(f.get());
    };
    check(at(0) == at(0), "the same tick gives the same frame, byte for byte (what --tick N records)");
    check(at(0) != at(160), "…and a different tick a different one: the effect is actually drawn");
    check(at(0) == at(640), "…and one full period later, the same one again");
  }

  // ---- a span carries its OWN phase (Mark::since_ms) ---------------------------------
  {
    const Theme theme = *builtin_theme("default-dark");
    FramePtr f = make_frame(20, 3, theme);
    rolltui_frame_mark(f.get(), 2, 1, 1, static_cast<int>(EffectState::Waiting), 0, 0);     // started at 0
    rolltui_frame_mark(f.get(), 6, 1, 1, static_cast<int>(EffectState::Waiting), 1000, 0);  // started later: a different frame of the cycle
    apply_effects(f.get(), theme, 1160);
    check(glyph_at(f.get(), 2, 1) != glyph_at(f.get(), 6, 1),
          "two spans of one state with different start times are at different points of the cycle");
    FramePtr g = make_frame(20, 3, theme);
    rolltui_frame_mark(g.get(), 2, 1, 1, static_cast<int>(EffectState::Waiting), 0, 0);
    rolltui_frame_mark(g.get(), 6, 1, 1, static_cast<int>(EffectState::Waiting), 0, 0);
    apply_effects(g.get(), theme, 1160);
    check(glyph_at(g.get(), 2, 1) == glyph_at(g.get(), 6, 1), "…and two with no start time of their own move together off the shared clock");
  }

  // ---- STACKING: glyph from one kind, colour from another ---------------------------
  {
    Theme theme = *builtin_theme("default-dark");
    theme.effects.clear();
    const std::size_t spin = theme.effects.add(EffectState::Waiting, "spinner", 400);
    theme.effects.add_frame(EffectState::Waiting, spin, "x");
    theme.effects.add_frame(EffectState::Waiting, spin, "y");
    const std::size_t tint = theme.effects.add(EffectState::Waiting, "pulse", 0);
    theme.effects.add_role(EffectState::Waiting, tint, Role::error);
    FramePtr f = make_frame(20, 3, theme);
    rolltui_frame_mark(f.get(), 2, 1, 4, static_cast<int>(EffectState::Waiting), 0, 0);
    apply_effects(f.get(), theme, 0);
    check(glyph_at(f.get(), 2, 1) == "x" && cell_at(f.get(), 2, 1).style == theme.style(Role::error),
          "a stacked pair gives the glyph from one and the style from the other");
    check(cell_at(f.get(), 3, 1).style == theme.style(Role::error) && glyph_at(f.get(), 3, 1) != "x",
          "…and the kind that answers for one cell does not answer for the rest");
  }

  // ---- a theme that maps nothing is a STILL UI (the degrade rung) --------------------
  {
    Theme theme = *builtin_theme("default-dark");
    theme.effects = EffectMap{};
    FramePtr f = make_frame(20, 3, theme);
    const std::vector<CellShot> before = shoot(f.get());
    rolltui_frame_mark(f.get(), 2, 1, 8, static_cast<int>(EffectState::Waiting), 0, 0);
    const EffectReport rep = apply_effects(f.get(), theme, 500);
    check(shoot(f.get()) == before && rep.marks_drawn == 0 && rep.clean(), "a theme that maps nothing leaves a marked frame untouched");
    check(!effect_tick_ms(f.get(), theme).has_value(), "…and asks for no wakeup");
  }

  // ---- THE TICK RULE: it runs only while something is marked ------------------------
  {
    for (std::string_view name : builtin_theme_names()) {
      const Theme& theme = *builtin_theme(name);
      FramePtr f = make_frame(20, 3, theme);
      check(!effect_tick_ms(f.get(), theme).has_value(), std::string("no wakeups with no marks — ") + std::string(name));
      check(poll_timeout_ms(f.get(), theme, 1000) == 1000, std::string("…so a host's idle timeout is untouched — ") + std::string(name));
      rolltui_frame_mark(f.get(), 2, 1, 8, static_cast<int>(EffectState::Waiting), 0, 0);
      const std::optional<int> tick = effect_tick_ms(f.get(), theme);
      check(tick && *tick >= 16, std::string("a marked waiting span asks for a tick — ") + std::string(name) + " " +
                                     (tick ? std::to_string(*tick) : "none"));
      check(poll_timeout_ms(f.get(), theme, 1000) == *tick, "…and the host's poll timeout becomes it");
      check(poll_timeout_ms(f.get(), theme, 10) == 10, "…but never LONGER than what the host already wanted");
    }
    // A state the theme maps to a STILL effect asks for nothing either: a bar is a
    // picture of a number, and the number changing is already a redraw.
    const Theme& dark = *builtin_theme("default-dark");
    FramePtr f = make_frame(20, 3, dark);
    rolltui_frame_mark(f.get(), 6, 1, 8, static_cast<int>(EffectState::Progress), 0, 0.5);  // eight NARROW cells: the arithmetic is the point here, not the wide glyph
    check(!effect_tick_ms(f.get(), dark).has_value(), "a still effect (period_ms 0) asks for no wakeup though its span IS marked");
    const EffectReport rep = apply_effects(f.get(), dark, 0);
    check(rep.marks_drawn == 1 && rep.cells_touched == 4, "…while still drawing: 0.5 of an 8-cell span is 4 cells");
    // A mark whose state the theme maps to nothing that RESOLVES: named, never silent.
    Theme t2 = dark;
    t2.effects.clear();
    t2.effects.add(EffectState::Flash, "confetti", 100);
    FramePtr g = make_frame(20, 3, t2);
    rolltui_frame_mark(g.get(), 2, 1, 4, static_cast<int>(EffectState::Flash), 0, 0);
    const EffectReport grep = apply_effects(g.get(), t2, 0);
    check(grep.unknown_kinds.size() == 1 && grep.unknown_kinds[0] == "confetti", "an unknown kind is NAMED in the report, not silently still");
    check(!effect_tick_ms(g.get(), t2).has_value(), "…and asks for no wakeup, since it cannot draw");
  }

  // ---- the file format --------------------------------------------------------------
  {
    const char* text = R"({
      "name": "fx", "roles": { "text": { "fg": "none" } },
      "effects": {
        "waiting": { "kind": "spinner", "frames": ["-", "\\"], "period_ms": 200 },
        "streaming": [ { "kind": "shimmer", "role": "accent_1", "width": 4 },
                       { "kind": "ellipsis", "frames": ["   ", "...  "] } ],
        "progress": { "kind": "bar", "roles": ["accent_2"], "period_ms": 0, "backward": true },
        "elsewhere": { "kind": "spinner" },
        "flash": { "kind": "blink", "role": "nosuchrole", "wobble": 3 }
      }
    })";
    ThemeLoadReport rep;
    std::optional<Theme> t = load_theme(text, ThemeMode::Dark, rep);
    check(t.has_value(), "a theme file with effects loads");
    check(t->effects.count(EffectState::Waiting) == 1 && t->effects.count(EffectState::Streaming) == 2,
          "one spec or an array of them, and an array STACKS");
    check(t->effects.at(EffectState::Waiting, 0).frame_count == 2 && t->effects.at(EffectState::Waiting, 0).period_ms == 200,
          "the spec's fields are read");
    check(t->effects.at(EffectState::Streaming, 0).own_role_count == 1 &&
              t->effects.at(EffectState::Streaming, 0).roles[0] == static_cast<unsigned char>(Role::accent_1),
          "\"role\" and \"roles\" are the same field");
    check(t->effects.at(EffectState::Progress, 0).backward && t->effects.at(EffectState::Progress, 0).period_ms == 0,
          "a still effect is written as period_ms 0, not as a missing key");
    auto has = [](const std::vector<std::string>& v, const char* needle) {
      for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
      return false;
    };
    check(has(rep.unknown_keys, "effects.elsewhere"), "an unknown STATE name is reported");
    check(has(rep.unknown_keys, "effects.flash.wobble"), "an unknown spec key is reported");
    check(has(rep.bad_values, "nosuchrole"), "a role name that is not a role is a named bad value");
    check(has(rep.bad_values, "every frame must be 3 cells wide"), "frames of unequal width are refused AT LOAD, where an author can fix them");
    // An unknown KIND is not judged here: rung 2 is the host's, and a theme file is read
    // long before a host has registered anything (the same rule as an unknown widget kind).
    check(!has(rep.bad_values, "spinner") && !has(rep.unknown_keys, "effects.elsewhere.kind"), "…but a kind NAME is never judged by the loader");
  }

  // ---- the round trip -----------------------------------------------------------------
  {
    for (std::string_view name : builtin_theme_names()) {
      const Theme& theme = *builtin_theme(name);
      ThemeLoadReport rep;
      std::optional<Theme> back = load_theme(theme_to_json(theme), ThemeMode::Dark, rep);
      check(back && rep.clean() && back->effects == theme.effects,
            std::string("the built-in '") + std::string(name) + "' round-trips its effects through a theme file");
    }
    // A colour edit through the editor rewrites the file from the parsed theme: the pair
    // writer must carry the motion across or a first edit would silently stop it.
    json::Value pair = theme_pair_to_json_value(*builtin_theme("default-dark"), *builtin_theme("default-light"), "x");
    ThemeLoadReport rep;
    std::optional<Theme> d = load_theme(pair, ThemeMode::Dark, rep), l = load_theme(pair, ThemeMode::Light, rep);
    check(d && l && d->effects == builtin_theme("default-dark")->effects && d->effects == l->effects,
          "a dark/light PAIR file carries one effects object for both variants");
    Theme still = *builtin_theme("default-dark");
    still.effects.clear();
    std::string err;
    check(!json::parse(theme_to_json(still), err).has("effects") && err.empty(),
          "a theme with no motion writes no \"effects\" key: absent and empty are the same answer here");
  }

  // ---- jitter: a sweep that varies its speed and still lands on the same frame ---------
  // The ask was "a little more random in speed". The constraint is that an effect stays a PURE
  // function of its inputs, because that is what makes `--tick N` a golden frame — so the
  // variation is derived from the pass number, not from a generator. These four assertions are
  // what separate those two things.
  {
    const Theme& base = *builtin_theme("default-dark");
    const std::size_t n = base.effects.count(EffectState::Streamed);
    check(n != 0, "default-dark maps `streamed`, the state a span enters when its tokens stop");

    check(base.effects.at(EffectState::Streamed, 0).jitter > 0,
          "…and the theme FILE's jitter survived parsing (got " +
              std::to_string(base.effects.at(EffectState::Streamed, 0).jitter) + ")");

    // A span wide enough that a shifted sweep lands on different cells.
    const auto sweep_at = [](const Theme& th, EffectState st, std::uint64_t tick) {
      FramePtr plain_f = make_frame(40, 3, th);
      const std::vector<CellShot> plain = shoot(plain_f.get());
      FramePtr f = make_frame(40, 3, th);
      rolltui_frame_mark(f.get(), 0, 1, 32, static_cast<int>(st), 0, 0.0);
      apply_effects(f.get(), th, tick);
      const std::vector<CellShot> after = shoot(f.get());
      std::string row;
      for (int x = 0; x < 32; ++x) {
        const std::size_t i = static_cast<std::size_t>(1 * 40 + x);
        row += (after[i].style == plain[i].style) ? '.' : '#';
      }
      return row;
    };

    // 1. DETERMINISM. The same tick draws the same cells, every time — no hidden state.
    check(sweep_at(base, EffectState::Streamed, 700) == sweep_at(base, EffectState::Streamed, 700) &&
              sweep_at(base, EffectState::Streamed, 5300) == sweep_at(base, EffectState::Streamed, 5300),
          "a jittered sweep is DETERMINISTIC: the same elapsed always draws the same cells");

    // 2. IT ACTUALLY VARIES. Same offset into the period, different passes — at least one pair
    //    must differ, or `jitter` is a field that does nothing.
    const int period = base.effects.at(EffectState::Streamed, 0).period_ms;
    std::set<std::string> at_same_offset;
    for (int pass = 0; pass < 8; ++pass)
      at_same_offset.insert(sweep_at(base, EffectState::Streamed,
                                     static_cast<std::uint64_t>(pass) * period + period / 3));
    check(at_same_offset.size() > 1,
          "…and it VARIES from pass to pass: the same offset into the period draws " +
              std::to_string(at_same_offset.size()) + " different sweeps across 8 passes");

    // 3. THE CONTROL. With jitter off, that same measurement must collapse to one — otherwise
    //    assertion 2 is measuring something else entirely.
    Theme still = base;
    for (std::size_t k = 0; k < still.effects.count(EffectState::Streamed); ++k)
      still.effects.set_jitter(EffectState::Streamed, k, 0);
    std::set<std::string> unjittered;
    for (int pass = 0; pass < 8; ++pass)
      unjittered.insert(sweep_at(still, EffectState::Streamed,
                                 static_cast<std::uint64_t>(pass) * period + period / 3));
    check(unjittered.size() == 1,
          "…and with jitter 0 the same measurement collapses to one sweep, so it is the jitter being seen");

    // 4. PASS BOUNDARIES ARE EXACT. The warp is zero at both ends, so a pass still starts where
    //    an unjittered one would — the wobble changes the journey, never the arrival.
    check(sweep_at(base, EffectState::Streamed, 0) == sweep_at(still, EffectState::Streamed, 0),
          "…and a pass BEGINS where an unjittered pass would: the warp is zero at the ends");
  }

  // ---- a popup covers the marks under it: the frame's half ------------------------------
  // `rolltui_frame_unmark_rect` is what the window stack calls for each popup's box before the
  // popup draws, so the effects applier — which runs over the FINISHED frame — never paints a
  // lower layer's motion onto a window drawn over it.
  {
    FramePtr f = new_frame(40, 6);
    auto mark = [&](int x, int y, int cells) { rolltui_frame_mark(f.get(), x, y, cells, static_cast<int>(EffectState::Waiting), 0, 0); };
    mark(2, 0, 5);    // above the box: untouched
    mark(12, 1, 6);   // wholly inside: gone
    mark(6, 2, 10);   // crosses the left edge: keeps 6..9
    mark(16, 3, 10);  // crosses the right edge: keeps 20..25
    mark(0, 4, 40);   // spans the whole row: two pieces
    rolltui_frame_unmark_rect(f.get(), RolltuiRect{10, 1, 10, 4});  // x 10..19, y 1..4
    struct M { int x, y, cells; };
    std::vector<M> got;
    for (std::size_t i = 0; i < rolltui_frame_mark_count(f.get()); ++i) {
      int x = 0, y = 0, cells = 0, state = 0; unsigned long long since = 0; double fr = 0;
      rolltui_frame_mark_at(f.get(), i, &x, &y, &cells, &state, &since, &fr);
      got.push_back({x, y, cells});
    }
    auto is = [&](std::size_t i, int x, int y, int cells) { return i < got.size() && got[i].x == x && got[i].y == y && got[i].cells == cells; };
    check(got.size() == 5, "five marks survive a box over five: the covered one is gone, the straddler is two (" + std::to_string(got.size()) + ")");
    check(is(0, 2, 0, 5), "a mark outside the box is untouched");
    check(is(1, 6, 2, 4), "a mark the box cuts on the right keeps its left part [" + (got.size() > 1 ? std::to_string(got[1].x) + "+" + std::to_string(got[1].cells) : std::string("-")) + "]");
    check(is(2, 20, 3, 6), "a mark the box cuts on the left keeps its right part");
    check(is(3, 0, 4, 10) && is(4, 20, 4, 20), "a mark the box takes the middle of is two marks, the order of the rest kept");
    // the control: a rect that touches nothing changes nothing
    rolltui_frame_unmark_rect(f.get(), RolltuiRect{30, 0, 5, 1});
    check(rolltui_frame_mark_count(f.get()) == 5, "…and a rect over no mark removes none");
  }

  // ---- shade: a tint at a strength keeps what the cells said about each other ----------
  // A modal's overlay REPLACING every foreground erases a gradient drawn under it — a fade
  // becomes one flat colour and reads as gone rather than as dimmed. Moving each colour PART of
  // the way keeps the gradient a gradient, quieter.
  {
    FramePtr f = new_frame(8, 1);
    RolltuiStyle over{};
    over.fg = RolltuiStyleColor::rgb(0, 0, 0);
    for (int x = 0; x < 8; ++x) {
      RolltuiStyle st{};
      st.fg = RolltuiStyleColor::rgb(static_cast<unsigned char>(x * 30), 100, 200);
      rolltui_frame_set_style(f.get(), x, 0, st);
    }
    rolltui_frame_shade(f.get(), RolltuiRect{0, 0, 8, 1}, over, 0.5);
    RolltuiCell a{}, b{};
    rolltui_frame_cell(f.get(), 1, 0, &a);
    rolltui_frame_cell(f.get(), 7, 0, &b);
    check(a.style.fg.kind == RolltuiStyleColor::Kind::Rgb && a.style.fg.r == 15 && a.style.fg.g == 50 && a.style.fg.b == 100,
          "at half strength an RGB colour moves half way toward the overlay's [" + std::to_string(a.style.fg.r) + "," + std::to_string(a.style.fg.g) + "," + std::to_string(a.style.fg.b) + "]");
    check(b.style.fg.r == 105 && a.style.fg.r != b.style.fg.r, "…and two cells that differed still differ: the gradient survives, halved");
    rolltui_frame_shade(f.get(), RolltuiRect{0, 0, 8, 1}, over, 1.0);
    rolltui_frame_cell(f.get(), 7, 0, &b);
    check(b.style.fg.r == 0 && b.style.fg.g == 0 && b.style.fg.b == 0, "at full strength shade IS tint: the overlay's colour replaces");
    // no RGB on one side: nothing to move along, so a strong shade replaces and a weak one dims
    RolltuiStyle idx{};
    idx.fg = RolltuiStyleColor::indexed(3);
    rolltui_frame_set_style(f.get(), 0, 0, idx);
    rolltui_frame_shade(f.get(), RolltuiRect{0, 0, 1, 1}, over, 0.3);
    rolltui_frame_cell(f.get(), 0, 0, &a);
    check(a.style.fg.kind == RolltuiStyleColor::Kind::Indexed && a.style.fg.index == 3 && a.style.dim == 1,
          "an indexed colour under a weak shade keeps its colour and takes the DIM attribute");
    rolltui_frame_shade(f.get(), RolltuiRect{0, 0, 1, 1}, over, 0.7);
    rolltui_frame_cell(f.get(), 0, 0, &a);
    check(a.style.fg.kind == RolltuiStyleColor::Kind::Rgb && a.style.fg.r == 0, "…and under a strong one is replaced, as a tint would");
  }

  // ---- the built-ins ------------------------------------------------------------------
  {
    const Theme& dark = *builtin_theme("default-dark");
    const Theme& light = *builtin_theme("default-light");
    const Theme& mono = *builtin_theme("mono");
    check(dark.effects == light.effects, "motion is a property of the THEME, not of dark vs light");
    // MONO IS THE ONE THEME WITHOUT `jitter`, and that is chosen rather than missed. It pulses
    // instead of shimmering because it has no colour to sweep, and a steady pulse is what the
    // plainest theme should do — a wobble there is decoration on a theme whose point is having
    // none. Do not "finish" it by adding jitter to match the other eleven.
    check(!(dark.effects == mono.effects), "…and the mono theme tells the same states a different way");
    // EVERY SHIPPED THEME MAPS EVERY STATE, not just the three built-ins. The narrower check is
    // what let `streamed` ship half-done: it was added to `default` and `mono`, the built-ins the
    // loop below covers, and the eight other shipped themes silently had no mapping — so anyone
    // on `catppuccin` or `ink` got no effect at all and nothing failed. A state is added to the
    // vocabulary once and every theme owes it an answer.
    {
      namespace fs = std::filesystem;
      std::vector<std::string> missing;
      std::size_t scanned = 0;
      for (const fs::directory_entry& de : fs::directory_iterator(ROLLTUI_PRESETS_DIR)) {
        if (de.path().extension() != ".json") continue;
        ++scanned;
        std::ifstream in(de.path());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (std::size_t i = 1; i < kEffectStateCount; ++i) {
          const std::string name = std::string("\"") + std::string(effect_state_name(static_cast<EffectState>(i))) + "\"";
          if (text.find(name) == std::string::npos)
            missing.push_back(de.path().stem().string() + " is missing " + name);
        }
      }
      check(scanned >= 8, "scanned the shipped themes (" + std::to_string(scanned) + ")");
      check(missing.empty(), "every SHIPPED theme maps every effect state" +
                                 (missing.empty() ? std::string() : " — " + missing.front() +
                                                        " (and " + std::to_string(missing.size() - 1) + " more)"));
    }

    for (const Theme* t : {&dark, &light, &mono})
      for (std::size_t i = 1; i < kEffectStateCount; ++i) {
        const EffectState state = static_cast<EffectState>(i);
        const std::size_t n = t->effects.count(state);
        check_quiet(n != 0, t->name + " maps " + std::string(effect_state_name(state)));
        for (std::size_t k = 0; k < n; ++k) {
          const RolltuiEffectSpec& s = t->effects.at(state, k);
          const std::string kind(kind_of(s));
          check_quiet(effect_kind_resolves(kind), t->name + ": kind '" + kind + "' resolves");
          if (s.frame_count == 0) continue;
          const int w = unicode::display_width(frame_of(s, 0));
          for (std::size_t fi = 0; fi < s.frame_count; ++fi) {
            const std::string_view fr = frame_of(s, fi);
            check_quiet(unicode::display_width(fr) == w, t->name + ": every frame of '" + kind + "' is " + std::to_string(w) + " cells");
            // …at BOTH ambiguous-width settings, or the applier would refuse the glyph on
            // a wide-ambiguous terminal and the theme would silently stop moving.
            check_quiet(unicode::display_width(fr, true) == w, t->name + ": '" + kind + "' frame is " + std::to_string(w) + " cells when ambiguous is wide too");
          }
        }
      }
    check(true, "every shipped effect's frames are one width at both ambiguous-width settings");
  }

  clear_registered_effect_kinds();
  return report("effects");
}
