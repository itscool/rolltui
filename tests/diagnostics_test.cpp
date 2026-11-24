// diagnostics_test.cpp — WHAT A PROGRAM SAYS FOR WHOEVER IS LOOKING: the log, a file because the terminal is the screen's, and the
// draw times on screen, each window's in its top border.
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace testkit;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

std::string read_all(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> out;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t nl = text.find('\n', at);
    out.push_back(text.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  return out;
}

void test_log(const std::string& dir) {
  ::unsetenv("ROLLTUI_LOG");
  ::unsetenv("ROLLTUI_LOG_LEVEL");
  check(rolltui_log_wants(ROLLTUI_LOG_ERROR) == 0, "with no file named, nothing is written: not even an error");
  rolltui_log(ROLLTUI_LOG_ERROR, "nowhere", 7); /* and saying something is harmless */

  const std::string path = dir + "/a.log";
  check(rolltui_log_to(path.data(), path.size()) == 1 && rolltui_log_wants(ROLLTUI_LOG_INFO) == 1 &&
            rolltui_log_wants(ROLLTUI_LOG_DEBUG) == 0,
        "a file named: INFO and above go to it by default, DEBUG does not");
  rolltui_logf(ROLLTUI_LOG_INFO, "hello %d", 7);
  rolltui_logf(ROLLTUI_LOG_DEBUG, "not yet");
  rolltui_log(ROLLTUI_LOG_WARN, "two\nlines", 9);
  rolltui_log_set_level(ROLLTUI_LOG_DEBUG);
  rolltui_logf(ROLLTUI_LOG_DEBUG, "now");
  rolltui_log_set_level(ROLLTUI_LOG_INFO);
  std::vector<std::string> l = lines_of(read_all(path));
  check(l.size() == 3, "three lines: the DEBUG one before the level let it through is not there [" + std::to_string(l.size()) + "]");
  if (l.size() == 3) {
    /* "2026-11-24 09:41:07    0.000s INFO  hello 7" */
    check(l[0].size() > 30 && l[0][4] == '-' && l[0][13] == ':' && l[0].find("s INFO  hello 7") != std::string::npos,
          "a line is the local time, the seconds since the log began, the level and the text [" + l[0] + "]");
    check(l[1].find("WARN  two lines") != std::string::npos, "…a newline in the text is a space: one line a call [" + l[1] + "]");
    check(l[2].find("DEBUG now") != std::string::npos, "…and the level set lets DEBUG through [" + l[2] + "]");
  }

  // WORKERS: lines from two threads at once never land inside one another
  const std::string tpath = dir + "/threads.log";
  rolltui_log_to(tpath.data(), tpath.size());
  auto writer = [](char who) {
    for (int i = 0; i < 300; ++i) rolltui_logf(ROLLTUI_LOG_INFO, "%c says line %03d of three hundred, whole", who, i);
  };
  std::thread a(writer, 'a'), b(writer, 'b');
  a.join();
  b.join();
  l = lines_of(read_all(tpath));
  int whole = 0;
  for (const std::string& s : l) whole += s.size() > 40 && s.compare(s.size() - 5, 5, "whole") == 0;
  check(l.size() == 600 && whole == 600, "two workers' 600 lines, every one of them whole [" + std::to_string(whole) + " of " + std::to_string(l.size()) + "]");

  // THE ENVIRONMENT names the file where the host names none
  const std::string epath = dir + "/env.log";
  ::setenv("ROLLTUI_LOG", epath.c_str(), 1);
  rolltui_log_to(nullptr, 0);
  rolltui_log(ROLLTUI_LOG_INFO, "from the environment", 20);
  check(read_all(epath).find("from the environment") != std::string::npos, "ROLLTUI_LOG names the file where the host names none");

  // THE LIBRARY SAYS WHAT TERMINAL IT ENTERED
  const int fd = ::open("/dev/null", O_RDWR);
  RolltuiTerminalOptions o{};
  o.no_probe = 1;
  o.no_cache = 1;
  o.handle_signals = 0;
  RolltuiTerminal* t = rolltui_terminal_new(fd, fd, o);
  rolltui_terminal_free(t);
  ::close(fd);
  const std::string said = read_all(epath);
  check(said.find("INFO  terminal: ") != std::string::npos && said.find("not a terminal") != std::string::npos,
        "the library says what terminal it entered, and that this one is not one [" + said.substr(said.find("terminal:") == std::string::npos ? 0 : said.find("terminal:")) + "]");
  ::unsetenv("ROLLTUI_LOG");
  rolltui_log_to(nullptr, 0);
}

// THE DRAW TIMES ON SCREEN: each window's top border carries how long its widget took
void test_timing() {
  constexpr std::string_view screen = R"({ "name": "t", "root": { "row": [
    { "id": "left", "content": "text:one", "border": "rounded", "title": "left" },
    { "id": "right", "content": "text:two", "border": "rounded", "title": "right" } ] } })";
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
  RolltuiLayout* layout = rolltui_load_layout_text(screen.data(), screen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(ctx());
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
  auto paint = [&]() {
    RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
    const RolltuiRect area{0, 0, 60, 6};
    rolltui_windows_sync(w, stack);
    rolltui_windows_autosize(w, stack, area);
    rolltui_windows_layout(w, stack, area);
    RolltuiFrame* f = rolltui_frame_new(60, 6, styles[0]);
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    struct Slot {
      RolltuiWindows* w;
      RolltuiStyle* styles;
    } slot{w, styles};
    rolltui_window_stack_compose(
        stack, f, area, styles, rolltui_layout_default_roles(),
        [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* s = static_cast<Slot*>(c);
          rolltui_windows_draw(s->w, rn, fr, s->styles, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    const std::string all = str_of(text);
    rolltui_str_free(&text);
    rolltui_compose_scratch_free(cs);
    rolltui_frame_free(f);
    return all.substr(0, all.find('\n'));
  };
  std::string top = paint();
  check(top.find(" ms ") == std::string::npos, "off by default: the borders are the layout's [" + top + "]");
  rolltui_windows_set_timing(w, 1);
  top = paint();
  const std::size_t first = top.find(" ms "), second = first == std::string::npos ? first : top.find(" ms ", first + 1);
  check(first != std::string::npos && second != std::string::npos && top.find("left") != std::string::npos,
        "on: each window's top border carries its draw time, its title still there [" + top + "]");
  rolltui_windows_free(w);
  ::setenv("ROLLTUI_TIMING", "1", 1);
  w = rolltui_windows_new(ctx());
  top = paint();
  check(top.find(" ms ") != std::string::npos, "ROLLTUI_TIMING=1 turns it on for any program, without a change to it");
  ::unsetenv("ROLLTUI_TIMING");
  rolltui_windows_free(w);
  rolltui_window_stack_free(stack);
  rolltui_layout_free(layout);
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  char dir[] = "/tmp/rolltui_diagnostics_XXXXXX";
  if (!mkdtemp(dir)) return 1;
  test_log(dir);
  test_timing();
  for (const char* f : {"a.log", "threads.log", "env.log"}) std::remove((std::string(dir) + "/" + f).c_str());
  rmdir(dir);
  return report("diagnostics_test");
}
