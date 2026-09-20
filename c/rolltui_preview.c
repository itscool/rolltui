/* rolltui/c/rolltui_preview.c — a file, drawn into a rectangle. The contract is in the header. */
#include "rolltui/c/rolltui_preview.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_md_lines.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"

#define SNIFF_BYTES 8192
#define TAB_COLUMNS 4
#define ELLIPSIS "\xE2\x80\xA6"

struct RolltuiPreview {
  RolltuiStr path;
  int kind;
  RolltuiStr message; /* UNREADABLE: why, in words */
  long long size;     /* bytes on disk */
  int truncated;      /* TEXT / MARKDOWN: the file was longer than the limit */
  int fd;             /* HEX: the file, kept open so the rows on screen can be read; -1 otherwise */
  RolltuiStr body;    /* TEXT / MARKDOWN: the bytes read, whole or the first LIMIT of them */
  size_t* line_off;   /* TEXT: where each line starts in `body` */
  size_t line_n, line_cap;
  size_t top;         /* the first row on screen, in the units of the body */
  size_t rows_vis;    /* how many rows the last draw had for the body */
  int hex_per_row;    /* how many bytes a hex row showed at the last draw */
  /* MARKDOWN: the parsed document and its lines at the width last drawn. Both are OWNED and REUSED between files
   * (a re-parse and a re-render keep every buffer), and the lines are laid out again only when the width, or how wide an
   * ambiguous glyph is, changes. */
  RolltuiMdDoc* md_doc;
  RolltuiMdLines* md_lines;
  int md_width, md_aw;
  int diagram; /* MARKDOWN: the file is a diagram (`.mmd`), not a document */
  RolltuiUnicodeScratch* u; /* OWNED */
  RolltuiDrawScratch* ds;   /* OWNED */
  RolltuiStr s1;            /* scratch a row is built in */
};

RolltuiPreview* rolltui_preview_new(void) {
  RolltuiPreview* pv = (RolltuiPreview*)rolltui_mem_alloc(sizeof *pv);
  memset(pv, 0, sizeof *pv);
  pv->fd = -1;
  pv->hex_per_row = 16;
  pv->u = rolltui_u_scratch_new();
  pv->ds = rolltui_draw_scratch_new();
  return pv;
}

static void release_content(RolltuiPreview* pv) {
  if (pv->fd >= 0) close(pv->fd);
  pv->fd = -1;
  pv->kind = ROLLTUI_PREVIEW_NONE;
  pv->size = 0;
  pv->truncated = 0;
  pv->line_n = 0;
  pv->top = 0;
  pv->rows_vis = 0;
  pv->md_width = 0;
  pv->diagram = 0;
  rolltui_str_clear(&pv->message);
  rolltui_str_clear(&pv->body);
}

void rolltui_preview_free(RolltuiPreview* pv) {
  if (!pv) return;
  release_content(pv);
  rolltui_str_free(&pv->path);
  rolltui_str_free(&pv->message);
  rolltui_str_free(&pv->body);
  rolltui_str_free(&pv->s1);
  rolltui_mem_free(pv->line_off);
  rolltui_md_doc_free(pv->md_doc);
  rolltui_md_lines_free(pv->md_lines);
  rolltui_u_scratch_free(pv->u);
  rolltui_draw_scratch_free(pv->ds);
  rolltui_mem_free(pv);
}

int rolltui_preview_kind(const RolltuiPreview* pv) { return pv ? pv->kind : ROLLTUI_PREVIEW_NONE; }
const char* rolltui_preview_message(const RolltuiPreview* pv, size_t* len) {
  if (len) *len = pv ? pv->message.n : 0;
  return pv && pv->message.p ? pv->message.p : "";
}

