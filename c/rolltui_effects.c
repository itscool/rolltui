/* rolltui/c/rolltui_effects.c — the C side of the effects engine. See rolltui_effects.h for
 * the boundary's rules and rolltui/Effects.hpp for the effects rules themselves;
 * `EffectsCpp.cpp` is the other implementation of the same eleven functions, and
 * `rolltui/tests/effects_test.cpp` — which asserts the two properties over EVERY registered
 * kind, built-in and host's, including one that deliberately lies — is the oracle for both.
 *
 * **THIS FILE HAS PROCESS-WIDE STATE, and it is the first ported module that does.** The
 * frame, the wrap engine and the Unicode algorithms hold nothing between calls by design;
 * a kind registry is state by definition. So this is where the port meets
 * `rolltui::shutdown()` for the first time, and the promise it has to keep is a NUMBER:
 * after shutdown, `rolltui::mem::stats().live_bytes == 0`. Three things are retained per
 * host kind — the table slot, a COPY of the name, and the host's context — and all three
 * are released with the CONTEXT that holds them: `rolltui_context_free` releases
 * the registry by name, so the number is a session's fact and not only the process's.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`. */
#include "rolltui/c/rolltui_effects.h"

#include <math.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_terminal.h"
#include "testkit/testctl.h"

/* ---- working memory ---------------------------------------------------------------------- */

/* What one spec's kind resolved to, for one mark. `builtin` is an index into the closed
 * table below, or -1 for a host kind; both -1 and a NULL `fn` mean "nothing answers". */
typedef struct {
  int builtin;
  RolltuiEffectFn fn;
  void* ctx;
} Resolved;

struct RolltuiEffectScratch {
  /* Created lazily: a frame with nothing marked never measures a glyph, and roll is in
   * that state almost always. */
  RolltuiUnicodeScratch* uni;
  /* GROWING, AMORTISED (rolltui_alloc.h strategy 2), one buffer per ROLE. `gs` is where a
   * glyph kind decodes its own frame to find the cell it is answering for; `res` is the
   * per-mark kind resolution, which is loop-invariant over the mark's cells and must not
   * be recomputed inside a per-cell draw loop. */
  RolltuiUnicodeGrapheme* gs;
  size_t gs_cap;
  Resolved* res;
  size_t res_cap;
};

RolltuiEffectScratch* rolltui_effect_scratch_new(void) {
  RolltuiEffectScratch* s = (RolltuiEffectScratch*)rolltui_mem_alloc(sizeof(RolltuiEffectScratch));
  memset(s, 0, sizeof *s);
  return s;
}

void rolltui_effect_scratch_free(RolltuiEffectScratch* s) {
  if (!s) return;
  rolltui_u_scratch_free(s->uni);
  rolltui_mem_free(s->gs);
  rolltui_mem_free(s->res);
  rolltui_mem_free(s);
}

static RolltuiUnicodeScratch* uni(RolltuiEffectScratch* s) {
  if (!s->uni) s->uni = rolltui_u_scratch_new();
  return s->uni;
}

/* ---- small shared rules ------------------------------------------------------------------ */

static double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static int name_eq(const char* a, size_t alen, const char* b, size_t blen) {
  return alen == blen && (alen == 0 || memcmp(a, b, alen) == 0);
}

/* Which of `steps` pictures of one period `elapsed` falls in. A still spec (period 0) is
 * always its first picture — which is what makes "a theme that maps nothing moving" a
 * legible still frame rather than a blank one. */
/* THE WOBBLE, and why it is shaped this way.
 *
 * `jitter` varies a sweep's SPEED from pass to pass without moving the pass boundaries: each
 * pass still takes exactly `period_ms`, and within it the sweep eases forward or hangs back by
 * an amount that differs every time. Warping the phase rather than the period is what keeps
 * this O(1) — a per-pass period would make "which pass is it" a walk from zero.
 *
 * The warp is `p + a*p*(1-p)`, a parabola that is ZERO AT BOTH ENDS, so a pass begins and ends
 * exactly where it would have. `a` comes from hashing the pass number, so it is deterministic:
 * the same tick always draws the same cell, which is what `--tick N` and every golden frame
 * depend on. A real RNG here would take the golden-frame harness with it.
 *
 * Integer arithmetic throughout: no libm, and no float in a per-cell path. */
static unsigned int pass_mix(unsigned long long pass) {
  unsigned int h = (unsigned int)(pass ^ (pass >> 32));
  h *= 2654435761u; /* Knuth's multiplicative hash */
  h ^= h >> 15;
  return h;
}

static int step_index(const RolltuiEffectSpec* s, unsigned long long elapsed, int steps) {
  if (steps <= 1 || s->period_ms <= 0) return 0;
  const unsigned long long period = (unsigned long long)s->period_ms;
  unsigned long long phase = elapsed % period;
  if (s->jitter > 0) {
    /* p and the warp in PERMILLE, so the whole thing stays in integers. */
    const long long p = (long long)(phase * 1000u / period);
    const long long bulge = p * (1000 - p) / 1000; /* 0 at both ends, 250 at the middle */
    /* `a` in [-jitter, +jitter] percent, fixed for this pass. */
    const long long a = (long long)(pass_mix(elapsed / period) % 201u) - 100; /* -100..100 */
    long long warped = p + (bulge * a * (long long)s->jitter) / 10000;
    if (warped < 0) warped = 0;
    if (warped > 999) warped = 999;
    phase = (unsigned long long)warped * period / 1000u;
  }
  int i = (int)(phase * (unsigned long long)steps / period);
  if (i >= steps) i = steps - 1;
  return s->backward ? steps - 1 - i : i;
}

