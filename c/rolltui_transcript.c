/* rolltui/c/rolltui_transcript.c — the C side of the transcript widget. See
 * rolltui_transcript.h; the rules are rolltui/Transcript.hpp's. */
#include "rolltui/c/rolltui_transcript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_marker.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_wrap.h"

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static size_t zmin(size_t a, size_t b) { return a < b ? a : b; }
static size_t zmax(size_t a, size_t b) { return a > b ? a : b; }

/* ---- the selection model -------------------------------------------------------------------- */

int rolltui_text_pos_less(const RolltuiTextPos* a, const RolltuiTextPos* b) {
  if (a->entry != b->entry) return a->entry < b->entry;
  if (a->offset != b->offset) return a->offset < b->offset;
  return a->length < b->length;
}

static RolltuiTextPos sel_first(const RolltuiSelection* s) {
  return rolltui_text_pos_less(&s->head, &s->anchor) ? s->head : s->anchor;
}
static RolltuiTextPos sel_last(const RolltuiSelection* s) {
  return rolltui_text_pos_less(&s->head, &s->anchor) ? s->anchor : s->head;
}

int rolltui_selection_range_in(const RolltuiSelection* s, size_t entry, size_t len, size_t* begin,
                               size_t* end) {
  RolltuiTextPos f, l;
  size_t b, e;
  if (!s->active) return 0;
  f = sel_first(s);
  l = sel_last(s);
  if (entry < f.entry || entry > l.entry) return 0;
  b = (entry == f.entry) ? zmin(f.offset, len) : 0;
  e = (entry == l.entry) ? zmin(l.offset + l.length, len) : len;
  if (e < b) e = b;
  *begin = b;
  *end = e;
  return 1;
}

/* ---- the state ------------------------------------------------------------------------------- */

typedef struct CacheKey {
  unsigned long long version;
  int width;
  int ambiguous;
  int tab;
  int folded;
  unsigned long long code_epoch;
  unsigned long long highlight_epoch;
} CacheKey;

static int key_equal(const CacheKey* a, const CacheKey* b) { return memcmp(a, b, sizeof *a) == 0; }

typedef struct Cached {
  CacheKey key;
  RolltuiEntryLayout layout;
  int seen;
} Cached;

typedef struct Parsed {
  unsigned long long version;
  RolltuiMdDoc* doc; /* OWNED */
  int seen;
} Parsed;

typedef struct FindText {
  CacheKey key;
  RolltuiStr text;
} FindText;

typedef struct CodeFolds {
  unsigned long long epoch;
  RolltuiMdFoldState* states;
  size_t n, cap;
} CodeFolds;

typedef struct RowRef {
  size_t entry, line;
  int gap, beyond;
} RowRef;

typedef struct EntryState {
  int state;
  double progress;
  unsigned long long since_ms;
} EntryState;

struct RolltuiTranscript {
  /* THE FIVE CACHES. Every one was a `std::unordered_map<std::string, T>`; four are swept per
   * frame, which is what `rolltui_map`'s mark/unmark pair is for. */
  RolltuiMap parse_;      /* id → Parsed* */
  RolltuiMap cache_;      /* id → Cached* */
  RolltuiMap find_text_;  /* id → FindText* */
  RolltuiMap fold_over_;  /* id → an owned int (0/1) */
  RolltuiMap code_folds_; /* id → CodeFolds* */

  RolltuiStr pad_;     /* a run of spaces, grown to the widest prefix ever seen */
  RolltuiStr scratch_; /* one string under construction (the fold's " (N lines)") */
  RolltuiEntryLayout unfolded_;

  const RolltuiEntryLayout** layouts_; /* per entry, this frame */
  size_t layouts_n, layouts_cap;
  EntryState* states_;
  size_t states_cap;
  size_t* starts_;
  size_t starts_cap;
  size_t total_;

  RolltuiRect area_, text_area_;
  RolltuiTranscriptOptions opt_;
  RolltuiScrollAnchor scroll_;
  RolltuiSelection sel_;

  RolltuiMdHighlightFn highlight_;
  void* highlight_ctx_;
  unsigned long long highlight_epoch_;

  RolltuiStr query_;
  RolltuiFindMatch* matches_;
  size_t matches_n, matches_cap;
  size_t current_;
  int has_current_;
  int find_dirty_, reveal_;

  struct {
    int active, outside;
    int x, y;
    size_t origin_entry, origin_begin, origin_end;
  } drag_;
  struct {
    unsigned long long at_ms;
    int x, y, count;
  } click_;

  RolltuiTranscriptStats stats_;
  RolltuiTranscriptRoles roles_; /* handed over once; see the header for why not per draw */
  RolltuiCopyFn copy_fn_;
  void* copy_ctx_;

  RolltuiUnicodeScratch* u_;   /* WORKING MEMORY, one role: the cluster walks */
  RolltuiWrapLines* wrap_;     /* …and one for the plain-entry wrap */
  RolltuiUnicodeGrapheme* gs_; /* …and one for `for_each_cell`, which does not nest */
  size_t gs_cap;
  unsigned int* srcs_; /* the plain wrap's per-grapheme source offsets */
  size_t srcs_cap;
  RolltuiMdFoldState* fold_scratch_;
  size_t fold_scratch_cap;
};

/* ---- small helpers ---------------------------------------------------------------------------- */

static RolltuiStyle overlay_style(RolltuiStyle base, RolltuiStyle over) {
  if (over.fg.kind != 0) base.fg = over.fg;
  if (over.bg.kind != 0) base.bg = over.bg;
  base.bold |= over.bold;
  base.italic |= over.italic;
  base.underline |= over.underline;
  base.dim |= over.dim;
  base.reverse |= over.reverse;
  return base;
}

static unsigned int source_of(const RolltuiMdSpan* sp, size_t k) {
  if (sp->src_n == 0) return ROLLTUI_MD_NO_SOURCE;
  return sp->src_p[zmin(k, sp->src_n - 1)];
}

/* A run of spaces, grown to the widest prefix ever seen. */
static const char* spaces(RolltuiTranscript* t, int n, size_t* out_n) {
  const size_t need = (size_t)imax(n, 0);
  while (t->pad_.n < need) rolltui_str_append(&t->pad_, " ", 1);
  *out_n = need;
  return t->pad_.p ? t->pad_.p : "";
}

/* A chrome span: no source offsets and no href, so every cluster is NO_SOURCE. */
static void chrome(RolltuiMdLines* S, const char* text, size_t n, unsigned char role, int amb) {
  rolltui_md_lines_span(S, text, n, role, amb, NULL, 0, NULL, 0);
}

/* ---- the per-line cell walk -------------------------------------------------------------------- */
/* Visits every drawable grapheme of a line: the span, the grapheme's index within the span,
 * its bytes and its width. Width-0 clusters are skipped exactly as `put_text` skips them, so
 * cell positions agree. A callback rather than a template, and one buffer rather than one per
 * instantiation — the C's answer to the same "this must not nest" constraint, which holds
 * here because no visitor calls back into the walk. */
typedef void (*CellFn)(void* ctx, const RolltuiMdSpan* sp, size_t k, const char* text, size_t text_n,
                       int width);

static void for_each_cell(RolltuiTranscript* t, const RolltuiMdLine* line, int amb, CellFn fn, void* ctx) {
  size_t s;
  for (s = 0; s < line->span_n; ++s) {
    const RolltuiMdSpan* sp = &line->span_p[s];
    size_t count, i;
    t->gs_ = (RolltuiUnicodeGrapheme*)rolltui_grow(t->gs_, &t->gs_cap, sp->text_n ? sp->text_n : 1,
                                                  sizeof *t->gs_);
    count = rolltui_u_graphemes(t->u_, sp->text_p, sp->text_n, amb, t->gs_);
    for (i = 0; i < count; ++i)
      if (t->gs_[i].width > 0)
        fn(ctx, sp, i, sp->text_p + t->gs_[i].offset, t->gs_[i].length, t->gs_[i].width);
  }
}

/* ---- the caches -------------------------------------------------------------------------------- */

static void parsed_free(void* p) {
  Parsed* v = (Parsed*)p;
  rolltui_md_doc_free(v->doc);
  rolltui_mem_free(v);
}

static void cached_free(void* p) {
  Cached* v = (Cached*)p;
  rolltui_entry_layout_release(&v->layout);
  rolltui_mem_free(v);
}