static void human_size(long long n, char* out, size_t cap) {
  const char* unit = "";
  double v = (double)n;
  if (v >= 1024.0) { v /= 1024.0; unit = "K"; }
  if (v >= 1024.0) { v /= 1024.0; unit = "M"; }
  if (v >= 1024.0) { v /= 1024.0; unit = "G"; }
  if (!*unit) snprintf(out, cap, "%lld B", n);
  else if (v < 10.0) snprintf(out, cap, "%.1f%s", v, unit);
  else snprintf(out, cap, "%.0f%s", v, unit);
}

/* ---- reading -------------------------------------------------------------------------------- */

static size_t read_upto(int fd, unsigned char* buf, size_t want) {
  size_t got = 0;
  while (got < want) {
    const ssize_t r = read(fd, buf + got, want - got);
    if (r > 0) got += (size_t)r;
    else if (r < 0 && errno == EINTR) continue;
    else break;
  }
  return got;
}

/* A file is binary when it has a NUL in what was sniffed, or when three in ten of it are bytes that are neither text
 * nor a valid sequence of it (a Latin-1 file with its accents is text; a few stray bytes are not a verdict). (A UTF-16 file has NULs, and is shown as bytes: it is not something a line
 * listing could show honestly.) */
static int looks_binary(const unsigned char* b, size_t n) {
  size_t i = 0, weird = 0;
  if (n == 0) return 0;
  for (i = 0; i < n; ++i)
    if (b[i] == 0) return 1;
  i = 0;
  while (i < n) {
    RolltuiDecodedChar d;
    rolltui_u_decode_one((const char*)b, n, i, &d);
    if (!d.valid) {
      /* a multi-byte character cut by the end of what was sniffed is not evidence of anything */
      if (i + 4 > n && b[i] >= 0xC0) break;
      ++weird;
    } else if (d.cp < 0x20 && d.cp != '\t' && d.cp != '\n' && d.cp != '\r' && d.cp != '\f' && d.cp != 0x1b) {
      ++weird;
    }
    i += d.length ? d.length : 1;
  }
  return weird * 10 > n * 3;
}

static int has_markdown_extension(const RolltuiStr* path) {
  static const char* const ext[] = {".md", ".markdown", ".mdown", ".mkd", ".mdx"};
  size_t k;
  for (k = 0; k < sizeof ext / sizeof *ext; ++k) {
    const size_t en = strlen(ext[k]);
    size_t i;
    if (path->n <= en) continue;
    for (i = 0; i < en; ++i) {
      char c = path->p[path->n - en + i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != ext[k][i]) break;
    }
    if (i == en) return 1;
  }
  return 0;
}

/* A DIAGRAM FILE is a mermaid block that is the whole document: drawn by the same renderer, as the fence it would be. */
static int has_diagram_extension(const RolltuiStr* path) {
  static const char* const ext[] = {".mmd", ".mermaid"};
  size_t k;
  for (k = 0; k < sizeof ext / sizeof *ext; ++k) {
    const size_t en = strlen(ext[k]);
    size_t i;
    if (path->n <= en) continue;
    for (i = 0; i < en; ++i) {
      char c = path->p[path->n - en + i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != ext[k][i]) break;
    }
    if (i == en) return 1;
  }
  return 0;
}

/* `s` with every byte a file must not be trusted with made a picture — a control character, a stray byte, a
 * terminal's own C1 controls, bidi overrides — but the line and the tab left alone, because Markdown is made
 * of both. The parser and the renderer downstream then never see a byte that could reach the terminal as one. */
static void scrub(const char* s, size_t n, RolltuiStr* out) {
  size_t i = 0;
  rolltui_str_clear(out);
  while (i < n) {
    RolltuiDecodedChar d;
    char enc[4];
    size_t en;
    RolltuiCodepoint cp;
    rolltui_u_decode_one(s, n, i, &d);
    i += d.length ? d.length : 1;
    cp = d.valid ? d.cp : 0xFFFD;
    if (cp == '\n' || cp == '\t') {
      /* Markdown is made of both: kept as they are */
    } else if (cp == '\r') {
      if (i < n && s[i] == '\n') continue; /* CRLF is a newline */
      cp = '\n';
    } else if (cp < 0x20) cp = 0x2400 + cp;
    else if (cp == 0x7F) cp = 0x2421;
    else if (cp >= 0x80 && cp <= 0x9F) cp = 0xFFFD;
    else if (cp == 0x2028 || cp == 0x2029 || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) cp = 0xFFFD;
    en = rolltui_u_append_utf8(cp, enc);
    rolltui_str_append(out, enc, en);
  }
}

