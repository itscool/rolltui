//
// authored_screen_test.cpp — THE PROOF THAT A SCREEN CAN BE AUTHORED FOR AN APP THE TOOL IS
// NOT — the sibling of the files-only test, one level up.
//
// The files-only suite proves a screen can be FILES: a layout, a menu, a bindings file and a
// document that a host has never heard of. This proves the step after it — that the FILES CAN BE AUTHORED in
// a tool that is not the app they are for. Two processes, in order:
//
//   1. `rolltui-studio …`          the AUTHORING tool, driven through its own design editor by
//                                  keystrokes alone, builds a three-window screen for an app it
//                                  is not and saves layouts/easel.json. It names `canvas:sheet`
//                                  — a kind it cannot build — and `menu:tools`, a menu file it
//                                  cannot resolve, and writes both down anyway.
//   2. `rolltui-paint --layout …`  the target app runs that file: the canvas takes a drag, the
//                                  palette draws from the app's OWN embedded menu, and `help`
//                                  lists an action that exists only because a person typed it
//                                  into the studio.
//
// THE STUDIO IS NEVER TOLD WHAT PAINT CAN BUILD. It does not need to know, and it does not
// ask. A process 0 in which the target published what a layout may name inside it — with the
// picker then offering exactly those kinds and refusing anything else — is the app bounding
// the design. The author is free and the TOOL reports its own limits, which makes the chain
// one process shorter and the claim bigger.
//
// THE PROOF APP IS DELIBERATELY NOT CHAT-SHAPED — no transcript, no input — because a proof
// built on another transcript-and-prompt screen would only re-test the shape roll already has.
//
// THE CONTROLS ARE WHAT CARRY THE WEIGHT:
//
//   THE PREVIEW CONTROL. `[canvas:sheet]` appears in the authoring frame and no painted mark
//   does. The studio draws a labelled placeholder for a kind it cannot build — it neither
//   refuses the content nor quietly rewrites it to something it can draw — and the SAVED file
//   still says `canvas:sheet`. The artifact records the intent, not the previewer's ability.
//
//   THE GAP-REPORT CONTROL, three screens through one binary. Paint prints its gap report
//   at end of init and runs the screen either way: the authored screen reports exactly the one
//   thing it names that paint cannot reach (an action with no chord); paint's OWN default
//   screen reports nothing at all; a screen naming a source paint does not have reports that
//   instead. A report that says something on every screen would say nothing.
//
//   THE SOURCE CONTROL. The word "easel" — the layout's name, all three window titles and the
//   action — appears in NO source the library or its hosts are built from. `canvas`, `sheet`
//   and `tools` DO appear in paint.cpp, and must: those are the app's own vocabulary. What no
//   source may know is the SCREEN.
//
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// the C API, through the umbrella alone. This suite reads the layout the studio
// saved back through the library's own loader, `rolltui_load_layout_text`, with the hooks the
// library itself supplies (`rolltui_layout_default_hooks`) rather than a copied table.
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

#ifndef ROLLTUI_STUDIO_BIN
#error "ROLLTUI_STUDIO_BIN must name the studio binary"
#endif
#ifndef ROLLTUI_PAINT_BIN
#error "ROLLTUI_PAINT_BIN must name the paint binary"
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

std::string status_line(const std::string& frame) {
  std::vector<std::string> rows;
  std::istringstream in(frame);
  std::string row;
  while (std::getline(in, row)) rows.push_back(row);
  return rows.empty() ? "" : rows.back();
}

