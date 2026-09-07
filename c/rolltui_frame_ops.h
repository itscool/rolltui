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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_FRAME_OPS_H */
