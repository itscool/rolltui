// rolltui/ThemeAnalysis.cpp — see ThemeAnalysis.hpp. The constants are the published
// ones and the tests hold them to reference values; nothing here is tuned by eye.
#include "rolltui/ThemeAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rolltui {

// ---- colour spaces ---------------------------------------------------------------------

double srgb_channel_to_linear(double c) {
  c = std::clamp(c, 0.0, 1.0);
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double linear_channel_to_srgb(double v) {
  v = std::clamp(v, 0.0, 1.0);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

std::optional<Lin> to_linear(Color c) {
  if (c.kind == Color::Kind::None) return std::nullopt;
  if (c.kind == Color::Kind::Indexed) c = ansi_index_rgb(c.index);
  return Lin{srgb_channel_to_linear(c.r / 255.0), srgb_channel_to_linear(c.g / 255.0), srgb_channel_to_linear(c.b / 255.0)};
}

Color from_linear(Lin l) {
  auto enc = [](double v) { return static_cast<std::uint8_t>(std::lround(linear_channel_to_srgb(v) * 255.0)); };
  return Color::rgb(enc(l.r), enc(l.g), enc(l.b));
}

OkLab linear_to_oklab(Lin c) {
  const double l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
  const double m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
  const double s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
  const double l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
  return {0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
          1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
          0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_};
}

Lin oklab_to_linear(OkLab lab) {
  const double l_ = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
  const double m_ = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
  const double s_ = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
  const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
          -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
          -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

OkLch oklab_to_oklch(OkLab lab) {
  const double C = std::sqrt(lab.a * lab.a + lab.b * lab.b);
  double h = std::atan2(lab.b, lab.a) * 180.0 / M_PI;
  if (h < 0) h += 360.0;
  return {lab.L, C, h};
}

OkLab oklch_to_oklab(OkLch lch) {
  const double rad = lch.h * M_PI / 180.0;
  return {lch.L, lch.C * std::cos(rad), lch.C * std::sin(rad)};
}

double relative_luminance(Lin l) { return 0.2126 * l.r + 0.7152 * l.g + 0.0722 * l.b; }

double wcag_contrast(Lin a, Lin b) {
  const double la = relative_luminance(a), lb = relative_luminance(b);
  const double hi = std::max(la, lb), lo = std::min(la, lb);
  return (hi + 0.05) / (lo + 0.05);
}

double apca_contrast(Lin text, Lin bg) {
  // SAPC-4g (APCA 0.1.9): luminance with the APCA coefficients and exponent, a soft
  // black clamp, then the polarity-specific powers.
  auto y = [](Lin c) {
    auto ch = [](double v) { return std::pow(std::clamp(linear_channel_to_srgb(v), 0.0, 1.0), 2.4); };
    return 0.2126729 * ch(c.r) + 0.7151522 * ch(c.g) + 0.0721750 * ch(c.b);
  };
  auto clamp_black = [](double Y) { return Y < 0.022 ? Y + std::pow(0.022 - Y, 1.414) : Y; };
  const double ytxt = clamp_black(y(text)), ybg = clamp_black(y(bg));
  if (std::fabs(ybg - ytxt) < 0.0005) return 0.0;
  double sapc;
  if (ybg > ytxt) {
    sapc = (std::pow(ybg, 0.56) - std::pow(ytxt, 0.57)) * 1.14;
    return sapc < 0.1 ? 0.0 : (sapc - 0.027) * 100.0;
  }
  sapc = (std::pow(ybg, 0.65) - std::pow(ytxt, 0.62)) * 1.14;
  return sapc > -0.1 ? 0.0 : (sapc + 0.027) * 100.0;
}

double delta_e(OkLab a, OkLab b) {
  const double dl = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
  return std::sqrt(dl * dl + da * da + db * db);
}

std::string_view cvd_name(Cvd c) {
  switch (c) {
    case Cvd::Protanopia: return "protanopia";
    case Cvd::Deuteranopia: return "deuteranopia";
    case Cvd::Tritanopia: return "tritanopia";
  }
  return "";
}

Lin simulate_cvd(Lin l, Cvd type) {
  // Machado, Oliveira & Fernandes 2009, severity 1.0, in linear RGB. Rows sum to 1, so
  // a grey stays that grey (asserted).
  static constexpr double kProtan[3][3] = {{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}};
  static constexpr double kDeutan[3][3] = {{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}};
  static constexpr double kTritan[3][3] = {{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}};
  const double (*m)[3] = type == Cvd::Protanopia ? kProtan : type == Cvd::Deuteranopia ? kDeutan : kTritan;
  Lin o;
  o.r = std::clamp(m[0][0] * l.r + m[0][1] * l.g + m[0][2] * l.b, 0.0, 1.0);
  o.g = std::clamp(m[1][0] * l.r + m[1][1] * l.g + m[1][2] * l.b, 0.0, 1.0);
  o.b = std::clamp(m[2][0] * l.r + m[2][1] * l.g + m[2][2] * l.b, 0.0, 1.0);
  return o;
}

// ---- the report --------------------------------------------------------------------------

namespace {

// Roles whose foreground is not text drawn for reading: frames, rules, fills.
bool is_text_role(Role r) {
  switch (r) {
    case Role::background: case Role::panel_background: case Role::border: case Role::border_active:
    case Role::md_rule: case Role::md_table_border: case Role::overlay: case Role::selection: case Role::scroll_marker:
    case Role::input_cursor:
      return false;
    default:
      return true;
  }
}

Color effective_bg(const Theme& t, Role r) {
  const Color own = t.style(r).bg;
  if (own.kind != Color::Kind::None) return own;
  return t.style(Role::background).bg;
}

bool attrs_differ(const Style& a, const Style& b) {
  return a.bold != b.bold || a.italic != b.italic || a.underline != b.underline || a.dim != b.dim || a.reverse != b.reverse;
}

// An OKLCH colour brought into the sRGB gamut by pulling its chroma in — never by
// clamping channels, which would move its lightness (the thing the fixes rely on).
Lin into_gamut(OkLch c) {
  for (int k = 0; k < 40; ++k) {
    const Lin lin = oklab_to_linear(oklch_to_oklab(c));
    if (lin.r >= -1e-6 && lin.g >= -1e-6 && lin.b >= -1e-6 && lin.r <= 1 + 1e-6 && lin.g <= 1 + 1e-6 && lin.b <= 1 + 1e-6)
      return {std::clamp(lin.r, 0.0, 1.0), std::clamp(lin.g, 0.0, 1.0), std::clamp(lin.b, 0.0, 1.0)};
    c.C *= 0.9;
  }
  const Lin lin = oklab_to_linear(oklch_to_oklab({c.L, 0, c.h}));
  return {std::clamp(lin.r, 0.0, 1.0), std::clamp(lin.g, 0.0, 1.0), std::clamp(lin.b, 0.0, 1.0)};
}

std::string fmt(double v, int digits = 2) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

}  // namespace

ThemeReport analyse(const Theme& theme) {
  ThemeReport rep;
  bool all_none = true;
  for (const Style& s : theme.styles) all_none &= s.fg.kind == Color::Kind::None && s.bg.kind == Color::Kind::None;
  rep.badges.mono = all_none;
  rep.badges.transparent = theme.style(Role::background).bg.kind == Color::Kind::None;
  if (std::optional<Lin> bg = to_linear(theme.style(Role::background).bg)) {
    const double y = relative_luminance(*bg);
    rep.badges.dark = y <= 0.5;
    rep.badges.light = y > 0.5;
  } else {
    rep.notes.push_back("background is the terminal's own colour: dark/light and every contrast against it depend on the terminal");
  }
  bool readable = true, high = true, any_text_known = false;
  for (std::size_t i = 0; i < kRoleCount; ++i) {
    const Role r = static_cast<Role>(i);
    RoleCheck c;
    c.role = r;
    c.text = is_text_role(r);
    c.fg = theme.style(r).fg;
    c.bg = effective_bg(theme, r);
    const std::optional<Lin> f = to_linear(c.fg), b = to_linear(c.bg);
    if (f && b) {
      c.wcag = wcag_contrast(*f, *b);
      c.apca = apca_contrast(*f, *b);
      c.readable = *c.wcag >= kReadableRatio;
      c.high = *c.wcag >= kHighContrastRatio;
      if (c.text) { any_text_known = true; readable &= c.readable; high &= c.high; }
    } else {
      c.unknown = true;
      if (c.text) { readable = false; high = false; }
    }
    rep.roles.push_back(c);
  }
  rep.badges.readable = any_text_known && readable;
  rep.badges.high_contrast = any_text_known && high;
  if (!any_text_known && !all_none) rep.notes.push_back("no text role has both colours known; readable / high-contrast cannot be awarded");
  bool distinct = true, cvd_all = true, per[3] = {true, true, true}, redundant = true, s16 = true, s256 = true, any_pair_known = false;
  for (const RolePair& p : kMustDiffer) {
    PairCheck c;
    c.a = p.a;
    c.b = p.b;
    const Style& sa = theme.style(p.a);
    const Style& sb = theme.style(p.b);
    c.attribute_redundant = attrs_differ(sa, sb);
    redundant &= c.attribute_redundant;
    const std::optional<Lin> fa = to_linear(sa.fg), fb = to_linear(sb.fg);
    if (fa && fb) {
      any_pair_known = true;
      c.delta = delta_e(linear_to_oklab(*fa), linear_to_oklab(*fb));
      c.distinct = *c.delta >= kDistinctDeltaE;
      c.cvd_distinct = true;
      for (std::size_t k = 0; k < 3; ++k) {
        c.delta_cvd[k] = delta_e(linear_to_oklab(simulate_cvd(*fa, kCvdTypes[k])), linear_to_oklab(simulate_cvd(*fb, kCvdTypes[k])));
        const bool ok = *c.delta_cvd[k] >= kDistinctDeltaE;
        c.cvd_distinct &= ok;
        per[k] &= ok;
      }
      distinct &= c.distinct;
      cvd_all &= c.distinct && c.cvd_distinct;
      c.collapses_16 = downgrade(sa.fg, ColorDepth::Ansi16) == downgrade(sb.fg, ColorDepth::Ansi16);
      c.collapses_256 = downgrade(sa.fg, ColorDepth::Ansi256) == downgrade(sb.fg, ColorDepth::Ansi256);
      s16 &= !c.collapses_16;
      s256 &= !c.collapses_256;
    } else {
      c.unknown = true;
      distinct = cvd_all = false;
      per[0] = per[1] = per[2] = false;
      if (!all_none) rep.notes.push_back(std::string(role_name(p.a)) + " / " + std::string(role_name(p.b)) + ": a foreground is the terminal's own; distinguishability depends on the terminal");
    }
    rep.pairs.push_back(c);
  }
  rep.badges.cvd_safe = any_pair_known && cvd_all;
  rep.badges.protan_safe = any_pair_known && per[0];
  rep.badges.deutan_safe = any_pair_known && per[1];
  rep.badges.tritan_safe = any_pair_known && per[2];
  rep.badges.attribute_redundant = redundant;
  rep.badges.safe_16 = any_pair_known && s16;
  rep.badges.safe_256 = any_pair_known && s256;
  return rep;
}

std::vector<std::string> badge_names(const Badges& b) {
  std::vector<std::string> out;
  if (b.dark) out.push_back("dark");
  if (b.light) out.push_back("light");
  if (b.high_contrast) out.push_back("high-contrast");
  if (b.readable) out.push_back("readable");
  if (b.cvd_safe) out.push_back("cvd-safe");
  if (b.protan_safe) out.push_back("protan-safe");
  if (b.deutan_safe) out.push_back("deutan-safe");
  if (b.tritan_safe) out.push_back("tritan-safe");
  if (b.mono) out.push_back("mono");
  if (b.safe_16) out.push_back("16-safe");
  if (b.safe_256) out.push_back("256-safe");
  if (b.transparent) out.push_back("transparent");
  if (b.attribute_redundant) out.push_back("attribute-redundant");
  return out;
}

bool has_badge(const Badges& b, std::string_view name) {
  for (const std::string& n : badge_names(b))
    if (n == name) return true;
  return false;
}

std::string report_text(const ThemeReport& r) {
  std::string s = "badges:";
  for (const std::string& b : badge_names(r.badges)) s += " " + b;
  if (badge_names(r.badges).empty()) s += " (none)";
  s += "\n";
  for (const std::string& n : r.notes) s += "note: " + n + "\n";
  s += "\nroles (fg on bg; WCAG " + fmt(kReadableRatio, 1) + " readable, " + fmt(kHighContrastRatio, 1) + " high; APCA Lc):\n";
  for (const RoleCheck& c : r.roles) {
    if (!c.text) continue;
    s += "  " + std::string(role_name(c.role));
    s.append(std::max(0, 20 - static_cast<int>(role_name(c.role).size())), ' ');
    if (c.unknown) s += " depends on the terminal (" + color_to_string(c.fg) + " on " + color_to_string(c.bg) + ")\n";
    else s += " " + fmt(*c.wcag) + ":1  APCA " + fmt(*c.apca, 0) + "  " + (c.high ? "high" : c.readable ? "readable" : "FAIL") + "  (" + color_to_string(c.fg) + " on " + color_to_string(c.bg) + ")\n";
  }
  s += "\nmust-differ pairs (OKLab dE >= " + fmt(kDistinctDeltaE) + ", normal / protan / deutan / tritan):\n";
  for (const PairCheck& c : r.pairs) {
    s += "  " + std::string(role_name(c.a)) + " / " + std::string(role_name(c.b)) + ": ";
    if (c.unknown) s += "depends on the terminal";
    else {
      s += fmt(*c.delta) + " / " + fmt(*c.delta_cvd[0]) + " / " + fmt(*c.delta_cvd[1]) + " / " + fmt(*c.delta_cvd[2]);
      s += c.distinct && c.cvd_distinct ? "  ok" : c.distinct ? "  CONFUSABLE under a deficiency" : "  CONFUSABLE";
      if (c.collapses_16) s += "  (collapses at 16 colours)";
      else if (c.collapses_256) s += "  (collapses at 256 colours)";
    }
    s += c.attribute_redundant ? "  +attr\n" : "\n";
  }
  return s;
}

std::vector<std::string> check_claims(const Theme& theme, const ThemeReport& report) {
  std::vector<std::string> failed;
  const json::Value& claims = theme.meta.get("badges");
  if (!claims.is_array()) return failed;
  for (const json::Value& c : claims.arr)
    if (c.is_string() && !has_badge(report.badges, c.str)) failed.push_back(c.str);
  return failed;
}

// ---- auto-fix ---------------------------------------------------------------------------

std::optional<Fix> fix_contrast(const Theme& theme, Role role, double target) {
  const Style before = theme.style(role);
  const std::optional<Lin> f = to_linear(before.fg), b = to_linear(effective_bg(theme, role));
  if (!f || !b) return std::nullopt;
  const double have = wcag_contrast(*f, *b);
  if (have >= target) return std::nullopt;
  // Move L away from the background's L, keeping hue and chroma; chroma is reduced
  // only when the target L cannot be reached in gamut.
  const OkLch bgc = oklab_to_oklch(linear_to_oklab(*b));
  OkLch c = oklab_to_oklch(linear_to_oklab(*f));
  const double dir = c.L >= bgc.L ? 1.0 : -1.0;
  Style after = before;
  double best = have;
  for (int step = 1; step <= 100; ++step) {
    OkLch t = c;
    t.L = std::clamp(c.L + dir * 0.01 * step, 0.0, 1.0);
    const Lin lin = into_gamut(t);
    const double ratio = wcag_contrast(lin, *b);
    if (ratio > best) { best = ratio; after.fg = from_linear(lin); }
    if (ratio >= target) break;
    if (t.L <= 0.0 || t.L >= 1.0) break;
  }
  if (best <= have) return std::nullopt;
  Fix fix{role, before, after, std::string(role_name(role)) + " fg: contrast " + fmt(have, 1) + " \xE2\x86\x92 " + fmt(best, 1) + ":1", have, best};
  return fix;
}

std::optional<Fix> fix_confusable(const Theme& theme, Role a, Role b) {
  const Style sa = theme.style(a), sb = theme.style(b);
  const std::optional<Lin> fa = to_linear(sa.fg), fb = to_linear(sb.fg);
  if (!fa || !fb) return std::nullopt;
  auto min_delta = [&](Lin x, Lin y) {
    double m = delta_e(linear_to_oklab(x), linear_to_oklab(y));
    for (Cvd t : kCvdTypes) m = std::min(m, delta_e(linear_to_oklab(simulate_cvd(x, t)), linear_to_oklab(simulate_cvd(y, t))));
    return m;
  };
  const double have = min_delta(*fa, *fb);
  if (have >= kDistinctDeltaE) return std::nullopt;
  // Rotate b's hue in 30° steps; keep the first rotation that passes every simulation,
  // else the best one found; if none passes, add attribute redundancy instead.
  OkLch c = oklab_to_oklch(linear_to_oklab(*fb));
  double best = have;
  Style after = sb;
  bool passed = false;
  for (int step = 1; step < 12; ++step) {
    OkLch t = c;
    t.h = std::fmod(c.h + 30.0 * step, 360.0);
    const Lin lin = into_gamut(t);
    const double d = min_delta(*fa, lin);
    if (d > best) { best = d; after.fg = from_linear(lin); }
    if (d >= kDistinctDeltaE) { passed = true; break; }
  }
  if (!passed) {
    after = sb;
    if (!sb.underline && !sa.underline) after.underline = true;
    else if (!sb.bold && !sa.bold) after.bold = true;
    else if (!sb.italic && !sa.italic) after.italic = true;
    else return std::nullopt;
    Fix fix{b, sb, after, std::string(role_name(a)) + " / " + std::string(role_name(b)) + ": no hue satisfies every deficiency (best dE " + fmt(best) + "); add " +
                          (after.underline != sb.underline ? "underline" : after.bold != sb.bold ? "bold" : "italic") + " to " + std::string(role_name(b)),
            have, best};
    return fix;
  }
  Fix fix{b, sb, after, std::string(role_name(a)) + " / " + std::string(role_name(b)) + ": min dE " + fmt(have) + " \xE2\x86\x92 " + fmt(best) + " (hue of " + std::string(role_name(b)) + " rotated)", have, best};
  return fix;
}

std::vector<Fix> propose_fixes(const Theme& theme) {
  std::vector<Fix> out;
  const ThemeReport rep = analyse(theme);
  for (const RoleCheck& c : rep.roles)
    if (c.text && !c.unknown && !c.readable)
      if (std::optional<Fix> f = fix_contrast(theme, c.role)) out.push_back(*f);
  // A pair already told apart by an attribute is not proposed again: attribute
  // redundancy is the fix of last resort and, once applied, the pair is handled.
  for (const PairCheck& c : rep.pairs)
    if (!c.unknown && !(c.distinct && c.cvd_distinct) && !c.attribute_redundant)
      if (std::optional<Fix> f = fix_confusable(theme, c.a, c.b)) out.push_back(*f);
  return out;
}

Theme apply_fix(Theme theme, const Fix& fix) {
  theme.style(fix.role) = fix.after;
  return theme;
}

}  // namespace rolltui
