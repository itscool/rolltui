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
#include <stddef.h>

#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- THE MODE AND DEPTH VOCABULARY (Phase 17 m2a, 2026-09-05) -----------------------------
 *
 * THIS FILE'S OWN TOP COMMENT SAID IT DELIBERATELY DOES NOT KNOW THESE NAMES, AND THAT WAS
 * RIGHT WHEN IT WAS WRITTEN. It read: *"a name a config file and a `--color-depth` flag both
 * spell is a vocabulary, and a vocabulary written down twice is a second thing to drift. So
 * `detect_color_depth` ... stays one level up, and this file is handed the answer."* True
 * while the library was C++ with a C core; false once the library IS the C, because
 * `Theme.cpp` is being deleted and the vocabulary would go with it. This is the same reversal
 * `ROLLTUI_ROLE_LIST` (rolltui_style.h) and `ROLLTUI_EFFECT_STATE_LIST` (rolltui_effects.h)
 * already made, for the same reason and on the same evidence: a vocabulary the C refuses to
 * carry does not disappear, it relocates into every caller that cannot reach it.
 *
 * FOUR SPELLINGS EXISTED WHEN THIS WAS WRITTEN, and the fourth is the one that matters:
 *   1. `Presets.cpp`  valid_depth_setting / depth_from_setting  (the "auto" layer)
 *   2. `Theme.cpp`    detect_color_depth / color_depth_name
 *   3. this header's own prose
 *   4. `rolltui/tests/theme_test.cpp:297-317` — a VERBATIM 13-line REIMPLEMENTATION of both
 *      of (2), in an anonymous namespace, which lines 498-506 then assert against. That file
 *      has no `using namespace rolltui` and does not include `Theme.hpp`, so it cannot reach
 *      the real function at all: `rolltui::detect_color_depth` ships in `studio.cpp` (5 call
 *      sites) and `TuiFrontend.cpp` and is tested by NOBODY. Nine assertions covering a path
 *      nothing runs — the same shape found in the same file one day earlier for the role
 *      names, and the third instance of it in this phase.
 *
 * ORDER IS ABI: the ordinal is what `rolltui_sgr`, `rolltui_color_downgrade` and every
 * renderer are handed. Mono must stay 0 and TrueColor last — `rolltui_color_downgrade`
 * compares against the constants, not against a count. */
#define ROLLTUI_DEPTH_LIST(X) \
  X("mono", MONO, Mono) \
  X("16", ANSI16, Ansi16) \
  X("256", ANSI256, Ansi256) \
  X("truecolor", TRUECOLOR, TrueColor)

/* The one ALIAS, and it belongs to the ENVIRONMENT rather than to the file format: COLORTERM
 * and ROLL_COLOR_DEPTH accept "24bit", a preset file's "depth" does not, and
 * `color_depth_name` must answer "truecolor" and only "truecolor". Keeping the two apart is
 * not pedantry — a preset that stored "24bit" would round-trip to "truecolor" and stop
 * matching itself, so `modified()` would report a change nobody made. Only
 * `rolltui_detect_color_depth` reads this list. */
#define ROLLTUI_DEPTH_ENV_ALIAS_LIST(X) X("24bit", TRUECOLOR)

typedef enum RolltuiColorDepth {
#define ROLLTUI_DEPTH_ENUM_(lower, UPPER, Camel) ROLLTUI_DEPTH_##UPPER,
  ROLLTUI_DEPTH_LIST(ROLLTUI_DEPTH_ENUM_)
#undef ROLLTUI_DEPTH_ENUM_
  ROLLTUI_DEPTH_COUNT
} RolltuiColorDepth;

#define ROLLTUI_MODE_LIST(X) \
  X("dark", DARK, Dark) \
  X("light", LIGHT, Light)

typedef enum RolltuiThemeMode {
#define ROLLTUI_MODE_ENUM_(lower, UPPER, Camel) ROLLTUI_MODE_##UPPER,
  ROLLTUI_MODE_LIST(ROLLTUI_MODE_ENUM_)
#undef ROLLTUI_MODE_ENUM_
  ROLLTUI_MODE_COUNT
} RolltuiThemeMode;

/* BORROWS a static literal; `*len` may be NULL. An out-of-range depth reads back as "mono"
 * and an out-of-range mode as "dark", which is what the C++ `color_depth_name` did and what a
 * zeroed byte means. */
const char* rolltui_color_depth_name(unsigned char depth, size_t* len);
const char* rolltui_theme_mode_name(unsigned char mode, size_t* len);

/* The depth/mode of that name, or -1 when there is none. Neither accepts "auto" (that is the
 * SETTING layer below, not a depth) and neither accepts the env alias above. */
int rolltui_color_depth_from_name(const char* name, size_t len);
int rolltui_theme_mode_from_name(const char* name, size_t len);

/* THE SETTING layer: what a preset file and `--color-depth`/`--theme-mode` accept, which is
 * every name above PLUS "auto" (resolve it, do not store it). `rolltui::valid_depth_setting`
 * and `valid_mode_setting` were these, and `rolltui_theme_preset_parse` took them as
 * CALLBACKS precisely because a C file could not spell the names; it can now, and the
 * callback parameters stay only so a host may narrow what IT accepts. */
int rolltui_color_depth_setting_valid(const char* s, size_t len);
int rolltui_theme_mode_setting_valid(const char* s, size_t len);

/* COLORTERM=truecolor|24bit -> TrueColor; TERM containing "256color" -> Ansi256; TERM=dumb or
 * empty -> Mono; else Ansi16. `force` (ROLL_COLOR_DEPTH) wins when set and valid. Any
 * argument may be NULL. `rolltui::detect_color_depth`'s port, verbatim. */
unsigned char rolltui_detect_color_depth(const char* colorterm, const char* term, const char* force);

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

/* ---- the style table: one role at a time (Phase 17 m2) ------------------------------------
 *
 * `rolltui::Theme::styles` is a fixed table the C++ side owns (a `std::array<RolltuiStyle,
 * kRoleCount>` — no allocation here, and none of this file's business to size) that
 * `rolltui_theme_builtin_fill`/`_load`/`_dump` below already fill and read POSITIONALLY,
 * `role_count` long. These two are the ORDINAL, bounds-checked way to read or write ONE entry
 * instead of a caller indexing `styles[role]` by hand: this file still names no role
 * (`rolltui_style.h`'s own rule, restated at the top of this file) — `role` crosses as the
 * ordinal a renderer is already handed, never a name, and `role_count` is the caller's own
 * vocabulary size, passed in exactly like every function above already takes it. */

/* A BORROW into the caller's own table, valid exactly as long as `styles` is. NULL when
 * `styles` is NULL or `role` is out of range — a bad ordinal is a caller bug, not a crash. */
const RolltuiStyle* rolltui_theme_style(const RolltuiStyle* styles, size_t role_count, unsigned char role);

/* Writes `*style` into `styles[role]`. A no-op — not a crash — when either pointer is NULL or
 * `role` is out of range, the same defensive shape `rolltui_theme_builtin_fill` already takes
 * for a mismatched count. */
void rolltui_theme_set_style(RolltuiStyle* styles, size_t role_count, unsigned char role, const RolltuiStyle* style);

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
