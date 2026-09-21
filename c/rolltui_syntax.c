/* rolltui/c/rolltui_syntax.c — languages as data, and the engine that runs them. The contract is in the header. */
#include "rolltui/c/rolltui_syntax.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_regex.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_termfacts.h"

#define MAX_LANGS 512
#define MAX_CTX_PER_LANG 256
#define MAX_RULES 2000
#define MAX_WORDS 8000
#define MAX_FRAMES 48
#define MAX_LINE 4000     /* a line is coloured this far and plain after it */
#define MAX_FILE 262144   /* a language file bigger than this is not one */
#define REF_BYTES 24      /* how much of what a begin captured a \1 can stand for */

/* ---- classes ------------------------------------------------------------------------------------------------ */
static const char* const kClassNames[ROLLTUI_SYN_COUNT] = {"plain",    "keyword",  "type",     "function", "string",  "escape",
                                                           "number",   "constant", "comment",  "doc",      "operator", "punct",
                                                           "preproc",  "variable", "attribute", "tag",     "property", "section"};

const char* rolltui_syntax_class_name(unsigned char cls) { return cls < ROLLTUI_SYN_COUNT ? kClassNames[cls] : NULL; }

static int class_from_name(const char* s, size_t n) {
  int i;
  for (i = 0; i < ROLLTUI_SYN_COUNT; ++i)
    if (strlen(kClassNames[i]) == n && memcmp(kClassNames[i], s, n) == 0) return i;
  return -1;
}

/* A class is drawn in an EXISTING theme role, so no theme has to change for a language to be coloured. The table is the one
 * place to edit if a theme ever names roles for syntax. */
unsigned char rolltui_syntax_role(unsigned char cls, unsigned char base) {
  switch (cls) {
    case ROLLTUI_SYN_KEYWORD:
    case ROLLTUI_SYN_PREPROC:
    case ROLLTUI_SYN_TAG: return ROLLTUI_ROLE_ACCENT_4;
    case ROLLTUI_SYN_TYPE:
    case ROLLTUI_SYN_ATTRIBUTE:
    case ROLLTUI_SYN_NUMBER:
    case ROLLTUI_SYN_CONSTANT: return ROLLTUI_ROLE_ACCENT_3;
    case ROLLTUI_SYN_FUNCTION:
    case ROLLTUI_SYN_VARIABLE:
    case ROLLTUI_SYN_ESCAPE:
    case ROLLTUI_SYN_PROPERTY: return ROLLTUI_ROLE_ACCENT_1;
    case ROLLTUI_SYN_STRING: return ROLLTUI_ROLE_ACCENT_2;
    case ROLLTUI_SYN_COMMENT: return ROLLTUI_ROLE_TEXT_MUTED;
    case ROLLTUI_SYN_DOC: return ROLLTUI_ROLE_NOTE;
    case ROLLTUI_SYN_PUNCT: return ROLLTUI_ROLE_TEXT_MUTED; /* (not `label`: a label's style carries a background of its own) */
    case ROLLTUI_SYN_SECTION: return ROLLTUI_ROLE_MD_HEADING;
    default: return base;
  }
}

unsigned char rolltui_syntax_flags(unsigned char cls) {
  switch (cls) {
    case ROLLTUI_SYN_KEYWORD:
    case ROLLTUI_SYN_PREPROC: return ROLLTUI_SYN_BOLD;
    case ROLLTUI_SYN_COMMENT:
    case ROLLTUI_SYN_DOC:
    case ROLLTUI_SYN_ATTRIBUTE: return ROLLTUI_SYN_ITALIC;
    default: return 0;
  }
}

/* the two attribute bits ride in the top of a role byte through the Markdown seam, so a Role must fit under them */
ROLLTUI_STATIC_ASSERT(ROLLTUI_ROLE_COUNT <= ROLLTUI_MD_ROLE_MASK, "a Role no longer fits under the seam's two attribute bits");

/* ---- the compiled forms -------------------------------------------------------------------------------------- */
enum { A_NONE, A_PUSH, A_POP, A_SET };

typedef struct Rule {
  RolltuiRegex* re;             /* NULL for a words rule or an include */
  char** words;                 /* a words rule: sorted; lower case when the language ignores case */
  size_t nwords;
  unsigned char cls;
  unsigned char cap_cls[10];    /* the class of each capture group; 0xFF: none */
  unsigned char has_caps;
  unsigned char action;
  unsigned char is_include;
  unsigned char icase;
  int target;                   /* push / set: the context; include: the context, or -1 to resolve by name at link time */
  char* inc_lang;               /* include: another language's name (NULL: this one's) */
  char* inc_ctx;                /* include: a context's name (NULL: main) */
  char** not_after;             /* a word after one of these is a member, not a keyword */
  size_t n_not_after;
  unsigned char wc[32];         /* words rule: the language's extra word bytes (`-` in a shell) */
} Rule;

typedef struct Ctx {
  char* name;
  int lang;
  Rule* rules;
  size_t nrules, rules_cap;
  Rule** flat;                  /* after link: the rules with the includes spliced in */
  size_t nflat;
  unsigned char default_cls;
  unsigned char eol_pop;        /* a region that ends at the end of its line if nothing ended it sooner */
  unsigned char start[32];      /* the bytes any rule here could begin with */
  unsigned char wc[32];         /* the language's extra word bytes, for the words no rule takes */
} Ctx;

typedef struct Lang {
  char* name;
  char** aliases; size_t naliases;
  char** exts; size_t nexts;
  char** files; size_t nfiles;
  RolltuiRegex* first_line;
  int main_ctx;
  int icase;
  int active;
  char** not_after; size_t n_not_after;
} Lang;

struct RolltuiSyntax {
  Lang* langs;
  size_t nlangs, langs_cap;
  Ctx* ctxs;
  size_t nctxs, ctxs_cap;
  int dirty;
};

