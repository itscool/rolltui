#ifndef ROLLTUI_C_STYLE_H
#define ROLLTUI_C_STYLE_H
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
#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A colour: none, one of the 256 indexed, or 24-bit rgb.
 *
 * `kind` is the one field whose SPELLING differs by language — an `enum class Kind` in C++
 * (so `Color::Kind::Rgb` still reads the way twenty call sites already write it) and the
 * plain byte it has always been in C. The underlying type is FIXED at `unsigned char`, so
 * the two spellings are the same one byte by the standard and not by convention, and the
 * assertion below is what says so out loud. */
/* ---- THE ROLE VOCABULARY, IN THE MODULE THAT OWNS IT (Phase 17, 2026-09-05) -------------
 *
 * THIS REPLACES THE RULE THAT USED TO BE STATED HERE, and the reversal is the point rather
 * than an edit to skim. The old text read: *"The styling vocabulary is `rolltui/Style.hpp`'s
 * and a C file names no role"*, with three ordinals crossing as macros and a static_assert
 * tying each to the enum. **That was right while the library was C++ with a C core, and it is
 * wrong now that the library IS the C.** `rolltui/Style.hpp` is being deleted; a vocabulary
 * that lives there does not survive it, and CLAUDE.md names the failure exactly — *a
 * vocabulary the C refuses to carry does not disappear, it relocates into every caller that
 * cannot reach it*. It had already started: a host converting off the C++ had to invent the
 * role bytes for eight entries and a 49-long style array with no count to size it by.
 *
 * ONE SPELLING, and it is this list. Both languages DERIVE from it — the C enum below, the
 * C++ `enum class Role` in `Style.hpp` while that file exists, and the name table in
 * `rolltui_style.c`. An X-macro rather than three parallel lists because the rule this
 * library keeps re-learning is that a vocabulary written down twice is a second thing to
 * drift, and here the drift would be silent: a role added in one place and not the other
 * draws in the wrong colour rather than failing to build.
 *
 * ORDER IS ABI. The ordinals are what cross in `RolltuiDocEntry`, `RolltuiLayoutNode` and the
 * style array `rolltui_window_stack_compose` is handed. APPEND to the end of a section; never
 * reorder, never insert. */
#include <stddef.h>

#define ROLLTUI_ROLE_LIST(X) \
  X(text, TEXT) \
  X(text_muted, TEXT_MUTED) \
  X(background, BACKGROUND) \
  X(panel_background, PANEL_BACKGROUND) \
  X(border, BORDER) \
  X(border_active, BORDER_ACTIVE) \
  X(title, TITLE) \
  X(label, LABEL) \
  X(value, VALUE) \
  X(accent_1, ACCENT_1) \
  X(accent_2, ACCENT_2) \
  X(accent_3, ACCENT_3) \
  X(accent_4, ACCENT_4) \
  X(prompt, PROMPT) \
  X(note, NOTE) \
  X(warning, WARNING) \
  X(error, ERROR) \
  X(md_heading, MD_HEADING) \
  X(md_emphasis, MD_EMPHASIS) \
  X(md_strong, MD_STRONG) \
  X(md_code_inline, MD_CODE_INLINE) \
  X(md_code_block, MD_CODE_BLOCK) \
  X(md_code_label, MD_CODE_LABEL) \
  X(md_link, MD_LINK) \
  X(md_link_url, MD_LINK_URL) \
  X(md_quote, MD_QUOTE) \
  X(md_list_marker, MD_LIST_MARKER) \
  X(md_table_border, MD_TABLE_BORDER) \
  X(md_table_header, MD_TABLE_HEADER) \
  X(md_rule, MD_RULE) \
  X(md_strikethrough, MD_STRIKETHROUGH) \
  X(diff_added, DIFF_ADDED) \
  X(diff_removed, DIFF_REMOVED) \
  X(diff_context, DIFF_CONTEXT) \
  X(diff_added_word, DIFF_ADDED_WORD) \
  X(diff_removed_word, DIFF_REMOVED_WORD) \
  X(input_text, INPUT_TEXT) \
  X(input_cursor, INPUT_CURSOR) \
  X(input_placeholder, INPUT_PLACEHOLDER) \
  X(scroll_marker, SCROLL_MARKER) \
  X(selection, SELECTION) \
  X(overlay, OVERLAY) \
  X(menu_item, MENU_ITEM) \
  X(menu_selected, MENU_SELECTED) \
  X(menu_breadcrumb, MENU_BREADCRUMB) \
  X(menu_shortcut, MENU_SHORTCUT) \
  X(find_match, FIND_MATCH) \
  X(find_current, FIND_CURRENT) \
  X(scrollbar, SCROLLBAR) \

