/* rolltui/c/rolltui_screen.c — the C side of the Frame. See rolltui_screen.h.
 *
 * Everything here allocates through `rolltui_mem_*` (rolltui/Memory.hpp's C face), which
 * is CLAUDE.md's rule and is also the point: in C the entry point is TOTAL, where in C++ it
 * only ever saw the library's own explicit allocations. the verdict reports the
 * difference, and this file is where it comes from.
 *
 * the findings are carried across deliberately, because they are properties of the
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
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- an owned byte string that reuses its buffer ----------------------------------------- */
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
/* Same reason as RolltuiCellRaw's assertion in the header: `rolltui_frame_equal` compares marks
 * with memcmp, which is only right while every byte of the struct is a byte somebody wrote. */
_Static_assert(sizeof(Mark) == 16 + 8 + 8, "Mark has padding; memcmp equality would compare bytes nobody wrote");

struct RolltuiFrame {
  int w, h;
  RolltuiCellRaw* cells;
  size_t cell_cap; /* in cells */
  Str* links;
  size_t link_count, link_cap;
  Str* glyphs;
  size_t glyph_count, glyph_cap;
  Mark* marks;
  size_t mark_count, mark_cap;
  RolltuiFrameImage* images; /* pictures drawn this frame (rolltui_frame_image), each a BORROW until it is presented */
  size_t image_count, image_cap;
  int cx, cy, cvis;
  unsigned char cshape; /* ROLLTUI_CARET_* */
  RolltuiStyle fill; /* what `reset` filled it with: its ground */
};

/* ---- helpers ----------------------------------------------------------------------------- */

static int in_bounds(const RolltuiFrame* f, int x, int y) {
  return x >= 0 && y >= 0 && x < f->w && y < f->h;
}
static RolltuiCellRaw* at_mut(RolltuiFrame* f, int x, int y) { return &f->cells[(size_t)y * (size_t)f->w + (size_t)x]; }
static const RolltuiCellRaw* at_const(const RolltuiFrame* f, int x, int y) {
  return &f->cells[(size_t)y * (size_t)f->w + (size_t)x];
}

static void cell_init(RolltuiCellRaw* c, RolltuiStyle fill) {
  memset(c, 0, sizeof *c);
  c->style = fill;
  c->bytes[0] = ' ';
  c->len = 1;
  c->width = 1;
}

/* Writes a cluster into a cell, inline when it fits and into the frame's spill table when
 * it does not. Unused inline bytes are ZEROED so cell equality compares cells and not the
 * garbage behind a shorter glyph. */
static void set_glyph(RolltuiFrame* f, RolltuiCellRaw* c, const char* g, size_t n) {
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

/* ---- lifetime ---------------------------------------------------------------------------- */

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
  rolltui_mem_free(f->images);
  rolltui_mem_free(f->cells);
  rolltui_mem_free(f);
}

void rolltui_frame_reset(RolltuiFrame* f, int w, int h, RolltuiStyle fill) {
  size_t n, i;
  RolltuiCellRaw proto;
  f->w = w > 0 ? w : 0;
  f->h = h > 0 ? h : 0;
  n = (size_t)f->w * (size_t)f->h;
  /* EXACT (strategy 3): a reset knows w * h, and this is the biggest buffer in the library —
   * doubling would make a 120x40 grid reserve 8,192 cells for the 4,800 it needs. */
  f->cells = rolltui_fit(f->cells, &f->cell_cap, n, sizeof *f->cells);
  f->fill = fill;
  cell_init(&proto, fill);
  if (n) { /* the first cell, then copies of what is filled, doubling */
    f->cells[0] = proto;
    for (i = 1; i < n;) {
      const size_t k = i < n - i ? i : n - i;
      memcpy(f->cells + i, f->cells, k * sizeof *f->cells);
      i += k;
    }
  }
  /* The tables keep their bytes; only the COUNT is reset. */
  f->link_count = 0;
  f->glyph_count = 0;
  f->mark_count = 0;
  f->image_count = 0;
  f->cx = f->cy = 0;
  f->cvis = 0;
  f->cshape = 0;
}

