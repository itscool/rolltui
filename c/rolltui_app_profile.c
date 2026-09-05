/* rolltui/c/rolltui_app_profile.c — see rolltui_app_profile.h. `build_from_json` and
 * `rolltui_app_profile_dump` are a direct port of `rolltui/AppProfile.cpp`'s original
 * `load_app_profile(const json::Value&, …)` and `app_profile_to_json`, preserving every key
 * name, every default and every report message exactly; `tests/app_profile_test.cpp` (which
 * runs both roll and the studio) is the oracle, unchanged by this port.
 *
 * Every allocation goes through `rolltui_alloc.h`'s closed set, named at the call site. There
 * is no process-wide retention: a profile is parsed, read and freed within one call's scope. */
#include "rolltui/c/rolltui_app_profile.h"

#include <string.h>
#include <stdio.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_json.h"

/* A literal C string plus its length, computed once here rather than hand-counted at every
 * `rolltui_json_get`/`_set` call site — this file's own version of `rolltui_json.c`'s `JLIT`,
 * for the same reason: a hand-counted length is right until the literal is edited and
 * nothing then notices it is not. Parsing and dumping a profile happen once per load, never
 * per frame, so the runtime `strlen` this costs is not one this library's budget covers. */
#define K(s) (s), strlen(s)

/* ---- the profile's own shape: opaque outside this file (see the header comment) --------- */

typedef struct {
  RolltuiStr name, description;
} PAction;

typedef struct {
  RolltuiStr name;
  int rule;
  RolltuiStr describes;
} PKind;

typedef struct {
  RolltuiStr name, sample;
} PDocument;

typedef struct {
  RolltuiStr label, value;
} PRowSample;

typedef struct {
  RolltuiStr name;
  PRowSample* sample;
  size_t sample_n, sample_cap;
} PRowSource;

typedef struct {
  RolltuiStr name, json;
} PMenuFile;

struct RolltuiAppProfile {
  RolltuiStr app;
  int min_width, min_height;
  PAction* actions;
  size_t actions_n, actions_cap;
  PKind* kinds;
  size_t kinds_n, kinds_cap;
  PDocument* documents;
  size_t documents_n, documents_cap;
  PRowSource* rows;
  size_t rows_n, rows_cap;
  RolltuiStr* submits;
  size_t submits_n, submits_cap;
  RolltuiStr* notes;
  size_t notes_n, notes_cap;
  PMenuFile* menus;
  size_t menus_n, menus_cap;
  RolltuiStr help_lead, help_note;
  RolltuiStr* help_scopes;
  size_t help_scopes_n, help_scopes_cap;
};

/* ---- small local helpers -----------------------------------------------------------------
 * `sbuf_add`/`SBuf` mirror `rolltui_json.c`'s own private growing-byte-buffer exactly (the
 * same GROWING AMORTISED role, in a different translation unit — a static helper has no
 * external linkage to share, and this is a ten-line wrapper over `rolltui_grow`, not a
 * strategy this file is inventing). */
typedef struct {
  char* p;
  size_t len, cap;
} SBuf;

static void sbuf_add(SBuf* b, const char* s, size_t n) {
  if (!n) return;
  b->p = (char*)rolltui_grow(b->p, &b->cap, b->len + n, sizeof *b->p);
  memcpy(b->p + b->len, s, n);
  b->len += n;
}

static const char* borrow(const RolltuiStr* s, size_t* len) {
  if (len) *len = s->n;
  return s->p ? s->p : "";
}

static int streq(const char* s, size_t slen, const char* lit) {
  size_t litlen = strlen(lit);
  return slen == litlen && (litlen == 0 || memcmp(s, lit, litlen) == 0);
}

static int int_or(const RolltuiJsonValue* v, int def) {
  return rolltui_json_is_number(v) ? (int)rolltui_json_as_number(v, (double)def) : def;
}

/* The source rule as a word, one table both directions — `AppProfile.cpp`'s original
 * `rule_name`/`rule_from_name`, ported verbatim (the C++ shim no longer needs its own copy:
 * the only place a rule is spelled as a string is here, since the shim converts the INT this
 * file uses into `SourceRule` with a `static_cast`, the values being the same by a documented
 * fact rather than a coincidence — see this header's top comment). */
static const char* rule_name(int rule) {
  switch (rule) {
    case ROLLTUI_APP_PROFILE_SOURCE_REQUIRED: return "required";
    case ROLLTUI_APP_PROFILE_SOURCE_OPTIONAL: return "optional";
    default: return "forbidden";
  }
}

