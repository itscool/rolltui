/* rolltui/c/rolltui_tasks.c — WORK OFF THE UI THREAD: a few workers, the jobs they run, and the queue of what comes back.
 *
 * ONE UI THREAD. A job's work runs on a worker; everything else a host sees (a job's `done`, a posted function) runs on the thread
 * that drains the tasks, in the order it arrived, so a host's state is touched by one thread only. A worker that finishes, and a
 * post from any thread, call the wake the tasks were made with, so a loop asleep in its input wait drains them at once.
 *
 * A JOB IS NAMED BY ITS ID, never a pointer a host could keep past its end: cancelling an id that has ended does nothing. A job
 * cancelled before a worker took it never runs; one cancelled while it runs is told (`rolltui_job_cancelled`) and its `done` says
 * so, so whatever it made is dropped rather than shown. Every job's `done` is called exactly once, which is where its `ctx` is
 * freed.
 */
#include "rolltui/rolltui.h"

#include <stdatomic.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_str.h"

struct RolltuiJob {
  unsigned int id;
  RolltuiJobWork work;
  RolltuiJobDone done;
  void* ctx;
  _Atomic int cancelled; /* read by the worker as it goes, written under the lock by the UI thread: atomic, so the read is never a race */
  int started, finished;
  struct RolltuiJob* next; /* the pending queue, or the finished list */
};
typedef struct RolltuiJob Job;

typedef struct Posted {
  RolltuiPostFn fn;
  void* ctx;
  struct Posted* next;
  int* ran; /* a call: the waiter's flag, set under the lock once `fn` has run */
} Posted;

/* A JOB'S QUESTION, on its worker's stack while it waits; answered under the lock. */
typedef struct Question {
  unsigned int id;
  int answered, yes;
  RolltuiStr text;
  struct Question* next;
} Question;

struct RolltuiTasks {
  RolltuiMutex lock;
  RolltuiCond more; /* a job was queued, or the tasks are stopping */
  RolltuiThread** workers;
  int worker_n;
  int stopping;
  Job *pending, *pending_tail; /* not yet taken by a worker */
  Job* running;                /* taken, not yet finished: a list linked through `next` is not kept; each is found by id */
  Job **live;                  /* every job started and not yet delivered, for cancel by id and for the busy count */
  size_t live_n, live_cap;
  Job *finished, *finished_tail;
  Posted *posted, *posted_tail;
  unsigned int next_id;
  RolltuiCond answered;            /* a call ran, a question was answered, a job was cancelled, or the tasks are stopping */
  unsigned long long drainer;      /* the thread that drains (made the tasks, or drained them last) */
  Question* questions;
  unsigned int next_question;
  void (*wake)(void*);
  void* wake_ctx;
};

static void wake(RolltuiTasks* t) {
  if (t->wake) t->wake(t->wake_ctx);
}

static void worker_main(void* arg) {
  RolltuiTasks* t = (RolltuiTasks*)arg;
  for (;;) {
    Job* j;
    rolltui_mutex_lock(&t->lock);
    while (!t->pending && !t->stopping) rolltui_cond_wait(&t->more, &t->lock);
    if (t->stopping) {
      rolltui_mutex_unlock(&t->lock);
      return;
    }
    j = t->pending;
    t->pending = j->next;
    if (!t->pending) t->pending_tail = NULL;
    j->next = NULL;
    j->started = 1;
    rolltui_mutex_unlock(&t->lock);
    if (!j->cancelled && j->work) j->work(j->ctx, j);
    rolltui_mutex_lock(&t->lock);
    j->finished = 1;
    if (t->finished_tail) t->finished_tail->next = j;
    else t->finished = j;
    t->finished_tail = j;
    rolltui_mutex_unlock(&t->lock);
    wake(t);
  }
}

RolltuiTasks* rolltui_tasks_new(int workers, void (*wake_fn)(void* ctx), void* wake_ctx) {
  RolltuiTasks* t = (RolltuiTasks*)rolltui_mem_alloc(sizeof *t);
  int i;
  memset(t, 0, sizeof *t);
  rolltui_mutex_init(&t->lock);
  rolltui_cond_init(&t->more);
  rolltui_cond_init(&t->answered);
  t->drainer = rolltui_os_thread_self();
  t->wake = wake_fn;
  t->wake_ctx = wake_ctx;
  if (workers < 0) workers = 0;
  if (workers > 16) workers = 16;
  t->workers = workers ? (RolltuiThread**)rolltui_mem_alloc((size_t)workers * sizeof *t->workers) : NULL;
  for (i = 0; i < workers; ++i) {
    t->workers[i] = rolltui_thread_start(worker_main, t);
    if (t->workers[i]) ++t->worker_n;
  }
  return t;
}

