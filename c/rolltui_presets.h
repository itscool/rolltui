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

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- files, which are mechanics too ------------------------------------------------------ */

/* Appends bytes somewhere the caller owns — how every string longer than a name leaves this
 * file. A `std::string` on the other side; a growing buffer on this one. */
/* `RolltuiPutFn` MOVED to `rolltui_str.h` (Phase 17 m3) — the string module owns the string
 * sink. It was declared here because presets happened to need one first, and `rolltui_menu.h`
 * then needed the same shape for its tree walks: a second spelling of one thing is the
 * duplication rule firing, so the type moved instead of being written twice. The NAME did not
 * change, so no call site did either. */

/* Reads a whole file. 0 when it cannot be opened. `put`/`ctx` rather than a `RolltuiStr*`
 * because the library's own three callers stream into their internal `Buf`; a consumer that
 * wants the bytes passes `rolltui_str_put` and a `RolltuiStr*`. */
int rolltui_preset_read_file(const char* path, size_t path_len, RolltuiPutFn put, void* ctx);
/* Writes to a sibling temp file, then renames — a reader sees the old complete file or the
 * new complete file, never a mix (the state-file rule from ResilientModelManager). On
 * failure, 0, and the reason REPLACES `*err` (which may be NULL). */
int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiStr* err);
/* The stem of every "*.json" in `dir`, sorted. REPLACES `*out`. */
void rolltui_preset_json_names_in(const char* dir, size_t dir_len, RolltuiStrList* out);
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
/* The shipped preset's FILE TEXT, verbatim — a BORROW of the embedded bytes, valid for the
 * process's life; NULL (and `*out_len` 0) when `name` is not shipped.
 *
 * ADDED Phase 17 m4b, because its absence was being paid for: `shipped_count`/`shipped_at`
 * gave every index but no way to ask by NAME, so roll and the studio each hand-wrote the same
 * linear search over them. A lookup the API can do and does not offer is a lookup every
 * consumer writes. */
const char* rolltui_preset_shipped_text(RolltuiPresetDomain* d, const char* name, size_t len, size_t* out_len);
/* The shipped names, "default" FIRST and the rest in table order — the order a chooser
 * offers them in. REPLACES `*out`. */
void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiStrList* out);
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

/* "<origin>", or "<origin> (modified)" once the working copy differs from what it was loaded
 * from — `rolltui::PresetStore::label()`'s composition, and the SECOND English sentence this
 * module owned that lived one level up (Phase 17 m2a, found while measuring m2c: the first
 * sweep for unported English read every `.cpp` and no `.hpp`, and `PresetStore.hpp` is a
 * template header where the whole store lives inline). `studio.cpp:1173` already spells
 * " (modified)" a second time, for a LAYOUT's own name rather than for a store's origin — one
 * word, two spellings, exactly the shape this phase keeps finding. APPENDS to `out`. */
void rolltui_preset_store_label(const RolltuiPresetStore* s, RolltuiStr* out);
unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s);

/* The only write anyone does (rule 2): replace the working copy. TAKES OWNERSHIP of `v`. */
void rolltui_preset_store_set_working(RolltuiPresetStore* s, void* v, int persist);
/* An in-place edit under the lock. */
void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist);

/* ---- ONE PRESET IN A LISTING, and the shape every "N things out" uses ---------------------
 *
 * THIS TYPE EXISTS BECAUSE ITS ABSENCE WAS BEING PAID FOR THREE TIMES (Phase 17 m4b,
 * 2026-09-05). `rolltui_preset_store_list` took a SINK — `(void* ctx, const char* name,
 * size_t, int shipped, const char* path, size_t)` — and that parameter list IS a struct
 * definition the library declined to write down. So every consumer wrote it instead:
 * `roll::PresetInfo` in `include/TuiFrontend.hpp`, `PresetInfo` in `rolltui/tools/studio.cpp`
 * and `PresetInfo` in `rolltui/tests/presets_test.cpp` — three byte-identical structs, each
 * with a lambda, a `static_cast<std::vector<PresetInfo>*>` and a collector around it.
 *
 * That is rule 5 of `rolltui.h` firing ("if two consumers write the same wrapper, the API is
 * wrong, not the consumers"), and the fix is not a C++ layer over the sink — it is naming the
 * thing the sink was spelling out.
 *
 * THE RULE THIS SETTLES, and it is the one `rolltui.h` had for TEXT OUT and not for N THINGS
 * OUT: a result the library ALREADY HAS goes into a buffer the CALLER owns and reuses —
 * `RolltuiStr*` for text, a growing list like this for many things — and is REPLACED on every
 * call. A callback is for a DECISION the library cannot make (`RolltuiScopeFn`,
 * `RolltuiRowsFn`, `RolltuiEffectFn`), never for handing back an answer. The two are told
 * apart by one question: does the callback carry a decision IN, or a result OUT?
 *
 * The shipped ones come first, "default" ahead of the rest — the order a chooser offers them
 * in — then the user's "*.json" that do not shadow a shipped name. `path` is empty for a
 * shipped preset. */
