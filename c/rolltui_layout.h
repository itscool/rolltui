#ifndef ROLLTUI_C_LAYOUT_H
#define ROLLTUI_C_LAYOUT_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_layout.h — PLACEMENT, COMPOSITION, THE STACK AND THE LOADER (Phase 15 m5,
 * the loader and every English sentence added at Phase 17 m2).
 *
 * The algorithm half of the layout module: what a Dim resolves to, how a Row divides its
 * width, which borders join, which window has focus and where an event goes, and — since
 * Phase 17 m2 — how a layout FILE turns into that tree and back. Every rule is stated in
 * `rolltui/Layout.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it is
 * repeated here. The DATA it walks is `rolltui_layout_tree.h`, which is C unconditionally: m5
 * built `-DROLLTUI_C` as a two-implementation rollback flag, and CMakeLists.txt's own note
 * records that flag as SPENT as of 2026-09-04 — `LayoutCpp.cpp` (this file's one-time C++
 * counterpart) is deleted along with the other fifteen `*Cpp.cpp` files, and this is now the
 * only implementation, not one side of a flag.
 *
 * ---- THE LIFETIME THIS FILE MAKES EXPLICIT ----------------------------------------------
 *
 * **`WindowStack` owns its layers.** In C++ that was `std::vector<Layer> layers_{Layer{}}`,
 * and the ownership was three separate accidents: the vector deep-copied a whole tree on
 * `set_base`, `push(Layer)` took one BY VALUE so a popup's tree was copied twice on the way
 * in, and a `Layer&` handed out by `base()` dangled the moment a popup pushed. Here the
 * stack is an opaque handle that owns an array of layers it MOVES into place, `push` takes
 * a layer and leaves the caller's empty, and `base()` is a call rather than a reference kept
 * across a mutation.
 *
 * ---- THE BOUNDARY'S RULES, all inherited from Phase 14 and none new ----------------------
 *
 *   1. **THE CALLER OWNS EVERY BUFFER**, including working memory: `rolltui_compose_layer`
 *      needs two screen-sized byte maps and takes a SCRATCH handle rather than keeping a
 *      `thread_local` of its own, which is CLAUDE.md's strategy 3 as amended on 2026-09-04.
 *   2. **NOTHING IS RETURNED BY VALUE** except plain scalars and the two POD geometry
 *      structs that are one definition in both languages.
 *   3. **NO `std::function` CROSSES.** A resolve EMITS THROUGH A SINK and a compose calls a
 *      slot renderer through a function pointer and a `void*` — the shape m4 fixed for the
 *      highlighting seam, and for the same reason: the caller decides where the nodes go.
 *   4. **THIS FILE NAMES NO ROLE AND NO ACTION.** The three roles a border needs and the
 *      three stack actions are HANDED IN as bytes and as strings, the m2 rule at
 *      `rolltui_diff.h` and the m3 rule at `rolltui_bindings.h`'s Enter rule. The C knows
 *      "the focused window's border uses this role" and none of the words.
 *
 * ---- WHAT THIS BOUNDARY DELIBERATELY DOES NOT KNOW ---------------------------------------
 *
 * **(Historical, kept for the reasoning.)** When this was written, `rolltui::Layout`,
 * `rolltui::Content` and `rolltui::ActionDecl` kept `std::string`/`std::vector` fields in
 * `Layout.hpp` and a shim unpacked `RolltuiLoadedLayout` into them once per load. `Layout.hpp`
 * and the shim are deleted (Phase 17 m2c): `RolltuiLayout`, `RolltuiContent` and
 * `RolltuiLayoutAction` below ARE the types every consumer holds, and since Phase 18 m2 a
 * content's kind is its NAME rather than a C++-only enum (the registry section says why).
 * What crosses is unchanged — the ALGORITHM (JSON in, JSON out, which rung, what rule) and
 * every SENTENCE a bad layout produces.
 *
 * **Role names.** `background`'s vocabulary is `Style.hpp`'s (`rolltui_layout_tree.h`'s own
 * rule: "this file names no role"), so the loader and the dumper ask back through
 * `RolltuiLayoutHooks::role_from_name`/`role_name` rather than carrying a table of their own
 * — the same shape `RolltuiScopeFn` already uses for "which scopes are the library's".
 * Anchor and border names, by contrast, are THIS module's own vocabulary (Layout.hpp states
 * both), so they are declared and looked up right here, no callback needed.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif
/* floor(fraction * extent + 1e-6) + cells. */
int rolltui_resolve_dim(RolltuiDim d, int extent);
/* The inner rect once the border is taken off (a None border takes nothing). */
void rolltui_inner_rect(RolltuiRect outer, unsigned char border, RolltuiRect* out);

/* "fill" | "fill 2" | a dim string. */
int rolltui_parse_split_size(const char* text, size_t len, RolltuiSplitSize* out);

/* Draws the nodes of ONE layer, in the order given. `styles` is `kRoleCount` styles — the
 * theme's table, a BORROW for the call. */
void rolltui_compose_layer(RolltuiFrame* f, const RolltuiResolvedNode* nodes, size_t count,
                           const RolltuiStyle* styles, const RolltuiLayoutRoles* roles, RolltuiSlotFn render,
                           void* ctx, int ambiguous_wide, RolltuiComposeScratch* scratch);

void rolltui_widget_kind_clear(void);

size_t rolltui_action_list_count(const RolltuiActionList* l);
RolltuiLayoutAction* rolltui_action_list_at(const RolltuiActionList* l, size_t i);
void rolltui_action_list_remove(RolltuiActionList* l, size_t i); /* frees it, shifts the rest down */

/* Reads exactly the "actions" object of an already-parsed tree, APPENDING every string-
 * valued entry. Never through `rolltui_load_layout`, which asks for this when a file
 * declares none — going through the full loader to compute its own fallback would recurse
 * into itself; this is the raw, independent read `rolltui::shipped_default_actions()` needs
 * (one definition site is still the "default" file; this is a direct read of one key of it). */
void rolltui_layout_read_actions_key(const RolltuiJsonValue* root, RolltuiLayoutAction** actions, size_t* actions_n,
                                    size_t* actions_cap);

size_t rolltui_layout_builtin_count(void);
const char* rolltui_layout_builtin_name(size_t i, size_t* out_len);

/* Parses one layout file's ALREADY-PARSED JSON tree into `out` (an `out` the caller has run
 * `rolltui_loaded_layout_init` on — its old fields are not released first, matching
 * `rolltui::load_layout`'s "everything else loads with the problems reported" only at the
 * level a fresh handle already gives it). 1 when there is a usable "root" (`report` may
 * still carry problems); 0 only when the JSON is not an object or has none of it
 * (`report->error` says which — `out` is left as `rolltui_loaded_layout_init` set it).
 * `default_actions`/`_n` are `shipped_default_actions()`'s, handed in for the "no actions
 * key at all" fallback (see `rolltui_layout_read_actions_key` for why that is computed
 * independently rather than through this same function). `report` is NOT reset on entry —
 * matching `rolltui::load_layout`'s own convention, the caller starts one fresh per call. */
int rolltui_load_layout(const RolltuiJsonValue* root, RolltuiLoadedLayout* out,
                        const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                        const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);

void rolltui_window_stack_cycle_focus(RolltuiWindowStack* s, int backwards);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
