/* rolltui/c/rolltui_screen.c — the C side of the Frame. See rolltui_screen.h.
 *
 * Everything here allocates through `rolltui_mem_*` (rolltui/Memory.hpp's C face), which
 * is CLAUDE.md's rule and is also the point: in C the entry point is TOTAL, where in C++ it
 * only ever saw the library's own explicit allocations. Phase 14's verdict reports the
 * difference, and this file is where it comes from.
 *
 * Phase 13's findings are carried across deliberately, because they are properties of the
 * design and not of the language:
 *   - reset REUSES every buffer, including the link and spill tables' bytes, so a steady
 *     frame allocates nothing;
 *   - `clear`/`erase` is never used to reset a container of owning elements — the tables
 *     keep a COUNT and assign into the storage already there;
 *   - equality compares what the frame SHOWS, not retained capacity.
 */
#include "rolltui/c/rolltui_screen.h"

#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

/* ---- an owned byte string that reuses its buffer ---------------------------------- */
typedef struct {
  char* p;
  size_t len, cap;
} Str;

static void str_assign(Str* s, const char* data, size_t n) {
  /* EXACT (strategy 3): a link URL or a spilled cluster is ASSIGNED whole, never appended
   * to, so there is nothing to amortise and doubling would only waste bytes. */
  s->p = rolltui_fit(s->p, &s->cap, n, 1);
  if (n) memcpy(s->p, data, n);
  s->len = n;
}
static void str_free(Str* s) {
  rolltui_mem_free(s->p);
  s->p = NULL;
  s->len = s->cap = 0;
}

typedef struct {
  int x, y, cells, state;
  unsigned long long since_ms;
  double fraction;
} Mark;
/* Same reason as RolltuiCell's assertion in the header: `rolltui_frame_equal` compares marks
 * with memcmp, which is only right while every byte of the struct is a byte somebody wrote. */
_Static_assert(sizeof(Mark) == 16 + 8 + 8, "Mark has padding; memcmp equality would compare bytes nobody wrote");

struct RolltuiFrame {
  int w, h;
  RolltuiCell* cells;
  size_t cell_cap; /* in cells */
  Str* links;
  size_t link_count, link_cap;
  Str* glyphs;
  size_t glyph_count, glyph_cap;
  Mark* marks;
  size_t mark_count, mark_cap;
  int cx, cy, cvis;
};

/* ---- helpers ---------------------------------------------------------------------- */

static int in_bounds(const RolltuiFrame* f, int x, int y) {
  return x >= 0 && y >= 0 && x < f->w && y < f->h;
}
static RolltuiCell* at_mut(RolltuiFrame* f, int x, int y) { return &f->cells[(size_t)y * (size_t)f->w + (size_t)x]; }
static const RolltuiCell* at_const(const RolltuiFrame* f, int x, int y) {
  return &f->cells[(size_t)y * (size_t)f->w + (size_t)x];
}

static void cell_init(RolltuiCell* c, RolltuiStyle fill) {
  memset(c, 0, sizeof *c);
  c->style = fill;
  c->bytes[0] = ' ';
  c->len = 1;
  c->width = 1;
}

/* Writes a cluster into a cell, inline when it fits and into the frame's spill table when
 * it does not. Unused inline bytes are ZEROED so cell equality compares cells and not the
 * garbage behind a shorter glyph. */
static void set_glyph(RolltuiFrame* f, RolltuiCell* c, const char* g, size_t n) {
  memset(c->bytes, 0, sizeof c->bytes);
  if (n <= ROLLTUI_CELL_INLINE_GLYPH) {
    if (n) memcpy(c->bytes, g, n);
    c->len = (unsigned char)n;
    return;
  }
  /* AMORTISED AND ZEROED (strategy 2): appended to one spill at a time, and a slot holds an
   * owned pointer that must start NULL or `str_assign` would realloc garbage. */
  f->glyphs = rolltui_grow_zeroed(f->glyphs, &f->glyph_cap, f->glyph_count + 1, sizeof *f->glyphs);
  str_assign(&f->glyphs[f->glyph_count], g, n);
  {
    unsigned int idx = (unsigned int)f->glyph_count++;
    memcpy(c->bytes, &idx, sizeof idx);
  }
  c->len = ROLLTUI_CELL_SPILLED;
}

/* ---- lifetime ---------------------------------------------------------------------- */

RolltuiFrame* rolltui_frame_new(int w, int h, RolltuiStyle fill) {
  RolltuiFrame* f = (RolltuiFrame*)rolltui_mem_alloc(sizeof(RolltuiFrame));
  memset(f, 0, sizeof *f);
  rolltui_frame_reset(f, w, h, fill);
  return f;
}

void rolltui_frame_free(RolltuiFrame* f) {
  size_t i;
  if (!f) return;
  for (i = 0; i < f->link_cap; ++i) str_free(&f->links[i]);
  for (i = 0; i < f->glyph_cap; ++i) str_free(&f->glyphs[i]);
  rolltui_mem_free(f->links);
  rolltui_mem_free(f->glyphs);
  rolltui_mem_free(f->marks);
  rolltui_mem_free(f->cells);
  rolltui_mem_free(f);
}

