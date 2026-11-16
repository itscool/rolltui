/* rolltui/c/rolltui_swap.c — see rolltui_swap.h. */
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/rolltui.h"

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_render.h"

/* OWNED, LONG-LIVED (CLAUDE.md's fourth strategy): two frames for the whole run, which is
 * the entire point of the type. `have_front` is the old hosts' `have_prev`, kept in one
 * place instead of three. */
struct RolltuiSwap {
  RolltuiFrame* back;  /* what `begin` lends out and `present` diffs */
  RolltuiFrame* front; /* the baseline: what is believed to be on screen */
  int have_front;
};

RolltuiSwap* rolltui_swap_new(int w, int h, RolltuiStyle fill) {
  RolltuiSwap* s = (RolltuiSwap*)rolltui_mem_alloc(sizeof *s);
  s->back = rolltui_frame_new(w, h, fill);
  s->front = rolltui_frame_new(w, h, fill);
  s->have_front = 0; /* nothing has been presented, so the first present paints in full */
  return s;
}

void rolltui_swap_free(RolltuiSwap* s) {
  if (!s) return;
  rolltui_frame_free(s->back);
  rolltui_frame_free(s->front);
  rolltui_mem_free(s);
}

RolltuiFrame* rolltui_swap_begin(RolltuiSwap* s, int w, int h, RolltuiStyle fill) {
  /* REUSE, not construction — this is the call `budget_test` measures and no host was
   * making. See the header. */
  rolltui_frame_reset(s->back, w, h, fill);
  return s->back;
}

void rolltui_swap_present(RolltuiSwap* s, unsigned char depth, RolltuiStr* out) { rolltui_swap_present_at(s, depth, 0, 0, out); }

const RolltuiFrame* rolltui_swap_pending(const RolltuiSwap* s) { return s->back; }

void rolltui_swap_present_at(RolltuiSwap* s, unsigned char depth, int origin, int ground, RolltuiStr* out) {
  /* THE DEPTH IS A REQUEST, not a fact: what a host passes is the most it would like, and a terminal
   * that has less is drawn to at what it has (rolltui_termfacts.h). No host has to remember. */
  depth = rolltui_termfacts_clamp_depth(depth);
  rolltui_render_diff_at(s->have_front ? s->front : NULL, s->back, depth, origin, ground, out);
  /* Swap the POINTERS. No frame is constructed, destroyed or copied here, which is what
   * makes the steady loop allocate nothing at all. */
  RolltuiFrame* t = s->front;
  s->front = s->back;
  s->back = t;
  s->have_front = 1;
}

void rolltui_swap_invalidate(RolltuiSwap* s) { s->have_front = 0; }

void rolltui_swap_forget_rect(RolltuiSwap* s, RolltuiRect r) {
  if (s && s->have_front) rolltui_frame_forget_rect(s->front, r);
}

const RolltuiFrame* rolltui_swap_front(const RolltuiSwap* s) {
  return s->have_front ? s->front : NULL;
}
