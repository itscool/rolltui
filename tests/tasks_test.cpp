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
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_preview.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
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
  rolltui_tasks_free(t);
  std::remove(a.c_str());
  std::remove(b.c_str());
  rmdir(dir.c_str());
}

}  // namespace

int main() {
  test_jobs();
  test_run_timers();
  test_preview_off_thread();
  return testkit::report("tasks_test");
}
