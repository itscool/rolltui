/* rolltui/c/rolltui_frame_ops.c — see rolltui_frame_ops.h. Compiled into both
 * configurations: one implementation of the three loops, not one per language. */
#include "rolltui/rolltui.h"

#include "rolltui/c/rolltui_frame_ops.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_termfacts.h"

struct RolltuiDrawScratch {
  RolltuiUnicodeScratch* u;       /* the Unicode module's own working memory — its role */
  RolltuiUnicodeGrapheme* gs;     /* the cluster array — this module's role */
  size_t gs_cap;
};

RolltuiDrawScratch* rolltui_draw_scratch_new(void) {
  RolltuiDrawScratch* s = (RolltuiDrawScratch*)rolltui_mem_alloc(sizeof *s);
  s->u = rolltui_u_scratch_new();
  s->gs = NULL;
  s->gs_cap = 0;
  return s;
}

void rolltui_draw_scratch_free(RolltuiDrawScratch* s) {
  if (!s) return;
  rolltui_u_scratch_free(s->u);
  rolltui_mem_free(s->gs);
  rolltui_mem_free(s);
}

/* How many leading bytes of `s` are each a whole cluster one cell wide: a printable ASCII byte followed by another ASCII byte (or the
 * end). A byte followed by a non-ASCII one may lead a cluster (`e` and a combining acute, a keycap), so the run stops before it; no two
 * ASCII bytes join except CR LF, and neither is printable. */
static size_t ascii_run(const char* s, size_t n) {
  size_t i = 0;
  while (i < n) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c > 0x7E) break;
    if (i + 1 < n && (unsigned char)s[i + 1] >= 0x80) break;
    ++i;
  }
  return i;
}

int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link) {
  const int w = rolltui_frame_width(f);
  size_t count, i;
  int used = 0;
  size_t ascii;
  if (y < 0 || y >= rolltui_frame_height(f) || len == 0) return 0;
  /* THE ASCII RUN FIRST, a byte a cell, with no cluster walk: most text drawn is ASCII, and the walk is what a cell cost. */
  ascii = ascii_run(utf8, len);
  if (ascii && x >= 0) {
    size_t take = ascii;
    if ((size_t)max_cells < take) take = max_cells > 0 ? (size_t)max_cells : 0;
    used = rolltui_frame_put_ascii(f, x, y, utf8, (int)take, style, link);
    if (used < (int)ascii) return used; /* stopped by `max_cells` or the edge */
  } else {
    for (i = 0; i < ascii; ++i) {
      if (used >= max_cells || x + used >= w) return used;
      used += rolltui_frame_put(f, x + used, y, utf8 + i, 1, 1, style, link);
    }
  }
  if (ascii == len) return used;
  utf8 += ascii;
  len -= ascii;
  /* GROWING, AMORTISED: there can be no more clusters than bytes, and this is the hottest
   * caller of the cluster walk in the library — every string any widget draws. */
  s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, len, sizeof *s->gs);
  count = rolltui_u_graphemes(s->u, utf8, len, ambiguous_wide, s->gs);
  for (i = 0; i < count; ++i) {
    const RolltuiUnicodeGrapheme* g = &s->gs[i];
    if (g->width <= 0) continue;
    if (used + g->width > max_cells || x + used >= w) break;
    if (g->width == 2 && x + used + 1 >= w) break; /* never a half glyph at the edge */
    used += rolltui_frame_put(f, x + used, y, utf8 + g->offset, g->length, g->width, style, link);
  }
  return used;
}

#define ELLIPSIS "\xe2\x80\xa6"

/* Where to cut `utf8` to `max` cells: keep [0, head) and [tail, len), with `marker` between (its cells in `*ew_out`). 0 when it fits
 * whole. A middle cut gives the head `split` percent of the room (0: half, its odd cell to the head). */
