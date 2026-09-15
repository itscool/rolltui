#ifndef TESTKIT_TESTCTL_H
#define TESTKIT_TESTCTL_H
/*
 * testctl.h — named control points, so a test can run a guarantee's NEGATIVE side inside the
 * same binary that runs its positive side. The shape is SQLite's SQLITE_TEST /
 * sqlite3_test_control — twenty years of the same idea, not derived here.
 *
 * WHAT THIS REPLACES: stub the guarantee by hand, rebuild, watch the named assertion FAIL,
 * restore, rebuild, watch it PASS. That method produced seven-plus false greens, and most of
 * them the same way — the control ran against a STALE binary, because "did the rebuild happen"
 * was a thing to be believed rather than asserted. A control compiled into the binary under
 * test and flipped at runtime cannot run against any other binary.
 *
 * WHY THE CORE IS C. roll's control points live in C++ headers; rolltui's must live in its C
 * sources. The flag chooses an algorithm, never the shape of the data, and "is this control on"
 * is data — so there is one table, in C, with a C++ binding over it (testctl.hpp). Two language
 * bindings to one definition is not two definitions.
 *
 * CONTRACT
 *   * At the guarantee's site: `if (testkit_ctl_on("area.what_it_breaks")) …`. The name says
 *     what turning it ON BREAKS — `hint.drop_line_text`, never `hint.line_text` — so a reader of
 *     production code sees a guarantee rather than a feature flag. The ON branch is the defect
 *     the guarantee was built against, kept as small as a real prior state of the code allows.
 *   * Without TESTKIT_CONTROL (the shipped artifacts), `testkit_ctl_on` is a MACRO that
 *     discards its argument and yields 0. That is stronger than the C++ side's constexpr-false,
 *     which leaves the literal in the translation unit for the optimizer to drop: here the name
 *     string is never an argument to anything, so it cannot survive into the artifact whatever
 *     the optimizer does. `strings` finding no control name becomes a property of the language
 *     rather than of the optimisation level.
 *   * With it defined, `on()` consults one process-wide table; `known` / `flipped` let the
 *     control test assert that every point the code REACHED was also flipped by a test, and
 *     that no name was flipped that no code asks about (a typo'd name controls nothing).
 *   * Control points live in pure and display helpers ONLY. Never in the self-edit gate: a flag
 *     that could widen it is exactly what the safety tiering forbids, and
 *     tests/test_control_test.cpp greps for it.
 *   * One first-time check is still a human's: when a control point is added, its
 *     `check_controlled` must read PASS / FAIL-expected / PASS on its first run. A middle
 *     assertion that PASSES means the control does not reach the guarantee, or the predicate is
 *     vacuous — and that is a test failure, which the manual method could not tell from a stale
 *     binary.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TESTKIT_CONTROL

/* Is `name` on? Every query registers the name as KNOWN, so the control test can see which
 * points the code under test actually reached. */
int testkit_ctl_on(const char* name);
void testkit_ctl_set(const char* name, int value);
/* Every control off. `known` and `flipped` are kept: they are the audit, not the state. */
void testkit_ctl_reset(void);
/* Fills `out` with up to `cap` interned names and returns how many exist. The pointers stay
 * valid for the life of the process — the table never forgets a name. */
size_t testkit_ctl_known(const char** out, size_t cap);
size_t testkit_ctl_flipped(const char** out, size_t cap);
/* Comma-separated names from an environment variable, for a binary that wants a control on for
 * its whole life (an A/B arm through a control build). Returns how many it turned on. */
int testkit_ctl_set_from_env(const char* var);

#else

/* The shipped build: no table, no names, no way to flip — and the name never reaches an
 * argument, so it cannot appear in the artifact. */
#define testkit_ctl_on(name) (0)

#endif /* TESTKIT_CONTROL */

#ifdef __cplusplus
}
#endif
#endif /* TESTKIT_TESTCTL_H */
