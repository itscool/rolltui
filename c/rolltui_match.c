/* rolltui/c/rolltui_match.c — FINDING AND ORDERING TEXT: natural order, caseless and accentless matching, and a fuzzy match with a
 * score and the positions it matched.
 *
 * ONE COMPARABLE FORM. Text is matched as its code points folded (Unicode's simple case folding) and stripped of combining marks
 * after canonical decomposition, so `cafe` finds `Café` and `STRASSE` finds `straße` only where folding says so. A pattern with an
 * uppercase letter in it matches case as written (smart case); one without matches without case.
 */
#include "rolltui/rolltui.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- natural order ---------------------------------------------------------------------------- */

static int is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* The next code point of `s` at `*i`, folded, its combining marks skipped; 0 at the end. */
static RolltuiCodepoint next_folded(const char* s, size_t n, size_t* i) {
  while (*i < n) {
    RolltuiDecodedChar d;
    rolltui_u_decode_one(s, n, *i, &d);
    *i += d.length;
    {
      RolltuiCodepoint base[4];
      const size_t k = rolltui_u_nfd(&d.cp, 1, base, 4);
      const RolltuiCodepoint b = k ? base[0] : d.cp;
      if (rolltui_u_combining_class(b) != 0 && k <= 1) continue; /* a mark of its own: nothing to compare */
      return rolltui_u_fold(b);
    }
  }
  return 0;
}

int rolltui_natural_compare(const char* a, size_t an, const char* b, size_t bn) {
  size_t i = 0, j = 0;
  int tie = 0; /* what decides when the folded forms are equal: the bytes as written */
  while (i < an && j < bn) {
    if (is_digit((unsigned char)a[i]) && is_digit((unsigned char)b[j])) {
      /* A NUMBER AGAINST A NUMBER compares by value: leading zeros skipped, then the longer run is the larger, then digit by
       * digit. Fewer leading zeros sorts first among equal values, so `07` and `7` keep one order. */
      size_t za = i, zb = j, ea, eb;
      while (za < an && a[za] == '0') ++za;
      while (zb < bn && b[zb] == '0') ++zb;
      ea = za;
      eb = zb;
      while (ea < an && is_digit((unsigned char)a[ea])) ++ea;
      while (eb < bn && is_digit((unsigned char)b[eb])) ++eb;
      if (ea - za != eb - zb) return ea - za < eb - zb ? -1 : 1;
      {
        const int c = memcmp(a + za, b + zb, ea - za);
        if (c) return c < 0 ? -1 : 1;
      }
      if (!tie && za - i != zb - j) tie = za - i < zb - j ? -1 : 1;
      i = ea;
      j = eb;
      continue;
    }
    {
      size_t ni = i, nj = j;
      const RolltuiCodepoint ca = next_folded(a, an, &ni), cb = next_folded(b, bn, &nj);
      if (ca != cb) return ca < cb ? -1 : 1;
      if (!tie) {
        const size_t la = ni - i, lb = nj - j;
        const int c = memcmp(a + i, b + j, la < lb ? la : lb);
        if (c) tie = c < 0 ? -1 : 1;
        else if (la != lb) tie = la < lb ? -1 : 1;
      }
      i = ni;
      j = nj;
    }
  }
  if (i < an) return 1;
  if (j < bn) return -1;
  return tie;
}

/* ---- the fuzzy match ------------------------------------------------------------------------- */

/* The comparable form of `text`, a code point at a time, with the byte each one began at. */
typedef struct Folded {
  RolltuiCodepoint* cp;
  size_t* at;
  unsigned char* upper; /* the code point was uppercase as written */
  size_t n, cap, at_cap, up_cap;
} Folded;

static void fold_into(const char* s, size_t n, int keep_case, Folded* f) {
  size_t i = 0;
  f->n = 0;
  while (i < n) {
    RolltuiDecodedChar d;
    RolltuiCodepoint base[4];
    size_t k;
    RolltuiCodepoint b;
    const size_t start = i;
    rolltui_u_decode_one(s, n, i, &d);
    i += d.length;
    k = rolltui_u_nfd(&d.cp, 1, base, 4);
    b = k ? base[0] : d.cp;
    if (rolltui_u_combining_class(b) != 0 && k <= 1) continue;
    f->cp = (RolltuiCodepoint*)rolltui_grow(f->cp, &f->cap, f->n + 1, sizeof *f->cp);
    f->at = (size_t*)rolltui_grow(f->at, &f->at_cap, f->n + 1, sizeof *f->at);
    f->upper = (unsigned char*)rolltui_grow(f->upper, &f->up_cap, f->n + 1, 1);
    f->upper[f->n] = (unsigned char)(rolltui_u_to_lower(b) != b);
    f->cp[f->n] = keep_case ? b : rolltui_u_fold(b);
    f->at[f->n] = start;
    ++f->n;
  }
}