static int rule_from_name(const char* s, size_t len, int* out) {
  if (streq(s, len, "required")) { *out = ROLLTUI_APP_PROFILE_SOURCE_REQUIRED; return 1; }
  if (streq(s, len, "optional")) { *out = ROLLTUI_APP_PROFILE_SOURCE_OPTIONAL; return 1; }
  if (streq(s, len, "forbidden")) { *out = ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN; return 1; }
  return 0;
}

/* ---- the report ---------------------------------------------------------------------- */

void rolltui_app_profile_report_release(RolltuiAppProfileReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->bad_values_n; ++i) rolltui_str_free(&r->bad_values[i]);
  rolltui_mem_free(r->bad_values);
  memset(r, 0, sizeof *r);
}

void rolltui_app_profile_report_set_error(RolltuiAppProfileReport* r, const char* s, size_t len) {
  rolltui_str_set(&r->error, s, len);
}

void rolltui_app_profile_report_add_unknown_key(RolltuiAppProfileReport* r, const char* s, size_t len) {
  /* GROWING AMORTISED (rolltui_alloc.h strategy 2): an array of small owned strings, the
   * same shape `rolltui_presets.c`'s `NameList` uses. */
  r->unknown_keys =
      (RolltuiStr*)rolltui_grow_zeroed(r->unknown_keys, &r->unknown_keys_cap, r->unknown_keys_n + 1, sizeof *r->unknown_keys);
  rolltui_str_set(&r->unknown_keys[r->unknown_keys_n++], s, len);
}

void rolltui_app_profile_report_add_bad_value(RolltuiAppProfileReport* r, const char* s, size_t len) {
  r->bad_values =
      (RolltuiStr*)rolltui_grow_zeroed(r->bad_values, &r->bad_values_cap, r->bad_values_n + 1, sizeof *r->bad_values);
  rolltui_str_set(&r->bad_values[r->bad_values_n++], s, len);
}

int rolltui_app_profile_report_clean(const RolltuiAppProfileReport* r) {
  return r->error.n == 0 && r->unknown_keys_n == 0 && r->bad_values_n == 0;
}

void rolltui_app_profile_report_summary(const RolltuiAppProfileReport* r, RolltuiStr* out) {
  SBuf b;
  size_t i;
  int first = 1;
  if (rolltui_app_profile_report_clean(r)) {
    rolltui_str_clear(out);
    return;
  }
  if (r->error.n != 0) {
    rolltui_str_set(out, r->error.p, r->error.n);
    return;
  }
  memset(&b, 0, sizeof b);
  for (i = 0; i < r->bad_values_n; ++i) {
    if (!first) sbuf_add(&b, "; ", 2);
    first = 0;
    sbuf_add(&b, "bad: ", 5);
    sbuf_add(&b, r->bad_values[i].p, r->bad_values[i].n);
  }
  for (i = 0; i < r->unknown_keys_n; ++i) {
    if (!first) sbuf_add(&b, "; ", 2);
    first = 0;
    sbuf_add(&b, "unknown: ", 9);
    sbuf_add(&b, r->unknown_keys[i].p, r->unknown_keys[i].n);
  }
  rolltui_str_set(out, b.p, b.len);
  rolltui_mem_free(b.p);
}

/* ---- building one from scratch --------------------------------------------------------- */

RolltuiAppProfile* rolltui_app_profile_new(void) {
  /* OWNED, LONG-LIVED (rolltui_alloc.h strategy 4). */
  RolltuiAppProfile* p = (RolltuiAppProfile*)rolltui_mem_alloc(sizeof(RolltuiAppProfile));
  memset(p, 0, sizeof *p);
  return p;
}

void rolltui_app_profile_set_app(RolltuiAppProfile* p, const char* s, size_t len) { rolltui_str_set(&p->app, s, len); }

void rolltui_app_profile_set_min_size(RolltuiAppProfile* p, int width, int height) {
  p->min_width = width;
  p->min_height = height;
}

void rolltui_app_profile_add_action(RolltuiAppProfile* p, const char* name, size_t name_len, const char* desc,
                                    size_t desc_len) {
  PAction* a;
  p->actions = (PAction*)rolltui_grow_zeroed(p->actions, &p->actions_cap, p->actions_n + 1, sizeof *p->actions);
  a = &p->actions[p->actions_n++];
  rolltui_str_set(&a->name, name, name_len);
  rolltui_str_set(&a->description, desc, desc_len);
}

void rolltui_app_profile_add_kind(RolltuiAppProfile* p, const char* name, size_t name_len, int rule,
                                  const char* describes, size_t describes_len) {
  PKind* k;
  p->kinds = (PKind*)rolltui_grow_zeroed(p->kinds, &p->kinds_cap, p->kinds_n + 1, sizeof *p->kinds);
  k = &p->kinds[p->kinds_n++];
  rolltui_str_set(&k->name, name, name_len);
  k->rule = rule;
  rolltui_str_set(&k->describes, describes, describes_len);
}

