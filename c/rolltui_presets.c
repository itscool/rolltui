/* rolltui/c/rolltui_presets.c — the C side of the preset mechanics. See rolltui_presets.h
 * for the boundary's rules and rolltui/Presets.hpp for the five rules themselves;
 * `Presets.cpp` is the other implementation of the same functions, and
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
#include "rolltui/c/rolltui_context.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/rolltui.h"

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

static int write_file_atomic_put(const char* path, size_t path_len, const char* bytes, size_t len, RolltuiPutFn err,
                                 void* err_ctx) {
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

int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiStr* err) {
  if (err) rolltui_str_clear(err);
  return write_file_atomic_put(path, path_len, bytes, len, rolltui_str_put, err);
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

void rolltui_preset_json_names_in(const char* dir, size_t dir_len, RolltuiStrList* out) {
  NameList list = {NULL, 0, 0};
  size_t i;
  if (!out) return;
  rolltui_str_list_clear(out);
  collect_json_names(dir, dir_len, &list);
  for (i = 0; i < list.count; ++i) rolltui_str_list_add(out, list.names[i].p, list.names[i].len);
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

/* ---- the library's own three descriptors (`rolltui_preset_domain`, Phase 18 m3) -------------
 * A SESSION'S SINCE PHASE 25 m2, and this one HAD to move rather than merely could: the Layout
 * descriptor holds `rolltui_layout_shipped_default_actions()`'s pointer, and that cache is
 * per-context now, so a process-wide descriptor would outlive the storage it borrows the moment
 * one session was freed. That is contract point 4 stated as a defect instead of a rule.
 *
 * The three are owned together because they are configured together and released together; the
 * config the three `*_preset_domain_init` calls used to leave in file-scope statics is here for
 * the same reason. Pointer EQUALITY against the three, never a range compare: a host's own
 * descriptor is an unrelated object. */
struct RolltuiPresetDomains {
  RolltuiPresetDomain d[3];
  int built[3];
  /* Theme: the vocab and the two validators, set once by `rolltui_theme_preset_domain_init`. */
  const RolltuiThemeVocab* theme_vocab;
  RolltuiThemePresetValidFn theme_mode_valid;
  RolltuiThemePresetValidFn theme_depth_valid;
  /* Layout: the hooks, and the BORROWED default-action table that forced this move. */
  const RolltuiLayoutHooks* layout_hooks;
  const RolltuiLayoutAction* layout_default_actions;
  size_t layout_default_actions_n;
  /* Bindings: the two callbacks and their contexts. */
  RolltuiScopeFn bindings_is_library_scope;
  void* bindings_scope_ctx;
  RolltuiReasonFn bindings_reason;
  void* bindings_reason_ctx;
};

/* THE ONE THREAD-LOCAL-SHAPED THING LEFT, and it is not state: `build_cache` needs to know
 * whether the descriptor it is filling is a library one, and a `RolltuiPresetDomain` carries no
 * back-pointer. It is set only for the length of one `rolltui_preset_domain` call. */
static RolltuiPresetDomains* domains_of(RolltuiContext* c) {
  if (c->presets == NULL) {
    c->presets = (RolltuiPresetDomains*)rolltui_mem_alloc(sizeof *c->presets);
    memset(c->presets, 0, sizeof *c->presets);
  }
  return c->presets;
}

RolltuiPresetDomains* rolltui_preset_domains_new(void) {
  RolltuiPresetDomains* p = (RolltuiPresetDomains*)rolltui_mem_alloc(sizeof *p);
  memset(p, 0, sizeof *p);
  return p;
}

void rolltui_preset_domains_free(RolltuiPresetDomains* p) {
  size_t i;
  if (p == NULL) return;
  for (i = 0; i < 3; ++i)
    if (p->built[i]) rolltui_preset_domain_release(&p->d[i]);
  rolltui_mem_free(p);
}

/* ---- the shipped cache ------------------------------------------------------------------- */

struct RolltuiPresetShippedCache {
  Buf* names;
  void** values;
  size_t count;
  RolltuiPresetDomain* owner; /* whose `destroy` frees the values */
};

static void build_cache(RolltuiPresetDomain* d) {
  const size_t n = d->shipped_count();
  size_t i;
  int have_default = 0;
  /* OWNED, short-lived: the report every shipped preset is parsed into and nobody reads —
   * a shipped preset loads cleanly or the process aborts below. Made by the domain, which
   * is the one thing that can name its type; until Phase 18 m3 every caller supplied one. */
  void* scratch = d->report->create();
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
    d->report->reset(scratch);
    v = d->parse(d, text, tlen, scratch);
    if (!v) {
      /* A shipped preset that does not load cleanly is a programming error (the layout
       * loader's standard); say so and stop rather than run half of one. */
      Buf why = {NULL, 0, 0};
      d->report->get_error(scratch, buf_put, &why);
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
  d->report->destroy(scratch);
  d->cache = c;
  /* NO HOOK: a library descriptor belongs to the context that built it and
   * `rolltui_preset_domains_free` releases all three by name. A host's own descriptor is
   * still the host's to release, and always was. */
}

static void ensure_cache(RolltuiPresetDomain* d) {
  if (!d->cache) build_cache(d);
}

const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const char* name, size_t len) {
  size_t i;
  ensure_cache(d);
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

const char* rolltui_preset_shipped_text(RolltuiPresetDomain* d, const char* name, size_t len, size_t* out_len) {
  const size_t n = d->shipped_count();
  size_t i;
  if (out_len) *out_len = 0;
  for (i = 0; i < n; ++i) {
    const char *nm = NULL, *text = NULL;
    size_t nlen = 0, tlen = 0;
    d->shipped_at(i, &nm, &nlen, &text, &tlen);
    if (nlen != len || memcmp(nm, name, len) != 0) continue;
    if (out_len) *out_len = tlen;
    return text;
  }
  return NULL;
}

void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiStrList* out) {
  const size_t n = d->shipped_count();
  size_t i, pass;
  if (!out) return;
  rolltui_str_list_clear(out);
  for (pass = 0; pass < 2; ++pass)
    for (i = 0; i < n; ++i) {
      const char* nm = NULL;
      const char* text = NULL;
      size_t nlen = 0, tlen = 0;
      const int is_default = (d->shipped_at(i, &nm, &nlen, &text, &tlen), nlen == 7 && memcmp(nm, "default", 7) == 0);
      if ((pass == 0) == (is_default != 0)) rolltui_str_list_add(out, nm, nlen);
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

/* ---- the store --------------------------------------------------------------------------- */

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
  int modified; /* working != origin_content, computed at every change (touch_locked, start) */
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

void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiStr* out) {
  Buf p = {NULL, 0, 0};
  store_working_path(s, &p);
  rolltui_str_set(out, p.p ? p.p : "", p.len);
  buf_free(&p);
}

void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiStr* out) {
  Buf p = {NULL, 0, 0};
  store_preset_path(s, name, len, &p);
  rolltui_str_set(out, p.p ? p.p : "", p.len);
  buf_free(&p);
}

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const char* dir, size_t dir_len,
                                             int may_write_shipped, const char* shipped_dir, size_t shipped_dir_len) {
  RolltuiPresetStore* s = (RolltuiPresetStore*)rolltui_mem_alloc(sizeof(RolltuiPresetStore));
  const void* def;
  memset(s, 0, sizeof *s);
  s->d = d;
  s->rep = d->report;
  buf_set(&s->dir, dir, dir_len);
  buf_set(&s->shipped_dir, shipped_dir, shipped_dir_len);
  s->may_write_shipped = may_write_shipped;
  pthread_mutex_init(&s->mu, NULL);
  s->version = 1;
  def = rolltui_preset_shipped(d, "default", 7);
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
  s->d->to_json_with_origin(s->d, s->working, s->origin.p, s->origin.len, buf_put, &bytes);
  ok = write_file_atomic_put(path.p, path.len, bytes.p, bytes.len, buf_put, &err);
  if (ok) s->last_error.len = 0;
  else buf_set(&s->last_error, err.p, err.len);
  buf_free(&path);
  buf_free(&bytes);
  buf_free(&err);
  return ok;
}

