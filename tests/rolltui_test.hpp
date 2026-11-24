#pragma once
//
// rolltui_test.hpp — the library's side of the shared test module.
//
// THE HARNESS IS NOT HERE ANY MORE. `check`, `check_quiet`, `report`, the assertion count and
// the report line live in `testkit/testkit.hpp`, which roll's suites include too. This file
// used to be a near-copy of roll's, and the two copies DISAGREED about what a passing test
// means: a suite here that ran zero assertions printed `0 passed, 0 failed — ALL PASS` and
// exited 0, and its report line carried no count the `check_counts` guard could parse, so 44
// binaries were invisible to it. Both are gone by having one definition rather than two.
//
// TESTKIT IS NOT ROLL. It is a leaf at the repository root that includes nothing of either
// side, so `rolltui` still builds and passes with roll's `include/` and `src/` deleted —
// asserted by `rolltui-boundary-test`, which was written before any of this moved and which
// scans this file among the rest.
//
// WHAT IS LEFT HERE is what only the library's own suites need: a session to register kinds
// into, and the std:: bridge over the library's C shapes. The bridge cannot move into the
// module: it is about `RolltuiStr`, so a module holding it would depend on rolltui, and rolltui
// is a CONSUMER of the module.
//
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/rolltui_cpp.h"
#include "testkit/testkit.hpp"

#if !defined(_WIN32)
#include <cerrno>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rolltui_test {

// A WALL-CLOCK BOUND IS SET FOR THE RELEASE BUILD. Under AddressSanitizer every memory access is checked, and the same run takes
// seven to twelve times as long, the most when the suite runs in parallel — so a bound set for one build is noise in the other,
// and the instrumented build failed checks the code passed. Every upper bound on elapsed time in a suite is written
// `bound * kClockScale`: tight in Release, ten times looser under the sanitizer, where it still catches a runaway, which is what
// it is for there. A LOWER bound (it waited at least this long) needs nothing: instrumentation only makes it easier to meet.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ROLLTUI_TEST_INSTRUMENTED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(ROLLTUI_TEST_INSTRUMENTED)
#define ROLLTUI_TEST_INSTRUMENTED 1
#endif
#ifdef ROLLTUI_TEST_INSTRUMENTED
inline constexpr double kClockScale = 10.0;
#else
inline constexpr double kClockScale = 1.0;
#endif

#if !defined(_WIN32)
// ENDING A PROGRAM RUN ON A PTY. A process that exits with output still unread on its terminal waits inside its own exit for the
// output to drain, until the master side reads it or is closed, and no signal ends that wait: the process is already exiting. A
// suite that waits for the child before closing the master hangs whenever the child drew a frame at the last moment. This reads
// the master while the child has `grace_ms` to leave by itself (what it writes is appended to `out`, when given), kills it after
// that, closes the master, and only then waits. Returns true when the child left by itself. The grace is an upper bound on elapsed
// time, so it is scaled here (`kClockScale`): a sanitizer's crash report, symbolized on a loaded machine, takes seconds.
inline bool end_pty_child(int master, pid_t pid, int grace_ms, int* status_out = nullptr, std::string* out = nullptr) {
  int status = 0;
  bool exited = false;
  const std::uint64_t end = testkit::mono_ms() + static_cast<std::uint64_t>(grace_ms > 0 ? grace_ms * kClockScale : 0);
  for (;;) {
    if (::waitpid(pid, &status, WNOHANG) == pid) {
      exited = true;
      break;
    }
    if (testkit::mono_ms() >= end) break;
    pollfd p{master, POLLIN, 0};
    if (::poll(&p, 1, 20) > 0 && (p.revents & (POLLIN | POLLHUP))) {
      char b[8192];
      const ssize_t k = ::read(master, b, sizeof b);
      if (k > 0 && out) out->append(b, static_cast<std::size_t>(k));
      if (k <= 0) ::usleep(5 * 1000);  // the slave is gone: the child is on its way out
    }
  }
  if (!exited) ::kill(pid, SIGKILL);
  ::close(master);
  if (!exited)
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  if (status_out) *status_out = status;
  return exited;
}
#endif


// ONE SESSION FOR A SUITE. A registry is a CONTEXT's now, so a suite that
// registers a widget or effect kind needs a session to register it INTO. Two suites had
// written the same wrapper, which is rule 5 firing, so it lives here once.
//
// It is NOT what a host does, and the difference is the point: a host owns its context and
// frees it where the session ends. A suite has no such place, so the holder frees it at
// static destruction — after the last check has run, which is why no assertion can see it.
// A suite that wants to PROVE a context releases what it holds builds its own and frees it
// itself, in sequence, the way `c_consumer_test` does.
inline RolltuiContext* test_context() {
  struct Holder {
    RolltuiContext* c = rolltui_context_new();
    Holder() = default;
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    ~Holder() { rolltui_context_free(c); }
  };
  static Holder h;
  return h.c;
}

}  // namespace rolltui_test

