// rolltui/Theme.cpp — THE C++ SHIM (Phase 15 m5). See Theme.hpp for the contract; the built-in
// themes, the colour engine and the JSON loader/dumper all live in `rolltui/c/rolltui_theme.h`
// (Phase 15 m3 moved the colour engine; m5, this pass, moves the rest — the built-in themes and
// the JSON loader/dumper, which m3's own note already named as "most of the file" and never
// moved). What is LEFT here is exactly what `rolltui/c/rolltui_theme.h`'s header comment says
// deliberately stays: the depth/mode NAME vocabulary, and the two things `Theme::meta`
// (`json::Value`) and `Theme::styles`/`Theme::effects`/`Theme::name`'s C++ SHAPES that no
// caller of this header may see change (rolltui/c/rolltui_json.h's own header comment is why
// `json::Value` itself does not port here — Theme.cpp is one of the six modules it names).
//
// rolltui_theme.h's loader/dumper never learn a Role's or an EffectState's NAME — a theme file
// resolves "md_heading"/"waiting" against a table THIS file hands over once per call
// (`RolltuiThemeVocab`), built from `Style.hpp`'s `kRoleNames` and `Effects.cpp`'s
// `effect_state_name` and never rebuilt (a Meyer's singleton of plain pointers into
// literal-backed storage — nothing here for `rolltui::shutdown()` to release).
#include "rolltui/Theme.hpp"

#include <cstring>

#include "rolltui/Json.hpp"
#include "rolltui/Lifetime.hpp"
#include "rolltui/c/rolltui_theme.h"

