/* rolltui/c/rolltui_bindings.c — the C side of chords, the binding table, and
 * the file format. See rolltui_bindings.h for the boundary's rules and rolltui/Bindings.hpp
 * for the binding rules themselves; `rolltui/tests/bindings_test.cpp` is the oracle.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`. A table is one handle
 * and holds nothing process-wide, so nothing here registers a shutdown releaser; the report
 * likewise owns nothing beyond one call's arrays. */
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/rolltui.h"
/* For `RolltuiLayoutAction`, which the header can only FORWARD-declare — `rolltui_layout.h`
 * includes this one, so including it back from the header would be a cycle. Dereferencing one
 * needs the definition, and a .c has no such constraint. */
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/testkit/testctl.h"

/* A literal C string plus its length, the same one-time convenience `rolltui_widget_menu.c` and
 * `rolltui_json.c` each name locally rather than share — a load happens once per file,
 * never per frame, so the `strlen` this costs is not one this library's budget covers. */
#define K(s) (s), strlen(s)

static int streq(const char* s, size_t slen, const char* lit) {
  const size_t litlen = strlen(lit);
  return slen == litlen && (litlen == 0 || memcmp(s, lit, litlen) == 0);
}

/* ---- the key-name table ------------------------------------------------------------------ */
/* THIS MODULE'S OWN FILE FORMAT, which is why it is here and `library_actions()` is not: a
 * chord's spelling is what a bindings file says, and this is the parser for it. The four
 * aliases ("esc", "pgup", "pgdn", "del") parse and never print, so a round trip is
 * canonical; "space" likewise, because it prints as itself and decodes to U+0020. */
typedef struct {
  const char* name;
  unsigned char key;
  unsigned char canonical; /* 0 for an alias: parsed, never printed */
  RolltuiCodepoint ch;     /* non-zero only for "space", the one alias that names a CHAR chord */
} KeyName;

/* The CANONICAL rows expand `rolltui_keys.h`'s ONE key list — a key with no file
 * spelling (`Char`, `Unknown`) contributes nothing — and only the ALIASES are written here,
 * because an alias is this file's own concern: it is parsed and never printed, so it has no
 * place in the vocabulary the rest of the library reads. */
