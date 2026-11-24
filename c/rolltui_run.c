/* rolltui/c/rolltui_run.c — the run loop. Declared in `rolltui/rolltui.h`, under "the run loop", where what it
 * promises an app is written. */
#include "rolltui/rolltui.h"

#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_presets.h" /* the stores a run watches, read again through their own calls */
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
  /* EVENTS A PROGRAM MADE (`rolltui_run_inject`), handed to it at the start of the next wake; their text back to back */
  RunEvent* inj;
  size_t inj_n, inj_cap;
  RolltuiStr inj_text;
  int flush;         /* `rolltui_run_flush_input`: the rest of this wake's events go */
  /* THE STORES WATCHED (`rolltui_run_watch_stores`, BORROWED): each store, the watch ids of its three files, and the origin they were
   * watched for (another origin: other files) */
  RolltuiWatch* watch;
  struct RunStore {
    RolltuiPresetStore* s;
    int kind; /* 0 theme, 1 layout, 2 bindings */
    unsigned int ids[3];
    RolltuiStr origin;
  } stores[3];
  size_t store_n;
};

static void refresh_stores(RolltuiRun* r);
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
  const int timed = rolltui_log_wants(ROLLTUI_LOG_DEBUG);
  const unsigned long long t0 = timed ? rolltui_os_monotonic_us() : 0;
  unsigned long long t1 = 0;
  ground_of(r, &fill);
  f = rolltui_swap_begin(r->swap, r->w, r->h, fill);
  timeout = app->render(app->ctx, r, f, r->w, r->h, steady_ms());
  timeout = timer_wait(r, timeout); /* a timer due sooner than the frame asked to wait wakes it */
  if (timed) t1 = rolltui_os_monotonic_us();
  /* AT THE DEPTH THE TERMINAL HAS: a terminal sent 24-bit colour it cannot draw reads it as stray attributes. */
  rolltui_terminal_present(r->term, r->swap, &r->out);
  if (timed) /* EACH FRAME'S TIME, at DEBUG: the host's drawing, then the diff and the write */
    rolltui_logf(ROLLTUI_LOG_DEBUG, "frame: render %.2f ms, present %.2f ms, %zu bytes", (double)(t1 - t0) / 1000.0,
                 (double)(rolltui_os_monotonic_us() - t1) / 1000.0, r->out.n);
  { /* a picture held back is due: the frame then sends it */
    const int held = rolltui_terminal_pending_ms(r->term);
    if (held >= 0 && (timeout < 0 || held < timeout)) timeout = held;
  }
  return timeout;
}

/* One wake: waits up to `timeout` for the terminal (0: takes what has arrived), then acts on it, in the order the loop promises. */
static void run_wake(RolltuiRun* r, int timeout) {
  const RolltuiRunApp* app = r->app;
  size_t i;
  r->ev_n = 0;
  r->facts = 0;
  r->flush = 0;
  rolltui_str_clear(&r->text);
  if (r->inj_n) { /* what the program made comes first, in the order it was made */
    RolltuiStr text;
    const size_t n = r->inj_n;
    RunEvent* made = r->inj;
    memset(&text, 0, sizeof text);
    rolltui_str_move(&text, &r->inj_text);
    r->inj = NULL, r->inj_n = 0, r->inj_cap = 0;
    for (i = 0; i < n && !r->stop; ++i) {
      if (made[i].has_text) made[i].e.text = text.p ? text.p + made[i].at : "";
      app->event(app->ctx, r, &made[i].e);
    }
    rolltui_mem_free(made);
    rolltui_str_free(&text);
    timeout = 0; /* and then whatever arrived, without waiting for more */
  }
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
      if (r->flush) break; /* a typeahead flush: what was typed after this key goes */
    }
  }
  if (r->facts && !r->stop) {
    if (app->facts_changed) app->facts_changed(app->ctx, r);
    rolltui_swap_invalidate(r->swap);
  }
  if (r->tasks && !r->stop) rolltui_tasks_drain(r->tasks); /* what workers finished and threads posted, on this thread */
  if (!r->stop) refresh_stores(r);
  if (!r->stop) fire_timers(r);
  if (app->settle) app->settle(app->ctx, r);
}

static void drop_store_watches(RolltuiRun* r) {
  size_t i, k;
  for (i = 0; i < r->store_n; ++i) {
    for (k = 0; k < 3; ++k) rolltui_watch_remove(r->watch, r->stores[i].ids[k]);
    rolltui_str_free(&r->stores[i].origin);
  }
  r->store_n = 0;
}

