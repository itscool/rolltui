#ifndef ROLLTUI_C_MENU_H
#define ROLLTUI_C_MENU_H
/*
 * rolltui/c/rolltui_menu.h — THE MENU WIDGET, THE TYPED-FIELD RULES AND (Phase 17 m1) THE
 * FILE FORMAT (Phase 15 m5 for the rest).
 *
 * Level navigation, a typed filter, a breadcrumb, palette mode, and the seven input TYPES a
 * field can be — with the three-state rule that makes them work: the committed value, the
 * editing text and the preview are never collapsed into one. Every rule is stated in
 * `rolltui/Menu.hpp` and asserted in `rolltui/tests/menu_test.cpp`; none of it is repeated
 * here. The TREE it walks and builds is `rolltui_menu_tree.h`.
 *
 * ---- WHAT THIS BOUNDARY DELIBERATELY DOES NOT KNOW --------------------------------------
 *
 * **The shipped menu files, and the VALIDATOR REGISTRY.** The embedded table (which menus
 * SHIP, the `.json` files under `rolltui/presets/menus`) stays in `Menu.cpp` for the reason
 * m3's `Theme` loader did — it is a table of names, not an algorithm. The validator registry
 * is more interesting: a Text field's host validator is a `std::function` keyed by name, and
 * rather than move that map across, the C ASKS — one callback, "is a validator by this name
 * registered, and does it accept this text". The map stays where the callables are, which is
 * the same trade `rolltui_bindings.h` makes for "is this scope the library's". The JSON
 * loader below JOINS this file at Phase 17 m1, once `rolltui_json.h` existed to build it on
 * — unlike Theme's and Layout's, which stay in their C++ modules permanently (see those
 * headers), a menu FILE has no sibling algorithm on the other side of a boundary to entangle
 * it, so the whole walk moves.
 *
 * ~~**Which tree items name an action, and what a chord is called.** `item_actions()`,
 * `unknown_validators()` and `apply_shortcuts()` are TREE WALKS with no widget state in them,
 * so the shim does them over the same C tree — the C would gain nothing but a second place to
 * know what `Bindings::chords_text` means.~~
 * **RETRACTED 2026-09-05 (Phase 17 m3), and the reason is that its premise expired.**
 * `chords_text` moved to C in m2a — it is `rolltui_bindings_chords_text` one header over — so
 * the C gains no second place to know anything; it already knows. What the sentence was
 * actually protecting was a home for three walks, and *"the shim does them"* stops being an
 * answer the moment the shim is the thing being deleted. `tests/tui_frontend_test.cpp` calls
 * two of the three, so without this they would relocate into every consumer that needed
 * them — which is the failure the vocabulary rule names, one level up.
 *
 * The three are below as `rolltui_menu_apply_shortcuts`, `rolltui_menu_item_actions` and
 * `rolltui_menu_unknown_validators`. They take a TREE, not a menu, because that is what they
 * are about; the widget's own one-line versions are beside them.
 *
 * ---- THE BOUNDARY'S RULES, all inherited and none new -----------------------------------
 *
 *   1. **THE CALLER OWNS EVERY BUFFER**, including the two results with text in them: an
 *      `InputCheck` and a `MenuEvent` are filled into a caller's struct of `RolltuiStr`s.
 *   2. **TEXT OUT IS A BORROW** with a stated window — the filter, the breadcrumb and the
 *      visible list are valid until the menu next changes.
 *   3. **THE EDITOR IS BORROWED, NOT OWNED.** `rolltui::Menu` owns its `Input`; the C is
 *      handed the handle. One owner, and the C++ side keeps `editor()` returning the object
 *      a host already reads.
 *   4. **THIS FILE NAMES NO ROLE AND NO ACTION.** Both are handed in as structs.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_unicode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the typed-field rules, as pure functions ------------------------------------------- */

/* What `check_input` answers, into a caller's struct. `rolltui::InputCheck` IS this. */
typedef struct RolltuiInputCheck {
  unsigned char prefix_ok ROLLTUI_DEFAULT(0); /* the text could still become a valid value */
  unsigned char valid ROLLTUI_DEFAULT(0);     /* the text IS a valid value now */
  RolltuiStr reason;                          /* why not, "" when valid */
  RolltuiStr canonical;                       /* the value as it would be committed */
} RolltuiInputCheck;

