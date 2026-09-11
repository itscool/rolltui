/* rolltui/c/rolltui_marker.c — the "▼ N more" marker. Contract in rolltui_marker.h. */
#include "rolltui/rolltui.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_unicode.h"
#include "testkit/testctl.h"

/* The marker is an arrow, ASCII digits and ASCII words, so its display width is its byte
 * count less two for the three-byte arrow — no Unicode scratch, and no allocation, on a
 * path a capped code block takes per block per frame. Stated rather than left implicit,
 * because it is the one place this file could quietly disagree with `display_width`. */
static int ascii_plus_arrow_width(const char* s, size_t n) {
  (void)s;
  return (int)n - 2;
}

size_t rolltui_scroll_marker_text(size_t below, int max_width, int ambiguous_wide, char* out, size_t cap) {
  char buf[ROLLTUI_MARKER_MAX];
  int n;
  (void)ambiguous_wide; /* ▼ is neutral and the rest is ASCII: no width depends on it */
  if (below == 0 || max_width <= 0 || cap == 0) return 0;
  /* The full form only when it costs at most HALF the width; then the count alone; then
   * the arrow, which still says "there is more" and costs one cell. */
  n = snprintf(buf, sizeof buf, "\xE2\x96\xBC %zu more ", below);
  /* ON = the HALF rule gone, so the full form is taken whenever it merely FITS. It never
   * overflows the row and every byte written is legal - it just eats most of a narrow row to
   * say something the count alone already said. */
  if (n > 0 && (size_t)n <= sizeof buf &&
      ascii_plus_arrow_width(buf, (size_t)n) *
              (testkit_ctl_on("marker.full_form_ignores_the_half_width_rule") ? 1 : 2) <=
          max_width) {
    if ((size_t)n > cap) return 0;
    memcpy(out, buf, (size_t)n);
    return (size_t)n;
  }
  n = snprintf(buf, sizeof buf, "\xE2\x96\xBC%zu", below);
  if (n > 0 && (size_t)n <= sizeof buf && ascii_plus_arrow_width(buf, (size_t)n) <= max_width) {
    if ((size_t)n > cap) return 0;
    memcpy(out, buf, (size_t)n);
    return (size_t)n;
  }
  if (max_width >= 1 && cap >= 3) {
    memcpy(out, "\xE2\x96\xBC", 3);
    return 3;
  }
  return 0;
}
