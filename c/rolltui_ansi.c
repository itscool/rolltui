/* rolltui/c/rolltui_ansi.c — ANOTHER PROGRAM'S COLOURED OUTPUT, read as styled lines.
 *
 * What a program writes to a terminal is read as a stream: text, newlines, carriage returns, backspace overstrike, tabs, SGR and
 * OSC 8's hyperlinks. Everything else a terminal would act on (cursor moves, erase-in-display, every other OSC, DCS, APC, PM, SOS,
 * charset designations) is swallowed, so a previewer's output, `ls --color`, `git log --color` or a man page reads as what it says,
 * never as commands to this screen. A sequence or a UTF-8 character split across two feeds is joined.
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
  size_t link; /* 0: none; else 1 + where its URL starts in the line's `links` */
} AnsiRun;

typedef struct AnsiLine {
  RolltuiStr text;
  AnsiRun* runs;
  size_t run_n, run_cap;
  int cols; /* the columns written so far, for tab stops */
  /* THE ROWS IT WRAPS TO, at `rows_w` cells and `rows_aw`, kept until its text changes: a wrapped view of a long log counts each line
   * once, not every line on every frame. `rows_w` 0: not counted. */
  int rows_w;
  unsigned char rows_aw;
  unsigned char non_ascii; /* a byte at or above 0x80 was written: its rows need the grapheme walk */
  size_t rows;
  unsigned long long at_ms; /* when its first character came (`rolltui_ansi_set_stamp`) */
  unsigned char tag;        /* where from: the host's number for the source */
  unsigned char rule;       /* `rolltui_ansi_mark`'s rule; `text` is its label */
  RolltuiStr links;         /* the URLs its runs link to, a NUL after each */
  size_t last_link;         /* the one added last, as a run's `link` */
} AnsiLine;

enum { ST_GROUND = 0, ST_ESC, ST_ESC_SKIP, ST_CSI, ST_STRING, ST_STRING_ESC };

#define SEQ_MAX 96
#define OSC_MAX 4096 /* an OSC longer than this is no link: a URL is a few hundred bytes */
#define TAB 8

struct RolltuiAnsiText {
  int tab; /* the columns between tab stops (`rolltui_ansi_set_tab_width`); 0: TAB */
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
  unsigned long long stamp_ms; /* what lines begun now are stamped with */
  unsigned char stamp_tag;
  unsigned long long dropped; /* lines the ring (or a clear) let go of: line i is number `dropped + i` */
  RolltuiStr osc;             /* an OSC's text as it comes */
  unsigned char osc_on, osc_over;
  RolltuiStr link;            /* the hyperlink being written (OSC 8): "" for none */
};

static int imax(int a, int b) { return a > b ? a : b; }

/* ---- the ring ------------------------------------------------------------------------------ */

static AnsiLine* line_at(const RolltuiAnsiText* t, size_t i) { return &t->lines[(t->head + i) % t->cap]; }
static AnsiLine* last_line(RolltuiAnsiText* t) { return line_at(t, t->n - 1); }

static void line_reset(AnsiLine* l) {
  rolltui_str_clear(&l->text);
  rolltui_str_clear(&l->links);
  l->last_link = 0;
  l->run_n = 0;
  l->cols = 0;
  l->rows_w = 0;
  l->non_ascii = 0;
  l->at_ms = 0;
  l->tag = 0;
  l->rule = 0;
}

/* The slots the ring may have: `limit` finished lines and the one being written. 0: no cap. */
static size_t slots(const RolltuiAnsiText* t) { return t->limit ? t->limit + 1 : 0; }

