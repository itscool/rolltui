/* rolltui/c/rolltui_chart.c — NUMBERS DRAWN: a line in braille, bars and a sparkline in eighth blocks, with the range they span and
 * what each series is called.
 *
 * A LINE is drawn on a grid of braille dots, two across and four down a cell, so a 40-cell chart is 80 points wide; consecutive
 * points are joined, so a steep change is a line and not two dots. Braille is narrow in every terminal, wide ambiguous glyphs or not.
 * BARS and a SPARKLINE stack eighth blocks; where ambiguous glyphs are two cells wide they are ASCII.
 *
 * The numbers are the host's: a fill function sets the kind, up to four series and, if it likes, the range. More values than the
 * chart is wide shows the newest, which is what a series that grows over time wants.
 */
#include "rolltui/rolltui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_chart.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_str.h"

typedef struct Series {
  RolltuiStr label;
  double* v;
  size_t n, cap;
  unsigned char role;
  int used;
  int has_gradient;
  RolltuiGradient gradient;
} Series;

struct RolltuiChart {
  unsigned char kind;
  unsigned char drawn_kind; /* AUTO resolved for the room this draw has */
  unsigned char symbols, default_symbols;
  unsigned flags;
  Series s[ROLLTUI_CHART_SERIES_MAX];
  double lo, hi;
  int fixed;
  double newest_s, step_s; /* time along the foot, when `step_s` > 0 */
  unsigned char* dots; /* the dot grid, a byte a cell */
  unsigned char* owner;
  size_t dots_cap, owner_cap;
};

struct RolltuiHistory {
  double* v;
  size_t cap, n, head; /* a ring: `n` values, the oldest at `head` */
};

RolltuiHistory* rolltui_history_new(size_t cap) {
  RolltuiHistory* h = (RolltuiHistory*)rolltui_mem_alloc(sizeof *h);
  memset(h, 0, sizeof *h);
  rolltui_history_resize(h, cap);
  return h;
}

void rolltui_history_free(RolltuiHistory* h) {
  if (!h) return;
  rolltui_mem_free(h->v);
  rolltui_mem_free(h);
}

static double history_at(const RolltuiHistory* h, size_t k) { return h->v[(h->head + k) % h->cap]; }

void rolltui_history_resize(RolltuiHistory* h, size_t cap) {
  double* v;
  size_t keep, k;
  if (!h) return;
  if (cap < 1) cap = 1;
  if (cap == h->cap) return;
  v = (double*)rolltui_mem_alloc(cap * sizeof *v);
  keep = h->n < cap ? h->n : cap;
  for (k = 0; k < keep; ++k) v[k] = history_at(h, h->n - keep + k); /* the newest `keep` */
  rolltui_mem_free(h->v);
  h->v = v;
  h->cap = cap;
  h->n = keep;
  h->head = 0;
}

void rolltui_history_push(RolltuiHistory* h, double value) {
  if (!h || !h->cap) return;
  if (h->n < h->cap) {
    h->v[(h->head + h->n) % h->cap] = value;
    ++h->n;
  } else {
    h->v[h->head] = value;
    h->head = (h->head + 1) % h->cap;
  }
}

size_t rolltui_history_count(const RolltuiHistory* h) { return h ? h->n : 0; }

int rolltui_chart_set_series_history(RolltuiChart* c, size_t i, const char* label, size_t label_len, unsigned char role,
                                     const RolltuiHistory* h) {
  size_t k;
  if (!rolltui_chart_set_series(c, i, label, label_len, role, NULL, 0)) return 0;
  if (!h || !h->n) return 1;
  c->s[i].v = (double*)rolltui_grow(c->s[i].v, &c->s[i].cap, h->n, sizeof *c->s[i].v);
  for (k = 0; k < h->n; ++k) c->s[i].v[k] = history_at(h, k);
  c->s[i].n = h->n;
  return 1;
}

void rolltui_chart_set_symbols(RolltuiChart* c, unsigned char symbols) {
  if (c && symbols <= ROLLTUI_CHART_SYMBOLS_SHADES) c->symbols = symbols;
}

void rolltui_chart_set_default_symbols(RolltuiChart* c, unsigned char symbols) {
  if (c && symbols <= ROLLTUI_CHART_SYMBOLS_SHADES) c->default_symbols = symbols;
}

