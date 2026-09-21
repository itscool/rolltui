/* rolltui/c/rolltui_regex.c — the matcher. What it takes and what it promises are in the header. */
#include "rolltui/c/rolltui_regex.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

#define MAX_PROG 6000       /* instructions: a pattern that would compile to more is refused */
#define MAX_SETS 256
#define MAX_REPEAT 64       /* {n,m}: each copy is compiled, so the count is capped */
#define MAX_STEPS 20000     /* per match: past this it fails rather than hangs */
#define MAX_STACK 1024
#define MAX_DEPTH 24        /* nesting of groups in a pattern */

enum { OP_CHAR, OP_ANY, OP_SET, OP_SPLIT, OP_JMP, OP_SAVE, OP_BOL, OP_EOL, OP_WORDB, OP_NWORDB, OP_LOOK, OP_LOOKEND,
       OP_BACKREF, OP_LOOPSET, OP_LOOPCHK, OP_MATCH };

typedef struct Inst {
  unsigned char op;
  unsigned char c;
  int x, y;
} Inst;

typedef unsigned char Set[32];

struct RolltuiRegex {
  Inst* prog;
  size_t n, cap;
  Set* sets;
  size_t nsets, sets_cap;
  int ngroups;
  int nloops;
  int uses_refs;
  int nullable;
  int icase;
  Set first;
};

/* ---- the syntax tree -------------------------------------------------------------------------- */
enum { N_EMPTY, N_CHAR, N_ANY, N_SET, N_CAT, N_ALT, N_REP, N_GROUP, N_BOL, N_EOL, N_WORDB, N_NWORDB, N_LOOK, N_BACKREF };

typedef struct Node {
  unsigned char type;
  unsigned char ch;
  unsigned char lazy, neg;
  int a, b;        /* N_SET: the set; N_REP: min, max (-1: no limit); N_GROUP: its number, or 0; N_BACKREF: n */
  int kid, next;   /* first child and next sibling (-1: none) */
} Node;

typedef struct Parser {
  const char* s;
  size_t n, i;
  int icase;
  RolltuiRegex* re;
  Node* nodes;
  size_t nn, cap;
  int depth;
  int failed;
  char why[96];
} Parser;