void rolltui_frame_add_image(RolltuiFrame* f, const RolltuiImage* img, RolltuiRect cells, int protocol, int px_w, int px_h,
                             RolltuiStyle ground) {
  RolltuiFrameImage* p;
  if (!f || !img || cells.w <= 0 || cells.h <= 0) return;
  f->images = (RolltuiFrameImage*)rolltui_grow(f->images, &f->image_cap, f->image_count + 1, sizeof *f->images);
  p = &f->images[f->image_count++];
  p->img = img;
  p->cells = cells;
  p->protocol = protocol;
  p->px_w = px_w;
  p->px_h = px_h;
  p->ground = ground;
  p->outside = 0;
}
void rolltui_frame_outside_picture(RolltuiFrame* f, unsigned int id, RolltuiRect cells, RolltuiStyle ground) {
  RolltuiFrameImage* p;
  int x, y, x1, y1;
  if (!f || id == 0) return;
  x1 = cells.x + cells.w < f->w ? cells.x + cells.w : f->w;
  y1 = cells.y + cells.h < f->h ? cells.y + cells.h : f->h;
  if (cells.x < 0) cells.x = 0;
  if (cells.y < 0) cells.y = 0;
  cells.w = x1 - cells.x;
  cells.h = y1 - cells.y;
  if (cells.w <= 0 || cells.h <= 0) return;
  for (y = cells.y; y < y1; ++y)
    for (x = cells.x; x < x1; ++x) rolltui_frame_put(f, x, y, " ", 1, 1, ground, 0);
  f->images = (RolltuiFrameImage*)rolltui_grow(f->images, &f->image_cap, f->image_count + 1, sizeof *f->images);
  p = &f->images[f->image_count++];
  memset(p, 0, sizeof *p);
  p->cells = cells;
  p->ground = ground;
  p->outside = id;
}
size_t rolltui_frame_image_count(const RolltuiFrame* f) { return f ? f->image_count : 0; }
const RolltuiFrameImage* rolltui_frame_image_at(const RolltuiFrame* f, size_t i) {
  return f && i < f->image_count ? &f->images[i] : NULL;
}

void rolltui_frame_forget_rect(RolltuiFrame* f, RolltuiRect r) {
  int x, y;
  for (y = r.y; y < r.y + r.h; ++y)
    for (x = r.x; x < r.x + r.w; ++x)
      if (in_bounds(f, x, y)) {
        /* a cell no draw makes: the next diff against this frame writes whatever is there now */
        RolltuiCellRaw* c = at_mut(f, x, y);
        c->bytes[0] = '\x01';
        c->len = 1;
        c->width = 1;
        c->continuation = 0;
      }
}

void rolltui_frame_clear(RolltuiFrame* f, RolltuiStyle fill) {
  size_t n = (size_t)f->w * (size_t)f->h, i;
  RolltuiCellRaw proto;
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
  f->cshape = src->cshape;
  return f;
}

/* ---- geometry and cells ------------------------------------------------------------------ */

int rolltui_frame_width(const RolltuiFrame* f) { return f->w; }
int rolltui_frame_height(const RolltuiFrame* f) { return f->h; }