static void parse_markdown(RolltuiPreview* pv) {
  RolltuiStr clean;
  memset(&clean, 0, sizeof clean);
  pv->diagram = has_diagram_extension(&pv->path);
  if (pv->diagram) {
    /* the file is the diagram: a document of one fenced block */
    RolltuiStr fenced;
    memset(&fenced, 0, sizeof fenced);
    rolltui_str_append(&fenced, "```mermaid\n", 11);
    scrub(pv->body.p ? pv->body.p : "", pv->body.n, &clean);
    rolltui_str_append_str(&fenced, &clean);
    if (clean.n == 0 || clean.p[clean.n - 1] != '\n') rolltui_str_append(&fenced, "\n", 1);
    rolltui_str_append(&fenced, "```\n", 4);
    rolltui_str_set(&pv->body, fenced.p, fenced.n);
    rolltui_str_free(&fenced);
  } else {
    scrub(pv->body.p ? pv->body.p : "", pv->body.n, &clean);
    rolltui_str_set(&pv->body, clean.p ? clean.p : "", clean.n);
  }
  rolltui_str_free(&clean);
  if (pv->truncated) {
    /* said in the document itself, where the reader is reading: the last line says the file goes on */
    char note[96], sz[24];
    human_size(pv->size, sz, sizeof sz);
    snprintf(note, sizeof note, "\n\n*\xE2\x80\xA6 first %d KB of %s shown*\n", ROLLTUI_PREVIEW_TEXT_LIMIT / 1024, sz);
    rolltui_str_append(&pv->body, note, strlen(note));
  }
  if (!pv->md_doc) pv->md_doc = rolltui_md_doc_new();
  rolltui_md_parse(pv->md_doc, pv->body.p ? pv->body.p : "", pv->body.n);
  pv->md_width = 0; /* laid out at the first draw, when the width is known */
}

static void index_lines(RolltuiPreview* pv) {
  const char* b = pv->body.p ? pv->body.p : "";
  size_t i = 0;
  pv->line_n = 0;
  if (pv->body.n == 0) return;
  pv->line_off = (size_t*)rolltui_grow(pv->line_off, &pv->line_cap, 1, sizeof *pv->line_off);
  pv->line_off[pv->line_n++] = 0;
  while (i < pv->body.n) {
    const char* nl = (const char*)memchr(b + i, '\n', pv->body.n - i);
    if (!nl) break;
    i = (size_t)(nl - b) + 1;
    if (i >= pv->body.n) break; /* a final newline ends the last line rather than starting another */
    pv->line_off = (size_t*)rolltui_grow(pv->line_off, &pv->line_cap, pv->line_n + 1, sizeof *pv->line_off);
    pv->line_off[pv->line_n++] = i;
  }
}

static void fail(RolltuiPreview* pv, const char* why) {
  pv->kind = ROLLTUI_PREVIEW_UNREADABLE;
  rolltui_str_set(&pv->message, why, strlen(why));
}