static int is_word(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

static void fail_with(Parser* p, const char* why) {
  if (p->failed) return;
  p->failed = 1;
  strncpy(p->why, why, sizeof p->why - 1);
  p->why[sizeof p->why - 1] = 0;
}

static int new_node(Parser* p, int type) {
  Node* nd;
  p->nodes = (Node*)rolltui_grow(p->nodes, &p->cap, p->nn + 1, sizeof *p->nodes); /* every time the live length moves, so the sanitizer knows it */
  nd = &p->nodes[p->nn];
  memset(nd, 0, sizeof *nd);
  nd->type = (unsigned char)type;
  nd->kid = nd->next = -1;
  return (int)p->nn++;
}

static int new_set(Parser* p) {
  RolltuiRegex* re = p->re;
  if (re->nsets >= MAX_SETS) { fail_with(p, "too many character classes"); return 0; }
  re->sets = (Set*)rolltui_grow(re->sets, &re->sets_cap, re->nsets + 1, sizeof *re->sets);
  memset(re->sets[re->nsets], 0, sizeof(Set));
  return (int)re->nsets++;
}

static void set_add(Set s, unsigned c) { s[c >> 3] = (unsigned char)(s[c >> 3] | (1u << (c & 7))); }
static int set_has(const Set s, unsigned c) { return (s[c >> 3] >> (c & 7)) & 1; }
static void set_add_range(Set s, unsigned lo, unsigned hi, int icase) {
  unsigned c;
  for (c = lo; c <= hi && c < 256; ++c) {
    set_add(s, c);
    if (icase && c >= 'a' && c <= 'z') set_add(s, c - 32);
    if (icase && c >= 'A' && c <= 'Z') set_add(s, c + 32);
  }
}
static void set_add_class(Set s, char kind) {
  unsigned c;
  for (c = 0; c < 256; ++c) {
    const int digit = c >= '0' && c <= '9';
    const int word = is_word((unsigned char)c);
    const int space = c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    int in = 0;
    switch (kind) {
      case 'd': in = digit; break;
      case 'D': in = !digit; break;
      case 'w': in = word; break;
      case 'W': in = !word; break;
      case 's': in = space; break;
      case 'S': in = !space; break;
      default: break;
    }
    if (in) set_add(s, c);
  }
}

static int hex_val(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int parse_alt(Parser* p);

/* One escaped thing: sets `*cls` to a class letter (d w s D W S) or returns a byte in `*byte` (-1: neither). */
static void parse_escape(Parser* p, int* byte, char* cls) {
  int c;
  *byte = -1;
  *cls = 0;
  if (p->i >= p->n) { fail_with(p, "a pattern ends in a backslash"); return; }
  c = (unsigned char)p->s[p->i++];
  switch (c) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': *cls = (char)c; return;
    case 'n': *byte = '\n'; return;
    case 't': *byte = '\t'; return;
    case 'r': *byte = '\r'; return;
    case 'f': *byte = '\f'; return;
    case 'x': {
      int h1 = p->i < p->n ? hex_val((unsigned char)p->s[p->i]) : -1;
      int h2 = p->i + 1 < p->n ? hex_val((unsigned char)p->s[p->i + 1]) : -1;
      if (h1 < 0 || h2 < 0) { fail_with(p, "\\x needs two hex digits"); return; }
      p->i += 2;
      *byte = h1 * 16 + h2;
      return;
    }
    default:
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { fail_with(p, "an unknown escape"); return; }
      *byte = c;
      return;
  }
}

static int parse_class(Parser* p) {
  int set = new_set(p), neg = 0, first = 1, nd;
  Set* sets;
  Set tmp;
  memset(tmp, 0, sizeof tmp);
  if (p->i < p->n && p->s[p->i] == '^') { neg = 1; ++p->i; }
  for (;;) {
    int lo, hi;
    char cls;
    if (p->i >= p->n) { fail_with(p, "a character class is not closed"); break; }
    if (p->s[p->i] == ']' && !first) { ++p->i; break; }
    first = 0;
    if (p->s[p->i] == '\\') {
      ++p->i;
      parse_escape(p, &lo, &cls);
      if (p->failed) break;
      if (cls) { set_add_class(tmp, cls); continue; }
    } else {
      lo = (unsigned char)p->s[p->i++];
    }
    hi = lo;
    if (p->i + 1 < p->n && p->s[p->i] == '-' && p->s[p->i + 1] != ']') {
      char cls2;
      ++p->i;
      if (p->s[p->i] == '\\') {
        ++p->i;
        parse_escape(p, &hi, &cls2);
        if (p->failed) break;
        if (cls2) { fail_with(p, "a class cannot end a range"); break; }
      } else {
        hi = (unsigned char)p->s[p->i++];
      }
      if (hi < lo) { fail_with(p, "a range runs backwards"); break; }
    }
    set_add_range(tmp, (unsigned)lo, (unsigned)hi, p->icase);
  }
  sets = &p->re->sets[set];
  if (neg) {
    unsigned c;
    for (c = 0; c < 32; ++c) (*sets)[c] = (unsigned char)~tmp[c];
    (*sets)['\n' >> 3] = (unsigned char)((*sets)['\n' >> 3] & ~(1u << ('\n' & 7))); /* a line has no newline to match */
  } else {
    memcpy(*sets, tmp, sizeof tmp);
  }
  nd = new_node(p, N_SET);
  p->nodes[nd].a = set;
  return nd;
}

static int parse_atom(Parser* p) {
  int nd, c;
  if (p->i >= p->n) return new_node(p, N_EMPTY);
  c = (unsigned char)p->s[p->i];
  switch (c) {
    case '(': {
      int group = 0, look = 0, neg = 0, inner;
      ++p->i;
      if (p->i + 1 < p->n && p->s[p->i] == '?') {
        if (p->s[p->i + 1] == ':') { p->i += 2; }
        else if (p->s[p->i + 1] == '=' || p->s[p->i + 1] == '!') { look = 1; neg = p->s[p->i + 1] == '!'; p->i += 2; }
        else { fail_with(p, "only (?: (?= and (?! are taken"); return new_node(p, N_EMPTY); }
      } else {
        if (p->re->ngroups >= 9) { fail_with(p, "more than nine groups"); return new_node(p, N_EMPTY); }
        group = ++p->re->ngroups;
      }
      if (++p->depth > MAX_DEPTH) { fail_with(p, "groups nested too deeply"); return new_node(p, N_EMPTY); }
      inner = parse_alt(p);
      --p->depth;
      if (p->i >= p->n || p->s[p->i] != ')') { fail_with(p, "a group is not closed"); return new_node(p, N_EMPTY); }
      ++p->i;
      nd = new_node(p, look ? N_LOOK : N_GROUP);
      p->nodes[nd].kid = inner;
      p->nodes[nd].a = group;
      p->nodes[nd].neg = (unsigned char)neg;
      return nd;
    }
    case '[': ++p->i; return parse_class(p);
    case '.': ++p->i; return new_node(p, N_ANY);
    case '^': ++p->i; return new_node(p, N_BOL);
    case '$': ++p->i; return new_node(p, N_EOL);
    case '\\': {
      int byte;
      char cls;
      ++p->i;
      if (p->i < p->n && p->s[p->i] == 'b') { ++p->i; return new_node(p, N_WORDB); }
      if (p->i < p->n && p->s[p->i] == 'B') { ++p->i; return new_node(p, N_NWORDB); }
      if (p->i < p->n && p->s[p->i] >= '1' && p->s[p->i] <= '9') {
        nd = new_node(p, N_BACKREF);
        p->nodes[nd].a = p->s[p->i++] - '0';
        p->re->uses_refs = 1;
        return nd;
      }
      parse_escape(p, &byte, &cls);
      if (p->failed) return new_node(p, N_EMPTY);
      if (cls) {
        nd = new_node(p, N_SET);
        p->nodes[nd].a = new_set(p);
        set_add_class(p->re->sets[p->nodes[nd].a], cls);
        return nd;
      }
      c = byte;
      break;
    }
    case ')': case '|': return new_node(p, N_EMPTY);
    case '*': case '+': case '?': fail_with(p, "a quantifier has nothing to repeat"); ++p->i; return new_node(p, N_EMPTY);
    default: ++p->i; break;
  }
  nd = new_node(p, N_CHAR);
  if (p->icase && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    p->nodes[nd].ch = (unsigned char)(c | 0x20); /* a letter that ignores case is one instruction, not a set of two */
    p->nodes[nd].a = 1;
    return nd;
  }
  p->nodes[nd].ch = (unsigned char)c;
  return nd;
}

static int parse_number(Parser* p, int* out) {
  int v = 0, any = 0;
  while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') {
    v = v * 10 + (p->s[p->i++] - '0');
    if (v > 1000) v = 1000;
    any = 1;
  }
  *out = v;
  return any;
}

