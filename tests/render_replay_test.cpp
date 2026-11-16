//
// render_replay_test.cpp — WHAT THE RENDERER WRITES LEAVES THE FRAME ON THE SCREEN.
//
// The renderer's bytes are chosen to be few, and few is only right when the screen comes out the frame. Every case here replays a
// full paint and then a diff into `vt_model.hpp`'s terminal and compares the grid with the frame, cell by cell: thousands of random
// frames and random edits to them (text, blanks in runs, wide and spilled glyphs, styles, links, the cursor), then the shapes the
// renderer has a short way for (a long blank run, a run at the right margin, a gap between two changes, a block that moved up).
//
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_render.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to render and read cells back */
#include "rolltui/c/rolltui_screen.h"
#include "rolltui_test.hpp"
#include "vt_model.hpp"

using testkit::check;

namespace {

struct Rng {
  std::uint64_t s;
  explicit Rng(std::uint64_t seed) : s(seed * 2862933555777941757ULL + 3037000493ULL) {}
  unsigned next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<unsigned>(s >> 11);
  }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<unsigned>(n)); }
};

RolltuiStyle style_of(int k) {
  RolltuiStyle s{};
  auto rgb = [](int r, int g, int b) {
    RolltuiStyleColorRaw c{};
    c.kind = RolltuiStyleColorRaw::Kind::Rgb;
    c.r = static_cast<unsigned char>(r), c.g = static_cast<unsigned char>(g), c.b = static_cast<unsigned char>(b);
    return c;
  };
  auto idx = [](int n) {
    RolltuiStyleColorRaw c{};
    c.kind = RolltuiStyleColorRaw::Kind::Indexed;
    c.index = static_cast<unsigned char>(n);
    return c;
  };
  switch (k % 8) {
    case 0: s.fg = rgb(220, 220, 220), s.bg = rgb(24, 24, 28); break;              /* the ground */
    case 1: s.fg = rgb(130, 170, 255), s.bg = rgb(24, 24, 28); break;              /* another ink on it */
    case 2: s.fg = rgb(255, 255, 255), s.bg = rgb(40, 60, 90), s.bold = 1; break;  /* a cursor row */
    case 3: s.fg = rgb(220, 220, 220), s.bg = rgb(24, 24, 28), s.underline = 1; break;
    case 4: s.fg = idx(3), s.bg = idx(4), s.italic = 1; break;
    case 5: s.fg = rgb(220, 220, 220), s.bg = rgb(24, 24, 28), s.reverse = 1; break;
    case 6: s.dim = 1; break; /* the terminal's own colours */
    default: s.fg = idx(9), s.bg = rgb(24, 24, 28), s.bold = 1, s.dim = 1; break;
  }
  return s;
}

const char* const kGlyphs[] = {"a", "b", " ", " ", " ", "x", "\xE6\xBC\xA2" /* 漢, wide */, "e\xCC\x81" /* e + acute */,
                               "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7" /* a family: spilled */,
                               "-", "\xE2\x94\x80" /* ─ */};

void scribble(Rng& r, RolltuiFrame* f, RolltuiDrawScratch* ds, int edits) {
  const int w = rolltui_frame_width(f), h = rolltui_frame_height(f);
  for (int e = 0; e < edits; ++e) {
    const int y = r.below(h), x = r.below(w);
    const RolltuiStyle st = style_of(r.below(8));
    const unsigned link = r.below(10) == 0 ? rolltui_frame_link_id(f, "https://example.com/a", 21) : 0;
    switch (r.below(4)) {
      case 0: { /* a word */
        static const char* const words[] = {"hello", "file10.txt", "  12K", "\xE6\xBC\xA2\xE5\xAD\x97 kanji", "caf\xC3\xA9"};
        const char* wd = words[r.below(5)];
        rolltui_frame_put_text(f, ds, x, y, wd, std::char_traits<char>::length(wd), st, w - x, 0, link);
        break;
      }
      case 1: { /* a run of blanks, long or short */
        const RolltuiRect band{x, y, 1 + r.below(w), 1};
        rolltui_frame_fill(f, ds, band, st, nullptr, 0);
        break;
      }
      case 2: { /* one glyph */
        const char* g = kGlyphs[r.below(static_cast<int>(sizeof kGlyphs / sizeof kGlyphs[0]))];
        rolltui_frame_put_text(f, ds, x, y, g, std::char_traits<char>::length(g), st, w - x, 0, link);
        break;
      }
      default: /* the cursor */
        rolltui_frame_set_cursor(f, x, y, r.below(2));
        break;
    }
  }
}