static void load(RolltuiPreview* pv) {
  struct stat st;
  unsigned char sniff[SNIFF_BYTES];
  size_t got;
  const int fd = open(pv->path.p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    fail(pv, strerror(errno));
    return;
  }
  if (fstat(fd, &st) != 0) {
    fail(pv, strerror(errno));
    close(fd);
    return;
  }
  /* A pipe, a socket, a device: opening was already a risk (NONBLOCK spares a FIFO), reading is not one to take. */
  if (!S_ISREG(st.st_mode)) {
    fail(pv, S_ISDIR(st.st_mode) ? "a folder" : "not a regular file");
    close(fd);
    return;
  }
  pv->size = (long long)st.st_size;
  got = read_upto(fd, sniff, SNIFF_BYTES);
  if (got == 0) {
    pv->kind = ROLLTUI_PREVIEW_EMPTY;
    close(fd);
    return;
  }
  if (looks_binary(sniff, got)) {
    pv->kind = ROLLTUI_PREVIEW_HEX;
    pv->fd = fd; /* kept: the rows on screen are read from it */
    if (pv->size < (long long)got) pv->size = (long long)got;
    return;
  }
  {
    /* TEXT: what was sniffed is the start of it; read on up to the limit. */
    unsigned char* buf = (unsigned char*)rolltui_mem_alloc(ROLLTUI_PREVIEW_TEXT_LIMIT + 1);
    size_t have = got, more;
    memcpy(buf, sniff, got);
    more = read_upto(fd, buf + have, (size_t)ROLLTUI_PREVIEW_TEXT_LIMIT - have);
    have += more;
    if (have == (size_t)ROLLTUI_PREVIEW_TEXT_LIMIT) {
      unsigned char probe;
      if (read_upto(fd, &probe, 1) == 1) pv->truncated = 1;
    }
    close(fd);
    if (pv->truncated) {
      /* Cut where a line ends, or failing that where a character does: never through the middle of one. */
      size_t cut = have;
      while (cut > 0 && cut + 4096 > have && buf[cut - 1] != '\n') --cut;
      if (cut == 0 || buf[cut - 1] != '\n') {
        cut = have;
        while (cut > 0 && (buf[cut - 1] & 0xC0) == 0x80) --cut;
        if (cut > 0 && buf[cut - 1] >= 0xC0) --cut;
      }
      have = cut;
    }
    rolltui_str_set(&pv->body, (const char*)buf, have);
    rolltui_mem_free(buf);
  }
  pv->kind = has_markdown_extension(&pv->path) || has_diagram_extension(&pv->path) ? ROLLTUI_PREVIEW_MARKDOWN : ROLLTUI_PREVIEW_TEXT;
  if (pv->kind == ROLLTUI_PREVIEW_MARKDOWN) parse_markdown(pv);
  else index_lines(pv);
}

void rolltui_preview_set_path(RolltuiPreview* pv, const char* path, size_t len) {
  if (!pv) return;
  if (len == 0 || !path) {
    if (pv->path.n == 0 && pv->kind == ROLLTUI_PREVIEW_NONE) return;
    release_content(pv);
    rolltui_str_clear(&pv->path);
    return;
  }
  if (pv->kind != ROLLTUI_PREVIEW_NONE && rolltui_str_eq(&pv->path, path, len)) return;
  release_content(pv);
  rolltui_str_set(&pv->path, path, len);
  load(pv);
}

/* ---- what a row is made of ------------------------------------------------------------------ */

/* `s`, made safe to draw, into `out`: a tab becomes the spaces to the next stop, a control character its
 * visible picture (`␛`), a byte that is not UTF-8 a `�`. Stops once `cells` are filled and reports whether
 * there was more. */