static void touch_locked(RolltuiPresetStore* s, int persist) {
  /* The one deep compare, at the one place the answer can change — never on a read. */
  s->modified = !s->d->equal(s->working, s->origin_content);
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
  shipped = rolltui_preset_shipped(self->d, name, len);
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
   * it kept (Presets.hpp). Its notes get the path in front of them.
   *
   * THE NULL CHECK IS NOT DEFENSIVE — `parse_partial` is a slot a domain may legitimately
   * leave empty, and two of the three do: `rolltui_layout_preset_domain_init` and
   * `rolltui_bindings_preset_domain_init` both set it NULL with the comment "a layout/bindings
   * file is always whole". Calling it unconditionally therefore segfaulted the FIRST time a
   * pure-C Layout or Bindings store loaded a user-saved preset by name. Nothing caught it
   * because `PresetStore<D>`'s C++ descriptor always installs a real lambda in that slot even
   * when the wrapped `D::parse_partial` only ever returns nullopt — so the C++ path could not
   * reach the branch, and until this milestone there was no other path. Found 2026-09-05 by
   * converting `presets_test`, which is the only way any of these has been found. */
  v = s->d->parse_partial ? s->d->parse_partial(s->d, text.p, text.len, s->working, report) : NULL;
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
  v = s->d->parse(s->d, text.p, text.len, report);
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

void rolltui_preset_store_value_free(const RolltuiPresetStore* s, void* v) {
  if (s && v) s->d->destroy(v);
}

void rolltui_preset_store_start(RolltuiPresetStore* s, void* report) {
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
  parsed = s->d->parse(s->d, text.p, text.len, report);
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
    /* OWNED, short-lived: the origin preset is re-read for its CONTENT (to say "(modified)"
     * by comparison); what its report says was already said when it was loaded. */
    void* scratch = s->rep->create();
    void* oc = get_locked(s, s->origin.p, s->origin.len, scratch, NULL);
    s->rep->destroy(scratch);
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
  s->modified = !s->d->equal(s->working, s->origin_content);
  if (s->modified) buf_add(&msg, " (modified)", 11);
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
  m = s->modified;
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

void rolltui_preset_list_release(RolltuiPresetList* l) {
  size_t i;
  if (!l) return;
  for (i = 0; i < l->cap; ++i) {
    rolltui_str_free(&l->v[i].name);
    rolltui_str_free(&l->v[i].path);
  }
  rolltui_mem_free(l->v);
  l->v = NULL;
  l->n = 0;
  l->cap = 0;
}

/* One entry appended, REUSING the string buffers a previous call left in `cap` — which is the
 * whole reason this list is the caller's rather than a fresh vector per call. */
static RolltuiPresetInfo* list_add(RolltuiPresetList* l) {
  if (l->n == l->cap) {
    const size_t want = l->cap ? l->cap * 2 : 8;
    l->v = (RolltuiPresetInfo*)rolltui_grow_zeroed(l->v, &l->cap, want, sizeof *l->v);
  }
  return &l->v[l->n++];
}

void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetList* out) {
  Buf dir = {NULL, 0, 0}, path = {NULL, 0, 0};
  NameList list = {NULL, 0, 0};
  const size_t n = s->d->shipped_count();
  size_t i, pass;
  if (!out) return;
  out->n = 0; /* REPLACES: the entries' buffers stay, to be refilled */
  /* The shipped ones first, "default" ahead of the rest — the order a chooser offers them
   * in, and the same order `shipped_names()` gave. */
  for (pass = 0; pass < 2; ++pass)
    for (i = 0; i < n; ++i) {
      const char* nm = NULL;
      const char* text = NULL;
      size_t nlen = 0, tlen = 0;
      int is_default;
      RolltuiPresetInfo* e;
      s->d->shipped_at(i, &nm, &nlen, &text, &tlen);
      is_default = nlen == 7 && memcmp(nm, "default", 7) == 0;
      if ((pass == 0) != (is_default != 0)) continue;
      e = list_add(out);
      rolltui_str_set(&e->name, nm, nlen);
      rolltui_str_set(&e->path, "", 0);
      e->shipped = 1;
    }
  buf_add(&dir, s->dir.p, s->dir.len);
  buf_add(&dir, "/", 1);
  buf_add(&dir, s->d->subdir, s->d->subdir_len);
  collect_json_names(dir.p, dir.len, &list);
  for (i = 0; i < list.count; ++i) {
    RolltuiPresetInfo* e;
    /* A user file that shadows a shipped name is never listed as a user preset: the
     * shipped one is what `get` will answer with, so offering both would be two entries
     * for one thing. */
    if (rolltui_preset_is_shipped(s->d, list.names[i].p, list.names[i].len)) continue;
    store_preset_path(s, list.names[i].p, list.names[i].len, &path);
    e = list_add(out);
    rolltui_str_set(&e->name, list.names[i].p, list.names[i].len);
    rolltui_str_set(&e->path, path.p ? path.p : "", path.len);
    e->shipped = 0;
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

/* The outcome, with its sentence in `err` for every refusal that has a fixed one — so the
 * three early returns below and the composed result at the end say it the same way. */
static int save_result(int code, RolltuiStr* err) {
  if (err && code != ROLLTUI_SAVE_SAVED && code != ROLLTUI_SAVE_WRITE_FAILED) {
    size_t n = 0;
    const char* text = rolltui_preset_save_result_text(code, &n);
    rolltui_str_set(err, text, n);
  }
  return code;
}

int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err) {
  Buf path = {NULL, 0, 0}, bytes = {NULL, 0, 0};
  int result = ROLLTUI_SAVE_SAVED;
  if (err) rolltui_str_clear(err);
  pthread_mutex_lock(&s->mu);
  if (!rolltui_preset_valid_name(name, len)) {
    pthread_mutex_unlock(&s->mu);
    return save_result(ROLLTUI_SAVE_BAD_NAME, err);
  }
  if (rolltui_preset_is_shipped(s->d, name, len)) {
    if (!s->may_write_shipped) {
      pthread_mutex_unlock(&s->mu);
      return save_result(ROLLTUI_SAVE_REFUSED_SHIPPED, err);
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
      return save_result(ROLLTUI_SAVE_EXISTS_ASK, err);
    }
  }
  s->d->to_json(s->d, s->working, name, len, buf_put, &bytes);
  if (!write_file_atomic_put(path.p, path.len, bytes.p, bytes.len, rolltui_str_put, err)) {
    result = ROLLTUI_SAVE_WRITE_FAILED;
  } else {
    buf_set(&s->origin, name, len);
    s->d->destroy(s->origin_content);
    s->origin_content = s->d->clone(s->working);
    /* ALWAYS persists, unlike load/set_working/edit — on purpose, and asserted. A save-as is an
     * explicit write: the working copy records its new origin so the next start knows it, and
     * the studio's --frame mode (which suppresses EDIT autosaves) relies on exactly that
     * (`studio_golden_test`: "a manual save writes even under --frame"). A `persist` parameter
     * was added and removed on 2026-09-06: no caller wanted 0. */
    touch_locked(s, 1);
  }
  buf_free(&path);
  buf_free(&bytes);
  pthread_mutex_unlock(&s->mu);
  return result;
}

/* ---- the Theme domain's preset FILE FORMAT (this task) --------------------------------------
 * See rolltui_presets.h for the boundary this section keeps (the vocab table, and mode/depth
 * value validity, both cross as parameters rather than being re-derived here) and why. */

/* `K("literal")` — the (ptr, len) pair for a string literal, the same convenience
 * `rolltui_theme.c`'s own loader already uses (`strlen` costs nothing at preset-load rate,
 * never per frame). File-local to this translation unit, like that one's. */
#define K(s) (s), strlen(s)

static int tp_streq(const char* s, size_t len, const char* lit) {
  const size_t n = strlen(lit);
  return len == n && (n == 0 || memcmp(s, lit, n) == 0);
}

/* ---- the report: three growing arrays of small owned strings, the MOVE shape
 * `rolltui_layout.c`'s own `add_bad`/`add_unknown` already use — build the message into a
 * local `RolltuiStr`, hand it straight over, so there is exactly one copy of the bytes rather
 * than a build-then-copy-then-free. Private to this file: nothing outside ever writes one — a
 * caller (`Presets.cpp`) only reads it after a parse call, then releases it. */
static void tp_add_bad(RolltuiThemePresetReport* r, RolltuiStr* msg) {
  r->bad_values = (RolltuiStr*)rolltui_grow_zeroed(r->bad_values, &r->bad_values_cap, r->bad_values_n + 1,
                                                   sizeof *r->bad_values);
  rolltui_str_move(&r->bad_values[r->bad_values_n++], msg);
}
static void tp_add_unknown(RolltuiThemePresetReport* r, RolltuiStr* msg) {
  r->unknown_keys = (RolltuiStr*)rolltui_grow_zeroed(r->unknown_keys, &r->unknown_keys_cap, r->unknown_keys_n + 1,
                                                     sizeof *r->unknown_keys);
  rolltui_str_move(&r->unknown_keys[r->unknown_keys_n++], msg);
}
static void tp_add_note(RolltuiThemePresetReport* r, RolltuiStr* msg) {
  r->notes = (RolltuiStr*)rolltui_grow_zeroed(r->notes, &r->notes_cap, r->notes_n + 1, sizeof *r->notes);
  rolltui_str_move(&r->notes[r->notes_n++], msg);
}

void rolltui_theme_preset_report_release(RolltuiThemePresetReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  for (i = 0; i < r->bad_values_n; ++i) rolltui_str_free(&r->bad_values[i]);
  rolltui_mem_free(r->bad_values);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->notes_n; ++i) rolltui_str_free(&r->notes[i]);
  rolltui_mem_free(r->notes);
  rolltui_theme_report_release(&r->colours);
  memset(r, 0, sizeof *r);
}

/* The colours part, at BOTH modes — `rolltui_theme_load` directly, the entanglement this port
 * removes (rolltui_presets.h's header comment). `report->colours` becomes DARK's report
 * verbatim; LIGHT's own bad values not already in it are merged in — mirrors
 * `theme_preset_from_json`'s own double load exactly, including that only dark's success/
 * failure decides the return value. `styles` is sized by `vocab->role_count` (a RUNTIME value:
 * `Style.hpp`'s `kRoleCount` does not cross this boundary, `rolltui_style.h`'s own rule) and
 * allocated ONCE, reused for both calls — each fully overwrites it, so there is nothing to
 * re-zero between them. Neither call's styles or effect map is kept: this function answers
 * only "did it load, and what did it say", the same thing the two discarded `Theme`s in the
 * original C++ were kept only long enough to ask. */
static int tp_load_colours(const RolltuiJsonValue* colours, const RolltuiThemeVocab* vocab,
                           RolltuiThemePresetReport* report) {
  RolltuiStyle* styles = (RolltuiStyle*)rolltui_mem_alloc(vocab->role_count * sizeof *styles);
  RolltuiStr name = {0};
  RolltuiEffectMap* dark_eff;
  RolltuiThemeReport light_rep = {0};
  RolltuiStr light_name = {0};
  RolltuiEffectMap* light_eff;
  size_t li;

  dark_eff = rolltui_theme_load(colours, ROLLTUI_MODE_DARK, vocab, styles, &name, &report->colours);
  rolltui_str_free(&name);
  if (dark_eff) rolltui_effect_map_free(dark_eff);

  light_eff = rolltui_theme_load(colours, ROLLTUI_MODE_LIGHT, vocab, styles, &light_name, &light_rep);
  rolltui_str_free(&light_name);
  if (light_eff) rolltui_effect_map_free(light_eff);

  for (li = 0; li < light_rep.bad_values_n; ++li) {
    size_t blen = 0;
    const char* btext = rolltui_str_get(&light_rep.bad_values[li], &blen);
    int dup = 0;
    size_t di;
    for (di = 0; di < report->colours.bad_values_n; ++di)
      if (rolltui_str_eq(&report->colours.bad_values[di], btext, blen)) { dup = 1; break; }
    if (!dup) rolltui_theme_report_add_bad_value(&report->colours, btext, blen);
  }
  rolltui_theme_report_release(&light_rep);
  rolltui_mem_free(styles);
  return dark_eff != NULL;
}

int rolltui_theme_preset_parse(const RolltuiJsonValue* root, const RolltuiThemeVocab* vocab,
                               RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid,
                               RolltuiStr* out_mode, RolltuiStr* out_depth, const RolltuiJsonValue** out_colours,
                               RolltuiThemePresetReport* report) {
  size_t i, n;
  rolltui_theme_preset_report_release(report);
  *out_colours = NULL;
  if (!rolltui_json_is_object(root)) {
    rolltui_str_set(&report->error, K("a preset file must be a JSON object"));
    return 0;
  }
  if (!rolltui_json_has(root, K("colours"))) {
    rolltui_str_set(&report->error, K("a preset file needs a \"colours\" object (Theme.hpp's format)"));
    return 0;
  }
  /* `ThemePreset`'s own member-initialisers (Presets.hpp): both default to "auto" when the
   * key is absent or fails its check below — set here, up front, once. */
  rolltui_str_set(out_mode, K("auto"));
  rolltui_str_set(out_depth, K("auto"));
  n = rolltui_json_object_size(root);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(root, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(root, i);
    if (tp_streq(k, klen, "name") || tp_streq(k, klen, "preset")) {
      if (!rolltui_json_is_string(x)) {
        RolltuiStr msg = {0};
        rolltui_str_append(&msg, k, klen);
        rolltui_str_append(&msg, K(": expected a string"));
        tp_add_bad(report, &msg);
      }
    } else if (tp_streq(k, klen, "mode")) {
      size_t slen = 0;
      const char* s = rolltui_json_as_string(x, "", 0, &slen);
      if (!mode_valid(s, slen)) {
        RolltuiStr msg = {0};
        rolltui_str_append(&msg, K("mode: expected auto | dark | light"));
        tp_add_bad(report, &msg);
      } else {
        rolltui_str_set(out_mode, s, slen);
      }
    } else if (tp_streq(k, klen, "depth")) {
      size_t slen = 0;
      const char* s = rolltui_json_as_string(x, "", 0, &slen);
      if (!depth_valid(s, slen)) {
        RolltuiStr msg = {0};
        rolltui_str_append(&msg, K("depth: expected auto | truecolor | 256 | 16 | mono"));
        tp_add_bad(report, &msg);
      } else {
        rolltui_str_set(out_depth, s, slen);
      }
    } else if (tp_streq(k, klen, "colours")) {
      *out_colours = x;
    } else {
      RolltuiStr msg = {0};
      rolltui_str_append(&msg, k, klen);
      tp_add_unknown(report, &msg);
    }
  }
  if (!tp_load_colours(*out_colours, vocab, report)) {
    size_t elen = 0;
    const char* etext = rolltui_str_get(&report->colours.error, &elen);
    RolltuiStr msg = {0};
    rolltui_str_append(&msg, K("colours: "));
    rolltui_str_append(&msg, etext, elen);
    rolltui_str_move(&report->error, &msg);
    return 0;
  }
  return 1;
}

int rolltui_theme_preset_parse_partial(const RolltuiJsonValue* root, const RolltuiThemeVocab* vocab,
                                       const RolltuiJsonValue** out_colours, RolltuiThemePresetReport* report) {
  RolltuiStyle* styles;
  RolltuiStr name = {0};
  RolltuiEffectMap* eff;
  rolltui_theme_preset_report_release(report);
  *out_colours = NULL;
  if (!rolltui_json_is_object(root) || rolltui_json_has(root, K("colours")) || !rolltui_json_has(root, K("roles")))
    return 0; /* not partial: report->error stays empty, so the generic mechanics fall back
               * to rolltui_theme_preset_parse() on the same text (get_locked, above). */
  styles = (RolltuiStyle*)rolltui_mem_alloc(vocab->role_count * sizeof *styles);
  eff = rolltui_theme_load(root, ROLLTUI_MODE_DARK, vocab, styles, &name, &report->colours);
  rolltui_str_free(&name);
  rolltui_mem_free(styles);
  if (!eff) {
    /* UNPREFIXED, unlike `_parse`'s "colours: " — the whole file IS the colours object here,
     * so there is no second thing to name (ported as found: `ThemeDomain::parse_partial`'s
     * own asymmetry with `_parse`, which validates both modes; this validates dark only). */
    size_t elen = 0;
    const char* etext = rolltui_str_get(&report->colours.error, &elen);
    rolltui_str_set(&report->error, etext, elen);
    return 0;
  }
  rolltui_effect_map_free(eff);
  *out_colours = root;
  {
    RolltuiStr msg = {0};
    rolltui_str_append(&msg, K("is a colours-only theme file; mode and depth are kept"));
    tp_add_note(report, &msg);
  }
  return 1;
}

RolltuiJsonValue* rolltui_theme_preset_to_json(RolltuiJsonValue* colours, const char* mode, size_t mode_len,
                                               const char* depth, size_t depth_len, const char* name,
                                               size_t name_len) {
  RolltuiJsonValue* o = rolltui_json_object();
  rolltui_json_set(o, K("name"), rolltui_json_string(name, name_len));
  rolltui_json_set(o, K("mode"), rolltui_json_string(mode, mode_len));
  rolltui_json_set(o, K("depth"), rolltui_json_string(depth, depth_len));
  rolltui_json_set(o, K("colours"), colours);
  return o;
}

/* ============================================================================================
 * C-SIDE DOMAIN DESCRIPTORS (rolltui_presets.h has the why) — Theme, then Layout, then
 * Bindings, in the order the header states them.
 * ============================================================================================ */

/* Reads the "preset" key a working-copy FILE carries (written by `to_json_with_origin`
 * below), "default" when absent or the text does not parse — the one answer
 * `PresetStore.hpp`'s own `domain_storage<D>()` gives for all three domains alike, since none
 * of their OWN file formats say anything about it either. Shared: nothing here is
 * domain-specific. */
static void generic_preset_origin_of(const char* text, size_t len, RolltuiPutFn put, void* ctx) {
  RolltuiStr err = {0};
  RolltuiJsonValue* root = rolltui_json_parse(text, len, &err);
  static const char kDefault[] = "default";
  if (root) {
    size_t olen = 0;
    const char* o = rolltui_json_as_string(rolltui_json_get(root, K("preset")), kDefault, 7, &olen);
    put(ctx, o, olen);
    rolltui_json_free(root);
  } else {
    put(ctx, kDefault, 7);
  }
  rolltui_str_free(&err);
}

/* ---- the Theme domain -------------------------------------------------------------------- */

void rolltui_theme_preset_value_release(RolltuiThemePresetValue* v) {
  if (!v) return;
  rolltui_json_free(v->colours);
  rolltui_str_free(&v->mode);
  rolltui_str_free(&v->depth);
  memset(v, 0, sizeof *v);
}

static void theme_preset_report_reset(void* r) { rolltui_theme_preset_report_release((RolltuiThemePresetReport*)r); }
static void theme_preset_report_set_error(void* r, const char* s, size_t len) {
  rolltui_str_set(&((RolltuiThemePresetReport*)r)->error, s, len);
}
static void theme_preset_report_get_error(const void* r, RolltuiPutFn put, void* ctx) {
  const RolltuiThemePresetReport* t = (const RolltuiThemePresetReport*)r;
  put(ctx, t->error.p ? t->error.p : "", t->error.n);
}
static void theme_preset_report_add_note(void* r, const char* s, size_t len) {
  RolltuiStr msg = {0};
  rolltui_str_set(&msg, s, len);
  tp_add_note((RolltuiThemePresetReport*)r, &msg); /* the file's own move-in helper, reused */
}
static void theme_preset_report_prefix_notes(void* r, const char* p, size_t len) {
  RolltuiThemePresetReport* t = (RolltuiThemePresetReport*)r;
  size_t i;
  for (i = 0; i < t->notes_n; ++i) {
    RolltuiStr n = {0};
    rolltui_str_append(&n, p, len);
    rolltui_str_append_str(&n, &t->notes[i]);
    rolltui_str_move(&t->notes[i], &n);
  }
}
/* OWNED, short-lived, through the library's entry point: a report the mechanics make for
 * a parse whose outcome they do not keep. `reset` is the domain's own `_release`. */
static void* theme_preset_report_create(void) {
  RolltuiThemePresetReport* r = (RolltuiThemePresetReport*)rolltui_mem_alloc(sizeof *r);
  memset(r, 0, sizeof *r);
  return r;
}
static void theme_preset_report_destroy(void* r) {
  rolltui_theme_preset_report_release((RolltuiThemePresetReport*)r);
  rolltui_mem_free(r);
}
static const RolltuiPresetReportFns kThemePresetReportFns = {
    theme_preset_report_reset,
    theme_preset_report_set_error,
    theme_preset_report_get_error,
    theme_preset_report_add_note,
    theme_preset_report_prefix_notes,
    theme_preset_report_create,
    theme_preset_report_destroy,
};

/* The vocab and the two validators live ON THE DESCRIPTOR since Phase 25 m2 — see the note at
 * `RolltuiPresetDomain`'s configuration block for the two defects the statics carried. */

static size_t theme_domain_shipped_count(void) { return rolltui_kThemePresetCount; }
static void theme_domain_shipped_at(size_t i, const char** name, size_t* nlen, const char** text, size_t* tlen) {
  *name = rolltui_kThemePresets[i].name;
  *nlen = strlen(*name);
  *text = rolltui_kThemePresets[i].text;
  *tlen = strlen(*text);
}

/* A NULL REPORT IS LEGAL AT THE DESCRIPTOR AND MEANS "DO NOT TELL ME WHY" — added
 * 2026-09-05, when the first test ever to pass one crashed here. Every other out-param in
 * this library is optional (`rolltui_mem_stats`: "Any pointer may be NULL"), and a C caller
 * that only wants the value has no reason to build a report to throw away.
 *
 * IT IS GUARDED HERE, AT THE TWO WRAPPERS, AND NOT INSIDE THE PARSE FUNCTIONS. The first
 * attempt substituted a scratch report inside `rolltui_theme_preset_parse{,_partial}` and
 * released it before each `return` — which silently broke two assertions, because one of
 * those returns is the body of a brace-less `if` and the inserted line made it
 * unconditional. The parse functions keep their contract (a report is required); only the
 * descriptor, which is the one door a NULL can come through, substitutes one. */
static void* theme_domain_parse(const RolltuiPresetDomain* d, const char* text, size_t len, void* rep) {
  RolltuiThemePresetReport* r = (RolltuiThemePresetReport*)rep;
  RolltuiThemePresetReport scratch = {0};
  if (!r) r = &scratch;
  RolltuiJsonValue* root;
  RolltuiStr err = {0};
  RolltuiStr mode = {0}, depth = {0};
  const RolltuiJsonValue* colours = NULL;
  RolltuiThemePresetValue* out = NULL;
  root = rolltui_json_parse(text, len, &err);
  if (!root) {
    rolltui_str_append(&r->error, K("unreadable ("));
    rolltui_str_append_str(&r->error, &err);
    rolltui_str_append(&r->error, ")", 1);
    rolltui_str_free(&err);
    if (r == &scratch) rolltui_theme_preset_report_release(&scratch);
    return NULL;
  }
  rolltui_str_free(&err);
  if (rolltui_theme_preset_parse(root, d->theme_vocab, d->theme_mode_valid, d->theme_depth_valid, &mode, &depth,
                                &colours, r)) {
    out = (RolltuiThemePresetValue*)rolltui_mem_alloc(sizeof *out);
    memset(out, 0, sizeof *out);
    /* `colours` BORROWS `root` (this header's own comment on `rolltui_theme_preset_parse`),
     * which is freed below, so the value clones it rather than adopting it. */
    out->colours = rolltui_json_clone(colours);
    rolltui_str_move(&out->mode, &mode);
    rolltui_str_move(&out->depth, &depth);
  }
  rolltui_str_free(&mode);
  rolltui_str_free(&depth);
  rolltui_json_free(root);
  if (r == &scratch) rolltui_theme_preset_report_release(&scratch);
  return out;
}

static void* theme_domain_parse_partial(const RolltuiPresetDomain* d, const char* text, size_t len,
                                        const void* working, void* rep) {
  RolltuiThemePresetReport* r = (RolltuiThemePresetReport*)rep;
  RolltuiThemePresetReport scratch = {0};
  if (!r) r = &scratch;
  RolltuiJsonValue* root;
  RolltuiStr err = {0};
  const RolltuiJsonValue* colours = NULL;
  RolltuiThemePresetValue* out = NULL;
  const RolltuiThemePresetValue* w = (const RolltuiThemePresetValue*)working;
  root = rolltui_json_parse(text, len, &err);
  if (!root) {
    rolltui_str_append(&r->error, K("unreadable ("));
    rolltui_str_append_str(&r->error, &err);
    rolltui_str_append(&r->error, ")", 1);
    rolltui_str_free(&err);
    if (r == &scratch) rolltui_theme_preset_report_release(&scratch);
    return NULL;
  }
  rolltui_str_free(&err);
  if (rolltui_theme_preset_parse_partial(root, d->theme_vocab, &colours, r)) {
    out = (RolltuiThemePresetValue*)rolltui_mem_alloc(sizeof *out);
    memset(out, 0, sizeof *out);
    out->colours = rolltui_json_clone(colours);
    rolltui_str_set(&out->mode, w->mode.p ? w->mode.p : "", w->mode.n);
    rolltui_str_set(&out->depth, w->depth.p ? w->depth.p : "", w->depth.n);
  }
  rolltui_json_free(root);
  if (r == &scratch) rolltui_theme_preset_report_release(&scratch);
  return out;
}

static void theme_domain_to_json(const RolltuiPresetDomain* d, const void* v, const char* name, size_t len,
                 RolltuiPutFn put, void* ctx) {
  (void)d;
  const RolltuiThemePresetValue* p = (const RolltuiThemePresetValue*)v;
  RolltuiJsonValue* tree = rolltui_theme_preset_to_json(rolltui_json_clone(p->colours), p->mode.p, p->mode.n,
                                                        p->depth.p, p->depth.n, name, len);
  RolltuiStr out = {0};
  rolltui_json_dump(tree, 2, &out);
  rolltui_str_append(&out, "\n", 1);
  put(ctx, out.p, out.n);
  rolltui_json_free(tree);
  rolltui_str_free(&out);
}
static void theme_domain_to_json_with_origin(const RolltuiPresetDomain* d, const void* v, const char* name,
                                             size_t len, RolltuiPutFn put, void* ctx) {
  (void)d; /* the theme serialiser needs no configuration; the layout one does */
  const RolltuiThemePresetValue* p = (const RolltuiThemePresetValue*)v;
  RolltuiJsonValue* tree = rolltui_theme_preset_to_json(rolltui_json_clone(p->colours), p->mode.p, p->mode.n,
                                                        p->depth.p, p->depth.n, name, len);
  RolltuiStr out = {0};
  rolltui_json_set(tree, K("preset"), rolltui_json_string(name, len));
  rolltui_json_dump(tree, 2, &out);
  rolltui_str_append(&out, "\n", 1);
  put(ctx, out.p, out.n);
  rolltui_json_free(tree);
  rolltui_str_free(&out);
}

static void* theme_domain_clone(const void* v) {
  const RolltuiThemePresetValue* p = (const RolltuiThemePresetValue*)v;
  RolltuiThemePresetValue* out = (RolltuiThemePresetValue*)rolltui_mem_alloc(sizeof *out);
  /* `rolltui_mem_alloc` does not zero: `rolltui_str_set` below reads `out->mode`/`out->depth`
   * as ALREADY-VALID `RolltuiStr`s (to decide whether to reuse or grow their buffers), so a
   * fresh allocation must be zeroed before either is touched, exactly as `theme_domain_parse`
   * above already does — ASan's `rolltui_fit` unpoison check on garbage `cap`/`p` is what
   * caught this one missing here. */
  memset(out, 0, sizeof *out);
  out->colours = rolltui_json_clone(p->colours);
  rolltui_str_set(&out->mode, p->mode.p ? p->mode.p : "", p->mode.n);
  rolltui_str_set(&out->depth, p->depth.p ? p->depth.p : "", p->depth.n);
  return out;
}
static void theme_domain_destroy(void* v) {
  rolltui_theme_preset_value_release((RolltuiThemePresetValue*)v);
  rolltui_mem_free(v);
}
static int theme_domain_equal(const void* a, const void* b) {
  const RolltuiThemePresetValue *x = (const RolltuiThemePresetValue*)a, *y = (const RolltuiThemePresetValue*)b;
  return rolltui_json_equal(x->colours, y->colours) &&
         rolltui_str_eq(&x->mode, y->mode.p ? y->mode.p : "", y->mode.n) &&
         rolltui_str_eq(&x->depth, y->depth.p ? y->depth.p : "", y->depth.n);
}

void rolltui_theme_preset_domain_init(RolltuiPresetDomain* out, const RolltuiThemeVocab* vocab,
                                      RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid) {
  memset(out, 0, sizeof *out);
  /* AFTER the memset, not before it — the assignments were written above it once and the
   * zeroing silently ate them, so the first shipped theme parsed through a NULL validator. */
  out->theme_vocab = vocab;
  out->theme_mode_valid = mode_valid;
  out->theme_depth_valid = depth_valid;
  out->kind = "theme";
  out->kind_len = sizeof("theme") - 1;
  out->working_file = "theme.working.json";
  out->working_file_len = sizeof("theme.working.json") - 1;
  out->subdir = "themes";
  out->subdir_len = sizeof("themes") - 1;
  out->shipped_count = theme_domain_shipped_count;
  out->shipped_at = theme_domain_shipped_at;
  out->parse = theme_domain_parse;
  out->parse_partial = theme_domain_parse_partial;
  out->to_json = theme_domain_to_json;
  out->to_json_with_origin = theme_domain_to_json_with_origin;
  out->origin_of = generic_preset_origin_of;
  out->clone = theme_domain_clone;
  out->destroy = theme_domain_destroy;
  out->equal = theme_domain_equal;
  out->report = &kThemePresetReportFns;
}

/* ---- the Layout domain ------------------------------------------------------------------- */

void rolltui_layout_preset_report_release(RolltuiLayoutPresetReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  rolltui_layout_report_release(&r->layout);
  for (i = 0; i < r->notes_n; ++i) rolltui_str_free(&r->notes[i]);
  rolltui_mem_free(r->notes);
  memset(r, 0, sizeof *r);
}
static RolltuiStr* layout_preset_note_add(RolltuiLayoutPresetReport* r) {
  r->notes = (RolltuiStr*)rolltui_grow_zeroed(r->notes, &r->notes_cap, r->notes_n + 1, sizeof *r->notes);
  return &r->notes[r->notes_n++];
}
static void layout_preset_report_reset(void* r) {
  rolltui_layout_preset_report_release((RolltuiLayoutPresetReport*)r);
}
static void layout_preset_report_set_error(void* r, const char* s, size_t len) {
  rolltui_str_set(&((RolltuiLayoutPresetReport*)r)->error, s, len);
}
static void layout_preset_report_get_error(const void* r, RolltuiPutFn put, void* ctx) {
  const RolltuiLayoutPresetReport* l = (const RolltuiLayoutPresetReport*)r;
  put(ctx, l->error.p ? l->error.p : "", l->error.n);
}
static void layout_preset_report_add_note(void* r, const char* s, size_t len) {
  RolltuiStr* n = layout_preset_note_add((RolltuiLayoutPresetReport*)r);
  rolltui_str_set(n, s, len);
}
static void layout_preset_report_prefix_notes(void* r, const char* p, size_t len) {
  RolltuiLayoutPresetReport* l = (RolltuiLayoutPresetReport*)r;
  size_t i;
  for (i = 0; i < l->notes_n; ++i) {
    RolltuiStr n = {0};
    rolltui_str_append(&n, p, len);
    rolltui_str_append_str(&n, &l->notes[i]);
    rolltui_str_move(&l->notes[i], &n);
  }
}
/* OWNED, short-lived, through the library's entry point: a report the mechanics make for
 * a parse whose outcome they do not keep. `reset` is the domain's own `_release`. */
static void* layout_preset_report_create(void) {
  RolltuiLayoutPresetReport* r = (RolltuiLayoutPresetReport*)rolltui_mem_alloc(sizeof *r);
  memset(r, 0, sizeof *r);
  return r;
}
static void layout_preset_report_destroy(void* r) {
  rolltui_layout_preset_report_release((RolltuiLayoutPresetReport*)r);
  rolltui_mem_free(r);
}
static const RolltuiPresetReportFns kLayoutPresetReportFns = {
    layout_preset_report_reset,
    layout_preset_report_set_error,
    layout_preset_report_get_error,
    layout_preset_report_add_note,
    layout_preset_report_prefix_notes,
    layout_preset_report_create,
    layout_preset_report_destroy,
};

/* The hooks and the shipped `default` layout's own actions, set once by
 * `rolltui_layout_preset_domain_init` — the same shape the Theme domain's vocab/validators
 * above already use. */

static size_t layout_domain_shipped_count(void) { return rolltui_kLayoutPresetCount; }
static void layout_domain_shipped_at(size_t i, const char** name, size_t* nlen, const char** text, size_t* tlen) {
  *name = rolltui_kLayoutPresets[i].name;
  *nlen = strlen(*name);
  *text = rolltui_kLayoutPresets[i].text;
  *tlen = strlen(*text);
}

/* "preset" is the STORE's own bookkeeping in a working-copy file, not a layout key
 * (`LayoutDomain::parse`'s rule) — stripped here, on a tree this call already owns outright,
 * over the type's own already-public fields and existing free functions. `rolltui_json.h` has
 * no removal primitive of its own (only `_set`, insert-or-replace — adding one belongs in that
 * file, not this one), so this is the walk `rolltui_json_object_size`/`_key_at` already make
 * possible, not a new API grown here. At most one "preset" member can exist (the parser
 * itself rejects a duplicate key), so the first match found is the only one there is. */
static void strip_preset_member(RolltuiJsonValue* root) {
  size_t i;
  if (!root || !rolltui_json_is_object(root)) return;
  for (i = 0; i < root->obj_n; ++i) {
    RolltuiJsonMember* m = root->obj[i];
    if (m->key.n == 6 && memcmp(m->key.p, "preset", 6) == 0) {
      rolltui_str_free(&m->key);
      rolltui_json_free(m->value);
      rolltui_mem_free(m);
      for (; i + 1 < root->obj_n; ++i) root->obj[i] = root->obj[i + 1];
      root->obj_n -= 1;
      return;
    }
  }
}

/* The loaded tree is MOVED into a `RolltuiLayout`, not copied, since the caller releases it
 * either way — `rolltui_loaded_layout_to_layout`. This file had its own file-static copy of
 * that conversion, whose comment said it existed "the same reason `Layout.cpp`'s own
 * `loaded_to_layout` exists"; there were THREE of it, and it took an agent converting
 * `layout_editor.cpp` to need a fourth before anyone counted. */
static void* layout_domain_parse(const RolltuiPresetDomain* d, const char* text, size_t len, void* rep) {
  RolltuiLayoutPresetReport* r = (RolltuiLayoutPresetReport*)rep;
  RolltuiJsonValue* root;
  RolltuiStr err = {0};
  RolltuiLoadedLayout loaded;
  RolltuiLayout* out = NULL;
  int ok;
  size_t i;
  root = rolltui_json_parse(text, len, &err);
  if (!root) {
    rolltui_str_append(&r->error, K("unreadable ("));
    rolltui_str_append_str(&r->error, &err);
    rolltui_str_append(&r->error, ")", 1);
    rolltui_str_free(&err);
    return NULL;
  }
  rolltui_str_free(&err);
  strip_preset_member(root);
  rolltui_loaded_layout_init(&loaded);
  ok = rolltui_load_layout(root, &loaded, d->layout_actions, d->layout_actions_n, d->layout_hooks,
                           &r->layout);
  rolltui_json_free(root);
  if (!ok) {
    rolltui_str_set(&r->error, r->layout.error.p ? r->layout.error.p : "", r->layout.error.n);
    rolltui_loaded_layout_release(&loaded);
    return NULL;
  }
  for (i = 0; i < r->layout.notes_n; ++i) {
    RolltuiStr* n = layout_preset_note_add(r);
    rolltui_str_clear(n);
    rolltui_str_append(n, K("layout: "));
    rolltui_str_append_str(n, &r->layout.notes[i]);
  }
  out = (RolltuiLayout*)rolltui_mem_alloc(sizeof *out);
  rolltui_layout_init(out);
  rolltui_loaded_layout_to_layout(&loaded, out);
  rolltui_loaded_layout_release(&loaded); /* a no-op now: every owning field was moved out */
  return out;
}

static void layout_domain_to_json(const RolltuiPresetDomain* d, const void* v, const char* name, size_t len,
                                  RolltuiPutFn put, void* ctx) {
  const RolltuiLayout* l = (const RolltuiLayout*)v;
  RolltuiStr out = {0};
  /* The preset name is NEVER written over the layout's own "name" (`LayoutDomain::to_json`'s
   * rule — a save that silently renamed what was saved would break `modified()`). */
  (void)name;
  (void)len;
  rolltui_layout_to_json_text(l->name.p, l->name.n, l->min_width, l->min_height, l->actions.v, l->actions.n, &l->base,
                              l->popups.v, l->popups.n, d->layout_hooks, &out);
  put(ctx, out.p, out.n);
  rolltui_str_free(&out);
}
static void layout_domain_to_json_with_origin(const RolltuiPresetDomain* d, const void* v, const char* name,
                                              size_t len, RolltuiPutFn put,
                                              void* ctx) {
  const RolltuiLayout* l = (const RolltuiLayout*)v;
  RolltuiJsonValue* tree = rolltui_layout_to_json_value(l->name.p, l->name.n, l->min_width, l->min_height,
                                                        l->actions.v, l->actions.n, &l->base, l->popups.v,
                                                        l->popups.n, d->layout_hooks);
  RolltuiStr out = {0};
  rolltui_json_set(tree, K("preset"), rolltui_json_string(name, len));
  rolltui_json_dump(tree, 2, &out);
  rolltui_str_append(&out, "\n", 1);
  put(ctx, out.p, out.n);
  rolltui_json_free(tree);
  rolltui_str_free(&out);
}

static void* layout_domain_clone(const void* v) {
  RolltuiLayout* out = (RolltuiLayout*)rolltui_mem_alloc(sizeof *out);
  rolltui_layout_init(out);
  rolltui_layout_copy(out, (const RolltuiLayout*)v);
  return out;
}
static void layout_domain_destroy(void* v) {
  RolltuiLayout* l = (RolltuiLayout*)v;
  rolltui_layout_release(l);
  rolltui_mem_free(l);
}
static int layout_domain_equal(const void* a, const void* b) {
  return rolltui_layout_equal((const RolltuiLayout*)a, (const RolltuiLayout*)b);
}

void rolltui_layout_preset_domain_init(RolltuiPresetDomain* out, const RolltuiLayoutHooks* hooks,
                                       const RolltuiLayoutAction* default_actions, size_t default_actions_n) {
  memset(out, 0, sizeof *out);
  out->layout_hooks = hooks;
  out->layout_actions = default_actions;
  out->layout_actions_n = default_actions_n;
  out->kind = "layout";
  out->kind_len = sizeof("layout") - 1;
  out->working_file = "layout.working.json";
  out->working_file_len = sizeof("layout.working.json") - 1;
  out->subdir = "layouts";
  out->subdir_len = sizeof("layouts") - 1;
  out->shipped_count = layout_domain_shipped_count;
  out->shipped_at = layout_domain_shipped_at;
  out->parse = layout_domain_parse;
  out->parse_partial = NULL; /* a layout file is always whole (LayoutDomain's own rule) */
  out->to_json = layout_domain_to_json;
  out->to_json_with_origin = layout_domain_to_json_with_origin;
  out->origin_of = generic_preset_origin_of;
  out->clone = layout_domain_clone;
  out->destroy = layout_domain_destroy;
  out->equal = layout_domain_equal;
  out->report = &kLayoutPresetReportFns;
}

/* ---- the Bindings domain ----------------------------------------------------------------- */

void rolltui_bindings_preset_report_release(RolltuiBindingsPresetReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  rolltui_bindings_report_release(&r->bindings);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->notes_n; ++i) rolltui_str_free(&r->notes[i]);
  rolltui_mem_free(r->notes);
  memset(r, 0, sizeof *r);
}
static RolltuiStr* bindings_preset_unknown_add(RolltuiBindingsPresetReport* r) {
  r->unknown_keys =
      (RolltuiStr*)rolltui_grow_zeroed(r->unknown_keys, &r->unknown_keys_cap, r->unknown_keys_n + 1,
                                       sizeof *r->unknown_keys);
  return &r->unknown_keys[r->unknown_keys_n++];
}
static RolltuiStr* bindings_preset_note_add(RolltuiBindingsPresetReport* r) {
  r->notes = (RolltuiStr*)rolltui_grow_zeroed(r->notes, &r->notes_cap, r->notes_n + 1, sizeof *r->notes);
  return &r->notes[r->notes_n++];
}
static void bindings_preset_report_reset(void* r) {
  rolltui_bindings_preset_report_release((RolltuiBindingsPresetReport*)r);
}
static void bindings_preset_report_set_error(void* r, const char* s, size_t len) {
  rolltui_str_set(&((RolltuiBindingsPresetReport*)r)->error, s, len);
}
static void bindings_preset_report_get_error(const void* r, RolltuiPutFn put, void* ctx) {
  const RolltuiBindingsPresetReport* b = (const RolltuiBindingsPresetReport*)r;
  put(ctx, b->error.p ? b->error.p : "", b->error.n);
}
static void bindings_preset_report_add_note(void* r, const char* s, size_t len) {
  RolltuiStr* n = bindings_preset_note_add((RolltuiBindingsPresetReport*)r);
  rolltui_str_set(n, s, len);
}
static void bindings_preset_report_prefix_notes(void* r, const char* p, size_t len) {
  RolltuiBindingsPresetReport* b = (RolltuiBindingsPresetReport*)r;
  size_t i;
  for (i = 0; i < b->notes_n; ++i) {
    RolltuiStr n = {0};
    rolltui_str_append(&n, p, len);
    rolltui_str_append_str(&n, &b->notes[i]);
    rolltui_str_move(&b->notes[i], &n);
  }
}
/* OWNED, short-lived, through the library's entry point: a report the mechanics make for
 * a parse whose outcome they do not keep. `reset` is the domain's own `_release`. */
