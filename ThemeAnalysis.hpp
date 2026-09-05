#pragma once
//
// rolltui/ThemeAnalysis.hpp — is this theme readable, and by whom? (plan/phase-9.md,
// milestone 15). Pure colour mathematics with reference-value tests
// (rolltui/tests/theme_analysis_test.cpp), because a wrong luminance formula or a wrong
// CVD matrix mislabels every theme silently and nothing but a known answer would
// notice:
//
//   sRGB ↔ linear (IEC 61966-2-1), linear ↔ OKLab ↔ OKLCH (Björn Ottosson, 2020);
//   WCAG 2.x contrast ratio (relative luminance, Rec. 709 weights) and APCA (SAPC-4g,
//   the 0.1.9 constants: black on white ≈ +106 Lc, white on black ≈ −108);
//   colour-vision-deficiency simulation — protanopia, deuteranopia, tritanopia — with
//   the Machado, Oliveira & Fernandes (2009) matrices at severity 1.0, applied in
//   LINEAR RGB; distinguishability as OKLab ΔE.
//
// The report is per role (its fg against the bg it is drawn on — its own bg, else the
// theme's background) and per MUST-DIFFER pair (kMustDiffer in Style.hpp), each with the
// number, the threshold and a verdict. A `none` colour is the terminal's own and is
// REPORTED AS UNKNOWN, never assumed — a badge that depends on an unknown pair is not
// awarded and the note says why. Badges are COMPUTED, never declared: a file may claim
// them in "meta": {"badges": [...]} and check_claims() says whether each claim holds.
//
// Thresholds, stated once here: readable = every text role ≥ 4.5:1 (WCAG AA), high
// contrast = ≥ 7:1 (AAA); distinct = OKLab ΔE ≥ 0.08 between the pair's foregrounds,
// under normal vision and under every simulation for cvd-safe; 16-safe / 256-safe =
// no must-differ pair collapses to one colour after `downgrade`.
//
// Auto-fix, as PROPOSALS (never applied silently): for a failing contrast pair, move
// the foreground's OKLCH lightness away from the background's keeping hue and chroma;
// for a confusable pair, rotate the second hue off the confusion axis, and when no
// rotation satisfies every simulation, add attribute redundancy (underline, else
// bold) — the one fix that works for every CVD type. The editor shows the before/after
// numbers and the user commits or cancels like any other change.
//
// PHASE 17 m1: the colour maths above — sRGB/linear/OKLab/OKLCH, both contrast formulas,
// ΔE, the CVD simulation — is now C (rolltui/c/rolltui_theme_analysis.h/.c); every
// function below it is a thin forwarding shim, and `Lin`/`OkLab`/`OkLch` are the C structs
// by alias, not a second definition. THE REPORT AND THE AUTO-FIX DID NOT MOVE: `analyse()`
// walks every `Role` of a `Theme` and reads `theme.meta` (a `json::Value`), and none of
// `Theme`, `Role`'s name table, or `json::Value` has a C representation yet (`Json` is
// deliberately the last of Phase 17's seven modules, and `Theme`/`Style.hpp` are outside
// this milestone). So everything from `RoleCheck` down stays exactly the C++ it was,
// calling the C maths instead of computing it locally — see rolltui_theme_analysis.h's
// own comment for the fuller reasoning.
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Style.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/c/rolltui_theme_analysis.h"

namespace rolltui {

// ONE DEFINITION, in rolltui/c/rolltui_theme_analysis.h, compiled by both languages
// (Phase 17 m1, the same move Phase 14 m2 made for Color/Style). `Lin{...}`, `.r`, `.a`,
// `.h` and every other read below is unchanged at every call site.
using Lin = RolltuiLin;      // linear sRGB, 0..1
using OkLab = RolltuiOkLab;
using OkLch = RolltuiOkLch;  // h in degrees, [0, 360)

double srgb_channel_to_linear(double c);   // c in 0..1
double linear_channel_to_srgb(double v);
std::optional<Lin> to_linear(Color c);      // nullopt for None (the terminal's own)
Color from_linear(Lin l);                   // clamped, encoded to an Rgb Color
OkLab linear_to_oklab(Lin l);
Lin oklab_to_linear(OkLab lab);
OkLch oklab_to_oklch(OkLab lab);
OkLab oklch_to_oklab(OkLch lch);
double relative_luminance(Lin l);           // Y, Rec. 709 weights
double wcag_contrast(Lin a, Lin b);         // (L1 + 0.05) / (L2 + 0.05), ≥ 1
double apca_contrast(Lin text, Lin bg);     // Lc, signed (negative for light text on dark)
double delta_e(OkLab a, OkLab b);           // Euclidean in OKLab

enum class Cvd : std::uint8_t { Protanopia, Deuteranopia, Tritanopia };
inline constexpr Cvd kCvdTypes[] = {Cvd::Protanopia, Cvd::Deuteranopia, Cvd::Tritanopia};
std::string_view cvd_name(Cvd c);
Lin simulate_cvd(Lin l, Cvd type);          // Machado 2009, severity 1.0

// ---- thresholds ----
inline constexpr double kReadableRatio = 4.5;
inline constexpr double kHighContrastRatio = 7.0;
inline constexpr double kDistinctDeltaE = 0.08;

// ---- the report ----
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
  std::optional<double> delta;                   // normal vision
  std::array<std::optional<double>, 3> delta_cvd;  // per kCvdTypes
  bool unknown = false;
  bool distinct = false;             // normal vision ≥ threshold
  bool cvd_distinct = false;         // under every simulation too
  bool attribute_redundant = false;  // also differ by bold/italic/underline/dim/reverse
  bool collapses_16 = false, collapses_256 = false;
};
struct Badges {
  bool dark = false, light = false, high_contrast = false, readable = false, cvd_safe = false;
  bool protan_safe = false, deutan_safe = false, tritan_safe = false;
  bool mono = false, safe_16 = false, safe_256 = false, transparent = false, attribute_redundant = false;
};
struct ThemeReport {
  std::vector<RoleCheck> roles;
  std::vector<PairCheck> pairs;
  Badges badges;
  std::vector<std::string> notes;  // what could not be measured, in words
};

ThemeReport analyse(const Theme& theme);
std::vector<std::string> badge_names(const Badges& b);  // "dark", "readable", ...
bool has_badge(const Badges& b, std::string_view name);
std::string report_text(const ThemeReport& r);          // for --check and the editor's popup
// A file's claimed badges ("meta": {"badges": [...]}) against the computed ones: the
// claims that do NOT hold (empty when all do, or none were claimed).
std::vector<std::string> check_claims(const Theme& theme, const ThemeReport& report);

// ---- auto-fix proposals ----
struct Fix {
  Role role;
  Style before, after;
  std::string what;    // "md_link fg: contrast 3.1 → 4.6"
  double before_value = 0, after_value = 0;
};
std::optional<Fix> fix_contrast(const Theme& theme, Role role, double target = kReadableRatio);
std::optional<Fix> fix_confusable(const Theme& theme, Role a, Role b);
std::vector<Fix> propose_fixes(const Theme& theme);  // every failing role and pair, in report order
Theme apply_fix(Theme theme, const Fix& fix);

}  // namespace rolltui
