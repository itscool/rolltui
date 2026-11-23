/* rolltui/c/rolltui_str.c — see rolltui_str.h. It was compiled into both configurations while
 * a C++ implementation existed, for the
 * reason `rolltui_md_lines.c` is: this is DATA, and two copies of a data structure are two
 * things that can disagree about what a name is. The flag chooses ALGORITHMS. */
#include "rolltui/c/rolltui_str.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_terminal.h"

/* GROWING, EXACT (rolltui_alloc.h strategy 3): a name's size is known at the assign, and a
 * name is not appended to a byte at a time. `+ 1` is the NUL, which this type guarantees so
 * that every C caller can hand `s->p` to something that wants a `const char*`. */
static void reserve(RolltuiStr* s, size_t need) {
  s->p = (char*)rolltui_fit(s->p, &s->cap, need + 1, 1);
}

void rolltui_str_set(RolltuiStr* s, const char* text, size_t len) {
  if (len == 0) {
    rolltui_str_clear(s);
    return;
  }
  reserve(s, len);
  memcpy(s->p, text, len);
  s->p[len] = '\0';
  s->n = len;
}

void rolltui_str_append(RolltuiStr* s, const char* text, size_t len) {
  if (len == 0) return;
  reserve(s, s->n + len);
  memcpy(s->p + s->n, text, len);
  s->n += len;
  s->p[s->n] = '\0';
}

void rolltui_str_append_str(RolltuiStr* s, const RolltuiStr* o) {
  if (o) rolltui_str_append(s, o->p, o->n);
}

void rolltui_str_clear(RolltuiStr* s) {
  s->n = 0;
  if (s->p) s->p[0] = '\0';
}

void rolltui_str_free(RolltuiStr* s) {
  if (!s) return;
  rolltui_mem_free(s->p);
  s->p = NULL;
  s->n = 0;
  s->cap = 0;
}

void rolltui_str_list_release(RolltuiStrListRaw* l) {
  size_t i;
  if (!l) return;
  for (i = 0; i < l->cap; ++i) rolltui_str_free(&l->v[i]);
  rolltui_mem_free(l->v);
  l->v = NULL;
  l->n = 0;
  l->cap = 0;
}

void rolltui_str_list_clear(RolltuiStrListRaw* l) {
  if (l) l->n = 0; /* the entries' buffers stay, to be refilled */
}

RolltuiStr* rolltui_str_list_add(RolltuiStrListRaw* l, const char* s, size_t len) {
  RolltuiStr* e;
  if (!l) return NULL;
  if (l->n == l->cap) {
    const size_t want = l->cap ? l->cap * 2 : 8;
    l->v = (RolltuiStr*)rolltui_grow_zeroed(l->v, &l->cap, want, sizeof *l->v);
  }
  e = &l->v[l->n++];
  rolltui_str_set(e, s ? s : "", len);
  return e;
}

void rolltui_str_put(void* ctx, const char* s, size_t len) {
  if (ctx) rolltui_str_append((RolltuiStr*)ctx, s, len);
}

void rolltui_str_move(RolltuiStr* to, RolltuiStr* from) {
  if (to == from) return;
  rolltui_mem_free(to->p);
  to->p = from->p;
  to->n = from->n;
  to->cap = from->cap;
  from->p = NULL;
  from->n = 0;
  from->cap = 0;
}

int rolltui_str_eq(const RolltuiStr* s, const char* text, size_t len) {
  if (s->n != len) return 0;
  return len == 0 || memcmp(s->p, text, len) == 0;
}

const char* rolltui_str_get(const RolltuiStr* s, size_t* len) {
  if (len) *len = s->n;
  return s->p ? s->p : "";
}

/* ---- the owned pointer array ------------------------------------------------------------- */

void rolltui_ptrvec_push(RolltuiPtrVec* a, void* p) {
  /* GROWING, AMORTISED (strategy 2): appended to, final size unknown. */
  a->v = (void**)rolltui_grow(a->v, &a->cap, a->n + 1, sizeof *a->v);
  a->v[a->n++] = p;
}

void rolltui_ptrvec_insert(RolltuiPtrVec* a, size_t i, void* p) {
  if (i > a->n) i = a->n;
  a->v = (void**)rolltui_grow(a->v, &a->cap, a->n + 1, sizeof *a->v);
  memmove(a->v + i + 1, a->v + i, (a->n - i) * sizeof *a->v);
  a->v[i] = p;
  a->n++;
}

void* rolltui_ptrvec_take(RolltuiPtrVec* a, size_t i) {
  void* p;
  if (i >= a->n) return NULL;
  p = a->v[i];
  memmove(a->v + i, a->v + i + 1, (a->n - i - 1) * sizeof *a->v);
  a->n--;
  return p;
}

