/* rolltui/c/rolltui_image.c — PICTURES: decoded, fitted to a window, and drawn the best way the terminal has.
 *
 * DECODING is stb_image's (third_party/stb), its functions static to this file and its memory the library's, so the leak gauge
 * counts a picture like any other buffer. A picture larger than ROLLTUI_IMAGE_MAX_SIDE on a side, or than ROLLTUI_IMAGE_MAX_BYTES
 * once decoded, is refused before it is decoded, with a reason, rather than taking the memory down.
 *
 * DRAWING has four ways, one chosen per terminal (rolltui_termfacts.c) and published process-wide like the depth:
 *   BLOCKS  half blocks, two pixels to a cell in their colours: every terminal, and every multiplexer.
 *   KITTY   the kitty graphics protocol's Unicode placeholders: each cell holds U+10EEEE with two diacritics naming its row and
 *           column, its foreground the picture's id. The picture is in the cells, so the diff, a popup over it, tmux and the
 *           scrollback all treat it as text; its pixels go once per picture, before the frame that first shows it.
 *   ITERM2  OSC 1337, and SIXEL: the picture is drawn at a cell by the terminal, outside the grid. Its cells are blanks in the
 *           frame, and the picture is sent again whenever they were all blanks this frame and were not last frame (a popup that
 *           covered part of it has closed) — and never while anything else is drawn over it.
 */
#include "rolltui/c/rolltui_image.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_termfacts.h"

/* ---- stb_image, the library's own allocator under it ---------------------------------------------- */

static void* stb_grow(void* p, size_t old, size_t n) {
  void* q = rolltui_mem_alloc(n ? n : 1);
  if (p) {
    memcpy(q, p, old < n ? old : n);
    rolltui_mem_free(p);
  }
  return q;
}
#define STBI_MALLOC(n) rolltui_mem_alloc((n) ? (n) : 1)
#define STBI_REALLOC_SIZED(p, old, n) stb_grow((p), (old), (n))
#define STBI_FREE(p) rolltui_mem_free(p)
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_PNM
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wcomma"
#endif
#endif
#include "rolltui/third_party/stb/stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* ---- the picture ---------------------------------------------------------------------------------- */

struct RolltuiImage {
  unsigned char* rgba; /* w * h * 4, OWNED */
  int w, h;
  unsigned int id;     /* 1 .. 2^24 - 1: kitty's id, carried in a 24-bit foreground */
  RolltuiStr encoded;  /* the PNG, JPEG or GIF it was decoded from: what iTerm2 is sent as it is */
};

static _Atomic unsigned int g_next_id = 0;
static unsigned int next_id(void) {
  /* A process's pictures share one counter, wrapping inside 24 bits and never 0; atomic, so a worker may decode a picture. */
  return atomic_fetch_add(&g_next_id, 1u) % 0xFFFFFEu + 1u;
}

static RolltuiImage* make(unsigned char* rgba, int w, int h) {
  RolltuiImage* img = (RolltuiImage*)rolltui_mem_alloc(sizeof *img);
  memset(img, 0, sizeof *img);
  img->rgba = rgba;
  img->w = w;
  img->h = h;
  img->id = next_id();
  return img;
}

static void say(RolltuiStr* err, const char* a, const char* b) {
  if (!err) return;
  rolltui_str_set(err, a, strlen(a));
  if (b) rolltui_str_append(err, b, strlen(b));
}

RolltuiImage* rolltui_image_decode(const unsigned char* bytes, size_t len, RolltuiStr* err) {
  int w = 0, h = 0, comp = 0;
  unsigned char* px;
  RolltuiImage* img;
  if (err) rolltui_str_clear(err);
  if (!bytes || len == 0 || len > 0x7fffffff) {
    say(err, "not a picture: empty", NULL);
    return NULL;
  }
  if (!stbi_info_from_memory(bytes, (int)len, &w, &h, &comp)) {
    say(err, "not a picture this library reads (PNG, JPEG, GIF, BMP, TGA, PNM): ", stbi_failure_reason());
    return NULL;
  }
  if (w <= 0 || h <= 0 || w > ROLLTUI_IMAGE_MAX_SIDE || h > ROLLTUI_IMAGE_MAX_SIDE ||
      (double)w * (double)h * 4.0 > (double)ROLLTUI_IMAGE_MAX_BYTES) {
    char buf[96];
    snprintf(buf, sizeof buf, "too large to decode: %dx%d", w, h);
    say(err, buf, NULL);
    return NULL;
  }
  px = stbi_load_from_memory(bytes, (int)len, &w, &h, &comp, 4);
  if (!px) {
    say(err, "cannot decode: ", stbi_failure_reason());
    return NULL;
  }
  img = make(px, w, h);
  /* the formats iTerm2 takes as they are */
  if ((len > 8 && memcmp(bytes, "\x89PNG", 4) == 0) || (len > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) ||
      (len > 6 && memcmp(bytes, "GIF8", 4) == 0))
    rolltui_str_set(&img->encoded, (const char*)bytes, len);
  return img;
}

