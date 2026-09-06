#ifndef ROLLTUI_C_MD_LINES_H
#define ROLLTUI_C_MD_LINES_H
/*
 * rolltui/c/rolltui_md_lines.h — THE SPAN STORE (Phase 15 m4).
 *
 * Styled, wrapped lines and the bytes they are made of, in ONE caller-owned handle that is
 * RESET and refilled rather than rebuilt. Everything the markdown renderer and the
 * transcript's layout produce lives in here; nothing they produce owns a byte of its own.
 *
 * ---- WHY THIS EXISTS, in the numbers m1 measured ---------------------------------------
 *
 * A resize frame was 10,297 allocations. **4,128 of them were `push_span`** — a
 * `std::string text` and a `std::vector<std::uint32_t> sources` per span, allocated and
 * freed every frame to hold bytes that had not changed — and **2,283 more were one span
 * being COPIED into another line** (`void append(StyledLine&, Span)`, by value, and the
 * copy constructor allocating both buffers again).
 *
 * Nobody DECIDED that a span should own its bytes; `std::string` is what you type. That is
 * CLAUDE.md's Phase 14 finding word for word, and this file is the answer to it: a span is
 * a BORROW — an offset and a length into pools this store owns — so there is no per-span
 * buffer to allocate, and copying a span into another line copies a descriptor.
 *
 * ---- THE THREE THINGS THAT MAKE IT SAFE -------------------------------------------------
 *
 * 1. **A published pointer is valid until the next mutation of the store.** `finish()` is
 *    what publishes; `RolltuiMdSpan` and `RolltuiMdLine` carry real pointers and are read
 *    through `rolltui_md_lines_line()`. Append after a `finish()` and the previous
 *    pointers are dead — the same window `Line`, `Frame::glyph` and `Scratch` already
 *    state, and the reason the build API never hands one back.
 * 2. **DURING the build nothing holds a pointer.** The span records are offsets, so a pool
 *    that grows mid-render cannot leave a dangling span behind it. That is the whole
 *    reason for the two arrays (`SpanRec` while building, `RolltuiMdSpan` once finished)
 *    rather than one array of pointers fixed up on every realloc — a fix-up has to read
 *    the offsets it just threw away.
 * 3. **A span can REFERENCE another span of the SAME store** (`span_ref`), by INDEX and
 *    never by pointer, which is what lets the transcript put a rendered body line behind
 *    its prefix without copying one byte. Index rather than pointer is not fussiness: the
 *    caller appends while it reads, and an append may grow the pool under a pointer it
 *    took a moment ago.
 *
 * ---- COMPILED INTO BOTH CONFIGURATIONS, and that is a decision -------------------------
 *
 * This file is not one of the ported modules; it was C in the C++ build too, for the
 * same reason `unicode_tables.c` and `rolltui_alloc.c` are: it is the DATA both
 * implementations of the renderer fill, and two copies of a data structure is two things
 * that can disagree about what a span is. What the flag chooses is the ALGORITHM that
 * fills it — `MarkdownCpp.cpp` or `c/rolltui_markdown.c` — which is the thing the
 * experiment is measuring. A store with two implementations would measure the store.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The byte offset a drawn grapheme came from in the logical text, or this when it is
 * CHROME — a border, a rule, continuation indentation, a fold header. One value, not a
 * flag beside it, because "this cell is not text" and "this cell came from byte N" are the
 * same question asked once. */
#define ROLLTUI_MD_NO_SOURCE 0xFFFFFFFFu

/* One run of cells sharing a role and a hyperlink.
 *
 * EVERY POINTER HERE IS A BORROW into the store that produced it (see the window above).
 * The lengths are separate because none of it is NUL-terminated: the pools are packed. */
typedef struct RolltuiMdSpan {
  const char* text_p; /* the bytes to draw, in order */
  size_t text_n;
  const char* href_p; /* non-empty: the OSC 8 target for these cells */
  size_t href_n;
  /* One entry per grapheme cluster of `text_p`: the byte offset that cluster came from in
   * the logical text, or ROLLTUI_MD_NO_SOURCE. A span's graphemes need not be contiguous
   * in that text (a tab is eight spaces that all point at the tab). */
  const uint32_t* src_p;
  size_t src_n;
  int width; /* cells */
  unsigned char role;

} RolltuiMdSpan;

/* One drawn line: a contiguous run of the store's spans. */
typedef struct RolltuiMdLine {
  const RolltuiMdSpan* span_p;
  size_t span_n;
  int width;

} RolltuiMdLine;

typedef struct RolltuiMdLines RolltuiMdLines;

/* ---- lifetime -------------------------------------------------------------------------- */