void rolltui_app_profile_add_document(RolltuiAppProfile* p, const char* name, size_t name_len, const char* sample,
                                      size_t sample_len) {
  PDocument* d;
  p->documents = (PDocument*)rolltui_grow_zeroed(p->documents, &p->documents_cap, p->documents_n + 1, sizeof *p->documents);
  d = &p->documents[p->documents_n++];
  rolltui_str_set(&d->name, name, name_len);
  rolltui_str_set(&d->sample, sample, sample_len);
}

size_t rolltui_app_profile_add_row(RolltuiAppProfile* p, const char* name, size_t name_len) {
  size_t idx = p->rows_n;
  PRowSource* r;
  p->rows = (PRowSource*)rolltui_grow_zeroed(p->rows, &p->rows_cap, p->rows_n + 1, sizeof *p->rows);
  r = &p->rows[p->rows_n++];
  rolltui_str_set(&r->name, name, name_len);
  return idx;
}

void rolltui_app_profile_row_add_sample(RolltuiAppProfile* p, size_t row_i, const char* label, size_t label_len,
                                       const char* value, size_t value_len) {
  PRowSource* r;
  PRowSample* s;
  if (row_i >= p->rows_n) return;
  r = &p->rows[row_i];
  r->sample = (PRowSample*)rolltui_grow_zeroed(r->sample, &r->sample_cap, r->sample_n + 1, sizeof *r->sample);
  s = &r->sample[r->sample_n++];
  rolltui_str_set(&s->label, label, label_len);
  rolltui_str_set(&s->value, value, value_len);
}

void rolltui_app_profile_add_submit(RolltuiAppProfile* p, const char* s, size_t len) {
  p->submits = (RolltuiStr*)rolltui_grow_zeroed(p->submits, &p->submits_cap, p->submits_n + 1, sizeof *p->submits);
  rolltui_str_set(&p->submits[p->submits_n++], s, len);
}

void rolltui_app_profile_add_note(RolltuiAppProfile* p, const char* s, size_t len) {
  p->notes = (RolltuiStr*)rolltui_grow_zeroed(p->notes, &p->notes_cap, p->notes_n + 1, sizeof *p->notes);
  rolltui_str_set(&p->notes[p->notes_n++], s, len);
}

void rolltui_app_profile_add_menu(RolltuiAppProfile* p, const char* name, size_t name_len, const char* json,
                                  size_t json_len) {
  PMenuFile* m;
  p->menus = (PMenuFile*)rolltui_grow_zeroed(p->menus, &p->menus_cap, p->menus_n + 1, sizeof *p->menus);
  m = &p->menus[p->menus_n++];
  rolltui_str_set(&m->name, name, name_len);
  rolltui_str_set(&m->json, json, json_len);
}

void rolltui_app_profile_set_help(RolltuiAppProfile* p, const char* lead, size_t lead_len, const char* note,
                                  size_t note_len) {
  rolltui_str_set(&p->help_lead, lead, lead_len);
  rolltui_str_set(&p->help_note, note, note_len);
}

void rolltui_app_profile_add_help_scope(RolltuiAppProfile* p, const char* s, size_t len) {
  p->help_scopes =
      (RolltuiStr*)rolltui_grow_zeroed(p->help_scopes, &p->help_scopes_cap, p->help_scopes_n + 1, sizeof *p->help_scopes);
  rolltui_str_set(&p->help_scopes[p->help_scopes_n++], s, len);
}

void rolltui_app_profile_free(RolltuiAppProfile* p) {
  size_t i, j;
  if (!p) return;
  rolltui_str_free(&p->app);
  for (i = 0; i < p->actions_n; ++i) {
    rolltui_str_free(&p->actions[i].name);
    rolltui_str_free(&p->actions[i].description);
  }
  rolltui_mem_free(p->actions);
  for (i = 0; i < p->kinds_n; ++i) {
    rolltui_str_free(&p->kinds[i].name);
    rolltui_str_free(&p->kinds[i].describes);
  }
  rolltui_mem_free(p->kinds);
  for (i = 0; i < p->documents_n; ++i) {
    rolltui_str_free(&p->documents[i].name);
    rolltui_str_free(&p->documents[i].sample);
  }
  rolltui_mem_free(p->documents);
  for (i = 0; i < p->rows_n; ++i) {
    PRowSource* r = &p->rows[i];
    rolltui_str_free(&r->name);
    for (j = 0; j < r->sample_n; ++j) {
      rolltui_str_free(&r->sample[j].label);
      rolltui_str_free(&r->sample[j].value);
    }
    rolltui_mem_free(r->sample);
  }
  rolltui_mem_free(p->rows);
  for (i = 0; i < p->submits_n; ++i) rolltui_str_free(&p->submits[i]);
  rolltui_mem_free(p->submits);
  for (i = 0; i < p->notes_n; ++i) rolltui_str_free(&p->notes[i]);
  rolltui_mem_free(p->notes);
  for (i = 0; i < p->menus_n; ++i) {
    rolltui_str_free(&p->menus[i].name);
    rolltui_str_free(&p->menus[i].json);
  }
  rolltui_mem_free(p->menus);
  rolltui_str_free(&p->help_lead);
  rolltui_str_free(&p->help_note);
  for (i = 0; i < p->help_scopes_n; ++i) rolltui_str_free(&p->help_scopes[i]);
  rolltui_mem_free(p->help_scopes);
  rolltui_mem_free(p);
}