static int sanitize(const char* s, size_t n, int cells, int aw, RolltuiStr* out) {
  size_t i = 0;
  int col = 0;
  rolltui_str_clear(out);
  while (i < n) {
    RolltuiDecodedChar d;
    char enc[4];
    size_t en;
    RolltuiCodepoint cp;
    int w;
    if (col > cells) return 1;
    rolltui_u_decode_one(s, n, i, &d);
    i += d.length ? d.length : 1;
    cp = d.valid ? d.cp : 0xFFFD;
    if (cp == '\t') {
      const int stop = TAB_COLUMNS - (col % TAB_COLUMNS);
      int k;
      for (k = 0; k < stop; ++k) rolltui_str_append(out, " ", 1);
      col += stop;
      continue;
    }
    if (cp < 0x20) cp = 0x2400 + cp;            /* ␀ … ␟ */
    else if (cp == 0x7F) cp = 0x2421;           /* ␡ */
    else if (cp >= 0x80 && cp <= 0x9F) cp = 0xFFFD; /* C1: a control of the terminal's own */
    else if (cp == 0x2028 || cp == 0x2029 || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) cp = 0xFFFD; /* separators, bidi overrides */
    en = rolltui_u_append_utf8(cp, enc);
    rolltui_str_append(out, enc, en);
    w = rolltui_u_codepoint_width(cp, aw);
    col += w > 0 ? w : 0;
  }
  return 0;
}

/* One line of text at (x, y), cut at `cells` with an ellipsis where it did not fit. */
static void put_row(RolltuiPreview* pv, RolltuiFrame* f, int x, int y, const char* s, size_t n, RolltuiStyle st,
                    RolltuiStyle dim, int cells, int aw) {
  int more, w;
  if (cells <= 0) return;
  more = sanitize(s, n, cells, aw, &pv->s1);
  w = rolltui_u_display_width(pv->u, pv->s1.p ? pv->s1.p : "", pv->s1.n, aw);
  if (!more && w <= cells) {
    rolltui_frame_put_text(f, pv->ds, x, y, pv->s1.p ? pv->s1.p : "", pv->s1.n, st, cells, aw, 0);
    return;
  }
  {
    int took = 0;
    const size_t keep = rolltui_u_fit(pv->u, pv->s1.p ? pv->s1.p : "", pv->s1.n, cells - 1, aw, &took);
    rolltui_frame_put_text(f, pv->ds, x, y, pv->s1.p ? pv->s1.p : "", keep, st, took, aw, 0);
    rolltui_frame_put_text(f, pv->ds, x + took, y, ELLIPSIS, 3, dim, 1, 0, 0);
  }
}

/* ---- hex ----------------------------------------------------------------------------------- */

static int hex_digits_for(long long size) {
  int d = 4;
  long long limit = 0x10000;
  while (size > limit && d < 16) { d += 2; limit <<= 8; }
  return d;
}

/* The width of one row for `n` bytes and `od` offset digits. */
static int hex_width(int n, int od) { return od + 2 + (3 * n - 1 + (n == 16 ? 1 : 0)) + 2 + (n + 2); }

static int hex_bytes_that_fit(int cells, int od) {
  static const int options[] = {16, 8, 4, 2, 1};
  size_t k;
  for (k = 0; k < sizeof options / sizeof *options; ++k)
    if (hex_width(options[k], od) <= cells) return options[k];
  return 1;
}

static size_t total_rows(const RolltuiPreview* pv) {
  switch (pv->kind) {
    case ROLLTUI_PREVIEW_TEXT: return pv->line_n;
    case ROLLTUI_PREVIEW_MARKDOWN: return pv->md_lines && pv->md_width ? rolltui_md_lines_count(pv->md_lines) : 0;
    case ROLLTUI_PREVIEW_HEX: {
      const size_t per = (size_t)(pv->hex_per_row > 0 ? pv->hex_per_row : 16);
      return ((size_t)pv->size + per - 1) / per;
    }
    default: return 0;
  }
}

static void clamp_top(RolltuiPreview* pv) {
  const size_t total = total_rows(pv);
  const size_t vis = pv->rows_vis > 0 ? pv->rows_vis : 1;
  const size_t max_top = total > vis ? total - vis : 0;
  if (pv->top > max_top) pv->top = max_top;
}