RolltuiMdLines* rolltui_md_lines_new(void);
void rolltui_md_lines_free(RolltuiMdLines* L); /* a no-op on NULL */
/* Drops every line, span and byte and KEEPS every buffer. This is the reset that makes a
 * re-laid entry cost nothing: `clear()` on a container of owning elements frees exactly the
 * storage being reused (CLAUDE.md, Phase 13's mistake-made-four-times), and there is
 * nothing owning left in here to free. */
void rolltui_md_lines_reset(RolltuiMdLines* L);

/* ---- building --------------------------------------------------------------------------
 *
 * Exactly one line is open at a time; opening a second aborts, the way `Scratch` does,
 * because the alternative is a plausible wrong frame rather than a crash. */

void rolltui_md_lines_open(RolltuiMdLines* L);
/* Appends a span, COPYING `text` and `href` into the store's pools. `sources` may be NULL,
 * which means chrome: every cluster of `text` gets ROLLTUI_MD_NO_SOURCE, generated rather
 * than passed, so nobody builds an array to describe an absence. When it is not NULL,
 * `src_n` must equal the cluster count of `text` or it is treated as chrome.
 *
 * MERGES into the previous span when the role and href match AND that span is the tail of
 * both pools — which is every span this store built itself, and never a `span_ref`. */
void rolltui_md_lines_span(RolltuiMdLines* L, const char* text, size_t text_n, unsigned char role,
                           int ambiguous_wide, const uint32_t* sources, size_t src_n, const char* href,
                           size_t href_n);
/* Appends span `index` of THIS store to the open line, sharing its pool range: no byte is
 * copied and nothing is merged. This is how a line is put behind a prefix. */
void rolltui_md_lines_span_ref(RolltuiMdLines* L, size_t index);
/* Trims trailing ASCII spaces off the open line, dropping spans that become empty. Only
 * valid on spans this store built itself (a `span_ref`'s bytes belong to another line);
 * a trim that would reach one stops there. */
void rolltui_md_lines_trim_trailing_spaces(RolltuiMdLines* L, int ambiguous_wide);
/* Closes the open line and returns its index. */
size_t rolltui_md_lines_close(RolltuiMdLines* L);

/* ---- reading ---------------------------------------------------------------------------
 *
 * `finish` publishes the pointer views; nothing below may be called before it, and every
 * pointer it hands out dies at the next append. */

void rolltui_md_lines_finish(RolltuiMdLines* L);
size_t rolltui_md_lines_count(const RolltuiMdLines* L);
const RolltuiMdLine* rolltui_md_lines_line(const RolltuiMdLines* L, size_t i);
/* Every line at once, for a caller that wants a range. NULL when there are none. */
const RolltuiMdLine* rolltui_md_lines_all(const RolltuiMdLines* L);
/* The span INDICES line `i` is made of — what `span_ref` takes, and the only safe way to
 * name another line's spans while appending to the store. */
void rolltui_md_lines_span_range(const RolltuiMdLines* L, size_t i, size_t* first, size_t* count);

/* ---- the line array's own mark/rewind ---------------------------------------------------
 *
 * Drops lines from `mark` on and KEEPS their spans and bytes, so a line that was built only
 * to be read — a table cell, laid out at the column's width and then poured into the row —
 * can be dropped while the row line that references its spans stays valid. */
size_t rolltui_md_lines_mark(const RolltuiMdLines* L);
void rolltui_md_lines_rewind(RolltuiMdLines* L, size_t mark);

/* ---- the logical text ------------------------------------------------------------------
 *
 * The document's text at infinite width — what every `src_p` offset indexes, what a
 * selection copies and what find searches (rolltui/Markdown.hpp, "LOGICAL TEXT"). It lives
 * here rather than in a `std::string` beside the store for one reason: it is grown by the
 * same render that grows everything else, and a caller that reuses the store must get the
 * text buffer's reuse with it. */
void rolltui_md_lines_text_append(RolltuiMdLines* L, const char* s, size_t n);
void rolltui_md_lines_text_set(RolltuiMdLines* L, const char* s, size_t n);
size_t rolltui_md_lines_text_size(const RolltuiMdLines* L);
const char* rolltui_md_lines_text(const RolltuiMdLines* L); /* never NULL; not terminated */
/* Drops the last byte — the render's one trailing '\n'. */
void rolltui_md_lines_text_pop(RolltuiMdLines* L);

/* ---- interned chrome, and a second store for what is laid out only to be poured ---------
 *
 * TWO SMALL THINGS A RENDERER NEEDS THAT ARE NOT LINES, both here because their lifetime is
 * the store's and a second owner would be a second lifetime to reason about.
 *
 * `intern` is a POOL OF ITS OWN, deliberately not the span pool: a renderer holds a block's
 * prefix (a quote bar, a list marker, the padding under it) for as long as the block is open
 * and emits it at the head of every line, and appending those bytes into the span pool
 * between two spans would silently break the tail-merge rule above. Its own pool cannot.
 *
 * `aux` is a whole second store, made on first use, for a caller that must lay something out
 * at one width and then pour it into a line here — a table cell, which is laid out in its
 * column's width and then padded into the row. The alternative was a mark/rewind on the line
 * array, which does not work: the row lines are appended AFTER the cell lines they read. */