static char* dup_n(const char* s, size_t n) {
  char* p = (char*)rolltui_mem_alloc(n + 1);
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

static int is_word_byte(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

/* a word byte: a letter, digit, _, a byte of a multi-byte letter, or one the language adds (`word_chars`) */
static int word_byte(const unsigned char* wc, unsigned char c) { return is_word_byte(c) || ((wc[c >> 3] >> (c & 7)) & 1); }

static void free_strings(char** v, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) rolltui_mem_free(v[i]);
  rolltui_mem_free(v);
}

static void rule_release(Rule* r) {
  rolltui_regex_free(r->re);
  free_strings(r->words, r->nwords);
  rolltui_mem_free(r->inc_lang);
  rolltui_mem_free(r->inc_ctx);
  free_strings(r->not_after, r->n_not_after);
  memset(r, 0, sizeof *r);
}

static void ctx_release(Ctx* c) {
  size_t i;
  for (i = 0; i < c->nrules; ++i) rule_release(&c->rules[i]);
  rolltui_mem_free(c->rules);
  rolltui_mem_free(c->flat);
  rolltui_mem_free(c->name);
  memset(c, 0, sizeof *c);
}

static void lang_release(Lang* l) {
  free_strings(l->aliases, l->naliases);
  free_strings(l->exts, l->nexts);
  free_strings(l->files, l->nfiles);
  free_strings(l->not_after, l->n_not_after);
  rolltui_regex_free(l->first_line);
  rolltui_mem_free(l->name);
  memset(l, 0, sizeof *l);
}

RolltuiSyntax* rolltui_syntax_new(void) {
  RolltuiSyntax* s = (RolltuiSyntax*)rolltui_mem_alloc(sizeof *s);
  memset(s, 0, sizeof *s);
  return s;
}

void rolltui_syntax_free(RolltuiSyntax* s) {
  size_t i;
  if (!s) return;
  for (i = 0; i < s->nctxs; ++i) ctx_release(&s->ctxs[i]);
  for (i = 0; i < s->nlangs; ++i) lang_release(&s->langs[i]);
  rolltui_mem_free(s->ctxs);
  rolltui_mem_free(s->langs);
  rolltui_mem_free(s);
}

/* ---- reading a language file -------------------------------------------------------------------------------------- */
typedef struct Cx {
  RolltuiSyntax* s;
  int lang;               /* the index this language will have */
  int icase;
  char** not_after;       /* borrowed from the language being built */
  size_t n_not_after;
  unsigned char wc[32];   /* `word_chars`: bytes besides letters, digits and _ that a word may hold */
  RolltuiStr* err;
  const char* lang_name;
  int failed;
  int anon;
} Cx;

static void say(Cx* cx, const char* where, const char* what, const char* detail) {
  if (cx->failed) return;
  cx->failed = 1;
  if (!cx->err) return;
  rolltui_str_set(cx->err, cx->lang_name ? cx->lang_name : "language", strlen(cx->lang_name ? cx->lang_name : "language"));
  rolltui_str_append(cx->err, ": ", 2);
  if (where && *where) { rolltui_str_append(cx->err, where, strlen(where)); rolltui_str_append(cx->err, ": ", 2); }
  rolltui_str_append(cx->err, what, strlen(what));
  if (detail && *detail) { rolltui_str_append(cx->err, " (", 2); rolltui_str_append(cx->err, detail, strlen(detail)); rolltui_str_append(cx->err, ")", 1); }
}

static int add_ctx(Cx* cx, const char* name, size_t n) {
  RolltuiSyntax* s = cx->s;
  Ctx* c;
  if (s->nctxs - 0 >= 100000) { say(cx, "", "too many contexts", NULL); return -1; }
  s->ctxs = (Ctx*)rolltui_grow(s->ctxs, &s->ctxs_cap, s->nctxs + 1, sizeof *s->ctxs);
  c = &s->ctxs[s->nctxs];
  memset(c, 0, sizeof *c);
  c->name = dup_n(name, n);
  c->lang = cx->lang;
  memcpy(c->wc, cx->wc, sizeof c->wc);
  return (int)s->nctxs++;
}

static int add_rule(Cx* cx, int ctx, const Rule* r) {
  Ctx* c = &cx->s->ctxs[ctx];
  if (c->nrules >= MAX_RULES) { say(cx, c->name, "too many rules in one context", NULL); return 0; }
  c->rules = (Rule*)rolltui_grow(c->rules, &c->rules_cap, c->nrules + 1, sizeof *c->rules);
  c->rules[c->nrules++] = *r;
  return 1;
}

static void rule_init(Rule* r) {
  memset(r, 0, sizeof *r);
  memset(r->cap_cls, 0xFF, sizeof r->cap_cls);
  r->target = -1;
}

static const RolltuiJsonValue* jget(const RolltuiJsonValue* v, const char* key) { return rolltui_json_get(v, key, strlen(key)); }
static int jstr(const RolltuiJsonValue* v, const char** s, size_t* n) {
  if (!rolltui_json_is_string(v)) return 0;
  *s = rolltui_json_as_string(v, "", 0, n);
  return 1;
}

/* a context in this language by name, or -1 */
static int find_ctx(Cx* cx, const char* name, size_t n) {
  size_t i;
  for (i = 0; i < cx->s->nctxs; ++i) {
    const Ctx* c = &cx->s->ctxs[i];
    if (c->lang == cx->lang && c->name && strlen(c->name) == n && memcmp(c->name, name, n) == 0) return (int)i;
  }
  return -1;
}

static RolltuiRegex* compile_re(Cx* cx, const char* where, const char* pat, size_t n) {
  RolltuiStr why;
  RolltuiRegex* re;
  memset(&why, 0, sizeof why);
  re = rolltui_regex_compile(pat, n, cx->icase, &why);
  if (!re) {
    char shown[80];
    const size_t k = n < 60 ? n : 60;
    memcpy(shown, pat, k);
    shown[k] = 0;
    say(cx, where, why.p ? why.p : "a pattern this matcher does not take", shown);
  }
  rolltui_str_free(&why);
  return re;
}

static int class_of(Cx* cx, const char* where, const RolltuiJsonValue* v, int dflt) {
  const char* s;
  size_t n;
  int c;
  if (!v || rolltui_json_is_null(v)) return dflt;
  if (!jstr(v, &s, &n)) { say(cx, where, "a class must be a string", NULL); return -1; }
  c = class_from_name(s, n);
  if (c < 0) { char shown[40]; const size_t k = n < 30 ? n : 30; memcpy(shown, s, k); shown[k] = 0; say(cx, where, "there is no such class", shown); }
  return c;
}

static int cmp_word(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

static int read_string_list(Cx* cx, const char* where, const RolltuiJsonValue* arr, char*** out, size_t* nout, int lower) {
  size_t i, n;
  if (!arr || rolltui_json_is_null(arr)) return 1;
  if (!rolltui_json_is_array(arr)) { say(cx, where, "must be a list of strings", NULL); return 0; }
  n = rolltui_json_array_size(arr);
  if (n > MAX_WORDS) { say(cx, where, "too many entries", NULL); return 0; }
  *out = (char**)rolltui_mem_alloc((n + 1) * sizeof **out);
  *nout = 0;
  for (i = 0; i < n; ++i) {
    const char* s;
    size_t sn, k;
    char* d;
    if (!jstr(rolltui_json_array_at(arr, i), &s, &sn) || sn == 0 || sn > 200) { say(cx, where, "an entry is not a string of a sensible length", NULL); return 0; }
    d = dup_n(s, sn);
    if (lower) for (k = 0; k < sn; ++k) if (d[k] >= 'A' && d[k] <= 'Z') d[k] = (char)(d[k] + 32);
    (*out)[(*nout)++] = d;
  }
  return 1;
}

static int compile_rules(Cx* cx, int ctx, const RolltuiJsonValue* arr, const char* where);

static int compile_rule(Cx* cx, int ctx, const RolltuiJsonValue* jr, const char* where) {
  Rule r;
  const RolltuiJsonValue* v;
  const char* s;
  size_t n;
  rule_init(&r);
  r.icase = (unsigned char)cx->icase;
  if (!rolltui_json_is_object(jr)) { say(cx, where, "a rule must be an object", NULL); return 0; }

  /* ---- a region: begin to end ------------------------------------------------------------------------------------ */
  if ((v = jget(jr, "region")) && rolltui_json_is_object(v)) {
    const RolltuiJsonValue* jb = jget(v, "begin");
    const RolltuiJsonValue* je = jget(v, "end");
    const RolltuiJsonValue* jx = jget(v, "escape");
    const RolltuiJsonValue* jn = jget(v, "rules");
    const int cls = class_of(cx, where, jget(v, "class"), ROLLTUI_SYN_STRING);
    int bcls, ecls, xcls, inner;
    int single = rolltui_json_as_bool(jget(v, "single_line"), 0);
    Rule b, e, x;
    if (cls < 0) return 0;
    bcls = class_of(cx, where, jget(v, "begin_class"), cls);
    ecls = class_of(cx, where, jget(v, "end_class"), cls);
    xcls = class_of(cx, where, jget(v, "escape_class"), ROLLTUI_SYN_ESCAPE);
    if (bcls < 0 || ecls < 0 || xcls < 0) return 0;
    if (!jstr(jb, &s, &n) || n == 0) { say(cx, where, "a region needs a begin pattern", NULL); return 0; }
    rule_init(&b);
    b.icase = (unsigned char)cx->icase;
    b.re = compile_re(cx, where, s, n);
    if (!b.re) return 0;
    b.cls = (unsigned char)bcls;
    inner = add_ctx(cx, "(region)", 8);
    if (inner < 0) { rule_release(&b); return 0; }
    cx->s->ctxs[inner].default_cls = (unsigned char)cls;
    if (!je || rolltui_json_is_null(je)) single = 1;
    cx->s->ctxs[inner].eol_pop = (unsigned char)(single ? 1 : 0);
    /* the order matters: an escape before the end, so `\"` is not the end of a string */
    if (jx && !rolltui_json_is_null(jx)) {
      rule_init(&x);
      x.icase = (unsigned char)cx->icase;
      if (!jstr(jx, &s, &n) || !(x.re = compile_re(cx, where, s, n))) { rule_release(&b); if (!cx->failed) say(cx, where, "escape must be a pattern", NULL); return 0; }
      x.cls = (unsigned char)xcls;
      if (!add_rule(cx, inner, &x)) { rule_release(&x); rule_release(&b); return 0; }
    }
    if (jn && rolltui_json_is_array(jn)) {
      if (!compile_rules(cx, inner, jn, where)) { rule_release(&b); return 0; }
    }
    if (je && !rolltui_json_is_null(je)) {
      rule_init(&e);
      e.icase = (unsigned char)cx->icase;
      if (!jstr(je, &s, &n) || !(e.re = compile_re(cx, where, s, n))) { rule_release(&b); if (!cx->failed) say(cx, where, "end must be a pattern", NULL); return 0; }
      e.cls = (unsigned char)ecls;
      e.action = A_POP;
      if (!add_rule(cx, inner, &e)) { rule_release(&e); rule_release(&b); return 0; }
    }
    b.action = A_PUSH;
    b.target = inner;
    if (!add_rule(cx, ctx, &b)) { rule_release(&b); return 0; }
    return 1;
  }

  /* ---- an include ------------------------------------------------------------------------------------------------ */
  if ((v = jget(jr, "include")) && !rolltui_json_is_null(v)) {
    if (!jstr(v, &s, &n) || n == 0) { say(cx, where, "include must name a context", NULL); return 0; }
    r.is_include = 1;
    if (n > 5 && memcmp(s, "lang:", 5) == 0) {
      const char* slash = memchr(s + 5, '/', n - 5);
      const size_t ln = slash ? (size_t)(slash - (s + 5)) : n - 5;
      r.inc_lang = dup_n(s + 5, ln);
      if (slash) r.inc_ctx = dup_n(slash + 1, n - 5 - ln - 1);
    } else {
      r.target = find_ctx(cx, s, n);
      if (r.target < 0) { say(cx, where, "there is no such context to include", s); return 0; }
    }
    if (!add_rule(cx, ctx, &r)) { rule_release(&r); return 0; }
    return 1;
  }

  /* ---- whole words --------------------------------------------------------------------------------------------------- */
  if ((v = jget(jr, "words")) && !rolltui_json_is_null(v)) {
    const int cls = class_of(cx, where, jget(jr, "class"), -1);
    size_t i, w;
    if (cls < 0) { if (!cx->failed) say(cx, where, "a words rule needs a class", NULL); return 0; }
    r.cls = (unsigned char)cls;
    memcpy(r.wc, cx->wc, sizeof r.wc);
    if (!read_string_list(cx, where, v, &r.words, &r.nwords, cx->icase)) { rule_release(&r); return 0; }
    for (i = 0; i < r.nwords; ++i)
      for (w = 0; r.words[i][w]; ++w) {
        const unsigned char c = (unsigned char)r.words[i][w];
        if (!word_byte(cx->wc, c)) {
          say(cx, where, "a word may hold only letters, digits, _ and the language's word_chars — anything else can never match a whole word", r.words[i]);
          rule_release(&r);
          return 0;
        }
      }
    qsort(r.words, r.nwords, sizeof *r.words, cmp_word);
    if ((v = jget(jr, "not_after")) && !rolltui_json_is_null(v)) {
      if (!read_string_list(cx, where, v, &r.not_after, &r.n_not_after, 0)) { rule_release(&r); return 0; }
    } else if (cx->n_not_after) {
      r.not_after = (char**)rolltui_mem_alloc((cx->n_not_after + 1) * sizeof *r.not_after);
      for (i = 0; i < cx->n_not_after; ++i) r.not_after[i] = dup_n(cx->not_after[i], strlen(cx->not_after[i]));
      r.n_not_after = cx->n_not_after;
    }
    if (!add_rule(cx, ctx, &r)) { rule_release(&r); return 0; }
    return 1;
  }

  /* ---- a pattern -------------------------------------------------------------------------------------------------------- */
  if ((v = jget(jr, "match")) && !rolltui_json_is_null(v)) {
    const RolltuiJsonValue* caps = jget(jr, "captures");
    const int cls = class_of(cx, where, jget(jr, "class"), cx->s->ctxs[ctx].default_cls); /* no class: the context's own */
    int actions = 0;
    if (cls < 0) return 0;
    if (!jstr(v, &s, &n) || n == 0) { say(cx, where, "match must be a non-empty pattern", NULL); return 0; }
    r.re = compile_re(cx, where, s, n);
    if (!r.re) return 0;
    r.cls = (unsigned char)cls;
    if (caps && rolltui_json_is_object(caps)) {
      size_t i;
      for (i = 0; i < rolltui_json_object_size(caps); ++i) {
        size_t kn;
        const char* k = rolltui_json_object_key_at(caps, i, &kn);
        const int g = kn == 1 && k[0] >= '1' && k[0] <= '9' ? k[0] - '0' : -1;
        const int c = class_of(cx, where, rolltui_json_object_value_at(caps, i), -1);
        if (g < 0 || g > rolltui_regex_groups(r.re)) { say(cx, where, "a capture names a group the pattern does not have", NULL); rule_release(&r); return 0; }
        if (c < 0) { if (!cx->failed) say(cx, where, "a capture needs a class", NULL); rule_release(&r); return 0; }
        r.cap_cls[g] = (unsigned char)c;
        r.has_caps = 1;
      }
    }
    if ((v = jget(jr, "push")) && !rolltui_json_is_null(v)) {
      if (!jstr(v, &s, &n) || (r.target = find_ctx(cx, s, n)) < 0) { say(cx, where, "push names a context that does not exist", NULL); rule_release(&r); return 0; }
      r.action = A_PUSH; ++actions;
    }
    if ((v = jget(jr, "set")) && !rolltui_json_is_null(v)) {
      if (!jstr(v, &s, &n) || (r.target = find_ctx(cx, s, n)) < 0) { say(cx, where, "set names a context that does not exist", NULL); rule_release(&r); return 0; }
      r.action = A_SET; ++actions;
    }
    if (rolltui_json_as_bool(jget(jr, "pop"), 0)) { r.action = A_POP; ++actions; }
    if (actions > 1) { say(cx, where, "a rule may push, set or pop, not more than one", NULL); rule_release(&r); return 0; }
    if (!add_rule(cx, ctx, &r)) { rule_release(&r); return 0; }
    return 1;
  }
  say(cx, where, "a rule needs one of match, words, region or include", NULL);
  return 0;
}

static int compile_rules(Cx* cx, int ctx, const RolltuiJsonValue* arr, const char* where) {
  size_t i;
  char at[96];
  if (!rolltui_json_is_array(arr)) { say(cx, where, "rules must be a list", NULL); return 0; }
  for (i = 0; i < rolltui_json_array_size(arr); ++i) {
    snprintf(at, sizeof at, "%s, rule %zu", where && *where ? where : cx->s->ctxs[ctx].name, i + 1);
    if (!compile_rule(cx, ctx, rolltui_json_array_at(arr, i), at)) return 0;
  }
  return 1;
}

int rolltui_syntax_add(RolltuiSyntax* s, const char* json, size_t n, RolltuiStr* err) {
  RolltuiJsonValue* root;
  RolltuiStr perr;
  Cx cx;
  Lang lang;
  const RolltuiJsonValue* ctxs;
  const char* name;
  size_t name_n, i;
  const size_t first_ctx = s->nctxs;
  int ok = 0;
  if (err) rolltui_str_clear(err);
  if (!s || !json || n == 0 || n > MAX_FILE) { if (err) rolltui_str_set(err, "not a language file", 19); return 0; }
  memset(&perr, 0, sizeof perr);
  root = rolltui_json_parse(json, n, &perr);
  if (!root) {
    if (err) {
      rolltui_str_set(err, "not JSON: ", 10);
      rolltui_str_append(err, perr.p ? perr.p : "", perr.n);
      /* the mistake everyone makes writing a pattern in JSON: `\d` is not an escape there, and a backslash is written twice */
      if (perr.p && strstr(perr.p, "unknown escape")) {
        static const char hint[] = " (in a JSON string a backslash is written twice: \\\\d for the pattern \\d, \\\\. for \\.)";
        rolltui_str_append(err, hint, sizeof hint - 1);
      }
    }
    rolltui_str_free(&perr);
    return 0;
  }
  rolltui_str_free(&perr);
  memset(&cx, 0, sizeof cx);
  memset(&lang, 0, sizeof lang);
  cx.s = s;
  cx.err = err;
  cx.lang = (int)s->nlangs;
  if (!jstr(jget(root, "name"), &name, &name_n) || name_n == 0 || name_n > 60) {
    if (err) rolltui_str_set(err, "a language needs a name", 23);
    rolltui_json_free(root);
    return 0;
  }
  lang.name = dup_n(name, name_n);
  cx.lang_name = lang.name;
  cx.icase = rolltui_json_as_bool(jget(root, "ignore_case"), 0);
  lang.icase = cx.icase;
  lang.main_ctx = -1;
  if (s->nlangs >= MAX_LANGS) { say(&cx, "", "too many languages", NULL); goto out; }
  if (!read_string_list(&cx, "aliases", jget(root, "aliases"), &lang.aliases, &lang.naliases, 1)) goto out;
  if (!read_string_list(&cx, "extensions", jget(root, "extensions"), &lang.exts, &lang.nexts, 1)) goto out;
  if (!read_string_list(&cx, "filenames", jget(root, "filenames"), &lang.files, &lang.nfiles, 0)) goto out;
  if (!read_string_list(&cx, "not_after", jget(root, "not_after"), &lang.not_after, &lang.n_not_after, 0)) goto out;
  cx.not_after = lang.not_after;
  cx.n_not_after = lang.n_not_after;
  {
    const char* wc;
    size_t wn, k;
    if (jstr(jget(root, "word_chars"), &wc, &wn))
      for (k = 0; k < wn; ++k) {
        const unsigned char c = (unsigned char)wc[k];
        if (c <= ' ' || c >= 0x7F || is_word_byte(c)) { say(&cx, "word_chars", "may hold only punctuation, such as -", NULL); goto out; }
        cx.wc[c >> 3] = (unsigned char)(cx.wc[c >> 3] | (1u << (c & 7)));
      }
  }
  {
    const char* fl;
    size_t fn;
    if (jstr(jget(root, "first_line"), &fl, &fn) && fn) {
      lang.first_line = compile_re(&cx, "first_line", fl, fn);
      if (!lang.first_line) goto out;
    }
  }
  ctxs = jget(root, "contexts");
  if (!rolltui_json_is_object(ctxs) || rolltui_json_object_size(ctxs) == 0 || rolltui_json_object_size(ctxs) > MAX_CTX_PER_LANG) {
    say(&cx, "", "contexts must be an object of rule lists, and hold a `main`", NULL);
    goto out;
  }
  /* every named context exists before any rule is read, so a rule may push or include one written later */
  for (i = 0; i < rolltui_json_object_size(ctxs); ++i) {
    size_t kn;
    const char* k = rolltui_json_object_key_at(ctxs, i, &kn);
    const int c = add_ctx(&cx, k, kn);
    if (c < 0) goto out;
    if (kn == 4 && memcmp(k, "main", 4) == 0) lang.main_ctx = c;
  }
  if (lang.main_ctx < 0) { say(&cx, "", "there is no `main` context", NULL); goto out; }
  for (i = 0; i < rolltui_json_object_size(ctxs); ++i) {
    size_t kn;
    const char* k = rolltui_json_object_key_at(ctxs, i, &kn);
    const int c = find_ctx(&cx, k, kn);
    char where[80];
    const RolltuiJsonValue* body = rolltui_json_object_value_at(ctxs, i);
    snprintf(where, sizeof where, "context %.60s", k);
    if (rolltui_json_is_object(body)) { /* { "class": "comment", "rules": [...] }: text no rule takes is drawn in that class */
      const int dc = class_of(&cx, where, jget(body, "class"), ROLLTUI_SYN_PLAIN);
      if (dc < 0) goto out;
      s->ctxs[c].default_cls = (unsigned char)dc;
      body = jget(body, "rules");
    }
    if (!compile_rules(&cx, c, body, where)) goto out;
  }
  ok = 1;
out:
  if (ok) {
    /* a language of the same name is replaced: the old one is switched off, its contexts kept (nothing points at them but it) */
    for (i = 0; i < s->nlangs; ++i)
      if (s->langs[i].active && strlen(s->langs[i].name) == strlen(lang.name)) {
        size_t k;
        int same = 1;
        for (k = 0; lang.name[k]; ++k) {
          char a = lang.name[k], b = s->langs[i].name[k];
          if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
          if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
          if (a != b) same = 0;
        }
        if (same) s->langs[i].active = 0;
      }
    lang.active = 1;
    s->langs = (Lang*)rolltui_grow(s->langs, &s->langs_cap, s->nlangs + 1, sizeof *s->langs);
    s->langs[s->nlangs++] = lang;
    s->dirty = 1;
  } else {
    /* refused whole: what was built is taken back */
    while (s->nctxs > first_ctx) ctx_release(&s->ctxs[--s->nctxs]);
    lang_release(&lang);
  }
  rolltui_json_free(root);
  return ok;
}

/* ---- linking: includes spliced, and what can begin a rule found ------------------------------------------------------- */
static int find_lang_named(const RolltuiSyntax* s, const char* name, size_t n, int any_alias) {
  size_t i, k;
  for (i = s->nlangs; i-- > 0;) {
    const Lang* l = &s->langs[i];
    int same = 1;
    if (!l->active) continue;
    if (strlen(l->name) == n) {
      for (k = 0; k < n; ++k) {
        char a = name[k], b = l->name[k];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) { same = 0; break; }
      }
      if (same) return (int)i;
    }
    if (any_alias)
      for (k = 0; k < l->naliases; ++k) {
        size_t m;
        int eq = strlen(l->aliases[k]) == n;
        for (m = 0; eq && m < n; ++m) {
          char a = name[m];
          if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
          if (a != l->aliases[k][m]) eq = 0;
        }
        if (eq) return (int)i;
      }
  }
  return -1;
}

static int resolve_include(const RolltuiSyntax* s, const Rule* r) {
  int lang, c;
  if (r->target >= 0) return r->target;
  lang = find_lang_named(s, r->inc_lang ? r->inc_lang : "", r->inc_lang ? strlen(r->inc_lang) : 0, 1);
  if (lang < 0) return -1;
  if (!r->inc_ctx) return s->langs[lang].main_ctx;
  for (c = 0; (size_t)c < s->nctxs; ++c)
    if (s->ctxs[c].lang == lang && strcmp(s->ctxs[c].name, r->inc_ctx) == 0) return c;
  return -1;
}

static void flatten(const RolltuiSyntax* s, int ctx, Rule*** out, size_t* n, size_t* cap, int depth) {
  size_t i;
  Ctx* c = &s->ctxs[ctx];
  for (i = 0; i < c->nrules; ++i) {
    Rule* r = &c->rules[i];
    if (r->is_include) {
      const int t = depth < 6 ? resolve_include(s, r) : -1;
      if (t >= 0 && t != ctx) flatten(s, t, out, n, cap, depth + 1);
    } else {
      *out = (Rule**)rolltui_grow(*out, cap, *n + 1, sizeof **out);
      (*out)[(*n)++] = r;
    }
  }
}

static void link_all(RolltuiSyntax* s) {
  size_t i, k;
  int b;
  if (!s->dirty) return;
  for (i = 0; i < s->nctxs; ++i) {
    Ctx* c = &s->ctxs[i];
    rolltui_mem_free(c->flat);
    c->flat = NULL;
    c->nflat = 0;
  }
  for (i = 0; i < s->nctxs; ++i) {
    Ctx* c = &s->ctxs[i];
    size_t cap = 0;
    flatten(s, (int)i, &c->flat, &c->nflat, &cap, 0);
    memset(c->start, 0, sizeof c->start);
    for (k = 0; k < c->nflat; ++k) {
      const Rule* r = c->flat[k];
      for (b = 0; b < 256; ++b) {
        const int can = r->re ? rolltui_regex_first_byte(r->re, (unsigned char)b) : word_byte(r->wc, (unsigned char)b);
        if (can) c->start[b >> 3] = (unsigned char)(c->start[b >> 3] | (1u << (b & 7)));
      }
    }
  }
  s->dirty = 0;
}

/* ---- finding a language ------------------------------------------------------------------------------------------------- */
size_t rolltui_syntax_language_count(const RolltuiSyntax* s) { return s ? s->nlangs : 0; }
const char* rolltui_syntax_language_name(const RolltuiSyntax* s, int lang) {
  return s && lang >= 0 && (size_t)lang < s->nlangs && s->langs[lang].active ? s->langs[lang].name : NULL;
}

int rolltui_syntax_find_name(RolltuiSyntax* s, const char* name, size_t n) {
  if (!s || !name || n == 0) return -1;
  return find_lang_named(s, name, n, 1);
}

static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

int rolltui_syntax_find_file(RolltuiSyntax* s, const char* path, size_t n, const char* first_line, size_t first_n) {
  size_t base = n, i, k;
  int best = -1;
  size_t best_len = 0;
  if (!s || !path) return -1;
  while (base > 0 && path[base - 1] != '/') --base;
  {
    const char* leaf = path + base;
    const size_t ln = n - base;
    for (i = s->nlangs; i-- > 0;) {
      const Lang* l = &s->langs[i];
      if (!l->active) continue;
      for (k = 0; k < l->nfiles; ++k)
        if (strlen(l->files[k]) == ln && memcmp(l->files[k], leaf, ln) == 0) return (int)i;
    }
    for (i = s->nlangs; i-- > 0;) {
      const Lang* l = &s->langs[i];
      if (!l->active) continue;
      for (k = 0; k < l->nexts; ++k) {
        const size_t en = strlen(l->exts[k]);
        size_t m;
        int same = ln > en + 1; /* a name that is only a dot and its letters (`.ts`) has no extension, as with `.bashrc` */
        for (m = 0; same && m < en; ++m) if (lc(leaf[ln - en + m]) != l->exts[k][m]) same = 0;
        if (same && leaf[ln - en - 1] == '.' && en > best_len) { best = (int)i; best_len = en; }
      }
    }
    if (best >= 0) return best;
  }
  if (first_line && first_n) {
    for (i = s->nlangs; i-- > 0;) {
      const Lang* l = &s->langs[i];
      if (l->active && l->first_line && rolltui_regex_match_at(l->first_line, first_line, first_n, 0, NULL, NULL)) return (int)i;
    }
  }
  return -1;
}

/* ---- the highlight result -------------------------------------------------------------------------------------------------- */
struct RolltuiHighlight {
  RolltuiSyntaxRun* runs;
  size_t n, cap;
  size_t* first;       /* first[i]: the index of line i's first run; first[nlines] == n */
  size_t nlines, first_cap;
};

RolltuiHighlight* rolltui_highlight_new(void) {
  RolltuiHighlight* h = (RolltuiHighlight*)rolltui_mem_alloc(sizeof *h);
  memset(h, 0, sizeof *h);
  return h;
}

void rolltui_highlight_free(RolltuiHighlight* h) {
  if (!h) return;
  rolltui_mem_free(h->runs);
  rolltui_mem_free(h->first);
  rolltui_mem_free(h);
}

size_t rolltui_highlight_line_count(const RolltuiHighlight* h) { return h ? h->nlines : 0; }

size_t rolltui_highlight_line(const RolltuiHighlight* h, size_t i, const RolltuiSyntaxRun** runs) {
  if (!h || i >= h->nlines) { if (runs) *runs = NULL; return 0; }
  if (runs) *runs = h->runs + h->first[i];
  return h->first[i + 1] - h->first[i];
}

/* ---- the engine ----------------------------------------------------------------------------------------------------------------- */
typedef struct Frame {
  int ctx;
  char buf[10][REF_BYTES];
  unsigned char len[10];
} Frame;

typedef struct Runner {
  const RolltuiSyntax* s;
  RolltuiHighlight* out;
  size_t line_first;   /* where this line's runs began in `out->runs` */
  const char* line;    /* the line being run, and how much of it is (a character boundary) */
  size_t lim;
  Frame stack[MAX_FRAMES];
  int sp;
} Runner;

/* The first character boundary at or after `i`: a pattern matches bytes, and `\.` before an é must not end a run inside it. */
static size_t snap(const Runner* R, size_t i) {
  while (i < R->lim && ((unsigned char)R->line[i] & 0xC0) == 0x80) ++i;
  return i;
}

static void emit_run(Runner* R, size_t begin, size_t end, int cls) {
  RolltuiHighlight* h = R->out;
  begin = snap(R, begin);
  end = snap(R, end);
  if (cls == ROLLTUI_SYN_PLAIN || end <= begin) return;
  if (h->n > R->line_first && h->runs[h->n - 1].cls == cls && h->runs[h->n - 1].end == begin) {
    h->runs[h->n - 1].end = (unsigned int)end;
    return;
  }
  h->runs = (RolltuiSyntaxRun*)rolltui_grow(h->runs, &h->cap, h->n + 1, sizeof *h->runs);
  h->runs[h->n].begin = (unsigned int)begin;
  h->runs[h->n].end = (unsigned int)end;
  h->runs[h->n].cls = (unsigned char)cls;
  ++h->n;
}

static size_t utf8_len(const char* line, size_t len, size_t pos) {
  size_t k = 1;
  while (pos + k < len && ((unsigned char)line[pos + k] & 0xC0) == 0x80 && k < 4) ++k;
  return k;
}

static int cmp_key(const char* key, size_t n, const char* word) {
  const size_t wn = strlen(word);
  const int c = memcmp(key, word, n < wn ? n : wn);
  if (c) return c;
  return n < wn ? -1 : n > wn ? 1 : 0;
}

static int word_in(const Rule* r, const char* w, size_t n, int icase) {
  char low[64];
  size_t lo = 0, hi = r->nwords;
  if (n == 0 || n >= sizeof low) return 0;
  if (icase) {
    size_t k;
    for (k = 0; k < n; ++k) low[k] = lc(w[k]);
    w = low;
  }
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = cmp_key(w, n, r->words[mid]);
    if (c == 0) return 1;
    if (c < 0) hi = mid; else lo = mid + 1;
  }
  return 0;
}