static int parse_repeat(Parser* p) {
  int atom = parse_atom(p);
  for (;;) {
    int lo, hi, rep;
    if (p->failed || p->i >= p->n) return atom;
    switch (p->s[p->i]) {
      case '*': lo = 0; hi = -1; ++p->i; break;
      case '+': lo = 1; hi = -1; ++p->i; break;
      case '?': lo = 0; hi = 1; ++p->i; break;
      case '{': {
        size_t save = p->i;
        int a, b;
        ++p->i;
        if (!parse_number(p, &a)) { p->i = save; return atom; }
        lo = hi = a;
        if (p->i < p->n && p->s[p->i] == ',') {
          ++p->i;
          if (parse_number(p, &b)) hi = b; else hi = -1;
        }
        if (p->i >= p->n || p->s[p->i] != '}') { p->i = save; return atom; }
        ++p->i;
        if (lo > MAX_REPEAT || hi > MAX_REPEAT) { fail_with(p, "a repeat count is over 64"); return atom; }
        if (hi >= 0 && hi < lo) { fail_with(p, "a repeat runs backwards"); return atom; }
        break;
      }
      default: return atom;
    }
    rep = new_node(p, N_REP);
    p->nodes[rep].kid = atom;
    p->nodes[rep].a = lo;
    p->nodes[rep].b = hi;
    if (p->i < p->n && p->s[p->i] == '?') { p->nodes[rep].lazy = 1; ++p->i; }
    atom = rep;
  }
}

static int parse_cat(Parser* p) {
  int cat = new_node(p, N_CAT), last = -1;
  while (!p->failed && p->i < p->n && p->s[p->i] != '|' && p->s[p->i] != ')') {
    const int part = parse_repeat(p);
    if (last < 0) p->nodes[cat].kid = part; else p->nodes[last].next = part;
    last = part;
  }
  return cat;
}

