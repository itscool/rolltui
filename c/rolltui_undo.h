#ifndef ROLLTUI_C_UNDO_H
#define ROLLTUI_C_UNDO_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
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

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
