#pragma once
//
// rolltui/Lifetime.hpp — THE RELEASE POINT (Phase 14 m6a).
//
// **THE IMPLEMENTATION MOVED TO C AT PHASE 17 m1** (`rolltui/c/rolltui_lifetime.{h,c}`);
// this header is now a thin forwarding shim so that no existing caller has to change.
//
// **THERE IS NO INIT, AND THAT IS THE DESIGN.** A library you must initialise is worse than
// one you need not: it adds an ordering requirement, a global, and one more thing a host can
// forget. Teardown carries none of that cost — `shutdown()` is safe to never call, safe to
// call twice, and needs nothing to have happened first. So rolltui has the half that pays and
// not the half that charges.
//
// WHY IT EXISTS AT ALL, since the library runs perfectly well without it. Two reasons, and
// the first is already true today rather than anticipated:
//
//   1. **THE LIBRARY RETAINS THINGS.** An effect-kind registry, a host-kind registry, the
//      parsed built-in layout cache, two action tables — all allocated on first use and never
//      released. Nothing is wrong with that in a process that exits; it means that "did we
//      leak?" has no answer, because by-design retention and a real leak look identical to
//      any checker.
//   2. **IT IS CHEAPEST TO ESTABLISH WHEN THE ANSWER IS ZERO.** Phase 14 ported 10% of the
//      library; if its verdict is to continue, the rest is the layer where long-lived state
//      actually lives — widgets, layers, the layout cache, preset stores. Adding a release
//      point after that is a pile of leaks to bisect at once. Adding it now is a hook and an
//      assertion that stay green.
//
// THE INVARIANT, and it is a TEST rather than a promise (rolltui/tests/lifetime_test.cpp):
//
//     after shutdown(), rolltui::mem::stats().live_bytes == 0 and live_blocks == 0
//
// IT IS NOW A TOTAL STATEMENT, and it was not always one. While a C++ implementation of the
// library existed alongside the C, this assertion was weaker than it looked in that build:
// `std::string` and `std::vector` reach the global `operator new`, never `rolltui::mem`, so a
// zero here could coexist with megabytes the gauge simply could not see. With the library in C
// every allocation is an explicit call through one entry point, so `live_bytes == 0` means the
// library holds nothing — not that it holds nothing it happens to be counting.
//
// HOW A MODULE TAKES PART: call `on_shutdown` where the retained thing is created, not in some
// central list. A central list is a second place to forget.
//
//     std::map<std::string, EffectFn>& registry() {
//       static std::map<std::string, EffectFn> r;
//       static const bool once = (on_shutdown([] { registry().clear(); }), true);
//       (void)once;
//       return r;
//     }
//
#include <cstddef>

#include "rolltui/c/rolltui_lifetime.h"

namespace rolltui {

// Registers a releaser to run at `shutdown()`, in reverse order of registration — so a module
// that retains something built out of another module's thing is released first. Registering
// the same function twice registers it twice; register once, where the thing is made.
inline void on_shutdown(void (*fn)()) { rolltui_on_shutdown(fn); }

// Releases everything the library retains: every registered releaser, then this thread's
// scratch buffers. Safe to never call and safe to call twice — the second call finds nothing
// to do. Nothing is invalidated for a host that carries on afterwards; the caches simply
// rebuild on next use, which is what makes this safe to call at any time rather than only at
// the very end.
inline void shutdown() { rolltui_shutdown(); }

// Releases just the calling thread's scratch buffers — the reused storage the draw path lends
// (rolltui/Scratch.hpp). Automatic at thread exit in C++, because those are `thread_local` and
// have destructors; this is the explicit path, for a long-lived thread that wants to hand back
// its high-water mark, and for a leak check that needs the number to be knowable BEFORE the
// process ends. A thread that is about to exit need not call it.
inline void release_thread() { rolltui_release_thread(); }

}  // namespace rolltui