static int parse_alt(Parser* p) {
  int first = parse_cat(p), alt;
  int last;
  if (p->failed || p->i >= p->n || p->s[p->i] != '|') return first;
  alt = new_node(p, N_ALT);
  p->nodes[alt].kid = first;
  last = first;
  while (!p->failed && p->i < p->n && p->s[p->i] == '|') {
    int next;
    ++p->i;
    next = parse_cat(p);
    p->nodes[last].next = next;
    last = next;
  }
  return alt;
}

/* ---- code generation ---------------------------------------------------------------------------- */
typedef struct Gen {
  Parser* p;
  RolltuiRegex* re;
} Gen;

static int emit(Gen* g, int op, int c, int x, int y) {
  RolltuiRegex* re = g->re;
  if (re->n >= MAX_PROG) { fail_with(g->p, "the pattern compiles to too much"); return 0; }
  re->prog = (Inst*)rolltui_grow(re->prog, &re->cap, re->n + 1, sizeof *re->prog);
  re->prog[re->n].op = (unsigned char)op;
  re->prog[re->n].c = (unsigned char)c;
  re->prog[re->n].x = x;
  re->prog[re->n].y = y;
  return (int)re->n++;
}

static int can_be_empty(const Parser* p, int nd) {
  const Node* n = &p->nodes[nd];
  int k;
  switch (n->type) {
    case N_CHAR: case N_ANY: case N_SET: return 0;
    case N_BACKREF: return 1;
    case N_CAT:
      for (k = n->kid; k >= 0; k = p->nodes[k].next) if (!can_be_empty(p, k)) return 0;
      return 1;
    case N_ALT:
      for (k = n->kid; k >= 0; k = p->nodes[k].next) if (can_be_empty(p, k)) return 1;
      return 0;
    case N_REP: return n->a == 0 || can_be_empty(p, n->kid);
    case N_GROUP: return can_be_empty(p, n->kid);
    default: return 1; /* an assertion or a look-around consumes nothing */
  }
}

static void gen(Gen* g, int nd);

static void gen_rep(Gen* g, const Node* n) {
  const int lo = n->a, hi = n->b, kid = n->kid;
  int i;
  for (i = 0; i < lo; ++i) gen(g, kid);
  if (hi < 0) {
    /* L1: split L2, L3;  L2: kid; jmp L1;  L3: — and, when the kid can match nothing, a check that each turn moved */
    const int nullable = can_be_empty(g->p, kid);
    int l1, sp, reg = -1;
    if (nullable) {
      if (g->re->nloops >= 16) { fail_with(g->p, "too many loops that can match nothing"); return; }
      reg = g->re->nloops++;
    }
    l1 = (int)g->re->n;
    sp = emit(g, OP_SPLIT, 0, 0, 0);
    if (g->p->failed) return;
    g->re->prog[sp].x = sp + 1;
    if (nullable) emit(g, OP_LOOPSET, 0, reg, 0);
    gen(g, kid);
    if (nullable) emit(g, OP_LOOPCHK, 0, reg, 0);
    emit(g, OP_JMP, 0, l1, 0);
    if (g->p->failed) return;
    g->re->prog[sp].y = (int)g->re->n;
    if (n->lazy) { const int t = g->re->prog[sp].x; g->re->prog[sp].x = g->re->prog[sp].y; g->re->prog[sp].y = t; }
  } else {
    /* (hi - lo) optional copies, each of which is only reached when the one before it matched */
    int splits[MAX_REPEAT + 1], ns = 0;
    for (i = lo; i < hi; ++i) {
      const int sp = emit(g, OP_SPLIT, 0, 0, 0);
      if (g->p->failed) return;
      g->re->prog[sp].x = sp + 1;
      splits[ns++] = sp;
      gen(g, kid);
    }
    for (i = 0; i < ns; ++i) {
      const int sp = splits[i];
      g->re->prog[sp].y = (int)g->re->n;
      if (n->lazy) { const int t = g->re->prog[sp].x; g->re->prog[sp].x = g->re->prog[sp].y; g->re->prog[sp].y = t; }
    }
  }
}

