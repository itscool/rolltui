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
#include "rolltui/c/rolltui_wrap.h"

#include <string.h>

#include "rolltui/c/rolltui_unicode.h"

/* rolltui::mem, as C. Declared here rather than included, so this file needs no C++ header. */
extern void* rolltui_mem_alloc(size_t bytes);
extern void* rolltui_mem_realloc(void* p, size_t bytes);
extern void rolltui_mem_free(void* p);

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

  /* Scratch, reused across every wrap into this handle. */
  RolltuiCodepoint* cps;
  size_t* coff;
  size_t* clen;
  size_t char_cap; /* the three above share it: they are always sized together */
  unsigned char* brk;
  unsigned char* bounds;
  size_t flag_cap; /* likewise these two */
  Cluster* clusters;
  size_t cluster_count, cluster_cap;
};

/* Grows a buffer to hold `need` elements, doubling, and never shrinks. `rolltui_mem_realloc`
 * takes NULL, so a first use and a growth are the same call. */
static void* reserve(void* p, size_t* cap, size_t need, size_t elem) {
  size_t c;
  if (need <= *cap) return p;
  c = *cap ? *cap : 16;
  while (c < need) c *= 2;
  *cap = c;
  return rolltui_mem_realloc(p, c * elem);
}

/* ---- lifetime ------------------------------------------------------------------------- */

RolltuiWrapLines* rolltui_wrap_new(void) {
  RolltuiWrapLines* w = (RolltuiWrapLines*)rolltui_mem_alloc(sizeof(RolltuiWrapLines));
  memset(w, 0, sizeof *w);
  return w;
}

void rolltui_wrap_free(RolltuiWrapLines* w) {
  if (!w) return;
  rolltui_mem_free(w->text);
  rolltui_mem_free(w->gs);
  rolltui_mem_free(w->lines);
  rolltui_mem_free(w->cps);
  rolltui_mem_free(w->coff);
  rolltui_mem_free(w->clen);
  rolltui_mem_free(w->brk);
  rolltui_mem_free(w->bounds);
  rolltui_mem_free(w->clusters);
  rolltui_mem_free(w);
}

void rolltui_wrap_reset(RolltuiWrapLines* w) {
  w->text_len = 0;
  w->gs_len = 0;
  w->line_count = 0;
}

/* ---- the engine ------------------------------------------------------------------------ */

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
  w->lines = reserve(w->lines, &w->line_cap, w->line_count + 1, sizeof *w->lines);
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
  w->gs = reserve(w->gs, &w->gs_cap, w->gs_len + 1, sizeof *w->gs);
  g = &w->gs[w->gs_len++];
  g->offset = w->text_len - s->committed_text; /* the text append below has not happened yet */
  g->length = nb;
  g->source_offset = source_offset;
  g->width = gw;
  g->space = space;
  w->text = reserve(w->text, &w->text_cap, w->text_len + nb, 1);
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

  /* Decode straight into three parallel arrays. The seam narrowed what the engine asks for
   * (rolltui_unicode.h): there is no array of structs and therefore no copy loop to get a
   * contiguous code-point array back out of one. */
  if (len > w->char_cap) {
    size_t c = w->char_cap ? w->char_cap : 16;
    while (c < len) c *= 2;
    w->cps = (RolltuiCodepoint*)rolltui_mem_realloc(w->cps, c * sizeof *w->cps);
    w->coff = (size_t*)rolltui_mem_realloc(w->coff, c * sizeof *w->coff);
    w->clen = (size_t*)rolltui_mem_realloc(w->clen, c * sizeof *w->clen);
    w->char_cap = c;
  }
  n = rolltui_u_decode_utf8(utf8, len, w->cps, w->coff, w->clen);
  if (n + 1 > w->flag_cap) {
    size_t c = w->flag_cap ? w->flag_cap : 16;
    while (c < n + 1) c *= 2;
    w->brk = (unsigned char*)rolltui_mem_realloc(w->brk, c);
    w->bounds = (unsigned char*)rolltui_mem_realloc(w->bounds, c);
    w->flag_cap = c;
  }
  rolltui_u_line_break_opportunities(w->cps, n, w->brk);
  rolltui_u_grapheme_boundaries(w->cps, n, w->bounds);

  /* One entry per grapheme cluster, in source order. */
  w->cluster_count = 0;
  start = 0;
  for (i = 0; i < n; ++i) {
    Cluster* c;
    RolltuiCodepoint c0;
    if (!w->bounds[i + 1]) continue;
    w->clusters = reserve(w->clusters, &w->cluster_cap, w->cluster_count + 1, sizeof *w->clusters);
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

void rolltui_wrap_copy(RolltuiWrapLines* dst, const RolltuiWrapLines* src) {
  /* Three copies, whatever the line count: the buffers keep their capacity and the lines
   * carry offsets rather than storage. */
  dst->text = reserve(dst->text, &dst->text_cap, src->text_len, 1);
  if (src->text_len) memcpy(dst->text, src->text, src->text_len);
  dst->text_len = src->text_len;
  dst->gs = reserve(dst->gs, &dst->gs_cap, src->gs_len, sizeof *dst->gs);
  if (src->gs_len) memcpy(dst->gs, src->gs, src->gs_len * sizeof *dst->gs);
  dst->gs_len = src->gs_len;
  dst->lines = reserve(dst->lines, &dst->line_cap, src->line_count, sizeof *dst->lines);
  if (src->line_count) memcpy(dst->lines, src->lines, src->line_count * sizeof *dst->lines);
  dst->line_count = src->line_count;
}

/* ---- reading the lines -------------------------------------------------------------------- */

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