/* ---- reading one ------------------------------------------------------------------------ */

const char* rolltui_app_profile_app(const RolltuiAppProfile* p, size_t* len) {
  if (!p) {
    if (len) *len = 0;
    return "";
  }
  return borrow(&p->app, len);
}
int rolltui_app_profile_min_width(const RolltuiAppProfile* p) { return p ? p->min_width : 0; }
int rolltui_app_profile_min_height(const RolltuiAppProfile* p) { return p ? p->min_height : 0; }

size_t rolltui_app_profile_action_count(const RolltuiAppProfile* p) { return p ? p->actions_n : 0; }
const char* rolltui_app_profile_action_name(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->actions_n) { if (len) *len = 0; return ""; }
  return borrow(&p->actions[i].name, len);
}
const char* rolltui_app_profile_action_description(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->actions_n) { if (len) *len = 0; return ""; }
  return borrow(&p->actions[i].description, len);
}

size_t rolltui_app_profile_kind_count(const RolltuiAppProfile* p) { return p ? p->kinds_n : 0; }
const char* rolltui_app_profile_kind_name(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->kinds_n) { if (len) *len = 0; return ""; }
  return borrow(&p->kinds[i].name, len);
}
int rolltui_app_profile_kind_rule(const RolltuiAppProfile* p, size_t i) {
  if (!p || i >= p->kinds_n) return ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN;
  return p->kinds[i].rule;
}
const char* rolltui_app_profile_kind_describes(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->kinds_n) { if (len) *len = 0; return ""; }
  return borrow(&p->kinds[i].describes, len);
}

size_t rolltui_app_profile_document_count(const RolltuiAppProfile* p) { return p ? p->documents_n : 0; }
const char* rolltui_app_profile_document_name(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->documents_n) { if (len) *len = 0; return ""; }
  return borrow(&p->documents[i].name, len);
}
const char* rolltui_app_profile_document_sample(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->documents_n) { if (len) *len = 0; return ""; }
  return borrow(&p->documents[i].sample, len);
}

size_t rolltui_app_profile_row_count(const RolltuiAppProfile* p) { return p ? p->rows_n : 0; }
const char* rolltui_app_profile_row_name(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->rows_n) { if (len) *len = 0; return ""; }
  return borrow(&p->rows[i].name, len);
}
size_t rolltui_app_profile_row_sample_count(const RolltuiAppProfile* p, size_t i) {
  return (p && i < p->rows_n) ? p->rows[i].sample_n : 0;
}
const char* rolltui_app_profile_row_sample_label(const RolltuiAppProfile* p, size_t i, size_t j, size_t* len) {
  if (!p || i >= p->rows_n || j >= p->rows[i].sample_n) { if (len) *len = 0; return ""; }
  return borrow(&p->rows[i].sample[j].label, len);
}
const char* rolltui_app_profile_row_sample_value(const RolltuiAppProfile* p, size_t i, size_t j, size_t* len) {
  if (!p || i >= p->rows_n || j >= p->rows[i].sample_n) { if (len) *len = 0; return ""; }
  return borrow(&p->rows[i].sample[j].value, len);
}

size_t rolltui_app_profile_submit_count(const RolltuiAppProfile* p) { return p ? p->submits_n : 0; }
const char* rolltui_app_profile_submit_at(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->submits_n) { if (len) *len = 0; return ""; }
  return borrow(&p->submits[i], len);
}

size_t rolltui_app_profile_note_count(const RolltuiAppProfile* p) { return p ? p->notes_n : 0; }
const char* rolltui_app_profile_note_at(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->notes_n) { if (len) *len = 0; return ""; }
  return borrow(&p->notes[i], len);
}

