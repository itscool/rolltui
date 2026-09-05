// rolltui/ThemeAnalysis.cpp — see ThemeAnalysis.hpp. The colour maths (this file's first
// section) is now a thin C++ shim over rolltui/c/rolltui_theme_analysis.h/.c — every
// formula, its reference and every published reference value moved there with it.
// The report and the auto-fix (below) are thin shims too now (Phase 17 m5): each converts
// its Theme/Role-shaped C++ arguments to the C function's styles-table-and-ordinals shape,
// calls it, and converts the result back. Nothing here computes a contrast ratio, a hue
// rotation or composes the report's English any more — see rolltui_theme_analysis.h's own
// header comment for the algorithm and for which sentences moved to C and which did not.
// The tests hold this file to the same reference values as before; nothing here is tuned by
// eye, and nothing here allocates beyond the plain conversions (a `std::vector` sized once,
// never grown).
#include "rolltui/ThemeAnalysis.hpp"

#include <algorithm>
#include <cmath>   // std::fmod, in fix_confusable's hue rotation — everything else moved to the C
#include <cstdio>

#include "rolltui/c/rolltui_theme.h"  // theme_vocab()'s return type, RolltuiThemeVocab

namespace rolltui {

// Borrowed from Theme.cpp, which gives it external linkage for exactly this reason —
// `Presets.cpp` already forward-declares it the same way (Theme.cpp's own header comment).
// Not declared in ThemeAnalysis.hpp: that header's public shape stays what every other
// caller of `rolltui::ThemeAnalysis` already sees.
const RolltuiThemeVocab& theme_vocab();

// ---- colour spaces: thin forwarding shims over rolltui/c/rolltui_theme_analysis.h ------

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

std::string_view cvd_name(Cvd c) {
  std::size_t len;
  const char* s = rolltui_cvd_name(static_cast<unsigned char>(c), &len);
  return std::string_view(s, len);
}

Lin simulate_cvd(Lin l, Cvd type) {
  Lin out;
  rolltui_simulate_cvd(l, static_cast<unsigned char>(type), &out);
  return out;
}

// ---- the report — thin shim over rolltui_theme_analyse (Phase 17 m5) ---------------------

ThemeReport analyse(const Theme& theme) {
  ThemeReport rep;
  std::vector<RolltuiRoleCheck> croles(kRoleCount);
  const std::size_t pair_count = rolltui_must_differ_count();
  std::vector<RolltuiPairCheck> cpairs(pair_count);
  rolltui_theme_analyse(theme.styles.data(), kRoleCount, croles.data(), cpairs.data(), &rep.badges);

  // `all_none`/the background's own linearisability are recomputed here, directly over
  // `theme.styles` and the C call's own `rolltui_to_linear` — one-line, pure re-checks
  // (not a second implementation of any RULE) rather than a vocab table `analyse()` would
  // otherwise have no other reason to take. See rolltui_theme_analysis.h's header comment
  // for why the notes below are built here rather than in C.
  bool all_none = true;
  for (const Style& s : theme.styles) all_none &= s.fg.kind == Color::Kind::None && s.bg.kind == Color::Kind::None;
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
  std::vector<RolltuiStr> notes(r.notes.begin(), r.notes.end());
  RolltuiStr out{};
  rolltui_theme_report_text(croles.data(), croles.size(), cpairs.data(), cpairs.size(), &r.badges, notes.data(),
                            notes.size(), &theme_vocab(), &out);
  std::string result(out.view());
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

// ---- auto-fix — thin shims over rolltui_fix_contrast/_confusable/rolltui_propose_fixes/
// rolltui_apply_fix (Phase 17 m5) ----------------------------------------------------------

std::optional<Fix> fix_contrast(const Theme& theme, Role role, double target) {
  RolltuiFix cf{};
  if (!rolltui_fix_contrast(theme.styles.data(), kRoleCount, static_cast<unsigned char>(role), target, &theme_vocab(),
                            &cf))
    return std::nullopt;
  Fix fix;
  fix.role = static_cast<Role>(cf.role);
  fix.before = cf.before;
  fix.after = cf.after;
  fix.what = cf.what.str();
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
  fix.what = cf.what.str();
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
    fix.what = cf.what.str();
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

}  // namespace rolltui