static void* bindings_preset_report_create(void) {
  RolltuiBindingsPresetReport* r = (RolltuiBindingsPresetReport*)rolltui_mem_alloc(sizeof *r);
  memset(r, 0, sizeof *r);
  return r;
}
static void bindings_preset_report_destroy(void* r) {
  rolltui_bindings_preset_report_release((RolltuiBindingsPresetReport*)r);
  rolltui_mem_free(r);
}
static const RolltuiPresetReportFns kBindingsPresetReportFns = {
    bindings_preset_report_reset,
    bindings_preset_report_set_error,
    bindings_preset_report_get_error,
    bindings_preset_report_add_note,
    bindings_preset_report_prefix_notes,
    bindings_preset_report_create,
    bindings_preset_report_destroy,
};

/* The three vocabulary hooks, set once by `rolltui_bindings_preset_domain_init` — the same
 * three `rolltui_bindings_load_json` itself already takes as parameters. */

static size_t bindings_domain_shipped_count(void) { return rolltui_kBindingsPresetCount; }
static void bindings_domain_shipped_at(size_t i, const char** name, size_t* nlen, const char** text, size_t* tlen) {
  *name = rolltui_kBindingsPresets[i].name;
  *nlen = strlen(*name);
  *text = rolltui_kBindingsPresets[i].text;
  *tlen = strlen(*text);
}