size_t rolltui_app_profile_menu_count(const RolltuiAppProfile* p) { return p ? p->menus_n : 0; }
const char* rolltui_app_profile_menu_name(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->menus_n) { if (len) *len = 0; return ""; }
  return borrow(&p->menus[i].name, len);
}
const char* rolltui_app_profile_menu_json(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->menus_n) { if (len) *len = 0; return ""; }
  return borrow(&p->menus[i].json, len);
}

const char* rolltui_app_profile_help_lead(const RolltuiAppProfile* p, size_t* len) {
  if (!p) { if (len) *len = 0; return ""; }
  return borrow(&p->help_lead, len);
}
const char* rolltui_app_profile_help_note(const RolltuiAppProfile* p, size_t* len) {
  if (!p) { if (len) *len = 0; return ""; }
  return borrow(&p->help_note, len);
}
size_t rolltui_app_profile_help_scope_count(const RolltuiAppProfile* p) { return p ? p->help_scopes_n : 0; }
const char* rolltui_app_profile_help_scope_at(const RolltuiAppProfile* p, size_t i, size_t* len) {
  if (!p || i >= p->help_scopes_n) { if (len) *len = 0; return ""; }
  return borrow(&p->help_scopes[i], len);
}

/* ---- parse: TEXT in, built through the SAME primitives `_new`/`add_*`/`set_*` above use,
 * so there is exactly one way a profile's fields get set, read by two callers (this walk of
 * parsed JSON, and the C++ shim converting a hand-built `rolltui::AppProfile`) rather than
 * duplicated for each. A direct port of `load_app_profile(const json::Value&, report)`. ---- */