RolltuiImage* rolltui_image_load(const char* path, size_t len, RolltuiStr* err) {
  RolltuiStr bytes;
  RolltuiImage* img;
  memset(&bytes, 0, sizeof bytes);
  if (err) rolltui_str_clear(err);
  if (!path || !rolltui_preset_read_file(path, len, rolltui_str_put, &bytes)) {
    if (err) {
      rolltui_str_set(err, "cannot read ", 12);
      rolltui_str_append(err, path ? path : "", path ? len : 0);
    }
    rolltui_str_free(&bytes);
    return NULL;
  }
  img = rolltui_image_decode((const unsigned char*)bytes.p, bytes.n, err);
  rolltui_str_free(&bytes);
  return img;
}

RolltuiImage* rolltui_image_from_rgba(const unsigned char* rgba, int w, int h) {
  unsigned char* px;
  if (!rgba || w <= 0 || h <= 0 || w > ROLLTUI_IMAGE_MAX_SIDE || h > ROLLTUI_IMAGE_MAX_SIDE) return NULL;
  px = (unsigned char*)rolltui_mem_alloc((size_t)w * (size_t)h * 4);
  memcpy(px, rgba, (size_t)w * (size_t)h * 4);
  return make(px, w, h);
}

void rolltui_image_free(RolltuiImage* img) {
  if (!img) return;
  rolltui_mem_free(img->rgba);
  rolltui_str_free(&img->encoded);
  rolltui_mem_free(img);
}

const unsigned char* rolltui_image_rgba(const RolltuiImage* img, int* w, int* h) {
  if (w) *w = img ? img->w : 0;
  if (h) *h = img ? img->h : 0;
  return img ? img->rgba : NULL;
}

unsigned int rolltui_image_id(const RolltuiImage* img) { return img ? img->id : 0; }

/* ---- fitting ------------------------------------------------------------------------------------- */

void rolltui_image_fit(const RolltuiImage* img, int cols, int rows, int cell_w, int cell_h, int* used_cols, int* used_rows,
                       int* px_w, int* px_h) {
  double scale;
  int pw, ph, uc, ur;
  if (cell_w <= 0 || cell_h <= 0) { /* not known: a cell is about twice as tall as wide */
    cell_w = 10;
    cell_h = 20;
  }
  if (!img || cols <= 0 || rows <= 0) {
    *used_cols = *used_rows = *px_w = *px_h = 0;
    return;
  }
  /* the pixel box the cells hold, the picture's aspect kept, never enlarged past its own size */
  scale = (double)cols * cell_w / img->w;
  if ((double)rows * cell_h / img->h < scale) scale = (double)rows * cell_h / img->h;
  if (scale > 1.0) scale = 1.0;
  pw = (int)floor(img->w * scale + 0.5);
  ph = (int)floor(img->h * scale + 0.5);
  if (pw < 1) pw = 1;
  if (ph < 1) ph = 1;
  uc = (pw + cell_w - 1) / cell_w;
  ur = (ph + cell_h - 1) / cell_h;
  *used_cols = uc > cols ? cols : uc;
  *used_rows = ur > rows ? rows : ur;
  *px_w = pw;
  *px_h = ph;
}

/* The picture scaled to `dw` x `dh` into `out` (dw * dh * 4): each output pixel the average of the source pixels it covers, so a
 * large picture shrinks without the shimmer of sampling every n-th pixel. Alpha weights the colour, so a transparent edge does not
 * darken what is next to it. */