static const KeyName kKeyNames[] = {
#define ROLLTUI_KEY_NAME_ROW_(UPPER, Title, file) {file, ROLLTUI_KEY_##UPPER, 1, 0},
    ROLLTUI_KEY_LIST(ROLLTUI_KEY_NAME_ROW_)
#undef ROLLTUI_KEY_NAME_ROW_
#define ROLLTUI_KEY_ALIAS_ROW_(name, UPPER, cp) {name, ROLLTUI_KEY_##UPPER, 0, cp},
    ROLLTUI_KEY_ALIAS_LIST(ROLLTUI_KEY_ALIAS_ROW_)
#undef ROLLTUI_KEY_ALIAS_ROW_
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

/* An EMPTY spelling is not a name: `Char` and `Unknown` are in the shared key
 * list with "" in the file column, because a bindings file has no word for either — a Char
 * chord is the character itself and Unknown is undecodable bytes. Both loops over this table
 * skip those rows.
 *
 * `name_is("", 0, "")` IS true, so the rows would otherwise match an empty string — but the
 * guard is BELT AND BRACES and this comment says so rather than claiming a defect it does not
 * prevent: `rolltui_chord_parse` refuses a zero-length last part upstream (`cur >= len`), and
 * `rolltui_chord_to_string` returns before reaching `canonical_key_name` for both CHAR and
 * UNKNOWN. Removing either guard was PLANTED and left all 184 assertions of `keys_test` and
 * `bindings_test` green, which is the honest measurement. It stays because the invariant then
 * belongs to this table rather than to two callers' early returns — but it is not load-bearing
 * today, and a comment saying otherwise would be the kind of unbacked claim this repo treats
 * as a defect in its own right. */
static const char* canonical_key_name(unsigned char key) {
  size_t i;
  for (i = 0; i < ROLLTUI_KEY_NAME_COUNT; ++i)
    if (kKeyNames[i].key == key && kKeyNames[i].canonical && kKeyNames[i].name[0]) return kKeyNames[i].name;
  return NULL;
}

static int is_any(const RolltuiChord* k) { return k->key == ROLLTUI_KEY_UNKNOWN && k->ch == ROLLTUI_CHORD_ANY; }
/* What an `any` slot takes: a typed character, Shift included, no Ctrl or Alt. */
static int any_takes(const RolltuiChord* k) { return k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && !k->super && k->ch >= ' '; }

int rolltui_chord_parse(const char* text, size_t len, RolltuiChord* out) {
  /* Split on '+', but a trailing "+" alone is the plus character. Done in place, over
   * (offset, length) pairs, so there is no vector of strings to own. */
  size_t part_at[8], part_len[8], n = 0, i, cur = 0;
  RolltuiChord k;
  memset(&k, 0, sizeof k);
  if (len == 3 && (text[0] | 0x20) == 'a' && (text[1] | 0x20) == 'n' && (text[2] | 0x20) == 'y') {
    k.key = ROLLTUI_KEY_UNKNOWN;
    k.ch = ROLLTUI_CHORD_ANY;
    *out = k;
    return 1;
  }
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
    else if (name_is(m, ml, "super") || name_is(m, ml, "cmd") || name_is(m, ml, "win") || name_is(m, ml, "d")) k.super = 1;
    else return 0;
  }
  {
    const char* last = text + part_at[n - 1];
    const size_t last_len = part_len[n - 1];
    RolltuiDecodedChar d;
    size_t consumed;
    for (i = 0; i < ROLLTUI_KEY_NAME_COUNT; ++i)
      if (kKeyNames[i].name[0] && name_is(last, last_len, kKeyNames[i].name)) {
        k.key = kKeyNames[i].key;
        k.ch = kKeyNames[i].ch; /* "space" is the one row that names a CHAR chord */
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
  if (is_any(k)) return put(out, 0, "any");
  if (k->key == ROLLTUI_KEY_UNKNOWN) return 0;
  if (k->super) n = put(out, n, "super+");
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
  /* Capitalise each part, join with '-': "Ctrl-Shift-Left", "Alt-Enter", "F1", "?". A letter alone is shown as it is typed, `g`, and
   * shift and a letter as the capital, `G`: a sequence reads as it is pressed. */
  char s[ROLLTUI_CHORD_STRING_MAX];
  size_t len, i, start = 0, n = 0;
  if (cap < ROLLTUI_CHORD_STRING_MAX) return 0;
  if (is_any(k)) {
    memcpy(out, "any key", 7);
    return 7;
  }
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && !k->super && k->ch >= 'a' && k->ch <= 'z') {
    out[0] = (char)(k->shift ? k->ch - 'a' + 'A' : k->ch);
    return 1;
  }
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

/* ---- the table --------------------------------------------------------------------------- */

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

/* A SEQUENCE: chords pressed one after another, `g g` or `ctrl+x ctrl+s`, bound like a chord. */
typedef struct {
  RolltuiChord c[ROLLTUI_SEQUENCE_MAX];
  unsigned char n;
} Seq;

typedef struct {
  Str action;
  RolltuiChord* chords;
  size_t chord_count, chords_cap;
  Seq* seqs;
  size_t seq_count, seqs_cap;
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
    rolltui_mem_free(b->rows[i].seqs);
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
  return a->key == b->key && a->ch == b->ch && a->ctrl == b->ctrl && a->alt == b->alt && a->shift == b->shift &&
         a->super == b->super;
}

void rolltui_chord_bound_form(const RolltuiChord* k, RolltuiChord* out) {
  *out = *k;
  if (out->key == ROLLTUI_KEY_CHAR && out->ch >= 'A' && out->ch <= 'Z') out->ch = out->ch - 'A' + 'a', out->shift = 1;
}

static RolltuiChord as_bound(const RolltuiChord* k) {
  RolltuiChord n;
  rolltui_chord_bound_form(k, &n);
  return n;
}

static int scope_eq(const char* a, size_t alen, const char* b, size_t blen) {
  size_t as = 0, bs = 0;
  const char* ap = rolltui_bindings_scope_of(a, alen, &as);
  const char* bp = rolltui_bindings_scope_of(b, blen, &bs);
  return as == bs && (as == 0 || memcmp(ap, bp, as) == 0);
}

const char* rolltui_bindings_action_for(const RolltuiBindings* b, const RolltuiChord* key, const char* scope,
                                        size_t scope_len, size_t* out_len) {
  const unsigned char p = rolltui_key_active_protocol();
  const RolltuiChord bound = as_bound(key);
  const RolltuiChord* k = &bound;
  size_t i, j;
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    size_t rs = 0;
    const char* rp = rolltui_bindings_scope_of(r->action.p, r->action.len, &rs);
    if (rs != scope_len || (rs && memcmp(rp, scope, rs) != 0)) continue;
    /* ON = the kept-and-inert guard gone: a row for an action NOTHING declares claims its
     * chord anyway. The row is legal, the file round-trips, and the key silently belongs to a
     * screen that is not running - which is exactly what "kept, never emitted" prevents. */
    if (!testkit_ctl_on("bindings.undeclared_action_claims_a_key") &&
        !rolltui_bindings_has(b, r->action.p, r->action.len))
      continue; /* kept, never emitted */
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

static int seq_eq(const Seq* a, const RolltuiChord* keys, size_t n) {
  size_t i;
  if (a->n != n) return 0;
  for (i = 0; i < n; ++i)
    if (!chord_eq(&a->c[i], &keys[i])) return 0;
  return 1;
}

static void row_push_seq(Row* r, const RolltuiChord* keys, size_t n) {
  size_t i;
  if (n < 2 || n > ROLLTUI_SEQUENCE_MAX) return;
  for (i = 0; i < r->seq_count; ++i)
    if (seq_eq(&r->seqs[i], keys, n)) return;
  r->seqs = (Seq*)rolltui_grow(r->seqs, &r->seqs_cap, r->seq_count + 1, sizeof *r->seqs);
  memset(&r->seqs[r->seq_count], 0, sizeof *r->seqs);
  memcpy(r->seqs[r->seq_count].c, keys, n * sizeof *keys);
  r->seqs[r->seq_count].n = (unsigned char)n;
  ++r->seq_count;
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
  if (r) r->chord_count = 0, r->seq_count = 0; /* the storage is KEPT: this row is about to be refilled */
}

void rolltui_bindings_add_sequence(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* keys, size_t n) {
  row_push_seq(make_row(b, action, len), keys, n);
}

size_t rolltui_bindings_sequence_count(const RolltuiBindings* b, const char* action, size_t len) {
  const Row* r = find_row(b, action, len);
  return r ? r->seq_count : 0;
}

size_t rolltui_bindings_sequence_at(const RolltuiBindings* b, const char* action, size_t len, size_t i, RolltuiChord* out,
                                    size_t cap) {
  const Row* r = find_row(b, action, len);
  size_t k;
  if (!r || i >= r->seq_count) return 0;
  for (k = 0; k < r->seqs[i].n && k < cap; ++k) out[k] = r->seqs[i].c[k];
  return r->seqs[i].n;
}

/* Whether a row of `scope` that something declares holds a sequence `keys` begins (or is). */
static int scope_row(const RolltuiBindings* b, const Row* r, const char* scope, size_t slen) {
  size_t rs = 0;
  const char* rp = rolltui_bindings_scope_of(r->action.p, r->action.len, &rs);
  return rs == slen && (rs == 0 || memcmp(rp, scope, rs) == 0) && rolltui_bindings_has(b, r->action.p, r->action.len);
}

static int seq_deliverable(const Seq* s, unsigned char p) {
  size_t i;
  for (i = 0; i < s->n; ++i)
    if (!rolltui_key_deliverable(&s->c[i], p)) return 0;
  return 1;
}

int rolltui_bindings_sequence_for(const RolltuiBindings* b, const RolltuiChord* keys, size_t n, const char* scope, size_t slen,
                                  const char** action, size_t* alen) {
  const unsigned char p = rolltui_key_active_protocol();
  size_t i, j, k;
  int prefix = 0;
  if (action) *action = NULL;
  if (alen) *alen = 0;
  if (n == 0) return ROLLTUI_SEQUENCE_NONE;
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    if (!scope_row(b, r, scope, slen)) continue;
    for (j = 0; j < r->seq_count; ++j) {
      const Seq* s = &r->seqs[j];
      if (s->n < n || !seq_deliverable(s, p)) continue;
      for (k = 0; k < n; ++k) {
        const RolltuiChord bk = as_bound(&keys[k]);
        if (is_any(&s->c[k]) ? !any_takes(&keys[k]) : !chord_eq(&s->c[k], &bk)) break;
      }
      if (k < n) continue;
      if (s->n == n) {
        if (action) *action = r->action.p;
        if (alen) *alen = r->action.len;
        return ROLLTUI_SEQUENCE_DONE;
      }
      prefix = 1;
    }
  }
  return prefix ? ROLLTUI_SEQUENCE_PREFIX : ROLLTUI_SEQUENCE_NONE;
}

int rolltui_bindings_sequence_ends_any(const RolltuiBindings* b, const RolltuiChord* keys, size_t n, const char* scope, size_t slen) {
  size_t i, j, k;
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    if (!scope_row(b, r, scope, slen)) continue;
    for (j = 0; j < r->seq_count; ++j) {
      const Seq* s = &r->seqs[j];
      if (s->n != n || !is_any(&s->c[n - 1])) continue;
      for (k = 0; k + 1 < n; ++k) {
        const RolltuiChord bk = as_bound(&keys[k]);
        if (!chord_eq(&s->c[k], &bk)) break;
      }
      if (k + 1 == n && any_takes(&keys[n - 1])) return 1;
    }
  }
  return 0;
}

size_t rolltui_bindings_sequence_next(const RolltuiBindings* b, const RolltuiChord* keys, size_t n, const char* scope, size_t slen,
                                      RolltuiChord* next, const char** actions, size_t* alens, unsigned char* more, size_t cap) {
  const unsigned char p = rolltui_key_active_protocol();
  size_t i, j, k, out = 0, m;
  for (i = 0; i < b->row_count && out < cap; ++i) {
    const Row* r = &b->rows[i];
    if (!scope_row(b, r, scope, slen)) continue;
    for (j = 0; j < r->seq_count && out < cap; ++j) {
      const Seq* s = &r->seqs[j];
      int dup = 0;
      if (s->n <= n || !seq_deliverable(s, p)) continue;
      for (k = 0; k < n; ++k) {
        const RolltuiChord bk = as_bound(&keys[k]);
        if (is_any(&s->c[k]) ? !any_takes(&keys[k]) : !chord_eq(&s->c[k], &bk)) break;
      }
      if (k < n) continue;
      for (m = 0; m < out; ++m) /* one row per next key: a longer sequence through it says there is more */
        if (chord_eq(&next[m], &s->c[n])) {
          if (s->n > n + 1) more[m] = 1;
          else actions[m] = r->action.p, alens[m] = r->action.len;
          dup = 1;
          break;
        }
      if (dup) continue;
      next[out] = s->c[n];
      more[out] = (unsigned char)(s->n > n + 1);
      actions[out] = s->n == n + 1 ? r->action.p : NULL;
      alens[out] = s->n == n + 1 ? r->action.len : 0;
      ++out;
    }
  }
  return out;
}

/* Whether `k` begins a sequence some other row of `scope` holds, or (with `seq`) a sequence of `n` begins with a chord bound alone. */
static const Row* sequence_starting(const RolltuiBindings* b, const RolltuiChord* k, const char* scope, size_t slen) {
  size_t i, j;
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    size_t rs = 0;
    const char* rp = rolltui_bindings_scope_of(r->action.p, r->action.len, &rs);
    if (rs != slen || (rs && memcmp(rp, scope, rs) != 0)) continue;
    for (j = 0; j < r->seq_count; ++j)
      if (chord_eq(&r->seqs[j].c[0], k)) return r;
  }
  return NULL;
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
    for (j = 0; j < b->rows[i].seq_count; ++j) row_push_seq(r, b->rows[i].seqs[j].c, b->rows[i].seqs[j].n);
  }
  str_set(&out->enter_action, b->enter_action.p, b->enter_action.len);
  return out;
}