static void forget_live(RolltuiTasks* t, const Job* j) {
  size_t i;
  for (i = 0; i < t->live_n; ++i)
    if (t->live[i] == j) {
      t->live[i] = t->live[--t->live_n];
      return;
    }
}

/* NO WORKER RUNS THEM: each job pending runs here, on the thread that drains, as a worker would have. */
static void run_pending_here(RolltuiTasks* t) {
  for (;;) {
    Job* j;
    rolltui_mutex_lock(&t->lock);
    j = t->pending;
    if (j) {
      t->pending = j->next;
      if (!t->pending) t->pending_tail = NULL;
      j->next = NULL;
      j->started = 1;
    }
    rolltui_mutex_unlock(&t->lock);
    if (!j) return;
    if (!j->cancelled && j->work) j->work(j->ctx, j);
    rolltui_mutex_lock(&t->lock);
    j->finished = 1;
    if (t->finished_tail) t->finished_tail->next = j;
    else t->finished = j;
    t->finished_tail = j;
    rolltui_mutex_unlock(&t->lock);
  }
}

size_t rolltui_tasks_drain(RolltuiTasks* t) {
  size_t ran = 0;
  if (!t) return 0;
  rolltui_mutex_lock(&t->lock);
  t->drainer = rolltui_os_thread_self();
  rolltui_mutex_unlock(&t->lock);
  if (t->worker_n == 0) run_pending_here(t);
  for (;;) {
    Job* done;
    Posted* post;
    rolltui_mutex_lock(&t->lock);
    done = t->finished;
    t->finished = t->finished_tail = NULL;
    post = t->posted;
    t->posted = t->posted_tail = NULL;
    for (Job* j = done; j; j = j->next) forget_live(t, j);
    rolltui_mutex_unlock(&t->lock);
    if (!done && !post) break;
    while (post) { /* in the order posted */
      Posted* p = post;
      post = p->next;
      p->fn(p->ctx);
      if (p->ran) { /* a call: its waiter goes on */
        rolltui_mutex_lock(&t->lock);
        *p->ran = 1;
        rolltui_cond_broadcast(&t->answered);
        rolltui_mutex_unlock(&t->lock);
      }
      rolltui_mem_free(p);
      ++ran;
    }
    while (done) {
      Job* j = done;
      done = j->next;
      if (j->done) j->done(j->ctx, j->cancelled != 0);
      rolltui_mem_free(j);
      ++ran;
    }
  }
  return ran;
}

void rolltui_tasks_free(RolltuiTasks* t) {
  int i;
  if (!t) return;
  rolltui_mutex_lock(&t->lock);
  t->stopping = 1;
  { /* what no worker took is cancelled and delivered: each `done` is called once, so its ctx is freed */
    Job* j = t->pending;
    t->pending = t->pending_tail = NULL;
    while (j) {
      Job* next = j->next;
      j->cancelled = 1;
      j->finished = 1;
      j->next = NULL;
      if (t->finished_tail) t->finished_tail->next = j;
      else t->finished = j;
      t->finished_tail = j;
      j = next;
    }
  }
  for (i = 0; (size_t)i < t->live_n; ++i) t->live[i]->cancelled = 1; /* the running ones are told */
  rolltui_cond_broadcast(&t->more);
  rolltui_cond_broadcast(&t->answered); /* a job waiting on a call or a question stops waiting */
  rolltui_mutex_unlock(&t->lock);
  for (i = 0; i < t->worker_n; ++i) rolltui_thread_join(t->workers[i]);
  /* every worker has stopped: what they finished is delivered here, on the thread freeing the tasks */
  rolltui_tasks_drain(t);
  rolltui_mem_free(t->workers);
  rolltui_mem_free(t->live);
  rolltui_cond_destroy(&t->more);
  rolltui_cond_destroy(&t->answered);
  rolltui_mutex_destroy(&t->lock);
  rolltui_mem_free(t);
}

