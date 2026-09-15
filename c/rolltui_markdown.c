/* rolltui/c/rolltui_markdown.c — md4c → block tree → styled wrapped lines, in C.
 * Contract in rolltui_markdown.h; every rendering rule is stated in rolltui/Markdown.hpp.
 *
 * md4c is included here and nowhere else on this side of the boundary, the same rule the
 * C++ implementation keeps — and here it is not a wrapper at all: the callbacks below write
 * straight into this file's own arrays, where the C++ ones cast a `void*` and moved bytes
 * out of an `MD_ATTRIBUTE` into a `std::string`.
 */
#include "rolltui/c/rolltui_markdown.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_md_lines.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/third_party/md4c/md4c.h"
#include "testkit/testctl.h"

#define NONE ((size_t)-1)

/* Inline style bits — the same set as `rolltui::markdown::InlineStyle`, which no longer
 * needs to be in a public header because nothing outside a renderer ever read one. */
#define S_PLAIN 0u
#define S_EMPHASIS (1u << 0)
#define S_STRONG (1u << 1)
#define S_CODE (1u << 2)
#define S_LINK (1u << 3)
#define S_STRIKE (1u << 4)
#define S_LINK_URL (1u << 5)
#define S_UNDERLINE (1u << 6)

/* Alignment, per column. */
#define A_DEFAULT 0
#define A_LEFT 1
#define A_CENTER 2
#define A_RIGHT 3

static void die(const char* what) {
  fprintf(stderr, "rolltui: markdown: %s\n", what);
  abort();
}

/* ======================================================================================
 * THE BLOCK TREE
 *
 * Index arrays with a first-child / next-sibling list, rather than the C++ side's nested
 * `std::vector<Block>`. The list is not a stylistic choice: the parser opens a block, fills
 * it, and only then learns what its siblings are, and a contiguous child range cannot be
 * built that way without moving blocks that the open stack is pointing at. The C++ gets
 * away with a nested vector because it holds RAW POINTERS into it and a comment explaining
 * why they stay valid; this holds indices, which cannot go stale at all.
 * ==================================================================================== */

typedef struct Run {
  size_t text_off, text_n; /* into d->bytes */
  size_t href_off, href_n;
  unsigned style;
} Run;

typedef struct Cell {
  size_t run_first, run_n;
} Cell;

typedef struct Row {
  size_t cell_first, cell_n;
} Row;

typedef struct Block {
  unsigned char kind;
  int level;               /* Heading: 1-6 */
  size_t run_first, run_n; /* Paragraph, Heading: into d->runs */
  size_t code_off, code_n; /* Code / Html: verbatim, '\n'-separated, into d->bytes */
  size_t info_off, info_n; /* Code: the fence's info string */
  unsigned char ordered, tight, task, checked, implicit;
  unsigned start;                        /* List (ordered) */
  size_t first_child, last_child, next;  /* the tree */
  size_t align_off, align_n;             /* Table: into d->aligns */
  unsigned head_rows;
  size_t row_first, row_n; /* Table: into d->rows */
} Block;

struct RolltuiMdDoc {
  char* bytes;
  size_t bytes_n, bytes_cap;
  Run* runs;
  size_t runs_n, runs_cap;
  Cell* cells;
  size_t cells_n, cells_cap;
  Row* rows;
  size_t rows_n, rows_cap;
  unsigned char* aligns;
  size_t aligns_n, aligns_cap;
  Block* blocks;
  size_t blocks_n, blocks_cap;
  size_t first_root, last_root, root_count;
};

RolltuiMdDoc* rolltui_md_doc_new(void) {
  RolltuiMdDoc* d = (RolltuiMdDoc*)rolltui_mem_alloc(sizeof *d);
  memset(d, 0, sizeof *d);
  d->first_root = d->last_root = NONE;
  return d;
}

void rolltui_md_doc_free(RolltuiMdDoc* d) {
  if (!d) return;
  rolltui_mem_free(d->bytes);
  rolltui_mem_free(d->runs);
  rolltui_mem_free(d->cells);
  rolltui_mem_free(d->rows);
  rolltui_mem_free(d->aligns);
  rolltui_mem_free(d->blocks);
  rolltui_mem_free(d);
}

static void doc_reset(RolltuiMdDoc* d) {
  d->bytes_n = 0;
  d->runs_n = 0;
  d->cells_n = 0;
  d->rows_n = 0;
  d->aligns_n = 0;
  d->blocks_n = 0;
  d->first_root = d->last_root = NONE;
  d->root_count = 0;
}

static size_t doc_intern(RolltuiMdDoc* d, const char* s, size_t n) {
  const size_t off = d->bytes_n;
  if (n == 0) return off;
  d->bytes = (char*)rolltui_grow(d->bytes, &d->bytes_cap, d->bytes_n + n, 1);
  memcpy(d->bytes + d->bytes_n, s, n);
  d->bytes_n += n;
  return off;
}

/* ---- entities ---------------------------------------------------------------------------- */

typedef struct Named {
  const char* name;
  const char* utf8;
} Named;

