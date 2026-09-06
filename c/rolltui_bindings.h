#ifndef ROLLTUI_C_BINDINGS_H
#define ROLLTUI_C_BINDINGS_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
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


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_BINDINGS_H */
