/* rolltui/c/rolltui_theme.c — the C side of the theme module. See rolltui_theme.h for the
 * boundary's rules and rolltui/Theme.hpp for the colour and file-format rules themselves; the
 * reference values in `rolltui/tests/theme_test.cpp` are the oracle for all of it.
 *
 * TWO PARTS, ported in two milestones and kept in one file because they are one module:
 *   Phase 15 m3 — THE COLOUR ENGINE (below, unchanged by m5): parsing/printing a colour,
 *     reduction, SGR emission, OSC 11. THE COLOUR LITERALS THERE ARE xterm's PUBLISHED
 *     PALETTE, not a theme's — the reference the 16-colour downgrade measures against.
 *     Nothing in that part allocates: every result goes into a caller's buffer sized from a
 *     constant in the header.
 *   Phase 15 m5 — THE BUILT-IN THEMES AND THE JSON LOADER/DUMPER (below the colour engine):
 *     this library's own TASTE (three compiled-in palettes) and its FILE FORMAT. THIS part
 *     allocates freely through `rolltui_alloc.h`'s closed set — a theme loads once per file,
 *     never per frame, so this is not the budget `rolltui-budget-test` holds the draw path to.
 * Both halves are exempted BY NAME in `theme_test`'s colour-literal grep control (the m3 half
 * for the xterm reference table, the m5 half for the three built-in themes' own colours —
 * `rolltui::Theme.cpp`'s taste, moved here) rather than by being in a directory the control
 * does not scan; the control also asserts each half still carries what it is exempt for, so
 * either one moving away silently fails a test instead of passing everywhere. */
#include "rolltui/c/rolltui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- parsing and printing --------------------------------------------------------------- */

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int rolltui_color_parse(const char* text, size_t len, RolltuiStyleColor* out) {
  size_t i;
  unsigned v = 0;
  if (len == 4 && text[0] == 'n' && text[1] == 'o' && text[2] == 'n' && text[3] == 'e') {
    out->kind = 0;
    out->index = 0;
    out->r = out->g = out->b = 0;
    return 1;
  }
  if (len == 7 && text[0] == '#') {
    for (i = 1; i < 7; ++i) {
      const int d = hex_digit(text[i]);
      if (d < 0) return 0;
      v = (v << 4) | (unsigned)d;
    }
    out->kind = 2;
    out->index = 0;
    out->r = (unsigned char)(v >> 16);
    out->g = (unsigned char)((v >> 8) & 0xFF);
    out->b = (unsigned char)(v & 0xFF);
    return 1;
  }
  if (len > 0 && len <= 3) {
    for (i = 0; i < len; ++i) {
      if (text[i] < '0' || text[i] > '9') return 0;
      v = v * 10 + (unsigned)(text[i] - '0');
    }
    if (v > 255) return 0;
    out->kind = 1;
    out->index = (unsigned char)v;
    out->r = out->g = out->b = 0;
    return 1;
  }
  return 0;
}