void rolltui_frame_reset(RolltuiFrame* f, int w, int h, RolltuiStyle fill) {
  size_t n, i;
  RolltuiCell proto;
  f->w = w > 0 ? w : 0;
  f->h = h > 0 ? h : 0;
  n = (size_t)f->w * (size_t)f->h;
  /* EXACT (strategy 3): a reset knows w * h, and this is the biggest buffer in the library —
   * doubling would make a 120x40 grid reserve 8,192 cells for the 4,800 it needs. */
  f->cells = rolltui_fit(f->cells, &f->cell_cap, n, sizeof *f->cells);
  cell_init(&proto, fill);
  for (i = 0; i < n; ++i) f->cells[i] = proto;
  /* The tables keep their bytes; only the COUNT is reset (Phase 13 m5b). */
  f->link_count = 0;
  f->glyph_count = 0;
  f->mark_count = 0;
  f->cx = f->cy = 0;
  f->cvis = 0;
}

void rolltui_frame_clear(RolltuiFrame* f, RolltuiStyle fill) {
  size_t n = (size_t)f->w * (size_t)f->h, i;
  RolltuiCell proto;
  cell_init(&proto, fill);
  for (i = 0; i < n; ++i) f->cells[i] = proto;
  f->mark_count = 0;  /* a mark names cells that have just been erased */
  f->glyph_count = 0; /* …and so does every spilled glyph */
}

RolltuiFrame* rolltui_frame_clone(const RolltuiFrame* src) {
  RolltuiFrame* f;
  size_t i, n;
  RolltuiStyle blank;
  memset(&blank, 0, sizeof blank);
  f = rolltui_frame_new(src->w, src->h, blank);
  n = (size_t)src->w * (size_t)src->h;
  for (i = 0; i < n; ++i) f->cells[i] = src->cells[i];
  for (i = 0; i < src->link_count; ++i) {
    unsigned int id = rolltui_frame_link_id(f, src->links[i].p, src->links[i].len);
    (void)id;
  }
  for (i = 0; i < src->glyph_count; ++i) {
    f->glyphs = rolltui_grow_zeroed(f->glyphs, &f->glyph_cap, f->glyph_count + 1, sizeof *f->glyphs);
    str_assign(&f->glyphs[f->glyph_count++], src->glyphs[i].p, src->glyphs[i].len);
  }
  for (i = 0; i < src->mark_count; ++i) {
    const Mark* m = &src->marks[i];
    rolltui_frame_mark(f, m->x, m->y, m->cells, m->state, m->since_ms, m->fraction);
  }
  f->cx = src->cx;
  f->cy = src->cy;
  f->cvis = src->cvis;
  return f;
}

/* ---- geometry and cells ------------------------------------------------------------ */

int rolltui_frame_width(const RolltuiFrame* f) { return f->w; }
int rolltui_frame_height(const RolltuiFrame* f) { return f->h; }

void rolltui_frame_cell(const RolltuiFrame* f, int x, int y, RolltuiCell* out) {
  if (in_bounds(f, x, y)) {
    *out = *at_const(f, x, y);
    return;
  }
  memset(out, 0, sizeof *out);
}

void rolltui_frame_set_style(RolltuiFrame* f, int x, int y, RolltuiStyle s) {
  if (!in_bounds(f, x, y)) return;
  at_mut(f, x, y)->style = s;
}

const char* rolltui_frame_glyph(const RolltuiFrame* f, int x, int y, size_t* len) {
  static const char kEmpty[1] = {0};
  const RolltuiCell* c;
  if (!in_bounds(f, x, y)) {
    *len = 0;
    return kEmpty;
  }
  c = at_const(f, x, y);
  if (c->len != ROLLTUI_CELL_SPILLED) {
    *len = c->len;
    return c->bytes;
  }
  {
    unsigned int idx;
    memcpy(&idx, c->bytes, sizeof idx);
    if (idx >= f->glyph_count) {
      *len = 0;
      return kEmpty;
    }
    *len = f->glyphs[idx].len;
    return f->glyphs[idx].p;
  }
}

