//
// from_nothing_test.cpp — BUILD AN APP FROM NOTHING, the third and last
// of this family, and the one that finally says the whole sentence.
//
// The claim: a person can build an app's whole screen in the designer, starting from an empty
// directory, with only custom widgets and custom bindings falling short of a true preview. The
// two tests before it each proved half:
//
//   files_only_test      A SCREEN CAN BE FILES. Six files a person hand-wrote; no source names
//                        them; the binary is not rebuilt.
//   authored_screen_test THE FILES CAN BE AUTHORED IN A TOOL THAT IS NOT THE APP. The designer
//                        writes one layout for `rolltui-paint`, which runs it.
//
// Both start from something. `files_only` starts from files somebody typed in an editor;
// `authored_screen` starts from a preset directory that already has a theme and bindings in it,
// authors ONE of the four file types, and opens a document to look at while it works. **This
// one starts from an empty directory and no document, and comes out with all four.**
//
//   NOTHING IN                    an empty --presets directory, and no FIXTURE.md argument
//   ONE PROCESS, KEYSTROKES ONLY  themes/sundial.json   the theme editor's seeded generator
//                                 layouts/sundial.json  the layout editor, two named windows
//                                 menus/sundial.json    THE MENU EDITOR — new at Phase 27 m2
//                                 bindings/sundial.json the keys editor, one chord
//   A HOST OUT                    the same binary, told only the four names, draws the screen
//
// **THE FOUR FILES INTERLOCK, AND THAT IS THE ASSERTION.** One row of the rendered screen reads
// `read the shadow               Ctrl-G`. Every one of the four wrote part of it: the LAYOUT
// declared `app.sundial`, the MENU gave an item that name, the BINDINGS gave that name Ctrl-G,
// and the THEME coloured it. No single file can produce that row, and no source can.
//
// THE CONTROLS, and they are the ones the two siblings already use:
//
//   THE SOURCE CONTROL. The word "sundial" appears in no source the binary is built from —
//   `.c` included, which matters now the library is C. It is the theme, the layout, the menu,
//   the bindings preset, both window ids and the action, so one grep covers every name the
//   screen has. Chosen for the same reason "kettle" was: a token this codebase would never
//   write for another reason.
//
//   THE UNMOUNTED-EDITOR CONTROL, which is what makes this a test of the MENU EDITOR rather
//   than a test of four editors in a row. The IDENTICAL keystrokes run against a designer that
//   did not mount one (`ROLLTUI_NO_MENU_EDITOR`, which drops `editor.menu` from the tools it
//   declares — the library's own mounting mechanism, not a test hook) produce THREE files, and
//   the screen then says `[no menu file 'sundial']` where the menu was. Before Phase 27 m2 that
//   was the only outcome available, and the fourth file had to be hand-written JSON.
//
//   THE NO-DOCUMENT CONTROL. The studio is started with no fixture at all, which it refused to
//   do until this milestone (`if (app.fixture_path.empty()) return usage();`). A design tool
//   whose job is building an app from nothing could not itself start from nothing.
//
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_STUDIO_BIN
#error "ROLLTUI_STUDIO_BIN must name the studio binary"
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

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string status_line(const std::string& frame) {
  std::vector<std::string> rows;
  std::istringstream in(frame);
  std::string row;
  while (std::getline(in, row)) rows.push_back(row);
  return rows.empty() ? "" : rows.back();
}

// THE AUTHORING, in four segments, one per file — every keystroke a thing a person does.
//
// `_` is a space and `\_` a literal underscore in a `Type:` token. The escape was added for
// this test: `Type:sundial_face` silently produced `sundialface`, because a Name field refuses
// a space and an identifier is exactly what you type into one.
//
// AN INPUT COMMIT IS FOLLOWED BY AN ESCAPE AND A TREE REPLACEMENT IS NOT, which is the same
// rule `authored_screen_test` states: Escape clears the menu's filter if there is one and
// closes the editor if there is not, and an operation that REPLACES the tree ("New menu", "New
// layout", "Generate") rebuilds the menu and clears the filter on its way.
//
// The MENU comes first deliberately, so the unmounted-editor control's stray keystrokes land
// in the prompt where they are inert, instead of in whichever editor happened to be open.
const char* kMenu =
    "F8 Type:new_menu Enter Type:sundial Enter "
    "Tab "
    "Type:item_id Enter CtrlU Type:face Enter Escape "
    "Type:label Enter CtrlU Type:read_the_shadow Enter Escape "
    "Type:action_it Enter Type:app.sundial Enter Escape "
    "Type:save_menu Enter Type:sundial Enter";

