//
// theme_analysis_test.cpp — the colour maths against published reference values
// (milestone 15): sRGB linearisation, OKLab of sRGB red / white / black and the round
// trip, WCAG's own examples, APCA's canonical black-on-white / white-on-black, the
// Machado matrices (rows sum to 1 so grey is preserved; pure red loses its red under
// protanopia), ΔE; then the report on the built-ins (their badges asserted, as the plan
// requires), the unknown-colour rule, claimed badges, and auto-fix on a deliberately
// broken pair.
//
// calls the C API (rolltui/c/rolltui_theme_analysis.h, rolltui_theme.h,
// rolltui_json.h, all reached through rolltui/rolltui.h) directly — ThemeAnalysis.hpp,
// Theme.hpp and Style.hpp are all deleted along with the rest of the C++ binding
// . `Lin`/`OkLab`/`OkLch`/`Badges` were one-definition
// aliases over the same C structs and are reproduced verbatim — there was
// never a second definition to convert away from. `Role` (Style.hpp) has no C enum form
// at all — a C file names no role, so the ordinal crosses a boundary call and the name
// table stays in every C++ consumer that needs it — reproduced below exactly as
// rolltui/tests/theme_test.cpp reproduces it independently. `RoleCheck`/`PairCheck`/`Fix`
// stay real (mirrored) C++ structs rather than aliases for the same reason
// rolltui_theme_analysis.h's own header comment gives: this file reads `c.role ==
// Role::warning`, an enum comparison a C struct cannot carry. `analyse`, `report_text`,
// `check_claims`, `fix_contrast`, `fix_confusable`, `propose_fixes` and `apply_fix` were
// thin forwarding shims over `rolltui_theme_analyse`/`_report_text`/`_check_claims`/
// `fix_contrast`/`fix_confusable`/`propose_fixes`/`apply_fix` and are reproduced the same
// way, over the same C calls — nothing here computes a contrast ratio, a hue rotation or
// composes the report's English; the reference values below still hold the C maths to
// account.
//
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_style.h"

#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_theme.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_theme_analysis.h"  // INTERNAL: this test opts in

using namespace rolltui_test;
using namespace testkit;

namespace {
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : " ") + x;
  return s;
}

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
constexpr std::string_view role_name(Role r) { return kRoleNamesTable()[static_cast<std::size_t>(r)]; }

// `rolltui::Color` (Style.hpp) was a one-definition alias over the same C struct --
// reproduced verbatim.
using Color = RolltuiStyleColor;

// ---- mirrors rolltui::Theme (Theme.hpp), but only the STYLE TABLE, NAME and META this
// file reads -- `.effects` (EffectMap) is a C++-only shim type this fixture never
// touches. ----
struct Theme {
  std::string name;
  struct MetaDeleter {
    void operator()(RolltuiJsonValue* p) const { rolltui_json_free(p); }
  };
  std::unique_ptr<RolltuiJsonValue, MetaDeleter> meta;
  std::array<RolltuiStyle, kRoleCount> styles{};
  const RolltuiStyle& style(Role r) const {
    return *rolltui_theme_style(styles.data(), styles.size(), static_cast<unsigned char>(r));
  }
  RolltuiStyle& style(Role r) { return styles[static_cast<std::size_t>(r)]; }

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

// Mirrors rolltui::builtin_theme's cache (Theme.cpp), minus `.effects`: filled once from
// the C built-ins, keyed by name.
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
        rolltui_effect_map_free(m);  // this fixture never reads effects
      v.emplace_back(nm, t);
    }
    return v;
  }();
  for (const auto& [n, t] : cache)
    if (n == name) return &t;
  return nullptr;
}

// Mirrors Theme.cpp's theme_vocab(): the role NAME table handed to the C analysis/fix
// calls once per call. No effect-state names: this file never touches effects.
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

