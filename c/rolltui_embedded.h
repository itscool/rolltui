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
