#ifndef ROLLTUI_C_EMBEDDED_H
#define ROLLTUI_C_EMBEDDED_H
/*
 * rolltui/c/rolltui_embedded.h — the shipped presets, compiled in, as C data.
 *
 * `rolltui/presets/{themes,layouts,menus,bindings}/*.json` are REAL FILES in the source
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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BOTH FIELDS ARE BARE `const char*`, AND A C++ CALLER MUST NOT COMPARE THEM WITH `==`.
 * `table[i].name == "default"` compiles and compares POINTERS. It cost an afternoon on
 * 2026-09-04: Release passed 51/51 because the compiler merged the identical literals so the
 * pointers really were equal, and the RelWithDebInfo sanitizer build — which did not merge
 * them — failed 14 of 31 suites. The green was not stale; it was true and useless.
 *
 * Everywhere else in this library an owned string is `RolltuiStr`, which carries
 * `operator==(const char*)` and does the right thing. This struct cannot be one: it is a
 * static table of literals that owns nothing. So the obligation moves to the caller —
 * **wrap in `std::string_view` before comparing**, and prefer `rolltui_embedded_text()`
 * below, which does the comparison correctly once so no caller has to. */
typedef struct RolltuiEmbeddedFile {
  const char* name; /* the file's stem: "default", "no-panel" */
  const char* text; /* its bytes, NUL-terminated */
} RolltuiEmbeddedFile;

/* The library's four shipped sets, and their counts. A HOST embedding its own files passes
 * its own PREFIX to the generator, so its table cannot collide with these. */
extern const RolltuiEmbeddedFile rolltui_kThemePresets[];
extern const size_t rolltui_kThemePresetCount;
extern const RolltuiEmbeddedFile rolltui_kLayoutPresets[];
extern const size_t rolltui_kLayoutPresetCount;
extern const RolltuiEmbeddedFile rolltui_kMenus[];
extern const size_t rolltui_kMenuCount;
extern const RolltuiEmbeddedFile rolltui_kBindingsPresets[];
extern const size_t rolltui_kBindingsPresetCount;

/* The text of one shipped file by name, or NULL. A BORROW, valid for the process. */
const char* rolltui_embedded_text(const RolltuiEmbeddedFile* table, size_t count, const char* name,
                                  size_t name_len);

#ifdef __cplusplus
}
#endif
#endif /* ROLLTUI_C_EMBEDDED_H */