static int cut_points_with(RolltuiDrawScratch* s, const char* utf8, size_t len, int max, int where, int split, const char* marker,
                           size_t mlen, int aw, size_t* head, size_t* tail, int* ew_out) {
  const int width = rolltui_frame_text_width(s, utf8, len, aw);
  const int ew = rolltui_u_display_width(s->u, marker, mlen, aw);
  int room, head_cells, tail_cells, used = 0;
  size_t i, count;
  *head = len, *tail = len, *ew_out = ew;
  if (width <= max) return 0;
  room = max - ew;
  if (room < 0) room = 0;
  head_cells = where == ROLLTUI_CUT_END     ? room
               : where == ROLLTUI_CUT_START ? 0
               : split > 0 && split < 100   ? room * split / 100
                                            : room - room / 2;
  tail_cells = room - head_cells;
  *head = head_cells > 0 ? rolltui_u_fit(s->u, utf8, len, head_cells, aw, &used) : 0;
  if (head_cells > 0) tail_cells = room - used; /* what the head could not use goes to the tail */
  *tail = len;
  if (tail_cells > 0) { /* the clusters from the end that fit in `tail_cells` */
    int got = 0;
    s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, len, sizeof *s->gs);
    count = rolltui_u_graphemes(s->u, utf8, len, aw, s->gs);
    for (i = count; i-- > 0;) {
      const RolltuiUnicodeGrapheme* g = &s->gs[i];
      if (g->offset < *head || got + g->width > tail_cells) break;
      got += g->width;
      *tail = g->offset;
    }
  }
  return 1;
}

static int cut_points(RolltuiDrawScratch* s, const char* utf8, size_t len, int max, int where, int aw, size_t* head, size_t* tail,
                      int* ew_out) {
  return cut_points_with(s, utf8, len, max, where, 0, ELLIPSIS, 3, aw, head, tail, ew_out);
}

int rolltui_frame_put_text_cut(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len, RolltuiStyle style,
                               int max_cells, int where, int ambiguous_wide, unsigned int link) {
  size_t head, tail;
  int ew, used;
  if (max_cells <= 0 || len == 0) return 0;
  if (!cut_points(s, utf8, len, max_cells, where, ambiguous_wide, &head, &tail, &ew) || max_cells <= ew)
    return rolltui_frame_put_text(f, s, x, y, utf8, len, style, max_cells, ambiguous_wide, link);
  used = rolltui_frame_put_text(f, s, x, y, utf8, head, style, max_cells, ambiguous_wide, link);
  used += rolltui_frame_put_text(f, s, x + used, y, ELLIPSIS, 3, style, ew, ambiguous_wide, link);
  return used + rolltui_frame_put_text(f, s, x + used, y, utf8 + tail, len - tail, style, max_cells - used, ambiguous_wide, link);
}

int rolltui_text_cut(RolltuiDrawScratch* s, const char* utf8, size_t len, int max_cells, int where, int ambiguous_wide, RolltuiStr* out) {
  size_t head, tail;
  int ew;
  rolltui_str_clear(out);
  if (max_cells <= 0 || len == 0) return 0;
  if (!cut_points(s, utf8, len, max_cells, where, ambiguous_wide, &head, &tail, &ew)) {
    rolltui_str_set(out, utf8, len);
    return rolltui_frame_text_width(s, utf8, len, ambiguous_wide);
  }
  if (max_cells <= ew) { /* no room for the mark: as much as fits */
    int cells = 0;
    rolltui_str_set(out, utf8, rolltui_u_fit(s->u, utf8, len, max_cells, ambiguous_wide, &cells));
    return cells;
  }
  rolltui_str_set(out, utf8, head);
  rolltui_str_append(out, ELLIPSIS, 3);
  rolltui_str_append(out, utf8 + tail, len - tail);
  return rolltui_frame_text_width(s, out->p, out->n, ambiguous_wide);
}

/* A file name's extension: the last dot after the last slash, not the name's first character, with 1 to 10 bytes after it. Its
 * offset, or `len` when there is none. */