void rolltui_image_scale(const RolltuiImage* img, int dw, int dh, unsigned char* out) {
  int x, y;
  for (y = 0; y < dh; ++y) {
    const int y0 = (int)((long long)y * img->h / dh);
    int y1 = (int)((long long)(y + 1) * img->h / dh);
    if (y1 <= y0) y1 = y0 + 1;
    for (x = 0; x < dw; ++x) {
      const int x0 = (int)((long long)x * img->w / dw);
      int x1 = (int)((long long)(x + 1) * img->w / dw);
      double r = 0, g = 0, b = 0, a = 0;
      int sx, sy, n = 0;
      unsigned char* o = out + ((size_t)y * dw + x) * 4;
      if (x1 <= x0) x1 = x0 + 1;
      for (sy = y0; sy < y1 && sy < img->h; ++sy)
        for (sx = x0; sx < x1 && sx < img->w; ++sx) {
          const unsigned char* p = img->rgba + ((size_t)sy * img->w + sx) * 4;
          const double al = p[3] / 255.0;
          r += p[0] * al;
          g += p[1] * al;
          b += p[2] * al;
          a += al;
          ++n;
        }
      if (n == 0 || a <= 0) {
        o[0] = o[1] = o[2] = o[3] = 0;
        continue;
      }
      o[0] = (unsigned char)(r / a + 0.5);
      o[1] = (unsigned char)(g / a + 0.5);
      o[2] = (unsigned char)(b / a + 0.5);
      o[3] = (unsigned char)(a / n * 255.0 + 0.5);
    }
  }
}

/* ---- base64 -------------------------------------------------------------------------------------- */

static void b64(const unsigned char* p, size_t n, RolltuiStr* out) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i;
  for (i = 0; i + 2 < n; i += 3) {
    const unsigned v = ((unsigned)p[i] << 16) | ((unsigned)p[i + 1] << 8) | p[i + 2];
    const char q[4] = {k[(v >> 18) & 63], k[(v >> 12) & 63], k[(v >> 6) & 63], k[v & 63]};
    rolltui_str_append(out, q, 4);
  }
  if (n - i == 1) {
    const unsigned v = (unsigned)p[i] << 16;
    const char q[4] = {k[(v >> 18) & 63], k[(v >> 12) & 63], '=', '='};
    rolltui_str_append(out, q, 4);
  } else if (n - i == 2) {
    const unsigned v = ((unsigned)p[i] << 16) | ((unsigned)p[i + 1] << 8);
    const char q[4] = {k[(v >> 18) & 63], k[(v >> 12) & 63], k[(v >> 6) & 63], '='};
    rolltui_str_append(out, q, 4);
  }
}

/* A terminal sequence appended as it is, or, inside tmux, passed through to the terminal tmux is drawn on: the DCS wrapper with
 * every ESC in it doubled. */
static void emit(const char* seq, size_t n, int tmux, RolltuiStr* out) {
  size_t i;
  if (!tmux) {
    rolltui_str_append(out, seq, n);
    return;
  }
  rolltui_str_append(out, "\x1bPtmux;", sizeof "\x1bPtmux;" - 1);
  for (i = 0; i < n; ++i) {
    if (seq[i] == 0x1b) rolltui_str_append(out, "\x1b", 1);
    rolltui_str_append(out, &seq[i], 1);
  }
  rolltui_str_append(out, "\x1b\\", sizeof "\x1b\\" - 1);
}

/* ---- kitty --------------------------------------------------------------------------------------- */

/* The largest picture sent to kitty, on a side: it scales the picture into the placement's cells itself, so more pixels than a
 * screen has are bytes for nothing. */
#define KITTY_MAX_SIDE 2048
#define KITTY_CHUNK 4096

