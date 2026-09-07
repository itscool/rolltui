#ifndef ROLLTUI_C_CONTEXT_H
#define ROLLTUI_C_CONTEXT_H
/*
 * rolltui/c/rolltui_context.h — A SESSION'S OWN STATE (Phase 25 m2). INTERNAL: a consumer sees
 * only the opaque `RolltuiContext` and `rolltui_context_new`/`_free` in `rolltui/rolltui.h`.
 *
 * THE STRUCT IS A CLOSED SET OF SUBSYSTEM POINTERS, one per registry or cache that used to be a
 * process-wide static, and `rolltui_context_free` releases each by name. That is deliberately
 * NOT a hook list: `rolltui_on_shutdown` existed so a subsystem could register a releaser for
 * state nobody could see, and a context makes the set VISIBLE — the same "a closed table plus
 * one explicit way to extend it" idiom the widget kinds and the allocation strategies use. A new
 * subsystem adds a member here and a line to `_free`, and `globals.inc` stops it doing anything
 * else.
 *
 * Each subsystem keeps its OWN type private to its own `.c` and exposes only `_new`/`_free`, so
 * this header never learns what a kind registry is made of.
 */
#include "rolltui/rolltui.h" /* the definition first, as every internal header does */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the subsystems, each private to its own translation unit ---- */
typedef struct RolltuiKindRegistry RolltuiKindRegistry;
RolltuiKindRegistry* rolltui_kind_registry_new(void);
void rolltui_kind_registry_free(RolltuiKindRegistry* r); /* a no-op on NULL */

struct RolltuiContext {
  RolltuiKindRegistry* kinds; /* rolltui_layout.c — the host widget kinds, rung 2 */
};

/* ---- THE TRANSITIONAL RUNG, and it is the ONE new global this phase adds ---------------------
 * Every no-context entry point still in the API forwards to this. It exists so the migration
 * lands in GREEN COMMITS, one subsystem at a time, instead of one unverifiable change — the same
 * discipline Phase 14 used with `-DROLLTUI_C`, where both implementations satisfied the same
 * tests and a bad module was reverted by deleting a filename.
 *
 * IT IS RECORDED IN `globals.inc` WITH ITS REMOVAL CONDITION rather than hidden: when the last
 * no-context entry point goes, this goes with it, and the boundary test's PROCESS count falls by
 * one. A transitional global that nothing is committed to removing is just a global. */
RolltuiContext* rolltui_context_default(void);

/* Releases the default and lets it be rebuilt on next use — called by `rolltui_shutdown()`, so
 * a caller that never asked for a context is not left holding its storage. */
void rolltui_context_default_release(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* ROLLTUI_C_CONTEXT_H */
