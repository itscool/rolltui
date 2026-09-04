/* rolltui/c/rolltui_bindings.c — the C side of chords and the binding table. See
 * rolltui_bindings.h for the boundary's rules and rolltui/Bindings.hpp for the binding
 * rules themselves; `BindingsCpp.cpp` is the other implementation of the same functions,
 * and `rolltui/tests/bindings_test.cpp` is the oracle for both.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`. A table is one handle
 * and holds nothing process-wide, so nothing here registers a shutdown releaser. */
#include "rolltui/c/rolltui_bindings.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- the key-name table ------------------------------------------------------------------ */
/* THIS MODULE'S OWN FILE FORMAT, which is why it is here and `library_actions()` is not: a
 * chord's spelling is what a bindings file says, and this is the parser for it. The four
 * aliases ("esc", "pgup", "pgdn", "del") parse and never print, so a round trip is
 * canonical; "space" likewise, because it prints as itself and decodes to U+0020. */
typedef struct {
  const char* name;
  unsigned char key;
  unsigned char canonical; /* 0 for an alias: parsed, never printed */
} KeyName;

static const KeyName kKeyNames[] = {
    {"enter", ROLLTUI_KEY_ENTER, 1},     {"tab", ROLLTUI_KEY_TAB, 1},
    {"backspace", ROLLTUI_KEY_BACKSPACE, 1}, {"escape", ROLLTUI_KEY_ESCAPE, 1},
    {"esc", ROLLTUI_KEY_ESCAPE, 0},      {"up", ROLLTUI_KEY_UP, 1},
    {"down", ROLLTUI_KEY_DOWN, 1},       {"left", ROLLTUI_KEY_LEFT, 1},
    {"right", ROLLTUI_KEY_RIGHT, 1},     {"home", ROLLTUI_KEY_HOME, 1},
    {"end", ROLLTUI_KEY_END, 1},         {"pageup", ROLLTUI_KEY_PAGEUP, 1},
    {"pagedown", ROLLTUI_KEY_PAGEDOWN, 1}, {"pgup", ROLLTUI_KEY_PAGEUP, 0},
    {"pgdn", ROLLTUI_KEY_PAGEDOWN, 0},   {"insert", ROLLTUI_KEY_INSERT, 1},
    {"delete", ROLLTUI_KEY_DELETE, 1},   {"del", ROLLTUI_KEY_DELETE, 0},
    {"space", ROLLTUI_KEY_CHAR, 0},      {"f1", ROLLTUI_KEY_F1, 1},
    {"f2", ROLLTUI_KEY_F1 + 1, 1},       {"f3", ROLLTUI_KEY_F1 + 2, 1},
    {"f4", ROLLTUI_KEY_F1 + 3, 1},       {"f5", ROLLTUI_KEY_F1 + 4, 1},
    {"f6", ROLLTUI_KEY_F1 + 5, 1},       {"f7", ROLLTUI_KEY_F1 + 6, 1},
    {"f8", ROLLTUI_KEY_F1 + 7, 1},       {"f9", ROLLTUI_KEY_F1 + 8, 1},
    {"f10", ROLLTUI_KEY_F1 + 9, 1},      {"f11", ROLLTUI_KEY_F1 + 10, 1},
    {"f12", ROLLTUI_KEY_F12, 1},
};
#define ROLLTUI_KEY_NAME_COUNT (sizeof kKeyNames / sizeof kKeyNames[0])

static char lower_char(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* Case-insensitive compare of a bounded slice against a NUL-terminated name. */
static int name_is(const char* s, size_t len, const char* name) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (name[i] == '\0' || lower_char(s[i]) != name[i]) return 0;
  }
  return name[len] == '\0';
}

static const char* canonical_key_name(unsigned char key) {
  size_t i;
  for (i = 0; i < ROLLTUI_KEY_NAME_COUNT; ++i)
    if (kKeyNames[i].key == key && kKeyNames[i].canonical) return kKeyNames[i].name;
  return NULL;
}

