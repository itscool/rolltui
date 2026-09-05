#ifndef ROLLTUI_C_MARKER_H
#define ROLLTUI_C_MARKER_H
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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* It SHORTENS rather than eating the line (Phase 12 m5). The marker writes over CONTENT
 * cells, and at 20 cells wide the full form took most of the row ("│   Ent▼ 187 more │").
 * Shortening is only safe because the scrollbar carries the proportion: the two are KEPT
 * TOGETHER on purpose — the bar is the positional signal and the marker is the
 * NON-GRAPHICAL one, which is the first thing a mono theme, a low colour depth or a
 * borderless window still has. Writes 0 bytes when there is nothing below or no room.
 *
 * `out` needs ROLLTUI_MARKER_MAX; the count is a `size_t`, so twenty digits is the bound. */
#define ROLLTUI_MARKER_MAX 40
size_t rolltui_scroll_marker_text(size_t below, int max_width, int ambiguous_wide, char* out, size_t cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MARKER_H */
