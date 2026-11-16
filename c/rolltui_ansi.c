/* rolltui/c/rolltui_ansi.c — ANOTHER PROGRAM'S COLOURED OUTPUT, read as styled lines.
 *
 * What a program writes to a terminal is read as a stream: text, newlines, carriage returns, backspace overstrike, tabs and SGR.
 * Everything else a terminal would act on (cursor moves, erase-in-display, OSC, DCS, APC, PM, SOS, charset designations) is
 * swallowed, so a previewer's output, `ls --color`, `git log --color` or a man page reads as what it says, never as commands to this
 * screen. A sequence or a UTF-8 character split across two feeds is joined.
 *
 * A LINE is its text and the runs of style over it, which is the compact form: a log of a hundred thousand lines holds the bytes
 * and a few runs a line, not a cell array. A carriage return not followed by a newline means the line is written again (a progress
 * counter), so the next character starts the line over. The lines are a ring capped at `limit`: a log keeps its newest.
 */
#include "rolltui/rolltui.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"

typedef struct AnsiRun {
  size_t begin; /* the byte of the line's text this style starts at */
  RolltuiStyle style;
} AnsiRun;

typedef struct AnsiLine {
  RolltuiStr text;
  AnsiRun* runs;
  size_t run_n, run_cap;
  int cols; /* the columns written so far, for tab stops */
} AnsiLine;

enum { ST_GROUND = 0, ST_ESC, ST_ESC_SKIP, ST_CSI, ST_STRING, ST_STRING_ESC };

#define SEQ_MAX 96
#define TAB 8

struct RolltuiAnsiText {
  AnsiLine* lines; /* a ring: `head` is the oldest */
  size_t n, cap, head, limit;
  int state;
  char seq[SEQ_MAX];
  size_t seq_n;
  int seq_over;
  unsigned char utf8[4];
  size_t utf8_n, utf8_need;
  RolltuiStyle cur;
  int pending_cr;
  char over[4]; /* the character a backspace stepped back over */
  size_t over_n;
  int over_pending;
  RolltuiUnicodeScratch* u;
  RolltuiUnicodeGrapheme* g; /* the draw's grapheme walk */
  size_t g_cap;
};

static int imax(int a, int b) { return a > b ? a : b; }

/* ---- the ring ------------------------------------------------------------------------------ */

static AnsiLine* line_at(const RolltuiAnsiText* t, size_t i) { return &t->lines[(t->head + i) % t->cap]; }
static AnsiLine* last_line(RolltuiAnsiText* t) { return line_at(t, t->n - 1); }

static void line_reset(AnsiLine* l) {
  rolltui_str_clear(&l->text);
  l->run_n = 0;
  l->cols = 0;
}

/* The slots the ring may have: `limit` finished lines and the one being written. 0: no cap. */
static size_t slots(const RolltuiAnsiText* t) { return t->limit ? t->limit + 1 : 0; }

/* A new line at the end: a fresh slot, or the oldest one's buffers once the ring holds all it may. */
static void push_line(RolltuiAnsiText* t) {
  const size_t max = slots(t);
  if (max && t->n >= max) { /* full: the oldest leaves the window and the slot after the newest is the new line */
    line_reset(&t->lines[(t->head + t->n) % t->cap]);
    t->head = (t->head + 1) % t->cap;
    return;
  }
  if (t->n == t->cap) { /* grow, keeping the ring's order */
    size_t i, ncap = t->cap ? t->cap * 2 : 64;
    AnsiLine* v;
    if (max && ncap > max) ncap = max;
    v = (AnsiLine*)rolltui_mem_alloc(ncap * sizeof *v);
    memset(v, 0, ncap * sizeof *v);
    for (i = 0; i < t->n; ++i) v[i] = *line_at(t, i);
    rolltui_mem_free(t->lines);
    t->lines = v;
    t->cap = ncap;
    t->head = 0;
  }
  line_reset(&t->lines[(t->head + t->n) % t->cap]);
  ++t->n;
}

RolltuiAnsiText* rolltui_ansi_new(void) {
  RolltuiAnsiText* t = (RolltuiAnsiText*)rolltui_mem_alloc(sizeof *t);
  memset(t, 0, sizeof *t);
  t->limit = ROLLTUI_ANSI_DEFAULT_LIMIT;
  t->u = rolltui_u_scratch_new();
  push_line(t);
  return t;
}

