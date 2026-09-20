/* rolltui/c/rolltui_run.c — the run loop. Declared in `rolltui/rolltui.h`, under "the run loop", where what it
 * promises an app is written. */
#include "rolltui/rolltui.h"

#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_alloc.h"

/* One event of a wake, copied out of the terminal's borrowed buffers. */
typedef struct RunEvent {
  RolltuiEvent e;
  size_t at;      /* where its `text` starts in `RolltuiRun::text` */
  int has_text;   /* an empty-but-present `text` is a real state (an unknown key with no bytes left) */
  int is_resize;
  int w, h;
} RunEvent;

/* OWNED BY `rolltui_run` for the length of one call, on its stack; an app only ever holds a borrow. */
struct RolltuiRun {
  const RolltuiRunApp* app;
  RolltuiTerminal* term;
  RolltuiSwap* swap;
  int stop;
  int w, h;
  int facts;         /* a FACTS event arrived in this wake */
  /* HELD FOR THE WHOLE RUN AND REFILLED, never rebuilt per frame: the terminal's events are BORROWED for the length
   * of its emit call, and handling one can resize the app or stop the loop, so the bytes are copied here first and
   * acted on after. */
  RunEvent* ev;
  size_t ev_n, ev_cap;
  RolltuiStr text;   /* every event's text, back to back */
  RolltuiStr out;    /* the bytes of a present, kept between frames */
};

static unsigned long long steady_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000L);
}

static void collect(void* vctx, const RolltuiTermEvent* te) {
  RolltuiRun* r = (RolltuiRun*)vctx;
  RunEvent* re;
  if (te->kind == ROLLTUI_TERM_EVENT_FACTS) { r->facts = 1; return; }
  if (te->kind > ROLLTUI_TERM_EVENT_FACTS) return; /* a kind this loop does not know is not an input */
  r->ev = (RunEvent*)rolltui_grow(r->ev, &r->ev_cap, r->ev_n + 1, sizeof *r->ev);
  re = &r->ev[r->ev_n++];
  memset(re, 0, sizeof *re);
  if (te->kind == ROLLTUI_TERM_EVENT_RESIZE) {
    re->is_resize = 1;
    re->w = te->w;
    re->h = te->h;
    return;
  }
  re->e.kind = te->kind;
  re->e.key = te->key;
  re->e.mouse = te->mouse;
  if (te->text) {
    re->has_text = 1;
    re->at = r->text.n;
    re->e.text_len = te->text_len;
    rolltui_str_append(&r->text, te->text, te->text_len);
  }
}

static void ground_of(RolltuiRun* r, RolltuiStyle* fill) {
  memset(fill, 0, sizeof *fill);
  if (r->app->ground) r->app->ground(r->app->ctx, r, fill);
}

int rolltui_run(int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app) {
  RolltuiRun r;
  RolltuiStyle fill;
  size_t i;
  if (!app || !app->render || !app->event) return ROLLTUI_RUN_BAD_APP;
  memset(&r, 0, sizeof r);
  r.app = app;
  opts.facts_events = 1; /* the loop is what listens for them */
  r.term = rolltui_terminal_new(in_fd, out_fd, opts);
  if (!rolltui_terminal_is_tty(r.term)) {
    rolltui_terminal_free(r.term);
    return ROLLTUI_RUN_NOT_A_TERMINAL;
  }
  r.w = rolltui_terminal_width(r.term);
  r.h = rolltui_terminal_height(r.term);
  if (app->start) app->start(app->ctx, &r);
  if (!r.stop) {
    ground_of(&r, &fill);
    r.swap = rolltui_swap_new(r.w, r.h, fill);
  }
  while (!r.stop) {
    RolltuiFrame* f;
    int timeout;
    ground_of(&r, &fill);
    f = rolltui_swap_begin(r.swap, r.w, r.h, fill);
    timeout = app->render(app->ctx, &r, f, r.w, r.h, steady_ms());
    /* AT THE DEPTH THE TERMINAL HAS: a terminal sent 24-bit colour it cannot draw reads it as stray attributes. */
    rolltui_terminal_present(r.term, r.swap, &r.out);

    r.ev_n = 0;
    r.facts = 0;
    rolltui_str_clear(&r.text);
    rolltui_terminal_poll(r.term, timeout, collect, &r);
    /* The text pointers only now: `text` growing while events were collected would have dangled every earlier one. */
    for (i = 0; i < r.ev_n; ++i)
      if (r.ev[i].has_text) r.ev[i].e.text = r.text.p ? r.text.p + r.ev[i].at : "";
    for (i = 0; i < r.ev_n && !r.stop; ++i) {
      if (r.ev[i].is_resize) {
        r.w = r.ev[i].w;
        r.h = r.ev[i].h;
        rolltui_swap_invalidate(r.swap);
        if (app->resized) app->resized(app->ctx, &r, r.w, r.h);
      } else {
        app->event(app->ctx, &r, &r.ev[i].e);
      }
    }
    if (r.facts && !r.stop) {
      if (app->facts_changed) app->facts_changed(app->ctx, &r);
      rolltui_swap_invalidate(r.swap);
    }
    if (app->settle) app->settle(app->ctx, &r);
  }
  rolltui_mem_free(r.ev);
  rolltui_str_free(&r.text);
  rolltui_str_free(&r.out);
  rolltui_swap_free(r.swap);
  rolltui_terminal_free(r.term); /* restores the screen before the caller says anything */
  return ROLLTUI_RUN_STOPPED;
}

void rolltui_run_stop(RolltuiRun* run) {
  if (run) run->stop = 1;
}

RolltuiTerminal* rolltui_run_terminal(RolltuiRun* run) { return run ? run->term : NULL; }

void rolltui_run_invalidate(RolltuiRun* run) {
  if (run && run->swap) rolltui_swap_invalidate(run->swap);
}

void rolltui_run_suspend(RolltuiRun* run) {
  if (run && run->term) rolltui_terminal_suspend(run->term);
}

void rolltui_run_resume(RolltuiRun* run) {
  if (!run || !run->term) return;
  rolltui_terminal_resume(run->term);
  rolltui_run_invalidate(run);
}