static size_t extension_at(const char* p, size_t len) {
  size_t i = len, start = 0;
  for (i = len; i-- > 0;)
    if (p[i] == '/') {
      start = i + 1;
      break;
    }
  for (i = len; i-- > start;) {
    if (p[i] != '.') continue;
    if (i == start || len - i < 2 || len - i > 11) return len;
    return i;
  }
  return len;
}

int rolltui_text_cut_with(RolltuiDrawScratch* s, const char* utf8, size_t len, int max_cells, const RolltuiCutOptions* o,
                          int ambiguous_wide, RolltuiStr* out) {
  const char* marker = o && o->marker ? o->marker : ELLIPSIS;
  const size_t mlen = strlen(marker);
  const int where = o ? o->where : ROLLTUI_CUT_END;
  const int split = o ? o->split : 0;
  size_t head, tail, ext = len;
  int ew;
  rolltui_str_clear(out);
  if (max_cells <= 0 || len == 0) return 0;
  if (rolltui_frame_text_width(s, utf8, len, ambiguous_wide) <= max_cells) {
    rolltui_str_set(out, utf8, len);
    return rolltui_frame_text_width(s, utf8, len, ambiguous_wide);
  }
  if (o && o->keep_extension) ext = extension_at(utf8, len);
  if (ext < len) { /* the stem is cut, and the extension follows whole */
    const int ext_w = rolltui_frame_text_width(s, utf8 + ext, len - ext, ambiguous_wide);
    const int mw = rolltui_frame_text_width(s, marker, mlen, ambiguous_wide);
    if (max_cells - ext_w > mw) {
      if (cut_points_with(s, utf8, ext, max_cells - ext_w, where, split, marker, mlen, ambiguous_wide, &head, &tail, &ew)) {
        rolltui_str_set(out, utf8, head);
        rolltui_str_append(out, marker, mlen);
        rolltui_str_append(out, utf8 + tail, ext - tail);
      } else {
        rolltui_str_set(out, utf8, ext);
      }
      rolltui_str_append(out, utf8 + ext, len - ext);
      return rolltui_frame_text_width(s, out->p, out->n, ambiguous_wide);
    }
  }
  cut_points_with(s, utf8, len, max_cells, where, split, marker, mlen, ambiguous_wide, &head, &tail, &ew);
  if (max_cells <= ew) { /* no room for the mark: as much as fits */
    int cells = 0;
    rolltui_str_set(out, utf8, rolltui_u_fit(s->u, utf8, len, max_cells, ambiguous_wide, &cells));
    return cells;
  }
  rolltui_str_set(out, utf8, head);
  rolltui_str_append(out, marker, mlen);
  rolltui_str_append(out, utf8 + tail, len - tail);
  return rolltui_frame_text_width(s, out->p, out->n, ambiguous_wide);
}

int rolltui_frame_put_text_cut_with(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                                    RolltuiStyle style, int max_cells, const RolltuiCutOptions* o, int ambiguous_wide) {
  RolltuiStr cut;
  int used;
  memset(&cut, 0, sizeof cut);
  rolltui_text_cut_with(s, utf8, len, max_cells, o, ambiguous_wide, &cut);
  used = rolltui_frame_put_text(f, s, x, y, cut.p ? cut.p : "", cut.n, style, max_cells, ambiguous_wide, 0);
  rolltui_str_free(&cut);
  return used;
}

