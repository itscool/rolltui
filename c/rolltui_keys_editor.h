#ifndef ROLLTUI_C_KEYS_EDITOR_H
#define ROLLTUI_C_KEYS_EDITOR_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, by rolltui's authoring tool, and by a suite
 * that opts in by including this header by name. */
/*
 * rolltui/c/rolltui_keys_editor.h — EDITING A BINDING TABLE, as a model with no terminal in it.
 *
 * A `RolltuiMenu` over the table being edited: scope > action > {add a chord, one
 * "remove <chord>" per chord, clear}. "Add a chord" puts the editor in CAPTURE — the next key
 * pressed becomes the chord, Escape cancels, a chord already held by another action of the same
 * scope MOVES and the status names where it came from, and a chord the Enter rule protects is
 * refused by name. There is no half-typed state to preview, so every change is a commit on the
 * undo stack the moment it lands.
 *
 * IT IS THE LIBRARY'S AND NOT A TOOL'S. A widget kind (`keys`, rolltui_widget_kinds.c) is a
 * model plus a draw and a handle, and this is the model; an app gets a keys editor by naming
 * the kind in a layout and binding a key, with no editor code of its own. Every app has a
 * binding table, the library ships the default one, and "which key does this" is exactly as
 * much a user's preference as "what colour is a warning".
 *
 * WHAT THIS MODEL DOES NOT DO, and who does it instead:
 *   - it never draws. The `keys` widget kind draws its menu, the store's label and its status
 *     line; a host that wants a different arrangement drives the same model.
 *   - it never writes a file. It hands back an OUTCOME — save as, load, reset, write shipped —
 *     and whoever mounted it decides what that means. The `keys` kind answers them against the
 *     preset store it is given.
 *   - it never decides WHICH ACTIONS EXIST. That is the table it was loaded with: an action is
 *     editable here only because something declared it, so a screen's own `app.*` actions are
 *     in the tree only when the table handed over is the one the app is running on.
 *
 * THE BOUNDARY'S RULES:
 *   1. **THE CALLER OWNS EVERY BUFFER**: text out goes into a caller's `RolltuiStr`, and the
 *      tables and action names come back as BORROWS with a stated window.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function except plain scalars and
 *      those borrows.
 *   3. **THE EDITOR IS OPAQUE.** Its undo-tracked value owns a whole binding table, and a
 *      struct with an owning pointer in it is a struct whose `=` in C++ is a synthesised deep
 *      copy and in C a shallow one — the same double free that shape has already produced here.
 *      Nothing crosses but scalars, borrows and ownership transfers that say so.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- what the editor asks of whoever mounted it --------------------------------------------
 * `Changed` means redraw; `Committed` means the committed table moved and should be written
 * where the app keeps it. The rest name a decision only the host can take. */
#define ROLLTUI_KEYS_EDIT_NONE 0
#define ROLLTUI_KEYS_EDIT_CHANGED 1
#define ROLLTUI_KEYS_EDIT_COMMITTED 2
#define ROLLTUI_KEYS_EDIT_SAVE_AS 3       /* `value`: the preset name typed */
#define ROLLTUI_KEYS_EDIT_WRITE_SHIPPED 4 /* `value`: the shipped preset chosen */
#define ROLLTUI_KEYS_EDIT_LOAD_PRESET 5   /* `value`: the preset chosen */
#define ROLLTUI_KEYS_EDIT_RESET_LOADED 6
#define ROLLTUI_KEYS_EDIT_CLOSED 7

typedef struct RolltuiKeysEditorOutcome {
  unsigned char kind ROLLTUI_DEFAULT(0);
  RolltuiStr value; /* OWNED — release with `rolltui_keys_editor_outcome_release` */
} RolltuiKeysEditorOutcome;

/* Frees `value` and zeroes. Safe on a zeroed outcome and on repeated calls. */
void rolltui_keys_editor_outcome_release(RolltuiKeysEditorOutcome* o);

typedef struct RolltuiKeysEditor RolltuiKeysEditor;

/* An editor over a CLONE of `baseline`, which becomes the undo stack's first entry. NULL means
 * a seeded table — the library's own actions declared and no chords — which is a table nobody
 * is running on and is here only so a mis-wired mount shows an empty editor rather than
 * crashing. Unlike a theme, a binding table has no built-in the editor could invent: which
 * actions exist is a fact about the screen that is running, so the baseline is always
 * somebody's. Free with `rolltui_keys_editor_free`. */
RolltuiKeysEditor* rolltui_keys_editor_new(const RolltuiBindings* baseline);
void rolltui_keys_editor_free(RolltuiKeysEditor* e); /* a no-op on NULL */

/* A new baseline; the undo stack restarts and any capture in progress is dropped. */
void rolltui_keys_editor_load(RolltuiKeysEditor* e, const RolltuiBindings* b);

/* The Load and Write-shipped choices' options. Both COPY; neither takes ownership. */
void rolltui_keys_editor_set_presets(RolltuiKeysEditor* e, const RolltuiStrList* names);
void rolltui_keys_editor_set_shipped(RolltuiKeysEditor* e, const RolltuiStrList* names, int may_write);

/* The table being edited, and the one the undo stack is standing on. Both BORROWS of the
 * editor's own storage, valid until the next event, load, undo, redo or replace. */
const RolltuiBindings* rolltui_keys_editor_current(const RolltuiKeysEditor* e);
const RolltuiBindings* rolltui_keys_editor_committed(const RolltuiKeysEditor* e);

/* Whether the next key will be taken as a chord, and for which action. The name BORROWS with
 * the same window; it is "" (and `*len` 0) when nothing is being captured. */
int rolltui_keys_editor_capturing(const RolltuiKeysEditor* e);
const char* rolltui_keys_editor_capturing_action(const RolltuiKeysEditor* e, size_t* len);

/* The menu to lay out and draw. A BORROW of the editor's own; it outlives every call but not
 * the editor. */
RolltuiMenu* rolltui_keys_editor_menu(RolltuiKeysEditor* e);

/* An event already routed to the editor. `nav` is the live bindings table — the table the HOST
 * is running on, so that rebinding a menu key mid-edit cannot strand the editor; the `menu`,
 * `edit` and `editor` scopes are read. `*out` is RESET first and is the caller's to release. */
void rolltui_keys_editor_handle(RolltuiKeysEditor* e, const RolltuiEvent* ev, const RolltuiBindings* nav,
                                RolltuiKeysEditorOutcome* out);

int rolltui_keys_editor_undo(RolltuiKeysEditor* e); /* 0 when there is nothing to undo */
int rolltui_keys_editor_redo(RolltuiKeysEditor* e);
size_t rolltui_keys_editor_undo_depth(const RolltuiKeysEditor* e);
size_t rolltui_keys_editor_redo_depth(const RolltuiKeysEditor* e);

/* Puts a whole table in as the new committed value (a reset). ADOPTS `b`. */
void rolltui_keys_editor_replace(RolltuiKeysEditor* e, RolltuiBindings* b);

/* One line, APPENDED to `out` (a fresh caller passes a zeroed `RolltuiStr`): what the editor is
 * doing — the capture prompt, a refusal, or the last thing that landed — and how deep undo
 * goes. */
void rolltui_keys_editor_status_line(const RolltuiKeysEditor* e, RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_KEYS_EDITOR_H */
