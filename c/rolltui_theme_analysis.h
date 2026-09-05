#ifndef ROLLTUI_C_THEME_ANALYSIS_H
#define ROLLTUI_C_THEME_ANALYSIS_H
/*
 * rolltui/c/rolltui_theme_analysis.h — THE COLOUR MATHS, as C (Phase 17 m1).
 *
 * `rolltui/ThemeAnalysis.hpp` is "is this theme readable, and by whom?" in full: colour
 * spaces, WCAG/APCA contrast, colour-vision-deficiency simulation, a per-role/per-pair
 * REPORT, and auto-fix PROPOSALS. That file states the thresholds and the shape of the
 * report; this one carries only the half of it with NO Theme, Role or JSON in it —
 * sRGB/linear/OKLab/OKLCH conversion, the two contrast formulas, ΔE, and the Machado CVD
 * matrices. Every formula's reference and every reference value is in the .c file next to
 * the code now, and `rolltui/tests/theme_analysis_test.cpp` is still the oracle for both.
 *
 * WHY THE REPORT AND THE AUTO-FIX DID NOT MOVE HERE TOO, though they are declared right
 * next to this in `ThemeAnalysis.hpp` and this phase's own milestone says "the seven,
 * ported — so there is no C++ implementation left to bind to": `analyse()` walks every
 * `Role` of a `Theme` and reads `theme.meta` (a `json::Value`), and neither `Theme` nor
 * `Role`'s name table nor `json::Value` has a C representation yet — `Json` is deliberately
 * the LAST of the seven this phase ports, for a sequencing reason stated in
 * `plan/phase-17.md`, and `Theme`/`Style.hpp`'s `Role` are outside this milestone's scope
 * entirely. So `analyse`, `report_text`, `check_claims`, `fix_contrast`, `fix_confusable`,
 * `propose_fixes` and `apply_fix` stay C++ in `ThemeAnalysis.cpp` — there is nothing yet to
 * bind them to — and now call the functions below instead of computing locally. That is
 * the whole of what this port does: the same split Phase 14 measured (`plan/phase-14.md`,
 * the C-vs-C++ line ratio), applied to the one function of the seven that turned out to be
 * two functions' worth of dependency, not one.
 *
 * THE BOUNDARY'S RULES, inherited from Phase 14/15 and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER.** There are none here to own: every result is a
 *      handful of doubles or a `RolltuiStyleColor`, so every one of them is an OUT-PARAM
 *      rather than a measure-then-fill round trip.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function except a plain
 *      `double` (never storage) and a `const char*` BORROW of a string literal that owns
 *      nothing and outlives every caller.
 *   3. **ONE DEFINITION**: `RolltuiLin`/`RolltuiOkLab`/`RolltuiOkLch` are defined once,
 *      here, the same way `RolltuiStyleColor` is in `rolltui_style.h` — `rolltui::Lin` and
 *      its two siblings are `using` aliases below, so a caller on either side of the
 *      boundary still writes `Lin{...}` / `lin.r` exactly as before.
 *   4. **NOTHING ALLOCATES.** Every function here is a value in, a value (or a few
 *      doubles) out; `rolltui_alloc.h`'s closed set has nothing to name because there is
 *      no storage to choose a strategy for.
 */
#include <stddef.h>
#include <stdint.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the three colour spaces, defined ONCE and compiled by both languages ------------- */
/* `rolltui::Lin`, `rolltui::OkLab` and `rolltui::OkLch` ARE these structs (ThemeAnalysis.hpp
 * aliases them), the same move Phase 14 m2 made for `Color`/`Style`. Linear sRGB, OKLab and
 * OKLCH are each three doubles with a defaulted-to-zero value — no methods, because nothing
 * in either language ever called one. */
typedef struct RolltuiLin {
  double r ROLLTUI_DEFAULT(0), g ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* linear sRGB, 0..1 */
} RolltuiLin;

typedef struct RolltuiOkLab {
  double L ROLLTUI_DEFAULT(0), a ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0);
} RolltuiOkLab;

typedef struct RolltuiOkLch {
  double L ROLLTUI_DEFAULT(0), C ROLLTUI_DEFAULT(0), h ROLLTUI_DEFAULT(0); /* h in degrees, [0, 360) */
} RolltuiOkLch;

/* ---- colour-vision-deficiency type, as a byte (the same order as `rolltui::Cvd`) ------- */
#define ROLLTUI_CVD_PROTANOPIA 0
#define ROLLTUI_CVD_DEUTERANOPIA 1
#define ROLLTUI_CVD_TRITANOPIA 2
#define ROLLTUI_CVD_COUNT 3

/* ---- sRGB <-> linear ------------------------------------------------------------------- */
double rolltui_srgb_channel_to_linear(double c); /* c in 0..1 */
double rolltui_linear_channel_to_srgb(double v);

/* 1 on success, 0 for `Color::none()` (the terminal's own colour — unknown, not assumed). */
int rolltui_to_linear(RolltuiStyleColor c, RolltuiLin* out);
/* Clamped, encoded to an Rgb colour. */
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

/* A BORROW of a string literal; never NULL, `*len` 0 for an out-of-range type. `len` may
 * be NULL. */
const char* rolltui_cvd_name(unsigned char type, size_t* len);
/* Machado, Oliveira & Fernandes 2009, severity 1.0. `type` one of the ROLLTUI_CVD_* above;
 * an out-of-range value is treated as Tritanopia, exactly as the original's `? : ?:` chain
 * fell through — see the .c file. */
void rolltui_simulate_cvd(RolltuiLin l, unsigned char type, RolltuiLin* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_ANALYSIS_H */
