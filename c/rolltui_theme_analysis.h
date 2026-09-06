#ifndef ROLLTUI_C_THEME_ANALYSIS_H
#define ROLLTUI_C_THEME_ANALYSIS_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_theme_analysis.h — THE COLOUR MATHS, as C (Phase 17 m1).
 *
 * `rolltui/ThemeAnalysis.hpp` WAS "is this theme readable, and by whom?" in full: colour
 * spaces, WCAG/APCA contrast, colour-vision-deficiency simulation, a per-role/per-pair
 * REPORT, and auto-fix PROPOSALS. It is deleted (Phase 17 m2c) and this file is the whole of
 * it. The first half — sRGB/linear/OKLab/OKLCH conversion, the two contrast formulas, ΔE, and
 * the Machado CVD matrices — has NO Theme, Role or JSON in it. Every formula's reference and
 * every reference value is in the .c file next to the code, and
 * `rolltui/tests/theme_analysis_test.cpp` is the oracle for both halves.
 *
 * THE REPORT AND THE AUTO-FIX ARE HERE NOW TOO (moved from `rolltui/ThemeAnalysis.cpp`,
 * Phase 17 m5): `Theme`'s styles table (`rolltui_theme_style`/`_set_style`) and `json::Value`
 * (`RolltuiJsonValue`, `rolltui_json.h`) both got C representations after this file's original
 * note above was written, which is what made the move possible — see this header's second
 * section comment below for the full reasoning and for what deliberately still does NOT move
 * (`analyse`'s notes, `generate`'s broken list, and anything that would need `Theme`, `Role`'s
 * name table beyond a handed-in vocab, or `std::string`/`std::vector` themselves).
 *
 * THE BOUNDARY'S RULES, inherited from Phase 14/15 and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER.** The colour maths above has none to own: every
 *      result is a handful of doubles or a `RolltuiStyleColor`, so every one of them is an
 *      OUT-PARAM rather than a measure-then-fill round trip. The report/auto-fix below DOES
 *      allocate (a diagnostic list has no bound) — CALLER-FILLED fixed arrays for the
 *      per-role/per-pair checks (the count is known upfront), GROWING AMORTISED
 *      (`rolltui_alloc.h`) for the variable-length parts (`RolltuiStrArray`,
 *      `RolltuiFixArray`), through this file's own release functions.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function except a plain
 *      `double`/`int` (never storage) and a `const char*` BORROW of a string literal that
 *      owns nothing and outlives every caller.
 *   3. **ONE DEFINITION**: `RolltuiLin`/`RolltuiOkLab`/`RolltuiOkLch` are defined once,
 *      here, the same way `RolltuiStyleColor` is in `rolltui_style.h` — `rolltui::Lin` and
 *      its two siblings are `using` aliases below, so a caller on either side of the
 *      boundary still writes `Lin{...}` / `lin.r` exactly as before. `RolltuiBadges` joins
 *      them the same way (`rolltui::Badges` is now `using Badges = RolltuiBadges;`) —
 *      it is thirteen plain flags, no `Role`, no `std::string`, nothing that keeps it from
 *      being one definition. `RolltuiRoleCheck`/`RolltuiPairCheck`/`RolltuiFix` carry role
 *      ORDINALS as `unsigned char` — `ROLLTUI_ROLE_*` values, the role vocabulary having been
 *      C since Phase 17 m2a (`ROLLTUI_ROLE_LIST`, rolltui_style.h). A C++ consumer that wants
 *      to read `c.role == Role::warning` mirrors the three structs over the same calls
 *      (`theme_analysis_test.cpp` does); this file hands over numbers and never a type.
 *   4. **NOTHING ALLOCATES** in the colour-maths half; the report/auto-fix half's
 *      allocations are named above and are the only ones in this file.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif
/* An OKLCH colour brought into the sRGB gamut by pulling its chroma in — never by clamping
 * channels, which would move its lightness (the thing the auto-fix below relies on). Shared
 * by this file's own fixes and by `rolltui_theme_gen.c`'s hue/lightness picks: the C++ had
 * two copies of this exact algorithm (`ThemeAnalysis.cpp`'s `into_gamut`, `ThemeGen.cpp`'s
 * `okl`) and one is enough here. */
void rolltui_into_gamut(RolltuiOkLch c, RolltuiLin* out);


/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
/* ---- sRGB <-> linear ------------------------------------------------------------------- */
double rolltui_srgb_channel_to_linear(double c); /* c in 0..1 */
double rolltui_linear_channel_to_srgb(double v);
void rolltui_from_linear(RolltuiLin l, RolltuiStyleColor* out);
/* ---- linear <-> OKLab <-> OKLCH (Björn Ottosson, 2020) --------------------------------- */
void rolltui_linear_to_oklab(RolltuiLin l, RolltuiOkLab* out);
void rolltui_oklab_to_linear(RolltuiOkLab lab, RolltuiLin* out);
void rolltui_oklab_to_oklch(RolltuiOkLab lab, RolltuiOkLch* out);
void rolltui_oklch_to_oklab(RolltuiOkLch lch, RolltuiOkLab* out);
/* ---- contrast and distance -------------------------------------------------------------- */
double rolltui_relative_luminance(RolltuiLin l);       /* Y, Rec. 709 weights */
double rolltui_wcag_contrast(RolltuiLin a, RolltuiLin b);   /* (L1 + 0.05) / (L2 + 0.05), >= 1 */
double rolltui_apca_contrast(RolltuiLin text, RolltuiLin bg); /* Lc, signed */
double rolltui_delta_e(RolltuiOkLab a, RolltuiOkLab b);     /* Euclidean in OKLab */
/* Machado, Oliveira & Fernandes 2009, severity 1.0. `type` one of the ROLLTUI_CVD_* above;
 * an out-of-range value is treated as Tritanopia, exactly as the original's `? : ?:` chain
 * fell through — see the .c file. */
void rolltui_simulate_cvd(RolltuiLin l, unsigned char type, RolltuiLin* out);
int rolltui_has_badge(const RolltuiBadges* b, const char* name, size_t len);
void rolltui_fix_release(RolltuiFix* f);
/* Returns 1 and fills `*out` (RESET first) when `role`'s own fg is below `target` against
 * its effective background (its own bg, else the theme's `background` role) and moving its
 * OKLCH lightness away from the background's reaches a higher contrast; 0 (`*out` left
 * RESET) when either colour is the terminal's own, or the role already meets `target`.
 * Mirrors `rolltui::fix_contrast` exactly — same 100-step search, same gamut clamp. */
int rolltui_fix_contrast(const RolltuiStyle* styles, size_t role_count, unsigned char role, double target,
                         const RolltuiThemeVocab* vocab, RolltuiFix* out);
/* Same shape, for a confusable PAIR: rotates `b`'s hue in twelve 30-degree steps looking for
 * one whose OKLab ΔE against `a` clears `ROLLTUI_DISTINCT_DELTA_E` under every CVD
 * simulation too; keeps the best rotation found when none fully passes; when NO rotation
 * helps at all, proposes adding underline, else bold, else italic to `b` instead (0 when
 * even that is unavailable, or either foreground is the terminal's own). Mirrors
 * `rolltui::fix_confusable` exactly. */
int rolltui_fix_confusable(const RolltuiStyle* styles, size_t role_count, unsigned char a, unsigned char b,
                           const RolltuiThemeVocab* vocab, RolltuiFix* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_ANALYSIS_H */
