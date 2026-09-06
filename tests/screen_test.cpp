//
// screen_test.cpp — Frame semantics (wide glyphs, continuation cells, clipping, the
// right edge) and golden byte strings for render_full / render_diff.
//
// PHASE 17: calls the C API (rolltui/c/rolltui_screen.h, rolltui_frame_ops.h,
// rolltui_render.h, all reached through rolltui/rolltui.h) directly rather than through
// the rolltui::Frame C++ RAII shim (Screen.hpp, and its shim Screen.cpp) that this file
// used to include — those are the files being deleted. `RolltuiRect`/`RolltuiCell`/
// `RolltuiStyle`/`RolltuiStyleColor` are the same one-definition structs `Rect`/`Cell`/
// `Style`/`Color` used to alias, so this file names them directly instead of pulling in
// Screen.hpp's aliases. `ColorDepth` DOES have a C spelling (`ROLLTUI_DEPTH_*`,
// rolltui_theme.h) so this file needs no C++ Theme header for it either. The one thing
// with no C vocabulary at all is a mark's STATE (rolltui_screen.h: the frame stores it as
// an opaque int and never interprets it; the enum stays in Effects.hpp, C++-only, on
// purpose) — this file does not test motion, so the one value it needs is named locally.
//
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "rolltui/rolltui.h"

#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_render.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_screen.h"  // INTERNAL: this test opts in

using namespace rolltui_test;

namespace {

// ---- the frame: OWNED, an explicit new/free pair (CLAUDE.md's "owned handles get
// explicit _new/_free pairs"), through a unique_ptr so an early return still frees it —
// the same RAII shape rolltui::Frame gave a caller, now built by the caller instead. ----
using FramePtr = std::unique_ptr<RolltuiFrame, void (*)(RolltuiFrame*)>;
FramePtr new_frame(int w, int h, RolltuiStyle fill = {}) {
  return FramePtr(rolltui_frame_new(w, h, fill), rolltui_frame_free);
}
FramePtr clone_frame(const RolltuiFrame* src) { return FramePtr(rolltui_frame_clone(src), rolltui_frame_free); }

// The one draw scratch this test binary needs (Screen.cpp kept one per thread via
// ThreadHandle; this binary is single-threaded and never frees it, the same convention
// tests/input_test.cpp already uses for its own draw_scratch()).
RolltuiDrawScratch* draw_scratch() {
  static std::unique_ptr<RolltuiDrawScratch, void (*)(RolltuiDrawScratch*)> s(rolltui_draw_scratch_new(),
                                                                              rolltui_draw_scratch_free);
  return s.get();
}

// A mark's STATE has no C vocabulary at all (rolltui::EffectState stays C++-only,
// Effects.hpp) — this file only needs SOME state to prove a reset/clear drops a mark, so
// the one value used is named here rather than pulled in through Theme.hpp/Effects.hpp.
constexpr int kWaitingState = 1;  // rolltui::EffectState::Waiting

int put(RolltuiFrame* f, int x, int y, std::string_view g, int cells, RolltuiStyle s, unsigned int link = 0) {
  return rolltui_frame_put(f, x, y, g.data(), g.size(), cells, s, link);
}
int put_text(RolltuiFrame* f, int x, int y, std::string_view utf8, RolltuiStyle style, int max_cells,
             int ambiguous_wide = 0, unsigned int link = 0) {
  return rolltui_frame_put_text(f, draw_scratch(), x, y, utf8.data(), utf8.size(), style, max_cells, ambiguous_wide,
                                link);
}
void fill(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style, std::string_view g = " ") {
  rolltui_frame_fill(f, draw_scratch(), r, style, g.data(), g.size());
}
RolltuiCell cell_at(const RolltuiFrame* f, int x, int y) {
  RolltuiCell c{};
  rolltui_frame_cell(f, x, y, &c);
  return c;
}
std::string_view glyph_at(const RolltuiFrame* f, int x, int y) {
  std::size_t n = 0;
  const char* p = rolltui_frame_glyph(f, x, y, &n);
  return std::string_view(p, n);
}
std::string_view link_at(const RolltuiFrame* f, unsigned int id) {
  std::size_t n = 0;
  const char* p = rolltui_frame_link(f, id, &n);
  return std::string_view(p, n);
}
// The cursor: no C vocabulary either (three out-params, never a named struct), so this
// mirrors what rolltui::Cursor was — pure data, assembled here exactly as Screen.hpp did.
struct Cursor {
  int x = 0, y = 0;
  bool visible = false;
  bool operator==(const Cursor&) const = default;
};
Cursor cursor_of(const RolltuiFrame* f) {
  int x = 0, y = 0, visible = 0;
  rolltui_frame_cursor(f, &x, &y, &visible);
  return {x, y, visible != 0};
}
bool frame_eq(const RolltuiFrame* a, const RolltuiFrame* b) { return rolltui_frame_equal(a, b) != 0; }

std::string frame_to_text(const RolltuiFrame* f) {
  RolltuiStr s;
  rolltui_frame_to_text(f, &s);
  return str_of(s);
}
std::string render_full(const RolltuiFrame* next, unsigned char depth) {
  RolltuiStr s;
  rolltui_render_full(next, depth, &s);
  return str_of(s);
}
std::string render_diff(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth) {
  RolltuiStr s;
  rolltui_render_diff(prev, next, depth, &s);
  return str_of(s);
}

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
  const unsigned char depth = ROLLTUI_DEPTH_TRUECOLOR;
  RolltuiStyle bold;
  bold.bold = true;
  RolltuiStyle red;
  red.fg = RolltuiStyleColor::indexed(1);