/* `role_count` is never zero (rolltui_effects.h), so there is no fallback role to name
 * here — which is what keeps the styling vocabulary in one language. */
static RolltuiStyle role_style(const RolltuiStyle* styles, const RolltuiEffectSpec* s, size_t i) {
  return styles[s->roles[i % s->role_count]];
}

/* The shimmer's sweeping window, in cells. */
static int sweep_width(const RolltuiEffectSpec* s, int length) {
  if (s->width > 0) return s->width;
  const int third = length / 3;
  return third > 1 ? third : 1;
}

static int frame_width(RolltuiEffectScratch* sc, const char* frame, size_t len, int amb) {
  return rolltui_u_display_width(uni(sc), frame, len, amb);
}

/* The `col`-th CELL of one frame string, when the frame is more than one cell wide (an
 * ellipsis's ".. "). Returns the cluster's byte length into `*out`, or 0 when the column is
 * past the frame's end or lands on the second half of a wide glyph. */
static size_t frame_cell(RolltuiEffectScratch* sc, const char* frame, size_t len, int col, int amb,
                         const char** out) {
  if (len == 0) return 0;
  sc->gs = rolltui_grow(sc->gs, &sc->gs_cap, len, sizeof *sc->gs); /* no more clusters than bytes */
  const size_t n = rolltui_u_graphemes(uni(sc), frame, len, amb, sc->gs);
  int at = 0;
  for (size_t i = 0; i < n; ++i) {
    if (sc->gs[i].width <= 0) continue;
    if (at == col) {
      *out = frame + sc->gs[i].offset;
      return sc->gs[i].length;
    }
    at += sc->gs[i].width;
    if (at > col) return 0; /* the column is the second half of a wide glyph: leave it */
  }
  return 0;
}

static void set_glyph(RolltuiEffectOut* out, const char* g, size_t len) {
  out->has_glyph = 1;
  out->glyph_len = len;
  if (len > 0 && len <= ROLLTUI_EFFECT_GLYPH_MAX) memcpy(out->glyph, g, len);
}

/* ---- rung 1: the built-in kinds ---------------------------------------------------------- */
/* Each one is the whole of its rule. None of them names a colour: they pick between the base
 * style and a role the theme named (Effects.hpp, "what an effect may not do"). */

static void glyph_cycle(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiEffectCell* in,
                        RolltuiEffectOut* out, int at_end) {
  if (s->frame_count == 0) return;
  const RolltuiEffectFrame first = s->frames[0];
  const int w = frame_width(sc, first.bytes, first.len, in->ambiguous_wide);
  if (w <= 0 || w > in->length) return;
  const int start = at_end ? in->length - w : 0;
  if (in->index < start || in->index >= start + w) return;
  const RolltuiEffectFrame frame = s->frames[step_index(s, in->elapsed_ms, (int)s->frame_count)];
  const char* g = NULL;
  const size_t glen = frame_cell(sc, frame.bytes, frame.len, in->index - start, in->ambiguous_wide, &g);
  if (glen == 0) return;
  set_glyph(out, g, glen);
}

static void kind_spinner(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                         const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)styles;
  glyph_cycle(sc, s, in, out, 0);
}

static void kind_ellipsis(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                          const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)styles;
  glyph_cycle(sc, s, in, out, 1);
}

static void kind_bar(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                     const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)sc;
  const int filled = (int)lround(clamp01(in->fraction) * in->length);
  /* ON = the fill boundary off by one: the cell AT `filled` is coloured too, so a bar drawn
   * for a fraction reads one cell fuller than the number it is a picture of. Every cell is
   * still a legal cell and the span is still the right width - the only symptom is that the
   * bar and the number disagree, which nothing downstream can notice. */
  if (testkit_ctl_on("effects.bar_fills_one_cell_too_many")) {
    if (in->index > filled) return;
  } else if (in->index >= filled) {
    return;
  }
  out->has_style = 1;
  out->style = role_style(styles, s, 0);
}

static void kind_pulse(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                       const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)sc;
  const int n = s->role_count > 1 ? (int)s->role_count : 1;
  out->has_style = 1;
  out->style = role_style(styles, s, (size_t)step_index(s, in->elapsed_ms, n));
}

static void kind_blink(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                       const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)sc;
  if (step_index(s, in->elapsed_ms, 2) != 0) return; /* the base half: the one kind that shows it */
  out->has_style = 1;
  out->style = role_style(styles, s, 0);
}

static void kind_shimmer(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                         const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)sc;
  const int w = sweep_width(s, in->length);
  const int pos = step_index(s, in->elapsed_ms, in->length + w);
  if (in->index > pos || in->index <= pos - w) return;
  out->has_style = 1;
  out->style = role_style(styles, s, 0);
}

static void kind_gradient(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiStyle* styles,
                          const RolltuiEffectCell* in, RolltuiEffectOut* out) {
  (void)sc;
  const int n = s->role_count > 1 ? (int)s->role_count : 1;
  const int band = in->length > 0 ? in->index * n / in->length : 0;
  out->has_style = 1;
  out->style = role_style(styles, s, (size_t)(band + step_index(s, in->elapsed_ms, n)));
}

