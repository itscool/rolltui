/* rolltui/c/rolltui_frame_ops.c — see rolltui_frame_ops.h. Compiled into both
 * configurations: one implementation of the three loops, not one per language. */
#include "rolltui/rolltui.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_screen.h"

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

int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link) {
  const int w = rolltui_frame_width(f);
  size_t count, i;
  int used = 0;
  if (y < 0 || y >= rolltui_frame_height(f) || len == 0) return 0;
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
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; xx += gw) rolltui_frame_put(f, xx, yy, glyph, glyph_len, gw, style, 0);
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
      RolltuiCell cell;
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
      rolltui_frame_set_style(f, xx, yy, st);
    }
}
