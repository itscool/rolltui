#ifndef ROLLTUI_C_PRESETS_H
#define ROLLTUI_C_PRESETS_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_presets.h — THE PRESET MECHANICS, as C.
 *
 * The five rules of `rolltui/Presets.hpp`, once, for every domain: a preset is the whole
 * domain, there is exactly one working copy and it autosaves, a preset is a named read-only
 * snapshot, "(modified)" is BY COMPARISON, and a shipped preset is read-only unless the
 * editor says otherwise. Every one of them is stated there and asserted over all three
 * domains in `rolltui/tests/presets_test.cpp`; none of it is repeated here.
 *
 * ---- THIS IS THE MILESTONE'S ONE HONEST ANSWER TO "WHAT DOES C COST FOR A GENERIC
 *      CONTAINER" ------------------------------------------------------------------------
 *
 * `PresetStore<Domain>` is the only place `rolltui` uses a template for real, and the three
 * domains it is instantiated with have nothing in common: a `ThemePreset` is a JSON object
 * plus two strings, a `Layout` is a window tree, a `Bindings` is a table. C has no way to
 * say "the same mechanics over a type I am given", so the type becomes a `void*` and
 * everything the mechanics need to DO with it becomes a function pointer. That trade is the
 * measurement, and it is worth stating exactly:
 *
 *   what the template got for free            what C has to be told
 *   ------------------------------------      -------------------------------------------------
 *   `Value working_` — a member               `void* working` plus `clone` and `destroy`
 *   `working_ == origin_content_`             `equal`
 *   `D::parse(json, report)`                  `parse`, over TEXT rather than a parsed tree
 *   `D::to_json(v, name)`                     `to_json`, appending through a callback
 *   `D::kind` / `working_file` / `subdir`     four (pointer, length) pairs
 *   `PresetLoadReport&`                       an opaque pointer and SEVEN more callbacks
 *
 * The last row is the one that surprises. The report is not the domain's VALUE — it is the
 * mechanics' own output — and in C++ it is simply a reference to a struct with vectors on
 * it. Here every one of the five things the mechanics do to a report (reset it, set its
 * error, read its error back to wrap a path around, add a note, prefix the notes with a
 * path) has to be a function pointer, because the words are `std::string`s and the store
 * cannot make one. The sixth and seventh — MAKE one and UNMAKE it — matter as much as the
 * five, and their absence is paid for at every call site: the store needs a report of its own
 * for two throwaway parses (the shipped cache, the origin re-read at start), and without a way
 * to make one it has to take a SECOND report from the caller. That second parameter reached
 * twenty-six call sites across five consumers before the pair existed.
 *
 * **WHAT IS NOT TOLD IS AS INTERESTING AS WHAT IS.** The JSON never crosses: `parse` takes
 * TEXT and hands back an owned value, so `json::Value` — a whole module that has not ported
 * — stays entirely on the other side, and the store gains a property the template did not
 * have, that it never sees a parsed tree it has no use for.
 *
 * THE BOUNDARY'S RULES:
 *   1. **THE CALLER OWNS EVERY BUFFER**, including every VALUE this file hands back: a
 *      `void*` out of here is a clone the caller destroys with `domain->destroy`, which is
 *      exactly what `PresetStore::working()` returning by value already meant.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function; text out goes
 *      through a `put` callback or into a caller-sized buffer.
 *   3. **TEXT OUT IS A BORROW WITH A STATED WINDOW** where it is not copied: `origin()` and
 *      `last_error()` lend the store's own bytes, valid until the store next changes.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif
/* The stem of every "*.json" in `dir`, sorted. REPLACES `*out`. */
void rolltui_preset_json_names_in(const char* dir, size_t dir_len, RolltuiStrList* out);