/* A byte as up to three decimal digits, appended. */
static size_t put_uint(char* out, unsigned v) {
  char tmp[12];
  size_t n = 0, i;
  do {
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  } while (v);
  for (i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
  return n;
}

static char hex_char(unsigned v) { return (char)(v < 10 ? '0' + v : 'a' + (v - 10)); }

size_t rolltui_color_to_string(RolltuiStyleColor c, char* out, size_t cap) {
  if (cap < ROLLTUI_COLOR_STRING_MAX) return 0;
  switch (c.kind) {
    case 1: return put_uint(out, c.index);
    case 2:
      out[0] = '#';
      out[1] = hex_char((unsigned)c.r >> 4);
      out[2] = hex_char((unsigned)c.r & 0xF);
      out[3] = hex_char((unsigned)c.g >> 4);
      out[4] = hex_char((unsigned)c.g & 0xF);
      out[5] = hex_char((unsigned)c.b >> 4);
      out[6] = hex_char((unsigned)c.b & 0xF);
      return 7;
    default:
      out[0] = 'n';
      out[1] = 'o';
      out[2] = 'n';
      out[3] = 'e';
      return 4;
  }
}

/* ---- the palette, and the nearest-colour search -------------------------------------------- */

typedef struct {
  int r, g, b;
} Rgb;

/* xterm's default 16-colour palette, the reference for the 16-colour downgrade. */
static const Rgb kSystem16[16] = {
    {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
};

static int cube_level(int x) { return x == 0 ? 0 : 55 + x * 40; }

static Rgb rgb_of_index(unsigned char i) {
  Rgb out;
  if (i < 16) return kSystem16[i];
  if (i < 232) {
    const int v = i - 16;
    out.r = cube_level(v / 36);
    out.g = cube_level((v / 6) % 6);
    out.b = cube_level(v % 6);
    return out;
  }
  out.r = out.g = out.b = 8 + (i - 232) * 10;
  return out;
}

static int dist2(Rgb a, Rgb b) {
  const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

static unsigned char nearest_256(Rgb c) {
  /* Search the cube and the grey ramp (the 16 system colours vary by terminal, so a
   * truecolor value is never mapped onto them). */
  int best = 1 << 30, i;
  unsigned char pick = 16;
  for (i = 16; i < 256; ++i) {
    const int d = dist2(c, rgb_of_index((unsigned char)i));
    if (d < best) {
      best = d;
      pick = (unsigned char)i;
    }
  }
  return pick;
}

static unsigned char nearest_16(Rgb c) {
  int best = 1 << 30, i;
  unsigned char pick = 0;
  for (i = 0; i < 16; ++i) {
    const int d = dist2(c, kSystem16[i]);
    if (d < best) {
      best = d;
      pick = (unsigned char)i;
    }
  }
  return pick;
}

void rolltui_ansi_index_rgb(unsigned char index, RolltuiStyleColor* out) {
  const Rgb c = rgb_of_index(index);
  out->kind = 2;
  out->index = 0;
  out->r = (unsigned char)c.r;
  out->g = (unsigned char)c.g;
  out->b = (unsigned char)c.b;
}

static RolltuiStyleColor indexed(unsigned char i) {
  RolltuiStyleColor c;
  c.kind = 1;
  c.index = i;
  c.r = c.g = c.b = 0;
  return c;
}

void rolltui_color_downgrade(RolltuiStyleColor* c, unsigned char depth) {
  Rgb v;
  if (c->kind == 0) return;
  v.r = c->r;
  v.g = c->g;
  v.b = c->b;
  switch (depth) {
    case ROLLTUI_DEPTH_TRUECOLOR: return;
    case ROLLTUI_DEPTH_ANSI256:
      if (c->kind == 2) *c = indexed(nearest_256(v));
      return;
    case ROLLTUI_DEPTH_ANSI16:
      if (c->kind == 2) *c = indexed(nearest_16(v));
      else if (c->index >= 16) *c = indexed(nearest_16(rgb_of_index(c->index)));
      return;
    case ROLLTUI_DEPTH_MONO:
      c->kind = 0;
      c->index = 0;
      c->r = c->g = c->b = 0;
      return;
    default: return;
  }
}

/* ---- the SGR sequence ---------------------------------------------------------------------- */

static size_t emit_color(char* out, RolltuiStyleColor c, int bg, unsigned char depth) {
  size_t n = 0;
  rolltui_color_downgrade(&c, depth);
  switch (c.kind) {
    case 1:
      out[n++] = ';';
      if (c.index < 8) {
        n += put_uint(out + n, (unsigned)((bg ? 40 : 30) + c.index));
      } else if (c.index < 16) {
        n += put_uint(out + n, (unsigned)((bg ? 100 : 90) + c.index - 8));
      } else {
        n += put_uint(out + n, (unsigned)(bg ? 48 : 38));
        out[n++] = ';';
        out[n++] = '5';
        out[n++] = ';';
        n += put_uint(out + n, c.index);
      }
      return n;
    case 2:
      out[n++] = ';';
      n += put_uint(out + n, (unsigned)(bg ? 48 : 38));
      out[n++] = ';';
      out[n++] = '2';
      out[n++] = ';';
      n += put_uint(out + n, c.r);
      out[n++] = ';';
      n += put_uint(out + n, c.g);
      out[n++] = ';';
      n += put_uint(out + n, c.b);
      return n;
    default: return 0;
  }
}

size_t rolltui_sgr(const RolltuiStyle* style, unsigned char depth, char* out, size_t cap) {
  size_t n = 0;
  if (cap < ROLLTUI_SGR_MAX) return 0;
  out[n++] = '\x1b';
  out[n++] = '[';
  out[n++] = '0';
  if (style->bold) { out[n++] = ';'; out[n++] = '1'; }
  if (style->dim) { out[n++] = ';'; out[n++] = '2'; }
  if (style->italic) { out[n++] = ';'; out[n++] = '3'; }
  if (style->underline) { out[n++] = ';'; out[n++] = '4'; }
  if (style->reverse) { out[n++] = ';'; out[n++] = '7'; }
  n += emit_color(out + n, style->fg, 0, depth);
  n += emit_color(out + n, style->bg, 1, depth);
  out[n++] = 'm';
  return n;
}

/* ---- the terminal's background ---------------------------------------------------------------- */

/* The index of `needle` in `hay`, or -1. Neither is NUL-terminated, so there is no `strstr`
 * to reach for and the search is the bytes it is. */
static long find_bytes(const char* hay, size_t hay_len, const char* needle, size_t needle_len) {
  size_t i, j;
  if (needle_len > hay_len) return -1;
  for (i = 0; i + needle_len <= hay_len; ++i) {
    for (j = 0; j < needle_len; ++j)
      if (hay[i + j] != needle[j]) break;
    if (j == needle_len) return (long)i;
  }
  return -1;
}

int rolltui_parse_osc11_reply(const char* reply, size_t len, RolltuiStyleColor* out) {
  /* ESC ] 11 ; rgb:RRRR/GGGG/BBBB (ESC \ | BEL), each channel 1-4 hex digits. */
  const long at = find_bytes(reply, len, "\x1b]11;", 5);
  const char* s;
  size_t n, i;
  long end, bel;
  unsigned char ch[3];
  int k;
  if (at < 0) return 0;
  s = reply + at + 5;
  n = len - (size_t)at - 5;
  end = find_bytes(s, n, "\x1b", 1);
  bel = find_bytes(s, n, "\a", 1);
  if (bel >= 0 && (end < 0 || bel < end)) end = bel;
  if (end >= 0) n = (size_t)end;
  if (n < 4 || s[0] != 'r' || s[1] != 'g' || s[2] != 'b' || s[3] != ':') return 0;
  s += 4;
  n -= 4;
  for (k = 0; k < 3; ++k) {
    const long slash = find_bytes(s, n, "/", 1);
    const size_t part_len = (k < 2) ? (slash < 0 ? 0 : (size_t)slash) : n;
    unsigned v = 0, max;
    if (k < 2 && slash < 0) return 0;
    if (part_len == 0 || part_len > 4) return 0;
    for (i = 0; i < part_len; ++i) {
      const int d = hex_digit(s[i]);
      if (d < 0) return 0;
      v = (v << 4) | (unsigned)d;
    }
    /* Scale to 8 bits from however many digits were given (4 → top byte; 1 → x*17). */
    max = (1u << (4 * part_len)) - 1;
    ch[k] = (unsigned char)((v * 255 + max / 2) / max);
    if (k < 2) {
      s += (size_t)slash + 1;
      n -= (size_t)slash + 1;
    }
  }
  out->kind = 2;
  out->index = 0;
  out->r = ch[0];
  out->g = ch[1];
  out->b = ch[2];
  return 1;
}

unsigned char rolltui_mode_for_background(RolltuiStyleColor bg) {
  double y;
  double lin[3];
  int i;
  const unsigned char raw[3] = {bg.r, bg.g, bg.b};
  if (bg.kind != 2) return ROLLTUI_MODE_DARK;
  for (i = 0; i < 3; ++i) {
    const double x = raw[i] / 255.0;
    lin[i] = x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
  }
  y = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
  return y > 0.5 ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK;
}

/* ---- role and effect-state ordinals, as file-local ALIASES of the library's own ------------
 * The loader and dumper below never use these: they resolve every role/state through the
 * caller's `RolltuiThemeVocab` table instead, exactly as this header's own comment states.
 * These exist for ONE reason — so the three built-in themes below read as
 * `styles[R_accent_1] = ...` instead of `styles[9] = ...`. `R_<name>` is
 * `ROLLTUI_ROLE_<NAME>` and `ST_<name>` is `ROLLTUI_EFFECT_STATE_<NAME>`, each generated from
 * the X-macro that owns the list (`ROLLTUI_ROLE_LIST`, `ROLLTUI_EFFECT_STATE_LIST`). Until
 * Phase 18 m1 both were HAND-WRITTEN COPIES whose comment said their order "must still agree
 * with rolltui::Role's declaration order (Style.hpp)" — a file deleted the day before, and
 * `rolltui_theme_analysis.c` carried a third copy. A copy can drift; an alias cannot.
 * `rolltui_theme_builtin_fill` below still checks `role_count` against `ROLLTUI_ROLE_COUNT`
 * before trusting a caller's array: a mismatch reads as "this theme doesn't exist" rather
 * than writing past its end. */
#define ROLLTUI_R_ALIAS_(lower, UPPER) R_##lower = ROLLTUI_ROLE_##UPPER,
enum { ROLLTUI_ROLE_LIST(ROLLTUI_R_ALIAS_) };
#undef ROLLTUI_R_ALIAS_
/* "none" is index 0 and never a spec a built-in (or a theme file) may address directly. */
#define ROLLTUI_ST_ALIAS_(lower, UPPER, Camel) ST_##lower = ROLLTUI_EFFECT_STATE_##UPPER,
enum { ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_ST_ALIAS_) };
#undef ROLLTUI_ST_ALIAS_

/* ---- small helpers shared by the built-ins and the loader --------------------------------- */

/* A literal C string plus its length, computed once here rather than hand-counted at every
 * call site — this file's own version of `rolltui_json.c`'s `JLIT` / `rolltui_app_profile.c`'s
 * `K` (the same macro name, same purpose, independently duplicated for the reason both of
 * those already state: a `static`/file-local helper has no external linkage to share). Theme
 * loading happens once per file, never per frame, so the `strlen` this costs is not one this
 * library's budget covers. */
#define K(s) (s), strlen(s)

static int streq(const char* s, size_t slen, const char* lit) {
  const size_t litlen = strlen(lit);
  return slen == litlen && (litlen == 0 || memcmp(s, lit, litlen) == 0);
}

/* Linear search against the caller's vocabulary table — this file's copy of
 * `rolltui::role_from_name`/`effect_state_from_name`'s own O(n) scan (Style.hpp, Effects.cpp),
 * not a faster reinvention. Returns the table's own count (the C spelling of `Role::count_` /
 * `EffectState::count_`: "no such name") when nothing matches. */
static size_t vocab_role_from_name(const RolltuiThemeVocab* vocab, const char* name, size_t len) {
  size_t i;
  for (i = 0; i < vocab->role_count; ++i)
    if (streq(name, len, vocab->role_names[i])) return i;
  return vocab->role_count;
}

static size_t vocab_state_from_name(const RolltuiThemeVocab* vocab, const char* name, size_t len) {
  size_t i;
  for (i = 0; i < vocab->state_count; ++i)
    if (streq(name, len, vocab->state_names[i])) return i;
  return vocab->state_count;
}

/* Report-append convenience: every message below is already built into a NUL-terminated
 * stack buffer (`snprintf` guarantees the NUL), so `strlen` here is the whole of turning that
 * into the `(ptr, len)` pair `rolltui_theme_report_add_*` wants. */
static void bad(RolltuiThemeReport* r, const char* s) { rolltui_theme_report_add_bad_value(r, s, strlen(s)); }
static void unk(RolltuiThemeReport* r, const char* s) { rolltui_theme_report_add_unknown_key(r, s, strlen(s)); }
static void missing(RolltuiThemeReport* r, const char* s) { rolltui_theme_report_add_missing_role(r, s, strlen(s)); }

/* ---- the built-in themes -------------------------------------------------------------------
 * `rolltui::Theme.cpp`'s own words, kept: this library's TASTE, not its algorithm. Every
 * comment below is that file's, reworded only where the C's shape forced it (`set(Role::x,
 * S(...))` becoming `styles[R_x] = mk(...)`); no reasoning was dropped. */

static RolltuiStyleColor rgbc(unsigned char r, unsigned char g, unsigned char b) {
  RolltuiStyleColor c;
  c.kind = 2; /* Rgb */
  c.index = 0;
  c.r = r;
  c.g = g;
  c.b = b;
  return c;
}

static RolltuiStyleColor nonec(void) {
  RolltuiStyleColor c;
  memset(&c, 0, sizeof c); /* kind 0 == None */
  return c;
}

static RolltuiStyle mk(RolltuiStyleColor fg, RolltuiStyleColor bg, int bold, int italic, int underline, int dim,
                       int reverse) {
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

/* ---- motion (Phase 12 m6), moved from Theme.cpp's `fx`/`frames` helpers -------------------
 * The theme's half of the effects contract: a widget says `waiting`, this says what waiting
 * LOOKS like. Every value below is expressible in a theme file (Theme.hpp's "effects"
 * object) and every one of these two maps is written into the shipped preset files that
 * carry the same name — the built-in and the file are one look with two definition sites,
 * kept equal by rolltui-presets-test. */

static void add_frames(RolltuiEffectMap* m, size_t state, size_t i, const char* const* fs, size_t n) {
  size_t j;
  for (j = 0; j < n; ++j) rolltui_effect_map_add_frame(m, state, i, fs[j], strlen(fs[j]));
}

static void add_role_spec(RolltuiEffectMap* m, size_t state, const char* kind, int period_ms, unsigned char role) {
  const size_t i = rolltui_effect_map_add(m, state, kind, strlen(kind), period_ms, 0, 0, 0);
  rolltui_effect_map_add_role(m, state, i, role);
}

/* For the two colour themes. The `waiting` spinner is BRAILLE (East Asian Neutral, so one
 * cell at any ambiguous-width setting — a two-cell frame would be refused by the applier's
 * width guarantee and the theme would silently stop moving). */
static void colour_effects(RolltuiEffectMap* m) {
  static const char* const waiting_frames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8",
                                               "\xE2\xA0\xBC", "\xE2\xA0\xB4", "\xE2\xA0\xA6", "\xE2\xA0\xA7"};
  size_t sweep;
  add_frames(m, ST_waiting, rolltui_effect_map_add(m, ST_waiting, K("spinner"), 640, 0, 0, 0), waiting_frames,
            sizeof waiting_frames / sizeof *waiting_frames);
  /* Bytes arriving move ALONG the text, so the sweep does too — and it is the accent, so a
   * reader who cannot see the motion still sees which span is live. */
  sweep = rolltui_effect_map_add(m, ST_streaming, K("shimmer"), 1200, /*width=*/ 6, 0, 0);
  rolltui_effect_map_add_role(m, ST_streaming, sweep, R_accent_1);
  /* A bar is a picture of a number and asks for NO tick: the number changing is already a
   * redraw (Effects.hpp — this is what "the tick runs only while something moves" is worth
   * in the shipped file, not only in the test). */
  add_role_spec(m, ST_progress, "bar", 0, R_accent_2);
  add_role_spec(m, ST_flash, "blink", 400, R_find_current);
}

/* The same four states, told with what a colourless terminal has. This is the pair the
 * design is FOR: same app, same widget code, a spinner that is ASCII here and braille
 * there, and a `streaming` that is a dim/normal breath rather than a colour sweep. */
static void mono_effects(RolltuiEffectMap* m) {
  static const char* const waiting_frames[] = {"|", "/", "-", "\\"};
  size_t breath;
  add_frames(m, ST_waiting, rolltui_effect_map_add(m, ST_waiting, K("spinner"), 400, 0, 0, 0), waiting_frames,
            sizeof waiting_frames / sizeof *waiting_frames);
  breath = rolltui_effect_map_add(m, ST_streaming, K("pulse"), 1200, 0, 0, 0);
  rolltui_effect_map_add_role(m, ST_streaming, breath, R_text_muted);
  rolltui_effect_map_add_role(m, ST_streaming, breath, R_text);
  add_role_spec(m, ST_progress, "bar", 0, R_menu_selected);  /* reverse video: the only "filled" this theme has */
  add_role_spec(m, ST_flash, "blink", 400, R_find_current);
}

/* set(Role::x, S(...)) -> styles[R_x] = mk(...): the C++ built the whole style
 * positionally too (a Role and a Style, nothing named 'set' survives crossing the
 * call), so this is the same construction with the array write spelled out instead
 * of hidden in a lambda capture. */
static void fill_default_dark(RolltuiStyle* styles, size_t role_count, RolltuiEffectMap* effects) {
  (void)role_count;
  // A restrained palette: text on a near-black ground, four accents, muted chrome.
  // The accents and the muted grey were re-picked 2026-09-02 by ThemeAnalysis
  // (milestone 15): the first cut's muted text missed 4.5:1 on the panel by a hair,
  // and blue/purple and green/yellow were confusable under protanopia and
  // deuteranopia. These five sit at hues 255/145/90/310/25 in OKLCH with their
  // lightness spread so every must-differ pair keeps an OKLab dE >= 0.13 under all
  // three simulations (a grid search, not taste) — rolltui-theme-analysis-test asserts
  // dark + readable + cvd-safe on this theme.
  const RolltuiStyleColor bg = rgbc(0x14, 0x16, 0x1A), panel = rgbc(0x1B, 0x1E, 0x24), fg = rgbc(0xD8, 0xDC, 0xE2);
  const RolltuiStyleColor muted = rgbc(0x85, 0x8D, 0x99), border = rgbc(0x3A, 0x40, 0x4A), border_active = rgbc(0x84, 0xB7, 0xF9);
  const RolltuiStyleColor blue = rgbc(0x84, 0xB7, 0xF9), green = rgbc(0xAD, 0xEE, 0xAE), yellow = rgbc(0xCB, 0xA6, 0x3A);
  const RolltuiStyleColor red = rgbc(0xC0, 0x6A, 0x64), purple = rgbc(0x9A, 0x73, 0xB8), cyan = rgbc(0x6C, 0xC8, 0xC8);
  const RolltuiStyleColor code_bg = rgbc(0x1E, 0x22, 0x28), sel = rgbc(0x2E, 0x44, 0x60), find_bg = rgbc(0x4A, 0x3E, 0x1C);
  styles[R_text] = mk(fg, bg, 0, 0, 0, 0, 0);
  styles[R_text_muted] = mk(muted, bg, 0, 0, 0, 0, 0);
  styles[R_background] = mk(fg, bg, 0, 0, 0, 0, 0);
  styles[R_panel_background] = mk(fg, panel, 0, 0, 0, 0, 0);
  styles[R_border] = mk(border, bg, 0, 0, 0, 0, 0);
  styles[R_border_active] = mk(border_active, bg, 0, 0, 0, 0, 0);
  styles[R_title] = mk(fg, bg, 1, 0, 0, 0, 0);
  styles[R_label] = mk(muted, panel, 0, 0, 0, 0, 0);
  styles[R_value] = mk(fg, panel, 0, 0, 0, 0, 0);
  styles[R_accent_1] = mk(blue, bg, 0, 0, 0, 0, 0);
  styles[R_accent_2] = mk(green, bg, 0, 0, 0, 0, 0);
  styles[R_accent_3] = mk(yellow, bg, 0, 0, 0, 0, 0);
  styles[R_accent_4] = mk(purple, bg, 0, 0, 0, 0, 0);
  styles[R_prompt] = mk(cyan, bg, 1, 0, 0, 0, 0);
  styles[R_note] = mk(muted, bg, 0, 1, 0, 0, 0);
  styles[R_warning] = mk(yellow, bg, 0, 0, 0, 0, 0);
  styles[R_error] = mk(red, bg, 1, 0, 0, 0, 0);
  styles[R_md_heading] = mk(blue, bg, 1, 0, 0, 0, 0);
  styles[R_md_emphasis] = mk(fg, bg, 0, 1, 0, 0, 0);
  styles[R_md_strong] = mk(fg, bg, 1, 0, 0, 0, 0);
  styles[R_md_code_inline] = mk(yellow, code_bg, 0, 0, 0, 0, 0);
  styles[R_md_code_block] = mk(fg, code_bg, 0, 0, 0, 0, 0);
  styles[R_md_code_label] = mk(muted, bg, 0, 0, 0, 0, 0);
  styles[R_md_link] = mk(cyan, bg, 0, 0, 1, 0, 0);
  styles[R_md_link_url] = mk(muted, bg, 0, 0, 0, 0, 0);
  styles[R_md_quote] = mk(muted, bg, 0, 1, 0, 0, 0);
  styles[R_md_list_marker] = mk(blue, bg, 0, 0, 0, 0, 0);
  styles[R_md_table_border] = mk(border, bg, 0, 0, 0, 0, 0);
  styles[R_md_table_header] = mk(fg, bg, 1, 0, 0, 0, 0);
  styles[R_md_rule] = mk(border, bg, 0, 0, 0, 0, 0);
  styles[R_md_strikethrough] = mk(muted, bg, 0, 0, 0, 1, 0);
  styles[R_diff_added] = mk(green, bg, 0, 0, 0, 0, 0);
  styles[R_diff_removed] = mk(red, bg, 0, 0, 0, 0, 0);
  styles[R_diff_context] = mk(muted, bg, 0, 0, 0, 0, 0);
  // m5b: the word run inside a changed PAIR. Same hue as its line — an emphasis, not a
  // second signal — so it costs no colour budget and cannot break a must-differ pair.
  styles[R_diff_added_word] = mk(green, bg, 1, 0, 0, 0, 0);
  styles[R_diff_removed_word] = mk(red, bg, 1, 0, 0, 0, 0);
  styles[R_input_text] = mk(fg, bg, 0, 0, 0, 0, 0);
  styles[R_input_cursor] = mk(bg, fg, 0, 0, 0, 0, 0);
  styles[R_input_placeholder] = mk(muted, bg, 0, 1, 0, 0, 0);
  styles[R_scroll_marker] = mk(bg, yellow, 1, 0, 0, 0, 0);
  styles[R_selection] = mk(fg, sel, 0, 0, 0, 0, 0);
  styles[R_overlay] = mk(muted, bg, 0, 0, 0, 1, 0);
  styles[R_menu_item] = mk(fg, panel, 0, 0, 0, 0, 0);
  styles[R_menu_selected] = mk(bg, blue, 1, 0, 0, 0, 0);
  styles[R_menu_breadcrumb] = mk(muted, panel, 0, 0, 0, 0, 0);
  styles[R_menu_shortcut] = mk(yellow, panel, 0, 0, 0, 0, 0);
  // Find (m4): every match is normal text on a dim amber ground — legible, and it keeps
  // the line's own shape. The current one is INVERTED on the accent, which is both the
  // strongest "you are here" a cell grid has and the reason the must-differ pair can be
  // measured at all (Style.hpp: the check reads `fg`, so a bg-only difference is
  // invisible to it).
  styles[R_find_match] = mk(fg, find_bg, 0, 0, 0, 0, 0);
  styles[R_find_current] = mk(bg, yellow, 1, 0, 0, 0, 0);
  // The thumb rides in the border column, so it is the border's brighter twin —
  // legible against the track without becoming a second accent.
  styles[R_scrollbar] = mk(muted, bg, 0, 0, 0, 0, 0);
  colour_effects(effects);
}

static void fill_default_light(RolltuiStyle* styles, size_t role_count, RolltuiEffectMap* effects) {
  (void)role_count;
  // Same story as the dark theme (2026-09-02): the light accents were confusable in
  // five pairs under deuteranopia and the muted grey missed 4.5:1 on the panel; these
  // are the grid search's pick at the same hues (a "yellow" readable on white is an
  // olive), min dE 0.12 under every simulation.
  const RolltuiStyleColor bg = rgbc(0xFA, 0xFA, 0xF8), panel = rgbc(0xEF, 0xF0, 0xF2), fg = rgbc(0x22, 0x26, 0x2C);
  const RolltuiStyleColor muted = rgbc(0x5F, 0x66, 0x70), border = rgbc(0xC8, 0xCC, 0xD2), border_active = rgbc(0x2D, 0x4E, 0x78);
  const RolltuiStyleColor blue = rgbc(0x2D, 0x4E, 0x78), green = rgbc(0x50, 0x7B, 0x51), yellow = rgbc(0x5E, 0x4B, 0x0C);
  const RolltuiStyleColor red = rgbc(0x4F, 0x1A, 0x18), purple = rgbc(0x40, 0x14, 0x59), cyan = rgbc(0x00, 0x7A, 0x8A);
  const RolltuiStyleColor code_bg = rgbc(0xF0, 0xF1, 0xF3), sel = rgbc(0xCC, 0xDF, 0xF5), find_bg = rgbc(0xF7, 0xE4, 0xA8);
  styles[R_text] = mk(fg, bg, 0, 0, 0, 0, 0);
  styles[R_text_muted] = mk(muted, bg, 0, 0, 0, 0, 0);
  styles[R_background] = mk(fg, bg, 0, 0, 0, 0, 0);
  styles[R_panel_background] = mk(fg, panel, 0, 0, 0, 0, 0);
  styles[R_border] = mk(border, bg, 0, 0, 0, 0, 0);
  styles[R_border_active] = mk(border_active, bg, 0, 0, 0, 0, 0);
  styles[R_title] = mk(fg, bg, 1, 0, 0, 0, 0);
  styles[R_label] = mk(muted, panel, 0, 0, 0, 0, 0);
  styles[R_value] = mk(fg, panel, 0, 0, 0, 0, 0);
  styles[R_accent_1] = mk(blue, bg, 0, 0, 0, 0, 0);
  styles[R_accent_2] = mk(green, bg, 0, 0, 0, 0, 0);
  styles[R_accent_3] = mk(yellow, bg, 0, 0, 0, 0, 0);
  styles[R_accent_4] = mk(purple, bg, 0, 0, 0, 0, 0);
  styles[R_prompt] = mk(cyan, bg, 1, 0, 0, 0, 0);
  styles[R_note] = mk(muted, bg, 0, 1, 0, 0, 0);
  styles[R_warning] = mk(yellow, bg, 0, 0, 0, 0, 0);
  styles[R_error] = mk(red, bg, 1, 0, 0, 0, 0);
  styles[R_md_heading] = mk(blue, bg, 1, 0, 0, 0, 0);
  styles[R_md_emphasis] = mk(fg, bg, 0, 1, 0, 0, 0);
  styles[R_md_strong] = mk(fg, bg, 1, 0, 0, 0, 0);
  styles[R_md_code_inline] = mk(purple, code_bg, 0, 0, 0, 0, 0);
  styles[R_md_code_block] = mk(fg, code_bg, 0, 0, 0, 0, 0);
  styles[R_md_code_label] = mk(muted, bg, 0, 0, 0, 0, 0);
  styles[R_md_link] = mk(blue, bg, 0, 0, 1, 0, 0);
  styles[R_md_link_url] = mk(muted, bg, 0, 0, 0, 0, 0);
  styles[R_md_quote] = mk(muted, bg, 0, 1, 0, 0, 0);
  styles[R_md_list_marker] = mk(blue, bg, 0, 0, 0, 0, 0);
  styles[R_md_table_border] = mk(border, bg, 0, 0, 0, 0, 0);
  styles[R_md_table_header] = mk(fg, bg, 1, 0, 0, 0, 0);
  styles[R_md_rule] = mk(border, bg, 0, 0, 0, 0, 0);
  styles[R_md_strikethrough] = mk(muted, bg, 0, 0, 0, 1, 0);
  styles[R_diff_added] = mk(green, bg, 0, 0, 0, 0, 0);
  styles[R_diff_removed] = mk(red, bg, 0, 0, 0, 0, 0);
  styles[R_diff_context] = mk(muted, bg, 0, 0, 0, 0, 0);
  // m5b: the word run inside a changed PAIR. Same hue as its line — an emphasis, not a
  // second signal — so it costs no colour budget and cannot break a must-differ pair.
  styles[R_diff_added_word] = mk(green, bg, 1, 0, 0, 0, 0);
  styles[R_diff_removed_word] = mk(red, bg, 1, 0, 0, 0, 0);
  styles[R_input_text] = mk(fg, bg, 0, 0, 0, 0, 0);
  styles[R_input_cursor] = mk(bg, fg, 0, 0, 0, 0, 0);
  styles[R_scroll_marker] = mk(bg, yellow, 1, 0, 0, 0, 0);
  styles[R_input_placeholder] = mk(muted, bg, 0, 1, 0, 0, 0);
  styles[R_selection] = mk(fg, sel, 0, 0, 0, 0, 0);
  styles[R_overlay] = mk(muted, bg, 0, 0, 0, 1, 0);
  styles[R_menu_item] = mk(fg, panel, 0, 0, 0, 0, 0);
  styles[R_menu_selected] = mk(bg, blue, 1, 0, 0, 0, 0);
  styles[R_menu_breadcrumb] = mk(muted, panel, 0, 0, 0, 0, 0);
  styles[R_menu_shortcut] = mk(purple, panel, 0, 0, 0, 0, 0);
  // Find (m4) — the dark theme's rule, read for a light ground: a pale amber wash for
  // every match, and the current one inverted on the olive that serves as this
  // palette's yellow.
  styles[R_find_match] = mk(fg, find_bg, 0, 0, 0, 0, 0);
  styles[R_find_current] = mk(bg, yellow, 1, 0, 0, 0, 0);
  styles[R_scrollbar] = mk(muted, bg, 0, 0, 0, 0, 0);
  colour_effects(effects);
}

/* Attributes only: every colour is the terminal's default. Emphasis by bold, dim,
 * italic, underline and reverse, which is what a 16-colour or high-contrast setup
 * can rely on. */
static void fill_mono(RolltuiStyle* styles, size_t role_count, RolltuiEffectMap* effects) {
  const RolltuiStyleColor n = nonec();
  memset(styles, 0, role_count * sizeof *styles); /* every role starts none/none, no attrs */
  // The four accents differ by attribute alone (milestone 15 found them identical):
  // bold, italic, underline, bold+italic.
  styles[R_accent_1] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_accent_2] = mk(n, n, 0, 1, 0, 0, 0);
  styles[R_accent_3] = mk(n, n, 0, 0, 1, 0, 0);
  styles[R_accent_4] = mk(n, n, 1, 1, 0, 0, 0);
  styles[R_text_muted] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_border] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_border_active] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_title] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_label] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_prompt] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_note] = mk(n, n, 0, 1, 0, 0, 0);
  styles[R_warning] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_error] = mk(n, n, 1, 0, 0, 0, 1);
  styles[R_md_heading] = mk(n, n, 1, 0, 1, 0, 0);
  styles[R_md_emphasis] = mk(n, n, 0, 1, 0, 0, 0);
  styles[R_md_strong] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_md_code_inline] = mk(n, n, 0, 0, 0, 0, 1);
  styles[R_md_code_label] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_md_link] = mk(n, n, 0, 0, 1, 0, 0);
  styles[R_md_link_url] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_md_quote] = mk(n, n, 0, 1, 0, 0, 0);
  styles[R_md_list_marker] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_md_table_border] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_md_table_header] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_md_rule] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_md_strikethrough] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_diff_added] = mk(n, n, 1, 0, 0, 0, 0);
  styles[R_diff_removed] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_diff_context] = mk(n, n, 0, 0, 0, 1, 0);
  // m5b, with no colour to spend: underline is the only attribute left, so it carries
  // the word run on top of whatever its line already uses.
  styles[R_diff_added_word] = mk(n, n, 1, 0, 1, 0, 0);
  styles[R_diff_removed_word] = mk(n, n, 0, 0, 1, 1, 0);
  styles[R_input_cursor] = mk(n, n, 0, 0, 0, 0, 1);
  styles[R_input_placeholder] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_scroll_marker] = mk(n, n, 1, 0, 0, 0, 1);
  styles[R_selection] = mk(n, n, 0, 0, 0, 0, 1);
  styles[R_overlay] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_menu_selected] = mk(n, n, 1, 0, 0, 0, 1);
  styles[R_menu_breadcrumb] = mk(n, n, 0, 0, 0, 1, 0);
  styles[R_menu_shortcut] = mk(n, n, 0, 0, 1, 0, 0);
  // Find (m4). With no colour to spend, the distinction is carried by attributes and
  // must still be a distinction: underline marks every match, bold+reverse the current
  // one — the same "inverted means here" this theme already uses for menu_selected.
  styles[R_find_match] = mk(n, n, 0, 0, 1, 0, 0);
  styles[R_find_current] = mk(n, n, 1, 0, 0, 0, 1);
  // With no colour, the thumb is the glyph's job (a solid block against the border
  // line); bold is what separates it from the track.
  styles[R_scrollbar] = mk(n, n, 1, 0, 0, 0, 0);
  mono_effects(effects);
}

