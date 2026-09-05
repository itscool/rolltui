#ifndef ROLLTUI_C_BINDINGS_H
#define ROLLTUI_C_BINDINGS_H
/*
 * rolltui/c/rolltui_bindings.h — CHORDS AND THE BINDING TABLE, as C (Phase 15 m3; the file
 * format joins them at Phase 17 m1, once `rolltui_json.h` existed to build it on).
 *
 * A chord's spelling ("ctrl+shift+left") and its help form ("Ctrl-Shift-Left"), the table an
 * action's keys live in and every edit to it, and — since m1 — the file format that reads and
 * writes a whole table at once. Every rule is stated in `rolltui/Bindings.hpp` and asserted in
 * `rolltui/tests/bindings_test.cpp`; none of it is repeated here.
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
 *
 * **Why a chord cannot be delivered, in English.** `rolltui_key_undeliverable_reason`
 * (rolltui_keys.h) classifies into a CODE for the same reason `library_actions()` above
 * stays put: "the C classifies and never carries a sentence" is that header's own rule, and
 * copying its six sentences here would be the vocabulary duplicated a second way. The file
 * loader below asks for the words through `RolltuiReasonFn` instead.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_str.h"

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

/* ---- the file format (Phase 17 m1): TEXT across the boundary, never a tree ----------------
 *
 * The loader and its report move here now that `rolltui_json.h` exists; what stays this
 * module's C++ (Bindings.cpp) is exactly what stayed out of THIS file at Phase 15 m3 for the
 * same reason: `library_actions()` and `migrated_action()` are a VOCABULARY (which action
 * names exist; which three were renamed), asked back through callbacks rather than moved,
 * matching `rolltui_bindings_undeclare_others`'s `RolltuiScopeFn` above. A fourth vocabulary
 * joins them for the same reason: the six English sentences for an undeliverable chord live
 * in `Keys.cpp` (`rolltui_keys.h` states why — "the C classifies and never carries a
 * sentence") and must not be copied here either.
 */

/* THE REPORT, transparent like `RolltuiAppProfileReport`: exactly `RolltuiStr` values in
 * GROWING AMORTISED arrays, one per `BindingsLoadReport` field. Zero-initialise before use. */
typedef struct RolltuiBindingsReport {
  RolltuiStr error; /* non-empty: unusable, and rolltui_bindings_load_json leaves `b` untouched */
  RolltuiStr* unknown_actions;
  size_t unknown_actions_n, unknown_actions_cap;
  RolltuiStr* bad_chords;
  size_t bad_chords_n, bad_chords_cap;
  /* Chords this terminal cannot deliver: kept in `b` and reported, never dropped. */
  RolltuiStr* undeliverable;
  size_t undeliverable_n, undeliverable_cap;
  RolltuiStr* conflicts;
  size_t conflicts_n, conflicts_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  /* A renamed action's rewrite, said once: "'<old>' \xE2\x86\x92 '<new>'" — not a problem
   * (`rolltui_bindings_report_clean` ignores it), so a host says it once, never asked to. */
  RolltuiStr* migrated;
  size_t migrated_n, migrated_cap;
} RolltuiBindingsReport;