int rolltui_path_shorten(RolltuiDrawScratch* s, const char* path, size_t len, int max_cells, int ambiguous_wide, RolltuiStr* out) {
  size_t starts[64], ends[64], k = 0, i, last;
  RolltuiStr name;
  rolltui_str_clear(out);
  if (!path || max_cells <= 0) return 0;
  if (rolltui_frame_text_width(s, path, len, ambiguous_wide) <= max_cells) {
    rolltui_str_set(out, path, len);
    return rolltui_frame_text_width(s, path, len, ambiguous_wide);
  }
  {
    size_t slashes = 0;
    for (i = 0; i < len; ++i) slashes += path[i] == '/';
    if (slashes >= 64) { /* deeper than this walk keeps: cut at its start */
      RolltuiCutOptions co;
      memset(&co, 0, sizeof co);
      co.where = ROLLTUI_CUT_START;
      return rolltui_text_cut_with(s, path, len, max_cells, &co, ambiguous_wide, out);
    }
  }
  { /* the components, between slashes */
    size_t b = 0;
    for (i = 0; i <= len && k < 64; ++i)
      if (i == len || path[i] == '/') {
        starts[k] = b, ends[k] = i, ++k;
        b = i + 1;
      }
  }
  last = k - 1;
  { /* each folder from the left to its first letter, until the whole fits */
    size_t shortened = 0;
    for (;;) {
      int w;
      rolltui_str_clear(out);
      for (i = 0; i < last; ++i) {
        size_t cut = ends[i];
        if (i < shortened && ends[i] > starts[i]) { /* its first character, after a leading dot */
          const size_t dot = path[starts[i]] == '.' && ends[i] - starts[i] > 1 ? 1 : 0;
          const size_t rest = ends[i] - starts[i] - dot;
          s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, rest ? rest : 1, sizeof *s->gs);
          cut = starts[i] + dot + (rest && rolltui_u_graphemes(s->u, path + starts[i] + dot, rest, ambiguous_wide, s->gs) ? s->gs[0].length : 0);
        }
        rolltui_str_append(out, path + starts[i], cut - starts[i]);
        rolltui_str_append(out, "/", 1);
      }
      w = rolltui_frame_text_width(s, out->p ? out->p : "", out->n, ambiguous_wide);
      if (w + rolltui_frame_text_width(s, path + starts[last], ends[last] - starts[last], ambiguous_wide) <= max_cells) {
        rolltui_str_append(out, path + starts[last], ends[last] - starts[last]);
        return rolltui_frame_text_width(s, out->p, out->n, ambiguous_wide);
      }
      if (shortened >= last) { /* every folder is a letter: the name is cut, its extension kept */
        RolltuiCutOptions co;
        memset(&co, 0, sizeof co);
        co.where = ROLLTUI_CUT_MIDDLE;
        co.keep_extension = 1;
        if (max_cells - w < 4) { /* no room for a name: the whole, cut at its start */
          co.where = ROLLTUI_CUT_START;
          co.keep_extension = 0;
          return rolltui_text_cut_with(s, path, len, max_cells, &co, ambiguous_wide, out);
        }
        memset(&name, 0, sizeof name);
        rolltui_text_cut_with(s, path + starts[last], ends[last] - starts[last], max_cells - w, &co, ambiguous_wide, &name);
        rolltui_str_append(out, name.p ? name.p : "", name.n);
        rolltui_str_free(&name);
        return rolltui_frame_text_width(s, out->p, out->n, ambiguous_wide);
      }
      ++shortened;
    }
  }
}

/* ---- a line of spans ---------------------------------------------------------------------------------------------------------- */