void rolltui_image_kitty_upload(const RolltuiImage* img, int tmux, RolltuiStr* out) {
  RolltuiStr data, seq;
  unsigned char* px = img->rgba;
  int w = img->w, h = img->h;
  size_t at = 0;
  char head[96];
  if (w > KITTY_MAX_SIDE || h > KITTY_MAX_SIDE) {
    const double s = (double)KITTY_MAX_SIDE / (w > h ? w : h);
    w = (int)(w * s) > 0 ? (int)(w * s) : 1;
    h = (int)(h * s) > 0 ? (int)(h * s) : 1;
    px = (unsigned char*)rolltui_mem_alloc((size_t)w * h * 4);
    rolltui_image_scale(img, w, h, px);
  }
  memset(&data, 0, sizeof data);
  memset(&seq, 0, sizeof seq);
  b64(px, (size_t)w * h * 4, &data);
  if (px != img->rgba) rolltui_mem_free(px);
  /* TRANSMITTED, NOT DISPLAYED (a=t): the virtual placement and the cells show it. q=2: no replies to read back. */
  do {
    const size_t n = data.n - at > KITTY_CHUNK ? KITTY_CHUNK : data.n - at;
    const int more = at + n < data.n;
    rolltui_str_clear(&seq);
    if (at == 0) snprintf(head, sizeof head, "\x1b_Ga=t,f=32,s=%d,v=%d,i=%u,q=2,m=%d;", w, h, img->id, more);
    else snprintf(head, sizeof head, "\x1b_Gm=%d;", more);
    rolltui_str_append(&seq, head, strlen(head));
    rolltui_str_append(&seq, data.p + at, n);
    rolltui_str_append(&seq, "\x1b\\", 2);
    emit(seq.p, seq.n, tmux, out);
    at += n;
  } while (at < data.n);
  rolltui_str_free(&data);
  rolltui_str_free(&seq);
}

void rolltui_image_kitty_place(unsigned int id, int cols, int rows, int tmux, RolltuiStr* out) {
  char seq[96];
  const int n = snprintf(seq, sizeof seq, "\x1b_Ga=p,U=1,i=%u,c=%d,r=%d,q=2\x1b\\", id, cols, rows);
  if (n > 0) emit(seq, (size_t)n, tmux, out);
}

void rolltui_image_kitty_delete(unsigned int id, int tmux, RolltuiStr* out) {
  char seq[64];
  const int n = snprintf(seq, sizeof seq, "\x1b_Ga=d,d=I,i=%u,q=2\x1b\\", id);
  if (n > 0) emit(seq, (size_t)n, tmux, out);
}

/* THE ROW AND COLUMN DIACRITICS of kitty's Unicode placeholders, in order: the n-th names row or column n. kitty's
 * rowcolumn-diacritics.txt, derived from Unicode 6.0.0's nonspacing marks of combining class 230 less the nineteen that compose
 * with a letter; a test checks each against this library's own Unicode tables. */
