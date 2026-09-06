#ifndef ROLLTUI_C_STYLE_H
#define ROLLTUI_C_STYLE_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_style.h — THE STYLING PODS, DEFINED ONCE (Phase 14 m2).
 *
 * `rolltui::Color` and `rolltui::Style` are `using` aliases for the two structs below.
 * There is no conversion function and nothing to `reinterpret_cast`, because there is only
 * one definition: the methods C++ wants live in `#ifdef __cplusplus` blocks inside the
 * struct, which is the standard dual-language shape.
 *
 * WHY THIS IS ITS OWN HEADER rather than sitting in rolltui_screen.h with `RolltuiCell`:
 * `Style.hpp` is included by almost every translation unit in the library, and the frame's
 * API is not its business. The split mirrors `Style.hpp` / `Screen.hpp` exactly, which is
 * the layering that already exists one level up.
 *
 * THE DUAL-LANGUAGE SPELLINGS `ROLLTUI_DEFAULT` and `ROLLTUI_STATIC_ASSERT` moved to
 * `rolltui_abi.h` in m3, when a third header needed them; that file says what each is for.
 * The one that belongs here is the third:
 *
 *   the attribute bits are `unsigned char` AND NOT `bool`. C's `_Bool` and C++'s `bool` are
 *                       the same byte on every toolchain this will ever see, and that is
 *                       exactly the kind of "layout-compatible by fiat" this project keeps
 *                       being burned by. `unsigned char` is one type in both languages with
 *                       nothing to assume. The cost is real and is recorded rather than
 *                       hidden: `theme_editor.cpp`'s `attr_of` returns `unsigned char&`
 *                       now, and a braced init that used to take a `bool` needs `!= 0`.
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
