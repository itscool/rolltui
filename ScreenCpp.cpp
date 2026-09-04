// rolltui/ScreenCpp.cpp — THE C++ SIDE OF THE FRAME (Phase 14 m2).
//
// `rolltui/c/rolltui_screen.c` is the other one, and `-DROLLTUI_C` picks which links. Same
// header, same symbols, same answers; the whole test suite is the oracle for both, so a
// behavioural difference between them is a failure on the day it appears rather than a
// merge conflict later (plan/phase-14.md, THE ROLLBACK PROPERTY).
//
// This is the pre-port implementation, moved rather than rewritten: it is exactly the
// storage `rolltui::Frame` held before m2 — a `std::vector<Cell>`, two `std::vector<
// std::string>` tables with a COUNT each, and a `std::vector<Mark>`. Keeping it idiomatic
// C++ is the point. m6 compares the two files, and a C++ side written to look like the C
// would answer a question nobody asked.
//
// Phase 13's findings hold on both sides, because they are properties of the DESIGN and not
// of the language:
//   - reset REUSES every buffer, including the tables' bytes, so a steady frame allocates
//     nothing;
//   - `clear()`/`erase()` is never the reset for a container of owning elements — it
//     destroys the elements and frees exactly the storage being reused, so the tables keep
//     a COUNT and assign into what is already there;
//   - equality compares what the frame SHOWS, never retained capacity.
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/c/rolltui_screen.h"

struct RolltuiFrame {
  // The C++-side mark. `rolltui::Mark` (Effects.hpp) is the caller's shape and holds a typed
  // `EffectState`; this one holds the int that crossed, and never interprets it.
  struct Mark {
    int x, y, cells, state;
    unsigned long long since_ms;
    double fraction;
    bool operator==(const Mark&) const = default;
  };

  int w = 0, h = 0;
  std::vector<RolltuiCell> cells;
  std::vector<std::string> links;        // links[id - 1]
  std::size_t link_count = 0;
  std::vector<std::string> glyphs;       // clusters too long to sit in a Cell
  std::size_t glyph_count = 0;
  std::vector<Mark> marks;
  std::size_t mark_count = 0;
  int cx = 0, cy = 0, cvis = 0;

  bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
  RolltuiCell& at(int x, int y) { return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]; }
  const RolltuiCell& at(int x, int y) const {
    return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
  }

  // Writes a cluster into a cell, inline when it fits and into the frame's spill table when
  // it does not. The unused inline bytes are ZEROED, so cell equality compares cells and
  // not the garbage behind a shorter glyph.
  void set_glyph(RolltuiCell& c, const char* g, std::size_t n) {
    std::memset(c.bytes, 0, sizeof c.bytes);
    if (n <= ROLLTUI_CELL_INLINE_GLYPH) {
      if (n) std::memcpy(c.bytes, g, n);
      c.len = static_cast<unsigned char>(n);
      return;
    }
    // THE NAMED EXCEPTION (Phase 13's Done-when): a cluster longer than ten bytes
    // allocates, once, into a table this frame owns. A family ZWJ emoji is the real case.
    if (glyph_count == glyphs.size()) glyphs.emplace_back();
    glyphs[glyph_count].assign(g, n);
    const unsigned int idx = static_cast<unsigned int>(glyph_count++);
    std::memcpy(c.bytes, &idx, sizeof idx);
    c.len = ROLLTUI_CELL_SPILLED;
  }
};

namespace {

RolltuiCell proto_cell(RolltuiStyle fill) {
  RolltuiCell c;  // the header's own defaults: one space, width 1, no link, no spill
  c.style = fill;
  return c;
}

}  // namespace

// ---- lifetime -------------------------------------------------------------------------

// OWNERSHIP CROSSES THE BOUNDARY HERE, and it is spelled with a `unique_ptr` on both sides
// of the crossing rather than with a bare `new`/`delete` — which the library's ownership
// test refuses anyway (rolltui/tests/ownership_test.cpp). The handle is OWNED by whoever
// holds it; `rolltui::Frame` is the C++ caller that does.
extern "C" RolltuiFrame* rolltui_frame_new(int w, int h, RolltuiStyle fill) {
  std::unique_ptr<RolltuiFrame> f = std::make_unique<RolltuiFrame>();
  rolltui_frame_reset(f.get(), w, h, fill);
  return f.release();
}

extern "C" void rolltui_frame_free(RolltuiFrame* f) {
  const std::unique_ptr<RolltuiFrame> owned(f);  // takes it back, and frees it on the way out
}

