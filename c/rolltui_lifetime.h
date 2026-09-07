#ifndef ROLLTUI_C_LIFETIME_H
#define ROLLTUI_C_LIFETIME_H
/*
 * rolltui/c/rolltui_lifetime.h — INTERNAL (Phase 20 m1/m3, 2026-09-06).
 *
 * RE-CREATED. Phase 19 m3 deleted this header under the rule "a header exists because a .c
 * needs a declaration from it; one that declares nothing is deleted" — at that point every
 * declaration here was public and lived in the definition. Phase 20 moved it
 * back to INTERNAL, so the .c needs a declaration again and the same rule re-creates the
 * file. Its subject: the per-thread scratch registration the library does for itself.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_OPT_IN`. */
/* Registers one per-thread buffer with `rolltui_release_thread`: `fn` is called with
 * `target` when the calling thread's scratch is released (by an explicit call, or by
 * `rolltui_shutdown`). This is `rolltui::detail::on_thread_release`'s C implementation —
 * declared for C++ in `rolltui/Scratch.hpp`, which keeps its own forward declaration
 * (deliberately not including this header) so that file "stays a template and nothing
 * else". Registering the same (fn, target) pair twice registers it twice. */
void rolltui_thread_on_release(void (*fn)(void*), void* target);

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
/* ========================================================================================
 * lifetime — the release point
 * ======================================================================================== */
/* Registers a releaser to run at `rolltui_shutdown()`, in reverse order of registration.
 * Registering the same function twice registers it twice; register once, where the thing
 * is made. */
void rolltui_on_shutdown(void (*fn)(void));

/* Releases just the calling thread's scratch buffers — the reused storage the draw path
 * lends (`rolltui/Scratch.hpp`). This is the explicit path for a long-lived thread that
 * wants to hand back its high-water mark, and for a leak check that needs the number to
 * be knowable BEFORE the process ends. A thread that is about to exit need not call it. */
void rolltui_release_thread(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LIFETIME_H */
