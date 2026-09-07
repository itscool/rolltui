#ifndef ROLLTUI_C_UNICODE_H
#define ROLLTUI_C_UNICODE_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_unicode.h — THE UNICODE ALGORITHMS, as C (Phase 14 m5).
 *
 * UTF-8 decoding, UAX #14 line breaking, UAX #29 grapheme clusters and word boundaries,
 * terminal cell widths, and the escape-sequence stripper. The rules and their justifications
 * live in `rolltui/Unicode.hpp` and are not repeated here: they are the same rules in both
 * languages, and a second copy is a second thing to drift.
 *
 * **THE ORACLE IS NOT THIS FILE, WHICH IS WHY THIS SLICE WAS WORTH PORTING LAST.** Three
 * published conformance suites run in full against whichever implementation is linked —
 * `GraphemeBreakTest`, `WordBreakTest` and `LineBreakTest` — plus a width table checked
 * against libc `wcwidth` over the whole BMP with every disagreement LISTED rather than
 * tolerated. Correctness here is decided by Unicode's own test files, not by review, which is
 * the strongest position any milestone in this phase has been in.
 *
 * THE BOUNDARY'S RULES, all inherited and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER** (m1), with the required size stated at each one. Every
 *      output length here has a bound the caller can compute WITHOUT asking first — a decode
 *      yields at most one scalar per byte, a boundary array is n + 1, a strip only ever
 *      shrinks — so no function here needs a measure-then-fill round trip, and none allocates.
 *      **That applies to WORKING memory too, through `RolltuiUnicodeScratch` below**, which is
 *      the same rule the rest of this port already follows: a `RolltuiFrame` and a
 *      `RolltuiWrapLines` are handles the caller owns that carry the reusable buffers, and
 *      these functions were the odd ones out for having nowhere to keep theirs.
 *   2. **ONE DEFINITION** (m2): `RolltuiDecodedChar` and `RolltuiUnicodeGrapheme` are the C++
 *      `unicode::DecodedChar` and `unicode::Grapheme`, aliased rather than converted.
 *   3. **A CODE POINT IS `RolltuiCodepoint`** (m3): `char32_t` to C++, `unsigned int` to C,
 *      asserted the same width, never cast at the seam.
 *
 * WHAT THE PORT DELETED, recorded because a removal is evidence too: `grapheme_boundaries_into`
 * and `line_break_opportunities_into` are gone from `Unicode.hpp`. They existed so that m3's
 * temporary seam could reach the algorithms without allocating; the caller-buffer functions
 * below ARE that, so the C++-only spelling of the same idea had no callers left.
 */

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif
/* One extended grapheme cluster of a UTF-8 string. */
typedef struct RolltuiUnicodeGrapheme {
  size_t offset; /* byte offset of the cluster in the source */
  size_t length; /* bytes */
  int width;     /* cells */
} RolltuiUnicodeGrapheme;

/* A UAX #14 opportunity at the position BEFORE a code point. `Break`'s three values, asserted
 * equal on the C++ side. */
#define ROLLTUI_BREAK_PROHIBITED 0
#define ROLLTUI_BREAK_ALLOWED 1
#define ROLLTUI_BREAK_MANDATORY 2

unsigned char rolltui_u_general_category(RolltuiCodepoint cp);

/* ---- UTF-8 ----------------------------------------------------------------------------- */
/* One scalar at `pos`, into a caller's struct. `pos` must be < `len`. */
void rolltui_u_decode_one(const char* s, size_t len, size_t pos, RolltuiDecodedChar* out);
/* Encodes one scalar into `out`, which must hold at least 4 bytes. Returns the bytes written. */
size_t rolltui_u_append_utf8(RolltuiCodepoint cp, char* out);

/* ---- widths ---------------------------------------------------------------------------- */
int rolltui_u_codepoint_width(RolltuiCodepoint cp, int ambiguous_wide);
int rolltui_u_cluster_width(const RolltuiCodepoint* cps, size_t n, int ambiguous_wide);

/* ---- UAX #29 --------------------------------------------------------------------------- */
/* `out` holds n + 1 entries: out[i] is 1 when a boundary lies before cps[i], out[n] is the end
 * of text. For a non-empty input out[0] and out[n] are 1; for empty input the single entry
 * is 1. */
void rolltui_u_grapheme_boundaries(RolltuiUnicodeScratch* s, const RolltuiCodepoint* cps, size_t n,
                                   unsigned char* out);
void rolltui_u_word_boundaries(RolltuiUnicodeScratch* s, const RolltuiCodepoint* cps, size_t n,
                               unsigned char* out);
/* Clusters of a UTF-8 string with their byte spans and cell widths. `out` must hold at least
 * `len` entries — there can be no more clusters than bytes. Returns the count. */
size_t rolltui_u_graphemes(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int ambiguous_wide,
                           RolltuiUnicodeGrapheme* out);
/* The word containing byte `offset`, as a byte range; an offset past the end gives
 * {len, len}. */
void rolltui_u_word_range(RolltuiUnicodeScratch* s, const char* utf8, size_t len, size_t offset,
                          size_t* begin, size_t* end);

/* ---- UAX #14 --------------------------------------------------------------------------- */
/* `out` holds n + 1 entries of ROLLTUI_BREAK_*: out[i] is the opportunity before cps[i] and
 * out[n] is end of text, always Mandatory (LB3); out[0] is always Prohibited (LB2). */
void rolltui_u_line_break_opportunities(RolltuiUnicodeScratch* s, const RolltuiCodepoint* cps, size_t n,
                                        unsigned char* out);

/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
/* Decodes into three parallel caller arrays, each of which must hold at least `len` entries —
 * decoding is total and a malformed byte is one scalar of length 1, so the count can never
 * exceed the byte count. Returns the number of scalars. Kept as parallel arrays rather than an
 * array of `RolltuiDecodedChar` because the wrap engine wants a contiguous code-point array
 * and building one out of an array of structs was a copy loop it no longer has (m3). */
size_t rolltui_u_decode_utf8(const char* s, size_t len, RolltuiCodepoint* cp, size_t* offset, size_t* length);
/* The same, into an array of structs, which is what a caller wanting `valid` needs. `out` must
 * hold at least `len` entries. Returns the number of scalars. */
size_t rolltui_u_decode_utf8_chars(const char* s, size_t len, RolltuiDecodedChar* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_UNICODE_H */