int rolltui_frame_put_spans(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, const RolltuiTextSpan* spans, size_t n,
                            unsigned char align, unsigned char overflow, int ambiguous_wide) {
  int total = 0, x;
  size_t i;
  if (!f || !s || r.w <= 0 || r.h <= 0 || !spans) return 0;
  for (i = 0; i < n; ++i) total += rolltui_frame_text_width(s, spans[i].text, spans[i].len, ambiguous_wide);
  if (total <= r.w || overflow == ROLLTUI_OVERFLOW_CLIP) { /* ONE ROW: placed by its alignment, cut at the edge if it must be */
    x = r.x;
    if (total < r.w) x += align == ROLLTUI_ALIGN_RIGHT ? r.w - total : align == ROLLTUI_ALIGN_CENTER ? (r.w - total) / 2 : 0;
    for (i = 0; i < n && x < r.x + r.w; ++i)
      x += rolltui_frame_put_text(f, s, x, r.y, spans[i].text, spans[i].len, spans[i].style, r.x + r.w - x, ambiguous_wide, 0);
    return 1;
  }
  if (overflow == ROLLTUI_OVERFLOW_ELLIPSIS) { /* ONE ROW, cut with an ellipsis in the style of the span it cuts */
    const int ew = rolltui_frame_text_width(s, ELLIPSIS, 3, ambiguous_wide);
    const int room = r.w - ew;
    x = r.x;
    for (i = 0; i < n; ++i) {
      const int w = rolltui_frame_text_width(s, spans[i].text, spans[i].len, ambiguous_wide);
      if (x - r.x + w <= room) {
        x += rolltui_frame_put_text(f, s, x, r.y, spans[i].text, spans[i].len, spans[i].style, w, ambiguous_wide, 0);
        continue;
      }
      if (room > x - r.x) {
        int cells = 0;
        const size_t keep = rolltui_u_fit(s->u, spans[i].text, spans[i].len, room - (x - r.x), ambiguous_wide, &cells);
        x += rolltui_frame_put_text(f, s, x, r.y, spans[i].text, keep, spans[i].style, cells, ambiguous_wide, 0);
      }
      rolltui_frame_put_text(f, s, x, r.y, ELLIPSIS, 3, spans[i].style, ew, ambiguous_wide, 0);
      break;
    }
    return 1;
  }
  { /* WRAPPED: the spans as one text, wrapped at word breaks, each piece drawn in the span its bytes came from */
    RolltuiStr all;
    RolltuiWrapLines* w = rolltui_wrap_new();
    RolltuiWrapOptions wo;
    size_t* ends = (size_t*)rolltui_mem_alloc((n ? n : 1) * sizeof *ends);
    size_t lines, li;
    int rows = 0;
    memset(&all, 0, sizeof all);
    memset(&wo, 0, sizeof wo);
    wo.ambiguous_wide = (unsigned char)ambiguous_wide;
    wo.tab_width = 8;
    for (i = 0; i < n; ++i) {
      rolltui_str_append(&all, spans[i].text, spans[i].len);
      ends[i] = all.n;
    }
    rolltui_wrap(w, all.p ? all.p : "", all.n, r.w, wo);
    lines = rolltui_wrap_line_count(w);
    for (li = 0; li < lines && rows < r.h; ++li, ++rows) {
      const char* text;
      size_t tl, gn, g, sp = 0;
      const RolltuiWrapGrapheme* gs;
      int width, indent, hard;
      rolltui_wrap_line(w, li, &text, &tl, &gs, &gn, &width, &indent, &hard);
      x = r.x + (width < r.w ? (align == ROLLTUI_ALIGN_RIGHT ? r.w - width : align == ROLLTUI_ALIGN_CENTER ? (r.w - width) / 2 : 0) : 0);
      for (g = 0; g < gn; ++g) {
        while (sp + 1 < n && gs[g].source_offset >= ends[sp]) ++sp;
        if (gs[g].width <= 0) continue;
        if (x + gs[g].width > r.x + r.w) break;
        x += rolltui_frame_put(f, x, r.y + rows, text + gs[g].offset, gs[g].length, gs[g].width, spans[sp].style, 0);
      }
    }
    rolltui_mem_free(ends);
    rolltui_wrap_free(w);
    rolltui_str_free(&all);
    return rows;
  }
}

size_t rolltui_hotkey_at(const char* label, size_t len, unsigned int ch, size_t* n) {
  char key[8];
  size_t kn, at = 0, l = 0, from = 0, first = (size_t)-1, first_n = 0;
  if (n) *n = 0;
  if (!label || !ch) return (size_t)-1;
  if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A'; /* a key's letter in either case */
  kn = rolltui_u_append_utf8(ch, key);
  while (from < len && rolltui_text_find(label, len, key, kn, from, &at, &l)) {
    const int word_start = at == 0 || label[at - 1] == ' ' || label[at - 1] == '-' || label[at - 1] == '_' || label[at - 1] == '/';
    if (word_start) {
      if (n) *n = l;
      return at;
    }
    if (first == (size_t)-1) first = at, first_n = l;
    from = at + (l ? l : 1);
  }
  if (first != (size_t)-1 && n) *n = first_n;
  return first;
}