static void draw_hex(RolltuiPreview* pv, RolltuiFrame* f, RolltuiRect body, RolltuiStyle text, RolltuiStyle dim, int aw) {
  const int od = hex_digits_for(pv->size);
  const int per = hex_bytes_that_fit(body.w, od);
  const size_t old_per = (size_t)(pv->hex_per_row > 0 ? pv->hex_per_row : 16);
  unsigned char buf[16 * 64];
  int rows = body.h, r;
  size_t first_byte;
  if ((size_t)per != old_per) {
    /* the width changed the row's length: keep the same BYTE at the top, not the same row number */
    pv->top = (pv->top * old_per) / (size_t)per;
    pv->hex_per_row = per;
  }
  pv->rows_vis = (size_t)body.h;
  clamp_top(pv);
  if (rows > 64) rows = 64;
  first_byte = pv->top * (size_t)per;
  for (r = 0; r < rows; ++r) {
    const size_t at = first_byte + (size_t)r * (size_t)per;
    ssize_t got;
    int x = body.x, k;
    char tmp[32];
    if (at >= (size_t)pv->size) break;
    got = pread(pv->fd, buf, (size_t)per, (off_t)at);
    if (got <= 0) break;
    snprintf(tmp, sizeof tmp, "%0*llx", od, (unsigned long long)at);
    rolltui_frame_put_text(f, pv->ds, x, body.y + r, tmp, (size_t)od, dim, od, aw, 0);
    x += od + 2;
    for (k = 0; k < per; ++k) {
      if (k < got) {
        char hx[3];
        snprintf(hx, sizeof hx, "%02x", buf[k]);
        rolltui_frame_put_text(f, pv->ds, x, body.y + r, hx, 2, buf[k] == 0 ? dim : text, 2, aw, 0);
      }
      x += 2;
      if (k < per - 1) x += 1;
      if (k == 7 && per == 16) x += 1;
    }
    x += 2; /* the loop left x just past the last pair: two spaces, then the gutter */
    rolltui_frame_put_text(f, pv->ds, x, body.y + r, "|", 1, dim, 1, aw, 0);
    for (k = 0; k < per && k < got; ++k) {
      const char c = (char)(buf[k] >= 0x20 && buf[k] < 0x7F ? buf[k] : '.');
      rolltui_frame_put_text(f, pv->ds, x + 1 + k, body.y + r, &c, 1, c == '.' && (buf[k] < 0x20 || buf[k] >= 0x7F) ? dim : text, 1, aw, 0);
    }
    rolltui_frame_put_text(f, pv->ds, x + 1 + (int)(got < per ? got : per), body.y + r, "|", 1, dim, 1, aw, 0);
  }
}

/* ---- markdown -------------------------------------------------------------------------------- */

static void render_md(RolltuiPreview* pv, int width, int aw) {
  RolltuiMdRenderOptions ro;
  if (!pv->md_lines) pv->md_lines = rolltui_md_lines_new();
  memset(&ro, 0, sizeof ro);
  ro.width = width;
  ro.ambiguous_wide = aw;
  ro.tab_width = TAB_COLUMNS;
  ro.base = ROLLTUI_ROLE_TEXT;
  ro.roles = *rolltui_md_roles();
  rolltui_md_render(pv->md_lines, pv->md_doc, &ro);
  pv->md_width = width;
  pv->md_aw = aw;
}

/* The document, laid out at this width and drawn line by line in the theme's markdown roles. A span whose role
 * states no background stands on the ground under it, as the text of every window now does. */
static void draw_markdown(RolltuiPreview* pv, RolltuiFrame* f, RolltuiRect body, const RolltuiStyle* styles,
                          RolltuiStyleColor ground, int aw) {
  int row;
  size_t n;
  if (!pv->md_doc) return;
  if (pv->md_width != body.w || pv->md_aw != aw) render_md(pv, body.w, aw);
  n = rolltui_md_lines_count(pv->md_lines);
  clamp_top(pv);
  for (row = 0; row < body.h; ++row) {
    const size_t li = pv->top + (size_t)row;
    const RolltuiMdLine* line;
    int x = body.x;
    size_t k;
    if (li >= n) break;
    line = rolltui_md_lines_line(pv->md_lines, li);
    for (k = 0; k < line->span_n && x < body.x + body.w; ++k) {
      const RolltuiMdSpan* sp = &line->span_p[k];
      RolltuiStyle st = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, sp->role);
      if (st.bg.kind == 0) st.bg = ground;
      x += rolltui_frame_put_text(f, pv->ds, x, body.y + row, sp->text_p, sp->text_n, st, body.x + body.w - x, aw, 0);
    }
  }
}

