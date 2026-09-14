//
// dirk_test.cpp — `dirk`, the shell directory picker, driven headlessly.
//
// `dirk` is a read-only column-view file browser that prints the chosen path on stdout: its
// custom widget is the first in this repo with internal structure the library does not model.
// This suite runs the real binary against a FIXTURE TREE it builds under $TMPDIR — never the
// machine's own files, so a frame is a pure function of the fixture — and asserts what the app
// does, the CONTRACT its shell function reads (path + exit 0, or nothing + exit 1, and the two
// never blur), that the `init zsh` script parses and its `dirk` function actually changes
// directory, plus the two controls that make the screen's file-ness checkable.
//
// WHY THESE ARE CONTENT ASSERTIONS AND NOT BYTE GOLDENS, decided here with its reason: the
// details page renders a file's real size and modification time, which no fixture can make
// byte-stable across machines or checkouts. A golden would either exclude the app's own richest
// window or be re-recorded on every clone. The frames are asserted on what must be in them, and
// `studio_golden_test` keeps the byte-level guarantee for the library's own rendering.
//
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;
namespace fs = std::filesystem;

#ifndef DIRKTUI_BIN
#error "DIRKTUI_BIN must name the dirktui binary"
#endif
#ifndef ROLLTUI_EXAMPLES_DIR
#error "ROLLTUI_EXAMPLES_DIR must point at rolltui/examples"
#endif

namespace {

std::string run(const std::string& cmd, int& rc) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) { rc = -1; return out; }
  char buf[4096];
  while (std::size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
  rc = pclose(p);
  return out;
}

// The child's EXIT STATUS, which is what a shell's `||` reads. `pclose` returns a wait status,
// and `rc == 1` on one is never true — a test comparing it to 1 would pass on nothing.
int status_of(int rc) { return rc == -1 ? -1 : (WIFEXITED(rc) ? WEXITSTATUS(rc) : 128); }

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_file(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << s;
}

}  // namespace