static void apply_action(Runner* R, const Rule* r, const char* line, const RolltuiRegexMatch* m) {
  if (r->action == A_PUSH) {
    Frame* f;
    int g;
    if (R->sp >= MAX_FRAMES) return;
    f = &R->stack[R->sp++];
    memset(f, 0, sizeof *f);
    f->ctx = r->target;
    if (m)
      for (g = 1; g < 10; ++g)
        if (m->start[g] >= 0) {
          size_t k = (size_t)(m->end[g] - m->start[g]);
          if (k > REF_BYTES) k = REF_BYTES;
          memcpy(f->buf[g], line + m->start[g], k);
          f->len[g] = (unsigned char)k;
        }
  } else if (r->action == A_POP) {
    if (R->sp > 1) --R->sp;
  } else if (r->action == A_SET) {
    Frame* f = &R->stack[R->sp - 1];
    memset(f, 0, sizeof *f);
    f->ctx = r->target;
  }
}

/* The line is over. A rule that can match nothing may still act here (`$` pops a value, a lookahead ends a block), and a
 * region that ends with its line ends now; each may open the way for the other, so they take turns, a bounded number of times. */
static void end_of_line(Runner* R, const char* line, size_t len) {
  int round;
  for (round = 0; round < 2 * MAX_FRAMES; ++round) {
    Frame* fr = &R->stack[R->sp - 1];
    const Ctx* c = &R->s->ctxs[fr->ctx];
    size_t i;
    int acted = 0;
    for (i = 0; i < c->nflat && !acted; ++i) {
      const Rule* r = c->flat[i];
      RolltuiRegexMatch m;
      RolltuiRegexRefs refs;
      int g;
      const int refd = r->re ? rolltui_regex_uses_refs(r->re) : 0;
      if (!r->re || r->action == A_NONE || !rolltui_regex_can_be_empty(r->re)) continue;
      if (refd) {
        memset(&refs, 0, sizeof refs);
        for (g = 1; g < 10; ++g) { refs.p[g] = fr->buf[g]; refs.n[g] = fr->len[g]; }
      }
      if (!rolltui_regex_match_at(r->re, line, len, len, refd ? &refs : NULL, &m)) continue;
      apply_action(R, r, line, &m);
      acted = 1;
    }
    if (acted) continue;
    if (R->sp > 1 && c->eol_pop) { --R->sp; continue; }
    break;
  }
}

