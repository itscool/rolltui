//
// line_break_test.cpp — UAX #14 line breaking conformance: every case in
// LineBreakTest.txt (Unicode 17.0.0, checked in under rolltui/ucd/; 19,338 cases
// counted), run in full against line_break_opportunities(). The suite
// marks ÷ (opportunity) and × (none); a mandatory break (LB4/LB5) also counts as ÷.
// A failing case prints the suite's rule annotations next to our marks.
//
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"
#include "ucd_test_file.hpp"

using namespace rolltui_test;
using namespace testkit;

#ifndef ROLLTUI_UCD_DIR
#error "ROLLTUI_UCD_DIR must point at rolltui/ucd"
#endif

namespace {

// line_break_opportunities/line_breaks mirror the C++ shim's own bodies
// (rolltui/Unicode.cpp) one level down, over the C API directly. `Break`'s three
// values are the ROLLTUI_BREAK_* constants (rolltui/c/rolltui_unicode.h); a plain
// unsigned char IS the byte array the boundary fills, so there is no conversion.
std::vector<unsigned char> line_break_opportunities(RolltuiUnicodeScratch* scratch, const std::vector<char32_t>& cps) {
  std::vector<unsigned char> out(cps.size() + 1);
  rolltui_u_line_break_opportunities(scratch, cps.data(), cps.size(), out.data());
  return out;
}

struct LineBreaks {
  std::vector<RolltuiDecodedChar> chars;
  std::vector<unsigned char> before;  // before[i] for chars[i]; before[chars.size()] is eot
};

LineBreaks line_breaks(RolltuiUnicodeScratch* scratch, std::string_view utf8) {
  LineBreaks r;
  r.chars.resize(utf8.size());  // at most one scalar per byte
  const std::size_t n = rolltui_u_decode_utf8_chars(utf8.data(), utf8.size(), r.chars.data());
  r.chars.resize(n);
  std::vector<char32_t> cps(r.chars.size());
  for (std::size_t i = 0; i < cps.size(); ++i) cps[i] = r.chars[i].cp;
  r.before = line_break_opportunities(scratch, cps);
  return r;
}

}  // namespace

int main() {
  RolltuiUnicodeScratch* scratch = rolltui_u_scratch_new();

  std::vector<UcdCase> cases = read_ucd_cases(std::string(ROLLTUI_UCD_DIR) + "/LineBreakTest.txt");
  check(cases.size() > 19000, "LineBreakTest.txt parsed (" + std::to_string(cases.size()) +
                                  " cases; a truncated file would be a silent green)");
  int failures_shown = 0;
  for (const UcdCase& c : cases) {
    std::vector<unsigned char> got = line_break_opportunities(scratch, c.cps);
    std::vector<bool> marks(got.size());
    for (std::size_t i = 0; i < got.size(); ++i) marks[i] = (got[i] != ROLLTUI_BREAK_PROHIBITED);
    bool ok = (marks == c.breaks);
    if (!ok && ++failures_shown > 60) {  // keep the log readable; the count is exact
      fail_unprinted();
      continue;
    }
    check_quiet(ok, "LineBreakTest.txt:" + std::to_string(c.line_number) + "  " +
                        cps_to_hex(c.cps) + "\n         expected " +
                        marks_to_string(c.cps, c.breaks) + "\n         got      " +
                        marks_to_string(c.cps, marks) + "\n         suite:" + c.comment);
  }

  // Mandatory vs allowed is not something the suite distinguishes; assert it here.
  {
    std::vector<char32_t> s = {'a', '\n', 'b', ' ', 'c', 0x2028, 'd', '\r', '\n', 'e'};
    std::vector<unsigned char> b = line_break_opportunities(scratch, s);
    check(b[2] == ROLLTUI_BREAK_MANDATORY, "LF is a mandatory break");
    check(b[4] == ROLLTUI_BREAK_ALLOWED, "after a space is an ordinary opportunity");
    check(b[6] == ROLLTUI_BREAK_MANDATORY, "U+2028 LINE SEPARATOR (BK) is mandatory");
    check(b[8] == ROLLTUI_BREAK_PROHIBITED && b[9] == ROLLTUI_BREAK_MANDATORY, "CR × LF, then mandatory");
    check(b[0] == ROLLTUI_BREAK_PROHIBITED && b[s.size()] == ROLLTUI_BREAK_MANDATORY, "sot never, eot always");
    check(line_break_opportunities(scratch, {}).size() == 1, "empty text: one eot entry");
  }
  // The UTF-8 convenience form lines up with the decode.
  {
    LineBreaks lb = line_breaks(scratch, "ab \xE4\xB8\xAD\xE6\x96\x87");
    check(lb.chars.size() == 5 && lb.before.size() == 6, "line_breaks(): 5 code points, 6 marks");
    check(lb.before[3] == ROLLTUI_BREAK_ALLOWED, "opportunity after the space");
    check(lb.before[4] == ROLLTUI_BREAK_ALLOWED, "opportunity between two ideographs (ID ÷ ID)");
    check(lb.before[1] == ROLLTUI_BREAK_PROHIBITED, "none inside a Latin word");
  }
  rolltui_u_scratch_free(scratch);
  return report("rolltui_line_break_test");
}