void rolltui_input_check_release(RolltuiInputCheck* c);
/* Pure: the type's own rules. A Text validator is the HOST's and is not consulted here —
 * `rolltui_menu_handle` asks for it at commit through the callback below.
 *
 * IT TAKES A UNICODE SCRATCH because a Text field's `max_len` counts GRAPHEMES, and the
 * cluster walk needs somewhere to work. CLAUDE.md's strategy 3 as amended: that is a missing
 * handle, not a new strategy, and inventing a stack buffer with a spill here would be the
 * exact mistake Phase 14 m5 made once already. */
void rolltui_check_input(const RolltuiInputSpec* spec, const char* text, size_t len,
                         RolltuiUnicodeScratch* u, RolltuiInputCheck* out);
/* "1..100", "0.0..1.0 (2 digits)", "#rrggbb | 0-255 | none", … into a caller's string. */
void rolltui_input_hint(const RolltuiInputSpec* spec, RolltuiStr* out);
/* The type's name, and the reverse. A BORROW of a constant; NULL for an unknown name. */
const char* rolltui_input_type_name(unsigned char type, size_t* len);
int rolltui_input_type_from_name(const char* name, size_t len, unsigned char* out);

/* ---- what a handle() call answers --------------------------------------------------------- */

#define ROLLTUI_MENU_EVENT_NONE 0
#define ROLLTUI_MENU_EVENT_ACTIVATE 1
#define ROLLTUI_MENU_EVENT_TOGGLE 2
#define ROLLTUI_MENU_EVENT_CHOOSE 3
#define ROLLTUI_MENU_EVENT_INPUT 4
#define ROLLTUI_MENU_EVENT_CLOSED 5

typedef struct RolltuiMenuEvent {
  unsigned char kind ROLLTUI_DEFAULT(ROLLTUI_MENU_EVENT_NONE);
  RolltuiStr id;    /* the acted-on item (Choose: the Choice item) */
  RolltuiStr value; /* Choose: the option id; Input: the committed (canonical) text */
  unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle: the new state */
} RolltuiMenuEvent;

void rolltui_menu_event_release(RolltuiMenuEvent* e);

/* ---- the handle ----------------------------------------------------------------------------- */

typedef struct RolltuiMenu RolltuiMenu;
/* `editor` is BORROWED and must outlive the menu — `rolltui::Menu` owns it. */
/* The menu OWNS its editor (Phase 17): the typed-field editing a menu does is not optional,
 * and every caller was constructing one to hand in. `rolltui_menu_editor` borrows it back. */
RolltuiMenu* rolltui_menu_new(void);

/* The menu's editor, BORROWED, valid for the menu's life. */
RolltuiInput* rolltui_menu_editor(const RolltuiMenu* m);
void rolltui_menu_free(RolltuiMenu* m);

/* ---- the tree ---------------------------------------------------------------------------------- */
void rolltui_menu_set_root(RolltuiMenu* m, const RolltuiMenuItem* root); /* by COPY; also resets */
RolltuiMenuItem* rolltui_menu_root(RolltuiMenu* m);
/* Depth-first, any level; NULL when absent. */
RolltuiMenuItem* rolltui_menu_find(RolltuiMenu* m, const char* id, size_t len);
/* A Choice's options / a Submenu's items, by COPY, then the flat list and the selection are
 * rebuilt. */
int rolltui_menu_set_options(RolltuiMenu* m, const char* id, size_t len, const RolltuiMenuItemList* options);
/* THE THREE PLAIN SETTERS, each `find` plus one assignment, 0 when no item has that id. They
 * lived in the shim through Phase 15 on the argument that a caller could write `find` itself —
 * true, and the reason it stopped holding is rule 5: with `Windows::menu()` handing back a
 * `RolltuiMenu*`, BOTH hosts write the same three-line wrapper, which is the tell that the API
 * is wrong rather than the consumers (Phase 17 m1c). */
int rolltui_menu_set_value(RolltuiMenu* m, const char* id, size_t len, const char* value, size_t value_len);
int rolltui_menu_set_checked(RolltuiMenu* m, const char* id, size_t len, int checked);
int rolltui_menu_set_enabled(RolltuiMenu* m, const char* id, size_t len, int enabled);