namespace rolltui {

namespace {

// ---- the vocabulary, handed to the C side once per call --------------------------------

// Every pointer here borrows LITERAL-backed storage — `kRoleNames`' entries are
// `std::string_view`s over string literals (Style.hpp), and `effect_state_name`'s are the
// same over `Effects.cpp`'s own `kStateNames` — so each is NUL-terminated (a string literal
// always is) even though `string_view` does not generally promise that, and each lives for
// the process's whole life. That is what lets `RolltuiThemeVocab` skip a parallel length
// array (rolltui_theme.h's own comment) and what makes a `std::array` of pointers, not a
// heap allocation, the whole of this singleton's storage — there is nothing for
// `rolltui::shutdown()` to release.
struct VocabTables {
  std::array<const char*, kRoleCount> role_names{};
  std::array<const char*, kEffectStateCount> state_names{};
  RolltuiThemeVocab vocab{};
  VocabTables() {
    for (std::size_t i = 0; i < kRoleCount; ++i) role_names[i] = kRoleNames[i].data();
    for (std::size_t i = 0; i < kEffectStateCount; ++i)
      state_names[i] = effect_state_name(static_cast<EffectState>(i)).data();
    vocab.role_names = role_names.data();
    vocab.role_count = kRoleCount;
    vocab.text_role = static_cast<std::size_t>(Role::text);
    vocab.state_names = state_names.data();
    vocab.state_count = kEffectStateCount;
    vocab.fallback_effect_role = static_cast<unsigned char>(Role::accent_1);
  }
};

const RolltuiThemeVocab& vocab() {
  static const VocabTables t;
  return t.vocab;
}

}  // namespace

// ---- json::Value <-> RolltuiJsonValue*, SHARED with Json.cpp -----------------------------
//
// **This was a duplicated copy for the length of one parallel work session, and the reason it
// is not one now is worth the four lines.** `Json.cpp` has always had this exact conversion;
// it was file-local, so the agent porting this module copied it rather than reach across into
// another module a sibling agent was editing at the same moment — a correct call about
// collision risk, and it left the library with two implementations of one thing. The sibling
// (`Layout.cpp`'s port) needed the same conversion and gave `Json.cpp`'s the external linkage
// instead. Deduplicated here on integration, the third duplicate this port has found after
// `put_text`/`fill`/`tint` and `Windows::factories_`.
//
// Needed for exactly two things: handing "defs"/"roles"/"effects" to the C loader when the
// caller already holds a parsed `json::Value` (`Presets.cpp`'s embedded theme object —
// `load_theme`'s TEXT overload parses straight to a `RolltuiJsonValue*` and never needs
// `value_to_c` at all), and reading "meta" back out as a `json::Value`, which is that
// struct's own C++ shape (`Theme.hpp`) and stays so. Not declared in `Json.hpp`: that
// header's public shape stays exactly what the other C++ modules already see.
namespace json {
RolltuiJsonValue* value_to_c(const Value& v);
Value value_from_c(const RolltuiJsonValue* v);
}  // namespace json

// ---- built-ins ---------------------------------------------------------------------

namespace {

// THE BUILT-INS ARE A CACHE, NOT THREE `static const Theme`s (Phase 15 m3) — a `Theme` owns a
// `RolltuiEffectMap`, an explicit allocation through the library's own entry point, and three
// function-local statics holding one each would be three PROCESS-WIDE RETAINERS that
// `rolltui::shutdown()` promises to release (`live_bytes == 0`).
//
// FILLED WHEN EMPTY, not by a static initializer, for the reason `builtin_layout_cache()`
// states one file over: releasing a cache is only safe if the cache rebuilds. The storage and
// the FILL are separate on purpose. A releaser written as `builtin_theme_cache().clear();
// builtin_theme_cache().shrink_to_fit();` reads fine and is wrong: the second call finds the
// cache it just emptied and REBUILDS it, so shutdown ends holding exactly what it set out to
// release — which is why the releaser below touches `theme_cache_storage()`, the STORAGE,
// and never `builtin_theme_cache()`, the ACCESSOR that would refill it.
std::vector<std::pair<std::string, Theme>>& theme_cache_storage() {
  static std::vector<std::pair<std::string, Theme>> cache;
  return cache;
}

std::vector<std::pair<std::string, Theme>>& builtin_theme_cache() {
  std::vector<std::pair<std::string, Theme>>& cache = theme_cache_storage();
  if (cache.empty()) {
    // RE-REGISTERED ON EVERY REBUILD, deliberately, and not through a `static bool once`:
    // `shutdown()` clears its own registry as it runs, so a cache rebuilt afterwards must
    // say so again or the SECOND shutdown would leave it held.
    on_shutdown([] {
      theme_cache_storage().clear();
      theme_cache_storage().shrink_to_fit();
    });
    const std::size_t n = rolltui_theme_builtin_count();
    cache.reserve(n);  // pointer stability: builtin_theme() hands back `&t` into this vector
    for (std::size_t i = 0; i < n; ++i) {
      const char* name = rolltui_theme_builtin_name(i);
      Theme t;
      t.name = name;
      if (RolltuiEffectMap* m = rolltui_theme_builtin_fill(name, std::strlen(name), t.styles.data(), kRoleCount))
        t.effects = EffectMap(m);  // adopts; NULL only on a role-count mismatch this file
                                   // would already have failed a static_assert over
      cache.emplace_back(t.name, std::move(t));
    }
  }
  return cache;
}

}  // namespace

const Theme* builtin_theme(std::string_view name) {
  for (const auto& [n, t] : builtin_theme_cache())
    if (n == name) return &t;
  return nullptr;
}

std::vector<std::string_view> builtin_theme_names() {
  std::vector<std::string_view> out;
  const std::size_t n = rolltui_theme_builtin_count();
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) out.push_back(rolltui_theme_builtin_name(i));
  return out;
}

// ---- colours -----------------------------------------------------------------------

// THE COLOUR ENGINE IS BEHIND A C BOUNDARY (`rolltui/c/rolltui_theme.h`) since Phase 15 m3,
// and that is the implementation. What is left on this side is the two things the boundary
// deliberately does not carry: the C++ SHAPES a caller already writes against
// (`std::optional<Color>`, `std::string`), and the DEPTH and MODE NAMES below, which are a
// vocabulary a config file and a `--color-depth` flag both spell — a vocabulary written down
// twice is a second thing to drift, the same reasoning that kept `Role` out of
// `rolltui_diff.h` in m2.

std::optional<Color> parse_color(std::string_view text) {
  Color c;
  if (!rolltui_color_parse(text.data(), text.size(), &c)) return std::nullopt;
  return c;
}

