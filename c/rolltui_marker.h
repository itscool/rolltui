#ifndef ROLLTUI_C_MARKER_H
#define ROLLTUI_C_MARKER_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_marker.h — the "▼ N more" marker's text, and nothing else.
 *
 * It had a header of its own already (`rolltui/Marker.hpp`) for one stated reason: THREE
 * callers at two layers — the transcript widget, `draw_scrolled_text`, and the markdown
 * renderer's capped code block — and "duplicating it is exactly what the ONE definition
 * note was written to prevent". Phase 15 m4 made one of those three callers C, so the note
 * required this file rather than permitting it. `Marker.hpp` is the C++ spelling over it.
 *
 * It was compiled into both configurations while a C++ implementation existed, like the span
 * store and the Unicode tables: it is not
 * an algorithm the flag chooses between, it is a rule with exactly one definition.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