void rolltui_chart_set_flags(RolltuiChart* c, unsigned flags) {
  if (c) c->flags = flags;
}

void rolltui_chart_set_time(RolltuiChart* c, double newest_s, double step_s) {
  if (!c) return;
  c->newest_s = newest_s;
  c->step_s = step_s > 0 ? step_s : 0;
}

void rolltui_chart_set_gradient(RolltuiChart* c, size_t i, const RolltuiGradient* g) {
  if (!c || i >= ROLLTUI_CHART_SERIES_MAX) return;
  c->s[i].has_gradient = g != NULL;
  if (g) c->s[i].gradient = *g;
}

static unsigned char blend(unsigned char a, unsigned char b, double t) { return (unsigned char)(a + (b - a) * t + 0.5); }

void rolltui_gradient_at(const RolltuiGradient* g, double t, RolltuiStyleColorRaw* out) {
  RolltuiStyleColorRaw a, b;
  if (!g || !out) return;
  if (t != t || t < 0) t = 0;
  if (t > 1) t = 1;
  if (g->end.kind == 0) { /* one stop */
    *out = g->start;
    return;
  }
  if (g->mid.kind == 0) a = g->start, b = g->end;
  else if (t < 0.5) a = g->start, b = g->mid, t *= 2;
  else a = g->mid, b = g->end, t = (t - 0.5) * 2;
  if (a.kind != 2 || b.kind != 2) { /* a palette stop is taken as it is: the nearer */
    *out = t < 0.5 ? a : b;
    return;
  }
  memset(out, 0, sizeof *out);
  out->kind = 2;
  out->r = blend(a.r, b.r, t);
  out->g = blend(a.g, b.g, t);
  out->b = blend(a.b, b.b, t);
}

RolltuiChart* rolltui_chart_new(void) {
  RolltuiChart* c = (RolltuiChart*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  return c;
}

void rolltui_chart_free(RolltuiChart* c) {
  size_t i;
  if (!c) return;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
    rolltui_str_free(&c->s[i].label);
    rolltui_mem_free(c->s[i].v);
  }
  rolltui_mem_free(c->dots);
  rolltui_mem_free(c->owner);
  rolltui_mem_free(c);
}

void rolltui_chart_clear(RolltuiChart* c) {
  size_t i;
  if (!c) return;
  c->kind = ROLLTUI_CHART_LINE;
  c->fixed = 0;
  c->symbols = 0;
  c->flags = 0;
  c->step_s = 0;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
    rolltui_str_clear(&c->s[i].label);
    c->s[i].n = 0;
    c->s[i].used = 0;
    c->s[i].role = 0;
    c->s[i].has_gradient = 0;
  }
}

void rolltui_chart_set_kind(RolltuiChart* c, unsigned char kind) {
  if (c && kind <= ROLLTUI_CHART_AUTO) c->kind = kind;
}

void rolltui_chart_set_range(RolltuiChart* c, double lo, double hi) {
  if (!c || !(hi > lo)) return;
  c->lo = lo, c->hi = hi, c->fixed = 1;
}

int rolltui_chart_set_series(RolltuiChart* c, size_t i, const char* label, size_t label_len, unsigned char role, const double* values,
                             size_t n) {
  Series* s;
  if (!c || i >= ROLLTUI_CHART_SERIES_MAX) return 0;
  s = &c->s[i];
  rolltui_str_set(&s->label, label ? label : "", label ? label_len : 0);
  s->v = (double*)rolltui_grow(s->v, &s->cap, n ? n : 1, sizeof *s->v);
  if (n) memcpy(s->v, values, n * sizeof *values);
  s->n = n;
  s->role = role;
  s->used = 1;
  return 1;
}

/* ---- drawing ------------------------------------------------------------------------------ */

static int series_count(const RolltuiChart* c);

/* The symbols a line or area draws in: the chart's own, else the theme's, else braille. */
static unsigned char symbols_of(const RolltuiChart* c) {
  return c->symbols ? c->symbols : c->default_symbols ? c->default_symbols : ROLLTUI_CHART_SYMBOLS_BRAILLE;
}
/* Dots across and down a cell, for the symbols. */
static int dots_x(const RolltuiChart* c) { return symbols_of(c) == ROLLTUI_CHART_SYMBOLS_SHADES ? 1 : 2; }
static int dots_y(const RolltuiChart* c) {
  return symbols_of(c) == ROLLTUI_CHART_SYMBOLS_BRAILLE ? 4 : symbols_of(c) == ROLLTUI_CHART_SYMBOLS_BLOCKS ? 2 : 4;
}
static int plotted(unsigned char kind) { return kind == ROLLTUI_CHART_LINE || kind == ROLLTUI_CHART_AREA; }