typedef void (*BuiltinFn)(RolltuiEffectScratch*, const RolltuiEffectSpec*, const RolltuiStyle*,
                          const RolltuiEffectCell*, RolltuiEffectOut*);
typedef struct {
  const char* name;
  size_t len;
  BuiltinFn fn;
} Builtin;
#define ROLLTUI_BUILTIN(n, f) {n, sizeof(n) - 1, f}
static const Builtin kBuiltins[] = {
    ROLLTUI_BUILTIN("spinner", kind_spinner), ROLLTUI_BUILTIN("ellipsis", kind_ellipsis),
    ROLLTUI_BUILTIN("bar", kind_bar),         ROLLTUI_BUILTIN("pulse", kind_pulse),
    ROLLTUI_BUILTIN("shimmer", kind_shimmer), ROLLTUI_BUILTIN("gradient", kind_gradient),
    ROLLTUI_BUILTIN("blink", kind_blink),
};
#undef ROLLTUI_BUILTIN
#define ROLLTUI_BUILTIN_COUNT (sizeof kBuiltins / sizeof kBuiltins[0])

static int builtin_index(const char* name, size_t len) {
  for (size_t i = 0; i < ROLLTUI_BUILTIN_COUNT; ++i)
    if (name_eq(kBuiltins[i].name, kBuiltins[i].len, name, len)) return (int)i;
  return -1;
}

int rolltui_effect_is_builtin(const char* name, size_t len) { return builtin_index(name, len) >= 0; }

static int spec_kind_is(const RolltuiEffectSpec* s, const char* name, size_t len) {
  return name_eq(s->kind, s->kind_len, name, len);
}

/* ---- rung 2: the kinds a HOST registered ------------------------------------------------- */
/* RUNG 2 IS A CONTEXT'S, NOT THE PROCESS'S — the same move, for the same reason,
 * as the widget kinds in `rolltui_layout.c`. Three owned things per entry: the slot, a copy of
 * the name, and the host's own context pointer.
 *
 * THE MUTEX WENT WITH THE GLOBAL, and that is a consequence of the contract rather than a
 * relaxation of it. It existed because the table was process-wide and two threads could reach
 * it; a context is entered by ONE THREAD AT A TIME (contract point 1), so the lock guarded
 * nothing a caller was still allowed to do. What it cost was real: the applier took it once
 * per MARK, inside the per-frame draw path, to read a table nothing had written since startup. */

typedef struct {
  char* name; /* OWNED: a copy, because the caller's may not outlive the registration */
  size_t name_len;
  RolltuiEffectFn fn;
  void* ctx;               /* OWNED, released with `free_ctx` */
  void (*free_ctx)(void*); /* may be NULL: a context with no lifetime of its own */
} HostKind;

struct RolltuiEffectRegistry {
  HostKind* v;
  size_t n, cap;
};

RolltuiEffectRegistry* rolltui_effect_registry_new(void) {
  RolltuiEffectRegistry* r = (RolltuiEffectRegistry*)rolltui_mem_alloc(sizeof *r);
  memset(r, 0, sizeof *r);
  return r;
}

void rolltui_effect_registry_free(RolltuiEffectRegistry* r) {
  size_t i;
  if (r == NULL) return;
  for (i = 0; i < r->n; ++i) {
    rolltui_mem_free(r->v[i].name);
    if (r->v[i].free_ctx) r->v[i].free_ctx(r->v[i].ctx);
  }
  rolltui_mem_free(r->v);
  rolltui_mem_free(r);
}

/* A NULL CONTEXT IS A CONTEXT WITH NO HOST KINDS — one rule, the same one
 * `rolltui_content_parse` states, so rung 1 answers everywhere and rung 2 is simply empty. */
static size_t host_n(const RolltuiContext* c) { return (c && c->effects) ? c->effects->n : 0; }
static const HostKind* host_v(const RolltuiContext* c) { return (c && c->effects) ? c->effects->v : NULL; }

int rolltui_effect_register(RolltuiContext* c, const char* name, size_t name_len, RolltuiEffectFn fn, void* ctx,
                            void (*free_ctx)(void*)) {
  RolltuiEffectRegistry* r;
  HostKind* k;
  if (!c) return ROLLTUI_EFFECT_NO_NAME;
  if (name_len == 0) return ROLLTUI_EFFECT_NO_NAME;
  if (!fn) return ROLLTUI_EFFECT_NO_FN;
  /* Rung 1 is never shadowed — the same guard, for the same reason, as a widget kind. */
  if (rolltui_effect_is_builtin(name, name_len)) return ROLLTUI_EFFECT_IS_BUILTIN;
  for (size_t i = 0; i < host_n(c); ++i)
    if (name_eq(c->effects->v[i].name, c->effects->v[i].name_len, name, name_len)) return ROLLTUI_EFFECT_DUPLICATE;
  if (c->effects == NULL) c->effects = rolltui_effect_registry_new();
  r = c->effects;
  /* NO RELEASER TO REGISTER: the storage belongs to the context and `rolltui_context_free`
   * releases it by name, which is what makes the set of owned things readable in one place
   * instead of discovered by following `rolltui_on_shutdown` calls. */
  r->v = rolltui_grow_zeroed(r->v, &r->cap, r->n + 1, sizeof *r->v);
  k = &r->v[r->n];
  k->name = (char*)rolltui_mem_alloc(name_len);
  memcpy(k->name, name, name_len);
  k->name_len = name_len;
  k->fn = fn;
  k->ctx = ctx;
  k->free_ctx = free_ctx;
  ++r->n;
  return ROLLTUI_EFFECT_OK;
}