/* Seeded exactly as `rolltui::Bindings::Bindings()` seeds one: every library action declared,
 * and the Enter rule set — over the closed table (`rolltui_library_action_*`) rather than a
 * second copy of the 59 rows, the same table that already closed this exact gap for four
 * other consumers (`rolltui_bindings.h`'s own header comment). */
static RolltuiBindings* bindings_domain_new_seeded(void) {
  RolltuiBindings* b = rolltui_bindings_new();
  const size_t n = rolltui_library_action_count();
  size_t i;
  rolltui_bindings_set_enter_rule(b, K("input.submit"));
  for (i = 0; i < n; ++i) {
    size_t nlen = 0, dlen = 0;
    const char* name = rolltui_library_action_name(i, &nlen);
    const char* desc = rolltui_library_action_description(i, &dlen);
    rolltui_bindings_add_action(b, name, nlen, desc, dlen);
  }
  return b;
}

static int bindings_key_is_outer(const char* k, size_t klen) {
  return (klen == 4 && memcmp(k, "name", 4) == 0) || (klen == 8 && memcmp(k, "bindings", 8) == 0) ||
         (klen == 6 && memcmp(k, "preset", 6) == 0);
}

static void* bindings_domain_parse(const RolltuiPresetDomain* d, const char* text, size_t len, void* rep) {
  RolltuiBindingsPresetReport* r = (RolltuiBindingsPresetReport*)rep;
  RolltuiJsonValue* root;
  RolltuiStr err = {0};
  RolltuiBindings* b;
  int ok;
  size_t i;
  root = rolltui_json_parse(text, len, &err);
  if (!root) {
    rolltui_str_append(&r->error, K("unreadable ("));
    rolltui_str_append_str(&r->error, &err);
    rolltui_str_append(&r->error, ")", 1);
    rolltui_str_free(&err);
    return NULL;
  }
  rolltui_str_free(&err);
  b = bindings_domain_new_seeded();
  /* `rolltui_bindings_load_json` re-parses `text` itself: the outer-key walk just below needs
   * the tree anyway, and there is no tree-taking overload to hand it this one instead — a real
   * but minor cost paid once per load, never once per frame. */
  ok = rolltui_bindings_load_json(b, text, len, rolltui_key_active_protocol(), d->bindings_is_library_scope,
                                 d->bindings_scope_ctx, d->bindings_reason, d->bindings_reason_ctx, &r->bindings);
  if (!ok) {
    rolltui_str_set(&r->error, r->bindings.error.p ? r->bindings.error.p : "", r->bindings.error.n);
    rolltui_bindings_free(b);
    rolltui_json_free(root);
    return NULL;
  }
  if (rolltui_json_is_object(root)) {
    const size_t on = rolltui_json_object_size(root);
    for (i = 0; i < on; ++i) {
      size_t klen = 0;
      const char* k = rolltui_json_object_key_at(root, i, &klen);
      if (!bindings_key_is_outer(k, klen)) {
        RolltuiStr* n = bindings_preset_unknown_add(r);
        rolltui_str_set(n, k, klen);
      }
    }
  }
  rolltui_json_free(root);
  return b;
}

