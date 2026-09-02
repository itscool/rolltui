#pragma once
//
// rolltui/ThemeGen.hpp — a seeded theme generator (plan/phase-9.md, milestone 15).
// generate(seed, ruleset, chaos) → Theme, pure and deterministic: the same three inputs
// always yield the same theme, and the theme's "meta" records them so a good accident
// is reproducible.
//
// A RULESET is a palette strategy in OKLCH: how the hues of the role GROUPS relate —
//   ground   background / panel_background (and the borders drawn on them)
//   ink      text / text_muted (and the markdown text roles)
//   accents  accent_1..4 (and what borrows them: headings, list markers, links, code)
//   semantic warning / error / note / prompt (and diff_added / diff_removed)
// analogous (accents 30° apart), complementary (two pairs 180° apart), triadic (120°),
// tetradic (90°), monochrome (one hue, lightness steps), pastel (low chroma, light),
// neon (high chroma), earth (warm low-chroma hues). Each ruleset picks the hues; the
// seed picks the base hue and the dark/light ground; then the REPAIR LOOP runs
// ThemeAnalysis and applies fix_contrast / fix_confusable until the badges the ruleset
// promises hold — every ruleset promises `readable`; cvd-safe is attempted, not
// promised — or until it gives up (a bounded number of passes), in which case the
// report honestly names what is still broken.
//
// CHAOS 0..1 widens every distribution: hue jitter, lightness and chroma spread, the
// probability of ignoring a rule (swapping two accents, skipping a repair pass), random
// attributes. At 0 the output is the ruleset's canonical theme for that seed; at 1 the
// badges honestly report what got broken (asserted: for some seed at chaos 1 the report
// names a broken rule). The PRNG is our own (splitmix64) so the sequence is the same on
// every platform and compiler.
//
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Theme.hpp"

namespace rolltui {

enum class Ruleset : std::uint8_t { Analogous, Complementary, Triadic, Tetradic, Monochrome, Pastel, Neon, Earth };
inline constexpr Ruleset kRulesets[] = {Ruleset::Analogous, Ruleset::Complementary, Ruleset::Triadic, Ruleset::Tetradic,
                                        Ruleset::Monochrome, Ruleset::Pastel, Ruleset::Neon, Ruleset::Earth};
std::string_view ruleset_name(Ruleset r);
std::optional<Ruleset> ruleset_from_name(std::string_view name);

struct GenOptions {
  std::optional<bool> dark;  // force a dark or light ground; nullopt: the seed decides
  int max_repair_passes = 8;
};

// The generated theme (named "gen-<ruleset>-<seed>-<chaos>", meta: {generator: {ruleset,
// seed, chaos}, badges: the computed ones}), plus what the repair loop did.
struct Generated {
  Theme theme;
  int repairs = 0;                      // fixes applied by the repair loop
  std::vector<std::string> broken;      // rules still broken after repair (chaos, or gave up)
  std::vector<std::string> badges;      // the computed badges
};
Generated generate(std::uint64_t seed, Ruleset ruleset, double chaos, const GenOptions& opts = {});

// splitmix64 — exposed for the tests' determinism check.
struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed) {}
  std::uint64_t next();
  double unit();  // [0, 1)
};

}  // namespace rolltui
