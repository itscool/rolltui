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
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"

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

void rolltui_action_list_remove(RolltuiActionList* l, size_t i); /* frees it, shifts the rest down */

/* Reads exactly the "actions" object of an already-parsed tree, APPENDING every string-
 * valued entry. Never through `rolltui_load_layout`, which asks for this when a file
 * declares none — going through the full loader to compute its own fallback would recurse
 * into itself; this is the raw, independent read `rolltui::shipped_default_actions()` needs
 * (one definition site is still the "default" file; this is a direct read of one key of it). */
void rolltui_layout_read_actions_key(const RolltuiJsonValue* root, RolltuiLayoutAction** actions, size_t* actions_n,
                                    size_t* actions_cap);

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


/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_OPT_IN`. */
/* Draws one border with NO joining — for a widget that boxes its own content. `title` may
 * be NULL when `title_n` is 0. */
void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border,
                         RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                         int ambiguous_wide);
unsigned char rolltui_widget_kind_rule(size_t row);
const char* rolltui_widget_kind_source_is(size_t row, size_t* len);
/* A C caller's pair, for the same reason every owned type here has one — both strings empty
 * either way. C++ needs neither (see above) but they exist so a pure C caller has the same
 * capability. */
void rolltui_content_init(RolltuiContent* c);
void rolltui_content_release(RolltuiContent* c);
void rolltui_content_copy(RolltuiContent* to, const RolltuiContent* from);
int rolltui_content_equal(const RolltuiContent* a, const RolltuiContent* b);
int rolltui_layout_equal(const RolltuiLayout* a, const RolltuiLayout* b);
/* Releases an array `rolltui_layout_read_actions_key` filled (or any array of this shape) —
 * so a caller need not reach past this header for `rolltui_alloc.h`'s raw `rolltui_mem_free`
 * just to hand one back. */
void rolltui_layout_actions_free(RolltuiLayoutAction* actions, size_t n);
/* ---- the built-in layouts, PARSED and cached (Phase 17 m3) ---------------------------------
 * `_builtin_json` above hands back the TEXT, which is the right shape for a caller that wants
 * to load it once. Both hosts want the LAYOUT, and want it on paths that run per resize and
 * per frame (`effective_layout()`, the `stacked` fallback), so parsing on every call would put
 * a JSON parse inside the frame — a thing `budget_test` exists to make impossible.
 *
 * So the cache is the library's, exactly as `Layout.cpp`'s was. Two properties of that C++ one
 * are carried over deliberately, both of which cost it a defect first:
 *   - it FILLS WHEN EMPTY rather than in a static initializer, because `rolltui_shutdown()`
 *     releases it and a once-only fill would leave this answering NULL forever after. Releasing
 *     a cache is only safe if the cache rebuilds.
 *   - it REGISTERS ITS RELEASER AT FILL TIME, not once per process, because `shutdown()` clears
 *     its own registry as it runs — so a register-once cache survives the second shutdown.
 * `_names` is in shipped order: "default" first (what a fresh install runs), then the table's.
 *
 * The result is a BORROW the library keeps until `rolltui_shutdown()`; NULL for a name that is
 * not a built-in. A built-in that does not parse cleanly is a programming error and aborts
 * rather than serving half a layout — the same call the C++ made. */
const RolltuiLayout* rolltui_layout_builtin(const char* name, size_t len);
/* Builds the JSON tree (an OWNED value the caller frees) — `layout_to_json_value`'s port.
 * `base`/`popups` are BORROWS (read-only: this never copies a tree merely to serialise it). */
RolltuiJsonValue* rolltui_layout_to_json_value(const char* name, size_t name_len, int min_width, int min_height,
                                               const RolltuiLayoutAction* actions, size_t actions_n,
                                               const RolltuiLayer* base, const RolltuiLayer* popups,
                                               size_t popups_n, const RolltuiLayoutHooks* hooks);
/* Dumps straight to TEXT, indent 2, REPLACING `*out` — `layout_to_json`'s port. */
void rolltui_layout_to_json_text(const char* name, size_t name_len, int min_width, int min_height,
                                const RolltuiLayoutAction* actions, size_t actions_n, const RolltuiLayer* base,
                                const RolltuiLayer* popups, size_t popups_n, const RolltuiLayoutHooks* hooks,
                                RolltuiStr* out);
/* Any layer, any node; NULL when absent. A BORROW valid until the tree is edited. */
RolltuiLayoutNode* rolltui_window_stack_find(const RolltuiWindowStack* s, const char* id, size_t len);
size_t rolltui_window_stack_focus_layer(const RolltuiWindowStack* s);
const char* rolltui_window_stack_captured(const RolltuiWindowStack* s, size_t* len);


/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_OPT_IN`. */
/* Draws one border with NO joining — for a widget that boxes its own content. `title` may
 * be NULL when `title_n` is 0. */
void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border,
                         RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                         int ambiguous_wide);
unsigned char rolltui_widget_kind_rule(size_t row);
const char* rolltui_widget_kind_source_is(size_t row, size_t* len);
/* A C caller's pair, for the same reason every owned type here has one — both strings empty
 * either way. C++ needs neither (see above) but they exist so a pure C caller has the same
 * capability. */
void rolltui_content_init(RolltuiContent* c);
void rolltui_content_release(RolltuiContent* c);
void rolltui_content_copy(RolltuiContent* to, const RolltuiContent* from);
int rolltui_content_equal(const RolltuiContent* a, const RolltuiContent* b);
int rolltui_layout_equal(const RolltuiLayout* a, const RolltuiLayout* b);
/* Releases an array `rolltui_layout_read_actions_key` filled (or any array of this shape) —
 * so a caller need not reach past this header for `rolltui_alloc.h`'s raw `rolltui_mem_free`
 * just to hand one back. */