static void folded_free(Folded* f) {
  rolltui_mem_free(f->cp);
  rolltui_mem_free(f->at);
  rolltui_mem_free(f->upper);
  memset(f, 0, sizeof *f);
}

/* A WORD START earns more: the text's first letter, a letter after a separator, an uppercase letter after a lowercase one. */
static int boundary_bonus(const Folded* t, size_t i) {
  RolltuiCodepoint prev;
  if (i == 0) return 8;
  prev = t->cp[i - 1];
  if (prev == '/' || prev == '\\') return 9;
  if (prev == ' ' || prev == '_' || prev == '-' || prev == '.' || prev == ':') return 8;
  if (t->upper[i] && !t->upper[i - 1]) return 7;
  return 0;
}

int rolltui_fuzzy_match(const char* pattern, size_t pattern_len, const char* text, size_t text_len, int* score,
                        size_t* positions, size_t positions_cap) {
  Folded p, t;
  size_t i, pi = 0, best_end = 0;
  int keep_case = 0, total = 0, run = 0;
  memset(&p, 0, sizeof p);
  memset(&t, 0, sizeof t);
  if (score) *score = 0;
  if (!pattern || pattern_len == 0) return 1;
  for (i = 0; i < pattern_len; ++i)
    if (pattern[i] >= 'A' && pattern[i] <= 'Z') keep_case = 1; /* smart case: an uppercase letter means case matters */
  fold_into(pattern, pattern_len, keep_case, &p);
  if (p.n == 0) { /* nothing left to find once folded (a pattern of marks alone): everything matches */
    folded_free(&p);
    return 1;
  }
  fold_into(text ? text : "", text ? text_len : 0, keep_case, &t);
  if (!keep_case) { /* a non-ASCII uppercase letter in the pattern means case matters too */
    Folded raw;
    memset(&raw, 0, sizeof raw);
    fold_into(pattern, pattern_len, 1, &raw);
    for (i = 0; i < raw.n && !keep_case; ++i) keep_case = raw.upper[i];
    folded_free(&raw);
    if (keep_case) {
      fold_into(pattern, pattern_len, 1, &p);
      fold_into(text ? text : "", text ? text_len : 0, 1, &t);
    }
  }
  /* GREEDY FORWARD, THEN TIGHTENED BACKWARD: the first place the whole pattern fits, then from its end the latest start that
   * still fits, so `abc` in `a_b_abc` lands on the `abc` rather than spread over the whole name. */
  for (i = 0; i < t.n && pi < p.n; ++i)
    if (t.cp[i] == p.cp[pi]) ++pi;
  if (pi < p.n) {
    folded_free(&p);
    folded_free(&t);
    return 0;
  }
  best_end = i; /* one past the last matched */
  {
    size_t k = p.n, j = best_end;
    size_t* hit = (size_t*)rolltui_mem_alloc(p.n * sizeof *hit);
    while (k > 0) {
      --j;
      if (t.cp[j] == p.cp[k - 1]) hit[--k] = j;
    }
    for (k = 0; k < p.n; ++k) {
      const size_t at = hit[k];
      int s = 16 + boundary_bonus(&t, at);
      if (k > 0 && hit[k - 1] + 1 == at) {
        ++run;
        s += 4 * run; /* consecutive letters earn more the longer the run */
      } else {
        run = 0;
        if (k > 0) s -= (int)(at - hit[k - 1] - 1) > 8 ? 8 : (int)(at - hit[k - 1] - 1); /* a gap costs, up to a point */
      }
      total += s;
      if (positions && k < positions_cap) positions[k] = t.at[at];
    }
    total -= (int)(hit[0] > 12 ? 12 : hit[0]); /* a match further in costs a little */
    rolltui_mem_free(hit);
  }
  if (score) *score = total;
  folded_free(&p);
  folded_free(&t);
  return 1;
}
