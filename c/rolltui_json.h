#ifndef ROLLTUI_C_JSON_H
#define ROLLTUI_C_JSON_H
/*
 * rolltui/c/rolltui_json.h — INTERNAL.
 *
 * The library's own declarations for this module. The PUBLIC API is `rolltui/rolltui.h`,
 * which declares everything a consumer may call; nothing below is promised to one, so its
 * shape can change without breaking a host.
 *
 * A suite that needs an internal declaration includes this header BY NAME and lists itself
 * in `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
RolltuiJsonValue* rolltui_json_bool(int b);
RolltuiJsonValue* rolltui_json_number(double n);

RolltuiJsonValue* rolltui_json_object(void);

int rolltui_json_equal(const RolltuiJsonValue* a, const RolltuiJsonValue* b);

int rolltui_json_is_null(const RolltuiJsonValue* v);

int rolltui_json_is_string(const RolltuiJsonValue* v);
int rolltui_json_is_array(const RolltuiJsonValue* v);
int rolltui_json_is_object(const RolltuiJsonValue* v);

double rolltui_json_as_number(const RolltuiJsonValue* v, double def);

int rolltui_json_has(const RolltuiJsonValue* v, const char* key, size_t key_len);

/* Generic object iteration (unordered lookup by key is `get`/`has` above; this is for a
 * caller that must see every member, e.g. a loader's "unknown key" scan). BORROWS. */
size_t rolltui_json_object_size(const RolltuiJsonValue* v);
const char* rolltui_json_object_key_at(const RolltuiJsonValue* v, size_t i, size_t* len);
RolltuiJsonValue* rolltui_json_object_value_at(const RolltuiJsonValue* v, size_t i);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_JSON_H */
