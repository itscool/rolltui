#ifndef ROLLTUI_C_APP_PROFILE_H
#define ROLLTUI_C_APP_PROFILE_H
/*
 * rolltui/c/rolltui_app_profile.h — WHAT A LAYOUT MAY NAME INSIDE ONE APP, as C (Phase 17
 * m1). `rolltui/AppProfile.hpp`'s header comment states what a profile IS and why each part
 * of it is a thing only the app can know (sources with sample content, registered kinds,
 * menu files verbatim, declared actions, help scopes, min sizes); none of that is repeated
 * here — this file is the parsing and serialising ALGORITHM, the same split Json got.
 *
 * ---- THE FIX THIS MODULE MAKES WITHOUT QUALIFICATION, UNLIKE JSON'S OWN HEADER ----------
 *
 * `rolltui/c/rolltui_json.h`'s own comment explains why `json::Value` still crosses ITS
 * boundary as a tree for six OTHER C++ modules that are not ported by this task. AppProfile
 * has no such excuse: nothing outside this file and `rolltui/AppProfile.cpp`'s thin shim
 * ever touches a `RolltuiJsonValue`, so `rolltui_app_profile_parse`/`_dump` take and return
 * TEXT ONLY — never a JSON tree — with no qualification needed. Internally this file builds
 * a `RolltuiJsonValue` tree with `rolltui_json.h` to parse and to serialise, exactly the way
 * `rolltui_presets.c` already does for its three domains; the tree is built, walked and freed
 * entirely inside this translation unit and is not part of this header's API. This is the
 * concrete instance CLAUDE.md's design note points at: `rolltui-paint` includes `Json.hpp`
 * today for exactly one call, `json::dump(app_profile_to_json(...))`, purely because the OLD
 * C++ API hands a profile back as a tree instead of as text — this header is what that call
 * site can be rewritten against, once a later step deletes the C++ shim (out of this task's
 * scope; see `AppProfile.hpp`'s note on why the shim still exposes a tree-returning overload
 * for now).
 *
 * ---- WHY `RolltuiAppProfile` IS OPAQUE, UNLIKE `RolltuiJsonValue` -----------------------
 *
 * Nothing outside this file ever reads or writes one directly — the C++ shim in
 * `AppProfile.cpp` walks it purely through the accessors below, because `rolltui::AppProfile`
 * (the C++ struct roll's `TuiFrontend.cpp` and `rolltui-paint` build BY HAND with aggregate
 * init, `push_back` and a direct `std::vector<ActionDecl>` assignment) keeps its own
 * independent shape for the same reason `json::Value` keeps its own — see `AppProfile.hpp`.
 * An opaque handle accessed only through named functions is the same choice
 * `rolltui_presets.h`'s `RolltuiPresetStore` already made for exactly this reason.
 *
 * ---- THE BOUNDARY'S RULES, all inherited from Phase 14/15/17 and none new --------------
 *   1. NOTHING is returned BY VALUE; accessors hand back BORROWS (a `const char*` plus an
 *      out `size_t*` length, valid as long as the `RolltuiAppProfile` is), and `_dump` fills
 *      a caller-owned `RolltuiStr*`.
 *   2. EVERY ALLOCATION goes through `rolltui_alloc.h`'s closed set, named at the call site.
 *   3. No process-wide retention: a profile is parsed, read, freed — there is no cache and
 *      nothing for `rolltui::shutdown()` to release, unlike the preset system's shipped-
 *      preset cache.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The three-way source rule a registered kind states (`AppProfile.hpp`'s `Kind::rule`,
 * `rolltui::SourceRule` in `Layout.hpp`). Named separately from `rolltui_layout.h`'s
 * `ROLLTUI_SOURCE_*` rather than reusing them, the same way `AppProfile.cpp`'s original
 * `rule_name`/`rule_from_name` table was already self-contained rather than reaching into
 * Layout for one: nothing here needs the rest of `rolltui_layout.h`'s (much larger) surface,
 * and the numeric VALUES matching is a documented fact the C++ shim converts through an
 * explicit switch, never a `static_cast` relying on the two staying numbered alike. */
#define ROLLTUI_APP_PROFILE_SOURCE_REQUIRED 0
#define ROLLTUI_APP_PROFILE_SOURCE_OPTIONAL 1
#define ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN 2

typedef struct RolltuiAppProfile RolltuiAppProfile; /* opaque; see the header comment */