static const unsigned int kDiacritics[] = {
    0x305,   0x30D,   0x30E,   0x310,   0x312,   0x33D,   0x33E,   0x33F,   0x346,   0x34A,   0x34B,   0x34C,   0x350,   0x351,
    0x352,   0x357,   0x35B,   0x363,   0x364,   0x365,   0x366,   0x367,   0x368,   0x369,   0x36A,   0x36B,   0x36C,   0x36D,
    0x36E,   0x36F,   0x483,   0x484,   0x485,   0x486,   0x487,   0x592,   0x593,   0x594,   0x595,   0x597,   0x598,   0x599,
    0x59C,   0x59D,   0x59E,   0x59F,   0x5A0,   0x5A1,   0x5A8,   0x5A9,   0x5AB,   0x5AC,   0x5AF,   0x5C4,   0x610,   0x611,
    0x612,   0x613,   0x614,   0x615,   0x616,   0x617,   0x657,   0x658,   0x659,   0x65A,   0x65B,   0x65D,   0x65E,   0x6D6,
    0x6D7,   0x6D8,   0x6D9,   0x6DA,   0x6DB,   0x6DC,   0x6DF,   0x6E0,   0x6E1,   0x6E2,   0x6E4,   0x6E7,   0x6E8,   0x6EB,
    0x6EC,   0x730,   0x732,   0x733,   0x735,   0x736,   0x73A,   0x73D,   0x73F,   0x740,   0x741,   0x743,   0x745,   0x747,
    0x749,   0x74A,   0x7EB,   0x7EC,   0x7ED,   0x7EE,   0x7EF,   0x7F0,   0x7F1,   0x7F3,   0x816,   0x817,   0x818,   0x819,
    0x81B,   0x81C,   0x81D,   0x81E,   0x81F,   0x820,   0x821,   0x822,   0x823,   0x825,   0x826,   0x827,   0x829,   0x82A,
    0x82B,   0x82C,   0x82D,   0x951,   0x953,   0x954,   0xF82,   0xF83,   0xF86,   0xF87,   0x135D,  0x135E,  0x135F,  0x17DD,
    0x193A,  0x1A17,  0x1A75,  0x1A76,  0x1A77,  0x1A78,  0x1A79,  0x1A7A,  0x1A7B,  0x1A7C,  0x1B6B,  0x1B6D,  0x1B6E,  0x1B6F,
    0x1B70,  0x1B71,  0x1B72,  0x1B73,  0x1CD0,  0x1CD1,  0x1CD2,  0x1CDA,  0x1CDB,  0x1CE0,  0x1DC0,  0x1DC1,  0x1DC3,  0x1DC4,
    0x1DC5,  0x1DC6,  0x1DC7,  0x1DC8,  0x1DC9,  0x1DCB,  0x1DCC,  0x1DD1,  0x1DD2,  0x1DD3,  0x1DD4,  0x1DD5,  0x1DD6,  0x1DD7,
    0x1DD8,  0x1DD9,  0x1DDA,  0x1DDB,  0x1DDC,  0x1DDD,  0x1DDE,  0x1DDF,  0x1DE0,  0x1DE1,  0x1DE2,  0x1DE3,  0x1DE4,  0x1DE5,
    0x1DE6,  0x1DFE,  0x20D0,  0x20D1,  0x20D4,  0x20D5,  0x20D6,  0x20D7,  0x20DB,  0x20DC,  0x20E1,  0x20E7,  0x20E9,  0x20F0,
    0x2CEF,  0x2CF0,  0x2CF1,  0x2DE0,  0x2DE1,  0x2DE2,  0x2DE3,  0x2DE4,  0x2DE5,  0x2DE6,  0x2DE7,  0x2DE8,  0x2DE9,  0x2DEA,
    0x2DEB,  0x2DEC,  0x2DED,  0x2DEE,  0x2DEF,  0x2DF0,  0x2DF1,  0x2DF2,  0x2DF3,  0x2DF4,  0x2DF5,  0x2DF6,  0x2DF7,  0x2DF8,
    0x2DF9,  0x2DFA,  0x2DFB,  0x2DFC,  0x2DFD,  0x2DFE,  0x2DFF,  0xA66F,  0xA67C,  0xA67D,  0xA6F0,  0xA6F1,  0xA8E0,  0xA8E1,
    0xA8E2,  0xA8E3,  0xA8E4,  0xA8E5,  0xA8E6,  0xA8E7,  0xA8E8,  0xA8E9,  0xA8EA,  0xA8EB,  0xA8EC,  0xA8ED,  0xA8EE,  0xA8EF,
    0xA8F0,  0xA8F1,  0xAAB0,  0xAAB2,  0xAAB3,  0xAAB7,  0xAAB8,  0xAABE,  0xAABF,  0xAAC1,  0xFE20,  0xFE21,  0xFE22,  0xFE23,
    0xFE24,  0xFE25,  0xFE26,  0x10A0F, 0x10A38, 0x1D185, 0x1D186, 0x1D187, 0x1D188, 0x1D189, 0x1D1AA, 0x1D1AB, 0x1D1AC, 0x1D1AD,
    0x1D242, 0x1D243, 0x1D244};
#define DIACRITIC_COUNT (sizeof kDiacritics / sizeof kDiacritics[0])

size_t rolltui_image_diacritic_count(void) { return DIACRITIC_COUNT; }
unsigned int rolltui_image_diacritic(size_t i) { return i < DIACRITIC_COUNT ? kDiacritics[i] : 0; }

static size_t utf8(unsigned int cp, char* out) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

/* ---- sixel --------------------------------------------------------------------------------------- */

/* The palette: a 6 x 7 x 6 cube (252 colours, green the finest step, which the eye resolves best) and four greys. Fixed, so the
 * same picture is the same bytes, and no colour search: a pixel's register is arithmetic. */
static int cube_r(int v) { return (v * 5 + 127) / 255; }
static int cube_g(int v) { return (v * 6 + 127) / 255; }
static int cube_b(int v) { return (v * 5 + 127) / 255; }
static int reg_of(const unsigned char* p) { return cube_r(p[0]) * 42 + cube_g(p[1]) * 6 + cube_b(p[2]); }

