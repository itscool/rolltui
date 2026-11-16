/* rolltui/c/rolltui_frame_ops.c — see rolltui_frame_ops.h. Compiled into both
 * configurations: one implementation of the three loops, not one per language. */
#include "rolltui/rolltui.h"

#include "rolltui/c/rolltui_frame_ops.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_termfacts.h"

struct RolltuiDrawScratch {
  RolltuiUnicodeScratch* u;       /* the Unicode module's own working memory — its role */
  RolltuiUnicodeGrapheme* gs;     /* the cluster array — this module's role */
  size_t gs_cap;
};

RolltuiDrawScratch* rolltui_draw_scratch_new(void) {
  RolltuiDrawScratch* s = (RolltuiDrawScratch*)rolltui_mem_alloc(sizeof *s);
  s->u = rolltui_u_scratch_new();
  s->gs = NULL;
  s->gs_cap = 0;
  return s;
}

void rolltui_draw_scratch_free(RolltuiDrawScratch* s) {
  if (!s) return;
  rolltui_u_scratch_free(s->u);
  rolltui_mem_free(s->gs);
  rolltui_mem_free(s);
}

/* How many leading bytes of `s` are each a whole cluster one cell wide: a printable ASCII byte followed by another ASCII byte (or the
 * end). A byte followed by a non-ASCII one may lead a cluster (`e` and a combining acute, a keycap), so the run stops before it; no two
 * ASCII bytes join except CR LF, and neither is printable. */
static size_t ascii_run(const char* s, size_t n) {
  size_t i = 0;
  while (i < n) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c > 0x7E) break;
    if (i + 1 < n && (unsigned char)s[i + 1] >= 0x80) break;
    ++i;
  }
  return i;
}

int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link) {
  const int w = rolltui_frame_width(f);
  size_t count, i;
  int used = 0;
  size_t ascii;
  if (y < 0 || y >= rolltui_frame_height(f) || len == 0) return 0;
  /* THE ASCII RUN FIRST, a byte a cell, with no cluster walk: most text drawn is ASCII, and the walk is what a cell cost. */
  ascii = ascii_run(utf8, len);
  if (ascii && x >= 0) {
    size_t take = ascii;
    if ((size_t)max_cells < take) take = max_cells > 0 ? (size_t)max_cells : 0;
    used = rolltui_frame_put_ascii(f, x, y, utf8, (int)take, style, link);
    if (used < (int)ascii) return used; /* stopped by `max_cells` or the edge */
  } else {
    for (i = 0; i < ascii; ++i) {
      if (used >= max_cells || x + used >= w) return used;
      used += rolltui_frame_put(f, x + used, y, utf8 + i, 1, 1, style, link);
    }
  }
  if (ascii == len) return used;
  utf8 += ascii;
  len -= ascii;
  /* GROWING, AMORTISED: there can be no more clusters than bytes, and this is the hottest
   * caller of the cluster walk in the library — every string any widget draws. */
  s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, len, sizeof *s->gs);
  count = rolltui_u_graphemes(s->u, utf8, len, ambiguous_wide, s->gs);
  for (i = 0; i < count; ++i) {
    const RolltuiUnicodeGrapheme* g = &s->gs[i];
    if (g->width <= 0) continue;
    if (used + g->width > max_cells || x + used >= w) break;
    if (g->width == 2 && x + used + 1 >= w) break; /* never a half glyph at the edge */
    used += rolltui_frame_put(f, x + used, y, utf8 + g->offset, g->length, g->width, style, link);
  }
  return used;
}

#define ELLIPSIS "\xe2\x80\xa6"