void rolltui_effect_clear_registered(RolltuiContext* c) {
  if (!c) return;
  rolltui_effect_registry_free(c->effects);
  c->effects = NULL;
}

size_t rolltui_effect_kind_count(const RolltuiContext* c) { return ROLLTUI_BUILTIN_COUNT + host_n(c); }

const char* rolltui_effect_kind_name(const RolltuiContext* c, size_t i, size_t* len) {
  if (i < ROLLTUI_BUILTIN_COUNT) {
    *len = kBuiltins[i].len;
    return kBuiltins[i].name;
  }
  i -= ROLLTUI_BUILTIN_COUNT;
  if (i < host_n(c)) {
    *len = host_v(c)[i].name_len;
    return host_v(c)[i].name;
  }
  *len = 0;
  return "";
}

/* Rung 1 first and never shadowed, then the host's — the resolution order the whole
 * library uses for a widget kind, a menu file and an effect kind alike. */
static int resolve(const RolltuiContext* c, const char* name, size_t len, Resolved* out) {
  out->builtin = builtin_index(name, len);
  out->fn = NULL;
  out->ctx = NULL;
  if (out->builtin >= 0) return 1;
  for (size_t i = 0; i < host_n(c); ++i)
    if (name_eq(host_v(c)[i].name, host_v(c)[i].name_len, name, len)) {
      out->fn = host_v(c)[i].fn;
      out->ctx = host_v(c)[i].ctx;
      return 1;
    }
  return 0;
}

int rolltui_effect_kind_resolves(const RolltuiContext* c, const char* name, size_t len) {
  Resolved r;
  if (builtin_index(name, len) >= 0) return 1;
  return resolve(c, name, len, &r);
}

/* ---- rung 2 for STATES: the states a HOST registered ------------------------------------- */
/* A STATE is what a widget marks a span with. The library's six are named for a transcript
 * (waiting, streaming, …); a host whose widget has states of its own — a file browser's cursor
 * on a folder, on a file, a column just dug into — registers them here BY NAME, and a theme file
 * then maps them exactly as it maps the library's, under the same `effects` key. Two rungs, one
 * order: the library's six first and never shadowed, then the host's in registration order —
 * the rule a widget kind and an effect kind already follow. The index a registration hands back
 * is what the widget marks with, and it is stable for the life of the context.
 *
 * THE VOCABULARY IS REBUILT AT REGISTRATION, NOT AT READ. `rolltui_theme_vocab` sits on the
 * theme-load path and a getter that allocates is a getter a caller cannot reason about;
 * registration is a handful of calls at start-up. Each host name is stored NUL-terminated
 * because the vocabulary's names are read with strlen, as the library's six literals are. */
typedef struct {
  char* name; /* OWNED, NUL-terminated */
  size_t name_len;
} HostState;

struct RolltuiEffectStates {
  HostState* v;
  size_t n, cap;
  const char** names; /* OWNED array of BORROWS: the six literals, then each host copy */
  RolltuiThemeVocab vocab;
};

RolltuiEffectStates* rolltui_effect_states_new(void) {
  RolltuiEffectStates* s = (RolltuiEffectStates*)rolltui_mem_alloc(sizeof *s);
  memset(s, 0, sizeof *s);
  s->vocab = *rolltui_theme_default_vocab();
  return s;
}

void rolltui_effect_states_free(RolltuiEffectStates* s) {
  size_t i;
  if (s == NULL) return;
  for (i = 0; i < s->n; ++i) rolltui_mem_free(s->v[i].name);
  rolltui_mem_free(s->v);
  rolltui_mem_free(s->names);
  rolltui_mem_free(s);
}

static void rebuild_vocab(RolltuiEffectStates* s) {
  const RolltuiThemeVocab* d = rolltui_theme_default_vocab();
  size_t i;
  rolltui_mem_free(s->names);
  s->names = (const char**)rolltui_mem_alloc((d->state_count + s->n) * sizeof *s->names);
  for (i = 0; i < d->state_count; ++i) s->names[i] = d->state_names[i];
  for (i = 0; i < s->n; ++i) s->names[d->state_count + i] = s->v[i].name;
  s->vocab = *d;
  s->vocab.state_names = s->names;
  s->vocab.state_count = d->state_count + s->n;
}

int rolltui_effect_state_register(RolltuiContext* c, const char* name, size_t name_len, int* out_state) {
  RolltuiEffectStates* s;
  HostState* h;
  size_t i;
  if (out_state) *out_state = -1;
  if (!c || name_len == 0) return ROLLTUI_EFFECT_NO_NAME;
  /* Rung 1 is never shadowed — and "none" is rung 1's too. */
  if (rolltui_effect_state_from_name(name, name_len) >= 0) return ROLLTUI_EFFECT_IS_BUILTIN;
  if (c->states)
    for (i = 0; i < c->states->n; ++i)
      if (name_eq(c->states->v[i].name, c->states->v[i].name_len, name, name_len)) {
        if (out_state) *out_state = (int)(ROLLTUI_EFFECT_STATE_COUNT + i); /* the row it already has */
        return ROLLTUI_EFFECT_DUPLICATE;
      }
  if (c->states == NULL) c->states = rolltui_effect_states_new();
  s = c->states;
  s->v = rolltui_grow_zeroed(s->v, &s->cap, s->n + 1, sizeof *s->v);
  h = &s->v[s->n];
  h->name = (char*)rolltui_mem_alloc(name_len + 1);
  memcpy(h->name, name, name_len);
  h->name[name_len] = '\0';
  h->name_len = name_len;
  ++s->n;
  rebuild_vocab(s);
  if (out_state) *out_state = (int)(ROLLTUI_EFFECT_STATE_COUNT + s->n - 1);
  return ROLLTUI_EFFECT_OK;
}

