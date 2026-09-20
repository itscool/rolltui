//
// run_test.cpp — THE RUN LOOP, on a real pty. The library runs in this process on the slave; a forked child plays the
// terminal on the master: it types, pastes, resizes the window and reads what was drawn, and says what it saw. Public
// API only — what an app that owns no loop of its own can reach.
//
// What is held to account is what an app used to have to remember for itself: a frame drawn at the size it was given,
// an event's text valid when the app is called, a repaint after an explicit request, after a child program has had the
// terminal and after a resize, and the terminal put back before the call returns.
//
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::size_t live_bytes() {
  std::size_t live = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &live, nullptr, nullptr);
  return live;
}

// ---- the app under test ------------------------------------------------------------------------------------------
struct State {
  RolltuiDrawScratch* ds = nullptr;
  int starts = 0, grounds = 0, renders = 0, settles = 0, keys = 0, resizes = 0, facts = 0;
  bool start_had_terminal = false;
  int start_w = 0, start_h = 0;
  int render_w = 0, render_h = 0;
  int resized_w = 0, resized_h = 0;
  std::string typed;   // the characters of key events, in order
  std::string pasted;  // what a paste event's text said, copied at the moment it was called
  bool text_seen_valid = true;
};

void app_start(void* c, RolltuiRun* run) {
  State* s = static_cast<State*>(c);
  ++s->starts;
  RolltuiTerminal* t = rolltui_run_terminal(run);
  s->start_had_terminal = t != nullptr;
  if (t) {
    s->start_w = rolltui_terminal_width(t);
    s->start_h = rolltui_terminal_height(t);
  }
}

void app_ground(void* c, RolltuiRun*, RolltuiStyle*) { ++static_cast<State*>(c)->grounds; }

int app_render(void* c, RolltuiRun*, RolltuiFrame* f, int w, int h, unsigned long long now_ms) {
  State* s = static_cast<State*>(c);
  ++s->renders;
  s->render_w = w;
  s->render_h = h;
  (void)now_ms;
  char line[96];
  const int n = std::snprintf(line, sizeof line, "READY %dx%d keys=%d", w, h, s->keys);
  RolltuiStyle style{};
  rolltui_frame_put_text(f, s->ds, 0, 0, line, static_cast<std::size_t>(n), style, w, 0, 0);
  if (!s->pasted.empty()) {
    const int m = std::snprintf(line, sizeof line, "paste=%s", s->pasted.c_str());
    rolltui_frame_put_text(f, s->ds, 0, 1, line, static_cast<std::size_t>(m), style, w, 0, 0);
  }
  return 25;  // a wait short enough that a wake with no events happens too
}

void app_event(void* c, RolltuiRun* run, const RolltuiEvent* e) {
  State* s = static_cast<State*>(c);
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    // copied NOW: the terminal's own buffer for it is gone by the time the next frame is drawn
    if (e->text) s->pasted.assign(e->text, e->text_len);
    else s->text_seen_valid = false;
    return;
  }
  if (e->kind != ROLLTUI_EVENT_KEY || e->key.key != ROLLTUI_KEY_CHAR) return;
  ++s->keys;
  const char ch = static_cast<char>(e->key.ch);
  s->typed.push_back(ch);
  if (ch == 'q') rolltui_run_stop(run);
  else if (ch == 'r') rolltui_run_invalidate(run);
  else if (ch == 's') {
    rolltui_run_suspend(run);
    rolltui_run_resume(run);
  }
}

void app_resized(void* c, RolltuiRun*, int w, int h) {
  State* s = static_cast<State*>(c);
  ++s->resizes;
  s->resized_w = w;
  s->resized_h = h;
}

void app_facts(void* c, RolltuiRun*) { ++static_cast<State*>(c)->facts; }

void app_settle(void* c, RolltuiRun*) { ++static_cast<State*>(c)->settles; }

RolltuiRunApp make_app(State* s) {
  RolltuiRunApp a;
  a.ctx = s;
  a.start = app_start;
  a.ground = app_ground;
  a.render = app_render;
  a.event = app_event;
  a.resized = app_resized;
  a.facts_changed = app_facts;
  a.settle = app_settle;
  return a;
}

// ---- the child: the terminal's side of the pty ---------------------------------------------------------------------
struct Screen {
  int master;
  std::string all;  // everything the app wrote, in order
  // Reads until `needle` appears at or after `from`, or `ms` pass. Returns where it starts, or npos.
  std::size_t wait_for(const std::string& needle, std::size_t from, int ms) {
    int waited = 0;
    for (;;) {
      const std::size_t at = all.find(needle, from);
      if (at != std::string::npos) return at;
      if (waited > ms) return std::string::npos;
      pollfd p{master, POLLIN, 0};
      const int r = ::poll(&p, 1, 20);
      if (r > 0 && (p.revents & POLLIN)) {
        char b[4096];
        const ssize_t k = ::read(master, b, sizeof b);
        if (k > 0) all.append(b, static_cast<std::size_t>(k));
        else if (k <= 0) return all.find(needle, from);
      } else {
        waited += 20;
      }
    }
  }
  void type(const std::string& s) { (void)!::write(master, s.data(), s.size()); }
};

