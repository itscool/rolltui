#ifndef ROLLTUI_C_DIFF_H
#define ROLLTUI_C_DIFF_H
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
#include <stddef.h>

#include "rolltui/c/rolltui_style.h" /* the role names this module's default mapping is written in */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- plain data, defined once and compiled by both languages ------------------------- */

/* One span of ONE line, as byte offsets into that line. `role` is a `rolltui::Role` value,
 * taken from the RolltuiDiffRoles the caller handed in and never invented here. */
typedef struct RolltuiDiffSpan {
  size_t begin, end; /* end exclusive */
  unsigned char role;
} RolltuiDiffSpan;

/* A line takes its whole role, or splits into line / word / line around its changed run —
 * so three is the maximum for any line, in any configuration, forever. A caller sizes its
 * output buffer from this constant and never asks first. */
#define ROLLTUI_DIFF_MAX_SPANS 3

/* The seven roles this module may emit, handed in by the caller (see the note above).
 * `file_header` is the `---`/`+++` pair, which is NOT an added or removed line; `hunk` is
 * `@@`, a position rather than a change. */
typedef struct RolltuiDiffRoles {
  unsigned char added, removed, context, file_header, hunk, added_word, removed_word;
} RolltuiDiffRoles;

/* THE MAPPING ITSELF, as a BORROW of a static table — the tenth vocabulary this phase has
 * brought home, and it arrived the way the other nine did: by converting a consumer.
 *
 * It lived in `Diff.cpp`'s anonymous namespace under this reason: *"`Role` is the styling
 * vocabulary of a layer that has not been ported, and mirroring the enum in a C header would be
 * a second definition of it."* **That was true when written and is not now.** `Role` IS ported
 * — `ROLLTUI_ROLE_LIST` in `rolltui_style.h`, whose own note reads "ONE SPELLING, and it is this
 * list. Both languages DERIVE from it" — so naming a role here mirrors nothing.
 *
 * And the tell had already fired: `rolltui/tests/markdown_test.cpp` carried a verbatim second
 * copy, and `lifetime_test`'s conversion was about to make a third before it stopped and
 * reported instead. Two consumers writing the same table means the API is wrong, not the
 * consumers. A caller that wants a DIFFERENT mapping still passes its own — this is the
 * default, not a replacement for the parameter. */
const RolltuiDiffRoles* rolltui_diff_default_roles(void);

/* The block's lines, read on demand. Returns a BORROW of line `i`, valid for the duration
 * of the call; `*len` receives its length. A zero-length line gives a valid pointer. */
typedef const char* (*RolltuiDiffLineFn)(const void* block, size_t i, size_t* len);

/* ---- working memory ------------------------------------------------------------------ */
/* The decode, boundary and token-range buffers the word-level refinement needs, owned by
 * the caller and reused across calls: one handle per thread, made once, grown to a
 * high-water mark over the first few calls and never again. One buffer per ROLE, so the
 * two sides of a pair cannot alias each other's token ranges.
 *
 * It owns its own `RolltuiUnicodeScratch` (created lazily, as the wrap engine's handle
 * does), which is what keeps this boundary at ONE handle for a caller to hold. */
typedef struct RolltuiDiffScratch RolltuiDiffScratch;
RolltuiDiffScratch* rolltui_diff_scratch_new(void);
void rolltui_diff_scratch_free(RolltuiDiffScratch* s);

/* ---- the colouriser ------------------------------------------------------------------- */

/* True for the info strings this colouriser answers to: "diff", "patch", "udiff". The
 * FENCE decides; content is never sniffed (Diff.hpp). */
int rolltui_diff_is_language(const char* lang, size_t lang_len);

/* The spans of `block[index]`, into `out` (capacity `out_cap`, at least
 * ROLLTUI_DIFF_MAX_SPANS). Returns how many were written: 0 for a language this does not
 * claim and for an index past the block. */
size_t rolltui_diff_spans(RolltuiDiffScratch* s, const char* lang, size_t lang_len, const void* block,
                          size_t line_count, RolltuiDiffLineFn line_at, size_t index,
                          const RolltuiDiffRoles* roles, RolltuiDiffSpan* out, size_t out_cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_DIFF_H */
