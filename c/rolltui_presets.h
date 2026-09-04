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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PRESETS_H */