void rolltui_ansi_free(RolltuiAnsiText* t) {
  size_t i;
  if (!t) return;
  for (i = 0; i < t->cap; ++i) {
    rolltui_str_free(&t->lines[i].text);
    rolltui_mem_free(t->lines[i].runs);
  }
  rolltui_mem_free(t->lines);
  rolltui_mem_free(t->g);
  rolltui_u_scratch_free(t->u);
  rolltui_mem_free(t);
}

void rolltui_ansi_clear(RolltuiAnsiText* t) {
  if (!t) return;
  t->n = 0;
  t->head = 0;
  t->state = ST_GROUND;
  t->seq_n = 0;
  t->utf8_n = t->utf8_need = 0;
  memset(&t->cur, 0, sizeof t->cur);
  t->pending_cr = 0;
  t->over_pending = 0;
  push_line(t);
}

void rolltui_ansi_set_limit(RolltuiAnsiText* t, size_t max_lines) {
  if (!t) return;
  t->limit = max_lines;
  while (slots(t) && t->n > slots(t)) { /* the oldest go */
    line_reset(&t->lines[t->head]);
    t->head = (t->head + 1) % t->cap;
    --t->n;
  }
}

size_t rolltui_ansi_line_count(const RolltuiAnsiText* t) {
  if (!t || t->n == 0) return 0;
  /* the line being written counts once it has text: "a\n" is one line, "a\nb" two */
  return line_at(t, t->n - 1)->text.n ? t->n : t->n - 1;
}

const char* rolltui_ansi_line(const RolltuiAnsiText* t, size_t i, size_t* len) {
  const AnsiLine* l;
  if (!t || i >= t->n) {
    if (len) *len = 0;
    return "";
  }
  l = line_at(t, i);
  if (len) *len = l->text.n;
  return l->text.p ? l->text.p : "";
}

/* ---- writing ------------------------------------------------------------------------------- */

static int style_eq(const RolltuiStyle* a, const RolltuiStyle* b) { return memcmp(a, b, sizeof *a) == 0; }

static void append(RolltuiAnsiText* t, const char* bytes, size_t n, const RolltuiStyle* st, int cols) {
  AnsiLine* l = last_line(t);
  if (l->run_n == 0 || !style_eq(&l->runs[l->run_n - 1].style, st)) {
    if (l->run_n && l->runs[l->run_n - 1].begin == l->text.n) { /* a style that never got a byte is replaced */
      l->runs[l->run_n - 1].style = *st;
    } else {
      l->runs = (AnsiRun*)rolltui_grow(l->runs, &l->run_cap, l->run_n + 1, sizeof *l->runs);
      l->runs[l->run_n].begin = l->text.n;
      l->runs[l->run_n].style = *st;
      ++l->run_n;
    }
  }
  rolltui_str_append(&l->text, bytes, n);
  l->cols += cols;
}

static int cp_cols(const char* bytes, size_t n) {
  RolltuiDecodedChar d;
  rolltui_u_decode_one(bytes, n, 0, &d);
  return imax(rolltui_u_codepoint_width(d.cp, 0), 0);
}

static void put_char(RolltuiAnsiText* t, const char* bytes, size_t n) {
  RolltuiStyle st = t->cur;
  if (t->pending_cr) { /* a carriage return, then text: the line is written again */
    line_reset(last_line(t));
    t->pending_cr = 0;
    t->over_pending = 0;
  }
  if (t->over_pending) { /* OVERSTRIKE, a man page's: X\bX is a bold X, _\bX an underlined one */
    t->over_pending = 0;
    if (t->over_n == n && memcmp(t->over, bytes, n) == 0) {
      st.bold = 1;
    } else if (t->over_n == 1 && t->over[0] == '_') {
      st.underline = 1;
    } else if (n == 1 && bytes[0] == '_') {
      st.underline = 1;
      append(t, t->over, t->over_n, &st, cp_cols(t->over, t->over_n));
      return;
    }
  }
  append(t, bytes, n, &st, cp_cols(bytes, n));
}

static void backspace(RolltuiAnsiText* t) {
  AnsiLine* l = last_line(t);
  size_t b;
  if (l->text.n == 0) return;
  b = l->text.n - 1;
  while (b > 0 && ((unsigned char)l->text.p[b] & 0xC0) == 0x80) --b;
  t->over_n = l->text.n - b > sizeof t->over ? sizeof t->over : l->text.n - b;
  memcpy(t->over, l->text.p + b, t->over_n);
  t->over_pending = 1;
  l->cols -= cp_cols(l->text.p + b, l->text.n - b);
  if (l->cols < 0) l->cols = 0;
  l->text.n = b;
  while (l->run_n && l->runs[l->run_n - 1].begin >= l->text.n && l->run_n > 1) --l->run_n;
  if (l->text.n == 0) l->run_n = 0;
}

