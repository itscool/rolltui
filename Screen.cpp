// rolltui/Screen.cpp — see Screen.hpp.
#include "rolltui/Screen.hpp"

#include "rolltui/c/rolltui_geom.h"

#include "rolltui/Scratch.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

int Frame::put_text(int x, int y, std::string_view utf8, const Style& style, int max_cells,
                    bool ambiguous_wide, std::uint32_t link) {
  const int w = width();
  if (y < 0 || y >= height()) return 0;
  int used = 0;
  // Phase 13 m3: the cluster list is a REUSED buffer. This is the hottest single caller
  // of `graphemes()` — every string any widget draws comes through here — and it was
  // building a fresh vector for each one. Not nested: nothing in the loop below calls
  // back into put_text.
  static thread_local Scratch<std::vector<unicode::Grapheme>> scratch("put_text clusters");
  auto gs = scratch.lock();  // m5b: the window is CHECKED, where m3 only claimed it
  unicode::graphemes_into(utf8, ambiguous_wide, *gs);
  for (const unicode::Grapheme& g : *gs) {
    if (g.width <= 0) continue;
    if (used + g.width > max_cells || x + used >= w) break;
    if (g.width == 2 && x + used + 1 >= w) break;  // never a half glyph at the edge
    used += put(x + used, y, utf8.substr(g.offset, g.length), g.width, style, link);
  }
  return used;
}

void Frame::fill(Rect r, const Style& style, std::string_view grapheme) {
  Rect c = r.intersect(bounds());
  int gw = unicode::display_width(grapheme);
  if (gw <= 0) { grapheme = " "; gw = 1; }
  for (int yy = c.y; yy < c.y + c.h; ++yy)
    for (int xx = c.x; xx < c.x + c.w; xx += gw) put(xx, yy, grapheme, gw, style);
}

void Frame::tint(Rect r, const Style& style) {
  Rect c = r.intersect(bounds());
  for (int yy = c.y; yy < c.y + c.h; ++yy)
    for (int xx = c.x; xx < c.x + c.w; ++xx) {
      // Read, amend, write back: the handle hands out a COPY of the cell, so the style a
      // widget drew is not a reference to reach through. A Style is fifteen bytes and this
      // allocates nothing either way.
      Style s = at(xx, yy).style;
      if (style.fg.kind != Color::Kind::None) s.fg = style.fg;
      if (style.bg.kind != Color::Kind::None) s.bg = style.bg;
      s.bold |= style.bold;
      s.italic |= style.italic;
      s.underline |= style.underline;
      s.dim |= style.dim;
      s.reverse |= style.reverse;
      set_style(xx, yy, s);
    }
}

namespace {

std::string cup(int x, int y) {
  return "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
}

// The SGR state carried across `emit_run` calls, BY VALUE.
//
// PHASE 14 m2 FIXED A REAL DEFECT HERE, found while designing the C boundary and present in
// the C++ long before it: this used to be a `const Style*` pointing INTO a cell
// (`current = &c.style`), kept across loop iterations and across calls. It was correct only
// because `Frame::at()` happened to return a reference into the frame's own storage — the
// moment an opaque handle made it return a Cell BY VALUE, that pointer aimed at a destroyed
// temporary and every SGR decision after it read freed memory. A `Style` is fifteen bytes;
// there was never a reason for the pointer.
struct SgrState {
  Style style;
  bool have = false;
};

// Emit cells [x0, x1) of row y, tracking the SGR state across calls. A hyperlink is
// opened when a run enters linked cells and always closed before the run ends.
void emit_run(std::string& out, const Frame& f, int y, int x0, int x1, ColorDepth depth, SgrState& current) {
  out += cup(x0, y);
  std::uint32_t link = 0;
  for (int x = x0; x < x1; ++x) {
    const Cell c = f.at(x, y);
    if (c.continuation) continue;
    if (c.link != link) {  // before the SGR, so a link closes right after its last glyph
      out += "\x1b]8;;";
      out += f.link(c.link);
      out += "\x1b\\";
      link = c.link;
    }
    if (!current.have || !(current.style == c.style)) {
      out += sgr(c.style, depth);
      current.style = c.style;
      current.have = true;
    }
    out += f.glyph(x, y);  // borrows from the FRAME, not from the copy above
  }
  if (link != 0) out += "\x1b]8;;\x1b\\";
}

// Two cells look the same on screen: everything equal, links compared by URL (the ids
// are per frame).
bool same(const Frame& a, const Frame& b, int x, int y) {
  const Cell p = a.at(x, y);
  const Cell q = b.at(x, y);
  // Glyphs and links are both compared BY VALUE across the two frames, never by their
  // per-frame index: a spill index and a link id mean nothing outside the frame that
  // minted them (Screen.hpp).
  return a.glyph(x, y) == b.glyph(x, y) && p.width == q.width && p.continuation == q.continuation &&
         p.style == q.style && a.link(p.link) == b.link(q.link);
}

void finish(std::string& out, const Frame& f) {
  const Cursor c = f.cursor();
  out += "\x1b[0m";
  out += cup(c.x, c.y);
  if (c.visible) out += "\x1b[?25h";
}

}  // namespace

std::string render_full(const Frame& next, ColorDepth depth) {
  std::string out = "\x1b[?25l\x1b[H\x1b[2J";
  SgrState current;
  // The geometry is read ONCE. `width()`/`height()` cross the C boundary and do not inline,
  // so a loop condition that calls one is a call per cell — invisible in C++, real here.
  const int w = next.width(), h = next.height();
  for (int y = 0; y < h; ++y) emit_run(out, next, y, 0, w, depth, current);
  finish(out, next);
  return out;
}

std::string frame_to_text(const Frame& f) {
  std::string out;
  const int w = f.width(), h = f.height();
  for (int y = 0; y < h; ++y) {
    std::string row;
    for (int x = 0; x < w; ++x) {
      if (!f.at(x, y).continuation) row += f.glyph(x, y);
    }
    const std::size_t end = row.find_last_not_of(' ');
    out += (end == std::string::npos) ? "" : row.substr(0, end + 1);
    out += '\n';
  }
  return out;
}

std::string render_diff(const Frame* prev, const Frame& next, ColorDepth depth) {
  if (!prev || prev->width() != next.width() || prev->height() != next.height())
    return render_full(next, depth);
  std::string out;
  SgrState current;
  const int w = next.width(), h = next.height();
  for (int y = 0; y < h; ++y) {
    int x = 0;
    while (x < w) {
      if (same(*prev, next, x, y)) { ++x; continue; }
      int start = x;
      if (next.at(start, y).continuation && start > 0) --start;  // rewrite the glyph whole
      int end = x + 1;
      while (end < w && !same(*prev, next, end, y)) ++end;
      if (end < w && next.at(end, y).continuation) ++end;
      if (out.empty()) out += "\x1b[?25l";
      emit_run(out, next, y, start, end, depth, current);
      x = end;
    }
  }
  if (out.empty() && prev->cursor() == next.cursor()) return out;  // nothing to do
  if (out.empty()) out += "\x1b[?25l";
  finish(out, next);
  return out;
}

}  // namespace rolltui
