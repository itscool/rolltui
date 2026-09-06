#ifndef ROLLTUI_C_UNDO_H
#define ROLLTUI_C_UNDO_H
/*
 * rolltui/c/rolltui_undo.h — INTERNAL (Phase 20 m6/m7, 2026-09-06).
 *
 * RE-CREATED, the same way `rolltui_render.h` was in m1/m3 and under the same rule: a
 * header exists because a `.c` needs a declaration from it, and one that declares nothing
 * is deleted. Phase 19 m3 deleted this file when every declaration in it was public and
 * lived in the definition; Phase 20 moved this module's operations back to INTERNAL — no
 * CONSUMER reaches them, only the studio, its editors, or a suite that tests
 * implementation — so the `.c` needs its declarations again and the rule re-creates it.
 *
 * A suite that needs one includes this header BY NAME and lists itself in
 * `ROLLTUI_INTERNAL_TESTS` (rolltui/CMakeLists.txt). */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
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
