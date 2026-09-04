/* rolltui/c/rolltui_diff.c — the C side of the unified-diff colouriser. See rolltui_diff.h
 * for the boundary's rules and rolltui/Diff.hpp for the colouring rules themselves;
 * `DiffCpp.cpp` is the other implementation of the same four functions, and the per-line
 * table in `rolltui/tests/markdown_test.cpp` is the oracle for both.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`, and every buffer lives
 * in the caller's handle: there is no `static` and no thread-local here, so two threads
 * colouring two blocks share nothing. */
#include "rolltui/c/rolltui_diff.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"

/* What one line of a diff IS, for the pairing rule. The file headers are tested BEFORE the
 * markers because "+++ b/x" starts with '+' and is not an added line — an order that looks
 * right in every screenshot that happens to start at a hunk. */
enum { kAdded, kRemoved, kOther };
#define ROLLTUI_DIFF_NO_PARTNER ((size_t)-1)

/* One UAX #29 word token, as a byte range in the line it came from. */
typedef struct {
  size_t begin, end;
} Range;

struct RolltuiDiffScratch {
  /* The Unicode algorithms' working memory, owned here for the same reason the wrap
   * engine's handle owns one: a diff is the only thing that asks for it, so a caller
   * holding one handle holds one of these and no hidden per-thread state exists anywhere
   * in the chain. Created lazily, so a handle that never meets a changed PAIR never pays. */
  RolltuiUnicodeScratch* uni;
  /* GROWING, AMORTISED, one buffer per ROLE (rolltui_alloc.h strategy 2). `ta` and `tb`
   * are the two sides of a pair and must coexist; the decode buffers are refilled for each
   * side in turn, because a token range names bytes of the ORIGINAL line and nothing in
   * the decode survives the call that produced it. */
  RolltuiCodepoint* cps;
  size_t cps_cap;
  size_t* coff;
  size_t coff_cap;
  size_t* clen;
  size_t clen_cap;
  unsigned char* bounds;
  size_t bounds_cap;
  Range* ta;
  size_t ta_cap;
  Range* tb;
  size_t tb_cap;
};

RolltuiDiffScratch* rolltui_diff_scratch_new(void) {
  RolltuiDiffScratch* s = (RolltuiDiffScratch*)rolltui_mem_alloc(sizeof(RolltuiDiffScratch));
  memset(s, 0, sizeof *s);
  return s;
}

void rolltui_diff_scratch_free(RolltuiDiffScratch* s) {
  if (!s) return;
  rolltui_u_scratch_free(s->uni);
  rolltui_mem_free(s->cps);
  rolltui_mem_free(s->coff);
  rolltui_mem_free(s->clen);
  rolltui_mem_free(s->bounds);
  rolltui_mem_free(s->ta);
  rolltui_mem_free(s->tb);
  rolltui_mem_free(s);
}

/* ---- the rules ------------------------------------------------------------------------ */

static int kind_of(const char* line, size_t len) {
  if (len == 0) return kOther;
  if (len >= 3 && (memcmp(line, "+++", 3) == 0 || memcmp(line, "---", 3) == 0)) return kOther;
  if (line[0] == '+') return kAdded;
  if (line[0] == '-') return kRemoved;
  return kOther;
}

static int kind_at(const void* block, RolltuiDiffLineFn line_at, size_t i) {
  size_t len = 0;
  const char* p = line_at(block, i, &len);
  return kind_of(p, len);
}

/* UAX #29 word tokens of `text`, into `*out` — the same segmentation double-click uses, so
 * "a word" means one thing in this library. A run of spaces is one token (WB3d) and
 * punctuation is one token per character (WB999), which is what makes the common-affix
 * comparison below land on boundaries a reader would call words. */
static size_t tokens(RolltuiDiffScratch* s, const char* text, size_t len, Range** out, size_t* out_cap) {
  if (len == 0) return 0;
  s->cps = rolltui_grow(s->cps, &s->cps_cap, len, sizeof *s->cps);
  s->coff = rolltui_grow(s->coff, &s->coff_cap, len, sizeof *s->coff);
  s->clen = rolltui_grow(s->clen, &s->clen_cap, len, sizeof *s->clen);
  const size_t n = rolltui_u_decode_utf8(text, len, s->cps, s->coff, s->clen);
  if (n == 0) return 0;
  if (!s->uni) s->uni = rolltui_u_scratch_new();
  s->bounds = rolltui_grow(s->bounds, &s->bounds_cap, n + 1, sizeof *s->bounds);
  rolltui_u_word_boundaries(s->uni, s->cps, n, s->bounds);
  *out = rolltui_grow(*out, out_cap, n, sizeof **out); /* at most one token per scalar */
  size_t count = 0, start = 0;
  /* `bounds[n]` is always the end of text, so the last token always closes here. */
  for (size_t i = 1; i <= n; ++i) {
    if (i < n && !s->bounds[i]) continue;
    (*out)[count].begin = s->coff[start];
    (*out)[count].end = i < n ? s->coff[i] : len;
    ++count;
    start = i;
  }
  return count;
}

static int same_token(const char* a, Range ra, const char* b, Range rb) {
  const size_t na = ra.end - ra.begin, nb = rb.end - rb.begin;
  return na == nb && memcmp(a + ra.begin, b + rb.begin, na) == 0;
}

/* The changed middle of `a` against `b`, both WITHOUT their marker byte, as a byte range in
 * `a`'s own space; `off` is added so the caller gets offsets in the full line. */
