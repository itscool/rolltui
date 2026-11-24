/* rolltui/c/rolltui_feedback.c — what the app tells the person: the gauge, the spinner, the message log and its toasts. See
 * rolltui_feedback.h; the public half is in rolltui.h's widgets book. */
#include "rolltui/c/rolltui_feedback.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ---- the gauge ---------------------------------------------------------------------------- */

void rolltui_gauge_release(RolltuiGauge* g) {
  if (g) rolltui_str_free(&g->label);
}

/* U+280B, the first picture of the shipped spinners: what a spinner shows before its theme turns it, and in a theme that does not. */
#define SPINNER_GLYPH "\xe2\xa0\x8b"

void rolltui_spinner_draw(RolltuiFrame* f, int x, int y, RolltuiStyle style, unsigned long long since_ms) {
  rolltui_frame_put(f, x, y, SPINNER_GLYPH, sizeof SPINNER_GLYPH - 1, 1, style, 0);
  rolltui_frame_mark(f, x, y, 1, ROLLTUI_EFFECT_STATE_WAITING, since_ms, 0.0);
}

/* A cell's worth of bar, by how many eighths of it are filled: the left blocks, U+258F (one eighth) to U+2588 (all). */
static const char* const kEighths[9] = {"",
                                        "\xe2\x96\x8f", "\xe2\x96\x8e", "\xe2\x96\x8d", "\xe2\x96\x8c",
                                        "\xe2\x96\x8b", "\xe2\x96\x8a", "\xe2\x96\x89", "\xe2\x96\x88"};
#define TRACK_GLYPH "\xe2\x94\x80" /* ─ */

/* The bar takes at least this many cells before the label is given room. */
#define GAUGE_MIN_BAR 8

int rolltui_gauge_draw_roles(RolltuiFrame* f, RolltuiDrawScratch* draw, int x, int y, int width, const RolltuiGauge* g,
                             const RolltuiStyle* styles, const RolltuiBuiltinRoles* roles, int ambiguous_wide) {
  const char* label = g->label.p ? g->label.p : "";
  const size_t label_n = g->label.n;
  const RolltuiStyle fill = styles[g->failed ? roles->error : roles->gauge];
  const RolltuiStyle track = styles[roles->text_muted];
  char pct[8];
  int pn, bar, bx, lw, i, eighths;
  double fr;
  if (width <= 0) return 0;
  if (!(g->fraction >= 0)) { /* not known (a NaN too): a spinner and what is being done */
    rolltui_spinner_draw(f, x, y, styles[roles->gauge], g->since_ms);
    if (label_n == 0 || width < 3) return 1;
    return 2 + rolltui_frame_put_text(f, draw, x + 2, y, label, label_n, styles[roles->label], width - 2, ambiguous_wide, 0);
  }
  fr = g->fraction > 1 ? 1 : g->fraction;
  /* ROUNDED DOWN, so 100% is said only when it is done: 99.7% of a copy is not a finished copy. */
  pn = snprintf(pct, sizeof pct, "%d%%", (int)floor(fr * 100.0 + 1e-9));
  if (width < pn) return 0;
  bar = width - pn - 1;
  bx = x;
  lw = label_n ? rolltui_frame_text_width(draw, label, label_n, ambiguous_wide) : 0;
  if (lw > 0 && bar - (lw + 1) >= GAUGE_MIN_BAR) {
    rolltui_frame_put_text(f, draw, x, y, label, label_n, styles[roles->label], lw, ambiguous_wide, 0);
    bx = x + lw + 1;
    bar -= lw + 1;
  }
  eighths = (int)floor(fr * bar * 8.0 + 1e-9);
  for (i = 0; i < bar; ++i) {
    const int k = imax(0, imin(8, eighths - i * 8));
    if (ambiguous_wide) { /* every block and the track are East Asian Ambiguous: a half or more is a hash, less a dash */
      rolltui_frame_put(f, bx + i, y, k >= 4 ? "#" : "-", 1, 1, k >= 4 ? fill : track, 0);
    } else if (k == 0) {
      rolltui_frame_put(f, bx + i, y, TRACK_GLYPH, sizeof TRACK_GLYPH - 1, 1, track, 0);
    } else {
      rolltui_frame_put(f, bx + i, y, kEighths[k], strlen(kEighths[k]), 1, fill, 0);
    }
  }
  rolltui_frame_put_text(f, draw, x + width - pn, y, pct, (size_t)pn, styles[g->failed ? roles->error : roles->value], pn,
                         ambiguous_wide, 0);
  return width;
}

