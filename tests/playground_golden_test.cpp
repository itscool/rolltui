//
// playground_golden_test.cpp — golden frames through the playground's `--frame WxH`
// mode (plan/phase-9.md VERIFY: "golden frames at 80×24, 120×40 and 40×12"). Runs
// the REAL rolltui-playground binary on the demo session fixture and compares its
// stdout byte-for-byte with rolltui/tests/fixtures/frames/<case>.txt.
//
// Re-recording is a deliberate human act — `rolltui-playground-golden-test --record`
// — never something ctest does; look at the new frames before committing them (the
// playground's `--frame-sgr` shows the same frame in colour).
//
// Also asserts what a golden cannot: every row of every frame fits the width in
// cells, the scrolled frame differs from the unscrolled one, and (milestone 8)
// opening the help popup and closing it with Escape gives back exactly the frame
// without it — a popup leaves nothing behind.
//
// Milestone 8 added the layout cases: each built-in, the stacked fallback below a
// layout's minimum size (40x12), Tab moving focus, a layout FILE with rounded /
// double / heavy borders and a mixed-border seam, and the help popup at two sizes
// (it re-places itself: Done-when h).
//
// Milestone 11 added the menu cases: F2 opens the settings menu popup; Enter descends
// into the Theme choice; Down + Enter chooses default-light three levels deep by
// keyboard alone (the frame goes light and the choice shows its value); typing filters;
// Left returns exactly to the opened frame; Escape leaves exactly the base frame; Ctrl-P
// is the same tree as a palette; a toggle shows [x]; three degenerate sizes with the
// menu driven.
//
// Milestone 14 added the theme editor: F4 opens it as a side popup; Roles › md_heading
// › fg with the selection moved previews a colour (--dump-role shows it), Escape puts
// the committed one back, Enter commits, Ctrl-Z undoes, save-as writes a preset file
// under a scratch --presets directory and a relaunch with --theme <that file> shows
// the colour; the confirm popup for a reset; a degenerate size with the editor open.
//
// Milestone 9 added the transcript-widget cases on a second fixture (tools.md: two
// foldable tool blocks, a link, a list): folded by default; a click on the summary
// line and Ctrl-O both unfold the same block (asserted equal); a drag across a wrapped
// prompt copies its text with no line break; a double-click copies one word; a
// triple-click copies a whole paragraph; a drag held past the bottom edge auto-scrolls
// over two ticks and copies across entries; ▼ marker placement. The copied text
// follows the frame in the playground's output, so the golden holds both.
//
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/Json.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_PLAYGROUND_BIN
#error "ROLLTUI_PLAYGROUND_BIN must name the playground binary"
#endif
#ifndef ROLLTUI_FIXTURE_DIR
#error "ROLLTUI_FIXTURE_DIR must point at rolltui/tests/fixtures"
#endif

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

struct Case {
  const char* name;
  const char* args;
  const char* fixture = "demo.md";
};

