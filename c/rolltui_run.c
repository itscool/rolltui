/* rolltui/c/rolltui_run.c — the run loop. Declared in `rolltui/rolltui.h`, under "the run loop", where what it
 * promises an app is written. */
#include "rolltui/rolltui.h"

#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_os.h"

/* One event of a wake, copied out of the terminal's borrowed buffers. */
typedef struct RunEvent {
  RolltuiEvent e;
  size_t at;      /* where its `text` starts in `RolltuiRun::text` */
  int has_text;   /* an empty-but-present `text` is a real state (an unknown key with no bytes left) */
  int is_resize;
  int is_focus, focused;
  int w, h;
} RunEvent;

/* OWNED BY `rolltui_run` for the length of one call, on its stack, or by a stepped host from `rolltui_run_begin` to
 * `rolltui_run_finish`; an app only ever holds a borrow. */
struct RolltuiRun {
  const RolltuiRunApp* app;
  RolltuiTerminal* term;
  RolltuiSwap* swap;
  int stop;
  int w, h;
  int rows;          /* inline: the region's height asked for, held to the terminal's; 0 on the alternate screen */
  int facts;         /* a FACTS event arrived in this wake */
  /* HELD FOR THE WHOLE RUN AND REFILLED, never rebuilt per frame: the terminal's events are BORROWED for the length
   * of its emit call, and handling one can resize the app or stop the loop, so the bytes are copied here first and
   * acted on after. */
  RunEvent* ev;
  size_t ev_n, ev_cap;
  int in_fd, out_fd; /* the terminal's descriptors, for a program that takes it over */
  /* TIMERS, by id: due on the steady clock, `every` 0 for once. Fired after a wake's events, before `settle`. */
  struct RunTimer { unsigned int id; unsigned long long due; int every; RolltuiTimerFn fn; void* ctx; } * timers;
  size_t timer_n, timer_cap;
  unsigned int next_timer;
  RolltuiTasks* tasks; /* made on first use (`rolltui_run_tasks`), drained every wake, freed before the terminal */
  RolltuiStr text;   /* every event's text, back to back */
  RolltuiStr out;    /* the bytes of a present, kept between frames */
  int stepped;       /* `rolltui_run_step` has drawn once: each later step takes what arrived before it draws */
};

static unsigned long long steady_ms(void) { return rolltui_os_monotonic_ms(); }

/* ---- timers ------------------------------------------------------------------------------- */

static int timer_wait(const RolltuiRun* r, int timeout) {
  size_t i;
  unsigned long long now, soonest = (unsigned long long)-1;
  if (r->timer_n == 0) return timeout;
  now = steady_ms();
  for (i = 0; i < r->timer_n; ++i)
    if (r->timers[i].due < soonest) soonest = r->timers[i].due;
  {
    const long long left = soonest <= now ? 0 : (long long)(soonest - now);
    const int t = left > 0x7fffffff ? 0x7fffffff : (int)left;
    return timeout < 0 || t < timeout ? t : timeout;
  }
}

static void fire_timers(RolltuiRun* r) {
  const unsigned long long now = steady_ms();
  size_t i = 0;
  while (i < r->timer_n && !r->stop) {
    struct RunTimer t = r->timers[i];
    if (t.due > now) {
      ++i;
      continue;
    }
    if (t.every > 0) { /* due again a period on, never in the past: a slow frame does not make a burst */
      r->timers[i].due = t.due + (unsigned long long)t.every > now ? t.due + (unsigned long long)t.every : now + (unsigned long long)t.every;
      ++i;
    } else {
      r->timers[i] = r->timers[--r->timer_n];
    }
    t.fn(t.ctx, r); /* it may add or cancel timers; the walk is by index over what is there */
  }
}

static unsigned int add_timer(RolltuiRun* r, int ms, int every, RolltuiTimerFn fn, void* ctx) {
  struct RunTimer* t;
  if (!r || !fn) return 0;
  r->timers = (struct RunTimer*)rolltui_grow(r->timers, &r->timer_cap, r->timer_n + 1, sizeof *r->timers);
  t = &r->timers[r->timer_n++];
  r->next_timer = r->next_timer == 0xFFFFFFFFu ? 1u : r->next_timer + 1u;
  t->id = r->next_timer;
  t->due = steady_ms() + (unsigned long long)(ms > 0 ? ms : 0);
  t->every = every;
  t->fn = fn;
  t->ctx = ctx;
  return t->id;
}

