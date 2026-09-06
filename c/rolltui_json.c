/* rolltui/c/rolltui_json.c — see rolltui_json.h. The parser and dumper are a direct port of
 * `rolltui/Json.cpp`'s `Parser`/`dump_value`, preserving every message and the line-tracking
 * exactly: `rolltui/tests/theme_test.cpp` is the oracle for both (round-trip, escapes
 * including `\u` surrogate pairs, the duplicate-key error and its line number), unchanged by
 * this port. Every allocation goes through `rolltui_alloc.h`'s closed set, named at the site. */
#include "rolltui/c/rolltui_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- construction -------------------------------------------------------------------- */

static RolltuiJsonValue* value_new(unsigned char kind) {
  /* OWNED, LONG-LIVED (rolltui_alloc.h strategy 4): one node, released by `rolltui_json_free`
   * or absorbed by whichever parent takes ownership of it (`rolltui_json_set`/`_array_push`). */
  RolltuiJsonValue* v = (RolltuiJsonValue*)rolltui_mem_alloc(sizeof(RolltuiJsonValue));
  memset(v, 0, sizeof *v);
  v->kind = kind;
  return v;
}

RolltuiJsonValue* rolltui_json_null(void) { return value_new(ROLLTUI_JSON_NULL); }

RolltuiJsonValue* rolltui_json_bool(int b) {
  RolltuiJsonValue* v = value_new(ROLLTUI_JSON_BOOL);
  v->b = (unsigned char)(b != 0);
  return v;
}

RolltuiJsonValue* rolltui_json_number(double n) {
  RolltuiJsonValue* v = value_new(ROLLTUI_JSON_NUMBER);
  v->num = n;
  return v;
}

RolltuiJsonValue* rolltui_json_string(const char* s, size_t len) {
  RolltuiJsonValue* v = value_new(ROLLTUI_JSON_STRING);
  rolltui_str_set(&v->str, s, len); /* GROWING EXACT (rolltui_str.h): a name written once. */
  return v;
}

RolltuiJsonValue* rolltui_json_array(void) { return value_new(ROLLTUI_JSON_ARRAY); }
RolltuiJsonValue* rolltui_json_object(void) { return value_new(ROLLTUI_JSON_OBJECT); }

void rolltui_json_free(RolltuiJsonValue* v) {
  size_t i;
  if (!v) return;
  /* Unconditional: every container is released regardless of `kind`, so retagging a value
   * (`rolltui_json_set` always does) can never orphan whichever one is no longer "the" one —
   * see the header comment on why this struct is not gated the way a real union would be. */
  rolltui_str_free(&v->str);
  for (i = 0; i < v->arr_n; ++i) rolltui_json_free(v->arr[i]);
  rolltui_mem_free(v->arr);
  for (i = 0; i < v->obj_n; ++i) {
    rolltui_str_free(&v->obj[i]->key);
    rolltui_json_free(v->obj[i]->value);
    rolltui_mem_free(v->obj[i]);
  }
  rolltui_mem_free(v->obj);
  rolltui_mem_free(v);
}

RolltuiJsonValue* rolltui_json_clone(const RolltuiJsonValue* v) {
  RolltuiJsonValue* c;
  size_t i;
  if (!v) return NULL;
  c = value_new(v->kind);
  c->b = v->b;
  c->num = v->num;
  rolltui_str_set(&c->str, v->str.p, v->str.n);
  for (i = 0; i < v->arr_n; ++i) {
    /* GROWING AMORTISED (rolltui_alloc.h strategy 2), laid out as RolltuiPtrVec. */
    c->arr = (RolltuiJsonValue**)rolltui_grow(c->arr, &c->arr_cap, c->arr_n + 1, sizeof *c->arr);
    c->arr[c->arr_n++] = rolltui_json_clone(v->arr[i]);
  }
  for (i = 0; i < v->obj_n; ++i) {
    RolltuiJsonMember* m = (RolltuiJsonMember*)rolltui_mem_alloc(sizeof(RolltuiJsonMember));
    memset(m, 0, sizeof *m);
    rolltui_str_set(&m->key, v->obj[i]->key.p, v->obj[i]->key.n);
    m->value = rolltui_json_clone(v->obj[i]->value);
    c->obj = (RolltuiJsonMember**)rolltui_grow(c->obj, &c->obj_cap, c->obj_n + 1, sizeof *c->obj);
    c->obj[c->obj_n++] = m;
  }
  return c;
}

