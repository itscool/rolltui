/* rolltui/c/rolltui_lifetime.c — see rolltui_lifetime.h. Moved from `rolltui/Lifetime.cpp`
 * at Phase 17 m1; `rolltui/Lifetime.hpp` is now a thin forwarding shim over this file.
 *
 * TWO GROWABLE LISTS, both grown through `rolltui_alloc.h`'s one home for growth
 * (`rolltui_grow`), never by a hand-rolled realloc here:
 *   - the PROCESS-WIDE releasers (`rolltui_on_shutdown`) — a plain function pointer and
 *     not a bound callable: a releaser never needs to capture, it names a static its own
 *     module already has, and a bare function pointer keeps this registry from costing an
 *     allocation per registration the way a closure would.
 *   - the CALLING THREAD's releasers (`rolltui_thread_on_release`), registered by
 *     `Scratch` (`rolltui/Scratch.hpp`) when it is first locked. Per-thread rather than
 *     process-wide because a thread-local buffer cannot be freed from another thread, and
 *     pretending otherwise would be the kind of ambiguity this project spends its time
 *     removing. The releaser and the buffer it releases are kept as two parallel arrays
 *     (function, target) rather than one array of pairs, so each grows through
 *     `rolltui_grow` on its own — the two capacities happen to move in lockstep (same
 *     element size, same `need` every call) but are tracked independently on purpose:
 *     sharing one `cap` between two independently-allocated buffers would let the second
 *     `rolltui_grow` call see "already big enough" from the FIRST buffer's just-updated
 *     capacity and skip reallocating itself. */
#include "rolltui/rolltui.h"

#include <stddef.h>

#include "rolltui/c/rolltui_alloc.h"

/* The process-wide releasers. Zero-initialised by static storage duration, so there is no
 * init to call — the whole point of this module (see the header). */
typedef struct {
  void (**fns)(void);
  size_t count;
  size_t cap;
} RolltuiProcessReleasers;
static RolltuiProcessReleasers g_process;

/* The calling thread's releasers. `_Thread_local` (not a function-local static the way the
 * C++ used `static thread_local` accessors): each thread gets its own zero-initialised
 * copy automatically, which is simpler in C than reproducing the lazy-init-on-first-call
 * shape the C++ used it for — there is nothing to lazily construct here, a
 * zero-initialised struct already IS the empty registry. */
typedef struct {
  void (**fns)(void*);
  size_t fns_cap;
  void** targets;
  size_t targets_cap;
  size_t count;
} RolltuiThreadReleasers;
static _Thread_local RolltuiThreadReleasers g_thread;

void rolltui_on_shutdown(void (*fn)(void)) {
  g_process.fns = rolltui_grow(g_process.fns, &g_process.cap, g_process.count + 1, sizeof *g_process.fns);
  g_process.fns[g_process.count++] = fn;
}

/* Registers one per-thread buffer. Called by `Scratch`'s first lock; the pair is kept as
 * (function, object) rather than a bound callable so that registration allocates at most a
 * growth of these two arrays and never a closure. */
void rolltui_thread_on_release(void (*fn)(void*), void* target) {
  g_thread.fns = rolltui_grow(g_thread.fns, &g_thread.fns_cap, g_thread.count + 1, sizeof *g_thread.fns);
  g_thread.targets =
      rolltui_grow(g_thread.targets, &g_thread.targets_cap, g_thread.count + 1, sizeof *g_thread.targets);
  g_thread.fns[g_thread.count] = fn;
  g_thread.targets[g_thread.count] = target;
  ++g_thread.count;
}

void rolltui_release_thread(void) {
  /* Reverse order, and BY INDEX: a releaser may not register anything new, but indexing
   * through `g_thread.fns`/`g_thread.targets` on every access — rather than caching their
   * current values into a local before the loop — means that even if one somehow did
   * grow either array (a reallocation moving it), the next iteration reads the CURRENT
   * pointer instead of a stale one. `i` itself is bounded once, from the count captured
   * before the loop starts, matching "a releaser may not register anything new": a
   * releaser that broke that rule would simply not have its new entry visited, a safe
   * degradation rather than a dangling access. */
  for (size_t i = g_thread.count; i > 0; --i) g_thread.fns[i - 1](g_thread.targets[i - 1]);
  /* The registries themselves are storage this thread holds, so THEY go too — not just
   * their contents — and the buffers re-register themselves on next use, which is what
   * makes this safe to call in the middle of a session rather than only at the end.
   * `rolltui_grow` never shrinks, so getting the capacity back to zero means freeing the
   * buffer directly rather than asking the grow strategy to do it — `rolltui_mem_free` is
   * available everywhere (CLAUDE.md: "a handle and its release are not a strategy anyone
   * can get subtly wrong"), unlike `rolltui_mem_realloc`, which only `rolltui_alloc.c` may
   * call. */
  rolltui_mem_free(g_thread.fns);
  rolltui_mem_free(g_thread.targets);
  g_thread.fns = NULL;
  g_thread.targets = NULL;
  g_thread.count = 0;
  g_thread.fns_cap = 0;
  g_thread.targets_cap = 0;
}

void rolltui_shutdown(void) {
  for (size_t i = g_process.count; i > 0; --i) g_process.fns[i - 1]();
  rolltui_mem_free(g_process.fns);
  g_process.fns = NULL;
  g_process.count = 0;
  g_process.cap = 0;
  rolltui_release_thread();
}
