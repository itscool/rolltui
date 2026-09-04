#ifndef ROLLTUI_C_THEME_H
#define ROLLTUI_C_THEME_H
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
 * **The DEPTH and MODE names.** `ColorDepth` crosses as a byte and the strings "truecolor",
 * "256", "16", "mono" stay in `Theme.cpp`, for exactly the reason m2 kept the styling roles
 * out of `rolltui_diff.h`: a name a config file and a `--color-depth` flag both spell is a
 * vocabulary, and a vocabulary written down twice is a second thing to drift. So
 * `detect_color_depth` — which is nothing but a comparison against those four names — stays
 * one level up, and this file is handed the answer.
 *
 * **What "none" means to a renderer.** It parses and prints the word because that is this
 * module's own FILE FORMAT, which is the thing being ported; it never decides what a
 * terminal does with it.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Colour depth, as a byte. The same order as `rolltui::ColorDepth`, asserted in Theme.cpp. */
#define ROLLTUI_DEPTH_MONO 0
#define ROLLTUI_DEPTH_ANSI16 1
#define ROLLTUI_DEPTH_ANSI256 2
#define ROLLTUI_DEPTH_TRUECOLOR 3

/* Theme mode, as a byte. The same order as `rolltui::ThemeMode`. */
#define ROLLTUI_MODE_DARK 0
#define ROLLTUI_MODE_LIGHT 1

/* ---- parsing and printing --------------------------------------------------------------- */

/* "#rrggbb" | "none" | "0".."255". 1 on success, 0 when it is not a colour. */
int rolltui_color_parse(const char* text, size_t len, RolltuiStyleColor* out);

/* The colour in the same spelling. "#rrggbb" is the longest, so seven bytes plus nothing —
 * the result is NOT terminated and the length is returned. */
#define ROLLTUI_COLOR_STRING_MAX 8
size_t rolltui_color_to_string(RolltuiStyleColor c, char* out, size_t cap);

/* The RGB an ANSI index shows in xterm's default palette (0-15 the system colours, 16-231
 * the 6x6x6 cube, 232-255 the grey ramp). */
void rolltui_ansi_index_rgb(unsigned char index, RolltuiStyleColor* out);

/* ---- reduction and emission --------------------------------------------------------------- */

/* Pure colour reduction: TrueColor keeps everything, Ansi256 maps rgb to the nearest of the
 * cube and the grey ramp, Ansi16 to the nearest of the 16 system colours, Mono drops colour. */
void rolltui_color_downgrade(RolltuiStyleColor* c, unsigned char depth);

/* The SGR sequence that selects `style` at `depth`, into `out`. Always starts from a reset,
 * so a cell's style never depends on the previous cell's. The longest is
 * "\x1b[0;1;2;3;4;7;38;2;255;255;255;48;2;255;255;255m" — 48 bytes, and the cap is a
 * constraint on this file rather than on a caller's data. */
#define ROLLTUI_SGR_MAX 64
size_t rolltui_sgr(const RolltuiStyle* style, unsigned char depth, char* out, size_t cap);

/* ---- the terminal's background ------------------------------------------------------------ */

/* An OSC 11 reply ("\x1b]11;rgb:1414/1616/1a1a\x1b\\" or BEL-terminated; 1-4 hex digits per
 * channel, scaled to 8 bits). 1 on success. */
int rolltui_parse_osc11_reply(const char* reply, size_t len, RolltuiStyleColor* out);

/* The mode a background implies: relative luminance (sRGB linearised, Rec. 709 weights)
 * above 0.5 is light, anything else — including a colour that is not rgb — is dark. */
unsigned char rolltui_mode_for_background(RolltuiStyleColor bg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_H */
