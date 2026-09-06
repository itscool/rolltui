#ifndef ROLLTUI_C_WRAP_H
#define ROLLTUI_C_WRAP_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_wrap.h — THE WRAP ENGINE, as C (Phase 14 m3).
 *
 * Word wrapping over Unicode: bytes and a width in, lines out, where a line is the bytes to
 * draw plus one entry per grapheme cluster with its cell width and the byte offset it came
 * from. Every rule the engine obeys is stated in `rolltui/Wrap.hpp` and asserted case by
 * case in `rolltui/tests/wrap_test.cpp`; none of it is repeated here, because the rules are
 * the same in both languages and a second copy is a second thing to drift.
 *
 * THIS IS THE MILESTONE'S REAL QUESTION, and it is not the algorithm — it is `wrap_borrow`.
 * The library leans on CALLEE-OWNED SCRATCH, LENT FOR A BOUNDED WINDOW (rolltui/Scratch.hpp)
 * everywhere it draws, and the wrap engine is the pattern's biggest customer: `wrap` runs for
 * every row of a `rows:` window on every frame and for every entry that re-lays, and Phase 13
 * m5b took a steady frame to zero allocations by lending those lines instead of handing them
 * over. **A port that cannot carry the lend cannot carry the library.** So:
 *
 *   1. **THE LINES ARE AN OPAQUE HANDLE, and everything hangs off it.** Cells, graphemes,
 *      the decode buffers, the UAX #14 and #29 buffers, the line under construction — all of
 *      it is storage the handle owns and reuses. The C side therefore has **NO GLOBAL AND NO
 *      THREAD-LOCAL STATE AT ALL**: every buffer belongs to a handle the caller holds, which
 *      is what lets the C++ side put one handle in a `Scratch` and lend it, and another in a
 *      caller's hands, with no coordination between them and nothing to free at thread exit.
 *   2. **A LINE IS READ AS BORROWS** through `rolltui_wrap_line`, valid until the next
 *      `rolltui_wrap`, `rolltui_wrap_copy` or `rolltui_wrap_reset` on that handle. Same
 *      contract as `rolltui_frame_glyph` (m2), same contract as `Scratch` one level up, and
 *      the reason an FFI consumer would hold a handle rather than a pointer and a hope.
 *   3. **THE ANSWER GOES THROUGH OUT-PARAMS, not a returned struct** — `rolltui_frame_mark_at`'s
 *      shape exactly, and for m2's reason: Clang refuses to promise an ABI for an `extern "C"`
 *      function returning a type that is not a C++98 POD, and a caller's buffer was the rule
 *      from m1 anyway. The C++ side turns the out-params into a `Line` of `string_view` +
 *      `span` in one place, which is the same conversion `Mark` and `Cursor` already get.
 *
 * WHY `rolltui_wrap_clone` EXISTS, since an entry point nothing needs is a thing m2 deleted:
 * `wrap()` — the form whose lines must OUTLIVE the call — needs a handle of its own, and a
 * fresh handle's scratch is cold. Wrapping straight into it would pay for the decode and
 * break buffers on every call, which is roughly eight allocations a call that the lend path
 * does not pay. So `wrap()` runs the engine once in a warm handle and clones the LINES out;
 * the clone carries no scratch at all. That is the one function on this boundary that exists
 * for a cost reason rather than a semantic one, and it is said out loud here rather than
 * discovered.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
