/*
 * rolltui/c/rolltui_settings.c — THE KEYS A PERSON CHANGES, AND WHERE EACH ONE LIVES.
 *
 * WHICH WORKING COPY A KEY BELONGS TO IS THE LIBRARY'S BUSINESS. The table below is the one
 * place that says it, and every operation a host performs on a setting — read it, resolve it
 * against the environment, change it, name the copy it landed in — routes from that column.
 * A host holds one `RolltuiSettings` over its three stores and spells keys; it never spells a
 * store.
 *
 * THE THREE DOMAIN REPORTS STAY THREE TYPES. A theme's report carries colours, a layout's
 * carries a window tree's, a bindings' carries chords, and they are not interchangeable — a
 * `void*` plus a tag between them is a crash rather than a compile error. So the concrete
 * report is made, read and destroyed INSIDE this file, in the one branch that knows which
 * store it is talking to, and what leaves is `RolltuiSettingsReport`: the text a host would
 * print. That is not a fourth report type; it holds no domain's fields and carries no tag.
 *
 * NO ALLOCATION HERE except the strings a report hands back: every row is a literal with
 * static storage duration, and every lookup is a linear scan of five rows.
 */
#include <string.h>

#include "rolltui/rolltui.h"

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_str.h"

/* ---- the table ------------------------------------------------------------------------- */
/* `store_ix` indexes `RolltuiSettings::store` below. It is an index and not the `store` name
 * re-compared, so the row's name is display text with nothing hanging off it. */
enum { STORE_THEME = 0, STORE_LAYOUT = 1, STORE_BINDINGS = 2, STORE_COUNT = 3 };

/* A key that is one FIELD inside the theme preset carries the predicate its value must pass
 * and the write that lands it; a key that names its store's preset carries neither, because
 * loading a preset is the store's own operation. */
typedef void (*FieldWriteFn)(void* value, void* ctx);

typedef struct {
  RolltuiSetting row;
  unsigned char store_ix;
  RolltuiThemePresetValidFn valid;
  FieldWriteFn write;
} SettingRow;

static void write_theme_mode(void* value, void* ctx) {
  RolltuiThemePresetValue* v = (RolltuiThemePresetValue*)value;
  const RolltuiStr* s = (const RolltuiStr*)ctx;
  rolltui_str_set(&v->mode, s->p, s->n);
}

static void write_color_depth(void* value, void* ctx) {
  RolltuiThemePresetValue* v = (RolltuiThemePresetValue*)value;
  const RolltuiStr* s = (const RolltuiStr*)ctx;
  rolltui_str_set(&v->depth, s->p, s->n);
}

/* `S` spells each row's five strings once, so a length is computed and never counted. */
#define S(k, e, b, v, st, names, ix, valid, write)                                                        \
  {{k, sizeof(k) - 1, e, sizeof(e) - 1, b, sizeof(b) - 1, v, sizeof(v) - 1, st, sizeof(st) - 1, names},   \
   ix, valid, write}

static const SettingRow kSettings[] = {
    S("theme", "THEME", "default", "a preset name (see `theme list`) or a preset file", "theme", 1,
      STORE_THEME, NULL, NULL),
    S("layout", "LAYOUT", "default", "a shipped layout, a layouts/ file name, or a layout file", "layout", 1,
      STORE_LAYOUT, NULL, NULL),
    S("theme_mode", "THEME_MODE", "auto", "auto | dark | light", "theme", 0,
      STORE_THEME, rolltui_theme_mode_setting_valid, write_theme_mode),
    S("color_depth", "COLOR_DEPTH", "auto", "auto | truecolor | 256 | 16 | mono", "theme", 0,
      STORE_THEME, rolltui_color_depth_setting_valid, write_color_depth),
    S("bindings", "BINDINGS", "default", "a bindings preset name (see `bindings list`) or a bindings file",
      "bindings", 1, STORE_BINDINGS, NULL, NULL),
};
#undef S
#define ROLLTUI_SETTINGS_COUNT (sizeof kSettings / sizeof kSettings[0])

struct RolltuiSettings {
  RolltuiPresetStore* store[STORE_COUNT]; /* BORROWED, must outlive this handle */
};

static const SettingRow* row_of(const char* key, size_t len) {
  size_t i;
  for (i = 0; i < ROLLTUI_SETTINGS_COUNT; ++i)
    if (kSettings[i].row.key_len == len && (len == 0 || memcmp(kSettings[i].row.key, key, len) == 0))
      return &kSettings[i];
  return NULL;
}