void say(int report, const std::string& line) { (void)!::write(report, (line + "\n").data(), line.size() + 1); }

void child_terminal(int master, int report) {
  Screen t{master, {}};
  auto step = [&](const char* name, bool ok) { say(report, std::string(ok ? "PASS " : "FAIL ") + name); };
  const int kWait = 3000;

  std::size_t at = t.wait_for("READY 80x24 keys=0", 0, kWait);
  step("the first frame is drawn at the terminal's size", at != std::string::npos);
  if (at == std::string::npos) return;
  step("the alternate screen is entered before the first frame", t.all.find("\x1b[?1049h") < at);

  // Two keys, then one that asks for a repaint. A frame is a DIFF — typing changes one cell, and a cell that did not
  // change is not sent — so the only place the whole line is sent again is a repaint: seeing "keys=3" whole in it is
  // seeing that all three keys were counted, and that the repaint was the whole screen and not only what changed.
  t.type("ab");
  const std::size_t before_r = t.all.size();
  t.type("r");
  step("two keys and an invalidate: the whole screen is sent again, and it counts all three",
       t.wait_for("READY 80x24 keys=3", before_r, kWait) != std::string::npos);

  // a paste's text is valid when the app is called (no space in it: a blank over a blank is a cell the diff skips)
  t.type("\x1b[200~hello-paste\x1b[201~");
  step("a paste's text reaches the app whole", t.wait_for("paste=hello-paste", before_r, kWait) != std::string::npos);

  // a child program has the terminal: it is left, and taken back with the screen repainted whole
  const std::size_t before_s = t.all.size();
  t.type("s");
  const std::size_t left = t.wait_for("\x1b[?1049l", before_s, kWait);
  const std::size_t entered = left == std::string::npos ? left : t.wait_for("\x1b[?1049h", left, kWait);
  step("suspend leaves the alternate screen and resume enters it again",
       left != std::string::npos && entered != std::string::npos);
  step("resume repaints the whole screen",
       entered != std::string::npos && t.wait_for("READY 80x24 keys=4", entered, kWait) != std::string::npos);

  // a resize: the next frame is drawn at the new size
  winsize ws{};
  ws.ws_row = 30;
  ws.ws_col = 100;
  ::ioctl(master, TIOCSWINSZ, &ws);
  const std::size_t before_w = t.all.size();
  ::kill(::getppid(), SIGWINCH);
  step("a resize is drawn at the new size, whole",
       t.wait_for("READY 100x30 keys=4", before_w, kWait) != std::string::npos);

  // quit; the terminal is put back before the call returns
  const std::size_t before_q = t.all.size();
  t.type("q");
  step("the terminal is left, cursor and all, when the loop stops",
       t.wait_for("\x1b[?1049l", before_q, kWait) != std::string::npos);
}

}  // namespace