void rolltui_image_sixel(const RolltuiImage* img, int px_w, int px_h, RolltuiStr* out) {
  unsigned char* px;
  unsigned char* used;
  char buf[64];
  int band, x, r;
  if (!img || px_w <= 0 || px_h <= 0) return;
  px = (unsigned char*)rolltui_mem_alloc((size_t)px_w * px_h * 4);
  rolltui_image_scale(img, px_w, px_h, px);
  used = (unsigned char*)rolltui_mem_alloc(252);
  /* P2 = 1: a pixel no colour is set for stays the cell's own background, so a transparent picture is transparent */
  rolltui_str_append(out, "\x1bP0;1;0q", sizeof "\x1bP0;1;0q" - 1);
  snprintf(buf, sizeof buf, "\"1;1;%d;%d", px_w, px_h);
  rolltui_str_append(out, buf, strlen(buf));
  for (r = 0; r < 252; ++r) {
    const int ri = r / 42, gi = (r / 6) % 7, bi = r % 6;
    snprintf(buf, sizeof buf, "#%d;2;%d;%d;%d", r, ri * 100 / 5, gi * 100 / 6, bi * 100 / 5);
    rolltui_str_append(out, buf, strlen(buf));
  }
  for (band = 0; band < px_h; band += 6) {
    int first = 1;
    memset(used, 0, 252);
    for (x = 0; x < px_w; ++x) { /* the registers this band uses */
      int k;
      for (k = 0; k < 6 && band + k < px_h; ++k) {
        const unsigned char* p = px + ((size_t)(band + k) * px_w + x) * 4;
        if (p[3] >= 128) used[reg_of(p)] = 1;
      }
    }
    for (r = 0; r < 252; ++r) {
      int run = 0;
      char prev = 0;
      if (!used[r]) continue;
      if (!first) rolltui_str_append(out, "$", 1); /* back to the band's start for the next colour */
      first = 0;
      snprintf(buf, sizeof buf, "#%d", r);
      rolltui_str_append(out, buf, strlen(buf));
      for (x = 0; x <= px_w; ++x) {
        char c = 0;
        if (x < px_w) {
          int k, bits = 0;
          for (k = 0; k < 6 && band + k < px_h; ++k) {
            const unsigned char* p = px + ((size_t)(band + k) * px_w + x) * 4;
            if (p[3] >= 128 && reg_of(p) == r) bits |= 1 << k;
          }
          c = (char)('?' + bits);
        }
        if (x < px_w && c == prev) {
          ++run;
          continue;
        }
        if (run > 3) { /* a run is said once with its count */
          snprintf(buf, sizeof buf, "!%d%c", run, prev);
          rolltui_str_append(out, buf, strlen(buf));
        } else {
          int i;
          for (i = 0; i < run; ++i) rolltui_str_append(out, &prev, 1);
        }
        prev = c;
        run = 1;
      }
    }
    rolltui_str_append(out, "-", 1); /* the next band */
  }
  rolltui_str_append(out, "\x1b\\", 2);
  rolltui_mem_free(used);
  rolltui_mem_free(px);
}

/* ---- iTerm2 -------------------------------------------------------------------------------------- */

/* A 32-bit BMP of the picture: what iTerm2 is sent for one that was not decoded from a file it reads itself. */
static void bmp(const RolltuiImage* img, RolltuiStr* out) {
  const size_t row = (size_t)img->w * 4, data = row * img->h, total = 54 + data;
  unsigned char h[54];
  int y, x;
  memset(h, 0, sizeof h);
  h[0] = 'B';
  h[1] = 'M';
  h[2] = (unsigned char)total, h[3] = (unsigned char)(total >> 8), h[4] = (unsigned char)(total >> 16), h[5] = (unsigned char)(total >> 24);
  h[10] = 54;
  h[14] = 40;
  h[18] = (unsigned char)img->w, h[19] = (unsigned char)(img->w >> 8), h[20] = (unsigned char)(img->w >> 16);
  h[22] = (unsigned char)img->h, h[23] = (unsigned char)(img->h >> 8), h[24] = (unsigned char)(img->h >> 16);
  h[26] = 1;
  h[28] = 32;
  rolltui_str_append(out, (const char*)h, sizeof h);
  for (y = img->h - 1; y >= 0; --y) /* bottom-up, BGRA */
    for (x = 0; x < img->w; ++x) {
      const unsigned char* p = img->rgba + ((size_t)y * img->w + x) * 4;
      const char q[4] = {(char)p[2], (char)p[1], (char)p[0], (char)p[3]};
      rolltui_str_append(out, q, 4);
    }
}

