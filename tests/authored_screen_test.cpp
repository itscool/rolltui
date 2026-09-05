//
// authored_screen_test.cpp — THE PHASE 11 PROOF (plan/phase-11.md, milestone 6), the
// sibling of Phase 10 m6's files-only test one level up.
//
// Phase 10 proved a screen can be FILES: a layout, a menu, a bindings file and a document
// that a host has never heard of. This proves the step after it — that the FILES CAN BE
// AUTHORED, in a tool that is not the app they are for, from nothing but a profile the app
// publishes about itself. Three processes, in order:
//
//   1. `rolltui-paint --profile`   the target app publishes what a layout may name in it:
//                                  its `canvas` kind, its `tools` menu file, its samples,
//                                  its min sizes. Generated from where the binary reads
//                                  them, never hand-written.
//   2. `rolltui-studio --app …`    the AUTHORING tool, driven through its own design
//                                  editor by keystrokes alone, builds a three-window
//                                  screen for an app it is not and saves layouts/easel.json.
//   3. `rolltui-paint --layout …`  the target app runs that file and finds it clean: the
//                                  canvas takes a drag, the palette draws, and `help`
//                                  lists an action that exists only because a person typed
//                                  it into the studio.
//
// THE PROOF APP IS DELIBERATELY NOT CHAT-SHAPED — no transcript, no input — because a
// proof built on another transcript-and-prompt screen would only re-test the shape roll
// already has, and the profile's whole claim is that the studio can author for an app it
// knows nothing about.
//
// THE CONTROLS ARE WHAT CARRY THE WEIGHT, both of them kept from Phase 10 m6:
//
//   THE PROFILE CONTROL. The identical key sequence is run with --app REMOVED. The kind
//   picker then does not offer `canvas` and the menu-file choice does not offer `tools`,
//   so the author cannot build the screen at all: the saved file is two empty `text:`
//   windows with no title, no action and no threshold, and the paint app draws nothing
//   from it. The profile is what made the authoring possible, not a decoration on it.
//
//   THE SOURCE CONTROL. The word "easel" — the layout's name, all three window titles and
//   the action — appears in NO source the library or its hosts are built from. `canvas`,
//   `sheet` and `tools` DO appear in paint.cpp, and must: those are the app's own
//   vocabulary, which is exactly what a profile publishes. What no source may know is the
//   SCREEN.
//
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// PHASE 17 m2c: the C API, through the umbrella alone. This suite reads two files back —
// the app profile `rolltui-paint --profile` wrote, and the layout the studio saved — and
// both loaders are C: `rolltui_app_profile_parse` and `rolltui_load_layout_text`. The
// hooks the second one wants are the library's own now (`rolltui_layout_default_hooks`),
// which is what made this conversion two lines rather than a copied table.
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;

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
// A `const char*` the library BORROWS back, as a string this file can compare. Valid only
// while the profile is — which is the whole of the borrow rule, stated at each accessor.
std::string borrowed(const char* p) { return p ? std::string(p) : std::string(); }

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
const char* kAuthor =
    "F6 "
    "Type:new_layout Enter Type:easel Enter "
    "Type:split_into_a_row Enter "
    "Type:widget_kind Enter Type:canvas Enter "
    "Type:source Enter Type:sheet Enter Escape "
    "Type:title Enter Type:easel_sheet Enter Escape "
    "Tab "
    "Type:split_into_a_column Enter "
    "Type:widget_kind Enter Type:menu Enter "
    "Type:menu_file Enter Type:tools Enter "
    "Type:title Enter Type:easel_tools Enter Escape "
    "Tab "
    "Type:widget_kind Enter Type:help Enter "
    "Type:title Enter Type:easel_keys Enter Escape "
    "Type:actions Enter End Enter Type:app.easel Enter "
    "Type:app.easel Enter Enter Type:clear_the_easel_sheet Enter Escape Escape "
    "Type:save Enter Type:easel Enter";

}  // namespace

