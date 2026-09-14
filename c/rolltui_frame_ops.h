#ifndef ROLLTUI_C_FRAME_OPS_H
#define ROLLTUI_C_FRAME_OPS_H
/*
 * rolltui/c/rolltui_frame_ops.h — INTERNAL.
 *
 * The library's own declarations for this module. The PUBLIC API is `rolltui/rolltui.h`,
 * which declares everything a consumer may call; nothing below is promised to one, so its
 * shape can change without breaking a host.
 *
 * A suite that needs an internal declaration includes this header BY NAME and lists itself
 * in `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
/* Amends every cell's style in `r` (clipped): a set colour replaces, an attribute bit is
 * OR'd in. Needs no scratch — it reads and writes styles and never looks at text. */
void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style);
/* The same amendment at a STRENGTH, 0..1. Where both the cell's colour and the style's are RGB
 * the cell's is moved that fraction of the way toward the style's, so what the cells said
 * about each other — a gradient, a fade, a highlight — is still said, more quietly. Where one
 * side is not RGB there is nothing to move along: a strength of one half or more replaces, as
 * `tint` does, and less than that leaves the colour and sets the DIM attribute. 1.0 is `tint`.
 *
 * Two strengths are named here rather than at their call sites, because they are one decision:
 * the screen behind a MODAL keeps enough of its own colour that a gradient drawn into it is
 * still a gradient, and a menu with a DROPDOWN open is shaded half as far as that — it is still
 * the thing being edited, only not the thing being looked at. */
#define ROLLTUI_SHADE_MODAL 0.7
#define ROLLTUI_SHADE_DROPDOWN 0.35
void rolltui_frame_shade(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style, double strength);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_FRAME_OPS_H */
