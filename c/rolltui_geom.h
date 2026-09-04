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

#include "rolltui/c/rolltui_abi.h"

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

/* ---- a rectangle, defined ONCE and compiled by both languages (Phase 15 m5) ------------ */
/* `rolltui::Rect` IS this struct. It moved here the moment the C had to HOLD one rather
 * than take four ints: a layout node's outer and inner boxes are the split's whole output,
 * and passing them as sixteen loose integers would have been the "layout-compatible by
 * fiat" this project keeps being burned by. The four ints in / four out below stay, because
 * they are what the C++ side's `intersect` is implemented in terms of and what the flag
 * still chooses between. */
typedef struct RolltuiRect {
  int x ROLLTUI_DEFAULT(0), y ROLLTUI_DEFAULT(0), w ROLLTUI_DEFAULT(0), h ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  RolltuiRect intersect(const RolltuiRect& o) const {  /* still through the seam */
    int r[4];
    rolltui_rect_intersect(x, y, w, h, o.x, o.y, o.w, o.h, r);
    return RolltuiRect{r[0], r[1], r[2], r[3]};
  }
  bool empty() const { return w <= 0 || h <= 0; }
  bool operator==(const RolltuiRect&) const = default;
#endif
} RolltuiRect;
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiRect) == 16, "a Rect must be four ints in both languages");

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ROLLTUI_C_GEOM_H */
