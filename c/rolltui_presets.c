/* rolltui/c/rolltui_presets.c — the C side of the preset mechanics. See rolltui_presets.h
 * for the boundary's rules and rolltui/Presets.hpp for the five rules themselves;
 * `PresetsCpp.cpp` is the other implementation of the same functions, and
 * `rolltui/tests/presets_test.cpp` — which asserts all five over ALL THREE domains — is the
 * oracle for both.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`. The one process-wide
 * retainer is a domain's parsed shipped cache, which lives in the caller's own descriptor
 * and is handed back by `rolltui_preset_domain_release`. */
#include "rolltui/c/rolltui_presets.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/c/rolltui_alloc.h"

/* ---- a growing byte buffer, the one shape everything here builds a string in ------------- */
/* GROWING, AMORTISED (rolltui_alloc.h strategy 2). Not NUL-terminated by construction —
 * `cstr()` adds one where a POSIX call needs it, which is the only place a terminator is a
 * fact rather than an assumption. */
typedef struct {
  char* p;
  size_t len, cap;
} Buf;

static void buf_add(Buf* b, const char* s, size_t n) {
  if (n == 0) return;
  b->p = (char*)rolltui_grow(b->p, &b->cap, b->len + n + 1, sizeof *b->p);
  memcpy(b->p + b->len, s, n);
  b->len += n;
}

static void buf_put(void* ctx, const char* s, size_t n) { buf_add((Buf*)ctx, s, n); }

static void buf_free(Buf* b) {
  rolltui_mem_free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
}

static void buf_set(Buf* b, const char* s, size_t n) {
  b->len = 0;
  buf_add(b, s, n);
}

/* A NUL-terminated view of the buffer, for the POSIX calls that need one. The terminator
 * lives in the slack `buf_add` always reserves, so it costs no second allocation. */
static const char* cstr(Buf* b) {
  if (!b->p) return "";
  b->p[b->len] = '\0';
  return b->p;
}

static int buf_eq(const Buf* b, const char* s, size_t n) {
  return b->len == n && (n == 0 || memcmp(b->p, s, n) == 0);
}

/* ---- files ------------------------------------------------------------------------------- */

int rolltui_preset_read_file(const char* path, size_t path_len, RolltuiPutFn put, void* ctx) {
  Buf p = {NULL, 0, 0};
  FILE* f;
  char chunk[4096];
  size_t n;
  buf_add(&p, path, path_len);
  f = fopen(cstr(&p), "rb");
  buf_free(&p);
  if (!f) return 0;
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) put(ctx, chunk, n);
  fclose(f);
  return 1;
}

/* `mkdir -p` over the path's parent. Every failure but "it is already there" is reported. */
static int make_parents(const char* path, size_t len, RolltuiPutFn err, void* err_ctx) {
  Buf dir = {NULL, 0, 0};
  size_t i, cut = 0;
  int ok = 1;
  for (i = 0; i < len; ++i)
    if (path[i] == '/') cut = i;
  if (cut == 0) return 1;
  buf_add(&dir, path, cut);
  for (i = 1; i <= dir.len && ok; ++i) {
    if (i != dir.len && dir.p[i] != '/') continue;
    {
      const char saved = dir.p[i];
      dir.p[i] = '\0';
      if (mkdir(dir.p, 0777) != 0 && errno != EEXIST) {
        err(err_ctx, "cannot create ", 14);
        err(err_ctx, dir.p, strlen(dir.p));
        err(err_ctx, ": ", 2);
        err(err_ctx, strerror(errno), strlen(strerror(errno)));
        ok = 0;
      }
      if (i != dir.len) dir.p[i] = saved;
    }
  }
  buf_free(&dir);
  return ok;
}