typedef struct RolltuiPresetInfo {
  RolltuiStr name;
  RolltuiStr path; /* "" for a shipped preset */
  int shipped ROLLTUI_DEFAULT(0);
} RolltuiPresetInfo;

/* A caller-owned, reusable list of them. Zero-initialise before first use; `_release` frees
 * everything and zeroes it (a no-op on a zeroed list, and on NULL). In C++ the destructor
 * does that, so a plain local needs no release call at all. */
typedef struct RolltuiPresetList {
  RolltuiPresetInfo* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiPresetList() = default;
  RolltuiPresetList(const RolltuiPresetList&) = delete;
  RolltuiPresetList& operator=(const RolltuiPresetList&) = delete;
  ~RolltuiPresetList();
  const RolltuiPresetInfo* begin() const { return v; }
  const RolltuiPresetInfo* end() const { return v + n; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
  const RolltuiPresetInfo& operator[](size_t i) const { return v[i]; }
#endif
} RolltuiPresetList;

void rolltui_preset_list_release(RolltuiPresetList* l);

/* REPLACES `*out` (its capacity, and each entry's string buffers, are reused). */
void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetList* out);

/* A preset by name or path, as a value the caller OWNS; NULL with the report saying why. */
void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, size_t len, void* report);
/* …and the same, into the working copy. 0 when it could not be read. */
int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name, size_t len, void* report, int persist);

#define ROLLTUI_SAVE_SAVED 0
#define ROLLTUI_SAVE_REFUSED_SHIPPED 1
#define ROLLTUI_SAVE_EXISTS_ASK 2
#define ROLLTUI_SAVE_BAD_NAME 3
#define ROLLTUI_SAVE_WRITE_FAILED 4
/* The SENTENCE for an outcome (Phase 17 m2a). The comment below used to end "...is a fixed
 * sentence per outcome and is built one level up, where the words already are" — the same
 * sentence, in the same shape, as the four other places this phase has had to reverse: the
 * words were one level up in `Presets.cpp`, which m2c deletes. A fixed sentence per outcome is
 * a table, and a table belongs with the constants it is indexed by. BORROWS a static literal;
 * `*len` may be NULL; an out-of-range code reads back as "". WRITE_FAILED's own reason is
 * still the caller's, through `err` below — that one is not fixed. */
const char* rolltui_preset_save_result_text(int result, size_t* len);

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

/* Save-as. Only WRITE_FAILED has a reason of its own; it REPLACES `*err` (which may be NULL
 * when the caller does not want it). Every other outcome's sentence is
 * `rolltui_preset_save_result_text` above. */
int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* The two paths. Both REPLACE `*out` — text out, rule 3(b), the same shape
 * `rolltui_preset_store_label` beside them already used. They took a `RolltuiPutFn` until
 * Phase 17 m4b, which is why every consumer had a lambda-and-append around them. */
void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiStr* out);
void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiStr* out);

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

