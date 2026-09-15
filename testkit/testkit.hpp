#pragma once
//
// testkit.hpp — the C++ binding over testkit's C core, plus the three things only C++ can
// offer: self-registration, two-sided controls, and a private temp root.
//
// DO include this from any suite on either side of the repository. DON'T add anything here
// that names a roll or a rolltui type: testkit is the shared LEAF, and the moment it depends
// on a consumer it stops being one. Side-specific helpers live in the side's own header
// (tests/test_util.hpp, rolltui/tests/rolltui_test.hpp), which include this and add to it.
//
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <cerrno>
#include <cstring>

#include <crt_externs.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#include "testkit/testctl.h"
#include "testkit/testkit.h"

namespace testkit {

inline void check(bool cond, const std::string& name) { testkit_check(cond ? 1 : 0, name.c_str()); }
inline void check_quiet(bool cond, const std::string& name) {
  testkit_check_quiet(cond ? 1 : 0, name.c_str());
}
inline void fail_unprinted() { testkit_fail_unprinted(); }
inline bool any_failed() { return testkit_any_failed() != 0; }
inline std::string report_line(const char* suite, int fails, int checks) {
  char buf[512];
  const size_t n = testkit_report_line(buf, sizeof buf, suite, fails, checks);
  return std::string(buf, n < sizeof buf ? n : sizeof buf - 1);
}

// --- self-registering tests -----------------------------------------------------
//
// A test defined and never called is a test that does not run, and a hand-written list of
// calls in `main()` makes that expressible: forgetting one is a silent no-op that still
// compiles and still goes green. Measured on this repository's own model arms — a correct test
// function with no `main()` line, in 19 of 20 first proposals for one model and 5 of 17 for the
// seat — and two DETECTORS were built before anyone reached the answer every mainstream
// framework already has. pytest discovers by name; GoogleTest and Catch2 self-register through
// a static initializer; Rust's `#[test]` and Go's `TestXxx` are collected by the toolchain.
// None of them can express the failure. This is ten lines of the same idea, with no framework
// and no dependency — a compensation converted into an invariant.
//
// Existing suites keep their explicit `main()` calls and are unaffected; TESTKIT_TEST is for
// new ones. `report()` runs anything registered before it counts, so a suite can mix both.
inline std::vector<std::pair<std::string, void (*)()>>& registered_tests() {
  static std::vector<std::pair<std::string, void (*)()>> v;
  return v;
}
struct Registrar {
  Registrar(const char* name, void (*fn)()) { registered_tests().emplace_back(name, fn); }
};

inline int report(const char* suite) {
  // Run anything that registered itself, before counting. A suite that uses only TESTKIT_TEST
  // needs no main() body at all.
  for (const auto& [name, fn] : registered_tests()) {
    (void)name;
    fn();
  }
  return testkit_report(suite);
}

inline uint64_t mono_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

#ifdef TESTKIT_CONTROL
namespace ctl {

inline bool on(const char* name) { return testkit_ctl_on(name) != 0; }
inline void set(const char* name, bool value) { testkit_ctl_set(name, value ? 1 : 0); }
inline void reset() { testkit_ctl_reset(); }

inline std::vector<std::string> names(size_t (*fn)(const char**, size_t)) {
  const size_t n = fn(nullptr, 0);
  std::vector<const char*> raw(n ? n : 1);
  const size_t got = fn(raw.data(), raw.size());
  std::vector<std::string> out;
  for (size_t i = 0; i < got && i < raw.size(); ++i) out.emplace_back(raw[i]);
  return out;
}
inline std::vector<std::string> known() { return names(&testkit_ctl_known); }
inline std::vector<std::string> flipped() { return names(&testkit_ctl_flipped); }
inline int set_from_env(const char* var = "ROLL_TEST_CONTROLS") {
  return testkit_ctl_set_from_env(var);
}

// Flip one control on for a block; restore on exit, however the block leaves.
struct Scoped {
  explicit Scoped(const char* name) : name_(name) { set(name_.c_str(), true); }
  ~Scoped() { set(name_.c_str(), false); }
  Scoped(const Scoped&) = delete;
  Scoped& operator=(const Scoped&) = delete;