int rolltui_chord_parse(const char* text, size_t len, RolltuiChord* out) {
  /* Split on '+', but a trailing "+" alone is the plus character. Done in place, over
   * (offset, length) pairs, so there is no vector of strings to own. */
  size_t part_at[8], part_len[8], n = 0, i, cur = 0;
  RolltuiChord k;
  memset(&k, 0, sizeof k);
  k.key = ROLLTUI_KEY_CHAR;
  for (i = 0; i < len; ++i) {
    if (text[i] == '+' && i > cur && i + 1 < len) {
      if (n >= 8) return 0;
      part_at[n] = cur;
      part_len[n] = i - cur;
      ++n;
      cur = i + 1;
    }
  }
  if (cur >= len) return 0;
  if (n >= 8) return 0;
  part_at[n] = cur;
  part_len[n] = len - cur;
  ++n;
  for (i = 0; i + 1 < n; ++i) {
    const char* m = text + part_at[i];
    const size_t ml = part_len[i];
    if (name_is(m, ml, "ctrl") || name_is(m, ml, "control") || name_is(m, ml, "c")) k.ctrl = 1;
    else if (name_is(m, ml, "alt") || name_is(m, ml, "meta") || name_is(m, ml, "option") || name_is(m, ml, "m")) k.alt = 1;
    else if (name_is(m, ml, "shift") || name_is(m, ml, "s")) k.shift = 1;
    else return 0;
  }
  {
    const char* last = text + part_at[n - 1];
    const size_t last_len = part_len[n - 1];
    RolltuiDecodedChar d;
    size_t consumed;
    for (i = 0; i < ROLLTUI_KEY_NAME_COUNT; ++i)
      if (name_is(last, last_len, kKeyNames[i].name)) {
        k.key = kKeyNames[i].key;
        if (name_is(last, last_len, "space")) {
          k.key = ROLLTUI_KEY_CHAR;
          k.ch = ' ';
        }
        *out = k;
        return 1;
      }
    /* One code point, and it must be printable: a chord names a key, not a control byte. */
    rolltui_u_decode_one(last, last_len, 0, &d);
    consumed = d.length;
    if (consumed != last_len || d.cp < 0x20) return 0;
    k.key = ROLLTUI_KEY_CHAR;
    /* A chord is spelled case-insensitively and stored lowercase — "Ctrl+P" and "ctrl+p"
     * are one chord, and `shift` is the only thing that ever says otherwise. ASCII only:
     * case is a property of a script, and this is a key name rather than prose. */
    k.ch = (d.cp >= 'A' && d.cp <= 'Z') ? d.cp - 'A' + 'a' : d.cp;
    *out = k;
    return 1;
  }
}

static size_t put(char* out, size_t n, const char* s) {
  const size_t len = strlen(s);
  memcpy(out + n, s, len);
  return n + len;
}

size_t rolltui_chord_to_string(const RolltuiChord* k, char* out, size_t cap) {
  size_t n = 0;
  const char* name;
  if (cap < ROLLTUI_CHORD_STRING_MAX) return 0;
  if (k->key == ROLLTUI_KEY_UNKNOWN) return 0;
  if (k->ctrl) n = put(out, n, "ctrl+");
  if (k->alt) n = put(out, n, "alt+");
  if (k->shift) n = put(out, n, "shift+");
  if (k->key == ROLLTUI_KEY_CHAR) {
    if (k->ch == ' ') return put(out, n, "space");
    return n + rolltui_u_append_utf8(k->ch, out + n);
  }
  name = canonical_key_name(k->key);
  return name ? put(out, n, name) : n;
}

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

size_t rolltui_chord_display(const RolltuiChord* k, char* out, size_t cap) {
  /* Capitalise each part, join with '-': "Ctrl-Shift-Left", "Alt-Enter", "F1", "?". */
  char s[ROLLTUI_CHORD_STRING_MAX];
  size_t len, i, start = 0, n = 0;
  if (cap < ROLLTUI_CHORD_STRING_MAX) return 0;
  len = rolltui_chord_to_string(k, s, sizeof s);
  for (i = 0; i <= len; ++i) {
    if (i != len && (s[i] != '+' || i == start)) continue;
    {
      const size_t plen = i - start;
      if (plen == 0) continue;
      if (n) out[n++] = '-';
      /* "pageup" and "pagedown" have a shorter help form; every other part is simply
       * capitalised when it is a word or a single letter. */
      if (plen == 6 && memcmp(s + start, "pageup", 6) == 0) {
        n = put(out, n, "PgUp");
      } else if (plen == 8 && memcmp(s + start, "pagedown", 8) == 0) {
        n = put(out, n, "PgDn");
      } else {
        memcpy(out + n, s + start, plen);
        if (plen > 1 || is_alpha(s[start]))
          out[n] = (char)(out[n] >= 'a' && out[n] <= 'z' ? out[n] - 'a' + 'A' : out[n]);
        n += plen;
      }
      start = i + 1;
    }
  }
  return n;
}