  // ---- Frame semantics -----------------------------------------------------------
  {
    FramePtr f = new_frame(4, 2);
    check(rolltui_frame_width(f.get()) == 4 && rolltui_frame_height(f.get()) == 2 && glyph_at(f.get(), 3, 1) == " ",
          "a fresh frame is spaces");
    check(put(f.get(), 1, 0, "a", 1, bold) == 1 && glyph_at(f.get(), 1, 0) == "a" && cell_at(f.get(), 1, 0).style == bold,
          "put one cell");
    check(put(f.get(), 2, 0, "\xE4\xB8\xAD", 2, red) == 2 && cell_at(f.get(), 2, 0).width == 2 &&
              cell_at(f.get(), 3, 0).continuation && glyph_at(f.get(), 3, 0).empty(),
          "a wide glyph occupies its cell and a continuation cell");
    check(put(f.get(), 3, 1, "\xE4\xB8\xAD", 2, red) == 1 && glyph_at(f.get(), 3, 1) == " " &&
              cell_at(f.get(), 3, 1).width == 1,
          "a wide glyph at the right edge becomes a space (never straddles)");
    check(put(f.get(), 4, 0, "x", 1, bold) == 0 && put(f.get(), 0, 2, "x", 1, bold) == 0 &&
              put(f.get(), -1, 0, "x", 1, bold) == 0,
          "puts outside the frame are ignored");
    // Overwrite half of the wide glyph: the other half is blanked, no orphan.
    put(f.get(), 3, 0, "z", 1, bold);
    check(glyph_at(f.get(), 2, 0) == " " && cell_at(f.get(), 2, 0).width == 1 && glyph_at(f.get(), 3, 0) == "z",
          "overwriting a continuation cell blanks the glyph");
    put(f.get(), 2, 0, "\xE4\xB8\xAD", 2, red);
    put(f.get(), 2, 0, "y", 1, bold);
    check(glyph_at(f.get(), 3, 0) == " " && !cell_at(f.get(), 3, 0).continuation,
          "overwriting a wide glyph's first cell blanks its continuation");
  }
  {
    FramePtr f = new_frame(6, 1);
    int used = put_text(f.get(), 1, 0, "a\xE4\xB8\xAD" "bcdef", bold, 10);
    check(used == 5 && glyph_at(f.get(), 1, 0) == "a" && cell_at(f.get(), 2, 0).width == 2 &&
              glyph_at(f.get(), 4, 0) == "b" && glyph_at(f.get(), 5, 0) == "c",
          "put_text lays graphemes left to right and clips at the frame edge (used " + std::to_string(used) + ")");
    FramePtr g = new_frame(6, 1);
    check(put_text(g.get(), 0, 0, "abcdef", bold, 3) == 3 && glyph_at(g.get(), 3, 0) == " ",
          "put_text honours max_cells");
    FramePtr h = new_frame(3, 1);
    check(put_text(h.get(), 0, 0, "ab\xE4\xB8\xAD", bold, 10) == 2 && glyph_at(h.get(), 2, 0) == " ",
          "put_text stops before a wide glyph that would straddle the edge");
    FramePtr k = new_frame(4, 1);
    check(put_text(k.get(), 0, 0, "a\x01" "b\xCC\x81" "c", bold, 10) == 3 && glyph_at(k.get(), 1, 0) == "b\xCC\x81",
          "controls skipped, combining marks stay with their base");
    FramePtr m = new_frame(4, 2);
    fill(m.get(), RolltuiRect{1, 0, 10, 10}, red, "#");
    check(glyph_at(m.get(), 0, 0) == " " && glyph_at(m.get(), 1, 0) == "#" && glyph_at(m.get(), 3, 1) == "#" &&
              cell_at(m.get(), 3, 1).style == red,
          "fill clips to the frame");
  }
  {
    RolltuiRect a{0, 0, 10, 5}, b{5, 2, 10, 10};
    RolltuiRect i = a.intersect(b);
    check(i == RolltuiRect{5, 2, 5, 3}, "Rect::intersect");
    check(a.intersect(RolltuiRect{20, 20, 1, 1}).empty(), "disjoint rects intersect to empty");
    check(a.contains(9, 4) && !a.contains(10, 4), "Rect::contains is half-open");
  }