// ---- mirrors rolltui::Lin/OkLab/OkLch (ThemeAnalysis.hpp): one-definition aliases over
// the same C structs -- reproduced verbatim. ----
using Lin = RolltuiLin;
using OkLab = RolltuiOkLab;
using OkLch = RolltuiOkLch;

// ---- mirrors the colour-space/contrast/distance shims (ThemeAnalysis.cpp): thin
// forwarding calls over rolltui/c/rolltui_theme_analysis.h, reproduced verbatim. ----
double srgb_channel_to_linear(double c) { return rolltui_srgb_channel_to_linear(c); }
double linear_channel_to_srgb(double v) { return rolltui_linear_channel_to_srgb(v); }
std::optional<Lin> to_linear(Color c) {
  Lin out;
  if (!rolltui_to_linear(c, &out)) return std::nullopt;
  return out;
}
Color from_linear(Lin l) {
  Color c;
  rolltui_from_linear(l, &c);
  return c;
}
OkLab linear_to_oklab(Lin c) {
  OkLab out;
  rolltui_linear_to_oklab(c, &out);
  return out;
}
Lin oklab_to_linear(OkLab lab) {
  Lin out;
  rolltui_oklab_to_linear(lab, &out);
  return out;
}
OkLch oklab_to_oklch(OkLab lab) {
  OkLch out;
  rolltui_oklab_to_oklch(lab, &out);
  return out;
}
OkLab oklch_to_oklab(OkLch lch) {
  OkLab out;
  rolltui_oklch_to_oklab(lch, &out);
  return out;
}
double relative_luminance(Lin l) { return rolltui_relative_luminance(l); }
double wcag_contrast(Lin a, Lin b) { return rolltui_wcag_contrast(a, b); }
double apca_contrast(Lin text, Lin bg) { return rolltui_apca_contrast(text, bg); }
double delta_e(OkLab a, OkLab b) { return rolltui_delta_e(a, b); }

enum class Cvd : std::uint8_t { Protanopia, Deuteranopia, Tritanopia };
constexpr Cvd kCvdTypes[] = {Cvd::Protanopia, Cvd::Deuteranopia, Cvd::Tritanopia};
Lin simulate_cvd(Lin l, Cvd type) {
  Lin out;
  rolltui_simulate_cvd(l, static_cast<unsigned char>(type), &out);
  return out;
}

// ---- thresholds: aliases of rolltui_theme_analysis.h's ROLLTUI_* macros, so the number
// is written down once. ----
constexpr double kReadableRatio = ROLLTUI_READABLE_RATIO;
constexpr double kDistinctDeltaE = ROLLTUI_DISTINCT_DELTA_E;
const std::size_t kMustDifferCount = rolltui_must_differ_count();

// ---- the report: RoleCheck/PairCheck/Fix stay real C++ structs (see header note above),
// built by analyse()/propose_fixes()'s shims from the C calls' unsigned char ordinals. ----
struct RoleCheck {
  Role role;
  bool text = true;                  // a role whose fg is drawn as text (counts for readable / high-contrast)
  std::optional<double> wcag, apca;  // absent when either colour is the terminal's
  Color fg, bg;                      // the colours measured (bg: the role's, else the theme's background)
  bool unknown = false;              // depends on the terminal
  bool readable = false, high = false;
};
struct PairCheck {
  Role a, b;
  std::optional<double> delta;                    // normal vision
  std::array<std::optional<double>, 3> delta_cvd;  // per kCvdTypes
  bool unknown = false;
  bool distinct = false;             // normal vision ≥ threshold
  bool cvd_distinct = false;         // under every simulation too
  bool attribute_redundant = false;  // also differ by bold/italic/underline/dim/reverse
  bool collapses_16 = false, collapses_256 = false;
};
using Badges = RolltuiBadges;
struct ThemeReport {
  std::vector<RoleCheck> roles;
  std::vector<PairCheck> pairs;
  Badges badges{};
  std::vector<std::string> notes;  // what could not be measured, in words
};