static void newline(RolltuiAnsiText* t) {
  t->pending_cr = 0;
  t->over_pending = 0;
  push_line(t);
}

/* ---- SGR ----------------------------------------------------------------------------------- */

static void colour_indexed(RolltuiStyleColorRaw* c, int i) {
  memset(c, 0, sizeof *c);
  c->kind = 1;
  c->index = (unsigned char)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static unsigned char byte_of(int v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* Parses `p` (the parameters of an SGR, `;` between parameters and `:` between a parameter's parts) into the current style. */
static void sgr(RolltuiAnsiText* t, const char* p, size_t n) {
  int v[32], sub[32];
  size_t k = 0, i = 0;
  int cur = 0, have = 0, colon = 0;
  /* the numbers, and for each whether it was joined to the one before by a colon */
  for (i = 0; i <= n && k < 32; ++i) {
    const char c = i < n ? p[i] : ';';
    if (c >= '0' && c <= '9') {
      cur = cur * 10 + (c - '0');
      if (cur > 100000) cur = 100000;
      have = 1;
    } else if (c == ';' || c == ':') {
      v[k] = have ? cur : 0;
      sub[k] = colon;
      ++k;
      colon = c == ':';
      cur = 0;
      have = 0;
    } else {
      return; /* a private or malformed SGR: not this file's to guess at */
    }
  }
  for (i = 0; i < k; ++i) {
    const int x = v[i];
    RolltuiStyleColorRaw* target = NULL;
    if (x == 38 || x == 48 || x == 58) {
      size_t j = i + 1;
      target = x == 38 ? &t->cur.fg : x == 48 ? &t->cur.bg : NULL; /* 58, the underline's colour, is read and dropped */
      if (j < k && v[j] == 5 && j + 1 < k) {
        if (target) colour_indexed(target, v[j + 1]);
        i = j + 1;
      } else if (j < k && v[j] == 2) {
        /* 38;2;r;g;b, or 38:2::r:g:b with the colour space's slot */
        size_t r = j + 1;
        if (sub[j] && r + 3 < k && sub[r + 3]) ++r;
        if (r + 2 < k) {
          if (target) {
            memset(target, 0, sizeof *target);
            target->kind = 2;
            target->r = byte_of(v[r]);
            target->g = byte_of(v[r + 1]);
            target->b = byte_of(v[r + 2]);
          }
          i = r + 2;
        } else {
          i = k;
        }
      } else {
        i = j;
      }
      continue;
    }
    if (sub[i]) continue; /* a part of something not read here (4:3, a curly underline, is still an underline: below) */
    switch (x) {
      case 0: memset(&t->cur, 0, sizeof t->cur); break;
      case 1: t->cur.bold = 1; break;
      case 2: t->cur.dim = 1; break;
      case 3: t->cur.italic = 1; break;
      case 4:
        t->cur.underline = !(i + 1 < k && sub[i + 1] && v[i + 1] == 0);
        break;
      case 7: t->cur.reverse = 1; break;
      case 21: t->cur.underline = 1; break;
      case 22: t->cur.bold = 0; t->cur.dim = 0; break;
      case 23: t->cur.italic = 0; break;
      case 24: t->cur.underline = 0; break;
      case 27: t->cur.reverse = 0; break;
      case 39: memset(&t->cur.fg, 0, sizeof t->cur.fg); break;
      case 49: memset(&t->cur.bg, 0, sizeof t->cur.bg); break;
      default:
        if (x >= 30 && x <= 37) colour_indexed(&t->cur.fg, x - 30);
        else if (x >= 40 && x <= 47) colour_indexed(&t->cur.bg, x - 40);
        else if (x >= 90 && x <= 97) colour_indexed(&t->cur.fg, x - 90 + 8);
        else if (x >= 100 && x <= 107) colour_indexed(&t->cur.bg, x - 100 + 8);
        break; /* blink, conceal, strike, fonts: read and dropped */
    }
  }
}

static void csi(RolltuiAnsiText* t, char final) {
  const char* p = t->seq;
  size_t n = t->seq_n;
  if (t->seq_over) return;
  if (final == 'm') {
    size_t i;
    for (i = 0; i < n; ++i)
      if (!((p[i] >= '0' && p[i] <= '9') || p[i] == ';' || p[i] == ':')) return; /* `>4;2m` and the like are a terminal's own */
    sgr(t, p, n);
    return;
  }
  if (final == 'K') { /* ERASE IN LINE: with writing only ever at the line's end, 1 and 2 clear it; 0 clears it after a CR */
    const int mode = n ? p[0] - '0' : 0;
    if (mode == 1 || mode == 2 || t->pending_cr) {
      line_reset(last_line(t));
      t->pending_cr = 0;
    }
  }
  /* every other CSI moves a cursor or asks a terminal something: swallowed */
}

/* ---- the stream ---------------------------------------------------------------------------- */

static void ground(RolltuiAnsiText* t, unsigned char b);

static void flush_utf8(RolltuiAnsiText* t) {
  /* an incomplete character: its bytes are text as they came, never dropped */
  if (t->utf8_n) put_char(t, (const char*)t->utf8, t->utf8_n);
  t->utf8_n = t->utf8_need = 0;
}

static void ground(RolltuiAnsiText* t, unsigned char b) {
  if (t->utf8_need) {
    if ((b & 0xC0) == 0x80) {
      t->utf8[t->utf8_n++] = b;
      if (--t->utf8_need == 0) {
        put_char(t, (const char*)t->utf8, t->utf8_n);
        t->utf8_n = 0;
      }
      return;
    }
    flush_utf8(t);
  }
  if (b == 0x1B) {
    t->state = ST_ESC;
  } else if (b == '\n') {
    newline(t);
  } else if (b == '\r') {
    t->pending_cr = 1;
  } else if (b == '\b') {
    backspace(t);
  } else if (b == '\t') {
    const int cols = last_line(t)->cols;
    const int stop = (cols / TAB + 1) * TAB;
    int i;
    if (t->pending_cr) {
      line_reset(last_line(t));
      t->pending_cr = 0;
    }
    for (i = last_line(t)->cols; i < stop; ++i) append(t, " ", 1, &t->cur, 1);
  } else if (b < 0x20 || b == 0x7F) {
    /* BEL and the other controls: nothing to show */
  } else if (b >= 0xC0 && b < 0xF8) {
    t->utf8[0] = b;
    t->utf8_n = 1;
    t->utf8_need = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
  } else {
    char c = (char)b;
    put_char(t, &c, 1);
  }
}

void rolltui_ansi_feed(RolltuiAnsiText* t, const char* bytes, size_t len) {
  size_t i;
  if (!t || !bytes) return;
  for (i = 0; i < len; ++i) {
    const unsigned char b = (unsigned char)bytes[i];
    switch (t->state) {
      case ST_GROUND: ground(t, b); break;
      case ST_ESC:
        if (b == '[') {
          t->state = ST_CSI;
          t->seq_n = 0;
          t->seq_over = 0;
        } else if (b == ']' || b == 'P' || b == '_' || b == '^' || b == 'X') {
          t->state = ST_STRING;
        } else if (b == '(' || b == ')' || b == '*' || b == '+' || b == '#' || b == '%') {
          t->state = ST_ESC_SKIP; /* a designation: one more byte names the set */
        } else {
          t->state = ST_GROUND; /* any other two-byte escape: swallowed */
        }
        break;
      case ST_ESC_SKIP: t->state = ST_GROUND; break;
      case ST_CSI:
        if (b >= 0x40 && b <= 0x7E) {
          csi(t, (char)b);
          t->state = ST_GROUND;
        } else if (b == 0x1B) {
          t->state = ST_ESC; /* an escape inside a sequence abandons it */
        } else if (b >= 0x20 && b < 0x40) {
          if (t->seq_n < SEQ_MAX) t->seq[t->seq_n++] = (char)b;
          else t->seq_over = 1;
        } else if (b == '\n') {
          newline(t);
          t->state = ST_GROUND;
        }
        break;
      case ST_STRING:
        if (b == 0x07) t->state = ST_GROUND;
        else if (b == 0x1B) t->state = ST_STRING_ESC;
        break;
      case ST_STRING_ESC:
        if (b == '\\') t->state = ST_GROUND;
        else if (b == 0x1B) t->state = ST_STRING_ESC;
        else t->state = ST_STRING; /* an ESC that is not ST is part of the string */
        break;
      default: t->state = ST_GROUND; break;
    }
  }
}

/* ---- drawing ------------------------------------------------------------------------------- */

/* A run's style over the base: its colours where it has them, its attributes added to the base's. */
static RolltuiStyle over_base(const RolltuiStyle* run, const RolltuiStyle* base) {
  RolltuiStyle s = *base;
  if (run->fg.kind) s.fg = run->fg;
  if (run->bg.kind) s.bg = run->bg;
  s.bold |= run->bold;
  s.dim |= run->dim;
  s.italic |= run->italic;
  s.underline |= run->underline;
  if (run->reverse) { /* reversed: the colours trade places, the base's standing in for one the run did not give */
    const RolltuiStyleColorRaw fg = s.fg;
    s.fg = s.bg;
    s.bg = fg;
  }
  return s;
}

static size_t graphemes(RolltuiAnsiText* t, const AnsiLine* l, int aw) {
  t->g = (RolltuiUnicodeGrapheme*)rolltui_grow(t->g, &t->g_cap, l->text.n ? l->text.n : 1, sizeof *t->g);
  return rolltui_u_graphemes(t->u, l->text.p ? l->text.p : "", l->text.n, aw, t->g);
}

/* How many rows one line takes at `width`, wrapped at the edge. */
static size_t line_rows(RolltuiAnsiText* t, const AnsiLine* l, int width, int aw) {
  const size_t gn = graphemes(t, l, aw);
  size_t i, rows = 1;
  int col = 0;
  for (i = 0; i < gn; ++i) {
    const int w = t->g[i].width;
    if (w <= 0) continue;
    if (col + w > width && col > 0) {
      ++rows;
      col = 0;
    }
    col += w;
  }
  return rows;
}

size_t rolltui_ansi_rows(const RolltuiAnsiText* t, int width, int ambiguous_wide) {
  RolltuiAnsiText* m = (RolltuiAnsiText*)t;
  size_t i, n, rows = 0;
  if (!t || width <= 0) return 0;
  n = rolltui_ansi_line_count(t);
  for (i = 0; i < n; ++i) rows += line_rows(m, line_at(t, i), width, ambiguous_wide);
  return rows;
}

int rolltui_ansi_width(const RolltuiAnsiText* t) {
  size_t i, n;
  int w = 0;
  if (!t) return 0;
  n = rolltui_ansi_line_count(t);
  for (i = 0; i < n; ++i)
    if (line_at(t, i)->cols > w) w = line_at(t, i)->cols;
  return w;
}

int rolltui_ansi_draw(const RolltuiAnsiText* t, RolltuiFrame* f, RolltuiRect area, size_t first, int left, int wrap,
                      RolltuiStyle base, int ambiguous_wide) {
  RolltuiAnsiText* m = (RolltuiAnsiText*)t;
  size_t li, n;
  int y = area.y, skip_rows = 0;
  if (!t || !f || area.w <= 0 || area.h <= 0) return 0;
  n = rolltui_ansi_line_count(t);
  /* `first` counts ROWS when wrapping (a wrapped line is several), LINES when clipping */
  li = 0;
  if (wrap) {
    size_t rows = 0;
    while (li < n) {
      const size_t r = line_rows(m, line_at(t, li), area.w, ambiguous_wide);
      if (rows + r > first) {
        skip_rows = (int)(first - rows);
        break;
      }
      rows += r;
      ++li;
    }
  } else {
    li = first;
  }
  for (; li < n && y < area.y + area.h; ++li) {
    const AnsiLine* l = line_at(t, li);
    const size_t gn = graphemes(m, l, ambiguous_wide);
    size_t gi, ri = 0;
    int col = 0, row = 0;
    for (gi = 0; gi < gn; ++gi) {
      const RolltuiUnicodeGrapheme* g = &m->g[gi];
      RolltuiStyle st;
      int x;
      if (g->width <= 0) continue;
      while (ri + 1 < l->run_n && l->runs[ri + 1].begin <= g->offset) ++ri;
      st = l->run_n ? over_base(&l->runs[ri].style, &base) : base;
      if (wrap) {
        if (col + g->width > area.w && col > 0) {
          ++row;
          col = 0;
        }
        if (row >= skip_rows && y + row - skip_rows < area.y + area.h)
          rolltui_frame_put(f, area.x + col, y + row - skip_rows, l->text.p + g->offset, g->length, g->width, st, 0);
        col += g->width;
      } else {
        x = col - left;
        if (x >= 0 && x + g->width <= area.w) rolltui_frame_put(f, area.x + x, y, l->text.p + g->offset, g->length, g->width, st, 0);
        col += g->width;
        if (x >= area.w) break;
      }
    }
    y += wrap ? row + 1 - skip_rows : 1;
    skip_rows = 0;
  }
  return y - area.y;
}
