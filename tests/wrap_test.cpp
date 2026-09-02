//
// wrap_test.cpp — the wrap engine (rolltui/Wrap.hpp): the edge-case table from
// plan/phase-9.md (CJK, ZWJ emoji, combining marks, a 300-cell URL, tabs, width 1,
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
#include <string>
#include <vector>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

std::vector<std::string> texts(const std::vector<Line>& lines) {
  std::vector<std::string> v;
  for (const Line& l : lines) v.push_back(l.text);
  return v;
}

std::string show(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += "[" + x + "]";
  return s;
}

// Assert exact lines. The expected list is written as the drawn bytes per line.
void expect_lines(const std::string& name, std::string_view input, int width,
                  const std::vector<std::string>& want, const WrapOptions& opt = {}) {
  std::vector<std::string> got = texts(wrap(input, width, opt));
  check(got == want, name + "  want " + show(want) + "  got " + show(got));
}

// Graphemes of a string as byte strings (via Unicode.hpp), for property 2.
std::vector<std::string> grapheme_strings(std::string_view s) {
  std::vector<std::string> v;
  for (const unicode::Grapheme& g : unicode::graphemes(s)) v.emplace_back(s.substr(g.offset, g.length));
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
std::string properties(std::string_view input, int width) {
  std::vector<Line> lines = wrap(input, width);
  if (lines.empty()) return "no lines";
  // 1. width
  for (const Line& l : lines) {
    int w = 0;
    for (const WrapGrapheme& g : l.graphemes) w += g.width;
    if (w != l.width) return "Line::width disagrees with its graphemes";
    if (width > 0 && l.width + l.indent > width && !(l.graphemes.size() == 1 && l.graphemes[0].width > width))
      return "line exceeds width " + std::to_string(width) + ": [" + l.text + "]";
    if (width <= 0 && !l.graphemes.empty()) return "width<=0 drew something";
  }
  // 2. every grapheme once, in order, minus dropped spaces; drawn width-0 never
  if (width > 0) {
    std::vector<std::string> src;
    for (const std::string& g : grapheme_strings(input)) {
      if (unicode::display_width(g) == 0 && g != "\t") continue;  // stripped
      src.push_back(g);
    }
    std::vector<std::string> drawn;
    for (const Line& l : lines)
      for (const WrapGrapheme& g : l.graphemes) drawn.push_back(l.text.substr(g.offset, g.length));
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
        return "soft-broken line ends in a space: [" + lines[i].text + "]";
  }
  // 3. idempotence: every drawn line newline-terminated (a trailing newline makes no
  //    extra line, so this is the exact inverse of the line convention)
  if (width > 0) {
    std::string joined;
    for (const Line& l : lines) joined += l.text + '\n';
    std::vector<std::string> again = texts(wrap(joined, width));
    if (again != texts(lines)) return "not idempotent: " + show(texts(lines)) + " -> " + show(again);
  }
  return "";
}

}  // namespace

int main() {
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
  expect_lines("tab width 4", "a\tb", 20, {"a   b"}, WrapOptions{.tab_width = 4});
  expect_lines("a tab that overflows is dropped like spaces", "ab\tc", 4, {"ab", "c"});
  expect_lines("hyphen is an opportunity after it", "well-known", 5, {"well-", "known"});
  expect_lines("no break before a closing paren", "foo (bar)", 5, {"foo", "(bar)"});
  expect_lines("first indent reduces the first line", "aaaa bbbb cccc", 6, {"aaaa", "bbbb", "cccc"},
               WrapOptions{.first_indent = 2});
  expect_lines("hanging indent reduces later lines", "aaaa bbbb cccc", 9, {"aaaa bbbb", "cccc"},
               WrapOptions{.hanging_indent = 4});
  {
    std::vector<Line> l = wrap("aaaa bbbb cccc", 9, WrapOptions{.first_indent = 1, .hanging_indent = 4});
    check(l.size() == 3 && l[0].indent == 1 && l[1].indent == 4 && l[2].indent == 4 &&
              l[0].text == "aaaa" && l[1].text == "bbbb" && l[2].text == "cccc",
          "indents are reported per line and applied to the width");
    std::vector<Line> m = wrap("abc", 3, WrapOptions{.first_indent = 10});
    check(m.size() == 2 && m[0].indent == 2 && m[0].text == "a", "an indent >= width is clamped to width-1");
  }
  expect_lines("ambiguous width narrow by default", "\xC2\xA1\xC2\xA1" "a", 2, {"\xC2\xA1\xC2\xA1", "a"});
  expect_lines("ambiguous width wide on request", "\xC2\xA1\xC2\xA1" "a", 2, {"\xC2\xA1", "\xC2\xA1", "a"},
               WrapOptions{.ambiguous_wide = true});
  expect_lines("invalid bytes draw as U+FFFD and count 1", "a\xFF" "b", 10, {"a\xFF" "b"});
  expect_lines("leading spaces are preserved", "  indented text", 20, {"  indented text"});
  expect_lines("leading spaces on a continuation come from the source only", "a  b", 2, {"a", "b"});
  expect_lines("many spaces then a word wider than the rest", "a       bbbbb", 6, {"a", "bbbbb"});
  {
    std::vector<Line> l = wrap("ab\tc", 20);
    check(l.size() == 1 && l[0].graphemes.size() == 9 && l[0].graphemes[2].source_offset == 2 &&
              l[0].graphemes[7].source_offset == 2 && l[0].graphemes[8].source_offset == 3,
          "expanded tab spaces record the tab's source offset");
    std::vector<Line> h = wrap("ab cd", 2);
    check(h.size() == 2 && !h[0].hard && h[1].hard, "soft break is not hard; end of text is");
    std::vector<Line> n = wrap("ab\ncd", 10);
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
      std::string err = properties(s, w);
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
      std::string err = properties(s, w);
      if (!err.empty() && ++rand_fail <= 20)
        check(false, "random case at width " + std::to_string(w) + ": " + err);
    }
  }
  check(rand_fail == 0, "properties hold over " + std::to_string(cases) + " seeded random cases (" +
                            std::to_string(rand_fail) + " failures)");

  return report("rolltui wrap_test");
}