// Mirrors rolltui::analyse (ThemeAnalysis.cpp): thin shim over rolltui_theme_analyse,
// plus the notes -- built here, not in C, over the same one-line re-checks the real shim
// used (not a second implementation of any rule).
ThemeReport analyse(const Theme& theme) {
  ThemeReport rep;
  std::vector<RolltuiRoleCheck> croles(kRoleCount);
  const std::size_t pair_count = rolltui_must_differ_count();
  std::vector<RolltuiPairCheck> cpairs(pair_count);
  rolltui_theme_analyse(theme.styles.data(), kRoleCount, croles.data(), cpairs.data(), &rep.badges);

  bool all_none = true;
  for (const RolltuiStyle& s : theme.styles)
    all_none &= s.fg.kind == RolltuiStyleColor::Kind::None && s.bg.kind == RolltuiStyleColor::Kind::None;
  Lin bg_lin;
  if (!rolltui_to_linear(theme.style(Role::background).bg, &bg_lin))
    rep.notes.push_back("background is the terminal's own colour: dark/light and every contrast against it depend on the terminal");

  bool any_text_known = false;
  for (const RolltuiRoleCheck& c : croles)
    if (c.text && !c.unknown) any_text_known = true;
  if (!any_text_known && !all_none) rep.notes.push_back("no text role has both colours known; readable / high-contrast cannot be awarded");

  rep.roles.reserve(kRoleCount);
  for (const RolltuiRoleCheck& c : croles) {
    RoleCheck rc;
    rc.role = static_cast<Role>(c.role);
    rc.text = c.text != 0;
    rc.fg = c.fg;
    rc.bg = c.bg;
    rc.unknown = c.unknown != 0;
    if (!rc.unknown) { rc.wcag = c.wcag; rc.apca = c.apca; }
    rc.readable = c.readable != 0;
    rc.high = c.high != 0;
    rep.roles.push_back(rc);
  }
  rep.pairs.reserve(pair_count);
  for (const RolltuiPairCheck& c : cpairs) {
    PairCheck pc;
    pc.a = static_cast<Role>(c.a);
    pc.b = static_cast<Role>(c.b);
    pc.unknown = c.unknown != 0;
    if (!pc.unknown) {
      pc.delta = c.delta;
      for (int k = 0; k < 3; ++k) pc.delta_cvd[k] = c.delta_cvd[k];
    }
    pc.distinct = c.distinct != 0;
    pc.cvd_distinct = c.cvd_distinct != 0;
    pc.attribute_redundant = c.attribute_redundant != 0;
    pc.collapses_16 = c.collapses_16 != 0;
    pc.collapses_256 = c.collapses_256 != 0;
    rep.pairs.push_back(pc);
    if (pc.unknown && !all_none)
      rep.notes.push_back(std::string(role_name(pc.a)) + " / " + std::string(role_name(pc.b)) +
                           ": a foreground is the terminal's own; distinguishability depends on the terminal");
  }
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

std::string report_text(const ThemeReport& r) {
  std::vector<RolltuiRoleCheck> croles(r.roles.size());
  for (std::size_t i = 0; i < r.roles.size(); ++i) {
    const RoleCheck& c = r.roles[i];
    RolltuiRoleCheck& o = croles[i];
    o.role = static_cast<unsigned char>(c.role);
    o.text = c.text;
    o.fg = c.fg;
    o.bg = c.bg;
    o.unknown = c.unknown;
    o.wcag = c.wcag.value_or(0.0);
    o.apca = c.apca.value_or(0.0);
    o.readable = c.readable;
    o.high = c.high;
  }
  std::vector<RolltuiPairCheck> cpairs(r.pairs.size());
  for (std::size_t i = 0; i < r.pairs.size(); ++i) {
    const PairCheck& c = r.pairs[i];
    RolltuiPairCheck& o = cpairs[i];
    o.a = static_cast<unsigned char>(c.a);
    o.b = static_cast<unsigned char>(c.b);
    o.unknown = c.unknown;
    o.delta = c.delta.value_or(0.0);
    for (int k = 0; k < 3; ++k) o.delta_cvd[k] = c.delta_cvd[k].value_or(0.0);
    o.distinct = c.distinct;
    o.cvd_distinct = c.cvd_distinct;
    o.attribute_redundant = c.attribute_redundant;
    o.collapses_16 = c.collapses_16;
    o.collapses_256 = c.collapses_256;
  }
  std::vector<RolltuiStr> notes;
  for (const std::string& n : r.notes) { notes.emplace_back(); notes.back().assign(n.data(), n.size()); }
  RolltuiStr out{};
  rolltui_theme_report_text(croles.data(), croles.size(), cpairs.data(), cpairs.size(), &r.badges, notes.data(),
                            notes.size(), &theme_vocab(), &out);
  std::string result(view_of(out));
  rolltui_str_free(&out);
  return result;
}

std::vector<std::string> check_claims(const Theme& theme, const ThemeReport& report) {
  RolltuiStrArray a{};
  rolltui_check_claims(theme.meta.get(), &report.badges, &a);
  std::vector<std::string> failed;
  failed.reserve(a.n);
  for (std::size_t i = 0; i < a.n; ++i) failed.emplace_back(a.v[i].p ? a.v[i].p : "", a.v[i].n);
  rolltui_str_array_release(&a);
  return failed;
}

// ---- auto-fix: thin shims over rolltui_fix_contrast/_confusable/_propose_fixes/
// _apply_fix, reproduced verbatim. ----
struct Fix {
  Role role;
  RolltuiStyle before, after;
  std::string what;  // "md_link fg: contrast 3.1 → 4.6"
  double before_value = 0, after_value = 0;
};
std::optional<Fix> fix_contrast(const Theme& theme, Role role, double target = kReadableRatio) {
  RolltuiFix cf{};
  if (!rolltui_fix_contrast(theme.styles.data(), kRoleCount, static_cast<unsigned char>(role), target, &theme_vocab(),
                            &cf))
    return std::nullopt;
  Fix fix;
  fix.role = static_cast<Role>(cf.role);
  fix.before = cf.before;
  fix.after = cf.after;
  fix.what = str_of(cf.what);
  fix.before_value = cf.before_value;
  fix.after_value = cf.after_value;
  rolltui_fix_release(&cf);
  return fix;
}
std::optional<Fix> fix_confusable(const Theme& theme, Role a, Role b) {
  RolltuiFix cf{};
  if (!rolltui_fix_confusable(theme.styles.data(), kRoleCount, static_cast<unsigned char>(a),
                              static_cast<unsigned char>(b), &theme_vocab(), &cf))
    return std::nullopt;
  Fix fix;
  fix.role = static_cast<Role>(cf.role);
  fix.before = cf.before;
  fix.after = cf.after;
  fix.what = str_of(cf.what);
  fix.before_value = cf.before_value;
  fix.after_value = cf.after_value;
  rolltui_fix_release(&cf);
  return fix;
}
std::vector<Fix> propose_fixes(const Theme& theme) {
  RolltuiFixArray a{};
  rolltui_propose_fixes(theme.styles.data(), kRoleCount, &theme_vocab(), &a);
  std::vector<Fix> out;
  out.reserve(a.n);
  for (std::size_t i = 0; i < a.n; ++i) {
    const RolltuiFix& cf = a.v[i];
    Fix fix;
    fix.role = static_cast<Role>(cf.role);
    fix.before = cf.before;
    fix.after = cf.after;
    fix.what = str_of(cf.what);
    fix.before_value = cf.before_value;
    fix.after_value = cf.after_value;
    out.push_back(std::move(fix));
  }
  rolltui_fix_array_release(&a);
  return out;
}
Theme apply_fix(Theme theme, const Fix& fix) {
  rolltui_apply_fix(theme.styles.data(), kRoleCount, static_cast<unsigned char>(fix.role), &fix.after);
  return theme;
}

}  // namespace

