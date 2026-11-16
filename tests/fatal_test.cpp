//
// fatal_test.cpp — A STOP PUTS THE TERMINAL BACK FIRST: the library's own fatal errors, an abort, and a crash.
//
// Each case runs a child on a pty that makes a terminal (the alternate screen, raw mode) and then stops. What the pty receives
// shows the order: the bytes that leave the alternate screen must come before the message, or the message is drawn on the
// screen that leaving discards and the person is left in a raw terminal with nothing said.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <cstdlib>
#include <cstring>
#include <string>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_os.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

using namespace testkit;

namespace {

void prev_abort_handler(int) {
  const char m[] = "PREV_HANDLER_RAN";
  (void)!::write(2, m, sizeof m - 1);
  ::_exit(42);
}

struct Ended {
  std::string bytes;
  int status = 0;
  bool exited = false;
};

// The child makes a terminal and then does `how`.
Ended run_child(const char* how) {
  winsize ws{};
  ws.ws_row = 24;
  ws.ws_col = 80;
  int master = -1;
  const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
  if (pid == 0) {
    ::setenv("ROLL_TERM_PROBE", "0", 1);
    if (std::strcmp(how, "chained") == 0) ::signal(SIGABRT, prev_abort_handler);  // a host's own, before the terminal
    RolltuiTerminalOptions opts{};
    opts.no_cache = 1;
    RolltuiTerminal* t = rolltui_terminal_new(0, 1, opts);
    (void)t;
    if (std::strcmp(how, "fatal") == 0) rolltui_fatal("stopped on purpose, code %d", 7);
    if (std::strcmp(how, "abort") == 0 || std::strcmp(how, "chained") == 0) std::abort();
    if (std::strcmp(how, "segv") == 0) ::raise(SIGSEGV);
    ::_exit(0);
  }
  Ended e;
  e.exited = rolltui_test::end_pty_child(master, pid, 5000, &e.status, &e.bytes);
  return e;
}

bool before(const std::string& s, const char* first, const char* then) {
  const std::size_t a = s.find(first);
  const std::size_t b = s.find(then);
  return a != std::string::npos && b != std::string::npos && a < b;
}

}  // namespace

int main() {
  const char* kLeave = "\x1b[?1049l";

  const Ended fatal = run_child("fatal");
  check(fatal.exited && WIFSIGNALED(fatal.status) && WTERMSIG(fatal.status) == SIGABRT, "a fatal error ends the process by SIGABRT");
  check(before(fatal.bytes, kLeave, "rolltui: stopped on purpose, code 7\r\n"),
        "…and the terminal is left before its message is printed, so the message stays on the screen");

  const Ended ab = run_child("abort");
  check(ab.exited && WIFSIGNALED(ab.status) && WTERMSIG(ab.status) == SIGABRT && ab.bytes.find(kLeave) != std::string::npos,
        "an abort puts the terminal back, and the process still ends by SIGABRT");

  const Ended chained = run_child("chained");
  check(chained.exited && WIFEXITED(chained.status) && WEXITSTATUS(chained.status) == 42 &&
            before(chained.bytes, kLeave, "PREV_HANDLER_RAN"),
        "a host's own abort handler, installed before the terminal, still runs, after the terminal is put back");

  const Ended segv = run_child("segv");
  const bool by_segv = WIFSIGNALED(segv.status) && WTERMSIG(segv.status) == SIGSEGV;
#ifdef ROLLTUI_TEST_INSTRUMENTED
  // the sanitizer reports the crash and ends the process its own way: an abort on macOS, exit 1 on Linux
  const bool ended = by_segv || WIFSIGNALED(segv.status) || (WIFEXITED(segv.status) && WEXITSTATUS(segv.status) != 0);
#else
  const bool ended = by_segv;
#endif
  const std::string how = WIFSIGNALED(segv.status) ? "signal " + std::to_string(WTERMSIG(segv.status))
                                                  : "exit " + std::to_string(WEXITSTATUS(segv.status));
  check(segv.exited && ended && segv.bytes.find(kLeave) != std::string::npos,
        "a crash puts the terminal back, and the process still ends as the crash ends it [" + how + "]");

  return report("fatal_test");
}
