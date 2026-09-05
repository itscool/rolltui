/* rolltui/c/rolltui_render.c — see rolltui_render.h. */
#include "rolltui/c/rolltui_render.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_theme.h"

static void put_lit(RolltuiStr* out, const char* s) { rolltui_str_append(out, s, strlen(s)); }

/* ESC [ y+1 ; x+1 H — one-based, which is what the terminal wants and the frame does not. */
static void put_cup(RolltuiStr* out, int x, int y) {
  char buf[32];
  const int n = snprintf(buf, sizeof buf, "\x1b[%d;%dH", y + 1, x + 1);
  if (n > 0) rolltui_str_append(out, buf, (size_t)n);
}

/* The SGR state carried across `emit_run` calls, BY VALUE. See the header's rule 2: this was
 * a `const Style*` into a cell once, and it was a use-after-free waiting for the frame to
 * become a handle. */
typedef struct {
  RolltuiStyle style;
  int have;
} SgrState;

static int color_eq(const RolltuiStyleColor* a, const RolltuiStyleColor* b) {
  return a->kind == b->kind && a->index == b->index && a->r == b->r && a->g == b->g &&
         a->b == b->b;
}

static int style_eq(const RolltuiStyle* a, const RolltuiStyle* b) {
  /* Field-wise, never memcmp: `RolltuiStyle` has padding, and reading it would call two
   * identical styles different — the same reason `rolltui_screen.h` gives for frames. */
  return color_eq(&a->fg, &b->fg) && color_eq(&a->bg, &b->bg) && a->bold == b->bold &&
         a->italic == b->italic && a->underline == b->underline && a->dim == b->dim &&
         a->reverse == b->reverse;
}

/* Two cells look the same on screen. Glyphs and links are compared BY VALUE across the two
 * frames, never by their per-frame index: a spill index and a link id mean nothing outside
 * the frame that minted them (`rolltui_screen.h`). */
static int same(const RolltuiFrame* a, const RolltuiFrame* b, int x, int y) {
  RolltuiCell p, q;
  rolltui_frame_cell(a, x, y, &p);
  rolltui_frame_cell(b, x, y, &q);
  if (p.width != q.width || p.continuation != q.continuation) return 0;
  if (!style_eq(&p.style, &q.style)) return 0;
  size_t alen = 0, blen = 0;
  const char* ag = rolltui_frame_glyph(a, x, y, &alen);
  const char* bg = rolltui_frame_glyph(b, x, y, &blen);
  if (alen != blen || (alen && memcmp(ag, bg, alen) != 0)) return 0;
  size_t aurl = 0, burl = 0;
  const char* au = rolltui_frame_link(a, p.link, &aurl);
  const char* bu = rolltui_frame_link(b, q.link, &burl);
  if (aurl != burl) return 0;
  return aurl == 0 || memcmp(au, bu, aurl) == 0;
}

/* Emit cells [x0, x1) of row y, tracking the SGR state across calls. A hyperlink is opened
 * when a run enters linked cells and always closed before the run ends. */