static void find_text_free(void* p) {
  FindText* v = (FindText*)p;
  rolltui_str_free(&v->text);
  rolltui_mem_free(v);
}

static void code_folds_free(void* p) {
  CodeFolds* v = (CodeFolds*)p;
  rolltui_mem_free(v->states);
  rolltui_mem_free(v);
}

static void map_free_all(RolltuiMap* m, void (*fn)(void*)) {
  size_t i;
  for (i = 0; i < rolltui_map_count(m); ++i) fn(rolltui_map_value_at(m, i));
  rolltui_map_release(m);
}

static const RolltuiMdDoc* parsed_doc(RolltuiTranscript* t, const RolltuiDocEntry* e) {
  /* KEYED ON VERSION AND NOT ON WIDTH, which is the whole finding (m1): `parse` is a pure
   * function of the entry's text, and the layout cache's key carries `width` — so a resize
   * re-parsed forty unchanged strings into an identical tree and threw it away. */
  Parsed* p = (Parsed*)rolltui_map_get(&t->parse_, e->id.p, e->id.n);
  if (!p) {
    p = (Parsed*)rolltui_mem_alloc(sizeof *p);
    memset(p, 0, sizeof *p);
    p->doc = rolltui_md_doc_new();
    p->version = e->version + 1; /* forces the parse below */
    rolltui_map_put(&t->parse_, e->id.p, e->id.n, p);
  }
  if (p->version != e->version) {
    rolltui_md_parse(p->doc, e->text.p, e->text.n);
    p->version = e->version;
  }
  p->seen = 1;
  return p->doc;
}

static CodeFolds* code_folds_for(RolltuiTranscript* t, const char* id, size_t len, int make) {
  CodeFolds* f = (CodeFolds*)rolltui_map_get(&t->code_folds_, id, len);
  if (f || !make) return f;
  f = (CodeFolds*)rolltui_mem_alloc(sizeof *f);
  memset(f, 0, sizeof *f);
  rolltui_map_put(&t->code_folds_, id, len, f);
  return f;
}

/* ---- laying an entry out ------------------------------------------------------------------------ */

static void lay_out(RolltuiTranscript* t, RolltuiEntryLayout* L, const RolltuiDocEntry* e, int width,
                    const RolltuiTranscriptOptions* opt, int folded) {
  RolltuiMdLines* S = rolltui_entry_layout_store(L);
  const int amb = opt->ambiguous_wide;
  const int prefix_w = rolltui_u_display_width(t->u_, e->prefix.p, e->prefix.n, amb);
  const int inner = imax(width - prefix_w, 1);
  size_t body, k;
  L->folded = (unsigned char)(folded && e->foldable);
  L->hidden_lines = 0;

  /* The BODY, laid out into the front of the store. Everything below appends after it and
   * references its spans; nothing copies a byte of it. */
  if (e->markdown) {
    RolltuiMdRenderOptions ro;
    const CodeFolds* f = code_folds_for(t, e->id.p, e->id.n, 0);
    memset(&ro, 0, sizeof ro);
    ro.width = inner;
    ro.ambiguous_wide = amb;
    ro.tab_width = opt->tab_width;
    ro.base = e->role;
    ro.roles = *rolltui_md_roles();
    ro.highlight = t->highlight_;
    ro.highlight_ctx = t->highlight_ctx_;
    ro.fold_over_lines = opt->code_fold_over_lines;
    ro.cap_lines = opt->code_cap_lines;
    if (f && f->n) {
      ro.states = f->states;
      ro.state_count = f->n;
    }
    rolltui_md_render(S, parsed_doc(t, e), &ro);
  } else {
    RolltuiWrapOptions wo;
    size_t i, n;
    memset(&wo, 0, sizeof wo);
    wo.ambiguous_wide = amb;
    wo.tab_width = opt->tab_width;
    rolltui_md_lines_reset(S);
    rolltui_md_lines_text_set(S, e->text.p, e->text.n);
    rolltui_wrap(t->wrap_, e->text.p, e->text.n, inner, wo);
    n = rolltui_wrap_line_count(t->wrap_);
    for (i = 0; i < n; ++i) {
      const char* text = NULL;
      size_t text_len = 0, gn = 0, g;
      const RolltuiWrapGrapheme* gs = NULL;
      int w = 0, indent = 0, hard = 0;
      rolltui_wrap_line(t->wrap_, i, &text, &text_len, &gs, &gn, &w, &indent, &hard);
      rolltui_md_lines_open(S);
      if (indent > 0) {
        size_t pn = 0;
        const char* p = spaces(t, indent, &pn);
        chrome(S, p, pn, e->role, amb);
      }
      t->srcs_ = (unsigned int*)rolltui_grow(t->srcs_, &t->srcs_cap, gn ? gn : 1, sizeof *t->srcs_);
      for (g = 0; g < gn; ++g) t->srcs_[g] = (unsigned int)gs[g].source_offset;
      rolltui_md_lines_span(S, text, text_len, e->role, amb, t->srcs_, gn, NULL, 0);
      rolltui_md_lines_close(S);
    }
  }
  body = rolltui_md_lines_count(S);
  if (body == 0) { /* an entry with nothing in it still occupies one line */
    rolltui_md_lines_open(S);
    rolltui_md_lines_close(S);
    body = 1;
  }
  L->body = body;

  if (e->foldable) {
    size_t pn = 0;
    const char* p;
    rolltui_md_lines_open(S);
    chrome(S, e->prefix.p, e->prefix.n, e->prefix_role, amb);
    chrome(S, L->folded ? "\xE2\x96\xB8 " : "\xE2\x96\xBE ", 4, t->roles_.text_muted, amb); /* ▸ ▾ */
    chrome(S, e->summary.p, e->summary.n, e->role, amb);
    if (L->folded) {
      char num[32];
      rolltui_str_clear(&t->scratch_);
      rolltui_str_append(&t->scratch_, " (", 2);
      snprintf(num, sizeof num, "%zu", body);
      rolltui_str_append(&t->scratch_, num, strlen(num));
      rolltui_str_append(&t->scratch_, body == 1 ? " line)" : " lines)", body == 1 ? 6 : 7);
      chrome(S, t->scratch_.p, t->scratch_.n, t->roles_.text_muted, amb);
    }
    rolltui_md_lines_close(S);
    if (L->folded) {
      L->hidden_lines = body;
      rolltui_md_lines_text_set(S, e->summary.p, e->summary.n);
      rolltui_md_lines_clear_code_blocks(S); /* nothing of the body is drawn, so nothing is clickable */
      rolltui_md_lines_finish(S);
      return;
    }
    /* The entry's own summary row pushes every body line down by one, so the block rows have
     * to move with it — a click routes by LINE NUMBER. */
    rolltui_md_lines_shift_code_blocks(S, 1);
    p = spaces(t, prefix_w, &pn);
    for (k = 0; k < body; ++k) {
      size_t f = 0, n = 0, j;
      rolltui_md_lines_open(S);
      if (prefix_w > 0) chrome(S, p, pn, e->role, amb);
      rolltui_md_lines_span_range(S, k, &f, &n);
      /* BY INDEX, never by pointer: the prefix above may have grown the pools. */
      for (j = 0; j < n; ++j) rolltui_md_lines_span_ref(S, f + j);
      rolltui_md_lines_close(S);
    }
  } else {
    for (k = 0; k < body; ++k) {
      size_t f = 0, n = 0, j;
      rolltui_md_lines_open(S);
      if (prefix_w > 0) {
        if (k == 0) {
          chrome(S, e->prefix.p, e->prefix.n, e->prefix_role, amb);
        } else {
          size_t pn = 0;
          const char* p = spaces(t, prefix_w, &pn);
          chrome(S, p, pn, e->role, amb);
        }
      }
      rolltui_md_lines_span_range(S, k, &f, &n);
      for (j = 0; j < n; ++j) rolltui_md_lines_span_ref(S, f + j);
      rolltui_md_lines_close(S);
    }
  }
  rolltui_md_lines_finish(S);
}

/* ---- geometry over the laid-out entries ---------------------------------------------------------- */

static size_t layout_lines(const RolltuiEntryLayout* L) {
  const size_t n = L->store ? rolltui_md_lines_count(L->store) : 0;
  return n > L->body ? n - L->body : 0;
}

static size_t block_len(const RolltuiTranscript* t, size_t entry) {
  return (entry > 0 ? (size_t)imax(t->opt_.gap, 0) : 0) + layout_lines(t->layouts_[entry]);
}

