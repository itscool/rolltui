/* rolltui/c/rolltui_wrap.c — the C side of the wrap engine. See rolltui_wrap.h for the
 * boundary's rules and rolltui/Wrap.hpp for the wrapping rules themselves; `WrapCpp.cpp` is
 * the other implementation of the same six functions, and the property tests at every width
 * over a corpus and 3,600 seeded random cases are the oracle for both.
 *
 * Everything here allocates through `rolltui_mem_*` (rolltui/Memory.hpp's C face), which is
 * CLAUDE.md's rule and is also the point: in C the entry point is TOTAL, where in C++ it only
 * ever saw the library's own explicit allocations.
 *
 * THREE BUFFERS AND A HANDLE, AND NO STATE ANYWHERE ELSE. Every line's bytes end to end,
 * every line's clusters end to end, and a record per line saying where its slice starts —
 * plus the decode and UAX #14/#29 scratch, which lives here too. There is no `static` and no
 * thread-local in this file: a handle is the only place a buffer can be, which is what lets
 * the C++ side put one in a `Scratch` and lend it while another sits in a caller's hands.
 *
 * THE LINE UNDER CONSTRUCTION IS THE TAIL OF THOSE SAME BUFFERS, which is the trick the whole
 * file rests on: emitting a line does not copy its bytes anywhere, it just records where they
 * already are and closes the gap left by the spaces dropped at the break. */
#include "rolltui/rolltui.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_terminal.h"

/* Where one line's slice of the two shared buffers is. Owns nothing. */
typedef struct {
  size_t text_off, text_len, g_off, g_count;
  int width, indent;
  unsigned char hard;
} Rec;

/* One source grapheme cluster, before it is placed. `brk` is a ROLLTUI_BREAK_* value. */
typedef struct {
  size_t byte0, byte1;
  int width;
  unsigned char brk, space, tab, newline;
} Cluster;

struct RolltuiWrapLines {
  char* text;
  size_t text_len, text_cap;
  RolltuiWrapGrapheme* gs;
  size_t gs_len, gs_cap;
  Rec* lines;
  size_t line_count, line_cap;
  /* PACKED: the three buffers above are INTERIOR to this handle's own block, carved out by
   * `rolltui_wrap_clone`. The invariant is all-or-nothing — either all three are interior
   * and none is freed separately, or all three are heap-or-NULL and each is. */
  unsigned char packed;

  /* Scratch, reused across every wrap into this handle. */
  /* One cap each rather than one shared between three: a cap per buffer is what the closed
   * set's `rolltui_grow` takes, and paying two extra size_t beats hand-writing the growth. */
  RolltuiCodepoint* cps;
  size_t cps_cap;
  size_t* coff;
  size_t coff_cap;
  size_t* clen;
  size_t clen_cap;
  unsigned char* brk;
  size_t brk_cap;
  unsigned char* bounds;
  size_t bounds_cap;
  Cluster* clusters;
  size_t cluster_count, cluster_cap;
  /* The Unicode algorithms' working memory, owned here for the same reason everything else in
   * this handle is: a wrap is the only thing that asks for it, so a caller that keeps one
   * handle keeps one of these, and no hidden per-thread state exists anywhere in the chain.
   *
   * CREATED LAZILY, and the budget is why: eagerly it cost one allocation per handle, and
   * `wrap()` mints a fresh handle per call to hand the lines over — a resize frame went up by
   * exactly 281, the number of `wrap()` calls in it. A handed-over result is READ, never
   * wrapped into, so it never needs this. */
  RolltuiUnicodeScratch* uni;
};

/* ---- lifetime ---------------------------------------------------------------------------- */

RolltuiWrapLines* rolltui_wrap_new(void) {
  RolltuiWrapLines* w = (RolltuiWrapLines*)rolltui_mem_alloc(sizeof(RolltuiWrapLines));
  memset(w, 0, sizeof *w);
  return w;
}

void rolltui_wrap_free(RolltuiWrapLines* w) {
  if (!w) return;
  if (!w->packed) { /* when packed these three are interior; the final free below takes them */
    rolltui_mem_free(w->text);
    rolltui_mem_free(w->gs);
    rolltui_mem_free(w->lines);
  }
  rolltui_mem_free(w->cps);
  rolltui_mem_free(w->coff);
  rolltui_mem_free(w->clen);
  rolltui_mem_free(w->brk);
  rolltui_mem_free(w->bounds);
  rolltui_mem_free(w->clusters);
  rolltui_u_scratch_free(w->uni);
  rolltui_mem_free(w);
}

void rolltui_wrap_reset(RolltuiWrapLines* w) {
  w->text_len = 0;
  w->gs_len = 0;
  w->line_count = 0;
}

/* ---- the engine -------------------------------------------------------------------------- */