/* The values a plot `w` cells wide holds, a series, at the kind being drawn. */
static size_t fit_of(const RolltuiChart* c, unsigned char kind, int w) {
  if (w <= 0) return 0;
  if (plotted(kind)) return (size_t)w * (size_t)dots_x(c);
  if (kind == ROLLTUI_CHART_METER) return 1;
  return (size_t)w; /* a sparkline, and flowing bars: a cell a value */
}

size_t rolltui_chart_capacity(const RolltuiChart* c, int cells) {
  if (!c) return 0;
  return fit_of(c, c->kind == ROLLTUI_CHART_AUTO ? ROLLTUI_CHART_LINE : c->kind, cells);
}

/* The first value of series `i` a plot `w` cells wide shows: more values than fit show the newest, so the range is theirs alone and a
 * spike that has scrolled off no longer flattens the rest. */
static size_t first_shown(const RolltuiChart* c, int i, int w) {
  const size_t n = c->s[i].n;
  const unsigned char kind = c->drawn_kind;
  size_t fit;
  if (w <= 0) return n;
  if (kind != ROLLTUI_CHART_BARS || (c->flags & ROLLTUI_CHART_FLOW)) {
    fit = fit_of(c, kind, w);
  } else { /* bars: the groups that fit, as draw_bars lays them out */
    size_t most = 0;
    int j, slot, gap, bar_w;
    const int ns = series_count(c);
    for (j = 0; j < ROLLTUI_CHART_SERIES_MAX; ++j)
      if (c->s[j].used && c->s[j].n > most) most = c->s[j].n;
    if (most == 0 || ns == 0) return n;
    slot = w / (int)most;
    gap = slot > ns ? 1 : 0;
    bar_w = slot > 0 ? (slot - gap) / ns : 0;
    if (bar_w < 1) slot = ns;
    fit = (size_t)(w / (slot > 0 ? slot : 1));
    return most > fit ? most - fit : 0;
  }
  return n > fit ? n - fit : 0;
}

static void range_of(const RolltuiChart* c, int w, double* lo, double* hi) {
  size_t i, k;
  int any = 0;
  if (c->fixed) {
    *lo = c->lo, *hi = c->hi;
    return;
  }
  *lo = 0, *hi = 1;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i)
    for (k = first_shown(c, (int)i, w); k < c->s[i].n; ++k) {
      const double v = c->s[i].v[k];
      if (v != v) continue; /* NaN: a gap */
      if (!any || v < *lo) *lo = v;
      if (!any || v > *hi) *hi = v;
      any = 1;
    }
  if (any && c->drawn_kind != ROLLTUI_CHART_LINE && *lo > 0) *lo = 0; /* a bar, an area, a meter stands on zero */
  if (*hi <= *lo) *hi = *lo + 1;
}

/* A number short enough for an axis: three significant figures, a k, M or G for thousands up. */
static int axis_label(double v, char* out, size_t cap) {
  const double a = fabs(v);
  if (a >= 1e9) return snprintf(out, cap, "%.3gG", v / 1e9);
  if (a >= 1e6) return snprintf(out, cap, "%.3gM", v / 1e6);
  if (a >= 1e4) return snprintf(out, cap, "%.3gk", v / 1e3);
  return snprintf(out, cap, "%.3g", v);
}

static int series_count(const RolltuiChart* c) {
  int i, n = 0;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) n += c->s[i].used;
  return n;
}

/* The style a series draws in: its own role, else the accents in order, on the ground the chart sits on; where it has a gradient, the
 * gradient's colour at `t` (a negative `t`: the role's). */
