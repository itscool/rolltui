//
// word_break_test.cpp — UAX #29 word boundary conformance: every case in
// WordBreakTest.txt (Unicode 17.0.0, checked in under rolltui/ucd/), run in full, plus
// the byte-range form `word_range` a double-click uses and the escape-sequence
// stripper that keeps model output from ever being terminal input.
//
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this test opts in (Phase 19 m2) */
#include "rolltui_test.hpp"
#include "ucd_test_file.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_UCD_DIR
#error "ROLLTUI_UCD_DIR must point at rolltui/ucd"
#endif

namespace {

// word_boundaries/word_range/strip_escape_sequences mirror the C++ shim's own bodies
// (rolltui/Unicode.cpp) one level down, over the C API directly.
std::vector<bool> word_boundaries(RolltuiUnicodeScratch* scratch, const std::vector<char32_t>& cps) {
  std::vector<unsigned char> bytes(cps.size() + 1);
  rolltui_u_word_boundaries(scratch, cps.data(), cps.size(), bytes.data());
  return std::vector<bool>(bytes.begin(), bytes.end());
}

struct ByteRange {
  std::size_t begin = 0, end = 0;
};

ByteRange word_range(RolltuiUnicodeScratch* scratch, std::string_view utf8, std::size_t offset) {
  ByteRange r;
  rolltui_u_word_range(scratch, utf8.data(), utf8.size(), offset, &r.begin, &r.end);
  return r;
}

std::string strip(std::string_view text) {
  std::string out(text.size(), '\0');
  const std::size_t n = rolltui_u_strip_escape_sequences(text.data(), text.size(), out.data());
  out.resize(n);
  return out;
}

}  // namespace

int main() {
  RolltuiUnicodeScratch* scratch = rolltui_u_scratch_new();

  // ---- the conformance suite, in full ----
  std::vector<UcdCase> cases = read_ucd_cases(std::string(ROLLTUI_UCD_DIR) + "/WordBreakTest.txt");
  check(cases.size() > 1900, "WordBreakTest.txt parsed (" + std::to_string(cases.size()) +
                                 " cases; a truncated file would be a silent green)");
  for (const UcdCase& c : cases) {
    std::vector<bool> got = word_boundaries(scratch, c.cps);
    check_quiet(got == c.breaks, "WordBreakTest.txt:" + std::to_string(c.line_number) + "  " +
                                     cps_to_hex(c.cps) + "\n         expected " +
                                     marks_to_string(c.cps, c.breaks) + "\n         got      " +
                                     marks_to_string(c.cps, got) + "\n         suite:" + c.comment);
  }

  // ---- word_range: the double-click ----
  {
    const std::string s = "hello, wide-world  caf\xC3\xA9 42.5x";
    auto word = [&](std::size_t off) { ByteRange r = word_range(scratch, s, off); return s.substr(r.begin, r.end - r.begin); };
    check(word(0) == "hello" && word(4) == "hello", "a click anywhere in a word selects it");
    check(word(5) == ",", "punctuation is a word of its own (WB999)");
    check(word(6) == " ", "a single space is its own run");
    check(word(7) == "wide" && word(12) == "world", "a hyphen splits words (it is neither MidLetter nor ExtendNumLet)");
    check(word(17) == "  ", "consecutive spaces are one run (WB3d)");
    check(word(19) == "caf\xC3\xA9" && word(22) == "caf\xC3\xA9", "a multi-byte letter belongs to its word; offsets are bytes");
    check(word(24) == " " && word(25) == "42.5x" && word(28) == "42.5x", "digits, a MidNumLet and a letter stay together (WB11/WB12/WB10)");
    check(word_range(scratch, s, s.size()).begin == s.size(), "an offset at the end selects nothing");
    check(word_range(scratch, "", 0).begin == 0 && word_range(scratch, "", 0).end == 0, "empty input");
    ByteRange u = word_range(scratch, "a_b c", 1);
    check(u.begin == 0 && u.end == 3, "underscore is ExtendNumLet: a_b is one word (WB13a/b)");
  }

  // ---- strip_escape_sequences ----
  {
    check(strip("plain \xC3\xA9 text\n") == "plain \xC3\xA9 text\n", "text without escapes passes byte for byte");
    check(strip("a\x1b[31mred\x1b[0mb") == "aredb", "CSI colour sequences are removed whole (not just the ESC)");
    check(strip("x\x1b]0;title\x07y") == "xy", "OSC terminated by BEL is removed with its payload");
    check(strip("x\x1b]8;;http://e\x1b\\link\x1b]8;;\x1b\\y") == "xlinky", "OSC 8 hyperlinks: the wrapping sequences go, the text stays");
    check(strip("x\x1bPq#0;2;0;0;0\x1b\\y") == "xy", "a DCS string is removed to its ST");
    check(strip("x\x1b(By") == "xy", "ESC + intermediate + final (charset designation) is removed");
    check(strip("x\x1b" "7y\x1b" "8z") == "xyz", "two-byte ESC sequences (save/restore cursor) are removed");
    check(strip("x\x1b") == "x", "a trailing lone ESC is dropped");
    check(strip("x\x1b\ny") == "x\ny", "ESC before a control: only the ESC goes");
    check(strip("x\xC2\x9B" "2Jy") == "xy", "the 8-bit CSI (U+009B) form is removed too");
    check(strip("x\x1b]unterminated") == "x", "an unterminated string sequence swallows to the end (never renders as text)");
    check(strip("\xFF\xFE ok") == "\xFF\xFE ok", "invalid UTF-8 passes through untouched (the decoder shows U+FFFD later)");
  }

  rolltui_u_scratch_free(scratch);
  return report("rolltui word_break_test");
}