int rolltui_json_equal(const RolltuiJsonValue* a, const RolltuiJsonValue* b) {
  size_t i;
  if (a == b) return 1;
  if (!a || !b) return 0;
  if (a->kind != b->kind || a->b != b->b || a->num != b->num) return 0;
  if (!rolltui_str_eq(&a->str, b->str.p, b->str.n)) return 0;
  if (a->arr_n != b->arr_n) return 0;
  for (i = 0; i < a->arr_n; ++i)
    if (!rolltui_json_equal(a->arr[i], b->arr[i])) return 0;
  if (a->obj_n != b->obj_n) return 0;
  for (i = 0; i < a->obj_n; ++i) {
    if (!rolltui_str_eq(&a->obj[i]->key, b->obj[i]->key.p, b->obj[i]->key.n)) return 0;
    if (!rolltui_json_equal(a->obj[i]->value, b->obj[i]->value)) return 0;
  }
  return 1;
}

int rolltui_json_is_null(const RolltuiJsonValue* v) { return !v || v->kind == ROLLTUI_JSON_NULL; }
int rolltui_json_is_bool(const RolltuiJsonValue* v) { return v && v->kind == ROLLTUI_JSON_BOOL; }
int rolltui_json_is_number(const RolltuiJsonValue* v) { return v && v->kind == ROLLTUI_JSON_NUMBER; }
int rolltui_json_is_string(const RolltuiJsonValue* v) { return v && v->kind == ROLLTUI_JSON_STRING; }
int rolltui_json_is_array(const RolltuiJsonValue* v) { return v && v->kind == ROLLTUI_JSON_ARRAY; }
int rolltui_json_is_object(const RolltuiJsonValue* v) { return v && v->kind == ROLLTUI_JSON_OBJECT; }

const char* rolltui_json_as_string(const RolltuiJsonValue* v, const char* def, size_t def_len, size_t* out_len) {
  if (v && v->kind == ROLLTUI_JSON_STRING) {
    if (out_len) *out_len = v->str.n;
    return v->str.p ? v->str.p : "";
  }
  if (out_len) *out_len = def_len;
  return def;
}

double rolltui_json_as_number(const RolltuiJsonValue* v, double def) {
  return (v && v->kind == ROLLTUI_JSON_NUMBER) ? v->num : def;
}

int rolltui_json_as_bool(const RolltuiJsonValue* v, int def) {
  return (v && v->kind == ROLLTUI_JSON_BOOL) ? (v->b != 0) : def;
}

/* Absent/wrong-kind lookups return this: a BORROW, valid forever, never allocated — the same
 * `static const Value kNull{};` sentinel `Value::get` already used. A `static` object with no
 * initialiser is zero-initialised in C, so every field (kind included, 0 == ROLLTUI_JSON_NULL)
 * is correct with nothing spelled out. */
static const RolltuiJsonValue kNull;

const RolltuiJsonValue* rolltui_json_get(const RolltuiJsonValue* v, const char* key, size_t key_len) {
  size_t i;
  if (!v || v->kind != ROLLTUI_JSON_OBJECT) return &kNull;
  for (i = 0; i < v->obj_n; ++i)
    if (rolltui_str_eq(&v->obj[i]->key, key, key_len)) return v->obj[i]->value;
  return &kNull;
}

int rolltui_json_has(const RolltuiJsonValue* v, const char* key, size_t key_len) {
  size_t i;
  if (!v || v->kind != ROLLTUI_JSON_OBJECT) return 0;
  for (i = 0; i < v->obj_n; ++i)
    if (rolltui_str_eq(&v->obj[i]->key, key, key_len)) return 1;
  return 0;
}