size_t rolltui_theme_builtin_count(void) { return ROLLTUI_THEME_BUILTIN_COUNT; }

const char* rolltui_theme_builtin_name(size_t i) {
  /* Plain DATA (this header's own comment explains why a theme's name is not the vocabulary
   * the rest of this file stays out of): compiled-in literals, a BORROW valid for the
   * process's life. */
  static const char* const names[ROLLTUI_THEME_BUILTIN_COUNT] = {"default-dark", "default-light", "mono"};
  return i < ROLLTUI_THEME_BUILTIN_COUNT ? names[i] : NULL;
}

RolltuiEffectMap* rolltui_theme_builtin_fill(const char* name, size_t name_len, RolltuiStyle* styles,
                                             size_t role_count) {
  RolltuiEffectMap* m;
  void (*filler)(RolltuiStyle*, size_t, RolltuiEffectMap*);
  if (role_count != ROLLTUI_ROLE_COUNT) return NULL;
  if (streq(name, name_len, "default-dark")) filler = fill_default_dark;
  else if (streq(name, name_len, "default-light")) filler = fill_default_light;
  else if (streq(name, name_len, "mono")) filler = fill_mono;
  else return NULL;
  /* OWNED, LONG-LIVED (rolltui_alloc.h strategy 4), through the entry point
   * `rolltui_effect_map_new` already is. The fallback role is handed over here, once, the
   * same value `rolltui::EffectMap`'s default constructor already hands it. */
  m = rolltui_effect_map_new(ROLLTUI_EFFECT_STATE_COUNT, (unsigned char)R_accent_1);
  filler(styles, role_count, m);
  return m;
}