static void bindings_domain_to_json(const RolltuiPresetDomain* d, const void* v, const char* name, size_t len,
                 RolltuiPutFn put, void* ctx) {
  (void)d;
  RolltuiStr out = {0};
  rolltui_bindings_dump_json((const RolltuiBindings*)v, name, len, &out); /* already trailing-newlined */
  put(ctx, out.p, out.n);
  rolltui_str_free(&out);
}
static void bindings_domain_to_json_with_origin(const RolltuiPresetDomain* d, const void* v, const char* name, size_t len,
                 RolltuiPutFn put,
                                                void* ctx) {
  Buf text = {NULL, 0, 0};
  RolltuiStr err = {0}, out = {0};
  RolltuiJsonValue* tree;
  bindings_domain_to_json(d, v, name, len, buf_put, &text);
  tree = rolltui_json_parse(text.p, text.len, &err);
  if (tree) {
    rolltui_json_set(tree, K("preset"), rolltui_json_string(name, len));
    rolltui_json_dump(tree, 2, &out);
    rolltui_str_append(&out, "\n", 1);
    put(ctx, out.p, out.n);
    rolltui_json_free(tree);
  } else {
    put(ctx, text.p, text.len); /* rolltui_bindings_dump_json always produces valid JSON */
  }
  buf_free(&text);
  rolltui_str_free(&out);
  rolltui_str_free(&err);
}