void rolltui_ptrvec_clear(RolltuiPtrVec* a) { a->n = 0; }

void rolltui_ptrvec_free(RolltuiPtrVec* a) {
  if (!a) return;
  rolltui_mem_free(a->v);
  a->v = NULL;
  a->n = 0;
  a->cap = 0;
}

/* How many of the first `k` bytes of `s` to keep so the last character kept is whole: the last
 * character's lead byte says how long it is, and one that runs past `k` is dropped entirely. */
static size_t whole_chars(const char* s, size_t k) {
  size_t j, need;
  unsigned char c;
  if (k == 0) return 0;
  j = k - 1;
  while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) --j;
  c = (unsigned char)s[j];
  need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
  return j + need > k ? j : k;
}

void rolltui_labelf(char* buf, size_t cap, const char* fmt, ...) {
  va_list ap;
  int n;
  size_t k;
  if (!cap) return;
  va_start(ap, fmt);
  n = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  if (n < 0) {
    buf[0] = '\0';
    return;
  }
  if ((size_t)n < cap) return;
  if (cap > 4) { /* room for the ellipsis: the text, cut whole, then `…` */
    k = whole_chars(buf, cap - 4);
    memcpy(buf + k, "\xE2\x80\xA6", 3);
    buf[k + 3] = '\0';
  } else {
    buf[whole_chars(buf, cap - 1)] = '\0';
  }
}

/* ---- numbers for a person ---------------------------------------------------------------------- */

static size_t put_out(char* out, size_t cap, int n) {
  if (!out || !cap) return 0;
  if (n < 0) {
    out[0] = 0;
    return 0;
  }
  return (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t rolltui_format_size(double n, unsigned flags, char* out, size_t cap) {
  static const char kBinary[] = "KMGTPE", kDecimal[] = "kMGTPE";
  const double step = (flags & ROLLTUI_SIZE_DECIMAL) ? 1000.0 : 1024.0;
  const char* sign = n < 0 ? "-" : "";
  const char* rate = (flags & ROLLTUI_SIZE_RATE) ? "/s" : "";
  const char* bit = (flags & ROLLTUI_SIZE_BITS) ? "b" : "";
  double v = fabs((flags & ROLLTUI_SIZE_BITS) ? n * 8.0 : n);
  int u = -1;
  if (!out || !cap) return 0;
  if (v != v) v = 0; /* not a number: nothing */
  while (v >= step && u < 5) v /= step, ++u;
  if (u >= 0 && v < 10.0 && floor(v * 10.0 + 0.5) >= 100.0) v = 10.0;            /* 9.96 is 10, written as one */
  if (u >= 0 && u < 5 && floor(v + 0.5) >= step) v /= step, ++u;                /* 1023.6K is 1.0M */
  if (u < 0) { /* under the first step: the number itself */
    const int whole = v == floor(v) || v >= 10.0;
    const char* unit = (flags & ROLLTUI_SIZE_BYTE_UNIT) ? ((flags & ROLLTUI_SIZE_BITS) ? " b" : " B") : bit;
    return put_out(out, cap, whole ? snprintf(out, cap, "%s%.0f%s%s", sign, v, unit, rate)
                                   : snprintf(out, cap, "%s%.1f%s%s", sign, v, unit, rate));
  }
  {
    const char letter = (flags & ROLLTUI_SIZE_DECIMAL) ? kDecimal[u] : kBinary[u];
    return put_out(out, cap, v < 10.0 ? snprintf(out, cap, "%s%.1f%c%s%s", sign, v, letter, bit, rate)
                                      : snprintf(out, cap, "%s%.0f%c%s%s", sign, v, letter, bit, rate));
  }
}

size_t rolltui_format_count(long long n, const char* sep, size_t sep_len, char* out, size_t cap) {
  char digits[32];
  size_t nd, i, k = 0;
  const unsigned long long a = n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n;
  if (!out || !cap) return 0;
  if (!sep || !sep_len) sep = ",", sep_len = 1;
  nd = (size_t)snprintf(digits, sizeof digits, "%llu", a);
  if (n < 0 && k + 1 < cap) out[k++] = '-';
  for (i = 0; i < nd && k + 1 < cap; ++i) {
    if (i && (nd - i) % 3 == 0) {
      if (k + sep_len >= cap) break;
      memcpy(out + k, sep, sep_len);
      k += sep_len;
    }
    out[k++] = digits[i];
  }
  out[k] = 0;
  return k;
}
