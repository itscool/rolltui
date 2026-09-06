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
#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"
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

/* A BORROW of a string literal; never NULL, `*len` 0 for an out-of-range type. `len` may
 * be NULL. */
const char* rolltui_cvd_name(unsigned char type, size_t* len);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