int rolltui_json_object_erase(RolltuiJsonValue* v, const char* key, size_t key_len) {
  size_t i, j;
  if (!v || v->kind != ROLLTUI_JSON_OBJECT) return 0;
  for (i = 0; i < v->obj_n; ++i) {
    if (!rolltui_str_eq(&v->obj[i]->key, key, key_len)) continue;
    rolltui_str_free(&v->obj[i]->key);
    rolltui_json_free(v->obj[i]->value);
    rolltui_mem_free(v->obj[i]);
    /* Order-preserving: shift the tail down, exactly as the `std::remove_if` + `erase` this
     * replaces did. An object's key order is what `dump` writes, so a swap-with-last would
     * silently reorder every file this touches. */
    for (j = i + 1; j < v->obj_n; ++j) v->obj[j - 1] = v->obj[j];
    --v->obj_n;
    return 1;
  }
  return 0;
}

RolltuiJsonValue* rolltui_json_set(RolltuiJsonValue* v, const char* key, size_t key_len, RolltuiJsonValue* child) {
  size_t i;
  v->kind = ROLLTUI_JSON_OBJECT;
  for (i = 0; i < v->obj_n; ++i) {
    if (rolltui_str_eq(&v->obj[i]->key, key, key_len)) {
      rolltui_json_free(v->obj[i]->value);
      v->obj[i]->value = child;
      return child;
    }
  }
  {
    RolltuiJsonMember* m = (RolltuiJsonMember*)rolltui_mem_alloc(sizeof(RolltuiJsonMember));
    memset(m, 0, sizeof *m);
    rolltui_str_set(&m->key, key, key_len);
    m->value = child;
    v->obj = (RolltuiJsonMember**)rolltui_grow(v->obj, &v->obj_cap, v->obj_n + 1, sizeof *v->obj);
    v->obj[v->obj_n++] = m;
  }
  return child;
}

size_t rolltui_json_array_size(const RolltuiJsonValue* v) { return v ? v->arr_n : 0; }

RolltuiJsonValue* rolltui_json_array_at(const RolltuiJsonValue* v, size_t i) {
  return (v && i < v->arr_n) ? v->arr[i] : NULL;
}

void rolltui_json_array_push(RolltuiJsonValue* v, RolltuiJsonValue* child) {
  v->arr = (RolltuiJsonValue**)rolltui_grow(v->arr, &v->arr_cap, v->arr_n + 1, sizeof *v->arr);
  v->arr[v->arr_n++] = child;
}

size_t rolltui_json_object_size(const RolltuiJsonValue* v) { return v ? v->obj_n : 0; }

const char* rolltui_json_object_key_at(const RolltuiJsonValue* v, size_t i, size_t* len) {
  if (!v || i >= v->obj_n) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = v->obj[i]->key.n;
  return v->obj[i]->key.p ? v->obj[i]->key.p : "";
}

RolltuiJsonValue* rolltui_json_object_value_at(const RolltuiJsonValue* v, size_t i) {
  return (v && i < v->obj_n) ? v->obj[i]->value : NULL;
}

/* ---- the parser ------------------------------------------------------------------------
 * A direct port of `Json.cpp`'s `Parser`: same fields, same order of operations, same
 * messages. `jp_parse_value` returns the parsed node (owned) or NULL, where the C++ used an
 * out-parameter and a bool — the shape a caller-owned tree wants in C — and every failure
 * path below frees exactly what IT allocated before propagating NULL, since there is no
 * destructor to do it for a partially built object/array the way there was in C++. */

typedef struct {
  const char* s;
  size_t len;
  size_t i;
  int line;
  RolltuiStr error; /* the FIRST failure only; owned, moved out by rolltui_json_parse */
} JParser;

/* A literal C string plus its length, computed once here rather than hand-counted at every
 * call site — a hand-counted length is exactly the kind of thing that is right until the
 * message is edited and nothing then notices it is not. */
#define JLIT(s) (s), strlen(s)

