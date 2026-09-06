/* rolltui/c/rolltui_theme_analysis.c — the colour maths. See rolltui_theme_analysis.h for
 * the boundary's rules and what deliberately stayed in `rolltui/ThemeAnalysis.cpp` (the
 * report and the auto-fix, both still C++ because `Theme`/`Role`/`json::Value` are). The
 * constants below are the published ones and `rolltui/tests/theme_analysis_test.cpp` holds
 * them to reference values; nothing here is tuned by eye. Nothing allocates. */
#include "rolltui/c/rolltui_theme_analysis.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_theme.h"

/* std::clamp/min/max have no C equivalent; three small helpers stand in for them
 * everywhere below (Phase 17 m1's own instance of the rule `rolltui_alloc.h` states for
 * allocation: a closed set of named helpers, not one invented per call site). */
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double maxd(double a, double b) { return a > b ? a : b; }
static double mind(double a, double b) { return a < b ? a : b; }

/* ---- sRGB <-> linear (IEC 61966-2-1) ---------------------------------------------------- */

double rolltui_srgb_channel_to_linear(double c) {
  c = clampd(c, 0.0, 1.0);
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

double rolltui_linear_channel_to_srgb(double v) {
  v = clampd(v, 0.0, 1.0);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

int rolltui_to_linear(RolltuiStyleColor c, RolltuiLin* out) {
  if (c.kind == 0) return 0; /* None: the terminal's own colour, unknown rather than assumed */
  if (c.kind == 1) rolltui_ansi_index_rgb(c.index, &c); /* Indexed -> the Rgb it resolves to */
  out->r = rolltui_srgb_channel_to_linear(c.r / 255.0);
  out->g = rolltui_srgb_channel_to_linear(c.g / 255.0);
  out->b = rolltui_srgb_channel_to_linear(c.b / 255.0);
  return 1;
}

static unsigned char encode_channel(double v) {
  return (unsigned char)lround(rolltui_linear_channel_to_srgb(v) * 255.0);
}

void rolltui_from_linear(RolltuiLin l, RolltuiStyleColor* out) {
  out->kind = 2; /* Rgb */
  out->index = 0;
  out->r = encode_channel(l.r);
  out->g = encode_channel(l.g);
  out->b = encode_channel(l.b);
}

/* ---- linear <-> OKLab <-> OKLCH (Björn Ottosson, 2020) ---------------------------------- */

void rolltui_linear_to_oklab(RolltuiLin c, RolltuiOkLab* out) {
  const double l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
  const double m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
  const double s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
  const double l_ = cbrt(l), m_ = cbrt(m), s_ = cbrt(s);
  out->L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
  out->a = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
  out->b = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;
}

void rolltui_oklab_to_linear(RolltuiOkLab lab, RolltuiLin* out) {
  const double l_ = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
  const double m_ = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
  const double s_ = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
  const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  out->r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  out->g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  out->b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

void rolltui_oklab_to_oklch(RolltuiOkLab lab, RolltuiOkLch* out) {
  double h = atan2(lab.b, lab.a) * 180.0 / M_PI;
  if (h < 0) h += 360.0;
  out->L = lab.L;
  out->C = sqrt(lab.a * lab.a + lab.b * lab.b);
  out->h = h;
}

void rolltui_oklch_to_oklab(RolltuiOkLch lch, RolltuiOkLab* out) {
  const double rad = lch.h * M_PI / 180.0;
  out->L = lch.L;
  out->a = lch.C * cos(rad);
  out->b = lch.C * sin(rad);
}

/* ---- contrast and distance --------------------------------------------------------------- */

double rolltui_relative_luminance(RolltuiLin l) { return 0.2126 * l.r + 0.7152 * l.g + 0.0722 * l.b; }

double rolltui_wcag_contrast(RolltuiLin a, RolltuiLin b) {
  const double la = rolltui_relative_luminance(a), lb = rolltui_relative_luminance(b);
  const double hi = maxd(la, lb), lo = mind(la, lb);
  return (hi + 0.05) / (lo + 0.05);
}

/* SAPC-4g (APCA 0.1.9): luminance with the APCA coefficients and exponent, a soft black
 * clamp, then the polarity-specific powers. */
static double apca_channel(double v) { return pow(clampd(rolltui_linear_channel_to_srgb(v), 0.0, 1.0), 2.4); }

static double apca_y(RolltuiLin c) {
  return 0.2126729 * apca_channel(c.r) + 0.7151522 * apca_channel(c.g) + 0.0721750 * apca_channel(c.b);
}

static double apca_clamp_black(double Y) { return Y < 0.022 ? Y + pow(0.022 - Y, 1.414) : Y; }

double rolltui_apca_contrast(RolltuiLin text, RolltuiLin bg) {
  const double ytxt = apca_clamp_black(apca_y(text));
  const double ybg = apca_clamp_black(apca_y(bg));
  double sapc;
  if (fabs(ybg - ytxt) < 0.0005) return 0.0;
  if (ybg > ytxt) {
    sapc = (pow(ybg, 0.56) - pow(ytxt, 0.57)) * 1.14;
    return sapc < 0.1 ? 0.0 : (sapc - 0.027) * 100.0;
  }
  sapc = (pow(ybg, 0.65) - pow(ytxt, 0.62)) * 1.14;
  return sapc > -0.1 ? 0.0 : (sapc + 0.027) * 100.0;
}

double rolltui_delta_e(RolltuiOkLab a, RolltuiOkLab b) {
  const double dl = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
  return sqrt(dl * dl + da * da + db * db);
}

static const char* const kCvdNames[ROLLTUI_CVD_COUNT] = {"protanopia", "deuteranopia", "tritanopia"};

const char* rolltui_cvd_name(unsigned char type, size_t* len) {
  const char* s = type < ROLLTUI_CVD_COUNT ? kCvdNames[type] : "";
  if (len) *len = strlen(s);
  return s;
}

/* Machado, Oliveira & Fernandes 2009, severity 1.0, in linear RGB. Rows sum to 1, so a grey
 * stays that grey (asserted in theme_analysis_test.cpp). An out-of-range `type` falls
 * through to the tritanopia matrix, exactly as the original C++'s `?:` chain did (it tested
 * Protanopia, then Deuteranopia, and used Tritanopia's matrix for anything else — including
 * the enum's third and only remaining legal value, but a byte crossing a C boundary has no
 * enum to enforce that, so the fallthrough is now the whole of the guarantee). */
void rolltui_simulate_cvd(RolltuiLin l, unsigned char type, RolltuiLin* out) {
  static const double kProtan[3][3] = {
      {0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}};
  static const double kDeutan[3][3] = {
      {0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}};
  static const double kTritan[3][3] = {
      {1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}};
  const double (*m)[3] = type == ROLLTUI_CVD_PROTANOPIA ? kProtan : type == ROLLTUI_CVD_DEUTERANOPIA ? kDeutan : kTritan;
  out->r = clampd(m[0][0] * l.r + m[0][1] * l.g + m[0][2] * l.b, 0.0, 1.0);
  out->g = clampd(m[1][0] * l.r + m[1][1] * l.g + m[1][2] * l.b, 0.0, 1.0);
  out->b = clampd(m[2][0] * l.r + m[2][1] * l.g + m[2][2] * l.b, 0.0, 1.0);
}

/* =========================================================================================
 * THE REPORT AND THE AUTO-FIX (Phase 17 m5) — ported verbatim from
 * `rolltui::analyse`/`report_text`/`check_claims`/`fix_contrast`/`fix_confusable`/
 * `propose_fixes`/`apply_fix` (`rolltui/ThemeAnalysis.cpp`, deleted at Phase 17 m2c — this
 * file is the only implementation now).
 * ========================================================================================= */

/* A literal C string plus its length via `strlen` — this file's own copy of
 * `rolltui_theme.c`'s `K` macro (same name, same purpose, independently duplicated for the
 * reason that file's own comment already gives: a file-local macro has no external linkage
 * to share). Used throughout `rolltui_theme_report_text`/the auto-fix `what` strings so a
 * literal's byte count is never hand-counted — `strlen` at report-generation rate costs
 * nothing this library's budget covers. */
#define K(s) (s), strlen(s)

/* ---- role ordinals: `R_<name>` is a file-local ALIAS of `ROLLTUI_ROLE_<NAME>`, generated
 * from `ROLLTUI_ROLE_LIST` (rolltui_style.h), the one spelling of the role vocabulary. Until
 * Phase 18 m1 this was a HAND-WRITTEN COPY of that list, with a comment saying its order "must
 * agree with rolltui::Role's declaration order (Style.hpp)" — a file that had been deleted
 * the day before. A copy can drift; an alias generated from the owner's list cannot. The
 * short form is kept only so the table below reads as pairs of roles rather than pairs of
 * numbers. Every entry point that takes `role_count` still checks it against
 * `ROLLTUI_ROLE_COUNT` before trusting a caller's array. */
#define ROLLTUI_R_ALIAS_(lower, UPPER) R_##lower = ROLLTUI_ROLE_##UPPER,
enum { ROLLTUI_ROLE_LIST(ROLLTUI_R_ALIAS_) };
#undef ROLLTUI_R_ALIAS_

/* =========================================================================================
 * THE MUST-DIFFER PAIRS — A LIBRARY RULE, CLOSED ON PURPOSE (Phase 18 m1, 2026-09-05).
 *
 * THE DECISION: which role pairs must be visually distinct is the LIBRARY's rule, not a
 * theme's, and this table is its one home. A theme file that states a `must_differ` key is
 * reported as an unknown key (`theme_test.cpp` asserts it), and `theme_analysis_test.cpp`
 * asserts these eleven BY NAME, so a change here is a decision recorded in a test rather than
 * an edit nothing notices.
 *
 * WHY THE LIBRARY AND NOT THE THEME — two reasons, and the second is decisive:
 *   1. Membership is a fact about RENDERING, which a theme cannot change. A pair is here
 *      because a library widget draws the two roles in the same place to mean different
 *      things, and the style is the cue a reader tells them apart by. A theme author can move
 *      every colour; they cannot make the menu draw a marker beside its selected row.
 *   2. The BADGES are claims checked against this set. `cvd-safe` means "every pair here is
 *      distinct under all three simulations", and `rolltui_check_claims` exists to catch a
 *      theme claiming a badge it did not earn. A pair set the theme writes is a test the
 *      claimant writes: a theme could declare zero pairs and be cvd-safe by construction, and
 *      `check_claims` would check nothing. CLAUDE.md opens with that self-satisfying shape
 *      (route everything to the fallback and "falls back when local can't" is satisfied);
 *      this is it one level down.
 *
 * WHAT A THEME AUTHOR CAN REACH: every pair, with its ΔE and per-CVD numbers, through
 * `rolltui_theme_analyse` (`RolltuiPairCheck.a`/`.b` are role ordinals; `rolltui_role_name`
 * spells them) and in `rolltui_theme_report_text`'s "must-differ pairs" section, which the
 * theme editor's `--check` popup prints verbatim. The rule is VISIBLE and not EDITABLE, and
 * that asymmetry is the decision — not an accident of where an array happened to live.
 *
 * THE REJECTED ALTERNATIVE: a theme-file `must_differ` array, with these eleven as the
 * default. It would let an author ADD a pair (a stricter self-check) or DROP one (declare a
 * widget's only cue unimportant). Dropping is reason 2 above. Adding is a bug report: a pair
 * an author needs is a widget drawing two roles with no other cue, which is this table
 * missing a row, not one theme's business. The one place a pair could legitimately come from
 * OUTSIDE the library is a HOST's registered widget, and no host has one — roll's
 * `approval`/`details` and paint's `canvas` draw no such pair — so that variant is deferred
 * with its trigger in `CONSIDERED.md` ("Host-declared must-differ pairs").
 *
 * MEMBERSHIP, one line per pair, naming what makes it load-bearing:
 *   diff_added / diff_removed        `rolltui_diff.c`: a line's role is its meaning.
 *   warning / error                  a transcript entry's severity is its role (roll maps
 *                                    `EntryKind::Warning`/`Error` to exactly these).
 *   accent_1..4, pairwise (six)      four accents exist to be four distinguishable classes;
 *                                    `rolltui_diff.c` and roll's escalation entry take one
 *                                    each, and a highlighter assigns them to token classes.
 *   menu_item / menu_selected        `rolltui_menu.c`: the selected row's style is the ONLY
 *                                    selection cue; no marker glyph is drawn.
 *   input_text / input_placeholder   `rolltui_input.c`: the placeholder is drawn where the
 *                                    text goes; style is the only cue.
 *   find_match / find_current        `rolltui_transcript.c`: the current match differs from
 *                                    the others by style alone.
 *   NOT a pair, by the same criterion: diff_added_word / diff_added — the word role is an
 *   emphasis ON its line (same hue, bold; `studio_golden_test.cpp` asserts the rendering).
 *
 * THE RULE FOR A NEW ROLE (it used to stand beside `Style.hpp`'s `kMustDiffer` and was lost
 * in the port): every role appended to `ROLLTUI_ROLE_LIST` decides its membership HERE — a
 * row, or a line saying "none" and why — before its addition is done. `rolltui_style.h` says
 * so at the list, which is where a new role is written.
 *
 * Sized `[ROLLTUI_MUST_DIFFER_COUNT]` on purpose: a count that drifted from the initialisers
 * below is a COMPILE ERROR, not a silent truncation.
 * ========================================================================================= */
typedef struct RolePairLocal { unsigned char a, b; } RolePairLocal;
static const RolePairLocal kMustDiffer[ROLLTUI_MUST_DIFFER_COUNT] = {
    {R_diff_added, R_diff_removed}, {R_warning, R_error},
    {R_accent_1, R_accent_2},       {R_accent_1, R_accent_3}, {R_accent_1, R_accent_4},
    {R_accent_2, R_accent_3},       {R_accent_2, R_accent_4}, {R_accent_3, R_accent_4},
    {R_menu_item, R_menu_selected}, {R_input_text, R_input_placeholder},
    {R_find_match, R_find_current},
};

size_t rolltui_must_differ_count(void) { return ROLLTUI_MUST_DIFFER_COUNT; }

/* Roles whose foreground is not text drawn for reading: frames, rules, fills. Mirrors
 * `rolltui::(anonymous namespace)::is_text_role` exactly. */
static int is_text_role(unsigned char r) {
  switch (r) {
    case R_background: case R_panel_background: case R_border: case R_border_active:
    case R_md_rule: case R_md_table_border: case R_overlay: case R_selection: case R_scroll_marker:
    case R_input_cursor:
      return 0;
    default:
      return 1;
  }
}

static RolltuiStyleColor effective_bg(const RolltuiStyle* styles, unsigned char r) {
  const RolltuiStyleColor own = styles[r].bg;
  if (own.kind != 0) return own;
  return styles[R_background].bg;
}

static int attrs_differ(const RolltuiStyle* a, const RolltuiStyle* b) {
  return a->bold != b->bold || a->italic != b->italic || a->underline != b->underline || a->dim != b->dim ||
         a->reverse != b->reverse;
}

/* An OKLCH colour brought into the sRGB gamut by pulling its chroma in — never by clamping
 * channels, which would move its lightness (the thing the fixes rely on). Mirrors
 * `rolltui::(anonymous namespace)::into_gamut` exactly. PUBLIC (declared in the header): also
 * called from `rolltui_theme_gen.c`, which is why this is not `static` — see the header's
 * comment on `rolltui_into_gamut` for why one copy replaces the C++'s two. */
void rolltui_into_gamut(RolltuiOkLch c, RolltuiLin* out) {
  int k;
  RolltuiOkLab lab;
  RolltuiLin lin;
  for (k = 0; k < 40; ++k) {
    rolltui_oklch_to_oklab(c, &lab);
    rolltui_oklab_to_linear(lab, &lin);
    if (lin.r >= -1e-6 && lin.g >= -1e-6 && lin.b >= -1e-6 && lin.r <= 1 + 1e-6 && lin.g <= 1 + 1e-6 &&
        lin.b <= 1 + 1e-6) {
      out->r = clampd(lin.r, 0.0, 1.0);
      out->g = clampd(lin.g, 0.0, 1.0);
      out->b = clampd(lin.b, 0.0, 1.0);
      return;
    }
    c.C *= 0.9;
  }
  {
    RolltuiOkLch c0 = c;
    c0.C = 0;
    rolltui_oklch_to_oklab(c0, &lab);
    rolltui_oklab_to_linear(lab, &lin);
  }
  out->r = clampd(lin.r, 0.0, 1.0);
  out->g = clampd(lin.g, 0.0, 1.0);
  out->b = clampd(lin.b, 0.0, 1.0);
}

/* The minimum OKLab dE between x and y across normal vision and all three CVD simulations —
 * `rolltui::fix_confusable`'s local `min_delta` lambda, ported verbatim. */
static double min_delta(RolltuiLin x, RolltuiLin y) {
  RolltuiOkLab lx, ly;
  double m;
  unsigned char t;
  rolltui_linear_to_oklab(x, &lx);
  rolltui_linear_to_oklab(y, &ly);
  m = rolltui_delta_e(lx, ly);
  for (t = 0; t < ROLLTUI_CVD_COUNT; ++t) {
    RolltuiLin cx, cy;
    RolltuiOkLab lcx, lcy;
    double d;
    rolltui_simulate_cvd(x, t, &cx);
    rolltui_simulate_cvd(y, t, &cy);
    rolltui_linear_to_oklab(cx, &lcx);
    rolltui_linear_to_oklab(cy, &lcy);
    d = rolltui_delta_e(lcx, lcy);
    m = mind(m, d);
  }
  return m;
}

/* A safe, bounds-checked read of `vocab->role_names[role]` — never NULL, `*len` 0 out of
 * range or with no vocab at all (a caller passing no vocab gets no role names, not a
 * crash — the same defensive shape every ordinal lookup in this library already takes). */
static const char* role_name_of(const RolltuiThemeVocab* vocab, unsigned char role, size_t* len) {
  if (!vocab || role >= vocab->role_count) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = strlen(vocab->role_names[role]);
  return vocab->role_names[role];
}

/* `snprintf("%.*f", digits, v)`, appended — this file's copy of `rolltui::(anonymous
 * namespace)::fmt`, which returned a `std::string`; here it appends directly to the
 * caller's growing buffer instead (the same "text out is UNBOUNDED, append to a RolltuiStr"
 * shape as everywhere else in this boundary). */
static void append_fmt(RolltuiStr* out, double v, int digits) {
  char buf[32];
  int n = snprintf(buf, sizeof buf, "%.*f", digits, v);
  if (n < 0) n = 0;
  if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
  rolltui_str_append(out, buf, (size_t)n);
}

/* ---- badges -------------------------------------------------------------------------- */

void rolltui_str_array_release(RolltuiStrArray* a) {
  size_t i;
  if (!a) return;
  for (i = 0; i < a->n; ++i) rolltui_str_free(&a->v[i]);
  rolltui_mem_free(a->v);
  memset(a, 0, sizeof *a);
}

static void str_array_add(RolltuiStrArray* a, const char* s, size_t len) {
  a->v = (RolltuiStr*)rolltui_grow_zeroed(a->v, &a->cap, a->n + 1, sizeof *a->v);
  rolltui_str_set(&a->v[a->n], s, len);
  a->n++;
}

void rolltui_badge_names(const RolltuiBadges* b, RolltuiStrArray* out) {
  rolltui_str_array_release(out);
  if (!b) return;
  if (b->dark) str_array_add(out, K("dark"));
  if (b->light) str_array_add(out, K("light"));
  if (b->high_contrast) str_array_add(out, K("high-contrast"));
  if (b->readable) str_array_add(out, K("readable"));
  if (b->cvd_safe) str_array_add(out, K("cvd-safe"));
  if (b->protan_safe) str_array_add(out, K("protan-safe"));
  if (b->deutan_safe) str_array_add(out, K("deutan-safe"));
  if (b->tritan_safe) str_array_add(out, K("tritan-safe"));
  if (b->mono) str_array_add(out, K("mono"));
  if (b->safe_16) str_array_add(out, K("16-safe"));
  if (b->safe_256) str_array_add(out, K("256-safe"));
  if (b->transparent) str_array_add(out, K("transparent"));
  if (b->attribute_redundant) str_array_add(out, K("attribute-redundant"));
}

int rolltui_has_badge(const RolltuiBadges* b, const char* name, size_t len) {
  RolltuiStrArray a;
  size_t i;
  int found;
  memset(&a, 0, sizeof a);
  rolltui_badge_names(b, &a);
  found = 0;
  for (i = 0; i < a.n; ++i)
    if (a.v[i].n == len && (len == 0 || memcmp(a.v[i].p, name, len) == 0)) { found = 1; break; }
  rolltui_str_array_release(&a);
  return found;
}

/* ---- analyse() ------------------------------------------------------------------------- */

int rolltui_theme_analyse(const RolltuiStyle* styles, size_t role_count, RolltuiRoleCheck* out_roles,
                          RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges) {
  size_t i;
  int all_none = 1, readable = 1, high = 1, any_text_known = 0;
  int distinct = 1, cvd_all = 1, per[3], redundant = 1, s16 = 1, s256 = 1, any_pair_known = 0;
  RolltuiLin bglin;
  if (!styles || !out_roles || !out_pairs || !out_badges) return 0;
  if (role_count != ROLLTUI_ROLE_COUNT) return 0;
  per[0] = per[1] = per[2] = 1;
  memset(out_badges, 0, sizeof *out_badges);

  for (i = 0; i < role_count; ++i) all_none &= (styles[i].fg.kind == 0 && styles[i].bg.kind == 0);
  out_badges->mono = (unsigned char)all_none;
  out_badges->transparent = (unsigned char)(styles[R_background].bg.kind == 0);
  if (rolltui_to_linear(styles[R_background].bg, &bglin)) {
    const double y = rolltui_relative_luminance(bglin);
    out_badges->dark = (unsigned char)(y <= 0.5);
    out_badges->light = (unsigned char)(y > 0.5);
  }

  for (i = 0; i < role_count; ++i) {
    RolltuiRoleCheck* c = &out_roles[i];
    RolltuiLin f, bl;
    memset(c, 0, sizeof *c);
    c->role = (unsigned char)i;
    c->text = (unsigned char)is_text_role((unsigned char)i);
    c->fg = styles[i].fg;
    c->bg = effective_bg(styles, (unsigned char)i);
    if (rolltui_to_linear(c->fg, &f) && rolltui_to_linear(c->bg, &bl)) {
      c->wcag = rolltui_wcag_contrast(f, bl);
      c->apca = rolltui_apca_contrast(f, bl);
      c->readable = (unsigned char)(c->wcag >= ROLLTUI_READABLE_RATIO);
      c->high = (unsigned char)(c->wcag >= ROLLTUI_HIGH_CONTRAST_RATIO);
      if (c->text) { any_text_known = 1; readable &= c->readable; high &= c->high; }
    } else {
      c->unknown = 1;
      if (c->text) { readable = 0; high = 0; }
    }
  }
  out_badges->readable = (unsigned char)(any_text_known && readable);
  out_badges->high_contrast = (unsigned char)(any_text_known && high);

  for (i = 0; i < ROLLTUI_MUST_DIFFER_COUNT; ++i) {
    RolltuiPairCheck* c = &out_pairs[i];
    const RolltuiStyle* sa = &styles[kMustDiffer[i].a];
    const RolltuiStyle* sb = &styles[kMustDiffer[i].b];
    RolltuiLin fa, fb;
    memset(c, 0, sizeof *c);
    c->a = kMustDiffer[i].a;
    c->b = kMustDiffer[i].b;
    c->attribute_redundant = (unsigned char)attrs_differ(sa, sb);
    redundant &= c->attribute_redundant;
    if (rolltui_to_linear(sa->fg, &fa) && rolltui_to_linear(sb->fg, &fb)) {
      RolltuiOkLab laba, labb;
      size_t k;
      int ok_all = 1;
      RolltuiStyleColor da, db;
      any_pair_known = 1;
      rolltui_linear_to_oklab(fa, &laba);
      rolltui_linear_to_oklab(fb, &labb);
      c->delta = rolltui_delta_e(laba, labb);
      c->distinct = (unsigned char)(c->delta >= ROLLTUI_DISTINCT_DELTA_E);
      for (k = 0; k < 3; ++k) {
        RolltuiLin ca, cb;
        RolltuiOkLab la, lb;
        int ok;
        rolltui_simulate_cvd(fa, (unsigned char)k, &ca);
        rolltui_simulate_cvd(fb, (unsigned char)k, &cb);
        rolltui_linear_to_oklab(ca, &la);
        rolltui_linear_to_oklab(cb, &lb);
        c->delta_cvd[k] = rolltui_delta_e(la, lb);
        ok = c->delta_cvd[k] >= ROLLTUI_DISTINCT_DELTA_E;
        ok_all &= ok;
        per[k] &= ok;
      }
      c->cvd_distinct = (unsigned char)ok_all;
      distinct &= c->distinct;
      cvd_all &= (c->distinct && c->cvd_distinct);
      da = sa->fg;
      db = sb->fg;
      rolltui_color_downgrade(&da, ROLLTUI_DEPTH_ANSI16);
      rolltui_color_downgrade(&db, ROLLTUI_DEPTH_ANSI16);
      c->collapses_16 = (unsigned char)(memcmp(&da, &db, sizeof da) == 0);
      da = sa->fg;
      db = sb->fg;
      rolltui_color_downgrade(&da, ROLLTUI_DEPTH_ANSI256);
      rolltui_color_downgrade(&db, ROLLTUI_DEPTH_ANSI256);
      c->collapses_256 = (unsigned char)(memcmp(&da, &db, sizeof da) == 0);
      s16 &= !c->collapses_16;
      s256 &= !c->collapses_256;
    } else {
      c->unknown = 1;
      distinct = 0;
      cvd_all = 0;
      per[0] = per[1] = per[2] = 0;
    }
  }
  out_badges->cvd_safe = (unsigned char)(any_pair_known && cvd_all);
  out_badges->protan_safe = (unsigned char)(any_pair_known && per[0]);
  out_badges->deutan_safe = (unsigned char)(any_pair_known && per[1]);
  out_badges->tritan_safe = (unsigned char)(any_pair_known && per[2]);
  out_badges->attribute_redundant = (unsigned char)redundant;
  out_badges->safe_16 = (unsigned char)(any_pair_known && s16);
  out_badges->safe_256 = (unsigned char)(any_pair_known && s256);
  /* Silence "set but not used" for the sequential accumulators once their final consumer
   * (the badge assignments above) has read them; `distinct` has no further reader, matching
   * `rolltui::analyse`, where the local of the same name is also write-only after its last
   * badge read. */
  (void)distinct;
  return 1;
}

/* ---- report_text() ----------------------------------------------------------------------
 * Composes the WHOLE report in C — see this header's top comment for why (rolltui_layout.c's
 * precedent, not rolltui_keys.h's). Mirrors `rolltui::report_text` line for line. */

void rolltui_theme_report_text(const RolltuiRoleCheck* roles, size_t role_count, const RolltuiPairCheck* pairs,
                               size_t pair_count, const RolltuiBadges* badges, const RolltuiStr* notes,
                               size_t notes_n, const RolltuiThemeVocab* vocab, RolltuiStr* out) {
  RolltuiStrArray bn;
  size_t i;
  memset(&bn, 0, sizeof bn);
  rolltui_badge_names(badges, &bn);
  rolltui_str_append(out, K("badges:"));
  for (i = 0; i < bn.n; ++i) {
    rolltui_str_append(out, K(" "));
    rolltui_str_append_str(out, &bn.v[i]);
  }
  if (bn.n == 0) rolltui_str_append(out, K(" (none)"));
  rolltui_str_array_release(&bn);
  rolltui_str_append(out, K("\n"));

  for (i = 0; i < notes_n; ++i) {
    rolltui_str_append(out, K("note: "));
    rolltui_str_append_str(out, &notes[i]);
    rolltui_str_append(out, K("\n"));
  }

  rolltui_str_append(out, K("\nroles (fg on bg; WCAG "));
  append_fmt(out, ROLLTUI_READABLE_RATIO, 1);
  rolltui_str_append(out, K(" readable, "));
  append_fmt(out, ROLLTUI_HIGH_CONTRAST_RATIO, 1);
  rolltui_str_append(out, K(" high; APCA Lc):\n"));
  for (i = 0; i < role_count; ++i) {
    const RolltuiRoleCheck* c = &roles[i];
    const char* rn;
    size_t rnlen;
    int pad;
    char cb1[ROLLTUI_COLOR_STRING_MAX], cb2[ROLLTUI_COLOR_STRING_MAX];
    size_t cl1, cl2;
    if (!c->text) continue;
    rn = role_name_of(vocab, c->role, &rnlen);
    rolltui_str_append(out, K("  "));
    rolltui_str_append(out, rn, rnlen);
    pad = 20 - (int)rnlen;
    if (pad > 0) {
      char sp[20];
      memset(sp, ' ', (size_t)pad);
      rolltui_str_append(out, sp, (size_t)pad);
    }
    cl1 = rolltui_color_to_string(c->fg, cb1, sizeof cb1);
    cl2 = rolltui_color_to_string(c->bg, cb2, sizeof cb2);
    if (c->unknown) {
      rolltui_str_append(out, K(" depends on the terminal ("));
      rolltui_str_append(out, cb1, cl1);
      rolltui_str_append(out, K(" on "));
      rolltui_str_append(out, cb2, cl2);
      rolltui_str_append(out, K(")\n"));
    } else {
      rolltui_str_append(out, K(" "));
      append_fmt(out, c->wcag, 2);
      rolltui_str_append(out, K(":1  APCA "));
      append_fmt(out, c->apca, 0);
      rolltui_str_append(out, K("  "));
      if (c->high) rolltui_str_append(out, K("high"));
      else if (c->readable) rolltui_str_append(out, K("readable"));
      else rolltui_str_append(out, K("FAIL"));
      rolltui_str_append(out, K("  ("));
      rolltui_str_append(out, cb1, cl1);
      rolltui_str_append(out, K(" on "));
      rolltui_str_append(out, cb2, cl2);
      rolltui_str_append(out, K(")\n"));
    }
  }

  rolltui_str_append(out, K("\nmust-differ pairs (OKLab dE >= "));
  append_fmt(out, ROLLTUI_DISTINCT_DELTA_E, 2);
  rolltui_str_append(out, K(", normal / protan / deutan / tritan):\n"));
  for (i = 0; i < pair_count; ++i) {
    const RolltuiPairCheck* c = &pairs[i];
    const char* an;
    size_t anlen;
    const char* bnm;
    size_t bnlen;
    an = role_name_of(vocab, c->a, &anlen);
    bnm = role_name_of(vocab, c->b, &bnlen);
    rolltui_str_append(out, K("  "));
    rolltui_str_append(out, an, anlen);
    rolltui_str_append(out, K(" / "));
    rolltui_str_append(out, bnm, bnlen);
    rolltui_str_append(out, K(": "));
    if (c->unknown) {
      rolltui_str_append(out, K("depends on the terminal"));
    } else {
      append_fmt(out, c->delta, 2);
      rolltui_str_append(out, K(" / "));
      append_fmt(out, c->delta_cvd[0], 2);
      rolltui_str_append(out, K(" / "));
      append_fmt(out, c->delta_cvd[1], 2);
      rolltui_str_append(out, K(" / "));
      append_fmt(out, c->delta_cvd[2], 2);
      if (c->distinct && c->cvd_distinct) rolltui_str_append(out, K("  ok"));
      else if (c->distinct) rolltui_str_append(out, K("  CONFUSABLE under a deficiency"));
      else rolltui_str_append(out, K("  CONFUSABLE"));
      if (c->collapses_16) rolltui_str_append(out, K("  (collapses at 16 colours)"));
      else if (c->collapses_256) rolltui_str_append(out, K("  (collapses at 256 colours)"));
    }
    if (c->attribute_redundant) rolltui_str_append(out, K("  +attr\n"));
    else rolltui_str_append(out, K("\n"));
  }
}

/* ---- check_claims() ---------------------------------------------------------------------- */

void rolltui_check_claims(const RolltuiJsonValue* meta, const RolltuiBadges* badges, RolltuiStrArray* out) {
  const RolltuiJsonValue* claims;
  size_t i, n;
  rolltui_str_array_release(out);
  if (!meta) return;
  claims = rolltui_json_get(meta, K("badges"));
  if (!rolltui_json_is_array(claims)) return;
  n = rolltui_json_array_size(claims);
  for (i = 0; i < n; ++i) {
    const RolltuiJsonValue* c = rolltui_json_array_at(claims, i);
    size_t len;
    const char* s;
    if (!rolltui_json_is_string(c)) continue;
    s = rolltui_json_as_string(c, "", 0, &len);
    if (!rolltui_has_badge(badges, s, len)) str_array_add(out, s, len);
  }
}

/* ---- auto-fix ------------------------------------------------------------------------------ */

void rolltui_fix_release(RolltuiFix* f) {
  if (!f) return;
  rolltui_str_free(&f->what);
  memset(f, 0, sizeof *f);
}

int rolltui_fix_contrast(const RolltuiStyle* styles, size_t role_count, unsigned char role, double target,
                         const RolltuiThemeVocab* vocab, RolltuiFix* out) {
  RolltuiStyle before, after;
  RolltuiLin f, bl;
  RolltuiStyleColor eb;
  double have, best, dir;
  RolltuiOkLab flab, blab;
  RolltuiOkLch bgc, c;
  int step;
  const char* rn;
  size_t rnlen;
  rolltui_fix_release(out);
  if (!styles || !out) return 0;
  if (role_count != ROLLTUI_ROLE_COUNT || role >= role_count) return 0;
  before = styles[role];
  if (!rolltui_to_linear(before.fg, &f)) return 0;
  eb = effective_bg(styles, role);
  if (!rolltui_to_linear(eb, &bl)) return 0;
  have = rolltui_wcag_contrast(f, bl);
  if (have >= target) return 0;
  rolltui_linear_to_oklab(bl, &blab);
  rolltui_oklab_to_oklch(blab, &bgc);
  rolltui_linear_to_oklab(f, &flab);
  rolltui_oklab_to_oklch(flab, &c);
  dir = c.L >= bgc.L ? 1.0 : -1.0;
  after = before;
  best = have;
  for (step = 1; step <= 100; ++step) {
    RolltuiOkLch t = c;
    RolltuiLin lin;
    double ratio;
    t.L = clampd(c.L + dir * 0.01 * step, 0.0, 1.0);
    rolltui_into_gamut(t, &lin);
    ratio = rolltui_wcag_contrast(lin, bl);
    if (ratio > best) {
      best = ratio;
      rolltui_from_linear(lin, &after.fg);
    }
    if (ratio >= target) break;
    if (t.L <= 0.0 || t.L >= 1.0) break;
  }
  if (best <= have) return 0;
  out->role = role;
  out->before = before;
  out->after = after;
  rn = role_name_of(vocab, role, &rnlen);
  rolltui_str_set(&out->what, rn, rnlen);
  rolltui_str_append(&out->what, K(" fg: contrast "));
  append_fmt(&out->what, have, 1);
  rolltui_str_append(&out->what, K(" \xE2\x86\x92 "));
  append_fmt(&out->what, best, 1);
  rolltui_str_append(&out->what, K(":1"));
  out->before_value = have;
  out->after_value = best;
  return 1;
}

int rolltui_fix_confusable(const RolltuiStyle* styles, size_t role_count, unsigned char a, unsigned char b,
                           const RolltuiThemeVocab* vocab, RolltuiFix* out) {
  RolltuiStyle sa, sb, after;
  RolltuiLin fa, fb;
  double have, best;
  RolltuiOkLab lab;
  RolltuiOkLch c;
  int step, passed;
  const char* an;
  size_t anlen;
  const char* bn;
  size_t bnlen;
  rolltui_fix_release(out);
  if (!styles || !out) return 0;
  if (role_count != ROLLTUI_ROLE_COUNT || a >= role_count || b >= role_count) return 0;
  sa = styles[a];
  sb = styles[b];
  if (!rolltui_to_linear(sa.fg, &fa) || !rolltui_to_linear(sb.fg, &fb)) return 0;
  have = min_delta(fa, fb);
  if (have >= ROLLTUI_DISTINCT_DELTA_E) return 0;
  rolltui_linear_to_oklab(fb, &lab);
  rolltui_oklab_to_oklch(lab, &c);
  best = have;
  after = sb;
  passed = 0;
  for (step = 1; step < 12; ++step) {
    RolltuiOkLch t = c;
    RolltuiLin lin;
    double d;
    t.h = fmod(c.h + 30.0 * step, 360.0);
    rolltui_into_gamut(t, &lin);
    d = min_delta(fa, lin);
    if (d > best) {
      best = d;
      rolltui_from_linear(lin, &after.fg);
    }
    if (d >= ROLLTUI_DISTINCT_DELTA_E) { passed = 1; break; }
  }
  an = role_name_of(vocab, a, &anlen);
  bn = role_name_of(vocab, b, &bnlen);
  if (!passed) {
    after = sb;
    if (!sb.underline && !sa.underline) after.underline = 1;
    else if (!sb.bold && !sa.bold) after.bold = 1;
    else if (!sb.italic && !sa.italic) after.italic = 1;
    else return 0;
    out->role = b;
    out->before = sb;
    out->after = after;
    rolltui_str_set(&out->what, an, anlen);
    rolltui_str_append(&out->what, K(" / "));
    rolltui_str_append(&out->what, bn, bnlen);
    rolltui_str_append(&out->what, K(": no hue satisfies every deficiency (best dE "));
    append_fmt(&out->what, best, 2);
    rolltui_str_append(&out->what, K("); add "));
    if (after.underline != sb.underline) rolltui_str_append(&out->what, K("underline"));
    else if (after.bold != sb.bold) rolltui_str_append(&out->what, K("bold"));
    else rolltui_str_append(&out->what, K("italic"));
    rolltui_str_append(&out->what, K(" to "));
    rolltui_str_append(&out->what, bn, bnlen);
    out->before_value = have;
    out->after_value = best;
    return 1;
  }
  out->role = b;
  out->before = sb;
  out->after = after;
  rolltui_str_set(&out->what, an, anlen);
  rolltui_str_append(&out->what, K(" / "));
  rolltui_str_append(&out->what, bn, bnlen);
  rolltui_str_append(&out->what, K(": min dE "));
  append_fmt(&out->what, have, 2);
  rolltui_str_append(&out->what, K(" \xE2\x86\x92 "));
  append_fmt(&out->what, best, 2);
  rolltui_str_append(&out->what, K(" (hue of "));
  rolltui_str_append(&out->what, bn, bnlen);
  rolltui_str_append(&out->what, K(" rotated)"));
  out->before_value = have;
  out->after_value = best;
  return 1;
}

void rolltui_fix_array_release(RolltuiFixArray* a) {
  size_t i;
  if (!a) return;
  for (i = 0; i < a->n; ++i) rolltui_str_free(&a->v[i].what);
  rolltui_mem_free(a->v);
  memset(a, 0, sizeof *a);
}

/* Appends `*f` to `*a` by MOVING it: a struct copy transfers `what`'s buffer, then `f` is
 * zeroed so its own (now redundant) release is a no-op and the array holds the only live
 * reference — avoids a double-free between a loop's reused local and the array it feeds. */
static void fix_array_add(RolltuiFixArray* a, RolltuiFix* f) {
  a->v = (RolltuiFix*)rolltui_grow_zeroed(a->v, &a->cap, a->n + 1, sizeof *a->v);
  a->v[a->n] = *f;
  memset(f, 0, sizeof *f);
  a->n++;
}

void rolltui_propose_fixes(const RolltuiStyle* styles, size_t role_count, const RolltuiThemeVocab* vocab,
                           RolltuiFixArray* out) {
  RolltuiRoleCheck roles[ROLLTUI_ROLE_COUNT];
  RolltuiPairCheck pairs[ROLLTUI_MUST_DIFFER_COUNT];
  RolltuiBadges badges;
  size_t i;
  rolltui_fix_array_release(out);
  if (role_count != ROLLTUI_ROLE_COUNT) return;
  if (!rolltui_theme_analyse(styles, role_count, roles, pairs, &badges)) return;
  for (i = 0; i < role_count; ++i) {
    if (roles[i].text && !roles[i].unknown && !roles[i].readable) {
      RolltuiFix f;
      memset(&f, 0, sizeof f);
      if (rolltui_fix_contrast(styles, role_count, (unsigned char)i, ROLLTUI_READABLE_RATIO, vocab, &f))
        fix_array_add(out, &f);
    }
  }
  for (i = 0; i < ROLLTUI_MUST_DIFFER_COUNT; ++i) {
    if (!pairs[i].unknown && !(pairs[i].distinct && pairs[i].cvd_distinct) && !pairs[i].attribute_redundant) {
      RolltuiFix f;
      memset(&f, 0, sizeof f);
      if (rolltui_fix_confusable(styles, role_count, pairs[i].a, pairs[i].b, vocab, &f)) fix_array_add(out, &f);
    }
  }
}

void rolltui_apply_fix(RolltuiStyle* styles, size_t role_count, unsigned char role, const RolltuiStyle* after) {
  if (!styles || !after || role >= role_count) return;
  styles[role] = *after;
}
