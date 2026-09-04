#ifndef ROLLTUI_C_ABI_H
#define ROLLTUI_C_ABI_H
/*
 * rolltui/c/rolltui_abi.h — THE THREE DUAL-LANGUAGE SPELLINGS, IN ONE PLACE (Phase 14).
 *
 * m2 wrote these at the top of `rolltui_style.h` because it was the only header that
 * needed them. m3 is the third (`rolltui_unicode.h`, `rolltui_wrap.h`), so they move here
 * rather than being copied — a macro defined twice is a macro that can disagree with
 * itself, which is the same "second place to be wrong" that decided m2's one-definition
 * rule for the structs.
 *
 *   ROLLTUI_DEFAULT(v)     C has no default member initializers and C++ needs them —
 *                          `Style s;` must be a blank style at every one of its call
 *                          sites, not garbage. The FIELD is declared once and only its
 *                          initializer varies, so there is still nothing to keep in sync.
 *
 *   ROLLTUI_STATIC_ASSERT  the layout is checked BY BOTH COMPILERS, on every include. That
 *                          is what makes "one definition" a fact rather than an intention:
 *                          if the two languages ever disagreed about a size or an offset,
 *                          the build stops instead of producing a plausible frame.
 *
 *   ROLLTUI_CODEPOINT      the one scalar whose SPELLING differs by language, for the same
 *                          reason `Color::Kind` does: C++ has `char32_t` as a distinct
 *                          builtin type and C does not, and casting a `unsigned int*` to a
 *                          `char32_t*` at the seam would be exactly the layout-compatible-
 *                          by-fiat this project keeps being burned by. Each language names
 *                          its own 4-byte unsigned scalar; the assertion says they are the
 *                          same width, and `extern "C"` linkage makes the pointer the same
 *                          pointer. Nothing is ever cast.
 */

/* `static inline` and the null pointer, spelled once — a header that carries a shared
 * definition for both languages needs both, and `NULL` is not `nullptr` in C++'s eyes. */
#define ROLLTUI_INLINE inline

#ifdef __cplusplus
#define ROLLTUI_NULL nullptr
#define ROLLTUI_DEFAULT(v) = v
#define ROLLTUI_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
typedef char32_t RolltuiCodepoint;
#else
#define ROLLTUI_NULL ((void*)0)
#define ROLLTUI_DEFAULT(v)
#define ROLLTUI_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
typedef unsigned int RolltuiCodepoint;
#endif

ROLLTUI_STATIC_ASSERT(sizeof(RolltuiCodepoint) == 4,
                      "a code point must be the same four-byte unsigned scalar in both languages");

#endif /* ROLLTUI_C_ABI_H */
