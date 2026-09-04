/* rolltui/c/rolltui_unicode.c — the C side of the Unicode algorithms. See
 * rolltui/c/rolltui_unicode.h for the boundary's rules and rolltui/Unicode.hpp for the
 * contracts; `UnicodeCpp.cpp` is the other implementation of the same 21 functions.
 *
 * **THE ORACLE IS UNICODE'S, NOT OURS.** GraphemeBreakTest, WordBreakTest and LineBreakTest
 * run in full against whichever implementation is linked — 19,339 line-break cases alone —
 * plus a width table cross-checked against libc `wcwidth` over the whole BMP. Every rule
 * below carries the rule number it implements, transcribed from the C++ unchanged, because
 * the suites can only say a translation is faithful if the translation did not also
 * reorganise the rules.
 *
 * WORKING MEMORY IS THE CALLER'S, through `RolltuiUnicodeScratch` — the same handle the rest
 * of this port already uses for a Frame and for wrapped lines. It holds one GROWING BUFFER per
 * role; a host makes one per thread and reuses it forever, so after the first few calls
 * nothing here allocates at all.
 *
 * **THE FIRST VERSION OF THIS FILE DID SOMETHING MORE COMPLICATED AND WORSE**, and it is worth
 * saying why so nobody reinvents it: a 16 KB stack buffer per call, carved into the arrays that
 * call needed, spilling to the heap past ~700 code points. It worked, and it was three
 * mechanisms (stack, carve, spill) where one sufficed — chosen because these functions were the
 * only ones on the boundary with nowhere to keep a buffer, so adding a parameter felt like
 * noise. It was not noise; it was the missing handle. The pattern was already written down as
 * CLAUDE.md's third strategy, CALLER-FILLED, and this port had used it twice already.
 */
#include "rolltui/c/rolltui_unicode.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/unicode_tables.h"

/* ---- the caller's working memory --------------------------------------------------------- */

typedef struct Unit Unit; /* the line-break unit, defined with the UAX #14 rules below */

/* ONE BUFFER PER ROLE, not one pool: `graphemes` calls `grapheme_boundaries` and
 * `display_width` calls `graphemes`, so a shared region would have a function aliasing its own
 * caller's scratch. Separate roles make that impossible rather than merely forbidden. Each
 * grows through the closed set's amortised strategy (rolltui_alloc.h) and never shrinks, so a
 * handle that has drawn a few frames never allocates again. */
struct RolltuiUnicodeScratch {
  RolltuiCodepoint* cps;
  size_t cps_cap;
  size_t* offs;
  size_t offs_cap;
  size_t* lens;
  size_t lens_cap;
  unsigned char* bounds;
  size_t bounds_cap;
  unsigned char *gb, *incb, *pict; /* grapheme_boundaries */
  size_t gb_cap, incb_cap, pict_cap;
  unsigned char *wb, *wpict; /* word_boundaries */
  size_t wb_cap, wpict_cap;
  Unit* units; /* line_break_opportunities */
  size_t units_cap;
  RolltuiUnicodeGrapheme* gr; /* display_width */
  size_t gr_cap;
};

RolltuiUnicodeScratch* rolltui_u_scratch_new(void) {
  RolltuiUnicodeScratch* s = (RolltuiUnicodeScratch*)rolltui_mem_alloc(sizeof(RolltuiUnicodeScratch));
  memset(s, 0, sizeof *s);
  return s;
}

void rolltui_u_scratch_free(RolltuiUnicodeScratch* s) {
  if (!s) return;
  rolltui_mem_free(s->cps);
  rolltui_mem_free(s->offs);
  rolltui_mem_free(s->lens);
  rolltui_mem_free(s->bounds);
  rolltui_mem_free(s->gb);
  rolltui_mem_free(s->incb);
  rolltui_mem_free(s->pict);
  rolltui_mem_free(s->wb);
  rolltui_mem_free(s->wpict);
  rolltui_mem_free(s->units);
  rolltui_mem_free(s->gr);
  rolltui_mem_free(s);
}

/* ---- property lookups ------------------------------------------------------------------ */

/* Binary search over a generated table: the ranges are sorted and disjoint, so the first
 * range whose `last` is >= cp is the only candidate. */
static unsigned char lookup(const RolltuiUnicodeRange* table, size_t n, RolltuiCodepoint cp,
                            unsigned char def) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (table[mid].last < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < n && table[lo].first <= cp && cp <= table[lo].last) return table[lo].value;
  return def;
}

static unsigned char lb_class(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_line_break, ROLLTUI_U_TABLE_LINE_BREAK_COUNT, cp, ROLLTUI_LINEBREAK_DEFAULT);
}
static unsigned char ea_width(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_east_asian_width, ROLLTUI_U_TABLE_EAST_ASIAN_WIDTH_COUNT, cp,
                ROLLTUI_EASTASIANWIDTH_DEFAULT);
}
static unsigned char gb_class(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_grapheme_break, ROLLTUI_U_TABLE_GRAPHEME_BREAK_COUNT, cp,
                ROLLTUI_GRAPHEMEBREAK_DEFAULT);
}
static unsigned char wb_class(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_word_break, ROLLTUI_U_TABLE_WORD_BREAK_COUNT, cp, ROLLTUI_WORDBREAK_DEFAULT);
}
static unsigned char incb_class(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_indic_conjunct_break, ROLLTUI_U_TABLE_INDIC_CONJUNCT_BREAK_COUNT, cp,
                ROLLTUI_INDICCONJUNCTBREAK_DEFAULT);
}
static unsigned char gc_class(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_general_category, ROLLTUI_U_TABLE_GENERAL_CATEGORY_COUNT, cp,
                ROLLTUI_GENERALCATEGORY_DEFAULT);
}
static int is_pict(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_extended_pictographic, ROLLTUI_U_TABLE_EXTENDED_PICTOGRAPHIC_COUNT, cp, 0) != 0;
}
static int is_ignorable(RolltuiCodepoint cp) {
  return lookup(rolltui_u_table_default_ignorable, ROLLTUI_U_TABLE_DEFAULT_IGNORABLE_COUNT, cp, 0) != 0;
}