/* ---- the load report: mirrors rolltui::ThemeLoadReport field for field, and
 * `RolltuiAppProfileReport`'s own shape one file over ------------------------------------- */

void rolltui_theme_report_release(RolltuiThemeReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  for (i = 0; i < r->missing_roles_n; ++i) rolltui_str_free(&r->missing_roles[i]);
  rolltui_mem_free(r->missing_roles);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->bad_values_n; ++i) rolltui_str_free(&r->bad_values[i]);
  rolltui_mem_free(r->bad_values);
  memset(r, 0, sizeof *r);
}

void rolltui_theme_report_set_error(RolltuiThemeReport* r, const char* s, size_t len) {
  rolltui_str_set(&r->error, s, len);
}

void rolltui_theme_report_add_missing_role(RolltuiThemeReport* r, const char* s, size_t len) {
  /* GROWING AMORTISED (rolltui_alloc.h strategy 2): an array of small owned strings, the same
   * shape `rolltui_app_profile.c`'s own report arrays already use. */
  r->missing_roles = (RolltuiStr*)rolltui_grow_zeroed(r->missing_roles, &r->missing_roles_cap,
                                                       r->missing_roles_n + 1, sizeof *r->missing_roles);
  rolltui_str_set(&r->missing_roles[r->missing_roles_n++], s, len);
}

