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

#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
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

/* ---- the built-in themes, and the JSON theme loader/dumper (Phase 15 m5) ------------------
 *
 * Everything above this line was Phase 15 m3: the colour engine, ~200 lines that were already
 * a binding. What follows is the ~600 lines that were not — the library's own THREE BUILT-IN
 * THEMES (`rolltui::Theme.cpp`'s `make_default_dark`/`_light`/`make_mono`, its taste rather
 * than its algorithm — the reason `theme_test`'s colour-literal grep still finds a literal
 * here and exempts this file BY NAME, same as it already did for the xterm reference
 * palette above) and its FILE FORMAT — `rolltui/Theme.hpp`'s JSON shape, defs references,
 * dark/light colour and attribute pairs, and the "effects" section (Effects.hpp).
 *
 * ---- WHAT THIS FILE STILL DELIBERATELY DOES NOT KNOW, and how that is kept true here -----
 *
 * A Role's NAME and an EffectState's NAME are the SAME vocabulary `rolltui_effects.h` and
 * `rolltui_compose_layer` (rolltui_layout.h) already keep out of C — "text", "md_heading",
 * "waiting" are `rolltui::Style.hpp`/`Effects.hpp` facts, and a vocabulary written down twice
 * is a second thing to drift (this header's own comment, above, makes the identical case for
 * the depth/mode names). Every function below that must resolve a JSON key to a role or a
 * state ordinal — or the reverse, for the dumper — takes a `RolltuiThemeVocab`: the caller's
 * name TABLE, handed over once per call the same way `rolltui_effect_map_new`'s
 * `fallback_role` and `rolltui_bindings_set_enter_rule`'s action name already hand over ONE
 * name — generalised to a whole table because a theme file resolves many. This file reads
 * the table; it never learns what any entry MEANS.
 *
 * A theme's OWN "name" ("default-dark" etc.) is a different thing and is NOT vocabulary in
 * that sense: it is DATA belonging to the theme object itself, with no C++ enum it could
 * drift from, exactly as a widget kind's own library-table names
 * (`rolltui_widget_kind_library_name`) are plain data one file over. That is why
 * `rolltui_theme_builtin_name` below hardcodes it directly, the same way this file already
 * hardcodes "dark"/"light" and the theme file's OWN structural keys ("roles", "defs", "fg",
 * "kind", "frames", ...) — this module's file format is its own vocabulary to own, the same
 * position `rolltui_app_profile.c` already takes for "actions"/"kinds"/"sources".
 *
 * `Theme::meta` (a free-form `json::Value`) and `Theme::name` (`std::string`) stay OUTSIDE
 * this file entirely — the loader fills `out_name` as a plain string and never touches
 * "meta", and the dumper never sets "name"/"meta" on its result — because both are shaped by
 * `rolltui::json::Value`/`std::string`, which `rolltui/c/rolltui_json.h`'s own header comment
 * explains are NOT ported (Theme.cpp is one of the six modules named there). The C++ shim
 * reads/writes both directly off the tree it already holds, before or after calling here.
 *
 * ---- THE BOUNDARY'S RULES, inherited from Phase 14/15 and none new ------------------------
 *   1. CALLER-FILLED styles (`RolltuiStyle* out_styles`, `role_count` long, positioned
 *      exactly as `rolltui::Role`'s declaration order — the same convention
 *      `rolltui_compose_layer`'s own `styles` parameter already uses): no allocation, because
 *      `rolltui::Theme::styles` is a fixed `std::array` member with no heap storage to own.
 *   2. The effects map is OWNED, LONG-LIVED (`RolltuiEffectMap*`, Phase 15 m3's strategy):
 *      returned directly rather than through an out-param, because a pointer is not the
 *      "nothing by value" rule's concern — every `rolltui_effect_map_new`/`_clone` already
 *      returns one the same way.
 *   3. The report (`RolltuiThemeReport`) is transparent, the same shape
 *      `RolltuiAppProfileReport` already is: plain `RolltuiStr` fields and growing arrays of
 *      them, because nothing about a diagnostic list needs hiding.
 *   4. Every allocation goes through `rolltui_alloc.h`'s closed set, named at the call site
 *      in `rolltui_theme.c`.
 */

/* A role or effect-state NAME TABLE, handed to the loader/dumper once per call — see this
 * header's comment above for why a table crosses instead of the vocabulary moving in.
 * `role_names[i]`/`state_names[i]` are NUL-terminated (every string this boundary already
 * hands across is — a parsed JSON string via `RolltuiStr`, and a C++ string literal both
 * are), so nothing here carries a parallel length array: `strlen` is cheap at theme-load
 * rate (never per frame) and a second array is a second thing that could disagree with the
 * first. Every pointer is a BORROW for the one call.
 *
 *   role_names / role_count   `rolltui::Role`'s declaration order (`Style.hpp`); `out_styles`
 *                              below is filled positionally against this SAME order.
 *   text_role                  the ordinal "text" resolves to — the role every other
 *                              inherits from, and the one this file must special-case
 *                              (`ROLLTUI_ROLE_DEFAULT_TEXT` one level up, in `rolltui_style.h`).
 *   state_names / state_count  `EffectState`'s declaration order (`Effects.hpp`); index 0
 *                              ("none") never matches a theme file's "effects" key, the same
 *                              way `rolltui::read_effects` already rejected it.
 *   fallback_effect_role        the role byte an effect spec with none of its own picks —
 *                              handed to `rolltui_effect_map_new` exactly once, here, the
 *                              same value `rolltui::EffectMap`'s default constructor already
 *                              hands it (`Role::accent_1`).
 */