const char* rolltui_bindings_scope_of(const char* action, size_t len, size_t* out_len) {
  size_t i;
  for (i = 0; i < len; ++i)
    if (action[i] == '.') {
      *out_len = i;
      return action;
    }
  *out_len = len;
  return action;
}

/* ---- the table ----------------------------------------------------------------------------- */

/* One owned, NUL-free byte string. A name and a description are both this; growing one is a
 * fresh allocation rather than a realloc, because neither is ever appended to. */
typedef struct {
  char* p;
  size_t len;
} Str;

static void str_set(Str* s, const char* p, size_t len) {
  rolltui_mem_free(s->p);
  s->p = (char*)rolltui_mem_alloc(len == 0 ? 1 : len);
  if (len) memcpy(s->p, p, len);
  s->len = len;
}

static void str_free(Str* s) {
  rolltui_mem_free(s->p);
  s->p = NULL;
  s->len = 0;
}

static int str_is(const Str* s, const char* p, size_t len) {
  return s->len == len && (len == 0 || memcmp(s->p, p, len) == 0);
}

typedef struct {
  Str action;
  RolltuiChord* chords;
  size_t chord_count, chords_cap;
} Row;

struct RolltuiBindings {
  /* The DECLARED actions, with their descriptions. */
  Str* actions;
  Str* descriptions;
  size_t action_count, action_cap, desc_cap;
  /* The ROWS. Separate from the declarations because a row may outlive one — see the
   * header. */
  Row* rows;
  size_t row_count, rows_cap;
  /* The bare-Enter rule's subject, and the scope derived from it. Empty: the rule is off. */
  Str enter_action;
};

RolltuiBindings* rolltui_bindings_new(void) {
  RolltuiBindings* b = (RolltuiBindings*)rolltui_mem_alloc(sizeof(RolltuiBindings));
  memset(b, 0, sizeof *b);
  return b;
}

void rolltui_bindings_free(RolltuiBindings* b) {
  size_t i;
  if (!b) return;
  for (i = 0; i < b->action_count; ++i) {
    str_free(&b->actions[i]);
    str_free(&b->descriptions[i]);
  }
  for (i = 0; i < b->row_count; ++i) {
    str_free(&b->rows[i].action);
    rolltui_mem_free(b->rows[i].chords);
  }
  rolltui_mem_free(b->actions);
  rolltui_mem_free(b->descriptions);
  rolltui_mem_free(b->rows);
  str_free(&b->enter_action);
  rolltui_mem_free(b);
}

void rolltui_bindings_set_enter_rule(RolltuiBindings* b, const char* action, size_t len) {
  str_set(&b->enter_action, action, len);
}

static Row* find_row(const RolltuiBindings* b, const char* action, size_t len) {
  size_t i;
  for (i = 0; i < b->row_count; ++i)
    if (str_is(&b->rows[i].action, action, len)) return &b->rows[i];
  return NULL;
}

static Row* make_row(RolltuiBindings* b, const char* action, size_t len) {
  Row* r = find_row(b, action, len);
  if (r) return r;
  b->rows = (Row*)rolltui_grow_zeroed(b->rows, &b->rows_cap, b->row_count + 1, sizeof *b->rows);
  r = &b->rows[b->row_count++];
  memset(r, 0, sizeof *r);
  str_set(&r->action, action, len);
  return r;
}

int rolltui_bindings_has(const RolltuiBindings* b, const char* action, size_t len) {
  size_t i;
  for (i = 0; i < b->action_count; ++i)
    if (str_is(&b->actions[i], action, len)) return 1;
  return 0;
}

void rolltui_bindings_add_action(RolltuiBindings* b, const char* action, size_t alen, const char* desc, size_t dlen) {
  if (rolltui_bindings_has(b, action, alen)) return;
  /* Two caps, not one shared: `rolltui_grow` is a no-op when the capacity it is told
   * already suffices, so passing the first array's grown capacity for the second would
   * leave the second unallocated and looking fine. */
  b->actions = (Str*)rolltui_grow_zeroed(b->actions, &b->action_cap, b->action_count + 1, sizeof *b->actions);
  b->descriptions =
      (Str*)rolltui_grow_zeroed(b->descriptions, &b->desc_cap, b->action_count + 1, sizeof *b->descriptions);
  memset(&b->actions[b->action_count], 0, sizeof(Str));
  memset(&b->descriptions[b->action_count], 0, sizeof(Str));
  str_set(&b->actions[b->action_count], action, alen);
  str_set(&b->descriptions[b->action_count], desc, dlen);
  ++b->action_count;
  /* A row may already exist with chords in it — declaring is what makes them live and must
   * never throw them away (Bindings.hpp). */
  make_row(b, action, alen);
}