/* The engine's working state. C has no lambdas, so what `WrapCpp.cpp` writes as six captures
 * is one struct passed to six functions — the port's most visible tax, and small. */
typedef struct {
  RolltuiWrapLines* w;
  RolltuiWrapOptions opt;
  int width, nothing;
  size_t committed_text, committed_gs; /* what belongs to lines already emitted */
  int cur_width, cur_indent, avail;
  int any_emitted, pending, dropping;
  size_t last_opportunity;
} State;

static size_t cur_count(const State* s) { return s->w->gs_len - s->committed_gs; }
static size_t cur_bytes(const State* s) { return s->w->text_len - s->committed_text; }
static RolltuiWrapGrapheme* cur_at(const State* s, size_t k) { return &s->w->gs[s->committed_gs + k]; }

static int indent_for(const State* s, int first) {
  int ind = first ? s->opt.first_indent : s->opt.hanging_indent;
  if (ind < 0) ind = 0;
  if (!s->nothing && ind >= s->width) ind = s->width - 1; /* keep at least one cell of text */
  return ind;
}

/* Emits the first `head` clusters of the current line as a finished line, then makes the
 * current line the clusters from `keep` on — the ones between are dropped (trailing spaces at
 * a soft break). A whole line is `head == keep == cur_count()`. */
static void emit(State* s, size_t head, size_t keep, unsigned char hard) {
  RolltuiWrapLines* w = s->w;
  const size_t n = cur_count(s);
  const size_t head_bytes = (head == n) ? cur_bytes(s) : cur_at(s, head)->offset;
  const size_t tail0 = (keep == n) ? cur_bytes(s) : cur_at(s, keep)->offset;
  const size_t tail_bytes = cur_bytes(s) - tail0;
  const size_t tail_g = n - keep;
  int head_width = 0, dropped_width = 0;
  size_t k;
  Rec* r;

  for (k = 0; k < keep; ++k) {
    if (k < head) head_width += cur_at(s, k)->width;
    else dropped_width += cur_at(s, k)->width;
  }
  w->lines = rolltui_grow(w->lines, &w->line_cap, w->line_count + 1, sizeof *w->lines);
  r = &w->lines[w->line_count++];
  r->text_off = s->committed_text;
  r->text_len = head_bytes;
  r->g_off = s->committed_gs;
  r->g_count = head;
  r->width = head_width;
  r->indent = s->cur_indent;
  r->hard = hard;

  /* Close the gap between the head and the tail. Nothing is copied out: the head's bytes are
   * already where the emitted line wants them, and the tail slides down to meet them. */
  if (tail_bytes > 0 && tail0 != head_bytes)
    memmove(w->text + s->committed_text + head_bytes, w->text + s->committed_text + tail0, tail_bytes);
  if (tail_g > 0 && keep != head)
    memmove(w->gs + s->committed_gs + head, w->gs + s->committed_gs + keep, tail_g * sizeof *w->gs);
  w->text_len = s->committed_text + head_bytes + tail_bytes;
  w->gs_len = s->committed_gs + head + tail_g;
  s->committed_text += head_bytes;
  s->committed_gs += head;
  for (k = 0; k < tail_g; ++k) cur_at(s, k)->offset -= tail0;
  s->cur_width -= head_width + dropped_width;

  s->cur_indent = indent_for(s, 0);
  s->avail = s->nothing ? 0 : s->width - s->cur_indent;
  s->any_emitted = 1;
  s->pending = tail_g > 0;
  s->last_opportunity = 0;
  s->dropping = 0;
}

static void emit_all(State* s, unsigned char hard) { emit(s, cur_count(s), cur_count(s), hard); }

static void append(State* s, const char* bytes, size_t nb, size_t source_offset, int gw, unsigned char space) {
  RolltuiWrapLines* w = s->w;
  RolltuiWrapGrapheme* g;
  w->gs = rolltui_grow(w->gs, &w->gs_cap, w->gs_len + 1, sizeof *w->gs);
  g = &w->gs[w->gs_len++];
  g->offset = w->text_len - s->committed_text; /* the text append below has not happened yet */
  g->length = nb;
  g->source_offset = source_offset;
  g->width = gw;
  g->space = space;
  w->text = rolltui_grow(w->text, &w->text_cap, w->text_len + nb, 1);
  memcpy(w->text + w->text_len, bytes, nb);
  w->text_len += nb;
  s->cur_width += gw;
  s->pending = 1;
}

static void drop_trailing_spaces(State* s) {
  while (cur_count(s) > 0) {
    RolltuiWrapGrapheme* g = cur_at(s, cur_count(s) - 1);
    if (!g->space) break;
    s->cur_width -= g->width;
    s->w->text_len = s->committed_text + g->offset;
    s->w->gs_len--;
  }
  if (s->last_opportunity > cur_count(s)) s->last_opportunity = cur_count(s);
}