void rolltui_image_iterm2(const RolltuiImage* img, int cols, int rows, int tmux, RolltuiStr* out) {
  RolltuiStr file, data, seq;
  char head[128];
  if (!img) return;
  memset(&file, 0, sizeof file);
  memset(&data, 0, sizeof data);
  memset(&seq, 0, sizeof seq);
  if (img->encoded.n) rolltui_str_set(&file, img->encoded.p, img->encoded.n);
  else bmp(img, &file);
  b64((const unsigned char*)file.p, file.n, &data);
  snprintf(head, sizeof head, "\x1b]1337;File=inline=1;size=%zu;width=%d;height=%d;preserveAspectRatio=1:", file.n, cols, rows);
  rolltui_str_append(&seq, head, strlen(head));
  rolltui_str_append(&seq, data.p, data.n);
  rolltui_str_append(&seq, "\x07", 1);
  emit(seq.p, seq.n, tmux, out);
  rolltui_str_free(&file);
  rolltui_str_free(&data);
  rolltui_str_free(&seq);
}

/* ---- into the frame ------------------------------------------------------------------------------ */

static RolltuiStyleColorRaw rgb(unsigned r, unsigned g, unsigned b) {
  RolltuiStyleColorRaw c;
  memset(&c, 0, sizeof c);
  c.kind = 2;
  c.r = (unsigned char)r;
  c.g = (unsigned char)g;
  c.b = (unsigned char)b;
  return c;
}

/* A pixel over the ground, by its alpha: a transparent picture shows the window through it. */
static RolltuiStyleColorRaw over_ground(const unsigned char* p, RolltuiStyleColorRaw ground) {
  const unsigned a = p[3];
  unsigned gr = 0, gg = 0, gb = 0;
  if (a == 255 || ground.kind != 2) return rgb(p[0], p[1], p[2]);
  gr = ground.r;
  gg = ground.g;
  gb = ground.b;
  return rgb((p[0] * a + gr * (255 - a) + 127) / 255, (p[1] * a + gg * (255 - a) + 127) / 255, (p[2] * a + gb * (255 - a) + 127) / 255);
}

static void draw_blocks(RolltuiFrame* f, const RolltuiImage* img, int x0, int y0, int cols, int rows, RolltuiStyle ground,
                        int ambiguous_wide) {
  /* two pixels to a cell (the upper half block, U+2580, its foreground the top and its background the bottom), or one where the
   * terminal draws that glyph two cells wide */
  const int per = ambiguous_wide ? 1 : 2;
  unsigned char* px = (unsigned char*)rolltui_mem_alloc((size_t)cols * rows * per * 4);
  int x, y;
  rolltui_image_scale(img, cols, rows * per, px);
  for (y = 0; y < rows; ++y)
    for (x = 0; x < cols; ++x) {
      const unsigned char* top = px + ((size_t)(y * per) * cols + x) * 4;
      RolltuiStyle st = ground;
      if (per == 2) {
        const unsigned char* bot = px + ((size_t)(y * 2 + 1) * cols + x) * 4;
        if (top[3] < 8 && bot[3] < 8) {
          rolltui_frame_put(f, x0 + x, y0 + y, " ", 1, 1, ground, 0);
          continue;
        }
        st.fg = top[3] < 8 ? ground.bg : over_ground(top, ground.bg);
        st.bg = bot[3] < 8 ? ground.bg : over_ground(bot, ground.bg);
        st.bold = st.italic = st.underline = st.dim = st.reverse = st.strike = 0;
        rolltui_frame_put(f, x0 + x, y0 + y, "\xe2\x96\x80", 3, 1, st, 0);
      } else {
        st.bg = top[3] < 8 ? ground.bg : over_ground(top, ground.bg);
        rolltui_frame_put(f, x0 + x, y0 + y, " ", 1, 1, st, 0);
      }
    }
  rolltui_mem_free(px);
}

