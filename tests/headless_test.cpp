// headless_test.cpp — A PROGRAM'S REAL LOOP WITH NO TERMINAL, as a host's own end-to-end test drives it: keys, a resize and focus
// injected, the screen read back as text, and the run recorded as an asciicast file.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace testkit;

namespace {

struct App {
  int count = 0, w = 0, h = 0, resizes = 0, focused = 1;
  RolltuiDrawScratch* draw = nullptr;
};

int render(void* c, RolltuiRun*, RolltuiFrame* f, int w, int h, unsigned long long) {
  App* a = static_cast<App*>(c);
  char line[64];
  a->w = w, a->h = h;
  const int n = std::snprintf(line, sizeof line, "count %d at %dx%d %s", a->count, w, h, a->focused ? "focused" : "away");
  rolltui_frame_put_text(f, a->draw, 0, 0, line, static_cast<std::size_t>(n), RolltuiStyle{}, w, 0, 0);
  return -1;
}
void event(void* c, RolltuiRun* run, const RolltuiEvent* e) {
  App* a = static_cast<App*>(c);
  if (e->kind == ROLLTUI_EVENT_KEY && e->key.key == ROLLTUI_KEY_CHAR && e->key.ch == 'a') ++a->count;
  if (e->kind == ROLLTUI_EVENT_KEY && e->key.key == ROLLTUI_KEY_CHAR && e->key.ch == 'q') rolltui_run_stop(run);
  if (e->kind == ROLLTUI_EVENT_MOUSE && e->mouse.kind == RolltuiMouseEvent::Kind::Press) a->count += 10;
}
void resized(void* c, RolltuiRun*, int, int) { ++static_cast<App*>(c)->resizes; }
void focus(void* c, RolltuiRun*, int on) { static_cast<App*>(c)->focused = on; }

RolltuiEvent key(char32_t ch) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = ch;
  return e;
}
std::string screen(RolltuiRun* run) {
  RolltuiStr s{};
  rolltui_run_screen_text(run, &s);
  const std::string out = str_of(s);
  rolltui_str_free(&s);
  return out.substr(0, out.find('\n'));
}

}  // namespace

int main() {
  App a;
  a.draw = rolltui_draw_scratch_new();
  RolltuiRunApp app{};
  app.ctx = &a;
  app.render = render;
  app.event = event;
  app.resized = resized;
  app.focus = focus;
  int result = -1;
  char dir[] = "/tmp/rolltui_headless_XXXXXX";
  if (!mkdtemp(dir)) return 1;
  const std::string cast = std::string(dir) + "/run.cast";
  ::setenv("ROLLTUI_RECORD", cast.c_str(), 1); /* recorded from before it enters, as any program is */
  RolltuiRun* run = rolltui_run_begin_headless(40, 6, &app, &result);
  ::unsetenv("ROLLTUI_RECORD");
  check(run != nullptr && result == ROLLTUI_RUN_STOPPED, "a run with no terminal begins, at the size asked");
  if (!run) return report("headless_test");

  rolltui_run_step(run);
  check(screen(run).rfind("count 0 at 40x6 focused", 0) == 0, "its first frame is on its screen [" + screen(run) + "]");
  for (int i = 0; i < 3; ++i) {
    const RolltuiEvent e = key('a');
    rolltui_run_inject(run, &e);
  }
  rolltui_run_step(run);
  check(screen(run).rfind("count 3", 0) == 0, "keys injected reach the app's event, and the next step draws what they did [" + screen(run) + "]");
  RolltuiEvent press{};
  press.kind = ROLLTUI_EVENT_MOUSE;
  press.mouse.button = 1;
  rolltui_run_inject(run, &press);
  rolltui_run_step(run);
  check(screen(run).rfind("count 13", 0) == 0, "…and a press [" + screen(run) + "]");
  rolltui_run_resize(run, 50, 8);
  rolltui_run_step(run);
  check(a.resizes == 1 && screen(run).rfind("count 13 at 50x8", 0) == 0, "a resize is reported as a window's is, and drawn at [" + screen(run) + "]");
  RolltuiEvent away{};
  away.kind = ROLLTUI_EVENT_FOCUS;
  away.focused = 0;
  rolltui_run_inject(run, &away);
  rolltui_run_step(run);
  check(a.focused == 0 && screen(run).find("away") != std::string::npos, "focus injected is the app's `focus`, as the terminal's is");
  { /* A HOST RECORDS FROM WHERE IT IS: a second recording takes the place of the first (which ends here, the checks below being on
     * what it held by then), and a repaint makes its first frame whole */
    const std::string again = std::string(dir) + "/again.cast";
    RolltuiTerminal* t = rolltui_run_terminal(run);
    check(rolltui_terminal_record(t, again.data(), again.size()) == 1, "a host starts a recording of its own");
    const RolltuiEvent e = key('a');
    rolltui_run_inject(run, &e);
    rolltui_run_invalidate(run);
    rolltui_run_step(run);
    check(rolltui_terminal_record(t, nullptr, 0) == 0, "…and stops it");
    std::ifstream g(again);
    const std::string text((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
    check(text.find("\"width\": 50") != std::string::npos && text.find("count 14") != std::string::npos,
          "…its file at the run's size, holding the frame between [" + std::to_string(text.size()) + " bytes]");
    std::remove(again.c_str());
  }
  const RolltuiEvent q = key('q');
  rolltui_run_inject(run, &q);
  check(rolltui_run_step(run) == ROLLTUI_RUN_STEP_STOPPED, "the app stopping ends the steps");
  check(rolltui_run_finish(run) == ROLLTUI_RUN_STOPPED, "…and the run finishes");

  // THE RECORDING: a header, then [time, "o", bytes] and [time, "r", "WxH"], every line JSON
  std::ifstream in(cast);
  std::string line, out;
  int lines = 0, json = 0, resizes = 0;
  bool header = false;
  while (std::getline(in, line)) {
    ++lines;
    RolltuiStr err{};
    RolltuiJsonValue* v = rolltui_json_parse(line.data(), line.size(), &err);
    json += v != nullptr;
    if (lines == 1) header = line.find("\"version\": 2") != std::string::npos && line.find("\"width\": 40") != std::string::npos;
    if (line.find("\"r\", \"50x8\"") != std::string::npos) ++resizes;
    if (v && rolltui_json_array_size(v) == 3) {
      std::size_t n = 0;
      const char* s = rolltui_json_as_string(rolltui_json_array_at(v, 2), "", 0, &n);
      if (line.find("\"o\"") != std::string::npos) out.append(s, n);
    }
    rolltui_json_free(v);
    rolltui_str_free(&err);
  }
  check(header && lines > 3 && json == lines, "the recording is an asciicast: a header and a line of JSON an event [" + std::to_string(json) + " of " +
                                                 std::to_string(lines) + "]");
  check(out.find("count 13 at 50x8") != std::string::npos && out.find("\x1b[?1049h") != std::string::npos && resizes == 1,
        "…holding what was sent, entering the screen included, and the resize");
  std::remove(cast.c_str());
  rmdir(dir);
  rolltui_draw_scratch_free(a.draw);
  return report("headless_test");
}
