#ifndef ROLLTUI_H
#define ROLLTUI_H
/*
 * rolltui.h — THE library's one public header.
 *
 * #include "rolltui/rolltui.h"  and you have the whole API. Nothing else in `rolltui/c/` is
 * a consumer's business, and nothing in `rolltui/` outside it is either.
 *
 * WHAT THIS FILE IS ALLOWED TO CONTAIN: `#include`s, comments, and the guard. Nothing else —
 * no types, no functions, no macros of its own. That is asserted by a grep control in
 * `rolltui/tests/public_header_test.cpp`, and the reason is not tidiness: the moment this
 * file declares something itself, the headers below stop being self-sufficient, a consumer
 * that includes one directly gets a subtly different API from one that includes this, and
 * the layering claim becomes false without anything failing.
 *
 * ============================================================================
 * HOW THE PUBLIC SET WAS CHOSEN — derived, not asserted
 * ============================================================================
 * Two measurements, on 2026-09-04, rather than a judgement about what "feels" public:
 *
 *   1. **What consumers actually reach for.** The four suites already ported to the C API
 *      (`layout`, `markdown`, `transcript`, `input`) include twelve of these headers between
 *      them. That is observed fact — and it already contradicted a guess: `rolltui_md_lines.h`
 *      and `rolltui_layout_tree.h` read like internal data structures and are reached
 *      directly by consumers, so they are public.
 *   2. **The include graph.** A header nothing else includes is a LEAF — an entry point a
 *      consumer reaches for on purpose (`transcript`, `widgets`, `menu`, `terminal`, `swap`,
 *      `presets`, `app_profile`, `diff`, `render`, `wrap`, the two theme tools). A header
 *      many others include is VOCABULARY — `style`, `geom`, `keys`, `screen`, `str`, `abi` —
 *      and is public because the leaves' own signatures speak it.
 *
 * **TWO are kept out** — the ones neither measurement reaches, each machinery a consumer
 * never names:
 *   - `c/rolltui_alloc.h`  — the CLOSED SET of allocation strategies (`rolltui_grow`,
 *                            `rolltui_fit`, the pack builder). Internal by construction:
 *                            `ownership_test` asserts that only the library grows a buffer.
 *   - `c/rolltui_map.h`    — the string-keyed table the library builds its registries from.
 *
 * It was THREE until Phase 17 m2c: `c/rolltui_marker.h` left the internal list when
 * `transcript_test.cpp` began asserting the "▼ N more" rule directly, so a consumer reaches
 * it and measurement 1 makes it public. This prose said three and listed marker while line 120
 * included it — corrected 2026-09-05. `public_header_test`'s `kInternal` is the enforced
 * list; this paragraph is commentary on it and can drift, which it did.
 *
 * **`rolltui_str.h` is IN, and it is the one genuinely awkward case.** It was written as an
 * internal container and it appears in public signatures anyway — in the ~15 functions of
 * rule 3(b) below, the ones whose output has no bound. That makes it vocabulary whether or
 * not it was meant to be. It is included here rather than hidden, because a header a
 * consumer must include to call the API is public by definition, and pretending otherwise
 * would be the "documented one way, used another" split this library refuses everywhere
 * else. **If that ever feels wrong, the fix is not hiding the header — it is giving those
 * fifteen functions a bound, which would move them to rule 3(a) and retire the type from the
 * public set honestly.**
 *
 * ============================================================================
 * THE FIVE RULES EVERY HEADER BELOW OBEYS
 * ============================================================================
 * Stated once here so a consumer learns them once rather than per module:
 *
 *   1. **A handle is created and released in a pair.** `rolltui_x_new(...)` /
 *      `rolltui_x_free(...)`, and `free` is always a no-op on NULL. There is no RAII to lean
 *      on; there is `rolltui_shutdown()` (`c/rolltui_lifetime.h`), after which the library
 *      holds NOTHING — asserted as `live_bytes == 0 && live_blocks == 0`, which is how a
 *      missed release is caught rather than hoped about.
 *   2. **Nothing is returned BY VALUE from an `extern "C"` function.** Clang will not
 *      promise an ABI for a non-POD return, and a caller's buffer was the better answer
 *      anyway — it is reused across calls instead of rebuilt.
 *   3. **TEXT OUT HAS EXACTLY THREE SHAPES, AND WHICH ONE IS NOT A MATTER OF TASTE.** The
 *      rule was consistent in the code before it was written here, which is the same as not
 *      having one — stated 2026-09-04 after a reader asked why two of them coexist:
 *        (a) **BOUNDED** — `size_t f(…, char* out, size_t cap)` filling a caller's fixed
 *            buffer and returning the length. Used when, and only when, the maximum is known
 *            and NAMED: `ROLLTUI_SGR_MAX`, `ROLLTUI_CHORD_STRING_MAX`,
 *            `ROLLTUI_COLOR_STRING_MAX`, `ROLLTUI_DIM_STRING_MAX`, `ROLLTUI_MARKER_MAX`,
 *            `ROLLTUI_KEY_ENCODE_MAX`, `ROLLTUI_MD_SUMMARY_MAX`. Eight functions. A caller
 *            declares `char buf[ROLLTUI_SGR_MAX]` and is done — no allocation at all.
 *        (b) **UNBOUNDED** — `void f(…, RolltuiStr* out)`, appending to a growing buffer the
 *            caller owns and reuses. Used when no maximum exists: a whole frame as text, a
 *            JSON document, a rendered diff, a breadcrumb, a hint. ~15 functions.
 *        (c) **BORROWED** — `const char* f(…, size_t* len)` handing back memory the library
 *            keeps, with the window stated on that function's own line. 59 functions. Never
 *            yours to free.
 *      **The test asserts (a): a fixed-buffer function without a named cap is a bounded
 *      promise nobody can size**, which is how this rule stays true rather than remembered.
 *   4. **Working memory is a HANDLE the caller owns**, not storage the callee invents:
 *      `RolltuiDrawScratch`, `RolltuiWrapScratch`, `RolltuiDiffScratch`. Make one per thread,
 *      reuse it, free it.
 *   5. **A callback crosses as {function pointer, void* ctx, void (*free_ctx)(void*)}.**
 *      The library calls `free_ctx` exactly once when it drops the entry. That is how a C++
 *      lambda reaches a C API — and that bridge belongs in the consumer, not here.
 *
 * ============================================================================
 * C++ consumers
 * ============================================================================
 * roll, the studio, paint and the editors are C++ and stay C++, and they call this header
 * directly.
 *   - DO write a RAII holder or a lambda bridge in your own file when you want one.
 *   - DON'T ship it from here. If two consumers write the SAME wrapper, the API is wrong,
 *     not the consumers. That has fired twice: three hosts had hand-written the same double
 *     buffer (hence `c/rolltui_swap.h`), and four had hand-copied the action table.
 */

