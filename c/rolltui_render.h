#ifndef ROLLTUI_C_RENDER_H
#define ROLLTUI_C_RENDER_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_render.h — turning a frame into bytes a terminal understands.
 *
 * `rolltui_screen.h` said these three were "NOT HERE, on purpose … porting them is its own
 * step and moves no behaviour when it happens". This is that step. They are CONSUMERS of a
 * frame rather than part of one — every one is a loop over the six primitives in
 * `rolltui_screen.h` plus the theme's SGR encoder (`rolltui_sgr`).
 *
 * THE OUTPUT RULE, and it is the boundary's rule rather than this file's: nothing is
 * returned by value from an `extern "C"` function, so every one of these FILLS A CALLER'S
 * `RolltuiStr`. That is not only ABI hygiene — it is what lets a host keep one output buffer
 * for the whole run and refill it every frame, where the C++ these replace built a fresh
 * `std::string` per repaint and threw it away. The caller owns it; these append to it and
 * never free it.
 *
 * TWO RULES THAT LIVE HERE RATHER THAN IN A CALLER, both load-bearing:
 *
 *   1. **A SIZE CHANGE REPAINTS IN FULL.** `rolltui_render_diff` compares the two frames'
 *      dimensions and falls back to a full repaint when they differ. `rolltui_screen.h`
 *      states why that belongs here: it "keeps rule 3 (a resize invalidates the baseline) a
 *      property of the code rather than of the caller". Measured 2026-09-04: of the three
 *      hosts, one invalidated on resize, one did it in five places for other reasons, and
 *      one did not invalidate at all — and all three were correct, precisely because this
 *      rule is not theirs to remember.
 *
 *   2. **THE SGR STATE IS A VALUE, NEVER A POINTER INTO A CELL.** It used to be a
 *      `const Style*` aimed at a cell and kept across loop iterations; that was correct only
 *      while `Frame::at()` returned a reference into the frame's storage, and the moment an
 *      opaque handle made it return a cell BY VALUE the pointer aimed at a destroyed
 *      temporary. Phase 14 m2 found it by DESIGNING this boundary, before a line of C
 *      existed. A style is fifteen bytes; there was never a reason for the pointer.
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
