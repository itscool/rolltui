/* rolltui/c/rolltui_theme.c — the C side of the theme module. See rolltui_theme.h for the
 * boundary's rules and rolltui/Theme.hpp for the colour and file-format rules themselves; the
 * reference values in `rolltui/tests/theme_test.cpp` are the oracle for all of it.
 *
 * TWO PARTS, in one file because they are one module:
 *   THE COLOUR ENGINE (first): parsing/printing a colour, reduction, SGR emission, OSC 11.
 *     THE COLOUR LITERALS THERE ARE xterm's PUBLISHED PALETTE, not a theme's — the reference
 *     the 16-colour downgrade measures against. Nothing in that part allocates: every result
 *     goes into a caller's buffer sized from a constant in the header.
 *   THE BUILT-IN THEMES AND THE JSON LOADER/DUMPER (below it): this library's FILE FORMAT,
 *     and the three built-in names that read a shipped file through it. THIS part allocates
 *     freely through `rolltui_alloc.h`'s closed set — a theme loads once per file, never per
 *     frame, so it is not under the budget `rolltui-budget-test` holds the draw path to.
 * NO THEME'S COLOURS ARE WRITTEN HERE. The colour engine's `kSystem16` is xterm's published
 * palette — the reference the 16-colour downgrade measures against — and it is why this file
 * is exempted BY NAME in `theme_test`'s colour-literal grep control. That control also asserts
 * this file still carries `kSystem16` and still names no theme colour of its own, so either
 * property moving fails a test instead of passing everywhere. */
#include "testkit/testctl.h"

#include "rolltui/c/rolltui_widgets.h"  /* the scrollbar glyph default, filled for a theme that states none */
#include "rolltui/c/rolltui_theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_terminal.h"

/* ---- parsing and printing ---------------------------------------------------------------- */

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

/* ---- the palette, and the nearest-colour search ------------------------------------------ */

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
      /* ON = the mono case does nothing, which is the defect state: a terminal that reported no
       * colour is sent colour anyway. It is invisible from inside the library — every value is
       * a legal colour and every SGR string is well formed — and shows up only as a screen that
       * is wrong on the one machine nobody tests on. */
      if (testkit_ctl_on("theme.mono_keeps_colour")) return;
      c->kind = 0;
      c->index = 0;
      c->r = c->g = c->b = 0;
      return;
    default: return;
  }
}

/* ---- the SGR sequence -------------------------------------------------------------------- */

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

/* ---- the terminal's background ----------------------------------------------------------- */

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

/* ---- role ordinals, as file-local ALIASES of the library's own ------------------------------
 * The loader and dumper below never use these: they resolve every role through the caller's
 * `RolltuiThemeVocab` table instead, exactly as this header's own comment states. One place
 * needs a role by name — the fallback an effect map is created with — and `R_accent_1` says
 * which role that is where `9` would not. `R_<name>` is `ROLLTUI_ROLE_<NAME>`, generated from
 * the X-macro that owns the list (`ROLLTUI_ROLE_LIST`), so it is an ALIAS rather than a
 * hand-written parallel enumeration: a copy can drift and an alias cannot.
 * `rolltui_theme_builtin_fill` below still checks `role_count` against `ROLLTUI_ROLE_COUNT`
 * before trusting a caller's array: a mismatch reads as "this theme doesn't exist" rather
 * than writing past its end. */
#define ROLLTUI_R_ALIAS_(lower, UPPER) R_##lower = ROLLTUI_ROLE_##UPPER,
enum { ROLLTUI_ROLE_LIST(ROLLTUI_R_ALIAS_) };
#undef ROLLTUI_R_ALIAS_

/* ---- small helpers shared by the built-ins and the loader -------------------------------- */

/* A literal C string plus its length, computed once here rather than hand-counted at every
 * call site — this file's own version of `rolltui_json.c`'s `JLIT` / `rolltui_menu.c`'s
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

/* ---- the built-in themes --------------------------------------------------------------------
 * A BUILT-IN THEME IS A SHIPPED FILE READ AT ONE MODE. `rolltui/presets/themes/default.json`
 * and `mono.json` are compiled in by cmake as `rolltui_kThemePresets`, and the three built-in
 * names below read them: "default-dark" is `default.json` at dark, "default-light" the same
 * file at light, "mono" is `mono.json`. There is no second copy of any of it in this file.
 *
 * WHY THE COLLAPSE GOES THIS WAY ROUND — the C reading the file, rather than a tool writing
 * the file out of the C. The file is what a session actually runs, what the theme editor
 * writes, and what a person can open and change; the C literals were reachable only through a
 * rebuild. Generating the file instead would leave two artifacts to keep in agreement plus a
 * step somebody has to remember to run, which is the shape being removed, not a fix for it.
 *
 * A BROKEN EMBEDDED FILE STOPS THE PROCESS rather than half-filling a caller's table: it is
 * compiled in, so it cannot be wrong at runtime without being wrong at build time, and the
 * shipped-preset cache already holds that standard. A caller gets 49 styles or it gets
 * nothing. */

