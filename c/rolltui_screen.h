#ifndef ROLLTUI_C_SCREEN_H
#define ROLLTUI_C_SCREEN_H
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
 *   1. **ONE DEFINITION. The C++ types ARE these structs.** `rolltui::Style` is a `using`
 *      alias for `RolltuiStyle`, and the methods C++ wants live in `#ifdef __cplusplus`
 *      blocks inside the struct — the standard dual-language shape.
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
 *      implementation linked, and the budget test asserts it in both configurations.
 *   4. **TEXT OUT IS A BORROW WITH A STATED WINDOW.** `rolltui_frame_glyph` returns a
 *      pointer into the frame, valid until the next call that mutates that cell. It is the
 *      same contract `Scratch` enforces one level up, and it is why a handle — not a raw
 *      pointer with a hope — is what an FFI consumer would hold.
 *
 * NOT HERE, on purpose: `render_diff` / `render_full` / `frame_to_text`. They are consumers
 * of a frame rather than part of one, they need the theme's SGR encoder, and they read
 * through the accessors below in either configuration. Porting them is its own step.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- plain data, defined once and compiled by both languages --------------------- */

/* kind: 0 none, 1 indexed, 2 rgb — the same three `rolltui::Color::Kind` has. */
typedef struct {
  unsigned char kind, index, r, g, b;
} RolltuiStyleColor;

typedef struct {
  RolltuiStyleColor fg, bg;
  unsigned char bold, italic, underline, dim, reverse;
} RolltuiStyle;

/* One cell, as the C side stores it. `len` is the inline glyph's byte count, or
 * ROLLTUI_CELL_SPILLED when the bytes live in the frame's table. */
#define ROLLTUI_CELL_INLINE_GLYPH 10
#define ROLLTUI_CELL_SPILLED 0xFF
typedef struct {
  unsigned int link;
  RolltuiStyle style;
  char bytes[ROLLTUI_CELL_INLINE_GLYPH];
  unsigned char len;
  unsigned char width;
  unsigned char continuation;
} RolltuiCell;

typedef struct RolltuiFrame RolltuiFrame;

/* ---- lifetime -------------------------------------------------------------------- */
/* OWNED by the caller. `new` never returns NULL: an allocation failure aborts inside
 * rolltui::mem, because a half-built frame is worse than a clean death. */
RolltuiFrame* rolltui_frame_new(int w, int h, RolltuiStyle fill);
RolltuiFrame* rolltui_frame_clone(const RolltuiFrame* src);
void rolltui_frame_free(RolltuiFrame* f);
/* Reuses every buffer it can (Phase 13 m5): the cells, the link table's strings and the
 * spill table's. A steady frame allocates nothing through here. */
void rolltui_frame_reset(RolltuiFrame* f, int w, int h, RolltuiStyle fill);
void rolltui_frame_clear(RolltuiFrame* f, RolltuiStyle fill);

/* ---- geometry and cells ----------------------------------------------------------- */
int rolltui_frame_width(const RolltuiFrame* f);
int rolltui_frame_height(const RolltuiFrame* f);
/* A COPY of the cell. Out of bounds gives a zeroed cell with width 0. */
RolltuiCell rolltui_frame_cell(const RolltuiFrame* f, int x, int y);
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

/* ---- marks (the widget's whole vocabulary for motion) ------------------------------ */
/* `state` is rolltui::EffectState as an int; the C side stores it and never interprets it,
 * which is what keeps the effects vocabulary in one place (Effects.hpp) rather than two. */
void rolltui_frame_mark(RolltuiFrame* f, int x, int y, int cells, int state,
                        unsigned long long since_ms, double fraction);
size_t rolltui_frame_mark_count(const RolltuiFrame* f);
void rolltui_frame_mark_at(const RolltuiFrame* f, size_t i, int* x, int* y, int* cells,
                           int* state, unsigned long long* since_ms, double* fraction);
void rolltui_frame_clear_marks(RolltuiFrame* f);

/* ---- cursor ------------------------------------------------------------------------ */
void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible);
void rolltui_frame_cursor(const RolltuiFrame* f, int* x, int* y, int* visible);

/* ---- equality ---------------------------------------------------------------------- */
/* What the frame SHOWS, not what it is holding on to: retained link/spill capacity past a
 * reset is not compared (Phase 13 m5b found that the hard way). */
int rolltui_frame_equal(const RolltuiFrame* a, const RolltuiFrame* b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_SCREEN_H */