int main() {
  // ---- sRGB ↔ linear ----
  check(near(srgb_channel_to_linear(0.5), 0.2140, 0.0005) && near(srgb_channel_to_linear(0.04045), 0.003131, 0.00001) && srgb_channel_to_linear(1.0) == 1.0,
        "sRGB 0.5 → linear 0.214; the 0.04045 knee; 1 → 1");
  check(near(linear_channel_to_srgb(0.2140), 0.5, 0.001) && near(linear_channel_to_srgb(0.0031308), 0.04045, 0.0001), "…and back");
  check(!to_linear(Color::none()) && to_linear(Color::indexed(15)) && near(to_linear(Color::indexed(15))->r, 1.0, 1e-9),
        "none is unknown; index 15 is xterm's white");
  check(from_linear({1, 1, 1}) == Color::rgb(255, 255, 255) && from_linear({0.2142, 0.2142, 0.2142}) == Color::rgb(128, 128, 128) && from_linear({0.2139, 0.2139, 0.2139}) == Color::rgb(127, 127, 127), "from_linear encodes and rounds (0.2142 → 128, 0.2139 → 127)");

  // ---- OKLab (Ottosson's reference values for sRGB primaries) ----
  {
    const OkLab white = linear_to_oklab({1, 1, 1});
    check(near(white.L, 1.0, 1e-3) && near(white.a, 0.0, 1e-3) && near(white.b, 0.0, 1e-3), "white is L 1, a 0, b 0");
    const OkLab black = linear_to_oklab({0, 0, 0});
    check(near(black.L, 0.0, 1e-9) && near(black.a, 0.0, 1e-9), "black is L 0");
    const OkLab red = linear_to_oklab({1, 0, 0});
    check(near(red.L, 0.62796, 1e-3) && near(red.a, 0.22486, 1e-3) && near(red.b, 0.12585, 1e-3),
          "sRGB red is L 0.628, a 0.225, b 0.126 (the published value)");
    const OkLab green = linear_to_oklab({0, 1, 0}), blue = linear_to_oklab({0, 0, 1});
    check(near(green.L, 0.86644, 1e-3) && near(green.a, -0.23389, 1e-3) && near(blue.L, 0.45201, 1e-3) && near(blue.b, -0.31153, 1e-3),
          "sRGB green and blue match the published values");
    const Lin back = oklab_to_linear(red);
    check(near(back.r, 1.0, 1e-6) && near(back.g, 0.0, 1e-6) && near(back.b, 0.0, 1e-6), "OKLab → linear round-trips red");
    const OkLch lch = oklab_to_oklch(red);
    check(near(lch.C, 0.2577, 1e-3) && near(lch.h, 29.23, 0.1), "red in OKLCH: C 0.258, h 29.2°");
    const OkLab from = oklch_to_oklab(lch);
    check(near(from.a, red.a, 1e-9) && near(from.b, red.b, 1e-9), "OKLCH → OKLab round-trips");
    check(near(oklab_to_oklch({0.5, -0.1, -0.1}).h, 225.0, 1e-6), "a negative a,b lands in the third quadrant (225°), never negative");
  }
  // ---- WCAG ----
  check(near(wcag_contrast({1, 1, 1}, {0, 0, 0}), 21.0, 1e-9) && near(wcag_contrast({0, 0, 0}, {1, 1, 1}), 21.0, 1e-9), "white/black is 21:1 either way");
  {
    const Lin grey = *to_linear(Color::rgb(0x77, 0x77, 0x77));
    check(near(wcag_contrast(grey, {1, 1, 1}), 4.48, 0.01), "#777777 on white is 4.48:1 (the WCAG example)");
    check(near(relative_luminance(*to_linear(Color::rgb(255, 0, 0))), 0.2126, 1e-6), "red's relative luminance is its Rec. 709 weight");
  }
  // ---- APCA ----
  check(near(apca_contrast({0, 0, 0}, {1, 1, 1}), 106.0, 1.5), "black on white is about +106 Lc (" + std::to_string(apca_contrast({0, 0, 0}, {1, 1, 1})) + ")");
  check(near(apca_contrast({1, 1, 1}, {0, 0, 0}), -107.9, 1.5), "white on black is about -108 Lc (" + std::to_string(apca_contrast({1, 1, 1}, {0, 0, 0})) + ")");
  check(apca_contrast({0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}) == 0.0, "the same colour is 0 Lc");
  // ---- CVD ----
  {
    bool grey_kept = true;
    for (Cvd t : kCvdTypes) {
      const Lin g = simulate_cvd({0.3, 0.3, 0.3}, t);
      grey_kept &= near(g.r, 0.3, 1e-4) && near(g.g, 0.3, 1e-4) && near(g.b, 0.3, 1e-4);
    }
    check(grey_kept, "every Machado matrix's rows sum to 1: a grey is preserved under all three");
    const Lin pr = simulate_cvd({1, 0, 0}, Cvd::Protanopia);
    check(near(pr.r, 0.152286, 1e-6) && near(pr.g, 0.114503, 1e-6) && near(pr.b, 0.0, 1e-6), "pure red under protanopia is the matrix's first column (0.152, 0.115, 0 clamped)");
    const Lin dg = simulate_cvd({0, 1, 0}, Cvd::Deuteranopia);
    check(near(dg.r, 0.860646, 1e-6) && near(dg.g, 0.672501, 1e-6), "pure green under deuteranopia is the second column");
    const Lin tb = simulate_cvd({0, 0, 1}, Cvd::Tritanopia);
    check(near(tb.r, 0.0, 1e-6) && near(tb.g, 0.147602, 1e-6) && near(tb.b, 0.303900, 1e-6), "pure blue under tritanopia is the third column");
    // Red and green are confusable to a deuteranope: their simulated ΔE is far below normal vision's.
    const Lin r = {1, 0, 0}, g = {0, 1, 0};
    const double normal = delta_e(linear_to_oklab(r), linear_to_oklab(g));
    const double deutan = delta_e(linear_to_oklab(simulate_cvd(r, Cvd::Deuteranopia)), linear_to_oklab(simulate_cvd(g, Cvd::Deuteranopia)));
    check(normal > 0.4 && deutan < normal * 0.7, "red vs green: dE " + std::to_string(normal) + " normally, " + std::to_string(deutan) + " under deuteranopia");
  }
  check(delta_e({0.5, 0, 0}, {0.5, 0, 0}) == 0.0 && near(delta_e(linear_to_oklab({0, 0, 0}), linear_to_oklab({1, 1, 1})), 1.0, 1e-3), "dE: identical 0, black/white 1");

  // ---- the built-ins' badges (the plan's assertion) ----
  {
    const ThemeReport dark = analyse(*builtin_theme("default-dark"));
    check(has_badge(dark.badges, "dark") && has_badge(dark.badges, "readable"), "default-dark is dark + readable: " + join(badge_names(dark.badges)));
    check(has_badge(dark.badges, "cvd-safe"), "default-dark is cvd-safe: " + join(badge_names(dark.badges)) + "\n" + report_text(dark));
    check(!has_badge(dark.badges, "mono") && !has_badge(dark.badges, "transparent"), "…not mono, not transparent");
    const ThemeReport light = analyse(*builtin_theme("default-light"));
    check(has_badge(light.badges, "light") && has_badge(light.badges, "readable"), "default-light is light + readable: " + join(badge_names(light.badges)));
    const ThemeReport mono = analyse(*builtin_theme("mono"));
    check(has_badge(mono.badges, "mono") && has_badge(mono.badges, "attribute-redundant") && has_badge(mono.badges, "transparent"),
          "mono is mono + attribute-redundant + transparent: " + join(badge_names(mono.badges)));
    check(!has_badge(mono.badges, "readable") && !has_badge(mono.badges, "dark") && !mono.notes.empty(),
          "…and neither readable nor dark: with the terminal's own colours those are unknown, and the note says so");
    check(dark.roles.size() == kRoleCount && dark.pairs.size() == kMustDifferCount, "one check per role and per must-differ pair");
    bool every_number = true;
    for (const RoleCheck& c : dark.roles) every_number &= !c.unknown && c.wcag && c.apca;
    check(every_number, "every role of default-dark has both numbers (nothing is none)");
    check(report_text(dark).find("badges: dark") == 0 && report_text(dark).find("must-differ pairs") != std::string::npos, "report_text starts with the badges and lists the pairs");

    // THE RULE, BY NAME. `kMustDiffer` is a LIBRARY rule closed on purpose (the
    // decision and every pair's reason are written at the table in rolltui_theme_analysis.c).
    // This reads the pairs the analysis hands back and asserts their MEMBERSHIP, so a change to
    // the table is a change to a test — a decision — rather than an edit nothing notices. The
    // count alone (asserted above) cannot tell eleven right pairs from eleven wrong ones.
    {
      const std::vector<std::pair<Role, Role>> expected = {
          {Role::diff_added, Role::diff_removed},     {Role::warning, Role::error},
          {Role::accent_1, Role::accent_2},           {Role::accent_1, Role::accent_3},
          {Role::accent_1, Role::accent_4},           {Role::accent_2, Role::accent_3},
          {Role::accent_2, Role::accent_4},           {Role::accent_3, Role::accent_4},
          {Role::menu_item, Role::menu_selected},     {Role::input_text, Role::input_placeholder},
          {Role::find_match, Role::find_current}};
      bool same = dark.pairs.size() == expected.size();
      std::string got;
      for (std::size_t i = 0; i < dark.pairs.size(); ++i) {
        same = same && i < expected.size() && dark.pairs[i].a == expected[i].first && dark.pairs[i].b == expected[i].second;
        got += std::string(i ? ", " : "") + std::string(role_name(dark.pairs[i].a)) + "/" + std::string(role_name(dark.pairs[i].b));
      }
      check(same, "the must-differ pairs are exactly the library's eleven, by name and in table order: " + got);
    }
  }
  // ---- unknown colours, claims ----
  {
    Theme t = *builtin_theme("default-dark");
    t.style(Role::warning).fg = Color::none();
    const ThemeReport r = analyse(t);
    bool warn_unknown = false, pair_unknown = false;
    for (const RoleCheck& c : r.roles) if (c.role == Role::warning) warn_unknown = c.unknown;
    for (const PairCheck& c : r.pairs) if (c.a == Role::warning) pair_unknown = c.unknown;
    check(warn_unknown && pair_unknown && !has_badge(r.badges, "readable") && !has_badge(r.badges, "cvd-safe") && !r.notes.empty(),
          "a none foreground is unknown, not assumed: readable and cvd-safe are withheld and a note names the pair");
    Theme claimed = *builtin_theme("default-dark");
    claimed.meta.reset(rolltui_json_object());
    RolltuiJsonValue* badges = rolltui_json_array();
    rolltui_json_array_push(badges, rolltui_json_string("dark", 4));
    rolltui_json_array_push(badges, rolltui_json_string("light", 5));
    rolltui_json_array_push(badges, rolltui_json_string("high-contrast", 13));
    rolltui_json_set(claimed.meta.get(), "badges", 6, badges);
    const std::vector<std::string> failed = check_claims(claimed, analyse(claimed));
    check(failed.size() == 2 && failed[0] == "light" && failed[1] == "high-contrast", "claimed badges are checked: dark holds, light and high-contrast do not [" + join(failed) + "]");
    check(check_claims(*builtin_theme("mono"), analyse(*builtin_theme("mono"))).empty(), "no claims: nothing fails");
  }
  // ---- auto-fix ----
  {
    Theme t = *builtin_theme("default-dark");
    t.style(Role::md_link).fg = Color::rgb(0x30, 0x34, 0x3a);  // barely off the background
    std::optional<Fix> f = fix_contrast(t, Role::md_link);
    check(f && f->before_value < kReadableRatio && f->after_value >= kReadableRatio && f->role == Role::md_link,
          "a low-contrast foreground gets a proposal that reaches 4.5:1 (" + (f ? f->what : std::string("none")) + ")");
    const Theme fixed = apply_fix(t, *f);
    bool link_ok = false;
    for (const RoleCheck& c : analyse(fixed).roles) if (c.role == Role::md_link) link_ok = c.readable;
    check(link_ok, "…and applying it makes the role readable in the report");
    const OkLch before = oklab_to_oklch(linear_to_oklab(*to_linear(f->before.fg))), after = oklab_to_oklch(linear_to_oklab(*to_linear(f->after.fg)));
    check(near(before.h, after.h, 3.0) || after.C < 0.02, "the fix moved lightness and kept the hue (" + std::to_string(before.h) + " → " + std::to_string(after.h) + ")");
    check(!fix_contrast(*builtin_theme("default-dark"), Role::text), "a role that already passes gets no proposal");
    // A confusable pair: make diff_removed the same as diff_added.
    Theme u = *builtin_theme("default-dark");
    u.style(Role::diff_removed).fg = u.style(Role::diff_added).fg;
    std::optional<Fix> g = fix_confusable(u, Role::diff_added, Role::diff_removed);
    check(g && g->role == Role::diff_removed && g->after_value >= kDistinctDeltaE, "an identical pair gets a hue rotation that passes every simulation (" + (g ? g->what : std::string("none")) + ")");
    const Theme u2 = apply_fix(u, *g);
    bool pair_ok = false;
    for (const PairCheck& c : analyse(u2).pairs) if (c.a == Role::diff_added) pair_ok = c.distinct && c.cvd_distinct;
    check(pair_ok, "…and the pair is distinct under every simulation after it");
    // When no hue can satisfy every deficiency (two greys), the fix is attribute redundancy.
    Theme v = *builtin_theme("default-dark");
    v.style(Role::warning).fg = Color::rgb(0x80, 0x80, 0x80);
    v.style(Role::error).fg = Color::rgb(0x82, 0x82, 0x82);
    v.style(Role::error).bold = false;
    v.style(Role::warning).bold = false;
    std::optional<Fix> h = fix_confusable(v, Role::warning, Role::error);
    check(h && (h->after.underline || h->after.bold) && h->what.find("add") != std::string::npos,
          "two greys: the rotation cannot help, so the proposal adds an attribute (" + (h ? h->what : std::string("none")) + ")");
    const std::vector<Fix> all = propose_fixes(v);
    check(!all.empty(), "propose_fixes lists every failing role and pair (" + std::to_string(all.size()) + ")");
    // Once the attribute is applied the pair is REPAIRED: not proposed again (the repair
    // loop would otherwise add underline, then bold, then italic to the same pair and
    // give up — found by the generator at chaos 0), though it still costs cvd-safe.
    const Theme v2 = apply_fix(v, *h);
    bool proposed_again = false;
    for (const Fix& f : propose_fixes(v2)) proposed_again |= f.role == Role::error || f.role == Role::warning;
    bool redundant = false, still_confusable = false;
    for (const PairCheck& c : analyse(v2).pairs) if (c.a == Role::warning) { redundant = c.attribute_redundant; still_confusable = !(c.distinct && c.cvd_distinct); }
    check(!proposed_again && redundant && still_confusable && !has_badge(analyse(v2).badges, "cvd-safe"),
          "a pair told apart by an attribute is not proposed again; it is attribute-redundant, still colour-confusable, and cvd-safe is withheld");
    check(propose_fixes(*builtin_theme("default-dark")).empty(), "…and nothing for a theme that passes");
  }
  return report("rolltui_theme_analysis_test");
}
