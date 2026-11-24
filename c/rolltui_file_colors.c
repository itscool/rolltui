/* rolltui/c/rolltui_file_colors.c — a style for a file by its name and type, from LS_COLORS (GNU), LSCOLORS (BSD) and a host's own
 * rules, as `ls` and the file managers colour them. The public half is in rolltui.h's filepicker book. */
#include "rolltui/rolltui.h"

#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_regex.h"
#include "rolltui/c/rolltui_str.h"

/* THE TYPES LS_COLORS NAMES, by its two letters. */
enum { K_NO, K_FI, K_DI, K_LN, K_OR, K_PI, K_SO, K_BD, K_CD, K_EX, K_SU, K_SG, K_TW, K_OW, K_ST, K_COUNT };
static const char kKeys[K_COUNT][3] = {"no", "fi", "di", "ln", "or", "pi", "so", "bd", "cd", "ex", "su", "sg", "tw", "ow", "st"};

typedef struct Rule {
  RolltuiStr pattern; /* a glob, or empty with `re` */
  RolltuiRegex* re;
  unsigned char type;
  RolltuiStyle style;
} Rule;

typedef struct Suffix {
  RolltuiStr text; /* what the name ends with, as LS_COLORS spells it after its `*` */
  RolltuiStyle style;
} Suffix;

struct RolltuiFileColors {
  Rule* rules; /* the host's, first match wins */
  size_t rule_n, rule_cap;
  Suffix* suffixes; /* LS_COLORS's `*.ext` entries, the later winning as `ls` has it */
  size_t suffix_n, suffix_cap;
  RolltuiStyle type[K_COUNT];
  unsigned char has_type[K_COUNT];
};

RolltuiFileColors* rolltui_file_colors_new(void) {
  RolltuiFileColors* fc = (RolltuiFileColors*)rolltui_mem_alloc(sizeof *fc);
  memset(fc, 0, sizeof *fc);
  return fc;
}

