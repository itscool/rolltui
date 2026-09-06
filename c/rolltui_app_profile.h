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
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif
void rolltui_app_profile_report_set_error(RolltuiAppProfileReport* r, const char* s, size_t len);
void rolltui_app_profile_report_add_unknown_key(RolltuiAppProfileReport* r, const char* s, size_t len);
void rolltui_app_profile_report_add_bad_value(RolltuiAppProfileReport* r, const char* s, size_t len);

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
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