typedef struct RolltuiThemeVocab {
  const char* const* role_names;
  size_t role_count;
  size_t text_role;
  const char* const* state_names;
  size_t state_count;
  unsigned char fallback_effect_role;
} RolltuiThemeVocab;

/* ---- the built-in themes ------------------------------------------------------------------
 * "default-dark", "default-light", "mono", compiled in: this library's own TASTE, not its
 * algorithm (`rolltui::Theme.cpp`'s own words, kept). Enumerated by index like every other
 * closed table a sibling file exposes (`rolltui_widget_kind_library_name`,
 * `rolltui_effect_kind_name`). */
#define ROLLTUI_THEME_BUILTIN_COUNT 3
size_t rolltui_theme_builtin_count(void);
/* A BORROW, valid for the process's life (a compiled-in literal, never freed). NULL past the
 * count. */
const char* rolltui_theme_builtin_name(size_t i);

/* Fills `styles[0..role_count)` (CALLER-FILLED: `styles` is the caller's own table, the
 * `Theme::styles` array itself — no allocation) for the named built-in theme, and returns a
 * freshly built, OWNED effect map the caller adopts (`rolltui::EffectMap`'s adopting
 * constructor) or frees with `rolltui_effect_map_free`. NULL, with `styles` untouched, when
 * the name is unknown OR `role_count` does not match this file's own table (a defensive
 * invariant check: `rolltui::Style.hpp`'s `kRoleCount` and this file's role tables must agree,
 * and disagreement should read as "this theme doesn't exist" rather than write past the end
 * of a caller's array) — mirroring `rolltui::builtin_theme`'s "unknown name -> nullptr". */
RolltuiEffectMap* rolltui_theme_builtin_fill(const char* name, size_t name_len, RolltuiStyle* styles,
                                             size_t role_count);

/* ---- the load report: mirrors rolltui::ThemeLoadReport field for field -------------------- */
typedef struct RolltuiThemeReport {
  RolltuiStr error; /* non-empty: the file was unusable */
  RolltuiStr* missing_roles; size_t missing_roles_n, missing_roles_cap; /* GROWING AMORTISED */
  RolltuiStr* unknown_keys;  size_t unknown_keys_n,  unknown_keys_cap;  /* GROWING AMORTISED */
  RolltuiStr* bad_values;    size_t bad_values_n,    bad_values_cap;    /* GROWING AMORTISED */
} RolltuiThemeReport;

/* Frees everything and zeroes the struct — safe on an already-zeroed one and on repeated
 * calls, the same "reset, not just release" contract `rolltui_app_profile_report_release`
 * states. Zero-initialise a fresh one (`RolltuiThemeReport r = {0};`) before first use. */
void rolltui_theme_report_release(RolltuiThemeReport* r);
void rolltui_theme_report_set_error(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_missing_role(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_unknown_key(RolltuiThemeReport* r, const char* s, size_t len);
void rolltui_theme_report_add_bad_value(RolltuiThemeReport* r, const char* s, size_t len);

/* ---- the loader ----------------------------------------------------------------------------
 * Parses a theme already as a TREE — the caller either parsed the file's text with
 * `rolltui_json_parse` directly, or already held a `json::Value` and converted it
 * (`rolltui::load_theme`'s two overloads do exactly one of these each). Mirrors
 * `rolltui::load_theme(const json::Value&, ThemeMode, ThemeLoadReport&)` exactly, MINUS
 * "meta" and the `Theme` object itself — see this header's top comment for why both stay
 * outside.
 *
 * `report` is RESET by this call (as if freshly zero-initialised) whether it succeeds or
 * fails, the same contract `rolltui_app_profile_parse` states. Returns NULL only when `root`
 * is not a usable theme object at all (`report->error` explains: not a JSON object, or no
 * "roles" object) — `out_styles`/`out_name` are untouched in that case. Every other problem
 * still produces a usable theme: `out_styles[0..vocab->role_count)` is filled in full (the
 * "text" style substituted for any role the file did not define, `report->missing_roles`
 * naming each), `out_name` gets the theme's own "name" ("unnamed" when absent or not a
 * string), and the return is a freshly built, OWNED, non-NULL effect map (empty — a still UI
 * — for a file with no usable "effects" key), with every problem in `report`. */
RolltuiEffectMap* rolltui_theme_load(const RolltuiJsonValue* root, int mode, const RolltuiThemeVocab* vocab,
                                     RolltuiStyle* out_styles, RolltuiStr* out_name, RolltuiThemeReport* report);

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
