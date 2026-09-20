/* rolltui/c/rolltui_mermaid.c — a mermaid diagram, drawn as text. The contract is in the header.
 *
 * THE SHAPE OF THIS FILE. Every kind of diagram is read into its own small model and drawn into ONE grid of
 * cells (a glyph, a class, and the directions any line drawn there runs). Lines are drawn as directions, not
 * glyphs — an edge that meets another at a cell is the union of the two, and the glyph is chosen from the union
 * at the end, which is how a fork is `┴` and a crossing `┼` without any code that knows about either. The
 * grid is then read out as runs of one class each. */
#include "rolltui/c/rolltui_mermaid.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"

#define MAX_NODES 240
#define MAX_EDGES 480
#define MAX_LINES 2000

/* ============================================================================================
 * THE GRID
 * ============================================================================================ */

#define M_U 1
#define M_D 2
#define M_L 4
#define M_R 8

#define ST_LIGHT 1
#define ST_HEAVY 2
#define ST_DASH 4

typedef struct Cell {
  unsigned char g[8];  /* the glyph's UTF-8, NUL-terminated; empty is a space */
  unsigned char width; /* 1; 2 for the left half of a wide glyph; 0 for its right half */
  unsigned char cls;
  unsigned char mask;  /* the directions a line drawn here runs */
  unsigned char style; /* which kinds of line drew here */
} Cell;

typedef struct Grid {
  Cell* c;
  int w, h;
} Grid;

struct RolltuiMermaid {
  Grid g;
  int ascii;
  const char* kind;
  RolltuiUnicodeScratch* u;
  /* the read-out: public runs, their text, and where each line's runs begin */
  RolltuiMermaidRun* runs;
  size_t runs_n, runs_cap;
  size_t* line_first;
  size_t lines_n, lines_cap;
  RolltuiStr pool;
  size_t* run_off; /* while building: each run's text as an offset into `pool` */
  size_t run_off_cap;
  int width;
  RolltuiStr scratch;
};

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

static void grid_init(Grid* g, int w, int h) {
  size_t n;
  rolltui_mem_free(g->c);
  g->c = NULL;
  g->w = imax(w, 0);
  g->h = imax(h, 0);
  n = (size_t)g->w * (size_t)g->h;
  if (n == 0) return;
  g->c = (Cell*)rolltui_mem_alloc(n * sizeof *g->c);
  memset(g->c, 0, n * sizeof *g->c);
  {
    size_t i;
    for (i = 0; i < n; ++i) g->c[i].width = 1;
  }
}

static Cell* cell_at(Grid* g, int x, int y) {
  if (x < 0 || y < 0 || x >= g->w || y >= g->h) return NULL;
  return &g->c[(size_t)y * (size_t)g->w + (size_t)x];
}

static int cell_empty(Grid* g, int x, int y) {
  const Cell* c = cell_at(g, x, y);
  return c && c->g[0] == 0 && c->mask == 0 && c->width == 1;
}

static void put_glyph(Grid* g, int x, int y, const char* s, unsigned char cls) {
  Cell* c = cell_at(g, x, y);
  size_t n = strlen(s);
  if (!c) return;
  if (n > 7) n = 7;
  memcpy(c->g, s, n);
  c->g[n] = 0;
  c->width = 1;
  c->cls = cls;
}

static void add_mask(Grid* g, int x, int y, int mask, int style, unsigned char cls) {
  Cell* c = cell_at(g, x, y);
  if (!c) return;
  c->mask = (unsigned char)(c->mask | mask);
  c->style = (unsigned char)(c->style | style);
  c->cls = cls;
}

/* A line from (x0,y0) to (x1,y1), which are on one row or one column: each cell gets the directions that join it
 * to its neighbours along the line. */
static void line_between(Grid* g, int x0, int y0, int x1, int y1, int style, unsigned char cls) {
  const int dx = x1 > x0 ? 1 : x1 < x0 ? -1 : 0;
  const int dy = y1 > y0 ? 1 : y1 < y0 ? -1 : 0;
  int x = x0, y = y0;
  if (dx != 0 && dy != 0) return;
  while (x != x1 || y != y1) {
    const int nx = x + dx, ny = y + dy;
    add_mask(g, x, y, dx > 0 ? M_R : dx < 0 ? M_L : dy > 0 ? M_D : M_U, style, cls);
    add_mask(g, nx, ny, dx > 0 ? M_L : dx < 0 ? M_R : dy > 0 ? M_U : M_D, style, cls);
    x = nx;
    y = ny;
  }
}

/* The glyph a mask stands for. Light lines take rounded elbows — they read as softer than corners and match the
 * rounded nodes; a heavy or dashed line keeps to the glyphs those sets have. */
static const char* mask_glyph(int mask, int style, int ascii) {
  static const char* const light[16] = {" ", "\xE2\x94\x82", "\xE2\x94\x82", "\xE2\x94\x82", "\xE2\x94\x80", "\xE2\x95\xAF",
                                        "\xE2\x95\xAE", "\xE2\x94\xA4", "\xE2\x94\x80", "\xE2\x95\xB0", "\xE2\x95\xAD",
                                        "\xE2\x94\x9C", "\xE2\x94\x80", "\xE2\x94\xB4", "\xE2\x94\xAC", "\xE2\x94\xBC"};
  static const char* const heavy[16] = {" ", "\xE2\x94\x83", "\xE2\x94\x83", "\xE2\x94\x83", "\xE2\x94\x81", "\xE2\x94\x9B",
                                        "\xE2\x94\x93", "\xE2\x94\xAB", "\xE2\x94\x81", "\xE2\x94\x97", "\xE2\x94\x8F",
                                        "\xE2\x94\xA3", "\xE2\x94\x81", "\xE2\x94\xBB", "\xE2\x94\xB3", "\xE2\x95\x8B"};
  if (mask == 0) return " ";
  if (ascii) {
    if (mask == M_U || mask == M_D || mask == (M_U | M_D)) return (style & ST_DASH) && !(style & (ST_LIGHT | ST_HEAVY)) ? ":" : "|";
    if (mask == M_L || mask == M_R || mask == (M_L | M_R))
      return (style & ST_HEAVY) && !(style & ST_LIGHT) ? "=" : (style & ST_DASH) && !(style & (ST_LIGHT | ST_HEAVY)) ? "." : "-";
    return "+";
  }
  if ((style & ST_HEAVY) && !(style & ST_LIGHT)) return heavy[mask & 15];
  if ((style & ST_DASH) && !(style & (ST_LIGHT | ST_HEAVY))) {
    if (mask == M_U || mask == M_D || mask == (M_U | M_D)) return "\xE2\x94\x86"; /* ┆ */
    if (mask == M_L || mask == M_R || mask == (M_L | M_R)) return "\xE2\x94\x84"; /* ┄ */
  }
  return light[mask & 15];
}

/* ---- text into the grid ---------------------------------------------------------------- */

static int text_width(RolltuiMermaid* m, const char* s, size_t n) {
  return rolltui_u_display_width(m->u, s, n, m->ascii);
}

/* `s` at (x, y), a cluster to a cell (two for a wide one), in class `cls`. Returns the cells used. Whatever a
 * cell held — a line, another glyph — is replaced. */
static int put_text(RolltuiMermaid* m, int x, int y, const char* s, size_t n, unsigned char cls) {
  RolltuiUnicodeGrapheme* gs;
  size_t count, i;
  int used = 0;
  if (n == 0) return 0;
  gs = (RolltuiUnicodeGrapheme*)rolltui_mem_alloc(n * sizeof *gs);
  count = rolltui_u_graphemes(m->u, s, n, m->ascii, gs);
  for (i = 0; i < count; ++i) {
    Cell* c = cell_at(&m->g, x + used, y);
    size_t len = gs[i].length;
    if (gs[i].width <= 0) continue; /* a control character, or a mark with nothing to sit on */
    if (c) {
      if (len > 7) {
        /* a cluster too long for a cell (a family of emoji): its first scalar stands for it */
        RolltuiDecodedChar d;
        rolltui_u_decode_one(s + gs[i].offset, len, 0, &d);
        len = d.length ? d.length : 1;
      }
      memcpy(c->g, s + gs[i].offset, len);
      c->g[len] = 0;
      c->width = (unsigned char)(gs[i].width >= 2 ? 2 : 1);
      c->cls = cls;
      c->mask = 0;
      if (gs[i].width >= 2) {
        Cell* r = cell_at(&m->g, x + used + 1, y);
        if (r) {
          r->g[0] = 0;
          r->width = 0;
          r->cls = cls;
          r->mask = 0;
        }
      }
    }
    used += gs[i].width >= 2 ? 2 : 1;
  }
  rolltui_mem_free(gs);
  return used;
}

static int put_str(RolltuiMermaid* m, int x, int y, const char* s, unsigned char cls) { return put_text(m, x, y, s, strlen(s), cls); }

/* Every cell of the grid that has lines and no glyph of its own gets the glyph its directions make. */
static void resolve_lines(RolltuiMermaid* m) {
  size_t i, n = (size_t)m->g.w * (size_t)m->g.h;
  for (i = 0; i < n; ++i) {
    Cell* c = &m->g.c[i];
    if (c->g[0] == 0 && c->mask != 0 && c->width == 1) {
      const char* s = mask_glyph(c->mask, c->style, m->ascii);
      const size_t len = strlen(s);
      memcpy(c->g, s, len);
      c->g[len] = 0;
    }
  }
}

/* ============================================================================================
 * THE READ-OUT: rows of cells become lines of runs
 * ============================================================================================ */

static void out_reset(RolltuiMermaid* m) {
  m->runs_n = 0;
  m->lines_n = 0;
  m->width = 0;
  m->kind = "";
  rolltui_str_clear(&m->pool);
}

static void read_out(RolltuiMermaid* m) {
  int y;
  size_t r;
  const char* kind = m->kind;
  int first_row = 0, last_row;
  out_reset(m);
  m->kind = kind;
  resolve_lines(m);
  last_row = m->g.h - 1;
  while (first_row <= last_row) {
    int x, empty = 1;
    for (x = 0; x < m->g.w && empty; ++x) { const Cell* c = cell_at(&m->g, x, first_row); if (c->g[0] != 0 || c->width == 0) empty = 0; }
    if (!empty) break;
    ++first_row;
  }
  while (last_row > first_row) {
    int x, empty = 1;
    for (x = 0; x < m->g.w && empty; ++x) { const Cell* c = cell_at(&m->g, x, last_row); if (c->g[0] != 0 || c->width == 0) empty = 0; }
    if (!empty) break;
    --last_row;
  }
  for (y = first_row; y <= last_row; ++y) {
    int last = -1, x, run_w = 0;
    unsigned char run_cls = 255;
    size_t run_start_off = 0;
    m->line_first = (size_t*)rolltui_grow(m->line_first, &m->lines_cap, m->lines_n + 2, sizeof *m->line_first);
    m->line_first[m->lines_n] = m->runs_n;
    /* the last cell with anything in it: trailing spaces are not part of a line */
    for (x = m->g.w - 1; x >= 0; --x) {
      const Cell* c = cell_at(&m->g, x, y);
      if (c->width == 0 || c->g[0] != 0) { last = x; break; }
    }
    for (x = 0; x <= last; ++x) {
      const Cell* c = cell_at(&m->g, x, y);
      const unsigned char cls = c->g[0] ? c->cls : ROLLTUI_MERMAID_CLASS_SPACE;
      if (c->width == 0) continue; /* the right half of a wide glyph: the left half carries it */
      if (cls != run_cls) {
        if (run_cls != 255) {
          m->runs = (RolltuiMermaidRun*)rolltui_grow(m->runs, &m->runs_cap, m->runs_n + 1, sizeof *m->runs);
          m->run_off = (size_t*)rolltui_grow(m->run_off, &m->run_off_cap, m->runs_n + 1, sizeof *m->run_off);
          m->run_off[m->runs_n] = run_start_off;
          m->runs[m->runs_n].n = m->pool.n - run_start_off;
          m->runs[m->runs_n].width = run_w;
          m->runs[m->runs_n].cls = run_cls;
          m->runs[m->runs_n].text = NULL;
          ++m->runs_n;
        }
        run_cls = cls;
        run_start_off = m->pool.n;
        run_w = 0;
      }
      if (c->g[0]) rolltui_str_append(&m->pool, (const char*)c->g, strlen((const char*)c->g));
      else rolltui_str_append(&m->pool, " ", 1);
      run_w += c->width == 2 ? 2 : 1;
    }
    if (run_cls != 255) {
      m->runs = (RolltuiMermaidRun*)rolltui_grow(m->runs, &m->runs_cap, m->runs_n + 1, sizeof *m->runs);
      m->run_off = (size_t*)rolltui_grow(m->run_off, &m->run_off_cap, m->runs_n + 1, sizeof *m->run_off);
      m->run_off[m->runs_n] = run_start_off;
      m->runs[m->runs_n].n = m->pool.n - run_start_off;
      m->runs[m->runs_n].width = run_w;
      m->runs[m->runs_n].cls = run_cls;
      m->runs[m->runs_n].text = NULL;
      ++m->runs_n;
    }
    if (last + 1 > m->width) m->width = last + 1;
    ++m->lines_n;
  }
  m->line_first[m->lines_n] = m->runs_n;
  /* the pool is done growing: point every run into it */
  for (r = 0; r < m->runs_n; ++r) m->runs[r].text = m->pool.p + m->run_off[r];
}

/* ============================================================================================
 * PARSING UTILITIES
 * ============================================================================================ */

typedef struct Span {
  const char* p;
  size_t n;
} Span;

static void trim(Span* s) {
  while (s->n && (s->p[0] == ' ' || s->p[0] == '\t' || s->p[0] == '\r')) { ++s->p; --s->n; }
  while (s->n && (s->p[s->n - 1] == ' ' || s->p[s->n - 1] == '\t' || s->p[s->n - 1] == '\r')) --s->n;
}

static int span_starts(const Span* s, const char* lit) {
  const size_t n = strlen(lit);
  return s->n >= n && memcmp(s->p, lit, n) == 0;
}

static int span_starts_ci(const Span* s, const char* lit) {
  const size_t n = strlen(lit);
  size_t i;
  if (s->n < n) return 0;
  for (i = 0; i < n; ++i)
    if (tolower((unsigned char)s->p[i]) != tolower((unsigned char)lit[i])) return 0;
  return 1;
}

static void skip_ws(Span* s) {
  while (s->n && (s->p[0] == ' ' || s->p[0] == '\t')) { ++s->p; --s->n; }
}

static int is_word_char(int c) { return isalnum(c) || c == '_' || (unsigned char)c >= 0x80; }

static int first_word_is(const Span* l, const char* w) {
  const size_t n = strlen(w);
  return span_starts_ci(l, w) && (l->n == n || !is_word_char((unsigned char)l->p[n]) );
}

typedef struct Source {
  Span* lines;
  int* indent; /* the width of the whitespace each line began with (a tab is four) */
  size_t n, cap;
  RolltuiStr title; /* from a `---` front matter block or a `title` line */
} Source;

static void source_release(Source* s) {
  rolltui_mem_free(s->lines);
  rolltui_mem_free(s->indent);
  rolltui_str_free(&s->title);
  memset(s, 0, sizeof *s);
}

/* The diagram's lines: trimmed, without comments, blank lines, or the front matter block and init directive that
 * configure mermaid rather than say what to draw (a `title:` in the front matter is kept). */
static int source_read(Source* s, const char* src, size_t n) {
  size_t i = 0;
  int in_front = 0, seen_content = 0, lead = 0;
  size_t indent_cap = 0;
  memset(s, 0, sizeof *s);
  while (i <= n) {
    size_t e = i;
    Span line;
    while (e < n && src[e] != '\n') ++e;
    line.p = src + i;
    line.n = e - i;
    i = e + 1;
    {
      size_t k = 0;
      lead = 0;
      while (k < line.n && (line.p[k] == ' ' || line.p[k] == '\t')) { lead += line.p[k] == '\t' ? 4 : 1; ++k; }
    }
    trim(&line);
    if (!seen_content && !in_front && line.n == 3 && memcmp(line.p, "---", 3) == 0) { in_front = 1; continue; }
    if (in_front) {
      if (line.n == 3 && memcmp(line.p, "---", 3) == 0) { in_front = 0; seen_content = 1; continue; }
      if (span_starts_ci(&line, "title:")) {
        Span t = {line.p + 6, line.n - 6};
        trim(&t);
        if (t.n >= 2 && (t.p[0] == '"' || t.p[0] == '\'') && t.p[t.n - 1] == t.p[0]) { ++t.p; t.n -= 2; }
        rolltui_str_set(&s->title, t.p, t.n);
      }
      continue;
    }
    if (line.n == 0) continue;
    if (span_starts(&line, "%%")) continue;
    seen_content = 1;
    if (s->n >= MAX_LINES) return 0;
    s->lines = (Span*)rolltui_grow(s->lines, &s->cap, s->n + 1, sizeof *s->lines);
    s->indent = (int*)rolltui_grow(s->indent, &indent_cap, s->n + 1, sizeof *s->indent);
    s->indent[s->n] = lead;
    s->lines[s->n++] = line;
  }
  return 1;
}

/* A label as it will be drawn: quotes taken off, `<br>` a line break, other tags dropped, the entities mermaid
 * uses decoded, and markdown's backticks taken off. Lines go into `out` separated by '\n'. */
static void clean_label(Span in, RolltuiStr* out) {
  size_t i = 0;
  rolltui_str_clear(out);
  trim(&in);
  if (in.n >= 2 && ((in.p[0] == '"' && in.p[in.n - 1] == '"') || (in.p[0] == '\'' && in.p[in.n - 1] == '\''))) { ++in.p; in.n -= 2; }
  if (in.n >= 2 && in.p[0] == '`' && in.p[in.n - 1] == '`') { ++in.p; in.n -= 2; }
  while (i < in.n) {
    const char c = in.p[i];
    if (c == '<') {
      size_t e = i + 1;
      while (e < in.n && in.p[e] != '>') ++e;
      if (e < in.n) {
        const size_t tn = e - i - 1;
        const char* t = in.p + i + 1;
        if ((tn >= 2 && (t[0] == 'b' || t[0] == 'B') && (t[1] == 'r' || t[1] == 'R') && (tn == 2 || t[2] == '/' || t[2] == ' '))) rolltui_str_append(out, "\n", 1);
        i = e + 1;
        continue;
      }
    }
    if (c == '#' || c == '&') {
      static const struct { const char* const ent; const char* const rep; } ents[] = {
          {"#quot;", "\""}, {"&quot;", "\""}, {"#amp;", "&"}, {"&amp;", "&"}, {"#lt;", "<"}, {"&lt;", "<"},
          {"#gt;", ">"}, {"&gt;", ">"}, {"#35;", "#"}, {"&#35;", "#"}, {"&nbsp;", " "}, {"#nbsp;", " "}};
      size_t k;
      int done = 0;
      for (k = 0; k < sizeof ents / sizeof *ents; ++k) {
        const size_t en = strlen(ents[k].ent);
        if (in.n - i >= en && memcmp(in.p + i, ents[k].ent, en) == 0) {
          rolltui_str_append(out, ents[k].rep, strlen(ents[k].rep));
          i += en;
          done = 1;
          break;
        }
      }
      if (done) continue;
    }
    if (c == 'f' && in.n - i > 6 && in.p[i + 1] == 'a' && (in.p[i + 2] == ':' || (in.p[i + 3] == ':' && (in.p[i + 2] == 'b' || in.p[i + 2] == 'r' || in.p[i + 2] == 's' || in.p[i + 2] == 'l')))) {
      /* `fa:fa-car` and its kin: an icon this cannot draw, and no part of the words */
      size_t k = i + (in.p[i + 2] == ':' ? 3 : 4);
      if (in.n - k > 3 && in.p[k] == 'f' && in.p[k + 1] == 'a' && in.p[k + 2] == '-') {
        k += 3;
        while (k < in.n && (isalnum((unsigned char)in.p[k]) || in.p[k] == '-')) ++k;
        if (k < in.n && in.p[k] == ' ') ++k;
        i = k;
        continue;
      }
    }
    if (c == '\\' && i + 1 < in.n && in.p[i + 1] == 'n') { rolltui_str_append(out, "\n", 1); i += 2; continue; }
    if (c == '\t') { rolltui_str_append(out, " ", 1); ++i; continue; }
    if ((unsigned char)c < 0x20 && c != '\n') { ++i; continue; } /* a control character is not a label */
    rolltui_str_append(out, &c, 1);
    ++i;
  }
}

/* A number, as pie charts and gantt bars use it. */
static int parse_double(Span s, double* out) {
  char buf[64];
  char* end;
  trim(&s);
  if (s.n == 0 || s.n >= sizeof buf) return 0;
  memcpy(buf, s.p, s.n);
  buf[s.n] = 0;
  *out = strtod(buf, &end);
  return end != buf && *end == 0;
}

static void reason_set(RolltuiStr* reason, const char* msg) {
  if (reason) rolltui_str_set(reason, msg, strlen(msg));
}

static void reason_setf(RolltuiStr* reason, const char* fmt, int a, int b) {
  char buf[200];
  if (!reason) return;
  snprintf(buf, sizeof buf, fmt, a, b);
  rolltui_str_set(reason, buf, strlen(buf));
}

/* ============================================================================================
 * PIE
 * ============================================================================================ */

static const char* const kEighths[8] = {" ", "\xE2\x96\x8F", "\xE2\x96\x8E", "\xE2\x96\x8D", "\xE2\x96\x8C", "\xE2\x96\x8B", "\xE2\x96\x8A", "\xE2\x96\x89"};

typedef struct Slice {
  RolltuiStr label;
  double value;
} Slice;

static int draw_pie(RolltuiMermaid* m, const Source* src, size_t first, int show_data, const RolltuiStr* title, int max_width,
                    RolltuiStr* reason) {
  Slice* slices = NULL;
  size_t n = 0, cap = 0, i;
  double total = 0;
  int label_w = 0, value_w = 0, bar_w, y = 0, rows;
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    size_t q;
    Span lab, val;
    RolltuiStr clean;
    double v;
    memset(&clean, 0, sizeof clean);
    if (span_starts_ci(&l, "title ")) continue;
    if (l.n && l.p[0] == '"') {
      q = 1;
      while (q < l.n && l.p[q] != '"') ++q;
      if (q >= l.n) { reason_set(reason, "a slice's label is missing its closing quote"); goto fail; }
      lab.p = l.p + 1;
      lab.n = q - 1;
      val.p = l.p + q + 1;
      val.n = l.n - q - 1;
      trim(&val);
      if (val.n == 0 || val.p[0] != ':') { reason_set(reason, "a slice needs `\"label\" : value`"); goto fail; }
      ++val.p;
      --val.n;
      if (!parse_double(val, &v) || v < 0) { reason_set(reason, "a slice's value must be a number that is not negative"); goto fail; }
      slices = (Slice*)rolltui_grow(slices, &cap, n + 1, sizeof *slices);
      memset(&slices[n], 0, sizeof slices[n]);
      clean_label(lab, &clean);
      for (q = 0; q < clean.n; ++q) if (clean.p[q] == '\n') clean.p[q] = ' ';
      rolltui_str_set(&slices[n].label, clean.p ? clean.p : "", clean.n);
      rolltui_str_free(&clean);
      slices[n].value = v;
      total += v;
      ++n;
    } else {
      reason_set(reason, "a pie chart's lines are `\"label\" : value`");
      goto fail;
    }
  }
  if (n == 0 || total <= 0) { reason_set(reason, "a pie chart with nothing in it"); goto fail; }
  for (i = 0; i < n; ++i) {
    char v[48];
    const int lw = text_width(m, slices[i].label.p ? slices[i].label.p : "", slices[i].label.n);
    const int vn = snprintf(v, sizeof v, "%g (%.1f%%)", slices[i].value, 100.0 * slices[i].value / total);
    if (lw > label_w) label_w = lw;
    if (vn > value_w) value_w = vn;
  }
  (void)show_data;
  bar_w = max_width - label_w - value_w - 4;
  if (bar_w < 8) {
    if (label_w > 16) {
      label_w = 16;
      bar_w = max_width - label_w - value_w - 4;
    }
    if (bar_w < 8) { reason_setf(reason, "a pie chart needs about %d columns; this view has %d", label_w + value_w + 12, max_width); goto fail; }
  }
  bar_w = imin(bar_w, 48);
  rows = (int)n + (title && title->n ? 2 : 0);
  grid_init(&m->g, label_w + value_w + bar_w + 4, rows);
  if (title && title->n) {
    put_text(m, 0, y, title->p, title->n, ROLLTUI_MERMAID_CLASS_TITLE);
    y += 2;
  }
  for (i = 0; i < n; ++i) {
    char v[48];
    const double frac = slices[i].value / total;
    const double cells = frac * bar_w * 8.0;
    int full = (int)(cells / 8.0), part = (int)(cells - full * 8.0 + 0.5), x, vn;
    const unsigned char accent = (unsigned char)(ROLLTUI_MERMAID_CLASS_ACCENT1 + i % 4);
    int lw = text_width(m, slices[i].label.p ? slices[i].label.p : "", slices[i].label.n);
    if (part >= 8) { ++full; part = 0; }
    if (lw > label_w) {
      const size_t keep = rolltui_u_fit(m->u, slices[i].label.p, slices[i].label.n, label_w - 1, m->ascii, NULL);
      put_text(m, 0, y, slices[i].label.p, keep, ROLLTUI_MERMAID_CLASS_TEXT);
      put_str(m, label_w - 1, y, "\xE2\x80\xA6", ROLLTUI_MERMAID_CLASS_TEXT);
    } else {
      put_text(m, 0, y, slices[i].label.p ? slices[i].label.p : "", slices[i].label.n, ROLLTUI_MERMAID_CLASS_TEXT);
    }
    put_str(m, label_w + 1, y, m->ascii ? "|" : "\xE2\x94\x82", ROLLTUI_MERMAID_CLASS_BOX);
    for (x = 0; x < full; ++x) put_str(m, label_w + 2 + x, y, m->ascii ? "#" : "\xE2\x96\x88", accent);
    if (part > 0 && !m->ascii) put_str(m, label_w + 2 + full, y, kEighths[part], accent);
    vn = snprintf(v, sizeof v, "%g (%.1f%%)", slices[i].value, 100.0 * frac);
    put_text(m, label_w + 3 + imax(full + (part > 0 ? 1 : 0), 1), y, v, (size_t)vn, ROLLTUI_MERMAID_CLASS_MUTED);
    ++y;
  }
  for (i = 0; i < n; ++i) rolltui_str_free(&slices[i].label);
  rolltui_mem_free(slices);
  m->kind = "pie";
  return 1;
fail:
  for (i = 0; i < n; ++i) rolltui_str_free(&slices[i].label);
  rolltui_mem_free(slices);
  return 0;
}

/* ============================================================================================
 * SEQUENCE DIAGRAMS
 * ============================================================================================ */

typedef struct SeqPart {
  RolltuiStr id;
  RolltuiStr label; /* lines separated by '\n' */
  int w, h;         /* the box */
  int cx;           /* its lifeline's column */
} SeqPart;

#define SEQ_MSG 1
#define SEQ_NOTE 2
#define SEQ_ACTIVATE 3
#define SEQ_DEACTIVATE 4
#define SEQ_BLOCK 5   /* opens a frame: loop, alt, opt, par, critical, break, rect */
#define SEQ_ELSE 6    /* a divider inside a frame: else, and, option */
#define SEQ_END 7     /* closes a frame */

#define SEQ_HEAD_NONE 0
#define SEQ_HEAD_ARROW 1
#define SEQ_HEAD_CROSS 2
#define SEQ_HEAD_ASYNC 3

typedef struct SeqEvent {
  int kind;
  int from, to;       /* participants (a note: the range it is over, or the one it is beside) */
  int dashed, head;   /* a message */
  int note_side;      /* 0 over, 1 left of, 2 right of */
  int number;         /* autonumber, 0 for none */
  RolltuiStr text;    /* a message's label, a note's text, a frame's title */
  RolltuiStr keyword; /* loop, alt, ... */
  int row;            /* the first row this event takes */
  int rows;
  int match;          /* a frame's closing event, and the closing event's opener */
  int depth;          /* how many frames it is inside */
} SeqEvent;

typedef struct Seq {
  SeqPart* parts;
  size_t np, np_cap;
  SeqEvent* ev;
  size_t ne, ne_cap;
} Seq;

static void seq_release(Seq* s) {
  size_t i;
  for (i = 0; i < s->np; ++i) {
    rolltui_str_free(&s->parts[i].id);
    rolltui_str_free(&s->parts[i].label);
  }
  for (i = 0; i < s->ne; ++i) {
    rolltui_str_free(&s->ev[i].text);
    rolltui_str_free(&s->ev[i].keyword);
  }
  rolltui_mem_free(s->parts);
  rolltui_mem_free(s->ev);
  memset(s, 0, sizeof *s);
}

static int seq_find(const Seq* s, Span id) {
  size_t i;
  for (i = 0; i < s->np; ++i)
    if (s->parts[i].id.n == id.n && memcmp(s->parts[i].id.p, id.p, id.n) == 0) return (int)i;
  return -1;
}

static int seq_part(Seq* s, Span id, const Span* label) {
  int at = seq_find(s, id);
  if (at >= 0) {
    if (label) clean_label(*label, &s->parts[at].label);
    return at;
  }
  if (s->np >= 40) return -1;
  s->parts = (SeqPart*)rolltui_grow(s->parts, &s->np_cap, s->np + 1, sizeof *s->parts);
  memset(&s->parts[s->np], 0, sizeof s->parts[s->np]);
  rolltui_str_set(&s->parts[s->np].id, id.p, id.n);
  if (label) clean_label(*label, &s->parts[s->np].label);
  else clean_label(id, &s->parts[s->np].label);
  return (int)s->np++;
}

static SeqEvent* seq_event(Seq* s, int kind) {
  SeqEvent* e;
  s->ev = (SeqEvent*)rolltui_grow(s->ev, &s->ne_cap, s->ne + 1, sizeof *s->ev);
  e = &s->ev[s->ne++];
  memset(e, 0, sizeof *e);
  e->kind = kind;
  e->match = -1;
  return e;
}

/* `A->>B` and its kin: the operator's length and what it means, at `at` in `s`, or 0. */
static size_t seq_operator(Span s, size_t at, int* dashed, int* head) {
  static const struct { const char* const op; int dashed; int head; } ops[] = {
      {"-->>", 1, SEQ_HEAD_ARROW}, {"--x", 1, SEQ_HEAD_CROSS}, {"--)", 1, SEQ_HEAD_ASYNC}, {"-->", 1, SEQ_HEAD_NONE},
      {"->>", 0, SEQ_HEAD_ARROW},  {"-x", 0, SEQ_HEAD_CROSS},  {"-)", 0, SEQ_HEAD_ASYNC},  {"->", 0, SEQ_HEAD_NONE}};
  size_t k;
  for (k = 0; k < sizeof ops / sizeof *ops; ++k) {
    const size_t n = strlen(ops[k].op);
    if (s.n - at >= n && memcmp(s.p + at, ops[k].op, n) == 0) {
      *dashed = ops[k].dashed;
      *head = ops[k].head;
      return n;
    }
  }
  return 0;
}

