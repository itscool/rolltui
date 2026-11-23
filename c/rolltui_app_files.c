/* rolltui_app_files.c — where an app's OWN default files live.
 *
 * This answers "where is this app's default screen", which is a different question from "may a
 * user change it". The preset store answers the second through its own rungs; this one is asked
 * first, and what it returns is what a user's preset directory then shadows.
 *
 * An app built the strict way keeps its screen in files rather than in its source, so it cannot
 * start until it knows where those files are. Building the path in each host is what produced
 * `rolltui-explorer: no layout ()` — an empty directory string, a path of `/layouts/x.json`, and
 * a message naming neither what was wanted nor where it was sought.
 *
 * THREE RUNGS, LATER OVERRIDING EARLIER. Not first-found-wins: under that reading an app with an
 * embedded default would never look at a file beside it, which makes the file useless for exactly
 * the apps that ship a default. Overriding makes the embedded copy a FLOOR that guarantees the app
 * runs, and a file on disk a customisation.
 *   1. embedded  — compiled in from the app's own files by cmake/embed_presets.cmake
 *   2. beside the binary — <dir of argv0>/<app>.<kind>.json
 *   3. a known folder    — <config>/rolltui/<app>/<kind>.json
 *
 * `tried` collects every path examined whether it hit or missed, so a host that finds nothing can
 * say what it looked for. A miss that cannot name its candidates is the failure this file exists
 * to remove.
 */
#include "rolltui/rolltui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_map.h"

/* PATHS ARE RolltuiStr: no length cuts one short, because a cut path names another file. */
static const char* str_c(const RolltuiStr* s) { return s->p ? s->p : ""; }

static int is_sep(char c) {
#if defined(_WIN32)
  if (c == '\\') return 1;
#endif
  return c == '/';
}

/* Appends `part` as the next component of `p`. */
static void path_add(RolltuiStr* p, const char* part) {
  if (p->n && !is_sep(p->p[p->n - 1])) rolltui_str_append(p, "/", 1);
  rolltui_str_append(p, part, strlen(part));
}

static int read_whole(const char* path, RolltuiStr* out) {
  FILE* f = rolltui_os_fopen(path, "rb");
  long n;
  char* buf;
  if (!f) return 0;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
  n = ftell(f);
  if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
  buf = (char*)rolltui_mem_alloc((size_t)n + 1);
  if (!buf) { fclose(f); return 0; }
  if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { rolltui_mem_free(buf); fclose(f); return 0; }
  fclose(f);
  buf[n] = '\0';
  rolltui_str_set(out, buf, (size_t)n);
  rolltui_mem_free(buf);
  return 1;
}

static void note_tried(RolltuiStr* tried, const char* path, int hit) {
  if (!tried) return;
  if (tried->n) rolltui_str_append(tried, "\n", 1);
  rolltui_str_append(tried, hit ? "  found   " : "  missing ", 10);
  rolltui_str_append(tried, path, strlen(path));
}

static void config_root(RolltuiStr* out) {
  const char* d = getenv("ROLL_CONFIG_DIR");
  const char* x = getenv("XDG_CONFIG_HOME");
  char home[4096];
  if (d && *d) {
    rolltui_str_set(out, d, strlen(d));
    return;
  }
  if (x && *x) rolltui_str_set(out, x, strlen(x));
  else if (rolltui_os_config_home(home, sizeof home) == 0) rolltui_str_set(out, home, strlen(home));
  else rolltui_str_set(out, ".", 1);
  path_add(out, "roll");
}

static int try_path(const RolltuiStr* path, RolltuiStr* out, RolltuiStr* tried) {
  const int hit = read_whole(str_c(path), out);
  note_tried(tried, str_c(path), hit);
  return hit;
}