/* ---- C-SIDE DOMAIN DESCRIPTORS for Theme, Layout and Bindings (Phase 17 m1c) ----------------
 *
 * `PresetStore.hpp`'s `domain_storage<D>()` is the only place that has ever ASSEMBLED a
 * `RolltuiPresetDomain` — it is a C++ template, so nothing built purely in C could ever reach
 * one. Every domain's own file-format ALGORITHM is already C by this point
 * (`rolltui_theme_preset_parse` above, `rolltui_load_layout`/`_text`, `rolltui_bindings_load_json`)
 * — what was missing was the ASSEMBLY: filling `parse`/`parse_partial`/`to_json`/
 * `to_json_with_origin`/`origin_of`/`clone`/`destroy`/`equal`/`shipped_count`/`shipped_at` with
 * real C functions over each domain's real C value type, so a store can be built with no C++
 * anywhere in the call chain. Two independent sessions reached for this and found nothing —
 * that is the signal this exists to close.
 *
 * WHAT EACH STILL TAKES FROM C++, ONCE, AT INIT — the SAME "domain supplies the policy" shape
 * this header already uses for Theme's `mode_valid`/`depth_valid` above, extended to the two
 * new domains rather than invented for them:
 *   Theme     the role/state VOCAB (`rolltui_style.h`: "a C file names no role") and the two
 *             mode/depth validators — exactly what `rolltui_theme_preset_parse` already took
 *             as parameters; this section only keeps a copy to hand over on every call.
 *   Layout    the SAME role vocab plus "which scopes are the library's" (`RolltuiLayoutHooks`,
 *             already `rolltui_load_layout`'s own parameter) and the shipped `default` layout's
 *             own "actions" list (`shipped_default_actions()`'s C-callable form).
 *   Bindings  "which scopes are the library's", the renamed-action table, and the six
 *             undeliverable-chord sentences — `rolltui_bindings_load_json`'s own three
 *             callback parameters, unchanged.
 * None of these is a vocabulary this section invents: each is a call site further down the
 * SAME file that already had to be told the identical thing, now told ONCE at process start
 * (by whichever caller — C++ today, since that is where the tables still live — builds the
 * descriptor) instead of on every call. A caller that never calls the matching `_init`
 * function gets a domain with no vocabulary to check against: every scope reads as
 * non-library and no chord is ever undeliverable, which is a wrong but LOUD answer (a role or
 * scope this host actually has will misbehave immediately and visibly), never a silent one.
 *
 * Each domain's REPORT is its own file-level report PLUS the one thing every domain shares
 * with `PresetLoadReport` and none of them had on its own: a top-level `error` a caller can
 * set/read independently of the nested report (`rolltui_load_layout`/`rolltui_bindings_load_json`
 * already write their OWN `error` field on the nested report; the mechanics' own failures — "no
 * preset 'x'", "unreadable (...)" — land on the outer one), and a growing `notes` array the
 * mechanics' own `add_note`/`prefix_notes` write into alongside whatever the domain's own parse
 * appended to it. Zero-initialise before use, and
 * release with the matching `_release` below (its own `reset` in the report_fns already does).
 */

/* ---- the Theme domain ----------------------------------------------------------------------
 * ~~`rolltui::ThemePreset` (Presets.hpp) IS this struct~~ — **FALSE, and a segfault is what
 * found it (Phase 17 m3, 2026-09-05).** `ThemePreset` holds `std::string mode/depth`; this
 * holds `RolltuiStr`. They are two different structs that describe the same thing, and casting
 * a store's `void*` from one to the other crashes. "two strings, which is already all-C" was
 * true of the CONCEPT and false of the TYPE, and the sentence collapsed the two.
 *
 * **The consequence is bigger than the wording.** There are two Theme preset DOMAINS in the
 * tree: `PresetStore<ThemeDomain>`'s C++ descriptor (whose values are `ThemePreset`) and
 * `rolltui_theme_preset_domain_init`'s (whose values are these). Every live store uses the
 * first; the second has NO production caller — only six `presets_test` assertions that its
 * function pointers are non-NULL. Ported but unreachable, the shape m1c named. Switching the
 * stores over is m2c's, and it changes the value type at every preset call site. */
typedef struct RolltuiThemePresetValue {
  RolltuiJsonValue* colours ROLLTUI_DEFAULT(nullptr); /* OWNED */
  RolltuiStr mode;                                     /* "auto" | "dark" | "light" */
  RolltuiStr depth;                                    /* "auto" | "truecolor" | "256" | "16" | "mono" */
} RolltuiThemePresetValue;

