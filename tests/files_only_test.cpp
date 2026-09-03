//
// files_only_test.cpp — THE PHASE 10 PROOF (plan/phase-10.md, milestone 6): a screen
// that exists only as files runs in a host that has never heard of it.
//
// Everything the screen is lives in rolltui/tests/fixtures/screen/, which this test
// copies into a scratch preset directory before running the REAL rolltui-studio
// binary against it:
//
//   layouts/kettle.json    the design — a text: window, a file: window, a menu: window
//                          and a help window, plus the ONE action the screen emits
//   menus/kettle.json      the menu that window shows; its first row NAMES app.kettle
//   bindings/kettle.json   the keys — it is the only thing that gives app.kettle a chord
//   docs/kettle.md         the document the file: window reads
//   layouts/kettle-silent.json   the same screen declaring NO actions, for the VERIFY rung
//
// Nothing here is a new capability: m2 put widgets behind kinds, m3 made menus files,
// m4 gave layouts their actions and m5 the editor. This milestone is the claim that the
// four compose — so the assertions below are deliberately end-to-end and the controls
// are what carry the weight:
//
//   THE SOURCE CONTROL. The word "kettle" appears in no host or library source file.
//   It is the layout name, the menu name, the document, all four window ids and the
//   action, so one grep covers every name the screen uses. That is also why the screen
//   is called that: a control that greps for one token needs a token the codebase would
//   never write for another reason ("brew" collides with Hebrew_Letter; "proof" appears
//   in prose).
//
//   THE BINDINGS CONTROL. The same layout is run WITHOUT --bindings kettle. The menu row
//   then shows no chord and the app scope is empty, so the Ctrl-J in the golden is proof
//   the FILE supplied it and not some default.
//
//   THE DECLARATION CONTROL (kettle-silent). A layout that declares nothing leaves the
//   menu item naming an action nothing declares: reported by name in the status line,
//   and its shortcut GONE — a key that cannot fire is never advertised.
//
//   THE DROP-IN CONTROL. A fifth file nobody has ever run is written into the scratch
//   directory mid-test and opened by the same binary, unrebuilt.
//
// Re-recording the frames is a deliberate human act — `rolltui-files-only-test --record`
// — never something ctest does; look at them before committing.
//
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/Unicode.hpp"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_STUDIO_BIN
#error "ROLLTUI_STUDIO_BIN must name the studio binary"
#endif
#ifndef ROLLTUI_FIXTURE_DIR
#error "ROLLTUI_FIXTURE_DIR must point at rolltui/tests/fixtures"
#endif
#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

namespace fs = std::filesystem;

namespace {

std::string run(const std::string& cmd, int& rc) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) { rc = -1; return out; }
  char buf[4096];
  std::size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
  rc = pclose(p);
  return out;
}

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool has(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

// The studio's own status bar is the frame's last row.
std::string status_line(const std::string& frame) {
  std::vector<std::string> rows;
  std::istringstream in(frame);
  std::string row;
  while (std::getline(in, row)) rows.push_back(row);
  return rows.empty() ? "" : rows.back();
}

}  // namespace