static size_t max_top(const RolltuiTranscript* t) {
  const size_t h = (size_t)imax(t->area_.h, 0);
  return t->total_ > h ? t->total_ - h : 0;
}

static size_t top_line(const RolltuiTranscript* t) {
  size_t e;
  if (t->layouts_n == 0) return 0;
  e = zmin(t->scroll_.entry, t->layouts_n - 1);
  return t->starts_[e] + t->scroll_.line;
}

static void set_top(RolltuiTranscript* t, size_t top) {
  size_t lo = 0, hi, e;
  if (t->layouts_n == 0) {
    t->scroll_.entry = 0;
    t->scroll_.line = 0;
    t->scroll_.follow = 1;
    return;
  }
  top = zmin(top, max_top(t));
  /* the last entry whose start is <= top */
  hi = t->layouts_n;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (top < t->starts_[mid]) hi = mid;
    else lo = mid + 1;
  }
  e = lo - 1;
  t->scroll_.entry = e;
  t->scroll_.line = top - t->starts_[e];
  t->scroll_.follow = (unsigned char)(top >= max_top(t));
}

static size_t lines_below(const RolltuiTranscript* t) {
  const size_t shown_end = top_line(t) + (size_t)imax(t->area_.h, 0);
  return t->total_ > shown_end ? t->total_ - shown_end : 0;
}

static RowRef row_at(const RolltuiTranscript* t, size_t global) {
  RowRef r;
  size_t lo = 0, hi, local, gapn;
  memset(&r, 0, sizeof r);
  if (t->layouts_n == 0 || global >= t->total_) {
    r.beyond = 1;
    r.entry = t->layouts_n ? t->layouts_n - 1 : 0;
    return r;
  }
  hi = t->layouts_n;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (global < t->starts_[mid]) hi = mid;
    else lo = mid + 1;
  }
  r.entry = lo - 1;
  local = global - t->starts_[r.entry];
  gapn = r.entry > 0 ? (size_t)imax(t->opt_.gap, 0) : 0;
  if (local < gapn) {
    r.gap = 1;
    return r;
  }
  r.line = local - gapn;
  return r;
}

static const RolltuiMdLine* layout_line(const RolltuiEntryLayout* L, size_t i) {
  return rolltui_md_lines_line(L->store, L->body + i);
}

static void layout_text(const RolltuiEntryLayout* L, const char** p, size_t* n) {
  if (!L->store) {
    *p = "";
    *n = 0;
    return;
  }
  *p = rolltui_md_lines_text(L->store);
  *n = rolltui_md_lines_text_size(L->store);
}

/* ---- folding -------------------------------------------------------------------------------------- */

int rolltui_transcript_is_folded(const RolltuiTranscript* t, const RolltuiDocEntry* e) {
  const int* v = (const int*)rolltui_map_get(&t->fold_over_, e->id.p, e->id.n);
  return v ? *v : (e->folded != 0);
}

void rolltui_transcript_set_folded(RolltuiTranscript* t, const char* id, size_t len, int folded) {
  int* v = (int*)rolltui_map_get(&t->fold_over_, id, len);
  if (!v) {
    v = (int*)rolltui_mem_alloc(sizeof *v);
    rolltui_map_put(&t->fold_over_, id, len, v);
  }
  *v = folded ? 1 : 0;
}

/* THE FIRST TOGGLE OF A BLOCK MUST BUMP THE EPOCH EVEN WHEN THE VALUE "MATCHES" — a block
 * folded by the THRESHOLD has no state row, so a default row reads folded=0 and an early
 * return would leave the row added, the block unfolded and the cache never invalidated. */
void rolltui_transcript_set_code_folded(RolltuiTranscript* t, const char* id, size_t len, size_t block,
                                        int folded) {
  CodeFolds* f = code_folds_for(t, id, len, 1);
  size_t i;
  for (i = 0; i < f->n; ++i) {
    if (f->states[i].index != block) continue;
    if (f->states[i].folded == (unsigned char)(folded != 0)) return;
    f->states[i].folded = (unsigned char)(folded != 0);
    ++f->epoch;
    return;
  }
  f->states = (RolltuiMdFoldState*)rolltui_grow(f->states, &f->cap, f->n + 1, sizeof *f->states);
  f->states[f->n].index = block;
  f->states[f->n].folded = (unsigned char)(folded != 0);
  f->states[f->n].uncapped = 0;
  ++f->n;
  ++f->epoch;
}

void rolltui_transcript_set_code_uncapped(RolltuiTranscript* t, const char* id, size_t len, size_t block,
                                          int uncapped) {
  CodeFolds* f = code_folds_for(t, id, len, 1);
  size_t i;
  for (i = 0; i < f->n; ++i) {
    if (f->states[i].index != block) continue;
    if (f->states[i].uncapped == (unsigned char)(uncapped != 0)) return;
    f->states[i].uncapped = (unsigned char)(uncapped != 0);
    ++f->epoch;
    return;
  }
  f->states = (RolltuiMdFoldState*)rolltui_grow(f->states, &f->cap, f->n + 1, sizeof *f->states);
  f->states[f->n].index = block;
  f->states[f->n].folded = 0;
  f->states[f->n].uncapped = (unsigned char)(uncapped != 0);
  ++f->n;
  ++f->epoch;
}

/* ---- the per-frame build ---------------------------------------------------------------------------- */

static CacheKey key_for(const RolltuiTranscript* t, const RolltuiDocEntry* e, int width, int folded) {
  CacheKey k;
  const CodeFolds* f = code_folds_for((RolltuiTranscript*)t, e->id.p, e->id.n, 0);
  memset(&k, 0, sizeof k);
  k.version = e->version;
  k.width = width;
  k.ambiguous = t->opt_.ambiguous_wide != 0;
  k.tab = t->opt_.tab_width;
  k.folded = folded != 0;
  k.code_epoch = f ? f->epoch : 0;
  k.highlight_epoch = t->highlight_epoch_;
  return k;
}

static void build(RolltuiTranscript* t, const RolltuiDocument* doc, int width) {
  const size_t n = rolltui_document_count(doc);
  size_t i, g = 0;
  t->layouts_ = (const RolltuiEntryLayout**)rolltui_grow(t->layouts_, &t->layouts_cap, n ? n : 1,
                                                        sizeof *t->layouts_);
  t->states_ = (EntryState*)rolltui_grow(t->states_, &t->states_cap, n ? n : 1, sizeof *t->states_);
  t->starts_ = (size_t*)rolltui_grow(t->starts_, &t->starts_cap, n ? n : 1, sizeof *t->starts_);
  t->layouts_n = n;
  for (i = 0; i < rolltui_map_count(&t->cache_); ++i)
    ((Cached*)rolltui_map_value_at(&t->cache_, i))->seen = 0;
  for (i = 0; i < n; ++i) {
    const RolltuiDocEntry* e = rolltui_document_at(doc, i);
    const int folded = e->foldable && rolltui_transcript_is_folded(t, e);
    const CacheKey key = key_for(t, e, width, folded);
    Cached* c = (Cached*)rolltui_map_get(&t->cache_, e->id.p, e->id.n);
    if (!c) {
      c = (Cached*)rolltui_mem_alloc(sizeof *c);
      memset(c, 0, sizeof *c);
      rolltui_map_put(&t->cache_, e->id.p, e->id.n, c);
    }
    if (!key_equal(&c->key, &key)) {
      /* Laid out INTO the cache's own layout, so a re-lay reuses every buffer it had. */
      c->key = key;
      lay_out(t, &c->layout, e, width, &t->opt_, folded);
      ++t->stats_.entries_relaid;
    }
    c->seen = 1;
    t->states_[i].state = (int)e->state;
    t->states_[i].progress = e->progress;
    t->states_[i].since_ms = e->state_since_ms;
    t->layouts_[i] = &c->layout;
    t->starts_[i] = g;
    g += block_len(t, i);
  }
  t->total_ = g;
  if (rolltui_map_count(&t->cache_) > 2 * n + 32) {
    for (i = rolltui_map_count(&t->cache_); i-- > 0;) {
      Cached* c = (Cached*)rolltui_map_value_at(&t->cache_, i);
      if (c->seen) continue;
      rolltui_map_remove_at(&t->cache_, i);
      cached_free(c);
    }
  }
  /* Reconcile the anchor with the new layout. */
  if (n == 0) {
    t->scroll_.entry = 0;
    t->scroll_.line = 0;
    t->scroll_.follow = 1;
  } else if (t->scroll_.follow) {
    set_top(t, max_top(t));
  } else {
    size_t len;
    t->scroll_.entry = zmin(t->scroll_.entry, n - 1);
    len = block_len(t, t->scroll_.entry);
    t->scroll_.line = zmin(t->scroll_.line, len > 0 ? len - 1 : 0);
    set_top(t, t->starts_[t->scroll_.entry] + t->scroll_.line);
  }
}