std::string render(const RolltuiFrame* prev, const RolltuiFrame* next) {
  RolltuiStr s{};
  if (prev) rolltui_render_diff(prev, next, ROLLTUI_DEPTH_TRUECOLOR, &s);
  else rolltui_render_full(next, ROLLTUI_DEPTH_TRUECOLOR, &s);
  std::string out = str_of(s);
  rolltui_str_free(&s);
  return out;
}

// The two frames replayed: the first painted in full, then the diff to the second. Empty when the grid is the second frame.
std::string replay(const RolltuiFrame* a, const RolltuiFrame* b) {
  vt::Screen screen(rolltui_frame_width(a), rolltui_frame_height(a));
  if (!screen.apply(render(nullptr, a))) return "the full paint: " + screen.error();
  std::string why = screen.differs_from(a);
  if (!why.empty()) return "the full paint: " + why;
  if (!screen.apply(render(a, b))) return "the diff: " + screen.error();
  why = screen.differs_from(b);
  return why.empty() ? "" : "the diff: " + why;
}

void test_random() {
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  int failures = 0;
  std::string first;
  for (int seed = 1; seed <= 3000; ++seed) {
    Rng r(static_cast<std::uint64_t>(seed));
    const int w = 1 + r.below(48), h = 1 + r.below(9);
    RolltuiFrame* a = rolltui_frame_new(w, h, style_of(0));
    scribble(r, a, ds, r.below(30));
    RolltuiFrame* b = rolltui_frame_clone(a);
    scribble(r, b, ds, r.below(12));
    const std::string why = replay(a, b);
    if (!why.empty() && failures++ == 0) first = "seed " + std::to_string(seed) + " (" + std::to_string(w) + "x" + std::to_string(h) + "): " + why;
    rolltui_frame_free(a);
    rolltui_frame_free(b);
  }
  check(failures == 0, "3000 random frames and edits: a full paint then a diff leaves each frame on the screen [" +
                           std::to_string(failures) + " wrong; " + first + "]");
  rolltui_draw_scratch_free(ds);
}

