#ifndef ROLLTUI_C_THEME_GEN_H
#define ROLLTUI_C_THEME_GEN_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_theme_gen.h — THE PRNG AND THE RULESET NAMES, as C.
 *
 * `rolltui/ThemeGen.hpp` is a seeded theme generator: `generate(seed, ruleset, chaos)` picks
 * hues in OKLCH per `Ruleset`, builds a whole `Theme`, then runs `ThemeAnalysis`'s repair
 * loop over it. Originally this whole function stayed C++ in `ThemeGen.cpp`, for the reason
 * `rolltui_theme_analysis.h`'s ORIGINAL note gave for the report it calls: it built a `Theme`
 * (every `Role` set) and a `json::Value` (the "generator"/"badges" meta), and neither had a C
 * representation yet. Two things changed that: `Theme`'s styles table already
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

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"

#ifdef __cplusplus
extern "C" {
#endif
/* ---- splitmix64, defined ONCE and compiled by both languages ----------------------------- */
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

#ifdef __cplusplus
inline uint64_t RolltuiRng::next() { return rolltui_rng_next(this); }
inline double RolltuiRng::unit() { return rolltui_rng_unit(this); }

#endif

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
/* A BORROW of a string literal; never NULL, `*len` 0 for an out-of-range ruleset. `len`
 * may be NULL. */
const char* rolltui_ruleset_name(unsigned char ruleset, size_t* len);
/* 1 and `*out` set on a match, 0 (leaving `*out` untouched) otherwise. */
int rolltui_ruleset_from_name(const char* name, size_t len, unsigned char* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_THEME_GEN_H */