unsigned int rolltui_run_after(RolltuiRun* run, int ms, RolltuiTimerFn fn, void* ctx) { return add_timer(run, ms, 0, fn, ctx); }
unsigned int rolltui_run_every(RolltuiRun* run, int ms, RolltuiTimerFn fn, void* ctx) {
  return add_timer(run, ms, ms > 0 ? ms : 1, fn, ctx);
}
void rolltui_run_cancel_timer(RolltuiRun* run, unsigned int id) {
  size_t i;
  if (!run || id == 0) return;
  for (i = 0; i < run->timer_n; ++i)
    if (run->timers[i].id == id) {
      run->timers[i] = run->timers[--run->timer_n];
      return;
    }
}
void rolltui_run_debounce(RolltuiRun* run, unsigned int* slot, int ms, RolltuiTimerFn fn, void* ctx) {
  if (!run || !slot) return;
  rolltui_run_cancel_timer(run, *slot);
  *slot = rolltui_run_after(run, ms, fn, ctx);
}

static void wake_terminal(void* term) { rolltui_terminal_wake((RolltuiTerminal*)term); }

RolltuiTasks* rolltui_run_tasks(RolltuiRun* run) {
  if (!run || !run->term) return NULL;
  if (!run->tasks) run->tasks = rolltui_tasks_new(2, wake_terminal, run->term);
  return run->tasks;
}

