// live_terminal_test.cpp — THE REAL PROGRAMS, ON A TERMINAL THAT CANNOT DRAW 24-BIT COLOUR.
//
// The bug that started all of this lived in the three programs' own event loops: each hard-coded
// `ROLLTUI_DEPTH_TRUECOLOR` into its present call, so a terminal that could not draw it (Apple's
// Terminal on macOS 15) was sent `38;2;r;g;b` and drew every colour as nonsense. Nothing exercised a
// program's live loop, so nothing could have noticed. This does: each SHIPPED binary is started on a
// pty whose master plays a terminal — Apple Terminal, answering Primary DA and nothing else, with a
// COLORTERM=truecolor that a shell rc file exported and that is not true — and what the program
// writes is read back.
//
// THE CONTROL IS THE POINT AND IT IS HERE. "No 24-bit colour was sent" is satisfied just as well by
// a program that drew nothing, or drew in monochrome, so the same binary is started on a terminal that
// CONFIRMS 24-bit colour and must send it. A program that passes the first half without the second is
// not fixed, it is silent.
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "rolltui_test.hpp"

using namespace testkit;

#if !defined(ROLLTUI_STUDIO_PRODUCT_BIN) || !defined(ROLLTUI_PAINT_PRODUCT_BIN) || !defined(DIRKTUI_PRODUCT_BIN)
#error "the three product binaries must be named"
#endif

namespace {

using Env = std::vector<std::pair<std::string, std::string>>;

struct Terminal {
  bool confirms_24bit = false;  // answers the DECRQSS colour question with a 24-bit colour, and the background with a dark one
};

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// What a program wrote to a terminal in `run_ms`, having started it under `env` on a pty this side plays the terminal of.
struct Capture {
  std::string bytes;
  bool started = false;      // it entered the alternate screen
  std::string after_probe;   // everything written AFTER the first Primary DA question: the frames, not the questions
};

// Runs until `wait_for` has been drawn (then a short settle, so the whole frame is there) or `max_ms` passes: a fixed
// window is a race against however loaded the machine running the suite is, and this suite runs eight tests at once.
Capture run_on_terminal(const std::string& bin, const std::vector<std::string>& args, const Env& env, const Terminal& term, int max_ms,
                        const char* wait_for) {
  Capture cap;
  int master = -1, slave = -1;
  winsize ws{};
  ws.ws_row = 24;
  ws.ws_col = 80;
  if (openpty(&master, &slave, nullptr, nullptr, &ws) != 0) return cap;
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::close(master);
    ::setsid();
    ::ioctl(slave, TIOCSCTTY, 0);
    ::signal(SIGTTOU, SIG_IGN);
    ::tcsetpgrp(slave, ::getpgrp());
    ::dup2(slave, 0);
    ::dup2(slave, 1);
    ::dup2(slave, 2);
    if (slave > 2) ::close(slave);
    for (const auto& kv : env) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(bin.c_str()));
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    ::execv(bin.c_str(), argv.data());
    ::_exit(127);
  }
  ::close(slave);

  std::size_t processed = 0;
  const long long deadline = now_ms() + max_ms;
  long long settle_until = 0;
  while (now_ms() < deadline && (settle_until == 0 || now_ms() < settle_until)) {
    pollfd p{master, POLLIN, 0};
    if (::poll(&p, 1, 20) > 0 && (p.revents & POLLIN)) {
      char b[4096];
      const ssize_t k = ::read(master, b, sizeof b);
      if (k <= 0) break;
      cap.bytes.append(b, static_cast<std::size_t>(k));
      // Answer each question the way this terminal would, in the order asked. The strings are asked in ONE write,
      // and DA1 is last, so a reply is composed from everything new in the buffer.
      std::string out;
      std::size_t at = processed;
      while (at < cap.bytes.size()) {
        auto starts = [&](const char* lit) { return cap.bytes.compare(at, std::strlen(lit), lit) == 0; };
        if (starts("\x1b]11;?\x1b\\")) {
          if (term.confirms_24bit) out += "\x1b]11;rgb:1e1e/1e1e/2e2e\x1b\\";
          at += 8;
        } else if (starts("\x1bP$qm\x1b\\")) {
          if (term.confirms_24bit) out += "\x1bP1$r0;48:2::1:1:1m\x1b\\";
          at += 6;
        } else if (starts("\x1b[c")) {
          out += "\x1b[?1;2c";
          at += 3;
        } else {
          ++at;
        }
      }
      processed = cap.bytes.size();
      if (!out.empty()) (void)!::write(master, out.data(), out.size());
      if (settle_until == 0) {
        const std::size_t da = cap.bytes.find("\x1b[c");
        if (da != std::string::npos && cap.bytes.find(wait_for, da) != std::string::npos) settle_until = now_ms() + 300;
      }
    }
  }
  ::kill(pid, SIGTERM);  // restores the terminal and dies; only what it wrote matters here
  // What it wrote while dying is read, and THEN the master is released: the child is a session leader, and a
  // session leader's exit revokes its terminal, which waits for the last holder of either side. Only after that
  // is it waited for — the order terminal_test.cpp learned the same way.
  for (int i = 0; i < 20; ++i) {
    pollfd p{master, POLLIN, 0};
    if (::poll(&p, 1, 10) > 0 && (p.revents & POLLIN)) {
      char b[4096];
      const ssize_t k = ::read(master, b, sizeof b);
      if (k > 0) cap.bytes.append(b, static_cast<std::size_t>(k));
      else break;
    }
  }
  ::close(master);
  int status = 0;
  for (int i = 0; i < 100 && ::waitpid(pid, &status, WNOHANG) == 0; ++i) ::usleep(20 * 1000);
  if (::waitpid(pid, &status, WNOHANG) == 0) {  // it did not die by itself: it does not get to hang the suite
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
  }
  cap.started = cap.bytes.find("\x1b[?1049h") != std::string::npos;
  const std::size_t da = cap.bytes.find("\x1b[c");
  if (da != std::string::npos) cap.after_probe = cap.bytes.substr(da + 3);
  return cap;
}