static void run_line(Runner* R, const char* line, size_t len) {
  size_t pos = 0, steps = 0;
  size_t limit = len < MAX_LINE ? len : MAX_LINE;
  while (limit > 0 && limit < len && ((unsigned char)line[limit] & 0xC0) == 0x80) --limit; /* not in the middle of a character */
  R->line = line;
  R->lim = limit;
  while (pos < limit) {
    Frame* fr = &R->stack[R->sp - 1];
    const Ctx* c = &R->s->ctxs[fr->ctx];
    const unsigned char b = (unsigned char)line[pos];
    size_t i;
    int did = 0;
    if (++steps > limit * 3 + 64) break;
    if (!((c->start[b >> 3] >> (b & 7)) & 1)) {
      /* nothing here can begin a token: everything up to the next byte something could is one run of the default class */
      size_t e = pos + 1;
      while (e < limit && !((c->start[(unsigned char)line[e] >> 3] >> ((unsigned char)line[e] & 7)) & 1)) ++e;
      emit_run(R, pos, e, c->default_cls);
      pos = e;
      continue;
    }
    for (i = 0; i < c->nflat && !did; ++i) {
      const Rule* r = c->flat[i];
      if (!r->re) {
        size_t we, k;
        int skip = 0;
        if (!word_byte(r->wc, b) || (pos > 0 && word_byte(r->wc, (unsigned char)line[pos - 1]))) continue;
        for (k = 0; k < r->n_not_after && !skip; ++k) {
          const size_t an = strlen(r->not_after[k]);
          if (pos >= an && memcmp(line + pos - an, r->not_after[k], an) == 0) skip = 1;
        }
        if (skip) continue;
        we = pos;
        while (we < limit && word_byte(r->wc, (unsigned char)line[we])) ++we;
        if (word_in(r, line + pos, we - pos, r->icase)) {
          emit_run(R, pos, we, r->cls);
          pos = we;
          did = 1;
        }
      } else {
        RolltuiRegexMatch m;
        RolltuiRegexRefs refs;
        int g;
        size_t end;
        if (!rolltui_regex_first_byte(r->re, b)) continue;
        if (rolltui_regex_uses_refs(r->re)) {
          memset(&refs, 0, sizeof refs);
          for (g = 1; g < 10; ++g) { refs.p[g] = fr->buf[g]; refs.n[g] = fr->len[g]; }
        }
        if (!rolltui_regex_match_at(r->re, line, limit, pos, rolltui_regex_uses_refs(r->re) ? &refs : NULL, &m)) continue;
        end = (size_t)m.end[0];
        if (end == pos && r->action == A_NONE) continue; /* a match of nothing that does nothing would never move */
        if (r->has_caps) {
          size_t at = pos;
          for (g = 1; g < 10; ++g) {
            if (m.start[g] < 0 || r->cap_cls[g] == 0xFF || (size_t)m.start[g] < at) continue;
            emit_run(R, at, (size_t)m.start[g], r->cls);
            emit_run(R, (size_t)m.start[g], (size_t)m.end[g], r->cap_cls[g]);
            at = (size_t)m.end[g];
          }
          emit_run(R, at, end, r->cls);
        } else {
          emit_run(R, pos, end, r->cls);
        }
        apply_action(R, r, line, &m);
        pos = snap(R, end);
        did = 1;
      }
    }
    if (!did) {
      /* no rule wanted it: a word is one token of the default class, anything else one character */
      size_t e = pos;
      if (word_byte(c->wc, b)) { while (e < limit && word_byte(c->wc, (unsigned char)line[e])) ++e; }
      else e = pos + utf8_len(line, limit, pos);
      emit_run(R, pos, e, c->default_cls);
      pos = e;
    }
  }
  end_of_line(R, line, len);
}

