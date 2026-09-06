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
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
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