size_t rolltui_md_lines_intern(RolltuiMdLines* L, const char* s, size_t n);
const char* rolltui_md_lines_interned(const RolltuiMdLines* L, size_t off);
RolltuiMdLines* rolltui_md_lines_aux(RolltuiMdLines* L);

/* A SLOT THE FILLER KEEPS ITS OWN WORKING MEMORY IN, made on first use and freed with this
 * store. The store never looks inside it.
 *
 * It is here rather than in a thread-local because of what a thread-local would cost:
 * `shutdown()` has to land on `live_bytes == 0`, and there is no C spelling for registering
 * a PER-THREAD releaser (`rolltui_on_shutdown` is process-wide). A renderer's buffers hang
 * off the store the caller already owns, so they are freed when it is — and a `Rendered`
 * that is rendered into every frame reuses the renderer's scratch along with everything
 * else. `make` and `destroy` must be the same pair on every call for one store. */
void* rolltui_md_lines_work(RolltuiMdLines* L, void* (*make)(void), void (*destroy)(void*));

/* ---- what the render REPORTED, kept here because its strings are pooled too --------------
 *
 * A code block's `lang` and a clamped highlight span's message are BORROWS like every other
 * string in this store, published by the same `finish()` and dead at the same moment. They
 * live in pools of their OWN rather than in the span pool, so appending one between two
 * spans cannot break the merge rule above — a subtle coupling, and the reason two extra
 * pools are cheaper than one shared one. */

#define ROLLTUI_MD_NO_LINE ((size_t)-1)

/* What the renderer did with one NUMBERED code block — enough for a host to hit-test a
 * click and to find the block holding a byte offset, without re-parsing anything. */
typedef struct RolltuiMdCodeBlock {
  size_t index;
  const char* lang_p; /* the fence's first word; empty for a bare fence or indented block */
  size_t lang_n;
  size_t lines; /* the block's own line count */
  size_t bytes;
  unsigned char foldable; /* over the threshold, or explicitly folded: it has a header row */
  unsigned char folded;
  size_t hidden;      /* lines the cap hides; 0 when not capped */
  size_t header_line; /* index into the store's lines — the fold's click target */
  size_t marker_line; /* …and the cap's */
  size_t text_begin, text_end; /* the block's byte range in the logical text */

} RolltuiMdCodeBlock;

/* Appends one. `lang_p`/`lang_n` are copied; every other field is taken as given. */
void rolltui_md_lines_add_code_block(RolltuiMdLines* L, const RolltuiMdCodeBlock* b);
size_t rolltui_md_lines_code_block_count(const RolltuiMdLines* L);
const RolltuiMdCodeBlock* rolltui_md_lines_code_blocks(const RolltuiMdLines* L); /* after finish() */
/* Shifts every block's header_line and marker_line by `by` — what the transcript does when
 * its own summary row pushes the body down (`Transcript.cpp`), stated as one call because a
 * click routes by LINE NUMBER and an off-by-one is a header that toggles nothing. */
void rolltui_md_lines_shift_code_blocks(RolltuiMdLines* L, size_t by);
void rolltui_md_lines_clear_code_blocks(RolltuiMdLines* L);

/* One clamped or dropped highlight span, named the way LayoutLoadReport names a bad value —
 * never silent, and a fully dropped span reported exactly like a trimmed one. */
void rolltui_md_lines_add_clamped(RolltuiMdLines* L, const char* msg, size_t n);
size_t rolltui_md_lines_clamped_count(const RolltuiMdLines* L);
void rolltui_md_lines_clamped_at(const RolltuiMdLines* L, size_t i, const char** p, size_t* n);

/* ---- working memory the store lends its filler ------------------------------------------
 *
 * CLAUDE.md's third strategy covers WORKING memory, not only results: a function that needs
 * somewhere to decode into takes a handle the caller owns. Both implementations of the
 * renderer cluster text constantly, and this is the buffer they do it in — one per store,
 * so a renderer holds no per-thread state of its own. */
typedef struct RolltuiUnicodeScratch RolltuiUnicodeScratch;
RolltuiUnicodeScratch* rolltui_md_lines_scratch(RolltuiMdLines* L);
/* Clusters `text` into the store's own grapheme buffer and reports the count and total
 * width. The buffer is handed back so a caller can walk it; it is valid until the next
 * call of this function on the same store. */
typedef struct RolltuiUnicodeGrapheme RolltuiUnicodeGrapheme;
const RolltuiUnicodeGrapheme* rolltui_md_lines_clusters(RolltuiMdLines* L, const char* text, size_t n,
                                                        int ambiguous_wide, size_t* count, int* width);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MD_LINES_H */
