#ifndef ROLLTUI_C_LAYOUT_H
#define ROLLTUI_C_LAYOUT_H
/*
 * rolltui/c/rolltui_layout.h — PLACEMENT, COMPOSITION AND THE STACK (Phase 15 m5).
 *
 * The algorithm half of the layout module: what a Dim resolves to, how a Row divides its
 * width, which borders join, which window has focus and where an event goes. Every rule is
 * stated in `rolltui/Layout.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it
 * is repeated here. The DATA it walks is `rolltui_layout_tree.h`, which is C in both
 * configurations — the flag chooses this file or `LayoutCpp.cpp`, never the shape of a node.
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
 * **JSON, and the English in a report.** `load_layout`, `layout_to_json` and the built-ins
 * stay in `Layout.cpp` — the split m3 made for `Theme`, where only the colour engine crossed
 * and the loader never moved. The same holds for the SENTENCES a bad content string
 * produces: the registry below answers WHICH RUNG resolved a kind and WHAT RULE its source
 * follows, and the shim composes the message, because a vocabulary written down twice is a
 * second thing to drift.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- placement --------------------------------------------------------------------------- */

/* floor(fraction * extent + 1e-6) + cells. */
int rolltui_resolve_dim(RolltuiDim d, int extent);
/* The rule in Layout.hpp's header comment; absolute coordinates (parent.x/y added).
 *
 * INTO A CALLER'S RECT, NOT RETURNED — and the compiler is what said so. `RolltuiRect`
 * gained methods and default initializers the moment it became one definition, which stops
 * it being a C++98 POD, and Clang's `-Wreturn-type-c-linkage` then refuses to promise an ABI
 * for returning one from an `extern "C"` function. `rolltui_screen.h` wrote that rule down
 * for `RolltuiCell` in Phase 14 m2 and it applies here unchanged: suppressing the warning
 * would be asserting an ABI the compiler declines to promise. A rect PARAMETER by value is
 * fine — that has one answer every ABI agrees on for a trivially-copyable type. */
void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out);
/* The inner rect once the border is taken off (a None border takes nothing). */
void rolltui_inner_rect(RolltuiRect outer, unsigned char border, RolltuiRect* out);

/* ---- the split ----------------------------------------------------------------------------- */

/* Called once per node, in TREE ORDER (a container precedes its children). The caller
 * decides where they go — a vector, a filter, a single hit test. */
typedef void (*RolltuiResolvedSink)(void* ctx, const RolltuiResolvedNode* rn);

/* Lays out one tree inside `box` (a layer's resolved placement). Hidden nodes are omitted,
 * and a hidden ROOT emits nothing at all. */
void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx);

/* ---- drawing --------------------------------------------------------------------------------- */

/* THE THREE ROLES A COMPOSE NEEDS, handed in as bytes. This file names none of them. */
typedef struct RolltuiLayoutRoles {
  unsigned char border;
  unsigned char border_active;
  unsigned char title;
  unsigned char overlay;
} RolltuiLayoutRoles;

/* Draws one border with NO joining — for a widget that boxes its own content. `title` may
 * be NULL when `title_n` is 0. */
void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border,
                         RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                         int ambiguous_wide);

/* The host fills a window's content slot into `rn->inner` (already clipped). NULL draws
 * nothing, which is what a golden-frame harness wants. */
typedef void (*RolltuiSlotFn)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

/* WORKING MEMORY THE CALLER OWNS (CLAUDE.md strategy 3). Two screen-sized arm maps — the
 * joins written so far, and what was under the ring before this window cleared it — plus the
 * draw scratch a title needs. The C++ had the maps as `thread_local` in `compose_layer`,
 * which is the "nobody decided the scratch's lifetime" shape Phase 14's design lens names;
 * here the caller says how long they live. One buffer per ROLE, so the compose's maps and
 * the text walk's clusters cannot alias. */
typedef struct RolltuiComposeScratch RolltuiComposeScratch;
RolltuiComposeScratch* rolltui_compose_scratch_new(void);
void rolltui_compose_scratch_free(RolltuiComposeScratch* s);

/* Draws the nodes of ONE layer, in the order given. `styles` is `kRoleCount` styles — the
 * theme's table, a BORROW for the call. */
void rolltui_compose_layer(RolltuiFrame* f, const RolltuiResolvedNode* nodes, size_t count,
                           const RolltuiStyle* styles, const RolltuiLayoutRoles* roles, RolltuiSlotFn render,
                           void* ctx, int ambiguous_wide, RolltuiComposeScratch* scratch);

/* ---- the widget-kind registry ------------------------------------------------------------------ */
/* Rung 1 is a CLOSED table this file holds; rung 2 is what a host registered, which is a
 * process-wide retainer released by `rolltui::shutdown()`. What a kind's source is CALLED,
 * and every sentence a bad content produces, stay in the shim. */

#define ROLLTUI_SOURCE_REQUIRED 0
#define ROLLTUI_SOURCE_OPTIONAL 1
#define ROLLTUI_SOURCE_FORBIDDEN 2

