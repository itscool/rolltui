#ifndef ROLLTUI_C_JSON_H
#define ROLLTUI_C_JSON_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_json.h — A SMALL JSON VALUE AND PARSER OF OUR OWN, as C (Phase 17 m1).
 *
 * So the library's file formats (themes, layouts, menus, config) need no dependency: "two
 * headers and go" for a consumer, and one parser held to one error-reporting standard. Not a
 * general-purpose JSON library: no streaming, no comments, numbers are doubles, objects keep
 * insertion order (so a theme's role list round-trips in the order the author wrote it).
 * `rolltui/Json.hpp`'s header comment states the same three lines of usage this file's
 * functions give C; none of it is repeated twice on purpose.
 *
 * ---- WHY THE C++ SIDE IS NOT "THE SAME STRUCT", UNLIKE MOST OF THIS PORT --------------
 *
 * Every other recursive data structure ported so far (`rolltui_document.h`,
 * `rolltui_menu_tree.h`) made the C++ type literally BE the C struct, with C++ methods added
 * under `#ifdef __cplusplus` — one definition, Phase 14's rule. Json is the one place that
 * rule is deliberately NOT followed, and it is a finding rather than an oversight:
 * `rolltui::json::Value` is read and written by roughly 40 call sites across six OTHER
 * modules that are staying C++ for now (`Theme.cpp`, `Layout.cpp`, `Menu.cpp`, `Bindings.cpp`,
 * `Presets.cpp`, `ThemeGen.cpp`, `ThemeAnalysis.cpp`, `PresetStore.hpp`), none of which this
 * task ports, and at least three of which use operations a C-backed proxy cannot honestly
 * offer without becoming a second, parallel container implementation:
 *   - `Presets.cpp`: `body.obj.erase(std::remove_if(body.obj.begin(), body.obj.end(), …),
 *     body.obj.end())` — a real `std::vector` erase-remove over `.obj`.
 *   - `Theme.cpp`: `s.frames.push_back(x.arr[i].str)` — `.str` handed straight into a
 *     `std::vector<std::string>::push_back`, which needs an implicit `std::string` (a
 *     `RolltuiStr`, deliberately, has no implicit conversion to one — see rolltui_str.h).
 *   - `TuiFrontend.cpp`/`paint.cpp`: `p.actions = rolltui::shipped_default_actions();`, a
 *     direct `std::vector<ActionDecl>` assignment, plus aggregate-init `push_back({…})` and
 *     `.emplace_back(...)` calls that require `Value`'s members to be real `std::string` /
 *     `std::vector`, not a typed view over this file's arrays.
 * So `rolltui::json::Value` KEEPS its existing `std::string`/`std::vector` shape exactly
 * (Phase 17's task instructions call this out explicitly as the reason AppProfile is paired
 * with Json rather than ported alone). What actually moves to C is the ALGORITHM — parsing
 * and serialising — which is the actual bulk of "a small hand-written JSON parser". The C++
 * `parse()`/`dump()` in `Json.cpp` become thin shims: convert in, call this file, convert out.
 * That is a real cost (a full tree conversion on every parse and every dump) that a same-
 * struct port would not have paid; it buys the ~40 call sites above zero required changes,
 * which is what "thin C++ shim so no existing caller changes" means for this module
 * specifically.
 *
 * PHASE 17 m2 ported two of the six anyway — `Bindings.cpp` and `Menu.cpp` (`Menu.hpp`'s
 * `menu_from_json`/`_to_json`, `Bindings.hpp`'s `from_json`/`to_json`) — because, unlike the
 * other four, NEITHER had a sibling algorithm staying C++ to entangle it: `Theme.cpp`'s and
 * `Presets.cpp`'s Layout domain each call into a same-language function that must keep seeing
 * a real `Value` (`load_theme`, `load_layout`), and `TuiFrontend.cpp`/`paint.cpp` build one by
 * real container ops directly. A menu file and a bindings file have no such neighbour, so the
 * whole walk moved to this file's C API (`rolltui_menu_parse_json`/`rolltui_bindings_load_json`
 * and their `_dump_json`/`_to_json` counterparts) rather than a shell calling back across the
 * boundary. `Bindings.cpp`'s `json::Value` overloads are now themselves a thin shim OVER that
 * text path (dump in, parse out) — kept only because two callers still hand it a parsed tree.
 * `Presets.cpp`'s `ThemeDomain` and `LayoutDomain` were NOT re-examined by that task and remain
 * exactly as reasoned above; deleting the Bindings shim entirely (its two remaining callers
 * moving to the text-based overloads) is a further, smaller step, still out of scope.
 *
 * `rolltui/AppProfile.hpp`'s new C backing (`rolltui_app_profile.h`) is NOT in this
 * position: nothing outside `rolltui_app_profile.c` itself ever touches a `RolltuiJsonValue`,
 * so it parses and builds trees with THIS header directly and hands text across ITS OWN
 * boundary — the fix `CLAUDE.md` asks for (`parse` takes text and hands back an owned value;
 * a tree never crosses) applies there without qualification. See that header's own comment.
 *
 * ---- THE BOUNDARY'S RULES, all inherited from Phase 14/15 and none new -----------------
 *   1. NOTHING is returned BY VALUE from an `extern "C"` function here; a tree is always a
 *      `RolltuiJsonValue*` the caller owns (or a BORROW clearly marked as one), and dumped
 *      text lands in a caller-owned `RolltuiStr*`.
 *   2. EVERY ALLOCATION goes through `rolltui_alloc.h`'s closed set — GROWING AMORTISED for
 *      the array/object member lists (`rolltui_grow`, laid out exactly as `RolltuiPtrVec`
 *      the way `RolltuiMenuItemList`/`RolltuiDocument` already do, because an array holds
 *      POINTERS to things it owns), OWNED/LONG-LIVED for each individually allocated node.
 *   3. `rolltui_json_get`/`_as_string` etc. hand back BORROWS, valid exactly as long as the
 *      value they were read from — the same rule `Value::get`'s `const Value&` already had.
 *
 * ---- ERROR REPORTING, preserved exactly ------------------------------------------------
 * `rolltui_json_parse` reports "line N: what" on the FIRST failure only, the same text
 * `rolltui/tests/theme_test.cpp` asserts byte-for-byte (including the duplicate-key message
 * and the surrogate-pair decode) — that test is the oracle for this file, not a paraphrase
 * of it.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