/* ---- THE STORE'S ONE-LINE PROBLEM SENTENCE --------------------------------------------------
 * `rolltui::PresetLoadReport::summary()`'s composition, moved with the words it composes: the
 * error alone when there is one, else every problem as "<prefix>: <text>" joined with "; ", in
 * a fixed order (this store's own bad values and unknown keys, then the colours part, then the
 * layout part, then the bindings part). Seven call sites in `studio.cpp` and one in
 * `presets_test.cpp` draw it, so the composition lives here with the words it composes.
 *
 * The three nested reports are the DOMAIN reports the C already defines; any of them may be
 * NULL, meaning "this store has no such part". APPENDS to `out`, and appends nothing when
 * every part is clean. */
void rolltui_preset_report_summary(const RolltuiStr* error, const RolltuiStr* bad_values, size_t bad_values_n,
                                   const RolltuiStr* unknown_keys, size_t unknown_keys_n,
                                   const RolltuiThemeReport* colours, const RolltuiLayoutReport* layout,
                                   const RolltuiBindingsReport* bindings, RolltuiStr* out);

/* The colours-only PARTIAL form (Theme.hpp's plain file: "roles" at the top, no "colours"
 * key) — mode/depth are not touched at all (the C++ shim keeps the rest of `working` itself,
 * per `ThemeDomain::parse_partial`'s own contract). Returns 0 with `report->error` EMPTY when
 * `root` is not partial-shaped at all (not an object, already has "colours", or has no
 * "roles") — the generic mechanics then fall back to `rolltui_theme_preset_parse` on the SAME
 * text (`rolltui_presets.c`'s own `get_locked`: "NULL when the file is not partial, and
 * `parse` is then used"). Returns 0 with `report->error` SET when `root` looked partial but
 * the colours failed to load (UNPREFIXED — unlike `_parse`'s "colours: " prefix, the whole
 * file IS the colours object here, so there is no second thing to name) — no fallback in this
 * case; the caller's own file was simply broken. Returns 1 with `*out_colours` borrowing
 * `root` itself and a note in `report` on success. Only DARK is validated here — mirroring
 * `ThemeDomain::parse_partial`'s own asymmetry with `_parse`'s two-mode check, ported as
 * found rather than corrected. */
int rolltui_theme_preset_parse_partial(const RolltuiJsonValue* root, const RolltuiThemeVocab* vocab,
                                       const RolltuiJsonValue** out_colours, RolltuiThemePresetReport* report);


/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
int rolltui_preset_looks_like_path(const char* s, size_t len);
int rolltui_preset_valid_name(const char* name, size_t len);

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
void rolltui_preset_domain_release(RolltuiPresetDomain* d);

/* The only write anyone does (rule 2): replace the working copy. TAKES OWNERSHIP of `v`. */
void rolltui_preset_store_set_working(RolltuiPresetStore* s, void* v, int persist);

/* Fills `out` with the Theme domain's mechanics — nothing here is a paraphrase of
 * `rolltui_theme_preset_parse`/`_parse_partial`, it is those functions with the walk they
 * already do. `vocab`/`mode_valid`/`depth_valid` are BORROWED for the process's life: a host
 * calls this once, at startup, with process-lifetime tables — the same assumption
 * `rolltui_windows_set_builtin_roles` already makes of ITS caller. */
void rolltui_theme_preset_domain_init(RolltuiPresetDomain* out, const RolltuiThemeVocab* vocab,
                                      RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid);

/* `default_actions`/`_n` are BORROWED for the process's life, same as `hooks` — a host passes
 * `shipped_default_actions()`'s C form (the shipped "default" layout's own "actions" list). */
void rolltui_layout_preset_domain_init(RolltuiPresetDomain* out, const RolltuiLayoutHooks* hooks,
                                       const RolltuiLayoutAction* default_actions, size_t default_actions_n);

/* `is_library_scope`/`reason` are BORROWED for the process's life, the same two
 * callbacks `rolltui_bindings_load_json` already takes — this keeps a copy to hand over on
 * every call instead of threading them through the generic mechanics. */
void rolltui_bindings_preset_domain_init(RolltuiPresetDomain* out,
                                        RolltuiScopeFn is_library_scope, void* scope_ctx,
                                         RolltuiReasonFn reason,
                                         void* reason_ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PRESETS_H */
