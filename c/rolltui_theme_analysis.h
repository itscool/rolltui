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
 *      being one definition. `RolltuiRoleCheck`/`RolltuiPairCheck`/`RolltuiFix` are NOT: the
 *      C++-facing `RoleCheck`/`PairCheck`/`Fix` are read by test and tool code as
 *      `c.role == Role::warning`, an enum comparison a C struct cannot carry (`Role` is
 *      C++-only — Style.hpp's name table stays that way, this file's own next section
 *      says why), so those three stay real C++ structs in `ThemeAnalysis.hpp`, built by its
 *      shim from this file's `unsigned char` ordinals.
 *   4. **NOTHING ALLOCATES** in the colour-maths half; the report/auto-fix half's
 *      allocations are named above and are the only ones in this file.
 */
#include <stddef.h>
#include <stdint.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"

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

/* An OKLCH colour brought into the sRGB gamut by pulling its chroma in — never by clamping
 * channels, which would move its lightness (the thing the auto-fix below relies on). Shared
 * by this file's own fixes and by `rolltui_theme_gen.c`'s hue/lightness picks: the C++ had
 * two copies of this exact algorithm (`ThemeAnalysis.cpp`'s `into_gamut`, `ThemeGen.cpp`'s
 * `okl`) and one is enough here. */
void rolltui_into_gamut(RolltuiOkLch c, RolltuiLin* out);

/* A BORROW of a string literal; never NULL, `*len` 0 for an out-of-range type. `len` may
 * be NULL. */
const char* rolltui_cvd_name(unsigned char type, size_t* len);
/* Machado, Oliveira & Fernandes 2009, severity 1.0. `type` one of the ROLLTUI_CVD_* above;
 * an out-of-range value is treated as Tritanopia, exactly as the original's `? : ?:` chain
 * fell through — see the .c file. */
void rolltui_simulate_cvd(RolltuiLin l, unsigned char type, RolltuiLin* out);

/* =========================================================================================
 * THE REPORT AND THE AUTO-FIX (Phase 17 m5)
 *
 * `analyse()` walks a whole styles table and the must-differ PAIRS (`rolltui::kMustDiffer`,
 * Style.hpp); `report_text`, `check_claims` and the auto-fix functions build on its result.
 * None of them need a `Theme` (no meta, no effects, no name) — every one takes the STYLES
 * TABLE directly, `rolltui_theme_builtin_fill`'s own shape. The must-differ pairs and which
 * roles count as "text" are THIS FILE's own local ordinals, matching `rolltui::Role`'s
 * declaration order exactly (the same `rolltui_theme.c` precedent for its built-in themes'
 * role positions), guarded the same way: `role_count` must match this file's own table
 * before either is trusted — a mismatch reads as "nothing to analyse" rather than writing
 * past the end of a caller's array.
 *
 * WHERE THE ENGLISH LIVES — a real call, not a lookup, so the reasoning is stated once:
 * `report_text` composes the WHOLE diagnostic report (badges, notes, a line per text role,
 * a line per pair) IN C, following `rolltui_layout.c`'s precedent rather than
 * `rolltui_keys.h`'s. The keys rule ("classifies and never carries a sentence") is for a
 * key's DISPLAY spelling, which legitimately varies by frontend or locale; `report_text`'s
 * sentences are this module's OWN fixed diagnostic vocabulary — produced once, printed
 * VERBATIM by two callers (`--check` and the editor's popup — ThemeAnalysis.hpp's own
 * words), tested byte-for-byte, with no caller ever wanting a different rendering of the
 * same numbers. Composing it a second time in the C++ shim would be exactly the drift
 * `rolltui_layout.c`'s own comment already argues against for its error sentences.
 * `analyse()`'s NOTES and `generate()`'s BROKEN list are the opposite case and deliberately
 * stay C++: each is a handful of short, call-site-specific sentences built from flags this
 * file already hands back (`unknown`, `text`, `readable`, and the badges), so the shim
 * builds them with `role_name()` — which it already has for free — rather than this file
 * carrying a vocab table it would otherwise have no other reason to take.
 * ========================================================================================= */

/* ---- badges: a fixed, closed set of 13 names — this module's OWN vocabulary, never a
 * Role's, so nothing below needs a vocab table to print or check one. --------------------- */
typedef struct RolltuiBadges {
  unsigned char dark ROLLTUI_DEFAULT(0), light ROLLTUI_DEFAULT(0), high_contrast ROLLTUI_DEFAULT(0),
      readable ROLLTUI_DEFAULT(0), cvd_safe ROLLTUI_DEFAULT(0);
  unsigned char protan_safe ROLLTUI_DEFAULT(0), deutan_safe ROLLTUI_DEFAULT(0), tritan_safe ROLLTUI_DEFAULT(0);
  unsigned char mono ROLLTUI_DEFAULT(0), safe_16 ROLLTUI_DEFAULT(0), safe_256 ROLLTUI_DEFAULT(0),
      transparent ROLLTUI_DEFAULT(0), attribute_redundant ROLLTUI_DEFAULT(0);
} RolltuiBadges;

/* A growing array of strings (rolltui_alloc.h strategy 2, GROWING AMORTISED) — this file's
 * one shape for "a list of short diagnostic messages": a report's claimed-badge failures.
 * `rolltui::Badges` is `using Badges = RolltuiBadges;` (ONE DEFINITION, the `Lin`/`Style`
 * move) — see this header's top comment for why `RoleCheck`/`PairCheck`/`Fix` are not. */
