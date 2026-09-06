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

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
/* 1 on success, 0 for `Color::none()` (the terminal's own colour — unknown, not assumed). */
int rolltui_to_linear(RolltuiStyleColor c, RolltuiLin* out);
/* Clamped, encoded to an Rgb colour. */

void rolltui_str_array_release(RolltuiStrArray* a);

/* Resets `out` first. Appends the names of the badges that are set, in the fixed order
 * `rolltui::badge_names` always used ("dark", "light", "high-contrast", "readable",
 * "cvd-safe", "protan-safe", "deutan-safe", "tritan-safe", "mono", "16-safe", "256-safe",
 * "transparent", "attribute-redundant"). */
void rolltui_badge_names(const RolltuiBadges* b, RolltuiStrArray* out);
/* 1 iff `name` is one of `b`'s set badges. */

size_t rolltui_must_differ_count(void); /* returns ROLLTUI_MUST_DIFFER_COUNT */

/* CALLER-FILLED: out_roles[0..role_count), out_pairs[0..rolltui_must_differ_count()). Both
 * arrays and `*out_badges` are written positionally in full on success. Returns 0 (nothing
 * written) when `role_count` does not match this file's own role table (the same defensive
 * shape `rolltui_theme_builtin_fill` already takes). Mirrors `rolltui::analyse` exactly,
 * MINUS the notes — see this section's top comment for why those are the consumer's to
 * build from the `unknown`/`text`/`readable` flags and `*out_badges` this already returns. */
int rolltui_theme_analyse(const RolltuiStyle* styles, size_t role_count, RolltuiRoleCheck* out_roles,
                          RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);

/* Composes the full report exactly as `rolltui::report_text` did: "badges: ...\n", each of
 * `notes` as "note: ...\n" (the caller's own, already built; see this section's top comment
 * for why those live in the consumer), a blank line and a line per TEXT
 * role, a blank line and a line per pair. APPENDS to `*out` (rolltui.h's UNBOUNDED text
 * shape) rather than clearing it first — a fresh caller passes a zero-initialised `RolltuiStr`. */
void rolltui_theme_report_text(const RolltuiRoleCheck* roles, size_t role_count, const RolltuiPairCheck* pairs,
                               size_t pair_count, const RolltuiBadges* badges, const RolltuiStr* notes,
                               size_t notes_n, const RolltuiThemeVocab* vocab, RolltuiStr* out);

/* A theme's claimed badges ("meta": {"badges": [...]}) against the computed ones: appends
 * each claim that does NOT hold, in claim order, to `out` (RESET first). `meta` may be NULL
 * or lack a "badges" array (or a "badges" that is not an array) — either way, nothing is
 * appended. */
void rolltui_check_claims(const RolltuiJsonValue* meta, const RolltuiBadges* badges, RolltuiStrArray* out);

void rolltui_fix_array_release(RolltuiFixArray* a);

/* Every failing role and pair, in report order (`rolltui_theme_analyse`'s own role and pair
 * order): a `rolltui_fix_contrast` for each TEXT role that is known and not yet readable, a
 * `rolltui_fix_confusable` for each pair that is known, not already (distinct AND
 * cvd_distinct), and not already attribute-redundant (the fix of last resort, applied). A
 * fix that turns out unavailable for a qualifying role/pair (e.g. an unknown colour slipping
 * through) is simply not appended. `out` is RESET first. */
void rolltui_propose_fixes(const RolltuiStyle* styles, size_t role_count, const RolltuiThemeVocab* vocab,
                           RolltuiFixArray* out);

/* `styles[role] = *after`. A no-op when `role` is out of range. Mirrors
 * `rolltui::apply_fix` (`theme.style(fix.role) = fix.after`) exactly. */
void rolltui_apply_fix(RolltuiStyle* styles, size_t role_count, unsigned char role, const RolltuiStyle* after);

/* ---- generate() (Phase 17 m5) ------------------------------------------------------------
 *
 * Builds a whole theme positionally into `out_styles[0..role_count)` (CALLER-FILLED, the
 * same convention `rolltui_theme_builtin_fill` already uses) and runs the repair loop
 * (`rolltui_theme_analyse` / `rolltui_propose_fixes` / `rolltui_apply_fix`,
 * `rolltui_theme_analysis.h`) until the promised badges hold or it gives up. Mirrors
 * `rolltui::generate` exactly (same hue/lightness picks per ruleset, same jitter/chance
 * draws off the same PRNG sequence, same repair loop shape), so the SAME (seed, ruleset,
 * chaos) still yields the SAME theme — `theme_gen_test.cpp`'s determinism check is the
 * oracle for this.
 *
 * `has_dark`/`dark_value` stand in for `GenOptions::dark` (a `std::optional<bool>` — one bit
 * needs no struct): `has_dark` 0 means "let the seed decide" (nullopt), matching
 * `opts.dark ? *opts.dark : rng.unit() < 0.6`. `max_repair_passes` is `GenOptions`'s field of
 * the same name verbatim. `vocab` is forwarded to `rolltui_propose_fixes` only — see this
 * header's top comment for why `generate()`'s own "broken" list is not built here.
 *
 * Returns 0 (nothing written) when `role_count` does not match this file's own role table
 * (the same defensive shape `rolltui_theme_builtin_fill` already takes). On success:
 *   out_styles[0..role_count)   the generated theme's styles, CALLER-FILLED
 *   *out_name                   "gen-<ruleset>-<seed>-<chaos>" (OWNED — free with
 *                                `rolltui_str_free`, or hand it straight to a `Theme::name`)
 *   *out_meta                   a fresh OWNED tree: {"generator": {"ruleset","seed","chaos"},
 *                                "badges": [...]} (free with `rolltui_json_free`, or adopt it
 *                                into `Theme::meta` directly)
 *   *out_repairs                fixes applied by the repair loop
 *   out_roles / out_pairs       the FINAL, post-repair `rolltui_theme_analyse` snapshot —
 *                                sized exactly as that function's own out-params
 *                                (role_count, `rolltui_must_differ_count()`) — for the
 *                                shim's "broken" list
 *   *out_badges                 the final computed badges (same as `out_meta`'s "badges",
 *                                as bits rather than names) */
int rolltui_theme_generate(uint64_t seed, unsigned char ruleset, double chaos, int has_dark, int dark_value,
                           int max_repair_passes, const RolltuiThemeVocab* vocab, RolltuiStyle* out_styles,
                           size_t role_count, RolltuiStr* out_name, RolltuiJsonValue** out_meta, int* out_repairs,
                           RolltuiRoleCheck* out_roles, RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_ANALYSIS_H */