/* ---- the log ------------------------------------------------------------------------------- */

void rolltui_message_log_init(RolltuiMessageLog* log) {
  memset(log, 0, sizeof *log);
  log->draw = rolltui_draw_scratch_new();
  log->u = rolltui_u_scratch_new();
  log->wrap = rolltui_wrap_new();
}

void rolltui_message_log_release(RolltuiMessageLog* log) {
  size_t i;
  for (i = 0; i < log->cap; ++i) rolltui_str_free(&log->v[i].text);
  rolltui_mem_free(log->v);
  for (i = 0; i < ROLLTUI_TOASTS_MAX; ++i) rolltui_str_free(&log->toasts[i].text);
  rolltui_draw_scratch_free(log->draw);
  rolltui_u_scratch_free(log->u);
  rolltui_wrap_free(log->wrap);
  memset(log, 0, sizeof *log);
}

unsigned long long rolltui_toast_ms(int level) {
  return level == ROLLTUI_MESSAGE_ERROR ? 2ull * ROLLTUI_TOAST_MS : (unsigned long long)ROLLTUI_TOAST_MS;
}

void rolltui_message_log_post(RolltuiMessageLog* log, int level, const char* text, size_t len, unsigned long long now_ms) {
  RolltuiMessage* m;
  RolltuiToast* t;
  if (level < ROLLTUI_MESSAGE_INFO || level > ROLLTUI_MESSAGE_ERROR) level = ROLLTUI_MESSAGE_INFO;
  if (!text) len = 0;
  if (log->cap == 0) {
    log->v = (RolltuiMessage*)rolltui_grow_zeroed(log->v, &log->cap, ROLLTUI_MESSAGE_HISTORY, sizeof *log->v);
    log->cap = ROLLTUI_MESSAGE_HISTORY;
  }
  if (log->n < log->cap) {
    m = &log->v[(log->head + log->n) % log->cap];
    ++log->n;
  } else { /* full: the oldest gives its slot to the newest */
    m = &log->v[log->head];
    log->head = (log->head + 1) % log->cap;
  }
  m->level = level;
  m->at_ms = now_ms;
  rolltui_str_set(&m->text, text, len);
  if (log->toast_n == ROLLTUI_TOASTS_MAX) { /* the oldest toast makes way */
    RolltuiStr keep = log->toasts[0].text;
    memmove(log->toasts, log->toasts + 1, (ROLLTUI_TOASTS_MAX - 1) * sizeof *log->toasts);
    memset(&log->toasts[ROLLTUI_TOASTS_MAX - 1], 0, sizeof *log->toasts);
    log->toasts[ROLLTUI_TOASTS_MAX - 1].text = keep; /* its buffer, reused */
    --log->toast_n;
  }
  t = &log->toasts[log->toast_n++];
  t->level = level;
  t->at_ms = now_ms;
  t->drawn.w = t->drawn.h = 0;
  rolltui_str_set(&t->text, text, len);
}

size_t rolltui_message_log_count(const RolltuiMessageLog* log) { return log->n; }

const RolltuiMessage* rolltui_message_log_at(const RolltuiMessageLog* log, size_t i) {
  return i < log->n ? &log->v[(log->head + i) % log->cap] : NULL;
}

unsigned char rolltui_message_role(const RolltuiBuiltinRoles* roles, int level) {
  switch (level) {
    case ROLLTUI_MESSAGE_OK: return roles->ok;
    case ROLLTUI_MESSAGE_WARNING: return roles->warning;
    case ROLLTUI_MESSAGE_ERROR: return roles->error;
    default: return roles->info;
  }
}

const char* rolltui_message_glyph(int level, size_t* len) {
  /* U+2713 and U+2717 are East Asian Neutral: one cell on every terminal, ambiguous width or not. */
  static const char* const kGlyphs[4] = {"i", "\xe2\x9c\x93", "!", "\xe2\x9c\x97"};
  const char* g = kGlyphs[level >= 0 && level <= 3 ? level : 0];
  if (len) *len = strlen(g);
  return g;
}

