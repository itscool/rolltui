#ifndef ROLLTUI_C_SCREEN_H
#define ROLLTUI_C_SCREEN_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_screen.h — THE FRAME, as C.
 *
 * The cell grid: what a widget draws into and what the diff reads. This is the informative
 * half of the experiment, because it is the part where C++ was doing real work — a Frame
 * OWNS three things (its cells, its interned links, its spilled glyphs), and the C++
 * version expressed that with a destructor nobody had to think about.
 *
 * THE BOUNDARY'S RULES, decided here and applying to everything after:
 *
 *   1. **ONE DEFINITION. The C++ types ARE these structs.** `rolltui::Cell` is a `using`
 *      alias for `RolltuiCellRaw` (and `Style`/`Color` for the two in rolltui_style.h), and
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
 *   3. **NO ALLOCATION IS HIDDEN.** `rolltui_frame_reset` reuses everything it can, because a
 *      steady frame must allocate NOTHING (`rolltui-budget-test` asserts that at zero).
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
 * THREE SHAPES THIS BOUNDARY REFUSES, each with the defect it refuses:
 *   - A `marks()` returning `const std::vector<Mark>&` cannot be supplied without
 *     materialising a vector, which is an allocation on a path asserted at zero. It is
 *     `rolltui_frame_mark_count` + `rolltui_frame_mark_at`, and a caller writes an index loop.
 *   - An `at()` returning a Cell BY VALUE is what an opaque handle forces, and it is the
 *     honest shape: holding a `const Style*` from one across loop iterations dangles.
 *   - There is no `rolltui_frame_clear_marks`. Nothing calls it, and an entry point no host
 *     uses inflates the API surface with something that was never part of it.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif
void rolltui_frame_mark_at(const RolltuiFrame* f, size_t i, int* x, int* y, int* cells,
                           int* state, unsigned long long* since_ms, double* fraction);

/* ---- pictures (rolltui_frame_image) ----------------------------------------------------------------
 * A picture drawn by a protocol the terminal draws outside the cell grid, or one it must be sent first, is recorded with the cells
 * it covers; the terminal reads the list at present (rolltui_terminal.c). The picture is a BORROW the host keeps alive until then. */
typedef struct RolltuiFrameImage {
  const RolltuiImage* img;
  RolltuiRect cells;
  int protocol;    /* ROLLTUI_IMAGES_* */
  int px_w, px_h;  /* the pixel box it fills */
  RolltuiStyle ground; /* the blanks under an out-of-grid picture: while every cell is one, nothing is drawn over it */
} RolltuiFrameImage;
void rolltui_frame_add_image(RolltuiFrame* f, const RolltuiImage* img, RolltuiRect cells, int protocol, int px_w, int px_h,
                             RolltuiStyle ground);
size_t rolltui_frame_image_count(const RolltuiFrame* f);
const RolltuiFrameImage* rolltui_frame_image_at(const RolltuiFrame* f, size_t i);
/* Marks the cells of `r` as unlike anything drawn, so the next diff against this frame writes them: a picture drawn outside the
 * grid is erased by writing what the cells hold now. */
void rolltui_frame_forget_rect(RolltuiFrame* f, RolltuiRect r);
/* Clips every mark to what lies OUTSIDE `r`: what a window drawn over marked cells calls, so
 * an effect never paints through it. Whole marks under it go; a mark it cuts keeps the part
 * that shows, in two pieces when the rect takes its middle. */
void rolltui_frame_unmark_rect(RolltuiFrame* f, RolltuiRect r);



/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
RolltuiFrame* rolltui_frame_clone(const RolltuiFrame* src);
void rolltui_frame_reset(RolltuiFrame* f, int w, int h, RolltuiStyle fill);
void rolltui_frame_clear(RolltuiFrame* f, RolltuiStyle fill);
void rolltui_frame_cell(const RolltuiFrame* f, int x, int y, RolltuiCellRaw* out);
void rolltui_frame_set_style(RolltuiFrame* f, int x, int y, RolltuiStyle s);
/* Writes one grapheme of `cells` (1 or 2) at (x, y); returns the cells it occupied. */
int rolltui_frame_put(RolltuiFrame* f, int x, int y, const char* glyph, size_t glyph_len,
                      int cells, RolltuiStyle s, unsigned int link);