/* ---- vocabulary: the types the rest of the API speaks ------------------------------- */
#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

/* ---- the cell grid, and turning it into bytes --------------------------------------- */
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_render.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_swap.h"

/* ---- text: Unicode, wrapping, markdown, diff ---------------------------------------- */
#include "rolltui/c/rolltui_diff.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_md_lines.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_wrap.h"
/* The "▼ N more" rule. It was in the INTERNAL three with the note "one definition, called from
 * two places inside the library" — and `transcript_test.cpp` asserts it directly, because that
 * suite IS the marker's test. PUBLIC as of Phase 17 m2c, on the same evidence as everything
 * else here: a consumer reached for it. */
#include "rolltui/c/rolltui_marker.h"

/* ---- look: theme, effects, and the analysis tools ----------------------------------- */
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"

/* ---- input: keys, chords, bindings --------------------------------------------------- */
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_keys.h"

/* ---- layout and the widgets a layout names ------------------------------------------- */
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_widgets.h"
/* The eight built-in kinds' registration, the five vocabularies they draw with, and the four
 * RULES they apply that a host applies too — scroll-by-action, the input window's two sizing
 * rules, and the help document. PUBLIC as of Phase 17 m2a, and FORCED rather than chosen: each
 * of those rules had a C++ twin that m2c deletes, and `Bindings.cpp` and every host reach for
 * the C one the moment it is the only one. m4 chose this set from what consumers reached for;
 * this is that method producing a new answer, which is what it was for. */
#include "rolltui/c/rolltui_widget_kinds.h"

/* ---- the widgets themselves ---------------------------------------------------------- */
#include "rolltui/c/rolltui_document.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_undo.h"

/* ---- files an app publishes or reads -------------------------------------------------- */
#include "rolltui/c/rolltui_app_profile.h"
/* The shipped preset/menu/layout FILES, embedded at build time. Four suites and three library
 * modules read them; it was in neither the public list nor the internal one, which is the hole
 * check 3 below now closes by enumerating the directory instead of a hand-written list. */
#include "rolltui/c/rolltui_embedded.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_presets.h"

/* ---- the process: the one fd, and the release point ----------------------------------- */
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_mem.h"
#include "rolltui/c/rolltui_terminal.h"

#endif /* ROLLTUI_H */