static const Named kNamed[] = {
    {"amp", "&"},          {"lt", "<"},           {"gt", ">"},           {"quot", "\""},
    {"apos", "'"},         {"nbsp", "\xC2\xA0"},  {"copy", "\xC2\xA9"},  {"reg", "\xC2\xAE"},
    {"trade", "\xE2\x84\xA2"}, {"hellip", "\xE2\x80\xA6"}, {"mdash", "\xE2\x80\x94"},
    {"ndash", "\xE2\x80\x93"}, {"laquo", "\xC2\xAB"}, {"raquo", "\xC2\xBB"},
    {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"}, {"lsquo", "\xE2\x80\x98"},
    {"rsquo", "\xE2\x80\x99"}, {"bull", "\xE2\x80\xA2"}, {"rarr", "\xE2\x86\x92"},
    {"larr", "\xE2\x86\x90"},  {"times", "\xC3\x97"}, {"deg", "\xC2\xB0"},
};

static size_t copy_out(const char* s, size_t n, char* out, size_t cap) {
  if (n > cap) n = cap;
  if (n) memcpy(out, s, n);
  return n;
}

size_t rolltui_md_decode_entity(const char* ent, size_t n, char* out, size_t cap) {
  size_t i;
  const char* body;
  size_t body_n;
  if (n < 3 || ent[0] != '&' || ent[n - 1] != ';') return copy_out(ent, n, out, cap);
  body = ent + 1;
  body_n = n - 2;
  if (body_n > 0 && body[0] == '#') {
    const char* num = body + 1;
    size_t num_n = body_n - 1;
    int base = 10;
    unsigned long v = 0;
    if (num_n > 0 && (num[0] == 'x' || num[0] == 'X')) {
      base = 16;
      ++num;
      --num_n;
    }
    if (num_n == 0) return copy_out(ent, n, out, cap);
    for (i = 0; i < num_n; ++i) {
      const char c = num[i];
      int digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else return copy_out(ent, n, out, cap);
      v = v * (unsigned long)base + (unsigned long)digit;
      if (v > 0x10FFFF) return copy_out("\xEF\xBF\xBD", 3, out, cap);
    }
    if (v == 0 || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
    if (cap < 4) return copy_out("\xEF\xBF\xBD", 3, out, cap);
    return rolltui_u_append_utf8((RolltuiCodepoint)v, out);
  }
  for (i = 0; i < sizeof kNamed / sizeof kNamed[0]; ++i) {
    const size_t ln = strlen(kNamed[i].name);
    if (ln == body_n && memcmp(body, kNamed[i].name, ln) == 0)
      return copy_out(kNamed[i].utf8, strlen(kNamed[i].utf8), out, cap);
  }
  return copy_out(ent, n, out, cap);
}

unsigned rolltui_md_parser_flags(void) {
  return MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS |
         MD_FLAG_PERMISSIVEWWWAUTOLINKS;
}

/* ---- parsing ----------------------------------------------------------------------------- */

typedef struct Href {
  size_t off, n;
  unsigned char autolink;
} Href;

typedef struct ParseState {
  RolltuiMdDoc* d;
  size_t* stack; /* open blocks, by index */
  size_t stack_n, stack_cap;
  unsigned* spans; /* open inline styles, one bit each */
  size_t spans_n, spans_cap;
  Href* hrefs;
  size_t hrefs_n, hrefs_cap;
  size_t cur_row, cur_cell; /* NONE, or an index */
  char* tmp;                /* one attribute or entity under construction */
  size_t tmp_n, tmp_cap;
} ParseState;

static void tmp_append(ParseState* st, const char* s, size_t n) {
  if (n == 0) return;
  st->tmp = (char*)rolltui_grow(st->tmp, &st->tmp_cap, st->tmp_n + n, 1);
  memcpy(st->tmp + st->tmp_n, s, n);
  st->tmp_n += n;
}

/* An MD_ATTRIBUTE's pieces, entity-decoded, into st->tmp. */
static void attr(ParseState* st, const MD_ATTRIBUTE* a) {
  unsigned i;
  st->tmp_n = 0;
  for (i = 0; a->substr_offsets && a->substr_offsets[i] < a->size; ++i) {
    const char* piece = a->text + a->substr_offsets[i];
    const size_t piece_n = a->substr_offsets[i + 1] - a->substr_offsets[i];
    if (a->substr_types[i] == MD_TEXT_ENTITY) {
      /* Decoded straight into the tail of `tmp`, grown to hold the WORST case — an unknown
       * entity is copied verbatim, so that is the input's own length, and a decoded scalar
       * is at most four bytes. Nothing is capped and nothing needs a size rule. */
      const size_t room = piece_n > 4 ? piece_n : 4;
      st->tmp = (char*)rolltui_grow(st->tmp, &st->tmp_cap, st->tmp_n + room, 1);
      st->tmp_n += rolltui_md_decode_entity(piece, piece_n, st->tmp + st->tmp_n, room);
    } else if (a->substr_types[i] == MD_TEXT_NULLCHAR) {
      tmp_append(st, "\xEF\xBF\xBD", 3);
    } else {
      tmp_append(st, piece, piece_n);
    }
  }
}

static unsigned style_of(const ParseState* st) {
  unsigned s = S_PLAIN;
  size_t i;
  for (i = 0; i < st->spans_n; ++i) s |= st->spans[i];
  return s;
}

static size_t top(const ParseState* st) { return st->stack_n ? st->stack[st->stack_n - 1] : NONE; }

static int is_container(unsigned char kind) {
  return kind == ROLLTUI_MD_BLOCK_QUOTE || kind == ROLLTUI_MD_BLOCK_ITEM || kind == ROLLTUI_MD_BLOCK_LIST;
}

static size_t new_block(ParseState* st, unsigned char kind) {
  RolltuiMdDoc* d = st->d;
  Block* b;
  const size_t i = d->blocks_n;
  d->blocks = (Block*)rolltui_grow(d->blocks, &d->blocks_cap, d->blocks_n + 1, sizeof *d->blocks);
  ++d->blocks_n;
  b = &d->blocks[i];
  memset(b, 0, sizeof *b);
  b->kind = kind;
  b->tight = 1;
  b->start = 1;
  b->run_first = d->runs_n;
  b->code_off = d->bytes_n;
  b->info_off = d->bytes_n;
  b->align_off = d->aligns_n;
  b->row_first = d->rows_n;
  b->first_child = b->last_child = b->next = NONE;
  return i;
}

/* Links the new block under the open one (or at the root) and opens it. */
static size_t push_child(ParseState* st, size_t i) {
  RolltuiMdDoc* d = st->d;
  const size_t parent = top(st);
  if (parent == NONE) {
    if (d->last_root == NONE) d->first_root = i;
    else d->blocks[d->last_root].next = i;
    d->last_root = i;
    ++d->root_count;
  } else {
    Block* p = &d->blocks[parent];
    if (p->last_child == NONE) p->first_child = i;
    else d->blocks[p->last_child].next = i;
    p->last_child = i;
  }
  st->stack = (size_t*)rolltui_grow(st->stack, &st->stack_cap, st->stack_n + 1, sizeof *st->stack);
  st->stack[st->stack_n++] = i;
  return i;
}

static void pop_implicit(ParseState* st) {
  const size_t t = top(st);
  if (t != NONE && st->d->blocks[t].implicit) --st->stack_n;
}

/* Where text goes: a table cell, the open paragraph/heading, or a synthesised paragraph
 * when md4c hands text straight to a container (tight list items). */
static void append_run(ParseState* st, const char* text, size_t text_n, unsigned style, size_t href_off,
                       size_t href_n) {
  RolltuiMdDoc* d = st->d;
  size_t* run_first;
  size_t* run_n;
  Run* prev;
  if (text_n == 0) return;
  if (st->cur_cell != NONE) {
    run_first = &d->cells[st->cur_cell].run_first;
    run_n = &d->cells[st->cur_cell].run_n;
  } else {
    size_t t = top(st);
    if (t == NONE || is_container(d->blocks[t].kind)) {
      t = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_PARAGRAPH));
      d->blocks[t].implicit = 1;
    }
    run_first = &d->blocks[t].run_first;
    run_n = &d->blocks[t].run_n;
  }
  /* A run list is CONTIGUOUS AND AT THE TAIL — asserted rather than assumed, because it is
   * a property of md4c's callback order (a block never opens inside another's inlines) and
   * not of anything in this file. If it ever stopped being true, the alternative is a run
   * list per block, and the abort is what would say so. */
  if (*run_n != 0 && *run_first + *run_n != d->runs_n) die("a block's runs stopped being contiguous");
  if (*run_n == 0) *run_first = d->runs_n;
  prev = *run_n ? &d->runs[d->runs_n - 1] : NULL;
  if (prev && prev->style == style && prev->href_n == href_n &&
      (href_n == 0 || memcmp(d->bytes + prev->href_off, d->bytes + href_off, href_n) == 0) &&
      prev->text_off + prev->text_n == d->bytes_n) {
    doc_intern(d, text, text_n);
    prev->text_n += text_n;
    return;
  }
  {
    Run* r;
    /* THE HREF IS NOT RE-INTERNED. It is already in `d->bytes`, put there once by
     * `enter_span`, so the run points at it — where the C++ copied a `std::string` per run.
     * That also keeps the TEXT at the tail of the pool, which is what lets the next run
     * merge into this one (the same ordering rule as the span store's, and it is what a
     * re-intern from `d->bytes` would break — the source would be inside the buffer the
     * copy is growing). */
    const size_t to = doc_intern(d, text, text_n);
    d->runs = (Run*)rolltui_grow(d->runs, &d->runs_cap, d->runs_n + 1, sizeof *d->runs);
    r = &d->runs[d->runs_n++];
    r->text_off = to;
    r->text_n = text_n;
    r->href_off = href_off;
    r->href_n = href_n;
    r->style = style;
    ++*run_n;
  }
}

static int enter_block(MD_BLOCKTYPE type, void* det, void* ud) {
  ParseState* st = (ParseState*)ud;
  RolltuiMdDoc* d = st->d;
  size_t i;
  pop_implicit(st);
  switch (type) {
    case MD_BLOCK_DOC:
      return 0;
    case MD_BLOCK_QUOTE:
      push_child(st, new_block(st, ROLLTUI_MD_BLOCK_QUOTE));
      return 0;
    case MD_BLOCK_UL: {
      const MD_BLOCK_UL_DETAIL* u = (const MD_BLOCK_UL_DETAIL*)det;
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_LIST));
      d->blocks[i].ordered = 0;
      d->blocks[i].tight = u->is_tight != 0;
      return 0;
    }
    case MD_BLOCK_OL: {
      const MD_BLOCK_OL_DETAIL* o = (const MD_BLOCK_OL_DETAIL*)det;
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_LIST));
      d->blocks[i].ordered = 1;
      d->blocks[i].start = o->start;
      d->blocks[i].tight = o->is_tight != 0;
      return 0;
    }
    case MD_BLOCK_LI: {
      const MD_BLOCK_LI_DETAIL* l = (const MD_BLOCK_LI_DETAIL*)det;
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_ITEM));
      d->blocks[i].task = l->is_task != 0;
      d->blocks[i].checked = (unsigned char)(d->blocks[i].task && (l->task_mark == 'x' || l->task_mark == 'X'));
      return 0;
    }
    case MD_BLOCK_HR:
      push_child(st, new_block(st, ROLLTUI_MD_BLOCK_RULE));
      return 0;
    case MD_BLOCK_H:
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_HEADING));
      d->blocks[i].level = (int)((const MD_BLOCK_H_DETAIL*)det)->level;
      return 0;
    case MD_BLOCK_CODE: {
      const MD_BLOCK_CODE_DETAIL* c = (const MD_BLOCK_CODE_DETAIL*)det;
      attr(st, &c->info);
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_CODE));
      d->blocks[i].info_off = doc_intern(d, st->tmp, st->tmp_n);
      d->blocks[i].info_n = st->tmp_n;
      d->blocks[i].code_off = d->bytes_n;
      return 0;
    }
    case MD_BLOCK_HTML:
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_HTML));
      d->blocks[i].code_off = d->bytes_n;
      return 0;
    case MD_BLOCK_P:
      push_child(st, new_block(st, ROLLTUI_MD_BLOCK_PARAGRAPH));
      return 0;
    case MD_BLOCK_TABLE: {
      const MD_BLOCK_TABLE_DETAIL* t = (const MD_BLOCK_TABLE_DETAIL*)det;
      unsigned c;
      i = push_child(st, new_block(st, ROLLTUI_MD_BLOCK_TABLE));
      d->blocks[i].head_rows = t->head_row_count;
      d->blocks[i].align_off = d->aligns_n;
      d->blocks[i].align_n = t->col_count;
      d->aligns = (unsigned char*)rolltui_grow(d->aligns, &d->aligns_cap, d->aligns_n + t->col_count, 1);
      for (c = 0; c < t->col_count; ++c) d->aligns[d->aligns_n + c] = A_DEFAULT;
      d->aligns_n += t->col_count;
      d->blocks[i].row_first = d->rows_n;
      return 0;
    }
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
      return 0;
    case MD_BLOCK_TR: {
      Block* t = &d->blocks[top(st)];
      d->rows = (Row*)rolltui_grow(d->rows, &d->rows_cap, d->rows_n + 1, sizeof *d->rows);
      d->rows[d->rows_n].cell_first = d->cells_n;
      d->rows[d->rows_n].cell_n = 0;
      st->cur_row = d->rows_n++;
      ++t->row_n;
      return 0;
    }
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
      const MD_BLOCK_TD_DETAIL* td = (const MD_BLOCK_TD_DETAIL*)det;
      Block* t = &d->blocks[top(st)];
      size_t col;
      if (st->cur_row == NONE) {
        d->rows = (Row*)rolltui_grow(d->rows, &d->rows_cap, d->rows_n + 1, sizeof *d->rows);
        d->rows[d->rows_n].cell_first = d->cells_n;
        d->rows[d->rows_n].cell_n = 0;
        st->cur_row = d->rows_n++;
        ++t->row_n;
      }
      d->cells = (Cell*)rolltui_grow(d->cells, &d->cells_cap, d->cells_n + 1, sizeof *d->cells);
      d->cells[d->cells_n].run_first = d->runs_n;
      d->cells[d->cells_n].run_n = 0;
      st->cur_cell = d->cells_n++;
      col = d->rows[st->cur_row].cell_n++;
      if (type == MD_BLOCK_TH && col < t->align_n) {
        switch (td->align) {
          case MD_ALIGN_LEFT: d->aligns[t->align_off + col] = A_LEFT; break;
          case MD_ALIGN_CENTER: d->aligns[t->align_off + col] = A_CENTER; break;
          case MD_ALIGN_RIGHT: d->aligns[t->align_off + col] = A_RIGHT; break;
          default: break;
        }
      }
      return 0;
    }
    default:
      /* A block kind this renderer does not know (footnotes, admonitions — not enabled) —
       * treated as a quote so its text is never dropped. */
      push_child(st, new_block(st, ROLLTUI_MD_BLOCK_QUOTE));
      return 0;
  }
}

