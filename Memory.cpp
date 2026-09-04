// rolltui/Memory.cpp — see Memory.hpp.
#include "rolltui/Memory.hpp"

#include <cstdio>
#include <cstdlib>

// THE USABLE SIZE OF A BLOCK, which is what makes `live_bytes` possible without a header on
// every allocation. Both mainstream libcs expose it; anything else is a BUILD ERROR rather
// than a `live_bytes` that silently reads zero — a memory gauge stuck at 0 looks exactly like
// a library that allocates nothing, which is this project's characteristic failure aimed at
// its own instrument (see the budget test's header).
#if defined(__APPLE__)
#include <malloc/malloc.h>
#define ROLLTUI_USABLE_SIZE(p) malloc_size(p)
#elif defined(__GLIBC__)
#include <malloc.h>
#define ROLLTUI_USABLE_SIZE(p) malloc_usable_size(p)
#else
#error "rolltui::mem needs a usable-size function to report live_bytes; add one for this libc"
#endif

namespace rolltui::mem {

namespace {
std::atomic<std::size_t> g_allocations{0}, g_frees{0}, g_bytes{0}, g_live{0};
std::atomic<std::size_t> g_live_bytes{0}, g_peak_bytes{0};

// Raises the high-water mark to `now` if it is higher. A plain load-compare-store would lose
// a concurrent raise; this is telemetry, but a peak that silently under-reports is worse than
// one that costs a CAS on the rare occasion it actually moves.
void raise_peak(std::size_t now) {
  std::size_t peak = g_peak_bytes.load(std::memory_order_relaxed);
  while (now > peak && !g_peak_bytes.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
  }
}

// Adds a block's USABLE size to the live total. Called after every allocation that returned
// storage, and mirrored by `drop_live` before every free.
void add_live(void* p) {
  if (!p) return;
  const std::size_t n = ROLLTUI_USABLE_SIZE(p);
  raise_peak(g_live_bytes.fetch_add(n, std::memory_order_relaxed) + n);
}
void drop_live(void* p) {
  if (!p) return;
  g_live_bytes.fetch_sub(ROLLTUI_USABLE_SIZE(p), std::memory_order_relaxed);
}

[[noreturn]] void out_of_memory(std::size_t bytes) {
  // Nothing useful is available here: a half-drawn frame is worse than a clean death, and
  // there is no caller in a terminal-UI library that can do better with a failed 200-byte
  // request. Said out loud so the exit is not mysterious.
  std::fprintf(stderr, "rolltui: out of memory requesting %zu bytes\n", bytes);
  std::abort();
}
}  // namespace

Stats stats() {
  Stats s;
  s.allocations = g_allocations.load(std::memory_order_relaxed);
  s.frees = g_frees.load(std::memory_order_relaxed);
  s.bytes_requested = g_bytes.load(std::memory_order_relaxed);
  s.live_bytes = g_live_bytes.load(std::memory_order_relaxed);
  s.peak_bytes = g_peak_bytes.load(std::memory_order_relaxed);
  // TRACKED, not derived. `allocations` counts every call that returned NEW STORAGE, a
  // growing realloc included, so it is no longer `live + frees` — see `realloc` below.
  s.live_blocks = g_live.load(std::memory_order_relaxed);
  return s;
}

// Resets the CUMULATIVE counters only. `live_bytes` and `live_blocks` describe storage that
// still exists and zeroing them would be a lie; `peak_bytes` is re-based to what is currently
// live, which is the lowest value it could honestly take.
void reset_stats() {
  g_allocations.store(0, std::memory_order_relaxed);
  g_frees.store(0, std::memory_order_relaxed);
  g_bytes.store(0, std::memory_order_relaxed);
  g_peak_bytes.store(g_live_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void* alloc(std::size_t bytes) {
  if (bytes == 0) return nullptr;
  void* p = std::malloc(bytes);
  if (!p) out_of_memory(bytes);
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  g_live.fetch_add(1, std::memory_order_relaxed);
  g_bytes.fetch_add(bytes, std::memory_order_relaxed);
  add_live(p);
  return p;
}

void* realloc(void* p, std::size_t bytes) {
  if (bytes == 0) {
    free(p);
    return nullptr;
  }
  // The old block's size has to be read BEFORE the realloc, which may free or move it.
  drop_live(p);
  void* q = std::realloc(p, bytes);
  if (!q) out_of_memory(bytes);
  add_live(q);
  // A GROWING REALLOC COUNTS AS AN ALLOCATION, and this was the other way round until
  // Phase 14 m3 pointed the instrument at a module that grows buffers. The old rule
  // ("a realloc that grew an existing block is not a NEW BLOCK, so it counts as neither")
  // was right about blocks and wrong about work: it made the C implementation, whose every
  // buffer grows through `realloc`, look free next to a C++ one whose every `std::vector`
  // growth is a counted `operator new`. A budget that reads a language difference as a cost
  // difference is the same hole m2 found, one level down — so `allocations` now means what
  // Memory.hpp always said it meant, "calls that returned new storage", and `live_blocks`
  // is tracked instead of derived.
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (!p) g_live.fetch_add(1, std::memory_order_relaxed);  // a grow replaces a block in place
  g_bytes.fetch_add(bytes, std::memory_order_relaxed);
  return q;
}

void free(void* p) {
  if (!p) return;
  drop_live(p);
  std::free(p);
  g_frees.fetch_add(1, std::memory_order_relaxed);
  g_live.fetch_sub(1, std::memory_order_relaxed);
}

}  // namespace rolltui::mem

// The C face of the same entry point (Phase 14). The ported modules are C and cannot see a
// namespace, so these three are what CLAUDE.md's "every allocation goes through the
// library's entry point" means on that side — and there it is TOTAL, because every
// allocation in C is an explicit call.
extern "C" void* rolltui_mem_alloc(std::size_t bytes) { return rolltui::mem::alloc(bytes); }
extern "C" void* rolltui_mem_realloc(void* p, std::size_t bytes) { return rolltui::mem::realloc(p, bytes); }
extern "C" void rolltui_mem_free(void* p) { rolltui::mem::free(p); }

// THE QUERY, as C. Out-params rather than a returned struct, which is this boundary's rule
// since Phase 14 m2: Clang declines to promise an ABI for an `extern "C"` function returning
// a non-POD class, and a caller's buffer was m1's rule anyway. Any pointer may be NULL, so a
// caller asks for exactly the numbers it wants to show.
extern "C" void rolltui_mem_stats(size_t* allocations, size_t* frees, size_t* bytes_requested,
                                  size_t* live_bytes, size_t* peak_bytes, size_t* live_blocks) {
  const rolltui::mem::Stats s = rolltui::mem::stats();
  if (allocations) *allocations = s.allocations;
  if (frees) *frees = s.frees;
  if (bytes_requested) *bytes_requested = s.bytes_requested;
  if (live_bytes) *live_bytes = s.live_bytes;
  if (peak_bytes) *peak_bytes = s.peak_bytes;
  if (live_blocks) *live_blocks = s.live_blocks;
}
