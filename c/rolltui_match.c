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
#include "rolltui/c/rolltui_regex.h"
#include "rolltui/c/rolltui_str.h"

/* ---- natural order ---------------------------------------------------------------------------- */

static int is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* The next code point of `s` at `*i`, folded, its combining marks skipped; 0 at the end. */
static RolltuiCodepoint next_folded(const char* s, size_t n, size_t* i) {
  while (*i < n) {
    RolltuiDecodedChar d;
    const unsigned char c = (unsigned char)s[*i];
    if (c < 0x80) { /* ASCII folds to its lowercase and has no marks: no decode, no decomposition (a sort's names are mostly this) */
      ++*i;
      return c >= 'A' && c <= 'Z' ? (RolltuiCodepoint)(c + ('a' - 'A')) : (RolltuiCodepoint)c;
    }
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
      const unsigned char x = (unsigned char)a[i], y = (unsigned char)b[j];
      if (x < 0x80 && y < 0x80 && (x == y || ((x | 0x20) == (y | 0x20) && (unsigned)((x | 0x20) - 'a') < 26u))) { /* the same ASCII letter */
        if (!tie && x != y) tie = x < y ? -1 : 1;
        ++i;
        ++j;
        continue;
      }
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

/* ---- finding text, and the filter every list, menu and palette reads ----------------------------- */

/* Whether case matters for `s`: it has an uppercase letter (smart case). */
static int smart_case(const char* s, size_t n) {
  Folded raw;
  size_t i;
  int up = 0;
  for (i = 0; i < n; ++i)
    if (s[i] >= 'A' && s[i] <= 'Z') return 1;
  memset(&raw, 0, sizeof raw);
  fold_into(s, n, 1, &raw);
  for (i = 0; i < raw.n && !up; ++i) up = raw.upper[i];
  folded_free(&raw);
  return up;
}

/* The byte a folded code point's run ends at: where the next begins, or the end. */
static size_t folded_end(const Folded* f, size_t i, size_t n) { return i < f->n ? f->at[i] : n; }

/* `p` in `h` from folded index `from`: the folded index it starts at, or (size_t)-1. */
static size_t folded_find(const Folded* h, const Folded* p, size_t from) {
  size_t i, k;
  if (p->n == 0) return from <= h->n ? from : (size_t)-1;
  for (i = from; i + p->n <= h->n; ++i) {
    for (k = 0; k < p->n; ++k)
      if (h->cp[i + k] != p->cp[k]) break;
    if (k == p->n) return i;
  }
  return (size_t)-1;
}

int rolltui_text_find(const char* hay, size_t hn, const char* needle, size_t nn, size_t from, size_t* at, size_t* len) {
  Folded h, p;
  size_t i, start = 0;
  int found = 0;
  const int keep = smart_case(needle ? needle : "", needle ? nn : 0);
  if (at) *at = 0;
  if (len) *len = 0;
  if (!needle || nn == 0) return 0;
  memset(&h, 0, sizeof h);
  memset(&p, 0, sizeof p);
  fold_into(needle, nn, keep, &p);
  fold_into(hay ? hay : "", hay ? hn : 0, keep, &h);
  while (start < h.n && h.at[start] < from) ++start;
  i = p.n ? folded_find(&h, &p, start) : (size_t)-1;
  if (i != (size_t)-1) {
    found = 1;
    if (at) *at = h.at[i];
    if (len) *len = folded_end(&h, i + p.n, hn) - h.at[i];
  }
  folded_free(&h);
  folded_free(&p);
  return found;
}

size_t rolltui_text_find_each(const char* hay, size_t hn, const char* needle, size_t nn, void (*fn)(void* ctx, size_t at, size_t len),
                              void* ctx) {
  Folded h, p;
  size_t i = 0, found = 0;
  const int keep = smart_case(needle ? needle : "", needle ? nn : 0);
  if (!needle || nn == 0) return 0;
  memset(&h, 0, sizeof h);
  memset(&p, 0, sizeof p);
  fold_into(needle, nn, keep, &p);
  fold_into(hay ? hay : "", hay ? hn : 0, keep, &h);
  while (p.n && (i = folded_find(&h, &p, i)) != (size_t)-1) {
    const size_t at = h.at[i];
    ++found;
    if (fn) fn(ctx, at, folded_end(&h, i + p.n, hn) - at);
    i += p.n; /* matches do not overlap */
  }
  folded_free(&h);
  folded_free(&p);
  return found;
}

#define FT_FUZZY 0
#define FT_EXACT 1
#define FT_PREFIX 2
#define FT_SUFFIX 3
#define FT_REGEX 4

typedef struct FilterTerm {
  unsigned char kind, neg;
  RolltuiStr text;
  RolltuiRegex* re; /* OWNED, FT_REGEX */
} FilterTerm;

struct RolltuiFilter {
  FilterTerm* t;
  size_t n, cap;
};

RolltuiFilter* rolltui_filter_new(const char* q, size_t len) {
  RolltuiFilter* f = (RolltuiFilter*)rolltui_mem_alloc(sizeof *f);
  size_t i = 0;
  memset(f, 0, sizeof *f);
  if (!q) len = 0;
  while (i < len) {
    size_t e, b;
    FilterTerm* t;
    size_t e0;
    while (i < len && q[i] == ' ') ++i;
    if (i >= len) break;
    e = i;
    while (e < len && q[e] != ' ') ++e;
    e0 = e;
    b = i;
    f->t = (FilterTerm*)rolltui_grow(f->t, &f->cap, f->n + 1, sizeof *f->t);
    t = &f->t[f->n];
    memset(t, 0, sizeof *t);
    if (q[b] == '!' && b + 1 < e) t->neg = 1, ++b;
    if (q[b] == '\'' && b + 1 < e) {
      t->kind = FT_EXACT, ++b;
    } else if (q[b] == '^' && b + 1 < e) {
      t->kind = FT_PREFIX, ++b;
    } else if (e - b > 1 && q[e - 1] == '$') {
      t->kind = FT_SUFFIX, --e;
    } else if (e - b > 2 && q[b] == '/' && q[e - 1] == '/') {
      t->re = rolltui_regex_compile(q + b + 1, e - b - 2, !smart_case(q + b + 1, e - b - 2), NULL);
      if (t->re) t->kind = FT_REGEX, ++b, --e;
      else t->kind = FT_EXACT; /* not a pattern this matcher takes: the text as written */
    } else {
      t->kind = t->neg ? FT_EXACT : FT_FUZZY; /* `!term` is "not as written": a fuzzy "not" would exclude nearly everything */
    }
    rolltui_str_set(&t->text, q + b, e - b);
    ++f->n;
    i = e0;
  }
  return f;
}

void rolltui_filter_free(RolltuiFilter* f) {
  size_t i;
  if (!f) return;
  for (i = 0; i < f->n; ++i) {
    rolltui_str_free(&f->t[i].text);
    rolltui_regex_free(f->t[i].re);
  }
  rolltui_mem_free(f->t);
  rolltui_mem_free(f);
}

size_t rolltui_filter_terms(const RolltuiFilter* f) { return f ? f->n : 0; }
int rolltui_filter_term_negated(const RolltuiFilter* f, size_t i) { return f && i < f->n && f->t[i].neg; }

/* Where term `t` matches `text` (its first place), as a byte range; 1 when it does. A fuzzy term's range is its first letter to
 * its last. */
static int term_hit(const FilterTerm* t, const char* text, size_t n, int* score, size_t* at, size_t* len) {
  const char* p = t->text.p ? t->text.p : "";
  *score = 0, *at = 0, *len = 0;
  switch (t->kind) {
    case FT_FUZZY: {
      size_t pos[2];
      return rolltui_fuzzy_match(p, t->text.n, text, n, score, pos, 1);
    }
    case FT_EXACT:
      *score = 40;
      return rolltui_text_find(text, n, p, t->text.n, 0, at, len);
    case FT_PREFIX:
      *score = 60;
      return rolltui_text_find(text, n, p, t->text.n, 0, at, len) && *at == 0;
    case FT_SUFFIX: {
      size_t from = 0, a = 0, l = 0;
      int any = 0;
      while (rolltui_text_find(text, n, p, t->text.n, from, &a, &l)) {
        any = 1, *at = a, *len = l;
        from = a + 1;
        if (from > n) break;
      }
      *score = 50;
      return any && *at + *len == n;
    }
    case FT_REGEX: {
      size_t i;
      RolltuiRegexMatch m;
      for (i = 0; i <= n; ++i)
        if (rolltui_regex_match_at(t->re, text, n, i, NULL, &m)) {
          *at = (size_t)m.start[0];
          *len = (size_t)(m.end[0] - m.start[0]);
          *score = 40;
          return 1;
        }
      return 0;
    }
    default: return 0;
  }
}

int rolltui_filter_term(const RolltuiFilter* f, size_t i, const char* text, size_t n, int* score) {
  size_t at, len;
  int s = 0, hit;
  if (!f || i >= f->n) return 0;
  hit = term_hit(&f->t[i], text ? text : "", text ? n : 0, &s, &at, &len);
  if (score) *score = hit ? s : 0;
  return hit;
}

int rolltui_filter_match(const RolltuiFilter* f, const char* text, size_t n, int* score) {
  size_t i;
  int total = 0;
  if (score) *score = 0;
  if (!f) return 1;
  for (i = 0; i < f->n; ++i) {
    int s = 0;
    const int hit = rolltui_filter_term(f, i, text, n, &s);
    if (f->t[i].neg ? hit : !hit) return 0;
    if (!f->t[i].neg) total += s;
  }
  if (score) *score = total;
  return 1;
}

size_t rolltui_filter_spans(const RolltuiFilter* f, const char* text, size_t n, size_t* begin, size_t* end, size_t cap) {
  size_t i, out = 0;
  if (!f || !text) return 0;
  for (i = 0; i < f->n && out < cap; ++i) {
    const FilterTerm* t = &f->t[i];
    if (t->neg) continue;
    if (t->kind == FT_FUZZY) {
      size_t pos[64], k;
      int s = 0;
      for (k = 0; k < 64; ++k) pos[k] = (size_t)-1; /* as many as the term has letters are filled */
      if (!rolltui_fuzzy_match(t->text.p ? t->text.p : "", t->text.n, text, n, &s, pos, 64)) continue;
      for (k = 0; k < 64 && out < cap; ++k) {
        RolltuiDecodedChar d;
        if (pos[k] >= n) break;
        rolltui_u_decode_one(text, n, pos[k], &d);
        begin[out] = pos[k];
        end[out] = pos[k] + (d.length ? d.length : 1);
        ++out;
      }
    } else {
      size_t at, len;
      int s;
      if (term_hit(t, text, n, &s, &at, &len) && len) {
        begin[out] = at;
        end[out] = at + len;
        ++out;
      }
    }
  }
  return out;
}
