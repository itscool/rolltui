//
// tasks_test.cpp — WORK OFF THE UI THREAD: jobs run on workers and come back to the draining thread, cancelled ones dropped,
// every `done` called once; posts from other threads; the run loop's timers, debounce and its own tasks; and the column
// browser's preview reading a file on a worker while the cursor moves on.
//
#include <poll.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_preview.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_syntax.h"
#include "rolltui_test.hpp"

using testkit::check;

namespace {

struct Job {
  int input = 0, output = 0;
  std::atomic<bool> ran{false}, saw_cancel{false};
  int sleep_ms = 0;
  std::thread::id done_on;
  int done_calls = 0, cancelled = -1;
};

void work(void* ctx, const RolltuiJob* job) {
  Job* j = static_cast<Job*>(ctx);
  j->ran = true;
  for (int i = 0; i < j->sleep_ms / 5; ++i) {
    if (rolltui_job_cancelled(job)) {
      j->saw_cancel = true;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  j->output = j->input * j->input;
}

void done(void* ctx, int cancelled) {
  Job* j = static_cast<Job*>(ctx);
  ++j->done_calls;
  j->cancelled = cancelled;
  j->done_on = std::this_thread::get_id();
}

std::atomic<int> g_wakes{0};
void count_wake(void*) { ++g_wakes; }

void test_jobs() {
  RolltuiTasks* t = rolltui_tasks_new(2, count_wake, nullptr);
  std::vector<Job> jobs(20);
  for (int i = 0; i < 20; ++i) {
    jobs[i].input = i;
    check(rolltui_tasks_start(t, work, done, &jobs[i]) != 0, i == 0 ? "a job has an id, never 0" : "");
  }
  check(rolltui_tasks_wait_idle(t, 5000) == 1 && rolltui_tasks_busy(t) == 0, "twenty jobs run and come back");
  bool all = true;
  for (int i = 0; i < 20; ++i)
    all = all && jobs[i].output == i * i && jobs[i].done_calls == 1 && jobs[i].cancelled == 0 &&
          jobs[i].done_on == std::this_thread::get_id();
  check(all, "each `done` once, on the draining thread, with what its work made");
  check(g_wakes > 0, "a finished job wakes the loop");

  // One worker, kept busy, so the next job waits in the queue and is cancelled there.
  RolltuiTasks* one = rolltui_tasks_new(1, nullptr, nullptr);
  Job busy, queued, running;
  busy.sleep_ms = 200;
  rolltui_tasks_start(one, work, done, &busy);
  const unsigned qid = rolltui_tasks_start(one, work, done, &queued);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  rolltui_tasks_cancel(one, qid);
  rolltui_tasks_wait_idle(one, 5000);
  check(!queued.ran && queued.done_calls == 1 && queued.cancelled == 1, "a job cancelled in the queue never runs, and is told");
  running.sleep_ms = 2000;
  const unsigned rid = rolltui_tasks_start(one, work, done, &running);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  rolltui_tasks_cancel(one, rid);
  rolltui_tasks_wait_idle(one, 5000);
  check(running.saw_cancel && running.done_calls == 1 && running.cancelled == 1, "one cancelled while it runs sees it, and stops");
  rolltui_tasks_cancel(one, rid);
  check(rolltui_tasks_busy(one) == 0, "cancelling an id that has ended does nothing");

  // Posts from another thread arrive on the draining thread, in order.
  std::vector<int> order;
  struct Post {
    std::vector<int>* order;
    int n;
  };
  std::vector<Post> posts;
  for (int i = 0; i < 5; ++i) posts.push_back(Post{&order, i});
  std::thread poster([&] {
    for (Post& p : posts) rolltui_tasks_post(one, [](void* c) { Post* q = static_cast<Post*>(c); q->order->push_back(q->n); }, &p);
  });
  poster.join();
  rolltui_tasks_drain(one);
  check(order == std::vector<int>{0, 1, 2, 3, 4}, "posts from another thread run here, in the order posted");

  // Freed with work still queued: every `done` is called before free returns.
  Job a, b, c;
  a.sleep_ms = 100;
  rolltui_tasks_start(one, work, done, &a);
  rolltui_tasks_start(one, work, done, &b);
  rolltui_tasks_start(one, work, done, &c);
  rolltui_tasks_free(one);
  check(a.done_calls == 1 && b.done_calls == 1 && c.done_calls == 1 && b.cancelled == 1,
        "freeing the tasks cancels what is queued and calls every `done` first");
  rolltui_tasks_free(t);
}

// ---- the run loop's timers and tasks ----------------------------------------------------------

struct RunState {
  int every_fired = 0, after_fired = 0, debounced = 0, cancelled_fired = 0;
  unsigned every_id = 0, debounce_slot = 0;
  Job job;
  bool job_done = false;
  unsigned long long started = 0, stopped_at = 0;
};

int render(void*, RolltuiRun*, RolltuiFrame*, int, int, unsigned long long) { return -1; }
void event(void*, RolltuiRun*, const RolltuiEvent*) {}

void test_run_timers() {
  int master = -1, slave = -1;
  winsize ws{};
  ws.ws_row = 10;
  ws.ws_col = 40;
  check(openpty(&master, &slave, nullptr, nullptr, &ws) == 0, "openpty");
  std::atomic<bool> stop{false};
  std::thread reader([&] {
    while (!stop) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0) {
        char buf[4096];
        (void)!::read(master, buf, sizeof buf);
      }
    }
  });
  RunState s;
  RolltuiRunApp app{};
  app.ctx = &s;
  app.render = render; // waits forever: only a timer or a job wakes it
  app.event = event;
  app.start = [](void* c, RolltuiRun* run) {
    RunState& st = *static_cast<RunState*>(c);
    st.started = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    st.every_id = rolltui_run_every(run, 20, [](void* c2, RolltuiRun*) { ++static_cast<RunState*>(c2)->every_fired; }, c);
    const unsigned gone = rolltui_run_after(run, 30, [](void* c2, RolltuiRun*) { ++static_cast<RunState*>(c2)->cancelled_fired; }, c);
    rolltui_run_cancel_timer(run, gone);
    for (int i = 0; i < 3; ++i)  // a burst: one call to the debounced function, after the last
      rolltui_run_debounce(run, &st.debounce_slot, 40, [](void* c2, RolltuiRun*) { ++static_cast<RunState*>(c2)->debounced; }, c);
    st.job.input = 7;
    rolltui_tasks_start(
        rolltui_run_tasks(run),
        [](void* c2, const RolltuiJob* job) { work(&static_cast<RunState*>(c2)->job, job); },
        [](void* c2, int) {
          RunState* st2 = static_cast<RunState*>(c2);
          st2->job_done = st2->job.output == 49;
        },
        c);
    rolltui_run_after(run, 200, [](void* c2, RolltuiRun* r) {
      RunState& st2 = *static_cast<RunState*>(c2);
      ++st2.after_fired;
      st2.stopped_at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      rolltui_run_stop(r);
    }, c);
  };
  RolltuiTerminalOptions o{};
  o.alt_screen = 0;
  o.mouse = 0;
  o.no_probe = 1;
  o.no_cache = 1;
  const int rc = rolltui_run(slave, slave, o, &app);
  stop = true;
  reader.join();
  ::close(master);
  ::close(slave);
  check(rc == ROLLTUI_RUN_STOPPED && s.after_fired == 1, "a loop that waits forever is woken by a timer, which stops it");
  check(s.stopped_at - s.started >= 190 && s.stopped_at - s.started < 1000, "…after the 200 ms it was set for");
  check(s.every_fired >= 5 && s.every_fired <= 12, "a periodic timer fired about every 20 ms [" + std::to_string(s.every_fired) + "]");
  check(s.cancelled_fired == 0, "a cancelled timer never fires");
  check(s.debounced == 1, "a burst of three debounced calls fires once");
  check(s.job_done, "a job started on the run's tasks came back on the run's thread, its work done");
}

// ---- the preview, read on a worker -----------------------------------------------------------

void test_preview_off_thread() {
  char tmpl[] = "/tmp/rolltui_tasks_XXXXXX";
  const std::string dir = mkdtemp(tmpl);
  const std::string a = dir + "/a.txt", b = dir + "/b.txt";
  std::ofstream(a) << "alpha\n";
  std::ofstream(b) << "beta\nbeta\n";
  RolltuiTasks* t = rolltui_tasks_new(1, nullptr, nullptr);
  RolltuiPreview* pv = rolltui_preview_new();
  rolltui_preview_set_tasks(pv, t);
  rolltui_preview_set_now(pv, 1000);
  rolltui_preview_set_path(pv, a.data(), a.size());
  check(rolltui_preview_kind(pv) == ROLLTUI_PREVIEW_LOADING, "with workers lent, a file is read off this thread: LOADING meanwhile");
  check(rolltui_preview_wake_ms(pv) == 100, "…and the frame that says \"loading\" is 100 ms away");
  rolltui_tasks_wait_idle(t, 5000);
  check(rolltui_preview_kind(pv) == ROLLTUI_PREVIEW_TEXT, "the read comes back and the text is shown");
  rolltui_preview_set_path(pv, b.data(), b.size());
  rolltui_preview_set_path(pv, a.data(), a.size());
  rolltui_tasks_wait_idle(t, 5000);
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiFrame* f = rolltui_frame_new(30, 4, RolltuiStyle{});
  rolltui_preview_draw(pv, f, RolltuiRect{0, 0, 30, 4}, styles, 0, 0);
  RolltuiStr text;
  rolltui_frame_to_text(f, &text);
  const std::string shown(text.data(), text.size());
  check(shown.find("alpha") != std::string::npos && shown.find("beta") == std::string::npos,
        "a file the cursor left before its read finished is dropped: the one it rests on is shown");
  rolltui_frame_free(f);
  rolltui_preview_set_path(pv, b.data(), b.size());
  rolltui_preview_free(pv);  // freed with its read in flight
  rolltui_tasks_wait_idle(t, 5000);
  check(rolltui_tasks_busy(t) == 0, "a preview freed while its read was in flight lets it finish unseen");
  // COLOURED WHERE IT WAS READ: a file in a known language comes back with its colours, made on the worker
  const std::string cfile = dir + "/m.c";
  {
    std::ofstream out(cfile);
    for (int i = 0; i < 4000; ++i) out << "int f" << i << "(void) { return " << i << "; } /* note */\n";
  }
  RolltuiSyntax* syn = rolltui_syntax_new_standard(nullptr);
  RolltuiPreview* pc = rolltui_preview_new();
  rolltui_preview_set_tasks(pc, t);
  rolltui_preview_set_syntax(pc, syn);
  rolltui_preview_set_path(pc, cfile.data(), cfile.size());
  rolltui_tasks_wait_idle(t, static_cast<int>(5000 * rolltui_test::kClockScale));
  {
    RolltuiStyle st[ROLLTUI_ROLE_COUNT]{};
    RolltuiEffectMap* fxm = rolltui_theme_builtin_fill("default-dark", 12, st, ROLLTUI_ROLE_COUNT);
    RolltuiFrame* fc = rolltui_frame_new(60, 6, st[ROLLTUI_ROLE_BACKGROUND]);
    rolltui_preview_draw(pc, fc, RolltuiRect{0, 0, 60, 6}, st, 0, 0);
    int kx = -1, ky = -1;
    for (int y = 0; y < 6 && kx < 0; ++y)
      for (int x = 0; x + 6 < 60; ++x) { /* "return", a keyword */
        bool word = true;
        for (int k = 0; k < 6 && word; ++k) {
          RolltuiCellRaw c{};
          rolltui_frame_cell(fc, x + k, y, &c);
          word = c.len == 1 && c.bytes[0] == "return"[k];
        }
        if (word) { kx = x, ky = y; break; }
      }
    RolltuiCellRaw kc{};
    if (kx >= 0) rolltui_frame_cell(fc, kx, ky, &kc);
    check(kx >= 0 && kc.style.bold, "a file read on a worker comes back coloured: its keywords bold, made where it was read");
    rolltui_frame_free(fc);
    rolltui_effect_map_free(fxm);
  }
  // FREED WITH ITS READ OUT: the read holds the languages it colours with, so the host's free leaves them until the read lets go
  rolltui_preview_set_path(pc, a.data(), a.size());
  rolltui_preview_set_path(pc, cfile.data(), cfile.size());
  rolltui_preview_free(pc);
  rolltui_syntax_free(syn);
  rolltui_tasks_wait_idle(t, static_cast<int>(5000 * rolltui_test::kClockScale));
  check(rolltui_tasks_busy(t) == 0, "a preview and its languages freed while a read colours with them: the read finishes on its own hold");
  rolltui_tasks_free(t);
  std::remove(cfile.c_str());
  std::remove(a.c_str());
  std::remove(b.c_str());
  rmdir(dir.c_str());
}

}  // namespace

