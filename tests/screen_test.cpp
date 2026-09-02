//
// screen_test.cpp — Frame semantics (wide glyphs, continuation cells, clipping, the
// right edge) and golden byte strings for render_full / render_diff.
//
#include <string>

#include "rolltui/Screen.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {
std::string visible(const std::string& s) {
  std::string o;
  for (char c : s) o += (c == '\x1b') ? std::string("ESC") : std::string(1, c);
  return o;
}
void expect_bytes(const std::string& name, const std::string& got, const std::string& want) {
  check(got == want, name + "\n         want " + visible(want) + "\n         got  " + visible(got));
}
}  // namespace

int main() {
  const ColorDepth depth = ColorDepth::TrueColor;
  Style bold;
  bold.bold = true;
  Style red;
  red.fg = Color::indexed(1);

  // ---- Frame semantics -----------------------------------------------------------
  {
    Frame f(4, 2);
    check(f.width() == 4 && f.height() == 2 && f.at(3, 1).text == " ", "a fresh frame is spaces");
    check(f.put(1, 0, "a", 1, bold) == 1 && f.at(1, 0).text == "a" && f.at(1, 0).style == bold, "put one cell");
    check(f.put(2, 0, "\xE4\xB8\xAD", 2, red) == 2 && f.at(2, 0).width == 2 && f.at(3, 0).continuation &&
              f.at(3, 0).text.empty(),
          "a wide glyph occupies its cell and a continuation cell");
    check(f.put(3, 1, "\xE4\xB8\xAD", 2, red) == 1 && f.at(3, 1).text == " " && f.at(3, 1).width == 1,
          "a wide glyph at the right edge becomes a space (never straddles)");
    check(f.put(4, 0, "x", 1, bold) == 0 && f.put(0, 2, "x", 1, bold) == 0 && f.put(-1, 0, "x", 1, bold) == 0,
          "puts outside the frame are ignored");
    // Overwrite half of the wide glyph: the other half is blanked, no orphan.
    f.put(3, 0, "z", 1, bold);
    check(f.at(2, 0).text == " " && f.at(2, 0).width == 1 && f.at(3, 0).text == "z", "overwriting a continuation cell blanks the glyph");
    f.put(2, 0, "\xE4\xB8\xAD", 2, red);
    f.put(2, 0, "y", 1, bold);
    check(f.at(3, 0).text == " " && !f.at(3, 0).continuation, "overwriting a wide glyph's first cell blanks its continuation");
  }
  {
    Frame f(6, 1);
    int used = f.put_text(1, 0, "a\xE4\xB8\xAD" "bcdef", bold, 10);
    check(used == 5 && f.at(1, 0).text == "a" && f.at(2, 0).width == 2 && f.at(4, 0).text == "b" &&
              f.at(5, 0).text == "c",
          "put_text lays graphemes left to right and clips at the frame edge (used " + std::to_string(used) + ")");
    Frame g(6, 1);
    check(g.put_text(0, 0, "abcdef", bold, 3) == 3 && g.at(3, 0).text == " ", "put_text honours max_cells");
    Frame h(3, 1);
    check(h.put_text(0, 0, "ab\xE4\xB8\xAD", bold, 10) == 2 && h.at(2, 0).text == " ",
          "put_text stops before a wide glyph that would straddle the edge");
    Frame k(4, 1);
    check(k.put_text(0, 0, "a\x01" "b\xCC\x81" "c", bold, 10) == 3 && k.at(1, 0).text == "b\xCC\x81",
          "controls skipped, combining marks stay with their base");
    Frame m(4, 2);
    m.fill({1, 0, 10, 10}, red, "#");
    check(m.at(0, 0).text == " " && m.at(1, 0).text == "#" && m.at(3, 1).text == "#" && m.at(3, 1).style == red,
          "fill clips to the frame");
  }
  {
    Rect a{0, 0, 10, 5}, b{5, 2, 10, 10};
    Rect i = a.intersect(b);
    check(i == Rect{5, 2, 5, 3}, "Rect::intersect");
    check(a.intersect(Rect{20, 20, 1, 1}).empty(), "disjoint rects intersect to empty");
    check(a.contains(9, 4) && !a.contains(10, 4), "Rect::contains is half-open");
  }

  // ---- golden bytes --------------------------------------------------------------
  {
    Frame blank(4, 2);
    expect_bytes("full repaint of a blank 4x2 frame", render_full(blank, depth),
                 "\x1b[?25l\x1b[H\x1b[2J"
                 "\x1b[1;1H\x1b[0m    "
                 "\x1b[2;1H    "
                 "\x1b[0m\x1b[1;1H");
    expect_bytes("diff with no prev is a full repaint", render_diff(nullptr, blank, depth), render_full(blank, depth));
    expect_bytes("diff of identical frames is empty", render_diff(&blank, blank, depth), "");

    Frame next = blank;
    next.put(1, 0, "a", 1, bold);
    next.put(2, 0, "b", 1, bold);
    expect_bytes("diff addresses the changed run absolutely and emits one SGR", render_diff(&blank, next, depth),
                 "\x1b[?25l\x1b[1;2H\x1b[0;1mab\x1b[0m\x1b[1;1H");

    Frame wide = blank;
    wide.put(2, 0, "\xE4\xB8\xAD", 2, red);
    expect_bytes("a wide glyph is written once, its continuation cell skipped", render_diff(&blank, wide, depth),
                 "\x1b[?25l\x1b[1;3H\x1b[0;31m\xE4\xB8\xAD\x1b[0m\x1b[1;1H");
    Frame wide2 = wide;
    wide2.put(3, 0, "z", 1, red);  // blanks the glyph: cells 2 and 3 both change
    expect_bytes("a run that begins on the continuation cell starts one cell earlier", render_diff(&wide, wide2, depth),
                 "\x1b[?25l\x1b[1;3H\x1b[0;31m z\x1b[0m\x1b[1;1H");

    Frame cur = blank;
    cur.set_cursor(3, 1, true);
    expect_bytes("cursor move only", render_diff(&blank, cur, depth), "\x1b[?25l\x1b[0m\x1b[2;4H\x1b[?25h");

    Frame two = blank;
    two.put(0, 0, "x", 1, bold);
    two.put(3, 1, "y", 1, red);
    expect_bytes("two runs on two rows", render_diff(&blank, two, depth),
                 "\x1b[?25l\x1b[1;1H\x1b[0;1mx\x1b[2;4H\x1b[0;31my\x1b[0m\x1b[1;1H");

    Frame resized(5, 2);
    expect_bytes("a size change forces a full repaint", render_diff(&blank, resized, depth), render_full(resized, depth));

    Frame same_style = blank;
    same_style.put(0, 0, "p", 1, bold);
    same_style.put(1, 0, "q", 1, bold);
    same_style.put(2, 0, "r", 1, red);
    expect_bytes("SGR only when the style changes inside a run", render_diff(&blank, same_style, depth),
                 "\x1b[?25l\x1b[1;1H\x1b[0;1mpq\x1b[0;31mr\x1b[0m\x1b[1;1H");

    Frame mono = blank;
    mono.put(0, 0, "m", 1, red);
    expect_bytes("depth downgrade flows into the emitted SGR", render_diff(&blank, mono, ColorDepth::Mono),
                 "\x1b[?25l\x1b[1;1H\x1b[0mm\x1b[0m\x1b[1;1H");
  }
  return report("rolltui screen_test");
}
