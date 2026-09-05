#ifndef ROLLTUI_C_THEME_GEN_H
#define ROLLTUI_C_THEME_GEN_H
/*
 * rolltui/c/rolltui_theme_gen.h — THE PRNG AND THE RULESET NAMES, as C (Phase 17 m1).
 *
 * `rolltui/ThemeGen.hpp` is a seeded theme generator: `generate(seed, ruleset, chaos)` picks
 * hues in OKLCH per `Ruleset`, builds a whole `Theme`, then runs `ThemeAnalysis`'s repair
 * loop over it. That whole function stays C++ in `ThemeGen.cpp` for the reason
 * `rolltui_theme_analysis.h` gives for the report it calls: it builds a `Theme` (every
 * `Role` set) and a `json::Value` (the "generator"/"badges" meta), and neither has a C
 * representation yet. What has NO such dependency, and so is what moved here, is the two
 * things `generate()` is built out of that answer to nothing but their own inputs:
 *
 *   splitmix64        the PRNG. Deterministic on every platform and compiler by
 *                      construction (fixed-width integer arithmetic only), which is the
 *                      property `theme_gen_test.cpp`'s determinism check depends on and
 *                      the reason this generator does not use `<random>`.
 *   the ruleset name   a `Ruleset` is a `std::uint8_t` enum with a fixed set of 8 spellings
 *                      ("analogous", "complementary", ...) — the exact shape `rolltui_menu.c`
 *                      already has for `RolltuiInputType`, copied rather than re-invented.
 *
 * THE BOUNDARY'S RULES: as `rolltui_theme_analysis.h` states them. Nothing allocates here
 * either — a PRNG is eight bytes of state advanced in place, and a ruleset name is a BORROW
 * of a string literal.
 */
#include <stddef.h>
#include <stdint.h>

#include "rolltui/c/rolltui_abi.h"

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

#ifdef __cplusplus
} /* extern "C" */

inline uint64_t RolltuiRng::next() { return rolltui_rng_next(this); }
inline double RolltuiRng::unit() { return rolltui_rng_unit(this); }
#endif

#endif /* ROLLTUI_C_THEME_GEN_H */