static int leave_block(MD_BLOCKTYPE type, void* det, void* ud) {
  ParseState* st = (ParseState*)ud;
  (void)det;
  switch (type) {
    case MD_BLOCK_DOC:
      pop_implicit(st);
      return 0;
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
      return 0;
    case MD_BLOCK_TR:
      st->cur_row = NONE;
      return 0;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
      st->cur_cell = NONE;
      return 0;
    default:
      break;
  }
  pop_implicit(st);
  if (st->stack_n) --st->stack_n;
  return 0;
}

static void push_style(ParseState* st, unsigned bit) {
  st->spans = (unsigned*)rolltui_grow(st->spans, &st->spans_cap, st->spans_n + 1, sizeof *st->spans);
  st->spans[st->spans_n++] = bit;
}

static void push_href(ParseState* st, size_t off, size_t n, int autolink) {
  st->hrefs = (Href*)rolltui_grow(st->hrefs, &st->hrefs_cap, st->hrefs_n + 1, sizeof *st->hrefs);
  st->hrefs[st->hrefs_n].off = off;
  st->hrefs[st->hrefs_n].n = n;
  st->hrefs[st->hrefs_n].autolink = (unsigned char)(autolink != 0);
  ++st->hrefs_n;
}

static int enter_span(MD_SPANTYPE type, void* det, void* ud) {
  ParseState* st = (ParseState*)ud;
  switch (type) {
    case MD_SPAN_EM: push_style(st, S_EMPHASIS); break;
    case MD_SPAN_STRONG: push_style(st, S_STRONG); break;
    case MD_SPAN_CODE: push_style(st, S_CODE); break;
    case MD_SPAN_DEL: push_style(st, S_STRIKE); break;
    case MD_SPAN_U: push_style(st, S_UNDERLINE); break;
    case MD_SPAN_A: {
      const MD_SPAN_A_DETAIL* a = (const MD_SPAN_A_DETAIL*)det;
      attr(st, &a->href);
      push_style(st, S_LINK);
      push_href(st, doc_intern(st->d, st->tmp, st->tmp_n), st->tmp_n, a->is_autolink != 0);
      break;
    }
    case MD_SPAN_IMG: {
      const MD_SPAN_IMG_DETAIL* im = (const MD_SPAN_IMG_DETAIL*)det;
      attr(st, &im->src);
      push_style(st, S_LINK);
      push_href(st, doc_intern(st->d, st->tmp, st->tmp_n), st->tmp_n, 0);
      break;
    }
    default:
      push_style(st, S_PLAIN); /* math, wikilinks, …: text passes through */
      break;
  }
  return 0;
}

static int leave_span(MD_SPANTYPE type, void* det, void* ud) {
  ParseState* st = (ParseState*)ud;
  (void)det;
  if (type == MD_SPAN_A || type == MD_SPAN_IMG) {
    size_t href_off = 0, href_n = 0;
    int autolink = 0;
    if (st->hrefs_n) {
      href_off = st->hrefs[st->hrefs_n - 1].off;
      href_n = st->hrefs[st->hrefs_n - 1].n;
      autolink = st->hrefs[st->hrefs_n - 1].autolink;
      --st->hrefs_n;
    }
    if (st->spans_n) --st->spans_n;
    /* ON = the " (url)" run is never appended, which is the defect state: a link whose text
     * differs from its target renders as the text alone, so the destination is invisible in a
     * terminal that cannot be hovered. The document is still well formed and nothing reports
     * anything. */
    if (!autolink && href_n && !testkit_ctl_on("md.link_url_is_dropped")) {
      st->tmp_n = 0;
      tmp_append(st, " (", 2);
      tmp_append(st, st->d->bytes + href_off, href_n);
      tmp_append(st, ")", 1);
      append_run(st, st->tmp, st->tmp_n, style_of(st) | S_LINK_URL, 0, 0);
    }
    return 0;
  }
  if (st->spans_n) --st->spans_n;
  return 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR* txt, MD_SIZE size, void* ud) {
  ParseState* st = (ParseState*)ud;
  RolltuiMdDoc* d = st->d;
  const size_t t = top(st);
  const int verbatim = t != NONE && st->cur_cell == NONE &&
                       (d->blocks[t].kind == ROLLTUI_MD_BLOCK_CODE || d->blocks[t].kind == ROLLTUI_MD_BLOCK_HTML);
  size_t href_off = 0, href_n = 0;
  if (verbatim) {
    Block* b = &d->blocks[t];
    if (b->code_n == 0) b->code_off = d->bytes_n;
    if (type == MD_TEXT_NULLCHAR) {
      doc_intern(d, "\xEF\xBF\xBD", 3);
      b->code_n += 3;
    } else {
      doc_intern(d, txt, size);
      b->code_n += size;
    }
    return 0;
  }
  if (st->hrefs_n) {
    href_off = st->hrefs[st->hrefs_n - 1].off;
    href_n = st->hrefs[st->hrefs_n - 1].n;
  }
  switch (type) {
    case MD_TEXT_NULLCHAR:
      append_run(st, "\xEF\xBF\xBD", 3, style_of(st), href_off, href_n);
      break;
    case MD_TEXT_BR:
      append_run(st, "\n", 1, style_of(st), href_off, href_n);
      break;
    case MD_TEXT_SOFTBR:
      append_run(st, " ", 1, style_of(st), href_off, href_n);
      break;
    case MD_TEXT_ENTITY: {
      const size_t room = size > 4 ? size : 4;
      st->tmp_n = 0;
      st->tmp = (char*)rolltui_grow(st->tmp, &st->tmp_cap, room, 1);
      append_run(st, st->tmp, rolltui_md_decode_entity(txt, size, st->tmp, room), style_of(st), href_off, href_n);
      break;
    }
    case MD_TEXT_CODE:
      append_run(st, txt, size, style_of(st) | S_CODE, href_off, href_n);
      break;
    default: /* NORMAL, HTML, LATEXMATH */
      append_run(st, txt, size, style_of(st), href_off, href_n);
      break;
  }
  return 0;
}

void rolltui_md_parse(RolltuiMdDoc* d, const char* src, size_t n) {
  ParseState st;
  MD_PARSER parser;
  memset(&st, 0, sizeof st);
  memset(&parser, 0, sizeof parser);
  doc_reset(d);
  st.d = d;
  st.cur_row = NONE;
  st.cur_cell = NONE;
  parser.abi_version = 0;
  parser.flags = rolltui_md_parser_flags();
  parser.enter_block = enter_block;
  parser.leave_block = leave_block;
  parser.enter_span = enter_span;
  parser.leave_span = leave_span;
  parser.text = on_text;
  parser.debug_log = NULL;
  parser.syntax = NULL;
  md_parse(src, (MD_SIZE)n, &parser, &st);
  rolltui_mem_free(st.stack);
  rolltui_mem_free(st.spans);
  rolltui_mem_free(st.hrefs);
  rolltui_mem_free(st.tmp);
}

size_t rolltui_md_doc_block_count(const RolltuiMdDoc* d) { return d->root_count; }

static size_t root_at(const RolltuiMdDoc* d, size_t i) {
  size_t b = d->first_root;
  while (b != NONE && i--) b = d->blocks[b].next;
  return b;
}

unsigned char rolltui_md_doc_block_kind(const RolltuiMdDoc* d, size_t i) {
  const size_t b = root_at(d, i);
  if (b == NONE) die("a block kind read past the end");
  return d->blocks[b].kind;
}

const char* rolltui_md_doc_block_code(const RolltuiMdDoc* d, size_t i, size_t* n) {
  const size_t b = root_at(d, i);
  if (b == NONE) die("a block's code read past the end");
  if (n) *n = d->blocks[b].code_n;
  return d->bytes ? d->bytes + d->blocks[b].code_off : "";
}

/* ======================================================================================
 * RENDERING
 *
 * The same algorithm as `MarkdownCpp.cpp`, against the same store. Everything it needs
 * somewhere to work in lives in `Work`, which hangs off the store the caller owns
 * (`rolltui_md_lines_work`) rather than in a thread-local: a `Rendered` that is rendered
 * into every frame reuses the renderer's scratch along with everything else, and there is
 * nothing retained for `shutdown()` to have to find.
 * ==================================================================================== */

typedef struct PrefixPart {
  size_t off, len; /* interned in the store's chrome pool */
  unsigned char role;
} PrefixPart;

typedef struct RunAt {
  size_t start; /* where this run begins in the concatenated paragraph */
  unsigned char role;
  const char* href;
  size_t href_n;
} RunAt;

/* One inline run as BORROWS, which is what the renderer actually needs: the document's runs
 * hold offsets into its byte pool, and a heading's "## " marks are not in that pool at all.
 * Collecting into this one array is what lets both come down the same path — where the
 * first cut cast the `const` document back to mutable and appended a scratch run to it. */
typedef struct RunView {
  const char* text;
  size_t text_n;
  const char* href;
  size_t href_n;
  unsigned style;
} RunView;

typedef struct HRun {
  size_t begin, end;
  unsigned char role;
} HRun;

typedef struct Range {
  size_t off, len;
} Range;