static RolltuiStyle series_style_at(const RolltuiChart* c, int i, const RolltuiStyle* styles, const RolltuiChartRoles* roles,
                                    RolltuiStyleColorRaw ground, double t) {
  RolltuiStyle st = styles[c->s[i].role ? c->s[i].role : roles->series[i]];
  st.bg = ground;
  if (t >= 0 && c->s[i].has_gradient) rolltui_gradient_at(&c->s[i].gradient, t, &st.fg);
  return st;
}
static RolltuiStyle series_style(const RolltuiChart* c, int i, const RolltuiStyle* styles, const RolltuiChartRoles* roles,
                                 RolltuiStyleColorRaw ground) {
  return series_style_at(c, i, styles, roles, ground, -1);
}

/* Dot (dx, dy) of a cell as a bit, for the symbols: braille's own numbering, a quadrant's, or a shade's row (one dot across). */
static unsigned char dot_bit(unsigned char sym, int dx, int dy) {
  static const unsigned char braille[2][4] = {{0x01, 0x02, 0x04, 0x40}, {0x08, 0x10, 0x20, 0x80}};
  if (sym == ROLLTUI_CHART_SYMBOLS_BRAILLE) return braille[dx][dy];
  if (sym == ROLLTUI_CHART_SYMBOLS_BLOCKS) return (unsigned char)(1u << (dx + 2 * dy)); /* upper left, upper right, lower left, lower right */
  return (unsigned char)(1u << dy);
}

