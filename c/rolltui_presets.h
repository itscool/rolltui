#ifndef ROLLTUI_C_PRESETS_H
#define ROLLTUI_C_PRESETS_H
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
 *   `PresetLoadReport&`                       an opaque pointer and FIVE more callbacks
 *
 * The last row is the one that surprises. The report is not the domain's VALUE — it is the
 * mechanics' own output — and in C++ it is simply a reference to a struct with vectors on
 * it. Here every one of the four things the mechanics do to a report (reset it, set its
 * error, read its error back to wrap a path around, add a note, prefix the notes with a
 * path) has to be a function pointer, because the words are `std::string`s and the store
 * cannot make one.
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
#include <stddef.h>

#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- files, which are mechanics too ------------------------------------------------------ */

/* Appends bytes somewhere the caller owns — how every string longer than a name leaves this
 * file. A `std::string` on the other side; a growing buffer on this one. */
typedef void (*RolltuiPutFn)(void* ctx, const char* s, size_t len);

/* Reads a whole file through `put`. 0 when it cannot be opened. */
int rolltui_preset_read_file(const char* path, size_t path_len, RolltuiPutFn put, void* ctx);
/* Writes to a sibling temp file, then renames — a reader sees the old complete file or the
 * new complete file, never a mix (the state-file rule from ResilientModelManager). On
 * failure, 0, and the reason goes through `err`. */
int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiPutFn err, void* err_ctx);
/* The stem of every "*.json" in `dir`, sorted, one `put` call each. */
void rolltui_preset_json_names_in(const char* dir, size_t dir_len, RolltuiPutFn put, void* ctx);
int rolltui_preset_looks_like_path(const char* s, size_t len);
int rolltui_preset_valid_name(const char* name, size_t len);

/* ---- what a DOMAIN has to supply ---------------------------------------------------------- */

/* The report, as five things the mechanics do to one. `report` is whatever the caller passed
 * in; this file never dereferences it. */
typedef struct RolltuiPresetReportFns {
  void (*reset)(void* report);
  void (*set_error)(void* report, const char* s, size_t len);
  /* The current error, so the store can wrap a path around it and set it back. */
  void (*get_error)(const void* report, RolltuiPutFn put, void* ctx);
  void (*add_note)(void* report, const char* s, size_t len);
  /* Every note gets `prefix` in front of it — `parse_partial` says what it kept, and the
   * store says which file it was. */
  void (*prefix_notes)(void* report, const char* prefix, size_t len);
} RolltuiPresetReportFns;

/* One domain: its four names, its embedded shipped table, and what can be done to a value.
 * `cache` is OWNED by this descriptor and built on first use — see the note on
 * `rolltui_preset_domain_release`. */
typedef struct RolltuiPresetShippedCache RolltuiPresetShippedCache;

typedef struct RolltuiPresetDomain {
  const char* kind;         /* "theme" | "layout" | "bindings" — for messages */
  size_t kind_len;
  const char* working_file; /* "theme.working.json" */
  size_t working_file_len;
  const char* subdir;       /* "themes" */
  size_t subdir_len;

  size_t (*shipped_count)(void);
  void (*shipped_at)(size_t i, const char** name, size_t* name_len, const char** text, size_t* text_len);

  /* TEXT in, an OWNED value out (NULL on failure, with the report saying why). The JSON
   * never crosses this boundary — see the note at the top. */
  void* (*parse)(const char* text, size_t len, void* report);
  /* A PARTIAL file (a colours-only theme file): fills part of `working`, notes why; NULL
   * when the file is not partial, and `parse` is then used. */
  void* (*parse_partial)(const char* text, size_t len, const void* working, void* report);
  void (*to_json)(const void* value, const char* name, size_t name_len, RolltuiPutFn put, void* ctx);
  /* …and the same, with the working copy's ORIGIN written into it as "preset". One call
   * rather than a second serialiser, because that key is the store's and not the domain's. */
  void (*to_json_with_origin)(const void* value, const char* name, size_t name_len, RolltuiPutFn put, void* ctx);
  /* …and back: the preset name a working-copy file says it came from, "default" when it
   * says nothing. The other half of the same key, and the reason it is a callback rather
   * than something the store reads itself is the same one — the JSON never crosses. */
  void (*origin_of)(const char* text, size_t len, RolltuiPutFn put, void* ctx);

  void* (*clone)(const void* value);
  void (*destroy)(void* value);
  int (*equal)(const void* a, const void* b);

  RolltuiPresetShippedCache* cache;
} RolltuiPresetDomain;