static void gen(Gen* g, int nd) {
  const Node* n = &g->p->nodes[nd];
  int k;
  if (g->p->failed) return;
  switch (n->type) {
    case N_EMPTY: break;
    case N_CHAR: emit(g, OP_CHAR, n->ch, n->a, 0); break; /* x: the letter (stored lower case) matches either case */
    case N_ANY: emit(g, OP_ANY, 0, 0, 0); break;
    case N_SET: emit(g, OP_SET, 0, n->a, 0); break;
    case N_BOL: emit(g, OP_BOL, 0, 0, 0); break;
    case N_EOL: emit(g, OP_EOL, 0, 0, 0); break;
    case N_WORDB: emit(g, OP_WORDB, 0, 0, 0); break;
    case N_NWORDB: emit(g, OP_NWORDB, 0, 0, 0); break;
    case N_BACKREF: emit(g, OP_BACKREF, 0, n->a, 0); break;
    case N_CAT:
      for (k = n->kid; k >= 0; k = g->p->nodes[k].next) gen(g, k);
      break;
    case N_ALT: {
      int jumps[256], nj = 0, sp = -1;
      for (k = n->kid; k >= 0; k = g->p->nodes[k].next) {
        const int last = g->p->nodes[k].next < 0;
        if (sp >= 0) g->re->prog[sp].y = (int)g->re->n;
        if (!last) {
          sp = emit(g, OP_SPLIT, 0, 0, 0);
          if (g->p->failed) return;
          g->re->prog[sp].x = sp + 1;
        }
        gen(g, k);
        if (!last) {
          if (nj >= 256) { fail_with(g->p, "too many alternatives"); return; }
          jumps[nj++] = emit(g, OP_JMP, 0, 0, 0);
        }
      }
      for (k = 0; k < nj; ++k) g->re->prog[jumps[k]].x = (int)g->re->n;
      break;
    }
    case N_REP: gen_rep(g, n); break;
    case N_GROUP:
      if (n->a) emit(g, OP_SAVE, 0, n->a * 2, 0);
      gen(g, n->kid);
      if (n->a) emit(g, OP_SAVE, 0, n->a * 2 + 1, 0);
      break;
    case N_LOOK: {
      const int look = emit(g, OP_LOOK, 0, 0, n->neg);
      gen(g, n->kid);
      emit(g, OP_LOOKEND, 0, 0, 0);
      if (!g->p->failed) g->re->prog[look].x = (int)g->re->n;
      break;
    }
    default: break;
  }
}

/* ---- what can begin a match ------------------------------------------------------------------------- */
static void first_walk(RolltuiRegex* re, int pc, unsigned char* seen, Set out, int* all) {
  for (;;) {
    const Inst* I;
    unsigned c;
    if (pc < 0 || (size_t)pc >= re->n || seen[pc]) return;
    seen[pc] = 1;
    I = &re->prog[pc];
    switch (I->op) {
      case OP_CHAR: set_add(out, I->c); if (I->x) set_add(out, I->c - 32u); return;
      case OP_ANY: for (c = 0; c < 256; ++c) if (c != '\n') set_add(out, c); return;
      case OP_SET: for (c = 0; c < 32; ++c) out[c] = (unsigned char)(out[c] | re->sets[I->x][c]); return;
      case OP_SPLIT: first_walk(re, I->x, seen, out, all); pc = I->y; continue;
      case OP_JMP: pc = I->x; continue;
      case OP_LOOK: pc = I->x; continue;  /* it constrains, and consumes nothing: what follows still begins the match */
      case OP_SAVE: case OP_BOL: case OP_EOL: case OP_WORDB: case OP_NWORDB: case OP_LOOPSET: case OP_LOOPCHK: ++pc; continue;
      case OP_BACKREF: *all = 1; return;
      case OP_MATCH: *all = 1; re->nullable = 1; return;
      case OP_LOOKEND: *all = 1; return;
      default: *all = 1; return;
    }
  }
}