extern "C" void rolltui_frame_reset(RolltuiFrame* f, int w, int h, RolltuiStyle fill) {
  f->w = std::max(w, 0);
  f->h = std::max(h, 0);
  // `assign` keeps the capacity and resets every field of every cell.
  f->cells.assign(static_cast<std::size_t>(f->w) * static_cast<std::size_t>(f->h), proto_cell(fill));
  f->link_count = 0;   // the strings stay; the next frame assigns into them
  f->glyph_count = 0;
  f->mark_count = 0;
  f->cx = f->cy = 0;
  f->cvis = 0;
}

extern "C" void rolltui_frame_clear(RolltuiFrame* f, RolltuiStyle fill) {
  std::fill(f->cells.begin(), f->cells.end(), proto_cell(fill));
  f->mark_count = 0;   // a mark names cells that have just been erased
  f->glyph_count = 0;  // …and so does every spilled glyph: nothing refers to them now
}

extern "C" RolltuiFrame* rolltui_frame_clone(const RolltuiFrame* src) {
  std::unique_ptr<RolltuiFrame> owned = std::make_unique<RolltuiFrame>();
  RolltuiFrame* f = owned.get();
  f->w = src->w;
  f->h = src->h;
  f->cells = src->cells;
  // The two tables carry their LIVE entries only, in order, so every spill index and link
  // id in the copied cells still means what it meant in `src`.
  f->links.assign(src->links.begin(), src->links.begin() + static_cast<std::ptrdiff_t>(src->link_count));
  f->link_count = src->link_count;
  f->glyphs.assign(src->glyphs.begin(), src->glyphs.begin() + static_cast<std::ptrdiff_t>(src->glyph_count));
  f->glyph_count = src->glyph_count;
  f->marks.assign(src->marks.begin(), src->marks.begin() + static_cast<std::ptrdiff_t>(src->mark_count));
  f->mark_count = src->mark_count;
  f->cx = src->cx;
  f->cy = src->cy;
  f->cvis = src->cvis;
  return owned.release();
}

// ---- geometry and cells -----------------------------------------------------------------

extern "C" int rolltui_frame_width(const RolltuiFrame* f) { return f->w; }
extern "C" int rolltui_frame_height(const RolltuiFrame* f) { return f->h; }

extern "C" void rolltui_frame_cell(const RolltuiFrame* f, int x, int y, RolltuiCell* out) {
  if (f->in_bounds(x, y)) {
    *out = f->at(x, y);
    return;
  }
  // An all-zero cell, byte for byte what the C side's `memset` produces. Matching exactly
  // matters: both implementations answer the same tests, and an out-of-bounds cell is a
  // value a caller can compare. `RolltuiCell` is trivially copyable, so this is defined.
  std::memset(out, 0, sizeof *out);
}

extern "C" void rolltui_frame_set_style(RolltuiFrame* f, int x, int y, RolltuiStyle s) {
  if (!f->in_bounds(x, y)) return;
  f->at(x, y).style = s;
}

extern "C" const char* rolltui_frame_glyph(const RolltuiFrame* f, int x, int y, size_t* len) {
  static const char kEmpty[1] = {0};
  if (!f->in_bounds(x, y)) {
    *len = 0;
    return kEmpty;
  }
  const RolltuiCell& c = f->at(x, y);
  if (!c.spilled()) {
    *len = c.len;
    return c.bytes;
  }
  unsigned int idx = 0;
  std::memcpy(&idx, c.bytes, sizeof idx);
  if (idx >= f->glyph_count) {
    *len = 0;
    return kEmpty;
  }
  *len = f->glyphs[idx].size();
  return f->glyphs[idx].data();
}