/* ---- find ------------------------------------------------------------------------------------------- */

static char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static int matches_at(const char* hay, size_t hn, size_t at, const char* needle, size_t nn) {
  size_t k;
  if (at + nn > hn) return 0;
  for (k = 0; k < nn; ++k)
    if (lower_ascii(hay[at + k]) != lower_ascii(needle[k])) return 0;
  return 1;
}

int rolltui_transcript_set_query(RolltuiTranscript* t, const char* q, size_t len) {
  if (rolltui_str_eq(&t->query_, q, len)) return 0;
  rolltui_str_set(&t->query_, q, len);
  t->matches_n = 0;
  t->has_current_ = 0;
  t->find_dirty_ = 1;
  /* An empty query clears and NEVER moves the view — a find bar you just emptied must not
   * throw you somewhere. */
  t->reveal_ = t->query_.n != 0;
  return 1;
}

/* The entry's logical text AS IF UNFOLDED — what find searches. */
static void searchable_text(RolltuiTranscript* t, const RolltuiDocEntry* e, size_t entry, int width,
                            const char** out, size_t* out_n) {
  FindText* ft;
  CacheKey key;
  if (!(e->foldable && rolltui_transcript_is_folded(t, e))) {
    layout_text(t->layouts_[entry], out, out_n);
    return;
  }
  key = key_for(t, e, width, /*folded=*/0);
  ft = (FindText*)rolltui_map_get(&t->find_text_, e->id.p, e->id.n);
  if (!ft) {
    ft = (FindText*)rolltui_mem_alloc(sizeof *ft);
    memset(ft, 0, sizeof *ft);
    rolltui_map_put(&t->find_text_, e->id.p, e->id.n, ft);
  } else if (key_equal(&ft->key, &key)) {
    *out = rolltui_str_get(&ft->text, out_n);
    return;
  }
  ft->key = key;
  lay_out(t, &t->unfolded_, e, width, &t->opt_, /*folded=*/0);
  {
    const char* p;
    size_t n;
    layout_text(&t->unfolded_, &p, &n);
    rolltui_str_set(&ft->text, p, n);
  }
  *out = rolltui_str_get(&ft->text, out_n);
}

static const RolltuiFindMatch* current_match(const RolltuiTranscript* t) {
  return t->has_current_ && t->current_ < t->matches_n ? &t->matches_[t->current_] : NULL;
}

static void match_push(RolltuiTranscript* t, size_t entry, size_t offset, size_t length) {
  t->matches_ = (RolltuiFindMatch*)rolltui_grow(t->matches_, &t->matches_cap, t->matches_n + 1,
                                                sizeof *t->matches_);
  t->matches_[t->matches_n].entry = entry;
  t->matches_[t->matches_n].offset = offset;
  t->matches_[t->matches_n].length = length;
  ++t->matches_n;
}

static void recompute_matches(RolltuiTranscript* t, const RolltuiDocument* doc, int width) {
  RolltuiFindMatch was;
  const int had = current_match(t) != NULL;
  size_t i, top, pick = 0;
  if (had) was = *current_match(t);
  t->matches_n = 0;
  t->has_current_ = 0;
  if (t->query_.n == 0) return;
  for (i = 0; i < rolltui_document_count(doc); ++i) {
    const char* hay = "";
    size_t hn = 0, at = 0;
    searchable_text(t, rolltui_document_at(doc, i), i, width, &hay, &hn);
    while (at + t->query_.n <= hn) {
      if (matches_at(hay, hn, at, t->query_.p, t->query_.n)) {
        match_push(t, i, at, t->query_.n);
        at += zmax(t->query_.n, 1);
      } else {
        ++at;
      }
    }
  }
  if (t->matches_n == 0) return;
  if (had) {
    size_t k;
    for (k = 0; k < t->matches_n; ++k)
      if (t->matches_[k].entry == was.entry && t->matches_[k].offset == was.offset &&
          t->matches_[k].length == was.length) {
        t->current_ = k;
        t->has_current_ = 1;
        return;
      }
  }
  /* The first match at or after the top of the view, so typing into a find bar moves forward
   * from where you are rather than jumping to the top of the document. */
  top = top_line(t);
  for (i = 0; i < t->matches_n; ++i) {
    const size_t e = t->matches_[i].entry;
    if (e < t->layouts_n && t->starts_[e] + block_len(t, e) > top) {
      pick = i;
      break;
    }
  }
  t->current_ = pick;
  t->has_current_ = 1;
}

/* The per-line source span, for `line_of_offset` and the selection's "fully inside" test. */
typedef struct SrcSpan {
  int any;
  size_t lo, hi;
} SrcSpan;

static void src_span_cell(void* ctx, const RolltuiMdSpan* sp, size_t k, const char* text, size_t text_n,
                          int width) {
  SrcSpan* s = (SrcSpan*)ctx;
  const unsigned int src = source_of(sp, k);
  (void)text;
  (void)width;
  if (src == ROLLTUI_MD_NO_SOURCE) return;
  if (!s->any) {
    s->lo = src;
    s->hi = src + text_n;
    s->any = 1;
  } else {
    s->lo = zmin(s->lo, src);
    s->hi = zmax(s->hi, src + text_n);
  }
}

static size_t line_of_offset(RolltuiTranscript* t, size_t entry, size_t offset) {
  const RolltuiEntryLayout* L = entry < t->layouts_n ? t->layouts_[entry] : NULL;
  const size_t n = L ? layout_lines(L) : 0;
  size_t best = 0, i;
  if (!L || n == 0) return 0;
  for (i = 0; i < n; ++i) {
    SrcSpan s;
    s.any = 0;
    s.lo = 0;
    s.hi = 0;
    for_each_cell(t, layout_line(L, i), t->opt_.ambiguous_wide, src_span_cell, &s);
    if (!s.any) continue;
    if (offset < s.hi) return i;
    if (s.lo <= offset) best = i;
  }
  return best;
}

static const RolltuiMdCodeBlock* hiding_block(const RolltuiTranscript* t, size_t entry, size_t offset) {
  const RolltuiEntryLayout* L = entry < t->layouts_n ? t->layouts_[entry] : NULL;
  size_t i, count;
  const RolltuiMdCodeBlock* blocks;
  if (!L || !L->store) return NULL;
  blocks = rolltui_md_lines_code_blocks(L->store);
  count = rolltui_md_lines_code_block_count(L->store);
  for (i = 0; i < count; ++i) {
    const RolltuiMdCodeBlock* b = &blocks[i];
    const char* text;
    size_t tn, line = 0, j;
    if (offset < b->text_begin || offset >= b->text_end) continue;
    if (b->folded) return b;
    if (b->hidden == 0) return NULL;
    /* Capped: the first (lines - hidden) are drawn; which line the offset is on is a count of
     * newlines from the block's start. */
    layout_text(L, &text, &tn);
    for (j = b->text_begin; j < offset && j < tn; ++j)
      if (text[j] == '\n') ++line;
    return line >= b->lines - b->hidden ? b : NULL;
  }
  return NULL;
}

static void reveal_current(RolltuiTranscript* t, const RolltuiDocument* doc, int width) {
  const RolltuiFindMatch* m = current_match(t);
  const RolltuiDocEntry* e;
  const RolltuiMdCodeBlock* b;
  size_t line, gapn, g, h, top;
  if (!m || m->entry >= rolltui_document_count(doc)) return;
  e = rolltui_document_at(doc, m->entry);
  if (e->foldable && rolltui_transcript_is_folded(t, e)) {
    rolltui_transcript_set_folded(t, e->id.p, e->id.n, 0);
    build(t, doc, width);
  }
  b = hiding_block(t, m->entry, m->offset);
  if (b) {
    rolltui_transcript_set_code_folded(t, e->id.p, e->id.n, b->index, 0);
    rolltui_transcript_set_code_uncapped(t, e->id.p, e->id.n, b->index, 1);
    build(t, doc, width);
  }
  line = line_of_offset(t, m->entry, m->offset);
  gapn = m->entry > 0 ? (size_t)imax(t->opt_.gap, 0) : 0;
  g = t->starts_[m->entry] + gapn + line;
  h = (size_t)imax(t->area_.h, 1);
  top = top_line(t);
  /* Minimal movement: already in view, nothing moves. */
  if (g < top) set_top(t, g);
  else if (g >= top + h) set_top(t, g - h + 1);
}