int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiPutFn err, void* err_ctx) {
  Buf p = {NULL, 0, 0}, tmp = {NULL, 0, 0};
  char pid[32];
  FILE* f;
  int ok = 1;
  if (!make_parents(path, path_len, err, err_ctx)) return 0;
  buf_add(&p, path, path_len);
  buf_add(&tmp, path, path_len);
  snprintf(pid, sizeof pid, ".tmp.%d", (int)getpid());
  buf_add(&tmp, pid, strlen(pid));
  f = fopen(cstr(&tmp), "wb");
  if (!f) {
    err(err_ctx, "cannot write ", 13);
    err(err_ctx, tmp.p, tmp.len);
    err(err_ctx, ": ", 2);
    err(err_ctx, strerror(errno), strlen(strerror(errno)));
    ok = 0;
  } else {
    if (len && fwrite(bytes, 1, len, f) != len) {
      err(err_ctx, "short write to ", 15);
      err(err_ctx, tmp.p, tmp.len);
      ok = 0;
    }
    if (fclose(f) != 0 && ok) {
      err(err_ctx, "short write to ", 15);
      err(err_ctx, tmp.p, tmp.len);
      ok = 0;
    }
    if (ok && rename(cstr(&tmp), cstr(&p)) != 0) {
      err(err_ctx, "cannot rename ", 14);
      err(err_ctx, tmp.p, tmp.len);
      err(err_ctx, " to ", 4);
      err(err_ctx, p.p, p.len);
      err(err_ctx, ": ", 2);
      err(err_ctx, strerror(errno), strlen(strerror(errno)));
      remove(cstr(&tmp));
      ok = 0;
    }
  }
  buf_free(&p);
  buf_free(&tmp);
  return ok;
}

/* A tiny sorted list of names, which is what `json_names_in` builds before it hands them
 * over. GROWING, AMORTISED for the array; each name is its own small buffer. */
typedef struct {
  Buf* names;
  size_t count, cap;
} NameList;

static int name_cmp(const void* a, const void* b) {
  const Buf* x = (const Buf*)a;
  const Buf* y = (const Buf*)b;
  const size_t n = x->len < y->len ? x->len : y->len;
  const int c = n ? memcmp(x->p, y->p, n) : 0;
  if (c) return c;
  return x->len < y->len ? -1 : (x->len > y->len ? 1 : 0);
}

static void name_list_free(NameList* l) {
  size_t i;
  for (i = 0; i < l->count; ++i) buf_free(&l->names[i]);
  rolltui_mem_free(l->names);
  l->names = NULL;
  l->count = l->cap = 0;
}

/* The stems of every "*.json" in `dir`, sorted. The caller owns the list and frees it —
 * `list()` needs each name twice (to test against the shipped table and to build a path),
 * so handing them over one at a time would mean keeping them anyway. */
static void collect_json_names(const char* dir, size_t dir_len, NameList* out) {
  Buf path = {NULL, 0, 0};
  DIR* d;
  struct dirent* e;
  buf_add(&path, dir, dir_len);
  d = opendir(cstr(&path));
  buf_free(&path);
  if (!d) return;
  while ((e = readdir(d)) != NULL) {
    const size_t n = strlen(e->d_name);
    if (n <= 5 || memcmp(e->d_name + n - 5, ".json", 5) != 0) continue;
    if (e->d_name[0] == '.') continue;
    out->names = (Buf*)rolltui_grow_zeroed(out->names, &out->cap, out->count + 1, sizeof *out->names);
    memset(&out->names[out->count], 0, sizeof(Buf));
    buf_set(&out->names[out->count], e->d_name, n - 5); /* the stem */
    ++out->count;
  }
  closedir(d);
  if (out->count > 1) qsort(out->names, out->count, sizeof *out->names, name_cmp);
}

void rolltui_preset_json_names_in(const char* dir, size_t dir_len, RolltuiPutFn put, void* ctx) {
  NameList list = {NULL, 0, 0};
  size_t i;
  collect_json_names(dir, dir_len, &list);
  for (i = 0; i < list.count; ++i) put(ctx, list.names[i].p, list.names[i].len);
  name_list_free(&list);
}

int rolltui_preset_looks_like_path(const char* s, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i)
    if (s[i] == '/') return 1;
  return len > 5 && memcmp(s + len - 5, ".json", 5) == 0;
}

int rolltui_preset_valid_name(const char* name, size_t len) {
  size_t i;
  if (len == 0 || len > 64 || name[0] == '.') return 0;
  for (i = 0; i < len; ++i) {
    const unsigned char c = (unsigned char)name[i];
    if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) return 0;
  }
  return 1;
}