const char* kLayout =
    "F6 Type:new_layout Enter Type:sundial Enter "
    "Type:split_into_a_row Enter "
    "Type:node_id Enter CtrlU Type:sundial\\_face Enter Escape "
    "Type:widget_kind Enter Type:text Enter Escape "
    "Type:source Enter Type:the_shadow_falls_where_no_code_was_written Enter Escape "
    "Type:title Enter Type:the_face Enter Escape "
    "Tab "
    "Type:node_id Enter CtrlU Type:sundial\\_tools Enter Escape "
    "Type:widget_kind Enter Type:menu Enter Escape "
    "Type:menu_file Enter Type:sundial Enter Escape "
    "Type:title Enter Type:the_tools Enter Escape "
    "Type:actions Enter End Enter Type:app.sundial Enter "
    "Type:app.sundial Enter Enter Type:read_the_shadow Enter Escape Escape "
    "Type:focused_window Enter Type:sundial\\_tools Enter "
    "Type:minimum_width Enter Type:20 Enter Escape "
    "Type:minimum_height Enter Type:6 Enter Escape "
    "Type:save Enter Type:sundial Enter";

// Ctrl-G because no shipped binding uses it, so the chord in the frame can only be this file's.
const char* kKeys =
    "F7 Type:actions_by Enter Type:app Enter Enter "
    "Enter CtrlG "
    "Escape Escape Escape "
    "Type:save_as Enter Type:sundial Enter";

const char* kTheme =
    "F4 Type:generate Enter "
    "Type:seed Enter CtrlU Type:7 Enter Escape "
    "Type:ruleset Enter Type:complementary Enter "
    "Type:generate_ Enter "
    "Type:save_as Enter Type:sundial Enter";

}  // namespace