static int jp_fail(JParser* p, const char* msg, size_t msg_len) {
  if (p->error.n == 0) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "line %d: ", p->line);
    rolltui_str_set(&p->error, buf, (size_t)(n > 0 ? n : 0));
    rolltui_str_append(&p->error, msg, msg_len);
  }
  return 0;
}

static void jp_ws(JParser* p) {
  while (p->i < p->len) {
    char c = p->s[p->i];
    if (c == '\n') {
      ++p->line;
      ++p->i;
    } else if (c == ' ' || c == '\t' || c == '\r') {
      ++p->i;
    } else {
      break;
    }
  }
}

static int jp_expect(JParser* p, char c) {
  jp_ws(p);
  if (p->i < p->len && p->s[p->i] == c) {
    ++p->i;
    return 1;
  }
  {
    char buf[64];
    size_t n;
    if (p->i < p->len) n = (size_t)snprintf(buf, sizeof buf, "expected '%c' but found '%c'", c, p->s[p->i]);
    else n = (size_t)snprintf(buf, sizeof buf, "expected '%c' at end of input", c);
    return jp_fail(p, buf, n);
  }
}

static int jp_hex4(JParser* p, RolltuiCodepoint* v) {
  int k;
  if (p->i + 4 > p->len) return 0;
  *v = 0;
  for (k = 0; k < 4; ++k) {
    char h = p->s[p->i++];
    int d;
    if (h >= '0' && h <= '9') d = h - '0';
    else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
    else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
    else return 0;
    *v = (RolltuiCodepoint)((*v << 4) | (unsigned)d);
  }
  return 1;
}

static int jp_parse_string(JParser* p, RolltuiStr* out) {
  if (p->i >= p->len || p->s[p->i] != '"') return jp_fail(p, JLIT("expected a string"));
  ++p->i;
  while (p->i < p->len) {
    char c = p->s[p->i++];
    if (c == '"') return 1;
    if (c == '\n') return jp_fail(p, JLIT("newline inside a string"));
    if (c != '\\') {
      rolltui_str_append(out, &c, 1);
      continue;
    }
    if (p->i >= p->len) return jp_fail(p, JLIT("unterminated escape"));
    {
      char e = p->s[p->i++];
      switch (e) {
        case '"': rolltui_str_append(out, "\"", 1); break;
        case '\\': rolltui_str_append(out, "\\", 1); break;
        case '/': rolltui_str_append(out, "/", 1); break;
        case 'b': rolltui_str_append(out, "\b", 1); break;
        case 'f': rolltui_str_append(out, "\f", 1); break;
        case 'n': rolltui_str_append(out, "\n", 1); break;
        case 'r': rolltui_str_append(out, "\r", 1); break;
        case 't': rolltui_str_append(out, "\t", 1); break;
        case 'u': {
          RolltuiCodepoint v, lo;
          char utf8[4];
          size_t n;
          if (!jp_hex4(p, &v)) return jp_fail(p, JLIT("bad \\u escape"));
          if (v >= 0xD800 && v <= 0xDBFF) { /* surrogate pair */
            if (p->i + 6 <= p->len && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
              p->i += 2;
              if (!jp_hex4(p, &lo) || lo < 0xDC00 || lo > 0xDFFF) return jp_fail(p, JLIT("bad surrogate pair"));
              v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              v = 0xFFFD;
            }
          } else if (v >= 0xDC00 && v <= 0xDFFF) {
            v = 0xFFFD;
          }
          n = rolltui_u_append_utf8(v, utf8);
          rolltui_str_append(out, utf8, n);
          break;
        }
        default: {
          char buf[16];
          size_t n = (size_t)snprintf(buf, sizeof buf, "unknown escape \\%c", e);
          return jp_fail(p, buf, n);
        }
      }
    }
  }
  return jp_fail(p, JLIT("unterminated string"));
}