static RolltuiPresetStore* store_of(const RolltuiSettings* s, const SettingRow* r) {
  return (s && r) ? s->store[r->store_ix] : NULL;
}

size_t rolltui_settings_count(void) { return ROLLTUI_SETTINGS_COUNT; }

const RolltuiSetting* rolltui_settings_at(size_t i) {
  return i < ROLLTUI_SETTINGS_COUNT ? &kSettings[i].row : NULL;
}

const RolltuiSetting* rolltui_settings_find(const char* key, size_t key_len) {
  const SettingRow* r = row_of(key, key_len);
  return r ? &r->row : NULL;
}

RolltuiSettings* rolltui_settings_new(RolltuiPresetStore* theme, RolltuiPresetStore* layout,
                                      RolltuiPresetStore* bindings) {
  /* OWNED, LONG-LIVED: one handle a host keeps for the session, released by `_free`. */
  RolltuiSettings* s = (RolltuiSettings*)rolltui_mem_alloc(sizeof *s);
  s->store[STORE_THEME] = theme;
  s->store[STORE_LAYOUT] = layout;
  s->store[STORE_BINDINGS] = bindings;
  return s;
}

void rolltui_settings_free(RolltuiSettings* s) {
  if (s) rolltui_mem_free(s);
}

void rolltui_settings_get(const RolltuiSettings* s, const char* key, size_t key_len, RolltuiStr* out) {
  const SettingRow* r = row_of(key, key_len);
  if (!out) return;
  rolltui_str_clear(out);
  rolltui_preset_working_value(store_of(s, r), key, key_len, out);
}

void rolltui_settings_label(const RolltuiSettings* s, const char* key, size_t key_len, RolltuiStr* out) {
  const SettingRow* r = row_of(key, key_len);
  RolltuiPresetStore* st = store_of(s, r);
  if (!out) return;
  rolltui_str_clear(out);
  if (st) rolltui_preset_store_label(st, out);
}

/* ---- the rungs ---------------------------------------------------------------------------- */
/* THREE, and a flag is not one of them. A flag that sets a setting is a second configuration
 * system with neither discoverability nor persistence, competing with the one that has both. */
typedef struct {
  const char* s;
  size_t len;
} NameLen;

static const NameLen kRungNames[] = {
    {"environment", 11},
    {"working copy", 12},
    {"built-in default", 16},
};

const char* rolltui_setting_rung_name(RolltuiSettingRung r, size_t* len) {
  const size_t i = (size_t)r;
  if (i >= sizeof kRungNames / sizeof kRungNames[0]) {
    *len = 0;
    return "";
  }
  *len = kRungNames[i].len;
  return kRungNames[i].s;
}

void rolltui_settings_resolve(const RolltuiSettings* s, const char* key, size_t key_len, const char* env,
                             size_t env_len, RolltuiStr* out_value, RolltuiSettingRung* out_rung) {
  const SettingRow* r = row_of(key, key_len);
  RolltuiSettingRung sink;
  if (!out_rung) out_rung = &sink;
  if (!out_value) return;
  rolltui_str_clear(out_value);
  if (!r) {
    *out_rung = ROLLTUI_SETTING_RUNG_BUILTIN;
    return;
  }
  if (env_len) {
    rolltui_str_append(out_value, env, env_len);
    *out_rung = ROLLTUI_SETTING_RUNG_ENV;
    return;
  }
  rolltui_preset_working_value(store_of(s, r), key, key_len, out_value);
  if (out_value->n) {
    *out_rung = ROLLTUI_SETTING_RUNG_WORKING;
    return;
  }
  rolltui_str_append(out_value, r->row.builtin, r->row.builtin_len);
  *out_rung = ROLLTUI_SETTING_RUNG_BUILTIN;
}

/* ---- applying a change --------------------------------------------------------------------- */

void rolltui_settings_report_release(RolltuiSettingsReport* r) {
  if (!r) return;
  rolltui_str_free(&r->error);
  rolltui_str_free(&r->problems);
  rolltui_str_list_release(&r->notes);
}

static void take_notes(RolltuiSettingsReport* out, const RolltuiStr* notes, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) rolltui_str_list_add(&out->notes, notes[i].p ? notes[i].p : "", notes[i].n);
}

/* One per domain, because a report's TYPE is what the store's `void*` actually is. The three
 * bodies are the same four steps over three unrelated structs. */
