/* rolltui/c/rolltui_render.c — see rolltui_render.h. */
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_render.h"

static void put_lit(RolltuiStr* out, const char* s) { rolltui_str_append(out, s, strlen(s)); }

/* The SGR state carried across `emit_run` calls, BY VALUE. See the header's rule 2: this was
 * a `const Style*` into a cell once, and it was a use-after-free waiting for the frame to
 * become a handle. */
typedef struct {
  RolltuiStyle style;
  int have;
  int has_ground;              /* a cell whose background is `ground` is drawn on the terminal's own background */
  RolltuiStyleColorRaw ground; /* (an inline region: it stands in the person's terminal, not on a painted screen) */
} SgrState;

static int color_eq(const RolltuiStyleColorRaw* a, const RolltuiStyleColorRaw* b);

static void sgr_start(SgrState* s, const RolltuiFrame* f, int ground) {
  s->have = 0;
  memset(&s->style, 0, sizeof s->style);
  RolltuiStyle g;
  rolltui_frame_ground(f, &g);
  s->has_ground = ground && g.bg.kind != 0;
  s->ground = g.bg;
}

static int color_eq(const RolltuiStyleColorRaw* a, const RolltuiStyleColorRaw* b) {
  return a->kind == b->kind && a->index == b->index && a->r == b->r && a->g == b->g &&
         a->b == b->b;
}

static int style_eq(const RolltuiStyle* a, const RolltuiStyle* b) {
  /* Field-wise, never memcmp: `RolltuiStyle` has padding, and reading it would call two
   * identical styles different — the same reason `rolltui_screen.h` gives for frames. */
  return color_eq(&a->fg, &b->fg) && color_eq(&a->bg, &b->bg) && a->bold == b->bold &&
         a->italic == b->italic && a->underline == b->underline && a->dim == b->dim &&
         a->reverse == b->reverse && a->strike == b->strike;
}

/* WHAT THE SCREEN HOLDS where it is compared with the next frame: the previous frame, or after a scrolled region, its rows moved by
 * `k` within `top`..`bottom` and blank rows in `blank` where the scroll brought them in. Mapped, never built: a steady frame
 * allocates nothing. */
typedef struct Base {
  const RolltuiFrame* prev;
  int k, top, bottom;
  RolltuiStyle blank;
  const unsigned char* same_row; /* rows that compared whole as the same, where nothing scrolled over them; NULL: none known */
} Base;

/* Two cells look the same on screen: one memcmp for nearly every cell, the glyph and the link by
 * value for the rest (`rolltui_frame_same_cell`). */
static int same(const Base* b, const RolltuiFrame* next, int x, int y) {
  int sy;
  if (b->k == 0 || y < b->top || y > b->bottom) return rolltui_frame_same_cell(b->prev, next, x, y, y);
  sy = y + b->k;
  if (sy >= b->top && sy <= b->bottom) return rolltui_frame_same_cell(b->prev, next, x, sy, y);
  return rolltui_frame_cell_is_blank(next, x, y, b->blank);
}

/* ---- the writer: where the cursor is, and the fewest bytes to get somewhere --------------------
 *
 * THE CURSOR IS TRACKED from the first move of a present to its last, so a move is the shortest of
 * the ways to make it: a CR and line feeds to the start of a row below, a CUF across a gap, a CUP
 * when nothing shorter is known to land. A glyph written in the last column leaves the cursor there
 * with the next glyph wrapping (the pending wrap), and only a CR or a CUP is trusted from there.
 *
 * THE SHORT FORMS stand on the terminal erasing in the background colour
 * (`rolltui_termfacts_active_erase`): a run of blank cells is an ECH and a move, a row's blank rest
 * an EL, and a block of rows that moved is a scrolled region. A blank is a space with no link,
 * underline, strikethrough or reverse: an erase draws exactly that. */
typedef struct Out {
  RolltuiStr* s;
  SgrState sgr;
  unsigned char depth;
  int w, origin;
  int cx, cy; /* the cursor, in screen rows, while `known` */
  int known, pending;
  int erase;
} Out;

static void out_start(Out* o, RolltuiStr* s, const RolltuiFrame* f, unsigned char depth, int origin, int ground) {
  o->s = s;
  sgr_start(&o->sgr, f, ground);
  o->depth = depth;
  o->w = rolltui_frame_width(f);
  o->origin = origin;
  o->cx = o->cy = 0;
  o->known = o->pending = 0;
  o->erase = rolltui_termfacts_active_erase();
}