void rolltui_wrap(RolltuiWrapLines* w, const char* utf8, size_t len, int width, RolltuiWrapOptions opt) {
  State s;
  size_t n, i, start, gi;

  /* A CLONED handle being wrapped into: its three buffers are interior to its own block and
   * cannot be grown or freed, so they are dropped and the handle starts over on the heap.
   * The interior space is abandoned until the handle is freed. Nothing in the library does
   * this — `wrap()` hands clones out to be read — but it is public API and so it is correct
   * rather than merely unlikely. */
  if (w->packed) {
    w->text = NULL;
    w->gs = NULL;
    w->lines = NULL;
    w->text_cap = w->gs_cap = w->line_cap = 0;
    w->packed = 0;
  }

  /* Decode straight into three parallel arrays. The seam narrowed what the engine asks for
   * (rolltui_unicode.h): there is no array of structs and therefore no copy loop to get a
   * contiguous code-point array back out of one. */
  w->cps = rolltui_grow(w->cps, &w->cps_cap, len, sizeof *w->cps);
  w->coff = rolltui_grow(w->coff, &w->coff_cap, len, sizeof *w->coff);
  w->clen = rolltui_grow(w->clen, &w->clen_cap, len, sizeof *w->clen);
  n = rolltui_u_decode_utf8(utf8, len, w->cps, w->coff, w->clen);
  w->brk = rolltui_grow(w->brk, &w->brk_cap, n + 1, sizeof *w->brk);
  w->bounds = rolltui_grow(w->bounds, &w->bounds_cap, n + 1, sizeof *w->bounds);
  if (!w->uni) w->uni = rolltui_u_scratch_new(); /* this handle is being used as an engine */
  rolltui_u_line_break_opportunities(w->uni, w->cps, n, w->brk);
  rolltui_u_grapheme_boundaries(w->uni, w->cps, n, w->bounds);

  /* One entry per grapheme cluster, in source order. */
  w->cluster_count = 0;
  start = 0;
  for (i = 0; i < n; ++i) {
    Cluster* c;
    RolltuiCodepoint c0;
    if (!w->bounds[i + 1]) continue;
    w->clusters = rolltui_grow(w->clusters, &w->cluster_cap, w->cluster_count + 1, sizeof *w->clusters);
    c = &w->clusters[w->cluster_count++];
    c0 = w->cps[start];
    c->byte0 = w->coff[start];
    c->byte1 = w->coff[i] + w->clen[i];
    c->width = rolltui_u_cluster_width(w->cps + start, i + 1 - start, opt.ambiguous_wide);
    c->brk = w->brk[start];
    c->space = (i == start && c0 == 0x20) ? 1u : 0u;
    c->tab = (i == start && c0 == 0x09) ? 1u : 0u;
    /* CR LF is one cluster (GB3). */
    c->newline = (c0 == 0x0A || c0 == 0x0B || c0 == 0x0C || c0 == 0x0D || c0 == 0x85 || c0 == 0x2028 ||
                  c0 == 0x2029)
                     ? 1u
                     : 0u;
    start = i + 1;
  }

  s.w = w;
  s.opt = opt;
  s.width = width;
  s.nothing = width <= 0;
  rolltui_wrap_reset(w);
  s.committed_text = 0;
  s.committed_gs = 0;
  s.cur_width = 0;
  s.cur_indent = indent_for(&s, 1);
  s.avail = s.nothing ? 0 : width - s.cur_indent;
  s.any_emitted = 0;
  s.pending = 0;
  s.dropping = 0;
  s.last_opportunity = 0;

  for (gi = 0; gi < w->cluster_count; ++gi) {
    const Cluster* g = &w->clusters[gi];
    const char* bytes;
    size_t nb;
    if (g->newline) { /* terminates the current line, even an empty one */
      emit_all(&s, 1);
      continue;
    }
    if (g->brk == ROLLTUI_BREAK_ALLOWED) s.last_opportunity = cur_count(&s);
    if (g->width > 0 || g->tab || g->space) s.pending = 1;
    if (g->tab) {
      int tw, stop, k;
      if (s.nothing) continue;
      tw = opt.tab_width > 0 ? opt.tab_width : 8;
      stop = tw - (s.cur_width % tw);
      for (k = 0; k < stop; ++k) {
        if (s.cur_width + 1 > s.avail) { s.dropping = 1; break; } /* overflowing spaces: dropped */
        if (s.dropping) break;
        append(&s, " ", 1, g->byte0, 1, 1);
      }
      continue;
    }
    if (g->width == 0) continue; /* draws nothing */
    if (s.nothing) continue;
    bytes = utf8 + g->byte0;
    nb = g->byte1 - g->byte0;
    if (g->space) {
      if (s.dropping || s.cur_width + g->width > s.avail) {
        s.dropping = 1;
        continue;
      }
      append(&s, bytes, nb, g->byte0, g->width, 1);
      continue;
    }
    if (s.dropping) {
      /* A non-space after dropped spaces starts the next line (SP ÷ is the opportunity). */
      drop_trailing_spaces(&s);
      emit_all(&s, 0);
    }
    if (s.cur_width + g->width > s.avail && cur_count(&s) > 0) {
      if (s.last_opportunity > 0 && s.last_opportunity < cur_count(&s)) {
        /* Cut at the last opportunity: the head goes out, the tail stays. The head's
         * trailing spaces are dropped by emitting fewer clusters than the cut point. */
        const size_t keep = s.last_opportunity;
        size_t head = keep;
        while (head > 0 && cur_at(&s, head - 1)->space) --head;
        emit(&s, head, keep, 0);
        if (g->brk == ROLLTUI_BREAK_ALLOWED) s.last_opportunity = cur_count(&s);
        /* g still has to be placed; the tail holds no opportunity but its start, so if it
         * does not fit now the only remaining cut is right before it. */
        if (s.cur_width + g->width > s.avail && cur_count(&s) > 0) {
          drop_trailing_spaces(&s);
          emit_all(&s, 0);
        }
      } else {
        /* No opportunity inside the line (or only at its start): hard-break here. */
        drop_trailing_spaces(&s);
        emit_all(&s, 0);
      }
    }
    append(&s, bytes, nb, g->byte0, g->width, 0);
  }
  /* End of text: the unterminated last segment is a line if it has content, or if it is the
   * whole (empty) text. A trailing newline has already emitted its line. */
  if (s.pending || !s.any_emitted) emit_all(&s, 1);
}