/* ---- navigation state --------------------------------------------------------------------------- */
void rolltui_menu_reset(RolltuiMenu* m);
/* The path from the root down, as a BORROW valid until the menu next navigates. */
size_t rolltui_menu_path(const RolltuiMenu* m, const size_t** out);
const RolltuiMenuItem* rolltui_menu_level(const RolltuiMenu* m);
size_t rolltui_menu_selected(const RolltuiMenu* m);
const RolltuiMenuItem* rolltui_menu_selected_item(const RolltuiMenu* m);
/* The current level's children passing the filter (indices into the level's children), or in
 * palette mode indices into the flattened list. A BORROW, valid until the menu next changes. */
size_t rolltui_menu_visible(const RolltuiMenu* m, const size_t** out);
int rolltui_menu_scroll_first(const RolltuiMenu* m);
int rolltui_menu_scroll_visible(const RolltuiMenu* m);
const char* rolltui_menu_filter(const RolltuiMenu* m, size_t* len);
/* "settings › theme", into a caller's string. */
void rolltui_menu_breadcrumb(const RolltuiMenu* m, RolltuiStr* out);
int rolltui_menu_editing(const RolltuiMenu* m);
const char* rolltui_menu_edit_reason(const RolltuiMenu* m, size_t* len);
void rolltui_menu_set_palette(RolltuiMenu* m, int on);
int rolltui_menu_palette(const RolltuiMenu* m);
/* The flattened list, for the palette. `path` is a BORROW of the entry's index path. */
size_t rolltui_menu_flat_count(const RolltuiMenu* m);
const char* rolltui_menu_flat_label(const RolltuiMenu* m, size_t i, size_t* len);
size_t rolltui_menu_flat_path(const RolltuiMenu* m, size_t i, const size_t** out);

/* ---- events -------------------------------------------------------------------------------------- */

/* THE THIRTEEN ACTION NAMES this widget's two tables are keyed by, handed over once. */
typedef struct RolltuiMenuActions {
  const char* up;
  const char* down;
  const char* page_up;
  const char* page_down;
  const char* first;
  const char* last;
  const char* activate;
  const char* descend;
  const char* ascend;
  const char* back;
  const char* erase;
  /* the `edit` scope, while a typed field is open */
  const char* commit;
  const char* cancel;
  const char* step_up;
  const char* step_down;
  /* …and the INPUT widget's own thirty, because a typed field forwards every key it does not
   * claim to the editor. Handed through rather than duplicated: there is one table of input
   * action names in the whole library and it is `Input.cpp`'s. */
  const RolltuiInputActions* input;
} RolltuiMenuActions;

/* THE HOST'S TEXT VALIDATORS, asked rather than moved. Returns 1 when a validator by that
 * name is registered; `why` is filled (non-empty) when it REFUSES the text. */
typedef int (*RolltuiValidatorFn)(void* ctx, const char* name, size_t nlen, const char* text, size_t tlen,
                                  RolltuiStr* why);
void rolltui_menu_set_validator_fn(RolltuiMenu* m, RolltuiValidatorFn fn, void* ctx);

/* ---- the three tree walks (Phase 17 m3) ----------------------------------------------------
 * Pure over a `RolltuiMenuItem` tree, with no widget state in them — which is why they take the
 * ROOT rather than a `RolltuiMenu*`, and why a host can call them on a tree it has not mounted.
 *
 * `apply_shortcuts` fills each item's `shortcut` from the live table. An action NO layout
 * declares is INERT — the table keeps its chords but nothing can emit it — so it gets an EMPTY
 * shortcut rather than its chords: printing them would promise a key that cannot fire, which is
 * the exact lie this function exists to remove.
 *
 * `item_actions` and `unknown_validators` ENUMERATE through a sink, so the caller owns whatever
 * it collects into (rule 1) and nothing is returned by value (rule 2). `item_actions` reports a
 * PAIR per item, which is why it has its own two-string sink rather than `RolltuiPutFn`. */
void rolltui_menu_apply_shortcuts(RolltuiMenuItem* root, const RolltuiBindings* b);

typedef void (*RolltuiMenuActionFn)(void* ctx, const char* id, size_t id_len, const char* action, size_t action_len);
void rolltui_menu_item_actions(const RolltuiMenuItem* root, RolltuiMenuActionFn put, void* ctx);

/* The validator names this tree REFERENCES that nothing has registered, de-duplicated in
 * first-seen order. It asks through `RolltuiValidatorFn` — the SAME callback the widget's own
 * registry uses, called with empty text purely for its "is this name registered" answer — so a
 * host answers the question the one way it already answers it, rather than gaining a second
 * registry-shaped thing to keep in step. */
