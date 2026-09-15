#ifndef ROLLTUI_C_WIDGET_PICKER_H
#define ROLLTUI_C_WIDGET_PICKER_H
/*
 * rolltui/c/rolltui_widget_picker.h — INTERNAL: the column browser behind the `filepicker` kind.
 *
 * The PUBLIC surface is in `rolltui/rolltui.h`: a host names `filepicker` in a layout and makes
 * the window calls (`rolltui_windows_set_picker_dir`, `_picker_event`, `_picker_selected`,
 * `_picker_dir`, `_set_picker_options`). What is below is the widget the adapter in
 * `rolltui_widget_kinds.c` wraps, reached by the library's own `.c` files and by a suite that
 * opts in by including this header by name.
 *
 * THE SHAPE. Miller columns: every column is one directory with a cursor and its own vertical
 * scroll; the column right of the focus is the preview of what the cursor is on. The columns
 * run from the file system's root down to where the picker was pointed, so a deep start shows
 * its ancestors. The horizontal scroll is ONE number — where column 0's left edge sits — and the
 * anchor rule picks it: the focused column sits one in from the right edge with its whole preview
 * beside it, sliding there in `kScrollMs`. The rest is chrome and memory: a column clipped at the
 * left edge fades toward the background (24-bit colour only), a hairline divider after every
 * column carries that column's scroll thumb, and where the cursor was in every folder is
 * remembered for the session so Left then Right lands where you were.
 *
 * WHAT IT NEVER DECIDES: what a chosen path MEANS. It records an event — taken, cancelled, a copy
 * asked for — and a host reads it. It opens nothing, prints nothing, and ends nothing.
 *
 * THE BOUNDARY'S RULES, as everywhere in `c/`: the caller owns every buffer through a handle
 * (`RolltuiPicker`); nothing is returned by value; text out is a BORROW or a caller's `RolltuiStr`.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiPicker RolltuiPicker;

/* The `picker` scope's twelve names, from the closed action table. */
const RolltuiPickerActions* rolltui_picker_default_actions(void);

RolltuiPicker* rolltui_picker_new(void);
void rolltui_picker_free(RolltuiPicker* p);

void rolltui_picker_set_options(RolltuiPicker* p, const RolltuiPickerOptions* o);
const RolltuiPickerOptions* rolltui_picker_options(const RolltuiPicker* p);
/* Walks from `/` to `path` (absolute; a relative one is taken from the working directory) with
 * every ancestor's component selected. A component that cannot be entered ends the walk with a
 * column that says why. */
void rolltui_picker_go_to(RolltuiPicker* p, const char* path, size_t len);
void rolltui_picker_focus_column(RolltuiPicker* p, size_t column); /* the i-th open column, clamped */
/* Re-reads every column in place, keeping each selection BY NAME — after an option changed. */
void rolltui_picker_reload(RolltuiPicker* p);

/* The frame clock, set before layout and before an event: 0 means headless, nothing moves. */
void rolltui_picker_set_now(RolltuiPicker* p, unsigned long long now_ms);
void rolltui_picker_layout(RolltuiPicker* p, RolltuiRect inner);
void rolltui_picker_draw(RolltuiPicker* p, RolltuiFrame* f, const RolltuiStyle* styles,
                         const RolltuiScrollbarGlyphs* glyphs, int ambiguous_wide);
/* 1 when the event was consumed. Keys are resolved in the `picker` scope of `b` against `a`. */
int rolltui_picker_handle(RolltuiPicker* p, const RolltuiEvent* e, const RolltuiBindings* b,
                          const RolltuiPickerActions* a);

/* The outcome since it was last taken: fills `out` and returns 1 exactly once per event. */
int rolltui_picker_event(RolltuiPicker* p, RolltuiPickerEvent* out);
/* What is under the cursor (the focused column's directory when it is empty). */
int rolltui_picker_selected(const RolltuiPicker* p, RolltuiStr* path, int* is_dir);
/* The focused column's directory. */
int rolltui_picker_dir(const RolltuiPicker* p, RolltuiStr* out);
/* The status line's facts; `out->error` is the caller's to release. */
void rolltui_picker_status(const RolltuiPicker* p, RolltuiPickerStatus* out);
/* How many entries the focused column hid, for a note. */
size_t rolltui_picker_hidden_count(const RolltuiPicker* p);
/* The scrollbar pair for the window's own bar: the LAST column's with dividers on (its "line to
 * the right" is the border), the focused column's without. */
int rolltui_picker_scroll_extent(const RolltuiPicker* p, RolltuiScrollExtent* out);
int rolltui_picker_scroll_to(RolltuiPicker* p, size_t first);
/* Whether a slide is in progress: the host's frame timer wants the next frame soon. */
int rolltui_picker_scrolling(const RolltuiPicker* p);
/* How many cells the last frame drew faded at the left edge — a self-test's number. */
size_t rolltui_picker_faded_cells(const RolltuiPicker* p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WIDGET_PICKER_H */
