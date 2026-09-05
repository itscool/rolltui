#ifndef ROLLTUI_C_FRAME_OPS_H
#define ROLLTUI_C_FRAME_OPS_H
/*
 * rolltui/c/rolltui_frame_ops.h — THE THREE DRAWING LOOPS, IN C (Phase 15 m5).
 *
 * `put_text`, `fill` and `tint`: the loops every widget draws through, built out of the
 * frame's primitives (`rolltui_screen.h`) and the Unicode module's cluster walk. They were
 * three methods on `rolltui::Frame` in `Screen.cpp`, which is the SHIM and therefore C++ in
 * both configurations — fine while only C++ drew, and impossible the moment the layout and
 * the widgets became C. They are here, and were compiled into both configurations for the same
 * reason `rolltui_md_lines.c` is: one implementation of "write this text into that row",
 * not one per language.
 *
 * ---- THE SCRATCH IS A HANDLE, AND THAT IS THE POINT ------------------------------------
 *
 * `Frame::put_text` kept its cluster buffer in a `static thread_local`, which is Phase 14's
 * design lens exactly: **nobody decided the buffer's lifetime — `thread_local` made it not
 * need one, so no one was ever asked who owned it.** CLAUDE.md's strategy 3, as amended on
 * 2026-09-04, says what to do instead: *"this function needs somewhere to work" is not a new
 * strategy, it is a missing handle.* So every function here takes a `RolltuiDrawScratch*`
 * the CALLER owns, and the C++ shim owns exactly one per thread through `ThreadHandle`
 * (`rolltui/Scratch.hpp`) — one owner, named, released at thread exit and at
 * `release_thread()`.
 *
 * One buffer per ROLE inside it, so a function that calls another cannot alias its own
 * caller's scratch: the cluster array and the Unicode module's own working memory are two
 * fields and not one.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiDrawScratch RolltuiDrawScratch;
RolltuiDrawScratch* rolltui_draw_scratch_new(void);
void rolltui_draw_scratch_free(RolltuiDrawScratch* s); /* a no-op on NULL */

/* Writes `utf8` at (x, y), cluster by cluster, stopping at `max_cells`, at the frame's right
 * edge, or before a wide glyph that would be cut in half. Returns the cells used. */
int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link);

/* Fills `r` (clipped) with a repeated grapheme — a space when `glyph` is NULL or has no
 * width. */
void rolltui_frame_fill(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, RolltuiStyle style,
                        const char* glyph, size_t glyph_len);

/* Amends every cell's style in `r` (clipped): a set colour replaces, an attribute bit is
 * OR'd in. Needs no scratch — it reads and writes styles and never looks at text. */
void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_FRAME_OPS_H */