int rolltui_transcript_find_next(RolltuiTranscript* t) {
  if (t->matches_n == 0) return 0;
  t->current_ = t->has_current_ ? (t->current_ + 1) % t->matches_n : 0;
  t->has_current_ = 1;
  t->reveal_ = 1;
  return 1;
}

int rolltui_transcript_find_prev(RolltuiTranscript* t) {
  if (t->matches_n == 0) return 0;
  t->current_ = (t->has_current_ && t->current_ > 0) ? t->current_ - 1 : t->matches_n - 1;
  t->has_current_ = 1;
  t->reveal_ = 1;
  return 1;
}

/* ---- layout ------------------------------------------------------------------------------------------ */

static unsigned long long now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000ull + (unsigned long long)(ts.tv_nsec / 1000);
}

void rolltui_transcript_layout(RolltuiTranscript* t, const RolltuiDocument* doc, RolltuiRect area,
                               const RolltuiTranscriptOptions* opt) {
  const unsigned long long t0 = now_us();
  int width;
  t->area_ = area;
  t->opt_ = *opt;
  t->text_area_ = area;
  if (opt->inset > 0 && area.w >= 2 * opt->inset + 1) {
    t->text_area_.x += opt->inset;
    t->text_area_.w -= 2 * opt->inset;
  }
  width = imax(t->text_area_.w, 1);
  t->stats_.entries_relaid = 0;
  build(t, doc, width);
  /* A relaid entry means text moved under the match list, so offsets recorded against the old
   * text are stale. Cheap to notice here; expensive to debug as a highlight over wrong bytes. */
  if (t->query_.n && t->stats_.entries_relaid > 0) t->find_dirty_ = 1;
  if (t->find_dirty_) {
    recompute_matches(t, doc, width);
    t->find_dirty_ = 0;
  }
  if (t->reveal_) {
    reveal_current(t, doc, width);
    t->reveal_ = 0;
  }
  t->stats_.total_lines = t->total_;
  t->stats_.cache_size = rolltui_map_count(&t->cache_);
  t->stats_.layout_us = (long)(now_us() - t0);
}

/* ---- drawing ------------------------------------------------------------------------------------------- */

typedef struct DrawCtx {
  RolltuiTranscript* t;
  RolltuiFrame* f;
  RolltuiDrawScratch* d;
  const RolltuiStyle* styles;
  const RolltuiTranscriptRoles* roles;
  RolltuiStyle sel_style, match_style, current_style;
  const RolltuiFindMatch* cur;
  const RolltuiFindMatch* mb;
  const RolltuiFindMatch* me;
  int in_sel, fully;
  size_t sb, se;
  int x, y, right, row_start;
  int stop;
} DrawCtx;

static void draw_cell(void* ctx, const RolltuiMdSpan* sp, size_t k, const char* text, size_t text_n,
                      int width) {
  DrawCtx* c = (DrawCtx*)ctx;
  unsigned int src;
  int selected;
  RolltuiStyle st;
  unsigned int link = 0;
  if (c->stop || c->x + width > c->right) {
    c->stop = 1;
    return;
  }
  src = source_of(sp, k);
  selected = c->fully || (c->in_sel && src != ROLLTUI_MD_NO_SOURCE && src >= c->sb && src < c->se);
  st = c->styles[sp->role];
  if (selected) st = overlay_style(st, c->sel_style);
  /* The selection WINS where they overlap: it is the user's most recent direct act.
   * Otherwise the current match beats the other matches. */
  if (!selected && src != ROLLTUI_MD_NO_SOURCE) {
    const RolltuiFindMatch* m;
    for (m = c->mb; m != c->me; ++m) {
      if (src >= m->offset && src < m->offset + m->length) {
        const int is_cur = c->cur && m->entry == c->cur->entry && m->offset == c->cur->offset &&
                           m->length == c->cur->length;
        st = overlay_style(st, is_cur ? c->current_style : c->match_style);
        break;
      }
    }
  }
  if (sp->href_n) link = rolltui_frame_link_id(c->f, sp->href_p, sp->href_n);
  c->x += rolltui_frame_put(c->f, c->x, c->y, text, text_n, width, st, link);
}

void rolltui_transcript_set_roles(RolltuiTranscript* t, const RolltuiTranscriptRoles* roles) {
  t->roles_ = *roles;
}

void rolltui_transcript_draw(const RolltuiTranscript* ct, RolltuiFrame* f, RolltuiDrawScratch* d,
                             const RolltuiStyle* styles) {
  RolltuiTranscript* t = (RolltuiTranscript*)ct;
  const RolltuiTranscriptRoles* roles = &t->roles_;
  RolltuiRect bounds, area;
  const int amb = t->opt_.ambiguous_wide;
  const size_t top = top_line(t);
  const int right = t->text_area_.x + t->text_area_.w;
  char marker[ROLLTUI_MARKER_MAX];
  size_t marker_n;
  int row;
  bounds.x = 0;
  bounds.y = 0;
  bounds.w = rolltui_frame_width(f);
  bounds.h = rolltui_frame_height(f);
  {
    int out[4];
    rolltui_rect_intersect(t->area_.x, t->area_.y, t->area_.w, t->area_.h, 0, 0, bounds.w, bounds.h, out);
    area.x = out[0];
    area.y = out[1];
    area.w = out[2];
    area.h = out[3];
  }
  if (area.w <= 0 || area.h <= 0) return;
  rolltui_frame_fill(f, d, area, styles[roles->background], NULL, 0);
  for (row = 0; row < t->area_.h; ++row) {
    const size_t g = top + (size_t)row;
    RowRef r;
    DrawCtx c;
    const RolltuiMdLine* line;
    const char* text;
    size_t len;
    if (g >= t->total_) break;
    r = row_at(t, g);
    if (r.gap || r.beyond) continue;
    c.y = t->area_.y + row;
    if (c.y < 0 || c.y >= bounds.h) continue;
    line = layout_line(t->layouts_[r.entry], r.line);
    layout_text(t->layouts_[r.entry], &text, &len);
    c.t = t;
    c.f = f;
    c.d = d;
    c.styles = styles;
    c.roles = roles;
    c.sel_style = styles[roles->selection];
    c.match_style = styles[roles->find_match];
    c.current_style = styles[roles->find_current];
    c.cur = current_match(t);
    c.sb = 0;
    c.se = 0;
    c.in_sel = rolltui_selection_range_in(&t->sel_, r.entry, len, &c.sb, &c.se);
    c.fully = 0;
    if (c.in_sel) {
      SrcSpan s;
      s.any = 0;
      s.lo = 0;
      s.hi = 0;
      for_each_cell(t, line, amb, src_span_cell, &s);
      c.fully = s.any ? (s.lo >= c.sb && s.hi <= c.se) : (c.sb == 0 && c.se >= len);
    }
    /* This entry's matches only: the list is sorted by (entry, offset), so this is a binary
     * search rather than a scan of every match for every cell. */
    c.mb = t->matches_;
    c.me = t->matches_;
    if (t->matches_n) {
      size_t lo = 0, hi = t->matches_n;
      while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (t->matches_[mid].entry < r.entry) lo = mid + 1;
        else hi = mid;
      }
      c.mb = t->matches_ + lo;
      hi = t->matches_n;
      while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (r.entry < t->matches_[mid].entry) hi = mid;
        else lo = mid + 1;
      }
      c.me = t->matches_ + lo;
    }
    c.x = t->text_area_.x;
    c.right = right;
    c.row_start = c.x;
    c.stop = 0;
    for_each_cell(t, line, amb, draw_cell, &c);
    /* This widget's ENTIRE contribution to motion: it marks the cells it just drew with the
     * entry's state and stops. One mark per DRAWN ROW. */
    if (t->states_[r.entry].state != 0 && c.x > c.row_start)
      rolltui_frame_mark(f, c.row_start, c.y, c.x - c.row_start, t->states_[r.entry].state,
                         t->states_[r.entry].since_ms, t->states_[r.entry].progress);
  }
  marker_n = t->area_.h > 0
                 ? rolltui_scroll_marker_text(lines_below(t), t->text_area_.w, amb, marker, sizeof marker)
                 : 0;
  if (marker_n) {
    const int mw = rolltui_u_display_width(t->u_, marker, marker_n, amb);
    rolltui_frame_put_text(f, d, right - mw, t->area_.y + t->area_.h - 1, marker, marker_n,
                           styles[roles->scroll_marker], mw, amb, 0);
  }
}