static void remove_toast(RolltuiMessageLog* log, size_t i) {
  RolltuiStr keep = log->toasts[i].text;
  memmove(log->toasts + i, log->toasts + i + 1, (log->toast_n - i - 1) * sizeof *log->toasts);
  --log->toast_n;
  memset(&log->toasts[log->toast_n], 0, sizeof *log->toasts);
  rolltui_str_clear(&keep);
  log->toasts[log->toast_n].text = keep;
}

void rolltui_message_log_dismiss(RolltuiMessageLog* log) {
  while (log->toast_n) remove_toast(log, log->toast_n - 1);
}

/* How long a toast takes to slide in. */
#define TOAST_SLIDE_MS 160
#define TOAST_MAX_W 48
#define TOAST_MAX_LINES 4

int rolltui_message_log_draw_toasts(RolltuiMessageLog* log, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles,
                                    const RolltuiBuiltinRoles* roles, unsigned long long now_ms, int ambiguous_wide, int still) {
  const RolltuiStyle panel = styles[roles->panel];
  const int maxw = imin(TOAST_MAX_W, screen.w - 2);
  const int cap_h = imax(3, screen.h / 3);
  unsigned long long next = (unsigned long long)-1;
  size_t i;
  int bottom = screen.y + screen.h - 2; /* the last row stays clear: a status line lives there more often than not */
  int used = 0;
  for (i = log->toast_n; i-- > 0;) /* expired ones go first */
    if (now_ms >= log->toasts[i].at_ms + rolltui_toast_ms(log->toasts[i].level)) remove_toast(log, i);
  if (log->toast_n == 0) return -1;
  for (i = 0; i < log->toast_n; ++i) log->toasts[i].drawn.w = log->toasts[i].drawn.h = 0;
  for (i = log->toast_n; i-- > 0;) { /* the newest at the bottom, the older ones stacked above it */
    RolltuiToast* t = &log->toasts[i];
    const unsigned long long ends = t->at_ms + rolltui_toast_ms(t->level);
    RolltuiWrapOptions wo;
    RolltuiStyle line;
    RolltuiRect box, clip;
    size_t glen, lines, j;
    const char* glyph;
    int text_w, inner, w, h, off = 0;
    if (ends - now_ms < next) next = ends - now_ms;
    if (maxw < 12 || screen.h < 3) continue; /* no room for a box: the history still has it */
    text_w = rolltui_frame_text_width(log->draw, t->text.p ? t->text.p : "", t->text.n, ambiguous_wide);
    inner = imin(maxw - 4, imax(text_w + 2, 10)); /* the glyph, a space, the text */
    w = inner + 4;
    memset(&wo, 0, sizeof wo);
    wo.ambiguous_wide = (unsigned char)ambiguous_wide;
    wo.tab_width = 4;
    rolltui_wrap(log->wrap, t->text.p ? t->text.p : "", t->text.n, imax(inner - 2, 1), wo);
    lines = rolltui_wrap_line_count(log->wrap);
    if (lines == 0) lines = 1;
    if (lines > TOAST_MAX_LINES) lines = TOAST_MAX_LINES;
    if ((int)lines > screen.h - 3) lines = (size_t)imax(screen.h - 3, 1);
    h = (int)lines + 2;
    if (used > 0 && used + h > cap_h) break; /* the stack is full: older toasts wait in the history; the newest always shows */
    if (!still && now_ms < t->at_ms + TOAST_SLIDE_MS) {
      const double left = 1.0 - (double)(now_ms - t->at_ms) / TOAST_SLIDE_MS;
      off = (int)ceil(left * (w + 1));
      if (16 < next) next = 16;
    }
    box.x = screen.x + screen.w - 1 - w + off;
    box.y = bottom - h + 1;
    box.w = w;
    box.h = h;
    {
      int c[4];
      rolltui_rect_intersect(box.x, box.y, box.w, box.h, screen.x, screen.y, screen.w, screen.h, c);
      clip.x = c[0];
      clip.y = c[1];
      clip.w = c[2];
      clip.h = c[3];
    }
    if (clip.w > 0 && clip.h > 0) {
      line = styles[rolltui_message_role(roles, t->level)];
      rolltui_style_on(&line, panel.bg);
      rolltui_frame_fill(f, log->draw, clip, panel, NULL, 0);
      rolltui_draw_border(f, log->draw, box, 2 /* rounded */, line, NULL, 0, line, ambiguous_wide);
      glyph = rolltui_message_glyph(t->level, &glen);
      rolltui_frame_put(f, box.x + 2, box.y + 1, glyph, glen, 1, line, 0);
      for (j = 0; j < lines; ++j) {
        const char* lt;
        size_t ln, gn;
        const RolltuiWrapGrapheme* gs;
        int lw, indent, hard;
        if (j >= rolltui_wrap_line_count(log->wrap)) break;
        rolltui_wrap_line(log->wrap, j, &lt, &ln, &gs, &gn, &lw, &indent, &hard);
        rolltui_frame_put_text(f, log->draw, box.x + 4, box.y + 1 + (int)j, lt, ln, panel, imax(inner - 2, 0), ambiguous_wide, 0);
      }
      t->drawn = clip;
    }
    used += h;
    bottom -= h;
  }
  return next == (unsigned long long)-1 ? -1 : (int)(next > 0x7fffffff ? 0x7fffffff : next);
}