/* Where to cut `utf8` to `max` cells: keep [0, head) and [tail, len), with an ellipsis between. 0 when it fits whole. */
static int cut_points(RolltuiDrawScratch* s, const char* utf8, size_t len, int max, int where, int aw, size_t* head, size_t* tail,
                      int* ew_out) {
  const int width = rolltui_frame_text_width(s, utf8, len, aw);
  const int ew = rolltui_u_display_width(s->u, ELLIPSIS, 3, aw);
  int room, head_cells, tail_cells, used = 0;
  size_t i, count;
  *head = len, *tail = len, *ew_out = ew;
  if (width <= max) return 0;
  room = max - ew;
  if (room < 0) room = 0;
  head_cells = where == ROLLTUI_CUT_END ? room : where == ROLLTUI_CUT_START ? 0 : room - room / 2;
  tail_cells = room - head_cells;
  *head = head_cells > 0 ? rolltui_u_fit(s->u, utf8, len, head_cells, aw, &used) : 0;
  if (head_cells > 0) tail_cells = room - used; /* what the head could not use goes to the tail */
  *tail = len;
  if (tail_cells > 0) { /* the clusters from the end that fit in `tail_cells` */
    int got = 0;
    s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, len, sizeof *s->gs);
    count = rolltui_u_graphemes(s->u, utf8, len, aw, s->gs);
    for (i = count; i-- > 0;) {
      const RolltuiUnicodeGrapheme* g = &s->gs[i];
      if (g->offset < *head || got + g->width > tail_cells) break;
      got += g->width;
      *tail = g->offset;
    }
  }
  return 1;
}

int rolltui_frame_put_text_cut(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len, RolltuiStyle style,
                               int max_cells, int where, int ambiguous_wide, unsigned int link) {
  size_t head, tail;
  int ew, used;
  if (max_cells <= 0 || len == 0) return 0;
  if (!cut_points(s, utf8, len, max_cells, where, ambiguous_wide, &head, &tail, &ew) || max_cells <= ew)
    return rolltui_frame_put_text(f, s, x, y, utf8, len, style, max_cells, ambiguous_wide, link);
  used = rolltui_frame_put_text(f, s, x, y, utf8, head, style, max_cells, ambiguous_wide, link);
  used += rolltui_frame_put_text(f, s, x + used, y, ELLIPSIS, 3, style, ew, ambiguous_wide, link);
  return used + rolltui_frame_put_text(f, s, x + used, y, utf8 + tail, len - tail, style, max_cells - used, ambiguous_wide, link);
}

int rolltui_text_cut(RolltuiDrawScratch* s, const char* utf8, size_t len, int max_cells, int where, int ambiguous_wide, RolltuiStr* out) {
  size_t head, tail;
  int ew;
  rolltui_str_clear(out);
  if (max_cells <= 0 || len == 0) return 0;
  if (!cut_points(s, utf8, len, max_cells, where, ambiguous_wide, &head, &tail, &ew)) {
    rolltui_str_set(out, utf8, len);
    return rolltui_frame_text_width(s, utf8, len, ambiguous_wide);
  }
  if (max_cells <= ew) { /* no room for the mark: as much as fits */
    int cells = 0;
    rolltui_str_set(out, utf8, rolltui_u_fit(s->u, utf8, len, max_cells, ambiguous_wide, &cells));
    return cells;
  }
  rolltui_str_set(out, utf8, head);
  rolltui_str_append(out, ELLIPSIS, 3);
  rolltui_str_append(out, utf8 + tail, len - tail);
  return rolltui_frame_text_width(s, out->p, out->n, ambiguous_wide);
}

int rolltui_frame_text_width(RolltuiDrawScratch* s, const char* utf8, size_t len, int ambiguous_wide) {
  size_t count, i;
  int w = 0;
  size_t ascii;
  if (!s || len == 0) return 0;
  ascii = ascii_run(utf8, len);
  if (ascii == len) return (int)len;
  w = (int)ascii;
  utf8 += ascii;
  len -= ascii;
  s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, len, sizeof *s->gs);
  count = rolltui_u_graphemes(s->u, utf8, len, ambiguous_wide, s->gs);
  for (i = 0; i < count; ++i)
    if (s->gs[i].width > 0) w += s->gs[i].width;
  return w;
}

