#ifndef ROLLTUI_C_PRESETS_H
#define ROLLTUI_C_PRESETS_H
/* INTERNAL: the public surface of this module is the three concrete stores in `rolltui.h`
 * (Theme/Layout/Bindings) and their CREATE-AND-EDIT half in `rolltui_studio.h`. What is below —
 * the generic engine that backs all three, and a handful of narrower helpers — is the library's
 * own: reached by its `.c` files, and by a suite that opts in by including this header by
 * name. */
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

/* ---- THE GENERIC ENGINE, once public, now this file's alone -----------------------------------
 * `RolltuiPresetDomain`/`RolltuiPresetStore` and the `rolltui_preset_store_*` family used to be
 * `rolltui.h`'s own: a `void*` plus a function-pointer descriptor, dispatched the same way over
 * whichever of the three domains a caller meant. Every real caller always knew which one that
 * was, so the public surface is now the three concrete stores below the Theme/Layout/Bindings
 * sections of `rolltui.h` — each one this same engine, under a name that cannot be confused with
 * either of the other two. Nothing here changed in mechanics; only its public SHAPE is gone. */

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
  /* A report of THIS domain's type, made and unmade by the domain: OWNED, short-lived. It lets the mechanics parse into a report of their
   * own instead of asking every caller for a second one. */
  void* (*create)(void);
  void (*destroy)(void* report);
} RolltuiPresetReportFns;

/* One domain: its four names, its embedded shipped table, and what can be done to a value. `cache` is OWNED by this descriptor and built
 * on first use (see `rolltui_preset_domain_release`). */
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

  /* TEXT in, an OWNED value out (NULL on failure, with the report saying why). The JSON never crosses this boundary. */
  void* (*parse)(const struct RolltuiPresetDomain* d, const char* text, size_t len, void* report);
  /* A PARTIAL file (a colours-only theme file): fills part of `working`, notes why; NULL
   * when the file is not partial, and `parse` is then used. */
  void* (*parse_partial)(const struct RolltuiPresetDomain* d, const char* text, size_t len, const void* working,
                         void* report);
  void (*to_json)(const struct RolltuiPresetDomain* d, const void* value, const char* name, size_t name_len,
                  RolltuiPutFn put, void* ctx);
  /* …and the same, with the working copy's ORIGIN written into it as "preset". One call
   * rather than a second serialiser, because that key is the store's and not the domain's. */
  void (*to_json_with_origin)(const struct RolltuiPresetDomain* d, const void* value, const char* name,
                              size_t name_len, RolltuiPutFn put, void* ctx);
  /* …and back: the preset name a working-copy file says it came from, "default" when it says nothing. A callback for the same reason as
   * its pair: the JSON never crosses. */
  void (*origin_of)(const char* text, size_t len, RolltuiPutFn put, void* ctx);

  void* (*clone)(const void* value);
  void (*destroy)(void* value);
  int (*equal)(const void* a, const void* b);

  /* The ops for this domain's REPORT type: a BORROW of a library static, set by the domain's `_init`. A field, not a parameter at every
   * `_new` / `_shipped`, because no caller pairs a domain with any table but its own. */
  const RolltuiPresetReportFns* report;

  /* ---- THE DOMAIN'S OWN CONFIGURATION -------------------------------------------------
     * What each `*_preset_domain_init` was handed. It lives in the descriptor, not in file statics, so two descriptors of the same kind
     * do not share one configuration and a borrowed table cannot outlive the session that owned it. Only the rows for a descriptor's
     * own kind are read. */
  const RolltuiThemeVocab* theme_vocab;          /* theme */
  RolltuiThemePresetValidFn theme_mode_valid;    /* theme */
  RolltuiThemePresetValidFn theme_depth_valid;   /* theme */
  const RolltuiLayoutHooks* layout_hooks;        /* layout */
  const RolltuiLayoutAction* layout_actions;     /* layout — BORROWED from a context's cache */
  size_t layout_actions_n;                       /* layout */
  RolltuiScopeFn bindings_is_library_scope;      /* bindings */
  void* bindings_scope_ctx;                      /* bindings */
  RolltuiReasonFn bindings_reason;               /* bindings */
  void* bindings_reason_ctx;                     /* bindings */

  RolltuiPresetShippedCache* cache;

  /* ---- THE PERSON'S SETTINGS A VALUE CARRIES BESIDE ITS CONTENT ------------------------------------------------
     * A theme's colours are the theme's; whether they are shown light or dark, and at what colour depth, are the PERSON'S (a theme
     * value carries them only so one working file keeps the lot). The store asks the domain to keep the two apart; each hook is NULL
     * for a domain with no such settings:
     *   adopt_settings      `value`'s settings become `from`'s, or the defaults ("auto") when `from` is NULL. A preset never brings
     *                       settings of its own, and choosing one keeps the person's;
     *   settings_to_json    the settings as `,\n  "key": value` text (no braces) that a working file which only FOLLOWS its preset
     *                       writes beside the pointer, so the choice is remembered without freezing the preset's colours;
     *   settings_from_json  reads them back from that text.
     * `equal` then compares CONTENT only: choosing light is not an edit, an unedited working copy follows its preset, and what a
     * release improves in the preset reaches everyone who chose it. */
  void (*adopt_settings)(void* value, const void* from);
  void (*settings_to_json)(const void* value, RolltuiPutFn put, void* ctx);
  void (*settings_from_json)(const struct RolltuiPresetDomain* d, void* value, const char* text, size_t len);
} RolltuiPresetDomain;