int rolltui_message_log_toast_event(RolltuiMessageLog* log, const RolltuiEvent* e) {
  size_t i;
  if (e->kind != ROLLTUI_EVENT_MOUSE || e->mouse.kind != 0 /* press */) return 0;
  for (i = log->toast_n; i-- > 0;) {
    const RolltuiRect r = log->toasts[i].drawn;
    if (r.w > 0 && e->mouse.x >= r.x && e->mouse.x < r.x + r.w && e->mouse.y >= r.y && e->mouse.y < r.y + r.h) {
      remove_toast(log, i);
      return 1;
    }
  }
  return 0;
}

/* ---- big digits and a count ------------------------------------------------------------------ */

void rolltui_count_release(RolltuiCount* c) {
  if (c) rolltui_str_free(&c->label);
}

/* SEVEN SEGMENTS IN HEAVY BOX DRAWING, three rows of three cells a digit: ━ ┃ ┏ ┓ ┗ ┛ ┣ ┫ and the half lines ╸ ╹ ╺ ╻. */
#define H_ "\xe2\x94\x81"  /* ━ */
#define V_ "\xe2\x94\x83"  /* ┃ */
#define TL "\xe2\x94\x8f"  /* ┏ */
#define TR "\xe2\x94\x93"  /* ┓ */
#define BL "\xe2\x94\x97"  /* ┗ */
#define BR "\xe2\x94\x9b"  /* ┛ */
#define ML "\xe2\x94\xa3"  /* ┣ */
#define MR "\xe2\x94\xab"  /* ┫ */
#define HL "\xe2\x95\xb8"  /* ╸ */
#define HU "\xe2\x95\xb9"  /* ╹ */
#define HR "\xe2\x95\xba"  /* ╺ */
#define HD "\xe2\x95\xbb"  /* ╻ */
static const char* const kSegments[10][3][3] = {
    {{TL, H_, TR}, {V_, " ", V_}, {BL, H_, BR}}, /* 0 */
    {{" ", " ", HD}, {" ", " ", V_}, {" ", " ", HU}}, /* 1 */
    {{HR, H_, TR}, {TL, H_, BR}, {BL, H_, HL}}, /* 2 */
    {{HR, H_, TR}, {HR, H_, MR}, {HR, H_, BR}}, /* 3 */
    {{HD, " ", HD}, {BL, H_, MR}, {" ", " ", HU}}, /* 4 */
    {{TL, H_, HL}, {BL, H_, TR}, {HR, H_, BR}}, /* 5 */
    {{TL, H_, HL}, {ML, H_, TR}, {BL, H_, BR}}, /* 6 */
    {{HR, H_, TR}, {" ", " ", V_}, {" ", " ", HU}}, /* 7 */
    {{TL, H_, TR}, {ML, H_, MR}, {BL, H_, BR}}, /* 8 */
    {{TL, H_, TR}, {BL, H_, MR}, {HR, H_, BR}}, /* 9 */
};
/* THE SAME IN ASCII, the classic three by three of underscores and bars. */
static const char* const kAsciiSegments[10][3] = {
    {" _ ", "| |", "|_|"}, {"   ", "  |", "  |"}, {" _ ", " _|", "|_ "}, {" _ ", " _|", " _|"}, {"   ", "|_|", "  |"},
    {" _ ", "|_ ", " _|"}, {" _ ", "|_ ", "|_|"}, {" _ ", "  |", "  |"}, {" _ ", "|_|", "|_|"}, {" _ ", "|_|", " _|"},
};
#undef H_
#undef V_
#undef TL
#undef TR
#undef BL
#undef BR
#undef ML
#undef MR