 private:
  std::string name_;
};

}  // namespace ctl

// Two-sided negative control. `holds` is the guarantee as a predicate: it must be TRUE with the
// control off, FALSE with it on, and TRUE again once restored — three named assertions. A
// middle PASS means the control does not reach the guarantee, or the predicate is vacuous; an
// outer FAIL means the guarantee is broken. Both are real failures, and neither can come from a
// stale binary: the flag and the assertion are in the same one.
template <class F>
inline void check_controlled(const char* control, const std::string& what, F&& holds) {
  const std::string c(control);
  check(holds(), what + "  [" + c + " off]");
  {
    ctl::Scoped on(control);
    check(!holds(), what + "  [" + c + " ON: the guarantee must be gone]");
  }
  check(holds(), what + "  [" + c + " restored]");
}
#endif  // TESTKIT_CONTROL

// ONE DIRECTORY PER TEST-BINARY RUN, and every temp path this process makes lives under it. The
// name comes from `mkdtemp`, which is the POSIX primitive for exactly this: it creates a
// directory whose name is unique against everything that already exists, atomically, with no
// seed or clock for us to get wrong.
//
// **DO NOT REPLACE IT WITH `<pid>_<counter>`, which reads as unique and is not.** A doc comment
// on that spelling claimed the property `mkdtemp` actually delivers ("leftovers from a previous
// failed run can't bleed into each other"), and neither component provides it: the OS reuses
// pids, and the counter is a process-local static that runs 0, 1, 2… the same way on every run
// of the same binary. Nothing ever deleted the directories, so **8,673 had accumulated**, of
// which 1,149 were a fully-populated model cache. A run that drew a pid matching one of those
// inherited its CONTENTS: a pull test found `model.safetensors` already complete, so `pull()`
// skipped the download entirely and four assertions about an interruption that never happened
// failed. **Measured at 1 run in 20**, and established rather than assumed: over 60 runs,
// pid-collided-with-a-populated-leftover and test-failed correlated perfectly.
//
// Two independent facts had to combine, and either alone is harmless: unique names make a
// leftover unreadable junk, and cleanup makes a collision find nothing. **The uniqueness is what
// fixes the defect; the cleanup is hygiene**, which is why the root survives a FAILING run —
// evidence is worth more than tidiness, and it can no longer be read back.
inline char* tmp_root_buf() {
  static char buf[1024] = {0};  // no destructor: `atexit` must be able to read it at exit
  return buf;
}

inline void tmp_root_cleanup() {
  const char* root = tmp_root_buf();
  if (!root[0]) return;
  if (testkit_any_failed() || std::getenv("ROLL_KEEP_TMP")) {
    std::fprintf(stdout, "  (temp files kept at %s)\n", root);
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

inline const char* tmp_root() {
  char* buf = tmp_root_buf();
  if (buf[0]) return buf;
  const char* t = std::getenv("TMPDIR");
  std::string dir = (t && *t) ? t : "/tmp";
  if (dir.back() != '/') dir.push_back('/');
  std::string tmpl = dir + "roll_test_XXXXXX";
  std::vector<char> mutable_tmpl(tmpl.begin(), tmpl.end());
  mutable_tmpl.push_back('\0');
  const char* made = ::mkdtemp(mutable_tmpl.data());
  if (!made) {
    std::fprintf(stderr, "testkit: mkdtemp under %s failed\n", dir.c_str());
    std::abort();  // every path below would silently collide; dying is the honest answer
  }
  std::snprintf(buf, 1024, "%s", made);
  std::atexit(&tmp_root_cleanup);
  return buf;
}

// A path under this run's private root. It does NOT exist yet — callers create a file or a
// directory at it, and one caller deliberately relies on it being absent.
inline std::string tmp_path(const char* tag) {
  static int ctr = 0;
  std::string p = std::string(tmp_root()) + "/" + tag + "_" + std::to_string(ctr++);
  // THE GUARANTEE, CHECKED RATHER THAN BELIEVED: a temp path is FRESH. This is precisely what
  // the old scheme broke — a colliding pid handed back a populated directory and the test read
  // a previous run's state as if it were its own, with every assertion about a download that
  // never happened failing and nothing saying why. Under `mkdtemp` it cannot fire, and that is
  // the reason it is here: it is the line that says so, on every run of every test, rather than
  // a property anyone has to take on trust.
  if (std::filesystem::exists(p)) {
    std::fprintf(stderr,
                 "testkit: temp path %s already exists — this run is about to read another "
                 "run's state\n",
                 p.c_str());
    std::abort();
  }
  return p;
}

// Three suites cannot isolate themselves and must therefore EXCLUDE each other across
// PROCESSES, not merely within one ctest run. Two resources are shared and both on purpose:
//
//   * the WORLD PATH is fixed (`/tmp/roll_plain_control` and friends) because the workspace
//     root is printed INSIDE the recording — the gate's approval reason says "code-tier
//     self-work in <root>" — so it has to be the same bytes on every run of every machine. A
//     pid-suffixed path would change the recorded bytes and break the control it exists to be.
//   * the PORT is 8080, hardcoded in `run_repl` for `--mock`. That is the production path this
//     control freezes, so a test may not move it.
//
// Neither can be made unique, so concurrency is what gives way: a second run WAITS instead of
// colliding. `ctest` marks these serial within one invocation, which is why the collision only
// appears when a SECOND invocation exists — and where more than one agent commits, it does.
//
// The lock is advisory and process-scoped; it is released when the fd closes, including on a
// crash, so a died-holding-it run cannot wedge the next one.
class SharedWorldLock {
 public:
  SharedWorldLock() {
    const char* t = std::getenv("TMPDIR");
    std::string dir = (t && *t) ? t : "/tmp";
    if (dir.back() != '/') dir.push_back('/');
    path_ = dir + "roll_shared_world.lock";
    fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ >= 0 && ::flock(fd_, LOCK_EX) != 0) { ::close(fd_); fd_ = -1; }
  }
  ~SharedWorldLock() { if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); } }
  SharedWorldLock(const SharedWorldLock&) = delete;
  SharedWorldLock& operator=(const SharedWorldLock&) = delete;
  bool held() const { return fd_ >= 0; }

