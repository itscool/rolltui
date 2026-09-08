//
// explorer_test.cpp — the FOURTH CONSUMER, driven headlessly.
//
// `rolltui-explorer` is a read-only column-view file browser whose custom widget is the first
// in this repo with internal structure the library does not model. This suite runs the real
// binary against a FIXTURE TREE it builds under $TMPDIR — never the machine's own files, so a
// frame is a pure function of the fixture — and asserts what the app does, plus the two
// controls that make the screen's file-ness checkable.
//
// WHY THESE ARE CONTENT ASSERTIONS AND NOT BYTE GOLDENS, decided here with its reason: the
// details page renders a file's real size and modification time, which no fixture can make
// byte-stable across machines or checkouts. A golden would either exclude the app's own richest
// window or be re-recorded on every clone. The frames are asserted on what must be in them, and
// `studio_golden_test` keeps the byte-level guarantee for the library's own rendering.
//
#include <sys/stat.h>
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
namespace fs = std::filesystem;

#ifndef ROLLTUI_EXPLORER_BIN
#error "ROLLTUI_EXPLORER_BIN must name the explorer binary"
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
  const fs::path scratch = fs::path(tmp && *tmp ? tmp : "/tmp") / ("rolltui-explorer-test-" + std::to_string(getpid()));
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

  const std::string bin = std::string("'") + ROLLTUI_EXPLORER_BIN + "'";
  const std::string presets = std::string(" --presets '") + ROLLTUI_EXAMPLES_DIR + "/presets'";
  const std::string base = bin + " '" + tree.string() + "'" + presets + " --theme default-dark";

  // ---- 0. THE LIBRARY'S EDITORS ARE THIS APP'S TOO -----------------------------------------
  // A kind is the library's; a STORE is what makes it an app's. Before this app opened one, the
  // theme and keys editors existed and had nothing to edit here — "built into any rolltui app"
  // was true of the library and false of every app but two.
  {
    int erc = 0;
    const std::string th = run(bin + " '" + tree.string() + "' --frame 80x14 --keys \"F4\" 2>&1", erc);
    check(has(th, "theme editor") && has(th, "Roles"), "F4 opens the theme editor in the explorer");
    const std::string ke = run(bin + " '" + tree.string() + "' --frame 80x14 --keys \"F5\" 2>&1", erc);
    check(has(ke, "keys editor") && has(ke, "Actions by scope"), "F5 opens the keys editor in the explorer");
    check(!has(th, "keys editor") && !has(ke, "theme editor"), "…and each chord opens its own, not the other");
  }

  // ---- 0a. THE PRODUCT BINARY CANNOT TEST ITSELF -------------------------------------------
  // Additive, not compiled out: rolltui-explorer-selftest is this same source plus the script
  // vocabulary, and the shipped binary simply does not contain it. Asserted on the ARTIFACT
  // rather than on the source, because what ships is a binary and that is what the claim is
  // about. TripleClick is a marker the vocabulary owns; a key NAME like PageDown would not
  // discriminate, since the library's own key table carries those and both binaries link it.
  {
    int prc = 0;
    const std::string product = std::string("'") + ROLLTUI_EXPLORER_PRODUCT_BIN + "'";
    const std::string refused = run(product + " --frame 40x6 2>&1", prc);
    check(has(refused, "usage:") && !has(refused, "--frame"),
          "the shipped explorer refuses --frame and does not advertise it");
    const std::string in_product = run("strings " + product + " | grep -cx TripleClick", prc);
    const std::string in_selftest = run("strings " + bin + " | grep -cx TripleClick", prc);
    check(in_product.substr(0, 1) == "0", "…and the script vocabulary is absent from the shipped binary [" + in_product.substr(0, 3) + "]");
    check(in_selftest.substr(0, 1) != "0", "…while the self-test binary has it, so the marker discriminates");
  }

  // ---- 0. it runs BARE, and a miss names every path it tried --------------------------------
  // The app keeps its screen in files rather than in its source, so with no --presets it has to
  // ask where its own files are. It used to build "/layouts/explorer.json" from an empty string
  // and print "no layout ()" — a message naming neither what it wanted nor where it looked.
  {
    int brc = 0;
    const std::string bare = run(bin + " '" + tree.string() + "' --frame 70x10 2>&1", brc);
    check(!has(bare, "no layout") && !has(bare, "cannot load"),
          "the explorer runs with NO arguments: it finds its own embedded layout [" + bare.substr(0, 60) + "]");
    check(has(bare, "columns"), "…and draws its own screen, whose title lives only in its layout file");

    int mrc = 0;
    const std::string miss = run(bin + " '" + tree.string() + "' --presets '/nonexistent-xyz' --frame 40x6 2>&1", mrc);
    check(has(miss, "cannot load its layout"), "a miss says what it could not load [" + miss.substr(0, 50) + "]");
    check(has(miss, "tried:") && has(miss, "/nonexistent-xyz/layouts/explorer.json"),
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
    const std::string err = run(bin + " '" + bad + "' --frame 60x10 2>&1 1>/dev/null", drc);
    check(err.find("must provide") == std::string::npos,
          "a missing directory is NOT reported as a capability gap [" + err.substr(0, 60) + "]");
    const std::string shown = run(bin + " '" + bad + "' --frame 150x10 2>/dev/null", drc);
    check(has(shown, "cannot open " + bad),
          "…the panel says it cannot open the path IN FULL — an error is not a filename and gets the "
          "panel width, because a column sized for names truncates it to nothing useful");
    check(!has(shown, "(empty)"),
          "…and a directory that CANNOT BE OPENED does not look like an EMPTY one — both have no "
          "entries, and drawing the same thing for both is a wrong answer reporting itself as success");
    const std::string ok_empty = run(bin + " '" + (tree / "empty-dir").string() + "' --frame 60x10 2>/dev/null", drc);
    check(has(ok_empty, "(empty)"), "…while a genuinely empty directory still says so, so the two are distinguishable");
  }

  // ---- 1. it renders a directory ------------------------------------------------------------
  int rc = 0;
  const std::string wide = run(base + " --frame 150x30 2>&1", rc);
  check(rc == 0 && !wide.empty(), "the explorer renders a directory headlessly (rc " + std::to_string(rc) + ")");
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
        for (const char* w : {"\"columns\"", "go to", "the live key table", "the vestibule"})
          if (line.find(w) != std::string::npos) hits.push_back(rel + ":" + std::to_string(ln) + ": " + w);
      }
    }
    check(scanned >= 30, "scanned every source the explorer is built from (" + std::to_string(scanned) + " files)");
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
    const std::string v = run(bin + " '" + tree.string() + "' --presets '" + own.string() +
                                  "' --layout vestibule --theme default-dark --frame 90x20 2>&1",
                              vrc);
    check(vrc == 0 && has(v, "the vestibule") && has(v, "never seen") && has(v, "alpha"),
          "a layout written after the build opens in the app, with its own ids and title");
  }

  fs::remove_all(scratch);
  return report("rolltui explorer_test");
}