/* The same keys in the same order; a missing row holds none. */
static int row_same(const Row* a, const Row* b) {
  const size_t ac = a ? a->chord_count : 0, as = a ? a->seq_count : 0;
  size_t i;
  if (ac != (b ? b->chord_count : 0) || as != (b ? b->seq_count : 0)) return 0;
  for (i = 0; i < ac; ++i)
    if (!chord_eq(&a->chords[i], &b->chords[i])) return 0;
  for (i = 0; i < as; ++i)
    if (!seq_eq(&a->seqs[i], b->seqs[i].c, b->seqs[i].n)) return 0;
  return 1;
}

/* `action`'s row in `into` made `from`'s keys (none for NULL); the row stays, so a saved table says the action has no key. */
static void row_take(RolltuiBindings* into, const char* action, size_t len, const Row* from) {
  Row* r = make_row(into, action, len);
  size_t i;
  r->chord_count = 0;
  r->seq_count = 0;
  if (!from) return;
  for (i = 0; i < from->chord_count; ++i) row_push(r, &from->chords[i]);
  for (i = 0; i < from->seq_count; ++i) row_push_seq(r, from->seqs[i].c, from->seqs[i].n);
}

void rolltui_bindings_take_changes(RolltuiBindings* into, const RolltuiBindings* before, const RolltuiBindings* after) {
  size_t i;
  for (i = 0; i < after->row_count; ++i) {
    const Row* a = &after->rows[i];
    if (!row_same(a, find_row(before, a->action.p, a->action.len))) row_take(into, a->action.p, a->action.len, a);
  }
  for (i = 0; i < before->row_count; ++i) {
    const Row* b = &before->rows[i];
    if (!find_row(after, b->action.p, b->action.len) && !row_same(b, NULL)) row_take(into, b->action.p, b->action.len, NULL);
  }
}

/* Every row of `a` that holds a key is in `b` with the same keys in the same order. */
static int rows_within(const RolltuiBindings* a, const RolltuiBindings* b) {
  size_t i, j;
  for (i = 0; i < a->row_count; ++i) {
    const Row* r = &a->rows[i];
    const Row* o;
    if (r->chord_count == 0 && r->seq_count == 0) continue;
    o = find_row(b, r->action.p, r->action.len);
    if (!o || o->chord_count != r->chord_count || o->seq_count != r->seq_count) return 0;
    for (j = 0; j < r->chord_count; ++j)
      if (!chord_eq(&r->chords[j], &o->chords[j])) return 0;
    for (j = 0; j < r->seq_count; ++j)
      if (!seq_eq(&r->seqs[j], o->seqs[j].c, o->seqs[j].n)) return 0;
  }
  return 1;
}

/* THE SAME KEYS: each action holds the same keys in both. Where a row sits, and a row holding none (an action declared since), make
 * no difference to what a person presses. */
int rolltui_bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b) {
  if (a == b) return 1;
  if (!a || !b) return 0;
  return rows_within(a, b) && rows_within(b, a);
}

/* ---- the file format --------------------------------------------------------------------- */

/* ---- the report: a plain `RolltuiStr` array per `BindingsLoadReport` field, the same shape
 * every loader's report on this boundary uses. ------------------------------------------- */

