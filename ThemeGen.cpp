// rolltui/ThemeGen.cpp — see ThemeGen.hpp.
#include "rolltui/ThemeGen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "rolltui/ThemeAnalysis.hpp"

namespace rolltui {

std::uint64_t Rng::next() {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

double Rng::unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

std::string_view ruleset_name(Ruleset r) {
  switch (r) {
    case Ruleset::Analogous: return "analogous";
    case Ruleset::Complementary: return "complementary";
    case Ruleset::Triadic: return "triadic";
    case Ruleset::Tetradic: return "tetradic";
    case Ruleset::Monochrome: return "monochrome";
    case Ruleset::Pastel: return "pastel";
    case Ruleset::Neon: return "neon";
    case Ruleset::Earth: return "earth";
  }
  return "";
}

std::optional<Ruleset> ruleset_from_name(std::string_view name) {
  for (Ruleset r : kRulesets)
    if (ruleset_name(r) == name) return r;
  return std::nullopt;
}

namespace {

double wrap_hue(double h) {
  h = std::fmod(h, 360.0);
  return h < 0 ? h + 360.0 : h;
}

// An OKLCH colour clamped into the sRGB gamut by pulling chroma in.
Color okl(double L, double C, double h) {
  L = std::clamp(L, 0.0, 1.0);
  for (int k = 0; k < 40; ++k) {
    const Lin lin = oklab_to_linear(oklch_to_oklab({L, C, wrap_hue(h)}));
    if (lin.r >= -1e-6 && lin.g >= -1e-6 && lin.b >= -1e-6 && lin.r <= 1 + 1e-6 && lin.g <= 1 + 1e-6 && lin.b <= 1 + 1e-6)
      return from_linear({std::clamp(lin.r, 0.0, 1.0), std::clamp(lin.g, 0.0, 1.0), std::clamp(lin.b, 0.0, 1.0)});
    C *= 0.9;
  }
  return from_linear(oklab_to_linear(oklch_to_oklab({L, 0, h})));
}

Style S(Color fg, Color bg, bool bold = false, bool italic = false, bool underline = false, bool dim = false, bool reverse = false) {
  Style s;
  s.fg = fg; s.bg = bg; s.bold = bold; s.italic = italic; s.underline = underline; s.dim = dim; s.reverse = reverse;
  return s;
}

}  // namespace

Generated generate(std::uint64_t seed, Ruleset ruleset, double chaos, const GenOptions& opts) {
  chaos = std::clamp(chaos, 0.0, 1.0);
  Rng rng(seed ^ (static_cast<std::uint64_t>(ruleset) << 56) ^ static_cast<std::uint64_t>(chaos * 1000));
  auto jitter = [&](double width) { return (rng.unit() * 2.0 - 1.0) * width * chaos; };
  auto chance = [&](double p) { return rng.unit() < p * chaos; };

  const bool dark = opts.dark ? *opts.dark : rng.unit() < 0.6;
  const double base_hue = rng.unit() * 360.0;

  // ---- hues per ruleset (four accents + the semantic hues) ----
  double acc[4];
  double chroma = 0.13, ink_chroma = 0.01, ground_chroma = 0.01;
  switch (ruleset) {
    case Ruleset::Analogous: for (int i = 0; i < 4; ++i) acc[i] = base_hue + 30.0 * i; break;
    case Ruleset::Complementary: acc[0] = base_hue; acc[1] = base_hue + 180; acc[2] = base_hue + 25; acc[3] = base_hue + 205; break;
    case Ruleset::Triadic: acc[0] = base_hue; acc[1] = base_hue + 120; acc[2] = base_hue + 240; acc[3] = base_hue + 60; break;
    case Ruleset::Tetradic: for (int i = 0; i < 4; ++i) acc[i] = base_hue + 90.0 * i; break;
    case Ruleset::Monochrome: for (int i = 0; i < 4; ++i) acc[i] = base_hue; chroma = 0.10; ground_chroma = 0.02; break;
    case Ruleset::Pastel: for (int i = 0; i < 4; ++i) acc[i] = base_hue + 72.0 * i; chroma = 0.07; break;
    case Ruleset::Neon: for (int i = 0; i < 4; ++i) acc[i] = base_hue + 90.0 * i + 45; chroma = 0.22; ground_chroma = 0.0; break;
    case Ruleset::Earth: acc[0] = 60; acc[1] = 40; acc[2] = 90; acc[3] = 25; chroma = 0.08; ground_chroma = 0.015; ink_chroma = 0.02; break;
  }
  for (double& h : acc) h = wrap_hue(h + jitter(40.0));
  if (chance(0.5)) std::swap(acc[1], acc[2]);  // a rule ignored: two accents swapped
  chroma = std::max(0.02, chroma + jitter(0.08));

  // ---- lightness ladder: ground, panel, ink, accents spread so they stay distinct under CVD ----
  const double ground_L = dark ? 0.18 + jitter(0.06) : 0.97 + jitter(0.02);
  const double panel_L = dark ? ground_L + 0.04 : ground_L - 0.04;
  const double ink_L = dark ? 0.90 + jitter(0.05) : 0.22 + jitter(0.05);
  const double muted_L = dark ? 0.66 + jitter(0.08) : 0.44 + jitter(0.08);
  const double border_L = dark ? 0.38 : 0.80;
  // The accents' lightness steps are at least 0.09 apart: lightness survives every CVD
  // simulation, so this alone keeps the pairs distinct (dE >= 0.08) even for monochrome,
  // where hue cannot separate them; the rulesets then add hue on top.
  const double acc_L[4] = {dark ? 0.78 : 0.42, dark ? 0.88 : 0.33, dark ? 0.69 : 0.51, dark ? 0.60 : 0.24};
  const Color bg = okl(ground_L, ground_chroma, base_hue), panel = okl(panel_L, ground_chroma, base_hue);
  const Color fg = okl(ink_L, ink_chroma, base_hue), muted = okl(muted_L, ink_chroma, base_hue), border = okl(border_L, ground_chroma, base_hue);
  Color a[4];
  for (int i = 0; i < 4; ++i) a[i] = okl(acc_L[i] + jitter(0.10), chroma, acc[i]);
  // The semantic pairs the analysis checks (diff_added/diff_removed, warning/error) use
  // green/red and yellow/red: keep those apart in lightness too.
  const Color warning = okl(dark ? 0.80 : 0.46, chroma, wrap_hue(85 + jitter(30))), error = okl(dark ? 0.66 : 0.40, chroma + 0.03, wrap_hue(25 + jitter(20)));
  const Color green = okl(dark ? 0.84 : 0.40, chroma, wrap_hue(145 + jitter(20))), cyan = okl(dark ? 0.80 : 0.42, chroma, wrap_hue(200 + jitter(20)));
  const Color code_bg = okl(dark ? ground_L + 0.03 : ground_L - 0.03, ground_chroma, base_hue), sel = okl(dark ? 0.35 : 0.85, 0.05, acc[0]);
  // The find wash sits at the SELECTION's lightness on the third accent's hue, so a
  // match and a selection read as two different highlights rather than one repeated.
  const Color find_wash = okl(dark ? 0.35 : 0.85, 0.06, acc[2]);

  Theme t;
  char name[96];
  std::snprintf(name, sizeof name, "gen-%s-%llu-%.2f", std::string(ruleset_name(ruleset)).c_str(), static_cast<unsigned long long>(seed), chaos);
  t.name = name;
  auto set = [&](Role r, Style s) { t.style(r) = s; };
  const bool rb = chance(0.5), ri = chance(0.5), ru = chance(0.4);  // random attributes under chaos
  set(Role::text, S(fg, bg));
  set(Role::text_muted, S(muted, bg));
  set(Role::background, S(fg, bg));
  set(Role::panel_background, S(fg, panel));
  set(Role::border, S(border, bg));
  set(Role::border_active, S(a[0], bg));
  set(Role::title, S(fg, bg, true));
  set(Role::label, S(muted, panel));
  set(Role::value, S(fg, panel));
  set(Role::accent_1, S(a[0], bg, rb));
  set(Role::accent_2, S(a[1], bg, false, ri));
  set(Role::accent_3, S(a[2], bg, false, false, ru));
  set(Role::accent_4, S(a[3], bg));
  set(Role::prompt, S(cyan, bg, true));
  set(Role::note, S(muted, bg, false, true));
  set(Role::warning, S(warning, bg));
  set(Role::error, S(error, bg, true));
  set(Role::md_heading, S(a[0], bg, true));
  set(Role::md_emphasis, S(fg, bg, false, true));
  set(Role::md_strong, S(fg, bg, true));
  set(Role::md_code_inline, S(a[2], code_bg));
  set(Role::md_code_block, S(fg, code_bg));
  set(Role::md_code_label, S(muted, bg));
  set(Role::md_link, S(cyan, bg, false, false, true));
  set(Role::md_link_url, S(muted, bg));
  set(Role::md_quote, S(muted, bg, false, true));
  set(Role::md_list_marker, S(a[0], bg));
  set(Role::md_table_border, S(border, bg));
  set(Role::md_table_header, S(fg, bg, true));
  set(Role::md_rule, S(border, bg));
  set(Role::md_strikethrough, S(muted, bg, false, false, false, true));
  set(Role::diff_added, S(green, bg));
  set(Role::diff_removed, S(error, bg));
  set(Role::diff_context, S(muted, bg));
  set(Role::input_text, S(fg, bg));
  set(Role::input_cursor, S(bg, fg));
  set(Role::input_placeholder, S(muted, bg, false, true));
  set(Role::scroll_marker, S(bg, a[2], true));
  set(Role::selection, S(fg, sel));
  set(Role::overlay, S(muted, bg, false, false, false, true));
  set(Role::menu_item, S(fg, panel));
  set(Role::menu_selected, S(bg, a[0], true));
  set(Role::menu_breadcrumb, S(muted, panel));
  set(Role::menu_shortcut, S(a[2], panel));
  // Find (Phase 12 m4), by the same rule the built-ins state: every match is normal text
  // on a dim wash of the accent, the current one is INVERTED on that accent and bold.
  // The inversion is not decoration — Style.hpp's must-differ check reads `fg` only, so a
  // pair distinguished by background alone would measure as identical and this generator
  // would emit a theme that fails its own promise.
  set(Role::find_match, S(fg, find_wash));
  set(Role::find_current, S(bg, a[2], true));
  set(Role::scrollbar, S(muted, bg));

  // ---- the repair loop: fix until the promised badges hold, or give up honestly ----
  Generated out;
  for (int pass = 0; pass < opts.max_repair_passes; ++pass) {
    const std::vector<Fix> fixes = propose_fixes(t);
    if (fixes.empty()) break;
    if (chance(0.7)) break;  // a repair pass skipped: chaos means the badges say what broke
    for (const Fix& f : fixes) {
      if (chance(0.3)) continue;
      t = apply_fix(t, f);
      ++out.repairs;
    }
  }
  const ThemeReport rep = analyse(t);
  out.badges = badge_names(rep.badges);
  for (const RoleCheck& c : rep.roles)
    if (c.text && !c.unknown && !c.readable) out.broken.push_back(std::string(role_name(c.role)) + " contrast " + std::to_string(*c.wcag).substr(0, 4));
  // A colour-confusable pair that differs by an attribute is repaired (the plan's rule:
  // "never rely on colour alone"); it still costs the cvd-safe badge, honestly.
  for (const PairCheck& c : rep.pairs)
    if (!c.unknown && !(c.distinct && c.cvd_distinct) && !c.attribute_redundant)
      out.broken.push_back(std::string(role_name(c.a)) + "/" + std::string(role_name(c.b)) + " confusable");
  json::Value meta = json::Value::object();
  json::Value gen = json::Value::object();
  gen.set("ruleset", json::Value::string(std::string(ruleset_name(ruleset))));
  gen.set("seed", json::Value::number(static_cast<double>(seed)));
  gen.set("chaos", json::Value::number(chaos));
  meta.set("generator", std::move(gen));
  json::Value badges = json::Value::array();
  for (const std::string& b : out.badges) badges.arr.push_back(json::Value::string(b));
  meta.set("badges", std::move(badges));
  t.meta = std::move(meta);
  out.theme = std::move(t);
  return out;
}

}  // namespace rolltui