/* Which rung answered, which is the whole of what the C decides. */
#define ROLLTUI_KIND_UNKNOWN 0  /* neither rung */
#define ROLLTUI_KIND_LIBRARY 1  /* rung 1, and `*ordinal` is its WidgetKind */
#define ROLLTUI_KIND_HOST 2     /* rung 2 */

/* Resolves a kind NAME. `rule` and `source_is` (a BORROW valid until the registry changes)
 * are filled for the two answering rungs; any out-param may be NULL. */
int rolltui_widget_kind_resolve(const char* name, size_t len, unsigned char* ordinal, unsigned char* rule,
                                const char** source_is, size_t* source_is_len);
/* The library's closed table, in table order. */
size_t rolltui_widget_kind_library_count(void);
const char* rolltui_widget_kind_library_name(size_t i, size_t* len);
unsigned char rolltui_widget_kind_library_rule(size_t i);
const char* rolltui_widget_kind_library_source_is(size_t i, size_t* len);
/* …and rung 2, in registration order. */
size_t rolltui_widget_kind_host_count(void);
const char* rolltui_widget_kind_host_name(size_t i, size_t* len);

/* Why a registration was refused, so the shim can say it in words. 0 is accepted. */
#define ROLLTUI_REGISTER_OK 0
#define ROLLTUI_REGISTER_EMPTY 1
#define ROLLTUI_REGISTER_HAS_COLON 2
#define ROLLTUI_REGISTER_IS_LIBRARY 3    /* rung 1 is never shadowed */
#define ROLLTUI_REGISTER_RULE_DIFFERS 4  /* already registered, with another source rule */
int rolltui_widget_kind_register(const char* name, size_t len, unsigned char rule, const char* source_is,
                                 size_t source_is_len);
void rolltui_widget_kind_clear(void);

/* Phase 9's bare slot names and Phase 10's `custom:` contents → their m3 spelling. A BORROW
 * of a constant; NULL when `legacy` is not one of them. */
const char* rolltui_migrated_content(const char* legacy, size_t len, size_t* out_len);

/* ---- the stack ------------------------------------------------------------------------------- */

typedef struct RolltuiWindowStack RolltuiWindowStack;

/* A stack with one empty base layer, which is what `WindowStack{}` has always meant. */
RolltuiWindowStack* rolltui_window_stack_new(void);
void rolltui_window_stack_free(RolltuiWindowStack* s);

/* Replaces the base layer by COPY. Popup layers stay; the base's focus id is kept when a
 * window with that id still exists. */
void rolltui_window_stack_set_base(RolltuiWindowStack* s, const RolltuiLayer* base);
RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s);
/* Takes `popup` BY MOVE and leaves the caller's empty — the ownership `push(Layer)` was
 * doing twice by value. */
void rolltui_window_stack_push(RolltuiWindowStack* s, RolltuiLayer* popup);
int rolltui_window_stack_pop(RolltuiWindowStack* s); /* 0 when only the base remains */
size_t rolltui_window_stack_depth(const RolltuiWindowStack* s);
const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, size_t i);
int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, size_t len);

/* Any layer, any node; NULL when absent. A BORROW valid until the tree is edited. */
RolltuiLayoutNode* rolltui_window_stack_find(const RolltuiWindowStack* s, const char* id, size_t len);

size_t rolltui_window_stack_focus_layer(const RolltuiWindowStack* s);
const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s);
void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, size_t len);
void rolltui_window_stack_cycle_focus(RolltuiWindowStack* s, int backwards);

/* Every layer resolved against `screen`, in draw order, `focused` set on the one focused
 * window. */
void rolltui_window_stack_resolve(const RolltuiWindowStack* s, RolltuiRect screen, RolltuiResolvedSink emit,
                                  void* ctx);
void rolltui_window_stack_compose(const RolltuiWindowStack* s, RolltuiFrame* f, RolltuiRect screen,
                                  const RolltuiStyle* styles, const RolltuiLayoutRoles* roles,
                                  RolltuiSlotFn render, void* ctx, int ambiguous_wide,
                                  RolltuiComposeScratch* scratch);

/* THE THREE ACTION NAMES, handed in — this file knows the RULES and none of the words. */
typedef struct RolltuiStackActions {
  const char* close_popup;
  const char* focus_next;
  const char* focus_prev;
} RolltuiStackActions;

#define ROLLTUI_ROUTE_DELIVER 0
#define ROLLTUI_ROUTE_CLOSED_POPUP 1
#define ROLLTUI_ROUTE_FOCUS_MOVED 2
#define ROLLTUI_ROUTE_DROPPED 3

/* Routes one event. `window` is filled with the target id — a COPY, because the
 * ClosedPopup case names a layer this call has already freed. */
unsigned char rolltui_window_stack_route(RolltuiWindowStack* s, const RolltuiEvent* e, RolltuiRect screen,
                                         const RolltuiBindings* bindings, const RolltuiStackActions* actions,
                                         RolltuiStr* window);
/* The window a press captured the pointer for, until its release ("" when none). */
const char* rolltui_window_stack_captured(const RolltuiWindowStack* s, size_t* len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LAYOUT_H */