/* ---- scrolling ------------------------------------------------------------------------------------------ */

void rolltui_transcript_scroll_by(RolltuiTranscript* t, long lines) {
  long v = (long)top_line(t) + lines;
  if (v < 0) v = 0;
  set_top(t, (size_t)v);
}

void rolltui_transcript_scroll_page(RolltuiTranscript* t, int direction) {
  const long page = imax(t->area_.h - 1, 1);
  rolltui_transcript_scroll_by(t, direction < 0 ? -page : page);
}

void rolltui_transcript_scroll_to_top(RolltuiTranscript* t) { set_top(t, 0); }

void rolltui_transcript_scroll_to_bottom(RolltuiTranscript* t) {
  set_top(t, max_top(t));
  t->scroll_.follow = 1;
}

void rolltui_transcript_scroll(const RolltuiTranscript* t, RolltuiScrollAnchor* out) { *out = t->scroll_; }
size_t rolltui_transcript_total_lines(const RolltuiTranscript* t) { return t->total_; }
size_t rolltui_transcript_top_line(const RolltuiTranscript* t) { return top_line(t); }
size_t rolltui_transcript_lines_below(const RolltuiTranscript* t) { return lines_below(t); }
int rolltui_transcript_viewport_height(const RolltuiTranscript* t) { return t->area_.h; }

/* ---- hit testing and selection ----------------------------------------------------------------------------- */

typedef struct HitCtx {
  size_t entry;
  int x, cx;
  int have_at, have_before, have_after, have_last;
  RolltuiTextPos at, before, after, last;
} HitCtx;

static void hit_cell(void* ctx, const RolltuiMdSpan* sp, size_t k, const char* text, size_t text_n,
                     int width) {
  HitCtx* h = (HitCtx*)ctx;
  const unsigned int src = source_of(sp, k);
  const int contains = h->x >= h->cx && h->x < h->cx + width;
  (void)text;
  if (src != ROLLTUI_MD_NO_SOURCE) {
    RolltuiTextPos p;
    p.entry = h->entry;
    p.offset = src;
    p.length = text_n;
    h->last = p;
    h->have_last = 1;
    if (contains) {
      h->at = p;
      h->have_at = 1;
    } else if (h->cx + width <= h->x) {
      h->before.entry = h->entry;
      h->before.offset = src + text_n;
      h->before.length = 0;
      h->have_before = 1;
    } else if (!h->have_after) {
      h->after.entry = h->entry;
      h->after.offset = src;
      h->after.length = 0;
      h->have_after = 1;
    }
  }
  h->cx += width;
}

/* The END of the nearest text on a line, for a line with none of its own. */
typedef struct TailCtx {
  size_t entry;
  int found;
  RolltuiTextPos pos;
} TailCtx;

static void tail_cell(void* ctx, const RolltuiMdSpan* sp, size_t k, const char* text, size_t text_n,
                      int width) {
  TailCtx* c = (TailCtx*)ctx;
  const unsigned int src = source_of(sp, k);
  (void)text;
  (void)width;
  if (src == ROLLTUI_MD_NO_SOURCE) return;
  c->pos.entry = c->entry;
  c->pos.offset = src + text_n;
  c->pos.length = 0;
  c->found = 1;
}

static int hit_row(RolltuiTranscript* t, const RowRef* r, int x, RolltuiTextPos* out) {
  const RolltuiEntryLayout* L = t->layouts_[r->entry];
  const int amb = t->opt_.ambiguous_wide;
  HitCtx h;
  size_t li;
  memset(&h, 0, sizeof h);
  h.entry = r->entry;
  h.x = x;
  h.cx = t->text_area_.x;
  for_each_cell(t, layout_line(L, r->line), amb, hit_cell, &h);
  if (h.have_at) {
    *out = h.at;
    return 1;
  }
  if (x >= h.cx) { /* past the end of the line: the last grapheme, inclusive */
    if (h.have_last) {
      *out = h.last;
      return 1;
    }
  } else { /* on chrome or before the first cell: the nearest text */
    if (h.have_before) {
      *out = h.before;
      return 1;
    }
    if (h.have_after) {
      *out = h.after;
      return 1;
    }
  }
  /* A line with no text at all (a summary line, a box rule): the end of the nearest text
   * above it in the same entry, else the entry's start. */
  for (li = r->line; li-- > 0;) {
    TailCtx c;
    memset(&c, 0, sizeof c);
    c.entry = r->entry;
    for_each_cell(t, layout_line(L, li), amb, tail_cell, &c);
    if (c.found) {
      *out = c.pos;
      return 1;
    }
  }
  out->entry = r->entry;
  out->offset = 0;
  out->length = 0;
  return 1;
}

int rolltui_transcript_hit(const RolltuiTranscript* ct, int x, int y, RolltuiTextPos* out) {
  RolltuiTranscript* t = (RolltuiTranscript*)ct;
  int row;
  RowRef r;
  if (t->layouts_n == 0 || y < t->area_.y) return 0;
  row = y - t->area_.y;
  if (row >= t->area_.h) row = imax(t->area_.h - 1, 0);
  r = row_at(t, top_line(t) + (size_t)row);
  if (r.beyond) {
    const char* p;
    size_t n;
    layout_text(t->layouts_[t->layouts_n - 1], &p, &n);
    out->entry = t->layouts_n - 1;
    out->offset = n;
    out->length = 0;
    return 1;
  }
  if (r.gap) {
    if (r.entry > 0) {
      const char* p;
      size_t n;
      layout_text(t->layouts_[r.entry - 1], &p, &n);
      out->entry = r.entry - 1;
      out->offset = n;
      out->length = 0;
      return 1;
    }
    out->entry = 0;
    out->offset = 0;
    out->length = 0;
    return 1;
  }
  return hit_row(t, &r, x, out);
}

void rolltui_transcript_selection(const RolltuiTranscript* t, RolltuiSelection* out) { *out = t->sel_; }
void rolltui_transcript_clear_selection(RolltuiTranscript* t) { memset(&t->sel_, 0, sizeof t->sel_); }
void rolltui_transcript_select(RolltuiTranscript* t, RolltuiTextPos anchor, RolltuiTextPos head) {
  t->sel_.anchor = anchor;
  t->sel_.head = head;
  t->sel_.active = 1;
}

void rolltui_transcript_selected_text(const RolltuiTranscript* t, RolltuiStr* out) {
  RolltuiTextPos f, l;
  size_t e;
  rolltui_str_clear(out);
  if (!t->sel_.active || t->layouts_n == 0) return;
  f = sel_first(&t->sel_);
  l = sel_last(&t->sel_);
  for (e = f.entry; e <= l.entry && e < t->layouts_n; ++e) {
    const char* text;
    size_t n, b = 0, en = 0;
    layout_text(t->layouts_[e], &text, &n);
    if (!rolltui_selection_range_in(&t->sel_, e, n, &b, &en)) continue;
    if (e > f.entry) rolltui_str_append(out, "\n", 1);
    rolltui_str_append(out, text + b, en - b);
  }
}

int rolltui_transcript_copy_selection(RolltuiTranscript* t) {
  if (!t->sel_.active) return 0;
  if (t->copy_fn_) {
    RolltuiStr s;
    memset(&s, 0, sizeof s);
    rolltui_transcript_selected_text(t, &s);
    t->copy_fn_(t->copy_ctx_, s.p ? s.p : "", s.n);
    rolltui_str_free(&s);
  }
  return 1;
}

/* The word (UAX #29) or logical line containing byte `off` of `text`. */
static void unit_around(RolltuiTranscript* t, const char* text, size_t n, size_t off, int word, size_t* b,
                        size_t* en) {
  size_t i;
  if (word) {
    rolltui_u_word_range(t->u_, text, n, off, b, en);
    return;
  }
  *b = 0;
  for (i = off; i-- > 0;)
    if (text[i] == '\n') {
      *b = i + 1;
      break;
    }
  *en = n;
  for (i = off; i < n; ++i)
    if (text[i] == '\n') {
      *en = i;
      break;
    }
}

