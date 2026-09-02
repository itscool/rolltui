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
// Milestone 9 added the transcript-widget cases on a second fixture (tools.md: two
// foldable tool blocks, a link, a list): folded by default; a click on the summary
// line and Ctrl-O both unfold the same block (asserted equal); a drag across a wrapped
// prompt copies its text with no line break; a double-click copies one word; a
// triple-click copies a whole paragraph; a drag held past the bottom edge auto-scrolls
// over two ticks and copies across entries; ▼ marker placement. The copied text
// follows the frame in the playground's output, so the golden holds both.
//
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
  const std::size_t at = out.find("--- copied ---\n");
  return at == std::string::npos ? out : out.substr(0, at);
}

}  // namespace

int main(int argc, char** argv) {
  const bool record = (argc > 1 && std::strcmp(argv[1], "--record") == 0);
  const std::string frames = std::string(ROLLTUI_FIXTURE_DIR) + "/frames/";
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
      {"demo.80x24.popup", "--frame 80x24 --theme default-dark --keys \"p\""},
      {"demo.120x40.popup", "--frame 120x40 --theme default-dark --keys \"p\""},
      {"demo.80x24.popup-closed", "--frame 80x24 --theme default-dark --keys \"p Escape\""},
      {"demo.80x24.file", "--frame 80x24 --theme default-dark --layout '" ROLLTUI_FIXTURE_DIR "/layouts/wide-left.json'"},
      {"demo.80x24.file-popup", "--frame 80x24 --theme default-dark --layout '" ROLLTUI_FIXTURE_DIR "/layouts/wide-left.json' --keys \"p\""},
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
  };
  std::string bottom, top, popup, popup_closed, popup_big;
  std::string tools_top, unfold_click, unfold_ctrl_o, drag_copy, dbl_copy, triple_copy, autoscroll_out;
  for (const Case& c : cases) {
    int rc = 0;
    const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/" + c.fixture;
    std::string cmd = std::string("'") + ROLLTUI_PLAYGROUND_BIN + "' '" + fixture + "' " + c.args;
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
  }
  return report("rolltui playground_golden_test");
}
