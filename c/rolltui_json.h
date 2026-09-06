#ifndef ROLLTUI_C_JSON_H
#define ROLLTUI_C_JSON_H
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
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROLLTUI_JSON_NULL 0
#define ROLLTUI_JSON_BOOL 1
#define ROLLTUI_JSON_NUMBER 2
#define ROLLTUI_JSON_STRING 3
#define ROLLTUI_JSON_ARRAY 4
#define ROLLTUI_JSON_OBJECT 5

typedef struct RolltuiJsonValue RolltuiJsonValue;

/* One member of an object: a key and its OWNED value. `obj` below keeps these in insertion
 * order, the same rule `rolltui::json::Value::obj` states (a theme's role list round-trips
 * in the order the author wrote it). */
typedef struct RolltuiJsonMember {
  RolltuiStr key;
  RolltuiJsonValue* value ROLLTUI_DEFAULT(nullptr); /* OWNED; never NULL on a complete value */
} RolltuiJsonMember;

/* A plain struct with every member always present, exactly the shape `rolltui::json::Value`
 * already is (it is not a real tagged union there either — see that header): `kind` says
 * which of `str`/`arr`/`obj` is MEANINGFUL, but `rolltui_json_free`/`_clone`/`_equal`
 * deliberately do not gate on it, so retagging a value (as `rolltui_json_set` always does)
 * can never orphan a buffer the other two left behind. */
struct RolltuiJsonValue {
  unsigned char kind ROLLTUI_DEFAULT(ROLLTUI_JSON_NULL);
  unsigned char b ROLLTUI_DEFAULT(0);
  double num ROLLTUI_DEFAULT(0);
  RolltuiStr str;
  /* ARRAY children: an owned array of owned value pointers, laid out as `RolltuiPtrVec`
   * (rolltui_str.h) and using its GROWING AMORTISED mechanics — the same choice
   * `RolltuiMenuItemList`/`RolltuiDocument` made for the same reason: an element's address
   * never moves, and a growth relocates one pointer per element rather than a whole node. */
  RolltuiJsonValue** arr ROLLTUI_DEFAULT(nullptr);
  size_t arr_n ROLLTUI_DEFAULT(0);
  size_t arr_cap ROLLTUI_DEFAULT(0);
  /* OBJECT members: an owned array of owned member pointers, insertion order kept, same
   * layout and reason as `arr` above. */
  RolltuiJsonMember** obj ROLLTUI_DEFAULT(nullptr);
  size_t obj_n ROLLTUI_DEFAULT(0);
  size_t obj_cap ROLLTUI_DEFAULT(0);
};

/* ---- construction: each an OWNED, LONG-LIVED node the caller frees (directly, or by
 * handing it to `rolltui_json_set`/`_array_push`, which then own it). ---------------------- */
RolltuiJsonValue* rolltui_json_null(void);
RolltuiJsonValue* rolltui_json_bool(int b);
RolltuiJsonValue* rolltui_json_number(double n);
RolltuiJsonValue* rolltui_json_string(const char* s, size_t len);
RolltuiJsonValue* rolltui_json_array(void);
RolltuiJsonValue* rolltui_json_object(void);

void rolltui_json_free(RolltuiJsonValue* v); /* recursive; a no-op on NULL */
/* A deep copy the caller owns; NULL in, NULL out (mirrors `Value`'s copy constructor). */
RolltuiJsonValue* rolltui_json_clone(const RolltuiJsonValue* v);
/* Deep, order-sensitive structural equality (mirrors `Value`'s defaulted `operator==`, which
 * compares every member unconditionally rather than only the ones `kind` says are live). */
int rolltui_json_equal(const RolltuiJsonValue* a, const RolltuiJsonValue* b);

int rolltui_json_is_null(const RolltuiJsonValue* v);
int rolltui_json_is_bool(const RolltuiJsonValue* v);
int rolltui_json_is_number(const RolltuiJsonValue* v);
int rolltui_json_is_string(const RolltuiJsonValue* v);
int rolltui_json_is_array(const RolltuiJsonValue* v);
int rolltui_json_is_object(const RolltuiJsonValue* v);

/* Typed reads with defaults; never fail. A BORROW valid as long as `v` (or `def`) is. */
const char* rolltui_json_as_string(const RolltuiJsonValue* v, const char* def, size_t def_len, size_t* out_len);
double rolltui_json_as_number(const RolltuiJsonValue* v, double def);
int rolltui_json_as_bool(const RolltuiJsonValue* v, int def);

/* Object lookup; a BORROW, and never NULL — a static Null (also a BORROW, valid forever)
 * when `v` is not an object or the key is absent, the same "missing keys are Null and
 * chainable" rule `Value::get` states. */
const RolltuiJsonValue* rolltui_json_get(const RolltuiJsonValue* v, const char* key, size_t key_len);
int rolltui_json_has(const RolltuiJsonValue* v, const char* key, size_t key_len);
/* Object insert-or-replace. ALWAYS turns `v` into an object (matches `Value::set` exactly,
 * including on a `v` that was something else — nothing is cleared, which is safe here only
 * because free/clone/equal above never gate on `kind`). TAKES OWNERSHIP of `child`; a
 * replaced value is freed. Returns a BORROW of the now-stored child. */
RolltuiJsonValue* rolltui_json_set(RolltuiJsonValue* v, const char* key, size_t key_len, RolltuiJsonValue* child);
/* Removes `key` if the object has it, freeing the value; 1 when something was removed. The
 * erase-remove over `.obj` this header's own note lists as a C++-only operation — it is not
 * one, it is a missing function, and `rolltui_preset_migrate_theme_layout` is what asked for
 * it (Phase 17 m3). Order-preserving, like the `std::remove_if` it replaces. */
int rolltui_json_object_erase(RolltuiJsonValue* v, const char* key, size_t key_len);

size_t rolltui_json_array_size(const RolltuiJsonValue* v);
RolltuiJsonValue* rolltui_json_array_at(const RolltuiJsonValue* v, size_t i); /* BORROW; NULL out of range */
/* Appends. TAKES OWNERSHIP of `child`. `v` must already be an array (`rolltui_json_array()`)
 * — no coercion, matching `.arr.push_back()` on the C++ side never touching `.kind` either. */
void rolltui_json_array_push(RolltuiJsonValue* v, RolltuiJsonValue* child);

/* Generic object iteration (unordered lookup by key is `get`/`has` above; this is for a
 * caller that must see every member, e.g. AppProfile's "unknown key" scan). BORROWS. */
size_t rolltui_json_object_size(const RolltuiJsonValue* v);
const char* rolltui_json_object_key_at(const RolltuiJsonValue* v, size_t i, size_t* len);
RolltuiJsonValue* rolltui_json_object_value_at(const RolltuiJsonValue* v, size_t i);

/* Parses `text`. Returns an OWNED value the caller frees, or NULL on failure. `error` may be
 * NULL when the caller does not care; otherwise it is cleared on entry and set to
 * "line N: message" on the first failure only, and left empty on success. */
RolltuiJsonValue* rolltui_json_parse(const char* text, size_t len, RolltuiStr* error);
/* Serialises deterministically into `out`, REPLACING its contents. indent = 0 -> single
 * line. */
void rolltui_json_dump(const RolltuiJsonValue* v, int indent, RolltuiStr* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_JSON_H */