/* Which shipped preset a built-in name reads, and the mode it pins. `*mode` is
 * ROLLTUI_MODE_DARK / ROLLTUI_MODE_LIGHT. 1 when `name` is a built-in, 0 otherwise. */
static int builtin_source(const char* name, size_t name_len, const char** preset, size_t* preset_len, int* mode) {
  if (streq(name, name_len, "default-dark")) {
    *preset = "default";
    *mode = ROLLTUI_MODE_DARK;
  } else if (streq(name, name_len, "default-light")) {
    *preset = "default";
    *mode = ROLLTUI_MODE_LIGHT;
  } else if (streq(name, name_len, "mono")) {
    *preset = "mono";
    *mode = ROLLTUI_MODE_DARK;
  } else {
    return 0;
  }
  *preset_len = strlen(*preset);
  return 1;
}

size_t rolltui_theme_builtin_count(void) { return ROLLTUI_THEME_BUILTIN_COUNT; }

const char* rolltui_theme_builtin_name(size_t i) {
  /* Plain DATA (this header's own comment explains why a theme's name is not the vocabulary
   * the rest of this file stays out of): compiled-in literals, a BORROW valid for the
   * process's life. */
  static const char* const names[ROLLTUI_THEME_BUILTIN_COUNT] = {"default-dark", "default-light", "mono"};
  return i < ROLLTUI_THEME_BUILTIN_COUNT ? names[i] : NULL;
}

/* Says which shipped file was unusable and stops. Reached only when a file compiled into this
 * binary does not load, which is a build that should not have been produced. */
static void builtin_broken(const char* preset, size_t preset_len, const char* why, size_t why_len) {
  fprintf(stderr, "rolltui: the shipped theme '%.*s' a built-in reads is broken: %.*s\n", (int)preset_len, preset,
          (int)why_len, why_len ? why : "");
  abort();
}

RolltuiEffectMap* rolltui_theme_builtin_fill(const char* name, size_t name_len, RolltuiStyle* styles,
                                             size_t role_count) {
  const char* preset;
  size_t preset_len;
  int mode;
  const char* text;
  RolltuiJsonValue* root;
  const RolltuiJsonValue* colours;
  RolltuiStr err, loaded_name;
  RolltuiThemeReport report;
  RolltuiEffectMap* m;

  if (role_count != ROLLTUI_ROLE_COUNT) return NULL;
  if (!builtin_source(name, name_len, &preset, &preset_len, &mode)) return NULL;
  text = rolltui_embedded_text(rolltui_kThemePresets, rolltui_kThemePresetCount, preset, preset_len);
  if (!text) builtin_broken(preset, preset_len, K("no such file is compiled in"));

  /* OWNED, SHORT-LIVED (rolltui_alloc.h strategy 4, scoped to this call): the parse tree and
   * the load report are wanted only long enough to fill the caller's table. A theme is filled
   * at start and when a look changes, never per frame, so there is nothing here for a cache to
   * buy that a second lifetime would not cost. */
  memset(&err, 0, sizeof err);
  root = rolltui_json_parse(text, strlen(text), &err);
  if (!root) builtin_broken(preset, preset_len, err.p ? err.p : "", err.n);
  rolltui_str_free(&err);

  /* The compiled-in bytes are a whole preset FILE — "colours" is the theme inside it, the same
   * object a preset store hands to `rolltui_theme_load`. */
  colours = rolltui_json_get(root, K("colours"));
  memset(&loaded_name, 0, sizeof loaded_name);
  memset(&report, 0, sizeof report);
  m = rolltui_theme_load(colours, mode, rolltui_theme_default_vocab(), styles, &loaded_name, &report);
  if (!m || report.error.n || report.missing_roles_n || report.bad_values_n || report.unknown_keys_n) {
    const char* why = report.error.n            ? report.error.p
                      : report.missing_roles_n  ? "a role is missing"
                      : report.bad_values_n     ? "a value is not one"
                                                : "a key is not one this format has";
    builtin_broken(preset, preset_len, why, strlen(why));
  }
  /* `report.badge_mismatches` is deliberately NOT fatal here: a stale `meta.badges` is a
   * declaration about the colours, not a defect in them, and the theme draws either way. The
   * shipped files' declarations are held to equality by rolltui-presets-test. */
  rolltui_theme_report_release(&report);
  rolltui_str_free(&loaded_name);
  rolltui_json_free(root);
  return m;
}