/* ---- the drag ------------------------------------------------------------------------------------------------ */

static void drag_to(RolltuiTranscript* t, int x, int y) {
  RolltuiTextPos pos;
  int row;
  if (!t->drag_.active) return;
  t->drag_.x = x;
  t->drag_.y = y;
  row = y - t->area_.y;
  t->drag_.outside = row < 0 || row >= t->area_.h;
  row = iclamp(row, 0, imax(t->area_.h - 1, 0));
  if (!rolltui_transcript_hit(t, x, t->area_.y + row, &pos)) return;
  t->sel_.active = 1;
  /* After a double/triple click the selection grows by whole words/lines while the pointer
   * stays in the same entry; elsewhere it grows by graphemes from the unit. */
  if (t->click_.count >= 2 && pos.entry == t->drag_.origin_entry) {
    const char* text;
    size_t n, b = 0, en = 0;
    layout_text(t->layouts_[pos.entry], &text, &n);
    unit_around(t, text, n, zmin(pos.offset, n), t->click_.count == 2, &b, &en);
    if (pos.offset < t->drag_.origin_begin) {
      t->sel_.anchor.entry = pos.entry;
      t->sel_.anchor.offset = t->drag_.origin_end;
      t->sel_.anchor.length = 0;
      t->sel_.head.entry = pos.entry;
      t->sel_.head.offset = b;
      t->sel_.head.length = 0;
    } else {
      t->sel_.anchor.entry = pos.entry;
      t->sel_.anchor.offset = t->drag_.origin_begin;
      t->sel_.anchor.length = 0;
      t->sel_.head.entry = pos.entry;
      t->sel_.head.offset = en;
      t->sel_.head.length = 0;
    }
    return;
  }
  if (t->click_.count >= 2) {
    RolltuiTextPos origin_first;
    origin_first.entry = t->drag_.origin_entry;
    origin_first.offset = t->drag_.origin_begin;
    origin_first.length = 0;
    if (rolltui_text_pos_less(&pos, &origin_first)) {
      t->sel_.anchor.entry = t->drag_.origin_entry;
      t->sel_.anchor.offset = t->drag_.origin_end;
      t->sel_.anchor.length = 0;
    } else {
      t->sel_.anchor = origin_first;
    }
  }
  t->sel_.head = pos;
}

static void begin_drag(RolltuiTranscript* t, int x, int y, int shift, unsigned long long now_ms,
                       const RolltuiDocument* doc) {
  RolltuiTextPos pos;
  char marker[ROLLTUI_MARKER_MAX];
  size_t marker_n;
  /* A click on the "▼ N more" marker scrolls to the bottom and re-engages follow. Checked
   * before the fold summary because it sits on the last row, over whatever is there. */
  marker_n = rolltui_scroll_marker_text(lines_below(t), t->text_area_.w, t->opt_.ambiguous_wide, marker,
                                        sizeof marker);
  if (marker_n && t->area_.h > 0 && y == t->area_.y + t->area_.h - 1) {
    const int mw = rolltui_u_display_width(t->u_, marker, marker_n, t->opt_.ambiguous_wide);
    const int right = t->text_area_.x + t->text_area_.w;
    if (x >= right - mw && x < right) {
      rolltui_transcript_scroll_to_bottom(t);
      memset(&t->click_, 0, sizeof t->click_);
      t->click_.x = -1;
      t->click_.y = -1;
      return;
    }
  }
  if (!rolltui_transcript_hit(t, x, y, &pos)) return;
  /* A click on a summary line toggles the fold and selects nothing; a code block's own header
   * and "▼ N more" rows do the same one rung down, over the WHOLE row. */
  {
    const int row = iclamp(y - t->area_.y, 0, imax(t->area_.h - 1, 0));
    const RowRef r = row_at(t, top_line(t) + (size_t)row);
    if (!r.gap && !r.beyond && r.entry < rolltui_document_count(doc)) {
      const RolltuiDocEntry* e = rolltui_document_at(doc, r.entry);
      const RolltuiEntryLayout* L = t->layouts_[r.entry];
      size_t i, count;
      const RolltuiMdCodeBlock* blocks;
      if (r.line == 0 && e->foldable) {
        rolltui_transcript_set_folded(t, e->id.p, e->id.n, !rolltui_transcript_is_folded(t, e));
        memset(&t->click_, 0, sizeof t->click_);
        t->click_.x = -1;
        t->click_.y = -1;
        return;
      }
      blocks = L->store ? rolltui_md_lines_code_blocks(L->store) : NULL;
      count = L->store ? rolltui_md_lines_code_block_count(L->store) : 0;
      for (i = 0; i < count; ++i) {
        if (blocks[i].header_line == r.line) {
          rolltui_transcript_set_code_folded(t, e->id.p, e->id.n, blocks[i].index, !blocks[i].folded);
          memset(&t->click_, 0, sizeof t->click_);
          t->click_.x = -1;
          t->click_.y = -1;
          return;
        }
        if (blocks[i].marker_line == r.line) { /* the cap's marker: show the rest of THIS block */
          rolltui_transcript_set_code_uncapped(t, e->id.p, e->id.n, blocks[i].index, 1);
          memset(&t->click_, 0, sizeof t->click_);
          t->click_.x = -1;
          t->click_.y = -1;
          return;
        }
      }
    }
  }
  if (shift) {
    if (!t->sel_.active) t->sel_.anchor = pos;
    t->sel_.head = pos;
    t->sel_.active = 1;
  } else {
    const int paired = t->click_.count > 0 && now_ms >= t->click_.at_ms &&
                       now_ms - t->click_.at_ms <= t->opt_.multi_click_ms && abs(x - t->click_.x) <= 1 &&
                       y == t->click_.y;
    t->click_.count = paired ? t->click_.count + 1 : 1;
    if (t->click_.count > 3) t->click_.count = 1;
    t->click_.at_ms = now_ms;
    t->click_.x = x;
    t->click_.y = y;
    if (t->click_.count >= 2) {
      const char* text;
      size_t n, b = 0, en = 0;
      layout_text(t->layouts_[pos.entry], &text, &n);
      unit_around(t, text, n, zmin(pos.offset, n), t->click_.count == 2, &b, &en);
      t->drag_.origin_entry = pos.entry;
      t->drag_.origin_begin = b;
      t->drag_.origin_end = en;
      t->sel_.anchor.entry = pos.entry;
      t->sel_.anchor.offset = b;
      t->sel_.anchor.length = 0;
      t->sel_.head.entry = pos.entry;
      t->sel_.head.offset = en;
      t->sel_.head.length = 0;
      t->sel_.active = 1;
    } else {
      t->sel_.anchor = pos;
      t->sel_.head = pos;
      t->sel_.active = 1;
    }
  }
  t->drag_.active = 1;
  t->drag_.outside = 0;
  t->drag_.x = x;
  t->drag_.y = y;
}

static void end_drag(RolltuiTranscript* t) {
  if (!t->drag_.active) return;
  t->drag_.active = 0;
  t->drag_.outside = 0;
  if (t->sel_.active && t->sel_.anchor.entry == t->sel_.head.entry &&
      t->sel_.anchor.offset == t->sel_.head.offset && t->sel_.anchor.length == t->sel_.head.length &&
      t->click_.count <= 1) {
    memset(&t->sel_, 0, sizeof t->sel_); /* a plain click selects nothing */
    return;
  }
  rolltui_transcript_copy_selection(t);
}

int rolltui_transcript_wants_tick(const RolltuiTranscript* t) { return t->drag_.active && t->drag_.outside; }

void rolltui_transcript_tick(RolltuiTranscript* t) {
  int dist, row;
  RolltuiTextPos pos;
  if (!rolltui_transcript_wants_tick(t)) return;
  dist = t->drag_.y < t->area_.y ? t->area_.y - t->drag_.y : t->drag_.y - (t->area_.y + t->area_.h - 1);
  dist = iclamp(dist, 1, imax(t->area_.h, 1));
  rolltui_transcript_scroll_by(t, t->drag_.y < t->area_.y ? -dist : dist);
  row = t->drag_.y < t->area_.y ? 0 : imax(t->area_.h - 1, 0);
  if (rolltui_transcript_hit(t, t->drag_.x, t->area_.y + row, &pos)) t->sel_.head = pos;
}