static RolltuiJsonValue* jp_parse_value(JParser* p, int depth) {
  char c;
  if (depth > 200) {
    jp_fail(p, JLIT("nesting too deep"));
    return NULL;
  }
  jp_ws(p);
  if (p->i >= p->len) {
    jp_fail(p, JLIT("unexpected end of input"));
    return NULL;
  }
  c = p->s[p->i];

  if (c == '{') {
    RolltuiJsonValue* out = rolltui_json_object();
    ++p->i;
    jp_ws(p);
    if (p->i < p->len && p->s[p->i] == '}') {
      ++p->i;
      return out;
    }
    for (;;) {
      RolltuiStr key;
      RolltuiJsonValue* v;
      size_t k;
      int dup = 0;
      memset(&key, 0, sizeof key);
      jp_ws(p);
      if (!jp_parse_string(p, &key)) {
        rolltui_str_free(&key);
        rolltui_json_free(out);
        return NULL;
      }
      if (!jp_expect(p, ':')) {
        rolltui_str_free(&key);
        rolltui_json_free(out);
        return NULL;
      }
      v = jp_parse_value(p, depth + 1);
      if (!v) {
        rolltui_str_free(&key);
        rolltui_json_free(out);
        return NULL;
      }
      for (k = 0; k < out->obj_n; ++k)
        if (rolltui_str_eq(&out->obj[k]->key, key.p, key.n)) {
          dup = 1;
          break;
        }
      if (dup) {
        char buf[256];
        size_t n = (size_t)snprintf(buf, sizeof buf, "duplicate key \"%.*s\"", (int)key.n, key.p ? key.p : "");
        jp_fail(p, buf, n);
        rolltui_str_free(&key);
        rolltui_json_free(v);
        rolltui_json_free(out);
        return NULL;
      }
      {
        RolltuiJsonMember* m = (RolltuiJsonMember*)rolltui_mem_alloc(sizeof(RolltuiJsonMember));
        memset(m, 0, sizeof *m);
        rolltui_str_move(&m->key, &key); /* the `std::move(key)` into `emplace_back` */
        m->value = v;
        out->obj = (RolltuiJsonMember**)rolltui_grow(out->obj, &out->obj_cap, out->obj_n + 1, sizeof *out->obj);
        out->obj[out->obj_n++] = m;
      }
      jp_ws(p);
      if (p->i < p->len && p->s[p->i] == ',') {
        ++p->i;
        continue;
      }
      if (p->i < p->len && p->s[p->i] == '}') {
        ++p->i;
        return out;
      }
      jp_fail(p, JLIT("expected ',' or '}' in object"));
      rolltui_json_free(out);
      return NULL;
    }
  }

  if (c == '[') {
    RolltuiJsonValue* out = rolltui_json_array();
    ++p->i;
    jp_ws(p);
    if (p->i < p->len && p->s[p->i] == ']') {
      ++p->i;
      return out;
    }
    for (;;) {
      RolltuiJsonValue* v = jp_parse_value(p, depth + 1);
      if (!v) {
        rolltui_json_free(out);
        return NULL;
      }
      out->arr = (RolltuiJsonValue**)rolltui_grow(out->arr, &out->arr_cap, out->arr_n + 1, sizeof *out->arr);
      out->arr[out->arr_n++] = v;
      jp_ws(p);
      if (p->i < p->len && p->s[p->i] == ',') {
        ++p->i;
        continue;
      }
      if (p->i < p->len && p->s[p->i] == ']') {
        ++p->i;
        return out;
      }
      jp_fail(p, JLIT("expected ',' or ']' in array"));
      rolltui_json_free(out);
      return NULL;
    }
  }

  if (c == '"') {
    RolltuiStr str;
    RolltuiJsonValue* out;
    memset(&str, 0, sizeof str);
    if (!jp_parse_string(p, &str)) {
      rolltui_str_free(&str);
      return NULL;
    }
    out = value_new(ROLLTUI_JSON_STRING);
    rolltui_str_move(&out->str, &str);
    return out;
  }

  if (p->len - p->i >= 4 && memcmp(p->s + p->i, "true", 4) == 0) {
    p->i += 4;
    return rolltui_json_bool(1);
  }
  if (p->len - p->i >= 5 && memcmp(p->s + p->i, "false", 5) == 0) {
    p->i += 5;
    return rolltui_json_bool(0);
  }
  if (p->len - p->i >= 4 && memcmp(p->s + p->i, "null", 4) == 0) {
    p->i += 4;
    return rolltui_json_null();
  }

  if (c == '-' || (c >= '0' && c <= '9')) {
    size_t start = p->i;
    char stackbuf[64];
    char* numbuf = stackbuf;
    int heap = 0;
    size_t numlen;
    char* end = NULL;
    double d;
    RolltuiJsonValue* out;
    if (p->s[p->i] == '-') ++p->i;
    while (p->i < p->len && ((p->s[p->i] >= '0' && p->s[p->i] <= '9') || p->s[p->i] == '.' || p->s[p->i] == 'e' ||
                             p->s[p->i] == 'E' || p->s[p->i] == '+' || p->s[p->i] == '-'))
      ++p->i;
    numlen = p->i - start;
    /* VALUE/INLINE with a stated SPILL (rolltui_alloc.h strategy 1): a JSON number is
     * almost always under 63 bytes; a pathologically long digit run spills to the heap so
     * strtod() still gets a NUL-terminated copy — the long case is handled, not assumed
     * away, and the common case allocates nothing. */
    if (numlen >= sizeof stackbuf) {
      numbuf = (char*)rolltui_mem_alloc(numlen + 1);
      heap = 1;
    }
    memcpy(numbuf, p->s + start, numlen);
    numbuf[numlen] = '\0';
    d = strtod(numbuf, &end);
    if (!end || *end != '\0' || (numlen == 1 && numbuf[0] == '-')) {
      char buf[128];
      size_t n = (size_t)snprintf(buf, sizeof buf, "bad number '%.*s'", (int)numlen, numbuf);
      jp_fail(p, buf, n);
      if (heap) rolltui_mem_free(numbuf);
      return NULL;
    }
    if (heap) rolltui_mem_free(numbuf);
    out = value_new(ROLLTUI_JSON_NUMBER);
    out->num = d;
    return out;
  }

  {
    char buf[48];
    size_t n = (size_t)snprintf(buf, sizeof buf, "unexpected character '%c'", c);
    jp_fail(p, buf, n);
  }
  return NULL;
}