void rolltui_bindings_report_release(RolltuiBindingsReport* r); /* frees everything; zeroes it */
void rolltui_bindings_report_set_error(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_unknown_action(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_bad_chord(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_undeliverable(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_conflict(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_bad_value(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_unknown_key(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_migrated(RolltuiBindingsReport* r, const char* s, size_t len);
int rolltui_bindings_report_clean(const RolltuiBindingsReport* r); /* `migrated` does not count */
/* Mirrors `BindingsLoadReport::summary()` exactly: "" when clean, else `error`, else
 * "bad: x; conflict: y; chord: z; undeliverable: w; unknown action: u; unknown: k" joined in
 * that order (never `migrated` — it is not a problem). Replaces `*out`. */
void rolltui_bindings_report_summary(const RolltuiBindingsReport* r, RolltuiStr* out);

/* Whether `legacy` (an action name) was renamed; when 1, the new name has been written into
 * `out` (a caller buffer of at least ROLLTUI_ACTION_NAME_MAX bytes) with `*out_len` set. The
 * three-row table itself stays in Bindings.cpp — this is only how the loader asks it, once
 * per key, exactly as the C++ loop already did. */
#define ROLLTUI_ACTION_NAME_MAX 64
typedef int (*RolltuiMigrateFn)(void* ctx, const char* legacy, size_t len, char* out, size_t* out_len);

/* The English for why a chord cannot be delivered, into a caller buffer of at least
 * ROLLTUI_UNDELIVERABLE_REASON_MAX bytes — deliberately not duplicated here (see the header
 * comment above this section). Returns the length written. */
#define ROLLTUI_UNDELIVERABLE_REASON_MAX 128
typedef size_t (*RolltuiReasonFn)(void* ctx, const RolltuiChord* k, unsigned char protocol, char* out, size_t cap);

/* ADDS a file's rows to `b`, which the caller constructs first (`Bindings::Bindings()` seeds
 * the library's own actions before calling this, exactly as the original C++ loop started
 * from `Bindings b;`) — so an action the caller already declared is never re-added, and its
 * row, if the file has one, simply gains chords. `deliver` is the protocol every chord in the
 * file is checked against. `is_library`/`migrate`/`reason` are the three vocabulary questions
 * above, asked back through callbacks.
 *
 * Returns 0 only when the file is fundamentally unusable (not a JSON object, or no "bindings"
 * object) — `report->error` says which, and `b` is left exactly as it was passed in. A lesser
 * problem is reported and `b` still gains whatever the file was good for, matching the C++
 * original's "a file with problems still loads" contract. `report` is reset (as if freshly
 * zero-initialised) on every call, success or failure. */
int rolltui_bindings_load_json(RolltuiBindings* b, const char* text, size_t len, unsigned char deliver_protocol,
                               RolltuiScopeFn is_library, void* library_ctx, RolltuiMigrateFn migrate,
                               void* migrate_ctx, RolltuiReasonFn reason, void* reason_ctx,
                               RolltuiBindingsReport* report);

/* Serialises to TEXT: {"name", "bindings": {action: [chord, ...], ...}}, 2-space indented with
 * a trailing newline (matches `json::dump(v, 2) + "\n"`). REPLACES `*out`. Every row is
 * written, declared or not — the file is the whole domain (rule 1), and an undeclared row's
 * chords must round-trip (Bindings.hpp's kept-and-inert rule). */
void rolltui_bindings_dump_json(const RolltuiBindings* b, const char* name, size_t name_len, RolltuiStr* out);


/* ---- THE LIBRARY'S CLOSED ACTION TABLE (Phase 17) ---------------------------------------
 * The 59 actions the library's own widgets look up, as data a consumer can enumerate. It
 * used to live only in `Bindings.cpp` on the rule that the C is TOLD which scopes are the
 * library's rather than storing the table — right about SCOPES, wrong about the TABLE: with
 * the shim gone, four consumers had each copied all 59 rows verbatim. Both accessors BORROW
 * into static storage, valid for the life of the process. */
size_t rolltui_library_action_count(void);
const char* rolltui_library_action_name(size_t i, size_t* len);
const char* rolltui_library_action_description(size_t i, size_t* len);

/* ---- DECLARING, SUGGESTING, AND THE SHIPPED TABLE (Phase 17) ----------------------------
 * These four were the last of this module's BEHAVIOUR to live only in C++. Every primitive
 * they stand on was already here; what was missing was the composition — and a composition a
 * pure-C host has to re-derive is a duplicate implementation waiting to disagree.
 *
 * `RolltuiToolAction` is the row a MOUNTED TOOL brings: its name, its English, and the chord
 * it SUGGESTS. A tool states its keys in code because no bindings file can — the shipped file
 * belongs to every host, and a row in it for a tool most hosts never mount is a key they
 * advertise and cannot press. */
/* FORWARD, never an include: `rolltui_layout.h` includes THIS header, so including it back
 * would be a cycle in which whichever of the two a translation unit reached first saw the
 * other's types undefined. `rolltui_bindings_declare` only ever takes a POINTER to one, so a
 * forward declaration is all it needs. Repeating the typedef identically is legal in both C11
 * and C++, which is what lets the definition stay in the module that owns it. */
typedef struct RolltuiLayoutAction RolltuiLayoutAction;

typedef struct RolltuiToolAction {
  const char* name;
  const char* description;
  const char* chord; /* "ctrl+q" — a SUGGESTION, never an override. NULL or "" for none. */
} RolltuiToolAction;

/* Is `scope` one the LIBRARY defines? True for exactly the scopes of the closed table above
 * (`input`, `transcript`, `menu`, `edit`, `stack`) — a scope is the library's because the
 * library DEFINES it, never because the library happens to ship the tool. Matches
 * `RolltuiScopeFn`, so it can be passed straight to `rolltui_bindings_undeclare_others`. */
int rolltui_bindings_library_scope(void* ctx, const char* scope, size_t len);

/* Which action of `scope` currently holds `chord`, across EVERY row — declared or not. That
 * breadth is the difference from `rolltui_bindings_action_for`, which answers only for
 * declared actions: a suggestion must not land on a chord an INERT row already holds, or
 * loading the screen that declares that row would silently steal the tool's key. Returns NULL
 * when the chord is free. BORROWS into `b`; valid until the next mutation. */
const char* rolltui_bindings_holder(const RolltuiBindings* b, const RolltuiChord* chord, const char* scope,
                                    size_t scope_len, size_t* out_len);

/* Installs each tool's suggested chord, into a GAP ONLY: a tool that already has a row is
 * skipped entirely, and a suggested chord another action of the same scope already holds is
 * dropped while the row is still created. That is what makes a bindings file always win —
 * including an EMPTY row, which is a user saying "no key for this". */
void rolltui_bindings_suggest(RolltuiBindings* b, const RolltuiToolAction* tools, size_t n);

/* AUTHORITATIVE over every non-library scope: after this call the declared non-library
 * actions are EXACTLY `declared` + `tools`, so loading another screen makes the last one's
 * inert. Merely ADDING would leave a key working because of a layout no longer running.
 *
 * THE ORDER INSIDE IS LOAD-BEARING AND IS WHY THESE ARE ONE CALL: the suggestions go in
 * FIRST, because declaring an action creates an empty row for it, and a suggestion made
 * afterwards would see that row and decline every time — a tool whose keys are all silently
 * unbound, which is exactly what the first cut of this did. */
void rolltui_bindings_declare(RolltuiBindings* b, const RolltuiLayoutAction* declared, size_t declared_n,
                              const RolltuiToolAction* tools, size_t tools_n);

/* THE SHIPPED DEFAULT TABLE: the embedded `default` bindings file, parsed and validated once
 * and cached for the life of the process (released by `rolltui_shutdown`). BORROWED — never
 * freed, never mutated by the caller; clone it to edit.
 *
 * IT ABORTS ON TWO BUILD MISTAKES, deliberately, because both are the LIBRARY's error and not
 * a user's, and both would otherwise ship:
 *   1. the file does not load cleanly — checked against the WEAKEST key protocol (Legacy) and
 *      not against whatever this terminal turned out to be, because the shipped file belongs
 *      to every host on every terminal. A chord that only works on kitty is fine in a user's
 *      own file and a build mistake in this one;
 *   2. it binds an action no shipped layout declares — a key every host advertises and cannot
 *      press. A mounted tool's chords come from the tool, so a tool row here stops the build. */
/* A table SEEDED with the library's own: the Enter rule set, and all 59 closed actions
 * declared. This is what every host actually starts from, and what `rolltui_bindings_load_json`
 * means by "the caller constructs first" — its contract is to ADD a file's rows to a table that
 * already knows the library's vocabulary, so a file naming `input.submit` is recognised rather
 * than reported as an unknown action. Loading the shipped file into a bare
 * `rolltui_bindings_new()` reports all 59 as unknown; that is not a defect in either call, it
 * is the seeding this one names.
 *
 * `rolltui_bindings_new` stays the EMPTY one, because a test that wants to watch rows appear
 * needs a table with nothing in it. */
RolltuiBindings* rolltui_bindings_new_seeded(void);

const RolltuiBindings* rolltui_bindings_default(void);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_BINDINGS_H */