/* A cell's glyph from its dots, for the symbols. */
static size_t dot_glyph(unsigned char sym, unsigned char bits, int aw, char* g) {
  static const char* const kQuad[16] = {" ", "\xe2\x96\x98", "\xe2\x96\x9d", "\xe2\x96\x80", "\xe2\x96\x96", "\xe2\x96\x8c",
                                        "\xe2\x96\x9e", "\xe2\x96\x9b", "\xe2\x96\x97", "\xe2\x96\x9a", "\xe2\x96\x90", "\xe2\x96\x9c",
                                        "\xe2\x96\x84", "\xe2\x96\x99", "\xe2\x96\x9f", "\xe2\x96\x88"};
  static const char* const kShade[5] = {" ", "\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93", "\xe2\x96\x88"};
  if (sym == ROLLTUI_CHART_SYMBOLS_BRAILLE) {
    const unsigned int cp = 0x2800u + bits;
    g[0] = (char)(0xE0 | (cp >> 12));
    g[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    g[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (aw) { /* blocks and shades are wide here: a mark by how full the cell is */
    unsigned int n = 0, b = bits;
    while (b) n += b & 1u, b >>= 1;
    g[0] = n >= 3 ? '#' : n >= 2 ? '=' : '.';
    return 1;
  }
  if (sym == ROLLTUI_CHART_SYMBOLS_BLOCKS) {
    memcpy(g, kQuad[bits & 15], 3);
    return strlen(kQuad[bits & 15]);
  }
  {
    unsigned int n = 0, b = bits;
    while (b) n += b & 1u, b >>= 1;
    if (n > 4) n = 4;
    memcpy(g, kShade[n], strlen(kShade[n]));
    return strlen(kShade[n]);
  }
}

/* A LINE OR AN AREA on the dot grid: each series' points joined, an area filled down to its foot, newest at the right. */
static void draw_plot(RolltuiChart* c, RolltuiFrame* f, RolltuiRect r, double lo, double hi, const RolltuiStyle* styles,
                      const RolltuiChartRoles* roles, RolltuiStyleColorRaw ground, int aw) {
  const unsigned char sym = symbols_of(c);
  const int DX = dots_x(c), DY = dots_y(c), W = r.w * DX, H = r.h * DY;
  const int area = c->drawn_kind == ROLLTUI_CHART_AREA, flow = (c->flags & ROLLTUI_CHART_FLOW) != 0;
  const size_t cells = (size_t)r.w * (size_t)r.h;
  int i, x, y;
  c->dots = (unsigned char*)rolltui_grow(c->dots, &c->dots_cap, cells ? cells : 1, 1);
  c->owner = (unsigned char*)rolltui_grow(c->owner, &c->owner_cap, cells ? cells : 1, 1);
  memset(c->dots, 0, cells);
  memset(c->owner, 0, cells);
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
    const Series* s = &c->s[i];
    size_t first, k;
    int px = -1, py = -1;
    if (!s->used || s->n == 0) continue;
    first = s->n > (size_t)W ? s->n - (size_t)W : 0; /* the newest, when there are more than the chart is wide */
    for (k = first; k < s->n; ++k) {
      const double v = s->v[k];
      const size_t shown = s->n - first;
      int qx, qy;
      if (v != v) { /* a gap: the line breaks */
        px = -1;
        continue;
      }
      if (flow) qx = W - (int)shown + (int)(k - first); /* a value a dot across, the left empty while short */
      else qx = shown <= 1 ? 0 : (int)((double)(k - first) * (W - 1) / (double)(shown - 1) + 0.5);
      qy = (int)((hi - v) / (hi - lo) * (H - 1) + 0.5);
      if (qy < 0) qy = 0;
      if (qy > H - 1) qy = H - 1;
      if (area && v <= lo) { /* the least: nothing to fill */
        px = -1;
        continue;
      }
      if (px < 0) px = qx, py = qy;
      { /* Bresenham from the last point to this one; an area fills below each dot it reaches */
        int dx = qx - px, dy = qy - py, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, err, ex = px, ey = py;
        dx = dx < 0 ? -dx : dx;
        dy = dy < 0 ? -dy : dy;
        err = dx - dy;
        for (;;) {
          int fy;
          for (fy = ey; fy < (area ? H : ey + 1); ++fy) {
            const size_t cell = (size_t)(fy / DY) * (size_t)r.w + (size_t)(ex / DX);
            c->dots[cell] |= dot_bit(sym, ex % DX, fy % DY);
            if (!c->owner[cell] || fy == ey) c->owner[cell] = (unsigned char)(i + 1);
          }
          if (ex == qx && ey == qy) break;
          {
            const int e2 = 2 * err;
            if (e2 > -dy) err -= dy, ex += sx;
            if (e2 < dx) err += dx, ey += sy;
          }
        }
      }
      px = qx, py = qy;
    }
  }
  for (y = 0; y < r.h; ++y)
    for (x = 0; x < r.w; ++x) {
      const size_t cell = (size_t)y * (size_t)r.w + (size_t)x;
      char g[4];
      size_t gn;
      const double t = r.h > 1 ? 1.0 - (double)y / (double)(r.h - 1) : 1.0; /* a gradient by height: the foot its start */
      if (!c->dots[cell]) continue;
      gn = dot_glyph(sym, c->dots[cell], aw, g);
      rolltui_frame_put(f, r.x + x, r.y + y, g, gn, 1, series_style_at(c, c->owner[cell] - 1, styles, roles, ground, t), 0);
    }
}

/* A cell of a bar `eighths` of eight full, from its foot: a full block, a partial one, or nothing. */
static const char* eighth(int eighths, int aw) {
  static const char* const blocks[9] = {"", "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84",
                                        "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"};
  if (eighths <= 0) return "";
  if (eighths > 8) eighths = 8;
  if (aw) return eighths >= 8 ? "#" : eighths >= 4 ? "=" : "_";
  return blocks[eighths];
}

static void draw_bars(const RolltuiChart* c, RolltuiFrame* f, RolltuiRect r, double lo, double hi, const RolltuiStyle* styles,
                      const RolltuiChartRoles* roles, RolltuiStyleColorRaw ground, int aw) {
  const int ns = series_count(c), flow = (c->flags & ROLLTUI_CHART_FLOW) != 0;
  size_t n = 0, k;
  int i, slot, bar_w, gap;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i)
    if (c->s[i].used && c->s[i].n > n) n = c->s[i].n;
  if (n == 0 || ns == 0) return;
  /* a group a value, a bar a series in it, a cell between groups where there is room; flowing, a cell a value */
  slot = flow ? ns : r.w / (int)n;
  gap = !flow && slot > ns ? 1 : 0;
  bar_w = slot > 0 ? (slot - gap) / ns : 0;
  if (bar_w < 1) { /* more values than room: the newest that fit, one cell a series */
    bar_w = 1, gap = 0;
    slot = ns;
  }
  {
    const size_t fit = (size_t)(r.w / (slot > 0 ? slot : 1));
    const size_t first = n > fit ? n - fit : 0;
    const int pad = flow ? (int)(fit - (n - first)) * slot : 0; /* flowing: the newest at the right */
    for (k = first; k < n; ++k) {
      int si = 0;
      for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
        const Series* s = &c->s[i];
        double v;
        int total, x0, bx, cy;
        if (!s->used) continue;
        x0 = r.x + pad + (int)(k - first) * slot + si * bar_w;
        ++si;
        if (k >= s->n || s->v[k] != s->v[k]) continue;
        v = s->v[k] < lo ? lo : s->v[k] > hi ? hi : s->v[k];
        total = (int)((v - lo) / (hi - lo) * r.h * 8 + 0.5);
        if (total == 0 && v > lo) total = 1; /* a value above the least is never no bar */
        for (cy = 0; cy < r.h && total > 0; ++cy, total -= 8) {
          const char* g = eighth(total, aw);
          const RolltuiStyle st = series_style_at(c, i, styles, roles, ground, (v - lo) / (hi - lo));
          for (bx = 0; bx < bar_w; ++bx) rolltui_frame_put(f, x0 + bx, r.y + r.h - 1 - cy, g, strlen(g), 1, st, 0);
        }
      }
    }
  }
}

