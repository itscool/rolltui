#ifndef ROLLTUI_C_BINDINGS_H
#define ROLLTUI_C_BINDINGS_H
/*
 * rolltui/c/rolltui_bindings.h — CHORDS AND THE BINDING TABLE, as C (Phase 15 m3).
 *
 * A chord's spelling ("ctrl+shift+left") and its help form ("Ctrl-Shift-Left"), plus the
 * table an action's keys live in and every edit to it. Every rule is stated in
 * `rolltui/Bindings.hpp` and asserted in `rolltui/tests/bindings_test.cpp`; none of it is
 * repeated here.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14 and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER.** Every string OUT of this file is either written
 *      into a caller buffer whose bound is a constant here, or a BORROW valid until the
 *      table next changes.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function.
 *   3. **ONE DEFINITION**: a chord is `RolltuiChord` (rolltui_keys.h), the same struct the
 *      decoder produces and the deliverability model classifies.
 *
 * ---- WHAT THIS FILE DELIBERATELY DOES NOT KNOW ------------------------------------------
 *
 * **Which actions exist.** `library_actions()` is a table of names and English
 * descriptions, and `migrated_action()` is three renamed names; both stay in `Bindings.cpp`
 * for the reason m2 kept `Role` out of `rolltui_diff.h` — a vocabulary written down twice
 * is a second thing to drift. `studio_golden_test`'s grep control depends on the second one
 * living in exactly one file, which is a check that would have quietly weakened if the
 * table had been moved down here.
 *
 * **Which SCOPES are the library's.** `declare()` is authoritative over every non-library
 * scope, and "non-library" is a fact about `library_actions()`. So the table is TOLD, by a
 * predicate it calls back into, rather than deciding.
 *
 * **That Enter belongs to input.submit.** The RULE is this module's and lives here; the
 * NAME is the vocabulary and is handed over once, by `rolltui_bindings_set_enter_rule`.
 * The C then knows only "bare Enter in this scope belongs to this action and nothing
 * else", which is the whole of the rule and none of the words.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- chords ------------------------------------------------------------------------------ */

/* "ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space" — in any modifier order,
 * case-insensitive. 1 on success. A chord never carries an Unknown key's raw bytes: the
 * boundary takes the chord, which is what makes that structural (rolltui_keys.h). */
int rolltui_chord_parse(const char* text, size_t len, RolltuiChord* out);

/* The canonical spelling, and the help form. "" for an Unknown key. The longest either
 * produces is three modifiers plus the longest key name, so thirty-two is past every one of
 * them and the cap is a constraint on this file rather than on a caller's data. */
#define ROLLTUI_CHORD_STRING_MAX 32
size_t rolltui_chord_to_string(const RolltuiChord* k, char* out, size_t cap);
size_t rolltui_chord_display(const RolltuiChord* k, char* out, size_t cap);

/* ---- the table --------------------------------------------------------------------------- */
/* OWNED, LONG-LIVED (CLAUDE.md's strategy 4): one per `rolltui::Bindings`, which frees it.
 *
 * THREE PARALLEL LISTS, exactly as the C++ had: the DECLARED actions with their
 * descriptions, and the ROWS (action → chords). They are separate because a row may outlive
 * a declaration — a bindings file is global and the user's, so a row for another screen's
 * action is KEPT and inert (Bindings.hpp), which is only expressible if "has a row" and "is
 * declared" are two questions. */
typedef struct RolltuiBindings RolltuiBindings;
RolltuiBindings* rolltui_bindings_new(void);
void rolltui_bindings_free(RolltuiBindings* b);
RolltuiBindings* rolltui_bindings_clone(const RolltuiBindings* b);
/* Equal by the ROWS alone, which is what the C++ `operator==` compared: a declaration is
 * this screen's, a row is the user's file, and only the second is the domain's content. */
int rolltui_bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b);

/* The bare-Enter rule's subject, handed over once — see the note above. Passing a zero
 * length turns the rule off. */
void rolltui_bindings_set_enter_rule(RolltuiBindings* b, const char* action, size_t len);

