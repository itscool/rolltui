//
// grapheme_break_test.cpp — UAX #29 extended grapheme cluster conformance: every case
// in GraphemeBreakTest.txt (Unicode 17.0.0, checked in under rolltui/ucd/), run in
// full. A failing case prints the suite's own rule annotations next to our marks.
//
// Also: the UTF-8 layer (invalid bytes become one U+FFFD each, never dropped) and the
// byte-span form `graphemes()` that the wrap engine consumes.
//
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_md_lines.h"  /* INTERNAL: this test opts in (Phase 19 m2) */
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this test opts in (Phase 19 m2) */
#include "rolltui_test.hpp"
#include "ucd_test_file.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_UCD_DIR
#error "ROLLTUI_UCD_DIR must point at rolltui/ucd"
#endif

namespace {

// The four helpers mirror the C++ shim's own bodies (rolltui/Unicode.cpp) one level
// down, over the C API directly: DecodedChar/Grapheme ARE RolltuiDecodedChar/
// RolltuiUnicodeGrapheme (Phase 14 m2's one-definition rule), so only the calls change.

std::vector<bool> grapheme_boundaries(RolltuiUnicodeScratch* scratch, const std::vector<char32_t>& cps) {
  std::vector<unsigned char> bytes(cps.size() + 1);
  rolltui_u_grapheme_boundaries(scratch, cps.data(), cps.size(), bytes.data());
  return std::vector<bool>(bytes.begin(), bytes.end());
}

RolltuiDecodedChar decode_one(std::string_view s, std::size_t pos) {
  RolltuiDecodedChar d{};
  rolltui_u_decode_one(s.data(), s.size(), pos, &d);
  return d;
}

std::vector<RolltuiDecodedChar> decode_utf8(std::string_view s) {
  std::vector<RolltuiDecodedChar> out(s.size());  // at most one scalar per byte
  const std::size_t n = rolltui_u_decode_utf8_chars(s.data(), s.size(), out.data());
  out.resize(n);
  return out;
}

std::vector<RolltuiUnicodeGrapheme> graphemes(RolltuiUnicodeScratch* scratch, std::string_view utf8) {
  std::vector<RolltuiUnicodeGrapheme> out(utf8.size());  // no more clusters than bytes
  const std::size_t n = rolltui_u_graphemes(scratch, utf8.data(), utf8.size(), false, out.data());
  out.resize(n);
  return out;
}

}  // namespace

int main() {
  RolltuiUnicodeScratch* scratch = rolltui_u_scratch_new();

  // ---- the conformance suite, in full ----
  std::vector<UcdCase> cases = read_ucd_cases(std::string(ROLLTUI_UCD_DIR) + "/GraphemeBreakTest.txt");
  check(cases.size() > 700, "GraphemeBreakTest.txt parsed (" + std::to_string(cases.size()) +
                                " cases; a truncated file would be a silent green)");
  for (const UcdCase& c : cases) {
    std::vector<bool> got = grapheme_boundaries(scratch, c.cps);
    bool ok = (got == c.breaks);
    check_quiet(ok, "GraphemeBreakTest.txt:" + std::to_string(c.line_number) + "  " +
                        cps_to_hex(c.cps) + "\n         expected " +
                        marks_to_string(c.cps, c.breaks) + "\n         got      " +
                        marks_to_string(c.cps, got) + "\n         suite:" + c.comment);
  }

  // ---- UTF-8 decoding is total and lossless in byte count ----
  {
    std::string bad = "a\xFF" "b\xC3" "c\xE2\x82" "\xF0\x9F\x98\x80" "\xED\xA0\x80" "z";
    std::vector<RolltuiDecodedChar> d = decode_utf8(bad);
    std::size_t total = 0;
    int replacements = 0;
    for (const RolltuiDecodedChar& x : d) {
      total += x.length;
      if (!x.valid) ++replacements;
    }
    check(total == bad.size(), "decode_utf8 accounts for every byte exactly once");
    // FF; C3 (lead with no continuation); E2 then 82 (a truncated 3-byte form is
    // reported one byte at a time); ED A0 80 (an encoded surrogate: the lead is
    // rejected, then each orphaned continuation byte) = 7.
    check(replacements == 7, "each malformed byte is one U+FFFD (got " +
                                 std::to_string(replacements) + ", expected 7)");
    check(d[0].cp == 'a' && d[d.size() - 1].cp == 'z', "valid neighbours survive");
    bool emoji_ok = false;
    for (const RolltuiDecodedChar& x : d) emoji_ok |= (x.cp == 0x1F600 && x.length == 4 && x.valid);
    check(emoji_ok, "a 4-byte sequence decodes to U+1F600");
    check(decode_one("\xC0\x80", 0).valid == false, "overlong C0 80 is rejected");
    check(decode_one("\xF4\x90\x80\x80", 0).valid == false, "> U+10FFFF is rejected");
  }

  // ---- graphemes(): byte spans and widths the renderer consumes ----
  {
    // "e" + combining acute, then a family ZWJ sequence, then a flag.
    std::string s = "e\xCC\x81" "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5";
    std::vector<RolltuiUnicodeGrapheme> g = graphemes(scratch, s);
    check(g.size() == 3, "three clusters (got " + std::to_string(g.size()) + ")");
    if (g.size() == 3) {
      check(g[0].offset == 0 && g[0].length == 3 && g[0].width == 1, "e + U+0301: 3 bytes, width 1");
      check(g[1].offset == 3 && g[1].length == 11 && g[1].width == 2, "woman ZWJ laptop: 11 bytes, width 2");
      check(g[2].offset == 14 && g[2].length == 8 && g[2].width == 2, "flag JP: 8 bytes, width 2");
    }
    std::size_t covered = 0;
    for (const RolltuiUnicodeGrapheme& x : g) covered += x.length;
    check(covered == s.size(), "clusters tile the input");
    check(graphemes(scratch, "").empty(), "empty input: no clusters");
    std::vector<RolltuiUnicodeGrapheme> bad = graphemes(scratch, "\xFF\xFE");
    check(bad.size() == 2 && bad[0].width == 1 && bad[1].width == 1,
          "invalid bytes are 1-cell clusters of their own, never dropped");
  }

  rolltui_u_scratch_free(scratch);
  return report("rolltui grapheme_break_test");
}
