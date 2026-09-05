/* rolltui/c/rolltui_alloc.c — the closed set. See rolltui_alloc.h for what each strategy is
 * for and why there are two growth policies rather than one.
 *
 * THIS IS THE ONLY FILE IN THE LIBRARY THAT MAY CALL `rolltui_mem_realloc`, and a grep
 * control in rolltui/tests/ownership_test.cpp is what makes that true rather than intended.
 * It was compiled into both configurations while a C++ implementation existed, even though only
 * the C used it — the
 * same reasoning as `UnicodeSeam.cpp`: a file only one build links is a file only one build
 * can find a defect in. */
#include "rolltui/c/rolltui_alloc.h"

#include <stddef.h>
#include <string.h>

/* ---- ASan: make the sanitizer able to see a LOGICAL overrun ---------------------------
 *
 * **WITHOUT THIS, AddressSanitizer IS NEARLY BLIND TO THIS LIBRARY'S C**, and that was m6b's
 * first real finding rather than a guess: a deliberate one-element read past the live length
 * of a grown buffer produced NO report, because `rolltui_grow` doubles — the byte after the
 * length is still inside the allocation, and ASan guards ALLOCATION boundaries, not logical
 * ones. Every buffer here is over-allocated on purpose, so the mistake C makes easiest —
 * indexing past the live length — was exactly the one the sanitizer could not see.
 *
 * So the slack between the length and the capacity is POISONED, which is the same manual
 * container annotation libc++ uses to make `std::vector` overruns visible. It costs nothing
 * without the sanitizer (the macros compile away) and it is possible at all because m4 put
 * every growth in this one file: annotating twenty hand-written growth sites correctly is not
 * a thing anyone would have finished. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ROLLTUI_ASAN 1
#endif
#endif

#ifdef ROLLTUI_ASAN
void __asan_poison_memory_region(void const volatile* addr, size_t size);
void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
/* Live bytes are usable; everything up to the capacity is not, until a later grow says so. */
static void mark(void* p, size_t live, size_t total) {
  if (!p) return;
  __asan_unpoison_memory_region(p, total);
  if (total > live) __asan_poison_memory_region((char*)p + live, total - live);
}
#else
static void mark(void* p, size_t live, size_t total) {
  (void)p;
  (void)live;
  (void)total;
}
#endif

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
  if (need <= *cap) {
    mark(p, need * elem, *cap * elem);  // the caller's live region just changed
    return p;
  }
  c = *cap ? *cap : ROLLTUI_GROW_FLOOR;
  while (c < need) c *= 2;
  *cap = c;
  p = rolltui_mem_realloc(p, c * elem);
  mark(p, need * elem, c * elem);
  return p;
}

// **THIS ONE POISONS NOTHING, and the difference from `rolltui_grow` is a real semantic one
// rather than an exception.** A zeroed table's slots are VALID OBJECTS all the way to the
// capacity — that is what zeroing them is for — and they may already own a buffer a past frame
// left there (Phase 13 m5b's retained link and spill tables). `rolltui_frame_free` walks to
// `cap`, not to the live count, precisely because of that, and the first version of this
// annotation reported it as a use-after-poison: **a false positive of my own making, caught by
// the studio's golden test within a minute of the poisoning going in.**
//
// So the rule the two growth strategies now carry is: beyond the live length, a plain grown
// buffer holds GARBAGE (poison it) and a zeroed one holds VALID EMPTY SLOTS (do not).
void* rolltui_grow_zeroed(void* p, size_t* cap, size_t need, size_t elem) {
  const size_t was = *cap;
  void* q = rolltui_grow(p, cap, need, elem);
  // BEFORE the memset, not after: `rolltui_grow` has just poisoned everything past `need`,
  // and the zeroing below writes into exactly that region. (ASan caught this ordering too,
  // one run after it caught the missing annotation — the report named `__asan_memset` inside
  // this function, which is about as direct as a diagnosis gets.)
  mark(q, *cap * elem, *cap * elem);  // every slot up to the capacity is a valid empty object
  if (*cap > was) memset((unsigned char*)q + was * elem, 0, (*cap - was) * elem);
  return q;
}

void* rolltui_fit(void* p, size_t* cap, size_t need, size_t elem) {
  if (need <= *cap) {
    mark(p, need * elem, *cap * elem);
    return p;
  }
  *cap = need;
  p = rolltui_mem_realloc(p, need * elem);
  mark(p, need * elem, need * elem);
  return p;
}

void rolltui_pack_begin(RolltuiPack* p, size_t header_bytes) { p->total = align_up(header_bytes); }

size_t rolltui_pack_add(RolltuiPack* p, size_t count, size_t elem) {
  const size_t at = p->total;
  p->total = align_up(at + count * elem);
  return at;
}

void* rolltui_pack_alloc(const RolltuiPack* p) { return rolltui_mem_alloc(p->total ? p->total : 1); }