/* EACH STORE'S FILES WATCHED for its origin now: watched again when the origin moved. */
static void watch_store_files(RolltuiRun* r) {
  size_t i, k;
  for (i = 0; i < r->store_n; ++i) {
    struct RunStore* st = &r->stores[i];
    size_t on = 0;
    const char* o = rolltui_preset_store_origin(st->s, &on);
    RolltuiStr paths[3];
    if (st->ids[0] && rolltui_str_eq(&st->origin, o ? o : "", on)) continue;
    for (k = 0; k < 3; ++k) {
      rolltui_watch_remove(r->watch, st->ids[k]);
      st->ids[k] = 0;
    }
    memset(paths, 0, sizeof paths);
    rolltui_preset_store_watch_paths(st->s, &paths[0], &paths[1], &paths[2]);
    for (k = 0; k < 3; ++k) {
      if (paths[k].n) st->ids[k] = rolltui_watch_add(r->watch, paths[k].p, paths[k].n);
      rolltui_str_free(&paths[k]);
    }
    rolltui_str_set(&st->origin, o ? o : "", on);
  }
}

/* THE STORES READ AGAIN when their files changed or SIGUSR1 came; the screen drawn again when one was. */
static void refresh_stores(RolltuiRun* r) {
  size_t i;
  int any = 0;
  const int asked = rolltui_os_take_reload();
  if (!r->watch || (!rolltui_watch_changed(r->watch, NULL, 0) && !asked)) return;
  for (i = 0; i < r->store_n; ++i) {
    struct RunStore* st = &r->stores[i];
    if (st->kind == 0) {
      RolltuiThemePresetReport rep;
      memset(&rep, 0, sizeof rep);
      any |= rolltui_theme_store_refresh((RolltuiThemeStore*)st->s, &rep);
      rolltui_theme_preset_report_release(&rep);
    } else if (st->kind == 1) {
      RolltuiLayoutPresetReport rep;
      memset(&rep, 0, sizeof rep);
      any |= rolltui_layout_store_refresh((RolltuiLayoutStore*)st->s, &rep);
      rolltui_layout_preset_report_release(&rep);
    } else {
      RolltuiBindingsPresetReport rep;
      memset(&rep, 0, sizeof rep);
      any |= rolltui_bindings_store_refresh((RolltuiBindingsStore*)st->s, &rep);
      rolltui_bindings_preset_report_release(&rep);
    }
  }
  if (any) {
    rolltui_logf(ROLLTUI_LOG_INFO, "stores: read again, %s", asked ? "as SIGUSR1 asked" : "another program changed their files");
    watch_store_files(r);
    if (r->swap) rolltui_swap_invalidate(r->swap);
  }
}

void rolltui_run_watch_stores(RolltuiRun* run, RolltuiThemeStore* theme, RolltuiLayoutStore* layout, RolltuiBindingsStore* keys) {
  RolltuiPresetStore* s[3];
  size_t k;
  if (!run) return;
  s[0] = (RolltuiPresetStore*)theme;
  s[1] = (RolltuiPresetStore*)layout;
  s[2] = (RolltuiPresetStore*)keys;
  if (!run->watch) run->watch = rolltui_watch_new(wake_terminal, run->term);
  drop_store_watches(run);
  for (k = 0; k < 3; ++k) {
    if (!s[k]) continue;
    memset(&run->stores[run->store_n], 0, sizeof run->stores[run->store_n]);
    run->stores[run->store_n].s = s[k];
    run->stores[run->store_n].kind = (int)k;
    ++run->store_n;
  }
  watch_store_files(run);
  rolltui_os_listen_reload(run->store_n > 0);
}

static void run_leave(RolltuiRun* r) {
  if (r->watch) {
    drop_store_watches(r);
    rolltui_watch_free(r->watch);
    r->watch = NULL;
    rolltui_os_listen_reload(0);
  }
  rolltui_tasks_free(r->tasks); /* the workers stopped and every job's `done` called, before the terminal goes */
  rolltui_mem_free(r->timers);
  rolltui_mem_free(r->ev);
  rolltui_mem_free(r->inj);
  rolltui_str_free(&r->inj_text);
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

void rolltui_run_inject(RolltuiRun* run, const RolltuiEvent* e) {
  RunEvent* re;
  if (!run || !e) return;
  run->inj = (RunEvent*)rolltui_grow(run->inj, &run->inj_cap, run->inj_n + 1, sizeof *run->inj);
  re = &run->inj[run->inj_n++];
  memset(re, 0, sizeof *re);
  re->e = *e;
  re->e.text = NULL;
  if (e->text) {
    re->has_text = 1;
    re->at = run->inj_text.n;
    rolltui_str_append(&run->inj_text, e->text, e->text_len);
  }
  if (run->term) rolltui_terminal_wake(run->term);
}

void rolltui_run_flush_input(RolltuiRun* run) {
  if (!run) return;
  run->flush = 1;
  if (run->term) rolltui_terminal_flush_input(run->term);
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