static int seq_parse(Seq* s, const Source* src, size_t first, int* autonumber, RolltuiStr* reason) {
  size_t i;
  int depth = 0, number = 0;
  int open[32];
  int open_n = 0;
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    if (span_starts_ci(&l, "participant ") || span_starts_ci(&l, "actor ")) {
      Span rest = {l.p + (l.p[0] == 'p' || l.p[0] == 'P' ? 12 : 6), l.n - (l.p[0] == 'p' || l.p[0] == 'P' ? 12 : 6)};
      Span id = rest, label;
      size_t as;
      int have_label = 0;
      trim(&rest);
      id = rest;
      for (as = 0; as + 4 <= rest.n; ++as)
        if (rest.p[as] == ' ' && (rest.p[as + 1] == 'a' || rest.p[as + 1] == 'A') && (rest.p[as + 2] == 's' || rest.p[as + 2] == 'S') && rest.p[as + 3] == ' ') {
          id.n = as;
          label.p = rest.p + as + 4;
          label.n = rest.n - as - 4;
          have_label = 1;
          break;
        }
      trim(&id);
      if (id.n >= 2 && id.p[0] == '"' && id.p[id.n - 1] == '"') { ++id.p; id.n -= 2; }
      if (id.n == 0) continue;
      if (seq_part(s, id, have_label ? &label : NULL) < 0) { reason_set(reason, "a sequence diagram with more than 40 participants is not drawn"); return 0; }
      continue;
    }
    if (span_starts_ci(&l, "autonumber")) { *autonumber = 1; continue; }
    if (span_starts_ci(&l, "title ") || span_starts_ci(&l, "accTitle") || span_starts_ci(&l, "accDescr") || span_starts_ci(&l, "box ") ||
        first_word_is(&l, "create") || first_word_is(&l, "destroy") || first_word_is(&l, "link") || first_word_is(&l, "links") ||
        first_word_is(&l, "properties") || first_word_is(&l, "details")) {
      if (first_word_is(&l, "create") || first_word_is(&l, "destroy")) {
        Span rest = {l.p + (l.p[0] == 'c' ? 6 : 7), l.n - (l.p[0] == 'c' ? 6 : 7)};
        trim(&rest);
        if (span_starts_ci(&rest, "participant ") || span_starts_ci(&rest, "actor ")) {
          Span r2 = {rest.p + (rest.p[0] == 'p' || rest.p[0] == 'P' ? 12 : 6), rest.n - (rest.p[0] == 'p' || rest.p[0] == 'P' ? 12 : 6)};
          Span id = r2, label;
          size_t as;
          int have_label = 0;
          trim(&r2);
          id = r2;
          for (as = 0; as + 4 <= r2.n; ++as)
            if (r2.p[as] == ' ' && (r2.p[as + 1] == 'a' || r2.p[as + 1] == 'A') && (r2.p[as + 2] == 's' || r2.p[as + 2] == 'S') && r2.p[as + 3] == ' ') {
              id.n = as;
              label.p = r2.p + as + 4;
              label.n = r2.n - as - 4;
              have_label = 1;
              break;
            }
          trim(&id);
          if (id.n) seq_part(s, id, have_label ? &label : NULL);
        }
      }
      continue;
    }
    if (first_word_is(&l, "activate") || first_word_is(&l, "deactivate")) {
      const int on = l.p[0] == 'a' || l.p[0] == 'A';
      Span id = {l.p + (on ? 8 : 10), l.n - (on ? 8 : 10)};
      int at;
      trim(&id);
      at = seq_part(s, id, NULL);
      if (at >= 0) {
        SeqEvent* e = seq_event(s, on ? SEQ_ACTIVATE : SEQ_DEACTIVATE);
        e->from = e->to = at;
      }
      continue;
    }
    if (first_word_is(&l, "note")) {
      Span rest = {l.p + 4, l.n - 4};
      Span who, text;
      size_t colon = 0;
      SeqEvent* e;
      int a = -1, b = -1, side = 0;
      trim(&rest);
      while (colon < rest.n && rest.p[colon] != ':') ++colon;
      if (colon >= rest.n) { reason_set(reason, "a note needs `Note over A: text`"); return 0; }
      who.p = rest.p;
      who.n = colon;
      text.p = rest.p + colon + 1;
      text.n = rest.n - colon - 1;
      trim(&who);
      if (span_starts_ci(&who, "over ")) { who.p += 5; who.n -= 5; side = 0; }
      else if (span_starts_ci(&who, "left of ")) { who.p += 8; who.n -= 8; side = 1; }
      else if (span_starts_ci(&who, "right of ")) { who.p += 9; who.n -= 9; side = 2; }
      else { reason_set(reason, "a note is `over`, `left of` or `right of` a participant"); return 0; }
      {
        size_t comma = 0;
        Span id1 = who, id2 = {NULL, 0};
        while (comma < who.n && who.p[comma] != ',') ++comma;
        if (comma < who.n) {
          id1.n = comma;
          id2.p = who.p + comma + 1;
          id2.n = who.n - comma - 1;
        }
        trim(&id1);
        trim(&id2);
        a = seq_part(s, id1, NULL);
        b = id2.n ? seq_part(s, id2, NULL) : a;
      }
      if (a < 0 || b < 0) { reason_set(reason, "too many participants"); return 0; }
      e = seq_event(s, SEQ_NOTE);
      e->from = imin(a, b);
      e->to = imax(a, b);
      e->note_side = side;
      clean_label(text, &e->text);
      e->depth = depth;
      continue;
    }
    if (first_word_is(&l, "loop") || first_word_is(&l, "alt") || first_word_is(&l, "opt") || first_word_is(&l, "par") ||
        first_word_is(&l, "critical") || first_word_is(&l, "break") || first_word_is(&l, "rect")) {
      size_t kn = 0;
      SeqEvent* e;
      Span t;
      while (kn < l.n && is_word_char((unsigned char)l.p[kn])) ++kn;
      t.p = l.p + kn;
      t.n = l.n - kn;
      trim(&t);
      e = seq_event(s, SEQ_BLOCK);
      rolltui_str_set(&e->keyword, l.p, kn);
      e->dashed = kn == 4 && strncasecmp(l.p, "rect", 4) == 0; /* a highlight: takes no row and draws no frame */
      clean_label(t, &e->text);
      e->depth = depth;
      if (open_n < 32) open[open_n++] = (int)s->ne - 1;
      ++depth;
      continue;
    }
    if (first_word_is(&l, "else") || first_word_is(&l, "and") || first_word_is(&l, "option")) {
      size_t kn = 0;
      SeqEvent* e;
      Span t;
      while (kn < l.n && is_word_char((unsigned char)l.p[kn])) ++kn;
      t.p = l.p + kn;
      t.n = l.n - kn;
      trim(&t);
      e = seq_event(s, SEQ_ELSE);
      rolltui_str_set(&e->keyword, l.p, kn);
      clean_label(t, &e->text);
      e->depth = depth - 1;
      continue;
    }
    if (first_word_is(&l, "end")) {
      SeqEvent* e = seq_event(s, SEQ_END);
      if (open_n > 0) {
        const int o = open[--open_n];
        e->match = o;
        s->ev[o].match = (int)s->ne - 1;
      }
      if (depth > 0) --depth;
      e->depth = depth;
      continue;
    }
    /* otherwise: a message */
    {
      size_t at, colon = 0;
      Span left, text, from_id, to_id;
      int dashed = 0, head = 0, from, to, act = 0;
      size_t opn = 0;
      SeqEvent* e;
      while (colon < l.n && l.p[colon] != ':') ++colon;
      left.p = l.p;
      left.n = colon < l.n ? colon : l.n;
      if (colon < l.n) { text.p = l.p + colon + 1; text.n = l.n - colon - 1; }
      else { text.p = l.p + l.n; text.n = 0; }
      for (at = 0; at < left.n; ++at) {
        if (left.p[at] == '-') {
          opn = seq_operator(left, at, &dashed, &head);
          if (opn) break;
        }
      }
      if (!opn) {
        char msg[160];
        snprintf(msg, sizeof msg, "a line of a sequence diagram is not understood: %.*s", (int)imin((int)l.n, 60), l.p);
        reason_set(reason, msg);
        return 0;
      }
      from_id.p = left.p;
      from_id.n = at;
      to_id.p = left.p + at + opn;
      to_id.n = left.n - at - opn;
      trim(&from_id);
      trim(&to_id);
      if (to_id.n && (to_id.p[0] == '+' || to_id.p[0] == '-')) { act = to_id.p[0] == '+' ? 1 : -1; ++to_id.p; --to_id.n; trim(&to_id); }
      if (from_id.n && (from_id.p[from_id.n - 1] == '+' || from_id.p[from_id.n - 1] == '-')) { --from_id.n; trim(&from_id); }
      from = seq_part(s, from_id, NULL);
      to = seq_part(s, to_id, NULL);
      if (from < 0 || to < 0 || from_id.n == 0 || to_id.n == 0) { reason_set(reason, "a message needs a participant at each end"); return 0; }
      e = seq_event(s, SEQ_MSG);
      e->from = from;
      e->to = to;
      e->dashed = dashed;
      e->head = head;
      e->depth = depth;
      clean_label(text, &e->text);
      if (*autonumber) e->number = ++number;
      if (act > 0) { SeqEvent* a2 = seq_event(s, SEQ_ACTIVATE); a2->from = a2->to = to; }
      if (act < 0) { SeqEvent* a2 = seq_event(s, SEQ_DEACTIVATE); a2->from = a2->to = from; }
    }
  }
  return 1;
}

/* the widest line of a label and how many it has */
static int label_extent(RolltuiMermaid* m, const RolltuiStr* s, int* lines) {
  size_t i = 0;
  int widest = 0, n = 0;
  while (i <= s->n) {
    size_t e = i;
    while (e < s->n && s->p[e] != '\n') ++e;
    widest = imax(widest, text_width(m, s->p + i, e - i));
    ++n;
    i = e + 1;
  }
  if (lines) *lines = n;
  return widest;
}

/* A label's lines each centred in `w` cells starting at column `x`, row `y` on. */
static void put_label_centered(RolltuiMermaid* m, int x, int y, int w, const RolltuiStr* s, unsigned char cls) {
  size_t i = 0;
  int row = 0;
  while (i <= s->n) {
    size_t e = i;
    int lw;
    while (e < s->n && s->p[e] != '\n') ++e;
    lw = text_width(m, s->p + i, e - i);
    put_text(m, x + (w - lw) / 2, y + row, s->p + i, e - i, cls);
    ++row;
    i = e + 1;
  }
}

static const char* g_arrow_r(int ascii) { return ascii ? ">" : "\xE2\x96\xB6"; }
static const char* g_arrow_l(int ascii) { return ascii ? "<" : "\xE2\x97\x80"; }