/* ONE ALLOCATION FOR THE WHOLE RESULT — strategy 4, PACKED (rolltui_alloc.h). See the header
 * for why this is the shape and not four separate blocks. The three `_Static_assert`s that
 * used to prove the offsets aligned are gone with it: `rolltui_pack_add` aligns every section
 * for any type, so alignment stopped being this file's problem. */
RolltuiWrapLines* rolltui_wrap_clone(const RolltuiWrapLines* src) {
  RolltuiPack pk;
  size_t off_lines, off_gs, off_text;
  unsigned char* block;
  RolltuiWrapLines* w;

  rolltui_pack_begin(&pk, sizeof(RolltuiWrapLines));
  off_lines = rolltui_pack_add(&pk, src->line_count, sizeof(Rec));
  off_gs = rolltui_pack_add(&pk, src->gs_len, sizeof(RolltuiWrapGrapheme));
  /* At least one byte for the text, so an empty line's borrowed pointer is inside the block
   * rather than one past its end. */
  off_text = rolltui_pack_add(&pk, src->text_len ? src->text_len : 1, 1);
  block = (unsigned char*)rolltui_pack_alloc(&pk);
  w = (RolltuiWrapLines*)block;

  memset(w, 0, sizeof *w); /* the buffers stay NULL: a clone is read, never wrapped into */
  w->packed = 1;
  w->lines = (Rec*)(void*)(block + off_lines);
  w->gs = (RolltuiWrapGrapheme*)(void*)(block + off_gs);
  w->text = (char*)(block + off_text);
  w->line_count = w->line_cap = src->line_count;
  w->gs_len = w->gs_cap = src->gs_len;
  w->text_len = w->text_cap = src->text_len;
  if (src->line_count) memcpy(w->lines, src->lines, src->line_count * sizeof(Rec));
  if (src->gs_len) memcpy(w->gs, src->gs, src->gs_len * sizeof(RolltuiWrapGrapheme));
  if (src->text_len) memcpy(w->text, src->text, src->text_len);
  return w;
}

/* ---- reading the lines ------------------------------------------------------------------- */

size_t rolltui_wrap_line_count(const RolltuiWrapLines* w) { return w->line_count; }

void rolltui_wrap_line(const RolltuiWrapLines* w, size_t i, const char** text, size_t* text_len,
                       const RolltuiWrapGrapheme** graphemes, size_t* grapheme_count, int* width, int* indent,
                       int* hard) {
  static const char kEmpty[1] = {0};
  const Rec* r = &w->lines[i];
  /* BORROWS into the handle. An empty line still answers with a readable pointer rather than
   * NULL, the same rule `rolltui_frame_glyph` follows for a continuation cell. */
  *text = w->text ? w->text + r->text_off : kEmpty;
  *text_len = r->text_len;
  *graphemes = w->gs + r->g_off;
  *grapheme_count = r->g_count;
  *width = r->width;
  *indent = r->indent;
  *hard = r->hard ? 1 : 0;
}
