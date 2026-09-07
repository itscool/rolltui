#ifndef ROLLTUI_C_UNDO_H
#define ROLLTUI_C_UNDO_H
/*
 * rolltui/c/rolltui_undo.h — INTERNAL.
 *
 * The library's own declarations for this module. The PUBLIC API is `rolltui/rolltui.h`,
 * which declares everything a consumer may call; nothing below is promised to one, so its
 * shape can change without breaking a host.
 *
 * A suite that needs an internal declaration includes this header BY NAME and lists itself
 * in `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
/* Starts EMPTY — nothing to undo or redo until reset() seeds a baseline, the same state
 * `rolltui::UndoStack<T>`'s default constructor left. `limit` bounds how many snapshots
 * are kept; past it, commit() drops the OLDEST rather than refusing the newest (0 is a
 * degenerate but well-defined bound: every commit evicts back down to one entry). */
RolltuiUndoStack* rolltui_undo_new(size_t limit, RolltuiUndoFreeFn free_fn);
void rolltui_undo_free(RolltuiUndoStack* u); /* no-op on NULL (rule 1) */

/* Restarts from a new baseline (a load, a reset): every existing snapshot is freed and
 * `baseline` becomes the sole entry, at position 0. TAKES OWNERSHIP of `baseline`. */
void rolltui_undo_reset(RolltuiUndoStack* u, void* baseline);
/* Records a new current value: the redo branch (if any) is freed and dropped, and the
 * OLDEST entry is freed too if this push carries the stack past its limit. TAKES
 * OWNERSHIP of `value`. */
void rolltui_undo_commit(RolltuiUndoStack* u, void* value);
/* Overwrites the CURRENT snapshot in place: no new step, the redo branch untouched (the
 * caret-move case `rolltui/Undo.hpp` was written for — a move must not become its own
 * undo step, yet must be what the next real edit's "before" snapshot restores to). TAKES
 * OWNERSHIP of `value`; the snapshot it replaces is freed. */
void rolltui_undo_replace_current(RolltuiUndoStack* u, void* value);

int rolltui_undo_undo(RolltuiUndoStack* u); /* 0/1: false at the baseline */
int rolltui_undo_redo(RolltuiUndoStack* u); /* 0/1: false with nothing committed since */
int rolltui_undo_can_undo(const RolltuiUndoStack* u);
int rolltui_undo_can_redo(const RolltuiUndoStack* u);
size_t rolltui_undo_undo_depth(const RolltuiUndoStack* u); /* steps back to the baseline */
size_t rolltui_undo_redo_depth(const RolltuiUndoStack* u); /* steps forward to the newest commit */
int rolltui_undo_empty(const RolltuiUndoStack* u);         /* true only before the first reset() */

/* The snapshot at the current position — the value before the last commit, once undo()
 * has walked back. BORROWED (rule 3c): valid until the next reset/commit/
 * replace_current/undo/redo/free call on THIS stack, and NULL only when empty(). */
const void* rolltui_undo_current(const RolltuiUndoStack* u);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_UNDO_H */