long rolltui_highlight_run(RolltuiHighlight* h, RolltuiSyntax* s, int lang, const char* text, size_t n) {
  Runner* R;
  size_t at = 0;
  if (!h || !s || lang < 0 || (size_t)lang >= s->nlangs || !s->langs[lang].active) return -1;
  link_all(s);
  h->n = 0;
  h->nlines = 0;
  R = (Runner*)rolltui_mem_alloc(sizeof *R);
  memset(R, 0, sizeof *R);
  R->s = s;
  R->out = h;
  R->sp = 1;
  R->stack[0].ctx = s->langs[lang].main_ctx;
  while (at < n) {
    const char* nl = (const char*)memchr(text + at, '\n', n - at);
    size_t end = nl ? (size_t)(nl - text) : n, len = end - at;
    if (len > 0 && text[at + len - 1] == '\r') --len;
    h->first = (size_t*)rolltui_grow(h->first, &h->first_cap, h->nlines + 2, sizeof *h->first);
    h->first[h->nlines] = h->n;
    R->line_first = h->n;
    run_line(R, text + at, len);
    ++h->nlines;
    at = nl ? end + 1 : n;
  }
  h->first = (size_t*)rolltui_grow(h->first, &h->first_cap, h->nlines + 2, sizeof *h->first);
  h->first[h->nlines] = h->n;
  rolltui_mem_free(R);
  return (long)h->nlines;
}