int rolltui_frame_put(RolltuiFrame* f, int x, int y, const char* glyph, size_t glyph_len,
                      int cells, RolltuiStyle s, unsigned int link) {
  RolltuiCell* c;
  if (y < 0 || y >= f->h || x < 0 || x >= f->w || cells <= 0) return 0;
  if (cells > 2) cells = 2;
  /* Overwriting half of an existing wide glyph: blank the other half so no orphan
   * continuation cell survives. */
  if (at_mut(f, x, y)->continuation && x > 0) {
    RolltuiCell* b = at_mut(f, x - 1, y);
    set_glyph(f, b, " ", 1);
    b->width = 1;
    b->continuation = 0;
    b->link = 0;
  }
  if (at_mut(f, x, y)->width == 2 && x + 1 < f->w) {
    RolltuiCell* b = at_mut(f, x + 1, y);
    set_glyph(f, b, " ", 1);
    b->width = 1;
    b->continuation = 0;
    b->link = 0;
  }
  if (cells == 2 && x + 1 >= f->w) { /* would straddle the right edge */
    c = at_mut(f, x, y);
    set_glyph(f, c, " ", 1);
    c->width = 1;
    c->continuation = 0;
    c->style = s;
    c->link = link;
    return 1;
  }
  if (cells == 2) {
    RolltuiCell* r;
    if (at_mut(f, x + 1, y)->width == 2 && x + 2 < f->w) {
      RolltuiCell* b = at_mut(f, x + 2, y);
      set_glyph(f, b, " ", 1);
      b->width = 1;
      b->continuation = 0;
      b->link = 0;
    }
    r = at_mut(f, x + 1, y);
    set_glyph(f, r, "", 0);
    r->width = 0;
    r->continuation = 1;
    r->style = s;
    r->link = link;
  }
  c = at_mut(f, x, y);
  set_glyph(f, c, glyph, glyph_len);
  c->width = (unsigned char)cells;
  c->continuation = 0;
  c->style = s;
  c->link = link;
  return cells;
}

/* ---- the link table ----------------------------------------------------------------- */

unsigned int rolltui_frame_link_id(RolltuiFrame* f, const char* url, size_t url_len) {
  size_t i;
  if (url_len == 0) return 0;
  for (i = 0; i < f->link_count; ++i)
    if (f->links[i].len == url_len && memcmp(f->links[i].p, url, url_len) == 0) return (unsigned int)(i + 1);
  f->links = rolltui_grow_zeroed(f->links, &f->link_cap, f->link_count + 1, sizeof *f->links);
  str_assign(&f->links[f->link_count], url, url_len);
  return (unsigned int)++f->link_count;
}

const char* rolltui_frame_link(const RolltuiFrame* f, unsigned int id, size_t* len) {
  static const char kEmpty[1] = {0};
  if (id == 0 || (size_t)id > f->link_count) {
    *len = 0;
    return kEmpty;
  }
  *len = f->links[id - 1].len;
  return f->links[id - 1].p;
}

/* ---- marks --------------------------------------------------------------------------- */

void rolltui_frame_mark(RolltuiFrame* f, int x, int y, int cells, int state,
                        unsigned long long since_ms, double fraction) {
  Mark* m;
  if (cells <= 0 || state == 0) return; /* EffectState::None is 0 */
  if (y < 0 || y >= f->h || x >= f->w) return;
  /* AMORTISED (strategy 2), not zeroed: a Mark is plain scalars, every one of which is
   * written below before anything reads it. */
  f->marks = rolltui_grow(f->marks, &f->mark_cap, f->mark_count + 1, sizeof *f->marks);
  m = &f->marks[f->mark_count++];
  m->x = x;
  m->y = y;
  m->cells = cells;
  m->state = state;
  m->since_ms = since_ms;
  m->fraction = fraction;
}

size_t rolltui_frame_mark_count(const RolltuiFrame* f) { return f->mark_count; }

void rolltui_frame_mark_at(const RolltuiFrame* f, size_t i, int* x, int* y, int* cells,
                           int* state, unsigned long long* since_ms, double* fraction) {
  const Mark* m = &f->marks[i];
  *x = m->x;
  *y = m->y;
  *cells = m->cells;
  *state = m->state;
  *since_ms = m->since_ms;
  *fraction = m->fraction;
}

/* ---- cursor --------------------------------------------------------------------------- */

void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible) {
  f->cx = x;
  f->cy = y;
  f->cvis = visible;
}
void rolltui_frame_cursor(const RolltuiFrame* f, int* x, int* y, int* visible) {
  *x = f->cx;
  *y = f->cy;
  *visible = f->cvis;
}

/* ---- equality -------------------------------------------------------------------------- */

int rolltui_frame_equal(const RolltuiFrame* a, const RolltuiFrame* b) {
  size_t n, i;
  if (a->w != b->w || a->h != b->h) return 0;
  if (a->cx != b->cx || a->cy != b->cy || a->cvis != b->cvis) return 0;
  if (a->mark_count != b->mark_count) return 0;
  if (a->link_count != b->link_count || a->glyph_count != b->glyph_count) return 0;
  n = (size_t)a->w * (size_t)a->h;
  for (i = 0; i < n; ++i)
    if (memcmp(&a->cells[i], &b->cells[i], sizeof(RolltuiCell)) != 0) return 0;
  for (i = 0; i < a->mark_count; ++i)
    if (memcmp(&a->marks[i], &b->marks[i], sizeof(Mark)) != 0) return 0;
  /* Only the LIVE table entries: retained capacity past a reset is not shown (m5b). */
  for (i = 0; i < a->link_count; ++i)
    if (a->links[i].len != b->links[i].len || memcmp(a->links[i].p, b->links[i].p, a->links[i].len) != 0) return 0;
  for (i = 0; i < a->glyph_count; ++i)
    if (a->glyphs[i].len != b->glyphs[i].len || memcmp(a->glyphs[i].p, b->glyphs[i].p, a->glyphs[i].len) != 0)
      return 0;
  return 1;
}