/* The shipped presets, parsed once per domain. A shipped preset that does not load cleanly
 * is a programming error (the layout loader's standard): it says so and aborts, and so does
 * a domain with no preset named "default" (rule 5). Returns a BORROW, valid until the
 * domain is released. NULL for a name that is not shipped. */
const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns, void* scratch_report,
                                   const char* name, size_t len);
int rolltui_preset_is_shipped(RolltuiPresetDomain* d, const char* name, size_t len);
/* The shipped names, "default" FIRST and the rest in table order — the order a chooser
 * offers them in. */
void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiPutFn put, void* ctx);
/* Releases the parsed cache. A domain descriptor is a process-wide static on the other
 * side, so this is what it hands back at `rolltui::shutdown()`; the cache rebuilds on next
 * use, which is what makes shutdown callable at any moment. */
void rolltui_preset_domain_release(RolltuiPresetDomain* d);

/* ---- the store ------------------------------------------------------------------------------ */
/* OWNED, LONG-LIVED: one per `rolltui::PresetStore<D>`, which frees it. Every method takes
 * the store's own lock and hands back copies, so a host may edit from one thread and render
 * from another. */
typedef struct RolltuiPresetStore RolltuiPresetStore;

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns,
                                             const char* dir, size_t dir_len, int may_write_shipped,
                                             const char* shipped_dir, size_t shipped_dir_len, void* scratch_report);
void rolltui_preset_store_free(RolltuiPresetStore* s);

/* Startup: the autosaved working copy when present and loadable, else "default".
 * `scratch_report` is a SECOND report the store reads the origin preset into and throws
 * away — the C++ had `PresetLoadReport ignore;` as a local, and a local of a type this file
 * cannot name is exactly the thing a caller has to supply. */
void rolltui_preset_store_start(RolltuiPresetStore* s, void* report, void* scratch_report);

/* A CLONE the caller owns and destroys with `domain->destroy`. */
void* rolltui_preset_store_working(const RolltuiPresetStore* s);
/* BORROWS of the store's own bytes, valid until it next changes. */
const char* rolltui_preset_store_origin(const RolltuiPresetStore* s, size_t* len);
const char* rolltui_preset_store_last_error(const RolltuiPresetStore* s, size_t* len);
int rolltui_preset_store_modified(const RolltuiPresetStore* s);
unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s);

/* The only write anyone does (rule 2): replace the working copy. TAKES OWNERSHIP of `v`. */
void rolltui_preset_store_set_working(RolltuiPresetStore* s, void* v, int persist);
/* An in-place edit under the lock. */
void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist);

/* Every preset: the shipped ones first, then the user's "*.json" that do not shadow one.
 * `shipped` is 1 or 0; `path` is empty for a shipped preset. */
typedef void (*RolltuiPresetInfoFn)(void* ctx, const char* name, size_t name_len, int shipped, const char* path,
                                    size_t path_len);
void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetInfoFn put, void* ctx);

/* A preset by name or path, as a value the caller OWNS; NULL with the report saying why. */
void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, size_t len, void* report);
/* …and the same, into the working copy. 0 when it could not be read. */
int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name, size_t len, void* report, int persist);

#define ROLLTUI_SAVE_SAVED 0
#define ROLLTUI_SAVE_REFUSED_SHIPPED 1
#define ROLLTUI_SAVE_EXISTS_ASK 2
#define ROLLTUI_SAVE_BAD_NAME 3
#define ROLLTUI_SAVE_WRITE_FAILED 4
/* Save-as. The MESSAGE for the first four outcomes is a fixed sentence per outcome and is
 * built one level up, where the words already are; only WRITE_FAILED has a reason of its
 * own, which goes through `err`. */
