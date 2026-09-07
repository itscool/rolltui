#ifndef ROLLTUI_C_DOCUMENT_H
#define ROLLTUI_C_DOCUMENT_H
/*
 * rolltui/c/rolltui_document.h — INTERNAL.
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
void rolltui_doc_entry_init(RolltuiDocEntry* e);

size_t rolltui_document_count(const RolltuiDocument* d);
RolltuiDocEntry* rolltui_document_at(const RolltuiDocument* d, size_t i);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_DOCUMENT_H */