int rolltui_transcript_toggle_fold_nearest_top(RolltuiTranscript* t, const RolltuiDocument* doc) {
  const size_t top = top_line(t);
  int row;
  for (row = 0; row < t->area_.h; ++row) {
    const size_t g = top + (size_t)row;
    RowRef r;
    const RolltuiDocEntry* e;
    const RolltuiEntryLayout* L;
    const RolltuiMdCodeBlock* blocks;
    size_t i, count;
    if (g >= t->total_) break;
    r = row_at(t, g);
    if (r.gap || r.beyond || r.entry >= rolltui_document_count(doc)) continue;
    e = rolltui_document_at(doc, r.entry);
    /* An entry's summary row is line 0; a code block's header row is anywhere. Whichever is
     * nearer the top wins, which is what "the first visible fold" has always meant. */
    if (r.line == 0 && e->foldable) {
      rolltui_transcript_set_folded(t, e->id.p, e->id.n, !rolltui_transcript_is_folded(t, e));
      return 1;
    }
    L = t->layouts_[r.entry];
    blocks = L->store ? rolltui_md_lines_code_blocks(L->store) : NULL;
    count = L->store ? rolltui_md_lines_code_block_count(L->store) : 0;
    for (i = 0; i < count; ++i) {
      if (blocks[i].header_line != r.line) continue;
      rolltui_transcript_set_code_folded(t, e->id.p, e->id.n, blocks[i].index, !blocks[i].folded);
      return 1;
    }
  }
  return 0;
}

/* ---- events ----------------------------------------------------------------------------------------------------- */

static int action_is(const char* a, size_t n, const char* name) {
  return name && strlen(name) == n && memcmp(a, name, n) == 0;
}

int rolltui_transcript_handle(RolltuiTranscript* t, const RolltuiEvent* e, const RolltuiDocument* doc,
                              unsigned long long now_ms, const RolltuiBindings* bindings,
                              const RolltuiTranscriptActions* A) {
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent* m = &e->mouse;
    switch (m->kind) {
      case 4: /* WheelUp */
        rolltui_transcript_scroll_by(t, -(long)t->opt_.wheel_lines);
        return 1;
      case 5: /* WheelDown */
        rolltui_transcript_scroll_by(t, (long)t->opt_.wheel_lines);
        return 1;
      case 6: /* WheelLeft */
      case 7: /* WheelRight */
        return 0;
      case 0: /* Press */
        if (m->button != 1) return 0;
        begin_drag(t, m->x, m->y, m->shift, now_ms, doc);
        return 1;
      case 2: /* Drag */
        if (!t->drag_.active) return 0;
        drag_to(t, m->x, m->y);
        return 1;
      case 1: /* Release */
        if (!t->drag_.active) return 0;
        /* A release where the pointer already is changes nothing. */
        if (m->x != t->drag_.x || m->y != t->drag_.y) drag_to(t, m->x, m->y);
        end_drag(t);
        return 1;
      default: /* Move */
        return 0;
    }
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    size_t alen = 0;
    const char* a = rolltui_bindings_action_for(bindings, &e->key, "transcript", 10, &alen);
    if (!a) return 0;
    if (action_is(a, alen, A->page_up)) {
      rolltui_transcript_scroll_page(t, -1);
      return 1;
    }
    if (action_is(a, alen, A->page_down)) {
      rolltui_transcript_scroll_page(t, 1);
      return 1;
    }
    if (action_is(a, alen, A->top)) {
      rolltui_transcript_scroll_to_top(t);
      return 1;
    }
    if (action_is(a, alen, A->bottom)) {
      rolltui_transcript_scroll_to_bottom(t);
      return 1;
    }
    if (action_is(a, alen, A->line_up)) {
      rolltui_transcript_scroll_by(t, -1);
      return 1;
    }
    if (action_is(a, alen, A->line_down)) {
      rolltui_transcript_scroll_by(t, 1);
      return 1;
    }
    if (action_is(a, alen, A->find_next)) return rolltui_transcript_find_next(t);
    if (action_is(a, alen, A->find_prev)) return rolltui_transcript_find_prev(t);
    if (action_is(a, alen, A->fold)) return rolltui_transcript_toggle_fold_nearest_top(t, doc);
    if (action_is(a, alen, A->copy)) return rolltui_transcript_copy_selection(t);
    if (action_is(a, alen, A->clear_selection) && t->sel_.active) {
      rolltui_transcript_clear_selection(t);
      return 1;
    }
  }
  return 0;
}

/* ---- find accessors, stats and lifetime ---------------------------------------------------------------------------- */

const char* rolltui_transcript_query(const RolltuiTranscript* t, size_t* len) {
  return rolltui_str_get(&t->query_, len);
}

size_t rolltui_transcript_match_count(const RolltuiTranscript* t) { return t->matches_n; }

int rolltui_transcript_match_at(const RolltuiTranscript* t, size_t i, RolltuiFindMatch* out) {
  if (i >= t->matches_n) return 0;
  *out = t->matches_[i];
  return 1;
}

size_t rolltui_transcript_current_match_number(const RolltuiTranscript* t) {
  return t->has_current_ ? t->current_ + 1 : 0;
}

int rolltui_transcript_current_match(const RolltuiTranscript* t, RolltuiFindMatch* out) {
  const RolltuiFindMatch* m = current_match(t);
  if (!m) return 0;
  *out = *m;
  return 1;
}

void rolltui_transcript_stats(const RolltuiTranscript* t, RolltuiTranscriptStats* out) { *out = t->stats_; }

const RolltuiEntryLayout* rolltui_transcript_layout_of(const RolltuiTranscript* t, size_t entry) {
  return entry < t->layouts_n ? t->layouts_[entry] : NULL;
}

void rolltui_transcript_area(const RolltuiTranscript* t, RolltuiRect* out) { *out = t->area_; }
void rolltui_transcript_text_area(const RolltuiTranscript* t, RolltuiRect* out) { *out = t->text_area_; }

void rolltui_transcript_set_copy(RolltuiTranscript* t, RolltuiCopyFn fn, void* ctx) {
  t->copy_fn_ = fn;
  t->copy_ctx_ = ctx;
}

void rolltui_transcript_set_highlight(RolltuiTranscript* t, RolltuiMdHighlightFn fn, void* ctx) {
  t->highlight_ = fn;
  t->highlight_ctx_ = ctx;
  /* Bumped so a LATER highlighter re-lays everything instead of being silently ignored. */
  ++t->highlight_epoch_;
}

RolltuiTranscript* rolltui_transcript_new(void) {
  RolltuiTranscript* t = (RolltuiTranscript*)rolltui_mem_alloc(sizeof *t);
  memset(t, 0, sizeof *t);
  t->scroll_.follow = 1;
  t->opt_.tab_width = 8;
  t->opt_.gap = 1;
  t->opt_.wheel_lines = 3;
  t->opt_.multi_click_ms = 400;
  t->click_.x = -1;
  t->click_.y = -1;
  t->u_ = rolltui_u_scratch_new();
  t->wrap_ = rolltui_wrap_new();
  return t;
}

void rolltui_transcript_free(RolltuiTranscript* t) {
  size_t i;
  if (!t) return;
  map_free_all(&t->parse_, parsed_free);
  map_free_all(&t->cache_, cached_free);
  map_free_all(&t->find_text_, find_text_free);
  for (i = 0; i < rolltui_map_count(&t->fold_over_); ++i)
    rolltui_mem_free(rolltui_map_value_at(&t->fold_over_, i));
  rolltui_map_release(&t->fold_over_);
  map_free_all(&t->code_folds_, code_folds_free);
  rolltui_str_free(&t->pad_);
  rolltui_str_free(&t->scratch_);
  rolltui_str_free(&t->query_);
  rolltui_entry_layout_release(&t->unfolded_);
  rolltui_mem_free(t->layouts_);
  rolltui_mem_free(t->states_);
  rolltui_mem_free(t->starts_);
  rolltui_mem_free(t->matches_);
  rolltui_mem_free(t->gs_);
  rolltui_mem_free(t->srcs_);
  rolltui_mem_free(t->fold_scratch_);
  rolltui_u_scratch_free(t->u_);
  rolltui_wrap_free(t->wrap_);
  rolltui_mem_free(t);
}