int main(int argc, char** argv) {
  const bool record = (argc > 1 && std::strcmp(argv[1], "--record") == 0);
  const std::string frames = std::string(ROLLTUI_FIXTURE_DIR) + "/frames/";
  const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md";

  // ---- the scratch preset directory: the screen's four files and nothing else --------
  const char* tmp = std::getenv("TMPDIR");
  const std::string scratch = std::string(tmp && *tmp ? tmp : "/tmp") + "/rolltui_files_only_" + std::to_string(::getpid());
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  const std::string presets = scratch + "/presets";
  std::error_code ec;
  fs::copy(std::string(ROLLTUI_FIXTURE_DIR) + "/screen", presets, fs::copy_options::recursive, ec);
  check(!ec, "copied the screen's files into a scratch preset directory (" + presets + ")");
  check(fs::exists(presets + "/layouts/kettle.json") && fs::exists(presets + "/menus/kettle.json") &&
            fs::exists(presets + "/bindings/kettle.json") && fs::exists(presets + "/docs/kettle.md"),
        "…all four of them: a layout, a menu, a bindings file and a document");

  // The startup trace ("no working copy; started from the shipped 'default'") is stderr
  // and expected; keep it out of the frames and out of ctest's output.
  const std::string err = " 2>>'" + scratch + "/stderr.txt'";
  auto play = [&](const std::string& args) {
    return std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + fixture + "' --theme default-dark --presets '" + presets +
           "' " + args + err;
  };

  struct Case {
    const char* name;
    const char* args;
  };
  const Case cases[] = {
      // The screen itself: a text: banner, a file: document, a menu: over a file, and a
      // help window rendered from the live table — none of them bound by any host.
      {"files-only.80x24", "--frame 80x24 --layout kettle --bindings kettle"},
      {"files-only.120x40", "--frame 120x40 --layout kettle --bindings kettle"},
      // The menu file navigates with no host code at all: Down, then Enter descends.
      {"files-only.80x24.menu", "--frame 80x24 --layout kettle --bindings kettle --keys \"Down Enter\""},
      // Tab focuses the help window and six pages down reach the app scope — one action,
      // declared by a layout file, described by it, keyed by a bindings file.
      {"files-only.80x24.app-scope",
       "--frame 80x24 --layout kettle --bindings kettle --keys \"Tab PageDown PageDown PageDown PageDown PageDown PageDown\""},
      // The SAME screen reached by switching layouts at runtime (F2 › Layout › kettle),
      // which is the only path that can accumulate declarations: the app scope must still
      // be this layout's one action and not also the five the layout we started on
      // declared. Loading a bindings file rebuilds the table, so the launch-time case
      // above cannot tell an authoritative declare() from an additive one — this one can.
      {"files-only.80x24.switched",
       "--frame 80x24 --layout default --bindings kettle --keys \"F2 Type:lay Enter Type:kettle Enter Escape Tab "
       "PageDown PageDown PageDown PageDown PageDown PageDown\""},
      // The VERIFY rung: a layout declaring nothing. The menu item's action is reported
      // by name in the status line and its shortcut is gone.
      {"files-only.100x14.silent", "--frame 100x14 --layout kettle-silent --bindings kettle"},
      // The standing rule (the user, 2026-09-01): every view shrinks to 1 or 0 cells in
      // either dimension and stays graceful — with the menu driven.
      {"files-only.20x6", "--frame 20x6 --layout kettle --bindings kettle --keys \"Down Enter\""},
      {"files-only.1x1", "--frame 1x1 --layout kettle --bindings kettle --keys \"Down Enter Tab\""},
  };

  std::string screen, menu_open, app_scope, switched, silent;
  for (const Case& c : cases) {
    int rc = 0;
    const std::string out = run(play(c.args), rc);
    check(rc == 0 && !out.empty(), std::string(c.name) + ": the studio ran (rc " + std::to_string(rc) + ", " +
                                       std::to_string(out.size()) + " bytes)");
    // Every row fits the frame, in cells — the same guard the golden harness applies.
    const int w = std::atoi(std::strstr(c.args, "--frame ") + 8);
    const int h = std::atoi(std::strchr(std::strstr(c.args, "--frame "), 'x') + 1);
    int rows = 0;
    bool fits = true;
    std::istringstream in(out);
    std::string row;
    while (std::getline(in, row)) {
      ++rows;
      if (rolltui::unicode::display_width(row) > w) {
        fits = false;
        check(false, std::string(c.name) + ": row wider than " + std::to_string(w) + ": [" + row + "]");
      }
    }
    check(fits && rows == h, std::string(c.name) + ": " + std::to_string(rows) + " rows of at most " + std::to_string(w) + " cells");

    const std::string path = frames + c.name + ".txt";
    if (record) {
      std::ofstream(path, std::ios::binary) << out;
      std::printf("  recorded %s\n", path.c_str());
    } else {
      bool ok = false;
      const std::string want = read_file(path, ok);
      check(ok, std::string(c.name) + ": golden exists (" + path + ")");
      if (ok && out != want) {
        // Name the first differing row rather than dumping two frames.
        std::istringstream a(out), b(want);
        std::string ra, rb;
        int line = 0;
        while (std::getline(a, ra) && std::getline(b, rb)) {
          ++line;
          if (ra != rb) break;
        }
        check(false, std::string(c.name) + ": frame differs from the golden at row " + std::to_string(line) + "\n    got  [" +
                         ra + "]\n    want [" + rb + "]");
      } else if (ok) {
        check(true, std::string(c.name) + ": frame matches the golden");
      }
    }
    const std::string n = c.name;
    if (n == "files-only.80x24") screen = out;
    if (n == "files-only.80x24.menu") menu_open = out;
    if (n == "files-only.80x24.app-scope") app_scope = out;
    if (n == "files-only.80x24.switched") switched = out;
    if (n == "files-only.100x14.silent") silent = out;
  }

  // ---- what the golden alone does not say --------------------------------------------
  {
    // Each of the four kinds actually drew its own content, and the window note is empty
    // (an unknown kind, an unbound source or an unreadable file: would put it in the
    // status line as "[...]" and draw an error panel instead — m2).
    check(has(screen, "a screen with no code behind it"), "the text: window drew the layout file's own literal");
    check(has(screen, "A screen that exists only as files."), "the file: window drew docs/kettle.md");
    check(has(screen, "Put the kettle on") && has(screen, "About this screen"),
          "the menu: window drew menus/kettle.json's items");
    check(has(screen, "send the line (always Enter)"), "the help window drew the live key table, bound to nothing");
    check(!has(status_line(screen), "["), "…and no window reported a problem [" + status_line(screen) + "]");
    check(has(status_line(screen), " kettle ") && has(status_line(screen), "focus:kettle_menu"),
          "the layout file named the screen and chose the focused window [" + status_line(screen) + "]");

    // The three files meeting in one row: the LAYOUT declares app.kettle, the BINDINGS
    // file gives it Ctrl-J, the MENU file names it, and the row is rendered from the live
    // chords (m4) rather than from any "shortcut" string in the file.
    check(has(screen, "Put the kettle on       Ctrl-J"),
          "the menu row carries the chord the bindings file gave the action the layout declared");
    check(has(app_scope, "Ctrl-J      put the kettle on"),
          "…and help renders the same action with the layout file's own description");
    // Exactly one app action: the app scope is this screen's, not this screen's plus the
    // last one's (declare() is authoritative — the m6 fix). The help window prints one
    // "scope:" header per scope with its actions under it, so the distance from the "app:"
    // header to the next header IS the size of the scope.
    auto app_scope_size = [](const std::string& frame, std::string& action_row) {
      std::vector<std::string> rows;
      std::istringstream in(frame);
      std::string line;
      while (std::getline(in, line)) rows.push_back(line);
      int at_app = -1;
      for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (at_app < 0 && has(rows[i], "app:")) { at_app = i; continue; }
        if (at_app >= 0 && has(rows[i], "editor:")) {
          if (i == at_app + 2) action_row = rows[at_app + 1];
          return i - at_app - 1;
        }
      }
      return -1;
    };
    std::string action_row;
    check(app_scope_size(app_scope, action_row) == 1 && has(action_row, "put the kettle on"),
          "the app scope is one action long — this layout's, and no other layout's [" + action_row + "]");
    // The discriminating one: reached by SWITCHING layouts at runtime, where an additive
    // declare() would leave the previous screen's five app actions live beneath this one.
    std::string switched_row;
    check(app_scope_size(switched, switched_row) == 1 && has(switched_row, "put the kettle on"),
          "…and still one action after switching to this layout at runtime, not six [" +
              std::to_string(app_scope_size(switched, switched_row)) + "]");
    check(has(status_line(switched), " kettle ") && !has(switched, "open the settings and commands menu"),
          "…the screen we switched away from left no action of its own behind");

    // Menu navigation, with nothing in the host knowing this tree.
    check(has(menu_open, "commands › About this screen") && has(menu_open, "layouts/kettle.json") &&
              has(menu_open, "bindings/kettle.json"),
          "Down + Enter descends a level of a menu that is only a file");
  }

  // ---- control: the bindings FILE is what supplies the chord --------------------------
  {
    int rc = 0;
    const std::string out = run(play("--frame 80x24 --layout kettle"), rc);  // no --bindings
    check(rc == 0 && has(out, "Put the kettle on") && !has(out, "Ctrl-J"),
          "without bindings/kettle.json the same row shows NO chord — the file is what bound it");
  }

  // ---- control: a layout that declares nothing (VERIFY) -------------------------------
  {
    check(has(silent, "Put the kettle on") && !has(silent, "Ctrl-J"),
          "a layout declaring no actions leaves the item with no shortcut: an inert key is never advertised");
    check(has(status_line(silent), "[window 'kett"),
          "…and the status line carries a note about that window [" + status_line(silent) + "]");
    // The whole note names the item AND the action, but it also carries the menu file's
    // absolute path (which rung answered), so it can only be read at a width no golden
    // should be recorded at — this run is the assertion, not a frame. The width is
    // derived from the scratch path so a long TMPDIR cannot clip the part being asserted.
    int rc = 0;
    const std::string wide =
        run(play("--frame " + std::to_string(presets.size() + 300) + "x8 --layout kettle-silent --bindings kettle"), rc);
    check(rc == 0 && has(wide, "item 'boil' names the action 'app.kettle', which no layout declares"),
          "…reported by item and by action, the loaders' standard — never silently ignored");
  }

  // ---- control: a file dropped in afterwards, same binary -----------------------------
  {
    std::ofstream(presets + "/menus/second.json")
        << R"({"id":"root","label":"a menu written after the binary was built","items":[{"id":"a","label":"row one"}]})";
    std::ofstream(presets + "/layouts/second.json") << R"({"name":"second","min_width":0,"min_height":0,
        "actions":{},"root":{"column":[
          {"id":"second_menu","content":"menu:second","border":"single","title":"menus/second.json","focusable":true}]}})";
    int rc = 0;
    const std::string out = run(play("--frame 60x8 --layout second --bindings kettle"), rc);
    check(rc == 0 && has(out, "a menu written after the binary was built") && has(out, "row one") && !has(status_line(out), "["),
          "a layout and a menu written after the binary was built open in it, unrebuilt");
  }

  // ---- THE control: no host or library source knows this screen -----------------------
  // One token covers the layout name, the menu name, the document, the four window ids
  // and the action, so a single grep answers "did any code have to change for this?".
  // Everything the studio binary is compiled from is scanned: the library and its
  // tools. (Tests, fixtures, vendored code and the generated Unicode tables are not
  // compiled into a host's screen and are excluded by directory.)
  {
    std::vector<std::string> scanned, hits;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
      const std::string p = e.path().string();
      const std::string rel = p.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
      if (rel.rfind("tests/", 0) == 0 || rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0 ||
          rel.rfind("presets/", 0) == 0)
        continue;
      const std::string ext = e.path().extension().string();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
      scanned.push_back(rel);
      bool ok = false;
      const std::string text = read_file(p, ok);
      if (!ok) continue;
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (line.find("kettle") != std::string::npos) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(scanned.size() >= 30, "scanned every source the studio binary is built from (" +
                                    std::to_string(scanned.size()) + " files)");
    check(hits.empty(), "no source of the library or its hosts names this screen — it is files all the way down" +
                            (hits.empty() ? "" : ": " + hits.front()));
  }

  fs::remove_all(scratch);
  return report("rolltui files_only_test");
}