unsigned int rolltui_tasks_start(RolltuiTasks* t, RolltuiJobWork work, RolltuiJobDone done, void* ctx) {
  Job* j;
  unsigned int id;
  if (!t) return 0;
  j = (Job*)rolltui_mem_alloc(sizeof *j);
  memset(j, 0, sizeof *j);
  j->work = work;
  j->done = done;
  j->ctx = ctx;
  rolltui_mutex_lock(&t->lock);
  t->next_id = t->next_id == 0xFFFFFFFFu ? 1u : t->next_id + 1u;
  id = j->id = t->next_id;
  t->live = (Job**)rolltui_grow(t->live, &t->live_cap, t->live_n + 1, sizeof *t->live);
  t->live[t->live_n++] = j;
  if (t->pending_tail) t->pending_tail->next = j;
  else t->pending = j;
  t->pending_tail = j;
  rolltui_cond_signal(&t->more);
  rolltui_mutex_unlock(&t->lock);
  if (t->worker_n == 0) wake(t); /* nothing else will: the drain is where it runs */
  return id;
}

void rolltui_tasks_cancel(RolltuiTasks* t, unsigned int id) {
  size_t i;
  if (!t || id == 0) return;
  rolltui_mutex_lock(&t->lock);
  rolltui_cond_broadcast(&t->answered); /* a cancelled job waiting on a call or a question stops waiting */
  for (i = 0; i < t->live_n; ++i) {
    Job* j = t->live[i];
    if (j->id != id) continue;
    j->cancelled = 1;
    if (!j->started) { /* never taken: off the queue, and delivered as cancelled */
      Job** at = &t->pending;
      Job* prev = NULL;
      while (*at && *at != j) prev = *at, at = &(*at)->next;
      if (*at) {
        *at = j->next;
        if (t->pending_tail == j) t->pending_tail = prev;
        j->next = NULL;
        j->finished = 1;
        if (t->finished_tail) t->finished_tail->next = j;
        else t->finished = j;
        t->finished_tail = j;
        rolltui_mutex_unlock(&t->lock);
        wake(t);
        return;
      }
    }
    break;
  }
  rolltui_mutex_unlock(&t->lock);
}

int rolltui_job_cancelled(const RolltuiJob* job) { return job ? job->cancelled != 0 : 1; }

void rolltui_tasks_post(RolltuiTasks* t, RolltuiPostFn fn, void* ctx) {
  Posted* p;
  if (!t || !fn) return;
  p = (Posted*)rolltui_mem_alloc(sizeof *p);
  p->fn = fn;
  p->ctx = ctx;
  p->next = NULL;
  p->ran = NULL;
  rolltui_mutex_lock(&t->lock);
  if (t->posted_tail) t->posted_tail->next = p;
  else t->posted = p;
  t->posted_tail = p;
  rolltui_mutex_unlock(&t->lock);
  wake(t);
}

/* A post still queued, taken off (under the lock): 1 when it was there, so it never runs. */
static int withdraw(RolltuiTasks* t, Posted* p) {
  Posted** at = &t->posted;
  Posted* prev = NULL;
  while (*at && *at != p) prev = *at, at = &(*at)->next;
  if (!*at) return 0;
  *at = p->next;
  if (t->posted_tail == p) t->posted_tail = prev;
  return 1;
}

int rolltui_tasks_call(RolltuiTasks* t, const RolltuiJob* job, RolltuiPostFn fn, void* ctx) {
  Posted* p;
  int ran = 0;
  if (!t || !fn) return 0;
  rolltui_mutex_lock(&t->lock);
  if (t->drainer == rolltui_os_thread_self()) { /* this is the thread that would run it */
    rolltui_mutex_unlock(&t->lock);
    fn(ctx);
    return 1;
  }
  if (t->stopping || (job && job->cancelled)) {
    rolltui_mutex_unlock(&t->lock);
    return 0;
  }
  p = (Posted*)rolltui_mem_alloc(sizeof *p);
  p->fn = fn;
  p->ctx = ctx;
  p->next = NULL;
  p->ran = &ran;
  if (t->posted_tail) t->posted_tail->next = p;
  else t->posted = p;
  t->posted_tail = p;
  rolltui_mutex_unlock(&t->lock);
  wake(t);
  rolltui_mutex_lock(&t->lock);
  while (!ran) {
    if ((t->stopping || (job && job->cancelled)) && withdraw(t, p)) { /* still queued: it never runs */
      rolltui_mutex_unlock(&t->lock);
      rolltui_mem_free(p);
      return 0;
    }
    rolltui_cond_wait(&t->answered, &t->lock); /* (taken by the drain, it runs to its end, and `ran` is set) */
  }
  rolltui_mutex_unlock(&t->lock);
  return 1;
}