RolltuiJsonValue* rolltui_json_parse(const char* text, size_t len, RolltuiStr* error) {
  JParser p;
  RolltuiJsonValue* v;
  memset(&p, 0, sizeof p);
  p.s = text;
  p.len = len;
  p.line = 1;
  if (error) rolltui_str_clear(error);
  v = jp_parse_value(&p, 0);
  if (!v) {
    if (error) rolltui_str_move(error, &p.error);
    else rolltui_str_free(&p.error);
    return NULL;
  }
  jp_ws(&p);
  if (p.i != p.len) {
    jp_fail(&p, JLIT("trailing characters after the value"));
    rolltui_json_free(v);
    if (error) rolltui_str_move(error, &p.error);
    else rolltui_str_free(&p.error);
    return NULL;
  }
  rolltui_str_free(&p.error); /* empty on success; freed defensively regardless */
  return v;
}

/* ---- the dumper --------------------------------------------------------------------------
 * Builds into a private, GROWING AMORTISED scratch buffer (rolltui_alloc.h strategy 2) —
 * `rolltui_str_append`'s own growth is EXACT (rolltui_str.h), which would realloc once per
 * fragment on a tree of any size — and copies the finished bytes into the caller's `RolltuiStr`
 * ONCE at the end, matching `dump()` returning one finished `std::string`. */

typedef struct {
  char* p;
  size_t len, cap;
} JBuf;

static void jbuf_add(JBuf* b, const char* s, size_t n) {
  if (!n) return;
  b->p = (char*)rolltui_grow(b->p, &b->cap, b->len + n, sizeof *b->p);
  memcpy(b->p + b->len, s, n);
  b->len += n;
}

static void jbuf_pad(JBuf* b, int indent, int level) {
  size_t n = (size_t)(indent * level);
  if (!n) return;
  b->p = (char*)rolltui_grow(b->p, &b->cap, b->len + n, sizeof *b->p);
  memset(b->p + b->len, ' ', n);
  b->len += n;
}