int rolltui_app_file(const char* argv0, const char* app, const char* kind,
                     const RolltuiEmbeddedFile* embedded, size_t embedded_n,
                     RolltuiStr* out, RolltuiStr* tried) {
  RolltuiStr path, leaf;
  int found = 0;
  size_t i;
  if (!app || !kind || !out) return 0;
  if (tried) tried->n = 0;
  memset(&path, 0, sizeof path);
  memset(&leaf, 0, sizeof leaf);

  /* Rung 1: the app's own compiled-in copy. Named by KIND, because that is the stem the
   * generator writes and the name a host thinks in ("layout", "bindings"). */
  for (i = 0; i < embedded_n; ++i) {
    if (embedded[i].name && strcmp(embedded[i].name, kind) == 0) {
      rolltui_str_set(out, embedded[i].text, strlen(embedded[i].text));
      note_tried(tried, "(embedded)", 1);
      found = 1;
      break;
    }
  }
  if (!found) note_tried(tried, "(embedded)", 0);

  /* Rung 2: beside the binary. No directory convention to learn — the file is named after the
   * program and sits next to it. argv0 with no directory in it is looked for in the working one. */
  if (argv0 && *argv0) {
    size_t at = strlen(argv0);
    while (at > 0 && !is_sep(argv0[at - 1])) --at;
    if (at == 0) rolltui_str_set(&path, ".", 1);
    else rolltui_str_set(&path, argv0, at > 1 ? at - 1 : 1); /* "/x" is in "/", not "" */
    rolltui_str_set(&leaf, app, strlen(app));
    rolltui_str_append(&leaf, ".", 1);
    rolltui_str_append(&leaf, kind, strlen(kind));
    rolltui_str_append(&leaf, ".json", 5);
    path_add(&path, str_c(&leaf));
    found |= try_path(&path, out, tried);
  }

  /* Rung 3: the known folder. Last, so it overrides both. */
  config_root(&path);
  path_add(&path, "rolltui");
  path_add(&path, app);
  path_add(&path, kind);
  rolltui_str_append(&path, ".json", 5);
  found |= try_path(&path, out, tried);

  rolltui_str_free(&path);
  rolltui_str_free(&leaf);
  return found;
}

/* ---- what an app keeps between runs -------------------------------------------------------------- */

int rolltui_state_dir(const char* app, size_t len, RolltuiStr* out) {
  const char* d = getenv("ROLL_STATE_DIR");
  const char* c = getenv("ROLL_CONFIG_DIR");
  const char* x = getenv("XDG_STATE_HOME");
  char home[4096];
  if (!out) return 0;
  if (d && *d) {
    rolltui_str_set(out, d, strlen(d));
  } else if (c && *c) { /* a scratch home keeps what is remembered beside what is configured */
    rolltui_str_set(out, c, strlen(c));
    path_add(out, "state");
  } else {
    if (x && *x) rolltui_str_set(out, x, strlen(x));
    else if (rolltui_os_state_home(home, sizeof home) == 0) rolltui_str_set(out, home, strlen(home));
    else {
      rolltui_str_clear(out);
      return 0;
    }
    path_add(out, "roll");
  }
  if (app && len) {
    RolltuiStr leaf;
    memset(&leaf, 0, sizeof leaf);
    rolltui_str_set(&leaf, app, len);
    path_add(out, str_c(&leaf));
    rolltui_str_free(&leaf);
  }
  return 1;
}

/* A NAME'S VALUE, or the mark that this handle removed it */
typedef struct StateValue {
  RolltuiStr v;
  unsigned char removed;
} StateValue;

struct RolltuiStateFile {
  RolltuiStr path;
  RolltuiMap values;  /* name -> StateValue*, OWNED: what the file held at the last read, with this handle's changes */
  RolltuiMap changed; /* name -> StateValue*, OWNED: what this handle set or removed since */
};

static void state_map_free(RolltuiMap* m) {
  size_t i;
  for (i = 0; i < rolltui_map_count(m); ++i) {
    StateValue* v = (StateValue*)rolltui_map_value_at(m, i);
    rolltui_str_free(&v->v);
    rolltui_mem_free(v);
  }
  rolltui_map_release(m);
}
static void state_put(RolltuiMap* m, const char* k, size_t kl, const char* v, size_t vl, int removed) {
  StateValue* sv = (StateValue*)rolltui_mem_alloc(sizeof *sv);
  StateValue* was;
  memset(sv, 0, sizeof *sv);
  if (v) rolltui_str_set(&sv->v, v, vl);
  sv->removed = (unsigned char)removed;
  was = (StateValue*)rolltui_map_put(m, k, kl, sv);
  if (was) {
    rolltui_str_free(&was->v);
    rolltui_mem_free(was);
  }
}
static void state_take(RolltuiMap* m, const char* k, size_t kl) {
  StateValue* was = (StateValue*)rolltui_map_take(m, k, kl);
  if (was) {
    rolltui_str_free(&was->v);
    rolltui_mem_free(was);
  }
}
/* The file's names into `into`; 1 when it was read or is not there yet, 0 when it is not a state file. */
static int state_read(const char* path, RolltuiMap* into, RolltuiStr* err) {
  RolltuiStr text, perr;
  RolltuiJsonValue* doc;
  size_t i;
  int ok = 1;
  memset(&text, 0, sizeof text);
  memset(&perr, 0, sizeof perr);
  if (!read_whole(path, &text)) return 1; /* not there yet: empty */
  doc = rolltui_json_parse(text.p ? text.p : "", text.n, &perr);
  if (!doc || !rolltui_json_is_object(doc)) {
    if (err) {
      rolltui_str_set(err, path, strlen(path));
      rolltui_str_append(err, ": not a state file", sizeof ": not a state file" - 1);
      if (perr.n) rolltui_str_append(err, ": ", 2), rolltui_str_append_str(err, &perr);
    }
    ok = 0;
  } else {
    for (i = 0; i < rolltui_json_object_size(doc); ++i) {
      size_t kl = 0, vl = 0;
      const char* k = rolltui_json_object_key_at(doc, i, &kl);
      const char* v = rolltui_json_as_string(rolltui_json_object_value_at(doc, i), NULL, 0, &vl);
      if (k && v) state_put(into, k, kl, v, vl, 0); /* a value that is not text is not one of ours, and is left out */
    }
  }
  rolltui_json_free(doc);
  rolltui_str_free(&text);
  rolltui_str_free(&perr);
  return ok;
}