/* OWNED, LONG-LIVED: one per concrete store, which frees it through its own `_free`. Every method takes the store's own lock and hands
 * back copies or borrows, so a host may edit from one thread and render from another. */
typedef struct RolltuiPresetStore RolltuiPresetStore;

/* The shipped presets, parsed once per domain into a report the domain makes for itself. A shipped preset that does not load cleanly is
 * a programming error: it says so and aborts, and so does a domain with no preset named "default". Returns a BORROW valid until the domain
 * is released; NULL for a name that is not shipped. */
const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const char* name, size_t len);

int rolltui_preset_is_shipped(RolltuiPresetDomain* d, const char* name, size_t len);

/* The shipped preset's FILE TEXT, verbatim: a BORROW of the embedded bytes, valid for the process's life; NULL (and `*out_len` 0) when
 * `name` is not shipped. A lookup by NAME, since `shipped_count` / `shipped_at` give only indexes. */
const char* rolltui_preset_shipped_text(RolltuiPresetDomain* d, const char* name, size_t len, size_t* out_len);

/* The shipped names, "default" FIRST and the rest in table order — the order a chooser
 * offers them in. REPLACES `*out`. */
void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiStrList* out);

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const char* dir, size_t dir_len,
                                             int may_write_shipped, const char* shipped_dir, size_t shipped_dir_len);

void rolltui_preset_store_free(RolltuiPresetStore* s);

/* Startup: the autosaved working copy when present and loadable, else "default". `report` says what happened to the working file; the
 * origin preset it names is re-read into a report the domain makes for itself, not a second one the caller must supply. */
void rolltui_preset_store_start(RolltuiPresetStore* s, void* report);

/* A CLONE the caller owns and frees with `rolltui_preset_store_value_free`. */
void* rolltui_preset_store_working(const RolltuiPresetStore* s);

/* Frees a value `_working` or `_get` handed back, by the store's own domain's `destroy`, so a caller holding the store alone can release
 * what it gave (a handle is created and released in a pair, rule 1). NULL is a no-op. */
void rolltui_preset_store_value_free(const RolltuiPresetStore* s, void* v);

/* BORROWS of the store's own bytes, valid until it next changes. */
const char* rolltui_preset_store_origin(const RolltuiPresetStore* s, size_t* len);

const char* rolltui_preset_store_last_error(const RolltuiPresetStore* s, size_t* len);

/* "(modified)" is BY COMPARISON, run when the store CHANGES and never when this is read: a read is one flag under the lock (running the
 * domain's deep `equal` on every call would put it on the draw path, twice a frame through `label`). */
int rolltui_preset_store_modified(const RolltuiPresetStore* s);

/* "<origin>", or "<origin> (modified)" once the working copy differs from what it was loaded from. THE LIBRARY OWNS THE WORD
 * "(modified)". REPLACES `*out`, reusing its buffer (it must not APPEND: a caller holding a buffer for its frame could not call it twice). */
void rolltui_preset_store_label(const RolltuiPresetStore* s, RolltuiStr* out);

unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s);

/* An in-place edit under the lock. */
void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist);

/* REPLACES `*out` (its capacity, and each entry's string buffers, are reused). */
void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetList* out);

/* A preset by name or path, as a value the caller OWNS and frees with
 * `rolltui_preset_store_value_free`; NULL with the report saying why. */
void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, size_t len, void* report);

/* …and the same, into the working copy. 0 when it could not be read. */
int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name, size_t len, void* report, int persist);

/* Save-as. REPLACES `*err` (may be NULL) with the outcome's SENTENCE for every result but SAVED (WRITE_FAILED's own reason, and
 * `rolltui_preset_save_result_text`'s fixed sentence for the other three), so a caller reads one string for any refusal. */
/* Always autosaves the working copy afterwards, unlike `_load` / `_set_working` / `_edit`: a save-as is an explicit write and the working
 * copy records its new origin. There is no `persist` parameter: no caller wants 0. */
int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* ADD a preset from elsewhere (a theme someone sent you, a layout from another directory). ADDITIVE: nothing is closed and nothing is
 * replaced. The file is COPIED in rather than referenced, because a dangling reference is a preset that stops existing for a reason
 * nobody can see. Named by `as`, or by the file's own stem when `as` is empty. Returns the codes `save_as` does: `EXISTS_ASK` when the
 * name is taken (refused, never overwritten), `REFUSED_SHIPPED` for a shipped name, `BAD_NAME` for an unusable name OR a file that does
 * not parse as this domain, `WRITE_FAILED` when it could not be read or written. `err` carries the sentence either way. */
int rolltui_preset_store_add(RolltuiPresetStore* s, const char* path, size_t path_len, const char* as,
                             size_t as_len, RolltuiStr* err);

/* The two paths. Both REPLACE `*out` (rule 3(b)), like `rolltui_preset_store_label`. */
void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiStr* out);

void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiStr* out);

/* A setting's value in a store's WORKING COPY, APPENDED to `out` (empty when this key is not this domain's). The identity key (the
 * domain's `kind`: "theme" / "layout" / "bindings") answers with the origin; a Theme store also answers "theme_mode" and "color_depth".
 * It takes no domain tag beside the store: `kind` IS the name. Reached only by `rolltui_settings.c`, over whichever concrete store's
 * `RolltuiPresetStore*` cast it is holding for the key's domain. */
void rolltui_preset_working_value(const RolltuiPresetStore* s, const char* key, size_t key_len, RolltuiStr* out);

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

/* Which of the three a STORE belongs to. Distinct from `RolltuiPresetDomain` above (that struct
 * is the MECHANICS for one domain — parse/to_json/clone/...; this is a tag naming one of the
 * library's three), hence the `Id` suffix. The library's own closed set, like `Anchor` and `Border`: a host domain
 * built through `_init` has no id and needs none — a store knows its domain by its `kind`. */
typedef enum RolltuiPresetDomainId {
  ROLLTUI_PRESET_DOMAIN_THEME = 0,
  ROLLTUI_PRESET_DOMAIN_LAYOUT,
  ROLLTUI_PRESET_DOMAIN_BINDINGS,
} RolltuiPresetDomainId;

/* A BORROW of a static string literal: "theme" | "layout" | "bindings" — and, not by
 * coincidence, exactly the key that is each domain's own IDENTITY setting AND each library
 * domain's `kind`: one spelling of the name, three readers. */
const char* rolltui_preset_domain_name(RolltuiPresetDomainId d, size_t* len);

/* The indexed lookup the three named accessors below wrap, and the one the library's own loop
 * over all three calls. */
RolltuiPresetDomain* rolltui_preset_domain(RolltuiContext* c, RolltuiPresetDomainId id);

/* One accessor per library domain, so naming a domain does not mean spelling its id — the three
 * concrete stores' own `_new` are this file's only callers now; no host reaches these directly
 * any more (see the file comment at the top). A domain's name is its `kind`: read `dom->kind`. */
RolltuiPresetDomain* rolltui_preset_domain_theme(RolltuiContext* c);

RolltuiPresetDomain* rolltui_preset_domain_layout(RolltuiContext* c);

RolltuiPresetDomain* rolltui_preset_domain_bindings(RolltuiContext* c);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PRESETS_H */
