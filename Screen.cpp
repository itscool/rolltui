// rolltui/Screen.cpp — see Screen.hpp.
#include "rolltui/Screen.hpp"

#include <algorithm>
#include <cstring>

#include "rolltui/c/rolltui_geom.h"

#include "rolltui/Scratch.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

// Phase 14 m1: through the seam. Whichever implementation is linked answers this, and the
// whole suite is the oracle for both.
Rect Rect::intersect(const Rect& o) const {
  int r[4];
  rolltui_rect_intersect(x, y, w, h, o.x, o.y, o.w, o.h, r);
  return {r[0], r[1], r[2], r[3]};
}

Frame::Frame(int w, int h, const Style& fill) : w_(std::max(w, 0)), h_(std::max(h, 0)) {
  Cell c;
  c.style = fill;
  cells_.assign(static_cast<std::size_t>(w_ * h_), c);
}

void Frame::reset(int w, int h, const Style& fill) {
  w_ = std::max(w, 0);
  h_ = std::max(h, 0);
  Cell c;
  c.style = fill;
  cells_.assign(static_cast<std::size_t>(w_ * h_), c);  // keeps capacity, resets every field
  link_count_ = 0;         // the strings stay; the next frame assigns into them
  long_glyph_count_ = 0;
  marks_.clear();
  cursor_ = Cursor{};
}

void Frame::clear(const Style& fill) {
  Cell c;
  c.style = fill;
  std::fill(cells_.begin(), cells_.end(), c);
  marks_.clear();          // a mark names cells that have just been erased
  long_glyph_count_ = 0;   // …and so does every spilled glyph: nothing refers to them now
}

void Frame::set_style(int x, int y, const Style& style) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  mut(x, y).style = style;
}

void Frame::mark(int x, int y, int cells, EffectState state, std::uint64_t since_ms, double fraction) {
  if (cells <= 0 || state == EffectState::None) return;
  if (y < 0 || y >= h_ || x >= w_) return;
  marks_.push_back({x, y, cells, state, since_ms, fraction});
}

// Writes a cluster into a cell, inline when it fits and into the frame's spill table when
// it does not. The unused inline bytes are ZEROED, so `Cell::operator==` (which the frame
// diff leans on) compares cells and not the garbage behind a shorter glyph.
void Frame::set_glyph(Cell& c, std::string_view g) {
  std::memset(c.bytes, 0, sizeof c.bytes);
  if (g.size() <= Cell::kInlineGlyph) {
    std::memcpy(c.bytes, g.data(), g.size());
    c.len = static_cast<std::uint8_t>(g.size());
    return;
  }
  // THE NAMED EXCEPTION (Phase 13's phase Done-when): a cluster longer than ten bytes
  // allocates, once, into a table this frame owns. A family ZWJ emoji is the real case.
  if (long_glyph_count_ == long_glyphs_.size()) long_glyphs_.emplace_back();
  long_glyphs_[long_glyph_count_].assign(g);
  const std::uint32_t idx = static_cast<std::uint32_t>(long_glyph_count_++);
  std::memcpy(c.bytes, &idx, sizeof idx);
  c.len = Cell::kSpilled;
}

std::string_view Frame::glyph_of(const Cell& c) const {
  if (!c.spilled()) return c.inline_bytes();
  std::uint32_t idx = 0;
  std::memcpy(&idx, c.bytes, sizeof idx);
  return idx < long_glyph_count_ ? std::string_view(long_glyphs_[idx]) : std::string_view();
}

std::string_view Frame::glyph(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return {};
  return glyph_of(at(x, y));
}

std::uint32_t Frame::link_id(std::string_view url) {
  if (url.empty()) return 0;
  for (std::size_t i = 0; i < link_count_; ++i)
    if (links_[i] == url) return static_cast<std::uint32_t>(i + 1);
  if (link_count_ == links_.size()) links_.emplace_back();
  links_[link_count_].assign(url);  // reuses the buffer a past frame left here
  return static_cast<std::uint32_t>(++link_count_);
}

std::string_view Frame::link(std::uint32_t id) const {
  if (id == 0 || id > link_count_) return {};
  return links_[id - 1];
}

