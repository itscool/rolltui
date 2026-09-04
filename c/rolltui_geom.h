#ifndef ROLLTUI_C_GEOM_H
#define ROLLTUI_C_GEOM_H
/*
 * rolltui/c/rolltui_geom.h — THE SEAM (Phase 14 m1).
 *
 * The first header both implementations present. Nothing here is interesting on its own:
 * it is rectangle arithmetic. Its job is to make the SWITCH real — one API, two object
 * files, a CMake flag choosing which one links, and the whole test suite green either way
 * — before a line of the actual port is written.
 *
 * WHY A C HEADER AND NOT A C++ ONE: whatever the verdict, this file has to be readable by
 * a C compiler, so the shape is decided now rather than discovered in m3. Two rules it
 * fixes for everything that follows:
 *   - **NO STRUCTS ACROSS THE BOUNDARY YET.** Ints in, ints out through a caller's array.
 *     A struct by value is a layout-and-ABI question, and there is no reason to answer it
 *     for four integers.
 *   - **THE CALLER OWNS EVERY BUFFER.** `out` is the caller's four ints. This is the shape
 *     the string-carrying functions in m3 will have to use, so it is worth being the shape
 *     of the trivial one too.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which implementation is linked: "c" or "c++". The test asserts this against what the
 * build was configured with, so a flag that silently fails to select is a test failure
 * rather than a mystery — the same reason Phase 13's budget proves its counter armed. */
const char* rolltui_impl_name(void);

/* The intersection of two rectangles, written into `out` as {x, y, w, h}. An empty result
 * is {x0, y0, 0, 0} where (x0, y0) is the clamped origin — NOT {0,0,0,0}, because callers
 * position things relative to it. */
void rolltui_rect_intersect(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh,
                            int out[4]);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ROLLTUI_C_GEOM_H */
