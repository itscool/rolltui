#ifndef ROLLTUI_C_JSON_H
#define ROLLTUI_C_JSON_H
/*
 * rolltui/c/rolltui_json.h — INTERNAL (Phase 20 m6/m7, 2026-09-06).
 *
 * RE-CREATED, the same way `rolltui_render.h` was in m1/m3 and under the same rule: a
 * header exists because a `.c` needs a declaration from it, and one that declares nothing
 * is deleted. Phase 19 m3 deleted this file when every declaration in it was public and
 * lived in the definition; Phase 20 moved this module's operations back to INTERNAL — no
 * CONSUMER reaches them, only the studio, its editors, or a suite that tests
 * implementation — so the `.c` needs its declarations again and the rule re-creates it.
 *
 * A suite that needs one includes this header BY NAME and lists itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PHASE 20 m6/m7: MOVED OUT OF THE DEFINITION ------------------------------------
 * PUBLIC until 2026-09-06, and reached by no CONSUMER: only by the studio or its editors
 * (rolltui's OWN authoring tool for rolltui's OWN files, which opts in like a test) or by a
 * suite that tests implementation. A test's reach is never a reason and neither is the
 * studio's. The code and its tests are unchanged; what changed is that the library no longer
 * PROMISES these, so their shape can move without breaking a consumer. */
RolltuiJsonValue* rolltui_json_bool(int b);
RolltuiJsonValue* rolltui_json_number(double n);

RolltuiJsonValue* rolltui_json_object(void);

int rolltui_json_equal(const RolltuiJsonValue* a, const RolltuiJsonValue* b);

int rolltui_json_is_null(const RolltuiJsonValue* v);

int rolltui_json_is_string(const RolltuiJsonValue* v);
int rolltui_json_is_array(const RolltuiJsonValue* v);
int rolltui_json_is_object(const RolltuiJsonValue* v);

const char* rolltui_json_as_string(const RolltuiJsonValue* v, const char* def, size_t def_len, size_t* out_len);
double rolltui_json_as_number(const RolltuiJsonValue* v, double def);

int rolltui_json_has(const RolltuiJsonValue* v, const char* key, size_t key_len);

/* Generic object iteration (unordered lookup by key is `get`/`has` above; this is for a
 * caller that must see every member, e.g. AppProfile's "unknown key" scan). BORROWS. */
size_t rolltui_json_object_size(const RolltuiJsonValue* v);
const char* rolltui_json_object_key_at(const RolltuiJsonValue* v, size_t i, size_t* len);
RolltuiJsonValue* rolltui_json_object_value_at(const RolltuiJsonValue* v, size_t i);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_JSON_H */
