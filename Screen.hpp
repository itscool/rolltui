#pragma once
//
// rolltui/Screen.hpp — the cell grid and the frame diff, pure:
//
//   Frame        a W×H grid of cells (one grapheme + its Style each; a 2-cell glyph
//                occupies its cell and a continuation cell), plus a cursor
//   render_diff  bytes that turn what the terminal shows (prev) into next; a full
//                repaint when there is no prev or the size changed
//
// A frontend draws every frame from state into a fresh Frame (plan/phase-9.md,
// requirement 3); the diff is the only place the previous frame is remembered, and
// only to save bytes — it never feeds back into what is drawn. Golden byte strings
// for the diff live in rolltui/tests/screen_test.cpp.
//
// Emission rules, each asserted:
//   - the cursor is hidden while drawing and restored (position + visibility) after;
//   - every changed run is addressed absolutely (CUP), never by newline or relative
//     motion, so the bottom-right cell is safe to write and a lost byte cannot shift
//     every later line;
//   - SGR is emitted only when the style changes within the bytes being written, and
//     always as a full reset-plus-attributes (Theme.hpp's sgr), so no cell's look
//     depends on the previous cell's;
//   - a 2-cell glyph that would straddle the right edge is drawn as a space instead
//     (never overflowed); a run that starts on a continuation cell starts one cell
//     earlier so the glyph is rewritten whole;
//   - text is clipped to the frame; nothing is ever written outside it;
//   - a cell may carry a hyperlink (an id into the frame's link table, interned by
//     `link_id`); the diff wraps every run of linked cells in OSC 8 (`ESC ] 8 ; ; URL
//     ST` … `ESC ] 8 ; ; ST`) and always closes the link before the run ends, so an
//     unchanged neighbour can never inherit it. A frame with no links emits no OSC 8
//     byte at all. Terminals without OSC 8 ignore it (plan/phase-9.md: links are
//     emitted by the renderer from a PARSED URL, never passed through from text).
//
// PHASE 14 m2 — THE FRAME IS A HANDLE. Its storage lives behind
// `rolltui/c/rolltui_screen.h`, in one of two implementations chosen by `-DROLLTUI_C`
// (`ScreenCpp.cpp` or `c/rolltui_screen.c`), and the class below is the RAII plus the
// loops that are built out of the primitives — `put_text`, `fill`, `tint` and the three
// renderers, none of which needs to know which side answered. `Cell` and `Style` ARE the C
// structs (one definition; see rolltui_style.h), so nothing is converted at the seam.
//
// TWO THINGS A CALLER CAN SEE, both forced by the handle rather than chosen:
//   - `at()` returns a Cell BY VALUE. A view from `Cell::inline_bytes()` therefore dies
//     with the copy; `glyph(x, y)` borrows from the FRAME and is the accessor to use.
//   - the marks are `mark_count()` + `mark_at(i)`, not a `const std::vector<Mark>&`, which
//     nothing on the C side can supply without allocating a vector per call.
//
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Effects.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/c/rolltui_screen.h"

namespace rolltui {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  Rect intersect(const Rect& o) const;
  bool empty() const { return w <= 0 || h <= 0; }
  bool operator==(const Rect&) const = default;
};

// A cell's grapheme lives INLINE (Phase 13 m4). It used to be a `std::string`, which made
// `Cell` 48 bytes and constructed-and-destructed one string per cell per frame — 4,800 of
// them on a 120x40 grid, for a cluster that is 1-4 bytes almost always. Those strings were
// SSO and never reached the heap, so this is a BYTES and cache-locality change and NOT an
// allocation-count one; the grid goes from 230 KB to 153 KB.
//
// ONE DEFINITION (Phase 14 m2): the struct, the spill rule and the two accessors are in
// `rolltui/c/rolltui_screen.h`, compiled by both languages.
using Cell = RolltuiCell;

struct Cursor {
  int x = 0, y = 0;
  bool visible = false;
  bool operator==(const Cursor&) const = default;
};

class Frame {
 public:
  // OWNED (CLAUDE.md's fourth strategy), through a `unique_ptr` with a deleter that calls
  // the C free — one owner, structural lifetime, and no hand-rolled `delete` anywhere. A
  // default-constructed Frame is a valid 0x0 one, which is what the hosts' `Frame prev;`
  // wants; a MOVED-FROM one is valid only to destroy or assign to, the ordinary contract.
  struct Handle {
    void operator()(RolltuiFrame* p) const { rolltui_frame_free(p); }
  };

  Frame() : f_(rolltui_frame_new(0, 0, Style{})) {}
  Frame(int w, int h, const Style& fill = {}) : f_(rolltui_frame_new(w, h, fill)) {}
  Frame(const Frame& o) : f_(rolltui_frame_clone(o.f_.get())) {}
  Frame& operator=(const Frame& o) {
    if (this != &o) f_.reset(rolltui_frame_clone(o.f_.get()));
    return *this;
  }
  Frame(Frame&&) noexcept = default;
  Frame& operator=(Frame&&) noexcept = default;

  int width() const { return rolltui_frame_width(f_.get()); }
  int height() const { return rolltui_frame_height(f_.get()); }
  Rect bounds() const { return {0, 0, width(), height()}; }
  // BY VALUE: an opaque handle cannot lend a reference into itself and stay opaque. A view
  // from the returned Cell's `inline_bytes()` dies with it — use `glyph(x, y)`.
  Cell at(int x, int y) const {
    Cell c;
    rolltui_frame_cell(f_.get(), x, y, &c);
    return c;
  }