/* ---- the report: unknown keys and bad values are warnings, not failures ---------------
 * Transparent (unlike `RolltuiAppProfile`) because nothing about it needs hiding — it is
 * exactly `RolltuiStr` values in GROWING AMORTISED arrays, the same shape
 * `rolltui_presets.c`'s own `NameList` uses for "an array of small owned strings". Zero-
 * initialise before use (`RolltuiAppProfileReport r = {0};`), the same rule `RolltuiStr`
 * itself states. */
typedef struct RolltuiAppProfileReport {
  RolltuiStr error; /* non-empty: unusable, and `rolltui_app_profile_parse` returns NULL */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
} RolltuiAppProfileReport;

void rolltui_app_profile_report_release(RolltuiAppProfileReport* r); /* frees everything; zeroes it */
void rolltui_app_profile_report_set_error(RolltuiAppProfileReport* r, const char* s, size_t len);
void rolltui_app_profile_report_add_unknown_key(RolltuiAppProfileReport* r, const char* s, size_t len);
void rolltui_app_profile_report_add_bad_value(RolltuiAppProfileReport* r, const char* s, size_t len);
int rolltui_app_profile_report_clean(const RolltuiAppProfileReport* r);
/* Mirrors `AppProfileReport::summary()` exactly: empty when clean; otherwise the error, or
 * "bad: x; unknown: y" joined the same way. Replaces `*out`. */
void rolltui_app_profile_report_summary(const RolltuiAppProfileReport* r, RolltuiStr* out);

/* ---- parse / dump: TEXT across the boundary, never a tree ------------------------------ */

/* Parses a profile from its file's TEXT. Returns an OWNED profile the caller frees with
 * `rolltui_app_profile_free`, or NULL when it is unusable (a JSON syntax error, the JSON is
 * not an object, or it has no "app" name) — `report->error` says which. A profile with
 * unknown keys or malformed entries is still returned; those go into `report` as warnings,
 * the same non-fatal/fatal split `load_app_profile` already made. `report` is reset by this
 * call (as if freshly zero-initialised) whether it succeeds or fails. */
RolltuiAppProfile* rolltui_app_profile_parse(const char* text, size_t len, RolltuiAppProfileReport* report);
void rolltui_app_profile_free(RolltuiAppProfile* p); /* a no-op on NULL */

/* Serialises to TEXT, REPLACING `*out` — never a `RolltuiJsonValue*` handed back for the
 * caller to dump itself, which is the fix this module makes (see the header comment).
 * indent = 0 -> single line, matching `rolltui_json_dump`. */
void rolltui_app_profile_dump(const RolltuiAppProfile* p, int indent, RolltuiStr* out);

/* ---- building one from scratch: what `AppProfile.cpp`'s shim calls when a C++ host has
 * already built a `rolltui::AppProfile` by hand (`TuiFrontend.cpp`'s `roll_app_profile()`,
 * `paint.cpp`'s `paint_profile()`) and needs a `RolltuiAppProfile*` to hand to `_dump`.
 * `rolltui_app_profile_parse` above is built out of these same primitives while walking
 * parsed JSON, so there is exactly one way a profile's fields get set, read by two callers
 * rather than duplicated for each. ---------------------------------------------------------- */
RolltuiAppProfile* rolltui_app_profile_new(void);
void rolltui_app_profile_set_app(RolltuiAppProfile* p, const char* s, size_t len);
void rolltui_app_profile_set_min_size(RolltuiAppProfile* p, int width, int height);
void rolltui_app_profile_add_action(RolltuiAppProfile* p, const char* name, size_t name_len, const char* desc,
                                    size_t desc_len);
void rolltui_app_profile_add_kind(RolltuiAppProfile* p, const char* name, size_t name_len, int rule,
                                  const char* describes, size_t describes_len);
void rolltui_app_profile_add_document(RolltuiAppProfile* p, const char* name, size_t name_len, const char* sample,
                                      size_t sample_len);
/* Returns the new row's index, so its samples can be added with `_row_add_sample`. */
size_t rolltui_app_profile_add_row(RolltuiAppProfile* p, const char* name, size_t name_len);
void rolltui_app_profile_row_add_sample(RolltuiAppProfile* p, size_t row_i, const char* label, size_t label_len,
                                       const char* value, size_t value_len);
void rolltui_app_profile_add_submit(RolltuiAppProfile* p, const char* s, size_t len);
void rolltui_app_profile_add_note(RolltuiAppProfile* p, const char* s, size_t len);
void rolltui_app_profile_add_menu(RolltuiAppProfile* p, const char* name, size_t name_len, const char* json,
                                  size_t json_len);
