// termfacts_test.cpp — WHAT A TERMINAL IS, as pure functions: the environment matrix (every terminal
// this was written for, and the one that wrote the bug), the reply scanner, the fingerprint and the
// file that remembers. No tty, no pty and no clock: an environment and a buffer go in and facts come
// out, which is the whole reason `rolltui_termfacts.c` is separate from the terminal.
//
// THE CASE THAT STARTED IT is the first block: Apple's Terminal on macOS 15 sets TERM=xterm-256color,
// TERM_PROGRAM=Apple_Terminal and no COLORTERM, cannot draw 24-bit colour, and was being sent it
// anyway — every colour it drew was nonsense. The matrix says what each terminal must be taken for,
// and the guard row says a COLORTERM exported by a shell rc file does not change that.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_termfacts.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_theme.h"      // INTERNAL: this test opts in
#include "rolltui/c/rolltui_unicode.h"    // INTERNAL: this test opts in
#include "rolltui_test.hpp"

using namespace testkit;

namespace {

struct Env {
  const char *colorterm = nullptr, *term = nullptr, *program = nullptr, *program_version = nullptr, *lc_terminal = nullptr,
             *colorfgbg = nullptr, *force_depth = nullptr, *force_wide = nullptr, *tmux = nullptr, *sty = nullptr,
             *ssh = nullptr, *locale = "en_US.UTF-8";
  const char* os = "25.6.0";
};

RolltuiTermEnv make(const Env& e) {
  RolltuiTermEnv r;
  std::memset(&r, 0, sizeof r);
  r.colorterm = e.colorterm;
  r.term = e.term;
  r.term_program = e.program;
  r.term_program_version = e.program_version;
  r.lc_terminal = e.lc_terminal;
  r.colorfgbg = e.colorfgbg;
  r.force_depth = e.force_depth;
  r.force_wide = e.force_wide;
  r.tmux = e.tmux;
  r.sty = e.sty;
  r.ssh_connection = e.ssh;
  r.locale = e.locale;
  std::snprintf(r.os_release, sizeof r.os_release, "%s", e.os);
  std::snprintf(r.host, sizeof r.host, "testhost");
  return r;
}

RolltuiTermFacts facts_of(const Env& e) {
  const RolltuiTermEnv env = make(e);
  RolltuiTermFacts f;
  rolltui_termfacts_from_env(&env, &f);
  return f;
}

const char* depth_name(unsigned char d) {
  switch (d) {
    case ROLLTUI_DEPTH_MONO: return "mono";
    case ROLLTUI_DEPTH_ANSI16: return "16";
    case ROLLTUI_DEPTH_ANSI256: return "256";
    case ROLLTUI_DEPTH_TRUECOLOR: return "truecolor";
  }
  return "?";
}

void depth_is(const std::string& what, const Env& e, unsigned char want, unsigned char source) {
  const RolltuiTermFacts f = facts_of(e);
  check(f.depth == want && f.depth_source == source,
        what + " → " + depth_name(f.depth) + " (source " + std::to_string(f.depth_source) + "), want " + depth_name(want) +
            " (source " + std::to_string(source) + ")");
}

// The bytes a truecolor terminal writes back to the FULL batch, in the order the questions were
// asked: kitty's flags, OSC 11, DECRQSS, XTVERSION, the cursor report, and Primary DA last.
const std::string kFullReply =
    "\x1b[?0u"
    "\x1b]11;rgb:1e1e/1e1e/2e2e\x1b\\"
    "\x1bP1$r0;48:2::1:2:3m\x1b\\"
    "\x1bP>|ghostty 1.1.3\x1b\\"
    "\x1b[1;3R"
    "\x1b[?62;c";

std::string take_all(std::string bytes, bool expect_cpr, RolltuiTermReplies& rep) {
  std::memset(&rep, 0, sizeof rep);
  const std::size_t n = rolltui_termreplies_take(bytes.data(), bytes.size(), expect_cpr ? 1 : 0, &rep);
  return bytes.substr(0, n);
}

std::string scratch_dir(const char* tag) {
  std::string t = std::string("/tmp/rolltui_termfacts_") + tag + "_XXXXXX";
  char* p = ::mkdtemp(t.data());
  return p ? std::string(p) : std::string();
}

}  // namespace

