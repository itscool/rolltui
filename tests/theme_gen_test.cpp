//
// theme_gen_test.cpp — the seeded generator (milestone 15): determinism (same seed +
// ruleset + chaos → the same theme, twice, and a different seed → a different one),
// every ruleset at chaos 0 passes `readable` for a run of seeds, chaos 1 breaks a rule
// for some seed (the chaos is real), the meta records the inputs and the computed
// badges, the file round-trips with its meta, and check_claims holds on a generated
// theme's own claims.
//
// PHASE 17 m2: calls the C API (rolltui/c/rolltui_theme_gen.h, rolltui_theme_analysis.h,
// rolltui_theme.h, rolltui_json.h, all reached through rolltui/rolltui.h) directly —
// ThemeGen.hpp, ThemeAnalysis.hpp, Theme.hpp and Style.hpp are all deleted along with the
// rest of the C++ binding (plan/phase-17.md milestone 2). `Rng` is `RolltuiRng` by alias,
// same as before (ThemeGen.hpp already aliased it, one definition compiled by both
// languages). `Role` (Style.hpp) and `Ruleset` (ThemeGen.hpp) have no C enum form — a C
// file takes the ORDINAL, never the name — so both are reproduced below exactly as
// rolltui/tests/theme_test.cpp reproduces `Role` independently; `generate()`,
// `load_theme()`, `theme_to_json()`, `analyse()`, `check_claims()` and `badge_names()`
// were thin forwarding shims over `rolltui_theme_generate` / `rolltui_theme_load` /
// `rolltui_theme_dump` / `rolltui_theme_analyse` / `rolltui_check_claims` /
// `rolltui_badge_names` and are reproduced the same way, over the same C calls. `analyse`
// here only ever needs the computed `Badges`, so its C++ shape below carries just that —
// this file never reads a per-role or per-pair check (rolltui/tests/theme_analysis_test.cpp
// is the oracle for those and reproduces the full report).
//
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_TESTS (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"  /* INTERNAL: this test opts in (Phase 19 m2) */

#include "rolltui_test.hpp"

using namespace rolltui_test;

namespace {

// THE ROLE ORDER, FROM THE LIBRARY — expanded from `ROLLTUI_ROLE_LIST` (Phase 17 m2a), not
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
constexpr std::string_view role_name(Role r) { return kRoleNamesTable()[static_cast<std::size_t>(r)]; }

// `rolltui::ThemeMode` (Theme.hpp): no C form either -- the loader takes the ordinal as a
// raw int, never a name.
enum class ThemeMode : unsigned char { Dark, Light };

// ---- mirrors rolltui::Theme (Theme.hpp), but only the STYLE TABLE, NAME and META this
// file reads -- `.effects` (EffectMap) is a C++-only shim type this fixture never touches
// (no theme text used below has an "effects" key). `.meta` IS read (the generator's
// inputs and the claimed badges), so it is reproduced exactly: OWNED, through a
// unique_ptr with a deleter that calls the C free, same as Theme.hpp's own field. ----
struct Theme {
  std::string name;
  struct MetaDeleter {
    void operator()(RolltuiJsonValue* p) const { rolltui_json_free(p); }
  };
  std::unique_ptr<RolltuiJsonValue, MetaDeleter> meta;
  std::array<RolltuiStyle, kRoleCount> styles{};
  const RolltuiStyle& style(Role r) const { return styles[static_cast<std::size_t>(r)]; }

  Theme() = default;
  Theme(const Theme& o) : name(o.name), meta(rolltui_json_clone(o.meta.get())), styles(o.styles) {}
  Theme(Theme&&) = default;
  Theme& operator=(const Theme& o) {
    if (this != &o) {
      name = o.name;
      meta.reset(rolltui_json_clone(o.meta.get()));
      styles = o.styles;
    }
    return *this;
  }
  Theme& operator=(Theme&&) = default;
};

struct ThemeLoadReport {
  std::string error;
  std::vector<std::string> missing_roles;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && missing_roles.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// Mirrors Theme.cpp's theme_vocab(): the role NAME table handed to the C loader/dumper/
// generator once per call. No effect-state names: no theme text below has an "effects"
// key, so state_count 0 is never consulted.
const RolltuiThemeVocab& theme_vocab() {
  static const RolltuiThemeVocab v = [] {
    RolltuiThemeVocab t{};
    t.role_names = kRoleNamesTable().data();
    t.role_count = kRoleCount;
    t.text_role = static_cast<std::size_t>(Role::text);
    t.state_names = nullptr;
    t.state_count = 0;
    t.fallback_effect_role = static_cast<unsigned char>(Role::accent_1);
    return t;
  }();
  return v;
}

// Mirrors rolltui::load_theme(string_view, ThemeMode, ThemeLoadReport&) (Theme.cpp):
// parse, hand the tree to the C loader, translate its report field for field, then
// resolve "meta" — including the per-variant ("dark"/"light") claimed-badges resolution
// Theme.cpp's real loader applies, since a generated theme's meta is read straight back
// by this file's round-trip check.
std::optional<Theme> load_theme(std::string_view json_text, ThemeMode mode, ThemeLoadReport& report) {
  report = ThemeLoadReport{};
  RolltuiStr err{};
  RolltuiJsonValue* root_c = rolltui_json_parse(json_text.data(), json_text.size(), &err);
  if (!root_c) {
    report.error = str_of(err);
    rolltui_str_free(&err);
    return std::nullopt;
  }
  rolltui_str_free(&err);

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
    rolltui_json_free(root_c);
    return std::nullopt;
  }
  rolltui_effect_map_free(eff);  // this fixture never reads effects
  t.name = str_of(name);
  rolltui_str_free(&name);

  const RolltuiJsonValue* meta_c = rolltui_json_get(root_c, "meta", 4);
  if (rolltui_json_is_object(meta_c)) {
    t.meta.reset(rolltui_json_clone(meta_c));  // OWNS its own clone; meta_c borrows root_c
    const RolltuiJsonValue* b = rolltui_json_get(t.meta.get(), "badges", 6);
    const char* key = mode == ThemeMode::Dark ? "dark" : "light";
    if (rolltui_json_is_object(b) && rolltui_json_has(b, key, std::strlen(key)))
      rolltui_json_set(t.meta.get(), "badges", 6, rolltui_json_clone(rolltui_json_get(b, key, std::strlen(key))));
  }
  rolltui_json_free(root_c);
  return t;
}

// Mirrors rolltui::theme_to_json (Theme.cpp), over the same rolltui_theme_dump call
// theme_test.cpp's mirror uses (no effects: nullptr for both effects-map arguments), plus
// "meta" — the one field theme_test.cpp's own mirror does not carry.
std::string theme_to_json(const Theme& theme) {
  RolltuiJsonValue* root = rolltui_json_object();
  rolltui_json_set(root, "name", 4, rolltui_json_string(theme.name.data(), theme.name.size()));
  if (rolltui_json_is_object(theme.meta.get())) rolltui_json_set(root, "meta", 4, rolltui_json_clone(theme.meta.get()));
  RolltuiJsonValue* c = rolltui_theme_dump(theme.styles.data(), nullptr, nullptr, nullptr, &theme_vocab());
  rolltui_json_set(root, "roles", 5, rolltui_json_clone(rolltui_json_get(c, "roles", 5)));
  rolltui_json_free(c);
  RolltuiStr out{};
  rolltui_json_dump(root, 2, &out);
  std::string result(str_of(out));
  rolltui_str_free(&out);
  rolltui_json_free(root);
  return result + "\n";
}

// ---- mirrors the half of rolltui::ThemeAnalysis this file reads: only the computed
// Badges, over rolltui_theme_analyse (rolltui_theme_analysis.h) — never a per-role or
// per-pair check, which rolltui/tests/theme_analysis_test.cpp is the oracle for and
// reproduces in full. ----
using Badges = RolltuiBadges;
struct ThemeReport {
  Badges badges{};
};
ThemeReport analyse(const Theme& theme) {
  ThemeReport rep;
  std::vector<RolltuiRoleCheck> croles(kRoleCount);
  const std::size_t pair_count = rolltui_must_differ_count();
  std::vector<RolltuiPairCheck> cpairs(pair_count);
  rolltui_theme_analyse(theme.styles.data(), kRoleCount, croles.data(), cpairs.data(), &rep.badges);
  return rep;
}
std::vector<std::string> badge_names(const Badges& b) {
  RolltuiStrArray a{};
  rolltui_badge_names(&b, &a);
  std::vector<std::string> out;
  out.reserve(a.n);
  for (std::size_t i = 0; i < a.n; ++i) out.emplace_back(a.v[i].p ? a.v[i].p : "", a.v[i].n);
  rolltui_str_array_release(&a);
  return out;
}
bool has_badge(const Badges& b, std::string_view name) { return rolltui_has_badge(&b, name.data(), name.size()) != 0; }
std::vector<std::string> check_claims(const Theme& theme, const ThemeReport& report) {
  RolltuiStrArray a{};
  rolltui_check_claims(theme.meta.get(), &report.badges, &a);
  std::vector<std::string> failed;
  failed.reserve(a.n);
  for (std::size_t i = 0; i < a.n; ++i) failed.emplace_back(a.v[i].p ? a.v[i].p : "", a.v[i].n);
  rolltui_str_array_release(&a);
  return failed;
}

// ---- mirrors rolltui::Ruleset/kRulesets/ruleset_name (ThemeGen.hpp): the ruleset NAME
// has no C enum form either (a `Ruleset` crosses the boundary call as the same byte
// rolltui_theme_gen.h's ROLLTUI_RULESET_* macros name), so the enum is reproduced and
// `ruleset_name` calls straight through to the C name table. `ruleset_from_name` is not
// needed below. ----
enum class Ruleset : std::uint8_t { Analogous, Complementary, Triadic, Tetradic, Monochrome, Pastel, Neon, Earth };
constexpr Ruleset kRulesets[] = {Ruleset::Analogous, Ruleset::Complementary, Ruleset::Triadic, Ruleset::Tetradic,
                                 Ruleset::Monochrome, Ruleset::Pastel, Ruleset::Neon, Ruleset::Earth};
std::string_view ruleset_name(Ruleset r) {
  std::size_t len = 0;
  const char* s = rolltui_ruleset_name(static_cast<unsigned char>(r), &len);
  return std::string_view(s, len);
}

struct GenOptions {
  std::optional<bool> dark;  // force a dark or light ground; nullopt: the seed decides
  int max_repair_passes = 8;
};

// The generated theme (named "gen-<ruleset>-<seed>-<chaos>", meta: {generator: {ruleset,
// seed, chaos}, badges: the computed ones}), plus what the repair loop did.
struct Generated {
  Theme theme;
  int repairs = 0;                      // fixes applied by the repair loop
  std::vector<std::string> broken;      // rules still broken after repair (chaos, or gave up)
  std::vector<std::string> badges;      // the computed badges
};

// splitmix64 — `RolltuiRng` IS this struct (rolltui_theme_gen.h defines `next()`/`unit()`
// inline on it directly), the same one-definition move Phase 14 m2 made for Color/Style.
using Rng = RolltuiRng;

// Mirrors rolltui::generate() (ThemeGen.cpp): a thin shim over rolltui_theme_generate.
// Every hue/lightness pick, the repair loop and the meta tree live in rolltui_theme_gen.c;
// this converts GenOptions to plain scalars, calls it, and builds the C++-only Generated
// from what it hands back — including the FINAL rolltui_theme_analyse snapshot, which is
// where role_name() builds `broken` from.
Generated generate(std::uint64_t seed, Ruleset ruleset, double chaos, const GenOptions& opts = {}) {
  Generated out;
  std::vector<RolltuiRoleCheck> croles(kRoleCount);
  const std::size_t pair_count = rolltui_must_differ_count();
  std::vector<RolltuiPairCheck> cpairs(pair_count);
  RolltuiBadges cbadges{};
  RolltuiStr cname{};
  RolltuiJsonValue* cmeta = nullptr;
  int repairs = 0;
  const int has_dark = opts.dark.has_value() ? 1 : 0;
  const int dark_value = opts.dark.value_or(false) ? 1 : 0;

  rolltui_theme_generate(seed, static_cast<unsigned char>(ruleset), chaos, has_dark, dark_value,
                        opts.max_repair_passes, &theme_vocab(), out.theme.styles.data(), kRoleCount, &cname, &cmeta,
                        &repairs, croles.data(), cpairs.data(), &cbadges);

  out.theme.name.assign(cname.p ? cname.p : "", cname.n);
  rolltui_str_free(&cname);
  out.theme.meta.reset(cmeta);  // adopts
  out.repairs = repairs;
  out.badges = badge_names(cbadges);
  for (const RolltuiRoleCheck& c : croles)
    if (c.text && !c.unknown && !c.readable)
      out.broken.push_back(std::string(role_name(static_cast<Role>(c.role))) + " contrast " + std::to_string(c.wcag).substr(0, 4));
  // A colour-confusable pair that differs by an attribute is repaired (the plan's rule:
  // "never rely on colour alone"); it still costs the cvd-safe badge, honestly.
  for (const RolltuiPairCheck& c : cpairs)
    if (!c.unknown && !(c.distinct && c.cvd_distinct) && !c.attribute_redundant)
      out.broken.push_back(std::string(role_name(static_cast<Role>(c.a))) + "/" +
                            std::string(role_name(static_cast<Role>(c.b))) + " confusable");
  return out;
}

}  // namespace

int main() {
  // ---- the PRNG ----
  {
    Rng a(42), b(42);
    bool same = true;
    for (int i = 0; i < 100; ++i) same &= a.next() == b.next();
    check(same, "splitmix64: the same seed gives the same sequence");
    Rng c(43);
    check(Rng(42).next() != c.next(), "…and a different seed a different one");
    Rng u(7);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) { const double x = u.unit(); in_range &= x >= 0.0 && x < 1.0; }
    check(in_range, "unit() stays in [0, 1)");
  }
  // ---- determinism ----
  {
    const Generated g1 = generate(1, Ruleset::Triadic, 0.0), g2 = generate(1, Ruleset::Triadic, 0.0);
    check(g1.theme.styles == g2.theme.styles && g1.theme.name == g2.theme.name && g1.badges == g2.badges, "the same inputs yield the same theme");
    const Generated g3 = generate(2, Ruleset::Triadic, 0.0);
    check(!(g3.theme.styles == g1.theme.styles), "a different seed yields a different theme");
    const Generated g4 = generate(1, Ruleset::Neon, 0.0);
    check(!(g4.theme.styles == g1.theme.styles), "a different ruleset too");
    check(g1.theme.name == "gen-triadic-1-0.00", "the name records ruleset, seed and chaos [" + g1.theme.name + "]");
    const RolltuiJsonValue* gen1 = rolltui_json_get(g1.theme.meta.get(), "generator", 9);
    std::size_t ruleset1_len = 0;
    const char* ruleset1 = rolltui_json_as_string(rolltui_json_get(gen1, "ruleset", 7), "", 0, &ruleset1_len);
    check(rolltui_json_as_number(rolltui_json_get(gen1, "seed", 4), -1) == 1 &&
              std::string_view(ruleset1, ruleset1_len) == "triadic" &&
              rolltui_json_is_array(rolltui_json_get(g1.theme.meta.get(), "badges", 6)) != 0,
          "meta records the generator inputs and the computed badges");
  }
  // ---- every ruleset at chaos 0 is readable ----
  {
    bool all = true;
    for (Ruleset r : kRulesets)
      for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const Generated g = generate(seed, r, 0.0);
        const bool ok = has_badge(analyse(g.theme).badges, "readable") && g.broken.empty();
        if (!ok) { all = false; check(false, std::string(ruleset_name(r)) + " seed " + std::to_string(seed) + " at chaos 0 is not readable: " + (g.broken.empty() ? "?" : g.broken[0])); }
      }
    check(all, "every ruleset × 12 seeds at chaos 0 passes readable with nothing broken (the repair loop delivers what it promises)");
    int dark = 0, light = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
      const Generated g = generate(seed, Ruleset::Analogous, 0.0);
      if (has_badge(analyse(g.theme).badges, "dark")) ++dark; else ++light;
    }
    check(dark > 5 && light > 5, "the seed decides the ground: both dark and light themes occur (" + std::to_string(dark) + " dark, " + std::to_string(light) + " light of 40)");
    GenOptions force;
    force.dark = false;
    check(has_badge(analyse(generate(3, Ruleset::Earth, 0.0, force).theme).badges, "light"), "GenOptions::dark forces the ground");
  }
  // ---- chaos is real ----
  {
    int broken_seeds = 0, first = -1;
    for (int seed = 1; seed <= 30; ++seed) {
      const Generated g = generate(static_cast<std::uint64_t>(seed), Ruleset::Neon, 1.0);
      if (!g.broken.empty()) { ++broken_seeds; if (first < 0) first = seed; }
    }
    check(broken_seeds > 0, "at chaos 1 some seed reports a broken rule (" + std::to_string(broken_seeds) + " of 30; first seed " + std::to_string(first) + ")");
    check(broken_seeds < 30, "…and not every seed: chaos widens, it does not guarantee breakage (" + std::to_string(broken_seeds) + " of 30)");
    const Generated g = generate(static_cast<std::uint64_t>(first), Ruleset::Neon, 1.0);
    const std::vector<std::string> failed = check_claims(g.theme, analyse(g.theme));
    check(failed.empty(), "a generated theme's claimed badges are exactly the computed ones, even when chaos broke a rule");
  }
  // ---- the file round trip keeps meta ----
  {
    const Generated g = generate(9, Ruleset::Pastel, 0.25);
    const std::string text = theme_to_json(g.theme);
    ThemeLoadReport rep;
    std::optional<Theme> back = load_theme(text, ThemeMode::Dark, rep);
    check(back && rep.clean() && back->styles == g.theme.styles && rolltui_json_equal(back->meta.get(), g.theme.meta.get()) != 0,
          "theme_to_json keeps meta, and the loader accepts it as a known key");
    check(text.find("\"generator\"") != std::string::npos && text.find("\"chaos\": 0.25") != std::string::npos, "…with the generator inputs in the file");
  }
  return report("rolltui theme_gen_test");
}
