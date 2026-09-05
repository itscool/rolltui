/* rolltui/c/rolltui_theme_gen.c — the PRNG and the ruleset name table. See
 * rolltui_theme_gen.h for the boundary's rules and what deliberately stayed in
 * `rolltui/ThemeGen.cpp` (`generate()` itself: it builds a `Theme`, which has no C
 * representation yet). Nothing allocates. */
#include "rolltui/c/rolltui_theme_gen.h"

#include <string.h>

/* splitmix64 (Vigna). Fixed-width integer arithmetic only, so the sequence is the same on
 * every platform and compiler this library runs on — the property theme_gen_test.cpp's
 * determinism check depends on. */
uint64_t rolltui_rng_next(RolltuiRng* r) {
  uint64_t z = (r->state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

double rolltui_rng_unit(RolltuiRng* r) {
  return (double)(rolltui_rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

static const char* const kRulesetNames[ROLLTUI_RULESET_COUNT] = {
    "analogous", "complementary", "triadic", "tetradic", "monochrome", "pastel", "neon", "earth"};

const char* rolltui_ruleset_name(unsigned char ruleset, size_t* len) {
  const char* s = ruleset < ROLLTUI_RULESET_COUNT ? kRulesetNames[ruleset] : "";
  if (len) *len = strlen(s);
  return s;
}

int rolltui_ruleset_from_name(const char* name, size_t len, unsigned char* out) {
  unsigned char i;
  for (i = 0; i < ROLLTUI_RULESET_COUNT; ++i)
    if (strlen(kRulesetNames[i]) == len && memcmp(kRulesetNames[i], name, len) == 0) {
      *out = i;
      return 1;
    }
  return 0;
}
