#ifndef ROLLTUI_C_WRAP_H
#define ROLLTUI_C_WRAP_H
/*
 * rolltui/c/rolltui_wrap.h — THE WRAP ENGINE, as C (Phase 14 m3).
 *
 * Word wrapping over Unicode: bytes and a width in, lines out, where a line is the bytes to
 * draw plus one entry per grapheme cluster with its cell width and the byte offset it came
 * from. Every rule the engine obeys is stated in `rolltui/Wrap.hpp` and asserted case by
 * case in `rolltui/tests/wrap_test.cpp`; none of it is repeated here, because the rules are
 * the same in both languages and a second copy is a second thing to drift.
 *
 * THIS IS THE MILESTONE'S REAL QUESTION, and it is not the algorithm — it is `wrap_borrow`.
 * The library leans on CALLEE-OWNED SCRATCH, LENT FOR A BOUNDED WINDOW (rolltui/Scratch.hpp)
 * everywhere it draws, and the wrap engine is the pattern's biggest customer: `wrap` runs for
 * every row of a `rows:` window on every frame and for every entry that re-lays, and Phase 13
 * m5b took a steady frame to zero allocations by lending those lines instead of handing them
 * over. **A port that cannot carry the lend cannot carry the library.** So:
 *
 *   1. **THE LINES ARE AN OPAQUE HANDLE, and everything hangs off it.** Cells, graphemes,
 *      the decode buffers, the UAX #14 and #29 buffers, the line under construction — all of
 *      it is storage the handle owns and reuses. The C side therefore has **NO GLOBAL AND NO
 *      THREAD-LOCAL STATE AT ALL**: every buffer belongs to a handle the caller holds, which
 *      is what lets the C++ side put one handle in a `Scratch` and lend it, and another in a
 *      caller's hands, with no coordination between them and nothing to free at thread exit.
 *   2. **A LINE IS READ AS BORROWS** through `rolltui_wrap_line`, valid until the next
 *      `rolltui_wrap`, `rolltui_wrap_copy` or `rolltui_wrap_reset` on that handle. Same
 *      contract as `rolltui_frame_glyph` (m2), same contract as `Scratch` one level up, and
 *      the reason an FFI consumer would hold a handle rather than a pointer and a hope.
 *   3. **THE ANSWER GOES THROUGH OUT-PARAMS, not a returned struct** — `rolltui_frame_mark_at`'s
 *      shape exactly, and for m2's reason: Clang refuses to promise an ABI for an `extern "C"`
 *      function returning a type that is not a C++98 POD, and a caller's buffer was the rule
 *      from m1 anyway. The C++ side turns the out-params into a `Line` of `string_view` +
 *      `span` in one place, which is the same conversion `Mark` and `Cursor` already get.
 *
 * WHY `rolltui_wrap_clone` EXISTS, since an entry point nothing needs is a thing m2 deleted:
 * `wrap()` — the form whose lines must OUTLIVE the call — needs a handle of its own, and a
 * fresh handle's scratch is cold. Wrapping straight into it would pay for the decode and
 * break buffers on every call, which is roughly eight allocations a call that the lend path
 * does not pay. So `wrap()` runs the engine once in a warm handle and clones the LINES out;
 * the clone carries no scratch at all. That is the one function on this boundary that exists
 * for a cost reason rather than a semantic one, and it is said out loud here rather than
 * discovered.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- plain data, defined once and compiled by both languages ------------------------- */

/* `ambiguous_wide` is `unsigned char` and not `bool` for m2's reason (rolltui_style.h): C's
 * `_Bool` and C++'s `bool` are the same byte on every toolchain this will meet, and that is
 * exactly the layout-compatible-by-fiat this project keeps being burned by. The field order
 * is the one the C++ struct had, because two call sites write it as a designated initializer
 * and those are order-sensitive. */
typedef struct RolltuiWrapOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(8);
  int first_indent ROLLTUI_DEFAULT(0);    /* cells before the first line */
  int hanging_indent ROLLTUI_DEFAULT(0);  /* cells before every subsequent line */
} RolltuiWrapOptions;

/* One drawn grapheme cluster of one line. */
typedef struct RolltuiWrapGrapheme {
  size_t offset;         /* into the line's text */
  size_t length;         /* bytes in the line's text */
  size_t source_offset;  /* byte offset in the wrap input (a tab's, for each of its spaces) */
  int width;             /* cells */
  unsigned char space;   /* U+0020 or an expanded tab: droppable at a soft break */
} RolltuiWrapGrapheme;

typedef struct RolltuiWrapLines RolltuiWrapLines;

/* ---- lifetime ------------------------------------------------------------------------ */
/* OWNED by the caller. `new` never returns NULL: an allocation failure aborts inside
 * rolltui::mem, because there is nothing useful to do with a half-built wrap. */
RolltuiWrapLines* rolltui_wrap_new(void);
void rolltui_wrap_free(RolltuiWrapLines* w);
/* Drops the LIVE LINES and keeps every buffer, so a handle that is wrapped into repeatedly
 * allocates nothing after its first few calls. This is what `Scratch` calls on acquire and
 * release, and it is why a steady frame is still zero with this implementation linked. */
void rolltui_wrap_reset(RolltuiWrapLines* w);

/* ---- the engine ----------------------------------------------------------------------- */
/* Wraps `len` bytes of UTF-8 to `width` cells, into `w`, reusing everything `w` holds.
 * `width <= 0` is legal and draws nothing (one empty line per paragraph); empty input is one
 * empty line. The rules are Wrap.hpp's. */
void rolltui_wrap(RolltuiWrapLines* w, const char* utf8, size_t len, int width, RolltuiWrapOptions opt);

/* A NEW handle holding a copy of `src`'s lines and nothing else — no scratch, because a
 * handed-over result is never wrapped into again in practice. This is what `wrap()` returns.
 *
 * **THE C IMPLEMENTATION DOES THIS IN ONE ALLOCATION**, and that is a deliberate answer to a
 * measurement rather than an optimisation for its own sake. The first cut of this boundary
 * copied into four separate blocks — the handle, the bytes, the clusters, the line records —
 * and cost 200 allocations a resize frame more than the C++ side, whose `std::string` holds
 * a short total inline. It was written up as "C has no small-string optimisation", which is
 * true and was the wrong conclusion: **the sizes of all three buffers are known at the moment
 * a result exists, so the whole thing is one block with the arrays carved out of it.** C++'s
 * containers cannot do that — each owns its own allocation by definition — so what looked
 * like a language deficit is the opposite once the C is written to C's strengths.
 *
 * The consequence a caller can see, stated because it is the price: a cloned handle's three
 * buffers are INTERIOR to its own block. Wrapping into one (legal, and nothing does) drops
 * them and starts over on the heap, abandoning that space until the handle is freed. Reading
 * and resetting are unaffected. */
RolltuiWrapLines* rolltui_wrap_clone(const RolltuiWrapLines* src);

/* ---- reading the lines ----------------------------------------------------------------- */
size_t rolltui_wrap_line_count(const RolltuiWrapLines* w);
/* Line `i` as BORROWS into `w` (see rule 2 above). `text` is NOT NUL-terminated — `text_len`
 * is the length, and a zero-length line gives a valid non-NULL pointer. `hard` receives 1
 * when the line was ended by a mandatory break or by the end of the text. */
void rolltui_wrap_line(const RolltuiWrapLines* w, size_t i, const char** text, size_t* text_len,
                       const RolltuiWrapGrapheme** graphemes, size_t* grapheme_count,
                       int* width, int* indent, int* hard);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WRAP_H */