void rolltui_bindings_report_release(RolltuiBindingsReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
#define REL(list) \
  for (i = 0; i < r->list##_n; ++i) rolltui_str_free(&r->list[i]); \
  rolltui_mem_free(r->list)
  REL(unknown_actions);
  REL(bad_chords);
  REL(undeliverable);
  REL(conflicts);
  REL(bad_values);
  REL(unknown_keys);
#undef REL
  memset(r, 0, sizeof *r);
}

void rolltui_bindings_report_set_error(RolltuiBindingsReport* r, const char* s, size_t len) {
  rolltui_str_set(&r->error, s, len);
}

/* `fn` is the SINGULAR suffix the header declares (`add_bad_chord`, one call per occurrence,
 * matching every other report builder here); `field` is the PLURAL array it grows. */
#define ADD(fn, field)                                                                                       \
  void rolltui_bindings_report_add_##fn(RolltuiBindingsReport* r, const char* s, size_t len) {                \
    r->field =                                                                                               \
        (RolltuiStr*)rolltui_grow_zeroed(r->field, &r->field##_cap, r->field##_n + 1, sizeof *r->field);      \
    rolltui_str_set(&r->field[r->field##_n++], s, len);                                                      \
  }
ADD(unknown_action, unknown_actions)
ADD(bad_chord, bad_chords)
ADD(undeliverable, undeliverable)
ADD(conflict, conflicts)
ADD(bad_value, bad_values)
ADD(unknown_key, unknown_keys)
#undef ADD

int rolltui_bindings_report_clean(const RolltuiBindingsReport* r) {
  return r->error.n == 0 && r->unknown_actions_n == 0 && r->bad_chords_n == 0 && r->conflicts_n == 0 &&
         r->bad_values_n == 0 && r->unknown_keys_n == 0 && r->undeliverable_n == 0;
}

/* A ten-line growing byte buffer, the same private helper `rolltui_json.c` and
 * `rolltui_theme.c` each name locally for the same GROWING AMORTISED role. */
typedef struct { char* p; size_t len, cap; } SBuf;

static void sbuf_add(SBuf* b, const char* s, size_t n) {
  if (!n) return;
  b->p = (char*)rolltui_grow(b->p, &b->cap, b->len + n, sizeof *b->p);
  memcpy(b->p + b->len, s, n);
  b->len += n;
}

void rolltui_bindings_report_summary(const RolltuiBindingsReport* r, RolltuiStr* out) {
  SBuf b;
  size_t i;
  int first = 1;
  if (rolltui_bindings_report_clean(r)) {
    rolltui_str_clear(out);
    return;
  }
  if (r->error.n != 0) {
    rolltui_str_set(out, r->error.p, r->error.n);
    return;
  }
  memset(&b, 0, sizeof b);
#define JOIN(list, label)                              \
  for (i = 0; i < r->list##_n; ++i) {                  \
    if (!first) sbuf_add(&b, "; ", 2);                  \
    first = 0;                                          \
    sbuf_add(&b, label, strlen(label));                 \
    sbuf_add(&b, r->list[i].p, r->list[i].n);           \
  }
  JOIN(bad_values, "bad: ")
  JOIN(conflicts, "conflict: ")
  JOIN(bad_chords, "chord: ")
  JOIN(undeliverable, "undeliverable: ")
  JOIN(unknown_actions, "unknown action: ")
  JOIN(unknown_keys, "unknown: ")
#undef JOIN
  rolltui_str_set(out, b.p, b.len);
  rolltui_mem_free(b.p);
}

/* ---- the loader -------------------------------------------------------------------------- */

/* Builds `action + suffix` into a fresh `RolltuiStr` and hands it to `add`; frees the buffer.
 * The one shape every per-row message below shares, whether the suffix is a fixed literal or
 * one built with more pieces first (the caller then passes a `RolltuiStr`'s own bytes as the
 * "suffix"). */
static void report_at(void (*add)(RolltuiBindingsReport*, const char*, size_t), RolltuiBindingsReport* r,
                      const char* action, size_t action_len, const char* suffix, size_t suffix_len) {
  RolltuiStr msg;
  memset(&msg, 0, sizeof msg);
  rolltui_str_set(&msg, action, action_len);
  rolltui_str_append(&msg, suffix, suffix_len);
  add(r, msg.p, msg.n);
  rolltui_str_free(&msg);
}

/* WHICH ROW of `scope` already holds this chord, or NULL — the C's own `Bindings::holder`,
 * over the ROWS themselves and not through `rolltui_bindings_action_for`, so an UNDECLARED
 * row conflicts at load exactly as Bindings.hpp states. `scope`/`scope_len` is already a bare
 * scope (no dot), and `scope_eq` extracting ITS scope again is a no-op — the same function the
 * public API already uses for two action names, reused here for one action name and one bare
 * scope. */
static const Row* holder_row(const RolltuiBindings* b, const RolltuiChord* k, const char* scope, size_t scope_len) {
  size_t i, j;
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    if (!scope_eq(r->action.p, r->action.len, scope, scope_len)) continue;
    for (j = 0; j < r->chord_count; ++j)
      if (chord_eq(&r->chords[j], k)) return r;
  }
  return NULL;
}

/* One row of the file: `action` its name, `chords_v` its JSON value. Mirrors the body
 * of the C++ loader's per-key loop exactly, including the two problems that are reported but
 * do NOT stop the row from loading (an undeliverable chord is kept; the file itself still
 * loads on any per-chord problem). */
/* One sequence of a row: its chords parsed, each deliverable or named, and refused where it would be ambiguous. */
static void load_sequence(RolltuiBindings* b, const char* action, size_t action_len, const char* text, size_t len,
                          unsigned char deliver, RolltuiReasonFn reason, void* reason_ctx, RolltuiBindingsReport* report) {
  RolltuiChord keys[ROLLTUI_SEQUENCE_MAX];
  size_t n = 0, i = 0, slen = 0;
  const char* scope = rolltui_bindings_scope_of(action, action_len, &slen);
  int ok = 1;
  while (i < len && ok) {
    size_t e = i;
    while (i < len && text[i] == ' ') ++i;
    if (i >= len) break;
    e = i;
    while (e < len && text[e] != ' ') ++e;
    if (n == ROLLTUI_SEQUENCE_MAX || !rolltui_chord_parse(text + i, e - i, &keys[n])) ok = 0;
    else ++n;
    i = e;
  }
  for (i = 0; ok && i + 1 < n; ++i)
    if (is_any(&keys[i])) ok = 0; /* `any` is the last key, or it would take every key after it */
  if (!ok || n < 2) {
    RolltuiStr msg;
    memset(&msg, 0, sizeof msg);
    rolltui_str_set(&msg, action, action_len);
    rolltui_str_append(&msg, K(": '"));
    rolltui_str_append(&msg, text, len);
    rolltui_str_append(&msg, K("' is not a sequence (two to four chords, apart by spaces, `any` only last)"));
    rolltui_bindings_report_add_bad_chord(report, msg.p, msg.n);
    rolltui_str_free(&msg);
    return;
  }
  for (i = 0; i < n; ++i)
    if (!rolltui_key_deliverable(&keys[i], deliver)) {
      char rbuf[ROLLTUI_UNDELIVERABLE_REASON_MAX];
      char cbuf[ROLLTUI_CHORD_STRING_MAX];
      const size_t rlen = reason ? reason(reason_ctx, &keys[i], deliver, rbuf, sizeof rbuf) : 0;
      const size_t clen2 = rolltui_chord_to_string(&keys[i], cbuf, sizeof cbuf);
      RolltuiStr msg;
      memset(&msg, 0, sizeof msg);
      rolltui_str_set(&msg, action, action_len);
      rolltui_str_append(&msg, K(": '"));
      rolltui_str_append(&msg, cbuf, clen2);
      rolltui_str_append(&msg, K("' cannot be delivered by this terminal; "));
      rolltui_str_append(&msg, rbuf, rlen);
      rolltui_bindings_report_add_undeliverable(report, msg.p, msg.n);
      rolltui_str_free(&msg);
      break; /* kept, and inert, as a chord is */
    }
  {
    /* AMBIGUOUS: its first chord bound alone in the scope, or the same keys bound to another action */
    const Row* alone = holder_row(b, &keys[0], scope, slen);
    size_t r, j;
    const Row* other = NULL;
    for (r = 0; r < b->row_count && !other; ++r) {
      size_t rs = 0;
      const char* rp = rolltui_bindings_scope_of(b->rows[r].action.p, b->rows[r].action.len, &rs);
      if (rs != slen || (rs && memcmp(rp, scope, rs) != 0) || str_is(&b->rows[r].action, action, action_len)) continue;
      for (j = 0; j < b->rows[r].seq_count; ++j)
        if (seq_eq(&b->rows[r].seqs[j], keys, n)) other = &b->rows[r];
    }
    if (alone || other) {
      RolltuiStr msg;
      memset(&msg, 0, sizeof msg);
      rolltui_str_append(&msg, K("'"));
      rolltui_str_append(&msg, text, len);
      if (other) {
        rolltui_str_append(&msg, K("' bound to both "));
        rolltui_str_append(&msg, other->action.p, other->action.len);
        rolltui_str_append(&msg, K(" and "));
        rolltui_str_append(&msg, action, action_len);
        rolltui_str_append(&msg, K(" ("));
        rolltui_str_append(&msg, other->action.p, other->action.len);
        rolltui_str_append(&msg, K(" kept)"));
      } else {
        rolltui_str_append(&msg, K("' begins with a chord bound alone to "));
        rolltui_str_append(&msg, alone->action.p, alone->action.len);
        rolltui_str_append(&msg, K(" (the chord kept)"));
      }
      rolltui_bindings_report_add_conflict(report, msg.p, msg.n);
      rolltui_str_free(&msg);
      return;
    }
  }
  rolltui_bindings_add_sequence(b, action, action_len, keys, n);
}

static void load_one_row(RolltuiBindings* b, const char* action, size_t action_len, const RolltuiJsonValue* chords_v,
                         unsigned char deliver, RolltuiScopeFn is_library, void* library_ctx, RolltuiReasonFn reason,
                         void* reason_ctx, RolltuiBindingsReport* report) {
  size_t i, n;
  if (!rolltui_bindings_has(b, action, action_len)) {
    /* A library scope is closed, so a name it does not define is a typo. Any other scope
     * belongs to a layout this file knows nothing about: the row is kept, inert, until
     * something declares it (Bindings.hpp's kept-and-inert rule). */
    size_t slen = 0;
    const char* scope = rolltui_bindings_scope_of(action, action_len, &slen);
    if ((is_library && is_library(library_ctx, scope, slen)) || slen == action_len) {
      rolltui_bindings_report_add_unknown_action(report, action, action_len);
      return;
    }
    /* Only if there is no row yet: a file may name one action twice, and a second row for
     * one action would split what the user sees from what fires. */
    rolltui_bindings_add_row(b, action, action_len);
  }
  if (!rolltui_json_is_array(chords_v)) {
    report_at(rolltui_bindings_report_add_bad_value, report, action, action_len, K(": expected an array of chords"));
    return;
  }
  n = rolltui_json_array_size(chords_v);
  for (i = 0; i < n; ++i) {
    const RolltuiJsonValue* cv = rolltui_json_array_at(chords_v, i);
    RolltuiChord k;
    size_t clen = 0;
    const char* ctext;
    if (!rolltui_json_is_string(cv)) {
      report_at(rolltui_bindings_report_add_bad_value, report, action, action_len, K(": a chord must be a string"));
      continue;
    }
    ctext = rolltui_json_as_string(cv, "", 0, &clen);
    if (memchr(ctext, ' ', clen)) { /* A SEQUENCE: chords apart by spaces */
      load_sequence(b, action, action_len, ctext, clen, deliver, reason, reason_ctx, report);
      continue;
    }
    if (!rolltui_chord_parse(ctext, clen, &k) || is_any(&k)) {
      RolltuiStr msg;
      memset(&msg, 0, sizeof msg);
      rolltui_str_set(&msg, action, action_len);
      rolltui_str_append(&msg, K(": '"));
      rolltui_str_append(&msg, ctext, clen);
      {
        const char* why = is_any(&k) ? "' is not a chord: `any` follows another key, as in \"m any\"" : "' is not a chord";
        rolltui_str_append(&msg, why, strlen(why));
      }
      rolltui_bindings_report_add_bad_chord(report, msg.p, msg.n);
      rolltui_str_free(&msg);
      continue;
    }
    /* A chord this terminal cannot deliver: named, with the reason — and then KEPT anyway (no
     * `continue`), because the refusal is about what can fire, not about what the user wrote;
     * the row round-trips through save so the same file loads clean the day the terminal
     * negotiates a stronger protocol (Bindings.hpp). */
    if (!rolltui_key_deliverable(&k, deliver)) {
      char rbuf[ROLLTUI_UNDELIVERABLE_REASON_MAX];
      char cbuf[ROLLTUI_CHORD_STRING_MAX];
      const size_t rlen = reason ? reason(reason_ctx, &k, deliver, rbuf, sizeof rbuf) : 0;
      const size_t clen2 = rolltui_chord_to_string(&k, cbuf, sizeof cbuf);
      RolltuiStr msg;
      memset(&msg, 0, sizeof msg);
      rolltui_str_set(&msg, action, action_len);
      rolltui_str_append(&msg, K(": '"));
      rolltui_str_append(&msg, cbuf, clen2);
      rolltui_str_append(&msg, K("' cannot be delivered by this terminal; "));
      rolltui_str_append(&msg, rbuf, rlen);
      rolltui_bindings_report_add_undeliverable(report, msg.p, msg.n);
      rolltui_str_free(&msg);
    }
    {
      const int is_enter = is_bare_enter(&k);
      size_t slen = 0;
      const char* scope = rolltui_bindings_scope_of(action, action_len, &slen);
      const int is_input = slen == 5 && memcmp(scope, "input", 5) == 0;
      const int is_submit = streq(action, action_len, "input.submit");
      if (is_enter && is_input && !is_submit) {
        report_at(rolltui_bindings_report_add_bad_value, report, action, action_len,
                 K(": 'enter' is always input.submit and cannot be bound here (refused)"));
        continue;
      }
    }
    {
      /* A conflict within the scope: the FIRST binding in the file wins. Over the rows
       * themselves (holder_row), not through action_for, so two UNDECLARED actions of one
       * scope conflict here rather than silently once something declares them. */
      size_t slen = 0;
      const char* scope = rolltui_bindings_scope_of(action, action_len, &slen);
      const Row* other = holder_row(b, &k, scope, slen);
      const Row* begins = sequence_starting(b, &k, scope, slen);
      if (!other && begins) { /* a chord alone and the first of a sequence cannot both be meant: the sequence was first */
        RolltuiStr msg;
        memset(&msg, 0, sizeof msg);
        rolltui_str_append(&msg, K("'"));
        rolltui_str_append(&msg, ctext, clen);
        rolltui_str_append(&msg, K("' begins a sequence of "));
        rolltui_str_append(&msg, begins->action.p, begins->action.len);
        rolltui_str_append(&msg, K(" and cannot also be "));
        rolltui_str_append(&msg, action, action_len);
        rolltui_str_append(&msg, K(" alone (the sequence kept)"));
        rolltui_bindings_report_add_conflict(report, msg.p, msg.n);
        rolltui_str_free(&msg);
        continue;
      }
      if (other && !str_is(&other->action, action, action_len)) {
        RolltuiStr msg;
        memset(&msg, 0, sizeof msg);
        rolltui_str_append(&msg, K("'"));
        rolltui_str_append(&msg, ctext, clen);
        rolltui_str_append(&msg, K("' bound to both "));
        rolltui_str_append(&msg, other->action.p, other->action.len);
        rolltui_str_append(&msg, K(" and "));
        rolltui_str_append(&msg, action, action_len);
        rolltui_str_append(&msg, K(" ("));
        rolltui_str_append(&msg, other->action.p, other->action.len);
        rolltui_str_append(&msg, K(" kept)"));
        rolltui_bindings_report_add_conflict(report, msg.p, msg.n);
        rolltui_str_free(&msg);
        continue;
      }
    }
    rolltui_bindings_add_chord(b, action, action_len, &k);
  }
}

int rolltui_bindings_load_json(RolltuiBindings* b, const char* text, size_t len, unsigned char deliver_protocol,
                               RolltuiScopeFn is_library, void* library_ctx, RolltuiReasonFn reason,
                               void* reason_ctx, RolltuiBindingsReport* report) {
  RolltuiJsonValue* root;
  RolltuiStr jerr;
  const RolltuiJsonValue* map;
  size_t i, n;
  memset(&jerr, 0, sizeof jerr);
  rolltui_bindings_report_release(report);
  root = rolltui_json_parse(text, len, &jerr);
  if (!root) {
    rolltui_bindings_report_set_error(report, jerr.p ? jerr.p : "", jerr.n);
    rolltui_str_free(&jerr);
    return 0;
  }
  rolltui_str_free(&jerr);
  if (!rolltui_json_is_object(root)) {
    rolltui_bindings_report_set_error(report, K("a bindings file must be a JSON object"));
    rolltui_json_free(root);
    return 0;
  }
  map = rolltui_json_get(root, K("bindings"));
  if (!rolltui_json_is_object(map)) {
    rolltui_bindings_report_set_error(report, K("a bindings file needs a \"bindings\" object"));
    rolltui_json_free(root);
    return 0;
  }
  n = rolltui_json_object_size(root);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(root, i, &klen);
    if (!streq(k, klen, "name") && !streq(k, klen, "bindings") && !streq(k, klen, "preset"))
      rolltui_bindings_report_add_unknown_key(report, k, klen);
  }
  n = rolltui_json_object_size(map);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* key = rolltui_json_object_key_at(map, i, &klen);
    const RolltuiJsonValue* chords_v = rolltui_json_object_value_at(map, i);
    load_one_row(b, key, klen, chords_v, deliver_protocol, is_library, library_ctx, reason, reason_ctx, report);
  }
  /* The Enter rule, the other half: the rule's subject must have Enter, restored when a file
   * moved it away. The NAME is the vocabulary and was handed over once, at construction
   * (`rolltui_bindings_set_enter_rule`); this reads it back rather than assuming it. */
  if (b->enter_action.len != 0) {
    RolltuiChord enter;
    const Row* r = find_row(b, b->enter_action.p, b->enter_action.len);
    int has_enter = 0;
    memset(&enter, 0, sizeof enter);
    enter.key = ROLLTUI_KEY_ENTER;
    if (r) {
      size_t j;
      for (j = 0; j < r->chord_count; ++j)
        if (chord_eq(&r->chords[j], &enter)) { has_enter = 1; break; }
    }
    if (!has_enter) {
      report_at(rolltui_bindings_report_add_bad_value, report, b->enter_action.p, b->enter_action.len,
               K(": 'enter' is always bound to it (restored)"));
      rolltui_bindings_add_chord(b, b->enter_action.p, b->enter_action.len, &enter);
    }
  }
  rolltui_json_free(root);
  return 1;
}

