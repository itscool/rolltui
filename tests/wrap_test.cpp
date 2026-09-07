//
// wrap_test.cpp — the wrap engine (rolltui/Wrap.hpp): the edge-case table from
// the plan (CJK, ZWJ emoji, combining marks, a 300-cell URL, tabs, width 1,
// empty, spaces-only) with exact expected lines, then the three properties over a
// corpus and a seeded random generator at every width 0..48:
//   1. no line exceeds the width, except a line holding a single grapheme that is
//      itself wider than the width;
//   2. every input grapheme appears exactly once across the lines, in order — with
//      the stated exception that spaces dropped at a soft break vanish, so the
//      drawn sequence is the source sequence minus some spaces, and no line ends in
//      a space unless it ended hard or is the last;
//   3. wrapping the joined output again at the same width is idempotent.
//
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_md_lines.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_wrap.h"  // INTERNAL: this test opts in

using namespace rolltui_test;

namespace {

// ---- local mirrors of the C++ shims (rolltui/Wrap.hpp, rolltui/Unicode.hpp), over the C
// API directly -- both headers are being removed, and the shims ARE the mapping for what
// follows: same names, same shapes, one level down.

// Mirrors WrapLines/Line (rolltui/Wrap.hpp): an OWNED handle plus a BORROWED per-line view
// built on read. `wrap()` fills it in place (rolltui_wrap, reusing all storage); `clear()`
// drops the lines and keeps the storage (rolltui_wrap_reset); `clone()` is the separate-
// block copy `rolltui_wrap_clone` makes, which is what `wrap_handoff` below uses it for.
struct Line {
  std::string_view text;                    // the bytes to draw, in order
  std::span<const RolltuiWrapGrapheme> graphemes;  // one per drawn cluster
  int width = 0;                            // cells, excluding `indent`
  int indent = 0;                           // cells the renderer pads before `text`
  bool hard = false;                        // ended by a mandatory break (or end of text)
};

class Lines {
 public:
  Lines() : w_(rolltui_wrap_new()) {}
  Lines(Lines&& o) noexcept : w_(o.w_) { o.w_ = nullptr; }
  Lines& operator=(Lines&& o) noexcept {
    if (this != &o) {
      rolltui_wrap_free(w_);
      w_ = o.w_;
      o.w_ = nullptr;
    }
    return *this;
  }
  Lines(const Lines&) = delete;
  Lines& operator=(const Lines&) = delete;
  ~Lines() { rolltui_wrap_free(w_); }

  // Wraps INTO this handle, reusing everything it already holds. Any Line taken from it
  // before this call is dead afterwards.
  void wrap(std::string_view input, int width, RolltuiWrapOptions opt = {}) {
    rolltui_wrap(w_, input.data(), input.size(), width, opt);
  }
  // Drops the lines and keeps every buffer (rolltui_wrap_reset).
  void clear() { rolltui_wrap_reset(w_); }
  // A new Lines holding a COPY of these lines and no scratch -- how a LENT result becomes
  // an OWNED one, and what `wrap_handoff` below hands back.
  Lines clone() const { return Lines(rolltui_wrap_clone(w_)); }

  std::size_t size() const { return rolltui_wrap_line_count(w_); }
  bool empty() const { return size() == 0; }
  Line operator[](std::size_t i) const {
    const char* text = nullptr;
    const RolltuiWrapGrapheme* graphemes = nullptr;
    std::size_t text_len = 0, grapheme_count = 0;
    int width = 0, indent = 0, hard = 0;
    rolltui_wrap_line(w_, i, &text, &text_len, &graphemes, &grapheme_count, &width, &indent, &hard);
    return Line{std::string_view(text, text_len), std::span<const RolltuiWrapGrapheme>(graphemes, grapheme_count),
                width, indent, hard != 0};
  }

  class iterator {
   public:
    iterator(const Lines* l, std::size_t i) : l_(l), i_(i) {}
    Line operator*() const { return (*l_)[i_]; }
    iterator& operator++() {
      ++i_;
      return *this;
    }
    bool operator==(const iterator& o) const { return i_ == o.i_; }

   private:
    const Lines* l_ = nullptr;
    std::size_t i_ = 0;
  };
  iterator begin() const { return iterator(this, 0); }
  iterator end() const { return iterator(this, size()); }