int main() {
  // NOTHING HERE MAY TOUCH THE PERSON'S OWN CONFIGURATION: a terminal remembers what it learned in the rolltui
  // config directory.
  char tmpl[] = "/tmp/rolltui_run_test_XXXXXX";
  const char* dir = ::mkdtemp(tmpl);
  if (dir) ::setenv("ROLL_CONFIG_DIR", dir, 1);
  ::unsetenv("ROLL_TERM_PROBE");

  // ---- THE PRODUCTS RUN ON IT: none of the three writes a loop of its own again -----------------------------------
  // A loop written by hand is a loop that can forget the depth, the borrowed events, or the repaint after a child
  // program; each product once did. What is left of `rolltui_terminal_poll` and `rolltui_terminal_present` is for a
  // host of a different shape.
  for (const char* rel : {"examples/dirktui.cpp", "examples/paint.cpp", "tools/studio.cpp"}) {
    const std::string src = slurp(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
    check(!src.empty() && src.find("rolltui_run(") != std::string::npos, std::string(rel) + " runs on `rolltui_run`");
    check(src.find("rolltui_terminal_poll(") == std::string::npos && src.find("rolltui_terminal_present(") == std::string::npos,
          std::string(rel) + " has no loop of its own: it never polls or presents the terminal itself");
  }

  // ---- an app that cannot run is refused before anything happens ------------------------------------------------
  {
    const std::size_t base = live_bytes();
    RolltuiRunApp none;
    check(rolltui_run(0, 1, RolltuiTerminalOptions{}, nullptr) == ROLLTUI_RUN_BAD_APP, "no app is refused");
    check(rolltui_run(0, 1, RolltuiTerminalOptions{}, &none) == ROLLTUI_RUN_BAD_APP, "an app with no render and no event is refused");
    State s;
    RolltuiRunApp only_render = make_app(&s);
    only_render.event = nullptr;
    check(rolltui_run(0, 1, RolltuiTerminalOptions{}, &only_render) == ROLLTUI_RUN_BAD_APP && s.starts == 0,
          "an app with no `event` is refused, and none of it was called");
    // a descriptor that is no terminal: said, and nothing entered
    const int devnull = ::open("/dev/null", O_RDWR);
    RolltuiTerminalOptions o;
    o.no_cache = 1;
    o.handle_signals = 0;
    RolltuiRunApp app = make_app(&s);
    check(rolltui_run(devnull, devnull, o, &app) == ROLLTUI_RUN_NOT_A_TERMINAL, "a descriptor that is no terminal is named as such");
    check(s.starts == 0 && s.renders == 0, "…and nothing of the app was called");
    ::close(devnull);
    check(live_bytes() == base, "…and nothing is left allocated by either");
    // the functions the callbacks call are harmless on nothing
    rolltui_run_stop(nullptr);
    rolltui_run_invalidate(nullptr);
    rolltui_run_suspend(nullptr);
    rolltui_run_resume(nullptr);
    check(rolltui_run_terminal(nullptr) == nullptr, "the run's functions are no-ops on NULL");
  }

  // ---- a real run on a pty ----------------------------------------------------------------------------------------
  int master = -1, slave = -1;
  winsize ws{};
  ws.ws_row = 24;
  ws.ws_col = 80;
  check(::openpty(&master, &slave, nullptr, nullptr, &ws) == 0, "openpty");
  if (master < 0) return report("rolltui_run_test");
  int rep[2] = {-1, -1};
  check(::pipe(rep) == 0, "pipe");
  const pid_t child = ::fork();
  if (child == 0) {
    ::close(slave);
    ::close(rep[0]);
    child_terminal(master, rep[1]);
    ::close(rep[1]);
    ::_exit(0);
  }
  ::close(master);
  ::close(rep[1]);

  const std::size_t base = live_bytes();
  State s;
  s.ds = rolltui_draw_scratch_new();
  RolltuiTerminalOptions o;
  o.no_cache = 1;
  o.handle_signals = 0;  // this process keeps its own signal dispositions
  RolltuiRunApp app = make_app(&s);
  const int rc = rolltui_run(slave, slave, o, &app);
  rolltui_draw_scratch_free(s.ds);
  check(rc == ROLLTUI_RUN_STOPPED, "the loop returns ROLLTUI_RUN_STOPPED when the app stops it");
  check(live_bytes() == base, "…and nothing is left allocated (" + std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  termios tio{};
  check(::tcgetattr(slave, &tio) == 0 && (tio.c_lflag & ICANON) && (tio.c_lflag & ECHO), "…and the tty is cooked again");
  ::close(slave);

  std::string said;
  char buf[1024];
  ssize_t k;
  while ((k = ::read(rep[0], buf, sizeof buf)) > 0) said.append(buf, static_cast<std::size_t>(k));
  ::close(rep[0]);
  int status = 0;
  ::waitpid(child, &status, 0);
  {
    std::size_t at = 0;
    int lines = 0;
    while (at < said.size()) {
      const std::size_t nl = said.find('\n', at);
      const std::string line = said.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      at = nl == std::string::npos ? said.size() : nl + 1;
      if (line.rfind("PASS ", 0) == 0) check(true, line.substr(5));
      else if (line.rfind("FAIL ", 0) == 0) check(false, line.substr(5));
      ++lines;
    }
    check(lines == 8, "the terminal's side ran every step (" + std::to_string(lines) + " of 8)");
  }

  // what the app saw
  check(s.starts == 1 && s.start_had_terminal && s.start_w == 80 && s.start_h == 24,
        "start is called once, with the terminal, before any frame, at the terminal's size");
  check(s.grounds >= s.renders, "the ground is asked before every frame");
  check(s.renders > 4, "frames were drawn (" + std::to_string(s.renders) + ")");
  check(s.settles >= s.renders - 1 && s.settles > 4, "settle is called once per wake, including the wakes that brought no event (" + std::to_string(s.settles) + ")");
  check(s.typed == "abrsq", "the keys arrived in the order they were typed: " + s.typed);
  check(s.pasted == "hello-paste" && s.text_seen_valid, "a paste's text was valid when the app was called");
  check(s.resizes == 1 && s.resized_w == 100 && s.resized_h == 30, "resized is called with the new size, once");
  check(s.render_w == 100 && s.render_h == 30, "and the next frame was drawn at it");

  return report("rolltui_run_test");
}
