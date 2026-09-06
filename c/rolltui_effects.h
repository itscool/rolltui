#ifndef ROLLTUI_C_EFFECTS_H
#define ROLLTUI_C_EFFECTS_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_effects.h — MOTION, as C (Phase 15 m2).
 *
 * A widget MARKS a span of cells with a STATE; the theme maps state → effect as data; an
 * effect is a pure function of (elapsed, cell index, span length, fraction, base style).
 * Every rule, every built-in kind and the reasoning behind all of it is in
 * `rolltui/Effects.hpp` and asserted over every registered kind in
 * `rolltui/tests/effects_test.cpp`; none of it is repeated here, because the rules are the
 * same in both languages and a second copy is a second thing to drift.
 *
 * THIS IS THE OWNERSHIP-HEAVY HALF OF m2, and the reason it and `Diff` were ported
 * together. `plan/phase-15.md` predicts the port's cost tracks how much of a module is
 * ownership work rather than algorithm — Phase 14 measured +12% for Unicode against +55%
 * for Wrap. `Diff` is a pure function; this is **a process-wide registry that OWNS a name
 * and a host's callable per entry, is guarded by a mutex, and must hand everything back at
 * `rolltui::shutdown()`.** If the prior is right, these two land on opposite sides of it.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14:
 *   1. **THE CALLER OWNS EVERY BUFFER**, working memory included, through a handle
 *      (`RolltuiEffectScratch`).
 *   2. **ONE DEFINITION**: `rolltui::EffectCell` and `rolltui::EffectOut` ARE the two
 *      structs below, aliased rather than converted, so a host's kind reads and writes the
 *      same bytes the applier does.
 *   3. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function.
 *
 * ---- THE VIEW BECAME THE DEFINITION, WHICH IS THE m2 SEAM CLOSING --------------------
 *
 * In m2 `RolltuiEffectSpec` was a VIEW of one `rolltui::EffectSpec`, because the spec was
 * the THEME's data — `std::string kind`, `std::vector<std::string> frames`,
 * `std::vector<Role> roles` — and `Theme` had not ported. That header said, in as many
 * words: **"when `Theme` ports in m3 the view becomes the definition and `fill_view` is
 * deleted — that deletion is the evidence m6 should read, not this paragraph."**
 *
 * This is that. `RolltuiEffectMap` below OWNS every spec a theme carries, in C, and
 * `rolltui::EffectMap` is a handle to one. Three things went with the change and none of
 * them is a rewrite anybody chose — each was a piece of machinery that existed ONLY to
 * bridge two owners:
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
/* Releases every host kind — for a test, and for a host tearing down. This is also what
 * runs at `rolltui::shutdown()`; the registry registers itself the first time it holds
 * anything, which is the rule in rolltui/Lifetime.hpp. */
void rolltui_effect_clear_registered(void);

/* How many distinct pictures one period of `spec` has over a span `length` cells long. */
int rolltui_effect_steps(const RolltuiEffectSpec* spec, int length);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_EFFECTS_H */