// NO WORKERS: a browser starts no thread, so each job runs on the thread that drains, and nothing waits forever.
void test_no_workers() {
  g_wakes = 0;
  RolltuiTasks* t = rolltui_tasks_new(0, count_wake, nullptr);
  std::vector<Job> jobs(5);
  for (int i = 0; i < 5; ++i) {
    jobs[i].input = i + 2;
    rolltui_tasks_start(t, work, done, &jobs[i]);
  }
  check(g_wakes >= 5 && !jobs[0].ran, "with no workers a job waits for the drain, and starting it wakes the loop to drain");
  const std::thread::id here = std::this_thread::get_id();
  check(rolltui_tasks_drain(t) == 5 && rolltui_tasks_busy(t) == 0, "one drain runs every job and delivers it");
  bool all = true;
  for (int i = 0; i < 5; ++i) all = all && jobs[i].output == (i + 2) * (i + 2) && jobs[i].done_calls == 1 && jobs[i].done_on == here;
  check(all, "…each on the draining thread, with what its work made");
  Job dropped;
  const unsigned int id = rolltui_tasks_start(t, work, done, &dropped);
  rolltui_tasks_cancel(t, id);
  check(rolltui_tasks_wait_idle(t, 1000) == 1 && !dropped.ran && dropped.cancelled == 1, "a job cancelled before the drain never runs, and is told");
  rolltui_tasks_free(t);
}

