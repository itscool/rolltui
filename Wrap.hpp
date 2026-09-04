#pragma once
//
// rolltui/Wrap.hpp — word wrapping over Unicode.hpp, as a pure function:
//
//   WrapLines wrap(std::string_view utf8, int width, const WrapOptions&)
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
// PHASE 14 m3 — THE LINES ARE A HANDLE. The engine lives behind
// `rolltui/c/rolltui_wrap.h`, in one of two implementations chosen by `-DROLLTUI_C`
// (`WrapCpp.cpp` or `c/rolltui_wrap.c`), and this header is the RAII plus the reading.
// `WrapOptions` and `WrapGrapheme` ARE the C structs (one definition, m2's rule); `Line`
// is a C++ VIEW built from the boundary's out-params in exactly one place, the same
// conversion `Mark` and `Cursor` already get on the Screen boundary.
//
// TWO THINGS A CALLER CAN SEE, both forced by the handle rather than chosen:
//   - `Line::text` is a `string_view` and `Line::graphemes` a `span`, BORROWED from the
//     WrapLines that produced them and valid exactly as long as it is not re-wrapped.
//     A line no longer owns a `std::string`, so a caller that wants one says so.
//   - `wrap()` returns a `WrapLines`, not a `std::vector<Line>`. It is move-only, and
//     one handle's worth of storage rather than a string and a vector per line.
//
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "rolltui/Scratch.hpp"
#include "rolltui/c/rolltui_wrap.h"

namespace rolltui {

// ONE DEFINITION (Phase 14 m3): both are declared in `rolltui/c/rolltui_wrap.h` and
// compiled by both languages, so there is nothing to convert and nothing to drift.
using WrapOptions = RolltuiWrapOptions;
using WrapGrapheme = RolltuiWrapGrapheme;

// One wrapped line, as BORROWS into the WrapLines that produced it. Valid until that
// object is wrapped into again, reset, or destroyed — the same window `Scratch` states
// one level up and `Frame::glyph` states one level down.
struct Line {
  std::string_view text;                     // the bytes to draw, in order
  std::span<const WrapGrapheme> graphemes;   // one per drawn cluster
  int width = 0;                             // cells, excluding `indent`
  int indent = 0;                            // cells the renderer pads before `text`
  bool hard = false;                         // ended by a mandatory break (or end of text)
};

// The lines, and the storage they are views into. Move-only: one owner, structural
// lifetime, no copy that could silently duplicate a frame's worth of buffers.
//
// **THIS IS ALSO THE REUSABLE FORM.** A caller that wraps repeatedly should keep one and
// call `wrap()` on it: every buffer — the lines, their bytes, their graphemes, and the
// decode and UAX #14/#29 scratch behind them — grows to a high-water mark and is never
// freed until the object is. That is what makes a steady frame allocate nothing.
class WrapLines {
 public:
  // OWNED (CLAUDE.md's fourth strategy), through a `unique_ptr` with a deleter that calls
  // the C free — one owner, and no hand-rolled `delete` anywhere.
  struct Handle {
    void operator()(RolltuiWrapLines* p) const { rolltui_wrap_free(p); }
  };

  WrapLines() : w_(rolltui_wrap_new()) {}
  WrapLines(WrapLines&&) noexcept = default;
  WrapLines& operator=(WrapLines&&) noexcept = default;
  WrapLines(const WrapLines&) = delete;
  WrapLines& operator=(const WrapLines&) = delete;

  // Wraps into THIS object, reusing everything it already holds. Any Line taken from it
  // before this call is dead afterwards.
  void wrap(std::string_view utf8, int width, const WrapOptions& opt = {}) {
    rolltui_wrap(w_.get(), utf8.data(), utf8.size(), width, opt);
  }
  // Replaces these lines with copies of another's, reusing this object's buffers. Neither
  // object's wrap scratch is touched: this is how a lent result becomes an owned one.
  void assign(const WrapLines& o) { rolltui_wrap_copy(w_.get(), o.w_.get()); }
  // Drops the lines and keeps every buffer. This is what `Scratch` calls on acquire and
  // on release, which is why a lender's second window costs nothing.
  void clear() { rolltui_wrap_reset(w_.get()); }

  std::size_t size() const { return rolltui_wrap_line_count(w_.get()); }
  bool empty() const { return size() == 0; }
  Line operator[](std::size_t i) const {
    const char* text = nullptr;              // BORROW: the line's bytes, inside the handle
    const WrapGrapheme* graphemes = nullptr; // BORROW: the line's clusters, likewise
    std::size_t text_len = 0, grapheme_count = 0;
    int width = 0, indent = 0, hard = 0;
    rolltui_wrap_line(w_.get(), i, &text, &text_len, &graphemes, &grapheme_count, &width, &indent, &hard);
    return Line{std::string_view(text, text_len), std::span<const WrapGrapheme>(graphemes, grapheme_count),
                width, indent, hard != 0};
  }

  // A `Line` is built on read rather than stored, so this yields BY VALUE — the views
  // inside it point at the handle and outlive the loop variable, which is what makes
  // `for (const Line& l : lines)` safe.
  class iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = Line;
    iterator() = default;
    iterator(const WrapLines* w, std::size_t i) : w_(w), i_(i) {}  // BORROW: the container
    Line operator*() const { return (*w_)[i_]; }
    iterator& operator++() {
      ++i_;
      return *this;
    }
    iterator operator++(int) {
      iterator t = *this;
      ++i_;
      return t;
    }
    bool operator==(const iterator& o) const { return i_ == o.i_; }

   private:
    const WrapLines* w_ = nullptr;  // BORROW: never owns, never outlives the container
    std::size_t i_ = 0;
  };
  iterator begin() const { return iterator(this, 0); }
  iterator end() const { return iterator(this, size()); }

 private:
  std::unique_ptr<RolltuiWrapLines, Handle> w_;
};

// THE LINES, HANDED OVER. Use this when they must OUTLIVE the call. It runs the engine in
// a warm thread-local handle and copies the lines into the returned one, so the decode and
// break buffers are paid for once per thread rather than once per call.
WrapLines wrap(std::string_view utf8, int width, const WrapOptions& opt = {});

// THE SAME LINES, LENT RATHER THAN HANDED OVER (Phase 13 m5b, rolltui/Scratch.hpp). `wrap`
// runs for every row of a `rows:` window on every frame and for every entry that re-lays,
// and returning a fresh result — each Line holding a string AND a vector — was 26 of a
// steady frame's 43 allocations and the bulk of a resize. This keeps the storage with the
// callee and reuses it: the borrow is valid until the Lock goes out of scope, and a second
// one while the first is live ABORTS rather than aliasing.
//
// Use `wrap()` when the lines must outlive the call. Use this in a draw or layout loop,
// which is every hot caller.
Scratch<WrapLines>::Lock wrap_borrow(std::string_view utf8, int width, const WrapOptions& opt = {});

}  // namespace rolltui
