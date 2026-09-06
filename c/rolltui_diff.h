#ifndef ROLLTUI_C_DIFF_H
#define ROLLTUI_C_DIFF_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_diff.h — THE UNIFIED-DIFF COLOURISER, as C (Phase 15 m2).
 *
 * Three rules on a line's first byte, plus a word-level refinement inside a changed PAIR.
 * Every rule, and the reasoning behind each, is stated in `rolltui/Diff.hpp` and asserted
 * case by case in `rolltui/tests/markdown_test.cpp`; none of it is repeated here, because
 * the rules are the same in both languages and a second copy is a second thing to drift.
 *
 * THIS IS THE LOW-OWNERSHIP HALF OF m2, and it is here to be compared against the other
 * half. `plan/phase-15.md` predicts the port's cost tracks how much of a module is
 * ownership work: Phase 14 measured +12% for Unicode (dense rule logic) against +55% for
 * Wrap (almost entirely buffer lifetime). This module is a pure function over two strings
 * with one array of token ranges in the middle, so it should land near Unicode; `Effects`
 * is a registry, a process-wide retainer and a stacking applier, so it should land near
 * Wrap. Two modules in one milestone is what makes that a measurement rather than a guess.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14 and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER**, working memory included, through a handle
 *      (`RolltuiDiffScratch`). The result's bound is known without asking: one line yields
 *      at most ROLLTUI_DIFF_MAX_SPANS spans, ever.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function.
 *   3. **THE LINES ARE READ, NEVER COPIED.** The block is whatever the caller holds — a
 *      `std::span<const std::string>` on the C++ side — and it is reached through one
 *      accessor, so nothing here owns a line and nothing builds an index of them. That
 *      also keeps the cost linear: this function is called once per line of a block, and
 *      flattening the block into a (pointer, length) array at each call would make a
 *      100-line diff a 10,000-entry copy for no reason.
 *
 * **ROLES CROSS AS DATA, and the C names no role at all.** `rolltui::Role` is the styling
 * vocabulary of a layer that has not been ported (Style.hpp, Theme.hpp), and the honest
 * way to reach it from here is not to mirror the enum — a mirror is a second definition
 * and this project's most repeated failure shape — but to be HANDED the seven roles this
 * module can emit. The mapping lives in exactly one place, `Diff.cpp`, beside the table in
 * `Diff.hpp` that already states it, and `markdown_test`'s per-line-kind table is its
 * oracle: swap two fields and it fails.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_style.h" /* the role names this module's default mapping is written in */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
