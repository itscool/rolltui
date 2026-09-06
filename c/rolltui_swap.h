#ifndef ROLLTUI_C_SWAP_H
#define ROLLTUI_C_SWAP_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_swap.h — THE DOUBLE BUFFER, as the library's, because three hosts had
 * written it.
 *
 * WHY THIS EXISTS, and the rule it satisfies is one the API design already carried: *if two
 * hosts write the same wrapper, that is evidence the C API is wrong, not evidence for
 * shipping the wrapper.* Three did. Measured 2026-09-04 across `TuiFrontend.cpp`,
 * `studio.cpp` and `paint.cpp`, every one of them had:
 *
 *     Frame prev; bool have_prev = false;
 *     while (running) {
 *       Frame f = render();                                  // a fresh frame, EVERY repaint
 *       write(render_diff(have_prev ? &prev : nullptr, f));
 *       prev = std::move(f); have_prev = true;
 *     }
 *
 * Checking the three separated a MECHANICAL half from a POLICY half, and only the first is
 * the library's:
 *   - the two frames, the validity flag, the diff and the swap are IDENTICAL in all three;
 *   - the invalidation POLICY legitimately differs — studio invalidated in 2 places,
 *     `TuiFrontend` in 5 (a new arrangement, a new palette, an explicit repaint action) and
 *     paint in 0. A host knows when its own content changed wholesale and the library does
 *     not. That is `rolltui_swap_invalidate`, one call, and it stays the host's.
 *   - paint's zero is not a defect: a SIZE change is already handled inside
 *     `rolltui_render_diff` (see its header's rule 1), so only the semantic kind was ever a
 *     host's to declare.
 *
 * WHAT IT BUYS BEYOND DELETING TWELVE LINES FROM THREE FILES:
 *   1. **THE PER-FRAME LIFETIME STOPS EXISTING.** Two frames are made once and swapped;
 *      nothing in the loop owns anything, so no early return can leak and there is no
 *      destructor to replace when a host stops being C++. This was the ONLY hot RAII site in
 *      the whole library measured from a host's side — everything else a host holds is
 *      app-lifetime.
 *   2. **IT PUTS THE HOSTS ON THE PATH THE BUDGET MEASURES.** `rolltui_frame_reset` — Phase
 *      13 m5's reuse — had ZERO callers outside `budget_test`, whose scene called it with the
 *      comment "exactly as a host does". No host did: all three built a fresh frame per
 *      repaint and threw ~153 KB away (`rolltui_screen.h`'s own figure at 120x40) while the
 *      budget reported a steady frame at zero. The instrument was not wrong about what it
 *      measured; it was wrong about who else measured it. `begin` calls `reset`, so a host's
 *      repaint now lands INSIDE the budget instead of beside it.
 *
 * OWNERSHIP: the swap owns both frames for its whole life. `rolltui_swap_begin` LENDS the
 * back frame — the pointer is valid until the next `begin` or `present` on the same swap,
 * and the caller must not free it. The output `RolltuiStr` is the CALLER's, kept across
 * frames and refilled, which is the point of it being a parameter.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