void rolltui_app_profile_set_help(RolltuiAppProfile* p, const char* lead, size_t lead_len, const char* note,
                                  size_t note_len);
void rolltui_app_profile_add_help_scope(RolltuiAppProfile* p, const char* s, size_t len);

/* ---- reading one: BORROWS, valid as long as `p` is ------------------------------------- */

const char* rolltui_app_profile_app(const RolltuiAppProfile* p, size_t* len);
int rolltui_app_profile_min_width(const RolltuiAppProfile* p);
int rolltui_app_profile_min_height(const RolltuiAppProfile* p);

size_t rolltui_app_profile_action_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_action_name(const RolltuiAppProfile* p, size_t i, size_t* len);
const char* rolltui_app_profile_action_description(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_kind_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_kind_name(const RolltuiAppProfile* p, size_t i, size_t* len);
int rolltui_app_profile_kind_rule(const RolltuiAppProfile* p, size_t i); /* ROLLTUI_APP_PROFILE_SOURCE_* */
const char* rolltui_app_profile_kind_describes(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_document_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_document_name(const RolltuiAppProfile* p, size_t i, size_t* len);
const char* rolltui_app_profile_document_sample(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_row_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_row_name(const RolltuiAppProfile* p, size_t i, size_t* len);
size_t rolltui_app_profile_row_sample_count(const RolltuiAppProfile* p, size_t i);
const char* rolltui_app_profile_row_sample_label(const RolltuiAppProfile* p, size_t i, size_t j, size_t* len);
const char* rolltui_app_profile_row_sample_value(const RolltuiAppProfile* p, size_t i, size_t j, size_t* len);

size_t rolltui_app_profile_submit_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_submit_at(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_note_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_note_at(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_menu_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_menu_name(const RolltuiAppProfile* p, size_t i, size_t* len);
const char* rolltui_app_profile_menu_json(const RolltuiAppProfile* p, size_t i, size_t* len);

const char* rolltui_app_profile_help_lead(const RolltuiAppProfile* p, size_t* len);
const char* rolltui_app_profile_help_note(const RolltuiAppProfile* p, size_t* len);
size_t rolltui_app_profile_help_scope_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_help_scope_at(const RolltuiAppProfile* p, size_t i, size_t* len);

/* ---- what a layout may NAME in this app, and MOUNTING it (Phase 17 m3) ---------------------
 * Both were `AppProfile.cpp`'s, left behind by m1 because they touch `Windows` rather than the
 * profile's own serialising algorithm. They are here because they are the profile's MEANING —
 * the answer to "what may a screen for this app say?" — and the alternative to moving them was
 * not a C++ home but no home at all.
 *
 * `_content_count`/`_content_at` enumerate the contents an authoring tool may OFFER: one per
 * document ("transcript:NAME"), submit ("input:NAME"), row source ("rows:NAME") and menu
 * ("menu:NAME"), then bare "help", one "help:SCOPE" per declared scope, and one per registered
 * kind. `*out` is REPLACED and the caller owns it.
 *
 * DELIBERATELY NOT `rolltui_content_format`: a kind that takes a source is offered as
 * "NAME:" — a template with the source left for the author to type — for BOTH Required and
 * Optional, where the join rule would drop the colon on an Optional kind with no source. These
 * are two different questions ("what does this resolved content spell as?" versus "what should
 * a picker put in the field?") and collapsing them would silently offer an Optional kind with
 * no way to see it takes a source. */
size_t rolltui_app_profile_content_count(const RolltuiAppProfile* p);
void rolltui_app_profile_content_at(const RolltuiAppProfile* p, size_t i, RolltuiStr* out);

/* Mounts the profile into a window table so a tool renders as the TARGET app: its kinds as
 * PLACEHOLDER widgets (never an error panel — the window is correct, this tool simply is not
 * the app that can build it), its documents as sample content the table owns, its row sources
 * as their recorded samples, its submits and notes as no-ops, its menu files verbatim, and its
 * help scopes in place of the tool's own when it declared any.
 *
 * ONE CALL, so a tool's wiring cannot half-apply a profile — which is the whole property the
 * function exists for and the reason it is not six calls a host makes in an order it chooses.
 * `w` must outlive nothing in `p`: everything crossing here is COPIED. */
void rolltui_app_profile_mount(const RolltuiAppProfile* p, RolltuiWindows* w);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_APP_PROFILE_H */