// A JOB THAT CALLS THE UI THREAD: it waits until what it handed over has run there, on that thread, and a cancel frees it
void test_call() {
  RolltuiTasks* t = rolltui_tasks_new(1, nullptr, nullptr);
  struct Ctx {
    RolltuiTasks* t;
    std::thread::id ran_on;
    int value = 0;
    int got = -1;
    bool waited = false;
  } c;
  c.t = t;
  rolltui_tasks_start(t, [](void* p, const RolltuiJob* job) {
    Ctx* x = static_cast<Ctx*>(p);
    const int r = rolltui_tasks_call(x->t, job, [](void* q) {
      Ctx* y = static_cast<Ctx*>(q);
      y->ran_on = std::this_thread::get_id();
      y->value = 42;
    }, x);
    x->got = r;
    x->waited = x->value == 42; /* seen by the worker after the call returned */
  }, [](void*, int) {}, &c);
  const std::thread::id here = std::this_thread::get_id();
  check(rolltui_tasks_wait_idle(t, static_cast<int>(5000 * rolltui_test::kClockScale)) == 1, "a job calling the UI thread ends once the drain ran its call");
  check(c.got == 1 && c.waited && c.ran_on == here, "…its call ran on the draining thread, and the job saw what it did after the call returned");
  // from the draining thread itself: at once
  int direct = 0;
  check(rolltui_tasks_call(t, nullptr, [](void* q) { *static_cast<int*>(q) = 7; }, &direct) == 1 && direct == 7,
        "a call from the draining thread runs at once rather than waiting on itself");
  // cancelled while it waits: never runs
  struct Late {
    RolltuiTasks* t;
    std::atomic<int> state{0}; /* 1 posted, 2 returned */
    int got = -1;
    bool ran = false;
  } l;
  l.t = t;
  const unsigned int id = rolltui_tasks_start(t, [](void* p, const RolltuiJob* job) {
    Late* x = static_cast<Late*>(p);
    x->state = 1;
    x->got = rolltui_tasks_call(x->t, job, [](void* q) { static_cast<Late*>(q)->ran = true; }, x);
    x->state = 2;
  }, [](void*, int) {}, &l);
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(3000 * rolltui_test::kClockScale));
  while (l.state.load() != 1 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(20)); /* it is waiting on its call, nothing drained */
  rolltui_tasks_cancel(t, id);
  while (l.state.load() != 2 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  rolltui_tasks_wait_idle(t, 2000);
  check(l.state.load() == 2 && l.got == 0 && !l.ran, "a job cancelled while it waits on a call stops waiting, and the call never runs");
  rolltui_tasks_free(t);
}