int rolltui_frame_text_width(RolltuiDrawScratch* s, const char* utf8, size_t len, int ambiguous_wide) {
  size_t count, i;
  int w = 0;
  size_t ascii;
  if (!s || len == 0) return 0;
  ascii = ascii_run(utf8, len);
  if (ascii == len) return (int)len;
  w = (int)ascii;
  utf8 += ascii;
  len -= ascii;
  s->gs = (RolltuiUnicodeGrapheme*)rolltui_grow(s->gs, &s->gs_cap, len, sizeof *s->gs);
  count = rolltui_u_graphemes(s->u, utf8, len, ambiguous_wide, s->gs);
  for (i = 0; i < count; ++i)
    if (s->gs[i].width > 0) w += s->gs[i].width;
  return w;
}

/* THE SWATCH. Two cells, one row tall: a cell is about twice as tall as it is wide, so two of them
 * are roughly a square, and an eighth-block on the outer edge of each is a thin outline round the
 * fill. The outline is in the text's own foreground so it is there on any ground — the case it exists
 * for is a colour that matches the background it sits on, which a bare fill would make disappear.
 *
 * The eighth-blocks are East Asian AMBIGUOUS, and a terminal that draws those two cells wide would
 * smear the swatch across four. There a bracket pair is the outline instead — ASCII, one cell everywhere.
 * A terminal that draws no colour has nothing for a swatch to show, and gets nothing. */
int rolltui_frame_put_swatch(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, RolltuiStyleColorRaw colour,
                             RolltuiStyle frame, int max_cells, int ambiguous_wide) {
  RolltuiStyle st = frame;
  (void)s;
  if (max_cells < 2 || x < 0 || y < 0 || y >= rolltui_frame_height(f) || x + 2 > rolltui_frame_width(f)) return 0;
  if (rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_TRUECOLOR) == ROLLTUI_DEPTH_MONO) return 0;
  st.bg = colour.kind != 0 ? colour : frame.bg; /* `none` is the frame alone, on the ground it sits on */
  st.bold = st.italic = st.underline = st.dim = st.reverse = st.strike = 0; /* an outline, not text: `reverse` would swap the fill out */
  st.blink = st.hidden = st.overline = 0;
  if (ambiguous_wide || rolltui_termfacts_active_wide()) {
    rolltui_frame_put(f, x, y, "[", 1, 1, st, 0);
    rolltui_frame_put(f, x + 1, y, "]", 1, 1, st, 0);
  } else {
    rolltui_frame_put(f, x, y, "\xE2\x96\x8F", 3, 1, st, 0); /* U+258F LEFT ONE EIGHTH BLOCK */
    rolltui_frame_put(f, x + 1, y, "\xE2\x96\x95", 3, 1, st, 0); /* U+2595 RIGHT ONE EIGHTH BLOCK */
  }
  return 2;
}

int rolltui_frame_put_fields(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const RolltuiRows* rows,
                             RolltuiStyle name_style, RolltuiStyle value_style, int max_cells,
                             int ambiguous_wide) {
  size_t i;
  int used = 0;
  if (!rows) return 0;
  for (i = 0; i < rows->n; ++i) {
    const RolltuiRow* row = &rows->v[i];
    if (used >= max_cells) break;
    /* The gap belongs to the field that follows, so a line never ends in trailing spaces that
     * a frame diff would then have to repaint. */
    if (used > 0)
      used += rolltui_frame_put_text(f, s, x + used, y, "  ", 2, name_style, max_cells - used, ambiguous_wide, 0);
    if (row->label.n != 0)
      used += rolltui_frame_put_text(f, s, x + used, y, row->label.p, row->label.n, name_style,
                                     max_cells - used, ambiguous_wide, 0);
    if (row->label.n != 0 && row->value.n != 0)
      used += rolltui_frame_put_text(f, s, x + used, y, " ", 1, name_style, max_cells - used, ambiguous_wide, 0);
    if (row->has_swatch && used < max_cells) {
      /* A COLOUR'S SQUARE IS FRAMED IN THE VALUE'S OWN FOREGROUND, and where the terminal draws no colour there
       * is none and no space is left for it. */
      const int sw = rolltui_frame_put_swatch(f, s, x + used, y, row->swatch, value_style, max_cells - used, ambiguous_wide);
      used += sw;
      if (sw && used < max_cells)
        used += rolltui_frame_put_text(f, s, x + used, y, " ", 1, name_style, max_cells - used, ambiguous_wide, 0);
    }
    if (row->value.n != 0)
      used += rolltui_frame_put_text(f, s, x + used, y, row->value.p, row->value.n, value_style,
                                     max_cells - used, ambiguous_wide, 0);
  }
  return used;
}