 private:
  std::string path_;
  int fd_ = -1;
};

// One scripted roll session, driven over a pipe: the binary's arguments, the environment
// ADDED on top of a scrubbed one, the working directory, and the whole of stdin.
//
// The scrub is the point of routing every session through here. Every ROLL_* /
// ANTHROPIC_* / TMPDIR the invoking shell carries is dropped before the script's own are
// added, so a developer's active model, a real key, or a pricing override cannot leak into
// a recording or an assertion — and a test that spawns roll through any other path has to
// re-derive that rule, which is how a third copy of it appears.
struct RollScript {
  std::string name;                 // for messages; a control test uses it as a fixture stem
  std::vector<std::string> args;    // after the binary
  std::vector<std::string> env;     // KEY=VALUE, added on top of the scrubbed environ
  std::string cwd;
  std::string stdin_text;
};

// Runs `bin` as a child with piped stdin/stdout and returns its complete stdout. stderr is
// left on the test's own stderr so ctest's log shows the mock server's chatter when a run
// goes wrong. The script is written whole and stdin closed before stdout is drained: every
// script is far smaller than a pipe buffer, so that cannot deadlock.
inline std::string run_roll(const char* bin, const RollScript& sc, int& exit_code) {
  int in[2], out[2];
  if (::pipe(in) != 0 || ::pipe(out) != 0) {
    check(false, "pipe() for " + sc.name);
    exit_code = -1;
    return {};
  }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&fa, out[1], STDOUT_FILENO);
  for (int fd : {in[0], in[1], out[0], out[1]}) posix_spawn_file_actions_addclose(&fa, fd);
  posix_spawn_file_actions_addchdir(&fa, sc.cwd.c_str());

  std::vector<std::string> env_store;
  for (char** e = *_NSGetEnviron(); e && *e; ++e) {
    const std::string entry(*e);
    if (entry.rfind("ROLL_", 0) == 0 || entry.rfind("ANTHROPIC_", 0) == 0 ||
        entry.rfind("TMPDIR=", 0) == 0)
      continue;
    env_store.push_back(entry);
  }
  for (const auto& kv : sc.env) env_store.push_back(kv);
  std::vector<char*> envp;
  for (auto& v : env_store) envp.push_back(v.data());
  envp.push_back(nullptr);

  std::vector<std::string> args{bin};
  args.insert(args.end(), sc.args.begin(), sc.args.end());
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int rc = ::posix_spawn(&pid, bin, &fa, nullptr, argv.data(), envp.data());
  posix_spawn_file_actions_destroy(&fa);
  ::close(in[0]);
  ::close(out[1]);
  if (rc != 0) {
    check(false, std::string("spawn ") + bin + ": " + std::strerror(rc));
    ::close(in[1]);
    ::close(out[0]);
    exit_code = -1;
    return {};
  }

  size_t off = 0;
  while (off < sc.stdin_text.size()) {
    const ssize_t n = ::write(in[1], sc.stdin_text.data() + off, sc.stdin_text.size() - off);
    if (n <= 0) break;
    off += (size_t)n;
  }
  ::close(in[1]);

  std::string captured;
  char buf[4096];
  for (;;) {
    const ssize_t n = ::read(out[0], buf, sizeof(buf));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    captured.append(buf, (size_t)n);
  }
  ::close(out[0]);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  return captured;
}

}  // namespace testkit

// TESTKIT_TEST(name) { ... } — defines the test AND registers it, in one place, so the two
// cannot drift apart.
#define TESTKIT_TEST(name)                                    \
  static void name();                                         \
  static ::testkit::Registrar testkit_reg_##name(#name, name); \
  static void name()