/* ---- the shipped cache -------------------------------------------------------------------- */

struct RolltuiPresetShippedCache {
  Buf* names;
  void** values;
  size_t count;
  RolltuiPresetDomain* owner; /* whose `destroy` frees the values */
};

static void build_cache(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns, void* scratch_report) {
  const size_t n = d->shipped_count();
  size_t i;
  int have_default = 0;
  RolltuiPresetShippedCache* c =
      (RolltuiPresetShippedCache*)rolltui_mem_alloc(sizeof(RolltuiPresetShippedCache));
  memset(c, 0, sizeof *c);
  c->owner = d;
  c->count = n;
  if (n) {
    c->names = (Buf*)rolltui_mem_alloc(n * sizeof *c->names);
    c->values = (void**)rolltui_mem_alloc(n * sizeof *c->values);
    memset(c->names, 0, n * sizeof *c->names);
    memset(c->values, 0, n * sizeof *c->values);
  }
  for (i = 0; i < n; ++i) {
    const char* name = NULL;
    const char* text = NULL;
    size_t nlen = 0, tlen = 0;
    void* v;
    d->shipped_at(i, &name, &nlen, &text, &tlen);
    rep_fns->reset(scratch_report);
    v = d->parse(text, tlen, scratch_report);
    if (!v) {
      /* A shipped preset that does not load cleanly is a programming error (the layout
       * loader's standard); say so and stop rather than run half of one. */
      Buf why = {NULL, 0, 0};
      rep_fns->get_error(scratch_report, buf_put, &why);
      fprintf(stderr, "rolltui: shipped %.*s preset '%.*s' is broken: %.*s\n", (int)d->kind_len, d->kind, (int)nlen,
              name, (int)why.len, why.p ? why.p : "");
      abort();
    }
    buf_set(&c->names[i], name, nlen);
    c->values[i] = v;
    if (nlen == 7 && memcmp(name, "default", 7) == 0) have_default = 1;
  }
  if (!have_default) {
    fprintf(stderr, "rolltui: no shipped %.*s preset named 'default' (rule 5)\n", (int)d->kind_len, d->kind);
    abort();
  }
  d->cache = c;
}

static void ensure_cache(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns, void* scratch_report) {
  if (!d->cache) build_cache(d, rep_fns, scratch_report);
}

const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns,
                                   void* scratch_report, const char* name, size_t len) {
  size_t i;
  ensure_cache(d, rep_fns, scratch_report);
  for (i = 0; i < d->cache->count; ++i)
    if (buf_eq(&d->cache->names[i], name, len)) return d->cache->values[i];
  return NULL;
}

int rolltui_preset_is_shipped(RolltuiPresetDomain* d, const char* name, size_t len) {
  size_t i;
  const size_t n = d->shipped_count();
  /* Asked WITHOUT parsing anything: the names are in the embedded table, and a store may
   * want to know before it has a report to hand a parse. */
  for (i = 0; i < n; ++i) {
    const char* nm = NULL;
    const char* text = NULL;
    size_t nlen = 0, tlen = 0;
    d->shipped_at(i, &nm, &nlen, &text, &tlen);
    if (nlen == len && (len == 0 || memcmp(nm, name, len) == 0)) return 1;
  }
  return 0;
}

void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiPutFn put, void* ctx) {
  const size_t n = d->shipped_count();
  size_t i, pass;
  for (pass = 0; pass < 2; ++pass)
    for (i = 0; i < n; ++i) {
      const char* nm = NULL;
      const char* text = NULL;
      size_t nlen = 0, tlen = 0;
      const int is_default = (d->shipped_at(i, &nm, &nlen, &text, &tlen), nlen == 7 && memcmp(nm, "default", 7) == 0);
      if ((pass == 0) == (is_default != 0)) put(ctx, nm, nlen);
    }
}

