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

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_chart.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

typedef struct Series {
  RolltuiStr label;
  double* v;
  size_t n, cap;
  unsigned char role;
  int used;
} Series;

struct RolltuiChart {
  unsigned char kind;
  Series s[ROLLTUI_CHART_SERIES_MAX];
  double lo, hi;
  int fixed;
  unsigned char* dots; /* the braille grid, a byte a cell */
  unsigned char* owner;
  size_t dots_cap, owner_cap;
};

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
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
    rolltui_str_clear(&c->s[i].label);
    c->s[i].n = 0;
    c->s[i].used = 0;
    c->s[i].role = 0;
  }
}

void rolltui_chart_set_kind(RolltuiChart* c, unsigned char kind) {
  if (c && kind <= ROLLTUI_CHART_SPARK) c->kind = kind;
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

/* The first value of series `i` a plot `w` cells wide shows: more values than fit show the newest, so the range is theirs alone and a
 * spike that has scrolled off no longer flattens the rest. */
static size_t first_shown(const RolltuiChart* c, int i, int w) {
  const size_t n = c->s[i].n;
  size_t fit;
  if (w <= 0) return n;
  if (c->kind == ROLLTUI_CHART_LINE) fit = (size_t)w * 2;
  else if (c->kind == ROLLTUI_CHART_SPARK) fit = (size_t)w;
  else { /* bars: the groups that fit, as draw_bars lays them out */
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
  if (any && c->kind != ROLLTUI_CHART_LINE && *lo > 0) *lo = 0; /* a bar stands on zero */
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

/* The style a series draws in: its own role, else the accents in order, on the ground the chart sits on. */
static RolltuiStyle series_style(const RolltuiChart* c, int i, const RolltuiStyle* styles, const RolltuiChartRoles* roles,
                                 RolltuiStyleColorRaw ground) {
  RolltuiStyle st = styles[c->s[i].role ? c->s[i].role : roles->series[i]];
  st.bg = ground;
  return st;
}

/* Braille: dot (dx, dy) of a cell, two across and four down, as Unicode numbers them. */
static unsigned char braille_bit(int dx, int dy) {
  static const unsigned char bits[2][4] = {{0x01, 0x02, 0x04, 0x40}, {0x08, 0x10, 0x20, 0x80}};
  return bits[dx][dy];
}

static void draw_line(RolltuiChart* c, RolltuiFrame* f, RolltuiRect r, double lo, double hi, const RolltuiStyle* styles,
                      const RolltuiChartRoles* roles, RolltuiStyleColorRaw ground) {
  const int W = r.w * 2, H = r.h * 4;
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
      qx = shown <= 1 ? 0 : (int)((double)(k - first) * (W - 1) / (double)(shown - 1) + 0.5);
      qy = (int)((hi - v) / (hi - lo) * (H - 1) + 0.5);
      if (qy < 0) qy = 0;
      if (qy > H - 1) qy = H - 1;
      if (px < 0) px = qx, py = qy;
      { /* Bresenham from the last point to this one */
        int dx = qx - px, dy = qy - py, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, err, ex = px, ey = py;
        dx = dx < 0 ? -dx : dx;
        dy = dy < 0 ? -dy : dy;
        err = dx - dy;
        for (;;) {
          const size_t cell = (size_t)(ey / 4) * (size_t)r.w + (size_t)(ex / 2);
          c->dots[cell] |= braille_bit(ex % 2, ey % 4);
          c->owner[cell] = (unsigned char)(i + 1);
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
      unsigned int cp;
      if (!c->dots[cell]) continue;
      cp = 0x2800u + c->dots[cell];
      g[0] = (char)(0xE0 | (cp >> 12));
      g[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
      g[2] = (char)(0x80 | (cp & 0x3F));
      rolltui_frame_put(f, r.x + x, r.y + y, g, 3, 1, series_style(c, c->owner[cell] - 1, styles, roles, ground), 0);
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
  const int ns = series_count(c);
  size_t n = 0, k;
  int i, slot, bar_w, gap;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i)
    if (c->s[i].used && c->s[i].n > n) n = c->s[i].n;
  if (n == 0 || ns == 0) return;
  /* a group a value, a bar a series in it, a cell between groups where there is room */
  slot = r.w / (int)n;
  gap = slot > ns ? 1 : 0;
  bar_w = slot > 0 ? (slot - gap) / ns : 0;
  if (bar_w < 1) { /* more values than room: the newest that fit, one cell a series */
    bar_w = 1, gap = 0;
    slot = ns;
  }
  {
    const size_t fit = (size_t)(r.w / (slot > 0 ? slot : 1));
    const size_t first = n > fit ? n - fit : 0;
    for (k = first; k < n; ++k) {
      int si = 0;
      for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) {
        const Series* s = &c->s[i];
        double v;
        int total, x0, bx, cy;
        if (!s->used) continue;
        x0 = r.x + (int)(k - first) * slot + si * bar_w;
        ++si;
        if (k >= s->n || s->v[k] != s->v[k]) continue;
        v = s->v[k] < lo ? lo : s->v[k] > hi ? hi : s->v[k];
        total = (int)((v - lo) / (hi - lo) * r.h * 8 + 0.5);
        for (cy = 0; cy < r.h && total > 0; ++cy, total -= 8) {
          const char* g = eighth(total, aw);
          for (bx = 0; bx < bar_w; ++bx)
            rolltui_frame_put(f, x0 + bx, r.y + r.h - 1 - cy, g, strlen(g), 1, series_style(c, i, styles, roles, ground), 0);
        }
      }
    }
  }
}

static void draw_spark(const RolltuiChart* c, RolltuiFrame* f, RolltuiRect r, double lo, double hi, const RolltuiStyle* styles,
                       const RolltuiChartRoles* roles, RolltuiStyleColorRaw ground, int aw) {
  int i, row = 0;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX && row < r.h; ++i) {
    const Series* s = &c->s[i];
    size_t first, k;
    if (!s->used) continue;
    first = s->n > (size_t)r.w ? s->n - (size_t)r.w : 0;
    for (k = first; k < s->n; ++k) {
      const double v = s->v[k];
      int e;
      const char* g;
      if (v != v) continue;
      e = 1 + (int)(((v < lo ? lo : v > hi ? hi : v) - lo) / (hi - lo) * 7 + 0.5); /* the least is still a mark */
      g = eighth(e, aw);
      rolltui_frame_put(f, r.x + (int)(k - first), r.y + row, g, strlen(g), 1, series_style(c, i, styles, roles, ground), 0);
    }
    ++row; /* a sparkline a series, one under another */
  }
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
  rolltui_frame_ground(f, &ground);
  muted = styles[roles->axis];
  muted.bg = ground.bg;
  range_of(c, area.w, &lo, &hi); /* a first guess, for the labels' width */
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) legend |= c->s[i].used && c->s[i].label.n;
  /* THE LEGEND, on the top row: each series' name in its own colour, bold, so it reads in every theme */
  if (legend && c->kind != ROLLTUI_CHART_SPARK && area.h >= 3) {
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
  if (c->kind != ROLLTUI_CHART_SPARK && plot.h >= 2 && area.w >= 12) {
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
    rolltui_frame_put_text(f, draw, plot.x + label_w - 1 - hi_n, plot.y, hi_s, (size_t)hi_n, muted, hi_n, aw, 0);
    rolltui_frame_put_text(f, draw, plot.x + label_w - 1 - lo_n, plot.y + plot.h - 1, lo_s, (size_t)lo_n, muted, lo_n, aw, 0);
    plot.x += label_w;
    plot.w -= label_w;
  }
  if (plot.w <= 0 || plot.h <= 0) return;
  if (c->kind == ROLLTUI_CHART_BARS) draw_bars(c, f, plot, lo, hi, styles, roles, ground.bg, aw);
  else if (c->kind == ROLLTUI_CHART_SPARK) draw_spark(c, f, plot, lo, hi, styles, roles, ground.bg, aw);
  else draw_line(c, f, plot, lo, hi, styles, roles, ground.bg);
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