static size_t put_num(char* out, const char* pre, int n, char fin) {
  const int k = snprintf(out, 24, "%s%d%c", pre, n, fin);
  return k > 0 ? (size_t)k : 0;
}

/* To (x, y) of the frame: the shortest way known to land there. */
static void move_to(Out* o, int x, int y) {
  const int sy = o->origin + y;
  char best[40], alt[40];
  size_t bn, an;
  int k;
  if (o->known && !o->pending && o->cy == sy && o->cx == x) return;
  bn = (size_t)snprintf(best, sizeof best, "\x1b[%d;%dH", sy + 1, x + 1);
  if (o->known) {
    /* along the row, rightwards */
    if (!o->pending && o->cy == sy && x > o->cx) {
      an = x - o->cx == 1 ? (memcpy(alt, "\x1b[C", 3), 3) : put_num(alt, "\x1b[", x - o->cx, 'C');
      if (an < bn) memcpy(best, alt, an), bn = an;
    }
    /* a CR, then line feeds down and a CUF along: never a line feed off the last row, as the target is on the screen */
    if (sy >= o->cy && sy - o->cy <= 4) {
      an = 0;
      alt[an++] = '\r';
      for (k = 0; k < sy - o->cy; ++k) alt[an++] = '\n';
      if (x == 1) memcpy(alt + an, "\x1b[C", 3), an += 3;
      else if (x > 1) an += put_num(alt + an, "\x1b[", x, 'C');
      if (an < bn) memcpy(best, alt, an), bn = an;
    }
  }
  rolltui_str_append(o->s, best, bn);
  o->cx = x, o->cy = sy, o->known = 1, o->pending = 0;
}

static void set_style(Out* o, RolltuiStyle st) {
  char sgr[ROLLTUI_SGR_MAX];
  size_t n;
  if (o->sgr.has_ground && color_eq(&st.bg, &o->sgr.ground)) memset(&st.bg, 0, sizeof st.bg);
  if (o->sgr.have && style_eq(&o->sgr.style, &st)) return;
  n = o->sgr.have ? rolltui_sgr_delta(&o->sgr.style, &st, o->depth, sgr, sizeof sgr) : rolltui_sgr(&st, o->depth, sgr, sizeof sgr);
  rolltui_str_append(o->s, sgr, n);
  o->sgr.style = st;
  o->sgr.have = 1;
}

static void advance(Out* o, int cells) {
  o->cx += cells;
  if (o->cx >= o->w) o->cx = o->w - 1, o->pending = 1;
}

static int is_blank(const RolltuiCellRaw* c) {
  return !c->continuation && c->width == 1 && c->len == 1 && c->bytes[0] == ' ' && c->link == 0 && !c->style.underline &&
         !c->style.reverse && !c->style.strike;
}

/* Cells [x0, x1) of row y: moved to, then their links, styles and glyphs, a run of blanks as an erase where that is shorter. */
static void emit_span(Out* o, const RolltuiFrame* f, int y, int x0, int x1) {
  unsigned int link = 0;
  int x = x0;
  move_to(o, x0, y);
  while (x < x1) {
    RolltuiCellRaw c;
    size_t glen = 0;
    const char* g;
    rolltui_frame_cell(f, x, y, &c);
    if (c.continuation) { /* the right half of a glyph whose lead was written */
      ++x;
      continue;
    }
    if (c.link != link) { /* before the SGR, so a link closes right after its last glyph */
      size_t ulen = 0;
      const char* url = rolltui_frame_link(f, c.link, &ulen);
      put_lit(o->s, "\x1b]8;;");
      if (ulen) rolltui_str_append(o->s, url, ulen);
      put_lit(o->s, "\x1b\\");
      link = c.link;
    }
    if (o->erase && is_blank(&c)) {
      int xb = x + 1;
      while (xb < x1) {
        RolltuiCellRaw d;
        rolltui_frame_cell(f, xb, y, &d);
        if (!is_blank(&d) || !style_eq(&d.style, &c.style)) break;
        ++xb;
      }
      if (xb == o->w && x1 == o->w && xb - x >= 4) { /* the row's blank rest: EL, and the cursor stays */
        set_style(o, c.style);
        put_lit(o->s, "\x1b[K");
        return;
      }
      if (xb - x >= 12) { /* a long run inside: ECH, and a move past it */
        char ech[24];
        set_style(o, c.style);
        rolltui_str_append(o->s, ech, put_num(ech, "\x1b[", xb - x, 'X'));
        x = xb;
        if (x < x1) move_to(o, x, y);
        continue;
      }
    }
    set_style(o, c.style);
    g = rolltui_frame_glyph(f, x, y, &glen); /* borrows from the FRAME */
    if (glen) rolltui_str_append(o->s, g, glen);
    advance(o, c.width > 1 ? c.width : 1);
    x += c.width > 1 ? c.width : 1;
  }
  if (link != 0) put_lit(o->s, "\x1b]8;;\x1b\\");
}

