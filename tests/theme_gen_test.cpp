//
// theme_gen_test.cpp — the seeded generator (milestone 15): determinism (same seed +
// ruleset + chaos → the same theme, twice, and a different seed → a different one),
// every ruleset at chaos 0 passes `readable` for a run of seeds, chaos 1 breaks a rule
// for some seed (the chaos is real), the meta records the inputs and the computed
// badges, the file round-trips with its meta, and check_claims holds on a generated
// theme's own claims.
//
#include <string>

#include "rolltui/ThemeAnalysis.hpp"
#include "rolltui/ThemeGen.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

int main() {
  // ---- the PRNG ----
  {
    Rng a(42), b(42);
    bool same = true;
    for (int i = 0; i < 100; ++i) same &= a.next() == b.next();
    check(same, "splitmix64: the same seed gives the same sequence");
    Rng c(43);
    check(Rng(42).next() != c.next(), "…and a different seed a different one");
    Rng u(7);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) { const double x = u.unit(); in_range &= x >= 0.0 && x < 1.0; }
    check(in_range, "unit() stays in [0, 1)");
  }
  // ---- determinism ----
  {
    const Generated g1 = generate(1, Ruleset::Triadic, 0.0), g2 = generate(1, Ruleset::Triadic, 0.0);
    check(g1.theme.styles == g2.theme.styles && g1.theme.name == g2.theme.name && g1.badges == g2.badges, "the same inputs yield the same theme");
    const Generated g3 = generate(2, Ruleset::Triadic, 0.0);
    check(!(g3.theme.styles == g1.theme.styles), "a different seed yields a different theme");
    const Generated g4 = generate(1, Ruleset::Neon, 0.0);
    check(!(g4.theme.styles == g1.theme.styles), "a different ruleset too");
    check(g1.theme.name == "gen-triadic-1-0.00", "the name records ruleset, seed and chaos [" + g1.theme.name + "]");
    const RolltuiJsonValue* gen1 = rolltui_json_get(g1.theme.meta.get(), "generator", 9);
    std::size_t ruleset1_len = 0;
    const char* ruleset1 = rolltui_json_as_string(rolltui_json_get(gen1, "ruleset", 7), "", 0, &ruleset1_len);
    check(rolltui_json_as_number(rolltui_json_get(gen1, "seed", 4), -1) == 1 &&
              std::string_view(ruleset1, ruleset1_len) == "triadic" &&
              rolltui_json_is_array(rolltui_json_get(g1.theme.meta.get(), "badges", 6)) != 0,
          "meta records the generator inputs and the computed badges");
  }
  // ---- every ruleset at chaos 0 is readable ----
  {
    bool all = true;
    for (Ruleset r : kRulesets)
      for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const Generated g = generate(seed, r, 0.0);
        const bool ok = has_badge(analyse(g.theme).badges, "readable") && g.broken.empty();
        if (!ok) { all = false; check(false, std::string(ruleset_name(r)) + " seed " + std::to_string(seed) + " at chaos 0 is not readable: " + (g.broken.empty() ? "?" : g.broken[0])); }
      }
    check(all, "every ruleset × 12 seeds at chaos 0 passes readable with nothing broken (the repair loop delivers what it promises)");
    int dark = 0, light = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
      const Generated g = generate(seed, Ruleset::Analogous, 0.0);
      if (has_badge(analyse(g.theme).badges, "dark")) ++dark; else ++light;
    }
    check(dark > 5 && light > 5, "the seed decides the ground: both dark and light themes occur (" + std::to_string(dark) + " dark, " + std::to_string(light) + " light of 40)");
    GenOptions force;
    force.dark = false;
    check(has_badge(analyse(generate(3, Ruleset::Earth, 0.0, force).theme).badges, "light"), "GenOptions::dark forces the ground");
  }
  // ---- chaos is real ----
  {
    int broken_seeds = 0, first = -1;
    for (int seed = 1; seed <= 30; ++seed) {
      const Generated g = generate(static_cast<std::uint64_t>(seed), Ruleset::Neon, 1.0);
      if (!g.broken.empty()) { ++broken_seeds; if (first < 0) first = seed; }
    }
    check(broken_seeds > 0, "at chaos 1 some seed reports a broken rule (" + std::to_string(broken_seeds) + " of 30; first seed " + std::to_string(first) + ")");
    check(broken_seeds < 30, "…and not every seed: chaos widens, it does not guarantee breakage (" + std::to_string(broken_seeds) + " of 30)");
    const Generated g = generate(static_cast<std::uint64_t>(first), Ruleset::Neon, 1.0);
    const std::vector<std::string> failed = check_claims(g.theme, analyse(g.theme));
    check(failed.empty(), "a generated theme's claimed badges are exactly the computed ones, even when chaos broke a rule");
  }
  // ---- the file round trip keeps meta ----
  {
    const Generated g = generate(9, Ruleset::Pastel, 0.25);
    const std::string text = theme_to_json(g.theme);
    ThemeLoadReport rep;
    std::optional<Theme> back = load_theme(text, ThemeMode::Dark, rep);
    check(back && rep.clean() && back->styles == g.theme.styles && rolltui_json_equal(back->meta.get(), g.theme.meta.get()) != 0,
          "theme_to_json keeps meta, and the loader accepts it as a known key");
    check(text.find("\"generator\"") != std::string::npos && text.find("\"chaos\": 0.25") != std::string::npos, "…with the generator inputs in the file");
  }
  return report("rolltui theme_gen_test");
}
