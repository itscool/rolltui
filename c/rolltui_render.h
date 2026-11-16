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
/* The same with the frame's row 0 drawn on screen row `origin`: an inline region's top. With
 * `ground`, a cell whose background is the frame's fill (`rolltui_frame_ground`) is drawn on the
 * terminal's own background: an inline region stands in the person's terminal, not on a screen it
 * painted. */
void rolltui_render_diff_at(const RolltuiFrame* prev, const RolltuiFrame* next, unsigned char depth, int origin, int ground,
                            RolltuiStr* out);
void rolltui_render_full_at(const RolltuiFrame* next, unsigned char depth, int origin, int ground, RolltuiStr* out);
/* `rolltui_swap_present` with the frame drawn from screen row `origin`, and the frame `begin` lent
 * out, which the next present sends: the terminal reads its height first, to place an inline
 * region (rolltui_terminal.c). */
void rolltui_swap_present_at(RolltuiSwap* s, unsigned char depth, int origin, int ground, RolltuiStr* out);
const RolltuiFrame* rolltui_swap_pending(const RolltuiSwap* s);
/* How far along row `y` the terminal keeps anything: its last cell that is not a blank on the
 * default background or the frame's ground, plus one. Those blanks are nothing a terminal rewraps. */
int rolltui_render_row_extent(const RolltuiFrame* f, int y);
/* The frame's rows as LINES in the terminal's flow, each up to its extent and then a newline, so
 * they scroll into the history like any output: an inline region's committed rows. Returns how many
 * screen rows they take at `screen_w`, counting a row wider than it as the rows it wraps to. */
int rolltui_render_lines(const RolltuiFrame* f, unsigned char depth, int screen_w, int ground, RolltuiStr* out);

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
/* ========================================================================================
 * render — the grid to bytes
 * ======================================================================================== */
/* The whole frame: every cell of every row, nothing cleared first. Appends to `out`; never clears it. */
void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_RENDER_H */
