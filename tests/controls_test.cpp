//
// controls_test.cpp — the library's negative controls, as PERMANENT tests.
//
// WHAT THIS REPLACES. Twenty of these were run once each during the C port: applied to a source
// file by hand, built, observed to fail by name, and reverted. They are prose in a journal
// entry, and NOTHING RE-RUNS ANY OF THEM — so nothing would notice if one of the assertions they
// proved became vacuous tomorrow. That is the same shape as the defect they were built to catch,
// one level up: a check that has stopped checking looks exactly like one that passes.
//
// HOW A CONTROL BECOMES A TEST. The defect state lives at the guarantee's own site, behind a
// named flag (`testkit_ctl_on("area.what_it_breaks")`, testkit/testctl.h), and this suite flips
// it. `check_controlled` then asserts three things in one binary: the guarantee HOLDS with the
// control off, is GONE with it on, and holds again once restored. The middle one is the half the
// hand method could not do — a stale binary and a control that does not reach its guarantee look
// identical from outside, and this repository has seven-plus false greens from exactly that.
//
// THE FLAG NEVER REACHES THE SHIPPED LIBRARY. This binary links `rolltui_ctl`; every host, app
// and other suite links `rolltui`, where `testkit_ctl_on` is a macro that discards its argument
// and the table is not compiled at all. `rolltui-shipped-artifact-test` asserts that, and proves
// its own scanner can find a string before believing it found none.
//
// THE TWO BELOW ARE THE PATTERN, one per shape: a classification inverted (a wrong answer that
// is still a legal answer) and a reduction made a no-op (a step skipped where the output stays
// well formed). The remaining eighteen are ported into this file the same way.
//
#include <cstddef>
#include <string>
#include <vector>

#include "rolltui/c/rolltui_theme.h"  // rolltui_sgr: an internal header, the way the
                                      // library's own suites reach what no host needs.
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

namespace {

// The library's own diff classifier, reached the way a host reaches it. A local
// reimplementation would leave the control flipping a flag nothing under test reads.
const char* line_at(const void* block, size_t i, size_t* len) {
  const auto& v = *static_cast<const std::vector<std::string>*>(block);
  *len = v[i].size();
  return v[i].data();
}

RolltuiRole first_role_of(const char* line) {
  RolltuiDiffScratch* s = rolltui_diff_scratch_new();
  const std::vector<std::string> block{line};
  RolltuiDiffSpan out[ROLLTUI_DIFF_MAX_SPANS];
  const size_t n = rolltui_diff_spans(s, "diff", 4, &block, block.size(), &line_at, 0,
                                      rolltui_diff_default_roles(), out, ROLLTUI_DIFF_MAX_SPANS);
  const RolltuiRole r = (RolltuiRole)(n == 0 ? (int)ROLLTUI_ROLE_COUNT : (int)out[0].role);
  rolltui_diff_scratch_free(s);
  return r;
}

std::string sgr_of(const RolltuiStyle& style, unsigned char depth) {
  char buf[ROLLTUI_SGR_MAX];
  const size_t n = rolltui_sgr(&style, depth, buf, sizeof buf);
  return std::string(buf, n);
}

}  // namespace

TESTKIT_TEST(a_diff_line_is_classified_by_its_marker) {
  // The guarantee `markdown_test` asserts as "'+' is an added line", with the control that makes
  // it fail. The defect state is an INVERSION rather than a crash on purpose: every downstream
  // step still runs, every span is a legal span, and the only symptom is that the colours are
  // the other way round. Nothing in the library can notice.
  check_controlled("diff.added_and_removed_are_swapped",
                   "a '+' line is diff_added and a '-' line is diff_removed", [] {
                     return first_role_of("+added") == ROLLTUI_ROLE_DIFF_ADDED &&
                            first_role_of("-gone") == ROLLTUI_ROLE_DIFF_REMOVED;
                   });
  // The guarantee that is NOT controlled, kept beside it because it is the reason the classifier
  // tests headers first: "+++ b/x" starts with '+' and is not an added line.
  check(first_role_of("+++ b/x") != ROLLTUI_ROLE_DIFF_ADDED,
        "a file header is not an added line, whatever it starts with");
}

TESTKIT_TEST(a_mono_terminal_is_sent_no_colour) {
  // `theme_test` asserts this as "mono SGR keeps attributes only". The defect state is a step
  // SKIPPED: the reduction returns without doing anything, the colour survives, and the SGR
  // string that comes out is still perfectly well formed — so the only place it is visible is a
  // terminal that reported no colour and got some, which is the one machine nobody tests on.
  RolltuiStyle s = {};
  s.fg = RolltuiStyleColor::rgb(1, 2, 3);
  s.bg = RolltuiStyleColor::indexed(21);
  s.bold = 1;
  s.underline = 1;
  check_controlled("theme.mono_keeps_colour", "a mono downgrade keeps the attributes and drops the colour",
                   [&s] { return sgr_of(s, ROLLTUI_DEPTH_MONO) == "\x1b[0;1;4m"; });
  check(sgr_of(s, ROLLTUI_DEPTH_TRUECOLOR) != sgr_of(s, ROLLTUI_DEPTH_MONO),
        "…and the same style at full depth is a different string, so the control above is not "
        "asserting that the two depths agree");
}

int main() { return testkit::report("rolltui_controls_test"); }