static void draw_spark(const RolltuiChart* c, RolltuiFrame* f, RolltuiRect r, double lo, double hi, const RolltuiStyle* styles,
                       const RolltuiChartRoles* roles, RolltuiStyleColorRaw ground, int aw) {
  int i, row = 0;
  const int flow = (c->flags & ROLLTUI_CHART_FLOW) != 0;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX && row < r.h; ++i) {
    const Series* s = &c->s[i];
    size_t first, k;
    int pad;
    if (!s->used) continue;
    first = s->n > (size_t)r.w ? s->n - (size_t)r.w : 0;
    pad = flow ? r.w - (int)(s->n - first) : 0;
    for (k = first; k < s->n; ++k) {
      const double v = s->v[k];
      double t;
      int e;
      const char* g;
      if (v != v) continue;
      t = ((v < lo ? lo : v > hi ? hi : v) - lo) / (hi - lo);
      e = 1 + (int)(t * 7 + 0.5); /* the least is still a mark */
      g = eighth(e, aw);
      rolltui_frame_put(f, r.x + pad + (int)(k - first), r.y + row, g, strlen(g), 1, series_style_at(c, i, styles, roles, ground, t), 0);
    }
    ++row; /* a sparkline a series, one under another */
  }
}

/* A METER a series, a row each: the label, then the newest value as ■ along the rest, its track in the axis role. */
static void draw_meters(const RolltuiChart* c, RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect r, double lo, double hi,
                        const RolltuiStyle* styles, const RolltuiChartRoles* roles, RolltuiStyleColorRaw ground, int aw) {
  const char* mark = aw ? "#" : "\xe2\x96\xa0"; /* ■ */
  const int invert = (c->flags & ROLLTUI_CHART_INVERT) != 0;
  int i, row = 0, label_w = 0;
  RolltuiStyle track = styles[roles->axis];
  track.bg = ground;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i)
    if (c->s[i].used && c->s[i].label.n) {
      const int w = rolltui_frame_text_width(draw, c->s[i].label.p, c->s[i].label.n, aw);
      if (w > label_w) label_w = w;
    }
  if (label_w) label_w += 1;
  if (label_w > r.w / 2) label_w = 0;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX && row < r.h; ++i) {
    const Series* s = &c->s[i];
    double v, t;
    int filled, x, bar_w;
    if (!s->used) continue;
    if (label_w && s->label.n) {
      RolltuiStyle ls = series_style(c, i, styles, roles, ground);
      ls.bold = 1;
      rolltui_frame_put_text(f, draw, r.x, r.y + row, s->label.p, s->label.n, ls, label_w - 1, aw, 0);
    }
    bar_w = r.w - label_w;
    v = s->n ? s->v[s->n - 1] : lo;
    t = v != v ? 0 : ((v < lo ? lo : v > hi ? hi : v) - lo) / (hi - lo);
    filled = (int)(t * bar_w + 0.5);
    if (filled == 0 && t > 0) filled = 1;
    for (x = 0; x < bar_w; ++x) {
      const int at = invert ? bar_w - 1 - x : x; /* how far along the fill this cell is */
      const int on = at < filled;
      const RolltuiStyle st = on ? series_style_at(c, i, styles, roles, ground, bar_w > 1 ? (double)at / (bar_w - 1) : 1.0) : track;
      rolltui_frame_put(f, r.x + label_w + x, r.y + row, mark, strlen(mark), 1, st, 0);
    }
    ++row;
  }
}