static RolltuiAppProfile* build_from_json(const RolltuiJsonValue* v, RolltuiAppProfileReport* report) {
  RolltuiAppProfile* p;
  size_t i, n;
  size_t app_len = 0;
  const char* app_s;

  if (!rolltui_json_is_object(v)) {
    rolltui_app_profile_report_set_error(report, K("an app profile must be a JSON object"));
    return NULL;
  }
  app_s = rolltui_json_as_string(rolltui_json_get(v, K("app")), "", 0, &app_len);
  if (app_len == 0) {
    rolltui_app_profile_report_set_error(report, K("an app profile needs an \"app\" name"));
    return NULL;
  }
  p = rolltui_app_profile_new();
  rolltui_app_profile_set_app(p, app_s, app_len);
  rolltui_app_profile_set_min_size(p, int_or(rolltui_json_get(v, K("min_width")), 0),
                                  int_or(rolltui_json_get(v, K("min_height")), 0));

  n = rolltui_json_object_size(v);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    if (!(streq(k, klen, "app") || streq(k, klen, "min_width") || streq(k, klen, "min_height") ||
          streq(k, klen, "actions") || streq(k, klen, "kinds") || streq(k, klen, "sources") ||
          streq(k, klen, "menus") || streq(k, klen, "help")))
      rolltui_app_profile_report_add_unknown_key(report, k, klen);
  }

  /* actions: name -> description, the same shape a layout's "actions" has. */
  {
    const RolltuiJsonValue* a = rolltui_json_get(v, K("actions"));
    if (rolltui_json_is_object(a)) {
      size_t an = rolltui_json_object_size(a);
      for (i = 0; i < an; ++i) {
        size_t namelen = 0;
        const char* name = rolltui_json_object_key_at(a, i, &namelen);
        const RolltuiJsonValue* d = rolltui_json_object_value_at(a, i);
        if (!rolltui_json_is_string(d)) {
          char buf[320];
          size_t bn = (size_t)snprintf(buf, sizeof buf, "actions.%.*s: expected a string", (int)namelen, name);
          rolltui_app_profile_report_add_bad_value(report, buf, bn);
          continue;
        }
        {
          size_t dlen = 0;
          const char* dstr = rolltui_json_as_string(d, "", 0, &dlen);
          rolltui_app_profile_add_action(p, name, namelen, dstr, dlen);
        }
      }
    } else if (!rolltui_json_is_null(a)) {
      rolltui_app_profile_report_add_bad_value(report, K("actions: expected an object of name -> description"));
    }
  }

  /* kinds the app registers. */
  {
    const RolltuiJsonValue* ks = rolltui_json_get(v, K("kinds"));
    if (rolltui_json_is_array(ks)) {
      size_t kn = rolltui_json_array_size(ks);
      for (i = 0; i < kn; ++i) {
        const RolltuiJsonValue* k = rolltui_json_array_at(ks, i);
        size_t namelen = 0;
        const char* name = rolltui_json_as_string(rolltui_json_get(k, K("name")), "", 0, &namelen);
        if (namelen == 0) {
          rolltui_app_profile_report_add_bad_value(report, K("kinds[]: a kind needs a name"));
          continue;
        }
        {
          size_t rulelen = 0;
          const char* rulestr = rolltui_json_as_string(rolltui_json_get(k, K("source")), "forbidden", 9, &rulelen);
          size_t deslen = 0;
          const char* des = rolltui_json_as_string(rolltui_json_get(k, K("describes")), "", 0, &deslen);
          int rule = ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN; /* Kind::rule's in-class default */
          if (!rule_from_name(rulestr, rulelen, &rule)) {
            char buf[320];
            size_t bn = (size_t)snprintf(buf, sizeof buf, "kinds.%.*s.source: '%.*s' is not required | optional | forbidden",
                                         (int)namelen, name, (int)rulelen, rulestr);
            rolltui_app_profile_report_add_bad_value(report, buf, bn);
            rule = ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN;
          }
          rolltui_app_profile_add_kind(p, name, namelen, rule, des, deslen);
        }
      }
    } else if (!rolltui_json_is_null(ks)) {
      rolltui_app_profile_report_add_bad_value(report, K("kinds: expected an array"));
    }
  }

  /* sources: documents, rows, submits, notes. */
  {
    const RolltuiJsonValue* s = rolltui_json_get(v, K("sources"));
    if (rolltui_json_is_object(s)) {
      size_t sn = rolltui_json_object_size(s);
      size_t dn, rn, on;
      const RolltuiJsonValue *docs, *rows, *submits, *notes;
      for (i = 0; i < sn; ++i) {
        size_t klen = 0;
        const char* k = rolltui_json_object_key_at(s, i, &klen);
        if (!(streq(k, klen, "documents") || streq(k, klen, "rows") || streq(k, klen, "submits") ||
              streq(k, klen, "notes"))) {
          char buf[320];
          size_t bn = (size_t)snprintf(buf, sizeof buf, "sources.%.*s", (int)klen, k);
          rolltui_app_profile_report_add_unknown_key(report, buf, bn);
        }
      }
      docs = rolltui_json_get(s, K("documents"));
      dn = rolltui_json_array_size(docs);
      for (i = 0; i < dn; ++i) {
        const RolltuiJsonValue* d = rolltui_json_array_at(docs, i);
        size_t namelen = 0;
        const char* name = rolltui_json_as_string(rolltui_json_get(d, K("name")), "", 0, &namelen);
        if (namelen == 0) {
          rolltui_app_profile_report_add_bad_value(report, K("sources.documents[]: a document needs a name"));
          continue;
        }
        {
          size_t samplelen = 0;
          const char* sample = rolltui_json_as_string(rolltui_json_get(d, K("sample")), "", 0, &samplelen);
          rolltui_app_profile_add_document(p, name, namelen, sample, samplelen);
        }
      }
      rows = rolltui_json_get(s, K("rows"));
      rn = rolltui_json_array_size(rows);
      for (i = 0; i < rn; ++i) {
        const RolltuiJsonValue* r = rolltui_json_array_at(rows, i);
        size_t namelen = 0;
        const char* name = rolltui_json_as_string(rolltui_json_get(r, K("name")), "", 0, &namelen);
        if (namelen == 0) {
          rolltui_app_profile_report_add_bad_value(report, K("sources.rows[]: a row source needs a name"));
          continue;
        }
        {
          size_t row_i = rolltui_app_profile_add_row(p, name, namelen);
          const RolltuiJsonValue* sample = rolltui_json_get(r, K("sample"));
          size_t sn2 = rolltui_json_array_size(sample);
          size_t j;
          for (j = 0; j < sn2; ++j) {
            const RolltuiJsonValue* row = rolltui_json_array_at(sample, j);
            size_t llen = 0, vlen = 0;
            const char* label = rolltui_json_as_string(rolltui_json_get(row, K("label")), "", 0, &llen);
            const char* value = rolltui_json_as_string(rolltui_json_get(row, K("value")), "", 0, &vlen);
            rolltui_app_profile_row_add_sample(p, row_i, label, llen, value, vlen);
          }
        }
      }
      submits = rolltui_json_get(s, K("submits"));
      on = rolltui_json_array_size(submits);
      for (i = 0; i < on; ++i) {
        const RolltuiJsonValue* n2 = rolltui_json_array_at(submits, i);
        if (rolltui_json_is_string(n2)) {
          size_t slen = 0;
          const char* sstr = rolltui_json_as_string(n2, "", 0, &slen);
          rolltui_app_profile_add_submit(p, sstr, slen);
        }
      }
      notes = rolltui_json_get(s, K("notes"));
      on = rolltui_json_array_size(notes);
      for (i = 0; i < on; ++i) {
        const RolltuiJsonValue* n2 = rolltui_json_array_at(notes, i);
        if (rolltui_json_is_string(n2)) {
          size_t slen = 0;
          const char* sstr = rolltui_json_as_string(n2, "", 0, &slen);
          rolltui_app_profile_add_note(p, sstr, slen);
        }
      }
    } else if (!rolltui_json_is_null(s)) {
      rolltui_app_profile_report_add_bad_value(report, K("sources: expected an object"));
    }
  }

  /* help: the app's key scopes, with its lead and note lines (Phase 11 m5b). */
  {
    const RolltuiJsonValue* h = rolltui_json_get(v, K("help"));
    if (rolltui_json_is_object(h)) {
      size_t hn = rolltui_json_object_size(h);
      size_t leadlen = 0, notelen = 0, sc;
      const char *lead, *note;
      const RolltuiJsonValue* scopes;
      for (i = 0; i < hn; ++i) {
        size_t klen = 0;
        const char* k = rolltui_json_object_key_at(h, i, &klen);
        if (!(streq(k, klen, "lead") || streq(k, klen, "note") || streq(k, klen, "scopes"))) {
          char buf[320];
          size_t bn = (size_t)snprintf(buf, sizeof buf, "help.%.*s", (int)klen, k);
          rolltui_app_profile_report_add_unknown_key(report, buf, bn);
        }
      }
      lead = rolltui_json_as_string(rolltui_json_get(h, K("lead")), "", 0, &leadlen);
      note = rolltui_json_as_string(rolltui_json_get(h, K("note")), "", 0, &notelen);
      rolltui_app_profile_set_help(p, lead, leadlen, note, notelen);
      scopes = rolltui_json_get(h, K("scopes"));
      sc = rolltui_json_array_size(scopes);
      for (i = 0; i < sc; ++i) {
        const RolltuiJsonValue* sv = rolltui_json_array_at(scopes, i);
        if (rolltui_json_is_string(sv)) {
          size_t slen = 0;
          const char* sstr = rolltui_json_as_string(sv, "", 0, &slen);
          rolltui_app_profile_add_help_scope(p, sstr, slen);
        }
      }
    } else if (!rolltui_json_is_null(h)) {
      rolltui_app_profile_report_add_bad_value(report, K("help: expected an object"));
    }
  }

  /* menus: the app's own menu files, verbatim. */
  {
    const RolltuiJsonValue* ms = rolltui_json_get(v, K("menus"));
    if (rolltui_json_is_array(ms)) {
      size_t mn = rolltui_json_array_size(ms);
      for (i = 0; i < mn; ++i) {
        const RolltuiJsonValue* m = rolltui_json_array_at(ms, i);
        size_t namelen = 0;
        const char* name = rolltui_json_as_string(rolltui_json_get(m, K("name")), "", 0, &namelen);
        if (namelen == 0) {
          rolltui_app_profile_report_add_bad_value(report, K("menus[]: a menu needs a name"));
          continue;
        }
        {
          size_t jlen = 0;
          const char* jtext = rolltui_json_as_string(rolltui_json_get(m, K("json")), "", 0, &jlen);
          rolltui_app_profile_add_menu(p, name, namelen, jtext, jlen);
        }
      }
    } else if (!rolltui_json_is_null(ms)) {
      rolltui_app_profile_report_add_bad_value(report, K("menus: expected an array"));
    }
  }

  return p;
}