typedef struct AskCall {
  RolltuiAskFn ask;
  void* ctx;
  unsigned int question;
} AskCall;
static void ask_here(void* ctx) {
  AskCall* a = (AskCall*)ctx;
  a->ask(a->ctx, a->question);
}

int rolltui_job_ask(RolltuiTasks* t, const RolltuiJob* job, RolltuiAskFn ask, void* ctx, RolltuiStr* out) {
  Question q;
  AskCall a;
  Question** at;
  int result;
  if (!t || !ask) return -1;
  memset(&q, 0, sizeof q);
  rolltui_mutex_lock(&t->lock);
  if (t->stopping || t->drainer == rolltui_os_thread_self() || (job && job->cancelled)) {
    rolltui_mutex_unlock(&t->lock);
    return -1;
  }
  t->next_question = t->next_question == 0xFFFFFFFFu ? 1u : t->next_question + 1u;
  q.id = t->next_question;
  q.next = t->questions;
  t->questions = &q;
  rolltui_mutex_unlock(&t->lock);
  a.ask = ask;
  a.ctx = ctx;
  a.question = q.id;
  result = rolltui_tasks_call(t, job, ask_here, &a) ? 0 : -1;
  rolltui_mutex_lock(&t->lock);
  while (result == 0 && !q.answered && !t->stopping && !(job && job->cancelled)) rolltui_cond_wait(&t->answered, &t->lock);
  for (at = &t->questions; *at; at = &(*at)->next) /* off the list before it leaves the stack */
    if (*at == &q) {
      *at = q.next;
      break;
    }
  result = q.answered ? (q.yes ? 1 : 0) : -1;
  rolltui_mutex_unlock(&t->lock);
  if (out && result >= 0) rolltui_str_set(out, q.text.p ? q.text.p : "", q.text.n);
  rolltui_str_free(&q.text);
  return result;
}

int rolltui_tasks_answer(RolltuiTasks* t, unsigned int question, const char* text, size_t len, int yes) {
  Question* q;
  int found = 0;
  if (!t || question == 0) return 0;
  rolltui_mutex_lock(&t->lock);
  for (q = t->questions; q; q = q->next)
    if (q->id == question && !q->answered) {
      rolltui_str_set(&q->text, text ? text : "", text ? len : 0);
      q->yes = yes != 0;
      q->answered = 1;
      found = 1;
      rolltui_cond_broadcast(&t->answered);
      break;
    }
  rolltui_mutex_unlock(&t->lock);
  return found;
}

int rolltui_tasks_running(RolltuiTasks* t, unsigned int id) {
  size_t i;
  int live = 0;
  if (!t || id == 0) return 0;
  rolltui_mutex_lock(&t->lock);
  for (i = 0; i < t->live_n && !live; ++i) live = t->live[i]->id == id;
  rolltui_mutex_unlock(&t->lock);
  return live;
}

int rolltui_tasks_workers(const RolltuiTasks* t) { return t ? t->worker_n : 0; }

size_t rolltui_tasks_busy(RolltuiTasks* t) {
  size_t n;
  if (!t) return 0;
  rolltui_mutex_lock(&t->lock);
  n = t->live_n;
  rolltui_mutex_unlock(&t->lock);
  return n;
}

int rolltui_tasks_wait_idle(RolltuiTasks* t, int timeout_ms) {
  const unsigned long long end = rolltui_os_monotonic_ms() + (unsigned long long)(timeout_ms < 0 ? 0 : timeout_ms);
  if (!t) return 1;
  for (;;) {
    rolltui_tasks_drain(t);
    if (rolltui_tasks_busy(t) == 0) return 1;
    if (timeout_ms >= 0 && rolltui_os_monotonic_ms() >= end) return 0;
    rolltui_os_sleep_ms(2);
  }
}