// THE AUTHORING, as keystrokes. Every one of them is a thing a person does in the design
// editor: create a layout from the empty skeleton, split it, pick each window's kind from
// the picker, name its source, title it, and declare the action the screen emits. An
// Escape follows each INPUT commit and no CHOICE, because a choice clears the menu's
// filter on its way back up and an input does not — an Escape with no filter to clear
// would close the editor instead.
// every one of `widget kind`, `source` and `menu file` is now an INPUT, so each
// commit is followed by an Escape (a choice clears the menu's filter on its way back up and an
// input does not — an Escape with no filter to clear would close the editor instead). The kind
// was a closed CHOICE until this phase, which is why `canvas` had to be offered to be typed at
// all; it is typed here exactly as `sheet` and `tools` always were.
//
// AND THE THRESHOLDS ARE TYPED TOO, which used to be the profile's one legitimate inheritance:
// the minimum size a screen needs is a fact about the screen, so its author states it.
// AND THE SCOPES ARE LEVELS NOW, which is what the script shows: creating a layout and saving
// it are `Layout file` operations, splitting a node is a `Tree` one, and the thresholds and the
// action list belong to `This screen`. A step that used to be one filter is a filter, an Enter,
// and a filter — one keystroke more, and the level you are in says what you are changing.
//
// AN OPERATION THAT CHANGES THE LAYOUT NEEDS NO ESCAPE AFTER IT: creating, splitting and saving
// rebuild the menu, which puts it back at the root. An INPUT commit does not, so it keeps the
// one Escape it always had — and inside a submenu a second Escape is what leaves the level.
const char* kAuthor =
    "F6 "
    "Type:layout_file Enter Type:new_layout Enter Type:easel Enter "
    "Type:tree Enter Type:split_into_a_row Enter "
    "Type:widget_kind Enter Type:canvas Enter Escape "
    "Type:source Enter Type:sheet Enter Escape "
    "Type:title Enter Type:easel_sheet Enter Escape "
    "Tab "
    "Type:tree Enter Type:split_into_a_column Enter "
    "Type:widget_kind Enter Type:menu Enter Escape "
    "Type:menu_file Enter Type:tools Enter Escape "
    "Type:title Enter Type:easel_tools Enter Escape "
    "Tab "
    "Type:widget_kind Enter Type:help Enter Escape "
    "Type:title Enter Type:easel_keys Enter Escape "
    "Type:this_screen Enter "
    "Type:actions Enter End Enter Type:app.easel Enter "
    "Type:app.easel Enter Enter Type:clear_the_easel_sheet Enter Escape Escape "
    "Type:minimum_width Enter Type:20 Enter Escape "
    "Type:minimum_height Enter Type:6 Enter Escape Escape "
    "Type:layout_file Enter Type:save Enter Type:easel Enter";

// SELECT THE SHEET AGAIN, WHICH MOVES THE PANEL. The editor floats over the design and goes to
// whichever side the selected node is not, so the run above — which ends on the rightmost
// window — has the panel on the LEFT, over the canvas. The two runs differ by these keys alone,
// which is what makes the pair a control on the panel moving rather than an assertion about one
// frame's layout.
const char* kReveal = " Escape Escape Tab Tab";

}  // namespace

