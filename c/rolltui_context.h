#ifndef ROLLTUI_C_CONTEXT_H
#define ROLLTUI_C_CONTEXT_H
/*
 * rolltui/c/rolltui_context.h — A SESSION'S OWN STATE. INTERNAL: a consumer sees
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

/* ---- the subsystems, each private to its own translation unit ---------------------------- */
typedef struct RolltuiKindRegistry RolltuiKindRegistry;
RolltuiKindRegistry* rolltui_kind_registry_new(void);
void rolltui_kind_registry_free(RolltuiKindRegistry* r); /* a no-op on NULL */

typedef struct RolltuiEffectRegistry RolltuiEffectRegistry;
RolltuiEffectRegistry* rolltui_effect_registry_new(void);
void rolltui_effect_registry_free(RolltuiEffectRegistry* r); /* a no-op on NULL */

/* THE BUILT-IN CACHES, and they are here because of contract point 4 rather than for tidiness:
 * a cached built-in belongs to the context that cached it. The bytes are identical for every
 * session — they are parsed from the same embedded `const` table — but the STORAGE is not
 * shared, so one context freeing it cannot leave another holding a dangling `RolltuiLayout*`. */
typedef struct RolltuiLayoutCache RolltuiLayoutCache;
RolltuiLayoutCache* rolltui_layout_cache_new(void);
void rolltui_layout_cache_free(RolltuiLayoutCache* c); /* a no-op on NULL */

/* The shipped default bindings table, parsed from the same embedded bytes. Here for contract
 * point 4 as well — a consumer BORROWS this table, so it may not outlive its session. */
typedef struct RolltuiBindings RolltuiBindings;
void rolltui_bindings_free(RolltuiBindings* b);

/* The library's three preset descriptors, built on demand. Here for contract point 4 in its
 * sharpest form: the LAYOUT descriptor borrows `rolltui_layout_shipped_default_actions`, which
 * is `layouts` above, so a process-wide descriptor would outlive the storage it points into. */
typedef struct RolltuiPresetDomains RolltuiPresetDomains;
RolltuiPresetDomains* rolltui_preset_domains_new(void);
void rolltui_preset_domains_free(RolltuiPresetDomains* p); /* a no-op on NULL */

/* What a PROGRAM configures once — its widget-kind factories, menus, key table, help scopes,
 * highlighter and the vocabularies its built-in kinds read back. It was the half of
 * `RolltuiWindows` that was never about what is on screen. Every widget a
 * `RolltuiWindows` owns BORROWS from here, which is why a context must outlive the windows made
 * against it — borrowers die first. */
typedef struct RolltuiWindowConfig RolltuiWindowConfig;
RolltuiWindowConfig* rolltui_window_config_new(void);
void rolltui_window_config_free(RolltuiWindowConfig* c); /* a no-op on NULL */
RolltuiWindowConfig* rolltui_context_window_config(RolltuiContext* ctx); /* made on first use */

struct RolltuiContext {
  RolltuiKindRegistry* kinds;     /* rolltui_layout.c   — the host WIDGET kinds, rung 2 */
  RolltuiEffectRegistry* effects; /* rolltui_effects.c  — the host EFFECT kinds, rung 2 */
  RolltuiLayoutCache* layouts;    /* rolltui_layout.c   — the built-ins, parsed once per session */
  RolltuiBindings* bindings;      /* rolltui_bindings.c — the shipped default table */
  RolltuiPresetDomains* presets;  /* rolltui_presets.c  — the library's three descriptors */
  RolltuiWindowConfig* window_config; /* rolltui_widgets.c — what a program configured once */
};


#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* ROLLTUI_C_CONTEXT_H */