/* THE SWATCH. Two cells, one row tall: a cell is about twice as tall as it is wide, so two of them
 * are roughly a square, and an eighth-block on the outer edge of each is a thin outline round the
 * fill. The outline is in the text's own foreground so it is there on any ground — the case it exists
 * for is a colour that matches the background it sits on, which a bare fill would make disappear.
 *
 * The eighth-blocks are East Asian AMBIGUOUS, and a terminal that draws those two cells wide would
 * smear the swatch across four. There a bracket pair is the outline instead — ASCII, one cell everywhere.
 * A terminal that draws no colour has nothing for a swatch to show, and gets nothing. */
int rolltui_frame_put_swatch(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, RolltuiStyleColorRaw colour,
                             RolltuiStyle frame, int max_cells, int ambiguous_wide) {
  RolltuiStyle st = frame;
  (void)s;
  if (max_cells < 2 || x < 0 || y < 0 || y >= rolltui_frame_height(f) || x + 2 > rolltui_frame_width(f)) return 0;
  if (rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_TRUECOLOR) == ROLLTUI_DEPTH_MONO) return 0;
  st.bg = colour.kind != 0 ? colour : frame.bg; /* `none` is the frame alone, on the ground it sits on */
  st.bold = st.italic = st.underline = st.dim = st.reverse = st.strike = 0; /* an outline, not text: `reverse` would swap the fill out */
  if (ambiguous_wide || rolltui_termfacts_active_wide()) {
    rolltui_frame_put(f, x, y, "[", 1, 1, st, 0);
    rolltui_frame_put(f, x + 1, y, "]", 1, 1, st, 0);
  } else {
    rolltui_frame_put(f, x, y, "\xE2\x96\x8F", 3, 1, st, 0); /* U+258F LEFT ONE EIGHTH BLOCK */
    rolltui_frame_put(f, x + 1, y, "\xE2\x96\x95", 3, 1, st, 0); /* U+2595 RIGHT ONE EIGHTH BLOCK */
  }
  return 2;
}

int rolltui_frame_put_fields(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const RolltuiRows* rows,
                             RolltuiStyle name_style, RolltuiStyle value_style, int max_cells,
                             int ambiguous_wide) {
  size_t i;
  int used = 0;
  if (!rows) return 0;
  for (i = 0; i < rows->n; ++i) {
    const RolltuiRow* row = &rows->v[i];
    if (used >= max_cells) break;
    /* The gap belongs to the field that follows, so a line never ends in trailing spaces that
     * a frame diff would then have to repaint. */
    if (used > 0)
      used += rolltui_frame_put_text(f, s, x + used, y, "  ", 2, name_style, max_cells - used, ambiguous_wide, 0);
    if (row->label.n != 0)
      used += rolltui_frame_put_text(f, s, x + used, y, row->label.p, row->label.n, name_style,
                                     max_cells - used, ambiguous_wide, 0);
    if (row->label.n != 0 && row->value.n != 0)
      used += rolltui_frame_put_text(f, s, x + used, y, " ", 1, name_style, max_cells - used, ambiguous_wide, 0);
    if (row->has_swatch && used < max_cells) {
      /* A COLOUR'S SQUARE IS FRAMED IN THE VALUE'S OWN FOREGROUND, and where the terminal draws no colour there
       * is none and no space is left for it. */
      const int sw = rolltui_frame_put_swatch(f, s, x + used, y, row->swatch, value_style, max_cells - used, ambiguous_wide);
      used += sw;
      if (sw && used < max_cells)
        used += rolltui_frame_put_text(f, s, x + used, y, " ", 1, name_style, max_cells - used, ambiguous_wide, 0);
    }
    if (row->value.n != 0)
      used += rolltui_frame_put_text(f, s, x + used, y, row->value.p, row->value.n, value_style,
                                     max_cells - used, ambiguous_wide, 0);
  }
  return used;
}

