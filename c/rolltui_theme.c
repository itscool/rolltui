/* rolltui/c/rolltui_theme.c — the C side of the colour engine. See rolltui_theme.h for the
 * boundary's rules and rolltui/Theme.hpp for the colour rules themselves; `ThemeCpp.cpp` is
 * the other implementation of the same seven functions, and the reference values in
 * `rolltui/tests/theme_test.cpp` are the oracle for both.
 *
 * THE COLOUR LITERALS HERE ARE xterm's PUBLISHED PALETTE, not a theme's. It is the
 * reference the 16-colour downgrade measures against, and it is exempted BY NAME in
 * theme_test's grep control — with `ThemeCpp.cpp`, its other half — rather than by being in
 * a directory the control does not scan. Nothing allocates: every result goes into a buffer
 * the caller sized from a constant in the header. */
#include "rolltui/c/rolltui_theme.h"

#include <math.h>

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
