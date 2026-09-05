#ifndef ROLLTUI_C_THEME_GEN_H
#define ROLLTUI_C_THEME_GEN_H
/*
 * rolltui/c/rolltui_theme_gen.h — THE PRNG AND THE RULESET NAMES, as C (Phase 17 m1).
 *
 * `rolltui/ThemeGen.hpp` is a seeded theme generator: `generate(seed, ruleset, chaos)` picks
 * hues in OKLCH per `Ruleset`, builds a whole `Theme`, then runs `ThemeAnalysis`'s repair
 * loop over it. Originally this whole function stayed C++ in `ThemeGen.cpp`, for the reason
 * `rolltui_theme_analysis.h`'s ORIGINAL note gave for the report it calls: it built a `Theme`
 * (every `Role` set) and a `json::Value` (the "generator"/"badges" meta), and neither had a C
 * representation yet. Two things changed that (Phase 17 m5): `Theme`'s styles table already
 * had one (`rolltui_theme_style`/`_set_style`), and the report/auto-fix it repairs with moved
 * to `rolltui_theme_analysis.h` alongside `RolltuiJsonValue` (`rolltui_json.h`) — so
 * `generate()` moved too, and `ThemeGen.cpp` is now a thin C++ shim over
 * `rolltui_theme_generate` below the same way `Theme.cpp` is over `rolltui_theme.h`.
 *
 * splitmix64 and the ruleset name (below) were ALREADY C (this file's original note, kept):
 *
 *   splitmix64        the PRNG. Deterministic on every platform and compiler by
 *                      construction (fixed-width integer arithmetic only), which is the
 *                      property `theme_gen_test.cpp`'s determinism check depends on and
 *                      the reason this generator does not use `<random>`.
 *   the ruleset name   a `Ruleset` is a `std::uint8_t` enum with a fixed set of 8 spellings
 *                      ("analogous", "complementary", ...) — the exact shape `rolltui_menu.c`
 *                      already has for `RolltuiInputType`, copied rather than re-invented.
 *
 * `rolltui_theme_generate` does NOT print a role's name anywhere (the hue/lightness picks
 * are positional, matching `rolltui_theme.c`'s built-in fillers exactly) EXCEPT by
 * forwarding `vocab` into `rolltui_propose_fixes` for its fixes' diagnostic text during the
 * repair loop — `generate()`'s OWN "broken" list, which DOES print role names, stays the
 * C++ shim's to build from this function's final `out_roles`/`out_pairs` snapshot, for the
 * same reason `rolltui_theme_analyse`'s notes do (`rolltui_theme_analysis.h`'s header
 * comment).
 *
 * THE BOUNDARY'S RULES: as `rolltui_theme_analysis.h` states them (this file allocates now
 * too — the meta tree, the growing report snapshot arrays it fills positionally into
 * caller-owned storage). A PRNG is eight bytes of state advanced in place, and a ruleset
 * name is a BORROW of a string literal, same as always.
 */
#include <stddef.h>
#include <stdint.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the ruleset, as a byte (the same order as `rolltui::Ruleset`) --------------------- */
#define ROLLTUI_RULESET_ANALOGOUS 0
#define ROLLTUI_RULESET_COMPLEMENTARY 1
#define ROLLTUI_RULESET_TRIADIC 2
#define ROLLTUI_RULESET_TETRADIC 3
#define ROLLTUI_RULESET_MONOCHROME 4
#define ROLLTUI_RULESET_PASTEL 5
#define ROLLTUI_RULESET_NEON 6
#define ROLLTUI_RULESET_EARTH 7
#define ROLLTUI_RULESET_COUNT 8

/* A BORROW of a string literal; never NULL, `*len` 0 for an out-of-range ruleset. `len`
 * may be NULL. */
const char* rolltui_ruleset_name(unsigned char ruleset, size_t* len);
/* 1 and `*out` set on a match, 0 (leaving `*out` untouched) otherwise. */
int rolltui_ruleset_from_name(const char* name, size_t len, unsigned char* out);

/* ---- splitmix64, defined ONCE and compiled by both languages --------------------------- */
/* `rolltui::Rng` IS this struct (ThemeGen.hpp aliases it): eight bytes of state, advanced by
 * the two functions declared right after it. The C++ methods are declared here but DEFINED
 * out-of-line below, once the free functions they call are themselves declared — the same
 * order `rolltui_str.h` uses for `RolltuiStr`'s destructor and `assign()`. */
typedef struct RolltuiRng {
  uint64_t state ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  explicit RolltuiRng(uint64_t seed) : state(seed) {}
  uint64_t next();
  double unit(); /* [0, 1) */
#endif
} RolltuiRng;

uint64_t rolltui_rng_next(RolltuiRng* r);
double rolltui_rng_unit(RolltuiRng* r); /* [0, 1) */

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

inline uint64_t RolltuiRng::next() { return rolltui_rng_next(this); }
inline double RolltuiRng::unit() { return rolltui_rng_unit(this); }
#endif

#endif /* ROLLTUI_C_THEME_GEN_H */
