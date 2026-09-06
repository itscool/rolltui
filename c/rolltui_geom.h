#ifndef ROLLTUI_C_GEOM_H
#define ROLLTUI_C_GEOM_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_geom.h — THE SEAM (Phase 14 m1).
 *
 * The first header both implementations present. Nothing here is interesting on its own:
 * it is rectangle arithmetic. Its job is to make the SWITCH real — one API, two object
 * files, a CMake flag choosing which one links, and the whole test suite green either way
 * — before a line of the actual port is written.
 *
 * WHY A C HEADER AND NOT A C++ ONE: whatever the verdict, this file has to be readable by
 * a C compiler, so the shape is decided now rather than discovered in m3. Two rules it
 * fixes for everything that follows:
 *   - **NO STRUCTS ACROSS THE BOUNDARY YET.** Ints in, ints out through a caller's array.
 *     A struct by value is a layout-and-ABI question, and there is no reason to answer it
 *     for four integers.
 *   - **THE CALLER OWNS EVERY BUFFER.** `out` is the caller's four ints. This is the shape
 *     the string-carrying functions in m3 will have to use, so it is worth being the shape
 *     of the trivial one too.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* {guard} */