int rolltui_effect_state_resolve(const RolltuiContext* c, const char* name, size_t len) {
  const int lib = rolltui_effect_state_from_name(name, len);
  size_t i;
  if (lib >= 0) return lib;
  if (c && c->states)
    for (i = 0; i < c->states->n; ++i)
      if (name_eq(c->states->v[i].name, c->states->v[i].name_len, name, len)) return (int)(ROLLTUI_EFFECT_STATE_COUNT + i);
  return -1;
}

size_t rolltui_effect_state_count(const RolltuiContext* c) {
  return ROLLTUI_EFFECT_STATE_COUNT + ((c && c->states) ? c->states->n : 0);
}

const RolltuiThemeVocab* rolltui_theme_vocab(const RolltuiContext* c) {
  return (c && c->states) ? &c->states->vocab : rolltui_theme_default_vocab();
}

static void call_kind(RolltuiEffectScratch* sc, const Resolved* r, const RolltuiEffectSpec* spec,
                      const RolltuiStyle* styles, const void* host, const RolltuiEffectCell* in,
                      RolltuiEffectOut* out) {
  if (r->builtin >= 0)
    kBuiltins[r->builtin].fn(sc, spec, styles, in, out);
  else
    r->fn(r->ctx, spec, styles, host, in, out);
}

/* ---- what a THEME carries, owned in C ---------------------------------------------------- */
/* A MAP OWNS EVERY BYTE OF EVERY SPEC IT HOLDS (rolltui_effects.h): the
 * kind name, each frame, and the role list. Each of those is its OWN allocation, so growing
 * one does not move the others and a spec's pointers stay put — the array of specs itself is
 * the one thing that moves, which is exactly the window `rolltui_effect_map_at` states.
 *
 * PER-STATE ARRAYS rather than one flat array with ranges: a theme file names its states in
 * whatever order it likes, and an insert into the middle of a flat array is a whole rewrite
 * for nothing. Three parallel arrays sized `states`, allocated once. */
typedef struct {
  char* kind;
  size_t kind_len;
  RolltuiEffectFrame* frames; /* each `bytes` is its own allocation, freed one by one */
  size_t frame_count, frames_cap;
  unsigned char* roles; /* NULL while the spec has none of its own: `spec.roles` then
                         * points at the map's `fallback` and `role_count` is 1 */
  size_t role_count, roles_cap;
} SpecStore;

struct RolltuiEffectMap {
  size_t states;
  unsigned char fallback; /* the role a spec with none of its own picks */
  RolltuiEffectSpec** view;
  SpecStore** store;
  size_t* count;
  size_t* cap;
};

RolltuiEffectMap* rolltui_effect_map_new(size_t states, unsigned char fallback_role) {
  RolltuiEffectMap* m = (RolltuiEffectMap*)rolltui_mem_alloc(sizeof(RolltuiEffectMap));
  memset(m, 0, sizeof *m);
  m->states = states;
  m->fallback = fallback_role;
  if (states == 0) return m;
  /* GROWING, EXACT (rolltui_alloc.h strategy 3): the state count is known and never
   * changes, so these four are sized once and never resized. */
  m->view = (RolltuiEffectSpec**)rolltui_mem_alloc(states * sizeof *m->view);
  m->store = (SpecStore**)rolltui_mem_alloc(states * sizeof *m->store);
  m->count = (size_t*)rolltui_mem_alloc(states * sizeof *m->count);
  m->cap = (size_t*)rolltui_mem_alloc(states * sizeof *m->cap);
  memset(m->view, 0, states * sizeof *m->view);
  memset(m->store, 0, states * sizeof *m->store);
  memset(m->count, 0, states * sizeof *m->count);
  memset(m->cap, 0, states * sizeof *m->cap);
  return m;
}

/* Widens a map to `states` rows, keeping every spec it holds; a map already that wide is left
 * alone. What lets a map built from a theme that knows six states take a host's mapping for
 * its seventh: the four arrays are EXACT (never resized) by construction, so this is the one
 * place they move, and every borrowed `RolltuiEffectSpec*` is invalidated by it — the same
 * window `rolltui_effect_map_at` already states for any change to the map. */
int rolltui_effect_map_grow(RolltuiEffectMap* m, size_t states) {
  RolltuiEffectSpec** view;
  SpecStore** store;
  size_t *count, *cap;
  if (!m || states <= m->states) return 0;
  view = (RolltuiEffectSpec**)rolltui_mem_alloc(states * sizeof *view);
  store = (SpecStore**)rolltui_mem_alloc(states * sizeof *store);
  count = (size_t*)rolltui_mem_alloc(states * sizeof *count);
  cap = (size_t*)rolltui_mem_alloc(states * sizeof *cap);
  memset(view, 0, states * sizeof *view);
  memset(store, 0, states * sizeof *store);
  memset(count, 0, states * sizeof *count);
  memset(cap, 0, states * sizeof *cap);
  if (m->states) {
    memcpy(view, m->view, m->states * sizeof *view);
    memcpy(store, m->store, m->states * sizeof *store);
    memcpy(count, m->count, m->states * sizeof *count);
    memcpy(cap, m->cap, m->states * sizeof *cap);
  }
  rolltui_mem_free(m->view);
  rolltui_mem_free(m->store);
  rolltui_mem_free(m->count);
  rolltui_mem_free(m->cap);
  m->view = view;
  m->store = store;
  m->count = count;
  m->cap = cap;
  m->states = states;
  return 1;
}