std::string color_to_string(Color c) {
  // CALLER-FILLED, with the bound known WITHOUT asking: "#rrggbb" is the longest spelling
  // there is, so the header states it as a constant and there is no measure-then-fill.
  char buf[ROLLTUI_COLOR_STRING_MAX];
  const std::size_t n = rolltui_color_to_string(c, buf, sizeof buf);
  return std::string(buf, n);
}

Color ansi_index_rgb(std::uint8_t index) {
  Color c;
  rolltui_ansi_index_rgb(index, &c);
  return c;
}

Color downgrade(Color c, ColorDepth depth) {
  rolltui_color_downgrade(&c, static_cast<unsigned char>(depth));
  return c;
}

std::string sgr(const Style& style, ColorDepth depth) {
  char buf[ROLLTUI_SGR_MAX];
  const std::size_t n = rolltui_sgr(&style, static_cast<unsigned char>(depth), buf, sizeof buf);
  return std::string(buf, n);
}

ColorDepth detect_color_depth(const char* colorterm, const char* term, const char* force) {
  std::string_view f = force ? force : "";
  if (f == "truecolor" || f == "24bit") return ColorDepth::TrueColor;
  if (f == "256") return ColorDepth::Ansi256;
  if (f == "16") return ColorDepth::Ansi16;
  if (f == "mono") return ColorDepth::Mono;
  std::string_view ct = colorterm ? colorterm : "";
  std::string_view t = term ? term : "";
  if (ct == "truecolor" || ct == "24bit") return ColorDepth::TrueColor;
  if (t.find("256color") != std::string_view::npos) return ColorDepth::Ansi256;
  if (t.empty() || t == "dumb") return ColorDepth::Mono;
  return ColorDepth::Ansi16;
}

std::string_view color_depth_name(ColorDepth d) {
  switch (d) {
    case ColorDepth::Mono: return "mono";
    case ColorDepth::Ansi16: return "16";
    case ColorDepth::Ansi256: return "256";
    case ColorDepth::TrueColor: return "truecolor";
  }
  return "mono";
}

// ---- OSC 11 --------------------------------------------------------------------------------

std::optional<Color> parse_osc11_reply(std::string_view reply) {
  Color c;
  if (!rolltui_parse_osc11_reply(reply.data(), reply.size(), &c)) return std::nullopt;
  return c;
}

ThemeMode mode_for_background(Color bg) { return static_cast<ThemeMode>(rolltui_mode_for_background(bg)); }

// ---- the JSON loader ---------------------------------------------------------------

namespace {

// Fills `report` (fully reset first) from loading `root_c`, and returns the theme unless
// `root_c` itself is not a usable theme object. Shared by both `load_theme` overloads below:
// the text one parses straight to a `RolltuiJsonValue*` and never builds a `json::Value` at
// all; the `json::Value` one converts its argument once via `value_to_c`. Neither overload's
// "meta" handling happens in here — rolltui_theme.h's header comment states why that field
// stays outside this file entirely, and each caller does its own, right after this returns.
std::optional<Theme> theme_from_c_root(const RolltuiJsonValue* root_c, ThemeMode mode, ThemeLoadReport& report) {
  report = ThemeLoadReport{};
  Theme t;
  RolltuiStr name{};
  RolltuiThemeReport rep{};
  RolltuiEffectMap* eff = rolltui_theme_load(root_c, static_cast<int>(mode), &vocab(), t.styles.data(), &name, &rep);
  report.error.assign(rep.error.p ? rep.error.p : "", rep.error.n);
  for (std::size_t i = 0; i < rep.missing_roles_n; ++i)
    report.missing_roles.emplace_back(rep.missing_roles[i].p ? rep.missing_roles[i].p : "", rep.missing_roles[i].n);
  for (std::size_t i = 0; i < rep.unknown_keys_n; ++i)
    report.unknown_keys.emplace_back(rep.unknown_keys[i].p ? rep.unknown_keys[i].p : "", rep.unknown_keys[i].n);
  for (std::size_t i = 0; i < rep.bad_values_n; ++i)
    report.bad_values.emplace_back(rep.bad_values[i].p ? rep.bad_values[i].p : "", rep.bad_values[i].n);
  rolltui_theme_report_release(&rep);
  if (!eff) {
    rolltui_str_free(&name);
    return std::nullopt;
  }
  t.name.assign(name.p ? name.p : "", name.n);
  rolltui_str_free(&name);
  t.effects = EffectMap(eff);  // adopts
  return t;
}

}  // namespace