static int draw_sequence(RolltuiMermaid* m, const Source* src, size_t first, int max_width, RolltuiStr* reason) {
  Seq s;
  int autonumber = 0, ok = 0;
  size_t i, k;
  int* gap = NULL;
  int total_h = 0, row, box_h = 3, left_extra = 0, right_extra = 0, x0;
  int frame_left_min = 0, bottom_boxes = 0, life_end = 0;
  memset(&s, 0, sizeof s);
  if (!seq_parse(&s, src, first, &autonumber, reason)) goto done;
  if (s.np == 0) { reason_set(reason, "a sequence diagram with no participants"); goto done; }
  /* ---- the boxes */
  for (i = 0; i < s.np; ++i) {
    int lines;
    const int lw = label_extent(m, &s.parts[i].label, &lines);
    s.parts[i].w = lw + 4;
    s.parts[i].h = lines + 2;
    box_h = imax(box_h, s.parts[i].h);
  }
  /* ---- the distance between neighbouring lifelines: at least what keeps the boxes apart, then whatever the
   * messages, notes and frames between them need, taken from the shortest span outward */
  gap = (int*)rolltui_mem_alloc((s.np + 1) * sizeof *gap);
  for (i = 0; i + 1 < s.np; ++i) gap[i] = (s.parts[i].w + 1) / 2 + (s.parts[i + 1].w + 1) / 2 + 2;
  gap[s.np - 1] = 0;
  for (k = 1; k < s.np; ++k) {
    for (i = 0; i < s.ne; ++i) {
      const SeqEvent* e = &s.ev[i];
      int need = 0, a = -1, b = -1, span;
      if (e->kind == SEQ_MSG) {
        const int tw = label_extent(m, &e->text, NULL) + (e->number ? 4 : 0);
        if (e->from == e->to) {
          /* a message to oneself loops out to the right and needs room for its label there */
          if ((size_t)e->from + 1 < s.np) { a = e->from; b = e->from + 1; need = tw + 6; }
          else continue;
        } else {
          a = imin(e->from, e->to);
          b = imax(e->from, e->to);
          need = tw + 4;
        }
      } else if (e->kind == SEQ_NOTE) {
        const int tw = label_extent(m, &e->text, NULL) + 4;
        if (e->note_side == 0) {
          if (e->from == e->to) {
            /* over one: centred on its lifeline, so half of it needs room on each side */
            if (e->from > 0) { a = e->from - 1; b = e->from; need = (tw + 1) / 2 + 2; }
            else continue;
          } else { a = e->from; b = e->to; need = tw - 2; }
        } else if (e->note_side == 2) {
          if ((size_t)e->to + 1 < s.np) { a = e->to; b = e->to + 1; need = tw + 4; }
          else continue;
        } else {
          if (e->from > 0) { a = e->from - 1; b = e->from; need = tw + 4; }
          else continue;
        }
      } else continue;
      span = b - a;
      if (span != (int)k) continue;
      {
        int have = 0, j;
        for (j = a; j < b; ++j) have += gap[j];
        if (have < need) {
          const int deficit = need - have, each = (deficit + span - 1) / span;
          for (j = a; j < b; ++j) gap[j] += each;
        }
      }
    }
  }
  s.parts[0].cx = s.parts[0].w / 2;
  for (i = 1; i < s.np; ++i) s.parts[i].cx = s.parts[i - 1].cx + gap[i - 1];
  /* ---- rows */
  row = box_h;
  for (i = 0; i < s.ne; ++i) {
    SeqEvent* e = &s.ev[i];
    e->row = row;
    switch (e->kind) {
      case SEQ_MSG: {
        int lines;
        label_extent(m, &e->text, &lines);
        if (e->from == e->to) e->rows = 2 + (e->text.n ? 1 : 0) - (e->text.n ? 0 : 0);
        else e->rows = e->text.n ? lines + 1 : 1;
        if (e->from == e->to && e->text.n) e->rows = lines + 2;
        break;
      }
      case SEQ_NOTE: {
        int lines;
        label_extent(m, &e->text, &lines);
        e->rows = lines + 3; /* a blank row after it, so what follows is not against its border */
        break;
      }
      case SEQ_BLOCK: e->rows = e->dashed ? 0 : 1; break;
      case SEQ_ELSE: e->rows = 1; break;
      case SEQ_END: e->rows = e->match >= 0 && s.ev[e->match].dashed ? 0 : 1; break;
      case SEQ_ACTIVATE: case SEQ_DEACTIVATE:
        /* on the row of the message just before it, where the arrow is */
        e->rows = 0;
        e->row = imax(row - 1, box_h);
        break;
      default: e->rows = 0; break;
    }
    row += e->rows;
  }
  total_h = row + 1;
  /* a tall diagram names its participants again at the foot, as one reads down to the end of it */
  life_end = total_h;
  if (row - box_h >= 14) { bottom_boxes = 1; total_h += box_h + 1; }
  /* ---- how far the frames reach beyond the lifelines they wrap, so the grid can start left of the first box */
  for (i = 0; i < s.ne; ++i) {
    const SeqEvent* e = &s.ev[i];
    if (e->kind == SEQ_BLOCK && !e->dashed) {
      int lo = INT_MAX, hi = INT_MIN, nest = 0;
      size_t j;
      const int end = e->match >= 0 ? e->match : (int)s.ne - 1;
      int level = 0;
      for (j = i + 1; j <= (size_t)end && j < s.ne; ++j) {
        const SeqEvent* f = &s.ev[j];
        if (f->kind == SEQ_BLOCK) { ++level; if (!f->dashed) nest = imax(nest, level); }
        else if (f->kind == SEQ_END) --level;
        if (f->kind == SEQ_MSG) { lo = imin(lo, imin(f->from, f->to)); hi = imax(hi, imax(f->from, f->to)); }
        if (f->kind == SEQ_NOTE) { lo = imin(lo, f->from); hi = imax(hi, f->to); }
      }
      if (lo == INT_MAX) { lo = 0; hi = (int)s.np - 1; }
      {
        int lx = s.parts[lo].cx - 2 - nest, rx = s.parts[hi].cx + 2 + nest;
        const int tw = label_extent(m, &e->text, NULL) + (int)e->keyword.n + 6;
        if (rx - lx < tw) rx = lx + tw;
        frame_left_min = imin(frame_left_min, lx - (s.parts[0].cx - s.parts[0].w / 2));
        right_extra = imax(right_extra, rx - (s.parts[s.np - 1].cx + (s.parts[s.np - 1].w + 1) / 2));
      }
    } else if (e->kind == SEQ_NOTE && e->note_side == 1) {
      const int tw = label_extent(m, &e->text, NULL) + 4;
      const int lx = s.parts[e->from].cx - 2 - tw;
      frame_left_min = imin(frame_left_min, lx - (s.parts[0].cx - s.parts[0].w / 2));
    } else if (e->kind == SEQ_NOTE && e->note_side == 2) {
      const int tw = label_extent(m, &e->text, NULL) + 4;
      const int rx = s.parts[e->to].cx + 2 + tw;
      right_extra = imax(right_extra, rx - (s.parts[s.np - 1].cx + (s.parts[s.np - 1].w + 1) / 2));
    } else if (e->kind == SEQ_NOTE && e->note_side == 0) {
      const int tw = label_extent(m, &e->text, NULL) + 4;
      const int rx = imax(s.parts[e->to].cx + (tw + 1) / 2, s.parts[e->from].cx + tw);
      const int lx = imin(s.parts[e->from].cx - tw / 2, s.parts[e->to].cx - 1);
      right_extra = imax(right_extra, rx - (s.parts[s.np - 1].cx + (s.parts[s.np - 1].w + 1) / 2));
      frame_left_min = imin(frame_left_min, lx - (s.parts[0].cx - s.parts[0].w / 2));
    } else if (e->kind == SEQ_MSG && e->from == e->to) {
      const int tw = label_extent(m, &e->text, NULL) + 6;
      const int rx = s.parts[e->from].cx + 2 + tw;
      right_extra = imax(right_extra, rx - (s.parts[s.np - 1].cx + (s.parts[s.np - 1].w + 1) / 2));
    }
  }
  left_extra = -frame_left_min;
  x0 = left_extra;
  {
    const int grid_w = x0 + s.parts[s.np - 1].cx + (s.parts[s.np - 1].w + 1) / 2 + 1 + imax(right_extra, 0);
    if (grid_w > max_width) {
      reason_setf(reason, "the diagram needs %d columns; this view has %d", grid_w, max_width);
      goto done;
    }
    grid_init(&m->g, grid_w, total_h);
  }
  /* ---- draw: boxes, lifelines, frames, messages, notes, activations, in that order */
  for (i = 0; i < s.np; ++i) {
    const SeqPart* p = &s.parts[i];
    const int bx = x0 + p->cx - p->w / 2;
    int y, lines;
    label_extent(m, &p->label, &lines);
    for (y = 0; y < box_h; ++y) {
      int x;
      const int top = y == 0, bottom = y == box_h - 1;
      for (x = 0; x < p->w; ++x) {
        const int left = x == 0, right = x == p->w - 1;
        const char* g = NULL;
        if (top && left) g = m->ascii ? "+" : "\xE2\x95\xAD";
        else if (top && right) g = m->ascii ? "+" : "\xE2\x95\xAE";
        else if (bottom && left) g = m->ascii ? "+" : "\xE2\x95\xB0";
        else if (bottom && right) g = m->ascii ? "+" : "\xE2\x95\xAF";
        else if (top || bottom) g = m->ascii ? "-" : "\xE2\x94\x80";
        else if (left || right) g = m->ascii ? "|" : "\xE2\x94\x82";
        if (g) put_glyph(&m->g, bx + x, y, g, ROLLTUI_MERMAID_CLASS_NODE);
      }
    }
    /* the label centred in the box's height, so a taller neighbour does not leave a short box's text at the top */
    put_label_centered(m, bx + 2, 1 + (box_h - 2 - lines) / 2, p->w - 4, &p->label, ROLLTUI_MERMAID_CLASS_TEXT);
    /* the lifeline, from the box's foot down */
    if (!m->ascii) put_glyph(&m->g, x0 + p->cx, box_h - 1, "\xE2\x94\xAC", ROLLTUI_MERMAID_CLASS_NODE);
    else put_glyph(&m->g, x0 + p->cx, box_h - 1, "+", ROLLTUI_MERMAID_CLASS_NODE);
    for (k = (size_t)box_h; k < (size_t)life_end; ++k)
      add_mask(&m->g, x0 + p->cx, (int)k, M_U | M_D, ST_DASH, ROLLTUI_MERMAID_CLASS_MUTED);
    if (bottom_boxes) {
      const int top = total_h - box_h;
      int yy;
      for (yy = 0; yy < box_h; ++yy) {
        int x;
        for (x = 0; x < p->w; ++x) {
          const int t2 = yy == 0, b2 = yy == box_h - 1, l2 = x == 0, r2 = x == p->w - 1;
          const char* g = NULL;
          if (t2 && l2) g = m->ascii ? "+" : "\xE2\x95\xAD";
          else if (t2 && r2) g = m->ascii ? "+" : "\xE2\x95\xAE";
          else if (b2 && l2) g = m->ascii ? "+" : "\xE2\x95\xB0";
          else if (b2 && r2) g = m->ascii ? "+" : "\xE2\x95\xAF";
          else if (t2 || b2) g = m->ascii ? "-" : "\xE2\x94\x80";
          else if (l2 || r2) g = m->ascii ? "|" : "\xE2\x94\x82";
          if (g) put_glyph(&m->g, bx + x, top + yy, g, ROLLTUI_MERMAID_CLASS_NODE);
        }
      }
      put_label_centered(m, bx + 2, top + 1 + (box_h - 2 - lines) / 2, p->w - 4, &p->label, ROLLTUI_MERMAID_CLASS_TEXT);
      put_glyph(&m->g, x0 + p->cx, top, m->ascii ? "+" : "\xE2\x94\xB4", ROLLTUI_MERMAID_CLASS_NODE);
    }
  }
  /* frames */
  for (i = 0; i < s.ne; ++i) {
    const SeqEvent* e = &s.ev[i];
    if (e->kind == SEQ_BLOCK && !e->dashed) {
      int lo = INT_MAX, hi = INT_MIN, nest = 0, level = 0, lx, rx, x, y, last;
      size_t j;
      const int end = e->match >= 0 ? e->match : (int)s.ne - 1;
      for (j = i + 1; j <= (size_t)end && j < s.ne; ++j) {
        const SeqEvent* f = &s.ev[j];
        if (f->kind == SEQ_BLOCK) { ++level; if (!f->dashed) nest = imax(nest, level); }
        else if (f->kind == SEQ_END) --level;
        if (f->kind == SEQ_MSG) { lo = imin(lo, imin(f->from, f->to)); hi = imax(hi, imax(f->from, f->to)); }
        if (f->kind == SEQ_NOTE) { lo = imin(lo, f->from); hi = imax(hi, f->to); }
      }
      if (lo == INT_MAX) { lo = 0; hi = (int)s.np - 1; }
      lx = x0 + s.parts[lo].cx - 2 - nest;
      rx = x0 + s.parts[hi].cx + 2 + nest;
      {
        const int tw = label_extent(m, &e->text, NULL) + (int)e->keyword.n + 6;
        if (rx - lx < tw) rx = lx + tw;
      }
      last = e->match >= 0 ? s.ev[e->match].row : life_end - 1;
      for (x = lx; x <= rx; ++x) {
        add_mask(&m->g, x, e->row, (x > lx ? M_L : 0) | (x < rx ? M_R : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
        add_mask(&m->g, x, last, (x > lx ? M_L : 0) | (x < rx ? M_R : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
      }
      for (y = e->row; y <= last; ++y) {
        add_mask(&m->g, lx, y, (y > e->row ? M_U : 0) | (y < last ? M_D : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
        add_mask(&m->g, rx, y, (y > e->row ? M_U : 0) | (y < last ? M_D : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
      }
      /* the tab: [keyword] title, on the top border */
      {
        char tab[160];
        const int tn = snprintf(tab, sizeof tab, " %.*s%s%.*s ", (int)imin((int)e->keyword.n, 20), e->keyword.p ? e->keyword.p : "",
                                e->text.n ? " " : "", (int)imin((int)e->text.n, 90), e->text.p ? e->text.p : "");
        put_text(m, lx + 2, e->row, tab, (size_t)tn, ROLLTUI_MERMAID_CLASS_TITLE);
      }
    }
  }
  for (i = 0; i < s.ne; ++i) {
    const SeqEvent* e = &s.ev[i];
    if (e->kind == SEQ_ELSE) {
      /* a divider across the frame it is in: the nearest enclosing open frame */
      int owner = -1, depth = 0;
      size_t j;
      for (j = i; j-- > 0;) {
        if (s.ev[j].kind == SEQ_END) ++depth;
        else if (s.ev[j].kind == SEQ_BLOCK) {
          if (depth == 0) { if (s.ev[j].dashed) continue; owner = (int)j; break; }
          --depth;
        }
      }
      if (owner >= 0) {
        const SeqEvent* b = &s.ev[owner];
        int lo = INT_MAX, hi = INT_MIN, nest = 0, level = 0, lx, rx, x;
        const int end = b->match >= 0 ? b->match : (int)s.ne - 1;
        for (j = (size_t)owner + 1; j <= (size_t)end && j < s.ne; ++j) {
          const SeqEvent* f = &s.ev[j];
          if (f->kind == SEQ_BLOCK) { ++level; nest = imax(nest, level); }
          else if (f->kind == SEQ_END) --level;
          if (f->kind == SEQ_MSG) { lo = imin(lo, imin(f->from, f->to)); hi = imax(hi, imax(f->from, f->to)); }
          if (f->kind == SEQ_NOTE) { lo = imin(lo, f->from); hi = imax(hi, f->to); }
        }
        if (lo == INT_MAX) { lo = 0; hi = (int)s.np - 1; }
        lx = x0 + s.parts[lo].cx - 2 - nest;
        rx = x0 + s.parts[hi].cx + 2 + nest;
        {
          const int tw = label_extent(m, &b->text, NULL) + (int)b->keyword.n + 6;
          if (rx - lx < tw) rx = lx + tw;
        }
        for (x = lx; x <= rx; ++x) {
          if (x == lx) put_glyph(&m->g, x, e->row, m->ascii ? "+" : "\xE2\x94\x9C", ROLLTUI_MERMAID_CLASS_BOX);
          else if (x == rx) put_glyph(&m->g, x, e->row, m->ascii ? "+" : "\xE2\x94\xA4", ROLLTUI_MERMAID_CLASS_BOX);
          else put_glyph(&m->g, x, e->row, m->ascii ? "." : "\xE2\x94\x84", ROLLTUI_MERMAID_CLASS_BOX);
        }
        {
          char tab[160];
          const int tn = snprintf(tab, sizeof tab, " %.*s%s%.*s ", (int)imin((int)e->keyword.n, 20), e->keyword.p ? e->keyword.p : "",
                                  e->text.n ? " " : "", (int)imin((int)e->text.n, 90), e->text.p ? e->text.p : "");
          put_text(m, lx + 2, e->row, tab, (size_t)tn, ROLLTUI_MERMAID_CLASS_TITLE);
        }
      }
    }
  }
  /* messages */
  for (i = 0; i < s.ne; ++i) {
    const SeqEvent* e = &s.ev[i];
    if (e->kind != SEQ_MSG) continue;
    {
      const int fx = x0 + s.parts[e->from].cx, tx = x0 + s.parts[e->to].cx;
      const int style = e->dashed ? ST_DASH : ST_LIGHT;
      char num[16];
      RolltuiStr label;
      int lines, lw, y;
      memset(&label, 0, sizeof label);
      if (e->number) {
        const int nn = snprintf(num, sizeof num, "%d. ", e->number);
        rolltui_str_append(&label, num, (size_t)nn);
      }
      rolltui_str_append_str(&label, &e->text);
      lw = label_extent(m, &label, &lines);
      if (e->from == e->to) {
        /* out to the right, down, and back */
        const int top = e->row + (e->text.n ? lines : 0), w = 3;
        if (e->text.n) put_label_centered(m, fx + 2, e->row, lw, &label, ROLLTUI_MERMAID_CLASS_LABEL);
        y = top;
        line_between(&m->g, fx, y, fx + w, y, style, ROLLTUI_MERMAID_CLASS_EDGE);
        line_between(&m->g, fx + w, y, fx + w, y + 1, style, ROLLTUI_MERMAID_CLASS_EDGE);
        line_between(&m->g, fx + w, y + 1, fx + 1, y + 1, style, ROLLTUI_MERMAID_CLASS_EDGE);
        put_glyph(&m->g, fx, y, m->ascii ? "+" : "\xE2\x94\x9C", ROLLTUI_MERMAID_CLASS_EDGE);
        put_glyph(&m->g, fx + 1, y + 1, m->ascii ? "<" : "\xE2\x97\x80", ROLLTUI_MERMAID_CLASS_ARROW);
        (void)tx;
      } else {
        const int right = tx > fx;
        const int lo = imin(fx, tx), hi = imax(fx, tx);
        y = e->row + (e->text.n || e->number ? lines : 0);
        if (e->text.n || e->number) {
          const int room = hi - lo - 1;
          const int start = lo + 1 + (room - lw) / 2;
          put_label_centered(m, imax(start, lo + 1), e->row, lw, &label, ROLLTUI_MERMAID_CLASS_LABEL);
        }
        line_between(&m->g, lo + 1, y, hi - 1, y, style, ROLLTUI_MERMAID_CLASS_EDGE);
        {
          Cell* c;
          if ((c = cell_at(&m->g, lo + 1, y)) != NULL && lo + 1 <= hi - 1) { c->mask |= 0; }
        }
        /* the ends: where it leaves, and its head where it lands */
        put_glyph(&m->g, fx, y, right ? (m->ascii ? "+" : "\xE2\x94\x9C") : (m->ascii ? "+" : "\xE2\x94\xA4"), ROLLTUI_MERMAID_CLASS_EDGE);
        {
          const int hx = right ? tx - 1 : tx + 1;
          const char* h = NULL;
          switch (e->head) {
            case SEQ_HEAD_ARROW: h = right ? g_arrow_r(m->ascii) : g_arrow_l(m->ascii); break;
            case SEQ_HEAD_CROSS: h = m->ascii ? "x" : "\xE2\x9C\x95"; break;
            case SEQ_HEAD_ASYNC: h = m->ascii ? ")" : (right ? "\xE2\x96\xB7" : "\xE2\x97\x81"); break;
            default: break;
          }
          if (h) put_glyph(&m->g, hx, y, h, ROLLTUI_MERMAID_CLASS_ARROW);
          else put_glyph(&m->g, tx, y, right ? (m->ascii ? "+" : "\xE2\x94\xA4") : (m->ascii ? "+" : "\xE2\x94\x9C"), ROLLTUI_MERMAID_CLASS_EDGE);
        }
      }
      rolltui_str_free(&label);
    }
  }
  /* notes, drawn over whatever lifelines they cross */
  for (i = 0; i < s.ne; ++i) {
    const SeqEvent* e = &s.ev[i];
    if (e->kind != SEQ_NOTE) continue;
    {
      int lines, tw = label_extent(m, &e->text, &lines) + 4, lx, w, x, y;
      if (e->note_side == 0) {
        const int a = x0 + s.parts[e->from].cx, b = x0 + s.parts[e->to].cx;
        if (e->from == e->to) { lx = a - tw / 2; w = tw; }
        else {
          w = imax(tw, b - a + 3);
          lx = a - 1 - (w - (b - a + 3)) / 2;
        }
      } else if (e->note_side == 1) { w = tw; lx = x0 + s.parts[e->from].cx - 2 - w + 1; }
      else { w = tw; lx = x0 + s.parts[e->to].cx + 2; }
      for (y = 0; y < lines + 2; ++y) {
        for (x = 0; x < w; ++x) {
          const int top = y == 0, bottom = y == lines + 1, left = x == 0, right = x == w - 1;
          const char* g = " ";
          if (top && left) g = m->ascii ? "+" : "\xE2\x94\x8C";
          else if (top && right) g = m->ascii ? "+" : "\xE2\x94\x90";
          else if (bottom && left) g = m->ascii ? "+" : "\xE2\x94\x94";
          else if (bottom && right) g = m->ascii ? "+" : "\xE2\x94\x98";
          else if (top || bottom) g = m->ascii ? "-" : "\xE2\x94\x80";
          else if (left || right) g = m->ascii ? "|" : "\xE2\x94\x82";
          {
            Cell* c = cell_at(&m->g, lx + x, e->row + y);
            if (c) { c->mask = 0; c->g[0] = 0; c->width = 1; }
          }
          put_glyph(&m->g, lx + x, e->row + y, g, ROLLTUI_MERMAID_CLASS_MUTED);
        }
      }
      put_label_centered(m, lx + 2, e->row + 1, w - 4, &e->text, ROLLTUI_MERMAID_CLASS_TEXT);
    }
  }
  /* activations: a heavier lifeline while a participant is active */
  for (i = 0; i < s.np; ++i) {
    int start = -1, depth = 0;
    size_t j;
    for (j = 0; j < s.ne; ++j) {
      const SeqEvent* e = &s.ev[j];
      if (e->kind == SEQ_ACTIVATE && e->from == (int)i) {
        if (depth++ == 0) start = e->row;
      } else if (e->kind == SEQ_DEACTIVATE && e->from == (int)i && depth > 0) {
        if (--depth == 0 && start >= 0) {
          int y;
          for (y = start; y <= e->row; ++y) {
            Cell* c = cell_at(&m->g, x0 + s.parts[i].cx, y);
            if (c && c->g[0] == 0 && c->mask != 0) { c->style = ST_HEAVY; c->cls = ROLLTUI_MERMAID_CLASS_ACCENT1; }
          }
          start = -1;
        }
      }
    }
    if (depth > 0 && start >= 0) {
      int y;
      for (y = start; y < life_end; ++y) {
        Cell* c = cell_at(&m->g, x0 + s.parts[i].cx, y);
        if (c && c->g[0] == 0 && c->mask != 0) { c->style = ST_HEAVY; c->cls = ROLLTUI_MERMAID_CLASS_ACCENT1; }
      }
    }
  }
  m->kind = "sequence";
  ok = 1;
done:
  rolltui_mem_free(gap);
  seq_release(&s);
  return ok;
}

/* ============================================================================================
 * GRAPHS: flowcharts, and everything else that is boxes joined by lines
 * ============================================================================================ */

#define SH_RECT 0
#define SH_ROUND 1
#define SH_STADIUM 2
#define SH_SUBROUTINE 3
#define SH_CYLINDER 4
#define SH_CIRCLE 5
#define SH_DOUBLECIRCLE 6
#define SH_DIAMOND 7
#define SH_HEXAGON 8
#define SH_FLAG 9
#define SH_PARA 10
#define SH_PARA_ALT 11
#define SH_TRAP 12
#define SH_TRAP_ALT 13
#define SH_START 14  /* a state's start: ● */
#define SH_END 15    /* a state's end: ◉ */
#define SH_FORK 16   /* a state's fork or join: a bar */
#define SH_RECORD 17 /* a class or an entity: a title over a rule over lines */

#define HEAD_NONE 0
#define HEAD_ARROW 1
#define HEAD_CIRCLE 2
#define HEAD_CROSS 3
#define HEAD_TRIANGLE 4 /* inheritance */
#define HEAD_DIAMOND 5  /* composition */
#define HEAD_DIAMOND_OPEN 6 /* aggregation */
#define HEAD_CROWFOOT_MANY 7
#define HEAD_CROWFOOT_ONE 8

#define ES_SOLID 0
#define ES_DOTTED 1
#define ES_THICK 2
#define ES_INVISIBLE 3

typedef struct GNode {
  RolltuiStr id;
  RolltuiStr label;   /* '\n' between lines; empty means the id */
  RolltuiStr detail;  /* a record's lines under its title, '\n' between */
  int shape;
  int sub;            /* the subgraph it is in, or -1 */
  int w, h;           /* the shape's size in cells */
  int halo;           /* cells reserved to the right of it for a self-loop */
  /* layout, in a frame where "main" runs along the flow and "cross" across it */
  int layer, order;
  int cs, ms;         /* size across / along */
  int cpos, mpos;     /* where its first cell is */
  double target;
  int dummy;          /* a stand-in for an edge in a layer it passes through */
  int x, y;           /* the real position of its first cell */
} GNode;

typedef struct GEdge {
  int from, to;
  RolltuiStr label;
  int style;
  int head_to, head_from;
  int minlen;
  int rev;            /* reversed for layout: it points up the flow */
  int parallel;       /* which of several between one pair this is */
  int parallel_n;
  int* chain;         /* the nodes it passes through, in layer order (real ends and its stand-ins) */
  size_t chain_n;
  RolltuiStr end_a, end_b; /* small text at its first end and its last (a cardinality), in the order it was written */
  int label_w;        /* the width of its label, 0 for none: known before layout, so a node can be made wide enough for it */
  int pa, pb;         /* where it attaches across the flow at its first node and at its last */
  int pa_single, pb_single; /* that end is the only kind on its side, so it can move to meet the other */
} GEdge;

typedef struct GSub {
  RolltuiStr id;
  RolltuiStr title;
  int parent;
  int dir;            /* 0: as the diagram, else 1 TD 2 BT 3 LR 4 RL (parsed, used only for the diagram's own) */
  int tw;             /* the title's width in cells */
  int fl, fr, ft, fb; /* the frame, in real cells, once laid out */
} GSub;

#define DIR_TD 0
#define DIR_BT 1
#define DIR_LR 2
#define DIR_RL 3

typedef struct Graph {
  GNode* n;
  size_t nn, ncap;
  GEdge* e;
  size_t ne, ecap;
  GSub* s;
  size_t ns, scap;
  int dir;
  int state;          /* a state diagram: [*] is a start or an end by where it is used */
  size_t start_seq, end_seq;
} Graph;

static void graph_release(Graph* g) {
  size_t i;
  for (i = 0; i < g->nn; ++i) {
    rolltui_str_free(&g->n[i].id);
    rolltui_str_free(&g->n[i].label);
    rolltui_str_free(&g->n[i].detail);
  }
  for (i = 0; i < g->ne; ++i) {
    rolltui_str_free(&g->e[i].label);
    rolltui_str_free(&g->e[i].end_a);
    rolltui_str_free(&g->e[i].end_b);
    rolltui_mem_free(g->e[i].chain);
  }
  for (i = 0; i < g->ns; ++i) {
    rolltui_str_free(&g->s[i].id);
    rolltui_str_free(&g->s[i].title);
  }
  rolltui_mem_free(g->n);
  rolltui_mem_free(g->e);
  rolltui_mem_free(g->s);
  memset(g, 0, sizeof *g);
}

static int graph_find(const Graph* g, Span id) {
  size_t i;
  for (i = 0; i < g->nn; ++i)
    if (g->n[i].id.n == id.n && memcmp(g->n[i].id.p, id.p, id.n) == 0) return (int)i;
  return -1;
}

static int graph_sub_find(const Graph* g, Span id) {
  size_t i;
  for (i = 0; i < g->ns; ++i)
    if (g->s[i].id.n == id.n && memcmp(g->s[i].id.p, id.p, id.n) == 0) return (int)i;
  return -1;
}

static int graph_node(Graph* g, Span id, int sub) {
  int at = graph_find(g, id);
  if (at >= 0) {
    if (sub >= 0 && g->n[at].sub < 0) g->n[at].sub = sub;
    return at;
  }
  if (g->nn >= MAX_NODES) return -1;
  g->n = (GNode*)rolltui_grow(g->n, &g->ncap, g->nn + 1, sizeof *g->n);
  memset(&g->n[g->nn], 0, sizeof g->n[g->nn]);
  rolltui_str_set(&g->n[g->nn].id, id.p, id.n);
  g->n[g->nn].sub = sub;
  return (int)g->nn++;
}

static int graph_edge(Graph* g, int from, int to, int style, int head_from, int head_to, int minlen, Span label) {
  GEdge* e;
  if (g->ne >= MAX_EDGES) return 0;
  g->e = (GEdge*)rolltui_grow(g->e, &g->ecap, g->ne + 1, sizeof *g->e);
  e = &g->e[g->ne++];
  memset(e, 0, sizeof *e);
  e->from = from;
  e->to = to;
  e->style = style;
  e->head_from = head_from;
  e->head_to = head_to;
  e->minlen = minlen < 1 ? 1 : minlen;
  if (label.n) clean_label(label, &e->label);
  return 1;
}

/* ---- the text of a flowchart ---------------------------------------------------------------- */

typedef struct Parse {
  Graph* g;
  int sub; /* the subgraph being read, or -1 */
  RolltuiStr* reason;
} Parse;

/* Where `closer` next appears in `s` outside a quoted string, or `s.n` when it does not. */
static size_t find_close(Span s, const char* closer) {
  const size_t cn = strlen(closer);
  size_t i = 0;
  int quoted = 0;
  while (i < s.n) {
    if (s.p[i] == '"') quoted = !quoted;
    else if (!quoted && s.n - i >= cn && memcmp(s.p + i, closer, cn) == 0) return i;
    ++i;
  }
  return s.n;
}

static int is_id_char(const Span* s, size_t i) {
  const unsigned char c = (unsigned char)s->p[i];
  if (isalnum(c) || c == '_' || c >= 0x80) return 1;
  /* a hyphen inside a name (`node-1`), but never the start of `--`, `-.`, or `->` */
  if (c == '-' && i > 0 && i + 1 < s->n) {
    const unsigned char nx = (unsigned char)s->p[i + 1];
    if (isalnum(nx) || nx == '_') return 1;
  }
  return 0;
}

/* Reads `id` and its shape, if it has one, at the start of `*s`. Returns the node, or -1. */
static int parse_node(Parse* p, Span* s) {
  Span id;
  size_t i = 0;
  int shape = -1, node;
  Span text = {NULL, 0};
  int has_text = 0;
  skip_ws(s);
  if (span_starts(s, "[*]")) {
    /* a state diagram's start or end: which is decided by where it is used, by the caller */
    return -2;
  }
  while (i < s->n && is_id_char(s, i)) ++i;
  if (i == 0) return -1;
  id.p = s->p;
  id.n = i;
  s->p += i;
  s->n -= i;
  /* the name of a subgraph that is already open or closed is the subgraph, not a node of that name */
  if (graph_find(p->g, id) < 0 && graph_sub_find(p->g, id) >= 0 && !(s->n && (s->p[0] == '[' || s->p[0] == '(' || s->p[0] == '{'))) return -100 - graph_sub_find(p->g, id);
  if (s->n > 0) {
    Span r = *s;
    size_t e;
    switch (r.p[0]) {
      case '[':
        if (r.n > 1 && r.p[1] == '[') { r.p += 2; r.n -= 2; e = find_close(r, "]]"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_SUBROUTINE; r.p += e + 2; r.n -= e + 2; } }
        else if (r.n > 1 && r.p[1] == '(') { r.p += 2; r.n -= 2; e = find_close(r, ")]"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_CYLINDER; r.p += e + 2; r.n -= e + 2; } }
        else if (r.n > 1 && (r.p[1] == '/' || r.p[1] == '\\')) {
          const char open = r.p[1];
          Span in = {r.p + 2, r.n - 2};
          size_t e1 = find_close(in, "/]"), e2 = find_close(in, "\\]");
          const size_t e_ = e1 < e2 ? e1 : e2;
          if (e_ < in.n) {
            const char close = in.p[e_];
            text.p = in.p;
            text.n = e_;
            has_text = 1;
            shape = open == '/' ? (close == '/' ? SH_PARA : SH_TRAP) : (close == '\\' ? SH_PARA_ALT : SH_TRAP_ALT);
            r.p = in.p + e_ + 2;
            r.n = in.n - e_ - 2;
          }
        } else {
          r.p += 1; r.n -= 1; e = find_close(r, "]");
          if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_RECT; r.p += e + 1; r.n -= e + 1; }
        }
        break;
      case '(':
        if (r.n > 2 && r.p[1] == '(' && r.p[2] == '(') { r.p += 3; r.n -= 3; e = find_close(r, ")))"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_DOUBLECIRCLE; r.p += e + 3; r.n -= e + 3; } }
        else if (r.n > 1 && r.p[1] == '(') { r.p += 2; r.n -= 2; e = find_close(r, "))"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_CIRCLE; r.p += e + 2; r.n -= e + 2; } }
        else if (r.n > 1 && r.p[1] == '[') { r.p += 2; r.n -= 2; e = find_close(r, "])"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_STADIUM; r.p += e + 2; r.n -= e + 2; } }
        else { r.p += 1; r.n -= 1; e = find_close(r, ")"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_ROUND; r.p += e + 1; r.n -= e + 1; } }
        break;
      case '{':
        if (r.n > 1 && r.p[1] == '{') { r.p += 2; r.n -= 2; e = find_close(r, "}}"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_HEXAGON; r.p += e + 2; r.n -= e + 2; } }
        else { r.p += 1; r.n -= 1; e = find_close(r, "}"); if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_DIAMOND; r.p += e + 1; r.n -= e + 1; } }
        break;
      case '>':
        r.p += 1; r.n -= 1; e = find_close(r, "]");
        if (e < r.n) { text.p = r.p; text.n = e; has_text = 1; shape = SH_FLAG; r.p += e + 1; r.n -= e + 1; }
        break;
      default: break;
    }
    if (has_text) *s = r;
  }
  /* a class shorthand after the shape (`A:::hot`) is styling this drawing does not do */
  if (span_starts(s, ":::")) {
    size_t k = 3;
    while (k < s->n && is_id_char(s, k)) ++k;
    s->p += k;
    s->n -= k;
  }
  node = graph_node(p->g, id, p->sub);
  if (node < 0) { reason_set(p->reason, "a flowchart with more than 240 nodes is not drawn"); return -3; }
  if (has_text) {
    clean_label(text, &p->g->n[node].label);
    p->g->n[node].shape = shape;
  }
  return node;
}

/* What a link is, read at the start of `*s`. Returns 1 and fills the fields, or 0 when `*s` is not a link. */
typedef struct Link {
  int style, head_from, head_to, minlen;
  Span label;
  int has_label;
} Link;

static int parse_link(Span* s, Link* lk) {
  Span r = *s;
  size_t i = 0, dashes = 0;
  char body;
  int arrow_end = 0;
  memset(lk, 0, sizeof *lk);
  skip_ws(&r);
  if (r.n == 0) return 0;
  /* a head at the start: `<-->`, `o--o`, `x--x` */
  if (r.p[0] == '<' && r.n > 1 && (r.p[1] == '-' || r.p[1] == '=')) { lk->head_from = HEAD_ARROW; ++r.p; --r.n; }
  else if ((r.p[0] == 'x' || r.p[0] == 'o') && r.n > 2 && (r.p[1] == '-' || r.p[1] == '=') && (r.p[2] == '-' || r.p[2] == '=')) {
    lk->head_from = r.p[0] == 'x' ? HEAD_CROSS : HEAD_CIRCLE;
    ++r.p;
    --r.n;
  }
  if (r.n == 0) return 0;
  body = r.p[0];
  if (body == '~') {
    while (i < r.n && r.p[i] == '~') ++i;
    if (i < 3) return 0;
    lk->style = ES_INVISIBLE;
    lk->minlen = (int)i - 2;
    r.p += i;
    r.n -= i;
    *s = r;
    return 1;
  }
  if (body != '-' && body != '=') return 0;
  /* text inside the link: `-- text -->`, `== text ==>`, `-. text .->` */
  if (r.n > 3 && ((body == '-' && r.p[1] == '-' && r.p[2] == ' ') || (body == '=' && r.p[1] == '=' && r.p[2] == ' ') ||
                  (body == '-' && r.p[1] == '.' && r.p[2] == ' '))) {
    const char* tail_a = body == '-' && r.p[1] == '-' ? "-->" : body == '=' ? "==>" : ".->";
    const char* tail_b = body == '-' && r.p[1] == '-' ? "---" : body == '=' ? "===" : ".-";
    const char* tail_c = body == '-' && r.p[1] == '-' ? "--x" : NULL;
    const char* tail_d = body == '-' && r.p[1] == '-' ? "--o" : NULL;
    Span in = {r.p + 3, r.n - 3};
    size_t ea = find_close(in, tail_a), eb = find_close(in, tail_b);
    size_t ec = tail_c ? find_close(in, tail_c) : in.n, ed = tail_d ? find_close(in, tail_d) : in.n;
    size_t e = ea, tl = strlen(tail_a);
    int head = HEAD_ARROW;
    if (eb < e) { e = eb; tl = strlen(tail_b); head = HEAD_NONE; }
    if (ec < e) { e = ec; tl = 3; head = HEAD_CROSS; }
    if (ed < e) { e = ed; tl = 3; head = HEAD_CIRCLE; }
    if (e < in.n) {
      lk->label.p = in.p;
      lk->label.n = e;
      lk->has_label = 1;
      lk->style = body == '=' ? ES_THICK : (r.p[1] == '.' ? ES_DOTTED : ES_SOLID);
      lk->head_to = head;
      lk->minlen = 1;
      r.p = in.p + e + tl;
      r.n = in.n - e - tl;
      goto after;
    }
  }
  if (body == '-' && r.n > 1 && r.p[1] == '.') {
    /* dotted: -.- and -.-> and -..-> */
    size_t dots = 0;
    i = 1;
    while (i < r.n && r.p[i] == '.') { ++i; ++dots; }
    if (i >= r.n || r.p[i] != '-') return 0;
    ++i;
    lk->style = ES_DOTTED;
    lk->minlen = (int)dots;
    if (i < r.n && r.p[i] == '>') { lk->head_to = HEAD_ARROW; ++i; }
    r.p += i;
    r.n -= i;
  } else {
    while (i < r.n && r.p[i] == body) { ++i; ++dashes; }
    if (dashes < 2 && !(body == '-' && i < r.n && r.p[i] == '>')) return 0;
    if (dashes < 2) return 0;
    lk->style = body == '=' ? ES_THICK : ES_SOLID;
    if (i < r.n && r.p[i] == '>') { lk->head_to = HEAD_ARROW; arrow_end = 1; ++i; }
    else if (i < r.n && (r.p[i] == 'x' || r.p[i] == 'o') && (i + 1 >= r.n || r.p[i + 1] == ' ' || r.p[i + 1] == '|')) { lk->head_to = r.p[i] == 'x' ? HEAD_CROSS : HEAD_CIRCLE; arrow_end = 1; ++i; }
    lk->minlen = (int)dashes - (arrow_end ? 1 : 2);
    r.p += i;
    r.n -= i;
  }
after:
  skip_ws(&r);
  if (r.n && r.p[0] == '|') {
    size_t e = 1;
    while (e < r.n && r.p[e] != '|') ++e;
    if (e < r.n) {
      lk->label.p = r.p + 1;
      lk->label.n = e - 1;
      lk->has_label = 1;
      r.p += e + 1;
      r.n -= e + 1;
    }
  }
  if (lk->minlen < 1) lk->minlen = 1;
  *s = r;
  return 1;
}

typedef struct NodeSet {
  int v[16];
  int n;
} NodeSet;

/* `A & B & C`: the nodes a link joins at one end. */
static int parse_group(Parse* p, Span* s, NodeSet* set, int* start_end) {
  set->n = 0;
  *start_end = 0;
  for (;;) {
    int node;
    skip_ws(s);
    if (span_starts(s, "[*]")) {
      s->p += 3;
      s->n -= 3;
      *start_end = 1;
      node = -2;
    } else {
      node = parse_node(p, s);
      if (node == -3) return 0;
      if (node < 0 && node > -100) return 0;
    }
    if (set->n < 16) set->v[set->n++] = node;
    skip_ws(s);
    if (s->n && s->p[0] == '&') { ++s->p; --s->n; continue; }
    break;
  }
  return 1;
}

static void trim_semicolon_split(Span line, Span* out, size_t* n, size_t cap) {
  /* statements separated by `;` outside brackets and quotes */
  size_t i = 0, start = 0;
  int depth = 0, quoted = 0;
  *n = 0;
  for (i = 0; i <= line.n; ++i) {
    const char c = i < line.n ? line.p[i] : ';';
    if (i < line.n) {
      if (c == '"') quoted = !quoted;
      else if (!quoted && (c == '[' || c == '(' || c == '{')) ++depth;
      else if (!quoted && (c == ']' || c == ')' || c == '}')) --depth;
    }
    if (i == line.n || (c == ';' && depth <= 0 && !quoted)) {
      Span st = {line.p + start, i - start};
      trim(&st);
      if (st.n && *n < cap) out[(*n)++] = st;
      start = i + 1;
    }
  }
}

static int dir_from_word(Span w) {
  if (span_starts_ci(&w, "TD") || span_starts_ci(&w, "TB")) return DIR_TD;
  if (span_starts_ci(&w, "BT")) return DIR_BT;
  if (span_starts_ci(&w, "LR")) return DIR_LR;
  if (span_starts_ci(&w, "RL")) return DIR_RL;
  return -1;
}

/* An edge to a subgraph is drawn to one of its members: the first it lists for an edge that arrives, the last for
 * one that leaves — where a person reading the frame top to bottom would expect each to meet it. */
static int sub_representative(const Graph* g, int sub, int arriving) {
  size_t i;
  int found = -1;
  for (i = 0; i < g->nn; ++i) {
    int s = g->n[i].sub;
    while (s >= 0 && s != sub) s = g->s[s].parent;
    if (s != sub) continue;
    if (arriving) return (int)i;
    found = (int)i;
  }
  return found;
}

/* A subgraph named by an edge before it was declared stands as a node of that name. When the subgraph comes, that
 * node is the subgraph: its edges move to the subgraph's members and it goes. */
static void graph_resolve_phantoms(Graph* g) {
  size_t s;
  for (s = 0; s < g->ns; ++s) {
    int at = graph_find(g, (Span){g->s[s].id.p, g->s[s].id.n});
    size_t e, i;
    if (at < 0 || g->n[at].sub >= 0 || g->n[at].label.n) continue;
    if (sub_representative(g, (int)s, 1) < 0) continue;
    for (e = 0; e < g->ne; ++e) {
      GEdge* ed = &g->e[e];
      if (ed->to == at) ed->to = sub_representative(g, (int)s, 1);
      if (ed->from == at) ed->from = sub_representative(g, (int)s, 0);
    }
    /* the node goes, and every index above it moves down one */
    rolltui_str_free(&g->n[at].id);
    rolltui_str_free(&g->n[at].label);
    rolltui_str_free(&g->n[at].detail);
    for (i = (size_t)at; i + 1 < g->nn; ++i) g->n[i] = g->n[i + 1];
    --g->nn;
    for (e = 0; e < g->ne; ++e) {
      if (g->e[e].from > at) --g->e[e].from;
      if (g->e[e].to > at) --g->e[e].to;
    }
  }
}

static int flow_statement(Parse* p, Span st) {
  Graph* g = p->g;
  NodeSet groups[16];
  int start_flags[16];
  Link links[16];
  int ng = 0, nl = 0, k;
  if (first_word_is(&st, "classDef") || first_word_is(&st, "class") || first_word_is(&st, "style") || first_word_is(&st, "linkStyle") ||
      first_word_is(&st, "click") || first_word_is(&st, "accTitle") || first_word_is(&st, "accDescr") || first_word_is(&st, "title"))
    return 1;
  if (first_word_is(&st, "direction")) {
    Span w = {st.p + 9, st.n - 9};
    trim(&w);
    if (p->sub >= 0) { const int d = dir_from_word(w); if (d >= 0) g->s[p->sub].dir = d + 1; }
    return 1;
  }
  if (first_word_is(&st, "subgraph")) {
    Span rest = {st.p + 8, st.n - 8};
    Span id, title;
    size_t br = 0;
    int at;
    trim(&rest);
    id = rest;
    title = rest;
    while (br < rest.n && rest.p[br] != '[') ++br;
    if (br < rest.n && rest.p[rest.n - 1] == ']') {
      id.n = br;
      title.p = rest.p + br + 1;
      title.n = rest.n - br - 2;
      trim(&id);
    } else if (rest.n && rest.p[0] == '"') {
      /* subgraph "Some title": the title is the id, spaces and all */
      id = rest;
    }
    if (id.n == 0) { reason_set(p->reason, "a subgraph needs a name"); return 0; }
    if (g->ns >= 60) { reason_set(p->reason, "a flowchart with more than 60 subgraphs is not drawn"); return 0; }
    {
      Span idc = id;
      trim(&idc);
      at = graph_sub_find(g, idc);
      if (at < 0) {
        RolltuiStr clean;
        memset(&clean, 0, sizeof clean);
        g->s = (GSub*)rolltui_grow(g->s, &g->scap, g->ns + 1, sizeof *g->s);
        memset(&g->s[g->ns], 0, sizeof g->s[g->ns]);
        rolltui_str_set(&g->s[g->ns].id, idc.p, idc.n);
        clean_label(title, &clean);
        rolltui_str_set(&g->s[g->ns].title, clean.p ? clean.p : "", clean.n);
        rolltui_str_free(&clean);
        g->s[g->ns].parent = p->sub;
        at = (int)g->ns++;
      }
    }
    p->sub = at;
    return 1;
  }
  if (first_word_is(&st, "end")) {
    if (p->sub >= 0) p->sub = g->s[p->sub].parent;
    return 1;
  }
  /* a chain of groups and the links between them */
  if (!parse_group(p, &st, &groups[0], &start_flags[0])) {
    char msg[160];
    if (p->reason && p->reason->n) return 0;
    snprintf(msg, sizeof msg, "a line of a flowchart is not understood: %.*s", (int)imin((int)st.n, 60), st.p);
    reason_set(p->reason, msg);
    return 0;
  }
  ng = 1;
  for (;;) {
    skip_ws(&st);
    if (st.n == 0) break;
    if (nl >= 15 || ng >= 16) break;
    if (!parse_link(&st, &links[nl])) {
      char msg[160];
      snprintf(msg, sizeof msg, "a line of a flowchart is not understood: %.*s", (int)imin((int)st.n, 60), st.p);
      reason_set(p->reason, msg);
      return 0;
    }
    if (!parse_group(p, &st, &groups[ng], &start_flags[ng])) {
      if (!(p->reason && p->reason->n)) reason_set(p->reason, "a link needs a node at each end");
      return 0;
    }
    ++nl;
    ++ng;
  }
  for (k = 0; k < nl; ++k) {
    int a, b;
    for (a = 0; a < groups[k].n; ++a)
      for (b = 0; b < groups[k + 1].n; ++b) {
        int from = groups[k].v[a], to = groups[k + 1].v[b];
        if (from == -2 || to == -2) { reason_set(p->reason, "[*] is a state diagram's"); return 0; }
        if (from <= -100) from = sub_representative(g, -100 - from, 0);
        if (to <= -100) to = sub_representative(g, -100 - to, 1);
        if (from < 0 || to < 0) continue; /* a subgraph with nothing in it has nowhere to draw an edge to */
        graph_edge(g, from, to, links[k].style, links[k].head_from, links[k].head_to, links[k].minlen,
                   links[k].has_label ? links[k].label : (Span){NULL, 0});
      }
  }
  return 1;
}

static int flow_parse(Graph* g, const Source* src, size_t first, Span head, RolltuiStr* reason) {
  Parse p;
  size_t i;
  Span rest = {head.p, head.n};
  memset(&p, 0, sizeof p);
  p.g = g;
  p.sub = -1;
  p.reason = reason;
  /* `flowchart TD` / `graph LR` and whatever follows it on the line */
  {
    size_t kn = 0;
    while (kn < rest.n && is_word_char((unsigned char)rest.p[kn])) ++kn;
    rest.p += kn;
    rest.n -= kn;
    trim(&rest);
    if (rest.n) {
      const int d = dir_from_word(rest);
      if (d >= 0) {
        g->dir = d;
        rest.p += 2;
        rest.n -= 2;
        trim(&rest);
        if (rest.n && rest.p[0] == ';') { ++rest.p; --rest.n; trim(&rest); }
      }
    }
  }
  if (rest.n) {
    Span sts[32];
    size_t ns, k;
    trim_semicolon_split(rest, sts, &ns, 32);
    for (k = 0; k < ns; ++k)
      if (!flow_statement(&p, sts[k])) return 0;
  }
  for (i = first; i < src->n; ++i) {
    Span sts[32];
    size_t ns, k;
    trim_semicolon_split(src->lines[i], sts, &ns, 32);
    for (k = 0; k < ns; ++k)
      if (!flow_statement(&p, sts[k])) return 0;
  }
  graph_resolve_phantoms(g);
  return 1;
}

/* ---- the size and the drawing of a node ---------------------------------------------------------- */

static const RolltuiStr* node_text(const GNode* n) { return n->label.n ? &n->label : &n->id; }

static void node_measure(RolltuiMermaid* m, GNode* n) {
  int lines = 1, iw = label_extent(m, node_text(n), &lines);
  switch (n->shape) {
    case SH_START:
    case SH_END: n->w = 1; n->h = 1; return;
    case SH_FORK: n->w = 9; n->h = 1; return;
    case SH_RECORD: {
      int dl = 0, dw = n->detail.n ? label_extent(m, &n->detail, &dl) : 0;
      iw = imax(iw, dw);
      n->w = iw + 4;
      n->h = lines + (n->detail.n ? dl + 1 : 0) + 2;
      return;
    }
    case SH_SUBROUTINE: n->w = iw + 6; n->h = lines + 2; return;
    case SH_CYLINDER: n->w = iw + 4; n->h = lines + 3; return;
    case SH_CIRCLE:
    case SH_DIAMOND:
    case SH_HEXAGON:
    case SH_DOUBLECIRCLE:
    case SH_PARA:
    case SH_PARA_ALT:
    case SH_TRAP:
    case SH_TRAP_ALT: n->w = iw + 6; n->h = lines + 2; return;
    default: n->w = iw + 4; n->h = lines + 2; return;
  }
}

typedef struct Box {
  const char *tl, *tr, *bl, *br, *hz, *vt, *left, *right;
} Box;

static void node_glyphs(const RolltuiMermaid* m, int shape, Box* b) {
  if (m->ascii) {
    b->tl = b->tr = b->bl = b->br = "+";
    b->hz = "-";
    b->vt = "|";
    b->left = b->right = NULL;
    switch (shape) {
      case SH_STADIUM: case SH_CIRCLE: b->left = "("; b->right = ")"; break;
      case SH_DIAMOND: b->tl = b->br = "/"; b->tr = b->bl = "\\"; b->left = "<"; b->right = ">"; break;
      case SH_HEXAGON: b->tl = b->br = "/"; b->tr = b->bl = "\\"; break;
      case SH_FLAG: b->left = ">"; break;
      case SH_PARA: b->left = b->right = "/"; break;
      case SH_PARA_ALT: b->left = b->right = "\\"; break;
      case SH_TRAP: b->left = "/"; b->right = "\\"; break;
      case SH_TRAP_ALT: b->left = "\\"; b->right = "/"; break;
      case SH_DOUBLECIRCLE: b->hz = "="; b->vt = "H"; break;
      default: break;
    }
    return;
  }
  b->tl = "\xE2\x94\x8C"; b->tr = "\xE2\x94\x90"; b->bl = "\xE2\x94\x94"; b->br = "\xE2\x94\x98";
  b->hz = "\xE2\x94\x80"; b->vt = "\xE2\x94\x82";
  b->left = b->right = NULL;
  switch (shape) {
    case SH_ROUND: case SH_CYLINDER: b->tl = "\xE2\x95\xAD"; b->tr = "\xE2\x95\xAE"; b->bl = "\xE2\x95\xB0"; b->br = "\xE2\x95\xAF"; break;
    case SH_STADIUM: case SH_CIRCLE:
      b->tl = "\xE2\x95\xAD"; b->tr = "\xE2\x95\xAE"; b->bl = "\xE2\x95\xB0"; b->br = "\xE2\x95\xAF";
      b->left = "("; b->right = ")";
      break;
    case SH_DIAMOND: b->tl = b->br = "\xE2\x95\xB1"; b->tr = b->bl = "\xE2\x95\xB2"; b->left = "<"; b->right = ">"; break;
    case SH_HEXAGON: b->tl = b->br = "\xE2\x95\xB1"; b->tr = b->bl = "\xE2\x95\xB2"; break;
    case SH_FLAG: b->left = ">"; break;
    case SH_PARA: b->left = b->right = "/"; break;
    case SH_PARA_ALT: b->left = b->right = "\\"; break;
    case SH_TRAP: b->left = "/"; b->right = "\\"; break;
    case SH_TRAP_ALT: b->left = "\\"; b->right = "/"; break;
    case SH_DOUBLECIRCLE: b->tl = "\xE2\x95\x94"; b->tr = "\xE2\x95\x97"; b->bl = "\xE2\x95\x9A"; b->br = "\xE2\x95\x9D"; b->hz = "\xE2\x95\x90"; b->vt = "\xE2\x95\x91"; break;
    default: break;
  }
}

/* A record's lines: a line that is the single byte 0x01 is a rule, not text. */
static int is_rule_line(const char* s, size_t n) { return n == 1 && (unsigned char)s[0] == 1; }

/* The node's shape and its text, at its real position. */
static void node_draw(RolltuiMermaid* m, const GNode* n) {
  const int x = n->x, y = n->y, w = n->w, h = n->h;
  Box b;
  int r, c, lines, text_top, inner_x, inner_w;
  const RolltuiStr* lab = node_text(n);
  if (n->shape == SH_START) { put_str(m, x, y, m->ascii ? "*" : "\xE2\x97\x8F", ROLLTUI_MERMAID_CLASS_NODE); return; }
  if (n->shape == SH_END) { put_str(m, x, y, m->ascii ? "@" : "\xE2\x97\x89", ROLLTUI_MERMAID_CLASS_NODE); return; }
  if (n->shape == SH_FORK) {
    if (h > w) for (r = 0; r < h; ++r) put_str(m, x, y + r, m->ascii ? "H" : "\xE2\x94\x83", ROLLTUI_MERMAID_CLASS_NODE);
    else for (c = 0; c < w; ++c) put_str(m, x + c, y, m->ascii ? "=" : "\xE2\x94\x81", ROLLTUI_MERMAID_CLASS_NODE);
    return;
  }
  node_glyphs(m, n->shape, &b);
  label_extent(m, lab, &lines);
  for (r = 0; r < h; ++r) {
    for (c = 0; c < w; ++c) {
      const int top = r == 0, bottom = r == h - 1, left = c == 0, right = c == w - 1;
      const char* g = " ";
      if (top && left) g = b.tl;
      else if (top && right) g = b.tr;
      else if (bottom && left) g = b.bl;
      else if (bottom && right) g = b.br;
      else if (top || bottom) g = b.hz;
      else if (left) g = b.left && (h == 3 || n->shape == SH_STADIUM || n->shape == SH_CIRCLE) ? b.left : b.vt;
      else if (right) g = b.right && (h == 3 || n->shape == SH_STADIUM || n->shape == SH_CIRCLE) ? b.right : b.vt;
      put_glyph(&m->g, x + c, y + r, g, ROLLTUI_MERMAID_CLASS_NODE);
    }
  }
  if (n->shape == SH_SUBROUTINE) {
    for (r = 1; r < h - 1; ++r) {
      put_glyph(&m->g, x + 1, y + r, b.vt, ROLLTUI_MERMAID_CLASS_NODE);
      put_glyph(&m->g, x + w - 2, y + r, b.vt, ROLLTUI_MERMAID_CLASS_NODE);
    }
  }
  if (n->shape == SH_CYLINDER && h >= 4) {
    put_glyph(&m->g, x, y + 1, m->ascii ? "+" : "\xE2\x94\x9C", ROLLTUI_MERMAID_CLASS_NODE);
    put_glyph(&m->g, x + w - 1, y + 1, m->ascii ? "+" : "\xE2\x94\xA4", ROLLTUI_MERMAID_CLASS_NODE);
    for (c = 1; c < w - 1; ++c) put_glyph(&m->g, x + c, y + 1, b.hz, ROLLTUI_MERMAID_CLASS_NODE);
  }
  {
    const int base_h = lines + 2 + (n->shape == SH_CYLINDER ? 1 : 0) + (n->shape == SH_RECORD && n->detail.n ? 1 : 0);
    int dl = 0;
    if (n->shape == SH_RECORD && n->detail.n) label_extent(m, &n->detail, &dl);
    text_top = y + 1 + (n->shape == SH_CYLINDER ? 1 : 0) + (n->h > base_h + dl ? (n->h - base_h - dl) / 2 : 0);
  }
  inner_x = x + (n->shape == SH_SUBROUTINE || n->shape == SH_CIRCLE || n->shape == SH_DIAMOND || n->shape == SH_HEXAGON ||
                 n->shape == SH_DOUBLECIRCLE || n->shape == SH_PARA || n->shape == SH_PARA_ALT || n->shape == SH_TRAP ||
                 n->shape == SH_TRAP_ALT ? 3 : 2);
  inner_w = w - (inner_x - x) * 2;
  put_label_centered(m, inner_x, text_top, inner_w, lab, n->shape == SH_RECORD ? ROLLTUI_MERMAID_CLASS_TITLE : ROLLTUI_MERMAID_CLASS_TEXT);
  if (n->shape == SH_RECORD && n->detail.n) {
    /* a rule under the title, then the lines under it, left-aligned */
    size_t i = 0;
    int row = text_top + lines;
    put_glyph(&m->g, x, row, m->ascii ? "+" : "\xE2\x94\x9C", ROLLTUI_MERMAID_CLASS_NODE);
    put_glyph(&m->g, x + w - 1, row, m->ascii ? "+" : "\xE2\x94\xA4", ROLLTUI_MERMAID_CLASS_NODE);
    for (c = 1; c < w - 1; ++c) put_glyph(&m->g, x + c, row, b.hz, ROLLTUI_MERMAID_CLASS_NODE);
    ++row;
    while (i <= n->detail.n) {
      size_t e = i;
      while (e < n->detail.n && n->detail.p[e] != '\n') ++e;
      if (is_rule_line(n->detail.p + i, e - i)) {
        put_glyph(&m->g, x, row, m->ascii ? "+" : "\xE2\x94\x9C", ROLLTUI_MERMAID_CLASS_NODE);
        put_glyph(&m->g, x + w - 1, row, m->ascii ? "+" : "\xE2\x94\xA4", ROLLTUI_MERMAID_CLASS_NODE);
        for (c = 1; c < w - 1; ++c) put_glyph(&m->g, x + c, row, b.hz, ROLLTUI_MERMAID_CLASS_NODE);
        ++row;
      } else {
        put_text(m, x + 2, row++, n->detail.p + i, e - i, ROLLTUI_MERMAID_CLASS_TEXT);
      }
      i = e + 1;
    }
  }
}

/* ---- layout: layers, order, and where everything goes ----------------------------------------------- */

#define NODE_GAP_TD 3
#define NODE_GAP_LR 1
#define FRAME_MARGIN 2

typedef struct Lay {
  Graph* g;
  int lr;             /* the flow runs along x */
  int compact;
  size_t nl;          /* layers */
  int* lcount;        /* nodes in each */
  int** lnodes;       /* …in order */
  int* up_off;        /* CSR neighbours: above and below, by node */
  int* up;
  int* dn_off;
  int* dn;
  int* thick;         /* each layer's extent along the flow */
  int* lstart;        /* where each layer starts along the flow */
  int* gap;           /* the extent of the gap after each layer */
  int* xbefore;       /* rows a frame opening at each layer takes above it */
  int* xafter;        /* …and one closing there takes below it, before the gap */
  int cross_extent, main_extent;
  int ox, oy;         /* where the drawing starts in the grid: room around it for labels that sit outside the lines */
} Lay;

static void lay_release(Lay* L) {
  size_t i;
  for (i = 0; i < L->nl; ++i) rolltui_mem_free(L->lnodes[i]);
  rolltui_mem_free(L->lnodes);
  rolltui_mem_free(L->lcount);
  rolltui_mem_free(L->up_off);
  rolltui_mem_free(L->up);
  rolltui_mem_free(L->dn_off);
  rolltui_mem_free(L->dn);
  rolltui_mem_free(L->thick);
  rolltui_mem_free(L->lstart);
  rolltui_mem_free(L->gap);
  rolltui_mem_free(L->xbefore);
  rolltui_mem_free(L->xafter);
  memset(L, 0, sizeof *L);
}

/* the size a node claims across the flow (its shape, and a self-loop's room beside it) */
static int cs_res(const Lay* L, const GNode* n) { return n->cs + (L->lr ? 0 : n->halo); }
static int ms_res(const Lay* L, const GNode* n) { return n->ms + (L->lr ? n->halo : 0); }

/* the blank cells kept between two neighbours across the flow */
static int min_sep(const Lay* L, const GNode* a, const GNode* b) {
  if (a->dummy || b->dummy) return 1;
  return L->lr ? NODE_GAP_LR : (L->compact ? 2 : NODE_GAP_TD);
}

/* ---- subgraph chains ------------------------------------------------------------------------------ */

static int sub_depth(const Graph* g, int s) {
  int d = 0;
  while (s >= 0) { ++d; s = g->s[s].parent; }
  return d;
}

/* the subgraph of `node` at nesting `depth` (0 is the outermost), or -1 */
static int sub_at(const Graph* g, int node, int depth) {
  int s = g->n[node].sub, d = sub_depth(g, s);
  if (depth >= d) return -1;
  while (d - 1 > depth) { s = g->s[s].parent; --d; }
  return s;
}

static int in_sub(const Graph* g, int node, int sub) {
  int s = g->n[node].sub;
  while (s >= 0) {
    if (s == sub) return 1;
    s = g->s[s].parent;
  }
  return 0;
}

static int sub_lca(const Graph* g, int sa, int sb) {
  int da = sub_depth(g, sa), db = sub_depth(g, sb);
  while (da > db) { sa = g->s[sa].parent; --da; }
  while (db > da) { sb = g->s[sb].parent; --db; }
  while (sa != sb) {
    sa = sa >= 0 ? g->s[sa].parent : -1;
    sb = sb >= 0 ? g->s[sb].parent : -1;
  }
  return sa;
}

/* ---- layering ------------------------------------------------------------------------------------- */

static int assign_layers(Graph* g, int* layer) {
  const size_t n = g->nn, ne = g->ne;
  int* state = (int*)rolltui_mem_alloc((n + 1) * sizeof *state);
  int* indeg = (int*)rolltui_mem_alloc((n + 1) * sizeof *indeg);
  int* outdeg = (int*)rolltui_mem_alloc((n + 1) * sizeof *outdeg);
  int* queue = (int*)rolltui_mem_alloc((n + 1) * sizeof *queue);
  int* order = (int*)rolltui_mem_alloc((n + 1) * sizeof *order);
  int* stack = (int*)rolltui_mem_alloc((n + 1) * sizeof *stack);
  int* it = (int*)rolltui_mem_alloc((n + 1) * sizeof *it);
  size_t i, head = 0, tail = 0, cnt = 0;
  int ok = 1;
  memset(state, 0, (n + 1) * sizeof *state);
  memset(indeg, 0, (n + 1) * sizeof *indeg);
  memset(outdeg, 0, (n + 1) * sizeof *outdeg);
  for (i = 0; i < ne; ++i) g->e[i].rev = 0;
  /* CYCLES: edges are taken in the order they were written, and one that would close a loop — its head already leads
   * back to its tail through the edges taken so far — is the one that points back up the flow. What a reader wrote
   * first is the way the picture runs. */
  {
    size_t e;
    for (e = 0; e < ne; ++e) {
      const GEdge* ed = &g->e[e];
      size_t sp = 0, k;
      int found = 0;
      if (ed->from == ed->to) continue;
      memset(state, 0, (n + 1) * sizeof *state);
      stack[sp++] = ed->to;
      state[ed->to] = 1;
      while (sp > 0 && !found) {
        const int u = stack[--sp];
        if (u == ed->from) { found = 1; break; }
        for (k = 0; k < e; ++k) {
          const GEdge* f = &g->e[k];
          if (f->from == f->to || f->rev || f->from != u || state[f->to]) continue;
          state[f->to] = 1;
          stack[sp++] = f->to;
        }
      }
      g->e[e].rev = found;
    }
    (void)it;
  }
  for (i = 0; i < ne; ++i) {
    const GEdge* ed = &g->e[i];
    int u, v;
    if (ed->from == ed->to) continue;
    u = ed->rev ? ed->to : ed->from;
    v = ed->rev ? ed->from : ed->to;
    ++indeg[v];
    ++outdeg[u];
  }
  /* longest path */
  for (i = 0; i < n; ++i) { layer[i] = 0; if (indeg[i] == 0) queue[tail++] = (int)i; }
  {
    int* deg = (int*)rolltui_mem_alloc((n + 1) * sizeof *deg);
    memcpy(deg, indeg, (n + 1) * sizeof *deg);
    while (head < tail) {
      const int u = queue[head++];
      size_t e;
      order[cnt++] = u;
      for (e = 0; e < ne; ++e) {
        const GEdge* ed = &g->e[e];
        int a, b;
        if (ed->from == ed->to) continue;
        a = ed->rev ? ed->to : ed->from;
        b = ed->rev ? ed->from : ed->to;
        if (a != u) continue;
        if (layer[b] < layer[u] + ed->minlen) layer[b] = layer[u] + ed->minlen;
        if (--deg[b] == 0) queue[tail++] = b;
      }
    }
    rolltui_mem_free(deg);
  }
  if (cnt != n) ok = 0;
  /* a node with nothing above it belongs as near what it leads to as the flow allows */
  for (i = cnt; i-- > 0;) {
    const int u = order[i];
    int best = INT_MAX;
    size_t e;
    if (indeg[u] != 0 || outdeg[u] == 0) continue;
    for (e = 0; e < ne; ++e) {
      const GEdge* ed = &g->e[e];
      int a, b;
      if (ed->from == ed->to) continue;
      a = ed->rev ? ed->to : ed->from;
      b = ed->rev ? ed->from : ed->to;
      if (a == u && layer[b] - ed->minlen < best) best = layer[b] - ed->minlen;
    }
    if (best != INT_MAX && best > layer[u]) layer[u] = best;
  }
  rolltui_mem_free(state);
  rolltui_mem_free(indeg);
  rolltui_mem_free(outdeg);
  rolltui_mem_free(queue);
  rolltui_mem_free(order);
  rolltui_mem_free(stack);
  rolltui_mem_free(it);
  return ok;
}

/* ---- ordering within layers ------------------------------------------------------------------------- */

typedef struct Item {
  double key;
  size_t pos;
  int cluster;
  int* members;
  size_t mn, mcap;
  int single;
} Item;

static int item_cmp(const void* a, const void* b) {
  const Item* x = (const Item*)a;
  const Item* y = (const Item*)b;
  if (x->key < y->key) return -1;
  if (x->key > y->key) return 1;
  return x->pos < y->pos ? -1 : x->pos > y->pos ? 1 : 0;
}

/* `ids` (with `keys`) put in order: by key, but the members of one subgraph kept together at every depth of
 * nesting, the subgraph as a whole taking the mean of its members' keys. */
static void order_group(const Graph* g, int* ids, const double* keys_by_node, size_t n, int depth, int* out, size_t* out_n) {
  Item* items = (Item*)rolltui_mem_alloc((n + 1) * sizeof *items);
  size_t ni = 0, i, k;
  memset(items, 0, (n + 1) * sizeof *items);
  for (i = 0; i < n; ++i) {
    const int c = sub_at(g, ids[i], depth);
    size_t at = ni;
    if (c >= 0) {
      for (k = 0; k < ni; ++k)
        if (items[k].cluster == c && !items[k].single) { at = k; break; }
    }
    if (at == ni) {
      items[ni].cluster = c;
      items[ni].single = c < 0;
      items[ni].pos = i;
      items[ni].key = 0;
      ++ni;
    }
    items[at].members = (int*)rolltui_grow(items[at].members, &items[at].mcap, items[at].mn + 1, sizeof *items[at].members);
    items[at].members[items[at].mn++] = ids[i];
    items[at].key += keys_by_node[ids[i]];
  }
  for (k = 0; k < ni; ++k) items[k].key /= (double)items[k].mn;
  qsort(items, ni, sizeof *items, item_cmp);
  for (k = 0; k < ni; ++k) {
    if (items[k].single) out[(*out_n)++] = items[k].members[0];
    else order_group(g, items[k].members, keys_by_node, items[k].mn, depth + 1, out, out_n);
  }
  for (k = 0; k < ni; ++k) rolltui_mem_free(items[k].members);
  rolltui_mem_free(items);
}

static void set_orders(Lay* L, size_t l) {
  size_t i;
  for (i = 0; i < (size_t)L->lcount[l]; ++i) L->g->n[L->lnodes[l][i]].order = (int)i;
}

static void sweep_layer(Lay* L, size_t l, int use_up, double* keys) {
  Graph* g = L->g;
  const int cnt = L->lcount[l];
  int i;
  int* out;
  size_t out_n = 0;
  for (i = 0; i < cnt; ++i) {
    const int v = L->lnodes[l][i];
    const int* nb = use_up ? L->up + L->up_off[v] : L->dn + L->dn_off[v];
    const int nn = use_up ? L->up_off[v + 1] - L->up_off[v] : L->dn_off[v + 1] - L->dn_off[v];
    if (nn == 0) keys[v] = (double)i;
    else {
      double sum = 0;
      int k;
      for (k = 0; k < nn; ++k) sum += g->n[nb[k]].order;
      keys[v] = sum / nn;
    }
  }
  out = (int*)rolltui_mem_alloc(((size_t)cnt + 1) * sizeof *out);
  order_group(g, L->lnodes[l], keys, (size_t)cnt, 0, out, &out_n);
  memcpy(L->lnodes[l], out, out_n * sizeof *out);
  rolltui_mem_free(out);
  set_orders(L, l);
}

static long crossings(const Lay* L) {
  const Graph* g = L->g;
  long total = 0;
  size_t l;
  for (l = 0; l + 1 < L->nl; ++l) {
    size_t a, b;
    for (a = 0; a < (size_t)L->lcount[l]; ++a) {
      const int u1 = L->lnodes[l][a];
      int k1;
      for (k1 = L->dn_off[u1]; k1 < L->dn_off[u1 + 1]; ++k1) {
        const int v1 = L->dn[k1];
        for (b = a + 1; b < (size_t)L->lcount[l]; ++b) {
          const int u2 = L->lnodes[l][b];
          int k2;
          for (k2 = L->dn_off[u2]; k2 < L->dn_off[u2 + 1]; ++k2)
            if (g->n[L->dn[k2]].order < g->n[v1].order) ++total;
        }
      }
    }
  }
  return total;
}

/* Crossings between the edges of two neighbours in a layer, if `u` is left of `v`. */
static long pair_crossings(const Lay* L, int u, int v) {
  const Graph* g = L->g;
  long c = 0;
  int a, b;
  for (a = L->up_off[u]; a < L->up_off[u + 1]; ++a)
    for (b = L->up_off[v]; b < L->up_off[v + 1]; ++b)
      if (g->n[L->up[a]].order > g->n[L->up[b]].order) ++c;
  for (a = L->dn_off[u]; a < L->dn_off[u + 1]; ++a)
    for (b = L->dn_off[v]; b < L->dn_off[v + 1]; ++b)
      if (g->n[L->dn[a]].order > g->n[L->dn[b]].order) ++c;
  return c;
}

static long pair_crossings_swapped(const Lay* L, int u, int v) {
  const Graph* g = L->g;
  long c = 0;
  int a, b;
  for (a = L->up_off[u]; a < L->up_off[u + 1]; ++a)
    for (b = L->up_off[v]; b < L->up_off[v + 1]; ++b)
      if (g->n[L->up[a]].order < g->n[L->up[b]].order) ++c;
  for (a = L->dn_off[u]; a < L->dn_off[u + 1]; ++a)
    for (b = L->dn_off[v]; b < L->dn_off[v + 1]; ++b)
      if (g->n[L->dn[a]].order < g->n[L->dn[b]].order) ++c;
  return c;
}

/* Two neighbours in a layer are swapped when that leaves fewer crossings — within one subgraph only, so the
 * subgraphs stay whole. */
static void transpose_layers(Lay* L) {
  Graph* g = L->g;
  int improved = 1, rounds = 0;
  while (improved && rounds++ < 12) {
    size_t l;
    improved = 0;
    for (l = 0; l < L->nl; ++l) {
      int i;
      for (i = 0; i + 1 < L->lcount[l]; ++i) {
        const int u = L->lnodes[l][i], v = L->lnodes[l][i + 1];
        if (g->n[u].sub != g->n[v].sub) continue;
        if (pair_crossings_swapped(L, u, v) < pair_crossings(L, u, v)) {
          L->lnodes[l][i] = v;
          L->lnodes[l][i + 1] = u;
          g->n[v].order = i;
          g->n[u].order = i + 1;
          improved = 1;
        }
      }
    }
  }
}

/* ---- isotonic placement -------------------------------------------------------------------------------- */

/* Positions `c` (centres) as near `target` as an order and a minimum distance between neighbours allow: the
 * squared distance minimised exactly by pooling adjacent violators. */
static void pav_place(size_t n, const double* target, const double* dist, double* c) {
  double* off = (double*)rolltui_mem_alloc((n + 1) * sizeof *off);
  double* mean = (double*)rolltui_mem_alloc((n + 1) * sizeof *mean);
  int* weight = (int*)rolltui_mem_alloc((n + 1) * sizeof *weight);
  size_t* start = (size_t*)rolltui_mem_alloc((n + 1) * sizeof *start);
  size_t nb = 0, i, b;
  off[0] = 0;
  for (i = 1; i < n; ++i) off[i] = off[i - 1] + dist[i - 1];
  for (i = 0; i < n; ++i) {
    mean[nb] = target[i] - off[i];
    weight[nb] = 1;
    start[nb] = i;
    ++nb;
    while (nb > 1 && mean[nb - 2] > mean[nb - 1]) {
      mean[nb - 2] = (mean[nb - 2] * weight[nb - 2] + mean[nb - 1] * weight[nb - 1]) / (weight[nb - 2] + weight[nb - 1]);
      weight[nb - 2] += weight[nb - 1];
      --nb;
    }
  }
  for (b = 0; b < nb; ++b) {
    const size_t end = b + 1 < nb ? start[b + 1] : n;
    for (i = start[b]; i < end; ++i) c[i] = mean[b] + off[i];
  }
  rolltui_mem_free(off);
  rolltui_mem_free(mean);
  rolltui_mem_free(weight);
  rolltui_mem_free(start);
}

/* one layer placed towards `target` (per node, or its current place where the target is unset) */
static void place_layer(Lay* L, size_t l, const double* target_by_node, const int* has_target) {
  Graph* g = L->g;
  const size_t n = (size_t)L->lcount[l];
  double* t = (double*)rolltui_mem_alloc((n + 1) * sizeof *t);
  double* d = (double*)rolltui_mem_alloc((n + 1) * sizeof *d);
  double* c = (double*)rolltui_mem_alloc((n + 1) * sizeof *c);
  size_t i;
  for (i = 0; i < n; ++i) {
    const GNode* v = &g->n[L->lnodes[l][i]];
    t[i] = has_target[L->lnodes[l][i]] ? target_by_node[L->lnodes[l][i]] : (double)v->cpos + cs_res(L, v) / 2.0;
    if (i + 1 < n) {
      const GNode* w = &g->n[L->lnodes[l][i + 1]];
      d[i] = (cs_res(L, v) + cs_res(L, w)) / 2.0 + min_sep(L, v, w);
    }
  }
  pav_place(n, t, d, c);
  for (i = 0; i < n; ++i) {
    GNode* v = &g->n[L->lnodes[l][i]];
    v->cpos = (int)floor(c[i] - cs_res(L, v) / 2.0 + 0.5);
  }
  /* rounding may have closed a gap by a cell: put it back, left to right */
  for (i = 1; i < n; ++i) {
    const GNode* a = &g->n[L->lnodes[l][i - 1]];
    GNode* b = &g->n[L->lnodes[l][i]];
    const int need = a->cpos + cs_res(L, a) + min_sep(L, a, b);
    if (b->cpos < need) b->cpos = need;
  }
  rolltui_mem_free(t);
  rolltui_mem_free(d);
  rolltui_mem_free(c);
}

/* ---- the layout, start to finish ------------------------------------------------------------------- */

#define MAX_TOTAL_NODES 700

typedef struct Seg {
  int edge;
  int idx;       /* which step of the edge's chain */
  int a, b;      /* the nodes it joins: a above (earlier) b */
  int ca, cb;    /* the cross coordinates it leaves and arrives at */
  int lo, hi;
  int row;       /* its track: -1 for a straight run */
} Seg;

typedef struct Gap {
  Seg* seg;
  size_t n, cap;
  int rows;
} Gap;

/* Where step `idx` of an edge's chain is at, across the flow: a stand-in has one column; a real node's ports were
 * given out by `assign_ports`. */
static int endpoint_cross(const Lay* L, const GEdge* e, size_t idx) {
  const GNode* v = &L->g->n[e->chain[idx]];
  if (v->dummy) return v->cpos;
  if (idx == 0) return e->pa;
  if (idx + 1 == e->chain_n) return e->pb;
  return v->cpos + v->cs / 2;
}

/* what an edge is at one end of it: an arrowhead of some kind or a bare line. Ends that are alike can share a cell
 * on a node's side — a fork, a fan-in — and ends that are not, cannot: a head and a line at one cell read as one thing. */
static int end_head_of(const GEdge* e, int side) {
  return side == 0 ? (e->rev ? e->head_to : e->head_from) : (e->rev ? e->head_from : e->head_to);
}

static int end_key(const GEdge* e, size_t edge_index, int side) {
  return end_head_of(e, side) * 4096 + (e->parallel_n > 1 ? 1 + (int)edge_index : 0);
}

typedef struct PortReq {
  int edge;
  int key;
  int neighbour;
} PortReq;

static int port_cmp(const void* a, const void* b) {
  const PortReq* x = (const PortReq*)a;
  const PortReq* y = (const PortReq*)b;
  if (x->key != y->key) return x->key < y->key ? -1 : 1;
  return x->edge < y->edge ? -1 : x->edge > y->edge ? 1 : 0;
}

typedef struct PortKey {
  int key;
  double mean;
  int n;
  int at;
  int lw;         /* the widest label among the edges sharing it */
  int first_edge; /* the smallest edge index among those sharing it: who is written first stands to the left, at both ends */
} PortKey;

static int portkey_cmp(const void* a, const void* b) {
  const PortKey* x = (const PortKey*)a;
  const PortKey* y = (const PortKey*)b;
  if (x->mean != y->mean) return x->mean < y->mean ? -1 : 1;
  if (x->first_edge != y->first_edge) return x->first_edge < y->first_edge ? -1 : 1;
  return x->key < y->key ? -1 : x->key > y->key ? 1 : 0;
}

/* How many distinct ports each side of a node needs, before there is any layout: from the edges' orientation. */
static int ports_needed(const Graph* g, size_t v, int side) {
  int keys[64], nk = 0;
  size_t e;
  for (e = 0; e < g->ne; ++e) {
    const GEdge* ed = &g->e[e];
    const int start = ed->rev ? ed->to : ed->from, end = ed->rev ? ed->from : ed->to;
    int k, seen = 0;
    if (ed->style == ES_INVISIBLE || ed->from == ed->to) continue;
    if (!((side == 0 && start == (int)v) || (side == 1 && end == (int)v))) continue;
    k = end_key(ed, e, side);
    for (; seen < nk; ++seen) if (keys[seen] == k) break;
    if (seen == nk && nk < 64) keys[nk++] = k;
  }
  return nk;
}

/* ACROSS THE FLOW, a node that sends out several labelled edges is as wide as their labels need: each edge leaves by a
 * stub, its label lies beside the stub, and the next stub is a label further along. 0 when it needs nothing beyond
 * what it has. */
static int label_width_needed(const Graph* g, size_t v) {
  int keys[64], lws[64], nk = 0, labelled = 0, total = 2, q;
  size_t e;
  for (e = 0; e < g->ne; ++e) {
    const GEdge* ed = &g->e[e];
    const int start = ed->rev ? ed->to : ed->from;
    int k, seen = 0;
    if (ed->style == ES_INVISIBLE || ed->from == ed->to || start != (int)v) continue;
    k = end_key(ed, e, 0);
    for (; seen < nk; ++seen) if (keys[seen] == k) break;
    if (seen == nk) { if (nk >= 64) continue; keys[nk] = k; lws[nk] = 0; ++nk; }
    if (ed->label_w > lws[seen]) lws[seen] = ed->label_w;
  }
  for (q = 0; q < nk; ++q) { if (lws[q] > 0) ++labelled; total += lws[q] > 0 ? lws[q] + 3 : 2; }
  return labelled >= 2 ? total : 0;
}

/* Whether an edge may leave a node from `offset` cells along its side: not from its corners, and not from a record's
 * rules, which are already a junction. Only a vertical side (the flow runs along x) has rows to choose among. */
static int port_allowed(const RolltuiMermaid* m, const GNode* n, int offset, int lr) {
  (void)m;
  if (!lr || n->shape != SH_RECORD) return 1;
  {
    int title_lines = 1, k;
    const RolltuiStr* t = n->label.n ? &n->label : &n->id;
    size_t i;
    for (i = 0; i < t->n; ++i) if (t->p[i] == '\n') ++title_lines;
    if (offset <= 0 || offset >= n->h - 1) return 0;
    if (n->detail.n && offset == title_lines + 1) return 0;
    /* a rule inside the lines */
    k = title_lines + 2;
    i = 0;
    while (i <= n->detail.n) {
      size_t e = i;
      while (e < n->detail.n && n->detail.p[e] != '\n') ++e;
      if (is_rule_line(n->detail.p + i, e - i) && offset == k) return 0;
      ++k;
      i = e + 1;
    }
  }
  return 1;
}

/* THE PORTS. Every kind of end at a node's side gets a cell of its own along it, in the order that keeps them from
 * crossing — by where the other ends are — spread evenly; one takes the middle. */
static void assign_ports(Lay* L) {
  Graph* g = L->g;
  PortReq* reqs = (PortReq*)rolltui_mem_alloc((g->ne + 1) * sizeof *reqs);
  PortKey* keys = (PortKey*)rolltui_mem_alloc((g->ne + 1) * sizeof *keys);
  size_t v;
  for (v = 0; v < g->nn; ++v) {
    int side;
    const GNode* n = &g->n[v];
    if (n->dummy) continue;
    for (side = 0; side < 2; ++side) {
      size_t e, k = 0, i, nk = 0;
      int lo, hi;
      for (e = 0; e < g->ne; ++e) {
        const GEdge* ed = &g->e[e];
        if (ed->style == ES_INVISIBLE || ed->chain_n < 2) continue;
        if ((side == 0 && ed->chain[0] == (int)v) || (side == 1 && ed->chain[ed->chain_n - 1] == (int)v)) {
          const GNode* nb = &g->n[ed->chain[side == 0 ? 1 : ed->chain_n - 2]];
          reqs[k].edge = (int)e;
          reqs[k].key = end_key(ed, e, side);
          reqs[k].neighbour = nb->cpos + nb->cs / 2;
          ++k;
        }
      }
      qsort(reqs, k, sizeof *reqs, port_cmp);
      for (i = 0; i < k; ++i) {
        if (nk == 0 || keys[nk - 1].key != reqs[i].key) { keys[nk].key = reqs[i].key; keys[nk].mean = 0; keys[nk].n = 0; keys[nk].lw = 0; keys[nk].first_edge = reqs[i].edge; ++nk; }
        if (g->e[reqs[i].edge].label_w > keys[nk - 1].lw) keys[nk - 1].lw = g->e[reqs[i].edge].label_w;
        keys[nk - 1].mean += reqs[i].neighbour;
        if (reqs[i].edge < keys[nk - 1].first_edge) keys[nk - 1].first_edge = reqs[i].edge;
        ++keys[nk - 1].n;
      }
      for (i = 0; i < nk; ++i) keys[i].mean /= keys[i].n;
      qsort(keys, nk, sizeof *keys, portkey_cmp);
      for (i = 0; i < nk; ++i) keys[i].at = (int)i;
      lo = n->cpos + 1;
      hi = n->cpos + n->cs - 2;
      if (hi < lo) lo = hi = n->cpos + n->cs / 2;
      {
        /* the cell each kind of end takes: spread along the side, and where a label will lie beside each stub, far enough
         * apart that it has room */
        int pos_of[64];
        size_t q;
        for (q = 0; q < nk && q < 64; ++q) {
          pos_of[q] = nk == 1 ? n->cpos + n->cs / 2 : lo + (int)(((2 * (size_t)q + 1) * (size_t)(hi - lo + 1)) / (2 * nk));
          if (pos_of[q] > hi) pos_of[q] = hi;
          if (pos_of[q] < lo) pos_of[q] = lo;
        }
        if (!L->lr && side == 0 && nk > 1 && nk <= 64) {
          int over;
          for (q = 1; q < nk; ++q) {
            const int want = pos_of[q - 1] + (keys[q - 1].lw > 0 ? keys[q - 1].lw + 3 : 2);
            if (pos_of[q] < want) pos_of[q] = want;
          }
          over = pos_of[nk - 1] - hi;
          if (over > 0) for (q = 0; q < nk; ++q) { pos_of[q] -= over; if (pos_of[q] < lo) pos_of[q] = lo; }
        }
        for (i = 0; i < k; ++i) {
          int at, idx = 0;
          for (q = 0; q < nk; ++q) if (keys[q].key == reqs[i].key) { idx = keys[q].at; break; }
          at = idx < 64 ? pos_of[idx] : n->cpos + n->cs / 2;
        {
          /* on a record's side, the nearest row that is not a rule */
          int d;
          for (d = 0; d <= n->cs && !port_allowed(NULL, n, at - n->cpos, L->lr); ++d) {
            if (at + d <= hi && port_allowed(NULL, n, at + d - n->cpos, L->lr)) { at += d; break; }
            if (at - d >= lo && port_allowed(NULL, n, at - d - n->cpos, L->lr)) { at -= d; break; }
          }
        }
        if (side == 0) { g->e[reqs[i].edge].pa = at; g->e[reqs[i].edge].pa_single = k == 1; }
        else { g->e[reqs[i].edge].pb = at; g->e[reqs[i].edge].pb_single = k == 1; }
        }
      }
    }
  }
  rolltui_mem_free(keys);
  rolltui_mem_free(reqs);
  /* An edge whose two ends are free to move meets at one column, so a line between two nodes that overlap is
   * straight, not a jog of a cell. A free end meeting a fixed one goes to its column if its node reaches. */
  {
    size_t e;
    for (e = 0; e < g->ne; ++e) {
      GEdge* ed = &g->e[e];
      const GNode *a, *b;
      int alo, ahi, blo, bhi, oa, ob;
      if (ed->style == ES_INVISIBLE || ed->chain_n < 2) continue;
      a = &g->n[ed->chain[0]];
      b = &g->n[ed->chain[ed->chain_n - 1]];
      alo = a->cpos + 1; ahi = a->cpos + a->cs - 2;
      blo = b->cpos + 1; bhi = b->cpos + b->cs - 2;
      if (ahi < alo) alo = ahi = a->cpos + a->cs / 2;
      if (bhi < blo) blo = bhi = b->cpos + b->cs / 2;
      /* the other end of a long edge is its first stand-in, one column */
      oa = ed->chain_n > 2 ? g->n[ed->chain[1]].cpos : ed->pb;
      ob = ed->chain_n > 2 ? g->n[ed->chain[ed->chain_n - 2]].cpos : ed->pa;
      if (ed->chain_n == 2 && ed->pa_single && ed->pb_single) {
        const int lo = imax(alo, blo), hi = imin(ahi, bhi);
        int c;
        for (c = (lo + hi) / 2; lo <= hi && c >= lo && c <= hi; ++c)
          if (port_allowed(NULL, a, c - a->cpos, L->lr) && port_allowed(NULL, b, c - b->cpos, L->lr)) { ed->pa = ed->pb = c; break; }
      } else if (ed->chain_n == 2 && ed->pb_single && !ed->pa_single) {
        if (ed->pa >= blo && ed->pa <= bhi && port_allowed(NULL, b, ed->pa - b->cpos, L->lr)) ed->pb = ed->pa;
      } else if (ed->chain_n == 2 && ed->pa_single && !ed->pb_single) {
        if (ed->pb >= alo && ed->pb <= ahi && port_allowed(NULL, a, ed->pb - a->cpos, L->lr)) ed->pa = ed->pb;
      } else if (ed->chain_n > 2) {
        if (ed->pa_single && oa >= alo && oa <= ahi && port_allowed(NULL, a, oa - a->cpos, L->lr)) ed->pa = oa;
        if (ed->pb_single && ob >= blo && ob <= bhi && port_allowed(NULL, b, ob - b->cpos, L->lr)) ed->pb = ob;
      }
    }
  }
}

static int seg_conflict(const Seg* x, const Seg* y) {
  if (x->row == -2 || y->row == -2) return 0;
  if (x->lo == x->hi || y->lo == y->hi) return 0;
  {
    const int same_src = x->a == y->a, same_dst = x->b == y->b;
    if (x->lo < y->hi && y->lo < x->hi) {
      /* overlapping: allowed only where they share an end, which merges them into one trunk */
      if (same_src || same_dst) return 0;
      return 1;
    }
    if (x->lo == y->hi || y->lo == x->hi) {
      const int touch = x->lo == y->hi ? x->lo : x->hi;
      (void)touch;
      return !(same_src || same_dst);
    }
  }
  return 0;
}

/* how many times X's lines would cross Y's if X's track is above Y's */
static int seg_cross_cost(const Seg* x, const Seg* y) {
  int cost = 0;
  if (x->cb > y->lo && x->cb < y->hi && y->lo != y->hi) ++cost; /* X's descent through Y's track */
  if (y->ca > x->lo && y->ca < x->hi && x->lo != x->hi) ++cost; /* Y's climb through X's track */
  return cost;
}

static void assign_tracks(Gap* gap) {
  size_t i, k, placed = 0;
  int* order = (int*)rolltui_mem_alloc((gap->n + 1) * sizeof *order);
  int nrows = 0;
  /* the segments needing a horizontal run, the long ones first */
  for (i = 0; i < gap->n; ++i) { gap->seg[i].row = gap->seg[i].lo == gap->seg[i].hi ? -1 : -2; }
  for (i = 0; i < gap->n; ++i) if (gap->seg[i].row == -2) order[placed++] = (int)i;
  for (i = 1; i < placed; ++i) {
    const int cur = order[i];
    size_t j = i;
    while (j > 0 && (gap->seg[order[j - 1]].hi - gap->seg[order[j - 1]].lo) < (gap->seg[cur].hi - gap->seg[cur].lo)) { order[j] = order[j - 1]; --j; }
    order[j] = cur;
  }
  for (k = 0; k < placed; ++k) {
    Seg* s = &gap->seg[order[k]];
    int best_pos = -1, best_cost = INT_MAX, best_join = 0, pos;
    /* candidates: join row `pos` (when nothing in it conflicts), or open a new row before it (pos == nrows: at the end) */
    for (pos = 0; pos <= nrows; ++pos) {
      int join_ok = pos < nrows, cost_join = 0, cost_new = 0;
      size_t j;
      for (j = 0; j < gap->n; ++j) {
        const Seg* t = &gap->seg[j];
        if (t == s || t->row < 0) continue;
        if (t->row == pos && pos < nrows) { if (seg_conflict(s, t)) join_ok = 0; continue; }
        /* rows at or above `pos` when opening a new row before it are above the new row */
        {
          const int t_above_new = t->row < pos;
          const int t_above_join = t->row < pos;
          cost_new += t_above_new ? seg_cross_cost(t, s) : seg_cross_cost(s, t);
          cost_join += t_above_join ? seg_cross_cost(t, s) : seg_cross_cost(s, t);
        }
      }
      if (join_ok && cost_join < best_cost) { best_cost = cost_join; best_pos = pos; best_join = 1; }
      if (cost_new < best_cost || (cost_new == best_cost && best_pos < 0)) { best_cost = cost_new; best_pos = pos; best_join = 0; }
    }
    if (best_pos < 0) best_pos = nrows;
    if (!best_join) {
      /* open a row at best_pos: everything at or below it moves down */
      for (i = 0; i < gap->n; ++i)
        if (gap->seg[i].row >= best_pos) ++gap->seg[i].row;
      ++nrows;
    }
    s->row = best_pos;
  }
  gap->rows = nrows;
  rolltui_mem_free(order);
}

static void to_real(const Lay* L, int dir, int main, int cross, int* x, int* y) {
  switch (dir) {
    case DIR_BT: *x = cross; *y = L->main_extent - 1 - main; break;
    case DIR_LR: *x = main; *y = cross; break;
    case DIR_RL: *x = L->main_extent - 1 - main; *y = cross; break;
    default: *x = cross; *y = main; break;
  }
  *x += L->ox;
  *y += L->oy;
}

static int layout_graph(RolltuiMermaid* m, Graph* g, Lay* L, int compact, Gap** gaps_out, RolltuiStr* reason) {
  const size_t nreal = g->nn;
  size_t i, l;
  int* layer;
  double* keys;
  double* targets;
  int* has_t;
  int ok = 0;
  memset(L, 0, sizeof *L);
  L->g = g;
  L->lr = g->dir == DIR_LR || g->dir == DIR_RL;
  L->compact = compact;
  *gaps_out = NULL;
  if (nreal == 0) { reason_set(reason, "the diagram has no nodes"); return 0; }
  for (i = 0; i < g->ns; ++i) g->s[i].tw = g->s[i].title.n ? text_width(m, g->s[i].title.p, g->s[i].title.n) : 0;
  for (i = 0; i < g->ne; ++i) g->e[i].label_w = g->e[i].label.n ? label_extent(m, &g->e[i].label, NULL) : 0;
  for (i = 0; i < nreal; ++i) g->n[i].halo = 0;
  for (i = 0; i < g->ne; ++i)
    if (g->e[i].from == g->e[i].to) g->n[g->e[i].from].halo = imax(g->n[g->e[i].from].halo, 3 + (g->e[i].label.n ? label_extent(m, &g->e[i].label, NULL) + 3 : 0));
  for (i = 0; i < nreal; ++i) {
    GNode* n = &g->n[i];
    node_measure(m, n);
    if (n->shape == SH_FORK && L->lr) { n->w = 1; n->h = 7; }
    n->cs = L->lr ? n->h : n->w;
    n->ms = L->lr ? n->w : n->h;
  }
  layer = (int*)rolltui_mem_alloc((nreal + 1) * sizeof *layer);
  if (!assign_layers(g, layer)) { rolltui_mem_free(layer); reason_set(reason, "the diagram's edges could not be layered"); return 0; }
  for (i = 0; i < nreal; ++i) g->n[i].layer = layer[i];
  rolltui_mem_free(layer);
  /* parallel edges: several between one pair of nodes are told apart by where they attach */
  for (i = 0; i < g->ne; ++i) {
    size_t j;
    int n = 0, at = 0;
    for (j = 0; j < g->ne; ++j) {
      const GEdge* f = &g->e[j];
      if ((f->from == g->e[i].from && f->to == g->e[i].to) || (f->from == g->e[i].to && f->to == g->e[i].from)) {
        if (j < i) ++at;
        ++n;
      }
    }
    g->e[i].parallel = at;
    g->e[i].parallel_n = n;
  }
  /* a node grows to hold its ports: two kinds of end on one side want two cells that are not the corners */
  for (i = 0; i < nreal; ++i) {
    GNode* n = &g->n[i];
    const int need = imax(ports_needed(g, i, 0), ports_needed(g, i, 1));
    if (need >= 2 && n->shape != SH_START && n->shape != SH_END && n->shape != SH_FORK) {
      const int want = 2 * need + 1;
      if (L->lr) { if (n->h < want) { n->h = want; n->cs = want; } }
      else if (n->w < want) { n->w = want; n->cs = want; }
    }
    if (!L->lr && n->shape != SH_START && n->shape != SH_END && n->shape != SH_FORK) {
      const int want = label_width_needed(g, i);
      if (want > n->w) { n->w = want; n->cs = want; }
    }
  }
  /* each edge's chain, with a stand-in in every layer it passes through */
  for (i = 0; i < g->ne; ++i) {
    GEdge* e = &g->e[i];
    int u, v, span, k;
    rolltui_mem_free(e->chain);
    e->chain = NULL;
    e->chain_n = 0;
    if (e->from == e->to) continue;
    u = e->rev ? e->to : e->from;
    v = e->rev ? e->from : e->to;
    span = g->n[v].layer - g->n[u].layer;
    if (span < 1) span = 1;
    e->chain = (int*)rolltui_mem_alloc(((size_t)span + 1) * sizeof *e->chain);
    e->chain[0] = u;
    for (k = 1; k < span; ++k) {
      GNode d;
      if (g->nn >= MAX_TOTAL_NODES) { reason_set(reason, "the diagram is too large to draw"); return 0; }
      memset(&d, 0, sizeof d);
      d.dummy = 1;
      d.layer = g->n[u].layer + k;
      d.sub = sub_lca(g, g->n[u].sub, g->n[v].sub);
      d.cs = 1;
      d.w = d.h = 1;
      g->n = (GNode*)rolltui_grow(g->n, &g->ncap, g->nn + 1, sizeof *g->n);
      g->n[g->nn] = d;
      e->chain[k] = (int)g->nn++;
    }
    e->chain[span] = v;
    e->chain_n = (size_t)span + 1;
  }
  /* the layers */
  {
    size_t nl = 0;
    for (i = 0; i < g->nn; ++i) if ((size_t)g->n[i].layer + 1 > nl) nl = (size_t)g->n[i].layer + 1;
    L->nl = nl;
    L->lcount = (int*)rolltui_mem_alloc((nl + 1) * sizeof *L->lcount);
    L->lnodes = (int**)rolltui_mem_alloc((nl + 1) * sizeof *L->lnodes);
    memset(L->lcount, 0, (nl + 1) * sizeof *L->lcount);
    for (i = 0; i < g->nn; ++i) ++L->lcount[g->n[i].layer];
    for (l = 0; l < nl; ++l) { L->lnodes[l] = (int*)rolltui_mem_alloc(((size_t)L->lcount[l] + 1) * sizeof **L->lnodes); L->lcount[l] = 0; }
    for (i = 0; i < g->nn; ++i) L->lnodes[g->n[i].layer][L->lcount[g->n[i].layer]++] = (int)i;
    for (l = 0; l < nl; ++l) set_orders(L, l);
  }
  /* neighbours above and below */
  {
    int* uc = (int*)rolltui_mem_alloc((g->nn + 2) * sizeof *uc);
    int* dc = (int*)rolltui_mem_alloc((g->nn + 2) * sizeof *dc);
    size_t e, k;
    memset(uc, 0, (g->nn + 2) * sizeof *uc);
    memset(dc, 0, (g->nn + 2) * sizeof *dc);
    for (e = 0; e < g->ne; ++e)
      for (k = 0; k + 1 < g->e[e].chain_n; ++k) { ++dc[g->e[e].chain[k]]; ++uc[g->e[e].chain[k + 1]]; }
    L->up_off = (int*)rolltui_mem_alloc((g->nn + 2) * sizeof *L->up_off);
    L->dn_off = (int*)rolltui_mem_alloc((g->nn + 2) * sizeof *L->dn_off);
    L->up_off[0] = L->dn_off[0] = 0;
    for (i = 0; i < g->nn; ++i) { L->up_off[i + 1] = L->up_off[i] + uc[i]; L->dn_off[i + 1] = L->dn_off[i] + dc[i]; }
    L->up = (int*)rolltui_mem_alloc(((size_t)L->up_off[g->nn] + 1) * sizeof *L->up);
    L->dn = (int*)rolltui_mem_alloc(((size_t)L->dn_off[g->nn] + 1) * sizeof *L->dn);
    memset(uc, 0, (g->nn + 2) * sizeof *uc);
    memset(dc, 0, (g->nn + 2) * sizeof *dc);
    for (e = 0; e < g->ne; ++e)
      for (k = 0; k + 1 < g->e[e].chain_n; ++k) {
        const int a = g->e[e].chain[k], b = g->e[e].chain[k + 1];
        L->dn[L->dn_off[a] + dc[a]++] = b;
        L->up[L->up_off[b] + uc[b]++] = a;
      }
    rolltui_mem_free(uc);
    rolltui_mem_free(dc);
  }
  keys = (double*)rolltui_mem_alloc((g->nn + 1) * sizeof *keys);
  targets = (double*)rolltui_mem_alloc((g->nn + 1) * sizeof *targets);
  has_t = (int*)rolltui_mem_alloc((g->nn + 1) * sizeof *has_t);
  /* ---- ordering: sweep down and up, keeping the arrangement with the fewest crossings */
  {
    int* best = (int*)rolltui_mem_alloc((g->nn + 1) * sizeof *best);
    long best_c = -1;
    int iter;
    for (iter = 0; iter < 16; ++iter) {
      long c;
      for (l = 1; l < L->nl; ++l) sweep_layer(L, l, 1, keys);
      for (l = L->nl; l-- > 1;) sweep_layer(L, l - 1, 0, keys);
      c = crossings(L);
      if (best_c < 0 || c < best_c) {
        best_c = c;
        for (i = 0; i < g->nn; ++i) best[i] = g->n[i].order;
      }
      if (c == 0) break;
    }
    for (i = 0; i < g->nn; ++i) g->n[i].order = best[i];
    for (l = 0; l < L->nl; ++l) {
      int a, b;
      for (a = 0; a < L->lcount[l]; ++a)
        for (b = a + 1; b < L->lcount[l]; ++b)
          if (g->n[L->lnodes[l][b]].order < g->n[L->lnodes[l][a]].order) { const int t = L->lnodes[l][a]; L->lnodes[l][a] = L->lnodes[l][b]; L->lnodes[l][b] = t; }
      set_orders(L, l);
    }
    transpose_layers(L);
    rolltui_mem_free(best);
  }
  /* ---- across the flow: packed, then pulled towards what each node is joined to */
  for (l = 0; l < L->nl; ++l) {
    int pos = 0, a;
    for (a = 0; a < L->lcount[l]; ++a) {
      GNode* v = &g->n[L->lnodes[l][a]];
      v->cpos = pos;
      pos += cs_res(L, v) + (a + 1 < L->lcount[l] ? min_sep(L, v, &g->n[L->lnodes[l][a + 1]]) : 0);
    }
  }
  {
    int iter;
    for (iter = 0; iter < 10; ++iter) {
      const int both = iter >= 6;
      int pass;
      for (pass = 0; pass < 2; ++pass) {
        size_t step;
        for (step = 0; step + 1 < L->nl + (L->nl == 1 ? 1 : 0); ++step) {
          const size_t lay = pass == 0 ? step + 1 : L->nl - 2 - step;
          int a;
          if (L->nl < 2) break;
          for (a = 0; a < L->lcount[lay]; ++a) {
            const int v = L->lnodes[lay][a];
            double sum = 0;
            int cnt = 0, k;
            if (pass == 0 || both) for (k = L->up_off[v]; k < L->up_off[v + 1]; ++k) { const GNode* u = &g->n[L->up[k]]; sum += u->cpos + u->cs / 2.0; ++cnt; }
            if (pass == 1 || both) for (k = L->dn_off[v]; k < L->dn_off[v + 1]; ++k) { const GNode* u = &g->n[L->dn[k]]; sum += u->cpos + u->cs / 2.0; ++cnt; }
            has_t[v] = cnt > 0;
            targets[v] = cnt > 0 ? sum / cnt + (cs_res(L, &g->n[v]) - g->n[v].cs) / 2.0 : 0;
          }
          place_layer(L, lay, targets, has_t);
        }
      }
    }
  }
  ok = 1;
  rolltui_mem_free(keys);
  rolltui_mem_free(targets);
  rolltui_mem_free(has_t);
  return ok;
}

/* ---- subgraphs: the room a frame takes across the flow ------------------------------------------------ */

typedef struct Band {
  int lo, hi;     /* across the flow, frame margin included */
  int lmin, lmax; /* the layers it spans */
  int set;
} Band;

static void compute_bands(Lay* L, Band* bands) {
  Graph* g = L->g;
  size_t s, i;
  /* deepest first, so a frame's band already holds its children's */
  int depth, maxd = 0;
  for (s = 0; s < g->ns; ++s) { bands[s].set = 0; maxd = imax(maxd, sub_depth(g, (int)s)); }
  for (depth = maxd; depth >= 1; --depth) {
    for (s = 0; s < g->ns; ++s) {
      if (sub_depth(g, (int)s) != depth) continue;
      for (i = 0; i < g->nn; ++i) {
        const GNode* v = &g->n[i];
        if (g->n[i].sub != (int)s) continue; /* deeper members arrive through their own frame */
        if (!bands[s].set) { bands[s].lo = v->cpos; bands[s].hi = v->cpos + cs_res(L, v) - 1; bands[s].lmin = bands[s].lmax = v->layer; bands[s].set = 1; }
        else {
          bands[s].lo = imin(bands[s].lo, v->cpos);
          bands[s].hi = imax(bands[s].hi, v->cpos + cs_res(L, v) - 1);
          bands[s].lmin = imin(bands[s].lmin, v->layer);
          bands[s].lmax = imax(bands[s].lmax, v->layer);
        }
      }
      {
        size_t c;
        for (c = 0; c < g->ns; ++c) {
          if (g->s[c].parent != (int)s || !bands[c].set) continue;
          if (!bands[s].set) bands[s] = bands[c];
          else {
            bands[s].lo = imin(bands[s].lo, bands[c].lo);
            bands[s].hi = imax(bands[s].hi, bands[c].hi);
            bands[s].lmin = imin(bands[s].lmin, bands[c].lmin);
            bands[s].lmax = imax(bands[s].lmax, bands[c].lmax);
          }
        }
      }
      if (bands[s].set) {
        bands[s].lo -= FRAME_MARGIN;
        bands[s].hi += FRAME_MARGIN;
        /* a frame is at least as wide as its title, where the title runs across the flow */
        if (!L->lr && bands[s].hi - bands[s].lo + 1 < g->s[s].tw + 6) bands[s].hi = bands[s].lo + g->s[s].tw + 5;
      }
    }
  }
}

/* how far the frames a node is in reach beyond it, not counting any that also hold `sub`: to be kept out of a frame
 * is to keep one's own frames out of it too */
static int frame_reach(const Graph* g, int node, int sub) {
  int reach = 0, t = g->n[node].sub;
  while (t >= 0) {
    int s = sub, holds = 0;
    while (s >= 0) { if (s == t) { holds = 1; break; } s = g->s[s].parent; }
    if (holds) break;
    reach += FRAME_MARGIN;
    t = g->s[t].parent;
  }
  return reach;
}

/* Every node that is not in a subgraph is kept out of its frame, pushed to whichever side it is on, and whatever
 * is beside it in its layer moves out with it. The frames only ever move things apart, so this ends. */
static void enforce_clusters(Lay* L) {
  Graph* g = L->g;
  Band* bands;
  int iter;
  if (g->ns == 0) return;
  bands = (Band*)rolltui_mem_alloc((g->ns + 1) * sizeof *bands);
  for (iter = 0; iter < 60; ++iter) {
    int changed = 0;
    size_t s;
    compute_bands(L, bands);
    for (s = 0; s < g->ns && !changed; ++s) {
      size_t l;
      if (!bands[s].set) continue;
      for (l = (size_t)bands[s].lmin; l <= (size_t)bands[s].lmax && !changed; ++l) {
        int a, first_member = INT_MAX;
        for (a = 0; a < L->lcount[l]; ++a) if (in_sub(g, L->lnodes[l][a], (int)s)) { first_member = a; break; }
        for (a = 0; a < L->lcount[l]; ++a) {
          GNode* v = &g->n[L->lnodes[l][a]];
          const int reach = frame_reach(g, L->lnodes[l][a], (int)s);
          const int vlo = v->cpos - reach, vhi = v->cpos + cs_res(L, v) - 1 + reach;
          int left, k;
          if (in_sub(g, L->lnodes[l][a], (int)s)) continue;
          if (vhi < bands[s].lo - 1 || vlo > bands[s].hi + 1) continue;
          left = first_member != INT_MAX ? a < first_member : (vlo + vhi) / 2 < (bands[s].lo + bands[s].hi) / 2;
          if (left) {
            const int shift = vhi - (bands[s].lo - 2);
            v->cpos -= shift;
            for (k = a - 1; k >= 0; --k) {
              GNode* w = &g->n[L->lnodes[l][k]];
              const GNode* nx = &g->n[L->lnodes[l][k + 1]];
              const int limit = nx->cpos - min_sep(L, w, nx) - cs_res(L, w);
              if (w->cpos > limit) w->cpos = limit;
            }
          } else {
            const int shift = (bands[s].hi + 2) - vlo;
            v->cpos += shift;
            for (k = a + 1; k < L->lcount[l]; ++k) {
              GNode* w = &g->n[L->lnodes[l][k]];
              const GNode* pv = &g->n[L->lnodes[l][k - 1]];
              const int limit = pv->cpos + cs_res(L, pv) + min_sep(L, pv, w);
              if (w->cpos < limit) w->cpos = limit;
            }
          }
          changed = 1;
          break;
        }
      }
    }
    if (!changed) break;
  }
  rolltui_mem_free(bands);
}

/* ---- along the flow: layers, their gaps, and the tracks in them ---------------------------------------- */

static int lay_out_main_axis(RolltuiMermaid* m, Lay* L, Gap** gaps_out) {
  Graph* g = L->g;
  Gap* gaps = (Gap*)rolltui_mem_alloc((L->nl + 1) * sizeof *gaps);
  int* extra_before = (int*)rolltui_mem_alloc((L->nl + 1) * sizeof *extra_before);
  int* extra_after = (int*)rolltui_mem_alloc((L->nl + 1) * sizeof *extra_after);
  size_t e, k, l, s;
  L->xbefore = extra_before;
  L->xafter = extra_after;
  int min_c = INT_MAX, pos;
  memset(gaps, 0, (L->nl + 1) * sizeof *gaps);
  memset(extra_before, 0, (L->nl + 1) * sizeof *extra_before);
  memset(extra_after, 0, (L->nl + 1) * sizeof *extra_after);
  /* the smallest cross coordinate becomes 0 (a frame's margin included) */
  {
    Band* bands = g->ns ? (Band*)rolltui_mem_alloc((g->ns + 1) * sizeof *bands) : NULL;
    size_t i;
    for (i = 0; i < g->nn; ++i) min_c = imin(min_c, g->n[i].cpos);
    if (bands) {
      compute_bands(L, bands);
      for (s = 0; s < g->ns; ++s) if (bands[s].set) {
        min_c = imin(min_c, bands[s].lo);
        extra_before[bands[s].lmin] += FRAME_MARGIN;
        extra_after[bands[s].lmax] += FRAME_MARGIN;
      }
      rolltui_mem_free(bands);
    }
    if (min_c == INT_MAX) min_c = 0;
    for (i = 0; i < g->nn; ++i) g->n[i].cpos -= min_c;
  }
  assign_ports(L);
  /* the segments of every gap, and their tracks */
  for (e = 0; e < g->ne; ++e) {
    const GEdge* ed = &g->e[e];
    if (ed->style == ES_INVISIBLE) continue;
    for (k = 0; k + 1 < ed->chain_n; ++k) {
      const int a = ed->chain[k], b = ed->chain[k + 1];
      const size_t lay = (size_t)g->n[a].layer;
      Gap* gp = &gaps[lay];
      Seg sg;
      memset(&sg, 0, sizeof sg);
      sg.edge = (int)e;
      sg.idx = (int)k;
      sg.a = a;
      sg.b = b;
      sg.ca = endpoint_cross(L, ed, k);
      sg.cb = endpoint_cross(L, ed, k + 1);
      sg.lo = imin(sg.ca, sg.cb);
      sg.hi = imax(sg.ca, sg.cb);
      gp->seg = (Seg*)rolltui_grow(gp->seg, &gp->cap, gp->n + 1, sizeof *gp->seg);
      gp->seg[gp->n++] = sg;
    }
  }
  L->thick = (int*)rolltui_mem_alloc((L->nl + 1) * sizeof *L->thick);
  L->lstart = (int*)rolltui_mem_alloc((L->nl + 1) * sizeof *L->lstart);
  L->gap = (int*)rolltui_mem_alloc((L->nl + 1) * sizeof *L->gap);
  for (l = 0; l < L->nl; ++l) {
    int a, t = 1;
    for (a = 0; a < L->lcount[l]; ++a) { const GNode* v = &g->n[L->lnodes[l][a]]; if (!v->dummy) t = imax(t, ms_res(L, v)); }
    L->thick[l] = t;
    L->gap[l] = 0;
    if (l + 1 < L->nl) {
      Gap* gp = &gaps[l];
      int rows, label_w = 0;
      assign_tracks(gp);
      rows = gp->rows;
      L->gap[l] = rows > 0 ? rows + 2 : (gp->n > 0 ? 2 : 1);
      /* along x the flow needs room for a label to lie on its edge; along y a label sits beside a vertical run */
      if (L->lr) {
        for (k = 0; k < gp->n; ++k) {
          const GEdge* ed = &g->e[gp->seg[k].edge];
          if (ed->label.n && (size_t)gp->seg[k].idx == (ed->chain_n - 2) / 2) label_w = imax(label_w, label_extent(m, &ed->label, NULL));
        }
        if (label_w > 0) L->gap[l] = imax(L->gap[l], label_w + 3);
      }
    }
  }
  pos = extra_before[0];
  for (l = 0; l < L->nl; ++l) {
    L->lstart[l] = pos;
    pos += L->thick[l];
    if (l + 1 < L->nl) pos += extra_after[l] + L->gap[l] + extra_before[l + 1];
  }
  pos += extra_after[L->nl - 1];
  L->main_extent = pos;
  for (l = 0; l < L->nl; ++l) {
    int a;
    for (a = 0; a < L->lcount[l]; ++a) {
      GNode* v = &g->n[L->lnodes[l][a]];
      if (v->dummy) { v->ms = L->thick[l]; v->mpos = L->lstart[l]; }
      else v->mpos = L->lstart[l] + (L->thick[l] - ms_res(L, v)) / 2;
    }
  }
  {
    size_t i;
    int c = 0;
    Band* bands = g->ns ? (Band*)rolltui_mem_alloc((g->ns + 1) * sizeof *bands) : NULL;
    for (i = 0; i < g->nn; ++i) c = imax(c, g->n[i].cpos + cs_res(L, &g->n[i]));
    if (bands) {
      compute_bands(L, bands);
      for (s = 0; s < g->ns; ++s) if (bands[s].set) c = imax(c, bands[s].hi + 1);
      rolltui_mem_free(bands);
    }
    L->cross_extent = c;
  }
  *gaps_out = gaps;
  return 1;
}

static void gaps_release(Lay* L, Gap* gaps) {
  size_t l;
  if (!gaps) return;
  for (l = 0; l < L->nl + 1; ++l) rolltui_mem_free(gaps[l].seg);
  rolltui_mem_free(gaps);
}

/* ---- drawing the graph ----------------------------------------------------------------------------- */

typedef struct Frame {
  int clo, chi, mlo, mhi, set;
} Frame;

static void compute_frames(Lay* L, Frame* fr) {
  Graph* g = L->g;
  size_t s, i, c;
  int depth, maxd = 0;
  for (s = 0; s < g->ns; ++s) { fr[s].set = 0; maxd = imax(maxd, sub_depth(g, (int)s)); }
  for (depth = maxd; depth >= 1; --depth) {
    for (s = 0; s < g->ns; ++s) {
      if (sub_depth(g, (int)s) != depth) continue;
      for (i = 0; i < g->nn; ++i) {
        const GNode* v = &g->n[i];
        if (v->sub != (int)s) continue;
        {
          const int clo = v->cpos, chi = v->cpos + cs_res(L, v) - 1, mlo = v->mpos, mhi = v->mpos + ms_res(L, v) - 1;
          if (!fr[s].set) { fr[s].clo = clo; fr[s].chi = chi; fr[s].mlo = mlo; fr[s].mhi = mhi; fr[s].set = 1; }
          else { fr[s].clo = imin(fr[s].clo, clo); fr[s].chi = imax(fr[s].chi, chi); fr[s].mlo = imin(fr[s].mlo, mlo); fr[s].mhi = imax(fr[s].mhi, mhi); }
        }
      }
      for (c = 0; c < g->ns; ++c) {
        if (g->s[c].parent != (int)s || !fr[c].set) continue;
        if (!fr[s].set) fr[s] = fr[c];
        else { fr[s].clo = imin(fr[s].clo, fr[c].clo); fr[s].chi = imax(fr[s].chi, fr[c].chi); fr[s].mlo = imin(fr[s].mlo, fr[c].mlo); fr[s].mhi = imax(fr[s].mhi, fr[c].mhi); }
      }
      if (fr[s].set) {
        fr[s].clo -= FRAME_MARGIN; fr[s].chi += FRAME_MARGIN; fr[s].mlo -= FRAME_MARGIN; fr[s].mhi += FRAME_MARGIN;
        if (!L->lr && fr[s].chi - fr[s].clo + 1 < g->s[s].tw + 6) fr[s].chi = fr[s].clo + g->s[s].tw + 5;
      }
    }
  }
}

static void place_real(const Lay* L, GNode* n) {
  int x0, y0, x1, y1;
  to_real(L, L->g->dir, n->mpos, n->cpos, &x0, &y0);
  to_real(L, L->g->dir, n->mpos + n->ms - 1, n->cpos + n->cs - 1, &x1, &y1);
  n->x = imin(x0, x1);
  n->y = imin(y0, y1);
}

static const char* head_glyph(const RolltuiMermaid* m, int head, int dx, int dy) {
  const int a = m->ascii;
  switch (head) {
    case HEAD_ARROW:
      if (dy > 0) return a ? "v" : "\xE2\x96\xBC";
      if (dy < 0) return a ? "^" : "\xE2\x96\xB2";
      if (dx > 0) return a ? ">" : "\xE2\x96\xB6";
      return a ? "<" : "\xE2\x97\x80";
    case HEAD_TRIANGLE:
      if (dy > 0) return a ? "v" : "\xE2\x96\xBD";
      if (dy < 0) return a ? "^" : "\xE2\x96\xB3";
      if (dx > 0) return a ? ">" : "\xE2\x96\xB7";
      return a ? "<" : "\xE2\x97\x81";
    case HEAD_CIRCLE: return a ? "o" : "\xE2\x97\x8B";
    case HEAD_CROSS: return a ? "x" : "\xE2\x9C\x95";
    case HEAD_DIAMOND: return a ? "*" : "\xE2\x97\x86";
    case HEAD_DIAMOND_OPEN: return a ? "o" : "\xE2\x97\x87";
    default: return NULL;
  }
}

/* a border cell where an edge joins: a tee in the direction it leaves */
static void join_border(RolltuiMermaid* m, int bx, int by, int dx, int dy) {
  Cell* c = cell_at(&m->g, bx, by);
  const char* g;
  if (!c || m->ascii) return;
  if (strcmp((const char*)c->g, "\xE2\x94\x80") != 0 && strcmp((const char*)c->g, "\xE2\x94\x82") != 0) return;
  if (dy > 0) g = "\xE2\x94\xAC";
  else if (dy < 0) g = "\xE2\x94\xB4";
  else if (dx > 0) g = "\xE2\x94\x9C";
  else g = "\xE2\x94\xA4";
  put_glyph(&m->g, bx, by, g, ROLLTUI_MERMAID_CLASS_NODE);
}

static int label_free(RolltuiMermaid* m, int x, int y, int lines, int lw) {
  int r, c;
  for (r = 0; r < lines; ++r)
    for (c = 0; c < lw; ++c)
      if (!cell_empty(&m->g, x + c, y + r)) return 0;
  return 1;
}

/* an edge's label: on a long straight run of the line where there is one, else beside the line where there is room */
static void place_edge_label(RolltuiMermaid* m, const int* px, const int* py, size_t np, const RolltuiStr* label, int lr) {
  int lines, lw, best_len = -1, best_at = -1;
  size_t i;
  lw = label_extent(m, label, &lines);
  if (lw == 0 || np < 2) return;
  /* a pure horizontal stretch to lie on: interior cells only, so a corner or a junction is never covered */
  for (i = 0; i + 1 < np; ++i) {
    if (py[i] == py[i + 1]) {
      const int x0 = imin(px[i], px[i + 1]), x1 = imax(px[i], px[i + 1]);
      const int len = x1 - x0 - 1;
      if (len >= lw + 2 && lines == 1 && len > best_len) {
        int ok = 1, x;
        for (x = x0 + 1; x < x1; ++x) {
          const Cell* c = cell_at(&m->g, x, py[i]);
          if (!c || c->g[0] != 0 || (c->mask & (M_U | M_D)) || !(c->mask & (M_L | M_R))) ok = 0;
        }
        if (ok) { best_len = len; best_at = (int)i; }
      }
    }
  }
  if (best_at >= 0) {
    const int x0 = imin(px[best_at], px[best_at + 1]), x1 = imax(px[best_at], px[best_at + 1]);
    const int start = x0 + 1 + (x1 - x0 - 1 - lw) / 2;
    put_label_centered(m, start, py[best_at], lw, label, ROLLTUI_MERMAID_CLASS_LABEL);
    return;
  }
  if (!lr && px[0] == px[1]) {
    /* the label of an edge lies beside the stub it leaves by, on the row next to the node it leaves: nothing else is
     * there but the stubs of its neighbours, which were given room for it */
    const int y0 = imin(py[0], py[1]), y1 = imax(py[0], py[1]);
    int margin, t;
    for (margin = 1; margin >= 0; --margin)
      for (t = 0; t <= y1 - y0 && t < 3; ++t) {
        const int yy = py[1] > py[0] ? py[0] + t : py[0] - t;
        if (label_free(m, px[0] + 2 - margin, yy, lines, lw + 2 * margin)) { put_label_centered(m, px[0] + 2, yy, lw, label, ROLLTUI_MERMAID_CLASS_LABEL); return; }
      }
  }
  {
    /* beside a run: vertical ones on their right, then left; horizontal ones above, then below. The middle of the
     * edge is where the label reads as the edge's, so runs are tried from there outward. */
    int order[64], on = 0, pass, margin;
    size_t k;
    const size_t runs = np - 1;
    double mid = 0, total = 0, acc = 0;
    for (k = 0; k < runs; ++k) total += abs(px[k + 1] - px[k]) + abs(py[k + 1] - py[k]);
    mid = total / 2;
    {
      int n2 = 0;
      double dist[64];
      for (k = 0; k < runs && n2 < 64; ++k) {
        const double len = abs(px[k + 1] - px[k]) + abs(py[k + 1] - py[k]);
        const double centre = acc + len / 2;
        int j = n2;
        acc += len;
        dist[n2] = fabs(centre - mid);
        order[n2] = (int)k;
        while (j > 0 && dist[j - 1] > dist[j]) {
          const double td = dist[j]; const int ti = order[j];
          dist[j] = dist[j - 1]; order[j] = order[j - 1]; dist[j - 1] = td; order[j - 1] = ti;
          --j;
        }
        ++n2;
      }
      on = n2;
    }
    for (margin = 1; margin >= 0; --margin) {
      for (pass = 0; pass < 2; ++pass) {
        int oi;
        for (oi = 0; oi < on; ++oi) {
          const int r = order[oi];
          const int vertical = px[r] == px[r + 1];
          const int y0 = imin(py[r], py[r + 1]), y1 = imax(py[r], py[r + 1]);
          const int x0 = imin(px[r], px[r + 1]), x1 = imax(px[r], px[r + 1]);
          int t;
          if (vertical && !lr) {
            for (t = 0; t <= (y1 - y0); ++t) {
              const int yy = y0 + (y1 - y0) / 2 + ((t & 1) ? (t + 1) / 2 : -(t / 2));
              int side;
              if (yy < y0 || yy > y1) continue;
              for (side = 0; side < 2; ++side) {
                const int sx = pass == 0 ? (side == 0 ? x0 + 2 : x0 - 1 - lw) : (side == 0 ? x0 - 1 - lw : x0 + 2);
                if (label_free(m, sx - margin, yy, lines, lw + 2 * margin)) { put_label_centered(m, sx, yy, lw, label, ROLLTUI_MERMAID_CLASS_LABEL); return; }
              }
            }
          } else if (!vertical) {
            for (t = 0; t <= (x1 - x0); ++t) {
              const int xx = x0 + 1 + t;
              int side;
              if (xx + lw > x1 + lw) continue;
              for (side = 0; side < 2; ++side) {
                const int yy = (pass == 0 ? (side == 0 ? py[r] - lines : py[r] + 1) : (side == 0 ? py[r] + 1 : py[r] - lines));
                if (label_free(m, xx - margin, yy, lines, lw + 2 * margin)) { put_label_centered(m, xx, yy, lw, label, ROLLTUI_MERMAID_CLASS_LABEL); return; }
              }
            }
          } else if (vertical && lr) {
            for (t = 0; t <= (y1 - y0); ++t) {
              const int yy = y0 + (y1 - y0) / 2 + ((t & 1) ? (t + 1) / 2 : -(t / 2));
              int side;
              if (yy < y0 || yy > y1) continue;
              for (side = 0; side < 2; ++side) {
                const int sx = side == 0 ? x0 + 2 : x0 - 1 - lw;
                if (label_free(m, sx - margin, yy, lines, lw + 2 * margin)) { put_label_centered(m, sx, yy, lw, label, ROLLTUI_MERMAID_CLASS_LABEL); return; }
              }
            }
          }
        }
      }
    }
  }
  /* no room beside it: on the line, over whatever is there */
  {
    int longest = 0;
    size_t at = 0;
    for (i = 0; i + 1 < np; ++i) {
      const int len = abs(px[i + 1] - px[i]) + abs(py[i + 1] - py[i]);
      if (len > longest) { longest = len; at = i; }
    }
    put_label_centered(m, (px[at] + px[at + 1]) / 2 - lw / 2, (py[at] + py[at + 1]) / 2, lw, label, ROLLTUI_MERMAID_CLASS_LABEL);
  }
}

/* Small text at one end of an edge — a cardinality — beside the line where it meets its node. */
static void place_end_label(RolltuiMermaid* m, const int* px, const int* py, size_t np, int at_end, const RolltuiStr* label) {
  const int i = at_end ? (int)np - 1 : 0, j = at_end ? (int)np - 2 : 1;
  const int x = px[i], y = py[i];
  const int lw = label_extent(m, label, NULL);
  const int vertical = px[i] == px[j];
  int t;
  if (lw == 0) return;
  if (vertical) {
    const int step = py[j] > y ? 1 : -1;
    for (t = 0; t < 3; ++t) {
      const int yy = y + step * t;
      if (label_free(m, x + 2, yy, 1, lw)) { put_text(m, x + 2, yy, label->p, label->n, ROLLTUI_MERMAID_CLASS_LABEL); return; }
      if (label_free(m, x - 1 - lw, yy, 1, lw)) { put_text(m, x - 1 - lw, yy, label->p, label->n, ROLLTUI_MERMAID_CLASS_LABEL); return; }
    }
  } else {
    const int step = px[j] > x ? 1 : -1;
    int margin;
    for (margin = 1; margin >= 0; --margin)
      for (t = 1; t < 5; ++t) {
        const int xx = x + step * t;
        const int left = step > 0 ? xx : xx - lw + 1;
        if (label_free(m, left - margin, y - 1, 1, lw + 2 * margin)) { put_text(m, left, y - 1, label->p, label->n, ROLLTUI_MERMAID_CLASS_LABEL); return; }
        if (label_free(m, left - margin, y + 1, 1, lw + 2 * margin)) { put_text(m, left, y + 1, label->p, label->n, ROLLTUI_MERMAID_CLASS_LABEL); return; }
      }
  }
}

static void draw_frames(RolltuiMermaid* m, Lay* L) {
  Graph* g = L->g;
  Frame* fr;
  size_t s;
  if (g->ns == 0) return;
  fr = (Frame*)rolltui_mem_alloc((g->ns + 1) * sizeof *fr);
  compute_frames(L, fr);
  for (s = 0; s < g->ns; ++s) {
    int x0, y0, x1, y1, xa, ya, xb, yb, x, y;
    if (!fr[s].set) continue;
    to_real(L, g->dir, fr[s].mlo, fr[s].clo, &xa, &ya);
    to_real(L, g->dir, fr[s].mhi, fr[s].chi, &xb, &yb);
    x0 = imin(xa, xb); x1 = imax(xa, xb); y0 = imin(ya, yb); y1 = imax(ya, yb);
    for (x = x0; x <= x1; ++x) {
      add_mask(&m->g, x, y0, (x > x0 ? M_L : 0) | (x < x1 ? M_R : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
      add_mask(&m->g, x, y1, (x > x0 ? M_L : 0) | (x < x1 ? M_R : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
    }
    for (y = y0; y <= y1; ++y) {
      add_mask(&m->g, x0, y, (y > y0 ? M_U : 0) | (y < y1 ? M_D : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
      add_mask(&m->g, x1, y, (y > y0 ? M_U : 0) | (y < y1 ? M_D : 0), ST_LIGHT, ROLLTUI_MERMAID_CLASS_BOX);
    }
    g->s[s].fl = x0; g->s[s].fr = x1; g->s[s].ft = y0; g->s[s].fb = y1;
  }
  rolltui_mem_free(fr);
}

static void draw_frame_titles(RolltuiMermaid* m, Graph* g) {
  size_t s;
  for (s = 0; s < g->ns; ++s) {
    const GSub* sb = &g->s[s];
    if (sb->fr <= sb->fl || sb->title.n == 0) continue;
    {
      RolltuiStr t;
      int room = sb->fr - sb->fl - 3;
      memset(&t, 0, sizeof t);
      rolltui_str_append(&t, " ", 1);
      rolltui_str_append_str(&t, &sb->title);
      rolltui_str_append(&t, " ", 1);
      if (room > 0) {
        const int tw = text_width(m, t.p, t.n);
        if (tw <= room) put_text(m, sb->fl + 2, sb->ft, t.p, t.n, ROLLTUI_MERMAID_CLASS_TITLE);
        else {
          const size_t keep = rolltui_u_fit(m->u, t.p, t.n, room - 1, m->ascii, NULL);
          put_text(m, sb->fl + 2, sb->ft, t.p, keep, ROLLTUI_MERMAID_CLASS_TITLE);
          put_str(m, sb->fl + 2 + text_width(m, t.p, keep), sb->ft, "\xE2\x80\xA6", ROLLTUI_MERMAID_CLASS_TITLE);
        }
      }
      rolltui_str_free(&t);
    }
  }
}

static void draw_edges(RolltuiMermaid* m, Lay* L, Gap* gaps) {
  Graph* g = L->g;
  size_t e;
  int* lp = NULL;
  size_t lp_cap = 0;
  for (e = 0; e < g->ne; ++e) {
    const GEdge* ed = &g->e[e];
    int* mp = NULL;  /* abstract points: main, cross */
    int* cp = NULL;
    size_t np = 0, cap_m = 0, cap_c = 0, k, i;
    int *px, *py, style;
    unsigned char cls = ROLLTUI_MERMAID_CLASS_EDGE;
    int start_head, end_head;
    if (ed->style == ES_INVISIBLE || ed->chain_n < 2) continue;
    for (k = 0; k + 1 < ed->chain_n; ++k) {
      const GNode* a = &g->n[ed->chain[k]];
      const GNode* b = &g->n[ed->chain[k + 1]];
      const Gap* gp = &gaps[a->layer];
      int ca = endpoint_cross(L, ed, k), cb = endpoint_cross(L, ed, k + 1), t = -1;
      const int exit_m = a->mpos + a->ms - 1, entry_m = b->mpos;
      size_t s;
      /* a track's row: the gap starts after any frame closing above it, its first row a straight stub, the tracks
       * one on from there */
      for (s = 0; s < gp->n; ++s)
        if (gp->seg[s].edge == (int)e && gp->seg[s].idx == (int)k) {
          if (gp->seg[s].row >= 0) t = L->lstart[a->layer] + L->thick[a->layer] + L->xafter[a->layer] + 1 + gp->seg[s].row;
          break;
        }
      mp = (int*)rolltui_grow(mp, &cap_m, np + 6, sizeof *mp);
      cp = (int*)rolltui_grow(cp, &cap_c, np + 6, sizeof *cp);
      if (np == 0 || mp[np - 1] != exit_m + 1 || cp[np - 1] != ca) { mp[np] = exit_m + 1; cp[np] = ca; ++np; }
      if (ca != cb && t >= 0) {
        if (mp[np - 1] != t || cp[np - 1] != ca) { mp[np] = t; cp[np] = ca; ++np; }
        mp[np] = t; cp[np] = cb; ++np;
      }
      if (mp[np - 1] != entry_m - 1 || cp[np - 1] != cb) { mp[np] = entry_m - 1; cp[np] = cb; ++np; }
    }
    if (np < 2) { rolltui_mem_free(mp); rolltui_mem_free(cp); continue; }
    lp = (int*)rolltui_grow(lp, &lp_cap, np * 2 + 2, sizeof *lp);
    px = lp;
    py = lp + np;
    for (i = 0; i < np; ++i) to_real(L, g->dir, mp[i], cp[i], &px[i], &py[i]);
    style = ed->style == ES_THICK ? ST_HEAVY : ed->style == ES_DOTTED ? ST_DASH : ST_LIGHT;
    for (i = 0; i + 1 < np; ++i) line_between(&m->g, px[i], py[i], px[i + 1], py[i + 1], style, cls);
    start_head = ed->rev ? ed->head_to : ed->head_from;
    end_head = ed->rev ? ed->head_from : ed->head_to;
    {
      const int sdx = px[1] > px[0] ? 1 : px[1] < px[0] ? -1 : 0, sdy = py[1] > py[0] ? 1 : py[1] < py[0] ? -1 : 0;
      const int edx = px[np - 1] > px[np - 2] ? 1 : px[np - 1] < px[np - 2] ? -1 : 0, edy = py[np - 1] > py[np - 2] ? 1 : py[np - 1] < py[np - 2] ? -1 : 0;
      const char* hs = head_glyph(m, start_head, -sdx, -sdy);
      const char* he = head_glyph(m, end_head, edx, edy);
      join_border(m, px[0] - sdx, py[0] - sdy, sdx, sdy);
      if (hs) put_glyph(&m->g, px[0], py[0], hs, ROLLTUI_MERMAID_CLASS_ARROW);
      if (he) put_glyph(&m->g, px[np - 1], py[np - 1], he, ROLLTUI_MERMAID_CLASS_ARROW);
      else join_border(m, px[np - 1] + edx, py[np - 1] + edy, -edx, -edy);
    }
    if (ed->label.n) place_edge_label(m, px, py, np, &ed->label, L->lr);
    if ((ed->rev ? ed->end_b : ed->end_a).n) place_end_label(m, px, py, np, 0, ed->rev ? &ed->end_b : &ed->end_a);
    if ((ed->rev ? ed->end_a : ed->end_b).n) place_end_label(m, px, py, np, 1, ed->rev ? &ed->end_a : &ed->end_b);
    rolltui_mem_free(mp);
    rolltui_mem_free(cp);
  }
  rolltui_mem_free(lp);
}

static void draw_self_loops(RolltuiMermaid* m, Graph* g, size_t nreal) {
  size_t e;
  for (e = 0; e < g->ne; ++e) {
    const GEdge* ed = &g->e[e];
    const GNode* n;
    int x, y, r1, r2, xr;
    if (ed->from != ed->to || (size_t)ed->from >= nreal) continue;
    n = &g->n[ed->from];
    x = n->x + n->w;
    y = n->y;
    r1 = y + 1;
    r2 = n->h > 3 ? y + n->h - 2 : y + n->h - 1;
    xr = x + 2;
    line_between(&m->g, x, r1, xr, r1, ST_LIGHT, ROLLTUI_MERMAID_CLASS_EDGE);
    line_between(&m->g, xr, r1, xr, r2, ST_LIGHT, ROLLTUI_MERMAID_CLASS_EDGE);
    line_between(&m->g, xr, r2, x + 1, r2, ST_LIGHT, ROLLTUI_MERMAID_CLASS_EDGE);
    join_border(m, x - 1, r1, 1, 0);
    put_glyph(&m->g, x, r2, m->ascii ? "<" : "\xE2\x97\x80", ROLLTUI_MERMAID_CLASS_ARROW);
    if (ed->label.n) put_label_centered(m, xr + 2, r1, label_extent(m, &ed->label, NULL), &ed->label, ROLLTUI_MERMAID_CLASS_LABEL);
  }
}

static void graph_strip(Graph* g, size_t nreal) {
  size_t i;
  for (i = 0; i < g->ne; ++i) { rolltui_mem_free(g->e[i].chain); g->e[i].chain = NULL; g->e[i].chain_n = 0; }
  for (i = nreal; i < g->nn; ++i) { rolltui_str_free(&g->n[i].id); rolltui_str_free(&g->n[i].label); rolltui_str_free(&g->n[i].detail); }
  g->nn = nreal;
}

static int draw_graph(RolltuiMermaid* m, Graph* g, int max_width, RolltuiStr* reason) {
  const size_t nreal = g->nn;
  int attempt, width = 0, ok = 0;
  Lay L;
  Gap* gaps = NULL;
  size_t i;
  memset(&L, 0, sizeof L);
  for (i = 0; i < g->ne; ++i) {
    /* an edge to a subgraph by its name stands for its first member */
    (void)i;
  }
  for (attempt = 0; attempt < 2; ++attempt) {
    if (attempt > 0) { gaps_release(&L, gaps); gaps = NULL; lay_release(&L); graph_strip(g, nreal); }
    if (!layout_graph(m, g, &L, attempt == 1, &gaps, reason)) goto done;
    enforce_clusters(&L);
    if (!lay_out_main_axis(m, &L, &gaps)) goto done;
    width = L.lr ? L.main_extent : L.cross_extent;
    if (width <= max_width) break;
  }
  if (width > max_width) { reason_setf(reason, "the diagram needs %d columns; this view has %d", width, max_width); goto done; }
  {
    /* room beyond the lines for what sits beside them: a label to the right of a vertical edge, one above the top
     * row of a horizontal one — the grid is trimmed to what is drawn when it is read out */
    int label_w = 0, gw, gh;
    for (i = 0; i < g->ne; ++i) if (g->e[i].label.n) label_w = imax(label_w, label_extent(m, &g->e[i].label, NULL) + 4);
    L.ox = 0;
    L.oy = L.lr ? 1 : 0;
    gw = L.lr ? width : imin(max_width, width + label_w);
    gh = (L.lr ? L.cross_extent : L.main_extent) + (L.lr ? 2 : 0);
    grid_init(&m->g, gw, gh);
  }
  for (i = 0; i < nreal; ++i) place_real(&L, &g->n[i]);
  draw_frames(m, &L);
  for (i = 0; i < nreal; ++i) node_draw(m, &g->n[i]);
  draw_self_loops(m, g, nreal);
  draw_edges(m, &L, gaps);
  draw_frame_titles(m, g);
  ok = 1;
done:
  gaps_release(&L, gaps);
  lay_release(&L);
  graph_strip(g, nreal);
  return ok;
}

/* ============================================================================================
 * STATE DIAGRAMS: a flowchart of states
 * ============================================================================================ */

static int state_scoped_node(Graph* g, int sub, int is_end) {
  char id[48];
  Span s;
  int at;
  const int n = snprintf(id, sizeof id, "[*]%s%d", is_end ? "end" : "start", sub);
  s.p = id;
  s.n = (size_t)n;
  at = graph_find(g, s);
  if (at >= 0) return at;
  at = graph_node(g, s, sub);
  if (at >= 0) g->n[at].shape = is_end ? SH_END : SH_START;
  return at;
}

/* a state named in a transition or a description: created as a plain state, or found */
static int state_named(Graph* g, Span name, int sub) {
  Span id = name;
  int at;
  trim(&id);
  if (id.n >= 2 && id.p[0] == '"' && id.p[id.n - 1] == '"') { ++id.p; id.n -= 2; }
  if (id.n == 0) return -1;
  at = graph_find(g, id);
  if (at >= 0) return at;
  /* a composite state's own name is the composite, whose edges go to its first member */
  if (graph_sub_find(g, id) >= 0) return -100 - graph_sub_find(g, id);
  at = graph_node(g, id, sub);
  if (at >= 0) g->n[at].shape = SH_ROUND;
  return at;
}

static size_t find_arrow(Span s) {
  size_t i;
  int quoted = 0;
  for (i = 0; i + 2 < s.n + 0; ++i) {
    if (s.p[i] == '"') quoted = !quoted;
    if (!quoted && s.p[i] == '-' && s.p[i + 1] == '-' && s.p[i + 2] == '>') return i;
  }
  return s.n;
}

static int state_parse(Graph* g, const Source* src, size_t first, Span head, RolltuiStr* reason) {
  size_t i;
  int sub = -1;
  int note_block = 0, note_node = -1;
  Span rest = {head.p, head.n};
  size_t kn = 0;
  g->state = 1;
  g->dir = DIR_TD;
  while (kn < rest.n && (is_word_char((unsigned char)rest.p[kn]) || rest.p[kn] == '-')) ++kn;
  (void)kn;
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    size_t arrow;
    if (note_block) {
      if (span_starts_ci(&l, "end note")) { note_block = 0; note_node = -1; continue; }
      if (note_node >= 0) {
        RolltuiStr* lab = &g->n[note_node].label;
        if (lab->n) rolltui_str_append(lab, "\n", 1);
        {
          RolltuiStr clean;
          memset(&clean, 0, sizeof clean);
          clean_label(l, &clean);
          rolltui_str_append_str(lab, &clean);
          rolltui_str_free(&clean);
        }
      }
      continue;
    }
    if (first_word_is(&l, "direction")) {
      Span w = {l.p + 9, l.n - 9};
      const int d = (trim(&w), dir_from_word(w));
      if (d >= 0) { if (sub >= 0) g->s[sub].dir = d + 1; else g->dir = d; }
      continue;
    }
    if (first_word_is(&l, "classDef") || first_word_is(&l, "class") || first_word_is(&l, "style") || first_word_is(&l, "scale") ||
        first_word_is(&l, "hide") || first_word_is(&l, "accTitle") || first_word_is(&l, "accDescr") || first_word_is(&l, "click") ||
        (l.n == 2 && memcmp(l.p, "--", 2) == 0))
      continue;
    if (l.n == 1 && l.p[0] == '}') { if (sub >= 0) sub = g->s[sub].parent; continue; }
    if (first_word_is(&l, "note")) {
      Span r = {l.p + 4, l.n - 4};
      size_t colon = 0;
      int target;
      Span who;
      trim(&r);
      while (colon < r.n && r.p[colon] != ':') ++colon;
      who.p = r.p;
      who.n = colon;
      trim(&who);
      if (span_starts_ci(&who, "right of ")) { who.p += 9; who.n -= 9; }
      else if (span_starts_ci(&who, "left of ")) { who.p += 8; who.n -= 8; }
      else if (span_starts_ci(&who, "over ")) { who.p += 5; who.n -= 5; }
      target = state_named(g, who, sub);
      if (target <= -100) target = sub_representative(g, -100 - target, 0);
      if (target < 0) continue;
      {
        Span idn;
        char nid[40];
        int at;
        const int n = snprintf(nid, sizeof nid, "[note]%zu", g->nn);
        idn.p = nid;
        idn.n = (size_t)n;
        at = graph_node(g, idn, sub);
        if (at < 0) continue;
        g->n[at].shape = SH_FLAG;
        if (colon < r.n) {
          Span t = {r.p + colon + 1, r.n - colon - 1};
          clean_label(t, &g->n[at].label);
        } else { note_block = 1; note_node = at; }
        graph_edge(g, target, at, ES_DOTTED, HEAD_NONE, HEAD_NONE, 1, (Span){NULL, 0});
      }
      continue;
    }
    if (first_word_is(&l, "state")) {
      Span r = {l.p + 5, l.n - 5};
      Span id, title = {NULL, 0};
      int has_title = 0, composite = 0, kind = 0;
      trim(&r);
      if (r.n && r.p[r.n - 1] == '{') { composite = 1; --r.n; trim(&r); }
      {
        size_t k = find_close(r, "<<");
        if (k < r.n) {
          Span tag = {r.p + k + 2, r.n - k - 2};
          size_t e = find_close(tag, ">>");
          tag.n = e;
          if (span_starts_ci(&tag, "fork") || span_starts_ci(&tag, "join")) kind = 1;
          else if (span_starts_ci(&tag, "choice")) kind = 2;
          r.n = k;
          trim(&r);
        }
      }
      if (r.n && r.p[0] == '"') {
        size_t q = 1;
        while (q < r.n && r.p[q] != '"') ++q;
        title.p = r.p + 1;
        title.n = q > 1 ? q - 1 : 0;
        has_title = 1;
        r.p += q < r.n ? q + 1 : r.n;
        r.n -= q < r.n ? q + 1 : r.n;
        trim(&r);
        if (span_starts_ci(&r, "as ")) { r.p += 3; r.n -= 3; trim(&r); }
      }
      id = r;
      trim(&id);
      if (id.n == 0) continue;
      if (composite) {
        RolltuiStr clean;
        int at = graph_sub_find(g, id);
        memset(&clean, 0, sizeof clean);
        if (at < 0) {
          if (g->ns >= 60) { reason_set(reason, "a state diagram with more than 60 composite states is not drawn"); return 0; }
          g->s = (GSub*)rolltui_grow(g->s, &g->scap, g->ns + 1, sizeof *g->s);
          memset(&g->s[g->ns], 0, sizeof g->s[g->ns]);
          rolltui_str_set(&g->s[g->ns].id, id.p, id.n);
          if (has_title) clean_label(title, &clean); else clean_label(id, &clean);
          rolltui_str_set(&g->s[g->ns].title, clean.p ? clean.p : "", clean.n);
          rolltui_str_free(&clean);
          g->s[g->ns].parent = sub;
          at = (int)g->ns++;
        }
        sub = at;
        continue;
      }
      {
        const int at = graph_node(g, id, sub);
        if (at < 0) { reason_set(reason, "a state diagram with more than 240 states is not drawn"); return 0; }
        g->n[at].shape = kind == 1 ? SH_FORK : kind == 2 ? SH_DIAMOND : SH_ROUND;
        if (has_title) clean_label(title, &g->n[at].label);
        if (kind == 2) rolltui_str_set(&g->n[at].label, " ", 1);
      }
      continue;
    }
    arrow = find_arrow(l);
    if (arrow < l.n) {
      Span a = {l.p, arrow}, b = {l.p + arrow + 3, l.n - arrow - 3}, label = {NULL, 0};
      size_t colon = 0;
      int from, to;
      while (colon < b.n && b.p[colon] != ':') ++colon;
      if (colon < b.n) { label.p = b.p + colon + 1; label.n = b.n - colon - 1; b.n = colon; }
      trim(&a);
      trim(&b);
      if (a.n == 3 && memcmp(a.p, "[*]", 3) == 0) from = state_scoped_node(g, sub, 0);
      else from = state_named(g, a, sub);
      if (b.n == 3 && memcmp(b.p, "[*]", 3) == 0) to = state_scoped_node(g, sub, 1);
      else to = state_named(g, b, sub);
      if (from <= -100) from = sub_representative(g, -100 - from, 0);
      if (to <= -100) to = sub_representative(g, -100 - to, 1);
      if (from < 0 || to < 0) {
        if (from == -1 || to == -1) { reason_set(reason, "a transition needs a state at each end"); return 0; }
        continue;
      }
      graph_edge(g, from, to, ES_SOLID, HEAD_NONE, HEAD_ARROW, 1, label);
      continue;
    }
    {
      /* `state : a description` */
      size_t colon = 0;
      int at;
      while (colon < l.n && l.p[colon] != ':') ++colon;
      if (colon < l.n) {
        Span id = {l.p, colon}, d = {l.p + colon + 1, l.n - colon - 1};
        RolltuiStr clean;
        memset(&clean, 0, sizeof clean);
        at = state_named(g, id, sub);
        if (at <= -100) continue;
        if (at < 0) continue;
        clean_label(d, &clean);
        if (g->n[at].detail.n) rolltui_str_append(&g->n[at].detail, "\n", 1);
        rolltui_str_append_str(&g->n[at].detail, &clean);
        rolltui_str_free(&clean);
        g->n[at].shape = SH_RECORD;
        continue;
      }
      if (l.n) {
        Span id = l;
        at = state_named(g, id, sub);
        (void)at;
        continue;
      }
    }
  }
  graph_resolve_phantoms(g);
  if (g->nn == 0) { reason_set(reason, "a state diagram with no states"); return 0; }
  return 1;
}

/* ============================================================================================
 * CLASS DIAGRAMS and ER DIAGRAMS: records joined by lines
 * ============================================================================================ */

typedef struct Members {
  RolltuiStr attrs, methods, annot;
} Members;

static void members_release(Members* v, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    rolltui_str_free(&v[i].attrs);
    rolltui_str_free(&v[i].methods);
    rolltui_str_free(&v[i].annot);
  }
  rolltui_mem_free(v);
}

static void add_line(RolltuiStr* s, const RolltuiStr* line) {
  if (s->n) rolltui_str_append(s, "\n", 1);
  rolltui_str_append_str(s, line);
}

/* `Shape~T~` is `Shape<T>` */
static void generic_text(Span in, RolltuiStr* out) {
  RolltuiStr clean;
  size_t i;
  int open = 0;
  memset(&clean, 0, sizeof clean);
  rolltui_str_clear(out);
  clean_label(in, &clean);
  for (i = 0; i < clean.n; ++i) {
    if (clean.p[i] == '~') { rolltui_str_append(out, open ? ">" : "<", 1); open = !open; }
    else rolltui_str_append(out, clean.p + i, 1);
  }
  rolltui_str_free(&clean);
}

typedef struct ClassParse {
  Graph* g;
  Members* mem;
  size_t mem_cap;
  RolltuiStr* reason;
} ClassParse;

static int class_named(ClassParse* cp, Span name) {
  Span id = name;
  int at;
  size_t k = 0;
  trim(&id);
  if (id.n >= 2 && id.p[0] == '"' && id.p[id.n - 1] == '"') { ++id.p; id.n -= 2; }
  /* a generic's parameter is part of how it is written, not of what it is called */
  while (k < id.n && id.p[k] != '~' && id.p[k] != '[' && id.p[k] != ' ') ++k;
  {
    Span bare = {id.p, k};
    if (bare.n == 0) return -1;
    at = graph_find(cp->g, bare);
    if (at >= 0) return at;
    at = graph_node(cp->g, bare, -1);
    if (at < 0) { reason_set(cp->reason, "a diagram with more than 240 classes is not drawn"); return -1; }
    cp->g->n[at].shape = SH_RECORD;
    cp->mem = (Members*)rolltui_grow(cp->mem, &cp->mem_cap, (size_t)at + 1, sizeof *cp->mem);
    memset(&cp->mem[at], 0, sizeof cp->mem[at]);
    if (k < id.n) {
      Span rest = {id.p + k, id.n - k};
      RolltuiStr shown;
      memset(&shown, 0, sizeof shown);
      if (rest.p[0] == '~') {
        Span gen = {id.p, id.n};
        generic_text(gen, &shown);
        rolltui_str_set(&cp->g->n[at].label, shown.p ? shown.p : "", shown.n);
      } else if (rest.p[0] == '[') {
        Span lab = {rest.p + 1, rest.n > 1 ? rest.n - 1 : 0};
        if (lab.n && lab.p[lab.n - 1] == ']') --lab.n;
        clean_label(lab, &cp->g->n[at].label);
      }
      rolltui_str_free(&shown);
    }
    return at;
  }
}

static void class_member(ClassParse* cp, int at, Span line) {
  RolltuiStr clean;
  memset(&clean, 0, sizeof clean);
  trim(&line);
  if (line.n == 0) return;
  if (span_starts(&line, "<<")) {
    size_t e = find_close(line, ">>");
    if (e < line.n) {
      rolltui_str_set(&cp->mem[at].annot, line.p, e + 2); /* kept as written: `<<interface>>` is not a tag */
      return;
    }
  }
  clean_label(line, &clean);
  add_line(memchr(line.p, '(', line.n) ? &cp->mem[at].methods : &cp->mem[at].attrs, &clean);
  rolltui_str_free(&clean);
}

/* the relation operators, longest first: what is at each end, and whether the line is dotted */
static size_t class_operator(Span s, size_t at, int* dotted, int* hf, int* ht) {
  static const struct { const char* const op; int dotted; int hf; int ht; } ops[] = {
      {"<|--", 0, HEAD_TRIANGLE, 0}, {"--|>", 0, 0, HEAD_TRIANGLE}, {"<|..", 1, HEAD_TRIANGLE, 0}, {"..|>", 1, 0, HEAD_TRIANGLE},
      {"*--", 0, HEAD_DIAMOND, 0},   {"--*", 0, 0, HEAD_DIAMOND},   {"o--", 0, HEAD_DIAMOND_OPEN, 0}, {"--o", 0, 0, HEAD_DIAMOND_OPEN},
      {"<--", 0, HEAD_ARROW, 0},     {"-->", 0, 0, HEAD_ARROW},     {"<..", 1, HEAD_ARROW, 0},        {"..>", 1, 0, HEAD_ARROW},
      {"--", 0, 0, 0},               {"..", 1, 0, 0}};
  size_t k;
  for (k = 0; k < sizeof ops / sizeof *ops; ++k) {
    const size_t n = strlen(ops[k].op);
    if (s.n - at >= n && memcmp(s.p + at, ops[k].op, n) == 0) {
      /* `o--` is an operator only after a space or a quote: `foo--bar` is not an aggregation */
      if (ops[k].op[0] == 'o' && at > 0 && s.p[at - 1] != ' ' && s.p[at - 1] != '"') continue;
      *dotted = ops[k].dotted;
      *hf = ops[k].hf;
      *ht = ops[k].ht;
      return n;
    }
  }
  return 0;
}

static int class_parse(Graph* g, const Source* src, size_t first, RolltuiStr* reason) {
  ClassParse cp;
  size_t i, k;
  int in_body = -1;
  int ok = 1;
  memset(&cp, 0, sizeof cp);
  cp.g = g;
  cp.reason = reason;
  g->dir = DIR_TD;
  for (i = first; i < src->n && ok; ++i) {
    Span l = src->lines[i];
    if (in_body >= 0) {
      if (l.n && l.p[0] == '}') { in_body = -1; continue; }
      class_member(&cp, in_body, l);
      continue;
    }
    if (first_word_is(&l, "direction")) {
      Span w = {l.p + 9, l.n - 9};
      const int d = (trim(&w), dir_from_word(w));
      if (d >= 0) g->dir = d;
      continue;
    }
    if (first_word_is(&l, "class") && !(l.n > 5 && memchr(l.p, '-', l.n) && 0)) {
      Span r = {l.p + 5, l.n - 5};
      int body = 0, at;
      trim(&r);
      if (r.n && r.p[r.n - 1] == '{') { body = 1; --r.n; trim(&r); }
      at = class_named(&cp, r);
      if (at < 0) { ok = 0; break; }
      if (body) in_body = at;
      continue;
    }
    if (first_word_is(&l, "namespace") || first_word_is(&l, "note") || first_word_is(&l, "click") || first_word_is(&l, "link") ||
        first_word_is(&l, "callback") || first_word_is(&l, "style") || first_word_is(&l, "classDef") || first_word_is(&l, "cssClass") ||
        first_word_is(&l, "accTitle") || first_word_is(&l, "accDescr") || (l.n == 1 && l.p[0] == '}'))
      continue;
    if (span_starts(&l, "<<")) {
      size_t e = find_close(l, ">>");
      if (e < l.n) {
        Span tag = {l.p, e + 2}, who = {l.p + e + 2, l.n - e - 2};
        int at;
        trim(&who);
        at = class_named(&cp, who);
        if (at >= 0) rolltui_str_set(&cp.mem[at].annot, tag.p, tag.n);
        continue;
      }
    }
    {
      /* a relation, or `Name : a member` */
      size_t at = 0, opn = 0;
      int dotted = 0, hf = 0, ht = 0;
      int quoted = 0;
      for (at = 0; at < l.n; ++at) {
        if (l.p[at] == '"') quoted = !quoted;
        if (quoted) continue;
        opn = class_operator(l, at, &dotted, &hf, &ht);
        if (opn) break;
      }
      if (opn) {
        Span a = {l.p, at}, b = {l.p + at + opn, l.n - at - opn}, label = {NULL, 0};
        RolltuiStr card_a, card_b;
        size_t colon = 0;
        int from, to, tmp;
        memset(&card_a, 0, sizeof card_a);
        memset(&card_b, 0, sizeof card_b);
        {
          size_t q = 0;
          int inq = 0;
          for (q = 0; q < b.n; ++q) { if (b.p[q] == '"') inq = !inq; if (!inq && b.p[q] == ':') { colon = q; break; } }
          if (q >= b.n) colon = b.n;
        }
        if (colon < b.n) { label.p = b.p + colon + 1; label.n = b.n - colon - 1; b.n = colon; }
        trim(&a);
        trim(&b);
        /* a cardinality in quotes closes the left name and opens the right */
        if (a.n && a.p[a.n - 1] == '"') {
          size_t q = a.n - 1;
          while (q > 0 && a.p[q - 1] != '"') --q;
          if (q > 0) { Span c = {a.p + q, a.n - q - 1}; clean_label(c, &card_a); a.n = q - 1; trim(&a); }
        }
        if (b.n && b.p[0] == '"') {
          size_t q = 1;
          while (q < b.n && b.p[q] != '"') ++q;
          if (q < b.n) { Span c = {b.p + 1, q - 1}; clean_label(c, &card_b); b.p += q + 1; b.n -= q + 1; trim(&b); }
        }
        from = class_named(&cp, a);
        to = class_named(&cp, b);
        if (from < 0 || to < 0) { rolltui_str_free(&card_a); rolltui_str_free(&card_b); if (!(reason && reason->n)) reason_set(reason, "a relation needs a class at each end"); ok = 0; break; }
        if (ht == HEAD_TRIANGLE || ht == HEAD_DIAMOND || ht == HEAD_DIAMOND_OPEN) {
          /* a parent, a whole, stands above: the end that carries the mark is the first, however it was written */
          tmp = from; from = to; to = tmp;
          hf = ht; ht = 0;
          { RolltuiStr t = card_a; card_a = card_b; card_b = t; }
        }
        if (graph_edge(g, from, to, dotted ? ES_DOTTED : ES_SOLID, hf, ht, 1, label)) {
          rolltui_str_set(&g->e[g->ne - 1].end_a, card_a.p ? card_a.p : "", card_a.n);
          rolltui_str_set(&g->e[g->ne - 1].end_b, card_b.p ? card_b.p : "", card_b.n);
        }
        rolltui_str_free(&card_a);
        rolltui_str_free(&card_b);
        continue;
      }
      {
        size_t colon = 0;
        while (colon < l.n && l.p[colon] != ':') ++colon;
        if (colon < l.n) {
          Span who = {l.p, colon}, mem = {l.p + colon + 1, l.n - colon - 1};
          const int at2 = class_named(&cp, who);
          if (at2 < 0) { ok = 0; break; }
          class_member(&cp, at2, mem);
          continue;
        }
        if (l.n) { if (class_named(&cp, l) < 0) { ok = 0; break; } continue; }
      }
    }
  }
  if (ok) {
    for (k = 0; k < g->nn; ++k) {
      RolltuiStr title;
      memset(&title, 0, sizeof title);
      if (cp.mem[k].annot.n) { rolltui_str_append_str(&title, &cp.mem[k].annot); rolltui_str_append(&title, "\n", 1); }
      rolltui_str_append_str(&title, g->n[k].label.n ? &g->n[k].label : &g->n[k].id);
      rolltui_str_set(&g->n[k].label, title.p ? title.p : "", title.n);
      rolltui_str_free(&title);
      rolltui_str_clear(&g->n[k].detail);
      if (cp.mem[k].attrs.n) rolltui_str_append_str(&g->n[k].detail, &cp.mem[k].attrs);
      if (cp.mem[k].attrs.n && cp.mem[k].methods.n) rolltui_str_append(&g->n[k].detail, "\n\x01\n", 3);
      if (cp.mem[k].methods.n) rolltui_str_append_str(&g->n[k].detail, &cp.mem[k].methods);
      if (!g->n[k].detail.n) g->n[k].shape = SH_RECT; /* a class with nothing in it is its name in a box */
    }
    if (g->nn == 0) { reason_set(reason, "a class diagram with no classes"); ok = 0; }
  }
  members_release(cp.mem, g->nn);
  return ok;
}

/* ---- ER ---------------------------------------------------------------------------------------------- */

static const char* er_card_left(const char* two) {
  if (!strncmp(two, "||", 2)) return "1";
  if (!strncmp(two, "|o", 2)) return "0..1";
  if (!strncmp(two, "}o", 2)) return "0..*";
  if (!strncmp(two, "}|", 2)) return "1..*";
  return NULL;
}
static const char* er_card_right(const char* two) {
  if (!strncmp(two, "||", 2)) return "1";
  if (!strncmp(two, "o|", 2)) return "0..1";
  if (!strncmp(two, "o{", 2)) return "0..*";
  if (!strncmp(two, "|{", 2)) return "1..*";
  return NULL;
}

static int er_entity(Graph* g, Span name) {
  Span id = name;
  int at;
  trim(&id);
  if (id.n >= 2 && id.p[0] == '"' && id.p[id.n - 1] == '"') { ++id.p; id.n -= 2; }
  {
    size_t k = 0;
    while (k < id.n && id.p[k] != '[' && id.p[k] != ' ') ++k;
    if (k < id.n && id.p[k] == '[') {
      Span alias = {id.p + k + 1, id.n - k - 1};
      if (alias.n && alias.p[alias.n - 1] == ']') --alias.n;
      id.n = k;
      at = graph_find(g, id);
      if (at < 0) at = graph_node(g, id, -1);
      if (at >= 0) { g->n[at].shape = SH_RECT; clean_label(alias, &g->n[at].label); }
      return at;
    }
  }
  if (id.n == 0) return -1;
  at = graph_find(g, id);
  if (at >= 0) return at;
  at = graph_node(g, id, -1);
  if (at >= 0) g->n[at].shape = SH_RECT;
  return at;
}

static int er_parse(Graph* g, const Source* src, size_t first, RolltuiStr* reason) {
  size_t i;
  int body = -1;
  typedef struct { RolltuiStr type, name, keys, comment; } Attr;
  Attr* attrs = NULL;
  size_t na = 0, acap = 0, k;
  g->dir = DIR_TD;
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    if (body >= 0) {
      if (l.n && l.p[0] == '}') {
        /* the block ends: its attributes are aligned into columns */
        int tw = 0, nw = 0, kw = 0;
        RolltuiStr det;
        memset(&det, 0, sizeof det);
        for (k = 0; k < na; ++k) {
          tw = imax(tw, (int)attrs[k].type.n);
          nw = imax(nw, (int)attrs[k].name.n);
          kw = imax(kw, (int)attrs[k].keys.n);
        }
        for (k = 0; k < na; ++k) {
          char buf[400];
          const int n = snprintf(buf, sizeof buf, "%-*.*s %-*.*s%s%-*.*s%s%.*s", tw, (int)attrs[k].type.n, attrs[k].type.p ? attrs[k].type.p : "", nw,
                                 (int)attrs[k].name.n, attrs[k].name.p ? attrs[k].name.p : "", kw ? " " : "", kw, (int)attrs[k].keys.n,
                                 attrs[k].keys.p ? attrs[k].keys.p : "", attrs[k].comment.n ? "  " : "", (int)attrs[k].comment.n, attrs[k].comment.p ? attrs[k].comment.p : "");
          size_t len = (size_t)n;
          while (len > 0 && buf[len - 1] == ' ') --len;
          if (k) rolltui_str_append(&det, "\n", 1);
          rolltui_str_append(&det, buf, len);
        }
        rolltui_str_set(&g->n[body].detail, det.p ? det.p : "", det.n);
        if (det.n) g->n[body].shape = SH_RECORD;
        rolltui_str_free(&det);
        for (k = 0; k < na; ++k) { rolltui_str_free(&attrs[k].type); rolltui_str_free(&attrs[k].name); rolltui_str_free(&attrs[k].keys); rolltui_str_free(&attrs[k].comment); }
        na = 0;
        body = -1;
        continue;
      }
      /* type name [keys] ["comment"] */
      {
        Span t = l, w[4];
        int nw2 = 0;
        Span comment = {NULL, 0};
        size_t q = 0;
        while (q < t.n && t.p[q] != '"') ++q;
        if (q < t.n) { comment.p = t.p + q + 1; comment.n = t.n - q - 1; if (comment.n && comment.p[comment.n - 1] == '"') --comment.n; t.n = q; }
        while (t.n && nw2 < 4) {
          size_t e = 0;
          skip_ws(&t);
          if (!t.n) break;
          while (e < t.n && t.p[e] != ' ' && t.p[e] != '\t') ++e;
          w[nw2].p = t.p;
          w[nw2].n = e;
          ++nw2;
          t.p += e;
          t.n -= e;
        }
        if (nw2 >= 2) {
          attrs = (Attr*)rolltui_grow(attrs, &acap, na + 1, sizeof *attrs);
          memset(&attrs[na], 0, sizeof attrs[na]);
          rolltui_str_set(&attrs[na].type, w[0].p, w[0].n);
          rolltui_str_set(&attrs[na].name, w[1].p, w[1].n);
          if (nw2 >= 3) {
            /* `PK, FK` is two keys: split at commas and spaces, and say them with one comma between */
            RolltuiStr keys;
            int q;
            memset(&keys, 0, sizeof keys);
            for (q = 2; q < nw2; ++q) {
              size_t a = 0;
              while (a < w[q].n) {
                size_t e = a;
                while (e < w[q].n && w[q].p[e] != ',') ++e;
                if (e > a) {
                  if (keys.n) rolltui_str_append(&keys, ",", 1);
                  rolltui_str_append(&keys, w[q].p + a, e - a);
                }
                a = e + 1;
              }
            }
            rolltui_str_set(&attrs[na].keys, keys.p ? keys.p : "", keys.n);
            rolltui_str_free(&keys);
          }
          if (comment.n) clean_label(comment, &attrs[na].comment);
          ++na;
        }
      }
      continue;
    }
    if (first_word_is(&l, "direction")) {
      Span w = {l.p + 9, l.n - 9};
      const int d = (trim(&w), dir_from_word(w));
      if (d >= 0) g->dir = d;
      continue;
    }
    if (first_word_is(&l, "title") || first_word_is(&l, "accTitle") || first_word_is(&l, "accDescr") || first_word_is(&l, "style") ||
        first_word_is(&l, "classDef") || first_word_is(&l, "class"))
      continue;
    if (l.n && l.p[l.n - 1] == '{') {
      Span name = {l.p, l.n - 1};
      const int at = er_entity(g, name);
      if (at < 0) { reason_set(reason, "an entity needs a name"); goto fail; }
      body = at;
      continue;
    }
    {
      /* ENTITY ||--o{ ENTITY : label */
      size_t q;
      int found = 0;
      for (q = 0; q + 6 <= l.n; ++q) {
        const char* c = l.p + q;
        const char* lc = er_card_left(c);
        const char* rc = er_card_right(c + 4);
        if (lc && rc && ((c[2] == '-' && c[3] == '-') || (c[2] == '.' && c[3] == '.'))) {
          Span a = {l.p, q}, b = {l.p + q + 6, l.n - q - 6}, label = {NULL, 0};
          size_t colon = 0;
          int from, to;
          while (colon < b.n && b.p[colon] != ':') ++colon;
          if (colon < b.n) { label.p = b.p + colon + 1; label.n = b.n - colon - 1; b.n = colon; }
          from = er_entity(g, a);
          to = er_entity(g, b);
          if (from < 0 || to < 0) { reason_set(reason, "a relationship needs an entity at each end"); goto fail; }
          if (graph_edge(g, from, to, c[2] == '.' ? ES_DOTTED : ES_SOLID, HEAD_NONE, HEAD_NONE, 1, label)) {
            rolltui_str_set(&g->e[g->ne - 1].end_a, lc, strlen(lc));
            rolltui_str_set(&g->e[g->ne - 1].end_b, rc, strlen(rc));
          }
          found = 1;
          break;
        }
      }
      if (found) continue;
      if (l.n) { if (er_entity(g, l) < 0) { reason_set(reason, "a line of an ER diagram is not understood"); goto fail; } }
    }
  }
  for (k = 0; k < na; ++k) { rolltui_str_free(&attrs[k].type); rolltui_str_free(&attrs[k].name); rolltui_str_free(&attrs[k].keys); rolltui_str_free(&attrs[k].comment); }
  rolltui_mem_free(attrs);
  if (g->nn == 0) { reason_set(reason, "an ER diagram with no entities"); return 0; }
  return 1;
fail:
  for (k = 0; k < na; ++k) { rolltui_str_free(&attrs[k].type); rolltui_str_free(&attrs[k].name); rolltui_str_free(&attrs[k].keys); rolltui_str_free(&attrs[k].comment); }
  rolltui_mem_free(attrs);
  return 0;
}

/* ============================================================================================
 * MIND MAPS, TIMELINES, JOURNEYS AND GANTT CHARTS: the ones that are a list with a shape
 * ============================================================================================ */

/* A label broken into lines at spaces, none wider than `width` cells (a word wider than that is cut). */
static void wrap_label(RolltuiMermaid* m, const RolltuiStr* s, int width, RolltuiStr* out) {
  size_t i = 0;
  rolltui_str_clear(out);
  while (i <= s->n) {
    size_t e = i;
    int col = 0;
    size_t start = i;
    while (e < s->n && s->p[e] != '\n') ++e;
    /* one source line, greedily filled */
    {
      size_t w0 = start;
      while (w0 <= e) {
        size_t we = w0;
        int ww;
        while (we < e && s->p[we] != ' ') ++we;
        ww = text_width(m, s->p + w0, we - w0);
        if (col > 0 && col + 1 + ww > width) { rolltui_str_append(out, "\n", 1); col = 0; }
        else if (col > 0) { rolltui_str_append(out, " ", 1); ++col; }
        while (ww > width && width > 1) {
          const size_t keep = rolltui_u_fit(m->u, s->p + w0, we - w0, width, m->ascii, NULL);
          if (keep == 0) break;
          rolltui_str_append(out, s->p + w0, keep);
          rolltui_str_append(out, "\n", 1);
          w0 += keep;
          ww = text_width(m, s->p + w0, we - w0);
          col = 0;
        }
        rolltui_str_append(out, s->p + w0, we - w0);
        col += ww;
        w0 = we + 1;
      }
    }
    if (e < s->n) rolltui_str_append(out, "\n", 1);
    i = e + 1;
  }
}

/* rows of text placed one under another, in a grid sized afterwards: a tiny builder for the list-shaped kinds */
typedef struct Row {
  RolltuiStr text;
  unsigned char cls;
  int x;
} Row;

typedef struct Rows {
  Row* v;
  size_t n, cap;
} Rows;

static void rows_add(Rows* r, int x, const char* text, size_t n, unsigned char cls) {
  r->v = (Row*)rolltui_grow(r->v, &r->cap, r->n + 1, sizeof *r->v);
  memset(&r->v[r->n], 0, sizeof r->v[r->n]);
  rolltui_str_set(&r->v[r->n].text, text, n);
  r->v[r->n].cls = cls;
  r->v[r->n].x = x;
  ++r->n;
}

/* ---- mind map -------------------------------------------------------------------------------------- */

typedef struct MindNode {
  RolltuiStr text;
  int depth, parent;
  int last;            /* the last child of its parent */
} MindNode;

static void mind_text(Span l, RolltuiStr* out) {
  Span t = l;
  size_t k = 0;
  RolltuiStr clean;
  memset(&clean, 0, sizeof clean);
  /* `id((text))`, `id(text)`, `id[text]`, `id{{text}}`, `id))text((`, or plain words; `::icon(...)` and `:::class` are dropped */
  {
    size_t ic = 0;
    while (ic + 1 < t.n && !(t.p[ic] == ':' && t.p[ic + 1] == ':')) ++ic;
    if (ic + 1 < t.n) t.n = ic;
  }
  trim(&t);
  while (k < t.n && (isalnum((unsigned char)t.p[k]) || t.p[k] == '_' || t.p[k] == '-')) ++k;
  if (k > 0 && k < t.n && (t.p[k] == '(' || t.p[k] == '[' || t.p[k] == '{' || t.p[k] == ')')) {
    Span in = {t.p + k, t.n - k};
    while (in.n && (in.p[0] == '(' || in.p[0] == '[' || in.p[0] == '{' || in.p[0] == ')')) { ++in.p; --in.n; }
    while (in.n && (in.p[in.n - 1] == ')' || in.p[in.n - 1] == ']' || in.p[in.n - 1] == '}' || in.p[in.n - 1] == '(')) --in.n;
    t = in;
  }
  clean_label(t, &clean);
  rolltui_str_set(out, clean.p ? clean.p : "", clean.n);
  rolltui_str_free(&clean);
}

static int draw_mindmap(RolltuiMermaid* m, const Source* src, size_t first, int max_width, RolltuiStr* reason) {
  MindNode* nodes = NULL;
  size_t nn = 0, cap = 0, i;
  int* stack_depth = (int*)rolltui_mem_alloc(64 * sizeof *stack_depth);
  int* stack_node = (int*)rolltui_mem_alloc(64 * sizeof *stack_node);
  int sp = 0, ok = 0, widest = 0, y = 0;
  Rows rows;
  memset(&rows, 0, sizeof rows);
  for (i = first; i < src->n; ++i) {
    const int ind = src->indent[i];
    int parent = -1;
    if (first_word_is(&src->lines[i], "icon") || span_starts(&src->lines[i], "::")) continue;
    while (sp > 0 && stack_depth[sp - 1] >= ind) --sp;
    if (sp > 0) parent = stack_node[sp - 1];
    if (parent < 0 && nn > 0) { reason_set(reason, "a mind map has one root, and the rest are under it"); goto done; }
    if (nn >= 400) { reason_set(reason, "a mind map with more than 400 nodes is not drawn"); goto done; }
    nodes = (MindNode*)rolltui_grow(nodes, &cap, nn + 1, sizeof *nodes);
    memset(&nodes[nn], 0, sizeof nodes[nn]);
    mind_text(src->lines[i], &nodes[nn].text);
    nodes[nn].parent = parent;
    nodes[nn].depth = parent < 0 ? 0 : nodes[parent].depth + 1;
    if (sp < 64) { stack_depth[sp] = ind; stack_node[sp] = (int)nn; ++sp; }
    ++nn;
  }
  if (nn == 0) { reason_set(reason, "a mind map with nothing in it"); goto done; }
  for (i = 0; i < nn; ++i) {
    size_t k;
    nodes[i].last = 1;
    for (k = i + 1; k < nn; ++k) {
      if (nodes[k].parent == nodes[i].parent) { nodes[i].last = 0; break; }
      if (nodes[k].depth < nodes[i].depth) break;
    }
  }
  /* rows: the root, then each node under its ancestors' rails */
  {
    RolltuiStr root, wrapped;
    memset(&root, 0, sizeof root);
    memset(&wrapped, 0, sizeof wrapped);
    wrap_label(m, &nodes[0].text, imax(max_width - 4, 8), &wrapped);
    rolltui_str_append_str(&root, &wrapped);
    {
      size_t a = 0;
      while (a <= root.n) {
        size_t e = a;
        while (e < root.n && root.p[e] != '\n') ++e;
        rows_add(&rows, 0, root.p + a, e - a, ROLLTUI_MERMAID_CLASS_TITLE);
        a = e + 1;
      }
    }
    rolltui_str_free(&root);
    rolltui_str_free(&wrapped);
  }
  for (i = 1; i < nn; ++i) {
    RolltuiStr prefix, cont, wrapped;
    int d, anc = (int)i;
    int rails[64];
    size_t a = 0;
    memset(&prefix, 0, sizeof prefix);
    memset(&cont, 0, sizeof cont);
    memset(&wrapped, 0, sizeof wrapped);
    /* the rails of the ancestors, outermost first: a bar while they have more to come */
    for (d = nodes[i].depth; d > 1; --d) {
      anc = nodes[anc].parent;
      rails[d] = !nodes[anc].last;
    }
    for (d = 2; d <= nodes[i].depth; ++d) {
      rolltui_str_append(&prefix, rails[d - 0 > 63 ? 63 : d] ? (m->ascii ? "|  " : "\xE2\x94\x82  ") : "   ", rails[d] ? (m->ascii ? 3 : 5) : 3);
      rolltui_str_append(&cont, rails[d] ? (m->ascii ? "|  " : "\xE2\x94\x82  ") : "   ", rails[d] ? (m->ascii ? 3 : 5) : 3);
    }
    {
      const char* conn = nodes[i].last ? (m->ascii ? "`- " : "\xE2\x94\x94\xE2\x94\x80 ") : (m->ascii ? "+- " : "\xE2\x94\x9C\xE2\x94\x80 ");
      const char* under = nodes[i].last ? "   " : (m->ascii ? "|  " : "\xE2\x94\x82  ");
      const size_t conn_n = strlen(conn), under_n = strlen(under);
      const int pw = 3 * (nodes[i].depth - 1) + 3;
      wrap_label(m, &nodes[i].text, imax(max_width - pw, 6), &wrapped);
      while (a <= wrapped.n) {
        size_t e = a;
        RolltuiStr line;
        memset(&line, 0, sizeof line);
        while (e < wrapped.n && wrapped.p[e] != '\n') ++e;
        rolltui_str_append_str(&line, a == 0 ? &prefix : &cont);
        rolltui_str_append(&line, a == 0 ? conn : under, a == 0 ? conn_n : under_n);
        rows_add(&rows, 0, line.p, line.n, ROLLTUI_MERMAID_CLASS_EDGE);
        {
          /* the text goes in its own class, after the rails */
          const int used = text_width(m, line.p, line.n);
          rows_add(&rows, used, wrapped.p + a, e - a, ROLLTUI_MERMAID_CLASS_TEXT);
          widest = imax(widest, used + text_width(m, wrapped.p + a, e - a));
        }
        rolltui_str_free(&line);
        a = e + 1;
      }
    }
    rolltui_str_free(&prefix);
    rolltui_str_free(&cont);
    rolltui_str_free(&wrapped);
  }
  {
    /* draw the rows: a row of class EDGE is followed by its text row, at its own x, on the same line */
    int total = 0;
    size_t r;
    for (r = 0; r < rows.n; ++r) if (rows.v[r].cls != ROLLTUI_MERMAID_CLASS_TEXT || rows.v[r].x == 0) ++total;
    (void)total;
    for (r = 0; r < rows.n; ++r) {
      if (rows.v[r].cls == ROLLTUI_MERMAID_CLASS_TEXT) continue;
      ++y;
    }
    grid_init(&m->g, imax(widest, 1) + 1, y + 1);
    y = 0;
    for (r = 0; r < rows.n; ++r) {
      if (rows.v[r].cls == ROLLTUI_MERMAID_CLASS_TEXT) continue;
      put_text(m, rows.v[r].x, y, rows.v[r].text.p ? rows.v[r].text.p : "", rows.v[r].text.n, rows.v[r].cls);
      if (r + 1 < rows.n && rows.v[r + 1].cls == ROLLTUI_MERMAID_CLASS_TEXT)
        put_text(m, rows.v[r + 1].x, y, rows.v[r + 1].text.p ? rows.v[r + 1].text.p : "", rows.v[r + 1].text.n, ROLLTUI_MERMAID_CLASS_TEXT);
      ++y;
    }
  }
  m->kind = "mindmap";
  ok = 1;
done:
  for (i = 0; i < nn; ++i) rolltui_str_free(&nodes[i].text);
  rolltui_mem_free(nodes);
  for (i = 0; i < rows.n; ++i) rolltui_str_free(&rows.v[i].text);
  rolltui_mem_free(rows.v);
  rolltui_mem_free(stack_depth);
  rolltui_mem_free(stack_node);
  return ok;
}

/* ---- timeline ---------------------------------------------------------------------------------------- */

typedef struct Period {
  RolltuiStr when;
  RolltuiStr events; /* '\n' between */
  int section;
} Period;

static int draw_timeline(RolltuiMermaid* m, const Source* src, size_t first, const RolltuiStr* title_in, int max_width, RolltuiStr* reason) {
  Period* ps = NULL;
  RolltuiStr* sections = NULL;
  size_t np = 0, pcap = 0, ns = 0, scap = 0, i, k;
  RolltuiStr title;
  int wwhen = 0, y = 0, widest = 0, ok = 0, cur_section = -1;
  memset(&title, 0, sizeof title);
  if (title_in) rolltui_str_set(&title, title_in->p ? title_in->p : "", title_in->n);
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    if (span_starts_ci(&l, "title ")) { Span t = {l.p + 6, l.n - 6}; clean_label(t, &title); continue; }
    if (first_word_is(&l, "section")) {
      Span t = {l.p + 7, l.n - 7};
      trim(&t);
      sections = (RolltuiStr*)rolltui_grow(sections, &scap, ns + 1, sizeof *sections);
      memset(&sections[ns], 0, sizeof sections[ns]);
      clean_label(t, &sections[ns]);
      cur_section = (int)ns++;
      continue;
    }
    {
      /* `2004 : Facebook : Google`, or `: another event` for the period above */
      size_t at = 0;
      Span rest = l;
      Period* p;
      const int cont = l.n && l.p[0] == ':';
      if (np >= 200) { reason_set(reason, "a timeline with more than 200 periods is not drawn"); goto done; }
      if (!cont) {
        while (at < rest.n && rest.p[at] != ':') ++at;
        ps = (Period*)rolltui_grow(ps, &pcap, np + 1, sizeof *ps);
        memset(&ps[np], 0, sizeof ps[np]);
        { Span w = {rest.p, at}; trim(&w); clean_label(w, &ps[np].when); }
        ps[np].section = cur_section;
        p = &ps[np++];
        rest.p += at < rest.n ? at + 1 : rest.n;
        rest.n -= at < rest.n ? at + 1 : rest.n;
      } else {
        if (np == 0) { reason_set(reason, "an event with no period above it"); goto done; }
        p = &ps[np - 1];
        ++rest.p;
        --rest.n;
      }
      while (rest.n) {
        size_t e = 0;
        Span ev;
        while (e < rest.n && rest.p[e] != ':') ++e;
        ev.p = rest.p;
        ev.n = e;
        trim(&ev);
        if (ev.n) {
          RolltuiStr clean;
          memset(&clean, 0, sizeof clean);
          clean_label(ev, &clean);
          if (p->events.n) rolltui_str_append(&p->events, "\n", 1);
          rolltui_str_append_str(&p->events, &clean);
          rolltui_str_free(&clean);
        }
        rest.p += e < rest.n ? e + 1 : rest.n;
        rest.n -= e < rest.n ? e + 1 : rest.n;
      }
    }
  }
  if (np == 0) { reason_set(reason, "a timeline with nothing on it"); goto done; }
  for (i = 0; i < np; ++i) wwhen = imax(wwhen, text_width(m, ps[i].when.p ? ps[i].when.p : "", ps[i].when.n));
  {
    /* rows: title, then per period its events (wrapped), sections as headings */
    const int room = imax(max_width - wwhen - 6, 10);
    int rows = (title.n ? 2 : 0);
    RolltuiStr* wrapped = (RolltuiStr*)rolltui_mem_alloc((np + 1) * sizeof *wrapped);
    int prev_section = -2;
    memset(wrapped, 0, (np + 1) * sizeof *wrapped);
    for (i = 0; i < np; ++i) {
      wrap_label(m, &ps[i].events, room, &wrapped[i]);
      if (ps[i].section != prev_section && ps[i].section >= 0) { rows += 1; }
      prev_section = ps[i].section;
      { int lines = 1; size_t q; for (q = 0; q < wrapped[i].n; ++q) if (wrapped[i].p[q] == '\n') ++lines; rows += lines; }
    }
    grid_init(&m->g, max_width, rows + 1);
    prev_section = -2;
    if (title.n) { put_text(m, 0, y, title.p, title.n, ROLLTUI_MERMAID_CLASS_TITLE); y += 2; }
    for (i = 0; i < np; ++i) {
      size_t a = 0;
      int first_line = 1;
      if (ps[i].section != prev_section && ps[i].section >= 0) {
        put_text(m, 0, y, sections[ps[i].section].p ? sections[ps[i].section].p : "", sections[ps[i].section].n, ROLLTUI_MERMAID_CLASS_TITLE);
        ++y;
      }
      prev_section = ps[i].section;
      put_text(m, wwhen - text_width(m, ps[i].when.p ? ps[i].when.p : "", ps[i].when.n), y, ps[i].when.p ? ps[i].when.p : "", ps[i].when.n, ROLLTUI_MERMAID_CLASS_TEXT);
      while (a <= wrapped[i].n) {
        size_t e = a;
        const int x = wwhen + 1;
        while (e < wrapped[i].n && wrapped[i].p[e] != '\n') ++e;
        if (first_line) {
          put_str(m, x, y, m->ascii ? "o" : "\xE2\x97\x8F", ROLLTUI_MERMAID_CLASS_ARROW);
          put_str(m, x + 1, y, m->ascii ? "-" : "\xE2\x94\x80", ROLLTUI_MERMAID_CLASS_EDGE);
        } else {
          put_str(m, x, y, m->ascii ? "|" : "\xE2\x94\x82", ROLLTUI_MERMAID_CLASS_EDGE);
        }
        put_text(m, x + 3, y, wrapped[i].p + a, e - a, ROLLTUI_MERMAID_CLASS_TEXT);
        widest = imax(widest, x + 3 + text_width(m, wrapped[i].p + a, e - a));
        first_line = 0;
        ++y;
        a = e + 1;
      }
      /* the rail between periods */
      if (i + 1 < np && !ps[i + 1].section) {}
    }
    for (k = 0; k < np; ++k) rolltui_str_free(&wrapped[k]);
    rolltui_mem_free(wrapped);
  }
  (void)widest;
  m->kind = "timeline";
  ok = 1;
done:
  for (i = 0; i < np; ++i) { rolltui_str_free(&ps[i].when); rolltui_str_free(&ps[i].events); }
  rolltui_mem_free(ps);
  for (i = 0; i < ns; ++i) rolltui_str_free(&sections[i]);
  rolltui_mem_free(sections);
  rolltui_str_free(&title);
  return ok;
}

/* ---- user journey -------------------------------------------------------------------------------------- */

typedef struct Task {
  RolltuiStr name, actors;
  int score, section;
} Task;

static int draw_journey(RolltuiMermaid* m, const Source* src, size_t first, int max_width, RolltuiStr* reason) {
  Task* ts = NULL;
  RolltuiStr* sections = NULL;
  RolltuiStr title;
  size_t nt = 0, tcap = 0, ns = 0, scap = 0, i;
  int cur = -1, ok = 0, nw = 0, y = 0, rows;
  memset(&title, 0, sizeof title);
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    if (span_starts_ci(&l, "title ")) { Span t = {l.p + 6, l.n - 6}; clean_label(t, &title); continue; }
    if (first_word_is(&l, "section")) {
      Span t = {l.p + 7, l.n - 7};
      trim(&t);
      sections = (RolltuiStr*)rolltui_grow(sections, &scap, ns + 1, sizeof *sections);
      memset(&sections[ns], 0, sizeof sections[ns]);
      clean_label(t, &sections[ns]);
      cur = (int)ns++;
      continue;
    }
    {
      size_t c1 = 0, c2;
      double score = 0;
      Span name, sc, actors;
      while (c1 < l.n && l.p[c1] != ':') ++c1;
      if (c1 >= l.n) { reason_set(reason, "a journey's task is `name: score: who`"); goto done; }
      c2 = c1 + 1;
      while (c2 < l.n && l.p[c2] != ':') ++c2;
      name.p = l.p; name.n = c1;
      sc.p = l.p + c1 + 1; sc.n = c2 - c1 - 1;
      actors.p = c2 < l.n ? l.p + c2 + 1 : l.p + l.n; actors.n = c2 < l.n ? l.n - c2 - 1 : 0;
      if (!parse_double(sc, &score) || score < 0 || score > 5) { reason_set(reason, "a task's score is a number from 0 to 5"); goto done; }
      if (nt >= 200) { reason_set(reason, "a journey with more than 200 tasks is not drawn"); goto done; }
      ts = (Task*)rolltui_grow(ts, &tcap, nt + 1, sizeof *ts);
      memset(&ts[nt], 0, sizeof ts[nt]);
      trim(&name);
      trim(&actors);
      clean_label(name, &ts[nt].name);
      clean_label(actors, &ts[nt].actors);
      ts[nt].score = (int)(score + 0.5);
      ts[nt].section = cur;
      ++nt;
    }
  }
  if (nt == 0) { reason_set(reason, "a journey with no tasks"); goto done; }
  for (i = 0; i < nt; ++i) nw = imax(nw, text_width(m, ts[i].name.p ? ts[i].name.p : "", ts[i].name.n));
  if (nw > max_width - 12) nw = imax(max_width - 12, 8);
  {
    int prev = -2;
    rows = title.n ? 2 : 0;
    for (i = 0; i < nt; ++i) { if (ts[i].section != prev && ts[i].section >= 0) ++rows; prev = ts[i].section; ++rows; }
    grid_init(&m->g, max_width, rows + 1);
    prev = -2;
    if (title.n) { put_text(m, 0, y, title.p, title.n, ROLLTUI_MERMAID_CLASS_TITLE); y += 2; }
    for (i = 0; i < nt; ++i) {
      int s;
      unsigned char cls;
      char num[8];
      if (ts[i].section != prev && ts[i].section >= 0) {
        put_text(m, 0, y, sections[ts[i].section].p ? sections[ts[i].section].p : "", sections[ts[i].section].n, ROLLTUI_MERMAID_CLASS_TITLE);
        ++y;
      }
      prev = ts[i].section;
      if (text_width(m, ts[i].name.p ? ts[i].name.p : "", ts[i].name.n) > nw) {
        const size_t keep = rolltui_u_fit(m->u, ts[i].name.p, ts[i].name.n, nw - 1, m->ascii, NULL);
        put_text(m, 2, y, ts[i].name.p, keep, ROLLTUI_MERMAID_CLASS_TEXT);
        put_str(m, 2 + nw - 1, y, "\xE2\x80\xA6", ROLLTUI_MERMAID_CLASS_TEXT);
      } else {
        put_text(m, 2, y, ts[i].name.p ? ts[i].name.p : "", ts[i].name.n, ROLLTUI_MERMAID_CLASS_TEXT);
      }
      cls = ts[i].score >= 4 ? ROLLTUI_MERMAID_CLASS_ACCENT2 : ts[i].score == 3 ? ROLLTUI_MERMAID_CLASS_ACCENT3 : ROLLTUI_MERMAID_CLASS_ACCENT4;
      for (s = 0; s < 5; ++s) put_str(m, 2 + nw + 2 + s, y, s < ts[i].score ? (m->ascii ? "#" : "\xE2\x97\x8F") : (m->ascii ? "." : "\xE2\x97\x8B"), s < ts[i].score ? cls : ROLLTUI_MERMAID_CLASS_MUTED);
      snprintf(num, sizeof num, "%d", ts[i].score);
      put_str(m, 2 + nw + 8, y, num, cls);
      put_text(m, 2 + nw + 10, y, ts[i].actors.p ? ts[i].actors.p : "", ts[i].actors.n, ROLLTUI_MERMAID_CLASS_MUTED);
      ++y;
    }
  }
  m->kind = "journey";
  ok = 1;
done:
  for (i = 0; i < nt; ++i) { rolltui_str_free(&ts[i].name); rolltui_str_free(&ts[i].actors); }
  rolltui_mem_free(ts);
  for (i = 0; i < ns; ++i) rolltui_str_free(&sections[i]);
  rolltui_mem_free(sections);
  rolltui_str_free(&title);
  return ok;
}

/* ---- gantt ----------------------------------------------------------------------------------------------- */

typedef struct GTask {
  RolltuiStr name, id;
  long start, end;   /* days: [start, end) */
  int status;        /* 0 plain, 1 done, 2 active, 3 crit */
  int milestone;
  int section;
} GTask;

/* days since 1970-01-01 of a civil date (the proleptic Gregorian calendar) */
static long days_from_civil(long y, unsigned mo, unsigned d) {
  y -= mo <= 2;
  {
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
  }
}

static void civil_from_days(long z, int* y, unsigned* mo, unsigned* d) {
  z += 719468;
  {
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long yy = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *mo = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*mo <= 2));
  }
}

static int parse_date(Span s, long* out) {
  int y, mo, d;
  char buf[32];
  trim(&s);
  if (s.n != 10 || s.n >= sizeof buf) return 0;
  memcpy(buf, s.p, s.n);
  buf[s.n] = 0;
  if (sscanf(buf, "%4d-%2d-%2d", &y, &mo, &d) != 3 || buf[4] != '-' || buf[7] != '-' || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  *out = days_from_civil(y, (unsigned)mo, (unsigned)d);
  return 1;
}

/* `3d`, `2w`, `12h`, `1.5w`: days (0 for an hour count too small to be one) */
static int parse_duration(Span s, double* days) {
  double v;
  Span num;
  char unit;
  trim(&s);
  if (s.n < 2) return 0;
  unit = s.p[s.n - 1];
  num.p = s.p;
  num.n = s.n - 1;
  if (!parse_double(num, &v) || v < 0) return 0;
  switch (unit) {
    case 'd': *days = v; return 1;
    case 'w': *days = v * 7; return 1;
    case 'h': *days = v / 24.0; return 1;
    case 'M': *days = v * 30; return 1;
    case 'y': *days = v * 365; return 1;
    default: return 0;
  }
}

static int draw_gantt(RolltuiMermaid* m, const Source* src, size_t first, int max_width, RolltuiStr* reason) {
  GTask* ts = NULL;
  RolltuiStr* sections = NULL;
  RolltuiStr title;
  size_t nt = 0, tcap = 0, ns = 0, scap = 0, i;
  int cur = -1, ok = 0;
  long t0 = 0, t1 = 0, prev_end = 0;
  int have_prev = 0, name_w = 0, chart_w, rows, y = 0, prev_section = -2, ticks, tk;
  memset(&title, 0, sizeof title);
  for (i = first; i < src->n; ++i) {
    Span l = src->lines[i];
    if (span_starts_ci(&l, "title ")) { Span t = {l.p + 6, l.n - 6}; clean_label(t, &title); continue; }
    if (first_word_is(&l, "dateFormat") || first_word_is(&l, "axisFormat") || first_word_is(&l, "tickInterval") || first_word_is(&l, "excludes") ||
        first_word_is(&l, "includes") || first_word_is(&l, "todayMarker") || first_word_is(&l, "weekday") || first_word_is(&l, "weekend") ||
        first_word_is(&l, "inclusiveEndDates") || first_word_is(&l, "topAxis") || first_word_is(&l, "accTitle") || first_word_is(&l, "accDescr") ||
        first_word_is(&l, "click"))
      continue;
    if (first_word_is(&l, "section")) {
      Span t = {l.p + 7, l.n - 7};
      trim(&t);
      sections = (RolltuiStr*)rolltui_grow(sections, &scap, ns + 1, sizeof *sections);
      memset(&sections[ns], 0, sizeof sections[ns]);
      clean_label(t, &sections[ns]);
      cur = (int)ns++;
      continue;
    }
    {
      size_t colon = 0;
      Span name, spec;
      GTask t;
      int have_start = 0, have_end = 0, have_dur = 0;
      long start = 0, end = 0;
      double dur = 0;
      memset(&t, 0, sizeof t);
      while (colon < l.n && l.p[colon] != ':') ++colon;
      if (colon >= l.n) { reason_set(reason, "a task in a Gantt chart is `name : id, start, duration`"); goto done; }
      name.p = l.p; name.n = colon;
      spec.p = l.p + colon + 1; spec.n = l.n - colon - 1;
      trim(&name);
      while (spec.n) {
        size_t e = 0;
        Span tok;
        long d;
        double dv;
        while (e < spec.n && spec.p[e] != ',') ++e;
        tok.p = spec.p; tok.n = e;
        trim(&tok);
        spec.p += e < spec.n ? e + 1 : spec.n;
        spec.n -= e < spec.n ? e + 1 : spec.n;
        if (tok.n == 0) continue;
        if (tok.n == 4 && !memcmp(tok.p, "done", 4)) t.status = 1;
        else if (tok.n == 6 && !memcmp(tok.p, "active", 6)) t.status = 2;
        else if (tok.n == 4 && !memcmp(tok.p, "crit", 4)) t.status = 3;
        else if (tok.n == 9 && !memcmp(tok.p, "milestone", 9)) t.milestone = 1;
        else if (span_starts_ci(&tok, "after ")) {
          /* after another task: the latest end among those named */
          Span ids = {tok.p + 6, tok.n - 6};
          long latest = 0;
          int found = 0;
          while (ids.n) {
            size_t q = 0;
            Span one;
            size_t k;
            while (q < ids.n && ids.p[q] != ' ') ++q;
            one.p = ids.p; one.n = q;
            for (k = 0; k < nt; ++k)
              if (ts[k].id.n == one.n && memcmp(ts[k].id.p, one.p, one.n) == 0) { if (!found || ts[k].end > latest) latest = ts[k].end; found = 1; }
            ids.p += q < ids.n ? q + 1 : ids.n;
            ids.n -= q < ids.n ? q + 1 : ids.n;
            skip_ws(&ids);
          }
          if (!found) { reason_set(reason, "a task starts after one that has not been defined"); goto done; }
          start = latest; have_start = 1;
        } else if (parse_date(tok, &d)) {
          if (!have_start) { start = d; have_start = 1; } else { end = d; have_end = 1; }
        } else if (parse_duration(tok, &dv)) { dur = dv; have_dur = 1; }
        else if (!t.id.n) rolltui_str_set(&t.id, tok.p, tok.n);
      }
      if (!have_start) start = have_prev ? prev_end : 0;
      if (have_end) { if (end < start) end = start; }
      else if (have_dur) end = start + (long)(dur + 0.999);
      else end = start + 1;
      if (t.milestone) end = start;
      if (nt >= 200) { reason_set(reason, "a Gantt chart with more than 200 tasks is not drawn"); goto done; }
      ts = (GTask*)rolltui_grow(ts, &tcap, nt + 1, sizeof *ts);
      ts[nt] = t;
      clean_label(name, &ts[nt].name);
      ts[nt].start = start;
      ts[nt].end = end;
      ts[nt].section = cur;
      prev_end = end;
      have_prev = 1;
      ++nt;
    }
  }
  if (nt == 0) { reason_set(reason, "a Gantt chart with no tasks"); goto done; }
  t0 = ts[0].start;
  t1 = ts[0].end;
  for (i = 0; i < nt; ++i) {
    if (ts[i].start < t0) t0 = ts[i].start;
    if (ts[i].end > t1) t1 = ts[i].end;
    name_w = imax(name_w, text_width(m, ts[i].name.p ? ts[i].name.p : "", ts[i].name.n));
  }
  if (t1 <= t0) t1 = t0 + 1;
  if (name_w > 28) name_w = 28;
  chart_w = imin(max_width - name_w - 3, 64);
  if (chart_w < 16) { reason_setf(reason, "a Gantt chart needs about %d columns; this view has %d", name_w + 20, max_width); goto done; }
  {
    int prev = -2;
    rows = (title.n ? 2 : 0) + 2;
    for (i = 0; i < nt; ++i) { if (ts[i].section != prev && ts[i].section >= 0) ++rows; prev = ts[i].section; ++rows; }
  }
  grid_init(&m->g, name_w + 3 + chart_w + 1, rows + 1);
  if (title.n) { put_text(m, 0, y, title.p, title.n, ROLLTUI_MERMAID_CLASS_TITLE); y += 2; }
  /* the axis: a rule with ticks, and the dates under them */
  ticks = imax(2, imin(chart_w / 12, 8));
  for (tk = 0; tk < chart_w; ++tk) put_str(m, name_w + 3 + tk, y + 1, m->ascii ? "-" : "\xE2\x94\x80", ROLLTUI_MERMAID_CLASS_BOX);
  for (tk = 0; tk <= ticks; ++tk) {
    const int cx = tk * (chart_w - 1) / ticks;
    const long day = t0 + (long)((double)(t1 - t0) * cx / (double)imax(chart_w - 1, 1));
    int yy;
    unsigned mo, dd;
    char lab[16];
    static const char* const mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    civil_from_days(day, &yy, &mo, &dd);
    snprintf(lab, sizeof lab, "%s %u", mon[mo - 1], dd);
    put_str(m, name_w + 3 + cx, y + 1, m->ascii ? "+" : "\xE2\x94\xAC", ROLLTUI_MERMAID_CLASS_BOX);
    {
      int lx = name_w + 3 + cx - (int)strlen(lab) / 2;
      if (lx < name_w + 3) lx = name_w + 3;
      if (lx + (int)strlen(lab) > m->g.w) lx = m->g.w - (int)strlen(lab);
      put_str(m, lx, y, lab, ROLLTUI_MERMAID_CLASS_MUTED);
    }
  }
  y += 2;
  for (i = 0; i < nt; ++i) {
    unsigned char cls = ts[i].status == 1 ? ROLLTUI_MERMAID_CLASS_MUTED : ts[i].status == 2 ? ROLLTUI_MERMAID_CLASS_ACCENT1 : ts[i].status == 3 ? ROLLTUI_MERMAID_CLASS_ACCENT4 : ROLLTUI_MERMAID_CLASS_ACCENT2;
    const double scale = (double)chart_w / (double)(t1 - t0);
    int a = (int)((double)(ts[i].start - t0) * scale + 0.5), b = (int)((double)(ts[i].end - t0) * scale + 0.5), c;
    if (ts[i].section != prev_section && ts[i].section >= 0) {
      put_text(m, 0, y, sections[ts[i].section].p ? sections[ts[i].section].p : "", sections[ts[i].section].n, ROLLTUI_MERMAID_CLASS_TITLE);
      ++y;
    }
    prev_section = ts[i].section;
    if (text_width(m, ts[i].name.p ? ts[i].name.p : "", ts[i].name.n) > name_w) {
      const size_t keep = rolltui_u_fit(m->u, ts[i].name.p, ts[i].name.n, name_w - 1, m->ascii, NULL);
      put_text(m, 0, y, ts[i].name.p, keep, ROLLTUI_MERMAID_CLASS_TEXT);
      put_str(m, name_w - 1, y, "\xE2\x80\xA6", ROLLTUI_MERMAID_CLASS_TEXT);
    } else {
      put_text(m, 0, y, ts[i].name.p ? ts[i].name.p : "", ts[i].name.n, ROLLTUI_MERMAID_CLASS_TEXT);
    }
    put_str(m, name_w + 1, y, m->ascii ? "|" : "\xE2\x94\x82", ROLLTUI_MERMAID_CLASS_BOX);
    if (a >= chart_w) a = chart_w - 1;
    if (b <= a) b = a + 1;
    if (b > chart_w) b = chart_w;
    if (ts[i].milestone) put_str(m, name_w + 3 + a, y, m->ascii ? "<>" : "\xE2\x97\x86", cls);
    else for (c = a; c < b; ++c) put_str(m, name_w + 3 + c, y, m->ascii ? "#" : "\xE2\x96\x88", cls);
    ++y;
  }
  m->kind = "gantt";
  ok = 1;
done:
  for (i = 0; i < nt; ++i) { rolltui_str_free(&ts[i].name); rolltui_str_free(&ts[i].id); }
  rolltui_mem_free(ts);
  for (i = 0; i < ns; ++i) rolltui_str_free(&sections[i]);
  rolltui_mem_free(sections);
  rolltui_str_free(&title);
  return ok;
}

/* ============================================================================================
 * THE ENTRY POINT
 * ============================================================================================ */

RolltuiMermaid* rolltui_mermaid_new(void) {
  RolltuiMermaid* m = (RolltuiMermaid*)rolltui_mem_alloc(sizeof *m);
  memset(m, 0, sizeof *m);
  m->u = rolltui_u_scratch_new();
  m->kind = "";
  return m;
}

void rolltui_mermaid_free(RolltuiMermaid* m) {
  if (!m) return;
  rolltui_mem_free(m->g.c);
  rolltui_mem_free(m->runs);
  rolltui_mem_free(m->line_first);
  rolltui_mem_free(m->run_off);
  rolltui_str_free(&m->pool);
  rolltui_str_free(&m->scratch);
  rolltui_u_scratch_free(m->u);
  rolltui_mem_free(m);
}

size_t rolltui_mermaid_line_count(const RolltuiMermaid* m) { return m ? m->lines_n : 0; }
int rolltui_mermaid_width(const RolltuiMermaid* m) { return m ? m->width : 0; }
const char* rolltui_mermaid_kind(const RolltuiMermaid* m) { return m ? m->kind : ""; }
size_t rolltui_mermaid_line(const RolltuiMermaid* m, size_t i, const RolltuiMermaidRun** runs) {
  if (!m || i >= m->lines_n) {
    if (runs) *runs = NULL;
    return 0;
  }
  if (runs) *runs = m->runs + m->line_first[i];
  return m->line_first[i + 1] - m->line_first[i];
}

int rolltui_mermaid_render(RolltuiMermaid* m, const char* src, size_t n, int max_width, int ascii, RolltuiStr* reason) {
  Source s;
  int ok = 0;
  size_t first = 0;
  Span head;
  if (reason) rolltui_str_clear(reason);
  if (!m) return 0;
  out_reset(m);
  grid_init(&m->g, 0, 0);
  m->ascii = ascii != 0;
  if (max_width < 8) max_width = 8;
  if (!source_read(&s, src ? src : "", src ? n : 0)) {
    reason_set(reason, "the diagram is too long to draw");
    return 0;
  }
  if (s.n == 0) {
    reason_set(reason, "the diagram has nothing in it");
    source_release(&s);
    return 0;
  }
  head = s.lines[0];
  first = 1;
  if (first_word_is(&head, "pie")) {
    RolltuiStr title;
    int show_data = 0;
    Span rest = {head.p + 3, head.n - 3};
    memset(&title, 0, sizeof title);
    trim(&rest);
    if (span_starts_ci(&rest, "showData")) { show_data = 1; rest.p += 8; rest.n -= 8; trim(&rest); }
    if (span_starts_ci(&rest, "title ")) { Span t = {rest.p + 6, rest.n - 6}; clean_label(t, &title); }
    else if (s.title.n) rolltui_str_set(&title, s.title.p, s.title.n);
    if (first < s.n && span_starts_ci(&s.lines[first], "title ")) {
      Span t = {s.lines[first].p + 6, s.lines[first].n - 6};
      clean_label(t, &title);
      ++first;
    }
    ok = draw_pie(m, &s, first, show_data, &title, max_width, reason);
    rolltui_str_free(&title);
  } else if (first_word_is(&head, "sequenceDiagram")) {
    ok = draw_sequence(m, &s, first, max_width, reason);
  } else if (first_word_is(&head, "flowchart") || first_word_is(&head, "graph")) {
    Graph g;
    memset(&g, 0, sizeof g);
    if (flow_parse(&g, &s, first, head, reason)) ok = draw_graph(m, &g, max_width, reason);
    if (ok) m->kind = "flowchart";
    graph_release(&g);
  } else if (first_word_is(&head, "stateDiagram") || first_word_is(&head, "stateDiagram-v2")) {
    Graph g;
    memset(&g, 0, sizeof g);
    if (state_parse(&g, &s, first, head, reason)) ok = draw_graph(m, &g, max_width, reason);
    if (ok) m->kind = "state";
    graph_release(&g);
  } else if (first_word_is(&head, "classDiagram") || first_word_is(&head, "classDiagram-v2")) {
    Graph g;
    memset(&g, 0, sizeof g);
    if (class_parse(&g, &s, first, reason)) ok = draw_graph(m, &g, max_width, reason);
    if (ok) m->kind = "class";
    graph_release(&g);
  } else if (first_word_is(&head, "erDiagram")) {
    Graph g;
    memset(&g, 0, sizeof g);
    if (er_parse(&g, &s, first, reason)) ok = draw_graph(m, &g, max_width, reason);
    if (ok) m->kind = "er";
    graph_release(&g);
  } else if (first_word_is(&head, "mindmap")) {
    ok = draw_mindmap(m, &s, first, max_width, reason);
  } else if (first_word_is(&head, "timeline")) {
    ok = draw_timeline(m, &s, first, s.title.n ? &s.title : NULL, max_width, reason);
  } else if (first_word_is(&head, "journey")) {
    ok = draw_journey(m, &s, first, max_width, reason);
  } else if (first_word_is(&head, "gantt")) {
    ok = draw_gantt(m, &s, first, max_width, reason);
  } else {
    char msg[120];
    size_t kn = 0;
    while (kn < head.n && head.p[kn] != ' ' && head.p[kn] != '\t' && kn < 40) ++kn;
    snprintf(msg, sizeof msg, "%.*s diagrams are not drawn", (int)kn, head.p);
    reason_set(reason, msg);
  }
  source_release(&s);
  if (!ok) {
    /* nothing is refused without a reason a person can read */
    if (reason && reason->n == 0) reason_set(reason, "a line of the diagram is not understood");
    grid_init(&m->g, 0, 0);
    out_reset(m);
    return 0;
  }
  read_out(m);
  if (m->width > max_width) {
    reason_setf(reason, "the diagram needs %d columns; this view has %d", m->width, max_width);
    grid_init(&m->g, 0, 0);
    out_reset(m);
    return 0;
  }
  return 1;
}