typedef struct Work {
  PrefixPart* prefix;
  size_t prefix_n, prefix_cap;
  uint32_t* srcs;
  size_t srcs_n, srcs_cap;
  char* run_text;
  size_t run_text_n, run_text_cap;
  RunAt* run_at;
  size_t run_at_n, run_at_cap;
  RunView* rv; /* the runs `layout_runs` is about to lay out */
  size_t rv_n, rv_cap;
  HRun* hruns;
  size_t hruns_n, hruns_cap;
  RolltuiMdHighlightSpan* hspans;
  size_t hspans_n, hspans_cap;
  RolltuiMdCodeLine* block_lines;
  size_t block_lines_cap;
  char* scratch;
  size_t scratch_n, scratch_cap;
  char* pad;
  size_t pad_cap;
  char* cell_text;
  size_t cell_text_n, cell_text_cap;
  char* src; /* a too-wide table's pipe-table source */
  size_t src_n, src_cap;
  Range* code_lines;
  size_t code_lines_n, code_lines_cap;
  int* col_width;
  size_t col_width_cap;
  Range* cell_at; /* (first aux line, count) per column */
  size_t cell_at_cap;
  RolltuiWrapLines* wrap;
  int busy;
} Work;

static void* work_make(void) {
  Work* w = (Work*)rolltui_mem_alloc(sizeof *w);
  memset(w, 0, sizeof *w);
  return w;
}

static void work_destroy(void* p) {
  Work* w = (Work*)p;
  if (!w) return;
  rolltui_mem_free(w->prefix);
  rolltui_mem_free(w->srcs);
  rolltui_mem_free(w->run_text);
  rolltui_mem_free(w->run_at);
  rolltui_mem_free(w->rv);
  rolltui_mem_free(w->hruns);
  rolltui_mem_free(w->hspans);
  rolltui_mem_free(w->block_lines);
  rolltui_mem_free(w->scratch);
  rolltui_mem_free(w->pad);
  rolltui_mem_free(w->cell_text);
  rolltui_mem_free(w->src);
  rolltui_mem_free(w->code_lines);
  rolltui_mem_free(w->col_width);
  rolltui_mem_free(w->cell_at);
  rolltui_wrap_free(w->wrap);
  rolltui_mem_free(w);
}

static void work_clear(Work* w) {
  w->prefix_n = 0;
  w->srcs_n = 0;
  w->run_text_n = 0;
  w->run_at_n = 0;
  w->rv_n = 0;
  w->hruns_n = 0;
  w->hspans_n = 0;
  w->scratch_n = 0;
  w->cell_text_n = 0;
  w->src_n = 0;
  w->code_lines_n = 0;
}

typedef struct State {
  RolltuiMdLines* out;   /* the logical text, the interned chrome, the code blocks, the report */
  RolltuiMdLines* lines; /* where lines are being built RIGHT NOW */
  const RolltuiMdDoc* d;
  Work* w;
  int ambiguous;
  int tab_width;
  const RolltuiMdRenderOptions* opt;
  size_t code_index;
} State;

typedef struct Ctx {
  int width;
  size_t pf_off, pf_len;
  size_t pr_off, pr_len;
  int first_used;
  unsigned char base;
  const char* terminator; /* "\t" inside a table row: a row is ONE logical line */
} Ctx;

/* ---- small helpers ----------------------------------------------------------------------- */

static void str_append(char** p, size_t* n, size_t* cap, const char* s, size_t sn) {
  if (sn == 0) return;
  *p = (char*)rolltui_grow(*p, cap, *n + sn, 1);
  memcpy(*p + *n, s, sn);
  *n += sn;
}

static void scratch_set(Work* w, const char* s, size_t n) {
  w->scratch_n = 0;
  str_append(&w->scratch, &w->scratch_n, &w->scratch_cap, s, n);
}

static void scratch_add(Work* w, const char* s, size_t n) {
  str_append(&w->scratch, &w->scratch_n, &w->scratch_cap, s, n);
}

static void scratch_num(Work* w, size_t v) {
  char b[24];
  scratch_add(w, b, (size_t)snprintf(b, sizeof b, "%zu", v));
}

static const char* spaces(Work* w, int n, size_t* out_n) {
  const size_t need = n > 0 ? (size_t)n : 0;
  *out_n = need;
  if (need == 0) return "";
  /* GROWING, AMORTISED, then filled to `need` AND NO FURTHER. The first cut filled to the
   * CAPACITY on a growth, which is a real logical overrun: `rolltui_grow` doubles, and
   * everything past the length the caller asked for is garbage the buffer does not own yet.
   * Every functional test passes over that; **ASan's poisoned slack names it on the first
   * sanitizer run**, which is the case the sanitizer build exists for. */
  w->pad = (char*)rolltui_grow(w->pad, &w->pad_cap, need, 1);
  memset(w->pad, ' ', need);
  return w->pad;
}

static int width_of(State* st, const char* s, size_t n) {
  return rolltui_u_display_width(rolltui_md_lines_scratch(st->lines), s, n, st->ambiguous);
}

static void emit_span(State* st, const char* text, size_t n, unsigned char role, const uint32_t* srcs,
                      size_t src_n, const char* href, size_t href_n) {
  rolltui_md_lines_span(st->lines, text, n, role, st->ambiguous, srcs, src_n, href, href_n);
}

static void text_append(State* st, const char* s, size_t n) { rolltui_md_lines_text_append(st->out, s, n); }
static size_t text_size(State* st) { return rolltui_md_lines_text_size(st->out); }
static void end_logical_line(State* st, const Ctx* ctx) { text_append(st, ctx->terminator, strlen(ctx->terminator)); }

/* Consecutive logical offsets for `text` appended to the logical text at `base`, into the
 * one buffer every span of every line reuses. */
static void sources_for(State* st, const char* text, size_t n, size_t base) {
  size_t count = 0, i;
  const RolltuiUnicodeGrapheme* gs = rolltui_md_lines_clusters(st->lines, text, n, st->ambiguous, &count, NULL);
  st->w->srcs = (uint32_t*)rolltui_grow(st->w->srcs, &st->w->srcs_cap, count, sizeof *st->w->srcs);
  for (i = 0; i < count; ++i) st->w->srcs[i] = (uint32_t)(base + gs[i].offset);
  st->w->srcs_n = count;
}

static unsigned char role_for(const RolltuiMdRoles* r, unsigned style, unsigned char base) {
  if (style & S_CODE) return r->code_inline;
  if (style & S_LINK_URL) return r->link_url;
  if (style & S_LINK) return r->link;
  if (style & S_STRIKE) return r->strikethrough;
  if (style & S_STRONG) return r->strong;
  if (style & S_EMPHASIS) return r->emphasis;
  if (style & S_UNDERLINE) return r->link;
  return base;
}

static void take_prefix(Ctx* c, size_t* off, size_t* len) {
  if (!c->first_used) {
    c->first_used = 1;
    *off = c->pf_off;
    *len = c->pf_len;
    return;
  }
  *off = c->pr_off;
  *len = c->pr_len;
}

static PrefixPart part(State* st, const char* text, size_t n, unsigned char role) {
  PrefixPart p;
  p.off = rolltui_md_lines_intern(st->out, text, n);
  p.len = n;
  p.role = role;
  return p;
}

/* A child block's context: narrower, and carrying THIS block's prefix in front of its own.
 * The parent's first-line prefix is CONSUMED here (a nested block's first line is the
 * parent's first line), which is why `parent` is not const. */
static Ctx child_ctx(State* st, Ctx* parent, int less, const PrefixPart* first_extra, size_t first_n,
                     const PrefixPart* rest_extra, size_t rest_n, unsigned char new_base) {
  Work* w = st->w;
  Ctx c = *parent;
  size_t off = 0, len = 0, fo, ro, i;
  c.width = parent->width - less;
  if (c.width < 1) c.width = 1;
  take_prefix(parent, &off, &len);
  /* Grown BEFORE the copy because the source range is in the same array: a growth mid-loop
   * would move the elements being read. */
  w->prefix = (PrefixPart*)rolltui_grow(w->prefix, &w->prefix_cap,
                                        w->prefix_n + len + first_n + parent->pr_len + rest_n, sizeof *w->prefix);
  fo = w->prefix_n;
  for (i = 0; i < len; ++i) w->prefix[w->prefix_n++] = w->prefix[off + i];
  for (i = 0; i < first_n; ++i) w->prefix[w->prefix_n++] = first_extra[i];
  ro = w->prefix_n;
  for (i = 0; i < parent->pr_len; ++i) w->prefix[w->prefix_n++] = w->prefix[parent->pr_off + i];
  for (i = 0; i < rest_n; ++i) w->prefix[w->prefix_n++] = rest_extra[i];
  c.pf_off = fo;
  c.pf_len = ro - fo;
  c.pr_off = ro;
  c.pr_len = w->prefix_n - ro;
  c.first_used = 0;
  c.base = new_base;
  return c;
}

/* Opens a line and lays down the context's prefix. On a block's first line the prefix is
 * logical text (the marker is copied with the item); elsewhere it is chrome. */
static void start_line(State* st, Ctx* ctx, int first_of_block) {
  size_t off = 0, len = 0, i;
  rolltui_md_lines_open(st->lines);
  take_prefix(ctx, &off, &len);
  for (i = 0; i < len; ++i) {
    const PrefixPart p = st->w->prefix[off + i];
    /* The chrome pool is not the span pool and not the text pool, so this stays valid
     * across both appends below. */
    const char* text = rolltui_md_lines_interned(st->out, p.off);
    if (first_of_block) {
      const size_t base = text_size(st);
      text_append(st, text, p.len);
      sources_for(st, text, p.len, base);
      emit_span(st, text, p.len, p.role, st->w->srcs, st->w->srcs_n, NULL, 0);
    } else {
      emit_span(st, text, p.len, p.role, NULL, 0, NULL, 0);
    }
  }
}

/* A chrome-only first line (a code box's top rule, a table's top border) still carries the
 * block's marker; the marker then stands on a logical line of its own. */
static void finish_chrome_first_line(State* st, Ctx* ctx, size_t before) {
  if (text_size(st) > before) end_logical_line(st, ctx);
}