/* A character's cells: a digit, a minus or a space three, anything else one. */
static int big_cells(char c) { return (c >= '0' && c <= '9') || c == '-' || c == ' ' ? 3 : 1; }

int rolltui_big_digits_width(const char* text, size_t len) {
  size_t i;
  int w = 0;
  for (i = 0; text && i < len; ++i) w += (i ? 1 : 0) + big_cells(text[i]);
  return w;
}

int rolltui_big_digits_draw(RolltuiFrame* f, int x, int y, const char* text, size_t len, RolltuiStyle style, const RolltuiStyle* leading,
                            int aw) {
  size_t i, lead = 0;
  int at = x, row;
  if (!f || !text) return 0;
  if (leading) /* the zeros before the first other digit, the last one kept bright: 007 dims two */
    while (lead + 1 < len && text[lead] == '0' && text[lead + 1] >= '0' && text[lead + 1] <= '9') ++lead;
  for (i = 0; i < len; ++i) {
    const char c = text[i];
    const RolltuiStyle st = i < lead ? *leading : style;
    if (i) ++at;
    for (row = 0; row < 3; ++row) {
      if (c >= '0' && c <= '9') {
        int k;
        if (aw) {
          for (k = 0; k < 3; ++k) rolltui_frame_put(f, at + k, y + row, kAsciiSegments[c - '0'][row] + k, 1, 1, st, 0);
        } else {
          for (k = 0; k < 3; ++k) rolltui_frame_put(f, at + k, y + row, kSegments[c - '0'][row][k], strlen(kSegments[c - '0'][row][k]), 1, st, 0);
        }
      } else if (c == '-') {
        const char* g = row != 1 ? " " : aw ? "_" : "\xe2\x94\x81";
        const char* e = row != 1 ? " " : aw ? "_" : HR;
        const char* l = row != 1 ? " " : aw ? "_" : HL;
        rolltui_frame_put(f, at, y + row, e, strlen(e), 1, st, 0);
        rolltui_frame_put(f, at + 1, y + row, g, strlen(g), 1, st, 0);
        rolltui_frame_put(f, at + 2, y + row, l, strlen(l), 1, st, 0);
      } else if (c == ' ') {
        int k;
        for (k = 0; k < 3; ++k) rolltui_frame_put(f, at + k, y + row, " ", 1, 1, st, 0);
      } else if (c == '.') {
        rolltui_frame_put(f, at, y + row, row == 2 ? (aw ? "." : HD) : " ", row == 2 && !aw ? 3 : 1, 1, st, 0);
      } else if (c == ':') {
        const char* g = row == 1 ? " " : aw ? "." : row == 0 ? HD : HU;
        rolltui_frame_put(f, at, y + row, g, strlen(g), 1, st, 0);
      } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
        rolltui_frame_put(f, at, y + row, row == 1 ? &text[i] : " ", 1, 1, st, 0);
      }
    }
    at += big_cells(c);
  }
  return at - x;
}
#undef HL
#undef HR
#undef HU
#undef HD

/* A count as digits, at least `digits` of them, zero-padded: "007". */
static int count_text(long long v, int digits, char* out, size_t cap) {
  if (digits > 18) digits = 18;
  if (v < 0) return snprintf(out, cap, "-%0*lld", digits > 1 ? digits - 1 : 1, -v);
  return snprintf(out, cap, "%0*lld", digits > 0 ? digits : 1, v);
}

#define COUNT_DIVIDER "\xe2\xa1\x87" /* ⡇, braille: one cell in every terminal */

