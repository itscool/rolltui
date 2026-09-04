/* rolltui/c/rolltui_alloc.c — the closed set. See rolltui_alloc.h for what each strategy is
 * for and why there are two growth policies rather than one.
 *
 * THIS IS THE ONLY FILE IN THE LIBRARY THAT MAY CALL `rolltui_mem_realloc`, and a grep
 * control in rolltui/tests/ownership_test.cpp is what makes that true rather than intended.
 * It is compiled into BOTH configurations even though only the C implementations use it — the
 * same reasoning as `UnicodeSeam.cpp`: a file only one build links is a file only one build
 * can find a defect in. */
#include "rolltui/c/rolltui_alloc.h"

#include <stddef.h>
#include <string.h>

/* The floor a doubling buffer starts at. Small enough that a handle holding two links does
 * not reserve a page, big enough that a wrap of one line does not realloc four times. */
#define ROLLTUI_GROW_FLOOR 16

/* Every packed section starts on this boundary, which is the strictest any type can ask for.
 * Over-aligning by up to 15 bytes per section is the price of a caller never having to prove
 * an alignment, and it is the right trade for a set whose whole purpose is removing per-site
 * reasoning. */
typedef union {
  long double ld;
  void* p;
  long long ll;
} RolltuiMaxAlign;
#define ROLLTUI_PACK_ALIGN (sizeof(RolltuiMaxAlign) < 16 ? 16u : sizeof(RolltuiMaxAlign))

static size_t align_up(size_t n) {
  const size_t a = ROLLTUI_PACK_ALIGN;
  return (n + a - 1) / a * a;
}

void* rolltui_grow(void* p, size_t* cap, size_t need, size_t elem) {
  size_t c;
  if (need <= *cap) return p;
  c = *cap ? *cap : ROLLTUI_GROW_FLOOR;
  while (c < need) c *= 2;
  *cap = c;
  return rolltui_mem_realloc(p, c * elem);
}

void* rolltui_grow_zeroed(void* p, size_t* cap, size_t need, size_t elem) {
  const size_t was = *cap;
  void* q = rolltui_grow(p, cap, need, elem);
  if (*cap > was) memset((unsigned char*)q + was * elem, 0, (*cap - was) * elem);
  return q;
}

void* rolltui_fit(void* p, size_t* cap, size_t need, size_t elem) {
  if (need <= *cap) return p;
  *cap = need;
  return rolltui_mem_realloc(p, need * elem);
}

void rolltui_pack_begin(RolltuiPack* p, size_t header_bytes) { p->total = align_up(header_bytes); }

size_t rolltui_pack_add(RolltuiPack* p, size_t count, size_t elem) {
  const size_t at = p->total;
  p->total = align_up(at + count * elem);
  return at;
}

void* rolltui_pack_alloc(const RolltuiPack* p) { return rolltui_mem_alloc(p->total ? p->total : 1); }