void rolltui_bindings_dump_json(const RolltuiBindings* b, const char* name, size_t name_len, RolltuiStr* out) {
  RolltuiJsonValue* root = rolltui_json_object();
  RolltuiJsonValue* map = rolltui_json_object();
  size_t i;
  rolltui_json_set(root, K("name"), rolltui_json_string(name, name_len));
  for (i = 0; i < b->row_count; ++i) {
    const Row* r = &b->rows[i];
    RolltuiJsonValue* arr = rolltui_json_array();
    size_t j;
    for (j = 0; j < r->chord_count; ++j) {
      char buf[ROLLTUI_CHORD_STRING_MAX];
      const size_t blen = rolltui_chord_to_string(&r->chords[j], buf, sizeof buf);
      rolltui_json_array_push(arr, rolltui_json_string(buf, blen));
    }
    for (j = 0; j < r->seq_count; ++j) { /* a sequence: its chords apart by spaces */
      char buf[ROLLTUI_CHORD_STRING_MAX];
      RolltuiStr seq;
      size_t k;
      memset(&seq, 0, sizeof seq);
      for (k = 0; k < r->seqs[j].n; ++k) {
        const size_t blen = rolltui_chord_to_string(&r->seqs[j].c[k], buf, sizeof buf);
        if (k) rolltui_str_append(&seq, " ", 1);
        rolltui_str_append(&seq, buf, blen);
      }
      rolltui_json_array_push(arr, rolltui_json_string(seq.p ? seq.p : "", seq.n));
      rolltui_str_free(&seq);
    }
    rolltui_json_set(map, r->action.p, r->action.len, arr);
  }
  rolltui_json_set(root, K("bindings"), map);
  rolltui_json_dump(root, 2, out);
  rolltui_str_append(out, "\n", 1);
  rolltui_json_free(root);
}

