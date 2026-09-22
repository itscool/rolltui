#ifndef ROLLTUI_STUDIO_H
#define ROLLTUI_STUDIO_H
/*
 * rolltui/rolltui_studio.h — the preset stores' CREATE-AND-EDIT surface: rolltui's own authoring
 * tool's, and nobody else's.
 *
 * `rolltui.h`'s theme/layout/bindings stores are the whole of what an ORDINARY host needs: create
 * one, read what's loaded, switch to a different existing preset by name. Checked against every
 * real caller — dirktui, paint, roll — not one of them ever mutates a preset's content, saves a
 * new one, or imports a file; only `rolltui-studio` (and, through it, the theme/layout/keys
 * editors) does any of that. So the three functions that make a preset's content EDITABLE —
 * `_edit`, `_save_as`, `_add` — live here instead, in a header an ordinary host never includes and
 * never needs to know exists.
 *
 * THIS IS THE FIRST TENANT, NOT THE WHOLE HOUSE. The studio reaches into a good deal more of the
 * library's own internals today (`rolltui/CMakeLists.txt`'s `ROLLTUI_INTERNAL_OPT_IN`, a per-file
 * exemption with no further curation) — this file is meant to be where that reach is curated over
 * time, not a promise that it all lives here yet.
 *
 * `#include`s `rolltui.h` for the store types and value types; adds nothing beyond the twelve
 * functions below. Only `rolltui-studio` and its editors may include this header — enforced the
 * same way `public_header_test` already enforces that only they may include an internal header
 * from c/.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the theme store's CREATE-AND-EDIT surface ----------------------------------------------- */

/* An in-place edit of the theme's own colours, under the store's lock. `fn` sees the LIVE working value — not a copy handed back
 * and forth, the way `rolltui_theme_store_working()` reading and a later call writing would be — so one lock covers the whole edit.
 * `persist` 0 is what a live preview (the theme editor's, while a person is still typing) uses; a commit passes 1. */
typedef void (*RolltuiThemeEditFn)(RolltuiThemePresetValue* value, void* ctx);
void rolltui_theme_store_edit(RolltuiThemeStore* s, RolltuiThemeEditFn fn, void* ctx, int persist);

/* Saves the CURRENT working value under a new name. Always autosaves afterwards (a save-as is an explicit write, unlike `_load`/
 * `_edit`, which respect `persist`): there is no `persist` parameter because no caller wants 0. Returns one of the ROLLTUI_SAVE_*
 * codes; `err` (may be NULL) carries the sentence for every result but SAVED. */
int rolltui_theme_store_save_as(RolltuiThemeStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* Copies an external FILE in as a new preset (a theme someone sent you) — additive, never replacing an existing one. Named by
 * `as`, or by the file's own stem when `as` is empty. Same ROLLTUI_SAVE_* codes as `_save_as`, plus BAD_NAME for a file that does
 * not parse as a theme. */
int rolltui_theme_store_add(RolltuiThemeStore* s, const char* path, size_t path_len, const char* as, size_t as_len,
                            RolltuiStr* err);

/* REPLACES the working value wholesale — the theme editor's own commit, when it has built a whole new value rather than changing
 * one field of the live one (`_edit` is for that). TAKES OWNERSHIP of `v`: it must be a fresh, independently-owned value (from
 * `rolltui_theme_store_get`, say, or built by hand), never a pointer this store already owns. */
void rolltui_theme_store_set_working(RolltuiThemeStore* s, RolltuiThemePresetValue* v, int persist);

/* ---- the layout store's CREATE-AND-EDIT surface ------------------------------------------------ */

typedef void (*RolltuiLayoutEditFn)(RolltuiLayout* value, void* ctx);
void rolltui_layout_store_edit(RolltuiLayoutStore* s, RolltuiLayoutEditFn fn, void* ctx, int persist);
int rolltui_layout_store_save_as(RolltuiLayoutStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);
int rolltui_layout_store_add(RolltuiLayoutStore* s, const char* path, size_t path_len, const char* as, size_t as_len,
                             RolltuiStr* err);

/* REPLACES the working value wholesale, same as the theme store's. TAKES OWNERSHIP of `v`. */
void rolltui_layout_store_set_working(RolltuiLayoutStore* s, RolltuiLayout* v, int persist);

/* ---- the bindings store's CREATE-AND-EDIT surface ---------------------------------------------- */

typedef void (*RolltuiBindingsEditFn)(RolltuiBindings* value, void* ctx);
void rolltui_bindings_store_edit(RolltuiBindingsStore* s, RolltuiBindingsEditFn fn, void* ctx, int persist);
int rolltui_bindings_store_save_as(RolltuiBindingsStore* s, const char* name, size_t len, int overwrite,
                                   RolltuiStr* err);
int rolltui_bindings_store_add(RolltuiBindingsStore* s, const char* path, size_t path_len, const char* as,
                               size_t as_len, RolltuiStr* err);

/* REPLACES the working table wholesale, same as the theme store's. TAKES OWNERSHIP of `v`. */
void rolltui_bindings_store_set_working(RolltuiBindingsStore* s, RolltuiBindings* v, int persist);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_STUDIO_H */
