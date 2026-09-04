// rolltui/Memory.cpp — see Memory.hpp.
#include "rolltui/Memory.hpp"

#include <cstdio>
#include <cstdlib>

namespace rolltui::mem {

namespace {
std::atomic<std::size_t> g_allocations{0}, g_frees{0}, g_bytes{0};

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
  s.live_blocks = s.allocations >= s.frees ? s.allocations - s.frees : 0;
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
  // A realloc that GREW an existing block is one allocation's worth of new storage and no
  // new block; counting it as both an alloc and a free would make live_blocks right and
  // the totals nonsense, so it counts as neither when p was non-null.
  if (!p) g_allocations.fetch_add(1, std::memory_order_relaxed);
  g_bytes.fetch_add(bytes, std::memory_order_relaxed);
  return q;
}

void free(void* p) {
  if (!p) return;
  std::free(p);
  g_frees.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace rolltui::mem

// The C face of the same entry point (Phase 14). The ported modules are C and cannot see a
// namespace, so these three are what CLAUDE.md's "every allocation goes through the
// library's entry point" means on that side — and there it is TOTAL, because every
// allocation in C is an explicit call.
extern "C" void* rolltui_mem_alloc(std::size_t bytes) { return rolltui::mem::alloc(bytes); }
extern "C" void* rolltui_mem_realloc(void* p, std::size_t bytes) { return rolltui::mem::realloc(p, bytes); }
extern "C" void rolltui_mem_free(void* p) { rolltui::mem::free(p); }
