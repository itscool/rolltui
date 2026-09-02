#pragma once
//
// rolltui_test.hpp — the whole test harness for the rolltui library. Deliberately the
// same shape as roll's tests/test_util.hpp and deliberately not that file: the
// library's tests include nothing of roll's, so that "rolltui builds and passes with
// roll's include/ absent" (phase 9 Done-when (g)) is a property of the build, not a
// promise.
//
#include <cstdio>
#include <string>

namespace rolltui_test {

inline int g_fail = 0;
inline int g_pass = 0;

inline void check(bool cond, const std::string& name) {
  std::fprintf(stdout, "  [%s] %s\n", cond ? "PASS" : "FAIL", name.c_str());
  if (cond) ++g_pass; else ++g_fail;
}

// For suites with thousands of cases: count silently, print only failures.
inline void check_quiet(bool cond, const std::string& name) {
  if (cond) { ++g_pass; return; }
  ++g_fail;
  std::fprintf(stdout, "  [FAIL] %s\n", name.c_str());
}

inline int report(const char* suite) {
  std::fprintf(stdout, "\n%s: %d passed, %d failed — %s\n", suite, g_pass, g_fail,
               g_fail == 0 ? "ALL PASS" : "FAILURES");
  return g_fail == 0 ? 0 : 1;
}

}  // namespace rolltui_test
