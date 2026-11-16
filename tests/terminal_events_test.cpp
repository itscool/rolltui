//
// terminal_events_test.cpp — WHAT THE TERMINAL TELLS THE PROGRAM WHILE IT RUNS: its window gaining and losing the person's focus,
// its background turning light or dark, and the job being stopped and continued.
//
// The decoder's half is tested on bytes. The rest runs `rolltui_run` in a child on a pty and reads what the pty receives: the modes
// asked for on entry and given back on leaving, the screen drawn again after a stop, and what the app's hooks were told.
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_keys.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to feed the decoder */
#include "rolltui_test.hpp"
#include "vt_model.hpp"

using testkit::check;

namespace {

void collect(void* ctx, const RolltuiEvent* e) { static_cast<std::vector<RolltuiEvent>*>(ctx)->push_back(*e); }

void test_decoder() {
  RolltuiKeyDecoder* d = rolltui_key_decoder_new();
  std::vector<RolltuiEvent> got;
  rolltui_key_decoder_feed(d, "\x1b[I\x1b[O", 6, collect, &got);
  check(got.size() == 2 && got[0].kind == ROLLTUI_EVENT_FOCUS && got[0].focused == 1 && got[1].kind == ROLLTUI_EVENT_FOCUS &&
            got[1].focused == 0,
        "CSI I and CSI O are the window gaining and losing focus, not keys");
  got.clear();
  rolltui_key_decoder_feed(d, "\x1b[?997;1na", 10, collect, &got);
  check(got.size() == 1 && got[0].kind == ROLLTUI_EVENT_KEY && got[0].key.ch == 'a',
        "a light-or-dark report is a reply, never typing: only the key after it arrives");
  rolltui_key_decoder_free(d);
}

// ---- the child: a run whose screen says what it was told ----------------------------------------------------------------------

struct Child {
  int frames = 0, focused = -1;
  std::string facts = "none";
  RolltuiDrawScratch* ds = nullptr;
};

int child_main() {
  Child c;
  c.ds = rolltui_draw_scratch_new();
  RolltuiRunApp app{};
  app.ctx = &c;
  app.render = [](void* ctx, RolltuiRun*, RolltuiFrame* f, int w, int, unsigned long long) {
    Child& ch = *static_cast<Child*>(ctx);
    char line[128];
    const int n = std::snprintf(line, sizeof line, "frame %d focus %d facts %s", ++ch.frames, ch.focused, ch.facts.c_str());
    rolltui_frame_put_text(f, ch.ds, 0, 0, line, static_cast<std::size_t>(n), RolltuiStyle{}, w, 0, 0);
    return -1;
  };
  app.event = [](void*, RolltuiRun* run, const RolltuiEvent* e) {
    if (e->kind != ROLLTUI_EVENT_KEY || e->key.key != ROLLTUI_KEY_CHAR) return;
    if (e->key.ch == 'q') rolltui_run_stop(run);
    if (e->key.ch == 'z') {
      const int stopped = rolltui_run_suspend_job(run);
      const char m[] = "JOB_STOPPED_AND_BACK";
      if (stopped) (void)!::write(2, m, sizeof m - 1);
    }
  };
  app.focus = [](void* ctx, RolltuiRun*, int focused) { static_cast<Child*>(ctx)->focused = focused; };
  app.facts_changed = [](void* ctx, RolltuiRun* run) {
    RolltuiTermFacts f{};
    rolltui_terminal_facts(rolltui_run_terminal(run), &f);
    static_cast<Child*>(ctx)->facts = f.mode == ROLLTUI_MODE_LIGHT ? "light" : "dark";
  };
  RolltuiTerminalOptions o{};
  o.alt_screen = 1;
  o.no_probe = 1;
  o.no_cache = 1;
  const int rc = rolltui_run(0, 1, o, &app);
  rolltui_draw_scratch_free(c.ds);
  return rc == ROLLTUI_RUN_STOPPED ? 0 : 3;
}

struct Pty {
  int master = -1;
  pid_t pid = -1;
  std::string all;
  vt::Screen screen{60, 10}; /* what the bytes leave on a screen: a diff writes only what changed */
  void pump(int ms) {
    const std::uint64_t end = testkit::mono_ms() + static_cast<std::uint64_t>(ms);
    while (testkit::mono_ms() < end) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 20) > 0 && (p.revents & POLLIN)) {
        char b[8192];
        const ssize_t k = ::read(master, b, sizeof b);
        if (k <= 0) return;
        all.append(b, static_cast<std::size_t>(k));
        screen.feed(std::string(b, static_cast<std::size_t>(k)));
      }
    }
  }
  // Reads until `needle` appears after byte `from`; true when it did.
  bool wait_for(const std::string& needle, std::size_t from, int budget_ms) {
    const std::uint64_t began = testkit::mono_ms();
    while (testkit::mono_ms() - began < static_cast<std::uint64_t>(budget_ms)) {
      pump(40);
      if (all.find(needle, from) != std::string::npos) return true;
    }
    return false;
  }
  // Reads until `text` is on the screen.
  bool wait_shown(const std::string& text, int budget_ms) {
    const std::uint64_t began = testkit::mono_ms();
    while (testkit::mono_ms() - began < static_cast<std::uint64_t>(budget_ms)) {
      pump(40);
      if (!screen.find(text).empty()) return true;
    }
    return false;
  }
  void send(const std::string& s) { (void)!::write(master, s.data(), s.size()); }
};