 private:
  explicit Lines(RolltuiWrapLines* owned) : w_(owned) {}
  RolltuiWrapLines* w_;
};

// Mirrors the free function `rolltui::wrap()` (rolltui/Wrap.cpp): wraps into a scratch
// handle, then clones the lines out into a fresh, scratch-free handle -- the "handed over"
// ownership path `rolltui_wrap_clone` documents, and the one every bare `wrap(...)` call
// below goes through (an in-place `.wrap()` on an existing Lines does not clone).
Lines wrap_handoff(std::string_view input, int width, RolltuiWrapOptions opt = {}) {
  Lines scratch;
  scratch.wrap(input, width, opt);
  return scratch.clone();
}

std::vector<std::string> texts(const Lines& lines) {
  std::vector<std::string> v;
  // `Line::text` is a BORROW now, so a caller that wants a string says so.
  for (const Line& l : lines) v.emplace_back(l.text);
  return v;
}

std::string show(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += "[" + x + "]";
  return s;
}

// Assert exact lines. The expected list is written as the drawn bytes per line.
void expect_lines(const std::string& name, std::string_view input, int width,
                  const std::vector<std::string>& want, const RolltuiWrapOptions& opt = {}) {
  std::vector<std::string> got = texts(wrap_handoff(input, width, opt));
  check(got == want, name + "  want " + show(want) + "  got " + show(got));
}

// Mirrors unicode::graphemes (rolltui/Unicode.hpp), over the C API directly.
std::vector<RolltuiUnicodeGrapheme> graphemes(RolltuiUnicodeScratch* scratch, std::string_view utf8) {
  std::vector<RolltuiUnicodeGrapheme> out(utf8.size());  // no more clusters than bytes
  const std::size_t n = rolltui_u_graphemes(scratch, utf8.data(), utf8.size(), false, out.data());
  out.resize(n);
  return out;
}

// Graphemes of a string as byte strings (via the Unicode C API), for property 2.
std::vector<std::string> grapheme_strings(RolltuiUnicodeScratch* scratch, std::string_view s) {
  std::vector<std::string> v;
  for (const RolltuiUnicodeGrapheme& g : graphemes(scratch, s)) v.emplace_back(s.substr(g.offset, g.length));
  return v;
}

struct Rng {  // xorshift64*, seeded: the corpus is reproducible
  std::uint64_t s;
  std::uint64_t next() {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return s * 0x2545F4914F6CDD1DULL;
  }
  std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
};

// Check the three properties for one input at one width. Returns a failure
// description or "".
std::string properties(RolltuiUnicodeScratch* u_scratch, std::string_view input, int width) {
  Lines lines = wrap_handoff(input, width);
  if (lines.empty()) return "no lines";
  // 1. width
  for (const Line& l : lines) {
    int w = 0;
    for (const RolltuiWrapGrapheme& g : l.graphemes) w += g.width;
    if (w != l.width) return "Line::width disagrees with its graphemes";
    if (width > 0 && l.width + l.indent > width && !(l.graphemes.size() == 1 && l.graphemes[0].width > width))
      return "line exceeds width " + std::to_string(width) + ": [" + std::string(l.text) + "]";
    if (width <= 0 && !l.graphemes.empty()) return "width<=0 drew something";
  }
  // 2. every grapheme once, in order, minus dropped spaces; drawn width-0 never
  if (width > 0) {
    std::vector<std::string> src;
    for (const std::string& g : grapheme_strings(u_scratch, input)) {
      if (rolltui_u_display_width(u_scratch, g.data(), g.size(), false) == 0 && g != "\t") continue;  // stripped
      src.push_back(g);
    }
    std::vector<std::string> drawn;
    for (const Line& l : lines)
      for (const RolltuiWrapGrapheme& g : l.graphemes) drawn.emplace_back(l.text.substr(g.offset, g.length));
    // Walk src; each drawn grapheme must match the next src grapheme, where a tab in
    // src matches 1..8 drawn spaces, and src spaces/tabs may be skipped (dropped).
    std::size_t si = 0, di = 0;
    while (di < drawn.size()) {
      if (si >= src.size()) return "drew more than the source has: [" + drawn[di] + "]";
      if (src[si] == "\t") {
        int n = 0;
        while (di < drawn.size() && drawn[di] == " " && n < 8) { ++di; ++n; }
        ++si;
        continue;
      }
      if (src[si] == drawn[di]) { ++si; ++di; continue; }
      if (src[si] == " ") { ++si; continue; }  // dropped
      return "order broken at drawn [" + drawn[di] + "] vs source [" + src[si] + "]";
    }
    for (; si < src.size(); ++si)
      if (src[si] != " " && src[si] != "\t") return "source grapheme never drawn: [" + src[si] + "]";
    for (std::size_t i = 0; i + 1 < lines.size(); ++i)
      if (!lines[i].hard && !lines[i].graphemes.empty() && lines[i].graphemes.back().space)
        return "soft-broken line ends in a space: [" + std::string(lines[i].text) + "]";
  }
  // 3. idempotence: every drawn line newline-terminated (a trailing newline makes no
  //    extra line, so this is the exact inverse of the line convention)
  if (width > 0) {
    std::string joined;
    for (const Line& l : lines) {
      joined += l.text;
      joined += '\n';
    }
    std::vector<std::string> again = texts(wrap_handoff(joined, width));
    if (again != texts(lines)) return "not idempotent: " + show(texts(lines)) + " -> " + show(again);
  }
  return "";
}

}  // namespace

