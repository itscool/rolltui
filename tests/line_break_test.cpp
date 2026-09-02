//
// line_break_test.cpp — UAX #14 line breaking conformance: every case in
// LineBreakTest.txt (Unicode 17.0.0, checked in under rolltui/ucd/; 19,338 cases
// counted 2026-09-01), run in full against line_break_opportunities(). The suite
// marks ÷ (opportunity) and × (none); a mandatory break (LB4/LB5) also counts as ÷.
// A failing case prints the suite's rule annotations next to our marks.
//
#include <string>
#include <vector>

#include "rolltui/Unicode.hpp"
#include "rolltui_test.hpp"
#include "ucd_test_file.hpp"

using namespace rolltui::unicode;
using namespace rolltui_test;

#ifndef ROLLTUI_UCD_DIR
#error "ROLLTUI_UCD_DIR must point at rolltui/ucd"
#endif

int main() {
  std::vector<UcdCase> cases = read_ucd_cases(std::string(ROLLTUI_UCD_DIR) + "/LineBreakTest.txt");
  check(cases.size() > 19000, "LineBreakTest.txt parsed (" + std::to_string(cases.size()) +
                                  " cases; a truncated file would be a silent green)");
  int failures_shown = 0;
  for (const UcdCase& c : cases) {
    std::vector<Break> got = line_break_opportunities(c.cps);
    std::vector<bool> marks(got.size());
    for (std::size_t i = 0; i < got.size(); ++i) marks[i] = (got[i] != Break::Prohibited);
    bool ok = (marks == c.breaks);
    if (!ok && ++failures_shown > 60) {  // keep the log readable; the count is exact
      ++g_fail;
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
    std::vector<Break> b = line_break_opportunities(s);
    check(b[2] == Break::Mandatory, "LF is a mandatory break");
    check(b[4] == Break::Allowed, "after a space is an ordinary opportunity");
    check(b[6] == Break::Mandatory, "U+2028 LINE SEPARATOR (BK) is mandatory");
    check(b[8] == Break::Prohibited && b[9] == Break::Mandatory, "CR × LF, then mandatory");
    check(b[0] == Break::Prohibited && b[s.size()] == Break::Mandatory, "sot never, eot always");
    check(line_break_opportunities({}).size() == 1, "empty text: one eot entry");
  }
  // The UTF-8 convenience form lines up with the decode.
  {
    LineBreaks lb = line_breaks("ab \xE4\xB8\xAD\xE6\x96\x87");
    check(lb.chars.size() == 5 && lb.before.size() == 6, "line_breaks(): 5 code points, 6 marks");
    check(lb.before[3] == Break::Allowed, "opportunity after the space");
    check(lb.before[4] == Break::Allowed, "opportunity between two ideographs (ID ÷ ID)");
    check(lb.before[1] == Break::Prohibited, "none inside a Latin word");
  }
  return report("rolltui line_break_test");
}
