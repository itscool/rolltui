#include "testkit/testkit.h"

#include <stdio.h>

int testkit_fail = 0;
int testkit_checks = 0;

void testkit_check(int cond, const char* name) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name ? name : "(unnamed)");
  ++testkit_checks;
  if (!cond) ++testkit_fail;
}

void testkit_check_quiet(int cond, const char* name) {
  ++testkit_checks;
  if (cond) return;
  ++testkit_fail;
  printf("  [FAIL] %s\n", name ? name : "(unnamed)");
}

void testkit_fail_unprinted(void) {
  ++testkit_checks;
  ++testkit_fail;
}

int testkit_any_failed(void) { return testkit_fail != 0; }

size_t testkit_report_line(char* buf, size_t cap, const char* suite, int fails, int checks) {
  const int n = snprintf(buf, cap, "%s: %s (%d checks)", suite,
                         fails == 0 ? "ALL PASS" : "FAILURES", checks);
  return n < 0 ? 0u : (size_t)n;
}

int testkit_report(const char* suite) {
  if (testkit_checks == 0) {
    printf("  [FAIL] %s ran ZERO assertions — a suite that tests nothing is not a suite that "
           "passed\n", suite);
    ++testkit_fail;
  }
  char line[512];
  testkit_report_line(line, sizeof line, suite, testkit_fail, testkit_checks);
  printf("\n%s\n", line);
  return testkit_fail == 0 ? 0 : 1;
}