/* A new line at the end: a fresh slot, or the oldest one's buffers once the ring holds all it may. */
static void push_line(RolltuiAnsiText* t) {
  const size_t max = slots(t);
  if (max && t->n >= max) { /* full: the oldest leaves the window and the slot after the newest is the new line */
    line_reset(&t->lines[(t->head + t->n) % t->cap]);
    t->head = (t->head + 1) % t->cap;
    ++t->dropped;
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

static void sgr(RolltuiAnsiText* t, const char* p, size_t n);
int rolltui_style_from_sgr(const char* params, size_t n, RolltuiStyle* out) {
  RolltuiAnsiText* t;
  size_t i;
  if (!params || !out) return 0;
  for (i = 0; i < n; ++i) { /* numbers, and ; or : between them; each number 0 to 255 */
    const char c = params[i];
    if (c == ';' || c == ':') continue;
    if (c < '0' || c > '9') return 0;
    {
      unsigned v = 0;
      while (i < n && params[i] >= '0' && params[i] <= '9') v = v * 10 + (unsigned)(params[i++] - '0');
      --i;
      if (v > 255) return 0;
    }
  }
  t = rolltui_ansi_new();
  sgr(t, params, n);
  *out = t->cur;
  rolltui_ansi_free(t);
  return 1;
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
    rolltui_str_free(&t->lines[i].links);
    rolltui_mem_free(t->lines[i].runs);
  }
  rolltui_str_free(&t->osc);
  rolltui_str_free(&t->link);
  rolltui_mem_free(t->lines);
  rolltui_mem_free(t->g);
  rolltui_u_scratch_free(t->u);
  rolltui_mem_free(t);
}

void rolltui_ansi_clear(RolltuiAnsiText* t) {
  if (!t) return;
  t->dropped += t->n; /* numbers are never given twice */
  t->n = 0;
  t->head = 0;
  t->state = ST_GROUND;
  t->seq_n = 0;
  t->utf8_n = t->utf8_need = 0;
  memset(&t->cur, 0, sizeof t->cur);
  rolltui_str_clear(&t->link);
  t->osc_on = 0;
  t->pending_cr = 0;
  t->over_pending = 0;
  push_line(t);
}

void rolltui_ansi_set_tab_width(RolltuiAnsiText* t, int columns) {
  if (t) t->tab = columns < 1 ? 1 : columns > 16 ? 16 : columns;
}

void rolltui_ansi_set_limit(RolltuiAnsiText* t, size_t max_lines) {
  if (!t) return;
  t->limit = max_lines;
  while (slots(t) && t->n > slots(t)) { /* the oldest go */
    line_reset(&t->lines[t->head]);
    t->head = (t->head + 1) % t->cap;
    --t->n;
    ++t->dropped;
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

/* A run's URL: "" for none. A BORROW into the line. */
static const char* run_url(const AnsiLine* l, const AnsiRun* r, size_t* n) {
  const char* u = r->link ? l->links.p + r->link - 1 : "";
  *n = strlen(u);
  return u;
}
static int links_to(const AnsiLine* l, const AnsiRun* r, const RolltuiStr* link) {
  size_t n;
  const char* u = run_url(l, r, &n);
  return n == link->n && (n == 0 || memcmp(u, link->p, n) == 0);
}
/* The hyperlink being written, in the line's table, as a run's `link`: the last one added when it is the same. */
static size_t line_link(AnsiLine* l, const RolltuiStr* link) {
  if (!link->n) return 0;
  if (l->last_link && strlen(l->links.p + l->last_link - 1) == link->n && memcmp(l->links.p + l->last_link - 1, link->p, link->n) == 0)
    return l->last_link;
  l->last_link = l->links.n + 1;
  rolltui_str_append(&l->links, link->p, link->n);
  rolltui_str_append(&l->links, "", 1);
  return l->last_link;
}

static void append(RolltuiAnsiText* t, const char* bytes, size_t n, const RolltuiStyle* st, int cols) {
  AnsiLine* l = last_line(t);
  if (l->text.n == 0) { /* its first character: when and from where the line came */
    l->at_ms = t->stamp_ms;
    l->tag = t->stamp_tag;
  }
  if (l->run_n == 0 || !style_eq(&l->runs[l->run_n - 1].style, st) || !links_to(l, &l->runs[l->run_n - 1], &t->link)) {
    const size_t link = line_link(l, &t->link);
    if (l->run_n && l->runs[l->run_n - 1].begin == l->text.n) { /* a style that never got a byte is replaced */
      l->runs[l->run_n - 1].style = *st;
      l->runs[l->run_n - 1].link = link;
    } else {
      l->runs = (AnsiRun*)rolltui_grow(l->runs, &l->run_cap, l->run_n + 1, sizeof *l->runs);
      l->runs[l->run_n].begin = l->text.n;
      l->runs[l->run_n].style = *st;
      l->runs[l->run_n].link = link;
      ++l->run_n;
    }
  }
  rolltui_str_append(&l->text, bytes, n);
  l->cols += cols;
  l->rows_w = 0;
  if (n && (unsigned char)bytes[0] >= 0x80) l->non_ascii = 1; /* a character is appended whole: its first byte says */
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
  l->rows_w = 0;
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
      target = x == 38 ? &t->cur.fg : x == 48 ? &t->cur.bg : &t->cur.underline_color;
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
      case 4: /* 4, or 4:N with its shape: 4:0 none, 4:1 to 4:5 single, double, curly, dotted, dashed */
        if (i + 1 < k && sub[i + 1]) t->cur.underline = (unsigned char)(v[i + 1] > ROLLTUI_UNDERLINE_DASHED ? 1 : v[i + 1]);
        else t->cur.underline = 1;
        break;
      case 5: t->cur.blink = 1; break;
      case 6: t->cur.blink = 2; break;
      case 7: t->cur.reverse = 1; break;
      case 8: t->cur.hidden = 1; break;
      case 9: t->cur.strike = 1; break;
      case 21: t->cur.underline = ROLLTUI_UNDERLINE_DOUBLE; break;
      case 25: t->cur.blink = 0; break;
      case 28: t->cur.hidden = 0; break;
      case 53: t->cur.overline = 1; break;
      case 55: t->cur.overline = 0; break;
      case 59: memset(&t->cur.underline_color, 0, sizeof t->cur.underline_color); break;
      case 22: t->cur.bold = 0; t->cur.dim = 0; break;
      case 23: t->cur.italic = 0; break;
      case 24: t->cur.underline = 0; break;
      case 27: t->cur.reverse = 0; break;
      case 29: t->cur.strike = 0; break;
      case 39: memset(&t->cur.fg, 0, sizeof t->cur.fg); break;
      case 49: memset(&t->cur.bg, 0, sizeof t->cur.bg); break;
      default:
        if (x >= 30 && x <= 37) colour_indexed(&t->cur.fg, x - 30);
        else if (x >= 40 && x <= 47) colour_indexed(&t->cur.bg, x - 40);
        else if (x >= 90 && x <= 97) colour_indexed(&t->cur.fg, x - 90 + 8);
        else if (x >= 100 && x <= 107) colour_indexed(&t->cur.bg, x - 100 + 8);
        break; /* fonts, framed, encircled: read and dropped */
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

/* ---- OSC 8 ----------------------------------------------------------------------------------- */

/* AN OSC ENDED: `8;params;URL` opens a hyperlink over what follows, and an empty URL closes it, as a terminal reads it. A URL is
 * printable ASCII or it is none. Every other OSC (a title, a colour, the clipboard) is the terminal's own and is swallowed. */
static void osc_end(RolltuiAnsiText* t) {
  const char* p = t->osc.p;
  const size_t n = t->osc.n;
  size_t i = 2, k;
  t->osc_on = 0;
  if (n < 2 || p[0] != '8' || p[1] != ';') return;
  if (t->osc_over) { /* too long to be a URL: what follows is no link */
    rolltui_str_clear(&t->link);
    return;
  }
  while (i < n && p[i] != ';') ++i; /* the parameters: `id=` and the like */
  if (i >= n) return;
  ++i;
  for (k = i; k < n; ++k)
    if ((unsigned char)p[k] < 0x20 || (unsigned char)p[k] > 0x7E) {
      rolltui_str_clear(&t->link);
      return;
    }
  rolltui_str_set(&t->link, p + i, n - i);
}
static void osc_byte(RolltuiAnsiText* t, unsigned char b) {
  const char c = (char)b;
  if (!t->osc_on || t->osc_over) return;
  if (t->osc.n >= OSC_MAX) {
    t->osc_over = 1;
    return;
  }
  rolltui_str_append(&t->osc, &c, 1);
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
    const int tw = t->tab > 0 ? t->tab : TAB;
    const int stop = (cols / tw + 1) * tw;
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
        } else if (b == ']') { /* OSC: read, for its hyperlinks */
          t->state = ST_STRING;
          t->osc_on = 1;
          t->osc_over = 0;
          rolltui_str_clear(&t->osc);
        } else if (b == 'P' || b == '_' || b == '^' || b == 'X') {
          t->state = ST_STRING;
          t->osc_on = 0;
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
        if (b == 0x07) {
          if (t->osc_on) osc_end(t);
          t->state = ST_GROUND;
        } else if (b == 0x1B) {
          t->state = ST_STRING_ESC;
        } else {
          osc_byte(t, b);
        }
        break;
      case ST_STRING_ESC:
        if (b == '\\') {
          if (t->osc_on) osc_end(t);
          t->state = ST_GROUND;
        } else if (b == 0x1B) {
          osc_byte(t, 0x1B);
        } else { /* an ESC that is not ST is part of the string */
          osc_byte(t, 0x1B);
          osc_byte(t, b);
          t->state = ST_STRING;
        }
        break;
      default: t->state = ST_GROUND; break;
    }
  }
}

/* ---- drawing ------------------------------------------------------------------------------- */

/* Run `ri`'s hyperlink as the frame's id: 0 for none. */
static unsigned int run_link_id(const AnsiLine* l, size_t ri, RolltuiFrame* f) {
  size_t n;
  const char* u;
  if (!l->run_n || !l->runs[ri].link) return 0;
  u = run_url(l, &l->runs[ri], &n);
  return rolltui_frame_link_id(f, u, n);
}

/* A run's style over the base: its colours where it has them, its attributes added to the base's. */
static RolltuiStyle over_base(const RolltuiStyle* run, const RolltuiStyle* base) {
  RolltuiStyle s = *base;
  if (run->fg.kind) s.fg = run->fg;
  if (run->bg.kind) s.bg = run->bg;
  s.bold |= run->bold;
  s.dim |= run->dim;
  s.italic |= run->italic;
  if (run->underline) s.underline = run->underline;
  if (run->underline_color.kind) s.underline_color = run->underline_color;
  s.strike |= run->strike;
  s.blink |= run->blink;
  s.hidden |= run->hidden;
  s.overline |= run->overline;
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
static size_t line_rows(RolltuiAnsiText* t, AnsiLine* l, int width, int aw) {
  size_t gn, i, rows = 1;
  int col = 0;
  if (l->rows_w == width && l->rows_aw == (unsigned char)(aw != 0)) return l->rows;
  if (!l->non_ascii) { /* every character one cell: the rows follow from the width written */
    rows = l->cols > width ? (size_t)((l->cols + width - 1) / width) : 1;
    l->rows_w = width;
    l->rows_aw = (unsigned char)(aw != 0);
    l->rows = rows;
    return rows;
  }
  gn = graphemes(t, l, aw);
  for (i = 0; i < gn; ++i) {
    const int w = t->g[i].width;
    if (w <= 0) continue;
    if (col + w > width && col > 0) {
      ++rows;
      col = 0;
    }
    col += w;
  }
  l->rows_w = width;
  l->rows_aw = (unsigned char)(aw != 0);
  l->rows = rows;
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

/* A RULE ACROSS `w` cells at (x, y): ── label ──────, in `base` dimmed. */
static void draw_rule(RolltuiAnsiText* t, const AnsiLine* l, RolltuiFrame* f, int x, int y, int w, RolltuiStyle base, int aw) {
  static const char kRule[] = "\xe2\x94\x80";
  const char* rule = aw ? "-" : kRule;
  const size_t rule_n = aw ? 1 : sizeof kRule - 1;
  int col = 0, took = 0;
  base.dim = 1;
  if (l->text.n && w > 6) { /* two of the rule, the label, then the rest */
    size_t fit;
    for (; col < 2; ++col) rolltui_frame_put(f, x + col, y, rule, rule_n, 1, base, 0);
    rolltui_frame_put(f, x + col++, y, " ", 1, 1, base, 0);
    fit = rolltui_u_fit(t->u, l->text.p, l->text.n, w - col - 2, aw, &took);
    base.dim = 0;
    base.italic = 1;
    {
      size_t gi = 0;
      int cx = 0;
      while (gi < fit) { /* the label, a character at a time */
        RolltuiDecodedChar d;
        int cw;
        rolltui_u_decode_one(l->text.p, fit, gi, &d);
        cw = imax(rolltui_u_codepoint_width(d.valid ? d.cp : 0xFFFD, aw), 1);
        rolltui_frame_put(f, x + col + cx, y, l->text.p + gi, d.length ? d.length : 1, cw, base, 0);
        cx += cw;
        gi += d.length ? d.length : 1;
      }
    }
    col += took;
    base.italic = 0;
    base.dim = 1;
    rolltui_frame_put(f, x + col++, y, " ", 1, 1, base, 0);
  }
  for (; col < w; ++col) rolltui_frame_put(f, x + col, y, rule, rule_n, 1, base, 0);
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
    size_t gn;
    if (l->rule) {
      draw_rule(m, l, f, area.x, y, area.w, base, ambiguous_wide);
      ++y;
      skip_rows = 0;
      continue;
    }
    gn = graphemes(m, l, ambiguous_wide);
    size_t gi, ri = 0, link_ri = (size_t)-1;
    unsigned int link = 0;
    int col = 0, row = 0;
    for (gi = 0; gi < gn; ++gi) {
      const RolltuiUnicodeGrapheme* g = &m->g[gi];
      RolltuiStyle st;
      int x;
      if (g->width <= 0) continue;
      while (ri + 1 < l->run_n && l->runs[ri + 1].begin <= g->offset) ++ri;
      st = l->run_n ? over_base(&l->runs[ri].style, &base) : base;
      if (ri != link_ri) link = run_link_id(l, link_ri = ri, f);
      if (wrap) {
        if (col + g->width > area.w && col > 0) {
          ++row;
          col = 0;
        }
        if (row >= skip_rows && y + row - skip_rows < area.y + area.h)
          rolltui_frame_put(f, area.x + col, y + row - skip_rows, l->text.p + g->offset, g->length, g->width, st, link);
        col += g->width;
      } else {
        x = col - left;
        if (x >= 0 && x + g->width <= area.w) rolltui_frame_put(f, area.x + x, y, l->text.p + g->offset, g->length, g->width, st, link);
        col += g->width;
        if (x >= area.w) break;
      }
    }
    y += wrap ? row + 1 - skip_rows : 1;
    skip_rows = 0;
  }
  return y - area.y;
}

void rolltui_ansi_set_stamp(RolltuiAnsiText* t, unsigned long long at_ms, unsigned char tag) {
  if (!t) return;
  t->stamp_ms = at_ms;
  t->stamp_tag = tag;
}

void rolltui_ansi_mark(RolltuiAnsiText* t, const char* label, size_t len) {
  AnsiLine* l;
  if (!t) return;
  if (last_line(t)->text.n) newline(t); /* the line being written ends first */
  l = last_line(t);
  line_reset(l);
  l->rule = 1;
  l->at_ms = t->stamp_ms;
  l->tag = t->stamp_tag;
  if (label && len) rolltui_str_set(&l->text, label, len);
  t->pending_cr = 0;
  t->over_pending = 0;
  push_line(t); /* what comes next starts after the rule */
}

int rolltui_ansi_line_info(const RolltuiAnsiText* t, size_t i, RolltuiAnsiLineInfo* out) {
  const AnsiLine* l;
  if (!t || !out || i >= t->n) return 0;
  l = line_at(t, i);
  out->at_ms = l->at_ms;
  out->seq = t->dropped + i;
  out->tag = l->tag;
  out->rule = l->rule;
  return 1;
}

size_t rolltui_ansi_line_rows(const RolltuiAnsiText* t, size_t i, int width, int ambiguous_wide) {
  AnsiLine* l;
  if (!t || i >= t->n || width <= 0) return 1;
  l = line_at(t, i);
  return l->rule ? 1 : line_rows((RolltuiAnsiText*)t, l, width, ambiguous_wide);
}

int rolltui_ansi_draw_line(const RolltuiAnsiText* t, size_t i, RolltuiFrame* f, RolltuiRect area, const RolltuiAnsiLineDraw* o) {
  RolltuiAnsiText* m = (RolltuiAnsiText*)t;
  const AnsiLine* l;
  size_t gn, gi, ri = 0, k = 0, link_ri = (size_t)-1;
  unsigned int link = 0;
  int col = 0, row = 0, drawn = 0;
  const int aw = o ? o->ambiguous_wide : 0;
  const int wrap = o ? o->wrap : 1;
  const int skip = o ? o->skip : 0;
  if (!t || !f || !o || i >= t->n || area.w <= 0 || area.h <= 0) return 0;
  l = line_at(t, i);
  if (l->rule) {
    draw_rule(m, l, f, area.x, area.y, area.w, o->base, aw);
    return 1;
  }
  gn = graphemes(m, l, aw);
  for (gi = 0; gi < gn; ++gi) {
    const RolltuiUnicodeGrapheme* g = &m->g[gi];
    RolltuiStyle st;
    if (g->width <= 0) continue;
    while (ri + 1 < l->run_n && l->runs[ri + 1].begin <= g->offset) ++ri;
    st = l->run_n ? over_base(&l->runs[ri].style, &o->base) : o->base;
    if (ri != link_ri) link = run_link_id(l, link_ri = ri, f);
    while (k < o->lit_n && o->lit[2 * k + 1] <= g->offset) ++k; /* the lit ranges are in order */
    if (k < o->lit_n && o->lit[2 * k] <= g->offset && g->offset < o->lit[2 * k + 1]) rolltui_style_patch(&st, &o->lit_style);
    if (wrap) {
      if (col + g->width > area.w && col > 0) {
        ++row;
        col = 0;
      }
      if (row >= skip && row - skip < area.h) {
        rolltui_frame_put(f, area.x + col, area.y + row - skip, l->text.p + g->offset, g->length, g->width, st, link);
        drawn = row - skip + 1;
      }
      col += g->width;
    } else {
      const int x = col - o->left;
      if (x >= 0 && x + g->width <= area.w) rolltui_frame_put(f, area.x + x, area.y, l->text.p + g->offset, g->length, g->width, st, link);
      col += g->width;
      if (x >= area.w) break;
    }
  }
  return wrap ? (drawn ? drawn : (skip == 0 ? 1 : 0)) : 1;
}