void rolltui_theme_report_add_unknown_key(RolltuiThemeReport* r, const char* s, size_t len) {
  r->unknown_keys = (RolltuiStr*)rolltui_grow_zeroed(r->unknown_keys, &r->unknown_keys_cap, r->unknown_keys_n + 1,
                                                     sizeof *r->unknown_keys);
  rolltui_str_set(&r->unknown_keys[r->unknown_keys_n++], s, len);
}

void rolltui_theme_report_add_bad_value(RolltuiThemeReport* r, const char* s, size_t len) {
  r->bad_values = (RolltuiStr*)rolltui_grow_zeroed(r->bad_values, &r->bad_values_cap, r->bad_values_n + 1,
                                                   sizeof *r->bad_values);
  rolltui_str_set(&r->bad_values[r->bad_values_n++], s, len);
}

/* ---- the JSON loader ----------------------------------------------------------------------
 * A direct port of `rolltui::(anonymous namespace)::resolve_color`/`read_role`/`EffectDraft`/
 * `read_effect`/`commit`/`read_effects` and `rolltui::load_theme`, working on
 * `RolltuiJsonValue*` directly (`rolltui/c/rolltui_json.h` — the parser this loader is BUILT
 * on, per this task's own instructions) instead of on a converted `json::Value` tree. Every
 * report message is byte-for-byte the original's; `rolltui-theme-test` is the oracle and
 * holds the assertions that say so. */

/* "where" paths are built with `snprintf` into a caller-owned stack buffer. Bounded because
 * the defs-cycle depth is capped at 4 and every piece appended (a role name, a JSON key, a
 * defs name) is itself short in practice; a diagnostic a human reads is allowed to TRUNCATE
 * past that rather than need unbounded storage — `rolltui_app_profile.c` already made the
 * same call with its own 320-byte buffers, sized up here for the deeper nesting a colour's
 * defs chain can reach. */
#define ROLLTUI_THEME_WHERE_MAX 512

/* Resolves one colour value: string forms (`#rrggbb` | `0-255` | `none`), an integer 0-255, a
 * `defs` reference (recursive, depth-capped), or a {"dark":..,"light":..} pair. 1 on success
 * with `*out` filled; 0 on failure, with `report` already explaining why. */
static int resolve_color(const RolltuiJsonValue* v, const RolltuiJsonValue* defs, int mode, const char* where,
                         RolltuiThemeReport* report, int depth, RolltuiStyleColor* out) {
  char buf[ROLLTUI_THEME_WHERE_MAX];
  if (depth > 4) {
    snprintf(buf, sizeof buf, "%s: defs reference cycle", where);
    bad(report, buf);
    return 0;
  }
  if (rolltui_json_is_number(v)) {
    const double d = rolltui_json_as_number(v, 0);
    if (d < 0 || d > 255 || d != floor(d)) {
      snprintf(buf, sizeof buf, "%s: %f is not an ANSI index 0-255", where, d);
      bad(report, buf);
      return 0;
    }
    out->kind = 1; /* Indexed */
    out->index = (unsigned char)d;
    out->r = out->g = out->b = 0;
    return 1;
  }
  if (rolltui_json_is_string(v)) {
    size_t slen;
    const char* s = rolltui_json_as_string(v, "", 0, &slen);
    if (rolltui_color_parse(s, slen, out)) return 1;
    if (rolltui_json_has(defs, s, slen)) {
      char where2[ROLLTUI_THEME_WHERE_MAX];
      snprintf(where2, sizeof where2, "%s \xe2\x86\x92 defs.%s", where, s);
      return resolve_color(rolltui_json_get(defs, s, slen), defs, mode, where2, report, depth + 1, out);
    }
    snprintf(buf, sizeof buf, "%s: '%s' is not a colour (#rrggbb, 0-255, none, or a defs name)", where, s);
    bad(report, buf);
    return 0;
  }
  if (rolltui_json_is_object(v)) {
    const char* key = mode == ROLLTUI_MODE_DARK ? "dark" : "light";
    size_t i, n;
    if (!rolltui_json_has(v, key, strlen(key))) {
      snprintf(buf, sizeof buf, "%s: missing \"%s\" variant", where, key);
      bad(report, buf);
      return 0;
    }
    n = rolltui_json_object_size(v);
    for (i = 0; i < n; ++i) {
      size_t klen;
      const char* k = rolltui_json_object_key_at(v, i, &klen);
      if (!streq(k, klen, "dark") && !streq(k, klen, "light")) {
        snprintf(buf, sizeof buf, "%s.%s", where, k);
        unk(report, buf);
      }
    }
    {
      char where2[ROLLTUI_THEME_WHERE_MAX];
      snprintf(where2, sizeof where2, "%s.%s", where, key);
      return resolve_color(rolltui_json_get(v, key, strlen(key)), defs, mode, where2, report, depth + 1, out);
    }
  }
  snprintf(buf, sizeof buf, "%s: expected a colour", where);
  bad(report, buf);
  return 0;
}

/* Reads one role's fg/bg/bold/italic/underline/dim/reverse object, inheriting `base` for any
 * field it does not set — mirrors `read_role` exactly, including a bad fg/bg/attribute
 * leaving that ONE field at `base` rather than failing the whole role. */
static void read_role_style(const RolltuiJsonValue* v, const RolltuiStyle* base, const RolltuiJsonValue* defs,
                            int mode, const char* where, RolltuiThemeReport* report, RolltuiStyle* out) {
  char buf[ROLLTUI_THEME_WHERE_MAX];
  size_t i, n;
  *out = *base;
  if (!rolltui_json_is_object(v)) {
    snprintf(buf, sizeof buf, "%s: expected an object", where);
    bad(report, buf);
    return;
  }
  n = rolltui_json_object_size(v);
  for (i = 0; i < n; ++i) {
    size_t klen;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, i);
    if (streq(k, klen, "fg") || streq(k, klen, "bg")) {
      char where2[ROLLTUI_THEME_WHERE_MAX];
      RolltuiStyleColor c;
      snprintf(where2, sizeof where2, "%s.%s", where, k);
      if (resolve_color(x, defs, mode, where2, report, 0, &c)) {
        if (streq(k, klen, "fg")) out->fg = c;
        else out->bg = c;
      }
    } else if (streq(k, klen, "bold") || streq(k, klen, "italic") || streq(k, klen, "underline") ||
              streq(k, klen, "dim") || streq(k, klen, "reverse")) {
      const RolltuiJsonValue* b = x;
      if (rolltui_json_is_object(x)) {
        const char* key = mode == ROLLTUI_MODE_DARK ? "dark" : "light";
        if (!rolltui_json_has(x, key, strlen(key))) {
          snprintf(buf, sizeof buf, "%s.%s: missing \"%s\" variant", where, k, key);
          bad(report, buf);
          continue;
        }
        b = rolltui_json_get(x, key, strlen(key));
      }
      if (!rolltui_json_is_bool(b)) {
        snprintf(buf, sizeof buf, "%s.%s: expected true or false", where, k);
        bad(report, buf);
        continue;
      }
      {
        const unsigned char bv = (unsigned char)rolltui_json_as_bool(b, 0);
        if (streq(k, klen, "bold")) out->bold = bv;
        else if (streq(k, klen, "italic")) out->italic = bv;
        else if (streq(k, klen, "underline")) out->underline = bv;
        else if (streq(k, klen, "dim")) out->dim = bv;
        else out->reverse = bv;
      }
    } else {
      snprintf(buf, sizeof buf, "%s.%s", where, k);
      unk(report, buf);
    }
  }
}

