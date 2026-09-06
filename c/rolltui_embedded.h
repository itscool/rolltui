#ifndef ROLLTUI_C_EMBEDDED_H
#define ROLLTUI_C_EMBEDDED_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_embedded.h — the shipped presets, compiled in, as C data.
 *
 * `rolltui/presets/{themes,layouts,menus,bindings}/` JSON files are REAL FILES in the source
 * tree — the editors write them, and `files_only_test` proves a whole screen can be nothing
 * but files. These tables are the same bytes compiled in, so a fresh install has working
 * defaults with nothing on disk, and `presets_test` asserts the two cannot drift.
 *
 * THEY ARE C BECAUSE A TABLE ONLY ONE LANGUAGE CAN READ IS A TABLE THE OTHER WILL
 * DUPLICATE. They used to be `std::pair<std::string_view, std::string_view>` in a C++
 * namespace. On 2026-09-04 `rolltui-paint`, converted to the C API, could not reach them and
 * copied four scopes of `presets/bindings/default.json` into itself — the fifth copy of a
 * vocabulary that day, and CLAUDE.md had just gained the rule against it. The fix is not a
 * per-host accessor; it is that the data every consumer parses is reachable by every
 * consumer.
 *
 * `text` is a BORROW into static storage: valid for the life of the process, never freed.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
