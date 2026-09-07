#ifndef ROLLTUI_C_EFFECTS_H
#define ROLLTUI_C_EFFECTS_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_effects.h — MOTION, as C.
 *
 * A widget MARKS a span of cells with a STATE; the theme maps state → effect as data; an
 * effect is a pure function of (elapsed, cell index, span length, fraction, base style).
 * Every rule, every built-in kind and the reasoning behind all of it is in
 * `rolltui/Effects.hpp` and asserted over every registered kind in
 * `rolltui/tests/effects_test.cpp`; none of it is repeated here, because the rules are the
 * same in both languages and a second copy is a second thing to drift.
 *
 * THIS MODULE IS OWNERSHIP-HEAVY, which is what makes it expensive to express in C and
 * worth the friction: a registry that OWNS a name and a host's callable per entry, and must
 * hand everything back at release. Measured against a pure function like `Diff`, the cost of
 * writing a module in C tracks how much of it is ownership rather than algorithm (+12% of
 * lines for Unicode, +55% for Wrap) — and so does the benefit.
 *
 * THE BOUNDARY'S RULES:
 *   1. **THE CALLER OWNS EVERY BUFFER**, working memory included, through a handle
 *      (`RolltuiEffectScratch`).
 *   2. **ONE DEFINITION**: `rolltui::EffectCell` and `rolltui::EffectOut` ARE the two
 *      structs below, aliased rather than converted, so a host's kind reads and writes the
 *      same bytes the applier does.
 *   3. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function.
 *
 * ---- THE SPEC IS THE DEFINITION, NOT A VIEW OF ONE ------------------------------------------
 *
 * `RolltuiEffectMap` below OWNS every spec a theme carries, and a C++ `EffectMap` is a handle
 * to one. A VIEW over some other owner's spec costs three pieces of machinery that exist ONLY
 * to bridge the two owners, and every one of them disappears when there is a single owner:
 *   - `fill_view`, the one place a `std::vector<Role>` was reinterpreted as bytes and a
 *     fallback role substituted (the map is told its fallback once, at construction);
 *   - `SpecViews`, twelve inline views with a heap spill, rebuilt on every `apply_effects`
 *     and every `effect_tick_ms` (the map already IS the flat array, so there is nothing
 *     to build);
 *   - the `frame` accessor and the `owner` token beside it, which existed because a
 *     `std::vector<std::string>` has no contiguous (pointer, length) array to lend. A map
 *     keeps its frames as exactly that array, so a kind indexes it.
 *
 * **ROLES STILL CROSS AS BYTES AND THE C STILL NAMES NONE OF THEM.** `roles` is one byte
 * per entry and `styles` is the theme's whole role table; the C indexes one with the other
 * and never learns what a role is called. `role_count` is never zero, because the map
 * substitutes the fallback it was HANDED at construction — so no rung of this file has an
 * opinion about which role that is.
 *
 * **AN UNKNOWN KIND IS REPORTED THROUGH A CALLBACK, not through an OUT field on the spec.**
 * It used to be `unresolved`, written into the caller's throwaway view. A map is the
 * theme's own storage and a theme is `const` while it is being drawn with, so the applier
 * says the name instead of marking it — which also costs nothing at all in the ordinary
 * case, where every kind resolves and the callback is never reached.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Releases every host kind THIS CONTEXT holds — for a test, and for a host tearing down a
 * session without freeing it. `rolltui_context_free` does the same by name, so nothing has to
 * remember to call this; it exists for the case where the context outlives its kinds. */
void rolltui_effect_clear_registered(RolltuiContext* c);

/* How many distinct pictures one period of `spec` has over a span `length` cells long. */
int rolltui_effect_steps(const RolltuiEffectSpec* spec, int length);



/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
RolltuiEffectMap* rolltui_effect_map_clone(const RolltuiEffectMap* m);
void rolltui_effect_map_clear(RolltuiEffectMap* m);
int rolltui_effect_map_equal(const RolltuiEffectMap* a, const RolltuiEffectMap* b);
size_t rolltui_effect_map_count(const RolltuiEffectMap* m, size_t state);
const RolltuiEffectSpec* rolltui_effect_map_at(const RolltuiEffectMap* m, size_t state, size_t i);
/* Appends a spec to `state` and returns its index; the two adders then fill it in. A spec
 * is built rather than handed over whole because its three arrays are variable-length, and
 * a builder is what keeps them the MAP's allocations instead of a caller's. */
size_t rolltui_effect_map_add(RolltuiEffectMap* m, size_t state, const char* kind, size_t kind_len, int period_ms,
                              int width, int steps, int backward);
void rolltui_effect_map_add_frame(RolltuiEffectMap* m, size_t state, size_t i, const char* bytes, size_t len);
void rolltui_effect_map_add_role(RolltuiEffectMap* m, size_t state, size_t i, unsigned char role);
/* BORROWS a static literal. An out-of-range state reads back as "none", which is what the
 * C++ `effect_state_name` did and what a mark of an unknown state means. */
const char* rolltui_effect_state_name(unsigned char state, size_t* len);
/* Registers a kind. The registry COPIES the name and takes ownership of `ctx`, releasing
 * it with `free_ctx` at `clear` or at `rolltui_context_free`. On any refusal it takes
 * nothing: `ctx` is still the caller's, and `free_ctx` is not called. */
int rolltui_effect_register(RolltuiContext* c, const char* name, size_t name_len, RolltuiEffectFn fn, void* ctx,
                            void (*free_ctx)(void*));
/* Every kind name that resolves right now, in RESOLUTION ORDER: the library's closed seven
 * first and never shadowed, then the host's. `name` is a BORROW, valid until the registry
 * next changes. */
size_t rolltui_effect_kind_count(const RolltuiContext* c);
const char* rolltui_effect_kind_name(const RolltuiContext* c, size_t i, size_t* len);
/* Whether anything answers for `name` — a HOST fact, never a theme error. */
int rolltui_effect_kind_resolves(const RolltuiContext* c, const char* name, size_t len);
int rolltui_effect_is_builtin(const char* name, size_t len);


/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
RolltuiEffectMap* rolltui_effect_map_new(size_t states, unsigned char fallback_role);

/* The state of that name, or -1 when there is none — what a theme LOADER needs. */
int rolltui_effect_state_from_name(const char* name, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_EFFECTS_H */
