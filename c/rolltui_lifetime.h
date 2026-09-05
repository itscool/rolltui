#ifndef ROLLTUI_C_LIFETIME_H
#define ROLLTUI_C_LIFETIME_H
/*
 * rolltui/c/rolltui_lifetime.h — THE RELEASE POINT, reachable from C (Phase 15 m2; the
 * implementation itself — process-wide AND per-thread releasers — moved here from
 * `rolltui/Lifetime.cpp` at Phase 17 m1; `rolltui/Lifetime.hpp` is now a thin forwarding
 * shim over this file, same as `rolltui/c/rolltui_mem.h` is for `rolltui/Memory.hpp`).
 *
 * `rolltui::shutdown()` (here, `rolltui_shutdown`) is where the library hands back
 * everything it retains, and `rolltui/Lifetime.hpp` states the rule for taking part:
 * **register a releaser where the retained thing is MADE, not in a central list — a
 * central list is a second place to forget.** Until Phase 15 m2 no C module retained
 * anything (the frame, the wrap engine and the Unicode algorithms have no `static` at
 * all, by design), so the registration had no C spelling. The effect-kind registry was
 * the first, and it was not the last: the widget registry, the layout cache and two
 * action tables followed.
 *
 * FOUR FUNCTIONS, and they are the same ones C++ calls (`rolltui::on_shutdown`,
 * `rolltui::shutdown`, `rolltui::release_thread`, `rolltui::detail::on_thread_release`).
 * `rolltui_shutdown` runs the process-wide releasers in reverse order of registration —
 * so a module that retains something built out of another module's thing is released
 * first — then releases every thread's own scratch the same way `rolltui_release_thread`
 * does on its own.
 *
 * **THERE IS NO INIT, AND THAT IS THE DESIGN** (carried over from `rolltui/Lifetime.hpp`,
 * which states the reasoning at length: a library you must initialise is worse than one
 * you need not, and teardown carries none of that ordering cost). `rolltui_shutdown` is
 * safe to never call, safe to call twice, and needs nothing to have happened first.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Registers a releaser to run at `rolltui_shutdown()`, in reverse order of registration.
 * Registering the same function twice registers it twice; register once, where the thing
 * is made. */
void rolltui_on_shutdown(void (*fn)(void));

/* Releases everything the library retains: every registered process-wide releaser (most
 * recently registered first), then every thread's own scratch via `rolltui_release_thread`
 * (which here can only release the CALLING thread's — the same limit `release_thread()`
 * has always had, since another thread's `_Thread_local` storage cannot be freed from
 * here). Safe to never call and safe to call twice — the second call finds nothing to do.
 * Nothing is invalidated for a host that carries on afterwards; the caches simply rebuild
 * on next use, which is what makes this safe to call at any time rather than only at the
 * very end. */
void rolltui_shutdown(void);

/* Releases just the calling thread's scratch buffers — the reused storage the draw path
 * lends (`rolltui/Scratch.hpp`). This is the explicit path for a long-lived thread that
 * wants to hand back its high-water mark, and for a leak check that needs the number to
 * be knowable BEFORE the process ends. A thread that is about to exit need not call it. */
void rolltui_release_thread(void);

/* Registers one per-thread buffer with `rolltui_release_thread`: `fn` is called with
 * `target` when the calling thread's scratch is released (by an explicit call, or by
 * `rolltui_shutdown`). This is `rolltui::detail::on_thread_release`'s C implementation —
 * declared for C++ in `rolltui/Scratch.hpp`, which keeps its own forward declaration
 * (deliberately not including this header) so that file "stays a template and nothing
 * else". Registering the same (fn, target) pair twice registers it twice. */
void rolltui_thread_on_release(void (*fn)(void*), void* target);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LIFETIME_H */
