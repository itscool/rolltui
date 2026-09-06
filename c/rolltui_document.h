#ifndef ROLLTUI_C_DOCUMENT_H
#define ROLLTUI_C_DOCUMENT_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_document.h — THE DOCUMENT MODEL, AS DATA (Phase 15 m5e).
 *
 * An ordered list of entries: text with a role, an optional prefix, markdown or verbatim,
 * foldable or not, possibly in a motion STATE. Every rule — what `version` means, why an
 * entry's state is not part of the layout cache's key, that text is what a renderer will
 * DRAW and never what a terminal will interpret — is stated in `rolltui/Document.hpp`; none
 * of it is repeated here.
 *
 * ---- WHY THIS IS IN BOTH CONFIGURATIONS ------------------------------------------------
 *
 * The same reason the two trees and the span store are: **it is DATA both implementations of
 * the transcript walk**, and it is filled by a HOST — roll's frontend, the studio, the tests
 * — so two shapes of it would be two things a host could disagree with. The flag chooses the
 * transcript's ALGORITHM, never what an entry is.
 *
 * ---- WHAT THE C++ LEFT IMPLICIT --------------------------------------------------------
 *
 * Four `std::string`s per entry and a `std::vector<DocEntry>` holding them. A host appends to
 * that vector every turn, so on each growth every existing entry is MOVED — four string
 * bookkeepings apiece — and every `const DocEntry*` anyone held is invalidated. Nobody
 * decided that; `push_back` is what you type. The list holds POINTERS here, so an entry's
 * address is stable and a growth moves `sizeof(void*)` per entry.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
