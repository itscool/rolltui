/* rolltui/c/rolltui_md_lines.c — the span store. Contract in rolltui_md_lines.h. */
#include "rolltui/c/rolltui_md_lines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_terminal.h"

/* A span WHILE IT IS BEING BUILT: offsets, never pointers, so a pool that grows cannot
 * leave a dangling span behind it (rolltui_md_lines.h, safety rule 2). */
typedef struct SpanRec {
  size_t text_off, text_n;
  size_t href_off, href_n;
  size_t src_off, src_n;
  int width;
  unsigned char role;
  unsigned char owns; /* 1: this span's pool ranges are its own and may be merged into or
                       * trimmed. 0: a `span_ref`, sharing another span's ranges. */
} SpanRec;

typedef struct ClampRec {
  size_t off, len;
} ClampRec;

typedef struct LineRec {
  size_t first, count;
  int width;
} LineRec;

struct RolltuiMdLines {
  char* bytes;
  size_t bytes_n, bytes_cap;
  uint32_t* srcs;
  size_t srcs_n, srcs_cap;

  SpanRec* rec;
  size_t rec_n, rec_cap;
  LineRec* lrec;
  size_t lrec_n, lrec_cap;

  /* Published by finish(); dead at the next append. */
  RolltuiMdSpan* span;
  size_t span_cap;
  RolltuiMdLine* line;
  size_t line_cap;
  size_t published;

  int open;
  size_t open_first;
  int open_width;

  char* text;
  size_t text_n, text_cap;

  /* What the render REPORTED. Two pools of their own: appending a lang or a message
   * between two spans must not break the span pool's tail-merge rule. */
  RolltuiMdCodeBlock* cb;
  size_t cb_n, cb_cap;
  size_t* cb_lang_off; /* parallel to `cb`: the offset lang_p is published from */
  size_t cb_lang_cap;
  char* cb_bytes;
  size_t cb_bytes_n, cb_bytes_cap;

  ClampRec* clamp; /* (offset, length) into clamp_bytes */
  size_t clamp_n, clamp_cap;
  char* clamp_bytes;
  size_t clamp_bytes_n, clamp_bytes_cap;

  char* chrome; /* interned prefix bytes — its OWN pool, see the header */
  size_t chrome_n, chrome_cap;
  RolltuiMdLines* aux; /* OWNED: made on first use, freed with this store */
  void* work;          /* OWNED: the filler's working memory, freed by `work_destroy` */
  void (*work_destroy)(void*);

  RolltuiUnicodeScratch* us;
  RolltuiUnicodeGrapheme* gs;
  size_t gs_cap;
};

static void die(const char* what) {
  fprintf(stderr, "rolltui: md_lines: %s\n", what);
  abort();
}

RolltuiMdLines* rolltui_md_lines_new(void) {
  RolltuiMdLines* L = (RolltuiMdLines*)rolltui_mem_alloc(sizeof *L);
  memset(L, 0, sizeof *L);
  return L;
}

void rolltui_md_lines_free(RolltuiMdLines* L) {
  if (!L) return;
  rolltui_mem_free(L->bytes);
  rolltui_mem_free(L->srcs);
  rolltui_mem_free(L->rec);
  rolltui_mem_free(L->lrec);
  rolltui_mem_free(L->span);
  rolltui_mem_free(L->line);
  rolltui_mem_free(L->text);
  rolltui_mem_free(L->cb);
  rolltui_mem_free(L->cb_lang_off);
  rolltui_mem_free(L->cb_bytes);
  rolltui_mem_free(L->clamp);
  rolltui_mem_free(L->clamp_bytes);
  rolltui_mem_free(L->chrome);
  if (L->work && L->work_destroy) L->work_destroy(L->work);
  rolltui_md_lines_free(L->aux);
  rolltui_mem_free(L->gs);
  rolltui_u_scratch_free(L->us);
  rolltui_mem_free(L);
}

