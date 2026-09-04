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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Effects.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/Theme.hpp"

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
// A GRAPHEME CLUSTER HAS NO MAXIMUM LENGTH, so the long case is handled rather than
// assumed away: ten bytes covers ASCII, accented Latin, CJK, an emoji with a variation
// selector, a flag and an emoji with a skin-tone modifier, and anything longer — a family
// ZWJ sequence is 25+ bytes, and a user can paste one — SPILLS into a table the Frame
// owns, with its index kept where the bytes would have been. That is exactly the shape the
// `link` field already has, which is why it is the shape used: one mechanism, twice.
// Spilling is the phase's "an allocation may happen, but it has a NAME" case.
struct Cell {
  static constexpr std::uint8_t kInlineGlyph = 10;
  static constexpr std::uint8_t kSpilled = 0xFF;  // `len`: the bytes are in the frame's table

  std::uint32_t link = 0;     // 0: none; else an id from Frame::link_id (per frame)
  Style style;
  char bytes[kInlineGlyph] = {' '};  // the cluster, or its spill index when len == kSpilled
  std::uint8_t len = 1;       // bytes in `bytes`; 0 on a continuation cell; kSpilled when spilled
  std::uint8_t width = 1;     // 1 or 2; 0 on a continuation cell
  bool continuation = false;  // the right half of a 2-cell glyph

  bool spilled() const { return len == kSpilled; }
  // The inline bytes. Empty for a continuation cell, and NOT the answer for a spilled
  // cell — `Frame::glyph()` is the one accessor that is right in both cases.
  std::string_view inline_bytes() const { return {bytes, spilled() ? 0u : static_cast<unsigned>(len)}; }
  bool operator==(const Cell&) const = default;
};

struct Cursor {
  int x = 0, y = 0;
  bool visible = false;
  bool operator==(const Cursor&) const = default;
};

class Frame {
 public:
  Frame() = default;
  Frame(int w, int h, const Style& fill = {});

  int width() const { return w_; }
  int height() const { return h_; }
  Rect bounds() const { return {0, 0, w_, h_}; }
  const Cell& at(int x, int y) const { return cells_[static_cast<std::size_t>(y * w_ + x)]; }

  void clear(const Style& fill);
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
  void reset(int w, int h, const Style& fill);
  // Puts one grapheme of `cells` (1 or 2) at (x, y), clipping to the frame and the
  // right edge; returns the cells it occupied (0 when clipped away).
  int put(int x, int y, std::string_view grapheme, int cells, const Style& style,
          std::uint32_t link = 0);
  // Puts a UTF-8 string left to right from (x, y), at most `max_cells` cells and never
  // past the frame's right edge; returns the cells used. Control characters and
  // width-0 clusters are skipped (the wrap engine already stripped them; this is the
  // last line of defence).
  int put_text(int x, int y, std::string_view utf8, const Style& style, int max_cells,
               bool ambiguous_wide = false, std::uint32_t link = 0);
  // THE ONE ACCESSOR for a cell's grapheme, right for an inline cell and a spilled one
  // alike (see Cell above). Reading `at(x, y).bytes` directly is correct only until
  // somebody pastes a family emoji, which is why the bytes are not called `text`.
  std::string_view glyph(int x, int y) const;
  std::string_view glyph_of(const Cell& c) const;
  // Interns a hyperlink target for this frame; the same URL gets the same id. 0 for
  // an empty URL.
  std::uint32_t link_id(std::string_view url);
  // The URL behind an id ("" for 0 or an unknown id).
  std::string_view link(std::uint32_t id) const;
  void fill(Rect r, const Style& style, std::string_view grapheme = " ");
  // Applies `style`'s set colours (fg/bg that are not None) and its attribute bits to
  // every cell in r, leaving the glyphs — a modal's overlay (Layout.hpp).
  void tint(Rect r, const Style& style);
  // Replaces one cell's style whole, leaving its glyph — what an effect does when it
  // recolours without redrawing (Effects.hpp). Out of bounds is a no-op.
  void set_style(int x, int y, const Style& style);
  void set_cursor(int x, int y, bool visible) { cursor_ = {x, y, visible}; }
  const Cursor& cursor() const { return cursor_; }

  // ---- MARKS: what a widget says instead of animating (Effects.hpp) -------------------
  // `cells` cells from (x, y) on one row are in `state`. That is the whole of a widget's
  // vocabulary for motion: it never names a glyph, a colour or a period, and the theme
  // may map the state to nothing at all. A mark with no state or no cells is not
  // recorded, so "is anything marked" and "does anything move" stay the same question.
  void mark(int x, int y, int cells, EffectState state, std::uint64_t since_ms = 0, double fraction = 0);
  const std::vector<Mark>& marks() const { return marks_; }

  bool operator==(const Frame&) const = default;

 private:
  Cell& mut(int x, int y) { return cells_[static_cast<std::size_t>(y * w_ + x)]; }
  void set_glyph(Cell& c, std::string_view g);
  int w_ = 0, h_ = 0;
  std::vector<Cell> cells_;
  Cursor cursor_;
  std::vector<std::string> links_;        // links_[id - 1]
  std::vector<std::string> long_glyphs_;  // m4: clusters too long to sit in a Cell
  std::vector<Mark> marks_;
};

// The frame as plain text: one line per row, continuation cells skipped, trailing
// spaces trimmed — the golden harness's and the pty test's view of a frame.
std::string frame_to_text(const Frame& f);

// Bytes that bring the terminal from `prev` (nullptr: unknown) to `next`.
std::string render_diff(const Frame* prev, const Frame& next, ColorDepth depth);

// Bytes for a full repaint of `next` (clear screen, every cell).
std::string render_full(const Frame& next, ColorDepth depth);

}  // namespace rolltui