std::vector<std::string> g_scratch_dirs;  // every directory this run made, removed when it ends

std::string make_dir(const char* tag) {
  std::string t = std::string("/tmp/rolltui_live_") + tag + "_XXXXXX";
  char* p = ::mkdtemp(t.data());
  if (p) g_scratch_dirs.push_back(p);
  return p ? std::string(p) : std::string();
}

bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// Apple Terminal on macOS 15: 256 colours, announces itself, and a COLORTERM that a shell rc file exported.
Env apple_sequoia(const std::string& config) {
  return {{"TERM", "xterm-256color"}, {"TERM_PROGRAM", "Apple_Terminal"}, {"TERM_PROGRAM_VERSION", "455.1"},
          {"COLORTERM", "truecolor"}, {"ROLL_CONFIG_DIR", config}, {"HOME", config}, {"LANG", "en_US.UTF-8"}};
}

// Something that draws 24-bit colour and says so.
Env ghostty(const std::string& config) {
  return {{"TERM", "xterm-ghostty"}, {"TERM_PROGRAM", "ghostty"}, {"COLORTERM", "truecolor"},
          {"ROLL_CONFIG_DIR", config}, {"HOME", config}, {"LANG", "en_US.UTF-8"}};
}

}  // namespace

int main() {
  // Clear everything in this process's own environment that a child would inherit and that decides colour.
  for (const char* n : {"ROLL_COLOR_DEPTH", "ROLL_AMBIGUOUS_WIDE", "TMUX", "STY", "SSH_CONNECTION", "COLORFGBG", "NO_COLOR"}) ::unsetenv(n);

  struct Program {
    const char* name;
    std::string bin;
    std::vector<std::string> args;
    const char* proof_of_life;  // something only its screen says
  };
  const std::string scratch_files = make_dir("files");
  {
    // A few entries for the browser to list, so it has something coloured to draw.
    for (const char* f : {"alpha.txt", "beta.md", "gamma.cpp"}) {
      const std::string p = scratch_files + "/" + f;
      FILE* fp = std::fopen(p.c_str(), "w");
      if (fp) { std::fputs("x\n", fp); std::fclose(fp); }
    }
    ::mkdir((scratch_files + "/folder").c_str(), 0755);
  }
  const std::vector<Program> programs = {
      {"dirktui", DIRKTUI_PRODUCT_BIN, {scratch_files}, "alpha.txt"},
      {"rolltui-paint", ROLLTUI_PAINT_PRODUCT_BIN, {}, "ink"},
      {"rolltui-studio", ROLLTUI_STUDIO_PRODUCT_BIN, {}, "transcript"},
  };

  for (const Program& p : programs) {
    const std::string tag = std::string("[") + p.name + "] ";
    const std::string cfg = make_dir("cfg");

    // ---- Apple Terminal on macOS 15: answers Primary DA, cannot draw 24-bit colour, COLORTERM lies
    const Capture apple = run_on_terminal(p.bin, p.args, apple_sequoia(cfg), Terminal{false}, 8000, p.proof_of_life);
    check(apple.started, tag + "started on the terminal (it entered the alternate screen)");
    check(has(apple.after_probe, p.proof_of_life), tag + "drew its screen (found '" + p.proof_of_life + "')");
    check(has(apple.after_probe, "38;5;") || has(apple.after_probe, "48;5;"),
          tag + "on Apple Terminal 455 it drew in PALETTE colour");
    check(!has(apple.after_probe, "38;2;") && !has(apple.after_probe, "48;2;"),
          tag + "…and sent it NO 24-bit colour, which that terminal reads as stray attributes and draws as nonsense");

    // ---- the CONTROL: the same program on a terminal that confirms 24-bit colour MUST send it
    const std::string cfg2 = make_dir("cfg");
    const Capture real = run_on_terminal(p.bin, p.args, ghostty(cfg2), Terminal{true}, 8000, p.proof_of_life);
    check(real.started && has(real.after_probe, p.proof_of_life), tag + "CONTROL: started and drew on a terminal that draws 24-bit colour");
    check(has(real.after_probe, "38;2;") || has(real.after_probe, "48;2;"),
          tag + "CONTROL: …and sent 24-bit colour there — so the absence above is the program choosing, not the program failing to draw");
  }

  // ---- and a program's SECOND launch on the same terminal asks it nothing, and still draws at the same depth
  {
    const std::string cfg = make_dir("second");
    const std::string bin = DIRKTUI_PRODUCT_BIN;
    const Capture first = run_on_terminal(bin, {scratch_files}, apple_sequoia(cfg), Terminal{false}, 8000, "alpha.txt");
    const Capture second = run_on_terminal(bin, {scratch_files}, apple_sequoia(cfg), Terminal{false}, 8000, "alpha.txt");
    check(has(first.bytes, "\x1b[c"), "dirktui's first launch on a new terminal asks it (one exchange)");
    check(!has(second.bytes, "\x1b[c") && !has(second.bytes, "\x1bP$qm"),
          "…its second launch asks it NOTHING: the answers were remembered");
    check(second.started && (has(second.bytes, "38;5;") || has(second.bytes, "48;5;")) && !has(second.bytes, "38;2;") && !has(second.bytes, "48;2;"),
          "…and still draws in palette colour, from what was remembered");
  }

  for (const std::string& d : g_scratch_dirs) {
    const std::string rm = "rm -rf '" + d + "'";
    (void)!std::system(rm.c_str());
  }
  return report("rolltui_live_terminal_test");
}