static void jbuf_free(JBuf* b) {
  rolltui_mem_free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
}

static void jdump_string(JBuf* o, const char* s, size_t n) {
  size_t i;
  jbuf_add(o, "\"", 1);
  for (i = 0; i < n; ++i) {
    char c = s[i];
    switch (c) {
      case '"': jbuf_add(o, "\\\"", 2); break;
      case '\\': jbuf_add(o, "\\\\", 2); break;
      case '\n': jbuf_add(o, "\\n", 2); break;
      case '\r': jbuf_add(o, "\\r", 2); break;
      case '\t': jbuf_add(o, "\\t", 2); break;
      default:
        if ((unsigned char)c < 0x20) {
          char buf[8];
          int m = snprintf(buf, sizeof buf, "\\u%04X", (unsigned)(unsigned char)c);
          jbuf_add(o, buf, (size_t)m);
        } else {
          jbuf_add(o, &c, 1);
        }
    }
  }
  jbuf_add(o, "\"", 1);
}

static void jdump_value(JBuf* o, const RolltuiJsonValue* v, int indent, int level) {
  switch (v->kind) {
    case ROLLTUI_JSON_NULL: jbuf_add(o, "null", 4); break;
    case ROLLTUI_JSON_BOOL:
      if (v->b) jbuf_add(o, "true", 4);
      else jbuf_add(o, "false", 5);
      break;
    case ROLLTUI_JSON_NUMBER: {
      char buf[32];
      int n;
      if (floor(v->num) == v->num && fabs(v->num) < 1e15) n = snprintf(buf, sizeof buf, "%.0f", v->num);
      else n = snprintf(buf, sizeof buf, "%.17g", v->num);
      jbuf_add(o, buf, (size_t)n);
      break;
    }
    case ROLLTUI_JSON_STRING: jdump_string(o, v->str.p ? v->str.p : "", v->str.n); break;
    case ROLLTUI_JSON_ARRAY: {
      size_t k;
      if (v->arr_n == 0) {
        jbuf_add(o, "[]", 2);
        break;
      }
      jbuf_add(o, "[", 1);
      if (indent > 0) jbuf_add(o, "\n", 1);
      for (k = 0; k < v->arr_n; ++k) {
        jbuf_pad(o, indent, level + 1);
        jdump_value(o, v->arr[k], indent, level + 1);
        if (k + 1 < v->arr_n) jbuf_add(o, ",", 1);
        if (indent > 0) jbuf_add(o, "\n", 1);
      }
      jbuf_pad(o, indent, level);
      jbuf_add(o, "]", 1);
      break;
    }
    case ROLLTUI_JSON_OBJECT: {
      size_t k;
      if (v->obj_n == 0) {
        jbuf_add(o, "{}", 2);
        break;
      }
      jbuf_add(o, "{", 1);
      if (indent > 0) jbuf_add(o, "\n", 1);
      for (k = 0; k < v->obj_n; ++k) {
        jbuf_pad(o, indent, level + 1);
        jdump_string(o, v->obj[k]->key.p ? v->obj[k]->key.p : "", v->obj[k]->key.n);
        if (indent > 0) jbuf_add(o, ": ", 2);
        else jbuf_add(o, ":", 1);
        jdump_value(o, v->obj[k]->value, indent, level + 1);
        if (k + 1 < v->obj_n) jbuf_add(o, ",", 1);
        if (indent > 0) jbuf_add(o, "\n", 1);
      }
      jbuf_pad(o, indent, level);
      jbuf_add(o, "}", 1);
      break;
    }
    default: break; /* unreachable: kind is always one of the six above */
  }
}

void rolltui_json_dump(const RolltuiJsonValue* v, int indent, RolltuiStr* out) {
  JBuf b;
  memset(&b, 0, sizeof b);
  jdump_value(&b, v, indent, 0);
  rolltui_str_set(out, b.p, b.len);
  jbuf_free(&b);
}
