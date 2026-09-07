/* rolltui/c/rolltui_map.c — see rolltui_map.h. It was compiled into both configurations while a
 * C++ implementation existed, like the
 * two trees and the span store: it is DATA, and the flag chooses algorithms. */
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_str.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_terminal.h"

static size_t find_index(const RolltuiMap* m, const char* key, size_t len) {
  size_t i;
  for (i = 0; i < m->n; ++i)
    if (rolltui_str_eq(&m->e[i].key, key, len)) return i;
  return m->n;
}

void* rolltui_map_get(const RolltuiMap* m, const char* key, size_t len) {
  const size_t i = find_index(m, key, len);
  return i < m->n ? m->e[i].value : NULL;
}

void* rolltui_map_put(RolltuiMap* m, const char* key, size_t len, void* value) {
  const size_t i = find_index(m, key, len);
  if (i < m->n) {
    void* prev = m->e[i].value;
    m->e[i].value = value;
    return prev;
  }
  /* GROWING, AMORTISED, and ZEROED because a slot holds an owned key pointer: garbage there
   * would be freed as if it were a buffer (rolltui_alloc.h states exactly this case). */
  m->e = (RolltuiMapEntry*)rolltui_grow_zeroed(m->e, &m->cap, m->n + 1, sizeof *m->e);
  rolltui_str_set(&m->e[m->n].key, key, len);
  m->e[m->n].value = value;
  m->e[m->n].marked = 0;
  ++m->n;
  return NULL;
}

void* rolltui_map_take(RolltuiMap* m, const char* key, size_t len) {
  const size_t i = find_index(m, key, len);
  if (i >= m->n) return NULL;
  return rolltui_map_remove_at(m, i);
}

size_t rolltui_map_count(const RolltuiMap* m) { return m->n; }

void* rolltui_map_value_at(const RolltuiMap* m, size_t i) { return i < m->n ? m->e[i].value : NULL; }

const char* rolltui_map_key_at(const RolltuiMap* m, size_t i, size_t* len) {
  if (i >= m->n) {
    if (len) *len = 0;
    return "";
  }
  return rolltui_str_get(&m->e[i].key, len);
}

void rolltui_map_unmark_all(RolltuiMap* m) {
  size_t i;
  for (i = 0; i < m->n; ++i) m->e[i].marked = 0;
}

void rolltui_map_mark(RolltuiMap* m, const char* key, size_t len) {
  const size_t i = find_index(m, key, len);
  if (i < m->n) m->e[i].marked = 1;
}

int rolltui_map_marked_at(const RolltuiMap* m, size_t i) { return i < m->n ? m->e[i].marked : 0; }

void* rolltui_map_remove_at(RolltuiMap* m, size_t i) {
  void* v;
  RolltuiStr key;
  if (i >= m->n) return NULL;
  v = m->e[i].value;
  /* The removed slot's KEY BUFFER is carried to the end and kept, not freed: a swept cache
   * refills with the same handful of ids frame after frame, and throwing the bytes away
   * would be the reuse this port keeps finding discarded. */
  key = m->e[i].key;
  memmove(m->e + i, m->e + i + 1, (m->n - i - 1) * sizeof *m->e);
  --m->n;
  rolltui_str_clear(&key);
  m->e[m->n].key = key;
  m->e[m->n].value = NULL;
  m->e[m->n].marked = 0;
  return v;
}

void rolltui_map_clear(RolltuiMap* m) {
  size_t i;
  for (i = 0; i < m->n; ++i) {
    rolltui_str_clear(&m->e[i].key);
    m->e[i].value = NULL;
    m->e[i].marked = 0;
  }
  m->n = 0;
}

void rolltui_map_release(RolltuiMap* m) {
  size_t i;
  if (!m) return;
  for (i = 0; i < m->cap; ++i) rolltui_str_free(&m->e[i].key);
  rolltui_mem_free(m->e);
  m->e = NULL;
  m->n = 0;
  m->cap = 0;
}