int main() {
  const char* tmp = std::getenv("TMPDIR");
  const std::string scratch = std::string(tmp && *tmp ? tmp : "/tmp") + "/rolltui_authored_" + std::to_string(::getpid());
  fs::remove_all(scratch);
  fs::create_directories(scratch + "/with");
  const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md";
  const std::string stderr_path = scratch + "/stderr.txt";
  const std::string err = " 2>>'" + stderr_path + "'";
  auto stderr_since = [&](std::size_t from) {
    bool ok = false;
    const std::string all = read_file(stderr_path, ok);
    return from < all.size() ? all.substr(from) : std::string();
  };
  auto stderr_size = [&]() -> std::size_t {
    bool ok = false;
    return read_file(stderr_path, ok).size();
  };

  // ---- 1. the studio authors the screen, knowing nothing about the app -------------------
  int rc = 0;
  const std::string authoring =
      run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + fixture + "' --theme default-dark --presets '" + scratch +
              "/with' --frame 150x34 --keys \"" + kAuthor + "\"" + err,
          rc);
  check(rc == 0 && !authoring.empty(), "the studio ran the authoring script (rc " + std::to_string(rc) + ")");
  check(has(authoring, "saved layout file"), "…and the save-as wrote the file");

  // THE SAME SCRIPT, ENDING ON A DIFFERENT NODE — the control on the panel moving.
  int rc_reveal = 0;
  const std::string revealed =
      run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + fixture + "' --theme default-dark --presets '" + scratch +
              "/with' --frame 150x34 --keys \"" + kAuthor + kReveal + "\"" + err,
          rc_reveal);
  // A DESIGN TOOL MAY NOT HIDE WHAT IT IS DESIGNING. The editor floats over the screen, so it
  // goes to whichever side the selected node is not: ending on the rightmost window puts it on
  // the left, and selecting the sheet again puts it back on the right. Row 0 says which side.
  check(rc_reveal == 0 && authoring.rfind("\xE2\x94\x8C layout editor", 0) == 0,
        "the panel is on the LEFT when the selection is on the right");
  check(revealed.rfind("\xE2\x94\x8C easel sheet", 0) == 0,
        "…and selecting the sheet moves it, so the node being edited is never behind the tool editing it");

  // THE PREVIEW CONTROL. The studio has no canvas and does not pretend to: the window draws
  // `[canvas:sheet]`, the whole content and not just the kind, and nothing paints in it.
  check(has(revealed, "[canvas:sheet]"),
        "a kind this tool cannot build previews as a labelled placeholder — not an error panel, and not a refusal");
  check(!has(revealed, "==="), "…and nothing is drawn in it: a placeholder is a preview, never a substitute widget");
  // The menu window is a LIBRARY kind, so it builds and says what it is missing in its own
  // words. Two different honest answers to two different questions, which is the split the
  // gap report is built on: a kind that does not exist here, a file that is not there.
  check(has(revealed, "no menu file 'tools'"),
        "…and a menu file this tool cannot resolve says so, in the menu widget's own words");
  check(has(revealed, "easel sheet") && has(revealed, "easel tools"),
        "…the windows carry the titles that were typed");

  bool ok = false;
  const std::string saved = read_file(scratch + "/with/layouts/easel.json", ok);
  check(ok, "layouts/easel.json exists — the artifact the target app reads");
  // The save-as goes through the Layout store rather than hand-building a path and writing the
  // file directly, so the store LEARNS the save: the working copy records the new origin — a manual save writes even under --frame, exactly as the theme save-as does and
  // `studio_golden_test` asserts — while nothing else under --frame wrote one.
  {
    bool wok = false;
    const std::string wc = read_file(scratch + "/with/layout.working.json", wok);
    check(wok && wc.find("\"preset\": \"easel\"") != std::string::npos && !fs::exists(scratch + "/with/theme.working.json"),
          "…and the Layout store recorded the save-as as its origin ('easel'), with no other working copy written under --frame");
  }
  {
    RolltuiLayoutReport rep{};
    std::size_t defaults_n = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(rolltui_test::test_context(), &defaults_n);
    RolltuiLayout* l = rolltui_load_layout_text(saved.data(), saved.size(), defaults, defaults_n,
                                                rolltui_layout_default_hooks(), &rep);
    const int ok_l = l != nullptr;
    check(ok_l && rolltui_layout_report_clean(&rep), "…it loads clean [" + std::string(rep.error.p ? rep.error.p : "", rep.error.n) + "]");
    if (ok_l) {
      std::size_t ln = 0;
      const char* lname = rolltui_layout_name(l, &ln);
      int lw = 0, lh = 0;
      rolltui_layout_min_size(l, &lw, &lh);
      std::size_t an = 0;
      const RolltuiLayoutAction* av = rolltui_layout_actions(l, &an);
      check(std::string_view(lname, ln) == "easel" && lw == 20 && lh == 6,
            "…named as typed, with the thresholds the author typed rather than any inherited from a tool");
      check(an == 1 && view_of(av[0].name) == "app.easel" &&
                view_of(av[0].description) == "clear the easel sheet",
            "…declaring exactly the one action a person typed, with the description they gave it");
      check(rolltui_layout_popup(l, "menu", 4) == nullptr,
            "…and no popup: it was created from the skeleton, not from the screen that was open");
    }
    rolltui_layout_free(l);
    rolltui_layout_report_release(&rep);
    // THE PREVIEW CONTROL, second half: what the studio could not draw, it still wrote down.
    check(has(saved, "\"content\": \"canvas:sheet\"") && has(saved, "\"content\": \"menu:tools\"") && has(saved, "\"content\": \"help\""),
          "…and the file names all three contents, INCLUDING the two this tool could not build or resolve");
  }

  // ---- 2. the target app runs it ---------------------------------------------------------
  {
    const std::size_t before = stderr_size();
    int rc2 = 0;
    const std::string frame = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch +
                                      "/with' --layout easel --frame 76x22 --stroke 3,2-24,9" + err,
                                  rc2);
    check(rc2 == 0 && !frame.empty(), "rolltui-paint rendered the authored screen (rc " + std::to_string(rc2) + ")");
    check(has(frame, "easel sheet") && has(frame, "easel tools") && has(frame, "easel keys"),
          "…all three windows, titled as the author titled them");
    // The canvas is a REAL widget: it received the press, the drag to the far point and the
    // release, and it filled the cells between them itself. 22 cells for a 21-step stroke.
    // One pass over a cell is the ascii ramp's light step, `:` — darkness in this app is a
    // count of passes, so a first stroke is deliberately faint.
    check(has(frame, ":::") && has(status_line(frame), "marks 22"),
          "…the canvas took the whole drag — press, drags and release — and painted it [" + status_line(frame) + "]");
    // The studio could not resolve `menu:tools` at all. The app it was authored for embeds
    // that file, so the window the designer placed fills with a palette they never saw.
    check(has(frame, "Clear the sheet") && has(frame, "Texture"),
          "…and `menu:tools` resolves to the app's OWN embedded palette, which the studio never had");
    check(has(frame, "app:") && has(frame, "(unbound)") && has(frame, "clear the easel sheet"),
          "…and `help` lists an action this binary has never named, described in the words typed in the studio");
    check(!has(status_line(frame), "[") && has(status_line(frame), " easel "),
          "…with no window reporting a problem [" + status_line(frame) + "]");

    // THE GAP REPORT, on a real screen and a real miss. `app.easel` exists
    // because a person typed it into a design tool; paint has no chord for it and says so, by
    // name, at end of init — and draws every window above regardless. That is the phase's whole
    // claim in one line of output: the screen is the intent, the code catches up.
    const std::string said = stderr_since(before);
    check(has(said, "this screen names 4 things this app must provide and 1 is missing"),
          "…and the app REPORTS what the screen names that it cannot provide [" + said + "]");
    check(has(said, "this screen declares 'app.easel': no chord reaches it"),
          "…naming the one thing, in the screen's own vocabulary");

    // THE STANDING RULE: every view shrinks to 1 or 0 cells in
    // either dimension and stays graceful. An authored screen is not exempt.
    for (const char* size : {"1x1", "20x4", "8x30"}) {
      int rc3 = 0;
      const std::string small = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch + "/with' --layout easel --frame " +
                                        size + " --stroke 0,0-3,3" + err,
                                    rc3);
      check(rc3 == 0 && !small.empty(), std::string("…and at ") + size + " it still renders");
    }
  }

  // ---- the host's own kind is not exempt from the library's rules -------------------------
  // `paint.cpp`'s header claims it: *"a source this app does not have is a NAMED problem and
  // an error panel, exactly as an unbound `rows:` source is — a host's own kind is not exempt
  // from the rule."* A CLAIM IN A COMMENT THAT NO ASSERTION COVERS IS A DEFECT. NULLing the
  // plugin's `problem` slot is the control: without this case, every suite stays green.
  {
    fs::create_directories(scratch + "/wrong/layouts");
    std::ofstream(scratch + "/wrong/layouts/wrong.json", std::ios::binary | std::ios::trunc)
        << R"({"name":"wrong","min_width":0,"min_height":0,"focus":"s","actions":{},
               "root":{"row":[{"id":"s","content":"canvas:nope","border":"single","title":"s","focusable":true}]}})";
    const std::size_t before = stderr_size();
    int rc2 = 0;
    const std::string frame = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch +
                                      "/wrong' --layout wrong --frame 120x10" + err,
                                  rc2);
    check(rc2 == 0 && has(frame, "nothing is bound to 'nope'"),
          "a canvas whose source the app does not have draws the panel, in the host's own words");
    check(has(status_line(frame), "nothing is bound to 'nope'"),
          "…and it is NAMED in the report, not merely drawn [" + status_line(frame) + "]");
    // THE GAP-REPORT CONTROL, second of three screens: a DIFFERENT screen, a DIFFERENT gap,
    // the same one line of code — and the app still renders the window.
    const std::string said = stderr_since(before);
    check(has(said, "window 's' wants 'canvas:nope': nothing is bound to 'nope'"),
          "…and the gap report says it too, at end of init, naming the window and the content [" + said + "]");
  }

  // ---- CONTROL: the gap report is not decoration ------------------------------------------
  // Third screen, same binary: paint's OWN default layout, which names only what paint has.
  // A report that printed on every screen would carry no information at all; this is what
  // makes the two lines above mean something.
  {
    const std::size_t before = stderr_size();
    int rc2 = 0;
    const std::string frame = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch + "/with' --frame 60x12" + err, rc2);
    const std::string said = stderr_since(before);
    check(rc2 == 0 && !frame.empty() && !has(said, "this screen names"),
          "on this app's own screen the gap report says nothing at all [" + said + "]");
  }

  // ---- CONTROL: no source knows this screen ------------------------------------------------
  // "easel" is the layout name, all three window titles and the action. The app's own
  // vocabulary (canvas / sheet / tools) is deliberately NOT part of this control: those are
  // paint's, and paint.cpp must name them.
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
      bool got = false;
      const std::string text = read_file(p, got);
      if (!got) continue;
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (line.find("easel") != std::string::npos) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(scanned.size() >= 30, "scanned every source the library and its hosts are built from (" + std::to_string(scanned.size()) + " files)");
    check(hits.empty(), std::string("no source names the authored screen — it was designed, not compiled in") +
                            (hits.empty() ? "" : ": " + hits.front()));
  }

  fs::remove_all(scratch);
  return report("rolltui_authored_screen_test");
}
