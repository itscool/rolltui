#ifndef ROLLTUI_C_DIFF_H
#define ROLLTUI_C_DIFF_H
/*
 * rolltui/c/rolltui_diff.h — INTERNAL.
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
/* True for the info strings this colouriser answers to: "diff", "patch", "udiff". The
 * FENCE decides; content is never sniffed (Diff.hpp). */
int rolltui_diff_is_language(const char* lang, size_t lang_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_DIFF_H */