/* THE AXES: a line up the plot's left and one along its foot, in box drawing; and with a time, HH:MM under it every ten columns. */
static void draw_axes(const RolltuiChart* c, RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect* plot, RolltuiStyle st, int aw) {
  const int labels = c->step_s > 0 && plot->h >= 4;
  const int foot = plot->y + plot->h - 1 - labels;
  int x, y;
  if (plot->w < 4 || plot->h < 3) return;
  for (y = plot->y; y < foot; ++y) rolltui_frame_put(f, plot->x, y, aw ? "|" : "\xe2\x94\x82", aw ? 1 : 3, 1, st, 0); /* │ */
  rolltui_frame_put(f, plot->x, foot, aw ? "+" : "\xe2\x94\x94", aw ? 1 : 3, 1, st, 0);                             /* └ */
  for (x = plot->x + 1; x < plot->x + plot->w; ++x) rolltui_frame_put(f, x, foot, aw ? "-" : "\xe2\x94\x80", aw ? 1 : 3, 1, st, 0);
  if (labels) { /* the time of the value in the column, every ten from the right */
    const double per_col = plotted(c->drawn_kind) ? (double)dots_x(c) : 1.0;
    int col;
    for (col = 0; plot->x + plot->w - 1 - col > plot->x + 5; col += 10) {
      struct tm tm;
      char hm[8];
      const int cx = plot->x + plot->w - 1 - col;
      const double at = c->newest_s - (double)col * per_col * c->step_s;
      if (!rolltui_os_localtime((long long)at, &tm) || strftime(hm, sizeof hm, "%H:%M", &tm) == 0) continue;
      rolltui_frame_put_text(f, draw, cx - 4, foot + 1, hm, 5, st, 5, aw, 0);
      rolltui_frame_put(f, cx, foot, aw ? "+" : "\xe2\x94\xac", aw ? 1 : 3, 1, st, 0); /* ┬, the tick */
    }
  }
  plot->x += 1;
  plot->w -= 1;
  plot->h = foot - plot->y;
}