void rolltui_menu_unknown_validators(const RolltuiMenuItem* root, RolltuiValidatorFn is_known, void* ctx,
                                     RolltuiPutFn put, void* put_ctx);


/* The LIBRARY'S OWN fifteen (plus the input table they point at), so a consumer can call
 * `handle` without spelling them (Phase 17 m2a). This table used to be in an ANONYMOUS
 * namespace in `Menu.cpp`, which made `rolltui_menu_handle` uncallable from C and from any
 * translation unit that did not include `Menu.hpp` — `menu_test.cpp` had already hand-written
 * its own copy to get past it. BORROWS static storage. */
const RolltuiMenuActions* rolltui_menu_default_actions(void);

void rolltui_menu_handle(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* bindings,
                         const RolltuiMenuActions* actions, RolltuiMenuEvent* out);

/* ---- layout and drawing ----------------------------------------------------------------------------- */

typedef struct RolltuiMenuOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
#ifdef __cplusplus
  bool operator==(const RolltuiMenuOptions&) const = default;
#endif
} RolltuiMenuOptions;

void rolltui_menu_set_options_struct(RolltuiMenu* m, const RolltuiMenuOptions* o);
const RolltuiMenuOptions* rolltui_menu_options(const RolltuiMenu* m);
void rolltui_menu_layout(RolltuiMenu* m, RolltuiRect area);
void rolltui_menu_area(const RolltuiMenu* m, RolltuiRect* out);
int rolltui_menu_rows_for(const RolltuiMenu* m);

/* THE SEVEN ROLES A DRAW NEEDS, handed in as bytes. Plus the input widget's three, which the
 * editor's own draw takes. */
typedef struct RolltuiMenuRoles {
  unsigned char item;
  unsigned char selected;
  unsigned char breadcrumb;
  unsigned char shortcut;
  unsigned char text_muted;
  unsigned char warning;
  unsigned char scroll_marker;
} RolltuiMenuRoles;

void rolltui_menu_draw(const RolltuiMenu* m, RolltuiFrame* f, RolltuiDrawScratch* draw,
                       const RolltuiStyle* styles, const RolltuiMenuRoles* roles,
                       const RolltuiInputRoles* input_roles, int focused);

/* ---- the file format (Phase 17 m1): TEXT across the boundary, never a tree ---------------- */

/* THE REPORT, transparent like `RolltuiAppProfileReport` and `RolltuiBindingsReport`: exactly
 * `RolltuiStr` values in GROWING AMORTISED arrays, one per `MenuLoadReport` field.
 * Zero-initialise before use. */
typedef struct RolltuiMenuLoadReport {
  RolltuiStr error; /* non-empty: unusable, and rolltui_menu_parse_json returns 0 */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
} RolltuiMenuLoadReport;

void rolltui_menu_load_report_release(RolltuiMenuLoadReport* r); /* frees everything; zeroes it */
void rolltui_menu_load_report_set_error(RolltuiMenuLoadReport* r, const char* s, size_t len);
void rolltui_menu_load_report_add_unknown_key(RolltuiMenuLoadReport* r, const char* s, size_t len);
void rolltui_menu_load_report_add_bad_value(RolltuiMenuLoadReport* r, const char* s, size_t len);
int rolltui_menu_load_report_clean(const RolltuiMenuLoadReport* r);

/* Parses a WHOLE menu file's TEXT into `out`, which the caller owns (a stack `RolltuiMenuItem`
 * default-constructed in C++, or `rolltui_menu_item_init`'d in C) — this FILLS it in place
 * rather than handing back a fresh allocation, releasing whatever `out` held first, so the one
 * node every file has costs the caller nothing beyond what it already owns.
 *
 * Returns 0 only when `text` is fundamentally unusable (a JSON syntax error, or the root is
 * not an object) — `report->error` says which, and `out` is left freshly empty. Otherwise 1,
 * even when the file's own shape is wrong (a bad kind, a duplicate id, a root that is not a
 * submenu, ...) — those are reported and `out` gets whatever the file was good for, matching
 * `menu_from_json`'s "still returns" contract. `report` is reset on every call. */
int rolltui_menu_parse_json(const char* text, size_t len, RolltuiMenuItem* out, RolltuiMenuLoadReport* report);
/* Serialises `root`, 2-space indented with a trailing newline (matches `json::dump(v, 2) +
 * "\n"`). REPLACES `*out`. */
void rolltui_menu_dump_json(const RolltuiMenuItem* root, RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MENU_H */