void rolltui_md_lines_reset(RolltuiMdLines* L) {
  L->bytes_n = 0;
  L->srcs_n = 0;
  L->rec_n = 0;
  L->lrec_n = 0;
  L->published = 0;
  L->open = 0;
  L->open_first = 0;
  L->open_width = 0;
  L->text_n = 0;
  L->cb_n = 0;
  L->cb_bytes_n = 0;
  L->clamp_n = 0;
  L->clamp_bytes_n = 0;
  L->chrome_n = 0;
}

/* ---- working memory --------------------------------------------------------------------- */

RolltuiUnicodeScratch* rolltui_md_lines_scratch(RolltuiMdLines* L) {
  if (!L->us) L->us = rolltui_u_scratch_new();
  return L->us;
}

const RolltuiUnicodeGrapheme* rolltui_md_lines_clusters(RolltuiMdLines* L, const char* text, size_t n,
                                                        int ambiguous_wide, size_t* count, int* width) {
  size_t i, c;
  int w = 0;
  if (n == 0) {
    if (count) *count = 0;
    if (width) *width = 0;
    return L->gs;
  }
  /* GROWING, AMORTISED — appended to a cluster at a time by the algorithm below, and reused
   * by every span of every line. There can be no more clusters than bytes. */
  L->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(L->gs, &L->gs_cap, n, sizeof *L->gs);
  c = rolltui_u_graphemes(rolltui_md_lines_scratch(L), text, n, ambiguous_wide, L->gs);
  for (i = 0; i < c; ++i) w += L->gs[i].width;
  if (count) *count = c;
  if (width) *width = w;
  return L->gs;
}

/* ---- building ---------------------------------------------------------------------------- */

void rolltui_md_lines_open(RolltuiMdLines* L) {
  if (L->open) die("a second line opened while one was still open");
  L->open = 1;
  L->open_first = L->rec_n;
  L->open_width = 0;
  L->published = 0;
}

static SpanRec* push_rec(RolltuiMdLines* L) {
  L->rec = (SpanRec*)rolltui_grow(L->rec, &L->rec_cap, L->rec_n + 1, sizeof *L->rec);
  return &L->rec[L->rec_n++];
}

/* True when `s` is the tail of both pools, so appending to it stays contiguous. */
static int is_tail(const RolltuiMdLines* L, const SpanRec* s) {
  return s->owns && s->text_off + s->text_n == L->bytes_n && s->src_off + s->src_n == L->srcs_n;
}

