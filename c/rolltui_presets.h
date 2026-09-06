#ifndef ROLLTUI_C_PRESETS_H
#define ROLLTUI_C_PRESETS_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_presets.h — THE PRESET MECHANICS, as C (Phase 15 m3).
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
 *   ------------------------------------      ---------------------------------------
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
 * cannot make one. The sixth and seventh — MAKE one and UNMAKE it — were missing until
 * Phase 18 m3, and their absence was paid for at every call site: the store needed a report
 * of its own for two throwaway parses (the shipped cache, the origin re-read at start) and,
 * unable to make one, took a SECOND report from the caller. Twenty-six call sites in five
 * consumers wrote that second one; the pure-C consumer was the fifth.
 *
 * **WHAT IS NOT TOLD IS AS INTERESTING AS WHAT IS.** The JSON never crosses: `parse` takes
 * TEXT and hands back an owned value, so `json::Value` — a whole module that has not ported
 * — stays entirely on the other side, and the store gains a property the template did not
 * have, that it never sees a parsed tree it has no use for.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14 and none new:
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

/* ---- THE STORE'S ONE-LINE PROBLEM SENTENCE (Phase 17 m2a) ----------------------------------
 * `rolltui::PresetLoadReport::summary()`'s composition, moved with the words it composes: the
 * error alone when there is one, else every problem as "<prefix>: <text>" joined with "; ", in
 * a fixed order (this store's own bad values and unknown keys, then the colours part, then the
 * layout part, then the bindings part). Seven call sites in `studio.cpp` and one in
 * `presets_test.cpp` draw it, and `Presets.cpp` — where it lived — is deleted in m2c.
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


/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_TESTS`. */
int rolltui_preset_looks_like_path(const char* s, size_t len);
int rolltui_preset_valid_name(const char* name, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PRESETS_H */
