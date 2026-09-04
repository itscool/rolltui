#pragma once
//
// rolltui/Memory.hpp — THE LIBRARY'S ONE ENTRY POINT FOR MEMORY (Phase 13).
//
// Every allocation the library makes ITSELF goes through here, even though today this is a
// thin wrapper over `malloc`. Four reasons, and the first is already a stated requirement
// rather than a preference:
//
//   1. **IT IS THE ONE PLACE TO INSTRUMENT.** `plan/phase-13.md`'s Done-when says the
//      budget's numbers must be tracked AT RUNTIME and not only inside a test binary that
//      replaces the global `operator new` — CLAUDE.md's one-pipeline-two-consumers rule.
//      This is the only place that can live, and `stats()` is it.
//   2. **IT IS THE ONE PLACE TO CHANGE.** `malloc` today; mmap'd pages, a pool or a
//      different libc tomorrow, without touching a caller.
//   3. **A GENERAL-PURPOSE GROWING HEAP IS A STRATEGY**, not the absence of one. It is the
//      sixth in CLAUDE.md's closed set, and it needs a home like the other five.
//   4. **THE FRICTION IS THE FEATURE.** A call that makes you name your strategy makes you
//      pick one, which is the whole reason the set is closed.
//
// **WHAT THIS CANNOT COVER IN C++, said plainly so nobody reads the numbers as total:**
// `std::string` and `std::vector` allocate through the global `operator new`, not through
// this. Threading a custom allocator through every container would be viral and is not
// worth it, so in C++ these figures account for the library's OWN explicit allocations and
// nothing else — which today, after Phase 13, is very little, because a steady frame
// allocates nothing at all. **In C the same rule would be TOTAL**, since every allocation
// is an explicit call; how much of the library's memory each language's entry point
// actually accounts for is one of the things `plan/phase-14.md`'s verdict has to report.
//
// THREADS: the counters are relaxed atomics. They are telemetry, not a ledger — a torn
// read would misreport a number, never corrupt an allocation.
//
#include <atomic>
#include <cstddef>

namespace rolltui::mem {

// Live totals since the process started. Cheap to read; safe from any thread.
struct Stats {
  // Calls that returned NEW STORAGE — a growing `realloc` included, because it hands out
  // storage and copies into it exactly as a fresh allocation does. This is the number the
  // budget adds to the C++ side's `operator new` count, so it has to mean the same thing in
  // a module that grows a `realloc`'d buffer as in one that grows a `std::vector`
  // (Phase 14 m3; it counted the realloc as neither before, which made C look free).
  std::size_t allocations = 0;
  std::size_t frees = 0;
  std::size_t bytes_requested = 0;  // cumulative, not current
  std::size_t live_blocks = 0;      // tracked, NOT allocations - frees: a grow is not a new block
};
Stats stats();
void reset_stats();  // for a test that wants a window; never called by the library

// The growing-heap strategy (CLAUDE.md's sixth). Returns nullptr only when the request is
// zero bytes; a genuine out-of-memory aborts, because there is nothing a terminal-UI
// library can usefully do with a failed 200-byte allocation and a half-drawn frame is
// worse than a clean death.
void* alloc(std::size_t bytes);
void* realloc(void* p, std::size_t bytes);
void free(void* p);

}  // namespace rolltui::mem
