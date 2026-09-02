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

struct Cell {
  std::string text = " ";     // one grapheme cluster; "" on a continuation cell
  std::uint8_t width = 1;     // 1 or 2; 0 on a continuation cell
  bool continuation = false;  // the right half of a 2-cell glyph
  Style style;
  std::uint32_t link = 0;     // 0: none; else an id from Frame::link_id (per frame)
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
  // Interns a hyperlink target for this frame; the same URL gets the same id. 0 for
  // an empty URL.
  std::uint32_t link_id(std::string_view url);
  // The URL behind an id ("" for 0 or an unknown id).
  std::string_view link(std::uint32_t id) const;
  void fill(Rect r, const Style& style, std::string_view grapheme = " ");
  // Applies `style`'s set colours (fg/bg that are not None) and its attribute bits to
  // every cell in r, leaving the glyphs — a modal's overlay (Layout.hpp).
  void tint(Rect r, const Style& style);
  void set_cursor(int x, int y, bool visible) { cursor_ = {x, y, visible}; }
  const Cursor& cursor() const { return cursor_; }

  bool operator==(const Frame&) const = default;

 private:
  Cell& mut(int x, int y) { return cells_[static_cast<std::size_t>(y * w_ + x)]; }
  int w_ = 0, h_ = 0;
  std::vector<Cell> cells_;
  Cursor cursor_;
  std::vector<std::string> links_;  // links_[id - 1]
};

// The frame as plain text: one line per row, continuation cells skipped, trailing
// spaces trimmed — the golden harness's and the pty test's view of a frame.
std::string frame_to_text(const Frame& f);

// Bytes that bring the terminal from `prev` (nullptr: unknown) to `next`.
std::string render_diff(const Frame* prev, const Frame& next, ColorDepth depth);

// Bytes for a full repaint of `next` (clear screen, every cell).
std::string render_full(const Frame& next, ColorDepth depth);

}  // namespace rolltui