void rolltui_frame_fill(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, RolltuiStyle style,
                        const char* glyph, size_t glyph_len) {
  RolltuiRect bounds, c;
  int gw, xx, yy;
  bounds.x = 0;
  bounds.y = 0;
  bounds.w = rolltui_frame_width(f);
  bounds.h = rolltui_frame_height(f);
  {
    int out[4];
    rolltui_rect_intersect(r.x, r.y, r.w, r.h, bounds.x, bounds.y, bounds.w, bounds.h, out);
    c.x = out[0];
    c.y = out[1];
    c.w = out[2];
    c.h = out[3];
  }
  gw = (glyph && glyph_len) ? rolltui_u_display_width(s->u, glyph, glyph_len, 0) : 0;
  if (gw <= 0) {
    glyph = " ";
    glyph_len = 1;
    gw = 1;
  }
  /* ONE CELL WIDE AND INLINE, the cells are written whole, a row at a time: a window's ground is most of every frame. A cluster a
   * terminal splits (`rolltui_termfacts_active_split`) goes the long way, which sends it its lead. */
  if (gw == 1 && glyph_len <= ROLLTUI_CELL_INLINE_GLYPH && (glyph_len == 1 || !rolltui_termfacts_active_split())) {
    rolltui_frame_fill_narrow(f, c.x, c.y, c.x + c.w, c.y + c.h, glyph, glyph_len, style);
    return;
  }
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; xx += gw) rolltui_frame_put(f, xx, yy, glyph, glyph_len, gw, style, 0);
}

static unsigned char toward(unsigned char from, unsigned char to, double t) {
  const double v = (double)from + ((double)to - (double)from) * t;
  return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5));
}

/* One colour of a style, shaded: see `rolltui_frame_shade` in the header for the three cases. */
static void shade_color(RolltuiStyleColorRaw* c, RolltuiStyleColorRaw to, double t, unsigned char* dim) {
  if (to.kind == 0 /* Color::Kind::None */) return;
  if (t >= 1.0) { *c = to; return; }
  if (c->kind == 2 /* Rgb */ && to.kind == 2) {
    c->r = toward(c->r, to.r, t);
    c->g = toward(c->g, to.g, t);
    c->b = toward(c->b, to.b, t);
    return;
  }
  if (t >= 0.5) *c = to;
  else *dim = 1;
}

void rolltui_frame_shade(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style, double strength) {
  RolltuiRect c;
  int xx, yy;
  if (strength <= 0) return;
  {
    int out[4];
    rolltui_rect_intersect(r.x, r.y, r.w, r.h, 0, 0, rolltui_frame_width(f), rolltui_frame_height(f), out);
    c.x = out[0];
    c.y = out[1];
    c.w = out[2];
    c.h = out[3];
  }
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; ++xx) {
      RolltuiCellRaw cell;
      RolltuiStyle st;
      unsigned char dim_fg = 0, dim_bg = 0;
      rolltui_frame_cell(f, xx, yy, &cell);
      st = cell.style;
      shade_color(&st.fg, style.fg, strength, &dim_fg);
      shade_color(&st.bg, style.bg, strength, &dim_bg);
      {
        RolltuiStyle attrs = style; /* the attributes; the colours were shaded above */
        memset(&attrs.fg, 0, sizeof attrs.fg);
        memset(&attrs.bg, 0, sizeof attrs.bg);
        rolltui_style_patch(&st, &attrs);
      }
      st.dim |= dim_fg; /* a background has no dim to fall back on */
      rolltui_frame_set_style(f, xx, yy, st);
    }
}