void rolltui_theme_preset_value_release(RolltuiThemePresetValue* v); /* frees `colours`; zeroes */

const RolltuiPresetReportFns* rolltui_theme_preset_report_fns(void);
/* Fills `out` with the Theme domain's mechanics — nothing here is a paraphrase of
 * `rolltui_theme_preset_parse`/`_parse_partial`, it is those functions with the walk they
 * already do. `vocab`/`mode_valid`/`depth_valid` are BORROWED for the process's life: a host
 * calls this once, at startup, with process-lifetime tables — the same assumption
 * `rolltui_windows_set_builtin_roles` already makes of ITS caller. */
void rolltui_theme_preset_domain_init(RolltuiPresetDomain* out, const RolltuiThemeVocab* vocab,
                                      RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid);

/* ---- the Layout domain ----------------------------------------------------------------------
 * The Value is `RolltuiLayout` itself (rolltui_layout.h): the shipped presets ARE the
 * built-ins, embedded once and read by both a host's `builtin_layout()`-shaped lookup and this
 * domain, so the two can never disagree — the same fact Presets.hpp already states of the
 * C++ path. */
typedef struct RolltuiLayoutPresetReport {
  RolltuiStr error; /* the PRESET-level error: a copy of `layout.error` on failure, or the
                     * mechanics' own ("no layout preset 'x' ...") */
  RolltuiLayoutReport layout; /* the file's own: error, unknown_keys, bad_values, notes */
  RolltuiStr* notes;          /* one per `layout.notes` entry, plus whatever the mechanics
                               * itself adds */
  size_t notes_n, notes_cap;
} RolltuiLayoutPresetReport;

void rolltui_layout_preset_report_release(RolltuiLayoutPresetReport* r); /* frees everything; zeroes */

const RolltuiPresetReportFns* rolltui_layout_preset_report_fns(void);
/* `default_actions`/`_n` are BORROWED for the process's life, same as `hooks` — a host passes
 * `shipped_default_actions()`'s C form (the shipped "default" layout's own "actions" list). */
void rolltui_layout_preset_domain_init(RolltuiPresetDomain* out, const RolltuiLayoutHooks* hooks,
                                       const RolltuiLayoutAction* default_actions, size_t default_actions_n);

/* ---- the Bindings domain --------------------------------------------------------------------
 * The Value is `RolltuiBindings*` itself (rolltui_bindings.h) — already fully C, so this
 * domain's `clone`/`destroy`/`equal` are `rolltui_bindings_clone`/`_free`/`_equal` verbatim. */
typedef struct RolltuiBindingsPresetReport {
  RolltuiStr error; /* the PRESET-level error: a copy of `bindings.error` on failure, or the
                     * mechanics' own */
  RolltuiBindingsReport bindings; /* the file's own: unknown_actions/bad_chords/undeliverable/
                                   * conflicts/bad_values/unknown_keys (its "bindings" object) */
  RolltuiStr* unknown_keys; /* the PRESET file's own top-level keys other than "name" /
                             * "bindings" / "preset" — `rolltui_bindings_load_json` only ever
                             * looks at its "bindings" object, so this level's unknown keys are
                             * this domain's own to find */
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* notes; /* whatever the mechanics itself adds */
  size_t notes_n, notes_cap;
} RolltuiBindingsPresetReport;

void rolltui_bindings_preset_report_release(RolltuiBindingsPresetReport* r); /* frees everything; zeroes */

/* ---- IS THIS REPORT CLEAN, AND WHAT DOES IT SAY — per domain (Phase 17 m3) -----------------
 * `rolltui_preset_report_summary` above is the generic COMPOSER: it takes the pieces and joins
 * them in order. What it does not know is which pieces each domain has, so every caller wired
 * its own fields in — and `clean()` was re-derived outright, three times per host.
 *
 * Both hosts wrote all six independently in one session (roll's `TuiFrontend.hpp`, the
 * studio's `studio.cpp`), which is `rolltui.h` rule 5's tell: two consumers writing the same
 * thing means the API is wrong, not the consumers. Unlike the store wrappers around them —
 * which are marshalling, and a language-boundary cost this phase deliberately pushed onto
 * hosts — this is a JUDGEMENT about the library's own data ("does a missing role make a theme
 * preset unclean?"), and two hosts answering it separately is two answers waiting to differ.
 *
 * `_clean` returns 1 when the report has nothing to report. `_summary` APPENDS the same
 * sentence the composer would, with that domain's fields already wired. */