void rolltui_md_lines_span(RolltuiMdLines* L, const char* text, size_t text_n, unsigned char role,
                           int ambiguous_wide, const uint32_t* sources, size_t src_n, const char* href,
                           size_t href_n) {
  size_t clusters = 0, i;
  int w = 0;
  int have;
  SpanRec* prev;
  if (!L->open) die("a span appended with no line open");
  if (text_n == 0) return;
  rolltui_md_lines_clusters(L, text, text_n, ambiguous_wide, &clusters, &w);
  /* A caller with no per-cluster offsets means chrome, and so does a caller whose array
   * does not describe these clusters — generated here rather than passed, so nobody builds
   * an array to say "none of this is text". */
  have = sources != NULL && src_n == clusters;

  prev = L->rec_n > L->open_first ? &L->rec[L->rec_n - 1] : NULL;
  if (prev && prev->role == role && prev->href_n == href_n &&
      (href_n == 0 || memcmp(L->bytes + prev->href_off, href, href_n) == 0) && is_tail(L, prev)) {
    L->bytes = (char*)rolltui_grow(L->bytes, &L->bytes_cap, L->bytes_n + text_n, 1);
    memcpy(L->bytes + L->bytes_n, text, text_n);
    L->bytes_n += text_n;
    prev->text_n += text_n;
    L->srcs = (uint32_t*)rolltui_grow(L->srcs, &L->srcs_cap, L->srcs_n + clusters, sizeof *L->srcs);
    for (i = 0; i < clusters; ++i) L->srcs[L->srcs_n + i] = have ? sources[i] : ROLLTUI_MD_NO_SOURCE;
    L->srcs_n += clusters;
    prev->src_n += clusters;
    prev->width += w;
  } else {
    SpanRec* s;
    size_t text_off, href_off, src_off;
    L->bytes = (char*)rolltui_grow(L->bytes, &L->bytes_cap, L->bytes_n + text_n + href_n, 1);
    /* THE HREF GOES FIRST, and that is load-bearing rather than arbitrary: the text has to
     * end at the pool's tail or the next span cannot merge into it, and a link's cells are
     * fed to this function ONE GRAPHEME AT A TIME by `layout_runs`. With the href after the
     * text, `is_tail` was false for every span that had one and a four-cell link became
     * four spans — which `markdown_test`'s "link text in md_link" caught on the first run. */
    href_off = L->bytes_n;
    if (href_n) {
      memcpy(L->bytes + L->bytes_n, href, href_n);
      L->bytes_n += href_n;
    }
    text_off = L->bytes_n;
    memcpy(L->bytes + L->bytes_n, text, text_n);
    L->bytes_n += text_n;
    L->srcs = (uint32_t*)rolltui_grow(L->srcs, &L->srcs_cap, L->srcs_n + clusters, sizeof *L->srcs);
    src_off = L->srcs_n;
    for (i = 0; i < clusters; ++i) L->srcs[L->srcs_n + i] = have ? sources[i] : ROLLTUI_MD_NO_SOURCE;
    L->srcs_n += clusters;
    s = push_rec(L);
    s->text_off = text_off;
    s->text_n = text_n;
    s->href_off = href_off;
    s->href_n = href_n;
    s->src_off = src_off;
    s->src_n = clusters;
    s->width = w;
    s->role = role;
    s->owns = 1;
  }
  L->open_width += w;
}

void rolltui_md_lines_span_ref(RolltuiMdLines* L, size_t index) {
  SpanRec src;
  SpanRec* s;
  if (!L->open) die("a span appended with no line open");
  if (index >= L->rec_n) die("span_ref past the end of the store");
  src = L->rec[index]; /* by value: push_rec may grow the array under it */
  if (src.text_n == 0) return;
  s = push_rec(L);
  *s = src;
  s->owns = 0;
  L->open_width += src.width;
}

void rolltui_md_lines_trim_trailing_spaces(RolltuiMdLines* L, int ambiguous_wide) {
  if (!L->open) die("a trim with no line open");
  while (L->rec_n > L->open_first) {
    SpanRec* s = &L->rec[L->rec_n - 1];
    size_t keep;
    if (!s->owns) return; /* another line's bytes: not ours to shorten */
    keep = s->text_n;
    while (keep > 0 && L->bytes[s->text_off + keep - 1] == ' ') --keep;
    if (keep == 0) {
      L->open_width -= s->width;
      if (is_tail(L, s)) {
        L->bytes_n = s->text_off;
        L->srcs_n = s->src_off;
      }
      --L->rec_n;
      continue;
    }
    if (keep < s->text_n) {
      size_t clusters = 0;
      int w = 0;
      const int dropped = (int)(s->text_n - keep);
      s->text_n = keep;
      s->width -= dropped; /* a trailing space is exactly one cell */
      L->open_width -= dropped;
      rolltui_md_lines_clusters(L, L->bytes + s->text_off, s->text_n, ambiguous_wide, &clusters, &w);
      if (is_tail(L, s)) {
        L->bytes_n = s->text_off + s->text_n;
        L->srcs_n = s->src_off + clusters;
      }
      s->src_n = clusters;
    }
    return;
  }
}