/* ---- the load report: mirrors rolltui::ThemeLoadReport field for field, and
 * `RolltuiBindingsReport`'s own shape one file over -------------------------------------- */

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
  for (i = 0; i < r->badge_mismatches_n; ++i) rolltui_str_free(&r->badge_mismatches[i]);
  rolltui_mem_free(r->badge_mismatches);
  memset(r, 0, sizeof *r);
}

void rolltui_theme_report_set_error(RolltuiThemeReport* r, const char* s, size_t len) {
  rolltui_str_set(&r->error, s, len);
}

void rolltui_theme_report_add_missing_role(RolltuiThemeReport* r, const char* s, size_t len) {
  /* GROWING AMORTISED (rolltui_alloc.h strategy 2): an array of small owned strings, the same
   * shape `rolltui_bindings.c`'s own report arrays already use. */
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

void rolltui_theme_report_add_badge_mismatch(RolltuiThemeReport* r, const char* s, size_t len) {
  r->badge_mismatches = (RolltuiStr*)rolltui_grow_zeroed(r->badge_mismatches, &r->badge_mismatches_cap,
                                                         r->badge_mismatches_n + 1, sizeof *r->badge_mismatches);
  rolltui_str_set(&r->badge_mismatches[r->badge_mismatches_n++], s, len);
}

/* ---- the JSON loader ------------------------------------------------------------------------
 * A direct port of `rolltui::(anonymous namespace)::resolve_color`/`read_role`/`EffectDraft`/
 * `read_effect`/`commit`/`read_effects` and `rolltui::load_theme`, working on
 * `RolltuiJsonValue*` directly (`rolltui/c/rolltui_json.h` — the parser this loader is BUILT
 * on, per this task's own instructions) instead of on a converted `json::Value` tree. Every
 * report message is byte-for-byte the original's; `rolltui-theme-test` is the oracle and
 * holds the assertions that say so. */

/* "where" paths are built with `snprintf` into a caller-owned stack buffer. Bounded because
 * the defs-cycle depth is capped at 4 and every piece appended (a role name, a JSON key, a
 * defs name) is itself short in practice; a diagnostic a human reads is allowed to TRUNCATE
 * past that rather than need unbounded storage; the buffer is sized here for the deepest
 * nesting a colour's defs chain can reach. */
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

/* ---- "glyphs": what the window's own chrome is DRAWN WITH -----------------------------------
 * A scrollbar thumb is a shape, and which shape is a look — so it belongs to the theme beside
 * the colours rather than in a C literal a theme author cannot reach.
 *
 *   "glyphs": { "scrollbar": { "single": "●", "top": "▄", "middle": "█", "bottom": "▀",
 *                              "ascii": { "single": "o", "top": "#", … } } }
 *
 * Every key is OPTIONAL and an absent one keeps the default, so a theme states only what it
 * changes. A value longer than the field is IGNORED rather than truncated — half a UTF-8
 * sequence is not a glyph, and drawing one would put a replacement character in the border of
 * every scrollable window. */
static void take_glyph(const RolltuiJsonValue* obj, const char* key, char* out, size_t cap) {
  const RolltuiJsonValue* v = obj ? rolltui_json_get(obj, key, strlen(key)) : NULL;
  size_t n;
  const char* g;
  if (!v || !rolltui_json_is_string(v)) return;
  g = rolltui_json_as_string(v, "", 0, &n);
  if (n == 0 || n >= cap) return;
  memcpy(out, g, n);
  out[n] = 0;
}

int rolltui_theme_scrollbar_glyphs(const RolltuiJsonValue* root, RolltuiScrollbarGlyphs* out) {
  const RolltuiJsonValue* glyphs;
  const RolltuiJsonValue* bar;
  const RolltuiJsonValue* ascii_;
  if (!out) return 0;
  rolltui_scrollbar_glyphs_default(out);
  glyphs = root ? rolltui_json_get(root, K("glyphs")) : NULL;
  bar = glyphs ? rolltui_json_get(glyphs, K("scrollbar")) : NULL;
  if (!bar) return 0;
  take_glyph(bar, "single", out->single, sizeof out->single);
  take_glyph(bar, "top", out->top, sizeof out->top);
  take_glyph(bar, "middle", out->middle, sizeof out->middle);
  take_glyph(bar, "bottom", out->bottom, sizeof out->bottom);
  ascii_ = rolltui_json_get(bar, K("ascii"));
  take_glyph(ascii_, "single", out->ascii_single, sizeof out->ascii_single);
  take_glyph(ascii_, "top", out->ascii_top, sizeof out->ascii_top);
  take_glyph(ascii_, "middle", out->ascii_middle, sizeof out->ascii_middle);
  take_glyph(ascii_, "bottom", out->ascii_bottom, sizeof out->ascii_bottom);
  return 1;
}

/* ---- "effects": state -> what it looks like while it lasts ----------------------------------
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

  /* THE FILE'S OWN CLASSIFICATION, RECOMPUTED. `meta.badges` says what this theme claims to be;
   * the colours above say what it is. The stored value is never used for anything — it is read
   * only to be disagreed with, and every disagreement is named in `report->badge_mismatches`.
   * A stale declaration is never fatal (see that field): the theme loads and draws either way. */
  {
    RolltuiStrArray badge_problems;
    memset(&badge_problems, 0, sizeof badge_problems);
    rolltui_theme_check_declaration(rolltui_json_get(root, K("meta")), mode, out_styles, vocab->role_count,
                                    &badge_problems);
    for (i = 0; i < badge_problems.n; ++i)
      rolltui_theme_report_add_badge_mismatch(report, badge_problems.v[i].p, badge_problems.v[i].n);
    rolltui_str_array_release(&badge_problems);
  }

  effects_v = rolltui_json_get(root, K("effects"));
  map = rolltui_effect_map_new(vocab->state_count, vocab->fallback_effect_role);
  scratch = rolltui_u_scratch_new();
  read_effects(effects_v, vocab, scratch, report, map);
  rolltui_u_scratch_free(scratch);
  return map;
}

