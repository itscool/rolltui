// rolltui/tests/file_watch_test.cpp — FILES AND FOLDERS WATCHED: a write, a save that renames over, a file arriving, a file removed,
// a folder's entry arriving, each told once and the loop woken; a path let go of told no more.
#include <sys/stat.h>
#include <unistd.h>

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
#include "rolltui_test.hpp"

namespace {

using testkit::check;

std::atomic<int> g_wakes{0};
void on_wake(void*) { ++g_wakes; }

void write_file(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

/* the ids told within the time allowed; it waits for the first */
std::vector<unsigned int> told(RolltuiWatch* w, int ms = 3000) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(ms * rolltui_test::kClockScale));
  std::vector<unsigned int> ids(16);
  for (;;) {
    const std::size_t n = rolltui_watch_changed(w, ids.data(), ids.size());
    if (n || std::chrono::steady_clock::now() >= until) {
      ids.resize(n);
      return ids;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

bool has(const std::vector<unsigned int>& v, unsigned int id) {
  for (unsigned int x : v)
    if (x == id) return true;
  return false;
}

}  // namespace

int main() {
  char tmpl[] = "/tmp/rolltui_fwatch_XXXXXX";
  const std::string dir = mkdtemp(tmpl);
  const std::string a = dir + "/a.json", b = dir + "/b.json", later = dir + "/later.json", sub = dir + "/sub";
  write_file(a, "{\"x\": 0}");
  write_file(b, "{}");
  mkdir(sub.c_str(), 0700);
  RolltuiWatch* w = rolltui_watch_new(on_wake, nullptr);
  const std::string how = rolltui_watch_how(w);
#if defined(__APPLE__)
  check(how == "kqueue", "on macOS the system says when to look: kqueue [" + how + "]");
#elif defined(__linux__)
  check(how == "inotify", "on Linux the system says when to look: inotify [" + how + "]");
#endif
  const unsigned int ia = rolltui_watch_add(w, a.data(), a.size());
  const unsigned int ib = rolltui_watch_add(w, b.data(), b.size());
  const unsigned int il = rolltui_watch_add(w, later.data(), later.size());
  const unsigned int is = rolltui_watch_add(w, sub.data(), sub.size());
  check(ia && ib && il && is && ia != ib, "each path watched has an id of its own");
  std::this_thread::sleep_for(std::chrono::milliseconds(50)); /* the thread arms its handles */
  check(told(w, 700).empty(), "nothing is told before anything changes");

  // A WRITE IN PLACE
  g_wakes = 0;
  const auto t0 = std::chrono::steady_clock::now();
  { /* overwritten in place, the same length, in one write: one change (a truncation first would be a change of its own) */
    std::FILE* f = std::fopen(a.c_str(), "r+");
    std::fputs("{\"x\": 1}", f);
    std::fclose(f);
  }
  std::vector<unsigned int> got = told(w);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  check(got.size() == 1 && got[0] == ia && g_wakes.load() > 0, "a file written is told, once, and the loop woken [" + std::to_string(ms) + " ms]");
  check(told(w, 700).empty(), "…and told no more until it changes again");

  // A SAVE THAT WRITES A NEW FILE AND RENAMES IT OVER THE OLD ONE
  write_file(b + ".tmp", "{\"saved\": true}");
  std::rename((b + ".tmp").c_str(), b.c_str());
  got = told(w);
  check(has(got, ib) && !has(got, ia), "a save that renames a new file over the old one is told for that path");
  write_file(b, "{\"saved\": 2}"); /* the file it now is is the one watched */
  check(has(told(w), ib), "…and a write to the file it became is told too");

  // A FILE ARRIVING, AND ONE REMOVED
  write_file(later, "{}");
  check(has(told(w), il), "a path that did not exist is told when it arrives");
  std::remove(a.c_str());
  check(has(told(w), ia), "a file removed is told");

  // A FOLDER'S ENTRY ARRIVING
  write_file(sub + "/new.txt", "x");
  check(has(told(w), is), "a folder is told when an entry arrives in it");

  // LET GO OF
  rolltui_watch_remove(w, ib);
  write_file(b, "{\"after\": 1}");
  check(!has(told(w, 700), ib), "a path let go of is told no more");

  // MORE CHANGES THAN ROOM: the rest wait
  write_file(later, "{\"x\": 2}");
  write_file(a, "{}");
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1500 * rolltui_test::kClockScale)));
  unsigned int one[1] = {0};
  const std::size_t first = rolltui_watch_changed(w, one, 1);
  const std::vector<unsigned int> rest = told(w, 700);
  check(first == 1 && rest.size() == 1 && rest[0] != one[0], "past the room given, the rest are told at the next call");

  rolltui_watch_free(w); /* with its thread running */
  check(true, "a watch is freed with its thread running");
  std::remove(a.c_str());
  std::remove(b.c_str());
  std::remove(later.c_str());
  std::remove((sub + "/new.txt").c_str());
  rmdir(sub.c_str());
  rmdir(dir.c_str());
  return testkit::report("file_watch_test");
}