void test_live() {
  winsize ws{};
  ws.ws_row = 10;
  ws.ws_col = 60;
  Pty t;
  t.pid = ::forkpty(&t.master, nullptr, nullptr, &ws);
  if (t.pid == 0) {
    ::setenv("TERM", "xterm-256color", 1);
    ::_exit(child_main());
  }
  check(t.pid > 0 && t.wait_for("frame 1", 0, 4000), "the run starts on a pty and draws");
  check(t.all.find("\x1b[?1004h") != std::string::npos && t.all.find("\x1b[?2031h") != std::string::npos,
        "entering asks the terminal to report focus and light-or-dark as they change");
  std::size_t at = t.all.size();
  t.send("\x1b[O");
  check(t.wait_shown("focus 0", 3000), "the window losing focus reaches the app's focus hook");
  at = t.all.size();
  t.send("\x1b[I");
  check(t.wait_shown("focus 1", 3000), "…and gaining it again");
  at = t.all.size();
  t.send("\x1b[?997;2n");
  check(t.wait_shown("facts light", 3000), "a light report while it runs is the terminal's facts changing, and the app re-reads them");
  // A STOP FROM OUTSIDE. A program on a fresh pty leads an orphaned group, whose stop the system discards: what is tested is that the
  // terminal is left as found, and entered again and drawn whole after.
  at = t.all.size();
  ::kill(t.pid, SIGTSTP);
  const bool left = t.wait_for("\x1b[?1049l", at, 3000);
  int st = 0;
  if (::waitpid(t.pid, &st, WUNTRACED | WNOHANG) == t.pid && WIFSTOPPED(st)) ::kill(t.pid, SIGCONT);
  const bool back = t.wait_for("\x1b[?1049h", at, 3000) && t.wait_for("frame", t.all.find("\x1b[?1049h", at), 3000);
  check(left && back, "a stop sent from outside leaves the terminal as it was found, and the continued program enters it again and draws");
  at = t.all.size();
  t.send("z");
  const bool job = t.wait_for("\x1b[?1049l", at, 3000);
  if (::waitpid(t.pid, &st, WUNTRACED | WNOHANG) == t.pid && WIFSTOPPED(st)) ::kill(t.pid, SIGCONT);
  check(job && t.wait_for("\x1b[?1049h", at, 3000) && t.wait_for("JOB_STOPPED_AND_BACK", at, 3000),
        "rolltui_run_suspend_job leaves the terminal, stops, and on continuing enters it again");
  at = t.all.size();
  t.send("q");
  int status = 0;
  std::string rest;
  check(rolltui_test::end_pty_child(t.master, t.pid, 3000, &status, &rest) && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "it quits when asked");
  check((t.all.substr(at) + rest).find("\x1b[?1004l") != std::string::npos, "…and gives the focus reports back on the way out");
}

}  // namespace

int main() {
  test_decoder();
  test_live();
  return testkit::report("terminal_events_test");
}
