#ifndef ROLLTUI_C_APP_PROFILE_H
#define ROLLTUI_C_APP_PROFILE_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
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

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif
void rolltui_app_profile_report_set_error(RolltuiAppProfileReport* r, const char* s, size_t len);
void rolltui_app_profile_report_add_unknown_key(RolltuiAppProfileReport* r, const char* s, size_t len);
void rolltui_app_profile_report_add_bad_value(RolltuiAppProfileReport* r, const char* s, size_t len);

const char* rolltui_app_profile_kind_describes(const RolltuiAppProfile* p, size_t i, size_t* len);

const char* rolltui_app_profile_document_name(const RolltuiAppProfile* p, size_t i, size_t* len);
const char* rolltui_app_profile_document_sample(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_row_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_row_name(const RolltuiAppProfile* p, size_t i, size_t* len);
size_t rolltui_app_profile_row_sample_count(const RolltuiAppProfile* p, size_t i);
const char* rolltui_app_profile_row_sample_label(const RolltuiAppProfile* p, size_t i, size_t j, size_t* len);
const char* rolltui_app_profile_row_sample_value(const RolltuiAppProfile* p, size_t i, size_t j, size_t* len);

const char* rolltui_app_profile_submit_at(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_note_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_note_at(const RolltuiAppProfile* p, size_t i, size_t* len);

const char* rolltui_app_profile_help_lead(const RolltuiAppProfile* p, size_t* len);
const char* rolltui_app_profile_help_note(const RolltuiAppProfile* p, size_t* len);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