size_t rolltui_md_lines_close(RolltuiMdLines* L) {
  LineRec* l;
  if (!L->open) die("a line closed with none open");
  L->lrec = (LineRec*)rolltui_grow(L->lrec, &L->lrec_cap, L->lrec_n + 1, sizeof *L->lrec);
  l = &L->lrec[L->lrec_n++];
  l->first = L->open_first;
  l->count = L->rec_n - L->open_first;
  l->width = L->open_width;
  L->open = 0;
  return L->lrec_n - 1;
}

/* ---- reading ------------------------------------------------------------------------------ */

void rolltui_md_lines_finish(RolltuiMdLines* L) {
  size_t i;
  if (L->open) die("finish() while a line was still open");
  /* GROWING, EXACT — sized to what the store already holds and never appended to. */
  L->span = (RolltuiMdSpan*)rolltui_fit(L->span, &L->span_cap, L->rec_n, sizeof *L->span);
  L->line = (RolltuiMdLine*)rolltui_fit(L->line, &L->line_cap, L->lrec_n, sizeof *L->line);
  for (i = 0; i < L->rec_n; ++i) {
    const SpanRec* r = &L->rec[i];
    RolltuiMdSpan* s = &L->span[i];
    s->text_p = L->bytes + r->text_off;
    s->text_n = r->text_n;
    s->href_p = L->bytes + r->href_off;
    s->href_n = r->href_n;
    s->src_p = L->srcs + r->src_off;
    s->src_n = r->src_n;
    s->width = r->width;
    s->role = r->role;
  }
  for (i = 0; i < L->lrec_n; ++i) {
    const LineRec* r = &L->lrec[i];
    RolltuiMdLine* l = &L->line[i];
    l->span_p = L->span + r->first;
    l->span_n = r->count;
    l->width = r->width;
  }
  for (i = 0; i < L->cb_n; ++i) L->cb[i].lang_p = L->cb_bytes + L->cb_lang_off[i];
  L->published = 1;
}

size_t rolltui_md_lines_count(const RolltuiMdLines* L) { return L->lrec_n; }

const RolltuiMdLine* rolltui_md_lines_line(const RolltuiMdLines* L, size_t i) {
  if (!L->published) die("a line read before finish()");
  if (i >= L->lrec_n) die("a line read past the end");
  return &L->line[i];
}

const RolltuiMdLine* rolltui_md_lines_all(const RolltuiMdLines* L) {
  if (L->lrec_n == 0) return NULL;
  if (!L->published) die("the lines read before finish()");
  return L->line;
}

void rolltui_md_lines_span_range(const RolltuiMdLines* L, size_t i, size_t* first, size_t* count) {
  if (i >= L->lrec_n) die("a span range read past the end");
  if (first) *first = L->lrec[i].first;
  if (count) *count = L->lrec[i].count;
}

/* ---- the logical text ---------------------------------------------------------------------- */

void rolltui_md_lines_text_append(RolltuiMdLines* L, const char* s, size_t n) {
  if (n == 0) return;
  L->text = (char*)rolltui_grow(L->text, &L->text_cap, L->text_n + n, 1);
  memcpy(L->text + L->text_n, s, n);
  L->text_n += n;
}

void rolltui_md_lines_text_set(RolltuiMdLines* L, const char* s, size_t n) {
  L->text_n = 0;
  rolltui_md_lines_text_append(L, s, n);
}

size_t rolltui_md_lines_text_size(const RolltuiMdLines* L) { return L->text_n; }

const char* rolltui_md_lines_text(const RolltuiMdLines* L) { return L->text ? L->text : ""; }

void rolltui_md_lines_text_pop(RolltuiMdLines* L) {
  if (L->text_n) --L->text_n;
}

/* ---- what the render reported ---------------------------------------------------------------- */

