/* rolltui/c/rolltui_theme_gen.c — the PRNG, the ruleset name table, and
 * generate() itself. See rolltui_theme_gen.h for the boundary's rules and the full
 * reasoning for why `generate()` moved here. */
#include "rolltui/c/rolltui_theme_gen.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_theme_analysis.h"

#include <math.h>
#include <stdio.h>
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

/* =========================================================================================
 * generate() — ported verbatim from `rolltui::generate` (`rolltui/ThemeGen.cpp`,
 * now a thin C++ shim over this).
 *
 * RNG-SEQUENCE FIDELITY IS THE WHOLE POINT of this port (theme_gen_test.cpp's determinism
 * check), so every `rolltui_rng_unit` draw below happens in EXACTLY the statement order the
 * original's `jitter(...)`/`chance(...)` calls did — verified line by line against
 * `ThemeGen.cpp` rather than reordered for convenience, even where reordering would not
 * otherwise change the maths (e.g. `bg`/`panel`/`fg`/`muted`/`border`/`code_bg`/`sel`/
 * `find_wash` below call NO jitter/chance at all — every argument is already computed — so
 * their relative order is free; `warning`/`error_c`/`green`/`cyan` each draw exactly once and
 * MUST stay in this order since each draw advances the shared state). */

/* A literal C string plus its length via `strlen` — see `rolltui_theme_analysis.c`'s own
 * copy of this macro for why it is duplicated rather than shared. */
#define K(s) (s), strlen(s)

/* Role ordinals, LOCAL to this file (mirrors `rolltui_theme.c`'s and
 * `rolltui_theme_analysis.c`'s own copies — a `static`/file-scope table has no external
 * linkage to share, and this file's `role_count` check guards its own copy independently). */
enum {
  R_text, R_text_muted, R_background, R_panel_background, R_border, R_border_active, R_title,
  R_label, R_value, R_accent_1, R_accent_2, R_accent_3, R_accent_4, R_prompt, R_note, R_warning, R_error,
  R_md_heading, R_md_emphasis, R_md_strong, R_md_code_inline, R_md_code_block, R_md_code_label,
  R_md_link, R_md_link_url, R_md_quote, R_md_list_marker, R_md_table_border, R_md_table_header,
  R_md_rule, R_md_strikethrough,
  R_diff_added, R_diff_removed, R_diff_context, R_diff_added_word, R_diff_removed_word,
  R_input_text, R_input_cursor, R_input_placeholder, R_scroll_marker, R_selection, R_overlay,
  R_menu_item, R_menu_selected, R_menu_breadcrumb, R_menu_shortcut,
  R_find_match, R_find_current,
  R_scrollbar,
  ROLLTUI_THEME_GEN_ROLE_COUNT_
};

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double maxd(double a, double b) { return a > b ? a : b; }

static double wrap_hue(double h) {
  h = fmod(h, 360.0);
  return h < 0 ? h + 360.0 : h;
}

/* `rng.unit() * 2 - 1) * width * chaos` — `rolltui::(anonymous namespace)`'s `jitter` lambda,
 * ported verbatim. Draws exactly once. */
static double rng_jitter(RolltuiRng* rng, double chaos, double width) {
  return (rolltui_rng_unit(rng) * 2.0 - 1.0) * width * chaos;
}
/* `rng.unit() < p * chaos` — the `chance` lambda, ported verbatim. Draws exactly once. */
static int rng_chance(RolltuiRng* rng, double chaos, double p) { return rolltui_rng_unit(rng) < p * chaos; }

/* An OKLCH colour (L, C, h) clamped into the sRGB gamut — `rolltui::(anonymous
 * namespace)::okl`, ported onto the shared `rolltui_into_gamut` (rolltui_theme_analysis.h)
 * instead of carrying its own copy of the reduction loop. */
static RolltuiStyleColor okl(double L, double C, double h) {
  RolltuiOkLch lch;
  RolltuiLin lin;
  RolltuiStyleColor c;
  lch.L = clampd(L, 0.0, 1.0);
  lch.C = C;
  lch.h = wrap_hue(h);
  rolltui_into_gamut(lch, &lin);
  rolltui_from_linear(lin, &c);
  return c;
}

static RolltuiStyle mkstyle(RolltuiStyleColor fg, RolltuiStyleColor bg, int bold, int italic, int underline,
                            int dim, int reverse) {
  RolltuiStyle s;
  s.fg = fg;
  s.bg = bg;
  s.bold = (unsigned char)bold;
  s.italic = (unsigned char)italic;
  s.underline = (unsigned char)underline;
  s.dim = (unsigned char)dim;
  s.reverse = (unsigned char)reverse;
  return s;
}

