//
// theme_analysis_test.cpp — the colour maths against published reference values
// (milestone 15): sRGB linearisation, OKLab of sRGB red / white / black and the round
// trip, WCAG's own examples, APCA's canonical black-on-white / white-on-black, the
// Machado matrices (rows sum to 1 so grey is preserved; pure red loses its red under
// protanopia), ΔE; then the report on the built-ins (their badges asserted, as the plan
// requires), the unknown-colour rule, claimed badges, and auto-fix on a deliberately
// broken pair.
//
#include <cmath>
#include <string>

#include "rolltui/ThemeAnalysis.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : " ") + x;
  return s;
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
  return report("rolltui theme_analysis_test");
}