void rolltui_frame_cell(const RolltuiFrame* f, int x, int y, RolltuiCellRaw* out) {
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

int rolltui_frame_same_cell(const RolltuiFrame* a, const RolltuiFrame* b, int x, int ay, int by) {
  const RolltuiCellRaw* p = at_const(a, x, ay);
  const RolltuiCellRaw* q = at_const(b, x, by);
  size_t alen, blen;
  const char *ag, *bg;
  if (p->link == 0 && q->link == 0 && p->len != ROLLTUI_CELL_SPILLED && q->len != ROLLTUI_CELL_SPILLED)
    return memcmp(p, q, sizeof *p) == 0; /* no padding, and a glyph's spare bytes are zeroed (`set_glyph`) */
  if (p->width != q->width || p->continuation != q->continuation || memcmp(&p->style, &q->style, sizeof p->style) != 0) return 0;
  ag = rolltui_frame_glyph(a, x, ay, &alen);
  bg = rolltui_frame_glyph(b, x, by, &blen);
  if (alen != blen || (alen && memcmp(ag, bg, alen) != 0)) return 0;
  ag = rolltui_frame_link(a, p->link, &alen);
  bg = rolltui_frame_link(b, q->link, &blen);
  return alen == blen && (alen == 0 || memcmp(ag, bg, alen) == 0);
}

static unsigned long long fnv(unsigned long long h, const void* p, size_t n) {
  const unsigned char* b = (const unsigned char*)p;
  size_t i;
  for (i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ULL;
  return h;
}

int rolltui_frame_rows_same(const RolltuiFrame* a, int ay, const RolltuiFrame* b, int by) {
  const RolltuiCellRaw* p = at_const(a, 0, ay);
  const RolltuiCellRaw* q = at_const(b, 0, by);
  int x;
  if (a->w != b->w || memcmp(p, q, (size_t)a->w * sizeof *p) != 0) return 0;
  for (x = 0; x < a->w; ++x) /* identical bytes: a spilled glyph's or a link's index must also name the same thing */
    if ((p[x].len == ROLLTUI_CELL_SPILLED || p[x].link != 0) && !rolltui_frame_same_cell(a, b, x, ay, by)) return 0;
  return 1;
}

/* Clears what a write over cells [x0, x1) of row y cuts: a wide glyph whose right half is x0, or whose left half is x1 - 1. */
static void cut_edges(RolltuiFrame* f, int x0, int x1, int y) {
  if (x0 > 0 && at_const(f, x0, y)->continuation) {
    RolltuiCellRaw* b = at_mut(f, x0 - 1, y);
    set_glyph(f, b, " ", 1);
    b->width = 1, b->continuation = 0, b->link = 0;
  }
  if (x1 < f->w && at_const(f, x1, y)->continuation) {
    RolltuiCellRaw* b = at_mut(f, x1, y);
    set_glyph(f, b, " ", 1);
    b->width = 1, b->continuation = 0, b->link = 0;
  }
}

int rolltui_frame_put_ascii(RolltuiFrame* f, int x, int y, const char* s, int n, RolltuiStyle st, unsigned int link) {
  RolltuiCellRaw proto;
  int i;
  if (y < 0 || y >= f->h || x < 0 || x >= f->w || n <= 0) return 0;
  if (n > f->w - x) n = f->w - x;
  cut_edges(f, x, x + n, y);
  memset(&proto, 0, sizeof proto);
  proto.style = st;
  proto.link = link;
  proto.len = 1;
  proto.width = 1;
  for (i = 0; i < n; ++i) {
    RolltuiCellRaw* c = at_mut(f, x + i, y);
    *c = proto;
    c->bytes[0] = s[i];
  }
  return n;
}

void rolltui_frame_fill_narrow(RolltuiFrame* f, int x0, int y0, int x1, int y1, const char* g, size_t len, RolltuiStyle st) {
  RolltuiCellRaw proto;
  int x, y;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > f->w) x1 = f->w;
  if (y1 > f->h) y1 = f->h;
  if (x0 >= x1 || y0 >= y1 || len == 0 || len > ROLLTUI_CELL_INLINE_GLYPH) return;
  memset(&proto, 0, sizeof proto);
  memcpy(proto.bytes, g, len);
  proto.len = (unsigned char)len;
  proto.width = 1;
  proto.style = st;
  for (y = y0; y < y1; ++y) {
    cut_edges(f, x0, x1, y);
    for (x = x0; x < x1; ++x) *at_mut(f, x, y) = proto;
  }
}

/* FOUR LANES, one per word of a cell, so the multiplies of one cell do not wait on each other: a row is hashed in the time a few
 * hundred multiplies take side by side, not one after another. */
unsigned long long rolltui_frame_row_hash(const RolltuiFrame* f, int y) {
  unsigned long long a = 14695981039346656037ULL, b = 0x9E3779B97F4A7C15ULL, c = 0xBF58476D1CE4E5B9ULL, d = 0x94D049BB133111EBULL;
  int x;
  for (x = 0; x < f->w; ++x) {
    const RolltuiCellRaw* cell = at_const(f, x, y);
    if (cell->link == 0 && cell->len != ROLLTUI_CELL_SPILLED) { /* the cell's own bytes, a word to each lane */
      unsigned long long w[4] = {0, 0, 0, 0};
      memcpy(w, cell, sizeof *cell < sizeof w ? sizeof *cell : sizeof w);
      a = (a ^ w[0]) * 1099511628211ULL;
      b = (b ^ w[1]) * 0xFF51AFD7ED558CCDULL;
      c = (c ^ w[2]) * 0xC4CEB9FE1A85EC53ULL;
      d = (d ^ w[3]) * 0x9E3779B97F4A7C15ULL;
    } else { /* by value: the glyph's and the link's bytes, never their per-frame index */
      size_t n = 0;
      const char* s = rolltui_frame_glyph(f, x, y, &n);
      unsigned long long h = 14695981039346656037ULL;
      h = fnv(h, &cell->style, sizeof cell->style);
      h = fnv(h, &cell->width, 1);
      h = fnv(h, &cell->continuation, 1);
      h = fnv(h, s, n);
      s = rolltui_frame_link(f, cell->link, &n);
      h = fnv(h, s, n);
      a = (a ^ h) * 1099511628211ULL;
    }
  }
  a ^= b * 0x9E3779B97F4A7C15ULL;
  a ^= c * 0xBF58476D1CE4E5B9ULL;
  a ^= d * 0x94D049BB133111EBULL;
  return a ^ (a >> 31);
}

int rolltui_frame_cell_is_blank(const RolltuiFrame* f, int x, int y, RolltuiStyle style) {
  const RolltuiCellRaw* c = at_const(f, x, y);
  return c->link == 0 && !c->continuation && c->width == 1 && c->len == 1 && c->bytes[0] == ' ' &&
         memcmp(&c->style, &style, sizeof style) == 0;
}

const char* rolltui_frame_glyph(const RolltuiFrame* f, int x, int y, size_t* len) {
  static const char kEmpty[1] = {0};
  const RolltuiCellRaw* c;
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
  RolltuiCellRaw* c;
  char lead[4];
  if (y < 0 || y >= f->h || x < 0 || x >= f->w || cells <= 0) return 0;
  if (cells > 2) cells = 2;
  /* A terminal that splits clusters is sent a cluster's lead alone (rolltui_termfacts.h): `é` for
   * `e` and a combining acute. */
  if (glyph_len > 1 && rolltui_termfacts_active_split()) {
    glyph_len = rolltui_u_cluster_lead(glyph, glyph_len, lead);
    glyph = lead;
  }
  /* Overwriting half of an existing wide glyph: blank the other half so no orphan
   * continuation cell survives. */
  if (at_mut(f, x, y)->continuation && x > 0) {
    RolltuiCellRaw* b = at_mut(f, x - 1, y);
    set_glyph(f, b, " ", 1);
    b->width = 1;
    b->continuation = 0;
    b->link = 0;
  }
  if (at_mut(f, x, y)->width == 2 && x + 1 < f->w) {
    RolltuiCellRaw* b = at_mut(f, x + 1, y);
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
    RolltuiCellRaw* r;
    if (at_mut(f, x + 1, y)->width == 2 && x + 2 < f->w) {
      RolltuiCellRaw* b = at_mut(f, x + 2, y);
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

/* ---- the link table ---------------------------------------------------------------------- */

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

/* ---- marks ------------------------------------------------------------------------------- */

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

/* A mark under a window drawn LATER is a mark on cells that window now owns: an effect applied
 * to it would paint motion onto the popup. Each mark is clipped to what is still visible —
 * dropped when the rect covers it, shortened when the rect takes one end, split in two when it
 * takes the middle. The order of the marks that survive is kept, so two frames drawn the same
 * way still compare equal. A split's second piece needs one more slot, counted first so the
 * array grows once (AMORTISED, strategy 2). */
void rolltui_frame_unmark_rect(RolltuiFrame* f, RolltuiRect r) {
  const int x0 = r.x, x1 = r.x + r.w, y0 = r.y, y1 = r.y + r.h;
  const size_t n = f->mark_count;
  size_t i, w = 0, extra = 0, tail = 0;
  if (r.w <= 0 || r.h <= 0 || n == 0) return;
  for (i = 0; i < n; ++i) {
    const Mark* m = &f->marks[i];
    if (m->y >= y0 && m->y < y1 && m->x < x0 && m->x + m->cells > x1) ++extra;
  }
  if (extra) f->marks = rolltui_grow(f->marks, &f->mark_cap, n + extra, sizeof *f->marks);
  for (i = 0; i < n; ++i) {
    const Mark m = f->marks[i]; /* a COPY, read before anything is written at or after i */
    const int mx1 = m.x + m.cells;
    if (m.y < y0 || m.y >= y1 || mx1 <= x0 || m.x >= x1) {
      f->marks[w++] = m;
      continue;
    }
    if (m.x < x0) { /* the piece left of the rect stays */
      Mark left = m;
      left.cells = x0 - m.x;
      f->marks[w++] = left;
    }
    if (mx1 > x1) { /* the piece right of it stays too: after the others, keeping the order */
      Mark right = m;
      right.x = x1;
      right.cells = mx1 - x1;
      if (m.x < x0) f->marks[n + tail++] = right; /* a split: its second piece parks past the end */
      else f->marks[w++] = right;
    }
  }
  for (i = 0; i < tail; ++i) f->marks[w++] = f->marks[n + i];
  f->mark_count = w;
}

/* ---- cursor ------------------------------------------------------------------------------ */

void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible) {
  f->cx = x;
  f->cy = y;
  f->cvis = visible;
}
void rolltui_frame_set_cursor_shape(RolltuiFrame* f, unsigned char shape) {
  if (f) f->cshape = shape <= ROLLTUI_CARET_BAR ? shape : ROLLTUI_CARET_DEFAULT;
}
unsigned char rolltui_frame_cursor_shape(const RolltuiFrame* f) { return f ? f->cshape : 0; }
void rolltui_frame_cursor(const RolltuiFrame* f, int* x, int* y, int* visible) {
  *x = f->cx;
  *y = f->cy;
  *visible = f->cvis;
}

/* ---- equality ---------------------------------------------------------------------------- */

int rolltui_frame_equal(const RolltuiFrame* a, const RolltuiFrame* b) {
  size_t n, i;
  if (a->w != b->w || a->h != b->h) return 0;
  if (a->cx != b->cx || a->cy != b->cy || a->cvis != b->cvis || a->cshape != b->cshape) return 0;
  if (a->mark_count != b->mark_count) return 0;
  if (a->link_count != b->link_count || a->glyph_count != b->glyph_count) return 0;
  n = (size_t)a->w * (size_t)a->h;
  for (i = 0; i < n; ++i)
    if (memcmp(&a->cells[i], &b->cells[i], sizeof(RolltuiCellRaw)) != 0) return 0;
  for (i = 0; i < a->mark_count; ++i)
    if (memcmp(&a->marks[i], &b->marks[i], sizeof(Mark)) != 0) return 0;
  /* Only the LIVE table entries: retained capacity past a reset is not shown. */
  for (i = 0; i < a->link_count; ++i)
    if (a->links[i].len != b->links[i].len || memcmp(a->links[i].p, b->links[i].p, a->links[i].len) != 0) return 0;
  for (i = 0; i < a->glyph_count; ++i)
    if (a->glyphs[i].len != b->glyphs[i].len || memcmp(a->glyphs[i].p, b->glyphs[i].p, a->glyphs[i].len) != 0)
      return 0;
  return 1;
}

void rolltui_frame_ground(const RolltuiFrame* f, RolltuiStyle* out) { *out = f->fill; }
void rolltui_frame_set_ground(RolltuiFrame* f, RolltuiStyle ground) { f->fill = ground; }