int rolltui_theme_generate(uint64_t seed, unsigned char ruleset, double chaos, int has_dark, int dark_value,
                           int max_repair_passes, const RolltuiThemeVocab* vocab, RolltuiStyle* out_styles,
                           size_t role_count, RolltuiStr* out_name, RolltuiJsonValue** out_meta, int* out_repairs,
                           RolltuiRoleCheck* out_roles, RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges) {
  RolltuiRng rng;
  int dark;
  double base_hue;
  double acc[4];
  double chroma = 0.13, ink_chroma = 0.01, ground_chroma = 0.01;
  int i;
  double ground_L, panel_L, ink_L, muted_L, border_L, acc_L[4];
  RolltuiStyleColor bg, panel, fg, muted, border, a[4], warning, error_c, green, cyan, code_bg, sel, find_wash;
  char namebuf[96];
  const char* rn;
  size_t rnlen;
  int rb, ri, ru;
  int pass;
  int namelen;
  RolltuiJsonValue *meta, *gen, *badges_arr;
  RolltuiStrArray bnames;

  if (!out_styles || !out_name || !out_meta || !out_repairs || !out_roles || !out_pairs || !out_badges) return 0;
  if (role_count != ROLLTUI_THEME_GEN_ROLE_COUNT_) return 0;

  chaos = clampd(chaos, 0.0, 1.0);
  rng.state = seed ^ ((uint64_t)ruleset << 56) ^ (uint64_t)(chaos * 1000.0);

  dark = has_dark ? (dark_value != 0) : (rolltui_rng_unit(&rng) < 0.6);
  base_hue = rolltui_rng_unit(&rng) * 360.0;

  switch (ruleset) {
    case ROLLTUI_RULESET_ANALOGOUS:
      for (i = 0; i < 4; ++i) acc[i] = base_hue + 30.0 * i;
      break;
    case ROLLTUI_RULESET_COMPLEMENTARY:
      acc[0] = base_hue; acc[1] = base_hue + 180; acc[2] = base_hue + 25; acc[3] = base_hue + 205;
      break;
    case ROLLTUI_RULESET_TRIADIC:
      acc[0] = base_hue; acc[1] = base_hue + 120; acc[2] = base_hue + 240; acc[3] = base_hue + 60;
      break;
    case ROLLTUI_RULESET_TETRADIC:
      for (i = 0; i < 4; ++i) acc[i] = base_hue + 90.0 * i;
      break;
    case ROLLTUI_RULESET_MONOCHROME:
      for (i = 0; i < 4; ++i) acc[i] = base_hue;
      chroma = 0.10; ground_chroma = 0.02;
      break;
    case ROLLTUI_RULESET_PASTEL:
      for (i = 0; i < 4; ++i) acc[i] = base_hue + 72.0 * i;
      chroma = 0.07;
      break;
    case ROLLTUI_RULESET_NEON:
      for (i = 0; i < 4; ++i) acc[i] = base_hue + 90.0 * i + 45;
      chroma = 0.22; ground_chroma = 0.0;
      break;
    case ROLLTUI_RULESET_EARTH:
      acc[0] = 60; acc[1] = 40; acc[2] = 90; acc[3] = 25;
      chroma = 0.08; ground_chroma = 0.015; ink_chroma = 0.02;
      break;
    default: /* an out-of-range byte cannot happen through the C++ shim's `Ruleset` enum;
              * defensively the same as Analogous rather than leaving `acc` uninitialised */
      for (i = 0; i < 4; ++i) acc[i] = base_hue + 30.0 * i;
      break;
  }
  for (i = 0; i < 4; ++i) acc[i] = wrap_hue(acc[i] + rng_jitter(&rng, chaos, 40.0));
  if (rng_chance(&rng, chaos, 0.5)) {
    double tmp = acc[1];
    acc[1] = acc[2];
    acc[2] = tmp;
  }
  chroma = maxd(0.02, chroma + rng_jitter(&rng, chaos, 0.08));

  ground_L = dark ? 0.18 + rng_jitter(&rng, chaos, 0.06) : 0.97 + rng_jitter(&rng, chaos, 0.02);
  panel_L = dark ? ground_L + 0.04 : ground_L - 0.04;
  ink_L = dark ? 0.90 + rng_jitter(&rng, chaos, 0.05) : 0.22 + rng_jitter(&rng, chaos, 0.05);
  muted_L = dark ? 0.66 + rng_jitter(&rng, chaos, 0.08) : 0.44 + rng_jitter(&rng, chaos, 0.08);
  border_L = dark ? 0.38 : 0.80;
  acc_L[0] = dark ? 0.78 : 0.42;
  acc_L[1] = dark ? 0.88 : 0.33;
  acc_L[2] = dark ? 0.69 : 0.51;
  acc_L[3] = dark ? 0.60 : 0.24;

  bg = okl(ground_L, ground_chroma, base_hue);
  panel = okl(panel_L, ground_chroma, base_hue);
  fg = okl(ink_L, ink_chroma, base_hue);
  muted = okl(muted_L, ink_chroma, base_hue);
  border = okl(border_L, ground_chroma, base_hue);
  for (i = 0; i < 4; ++i) a[i] = okl(acc_L[i] + rng_jitter(&rng, chaos, 0.10), chroma, acc[i]);
  /* Order matters from here: each of the next four draws exactly one jitter(). */
  warning = okl(dark ? 0.80 : 0.46, chroma, wrap_hue(85.0 + rng_jitter(&rng, chaos, 30.0)));
  error_c = okl(dark ? 0.66 : 0.40, chroma + 0.03, wrap_hue(25.0 + rng_jitter(&rng, chaos, 20.0)));
  green = okl(dark ? 0.84 : 0.40, chroma, wrap_hue(145.0 + rng_jitter(&rng, chaos, 20.0)));
  cyan = okl(dark ? 0.80 : 0.42, chroma, wrap_hue(200.0 + rng_jitter(&rng, chaos, 20.0)));
  code_bg = okl(dark ? ground_L + 0.03 : ground_L - 0.03, ground_chroma, base_hue);
  sel = okl(dark ? 0.35 : 0.85, 0.05, acc[0]);
  find_wash = okl(dark ? 0.35 : 0.85, 0.06, acc[2]);

  rn = rolltui_ruleset_name(ruleset, &rnlen);
  namelen = snprintf(namebuf, sizeof namebuf, "gen-%.*s-%llu-%.2f", (int)rnlen, rn, (unsigned long long)seed, chaos);
  if (namelen < 0) namelen = 0;
  if ((size_t)namelen >= sizeof namebuf) namelen = (int)sizeof namebuf - 1;
  rolltui_str_set(out_name, namebuf, (size_t)namelen);

  /* Random attributes under chaos — three more draws, in this order. */
  rb = rng_chance(&rng, chaos, 0.5);
  ri = rng_chance(&rng, chaos, 0.5);
  ru = rng_chance(&rng, chaos, 0.4);

  out_styles[R_text] = mkstyle(fg, bg, 0, 0, 0, 0, 0);
  out_styles[R_text_muted] = mkstyle(muted, bg, 0, 0, 0, 0, 0);
  out_styles[R_background] = mkstyle(fg, bg, 0, 0, 0, 0, 0);
  out_styles[R_panel_background] = mkstyle(fg, panel, 0, 0, 0, 0, 0);
  out_styles[R_border] = mkstyle(border, bg, 0, 0, 0, 0, 0);
  out_styles[R_border_active] = mkstyle(a[0], bg, 0, 0, 0, 0, 0);
  out_styles[R_title] = mkstyle(fg, bg, 1, 0, 0, 0, 0);
  out_styles[R_label] = mkstyle(muted, panel, 0, 0, 0, 0, 0);
  out_styles[R_value] = mkstyle(fg, panel, 0, 0, 0, 0, 0);
  out_styles[R_accent_1] = mkstyle(a[0], bg, rb, 0, 0, 0, 0);
  out_styles[R_accent_2] = mkstyle(a[1], bg, 0, ri, 0, 0, 0);
  out_styles[R_accent_3] = mkstyle(a[2], bg, 0, 0, ru, 0, 0);
  out_styles[R_accent_4] = mkstyle(a[3], bg, 0, 0, 0, 0, 0);
  out_styles[R_prompt] = mkstyle(cyan, bg, 1, 0, 0, 0, 0);
  out_styles[R_note] = mkstyle(muted, bg, 0, 1, 0, 0, 0);
  out_styles[R_warning] = mkstyle(warning, bg, 0, 0, 0, 0, 0);
  out_styles[R_error] = mkstyle(error_c, bg, 1, 0, 0, 0, 0);
  out_styles[R_md_heading] = mkstyle(a[0], bg, 1, 0, 0, 0, 0);
  out_styles[R_md_emphasis] = mkstyle(fg, bg, 0, 1, 0, 0, 0);
  out_styles[R_md_strong] = mkstyle(fg, bg, 1, 0, 0, 0, 0);
  out_styles[R_md_code_inline] = mkstyle(a[2], code_bg, 0, 0, 0, 0, 0);
  out_styles[R_md_code_block] = mkstyle(fg, code_bg, 0, 0, 0, 0, 0);
  out_styles[R_md_code_label] = mkstyle(muted, bg, 0, 0, 0, 0, 0);
  out_styles[R_md_link] = mkstyle(cyan, bg, 0, 0, 1, 0, 0);
  out_styles[R_md_link_url] = mkstyle(muted, bg, 0, 0, 0, 0, 0);
  out_styles[R_md_quote] = mkstyle(muted, bg, 0, 1, 0, 0, 0);
  out_styles[R_md_list_marker] = mkstyle(a[0], bg, 0, 0, 0, 0, 0);
  out_styles[R_md_table_border] = mkstyle(border, bg, 0, 0, 0, 0, 0);
  out_styles[R_md_table_header] = mkstyle(fg, bg, 1, 0, 0, 0, 0);
  out_styles[R_md_rule] = mkstyle(border, bg, 0, 0, 0, 0, 0);
  out_styles[R_md_strikethrough] = mkstyle(muted, bg, 0, 0, 0, 1, 0);
  out_styles[R_diff_added] = mkstyle(green, bg, 0, 0, 0, 0, 0);
  out_styles[R_diff_removed] = mkstyle(error_c, bg, 0, 0, 0, 0, 0);
  out_styles[R_diff_context] = mkstyle(muted, bg, 0, 0, 0, 0, 0);
  out_styles[R_diff_added_word] = mkstyle(green, bg, 1, 0, 0, 0, 0);
  out_styles[R_diff_removed_word] = mkstyle(error_c, bg, 1, 0, 0, 0, 0);
  out_styles[R_input_text] = mkstyle(fg, bg, 0, 0, 0, 0, 0);
  out_styles[R_input_cursor] = mkstyle(bg, fg, 0, 0, 0, 0, 0);
  out_styles[R_input_placeholder] = mkstyle(muted, bg, 0, 1, 0, 0, 0);
  out_styles[R_scroll_marker] = mkstyle(bg, a[2], 1, 0, 0, 0, 0);
  out_styles[R_selection] = mkstyle(fg, sel, 0, 0, 0, 0, 0);
  out_styles[R_overlay] = mkstyle(muted, bg, 0, 0, 0, 1, 0);
  out_styles[R_menu_item] = mkstyle(fg, panel, 0, 0, 0, 0, 0);
  out_styles[R_menu_selected] = mkstyle(bg, a[0], 1, 0, 0, 0, 0);
  out_styles[R_menu_breadcrumb] = mkstyle(muted, panel, 0, 0, 0, 0, 0);
  out_styles[R_menu_shortcut] = mkstyle(a[2], panel, 0, 0, 0, 0, 0);
  out_styles[R_find_match] = mkstyle(fg, find_wash, 0, 0, 0, 0, 0);
  out_styles[R_find_current] = mkstyle(bg, a[2], 1, 0, 0, 0, 0);
  out_styles[R_scrollbar] = mkstyle(muted, bg, 0, 0, 0, 0, 0);

  /* ---- the repair loop: fix until the promised badges hold, or give up honestly ---------- */
  *out_repairs = 0;
  for (pass = 0; pass < max_repair_passes; ++pass) {
    RolltuiFixArray fixes;
    size_t fi;
    memset(&fixes, 0, sizeof fixes);
    rolltui_propose_fixes(out_styles, role_count, vocab, &fixes);
    if (fixes.n == 0) {
      rolltui_fix_array_release(&fixes);
      break;
    }
    if (rng_chance(&rng, chaos, 0.7)) {
      rolltui_fix_array_release(&fixes);
      break;
    }
    for (fi = 0; fi < fixes.n; ++fi) {
      if (rng_chance(&rng, chaos, 0.3)) continue;
      rolltui_apply_fix(out_styles, role_count, fixes.v[fi].role, &fixes.v[fi].after);
      ++*out_repairs;
    }
    rolltui_fix_array_release(&fixes);
  }

  /* ---- the final report: the badges the meta records, and what the shim builds "broken"
   * from --------------------------------------------------------------------------------- */
  if (!rolltui_theme_analyse(out_styles, role_count, out_roles, out_pairs, out_badges)) return 0;

  meta = rolltui_json_object();
  gen = rolltui_json_object();
  rolltui_json_set(gen, K("ruleset"), rolltui_json_string(rn, rnlen));
  rolltui_json_set(gen, K("seed"), rolltui_json_number((double)seed));
  rolltui_json_set(gen, K("chaos"), rolltui_json_number(chaos));
  rolltui_json_set(meta, K("generator"), gen);
  badges_arr = rolltui_json_array();
  memset(&bnames, 0, sizeof bnames);
  rolltui_badge_names(out_badges, &bnames);
  {
    size_t bi;
    for (bi = 0; bi < bnames.n; ++bi)
      rolltui_json_array_push(badges_arr, rolltui_json_string(bnames.v[bi].p, bnames.v[bi].n));
  }
  rolltui_str_array_release(&bnames);
  rolltui_json_set(meta, K("badges"), badges_arr);
  *out_meta = meta;
  return 1;
}
