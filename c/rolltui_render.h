#ifndef ROLLTUI_C_RENDER_H
#define ROLLTUI_C_RENDER_H
/*
 * rolltui/c/rolltui_render.h — INTERNAL.
 *
 * A header exists because a `.c` needs a declaration from it. One that declares nothing is a
 * file with no reason and is deleted; a module whose declarations go internal earns one back
 * by the same rule. Its subject: the diff step under `rolltui_swap_present`.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
/* Only what changed between `prev` and `next`. A NULL `prev`, or one whose dimensions
 * differ, repaints in full (rule 1 above). When nothing changed AND the cursor did not
 * move, nothing is appended — which is what lets an idle screen cost zero bytes. */
void rolltui_render_diff(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth,
                         RolltuiStr* out);

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
/* ========================================================================================
 * render — the grid to bytes
 * ======================================================================================== */
/* The whole frame, from a cleared screen. Appends to `out`; never clears it. */
void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_RENDER_H */
