#ifndef ROLLTUI_C_THEME_H
#define ROLLTUI_C_THEME_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_theme.h — THE COLOUR ENGINE, as C (Phase 15 m3).
 *
 * Parsing and printing a colour in this library's own spelling, reducing one to what a
 * terminal can show, emitting the SGR that selects a style, and reading a terminal's OSC 11
 * answer. Every rule is stated in `rolltui/Theme.hpp` and asserted against published
 * reference values in `rolltui/tests/theme_test.cpp`; none of it is repeated here.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14 and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER**, and every result here has a bound known WITHOUT
 *      asking — a colour prints in at most seven bytes, an SGR sequence in at most
 *      thirty-one — so there is no measure-then-fill round trip anywhere on this file.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function, except the plain
 *      scalars that are not storage.
 *   3. **ONE DEFINITION**: a colour and a style are `rolltui_style.h`'s structs, which is
 *      what lets this file read a theme's own array without converting anything.
 *
 * ---- WHAT THIS FILE DELIBERATELY DOES NOT KNOW -----------------------------------------
 *
 * ~~**The DEPTH and MODE names.**~~ **RETRACTED 2026-09-05 (Phase 17 m2a) — see the mode and
 * depth vocabulary below.** This entry read: *"`ColorDepth` crosses as a byte and the strings
 * "truecolor", "256", "16", "mono" stay in `Theme.cpp` ... a vocabulary written down twice is
 * a second thing to drift. So `detect_color_depth` stays one level up, and this file is handed
 * the answer."* Right while the library was C++ with a C core; wrong once the library IS the
 * C, because `Theme.cpp` is deleted in m2c and the vocabulary would go with it. The rule it
 * cites is the reason it is retracted, not the reason it stood: the names had reached FOUR
 * spellings by the time anyone counted.
 *
 * **What "none" means to a renderer.** It parses and prints the word because that is this
 * module's own FILE FORMAT, which is the thing being ported; it never decides what a
 * terminal does with it.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif
const char* rolltui_theme_mode_name(unsigned char mode, size_t* len);

/* The RGB an ANSI index shows in xterm's default palette (0-15 the system colours, 16-231
 * the 6x6x6 cube, 232-255 the grey ramp). */
void rolltui_ansi_index_rgb(unsigned char index, RolltuiStyleColor* out);

/* Writes `*style` into `styles[role]`. A no-op — not a crash — when either pointer is NULL or
 * `role` is out of range, the same defensive shape `rolltui_theme_builtin_fill` already takes
 * for a mismatched count. */
void rolltui_theme_set_style(RolltuiStyle* styles, size_t role_count, unsigned char role, const RolltuiStyle* style);

void rolltui_theme_report_set_error(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_missing_role(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_unknown_key(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_bad_value(RolltuiThemeReport* r, const char* s, size_t len);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