/* ---- the shipped languages, and a person's own ------------------------------------------------------------------------------------- */
static void note(RolltuiStr* report, const char* who, const RolltuiStr* why) {
  if (!report) return;
  if (report->n) rolltui_str_append(report, "\n", 1);
  rolltui_str_append(report, who, strlen(who));
  rolltui_str_append(report, ": ", 2);
  rolltui_str_append(report, why->p ? why->p : "", why->n);
}

void rolltui_syntax_add_shipped(RolltuiSyntax* s, RolltuiStr* report) {
  size_t i;
  for (i = 0; i < rolltui_kSyntaxPresetCount; ++i) {
    RolltuiStr why;
    memset(&why, 0, sizeof why);
    if (!rolltui_syntax_add(s, rolltui_kSyntaxPresets[i].text, strlen(rolltui_kSyntaxPresets[i].text), &why)) note(report, rolltui_kSyntaxPresets[i].name, &why);
    rolltui_str_free(&why);
  }
}

/* How many languages are in play (a replaced one is not). */
static void add_lit(RolltuiStr* to, const char* lit) { rolltui_str_append(to, lit, strlen(lit)); }

static size_t active_count(const RolltuiSyntax* s) {
  size_t i, n = 0;
  for (i = 0; i < s->nlangs; ++i) n += s->langs[i].active ? 1 : 0;
  return n;
}

