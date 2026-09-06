#ifndef ROLLTUI_C_SCREEN_H
#define ROLLTUI_C_SCREEN_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_screen.h — THE FRAME, as C (Phase 14 m2).
 *
 * The cell grid: what a widget draws into and what the diff reads. This is the informative
 * half of the experiment, because it is the part where C++ was doing real work — a Frame
 * OWNS three things (its cells, its interned links, its spilled glyphs), and the C++
 * version expressed that with a destructor nobody had to think about.
 *
 * THE BOUNDARY'S RULES, decided here and applying to everything after:
 *
 *   1. **ONE DEFINITION. The C++ types ARE these structs.** `rolltui::Cell` is a `using`
 *      alias for `RolltuiCell` (and `Style`/`Color` for the two in rolltui_style.h), and
 *      the methods C++ wants live in `#ifdef __cplusplus` blocks inside the struct — the
 *      standard dual-language shape.
 *      **The first draft of this file said the opposite** (two types, converted at the
 *      seam) on the grounds that a `reinterpret_cast` between "layout-compatible" types is
 *      a silent wrong answer. That is true, and the conclusion did not follow: the fix for
 *      a bad cast is not a hand-written CONVERSION, it is having nothing to convert. Two
 *      definitions of the same data plus a conversion function is a second place to be
 *      wrong — swap `index` and `r`, forget `dim`, and you get a plausible frame and no
 *      error — which is the same failure with more code. **One definition removes the
 *      cast, the conversion, the drift and the per-access copy at once.**
 *   2. **THE FRAME IS AN OPAQUE HANDLE.** Created, cloned, freed. The C++ `Frame` holds one
 *      and does the RAII; C callers do it by hand, which is the trade the experiment is
 *      here to price.
 *   3. **NO ALLOCATION IS HIDDEN.** `rolltui_frame_reset` reuses everything it can, exactly
 *      as Phase 13 m5 made it — a steady frame must still allocate NOTHING with this
 *      implementation linked. That seam (`rolltui_impl_name`) was deleted on 2026-09-04 with
 *      the C++ implementations it existed to tell apart.
 *   4. **TEXT OUT IS A BORROW WITH A STATED WINDOW.** `rolltui_frame_glyph` returns a
 *      pointer into the frame, valid until the next call that mutates that cell. It is the
 *      same contract `Scratch` enforces one level up, and it is why a handle — not a raw
 *      pointer with a hope — is what an FFI consumer would hold.
 *
 * NOT HERE, on purpose: `render_diff` / `render_full` / `frame_to_text`, and `put_text` /
 * `fill` / `tint`. They are CONSUMERS of a frame rather than part of one — every one of
 * them is a loop over the six primitives below, and two of them need the theme's SGR
 * encoder or the Unicode segmenter besides. They read through the accessors here in either
 * configuration, so porting them is its own step and moves no behaviour when it happens.
 *
 * WHAT THE WIRING CHANGED, recorded because an API change the PORT FORCED is evidence for
 * m6's verdict in a way one it merely chose is not:
 *   - `Frame::marks()` returned `const std::vector<Mark>&`, which nothing on this side can
 *     supply without materialising a vector — an allocation, on a path Phase 13 took to
 *     zero. It is `rolltui_frame_mark_count` + `rolltui_frame_mark_at` now, and the two
 *     range-for loops in `Effects.cpp` are index loops.
 *   - `Frame::at()` returns a Cell BY VALUE, which an opaque handle forces. That dangled a
 *     `const Style*` `render_diff` was keeping across loop iterations — a defect found in
 *     the C++ before a line of it was ported.
 *   - `rolltui_frame_clear_marks` was declared in the design draft and is gone: nothing
 *     calls it, and an entry point no host uses would inflate m6's count of the API surface
 *     with something that was never really part of it.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif
void rolltui_frame_mark_at(const RolltuiFrame* f, size_t i, int* x, int* y, int* cells,
                           int* state, unsigned long long* since_ms, double* fraction);



/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
RolltuiFrame* rolltui_frame_clone(const RolltuiFrame* src);
void rolltui_frame_reset(RolltuiFrame* f, int w, int h, RolltuiStyle fill);
void rolltui_frame_clear(RolltuiFrame* f, RolltuiStyle fill);
void rolltui_frame_cell(const RolltuiFrame* f, int x, int y, RolltuiCell* out);
void rolltui_frame_set_style(RolltuiFrame* f, int x, int y, RolltuiStyle s);
/* Writes one grapheme of `cells` (1 or 2) at (x, y); returns the cells it occupied. */
int rolltui_frame_put(RolltuiFrame* f, int x, int y, const char* glyph, size_t glyph_len,
                      int cells, RolltuiStyle s, unsigned int link);
/* The cell's grapheme: a BORROW into the frame, valid until that cell is written again.
 * `*len` receives the byte count. Never NULL; a continuation cell gives length 0. */
const char* rolltui_frame_glyph(const RolltuiFrame* f, int x, int y, size_t* len);
/* ---- the link table --------------------------------------------------------------- */
/* Interns a URL for this frame; the same URL gets the same id. 0 for an empty URL. */
unsigned int rolltui_frame_link_id(RolltuiFrame* f, const char* url, size_t url_len);
/* A BORROW, valid until the next reset. Empty for id 0 or an unknown id. */
const char* rolltui_frame_link(const RolltuiFrame* f, unsigned int id, size_t* len);
void rolltui_frame_cursor(const RolltuiFrame* f, int* x, int* y, int* visible);
/* ---- equality ---------------------------------------------------------------------- */
/* What the frame SHOWS, not what it is holding on to: retained link/spill capacity past a
 * reset is not compared (Phase 13 m5b found that the hard way). */
int rolltui_frame_equal(const RolltuiFrame* a, const RolltuiFrame* b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_SCREEN_H */