void rolltui_preset_domain_release(RolltuiPresetDomain* d) {
  size_t i;
  if (!d || !d->cache) return;
  for (i = 0; i < d->cache->count; ++i) {
    d->destroy(d->cache->values[i]);
    buf_free(&d->cache->names[i]);
  }
  rolltui_mem_free(d->cache->names);
  rolltui_mem_free(d->cache->values);
  rolltui_mem_free(d->cache);
  d->cache = NULL;
}

/* ---- the store ------------------------------------------------------------------------------ */

struct RolltuiPresetStore {
  RolltuiPresetDomain* d;
  const RolltuiPresetReportFns* rep;
  Buf dir, shipped_dir;
  int may_write_shipped;

  pthread_mutex_t mu;
  void* working;
  void* origin_content;
  Buf origin;
  Buf last_error;
  unsigned long long version;
};

static void store_working_path(const RolltuiPresetStore* s, Buf* out) {
  out->len = 0;
  buf_add(out, s->dir.p, s->dir.len);
  buf_add(out, "/", 1);
  buf_add(out, s->d->working_file, s->d->working_file_len);
}

static void store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, Buf* out) {
  out->len = 0;
  buf_add(out, s->dir.p, s->dir.len);
  buf_add(out, "/", 1);
  buf_add(out, s->d->subdir, s->d->subdir_len);
  buf_add(out, "/", 1);
  buf_add(out, name, len);
  buf_add(out, ".json", 5);
}

void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiPutFn put, void* ctx) {
  Buf p = {NULL, 0, 0};
  store_working_path(s, &p);
  put(ctx, p.p, p.len);
  buf_free(&p);
}

void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiPutFn put,
                                      void* ctx) {
  Buf p = {NULL, 0, 0};
  store_preset_path(s, name, len, &p);
  put(ctx, p.p, p.len);
  buf_free(&p);
}

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns,
                                             const char* dir, size_t dir_len, int may_write_shipped,
                                             const char* shipped_dir, size_t shipped_dir_len, void* scratch_report) {
  RolltuiPresetStore* s = (RolltuiPresetStore*)rolltui_mem_alloc(sizeof(RolltuiPresetStore));
  const void* def;
  memset(s, 0, sizeof *s);
  s->d = d;
  s->rep = rep_fns;
  buf_set(&s->dir, dir, dir_len);
  buf_set(&s->shipped_dir, shipped_dir, shipped_dir_len);
  s->may_write_shipped = may_write_shipped;
  pthread_mutex_init(&s->mu, NULL);
  s->version = 1;
  def = rolltui_preset_shipped(d, rep_fns, scratch_report, "default", 7);
  s->working = d->clone(def);
  s->origin_content = d->clone(def);
  buf_set(&s->origin, "default", 7);
  return s;
}

void rolltui_preset_store_free(RolltuiPresetStore* s) {
  if (!s) return;
  if (s->working) s->d->destroy(s->working);
  if (s->origin_content) s->d->destroy(s->origin_content);
  buf_free(&s->dir);
  buf_free(&s->shipped_dir);
  buf_free(&s->origin);
  buf_free(&s->last_error);
  pthread_mutex_destroy(&s->mu);
  rolltui_mem_free(s);
}

static int autosave_locked(RolltuiPresetStore* s) {
  Buf path = {NULL, 0, 0}, bytes = {NULL, 0, 0}, err = {NULL, 0, 0};
  int ok;
  store_working_path(s, &path);
  s->d->to_json_with_origin(s->working, s->origin.p, s->origin.len, buf_put, &bytes);
  ok = rolltui_preset_write_file_atomic(path.p, path.len, bytes.p, bytes.len, buf_put, &err);
  if (ok) s->last_error.len = 0;
  else buf_set(&s->last_error, err.p, err.len);
  buf_free(&path);
  buf_free(&bytes);
  buf_free(&err);
  return ok;
}

static void touch_locked(RolltuiPresetStore* s, int persist) {
  ++s->version;
  if (persist) autosave_locked(s);
}