/* One language file by its path: 1 when it loaded, else 0 with the reason in `why` (unreadable, empty, too big, or not a language). */
static int load_language_file(RolltuiSyntax* s, const char* path, RolltuiStr* why) {
  FILE* f = fopen(path, "rb");
  char* buf;
  size_t got;
  int ok;
  rolltui_str_clear(why);
  if (!f) { const char* m = strerror(errno); rolltui_str_set(why, m, strlen(m)); return 0; }
  buf = (char*)rolltui_mem_alloc(MAX_FILE + 2);
  got = fread(buf, 1, MAX_FILE + 1, f);
  fclose(f);
  if (got == 0) { { const char* m = "the file is empty"; rolltui_str_set(why, m, strlen(m)); } ok = 0; }
  else if (got > MAX_FILE) { { const char* m = "the file is larger than 256 KB, which no language is"; rolltui_str_set(why, m, strlen(m)); } ok = 0; }
  else ok = rolltui_syntax_add(s, buf, got, why);
  rolltui_mem_free(buf);
  return ok;
}

/* Every `*.json` in `dir`, in name order. Each that loads is counted; each that does not is noted in `report` (`file: why`) and
 * as a line in `lines` (`file NOT LOADED: why`), as is each that does (`file  Language`, and that it replaced a shipped one when
 * it did). A folder that is not there is a person who has written no languages. */
static size_t load_dir(RolltuiSyntax* s, const char* dir, RolltuiStr* report, RolltuiStr* lines, size_t* bad) {
  RolltuiDirList list;
  RolltuiStr err;
  size_t i, loaded = 0;
  memset(&list, 0, sizeof list);
  memset(&err, 0, sizeof err);
  if (!dir || !rolltui_dir_read(dir, strlen(dir), ROLLTUI_SORT_NAME, ROLLTUI_DIR_HIDDEN, &list, &err)) {
    rolltui_dir_list_release(&list);
    rolltui_str_free(&err);
    return 0;
  }
  rolltui_str_free(&err);
  for (i = 0; i < list.n; ++i) {
    const RolltuiDirEntry* e = &list.v[i];
    RolltuiStr path, why;
    size_t before;
    if (e->is_dir || e->name.n < 6 || memcmp(e->name.p + e->name.n - 5, ".json", 5) != 0) continue;
    memset(&path, 0, sizeof path);
    memset(&why, 0, sizeof why);
    rolltui_str_set(&path, dir, strlen(dir));
    rolltui_str_append(&path, "/", 1);
    rolltui_str_append(&path, e->name.p, e->name.n);
    before = active_count(s);
    if (load_language_file(s, path.p, &why)) {
      ++loaded;
      if (lines) {
        const char* name = s->langs[s->nlangs - 1].name;
        add_lit(lines, "  ");
        rolltui_str_append(lines, e->name.p, e->name.n);
        add_lit(lines, "  ");
        rolltui_str_append(lines, name, strlen(name));
        if (active_count(s) == before) add_lit(lines, "  (replaces the shipped one of that name)");
        add_lit(lines, "\n");
      }
    } else {
      if (bad) ++*bad;
      note(report, e->name.p, &why);
      if (lines) {
        add_lit(lines, "  ");
        rolltui_str_append(lines, e->name.p, e->name.n);
        add_lit(lines, "  NOT LOADED: ");
        rolltui_str_append(lines, why.p ? why.p : "", why.n);
        add_lit(lines, "\n");
      }
    }
    rolltui_str_free(&why);
    rolltui_str_free(&path);
  }
  rolltui_dir_list_release(&list);
  return loaded;
}