/* ---- compile ------------------------------------------------------------------------------------------ */
RolltuiRegex* rolltui_regex_compile(const char* pat, size_t n, int icase, RolltuiStr* err) {
  Parser p;
  Gen g;
  RolltuiRegex* re = (RolltuiRegex*)rolltui_mem_alloc(sizeof *re);
  int root, all = 0;
  unsigned char* seen;
  memset(re, 0, sizeof *re);
  memset(&p, 0, sizeof p);
  p.s = pat ? pat : "";
  p.n = n;
  p.icase = icase;
  p.re = re;
  re->icase = icase;
  root = parse_alt(&p);
  if (!p.failed && p.i < p.n) fail_with(&p, "an unmatched )");
  if (!p.failed) {
    g.p = &p;
    g.re = re;
    emit(&g, OP_SAVE, 0, 0, 0);
    gen(&g, root);
    emit(&g, OP_SAVE, 0, 1, 0);
    emit(&g, OP_MATCH, 0, 0, 0);
  }
  if (p.failed) {
    if (err) rolltui_str_set(err, p.why, strlen(p.why));
    rolltui_mem_free(p.nodes);
    rolltui_regex_free(re);
    return NULL;
  }
  rolltui_mem_free(p.nodes);
  seen = (unsigned char*)rolltui_mem_alloc(re->n + 1);
  memset(seen, 0, re->n + 1);
  first_walk(re, 0, seen, re->first, &all);
  rolltui_mem_free(seen);
  if (all) memset(re->first, 0xFF, sizeof re->first);
  return re;
}

void rolltui_regex_free(RolltuiRegex* re) {
  if (!re) return;
  rolltui_mem_free(re->prog);
  rolltui_mem_free(re->sets);
  rolltui_mem_free(re);
}

int rolltui_regex_first_byte(const RolltuiRegex* re, unsigned char b) { return re ? set_has(re->first, b) : 0; }
int rolltui_regex_groups(const RolltuiRegex* re) { return re ? re->ngroups : 0; }
int rolltui_regex_uses_refs(const RolltuiRegex* re) { return re ? re->uses_refs : 0; }
int rolltui_regex_can_be_empty(const RolltuiRegex* re) { return re ? re->nullable : 0; }

/* ---- the matcher -------------------------------------------------------------------------------------- */
enum { K_BRANCH, K_CAP, K_REG };
typedef struct Frame {
  int kind, a, b;
} Frame;

typedef struct Vm {
  const RolltuiRegex* re;
  const char* text;
  int len;
  const RolltuiRegexRefs* refs;
  int caps[20];
  int regs[16];
  Frame* stack;
  int sp;
  int steps;
} Vm;