/* The cell's grapheme: a BORROW into the frame, valid until that cell is written again.
 * `*len` receives the byte count. Never NULL; a continuation cell gives length 0. */
const char* rolltui_frame_glyph(const RolltuiFrame* f, int x, int y, size_t* len);
/* ---- the link table ---------------------------------------------------------------------- */
/* A BORROW, valid until the next reset. Empty for id 0 or an unknown id. */
const char* rolltui_frame_link(const RolltuiFrame* f, unsigned int id, size_t* len);
void rolltui_frame_cursor(const RolltuiFrame* f, int* x, int* y, int* visible);
/* ---- equality ---------------------------------------------------------------------------- */
/* What the frame SHOWS, not what it is holding on to: retained link/spill capacity past a
 * reset is not compared. */
int rolltui_frame_equal(const RolltuiFrame* a, const RolltuiFrame* b);


/* ---- INTERNAL: no consumer, host suite or roll test reaches these, and no public shape
 * needs them. ---- */
int rolltui_frame_height(const RolltuiFrame* f);
/* The style the frame was filled with: its ground. An inline region draws it as the terminal's own background. */
void rolltui_frame_ground(const RolltuiFrame* f, RolltuiStyle* out);
/* The ground `rolltui_frame_ground` answers from now on, the cells untouched: the compose sets each window's own background while that
 * window draws, so what a widget draws on its ground lands on the window's and not the screen's. */
void rolltui_frame_set_ground(RolltuiFrame* f, RolltuiStyle ground);

/* 1 when cell (x, ay) of `a` and cell (x, by) of `b` would look the same on screen: one memcmp for a cell with no link and its glyph
 * inline, which is nearly every cell; the glyph and the link compared by value otherwise (an index means nothing outside the frame that
 * made it). Both frames are the same width and the cells are inside them. */
int rolltui_frame_same_cell(const RolltuiFrame* a, const RolltuiFrame* b, int x, int ay, int by);
/* 1 when row `ay` of `a` and row `by` of `b` look the same: one memcmp of the row, then by value only where a cell's glyph is spilled
 * or it links. 0 says only that the cells must be compared one by one. */
int rolltui_frame_rows_same(const RolltuiFrame* a, int ay, const RolltuiFrame* b, int by);
/* `n` single-byte glyphs from (x, y) along the row, one cell each, in `st`: an ASCII run written whole. A wide glyph cut at either end
 * goes whole, as `rolltui_frame_put` does. Clipped at the right edge; returns the cells written. */
int rolltui_frame_put_ascii(RolltuiFrame* f, int x, int y, const char* s, int n, RolltuiStyle st, unsigned int link);
/* Cells [x0, x1) of rows [y0, y1), clipped, set to the one-cell glyph `g` (inline length) in `st`. A wide glyph cut at an edge goes
 * whole. */
void rolltui_frame_fill_narrow(RolltuiFrame* f, int x0, int y0, int x1, int y1, const char* g, size_t len, RolltuiStyle st);
/* A hash of row `y` that two rows which look the same share, whichever frame they are in: a row that moved is found by it. */
unsigned long long rolltui_frame_row_hash(const RolltuiFrame* f, int y);
/* 1 when cell (x, y) of `f` is a space in `style` with no link: a row a scrolled region brought in, compared without being built. */
int rolltui_frame_cell_is_blank(const RolltuiFrame* f, int x, int y, RolltuiStyle style);

int rolltui_frame_width(const RolltuiFrame* f);

/* The cursor's shape the frame asked for (`rolltui_frame_set_cursor_shape`). */
unsigned char rolltui_frame_cursor_shape(const RolltuiFrame* f);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_SCREEN_H */