/* ---- DECLARING, SUGGESTING, AND THE SHIPPED TABLE — see the header ----------------------- */

int rolltui_bindings_library_scope(void* ctx, const char* scope, size_t len) {
  const size_t n = rolltui_library_action_count();
  size_t i;
  (void)ctx;
  for (i = 0; i < n; ++i) {
    size_t alen = 0, slen = 0;
    const char* a = rolltui_library_action_name(i, &alen);
    const char* s = rolltui_bindings_scope_of(a, alen, &slen);
    if (slen == len && memcmp(s, scope, len) == 0) return 1;
  }
  return 0;
}

const char* rolltui_bindings_holder(const RolltuiBindings* b, const RolltuiChord* chord, const char* scope,
                                    size_t scope_len, size_t* out_len) {
  const size_t rows = rolltui_bindings_row_count(b);
  size_t i;
  for (i = 0; i < rows; ++i) {
    size_t alen = 0, slen = 0, n, j;
    const char* a = rolltui_bindings_row_at(b, i, &alen);
    const char* s = rolltui_bindings_scope_of(a, alen, &slen);
    if (slen != scope_len || memcmp(s, scope, scope_len) != 0) continue;
    n = rolltui_bindings_chord_count(b, a, alen);
    for (j = 0; j < n; ++j) {
      RolltuiChord c;
      if (rolltui_bindings_chord_at(b, a, alen, j, &c) && chord_eq(&c, chord)) {
        if (out_len) *out_len = alen;
        return a;
      }
    }
  }
  if (out_len) *out_len = 0;
  return NULL;
}

