//
// testkit_test.cpp — the module's own suite, and it belongs to neither side.
//
// THE FILE IS ITS OWN FIRST CONTROL, and that is the design rather than a flourish. It has NO
// explicit calls in `main()`: every assertion below is inside a TESTKIT_TEST body, so if
// self-registration were broken this suite would run ZERO assertions — and the zero-assertion
// guard would fail it rather than let it print ALL PASS and exit 0. The two mechanisms this
// file is here to check are wired to catch each other's absence, which is why neither needs a
// hand-run negative control to be believed.
//
// A module that carries its own test out with it is more extractable than one whose harness is
// a hand-maintained copy of another program's — which is the argument the library's own
// extraction row in `CONSIDERED.md` rests on.
//
#include <cstdio>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "testkit/testkit.hpp"

using testkit::check;

namespace {
bool g_probe_ran = false;
}

TESTKIT_TEST(a_registered_test_runs_with_no_call_in_main) {
  g_probe_ran = true;
  check(true, "this body ran, and nothing in main() called it");
}

TESTKIT_TEST(registration_is_visible_before_the_run) {
  bool found = false;
  for (const auto& [name, fn] : testkit::registered_tests()) {
    (void)fn;
    if (name == "registration_is_visible_before_the_run") found = true;
  }
  check(found, "the macro registers a test under its own name, so the list is readable");
  check(g_probe_ran,
        "…and the earlier body already ran: registration order is definition order, which is "
        "what lets one body observe another's effect");
}

TESTKIT_TEST(the_report_line_has_the_exact_shape_the_guard_parses) {
  // A COPY OF THE FORMAT WOULD PROVE NOTHING. `roll::check_counts` reads this line out of
  // ctest's log to say when a suite went green with fewer assertions in it than last time, and
  // a test written against a remembered spelling keeps passing after the real one changes. So
  // this calls the PRODUCER and checks the bytes it actually emits.
  check(testkit::report_line("suite", 0, 7) == "suite: ALL PASS (7 checks)",
        "green: `<suite>: ALL PASS (N checks)`");
  check(testkit::report_line("suite", 2, 7) == "suite: FAILURES (7 checks)",
        "red: the same shape with the same count, so a failing run is still countable");
  check(testkit::report_line("rolltui wrap_test", 0, 57) == "rolltui wrap_test: ALL PASS (57 checks)",
        "a suite name containing a space survives — 38 of the library's suites have one");
}

TESTKIT_TEST(zero_assertions_is_a_failure_and_not_a_pass) {
  // THE GUARANTEE, EXERCISED RATHER THAN DESCRIBED. A suite whose calls were dropped — by a bad
  // merge, or by a model editing main() — would otherwise print ALL PASS (0 checks) and exit 0:
  // the vacuous green one level up, where the SUITE is the thing that stopped testing.
  //
  // The counters are process-global, so this saves them, runs `testkit_report` against an empty
  // one, and puts them back. Nothing else in the process can observe the gap: report() is not
  // called again until this suite ends.
  //
  // ITS OUTPUT IS SWALLOWED, and that is not tidiness. The probe makes the real function print a
  // real `[FAIL]` line and a real FAILURES report; leaving those in a GREEN suite's log is how a
  // reader is trained that a FAIL line does not always mean a failure, which is worse than
  // anything this assertion buys. stdout goes to /dev/null for the four lines it takes.
  std::fflush(stdout);
  const int saved_out = ::dup(STDOUT_FILENO);
  const int devnull = ::open("/dev/null", O_WRONLY);
  if (devnull >= 0) ::dup2(devnull, STDOUT_FILENO);
  const int saved_fail = testkit_fail;
  const int saved_checks = testkit_checks;
  testkit_fail = 0;
  testkit_checks = 0;
  const int rc = testkit_report("a_suite_that_ran_nothing");
  const int counted = testkit_fail;
  testkit_fail = saved_fail;
  testkit_checks = saved_checks;
  std::fflush(stdout);
  if (saved_out >= 0) ::dup2(saved_out, STDOUT_FILENO);
  if (devnull >= 0) ::close(devnull);
  if (saved_out >= 0) ::close(saved_out);
  check(rc == 1, "a suite with zero assertions exits NON-ZERO");
  check(counted == 1, "…and the failure is COUNTED, so the report line says FAILURES too");
}

TESTKIT_TEST(check_quiet_counts_what_it_does_not_print) {
  // The conformance suites run tens of thousands of cases; printing every passing row would
  // bury every other suite's output in the log a human reads to find out what broke. The count
  // must not pay for that.
  const int before = testkit_checks;
  testkit::check_quiet(true, "not printed");
  testkit::check_quiet(true, "also not printed");
  // +2 for the quiet pair; this check's own increment happens AFTER its condition is evaluated.
  check(testkit_checks == before + 2, "a quiet check counts exactly as much as a loud one");
}

int main() { return testkit::report("testkit_test"); }