static void spec_release(SpecStore* st) {
  size_t j;
  rolltui_mem_free(st->kind);
  for (j = 0; j < st->frame_count; ++j) rolltui_mem_free((void*)st->frames[j].bytes);
  rolltui_mem_free(st->frames);
  rolltui_mem_free(st->roles);
  memset(st, 0, sizeof *st);
}

void rolltui_effect_map_clear(RolltuiEffectMap* m) {
  size_t s, i;
  if (!m) return;
  for (s = 0; s < m->states; ++s) {
    for (i = 0; i < m->count[s]; ++i) spec_release(&m->store[s][i]);
    m->count[s] = 0;
  }
}

void rolltui_effect_map_free(RolltuiEffectMap* m) {
  size_t s;
  if (!m) return;
  rolltui_effect_map_clear(m);
  for (s = 0; s < m->states; ++s) {
    rolltui_mem_free(m->view[s]);
    rolltui_mem_free(m->store[s]);
  }
  rolltui_mem_free(m->view);
  rolltui_mem_free(m->store);
  rolltui_mem_free(m->count);
  rolltui_mem_free(m->cap);
  rolltui_mem_free(m);
}

size_t rolltui_effect_map_count(const RolltuiEffectMap* m, size_t state) {
  return (m && state < m->states) ? m->count[state] : 0;
}

const RolltuiEffectSpec* rolltui_effect_map_at(const RolltuiEffectMap* m, size_t state, size_t i) {
  if (!m || state >= m->states || i >= m->count[state]) return NULL;
  return &m->view[state][i];
}

int rolltui_effect_map_empty(const RolltuiEffectMap* m) {
  size_t s;
  if (!m) return 1;
  for (s = 0; s < m->states; ++s)
    if (m->count[s] != 0) return 0;
  return 1;
}

/* The view is rebuilt from the store after any change, because both may have moved. One
 * place, called from all three mutators, so a pointer can never be stale by omission. */
static void refresh_views(RolltuiEffectMap* m, size_t state) {
  size_t i;
  for (i = 0; i < m->count[state]; ++i) {
    const SpecStore* st = &m->store[state][i];
    RolltuiEffectSpec* v = &m->view[state][i];
    v->kind = st->kind;
    v->kind_len = st->kind_len;
    v->roles = st->roles ? st->roles : &m->fallback;
    v->role_count = st->roles ? st->role_count : 1;
    v->own_role_count = st->role_count;
    v->frames = st->frames;
    v->frame_count = st->frame_count;
  }
}

size_t rolltui_effect_map_add(RolltuiEffectMap* m, size_t state, const char* kind, size_t kind_len, int period_ms,
                              int width, int steps, int backward) {
  size_t i;
  SpecStore* st;
  RolltuiEffectSpec* v;
  if (!m || state >= m->states) return 0;
  i = m->count[state];
  /* GROWING, AMORTISED (strategy 2), and the two arrays grow together because a spec is
   * one thing stored in two: what it OWNS, and the borrows a reader is handed. */
  {
    size_t cap_store = m->cap[state], cap_view = m->cap[state];
    m->store[state] = (SpecStore*)rolltui_grow_zeroed(m->store[state], &cap_store, i + 1, sizeof **m->store);
    m->view[state] = (RolltuiEffectSpec*)rolltui_grow_zeroed(m->view[state], &cap_view, i + 1, sizeof **m->view);
    m->cap[state] = cap_store < cap_view ? cap_store : cap_view;
  }
  st = &m->store[state][i];
  memset(st, 0, sizeof *st);
  st->kind_len = kind_len;
  st->kind = (char*)rolltui_mem_alloc(kind_len == 0 ? 1 : kind_len);
  if (kind_len) memcpy(st->kind, kind, kind_len);
  v = &m->view[state][i];
  memset(v, 0, sizeof *v);
  v->period_ms = period_ms;
  v->width = width;
  v->steps = steps;
  v->backward = (unsigned char)(backward != 0);
  m->count[state] = i + 1;
  refresh_views(m, state);
  return i;
}

void rolltui_effect_map_set_jitter(RolltuiEffectMap* m, size_t state, size_t i, int jitter) {
  /* A scalar, so it lives on the VIEW: `refresh_views` rewrites only the pointer fields. */
  if (!m || state >= m->states || i >= m->count[state]) return;
  m->view[state][i].jitter = jitter < 0 ? 0 : (jitter > 100 ? 100 : jitter);
}

void rolltui_effect_map_add_frame(RolltuiEffectMap* m, size_t state, size_t i, const char* bytes, size_t len) {
  SpecStore* st;
  char* copy;
  if (!m || state >= m->states || i >= m->count[state]) return;
  st = &m->store[state][i];
  st->frames = (RolltuiEffectFrame*)rolltui_grow(st->frames, &st->frames_cap, st->frame_count + 1, sizeof *st->frames);
  copy = (char*)rolltui_mem_alloc(len == 0 ? 1 : len);
  if (len) memcpy(copy, bytes, len);
  st->frames[st->frame_count].bytes = copy;
  st->frames[st->frame_count].len = len;
  ++st->frame_count;
  refresh_views(m, state);
}

