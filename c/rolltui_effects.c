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
 * are released by `rolltui_effect_clear_registered`, which the registry hands to
 * `rolltui_on_shutdown` the first time it holds anything.
 *
 * Everything allocates through the closed set in `rolltui_alloc.h`. */
#include "rolltui/c/rolltui_effects.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_unicode.h"

/* ---- working memory --------------------------------------------------------------------- */

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
static int step_index(const RolltuiEffectSpec* s, unsigned long long elapsed, int steps) {
  if (steps <= 1 || s->period_ms <= 0) return 0;
  const unsigned long long period = (unsigned long long)s->period_ms;
  const unsigned long long phase = elapsed % period;
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

/* ---- rung 1: the built-in kinds ----------------------------------------------------------- */
/* Each one is the whole of its rule. None of them names a colour: they pick between the base
 * style and a role the theme named (Effects.hpp, "what an effect may not do"). */

static void glyph_cycle(RolltuiEffectScratch* sc, const RolltuiEffectSpec* s, const RolltuiEffectCell* in,
                        RolltuiEffectOut* out, int at_end) {
  if (s->frame_count == 0) return;
  size_t first_len = 0;
  const char* first = s->frame(s->owner, 0, &first_len);
  const int w = frame_width(sc, first, first_len, in->ambiguous_wide);
  if (w <= 0 || w > in->length) return;
  const int start = at_end ? in->length - w : 0;
  if (in->index < start || in->index >= start + w) return;
  size_t flen = 0;
  const char* frame = s->frame(s->owner, (size_t)step_index(s, in->elapsed_ms, (int)s->frame_count), &flen);
  const char* g = NULL;
  const size_t glen = frame_cell(sc, frame, flen, in->index - start, in->ambiguous_wide, &g);
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
  if (in->index >= filled) return;
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

/* ---- rung 2: the kinds a HOST registered --------------------------------------------------- */
/* THE ONLY PROCESS-WIDE STATE IN THE PORTED SLICE. Three owned things per entry — the slot,
 * a copy of the name, and the host's context — and one releaser for all of them. */

typedef struct {
  char* name; /* OWNED: a copy, because the caller's may not outlive the registration */
  size_t name_len;
  RolltuiEffectFn fn;
  void* ctx;               /* OWNED, released with `free_ctx` */
  void (*free_ctx)(void*); /* may be NULL: a context with no lifetime of its own */
} HostKind;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static HostKind* g_kinds;
static size_t g_count, g_cap;
static int g_releaser_registered;

int rolltui_effect_register(const char* name, size_t name_len, RolltuiEffectFn fn, void* ctx,
                            void (*free_ctx)(void*)) {
  if (name_len == 0) return ROLLTUI_EFFECT_NO_NAME;
  if (!fn) return ROLLTUI_EFFECT_NO_FN;
  /* Rung 1 is never shadowed — the same guard, for the same reason, as a widget kind. */
  if (rolltui_effect_is_builtin(name, name_len)) return ROLLTUI_EFFECT_IS_BUILTIN;
  pthread_mutex_lock(&g_mu);
  for (size_t i = 0; i < g_count; ++i)
    if (name_eq(g_kinds[i].name, g_kinds[i].name_len, name, name_len)) {
      pthread_mutex_unlock(&g_mu);
      return ROLLTUI_EFFECT_DUPLICATE;
    }
  if (!g_releaser_registered) {
    /* Registered where the retained thing is MADE, not in a central list (Lifetime.hpp).
     * The flag is cleared again by `clear`, so a registry emptied by `shutdown()` and then
     * used again says so again — a releaser registered twice is idempotent, and a missing
     * one is a leak, so the duplicate is the safe direction. */
    g_releaser_registered = 1;
    rolltui_on_shutdown(rolltui_effect_clear_registered);
  }
  g_kinds = rolltui_grow_zeroed(g_kinds, &g_cap, g_count + 1, sizeof *g_kinds);
  HostKind* k = &g_kinds[g_count];
  k->name = (char*)rolltui_mem_alloc(name_len);
  memcpy(k->name, name, name_len);
  k->name_len = name_len;
  k->fn = fn;
  k->ctx = ctx;
  k->free_ctx = free_ctx;
  ++g_count;
  pthread_mutex_unlock(&g_mu);
  return ROLLTUI_EFFECT_OK;
}

void rolltui_effect_clear_registered(void) {
  /* The table is taken OUT under the lock and released outside it, so a host's own
   * destructor never runs while this library holds a mutex it might want. */
  pthread_mutex_lock(&g_mu);
  HostKind* kinds = g_kinds;
  const size_t n = g_count;
  g_kinds = NULL;
  g_count = 0;
  g_cap = 0;
  g_releaser_registered = 0;
  pthread_mutex_unlock(&g_mu);
  for (size_t i = 0; i < n; ++i) {
    rolltui_mem_free(kinds[i].name);
    if (kinds[i].free_ctx) kinds[i].free_ctx(kinds[i].ctx);
  }
  rolltui_mem_free(kinds);
}

size_t rolltui_effect_kind_count(void) {
  pthread_mutex_lock(&g_mu);
  const size_t n = ROLLTUI_BUILTIN_COUNT + g_count;
  pthread_mutex_unlock(&g_mu);
  return n;
}

const char* rolltui_effect_kind_name(size_t i, size_t* len) {
  if (i < ROLLTUI_BUILTIN_COUNT) {
    *len = kBuiltins[i].len;
    return kBuiltins[i].name;
  }
  pthread_mutex_lock(&g_mu);
  const size_t at = i - ROLLTUI_BUILTIN_COUNT;
  const char* name = "";
  *len = 0;
  if (at < g_count) {
    name = g_kinds[at].name;
    *len = g_kinds[at].name_len;
  }
  pthread_mutex_unlock(&g_mu);
  return name;
}

/* Rung 1 first and never shadowed, then the host's — the resolution order the whole
 * library uses for a widget kind, a menu file and an effect kind alike. */
static int resolve(const char* name, size_t len, Resolved* out) {
  out->builtin = builtin_index(name, len);
  out->fn = NULL;
  out->ctx = NULL;
  if (out->builtin >= 0) return 1;
  for (size_t i = 0; i < g_count; ++i)
    if (name_eq(g_kinds[i].name, g_kinds[i].name_len, name, len)) {
      out->fn = g_kinds[i].fn;
      out->ctx = g_kinds[i].ctx;
      return 1;
    }
  return 0;
}

int rolltui_effect_kind_resolves(const char* name, size_t len) {
  if (builtin_index(name, len) >= 0) return 1;
  pthread_mutex_lock(&g_mu);
  Resolved r;
  const int ok = resolve(name, len, &r);
  pthread_mutex_unlock(&g_mu);
  return ok;
}

static void call_kind(RolltuiEffectScratch* sc, const Resolved* r, const RolltuiEffectSpec* spec,
                      const RolltuiStyle* styles, const void* host, const RolltuiEffectCell* in,
                      RolltuiEffectOut* out) {
  if (r->builtin >= 0)
    kBuiltins[r->builtin].fn(sc, spec, styles, in, out);
  else
    r->fn(r->ctx, spec, styles, host, in, out);
}

/* ---- the pure parts -------------------------------------------------------------------------- */

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

/* ---- applying, and the tick -------------------------------------------------------------------- */

void rolltui_effects_apply(RolltuiFrame* f, RolltuiEffectScratch* sc, const RolltuiStyle* styles, const void* host,
                           RolltuiEffectSpec* specs, const size_t* state_first, const size_t* state_count,
                           size_t states, unsigned long long now_ms, int ambiguous_wide, RolltuiEffectReport* rep) {
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
    if ((size_t)state >= states) continue;
    const size_t first = state_first[state], n_specs = state_count[state];
    if (n_specs == 0) continue;
    /* Each spec's kind is resolved ONCE PER MARK, not once per cell: the lookup is
     * loop-invariant, it is a string compare against the closed table, and for a HOST kind
     * it takes the registry's mutex — none of which belongs inside a per-cell draw loop. */
    sc->res = rolltui_grow(sc->res, &sc->res_cap, n_specs, sizeof *sc->res);
    pthread_mutex_lock(&g_mu);
    for (size_t k = 0; k < n_specs; ++k) {
      RolltuiEffectSpec* s = &specs[first + k];
      if (!resolve(s->kind, s->kind_len, &sc->res[k])) s->unresolved = 1; /* named once per mark */
    }
    pthread_mutex_unlock(&g_mu);
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
        call_kind(sc, &sc->res[k], &specs[first + k], styles, host, &in, &one);
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

int rolltui_effects_tick_ms(const RolltuiFrame* f, const RolltuiEffectSpec* specs, const size_t* state_first,
                            const size_t* state_count, size_t states) {
  int best = 0;
  const size_t marks = rolltui_frame_mark_count(f);
  for (size_t im = 0; im < marks; ++im) {
    int mx = 0, my = 0, cells = 0, state = 0;
    unsigned long long since = 0;
    double fraction = 0;
    rolltui_frame_mark_at(f, im, &mx, &my, &cells, &state, &since, &fraction);
    if (cells <= 0 || state <= 0 || (size_t)state >= states) continue;
    const size_t first = state_first[state], n_specs = state_count[state];
    for (size_t k = 0; k < n_specs; ++k) {
      const RolltuiEffectSpec* s = &specs[first + k];
      if (s->period_ms <= 0) continue;                                    /* a still effect asks for no wakeup */
      if (!rolltui_effect_kind_resolves(s->kind, s->kind_len)) continue;  /* nor one that cannot draw */
      int steps = rolltui_effect_steps(s, cells);
      if (steps < 1) steps = 1;
      int ms = s->period_ms / steps;
      if (ms < 16) ms = 16;
      if (best == 0 || ms < best) best = ms;
    }
  }
  return best;
}
