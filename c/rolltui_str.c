/* rolltui/c/rolltui_str.c — see rolltui_str.h. It was compiled into both configurations while
 * a C++ implementation existed, for the
 * reason `rolltui_md_lines.c` is: this is DATA, and two copies of a data structure are two
 * things that can disagree about what a name is. The flag chooses ALGORITHMS. */
#include "rolltui/c/rolltui_str.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"

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

/* ---- the owned pointer array ------------------------------------------------------------ */

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