int main() {
  const char* tmp = std::getenv("TMPDIR");
  const fs::path scratch = fs::path(tmp && *tmp ? tmp : "/tmp") / ("dirk-test-" + std::to_string(getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);

  // ---- the fixture tree: everything the widget has to survive drawing ----------------------
  const fs::path tree = scratch / "tree";
  write_file(tree / "alpha" / "one.txt", "one\n");
  write_file(tree / "alpha" / "two.txt", "two\n");
  write_file(tree / "alpha" / "nested" / "deep.txt", std::string(300, 'x'));
  fs::create_directories(tree / "beta");
  write_file(tree / "zeta.txt", "zeta\n");
  write_file(tree / ".hidden", "dot\n");
  write_file(tree / "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt", "wide\n");              // CJK: two cells a glyph
  write_file(tree / "cafe\xCC\x81.txt", "combining\n");                                  // e + U+0301
  write_file(tree / "a-very-long-name-that-will-not-fit-inside-one-column.txt", "long\n");
  std::filesystem::create_directories(tree / "empty-dir");

  const std::string bin = std::string("'") + DIRKTUI_BIN + "'";
  const std::string presets = std::string(" --presets '") + ROLLTUI_EXAMPLES_DIR + "/presets'";
  // EVERY RUN GETS ITS OWN CONFIG DIRECTORY. The app writes its settings file and its preset
  // stores under ROLL_CONFIG_DIR; without this, a test pressing Ctrl-S writes into the person's
  // home and the next run reads it back — which is how one assertion here failed once.
  const std::string home_env = "ROLL_CONFIG_DIR='" + (scratch / "home").string() + "' ";
  const std::string base = home_env + bin + " '" + tree.string() + "'" + presets + " --theme default-dark";

  // ---- 0. THE LIBRARY'S EDITORS ARE THIS APP'S TOO -----------------------------------------
  // A kind is the library's; a STORE is what makes it an app's. Before this app opened one, the
  // theme and keys editors existed and had nothing to edit here — "built into any rolltui app"
  // was true of the library and false of every app but two.
  {
    int erc = 0;
    const std::string th = run(home_env + bin + " '" + tree.string() + "' --frame 80x14 --keys \"F4\" 2>&1", erc);
    check(has(th, "theme editor") && has(th, "Roles"), "F4 opens the theme editor in dirktui");
    const std::string ke = run(home_env + bin + " '" + tree.string() + "' --frame 80x14 --keys \"F5\" 2>&1", erc);
    check(has(ke, "keys editor") && has(ke, "Actions by scope"), "F5 opens the keys editor in dirktui");
    check(!has(th, "keys editor") && !has(ke, "theme editor"), "…and each chord opens its own, not the other");
  }

  // ---- 0a. THE PRODUCT BINARY CANNOT TEST ITSELF -------------------------------------------
  // Additive, not compiled out: dirk-selftest is this same source plus the script
  // vocabulary, and the shipped binary simply does not contain it. Asserted on the ARTIFACT
  // rather than on the source, because what ships is a binary and that is what the claim is
  // about. TripleClick is a marker the vocabulary owns; a key NAME like PageDown would not
  // discriminate, since the library's own key table carries those and both binaries link it.
  {
    int prc = 0;
    const std::string product = std::string("'") + DIRKTUI_PRODUCT_BIN + "'";
    const std::string refused = run(product + " --frame 40x6 2>&1", prc);
    check(has(refused, "usage:") && !has(refused, "--frame"),
          "the shipped dirktui refuses --frame and does not advertise it");
    const std::string in_product = run("strings " + product + " | grep -cx TripleClick", prc);
    const std::string in_selftest = run("strings " + bin + " | grep -cx TripleClick", prc);
    check(in_product.substr(0, 1) == "0", "…and the script vocabulary is absent from the shipped binary [" + in_product.substr(0, 3) + "]");
    check(in_selftest.substr(0, 1) != "0", "…while the self-test binary has it, so the marker discriminates");
  }

  // ---- 0. it runs BARE, and a miss names every path it tried --------------------------------
  // The app keeps its screen in files rather than in its source, so with no --presets it has to
  // ask where its own files are. It used to build "/layouts/dirktui.json" from an empty string
  // and print "no layout ()" — a message naming neither what it wanted nor where it looked.
  {
    int brc = 0;
    const std::string bare = run(home_env + bin + " '" + tree.string() + "' --frame 70x10 2>&1", brc);
    check(!has(bare, "no layout") && !has(bare, "cannot load"),
          "dirktui runs with NO arguments: it finds its own embedded layout [" + bare.substr(0, 60) + "]");
    check(has(bare, "columns"), "…and draws its own screen, whose title lives only in its layout file");

    int mrc = 0;
    const std::string miss = run(home_env + bin + " '" + tree.string() + "' --presets '/nonexistent-xyz' --frame 40x6 2>&1", mrc);
    check(has(miss, "cannot load its layout"), "a miss says what it could not load [" + miss.substr(0, 50) + "]");
    check(has(miss, "tried:") && has(miss, "/nonexistent-xyz/layouts/dirktui.json"),
          "…and NAMES the path it tried, rather than an empty parenthesis");
  }

  // ---- 0b. A DATA ERROR IS NOT A CAPABILITY GAP --------------------------------------------
  // `problem()` answers "what does this kind NEED that the app has not provided", and it feeds
  // the end-of-init gap report, whose sentence is "this screen names N things this app must
  // provide". A mistyped path is DATA: the app provides `browser` perfectly well. Routing it
  // through that channel hands a person a message written for whoever builds the app.
  {
    int drc = 0;
    const std::string bad = tree.string() + "/no-such-dir-xyz";
    const std::string err = run(home_env + bin + " '" + bad + "' --frame 60x10 2>&1 1>/dev/null", drc);
    check(err.find("must provide") == std::string::npos,
          "a missing directory is NOT reported as a capability gap [" + err.substr(0, 60) + "]");
    const std::string shown = run(home_env + bin + " '" + bad + "' --frame 150x10 2>/dev/null", drc);
    check(has(shown, "cannot open " + bad),
          "…the panel says it cannot open the path IN FULL — an error is not a filename and gets the "
          "panel width, because a column sized for names truncates it to nothing useful");
    check(!has(shown, "(empty)"),
          "…and a directory that CANNOT BE OPENED does not look like an EMPTY one — both have no "
          "entries, and drawing the same thing for both is a wrong answer reporting itself as success");
    const std::string ok_empty = run(home_env + bin + " '" + (tree / "empty-dir").string() + "' --frame 60x10 2>/dev/null", drc);
    check(has(ok_empty, "(empty)"), "…while a genuinely empty directory still says so, so the two are distinguishable");
  }

  // ---- 1. it renders a directory ------------------------------------------------------------
  // ---- THE SHELL CONTRACT: what `$(dirk)` receives and what `|| return` sees -------------------
  // Everything the `init zsh` function relies on, asserted on the product's answer rather than on
  // a frame. "Nothing printed" and "exit 0" must never coincide: `cd ""` is `cd ~`, silently.
  {
    int crc = 0;
    const std::string here = tree.string();
    const std::string dir = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && dir == (tree / "alpha").string() + "\n",
          "Enter prints the selected directory, one line, exit 0 [" + dir.substr(0, 60) + "]");
    const std::string deeper = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"Right Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && deeper == (tree / "alpha" / "nested").string() + "\n",
          "…and Right then Enter prints the directory one column in — the path is the eye's, not the root's");
    // ENTER ON A FILE is a setting; every mode runs against a stand-in opener and clipboard that
    // RECORD what they were handed, so "opened" and "copied" are facts and not hopes.
    const fs::path stub = scratch / "stub";
    fs::create_directories(stub);
    const fs::path stublog = scratch / "stub.log";
    write_file(stub / "opener", "#!/bin/sh\nprintf 'open %s\\n' \"$1\" >> '" + stublog.string() + "'\n");
    write_file(stub / "clip", "#!/bin/sh\n{ printf 'clip '; cat; printf '\\n'; } >> '" + stublog.string() + "'\n");
    chmod((stub / "opener").c_str(), 0755);
    chmod((stub / "clip").c_str(), 0755);
    const std::string stubs = "DIRK_OPEN='" + (stub / "opener").string() + "' DIRK_CLIPBOARD='" + (stub / "clip").string() + "' ";
    auto stublines = [&]() { bool ok = false; const std::string t = read_file(stublog.string(), ok); fs::remove(stublog); return ok ? t : std::string(); };
    auto with_setting = [&](const std::string& json) {
      const fs::path cfg = scratch / "file-enter-cfg";
      fs::create_directories(cfg / "rolltui" / "dirktui");
      write_file(cfg / "rolltui" / "dirktui" / "settings.json", json);
      return "ROLL_CONFIG_DIR='" + cfg.string() + "' " + stubs + bin + " '" + here + "'" + presets + " --theme default-dark";
    };
    // THE CONTROL FIRST: a headless run with NO stand-in named reaches no real opener — it says so
    // on the status line and the log stays empty — so a test can never put a window on a screen.
    const std::string bare_env = "ROLL_CONFIG_DIR='" + (scratch / "file-enter-cfg").string() + "' ";
    fs::create_directories(scratch / "file-enter-cfg" / "rolltui" / "dirktui");
    write_file(scratch / "file-enter-cfg" / "rolltui" / "dirktui" / "settings.json", "{}");
    const std::string unopened = run(bare_env + bin + " '" + here + "'" + presets + " --theme default-dark --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    check(has(unopened, "could not open") && stublines().empty(),
          "a headless run with no stand-in opener opens NOTHING and says so — the control that keeps a test off the screen");
    const std::string stay = run(with_setting("{}") + " --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    const std::string stay_log = stublines();
    check(status_of(crc) == 0 && has(stay, "columns") && has(stay_log, "open ") && has(stay_log, ".txt"),
          "Enter on a file, by default, OPENS it and stays: a frame is drawn and the opener was handed the file [" +
              stay_log.substr(0, 40) + "]");
    const std::string leave = run(with_setting("{ \"file_enter\": \"open_leave\" }") + " --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    const std::string leave_log = stublines();
    check(status_of(crc) == 1 && leave.empty() && has(leave_log, "open "), "…`open_leave` opens it and leaves with nothing printed, exit 1");
    const std::string parent = run(with_setting("{ \"file_enter\": \"parent\" }") + " --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    const std::string parent_path = parent.empty() ? parent : parent.substr(0, parent.size() - 1);
    check(status_of(crc) == 0 && fs::is_regular_file(parent_path) && stublines().empty(),
          "…`parent` leaves with the file's path and exit 0 — the shell lands in its folder — and opens nothing");
    const std::string insert = run(with_setting("{ \"file_enter\": \"insert\" }") + " --frame 80x20 --keys \"Right End Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && insert.rfind("./alpha/", 0) == 0,
          "…`insert` leaves with the path RELATIVE to where dirk started and exit 3: the verb is the status [" + insert + "]");

    // COPY: `c` puts the path on the clipboard, absolute by default; the Option chord inverts
    // the setting, and the setting inverts the chord.
    run(with_setting("{}") + " --frame 80x20 --keys \"Right End c\" >/dev/null 2>&1", crc);
    const std::string abs_log = stublines();
    check(abs_log.rfind("clip /", 0) == 0 && has(abs_log, "/alpha/"), "c copies the absolute path [" + abs_log.substr(0, 30) + "]");
    run(with_setting("{}") + " --frame 80x20 --keys \"Right End AltC\" >/dev/null 2>&1", crc);
    check(stublines().rfind("clip ./alpha/", 0) == 0, "…Alt-c copies it relative to where dirk started: the inverse of the setting");
    run(with_setting("{ \"copy_path\": \"relative\" }") + " --frame 80x20 --keys \"Right End c\" >/dev/null 2>&1", crc);
    check(stublines().rfind("clip ./alpha/", 0) == 0, "…with the setting on relative, c copies relative");
    run(with_setting("{ \"copy_path\": \"relative\" }") + " --frame 80x20 --keys \"Right End AltC\" >/dev/null 2>&1", crc);
    check(stublines().rfind("clip /", 0) == 0, "…and Alt-c then copies the absolute path");
    const std::string empty = run(home_env + bin + " '" + (tree / "empty-dir").string() + "' --frame 80x20 --keys \"Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && empty == (tree / "empty-dir").string() + "\n",
          "Enter in an EMPTY directory prints that directory: the eye is on it and there is nothing else to choose");
    for (const char* cancel : {"Escape", "CtrlQ", "CtrlC"}) {
      const std::string none = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"" + cancel + "\" 2>/dev/null", crc);
      check(status_of(crc) == 1 && none.empty(),
            std::string(cancel) + " prints NOTHING and exits 1 — the shell function's `|| return` needs both");
    }
    const std::string popup = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"F1 Escape\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(popup, "alpha") && !has(popup, "the live key table"),
          "…while Escape over a POPUP closes the popup and the session goes on — cancel is the browser's key, not a global one");
    const std::string typing = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"CtrlG Type:abc Escape\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(typing, "abc"),
          "…and Escape in the path input stays the input's, so typing a path is never one key from leaving");
  }

  // ---- `dirktui init <shell>`: the shell side, parsed by each shell and RUN through a stand-in ----
  // The Right Arrow bindings need a line editor and are not exercised here; the `dirk` function
  // is, in zsh and bash, with `command dirktui` resolved to a script on PATH that answers what the
  // real binary would, and `$DIRK_OPEN` resolved to a script that RECORDS what it was asked to open.
  {
    int irc = 0;
    const std::string product = std::string("'") + DIRKTUI_PRODUCT_BIN + "'";
    const std::string other = run(product + " init nushell 2>&1", irc);
    check(status_of(irc) == 2 && has(other, "usage: dirktui init zsh|bash|fish"),
          "a shell it has no script for is refused with the list it has, never answered with another's");

    const fs::path fake = scratch / "fake";
    fs::create_directories(fake);
    write_file(tree / "alpha" / "run.sh", "#!/bin/sh\necho hi\n");
    chmod((tree / "alpha" / "run.sh").c_str(), 0755);
    // The stand-in: `FAKE=dir` answers a directory, `FAKE=file` a plain file, `FAKE=exe` an
    // executable file, anything else cancels (exit 1, nothing printed) — the answers the real
    // binary can give.
    write_file(fake / "dirktui", "#!/bin/sh\ncase \"$FAKE\" in\n  dir) printf '%s\\n' '" + (tree / "alpha").string() +
                                     "';;\n  file) printf '%s\\n' '" + (tree / "alpha" / "one.txt").string() +
                                     "';;\n  exe) printf '%s\\n' '" + (tree / "alpha" / "run.sh").string() +
                                     "';;\n  insert) printf './alpha/one.txt\\n'; exit 3;;\n  *) exit 1;;\nesac\n");
    chmod((fake / "dirktui").c_str(), 0755);
    const fs::path opened = scratch / "opened.log";
    write_file(fake / "openlog", "#!/bin/sh\nprintf '%s\\n' \"$1\" >> '" + opened.string() + "'\n");
    chmod((fake / "openlog").c_str(), 0755);

    struct Shell { const char* name; const char* source; };
    for (const Shell& sh : {Shell{"zsh", "source"}, Shell{"bash", "source"}}) {
      const std::string script = run(product + " init " + sh.name + " 2>/dev/null", irc);
      check(status_of(irc) == 0 && has(script, "_dirk_forward_char") && has(script, "_dirk_command") &&
                has(script, "command dirktui"),
            std::string("`dirktui init ") + sh.name + "` prints the wrapper, the composed command, and the Right Arrow binding");
      const fs::path init = scratch / (std::string("init.") + sh.name);
      write_file(init, script);
      run(std::string(sh.name) + " -n '" + init.string() + "' 2>&1", irc);
      check(status_of(irc) == 0, std::string("…and ") + sh.name + " -n accepts the script it printed");
      fs::remove(opened);
      const std::string drive = "PATH='" + fake.string() + "':\"$PATH\" DIRK_OPEN='" + (fake / "openlog").string() + "' " +
                                sh.name + " -c '" + sh.source + " \"" + init.string() + "\"; " +
                                "cd /; FAKE=dir dirk; echo \"dir=$? $PWD\"; " +
                                "cd /; FAKE=file dirk >/dev/null; echo \"file=$? $PWD\"; " +
                                "cd /; FAKE=exe dirk >/dev/null; echo \"exe=$? $PWD\"; " +
                                "cd /; FAKE=insert dirk; echo \"insert=$? $PWD\"; " +
                                "cd /; FAKE=no dirk; echo \"cancel=$? $PWD\"' 2>&1";
      const std::string drove = run(drive, irc);
      const std::string alpha = (tree / "alpha").string();
      check(has(drove, "dir=0 " + alpha + "\n"),
            std::string(sh.name) + ": the `dirk` function cd's to the folder the binary printed [" + drove.substr(0, 90) + "]");
      check(has(drove, "file=0 " + alpha + "\n"), std::string(sh.name) + ": a chosen FILE lands in its folder");
      check(has(drove, "exe=0 " + alpha + "\n"), std::string(sh.name) + ": so does an executable file");
      check(has(drove, "cancel=1 /\n"), std::string(sh.name) + ": a cancel changes nothing and returns 1");
      check(has(drove, "insert=0 /\n") && has(drove, "./alpha/one.txt"),
            std::string(sh.name) + ": exit 3 changes no directory and hands the path to the line (or shows it) [" +
                drove.substr(drove.find("insert="), 30) + "]");
      check(!fs::exists(opened), std::string(sh.name) + ": the shell side opens NOTHING any more — opening is the binary's");
    }

    // fish is not on every machine. When it is, its script must parse; when it is not, the script
    // is still asserted to exist — a check that silently did nothing would look like a pass.
    const std::string fish = run(product + " init fish 2>/dev/null", irc);
    check(status_of(irc) == 0 && has(fish, "function dirk") && has(fish, "_dirk_forward_char"),
          "`dirktui init fish` prints the wrapper and the Right Arrow binding");
    const std::string have_fish = run("command -v fish 2>/dev/null", irc);
    if (!have_fish.empty()) {
      const fs::path init = scratch / "init.fish";
      write_file(init, fish);
      run("fish -n '" + init.string() + "' 2>&1", irc);
      check(status_of(irc) == 0, "…and fish -n accepts the script it printed");
    } else {
      std::fprintf(stderr, "  [SKIP] fish is not installed here, so its script was not parsed\n");
    }
  }

  int rc = 0;
  const std::string wide = run(base + " --frame 150x30 2>&1", rc);
  check(rc == 0 && !wide.empty(), "dirktui renders a directory headlessly (rc " + std::to_string(rc) + ")");
  check(has(wide, "alpha") && has(wide, "beta") && has(wide, "zeta.txt"),
        "…the first column lists the fixture's entries");
  check(has(wide, "one.txt") || has(wide, "two.txt"),
        "…and the column to its RIGHT is already filled from the selection — the column view's whole rule");
  check(!has(wide, ".hidden"), "a dotfile is not shown until it is asked for");
  {
    const std::string roomy = run(base + " --frame 220x30 2>&1", rc);
    check(rc == 0 && has(roomy, "1 hidden"),
          "…and the widget SAYS how many it is holding back, through the plugin's `note_at` slot");
  }

  // ---- 2. the selection propagates sideways, which is the widget's argument ------------------
  const std::string into = run(base + " --frame 150x30 --keys \"Right Down\" 2>&1", rc);
  check(rc == 0 && has(into, "one.txt") && has(into, "two.txt"),
        "Right enters the selected directory and Down moves inside it");
  // alpha holds `nested/`, `one.txt`, `two.txt` — directories first, so Right Right walks
  // root → alpha → nested and the third column is the deepest one.
  const std::string deep = run(base + " --frame 150x30 --keys \"Right Right\" 2>&1", rc);
  check(rc == 0 && has(deep, "deep.txt"), "…and a third column opens from the second's selection");
  check(has(deep, "column 3/3"), "…the status line counts the columns it is showing");

  // ---- 3. wide, combining and over-long names ------------------------------------------------
  // ---- THE ANCHOR RULE: the focused column and its whole preview are always on screen ----------
  // Wide enough for the root column (28 cells: the long name) and one preview, not for three: after Right the focus is `alpha` and the column
  // the selection fills (`nested`) must END at the right edge, with the root column shown
  // partially on the left — so its title, which starts at its left edge, is off screen.
  {
    const std::string narrow = run(base + " --frame 46x10 --keys \"Right\" 2>&1", rc);
    check(rc == 0 && has(narrow, "nested") && has(narrow, "deep.txt"),
          "Right opens the NEW focus's preview at once, so the column to the right is never empty");
    check(!has(narrow, "\xE2\x94\x82 tree"),
          "…and the root column is scrolled partly off the left edge to make room for it");
    const std::string roomy = run(base + " --frame 150x30 --keys \"Right\" 2>&1", rc);
    check(has(roomy, "\xE2\x94\x82 tree"),
          "…while a window that fits every column packs them from the left and scrolls nothing");
    const std::string back = run(base + " --frame 46x10 --keys \"Right Left\" 2>&1", rc);
    check(rc == 0 && has(back, "\xE2\x94\x82 tree") && has(back, "alpha"),
          "Left slides the columns back so the root column is whole again");
  }

  // ---- MOTION: this app's own states and kinds, mapped by ITS file, read at the script's clock ----
  // A text frame cannot show a colour, so the self-test binary prints what the frame's effects
  // touched. The states are dirktui's (`dirk.folder`, `dirk.file`, `dirk.dig`), registered on the
  // session; the kinds are dirktui's; the mapping is `examples/presets/effects/dirktui.json`.
  {
    auto fx = [&](const std::string& keys) {
      int frc = 0;
      const std::string err = run(base + " --frame 46x10 --keys \"" + keys + "\" 2>&1 >/dev/null", frc);
      const std::size_t at = err.find("effects: ");
      return at == std::string::npos ? std::string("(no effects line)") : err.substr(at, err.find('\n', at) - at);
    };
    check(has(fx("Tick:0"), "marks=1 drawn=1"), "the folder under the cursor is marked and its effect draws [" + fx("Tick:0") + "]");
    check(fx("Tick:600") != fx("Tick:1200") && has(fx("Tick:1200"), "drawn=1"),
          "…and it BREATHES: a later moment lights a different number of cells [" + fx("Tick:600") + " / " + fx("Tick:1200") + "]");
    auto num = [](const std::string& line, const char* key) {
      const std::size_t at = line.find(key);
      return at == std::string::npos ? -1 : std::atoi(line.c_str() + at + std::strlen(key));
    };
    const std::string land = fx("End Tick:0"), half = fx("End Tick:175");
    check(num(land, "marks=") == 1 && num(land, "drawn=") == 1 && num(land, "cells=") > 0,
          "a file under the cursor lands lit from end to end [" + land + "]");
    check(num(half, "drawn=") == 1 && num(half, "cells=") > 0 && num(half, "cells=") < num(land, "cells="),
          "…half-way through it has handed part of the row back [" + half + "]");
    check(has(fx("End Tick:400"), "marks=1 drawn=0"), "…settled, the row is still but the moment is still marked");
    check(has(fx("End Tick:700"), "marks=0"), "…and once landed the file's mark is gone, so the tick stops asking for frames");
    const std::string dig = fx("Right Tick:100"), past = fx("Right Tick:400"), still = fx("Right");
    check(num(dig, "marks=") >= 3 && num(dig, "drawn=") == num(dig, "marks="),
          "Right marks every row of the column it dug into, and the sweep is crossing them all [" + dig + "]");
    check(num(past, "marks=") == num(dig, "marks=") && num(past, "drawn=") == 1,
          "…the sweep is one pass: past its period the rows draw nothing and only the cursor breathes [" + past + "]");
    check(has(fx("Right Tick:600"), "marks=1"), "…and the dig's marks leave, so only the cursor's remains");
    check(num(still, "marks=") == num(dig, "marks=") && num(still, "drawn=") == 0,
          "a script with no tick is the still picture: marked, every effect at its first instant [" + still + "]");

    // THE MAPPING IS A FILE, and a person's config directory shadows the embedded one: an empty
    // mapping there leaves the marks with nothing to draw, and a state nobody registered is
    // reported by name where a developer is looking.
    const fs::path cfg = scratch / "cfg";
    fs::create_directories(cfg / "rolltui" / "dirktui");
    write_file(cfg / "rolltui" / "dirktui" / "effects.json", "{ \"effects\": {} }\n");
    int crc = 0;
    const std::string shadowed = run("ROLL_CONFIG_DIR='" + cfg.string() + "' " + bin + " '" + tree.string() + "'" + presets + " --theme default-dark --frame 46x10 --keys \"Tick:0\" 2>&1 >/dev/null", crc);
    check(has(shadowed, "marks=1 drawn=0"), "a user's own effects file shadows the app's: the mark is there, the theme has nothing for it [" +
                                                shadowed.substr(0, 60) + "]");
    write_file(cfg / "rolltui" / "dirktui" / "effects.json", "{ \"effects\": { \"dirk.nosuch\": { \"kind\": \"blink\" } } }\n");
    const std::string unknown = run("ROLL_CONFIG_DIR='" + cfg.string() + "' " + bin + " '" + tree.string() + "'" + presets + " --theme default-dark --frame 46x10 2>&1 >/dev/null", crc);
    check(has(unknown, "effects file") && has(unknown, "dirk.nosuch"), "…and a state the app never registered is named as unknown");
  }

  // ---- THE SETTINGS MENU: a file, driven by the host, and a settings file the choices land in ----
  // F2 opens `menu:places`; its items are the app's three settings and a jump. A toggle writes
  // `<config>/rolltui/dirktui/settings.json`, the next run reads it, and the box reads back the
  // way the app behaves. Motion off is a STILL app: the marks stay, nothing draws, the slide snaps.
  {
    const fs::path cfg = scratch / "settings-cfg";
    const std::string env = "ROLL_CONFIG_DIR='" + cfg.string() + "' ";
    int mrc = 0;
    const std::string sbase = env + bin + " '" + tree.string() + "'" + presets + " --theme default-dark";
    const std::string opened = run(sbase + " --frame 60x14 --keys \"F2\" 2>/dev/null", mrc);
    check(has(opened, "settings") && has(opened, "[x] Motion") && has(opened, "[ ] Show dotfiles") && has(opened, "Sort by"),
          "F2 opens the settings menu, its boxes set from the live values (motion on, dotfiles off)");
    run(sbase + " --frame 60x14 --keys \"F2 Down Down Enter\" >/dev/null 2>&1", mrc);
    bool ok = false;
    const std::string saved = read_file((cfg / "rolltui" / "dirktui" / "settings.json").string(), ok);
    check(ok && has(saved, "\"motion\": false"), "toggling Motion writes the settings file [" + saved.substr(0, 60) + "]");
    const std::string still = run(sbase + " --frame 46x10 --keys \"Tick:0\" 2>&1 >/dev/null", mrc);
    check(has(still, "marks=1 drawn=0"), "…the next run reads it: the cursor is marked and nothing draws");
    const std::string snapped = run(sbase + " --frame 46x10 --keys \"Right Tick:30\" 2>/dev/null", mrc);
    const std::string ended = run(sbase + " --frame 46x10 --keys \"Right Tick:200\" 2>/dev/null", mrc);
    check(snapped == ended, "…and with motion off the columns do not slide, they are simply there");
    const std::string reopened = run(sbase + " --frame 60x14 --keys \"F2\" 2>/dev/null", mrc);
    check(has(reopened, "[ ] Motion"), "…and the box reads back unchecked");
    run(sbase + " --frame 60x14 --keys \"F2 Enter Down Enter\" >/dev/null 2>&1", mrc);
    const std::string sorted = read_file((cfg / "rolltui" / "dirktui" / "settings.json").string(), ok);
    check(ok && has(sorted, "\"sort\": \"size\""), "a sort chosen in the menu is saved too [" + sorted.substr(0, 60) + "]");
    const std::string parent = run(env + bin + " '" + (tree / "alpha").string() + "'" + presets + " --theme default-dark --frame 60x12 --keys \"F2 End Enter\" 2>/dev/null", mrc);
    check(has(parent, "\xE2\x94\x82 tree"), "\"Go to the parent\" re-roots one level up");
  }

  // ---- THE SLIDE, at the script's clock: a moment into it the columns are between ---------------
  {
    auto row = [&](const std::string& keys) {
      int src = 0;
      const std::string out = run(base + " --frame 46x10 --keys \"" + keys + "\" 2>/dev/null", src);
      std::istringstream in(out);
      std::string l0, l1;
      std::getline(in, l0);
      std::getline(in, l1);
      return l1;
    };
    const std::size_t at0 = row("Right Tick:0").find("alpha"), at30 = row("Right Tick:30").find("alpha"),
                      done = row("Right Tick:200").find("alpha"), still = row("Right").find("alpha");
    check(at0 != std::string::npos && done != std::string::npos && done < at0,
          "the instant Right is pressed the columns are where they were; later they have moved left");
    check(at30 != std::string::npos && done < at30 && at30 < at0,
          "…and 30 ms in they are BETWEEN: the slide is a slide, not a jump [" + std::to_string(at0) + " > " +
              std::to_string(at30) + " > " + std::to_string(done) + "]");
    check(still == done, "…while a script with no tick draws the slide already ended");
  }

  check(has(wide, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"), "a CJK name is drawn, and the row it is on is not torn");
  check(has(wide, "cafe\xCC\x81") || has(wide, "caf"), "a combining sequence survives the column");
  check(has(wide, "\xE2\x80\xA6"), "a name too long for its column is cut with an ellipsis, not clipped silently");

  // ---- 4. the details page — a POPUP THE LAYOUT DECLARES, filled by a rows source ------------
  const std::string details = run(base + " --frame 150x30 --keys \"CtrlD\" 2>&1", rc);
  check(rc == 0 && has(details, "kind") && has(details, "directory"),
        "Ctrl-D opens the details page the layout declares, and it names the selection's kind");
  check(has(details, "modified") && has(details, "mode"), "…with the modified time and the permissions");

  // ---- 5. the path entry, and a bad path as a NAMED problem ----------------------------------
  const std::string jump = run(base + " --frame 150x30 --keys \"CtrlG Type:" + (tree / "alpha").string() +
                                   " Enter\" 2>&1",
                               rc);
  check(rc == 0 && has(jump, "one.txt"), "a path typed into the input jumps there");
  const std::string bad = run(base + " --frame 150x30 --keys \"CtrlG Type:/no/such/place Enter\" 2>&1", rc);
  check(rc == 0 && has(bad, "no such path"),
        "a bad path is a NAMED problem on the screen — never a crash and never silence");

  // ---- 6. dotfiles and the sort order are the app's, driven from the bindings FILE ------------
  const std::string dots = run(base + " --frame 150x30 --keys \"AltH\" 2>&1", rc);
  check(rc == 0 && has(dots, ".hidden"), "Alt-H shows the dotfiles");
  const std::string sorted = run(base + " --frame 150x30 --keys \"CtrlS\" 2>&1", rc);
  check(rc == 0 && has(sorted, "size"), "Ctrl-S cycles the sort order and the status line says which");

  // ---- 7. THE STANDING RULE: every view shrinks to nothing gracefully ------------------------
  for (const char* size : {"1x1", "2x1", "8x3", "40x2", "100x1"}) {
    int src = 0;
    run(base + " --frame " + size + " > /dev/null 2>&1", src);
    check(src == 0, std::string("a ") + size + " frame is drawn without crashing");
  }

  // ---- 8. CONTROL: the screen's window ids and titles are in NO source -----------------------
  // Narrower than `files_only_test`'s, and deliberately so: that test runs the STUDIO, a host
  // that has never heard of its fixture screen, so one token covers everything. This is a host
  // with its own screen, and it must name what it OPENS (`details`, `help`) and what it BINDS
  // (`browser`, `tree`, `entry`, `path`) exactly as paint names its canvas. What it must never
  // name is the base window ids and the titles — those come only from the layout file, and this
  // grep is what says so.
  {
    std::vector<std::string> hits;
    int scanned = 0;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
      const std::string p = e.path().string();
      const std::string rel = p.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
      if (rel.rfind("tests/", 0) == 0 || rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0 ||
          rel.rfind("presets/", 0) == 0 || rel.rfind("examples/presets/", 0) == 0)
        continue;
      const std::string ext = e.path().extension().string();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
      ++scanned;
      bool ok = false;
      const std::string text = read_file(p, ok);
      if (!ok) continue;
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        // `"where"` is deliberately NOT in this list, and its absence is wall 4's evidence:
        // `rolltui_window_stack_focus` takes an ID, so `app.jump` must name the layout's input
        // window in the source. Every other id and every title stays in the file.
        for (const char* w : {"\"columns\"", "go to", "the live key table", "the vestibule", "Go to the parent", "Show dotfiles"})
          if (line.find(w) != std::string::npos) hits.push_back(rel + ":" + std::to_string(ln) + ": " + w);
      }
    }
    check(scanned >= 30, "scanned every source dirktui is built from (" + std::to_string(scanned) + " files)");
    check(hits.empty(), "no source names this screen's window ids or titles — they exist only in the layout file" +
                            (hits.empty() ? "" : " [" + hits.front() + "]"));
  }

  // ---- 9. CONTROL: a screen written AFTER the build opens in it ------------------------------
  // The stronger half of the same property, and the one a single-host app can still prove: a
  // layout this binary has never seen, with ids and titles no source contains, renders.
  {
    const fs::path own = scratch / "own";
    fs::create_directories(own / "layouts");
    fs::create_directories(own / "bindings");
    fs::copy_file(fs::path(ROLLTUI_EXAMPLES_DIR) / "presets" / "bindings" / "default.json",
                  own / "bindings" / "default.json", fs::copy_options::overwrite_existing);
    write_file(own / "layouts" / "vestibule.json", R"({
  "name": "vestibule", "min_width": 10, "min_height": 4, "focus": "vestibule_cols",
  "actions": { "app.quit": "leave", "browser.down": "down", "browser.into": "in" },
  "root": { "column": [
    { "id": "vestibule_note", "content": "text:  a screen this binary has never seen", "size": 1 },
    { "id": "vestibule_cols", "content": "browser:tree", "border": "single",
      "title": "the vestibule", "focusable": true } ] }
})");
    int vrc = 0;
    const std::string v = run(home_env + bin + " '" + tree.string() + "' --presets '" + own.string() +
                                  "' --layout vestibule --theme default-dark --frame 90x20 2>&1",
                              vrc);
    check(vrc == 0 && has(v, "the vestibule") && has(v, "never seen") && has(v, "alpha"),
          "a layout written after the build opens in the app, with its own ids and title");
  }

  fs::remove_all(scratch);
  return report("rolltui_dirktui_test");
}