/* The cells x0..x1 of row y where the cursor already is, for the lines an inline region commits: no moves, no erases. */
static void emit_cells(RolltuiStr* out, const RolltuiFrame* f, int y, int x0, int x1, unsigned char depth,
                       SgrState* current) {
  unsigned int link = 0;
  for (int x = x0; x < x1; ++x) {
    RolltuiCellRaw c;
    rolltui_frame_cell(f, x, y, &c);
    if (c.continuation) continue;
    if (c.link != link) {
      size_t ulen = 0;
      const char* url = rolltui_frame_link(f, c.link, &ulen);
      put_lit(out, "\x1b]8;;");
      if (ulen) rolltui_str_append(out, url, ulen);
      put_lit(out, "\x1b\\");
      link = c.link;
    }
    if (current->has_ground && color_eq(&c.style.bg, &current->ground)) memset(&c.style.bg, 0, sizeof c.style.bg);
    if (!current->have || !style_eq(&current->style, &c.style)) {
      char sgr[ROLLTUI_SGR_MAX];
      const size_t n = current->have ? rolltui_sgr_delta(&current->style, &c.style, depth, sgr, sizeof sgr)
                                     : rolltui_sgr(&c.style, depth, sgr, sizeof sgr);
      rolltui_str_append(out, sgr, n);
      current->style = c.style;
      current->have = 1;
    }
    size_t glen = 0;
    const char* g = rolltui_frame_glyph(f, x, y, &glen); /* borrows from the FRAME */
    if (glen) rolltui_str_append(out, g, glen);
  }
  if (link != 0) put_lit(out, "\x1b]8;;\x1b\\");
}

static void finish(Out* o, const RolltuiFrame* f) {
  int cx = 0, cy = 0, vis = 0;
  rolltui_frame_cursor(f, &cx, &cy, &vis);
  put_lit(o->s, "\x1b[0m");
  o->sgr.have = 0;
  move_to(o, cx, cy);
  if (vis) put_lit(o->s, "\x1b[?25h");
}

/* A FULL REPAINT WRITES EVERY CELL, each in its own style, so nothing is cleared first: a clear is a
 * blank screen a terminal may show between it and the frame. A row's blank rest is an erase in its
 * own background, which is a cell written, not a clear. */
void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out) {
  rolltui_render_full_at(next, depth, 0, 0, out);
}

void rolltui_render_full_at(const RolltuiFrame* next, unsigned char depth, int origin, int ground, RolltuiStr* out) {
  Out o;
  put_lit(out, "\x1b[?25l");
  out_start(&o, out, next, depth, origin, ground);
  /* The geometry is read ONCE. `width()`/`height()` are calls that do not inline, so a loop
   * condition calling one is a call per cell — invisible in C++, real here. */
  const int w = rolltui_frame_width(next), h = rolltui_frame_height(next);
  for (int y = 0; y < h; ++y) emit_span(&o, next, y, 0, w);
  finish(&o, next);
}

int rolltui_render_row_extent(const RolltuiFrame* f, int y) {
  const int w = rolltui_frame_width(f);
  RolltuiStyle ground;
  int x, extent = 0;
  rolltui_frame_ground(f, &ground);
  for (x = 0; x < w; ++x) {
    RolltuiCellRaw c;
    size_t glen = 0;
    const char* g;
    rolltui_frame_cell(f, x, y, &c);
    if (c.continuation) continue;
    g = rolltui_frame_glyph(f, x, y, &glen);
    if ((glen == 0 || (glen == 1 && g[0] == ' ')) && (c.style.bg.kind == 0 || color_eq(&c.style.bg, &ground.bg)) &&
        !c.style.reverse && !c.style.underline && !c.style.strike)
      continue; /* a blank on the ground: the terminal keeps nothing there */
    extent = x + (c.width > 1 ? c.width : 1);
  }
  return extent;
}

int rolltui_render_lines(const RolltuiFrame* f, unsigned char depth, int screen_w, int ground, RolltuiStr* out) {
  const int h = rolltui_frame_height(f);
  int y, rows = 0;
  for (y = 0; y < h; ++y) {
    const int extent = rolltui_render_row_extent(f, y);
    SgrState current;
    sgr_start(&current, f, ground);
    emit_cells(out, f, y, 0, extent, depth, &current);
    put_lit(out, "\x1b[0m\r\n");
    rows += screen_w > 0 && extent > screen_w ? (extent + screen_w - 1) / screen_w : 1;
  }
  return rows;
}

