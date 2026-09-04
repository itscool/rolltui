// rolltui/Memory.cpp — see Memory.hpp.
#include "rolltui/Memory.hpp"

#include <cstdio>
#include <cstdlib>

namespace rolltui::mem {

namespace {
std::atomic<std::size_t> g_allocations{0}, g_frees{0}, g_bytes{0}, g_live{0};

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
  // TRACKED, not derived. `allocations` counts every call that returned NEW STORAGE, a
  // growing realloc included, so it is no longer `live + frees` — see `realloc` below.
  s.live_blocks = g_live.load(std::memory_order_relaxed);
  return s;
}

void reset_stats() {
  g_allocations.store(0, std::memory_order_relaxed);
  g_frees.store(0, std::memory_order_relaxed);
  g_bytes.store(0, std::memory_order_relaxed);
}

void* alloc(std::size_t bytes) {
  if (bytes == 0) return nullptr;
  void* p = std::malloc(bytes);
  if (!p) out_of_memory(bytes);
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  g_live.fetch_add(1, std::memory_order_relaxed);
  g_bytes.fetch_add(bytes, std::memory_order_relaxed);
  return p;
}

void* realloc(void* p, std::size_t bytes) {
  if (bytes == 0) {
    free(p);
    return nullptr;
  }
  void* q = std::realloc(p, bytes);
  if (!q) out_of_memory(bytes);
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