int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiPutFn err,
                                 void* err_ctx);

/* The two paths, through `put`. */
void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiPutFn put, void* ctx);
void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiPutFn put,
                                      void* ctx);

/* ---- the Theme domain's preset FILE FORMAT (this task; everything above is domain-agnostic
 * mechanics, and this is the one domain whose OWN parse/to_json moved here with it) ----------
 *
 * `rolltui::ThemePreset` (Presets.hpp) wraps a Theme.hpp colours object with "name" (write-
 * only — read back as the STORE's own bookkeeping, via `to_json_with_origin`'s "preset" key,
 * never this format's own), "mode" and "depth". What blocked this port before was that the
 * "colours" part's validation is `rolltui_theme_load` — a C++-only function until Phase 15 m5.
 * It is C now (`rolltui_theme.h`), so this file calls it DIRECTLY instead of bouncing back into
 * C++ for it. Two things deliberately do NOT move here, each for its own stated reason:
 *
 *   - The VOCAB table `rolltui_theme_load` itself needs (role/state NAMES) still cannot be
 *     built here — `rolltui_style.h`: "a C file names no role" — so it crosses as a parameter
 *     exactly as it already does for `rolltui_theme_load`, and `Presets.cpp`'s shim obtains it
 *     from `Theme.cpp`'s `theme_vocab()` (given external linkage for exactly this) and hands
 *     it down. One table, read in two places, built in neither of them a second time.
 *   - Whether a "mode"/"depth" STRING is one of the valid values ("auto"/"dark"/"light", …) is
 *     `rolltui::valid_mode_setting`/`valid_depth_setting` (Presets.hpp) — a five-line C++
 *     predicate with no other caller anywhere in the tree. Re-deriving it here would be a
 *     THIRD spelling of that vocabulary (Presets.hpp's `kSettings` help text is already a
 *     second, tolerated one) for a predicate that exists for this one call site, so instead
 *     `rolltui_theme_preset_parse` takes it as a CALLBACK — the same "domain supplies the
 *     policy, mechanics supply the walk" shape `RolltuiPresetDomain` itself is built from —
 *     which keeps the walk a SINGLE PASS (bad_values/unknown_keys/notes stay in file order,
 *     the same order `rolltui::json::Value::obj` already preserves) rather than a second pass
 *     over the same object after the fact.
 *
 * The "colours" subtree crosses as a `RolltuiJsonValue*` BORROW, never text: `Presets.cpp`'s
 * shim already holds a parsed tree at every call site (`PresetStore.hpp`'s adapter parses text
 * to a tree before calling the domain), so converting it once with `json::value_to_c` — the
 * same conversion `Theme.cpp` already pays for its own `json::Value` overload of `load_theme`
 * — is the honest boundary, not text re-parsed a second time nor a tree invented to avoid one
 * conversion. `*out_colours` is a BORROW of a subtree of `root`, valid exactly as long as
 * `root` is (the same window `rolltui_json_get` itself promises).
 */

/* Mirrors `rolltui::ThemeLoadReport`/`PresetLoadReport`'s Theme-relevant fields, transparent
 * like `RolltuiThemeReport`/`RolltuiAppProfileReport` one file over — nothing about a
 * diagnostic list needs hiding, and nothing outside `rolltui_presets.c` ever writes one; a
 * caller only reads it after a parse call, then releases it. */
typedef struct RolltuiThemePresetReport {
  RolltuiStr error; /* non-empty: unusable */
  RolltuiStr* bad_values;   size_t bad_values_n,   bad_values_cap;   /* GROWING AMORTISED */
  RolltuiStr* unknown_keys; size_t unknown_keys_n, unknown_keys_cap; /* GROWING AMORTISED */
  RolltuiStr* notes;        size_t notes_n,        notes_cap;        /* GROWING AMORTISED */
  RolltuiThemeReport colours; /* the "colours" part's own report, verbatim */
} RolltuiThemePresetReport;

/* Frees everything and zeroes the struct — safe on an already-zeroed one and on repeated
 * calls, the same "reset, not just release" contract every report on this boundary states. */