std::optional<Theme> load_theme(std::string_view json_text, ThemeMode mode, ThemeLoadReport& report) {
  RolltuiStr err{};
  RolltuiJsonValue* root_c = rolltui_json_parse(json_text.data(), json_text.size(), &err);
  if (!root_c) {
    report = ThemeLoadReport{};
    report.error.assign(err.p ? err.p : "", err.n);
    rolltui_str_free(&err);
    return std::nullopt;
  }
  rolltui_str_free(&err);
  std::optional<Theme> t = theme_from_c_root(root_c, mode, report);
  if (t) {
    const RolltuiJsonValue* meta_c = rolltui_json_get(root_c, "meta", 4);
    if (rolltui_json_is_object(meta_c)) {
      t->meta = json::value_from_c(meta_c);
      const json::Value& b = t->meta.get("badges");
      const char* key = mode == ThemeMode::Dark ? "dark" : "light";
      if (b.is_object() && b.has(key)) t->meta.set("badges", b.get(key));
    }
  }
  rolltui_json_free(root_c);
  return t;
}

std::optional<Theme> load_theme(const json::Value& root, ThemeMode mode, ThemeLoadReport& report) {
  RolltuiJsonValue* root_c = json::value_to_c(root);
  std::optional<Theme> t = theme_from_c_root(root_c, mode, report);
  rolltui_json_free(root_c);
  if (t && root.get("meta").is_object()) {
    // Claimed badges may be per variant ({"dark": [...], "light": [...]}): resolve them for
    // this mode like a colour pair, so check_claims sees one list.
    t->meta = root.get("meta");
    const json::Value& b = t->meta.get("badges");
    const char* key = mode == ThemeMode::Dark ? "dark" : "light";
    if (b.is_object() && b.has(key)) t->meta.set("badges", b.get(key));
  }
  return t;
}

json::Value theme_to_json_value(const Theme& theme) {
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(theme.name));
  if (theme.meta.is_object()) root.set("meta", theme.meta);
  RolltuiJsonValue* c = rolltui_theme_dump(theme.styles.data(), theme.effects.handle(), nullptr, nullptr, &vocab());
  root.set("roles", json::value_from_c(rolltui_json_get(c, "roles", 5)));
  const RolltuiJsonValue* fx = rolltui_json_get(c, "effects", 7);
  if (!rolltui_json_is_null(fx)) root.set("effects", json::value_from_c(fx));
  rolltui_json_free(c);
  return root;
}

std::string theme_to_json(const Theme& theme) { return json::dump(theme_to_json_value(theme), 2) + "\n"; }

json::Value theme_pair_to_json_value(const Theme& dark, const Theme& light, std::string_view name) {
  // Colours AND attributes are written as {"dark","light"} pairs wherever the two variants
  // differ, so the round trip is exact for both (asserted in rolltui-theme-editor-test with
  // an attribute set in one variant only).
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(std::string(name)));
  if (dark.meta.is_object()) {
    json::Value meta = dark.meta;
    // Each variant's claimed badges, as a pair when they differ.
    if (light.meta.is_object() && !(dark.meta.get("badges") == light.meta.get("badges"))) {
      json::Value pair = json::Value::object();
      pair.set("dark", dark.meta.get("badges"));
      pair.set("light", light.meta.get("badges"));
      meta.set("badges", std::move(pair));
    }
    root.set("meta", std::move(meta));
  }
  // ONE effects object for both variants: motion is a property of the theme, not of the
  // terminal's background (Theme.hpp) — `rolltui_theme_dump` takes `light_effects` only to
  // document that it is never consulted, and always dumps `dark.effects` alone.
  RolltuiJsonValue* c =
      rolltui_theme_dump(dark.styles.data(), dark.effects.handle(), light.styles.data(), light.effects.handle(), &vocab());
  root.set("roles", json::value_from_c(rolltui_json_get(c, "roles", 5)));
  const RolltuiJsonValue* fx = rolltui_json_get(c, "effects", 7);
  if (!rolltui_json_is_null(fx)) root.set("effects", json::value_from_c(fx));
  rolltui_json_free(c);
  return root;
}

}  // namespace rolltui
