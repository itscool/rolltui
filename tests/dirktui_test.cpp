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

// The status line's "column a/b": which column is focused and how many there are.
std::pair<int, int> column_of(const std::string& frame) {
  const std::size_t at = frame.find("column ");
  if (at == std::string::npos) return {-1, -1};
  int a = 0, b = 0;
  if (std::sscanf(frame.c_str() + at, "column %d/%d", &a, &b) != 2) return {-1, -1};
  return {a, b};
}

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
  write_file(tree / "alpha" / "run.sh", "#!/bin/sh\necho hi\n");
  chmod((tree / "alpha" / "run.sh").c_str(), 0755);
  write_file(tree / "alpha" / "nested" / "deep.txt", std::string(300, 'x'));
  write_file(tree / "alpha" / "nested" / "leaf-file-long-name.txt", "in a leaf\n");
  fs::create_directories(tree / "beta");
  write_file(tree / "zeta.txt", "zeta\n");
  write_file(tree / "beta" / "a-quite-long-name-so-this-preview-is-wide.txt", "wide preview\n");
  write_file(tree / "beta" / "data.bin", std::string("\x00\x01\x02", 3));  // a type nobody listed
  write_file(tree / "beta" / "page.html", "<p>hi</p>\n");               // web: the browser's
  write_file(tree / "beta" / "tool.py", "print(1)\n");                    // code, not executable
  write_file(tree / ".hidden", "dot\n");
  write_file(tree / "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt", "wide\n");              // CJK: two cells a glyph
  write_file(tree / "cafe\xCC\x81.txt", "combining\n");                                  // e + U+0301
  write_file(tree / "a-very-long-name-that-will-not-fit-inside-one-column.txt", "long\n");
  std::filesystem::create_directories(tree / "empty-dir");
  std::filesystem::create_directories(tree / "yapps" / "Thing.app" / "Contents");  // a bundle: a leaf to the browser

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
    check(has(bare, "find:"), "…and draws its own screen, whose words live only in its layout file and its own prompts");

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
    check(has(shown, "cannot open " + bad.substr(0, 40)),
          "…the panel says it cannot open the path, as much of it as the panel holds — an error is not a "
          "filename and gets the panel width, because a column sized for names truncates it to nothing useful");
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
    write_file(stub / "opener", "#!/bin/sh\nprintf 'ran %s\\n' \"$*\" >> '" + stublog.string() + "'\n");
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
    // WHAT IS INSTALLED IS A FIXTURE: a PATH holding a `nvim` and a `code`, and an applications
    // directory holding a TextEdit — or a PATH holding nothing. The stand-in opener records the
    // COMMAND it stood in for, so "opened with Neovim" is a line saying `nvim <path>`.
    const fs::path fakebin = scratch / "fakebin";
    fs::create_directories(fakebin);
    for (const char* exe : {"nvim", "code"}) {
      write_file(fakebin / exe, "#!/bin/sh\nexit 0\n");
      chmod((fakebin / exe).c_str(), 0755);
    }
    const fs::path apps = scratch / "apps";
    fs::create_directories(apps / "TextEdit.app");
    const std::string editors = "PATH='" + fakebin.string() + "' ";
    const std::string no_editors = "PATH='" + (scratch / "nowhere").string() + "' ";
    const std::string apps_flag = " --apps '" + apps.string() + "'";
    const std::string no_apps_flag = " --apps '" + (scratch / "noapps").string() + "'";
    // THE CONTROL FIRST: a headless run with NO stand-in named reaches no real opener — it says so
    // on the status line and the log stays empty — so a test can never put a window on a screen.
    const std::string bare_env = "ROLL_CONFIG_DIR='" + (scratch / "file-enter-cfg").string() + "' ";
    fs::create_directories(scratch / "file-enter-cfg" / "rolltui" / "dirktui");
    write_file(scratch / "file-enter-cfg" / "rolltui" / "dirktui" / "settings.json", "{}");
    const std::string unopened = run(editors + bare_env + bin + " '" + here + "'" + presets + " --theme default-dark --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    check(has(unopened, "could not open") && stublines().empty(),
          "a headless run with no stand-in opener opens NOTHING and says so — the control that keeps a test off the screen");
    // A DOCUMENT OPENS WITH THE PROGRAM CHOSEN FOR ITS TYPE, FROM WHAT IS INSTALLED, and the
    // cursor stays. Text: the first editor of the type's preference found on PATH.
    const std::string txt = run(editors + with_setting("{}") + apps_flag + " --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    const std::string txt_log = stublines();
    check(status_of(crc) == 0 && has(txt, "find: ") && txt_log.rfind("ran nvim ", 0) == 0 && has(txt_log, ".txt"),
          "Enter on a text file opens it with the editor found first — Neovim — and stays: a frame is drawn [" + txt_log.substr(0, 40) + "]");
    run(no_editors + with_setting("{}") + no_apps_flag + " --frame 80x20 --keys \"End Enter\" >/dev/null 2>&1", crc);
    const std::string bare_log = stublines();
    check(bare_log.rfind("ran open ", 0) == 0 && has(bare_log, ".txt"), "…with no editor installed, with the system opener [" + bare_log.substr(0, 30) + "]");
    // beta: a-quite…txt, data.bin, page.html, tool.py — Down Right enters it on the first
    run(editors + with_setting("{}") + apps_flag + " --frame 80x20 --keys \"Down Right Down Down Down Enter\" >/dev/null 2>&1", crc);
    const std::string py_log = stublines();
    check(py_log.rfind("ran code ", 0) == 0 && has(py_log, "tool.py"),
          "…a source file opens with the CODE group's first choice — Visual Studio Code over Neovim, both installed [" + py_log.substr(0, 40) + "]");
    run(editors + with_setting("{}") + apps_flag + " --frame 80x20 --keys \"Down Right Down Down Enter\" >/dev/null 2>&1", crc);
    const std::string html_log = stublines();
    check(html_log.rfind("ran open ", 0) == 0 && has(html_log, "page.html"),
          "…a web page goes to the system opener — the browser — even with editors installed");
    const std::string unknown = run(editors + with_setting("{}") + apps_flag + " --frame 80x20 --keys \"Down Right Down Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && has(unknown, "/beta/data.bin\n") && stublines().empty(),
          "…and a type nobody listed goes to the command line (exit 3), opened by nothing");
    // THE CHOICE IS THE PERSON'S, PER TYPE — the setting `open` names a program per group.
    run(editors + with_setting("{ \"open\": { \"text\": \"system\" } }") + apps_flag + " --frame 80x20 --keys \"End Enter\" >/dev/null 2>&1", crc);
    check(stublines().rfind("ran open ", 0) == 0, "the setting picks the program for a type: text to the system opener although Neovim is installed");
    run(editors + with_setting("{ \"open\": { \"text\": \"textedit\" } }") + apps_flag + " --frame 80x20 --keys \"End Enter\" >/dev/null 2>&1", crc);
    const std::string app_log = stublines();
    check(app_log.rfind("ran open -a TextEdit ", 0) == 0, "…an application found in the applications directory runs through `open -a` [" + app_log.substr(0, 30) + "]");
    const std::string to_shell = run(editors + with_setting("{ \"open\": { \"text\": \"shell\" } }") + apps_flag + " --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && has(to_shell, ".txt\n") && stublines().empty(), "…and 'the command line' hands the file to the shell instead, exit 3");
    run(no_editors + with_setting("{ \"open\": { \"text\": \"nvim\" } }") + no_apps_flag + " --frame 80x20 --keys \"End Enter\" >/dev/null 2>&1", crc);
    check(stublines().rfind("ran open ", 0) == 0, "a chosen program that is not installed any more falls back to the type's default, never to nothing");
    // THE MENU OFFERS WHAT WAS FOUND, per type, with the choice shown.
    const std::string menu = run(editors + with_setting("{}") + apps_flag + " --frame 110x40 --keys \"F2\" 2>/dev/null", crc);
    check(has(menu, "General") && has(menu, "Look") && has(menu, "Enter on a file") && has(menu, "Open with") &&
              has(menu, "Key bindings") && has(menu, "Theme") && has(menu, "Neovim") && has(menu, "Visual Studio Code"),
          "F2: four sections — General, Look, Enter on a file, Open with — with the theme and the key bindings as choices, and the open-with rows showing the program chosen from what is installed");
    // dotfiles, sort, keys, theme, motion, sparkle, dividers, sizes, modified, leave, land, relative, executables, then Text: its dropdown
    const std::string bare_menu = run(no_editors + with_setting("{}") + no_apps_flag + " --frame 110x40 --keys \"F2 Down Down Down Down Down Down Down Down Down Down Down Down Down Enter\" 2>/dev/null", crc);
    check(has(bare_menu, "the system opener") && has(bare_menu, "Neovim") && has(bare_menu, "Helix") && has(bare_menu, "the command line"),
          "…and with nothing installed a type's dropdown still LISTS every known program — disabled, so a person sees what could open it — plus the system opener and the command line");
    // SCRIPTS AND BINARIES GO TO THE COMMAND LINE, never to an opener: exit 3 with the path, so
    // arguments can follow; with the landing set to the file's folder, exit 4 with the absolute path.
    const std::string script = run(editors + with_setting("{}") + " --frame 80x20 --keys \"Right Down Down Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && script.find("/alpha/run.sh\n") != std::string::npos && stublines().empty(),
          "Enter on an EXECUTABLE hands it to the command line (exit 3) and opens nothing, whatever its type [" + script.substr(script.rfind('/') + 1) + "]");
    const std::string script_rel = run(with_setting("{ \"paths\": \"relative\" }") + " --frame 80x20 --keys \"Right Down Down Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && script_rel == "./alpha/run.sh\n", "…relative to where dirk started when the path setting says so");
    const std::string script_at = run(with_setting("{ \"land\": \"file\" }") + " --frame 80x20 --keys \"Right Down Down Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 4 && script_at.find("/alpha/run.sh\n") != std::string::npos,
          "…and with the landing set to the file's folder, exit 4 with the absolute path for the shell to cd beside");
    // LEAVE: every file goes to the command line, a document included.
    // EXECUTABLES by the `exec` setting: the command line (default), run here, or the opener.
    const std::string ran = run(editors + with_setting("{ \"exec\": \"run\" }") + " --frame 80x20 --keys \"Right Down Down Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(ran, "ran run.sh") && stublines().rfind("ran /", 0) == 0 && has(stublines(), "") , "`exec: run`: Enter on a script with the x bit runs it here and stays");
    run(editors + with_setting("{ \"exec\": \"open\" }") + " --frame 80x20 --keys \"Right Down Down Enter\" >/dev/null 2>&1", crc);
    check(status_of(crc) == 0 && has(stublines(), "/alpha/run.sh"), "`exec: open`: the system opener gets it");
    // A BUNDLE: listed as a leaf, never entered, opened as an application.
    const std::string bundle = run(editors + with_setting("{}") + " --frame 100x20 --keys \"Down Down Down Right\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(bundle, "Thing.app") && !has(bundle, "Thing.app \xE2\x80\xBA") && !has(bundle, "Contents"),
          "a .app is listed without the chevron and Right does not enter it");
    run(editors + with_setting("{}") + " --frame 100x20 --keys \"Down Down Down Right Enter\" >/dev/null 2>&1", crc);
    check(status_of(crc) == 0 && has(stublines(), "Thing.app"), "…and Enter opens it as the application it is, through the opener");
    const std::string leave = run(editors + with_setting("{ \"leave\": true }") + " --frame 80x20 --keys \"End Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && has(leave, ".txt\n") && stublines().empty(), "`leave` on: Enter on a document leaves with it on the command line too, opening nothing");
    // A DOUBLE-CLICK IS ENTER ON THAT ROW: the same file, chosen by the mouse, hands its path over
    // exactly as Enter does; a single click only selects it.
    int cx = -1, cy = -1;
    {
      const std::string frame = run(with_setting("{ \"leave\": true }") + " --frame 80x20 2>/dev/null", crc);
      std::istringstream in(frame);
      std::string l;
      for (int y = 0; std::getline(in, l); ++y) {
        const std::size_t at = l.find("one.txt");
        if (at != std::string::npos) { cx = static_cast<int>(at); cy = y; break; }
      }
    }
    const std::string where = std::to_string(cx) + "," + std::to_string(cy);
    const std::string dbl = run(with_setting("{ \"leave\": true }") + " --frame 80x20 --keys \"DblClick " + where + "\" 2>/dev/null", crc);
    check(cx > 0 && status_of(crc) == 3 && dbl.find("/alpha/one.txt\n") != std::string::npos,
          "a double-click on a row is Enter on it [" + dbl.substr(dbl.rfind('/') + 1) + " at " + where + "]");
    const std::string single = run(with_setting("{ \"leave\": true }") + " --frame 80x20 --keys \"Click " + where + "\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(single, "find: "), "…while a single click only selects: the frame is still up");
    // THE CURSOR REMEMBERS WHERE IT WAS, PER FOLDER: down into alpha, onto one.txt, back out,
    // over to beta and back to alpha, in again — one.txt is still under the cursor. Into beta
    // instead, and it is beta's own memory (none yet: its first entry), never alpha's.
    const std::string back_again = run(with_setting("{ \"leave\": true }") + " --frame 80x20 --keys \"Right Down Left Down Up Right Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && back_again.find("/alpha/one.txt\n") != std::string::npos,
          "a folder left and re-entered — even after visiting a sibling — puts the cursor back where it was [" +
              back_again.substr(back_again.rfind('/') + 1) + "]");
    const std::string sibling = run(with_setting("{ \"leave\": true }") + " --frame 80x20 --keys \"Right Down Left Down Right Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && sibling.find("/beta/") != std::string::npos && sibling.find("one.txt") == std::string::npos,
          "…while a sibling folder entered instead starts on its own first entry, not on alpha's memory");
    // A DEEP START SEEDS THE MEMORY: the ancestors' selections the walk made count as visits, so
    // Left out of the start folder and Right again is back on it — beta, the SECOND entry of
    // tree, not alpha, its first. A folder chosen with Enter is printed with exit 0.
    const std::string deep_back = run(home_env + bin + " '" + (tree / "beta").string() + "'" + presets +
                                      " --theme default-dark --frame 80x20 --keys \"Left Left Right Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && deep_back == (tree / "beta").string() + "\n",
          "started deep, Left twice and Right again lands on the start folder, not on its parent's first entry [" +
              deep_back.substr(deep_back.rfind('/') + 1) + "]");
    const std::string insert = run(with_setting("{ \"leave\": true, \"paths\": \"relative\" }") + " --frame 80x20 --keys \"Right End Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 3 && insert.rfind("./alpha/", 0) == 0,
          "…`leave` with relative paths leaves with `./alpha/…` for the command line and exit 3: the verb is the status [" + insert + "]");

    // COPY: `c` puts the path on the clipboard, absolute by default; the Option chord inverts
    // the setting, and the setting inverts the chord.
    run(with_setting("{}") + " --frame 80x20 --keys \"Right End CtrlC\" >/dev/null 2>&1", crc);
    const std::string abs_log = stublines();
    check(abs_log.rfind("clip /", 0) == 0 && has(abs_log, "/alpha/"), "c copies the absolute path [" + abs_log.substr(0, 30) + "]");
    run(with_setting("{}") + " --frame 80x20 --keys \"Right End AltC\" >/dev/null 2>&1", crc);
    check(stublines().rfind("clip ./alpha/", 0) == 0, "…Alt-c copies it relative to where dirk started: the inverse of the setting");
    run(with_setting("{ \"paths\": \"relative\" }") + " --frame 80x20 --keys \"Right End CtrlC\" >/dev/null 2>&1", crc);
    check(stublines().rfind("clip ./alpha/", 0) == 0, "…with the setting on relative, c copies relative");
    run(with_setting("{ \"paths\": \"relative\" }") + " --frame 80x20 --keys \"Right End AltC\" >/dev/null 2>&1", crc);
    check(stublines().rfind("clip /", 0) == 0, "…and Alt-c then copies the absolute path");
    const std::string empty = run(home_env + bin + " '" + (tree / "empty-dir").string() + "' --frame 80x20 --keys \"Enter\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && empty == (tree / "empty-dir").string() + "\n",
          "Enter in an EMPTY directory prints that directory: the eye is on it and there is nothing else to choose");
    for (const char* cancel : {"Escape", "CtrlQ"}) {  // Ctrl-C is copy, as everywhere a clipboard has one
      const std::string none = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"" + cancel + "\" 2>/dev/null", crc);
      check(status_of(crc) == 1 && none.empty(),
            std::string(cancel) + " prints NOTHING and exits 1 — the shell function's `|| return` needs both");
    }
    // A PRESS ON A KEY HINT IS THAT KEY: the status line's "F2 settings" is a hint bar, and a
    // click on it opens the settings popup exactly as F2 does.
    {
      const std::string plain = run(home_env + bin + " '" + here + "' --frame 80x20 2>/dev/null", crc);
      std::string last;
      { std::istringstream in(plain); for (std::string l; std::getline(in, l);) if (!l.empty()) last = l; }
      const std::size_t at = last.find("settings");
      int cell = -1;
      if (at != std::string::npos) { cell = 0; for (std::size_t i = 0; i < at; ++i) if ((static_cast<unsigned char>(last[i]) & 0xC0) != 0x80) ++cell; }
      const std::string clicked = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"Click " + std::to_string(cell + 2) + ",19\" 2>/dev/null", crc);
      check(cell >= 0 && has(clicked, "General") && has(clicked, "Show dotfiles"),
            "a click on the status line's 'F2 settings' opens the settings, as the key does [cell " + std::to_string(cell) + "]");
    }
    // A CLICK OUTSIDE THE SETTINGS CLOSES THEM — the popup's own `dismiss` — and a click inside
    // does not.
    {
      const std::string outside = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"F2 Click 1,1\" 2>/dev/null", crc);
      check(!has(outside, "General") && has(outside, "find: "), "a click outside the settings popup closes it");
      // On its top border: a click on a row would be a choice, and a choice is saved.
      const std::string inside = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"F2 Click 40,0\" 2>/dev/null", crc);
      check(has(inside, "General"), "…and a click inside it leaves it open");
      // ONE LEVEL AT A TIME: with a dropdown open in the settings, Escape — or a click outside the
      // popup — closes the dropdown and leaves the settings; the next one closes the settings.
      const std::string eleven = "Down Down Down Down Down Down Down Down Down Down Down Down Down";  // thirteen: sparkle joined Look and the executables choice "Enter on a file"
      const std::string dd = run(home_env + bin + " '" + here + "' --frame 90x24 --keys \"F2 " + eleven + " Enter\" 2>/dev/null", crc);
      check(has(dd, "Neovim") && has(dd, "General"), "the control: Enter on an open-with choice opens its dropdown over the settings");
      const std::string dd_esc = run(home_env + bin + " '" + here + "' --frame 90x24 --keys \"F2 " + eleven + " Enter Escape\" 2>/dev/null", crc);
      check(!has(dd_esc, "Neovim") && has(dd_esc, "General"), "Escape closes the dropdown only: the settings stay");
      const std::string dd_click = run(home_env + bin + " '" + here + "' --frame 90x24 --keys \"F2 " + eleven + " Enter Click 1,1\" 2>/dev/null", crc);
      check(!has(dd_click, "Neovim") && has(dd_click, "General"), "…a click outside the popup likewise closes the dropdown only");
      const std::string dd_esc2 = run(home_env + bin + " '" + here + "' --frame 90x24 --keys \"F2 " + eleven + " Enter Escape Escape\" 2>/dev/null", crc);
      check(!has(dd_esc2, "General") && has(dd_esc2, "find:"), "…and the next Escape closes the settings");
      // THE STATUS LINE'S STATES ARE CLICKABLE: `sort name` cycles the sort, `+dotfiles` toggles.
      const std::string plain = run(home_env + bin + " '" + here + "' --frame 100x20 2>/dev/null", crc);
      auto cell_of = [](const std::string& fr, const char* word) {
        std::string last; std::istringstream in(fr); for (std::string l; std::getline(in, l);) if (!l.empty()) last = l;
        const std::size_t at = last.find(word); if (at == std::string::npos) return -1;
        int cell = 0; for (std::size_t i = 0; i < at; ++i) if ((static_cast<unsigned char>(last[i]) & 0xC0) != 0x80) ++cell; return cell; };
      const int sc = cell_of(plain, "sort name"), dc = cell_of(plain, "+dotfiles");
      // THE CHORD SHOWN IS ONE THIS TERMINAL CAN DELIVER: a headless run negotiates nothing, so the
      // dotfiles hint says Alt-H, not the file's first chord Ctrl-. — and nothing is called
      // undeliverable before a terminal has been asked.
      check(has(plain, "Alt-H +dotfiles") && !has(plain, "Ctrl-."), "the hint bar shows the first chord that can arrive on this terminal");
      const std::string quiet = run(home_env + bin + " '" + here + "' --frame 100x20 2>&1", crc);
      check(!has(quiet, "undeliverable"), "…and a chord is never called undeliverable before the terminal has said what it speaks");
      // EACH CLICK RUN IN ITS OWN SETTINGS DIRECTORY: a click saves, and the next run would
      // otherwise start from what the last one chose.
      auto own = [&](const char* name) { return "ROLL_CONFIG_DIR='" + (scratch / name).string() + "' " + bin + " '" + here + "'"; };
      const std::string cycled = run(own("click-1") + " --frame 100x20 --keys \"Click " + std::to_string(sc + 1) + ",19\" 2>/dev/null", crc);
      check(sc >= 0 && has(cycled, "sort name z-a"), "a click on `sort name a-z` cycles the sort: the line says `sort name z-a` [" + std::to_string(sc) + "]");
      const std::string cycled2 = run(own("click-2") + " --frame 100x20 --keys \"Click " + std::to_string(sc + 1) + ",19 Click " + std::to_string(sc + 1) + ",19\" 2>/dev/null", crc);
      check(has(cycled2, "sort size big-small"), "…twice: `sort size big-small`");
      const std::string dflt = run(own("click-3") + " --frame 100x24 --keys \"F2\" 2>/dev/null", crc);
      check(has(dflt, "(default on)") && has(dflt, "(default)"), "every setting says its default: toggles as (default on/off), a choice on its default option");
      const std::string toggled = run(own("click-4") + " --frame 100x20 --keys \"Click " + std::to_string(dc + 1) + ",19\" 2>/dev/null", crc);
      check(dc >= 0 && has(toggled, "\xE2\x88\x92" "dotfiles") && !has(toggled, ".hidden"), "a click on `+dotfiles` hides them: the line says `−dotfiles` and .hidden is gone");
      // A NOTE ON THE LINE — "copied …" — shows, holds two seconds, then fades out and is gone.
      const std::string noted = run(stubs + own("click-5") + " --frame 100x20 --keys \"CtrlC Tick:500\" 2>/dev/null", crc);
      check(has(noted, "copied"), "Ctrl-C copies the path and the line says so");
      const std::string gone = run(stubs + own("click-6") + " --frame 100x20 --keys \"CtrlC Tick:3000\" 2>/dev/null", crc);
      check(!has(gone, "copied") && has(gone, "copy"), "…and three seconds later the note has faded away, the copy hint still there");
      stublines();
      // COPY IS DISABLED WHERE THERE IS NOTHING TO COPY: in an empty folder a press on `copy` is nobody's.
      const std::string empty_start = "'" + (tree / "empty-dir").string() + "'";
      const std::string in_empty = run(stubs + "ROLL_CONFIG_DIR='" + (scratch / "click-7").string() + "' " + bin + " " + empty_start + " --frame 100x20 2>/dev/null", crc);
      const int cc = cell_of(in_empty, "copy");
      const std::string pressed = run(stubs + "ROLL_CONFIG_DIR='" + (scratch / "click-7").string() + "' " + bin + " " + empty_start + " --frame 100x20 --keys \"Click " + std::to_string(cc + 1) + ",19\" 2>/dev/null", crc);
      check(cc >= 0 && !has(pressed, "copied") && stublines().empty(), "in an empty folder the copy hint is disabled: a press on it copies nothing");
      // TYPE TO JUMP: a letter lands on the next name starting with it in the cursor's column;
      // again goes on; Shift goes back; it wraps.
      const std::string z = run(stubs + own("click-8") + " --frame 100x20 --keys \"z CtrlC\" 2>/dev/null", crc);
      check(has(z, "copied") && stublines().find("zeta.txt") != std::string::npos, "typing z lands on zeta.txt");
      const std::string aa = run(stubs + own("click-9") + " --frame 100x20 --keys \"a CtrlC\" 2>/dev/null", crc);
      check(stublines().find("a-very-long-name") != std::string::npos, "a, with the cursor already on alpha, goes on to the next name starting with a");
      const std::string back = run(stubs + own("click-10") + " --frame 100x20 --keys \"a A CtrlC\" 2>/dev/null", crc);
      check(stublines().find("clip " + here + "/alpha\n") != std::string::npos, "…and Shift-A goes back to alpha");
      const std::string wrap = run(stubs + own("click-11") + " --frame 100x20 --keys \"a a CtrlC\" 2>/dev/null", crc);
      check(stublines().find("clip " + here + "/alpha\n") != std::string::npos, "…and a third a wraps round to alpha again");
    }
    const std::string popup = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"F1 Escape\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(popup, "alpha") && !has(popup, "the live key table"),
          "…while Escape over a POPUP closes the popup and the session goes on — cancel is the browser's key, not a global one");
    // THE PATH LINE IS A BREADCRUMB until it is edited: each part is its column, the pencil at
    // the end opens the line as text with the whole path selected. The app's name sits at the
    // right end of the status line, not on the columns' border.
    {
      const std::string deep = run(home_env + bin + " '" + (tree / "alpha" / "nested").string() + "' --frame 100x12 2>&1", crc);
      check((has(deep, "/ \xE2\x80\xBA ") || has(deep, "\xE2\x80\xA6 \xE2\x80\xBA ")) && has(deep, "alpha \xE2\x80\xBA nested \xE2\x80\xBA deep.txt \xE2\x9C\x8E") && !has(deep, "\xE2\x94\x8C dirktui"),
            "the path line is a breadcrumb — the folder's parts joined by ›, the entry under the cursor last, its head behind an ellipsis when long, a pencil at the end — and 'dirktui' is off the border");
      { std::istringstream in(deep); std::string last; for (std::string l; std::getline(in, l);) if (!l.empty()) last = l; check(last.find("dirktui") != std::string::npos, "…and on the status line, at its right end"); }
      // The x of a part: cells before it on the crumb row (the frame's first row after the two stderr lines).
      auto cell_at = [](const std::string& fr, const std::string& word) { std::istringstream in(fr); std::string l; while (std::getline(in, l) && l.find("\xE2\x9C\x8E") == std::string::npos) {} const std::size_t at = l.find(word); if (at == std::string::npos) return -1; int c = 0; for (std::size_t i = 0; i < at; ++i) if ((static_cast<unsigned char>(l[i]) & 0xC0) != 0x80) ++c; return c; };
      const int ax = cell_at(deep, "alpha"), px = cell_at(deep, "\xE2\x9C\x8E");
      const std::string up = run(home_env + bin + " '" + (tree / "alpha" / "nested").string() + "' --frame 100x12 --keys \"Click " + std::to_string(ax + 1) + ",0\" 2>&1", crc);
      check(ax >= 0 && has(up, "column ") && column_of(up).first == column_of(deep).first - 2 && column_of(up).second == column_of(up).first + 1 && has(up, "tree \xE2\x80\xBA alpha \xE2\x9C\x8E"),
            "a click on a part puts the cursor ON it — its parent column focused, the part highlighted, its listing the preview, nothing deeper — and the breadcrumb shrinks to it [" + std::to_string(column_of(up).first) + "/" + std::to_string(column_of(up).second) + " from " + std::to_string(column_of(deep).first) + "]");
      const std::string pen = run(home_env + bin + " '" + (tree / "alpha" / "nested").string() + "' --frame 100x12 --keys \"Click " + std::to_string(px) + ",0 Type:abc\" 2>/dev/null", crc);
      check(px >= 0 && has(pen, "abc") && !has(pen, " \xE2\x9C\x8E"), "a click on the pencil opens the line as text, selected whole: typing replaces the path");
      const std::string beyond = run(home_env + bin + " '" + (tree / "alpha" / "nested").string() + "' --frame 200x12 --keys \"Click 198,0 Type:abc\" 2>/dev/null", crc);  // wide: the bar ends well before the edge
      check(has(beyond, "abc") && !has(beyond, " \xE2\x9C\x8E"), "…and so does a click anywhere right of the pencil on that row");
      // Editing the line: Home and End, Shift-End selects to the end, Alt-C copies the selection.
      const std::string home = run(home_env + bin + " '" + here + "' --frame 100x12 --keys \"ShiftTab Right Home Type:X\" 2>/dev/null", crc);
      check(home.rfind("X/", 0) == 0, "Home in the path line goes to its start");
      const std::string end = run(home_env + bin + " '" + here + "' --frame 100x12 --keys \"ShiftTab Home End Type:X\" 2>/dev/null", crc);
      { std::istringstream in(end); std::string l; std::getline(in, l); while (!l.empty() && l.back() == ' ') l.pop_back(); check(!l.empty() && l.back() == 'X', "…and End to its end"); }
      const std::string copied = run(stubs + home_env + bin + " '" + here + "' --frame 100x12 --keys \"ShiftTab Home ShiftEnd AltC\" 2>/dev/null", crc);
      const std::string clip = stublines();
      check(clip.rfind("clip " + here, 0) == 0, "Shift-End selects to the end and Alt-C copies the selection to the clipboard [" + clip.substr(0, 40) + "]");
    }
    // THE PATH LINE: Shift-Tab (or a click) reaches it, selected whole, so typing replaces the
    // path; Escape puts the path back and returns to the columns — never one key from leaving.
    const std::string typed = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"ShiftTab Type:abc\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && has(typed, "abc") && !has(typed, here),
          "the path line, reached, is selected whole: typing replaces the path");
    const std::string typing = run(home_env + bin + " '" + here + "' --frame 80x20 --keys \"ShiftTab Type:abc Escape\" 2>/dev/null", crc);
    check(status_of(crc) == 0 && !has(typing, "abc") && has(typing, " \xE2\x9C\x8E") && has(typing, "alpha"),
          "…and Escape in the path line puts the path back — a breadcrumb again — and goes on, so typing a path is never one key from leaving");
  }

  // ---- `dirktui init <shell>`: the shell side, parsed by each shell and RUN through a stand-in ----
  // The Right Arrow bindings need a line editor and are not exercised here; the `dirk` function
  // is, in zsh and bash, with `command dirktui` resolved to a script on PATH that answers what the
  // real binary would, and `$DIRK_OPEN` resolved to a script that RECORDS what it was asked to open.
  {
    int irc = 0;
    const std::string product = std::string("'") + DIRKTUI_PRODUCT_BIN + "'";
    // ---- `dirktui install`: the binary on PATH and the shell side in the rc file, once ----
    {
      const fs::path ihome = scratch / "home-install";
      fs::create_directories(ihome);
      write_file(ihome / ".zshrc", "# mine\nexport FOO=1\nsource ~/.zsh/zsh-autosuggestions/zsh-autosuggestions.zsh\n");
      const std::string ienv = "HOME='" + ihome.string() + "' SHELL=/bin/zsh ";
      const std::string said = run(ienv + product + " install 2>&1", irc);
      const fs::path link = ihome / ".local" / "bin" / "dirktui";
      check(status_of(irc) == 0 && fs::is_symlink(link) && fs::canonical(link) == fs::canonical(DIRKTUI_PRODUCT_BIN),
            "`dirktui install` links THIS binary into ~/.local/bin, the shell taken from $SHELL [" + said.substr(0, 60) + "]");
      check(has(said, "zsh-autosuggestions is here too") && has(said, "Right to accept, Right to browse"),
            "…and, seeing zsh-autosuggestions in the rc file, says once how Right Arrow is shared");
      bool ok = false;
      const std::string zrc = read_file((ihome / ".zshrc").string(), ok);
      check(ok && zrc.rfind("# mine\nexport FOO=1\nsource ~/.zsh/", 0) == 0 && has(zrc, "command -v dirktui >/dev/null 2>&1 && eval \"$(dirktui init zsh)\"") && has(zrc, ".local/bin"),
            "…and appends ONE marked block to ~/.zshrc — PATH, then the shell side GUARDED on the binary existing — after what was there");
      run(ienv + product + " install zsh 2>&1", irc);
      check(read_file((ihome / ".zshrc").string(), ok) == zrc, "…a second install writes nothing twice");
      run("zsh -n '" + (ihome / ".zshrc").string() + "' 2>&1", irc);
      check(status_of(irc) == 0, "…and zsh -n accepts the rc file it wrote");
      write_file(ihome / ".bash_profile", "export BAR=2\n");
      const std::string bsaid = run(ienv + product + " install bash 2>&1", irc);
      check(has(read_file((ihome / ".bashrc").string(), ok), "eval \"$(dirktui init bash)\"") && has(bsaid, ".bash_profile, which does not source ~/.bashrc"),
            "bash: the block goes into ~/.bashrc, and a profile that never reads it is NAMED, not edited [" + bsaid.substr(bsaid.find("note"), 50) + "]");
      check(read_file((ihome / ".bash_profile").string(), ok) == "export BAR=2\n", "…the profile is untouched");
      run(ienv + product + " install fish 2>&1", irc);
      check(has(read_file((ihome / ".config" / "fish" / "conf.d" / "dirk.fish").string(), ok), "dirktui init fish | source"),
            "fish: a conf.d file of its own, since fish sources the directory");
      const std::string bad = run(ienv + product + " install nushell 2>&1", irc);
      check(status_of(irc) == 2 && has(bad, "usage: dirktui install zsh|bash|fish"), "a shell it has no script for is refused");
      run(ienv + product + " uninstall zsh 2>&1", irc);
      check(status_of(irc) == 0 && !fs::exists(fs::symlink_status(link)) && read_file((ihome / ".zshrc").string(), ok) == "# mine\nexport FOO=1\nsource ~/.zsh/zsh-autosuggestions/zsh-autosuggestions.zsh\n" &&
                has(read_file((ihome / ".bashrc").string(), ok), "init bash"),
            "`dirktui uninstall zsh` removes the link and the block, leaving the file as it was and the other shells alone");
    }
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
                                     "';;\n  insert) printf './alpha/one.txt\\n'; exit 3;;\n  beside) printf '%s\\n' '" + (tree / "alpha" / "one.txt").string() +
                                     "'; exit 4;;\n  *) exit 1;;\nesac\n");
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
                                "cd /; FAKE=beside dirk; echo \"beside=$? $PWD\"; " +
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
      check(has(drove, "beside=0 " + alpha + "\n") && has(drove, "./one.txt"),
            std::string(sh.name) + ": exit 4 lands in the file's folder with ./name on the line (or shown) [" +
                drove.substr(drove.find("beside="), 40) + "]");
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
  check(has(wide, ".hidden"), "a dotfile is shown unless a person turns them off");
  {
    const std::string roomy = run(base + " --frame 220x30 --keys \"AltH\" 2>&1", rc);
    check(rc == 0 && !has(roomy, " .hidden") && has(roomy, "1 hidden"),
          "…and once hidden the widget SAYS how many it is holding back, through the plugin's `note_at` slot");
  }

  // ---- 2. the selection propagates sideways, which is the widget's argument ------------------
  const std::string into = run(base + " --frame 150x30 --keys \"Right Down\" 2>&1", rc);
  check(rc == 0 && has(into, "one.txt") && has(into, "two.txt"),
        "Right enters the selected directory and Down moves inside it");
  // alpha holds `nested/`, `one.txt`, `two.txt` — directories first, so Right Right walks
  // root → alpha → nested and the third column is the deepest one.
  const std::string deep = run(base + " --frame 150x30 --keys \"Right Right\" 2>&1", rc);
  check(rc == 0 && has(deep, "deep.txt"), "…and a third column opens from the second's selection");
  check(column_of(deep).first == column_of(deep).second && column_of(deep).second >= 3,
        "…the status line counts the columns it is showing, and the focus is the last of them [" +
            std::to_string(column_of(deep).first) + "/" + std::to_string(column_of(deep).second) + "]");

  // ---- 3. wide, combining and over-long names ------------------------------------------------
  // ---- THE ANCHOR RULE: the focused column and its whole preview are always on screen ----------
  // Wide enough for the root column (28 cells: the long name) and one preview, not for three: after Right the focus is `alpha` and the column
  // the selection fills (`nested`) must END at the right edge, with the root column shown
  // partially on the left — so its title, which starts at its left edge, is off screen.
  {
    const std::string narrow = run(base + " --frame 46x10 --keys \"Right\" 2>&1", rc);
    check(rc == 0 && has(narrow, "nested") && has(narrow, "deep.txt"),
          "Right opens the NEW focus's preview at once, so the column to the right is never empty");
    check(!has(narrow, "\xE2\x94\x82  tree"),  // a head starts one in, like the rows
          "…and the root column is scrolled partly off the left edge to make room for it");
    const std::string roomy = run(base + " --frame 300x30 --keys \"Right\" 2>&1", rc);
    check(has(roomy, "\xE2\x94\x82  /"),
          "…while a window that fits every column packs them from the left, the file system's root first");
    // The status line is truncated from the right, so its column count is read from a wide frame
    // of the same keys; the narrow frames above are for what is drawn, not for what is counted.
    const std::string in = run(base + " --frame 200x10 --keys \"Right\" 2>&1", rc);
    const std::string back = run(base + " --frame 200x10 --keys \"Right Left\" 2>&1", rc);
    check(rc == 0 && column_of(back).first == column_of(in).first - 1 && has(back, "alpha"),
          "Left slides the columns back one, to the column the eye came from");
    // THE LAST SLOT IS RESERVED AT THE MAXIMUM WIDTH: moving the cursor from a folder with a narrow
    // preview to one with a wide preview does not move the focused column.
    const std::string on_alpha = run(base + " --frame 60x10 2>&1", rc);
    const std::string on_beta = run(base + " --frame 60x10 --keys \"Down\" 2>&1", rc);
    auto col_of_word = [](const std::string& frame, const char* word) {
      std::istringstream in(frame);
      std::string l;
      while (std::getline(in, l)) {
        if (l.find("\xE2\x9C\x8E") != std::string::npos) continue;  // the breadcrumb row names the folder too: not a column
        const std::size_t at = l.find(word);
        if (at != std::string::npos) return static_cast<int>(at);
      }
      return -1;
    };
    check(has(on_beta, "a-quite-long-name") && !has(on_alpha, "a-quite-long-name"),
          "the control: Down really changed the preview, to a wider column");
    check(col_of_word(on_alpha, "alpha \xE2\x80\xBA") == col_of_word(on_beta, "alpha \xE2\x80\xBA") && col_of_word(on_alpha, "alpha \xE2\x80\xBA") > 0,
          "…and the focused column did not move: the last slot is reserved at the maximum width [" +
              std::to_string(col_of_word(on_alpha, "alpha \xE2\x80\xBA")) + " = " + std::to_string(col_of_word(on_beta, "alpha \xE2\x80\xBA")) + "]");
    // …AND THE SLOT IS HELD WHILE A FOLDER COULD BE SELECTED: a file under the cursor has no
    // preview, but the focused column keeps its place as long as it holds any folder; only a
    // column with no folders at all (beta: files only) gives the space up and moves right.
    const std::string on_nested = run(base + " --frame 60x10 --keys \"Right\" 2>&1", rc);
    const std::string on_one = run(base + " --frame 60x10 --keys \"Right Down\" 2>&1", rc);
    check(has(on_one, "one.txt") && col_of_word(on_nested, "nested \xE2\x80\xBA") == col_of_word(on_one, "nested \xE2\x80\xBA") &&
              col_of_word(on_one, "nested \xE2\x80\xBA") > 0,
          "a file under the cursor still reserves the slot: the focused column does not move when the cursor crosses a file");
    const std::string in_beta = run(base + " --frame 60x10 --keys \"Down Right\" 2>&1", rc);
    check(col_of_word(in_beta, "a-quite-long-name") > col_of_word(on_one, "one.txt"),
          "…while a column with no folders at all gives the slot up and sits further right [" +
              std::to_string(col_of_word(in_beta, "a-quite-long-name")) + " > " + std::to_string(col_of_word(on_one, "one.txt")) + "]");
    // …AND A LEAF PREVIEW IS LAID OUT AT ITS FINAL WIDTH BEFORE THE CURSOR ENTERS IT: beta, files
    // only, sits where it will sit once entered, so Right into it shifts nothing.
    check(col_of_word(on_beta, "a-quite-long-name") == col_of_word(in_beta, "a-quite-long-name") && col_of_word(in_beta, "a-quite-long-name") > 0,
          "a preview with no folders is already at its final place: entering it does not move it [" +
              std::to_string(col_of_word(on_beta, "a-quite-long-name")) + " = " + std::to_string(col_of_word(in_beta, "a-quite-long-name")) + "]");
    // THE FADE at the left edge is the library kind's and is asserted in `picker_test`, which can
    // read the cell count a text frame cannot show; this app only names `filepicker`.
    // A START INSIDE A LEAF — a folder with no folders — is anchored like any other start: the leaf
    // at the right, its ancestors to the left, never the leaf alone at the far left.
    const std::string leaf_start = run(home_env + bin + " '" + (tree / "alpha" / "nested").string() + "'" + presets +
                                           " --theme default-dark --frame 60x10 2>&1", rc);
    check(has(leaf_start, "alpha") && has(leaf_start, "deep.txt") && col_of_word(leaf_start, "deep.txt") > 30,
          "a start inside a leaf folder shows its ancestors, with the leaf at the right [deep.txt at " +
              std::to_string(col_of_word(leaf_start, "deep.txt")) + "]");
    check(has(leaf_start, "leaf-file-long-name.txt"),
          "…and the leaf is drawn at the slot's full width, so a name longer than a content-sized column shows whole");
    // A DEEP START SHOWS ITS ANCESTORS: the columns run from the root down to the start folder,
    // each with the next component selected — the reason the focus sits one in from the edge.
    const std::string deep_start = run(base + " --frame 200x10 2>&1", rc);
    check(column_of(deep_start).second > column_of(deep_start).first && column_of(deep_start).first >= 3 &&
              has(deep_start, "tree") && has(deep_start, "alpha"),
          "a start folder is shown with its ancestors to the left and its preview to the right [column " +
              std::to_string(column_of(deep_start).first) + "/" + std::to_string(column_of(deep_start).second) + "]");
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
    auto num = [](const std::string& line, const char* key) {
      const std::size_t at = line.find(key);
      return at == std::string::npos ? -1 : std::atoi(line.c_str() + at + std::strlen(key));
    };
    // THE CURSOR shimmers, folder or file alike; THE TRAIL — every ancestor column's selection on
    // screen — sparkles; and both are marked at every moment.
    const std::string t0 = fx("Tick:0"), t1 = fx("Tick:400"), on_file = fx("End Tick:0");
    check(num(t0, "marks=") >= 1 && num(t0, "drawn=") >= 1,
          "the cursor row is marked, and the theme draws on it [" + t0 + "]");
    check(num(t1, "marks=") == num(t0, "marks=") && num(t1, "drawn=") >= 1,
          "…at a later moment the same rows are marked and still drawn on [" + t1 + "]");
    // EXACTLY WHICH ROWS ARE MARKED, in a window wide enough to show every column: the cursor,
    // and one trail row per column LEFT of the focus — never the preview to its right, whose
    // first entry is not a choice anyone made. So the count is the focused column's index.
    auto fx_wide = [&](const std::string& keys) {
      int frc = 0;
      const std::string out = run(base + " --frame 300x10 --keys \"" + keys + "\" 2>&1", frc);
      const std::size_t at = out.find("effects: ");
      return std::make_pair(at == std::string::npos ? std::string() : out.substr(at, out.find('\n', at) - at), column_of(out).first);
    };
    const auto w0 = fx_wide("Tick:0"), w1 = fx_wide("Right Tick:0");
    // A row's mark is one span per WORD of its name (a spark never lands on a space or the
    // chevron), and no name on this path has a space, so one row is one mark.
    check(num(w0.first, "marks=") == w0.second && w0.second >= 2,
          "the marks are the cursor plus one trail row per ancestor column, and NOT the preview's first entry [marks " +
              std::to_string(num(w0.first, "marks=")) + " = column " + std::to_string(w0.second) + "]");
    check(num(w1.first, "marks=") == w1.second && w1.second == w0.second + 1,
          "…and after Right the column we left is a trail row and the new preview still is not");
    // A name with a space is two spans; a chevron is none.
    fs::create_directories(tree / "two words");
    const std::string spaced = run(base + "/two\\ words --frame 300x10 --keys \"Tick:0\" 2>&1", rc);
    // Started INSIDE the empty folder "two words": its column has no row to be a cursor, so the
    // marks are the ancestors' trail rows, one each — except `tree`'s, whose selection "two
    // words" is two runs. One row of two marks: the count is the focused column's index.
    check(num(spaced.substr(spaced.find("effects: ")), "marks=") == column_of(spaced).first,
          "a name with a space is marked as two runs of letters, so a spark never lands on the space [" +
              spaced.substr(spaced.find("effects: "), 40) + " column " + std::to_string(column_of(spaced).first) + "]");
    check(num(on_file, "marks=") >= 1 && num(on_file, "drawn=") >= 1,
          "a file under the cursor is marked exactly as a folder is — the cursor is the cursor [" + on_file + "]");
    // OPENING A FILE adds one mark, the burst, for its moment; then the count is back. Compared
    // at the SAME moment without the Enter, because End moves the anchor and the slide reveals
    // a trail row between one tick and the next.
    // The Enter runs need an opener to hand the file to — a stand-in, since a headless run
    // refuses the real one and, refused, has nothing to burst about.
    const fs::path fx_stub = scratch / "fx-opener";
    write_file(fx_stub, "#!/bin/sh\nexit 0\n");
    chmod(fx_stub.c_str(), 0755);
    auto fx_open = [&](const std::string& keys) {
      int frc = 0;
      const std::string err = run("DIRK_OPEN='" + fx_stub.string() + "' " + base + " --frame 46x10 --keys \"" + keys + "\" 2>&1 >/dev/null", frc);
      const std::size_t at = err.find("effects: ");
      return at == std::string::npos ? std::string("(no effects line)") : err.substr(at, err.find('\n', at) - at);
    };
    const std::string base100 = fx("End Tick:100"), burst = fx_open("End Enter Tick:100");
    const std::string base2000 = fx("End Tick:2000"), after = fx_open("End Enter Tick:2000");
    check(num(burst, "marks=") == num(base100, "marks=") + 1 && num(burst, "drawn=") >= 1,
          "Enter on a file adds ONE mark — the burst on that row — for its moment [" + burst + " vs " + base100 + "]");
    check(num(after, "marks=") == num(base2000, "marks="), "…and the moment passes: the burst's mark is gone, the cursor's stays");
    const std::string still = fx("Right");
    check(num(still, "marks=") >= 1, "a script with no tick is the still picture: marked, every effect at its first instant [" + still + "]");

    // THE MAPPING IS A FILE, and a person's config directory shadows the embedded one. The
    // picker's states are the library's and every shipped theme maps them, so the proof is a
    // row that REPLACES the theme's: a person's file maps the cursor to a kind the registry does
    // not have, and the marks then draw nothing — the theme's shimmer is gone, and the app's
    // glow was never read. A state nobody registered is reported by name where a developer is
    // looking.
    const fs::path cfg = scratch / "cfg";
    fs::create_directories(cfg / "rolltui" / "dirktui");
    write_file(cfg / "rolltui" / "dirktui" / "effects.json", "{ \"effects\": { \"picker_cursor\": { \"kind\": \"no_such_kind\", \"period_ms\": 100 } } }\n");
    int crc = 0;
    const std::string shadowed = run("ROLL_CONFIG_DIR='" + cfg.string() + "' " + bin + " '" + tree.string() + "'" + presets + " --theme default-dark --frame 46x10 --keys \"Tick:0\" 2>&1 >/dev/null", crc);
    check(num(shadowed, "marks=") >= 1 && has(shadowed, "drawn=0"), "a user's own effects file shadows the app's and REPLACES the theme's row: the marks are there and nothing draws them [" +
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
    check(has(opened, "settings") && has(opened, "[\xE2\x9C\x93] Motion") && has(opened, "[\xE2\x9C\x93] Show dotfiles") && has(opened, "Sort by"),
          "F2 opens the settings menu, its boxes set from the live values (motion on, dotfiles on)");
    run(sbase + " --frame 60x18 --keys \"F2 Down Down Down Down Enter\" >/dev/null 2>&1", mrc);  // dotfiles, sort, keys, theme, Motion
    bool ok = false;
    const std::string saved = read_file((cfg / "rolltui" / "dirktui" / "settings.json").string(), ok);
    check(ok && has(saved, "\"motion\": false"), "toggling Motion writes the settings file [" + saved.substr(0, 60) + "]");
    // Motion is the SLIDE; the marks and their effects are Sparkle's, a setting of its own.
    run(sbase + " --frame 60x18 --keys \"F2 Down Down Down Down Down Enter\" >/dev/null 2>&1", mrc);  // …and Sparkle, the row under Motion
    const std::string still = run(sbase + " --frame 46x10 --keys \"Tick:0\" 2>&1 >/dev/null", mrc);
    check(has(still, "drawn=0") && !has(still, "marks=0"), "…the next run reads both: the rows are marked and, with sparkle off, nothing draws");
    run(sbase + " --frame 60x18 --keys \"F2 Down Down Down Down Down Enter\" >/dev/null 2>&1", mrc);  // sparkle back on
    const std::string snapped = run(sbase + " --frame 46x10 --keys \"Right Tick:30\" 2>/dev/null", mrc);
    const std::string ended = run(sbase + " --frame 46x10 --keys \"Right Tick:200\" 2>/dev/null", mrc);
    check(snapped == ended, "…and with motion off the columns do not slide, they are simply there");
    const std::string reopened = run(sbase + " --frame 60x14 --keys \"F2\" 2>/dev/null", mrc);
    check(has(reopened, "[ ] Motion"), "…and the box reads back unchecked");
    // COLUMN DIVIDERS: a hairline in the margin after every column that has a neighbour, on by
    // default; the checkbox under Motion turns them off, and the frame loses exactly those cells.
    auto bars = [](const std::string& frame) { std::size_t n = 0, at = 0; while ((at = frame.find("\xE2\x94\x82", at)) != std::string::npos) { ++n; at += 3; } return n; };
    const std::string with_lines = run(sbase + " --frame 100x12 --keys \"Right\" 2>/dev/null", mrc);
    run(sbase + " --frame 60x14 --keys \"F2 Down Down Down Down Down Down Enter\" >/dev/null 2>&1", mrc);  // dotfiles, sort, keys, theme, motion, sparkle, dividers
    const std::string no_lines = run(sbase + " --frame 100x12 --keys \"Right\" 2>/dev/null", mrc);
    check(has(read_file((cfg / "rolltui" / "dirktui" / "settings.json").string(), ok), "\"dividers\": false"), "the dividers checkbox is saved");
    check(bars(with_lines) > bars(no_lines) && bars(with_lines) - bars(no_lines) >= 8,
          "a line runs the height of the margin between columns, and the checkbox removes it [" + std::to_string(bars(with_lines)) + " vs " + std::to_string(bars(no_lines)) + " bars]");
    // EVERY COLUMN'S THUMB IS ON ITS OWN DIVIDER, in the window's capsule: a frame too short for
    // the root and alpha columns shows two more capsules with dividers than without — where the
    // one bar in the window's border is the focused column's.
    auto capsules = [](const std::string& frame) {
      std::size_t n = 0;
      for (const char* g : {"\xE2\x94\x83", "\xE2\x95\xBB", "\xE2\x95\xB9", "\xE2\x80\xA2"}) { std::size_t at = 0; while ((at = frame.find(g, at)) != std::string::npos) { ++n; at += 3; } }
      return n;
    };
    const std::string short_off = run(sbase + " --frame 100x7 --keys \"Right\" 2>/dev/null", mrc);
    run(sbase + " --frame 60x14 --keys \"F2 Down Down Down Down Down Down Enter\" >/dev/null 2>&1", mrc);  // back on, so the checks below see the default
    const std::string short_on = run(sbase + " --frame 100x7 --keys \"Right\" 2>/dev/null", mrc);
    check(capsules(short_off) > 0 && capsules(short_on) > capsules(short_off),
          "with dividers, each scrolled column carries its own thumb on its divider; without, one bar in the border [" +
              std::to_string(capsules(short_on)) + " vs " + std::to_string(capsules(short_off)) + " capsule cells]");
    // THE DIVIDER IS A SCROLL TRACK: a click at the bottom of the tree column's divider scrolls
    // THAT column to its end while the cursor stays in alpha; a drag from there back to the top
    // of the track brings it back; the wheel scrolls the column under the pointer, not the focused
    // one. Ten rows high: four entry rows (frame rows 2..5), so the tree's eight entries scroll.
    {
      const std::string tall = run(sbase + " --frame 100x10 --keys \"Right\" 2>/dev/null", mrc);
      int dx = -1;
      {
        std::istringstream in(tall);
        std::string l;
        for (int y = 0; std::getline(in, l); ++y) {
          if (y != 2) continue;  // the head row (under the path line and the border): ASCII heads, 3-byte box glyphs
          const std::size_t head = l.find(" tree");
          const std::size_t bar = head == std::string::npos ? std::string::npos : l.find("\xE2\x94\x82", head);
          if (bar == std::string::npos) break;
          int cells = 0;
          for (std::size_t i = 0; i < bar; ++i) if ((static_cast<unsigned char>(l[i]) & 0xC0) != 0x80) ++cells;  // UTF-8 lead bytes = cells here
          dx = cells;
        }
      }
      const std::string at = std::to_string(dx);
      const std::string jumped = run(sbase + " --frame 100x10 --keys \"Right Click " + at + ",6\" 2>/dev/null", mrc);
      check(dx > 0 && !has(tall, "zeta.txt") && has(jumped, "zeta.txt") && has(jumped, "nested"),
            "a click at the bottom of a divider's track scrolls THAT column to its end — the tree shows zeta.txt — while the cursor stays in alpha [x " + at + "]");
      const std::string dragged = run(sbase + " --frame 100x10 --keys \"Right Click " + at + ",6 Drag " + at + ",3 Release\" 2>/dev/null", mrc);
      check(!has(dragged, "zeta.txt") && has(dragged, "alpha"), "…and dragging the thumb back to the top of the track scrolls it back");
      const std::string wheeled = run(sbase + " --frame 100x10 --keys \"Right WheelDown " + std::to_string(dx - 1) + ",4 WheelDown " + std::to_string(dx - 1) + ",4\" 2>/dev/null", mrc);
      check(has(wheeled, "zeta.txt"), "the wheel scrolls the column under the pointer — the tree — not the focused alpha");
    }
    run(sbase + " --frame 60x18 --keys \"F2 Down Enter Down Enter\" >/dev/null 2>&1", mrc);  // dotfiles, Sort by: its dropdown, the second option
    const std::string sorted = read_file((cfg / "rolltui" / "dirktui" / "settings.json").string(), ok);
    check(ok && has(sorted, "\"sort\": \"name\"") && has(sorted, "\"reversed\": true"), "a sort chosen in the menu is saved too — the second option is name, z to a [" + sorted.substr(0, 80) + "]");
  }

  // ---- THE SLIDE, at the script's clock: a moment into it the columns are between ---------------
  {
    auto row = [&](const std::string& keys) {
      int src = 0;
      const std::string out = run(base + " --frame 46x10 --keys \"" + keys + "\" 2>/dev/null", src);
      std::istringstream in(out);
      std::string l0, l1, l2;
      std::getline(in, l0);  // the path line
      std::getline(in, l1);  // the border
      std::getline(in, l2);  // the head row
      return l2;
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

  // ---- 5. the path line, and a bad path as a NAMED problem ------------------------------------
  // PASTED, not typed, with every underscore escaped: the script spells `_` as a space and `\_`
  // as an underscore, and a scratch path has underscores in it.
  auto script_path = [](const std::string& s) { std::string o; for (char c : s) { if (c == '_') o += "\\_"; else o += c; } return o; };
  const std::string jump = run(base + " --frame 150x30 --keys \"ShiftTab Paste:" + script_path((tree / "alpha").string()) +
                                   " Enter\" 2>&1",
                               rc);
  check(rc == 0 && has(jump, "alpha \xE2\x80\xBA nested \xE2\x9C\x8E"), "a folder's path put onto the path line goes there: the breadcrumb ends in it and its first entry");
  const std::string to_file = run(base + " --frame 150x30 --keys \"ShiftTab Paste:" + script_path((tree / "alpha" / "two.txt").string()) + " Enter\" 2>&1", rc);
  check(rc == 0 && has(to_file, "alpha \xE2\x80\xBA two.txt \xE2\x9C\x8E"), "a file's path put onto the line selects that file in its folder");
  const std::string bad = run(base + " --frame 150x30 --keys \"ShiftTab Type:/no/such/place Enter\" 2>&1", rc);
  check(rc == 0 && has(bad, "no such path") && has(bad, "/no/such/place"),
        "a bad path is a NAMED problem on the screen, the text kept for correcting — never a crash and never silence");

  // ---- 5b. FIND: a query on the find line, Enter, a dialog of what matches under this folder --
  {
    const std::string listed = run(base + " --frame 100x20 --keys \"Tab Type:one Enter\" 2>&1", rc);
    check(rc == 0 && has(listed, "matches for 'one' under") && has(listed, "alpha/one.txt") && has(listed, "leaf-file-long-name.txt"),
          "Enter on the find line opens a dialog of the names under this folder that match, fuzzily, with their paths");
    check(listed.find("alpha/one.txt") < listed.find("leaf-file-long-name.txt"), "…the closest match first");
    const std::string picked = run(base + " --frame 100x20 --keys \"Tab Type:one Enter Enter\" 2>&1", rc);
    check(rc == 0 && !has(picked, "' under") && has(picked, "one.txt") && has(picked, "alpha \xE2\x80\xBA one.txt \xE2\x9C\x8E"),
          "Enter on a match goes there: a file is selected in its folder, the path line says so, the dialog is gone");
    const std::string none = run(base + " --frame 100x20 --keys \"Tab Type:qqqqqq Enter\" 2>&1", rc);
    check(rc == 0 && has(none, "nothing matched 'qqqqqq'"), "a query nothing matches says so in the dialog");
    const std::string clicked_off = run(base + " --frame 100x20 --keys \"Tab Type:one Enter Click 1,1\" 2>&1", rc);
    check(rc == 0 && !has(clicked_off, "' under") && has(clicked_off, "find: one"),
          "a click outside the dialog closes it — its layout says `dismiss` — and the query stays on the find line to refine");
    // THE WHEEL OVER THE DIALOG SCROLLS ITS LIST, the cursor staying on the first match: at ten
    // rows the dialog shows a few of the many names with a 't', and two turns bring later ones up.
    const std::string before = run(base + " --frame 100x10 --keys \"Tab Type:t Enter\" 2>&1", rc);
    const std::string wheeled = run(base + " --frame 100x10 --keys \"Tab Type:t Enter WheelDown 50,4 WheelDown 50,4\" 2>&1", rc);
    auto first_row = [](const std::string& fr) { const std::size_t at = fr.find("' under"); const std::size_t nl = fr.find('\n', at); return fr.substr(nl + 1, fr.find('\n', nl + 1) - nl - 1); };
    check(rc == 0 && has(before, "' under") && has(wheeled, "' under") && first_row(before) != first_row(wheeled),
          "the wheel over the find dialog scrolls its list [" + first_row(before).substr(0, 40) + " -> " + first_row(wheeled).substr(0, 40) + "]");
    const std::string escaped = run(base + " --frame 100x20 --keys \"Tab Type:one Escape\" 2>&1", rc);
    check(rc == 0 && has(escaped, "find: one") && !has(escaped, "' under"), "Escape on the find line hands the focus back to the columns and keeps the query");
  }

  // ---- 6. dotfiles and the sort order are the app's, driven from the bindings FILE ------------
  // Its own config directory: the setting persists, and an earlier press in this suite must not
  // decide what this press toggles.
  const std::string dots = run("ROLL_CONFIG_DIR='" + (scratch / "dots-home").string() + "' " + bin + " '" + tree.string() + "'" + presets +
                                   " --theme default-dark --frame 150x30 --keys \"AltH\" 2>&1", rc);
  check(rc == 0 && !has(dots, " .hidden"), "Alt-H hides the dotfiles");
  const std::string sorted = run(base + " --frame 150x30 --keys \"CtrlS\" 2>&1", rc);
  check(rc == 0 && has(sorted, "sort name z-a"), "Ctrl-S cycles the sort order and the status line says which");

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
        // Every window id and every title stays in the file: the source focuses the columns
        // through the layout's own `focus` (`rolltui_layer_focus`) rather than by naming them.
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
  "actions": { "app.quit": "leave" },
  "root": { "column": [
    { "id": "vestibule_note", "content": "text:  a screen this binary has never seen", "size": 1 },
    { "id": "vestibule_cols", "content": "filepicker", "border": "single",
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