RolltuiStateFile* rolltui_state_file_open(const char* path, size_t len, RolltuiStr* err) {
  RolltuiStateFile* s = (RolltuiStateFile*)rolltui_mem_alloc(sizeof *s);
  if (!s) return NULL;
  memset(s, 0, sizeof *s);
  if (err) rolltui_str_clear(err);
  rolltui_str_set(&s->path, path ? path : "", path ? len : 0);
  state_read(str_c(&s->path), &s->values, err);
  return s;
}

const char* rolltui_state_file_get(const RolltuiStateFile* s, const char* name, size_t len, size_t* value_len) {
  const StateValue* v = s ? (const StateValue*)rolltui_map_get(&s->values, name, len) : NULL;
  if (value_len) *value_len = v ? v->v.n : 0;
  return v ? (v->v.p ? v->v.p : "") : NULL;
}

void rolltui_state_file_set(RolltuiStateFile* s, const char* name, size_t len, const char* value, size_t value_len) {
  if (!s || !name) return;
  if (value) state_put(&s->values, name, len, value, value_len, 0);
  else state_take(&s->values, name, len);
  state_put(&s->changed, name, len, value, value_len, value == NULL);
}

size_t rolltui_state_file_count(const RolltuiStateFile* s) { return s ? rolltui_map_count(&s->values) : 0; }
const char* rolltui_state_file_name_at(const RolltuiStateFile* s, size_t i, size_t* len) {
  if (!s || i >= rolltui_map_count(&s->values)) {
    if (len) *len = 0;
    return NULL;
  }
  return rolltui_map_key_at(&s->values, i, len);
}

int rolltui_state_file_save(RolltuiStateFile* s, RolltuiStr* err) {
  RolltuiMap merged;
  RolltuiJsonValue* doc;
  RolltuiStr out;
  size_t i;
  int ok;
  if (!s) return 0;
  if (err) rolltui_str_clear(err);
  memset(&merged, 0, sizeof merged);
  if (!state_read(str_c(&s->path), &merged, err)) { /* what is there now is not ours to overwrite */
    state_map_free(&merged);
    return 0;
  }
  for (i = 0; i < rolltui_map_count(&s->changed); ++i) { /* this handle's changes over what is there now */
    size_t kl = 0;
    const char* k = rolltui_map_key_at(&s->changed, i, &kl);
    const StateValue* v = (const StateValue*)rolltui_map_value_at(&s->changed, i);
    if (v->removed) state_take(&merged, k, kl);
    else state_put(&merged, k, kl, v->v.p ? v->v.p : "", v->v.n, 0);
  }
  doc = rolltui_json_object();
  for (i = 0; i < rolltui_map_count(&merged); ++i) {
    size_t kl = 0;
    const char* k = rolltui_map_key_at(&merged, i, &kl);
    const StateValue* v = (const StateValue*)rolltui_map_value_at(&merged, i);
    rolltui_json_set(doc, k, kl, rolltui_json_string(v->v.p ? v->v.p : "", v->v.n));
  }
  memset(&out, 0, sizeof out);
  rolltui_json_dump(doc, 1, &out);
  rolltui_str_append(&out, "\n", 1);
  ok = rolltui_preset_write_file_atomic(s->path.p ? s->path.p : "", s->path.n, out.p ? out.p : "", out.n, err);
  if (ok) { /* the handle holds what the file holds now, another instance's names too */
    state_map_free(&s->values);
    s->values = merged;
    state_map_free(&s->changed);
    memset(&s->changed, 0, sizeof s->changed);
  } else {
    state_map_free(&merged);
  }
  rolltui_json_free(doc);
  rolltui_str_free(&out);
  return ok;
}

void rolltui_state_file_free(RolltuiStateFile* s) {
  if (!s) return;
  state_map_free(&s->values);
  state_map_free(&s->changed);
  rolltui_str_free(&s->path);
  rolltui_mem_free(s);
}