void rolltui_layout_actions_free(RolltuiLayoutAction* actions, size_t n);
/* ---- the built-in layouts, PARSED and cached (Phase 17 m3) ---------------------------------
 * `_builtin_json` above hands back the TEXT, which is the right shape for a caller that wants
 * to load it once. Both hosts want the LAYOUT, and want it on paths that run per resize and
 * per frame (`effective_layout()`, the `stacked` fallback), so parsing on every call would put
 * a JSON parse inside the frame — a thing `budget_test` exists to make impossible.
 *
 * So the cache is the library's, exactly as `Layout.cpp`'s was. Two properties of that C++ one
 * are carried over deliberately, both of which cost it a defect first:
 *   - it FILLS WHEN EMPTY rather than in a static initializer, because `rolltui_shutdown()`
 *     releases it and a once-only fill would leave this answering NULL forever after. Releasing
 *     a cache is only safe if the cache rebuilds.
 *   - it REGISTERS ITS RELEASER AT FILL TIME, not once per process, because `shutdown()` clears
 *     its own registry as it runs — so a register-once cache survives the second shutdown.
 * `_names` is in shipped order: "default" first (what a fresh install runs), then the table's.
 *
 * The result is a BORROW the library keeps until `rolltui_shutdown()`; NULL for a name that is
 * not a built-in. A built-in that does not parse cleanly is a programming error and aborts
 * rather than serving half a layout — the same call the C++ made. */
const RolltuiLayout* rolltui_layout_builtin(const char* name, size_t len);
/* Builds the JSON tree (an OWNED value the caller frees) — `layout_to_json_value`'s port.
 * `base`/`popups` are BORROWS (read-only: this never copies a tree merely to serialise it). */
RolltuiJsonValue* rolltui_layout_to_json_value(const char* name, size_t name_len, int min_width, int min_height,
                                               const RolltuiLayoutAction* actions, size_t actions_n,
                                               const RolltuiLayer* base, const RolltuiLayer* popups,
                                               size_t popups_n, const RolltuiLayoutHooks* hooks);
/* Dumps straight to TEXT, indent 2, REPLACING `*out` — `layout_to_json`'s port. */
void rolltui_layout_to_json_text(const char* name, size_t name_len, int min_width, int min_height,
                                const RolltuiLayoutAction* actions, size_t actions_n, const RolltuiLayer* base,
                                const RolltuiLayer* popups, size_t popups_n, const RolltuiLayoutHooks* hooks,
                                RolltuiStr* out);
/* Any layer, any node; NULL when absent. A BORROW valid until the tree is edited. */
RolltuiLayoutNode* rolltui_window_stack_find(const RolltuiWindowStack* s, const char* id, size_t len);
size_t rolltui_window_stack_focus_layer(const RolltuiWindowStack* s);
const char* rolltui_window_stack_captured(const RolltuiWindowStack* s, size_t* len);

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
int rolltui_parse_dim(const char* text, size_t len, RolltuiDim* out);

size_t rolltui_dim_to_string(RolltuiDim d, char* out, size_t cap);

/* The same, plus a bare integer as cells — a size as TYPED. */
int rolltui_parse_size_text(const char* text, size_t len, RolltuiSplitSize* out);
size_t rolltui_split_size_to_string(RolltuiSplitSize s, char* out, size_t cap);

/* Lays out one tree inside `box` (a layer's resolved placement). Hidden nodes are omitted,
 * and a hidden ROOT emits nothing at all. */
void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx);

const char* rolltui_widget_kind_name(size_t row, size_t* len);

const char* rolltui_anchor_name(unsigned char a, size_t* len); /* "" when `a` is out of range */
int rolltui_anchor_from_name(const char* name, size_t len, unsigned char* out);
const char* rolltui_border_name(unsigned char b, size_t* len);
int rolltui_border_from_name(const char* name, size_t len, unsigned char* out);

/* Why `name` cannot be declared as an action ("" when it can) — Layout.hpp's three rules, as
 * ONE function, so the loader and the design editor refuse exactly the same names with
 * exactly the same words. REPLACES `*out`. Calls back through `hooks->is_library_scope` for
 * "which scopes are the library's" — that stays Bindings' vocabulary (rolltui_bindings.h's
 * own rule), never duplicated here. */
void rolltui_action_decl_problem(const char* name, size_t len, const RolltuiLayoutHooks* hooks, RolltuiStr* out);

void rolltui_action_list_copy(RolltuiActionList* to, const RolltuiActionList* from);

RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s);

/* ---- MOVED HERE BY PHASE 23: the loader's own carrier and the value lifecycle ---------------
 * `rolltui/rolltui.h` publishes `rolltui_load_layout_text` returning an OWNED `RolltuiLayout*`
 * and `rolltui_layout_new/_free/_clone`. What is below is how the library builds one: the
 * carrier the parser fills, and the by-value init/release/copy the opaque handle wraps. A test
 * or the studio's layout editor may include this header by name; a host may not. */
void rolltui_layout_init(RolltuiLayout* l);    /* zeroes; inits `base` */
void rolltui_layout_release(RolltuiLayout* l); /* frees name/actions/base/popups; zeroes */
void rolltui_layout_copy(RolltuiLayout* to, const RolltuiLayout* from);
void rolltui_loaded_layout_init(RolltuiLoadedLayout* l);
void rolltui_loaded_layout_release(RolltuiLoadedLayout* l);
void rolltui_loaded_layout_to_layout(RolltuiLoadedLayout* loaded, RolltuiLayout* out);
/* The pre-Phase-23 loader, still the implementation: fills a carrier the caller supplies. */
int rolltui_load_layout_text_into(const char* text, size_t len, RolltuiLoadedLayout* out,
                                  const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                                  const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LAYOUT_H */
