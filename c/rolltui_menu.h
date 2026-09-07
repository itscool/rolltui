#ifndef ROLLTUI_C_MENU_H
#define ROLLTUI_C_MENU_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_menu.h — THE MENU WIDGET, THE TYPED-FIELD RULES AND THE
 * FILE FORMAT (Phase 15 m5 for the rest).
 *
 * Level navigation, a typed filter, a breadcrumb, palette mode, and the seven input TYPES a
 * field can be — with the three-state rule that makes them work: the committed value, the
 * editing text and the preview are never collapsed into one. Every rule is stated in
 * `rolltui/Menu.hpp` and asserted in `rolltui/tests/menu_test.cpp`; none of it is repeated
 * here. The TREE it walks and builds is `rolltui_menu_tree.h`.
 *
 * ---- WHAT THIS BOUNDARY DELIBERATELY DOES NOT KNOW ------------------------------------------
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
 * **RETRACTED 2026-09-05, and the reason is that its premise expired.**
 * `chords_text` moved to C in m2a — it is `rolltui_bindings_chords_text` one header over — so
 * the C gains no second place to know anything; it already knows. What the sentence was
 * actually protecting was a home for three walks, and *"the shim does them"* stops being an
 * answer the moment the shim is the thing being deleted. `tests/tui_frontend_test.cpp` calls
 * two of the three, so without this they would relocate into every consumer that needed
 * them — which is the failure the vocabulary rule names, one level up.
 *
 * Two are below as `rolltui_menu_apply_shortcuts` and `rolltui_menu_item_actions` (the third,
 * `rolltui_menu_unknown_validators`, was reached by nothing and went in Phase 19 m3). They take
 * a TREE, not a menu, because that is what they are about; the widget's own one-line versions
 * are beside them.
 *
 * ---- THE BOUNDARY'S RULES, all inherited and none new ---------------------------------------
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

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_unicode.h"

#ifdef __cplusplus
extern "C" {
#endif
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
/* The type's name, and the reverse. A BORROW of a constant; NULL for an unknown name. */
const char* rolltui_input_type_name(unsigned char type, size_t* len);
int rolltui_input_type_from_name(const char* name, size_t len, unsigned char* out);

int rolltui_menu_scroll_first(const RolltuiMenu* m);
int rolltui_menu_scroll_visible(const RolltuiMenu* m);
/* The flattened list, for the palette. `path` is a BORROW of the entry's index path. */
size_t rolltui_menu_flat_count(const RolltuiMenu* m);
const char* rolltui_menu_flat_label(const RolltuiMenu* m, size_t i, size_t* len);

/* The validator names this tree REFERENCES that nothing has registered, de-duplicated in
 * first-seen order. It asks through `RolltuiValidatorFn` — the SAME callback the widget's own
 * registry uses, called with empty text purely for its "is this name registered" answer — so a
 * host answers the question the one way it already answers it, rather than gaining a second
 * registry-shaped thing to keep in step. */

const RolltuiMenuOptions* rolltui_menu_options(const RolltuiMenu* m);

void rolltui_menu_load_report_set_error(RolltuiMenuLoadReport* r, const char* s, size_t len);
void rolltui_menu_load_report_add_unknown_key(RolltuiMenuLoadReport* r, const char* s, size_t len);
void rolltui_menu_load_report_add_bad_value(RolltuiMenuLoadReport* r, const char* s, size_t len);


/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
/* ========================================================================================
 * menu — the menu widget
 * ======================================================================================== */
/* "1..100", "0.0..1.0 (2 digits)", "#rrggbb | 0-255 | none", … into a caller's string. */
void rolltui_input_hint(const RolltuiInputSpec* spec, RolltuiStr* out);
/* The path from the root down, as a BORROW valid until the menu next navigates. */
size_t rolltui_menu_path(const RolltuiMenu* m, const size_t** out);
size_t rolltui_menu_selected(const RolltuiMenu* m);
/* The current level's children passing the filter (indices into the level's children), or in
 * palette mode indices into the flattened list. A BORROW, valid until the menu next changes. */
size_t rolltui_menu_visible(const RolltuiMenu* m, const size_t** out);
const char* rolltui_menu_filter(const RolltuiMenu* m, size_t* len);
int rolltui_menu_palette(const RolltuiMenu* m);
void rolltui_menu_set_validator_fn(RolltuiMenu* m, RolltuiValidatorFn fn, void* ctx);
void rolltui_menu_dump_json(const RolltuiMenuItem* root, RolltuiStr* out);

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
RolltuiMenu* rolltui_menu_new(void);

/* The menu's editor, BORROWED, valid for the menu's life. */
RolltuiInput* rolltui_menu_editor(const RolltuiMenu* m);
void rolltui_menu_free(RolltuiMenu* m);

void rolltui_menu_set_root(RolltuiMenu* m, const RolltuiMenuItem* root); /* by COPY; also resets */

int rolltui_menu_set_checked(RolltuiMenu* m, const char* id, size_t len, int checked);

const RolltuiMenuItem* rolltui_menu_level(const RolltuiMenu* m);
const RolltuiMenuItem* rolltui_menu_selected_item(const RolltuiMenu* m);

void rolltui_menu_breadcrumb(const RolltuiMenu* m, RolltuiStr* out);
int rolltui_menu_editing(const RolltuiMenu* m);
const char* rolltui_menu_edit_reason(const RolltuiMenu* m, size_t* len);

void rolltui_menu_set_options_struct(RolltuiMenu* m, const RolltuiMenuOptions* o);
void rolltui_menu_layout(RolltuiMenu* m, RolltuiRect area);

void rolltui_menu_draw(const RolltuiMenu* m, RolltuiFrame* f, RolltuiDrawScratch* draw,
                       const RolltuiStyle* styles, const RolltuiMenuRoles* roles,
                       const RolltuiInputRoles* input_roles, int focused);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MENU_H */
