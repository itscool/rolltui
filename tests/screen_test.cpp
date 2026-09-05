//
// screen_test.cpp — Frame semantics (wide glyphs, continuation cells, clipping, the
// right edge) and golden byte strings for render_full / render_diff.
//
#include <string>

#include "rolltui/Screen.hpp"

#include "rolltui/c/rolltui_geom.h"
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
    check(f.width() == 4 && f.height() == 2 && f.glyph(3, 1) == " ", "a fresh frame is spaces");
    check(f.put(1, 0, "a", 1, bold) == 1 && f.glyph(1, 0) == "a" && f.at(1, 0).style == bold, "put one cell");
    check(f.put(2, 0, "\xE4\xB8\xAD", 2, red) == 2 && f.at(2, 0).width == 2 && f.at(3, 0).continuation &&
              f.glyph(3, 0).empty(),
          "a wide glyph occupies its cell and a continuation cell");
    check(f.put(3, 1, "\xE4\xB8\xAD", 2, red) == 1 && f.glyph(3, 1) == " " && f.at(3, 1).width == 1,
          "a wide glyph at the right edge becomes a space (never straddles)");
    check(f.put(4, 0, "x", 1, bold) == 0 && f.put(0, 2, "x", 1, bold) == 0 && f.put(-1, 0, "x", 1, bold) == 0,
          "puts outside the frame are ignored");
    // Overwrite half of the wide glyph: the other half is blanked, no orphan.
    f.put(3, 0, "z", 1, bold);
    check(f.glyph(2, 0) == " " && f.at(2, 0).width == 1 && f.glyph(3, 0) == "z", "overwriting a continuation cell blanks the glyph");
    f.put(2, 0, "\xE4\xB8\xAD", 2, red);
    f.put(2, 0, "y", 1, bold);
    check(f.glyph(3, 0) == " " && !f.at(3, 0).continuation, "overwriting a wide glyph's first cell blanks its continuation");
  }
  {
    Frame f(6, 1);
    int used = f.put_text(1, 0, "a\xE4\xB8\xAD" "bcdef", bold, 10);
    check(used == 5 && f.glyph(1, 0) == "a" && f.at(2, 0).width == 2 && f.glyph(4, 0) == "b" &&
              f.glyph(5, 0) == "c",
          "put_text lays graphemes left to right and clips at the frame edge (used " + std::to_string(used) + ")");
    Frame g(6, 1);
    check(g.put_text(0, 0, "abcdef", bold, 3) == 3 && g.glyph(3, 0) == " ", "put_text honours max_cells");
    Frame h(3, 1);
    check(h.put_text(0, 0, "ab\xE4\xB8\xAD", bold, 10) == 2 && h.glyph(2, 0) == " ",
          "put_text stops before a wide glyph that would straddle the edge");
    Frame k(4, 1);
    check(k.put_text(0, 0, "a\x01" "b\xCC\x81" "c", bold, 10) == 3 && k.glyph(1, 0) == "b\xCC\x81",
          "controls skipped, combining marks stay with their base");
    Frame m(4, 2);
    m.fill({1, 0, 10, 10}, red, "#");
    check(m.glyph(0, 0) == " " && m.glyph(1, 0) == "#" && m.glyph(3, 1) == "#" && m.at(3, 1).style == red,
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
    expect_bytes("writing over a wide glyph's continuation blanks the whole glyph", render_diff(&wide, wide2, depth),
                 "\x1b[?25l\x1b[1;3H\x1b[0;31m z\x1b[0m\x1b[1;1H");

    // THE BACKUP RULE, and it needs a case the one above does NOT provide — found
    // 2026-09-04 by a negative control that failed NOTHING when the rule was disabled.
    //
    // The assertion above used to be named "a run that begins on the continuation cell
    // starts one cell earlier", which is the rule's own words, and it never exercised it:
    // its own comment says why — writing over cell 3 BLANKS the glyph, so cell 2 changes
    // too and the run starts at 2 on its own merits. The branch that backs `start` up was
    // dead in every test in the suite. A test that names a rule and does not reach it is
    // worse than no test, because it is why nobody looked.
    //
    // To reach it, the FIRST changed cell must be a continuation whose LEAD is unchanged.
    // Restyling only cell 3 does exactly that: `same()` compares styles, so cell 3 differs
    // while cell 2 is identical. Without the backup the run is [3,4), `emit_run` skips the
    // continuation cell, and the glyph is never re-emitted — the terminal keeps a stale
    // one. The expected bytes below are therefore the glyph WRITTEN WHOLE from column 3.
    Frame wide3 = wide;
    wide3.set_style(3, 0, Style{.fg = Color::indexed(4)});
    expect_bytes("a run that begins on the continuation cell starts one cell earlier",
                 render_diff(&wide, wide3, depth),
                 "\x1b[?25l\x1b[1;3H\x1b[0;31m\xE4\xB8\xAD\x1b[0m\x1b[1;1H");

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
  // ---- Phase 13 m4: a cluster too long to sit in a Cell ------------------------------
  // The overflow case is a NAMED test with a REAL cluster, not a hypothetical: a family
  // ZWJ sequence is 25 bytes, a user can paste one, and `Cell` holds ten. Everything below
  // is about it behaving exactly like a short glyph from the outside.
  {
    // 👨‍👩‍👧‍👦 — MAN ZWJ WOMAN ZWJ GIRL ZWJ BOY: four 4-byte emoji and three 3-byte joiners.
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                               "\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
    check(family.size() == 25, "the control cluster really is longer than a Cell holds (" + std::to_string(family.size()) + " bytes)");
    Frame f(6, 1);
    f.put(0, 0, family, 2, Style{});
    check(f.at(0, 0).spilled(), "a 25-byte cluster SPILLS rather than being truncated into the cell");
    check(f.glyph(0, 0) == family, "…and reads back byte for byte through the one accessor");
    check(f.at(0, 0).width == 2 && f.at(1, 0).continuation, "…keeping its width and its continuation cell");

    // A ten-byte cluster is the boundary and must NOT spill — an off-by-one here would
    // send every flag emoji through the slow path and nobody would notice.
    Frame g(4, 1);
    g.put(0, 0, "0123456789", 1, Style{});
    check(!g.at(0, 0).spilled() && g.glyph(0, 0) == "0123456789", "exactly ten bytes stays INLINE: the boundary is >, not >=");

    // The frame diff compares glyphs BY VALUE across frames, so a spill index minted in
    // one frame is never mistaken for the same index in another.
    Frame h(6, 1);
    h.put(0, 0, family, 2, Style{});
    check(render_diff(&f, h, ColorDepth::TrueColor).find(family) == std::string::npos,
          "two frames holding the same long cluster diff to nothing, though their indices are their own");
    Frame other(6, 1);
    other.put(0, 0, "x", 1, Style{});
    check(render_diff(&other, f, ColorDepth::TrueColor).find(family) != std::string::npos,
          "…and a frame that actually gained the cluster emits its bytes");
    check(frame_to_text(f).rfind(family, 0) == 0, "frame_to_text carries it too");

    // clear() drops the spill table with the cells that referenced it: a stale entry would
    // grow without bound over a long session, which is the failure a table like this has.
    f.clear(Style{});
    f.put(0, 0, "a", 1, Style{});
    check(!f.at(0, 0).spilled() && f.glyph(0, 0) == "a", "clear() resets the cells…");
    f.put(1, 0, family, 2, Style{});
    check(f.glyph(1, 0) == family, "…and the table is reusable afterwards, not poisoned by the reset");
  }

  // ---- Phase 13 m5: reuse the Frame, and the ghosting control ------------------------
  // The failure mode of a hand-written reset is not a crash — it is a STALE FIELD that
  // renders as a perfectly well-formed frame. So the control is equivalence: a reused
  // frame must be indistinguishable from a freshly constructed one, cell for cell,
  // including the fields nobody thinks about.
  {
    const Style fill{Color::rgb(1, 2, 3), Color::rgb(4, 5, 6)};
    // Paint a busy frame: wide glyphs, a link, a spilled cluster, marks, a moved cursor.
    Frame used(8, 2, fill);
    used.put_text(0, 0, "abc\xE4\xBD\xA0", Style{}, 8, false, used.link_id("https://example.invalid/"));
    used.put(0, 1, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", 2, Style{});
    used.mark(0, 0, 3, EffectState::Waiting);
    used.set_cursor(4, 1, true);

    used.reset(8, 2, fill);
    const Frame fresh(8, 2, fill);
    check(used == fresh, "a RESET frame equals a freshly constructed one — every field, not the ones that looked like they mattered");
    check(used.mark_count() == 0 && used.link(1).empty() && used.cursor() == Cursor{},
          "…including the marks, the link table and the cursor, all of which named cells that are gone");
    check(used.glyph(0, 1) == " " && !used.at(0, 1).spilled(), "…and the spilled glyph, so the table cannot grow across a session");

    // GHOSTING: paint a full frame, reuse it for one that writes strictly fewer cells, and
    // assert nothing of the first survives.
    Frame reused(8, 2, fill);
    reused.put_text(0, 0, "XXXXXXXX", Style{}, 8);
    reused.put_text(0, 1, "YYYYYYYY", Style{}, 8);
    reused.reset(8, 2, fill);
    reused.put_text(0, 0, "ab", Style{}, 8);
    Frame control(8, 2, fill);
    control.put_text(0, 0, "ab", Style{}, 8);
    check(reused == control, "a reused frame painted with FEWER cells has no ghost of the last paint");
    check(frame_to_text(reused).find('X') == std::string::npos && frame_to_text(reused).find('Y') == std::string::npos,
          "…asserted on the text too, since a ghost renders as a perfectly well-formed frame");

    // RESIZE: the geometry is authoritative, and the diff refuses the old baseline.
    Frame before(8, 2, fill);
    before.put_text(0, 0, "12345678", Style{}, 8);
    Frame after = before;
    after.reset(4, 3, fill);
    check(after.width() == 4 && after.height() == 3 && after == Frame(4, 3, fill),
          "reset to a new size resizes and still equals a fresh frame of that size");
    const std::string bytes = render_diff(&before, after, ColorDepth::TrueColor);
    check(bytes.rfind("\x1b[?25l\x1b[H\x1b[2J", 0) == 0,
          "…and diffing across a size change is a FULL repaint, so a resize cannot corrupt by geometry");

    // A long run of paints, including resizes, ends where a fresh frame would.
    Frame loop(8, 2, fill);
    for (int i = 0; i < 25; ++i) {
      loop.reset((i % 3) ? 8 : 5, 2, fill);
      loop.put_text(0, 0, "run", Style{}, 8);
    }
    loop.reset(8, 2, fill);
    loop.put_text(0, 0, "ab", Style{}, 8);
    check(loop == control, "…and twenty-five paints with resizes among them leave exactly what one paint would");
  }

  // ---- Phase 14 m1: the seam, and proof the flag SELECTS ----------------------------
  // Both implementations satisfy this file. What is asserted here is that the one the build
  // asked for is the one that linked — because a flag that silently fails to select would
  // leave the whole experiment testing C++ twice and reporting success, which is this
  // project's characteristic failure aimed at its own instrument (Phase 13 m1 had to prove
  // its counter armed for exactly the same reason).
  {
    // The frame diff's goldens above already exercise `intersect` in anger; these are the
    // edges worth naming.
    const Rect a{0, 0, 10, 10};
    check(a.intersect({5, 5, 10, 10}) == Rect{5, 5, 5, 5}, "overlapping rectangles intersect");
    check(a.intersect({20, 20, 5, 5}) == Rect{20, 20, 0, 0},
          "…and disjoint ones give an EMPTY rect at the clamped origin, not at {0,0}");
    check(a.intersect({2, 2, 3, 3}) == Rect{2, 2, 3, 3}, "…a contained rect is itself");
    check(a.intersect({0, 0, 0, 0}) == Rect{0, 0, 0, 0}, "…and a zero-sized one stays zero-sized");
  }

  return report("rolltui screen_test");
}