RolltuiAppProfile* rolltui_app_profile_parse(const char* text, size_t len, RolltuiAppProfileReport* report) {
  RolltuiStr jerr;
  RolltuiJsonValue* root;
  RolltuiAppProfile* p;
  memset(&jerr, 0, sizeof jerr);
  rolltui_app_profile_report_release(report); /* == "report = AppProfileReport{}" */
  root = rolltui_json_parse(text, len, &jerr);
  if (!root) {
    rolltui_app_profile_report_set_error(report, jerr.p ? jerr.p : "", jerr.n);
    rolltui_str_free(&jerr);
    return NULL;
  }
  rolltui_str_free(&jerr);
  p = build_from_json(root, report);
  rolltui_json_free(root);
  return p;
}

/* ---- dump: a direct port of `app_profile_to_json`, except the tree it builds is freed
 * inside this call rather than handed back — see this file's header comment. ------------- */

void rolltui_app_profile_dump(const RolltuiAppProfile* p, int indent, RolltuiStr* out) {
  RolltuiJsonValue* root = rolltui_json_object();
  RolltuiJsonValue *actions, *kinds, *sources, *docs, *rows, *submits, *notes, *help, *scopes, *menus;
  size_t i, j;

  rolltui_json_set(root, K("app"), rolltui_json_string(p->app.p, p->app.n));
  rolltui_json_set(root, K("min_width"), rolltui_json_number(p->min_width));
  rolltui_json_set(root, K("min_height"), rolltui_json_number(p->min_height));

  actions = rolltui_json_object();
  for (i = 0; i < p->actions_n; ++i)
    rolltui_json_set(actions, p->actions[i].name.p, p->actions[i].name.n,
                     rolltui_json_string(p->actions[i].description.p, p->actions[i].description.n));
  rolltui_json_set(root, K("actions"), actions);

  kinds = rolltui_json_array();
  for (i = 0; i < p->kinds_n; ++i) {
    RolltuiJsonValue* o = rolltui_json_object();
    const char* rn = rule_name(p->kinds[i].rule);
    rolltui_json_set(o, K("name"), rolltui_json_string(p->kinds[i].name.p, p->kinds[i].name.n));
    rolltui_json_set(o, K("source"), rolltui_json_string(rn, strlen(rn)));
    rolltui_json_set(o, K("describes"), rolltui_json_string(p->kinds[i].describes.p, p->kinds[i].describes.n));
    rolltui_json_array_push(kinds, o);
  }
  rolltui_json_set(root, K("kinds"), kinds);

  sources = rolltui_json_object();

  docs = rolltui_json_array();
  for (i = 0; i < p->documents_n; ++i) {
    RolltuiJsonValue* o = rolltui_json_object();
    rolltui_json_set(o, K("name"), rolltui_json_string(p->documents[i].name.p, p->documents[i].name.n));
    rolltui_json_set(o, K("sample"), rolltui_json_string(p->documents[i].sample.p, p->documents[i].sample.n));
    rolltui_json_array_push(docs, o);
  }
  rolltui_json_set(sources, K("documents"), docs);

  rows = rolltui_json_array();
  for (i = 0; i < p->rows_n; ++i) {
    RolltuiJsonValue* o = rolltui_json_object();
    RolltuiJsonValue* sample = rolltui_json_array();
    rolltui_json_set(o, K("name"), rolltui_json_string(p->rows[i].name.p, p->rows[i].name.n));
    for (j = 0; j < p->rows[i].sample_n; ++j) {
      RolltuiJsonValue* row = rolltui_json_object();
      rolltui_json_set(row, K("label"), rolltui_json_string(p->rows[i].sample[j].label.p, p->rows[i].sample[j].label.n));
      rolltui_json_set(row, K("value"), rolltui_json_string(p->rows[i].sample[j].value.p, p->rows[i].sample[j].value.n));
      rolltui_json_array_push(sample, row);
    }
    rolltui_json_set(o, K("sample"), sample);
    rolltui_json_array_push(rows, o);
  }
  rolltui_json_set(sources, K("rows"), rows);

  submits = rolltui_json_array();
  for (i = 0; i < p->submits_n; ++i) rolltui_json_array_push(submits, rolltui_json_string(p->submits[i].p, p->submits[i].n));
  rolltui_json_set(sources, K("submits"), submits);

  notes = rolltui_json_array();
  for (i = 0; i < p->notes_n; ++i) rolltui_json_array_push(notes, rolltui_json_string(p->notes[i].p, p->notes[i].n));
  rolltui_json_set(sources, K("notes"), notes);

  rolltui_json_set(root, K("sources"), sources);

  help = rolltui_json_object();
  rolltui_json_set(help, K("lead"), rolltui_json_string(p->help_lead.p, p->help_lead.n));
  rolltui_json_set(help, K("note"), rolltui_json_string(p->help_note.p, p->help_note.n));
  scopes = rolltui_json_array();
  for (i = 0; i < p->help_scopes_n; ++i)
    rolltui_json_array_push(scopes, rolltui_json_string(p->help_scopes[i].p, p->help_scopes[i].n));
  rolltui_json_set(help, K("scopes"), scopes);
  rolltui_json_set(root, K("help"), help);

  menus = rolltui_json_array();
  for (i = 0; i < p->menus_n; ++i) {
    RolltuiJsonValue* o = rolltui_json_object();
    rolltui_json_set(o, K("name"), rolltui_json_string(p->menus[i].name.p, p->menus[i].name.n));
    rolltui_json_set(o, K("json"), rolltui_json_string(p->menus[i].json.p, p->menus[i].json.n));
    rolltui_json_array_push(menus, o);
  }
  rolltui_json_set(root, K("menus"), menus);

  rolltui_json_dump(root, indent, out);
  rolltui_json_free(root);
}
