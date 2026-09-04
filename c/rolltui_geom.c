/* rolltui/c/rolltui_geom.c — the C side of the seam. See rolltui_geom.h. */
#include "rolltui/c/rolltui_geom.h"

const char* rolltui_impl_name(void) { return "c"; }

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

void rolltui_rect_intersect(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh,
                            int out[4]) {
  const int x0 = imax(ax, bx), y0 = imax(ay, by);
  const int x1 = imin(ax + aw, bx + bw), y1 = imin(ay + ah, by + bh);
  out[0] = x0;
  out[1] = y0;
  if (x1 <= x0 || y1 <= y0) {
    out[2] = 0;
    out[3] = 0;
    return;
  }
  out[2] = x1 - x0;
  out[3] = y1 - y0;
}
