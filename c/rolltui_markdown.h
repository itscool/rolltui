#ifndef ROLLTUI_C_MARKDOWN_H
#define ROLLTUI_C_MARKDOWN_H
/*
 * rolltui/c/rolltui_markdown.h — THE MARKDOWN CONTRACT (Phase 15 m4).
 *
 * CommonMark + GFM parsed by vendored md4c into a block tree, then rendered into styled,
 * wrapped lines in the caller's span store (`rolltui_md_lines.h`). Every rule about what
 * gets drawn — what counts as logical text, what a fold hides, what a highlighter may and
 * may not do — is stated in `rolltui/Markdown.hpp` and asserted in
 * `rolltui/tests/markdown_test.cpp`; none of it is repeated here.
 *
 * ---- THE ONE PLACE IN THE PORT WHERE C WAS PREDICTED TO WIN -------------------------
 *
 * **md4c is already C.** The C++ side of this module was a wrapper around a C parser: every
 * callback took `void* ud`, cast it, and moved bytes out of `MD_ATTRIBUTE` into
 * `std::string`s. The C implementation is not wrapping anything — it is the same callbacks
 * writing into its own arrays. `plan/phase-15.md` m4 records what the ratio came out at.
 *
 * ---- THE BOUNDARY'S RULES, all inherited from Phase 14 and none new -------------------
 *
 *   1. **THE CALLER OWNS EVERY BUFFER.** The parsed document is a handle the caller makes,
 *      reuses and frees; so is the store a render fills. Neither is returned by value and
 *      neither is rebuilt per frame.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function, except plain scalars.
 *   3. **TEXT OUT IS A BORROW** with a stated window — here, until the handle it came from
 *      is parsed into again or freed.
 *   4. **NO `std::function` CROSSES.** The syntax-highlighting seam is a function pointer
 *      plus a `void*`, and it EMITS THROUGH A SINK the renderer supplies — the shape
 *      `rolltui_diff.h` already uses to read a block's lines. The first cut handed over a
 *      fixed buffer and a "how many did you want" return, which is the other standard
 *      answer and is wrong HERE for a reason worth keeping: the renderer would have to ask
 *      a highlighter TWICE whenever one line wanted more spans than the last, and
 *      `markdown_test.cpp` asserts the callback is invoked EXACTLY ONCE per code line —
 *      a seam property, not an implementation detail. A sink has no cap to run out of.
 *
 * ---- WHAT THIS BOUNDARY DELIBERATELY DOES NOT KNOW ------------------------------------
 *
 * **The block tree.** `RolltuiMdDoc` is OPAQUE, and that is the port's one real
 * simplification rather than a cost: `Block`, `Run`, `Align` and the inline-style bits were
 * in the public header for four phases and NOTHING outside the renderer ever read one. Each
 * implementation now shapes its tree the way its language wants — nested `std::vector`s on
 * one side, index arrays with a first-child/next-sibling list on the other — and the only
 * thing a caller can ask is what the block KINDS are, which is what the one test that cared
 * was really asserting.
 *
 * **A `Role`.** This file names no role and never will, the rule m2 fixed at
 * `rolltui_diff.h`: the styling vocabulary is `rolltui/Style.hpp`'s, written down ONCE, and
 * a renderer is HANDED the bytes it should tag its output with. Sixteen of them is more
 * than a diff's six, and that is the honest price of keeping one vocabulary rather than
 * two — a table in `RolltuiMdRenderOptions` and no enum here.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_md_lines.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- block kinds, as bytes ------------------------------------------------------------- */
/* The same order as `rolltui::markdown::BlockKind`, asserted on the C++ side. */
#define ROLLTUI_MD_BLOCK_PARAGRAPH 0
#define ROLLTUI_MD_BLOCK_HEADING 1
#define ROLLTUI_MD_BLOCK_CODE 2
#define ROLLTUI_MD_BLOCK_HTML 3
#define ROLLTUI_MD_BLOCK_QUOTE 4
#define ROLLTUI_MD_BLOCK_LIST 5
#define ROLLTUI_MD_BLOCK_ITEM 6
#define ROLLTUI_MD_BLOCK_TABLE 7
#define ROLLTUI_MD_BLOCK_RULE 8

/* ---- the parsed document ---------------------------------------------------------------- */

typedef struct RolltuiMdDoc RolltuiMdDoc;

RolltuiMdDoc* rolltui_md_doc_new(void);
void rolltui_md_doc_free(RolltuiMdDoc* d); /* a no-op on NULL */
/* Parses INTO `d`, dropping whatever it held and keeping every buffer — the reuse that
 * makes a re-parse of a streaming entry cost nothing after the first. */
void rolltui_md_parse(RolltuiMdDoc* d, const char* src, size_t n);

/* The TOP-LEVEL blocks, which is all anything outside the renderer has ever asked about. */
size_t rolltui_md_doc_block_count(const RolltuiMdDoc* d);
unsigned char rolltui_md_doc_block_kind(const RolltuiMdDoc* d, size_t i);
/* A Code or Html block's verbatim text; empty for every other kind. A BORROW. */
const char* rolltui_md_doc_block_code(const RolltuiMdDoc* d, size_t i, size_t* n);

