#ifndef ROLLTUI_C_FRAME_OPS_H
#define ROLLTUI_C_FRAME_OPS_H
/*
 * rolltui/c/rolltui_frame_ops.h — INTERNAL (Phase 20 m6/m7, 2026-09-06).
 *
 * RE-CREATED, the same way `rolltui_render.h` was in m1/m3 and under the same rule: a
 * header exists because a `.c` needs a declaration from it, and one that declares nothing
 * is deleted. Phase 19 m3 deleted this file when every declaration in it was public and
 * lived in the definition; Phase 20 moved this module's operations back to INTERNAL — no
 * CONSUMER reaches them, only the studio, its editors, or a suite that tests
 * implementation — so the `.c` needs its declarations again and the rule re-creates it.
 *
 * A suite that needs one includes this header BY NAME and lists itself in
 * `ROLLTUI_INTERNAL_TESTS` (rolltui/CMakeLists.txt). */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
/* Amends every cell's style in `r` (clipped): a set colour replaces, an attribute bit is
 * OR'd in. Needs no scratch — it reads and writes styles and never looks at text. */
void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_FRAME_OPS_H */
