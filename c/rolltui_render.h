#ifndef ROLLTUI_C_RENDER_H
#define ROLLTUI_C_RENDER_H
/*
 * rolltui/c/rolltui_render.h — INTERNAL (Phase 20 m1/m3, 2026-09-06).
 *
 * RE-CREATED. Phase 19 m3 deleted this header under the rule "a header exists because a .c
 * needs a declaration from it; one that declares nothing is deleted" — at that point every
 * declaration here was public and lived in the definition. Phase 20 moved it
 * back to INTERNAL, so the .c needs a declaration again and the same rule re-creates the
 * file. Its subject: the diff step under `rolltui_swap_present`.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_OPT_IN`. */
/* Only what changed between `prev` and `next`. A NULL `prev`, or one whose dimensions
 * differ, repaints in full (rule 1 above). When nothing changed AND the cursor did not
 * move, nothing is appended — which is what lets an idle screen cost zero bytes. */
void rolltui_render_diff(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth,
                         RolltuiStr* out);

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
/* ========================================================================================
 * render — the grid to bytes
 * ======================================================================================== */
/* The whole frame, from a cleared screen. Appends to `out`; never clears it. */
void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_RENDER_H */
