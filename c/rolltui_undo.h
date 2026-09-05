#ifndef ROLLTUI_C_UNDO_H
#define ROLLTUI_C_UNDO_H
/*
 * rolltui/c/rolltui_undo.h — ONE UNDO STACK FOR EVERY EDITOR (theme, layout, keys), as
 * data. `rolltui/Undo.hpp` stated the rule once for both editors ("one undo-stack type,
 * not two") as a `std::vector<T>` of its own — the last pure-C++ template in the library
 * with no C form — and is deleted; this is that stack's C form. A stack of WHOLE
 * snapshots, same as before: cheap for a 44-style theme or a split tree, and the reason
 * undo works blind — there is no inverse operation to get wrong, only a value to put back.
 *
 * ---- WHY AN OPAQUE POINTER AND ONE CALLBACK, NOT A BYTE BLOB ---------------------------
 *
 * The obvious C shape for "a stack of T" is a caller-stated element size and a plain
 * `memcpy` into a growing buffer. It is wrong here: of the three snapshot types this is
 * instantiated with — a theme pair (two fixed-size structs, genuinely POD), a
 * key-binding table (a `unique_ptr` to a C handle) and a layout tree (`std::string` and
 * `std::vector` throughout) — a raw byte copy of the last two would duplicate the BITS
 * and alias the HEAP they point at: two "independent" snapshots that free the same
 * block. So a snapshot is opaque (`void*`), and the one thing the stack cannot know on
 * its own — how to free one — is supplied once at construction as `RolltuiUndoFreeFn`.
 *
 * **THERE IS NO `clone` CALLBACK, unlike `rolltui_presets.h`'s `RolltuiPresetDomain`**
 * (the library's other generic-value-over-a-C-boundary container, and the nearer
 * precedent than a byte blob). That one hands out an independent COPY of its working
 * value on every read, because a preset store keeps its own value alive while callers
 * mutate copies of it. Undo never does that: `current()` lends its OWN pointer (rule 3c
 * below), and every value that ever enters the stack is already a fresh one the caller
 * just built — commit(), reset() and replace_current() each TAKE OWNERSHIP of a snapshot
 * the caller allocated for this call and will not touch again. Reading what the three
 * real callers (theme, keys and layout editors) do with the stack settled this: all
 * three only ever push a whole snapshot and ask for the previous one back, never ask the
 * stack to duplicate one of its own, so there is nothing for a `clone` callback to do —
 * adding one would be exactly the over-engineering a generic container invites when the
 * actual callers are read second instead of first.
 *
 * `rolltui/tools/undo_stack.hpp` is the C++ side: a `template <class T> class UndoStack`
 * whose commit()/reset()/replace_current() heap-allocate a fresh `T` moved from the
 * caller's value (one move, the same one `std::vector<T>::push_back` already cost) and
 * hand this boundary the pointer; `RolltuiUndoFreeFn` is `T`'s own destructor, generated
 * once per `T` as a captureless lambda — the same idiom `rolltui/PresetStore.hpp` uses
 * for its `clone`/`destroy` pair, minus the half this stack has no use for.
 *
 * THE REST OF THE SHAPE IS A DIRECT PORT of `rolltui/Undo.hpp`'s five operations and
 * seven queries; see its (deleted) header comment for the worked example. Rule 1 (create/
 * release in a pair), rule 2 (nothing returned by value) and rule 3(c) (current() is a
 * BORROW, valid until the next mutating call on this stack) are the three of
 * `rolltui/rolltui.h`'s five that apply here.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Releases one snapshot the stack no longer holds — T's own destructor, generated once
 * per T by the C++ side (rolltui/tools/undo_stack.hpp). Fixed for the life of a stack:
 * one instance is always over one T, so this is supplied once at rolltui_undo_new and
 * never again. */
typedef void (*RolltuiUndoFreeFn)(void* snapshot);

typedef struct RolltuiUndoStack RolltuiUndoStack;

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