typedef enum RolltuiRole {
#define ROLLTUI_ROLE_ENUM_(lower, UPPER) ROLLTUI_ROLE_##UPPER,
  ROLLTUI_ROLE_LIST(ROLLTUI_ROLE_ENUM_)
#undef ROLLTUI_ROLE_ENUM_
  ROLLTUI_ROLE_COUNT
} RolltuiRole;

/* The three that cross as struct-field DEFAULTS keep their old names, because a default is
 * the one case the "a renderer is handed the byte" rule cannot cover: there is no call at
 * which to hand one in. They are now ALIASES of the enum rather than hand-written numbers,
 * so the comment that used to say `Role::text` is the definition instead of a promise. */
#define ROLLTUI_ROLE_DEFAULT_TEXT ROLLTUI_ROLE_TEXT
#define ROLLTUI_ROLE_DEFAULT_BACKGROUND ROLLTUI_ROLE_BACKGROUND
#define ROLLTUI_ROLE_DEFAULT_PROMPT ROLLTUI_ROLE_PROMPT

/* The role's name, as the themes and layout files spell it ("md_code_block"). BORROWS a
 * static literal, valid for the life of the process; `*len` may be NULL. Returns "" for an
 * out-of-range value rather than reading past the table. */
const char* rolltui_role_name(unsigned char role, size_t* len);

/* The role of that name, or -1 when there is none. This is what a theme LOADER needs and had
 * no way to ask for in C — `rolltui_theme_load` was handed a vocabulary by its caller for
 * exactly this reason, and can now be handed the library's own. */
int rolltui_role_from_name(const char* name, size_t len);



typedef struct RolltuiStyleColor {
#ifdef __cplusplus
  enum class Kind : unsigned char { None = 0, Indexed = 1, Rgb = 2 };
  Kind kind = Kind::None;
#else
  unsigned char kind; /* 0 none, 1 indexed, 2 rgb */
#endif
  unsigned char index ROLLTUI_DEFAULT(0);              /* Indexed: 0-255 */
  unsigned char r ROLLTUI_DEFAULT(0), g ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* Rgb */

#ifdef __cplusplus
  static constexpr RolltuiStyleColor none() { return {}; }
  static constexpr RolltuiStyleColor indexed(unsigned char i) {
    RolltuiStyleColor c;
    c.kind = Kind::Indexed;
    c.index = i;
    return c;
  }
  static constexpr RolltuiStyleColor rgb(unsigned char red, unsigned char green, unsigned char blue) {
    RolltuiStyleColor c;
    c.kind = Kind::Rgb;
    c.r = red;
    c.g = green;
    c.b = blue;
    return c;
  }
  constexpr bool operator==(const RolltuiStyleColor&) const = default;
#endif
} RolltuiStyleColor;
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiStyleColor) == 5, "RolltuiStyleColor must be five bytes in both languages");

typedef struct RolltuiStyle {
  RolltuiStyleColor fg, bg;
  unsigned char bold ROLLTUI_DEFAULT(0), italic ROLLTUI_DEFAULT(0), underline ROLLTUI_DEFAULT(0),
      dim ROLLTUI_DEFAULT(0), reverse ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  constexpr bool operator==(const RolltuiStyle&) const = default;
#endif
} RolltuiStyle;
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiStyle) == 15, "RolltuiStyle must be fifteen bytes with no padding in either language");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_STYLE_H */