void rolltui_theme_preset_report_release(RolltuiThemePresetReport* r);

/* A pure predicate over a "mode"/"depth" string — no context, because `valid_mode_setting`/
 * `valid_depth_setting` (Presets.hpp) are themselves pure over a `string_view` with nothing to
 * capture. `Presets.cpp` hands over a captureless-lambda-decayed function pointer, the same
 * bridge `PresetStore.hpp`'s own `domain_storage<D>()` already builds an entire domain
 * descriptor out of. */
typedef int (*RolltuiThemePresetValidFn)(const char* s, size_t len);

/* Parses a preset file's top level from an already-parsed tree: "name"/"preset" (must be a
 * string when present, else a bad value; the VALUE itself is the store's business, not read
 * here), "mode"/"depth" (checked with `mode_valid`/`depth_valid`; kept at whatever `out_mode`/
 * `out_depth` already held — this file sets both to "auto" first, matching `ThemePreset`'s own
 * member-initialisers — when the key is absent or fails its check), "colours" (required; a
 * bad "preset file must be a JSON object"/"needs a \"colours\" object" `report->error` when
 * `root` itself is not usable, checked BEFORE anything else), "layout" (a Phase 9 leftover:
 * not an error, a NOTE naming it — this format's own vocabulary to own, the same position
 * `rolltui_theme.h` takes for a theme file's structural keys). Unknown keys are reported, not
 * rejected. Colour validation runs at BOTH modes (dark then light) so a role wrong only in one
 * variant is still caught: `report->colours` is dark's report, plus light's bad values not
 * already in dark's — mirroring `theme_preset_from_json`'s own double load exactly, including
 * the asymmetry that only DARK's success/failure decides the return value.
 *
 * Returns 1 when `root` is a usable preset (`report` may still carry notes/bad_values/
 * unknown_keys — a usable file can still have problems), 0 when it is not (`report->error`
 * says which; `*out_colours` is NULL). `report` is reset by this call, as every report on this
 * boundary is. */
int rolltui_theme_preset_parse(const RolltuiJsonValue* root, const RolltuiThemeVocab* vocab,
                               RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid,
                               RolltuiStr* out_mode, RolltuiStr* out_depth, const RolltuiJsonValue** out_colours,
                               RolltuiThemePresetReport* report);

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

/* Builds a preset file's tree: {"name","mode","depth","colours"}. TAKES OWNERSHIP of
 * `colours` (folds it into the result directly, the same contract `rolltui_json_set` itself
 * has) — the caller has usually just built it fresh via `json::value_to_c` for this one call
 * and has no further use for it, so consuming it here is a clone fewer. OWNED; the caller
 * frees the result with `rolltui_json_free`. Never sets "preset": that key is
 * `to_json_with_origin`'s, one level up, not this format's own. */
RolltuiJsonValue* rolltui_theme_preset_to_json(RolltuiJsonValue* colours, const char* mode, size_t mode_len,
                                               const char* depth, size_t depth_len, const char* name,
                                               size_t name_len);

/* ---- settings and precedence (Presets.hpp; Phase 17 m2) -------------------------------------
 *
 * ONE LEVEL ABOVE the store: a HOST's own setting vocabulary ("theme", "theme_mode", "layout",
 * ...) and the four-rung precedence a flag/env/config value resolves through. It never touches
 * a `RolltuiPresetStore` or a `RolltuiPresetDomain` — which is exactly why it had ZERO C-side
 * implementation before this: nothing else in this file ever called it, so nothing forced it
 * out of `Presets.hpp`.
 *
 * WHAT DID NOT MOVE, AND STAYS AT `Presets.hpp`: `working_value(const ThemePresets&, key)` /
 * `(const LayoutPresets&, ...)` / `(const BindingsPresets&, ...)` read a setting out of a
 * store's WORKING COPY. For "theme"/"layout"/"bindings" that is just `store.origin()`
 * (already this boundary's own `rolltui_preset_store_origin`), but "theme_mode"/"color_depth"
 * read `ThemePreset::mode`/`::depth` — `std::string` MEMBERS of a C++-only struct (only its
 * `colours` field is a `RolltuiJsonValue*` today). `ThemePreset` itself is not this task's to
 * move, so those two fields have no C form to read them from. What DID move out of
 * `working_value`'s own dispatch: which key is a domain's IDENTITY setting (the one whose
 * value is the whole preset name) is now `key == rolltui_preset_domain_name(domain)` rather
 * than "theme"/"layout"/"bindings" repeated as three more C++ string literals.
 */