/* ---- "effects": state -> what it looks like while it lasts (Phase 12 m6) -----------------
 * The KIND is deliberately not judged here either: rung 2 belongs to whoever registered it,
 * and a theme file is read long before a host has registered anything (Effects.hpp). A
 * BORROW of the JSON tree's own bytes (`EffectFrameRef`), not a copy: the tree stays alive
 * for the whole of `rolltui_theme_load`, and `commit_draft` copies into the map before the
 * caller ever frees it — the same "borrow while the source lives, copy at the owning point"
 * rule the map's own `rolltui_effect_map_add_frame` already states. */

typedef struct {
  const char* p;
  size_t n;
} EffectFrameRef;

typedef struct {
  const char* kind;
  size_t kind_len;
  EffectFrameRef* frames;
  size_t frames_n, frames_cap; /* GROWING AMORTISED (strategy 2): a handful of borrows */
  unsigned char* roles;
  size_t roles_n, roles_cap; /* GROWING AMORTISED (strategy 2) */
  int period_ms, width, steps;
  unsigned char backward;
} EffectDraft;

static void draft_free(EffectDraft* d) {
  rolltui_mem_free(d->frames);
  rolltui_mem_free(d->roles);
  memset(d, 0, sizeof *d);
}

static void role_one(const RolltuiJsonValue* n, const char* w, const RolltuiThemeVocab* vocab,
                     RolltuiThemeReport* report, EffectDraft* out) {
  char buf[ROLLTUI_THEME_WHERE_MAX];
  size_t nlen, r;
  const char* name;
  if (!rolltui_json_is_string(n)) {
    snprintf(buf, sizeof buf, "%s: expected a role name", w);
    bad(report, buf);
    return;
  }
  name = rolltui_json_as_string(n, "", 0, &nlen);
  r = vocab_role_from_name(vocab, name, nlen);
  if (r == vocab->role_count) {
    snprintf(buf, sizeof buf, "%s: '%s' is not a role", w, name);
    bad(report, buf);
    return;
  }
  out->roles = (unsigned char*)rolltui_grow(out->roles, &out->roles_cap, out->roles_n + 1, sizeof *out->roles);
  out->roles[out->roles_n++] = (unsigned char)r;
}

static void roles_from(const RolltuiJsonValue* x, const char* at, const RolltuiThemeVocab* vocab,
                       RolltuiThemeReport* report, EffectDraft* out) {
  if (rolltui_json_is_array(x)) {
    const size_t n = rolltui_json_array_size(x);
    size_t i;
    for (i = 0; i < n; ++i) {
      char w[ROLLTUI_THEME_WHERE_MAX];
      snprintf(w, sizeof w, "%s[%zu]", at, i);
      role_one(rolltui_json_array_at(x, i), w, vocab, report, out);
    }
  } else {
    role_one(x, at, vocab, report, out);
  }
}

/* Parses ONE effect spec object. 1 when `out` is usable (the caller still owns it — and must
 * `draft_free` it either way); 0 when it is not (the report already explains why). */
static int read_effect(const RolltuiJsonValue* v, const char* where, const RolltuiThemeVocab* vocab,
                       RolltuiUnicodeScratch* scratch, RolltuiThemeReport* report, EffectDraft* out) {
  char buf[ROLLTUI_THEME_WHERE_MAX];
  size_t i, n;
  memset(out, 0, sizeof *out);
  out->period_ms = 800; /* EffectDraft's own default, same as the C++ member initialiser */
  if (!rolltui_json_is_object(v)) {
    snprintf(buf, sizeof buf, "%s: expected an effect object", where);
    bad(report, buf);
    return 0;
  }
  n = rolltui_json_object_size(v);
  for (i = 0; i < n; ++i) {
    size_t klen;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, i);
    if (streq(k, klen, "kind")) {
      size_t slen;
      const char* s = rolltui_json_as_string(x, "", 0, &slen);
      if (!rolltui_json_is_string(x) || slen == 0) {
        snprintf(buf, sizeof buf, "%s.kind: expected an effect kind name", where);
        bad(report, buf);
        continue;
      }
      out->kind = s;
      out->kind_len = slen;
    } else if (streq(k, klen, "frames")) {
      size_t an, j;
      if (!rolltui_json_is_array(x)) {
        snprintf(buf, sizeof buf, "%s.frames: expected an array of strings", where);
        bad(report, buf);
        continue;
      }
      an = rolltui_json_array_size(x);
      for (j = 0; j < an; ++j) {
        const RolltuiJsonValue* e = rolltui_json_array_at(x, j);
        if (!rolltui_json_is_string(e)) {
          snprintf(buf, sizeof buf, "%s.frames[%zu]: expected a string", where, j);
          bad(report, buf);
          continue;
        }
        {
          size_t flen;
          const char* f = rolltui_json_as_string(e, "", 0, &flen);
          EffectFrameRef ref;
          ref.p = f;
          ref.n = flen;
          out->frames =
              (EffectFrameRef*)rolltui_grow(out->frames, &out->frames_cap, out->frames_n + 1, sizeof *out->frames);
          out->frames[out->frames_n++] = ref;
        }
      }
    } else if (streq(k, klen, "role") || streq(k, klen, "roles")) {
      char at[ROLLTUI_THEME_WHERE_MAX];
      snprintf(at, sizeof at, "%s.%s", where, k);
      roles_from(x, at, vocab, report, out);
    } else if (streq(k, klen, "period_ms") || streq(k, klen, "width") || streq(k, klen, "steps")) {
      int val;
      if (!rolltui_json_is_number(x)) {
        snprintf(buf, sizeof buf, "%s.%s: expected a number", where, k);
        bad(report, buf);
        continue;
      }
      val = (int)rolltui_json_as_number(x, 0);
      if (streq(k, klen, "period_ms")) out->period_ms = val;
      else if (streq(k, klen, "width")) out->width = val;
      else out->steps = val;
    } else if (streq(k, klen, "backward")) {
      if (!rolltui_json_is_bool(x)) {
        snprintf(buf, sizeof buf, "%s.backward: expected true or false", where);
        bad(report, buf);
        continue;
      }
      out->backward = (unsigned char)rolltui_json_as_bool(x, 0);
    } else {
      snprintf(buf, sizeof buf, "%s.%s", where, k);
      unk(report, buf);
    }
  }
  if (out->kind_len == 0) {
    snprintf(buf, sizeof buf, "%s: no \"kind\"", where);
    bad(report, buf);
    return 0;
  }
  /* The equal-width rule. Checked HERE rather than left to the applier's clamp because a
   * theme author can fix a file and a running frame cannot: the clamp is the guarantee, this
   * is the message. */
  if (out->frames_n > 0) {
    const int w = rolltui_u_display_width(scratch, out->frames[0].p, out->frames[0].n, 0);
    if (w <= 0) {
      snprintf(buf, sizeof buf, "%s.frames[0]: a frame must be at least one cell wide", where);
      bad(report, buf);
    }
    for (i = 1; i < out->frames_n; ++i) {
      if (rolltui_u_display_width(scratch, out->frames[i].p, out->frames[i].n, 0) != w) {
        snprintf(buf, sizeof buf, "%s.frames[%zu]: every frame must be %d cells wide (an effect never changes a span's width)",
                where, i, w);
        bad(report, buf);
        break;
      }
    }
  }
  return 1;
}

static void commit_draft(RolltuiEffectMap* map, size_t state, const EffectDraft* d) {
  size_t i;
  const size_t idx = rolltui_effect_map_add(map, state, d->kind, d->kind_len, d->period_ms, d->width, d->steps,
                                            d->backward);
  for (i = 0; i < d->frames_n; ++i) rolltui_effect_map_add_frame(map, state, idx, d->frames[i].p, d->frames[i].n);
  for (i = 0; i < d->roles_n; ++i) rolltui_effect_map_add_role(map, state, idx, d->roles[i]);
}