// The part of a `--frame` output after "--- copied ---" ("" when nothing was copied).
std::string copied_part(const std::string& out) {
  const std::size_t at = out.find("--- copied ---\n");
  if (at == std::string::npos) return "";
  std::string s = out.substr(at + 15);
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}
std::string frame_part(const std::string& out) {
  std::size_t at = out.find("--- copied ---\n");
  const std::size_t role = out.find("--- role ---\n");
  if (role != std::string::npos && (at == std::string::npos || role < at)) at = role;
  return at == std::string::npos ? out : out.substr(0, at);
}
// The "--- role ---" trailer's line ("md_heading fg=#.. bg=#.. bold"), "" when absent.
std::string role_part(const std::string& out) {
  const std::size_t at = out.find("--- role ---\n");
  if (at == std::string::npos) return "";
  std::string s = out.substr(at + 13);
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  const bool record = (argc > 1 && std::strcmp(argv[1], "--record") == 0);
  const std::string frames = std::string(ROLLTUI_FIXTURE_DIR) + "/frames/";
  // A scratch preset directory for the editor cases (never the developer's own).
  const char* tmp = std::getenv("TMPDIR");
  const std::string scratch = std::string(tmp && *tmp ? tmp : "/tmp") + "/rolltui_golden_" + std::to_string(::getpid());
  const std::string presets = " --presets '" + scratch + "/p' --shipped '" + scratch + "/s'";
  std::filesystem::remove_all(scratch);
  std::filesystem::create_directories(scratch);
  const Case cases[] = {
      {"demo.80x24", "--frame 80x24 --theme default-dark"},
      {"demo.120x40", "--frame 120x40 --theme default-dark"},
      {"demo.40x12", "--frame 40x12 --theme default-dark"},
      {"demo.80x24.top", "--frame 80x24 --theme default-dark --keys \"Home\""},
      {"demo.80x24.scrolled", "--frame 80x24 --theme default-dark --keys \"Home PageDown Down Down\""},
      {"demo.80x24.light", "--frame 80x24 --theme default-light"},
      {"demo.80x24.ambiguous", "--frame 80x24 --theme mono --ambiguous-wide"},
      // milestone 8
      {"demo.80x24.panel-left", "--frame 80x24 --theme default-dark --layout panel-left"},
      {"demo.80x24.no-panel", "--frame 80x24 --theme default-dark --layout no-panel"},
      {"demo.80x24.stacked", "--frame 80x24 --theme default-dark --layout stacked"},
      {"demo.80x24.focus", "--frame 80x24 --theme default-dark --keys \"Tab\""},
      {"demo.80x24.popup", "--frame 80x24 --theme default-dark --keys \"F1\""},
      {"demo.120x40.popup", "--frame 120x40 --theme default-dark --keys \"F1\""},
      {"demo.80x24.popup-closed", "--frame 80x24 --theme default-dark --keys \"F1 Escape\""},
      {"demo.80x24.file", "--frame 80x24 --theme default-dark --layout '" ROLLTUI_FIXTURE_DIR "/layouts/wide-left.json'"},
      {"demo.80x24.file-popup", "--frame 80x24 --theme default-dark --layout '" ROLLTUI_FIXTURE_DIR "/layouts/wide-left.json' --keys \"F1\""},
      // milestone 9 (the transcript widget), on the tools fixture
      {"tools.80x24", "--frame 80x24 --theme default-dark", "tools.md"},
      {"tools.80x24.top", "--frame 80x24 --theme default-dark --keys \"Home\"", "tools.md"},
      {"tools.80x24.unfold-click", "--frame 80x24 --theme default-dark --keys \"Home Click 5,4\"", "tools.md"},
      {"tools.80x24.unfold-ctrl-o", "--frame 80x24 --theme default-dark --keys \"Home CtrlO\"", "tools.md"},
      {"tools.80x24.drag", "--frame 80x24 --theme default-dark --keys \"Home Click 4,1 Drag 20,2 Release\"", "tools.md"},
      {"tools.80x24.dblclick", "--frame 80x24 --theme default-dark --keys \"Home DblClick 10,1 Release\"", "tools.md"},
      {"tools.80x24.tripleclick", "--frame 80x24 --theme default-dark --keys \"Home TripleClick 30,7 Release\"", "tools.md"},
      {"tools.80x24.autoscroll", "--frame 80x24 --theme default-dark --keys \"Home Click 4,1 Drag 4,30 Tick Tick Release\"", "tools.md"},
      // Ctrl-O unfolds the first block; the second block's summary then sits on row 29
      // and a click unfolds it (Ctrl-O again would re-fold the first, still nearest).
      {"tools.120x40.unfold-all", "--frame 120x40 --theme default-dark --keys \"Home CtrlO Click 5,29\"", "tools.md"},
      // milestone 10 (the input widget) on the demo fixture; the input's inner row is
      // 21 at 80x24 and, with the bordered window's inset, its text starts at x=4
      {"input.80x24.typed", "--frame 80x24 --theme default-dark --keys \"Type:hello_world\""},
      {"input.80x24.multiline", "--frame 80x24 --theme default-dark --keys \"Type:one AltEnter Type:two\""},
      {"input.80x24.wrap", "--frame 80x24 --theme default-dark --keys \"Type:the_quick_brown_fox_jumps_over_the_lazy_dog_and_keeps_running_until_it_wraps\""},
      {"input.80x24.stacked-multiline", "--frame 80x24 --theme default-dark --layout stacked --keys \"Type:one AltEnter Type:two\""},
      {"input.80x24.select-all", "--frame 80x24 --theme default-dark --keys \"Type:hello_world CtrlA AltC\""},
      {"input.80x24.drag", "--frame 80x24 --theme default-dark --keys \"Type:hello_world Click 4,21 Drag 8,21 Release\""},
      {"input.80x24.dblclick", "--frame 80x24 --theme default-dark --keys \"Type:hello_world DblClick 10,21 Release\""},
      {"input.80x24.submit", "--frame 80x24 --theme default-dark --keys \"Type:hi_there Enter\""},
      {"input.80x24.history", "--frame 80x24 --theme default-dark --keys \"Type:first Enter Type:second Enter Up Up\""},
      {"input.80x24.edit", "--frame 80x24 --theme default-dark --keys \"Type:hello_world CtrlLeft ShiftEnd Type:there Home Delete Type:J\""},
      {"input.80x24.paste", "--frame 80x24 --theme default-dark --keys \"Paste:line_one\\nline_two\""},
      // fourteen lines pasted: the window caps at half its parent (the 23-row column:
      // 11 outer rows, 9 of text) and scrolls so the caret's row (line 14) is in view
      {"input.80x24.cap", "--frame 80x24 --theme default-dark --keys \"Paste:l1\\nl2\\nl3\\nl4\\nl5\\nl6\\nl7\\nl8\\nl9\\nl10\\nl11\\nl12\\nl13\\nl14\""},
      // degenerate sizes: 1 or 0 cells in either dimension for some window, with input
      // and popups exercised — graceful, never a crash or an overflow
      {"tiny.1x1", "--frame 1x1 --theme default-dark --keys \"Type:abc F1 Tab\""},
      {"tiny.2x2", "--frame 2x2 --theme default-dark --keys \"Type:abc AltEnter Type:d F1\""},
      {"tiny.6x1", "--frame 6x1 --theme default-dark --keys \"Paste:one\\ntwo Up Down Home End\""},
      {"tiny.1x6", "--frame 1x6 --theme default-dark --keys \"Type:hello Click 0,3 Drag 0,5 Release F1\""},
      {"tiny.20x3", "--frame 20x3 --theme default-dark --keys \"Type:a_prompt_that_is_longer_than_the_row F1 Escape Tab\""},
      {"tiny.80x2", "--frame 80x2 --theme default-dark --layout default --keys \"Type:hi Enter Up PageUp F1\""},
      // milestone 11 (the menu widget)
      {"menu.80x24.open", "--frame 80x24 --theme default-dark --keys \"F2\""},
      {"menu.80x24.theme", "--frame 80x24 --theme default-dark --keys \"F2 Enter\""},
      {"menu.80x24.choose-light", "--frame 80x24 --theme default-dark --keys \"F2 Enter Down Enter\""},
      {"menu.80x24.filter", "--frame 80x24 --theme default-dark --keys \"F2 Type:lay\""},
      {"menu.80x24.left", "--frame 80x24 --theme default-dark --keys \"F2 Enter Left\""},
      {"menu.80x24.escape", "--frame 80x24 --theme default-dark --keys \"F2 Escape\""},
      {"menu.80x24.toggle", "--frame 80x24 --theme default-dark --keys \"F2 Down Down Down Enter\""},
      {"menu.80x24.palette", "--frame 80x24 --theme default-dark --keys \"CtrlP Type:mono\""},
      {"menu.80x24.palette-choose", "--frame 80x24 --theme default-dark --keys \"CtrlP Type:stacked Enter\""},
      {"menu.120x40.open", "--frame 120x40 --theme default-dark --keys \"F2\""},
      {"tiny.1x1.menu", "--frame 1x1 --theme default-dark --keys \"F2 Enter Down Enter\""},
      {"tiny.3x3.menu", "--frame 3x3 --theme default-dark --keys \"F2 Down Enter Type:s\""},
      {"tiny.30x2.menu", "--frame 30x2 --theme default-dark --keys \"F2 Type:th Enter Down Enter CtrlP Type:q\""},
      // milestone 14 (the theme editor); these get the scratch --presets appended
      {"editor.120x40.open", "--frame 120x40 --theme default-dark --keys \"F4\""},
      {"editor.120x40.heading-fg", "--frame 120x40 --theme default-dark --dump-role md_heading --keys \"F4 Enter Type:heading Enter Enter Down Down\""},
      {"editor.120x40.heading-cancel", "--frame 120x40 --theme default-dark --dump-role md_heading --keys \"F4 Enter Type:heading Enter Enter Down Down Escape\""},
      {"editor.120x40.heading-commit", "--frame 120x40 --theme default-dark --dump-role md_heading --keys \"F4 Enter Type:heading Enter Enter Down Down Enter\""},
      {"editor.120x40.heading-undo", "--frame 120x40 --theme default-dark --dump-role md_heading --keys \"F4 Enter Type:heading Enter Enter Down Down Enter CtrlZ\""},
      {"editor.120x40.confirm", "--frame 120x40 --theme default-dark --keys \"F4 Type:built Enter\""},
      {"editor.120x40.save", "--frame 120x40 --theme default-dark --dump-role md_heading --keys \"F4 Enter Type:heading Enter Enter Down Down Enter Escape Escape Type:save Enter Type:mine Enter\""},
      {"tiny.8x3.editor", "--frame 8x3 --theme default-dark --keys \"F4 Enter Down Enter Type:x\""},
      // milestone 15: the Check popup and the Fixes level
      {"editor.120x40.check", "--frame 120x40 --theme default-dark --keys \"F4 Type:check Enter\""},
      {"editor.120x40.fixes", "--frame 120x40 --theme default-dark --keys \"F4 Type:fixes Enter\""},
      // milestone 16 (the layout editor); the scratch --presets is appended
      {"layout-editor.120x40.open", "--frame 120x40 --theme default-dark --keys \"F6\""},
      {"layout-editor.120x40.split", "--frame 120x40 --theme default-dark --keys \"F6 Type:split_into_a_row Enter\""},
      {"layout-editor.120x40.undo", "--frame 120x40 --theme default-dark --keys \"F6 Type:split_into_a_row Enter CtrlZ\""},
      {"layout-editor.120x40.border-preview", "--frame 120x40 --theme default-dark --keys \"F6 Type:border Enter Down\""},
      {"layout-editor.120x40.border-cancel", "--frame 120x40 --theme default-dark --keys \"F6 Type:border Enter Down Escape\""},
      {"layout-editor.120x40.drag", "--frame 120x40 --theme default-dark --keys \"F6 Type:split_into_a_row Enter Click 44,10 Drag 30,10 Release\""},
      {"layout-editor.120x40.click", "--frame 120x40 --theme default-dark --keys \"F6 Click 30,37\""},
      {"layout-editor.120x40.save", "--frame 120x40 --theme default-dark --keys \"F6 Type:split_into_a_row Enter Type:title Enter CtrlU Type:chat Enter Escape Type:save Enter Type:two Enter\""},
      {"tiny.9x4.layout-editor", "--frame 9x4 --theme default-dark --keys \"F6 Tab Type:split Enter Type:x\""},
  };
  std::string bottom, top, popup, popup_closed, popup_big;
  std::string tools_top, unfold_click, unfold_ctrl_o, drag_copy, dbl_copy, triple_copy, autoscroll_out;
  std::string typed, multiline, wrapped, stacked_ml, select_all_copy, in_drag_copy, in_dbl_copy, submitted, history, edited, pasted, capped;
  std::string menu_open, menu_theme, menu_light, menu_filter, menu_left, menu_escape, menu_toggle, menu_palette, menu_palette_choose, menu_big;
  std::string ed_open, ed_fg, ed_cancel, ed_commit, ed_undo, ed_confirm, ed_save, ed_check, ed_fixes;
  std::string le_open, le_split, le_undo, le_preview, le_cancel, le_drag, le_click, le_save;
  bool tiny_failed = false;
  for (const Case& c : cases) {
    int rc = 0;
    const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/" + c.fixture;
    std::string cmd = std::string("'") + ROLLTUI_PLAYGROUND_BIN + "' '" + fixture + "' " + c.args;
    if (std::string(c.name).find("editor") != std::string::npos) cmd += presets;  // the theme AND layout editor cases
    std::string out = run(cmd, rc);
    check(rc == 0 && !out.empty(), std::string(c.name) + ": playground ran (rc " + std::to_string(rc) + ", " +
                                       std::to_string(out.size()) + " bytes)");
    // Every row fits the width, in cells.
    int w = std::atoi(std::strstr(c.args, "--frame ") + 8);
    int rows = 0;
    bool fits = true;
    std::istringstream in(frame_part(out));
    std::string row;
    while (std::getline(in, row)) {
      ++rows;
      int cw = rolltui::unicode::display_width(row, std::strstr(c.args, "--ambiguous-wide") != nullptr);
      if (cw > w) { fits = false; check(false, std::string(c.name) + ": row wider than " + std::to_string(w) + ": [" + row + "]"); }
    }
    int h = std::atoi(std::strchr(c.args, 'x') + 1);
    check(fits && rows == h, std::string(c.name) + ": " + std::to_string(rows) + " rows of at most " + std::to_string(w) + " cells");
    if (std::string(c.name).rfind("tiny.", 0) == 0 && !(rc == 0 && fits && rows == h)) tiny_failed = true;
    if (std::string(c.name) == "demo.80x24") bottom = out;
    if (std::string(c.name) == "demo.80x24.top") top = out;
    if (std::string(c.name) == "demo.80x24.popup") popup = out;
    if (std::string(c.name) == "demo.80x24.popup-closed") popup_closed = out;
    if (std::string(c.name) == "demo.120x40.popup") popup_big = out;
    if (std::string(c.name) == "tools.80x24.top") tools_top = out;
    if (std::string(c.name) == "tools.80x24.unfold-click") unfold_click = out;
    if (std::string(c.name) == "tools.80x24.unfold-ctrl-o") unfold_ctrl_o = out;
    if (std::string(c.name) == "tools.80x24.drag") drag_copy = copied_part(out);
    if (std::string(c.name) == "tools.80x24.dblclick") dbl_copy = copied_part(out);
    if (std::string(c.name) == "tools.80x24.tripleclick") triple_copy = copied_part(out);
    if (std::string(c.name) == "tools.80x24.autoscroll") autoscroll_out = out;
    if (std::string(c.name) == "input.80x24.typed") typed = out;
    if (std::string(c.name) == "input.80x24.multiline") multiline = out;
    if (std::string(c.name) == "input.80x24.wrap") wrapped = out;
    if (std::string(c.name) == "input.80x24.stacked-multiline") stacked_ml = out;
    if (std::string(c.name) == "input.80x24.select-all") select_all_copy = copied_part(out);
    if (std::string(c.name) == "input.80x24.drag") in_drag_copy = copied_part(out);
    if (std::string(c.name) == "input.80x24.dblclick") in_dbl_copy = copied_part(out);
    if (std::string(c.name) == "input.80x24.submit") submitted = out;
    if (std::string(c.name) == "input.80x24.history") history = out;
    if (std::string(c.name) == "input.80x24.edit") edited = out;
    if (std::string(c.name) == "input.80x24.paste") pasted = out;
    if (std::string(c.name) == "input.80x24.cap") capped = out;
    if (std::string(c.name) == "menu.80x24.open") menu_open = out;
    if (std::string(c.name) == "menu.80x24.theme") menu_theme = out;
    if (std::string(c.name) == "menu.80x24.choose-light") menu_light = out;
    if (std::string(c.name) == "menu.80x24.filter") menu_filter = out;
    if (std::string(c.name) == "menu.80x24.left") menu_left = out;
    if (std::string(c.name) == "menu.80x24.escape") menu_escape = out;
    if (std::string(c.name) == "menu.80x24.toggle") menu_toggle = out;
    if (std::string(c.name) == "menu.80x24.palette") menu_palette = out;
    if (std::string(c.name) == "menu.80x24.palette-choose") menu_palette_choose = out;
    if (std::string(c.name) == "menu.120x40.open") menu_big = out;
    if (std::string(c.name) == "editor.120x40.open") ed_open = out;
    if (std::string(c.name) == "editor.120x40.heading-fg") ed_fg = out;
    if (std::string(c.name) == "editor.120x40.heading-cancel") ed_cancel = out;
    if (std::string(c.name) == "editor.120x40.heading-commit") ed_commit = out;
    if (std::string(c.name) == "editor.120x40.heading-undo") ed_undo = out;
    if (std::string(c.name) == "editor.120x40.confirm") ed_confirm = out;
    if (std::string(c.name) == "editor.120x40.save") ed_save = out;
    if (std::string(c.name) == "editor.120x40.check") ed_check = out;
    if (std::string(c.name) == "editor.120x40.fixes") ed_fixes = out;
    if (std::string(c.name) == "layout-editor.120x40.open") le_open = out;
    if (std::string(c.name) == "layout-editor.120x40.split") le_split = out;
    if (std::string(c.name) == "layout-editor.120x40.undo") le_undo = out;
    if (std::string(c.name) == "layout-editor.120x40.border-preview") le_preview = out;
    if (std::string(c.name) == "layout-editor.120x40.border-cancel") le_cancel = out;
    if (std::string(c.name) == "layout-editor.120x40.drag") le_drag = out;
    if (std::string(c.name) == "layout-editor.120x40.click") le_click = out;
    if (std::string(c.name) == "layout-editor.120x40.save") le_save = out;
    std::string path = frames + c.name + ".txt";
    if (record) {
      std::ofstream f(path, std::ios::binary);
      f << out;
      std::printf("  recorded %s\n", path.c_str());
      continue;
    }
    bool ok;
    std::string want = read_file(path, ok);
    check(ok, std::string(c.name) + ": golden exists (" + path + ")");
    if (ok && want != out) {
      // Name the first differing row.
      std::istringstream a(want), b(out);
      std::string ra, rb;
      int line = 0;
      while (true) {
        bool ha = static_cast<bool>(std::getline(a, ra)), hb = static_cast<bool>(std::getline(b, rb));
        ++line;
        if (!ha && !hb) break;
        if (ra != rb || ha != hb) {
          check(false, std::string(c.name) + ": frame differs from the golden at row " + std::to_string(line) +
                           "\n         golden [" + ra + "]\n         got    [" + rb + "]");
          break;
        }
      }
    } else if (ok) {
      check(true, std::string(c.name) + ": matches the golden");
    }
  }
  if (!record) {
    check(!bottom.empty() && !top.empty() && bottom != top, "the default frame follows the bottom; Home shows the top; they differ");
    check(bottom.find("\xE2\x96\xBC") == std::string::npos && top.find("\xE2\x96\xBC ") != std::string::npos,
          "the ▼ N more marker appears only when lines are hidden below");
    check(!popup.empty() && popup.find("\xE2\x95\xAD help ") != std::string::npos && popup.find("focus:help") != std::string::npos,
          "p opens the help popup (rounded ╭ help title, focus:help)");
    check(!popup_closed.empty() && popup_closed == bottom, "p then Escape gives back exactly the frame without the popup");
    // The popup re-places itself: at 120x40 its top edge sits on a different row than
    // at 80x24. The layout area is the screen minus the playground's one-line status
    // bar (80x23 / 120x39), so the centred 12-high popup starts at floor(23/2)-6 = 5
    // and floor(39/2)-6 = 13 — the same arithmetic the table test does on a full
    // 80x24 (row 6).
    auto row_of = [](const std::string& frame, const char* needle) {
      std::istringstream in(frame);
      std::string row;
      int y = 0;
      while (std::getline(in, row)) { if (row.find(needle) != std::string::npos) return y; ++y; }
      return -1;
    };
    check(row_of(popup, "\xE2\x95\xAD help ") == 5 && row_of(popup_big, "\xE2\x95\xAD help ") == 13,
          "the popup's top edge is on row 5 at 80x24 and row 13 at 120x40 (" + std::to_string(row_of(popup, "\xE2\x95\xAD help ")) +
              ", " + std::to_string(row_of(popup_big, "\xE2\x95\xAD help ")) + ")");
    // ---- milestone 9: the widget's behaviour, asserted beyond the bytes ----
    check(tools_top.find("\xE2\x96\xB8 read_file") != std::string::npos && tools_top.find("void stop_heartbeat") == std::string::npos,
          "tool blocks start folded: the summary line shows, the body does not");
    check(unfold_click.find("void stop_heartbeat") != std::string::npos && unfold_click.find("\xE2\x96\xBE read_file") != std::string::npos,
          "a click on the summary line unfolds the block (▾ and its first body line appear)");
    // Ctrl-O moves no focus and the click does, so compare the frames minus the rows
    // that name the focus (the status panel's row and the playground's status line).
    auto without_focus = [](const std::string& s) {
      std::istringstream in(s);
      std::string row, out;
      while (std::getline(in, row))
        if (row.find("focus") == std::string::npos) out += row + "\n";
      return out;
    };
    check(!unfold_click.empty() && without_focus(unfold_click) == without_focus(unfold_ctrl_o),
          "a click on the summary and Ctrl-O unfold the same block to the same frame");
    check(drag_copy == "read the manager header and tell me what stop_heartbeat do",
          "a drag across a wrapped prompt copies its text with no line break at the wrap [" + drag_copy + "]");
    check(dbl_copy == "the", "a double-click copies the word under the pointer [" + dbl_copy + "]");
    check(triple_copy.rfind("stop_heartbeat cancels the heartbeat timer", 0) == 0 && triple_copy.find('\n') == std::string::npos &&
              triple_copy.find("already running.") != std::string::npos,
          "a triple-click copies the whole paragraph as one logical line");
    const std::string auto_copy = copied_part(autoscroll_out);
    check(auto_copy.rfind("read the manager header", 0) == 0 && auto_copy.find("\n\xE2\x96\xB8") == std::string::npos &&
              auto_copy.find("\nread_file include") != std::string::npos && auto_copy.find("stop_heartbeat cancels") != std::string::npos,
          "a drag held past the bottom edge auto-scrolls and copies across entries (a folded block contributes its summary) [" +
              auto_copy.substr(0, 120) + "...]");
    check(autoscroll_out.find("line 1/27") == std::string::npos && autoscroll_out.find("line 9/27  follow") != std::string::npos,
          "two ticks past the bottom edge reach the end (line 9/27) and follow re-engages there");
    // ---- milestone 10: the input widget, asserted beyond the bytes ----
    auto row = [](const std::string& frame, int y) {
      std::istringstream in(frame);
      std::string r;
      for (int i = 0; i <= y; ++i) if (!std::getline(in, r)) return std::string();
      return r;
    };
    check(row(typed, 21).rfind("\xE2\x94\x82 > hello world", 0) == 0, "typed text shows after the prompt on the input row, one cell in [" + row(typed, 21) + "]");
    check(row(multiline, 20).rfind("\xE2\x94\x82 > one", 0) == 0 && row(multiline, 21).rfind("\xE2\x94\x82   two", 0) == 0 &&
              row(multiline, 19).rfind("\xE2\x94\x9C", 0) == 0,
          "Alt-Enter makes a second row: the window grew upward by one and the transcript's bottom border moved up");
    check(row(wrapped, 20).find("over the lazy dog") != std::string::npos && row(wrapped, 21).rfind("\xE2\x94\x82    and keeps running", 0) == 0,
          "a long line cell-wraps at the window's width (43 text cells: the space after 'dog' starts the second row, under the hanging indent)");
    check(row(stacked_ml, 21) == "> one" && row(stacked_ml, 22) == "  two", "the stacked layout's borderless input grows the same way");
    check(select_all_copy == "hello world", "Ctrl-A then Alt-C copies the whole input [" + select_all_copy + "]");
    check(in_drag_copy == "hello", "a drag inside the input from h to o copies hello [" + in_drag_copy + "]");
    check(in_dbl_copy == "world", "a double-click inside the input copies the word [" + in_dbl_copy + "]");
    check(submitted.find("\xE2\x94\x82 > hi there") != std::string::npos && row(submitted, 21).rfind("\xE2\x94\x82 > type here", 0) == 0,
          "Enter appends the text to the transcript as a user entry and empties the input (placeholder back)");
    check(row(history, 21).rfind("\xE2\x94\x82 > first", 0) == 0 && history.find("\xE2\x94\x82 > second") != std::string::npos,
          "Up twice after two submits recalls the older entry; both entries are in the transcript");
    check(row(edited, 21).rfind("\xE2\x94\x82 > Jello there", 0) == 0,
          "Ctrl-Left, Shift-End, typing over the selection, Home, Delete, typing: \"Jello there\" [" + row(edited, 21) + "]");
    check(row(pasted, 20).rfind("\xE2\x94\x82 > line one", 0) == 0 && row(pasted, 21).rfind("\xE2\x94\x82   line two", 0) == 0,
          "a bracketed paste with a newline is inserted literally as two rows");
    check(row(capped, 12).rfind("\xE2\x94\x9C", 0) == 0 && row(capped, 13).rfind("\xE2\x94\x82   l6", 0) == 0 &&
              row(capped, 21).rfind("\xE2\x94\x82   l14", 0) == 0 && row(capped, 3).find("\xE2\x94\x82") == 0,
          "fourteen pasted lines: the input caps at half its 23-row parent (11 outer rows, 9 of text, top border on row 12), scrolled so l6..l14 show with the caret's line last; the transcript keeps the top half");
    // Degenerate sizes: every case above already asserted "ran, h rows, each within w
    // cells" — for the tiny frames that is the whole point (the user, 2026-09-01:
    // views can shrink to 1 or even 0 in either dimension; it must be graceful).
    check(!tiny_failed, "the 1x1, 2x2, 6x1, 1x6, 20x3 and 80x2 frames render (typed text, a paste, the help popup and Tab included) without a row out of bounds");
    // ---- milestone 11: the menu, asserted beyond the bytes ----
    check(menu_open.find("\xE2\x95\xAD menu ") != std::string::npos && menu_open.find("focus:menu") != std::string::npos &&
              menu_open.find("settings") != std::string::npos && menu_open.find("Theme") != std::string::npos,
          "F2 opens the menu popup (rounded ╭ menu title, focus:menu, the settings breadcrumb, the Theme row)");
    check(menu_theme.find("settings \xE2\x80\xBA Theme") != std::string::npos && menu_theme.find("\xE2\x80\xA2 default-dark") != std::string::npos,
          "Enter descends into the Theme choice: the breadcrumb grows and the current option is marked •");
    check(menu_light.find("theme   default-light") != std::string::npos && menu_light.find("default-light \xE2\x96\xB8") != std::string::npos &&
              menu_light.find("settings \xE2\x80\xBA Theme") == std::string::npos,
          "Down + Enter chooses default-light three levels deep by keyboard alone: the status says so, the choice shows its value, the menu is back at the top");
    check(!menu_light.empty() && menu_light != menu_open, "…and the frame changed (it went light)");
    check(menu_filter.find("settings  /lay") != std::string::npos && menu_filter.find("/lay                       \xE2\x94\x82") != std::string::npos && menu_filter.find("Colour depth") == std::string::npos,
          "typing \"lay\" filters the level to Layout and shows the filter after the breadcrumb");
    check(!menu_left.empty() && menu_left == menu_open, "Enter then Left gives back exactly the opened frame");
    check(!menu_escape.empty() && menu_escape == bottom, "F2 then Escape gives back exactly the frame without the menu");
    check(menu_toggle.find("[x] Ambiguous width") != std::string::npos, "Enter on the toggle shows [x]");
    check(menu_palette.find("Theme \xE2\x80\xBA mono") != std::string::npos && menu_palette.find("Layout") == std::string::npos,
          "Ctrl-P opens the palette: rows are paths, and \"mono\" filters to the one theme option");
    check(menu_palette_choose.find("stacked") != std::string::npos && menu_palette_choose.find("\xE2\x95\xAD menu ") != std::string::npos &&
              menu_palette_choose.find("\xE2\x94\x8C transcript") == std::string::npos,
          "Enter on a palette row chooses the stacked layout (borderless transcript) with the menu still open");
    // ---- milestone 14: the theme editor, asserted beyond the bytes ----
    check(ed_open.find("\xE2\x94\x8C theme editor ") != std::string::npos && ed_open.find("focus:editor") != std::string::npos && ed_open.find("Roles") != std::string::npos,
          "F4 opens the theme editor popup with focus and the Roles level");
    check(ed_fg.find("Roles \xE2\x80\xBA md_heading \xE2\x80\xBA fg") != std::string::npos && role_part(ed_fg) == "md_heading fg=#cba63a bg=#14161a bold" &&
              ed_fg.find("previewing") != std::string::npos,
          "Roles › md_heading › fg two entries down previews #cba63a on the heading and says previewing [" + role_part(ed_fg) + "]");
    check(role_part(ed_cancel) == "md_heading fg=#84b7f9 bg=#14161a bold" && ed_cancel.find("focus:editor") != std::string::npos,
          "Escape puts the committed #84b7f9 back and stays in the editor [" + role_part(ed_cancel) + "]");
    check(role_part(ed_commit) == "md_heading fg=#cba63a bg=#14161a bold" && ed_commit.find("undo 1") != std::string::npos,
          "Enter commits: the heading is #cba63a, undo depth 1 [" + role_part(ed_commit) + "]");
    check(role_part(ed_undo) == "md_heading fg=#84b7f9 bg=#14161a bold" && ed_undo.find("undo 0") != std::string::npos && ed_undo.find("redo 1") != std::string::npos,
          "Ctrl-Z undoes it: #84b7f9, undo 0, redo 1");
    check(ed_confirm.find("\xE2\x95\xAD confirm ") != std::string::npos && ed_confirm.find("built-in default? (y/n)") != std::string::npos,
          "Reset to the built-in default asks in a confirm popup, never applies bare");
    check(ed_save.find("saved preset 'mine'") != std::string::npos && std::filesystem::exists(scratch + "/p/themes/mine.json"),
          "save-as writes themes/mine.json under the scratch presets directory (a manual save writes even under --frame)");
    {
      int rc = 0;
      const std::string relaunch = std::string("'") + ROLLTUI_PLAYGROUND_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md' --frame 80x24 --presets '" +
                                   scratch + "/p2' --theme '" + scratch + "/p/themes/mine.json' --dump-role md_heading";
      const std::string again = run(relaunch, rc);
      check(rc == 0 && role_part(again) == "md_heading fg=#cba63a bg=#14161a bold", "a relaunch with --theme <that file> shows the saved heading colour [" + role_part(again) + "]");
      // Under --frame nothing autosaves from an edit; the explicit save-as records its
      // new origin in the working copy, which is the one write the frame runs made.
      bool ok = false;
      std::string wc = read_file(scratch + "/p/theme.working.json", ok);
      check(ok && wc.find("\"preset\": \"mine\"") != std::string::npos, "the working copy written by the save-as records preset 'mine' — and nothing wrote it before that (the earlier frames' edits did not persist)");
    }
    // ---- milestone 16: the layout editor, asserted beyond the bytes ----
    check(le_open.find("\xE2\x94\x8C layout editor ") != std::string::npos && le_open.find("layout editor \xE2\x80\xA2 transcript") != std::string::npos && le_open.find("focus:editor") != std::string::npos,
          "F6 opens the layout editor with the transcript selected");
    check(le_split.find("transcript-2") != std::string::npos && le_split.find("\xE2\x94\xAC") != std::string::npos && le_split.find("selected: transcript") != std::string::npos,
          "split into a row: a second transcript pane appears beside the first (a ┬ junction on the top edge)");
    check(!le_undo.empty() && le_undo.find("transcript-2") == std::string::npos && le_undo.find("(modified)") == std::string::npos && le_undo.find("redo 1") != std::string::npos,
          "Ctrl-Z after the split removes the second pane, the working copy reads unmodified again (the undo was written back), redo 1");
    check(le_preview.find("\xE2\x95\xAD transcript") != std::string::npos && le_preview.find("previewing") != std::string::npos,
          "the Border choice on rounded previews a rounded transcript border and says previewing");
    check(!le_cancel.empty() && le_cancel.find("\xE2\x95\xAD transcript") == std::string::npos && le_cancel.find("\xE2\x94\x8C transcript") != std::string::npos,
          "Escape puts the single border back");
    check(le_drag.find("selected: transcript ") != std::string::npos && le_drag.find("size 31") != std::string::npos && le_drag.find("undo 2") != std::string::npos,
          "after the split, a press on the seam between the two panes dragged left narrows the first to 31 cells, one more commit");
    check(le_click.find("layout editor \xE2\x80\xA2 input") != std::string::npos, "a click on the input window selects it");
    check(le_save.find("saved layout file") != std::string::npos && std::filesystem::exists(scratch + "/p/layouts/two.json") && le_save.find("\xE2\x94\x8C chat ") != std::string::npos,
          "title 'chat' and save-as 'two' write layouts/two.json under the scratch presets directory");
    {
      int rc = 0;
      const std::string relaunch = std::string("'") + ROLLTUI_PLAYGROUND_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md' --frame 120x40 --presets '" +
                                   scratch + "/p4' --layout '" + scratch + "/p/layouts/two.json'";
      const std::string again = run(relaunch, rc);
      check(rc == 0 && again.find("\xE2\x94\x8C chat ") != std::string::npos && again.find("\xE2\x94\xAC transcript-2 ") != std::string::npos && again.find("[layout editor]") == std::string::npos,
            "a relaunch with --layout <that file> shows the two panes ('chat' and 'transcript-2') with no editor open (Done-when of m16)");
    }
    // ---- milestone 15: --check, --generate, the Check popup ----
    check(ed_check.find("\xE2\x95\xAD report ") != std::string::npos && ed_check.find("badges: dark") != std::string::npos && ed_check.find("roles (fg on bg") != std::string::npos,
          "Check opens the report popup with the badges and the per-role numbers");
    check(ed_fixes.find("nothing to fix") != std::string::npos, "the shipped default has nothing to fix");
    {
      int rc = 0;
      const std::string bin = std::string("'") + ROLLTUI_PLAYGROUND_BIN + "'";
      for (const char* name : {"default", "default-dark", "default-light", "mono"}) {
        const std::string out = run(bin + " --check " + name + presets, rc);
        check(rc == 0 && out.find("badges:") != std::string::npos, std::string("--check ") + name + " runs, prints badges, exit 0");
        if (std::string(name) == "default") check(out.find("badges: dark") != std::string::npos && out.find("badges: light") != std::string::npos && out.find("cvd-safe") != std::string::npos,
                                                  "--check default reports both variants, and the dark one cvd-safe");
      }
      const std::string g1 = run(bin + " --generate triadic --seed 3 --chaos 0", rc);
      const std::string g2 = run(bin + " --generate triadic --seed 3 --chaos 0", rc);
      check(rc == 0 && !g1.empty() && g1 == g2 && g1.find("\"generator\"") != std::string::npos, "--generate is deterministic and records its inputs in meta");
      std::ofstream(scratch + "/gen.json", std::ios::binary) << g1;
      const std::string dumped = run(bin + " '" + std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md' --frame 80x24 --presets '" + scratch + "/p3' --theme '" + scratch + "/gen.json' --dump-role md_heading", rc);
      check(rc == 0 && role_part(dumped).rfind("md_heading fg=#", 0) == 0, "a generated file loads as a colours-only theme [" + role_part(dumped) + "]");
      const std::string checked = run(bin + " --check '" + scratch + "/gen.json'" + presets, rc);
      check(rc == 0 && checked.find("every claimed badge holds") != std::string::npos, "--check on it: every badge the generator claimed holds");
      // A false claim fails the check.
      std::string err;
      rolltui::json::Value lying = rolltui::json::parse(g1, err);
      rolltui::json::Value claims = rolltui::json::Value::array();
      claims.arr.push_back(rolltui::json::Value::string("high-contrast"));
      claims.arr.push_back(rolltui::json::Value::string("mono"));
      rolltui::json::Value meta = lying.get("meta");
      meta.set("badges", claims);
      lying.set("meta", meta);
      std::ofstream(scratch + "/lying.json", std::ios::binary) << rolltui::json::dump(lying, 2);
      const std::string liar = run(bin + " --check '" + scratch + "/lying.json'" + presets, rc);
      check(rc != 0 && liar.find("CLAIM FAILED: mono") != std::string::npos, "a file claiming a badge it does not have fails --check with the claim named");
    }
    check(row_of(menu_open, "\xE2\x95\xAD menu ") == 5 && row_of(menu_big, "\xE2\x95\xAD menu ") == 8,
          "the menu popup re-places itself: top edge on row 5 at 80x24 (60% of 23 = 13 rows, centred: 11 - 6) and row 8 at 120x40 (23 rows: 19 - 11) (" +
              std::to_string(row_of(menu_open, "\xE2\x95\xAD menu ")) + ", " + std::to_string(row_of(menu_big, "\xE2\x95\xAD menu ")) + ")");
  }
  std::filesystem::remove_all(scratch);
  return report("rolltui playground_golden_test");
}
