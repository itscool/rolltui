// rolltui/ThemeGen.cpp — see ThemeGen.hpp.
#include "rolltui/ThemeGen.hpp"

#include "rolltui/ThemeAnalysis.hpp"
#include "rolltui/c/rolltui_theme.h"  // theme_vocab()'s return type, RolltuiThemeVocab

namespace rolltui {

// Rng::next()/unit() are now rolltui_rng_next/rolltui_rng_unit (rolltui/c/rolltui_theme_gen.c),
// called by the inline methods rolltui_theme_gen.h declares on RolltuiRng itself — there is
// nothing left to define here.

std::string_view ruleset_name(Ruleset r) {
  std::size_t len;
  const char* s = rolltui_ruleset_name(static_cast<unsigned char>(r), &len);
  return std::string_view(s, len);
}

std::optional<Ruleset> ruleset_from_name(std::string_view name) {
  unsigned char out;
  if (!rolltui_ruleset_from_name(name.data(), name.size(), &out)) return std::nullopt;
  return static_cast<Ruleset>(out);
}

// Borrowed from Theme.cpp — see ThemeAnalysis.cpp's identical forward declaration for why.
const RolltuiThemeVocab& theme_vocab();

// ---- generate(): a thin shim over rolltui_theme_generate (Phase 17 m5) --------------------
// Every hue/lightness pick, the repair loop and the meta tree now live in
// rolltui_theme_gen.c; what's left here is converting `GenOptions` to plain scalars, calling
// it, and building the C++-only `Generated` (a real `Theme`, `std::vector<std::string>`)
// from what it hands back — including the FINAL `rolltui_theme_analyse` snapshot, which is
// where `role_name()` builds `broken` from (ThemeGen.hpp's header comment says why that
// stays here rather than moving to C alongside the rest).
Generated generate(std::uint64_t seed, Ruleset ruleset, double chaos, const GenOptions& opts) {
  Generated out;
  std::vector<RolltuiRoleCheck> croles(kRoleCount);
  const std::size_t pair_count = rolltui_must_differ_count();
  std::vector<RolltuiPairCheck> cpairs(pair_count);
  RolltuiBadges cbadges{};
  RolltuiStr cname{};
  RolltuiJsonValue* cmeta = nullptr;
  int repairs = 0;
  const int has_dark = opts.dark.has_value() ? 1 : 0;
  const int dark_value = opts.dark.value_or(false) ? 1 : 0;

  rolltui_theme_generate(seed, static_cast<unsigned char>(ruleset), chaos, has_dark, dark_value,
                        opts.max_repair_passes, &theme_vocab(), out.theme.styles.data(), kRoleCount, &cname, &cmeta,
                        &repairs, croles.data(), cpairs.data(), &cbadges);

  out.theme.name.assign(cname.p ? cname.p : "", cname.n);
  rolltui_str_free(&cname);
  out.theme.meta.reset(cmeta);  // adopts
  out.repairs = repairs;
  out.badges = badge_names(cbadges);
  for (const RolltuiRoleCheck& c : croles)
    if (c.text && !c.unknown && !c.readable)
      out.broken.push_back(std::string(role_name(static_cast<Role>(c.role))) + " contrast " + std::to_string(c.wcag).substr(0, 4));
  // A colour-confusable pair that differs by an attribute is repaired (the plan's rule:
  // "never rely on colour alone"); it still costs the cvd-safe badge, honestly.
  for (const RolltuiPairCheck& c : cpairs)
    if (!c.unknown && !(c.distinct && c.cvd_distinct) && !c.attribute_redundant)
      out.broken.push_back(std::string(role_name(static_cast<Role>(c.a))) + "/" +
                            std::string(role_name(static_cast<Role>(c.b))) + " confusable");
  return out;
}

}  // namespace rolltui