int Frame::put(int x, int y, std::string_view grapheme, int cells, const Style& style, std::uint32_t link) {
  if (y < 0 || y >= h_ || x < 0 || x >= w_ || cells <= 0) return 0;
  if (cells > 2) cells = 2;
  // Overwriting half of an existing wide glyph: blank the other half so no orphan
  // continuation cell survives.
  auto blank = [&](int cx) {
    Cell& c = mut(cx, y);
    set_glyph(c, " ");
    c.width = 1;
    c.continuation = false;
    c.link = 0;
  };
  if (mut(x, y).continuation && x > 0) blank(x - 1);
  if (mut(x, y).width == 2 && x + 1 < w_) blank(x + 1);
  if (cells == 2 && x + 1 >= w_) {  // would straddle the right edge
    Cell& c = mut(x, y);
    set_glyph(c, " ");
    c.width = 1;
    c.continuation = false;
    c.style = style;
    c.link = link;
    return 1;
  }
  if (cells == 2) {
    if (mut(x + 1, y).width == 2 && x + 2 < w_) blank(x + 2);
    Cell& r = mut(x + 1, y);
    set_glyph(r, {});
    r.width = 0;
    r.continuation = true;
    r.style = style;
    r.link = link;
  }
  Cell& c = mut(x, y);
  set_glyph(c, grapheme);
  c.width = static_cast<std::uint8_t>(cells);
  c.continuation = false;
  c.style = style;
  c.link = link;
  return cells;
}

int Frame::put_text(int x, int y, std::string_view utf8, const Style& style, int max_cells,
                    bool ambiguous_wide, std::uint32_t link) {
  if (y < 0 || y >= h_) return 0;
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
    if (used + g.width > max_cells || x + used >= w_) break;
    if (g.width == 2 && x + used + 1 >= w_) break;  // never a half glyph at the edge
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
      Style& s = mut(xx, yy).style;
      if (style.fg.kind != Color::Kind::None) s.fg = style.fg;
      if (style.bg.kind != Color::Kind::None) s.bg = style.bg;
      s.bold |= style.bold;
      s.italic |= style.italic;
      s.underline |= style.underline;
      s.dim |= style.dim;
      s.reverse |= style.reverse;
    }
}

namespace {

std::string cup(int x, int y) {
  return "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
}

// Emit cells [x0, x1) of row y, tracking the SGR state across calls. A hyperlink is
// opened when a run enters linked cells and always closed before the run ends.
void emit_run(std::string& out, const Frame& f, int y, int x0, int x1, ColorDepth depth,
              const Style*& current) {
  out += cup(x0, y);
  std::uint32_t link = 0;
  for (int x = x0; x < x1; ++x) {
    const Cell& c = f.at(x, y);
    if (c.continuation) continue;
    if (c.link != link) {  // before the SGR, so a link closes right after its last glyph
      out += "\x1b]8;;";
      out += f.link(c.link);
      out += "\x1b\\";
      link = c.link;
    }
    if (!current || !(*current == c.style)) {
      out += sgr(c.style, depth);
      current = &c.style;
    }
    out += f.glyph_of(c);
  }
  if (link != 0) out += "\x1b]8;;\x1b\\";
}

// Two cells look the same on screen: everything equal, links compared by URL (the ids
// are per frame).
bool same(const Frame& a, const Frame& b, int x, int y) {
  const Cell& p = a.at(x, y);
  const Cell& q = b.at(x, y);
  // Glyphs and links are both compared BY VALUE across the two frames, never by their
  // per-frame index: a spill index and a link id mean nothing outside the frame that
  // minted them (Screen.hpp).
  return a.glyph_of(p) == b.glyph_of(q) && p.width == q.width && p.continuation == q.continuation &&
         p.style == q.style && a.link(p.link) == b.link(q.link);
}

void finish(std::string& out, const Frame& f) {
  out += "\x1b[0m";
  out += cup(f.cursor().x, f.cursor().y);
  if (f.cursor().visible) out += "\x1b[?25h";
}

}  // namespace

std::string render_full(const Frame& next, ColorDepth depth) {
  std::string out = "\x1b[?25l\x1b[H\x1b[2J";
  const Style* current = nullptr;
  for (int y = 0; y < next.height(); ++y) emit_run(out, next, y, 0, next.width(), depth, current);
  finish(out, next);
  return out;
}

std::string frame_to_text(const Frame& f) {
  std::string out;
  for (int y = 0; y < f.height(); ++y) {
    std::string row;
    for (int x = 0; x < f.width(); ++x) {
      const Cell& c = f.at(x, y);
      if (!c.continuation) row += f.glyph_of(c);
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
  const Style* current = nullptr;
  for (int y = 0; y < next.height(); ++y) {
    int x = 0;
    while (x < next.width()) {
      if (same(*prev, next, x, y)) { ++x; continue; }
      int start = x;
      if (next.at(start, y).continuation && start > 0) --start;  // rewrite the glyph whole
      int end = x + 1;
      while (end < next.width() && !same(*prev, next, end, y)) ++end;
      if (end < next.width() && next.at(end, y).continuation) ++end;
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
