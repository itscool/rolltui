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
// **THIS IS NOW TOTAL, AND THE HISTORY IS WHY THAT SENTENCE IS WORTH ANYTHING.** Every
// allocation the library makes is an explicit call through here, so these figures are the
// library's whole footprint rather than the part it happens to be counting.
//
// It said the opposite for four phases, and the limit was real: `std::string` and
// `std::vector` allocate through the global `operator new`, never through this, and threading
// a custom allocator through every container is viral and was never worth it. So in C++ these
// figures covered the library's OWN explicit allocations and nothing else — **partial by
// construction**, stated here rather than discovered later, and asserted in
// `rolltui/tests/budget_test.cpp` rather than promised.
//
// **THE DIFFERENCE WAS MEASURED BEFORE IT WAS ACTED ON (Phase 14 m4, then Phase 15 m6).** On
// one 40-entry scene painted and still held, `live_bytes` read **0 B** in the C++ build and
// 187,776 B with Phase 14's slice ported; at the end of Phase 15 the same scene read
// **909,600 B against 2,746,576 B**, and the sharpest form of it was a single markdown parse
// that the gauge could see entirely in C and **not at all** in C++. The C++ implementations
// were deleted on 2026-09-04 and that gap closed with them — the assertion in `budget_test`
// is now the positive one, and the limit above is history rather than a caveat.
//
// THREADS: the counters are relaxed atomics. They are telemetry, not a ledger — a torn
// read would misreport a number, never corrupt an allocation.
//
#include <atomic>
#include <cstddef>

namespace rolltui::mem {

// Live totals since the process started. Cheap to read; safe from any thread.
//
// **THREE BYTE NUMBERS, AND THEY ANSWER THREE DIFFERENT QUESTIONS.** They are separated
// rather than collapsed because "memory usage" is exactly the kind of identifier this
// project keeps getting burned by: a single `bytes` field would have been read as "what we
// are holding" by a status pane and as "how much we churned" by the budget, and it cannot be
// both. `bytes_requested` was that field, and until 2026-09-04 there was no way at all to ask
// what the library is holding RIGHT NOW — which is the one a human means.
struct Stats {
  // Calls that returned NEW STORAGE — a growing `realloc` included, because it hands out
  // storage and copies into it exactly as a fresh allocation does. This is the number the
  // budget adds to the C++ side's `operator new` count, so it has to mean the same thing in
  // a module that grows a `realloc`'d buffer as in one that grows a `std::vector`
  // (Phase 14 m3; it counted the realloc as neither before, which made C look free).
  std::size_t allocations = 0;
  std::size_t frees = 0;
  // CUMULATIVE bytes handed out, never decreasing. A growing buffer is counted again at
  // every growth, so this is a CHURN signal and is emphatically NOT how much is held.
  std::size_t bytes_requested = 0;
  // HELD RIGHT NOW — the number a status pane means by "memory usage". This is the
  // allocator's USABLE size, not the requested size, so it is what the process actually has
  // reserved (>= requested, by the rounding every malloc does). Goes down on free.
  std::size_t live_bytes = 0;
  // The high-water mark of `live_bytes`. The interesting one for a library whose whole
  // design is reusing buffers: it says how big the reuse ever had to get.
  std::size_t peak_bytes = 0;
  std::size_t live_blocks = 0;  // tracked, NOT allocations - frees: a grow is not a new block
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