// ---- the tests' own bridge to std:: ----------------------------------------------
// The library's C++ shape names no std:: container or view; what a test wants as a std::string
// or string_view it makes HERE, in its own harness, which is what "in your own file" means.
#ifndef ROLLTUI_STD_BRIDGE_DEFINED
#define ROLLTUI_STD_BRIDGE_DEFINED
// THE TERMINAL A RUN SAYS IT IS: every variable the library reads about a terminal is cleared, then
// `vars` set. A golden frame records a colour depth and a glyph width, so a suite that draws one states
// the terminal it was recorded in, and the person's own (Apple Terminal, a CI runner with no TERM)
// never reaches a child it starts.
inline void set_terminal_env(std::initializer_list<std::pair<const char*, const char*>> vars) {
  // EVERY VARIABLE THE LIBRARY READS ABOUT A TERMINAL, so a suite run inside kitty, Zellij or Windows Terminal tests the terminal it
  // scripts and not the one it was started in
  for (const char* name : {"COLORTERM", "TERM", "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "LC_TERMINAL", "LC_TERMINAL_VERSION",
                           "COLORFGBG", "ROLL_COLOR_DEPTH", "ROLL_AMBIGUOUS_WIDE", "ROLL_TERM_PROBE", "TMUX", "TMUX_PANE", "STY",
                           "SSH_CONNECTION", "KITTY_WINDOW_ID", "KONSOLE_VERSION", "WT_SESSION", "VTE_VERSION", "ZELLIJ", "NVIM",
                           "ROLLTUI_IMAGES", "ROLLTUI_CONSOLE", "LC_ALL", "LC_CTYPE"})
    ::unsetenv(name);
  ::setenv("LANG", "en_US.UTF-8", 1); /* the encoding is the terminal's too: UTF-8, as a terminal a person uses speaks, unless `vars` says */
  for (const auto& v : vars) ::setenv(v.first, v.second, 1);
}

inline std::string str_of(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }
inline std::string_view view_of(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }
inline void set_str(RolltuiStr& s, std::string_view v) { rolltui_str_set(&s, v.data(), v.size()); }
#endif  /* ROLLTUI_STD_BRIDGE_DEFINED */
// The tree shapes the tests compose in a std::vector of their own, then hand to a rolltui item — the consumer's container, the
// library's items. The same three `tools/tool_str.hpp` defines, under the same guard, so a suite that includes both takes one.
#ifndef ROLLTUI_TREE_BRIDGE_DEFINED
#define ROLLTUI_TREE_BRIDGE_DEFINED
inline RolltuiMenuItem submenu_of(const char* id, const char* label, std::vector<RolltuiMenuItem>&& children) {
  RolltuiMenuItem it = RolltuiMenuItem::submenu(id, label);
  for (RolltuiMenuItem& c : children) it.children.push_back(std::move(c));
  return it;
}
inline RolltuiMenuItem choice_of(const char* id, const char* label, std::vector<RolltuiMenuItem>&& options, const char* value) {
  RolltuiMenuItem it = RolltuiMenuItem::choice(id, label, value);
  for (RolltuiMenuItem& c : options) it.children.push_back(std::move(c));
  return it;
}
inline std::vector<RolltuiMenuItem> clone_items(const std::vector<RolltuiMenuItem>& v) {
  std::vector<RolltuiMenuItem> out;
  out.reserve(v.size());
  for (const RolltuiMenuItem& c : v) out.push_back(c.clone());
  return out;
}
#endif  /* ROLLTUI_TREE_BRIDGE_DEFINED */
// A message a test builds from rolltui's own strings (`"[" + ed.status_line() + "]"` is a RolltuiStr) is a message all the same. The literal
// overload is the exact match that keeps `check(ok, "words")` from being ambiguous between the three.
inline void check(bool cond, const char* name) { testkit::check(cond, std::string(name)); }
inline void check(bool cond, const RolltuiStr& name) { testkit::check(cond, std::string(name.data(), name.size())); }
inline std::string_view kind_of(const RolltuiEffectSpec& s) { return std::string_view(s.kind, s.kind_len); }
inline std::string_view frame_of(const RolltuiEffectSpec& s, std::size_t i) {
  return i < s.frame_count ? std::string_view(s.frames[i].bytes, s.frames[i].len) : std::string_view();
}
