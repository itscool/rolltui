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
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
/* Only what changed between `prev` and `next`. A NULL `prev`, or one whose dimensions
 * differ, repaints in full (rule 1 above). When nothing changed AND the cursor did not
 * move, nothing is appended — which is what lets an idle screen cost zero bytes. */
void rolltui_render_diff(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth,
                         RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_RENDER_H */