void rolltui_file_colors_free(RolltuiFileColors* fc) {
  size_t i;
  if (!fc) return;
  for (i = 0; i < fc->rule_n; ++i) {
    rolltui_str_free(&fc->rules[i].pattern);
    rolltui_regex_free(fc->rules[i].re);
  }
  for (i = 0; i < fc->suffix_n; ++i) rolltui_str_free(&fc->suffixes[i].text);
  rolltui_mem_free(fc->rules);
  rolltui_mem_free(fc->suffixes);
  rolltui_mem_free(fc);
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

size_t rolltui_file_colors_add_gnu(RolltuiFileColors* fc, const char* text, size_t len) {
  size_t at = 0, taken = 0;
  if (!fc || !text) return 0;
  while (at < len) { /* entries `key=sgr` between colons */
    size_t end = at, eq;
    RolltuiStyle st;
    while (end < len && text[end] != ':') ++end;
    for (eq = at; eq < end && text[eq] != '='; ++eq) {
    }
    memset(&st, 0, sizeof st);
    if (eq < end && eq > at && rolltui_style_from_sgr(text + eq + 1, end - eq - 1, &st)) {
      if (text[at] == '*' && eq - at > 1) {
        Suffix* s;
        fc->suffixes = (Suffix*)rolltui_grow_zeroed(fc->suffixes, &fc->suffix_cap, fc->suffix_n + 1, sizeof *fc->suffixes);
        s = &fc->suffixes[fc->suffix_n++];
        rolltui_str_set(&s->text, text + at + 1, eq - at - 1);
        s->style = st;
        ++taken;
      } else if (eq - at == 2) {
        int k;
        for (k = 0; k < K_COUNT; ++k)
          if (text[at] == kKeys[k][0] && text[at + 1] == kKeys[k][1]) {
            fc->type[k] = st;
            fc->has_type[k] = 1;
            ++taken;
            break;
          }
      }
    }
    at = end + 1;
  }
  return taken;
}

size_t rolltui_file_colors_add_bsd(RolltuiFileColors* fc, const char* text, size_t len) {
  /* eleven pairs of letters, foreground then background, for these types in this order */
  static const int order[11] = {K_DI, K_LN, K_SO, K_PI, K_EX, K_BD, K_CD, K_SU, K_SG, K_TW, K_OW};
  size_t i, taken = 0;
  if (!fc || !text) return 0;
  for (i = 0; i < 11 && 2 * i + 1 < len; ++i) {
    const char f = text[2 * i], b = text[2 * i + 1];
    RolltuiStyle st;
    memset(&st, 0, sizeof st);
    if (f >= 'a' && f <= 'h') st.fg.kind = 1, st.fg.index = (unsigned char)(f - 'a');
    else if (f >= 'A' && f <= 'H') st.fg.kind = 1, st.fg.index = (unsigned char)(f - 'A'), st.bold = 1;
    else if (f != 'x') continue; /* not a letter LSCOLORS knows: the rest is not trusted either */
    if (b >= 'a' && b <= 'h') st.bg.kind = 1, st.bg.index = (unsigned char)(b - 'a');
    else if (b >= 'A' && b <= 'H') st.bg.kind = 1, st.bg.index = (unsigned char)(b - 'A' + 8);
    else if (b != 'x') continue;
    if (f == 'x' && b == 'x') continue; /* the default on both: nothing to say */
    fc->type[order[i]] = st;
    fc->has_type[order[i]] = 1;
    ++taken;
  }
  return taken;
}

size_t rolltui_file_colors_add_env(RolltuiFileColors* fc) {
  const char* gnu = getenv("LS_COLORS");
  const char* bsd = getenv("LSCOLORS");
  if (gnu && *gnu) return rolltui_file_colors_add_gnu(fc, gnu, strlen(gnu));
  if (bsd && *bsd) return rolltui_file_colors_add_bsd(fc, bsd, strlen(bsd));
  return 0;
}

int rolltui_file_colors_add_rule(RolltuiFileColors* fc, const char* pattern, size_t len, unsigned char type, RolltuiStyle style) {
  Rule* r;
  RolltuiRegex* re = NULL;
  if (!fc || !pattern || len == 0 || type > ROLLTUI_FILE_EXEC) return 0;
  if (len > 3 && memcmp(pattern, "re:", 3) == 0) {
    re = rolltui_regex_compile(pattern + 3, len - 3, 1, NULL);
    if (!re) return 0;
  }
  fc->rules = (Rule*)rolltui_grow_zeroed(fc->rules, &fc->rule_cap, fc->rule_n + 1, sizeof *fc->rules);
  r = &fc->rules[fc->rule_n++];
  r->re = re;
  if (!re) rolltui_str_set(&r->pattern, pattern, len);
  r->type = type;
  r->style = style;
  return 1;
}

/* A GLOB over a whole name, case aside: `*` any run, `?` one byte, `[abc]` `[a-z]` `[!x]` one of a set or not. */
static int glob(const char* p, size_t pn, const char* s, size_t sn) {
  size_t pi = 0, si = 0, star_p = (size_t)-1, star_s = 0;
  while (si < sn) {
    if (pi < pn && p[pi] == '*') {
      star_p = ++pi;
      star_s = si;
      continue;
    }
    if (pi < pn && p[pi] == '[') {
      size_t q = pi + 1;
      int neg = 0, hit = 0;
      if (q < pn && (p[q] == '!' || p[q] == '^')) neg = 1, ++q;
      while (q < pn && p[q] != ']') {
        if (q + 2 < pn && p[q + 1] == '-' && p[q + 2] != ']') {
          if (lower(s[si]) >= lower(p[q]) && lower(s[si]) <= lower(p[q + 2])) hit = 1;
          q += 3;
        } else {
          if (lower(s[si]) == lower(p[q])) hit = 1;
          ++q;
        }
      }
      if (q < pn && hit != neg) {
        pi = q + 1;
        ++si;
        continue;
      }
    } else if (pi < pn && (p[pi] == '?' || lower(p[pi]) == lower(s[si]))) {
      ++pi;
      ++si;
      continue;
    }
    if (star_p == (size_t)-1) return 0;
    pi = star_p;
    si = ++star_s;
  }
  while (pi < pn && p[pi] == '*') ++pi;
  return pi == pn;
}

/* A REGULAR EXPRESSION anywhere in the name, `^` and `$` to pin it. */
static int re_search(const RolltuiRegex* re, const char* s, size_t n) {
  size_t at;
  for (at = 0; at <= n; ++at)
    if (rolltui_regex_match_at(re, s, n, at, NULL, NULL)) return 1;
  return 0;
}

#define IFMT 0170000
#define IFDIR 0040000
#define IFREG 0100000
#define IFIFO 0010000
#define IFSOCK 0140000
#define IFBLK 0060000
#define IFCHR 0020000

/* WHAT LS_COLORS CALLS IT, by `ls`'s own order: a broken link, a link, then the type, and for a plain file its special bits. */
static int kind_of(unsigned int mode, int link) {
  if (link == 2) return K_OR;
  if (link) return K_LN;
  switch (mode & IFMT) {
    case IFDIR:
      if ((mode & 01002) == 01002) return K_TW;
      if (mode & 0002) return K_OW;
      if (mode & 01000) return K_ST;
      return K_DI;
    case IFIFO: return K_PI;
    case IFSOCK: return K_SO;
    case IFBLK: return K_BD;
    case IFCHR: return K_CD;
    default: break;
  }
  if (mode & 04000) return K_SU;
  if (mode & 02000) return K_SG;
  if (mode & 0111) return K_EX;
  return K_FI;
}

static int rule_type_matches(unsigned char type, int kind) {
  switch (type) {
    case ROLLTUI_FILE_ANY: return 1;
    case ROLLTUI_FILE_REGULAR: return kind == K_FI;
    case ROLLTUI_FILE_DIR: return kind == K_DI || kind == K_TW || kind == K_OW || kind == K_ST;
    case ROLLTUI_FILE_LINK: return kind == K_LN || kind == K_OR;
    case ROLLTUI_FILE_EXEC: return kind == K_EX || kind == K_SU || kind == K_SG;
    default: return 0;
  }
}

int rolltui_file_colors_match(const RolltuiFileColors* fc, const char* name, size_t len, unsigned int mode, int link, RolltuiStyle* out) {
  int kind;
  size_t i;
  if (!fc || !name || !out) return 0;
  kind = kind_of(mode, link);
  for (i = 0; i < fc->rule_n; ++i) { /* the host's rules first, in order */
    const Rule* r = &fc->rules[i];
    int hit;
    if (!rule_type_matches(r->type, kind)) continue;
    hit = r->re ? re_search(r->re, name, len) : glob(r->pattern.p, r->pattern.n, name, len);
    if (hit) {
      rolltui_style_patch(out, &r->style);
      return 1;
    }
  }
  /* A broken link said nothing of itself is a link; a plain file is its suffix's before it is a file */
  if (kind == K_OR && !fc->has_type[K_OR]) kind = K_LN;
  if (kind == K_FI)
    for (i = fc->suffix_n; i-- > 0;) {
      const Suffix* s = &fc->suffixes[i];
      size_t k;
      if (s->text.n > len) continue;
      for (k = 0; k < s->text.n && lower(name[len - s->text.n + k]) == lower(s->text.p[k]); ++k) {
      }
      if (k == s->text.n) {
        rolltui_style_patch(out, &s->style);
        return 1;
      }
    }
  if (fc->has_type[kind]) {
    rolltui_style_patch(out, &fc->type[kind]);
    return 1;
  }
  if (kind == K_FI && fc->has_type[K_NO]) {
    rolltui_style_patch(out, &fc->type[K_NO]);
    return 1;
  }
  return 0;
}