/* ---- the dumper -----------------------------------------------------------------------------
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

/* ---- the style table: one role at a time ----------------------------------------------------
 * See this file's header for why these take `(styles, role_count, role)` rather than a
 * `Theme` handle: the table is the caller's own fixed-size storage, never allocated here. */

const RolltuiStyle* rolltui_theme_style(const RolltuiStyle* styles, size_t role_count, unsigned char role) {
  if (!styles || role >= role_count) return NULL;
  return &styles[role];
}

/* ---- the mode and depth vocabulary ----------------------------------------------------------
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

/* ---- the library's own vocabulary table -----------------------------------------------------
 * See rolltui_theme.h for why this can exist now and could not before. */
/* A COMPILE-TIME table, not a memoized one. It was four function-local statics
 * filled on first call behind a `built` flag — correct, idempotent and allocation-free, but
 * still four pieces of mutable process-wide state that the globals boundary would have had to
 * carry a justification for. Both name arrays come from the SAME X-macros `rolltui_style.c`
 * expands, so they cannot drift from the tables `rolltui_role_name` reads; what goes away is
 * the runtime fill, the branch on every call, and four entries on the boundary list. */
static const char* const kVocabRoleNames[] = {
#define ROLLTUI_VOCAB_ROLE_(lower, UPPER) #lower,
    ROLLTUI_ROLE_LIST(ROLLTUI_VOCAB_ROLE_)
#undef ROLLTUI_VOCAB_ROLE_
};
static const char* const kVocabStateNames[] = {
#define ROLLTUI_VOCAB_STATE_(lower, UPPER, Camel) #lower,
    ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_VOCAB_STATE_)
#undef ROLLTUI_VOCAB_STATE_
};
static const RolltuiThemeVocab kDefaultVocab = {
    kVocabRoleNames, ROLLTUI_ROLE_COUNT,          ROLLTUI_ROLE_TEXT,
    kVocabStateNames, ROLLTUI_EFFECT_STATE_COUNT, ROLLTUI_ROLE_ACCENT_1,
};

const RolltuiThemeVocab* rolltui_theme_default_vocab(void) { return &kDefaultVocab; }
