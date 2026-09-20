#ifndef ROLLTUI_C_PREVIEW_H
#define ROLLTUI_C_PREVIEW_H
/*
 * rolltui/c/rolltui_preview.h — INTERNAL: what a file looks like, drawn into a rectangle.
 *
 * The column browser (`rolltui_widget_picker.c`) owns one of these and shows it in the slot to the
 * right of the cursor when the cursor is on a file. It is a module of its own because it has
 * nothing to do with folders: it is handed a PATH and a RECT and draws what is in the file.
 *
 * WHAT IT SHOWS, decided once when the path is set:
 *   text      the file's lines, tabs expanded, a line longer than the pane cut at an ellipsis.
 *             READ WHOLE up to `ROLLTUI_PREVIEW_TEXT_LIMIT` (256 KB); a bigger file shows its
 *             first 256 KB and says so. Nothing is lazy: the line count is known the moment the
 *             file is, so the scrollbar is honest and End means the end.
 *   markdown  rendered, not listed — headings, lists, tables, code blocks, and a mermaid block as
 *             the diagram it describes. Same limit.
 *   hex       a file with a NUL in it, or mostly bytes that are not text, as offset, byte pairs
 *             and an ASCII gutter. The size is known, so nothing is read but what is on screen
 *             (a seek per frame) and a file of any size scrolls at once.
 *   empty / unreadable / not a file
 *             one line saying so, in words.
 *
 * NOTHING THE FILE HOLDS REACHES THE TERMINAL AS ITSELF. A control character, an escape, an
 * invalid byte is drawn as a visible stand-in (`␛`, `�`): a text file is data, and data that
 * moves the cursor or sets the title is a way for a file to talk to your terminal.
 *
 * THE BOUNDARY'S RULES, as everywhere in `c/`: an opaque handle the caller frees, nothing returned
 * by value, text out is a caller's `RolltuiStr` or a BORROW.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiPreview RolltuiPreview;

#define ROLLTUI_PREVIEW_TEXT_LIMIT (256 * 1024)

#define ROLLTUI_PREVIEW_NONE 0
#define ROLLTUI_PREVIEW_TEXT 1
#define ROLLTUI_PREVIEW_MARKDOWN 2
#define ROLLTUI_PREVIEW_HEX 3
#define ROLLTUI_PREVIEW_EMPTY 4
#define ROLLTUI_PREVIEW_UNREADABLE 5

RolltuiPreview* rolltui_preview_new(void);
void rolltui_preview_free(RolltuiPreview* pv); /* a no-op on NULL */

/* Points the preview at a file, and reads it. The same path again is a no-op; a different one
 * replaces everything and scrolls to the top; an empty path clears it (kind NONE). */
void rolltui_preview_set_path(RolltuiPreview* pv, const char* path, size_t len);
int rolltui_preview_kind(const RolltuiPreview* pv);
/* The reason an UNREADABLE file could not be shown; empty otherwise. A BORROW. */
const char* rolltui_preview_message(const RolltuiPreview* pv, size_t* len);

/* Draws into `r`: a head row (the file's name, and what it is) and, under it, the body. `styles`
 * is the theme's table. `focused` marks the head so a person can tell the keys are theirs. */
void rolltui_preview_draw(RolltuiPreview* pv, RolltuiFrame* f, RolltuiRect r, const RolltuiStyle* styles,
                          int ambiguous_wide, int focused);

/* Scrolling, in the units the body draws: a text line, a markdown line, a hex row. Each returns 1
 * when the view moved. `page` is what the last draw showed, less one. */
int rolltui_preview_scroll_by(RolltuiPreview* pv, long long lines);
int rolltui_preview_scroll_page(RolltuiPreview* pv, int direction);
int rolltui_preview_scroll_edge(RolltuiPreview* pv, int to_end);
int rolltui_preview_scroll_extent(const RolltuiPreview* pv, RolltuiScrollExtent* out);
int rolltui_preview_scroll_to(RolltuiPreview* pv, size_t first);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PREVIEW_H */