size_t rolltui_bindings_action_count(const RolltuiBindings* b) { return b->action_count; }

const char* rolltui_bindings_action_at(const RolltuiBindings* b, size_t i, size_t* len) {
  if (i >= b->action_count) {
    *len = 0;
    return NULL;
  }
  *len = b->actions[i].len;
  return b->actions[i].p;
}

const char* rolltui_bindings_description(const RolltuiBindings* b, const char* action, size_t len, size_t* out_len) {
  size_t i;
  for (i = 0; i < b->action_count; ++i)
    if (str_is(&b->actions[i], action, len)) {
      *out_len = b->descriptions[i].len;
      return b->descriptions[i].p;
    }
  *out_len = 0;
  return NULL;
}

void rolltui_bindings_undeclare_others(RolltuiBindings* b, RolltuiScopeFn is_library, void* ctx) {
  size_t i = b->action_count;
  while (i-- > 0) {
    size_t slen = 0;
    const char* scope = rolltui_bindings_scope_of(b->actions[i].p, b->actions[i].len, &slen);
    if (is_library(ctx, scope, slen)) continue;
    str_free(&b->actions[i]);
    str_free(&b->descriptions[i]);
    memmove(&b->actions[i], &b->actions[i + 1], (b->action_count - i - 1) * sizeof *b->actions);
    memmove(&b->descriptions[i], &b->descriptions[i + 1], (b->action_count - i - 1) * sizeof *b->descriptions);
    --b->action_count;
  }
}

size_t rolltui_bindings_row_count(const RolltuiBindings* b) { return b->row_count; }

const char* rolltui_bindings_row_at(const RolltuiBindings* b, size_t i, size_t* len) {
  if (i >= b->row_count) {
    *len = 0;
    return NULL;
  }
  *len = b->rows[i].action.len;
  return b->rows[i].action.p;
}

int rolltui_bindings_has_row(const RolltuiBindings* b, const char* action, size_t len) {
  return find_row(b, action, len) != NULL;
}

void rolltui_bindings_add_row(RolltuiBindings* b, const char* action, size_t len) { make_row(b, action, len); }

size_t rolltui_bindings_chord_count(const RolltuiBindings* b, const char* action, size_t len) {
  const Row* r = find_row(b, action, len);
  return r ? r->chord_count : 0;
}

int rolltui_bindings_chord_at(const RolltuiBindings* b, const char* action, size_t len, size_t i, RolltuiChord* out) {
  const Row* r = find_row(b, action, len);
  if (!r || i >= r->chord_count) return 0;
  *out = r->chords[i];
  return 1;
}

static int chord_eq(const RolltuiChord* a, const RolltuiChord* b) {
  return a->key == b->key && a->ch == b->ch && a->ctrl == b->ctrl && a->alt == b->alt && a->shift == b->shift;
}

static int scope_eq(const char* a, size_t alen, const char* b, size_t blen) {
  size_t as = 0, bs = 0;
  const char* ap = rolltui_bindings_scope_of(a, alen, &as);
  const char* bp = rolltui_bindings_scope_of(b, blen, &bs);
  return as == bs && (as == 0 || memcmp(ap, bp, as) == 0);
}

const char* rolltui_bindings_action_for(const RolltuiBindings* b, const RolltuiChord* k, const char* scope,
                                        size_t scope_len, size_t* out_len) {
  const unsigned char p = rolltui_key_active_protocol();
  size_t i, j;
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    size_t rs = 0;
    const char* rp = rolltui_bindings_scope_of(r->action.p, r->action.len, &rs);
    if (rs != scope_len || (rs && memcmp(rp, scope, rs) != 0)) continue;
    if (!rolltui_bindings_has(b, r->action.p, r->action.len)) continue; /* kept, never emitted */
    for (j = 0; j < r->chord_count; ++j)
      /* A chord this terminal cannot deliver is kept and inert for the same reason: the
       * row survives save, and nothing can emit it, so it must not claim a key. */
      if (chord_eq(&r->chords[j], k) && rolltui_key_deliverable(&r->chords[j], p)) {
        *out_len = r->action.len;
        return r->action.p;
      }
  }
  *out_len = 0;
  return NULL;
}