int rolltui_theme_preset_report_clean(const RolltuiThemePresetReport* r);
void rolltui_theme_preset_report_summary(const RolltuiThemePresetReport* r, RolltuiStr* out);
int rolltui_layout_preset_report_clean(const RolltuiLayoutPresetReport* r);
void rolltui_layout_preset_report_summary(const RolltuiLayoutPresetReport* r, RolltuiStr* out);
int rolltui_bindings_preset_report_clean(const RolltuiBindingsPresetReport* r);
void rolltui_bindings_preset_report_summary(const RolltuiBindingsPresetReport* r, RolltuiStr* out);

const RolltuiPresetReportFns* rolltui_bindings_preset_report_fns(void);
/* `is_library_scope`/`reason` are BORROWED for the process's life, the same two
 * callbacks `rolltui_bindings_load_json` already takes — this keeps a copy to hand over on
 * every call instead of threading them through the generic mechanics. */
void rolltui_bindings_preset_domain_init(RolltuiPresetDomain* out, RolltuiScopeFn is_library_scope, void* scope_ctx,
                                         RolltuiReasonFn reason,
                                         void* reason_ctx);

/* ---- settings and precedence (Presets.hpp; Phase 17 m2) -------------------------------------
 *
 * ONE LEVEL ABOVE the store: a HOST's own setting vocabulary ("theme", "theme_mode", "layout",
 * ...) and the four-rung precedence a flag/env/config value resolves through. It never touches
 * a `RolltuiPresetStore` or a `RolltuiPresetDomain` — which is exactly why it had ZERO C-side
 * implementation before this: nothing else in this file ever called it, so nothing forced it
 * out of `Presets.hpp`.
 *
 * ~~WHAT DID NOT MOVE, AND STAYS AT `Presets.hpp`: `working_value(...)` ... "theme_mode"/
 * "color_depth" read `ThemePreset::mode`/`::depth` — `std::string` MEMBERS of a C++-only
 * struct ... so those two fields have no C form to read them from.~~
 * **RETRACTED IN PLACE 2026-09-05 (Phase 17 m3), because it was already false when written.**
 * `RolltuiThemePresetValue` — sixty lines below in this same header — holds `mode` and `depth`
 * as `RolltuiStr`, and its own comment says *"'mode'/'depth' are two strings, which is already
 * all-C"*. The two sentences contradicted each other across one file. `working_value` had no
 * blocker; it had a reason nobody re-read, and `rolltui_preset_working_value` below is the
 * whole of it. What DID move out of its dispatch back in m2: which key is a domain's IDENTITY
 * setting (the one whose value is the whole preset name) is `key ==
 * rolltui_preset_domain_name(domain)` rather than "theme"/"layout"/"bindings" spelled again.
 */

/* `rolltui_preset_working_value` is declared below, after
 * `RolltuiPresetDomainId` — the type the first of them takes. */

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

/* A setting's value in a store's WORKING COPY, APPENDED to `out` (empty when this key is not
 * this domain's). `domain` says which store `s` is — the caller knows, and the store does not
 * carry its own tag. The identity key ("theme"/"layout"/"bindings") answers with the origin;
 * the Theme domain additionally answers "theme_mode" and "color_depth". */
void rolltui_preset_working_value(const RolltuiPresetStore* s, RolltuiPresetDomainId domain, const char* key,
                                  size_t key_len, RolltuiStr* out);

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

/* The one method that cannot be inline in the struct: it calls a function declared after it.
 * Same placement, and same reason, as `RolltuiStr::~RolltuiStr` in `rolltui_str.h`. */
inline RolltuiPresetList::~RolltuiPresetList() { rolltui_preset_list_release(this); }
#endif

#endif /* ROLLTUI_C_PRESETS_H */