void rolltui_effect_map_add_role(RolltuiEffectMap* m, size_t state, size_t i, unsigned char role) {
  SpecStore* st;
  if (!m || state >= m->states || i >= m->count[state]) return;
  st = &m->store[state][i];
  st->roles = (unsigned char*)rolltui_grow(st->roles, &st->roles_cap, st->role_count + 1, sizeof *st->roles);
  st->roles[st->role_count++] = role;
  refresh_views(m, state);
}

RolltuiEffectMap* rolltui_effect_map_clone(const RolltuiEffectMap* m) {
  RolltuiEffectMap* out;
  size_t s, i, j;
  if (!m) return NULL;
  out = rolltui_effect_map_new(m->states, m->fallback);
  for (s = 0; s < m->states; ++s)
    for (i = 0; i < m->count[s]; ++i) {
      const SpecStore* st = &m->store[s][i];
      const RolltuiEffectSpec* v = &m->view[s][i];
      const size_t k = rolltui_effect_map_add(out, s, st->kind, st->kind_len, v->period_ms, v->width, v->steps,
                                              v->backward);
      /* EVERY SCALAR ON THE SPEC MUST BE COPIED HERE AND COMPARED IN `_equal` BELOW. `jitter`
       * was added and neither was updated: a clone silently dropped it, so a theme lost its
       * motion the moment anything copied it, and two maps differing only in jitter compared
       * EQUAL. The scalars not passed to `_add` are the ones to check when adding the next. */
      rolltui_effect_map_set_jitter(out, s, k, v->jitter);
      for (j = 0; j < st->frame_count; ++j)
        rolltui_effect_map_add_frame(out, s, k, st->frames[j].bytes, st->frames[j].len);
      for (j = 0; j < st->role_count; ++j) rolltui_effect_map_add_role(out, s, k, st->roles[j]);
    }
  return out;
}

int rolltui_effect_map_equal(const RolltuiEffectMap* a, const RolltuiEffectMap* b) {
  size_t s, i, j;
  if (a == b) return 1;
  if (!a || !b || a->states != b->states) return 0;
  for (s = 0; s < a->states; ++s) {
    if (a->count[s] != b->count[s]) return 0;
    for (i = 0; i < a->count[s]; ++i) {
      const SpecStore* x = &a->store[s][i];
      const SpecStore* y = &b->store[s][i];
      const RolltuiEffectSpec* vx = &a->view[s][i];
      const RolltuiEffectSpec* vy = &b->view[s][i];
      if (!name_eq(x->kind, x->kind_len, y->kind, y->kind_len)) return 0;
      if (vx->period_ms != vy->period_ms || vx->width != vy->width || vx->steps != vy->steps ||
          vx->backward != vy->backward || vx->jitter != vy->jitter)
        return 0;
      if (x->frame_count != y->frame_count || x->role_count != y->role_count) return 0;
      for (j = 0; j < x->frame_count; ++j)
        if (!name_eq(x->frames[j].bytes, x->frames[j].len, y->frames[j].bytes, y->frames[j].len)) return 0;
      for (j = 0; j < x->role_count; ++j)
        if (x->roles[j] != y->roles[j]) return 0;
    }
  }
  return 1;
}

/* ---- the pure parts ---------------------------------------------------------------------- */

int rolltui_effect_steps(const RolltuiEffectSpec* spec, int length) {
  if (spec->steps > 0) return spec->steps;
  const int len = length > 1 ? length : 1;
  if (spec_kind_is(spec, "spinner", 7) || spec_kind_is(spec, "ellipsis", 8))
    return spec->frame_count > 1 ? (int)spec->frame_count : 1;
  if (spec_kind_is(spec, "pulse", 5) || spec_kind_is(spec, "gradient", 8))
    return spec->role_count > 1 ? (int)spec->role_count : 1;
  if (spec_kind_is(spec, "blink", 5)) return 2;
  if (spec_kind_is(spec, "shimmer", 7)) return len + sweep_width(spec, len);
  if (spec_kind_is(spec, "bar", 3)) return 1;
  return len; /* a host's own kind, until its theme row says otherwise */
}

/* ---- applying, and the tick -------------------------------------------------------------- */

