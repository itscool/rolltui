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
 * THE THREE DUAL-LANGUAGE SPELLINGS BELOW, and why each is the way it is:
 *
 *   ROLLTUI_DEFAULT(v)  C has no default member initializers and C++ needs them — `Style s;`
 *                       must be a blank style at every one of its call sites, not garbage.
 *                       The FIELD is declared once and only its initializer varies, so
 *                       there is still nothing to keep in sync.
 *
 *   ROLLTUI_STATIC_ASSERT  the layout is checked BY BOTH COMPILERS, on every include. That
 *                       is what makes "one definition" a fact rather than an intention: if
 *                       the two languages ever disagreed about a size or an offset, the
 *                       build stops instead of producing a plausible frame.
 *
 *   the attribute bits are `unsigned char` AND NOT `bool`. C's `_Bool` and C++'s `bool` are
 *                       the same byte on every toolchain this will ever see, and that is
 *                       exactly the kind of "layout-compatible by fiat" this project keeps
 *                       being burned by. `unsigned char` is one type in both languages with
 *                       nothing to assume. The cost is real and is recorded rather than
 *                       hidden: `theme_editor.cpp`'s `attr_of` returns `unsigned char&`
 *                       now, and a braced init that used to take a `bool` needs `!= 0`.
 */

#ifdef __cplusplus
#define ROLLTUI_DEFAULT(v) = v
#define ROLLTUI_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define ROLLTUI_DEFAULT(v)
#define ROLLTUI_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

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