static void* bindings_domain_clone(const void* v) { return rolltui_bindings_clone((const RolltuiBindings*)v); }
static void bindings_domain_destroy(void* v) { rolltui_bindings_free((RolltuiBindings*)v); }
static int bindings_domain_equal(const void* a, const void* b) {
  return rolltui_bindings_equal((const RolltuiBindings*)a, (const RolltuiBindings*)b);
}

void rolltui_bindings_preset_domain_init(RolltuiPresetDomain* out,
                                        RolltuiScopeFn is_library_scope, void* scope_ctx,
                                         RolltuiReasonFn reason,
                                         void* reason_ctx) {
  memset(out, 0, sizeof *out);
  out->bindings_is_library_scope = is_library_scope;
  out->bindings_scope_ctx = scope_ctx;
  out->bindings_reason = reason;
  out->bindings_reason_ctx = reason_ctx;
  out->kind = "bindings";
  out->kind_len = sizeof("bindings") - 1;
  out->working_file = "bindings.working.json";
  out->working_file_len = sizeof("bindings.working.json") - 1;
  out->subdir = "bindings";
  out->subdir_len = sizeof("bindings") - 1;
  out->shipped_count = bindings_domain_shipped_count;
  out->shipped_at = bindings_domain_shipped_at;
  out->parse = bindings_domain_parse;
  out->parse_partial = NULL; /* a bindings file is always whole (BindingsDomain's own rule) */
  out->to_json = bindings_domain_to_json;
  out->to_json_with_origin = bindings_domain_to_json_with_origin;
  out->origin_of = generic_preset_origin_of;
  out->clone = bindings_domain_clone;
  out->destroy = bindings_domain_destroy;
  out->equal = bindings_domain_equal;
  out->report = &kBindingsPresetReportFns;
}