int main() {
  const char* tmp = std::getenv("TMPDIR");
  const std::string scratch = std::string(tmp && *tmp ? tmp : "/tmp") + "/rolltui_from_nothing_" + std::to_string(::getpid());
  fs::remove_all(scratch);
  const std::string with = scratch + "/with";     // a designer with a menu editor
  const std::string without = scratch + "/without";  // …and one that never mounted it
  fs::create_directories(with);
  fs::create_directories(without);
  const std::string stderr_path = scratch + "/stderr.txt";
  const std::string err = " 2>>'" + stderr_path + "'";

  const std::string script = std::string(kMenu) + " " + kLayout + " " + kKeys + " " + kTheme;
  auto author = [&](const std::string& dir, bool mount_menu_editor, int& rc) {
    return run((mount_menu_editor ? std::string() : std::string("ROLLTUI_NO_MENU_EDITOR=1 ")) + "'" +
                   ROLLTUI_STUDIO_BIN + "' --presets '" + dir + "' --frame 100x30 --keys \"" + script + "\"" + err,
               rc);
  };
  auto play = [&](const std::string& dir, const std::string& args, int& rc) {
    return run(std::string("'") + ROLLTUI_STUDIO_BIN + "' --presets '" + dir +
                   "' --theme sundial --layout sundial --bindings sundial " + args + err,
               rc);
  };

  // ---- 1. nothing in: an empty directory, and no document ------------------------------
  {
    int n = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(with)) { (void)e; ++n; }
    check(n == 0, "the preset directory starts EMPTY — no theme, no layout, no menu, no bindings");
  }
  int rc = 0;
  const std::string authoring = author(with, true, rc);
  check(rc == 0 && !authoring.empty(),
        "the studio started with NO document argument and ran the script (rc " + std::to_string(rc) + ")");
  // THE NO-DOCUMENT CONTROL: it drew a screen, not the usage text it printed until Phase 27 m3.
  check(!has(authoring, "usage: rolltui-studio"),
        "…starting from nothing is not a usage error — the tool that designs from nothing starts from nothing");

  // ---- 2. four files out ----------------------------------------------------------------
  struct Want { const char* rel; const char* what; };
  const Want wanted[] = {
      {"themes/sundial.json", "the theme editor's seeded generator wrote themes/sundial.json"},
      {"layouts/sundial.json", "the layout editor wrote layouts/sundial.json"},
      {"menus/sundial.json", "THE MENU EDITOR wrote menus/sundial.json — the file that had to be hand-written before Phase 27"},
      {"bindings/sundial.json", "the keys editor wrote bindings/sundial.json"},
  };
  for (const Want& w : wanted) check(fs::exists(with + "/" + w.rel), w.what);

  // Each file is read back through the LIBRARY's own loader wherever there is one, so what the
  // editor wrote and what a host reads cannot drift into two spellings of one format.
  bool ok = false;
  const std::string menu_text = read_file(with + "/menus/sundial.json", ok);
  {
    RolltuiMenuItem root{};
    RolltuiMenuLoadReport rep{};
    const int loaded = rolltui_menu_parse_json(menu_text.data(), menu_text.size(), &root, &rep);
    check(loaded && rolltui_menu_load_report_clean(&rep),
          "…and the menu file loads clean through the library's own parser — the editor and the loader are one format");
    const RolltuiStr& act = root.children.v[0]->action_name;
    check(root.children.n == 1 && std::string_view(act.p, act.n) == "app.sundial",
          "…its one item names the action the LAYOUT declares — a name this tool cannot verify, and writes anyway");
    rolltui_menu_load_report_release(&rep);
  }
  const std::string layout_text = read_file(with + "/layouts/sundial.json", ok);
  check(has(layout_text, "\"app.sundial\"") && has(layout_text, "\"focus\": \"sundial_tools\""),
        "the layout declares the action and focuses a window the author NAMED — not the `main-2` the editor "
        "generated, which is the third thing you could not design without writing JSON until this milestone");
  const std::string keys_text = read_file(with + "/bindings/sundial.json", ok);
  check(has(keys_text, "\"app.sundial\"") && has(keys_text, "ctrl+g"),
        "the bindings file gives that same action a chord no shipped default uses");
  const std::string theme_text = read_file(with + "/themes/sundial.json", ok);
  check(has(theme_text, "\"sundial\""), "the theme file carries the name it was saved under");

  // ---- 3. a host runs the four files ----------------------------------------------------
  const std::string screen = play(with, "--frame 78x14", rc);
  check(rc == 0 && !screen.empty(), "a host ran the four files by name alone (rc " + std::to_string(rc) + ")");
  check(has(screen, "the face") && has(screen, "the tools"), "…the two windows carry the titles that were typed");
  // The literal text a `text:` window carries WRAPS in a 78-cell frame, so the assertion is on
  // the first line of it rather than the whole sentence.
  check(has(screen, "the shadow falls where no code was"), "…the text window draws the literal source that was typed");
  // THE INTERLOCK. Four files meet in this one row and no single one of them can produce it.
  check(has(screen, "read the shadow               Ctrl-G"),
        "…and one row is all four files at once: the LAYOUT declared app.sundial, the MENU named it, "
        "the BINDINGS gave it Ctrl-G, and the THEME coloured it");
  check(has(status_line(screen), " sundial ") && has(status_line(screen), "focus:sundial_tools"),
        "…the status line names the authored theme and layout, focused where the layout said");

  // ---- 4. THE UNMOUNTED-EDITOR CONTROL: the same keystrokes, three files ------------------
  const std::string authoring2 = author(without, false, rc);
  check(rc == 0 && !authoring2.empty(), "the identical keystrokes ran against a designer that mounted no menu editor");
  check(fs::exists(without + "/themes/sundial.json") && fs::exists(without + "/layouts/sundial.json") &&
            fs::exists(without + "/bindings/sundial.json"),
        "…the other three editors still wrote their files");
  check(!fs::exists(without + "/menus/sundial.json") && !fs::exists(without + "/menus"),
        "…and NO menu file was written: the fourth file is the menu editor's doing, not some other path's");
  const std::string screen2 = play(without, "--frame 78x14", rc);
  check(rc == 0 && has(screen2, "no menu file \'sundial\'"),
        "…the same screen then says what is missing, by name — which is what a person got before Phase 27 m2");
  check(!has(screen2, "Ctrl-G"),
        "…and the interlocked row is gone with it, though the layout and the bindings are unchanged");

  // ---- 5. THE SOURCE CONTROL: no source the binary is built from knows this screen --------
  {
    std::vector<std::string> scanned, hits;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
      const std::string path = e.path().string();
      const std::string rel = path.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
      if (rel.rfind("tests/", 0) == 0 || rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0 ||
          rel.rfind("presets/", 0) == 0)
        continue;
      const std::string ext = e.path().extension().string();
      // `.c` IS SCANNED, and it is most of the library since the Phase 15 port. A control that
      // greps only the C++ would now be looking at the shell rather than the thing.
      if (ext != ".c" && ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
      scanned.push_back(rel);
      bool fok = false;
      const std::string text = read_file(path, fok);
      if (!fok) continue;
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (line.find("sundial") != std::string::npos) hits.push_back(rel + ":" + std::to_string(ln));
      }
    }
    check(scanned.size() >= 60, "scanned every source the studio binary is built from, C included (" +
                                    std::to_string(scanned.size()) + " files)");
    std::string first = hits.empty() ? std::string() : (" — first at " + hits.front());
    check(hits.empty(), "no source names this screen: the app is four files and a binary that never heard of it" + first);
  }

  // The scratch survives a failure so the four files can be read; a green run cleans up.
  if (g_fail == 0) fs::remove_all(scratch);
  return report("rolltui from_nothing_test");
}
