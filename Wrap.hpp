#pragma once
//
// rolltui/Wrap.hpp — word wrapping over Unicode.hpp, as a pure function:
//
//   std::vector<Line> wrap(std::string_view utf8, int width, const WrapOptions&)
//
// A Line is the bytes to draw plus one entry per grapheme cluster with its cell width
// and the byte offset it came from in the source, so a renderer never re-measures and
// a selection model can map a drawn cell back to the logical text.
//
// Rules (each asserted in rolltui/tests/wrap_test.cpp):
//   - Breaks happen only at UAX #14 opportunities that fall on grapheme boundaries. A
//     token with no opportunity inside the width (a hash, a `!!!!!` run — a URL is NOT
//     this, it breaks after every `/` and `?`; a CJK run breaks anywhere) is
//     hard-broken at a grapheme boundary: never truncated, never overflowed.
//   - The one exception to "never overflowed": a single grapheme wider than the width
//     (a 2-cell ideograph at width 1) is placed on a line of its own and overflows by
//     one cell, because dropping it would violate "every grapheme appears once".
//   - Mandatory breaks (LF, CR, CRLF, NEL, VT, FF, LS, PS) end a line, marked `hard`.
//     A trailing newline does not produce a trailing empty line; a blank line between
//     two newlines does.
//   - Trailing spaces (U+0020 and expanded tabs) at a SOFT break are dropped: they are
//     not counted, drawn or carried to the next line. Spaces at a hard break or at the
//     end of text are kept if they fit (an input window needs the cell after a typed
//     space) and dropped if they would overflow.
//   - Tabs expand to the next multiple of `tab_width` (measured from the line's first
//     cell, after the indent); each expanded space records the tab's source offset.
//   - Grapheme clusters of width 0 (controls, a lone combining mark, ZWSP, BOM, soft
//     hyphen, variation selectors on their own) are stripped: they draw nothing and
//     would corrupt a cell count. Their break semantics survive (ZWSP still separates).
//   - `first_indent` / `hanging_indent` are cells the renderer pads before the first
//     line / every later line; the wrap width is reduced accordingly, and an indent
//     that leaves fewer than one cell is clamped so text still makes progress.
//   - width <= 0 is legal and renders nothing: one empty Line per hard-break-separated
//     paragraph (a caller can still count them). Empty input renders one empty line.
//   - East Asian ambiguous-width characters are narrow unless `ambiguous_wide`.
//   - No bidi reordering: right-to-left text is laid out in logical order. Stated limit.
//   - ANSI/OSC escape sequences are NOT interpreted here. ESC itself is a control and
//     is stripped, so the rest of a sequence would render as text; the adapter strips
//     model output before it reaches any renderer (plan/phase-9.md, "Model output is
//     data, never terminal input").
//
#include <cstddef>
#include <string>

#include "rolltui/Scratch.hpp"
#include <string_view>
#include <vector>

namespace rolltui {

struct WrapOptions {
  bool ambiguous_wide = false;
  int tab_width = 8;
  int first_indent = 0;    // cells before the first line
  int hanging_indent = 0;  // cells before every subsequent line
};

struct WrapGrapheme {
  std::size_t offset;         // into Line::text
  std::size_t length;         // bytes in Line::text
  std::size_t source_offset;  // byte offset in the wrap() input (a tab's, for its spaces)
  int width;                  // cells
  bool space;                 // U+0020 or an expanded tab: droppable at a soft break
};

struct Line {
  std::string text;                     // the bytes to draw, in order
  std::vector<WrapGrapheme> graphemes;  // one per drawn cluster
  int width = 0;                        // cells, excluding `indent`
  int indent = 0;                       // cells the renderer pads before `text`
  bool hard = false;                    // ended by a mandatory break (or end of text)
};

std::vector<Line> wrap(std::string_view utf8, int width, const WrapOptions& opt = {});

// THE SAME LINES, LENT RATHER THAN HANDED OVER (Phase 13 m5b, rolltui/Scratch.hpp). `wrap`
// runs for every row of a `rows:` window on every frame and for every entry that re-lays,
// and returning a fresh `vector<Line>` — each `Line` holding a string AND a vector — was
// 26 of a steady frame's 43 allocations and the bulk of a resize. This keeps the storage
// with the callee and reuses it: the borrow is valid until the Lock goes out of scope, and
// a second one while the first is live ABORTS rather than aliasing.
//
// Use `wrap()` when the lines must outlive the call. Use this in a draw or layout loop,
// which is every hot caller.
//
// **`clear()` IS THE WRONG RESET FOR A CONTAINER OF OWNING ELEMENTS**, and that is why this
// is its own type rather than a `vector<Line>`: clearing a `vector<Line>` destroys each
// Line and frees the string and the vector INSIDE it, which is precisely the storage being
// reused. Found by measurement — lending the outer vector alone took a steady frame from
// 43 to 39, and reaching into the Lines took it to 25. So the reset here sets a COUNT and
// keeps every Line intact.
struct WrapLines {
  std::vector<Line> lines;  // storage; grows to the high-water mark and never shrinks
  std::size_t n = 0;        // how many of them are live

  void clear() { n = 0; }   // what Scratch calls: keeps every Line's buffers
  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  const Line& operator[](std::size_t i) const { return lines[i]; }
  const Line* begin() const { return lines.data(); }
  const Line* end() const { return lines.data() + n; }
};
Scratch<WrapLines>::Lock wrap_borrow(std::string_view utf8, int width, const WrapOptions& opt = {});

}  // namespace rolltui