  // ---- golden bytes --------------------------------------------------------------
  {
    FramePtr blank = new_frame(4, 2);
    expect_bytes("full repaint of a blank 4x2 frame", render_full(blank.get(), depth),
                 "\x1b[?25l\x1b[H\x1b[2J"
                 "\x1b[1;1H\x1b[0m    "
                 "\x1b[2;1H    "
                 "\x1b[0m\x1b[1;1H");
    expect_bytes("diff with no prev is a full repaint", render_diff(nullptr, blank.get(), depth),
                 render_full(blank.get(), depth));
    expect_bytes("diff of identical frames is empty", render_diff(blank.get(), blank.get(), depth), "");

    FramePtr next = clone_frame(blank.get());
    put(next.get(), 1, 0, "a", 1, bold);
    put(next.get(), 2, 0, "b", 1, bold);
    expect_bytes("diff addresses the changed run absolutely and emits one SGR",
                 render_diff(blank.get(), next.get(), depth), "\x1b[?25l\x1b[1;2H\x1b[0;1mab\x1b[0m\x1b[1;1H");

    FramePtr wide = clone_frame(blank.get());
    put(wide.get(), 2, 0, "\xE4\xB8\xAD", 2, red);
    expect_bytes("a wide glyph is written once, its continuation cell skipped",
                 render_diff(blank.get(), wide.get(), depth),
                 "\x1b[?25l\x1b[1;3H\x1b[0;31m\xE4\xB8\xAD\x1b[0m\x1b[1;1H");
    FramePtr wide2 = clone_frame(wide.get());
    put(wide2.get(), 3, 0, "z", 1, red);  // blanks the glyph: cells 2 and 3 both change
    expect_bytes("writing over a wide glyph's continuation blanks the whole glyph",
                 render_diff(wide.get(), wide2.get(), depth), "\x1b[?25l\x1b[1;3H\x1b[0;31m z\x1b[0m\x1b[1;1H");

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
    FramePtr wide3 = clone_frame(wide.get());
    rolltui_frame_set_style(wide3.get(), 3, 0, RolltuiStyle{.fg = RolltuiStyleColor::indexed(4)});
    expect_bytes("a run that begins on the continuation cell starts one cell earlier",
                 render_diff(wide.get(), wide3.get(), depth),
                 "\x1b[?25l\x1b[1;3H\x1b[0;31m\xE4\xB8\xAD\x1b[0m\x1b[1;1H");

    FramePtr cur = clone_frame(blank.get());
    rolltui_frame_set_cursor(cur.get(), 3, 1, 1);
    expect_bytes("cursor move only", render_diff(blank.get(), cur.get(), depth), "\x1b[?25l\x1b[0m\x1b[2;4H\x1b[?25h");

    FramePtr two = clone_frame(blank.get());
    put(two.get(), 0, 0, "x", 1, bold);
    put(two.get(), 3, 1, "y", 1, red);
    expect_bytes("two runs on two rows", render_diff(blank.get(), two.get(), depth),
                 "\x1b[?25l\x1b[1;1H\x1b[0;1mx\x1b[2;4H\x1b[0;31my\x1b[0m\x1b[1;1H");

    FramePtr resized = new_frame(5, 2);
    expect_bytes("a size change forces a full repaint", render_diff(blank.get(), resized.get(), depth),
                 render_full(resized.get(), depth));

    FramePtr same_style = clone_frame(blank.get());
    put(same_style.get(), 0, 0, "p", 1, bold);
    put(same_style.get(), 1, 0, "q", 1, bold);
    put(same_style.get(), 2, 0, "r", 1, red);
    expect_bytes("SGR only when the style changes inside a run", render_diff(blank.get(), same_style.get(), depth),
                 "\x1b[?25l\x1b[1;1H\x1b[0;1mpq\x1b[0;31mr\x1b[0m\x1b[1;1H");

    FramePtr mono = clone_frame(blank.get());
    put(mono.get(), 0, 0, "m", 1, red);
    expect_bytes("depth downgrade flows into the emitted SGR", render_diff(blank.get(), mono.get(), ROLLTUI_DEPTH_MONO),
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
    check(family.size() == 25,
          "the control cluster really is longer than a Cell holds (" + std::to_string(family.size()) + " bytes)");
    FramePtr f = new_frame(6, 1);
    put(f.get(), 0, 0, family, 2, RolltuiStyle{});
    check(cell_at(f.get(), 0, 0).spilled(), "a 25-byte cluster SPILLS rather than being truncated into the cell");
    check(glyph_at(f.get(), 0, 0) == family, "…and reads back byte for byte through the one accessor");
    check(cell_at(f.get(), 0, 0).width == 2 && cell_at(f.get(), 1, 0).continuation,
          "…keeping its width and its continuation cell");

    // A ten-byte cluster is the boundary and must NOT spill — an off-by-one here would
    // send every flag emoji through the slow path and nobody would notice.
    FramePtr g = new_frame(4, 1);
    put(g.get(), 0, 0, "0123456789", 1, RolltuiStyle{});
    check(!cell_at(g.get(), 0, 0).spilled() && glyph_at(g.get(), 0, 0) == "0123456789",
          "exactly ten bytes stays INLINE: the boundary is >, not >=");

    // The frame diff compares glyphs BY VALUE across frames, so a spill index minted in
    // one frame is never mistaken for the same index in another.
    FramePtr h = new_frame(6, 1);
    put(h.get(), 0, 0, family, 2, RolltuiStyle{});
    check(render_diff(f.get(), h.get(), ROLLTUI_DEPTH_TRUECOLOR).find(family) == std::string::npos,
          "two frames holding the same long cluster diff to nothing, though their indices are their own");
    FramePtr other = new_frame(6, 1);
    put(other.get(), 0, 0, "x", 1, RolltuiStyle{});
    check(render_diff(other.get(), f.get(), ROLLTUI_DEPTH_TRUECOLOR).find(family) != std::string::npos,
          "…and a frame that actually gained the cluster emits its bytes");
    check(frame_to_text(f.get()).rfind(family, 0) == 0, "frame_to_text carries it too");

    // clear() drops the spill table with the cells that referenced it: a stale entry would
    // grow without bound over a long session, which is the failure a table like this has.
    rolltui_frame_clear(f.get(), RolltuiStyle{});
    put(f.get(), 0, 0, "a", 1, RolltuiStyle{});
    check(!cell_at(f.get(), 0, 0).spilled() && glyph_at(f.get(), 0, 0) == "a", "clear() resets the cells…");
    put(f.get(), 1, 0, family, 2, RolltuiStyle{});
    check(glyph_at(f.get(), 1, 0) == family, "…and the table is reusable afterwards, not poisoned by the reset");
  }

  // ---- Phase 13 m5: reuse the Frame, and the ghosting control ------------------------
  // The failure mode of a hand-written reset is not a crash — it is a STALE FIELD that
  // renders as a perfectly well-formed frame. So the control is equivalence: a reused
  // frame must be indistinguishable from a freshly constructed one, cell for cell,
  // including the fields nobody thinks about.
  {
    const RolltuiStyle fill_style{RolltuiStyleColor::rgb(1, 2, 3), RolltuiStyleColor::rgb(4, 5, 6)};
    // Paint a busy frame: wide glyphs, a link, a spilled cluster, marks, a moved cursor.
    FramePtr used = new_frame(8, 2, fill_style);
    const unsigned int link = rolltui_frame_link_id(used.get(), "https://example.invalid/",
                                                     std::strlen("https://example.invalid/"));
    put_text(used.get(), 0, 0, "abc\xE4\xBD\xA0", RolltuiStyle{}, 8, 0, link);
    put(used.get(), 0, 1, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", 2, RolltuiStyle{});
    rolltui_frame_mark(used.get(), 0, 0, 3, kWaitingState, 0, 0);
    rolltui_frame_set_cursor(used.get(), 4, 1, 1);

    rolltui_frame_reset(used.get(), 8, 2, fill_style);
    FramePtr fresh = new_frame(8, 2, fill_style);
    check(frame_eq(used.get(), fresh.get()),
          "a RESET frame equals a freshly constructed one — every field, not the ones that looked like they mattered");
    check(rolltui_frame_mark_count(used.get()) == 0 && link_at(used.get(), 1).empty() &&
              cursor_of(used.get()) == Cursor{},
          "…including the marks, the link table and the cursor, all of which named cells that are gone");
    check(glyph_at(used.get(), 0, 1) == " " && !cell_at(used.get(), 0, 1).spilled(),
          "…and the spilled glyph, so the table cannot grow across a session");

    // GHOSTING: paint a full frame, reuse it for one that writes strictly fewer cells, and
    // assert nothing of the first survives.
    FramePtr reused = new_frame(8, 2, fill_style);
    put_text(reused.get(), 0, 0, "XXXXXXXX", RolltuiStyle{}, 8);
    put_text(reused.get(), 0, 1, "YYYYYYYY", RolltuiStyle{}, 8);
    rolltui_frame_reset(reused.get(), 8, 2, fill_style);
    put_text(reused.get(), 0, 0, "ab", RolltuiStyle{}, 8);
    FramePtr control = new_frame(8, 2, fill_style);
    put_text(control.get(), 0, 0, "ab", RolltuiStyle{}, 8);
    check(frame_eq(reused.get(), control.get()), "a reused frame painted with FEWER cells has no ghost of the last paint");
    check(frame_to_text(reused.get()).find('X') == std::string::npos &&
              frame_to_text(reused.get()).find('Y') == std::string::npos,
          "…asserted on the text too, since a ghost renders as a perfectly well-formed frame");

    // RESIZE: the geometry is authoritative, and the diff refuses the old baseline.
    FramePtr before = new_frame(8, 2, fill_style);
    put_text(before.get(), 0, 0, "12345678", RolltuiStyle{}, 8);
    FramePtr after = clone_frame(before.get());
    rolltui_frame_reset(after.get(), 4, 3, fill_style);
    FramePtr fresh_4x3 = new_frame(4, 3, fill_style);
    check(rolltui_frame_width(after.get()) == 4 && rolltui_frame_height(after.get()) == 3 &&
              frame_eq(after.get(), fresh_4x3.get()),
          "reset to a new size resizes and still equals a fresh frame of that size");
    const std::string bytes = render_diff(before.get(), after.get(), ROLLTUI_DEPTH_TRUECOLOR);
    check(bytes.rfind("\x1b[?25l\x1b[H\x1b[2J", 0) == 0,
          "…and diffing across a size change is a FULL repaint, so a resize cannot corrupt by geometry");

    // A long run of paints, including resizes, ends where a fresh frame would.
    FramePtr loop = new_frame(8, 2, fill_style);
    for (int i = 0; i < 25; ++i) {
      rolltui_frame_reset(loop.get(), (i % 3) ? 8 : 5, 2, fill_style);
      put_text(loop.get(), 0, 0, "run", RolltuiStyle{}, 8);
    }
    rolltui_frame_reset(loop.get(), 8, 2, fill_style);
    put_text(loop.get(), 0, 0, "ab", RolltuiStyle{}, 8);
    check(frame_eq(loop.get(), control.get()), "…and twenty-five paints with resizes among them leave exactly what one paint would");
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
    const RolltuiRect a{0, 0, 10, 10};
    check(a.intersect({5, 5, 10, 10}) == RolltuiRect{5, 5, 5, 5}, "overlapping rectangles intersect");
    check(a.intersect({20, 20, 5, 5}) == RolltuiRect{20, 20, 0, 0},
          "…and disjoint ones give an EMPTY rect at the clamped origin, not at {0,0}");
    check(a.intersect({2, 2, 3, 3}) == RolltuiRect{2, 2, 3, 3}, "…a contained rect is itself");
    check(a.intersect({0, 0, 0, 0}) == RolltuiRect{0, 0, 0, 0}, "…and a zero-sized one stays zero-sized");
  }

  // ---- THE DOUBLE BUFFER (Phase 17 m4, first called in m3) ---------------------------
  // `rolltui_swap` exists because THREE hosts had hand-written the same `Frame prev; bool
  // have_prev; … render_diff(have_prev ? &prev : nullptr, f); prev = std::move(f);`. Until
  // now the only thing asserted about it was that `_new`/`_free` link (public_header_test),
  // so its whole point — that it produces exactly what the hand-written loop produced — was
  // checked by nothing. These are DIFFERENTIAL against that loop rather than golden strings:
  // the claim is equivalence to the code it deletes, and a golden would only restate the
  // renderer's own tests one file over.
  {
    const RolltuiStyle plain{};
    RolltuiStyle bold{};
    bold.bold = 1;
    RolltuiSwap* s = rolltui_swap_new(8, 2, plain);
    check(rolltui_swap_front(s) == nullptr, "swap: nothing has been presented, so there is no front frame yet");

    RolltuiFrame* a = rolltui_swap_begin(s, 8, 2, plain);
    put_text(a, 0, 0, "hello", bold, 8);
    // What the hand-written loop's FIRST iteration wrote: no baseline, so a full paint.
    FramePtr mirror = clone_frame(a);
    const std::string want_first = render_diff(nullptr, mirror.get(), ROLLTUI_DEPTH_TRUECOLOR);
    RolltuiStr out{};
    rolltui_swap_present(s, ROLLTUI_DEPTH_TRUECOLOR, &out);
    check(view_of(out) == want_first, "swap: the first present is a full paint — render_diff(nullptr, f)");
    check(rolltui_swap_front(s) != nullptr && rolltui_frame_equal(rolltui_swap_front(s), mirror.get()),
          "…and the frame just presented is now the front, byte for byte");

    // A second frame drawn identically: the loop appends nothing, and so does this.
    RolltuiFrame* b = rolltui_swap_begin(s, 8, 2, plain);
    check(b != a, "swap: `begin` alternates between exactly TWO frames — the back is not the one just presented");
    put_text(b, 0, 0, "hello", bold, 8);
    out.clear();
    rolltui_swap_present(s, ROLLTUI_DEPTH_TRUECOLOR, &out);
    check(out.n == 0, "swap: an unchanged frame appends NOTHING (the whole reason a host keeps a baseline)");

    // A third, changed: identical to what diffing against the previous frame gives.
    RolltuiFrame* c = rolltui_swap_begin(s, 8, 2, plain);
    check(c == a, "…and the third `begin` is the FIRST frame again: two frames for the whole run, swapped");
    put_text(c, 0, 0, "world", bold, 8);
    FramePtr next = clone_frame(c);
    const std::string want_third = render_diff(mirror.get(), next.get(), ROLLTUI_DEPTH_TRUECOLOR);
    out.clear();
    rolltui_swap_present(s, ROLLTUI_DEPTH_TRUECOLOR, &out);
    check(view_of(out) == want_third && !want_third.empty(),
          "swap: a changed frame appends exactly render_diff(prev, next) — equivalence with the loop it replaces");

    // The host's POLICY half, which is the only part that legitimately differed between the
    // three hosts: "repaint whole at the next present".
    RolltuiFrame* d = rolltui_swap_begin(s, 8, 2, plain);
    put_text(d, 0, 0, "world", bold, 8);
    rolltui_swap_invalidate(s);
    out.clear();
    rolltui_swap_present(s, ROLLTUI_DEPTH_TRUECOLOR, &out);
    check(view_of(out) == render_diff(nullptr, next.get(), ROLLTUI_DEPTH_TRUECOLOR),
          "swap: after `invalidate` the next present paints in FULL, though nothing on the frame changed");

    // A SIZE change needs no `invalidate` — rolltui_render_diff's own rule 1, which is why
    // `paint.cpp` has zero invalidation sites and that is not a defect.
    RolltuiFrame* e = rolltui_swap_begin(s, 12, 3, plain);
    check(rolltui_frame_width(e) == 12 && rolltui_frame_height(e) == 3, "swap: `begin` resizes the back frame in place");
    put_text(e, 0, 0, "wider", bold, 12);
    FramePtr wide = clone_frame(e);
    out.clear();
    rolltui_swap_present(s, ROLLTUI_DEPTH_TRUECOLOR, &out);
    check(view_of(out) == render_diff(nullptr, wide.get(), ROLLTUI_DEPTH_TRUECOLOR),
          "…and a resize repaints in full with no host call at all");

    // Degenerate sizes, the standing rule for every view in this library.
    RolltuiFrame* z = rolltui_swap_begin(s, 0, 0, plain);
    check(rolltui_frame_width(z) == 0 && rolltui_frame_height(z) == 0, "swap: a 0x0 frame is a frame");
    out.clear();
    rolltui_swap_present(s, ROLLTUI_DEPTH_TRUECOLOR, &out);
    check(true, "…and presenting it does not crash");
    rolltui_str_free(&out);
    rolltui_swap_free(s);
    rolltui_swap_free(nullptr);  // free is a no-op on NULL (rolltui.h rule 1)
  }

  return report("rolltui screen_test");
}
