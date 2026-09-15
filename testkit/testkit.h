#ifndef TESTKIT_TESTKIT_H
#define TESTKIT_TESTKIT_H
/*
 * testkit.h — the assertion counters and the report line, in C, shared by every suite in
 * this repository.
 *
 * WHY C AND NOT C++, when almost every caller is C++: the pure-C consumer
 * (rolltui/tests/c_consumer_test.c) is a suite too, and it had written its own fifteen-line
 * harness because it could not include a C++ one. That was a THIRD copy of the same four
 * ideas. The counters and the report line are DATA and a format, not an algorithm, so they
 * live where every caller can reach them and the C++ layer (testkit.hpp) is a binding rather
 * than a second implementation.
 *
 * TESTKIT BELONGS TO NEITHER SIDE. roll depends on it and so does rolltui; a shared LEAF that
 * both depend on is not an upward dependency, which is what lets `rolltui` keep the property
 * that it builds with roll's include/ and src/ deleted (asserted by rolltui-boundary-test).
 * It must therefore never include a header from either.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How many assertions actually RAN, and how many failed. `checks` is parsed back out of
 * ctest's LastTest.log by check_counts (include/GatedToolExecutor.hpp) so `run_tests` can say
 * when a suite went green with fewer assertions in it than last time: ctest counts BINARIES,
 * so deleting one caller's check() leaves the binary count intact and only this number moves. */
extern int testkit_fail;
extern int testkit_checks;

/* Prints `  [PASS] name` / `  [FAIL] name` and counts. */
void testkit_check(int cond, const char* name);

/* For suites with thousands of cases: counts silently, prints only failures. A conformance
 * suite that printed 19,338 passing lines would bury every other suite's output in a log a
 * human reads to find out what broke. */
void testkit_check_quiet(int cond, const char* name);

/* THE EXACT SHAPE, not decoration: `<suite>: ALL PASS (N checks)` / `<suite>: FAILURES (N
 * checks)`. `roll::check_counts` parses this out of ctest's log; change the wording here and
 * that guard stops finding any counts. Written by a FUNCTION rather than formatted inline so a
 * test for the PARSER can call the PRODUCER — a test written against a copy of this format
 * would keep passing after the format changed, which is the one failure that pairing rules out.
 * Writes at most `cap` bytes including the terminator and returns the length it wanted. */
size_t testkit_report_line(char* buf, size_t cap, const char* suite, int fails, int checks);

/* Count a failure WITHOUT printing it. For a conformance suite that has already shown enough
 * failing rows to be actionable and would otherwise bury every other suite's output in a log a
 * human reads to find out what broke: the printed rows are capped, the COUNT stays exact. */
void testkit_fail_unprinted(void);

/* Has anything failed so far? For a suite that keeps its scratch tree on failure and removes it
 * on success — evidence is worth more than tidiness. Reading the counter directly is the same
 * question asked in a way that ties the caller to a variable rather than to a fact. */
int testkit_any_failed(void);

/* Prints the report line and returns the process exit status: 0 green, 1 red.
 *
 * ZERO ASSERTIONS IS NOT SUCCESS. A suite whose calls were dropped — by a bad merge, or by a
 * model editing main() — would otherwise print ALL PASS (0 checks) and exit 0: the vacuous
 * green one level up, where the SUITE is the thing that stopped testing. That is counted as a
 * failure here, in the one place every suite passes through. */
int testkit_report(const char* suite);

#ifdef __cplusplus
}
#endif
#endif /* TESTKIT_TESTKIT_H */
