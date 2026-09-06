#ifndef ROLLTUI_C_ALLOC_H
#define ROLLTUI_C_ALLOC_H
/*
 * rolltui/c/rolltui_alloc.h — THE C SIDE'S CLOSED SET OF ALLOCATION STRATEGIES (Phase 14).
 *
 * CLAUDE.md states the rule for the whole library: every allocation belongs to a NAMED
 * strategy, the set is CLOSED, and **a new allocation is a CHOICE from that set, never an
 * invention**. It also says that in C the rule can be TOTAL, because every allocation is an
 * explicit call — which is one of the concrete things this phase is here to test.
 *
 * **WE HAD THE ENTRY POINT AND NOT THE SET, and it cost exactly what Phase 13 said it would.**
 * By the end of m3 the two ported C files had invented the same growing buffer EIGHT times
 * between them, with two different policies: `rolltui_screen.c` grew a `Str` to exactly the
 * bytes asked for (so a buffer that gains one byte reallocs every single time) and hand-wrote
 * `cap = cap ? cap * 2 : 4` at five more sites; `rolltui_wrap.c` had one `reserve()` helper
 * that doubled from 16 and hand-wrote two more paired-growth blocks beside it. Nobody was
 * careless — each site was written by someone with no reason to look at the others, which is
 * Phase 13's finding verbatim ("seven independently invented ad-hoc allocations, each needing
 * its own discovery"). The entry point made every allocation VISIBLE; only a closed set makes
 * it a DECISION.
 *
 * THE SET. Four of CLAUDE.md's six need code here; the other two are API shapes and need
 * none — LENT is `Scratch` one level up, and CALLER-FILLED is a parameter.
 *
 *   1. VALUE / INLINE, with a stated SPILL — no code, a layout choice. `RolltuiCell`'s ten
 *      inline glyph bytes spilling into the frame's table is the worked example.
 *   2. GROWING, AMORTISED  `rolltui_grow` / `rolltui_grow_zeroed` — a buffer APPENDED to,
 *      whose final size is not known. Doubles, never shrinks.
 *   3. GROWING, EXACT      `rolltui_fit` — a buffer RESIZED to a size the caller already
 *      knows. Never shrinks below what it holds, never over-allocates.
 *   4. PACKED              `rolltui_pack_*` — one block holding a whole IMMUTABLE composite:
 *      a header and its arrays, carved out together. This is what makes a handed-over wrap
 *      result cost ONE allocation instead of four, and it is the strategy a C library has and
 *      a container-based C++ one structurally cannot.
 *
 * **WHY TWO GROWTH POLICIES RATHER THAN ONE**, since a closed set is supposed to reduce
 * choices: because collapsing them is wrong in a way that costs real bytes. The frame's cell
 * array is resized to exactly `w * h` at every reset — doubling it would make a 120x40 grid
 * allocate 8,192 cells for the 4,800 it needs, a 70% overshoot on the biggest buffer in the
 * library. The wrap engine's buffers are appended to a cluster at a time and MUST amortise.
 * The two cases are genuinely different, so the set names both and every call site says which
 * it is — which is the friction working, not a hole in the rule.
 *
 * ENFORCEMENT IS A GREP CONTROL, NOT A CONVENTION (`rolltui/tests/ownership_test.cpp`):
 * `rolltui_mem_realloc` may be called ONLY from `rolltui_alloc.c`. Growth is the thing that
 * got invented eight times, and `realloc` is how growth is spelled, so that one line is the
 * whole rule. `alloc` and `free` stay available everywhere — a handle and its release are not
 * a strategy anyone can get subtly wrong.
 */
#include <stddef.h>

/* For `rolltui_mem_stats`, which belongs to the module that owns the counters. Every C
 * translation unit includes THIS header for the strategies, and so still gets it. */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the library's one entry point ----------------------------------------------------- */
/* `rolltui_mem_alloc` and `rolltui_mem_free` MOVED to the public `rolltui_mem.h` (included
 * above, so every C translation unit still gets them here) on 2026-09-05, Phase 17 m3. The
 * paragraph at the top of this file already said they "stay available everywhere" while this
 * header — which the umbrella deliberately excludes — was the only place they were declared, so
 * a consumer wanting a handle-and-release pair could not reach one. `lifetime_test`'s
 * conversion is what found it, exactly as a test found `rolltui_mem_stats` in the same position
 * one day earlier.
 *
 * `rolltui_mem_realloc` STAYS HERE, and that is the whole point of splitting them: growth is
 * the thing that got invented eight times, and keeping its declaration in an internal header
 * makes the restriction STRUCTURAL rather than a grep control's promise. The grep stays as
 * well — it catches a `rolltui_alloc.h` includer inside the library, which the header boundary
 * cannot.
 *
 * An allocation failure ABORTS rather than returning NULL, so nothing below can fail and no
 * caller checks. */
void* rolltui_mem_realloc(void* p, size_t bytes);

/* ---- 2. GROWING, AMORTISED ------------------------------------------------------------ */
/* Ensures `p` holds at least `need` elements of `elem` bytes, doubling from a small floor.
 * Returns the buffer, which the caller assigns back. `*cap` is in ELEMENTS and is updated.
 * A no-op when the capacity already suffices, which is what makes a reused buffer free.
 *
 *   w->gs = rolltui_grow(w->gs, &w->gs_cap, w->gs_len + 1, sizeof *w->gs);
 */
void* rolltui_grow(void* p, size_t* cap, size_t need, size_t elem);
/* The same, ZEROING the newly added elements. For a table of slots that must start empty —
 * a slot holding an owned pointer is the case, where garbage would be freed as if it were a
 * buffer. Separated rather than made the default because zeroing a cell grid every growth is
 * work the caller is about to overwrite anyway. */
void* rolltui_grow_zeroed(void* p, size_t* cap, size_t need, size_t elem);

/* ---- 3. GROWING, EXACT ---------------------------------------------------------------- */
/* Ensures `p` holds at least `need` elements, allocating EXACTLY that many when it must
 * grow. For a buffer whose size the caller already knows and which is not appended to. */
void* rolltui_fit(void* p, size_t* cap, size_t need, size_t elem);

/* ---- 4. PACKED ------------------------------------------------------------------------ */
/* One allocation for a header and every array belonging to it. Measure, then carve:
 *
 *   RolltuiPack pk;
 *   rolltui_pack_begin(&pk, sizeof(MyStruct));
 *   const size_t off_a = rolltui_pack_add(&pk, n_a, sizeof(A));
 *   const size_t off_b = rolltui_pack_add(&pk, n_b, sizeof(B));
 *   unsigned char* block = rolltui_pack_alloc(&pk);
 *   MyStruct* s = (MyStruct*)block;
 *   s->a = (A*)(void*)(block + off_a);
 *
 * **EVERY OFFSET IS ALIGNED FOR ANY TYPE**, so a caller never reasons about alignment and
 * never writes the `_Static_assert` proving it — which is the second thing this replaces.
 * The first version of the packed wrap result carried three such assertions and a comment
 * explaining why the byte array had to come last; none of that is a caller's problem.
 *
 * ONLY FOR AN IMMUTABLE COMPOSITE. The arrays are interior to the block: they cannot be
 * grown or freed individually, and whatever owns the block must know that. Use it for a
 * result that is read and released, never for state that is still being built. */
typedef struct RolltuiPack {
  size_t total;
} RolltuiPack;

void rolltui_pack_begin(RolltuiPack* p, size_t header_bytes);
size_t rolltui_pack_add(RolltuiPack* p, size_t count, size_t elem);
void* rolltui_pack_alloc(const RolltuiPack* p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_ALLOC_H */
