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
};

}  // namespace

int main(int argc, char** argv) {
  const bool record = (argc > 1 && std::strcmp(argv[1], "--record") == 0);
  const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md";
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
  };
  std::string bottom, top, popup, popup_closed, popup_big;
  for (const Case& c : cases) {
    int rc = 0;
    std::string cmd = std::string("'") + ROLLTUI_PLAYGROUND_BIN + "' '" + fixture + "' " + c.args;
    std::string out = run(cmd, rc);
    check(rc == 0 && !out.empty(), std::string(c.name) + ": playground ran (rc " + std::to_string(rc) + ", " +
                                       std::to_string(out.size()) + " bytes)");
    // Every row fits the width, in cells.
    int w = std::atoi(std::strstr(c.args, "--frame ") + 8);
    int rows = 0;
    bool fits = true;
    std::istringstream in(out);
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
  }
  return report("rolltui playground_golden_test");
}