/* The one read that both `get` and `load` and `start` go through. `partial` may be NULL. */
static void* get_locked(const RolltuiPresetStore* s, const char* name, size_t len, void* report, int* partial) {
  RolltuiPresetStore* self = (RolltuiPresetStore*)s; /* the cache is the only mutation */
  Buf path = {NULL, 0, 0}, text = {NULL, 0, 0}, err = {NULL, 0, 0};
  void* v = NULL;
  const void* shipped;
  s->rep->reset(report);
  if (partial) *partial = 0;
  shipped = rolltui_preset_shipped(self->d, s->rep, report, name, len);
  if (shipped) {
    s->rep->reset(report);
    return s->d->clone(shipped);
  }
  if (rolltui_preset_looks_like_path(name, len)) buf_add(&path, name, len);
  else store_preset_path(s, name, len, &path);
  if (!rolltui_preset_read_file(path.p, path.len, buf_put, &text)) {
    Buf msg = {NULL, 0, 0};
    buf_add(&msg, "no ", 3);
    buf_add(&msg, s->d->kind, s->d->kind_len);
    buf_add(&msg, " preset '", 9);
    buf_add(&msg, name, len);
    buf_add(&msg, "' (not shipped, and ", 20);
    buf_add(&msg, path.p, path.len);
    buf_add(&msg, " is not readable)", 17);
    s->rep->set_error(report, msg.p, msg.len);
    buf_free(&msg);
    buf_free(&path);
    buf_free(&text);
    return NULL;
  }
  /* PARTIAL FIRST: a colours-only theme file fills part of the working copy and says what
   * it kept (Presets.hpp). Its notes get the path in front of them. */
  v = s->d->parse_partial(text.p, text.len, s->working, report);
  if (v) {
    Buf prefix = {NULL, 0, 0};
    if (partial) *partial = 1;
    buf_add(&prefix, path.p, path.len);
    buf_add(&prefix, " ", 1);
    s->rep->prefix_notes(report, prefix.p, prefix.len);
    buf_free(&prefix);
    buf_free(&path);
    buf_free(&text);
    buf_free(&err);
    return v;
  }
  s->rep->get_error(report, buf_put, &err);
  if (err.len) { /* the file was unusable AS a partial: the path, then whatever it said */
    Buf msg = {NULL, 0, 0};
    buf_add(&msg, path.p, path.len);
    buf_add(&msg, ": ", 2);
    buf_add(&msg, err.p, err.len);
    s->rep->set_error(report, msg.p, msg.len);
    buf_free(&msg);
    buf_free(&path);
    buf_free(&text);
    buf_free(&err);
    return NULL;
  }
  v = s->d->parse(text.p, text.len, report);
  if (!v) {
    Buf msg = {NULL, 0, 0};
    err.len = 0;
    s->rep->get_error(report, buf_put, &err);
    buf_add(&msg, path.p, path.len);
    buf_add(&msg, ": ", 2);
    buf_add(&msg, err.p, err.len);
    s->rep->set_error(report, msg.p, msg.len);
    buf_free(&msg);
  }
  buf_free(&path);
  buf_free(&text);
  buf_free(&err);
  return v;
}