// THE SHAPES WITH A SHORT WAY: each built on purpose, replayed, and the bytes compared with the long way's.
void test_shapes() {
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  const RolltuiStyle ground = style_of(0), ink = style_of(1), bar = style_of(2);
  {
    // a page of short lines on a coloured ground, painted in full: the blank rest of each row is not written cell by cell
    RolltuiFrame* f = rolltui_frame_new(120, 20, ground);
    for (int y = 0; y < 20; ++y) rolltui_frame_put_text(f, ds, 0, y, "a short line", 12, y % 2 ? ink : ground, 120, 0, 0);
    vt::Screen s(120, 20);
    const std::string bytes = render(nullptr, f);
    check(s.apply(bytes) && s.differs_from(f).empty(), "a page of short lines on a coloured ground paints right [" + s.error() + "]");
    check(bytes.size() < 20 * 60, "…in far fewer bytes than a cell each: the rest of a row is erased, not written [" +
                                      std::to_string(bytes.size()) + " bytes]");
    rolltui_frame_free(f);
  }
  {
    // two changes on one row with a gap between them, and a change on the next row
    RolltuiFrame* a = rolltui_frame_new(60, 4, ground);
    rolltui_frame_put_text(a, ds, 0, 1, "0123456789abcdefghij", 20, ground, 60, 0, 0);
    RolltuiFrame* b = rolltui_frame_clone(a);
    rolltui_frame_put_text(b, ds, 2, 1, "X", 1, ground, 60, 0, 0);
    rolltui_frame_put_text(b, ds, 5, 1, "Y", 1, ground, 60, 0, 0);
    rolltui_frame_put_text(b, ds, 40, 1, "Z", 1, ink, 60, 0, 0);
    rolltui_frame_put_text(b, ds, 0, 2, "W", 1, ground, 60, 0, 0);
    const std::string why = replay(a, b);
    const std::string diff = render(a, b);
    check(why.empty(), "changes with gaps between them on a row, and on the next, replay right [" + why + "]");
    std::string shown;
    for (char ch : diff) shown += ch == '\x1b' ? std::string("ESC") : ch == '\r' ? std::string("\\r") : ch == '\n' ? std::string("\\n") : std::string(1, ch);
    const auto count = [&](const std::string& needle) {
      std::size_t n = 0;
      for (std::size_t at = diff.find(needle); at != std::string::npos; at = diff.find(needle, at + 1)) ++n;
      return n;
    };
    check(diff.find("X34Y") != std::string::npos && diff.find("\x1b[34C") != std::string::npos && diff.find("\r\n") != std::string::npos &&
              count("H") == 2,
          "…the short way: a two-cell gap written over, a long one stepped across, the next row reached by CR LF, a CUP only to "
          "begin and for the cursor [" + shown + "]");
    rolltui_frame_free(a);
    rolltui_frame_free(b);
  }
  {
    // the last column written, then a change on the next row: the pending wrap must not be stepped on
    RolltuiFrame* a = rolltui_frame_new(10, 3, ground);
    RolltuiFrame* b = rolltui_frame_clone(a);
    rolltui_frame_put_text(b, ds, 9, 0, "E", 1, ink, 1, 0, 0);
    rolltui_frame_put_text(b, ds, 0, 1, "S", 1, ink, 1, 0, 0);
    rolltui_frame_put_text(b, ds, 9, 2, "\xE6\xBC\xA2", 3, ink, 1, 0, 0); /* a wide glyph that does not fit the last column */
    const std::string why = replay(a, b);
    check(why.empty(), "a change in the last column and one at the next row's start replay right [" + why + "]");
    rolltui_frame_free(a);
    rolltui_frame_free(b);
  }
  {
    // a list that moved up one row: every row changes, and the bytes are a scroll and a row, not a screenful
    RolltuiFrame* a = rolltui_frame_new(80, 24, ground);
    RolltuiFrame* b = rolltui_frame_new(80, 24, ground);
    for (int y = 0; y < 24; ++y) {
      std::string line = "line " + std::to_string(y * 7919 % 1000) + " of a log that is scrolling, with something different on it";
      rolltui_frame_put_text(a, ds, 0, y, line.data(), line.size(), y % 3 ? ground : ink, 80, 0, 0);
      if (y > 0) rolltui_frame_put_text(b, ds, 0, y - 1, line.data(), line.size(), y % 3 ? ground : ink, 80, 0, 0);
    }
    rolltui_frame_put_text(b, ds, 0, 23, "the newest line", 15, bar, 80, 0, 0);
    const std::string why = replay(a, b);
    const std::string diff = render(a, b);
    check(why.empty(), "a screen whose rows moved up one replays right [" + why + "]");
    check(diff.size() < 300, "…in a scroll and the new row, not every row again [" + std::to_string(diff.size()) + " bytes]");
    // and down, as a list scrolled back
    const std::string back_why = replay(b, a);
    check(back_why.empty() && render(b, a).size() < 400, "…and moved down, the same [" + back_why + "]");
    rolltui_frame_free(a);
    rolltui_frame_free(b);
  }
  rolltui_draw_scratch_free(ds);
}

}  // namespace

int main() {
  test_random();
  test_shapes();
  return testkit::report("render_replay_test");
}