static void blank_line(State* st, Ctx* ctx) {
  start_line(st, ctx, 0);
  /* Trim trailing spaces so a blank line inside a list is empty and a blank line inside a
   * quote is just its bar. */
  rolltui_md_lines_trim_trailing_spaces(st->lines, st->ambiguous);
  end_logical_line(st, ctx);
  rolltui_md_lines_close(st->lines);
}

/* ---- inline runs ------------------------------------------------------------------------- */

/* Fills `w->rv` with a block's or a cell's runs, as borrows into the document. */
static void collect_runs(State* st, size_t run_first, size_t run_n) {
  Work* w = st->w;
  const RolltuiMdDoc* d = st->d;
  size_t i;
  w->rv = (RunView*)rolltui_grow(w->rv, &w->rv_cap, run_n + 1, sizeof *w->rv);
  w->rv_n = 0;
  for (i = 0; i < run_n; ++i) {
    const Run* r = &d->runs[run_first + i];
    w->rv[w->rv_n].text = d->bytes + r->text_off;
    w->rv[w->rv_n].text_n = r->text_n;
    w->rv[w->rv_n].href = d->bytes + r->href_off;
    w->rv[w->rv_n].href_n = r->href_n;
    w->rv[w->rv_n].style = r->style;
    ++w->rv_n;
  }
}

/* Lay out `w->rv` as wrapped lines with per-grapheme roles and hrefs. The runs'
 * concatenated text becomes one logical line. */
static void layout_runs(State* st, Ctx* ctx, int first_indent, int hanging_indent,
                        unsigned char role_override) {
  Work* w = st->w;
  RolltuiWrapOptions wo;
  size_t i, k, base = 0, line_count;
  memset(&wo, 0, sizeof wo);
  w->run_text_n = 0;
  w->run_at_n = 0;
  w->run_at = (RunAt*)rolltui_grow(w->run_at, &w->run_at_cap, w->rv_n + 1, sizeof *w->run_at);
  for (i = 0; i < w->rv_n; ++i) {
    const RunView* r = &w->rv[i];
    RunAt at;
    at.start = w->run_text_n;
    at.role = role_override != ROLLTUI_MD_NO_ROLE ? role_override : role_for(&st->opt->roles, r->style, ctx->base);
    at.href = (r->style & S_LINK) ? r->href : NULL;
    at.href_n = (r->style & S_LINK) ? r->href_n : 0;
    w->run_at[w->run_at_n++] = at;
    str_append(&w->run_text, &w->run_text_n, &w->run_text_cap, r->text, r->text_n);
  }
  wo.ambiguous_wide = (unsigned char)(st->ambiguous != 0);
  wo.tab_width = st->tab_width;
  wo.first_indent = first_indent;
  wo.hanging_indent = hanging_indent;
  if (!w->wrap) w->wrap = rolltui_wrap_new();
  rolltui_wrap(w->wrap, w->run_text, w->run_text_n, ctx->width, wo);
  line_count = rolltui_wrap_line_count(w->wrap);
  for (k = 0; k < line_count; ++k) {
    const char* ltext;
    size_t ltext_n, gn, g;
    const RolltuiWrapGrapheme* gs;
    int lw, indent, hard;
    rolltui_wrap_line(w->wrap, k, &ltext, &ltext_n, &gs, &gn, &lw, &indent, &hard);
    start_line(st, ctx, k == 0);
    if (k == 0) {
      base = text_size(st);
      text_append(st, w->run_text, w->run_text_n);
    }
    if (indent > 0) {
      size_t pn = 0;
      const char* p = spaces(w, indent, &pn);
      emit_span(st, p, pn, ctx->base, NULL, 0, NULL, 0);
    }
    for (g = 0; g < gn; ++g) {
      const RunAt* at = NULL;
      const uint32_t one = (uint32_t)(base + gs[g].source_offset);
      for (i = 0; i < w->run_at_n; ++i) {
        if (w->run_at[i].start <= gs[g].source_offset) at = &w->run_at[i];
        else break;
      }
      emit_span(st, ltext + gs[g].offset, gs[g].length, at ? at->role : ctx->base, &one, 1,
                at ? at->href : NULL, at ? at->href_n : 0);
    }
    rolltui_md_lines_close(st->lines);
  }
  end_logical_line(st, ctx);
}

/* ---- code blocks ------------------------------------------------------------------------- */

/* A code block's lines as ranges into the document's bytes — no copy, where the C++ side's
 * `split_lines` used to build a whole `std::vector<std::string>` per block per frame. */
static void split_lines(Work* w, const char* code, size_t code_n) {
  size_t start = 0;
  w->code_lines_n = 0;
  while (start <= code_n) {
    const char* nl = (const char*)memchr(code + start, '\n', code_n - start);
    Range r;
    if (!nl) {
      if (start < code_n) {
        r.off = start;
        r.len = code_n - start;
        w->code_lines = (Range*)rolltui_grow(w->code_lines, &w->code_lines_cap, w->code_lines_n + 1, sizeof *w->code_lines);
        w->code_lines[w->code_lines_n++] = r;
      }
      break;
    }
    r.off = start;
    r.len = (size_t)(nl - (code + start));
    w->code_lines = (Range*)rolltui_grow(w->code_lines, &w->code_lines_cap, w->code_lines_n + 1, sizeof *w->code_lines);
    w->code_lines[w->code_lines_n++] = r;
    start = r.off + r.len + 1;
  }
}

/* The first whitespace-delimited word of a fence's info string ("cpp" from
 * "cpp title=x.cpp") — the language tag a highlighter is called with. */
static size_t first_word(const char* s, size_t n) {
  size_t i = 0;
  while (i < n && s[i] != ' ' && s[i] != '\t') ++i;
  return i;
}