// A JOB THAT ASKS THE PERSON: paused until the answer, then on with it; a refusal is 0; a cancel frees it with -1
void test_ask() {
  RolltuiTasks* t = rolltui_tasks_new(1, nullptr, nullptr);
  struct Ctx {
    RolltuiTasks* t;
    std::atomic<unsigned int> question{0};
    int result = -2;
    std::string answer;
    std::atomic<bool> finished{false};
  };
  auto run = [&](Ctx& c) {
    c.t = t;
    return rolltui_tasks_start(t, [](void* p, const RolltuiJob* job) {
      Ctx* x = static_cast<Ctx*>(p);
      RolltuiStr out{};
      x->result = rolltui_job_ask(x->t, job, [](void* q, unsigned int question) { static_cast<Ctx*>(q)->question = question; }, x, &out);
      x->answer.assign(out.p ? out.p : "", out.n);
      rolltui_str_free(&out);
      x->finished = true;
    }, [](void*, int) {}, &c);
  };
  auto wait_for = [&](const std::function<bool()>& ok) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(3000 * rolltui_test::kClockScale));
    while (!ok() && std::chrono::steady_clock::now() < until) {
      rolltui_tasks_drain(t);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  Ctx a;
  run(a);
  wait_for([&] { return a.question.load() != 0; });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  check(a.question.load() != 0 && !a.finished.load(), "a job asking the person puts the question on the UI thread and waits");
  check(rolltui_tasks_answer(t, a.question.load(), "hunter2", 7, 1) == 1, "…the answer reaches the job that asked");
  wait_for([&] { return a.finished.load(); });
  check(a.result == 1 && a.answer == "hunter2", "…which carries on with it [" + a.answer + "]");
  check(rolltui_tasks_answer(t, a.question.load(), "x", 1, 1) == 0, "an answer to a question no one waits on any more is refused");
  Ctx no;
  run(no);
  wait_for([&] { return no.question.load() != 0; });
  rolltui_tasks_answer(t, no.question.load(), nullptr, 0, 0);
  wait_for([&] { return no.finished.load(); });
  check(no.result == 0, "a refusal (No, Escape) is 0");
  Ctx gone;
  const unsigned int id = run(gone);
  wait_for([&] { return gone.question.load() != 0; });
  rolltui_tasks_cancel(t, id);
  wait_for([&] { return gone.finished.load(); });
  check(gone.result == -1 && rolltui_tasks_answer(t, gone.question.load(), "late", 4, 1) == 0,
        "a job cancelled while it waits on its question stops with -1, and the late answer finds no one");
  check(rolltui_job_ask(t, nullptr, [](void*, unsigned int) {}, nullptr, nullptr) == -1, "the draining thread cannot ask: it would wait on itself");
  rolltui_tasks_wait_idle(t, 2000);
  rolltui_tasks_free(t);
}

int main() {
  test_jobs();
  test_call();
  test_ask();
  test_no_workers();
  test_run_timers();
  test_preview_off_thread();
  return testkit::report("tasks_test");
}