/* The md4c flag set `rolltui_md_parse` uses, exposed so a test can collect md4c's own text
 * callbacks under identical parsing and compare them with what was rendered. */
unsigned rolltui_md_parser_flags(void);

/* Decodes "&amp;", "&#65;", "&#x42;" and the handful of named entities model output actually
 * uses, into `out`. Returns the bytes written.
 *
 * **`cap` MUST BE AT LEAST max(n, 4)**, and that is the whole size rule: an entity this does
 * not recognise is copied through VERBATIM rather than dropped, so the worst case is the
 * input's own length, and a decoded scalar is at most four UTF-8 bytes. Sizing it from the
 * input instead of from a constant is what keeps a pathological entity — six hundred leading
 * zeros in a numeric reference — decoding the same on both sides of the flag rather than
 * falling off a cap that only one of them happens to have. */
size_t rolltui_md_decode_entity(const char* ent, size_t n, char* out, size_t cap);

/* ---- rendering --------------------------------------------------------------------------- */

/* The styling vocabulary this renderer tags its output with, handed in rather than named
 * here (see above). Every field is a `rolltui::Role` byte. */
typedef struct RolltuiMdRoles {
  unsigned char text_muted;
  unsigned char heading, emphasis, strong, strikethrough;
  unsigned char code_inline, code_block, code_label;
  unsigned char link, link_url;
  unsigned char quote, list_marker;
  unsigned char table_border, table_header;
  unsigned char rule, scroll_marker;
} RolltuiMdRoles;

/* The sentinel a table cell's "no override" uses. Not a Role and never emitted: 255 cannot
 * collide with an enum that has fifty-odd values, and saying so here is cheaper than
 * teaching this file what `Role::count_` happens to be today. */
#define ROLLTUI_MD_NO_ROLE 0xFFu

/* A host's per-block fold/cap override. */
typedef struct RolltuiMdFoldState {
  size_t index;
  unsigned char folded;   /* overrides fold_over_lines, in both directions */
  unsigned char uncapped; /* this block ignores cap_lines */
} RolltuiMdFoldState;

/* One byte range of a code line that should take a different role. */
typedef struct RolltuiMdHighlightSpan {
  size_t begin, end; /* byte offsets into the LINE the callback was given; end exclusive */
  unsigned char role;
} RolltuiMdHighlightSpan;

/* One verbatim line of a code block, as a borrow. */
typedef struct RolltuiMdCodeLine {
  const char* p;
  size_t n;
} RolltuiMdCodeLine;

/* Where a highlighter puts one span. Supplied by the renderer; valid for the call only. */
typedef void (*RolltuiMdSpanSink)(void* sink, size_t begin, size_t end, unsigned char role);

/* THE SYNTAX-HIGHLIGHTING SEAM (plan/phase-12.md m2), as a function pointer.
 *
 * Emits the spans of `lines[index]` through `sink`, in any order and any number. It emits
 * DATA, never a painter — the renderer alone decides how those bytes wrap and land in the
 * cell grid, and a span that overlaps a prior one, runs backwards or exceeds the line is
 * clamped and NAMED in the store's report rather than corrupting a frame. Called ONCE per
 * code line of every Code block, and never for an HTML block. */
typedef void (*RolltuiMdHighlightFn)(void* ctx, const char* lang, size_t lang_n,
                                     const RolltuiMdCodeLine* lines, size_t line_count, size_t index,
                                     RolltuiMdSpanSink emit, void* sink);

typedef struct RolltuiMdRenderOptions {
  int width ROLLTUI_DEFAULT(80);
  int ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(8);
  unsigned char base ROLLTUI_DEFAULT(0); /* the base Role (rolltui/Style.hpp) */
  RolltuiMdRoles roles;                  /* the rest of the vocabulary */

  /* Unset (NULL) is the seam's whole opt-in: an unregistered highlighter is never called,
   * and every code line renders through the exact pre-seam path. */
  RolltuiMdHighlightFn highlight ROLLTUI_DEFAULT(NULL);
  void* highlight_ctx ROLLTUI_DEFAULT(NULL);

  int fold_over_lines ROLLTUI_DEFAULT(0); /* 0: no block ever arrives folded */
  int cap_lines ROLLTUI_DEFAULT(0);       /* 0: an unfolded block is never capped */
  const RolltuiMdFoldState* states ROLLTUI_DEFAULT(NULL); /* BORROWED for the call */
  size_t state_count ROLLTUI_DEFAULT(0);
} RolltuiMdRenderOptions;

/* Renders `doc` into `out`, which is RESET first: its lines, its logical text, its code
 * blocks and its report are all replaced, and every buffer it holds is reused. */
void rolltui_md_render(RolltuiMdLines* out, const RolltuiMdDoc* doc, const RolltuiMdRenderOptions* opt);

/* "diff · 42 lines · 1.2 kB" — a foldable block's header text, without its marker. A bare
 * fence has no language to name, so it is called "code". Fills `out` and returns the bytes
 * written; ROLLTUI_MD_SUMMARY_MAX is enough for any of it. */
#define ROLLTUI_MD_SUMMARY_MAX 128
size_t rolltui_md_code_block_summary(const char* lang, size_t lang_n, size_t lines, size_t bytes, char* out,
                                     size_t cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MARKDOWN_H */