/* ---- the library's own three domains (Phase 18 m3; the case is at the header) ------------ */
RolltuiPresetDomain* rolltui_preset_domain(RolltuiContext* c, RolltuiPresetDomainId id) {
  const size_t i = (size_t)id;
  RolltuiPresetDomains* p;
  if (c == NULL || i >= 3) return NULL;
  p = domains_of(c);
  if (!p->built[i]) {
    switch (id) {
      case ROLLTUI_PRESET_DOMAIN_THEME:
        rolltui_theme_preset_domain_init(&p->d[i], rolltui_theme_default_vocab(),
                                         rolltui_theme_mode_setting_valid, rolltui_color_depth_setting_valid);
        break;
      case ROLLTUI_PRESET_DOMAIN_LAYOUT: {
        size_t n = 0;
        const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(c, &n);
        rolltui_layout_preset_domain_init(&p->d[i], rolltui_layout_default_hooks(), defaults, n);
        break;
      }
      case ROLLTUI_PRESET_DOMAIN_BINDINGS:
        rolltui_bindings_preset_domain_init(&p->d[i], rolltui_bindings_library_scope, NULL,
                                            rolltui_undeliverable_reason_fn, NULL);
        break;
    }
    p->built[i] = 1;
  }
  return &p->d[i];
}

/* ---- settings and precedence (rolltui_presets.h has the why) --------------------------------
 * No allocation anywhere below: every string is a literal with static storage duration, and
 * every lookup is a linear scan over a table of four or five rows. */

typedef struct {
  const char* s;
  size_t len;
} NameLen;

#define ROLLTUI_NAMED(n) {n, sizeof(n) - 1}
static const NameLen kRungNames[] = {
    ROLLTUI_NAMED("flag"),
    ROLLTUI_NAMED("environment"),
    ROLLTUI_NAMED("working copy"),
    ROLLTUI_NAMED("built-in default"),
};
#undef ROLLTUI_NAMED

const char* rolltui_preset_rung_name(RolltuiPresetRung r, size_t* len) {
  const size_t i = (size_t)r;
  if (i >= sizeof kRungNames / sizeof kRungNames[0]) {
    *len = 0;
    return "";
  }
  *len = kRungNames[i].len;
  return kRungNames[i].s;
}

void rolltui_preset_resolve_setting(const char* flag, size_t flag_len, const char* env, size_t env_len,
                                    const char* working, size_t working_len, const char* builtin,
                                    size_t builtin_len, const char** out_value, size_t* out_value_len,
                                    RolltuiPresetRung* out_rung) {
  if (flag_len) {
    *out_value = flag;
    *out_value_len = flag_len;
    *out_rung = ROLLTUI_PRESET_RUNG_FLAG;
    return;
  }
  if (env_len) {
    *out_value = env;
    *out_value_len = env_len;
    *out_rung = ROLLTUI_PRESET_RUNG_ENV;
    return;
  }
  if (working_len) {
    *out_value = working;
    *out_value_len = working_len;
    *out_rung = ROLLTUI_PRESET_RUNG_WORKING;
    return;
  }
  *out_value = builtin;
  *out_value_len = builtin_len;
  *out_rung = ROLLTUI_PRESET_RUNG_BUILTIN;
}

#define ROLLTUI_NAMED(n) {n, sizeof(n) - 1}
static const NameLen kDomainNames[] = {
    ROLLTUI_NAMED("theme"),
    ROLLTUI_NAMED("layout"),
    ROLLTUI_NAMED("bindings"),
};
#undef ROLLTUI_NAMED

const char* rolltui_preset_domain_name(RolltuiPresetDomainId d, size_t* len) {
  const size_t i = (size_t)d;
  if (i >= sizeof kDomainNames / sizeof kDomainNames[0]) {
    *len = 0;
    return "";
  }
  *len = kDomainNames[i].len;
  return kDomainNames[i].s;
}

/* `kSettings` (Presets.hpp), verbatim: "theme"/"layout"/"theme_mode"/"color_depth"/"bindings"
 * in listing order. `Presets.cpp`'s own `kSettings` is BUILT FROM this table row for row
 * rather than holding a second copy of the same five rows — "a vocabulary written down twice
 * is a second thing to drift", the rule this port has followed everywhere else in the file. */
#define ROLLTUI_SETTING(k, d, e, b, v) {k, sizeof(k) - 1, d, e, sizeof(e) - 1, b, sizeof(b) - 1, v, sizeof(v) - 1}
static const RolltuiPresetSettingSpec kSettingTable[] = {
    ROLLTUI_SETTING("theme", ROLLTUI_PRESET_DOMAIN_THEME, "THEME", "default",
                    "a preset name (see `theme list`) or a preset file"),
    ROLLTUI_SETTING("layout", ROLLTUI_PRESET_DOMAIN_LAYOUT, "LAYOUT", "default",
                    "a shipped layout, a layouts/ file name, or a layout file"),
    ROLLTUI_SETTING("theme_mode", ROLLTUI_PRESET_DOMAIN_THEME, "THEME_MODE", "auto", "auto | dark | light"),
    ROLLTUI_SETTING("color_depth", ROLLTUI_PRESET_DOMAIN_THEME, "COLOR_DEPTH", "auto",
                    "auto | truecolor | 256 | 16 | mono"),
    ROLLTUI_SETTING("bindings", ROLLTUI_PRESET_DOMAIN_BINDINGS, "BINDINGS", "default",
                    "a bindings preset name (see `bindings list`) or a bindings file"),
};
#undef ROLLTUI_SETTING
#define ROLLTUI_SETTINGS_COUNT (sizeof kSettingTable / sizeof kSettingTable[0])