int main() {
  const char* tmp = std::getenv("TMPDIR");
  const std::string scratch = std::string(tmp && *tmp ? tmp : "/tmp") + "/rolltui_authored_" + std::to_string(::getpid());
  fs::remove_all(scratch);
  fs::create_directories(scratch + "/with");
  fs::create_directories(scratch + "/without");
  const std::string profile = scratch + "/paint.json";
  const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md";
  const std::string err = " 2>>'" + scratch + "/stderr.txt'";

  // ---- 1. the target app publishes itself ----------------------------------------------
  {
    int rc = 0;
    run(std::string("'") + ROLLTUI_PAINT_BIN + "' --profile '" + profile + "'" + err, rc);
    bool ok = false;
    const std::string text = read_file(profile, ok);
    check(rc == 0 && ok, "`rolltui-paint --profile` wrote the app's profile");
    RolltuiAppProfileReport rep{};
    RolltuiAppProfile* p = rolltui_app_profile_parse(text.data(), text.size(), &rep);
    RolltuiStr sum{};
    rolltui_app_profile_report_summary(&rep, &sum);
    check(p && rolltui_app_profile_report_clean(&rep),
          "…and it loads clean [" + std::string(sum.p ? sum.p : "", sum.n) + "]");
    rolltui_str_free(&sum);
    if (p) {
      check(borrowed(rolltui_app_profile_app(p, nullptr)) == "paint" && rolltui_app_profile_kind_count(p) == 1 &&
                borrowed(rolltui_app_profile_kind_name(p, 0, nullptr)) == "canvas" &&
                rolltui_app_profile_kind_rule(p, 0) == ROLLTUI_APP_PROFILE_SOURCE_REQUIRED,
            "…naming the one kind this app registers, and that it takes a source");
      check(rolltui_app_profile_menu_count(p) == 1 && borrowed(rolltui_app_profile_menu_name(p, 0, nullptr)) == "tools" &&
                has(borrowed(rolltui_app_profile_menu_json(p, 0, nullptr)), "Clear the sheet"),
            "…and carrying its tool palette VERBATIM, so the studio resolves `menu:tools` as this app does");
      check(rolltui_app_profile_document_count(p) == 0 && rolltui_app_profile_submit_count(p) == 0 &&
                rolltui_app_profile_min_width(p) == 20 && rolltui_app_profile_min_height(p) == 6,
            "…with no document and no submit target: this app has no transcript and no input");
    }
    rolltui_app_profile_free(p);
    rolltui_app_profile_report_release(&rep);
  }

  // ---- 2. the studio authors the screen, knowing only that file --------------------------
  auto author = [&](const std::string& dir, const std::string& app_flag) {
    int rc = 0;
    const std::string out = run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + fixture + "' --theme default-dark --presets '" +
                                    dir + "' " + app_flag + " --frame 150x34 --keys \"" + kAuthor + "\"" + err,
                                rc);
    check(rc == 0 && !out.empty(), "the studio ran the authoring script (rc " + std::to_string(rc) + ")");
    return out;
  };
  const std::string authoring = author(scratch + "/with", "--app '" + profile + "'");
  {
    check(has(authoring, "[canvas]"),
          "while authoring, the app's own kind previews as a labelled placeholder — the studio cannot build a canvas and does not pretend to");
    // Wide enough that the two left-hand windows show their titles beside the editor's
    // own popup — the third is behind it, and is asserted where it matters, in the app.
    check(has(authoring, "easel sheet") && has(authoring, "easel tools"),
          "…the windows carry the titles that were typed");
    check(has(authoring, "Clear the sheet"), "…and `menu:tools` resolves to the TARGET's palette, from the profile");
    check(has(authoring, "saved layout file"), "…and the save-as wrote the file");
  }

  bool ok = false;
  const std::string saved = read_file(scratch + "/with/layouts/easel.json", ok);
  check(ok, "layouts/easel.json exists — the artifact the target app reads");
  {
    RolltuiLoadedLayout l;
    RolltuiLayoutReport rep{};
    rolltui_loaded_layout_init(&l);
    std::size_t defaults_n = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(&defaults_n);
    const int ok_l = rolltui_load_layout_text(saved.data(), saved.size(), &l, defaults, defaults_n,
                                              rolltui_layout_default_hooks(), &rep);
    check(ok_l && rolltui_layout_report_clean(&rep), "…it loads clean [" + std::string(rep.error.p ? rep.error.p : "", rep.error.n) + "]");
    if (ok_l) {
      check(l.name.view() == "easel" && l.min_width == 20 && l.min_height == 6,
            "…named as typed, with the thresholds inherited from the PROFILE (m5's one right inheritance)");
      check(l.actions_n == 1 && l.actions[0].name.view() == "app.easel" &&
                l.actions[0].description.view() == "clear the easel sheet",
            "…declaring exactly the one action a person typed, with the description they gave it");
      check(l.popups_n == 0, "…and no popup: it was created from the skeleton, not from the screen that was open");
    }
    rolltui_loaded_layout_release(&l);
    rolltui_layout_report_release(&rep);
    check(has(saved, "\"content\": \"canvas:sheet\"") && has(saved, "\"content\": \"menu:tools\"") && has(saved, "\"content\": \"help\""),
          "…and the three contents are the app's kind, the app's menu, and a library kind");
  }

  // ---- 3. the target app runs it ---------------------------------------------------------
  {
    int rc = 0;
    const std::string frame = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch +
                                      "/with' --layout easel --frame 76x22 --stroke 3,2-24,9" + err,
                                  rc);
    check(rc == 0 && !frame.empty(), "rolltui-paint rendered the authored screen (rc " + std::to_string(rc) + ")");
    check(has(frame, "easel sheet") && has(frame, "easel tools") && has(frame, "easel keys"),
          "…all three windows, titled as the author titled them");
    // The canvas is a REAL widget: it received the press, every drag between the two
    // points, and the release, and it kept the marks. 22 cells for a 21-step stroke.
    check(has(frame, "###") && has(status_line(frame), "marks 22"),
          "…the canvas took the whole drag — press, drags and release — and painted it [" + status_line(frame) + "]");
    check(has(frame, "Clear the sheet") && has(frame, "Brush"),
          "…the palette is the app's own menu file, drawn in a window a person placed");
    check(has(frame, "app:") && has(frame, "(unbound)") && has(frame, "clear the easel sheet"),
          "…and `help` lists an action this binary has never named, described in the words typed in the studio");
    check(!has(status_line(frame), "[") && has(status_line(frame), " easel "),
          "…with no window reporting a problem [" + status_line(frame) + "]");
    // The standing rule (the user, 2026-09-01): every view shrinks to 1 or 0 cells in
    // either dimension and stays graceful. An authored screen is not exempt.
    for (const char* size : {"1x1", "20x4", "8x30"}) {
      int rc2 = 0;
      const std::string small = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch + "/with' --layout easel --frame " +
                                        size + " --stroke 0,0-3,3" + err,
                                    rc2);
      check(rc2 == 0 && !small.empty(), std::string("…and at ") + size + " it still renders");
    }
  }

  // ---- CONTROL: the profile is what made the authoring possible ---------------------------
  {
    const std::string blind = author(scratch + "/without", "");
    check(!has(blind, "[canvas]") && !has(blind, "Clear the sheet"),
          "with no --app the studio offers neither the app's kind nor its menu file");
    bool ok2 = false;
    const std::string other = read_file(scratch + "/without/layouts/easel.json", ok2);
    check(ok2 && !has(other, "canvas") && !has(other, "menu:tools") && !has(other, "easel sheet") && !has(other, "app.easel"),
          "…so the identical keystrokes save a file with none of it: no canvas, no palette, no title, no action");
    check(ok2 && !has(other, "min_width"), "…and no thresholds, because there was no app to take them from");
    int rc = 0;
    const std::string frame = run(std::string("'") + ROLLTUI_PAINT_BIN + "' --presets '" + scratch + "/without' --layout easel --frame 60x12" + err, rc);
    check(rc == 0 && !has(frame, "###") && !has(frame, "Brush"),
          "…and the app draws nothing from it: two empty windows");
  }

  // ---- CONTROL: no source knows this screen ------------------------------------------------
  // "easel" is the layout name, all three window titles and the action. The app's own
  // vocabulary (canvas / sheet / tools) is deliberately NOT part of this control: those
  // are what a profile exists to publish, and paint.cpp must name them.
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
  return report("rolltui authored_screen_test");
}