/* Bare Enter — no modifier — which is the only form the rule is about. */
static int is_bare_enter(const RolltuiChord* k) {
  return k->key == ROLLTUI_KEY_ENTER && !k->ctrl && !k->alt && !k->shift;
}

int rolltui_bindings_breaks_enter_rule(const RolltuiBindings* b, const char* action, size_t len,
                                       const RolltuiChord* chord) {
  if (b->enter_action.len == 0 || !is_bare_enter(chord)) return 0;
  if (str_is(&b->enter_action, action, len)) return 0;
  return scope_eq(b->enter_action.p, b->enter_action.len, action, len);
}

static void row_push(Row* r, const RolltuiChord* k) {
  size_t i;
  for (i = 0; i < r->chord_count; ++i)
    if (chord_eq(&r->chords[i], k)) return;
  r->chords = (RolltuiChord*)rolltui_grow(r->chords, &r->chords_cap, r->chord_count + 1, sizeof *r->chords);
  r->chords[r->chord_count++] = *k;
}

static int row_erase(Row* r, const RolltuiChord* k) {
  size_t i;
  for (i = 0; i < r->chord_count; ++i)
    if (chord_eq(&r->chords[i], k)) {
      memmove(&r->chords[i], &r->chords[i + 1], (r->chord_count - i - 1) * sizeof *r->chords);
      --r->chord_count;
      return 1;
    }
  return 0;
}

int rolltui_bindings_bind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord,
                          const char** moved_from, size_t* moved_len) {
  size_t i;
  Row* mine;
  if (!rolltui_bindings_has(b, action, len)) return 0;
  if (rolltui_bindings_breaks_enter_rule(b, action, len, chord)) return 0;
  for (i = 0; i < b->row_count; ++i) {
    Row* r = &b->rows[i];
    if (str_is(&r->action, action, len)) continue;
    if (!scope_eq(r->action.p, r->action.len, action, len)) continue;
    /* Never away from the Enter rule's subject. */
    if (is_bare_enter(chord) && str_is(&b->enter_action, r->action.p, r->action.len)) {
      size_t j;
      for (j = 0; j < r->chord_count; ++j)
        if (chord_eq(&r->chords[j], chord)) return 0;
      continue;
    }
    if (row_erase(r, chord) && moved_from) {
      *moved_from = r->action.p;
      *moved_len = r->action.len;
    }
  }
  mine = make_row(b, action, len);
  row_push(mine, chord);
  return 1;
}

int rolltui_bindings_unbind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord) {
  Row* r;
  if (is_bare_enter(chord) && str_is(&b->enter_action, action, len)) return 0;
  r = find_row(b, action, len);
  return r ? row_erase(r, chord) : 0;
}

void rolltui_bindings_clear(RolltuiBindings* b, const char* action, size_t len) {
  Row* r;
  if (str_is(&b->enter_action, action, len)) return;
  r = find_row(b, action, len);
  if (r) r->chord_count = 0; /* the storage is KEPT: this row is about to be refilled */
}

void rolltui_bindings_add_chord(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord) {
  row_push(make_row(b, action, len), chord);
}

RolltuiBindings* rolltui_bindings_clone(const RolltuiBindings* b) {
  RolltuiBindings* out = rolltui_bindings_new();
  size_t i, j;
  if (!b) return out;
  for (i = 0; i < b->action_count; ++i)
    rolltui_bindings_add_action(out, b->actions[i].p, b->actions[i].len, b->descriptions[i].p, b->descriptions[i].len);
  for (i = 0; i < b->row_count; ++i) {
    Row* r = make_row(out, b->rows[i].action.p, b->rows[i].action.len);
    for (j = 0; j < b->rows[i].chord_count; ++j) row_push(r, &b->rows[i].chords[j]);
  }
  str_set(&out->enter_action, b->enter_action.p, b->enter_action.len);
  return out;
}

int rolltui_bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b) {
  size_t i, j;
  if (a == b) return 1;
  if (!a || !b || a->row_count != b->row_count) return 0;
  for (i = 0; i < a->row_count; ++i) {
    if (!str_is(&a->rows[i].action, b->rows[i].action.p, b->rows[i].action.len)) return 0;
    if (a->rows[i].chord_count != b->rows[i].chord_count) return 0;
    for (j = 0; j < a->rows[i].chord_count; ++j)
      if (!chord_eq(&a->rows[i].chords[j], &b->rows[i].chords[j])) return 0;
  }
  return 1;
}