/* ---- the draw ------------------------------------------------------------------------------- */

static const char* base_name(const RolltuiStr* path, size_t* n) {
  size_t at = path->n;
  while (at > 0 && path->p[at - 1] != '/') --at;
  *n = path->n - at;
  return path->p + at;
}

void rolltui_preview_draw(RolltuiPreview* pv, RolltuiFrame* f, RolltuiRect r, const RolltuiStyle* styles,
                          int ambiguous_wide, int focused) {
  RolltuiStyle text, dim, head, err;
  RolltuiStyleColor ground;
  RolltuiRect body;
  size_t name_n = 0;
  const char* name;
  char info[96];
  int name_w, info_w;
  if (!pv || pv->kind == ROLLTUI_PREVIEW_NONE || r.w < 6 || r.h < 2) return;
  text = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  dim = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT_MUTED);
  err = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_ERROR);
  ground = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND)->bg;
  head = focused ? *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_MENU_SELECTED)
                 : *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_LABEL);
  if (!focused) head.bg = rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_PANEL_BACKGROUND)->bg;
  text.bg = dim.bg = err.bg = ground;

  /* THE HEAD ROW: the file's name, and on the right what the preview made of it. */
  {
    RolltuiRect band = {r.x, r.y, r.w, 1};
    rolltui_frame_fill(f, pv->ds, band, head, NULL, 0);
  }
  switch (pv->kind) {
    case ROLLTUI_PREVIEW_TEXT: {
      char sz[24];
      human_size(pv->size, sz, sizeof sz);
      if (pv->truncated) {
        char shown[24];
        human_size((long long)pv->body.n, shown, sizeof shown);
        snprintf(info, sizeof info, "text \xC2\xB7 first %s of %s", shown, sz);
      } else {
        snprintf(info, sizeof info, "text \xC2\xB7 %zu %s \xC2\xB7 %s", pv->line_n, pv->line_n == 1 ? "line" : "lines", sz);
      }
      break;
    }
    case ROLLTUI_PREVIEW_MARKDOWN: {
      char sz[24];
      human_size(pv->size, sz, sizeof sz);
      snprintf(info, sizeof info, "%s \xC2\xB7 %s", pv->diagram ? "diagram" : "markdown", sz);
      break;
    }
    case ROLLTUI_PREVIEW_HEX: {
      char sz[24];
      human_size(pv->size, sz, sizeof sz);
      snprintf(info, sizeof info, "binary \xC2\xB7 %s", sz);
      break;
    }
    case ROLLTUI_PREVIEW_EMPTY: snprintf(info, sizeof info, "empty"); break;
    default: info[0] = '\0'; break;
  }
  name = base_name(&pv->path, &name_n);
  info_w = rolltui_u_display_width(pv->u, info, strlen(info), ambiguous_wide);
  name_w = rolltui_u_display_width(pv->u, name, name_n, ambiguous_wide);
  {
    /* the name gets the room; the info takes the right of the band, one cell in from the edge, only when whole */
    const int show_info = info[0] && 1 + name_w + 2 + info_w + 1 <= r.w;
    if (show_info) rolltui_frame_put_text(f, pv->ds, r.x + r.w - 1 - info_w, r.y, info, strlen(info), head, info_w, ambiguous_wide, 0);
    put_row(pv, f, r.x + 1, r.y, name, name_n, head, head, r.w - 2 - (show_info ? info_w + 2 : 0), ambiguous_wide);
  }

  body.x = r.x + 1;
  body.y = r.y + 1;
  body.w = r.w - 2;
  body.h = r.h - 1;
  if (body.w <= 0 || body.h <= 0) return;
  pv->rows_vis = (size_t)body.h;

  switch (pv->kind) {
    case ROLLTUI_PREVIEW_EMPTY:
      put_row(pv, f, body.x, body.y, "(empty file)", 12, dim, dim, body.w, ambiguous_wide);
      break;
    case ROLLTUI_PREVIEW_UNREADABLE:
      put_row(pv, f, body.x, body.y, pv->message.p ? pv->message.p : "", pv->message.n, err, err, body.w, ambiguous_wide);
      break;
    case ROLLTUI_PREVIEW_HEX:
      draw_hex(pv, f, body, text, dim, ambiguous_wide);
      break;
    case ROLLTUI_PREVIEW_MARKDOWN:
      draw_markdown(pv, f, body, styles, ground, ambiguous_wide);
      break;
    default: {
      int row;
      clamp_top(pv);
      for (row = 0; row < body.h; ++row) {
        const size_t li = pv->top + (size_t)row;
        size_t from, to;
        if (li >= pv->line_n) break;
        from = pv->line_off[li];
        to = li + 1 < pv->line_n ? pv->line_off[li + 1] - 1 : pv->body.n;
        if (to > from && pv->body.p[to - 1] == '\n') --to;
        if (to > from && pv->body.p[to - 1] == '\r') --to;
        put_row(pv, f, body.x, body.y + row, pv->body.p + from, to - from, text, dim, body.w, ambiguous_wide);
      }
      if (pv->truncated && pv->top + (size_t)body.h >= pv->line_n && body.h > 0) {
        /* the last thing a truncated file shows is that it stops here, and that the file does not */
        const int last = (int)(pv->line_n > pv->top ? pv->line_n - pv->top : 0);
        if (last < body.h) {
          char note[64], sz[24];
          human_size(pv->size, sz, sizeof sz);
          snprintf(note, sizeof note, "%s  first %d KB of %s", ELLIPSIS, ROLLTUI_PREVIEW_TEXT_LIMIT / 1024, sz);
          put_row(pv, f, body.x, body.y + last, note, strlen(note), dim, dim, body.w, ambiguous_wide);
        }
      }
      break;
    }
  }
}