void rolltui_bindings_suggest(RolltuiBindings* b, const RolltuiToolAction* tools, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    const char* name = tools[i].name;
    const size_t nlen = name ? strlen(name) : 0;
    RolltuiChord c;
    int have;
    size_t slen = 0;
    const char* scope;
    if (!nlen || rolltui_bindings_has_row(b, name, nlen)) continue;
    have = tools[i].chord && tools[i].chord[0] &&
           rolltui_chord_parse(tools[i].chord, strlen(tools[i].chord), &c);
    scope = rolltui_bindings_scope_of(name, nlen, &slen);
    /* The ROW is created either way — a tool that suggests nothing, or whose suggestion is
     * taken, is still a declared action with no key rather than no action at all. */
    rolltui_bindings_add_row(b, name, nlen);
    if (have && rolltui_bindings_holder(b, &c, scope, slen, NULL) == NULL)
      rolltui_bindings_add_chord(b, name, nlen, &c);
  }
}

void rolltui_bindings_declare(RolltuiBindings* b, const RolltuiLayoutAction* declared, size_t declared_n,
                              const RolltuiToolAction* tools, size_t tools_n) {
  size_t i;
  /* SUGGESTIONS FIRST — see the header. A declaration creates the row a suggestion checks. */
  rolltui_bindings_suggest(b, tools, tools_n);
  rolltui_bindings_undeclare_others(b, rolltui_bindings_library_scope, NULL);
  for (i = 0; i < declared_n; ++i) {
    size_t nlen = 0, dlen = 0;
    const char* nm = rolltui_str_get(&declared[i].name, &nlen);
    const char* de = rolltui_str_get(&declared[i].description, &dlen);
    rolltui_bindings_add_action(b, nm, nlen, de, dlen);
  }
  for (i = 0; i < tools_n; ++i) {
    const char* nm = tools[i].name;
    const char* de = tools[i].description;
    if (nm && nm[0]) rolltui_bindings_add_action(b, nm, strlen(nm), de ? de : "", de ? strlen(de) : 0);
  }
}

/* THE SHIPPED DEFAULT TABLE — see the header for the two aborts and why they are aborts.
 * OWNED, LONG-LIVED (CLAUDE.md strategy 4), and A SESSION'S now: it is BORROWED
 * by every caller, so `rolltui_context_free` releasing it is what bounds the borrow. The
 * shutdown hook it used to register went with the global. */

static const char* default_bindings_json(size_t* len) {
  size_t i;
  for (i = 0; i < rolltui_kBindingsPresetCount; ++i)
    if (strcmp(rolltui_kBindingsPresets[i].name, "default") == 0) {
      const char* t = rolltui_kBindingsPresets[i].text;
      *len = strlen(t);
      return t;
    }
  *len = 0;
  return "";
}

void rolltui_bindings_fill_unnamed(RolltuiBindings* b, const char* text, size_t len) {
  RolltuiJsonValue* root = rolltui_json_parse(text, len, NULL);
  const RolltuiJsonValue* map = root ? rolltui_json_get(root, K("bindings")) : NULL;
  RolltuiBindings* d;
  RolltuiBindingsReport rep;
  size_t dlen = 0, i, j;
  const char* dtext;
  if (!rolltui_json_is_object(map)) {
    rolltui_json_free(root);
    return;
  }
  d = rolltui_bindings_new_seeded();
  dtext = default_bindings_json(&dlen);
  memset(&rep, 0, sizeof rep);
  if (rolltui_bindings_load_json(d, dtext, dlen, ROLLTUI_PROTOCOL_LEGACY, rolltui_bindings_library_scope, NULL, NULL, NULL, &rep)) {
    const size_t n = rolltui_library_action_count();
    for (i = 0; i < n; ++i) {
      size_t alen = 0, slen = 0;
      const char* a = rolltui_library_action_name(i, &alen);
      const char* s = rolltui_bindings_scope_of(a, alen, &slen);
      const Row* r = find_row(d, a, alen);
      if (!r || rolltui_json_has(map, a, alen)) continue;
      for (j = 0; j < r->chord_count; ++j)
        if (!holder_row(b, &r->chords[j], s, slen) && !rolltui_bindings_breaks_enter_rule(b, a, alen, &r->chords[j]))
          rolltui_bindings_add_chord(b, a, alen, &r->chords[j]);
    }
  }
  rolltui_bindings_report_release(&rep);
  rolltui_bindings_free(d);
  rolltui_json_free(root);
}

