// rolltui/GeomCpp.cpp — the C++ side of the seam (Phase 14 m1). Same symbols, same
// answers; `rolltui/c/rolltui_geom.c` is the other one, and the ROLLTUI_C flag picks.
//
// This exists so the switch can be built and proved BEFORE anything is ported: with the
// flag off the library behaves exactly as it did, and the test suite is already running
// against a C-shaped API.
#include "rolltui/c/rolltui_geom.h"

#include <algorithm>

extern "C" const char* rolltui_impl_name(void) { return "c++"; }

extern "C" void rolltui_rect_intersect(int ax, int ay, int aw, int ah,
                                       int bx, int by, int bw, int bh,
                                       int out[4]) {
  const int x0 = std::max(ax, bx), y0 = std::max(ay, by);
  const int x1 = std::min(ax + aw, bx + bw), y1 = std::min(ay + ah, by + bh);
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