void rolltui_preset_store_start(RolltuiPresetStore* s, void* report, void* scratch_report) {
  Buf path = {NULL, 0, 0}, text = {NULL, 0, 0}, msg = {NULL, 0, 0};
  void* parsed;
  pthread_mutex_lock(&s->mu);
  s->rep->reset(report);
  store_working_path(s, &path);
  if (!rolltui_preset_read_file(path.p, path.len, buf_put, &text)) {
    buf_add(&msg, "no working copy at ", 19);
    buf_add(&msg, path.p, path.len);
    buf_add(&msg, "; started from the shipped 'default'", 36);
    s->rep->add_note(report, msg.p, msg.len);
    buf_free(&path);
    buf_free(&text);
    buf_free(&msg);
    pthread_mutex_unlock(&s->mu);
    return;
  }
  parsed = s->d->parse(text.p, text.len, report);
  if (!parsed) {
    Buf why = {NULL, 0, 0};
    s->rep->get_error(report, buf_put, &why);
    buf_add(&msg, "working copy ", 13);
    buf_add(&msg, path.p, path.len);
    buf_add(&msg, ": ", 2);
    buf_add(&msg, why.len ? why.p : "unreadable", why.len ? why.len : 10);
    buf_add(&msg, "; started from the shipped 'default'", 36);
    s->rep->set_error(report, msg.p, msg.len);
    buf_free(&why);
    buf_free(&path);
    buf_free(&text);
    buf_free(&msg);
    pthread_mutex_unlock(&s->mu);
    return;
  }
  s->d->destroy(s->working);
  s->working = parsed;
  /* WHICH PRESET IT CAME FROM: the domain wrote it as "preset" and reads it back, because
   * the JSON never crosses this boundary (rolltui_presets.h). */
  s->origin.len = 0;
  s->d->origin_of(text.p, text.len, buf_put, &s->origin);
  if (s->origin.len == 0) buf_set(&s->origin, "default", 7);
  {
    void* oc = get_locked(s, s->origin.p, s->origin.len, scratch_report, NULL);
    s->d->destroy(s->origin_content);
    s->origin_content = oc ? oc : s->d->clone(s->working);
    if (!oc) {
      msg.len = 0;
      buf_add(&msg, "the working copy's preset '", 27);
      buf_add(&msg, s->origin.p, s->origin.len);
      buf_add(&msg, "' no longer exists", 18);
      s->rep->add_note(report, msg.p, msg.len);
    }
  }
  msg.len = 0;
  buf_add(&msg, "loaded the working copy (", 25);
  buf_add(&msg, s->origin.p, s->origin.len);
  if (!s->d->equal(s->working, s->origin_content)) buf_add(&msg, " (modified)", 11);
  buf_add(&msg, ")", 1);
  s->rep->add_note(report, msg.p, msg.len);
  ++s->version;
  buf_free(&path);
  buf_free(&text);
  buf_free(&msg);
  pthread_mutex_unlock(&s->mu);
}

void* rolltui_preset_store_working(const RolltuiPresetStore* s) {
  void* v;
  pthread_mutex_lock((pthread_mutex_t*)&s->mu);
  v = s->d->clone(s->working);
  pthread_mutex_unlock((pthread_mutex_t*)&s->mu);
  return v;
}

const char* rolltui_preset_store_origin(const RolltuiPresetStore* s, size_t* len) {
  *len = s->origin.len;
  return s->origin.p ? s->origin.p : "";
}

const char* rolltui_preset_store_last_error(const RolltuiPresetStore* s, size_t* len) {
  *len = s->last_error.len;
  return s->last_error.p ? s->last_error.p : "";
}

int rolltui_preset_store_modified(const RolltuiPresetStore* s) {
  int m;
  pthread_mutex_lock((pthread_mutex_t*)&s->mu);
  m = !s->d->equal(s->working, s->origin_content);
  pthread_mutex_unlock((pthread_mutex_t*)&s->mu);
  return m;
}

unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s) {
  unsigned long long v;
  pthread_mutex_lock((pthread_mutex_t*)&s->mu);
  v = s->version;
  pthread_mutex_unlock((pthread_mutex_t*)&s->mu);
  return v;
}

void rolltui_preset_store_set_working(RolltuiPresetStore* s, void* v, int persist) {
  pthread_mutex_lock(&s->mu);
  s->d->destroy(s->working);
  s->working = v;
  touch_locked(s, persist);
  pthread_mutex_unlock(&s->mu);
}

void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist) {
  pthread_mutex_lock(&s->mu);
  fn(s->working, ctx);
  touch_locked(s, persist);
  pthread_mutex_unlock(&s->mu);
}