extern "C" int rolltui_frame_put(RolltuiFrame* f, int x, int y, const char* glyph, size_t glyph_len,
                                 int cells, RolltuiStyle s, unsigned int link) {
  if (y < 0 || y >= f->h || x < 0 || x >= f->w || cells <= 0) return 0;
  if (cells > 2) cells = 2;
  // Overwriting half of an existing wide glyph: blank the other half so no orphan
  // continuation cell survives.
  auto blank = [&](int cx) {
    RolltuiCell& c = f->at(cx, y);
    f->set_glyph(c, " ", 1);
    c.width = 1;
    c.continuation = 0;
    c.link = 0;
  };
  if (f->at(x, y).continuation && x > 0) blank(x - 1);
  if (f->at(x, y).width == 2 && x + 1 < f->w) blank(x + 1);
  if (cells == 2 && x + 1 >= f->w) {  // would straddle the right edge
    RolltuiCell& c = f->at(x, y);
    f->set_glyph(c, " ", 1);
    c.width = 1;
    c.continuation = 0;
    c.style = s;
    c.link = link;
    return 1;
  }
  if (cells == 2) {
    if (f->at(x + 1, y).width == 2 && x + 2 < f->w) blank(x + 2);
    RolltuiCell& r = f->at(x + 1, y);
    f->set_glyph(r, "", 0);
    r.width = 0;
    r.continuation = 1;
    r.style = s;
    r.link = link;
  }
  RolltuiCell& c = f->at(x, y);
  f->set_glyph(c, glyph, glyph_len);
  c.width = static_cast<unsigned char>(cells);
  c.continuation = 0;
  c.style = s;
  c.link = link;
  return cells;
}

// ---- the link table -----------------------------------------------------------------------

extern "C" unsigned int rolltui_frame_link_id(RolltuiFrame* f, const char* url, size_t url_len) {
  if (url_len == 0) return 0;
  const std::string_view want(url, url_len);
  for (std::size_t i = 0; i < f->link_count; ++i)
    if (f->links[i] == want) return static_cast<unsigned int>(i + 1);
  if (f->link_count == f->links.size()) f->links.emplace_back();
  f->links[f->link_count].assign(url, url_len);  // reuses the buffer a past frame left here
  return static_cast<unsigned int>(++f->link_count);
}

extern "C" const char* rolltui_frame_link(const RolltuiFrame* f, unsigned int id, size_t* len) {
  static const char kEmpty[1] = {0};
  if (id == 0 || static_cast<std::size_t>(id) > f->link_count) {
    *len = 0;
    return kEmpty;
  }
  *len = f->links[id - 1].size();
  return f->links[id - 1].data();
}

// ---- marks -----------------------------------------------------------------------------------

extern "C" void rolltui_frame_mark(RolltuiFrame* f, int x, int y, int cells, int state,
                                   unsigned long long since_ms, double fraction) {
  if (cells <= 0 || state == 0) return;  // EffectState::None is 0
  if (y < 0 || y >= f->h || x >= f->w) return;
  // Phase 13 m5b's rule, one more time: a COUNT rather than a clear, so the vector's storage
  // survives a reset and a frame that marks the same spans every paint allocates nothing.
  if (f->mark_count == f->marks.size()) f->marks.emplace_back();
  f->marks[f->mark_count++] = RolltuiFrame::Mark{x, y, cells, state, since_ms, fraction};
}

extern "C" size_t rolltui_frame_mark_count(const RolltuiFrame* f) { return f->mark_count; }

extern "C" void rolltui_frame_mark_at(const RolltuiFrame* f, size_t i, int* x, int* y, int* cells,
                                      int* state, unsigned long long* since_ms, double* fraction) {
  const RolltuiFrame::Mark& m = f->marks[i];
  *x = m.x;
  *y = m.y;
  *cells = m.cells;
  *state = m.state;
  *since_ms = m.since_ms;
  *fraction = m.fraction;
}

// ---- cursor -------------------------------------------------------------------------------------

extern "C" void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible) {
  f->cx = x;
  f->cy = y;
  f->cvis = visible;
}

extern "C" void rolltui_frame_cursor(const RolltuiFrame* f, int* x, int* y, int* visible) {
  *x = f->cx;
  *y = f->cy;
  *visible = f->cvis;
}

// ---- equality ----------------------------------------------------------------------------------

extern "C" int rolltui_frame_equal(const RolltuiFrame* a, const RolltuiFrame* b) {
  if (a->w != b->w || a->h != b->h) return 0;
  if (a->cx != b->cx || a->cy != b->cy || a->cvis != b->cvis) return 0;
  if (a->cells != b->cells) return 0;
  if (a->mark_count != b->mark_count) return 0;
  if (a->link_count != b->link_count || a->glyph_count != b->glyph_count) return 0;
  for (std::size_t i = 0; i < a->mark_count; ++i)
    if (!(a->marks[i] == b->marks[i])) return 0;
  // Only the LIVE table entries: retained capacity past a reset is not shown (m5b).
  for (std::size_t i = 0; i < a->link_count; ++i)
    if (a->links[i] != b->links[i]) return 0;
  for (std::size_t i = 0; i < a->glyph_count; ++i)
    if (a->glyphs[i] != b->glyphs[i]) return 0;
  return 1;
}