static void emit_run(RolltuiStr* out, const RolltuiFrame* f, int y, int x0, int x1,
                     unsigned char depth, SgrState* current) {
  put_cup(out, x0, y);
  unsigned int link = 0;
  for (int x = x0; x < x1; ++x) {
    RolltuiCell c;
    rolltui_frame_cell(f, x, y, &c);
    if (c.continuation) continue;
    if (c.link != link) { /* before the SGR, so a link closes right after its last glyph */
      size_t ulen = 0;
      const char* url = rolltui_frame_link(f, c.link, &ulen);
      put_lit(out, "\x1b]8;;");
      if (ulen) rolltui_str_append(out, url, ulen);
      put_lit(out, "\x1b\\");
      link = c.link;
    }
    if (!current->have || !style_eq(&current->style, &c.style)) {
      char sgr[ROLLTUI_SGR_MAX];
      const size_t n = rolltui_sgr(&c.style, depth, sgr, sizeof sgr);
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

static void finish(RolltuiStr* out, const RolltuiFrame* f) {
  int cx = 0, cy = 0, vis = 0;
  rolltui_frame_cursor(f, &cx, &cy, &vis);
  put_lit(out, "\x1b[0m");
  put_cup(out, cx, cy);
  if (vis) put_lit(out, "\x1b[?25h");
}

void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out) {
  put_lit(out, "\x1b[?25l\x1b[H\x1b[2J");
  SgrState current;
  current.have = 0;
  memset(&current.style, 0, sizeof current.style);
  /* The geometry is read ONCE. `width()`/`height()` are calls that do not inline, so a loop
   * condition calling one is a call per cell — invisible in C++, real here. */
  const int w = rolltui_frame_width(next), h = rolltui_frame_height(next);
  for (int y = 0; y < h; ++y) emit_run(out, next, y, 0, w, depth, &current);
  finish(out, next);
}

void rolltui_frame_to_text(const RolltuiFrame* f, RolltuiStr* out) {
  const int w = rolltui_frame_width(f), h = rolltui_frame_height(f);
  RolltuiStr row = {0};
  for (int y = 0; y < h; ++y) {
    rolltui_str_clear(&row); /* keeps the capacity: one buffer for every row of every call */
    for (int x = 0; x < w; ++x) {
      RolltuiCell c;
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
  /* RULE 1 (header): a size change repaints in full, and it is checked HERE so no caller has
   * to remember it. */
  if (!prev || rolltui_frame_width(prev) != rolltui_frame_width(next) ||
      rolltui_frame_height(prev) != rolltui_frame_height(next)) {
    rolltui_render_full(next, depth, out);
    return;
  }
  size_t before = 0;
  (void)rolltui_str_get(out, &before);
  SgrState current;
  current.have = 0;
  memset(&current.style, 0, sizeof current.style);
  const int w = rolltui_frame_width(next), h = rolltui_frame_height(next);
  for (int y = 0; y < h; ++y) {
    int x = 0;
    while (x < w) {
      if (same(prev, next, x, y)) {
        ++x;
        continue;
      }
      int start = x;
      RolltuiCell c;
      rolltui_frame_cell(next, start, y, &c);
      /* Rewrite a wide glyph WHOLE: if the first changed cell is the second half of a
       * two-cell glyph, back up so its lead cell is re-emitted too. Emitting only the tail
       * would leave the terminal with half a character.
       *
       * NOT COVERED BY ANY TEST — found 2026-09-04 by a negative control that failed
       * NOTHING with this line disabled (the harness was proved live the same run: gutting
       * `emit_run` fails 4 suites). The gap is pre-existing and was equally uncovered while
       * this was C++; the port did not create it, it revealed it. A test wants a frame whose
       * changed cell is a wide glyph's continuation. */
      if (c.continuation && start > 0) --start;
      int end = x + 1;
      while (end < w && !same(prev, next, end, y)) ++end;
      if (end < w) {
        rolltui_frame_cell(next, end, y, &c);
        if (c.continuation) ++end;
      }
      size_t now = 0;
      (void)rolltui_str_get(out, &now);
      if (now == before) put_lit(out, "\x1b[?25l");
      emit_run(out, next, y, start, end, depth, &current);
      x = end;
    }
  }
  size_t now = 0;
  (void)rolltui_str_get(out, &now);
  int pcx, pcy, pvis, ncx, ncy, nvis;
  rolltui_frame_cursor(prev, &pcx, &pcy, &pvis);
  rolltui_frame_cursor(next, &ncx, &ncy, &nvis);
  const int cursor_same = pcx == ncx && pcy == ncy && pvis == nvis;
  if (now == before && cursor_same) return; /* nothing to do: an idle screen costs no bytes */
  if (now == before) put_lit(out, "\x1b[?25l");
  finish(out, next);
}