void rolltui_count_draw_roles(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect r, const RolltuiCount* c, int focused,
                              const RolltuiStyle* styles, const RolltuiBuiltinRoles* roles, int aw) {
  char vs[24], fs[24];
  int vn, fn = 0, w, y, x, big;
  RolltuiStyle ground, value, fault, muted, legend, arrow;
  const char* up = aw ? "^" : "\xe2\x86\x91";   /* ↑ */
  const char* down = aw ? "v" : "\xe2\x86\x93"; /* ↓ */
  if (!f || !c || r.w <= 0 || r.h <= 0) return;
  rolltui_frame_ground(f, &ground);
  value = styles[roles->ok];
  fault = styles[roles->error];
  muted = styles[roles->text_muted];
  legend = styles[focused ? roles->focus : roles->label];
  arrow = styles[roles->text];
  value.bg = fault.bg = muted.bg = legend.bg = arrow.bg = ground.bg;
  value.bold = fault.bold = legend.bold = 1;
  vn = count_text(c->value, c->digits, vs, sizeof vs);
  if (c->fault >= 0) fn = count_text(c->fault, c->digits, fs, sizeof fs);
  if (vn < 0) vn = 0;
  if (fn < 0) fn = 0;
  /* the count, a space, the divider, a space, the second count; a space and the arrow */
  w = rolltui_big_digits_width(vs, (size_t)vn) + (c->fault >= 0 ? 3 + rolltui_big_digits_width(fs, (size_t)fn) : 0) + (c->trend ? 2 : 0);
  big = r.h >= 3 && w <= r.w;
  if (big) {
    const int legend_row = c->label.n && r.h >= 4;
    y = r.y + (r.h - 3 - legend_row) / 2;
    if (legend_row) {
      const int lw = rolltui_frame_text_width(draw, c->label.p, c->label.n, aw);
      rolltui_frame_put_text_cut(f, draw, r.x + imax((r.w - lw) / 2, 0), y, c->label.p, c->label.n, legend, r.w, ROLLTUI_CUT_END, aw, 0);
      ++y;
    }
    x = r.x + (r.w - w) / 2;
    x += rolltui_big_digits_draw(f, x, y, vs, (size_t)vn, value, &muted, aw);
    if (c->fault >= 0) {
      int k;
      for (k = 0; k < 3; ++k) rolltui_frame_put(f, x + 1, y + k, COUNT_DIVIDER, sizeof COUNT_DIVIDER - 1, 1, muted, 0);
      x += 3;
      x += rolltui_big_digits_draw(f, x, y, fs, (size_t)fn, fault, &muted, aw);
    }
    if (c->trend) rolltui_frame_put(f, x + 1, y + 1, c->trend > 0 ? up : down, strlen(c->trend > 0 ? up : down), 1, arrow, 0);
    return;
  }
  { /* ONE LINE: the legend, the count, the divider and the second count, the arrow; centred, cut where it must be */
    const int lw = c->label.n ? rolltui_frame_text_width(draw, c->label.p, c->label.n, aw) + 1 : 0;
    const int line = lw + vn + (c->fault >= 0 ? 3 + fn : 0) + (c->trend ? 2 : 0);
    x = r.x + imax((r.w - line) / 2, 0);
    y = r.y + (r.h - 1) / 2;
    if (lw && lw + vn <= r.w) x += 1 + rolltui_frame_put_text(f, draw, x, y, c->label.p, c->label.n, legend, r.w - vn - 1, aw, 0);
    x += rolltui_frame_put_text(f, draw, x, y, vs, (size_t)vn, value, imax(r.x + r.w - x, 0), aw, 0);
    if (c->fault >= 0 && x + 3 + fn <= r.x + r.w) {
      rolltui_frame_put(f, x + 1, y, COUNT_DIVIDER, sizeof COUNT_DIVIDER - 1, 1, muted, 0);
      x += 3 + rolltui_frame_put_text(f, draw, x + 3, y, fs, (size_t)fn, fault, fn, aw, 0);
    }
    if (c->trend && x + 2 <= r.x + r.w) rolltui_frame_put(f, x + 1, y, c->trend > 0 ? up : down, strlen(c->trend > 0 ? up : down), 1, arrow, 0);
  }
}

int rolltui_gauge_draw(RolltuiFrame* f, RolltuiDrawScratch* draw, int x, int y, int width, const RolltuiGauge* g,
                       const RolltuiStyle* styles, int ambiguous_wide) {
  RolltuiBuiltinRoles roles;
  if (!f || !draw || !g || !styles) return 0;
  memset(&roles, 0, sizeof roles);
  roles.text_muted = ROLLTUI_ROLE_TEXT_MUTED;
  roles.error = ROLLTUI_ROLE_ERROR;
  roles.label = ROLLTUI_ROLE_LABEL;
  roles.value = ROLLTUI_ROLE_VALUE;
  roles.gauge = ROLLTUI_ROLE_ACCENT_2;
  return rolltui_gauge_draw_roles(f, draw, x, y, width, g, styles, &roles, ambiguous_wide);
}