void rolltui_md_lines_add_code_block(RolltuiMdLines* L, const RolltuiMdCodeBlock* b) {
  RolltuiMdCodeBlock* out;
  L->cb = (RolltuiMdCodeBlock*)rolltui_grow(L->cb, &L->cb_cap, L->cb_n + 1, sizeof *L->cb);
  L->cb_lang_off = (size_t*)rolltui_grow(L->cb_lang_off, &L->cb_lang_cap, L->cb_n + 1, sizeof *L->cb_lang_off);
  L->cb_bytes = (char*)rolltui_grow(L->cb_bytes, &L->cb_bytes_cap, L->cb_bytes_n + b->lang_n + 1, 1);
  L->cb_lang_off[L->cb_n] = L->cb_bytes_n;
  if (b->lang_n) memcpy(L->cb_bytes + L->cb_bytes_n, b->lang_p, b->lang_n);
  L->cb_bytes_n += b->lang_n;
  out = &L->cb[L->cb_n++];
  *out = *b;
  out->lang_p = NULL; /* published by finish() */
  L->published = 0;
}

size_t rolltui_md_lines_code_block_count(const RolltuiMdLines* L) { return L->cb_n; }

const RolltuiMdCodeBlock* rolltui_md_lines_code_blocks(const RolltuiMdLines* L) {
  if (L->cb_n == 0) return NULL;
  if (!L->published) die("the code blocks read before finish()");
  return L->cb;
}

void rolltui_md_lines_shift_code_blocks(RolltuiMdLines* L, size_t by) {
  size_t i;
  for (i = 0; i < L->cb_n; ++i) {
    if (L->cb[i].header_line != ROLLTUI_MD_NO_LINE) L->cb[i].header_line += by;
    if (L->cb[i].marker_line != ROLLTUI_MD_NO_LINE) L->cb[i].marker_line += by;
  }
}

void rolltui_md_lines_clear_code_blocks(RolltuiMdLines* L) {
  L->cb_n = 0;
  L->cb_bytes_n = 0;
}

void rolltui_md_lines_add_clamped(RolltuiMdLines* L, const char* msg, size_t n) {
  L->clamp = (ClampRec*)rolltui_grow(L->clamp, &L->clamp_cap, L->clamp_n + 1, sizeof *L->clamp);
  L->clamp_bytes = (char*)rolltui_grow(L->clamp_bytes, &L->clamp_bytes_cap, L->clamp_bytes_n + n + 1, 1);
  if (n) memcpy(L->clamp_bytes + L->clamp_bytes_n, msg, n);
  L->clamp[L->clamp_n].off = L->clamp_bytes_n;
  L->clamp[L->clamp_n].len = n;
  L->clamp_bytes_n += n;
  ++L->clamp_n;
}

size_t rolltui_md_lines_clamped_count(const RolltuiMdLines* L) { return L->clamp_n; }

void rolltui_md_lines_clamped_at(const RolltuiMdLines* L, size_t i, const char** p, size_t* n) {
  if (i >= L->clamp_n) die("a clamped message read past the end");
  if (p) *p = L->clamp_bytes + L->clamp[i].off;
  if (n) *n = L->clamp[i].len;
}

/* ---- interned chrome, and the second store -------------------------------------------------- */

size_t rolltui_md_lines_intern(RolltuiMdLines* L, const char* s, size_t n) {
  const size_t off = L->chrome_n;
  if (n == 0) return off;
  L->chrome = (char*)rolltui_grow(L->chrome, &L->chrome_cap, L->chrome_n + n, 1);
  memcpy(L->chrome + L->chrome_n, s, n);
  L->chrome_n += n;
  return off;
}

const char* rolltui_md_lines_interned(const RolltuiMdLines* L, size_t off) {
  return L->chrome ? L->chrome + off : "";
}

RolltuiMdLines* rolltui_md_lines_aux(RolltuiMdLines* L) {
  if (!L->aux) L->aux = rolltui_md_lines_new();
  return L->aux;
}

void* rolltui_md_lines_work(RolltuiMdLines* L, void* (*make)(void), void (*destroy)(void*)) {
  if (!L->work) {
    L->work = make();
    L->work_destroy = destroy;
  }
  return L->work;
}
