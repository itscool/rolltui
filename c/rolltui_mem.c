/* rolltui/c/rolltui_mem.c — see rolltui_mem.h and rolltui_alloc.h (which declares the four
 * entry points this file defines: `rolltui_mem_alloc`, `rolltui_mem_realloc`,
 * `rolltui_mem_free`, `rolltui_mem_stats`). Moved from `rolltui/Memory.cpp` at Phase 17 m1,
 * the C++ implementation deleted with it; `rolltui/Memory.hpp` is now a thin forwarding
 * shim over this file.
 *
 * Included below for the compiler to check this file's definitions against the canonical
 * declarations — a purely compile-time cross-check, not a functional dependency: this
 * module is the BOTTOM of the allocation-strategy stack (see rolltui_mem.h), and nothing
 * here calls anything `rolltui_alloc.c` provides. */
#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/rolltui.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* THE USABLE SIZE OF A BLOCK, which is what makes `live_bytes` possible without a header on
 * every allocation. Both mainstream libcs expose it; anything else is a BUILD ERROR rather
 * than a `live_bytes` that silently reads zero — a memory gauge stuck at 0 looks exactly like
 * a library that allocates nothing, which is this project's characteristic failure aimed at
 * its own instrument (see the budget test's header). */
#if defined(__APPLE__)
#include <malloc/malloc.h>
#define ROLLTUI_USABLE_SIZE(p) malloc_size(p)
#elif defined(__GLIBC__)
#include <malloc.h>
#define ROLLTUI_USABLE_SIZE(p) malloc_usable_size(p)
#else
#error "rolltui_mem needs a usable-size function to report live_bytes; add one for this libc"
#endif

/* Live totals since the process started. Cheap to read; safe from any thread — RELAXED
 * atomics, because these are telemetry, not a ledger: a torn read would misreport a
 * number, never corrupt an allocation. */
static _Atomic size_t g_allocations = 0, g_frees = 0, g_bytes = 0, g_live = 0;
static _Atomic size_t g_live_bytes = 0, g_peak_bytes = 0;

/* Raises the high-water mark to `now` if it is higher. A plain load-compare-store would lose
 * a concurrent raise; this is telemetry, but a peak that silently under-reports is worse than
 * one that costs a CAS on the rare occasion it actually moves. */
static void raise_peak(size_t now) {
  size_t peak = atomic_load_explicit(&g_peak_bytes, memory_order_relaxed);
  while (now > peak &&
         !atomic_compare_exchange_weak_explicit(&g_peak_bytes, &peak, now, memory_order_relaxed, memory_order_relaxed)) {
  }
}

/* Adds a block's USABLE size to the live total. Called after every allocation that returned
 * storage, and mirrored by `drop_live` before every free. */
static void add_live(void* p) {
  if (!p) return;
  const size_t n = ROLLTUI_USABLE_SIZE(p);
  raise_peak(atomic_fetch_add_explicit(&g_live_bytes, n, memory_order_relaxed) + n);
}
static void drop_live(void* p) {
  if (!p) return;
  atomic_fetch_sub_explicit(&g_live_bytes, ROLLTUI_USABLE_SIZE(p), memory_order_relaxed);
}

/* Nothing useful is available here: a half-drawn frame is worse than a clean death, and
 * there is no caller in a terminal-UI library that can do better with a failed 200-byte
 * request. Said out loud so the exit is not mysterious. */
static _Noreturn void out_of_memory(size_t bytes) {
  fprintf(stderr, "rolltui: out of memory requesting %zu bytes\n", bytes);
  abort();
}

void rolltui_mem_stats(size_t* allocations, size_t* frees, size_t* bytes_requested,
                       size_t* live_bytes, size_t* peak_bytes, size_t* live_blocks) {
  if (allocations) *allocations = atomic_load_explicit(&g_allocations, memory_order_relaxed);
  if (frees) *frees = atomic_load_explicit(&g_frees, memory_order_relaxed);
  if (bytes_requested) *bytes_requested = atomic_load_explicit(&g_bytes, memory_order_relaxed);
  if (live_bytes) *live_bytes = atomic_load_explicit(&g_live_bytes, memory_order_relaxed);
  if (peak_bytes) *peak_bytes = atomic_load_explicit(&g_peak_bytes, memory_order_relaxed);
  /* TRACKED, not derived. `allocations` counts every call that returned NEW STORAGE, a
   * growing realloc included, so it is no longer `live + frees` — see `rolltui_mem_realloc`
   * below. */
  if (live_blocks) *live_blocks = atomic_load_explicit(&g_live, memory_order_relaxed);
}

void* rolltui_mem_alloc(size_t bytes) {
  if (bytes == 0) return NULL;
  void* p = malloc(bytes);
  if (!p) out_of_memory(bytes);
  atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_live, 1, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_bytes, bytes, memory_order_relaxed);
  add_live(p);
  return p;
}

void* rolltui_mem_realloc(void* p, size_t bytes) {
  if (bytes == 0) {
    rolltui_mem_free(p);
    return NULL;
  }
  /* The old block's size has to be read BEFORE the realloc, which may free or move it. */
  drop_live(p);
  void* q = realloc(p, bytes);
  if (!q) out_of_memory(bytes);
  add_live(q);
  /* A GROWING REALLOC COUNTS AS AN ALLOCATION, and this was the other way round until
   * Phase 14 m3 pointed the instrument at a module that grows buffers. The old rule
   * ("a realloc that grew an existing block is not a NEW BLOCK, so it counts as neither")
   * was right about blocks and wrong about work: it made the C implementation, whose every
   * buffer grows through `realloc`, look free next to a C++ one whose every `std::vector`
   * growth is a counted `operator new`. A budget that reads a language difference as a cost
   * difference is the same hole m2 found, one level down — so `allocations` now means what
   * this header always said it meant, "calls that returned new storage", and `live_blocks`
   * is tracked instead of derived. */
  atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
  if (!p) atomic_fetch_add_explicit(&g_live, 1, memory_order_relaxed);  /* a grow replaces a block in place */
  atomic_fetch_add_explicit(&g_bytes, bytes, memory_order_relaxed);
  return q;
}

void rolltui_mem_free(void* p) {
  if (!p) return;
  drop_live(p);
  free(p);
  atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
  atomic_fetch_sub_explicit(&g_live, 1, memory_order_relaxed);
}
