// rolltui/Wrap.cpp — the wrap engine. Contract and rules in Wrap.hpp; every rule is a
// named case in rolltui/tests/wrap_test.cpp, plus the three properties over a corpus.
#include "rolltui/Wrap.hpp"

#include "rolltui/Scratch.hpp"

#include <span>

#include "rolltui/Unicode.hpp"

namespace rolltui {

// Phase 13 m5b: the five setup buffers are REUSED. `wrap()` is called for every row of a
// `rows:` window on every frame AND for every entry that re-lays, so it is both the status
// panel's whole cost (60 of a steady frame's 77 allocations before this) and the bulk of a
// resize. Same algorithm, same UAX #14 breaks — the conformance suite is what says so.
// Not nested: nothing between the first clear and the last use calls back into wrap().
namespace {
// The one implementation; `wrap()` copies its result out, `wrap_borrow()` lends it.
void wrap_into(std::string_view utf8, int width, const WrapOptions& opt, WrapLines& out);
}  // namespace

std::vector<Line> wrap(std::string_view utf8, int width, const WrapOptions& opt) {
  WrapLines w;
  wrap_into(utf8, width, opt, w);
  // MOVE, do not copy: `w` is local and dies here, and copying would allocate a string and
  // a vector per Line. Caught by the budget — the first version copied and put 756
  // allocations back onto the resize frame, which is the one path that calls this in bulk.
  std::vector<Line> out;
  out.reserve(w.n);
  for (std::size_t i = 0; i < w.n; ++i) out.push_back(std::move(w.lines[i]));
  return out;
}

Scratch<WrapLines>::Lock wrap_borrow(std::string_view utf8, int width, const WrapOptions& opt) {
  static thread_local Scratch<WrapLines> scratch("wrap lines");
  auto lock = scratch.lock();
  wrap_into(utf8, width, opt, *lock);
  return lock;
}

namespace {
void wrap_into(std::string_view utf8, int width, const WrapOptions& opt, WrapLines& w) {
  std::vector<Line>& out = w.lines;
  using unicode::Break;
  using unicode::DecodedChar;

  // NOT out.clear(): that would destroy each Line's string and vector. The emit loop
  // assigns into the entries already there and trims the surplus at the end.
  thread_local std::vector<DecodedChar> chars;
  thread_local std::vector<char32_t> cps;
  unicode::decode_utf8_into(utf8, chars);
  cps.assign(chars.size(), 0);
  for (std::size_t i = 0; i < cps.size(); ++i) cps[i] = chars[i].cp;
  thread_local std::vector<Break> before;
  thread_local std::vector<bool> bounds;
  unicode::line_break_opportunities_into(cps, before);
  unicode::grapheme_boundaries_into(cps, bounds);

  // One entry per grapheme cluster, in source order.
  struct G {
    std::size_t byte0, byte1;  // source byte span
    int width;
    Break brk;                 // opportunity before this cluster
    bool space, tab, newline;
  };
  thread_local std::vector<G> gs;
  gs.clear();
  for (std::size_t i = 0, start = 0; i < cps.size(); ++i) {
    if (!bounds[i + 1]) continue;
    G g;
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

  // m5b: `cur` is a reused buffer too, and lines are ASSIGNED into `out`'s existing
  // entries rather than pushed. `out.clear()` would DESTROY each Line — freeing the string
  // and the grapheme vector inside it — which is why lending the outer vector alone bought
  // almost nothing (43 → 39). The reuse has to reach the Line's own storage.
  static thread_local Line cur;
  cur.text.clear();
  cur.graphemes.clear();
  cur.width = 0;
  cur.hard = false;
  cur.indent = indent_for(true);
  std::size_t n_out = 0;
  w.n = 0;
  int avail = nothing ? 0 : width - cur.indent;
  bool any_emitted = false;
  bool pending = false;   // content seen since the last emitted line (even at width 0)
  bool dropping = false;  // inside a run of spaces being dropped at a soft break
  std::size_t last_opportunity = 0;  // grapheme index in cur where a soft break may go
  auto emit = [&](bool hard) {
    cur.hard = hard;
    if (n_out == out.size()) out.emplace_back();
    Line& dst = out[n_out++];
    dst.text.assign(cur.text);                                          // keeps dst's buffer
    dst.graphemes.assign(cur.graphemes.begin(), cur.graphemes.end());   // keeps dst's capacity
    dst.width = cur.width;
    dst.indent = cur.indent;
    dst.hard = cur.hard;
    cur.text.clear();
    cur.graphemes.clear();
    cur.width = 0;
    cur.hard = false;
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
  // NOT out.resize(n_out): shrinking destroys the surplus Lines and frees their buffers,
  // so a caller wrapping rows of DIFFERENT lengths would thrash — which is exactly what the
  // status panel does, four rows a frame. The count is what says how many are live.
  w.n = n_out;
}
}  // namespace

}  // namespace rolltui