void rolltui_frame_to_text(const RolltuiFrame* f, RolltuiStr* out) {
  const int w = rolltui_frame_width(f), h = rolltui_frame_height(f);
  RolltuiStr row = {0};
  for (int y = 0; y < h; ++y) {
    rolltui_str_clear(&row); /* keeps the capacity: one buffer for every row of every call */
    for (int x = 0; x < w; ++x) {
      RolltuiCellRaw c;
      rolltui_frame_cell(f, x, y, &c);
      if (c.continuation) continue;
      size_t glen = 0;
      const char* g = rolltui_frame_glyph(f, x, y, &glen);
      if (glen) rolltui_str_append(&row, g, glen);
    }
    size_t len = 0;
    const char* text = rolltui_str_get(&row, &len);
    while (len > 0 && text[len - 1] == ' ') --len; /* trailing spaces are not content */
    if (len) rolltui_str_append(out, text, len);
    put_lit(out, "\n");
  }
  rolltui_str_free(&row);
}

void rolltui_render_diff(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth,
                         RolltuiStr* out) {
  rolltui_render_diff_at(prev, next, depth, 0, 0, out);
}

/* THE CHANGED SPANS OF A ROW, the shortest way: each span of changed cells, a wide glyph's two cells
 * always together, and two spans joined across a gap of unchanged cells where writing the gap again
 * is no longer than stepping over it. */
static void diff_row(Out* o, const Base* prev, const RolltuiFrame* next, int y, int w, int* hid) {
  int x = 0, have = 0, s0 = 0, s1 = 0;
  if (prev->same_row && prev->same_row[y]) return; /* compared whole, and the same */
  while (x < w) {
    int start, end;
    RolltuiCellRaw c;
    if (same(prev, next, x, y)) {
      ++x;
      continue;
    }
    start = x;
    rolltui_frame_cell(next, start, y, &c);
    /* Rewrite a wide glyph WHOLE: if the first changed cell is the second half of a two-cell
     * glyph, back up so its lead cell is re-emitted too. Emitting only the tail would leave the
     * terminal with half a character (render_replay_test's random frames reach it). */
    if (c.continuation && start > 0) --start;
    end = x + 1;
    while (end < w && !same(prev, next, end, y)) ++end;
    if (end < w) {
      rolltui_frame_cell(next, end, y, &c);
      if (c.continuation) ++end;
    }
    if (have) {
      /* the gap s1..start: unchanged cells in the span's last style, no links, each one ASCII byte, cost their bytes; stepping over
       * costs a CUF */
      int g, cheap = start - s1 <= 3;
      RolltuiCellRaw last;
      rolltui_frame_cell(next, s1 - 1, y, &last);
      for (g = s1; cheap && g < start; ++g) {
        RolltuiCellRaw d;
        rolltui_frame_cell(next, g, y, &d);
        cheap = !d.continuation && d.width == 1 && d.len == 1 && d.link == 0 && last.link == 0 && style_eq(&d.style, &last.style);
      }
      if (cheap) {
        s1 = end;
        x = end;
        continue;
      }
      if (!*hid) put_lit(o->s, "\x1b[?25l"), *hid = 1;
      emit_span(o, next, y, s0, s1);
    }
    have = 1, s0 = start, s1 = end;
    x = end;
  }
  if (have) {
    if (!*hid) put_lit(o->s, "\x1b[?25l"), *hid = 1;
    emit_span(o, next, y, s0, s1);
  }
}

/* A BLOCK OF ROWS THAT MOVED: the shift `k` (up for k > 0) and the region it moves in, when scrolling the region on the terminal and
 * writing what is left saves more rows than it costs. Rows are matched by hash and then cell by cell. 0 when nothing moved. */
