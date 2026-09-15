//
// files_only_test.cpp — THE PROOF: a screen that exists only as FILES runs in a host that
// has never heard of it.
//
// Everything the screen is lives in rolltui/tests/fixtures/screen/, which this test
// copies into a scratch preset directory before running the REAL rolltui-studio
// binary against it:
//
//   layouts/kettle.json    the design — a transcript:, a text:, a file:, a menu: and a
//                          help window, a find popup, and the TWO actions it emits
//   menus/kettle.json      the menu that window shows; its first row NAMES app.kettle
//   bindings/kettle.json   the keys — it is the only thing that gives app.kettle and
//                          app.find their chords
//   docs/kettle.md         the document the file: window reads
//   docs/kettle-session.md the document the transcript: window reads, and the studio's
//                          fixture argument — a marked span, a ```diff fence, and more
//                          lines than the window has rows
//   layouts/kettle-silent.json   the same screen declaring NO actions, for the VERIFY rung
//
// ONE SCREEN CARRIES EVERY CAPABILITY, rather than one screen per capability, because the
// claim is about COMPOSITION. The cheapest way to be wrong about "it reaches a screen
// through files" is to demonstrate each capability in a host written for it. So the same
// six files also carry
//
//   a MARKED SPAN      docs/kettle-session.md's `<!-- state: waiting -->` entry, which
//                      the theme turns into a spinner
//   a HIGHLIGHTED      the same document's ```diff fence, coloured by the highlighter
//   CODE BLOCK         the host registered — asserted on the SGR bytes, since colour is
//                      the whole point and a text frame cannot show it
//   a SCROLLBAR        the transcript window is smaller than its document, so it reports
//                      a scroll extent and the window draws a thumb
//   FIND               Ctrl-F, from the bindings file, opening the find popup the LAYOUT
//                      file declares — not a mode any host implements
//
// None of that needs a line of host code, which is the claim. Composing them on ONE screen
// is also what found two defects that each capability's own suite had passed over:
// a window whose right border is shared with a bordered neighbour had its thumb drawn and
// then overwritten by that neighbour's border (so roll's own shipped `default` layout had
// an invisible scrollbar), and the thumb glyph █ is East Asian AMBIGUOUS, so on a
// wide-ambiguous terminal it overflowed the one-cell border column.
//
// NOTHING HERE IS A NEW CAPABILITY. Widgets sit behind kinds, menus are files, layouts
// declare their actions, and the editor authors them; this file is the claim that the four
// COMPOSE. So the assertions below are deliberately end-to-end and the controls are what
// carry the weight:
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
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

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

  // Working memory for display_width (rolltui/c/rolltui_unicode.h): CALLER-OWNED, made
  // once and reused for every row of every case below, freed before the one return.
  // (named u_scratch, not scratch — that name is already the preset scratch directory's)
  RolltuiUnicodeScratch* u_scratch = rolltui_u_scratch_new();

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
            fs::exists(presets + "/bindings/kettle.json") && fs::exists(presets + "/docs/kettle.md") &&
            fs::exists(presets + "/docs/kettle-session.md") && fs::exists(presets + "/layouts/kettle-silent.json"),
        "…all six of them: two layouts, a menu, a bindings file and two documents");

  // The startup trace ("no working copy; started from the shipped 'default'") is stderr
  // and expected; keep it out of the frames and out of ctest's output.
  const std::string err = " 2>>'" + scratch + "/stderr.txt'";
  // THE DOCUMENT IS ONE OF THE SCREEN'S FILES. The studio's fixture argument is how a
  // host says "this is the transcript's document", so pointing it at the screen's own
  // docs/kettle-session.md is what makes the transcript window part of the same six files
  // as everything else — rather than the screen borrowing the library's demo fixture.
  const std::string fixture = presets + "/docs/kettle-session.md";
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
      // Tab focuses the help window and scrolls it to the app scope — one action, declared
      // by a layout file, described by it, keyed by a bindings file.
      //
      // The scroll amount is ARITHMETIC, not a feel: kettle.json gives this window 8 rows,
      // so 6 of content, and PageDown moves exactly one viewport. The assertion below needs
      // the "app:" header, its one action row and the following "editor:" header all in
      // view at once, so the scroll offset has to land in a FOUR-line window — and a whole
      // number of pages does not always fall inside it. Six pages (offset 36) plus FOUR
      // lines (40) puts "app:" on the fourth visible row. **ANY CHANGE THAT GROWS
      // `library_actions()` MOVES THE APP SCOPE DOWN THIS TABLE AND THIS NUMBER MUST BE
      // RECOMPUTED.** The recipe, so the next one is arithmetic and not archaeology: count
      // the rows your change adds BEFORE the "app:" header and add that many `Down`s. A row
      // added INSIDE the app scope counts too, for the opposite reason — it pushes the next
      // header down, so the four-line window has to START one line higher.
      {"files-only.80x24.app-scope",
       "--frame 80x24 --layout kettle --bindings kettle --keys \"Tab PageDown PageDown PageDown PageDown PageDown "
       "PageDown Down Down Down Down Down\""},
      // The SAME screen reached by switching layouts at runtime (F2 › Layout › kettle),
      // which is the only path that can accumulate declarations: the app scope must still
      // be this layout's one action and not also the five the layout we started on
      // declared. Loading a bindings file rebuilds the table, so the launch-time case
      // above cannot tell an authoritative declare() from an additive one — this one can.
      {"files-only.80x24.switched",
       "--frame 80x24 --layout default --bindings kettle --keys \"F2 Type:Appearance Enter Type:lay Enter Type:kettle Enter Escape Escape Escape Tab "
       "PageDown PageDown PageDown PageDown PageDown PageDown Down Down Down Down Down\""},
      // The VERIFY rung: a layout declaring nothing. The menu item's action is reported
      // by name in the status line and its shortcut is gone.
      {"files-only.100x14.silent", "--frame 100x14 --layout kettle-silent --bindings kettle"},
      // THE STANDING RULE: every view shrinks to 1 or 0 cells in
      // either dimension and stays graceful — with the menu driven.
      // the four capabilities, on the same screen and from the same files.
      // `--tick 240` fixes the effect clock so the marked span records deterministically;
      // Tab Tab reaches the transcript from the menu the layout focuses, and Home puts
      // the marked entry and the diff block in view together.
      {"files-only.120x40.capabilities", "--frame 120x40 --layout kettle --bindings kettle --tick 240 --keys \"Tab Tab Home\""},
      // Ctrl-T is the bindings file's — deliberately a chord NO shipped default binds,
      // so the control below discriminates; the popup it opens is the layout file's; the
      // count is the widget's and the status line is the host's. No find MODE anywhere.
      {"files-only.120x40.find", "--frame 120x40 --layout kettle --bindings kettle --tick 240 --keys \"CtrlT Type:target\""},
      {"files-only.20x6", "--frame 20x6 --layout kettle --bindings kettle --keys \"Down Enter\""},
      {"files-only.1x1", "--frame 1x1 --layout kettle --bindings kettle --keys \"Down Enter Tab\""},
  };

  std::string screen, menu_open, app_scope, switched, silent, caps, found;
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
      if (rolltui_u_display_width(u_scratch, row.data(), row.size(), false) > w) {
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
    if (n == "files-only.120x40.capabilities") caps = out;
    if (n == "files-only.120x40.find") found = out;
  }

  // ---- what the golden alone does not say --------------------------------------------
  {
    // Each of the four kinds actually drew its own content, and the window note is empty
    // (an unknown kind, an unbound source or an unreadable file: would put it in the
    // status line as "[...]" and draw an error panel instead).
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
    // chords rather than from any "shortcut" string in the file.
    check(has(screen, "Put the kettle on       Ctrl-J"),
          "the menu row carries the chord the bindings file gave the action the layout declared");
    check(has(app_scope, "Ctrl-J      put the kettle on"),
          "…and help renders the same action with the layout file's own description");
    // Exactly one app action: the app scope is this screen's, not this screen's plus the
    // last one's, because declare() is authoritative rather than additive. The help window prints one
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
          if (i > at_app + 1) action_row = rows[at_app + 1];
          return i - at_app - 1;
        }
      }
      return -1;
    };
    std::string action_row;
    // TWO (app.kettle and app.find), and the number is the point: it is exactly
    // what THIS layout file declares. `app_scope_size` reports the rows between the "app:"
    // header and the next one, and `action_row` is the first of them.
    check(app_scope_size(app_scope, action_row) == 2 && has(action_row, "put the kettle on"),
          "the app scope is two actions long — this layout's, and no other layout's [" + action_row + "]");
    // The discriminating one: reached by SWITCHING layouts at runtime, where an additive
    // declare() would leave the previous screen's five app actions live beneath this one.
    std::string switched_row;
    check(app_scope_size(switched, switched_row) == 2 && has(switched_row, "put the kettle on"),
          "…and still two after switching to this layout at runtime, not eight [" +
              std::to_string(app_scope_size(switched, switched_row)) + "]");
    check(has(status_line(switched), " kettle ") && !has(switched, "open the settings and commands menu"),
          "…the screen we switched away from left no action of its own behind");

    // Menu navigation, with nothing in the host knowing this tree.
    // The level's path is the menu WINDOW's title — the layout's own title, then the path.
    check(has(menu_open, "menus/kettle.json \xE2\x80\xBA About this") && has(menu_open, "layouts/kettle.json") &&
              has(menu_open, "bindings/kettle.json"),
          "Down + Enter descends a level of a menu that is only a file");
  }

  // ---- the four capabilities, each reached through a file ---------------------------
  // The screen's files say `waiting`, ```diff, a window smaller than its document, and
  // app.find. No host source says any of it — which the grep control at the bottom is
  // what actually establishes; these assertions establish that it WORKED.
  {
    // (1) A MARKED SPAN. docs/kettle-session.md's entry is the still "· waiting …" the
    // document itself wrote; the shipped theme is what turns the "·" into a braille frame.
    const std::size_t at = caps.find(" waiting for the kettle to boil");
    check(at != std::string::npos, "the marked entry is on screen");
    check(at >= 3 && static_cast<unsigned char>(caps[at - 3]) == 0xE2 && static_cast<unsigned char>(caps[at - 2]) == 0xA0,
          "a MARKED SPAN: the theme's braille spinner replaced the still '·' the document wrote");
    check(!has(caps, "\xC2\xB7 waiting"), "…so the still frame is gone while it spins");

    // (2) A HIGHLIGHTED CODE BLOCK. The fence — not the content — is what says `diff`, and
    // the block's label proves the renderer read it as one.
    check(has(caps, "\xE2\x94\x8C diff ") && has(caps, "-  int target = 80;") && has(caps, "+  int target = 100;"),
          "a HIGHLIGHTED CODE BLOCK: the ```diff fence names the block and its pair is drawn");

    // (3) A SCROLLBAR. The transcript is smaller than its document, so it reports an
    // extent and the WINDOW draws a thumb in its right border column — which is the
    // column it SHARES with the menu beside it — the case where the neighbour's border
    // silently overwrote the thumb. Asserting the thumb on the marked entry's own row is what ties
    // it to this window rather than to some other one on the screen.
    std::vector<std::string> rows;
    {
      std::istringstream in(caps);
      std::string r;
      while (std::getline(in, r)) rows.push_back(r);
    }
    std::string marked_row;
    for (const std::string& r : rows)
      if (has(r, "waiting for the kettle to boil")) marked_row = r;
    // ANY OF THE CAPSULE'S CELLS COUNTS. A thumb is an oval alone, or a lower-half cap, a body
    // and an upper-half cap — naming one of them would make this a statement about the thumb's
    // LENGTH, which belongs to the layout and not to this check. A theme may replace all four,
    // so this is the shipped set and a theme that changes them re-records the frames with it.
    const bool has_thumb = has(marked_row, "\xE2\x94\x83") || has(marked_row, "\xE2\x95\xBB") ||
                           has(marked_row, "\xE2\x95\xB9") || has(marked_row, "\xE2\x80\xA2");
    check(has_thumb && has(marked_row, "Put the kettle on"),
          "a SCROLLBAR: the thumb is in the transcript's right border column — the one it SHARES with the menu [" +
              marked_row.substr(0, 40) + " … ]");

    // (4) FIND. Ctrl-F is the bindings file's, the popup is the layout file's, the count
    // is the widget's. There is no find mode in any host.
    check(has(found, "\xE2\x95\xAD find ") && has(found, "focus:find"),
          "FIND: Ctrl-T opened the popup THIS LAYOUT FILE declares, and focused its input");
    check(has(found, "> target") && has(status_line(found), "find 2/4"),
          "…the query is the bar's own text and the count is visible [" + status_line(found) + "]");
  }

  // ---- control: a theme that maps nothing leaves the document's own still frame -------
  // The same six files, one theme swapped. This is the degrade rung as a control: if the
  // spinner above came from anything but the theme, this would still spin.
  {
    int rc = 0;
    const std::string out = run(play(std::string("--frame 120x40 --layout kettle --bindings kettle --tick 240 --keys "
                                                 "\"Tab Tab Home\" --theme '") +
                                    ROLLTUI_FIXTURE_DIR + "/themes/still.json'"),
                                rc);
    check(rc == 0 && has(out, "\xC2\xB7 waiting for the kettle to boil"),
          "under a theme that maps no effects the SAME document shows the still '·' it wrote");
  }

  // ---- control: the block's COLOUR, which a text frame cannot carry -------------------
  // The one capability whose whole point is invisible in `frame_to_text`. `--dump-role`
  // says what the theme resolved the role to and `--frame-sgr` is the frame with its
  // escape sequences, so the assertion is that THOSE BYTES are in THAT frame — not that
  // a renderer test passed somewhere else.
  {
    auto sgr_fg = [&](const char* role, std::string& why) {
      int rc = 0;
      const std::string out = run(play(std::string("--frame-sgr 120x40 --depth truecolor --layout kettle --bindings kettle "
                                                   "--tick 240 --keys \"Tab Tab Home\" --dump-role ") +
                                       role),
                                  rc);
      const std::size_t at = out.find(std::string(role) + " fg=#");
      if (rc != 0 || at == std::string::npos) { why = "could not read the role"; return false; }
      const std::string hex = out.substr(at + std::strlen(role) + 5, 6);
      const long v = std::strtol(hex.c_str(), nullptr, 16);
      const std::string want = "38;2;" + std::to_string((v >> 16) & 0xff) + ";" + std::to_string((v >> 8) & 0xff) + ";" +
                               std::to_string(v & 0xff);
      why = std::string(role) + " #" + hex + " → " + want;
      return out.find(want) != std::string::npos;
    };
    std::string why_a, why_r;
    const bool added = sgr_fg("diff_added", why_a), removed = sgr_fg("diff_removed", why_r);
    check(added && removed, "the diff roles are in the frame's SGR bytes, so the block is actually COLOURED [" + why_a +
                                " | " + why_r + "]");
  }

  // ---- control: the bindings FILE is what supplies the chord --------------------------
  {
    int rc = 0;
    const std::string out = run(play("--frame 80x24 --layout kettle"), rc);  // no --bindings
    check(rc == 0 && has(out, "Put the kettle on") && !has(out, "Ctrl-J"),
          "without bindings/kettle.json the same row shows NO chord — the file is what bound it");
    int rc2 = 0;
    const std::string nofind = run(play("--frame 120x40 --layout kettle --keys \"CtrlT Type:target\""), rc2);
    check(rc2 == 0 && !has(nofind, "\xE2\x95\xAD find ") && !has(status_line(nofind), "find "),
          "…and Ctrl-T opens nothing: the find chord came from that file too, not from the host");
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
          {"id":"second_menu","content":"menu:second","border":"single","focusable":true}]}})";  // untitled: the menu's own root label names the window
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
      // `.c` IS SCANNED. The library is C, so a control that skipped `.c` would be looking
      // at a shell around the code it means to check.
      if (ext != ".c" && ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
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
    check(scanned.size() >= 60, "scanned every source the studio binary is built from, C included (" +
                                    std::to_string(scanned.size()) + " files)");
    check(hits.empty(), "no source of the library or its hosts names this screen — it is files all the way down" +
                            (hits.empty() ? "" : ": " + hits.front()));
  }

  // ---- AND THE WAY BACK IS NOT IN ANY FILE EITHER ---------------------------------------
  // Every menu file that ships — the library's, and the one this fixture screen carries — is
  // read for an item that offers a way out of a level. There is none, and there must be none:
  // a file author who has to write one is a file author who can forget to, and the time they
  // forget is the time somebody is stuck in a level with no visible exit. The widget draws it,
  // which is why the frames above show it on a screen whose menu was written by a person who
  // never thought about it.
  {
    std::vector<std::string> menus, declaring;
    for (const fs::path& dir : {fs::path(ROLLTUI_SOURCE_DIR) / "presets" / "menus",
                                fs::path(presets) / "menus"}) {
      if (!fs::exists(dir)) continue;
      for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".json") continue;
        menus.push_back(e.path().filename().string());
        bool ok = false;
        std::string text = read_file(e.path().string(), ok);
        if (!ok) continue;
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (text.find("back") != std::string::npos || text.find("\xE2\x97\x82") != std::string::npos)
          declaring.push_back(e.path().filename().string());
      }
    }
    check(menus.size() >= 2, "read every shipped menu file and this screen's own (" + std::to_string(menus.size()) + ")");
    check(declaring.empty(), "no menu FILE declares a way back — the widget owns it" +
                                 (declaring.empty() ? "" : ": " + declaring.front()));
  }

  fs::remove_all(scratch);
  rolltui_u_scratch_free(u_scratch);
  return report("rolltui_files_only_test");
}
