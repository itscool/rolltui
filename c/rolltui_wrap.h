#ifndef ROLLTUI_C_WRAP_H
#define ROLLTUI_C_WRAP_H
/*
 * rolltui/c/rolltui_wrap.h — INTERNAL (Phase 20 m1/m3, 2026-09-06).
 *
 * RE-CREATED. Phase 19 m3 deleted this header under the rule "a header exists because a .c
 * needs a declaration from it; one that declares nothing is deleted" — at that point every
 * declaration here was public and lived in the definition. Phase 20 moved them
 * back to INTERNAL, so the .c needs a declaration again and the same rule re-creates the
 * file. Its subject: the wrap scratch's own steps.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
void rolltui_wrap_reset(RolltuiWrapLines* w);
/* A NEW handle holding a copy of `src`'s lines and nothing else — no scratch, because a
 * handed-over result is never wrapped into again in practice. This is what `wrap()` returns.
 *
 * **THE C IMPLEMENTATION DOES THIS IN ONE ALLOCATION**, and that is a deliberate answer to a
 * measurement rather than an optimisation for its own sake. The first cut of this boundary
 * copied into four separate blocks — the handle, the bytes, the clusters, the line records —
 * and cost 200 allocations a resize frame more than the C++ side, whose `std::string` holds
 * a short total inline. It was written up as "C has no small-string optimisation", which is
 * true and was the wrong conclusion: **the sizes of all three buffers are known at the moment
 * a result exists, so the whole thing is one block with the arrays carved out of it.** C++'s
 * containers cannot do that — each owns its own allocation by definition — so what looked
 * like a language deficit is the opposite once the C is written to C's strengths.
 *
 * The consequence a caller can see, stated because it is the price: a cloned handle's three
 * buffers are INTERIOR to its own block. Wrapping into one (legal, and nothing does) drops
 * them and starts over on the heap, abandoning that space until the handle is freed. Reading
 * and resetting are unaffected. */
RolltuiWrapLines* rolltui_wrap_clone(const RolltuiWrapLines* src);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WRAP_H */