static int changed_run(RolltuiDiffScratch* s, const char* a, size_t a_len, const char* b, size_t b_len, size_t off,
                       size_t* begin, size_t* end) {
  const size_t na = tokens(s, a, a_len, &s->ta, &s->ta_cap);
  const size_t nb = tokens(s, b, b_len, &s->tb, &s->tb_cap);
  if (na == 0 || nb == 0) return 0;
  const size_t n = na < nb ? na : nb;
  size_t p = 0;
  while (p < n && same_token(a, s->ta[p], b, s->tb[p])) ++p;
  size_t suf = 0;
  while (suf < n - p && same_token(a, s->ta[na - 1 - suf], b, s->tb[nb - 1 - suf])) ++suf;
  /* Nothing common at either end: the whole line changed, and the line's own role already
   * says so. Marking it a second time is noise, not information. */
  if (p == 0 && suf == 0) return 0;
  if (p + suf >= na) return 0; /* this side's middle is empty: nothing of ITS own changed */
  *begin = off + s->ta[p].begin;
  *end = off + s->ta[na - 1 - suf].end;
  return 1;
}

/* The partner line of a changed line, under the pairing rule in Diff.hpp: a maximal run of
 * k removals immediately followed by a run of k additions pairs i with i.
 * ROLLTUI_DIFF_NO_PARTNER when this line is not in such a pair. */
static size_t partner_of(const void* block, size_t line_count, RolltuiDiffLineFn line_at, size_t index) {
  const int k = kind_at(block, line_at, index);
  if (k == kOther) return ROLLTUI_DIFF_NO_PARTNER;
  /* The removal run [rs, re) and the addition run [as, ae) that must abut it. */
  size_t rs, re, as, ae;
  if (k == kRemoved) {
    rs = index;
    while (rs > 0 && kind_at(block, line_at, rs - 1) == kRemoved) --rs;
    re = index + 1;
    while (re < line_count && kind_at(block, line_at, re) == kRemoved) ++re;
    as = re;
  } else {
    as = index;
    while (as > 0 && kind_at(block, line_at, as - 1) == kAdded) --as;
    re = as;
    rs = as;
    while (rs > 0 && kind_at(block, line_at, rs - 1) == kRemoved) --rs;
  }
  ae = as;
  while (ae < line_count && kind_at(block, line_at, ae) == kAdded) ++ae;
  const size_t removals = re - rs, additions = ae - as;
  if (removals == 0 || removals != additions) return ROLLTUI_DIFF_NO_PARTNER;
  return k == kRemoved ? as + (index - rs) : rs + (index - as);
}

/* ---- the boundary --------------------------------------------------------------------- */

int rolltui_diff_is_language(const char* lang, size_t lang_len) {
  return (lang_len == 4 && memcmp(lang, "diff", 4) == 0) || (lang_len == 5 && memcmp(lang, "patch", 5) == 0) ||
         (lang_len == 5 && memcmp(lang, "udiff", 5) == 0);
}

size_t rolltui_diff_spans(RolltuiDiffScratch* s, const char* lang, size_t lang_len, const void* block,
                          size_t line_count, RolltuiDiffLineFn line_at, size_t index,
                          const RolltuiDiffRoles* roles, RolltuiDiffSpan* out, size_t out_cap) {
  if (!rolltui_diff_is_language(lang, lang_len)) return 0; /* the fence decides; content is never sniffed */
  if (index >= line_count || out_cap < ROLLTUI_DIFF_MAX_SPANS) return 0;
  size_t len = 0;
  const char* line = line_at(block, index, &len);
  /* The WHOLE line takes the role, not just its marker: a half-coloured line reads as a
   * rendering bug, and the marker is doing separate work (it is the non-colour signal). */
  if (len == 0) {
    out[0].begin = 0;
    out[0].end = 0;
    out[0].role = roles->context;
    return 1;
  }
  out[0].begin = 0;
  out[0].end = len;
  if (len >= 3 && (memcmp(line, "+++", 3) == 0 || memcmp(line, "---", 3) == 0)) {
    out[0].role = roles->file_header;
    return 1;
  }
  if (len >= 2 && memcmp(line, "@@", 2) == 0) {
    out[0].role = roles->hunk;
    return 1;
  }
  const int k = kind_of(line, len);
  if (k == kOther) {
    out[0].role = roles->context;
    return 1;
  }
  const unsigned char line_role = k == kAdded ? roles->added : roles->removed;
  const unsigned char word_role = k == kAdded ? roles->added_word : roles->removed_word;
  const size_t partner = partner_of(block, line_count, line_at, index);
  if (partner != ROLLTUI_DIFF_NO_PARTNER) {
    size_t plen = 0;
    const char* pline = line_at(block, partner, &plen);
    size_t b = 0, e = 0;
    /* Both sides without their marker byte; `off` 1 puts the answer back in the line's
     * own space. A partner is always a marked line, so `plen` is at least 1. */
    if (changed_run(s, line + 1, len - 1, pline + 1, plen - 1, 1, &b, &e)) {
      /* Three NON-OVERLAPPING spans. The renderer resolves an overlap by dropping the
       * later span and reporting it, so a highlighter that relied on being clamped into
       * shape would be one that is wrong (Diff.hpp). */
      size_t n = 0;
      if (b > 0) {
        out[n].begin = 0;
        out[n].end = b;
        out[n].role = line_role;
        ++n;
      }
      out[n].begin = b;
      out[n].end = e;
      out[n].role = word_role;
      ++n;
      if (e < len) {
        out[n].begin = e;
        out[n].end = len;
        out[n].role = line_role;
        ++n;
      }
      return n;
    }
  }
  out[0].begin = 0;
  out[0].end = len;
  out[0].role = line_role;
  return 1;
}