static int load_theme(RolltuiPresetStore* st, const char* v, size_t vn, int persist, RolltuiSettingsReport* out) {
  RolltuiThemePresetReport rep;
  int ok;
  memset(&rep, 0, sizeof rep);
  ok = rolltui_preset_store_load(st, v, vn, &rep, persist);
  if (!ok) rolltui_str_set(&out->error, rep.error.p, rep.error.n);
  else if (!rolltui_theme_preset_report_clean(&rep)) rolltui_theme_preset_report_summary(&rep, &out->problems);
  take_notes(out, rep.notes, rep.notes_n);
  rolltui_theme_preset_report_release(&rep);
  return ok;
}

static int load_layout(RolltuiPresetStore* st, const char* v, size_t vn, int persist, RolltuiSettingsReport* out) {
  RolltuiLayoutPresetReport rep;
  int ok;
  memset(&rep, 0, sizeof rep);
  ok = rolltui_preset_store_load(st, v, vn, &rep, persist);
  if (!ok) rolltui_str_set(&out->error, rep.error.p, rep.error.n);
  else if (!rolltui_layout_preset_report_clean(&rep)) rolltui_layout_preset_report_summary(&rep, &out->problems);
  take_notes(out, rep.notes, rep.notes_n);
  rolltui_layout_preset_report_release(&rep);
  return ok;
}

static int load_bindings(RolltuiPresetStore* st, const char* v, size_t vn, int persist, RolltuiSettingsReport* out) {
  RolltuiBindingsPresetReport rep;
  int ok;
  memset(&rep, 0, sizeof rep);
  ok = rolltui_preset_store_load(st, v, vn, &rep, persist);
  if (!ok) rolltui_str_set(&out->error, rep.error.p, rep.error.n);
  else if (!rolltui_bindings_preset_report_clean(&rep)) rolltui_bindings_preset_report_summary(&rep, &out->problems);
  take_notes(out, rep.notes, rep.notes_n);
  rolltui_bindings_preset_report_release(&rep);
  return ok;
}

int rolltui_settings_set(RolltuiSettings* s, const char* key, size_t key_len, const char* value, size_t value_len,
                         int persist, RolltuiSettingsReport* report) {
  const SettingRow* r = row_of(key, key_len);
  RolltuiPresetStore* st = store_of(s, r);
  RolltuiSettingsReport sink;
  memset(&sink, 0, sizeof sink);
  if (!report) report = &sink;
  rolltui_str_clear(&report->error);
  rolltui_str_clear(&report->problems);
  rolltui_str_list_clear(&report->notes);
  if (!r) {
    rolltui_str_append(&report->error, "'", 1);
    rolltui_str_append(&report->error, key ? key : "", key_len);
    rolltui_str_append(&report->error, "' is not a setting", 18);
    rolltui_settings_report_release(&sink);
    return 0;
  }
  if (!st) {
    rolltui_str_append(&report->error, "no ", 3);
    rolltui_str_append(&report->error, r->row.store, r->row.store_len);
    rolltui_str_append(&report->error, " working copy", 13);
    rolltui_settings_report_release(&sink);
    return 0;
  }
  if (r->row.names_preset) {
    int ok = 0;
    switch (r->store_ix) {
      case STORE_THEME: ok = load_theme(st, value, value_len, persist, report); break;
      case STORE_LAYOUT: ok = load_layout(st, value, value_len, persist, report); break;
      default: ok = load_bindings(st, value, value_len, persist, report); break;
    }
    rolltui_settings_report_release(&sink);
    return ok;
  }
  /* A FIELD, not a preset: the value is checked against the row's own help text, so the
   * sentence a bad value is refused with and the list a listing prints are one string. */
  if (r->valid && !r->valid(value ? value : "", value_len)) {
    rolltui_str_append(&report->error, r->row.key, r->row.key_len);
    rolltui_str_append(&report->error, " must be ", 9);
    rolltui_str_append(&report->error, r->row.values, r->row.values_len);
    rolltui_str_append(&report->error, ", not '", 7);
    rolltui_str_append(&report->error, value ? value : "", value_len);
    rolltui_str_append(&report->error, "'", 1);
    rolltui_settings_report_release(&sink);
    return 0;
  }
  {
    RolltuiStr v;
    memset(&v, 0, sizeof v);
    rolltui_str_append(&v, value ? value : "", value_len);
    rolltui_preset_store_edit(st, r->write, &v, persist);
    rolltui_str_free(&v);
  }
  rolltui_settings_report_release(&sink);
  return 1;
}