static void draw_placeholders(RolltuiFrame* f, const RolltuiImage* img, int x0, int y0, int cols, int rows, RolltuiStyle ground) {
  RolltuiStyle st = ground;
  int x, y;
  st.fg = rgb((img->id >> 16) & 255, (img->id >> 8) & 255, img->id & 255);
  st.bold = st.italic = st.underline = st.dim = st.reverse = st.strike = 0;
  for (y = 0; y < rows; ++y)
    for (x = 0; x < cols; ++x) {
      char cell[16];
      size_t n = utf8(0x10EEEE, cell);
      n += utf8(kDiacritics[(size_t)y < DIACRITIC_COUNT ? (size_t)y : DIACRITIC_COUNT - 1], cell + n);
      n += utf8(kDiacritics[(size_t)x < DIACRITIC_COUNT ? (size_t)x : DIACRITIC_COUNT - 1], cell + n);
      rolltui_frame_put(f, x0 + x, y0 + y, cell, n, 1, st, 0);
    }
}

/* The way a picture would be drawn now, and the cells and pixels it would take in `cols` x `rows` (and how many rows a frame of
 * `frame_h` leaves a sixel, which is kept off the last row). */
static int plan(const RolltuiImage* img, int cols, int rows, int* uc, int* ur, int* pw, int* ph) {
  int protocol = rolltui_termfacts_active_images();
  int cw = 0, ch = 0;
  const int aw = rolltui_termfacts_active_wide() == 1;
  rolltui_termfacts_active_cell_pixels(&cw, &ch);
  if (protocol == ROLLTUI_IMAGES_KITTY && (cols > (int)DIACRITIC_COUNT || rows > (int)DIACRITIC_COUNT)) protocol = ROLLTUI_IMAGES_BLOCKS;
  if (protocol <= 0) protocol = ROLLTUI_IMAGES_BLOCKS;
  if (protocol == ROLLTUI_IMAGES_BLOCKS) {
    /* in blocks a cell is two pixels tall and one wide: fitted in those units, the picture keeps its shape */
    rolltui_image_fit(img, cols, rows, 1, aw ? 1 : 2, uc, ur, pw, ph);
    if (!aw) *ur = (*ph + 1) / 2;
    if (*ur > rows) *ur = rows;
  } else {
    rolltui_image_fit(img, cols, rows, cw, ch, uc, ur, pw, ph);
  }
  return protocol;
}

void rolltui_image_cells(const RolltuiImage* img, int cols, int rows, int* used_cols, int* used_rows) {
  int uc = 0, ur = 0, pw = 0, ph = 0;
  if (img && cols > 0 && rows > 0) plan(img, cols, rows, &uc, &ur, &pw, &ph);
  if (used_cols) *used_cols = uc;
  if (used_rows) *used_rows = ur;
}

void rolltui_frame_image(RolltuiFrame* f, RolltuiRect area, const RolltuiImage* img, RolltuiStyle ground, RolltuiRect* used) {
  int protocol, uc = 0, ur = 0, pw = 0, ph = 0;
  RolltuiRect u;
  memset(&u, 0, sizeof u);
  u.x = area.x;
  u.y = area.y;
  if (used) *used = u;
  if (!f || !img || area.w <= 0 || area.h <= 0) return;
  /* A SIXEL ON THE LAST ROW scrolls the screen when the terminal moves the cursor below it: kept off that row. */
  if (rolltui_termfacts_active_images() == ROLLTUI_IMAGES_SIXEL && area.y + area.h >= rolltui_frame_height(f)) {
    area.h = rolltui_frame_height(f) - 1 - area.y;
    if (area.h <= 0) return;
  }
  protocol = plan(img, area.w, area.h, &uc, &ur, &pw, &ph);
  if (protocol == ROLLTUI_IMAGES_BLOCKS) {
    draw_blocks(f, img, area.x, area.y, uc, ur, ground, rolltui_termfacts_active_wide() == 1);
  } else {
    RolltuiRect r;
    if (protocol == ROLLTUI_IMAGES_KITTY) {
      draw_placeholders(f, img, area.x, area.y, uc, ur, ground);
    } else { /* ITERM2 and SIXEL: blanks the terminal draws the picture over */
      int x, y;
      for (y = 0; y < ur; ++y)
        for (x = 0; x < uc; ++x) rolltui_frame_put(f, area.x + x, area.y + y, " ", 1, 1, ground, 0);
    }
    r.x = area.x;
    r.y = area.y;
    r.w = uc;
    r.h = ur;
    rolltui_frame_add_image(f, img, r, protocol, pw, ph, ground);
  }
  u.w = uc;
  u.h = ur;
  if (used) *used = u;
}