static void read_effects(const RolltuiJsonValue* v, const RolltuiThemeVocab* vocab, RolltuiUnicodeScratch* scratch,
                         RolltuiThemeReport* report, RolltuiEffectMap* map) {
  size_t n, i;
  if (rolltui_json_is_null(v)) return; /* no "effects" key: a still UI, and not a problem */
  if (!rolltui_json_is_object(v)) {
    bad(report, "effects: expected an object of state \xe2\x86\x92 effect");
    return;
  }
  n = rolltui_json_object_size(v);
  for (i = 0; i < n; ++i) {
    size_t klen;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, i);
    const size_t state = vocab_state_from_name(vocab, k, klen);
    char where[ROLLTUI_THEME_WHERE_MAX];
    if (state == vocab->state_count || state == 0) {
      char buf[ROLLTUI_THEME_WHERE_MAX];
      snprintf(buf, sizeof buf, "effects.%s", k);
      unk(report, buf);
      continue;
    }
    snprintf(where, sizeof where, "effects.%s", k);
    if (rolltui_json_is_array(x)) {
      const size_t an = rolltui_json_array_size(x);
      size_t j;
      for (j = 0; j < an; ++j) {
        char where2[ROLLTUI_THEME_WHERE_MAX];
        EffectDraft d;
        snprintf(where2, sizeof where2, "%s[%zu]", where, j);
        if (read_effect(rolltui_json_array_at(x, j), where2, vocab, scratch, report, &d)) commit_draft(map, state, &d);
        draft_free(&d);
      }
    } else {
      EffectDraft d;
      if (read_effect(x, where, vocab, scratch, report, &d)) commit_draft(map, state, &d);
      draft_free(&d);
    }
  }
}

RolltuiEffectMap* rolltui_theme_load(const RolltuiJsonValue* root, int mode, const RolltuiThemeVocab* vocab,
                                     RolltuiStyle* out_styles, RolltuiStr* out_name, RolltuiThemeReport* report) {
  const RolltuiJsonValue *defs, *roles, *effects_v;
  RolltuiUnicodeScratch* scratch;
  RolltuiEffectMap* map;
  RolltuiStyle default_style, text_style;
  size_t i, n;

  rolltui_theme_report_release(report); /* full reset, mirrors `report = ThemeLoadReport{};` */

  if (!rolltui_json_is_object(root)) {
    rolltui_theme_report_set_error(report, K("theme file must be a JSON object"));
    return NULL;
  }
  n = rolltui_json_object_size(root);
  for (i = 0; i < n; ++i) {
    size_t klen;
    const char* k = rolltui_json_object_key_at(root, i, &klen);
    if (!streq(k, klen, "name") && !streq(k, klen, "defs") && !streq(k, klen, "roles") &&
        !streq(k, klen, "meta") && !streq(k, klen, "effects"))
      unk(report, k);
  }
  defs = rolltui_json_get(root, K("defs"));
  roles = rolltui_json_get(root, K("roles"));
  if (!rolltui_json_is_object(roles)) {
    rolltui_theme_report_set_error(report, K("theme file has no \"roles\" object"));
    return NULL;
  }
  n = rolltui_json_object_size(defs);
  for (i = 0; i < n; ++i) {
    size_t klen;
    const char* k = rolltui_json_object_key_at(defs, i, &klen);
    const RolltuiJsonValue* v = rolltui_json_object_value_at(defs, i);
    if (!rolltui_json_is_string(v) && !rolltui_json_is_number(v) && !rolltui_json_is_object(v)) {
      char buf[ROLLTUI_THEME_WHERE_MAX];
      snprintf(buf, sizeof buf, "defs.%s: expected a colour", k);
      bad(report, buf);
    }
  }

  {
    size_t nlen;
    const char* nm = rolltui_json_as_string(rolltui_json_get(root, K("name")), "unnamed", 7, &nlen);
    rolltui_str_set(out_name, nm, nlen);
  }

  n = rolltui_json_object_size(roles);
  for (i = 0; i < n; ++i) {
    size_t klen;
    const char* k = rolltui_json_object_key_at(roles, i, &klen);
    if (vocab_role_from_name(vocab, k, klen) == vocab->role_count) {
      char buf[ROLLTUI_THEME_WHERE_MAX];
      snprintf(buf, sizeof buf, "roles.%s", k);
      unk(report, buf);
    }
  }

  memset(&default_style, 0, sizeof default_style);
  if (rolltui_json_has(roles, K("text"))) {
    read_role_style(rolltui_json_get(roles, K("text")), &default_style, defs, mode, "roles.text", report,
                    &text_style);
  } else {
    text_style = default_style;
    missing(report, "text");
  }

  for (i = 0; i < vocab->role_count; ++i) {
    const char* rn = vocab->role_names[i];
    if (i == vocab->text_role) {
      out_styles[i] = text_style;
      continue;
    }
    if (!rolltui_json_has(roles, rn, strlen(rn))) {
      missing(report, rn);
      out_styles[i] = text_style;
      continue;
    }
    {
      char where[ROLLTUI_THEME_WHERE_MAX];
      snprintf(where, sizeof where, "roles.%s", rn);
      read_role_style(rolltui_json_get(roles, rn, strlen(rn)), &text_style, defs, mode, where, report,
                      &out_styles[i]);
    }
  }

  effects_v = rolltui_json_get(root, K("effects"));
  map = rolltui_effect_map_new(vocab->state_count, vocab->fallback_effect_role);
  scratch = rolltui_u_scratch_new();
  read_effects(effects_v, vocab, scratch, report, map);
  rolltui_u_scratch_free(scratch);
  return map;
}

/* ---- the dumper ----------------------------------------------------------------------------
 * A direct port of `style_to_json`/`effect_to_json`/`effects_to_json` and the "roles"/
 * "effects" halves of `theme_to_json_value`/`theme_pair_to_json_value`, building a
 * `RolltuiJsonValue*` tree directly instead of a `json::Value` one. */

static int color_eq(RolltuiStyleColor a, RolltuiStyleColor b) {
  return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b;
}

/* `light` NULL: write `d` plainly when true, omit when false (a single-variant dump).
 * `light` given: write a {"dark":..,"light":..} pair whenever the two differ (REGARDLESS of
 * which is true — an attribute set in only one variant must still say so), a plain value
 * once when they agree (mirrors `theme_pair_to_json_value`'s "a role identical in both is
 * written once"). */
static void set_attr(RolltuiJsonValue* o, const char* name, unsigned char d, int has_light, unsigned char l) {
  if (has_light && d != l) {
    RolltuiJsonValue* pair = rolltui_json_object();
    rolltui_json_set(pair, K("dark"), rolltui_json_bool(d != 0));
    rolltui_json_set(pair, K("light"), rolltui_json_bool(l != 0));
    rolltui_json_set(o, name, strlen(name), pair);
  } else if (d) {
    rolltui_json_set(o, name, strlen(name), rolltui_json_bool(1));
  }
}

static RolltuiJsonValue* style_to_json(const RolltuiStyle* s, const RolltuiStyle* light) {
  RolltuiJsonValue* o = rolltui_json_object();
  char buf[ROLLTUI_COLOR_STRING_MAX];
  size_t n;
  RolltuiJsonValue *fgv, *bgv;
  if (light && !color_eq(s->fg, light->fg)) {
    fgv = rolltui_json_object();
    n = rolltui_color_to_string(s->fg, buf, sizeof buf);
    rolltui_json_set(fgv, K("dark"), rolltui_json_string(buf, n));
    n = rolltui_color_to_string(light->fg, buf, sizeof buf);
    rolltui_json_set(fgv, K("light"), rolltui_json_string(buf, n));
  } else {
    n = rolltui_color_to_string(s->fg, buf, sizeof buf);
    fgv = rolltui_json_string(buf, n);
  }
  rolltui_json_set(o, K("fg"), fgv);
  if (light && !color_eq(s->bg, light->bg)) {
    bgv = rolltui_json_object();
    n = rolltui_color_to_string(s->bg, buf, sizeof buf);
    rolltui_json_set(bgv, K("dark"), rolltui_json_string(buf, n));
    n = rolltui_color_to_string(light->bg, buf, sizeof buf);
    rolltui_json_set(bgv, K("light"), rolltui_json_string(buf, n));
  } else {
    n = rolltui_color_to_string(s->bg, buf, sizeof buf);
    bgv = rolltui_json_string(buf, n);
  }
  rolltui_json_set(o, K("bg"), bgv);
  set_attr(o, "bold", s->bold, light != NULL, light ? light->bold : s->bold);
  set_attr(o, "italic", s->italic, light != NULL, light ? light->italic : s->italic);
  set_attr(o, "underline", s->underline, light != NULL, light ? light->underline : s->underline);
  set_attr(o, "dim", s->dim, light != NULL, light ? light->dim : s->dim);
  set_attr(o, "reverse", s->reverse, light != NULL, light ? light->reverse : s->reverse);
  return o;
}

/* `s` is already `RolltuiEffectSpec` — the map's own storage, not a copy — so the only
 * translation left is a role BYTE back to its NAME, which is exactly what `vocab` is for. */
static RolltuiJsonValue* effect_spec_to_json(const RolltuiEffectSpec* s, const RolltuiThemeVocab* vocab) {
  RolltuiJsonValue* o = rolltui_json_object();
  rolltui_json_set(o, K("kind"), rolltui_json_string(s->kind, s->kind_len));
  if (s->frame_count) {
    RolltuiJsonValue* fs = rolltui_json_array();
    size_t i;
    for (i = 0; i < s->frame_count; ++i) rolltui_json_array_push(fs, rolltui_json_string(s->frames[i].bytes, s->frames[i].len));
    rolltui_json_set(o, K("frames"), fs);
  }
  /* `own_role_count`, never the substituted fallback's: a spec that named no role of its own
   * borrows the map's fallback, and writing that back would put a role in the file nobody
   * wrote (the same reasoning `effect_to_json`'s own comment states). */
  if (s->own_role_count) {
    RolltuiJsonValue* rs = rolltui_json_array();
    size_t i;
    for (i = 0; i < s->own_role_count; ++i) {
      const unsigned char r = s->roles[i];
      const char* rn = r < vocab->role_count ? vocab->role_names[r] : "";
      rolltui_json_array_push(rs, rolltui_json_string(rn, strlen(rn)));
    }
    rolltui_json_set(o, K("roles"), rs);
  }
  rolltui_json_set(o, K("period_ms"), rolltui_json_number(s->period_ms));
  if (s->width) rolltui_json_set(o, K("width"), rolltui_json_number(s->width));
  if (s->steps) rolltui_json_set(o, K("steps"), rolltui_json_number(s->steps));
  if (s->backward) rolltui_json_set(o, K("backward"), rolltui_json_bool(1));
  return o;
}

