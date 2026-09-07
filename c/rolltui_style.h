#ifndef ROLLTUI_C_STYLE_H
#define ROLLTUI_C_STYLE_H
/*
 * rolltui/c/rolltui_style.h — INTERNAL.
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
/* The role's name, as the themes and layout files spell it ("md_code_block"). BORROWS a
 * static literal, valid for the life of the process; `*len` may be NULL. Returns "" for an
 * out-of-range value rather than reading past the table. */
const char* rolltui_role_name(unsigned char role, size_t* len);

/* The role of that name, or -1 when there is none. This is what a theme LOADER needs and had
 * no way to ask for in C — `rolltui_theme_load` was handed a vocabulary by its caller for
 * exactly this reason, and can now be handed the library's own. */
int rolltui_role_from_name(const char* name, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_STYLE_H */
