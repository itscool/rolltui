// rolltui/Screen.cpp — see Screen.hpp.
#include "rolltui/Screen.hpp"

#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_render.h"

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

// THE THREE CONSUMERS NOW FORWARD TO C (`rolltui/c/rolltui_render.h`). `rolltui_screen.h`
// deferred them on purpose — "porting them is its own step and moves no behaviour when it
// happens" — and this is that step. What is left here is the std::string shape a C++ caller
// still writes against; the loops, the SGR state and the run-finding are all in the C.
//
// HOW THE PORT WAS VERIFIED, because "moves no behaviour" is a claim and not a hope: the
// golden-frame suites record the BYTES these produce, and they were recorded from the C++
// implementation. Forwarding to the C and keeping 61+ goldens green is a byte-for-byte
// equivalence check against every one of them, which is a stronger control than any
// differential test written for the occasion.
namespace {
// The library's own growing buffer, lent to the C and copied out once. A caller that wants
// the allocation gone entirely uses `rolltui_render_diff` directly with a buffer it keeps —
// which is what `rolltui_swap.h` exists to make the normal thing.
std::string take(RolltuiStr& s) {
  std::size_t len = 0;
  const char* p = rolltui_str_get(&s, &len);
  std::string out(p ? p : "", len);
  rolltui_str_free(&s);
  return out;
}
}  // namespace

std::string render_full(const Frame& next, ColorDepth depth) {
  RolltuiStr s{};
  rolltui_render_full(next.handle(), static_cast<unsigned char>(depth), &s);
  return take(s);
}

std::string frame_to_text(const Frame& f) {
  RolltuiStr s{};
  rolltui_frame_to_text(f.handle(), &s);
  return take(s);
}

std::string render_diff(const Frame* prev, const Frame& next, ColorDepth depth) {
  RolltuiStr s{};
  rolltui_render_diff(prev ? prev->handle() : nullptr, next.handle(), static_cast<unsigned char>(depth), &s);
  return take(s);
}

}  // namespace rolltui