void rolltui_chart_draw_roles(RolltuiChart* c, RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect area, const RolltuiStyle* styles,
                              const RolltuiChartRoles* roles, int aw) {
  RolltuiStyle ground, muted;
  RolltuiRect plot = area;
  double lo, hi;
  int i, legend = 0, label_w = 0;
  char lo_s[24], hi_s[24];
  int lo_n = 0, hi_n = 0;
  if (!c || area.w <= 0 || area.h <= 0) return;
  c->drawn_kind = c->kind != ROLLTUI_CHART_AUTO ? c->kind
                  : area.h <= 1                 ? ROLLTUI_CHART_METER
                  : area.h <= 3                 ? ROLLTUI_CHART_SPARK
                                                : ROLLTUI_CHART_LINE;
  rolltui_frame_ground(f, &ground);
  muted = styles[roles->axis];
  muted.bg = ground.bg;
  range_of(c, area.w, &lo, &hi); /* a first guess, for the labels' width */
  if (c->drawn_kind == ROLLTUI_CHART_METER) {
    draw_meters(c, f, draw, area, lo, hi, styles, roles, ground.bg, aw);
    return;
  }
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) legend |= c->s[i].used && c->s[i].label.n;
  /* THE LEGEND, on the top row: each series' name in its own colour, bold, so it reads in every theme */
  if (legend && c->drawn_kind != ROLLTUI_CHART_SPARK && area.h >= 3) {
    int x = area.x;
    for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
      RolltuiStyle st;
      if (!c->s[i].used || !c->s[i].label.n) continue;
      st = series_style(c, i, styles, roles, ground.bg);
      st.bold = 1;
      if (x > area.x) x += 2;
      x += rolltui_frame_put_text(f, draw, x, area.y, c->s[i].label.p, c->s[i].label.n, st, area.x + area.w - x, aw, 0);
    }
    plot.y += 1;
    plot.h -= 1;
  }
  /* THE RANGE, on the left: the most at the top, the least at the foot */
  if (c->drawn_kind != ROLLTUI_CHART_SPARK && plot.h >= 2 && area.w >= 12) {
    hi_n = axis_label(hi, hi_s, sizeof hi_s);
    lo_n = axis_label(lo, lo_s, sizeof lo_s);
    if (hi_n < 0) hi_n = 0;
    if (lo_n < 0) lo_n = 0;
    label_w = (hi_n > lo_n ? hi_n : lo_n) + 1;
    if (label_w > area.w / 3) label_w = 0;
    if (label_w) { /* the range of what the narrower plot shows, and its labels */
      range_of(c, area.w - label_w, &lo, &hi);
      hi_n = axis_label(hi, hi_s, sizeof hi_s);
      lo_n = axis_label(lo, lo_s, sizeof lo_s);
      if (hi_n < 0) hi_n = 0;
      if (lo_n < 0) lo_n = 0;
      if ((hi_n > lo_n ? hi_n : lo_n) + 1 > label_w && (hi_n > lo_n ? hi_n : lo_n) + 1 <= area.w / 3) label_w = (hi_n > lo_n ? hi_n : lo_n) + 1;
    }
  }
  if (label_w) {
    const int axes = (c->flags & ROLLTUI_CHART_AXES) != 0;
    const int foot_label_y = plot.y + plot.h - 1 - (axes ? 1 + (c->step_s > 0 && plot.h >= 4) : 0);
    rolltui_frame_put_text(f, draw, plot.x + label_w - 1 - hi_n, plot.y, hi_s, (size_t)hi_n, muted, hi_n, aw, 0);
    rolltui_frame_put_text(f, draw, plot.x + label_w - 1 - lo_n, foot_label_y, lo_s, (size_t)lo_n, muted, lo_n, aw, 0);
    plot.x += label_w;
    plot.w -= label_w;
  }
  if (c->flags & ROLLTUI_CHART_AXES) draw_axes(c, f, draw, &plot, muted, aw);
  if (plot.w <= 0 || plot.h <= 0) return;
  if (c->drawn_kind == ROLLTUI_CHART_BARS) draw_bars(c, f, plot, lo, hi, styles, roles, ground.bg, aw);
  else if (c->drawn_kind == ROLLTUI_CHART_SPARK) draw_spark(c, f, plot, lo, hi, styles, roles, ground.bg, aw);
  else draw_plot(c, f, plot, lo, hi, styles, roles, ground.bg, aw);
}

void rolltui_chart_draw(RolltuiChart* c, RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect area, const RolltuiStyle* styles,
                        int ambiguous_wide) {
  RolltuiChartRoles r;
  r.axis = ROLLTUI_ROLE_TEXT_MUTED;
  r.series[0] = ROLLTUI_ROLE_ACCENT_1;
  r.series[1] = ROLLTUI_ROLE_ACCENT_2;
  r.series[2] = ROLLTUI_ROLE_ACCENT_3;
  r.series[3] = ROLLTUI_ROLE_ACCENT_4;
  rolltui_chart_draw_roles(c, f, draw, area, styles, &r, ambiguous_wide);
}

/* ---- a sparkline as text ------------------------------------------------------------------------ */

void rolltui_sparkline_text(const double* values, size_t n, double lo, double hi, int ascii, RolltuiStr* out) {
  static const char kAscii[] = "_.-~^";
  size_t i;
  if (!out || !values) return;
  if (!(lo < hi)) { /* the values' own range */
    int any = 0;
    for (i = 0; i < n; ++i) {
      if (values[i] != values[i]) continue;
      if (!any || values[i] < lo) lo = values[i];
      if (!any || values[i] > hi) hi = values[i];
      any = 1;
    }
  }
  for (i = 0; i < n; ++i) {
    const double v = values[i];
    double t;
    int level;
    if (v != v) { /* NaN: a gap */
      rolltui_str_append(out, " ", 1);
      continue;
    }
    t = hi > lo ? (v - lo) / (hi - lo) : 0.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (ascii) {
      level = (int)(t * (double)(sizeof kAscii - 2) + 0.5);
      rolltui_str_append(out, kAscii + level, 1);
    } else {
      char g[3];
      level = (int)(t * 7.0 + 0.5); /* U+2581 LOWER ONE EIGHTH BLOCK to U+2588 FULL BLOCK */
      g[0] = (char)0xE2, g[1] = (char)0x96, g[2] = (char)(0x81 + level);
      rolltui_str_append(out, g, 3);
    }
  }
}
