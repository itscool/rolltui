#ifndef ROLLTUI_C_UNICODE_H
#define ROLLTUI_C_UNICODE_H
/*
 * rolltui/c/rolltui_unicode.h — THE UNICODE SEAM (Phase 14 m3).
 *
 * The four things the wrap engine asks Unicode for, and nothing else. It exists because m3
 * ports `Wrap` and m4 ports `Unicode`, so for one milestone the C wrap engine has to call a
 * C++ implementation — which is the ordinary shape of an incremental port and is worth
 * meeting early rather than avoiding by reordering the work (plan/phase-14.md).
 *
 * **THIS HEADER IS THE CONTRACT; `UnicodeSeam.cpp` IS TODAY'S IMPLEMENTATION OF IT.** m4
 * replaces that file's body with C and changes nothing here. Until then the seam is
 * compiled into BOTH configurations, so the C++ build links the same symbols and a mistake
 * in the seam is not a mistake that only one configuration can see.
 *
 * TWO RULES, both inherited:
 *   - **THE CALLER OWNS EVERY BUFFER** (m1), with the required size stated at each one. No
 *     function here allocates, and none can fail.
 *   - **NOTHING IS CAST.** A code point is `RolltuiCodepoint` (rolltui_abi.h) — `char32_t`
 *     to C++, `unsigned int` to C, asserted the same width. The alternative, a
 *     `reinterpret_cast<const char32_t*>` at the seam, is a strict-aliasing violation that
 *     compiles and works right up until it does not.
 *
 * WHAT THE PORT ALREADY NARROWED, recorded because it is evidence and not decoration: the
 * C++ `DecodedChar` carries a fourth field, `valid`, and the wrap engine has never read it.
 * The seam decodes into three parallel arrays instead of one array of structs, which also
 * deletes the `cps.assign(...)` copy loop `wrap_into` needed to get a contiguous code-point
 * array out of an array of structs.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A UAX #14 opportunity at the position BEFORE a code point. These are
 * `rolltui::unicode::Break`'s three values; UnicodeSeam.cpp asserts they still are. */
#define ROLLTUI_BREAK_PROHIBITED 0
#define ROLLTUI_BREAK_ALLOWED 1
#define ROLLTUI_BREAK_MANDATORY 2

/* Decodes `len` bytes into three parallel caller arrays, each of which must hold at least
 * `len` entries — decoding is total and every malformed byte becomes one U+FFFD scalar of
 * length 1, so the count can never exceed the byte count. Returns the number of scalars. */
size_t rolltui_u_decode_utf8(const char* s, size_t len, RolltuiCodepoint* cp, size_t* offset, size_t* length);

/* UAX #14. `out` holds n + 1 entries: out[i] is the opportunity before cps[i] and out[n]
 * is end of text (always Mandatory). */
void rolltui_u_line_break_opportunities(const RolltuiCodepoint* cps, size_t n, unsigned char* out);

/* UAX #29 extended grapheme clusters. `out` holds n + 1 entries: out[i] is 1 when a cluster
 * boundary lies before cps[i], and out[n] is the end of text. */
void rolltui_u_grapheme_boundaries(const RolltuiCodepoint* cps, size_t n, unsigned char* out);

/* Cells one extended grapheme cluster occupies. */
int rolltui_u_cluster_width(const RolltuiCodepoint* cps, size_t n, int ambiguous_wide);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_UNICODE_H */