#define ROWS_MAX 512 /* a taller frame is diffed cell by cell, without looking for a moved block: these buffers are on the stack */
static int find_scroll(const RolltuiFrame* prev, const RolltuiFrame* next, int h, const unsigned char* same_row, int* top,
                       int* bottom) {
  unsigned long long hp[ROWS_MAX], hn[ROWS_MAX];
  unsigned char changed[ROWS_MAX];
  const int w = rolltui_frame_width(next);
  int y, k, best_k = 0, best_net = 2, best_a = 0, best_b = 0, n_changed = 0;
  if (h < 4 || h > ROWS_MAX) return 0;
  for (y = 0; y < h; ++y) n_changed += changed[y] = !same_row[y];
  if (n_changed < 3) return 0;
  for (y = 0; y < h; ++y) hp[y] = rolltui_frame_row_hash(prev, y), hn[y] = rolltui_frame_row_hash(next, y);
  for (k = -(h - 2); k <= h - 2; ++k) {
    int a;
    if (k == 0) continue;
    /* each run of rows of `next` that are rows of `prev` moved by k */
    for (a = 0; a < h; ++a) {
      int b = a, net = 0, lost = 0, r, lo, hi;
      if (a + k < 0 || a + k >= h || hn[a] != hp[a + k] || !changed[a]) continue;
      while (b + 1 < h && b + 1 + k >= 0 && b + 1 + k < h && hn[b + 1] == hp[b + 1 + k]) ++b;
      for (r = a; r <= b; ++r) net += changed[r];
      /* the rows the scroll brings in blank, which were right before it and are not now */
      lo = k > 0 ? b + 1 : a + k;
      hi = k > 0 ? b + k : a - 1;
      for (r = lo; r <= hi; ++r) lost += !changed[r];
      net -= lost;
      if (net > best_net) best_net = net, best_k = k, best_a = a, best_b = b;
      a = b;
    }
  }
  if (best_k != 0) { /* the hashes said so; the cells must too */
    for (y = best_a; y <= best_b && best_k; ++y) {
      int x;
      for (x = 0; x < w; ++x)
        if (!rolltui_frame_same_cell(prev, next, x, y + best_k, y)) {
          best_k = 0;
          break;
        }
    }
  }
  *top = best_k > 0 ? best_a : best_a + best_k;
  *bottom = best_k > 0 ? best_b + best_k : best_b;
  return best_k;
}

void rolltui_render_diff_at(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth, int origin, int ground,
                            RolltuiStr* out) {
  /* RULE 1 (header): a size change repaints in full, and it is checked HERE so no caller has
   * to remember it. */
  if (!prev || rolltui_frame_width(prev) != rolltui_frame_width(next) ||
      rolltui_frame_height(prev) != rolltui_frame_height(next)) {
    rolltui_render_full_at(next, depth, origin, ground, out);
    return;
  }
  const int w = rolltui_frame_width(next), h = rolltui_frame_height(next);
  int hid = 0, top = 0, bottom = 0, k = 0;
  Base base;
  Out o;
  unsigned char same_row[ROWS_MAX];
  out_start(&o, out, next, depth, origin, ground);
  memset(&base, 0, sizeof base);
  base.prev = prev;
  if (h <= ROWS_MAX) { /* each row compared whole, once: most rows of most frames are the same */
    for (int y = 0; y < h; ++y) same_row[y] = (unsigned char)rolltui_frame_rows_same(prev, y, next, y);
    base.same_row = same_row;
  }
  if (o.erase && base.same_row) k = find_scroll(prev, next, h, same_row, &top, &bottom);
  if (k != 0) {
    /* SCROLLED ON THE TERMINAL, in the default background so the rows it brings in are known; the rest is diffed against what the
     * screen then holds */
    char seq[64];
    int n;
    RolltuiStyle blank;
    memset(&blank, 0, sizeof blank);
    put_lit(out, "\x1b[?25l");
    hid = 1;
    n = snprintf(seq, sizeof seq, "\x1b[0m\x1b[%d;%dr\x1b[%d%c\x1b[r", origin + top + 1, origin + bottom + 1, k > 0 ? k : -k, k > 0 ? 'S' : 'T');
    if (n > 0) rolltui_str_append(out, seq, (size_t)n);
    o.sgr.have = 0;
    o.known = 0; /* a scrolling region homes the cursor */
    if (o.sgr.has_ground) blank.bg = o.sgr.ground; /* on an inline region, the default background is its ground */
    base.k = k, base.top = top, base.bottom = bottom, base.blank = blank;
    for (int y = top; y <= bottom; ++y) same_row[y] = 0; /* what the scroll moved is compared again, against where it went */
  }
  for (int y = 0; y < h; ++y) diff_row(&o, &base, next, y, w, &hid);
  int pcx, pcy, pvis, ncx, ncy, nvis;
  rolltui_frame_cursor(prev, &pcx, &pcy, &pvis);
  rolltui_frame_cursor(next, &ncx, &ncy, &nvis);
  if (!hid && pcx == ncx && pcy == ncy && pvis == nvis) return; /* nothing to do: an idle screen costs no bytes */
  if (!hid) put_lit(out, "\x1b[?25l");
  finish(&o, next);
}
