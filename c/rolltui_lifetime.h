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

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
/* Registers one per-thread buffer with `rolltui_release_thread`: `fn` is called with
 * `target` when the calling thread's scratch is released (by an explicit call, or by
 * `rolltui_shutdown`). This is `rolltui::detail::on_thread_release`'s C implementation —
 * declared for C++ in `rolltui/Scratch.hpp`, which keeps its own forward declaration
 * (deliberately not including this header) so that file "stays a template and nothing
 * else". Registering the same (fn, target) pair twice registers it twice. */
void rolltui_thread_on_release(void (*fn)(void*), void* target);

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
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