  void clear(const Style& fill) { rolltui_frame_clear(f_.get(), fill); }
  // REUSES this frame's storage for the next paint (Phase 13 m5), instead of constructing
  // a new one and throwing 153 KB away every repaint. It is EXACTLY equivalent to
  // `Frame(w, h, fill)` — asserted, because the failure mode of a hand-written reset is
  // ghosting: one field left over from the last paint renders as a perfectly well-formed
  // frame that is quietly wrong. So this resets every field of every cell, and drops the
  // link table, the spilled glyphs and the marks, all of which named cells that are gone.
  //
  // A SIZE CHANGE is safe here and is NOT safe to diff against: `render_diff` already
  // repaints in full when the dimensions differ, which is what keeps rule 3 (a resize
  // invalidates the baseline) a property of the code rather than of the caller.
  void reset(int w, int h, const Style& fill) { rolltui_frame_reset(f_.get(), w, h, fill); }
  // Puts one grapheme of `cells` (1 or 2) at (x, y), clipping to the frame and the
  // right edge; returns the cells it occupied (0 when clipped away).
  int put(int x, int y, std::string_view grapheme, int cells, const Style& style, std::uint32_t link = 0) {
    return rolltui_frame_put(f_.get(), x, y, grapheme.data(), grapheme.size(), cells, style, link);
  }
  // Puts a UTF-8 string left to right from (x, y), at most `max_cells` cells and never
  // past the frame's right edge; returns the cells used. Control characters and
  // width-0 clusters are skipped (the wrap engine already stripped them; this is the
  // last line of defence).
  int put_text(int x, int y, std::string_view utf8, const Style& style, int max_cells,
               bool ambiguous_wide = false, std::uint32_t link = 0);
  // THE ONE ACCESSOR for a cell's grapheme, right for an inline cell and a spilled one
  // alike (see Cell above). Reading `at(x, y).bytes` directly is correct only until
  // somebody pastes a family emoji, which is why the bytes are not called `text`.
  // A BORROW from the frame, valid until that cell is written again.
  std::string_view glyph(int x, int y) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_glyph(f_.get(), x, y, &n);
    return std::string_view(p, n);
  }
  // Interns a hyperlink target for this frame; the same URL gets the same id. 0 for
  // an empty URL.
  std::uint32_t link_id(std::string_view url) { return rolltui_frame_link_id(f_.get(), url.data(), url.size()); }
  // The URL behind an id ("" for 0 or an unknown id). A borrow, valid until the next reset.
  std::string_view link(std::uint32_t id) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_link(f_.get(), id, &n);
    return std::string_view(p, n);
  }
  void fill(Rect r, const Style& style, std::string_view grapheme = " ");
  // Applies `style`'s set colours (fg/bg that are not None) and its attribute bits to
  // every cell in r, leaving the glyphs — a modal's overlay (Layout.hpp).
  void tint(Rect r, const Style& style);
  // Replaces one cell's style whole, leaving its glyph — what an effect does when it
  // recolours without redrawing (Effects.hpp). Out of bounds is a no-op.
  void set_style(int x, int y, const Style& style) { rolltui_frame_set_style(f_.get(), x, y, style); }
  void set_cursor(int x, int y, bool visible) { rolltui_frame_set_cursor(f_.get(), x, y, visible); }
  Cursor cursor() const {
    int x = 0, y = 0, visible = 0;
    rolltui_frame_cursor(f_.get(), &x, &y, &visible);
    return {x, y, visible != 0};
  }

  // ---- MARKS: what a widget says instead of animating (Effects.hpp) -------------------
  // `cells` cells from (x, y) on one row are in `state`. That is the whole of a widget's
  // vocabulary for motion: it never names a glyph, a colour or a period, and the theme
  // may map the state to nothing at all. A mark with no state or no cells is not
  // recorded, so "is anything marked" and "does anything move" stay the same question.
  void mark(int x, int y, int cells, EffectState state, std::uint64_t since_ms = 0, double fraction = 0) {
    rolltui_frame_mark(f_.get(), x, y, cells, static_cast<int>(state), since_ms, fraction);
  }
  // COUNT + INDEX, not a `const std::vector<Mark>&`: the handle cannot lend a vector it
  // does not keep, and materialising one would be an allocation per call on a path Phase 13
  // took to zero. `EffectState` crosses as its underlying int and is never interpreted over
  // there, which keeps the effects vocabulary in Effects.hpp alone.
  std::size_t mark_count() const { return rolltui_frame_mark_count(f_.get()); }
  Mark mark_at(std::size_t i) const {
    int x = 0, y = 0, cells = 0, state = 0;
    unsigned long long since_ms = 0;
    double fraction = 0;
    rolltui_frame_mark_at(f_.get(), i, &x, &y, &cells, &state, &since_ms, &fraction);
    return {x, y, cells, static_cast<EffectState>(state), since_ms, fraction};
  }

  // EQUALITY IS ABOUT WHAT THE FRAME SHOWS, not about what it is holding on to. The link
  // and spill tables keep their strings past a `reset` so the next frame can assign into
  // them (m5b), and a defaulted `==` compared that retained capacity — so a reset frame
  // stopped equalling a fresh one, which is the ghosting control's exact question and the
  // wrong answer to it. Only the LIVE entries are compared.
  bool operator==(const Frame& o) const { return rolltui_frame_equal(f_.get(), o.f_.get()) != 0; }

 private:
  std::unique_ptr<RolltuiFrame, Handle> f_;
};

// The frame as plain text: one line per row, continuation cells skipped, trailing
// spaces trimmed — the golden harness's and the pty test's view of a frame.
std::string frame_to_text(const Frame& f);

// Bytes that bring the terminal from `prev` (nullptr: unknown) to `next`.
std::string render_diff(const Frame* prev, const Frame& next, ColorDepth depth);

// Bytes for a full repaint of `next` (clear screen, every cell).
std::string render_full(const Frame& next, ColorDepth depth);

}  // namespace rolltui