int main() {
  RolltuiUnicodeScratch* u_scratch = rolltui_u_scratch_new();

  // ---- the edge-case table -------------------------------------------------------
  expect_lines("plain words", "the quick brown fox", 10, {"the quick", "brown fox"});
  expect_lines("exact fit", "abcde fghij", 5, {"abcde", "fghij"});
  expect_lines("single line", "hello world", 80, {"hello world"});
  expect_lines("empty input: one empty line", "", 10, {""});
  expect_lines("width 1: one grapheme per line, spaces dropped", "ab c", 1, {"a", "b", "c"});
  expect_lines("width 0: one empty line per paragraph", "abc\ndef", 0, {"", ""});
  expect_lines("width 0, empty", "", 0, {""});
  expect_lines("spaces only, fitting", "   ", 5, {"   "});
  expect_lines("spaces only, overflowing: the overflow is dropped", "        ", 3, {"   "});
  expect_lines("trailing spaces at eot are kept", "a  ", 5, {"a  "});
  expect_lines("trailing newline makes no extra line", "abc\n", 10, {"abc"});
  expect_lines("blank line between newlines survives", "abc\n\ndef", 10, {"abc", "", "def"});
  expect_lines("newline alone: one empty line", "\n", 10, {""});
  expect_lines("two newlines: two empty lines", "\n\n", 10, {"", ""});
  expect_lines("CRLF is one break", "a\r\nb", 10, {"a", "b"});
  expect_lines("CR alone breaks", "a\rb", 10, {"a", "b"});
  expect_lines("U+2028 breaks", "a\xE2\x80\xA8" "b", 10, {"a", "b"});
  expect_lines("a 300-cell token is hard-broken at grapheme boundaries", std::string(300, 'x'), 100,
               {std::string(100, 'x'), std::string(100, 'x'), std::string(100, 'x')});
  // UAX #14 allows a break after every "/" (SY) and "?" (EX), so a URL breaks at its
  // separators first, the way a browser does; only a separator-free token is
  // hard-broken.
  expect_lines("a URL breaks at its separators",
               "see https://example.com/a/very/long/path?with=query&and=more now", 20,
               {"see https://", "example.com/a/very/", "long/path?", "with=query&and=more", "now"});
  expect_lines("a hash with no opportunity is hard-broken",
               "sha deadbeefcafef00d0123456789abcdef01234567 end", 20,
               {"sha", "deadbeefcafef00d0123", "456789abcdef01234567", "end"});
  expect_lines("CJK breaks between ideographs", "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\xE7\xAC\xA6", 4,
               {"\xE4\xB8\xAD\xE6\x96\x87", "\xE5\xAD\x97\xE7\xAC\xA6"});
  expect_lines("a 2-cell ideograph at width 1 overflows rather than vanishes", "\xE4\xB8\xAD" "a", 1,
               {"\xE4\xB8\xAD", "a"});
  expect_lines("CJK at odd width leaves a cell empty rather than splitting a glyph",
               "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97", 3, {"\xE4\xB8\xAD", "\xE6\x96\x87", "\xE5\xAD\x97"});
  expect_lines("ZWJ emoji is one 2-cell grapheme", "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" "ab", 2,
               {"\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB", "ab"});
  expect_lines("combining mark stays with its base", "e\xCC\x81" "f", 1, {"e\xCC\x81", "f"});
  expect_lines("a lone combining mark is stripped", "\xCC\x81" "ab", 10, {"ab"});
  expect_lines("ZWSP is stripped but still separates", "aaa\xE2\x80\x8B" "bbb", 3, {"aaa", "bbb"});
  expect_lines("soft hyphen is stripped", "a\xC2\xAD" "b", 10, {"ab"});
  expect_lines("controls are stripped", "a\x01\x02" "b\x1B" "c", 10, {"abc"});
  expect_lines("NBSP is not a break and not dropped", "a\xC2\xA0" "b c", 3, {"a\xC2\xA0" "b", "c"});
  expect_lines("tab expands to the next multiple of 8", "a\tb", 20, {"a       b"});
  expect_lines("tab at column 8 expands to 8", "abcdefgh\tb", 20, {"abcdefgh        b"});
  expect_lines("tab width 4", "a\tb", 20, {"a   b"}, RolltuiWrapOptions{.tab_width = 4});
  expect_lines("a tab that overflows is dropped like spaces", "ab\tc", 4, {"ab", "c"});
  expect_lines("hyphen is an opportunity after it", "well-known", 5, {"well-", "known"});
  expect_lines("no break before a closing paren", "foo (bar)", 5, {"foo", "(bar)"});
  expect_lines("first indent reduces the first line", "aaaa bbbb cccc", 6, {"aaaa", "bbbb", "cccc"},
               RolltuiWrapOptions{.first_indent = 2});
  expect_lines("hanging indent reduces later lines", "aaaa bbbb cccc", 9, {"aaaa bbbb", "cccc"},
               RolltuiWrapOptions{.hanging_indent = 4});
  {
    Lines l = wrap_handoff("aaaa bbbb cccc", 9, RolltuiWrapOptions{.first_indent = 1, .hanging_indent = 4});
    check(l.size() == 3 && l[0].indent == 1 && l[1].indent == 4 && l[2].indent == 4 &&
              l[0].text == "aaaa" && l[1].text == "bbbb" && l[2].text == "cccc",
          "indents are reported per line and applied to the width");
    Lines m = wrap_handoff("abc", 3, RolltuiWrapOptions{.first_indent = 10});
    check(m.size() == 2 && m[0].indent == 2 && m[0].text == "a", "an indent >= width is clamped to width-1");
  }
  expect_lines("ambiguous width narrow by default", "\xC2\xA1\xC2\xA1" "a", 2, {"\xC2\xA1\xC2\xA1", "a"});
  expect_lines("ambiguous width wide on request", "\xC2\xA1\xC2\xA1" "a", 2, {"\xC2\xA1", "\xC2\xA1", "a"},
               RolltuiWrapOptions{.ambiguous_wide = true});
  expect_lines("invalid bytes draw as U+FFFD and count 1", "a\xFF" "b", 10, {"a\xFF" "b"});
  expect_lines("leading spaces are preserved", "  indented text", 20, {"  indented text"});
  expect_lines("leading spaces on a continuation come from the source only", "a  b", 2, {"a", "b"});
  expect_lines("many spaces then a word wider than the rest", "a       bbbbb", 6, {"a", "bbbbb"});
  {
    Lines l = wrap_handoff("ab\tc", 20);
    check(l.size() == 1 && l[0].graphemes.size() == 9 && l[0].graphemes[2].source_offset == 2 &&
              l[0].graphemes[7].source_offset == 2 && l[0].graphemes[8].source_offset == 3,
          "expanded tab spaces record the tab's source offset");
    Lines h = wrap_handoff("ab cd", 2);
    check(h.size() == 2 && !h[0].hard && h[1].hard, "soft break is not hard; end of text is");
    Lines n = wrap_handoff("ab\ncd", 10);
    check(n.size() == 2 && n[0].hard && n[1].hard, "newline-ended line is hard");
  }

  // ---- properties over a corpus ----------------------------------------------------
  const std::vector<std::string> corpus = {
      "", " ", "   ", "a", "hello world", "the quick brown fox jumps over the lazy dog",
      "supercalifragilisticexpialidocious and more",
      "https://example.com/a/very/long/path?with=query&and=more#fragment",
      "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\xE7\xAC\xA6 mixed with latin \xE4\xB8\xAD\xE6\x96\x87",
      "emoji \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB and \xF0\x9F\x87\xAF\xF0\x9F\x87\xB5 flags",
      "e\xCC\x81" "e\xCC\x81" "e\xCC\x81 combining", "tabs\there\tand\tthere", "\t\tleading tabs",
      "line one\nline two\n\nline four\r\nfive", "a\xC2\xA0" "b\xC2\xA0" "c nbsp", "!!!!!!!!!!!!!!!!!!!!!!!!!",
      "\xC2\xAD" "soft\xC2\xAD" "hyphens", "zero\xE2\x80\x8B" "width\xE2\x80\x8B" "spaces",
      "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9 arabic in logical order",
      "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7 conjunct \xE0\xA4\x95\xE0\xA4\xBF",
      "trailing spaces      ", "      leading spaces", "mixed   spaces    inside   words",
  };
  int prop_fail = 0;
  for (const std::string& s : corpus)
    for (int w = 0; w <= 48; ++w) {
      std::string err = properties(u_scratch, s, w);
      if (!err.empty() && ++prop_fail <= 20)
        check(false, "property at width " + std::to_string(w) + " on [" + s + "]: " + err);
    }
  check(prop_fail == 0, "all three properties hold over the corpus at widths 0..48 (" +
                            std::to_string(prop_fail) + " failures)");

  // ---- properties over seeded random strings ---------------------------------------
  const std::vector<std::string> atoms = {
      "a", "b", "word", " ", "  ", "\t", "\n", "-", "/", "\xE4\xB8\xAD", "\xE6\x96\x87",
      "\xF0\x9F\x98\x80", "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB", "e\xCC\x81", "\xCC\x81",
      "\xE2\x80\x8B", "\xC2\xA0", "(", ")", ".", ",", "http://x.y/", "\xC2\xA1", "\xFF", "\r\n",
      "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5", "!!!!", "\x01"};
  Rng rng{0x9E3779B97F4A7C15ULL};
  int rand_fail = 0, cases = 0;
  for (int t = 0; t < 400; ++t) {
    std::string s;
    std::size_t n = rng.below(24);
    for (std::size_t i = 0; i < n; ++i) s += atoms[rng.below(atoms.size())];
    for (int w : {0, 1, 2, 3, 5, 8, 13, 21, 40}) {
      ++cases;
      std::string err = properties(u_scratch, s, w);
      if (!err.empty() && ++rand_fail <= 20)
        check(false, "random case at width " + std::to_string(w) + ": " + err);
    }
  }
  check(rand_fail == 0, "properties hold over " + std::to_string(cases) + " seeded random cases (" +
                            std::to_string(rand_fail) + " failures)");

  // ---- THE LENT WINDOW (the Done-when) -----------------------------------
  // `wrap_borrow` is the form every draw and layout loop uses and the reason a steady frame
  // allocates nothing, so the port had to carry it across intact. What needed proving is not
  // the ANSWER — that is the same engine the whole file above already tests — it is the
  // WINDOW: the lines belong to the callee, a second window reuses the same storage, and a
  // borrow held past its window reads EMPTY rather than plausibly stale.
  //
  // `wrap_borrow` (rolltui/Wrap.hpp) is `Scratch<WrapLines>` (rolltui/Scratch.hpp) lending
  // ONE thread-local handle: wrapped into while "locked", reset back to empty when the
  // lock's scope ends. Replicated directly over one Lines below, since Scratch<T> is
  // C++-only machinery with no per-module C entry point of its own.
  {
    const std::vector<std::string> want = texts(wrap_handoff("the quick brown fox jumps", 10));
    Lines lent;
    {
      lent.wrap("the quick brown fox jumps", 10);
      check(texts(lent) == want && lent.size() == want.size(),
            "wrap_borrow lends the same lines wrap() hands over: " + show(texts(lent)));
      lent.clear();  // the lock's release, on scope exit
    }
    {  // a second window, after the first closed: same buffer, different content
      lent.wrap("a b c", 1);
      check(texts(lent) == std::vector<std::string>({"a", "b", "c"}),
            "…and a second window on the reused buffer answers for its own input");
      lent.clear();
    }
    {  // rolltui/Scratch.hpp: on release the storage is CLEARED, capacity kept
      lent.wrap("held past its own window", 8);
      lent.clear();
      check(lent.empty(), "…and a borrow read past its window is EMPTY: deterministic garbage, not stale truth");
    }
  }

  // ---- A HANDED-OVER RESULT, WRAPPED INTO AGAIN --------------------------------------
  // The C implementation carves a clone's three buffers out of the handle's OWN block (one
  // allocation for the whole result), so this is the one path where a handle must let go of
  // interior storage and start over on the heap. **Nothing in the library does it** — `wrap()`
  // hands clones out to be read — which is precisely why it is asserted here: an ownership
  // branch in C that no test reaches is the failure mode this phase exists to guard against.
  {
    Lines w = wrap_handoff("alpha beta gamma", 6);
    check(texts(w) == std::vector<std::string>({"alpha", "beta", "gamma"}), "a handed-over result reads correctly");
    w.wrap("one two three four", 8);
    check(texts(w) == texts(wrap_handoff("one two three four", 8)),
          "…and wrapping INTO it gives what a fresh wrap gives: " + show(texts(w)));
    w.wrap("x", 1);
    check(texts(w) == std::vector<std::string>({"x"}), "…and again, so the handle really did change hands cleanly");
  }

  rolltui_u_scratch_free(u_scratch);
  return report("rolltui wrap_test");
}