int main() {
  // ============================ DEPTH, FROM THE ENVIRONMENT ============================
  // THE BUG: Apple Terminal on Sequoia. Whatever it is, it is not 24-bit.
  depth_is("Apple Terminal, macOS 15 (build 455), no COLORTERM", {.term = "xterm-256color", .program = "Apple_Terminal", .program_version = "455.1"},
           ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);
  // THE GUARD: shell config exports COLORTERM=truecolor unconditionally, and it follows the person into
  // a terminal that cannot honour it. On a build known to lack 24-bit colour it is not believed.
  depth_is("…with COLORTERM=truecolor exported by a shell rc file", {.colorterm = "truecolor", .term = "xterm-256color", .program = "Apple_Terminal", .program_version = "455.1"},
           ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);
  depth_is("…and with no version at all, which is treated as an old one", {.colorterm = "truecolor", .term = "xterm-256color", .program = "Apple_Terminal"},
           ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);
  depth_is("Apple Terminal, macOS 26 (build 470), which does draw 24-bit", {.colorterm = "truecolor", .term = "xterm-256color", .program = "Apple_Terminal", .program_version = "470.2"},
           ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  depth_is("the guard never RAISES: an old Apple Terminal on TERM=xterm stays at sixteen", {.term = "xterm", .program = "Apple_Terminal", .program_version = "455"},
           ROLLTUI_DEPTH_ANSI16, ROLLTUI_FACT_ENV);

  depth_is("Ghostty, local", {.colorterm = "truecolor", .term = "xterm-ghostty", .program = "ghostty"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  // Over ssh, TERM is forwarded and COLORTERM is not. Without the table this is SIXTEEN colours.
  depth_is("Ghostty over ssh (TERM only)", {.term = "xterm-ghostty", .ssh = "10.0.0.2 5000 10.0.0.9 22"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  depth_is("kitty over ssh (TERM only)", {.term = "xterm-kitty"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  depth_is("WezTerm", {.term = "wezterm"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  depth_is("iTerm2", {.term = "xterm-256color", .program = "iTerm.app"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  depth_is("iTerm2 over ssh, where only LC_TERMINAL survives", {.term = "xterm-256color", .lc_terminal = "iTerm2"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);
  depth_is("VS Code's terminal", {.term = "xterm-256color", .program = "vscode"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);

  depth_is("a plain xterm-256color is 256, not more", {.term = "xterm-256color"}, ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);
  depth_is("an unknown program on xterm-256color is not over-detected", {.term = "xterm-256color", .program = "SomethingNew"}, ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);
  depth_is("tmux with no COLORTERM", {.term = "tmux-256color", .tmux = "/tmp/tmux-501/default,1,0"}, ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);
  depth_is("TERM=xterm is sixteen", {.term = "xterm"}, ROLLTUI_DEPTH_ANSI16, ROLLTUI_FACT_ENV);
  depth_is("TERM=dumb is none", {.term = "dumb"}, ROLLTUI_DEPTH_MONO, ROLLTUI_FACT_ENV);
  depth_is("nothing set at all is none, and says nothing said it", {}, ROLLTUI_DEPTH_MONO, ROLLTUI_FACT_DEFAULT);
  depth_is("COLORTERM=24bit is the alias", {.colorterm = "24bit", .term = "xterm"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_ENV);

  // FORCED beats everything, including a guard: a person who knows better than the environment.
  depth_is("ROLL_COLOR_DEPTH=256 over COLORTERM=truecolor", {.colorterm = "truecolor", .term = "xterm-ghostty", .force_depth = "256"}, ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_FORCED);
  depth_is("ROLL_COLOR_DEPTH=truecolor on Apple Terminal 455 (the person insists)", {.term = "xterm-256color", .program = "Apple_Terminal", .program_version = "455", .force_depth = "truecolor"},
           ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_FORCED);
  depth_is("ROLL_COLOR_DEPTH=24bit is the alias here too", {.term = "xterm-256color", .force_depth = "24bit"}, ROLLTUI_DEPTH_TRUECOLOR, ROLLTUI_FACT_FORCED);
  depth_is("ROLL_COLOR_DEPTH=mono", {.colorterm = "truecolor", .force_depth = "mono"}, ROLLTUI_DEPTH_MONO, ROLLTUI_FACT_FORCED);
  depth_is("an invalid ROLL_COLOR_DEPTH is ignored, not an error", {.term = "xterm-256color", .force_depth = "banana"}, ROLLTUI_DEPTH_ANSI256, ROLLTUI_FACT_ENV);

  {
    const RolltuiTermEnv old_apple = make({.term = "xterm-256color", .program = "Apple_Terminal", .program_version = "455.1"});
    const RolltuiTermEnv new_apple = make({.term = "xterm-256color", .program = "Apple_Terminal", .program_version = "470.2"});
    const RolltuiTermEnv ghost = make({.term = "xterm-ghostty"});
    check(rolltui_termenv_no_24bit_known(&old_apple) == 1 && rolltui_termenv_no_24bit_known(&new_apple) == 0 && rolltui_termenv_no_24bit_known(&ghost) == 0,
          "only an Apple Terminal before build 470 is KNOWN not to draw 24-bit colour");
  }

  // ============================ MODE, AMBIGUOUS WIDTH, NAME ============================
  {
    RolltuiTermFacts f = facts_of({});
    check(f.mode == ROLLTUI_MODE_DARK && f.mode_source == ROLLTUI_FACT_DEFAULT, "with nothing to go on the terminal is taken to be dark");
    f = facts_of({.colorfgbg = "15;0"});
    check(f.mode == ROLLTUI_MODE_DARK && f.mode_source == ROLLTUI_FACT_ENV, "COLORFGBG 15;0 is a dark background");
    f = facts_of({.colorfgbg = "0;15"});
    check(f.mode == ROLLTUI_MODE_LIGHT && f.mode_source == ROLLTUI_FACT_ENV, "COLORFGBG 0;15 is a light one");
    f = facts_of({.colorfgbg = "0;default;15"});
    check(f.mode == ROLLTUI_MODE_LIGHT, "…and the last field is the background even when there are three");
    f = facts_of({.colorfgbg = "0;7"});
    check(f.mode == ROLLTUI_MODE_LIGHT, "index 7 (white) is light");
    f = facts_of({.colorfgbg = "rubbish"});
    check(f.mode == ROLLTUI_MODE_DARK && f.mode_source == ROLLTUI_FACT_DEFAULT, "an unreadable COLORFGBG says nothing");

    f = facts_of({});
    check(f.ambiguous_wide == 0 && f.ambiguous_source == ROLLTUI_FACT_DEFAULT, "an ambiguous glyph is one cell until something says two");
    f = facts_of({.force_wide = "1"});
    check(f.ambiguous_wide == 1 && f.ambiguous_source == ROLLTUI_FACT_FORCED, "ROLL_AMBIGUOUS_WIDE=1 forces two cells");
    f = facts_of({.force_wide = "0"});
    check(f.ambiguous_wide == 0 && f.ambiguous_source == ROLLTUI_FACT_FORCED, "…and =0 forces one, which is still 'said outright'");

    f = facts_of({.term = "xterm-256color", .program = "Apple_Terminal", .program_version = "455.1"});
    check(std::string(f.name) == "Apple_Terminal 455.1", "the name is the program and its version for a person to read [" + std::string(f.name) + "]");
    f = facts_of({.term = "xterm-256color"});
    check(std::string(f.name) == "xterm-256color", "…or TERM when nothing announced itself");
    check(f.keyboard == ROLLTUI_PROTOCOL_LEGACY && !f.responsive && !f.remembered, "the environment alone never claims a terminal answered");
  }

  // ============================ THE FINGERPRINT ============================
  {
    auto fp = [](const Env& e, const char* app) {
      const RolltuiTermEnv env = make(e);
      char out[512];
      rolltui_termfacts_fingerprint(&env, app, out, sizeof out);
      return std::string(out);
    };
    const Env base{.colorterm = "truecolor", .term = "xterm-ghostty", .program = "ghostty", .program_version = "1.1.3"};
    const std::string a = fp(base, "0.1.4");
    check(a == fp(base, "0.1.4"), "the same terminal has the same fingerprint");
    check(a != fp(base, "0.1.5"), "a new release of the program asks again");
    { Env e = base; e.os = "26.0.0"; check(a != fp(e, "0.1.4"), "a new operating system asks again"); }
    { Env e = base; e.program_version = "1.2.0"; check(a != fp(e, "0.1.4"), "a new terminal version asks again"); }
    { Env e = base; e.term = "xterm-256color"; check(a != fp(e, "0.1.4"), "a different TERM asks again"); }
    { Env e = base; e.colorterm = nullptr; check(a != fp(e, "0.1.4"), "…and so does losing COLORTERM"); }
    { Env e = base; e.tmux = "x"; check(a != fp(e, "0.1.4"), "being inside tmux asks again"); }
    { Env e = base; e.locale = "C"; check(a != fp(e, "0.1.4"), "a different locale asks again"); }
    { Env e = base; e.ssh = "10.0.0.2 5000 10.0.0.9 22"; check(a != fp(e, "0.1.4"), "arriving over ssh asks again"); }
    {
      Env x = base, y = base;
      x.ssh = "10.0.0.2 5000 10.0.0.9 22";
      y.ssh = "10.0.0.2 61234 10.0.0.9 22";
      check(fp(x, "0.1.4") == fp(y, "0.1.4"), "…but a new ssh connection from the SAME machine does not (the ports change every time)");
      y.ssh = "10.0.0.3 61234 10.0.0.9 22";
      check(fp(x, "0.1.4") != fp(y, "0.1.4"), "…while a connection from a DIFFERENT machine does");
    }
    {
      const RolltuiTermEnv in_tmux = make({.tmux = "x"}), in_screen = make({.sty = "1.pts"}), bare = make(base);
      check(rolltui_termenv_multiplexed(&in_tmux) == 1 && rolltui_termenv_multiplexed(&in_screen) == 1 && rolltui_termenv_multiplexed(&bare) == 0,
            "tmux and screen are multiplexers; a terminal is not");
    }
    // A tiny buffer is not overrun and is still terminated.
    {
      const RolltuiTermEnv env = make(base);
      char tiny[8];
      std::memset(tiny, 'x', sizeof tiny);
      const std::size_t n = rolltui_termfacts_fingerprint(&env, "0.1.4", tiny, sizeof tiny);
      check(n == sizeof tiny - 1 && tiny[sizeof tiny - 1] == '\0', "a fingerprint into a small buffer is truncated and terminated");
    }
  }

  // ============================ THE QUESTIONS ============================
  {
    char q[512];
    // THE CONTROL: the keyboard question alone is BYTE FOR BYTE what negotiation always sent, so
    // folding it into a bigger batch cannot have changed what an existing terminal is asked.
    rolltui_termprobe_queries(ROLLTUI_TERMQ_KEYBOARD, q, sizeof q);
    check(std::string(q) == "\x1b[?u\x1b[?4m\x1b[c", "the keyboard question alone is exactly the historical one");
    std::size_t n = rolltui_termprobe_queries(ROLLTUI_TERMQ_ALL, q, sizeof q);
    const std::string all(q, n);
    check(all.size() >= 3 && all.substr(all.size() - 3) == "\x1b[c", "every batch ends in Primary DA, the question every terminal answers");
    check(all.find("\x1b]11;?") != std::string::npos, "the full batch asks for the background (OSC 11)");
    check(all.find("\x1bP$qm") != std::string::npos && all.find(";48;2;1;1;1m") != std::string::npos,
          "…asks the terminal what a 24-bit background became (DECRQSS)");
    check(all.find("\x1b[>0q") != std::string::npos, "…asks who it is (XTVERSION)");
    check(all.find("\xE2\x96\x88\x1b[6n") != std::string::npos, "…and draws one ambiguous glyph and asks where the cursor went");
    n = rolltui_termprobe_queries(ROLLTUI_TERMQ_BACKGROUND, q, sizeof q);
    check(std::string(q, n) == "\x1b]11;?\x1b\\\x1b[c", "asking one thing asks ONLY that thing, then DA");
    n = rolltui_termprobe_queries(0, q, sizeof q);
    check(std::string(q, n) == "\x1b[c", "asking nothing still ends in DA");
    char small[12];
    std::memset(small, 'x', sizeof small);
    n = rolltui_termprobe_queries(ROLLTUI_TERMQ_ALL, small, sizeof small);
    check(n < sizeof small && small[n] == '\0', "a small buffer is never overrun and stays terminated");
  }

  // ============================ THE ANSWERS ============================
  {
    RolltuiTermReplies rep;
    // The whole reply from a terminal that answers everything, with the person typing at the end.
    std::string rest = take_all(kFullReply + "xy", true, rep);
    check(rest == "xy", "every reply is removed and only what was typed is left [" + rest + "]");
    check(rep.da1 == 1 && rep.kitty == 1 && rep.key_last == 1 && rep.modkeys == 0, "Primary DA and kitty's flags were heard");
    check(rep.has_bg == 1 && rep.bg.r == 0x1e && rep.bg.g == 0x1e && rep.bg.b == 0x2e, "the background was read from OSC 11");
    check(rep.sgr == ROLLTUI_TERMR_SGR_TRUE, "a DECRQSS reply carrying 48:2 says the background took a 24-bit colour");
    check(std::string(rep.version) == "ghostty 1.1.3", "XTVERSION was read [" + std::string(rep.version) + "]");
    check(rep.cpr == 1 && rep.cpr_row == 1 && rep.cpr_col == 3, "the cursor report was read: an ambiguous glyph took two cells");

    // THE PROPERTY: cut the bytes ANYWHERE and read on, and the result is the same. A terminal's
    // reply is written in one go and read in however many pieces the kernel likes.
    {
      const std::string whole = kFullReply + "xy";
      bool all_same = true;
      std::string why;
      for (std::size_t cut = 0; cut <= whole.size(); ++cut) {
        RolltuiTermReplies split;
        std::memset(&split, 0, sizeof split);
        std::string buf = whole.substr(0, cut);
        std::size_t n = rolltui_termreplies_take(buf.data(), buf.size(), 1, &split);
        buf.resize(n);
        buf += whole.substr(cut);
        n = rolltui_termreplies_take(buf.data(), buf.size(), 1, &split);
        buf.resize(n);
        const bool same = buf == "xy" && split.da1 == 1 && split.has_bg == 1 && split.cpr == 1 && split.cpr_col == 3 &&
                          split.sgr == ROLLTUI_TERMR_SGR_TRUE && std::string(split.version) == "ghostty 1.1.3" && split.kitty == 1;
        if (!same && all_same) {
          all_same = false;
          why = "cut at " + std::to_string(cut) + " left [" + buf + "]";
        }
      }
      check(all_same, "the batch reply read in two pieces, cut at EVERY byte, gives the same answer " + why);
    }

    // Apple Terminal sends nothing but Primary DA. Everything unanswered is unknown, not 'no'.
    take_all("\x1b[?1;2c", true, rep);
    check(rep.da1 == 1 && !rep.has_bg && !rep.cpr && rep.sgr == ROLLTUI_TERMR_SGR_NONE && !rep.version[0],
          "a terminal that answers only DA leaves every other question unanswered");

    // A cursor report is only a cursor report when one was asked for: `CSI 1;2 R` is Shift-F3.
    std::string keep = take_all("\x1b[1;2R", false, rep);
    check(keep == "\x1b[1;2R" && !rep.cpr, "with no cursor report outstanding, CSI 1;2 R is left alone as a key");
    keep = take_all("\x1b[1;2R", true, rep);
    check(keep.empty() && rep.cpr == 1, "…and with one outstanding it is the report");

    // Typing is never eaten: ordinary keys, arrow keys, and Alt-] all survive the scan.
    check(take_all("hello", true, rep) == "hello", "plain typing passes through");
    check(take_all("\x1b[A\x1b[B", true, rep) == "\x1b[A\x1b[B", "arrow keys pass through");
    check(take_all("\x1b]a", true, rep) == "\x1b]a", "Alt-] followed by a letter is a key, not an OSC");
    check(take_all("\x1bPa", true, rep) == "\x1bPa", "Alt-Shift-P followed by a letter is a key, not a DCS");

    // modifyOtherKeys is remembered as the LAST of the two keyboard answers, the order old code judged in.
    take_all("\x1b[?1u\x1b[>4;2m\x1b[?1;2c", true, rep);
    check(rep.kitty == 1 && rep.modkeys == 1 && rep.key_last == 2, "when both keyboard answers arrive the later one is the one that counts");

    // A DCS "invalid request" is not an SGR answer.
    take_all("\x1bP0$r\x1b\\\x1b[?1;2c", true, rep);
    check(rep.sgr == ROLLTUI_TERMR_SGR_NONE && rep.da1 == 1, "DCS 0 $ r (request not understood) is 'no answer', not 'not truecolor'");
    // A terminal that reduced our 24-bit colour says a palette index.
    take_all("\x1bP1$r48;5;16m\x1b\\\x1b[?1;2c", true, rep);
    check(rep.sgr == ROLLTUI_TERMR_SGR_256, "a DECRQSS reply carrying 48;5 says the colour was reduced to the palette");
    // The semicolon spelling of 24-bit is recognised as well as the colon one.
    take_all("\x1bP1$r0;48;2;1;2;3m\x1b\\", true, rep);
    check(rep.sgr == ROLLTUI_TERMR_SGR_TRUE, "…and 48;2 is 24-bit as much as 48:2 is");
    // A runaway string with no terminator is not waited for for ever.
    {
      std::string runaway = "\x1b]11;" + std::string(5000, 'x');
      RolltuiTermReplies r2;
      std::memset(&r2, 0, sizeof r2);
      const std::size_t n = rolltui_termreplies_take(runaway.data(), runaway.size(), 0, &r2);
      check(n < runaway.size(), "an OSC that never ends is dropped, not held for ever");
    }
  }

  // ============================ APPLYING THEM ============================
  {
    // Terminal says 24-bit, environment said 256: the terminal knows itself.
    RolltuiTermFacts f = facts_of({.term = "xterm-256color"});
    RolltuiTermReplies rep;
    take_all("\x1bP1$r48:2::1:2:3m\x1b\\\x1b[?1;2c", false, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_SGR);
    check(f.depth == ROLLTUI_DEPTH_TRUECOLOR && f.depth_source == ROLLTUI_FACT_PROBE && f.responsive,
          "a terminal that CONFIRMS 24-bit is drawn to at 24-bit, over what the environment guessed");

    // Environment said 24-bit (a lying COLORTERM); the terminal reduced the colour: believe the terminal.
    f = facts_of({.colorterm = "truecolor", .term = "xterm-256color"});
    take_all("\x1bP1$r48;5;16m\x1b\\\x1b[?1;2c", false, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_SGR);
    check(f.depth == ROLLTUI_DEPTH_ANSI256 && f.depth_source == ROLLTUI_FACT_PROBE, "a terminal that reduced our colour is not 24-bit, whatever COLORTERM said");

    // ...and a sixteen-colour guess is raised when the terminal shows it takes palette colours.
    f = facts_of({.term = "xterm"});
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_SGR);
    check(f.depth == ROLLTUI_DEPTH_ANSI256, "a palette answer raises a sixteen-colour guess to 256");

    // No answer changes nothing.
    f = facts_of({.term = "xterm-256color", .program = "Apple_Terminal", .program_version = "455"});
    take_all("\x1b[?1;2c", false, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_ALL & ~(unsigned)ROLLTUI_TERMQ_KEYBOARD);
    check(f.depth == ROLLTUI_DEPTH_ANSI256 && f.depth_source == ROLLTUI_FACT_ENV && f.responsive && f.mode_source == ROLLTUI_FACT_DEFAULT &&
              f.ambiguous_source == ROLLTUI_FACT_DEFAULT,
          "a terminal that answers only DA leaves every fact as the environment had it (Apple Terminal stays at 256)");

    // FORCED is never touched, by anything.
    f = facts_of({.colorterm = "truecolor", .term = "xterm-ghostty", .force_depth = "16", .force_wide = "1"});
    take_all("\x1bP1$r48:2::1:2:3m\x1b\\\x1b[1;2R\x1b[?1;2c", true, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_SGR | ROLLTUI_TERMQ_WIDTH);
    check(f.depth == ROLLTUI_DEPTH_ANSI16 && f.depth_source == ROLLTUI_FACT_FORCED && f.ambiguous_wide == 1 &&
              f.ambiguous_source == ROLLTUI_FACT_FORCED,
          "a fact that was said outright is not overridden by what the terminal says");

    // Background → mode; width → ambiguous.
    f = facts_of({});
    take_all("\x1b]11;rgb:ffff/ffff/ffff\x1b\\\x1b[1;3R\x1b[?1;2c", true, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_BACKGROUND | ROLLTUI_TERMQ_WIDTH);
    check(f.mode == ROLLTUI_MODE_LIGHT && f.mode_source == ROLLTUI_FACT_PROBE && f.has_background && f.background.r == 255,
          "a white background report makes the mode light, from the terminal");
    check(f.ambiguous_wide == 1 && f.ambiguous_source == ROLLTUI_FACT_PROBE, "a cursor report at column 3 is a two-cell ambiguous glyph");
    take_all("\x1b[1;2R\x1b[?1;2c", true, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_WIDTH);
    check(f.ambiguous_wide == 0, "…and at column 2 a one-cell one");
    // Asked about one thing, nothing else moves: a background reply not asked for is not applied.
    f = facts_of({});
    take_all("\x1b]11;rgb:ffff/ffff/ffff\x1b\\\x1b[?1;2c", false, rep);
    rolltui_termfacts_apply_replies(&f, &rep, ROLLTUI_TERMQ_WIDTH);
    check(f.mode == ROLLTUI_MODE_DARK && !f.has_background, "a reply to a question that was not asked is not applied");
  }


  // ============================ WHAT THE LIBRARY APPLIES WITHOUT BEING ASKED ============================
  // The point of all of it: a host that never reads a fact is still right. Three consumers, each at the one place
  // it is decided — the depth `present` draws at, the width a glyph is measured at, the mode a theme follows.
  {
    // THE CEILING. A depth handed to `present` is a request, and never more than the terminal has.
    rolltui_termfacts_set_active(-1, 0);
    check(rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_TRUECOLOR) == ROLLTUI_DEPTH_TRUECOLOR &&
              rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_ANSI16) == ROLLTUI_DEPTH_ANSI16,
          "with no terminal entered a depth is exactly what was asked for (a golden test, a tool writing to a pipe)");
    rolltui_termfacts_set_active(ROLLTUI_DEPTH_ANSI256, 0);
    check(rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_TRUECOLOR) == ROLLTUI_DEPTH_ANSI256, "a 256-colour terminal is never sent 24-bit, however hard a host asks");
    check(rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_ANSI16) == ROLLTUI_DEPTH_ANSI16 && rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_MONO) == ROLLTUI_DEPTH_MONO,
          "…and a host that asks for LESS gets less (a preview at sixteen colours is still possible)");
    rolltui_termfacts_set_active(ROLLTUI_DEPTH_ANSI256, 1);
    check(rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_TRUECOLOR) == ROLLTUI_DEPTH_ANSI256 && rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_ANSI16) == ROLLTUI_DEPTH_ANSI256,
          "a depth said outright REPLACES the request rather than capping it");
    rolltui_termfacts_set_active(ROLLTUI_DEPTH_TRUECOLOR, 1);
    check(rolltui_termfacts_clamp_depth(ROLLTUI_DEPTH_ANSI16) == ROLLTUI_DEPTH_TRUECOLOR, "…including upward, for a person who insists on 24-bit");
    rolltui_termfacts_set_active(-1, 0);

    // THE WIDTH. One place decides it, and it is ORed there.
    check(rolltui_u_codepoint_width(0x2588, 0) == 1 && rolltui_u_codepoint_width(0x2588, 1) == 2, "FULL BLOCK is one cell, or two where a host says so");
    rolltui_termfacts_set_active_wide(1);
    check(rolltui_u_codepoint_width(0x2588, 0) == 2, "…and two when the terminal measured two, though the host passed zero");
    check(rolltui_u_codepoint_width('a', 0) == 1 && rolltui_u_codepoint_width(0x4E2D, 0) == 2, "…without touching a glyph that was never ambiguous");
    check(rolltui_u_cluster_width(&(const RolltuiCodepoint&)(RolltuiCodepoint)0x2588, 1, 0) == 2, "…for a cluster as much as a codepoint");
    rolltui_termfacts_set_active_wide(0);
    check(rolltui_u_codepoint_width(0x2588, 0) == 1 && rolltui_u_codepoint_width(0x2588, 1) == 2,
          "a terminal that measured ONE cell does not override a host that says two");
    rolltui_termfacts_set_active_wide(-1);
    check(rolltui_u_codepoint_width(0x2588, 0) == 1, "…and withdrawing it puts everything back");

    // THE MODE. `theme_load` given a mode below zero follows the terminal.
    static const char kTheme[] =
        R"({"roles": {"text": {"fg": {"dark": "#111111", "light": "#eeeeee"}, "bg": {"dark": "#000000", "light": "#ffffff"}}}})";
    auto text_fg_at = [&](int mode) {
      RolltuiJsonValue* root = rolltui_json_parse(kTheme, sizeof kTheme - 1, nullptr);
      RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
      RolltuiStr name{};
      RolltuiThemeReport rep{};
      RolltuiEffectMap* eff = rolltui_theme_load(root, mode, rolltui_theme_default_vocab(), styles, &name, &rep);
      const unsigned char r = styles[ROLLTUI_ROLE_TEXT].fg.r;
      rolltui_effect_map_free(eff);
      rolltui_str_free(&name);
      rolltui_theme_report_release(&rep);
      rolltui_json_free(root);
      return static_cast<int>(r);
    };
    rolltui_termfacts_set_active_mode(-1);
    check(text_fg_at(ROLLTUI_MODE_DARK) == 0x11 && text_fg_at(ROLLTUI_MODE_LIGHT) == 0xee, "an explicit mode picks its own variant");
    check(text_fg_at(-1) == 0x11, "'auto' with no terminal is dark, as it always was");
    rolltui_termfacts_set_active_mode(ROLLTUI_MODE_LIGHT);
    check(text_fg_at(-1) == 0xee, "'auto' follows a terminal that reported a light background");
    check(text_fg_at(ROLLTUI_MODE_DARK) == 0x11, "…and an explicit mode still wins over the terminal");
    rolltui_termfacts_set_active_mode(99);
    check(rolltui_termfacts_active_mode() == -1, "a mode that is not one is withdrawn, not stored");
    rolltui_termfacts_set_active_mode(-1);

    // THE IDENTITY: this program's own file, so a new build asks again without a host saying so.
    char id1[256], id2[256];
    rolltui_termfacts_exe_identity(id1, sizeof id1);
    rolltui_termfacts_exe_identity(id2, sizeof id2);
    check(std::string(id1).rfind("rolltui-termfacts-test:", 0) == 0 && std::string(id1) == std::string(id2),
          "the executable's own name, mtime and size identify a build, the same twice [" + std::string(id1) + "]");
  }

  // ============================ REMEMBERING ============================
  {
    const std::string dir = scratch_dir("cache");
    check(!dir.empty(), "a scratch directory");
    RolltuiStr path_str{};
    rolltui_termcache_path(dir.c_str(), &path_str);
    const std::string path = std::string(path_str.p, path_str.n);
    rolltui_str_free(&path_str);
    check(path == dir + "/terminal-facts.json", "the file is terminal-facts.json under the directory given [" + path + "]");

    RolltuiTermCacheEntry e{};
    check(!rolltui_termcache_load(path.c_str(), "k1", &e), "no file is a miss");

    RolltuiTermCacheEntry w{};
    w.probed = 1000000;
    w.responsive = 1;
    w.keyboard = ROLLTUI_PROTOCOL_KITTY;
    w.has_wide = 1;
    w.ambiguous_wide = 1;
    w.has_depth = 1;
    w.depth = ROLLTUI_DEPTH_TRUECOLOR;
    w.answers_background = 1;
    w.has_background = 1;
    w.background = RolltuiStyleColor::rgb(0x1e, 0x1e, 0x2e);
    std::snprintf(w.name, sizeof w.name, "ghostty 1.1.3");
    check(rolltui_termcache_store(path.c_str(), "k1", &w) == 1, "an entry is stored");
    struct stat st{};
    check(::stat(path.c_str(), &st) == 0, "…and the file exists (parents created, written atomically)");

    RolltuiTermCacheEntry r{};
    check(rolltui_termcache_load(path.c_str(), "k1", &r) == 1, "…and read back");
    check(r.probed == 1000000 && r.responsive == 1 && r.keyboard == ROLLTUI_PROTOCOL_KITTY && r.has_wide == 1 && r.ambiguous_wide == 1 &&
              r.has_depth == 1 && r.depth == ROLLTUI_DEPTH_TRUECOLOR && r.answers_background == 1 && r.has_background == 1 &&
              r.background.kind == RolltuiStyleColor::Kind::Rgb && r.background.r == 0x1e && r.background.b == 0x2e &&
              std::string(r.name) == "ghostty 1.1.3",
          "every field survives the round trip");
    check(!rolltui_termcache_load(path.c_str(), "k2", &r), "a different fingerprint is a miss");

    // An entry that was NOT answered stays not answered: absence is preserved, not turned into a 0.
    RolltuiTermCacheEntry bare{};
    bare.probed = 2000000;
    bare.responsive = 0;
    check(rolltui_termcache_store(path.c_str(), "k2", &bare) == 1, "a second fingerprint is stored beside the first");
    RolltuiTermCacheEntry rb{};
    check(rolltui_termcache_load(path.c_str(), "k2", &rb) && !rb.has_wide && !rb.has_depth && !rb.has_background && !rb.responsive,
          "what was never asked is still unknown after the round trip");
    check(rolltui_termcache_load(path.c_str(), "k1", &r) == 1 && r.has_wide == 1, "…and the first is untouched");

    // Freshness.
    const long long hour = 3600, day = 24 * hour;
    RolltuiTermCacheEntry fr{};
    fr.probed = 10 * day;
    fr.responsive = 1;
    check(rolltui_termcache_fresh(&fr, fr.probed + 6 * day), "an answering terminal is trusted for six days");
    check(!rolltui_termcache_fresh(&fr, fr.probed + 8 * day), "…and asked again after eight");
    fr.responsive = 0;
    check(rolltui_termcache_fresh(&fr, fr.probed + 30 * 60), "a silent terminal is not asked again for half an hour");
    check(!rolltui_termcache_fresh(&fr, fr.probed + 2 * hour), "…but a silence is not remembered for long: it may have been a slow link");
    check(!rolltui_termcache_fresh(&fr, fr.probed - 10), "an entry from the FUTURE is not trusted — the clock went backwards");

    // Forgetting.
    check(rolltui_termcache_forget(path.c_str(), "k1") == 1 && !rolltui_termcache_load(path.c_str(), "k1", &r) &&
              rolltui_termcache_load(path.c_str(), "k2", &r),
          "forgetting one fingerprint leaves the others");
    check(rolltui_termcache_forget(path.c_str(), "nope") == 0, "…and forgetting a stranger says so");

    // Only the newest sixteen are kept.
    for (int i = 0; i < 30; ++i) {
      RolltuiTermCacheEntry x{};
      x.probed = 3000000 + i;
      x.responsive = 1;
      rolltui_termcache_store(path.c_str(), ("many" + std::to_string(i)).c_str(), &x);
    }
    int present = 0;
    for (int i = 0; i < 30; ++i) present += rolltui_termcache_load(path.c_str(), ("many" + std::to_string(i)).c_str(), &r);
    check(present == 16, "at most sixteen fingerprints are kept (" + std::to_string(present) + ")");
    check(rolltui_termcache_load(path.c_str(), "many29", &r) && !rolltui_termcache_load(path.c_str(), "many0", &r),
          "…and it is the OLDEST that go");

    // A damaged file is a miss and the next store replaces it.
    {
      FILE* f = std::fopen(path.c_str(), "wb");
      std::fputs("{ this is not json", f);
      std::fclose(f);
    }
    check(!rolltui_termcache_load(path.c_str(), "many29", &r), "a damaged file is a miss, not a crash");
    check(rolltui_termcache_store(path.c_str(), "fresh", &w) == 1 && rolltui_termcache_load(path.c_str(), "fresh", &r),
          "…and the next store replaces it");
    // A file that is JSON but not ours.
    {
      FILE* f = std::fopen(path.c_str(), "wb");
      std::fputs("{\"schema\": 99, \"entries\": {\"fresh\": {\"probed\": 5, \"responsive\": true}}}", f);
      std::fclose(f);
    }
    check(!rolltui_termcache_load(path.c_str(), "fresh", &r), "a file of another schema is not read");

    // With no directory to name, there is no path and nothing is written.
    check(rolltui_termcache_store("", "k", &w) == 0 && rolltui_termcache_load("", "k", &r) == 0, "no path: nothing is read and nothing is written");

    std::string rm = "rm -rf '" + dir + "'";
    (void)!std::system(rm.c_str());
  }

  return report("rolltui_termfacts_test");
}
