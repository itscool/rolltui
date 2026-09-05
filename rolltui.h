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
 * **The four kept out are the ones neither measurement reaches**, and each is machinery a
 * consumer never names:
 *   - `c/rolltui_alloc.h`  — the CLOSED SET of allocation strategies (`rolltui_grow`,
 *                            `rolltui_fit`, the pack builder). Internal by construction:
 *                            `ownership_test` asserts that only the library grows a buffer.
 *   - `c/rolltui_map.h`    — the string-keyed table the library builds its registries from.
 *   - `c/rolltui_marker.h` — the "▼ N more" rule, one definition, called from two places
 *                            inside the library.
 *   - `c/rolltui_str.h`    — SEE BELOW. It is the one genuinely awkward case.
 *
 * **`rolltui_str.h` is public and it is the exception worth stating.** It is an internal
 * container by intent, and it appears in public signatures anyway, because this boundary's
 * rule is that nothing is returned by value from an `extern "C"` function — so every call
 * that produces text fills a caller's `RolltuiStr`. That makes it vocabulary whether or not
 * it was meant to be. It is included here rather than hidden, because a header a consumer
 * must include to call the API is public by definition, and pretending otherwise would be
 * the "documented one way, used another" split this library refuses everywhere else.
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
 *   2. **Nothing is returned BY VALUE from an `extern "C"` function.** Results fill a
 *      caller's struct or a caller's `RolltuiStr`. Clang will not promise an ABI for a
 *      non-POD return, and a caller's buffer was the better answer anyway — it is reused
 *      across frames instead of rebuilt.
 *   3. **Text out is a BORROW with a stated window.** Every function returning
 *      `const char*` says on its own line how long the pointer stays good. It is never
 *      yours to free.
 *   4. **Working memory is a HANDLE the caller owns**, not storage the callee invents:
 *      `RolltuiDrawScratch`, `RolltuiWrapScratch`, `RolltuiDiffScratch`. Make one per thread,
 *      reuse it, free it.
 *   5. **A callback crosses as {function pointer, void* ctx, void (*free_ctx)(void*)}.**
 *      The library calls `free_ctx` exactly once when it drops the entry. That is how a C++
 *      lambda reaches a C API — and that bridge belongs in the consumer, not here.
 *
 * ============================================================================
 * THE C++ CONSUMER'S QUESTION, answered once
 * ============================================================================
 * roll, the studio, paint and the editors are C++ and stay C++. They call this header
 * directly. If a consumer wants RAII or a lambda, it writes that wrapper in its own file —
 * **and if two consumers write the SAME wrapper, that is evidence this API is wrong, not
 * evidence for shipping the wrapper.** That rule already paid once: three hosts had written
 * the same double buffer by hand, which is why `c/rolltui_swap.h` exists.
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

/* ---- the widgets themselves ---------------------------------------------------------- */
#include "rolltui/c/rolltui_document.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_transcript.h"

/* ---- files an app publishes or reads -------------------------------------------------- */
#include "rolltui/c/rolltui_app_profile.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_presets.h"

/* ---- the process: the one fd, and the release point ----------------------------------- */
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_mem.h"
#include "rolltui/c/rolltui_terminal.h"

#endif /* ROLLTUI_H */