void rolltui_frame_fill(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, RolltuiStyle style,
                        const char* glyph, size_t glyph_len) {
  RolltuiRect bounds, c;
  int gw, xx, yy;
  bounds.x = 0;
  bounds.y = 0;
  bounds.w = rolltui_frame_width(f);
  bounds.h = rolltui_frame_height(f);
  {
    int out[4];
    rolltui_rect_intersect(r.x, r.y, r.w, r.h, bounds.x, bounds.y, bounds.w, bounds.h, out);
    c.x = out[0];
    c.y = out[1];
    c.w = out[2];
    c.h = out[3];
  }
  gw = (glyph && glyph_len) ? rolltui_u_display_width(s->u, glyph, glyph_len, 0) : 0;
  if (gw <= 0) {
    glyph = " ";
    glyph_len = 1;
    gw = 1;
  }
  /* ONE CELL WIDE AND INLINE, the cells are written whole, a row at a time: a window's ground is most of every frame. A cluster a
   * terminal splits (`rolltui_termfacts_active_split`) goes the long way, which sends it its lead. */
  if (gw == 1 && glyph_len <= ROLLTUI_CELL_INLINE_GLYPH && (glyph_len == 1 || !rolltui_termfacts_active_split())) {
    rolltui_frame_fill_narrow(f, c.x, c.y, c.x + c.w, c.y + c.h, glyph, glyph_len, style);
    return;
  }
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; xx += gw) rolltui_frame_put(f, xx, yy, glyph, glyph_len, gw, style, 0);
}

static unsigned char toward(unsigned char from, unsigned char to, double t) {
  const double v = (double)from + ((double)to - (double)from) * t;
  return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5));
}

/* One colour of a style, shaded: see `rolltui_frame_shade` in the header for the three cases. */
static void shade_color(RolltuiStyleColorRaw* c, RolltuiStyleColorRaw to, double t, unsigned char* dim) {
  if (to.kind == 0 /* Color::Kind::None */) return;
  if (t >= 1.0) { *c = to; return; }
  if (c->kind == 2 /* Rgb */ && to.kind == 2) {
    c->r = toward(c->r, to.r, t);
    c->g = toward(c->g, to.g, t);
    c->b = toward(c->b, to.b, t);
    return;
  }
  if (t >= 0.5) *c = to;
  else *dim = 1;
}

void rolltui_frame_shade(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style, double strength) {
  RolltuiRect c;
  int xx, yy;
  if (strength <= 0) return;
  {
    int out[4];
    rolltui_rect_intersect(r.x, r.y, r.w, r.h, 0, 0, rolltui_frame_width(f), rolltui_frame_height(f), out);
    c.x = out[0];
    c.y = out[1];
    c.w = out[2];
    c.h = out[3];
  }
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; ++xx) {
      RolltuiCellRaw cell;
      RolltuiStyle st;
      unsigned char dim_fg = 0, dim_bg = 0;
      rolltui_frame_cell(f, xx, yy, &cell);
      st = cell.style;
      shade_color(&st.fg, style.fg, strength, &dim_fg);
      shade_color(&st.bg, style.bg, strength, &dim_bg);
      st.bold |= style.bold;
      st.italic |= style.italic;
      st.underline |= style.underline;
      st.dim |= style.dim | dim_fg; /* a background has no dim to fall back on */
      st.reverse |= style.reverse;
      st.strike |= style.strike;
      rolltui_frame_set_style(f, xx, yy, st);
    }
}

void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style) {
  RolltuiRect c;
  int xx, yy;
  {
    int out[4];
    rolltui_rect_intersect(r.x, r.y, r.w, r.h, 0, 0, rolltui_frame_width(f), rolltui_frame_height(f), out);
    c.x = out[0];
    c.y = out[1];
    c.w = out[2];
    c.h = out[3];
  }
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; ++xx) {
      /* Read, amend, write back: the handle hands out a COPY of the cell, so the style a
       * widget drew is not a reference to reach through. */
      RolltuiCellRaw cell;
      RolltuiStyle st;
      rolltui_frame_cell(f, xx, yy, &cell);
      st = cell.style;
      if (style.fg.kind != 0 /* Color::Kind::None */) st.fg = style.fg;
      if (style.bg.kind != 0 /* Color::Kind::None */) st.bg = style.bg;
      st.bold |= style.bold;
      st.italic |= style.italic;
      st.underline |= style.underline;
      st.dim |= style.dim;
      st.reverse |= style.reverse;
      st.strike |= style.strike;
      rolltui_frame_set_style(f, xx, yy, st);
    }
}
