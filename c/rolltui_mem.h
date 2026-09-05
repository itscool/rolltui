#ifndef ROLLTUI_C_MEM_H
#define ROLLTUI_C_MEM_H
/*
 * rolltui/c/rolltui_mem.h — THE LIBRARY'S ONE ENTRY POINT FOR MEMORY, as C (Phase 13,
 * moved from C++ at Phase 17 m1).
 *
 * Every allocation the library makes ITSELF goes through here, even though today this is a
 * thin wrapper over `malloc`. Four reasons, and the first is already a stated requirement
 * rather than a preference:
 *
 *   1. **IT IS THE ONE PLACE TO INSTRUMENT.** `plan/phase-13.md`'s Done-when says the
 *      budget's numbers must be tracked AT RUNTIME and not only inside a test binary that
 *      replaces the global `operator new` — CLAUDE.md's one-pipeline-two-consumers rule.
 *      This is the only place that can live, and `rolltui_mem_stats` is it.
 *   2. **IT IS THE ONE PLACE TO CHANGE.** `malloc` today; mmap'd pages, a pool or a
 *      different libc tomorrow, without touching a caller.
 *   3. **A GENERAL-PURPOSE GROWING HEAP IS A STRATEGY**, not the absence of one. It is the
 *      sixth in CLAUDE.md's closed set, and it needs a home like the other five.
 *   4. **THE FRICTION IS THE FEATURE.** A call that makes you name your strategy makes you
 *      pick one, which is the whole reason the set is closed.
 *
 * **THIS IS TOTAL IN C, AND THE C++ HISTORY IS WHY THAT SENTENCE IS WORTH ANYTHING.** Every
 * allocation the library makes is an explicit call through here, so these figures are the
 * library's whole footprint rather than the part it happens to be counting. While a C++
 * implementation of the library existed alongside this one, `std::string` and
 * `std::vector` reached the global `operator new` and never this entry point, so the same
 * figures covered only the library's OWN explicit allocations there — **partial by
 * construction** in that build, asserted in `rolltui/tests/budget_test.cpp` rather than
 * promised. The C++ implementations were deleted 2026-09-04; this file (moved from
 * `rolltui/Memory.cpp`, Phase 17 m1) is now the only implementation there is.
 *
 * THREADS: the counters are RELAXED atomics. They are telemetry, not a ledger — a torn
 * read would misreport a number, never corrupt an allocation.
 *
 * WHERE `rolltui_mem_alloc` / `rolltui_mem_realloc` / `rolltui_mem_free` /
 * `rolltui_mem_stats` ARE DECLARED: not here, but in `rolltui/c/rolltui_alloc.h`. That
 * header needed them before this one existed (every C translation unit already includes
 * it for the closed set of allocation strategies built on top of these four), and it
 * still declares them "for every C translation unit" rather than duplicating the same
 * text a second time for no reason. This header carries this module's documentation and
 * the one operation `rolltui_alloc.h` never needed: resetting the counters for a test
 * that wants a clean window.
 *
 * THE ONE EXCEPTION IN THE CLOSED SET (CLAUDE.md): everywhere else, a new allocation is a
 * CHOICE from `rolltui_alloc.h`'s four named strategies, never an invention. This module
 * is the one place that is allowed to call `malloc`/`realloc`/`free` directly, because it
 * IS the bottom of that stack — the strategies are built out of these four calls, not
 * the other way around, so there is nothing under them to route through.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resets the CUMULATIVE counters only (`rolltui_mem_alloc`'s `allocations`, `frees` and
 * `bytes_requested`, as `rolltui_mem_stats` reports them). `live_bytes` and `live_blocks`
 * describe storage that still exists and zeroing them would be a lie; `peak_bytes` is
 * re-based to whatever is currently live, which is the lowest value it could honestly
 * take. For a test that wants a window; never called by the library itself. */
void rolltui_mem_reset_stats(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MEM_H */
