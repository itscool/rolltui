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
//     token with no opportunity inside the width (a URL, a hash, a `!!!!!` run, a CJK
//     run is NOT this — ideographs break anywhere) is hard-broken at a grapheme
//     boundary: never truncated, never overflowed.
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
#include <string_view>
#include <vector>

#include "rolltui/Unicode.hpp"

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

inline std::vector<Line> wrap(std::string_view utf8, int width, const WrapOptions& opt = {}) {
  using unicode::Break;
  using unicode::DecodedChar;
  using unicode::Grapheme;

  std::vector<Line> out;
  std::vector<DecodedChar> chars = unicode::decode_utf8(utf8);
  std::vector<char32_t> cps(chars.size());
  for (std::size_t i = 0; i < cps.size(); ++i) cps[i] = chars[i].cp;
  std::vector<Break> before = unicode::line_break_opportunities(cps);
  std::vector<bool> bounds = unicode::grapheme_boundaries(cps);

  // One entry per grapheme cluster, in source order.
  struct G {
    std::size_t cp_first, cp_last;  // indices into cps (inclusive)
    std::size_t byte0, byte1;       // source byte span
    int width;
    Break brk;                      // opportunity before this cluster
    bool space, tab, newline;
  };
  std::vector<G> gs;
  for (std::size_t i = 0, start = 0; i < cps.size(); ++i) {
    if (!bounds[i + 1]) continue;
    G g;
    g.cp_first = start;
    g.cp_last = i;
    g.byte0 = chars[start].offset;
    g.byte1 = chars[i].offset + chars[i].length;
    g.width = unicode::cluster_width(std::span<const char32_t>(cps).subspan(start, i + 1 - start),
                                     opt.ambiguous_wide);
    g.brk = before[start];
    g.space = (i == start && cps[start] == 0x20);
    g.tab = (i == start && cps[start] == '\t');
    char32_t c0 = cps[start];
    g.newline = (c0 == 0x0A || c0 == 0x0B || c0 == 0x0C || c0 == 0x0D || c0 == 0x85 ||
                 c0 == 0x2028 || c0 == 0x2029);  // CR LF is one cluster (GB3)
    gs.push_back(g);
    start = i + 1;
  }

  const bool nothing = (width <= 0);
  auto indent_for = [&](bool first) {
    int ind = first ? opt.first_indent : opt.hanging_indent;
    if (ind < 0) ind = 0;
    if (!nothing && ind >= width) ind = width - 1;  // keep at least one cell of text
    return ind;
  };

  Line cur;
  cur.indent = indent_for(true);
  int avail = nothing ? 0 : width - cur.indent;
  bool any_emitted = false;
  bool pending = false;   // content seen since the last emitted line (even at width 0)
  bool dropping = false;  // inside a run of spaces being dropped at a soft break
  std::size_t last_opportunity = 0;  // grapheme index in cur where a soft break may go
  // Per-grapheme records for cur, in parallel with cur.graphemes, to allow cutting.
  auto emit = [&](bool hard) {
    cur.hard = hard;
    out.push_back(std::move(cur));
    cur = Line{};
    cur.indent = indent_for(false);
    avail = nothing ? 0 : width - cur.indent;
    any_emitted = true;
    pending = false;
    last_opportunity = 0;
    dropping = false;
  };
  auto append = [&](std::string_view bytes, std::size_t source_offset, int w, bool space) {
    cur.graphemes.push_back({cur.text.size(), bytes.size(), source_offset, w, space});
    cur.text.append(bytes);
    cur.width += w;
    pending = true;
  };
  auto drop_trailing_spaces = [&]() {
    while (!cur.graphemes.empty() && cur.graphemes.back().space) {
      cur.width -= cur.graphemes.back().width;
      cur.text.resize(cur.graphemes.back().offset);
      cur.graphemes.pop_back();
    }
    if (last_opportunity > cur.graphemes.size()) last_opportunity = cur.graphemes.size();
  };

  for (std::size_t gi = 0; gi < gs.size(); ++gi) {
    const G& g = gs[gi];
    if (g.newline) {  // terminates the current line, even an empty one
      emit(true);
      continue;
    }
    if (g.brk == Break::Allowed) last_opportunity = cur.graphemes.size();
    if (g.width > 0 || g.tab || g.space) pending = true;
    if (g.tab) {
      if (nothing) continue;
      int tw = opt.tab_width > 0 ? opt.tab_width : 8;
      int n = tw - (cur.width % tw);
      for (int k = 0; k < n; ++k) {
        if (cur.width + 1 > avail) { dropping = true; break; }  // overflowing spaces: dropped
        if (dropping) break;
        append(" ", g.byte0, 1, true);
      }
      continue;
    }
    if (g.width == 0) continue;  // draws nothing
    if (nothing) continue;
    std::string_view bytes = utf8.substr(g.byte0, g.byte1 - g.byte0);
    if (g.space) {
      if (dropping || cur.width + g.width > avail) { dropping = true; continue; }
      append(bytes, g.byte0, g.width, true);
      continue;
    }
    if (dropping) {
      // A non-space after dropped spaces starts the next line (SP ÷ is the opportunity).
      drop_trailing_spaces();
      emit(false);
    }
    if (cur.width + g.width > avail && !cur.graphemes.empty()) {
      if (last_opportunity > 0 && last_opportunity < cur.graphemes.size()) {
        // Cut at the last opportunity; the tail moves to the next line.
        std::vector<WrapGrapheme> tail(cur.graphemes.begin() + static_cast<long>(last_opportunity),
                                       cur.graphemes.end());
        std::string tail_text = cur.text.substr(tail.front().offset);
        cur.text.resize(tail.front().offset);
        cur.graphemes.resize(last_opportunity);
        cur.width = 0;
        for (const WrapGrapheme& t : cur.graphemes) cur.width += t.width;
        drop_trailing_spaces();
        emit(false);
        const std::size_t tail0 = tail.front().offset;
        for (const WrapGrapheme& t : tail) {
          std::string_view tb(tail_text.data() + (t.offset - tail0), t.length);
          append(tb, t.source_offset, t.width, t.space);
        }
        if (g.brk == Break::Allowed) last_opportunity = cur.graphemes.size();
        // g still has to be placed; the tail holds no opportunity but its start, so
        // if it does not fit now the only remaining cut is right before it.
        if (cur.width + g.width > avail && !cur.graphemes.empty()) {
          drop_trailing_spaces();
          emit(false);
        }
      } else {
        // No opportunity inside the line (or only at its start): hard-break here.
        drop_trailing_spaces();
        emit(false);
      }
    }
    append(bytes, g.byte0, g.width, false);
  }
  // End of text: the unterminated last segment is a line if it has content, or if it
  // is the whole (empty) text. A trailing newline has already emitted its line.
  if (pending || !any_emitted) emit(true);
  return out;
}

}  // namespace rolltui