size_t rolltui_preset_settings_count(void) { return ROLLTUI_SETTINGS_COUNT; }

const RolltuiPresetSettingSpec* rolltui_preset_settings_at(size_t i) { return &kSettingTable[i]; }

int rolltui_preset_setting_index(const char* key, size_t len) {
  size_t i;
  for (i = 0; i < ROLLTUI_SETTINGS_COUNT; ++i)
    if (kSettingTable[i].key_len == len && (len == 0 || memcmp(kSettingTable[i].key, key, len) == 0)) return (int)i;
  return -1;
}

/* ---- the save-as sentences ------------------------------------------------------------------
 * Indexed by ROLLTUI_SAVE_*; `rolltui::to_string(SaveResult)`'s four, moved with them. */
static const char* const kSaveResultText[] = {
    "saved",
    "refused: a shipped preset is read-only",
    "a preset with that name exists; confirm to overwrite",
    "not a preset name (letters, digits, - _ . ; not starting with a dot)",
    "write failed",
};

const char* rolltui_preset_save_result_text(int result, size_t* len) {
  const size_t n = sizeof kSaveResultText / sizeof kSaveResultText[0];
  const char* s = (result >= 0 && (size_t)result < n) ? kSaveResultText[result] : "";
  if (len) *len = strlen(s);
  return s;
}

/* ---- the store's one-line problem sentence ----------------------------------------------- */

static void sum_add(RolltuiStr* out, size_t* started, const char* prefix, size_t plen, const char* text,
                    size_t tlen) {
  if (*started) rolltui_str_append(out, "; ", 2);
  *started = 1;
  rolltui_str_append(out, prefix, plen);
  rolltui_str_append(out, text, tlen);
}
#define SUM_ADD(out, started, lit, text, tlen) sum_add((out), (started), (lit), sizeof(lit) - 1, (text), (tlen))

void rolltui_preset_report_summary(const RolltuiStr* error, const RolltuiStr* bad_values, size_t bad_values_n,
                                   const RolltuiStr* unknown_keys, size_t unknown_keys_n,
                                   const RolltuiThemeReport* colours, const RolltuiLayoutReport* layout,
                                   const RolltuiBindingsReport* bindings, RolltuiStr* out) {
  size_t started = 0, i;
  if (!out) return;
  /* An error is the whole sentence: nothing else loaded, so nothing else has anything to say. */
  if (error && error->n) { rolltui_str_append(out, error->p, error->n); return; }
  for (i = 0; i < bad_values_n; ++i) SUM_ADD(out, &started, "bad: ", bad_values[i].p, bad_values[i].n);
  for (i = 0; i < unknown_keys_n; ++i) SUM_ADD(out, &started, "unknown: ", unknown_keys[i].p, unknown_keys[i].n);
  if (colours) {
    if (colours->error.n) SUM_ADD(out, &started, "colours: ", colours->error.p, colours->error.n);
    if (colours->missing_roles_n) {
      /* The one COUNTED part: naming forty missing roles one by one would bury the rest. */
      char buf[64];
      const int k = snprintf(buf, sizeof buf, "%zu roles missing (inherit text)", colours->missing_roles_n);
      if (k > 0) SUM_ADD(out, &started, "colours: ", buf, (size_t)k);
    }
    for (i = 0; i < colours->bad_values_n; ++i)
      SUM_ADD(out, &started, "colours: ", colours->bad_values[i].p, colours->bad_values[i].n);
    for (i = 0; i < colours->unknown_keys_n; ++i)
      SUM_ADD(out, &started, "colours: unknown ", colours->unknown_keys[i].p, colours->unknown_keys[i].n);
  }
  if (layout) {
    if (layout->error.n) SUM_ADD(out, &started, "layout: ", layout->error.p, layout->error.n);
    for (i = 0; i < layout->bad_values_n; ++i)
      SUM_ADD(out, &started, "layout: ", layout->bad_values[i].p, layout->bad_values[i].n);
    for (i = 0; i < layout->unknown_keys_n; ++i)
      SUM_ADD(out, &started, "layout: unknown ", layout->unknown_keys[i].p, layout->unknown_keys[i].n);
  }
  if (bindings && !rolltui_bindings_report_clean(bindings)) {
    RolltuiStr b;
    memset(&b, 0, sizeof b);
    rolltui_bindings_report_summary(bindings, &b);
    SUM_ADD(out, &started, "bindings: ", b.p ? b.p : "", b.n);
    rolltui_str_free(&b);
  }
}

/* ---- the working copy's label ------------------------------------------------------------ */
void rolltui_preset_store_label(const RolltuiPresetStore* s, RolltuiStr* out) {
  size_t len = 0;
  const char* origin;
  if (!out) return;
  rolltui_str_clear(out); /* REPLACES; the buffer is kept for the next frame */
  origin = rolltui_preset_store_origin(s, &len);
  rolltui_str_append(out, origin, len);
  if (rolltui_preset_store_modified(s)) rolltui_str_append(out, " (modified)", 11);
}

/* ============================================================================================
 * A SETTING'S VALUE IN THE WORKING COPY. See the header for why
 * `working_value`'s stated blocker was already false when it was written down.
 * ============================================================================================ */

void rolltui_preset_working_value(const RolltuiPresetStore* s, const char* key, size_t key_len, RolltuiStr* out) {
  if (!s) return;
  /* The IDENTITY key — the one whose value IS the preset name — is the domain's own name,
   * which the store carries as its domain's `kind`. One rule for all three domains, which is
   * what stopped it being three string literals; one SOURCE for the name, which
   * is what stopped every caller carrying an id in step with the store. */
  if (key_len == s->d->kind_len && memcmp(key, s->d->kind, key_len) == 0) {
    size_t on = 0;
    const char* o = rolltui_preset_store_origin(s, &on);
    rolltui_str_append(out, o ? o : "", on);
    return;
  }
  if (!(s->d->kind_len == 5 && memcmp(s->d->kind, "theme", 5) == 0)) return; /* only Theme has non-identity settings */
  {
    /* Read in place, under the lock. Until 2026-09-06 this CLONED the whole working theme — a
     * JSON tree — to read one short string, then freed it: a copy that existed for no reason. */
    const RolltuiThemePresetValue* w;
    pthread_mutex_lock((pthread_mutex_t*)&s->mu);
    w = (const RolltuiThemePresetValue*)s->working;
    if (w != NULL) {
      if (key_len == 10 && memcmp(key, "theme_mode", 10) == 0)
        rolltui_str_append(out, w->mode.p ? w->mode.p : "", w->mode.n);
      else if (key_len == 11 && memcmp(key, "color_depth", 11) == 0)
        rolltui_str_append(out, w->depth.p ? w->depth.p : "", w->depth.n);
    }
    pthread_mutex_unlock((pthread_mutex_t*)&s->mu);
  }
}

/* ---- per-domain report judgement — see the header for why these are the
 * library's and the store wrappers around them are not. --------------------------------------- */

int rolltui_theme_preset_report_clean(const RolltuiThemePresetReport* r) {
  if (!r) return 1;
  return r->error.n == 0 && r->bad_values_n == 0 && r->unknown_keys_n == 0 && r->colours.error.n == 0 &&
         r->colours.missing_roles_n == 0 && r->colours.unknown_keys_n == 0 && r->colours.bad_values_n == 0;
}

void rolltui_theme_preset_report_summary(const RolltuiThemePresetReport* r, RolltuiStr* out) {
  if (!r) return;
  rolltui_preset_report_summary(&r->error, r->bad_values, r->bad_values_n, r->unknown_keys, r->unknown_keys_n,
                                &r->colours, NULL, NULL, out);
}

int rolltui_layout_preset_report_clean(const RolltuiLayoutPresetReport* r) {
  if (!r) return 1;
  return r->error.n == 0 && rolltui_layout_report_clean(&r->layout) != 0;
}

void rolltui_layout_preset_report_summary(const RolltuiLayoutPresetReport* r, RolltuiStr* out) {
  if (!r) return;
  rolltui_preset_report_summary(&r->error, NULL, 0, NULL, 0, NULL, &r->layout, NULL, out);
}

int rolltui_bindings_preset_report_clean(const RolltuiBindingsPresetReport* r) {
  if (!r) return 1;
  return r->error.n == 0 && r->unknown_keys_n == 0 && rolltui_bindings_report_clean(&r->bindings) != 0;
}

void rolltui_bindings_preset_report_summary(const RolltuiBindingsPresetReport* r, RolltuiStr* out) {
  if (!r) return;
  rolltui_preset_report_summary(&r->error, NULL, 0, r->unknown_keys, r->unknown_keys_n, NULL, NULL, &r->bindings, out);
}