unsigned char rolltui_u_line_break_class(RolltuiCodepoint cp) { return lb_class(cp); }
unsigned char rolltui_u_east_asian_width(RolltuiCodepoint cp) { return ea_width(cp); }
unsigned char rolltui_u_grapheme_break(RolltuiCodepoint cp) { return gb_class(cp); }
unsigned char rolltui_u_word_break(RolltuiCodepoint cp) { return wb_class(cp); }
unsigned char rolltui_u_indic_conjunct_break(RolltuiCodepoint cp) { return incb_class(cp); }
unsigned char rolltui_u_general_category(RolltuiCodepoint cp) { return gc_class(cp); }
int rolltui_u_is_extended_pictographic(RolltuiCodepoint cp) { return is_pict(cp); }
int rolltui_u_is_default_ignorable(RolltuiCodepoint cp) { return is_ignorable(cp); }

/* ---- UTF-8 ------------------------------------------------------------------------------ */

static void decode_at(const char* s, size_t len, size_t pos, RolltuiDecodedChar* d) {
  unsigned char b0 = (unsigned char)s[pos];
  size_t need, i;
  RolltuiCodepoint cp, min;
  d->offset = pos;
  if (b0 < 0x80) {
    d->cp = b0;
    d->length = 1;
    d->valid = 1;
    return;
  }
  if ((b0 & 0xE0) == 0xC0) { need = 1; cp = b0 & 0x1Fu; min = 0x80; }
  else if ((b0 & 0xF0) == 0xE0) { need = 2; cp = b0 & 0x0Fu; min = 0x800; }
  else if ((b0 & 0xF8) == 0xF0) { need = 3; cp = b0 & 0x07u; min = 0x10000; }
  else {
    d->cp = 0xFFFD;
    d->length = 1;
    d->valid = 0;
    return;
  }
  if (pos + need >= len) { /* truncated sequence */
    d->cp = 0xFFFD;
    d->length = 1;
    d->valid = 0;
    return;
  }
  for (i = 1; i <= need; ++i) {
    unsigned char b = (unsigned char)s[pos + i];
    if ((b & 0xC0) != 0x80) {
      d->cp = 0xFFFD;
      d->length = 1;
      d->valid = 0;
      return;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    d->cp = 0xFFFD;
    d->length = 1;
    d->valid = 0;
    return;
  }
  d->cp = cp;
  d->length = need + 1;
  d->valid = 1;
}

void rolltui_u_decode_one(const char* s, size_t len, size_t pos, RolltuiDecodedChar* out) {
  decode_at(s, len, pos, out);
}

size_t rolltui_u_decode_utf8(const char* s, size_t len, RolltuiCodepoint* cp, size_t* offset, size_t* length) {
  size_t n = 0, pos = 0;
  while (pos < len) {
    RolltuiDecodedChar d;
    decode_at(s, len, pos, &d);
    cp[n] = d.cp;
    offset[n] = d.offset;
    length[n] = d.length;
    ++n;
    pos += d.length;
  }
  return n;
}

size_t rolltui_u_decode_utf8_chars(const char* s, size_t len, RolltuiDecodedChar* out) {
  size_t n = 0, pos = 0;
  while (pos < len) {
    decode_at(s, len, pos, &out[n]);
    pos += out[n].length;
    ++n;
  }
  return n;
}

size_t rolltui_u_append_utf8(RolltuiCodepoint cp, char* out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

/* ---- width ------------------------------------------------------------------------------ */

int rolltui_u_codepoint_width(RolltuiCodepoint cp, int ambiguous_wide) {
  unsigned char gc = gc_class(cp), gb, ea;
  if (gc == ROLLTUI_GENERALCATEGORY_Cc || gc == ROLLTUI_GENERALCATEGORY_Cs) return 0;
  if (is_ignorable(cp)) return 0;
  if (gc == ROLLTUI_GENERALCATEGORY_Mn || gc == ROLLTUI_GENERALCATEGORY_Mc ||
      gc == ROLLTUI_GENERALCATEGORY_Me || gc == ROLLTUI_GENERALCATEGORY_Cf)
    return 0;
  gb = gb_class(cp);
  if (gb == ROLLTUI_GRAPHEMEBREAK_V || gb == ROLLTUI_GRAPHEMEBREAK_T) return 0;
  if (gc == ROLLTUI_GENERALCATEGORY_Zl || gc == ROLLTUI_GENERALCATEGORY_Zp) return 0;
  ea = ea_width(cp);
  if (ea == ROLLTUI_EASTASIANWIDTH_W || ea == ROLLTUI_EASTASIANWIDTH_F) return 2;
  if (ea == ROLLTUI_EASTASIANWIDTH_A && ambiguous_wide) return 2;
  return 1;
}

int rolltui_u_cluster_width(const RolltuiCodepoint* cps, size_t n, int ambiguous_wide) {
  int ri = 0, sum = 0;
  int vs16 = 0, keycap = 0, zwj = 0;
  size_t i;
  if (n == 0) return 0;
  for (i = 0; i < n; ++i) {
    RolltuiCodepoint cp = cps[i];
    unsigned char gb = gb_class(cp);
    if (gb == ROLLTUI_GRAPHEMEBREAK_Regional_Indicator) ++ri;
    if (cp == 0xFE0F) vs16 = 1;
    if (cp == 0x20E3) keycap = 1;
    if (cp == 0x200D) zwj = 1;
    if (i > 0 && gb == ROLLTUI_GRAPHEMEBREAK_Extend) continue;
    sum += rolltui_u_codepoint_width(cp, ambiguous_wide);
  }
  if (ri >= 2) return 2;
  if (keycap) return 2;
  if (is_pict(cps[0]) && (vs16 || zwj)) return 2;
  return sum;
}

/* ---- UAX #29: grapheme clusters ---------------------------------------------------------- */

void rolltui_u_grapheme_boundaries(RolltuiUnicodeScratch* sc, const RolltuiCodepoint* cps, size_t n,
                                   unsigned char* b) {
  unsigned char *g, *incb, *pict;
  size_t i;
  memset(b, 0, n + 1);
  b[0] = 1;
  b[n] = 1;
  if (n < 2) return;

  g = sc->gb = rolltui_grow(sc->gb, &sc->gb_cap, n, 1);
  incb = sc->incb = rolltui_grow(sc->incb, &sc->incb_cap, n, 1);
  pict = sc->pict = rolltui_grow(sc->pict, &sc->pict_cap, n, 1);
  for (i = 0; i < n; ++i) {
    g[i] = gb_class(cps[i]);
    incb[i] = incb_class(cps[i]);
    pict[i] = (unsigned char)is_pict(cps[i]);
  }
  for (i = 1; i < n; ++i) {
    const unsigned char p = g[i - 1], q = g[i];
    int brk;
    if (p == ROLLTUI_GRAPHEMEBREAK_CR && q == ROLLTUI_GRAPHEMEBREAK_LF) brk = 0;             /* GB3 */
    else if (p == ROLLTUI_GRAPHEMEBREAK_Control || p == ROLLTUI_GRAPHEMEBREAK_CR ||
             p == ROLLTUI_GRAPHEMEBREAK_LF)
      brk = 1;                                                                               /* GB4 */
    else if (q == ROLLTUI_GRAPHEMEBREAK_Control || q == ROLLTUI_GRAPHEMEBREAK_CR ||
             q == ROLLTUI_GRAPHEMEBREAK_LF)
      brk = 1;                                                                               /* GB5 */
    else if (p == ROLLTUI_GRAPHEMEBREAK_L &&
             (q == ROLLTUI_GRAPHEMEBREAK_L || q == ROLLTUI_GRAPHEMEBREAK_V ||
              q == ROLLTUI_GRAPHEMEBREAK_LV || q == ROLLTUI_GRAPHEMEBREAK_LVT))
      brk = 0;                                                                               /* GB6 */
    else if ((p == ROLLTUI_GRAPHEMEBREAK_LV || p == ROLLTUI_GRAPHEMEBREAK_V) &&
             (q == ROLLTUI_GRAPHEMEBREAK_V || q == ROLLTUI_GRAPHEMEBREAK_T))
      brk = 0;                                                                               /* GB7 */
    else if ((p == ROLLTUI_GRAPHEMEBREAK_LVT || p == ROLLTUI_GRAPHEMEBREAK_T) &&
             q == ROLLTUI_GRAPHEMEBREAK_T)
      brk = 0;                                                                               /* GB8 */
    else if (q == ROLLTUI_GRAPHEMEBREAK_Extend || q == ROLLTUI_GRAPHEMEBREAK_ZWJ) brk = 0;   /* GB9 */
    else if (q == ROLLTUI_GRAPHEMEBREAK_SpacingMark) brk = 0;                                /* GB9a */
    else if (p == ROLLTUI_GRAPHEMEBREAK_Prepend) brk = 0;                                    /* GB9b */
    else {
      brk = 1;
      /* GB9c: Consonant [Extend Linker]* Linker [Extend Linker]* x Consonant */
      if (incb[i] == ROLLTUI_INDICCONJUNCTBREAK_Consonant) {
        int linker = 0;
        size_t j = i;
        while (j > 0 && (incb[j - 1] == ROLLTUI_INDICCONJUNCTBREAK_Extend ||
                         incb[j - 1] == ROLLTUI_INDICCONJUNCTBREAK_Linker)) {
          if (incb[j - 1] == ROLLTUI_INDICCONJUNCTBREAK_Linker) linker = 1;
          --j;
        }
        if (linker && j > 0 && incb[j - 1] == ROLLTUI_INDICCONJUNCTBREAK_Consonant) brk = 0;
      }
      /* GB11: ExtPict Extend* ZWJ x ExtPict */
      if (brk && pict[i] && p == ROLLTUI_GRAPHEMEBREAK_ZWJ) {
        size_t j = i - 1;
        while (j > 0 && g[j - 1] == ROLLTUI_GRAPHEMEBREAK_Extend) --j;
        if (j > 0 && pict[j - 1]) brk = 0;
      }
      /* GB12/13: break between RIs only after an even run of them */
      if (brk && p == ROLLTUI_GRAPHEMEBREAK_Regional_Indicator &&
          q == ROLLTUI_GRAPHEMEBREAK_Regional_Indicator) {
        size_t run = 0, j = i;
        while (j > 0 && g[j - 1] == ROLLTUI_GRAPHEMEBREAK_Regional_Indicator) {
          ++run;
          --j;
        }
        if (run % 2 == 1) brk = 0;
      }
    }
    b[i] = (unsigned char)brk;
  }
}

size_t rolltui_u_graphemes(RolltuiUnicodeScratch* sc, const char* utf8, size_t len, int ambiguous_wide,
                           RolltuiUnicodeGrapheme* out) {
  RolltuiCodepoint* cps;
  size_t *offs, *lens;
  unsigned char* b;
  size_t n, i, start = 0, count = 0;
  if (len == 0) return 0;

  cps = sc->cps = rolltui_grow(sc->cps, &sc->cps_cap, len, sizeof *sc->cps);
  offs = sc->offs = rolltui_grow(sc->offs, &sc->offs_cap, len, sizeof *sc->offs);
  lens = sc->lens = rolltui_grow(sc->lens, &sc->lens_cap, len, sizeof *sc->lens);
  b = sc->bounds = rolltui_grow(sc->bounds, &sc->bounds_cap, len + 1, 1);

  n = rolltui_u_decode_utf8(utf8, len, cps, offs, lens);
  rolltui_u_grapheme_boundaries(sc, cps, n, b);
  for (i = 1; i <= n; ++i) {
    if (!b[i]) continue;
    out[count].offset = offs[start];
    out[count].length = offs[i - 1] + lens[i - 1] - offs[start];
    out[count].width = rolltui_u_cluster_width(cps + start, i - start, ambiguous_wide);
    ++count;
    start = i;
  }
  return count;
}

int rolltui_u_display_width(RolltuiUnicodeScratch* sc, const char* utf8, size_t len, int ambiguous_wide) {
  RolltuiUnicodeGrapheme* g;
  size_t n, i;
  int w = 0;
  if (len == 0) return 0;
  g = sc->gr = rolltui_grow(sc->gr, &sc->gr_cap, len, sizeof *sc->gr);
  n = rolltui_u_graphemes(sc, utf8, len, ambiguous_wide, g);
  for (i = 0; i < n; ++i) w += g[i].width;
  return w;
}

/* ---- UAX #29: words ---------------------------------------------------------------------- */

#define WB_NONE ((size_t) - 1)

static int wb_ignorable(unsigned char c) {
  return c == ROLLTUI_WORDBREAK_Extend || c == ROLLTUI_WORDBREAK_Format || c == ROLLTUI_WORDBREAK_ZWJ;
}
static int wb_newline(unsigned char c) {
  return c == ROLLTUI_WORDBREAK_Newline || c == ROLLTUI_WORDBREAK_CR || c == ROLLTUI_WORDBREAK_LF;
}
static int wb_ah(unsigned char c) {
  return c == ROLLTUI_WORDBREAK_ALetter || c == ROLLTUI_WORDBREAK_Hebrew_Letter;
}
static int wb_midnumletq(unsigned char c) {
  return c == ROLLTUI_WORDBREAK_MidNumLet || c == ROLLTUI_WORDBREAK_Single_Quote;
}
static int wb_word_like(unsigned char c) {
  return wb_ah(c) || c == ROLLTUI_WORDBREAK_Numeric || c == ROLLTUI_WORDBREAK_Katakana;
}
/* The class of the nearest non-ignorable code point strictly before `i`, per WB4. */
static size_t wb_prev_of(const unsigned char* w, size_t i) {
  while (i > 0) {
    --i;
    if (!wb_ignorable(w[i])) return i;
  }
  return WB_NONE;
}
static size_t wb_next_of(const unsigned char* w, size_t n, size_t i) {
  while (i < n && wb_ignorable(w[i])) ++i;
  return i < n ? i : WB_NONE;
}

void rolltui_u_word_boundaries(RolltuiUnicodeScratch* sc, const RolltuiCodepoint* cps, size_t n,
                               unsigned char* b) {
  unsigned char *w, *pict;
  size_t i;
  memset(b, 0, n + 1);
  b[0] = 1; /* WB1 */
  b[n] = 1; /* WB2 */
  if (n < 2) return;

  w = sc->wb = rolltui_grow(sc->wb, &sc->wb_cap, n, 1);
  pict = sc->wpict = rolltui_grow(sc->wpict, &sc->wpict_cap, n, 1);
  for (i = 0; i < n; ++i) {
    w[i] = wb_class(cps[i]);
    pict[i] = (unsigned char)is_pict(cps[i]);
  }
  for (i = 1; i < n; ++i) {
    const unsigned char p = w[i - 1], q = w[i];
    int brk;
    if (p == ROLLTUI_WORDBREAK_CR && q == ROLLTUI_WORDBREAK_LF) brk = 0;              /* WB3 */
    else if (wb_newline(p)) brk = 1;                                                  /* WB3a */
    else if (wb_newline(q)) brk = 1;                                                  /* WB3b */
    else if (p == ROLLTUI_WORDBREAK_ZWJ && pict[i]) brk = 0;                          /* WB3c */
    else if (p == ROLLTUI_WORDBREAK_WSegSpace && q == ROLLTUI_WORDBREAK_WSegSpace) brk = 0; /* WB3d */
    else if (wb_ignorable(q)) brk = 0;                                                /* WB4 */
    else {
      /* From here on, WB4 has already erased Extend/Format/ZWJ: look through them. */
      const size_t pi = wb_prev_of(w, i);
      const int has_a = pi != WB_NONE;
      const unsigned char a = has_a ? w[pi] : ROLLTUI_WORDBREAK_Other;
      const size_t ppi = has_a ? wb_prev_of(w, pi) : WB_NONE;
      const int has_aa = ppi != WB_NONE;
      const unsigned char aa = has_aa ? w[ppi] : ROLLTUI_WORDBREAK_Other;
      const unsigned char c = q;
      const size_t ni = wb_next_of(w, n, i + 1);
      const int has_cc = ni != WB_NONE;
      const unsigned char cc = has_cc ? w[ni] : ROLLTUI_WORDBREAK_Other;
      brk = 1;
      if (!has_a) brk = 1;                                                            /* WB999 */
      else if (wb_ah(a) && wb_ah(c)) brk = 0;                                         /* WB5 */
      else if (wb_ah(a) && (c == ROLLTUI_WORDBREAK_MidLetter || wb_midnumletq(c)) && has_cc && wb_ah(cc))
        brk = 0;                                                                      /* WB6 */
      else if (has_aa && wb_ah(aa) && (a == ROLLTUI_WORDBREAK_MidLetter || wb_midnumletq(a)) && wb_ah(c))
        brk = 0;                                                                      /* WB7 */
      else if (a == ROLLTUI_WORDBREAK_Hebrew_Letter && c == ROLLTUI_WORDBREAK_Single_Quote) brk = 0; /* WB7a */
      else if (a == ROLLTUI_WORDBREAK_Hebrew_Letter && c == ROLLTUI_WORDBREAK_Double_Quote && has_cc &&
               cc == ROLLTUI_WORDBREAK_Hebrew_Letter)
        brk = 0;                                                                      /* WB7b */
      else if (has_aa && aa == ROLLTUI_WORDBREAK_Hebrew_Letter && a == ROLLTUI_WORDBREAK_Double_Quote &&
               c == ROLLTUI_WORDBREAK_Hebrew_Letter)
        brk = 0;                                                                      /* WB7c */
      else if (a == ROLLTUI_WORDBREAK_Numeric && c == ROLLTUI_WORDBREAK_Numeric) brk = 0;  /* WB8 */
      else if (wb_ah(a) && c == ROLLTUI_WORDBREAK_Numeric) brk = 0;                   /* WB9 */
      else if (a == ROLLTUI_WORDBREAK_Numeric && wb_ah(c)) brk = 0;                   /* WB10 */
      else if (has_aa && aa == ROLLTUI_WORDBREAK_Numeric &&
               (a == ROLLTUI_WORDBREAK_MidNum || wb_midnumletq(a)) && c == ROLLTUI_WORDBREAK_Numeric)
        brk = 0;                                                                      /* WB11 */
      else if (a == ROLLTUI_WORDBREAK_Numeric && (c == ROLLTUI_WORDBREAK_MidNum || wb_midnumletq(c)) &&
               has_cc && cc == ROLLTUI_WORDBREAK_Numeric)
        brk = 0;                                                                      /* WB12 */
      else if (a == ROLLTUI_WORDBREAK_Katakana && c == ROLLTUI_WORDBREAK_Katakana) brk = 0;  /* WB13 */
      else if ((wb_word_like(a) || a == ROLLTUI_WORDBREAK_ExtendNumLet) && c == ROLLTUI_WORDBREAK_ExtendNumLet)
        brk = 0;                                                                      /* WB13a */
      else if (a == ROLLTUI_WORDBREAK_ExtendNumLet && wb_word_like(c)) brk = 0;       /* WB13b */
      else if (a == ROLLTUI_WORDBREAK_Regional_Indicator && c == ROLLTUI_WORDBREAK_Regional_Indicator) {
        size_t run = 0, j = pi;                                                       /* WB15/16 */
        while (j != WB_NONE && w[j] == ROLLTUI_WORDBREAK_Regional_Indicator) {
          ++run;
          j = wb_prev_of(w, j);
        }
        brk = (run % 2 == 0);
      }
    }
    b[i] = (unsigned char)brk;
  }
}

void rolltui_u_word_range(RolltuiUnicodeScratch* sc, const char* utf8, size_t len, size_t offset,
                          size_t* out_begin, size_t* out_end) {
  RolltuiCodepoint* cps;
  size_t *offs, *lens;
  unsigned char* b;
  size_t n, k = 0, start, end;
  if (offset >= len || len == 0) {
    *out_begin = *out_end = len;
    return;
  }
  cps = sc->cps = rolltui_grow(sc->cps, &sc->cps_cap, len, sizeof *sc->cps);
  offs = sc->offs = rolltui_grow(sc->offs, &sc->offs_cap, len, sizeof *sc->offs);
  lens = sc->lens = rolltui_grow(sc->lens, &sc->lens_cap, len, sizeof *sc->lens);
  b = sc->bounds = rolltui_grow(sc->bounds, &sc->bounds_cap, len + 1, 1);
  n = rolltui_u_decode_utf8(utf8, len, cps, offs, lens);
  if (n == 0) {
    *out_begin = *out_end = len;
    return;
  }
  rolltui_u_word_boundaries(sc, cps, n, b);
  while (k + 1 < n && offs[k + 1] <= offset) ++k; /* the code point containing `offset` */
  start = k;
  end = k + 1;
  while (start > 0 && !b[start]) --start;
  while (end < n && !b[end]) ++end;
  *out_begin = offs[start];
  *out_end = offs[end - 1] + lens[end - 1];
}

/* ---- sanitising --------------------------------------------------------------------------- */

/* Skips a string sequence's payload up to and including its terminator (BEL, or ESC \, or the
 * C1 ST U+009C); returns the index just past it (n if unterminated). */
static size_t skip_string(const unsigned char* s, size_t n, size_t i) {
  while (i < n) {
    if (s[i] == 0x07) return i + 1;
    if (s[i] == 0x1B && i + 1 < n && s[i + 1] == '\\') return i + 2;
    if (s[i] == 0xC2 && i + 1 < n && s[i + 1] == 0x9C) return i + 2;
    ++i;
  }
  return n;
}
static size_t skip_csi(const unsigned char* s, size_t n, size_t i) { /* i is just past the introducer */
  while (i < n && s[i] >= 0x20 && s[i] <= 0x3F) ++i; /* parameters + intermediates */
  if (i < n && s[i] >= 0x40 && s[i] <= 0x7E) ++i;    /* final byte */
  return i;
}

size_t rolltui_u_strip_escape_sequences(const char* text, size_t len, char* out) {
  const unsigned char* s = (const unsigned char*)text;
  size_t i = 0, w = 0;
  while (i < len) {
    const unsigned char c = s[i];
    if (c == 0x1B) {
      unsigned char d;
      if (i + 1 >= len) { /* a lone trailing ESC: dropped */
        ++i;
        continue;
      }
      d = s[i + 1];
      if (d == '[') i = skip_csi(s, len, i + 2);
      else if (d == ']' || d == 'P' || d == 'X' || d == '^' || d == '_') i = skip_string(s, len, i + 2);
      else if (d >= 0x20 && d <= 0x2F) { /* ESC intermediate* final */
        size_t j = i + 1;
        while (j < len && s[j] >= 0x20 && s[j] <= 0x2F) ++j;
        i = (j < len && s[j] >= 0x30 && s[j] <= 0x7E) ? j + 1 : j;
      } else if (d >= 0x30 && d <= 0x7E) i += 2; /* ESC final (ESC c, ESC 7, ESC = ...) */
      else ++i;                                  /* ESC before a control or non-ASCII: drop the ESC alone */
      continue;
    }
    /* 8-bit C1 introducers, as UTF-8 (C2 9B = CSI, C2 9D = OSC, C2 90/98/9E/9F strings). */
    if (c == 0xC2 && i + 1 < len) {
      const unsigned char d = s[i + 1];
      if (d == 0x9B) {
        i = skip_csi(s, len, i + 2);
        continue;
      }
      if (d == 0x9D || d == 0x90 || d == 0x98 || d == 0x9E || d == 0x9F) {
        i = skip_string(s, len, i + 2);
        continue;
      }
    }
    out[w++] = text[i];
    ++i;
  }
  return w;
}

/* ---- UAX #14 ------------------------------------------------------------------------------- */

/* LB9/LB10 are applied structurally: the text is first cut into units — a base character with
 * every CM/ZWJ attached to it (LB9), or a lone CM/ZWJ that had no eligible base and so becomes
 * AL with U+0041's properties (LB10). A unit carries its base's class and the properties later
 * rules read (East Asian width, Pi/Pf for quotation marks, the dotted circle,
 * Extended_Pictographic AND Cn). Positions inside a unit are never breaks; every rule below
 * runs between units. */
struct Unit {
  unsigned char cls;
  unsigned char east_asian; /* ea in {F, W, H} — $EastAsian in LB19a / LB30 */
  unsigned char pi, pf;     /* gc of a QU base */
  unsigned char dotted;     /* U+25CC, the [o] of LB28a */
  unsigned char pict_cn;    /* Extended_Pictographic AND Cn, for LB30b */
  unsigned char ends_zwj;   /* last code point is U+200D, for LB8a */
  size_t first;             /* index of the base in cps */
};

static unsigned char lb_resolved(RolltuiCodepoint cp) {
  unsigned char c = lb_class(cp);
  switch (c) {
    case ROLLTUI_LINEBREAK_AI:
    case ROLLTUI_LINEBREAK_SG:
    case ROLLTUI_LINEBREAK_XX: return ROLLTUI_LINEBREAK_AL;
    case ROLLTUI_LINEBREAK_CJ: return ROLLTUI_LINEBREAK_NS;
    case ROLLTUI_LINEBREAK_SA: {
      unsigned char gc = gc_class(cp);
      return (gc == ROLLTUI_GENERALCATEGORY_Mn || gc == ROLLTUI_GENERALCATEGORY_Mc) ? ROLLTUI_LINEBREAK_CM
                                                                                    : ROLLTUI_LINEBREAK_AL;
    }
    default: return c;
  }
}

static int lb_no_attach(unsigned char c) {
  return c == ROLLTUI_LINEBREAK_BK || c == ROLLTUI_LINEBREAK_CR || c == ROLLTUI_LINEBREAK_LF ||
         c == ROLLTUI_LINEBREAK_NL || c == ROLLTUI_LINEBREAK_SP || c == ROLLTUI_LINEBREAK_ZW;
}
/* Index of the last unit before k that is not SP (or -1): the "SP*" lookbehind that LB8, LB14,
 * LB15a, LB16 and LB17 share. */
static long lb_before_spaces(const Unit* u, size_t k) {
  long j = (long)k - 1;
  while (j >= 0 && u[j].cls == ROLLTUI_LINEBREAK_SP) --j;
  return j;
}
/* Index of the last unit before k that is not SY/IS (or -1): LB25's "(SY|IS)*". */
static long lb_before_sy_is(const Unit* u, size_t k) {
  long j = (long)k - 1;
  while (j >= 0 && (u[j].cls == ROLLTUI_LINEBREAK_SY || u[j].cls == ROLLTUI_LINEBREAK_IS)) --j;
  return j;
}

#define LB_(x) ROLLTUI_LINEBREAK_##x

/* The boundary before unit k, 1 <= k < m. */
static unsigned char lb_decide(const Unit* u, size_t m, size_t k) {
  const Unit* P = &u[k - 1];
  const Unit* N = &u[k];
  const unsigned char p = P->cls, q = N->cls;
  const int has_next = (k + 1 < m);
  const unsigned char next2 = has_next ? u[k + 1].cls : LB_(XX);
  const int has_prev2 = (k >= 2);
  const unsigned char prev2 = has_prev2 ? u[k - 2].cls : LB_(XX);

  /* LB4, LB5 */
  if (p == LB_(BK)) return ROLLTUI_BREAK_MANDATORY;
  if (p == LB_(CR) && q == LB_(LF)) return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(CR) || p == LB_(LF) || p == LB_(NL)) return ROLLTUI_BREAK_MANDATORY;
  /* LB6 */
  if (q == LB_(BK) || q == LB_(CR) || q == LB_(LF) || q == LB_(NL)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB7 */
  if (q == LB_(SP) || q == LB_(ZW)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB8: ZW SP* / */
  {
    long j = lb_before_spaces(u, k);
    if (j >= 0 && u[j].cls == LB_(ZW)) return ROLLTUI_BREAK_ALLOWED;
  }
  /* LB8a: ZWJ x */
  if (P->ends_zwj) return ROLLTUI_BREAK_PROHIBITED;
  /* LB9, LB10: structural (units) */
  /* LB11 */
  if (q == LB_(WJ) || p == LB_(WJ)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB12 */
  if (p == LB_(GL)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB12a: [^SP BA HY HH] x GL */
  if (q == LB_(GL) && !(p == LB_(SP) || p == LB_(BA) || p == LB_(HY) || p == LB_(HH)))
    return ROLLTUI_BREAK_PROHIBITED;
  /* LB13 */
  if (q == LB_(CL) || q == LB_(CP) || q == LB_(EX) || q == LB_(SY)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB14: OP SP* x */
  {
    long j = lb_before_spaces(u, k);
    if (j >= 0 && u[j].cls == LB_(OP)) return ROLLTUI_BREAK_PROHIBITED;
  }
  /* LB15a: (sot | BK | CR | LF | NL | OP | QU | GL | SP | ZW) [\p{Pi}&QU] SP* x */
  {
    long j = lb_before_spaces(u, k);
    if (j >= 0 && u[j].cls == LB_(QU) && u[j].pi) {
      unsigned char b = (j == 0) ? 0 : u[j - 1].cls;
      int ctx = (j == 0) || b == LB_(BK) || b == LB_(CR) || b == LB_(LF) || b == LB_(NL) || b == LB_(OP) ||
                b == LB_(QU) || b == LB_(GL) || b == LB_(SP) || b == LB_(ZW);
      if (ctx) return ROLLTUI_BREAK_PROHIBITED;
    }
  }
  /* LB15b: x [\p{Pf}&QU] (SP | GL | WJ | CL | QU | CP | EX | IS | SY | BK | CR | LF | NL | ZW | eot) */
  if (q == LB_(QU) && N->pf) {
    int ctx = !has_next || next2 == LB_(SP) || next2 == LB_(GL) || next2 == LB_(WJ) || next2 == LB_(CL) ||
              next2 == LB_(QU) || next2 == LB_(CP) || next2 == LB_(EX) || next2 == LB_(IS) ||
              next2 == LB_(SY) || next2 == LB_(BK) || next2 == LB_(CR) || next2 == LB_(LF) ||
              next2 == LB_(NL) || next2 == LB_(ZW);
    if (ctx) return ROLLTUI_BREAK_PROHIBITED;
  }
  /* LB15c: SP / IS NU */
  if (p == LB_(SP) && q == LB_(IS) && has_next && next2 == LB_(NU)) return ROLLTUI_BREAK_ALLOWED;
  /* LB15d: x IS */
  if (q == LB_(IS)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB16: (CL | CP) SP* x NS */
  if (q == LB_(NS)) {
    long j = lb_before_spaces(u, k);
    if (j >= 0 && (u[j].cls == LB_(CL) || u[j].cls == LB_(CP))) return ROLLTUI_BREAK_PROHIBITED;
  }
  /* LB17: B2 SP* x B2 */
  if (q == LB_(B2)) {
    long j = lb_before_spaces(u, k);
    if (j >= 0 && u[j].cls == LB_(B2)) return ROLLTUI_BREAK_PROHIBITED;
  }
  /* LB18: SP / */
  if (p == LB_(SP)) return ROLLTUI_BREAK_ALLOWED;
  /* LB19: x [QU - \p{Pi}] ; [QU - \p{Pf}] x */
  if (q == LB_(QU) && !N->pi) return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(QU) && !P->pf) return ROLLTUI_BREAK_PROHIBITED;
  /* LB19a: unless surrounded by East Asian characters, do not break either side of QU */
  if (q == LB_(QU) && !P->east_asian) return ROLLTUI_BREAK_PROHIBITED;
  if (q == LB_(QU) && (!has_next || !u[k + 1].east_asian)) return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(QU) && !N->east_asian) return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(QU) && (!has_prev2 || !u[k - 2].east_asian)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB20: / CB ; CB / */
  if (q == LB_(CB) || p == LB_(CB)) return ROLLTUI_BREAK_ALLOWED;
  /* LB20a: (sot | BK | CR | LF | NL | SP | ZW | CB | GL) (HY | HH) x (AL | HL) */
  if ((p == LB_(HY) || p == LB_(HH)) && (q == LB_(AL) || q == LB_(HL))) {
    int ctx = !has_prev2 || prev2 == LB_(BK) || prev2 == LB_(CR) || prev2 == LB_(LF) || prev2 == LB_(NL) ||
              prev2 == LB_(SP) || prev2 == LB_(ZW) || prev2 == LB_(CB) || prev2 == LB_(GL);
    if (ctx) return ROLLTUI_BREAK_PROHIBITED;
  }
  /* LB21: x BA ; x HH ; x HY ; x NS ; BB x */
  if (q == LB_(BA) || q == LB_(HH) || q == LB_(HY) || q == LB_(NS)) return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(BB)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB21a: HL (HY | HH) x [^HL] */
  if ((p == LB_(HY) || p == LB_(HH)) && has_prev2 && prev2 == LB_(HL) && q != LB_(HL))
    return ROLLTUI_BREAK_PROHIBITED;
  /* LB21b: SY x HL */
  if (p == LB_(SY) && q == LB_(HL)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB22: x IN */
  if (q == LB_(IN)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB23: (AL | HL) x NU ; NU x (AL | HL) */
  if ((p == LB_(AL) || p == LB_(HL)) && q == LB_(NU)) return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(NU) && (q == LB_(AL) || q == LB_(HL))) return ROLLTUI_BREAK_PROHIBITED;
  /* LB23a: PR x (ID | EB | EM) ; (ID | EB | EM) x PO */
  if (p == LB_(PR) && (q == LB_(ID) || q == LB_(EB) || q == LB_(EM))) return ROLLTUI_BREAK_PROHIBITED;
  if ((p == LB_(ID) || p == LB_(EB) || p == LB_(EM)) && q == LB_(PO)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB24: (PR | PO) x (AL | HL) ; (AL | HL) x (PR | PO) */
  if ((p == LB_(PR) || p == LB_(PO)) && (q == LB_(AL) || q == LB_(HL))) return ROLLTUI_BREAK_PROHIBITED;
  if ((p == LB_(AL) || p == LB_(HL)) && (q == LB_(PR) || q == LB_(PO))) return ROLLTUI_BREAK_PROHIBITED;
  /* LB25 (the fifteen sub-rules) */
  {
    /* NU (SY | IS)* (CL | CP) x (PO | PR) */
    if ((p == LB_(CL) || p == LB_(CP)) && (q == LB_(PO) || q == LB_(PR))) {
      long j = lb_before_sy_is(u, k - 1);
      if (j >= 0 && u[j].cls == LB_(NU)) return ROLLTUI_BREAK_PROHIBITED;
    }
    /* NU (SY | IS)* x (PO | PR) */
    if (q == LB_(PO) || q == LB_(PR)) {
      long j = lb_before_sy_is(u, k);
      if (j >= 0 && u[j].cls == LB_(NU)) return ROLLTUI_BREAK_PROHIBITED;
    }
    /* (PO | PR) x OP NU ; (PO | PR) x OP IS NU */
    if ((p == LB_(PO) || p == LB_(PR)) && q == LB_(OP) && has_next) {
      if (next2 == LB_(NU)) return ROLLTUI_BREAK_PROHIBITED;
      if (next2 == LB_(IS) && k + 2 < m && u[k + 2].cls == LB_(NU)) return ROLLTUI_BREAK_PROHIBITED;
    }
    /* (PO | PR) x NU ; HY x NU ; IS x NU */
    if ((p == LB_(PO) || p == LB_(PR) || p == LB_(HY) || p == LB_(IS)) && q == LB_(NU))
      return ROLLTUI_BREAK_PROHIBITED;
    /* NU (SY | IS)* x NU */
    if (q == LB_(NU)) {
      long j = lb_before_sy_is(u, k);
      if (j >= 0 && u[j].cls == LB_(NU)) return ROLLTUI_BREAK_PROHIBITED;
    }
  }
  /* LB26: JL x (JL | JV | H2 | H3) ; (JV | H2) x (JV | JT) ; (JT | H3) x JT */
  if (p == LB_(JL) && (q == LB_(JL) || q == LB_(JV) || q == LB_(H2) || q == LB_(H3)))
    return ROLLTUI_BREAK_PROHIBITED;
  if ((p == LB_(JV) || p == LB_(H2)) && (q == LB_(JV) || q == LB_(JT))) return ROLLTUI_BREAK_PROHIBITED;
  if ((p == LB_(JT) || p == LB_(H3)) && q == LB_(JT)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB27: (JL | JV | JT | H2 | H3) x PO ; PR x (JL | JV | JT | H2 | H3) */
  if ((p == LB_(JL) || p == LB_(JV) || p == LB_(JT) || p == LB_(H2) || p == LB_(H3)) && q == LB_(PO))
    return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(PR) && (q == LB_(JL) || q == LB_(JV) || q == LB_(JT) || q == LB_(H2) || q == LB_(H3)))
    return ROLLTUI_BREAK_PROHIBITED;
  /* LB28: (AL | HL) x (AL | HL) */
  if ((p == LB_(AL) || p == LB_(HL)) && (q == LB_(AL) || q == LB_(HL))) return ROLLTUI_BREAK_PROHIBITED;
  /* LB28a: Brahmic orthographic syllables, with [o] = U+25CC */
  {
#define LB_AK(x) ((x)->cls == LB_(AK) || (x)->dotted)
#define LB_AK_AS(x) (LB_AK(x) || (x)->cls == LB_(AS))
    if (p == LB_(AP) && LB_AK_AS(N)) return ROLLTUI_BREAK_PROHIBITED;              /* AP x (AK|o|AS) */
    if (LB_AK_AS(P) && (q == LB_(VF) || q == LB_(VI))) return ROLLTUI_BREAK_PROHIBITED; /* (AK|o|AS) x (VF|VI) */
    if (p == LB_(VI) && has_prev2 && LB_AK_AS(&u[k - 2]) && LB_AK(N))
      return ROLLTUI_BREAK_PROHIBITED;                                            /* (AK|o|AS) VI x (AK|o) */
    if (LB_AK_AS(P) && LB_AK_AS(N) && has_next && next2 == LB_(VF))
      return ROLLTUI_BREAK_PROHIBITED;                                            /* (AK|o|AS) x (AK|o|AS) VF */
#undef LB_AK
#undef LB_AK_AS
  }
  /* LB29: IS x (AL | HL) */
  if (p == LB_(IS) && (q == LB_(AL) || q == LB_(HL))) return ROLLTUI_BREAK_PROHIBITED;
  /* LB30: (AL | HL | NU) x [OP-$EastAsian] ; [CP-$EastAsian] x (AL | HL | NU) */
  if ((p == LB_(AL) || p == LB_(HL) || p == LB_(NU)) && q == LB_(OP) && !N->east_asian)
    return ROLLTUI_BREAK_PROHIBITED;
  if (p == LB_(CP) && !P->east_asian && (q == LB_(AL) || q == LB_(HL) || q == LB_(NU)))
    return ROLLTUI_BREAK_PROHIBITED;
  /* LB30a: break between RIs only after an even number of them */
  if (p == LB_(RI) && q == LB_(RI)) {
    size_t run = 0;
    long j;
    for (j = (long)k - 1; j >= 0 && u[j].cls == LB_(RI); --j) ++run;
    if (run % 2 == 1) return ROLLTUI_BREAK_PROHIBITED;
  }
  /* LB30b: EB x EM ; [\p{Extended_Pictographic}&\p{Cn}] x EM */
  if (q == LB_(EM) && (p == LB_(EB) || P->pict_cn)) return ROLLTUI_BREAK_PROHIBITED;
  /* LB31 */
  return ROLLTUI_BREAK_ALLOWED;
}

void rolltui_u_line_break_opportunities(RolltuiUnicodeScratch* sc, const RolltuiCodepoint* cps, size_t n,
                                        unsigned char* out) {
  Unit* u;
  size_t m = 0, i, k;

  memset(out, ROLLTUI_BREAK_PROHIBITED, n + 1);
  out[n] = ROLLTUI_BREAK_MANDATORY;
  if (n == 0) return;

  u = sc->units = rolltui_grow(sc->units, &sc->units_cap, n, sizeof *sc->units);
  for (i = 0; i < n; ++i) {
    unsigned char c = lb_resolved(cps[i]);
    int joiner = (c == ROLLTUI_LINEBREAK_CM || c == ROLLTUI_LINEBREAK_ZWJ);
    Unit* x;
    if (joiner && m > 0 && !lb_no_attach(u[m - 1].cls)) {
      u[m - 1].ends_zwj = (unsigned char)(c == ROLLTUI_LINEBREAK_ZWJ);
      continue;
    }
    x = &u[m++];
    x->first = i;
    x->ends_zwj = (unsigned char)(c == ROLLTUI_LINEBREAK_ZWJ);
    if (joiner) { /* LB10 */
      x->cls = ROLLTUI_LINEBREAK_AL;
      x->east_asian = x->pi = x->pf = x->dotted = x->pict_cn = 0;
    } else {
      unsigned char ea = ea_width(cps[i]);
      unsigned char gc = gc_class(cps[i]);
      x->cls = c;
      x->east_asian = (unsigned char)(ea == ROLLTUI_EASTASIANWIDTH_F || ea == ROLLTUI_EASTASIANWIDTH_W ||
                                      ea == ROLLTUI_EASTASIANWIDTH_H);
      x->pi = (unsigned char)(c == ROLLTUI_LINEBREAK_QU && gc == ROLLTUI_GENERALCATEGORY_Pi);
      x->pf = (unsigned char)(c == ROLLTUI_LINEBREAK_QU && gc == ROLLTUI_GENERALCATEGORY_Pf);
      x->dotted = (unsigned char)(cps[i] == 0x25CC);
      x->pict_cn = (unsigned char)(gc == ROLLTUI_GENERALCATEGORY_Cn && is_pict(cps[i]));
    }
  }
  /* Note: a unit whose base is itself a lone CM (LB10 -> AL) still collects later CM/ZWJ into
   * itself, because its class AL is attachable — "SP CM CM" is one AL. */
  for (k = 1; k < m; ++k) out[u[k].first] = lb_decide(u, m, k);
  /* LB3: eot is a mandatory break, LB2: sot never is — both set at construction. */
}
