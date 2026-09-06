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
 * HOW THE PUBLIC SET IS CHOSEN — DECIDED, and this replaces a DERIVATION
 * ============================================================================
 * This paragraph used to say "derived, not asserted", and describe two measurements taken on
 * 2026-09-04: what consumers reached for, and which headers were leaves of the include graph.
 * THAT METHOD IS RETRACTED (Phase 17 m4b, 2026-09-05). Two things were wrong with it:
 *
 *   1. **Measuring reach cannot tell an entry point from plumbing**, because a consumer
 *      reaching THROUGH a bad API looks identical to one reaching FOR a good one. The old
 *      text says so without noticing: `md_lines.h` and `layout_tree.h` "read like internal
 *      data structures" and were made public anyway, on the strength of four suites naming
 *      them. The result was 37 of 39 headers public — not a curated surface, the whole
 *      library with an include list on top.
 *   2. **Its instrument went blind.** Once consumers include only this file, "who names this
 *      header" stops reflecting who USES it, so the measurement is not even repeatable.
 *
 * WHAT THE HONEST MEASUREMENT SAYS, taken at symbol level instead (2026-09-05): the library
 * declares **1,295 symbols across 40 headers. 439 are reached by a host. 296 only by a test.
 * 560 have no caller anywhere in this repository** — 43% of the surface. A number like that is
 * not an argument for a different include list; it is the evidence that "which headers are
 * public" was never the interesting question.
 *
 * SO THE RULE IS A DECISION, AND IT IS THIS: a header is public because a STATED REASON says
 * it is an entry point or the vocabulary an entry point's signatures speak. Nothing is public
 * because someone reached for it. **A gap is evidence; a usage is not** — when a consumer
 * cannot do its job, that argues for opening something, and each such argument is made and
 * recorded one at a time.
 *
 * **TWO are internal**, and `public_header_test`'s `kInternal` is the enforced list:
 *   - `c/rolltui_alloc.h`  — the CLOSED SET of allocation strategies. Internal by
 *                            construction: `ownership_test` asserts only the library grows a
 *                            buffer.
 *   - `c/rolltui_map.h`    — the string-keyed table the library builds its registries from.
 *
 * `rolltui_str.h` is IN, and it is the one genuinely awkward case. It was written as an
 * internal container and it appears in public signatures anyway — the ~15 functions of rule
 * 3(b), and now `RolltuiStrList` under rule 4. That makes it vocabulary whether or not it was
 * meant to be, and a header a consumer must include to call the API is public by definition.
 *
 * ============================================================================
 * WHAT A CONSUMER SHOULD NEVER HAVE TO WRITE, and how that is checked
 * ============================================================================
 * The user's framing, 2026-09-05: *"if I'm using MFC I don't expect to build a bunch of
 * scaffolding around it for my C++ projects"* — and the question that follows it, *"what is
 * there we even need to provide?"* The answer turned out to be **nothing new**: the wrappers
 * existed because the API had two shapes for one job, and one of them forces a wrapper.
 *
 * It was NOT a string problem, which was the first hypothesis and is worth recording as
 * disconfirmed: `RolltuiStr` has had `operator std::string_view()` all along, so a C++ host
 * pays nothing to read one. The wrappers were about RESULTS ARRIVING THROUGH CALLBACKS.
 *
 * **THE RULE, and it is checkable rather than a matter of taste: does a callback carry a
 * DECISION going IN, or a RESULT coming OUT?**
 *   - **A DECISION going in is what a callback is FOR** and stays one. `RolltuiScopeFn` (is
 *     this a library scope?), `RolltuiRowsFn`, `RolltuiEffectFn`, `RolltuiValidatorFn`,
 *     `RolltuiSlotFn`, `RolltuiEventFn`. A host WANTS to write that lambda; it is the payload.
 *   - **A RESULT coming out goes into a buffer the CALLER owns and reuses**, replaced on every
 *     call — `RolltuiStr*` for one string, `RolltuiStrList*` for many, a typed list
 *     (`RolltuiPresetList`, `RolltuiMenuActionList`) for many of something structured.
 *   - The tell that this was wrong was measured, not felt: `rolltui_preset_store_list` was
 *     wrapped at **7 of 7** call sites while count/at-shaped APIs were wrapped at **0 of 32**,
 *     and `struct PresetInfo` had been written out **three times, byte for byte**, in roll, the
 *     studio and `presets_test.cpp`. **A sink's parameter list IS a struct definition the
 *     library declined to write down, so every consumer wrote it instead.**
 *   - `rolltui_preset_shipped_text` was added for the same reason one level down: a lookup the
 *     API could do and did not offer is a lookup every consumer hand-writes, and two had.
 *
 * **ENFORCED, not remembered:** `public_header_test` asserts that no public function hands a
 * result back through a callback, proves its own scanner armed on every run, and fails on a
 * planted violation by name. Retiring the sink shape deleted far more consumer code than it
 * added library code — the numbers are in `plan/phase-17.md` m4b.
 *
 * ============================================================================
 * THE RULES EVERY HEADER BELOW OBEYS
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
 *   4b. **MANY THINGS OUT HAS ONE SHAPE, the same one rule 3(b) has for text:** a list the
 *      CALLER owns and reuses, REPLACED on every call — `RolltuiStrList` for strings,
 *      `RolltuiPresetList` / `RolltuiMenuActionList` for structured rows. Never a sink
 *      callback; see the section above for why, and `public_header_test` for the check.
 *   5. **A callback crosses as {function pointer, void* ctx, void (*free_ctx)(void*)}, and
 *      only ever carries a DECISION INTO the library.** The library calls `free_ctx` exactly
 *      once when it drops the entry. That is how a C++ lambda reaches a C API — and that
 *      bridge belongs in the consumer, not here.
 *
 * ============================================================================
 * C++ consumers
 * ============================================================================
 * roll, the studio, paint and the editors are C++ and stay C++, and they call this header
 * directly.
 *   - DO write a RAII holder or a lambda bridge in your own file when you want one.
 *   - DON'T ship it from here. If two consumers write the SAME wrapper, the API is wrong,
 *     not the consumers. That has now fired FIVE times: three hosts had hand-written the same
 *     double buffer (hence `c/rolltui_swap.h`), four had hand-copied the action table,
 *     three had written `struct PresetInfo` (hence `RolltuiPresetInfo`, and the sink rule
 *     above that made it necessary), two had written "open the popup this layout declared"
 *     at six call sites (hence `rolltui_window_stack_push_popup`), and five had assembled the
 *     same three preset domains while four freed a store's value through the domain and three
 *     folded the same save-as sentence (hence `rolltui_preset_domain`,
 *     `rolltui_preset_store_value_free` and `err` carrying the sentence — Phase 18 m3, found
 *     by the pure-C consumer opening a store). Each time the fix was the API, never the
 *     wrapper.
 *   - AND THE FOURTH ONE CARRIES A WARNING THE FIRST THREE DID NOT. Its two spellings were
 *     `RolltuiLayer copy = *p;` and `RolltuiLayer copy{}; rolltui_layer_copy(&copy, p);`, and
 *     **those are the same operation only in C++.** The first relies on a copy constructor
 *     these headers declare under `__cplusplus`; compiled as C it is a shallow struct
 *     assignment and the push double-frees (measured under ASan — the declaration of
 *     `rolltui_window_stack_push_popup` has the trace). **So a C++ consumer's `=` on any
 *     owning struct here is doing work a C consumer cannot ask for**, and where that work is
 *     the missing half of an operation, the special members are ABSORBING an API gap rather
 *     than serving a host. `rolltui/tests/c_consumer_test.c` is what stands in front of that
 *     wall now; it found this one on its first run.
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