static void collect(void* vctx, const RolltuiTermEvent* te) {
  RolltuiRun* r = (RolltuiRun*)vctx;
  RunEvent* re;
  if (te->kind == ROLLTUI_TERM_EVENT_FACTS) { r->facts = 1; return; }
  if (te->kind > ROLLTUI_TERM_EVENT_FACTS && te->kind != ROLLTUI_TERM_EVENT_FOCUS) return; /* not a kind this loop knows */
  r->ev = (RunEvent*)rolltui_grow(r->ev, &r->ev_cap, r->ev_n + 1, sizeof *r->ev);
  re = &r->ev[r->ev_n++];
  memset(re, 0, sizeof *re);
  if (te->kind == ROLLTUI_TERM_EVENT_FOCUS) {
    re->is_focus = 1;
    re->focused = te->focused;
    return;
  }
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

/* Enters the terminal and makes the run's own state. 0, with nothing made, when the app or the descriptors are refused. */
static int run_enter(RolltuiRun* r, int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app, int* result) {
  RolltuiStyle fill;
  if (!app || !app->render || !app->event) {
    *result = ROLLTUI_RUN_BAD_APP;
    return 0;
  }
  /* ASKED BEFORE THE TERMINAL IS MADE, of both descriptors. Making it enters: the alternate screen, the modes, the
   * questions, and the leaving again at free, all WRITTEN — so a refusal decided afterwards has already sent all of
   * that into a pipe or a file, which is the one place a program refusing to draw must leave untouched. */
  if (!rolltui_os_isatty(in_fd) || !rolltui_os_isatty(out_fd)) {
    *result = ROLLTUI_RUN_NOT_A_TERMINAL;
    return 0;
  }
  memset(r, 0, sizeof *r);
  r->app = app;
  r->in_fd = in_fd;
  r->out_fd = out_fd;
  opts.facts_events = 1; /* the loop is what listens for them */
  r->term = rolltui_terminal_new(in_fd, out_fd, opts);
  r->w = rolltui_terminal_width(r->term);
  r->h = rolltui_terminal_height(r->term);
  r->rows = opts.inline_rows;
  if (r->rows > 0 && r->rows < r->h) r->h = r->rows; /* an inline region is the frame's height */
  if (app->start) app->start(app->ctx, r);
  if (!r->stop) {
    ground_of(r, &fill);
    r->swap = rolltui_swap_new(r->w, r->h, fill);
  }
  *result = ROLLTUI_RUN_STOPPED;
  return 1;
}

/* One frame drawn and presented. Returns how long it asked to wait, a timer due sooner winning. */
static int run_frame(RolltuiRun* r) {
  const RolltuiRunApp* app = r->app;
  RolltuiStyle fill;
  RolltuiFrame* f;
  int timeout;
  ground_of(r, &fill);
  f = rolltui_swap_begin(r->swap, r->w, r->h, fill);
  timeout = app->render(app->ctx, r, f, r->w, r->h, steady_ms());
  timeout = timer_wait(r, timeout); /* a timer due sooner than the frame asked to wait wakes it */
  /* AT THE DEPTH THE TERMINAL HAS: a terminal sent 24-bit colour it cannot draw reads it as stray attributes. */
  rolltui_terminal_present(r->term, r->swap, &r->out);
  return timeout;
}

/* One wake: waits up to `timeout` for the terminal (0: takes what has arrived), then acts on it, in the order the loop promises. */
static void run_wake(RolltuiRun* r, int timeout) {
  const RolltuiRunApp* app = r->app;
  size_t i;
  r->ev_n = 0;
  r->facts = 0;
  rolltui_str_clear(&r->text);
  rolltui_terminal_poll(r->term, timeout, collect, r);
  /* The text pointers only now: `text` growing while events were collected would have dangled every earlier one. */
  for (i = 0; i < r->ev_n; ++i)
    if (r->ev[i].has_text) r->ev[i].e.text = r->text.p ? r->text.p + r->ev[i].at : "";
  for (i = 0; i < r->ev_n && !r->stop; ++i) {
    if (r->ev[i].is_resize) {
      r->w = r->ev[i].w;
      r->h = r->rows > 0 && r->rows < r->ev[i].h ? r->rows : r->ev[i].h;
      rolltui_swap_invalidate(r->swap);
      if (app->resized) app->resized(app->ctx, r, r->w, r->h);
    } else if (r->ev[i].is_focus) {
      if (app->focus) app->focus(app->ctx, r, r->ev[i].focused);
    } else {
      app->event(app->ctx, r, &r->ev[i].e);
    }
  }
  if (r->facts && !r->stop) {
    if (app->facts_changed) app->facts_changed(app->ctx, r);
    rolltui_swap_invalidate(r->swap);
  }
  if (r->tasks && !r->stop) rolltui_tasks_drain(r->tasks); /* what workers finished and threads posted, on this thread */
  if (!r->stop) fire_timers(r);
  if (app->settle) app->settle(app->ctx, r);
}

static void run_leave(RolltuiRun* r) {
  rolltui_tasks_free(r->tasks); /* the workers stopped and every job's `done` called, before the terminal goes */
  rolltui_mem_free(r->timers);
  rolltui_mem_free(r->ev);
  rolltui_str_free(&r->text);
  rolltui_str_free(&r->out);
  rolltui_swap_free(r->swap);
  rolltui_terminal_free(r->term); /* restores the screen before the caller says anything */
}

int rolltui_run(int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app) {
  RolltuiRun r;
  int result;
  if (!run_enter(&r, in_fd, out_fd, opts, app, &result)) return result;
  while (!r.stop) {
    const int timeout = run_frame(&r);
    run_wake(&r, timeout);
  }
  run_leave(&r);
  return ROLLTUI_RUN_STOPPED;
}

RolltuiRun* rolltui_run_begin(int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app, int* result) {
  RolltuiRun* r = (RolltuiRun*)rolltui_mem_alloc(sizeof *r);
  int ignored;
  if (!result) result = &ignored;
  if (!run_enter(r, in_fd, out_fd, opts, app, result)) {
    rolltui_mem_free(r);
    return NULL;
  }
  return r;
}

int rolltui_run_step(RolltuiRun* r) {
  if (!r || r->stop) return ROLLTUI_RUN_STEP_STOPPED;
  if (r->stepped) run_wake(r, 0); /* the first step has had nothing arrive: it draws */
  r->stepped = 1;
  if (r->stop) return ROLLTUI_RUN_STEP_STOPPED;
  return run_frame(r);
}

int rolltui_run_finish(RolltuiRun* r) {
  if (!r) return ROLLTUI_RUN_STOPPED;
  run_leave(r);
  rolltui_mem_free(r);
  return ROLLTUI_RUN_STOPPED;
}

void rolltui_run_stop(RolltuiRun* run) {
  if (run) run->stop = 1;
}

RolltuiTerminal* rolltui_run_terminal(RolltuiRun* run) { return run ? run->term : NULL; }

int rolltui_run_suspend_job(RolltuiRun* run) {
#ifdef _WIN32
  (void)run;
  return 0; /* no job control: nothing to suspend into */
#else
  if (!run || !run->term) return 0;
  rolltui_terminal_suspend(run->term); /* as it was found, with nothing of this program's left on it */
  rolltui_os_stop_process();          /* until the shell continues it */
  rolltui_terminal_resume(run->term);
  if (run->swap) rolltui_swap_invalidate(run->swap); /* a resize while stopped arrives as one, after */
  return 1;
#endif
}

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

int rolltui_run_program(RolltuiRun* run, const char* const* argv, const char* cwd, int* status, RolltuiStr* err) {
  int ok;
  if (err) rolltui_str_clear(err);
  if (!run || !run->term) {
    if (err) rolltui_str_set(err, "no terminal to hand over", sizeof "no terminal to hand over" - 1);
    return 0;
  }
  rolltui_run_suspend(run);
  ok = rolltui_os_child_wait_terminal(argv, cwd, run->in_fd, run->out_fd, status, err);
  rolltui_run_resume(run); /* what was on the screen is gone: drawn whole */
  return ok;
}