/* NULL when `m` is empty (no "effects" key at all — a still UI); otherwise an OWNED object
 * the caller sets onto its own root or frees. Starts at state 1: state 0 ("none") is never
 * written, the same skip `effects_to_json` already makes. */
static RolltuiJsonValue* effects_map_to_json(const RolltuiEffectMap* m, const RolltuiThemeVocab* vocab) {
  RolltuiJsonValue* o;
  size_t i;
  if (!m || rolltui_effect_map_empty(m)) return NULL;
  o = rolltui_json_object();
  for (i = 1; i < vocab->state_count; ++i) {
    const size_t n = rolltui_effect_map_count(m, i);
    const char* sn = vocab->state_names[i];
    if (n == 0) continue;
    if (n == 1) {
      rolltui_json_set(o, sn, strlen(sn), effect_spec_to_json(rolltui_effect_map_at(m, i, 0), vocab));
      continue;
    }
    {
      RolltuiJsonValue* arr = rolltui_json_array();
      size_t k;
      for (k = 0; k < n; ++k) rolltui_json_array_push(arr, effect_spec_to_json(rolltui_effect_map_at(m, i, k), vocab));
      rolltui_json_set(o, sn, strlen(sn), arr);
    }
  }
  return o;
}

RolltuiJsonValue* rolltui_theme_dump(const RolltuiStyle* dark_styles, const RolltuiEffectMap* dark_effects,
                                     const RolltuiStyle* light_styles, const RolltuiEffectMap* light_effects,
                                     const RolltuiThemeVocab* vocab) {
  RolltuiJsonValue* root = rolltui_json_object();
  RolltuiJsonValue* roles = rolltui_json_object();
  RolltuiJsonValue* fx;
  size_t i;
  (void)light_effects; /* motion is the THEME's, not the terminal background's — see this
                        * header's comment; "effects" is always dark_effects alone. */
  for (i = 0; i < vocab->role_count; ++i) {
    const RolltuiStyle* light_i = light_styles ? &light_styles[i] : NULL;
    rolltui_json_set(roles, vocab->role_names[i], strlen(vocab->role_names[i]), style_to_json(&dark_styles[i], light_i));
  }
  rolltui_json_set(root, K("roles"), roles);
  fx = effects_map_to_json(dark_effects, vocab);
  if (fx) rolltui_json_set(root, K("effects"), fx);
  return root;
}

/* ---- the style table: one role at a time (Phase 17 m2) ------------------------------------
 * See this file's header for why these take `(styles, role_count, role)` rather than a
 * `Theme` handle: the table is the caller's own fixed-size storage, never allocated here. */

const RolltuiStyle* rolltui_theme_style(const RolltuiStyle* styles, size_t role_count, unsigned char role) {
  if (!styles || role >= role_count) return NULL;
  return &styles[role];
}

/* ---- the mode and depth vocabulary (Phase 17 m2a) ------------------------------------------
 * See rolltui_theme.h for why these names moved here after that header spent a phase saying
 * they would not. The tables expand the X-macros; there is no second list to keep in step. */

static const char* const kDepthNames[] = {
#define ROLLTUI_DEPTH_NAME_(lower, UPPER, Camel) lower,
    ROLLTUI_DEPTH_LIST(ROLLTUI_DEPTH_NAME_)
#undef ROLLTUI_DEPTH_NAME_
};

static const char* const kModeNames[] = {
#define ROLLTUI_MODE_NAME_(lower, UPPER, Camel) lower,
    ROLLTUI_MODE_LIST(ROLLTUI_MODE_NAME_)
#undef ROLLTUI_MODE_NAME_
};

typedef struct { const char* name; unsigned char depth; } DepthAlias;
static const DepthAlias kDepthAliases[] = {
#define ROLLTUI_DEPTH_ENV_ALIAS_(lower, UPPER) {lower, ROLLTUI_DEPTH_##UPPER},
    ROLLTUI_DEPTH_ENV_ALIAS_LIST(ROLLTUI_DEPTH_ENV_ALIAS_)
#undef ROLLTUI_DEPTH_ALIAS_
};

/* Exact, bounded compare against a NUL-terminated literal — the same shape
 * `rolltui_bindings.c`'s `name_is` takes, minus the case folding these names never wanted
 * (a theme file's "truecolor" is spelled one way). */
static int lit_eq(const char* s, size_t len, const char* lit) {
  size_t i;
  for (i = 0; i < len; ++i)
    if (lit[i] == '\0' || s[i] != lit[i]) return 0;
  return lit[len] == '\0';
}

const char* rolltui_color_depth_name(unsigned char depth, size_t* len) {
  const char* p = kDepthNames[depth < ROLLTUI_DEPTH_COUNT ? depth : ROLLTUI_DEPTH_MONO];
  if (len) *len = strlen(p);
  return p;
}

int rolltui_color_depth_from_name(const char* name, size_t len) {
  size_t i;
  if (!name) return -1;
  for (i = 0; i < ROLLTUI_DEPTH_COUNT; ++i)
    if (lit_eq(name, len, kDepthNames[i])) return (int)i;
  return -1;
}

int rolltui_theme_mode_from_name(const char* name, size_t len) {
  size_t i;
  if (!name) return -1;
  for (i = 0; i < ROLLTUI_MODE_COUNT; ++i)
    if (lit_eq(name, len, kModeNames[i])) return (int)i;
  return -1;
}

/* "auto" is a SETTING, never a depth: it means "ask the environment", which is what
 * `rolltui_detect_color_depth` below is for. The alias is deliberately NOT accepted here —
 * `depth_from_setting`'s C++ original did not take it either, and a preset file that stores
 * "24bit" would round-trip to "truecolor" and stop matching itself. */
int rolltui_color_depth_setting_valid(const char* s, size_t len) {
  size_t i;
  if (!s) return 0;
  if (lit_eq(s, len, "auto")) return 1;
  for (i = 0; i < ROLLTUI_DEPTH_COUNT; ++i)
    if (lit_eq(s, len, kDepthNames[i])) return 1;
  return 0;
}

int rolltui_theme_mode_setting_valid(const char* s, size_t len) {
  size_t i;
  if (!s) return 0;
  if (lit_eq(s, len, "auto")) return 1;
  for (i = 0; i < ROLLTUI_MODE_COUNT; ++i)
    if (lit_eq(s, len, kModeNames[i])) return 1;
  return 0;
}

/* A name as the ENVIRONMENT may spell it: the four above plus the alias. */
static int env_depth_of(const char* s, size_t len) {
  size_t i;
  const int d = rolltui_color_depth_from_name(s, len);
  if (d >= 0) return d;
  for (i = 0; i < sizeof kDepthAliases / sizeof kDepthAliases[0]; ++i)
    if (lit_eq(s, len, kDepthAliases[i].name)) return (int)kDepthAliases[i].depth;
  return -1;
}

unsigned char rolltui_detect_color_depth(const char* colorterm, const char* term, const char* force) {
  const char* ct = colorterm ? colorterm : "";
  const char* t = term ? term : "";
  if (force && *force) {
    const int d = env_depth_of(force, strlen(force));
    if (d >= 0) return (unsigned char)d;  /* an invalid override is ignored, not an error */
  }
  {
    const int d = env_depth_of(ct, strlen(ct));
    if (d == ROLLTUI_DEPTH_TRUECOLOR) return ROLLTUI_DEPTH_TRUECOLOR;
  }
  if (strstr(t, "256color")) return ROLLTUI_DEPTH_ANSI256;
  if (t[0] == '\0' || lit_eq(t, strlen(t), "dumb")) return ROLLTUI_DEPTH_MONO;
  return ROLLTUI_DEPTH_ANSI16;
}

/* ---- the library's own vocabulary table (Phase 17 m2a) -------------------------------------
 * See rolltui_theme.h for why this can exist now and could not before. */
const RolltuiThemeVocab* rolltui_theme_default_vocab(void) {
  static const char* role_names[ROLLTUI_ROLE_COUNT];
  static const char* state_names[ROLLTUI_EFFECT_STATE_COUNT];
  static RolltuiThemeVocab v;
  static int built = 0;
  if (!built) {
    size_t i;
    for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) role_names[i] = rolltui_role_name((unsigned char)i, NULL);
    for (i = 0; i < ROLLTUI_EFFECT_STATE_COUNT; ++i)
      state_names[i] = rolltui_effect_state_name((unsigned char)i, NULL);
    v.role_names = role_names;
    v.role_count = ROLLTUI_ROLE_COUNT;
    v.text_role = ROLLTUI_ROLE_TEXT;
    v.state_names = state_names;
    v.state_count = ROLLTUI_EFFECT_STATE_COUNT;
    v.fallback_effect_role = ROLLTUI_ROLE_ACCENT_1;
    built = 1;  /* idempotent: every write above is the same value every time */
  }
  return &v;
}
