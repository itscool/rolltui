#ifndef ROLLTUI_C_BINDINGS_H
#define ROLLTUI_C_BINDINGS_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
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
 * ---- WHAT THIS FILE DELIBERATELY DOES NOT KNOW ----------------------------------------------
 *
 * **Which actions exist.** `rolltui_library_actions.c` owns that table — names and English
 * descriptions — and this file asks it through `RolltuiScopeFn`, for the reason m2 kept
 * `Role` out of `rolltui_diff.h`: a vocabulary written down twice is a second thing to
 * drift.
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

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Removes every DECLARATION whose scope the predicate rejects, leaving every row alone.
 * This is `declare()`'s authoritative half, and the reason it is a separate call is that
 * the additive half is just `add_action` in a loop. */
void rolltui_bindings_undeclare_others(RolltuiBindings* b, RolltuiScopeFn is_library, void* ctx);

/* True when `chord` is the bare Enter the rule protects and `action` is not its subject —
 * the loader asks before it binds, so it can say WHY rather than just fail. */
int rolltui_bindings_breaks_enter_rule(const RolltuiBindings* b, const char* action, size_t len,
                                       const RolltuiChord* chord);

void rolltui_bindings_report_set_error(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_unknown_action(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_bad_chord(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_undeliverable(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_conflict(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_bad_value(RolltuiBindingsReport* r, const char* s, size_t len);
void rolltui_bindings_report_add_unknown_key(RolltuiBindingsReport* r, const char* s, size_t len);

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



/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
/* The bare-Enter rule's subject, handed over once — see the note above. Passing a zero
 * length turns the rule off. */
void rolltui_bindings_set_enter_rule(RolltuiBindings* b, const char* action, size_t len);
/* A no-op when the action is already declared. Creates an empty ROW when there is none —
 * and never touches a row that exists, because declaring is what makes a kept row live and
 * must not throw its chords away. */
void rolltui_bindings_add_action(RolltuiBindings* b, const char* action, size_t alen, const char* desc, size_t dlen);
const char* rolltui_bindings_description(const RolltuiBindings* b, const char* action, size_t len, size_t* out_len);
size_t rolltui_bindings_row_count(const RolltuiBindings* b);
const char* rolltui_bindings_row_at(const RolltuiBindings* b, size_t i, size_t* len);
int rolltui_bindings_has_row(const RolltuiBindings* b, const char* action, size_t len);
/* Creates an empty row for an action nothing has declared — the kept-and-inert case. A
 * no-op when a row already exists. */
void rolltui_bindings_add_row(RolltuiBindings* b, const char* action, size_t len);
/* Pushes a chord onto a row with NO rule checking, creating the row if there is none. The
 * loader's own, and separate from `bind` on purpose: a file's conflicts, its Enter rule and
 * its undeliverable chords are all REPORTED with a message before anything lands, so the
 * loader has already decided and needs a put rather than a policy. */
void rolltui_bindings_add_chord(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord);
/* ---- THE LIBRARY'S CLOSED ACTION TABLE ------------------------------------------------------
 * The 59 actions the library's own widgets look up, as data a consumer can enumerate. It
 * used to live only in `Bindings.cpp` on the rule that the C is TOLD which scopes are the
 * library's rather than storing the table — right about SCOPES, wrong about the TABLE: with
 * the shim gone, four consumers had each copied all 59 rows verbatim. Both accessors BORROW
 * into static storage, valid for the life of the process. */
size_t rolltui_library_action_count(void);
const char* rolltui_library_action_name(size_t i, size_t* len);
const char* rolltui_library_action_description(size_t i, size_t* len);
/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
size_t rolltui_chord_to_string(const RolltuiChord* k, char* out, size_t cap);
size_t rolltui_chord_display(const RolltuiChord* k, char* out, size_t cap);

RolltuiBindings* rolltui_bindings_new(void);

/* Equal by the ROWS alone, which is what the C++ `operator==` compared: a declaration is
 * this screen's, a row is the user's file, and only the second is the domain's content. */
int rolltui_bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b);

size_t rolltui_bindings_action_count(const RolltuiBindings* b);

const char* rolltui_bindings_action_at(const RolltuiBindings* b, size_t i, size_t* len);

size_t rolltui_bindings_chord_count(const RolltuiBindings* b, const char* action, size_t len);

/* The HELP spelling: every chord bound to `action` that THIS TERMINAL can deliver, in display
 * form ("Ctrl-W, Alt-Backspace"), comma-separated. CLEARS `out`. The undeliverable filter is
 * what makes this the library's and not a loop a caller writes — it had three independent
 * implementations before Phase 17 m2a, the third written by an agent that could reach neither
 * of the other two. */
void rolltui_bindings_chords_text(const RolltuiBindings* b, const char* action, size_t alen, RolltuiStr* out);

/* Chord `i` of the row, into `out`. 0 when there is none. */
int rolltui_bindings_chord_at(const RolltuiBindings* b, const char* action, size_t len, size_t i, RolltuiChord* out);

int rolltui_bindings_unbind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord);

void rolltui_bindings_clear(RolltuiBindings* b, const char* action, size_t len);

/* The scope of an action name: "input" of "input.submit", the whole name when there is no
 * dot. A BORROW of the caller's own bytes. */
const char* rolltui_bindings_scope_of(const char* action, size_t len, size_t* out_len);

int rolltui_bindings_report_clean(const RolltuiBindingsReport* r);

/* `RolltuiReasonFn`-shaped, over `rolltui_key_undeliverable_reason`/`_text` (rolltui_keys.h) —
 * the same story one function over: the reason text has been C since Phase 15, and the only
 * thing keeping a host from passing it to the domain init was that nobody had written it in
 * this shape. Truncates at `cap`, like every other bounded writer here. */
size_t rolltui_undeliverable_reason_fn(void* ctx, const RolltuiChord* k, unsigned char protocol, char* out,
                                       size_t cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_BINDINGS_H */