void rolltui_style_on_window(RolltuiStyle* st, const RolltuiStyle* styles, RolltuiStyleColorRaw ground) {
  const RolltuiStyleColorRaw screen = styles[ROLLTUI_ROLE_BACKGROUND].bg;
  if (st->bg.kind == 0 || memcmp(&st->bg, &screen, sizeof screen) == 0) st->bg = ground;
}

void rolltui_style_patch(RolltuiStyle* base, const RolltuiStyle* over) {
  if (!base || !over) return;
  if (over->fg.kind != 0) base->fg = over->fg;
  if (over->bg.kind != 0) base->bg = over->bg;
  if (over->underline) base->underline = over->underline; /* a shape is replaced, never OR'd into another */
  if (over->underline_color.kind != 0) base->underline_color = over->underline_color;
  base->bold |= over->bold;
  base->italic |= over->italic;
  base->dim |= over->dim;
  base->reverse |= over->reverse;
  base->strike |= over->strike;
  if (over->blink > base->blink) base->blink = over->blink;
  base->hidden |= over->hidden;
  base->overline |= over->overline;
}

void rolltui_cursor_text_style(const RolltuiStyle* cursor, RolltuiStyleColorRaw ground, unsigned char shape, RolltuiStyle* out) {
  RolltuiStyle s;
  if (!cursor || !out) return;
  s = *cursor;
  if (shape == ROLLTUI_CURSOR_EDGES) {
    if (cursor->bg.kind) s.fg = cursor->bg; /* the highlight's colour, as the text's */
    s.bg = ground;
    s.reverse = 0;
    s.bold = 1;
  }
  *out = s;
}

void rolltui_frame_cursor_ends(RolltuiFrame* f, RolltuiRect band, RolltuiStyle cursor, RolltuiStyleColorRaw ground, unsigned char shape) {
  RolltuiStyle end;
  const char *l, *r;
  if (!f || band.w < 2 || shape == ROLLTUI_CURSOR_FILL) return;
  memset(&end, 0, sizeof end);
  end.fg = cursor.bg.kind ? cursor.bg : cursor.fg; /* a reversed cursor shows its text colour as its ground */
  end.bg = ground;
  if (shape == ROLLTUI_CURSOR_CAPS) l = "\xe2\x96\x90", r = "\xe2\x96\x8c"; /* ▐ ▌ */
  else l = "\xe2\x96\x8c", r = "\xe2\x96\x90";                            /* ▌ ▐ */
  rolltui_frame_put(f, band.x, band.y, l, 3, 1, end, 0);
  rolltui_frame_put(f, band.x + band.w - 1, band.y, r, 3, 1, end, 0);
}

void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style) {
  RolltuiRect c;
  int xx, yy;
  {
    int out[4];
    rolltui_rect_intersect(r.x, r.y, r.w, r.h, 0, 0, rolltui_frame_width(f), rolltui_frame_height(f), out);
    c.x = out[0];
    c.y = out[1];
    c.w = out[2];
    c.h = out[3];
  }
  for (yy = c.y; yy < c.y + c.h; ++yy)
    for (xx = c.x; xx < c.x + c.w; ++xx) {
      /* Read, amend, write back: the handle hands out a COPY of the cell, so the style a
       * widget drew is not a reference to reach through. */
      RolltuiCellRaw cell;
      RolltuiStyle st;
      rolltui_frame_cell(f, xx, yy, &cell);
      st = cell.style;
      rolltui_style_patch(&st, &style);
      rolltui_frame_set_style(f, xx, yy, st);
    }
}