/* ---- declarations ------------------------------------------------------------------------- */

/* A no-op when the action is already declared. Creates an empty ROW when there is none —
 * and never touches a row that exists, because declaring is what makes a kept row live and
 * must not throw its chords away. */
void rolltui_bindings_add_action(RolltuiBindings* b, const char* action, size_t alen, const char* desc, size_t dlen);
size_t rolltui_bindings_action_count(const RolltuiBindings* b);
/* A BORROW, valid until the table next changes. */
const char* rolltui_bindings_action_at(const RolltuiBindings* b, size_t i, size_t* len);
int rolltui_bindings_has(const RolltuiBindings* b, const char* action, size_t len);
const char* rolltui_bindings_description(const RolltuiBindings* b, const char* action, size_t len, size_t* out_len);

/* Answers whether a scope is one the library defines — the caller's fact, asked for by
 * `rolltui_bindings_undeclare_others` below. */
typedef int (*RolltuiScopeFn)(void* ctx, const char* scope, size_t len);
/* Removes every DECLARATION whose scope the predicate rejects, leaving every row alone.
 * This is `declare()`'s authoritative half, and the reason it is a separate call is that
 * the additive half is just `add_action` in a loop. */
void rolltui_bindings_undeclare_others(RolltuiBindings* b, RolltuiScopeFn is_library, void* ctx);

/* ---- rows ---------------------------------------------------------------------------------- */

size_t rolltui_bindings_row_count(const RolltuiBindings* b);
const char* rolltui_bindings_row_at(const RolltuiBindings* b, size_t i, size_t* len);
int rolltui_bindings_has_row(const RolltuiBindings* b, const char* action, size_t len);
/* Creates an empty row for an action nothing has declared — the kept-and-inert case. A
 * no-op when a row already exists. */
void rolltui_bindings_add_row(RolltuiBindings* b, const char* action, size_t len);

size_t rolltui_bindings_chord_count(const RolltuiBindings* b, const char* action, size_t len);
/* Chord `i` of the row, into `out`. 0 when there is none. */
int rolltui_bindings_chord_at(const RolltuiBindings* b, const char* action, size_t len, size_t i, RolltuiChord* out);
/* The action of `scope` this chord serves, or NULL: a row that nothing declares never
 * answers, and neither does a chord the ACTIVE protocol cannot deliver — both are kept in
 * the table and written back, so neither may claim a key. A BORROW, as above. */
const char* rolltui_bindings_action_for(const RolltuiBindings* b, const RolltuiChord* k, const char* scope,
                                        size_t scope_len, size_t* out_len);

/* ---- edits ----------------------------------------------------------------------------------- */

/* Binds `chord`; a chord already bound to another action of the same scope MOVES, and
 * `moved_from` (a BORROW, valid until the next change) says which. Refused (0) for an
 * undeclared action and for a chord the Enter rule protects. */
int rolltui_bindings_bind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord,
                          const char** moved_from, size_t* moved_len);
int rolltui_bindings_unbind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord);
/* Pushes a chord onto a row with NO rule checking, creating the row if there is none. The
 * loader's own, and separate from `bind` on purpose: a file's conflicts, its Enter rule and
 * its undeliverable chords are all REPORTED with a message before anything lands, so the
 * loader has already decided and needs a put rather than a policy. */
void rolltui_bindings_add_chord(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord);
void rolltui_bindings_clear(RolltuiBindings* b, const char* action, size_t len);
/* True when `chord` is the bare Enter the rule protects and `action` is not its subject —
 * the loader asks before it binds, so it can say WHY rather than just fail. */
int rolltui_bindings_breaks_enter_rule(const RolltuiBindings* b, const char* action, size_t len,
                                       const RolltuiChord* chord);

/* The scope of an action name: "input" of "input.submit", the whole name when there is no
 * dot. A BORROW of the caller's own bytes. */
const char* rolltui_bindings_scope_of(const char* action, size_t len, size_t* out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_BINDINGS_H */