RolltuiBindings* rolltui_bindings_new_seeded(void) {
  RolltuiBindings* b = rolltui_bindings_new();
  const size_t n = rolltui_library_action_count();
  size_t i;
  /* The Enter rule's SUBJECT — the one name the C is handed so the rule can live here. */
  rolltui_bindings_set_enter_rule(b, "input.submit", 12);
  for (i = 0; i < n; ++i) {
    size_t alen = 0, dlen = 0;
    const char* a = rolltui_library_action_name(i, &alen);
    const char* d = rolltui_library_action_description(i, &dlen);
    rolltui_bindings_add_action(b, a, alen, d, dlen);
  }
  return b;
}

const RolltuiBindings* rolltui_bindings_default(RolltuiContext* c) {
  RolltuiBindings* b;
  RolltuiBindingsReport rep;
  RolltuiStr summary = {0};
  size_t tlen = 0, n = 0, rows, i;
  const char* text;
  const RolltuiLayoutAction* actions;
  int ok;

  if (c == NULL) return NULL;
  if (c->bindings) return c->bindings;

  b = rolltui_bindings_new_seeded();
  text = default_bindings_json(&tlen);
  memset(&rep, 0, sizeof rep);
  /* AGAINST LEGACY, EXPLICITLY, and not against whatever this terminal turned out to be: the
   * shipped file belongs to every host on every terminal, so it must be deliverable under the
   * WEAKEST model. Checking it against the ACTIVE protocol would let a kitty terminal ship a
   * file a plain xterm cannot press — the same defect one level up.
   *
   * `reason` is NULL because it only supplies English for a chord that cannot be delivered,
   * and the abort below prints the report either way. */
  ok = rolltui_bindings_load_json(b, text, tlen, ROLLTUI_PROTOCOL_LEGACY, rolltui_bindings_library_scope, NULL, NULL,
                                  NULL, &rep);
  if (!ok || !rolltui_bindings_report_clean(&rep)) {
    size_t slen = 0;
    const char* stext;
    rolltui_bindings_report_summary(&rep, &summary);
    stext = rolltui_str_get(&summary, &slen);
    rolltui_fatal("the shipped default bindings are broken: %.*s", (int)slen, stext);
  }
  rolltui_bindings_report_release(&rep);

  /* The shipped default LAYOUT declares the app scope; the two files ship together, so this is
   * the library's one complete "default screen + default keys". */
  actions = rolltui_layout_shipped_default_actions(c, &n);
  rolltui_bindings_declare(b, actions, n, NULL, 0);

  /* A row for an action no shipped layout declares is a key EVERY host advertises and cannot
   * press. A mounted tool's chords come from
   * the tool, so a tool row in this file stops the build rather than shipping. */
  rows = rolltui_bindings_row_count(b);
  for (i = 0; i < rows; ++i) {
    size_t alen = 0;
    const char* a = rolltui_bindings_row_at(b, i, &alen);
    if (!rolltui_bindings_has(b, a, alen)) {
      rolltui_fatal("the shipped default bindings bind '%.*s', which no shipped layout declares (a mounted tool's chords "
                    "belong to the tool)",
                    (int)alen, a);
    }
  }

  c->bindings = b;
  return c->bindings;
}


/* ---- the HELP spelling of an action's chords ------------------------------------------------
 * "Ctrl-W, Alt-Backspace": every chord bound to `action` that THIS TERMINAL can actually
 * deliver, in display form, comma-separated. The undeliverable filter is the point and is why
 * this is not a loop a caller writes: a chord the terminal cannot report must not be offered
 * as a shortcut. It had THREE implementations when this was written —
 * `Bindings::chords_text`, `help_chords_text` in `rolltui_widget_kinds.c`, and a fresh one an
 * agent had to write in `tools/keys_editor.cpp` because it could not reach either. Two is the
 * tell (`rolltui/rolltui.h` rule 5); three is not an argument any more.
 * CLEARS `out` first, unlike the appending shape most of this API takes: it is one value. */
void rolltui_bindings_chords_text(const RolltuiBindings* b, const char* action, size_t alen, RolltuiStr* out) {
  const unsigned char proto = rolltui_key_active_protocol();
  const size_t n = rolltui_bindings_chord_count(b, action, alen);
  size_t i;
  if (!out) return;
  rolltui_str_clear(out);
  for (i = 0; i < n; ++i) {
    RolltuiChord c;
    char buf[ROLLTUI_CHORD_STRING_MAX];
    size_t len;
    if (!rolltui_bindings_chord_at(b, action, alen, i, &c)) continue;
    if (!rolltui_key_deliverable(&c, proto)) continue;
    len = rolltui_chord_display(&c, buf, sizeof buf);
    if (out->n > 0) rolltui_str_append(out, ", ", 2);
    rolltui_str_append(out, buf, len);
  }
  {
    const size_t ns = rolltui_bindings_sequence_count(b, action, alen);
    for (i = 0; i < ns; ++i) { /* a sequence as it is pressed: `g g` */
      RolltuiChord keys[ROLLTUI_SEQUENCE_MAX];
      const size_t k = rolltui_bindings_sequence_at(b, action, alen, i, keys, ROLLTUI_SEQUENCE_MAX);
      size_t j;
      int deliverable = 1;
      for (j = 0; j < k; ++j) deliverable = deliverable && rolltui_key_deliverable(&keys[j], proto);
      if (!deliverable) continue;
      if (out->n > 0) rolltui_str_append(out, ", ", 2);
      for (j = 0; j < k; ++j) {
        char buf[ROLLTUI_CHORD_STRING_MAX];
        const size_t len = rolltui_chord_display(&keys[j], buf, sizeof buf);
        if (j) rolltui_str_append(out, " ", 1);
        rolltui_str_append(out, buf, len);
      }
    }
  }
}


size_t rolltui_undeliverable_reason_fn(void* ctx, const RolltuiChord* k, unsigned char protocol, char* out,
                                       size_t cap) {
  const int code = rolltui_key_undeliverable_reason(k, protocol);
  size_t n = 0;
  const char* text = rolltui_key_undeliverable_text(code, &n);
  (void)ctx;
  if (n > cap) n = cap;
  if (n != 0) memcpy(out, text, n);
  return n;
}