void rolltui_effects_apply(const RolltuiContext* c, RolltuiFrame* f, RolltuiEffectScratch* sc,
                           const RolltuiStyle* styles, const void* host,
                           const RolltuiEffectMap* map, unsigned long long now_ms, int ambiguous_wide,
                           RolltuiEffectReport* rep, RolltuiEffectUnknownFn on_unknown, void* unknown_ctx) {
  rep->marks_drawn = 0;
  rep->cells_touched = 0;
  rep->glyphs_refused = 0;
  const int fw = rolltui_frame_width(f), fh = rolltui_frame_height(f);
  const size_t marks = rolltui_frame_mark_count(f);
  for (size_t im = 0; im < marks; ++im) {
    int mx = 0, my = 0, cells = 0, state = 0;
    unsigned long long since = 0;
    double fraction = 0;
    rolltui_frame_mark_at(f, im, &mx, &my, &cells, &state, &since, &fraction);
    if (cells <= 0 || state <= 0) continue; /* state 0 is None, and is never recorded anyway */
    if (my < 0 || my >= fh) continue;
    const size_t n_specs = rolltui_effect_map_count(map, (size_t)state);
    if (n_specs == 0) continue;
    /* Each spec's kind is resolved ONCE PER MARK, not once per cell: the lookup is
     * loop-invariant and it is a string compare against a closed table, neither of which
     * belongs inside a per-cell draw loop. It also used to take the registry's mutex here,
     * once per mark, on the draw path — that went with the global (see rung 2 above). */
    sc->res = rolltui_grow(sc->res, &sc->res_cap, n_specs, sizeof *sc->res);
    for (size_t k = 0; k < n_specs; ++k) {
      const RolltuiEffectSpec* s = rolltui_effect_map_at(map, (size_t)state, k);
      if (!resolve(c, s->kind, s->kind_len, &sc->res[k]) && on_unknown) on_unknown(unknown_ctx, s->kind, s->kind_len);
    }
    int any = 0;
    const unsigned long long elapsed = now_ms >= since ? now_ms - since : 0;
    for (int i = 0; i < cells; ++i) {
      const int x = mx + i;
      if (x < 0 || x >= fw) continue;
      RolltuiCell cell;
      rolltui_frame_cell(f, x, my, &cell); /* a COPY: the handle lends no reference into itself */
      /* Property 2, at its two edges: a continuation cell belongs to the glyph before it,
       * and a 2-cell glyph whose second half is outside the span is skipped WHOLE. */
      if (cell.continuation) continue;
      if (cell.width == 2 && i + 1 >= cells) continue;
      RolltuiEffectCell in;
      in.elapsed_ms = elapsed;
      in.index = i;
      in.length = cells;
      in.fraction = clamp01(fraction);
      in.base = cell.style;
      in.ambiguous_wide = (unsigned char)(ambiguous_wide != 0);
      RolltuiEffectOut out;
      out.has_glyph = 0;
      out.has_style = 0;
      out.glyph_len = 0;
      for (size_t k = 0; k < n_specs; ++k) {
        if (sc->res[k].builtin < 0 && !sc->res[k].fn) continue; /* unknown: named above, draws nothing */
        RolltuiEffectOut one;
        one.has_glyph = 0;
        one.has_style = 0;
        one.glyph_len = 0;
        call_kind(sc, &sc->res[k], rolltui_effect_map_at(map, (size_t)state, k), styles, host, &in, &one);
        /* Stacking: the glyph comes from whichever kind last set one, the style likewise,
         * and a later kind sees the earlier one's style as its base — so "glyph from one,
         * colour from another" is the ordinary case rather than a special one. */
        if (one.has_style) {
          out.has_style = 1;
          out.style = one.style;
          in.base = one.style;
        }
        if (one.has_glyph) set_glyph(&out, one.glyph, one.glyph_len);
      }
      if (!out.has_glyph && !out.has_style) continue;
      const RolltuiStyle style = out.has_style ? out.style : cell.style;
      if (out.has_glyph) {
        /* PROPERTY 1, and it is enforced here rather than trusted: a glyph of the wrong
         * width is dropped and counted, never written. A glyph past the cap is refused the
         * same way and counted in the same number — its width is not knowable from the
         * bytes that fit (rolltui_effects.h). */
        const int w = out.glyph_len <= ROLLTUI_EFFECT_GLYPH_MAX
                          ? frame_width(sc, out.glyph, out.glyph_len, ambiguous_wide)
                          : -1;
        if (w != cell.width) {
          ++rep->glyphs_refused;
          out.has_glyph = 0;
        }
      }
      if (out.has_glyph) {
        rolltui_frame_put(f, x, my, out.glyph, out.glyph_len, cell.width, style, cell.link);
      } else {
        rolltui_frame_set_style(f, x, my, style);
        /* inside the span, by the guard above */
        if (cell.width == 2 && x + 1 < fw) rolltui_frame_set_style(f, x + 1, my, style);
      }
      ++rep->cells_touched;
      any = 1;
    }
    if (any) ++rep->marks_drawn;
  }
}

int rolltui_effects_tick_ms(const RolltuiContext* c, const RolltuiFrame* f, const RolltuiEffectMap* map) {
  int best = 0;
  const size_t marks = rolltui_frame_mark_count(f);
  for (size_t im = 0; im < marks; ++im) {
    int mx = 0, my = 0, cells = 0, state = 0;
    unsigned long long since = 0;
    double fraction = 0;
    rolltui_frame_mark_at(f, im, &mx, &my, &cells, &state, &since, &fraction);
    if (cells <= 0 || state <= 0) continue;
    const size_t n_specs = rolltui_effect_map_count(map, (size_t)state);
    for (size_t k = 0; k < n_specs; ++k) {
      const RolltuiEffectSpec* s = rolltui_effect_map_at(map, (size_t)state, k);
      if (s->period_ms <= 0) continue;                                    /* a still effect asks for no wakeup */
      if (!rolltui_effect_kind_resolves(c, s->kind, s->kind_len)) continue; /* nor one that cannot draw */
      int steps = rolltui_effect_steps(s, cells);
      if (steps < 1) steps = 1;
      int ms = s->period_ms / steps;
      if (ms < 16) ms = 16;
      if (best == 0 || ms < best) best = ms;
    }
  }
  return best;
}