/* Runs from `pc` at `pos` until OP_MATCH or OP_LOOKEND. Returns the end position or -1. Leaves the stack as it found it. */
static int run(Vm* vm, int pc, int pos) {
  const RolltuiRegex* re = vm->re;
  const int base = vm->sp;
  int result = -1, whole = 0;
  for (;;) {
    const Inst* I;
    int fail = 0;
    if (++vm->steps > MAX_STEPS) { result = -1; break; }
    I = &re->prog[pc];
    switch (I->op) {
      case OP_CHAR:
        if (pos < vm->len && ((unsigned char)vm->text[pos] == I->c || (I->x && ((unsigned char)vm->text[pos] | 0x20u) == I->c))) { ++pos; ++pc; }
        else fail = 1;
        break;
      case OP_ANY: if (pos < vm->len) { ++pos; ++pc; } else fail = 1; break;
      case OP_SET:
        if (pos < vm->len && set_has(re->sets[I->x], (unsigned char)vm->text[pos])) { ++pos; ++pc; } else fail = 1;
        break;
      case OP_SPLIT:
        if (vm->sp >= MAX_STACK) { result = -1; goto done; }
        vm->stack[vm->sp].kind = K_BRANCH; vm->stack[vm->sp].a = I->y; vm->stack[vm->sp].b = pos; ++vm->sp;
        pc = I->x;
        break;
      case OP_JMP: pc = I->x; break;
      case OP_SAVE:
        if (vm->sp >= MAX_STACK) { result = -1; goto done; }
        vm->stack[vm->sp].kind = K_CAP; vm->stack[vm->sp].a = I->x; vm->stack[vm->sp].b = vm->caps[I->x]; ++vm->sp;
        vm->caps[I->x] = pos;
        ++pc;
        break;
      case OP_LOOPSET:
        if (vm->sp >= MAX_STACK) { result = -1; goto done; }
        vm->stack[vm->sp].kind = K_REG; vm->stack[vm->sp].a = I->x; vm->stack[vm->sp].b = vm->regs[I->x]; ++vm->sp;
        vm->regs[I->x] = pos;
        ++pc;
        break;
      case OP_LOOPCHK: if (vm->regs[I->x] == pos) fail = 1; else ++pc; break;
      case OP_BOL: if (pos == 0) ++pc; else fail = 1; break;
      case OP_EOL: if (pos == vm->len) ++pc; else fail = 1; break;
      case OP_WORDB: case OP_NWORDB: {
        const int before = pos > 0 && is_word((unsigned char)vm->text[pos - 1]);
        const int after = pos < vm->len && is_word((unsigned char)vm->text[pos]);
        if ((before != after) == (I->op == OP_WORDB)) ++pc; else fail = 1;
        break;
      }
      case OP_BACKREF: {
        const RolltuiRegexRefs* r = vm->refs;
        const size_t rn = r && I->x >= 0 && I->x < 10 ? r->n[I->x] : 0;
        size_t k;
        if ((size_t)pos + rn > (size_t)vm->len) { fail = 1; break; }
        for (k = 0; k < rn; ++k) {
          unsigned char a = (unsigned char)vm->text[pos + (int)k], b = (unsigned char)r->p[I->x][k];
          if (re->icase) { if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32); if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32); }
          if (a != b) { fail = 1; break; }
        }
        if (!fail) { pos += (int)rn; ++pc; }
        break;
      }
      case OP_LOOK: {
        const int at = vm->sp;
        const int end = run(vm, pc + 1, pos);
        /* what a look-around captured is not kept: undo down to where it began, whatever it found */
        while (vm->sp > at) {
          --vm->sp;
          if (vm->stack[vm->sp].kind == K_CAP) vm->caps[vm->stack[vm->sp].a] = vm->stack[vm->sp].b;
          else if (vm->stack[vm->sp].kind == K_REG) vm->regs[vm->stack[vm->sp].a] = vm->stack[vm->sp].b;
        }
        if (vm->steps > MAX_STEPS) { result = -1; goto done; }
        if ((end >= 0) != (I->y != 0)) pc = I->x; else fail = 1;
        break;
      }
      case OP_LOOKEND: result = pos; goto done;
      case OP_MATCH: result = pos; whole = 1; goto done;
      default: fail = 1; break;
    }
    if (fail) {
      for (;;) {
        Frame* f;
        if (vm->sp <= base) { result = -1; goto done; }
        f = &vm->stack[--vm->sp];
        if (f->kind == K_BRANCH) { pc = f->a; pos = f->b; break; }
        if (f->kind == K_CAP) vm->caps[f->a] = f->b;
        else vm->regs[f->a] = f->b;
      }
    }
  }
done:
  /* a failure undoes what it did. A whole match keeps its captures (the caller reads them) and drops its branches; a
   * look-around that succeeded leaves its records for the caller to undo, since what it captured is not exported */
  if (result < 0) {
    while (vm->sp > base) {
      const Frame* f = &vm->stack[--vm->sp];
      if (f->kind == K_CAP) vm->caps[f->a] = f->b;
      else if (f->kind == K_REG) vm->regs[f->a] = f->b;
    }
  } else if (whole) {
    vm->sp = base;
  }
  return result;
}

int rolltui_regex_match_at(const RolltuiRegex* re, const char* line, size_t len, size_t at, const RolltuiRegexRefs* refs,
                           RolltuiRegexMatch* m) {
  Vm vm;
  Frame stack[MAX_STACK];
  int end, g;
  if (!re || at > len) return 0;
  if (at < len && !rolltui_regex_first_byte(re, (unsigned char)line[at])) return 0;
  vm.re = re;
  vm.text = line;
  vm.len = (int)len;
  vm.refs = refs;
  vm.stack = stack;
  vm.sp = 0;
  vm.steps = 0;
  for (g = 0; g < 20; ++g) vm.caps[g] = -1;
  for (g = 0; g < 16; ++g) vm.regs[g] = -1;
  end = run(&vm, 0, (int)at);
  if (end < 0) return 0;
  if (m) {
    for (g = 0; g < 10; ++g) {
      const int s = g <= re->ngroups ? vm.caps[g * 2] : -1, e = g <= re->ngroups ? vm.caps[g * 2 + 1] : -1;
      m->start[g] = s >= 0 && e >= s ? s : -1;
      m->end[g] = s >= 0 && e >= s ? e : -1;
    }
    m->start[0] = (int)at;
    m->end[0] = end;
  }
  return 1;
}