size_t rolltui_md_code_block_summary(const char* lang, size_t lang_n, size_t lines, size_t bytes, char* out,
                                     size_t cap) {
  char size_buf[32];
  int n;
  if (lang_n == 0) {
    lang = "code";
    lang_n = 4;
  }
  if (bytes < 1024) snprintf(size_buf, sizeof size_buf, "%zu B", bytes);
  else if (bytes < 1024 * 1024) snprintf(size_buf, sizeof size_buf, "%.1f kB", (double)bytes / 1024.0);
  else snprintf(size_buf, sizeof size_buf, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
  n = snprintf(out, cap, "%.*s \xC2\xB7 %zu %s \xC2\xB7 %s", (int)lang_n, lang, lines,
               lines == 1 ? "line" : "lines", size_buf);
  if (n < 0) return 0;
  return (size_t)n < cap ? (size_t)n : (cap ? cap - 1 : 0);
}

/* Turns a highlighter's raw spans for ONE code line into a run list that is safe to paint:
 * sorted by start (stable, so ties keep the highlighter's own order), every span forced
 * forward and inside [0, line_len), later spans losing ground to earlier ones on overlap.
 * Every span the input needed correcting or dropping is NAMED in the store's report. */
static void sort_spans(RolltuiMdHighlightSpan* a, size_t n) {
  size_t i, j; /* insertion sort: STABLE, which the rule depends on, and n is a handful */
  for (i = 1; i < n; ++i) {
    const RolltuiMdHighlightSpan v = a[i];
    j = i;
    while (j > 0 && a[j - 1].begin > v.begin) {
      a[j] = a[j - 1];
      --j;
    }
    a[j] = v;
  }
}

static void report_span(State* st, const char* lang, size_t lang_n, int line_no, size_t sb, size_t se,
                        const char* tail) {
  Work* w = st->w;
  scratch_set(w, "\"", 1);
  scratch_add(w, lang, lang_n);
  scratch_add(w, "\" line ", 7);
  scratch_num(w, (size_t)line_no);
  scratch_add(w, ": span [", 8);
  scratch_num(w, sb);
  scratch_add(w, ",", 1);
  scratch_num(w, se);
  scratch_add(w, ") ", 2);
  scratch_add(w, tail, strlen(tail));
  rolltui_md_lines_add_clamped(st->out, w->scratch, w->scratch_n);
}

static void hl_sink(void* p, size_t begin, size_t end, unsigned char role) {
  Work* w = (Work*)p;
  w->hspans = (RolltuiMdHighlightSpan*)rolltui_grow(w->hspans, &w->hspans_cap, w->hspans_n + 1, sizeof *w->hspans);
  w->hspans[w->hspans_n].begin = begin;
  w->hspans[w->hspans_n].end = end;
  w->hspans[w->hspans_n].role = role;
  ++w->hspans_n;
}

static void clamp_highlight_spans(State* st, size_t line_len, const char* lang, size_t lang_n, int line_no) {
  Work* w = st->w;
  size_t i, cursor = 0;
  sort_spans(w->hspans, w->hspans_n);
  w->hruns_n = 0;
  for (i = 0; i < w->hspans_n; ++i) {
    const RolltuiMdHighlightSpan s = w->hspans[i];
    size_t b, e;
    char why[128];
    why[0] = 0;
    if (s.end <= s.begin) {
      report_span(st, lang, lang_n, line_no, s.begin, s.end, "runs backwards or is empty — dropped");
      continue;
    }
    if (s.begin >= line_len) {
      snprintf(why, sizeof why, "starts past the line's %zu bytes — dropped", line_len);
      report_span(st, lang, lang_n, line_no, s.begin, s.end, why);
      continue;
    }
    b = s.begin;
    e = s.end;
    if (e > line_len) {
      e = line_len;
      snprintf(why, sizeof why, "exceeds the line's %zu bytes", line_len);
    }
    if (b < cursor) {
      b = cursor;
      if (why[0]) {
        char both[160];
        snprintf(both, sizeof both, "%s and overlaps an earlier span", why);
        snprintf(why, sizeof why, "%.*s", (int)sizeof why - 1, both);
      } else {
        snprintf(why, sizeof why, "overlaps an earlier span");
      }
    }
    if (b >= e) {
      report_span(st, lang, lang_n, line_no, s.begin, s.end, "entirely overlapped by an earlier span — dropped");
      continue;
    }
    if (why[0]) {
      char tail[224];
      snprintf(tail, sizeof tail, "%s — clamped to [%zu,%zu)", why, b, e);
      report_span(st, lang, lang_n, line_no, s.begin, s.end, tail);
    }
    w->hruns = (HRun*)rolltui_grow(w->hruns, &w->hruns_cap, w->hruns_n + 1, sizeof *w->hruns);
    w->hruns[w->hruns_n].begin = b;
    w->hruns[w->hruns_n].end = e;
    w->hruns[w->hruns_n].role = s.role;
    ++w->hruns_n;
    cursor = e;
  }
}

static unsigned char highlight_role_at(const Work* w, size_t offset, unsigned char fallback) {
  size_t i;
  for (i = 0; i < w->hruns_n; ++i)
    if (w->hruns[i].begin <= offset && offset < w->hruns[i].end) return w->hruns[i].role;
  return fallback;
}

/* Decides one numbered block's fold/cap state from the options and any override. Split out
 * so the RULE reads as a rule: a threshold, then a state that may reverse it. */
static void decide_fold(const RolltuiMdRenderOptions* o, size_t lines, RolltuiMdCodeBlock* info, size_t* cap) {
  int over, folded, uncapped = 0;
  size_t i;
  *cap = 0;
  if (!o) return;
  /* ON = the threshold folds a block that is exactly AT the limit, not over it. A block of
   * `fold_over_lines` lines gets a header row and hides itself; the render is legal and the
   * only symptom is that a block short enough to show is folded. */
  over = o->fold_over_lines > 0 &&
         (testkit_ctl_on("md.fold_threshold_folds_at_the_limit")
              ? lines >= (size_t)o->fold_over_lines
              : lines > (size_t)o->fold_over_lines);
  folded = over;
  for (i = 0; i < o->state_count; ++i)
    if (o->states[i].index == info->index) {
      folded = o->states[i].folded != 0;
      uncapped = o->states[i].uncapped != 0;
      break;
    }
  info->folded = (unsigned char)(folded ? 1 : 0);
  /* Foldable when the threshold says so OR a state folded it: either way it needs a header
   * row, because a folded block with nothing to click cannot be reopened. */
  info->foldable = (unsigned char)((over || folded) ? 1 : 0);
  if (!folded && !uncapped && o->cap_lines > 0 && lines > (size_t)o->cap_lines) *cap = (size_t)o->cap_lines;
}

static void render_blocks(State* st, size_t first, Ctx* ctx, int tight);

/* `fold_index` < 0 means this block is NOT numbered and can never fold: the only such block
 * is the pipe-table source a too-narrow table falls back to, which exists because of the
 * WIDTH and so must not be able to own a user's fold toggle (Markdown.hpp). */
static void render_code(State* st, const char* code, size_t code_n, const char* label, size_t label_n, Ctx* ctx,
                        int highlightable, long fold_index) {
  Work* w = st->w;
  const int boxed = ctx->width >= 8;
  const int inner = boxed ? ctx->width - 4 : ctx->width;
  const size_t lang_n = highlightable ? first_word(label, label_n) : 0;
  const char* lang = label;
  const int highlighting = highlightable && st->opt->highlight != NULL;
  const int numbered = fold_index >= 0;
  RolltuiMdCodeBlock info;
  size_t cap = 0, src_n, i;
  int first = 1;
  RolltuiWrapOptions wo;
  memset(&wo, 0, sizeof wo);
  wo.ambiguous_wide = (unsigned char)(st->ambiguous != 0);
  wo.tab_width = st->tab_width;
  split_lines(w, code, code_n);
  src_n = w->code_lines_n;

  memset(&info, 0, sizeof info);
  info.header_line = ROLLTUI_MD_NO_LINE;
  info.marker_line = ROLLTUI_MD_NO_LINE;
  if (numbered) {
    info.index = (size_t)fold_index;
    info.lang_p = label;
    info.lang_n = first_word(label, label_n);
    info.lines = src_n;
    info.bytes = code_n;
    decide_fold(st->opt, src_n, &info, &cap);
  }

  /* The header row IS the language label when there is one, so the box rule below drops its
   * own — one place names the block, and it does not move when the block opens. */
  if (info.foldable) {
    char buf[ROLLTUI_MD_SUMMARY_MAX];
    size_t before = text_size(st);
    size_t n;
    info.header_line = rolltui_md_lines_count(st->lines);
    start_line(st, ctx, first);
    if (first) finish_chrome_first_line(st, ctx, before);
    first = 0;
    emit_span(st, info.folded ? "\xE2\x96\xB8 " : "\xE2\x96\xBE ", 4, st->opt->roles.text_muted, NULL, 0, NULL, 0); /* ▸ ▾ */
    n = rolltui_md_code_block_summary(info.lang_p, info.lang_n, info.lines, info.bytes, buf, sizeof buf);
    emit_span(st, buf, n, st->opt->roles.code_label, NULL, 0, NULL, 0);
    rolltui_md_lines_close(st->lines);
  }
  /* FOLDED AND CAPPED BLOCKS STILL CONTRIBUTE EVERY BYTE (Markdown.hpp): what is hidden is
   * lines, never text, so an offset into this document does not move when a block opens or
   * closes and a find highlight cannot land on the wrong bytes. */
  if (info.folded) {
    info.text_begin = text_size(st);
    for (i = 0; i < src_n; ++i) {
      text_append(st, code + w->code_lines[i].off, w->code_lines[i].len);
      end_logical_line(st, ctx);
    }
    info.text_end = text_size(st);
    info.hidden = src_n;
    rolltui_md_lines_add_code_block(st->out, &info);
    return;
  }

  if (boxed) { /* ┌ ┐ */
    const size_t lab_n = info.foldable ? 0 : label_n;
    size_t before = text_size(st);
    int used = 1;
    start_line(st, ctx, first);
    if (first) finish_chrome_first_line(st, ctx, before);
    first = 0;
    scratch_set(w, "\xE2\x94\x8C", 3);
    if (lab_n) {
      const int tw = width_of(st, label, lab_n) + 2;
      if (tw + 2 <= ctx->width) {
        scratch_add(w, " ", 1);
        scratch_add(w, label, lab_n);
        scratch_add(w, " ", 1);
        used += tw;
      }
    }
    for (; used < ctx->width - 1; ++used) scratch_add(w, "\xE2\x94\x80", 3);
    scratch_add(w, "\xE2\x94\x90", 3);
    emit_span(st, w->scratch, w->scratch_n, st->opt->roles.code_label, NULL, 0, NULL, 0);
    rolltui_md_lines_close(st->lines);
  }

  info.text_begin = text_size(st);
  for (i = 0; i < src_n; ++i) {
    const char* raw = code + w->code_lines[i].off;
    const size_t raw_n = w->code_lines[i].len;
    const size_t base = text_size(st);
    size_t lc, k;
    text_append(st, raw, raw_n);
    end_logical_line(st, ctx);
    if (cap > 0 && i >= cap) continue; /* hidden by the cap: text above, no lines drawn */
    if (!w->wrap) w->wrap = rolltui_wrap_new();
    rolltui_wrap(w->wrap, raw, raw_n, inner, wo);
    /* Unregistered highlighter (the default) or a non-highlightable block (HTML): `hruns`
     * stays empty and every grapheme below takes exactly the pre-seam path — this is the
     * control markdown_test.cpp asserts byte-identical. */
    w->hruns_n = 0;
    if (highlighting) {
      w->block_lines = (RolltuiMdCodeLine*)rolltui_grow(w->block_lines, &w->block_lines_cap, src_n,
                                                        sizeof *w->block_lines);
      for (k = 0; k < src_n; ++k) {
        w->block_lines[k].p = code + w->code_lines[k].off;
        w->block_lines[k].n = w->code_lines[k].len;
      }
      w->hspans_n = 0;
      st->opt->highlight(st->opt->highlight_ctx, lang, lang_n, w->block_lines, src_n, i, hl_sink, w);
      clamp_highlight_spans(st, raw_n, lang, lang_n, (int)i + 1);
    }
    lc = rolltui_wrap_line_count(w->wrap);
    for (k = 0; k < lc; ++k) {
      const char* ltext;
      size_t ltext_n, gn, g;
      const RolltuiWrapGrapheme* gs;
      int lw, indent, hard, pad;
      rolltui_wrap_line(w->wrap, k, &ltext, &ltext_n, &gs, &gn, &lw, &indent, &hard);
      start_line(st, ctx, first);
      first = 0;
      if (boxed) emit_span(st, "\xE2\x94\x82 ", 4, st->opt->roles.code_label, NULL, 0, NULL, 0); /* │ */
      if (w->hruns_n == 0) {
        w->srcs = (uint32_t*)rolltui_grow(w->srcs, &w->srcs_cap, gn, sizeof *w->srcs);
        for (g = 0; g < gn; ++g) w->srcs[g] = (uint32_t)(base + gs[g].source_offset);
        emit_span(st, ltext, ltext_n, st->opt->roles.code_block, w->srcs, gn, NULL, 0);
      } else {
        for (g = 0; g < gn; ++g) {
          const uint32_t one = (uint32_t)(base + gs[g].source_offset);
          emit_span(st, ltext + gs[g].offset, gs[g].length, highlight_role_at(w, gs[g].source_offset, st->opt->roles.code_block), &one, 1,
                    NULL, 0);
        }
      }
      pad = inner - lw;
      if (pad > 0) {
        size_t pn = 0;
        const char* p = spaces(w, pad, &pn);
        emit_span(st, p, pn, st->opt->roles.code_block, NULL, 0, NULL, 0);
      }
      if (boxed) emit_span(st, " \xE2\x94\x82", 4, st->opt->roles.code_label, NULL, 0, NULL, 0);
      rolltui_md_lines_close(st->lines);
    }
  }
  info.text_end = text_size(st);
  if (cap > 0 && src_n > cap) {
    char marker[ROLLTUI_MARKER_MAX];
    size_t mn;
    info.hidden = src_n - cap;
    /* The SAME marker the transcript and draw_scrolled_text use (rolltui_marker.h), so the
     * three cannot say "there is more below" three different ways. */
    mn = rolltui_scroll_marker_text(info.hidden, inner, st->ambiguous, marker, sizeof marker);
    if (mn) {
      const int pad = inner - width_of(st, marker, mn);
      size_t before = text_size(st);
      info.marker_line = rolltui_md_lines_count(st->lines);
      start_line(st, ctx, first);
      if (first) finish_chrome_first_line(st, ctx, before);
      first = 0;
      if (boxed) emit_span(st, "\xE2\x94\x82 ", 4, st->opt->roles.code_label, NULL, 0, NULL, 0);
      if (pad > 0) {
        size_t pn = 0;
        const char* p = spaces(w, pad, &pn);
        emit_span(st, p, pn, st->opt->roles.code_block, NULL, 0, NULL, 0);
      }
      emit_span(st, marker, mn, st->opt->roles.scroll_marker, NULL, 0, NULL, 0);
      if (boxed) emit_span(st, " \xE2\x94\x82", 4, st->opt->roles.code_label, NULL, 0, NULL, 0);
      rolltui_md_lines_close(st->lines);
    }
  }
  if (boxed) { /* └ ┘ */
    size_t before = text_size(st);
    int used = 1;
    start_line(st, ctx, first);
    if (first) finish_chrome_first_line(st, ctx, before);
    first = 0;
    scratch_set(w, "\xE2\x94\x94", 3);
    for (; used < ctx->width - 1; ++used) scratch_add(w, "\xE2\x94\x80", 3);
    scratch_add(w, "\xE2\x94\x98", 3);
    emit_span(st, w->scratch, w->scratch_n, st->opt->roles.code_label, NULL, 0, NULL, 0);
    rolltui_md_lines_close(st->lines);
  }
  if (numbered) rolltui_md_lines_add_code_block(st->out, &info);
}

/* ---- tables ------------------------------------------------------------------------------ */

static void runs_plain(Work* w, const RolltuiMdDoc* d, size_t run_first, size_t run_n) {
  size_t i;
  w->cell_text_n = 0;
  for (i = 0; i < run_n; ++i)
    str_append(&w->cell_text, &w->cell_text_n, &w->cell_text_cap, d->bytes + d->runs[run_first + i].text_off,
               d->runs[run_first + i].text_n);
}

static void table_border(State* st, Ctx* ctx, const Block* t, int* first, const char* l, const char* m,
                         const char* r) {
  Work* w = st->w;
  const size_t cols = t->align_n;
  const size_t before = text_size(st);
  size_t c;
  int i;
  start_line(st, ctx, *first);
  if (*first) finish_chrome_first_line(st, ctx, before);
  *first = 0;
  scratch_set(w, l, 3);
  for (c = 0; c < cols; ++c) {
    for (i = 0; i < w->col_width[c] + 2; ++i) scratch_add(w, "\xE2\x94\x80", 3);
    scratch_add(w, (c + 1 < cols) ? m : r, 3);
  }
  emit_span(st, w->scratch, w->scratch_n, st->opt->roles.table_border, NULL, 0, NULL, 0);
  rolltui_md_lines_close(st->lines);
}

static void render_table(State* st, size_t bi, Ctx* ctx) {
  Work* w = st->w;
  const RolltuiMdDoc* d = st->d;
  const Block* t = &d->blocks[bi];
  const size_t cols = t->align_n;
  int chrome, total, first = 1;
  size_t c, r, i;
  RolltuiMdLines* aux;
  if (cols == 0 || t->row_n == 0) return;
  w->col_width = (int*)rolltui_grow(w->col_width, &w->col_width_cap, cols, sizeof *w->col_width);
  for (c = 0; c < cols; ++c) w->col_width[c] = 1;
  for (r = 0; r < t->row_n; ++r) {
    const Row* row = &d->rows[t->row_first + r];
    for (c = 0; c < cols && c < row->cell_n; ++c) {
      const Cell* cell = &d->cells[row->cell_first + c];
      int cw;
      runs_plain(w, d, cell->run_first, cell->run_n);
      cw = width_of(st, w->cell_text, w->cell_text_n);
      if (cw > w->col_width[c]) w->col_width[c] = cw;
    }
  }
  chrome = (int)cols * 3 + 1; /* "│ " per column + " │" */
  if (chrome + (int)cols > ctx->width) {
    /* Cannot show one cell per column: render the source instead, in full. */
    w->src_n = 0;
    for (r = 0; r < t->row_n; ++r) {
      const Row* row = &d->rows[t->row_first + r];
      str_append(&w->src, &w->src_n, &w->src_cap, "|", 1);
      for (c = 0; c < cols; ++c) {
        str_append(&w->src, &w->src_n, &w->src_cap, " ", 1);
        if (c < row->cell_n) {
          const Cell* cell = &d->cells[row->cell_first + c];
          runs_plain(w, d, cell->run_first, cell->run_n);
          str_append(&w->src, &w->src_n, &w->src_cap, w->cell_text, w->cell_text_n);
        }
        str_append(&w->src, &w->src_n, &w->src_cap, " |", 2);
      }
      str_append(&w->src, &w->src_n, &w->src_cap, "\n", 1);
      if (r + 1 == t->head_rows) {
        str_append(&w->src, &w->src_n, &w->src_cap, "|", 1);
        for (c = 0; c < cols; ++c) str_append(&w->src, &w->src_n, &w->src_cap, "---|", 4);
        str_append(&w->src, &w->src_n, &w->src_cap, "\n", 1);
      }
    }
    render_code(st, w->src, w->src_n, "table", 5, ctx, 0, -1);
    return;
  }
  total = chrome;
  for (c = 0; c < cols; ++c) total += w->col_width[c];
  while (total > ctx->width) { /* shrink the widest column until it fits */
    size_t widest = 0;
    for (c = 1; c < cols; ++c)
      if (w->col_width[c] > w->col_width[widest]) widest = c;
    if (w->col_width[widest] <= 1) break;
    --w->col_width[widest];
    --total;
  }
  table_border(st, ctx, t, &first, "\xE2\x94\x8C", "\xE2\x94\xAC", "\xE2\x94\x90"); /* ┌ ┬ ┐ */
  aux = rolltui_md_lines_aux(st->out);
  w->cell_at = (Range*)rolltui_grow(w->cell_at, &w->cell_at_cap, cols, sizeof *w->cell_at);
  for (r = 0; r < t->row_n; ++r) {
    const Row* row = &d->rows[t->row_first + r];
    const int head = r < t->head_rows;
    size_t height = 1, h, tn;
    /* The cells are laid out at their COLUMN's width, in a store of their own, and then
     * poured into the row: the row's lines are appended AFTER the cell lines they read, so
     * a mark/rewind on one store could not drop the cells (rolltui_md_lines.h). */
    rolltui_md_lines_reset(aux);
    for (c = 0; c < cols; ++c) {
      Ctx cc = *ctx;
      const size_t before = rolltui_md_lines_count(aux);
      cc.pf_len = 0;
      cc.pr_len = 0;
      cc.first_used = 0;
      cc.width = w->col_width[c];
      cc.base = (unsigned char)(head ? st->opt->roles.table_header : ctx->base);
      cc.terminator = "\t"; /* cells of a row share one logical line */
      st->lines = aux;
      if (c < row->cell_n) {
        const Cell* cell = &d->cells[row->cell_first + c];
        collect_runs(st, cell->run_first, cell->run_n);
        layout_runs(st, &cc, 0, 0, (unsigned char)(head ? st->opt->roles.table_header : ROLLTUI_MD_NO_ROLE));
      } else {
        end_logical_line(st, &cc); /* an absent cell still holds its column */
      }
      st->lines = st->out;
      w->cell_at[c].off = before;
      w->cell_at[c].len = rolltui_md_lines_count(aux) - before;
      if (w->cell_at[c].len > height) height = w->cell_at[c].len;
    }
    rolltui_md_lines_finish(aux);
    tn = text_size(st);
    if (tn > 0 && rolltui_md_lines_text(st->out)[tn - 1] == '\t') {
      rolltui_md_lines_text_pop(st->out);
      text_append(st, "\n", 1);
    }
    for (h = 0; h < height; ++h) {
      start_line(st, ctx, 0);
      emit_span(st, "\xE2\x94\x82", 3, st->opt->roles.table_border, NULL, 0, NULL, 0);
      for (c = 0; c < cols; ++c) {
        const RolltuiMdLine* cell = h < w->cell_at[c].len ? rolltui_md_lines_line(aux, w->cell_at[c].off + h) : NULL;
        const int pad = w->col_width[c] - (cell ? cell->width : 0);
        int left = 0;
        emit_span(st, " ", 1, st->opt->roles.table_border, NULL, 0, NULL, 0);
        if (d->aligns[t->align_off + c] == A_RIGHT) left = pad;
        else if (d->aligns[t->align_off + c] == A_CENTER) left = pad / 2;
        if (left > 0) {
          size_t pn = 0;
          const char* p = spaces(w, left, &pn);
          emit_span(st, p, pn, ctx->base, NULL, 0, NULL, 0);
        }
        if (cell)
          for (i = 0; i < cell->span_n; ++i) {
            const RolltuiMdSpan* s = &cell->span_p[i];
            emit_span(st, s->text_p, s->text_n, s->role, s->src_p, s->src_n, s->href_p, s->href_n);
          }
        if (pad - left > 0) {
          size_t pn = 0;
          const char* p = spaces(w, pad - left, &pn);
          emit_span(st, p, pn, ctx->base, NULL, 0, NULL, 0);
        }
        emit_span(st, " \xE2\x94\x82", 4, st->opt->roles.table_border, NULL, 0, NULL, 0);
      }
      rolltui_md_lines_close(st->lines);
    }
    if (r + 1 == t->head_rows && r + 1 < t->row_n)
      table_border(st, ctx, t, &first, "\xE2\x94\x9C", "\xE2\x94\xBC", "\xE2\x94\xA4"); /* ├ ┼ ┤ */
  }
  table_border(st, ctx, t, &first, "\xE2\x94\x94", "\xE2\x94\xB4", "\xE2\x94\x98"); /* └ ┴ ┘ */
}

/* ---- blocks ------------------------------------------------------------------------------ */

static void render_block(State* st, size_t bi, Ctx* ctx) {
  const RolltuiMdDoc* d = st->d;
  const Block* b = &d->blocks[bi];
  Work* w = st->w;
  switch (b->kind) {
    case ROLLTUI_MD_BLOCK_PARAGRAPH:
      collect_runs(st, b->run_first, b->run_n);
      layout_runs(st, ctx, 0, 0, ROLLTUI_MD_NO_ROLE);
      break;
    case ROLLTUI_MD_BLOCK_HEADING: {
      /* The heading marks are a Run like any other, and this is the one place that needs a
       * run list that is not the block's own — which is the whole reason `RunView` exists. */
      const int level = b->level < 1 ? 1 : (b->level > 6 ? 6 : b->level);
      char marks[8];
      int i;
      for (i = 0; i < level; ++i) marks[i] = '#';
      marks[level] = ' ';
      collect_runs(st, b->run_first, b->run_n);
      memmove(w->rv + 1, w->rv, w->rv_n * sizeof *w->rv);
      w->rv[0].text = marks;
      w->rv[0].text_n = (size_t)level + 1;
      w->rv[0].href = NULL;
      w->rv[0].href_n = 0;
      w->rv[0].style = S_PLAIN;
      ++w->rv_n;
      layout_runs(st, ctx, 0, level + 1, st->opt->roles.heading);
      break;
    }
    case ROLLTUI_MD_BLOCK_CODE:
      render_code(st, d->bytes + b->code_off, b->code_n, d->bytes + b->info_off, b->info_n, ctx, 1,
                  (long)st->code_index++);
      break;
    case ROLLTUI_MD_BLOCK_HTML:
      /* Never highlighted: an HTML block is opaque code by design (Markdown.hpp), and it
       * carries no language tag to highlight it by. It IS numbered, because it is a block of
       * the document and folds like one. */
      render_code(st, d->bytes + b->code_off, b->code_n, "html", 4, ctx, 0, (long)st->code_index++);
      break;
    case ROLLTUI_MD_BLOCK_RULE: {
      int i;
      start_line(st, ctx, 1);
      end_logical_line(st, ctx);
      w->scratch_n = 0;
      for (i = 0; i < ctx->width; ++i) scratch_add(w, "\xE2\x94\x80", 3);
      emit_span(st, w->scratch, w->scratch_n, st->opt->roles.rule, NULL, 0, NULL, 0);
      rolltui_md_lines_close(st->lines);
      break;
    }
    case ROLLTUI_MD_BLOCK_QUOTE: {
      const PrefixPart bar = part(st, "\xE2\x94\x82 ", 4, st->opt->roles.quote);
      Ctx c = child_ctx(st, ctx, 2, &bar, 1, &bar, 1, st->opt->roles.quote);
      render_blocks(st, b->first_child, &c, 0);
      break;
    }
    case ROLLTUI_MD_BLOCK_LIST: {
      unsigned n = b->start;
      int marker_w = 2;
      size_t child = b->first_child, count = 0;
      size_t k = 0;
      for (; child != NONE; child = d->blocks[child].next) ++count;
      if (b->ordered) {
        char last[24];
        marker_w = (int)snprintf(last, sizeof last, "%u", b->start + (unsigned)count) + 2;
      }
      for (child = b->first_child; child != NONE; child = d->blocks[child].next, ++k) {
        const Block* item = &d->blocks[child];
        char mark[24];
        size_t mark_n;
        int mw, width;
        PrefixPart marker, pad;
        Ctx c;
        if (k > 0 && !b->tight) blank_line(st, ctx);
        if (item->task) {
          memcpy(mark, item->checked ? "[x] " : "[ ] ", 4);
          mark_n = 4;
        } else if (b->ordered) {
          mark_n = (size_t)snprintf(mark, sizeof mark, "%u. ", n);
        } else {
          memcpy(mark, "\xE2\x80\xA2 ", 4); /* • */
          mark_n = 4;
        }
        mw = width_of(st, mark, mark_n);
        width = mw > marker_w ? mw : marker_w;
        {  /* right-align numbers under the widest */
          size_t pn = 0;
          const char* p = spaces(w, width - mw, &pn);
          scratch_set(w, p, pn);
          scratch_add(w, mark, mark_n);
          marker = part(st, w->scratch, w->scratch_n, st->opt->roles.list_marker);
          p = spaces(w, width, &pn);
          pad = part(st, p, pn, ctx->base);
        }
        c = child_ctx(st, ctx, width, &marker, 1, &pad, 1, ctx->base);
        render_blocks(st, item->first_child, &c, b->tight);
        if (item->first_child == NONE) { /* an empty item still shows its marker */
          start_line(st, &c, 1);
          rolltui_md_lines_close(st->lines);
          end_logical_line(st, &c);
        }
        ++n;
      }
      break;
    }
    case ROLLTUI_MD_BLOCK_ITEM: /* only ever inside a List */
      render_blocks(st, b->first_child, ctx, 1);
      break;
    case ROLLTUI_MD_BLOCK_TABLE:
      render_table(st, bi, ctx);
      break;
    default:
      break;
  }
}

static void render_blocks(State* st, size_t first, Ctx* ctx, int tight) {
  size_t b = first;
  int n = 0;
  while (b != NONE) {
    const size_t next = st->d->blocks[b].next;
    if (n > 0 && !tight) blank_line(st, ctx);
    render_block(st, b, ctx);
    b = next;
    ++n;
  }
}

void rolltui_md_render(RolltuiMdLines* out, const RolltuiMdDoc* doc, const RolltuiMdRenderOptions* opt) {
  RolltuiMdRenderOptions defaults;
  State st;
  Ctx ctx;
  Work* w;
  size_t n;
  if (!opt) {
    memset(&defaults, 0, sizeof defaults);
    defaults.width = 80;
    defaults.tab_width = 8;
    opt = &defaults;
  }
  rolltui_md_lines_reset(out);
  w = (Work*)rolltui_md_lines_work(out, work_make, work_destroy);
  /* The window is ENFORCED rather than promised, the same rule `Scratch` states one level
   * up: a highlighter that re-entered this render would otherwise quietly share the buffers
   * of the render it is inside. */
  if (w->busy) die("a render re-entered the same store");
  w->busy = 1;
  work_clear(w);
  memset(&st, 0, sizeof st);
  st.out = out;
  st.lines = out;
  st.d = doc;
  st.w = w;
  st.ambiguous = opt->ambiguous_wide;
  st.tab_width = opt->tab_width;
  st.opt = opt;
  memset(&ctx, 0, sizeof ctx);
  ctx.width = opt->width > 1 ? opt->width : 1;
  ctx.base = opt->base;
  ctx.terminator = "\n";
  render_blocks(&st, doc->first_root, &ctx, 0);
  n = rolltui_md_lines_text_size(out);
  if (n > 0 && rolltui_md_lines_text(out)[n - 1] == '\n') rolltui_md_lines_text_pop(out);
  rolltui_md_lines_finish(out);
  w->busy = 0;
}


/* ============================================================================================
 * THE STYLING VOCABULARY — moved from `Markdown.cpp`, where it was a C symbol
 * (`extern "C" rolltui_md_roles`) defined in a C++ file that a C file already called
 * (`rolltui_widget_transcript.c`). The C library could not link without it.
 *
 * Its stated reason for living there was the same one `Diff.cpp`'s role table had:
 * *"`rolltui/Style.hpp` is the one place these names exist ... the reason neither
 * implementation names a role."* True when written, and false once `ROLLTUI_ROLE_LIST` made
 * Role the C's own — at which point naming a role here mirrors nothing.
 *
 * A caller that wants a DIFFERENT mapping still hands one in; this is the default, not a
 * replacement for the parameter.
 * ============================================================================================ */

/* The no-override sentinel must not be able to collide with a real Role — the same assertion
 * `Markdown.cpp` carried, in the C's spelling. */
ROLLTUI_STATIC_ASSERT(ROLLTUI_ROLE_COUNT < ROLLTUI_MD_NO_ROLE,
                      "the no-override sentinel must not collide with a real Role");

const RolltuiMdRoles* rolltui_md_roles(void) {
  static const RolltuiMdRoles kRoles = {
      ROLLTUI_ROLE_TEXT_MUTED,        ROLLTUI_ROLE_MD_HEADING,     ROLLTUI_ROLE_MD_EMPHASIS,
      ROLLTUI_ROLE_MD_STRONG,         ROLLTUI_ROLE_MD_STRIKETHROUGH, ROLLTUI_ROLE_MD_CODE_INLINE,
      ROLLTUI_ROLE_MD_CODE_BLOCK,     ROLLTUI_ROLE_MD_CODE_LABEL,  ROLLTUI_ROLE_MD_LINK,
      ROLLTUI_ROLE_MD_LINK_URL,       ROLLTUI_ROLE_MD_QUOTE,       ROLLTUI_ROLE_MD_LIST_MARKER,
      ROLLTUI_ROLE_MD_TABLE_BORDER,   ROLLTUI_ROLE_MD_TABLE_HEADER, ROLLTUI_ROLE_MD_RULE,
      ROLLTUI_ROLE_SCROLL_MARKER,
  };
  return &kRoles;
}