/* ---- scrolling ------------------------------------------------------------------------------ */

int rolltui_preview_scroll_by(RolltuiPreview* pv, long long lines) {
  size_t before, total;
  long long at;
  if (!pv) return 0;
  before = pv->top;
  total = total_rows(pv);
  at = (long long)pv->top + lines;
  if (at < 0) at = 0;
  pv->top = (size_t)at;
  if (pv->top >= total) pv->top = total ? total - 1 : 0;
  clamp_top(pv);
  return pv->top != before;
}

int rolltui_preview_scroll_page(RolltuiPreview* pv, int direction) {
  const long long page = pv && pv->rows_vis > 1 ? (long long)pv->rows_vis - 1 : 1;
  return rolltui_preview_scroll_by(pv, direction < 0 ? -page : page);
}

int rolltui_preview_scroll_edge(RolltuiPreview* pv, int to_end) {
  size_t before;
  if (!pv) return 0;
  before = pv->top;
  pv->top = to_end ? total_rows(pv) : 0;
  clamp_top(pv);
  return pv->top != before;
}

int rolltui_preview_scroll_extent(const RolltuiPreview* pv, RolltuiScrollExtent* out) {
  if (!pv || pv->kind == ROLLTUI_PREVIEW_NONE) return 0;
  out->first = pv->top;
  out->visible = pv->rows_vis;
  out->total = total_rows(pv);
  return 1;
}

int rolltui_preview_scroll_to(RolltuiPreview* pv, size_t first) {
  if (!pv) return 0;
  pv->top = first;
  clamp_top(pv);
  return 1;
}
