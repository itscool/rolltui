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
#include "rolltui/c/rolltui_os.h"
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
  unsigned char filter; /* ROLLTUI_IMAGE_SMOOTH or _NEAREST: how it is scaled */
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

/* A JPEG'S EXIF ORIENTATION (1 to 8; 1 as it is stored): how a camera held sideways says to show what it stored. */
static unsigned rd16(const unsigned char* p, int le) { return le ? (unsigned)(p[0] | p[1] << 8) : (unsigned)(p[0] << 8 | p[1]); }
static unsigned long rd32(const unsigned char* p, int le) {
  return le ? (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24
            : (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | (unsigned long)p[3];
}
static int jpeg_orientation(const unsigned char* b, size_t n) {
  size_t at = 2;
  if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return 1;
  while (at + 4 <= n && b[at] == 0xFF) {
    const unsigned marker = b[at + 1];
    const size_t seg = (size_t)rd16(b + at + 2, 0);
    if (marker == 0xDA || seg < 2) break; /* the picture's data: no EXIF past here */
    if (marker == 0xE1 && at + 2 + seg <= n && seg >= 16 && memcmp(b + at + 4, "Exif\0\0", 6) == 0) {
      const unsigned char* t = b + at + 10; /* the TIFF header */
      const size_t tn = seg - 8;
      int le;
      unsigned long ifd;
      unsigned count, i;
      if (tn < 8 || !(memcmp(t, "II", 2) == 0 || memcmp(t, "MM", 2) == 0)) return 1;
      le = t[0] == 'I';
      ifd = rd32(t + 4, le);
      if (ifd + 2 > tn) return 1;
      count = rd16(t + ifd, le);
      for (i = 0; i < count && ifd + 2 + (i + 1) * 12 <= tn; ++i) {
        const unsigned char* e = t + ifd + 2 + i * 12;
        if (rd16(e, le) == 0x0112) {
          const unsigned v = rd16(e + 8, le);
          return v >= 1 && v <= 8 ? (int)v : 1;
        }
      }
      return 1;
    }
    at += 2 + seg;
  }
  return 1;
}
/* THE PIXELS TURNED as orientation `o` says, into a new buffer (the old one freed); `*w`, `*h` the shown size. */
static unsigned char* orient(unsigned char* px, int* w, int* h, int o) {
  const int sw = *w, sh = *h, swap = o >= 5;
  const int dw = swap ? sh : sw, dh = swap ? sw : sh;
  unsigned char* out;
  int dx, dy;
  if (o <= 1 || o > 8) return px;
  out = (unsigned char*)rolltui_mem_alloc((size_t)dw * (size_t)dh * 4);
  for (dy = 0; dy < dh; ++dy)
    for (dx = 0; dx < dw; ++dx) {
      int sx = dx, sy = dy;
      switch (o) {
        case 2: sx = sw - 1 - dx; break;
        case 3: sx = sw - 1 - dx, sy = sh - 1 - dy; break;
        case 4: sy = sh - 1 - dy; break;
        case 5: sx = dy, sy = dx; break;
        case 6: sx = dy, sy = sh - 1 - dx; break;
        case 7: sx = sw - 1 - dy, sy = sh - 1 - dx; break;
        case 8: sx = sw - 1 - dy, sy = dx; break;
        default: break;
      }
      memcpy(out + ((size_t)dy * dw + dx) * 4, px + ((size_t)sy * sw + sx) * 4, 4);
    }
  stbi_image_free(px);
  *w = dw;
  *h = dh;
  return out;
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
  px = orient(px, &w, &h, jpeg_orientation(bytes, len)); /* a photo shown the way its camera was held */
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
void rolltui_image_set_filter(RolltuiImage* img, int filter) {
  if (img) img->filter = (unsigned char)(filter == ROLLTUI_IMAGE_NEAREST ? ROLLTUI_IMAGE_NEAREST : ROLLTUI_IMAGE_SMOOTH);
}

void rolltui_image_scale(const RolltuiImage* img, int dw, int dh, unsigned char* out) {
  int x, y;
  if (img->filter == ROLLTUI_IMAGE_NEAREST) { /* each pixel the one under its centre: pixel art kept hard-edged */
    for (y = 0; y < dh; ++y)
      for (x = 0; x < dw; ++x) {
        int sx = (int)(((long long)x * 2 + 1) * img->w / (2LL * dw)), sy = (int)(((long long)y * 2 + 1) * img->h / (2LL * dh));
        if (sx >= img->w) sx = img->w - 1;
        if (sy >= img->h) sy = img->h - 1;
        memcpy(out + ((size_t)y * dw + x) * 4, img->rgba + ((size_t)sy * img->w + sx) * 4, 4);
      }
    return;
  }
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

void rolltui_image_kitty_upload(const RolltuiImage* img, int tmux, int by_file, RolltuiStr* out, RolltuiStr* file) {
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
  if (by_file) { /* A KITTY ON THIS MACHINE reads the pixels from a temp file and deletes it: no megabytes through the escapes */
    RolltuiStr path;
    memset(&path, 0, sizeof path);
    if (rolltui_os_temp_file("rolltui-tty-graphics-protocol", px, (size_t)w * h * 4, &path)) {
      if (px != img->rgba) rolltui_mem_free(px);
      b64((const unsigned char*)path.p, path.n, &data);
      snprintf(head, sizeof head, "\x1b_Ga=t,t=t,f=32,s=%d,v=%d,i=%u,q=2;", w, h, img->id);
      rolltui_str_append(&seq, head, strlen(head));
      rolltui_str_append(&seq, data.p, data.n);
      rolltui_str_append(&seq, "\x1b\\", 2);
      emit(seq.p, seq.n, tmux, out);
      if (file) rolltui_str_set(file, path.p, path.n);
      rolltui_str_free(&path);
      rolltui_str_free(&data);
      rolltui_str_free(&seq);
      return;
    }
    rolltui_str_free(&path); /* no temp file to be had: in the escapes */
  }
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

/* THE PALETTE FITTED TO THE PICTURE: median cut over its opaque pixels, at most SIXEL_REGS colours, each the mean of the pixels in
 * its box. The same picture is the same bytes (nothing in it is random), and a pixel finds its register through a cache of 15-bit
 * colours, so the search is paid once per colour, not once per pixel. */
#define SIXEL_REGS 255
typedef struct Rgb3 {
  unsigned char c[3];
} Rgb3;
static int by_r(const void* a, const void* b) { return (int)((const Rgb3*)a)->c[0] - (int)((const Rgb3*)b)->c[0]; }
static int by_g(const void* a, const void* b) { return (int)((const Rgb3*)a)->c[1] - (int)((const Rgb3*)b)->c[1]; }
static int by_b(const void* a, const void* b) { return (int)((const Rgb3*)a)->c[2] - (int)((const Rgb3*)b)->c[2]; }
typedef struct CutBox {
  size_t lo, hi;
} CutBox;
static int box_spread(const Rgb3* v, CutBox b, int* channel) {
  int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0}, k, best = -1;
  size_t i;
  for (i = b.lo; i < b.hi; ++i)
    for (k = 0; k < 3; ++k) {
      if (v[i].c[k] < lo[k]) lo[k] = v[i].c[k];
      if (v[i].c[k] > hi[k]) hi[k] = v[i].c[k];
    }
  for (k = 0; k < 3; ++k)
    if (hi[k] - lo[k] > best) best = hi[k] - lo[k], *channel = k;
  return best;
}
/* `pal` filled (`*n` colours) from the opaque pixels of `px` (`count` of them, RGBA). */
static void fit_palette(const unsigned char* px, size_t count, unsigned char pal[SIXEL_REGS][3], int* n) {
  Rgb3* v;
  CutBox boxes[SIXEL_REGS];
  size_t m = 0, i, step;
  int nb = 1, k;
  v = (Rgb3*)rolltui_mem_alloc((count ? count : 1) * sizeof *v);
  step = count > 262144 ? count / 262144 : 1; /* a sample of a very large picture is enough to cut boxes from */
  for (i = 0; i < count; i += step)
    if (px[i * 4 + 3] >= 128) memcpy(v[m++].c, px + i * 4, 3);
  if (m == 0) {
    pal[0][0] = pal[0][1] = pal[0][2] = 0;
    *n = 1;
    rolltui_mem_free(v);
    return;
  }
  boxes[0].lo = 0;
  boxes[0].hi = m;
  while (nb < SIXEL_REGS) { /* the widest box cut at its median, until there are enough or none can be cut */
    int pick = -1, ch = 0, spread = 0;
    for (k = 0; k < nb; ++k) {
      int c = 0;
      const int s = boxes[k].hi - boxes[k].lo > 1 ? box_spread(v, boxes[k], &c) : 0;
      if (s > spread) spread = s, pick = k, ch = c;
    }
    if (pick < 0) break;
    qsort(v + boxes[pick].lo, boxes[pick].hi - boxes[pick].lo, sizeof *v, ch == 0 ? by_r : ch == 1 ? by_g : by_b);
    {
      const size_t mid = boxes[pick].lo + (boxes[pick].hi - boxes[pick].lo) / 2;
      boxes[nb].lo = mid;
      boxes[nb].hi = boxes[pick].hi;
      boxes[pick].hi = mid;
      ++nb;
    }
  }
  for (k = 0; k < nb; ++k) {
    unsigned long sum[3] = {0, 0, 0};
    const size_t cnt = boxes[k].hi - boxes[k].lo;
    int c;
    for (i = boxes[k].lo; i < boxes[k].hi; ++i)
      for (c = 0; c < 3; ++c) sum[c] += v[i].c[c];
    for (c = 0; c < 3; ++c) pal[k][c] = (unsigned char)((sum[c] + cnt / 2) / (cnt ? cnt : 1));
  }
  *n = nb;
  rolltui_mem_free(v);
}
/* The register nearest `p`, through `cache` (32768 entries, -1 not yet asked). */
static int reg_of(const unsigned char* p, unsigned char pal[SIXEL_REGS][3], int n, short* cache) {
  const int key = (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3);
  int k, best = 0;
  long bd = -1;
  if (cache[key] >= 0) return cache[key];
  for (k = 0; k < n; ++k) {
    const long dr = (long)p[0] - pal[k][0], dg = (long)p[1] - pal[k][1], db = (long)p[2] - pal[k][2];
    const long d = dr * dr * 3 + dg * dg * 4 + db * db * 2; /* green weighs most, as the eye resolves it best */
    if (bd < 0 || d < bd) bd = d, best = k;
  }
  cache[key] = (short)best;
  return best;
}

void rolltui_image_sixel(const RolltuiImage* img, int px_w, int px_h, RolltuiStr* out) {
  unsigned char* px;
  unsigned char* used;
  unsigned char* reg;
  unsigned char (*pal)[3];
  short* cache;
  int npal = 0;
  char buf[64];
  int band, x, r;
  if (!img || px_w <= 0 || px_h <= 0) return;
  px = (unsigned char*)rolltui_mem_alloc((size_t)px_w * px_h * 4);
  rolltui_image_scale(img, px_w, px_h, px);
  pal = (unsigned char(*)[3])rolltui_mem_alloc(SIXEL_REGS * 3);
  fit_palette(px, (size_t)px_w * px_h, pal, &npal);
  cache = (short*)rolltui_mem_alloc(32768 * sizeof *cache);
  for (r = 0; r < 32768; ++r) cache[r] = -1;
  reg = (unsigned char*)rolltui_mem_alloc((size_t)px_w * px_h); /* each pixel's register, found once */
  for (r = 0; r < px_w * px_h; ++r)
    reg[r] = (unsigned char)(px[(size_t)r * 4 + 3] >= 128 ? reg_of(px + (size_t)r * 4, pal, npal, cache) : 0);
  used = (unsigned char*)rolltui_mem_alloc(SIXEL_REGS);
  /* P2 = 1: a pixel no colour is set for stays the cell's own background, so a transparent picture is transparent */
  rolltui_str_append(out, "\x1bP0;1;0q", sizeof "\x1bP0;1;0q" - 1);
  snprintf(buf, sizeof buf, "\"1;1;%d;%d", px_w, px_h);
  rolltui_str_append(out, buf, strlen(buf));
  for (r = 0; r < npal; ++r) { /* the registers, in percent */
    snprintf(buf, sizeof buf, "#%d;2;%d;%d;%d", r, (pal[r][0] * 100 + 127) / 255, (pal[r][1] * 100 + 127) / 255, (pal[r][2] * 100 + 127) / 255);
    rolltui_str_append(out, buf, strlen(buf));
  }
  for (band = 0; band < px_h; band += 6) {
    int first = 1;
    memset(used, 0, SIXEL_REGS);
    for (x = 0; x < px_w; ++x) { /* the registers this band uses */
      int k;
      for (k = 0; k < 6 && band + k < px_h; ++k) {
        const size_t at = (size_t)(band + k) * px_w + x;
        if (px[at * 4 + 3] >= 128) used[reg[at]] = 1;
      }
    }
    for (r = 0; r < npal; ++r) {
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
            const size_t at = (size_t)(band + k) * px_w + x;
            if (px[at * 4 + 3] >= 128 && reg[at] == r) bits |= 1 << k;
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
  rolltui_mem_free(reg);
  rolltui_mem_free(cache);
  rolltui_mem_free(pal);
  rolltui_mem_free(px);
}

/* ---- iTerm2 -------------------------------------------------------------------------------------- */

/* A PNG OF THE PICTURE, its alpha kept: what iTerm2 is sent for one that was not decoded from a file it reads itself (a host's
 * pixels, a BMP, a TGA). Stored, not compressed: a valid PNG every reader takes, made without a compressor. */
static void put32(RolltuiStr* out, unsigned long v) {
  const char b[4] = {(char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v};
  rolltui_str_append(out, b, 4);
}
static unsigned long crc_of(const unsigned long* table, const unsigned char* p, size_t n, unsigned long c) {
  size_t i;
  for (i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void png_chunk(RolltuiStr* out, const unsigned long* table, const char* type, const unsigned char* data, size_t n) {
  unsigned long c = 0xFFFFFFFFul;
  put32(out, (unsigned long)n);
  rolltui_str_append(out, type, 4);
  if (n) rolltui_str_append(out, (const char*)data, n);
  c = crc_of(table, (const unsigned char*)type, 4, c);
  if (n) c = crc_of(table, data, n, c);
  put32(out, c ^ 0xFFFFFFFFul);
}
static void png(const RolltuiImage* img, RolltuiStr* out) {
  unsigned long table[256], a = 1, b = 0;
  const size_t row = (size_t)img->w * 4 + 1, raw_n = row * (size_t)img->h;
  RolltuiStr z;
  unsigned char ihdr[13];
  size_t i, at = 0;
  int y;
  for (i = 0; i < 256; ++i) { /* the CRC's table, made here: no state outlives the call */
    unsigned long c = (unsigned long)i;
    int k;
    for (k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320ul ^ (c >> 1) : c >> 1;
    table[i] = c;
  }
  memset(&z, 0, sizeof z);
  rolltui_str_append(&z, "\x78\x01", 2); /* zlib: deflate, no dictionary */
  for (y = 0; y < img->h; ++y) { /* each row: filter 0, then its pixels; written in stored blocks as it goes */
    const unsigned char* src = img->rgba + (size_t)y * img->w * 4;
    size_t k;
    for (k = 0; k < row; ++k) {
      const unsigned char byte = k == 0 ? 0 : src[k - 1];
      if (at % 65535 == 0) { /* a stored block's head: final or not, its length and that length's complement */
        const size_t left = raw_n - at, len = left < 65535 ? left : 65535;
        const char head[5] = {(char)(left <= 65535 ? 1 : 0), (char)(len & 0xFF), (char)(len >> 8), (char)(~len & 0xFF),
                              (char)((~len >> 8) & 0xFF)};
        rolltui_str_append(&z, head, 5);
      }
      rolltui_str_append(&z, (const char*)&byte, 1);
      a = (a + byte) % 65521;
      b = (b + a) % 65521;
      ++at;
    }
  }
  put32(&z, b << 16 | a); /* the zlib check */
  rolltui_str_append(out, "\x89PNG\r\n\x1a\n", 8);
  ihdr[0] = (unsigned char)(img->w >> 24), ihdr[1] = (unsigned char)(img->w >> 16), ihdr[2] = (unsigned char)(img->w >> 8),
  ihdr[3] = (unsigned char)img->w;
  ihdr[4] = (unsigned char)(img->h >> 24), ihdr[5] = (unsigned char)(img->h >> 16), ihdr[6] = (unsigned char)(img->h >> 8),
  ihdr[7] = (unsigned char)img->h;
  ihdr[8] = 8; /* bits a channel */
  ihdr[9] = 6; /* RGBA */
  ihdr[10] = ihdr[11] = ihdr[12] = 0;
  png_chunk(out, table, "IHDR", ihdr, sizeof ihdr);
  png_chunk(out, table, "IDAT", (const unsigned char*)z.p, z.n);
  png_chunk(out, table, "IEND", NULL, 0);
  rolltui_str_free(&z);
}

void rolltui_image_iterm2(const RolltuiImage* img, int cols, int rows, int tmux, RolltuiStr* out) {
  RolltuiStr file, data, seq;
  char head[128];
  if (!img) return;
  memset(&file, 0, sizeof file);
  memset(&data, 0, sizeof data);
  memset(&seq, 0, sizeof seq);
  if (img->encoded.n) rolltui_str_set(&file, img->encoded.p, img->encoded.n);
  else png(img, &file);
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
        st.bold = st.italic = st.underline = st.dim = st.reverse = st.strike = st.blink = st.hidden = st.overline = 0;
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
  st.bold = st.italic = st.underline = st.dim = st.reverse = st.strike = st.blink = st.hidden = st.overline = 0;
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
