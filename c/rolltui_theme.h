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
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The RGB an ANSI index shows in xterm's default palette (0-15 the system colours, 16-231
 * the 6x6x6 cube, 232-255 the grey ramp). */
void rolltui_ansi_index_rgb(unsigned char index, RolltuiStyleColor* out);

void rolltui_theme_report_set_error(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_missing_role(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_unknown_key(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_bad_value(RolltuiThemeReport* r, const char* s, size_t len);


/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
/* Pure colour reduction: TrueColor keeps everything, Ansi256 maps rgb to the nearest of the
 * cube and the grey ramp, Ansi16 to the nearest of the 16 system colours, Mono drops colour. */
void rolltui_color_downgrade(RolltuiStyleColor* c, unsigned char depth);
/* An OSC 11 reply ("\x1b]11;rgb:1414/1616/1a1a\x1b\\" or BEL-terminated; 1-4 hex digits per
 * channel, scaled to 8 bits). 1 on success. */
int rolltui_parse_osc11_reply(const char* reply, size_t len, RolltuiStyleColor* out);
/* ---- the built-in themes ------------------------------------------------------------------
 * "default-dark", "default-light", "mono", compiled in: this library's own TASTE, not its
 * algorithm (`rolltui::Theme.cpp`'s own words, kept). Enumerated by index like every other
 * closed table a sibling file exposes (`rolltui_widget_kind_name`,
 * `rolltui_effect_kind_name`). */
#define ROLLTUI_THEME_BUILTIN_COUNT 3
size_t rolltui_theme_builtin_count(void);
/* A BORROW, valid for the process's life (a compiled-in literal, never freed). NULL past the
 * count. */
const char* rolltui_theme_builtin_name(size_t i);

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
/* BORROWS a static literal; `*len` may be NULL. An out-of-range depth reads back as "mono"
 * and an out-of-range mode as "dark", which is what the C++ `color_depth_name` did and what a
 * zeroed byte means. */
const char* rolltui_color_depth_name(unsigned char depth, size_t* len);

/* "#rrggbb" | "none" | "0".."255". 1 on success, 0 when it is not a colour. */
int rolltui_color_parse(const char* text, size_t len, RolltuiStyleColor* out);

size_t rolltui_color_to_string(RolltuiStyleColor c, char* out, size_t cap);

/* ---- the dumper ----------------------------------------------------------------------------
 * Builds a fresh, OWNED tree (caller frees with `rolltui_json_free`) with a "roles" object
 * and, when anything is marked, an "effects" object — mirrors `style_to_json`+
 * `effect_to_json`+`effects_to_json`+the "roles"/"effects" halves of
 * `theme_to_json_value`/`theme_pair_to_json_value` exactly. Neither "name" nor "meta" is set
 * here (see this header's top comment); the C++ shim pulls "roles"/"effects" out of the
 * result to set them, in order, after both.
 *
 * `light_styles`/`light_effects` NULL together dump ONE variant plainly
 * (`style_to_json(s, nullptr)`); non-NULL dumps BOTH as one object, a role or attribute
 * written as {"dark":..,"light":..} only where the two differ. `light_effects` is accepted
 * but never consulted: motion is the THEME's, not the terminal background's (`Theme.hpp`),
 * so "effects" is always `dark_effects` alone — the same asymmetry
 * `theme_pair_to_json_value` already has by taking a whole light `Theme` and reading only
 * its `.styles`. */
RolltuiJsonValue* rolltui_theme_dump(const RolltuiStyle* dark_styles, const RolltuiEffectMap* dark_effects,
                                     const RolltuiStyle* light_styles, const RolltuiEffectMap* light_effects,
                                     const RolltuiThemeVocab* vocab);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_H */