size_t rolltui_syntax_add_dir(RolltuiSyntax* s, const char* dir, RolltuiStr* report) { return load_dir(s, dir, report, NULL, NULL); }

/* `<config>/rolltui/syntax`, where a person's own languages live: beside the terminal's remembered answers, which name the
 * configuration directory, so it is read from there rather than worked out a second time. 0 when there is no such directory to name. */
static int own_languages_dir(RolltuiStr* dir) {
  RolltuiStr cache;
  size_t slash;
  memset(&cache, 0, sizeof cache);
  rolltui_termcache_path(NULL, &cache);
  slash = cache.n;
  while (slash > 0 && cache.p[slash - 1] != '/') --slash;
  if (cache.n == 0 || slash == 0) { rolltui_str_free(&cache); return 0; }
  rolltui_str_set(dir, cache.p, slash - 1);
  add_lit(dir, "/syntax");
  rolltui_str_free(&cache);
  return 1;
}

RolltuiSyntax* rolltui_syntax_new_standard(RolltuiStr* report) {
  RolltuiSyntax* s = rolltui_syntax_new();
  RolltuiStr dir;
  memset(&dir, 0, sizeof dir);
  rolltui_syntax_add_shipped(s, report);
  if (own_languages_dir(&dir)) load_dir(s, dir.p, report, NULL, NULL);
  rolltui_str_free(&dir);
  return s;
}

size_t rolltui_syntax_check(const char* const* paths, size_t path_count, RolltuiStr* out) {
  RolltuiSyntax* s = rolltui_syntax_new();
  RolltuiStr shipped, lines, dir, why;
  size_t bad = 0, i;
  char num[32];
  memset(&shipped, 0, sizeof shipped);
  memset(&lines, 0, sizeof lines);
  memset(&dir, 0, sizeof dir);
  memset(&why, 0, sizeof why);
  rolltui_str_clear(out);
  rolltui_syntax_add_shipped(s, &shipped);
  snprintf(num, sizeof num, "%zu", active_count(s));
  add_lit(out, "shipped: ");
  rolltui_str_append(out, num, strlen(num));
  add_lit(out, " languages");
  if (shipped.n) { /* the build's own files must load: a line here is a bug in the library, said plainly */
    add_lit(out, "\nTHE SHIPPED LANGUAGES DID NOT ALL LOAD (a bug):\n");
    rolltui_str_append_str(out, &shipped);
    ++bad;
  }
  add_lit(out, "\n");
  if (own_languages_dir(&dir)) {
    load_dir(s, dir.p, NULL, &lines, &bad);
    add_lit(out, "yours, in ");
    rolltui_str_append(out, dir.p, dir.n);
    add_lit(out, lines.n ? ":\n" : ": none (a `.json` file put there is read when the browser starts)\n");
    rolltui_str_append_str(out, &lines);
  } else {
    add_lit(out, "yours: there is no configuration directory to keep them in\n");
  }
  for (i = 0; i < path_count; ++i) {
    const size_t before = active_count(s);
    add_lit(out, "file ");
    rolltui_str_append(out, paths[i] ? paths[i] : "", paths[i] ? strlen(paths[i]) : 0);
    if (paths[i] && load_language_file(s, paths[i], &why)) {
      const char* name = s->langs[s->nlangs - 1].name;
      add_lit(out, ": loaded as ");
      rolltui_str_append(out, name, strlen(name));
      if (active_count(s) == before) add_lit(out, " (replaces a language of that name)");
      add_lit(out, "\n");
    } else {
      ++bad;
      add_lit(out, ": NOT LOADED: ");
      if (why.n) rolltui_str_append(out, why.p, why.n); else add_lit(out, "no file named");
      add_lit(out, "\n");
    }
  }
  rolltui_str_free(&shipped);
  rolltui_str_free(&lines);
  rolltui_str_free(&dir);
  rolltui_str_free(&why);
  rolltui_syntax_free(s);
  return bad;
}

/* ---- the Markdown seam ------------------------------------------------------------------------------------------------------------------ */
struct RolltuiSyntaxMd {
  RolltuiSyntax* s;
  RolltuiHighlight* h;
  unsigned char base;
  int lang;
  size_t lines;
  RolltuiStr joined;
};

RolltuiSyntaxMd* rolltui_syntax_md_new(RolltuiSyntax* s, unsigned char base_role) {
  RolltuiSyntaxMd* m = (RolltuiSyntaxMd*)rolltui_mem_alloc(sizeof *m);
  memset(m, 0, sizeof *m);
  m->s = s;
  m->base = base_role;
  m->h = rolltui_highlight_new();
  m->lang = -1;
  return m;
}

void rolltui_syntax_md_free(RolltuiSyntaxMd* m) {
  if (!m) return;
  rolltui_highlight_free(m->h);
  rolltui_str_free(&m->joined);
  rolltui_mem_free(m);
}

void rolltui_syntax_md_highlight(void* ctx, const char* lang, size_t lang_n, const RolltuiMdCodeLine* lines, size_t line_count,
                                 size_t index, RolltuiMdSpanSink emit, void* sink) {
  RolltuiSyntaxMd* m = (RolltuiSyntaxMd*)ctx;
  const RolltuiSyntaxRun* runs = NULL;
  size_t i, n;
  int l;
  if (!m || !m->s || !emit || index >= line_count) return;
  l = rolltui_syntax_find_name(m->s, lang, lang_n);
  if (l < 0) return;
  /* the seam asks once per line, in order: the block is run through the language once, at its first line */
  if (index == 0 || m->lang != l || m->lines != line_count || rolltui_highlight_line_count(m->h) != line_count) {
    rolltui_str_clear(&m->joined);
    for (i = 0; i < line_count; ++i) {
      rolltui_str_append(&m->joined, lines[i].p ? lines[i].p : "", lines[i].n);
      rolltui_str_append(&m->joined, "\n", 1);
    }
    m->lang = l;
    m->lines = line_count;
    rolltui_highlight_run(m->h, m->s, l, m->joined.p ? m->joined.p : "", m->joined.n);
  }
  n = rolltui_highlight_line(m->h, index, &runs);
  for (i = 0; i < n; ++i) {
    const unsigned char role = rolltui_syntax_role(runs[i].cls, m->base);
    const unsigned char flags = rolltui_syntax_flags(runs[i].cls);
    if (role != m->base || flags)
      emit(sink, runs[i].begin, runs[i].end,
           (unsigned char)(role | ((flags & ROLLTUI_SYN_BOLD) ? ROLLTUI_MD_ROLE_BOLD : 0) | ((flags & ROLLTUI_SYN_ITALIC) ? ROLLTUI_MD_ROLE_ITALIC : 0)));
  }
}
