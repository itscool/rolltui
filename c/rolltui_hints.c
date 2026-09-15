/* rolltui/c/rolltui_hints.c — A HINT BAR: "F1 help  F2 settings  c copy", each hint a chord and
 * a label standing for an ACTION, drawn once and hit-tested after. A status line that names the
 * keys is telling a person what they can do; a hint they can click is the same sentence with a
 * hand on it. The bar keeps where every hint landed on the last draw, so a press at a cell
 * answers with the action — and a hint that did not fit is neither drawn nor hittable. */
#include "rolltui/c/rolltui_hints.h"

#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_unicode.h"

typedef struct Hint {
  RolltuiStr chord, label, action;
  int x, w, y; /* where it landed on the last draw; w 0 when it did not fit */
  unsigned char disabled; /* drawn muted, never hit: the action cannot be taken now */
} Hint;

struct RolltuiHintBar {
  Hint* v; /* GROWING, AMORTISED (strategy 2): a bar is rebuilt when the bindings change */
  size_t n, cap;
  RolltuiUnicodeScratch* u; /* OWNED: the width of a chord's text */
  RolltuiStr separator;     /* between hints: two spaces for keys, " › " for a breadcrumb */
  unsigned char keep_tail;  /* drop from the HEAD when short of room, an ellipsis first: a breadcrumb */
};

RolltuiHintBar* rolltui_hint_bar_new(void) {
  RolltuiHintBar* b = (RolltuiHintBar*)rolltui_mem_alloc(sizeof *b);
  memset(b, 0, sizeof *b);
  b->u = rolltui_u_scratch_new();
  rolltui_str_set(&b->separator, "  ", 2);
  return b;
}
void rolltui_hint_bar_set_separator(RolltuiHintBar* b, const char* sep, size_t len) { rolltui_str_set(&b->separator, sep, len); }
void rolltui_hint_bar_set_keep_tail(RolltuiHintBar* b, int on) { b->keep_tail = on ? 1 : 0; }
void rolltui_hint_bar_clear(RolltuiHintBar* b) {
  size_t i;
  for (i = 0; i < b->n; ++i) {
    rolltui_str_free(&b->v[i].chord);
    rolltui_str_free(&b->v[i].label);
    rolltui_str_free(&b->v[i].action);
  }
  b->n = 0;
}
void rolltui_hint_bar_free(RolltuiHintBar* b) {
  if (!b) return;
  rolltui_hint_bar_clear(b);
  rolltui_mem_free(b->v);
  rolltui_str_free(&b->separator);
  rolltui_u_scratch_free(b->u);
  rolltui_mem_free(b);
}
void rolltui_hint_bar_add(RolltuiHintBar* b, const char* chord, size_t chord_len, const char* label, size_t label_len,
                          const char* action, size_t action_len) {
  Hint* h;
  b->v = (Hint*)rolltui_grow_zeroed(b->v, &b->cap, b->n + 1, sizeof *b->v);
  h = &b->v[b->n++];
  memset(h, 0, sizeof *h);
  rolltui_str_set(&h->chord, chord, chord_len);
  rolltui_str_set(&h->label, label, label_len);
  rolltui_str_set(&h->action, action, action_len);
}
size_t rolltui_hint_bar_count(const RolltuiHintBar* b) { return b->n; }
void rolltui_hint_bar_enable(RolltuiHintBar* b, const char* action, size_t len, int on) {
  size_t i;
  for (i = 0; i < b->n; ++i)
    if (b->v[i].action.n == len && (len == 0 || memcmp(b->v[i].action.p, action, len) == 0)) b->v[i].disabled = on ? 0 : 1;
}

/* A hint's own width: chord, a space, label — or the label alone when it has no chord. */
static int hint_width(RolltuiHintBar* b, const Hint* h, int aw, int* cw_out) {
  const int cw = rolltui_u_display_width(b->u, h->chord.p ? h->chord.p : "", h->chord.n, aw);
  const int lw = rolltui_u_display_width(b->u, h->label.p ? h->label.p : "", h->label.n, aw);
  *cw_out = cw;
  return (cw ? cw + 1 : 0) + lw;
}

int rolltui_hint_bar_draw(RolltuiHintBar* b, RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, int width,
                          RolltuiStyle chord_style, RolltuiStyle label_style, RolltuiStyle muted, int ambiguous_wide) {
  int used = 0;
  size_t i, first = 0;
  const int sep_w = rolltui_u_display_width(b->u, b->separator.p ? b->separator.p : "", b->separator.n, ambiguous_wide);
  for (i = 0; i < b->n; ++i) b->v[i].w = 0;
  if (b->keep_tail && b->n) {
    /* A BREADCRUMB KEEPS ITS TAIL: the last hint is the place the eye is at and the pencil after
     * it, so when the bar is short of room the HEAD is dropped, an ellipsis standing for it, and
     * the rest is drawn whole from there. */
    int total = 0;
    for (i = b->n; i-- > 0;) {
      int cw;
      const int hw = hint_width(b, &b->v[i], ambiguous_wide, &cw);
      /* THE TAIL IS THE BAR'S OWN MARK, not a part: a space joins it, never the separator. */
      const int with = total + (i + 1 < b->n ? (i + 2 == b->n ? 1 : sep_w) : 0) + hw;
      const int lead = i > 0 ? 1 + sep_w : 0; /* the ellipsis and its separator, if this is where it starts */
      if (with + lead > width && i + 1 < b->n) break;
      total = with;
      first = i;
    }
    if (first > 0) {
      rolltui_frame_put_text(f, s, x, y, "\xE2\x80\xA6", 3, chord_style, 1, ambiguous_wide, 0);
      rolltui_frame_put_text(f, s, x + 1, y, b->separator.p ? b->separator.p : "", b->separator.n, chord_style, sep_w, ambiguous_wide, 0);
      used = 1 + sep_w;
    }
  }
  for (i = first; i < b->n; ++i) {
    Hint* h = &b->v[i];
    int cw;
    const int hw = hint_width(b, h, ambiguous_wide, &cw);
    const int tail = b->keep_tail && i + 1 == b->n && i > 0;
    const int gap = (used && !(b->keep_tail && i == first && first > 0)) ? (tail ? 1 : sep_w) : 0;
    const int cl = cw ? cw + 1 : 0;
    /* WHOLE OR NOT AT ALL: a hint cut in half reads as a different key. */
    if (used + gap + hw > width) { if (b->keep_tail) break; continue; }
    if (gap && !tail) rolltui_frame_put_text(f, s, x + used, y, b->separator.p ? b->separator.p : "", b->separator.n, chord_style, sep_w, ambiguous_wide, 0);
    h->x = x + used + gap;
    h->y = y;
    h->w = hw;
    if (cw) rolltui_frame_put_text(f, s, h->x, y, h->chord.p ? h->chord.p : "", h->chord.n, h->disabled ? muted : chord_style, cw, ambiguous_wide, 0);
    rolltui_frame_put_text(f, s, h->x + cl, y, h->label.p ? h->label.p : "", h->label.n, h->disabled ? muted : label_style, hw - cl, ambiguous_wide, 0);
    used += gap + hw;
  }
  return used;
}

const char* rolltui_hint_bar_hit(const RolltuiHintBar* b, int x, int y, size_t* len) {
  size_t i;
  for (i = 0; i < b->n; ++i) {
    const Hint* h = &b->v[i];
    if (h->w > 0 && !h->disabled && y == h->y && x >= h->x && x < h->x + h->w) {
      if (len) *len = h->action.n;
      return h->action.p ? h->action.p : "";
    }
  }
  if (len) *len = 0;
  return NULL;
}