typedef enum RolltuiPresetRung {
  ROLLTUI_PRESET_RUNG_FLAG = 0,
  ROLLTUI_PRESET_RUNG_ENV,
  ROLLTUI_PRESET_RUNG_WORKING,
  ROLLTUI_PRESET_RUNG_BUILTIN,
} RolltuiPresetRung;

/* A BORROW of a static string literal, never freed: "flag" | "environment" | "working copy" |
 * "built-in default". */
const char* rolltui_preset_rung_name(RolltuiPresetRung r, size_t* len);

/* THE WHOLE RULE (Presets.hpp): the first NON-EMPTY rung wins. An empty string at a rung means
 * "not given there". `*out_value`/`*out_value_len` BORROW whichever of the four input strings
 * won — never copied, never allocated, valid exactly as long as that one input buffer is (the
 * same window the caller's own four strings already have). */
void rolltui_preset_resolve_setting(const char* flag, size_t flag_len, const char* env, size_t env_len,
                                    const char* working, size_t working_len, const char* builtin,
                                    size_t builtin_len, const char** out_value, size_t* out_value_len,
                                    RolltuiPresetRung* out_rung);

/* Which STORE a setting lives in — "theme_mode" is the Theme domain's even though it is not
 * the Theme domain's IDENTITY key ("theme" is). Distinct from `RolltuiPresetDomain` above
 * (that struct is the MECHANICS for one domain — parse/to_json/clone/...; this is a tag
 * saying which of the three a setting belongs to), hence the `Id` suffix. */
typedef enum RolltuiPresetDomainId {
  ROLLTUI_PRESET_DOMAIN_THEME = 0,
  ROLLTUI_PRESET_DOMAIN_LAYOUT,
  ROLLTUI_PRESET_DOMAIN_BINDINGS,
} RolltuiPresetDomainId;

/* A BORROW of a static string literal: "theme" | "layout" | "bindings" — and, not by
 * coincidence, exactly the key that is each domain's own IDENTITY setting (`kSettings`
 * below): `working_value`'s "is this key the whole preset name" case is `key ==
 * domain_name(store's domain)`, which is what `presets_test.cpp`'s "setting(...)->domain ==
 * Domain::X" checks holding for every row make true. */
const char* rolltui_preset_domain_name(RolltuiPresetDomainId d, size_t* len);

/* One row of `kSettings` (Presets.hpp): a setting's key, which domain/store it belongs to, its
 * environment-variable suffix ("THEME" joined to a host's own prefix), its built-in default,
 * and help text for its legal values. BORROWED fields throughout — every string is a literal
 * in the table below, alive for the process's whole life. */
typedef struct RolltuiPresetSettingSpec {
  const char* key;
  size_t key_len;
  RolltuiPresetDomainId domain;
  const char* env_suffix;
  size_t env_suffix_len;
  const char* builtin;
  size_t builtin_len;
  const char* values; /* help text, e.g. "auto | dark | light" */
  size_t values_len;
} RolltuiPresetSettingSpec;

size_t rolltui_preset_settings_count(void);
/* BORROW, table order ("theme", "layout", "theme_mode", "color_depth", "bindings" — the order
 * a listing offers them in), valid for the process's whole life. */
const RolltuiPresetSettingSpec* rolltui_preset_settings_at(size_t i);
/* The row named `key`, as an INDEX into the table above (`rolltui_preset_settings_at`) rather
 * than a pointer — the shape a caller whose OWN copy of this table is a different array
 * (`Presets.cpp`'s `kSettings`, built from this one row for row) needs to find the matching
 * row without a second string comparison. -1: `key` is not a known setting. */
int rolltui_preset_setting_index(const char* key, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PRESETS_H */