void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetInfoFn put, void* ctx) {
  Buf dir = {NULL, 0, 0}, path = {NULL, 0, 0};
  NameList list = {NULL, 0, 0};
  const size_t n = s->d->shipped_count();
  size_t i, pass;
  /* The shipped ones first, "default" ahead of the rest — the order a chooser offers them
   * in, and the same order `shipped_names()` gave. */
  for (pass = 0; pass < 2; ++pass)
    for (i = 0; i < n; ++i) {
      const char* nm = NULL;
      const char* text = NULL;
      size_t nlen = 0, tlen = 0;
      int is_default;
      s->d->shipped_at(i, &nm, &nlen, &text, &tlen);
      is_default = nlen == 7 && memcmp(nm, "default", 7) == 0;
      if ((pass == 0) != (is_default != 0)) continue;
      put(ctx, nm, nlen, 1, "", 0);
    }
  buf_add(&dir, s->dir.p, s->dir.len);
  buf_add(&dir, "/", 1);
  buf_add(&dir, s->d->subdir, s->d->subdir_len);
  collect_json_names(dir.p, dir.len, &list);
  for (i = 0; i < list.count; ++i) {
    /* A user file that shadows a shipped name is never listed as a user preset: the
     * shipped one is what `get` will answer with, so offering both would be two entries
     * for one thing. */
    if (rolltui_preset_is_shipped(s->d, list.names[i].p, list.names[i].len)) continue;
    store_preset_path(s, list.names[i].p, list.names[i].len, &path);
    put(ctx, list.names[i].p, list.names[i].len, 0, path.p, path.len);
  }
  name_list_free(&list);
  buf_free(&dir);
  buf_free(&path);
}

void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, size_t len, void* report) {
  void* v;
  pthread_mutex_lock((pthread_mutex_t*)&s->mu);
  v = get_locked(s, name, len, report, NULL);
  pthread_mutex_unlock((pthread_mutex_t*)&s->mu);
  return v;
}

/* The stem of a path: "themes/x.json" → "x". Used when a LOAD by path becomes the origin. */
static void stem_of(const char* p, size_t len, Buf* out) {
  size_t begin = 0, end = len, i;
  for (i = 0; i < len; ++i)
    if (p[i] == '/') begin = i + 1;
  if (end > begin + 5 && memcmp(p + end - 5, ".json", 5) == 0) end -= 5;
  out->len = 0;
  buf_add(out, p + begin, end - begin);
}

int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name, size_t len, void* report, int persist) {
  int partial = 0;
  void* v;
  pthread_mutex_lock(&s->mu);
  v = get_locked(s, name, len, report, &partial);
  if (!v) {
    pthread_mutex_unlock(&s->mu);
    return 0;
  }
  s->d->destroy(s->working);
  s->working = v;
  if (!partial) {
    if (rolltui_preset_looks_like_path(name, len)) stem_of(name, len, &s->origin);
    else buf_set(&s->origin, name, len);
    s->d->destroy(s->origin_content);
    s->origin_content = s->d->clone(s->working);
  }
  touch_locked(s, persist);
  pthread_mutex_unlock(&s->mu);
  return 1;
}

int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiPutFn err,
                                 void* err_ctx) {
  Buf path = {NULL, 0, 0}, bytes = {NULL, 0, 0};
  int result = ROLLTUI_SAVE_SAVED;
  pthread_mutex_lock(&s->mu);
  if (!rolltui_preset_valid_name(name, len)) {
    pthread_mutex_unlock(&s->mu);
    return ROLLTUI_SAVE_BAD_NAME;
  }
  if (rolltui_preset_is_shipped(s->d, name, len)) {
    if (!s->may_write_shipped) {
      pthread_mutex_unlock(&s->mu);
      return ROLLTUI_SAVE_REFUSED_SHIPPED;
    }
    buf_add(&path, s->shipped_dir.p, s->shipped_dir.len);
    buf_add(&path, "/", 1);
    buf_add(&path, name, len);
    buf_add(&path, ".json", 5);
  } else {
    struct stat st;
    store_preset_path(s, name, len, &path);
    if (!overwrite && stat(cstr(&path), &st) == 0) {
      buf_free(&path);
      pthread_mutex_unlock(&s->mu);
      return ROLLTUI_SAVE_EXISTS_ASK;
    }
  }
  s->d->to_json(s->working, name, len, buf_put, &bytes);
  if (!rolltui_preset_write_file_atomic(path.p, path.len, bytes.p, bytes.len, err, err_ctx)) {
    result = ROLLTUI_SAVE_WRITE_FAILED;
  } else {
    buf_set(&s->origin, name, len);
    s->d->destroy(s->origin_content);
    s->origin_content = s->d->clone(s->working);
    touch_locked(s, 1);
  }
  buf_free(&path);
  buf_free(&bytes);
  pthread_mutex_unlock(&s->mu);
  return result;
}