typedef struct RolltuiStrArray {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0), cap ROLLTUI_DEFAULT(0);
} RolltuiStrArray;
/* Frees every element and the array; zeroes. Safe on a zeroed array and on repeated calls. */
void rolltui_str_array_release(RolltuiStrArray* a);

/* Resets `out` first. Appends the names of the badges that are set, in the fixed order
 * `rolltui::badge_names` always used ("dark", "light", "high-contrast", "readable",
 * "cvd-safe", "protan-safe", "deutan-safe", "tritan-safe", "mono", "16-safe", "256-safe",
 * "transparent", "attribute-redundant"). */
void rolltui_badge_names(const RolltuiBadges* b, RolltuiStrArray* out);
/* 1 iff `name` is one of `b`'s set badges. */
int rolltui_has_badge(const RolltuiBadges* b, const char* name, size_t len);

/* ---- thresholds. `rolltui::kReadableRatio` etc. are `= ROLLTUI_*` aliases of these, so the
 * number is written down once. ------------------------------------------------------------ */
#define ROLLTUI_READABLE_RATIO 4.5
#define ROLLTUI_HIGH_CONTRAST_RATIO 7.0
#define ROLLTUI_DISTINCT_DELTA_E 0.08

/* The must-differ pairs' COUNT (Style.hpp's `kMustDifferCount`) — a fixed, closed set of
 * role-ordinal pairs this file carries locally (see this section's top comment). */
#define ROLLTUI_MUST_DIFFER_COUNT 11
size_t rolltui_must_differ_count(void); /* returns ROLLTUI_MUST_DIFFER_COUNT */

/* ---- the per-role / per-pair check ------------------------------------------------------- */
typedef struct RolltuiRoleCheck {
  unsigned char role ROLLTUI_DEFAULT(0);  /* always `i` for out_roles[i] — positional, not looked up */
  unsigned char text ROLLTUI_DEFAULT(1);  /* is this role's fg drawn as text? (counts for readable/high) */
  double wcag ROLLTUI_DEFAULT(0), apca ROLLTUI_DEFAULT(0); /* meaningless when `unknown` */
  RolltuiStyleColor fg, bg;                /* measured colours (bg: the role's own, else the theme's background) */
  unsigned char unknown ROLLTUI_DEFAULT(0); /* depends on the terminal: a measured colour was None */
  unsigned char readable ROLLTUI_DEFAULT(0), high ROLLTUI_DEFAULT(0);
} RolltuiRoleCheck;

typedef struct RolltuiPairCheck {
  unsigned char a ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* the pair's OWN role ordinals, not positional */
  double delta ROLLTUI_DEFAULT(0);          /* meaningless when `unknown` */
  double delta_cvd[3];                      /* per ROLLTUI_CVD_*; meaningless when `unknown` */
  unsigned char unknown ROLLTUI_DEFAULT(0);
  unsigned char distinct ROLLTUI_DEFAULT(0), cvd_distinct ROLLTUI_DEFAULT(0);
  unsigned char attribute_redundant ROLLTUI_DEFAULT(0);
  unsigned char collapses_16 ROLLTUI_DEFAULT(0), collapses_256 ROLLTUI_DEFAULT(0);
} RolltuiPairCheck;

/* CALLER-FILLED: out_roles[0..role_count), out_pairs[0..rolltui_must_differ_count()). Both
 * arrays and `*out_badges` are written positionally in full on success. Returns 0 (nothing
 * written) when `role_count` does not match this file's own role table (the same defensive
 * shape `rolltui_theme_builtin_fill` already takes). Mirrors `rolltui::analyse` exactly,
 * MINUS the notes — see this section's top comment for why those are the C++ shim's to
 * build from the `unknown`/`text`/`readable` flags and `*out_badges` this already returns. */
int rolltui_theme_analyse(const RolltuiStyle* styles, size_t role_count, RolltuiRoleCheck* out_roles,
                          RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);

/* Composes the full report exactly as `rolltui::report_text` did: "badges: ...\n", each of
 * `notes` as "note: ...\n" (the caller's own — `ThemeReport::notes`, already built; see this
 * section's top comment for why those live in the shim), a blank line and a line per TEXT
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

/* ---- auto-fix proposals ------------------------------------------------------------------ */
typedef struct RolltuiFix {
  unsigned char role ROLLTUI_DEFAULT(0);
  RolltuiStyle before, after;
  RolltuiStr what;   /* OWNED — e.g. "md_link fg: contrast 3.1 -> 4.6" */
  double before_value ROLLTUI_DEFAULT(0), after_value ROLLTUI_DEFAULT(0);
} RolltuiFix;
/* Frees `what`, zeroes. Safe on a zeroed `RolltuiFix` and on repeated calls. */
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

/* A growing array of `RolltuiFix` (GROWING AMORTISED); each element owns its own `what`. */
typedef struct RolltuiFixArray {
  RolltuiFix* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0), cap ROLLTUI_DEFAULT(0);
} RolltuiFixArray;
/* Releases every element's `what`, then the array; zeroes. Safe on a zeroed array and on
 * repeated calls. */
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_ANALYSIS_H */
