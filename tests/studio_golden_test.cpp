//
// studio_golden_test.cpp — golden frames through the studio's `--frame WxH`
// mode. Runs the REAL rolltui-studio binary on the demo session fixture and compares its
// stdout byte-for-byte with rolltui/tests/fixtures/frames/<case>.txt.
//
// Re-recording is a deliberate human act — `rolltui-studio-golden-test --record`
// — never something ctest does; look at the new frames before committing them (the
// studio's `--frame-sgr` shows the same frame in colour).
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
// follows the frame in the studio's output, so the golden holds both.
//
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// the C API, through the umbrella alone. This suite drives the REAL
// rolltui-studio binary as a subprocess and mostly matches its stdout as text, so the only
// library calls left are: a display-width check on each frame's rows, one layout round-trip
// (load a saved file back and ask what it has), and building two small bindings/JSON fixture
// files on disk for the studio to load. Small local helpers below stand in for the deleted
// `rolltui::unicode`/`rolltui::Layout`/`rolltui::Bindings`/`rolltui::json` shims — each mirrors
// that shim's own body over the C, the same idiom `layout_test.cpp` and `transcript_test.cpp`
// already use.
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_STUDIO_BIN
#error "ROLLTUI_STUDIO_BIN must name the studio binary"
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
  for (const char* trailer : {"--- role ---\n", "--- tick ---\n"}) {
    const std::size_t t = out.find(trailer);
    if (t != std::string::npos && (at == std::string::npos || t < at)) at = t;
  }
  return at == std::string::npos ? out : out.substr(0, at);
}
// The "--- tick ---" trailer's line (the ms this frame asks to be redrawn in, or
// "none"), "" when --dump-tick was not asked for.
std::string tick_part(const std::string& out) {
  const std::size_t at = out.find("--- tick ---\n");
  if (at == std::string::npos) return "";
  std::string s = out.substr(at + 13);
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}
// The "--- role ---" trailer's line ("md_heading fg=#.. bg=#.. bold"), "" when absent.
std::string role_part(const std::string& out) {
  const std::size_t at = out.find("--- role ---\n");
  if (at == std::string::npos) return "";
  std::string s = out.substr(at + 13);
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}

// ---- Unicode: the one per-row width check below. Identical local wrapper to
// transcript_test.cpp's — a scratch handle this binary owns for its life, CLAUDE.md's
// CALLER-FILLED working-memory strategy. ----
RolltuiUnicodeScratch* u_scratch() {
  static RolltuiUnicodeScratch* u = rolltui_u_scratch_new();
  return u;
}
namespace unicode {
inline int display_width(std::string_view s, bool ambiguous_wide) {
  return rolltui_u_display_width(u_scratch(), s.data(), s.size(), ambiguous_wide ? 1 : 0);
}
}  // namespace unicode

// ---- Layout: one round-trip check (a saved file loads back clean, with no loader notes
// and no actions/popups). Mirrors layout_test.cpp's `load_layout_c` over the same C calls,
// trimmed to the four answers this suite reads instead of a full `Layout` value. ----
struct LoadedLayoutCheck {
  bool ok = false;
  bool clean = false;
  bool no_notes = false;
  bool actions_empty = false;
  bool popups_empty = false;
  std::string error;
};
LoadedLayoutCheck load_layout_check(std::string_view json_text) {
  std::size_t default_actions_n = 0;
  const RolltuiLayoutAction* default_actions = rolltui_layout_shipped_default_actions(rolltui_test::test_context(), &default_actions_n);
  RolltuiLayoutReport rep{};
  // one call, an OWNED layout back, and the counts through the public doors.
  RolltuiLayout* out = rolltui_load_layout_text(json_text.data(), json_text.size(), default_actions,
                                                default_actions_n, rolltui_layout_default_hooks(), &rep);
  LoadedLayoutCheck result;
  result.ok = out != nullptr;
  result.clean = rolltui_layout_report_clean(&rep) != 0;
  result.no_notes = rep.notes_n == 0;
  result.error.assign(rep.error.p ? rep.error.p : "", rep.error.n);
  if (out) {
    std::size_t an = 0;
    rolltui_layout_actions(out, &an);
    result.actions_empty = an == 0;
    result.popups_empty = rolltui_layout_popup(out, "menu", 4) == nullptr;
    rolltui_layout_free(out);
  }
  rolltui_layout_report_release(&rep);
  return result;
}

// ---- Bindings + JSON: the shipped default table, and the two fixture files built below by
// editing its dumped JSON directly — no `rolltui::json::Value` (rolltui_json.h's own header
// comment: "there is no json::Value on the C side and there does not need to be one"). ----
std::string_view default_bindings_text() {
  const char* t = rolltui_embedded_text(rolltui_kBindingsPresets, rolltui_kBindingsPresetCount, "default", 7);
  return t ? std::string_view(t) : std::string_view();
}
// A fresh, mutable copy of the shipped default table, loaded exactly as
// `rolltui::Bindings::from_json(default_bindings_json(), report)` did: seeded with the
// library's own actions, then the shipped file's rows loaded onto it against the terminal's
// ACTIVE protocol. `reason` is NULL for the same reason `rolltui_bindings_default`'s own
// loader call (rolltui_bindings.c) passes it NULL: neither call site below reads the
// undeliverable-reason text.
RolltuiBindings* load_default_bindings() {
  RolltuiBindings* b = rolltui_bindings_new_seeded();
  RolltuiBindingsReport rep{};
  const std::string_view text = default_bindings_text();
  rolltui_bindings_load_json(b, text.data(), text.size(), rolltui_key_active_protocol(), rolltui_bindings_library_scope,
                             nullptr, nullptr, nullptr, &rep);
  rolltui_bindings_report_release(&rep);
  return b;
}
std::string bindings_to_json_text(const RolltuiBindings* b, std::string_view name) {
  RolltuiStr out{};
  rolltui_bindings_dump_json(b, name.data(), name.size(), &out);
  std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}
// string_view-friendly spellings of the four calls below, so no call site counts bytes by
// hand (rolltui_json.h's own functions take a length because C has no `std::string_view`).
const RolltuiJsonValue* json_get_v(const RolltuiJsonValue* v, std::string_view key) {
  return rolltui_json_get(v, key.data(), key.size());
}
RolltuiJsonValue* json_set_v(RolltuiJsonValue* v, std::string_view key, RolltuiJsonValue* child) {
  return rolltui_json_set(v, key.data(), key.size(), child);
}
RolltuiJsonValue* json_string_v(std::string_view s) { return rolltui_json_string(s.data(), s.size()); }
std::string json_dump_text(const RolltuiJsonValue* v, int indent) {
  RolltuiStr out{};
  rolltui_json_dump(v, indent, &out);
  std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
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

  // ---- THE PRODUCT BINARY CANNOT DRIVE ITSELF ----------------------------------------------
  // Additive, not compiled out: `rolltui-studio-selftest` is this same source plus the script
  // vocabulary, and the shipped studio does not contain it.
  //
  // `--check` and `--generate` are the OTHER side of that line and stay in the product. They run
  // the accessibility checker and the seeded generator, which the theme editor also offers from
  // inside the app — that makes them a non-interactive entry to a shipped capability, and a
  // headless entry is the whole point of one: CI has no terminal to open the editor in.
  {
    int prc = 0;
    const std::string product = std::string("'") + ROLLTUI_STUDIO_PRODUCT_BIN + "'";
    const std::string in_product = run("strings " + product + " | grep -cx TripleClick", prc);
    check(in_product.substr(0, 1) == "0", "the script vocabulary is absent from the shipped studio");
    const std::string in_selftest = run(std::string("strings '") + ROLLTUI_STUDIO_BIN + "' | grep -cx TripleClick", prc);
    check(in_selftest.substr(0, 1) != "0", "…while the self-test binary has it, so the marker discriminates");
    const std::string feat = run(product + " --check default 2>&1", prc);
    check(feat.find("badges:") != std::string::npos,
          "…while --check RUNS in the shipped studio: it is a feature reached headlessly, not a hook");
    const std::string hook = run(product + " --dump-role md_heading 2>&1", prc);
    check(hook.find("usage:") != std::string::npos,
          "…and --dump-role, which only a golden frame ever wanted, is not there");
  }
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
      // milestone 12.4 — find. The bar is an ordinary `input:` window in an ordinary
      // popup, so these frames say three things a golden can say: the bar is drawn and
      // focused, the view SCROLLED to a match that was off screen, and the status line
      // carries the count. What a golden cannot say is the HIGHLIGHT (these frames are
      // plain text) — that is asserted on cell roles in transcript_test, the same split
      // milestone 2 made for the syntax highlighter.
      {"demo.80x24.find", "--frame 80x24 --theme default-dark --keys \"CtrlF Type:wrap\""},
      {"demo.80x24.find-next", "--frame 80x24 --theme default-dark --keys \"CtrlF Type:wrap Enter Enter\""},
      {"demo.100x24.find-none", "--frame 100x24 --theme default-dark --keys \"CtrlF Type:zzzznope\""},
      {"demo.80x24.find-closed", "--frame 80x24 --theme default-dark --keys \"CtrlF Type:wrap CtrlF\""},
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
      // A third fixture (long_diff.md): a long ```diff block folded to
      // one summary row, opened by a click on that row (still CAPPED at 10 of its 13
      // lines, with the "▼ N more" marker INSIDE the box), and uncapped by a click on
      // the marker row. --code-fold sets the two thresholds so the fixture can stay
      // small; the shipped ones are the studio's own 30,100.
      {"diff.80x24.folded", "--frame 80x24 --theme default-dark --code-fold 6,10 --keys \"Home\"", "long_diff.md"},
      {"diff.100x30.open", "--frame 100x30 --theme default-dark --code-fold 6,10 --keys \"Home Click 5,8\"", "long_diff.md"},
      {"diff.100x30.uncapped", "--frame 100x30 --theme default-dark --code-fold 6,10 --keys \"Home Click 5,8 Click 30,20\"", "long_diff.md"},
      {"diff.120x40.whole", "--frame 120x40 --theme default-dark --code-fold 6,10", "long_diff.md"},
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
      {"menu.80x24.theme", "--frame 80x24 --theme default-dark --keys \"F2 Type:Appearance Enter Type:theme Enter\""},
      {"menu.80x24.choose-light", "--frame 80x24 --theme default-dark --keys \"F2 Type:Appearance Enter Type:theme Enter Type:default-light Enter\""},
      {"menu.80x24.filter", "--frame 80x24 --theme default-dark --keys \"F2 Type:Appearance Enter Type:lay\""},
      {"menu.80x24.left", "--frame 80x24 --theme default-dark --keys \"F2 Type:Appearance Enter Left\""},
      {"menu.80x24.escape", "--frame 80x24 --theme default-dark --keys \"F2 Escape\""},
      {"menu.80x24.toggle", "--frame 80x24 --theme default-dark --keys \"F2 Type:Appearance Enter Type:ambig Enter\""},
      // The Commands level, whose shortcuts are the LIVE chords of the actions its
      // items name — nothing in the menu file spells a key out.
      {"menu.80x24.commands", "--frame 80x24 --theme default-dark --keys \"F2 Type:comm Enter\""},
      {"menu.80x24.editors", "--frame 80x24 --theme default-dark --keys \"F2 Type:Editors Enter\""},
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
      // milestone 18: a typed field — Chaos is a float 0..1 with two digits: "1." is
      // accepted, the 5 and the x are refused (the breadcrumb shows the hint and the
      // reason, the row shows the label, the text and the caret); Up steps from 0 by 0.1
      {"editor.120x40.chaos-typing", "--frame 120x40 --theme default-dark --keys \"F4 Type:generate Enter Down Down Enter Type:1.5x\""},
      {"editor.120x40.chaos-step", "--frame 120x40 --theme default-dark --keys \"F4 Type:generate Enter Down Down Enter Up Up\""},
      // milestone 15: the Check popup and the Fixes level
      {"editor.120x40.check", "--frame 120x40 --theme default-dark --keys \"F4 Type:check Enter\""},
      {"editor.120x40.fixes", "--frame 120x40 --theme default-dark --keys \"F4 Type:fixes Enter\""},
      // milestone 16 (the layout editor); the scratch --presets is appended
      {"layout-editor.120x40.open", "--frame 120x40 --theme default-dark --keys \"F6\""},
      {"layout-editor.120x40.split", "--frame 120x40 --theme default-dark --keys \"F6 Type:tree Enter Type:split_into_a_row Enter\""},
      {"layout-editor.120x40.undo", "--frame 120x40 --theme default-dark --keys \"F6 Type:tree Enter Type:split_into_a_row Enter CtrlZ\""},
      {"layout-editor.120x40.border-preview", "--frame 120x40 --theme default-dark --keys \"F6 Type:border Enter Down\""},
      {"layout-editor.120x40.border-cancel", "--frame 120x40 --theme default-dark --keys \"F6 Type:border Enter Down Escape\""},
      {"layout-editor.120x40.drag", "--frame 120x40 --theme default-dark --keys \"F6 Type:tree Enter Type:split_into_a_row Enter Click 44,10 Drag 30,10 Release\""},
      {"layout-editor.120x40.click", "--frame 120x40 --theme default-dark --keys \"F6 Click 30,37\""},
      {"layout-editor.120x40.save", "--frame 120x40 --theme default-dark --keys \"F6 Type:tree Enter Type:split_into_a_row Enter Type:title Enter CtrlU Type:chat Enter Escape Type:layout_file Enter Type:save Enter Type:two Enter\""},
      {"tiny.9x4.layout-editor", "--frame 9x4 --theme default-dark --keys \"F6 Tab Type:tree Enter Type:split Enter Type:x\""},
      // the seam rule: the FIXED side takes the new size — the status before the seam
      // (panel-left) and the status after it (a fixture with a 55-wide right panel), both
      // seams left of the editor's popup
      // The design editor's `add-widget`, end to end: a window that did not exist, holding a
      // widget of a kind this layout never had, through the kind picker alone — and DRAWN in
      // the same frame.
      // The trailing Escape is needed because the widget kind is an INPUT, not a choice, and
      // an input's commit leaves the menu's typed filter standing (a choice clears it on the way
      // back up). Without it this golden would show the field being edited rather than the window
      // it produced, which is the thing the case exists to show.
      {"layout-editor.120x40.add-widget", "--frame 120x40 --theme default-dark --keys \"F6 Type:tree Enter Type:split_into_a_row Enter Tab Type:widget_kind Enter Type:help Enter Escape\""},
      {"layout-editor.120x40.actions", "--frame 120x40 --theme default-dark --keys \"F6 Type:this_screen Enter Type:actions Enter End Enter Type:app.zoom Enter Escape Escape Type:layout_file Enter Type:save Enter Type:three Enter Escape Escape Type:this_screen Enter Type:actions Enter\""},
      // CREATING a layout, not inheriting one. Started from the shipped
      // `default` — five actions, four popups, min 60x8 — so what the skeleton does NOT
      // carry is visible in the same frame that shows what it does.
      // The SAVE is deliberately not in this case: the written file is asserted below in
      // its own run, and a golden that names a scratch path is a golden about the machine.
      {"layout-editor.120x40.new", "--frame 120x40 --theme default-dark --keys \"F6 Type:layout_file Enter Type:new_layout Enter Type:kiosk Enter\""},
      {"layout-editor.120x40.drag-fixed-before", "--frame 120x40 --theme default-dark --layout panel-left --keys \"F6 Click 31,10 Drag 20,10 Release\""},
      {"layout-editor.120x40.drag-fixed-after", "--frame 120x40 --theme default-dark --layout '" ROLLTUI_FIXTURE_DIR "/layouts/wide-right.json' --keys \"F6 Click 65,10 Drag 50,10 Release\""},
      // milestone 17 (bindings as data): a vim-ish file, the help rendered from it, the
      // input obeying it, and the keys editor
      {"keys.120x40.help-default", "--frame 120x40 --theme default-dark --keys \"F1 PageDown\""},
      {"keys.120x40.help-vim", "--frame 120x40 --theme default-dark --bindings '" ROLLTUI_FIXTURE_DIR "/bindings/vim-ish.json' --keys \"F1 PageDown\""},
      {"keys.80x24.input-default", "--frame 80x24 --theme default-dark --keys \"Type:hello_world AltB Type:X\""},
      {"keys.80x24.input-vim", "--frame 80x24 --theme default-dark --bindings '" ROLLTUI_FIXTURE_DIR "/bindings/vim-ish.json' --keys \"Type:hello_world AltB Type:X\""},
      {"keys-editor.120x40.open", "--frame 120x40 --theme default-dark --keys \"F7\""},
      {"keys-editor.120x40.capture", "--frame 120x40 --theme default-dark --keys \"F7 Enter Enter Down Down Down Down Down Down Down Down Down Down Enter Enter\""},
      {"keys-editor.120x40.bound", "--frame 120x40 --theme default-dark --keys \"F7 Enter Enter Down Down Down Down Down Down Down Down Down Down Enter Enter AltB\""},
      {"keys-editor.120x40.moved", "--frame 120x40 --theme default-dark --keys \"F7 Enter Enter Down Down Down Down Down Down Down Down Down Down Enter Enter AltD\""},
      {"tiny.7x3.keys-editor", "--frame 7x3 --theme default-dark --keys \"F7 Enter Enter Enter Enter AltB Type:x\""},
      // Effects on a fourth fixture, whose entries carry STATES and nothing
      // else — no glyph, no colour, no period anywhere in effects.md. Three ticks of one
      // screen, and `--dump-tick` recording what each frame ASKS FOR, in the golden bytes.
      {"fx.80x24.tick0", "--frame 80x24 --theme default-dark --tick 0 --dump-tick --keys \"Home\"", "effects.md"},
      {"fx.80x24.tick240", "--frame 80x24 --theme default-dark --tick 240 --dump-tick --keys \"Home\"", "effects.md"},
      {"fx.80x24.tick640", "--frame 80x24 --theme default-dark --tick 640 --dump-tick --keys \"Home\"", "effects.md"},
      // The SAME document under three other themes: mono's ASCII spinner, a theme that
      // STACKS two kinds per state (a glyph from one, a colour from another, plus a
      // trailing ellipsis), and a theme with no "effects" key at all — the degrade rung,
      // which must be the still text the fixture wrote and a tick of "none".
      {"fx.80x24.mono", "--frame 80x24 --theme mono --tick 200 --dump-tick --keys \"Home\"", "effects.md"},
      {"fx.80x24.loud", "--frame 80x24 --theme '" ROLLTUI_FIXTURE_DIR "/themes/loud.json' --tick 100 --dump-tick --keys \"Home\"", "effects.md"},
      {"fx.80x24.still", "--frame 80x24 --theme '" ROLLTUI_FIXTURE_DIR "/themes/still.json' --tick 240 --dump-tick --keys \"Home\"", "effects.md"},
      // NOTHING MARKED: the same theme that spins above asks for no wakeup at all on a
      // document with no states in it.
      {"fx.80x24.unmarked", "--frame 80x24 --theme default-dark --tick 240 --dump-tick --keys \"Home\""},
  };
  std::string bottom, top, popup, popup_closed, popup_big;
  std::string tools_top, unfold_click, unfold_ctrl_o, drag_copy, dbl_copy, triple_copy, autoscroll_out;
  std::string typed, multiline, wrapped, stacked_ml, select_all_copy, in_drag_copy, in_dbl_copy, submitted, history, edited, pasted, capped;
  std::string menu_commands;
  std::string menu_editors;
  std::string menu_open, menu_theme, menu_light, menu_filter, menu_left, menu_escape, menu_toggle, menu_palette, menu_palette_choose, menu_big;
  std::string ed_open, ed_fg, ed_cancel, ed_commit, ed_undo, ed_confirm, ed_save, ed_check, ed_fixes;
  std::string le_open, le_split, le_undo, le_preview, le_cancel, le_drag, le_click, le_save, le_fixed_before, le_fixed_after;
  std::string le_widget, le_actions, le_new;
  std::string kh_default, kh_vim, ki_default, ki_vim, ke_open, ke_capture, ke_bound, ke_moved;
  std::string find_open, find_next, find_none, find_closed;
  std::string fx0, fx240, fx640, fx_mono, fx_loud, fx_still, fx_unmarked;
  bool tiny_failed = false;
  for (const Case& c : cases) {
    int rc = 0;
    const std::string fixture = std::string(ROLLTUI_FIXTURE_DIR) + "/session/" + c.fixture;
    std::string cmd = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + fixture + "' " + c.args;
    // THE LAYOUT AXIS IS PINNED THE WAY THE THEME AXIS IS. Every case names `--theme`; none
    // named `--layout`, so each case's layout was whatever the SHARED scratch working copy held
    // — stable only while no case writes one. The layout editor's save-as goes through the
    // Layout store and records its origin (a manual save writes even under --frame, the rule
    // the theme save-as already follows and this file asserts), so one editor case can leave
    // every later frame inheriting its layout through the shared directory. A case that names
    // its layout keeps it.
    if (std::strstr(c.args, "--layout") == nullptr) cmd += " --layout default";
    // THE SCRATCH PRESET DIRECTORY GOES TO EVERY CASE, not just the editor ones.
    // Giving it only to the editor and bindings cases leaves every other golden running
    // against WHATEVER PRESET DIRECTORY THE DEVELOPER HAPPENED TO HAVE, passing only while
    // that working copy equals the shipped default. Change a shipped layout and the status
    // line reads "default (modified)" on that machine and nowhere else: a golden whose value
    // depends on the machine running it.
    cmd += presets;
    std::string out = run(cmd, rc);
    check(rc == 0 && !out.empty(), std::string(c.name) + ": studio ran (rc " + std::to_string(rc) + ", " +
                                       std::to_string(out.size()) + " bytes)");
    // Every row fits the width, in cells.
    int w = std::atoi(std::strstr(c.args, "--frame ") + 8);
    int rows = 0;
    bool fits = true;
    std::istringstream in(frame_part(out));
    std::string row;
    while (std::getline(in, row)) {
      ++rows;
      int cw = unicode::display_width(row, std::strstr(c.args, "--ambiguous-wide") != nullptr);
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
    if (std::string(c.name) == "menu.80x24.editors") menu_editors = out;
    if (std::string(c.name) == "menu.80x24.theme") menu_theme = out;
    if (std::string(c.name) == "menu.80x24.choose-light") menu_light = out;
    if (std::string(c.name) == "menu.80x24.filter") menu_filter = out;
    if (std::string(c.name) == "menu.80x24.left") menu_left = out;
    if (std::string(c.name) == "menu.80x24.escape") menu_escape = out;
    if (std::string(c.name) == "menu.80x24.toggle") menu_toggle = out;
    if (std::string(c.name) == "menu.80x24.commands") menu_commands = out;
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
    if (std::string(c.name) == "layout-editor.120x40.add-widget") le_widget = out;
    if (std::string(c.name) == "layout-editor.120x40.actions") le_actions = out;
    if (std::string(c.name) == "layout-editor.120x40.new") le_new = out;
    if (std::string(c.name) == "layout-editor.120x40.drag-fixed-before") le_fixed_before = out;
    if (std::string(c.name) == "layout-editor.120x40.drag-fixed-after") le_fixed_after = out;
    if (std::string(c.name) == "demo.80x24.find") find_open = out;
    if (std::string(c.name) == "demo.80x24.find-next") find_next = out;
    if (std::string(c.name) == "demo.100x24.find-none") find_none = out;
    if (std::string(c.name) == "demo.80x24.find-closed") find_closed = out;
    if (std::string(c.name) == "keys.120x40.help-default") kh_default = out;
    if (std::string(c.name) == "keys.120x40.help-vim") kh_vim = out;
    if (std::string(c.name) == "keys.80x24.input-default") ki_default = out;
    if (std::string(c.name) == "keys.80x24.input-vim") ki_vim = out;
    if (std::string(c.name) == "keys-editor.120x40.open") ke_open = out;
    if (std::string(c.name) == "keys-editor.120x40.capture") ke_capture = out;
    if (std::string(c.name) == "keys-editor.120x40.bound") ke_bound = out;
    if (std::string(c.name) == "keys-editor.120x40.moved") ke_moved = out;
    if (std::string(c.name) == "fx.80x24.tick0") fx0 = out;
    if (std::string(c.name) == "fx.80x24.tick240") fx240 = out;
    if (std::string(c.name) == "fx.80x24.tick640") fx640 = out;
    if (std::string(c.name) == "fx.80x24.mono") fx_mono = out;
    if (std::string(c.name) == "fx.80x24.loud") fx_loud = out;
    if (std::string(c.name) == "fx.80x24.still") fx_still = out;
    if (std::string(c.name) == "fx.80x24.unmarked") fx_unmarked = out;
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
    // at 80x24. The layout area is the screen minus the studio's one-line status
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
    // that name the focus (the status panel's row and the studio's status line).
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
    // cells" — for the tiny frames that is the whole point (
    // views can shrink to 1 or even 0 in either dimension; it must be graceful).
    check(!tiny_failed, "the 1x1, 2x2, 6x1, 1x6, 20x3 and 80x2 frames render (typed text, a paste, the help popup and Tab included) without a row out of bounds");
    // ---- milestone 11: the menu, asserted beyond the bytes ----
    check(menu_open.find("\xE2\x95\xAD menu ") != std::string::npos && menu_open.find("focus:menu") != std::string::npos &&
              menu_open.find("Editors") != std::string::npos && menu_open.find("Appearance") != std::string::npos &&
              menu_open.find("Commands") != std::string::npos,
          "F2 opens the menu popup, and its top level names the app's own tools rather than burying them under a category");
    check(menu_theme.find("Appearance \xE2\x80\xBA Theme") != std::string::npos && menu_theme.find("\xE2\x80\xA2 default-dark") != std::string::npos,
          "Enter descends into the Theme choice: the breadcrumb grows and the current option is marked •");
    check(menu_light.find("theme F4   default-light") != std::string::npos && menu_light.find("default-light \xE2\x96\xB8") != std::string::npos &&
              menu_light.find("Appearance \xE2\x80\xBA Theme") == std::string::npos,
          "choosing default-light BY NAME four levels deep by keyboard alone: the status says so, the choice shows its value, the menu is back at the top");
    check(!menu_light.empty() && menu_light != menu_open, "…and the frame changed (it went light)");

    // ---- THE PANEL TEACHES AND THE CHORD IS THE BINDINGS' TO SAY --------------------------
    // The status panel names the three editable things and never disappears; the keys used to
    // live only in the placeholder document, which a user's first real action deletes and the
    // menu popup covers. So the label carries the chord — but it must carry the LIVE one, or it
    // is a third place a key is written down and the first to go stale.
    {
      int rc2 = 0;
      const std::string dir = scratch + "/pk";
      std::filesystem::create_directories(dir + "/bindings");
      const std::string bin = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
                              "/session/demo.md' --frame 100x14 --presets '" + dir + "'";
      const std::string plain = run(bin + " 2>/dev/null", rc2);
      check(plain.find("theme F4") != std::string::npos && plain.find("layout F6") != std::string::npos &&
                plain.find("keys F7") != std::string::npos,
            "the status panel's labels carry the editors' chords, so the keys survive the document being replaced");

      // REBIND, and the label must follow. A hardcoded "F4" passes the check above and fails this.
      std::ofstream(dir + "/bindings/moved.json")
          << "{\n  \"name\": \"moved\",\n  \"bindings\": {\n    \"editor.theme\": [\"f9\"]\n  }\n}\n";
      const std::string moved = run(bin + " --bindings moved 2>/dev/null", rc2);
      check(moved.find("theme F9") != std::string::npos && moved.find("theme F4") == std::string::npos,
            "…and rebinding the theme editor to F9 moves the label with it");
    }
    check(menu_filter.find("/lay") != std::string::npos && menu_filter.find("Layout") != std::string::npos && menu_filter.find("Colour depth") == std::string::npos,
          "typing \"lay\" filters the level to Layout and shows the filter after the breadcrumb");
    check(!menu_left.empty() && menu_left == menu_open, "Enter then Left gives back exactly the opened frame");
    check(!menu_escape.empty() && menu_escape == bottom, "F2 then Escape gives back exactly the frame without the menu");
    check(menu_toggle.find("[x] Ambiguous width") != std::string::npos, "Enter on the toggle shows [x]");
    // The shipped menu file names ACTIONS, never keys — so these columns are the
    // live table's, and "F1, ?" (two chords) is what app.help actually has.
    // The shortcut column is the LIVE table's either way; what changed is that the four editors
    // are a level of their own rather than filed under a category word.
    check(menu_editors.find("Theme editor") != std::string::npos && menu_editors.find("F4") != std::string::npos &&
              menu_editors.find("Layout editor") != std::string::npos && menu_editors.find("Keys editor") != std::string::npos,
          "the Editors level lists the four editors with their LIVE chords");
    check(menu_commands.find("Help") != std::string::npos && menu_commands.find("F1, ?") != std::string::npos &&
              menu_commands.find("Ctrl-Q") != std::string::npos,
          "the Commands level shows each item's LIVE chords, from the action it names");
    check(menu_palette.find("Theme \xE2\x80\xBA mono") != std::string::npos && menu_palette.find("Layout") == std::string::npos,
          "Ctrl-P opens the palette: rows are paths, and \"mono\" filters to the one theme option");
    check(menu_palette_choose.find("stacked") != std::string::npos && menu_palette_choose.find("\xE2\x95\xAD menu ") != std::string::npos &&
              menu_palette_choose.find("\xE2\x94\x8C transcript") == std::string::npos,
          "Enter on a palette row chooses the stacked layout (borderless transcript) with the menu still open");
    // ---- milestone 14: the theme editor, asserted beyond the bytes ----
    check(ed_open.find("\xE2\x94\x8C theme editor ") != std::string::npos && ed_open.find("focus:editor") != std::string::npos && ed_open.find("Roles") != std::string::npos,
          "F4 opens the theme editor popup with focus and the Roles level");
    check(ed_fg.find("Roles \xE2\x80\xBA md_heading \xE2\x80\xBA fg") != std::string::npos && role_part(ed_fg) == "md_heading fg=#adeeae bg=#14161a bold" &&
              ed_fg.find("previewing") != std::string::npos,
          "Roles › md_heading › fg two entries down previews #adeeae on the heading and says previewing [" + role_part(ed_fg) + "]");
    check(role_part(ed_cancel) == "md_heading fg=#84b7f9 bg=#14161a bold" && ed_cancel.find("focus:editor") != std::string::npos,
          "Escape puts the committed #84b7f9 back and stays in the editor [" + role_part(ed_cancel) + "]");
    check(role_part(ed_commit) == "md_heading fg=#adeeae bg=#14161a bold" && ed_commit.find("undo 1") != std::string::npos,
          "Enter commits: the heading is #adeeae, undo depth 1 [" + role_part(ed_commit) + "]");
    check(role_part(ed_undo) == "md_heading fg=#84b7f9 bg=#14161a bold" && ed_undo.find("undo 0") != std::string::npos && ed_undo.find("redo 1") != std::string::npos,
          "Ctrl-Z undoes it: #84b7f9, undo 0, redo 1");
    check(ed_confirm.find("\xE2\x95\xAD confirm ") != std::string::npos && ed_confirm.find("built-in default? (y/n)") != std::string::npos,
          "Reset to the built-in default asks in a confirm popup, never applies bare");
    check(ed_save.find("saved preset 'mine'") != std::string::npos && std::filesystem::exists(scratch + "/p/themes/mine.json"),
          "save-as writes themes/mine.json under the scratch presets directory (a manual save writes even under --frame)");
    {
      int rc = 0;
      const std::string relaunch = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md' --frame 80x24 --presets '" +
                                   scratch + "/p2' --theme '" + scratch + "/p/themes/mine.json' --dump-role md_heading";
      const std::string again = run(relaunch, rc);
      check(rc == 0 && role_part(again) == "md_heading fg=#adeeae bg=#14161a bold", "a relaunch with --theme <that file> shows the saved heading colour [" + role_part(again) + "]");
      // Under --frame nothing autosaves from an edit; the explicit save-as records its
      // new origin in the working copy, which is the one write the frame runs made.
      bool ok = false;
      std::string wc = read_file(scratch + "/p/theme.working.json", ok);
      check(ok && wc.find("\"preset\": \"mine\"") != std::string::npos, "the working copy written by the save-as records preset 'mine' — and nothing wrote it before that (the earlier frames' edits did not persist)");
    }
    // ---- milestone 17: bindings as data, asserted beyond the bytes ----
    // ---- find (milestone 12.4) — what the golden alone does not say -----------------
    // The bar is a POPUP with an `input:` window, not a mode: the studio pushes a layout
    // popup and pipes its text in, which is why "find" shows up as the focused window id
    // like any other and nothing here knows about a find mode.
    check(find_open.find("\xE2\x95\xAD find ") != std::string::npos && find_open.find("focus:find") != std::string::npos,
          "Ctrl-F opens the find popup and focuses its input");
    check(find_open.find("find 2/4") != std::string::npos,
          "…and the match count and position are VISIBLE, rendered by the host from the widget's numbers");
    check(find_open.find("> wrap") != std::string::npos, "…the query is the bar's own text, typed into an ordinary input");
    // The view MOVED to a match that was not on screen — the milestone's "scrolled to".
    check(!bottom.empty() && find_open != bottom && find_open.find("line 149/191") != std::string::npos,
          "…and the transcript scrolled to the match (line 149, where the unsearched view sits at the bottom)");
    check(find_next.find("find 4/4") != std::string::npos,
          "Enter in the find bar is next-match, twice: 2/4 → 4/4 (its Submit, no routing rule of its own)");
    // Enter must NOT clear the bar: a find bar's text is a standing query, not a message
    // (Widgets.hpp's OnSubmit). With one shared submit rule every input gets the prompt's
    // send-and-clear, so
    // the bar erased its own query on its own next-match key.
    check(find_next.find("> wrap") != std::string::npos,
          "…and the query SURVIVES its own Enter — the text is a standing query, not a message");
    // 100 cells wide because at 80 the count falls off the end of the status line, and a
    // golden that proves a number by cropping it proves nothing.
    check(row(find_none, 23).find("find 0/0") != std::string::npos && find_none.find("focus:find") != std::string::npos,
          "a query with no matches says 0/0 and stays put rather than reporting nothing at all [" + row(find_none, 23) + "]");
    check(find_closed.find("\xE2\x95\xAD find ") == std::string::npos && row(find_closed, 23).find("find ") == std::string::npos,
          "closing the bar clears the query, so highlights never outlive the bar invisibly");
    // ---- effects — what the goldens alone do not say --------------------------------
    // effects.md contains no glyph, no colour and no period: its entries say `waiting`,
    // `streaming`, `progress`, `flash` and stop. Everything below is the THEME's answer
    // to those five words, recorded at a fixed tick.
    auto note_row = [&](const std::string& out) { return row(out, 3); };  // the waiting entry's row (0-based)
    check(note_row(fx0).find("\xE2\xA0\x8B waiting") != std::string::npos, "at tick 0 the waiting span shows the theme's first braille frame [" + note_row(fx0) + "]");
    check(note_row(fx240).find("\xE2\xA0\xB8 waiting") != std::string::npos, "…at tick 240 a different one: --tick N records an effect deterministically [" + note_row(fx240) + "]");
    check(frame_part(fx0) != frame_part(fx240), "…so two ticks of one screen are two different frames");
    check(frame_part(fx0) == frame_part(fx640), "…and one full period later, the same frame again (640 ms, eight frames)");
    check(note_row(fx_mono).find("- waiting") != std::string::npos,
          "the SAME document under mono is an ASCII spinner — one app, one widget, a different look [" + note_row(fx_mono) + "]");
    check(note_row(fx_loud).find("^ waiting") != std::string::npos,
          "…and under a theme that STACKS two kinds, that theme's own frames [" + note_row(fx_loud) + "]");
    // The stack's other half: the ellipsis eats the last three cells of the streaming
    // row while the shimmer colours it — a glyph from one kind, a colour from another.
    check(row(fx_loud, 5).find("streaming a reply now .") != std::string::npos && row(fx_loud, 5).find("now ...") == std::string::npos,
          "…whose SECOND kind is drawing at the same time, at the other end of another span [" + row(fx_loud, 5) + "]");
    // The degrade rung, as a file: the same colours with no "effects" key at all.
    check(note_row(fx_still).find("\xC2\xB7 waiting") != std::string::npos,
          "a theme that maps nothing leaves the STILL text the document wrote [" + note_row(fx_still) + "]");
    check(tick_part(fx_still) == "none", "…and asks for no wakeup: a still UI costs nothing");
    check(tick_part(fx_unmarked) == "none",
          "NO WAKEUPS WITH NO MARKS: the theme that spins above asks for nothing on a document with no states in it");
    check(tick_part(fx0) == "38" && tick_part(fx_mono) == "100" && tick_part(fx_loud) == "26",
          "…and where something IS marked the wakeup is the theme's own, the shortest of what the frame carries [" +
              tick_part(fx0) + " / " + tick_part(fx_mono) + " / " + tick_part(fx_loud) + "]");
    // The ordinary entry is the control INSIDE the fixture: five states above it, and it
    // is byte-identical at every tick and under every theme.
    check(row(fx0, 12) == row(fx240, 12) && row(fx0, 12) == row(fx_still, 12) && row(fx0, 12).find("An ordinary entry") != std::string::npos,
          "an unmarked entry in a marked document never moves, under any theme or tick [" + row(fx0, 12) + "]");

    check(kh_default.find("Ctrl-Left, Alt-Left") != std::string::npos && kh_default.find("move one word left") != std::string::npos,
          "the default help popup (scrolled a page) is rendered from the table: word motions on Ctrl/Alt-arrows");
    check(kh_vim.find("Alt-B") != std::string::npos && kh_vim.find("Alt-F") != std::string::npos, "with vim-ish.json the help popup shows Alt-B / Alt-F: it is rendered from the LIVE table");
    check(row(ki_vim, 21).rfind("\xE2\x94\x82 > hello Xworld", 0) == 0, "with vim-ish.json Alt-B moves a word back, so the X lands before 'world' [" + row(ki_vim, 21) + "]");
    check(row(ki_default, 21).rfind("\xE2\x94\x82 > hello worldX", 0) == 0, "with the default table Alt-B is unbound and the X lands at the end [" + row(ki_default, 21) + "]");
    {
      std::istringstream a(ki_default), b(ki_vim);
      std::string ra, rb;
      int differing = 0, line = 0;
      while (std::getline(a, ra) && std::getline(b, rb)) {
        ++line;
        if (ra != rb && line != 22 && ra.find("keys") == std::string::npos) ++differing;
      }
      check(differing == 0, "…and every other row is identical: a bindings change never touches the look (" + std::to_string(differing) + " rows differ)");
    }
    check(ke_open.find("\xE2\x94\x8C keys editor ") != std::string::npos && ke_open.find("Actions by scope") != std::string::npos && ke_open.find("preset: default") != std::string::npos,
          "F7 opens the keys editor on the shipped default");
    check(ke_capture.find("press the chord for input.word_left") != std::string::npos, "'add a chord' on word_left captures: the status asks for the chord");
    check(ke_bound.find("bound Alt-B \xE2\x86\x92 word_left") != std::string::npos && ke_bound.find("remove Alt-B") != std::string::npos && ke_bound.find("preset: default (modified)") != std::string::npos,
          "Alt-B becomes the chord: the status, a 'remove Alt-B' item and the preset label ('default (modified)') all say so");
    check(ke_moved.find("(was kill_word_forward") != std::string::npos, "Alt-D, bound to kill_word_forward, moves and the status names the loser");
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
    check(le_fixed_before.find("selected: status") != std::string::npos && le_fixed_before.find("size 21") != std::string::npos,
          "panel-left: the seam's fixed side (the 32-wide status, before the seam) is dragged to 21 cells");
    check(le_fixed_after.find("selected: status") != std::string::npos && le_fixed_after.find("size 70") != std::string::npos,
          "wide-right: the fixed status AFTER a fill seam is the side that resizes (to 70), never the fill — no gap opens");
    check(le_save.find("saved layout file") != std::string::npos && std::filesystem::exists(scratch + "/p/layouts/two.json") && le_save.find("\xE2\x94\x8C chat ") != std::string::npos,
          "title 'chat' and save-as 'two' write layouts/two.json under the scratch presets directory");
    {
      int rc = 0;
      const std::string relaunch = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md' --frame 120x40 --presets '" +
                                   scratch + "/p4' --layout '" + scratch + "/p/layouts/two.json'";
      const std::string again = run(relaunch, rc);
      check(rc == 0 && again.find("\xE2\x94\x8C chat ") != std::string::npos && again.find("\xE2\x94\xAC transcript-2 ") != std::string::npos && again.find("[layout editor]") == std::string::npos,
            "a relaunch with --layout <that file> shows the two panes ('chat' and 'transcript-2') with no editor open (Done-when of m16)");
    }
    // ---- the design editor ----
    // The Done-when, end to end through the real binary: a window that did not exist,
    // holding a widget of a kind this layout never had, DRAWN in the same frame — and
    // the kind picker is the only thing that put it there. "Ctrl-W, Alt-Backspace" is
    // a chord pair only a `help` widget renders (from the LIVE bindings), so finding it
    // in the second pane is the widget itself, not a title the editor wrote — and it
    // survives the pane being narrow enough to wrap the descriptions.
    // The seam is `┬` and not `┌`: the layout editor's selection outline used to
    // redraw the selected window's border UNJOINED and win, which broke the join it sits
    // on. This assertion was pinned to that broken glyph.
    // "Widget kind: help" rather than "help ▸" now — the field is an input the
    // author types into, not a closed list they step through.
    check(le_widget.find("\xE2\x94\xAC transcript-2 ") != std::string::npos && le_widget.find("Ctrl-W, Alt-Backspace") != std::string::npos &&
              le_widget.find("Widget kind: help") != std::string::npos && le_widget.find("Source:  ") != std::string::npos,
          "a `help` widget typed into the kind field alone draws in the next frame, with the Source field emptied and disabled");
    check(le_actions.find("app.zoom") != std::string::npos && le_actions.find("add an action (name)") != std::string::npos,
          "the Actions level lists the shipped layout's declarations and the one just added");
    {
      bool ok = false;
      const std::string saved = read_file(scratch + "/p/layouts/three.json", ok);
      check(ok && saved.find("\"app.zoom\"") != std::string::npos && saved.find("\"app.help\"") != std::string::npos,
            "…and saving writes it into the layout file's \"actions\", beside the ones it was loaded with");
    }
    // ---- creating a layout, not inheriting one ----
    // The Done-when is asserted against the WRITTEN FILE, not the frame, because the file
    // is the artifact the target app reads — and because the measurement that scoped this
    // milestone was about a file: stripping `no-panel` to one window and saving it as
    // `myapp` produced roll's five `app.*` actions, four popups pointing at roll's own
    // composites, and `no-panel`'s min sizes, none of them the author's.
    check(le_new.find("new layout 'kiosk'") != std::string::npos && le_new.find("one window, no popups") != std::string::npos &&
              le_new.find("layout editor \xE2\x80\xA2 main") != std::string::npos && le_new.find("\xE2\x94\x8C transcript") == std::string::npos,
          "New layout replaces the whole screen with the skeleton — the transcript window of the layout it was created from is gone");
    {
      // The same thing again with a save, in its own preset directory: the file is the
      // assertion, and keeping it out of the golden keeps a scratch path out of the frame.
      std::filesystem::create_directories(scratch + "/p6");
      int rc = 0;
      run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
              "/session/demo.md' --frame 100x28 --theme default-dark --presets '" + scratch +
              "/p6' --keys \"F6 Type:layout_file Enter Type:new_layout Enter Type:kiosk Enter Type:layout_file Enter Type:save Enter Type:kiosk Enter\" 2>/dev/null",
          rc);
      bool ok = false;
      const std::string saved = read_file(scratch + "/p6/layouts/kiosk.json", ok);
      check(rc == 0 && ok, "the save-as wrote layouts/kiosk.json");
      check(ok && saved.find("\"actions\": {}") != std::string::npos,
            "the file declares NO actions — explicitly, since an ABSENT \"actions\" key would make the loader fill in the shipped default's five");
      check(ok && saved.find("\"popups\"") == std::string::npos && saved.find("\"min_width\"") == std::string::npos &&
                saved.find("\"min_height\"") == std::string::npos,
            "…no popups, and no size threshold: the layout it was created FROM had four popups and min 60x8");
      check(ok && saved.find("\"app.help\"") == std::string::npos && saved.find("approval") == std::string::npos &&
                saved.find("transcript") == std::string::npos,
            "…and not one name from the screen that was open — it is a skeleton, not a stripped copy");
      check(ok && saved.find("\"content\": \"text:\"") != std::string::npos && saved.find("\"focus\": \"main\"") != std::string::npos,
            "what it DOES have is one window naming nothing a host must have bound, and the focus on it");
      // It is a layout, not just a file: the loader takes it back clean.
      const LoadedLayoutCheck back = load_layout_check(saved);
      check(back.ok && back.clean && back.no_notes && back.actions_empty && back.popups_empty,
            "…and it loads clean with nothing filled in — a fill-in would have shown up here as five actions [" + back.error + "]");
    }
    {
      // A NEW LAYOUT INHERITS NOTHING, thresholds included. The size a screen needs is a fact
      // about the SCREEN, not about whatever app or layout was open when it was created, so
      // the author types it and this case checks that what was typed is what reaches the file.
      std::filesystem::create_directories(scratch + "/p7");
      int rc = 0;
      run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
              "/session/demo.md' --frame 100x28 --theme default-dark --presets '" + scratch + "/p7'" +
              " --keys \"F6 Type:layout_file Enter Type:new_layout Enter Type:kiosk Enter Type:this_screen Enter Type:minimum_width Enter Type:40 Enter Escape"
              " Type:minimum_height Enter Type:12 Enter Escape Escape Type:layout_file Enter Type:save Enter Type:kiosk Enter\" 2>/dev/null",
          rc);
      bool ok = false;
      const std::string saved = read_file(scratch + "/p7/layouts/kiosk.json", ok);
      check(rc == 0 && ok && saved.find("\"min_width\": 40") != std::string::npos && saved.find("\"min_height\": 12") != std::string::npos,
            "a new layout takes the thresholds its author typed, and takes them from nowhere else");
      check(ok && saved.find("\"actions\": {}") != std::string::npos && saved.find("\"popups\"") == std::string::npos,
            "…and nothing else came with them");
    }
    // ---- the FENCE decides, and a golden frame cannot show that --------------------
    // The frames above prove the FOLD (a summary row, a capped body, a marker). They
    // cannot prove the COLOURING, because a golden is text. --frame-sgr can: the same
    // shape of text under a ```diff fence and under a bare one, in one document, and
    // the roles that reach the cells have to differ. This is the milestone's control.
    {
      int rc = 0;
      const std::string sgr =
          run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
                  "/session/long_diff.md' --frame-sgr 120x40 --theme default-dark --code-fold 6,10 --presets '" +
                  scratch + "/p8' 2>/dev/null",
              rc);
      // default-dark's diff_added is #adeeae and diff_removed is #c06a64, and the code ground
      // is #32363c — all three from presets/themes/default.json, where the design lives.
      const std::string added = "\x1b[0;38;2;173;238;174";
      const std::string removed = "\x1b[0;38;2;192;106;100";
      const std::string plain_code = "\x1b[0;38;2;216;220;226";  // md_code_block
      const std::size_t coloured = sgr.find(removed + ";48;2;20;22;26m-const int cap = ");
      check(rc == 0 && coloured != std::string::npos, "a ```diff fence colours its '-' line through diff_removed");
      check(sgr.find(added + ";48;2;20;22;26m+const int cap = ") != std::string::npos,
            "…and its '+' line through diff_added");
      // THE CONTROL. The bare fence's lines are the same shape — "-milk", "+oat milk",
      // "@@ -1,3 +1,3 @@" — and every one of them is md_code_block, like any other code.
      for (const char* line : {"-milk", "+oat milk", " bread", "@@ -1,3 +1,3 @@"}) {
        const std::string at = plain_code + ";48;2;50;54;60m" + line;
        check(sgr.find(at) != std::string::npos,
              std::string("a bare fence renders \"") + line + "\" as plain code, whatever it looks like");
        check(sgr.find(added + ";48;2;20;22;26m" + line) == std::string::npos &&
                  sgr.find(removed + ";48;2;20;22;26m" + line) == std::string::npos,
              std::string("…and NOWHERE in the frame does \"") + line + "\" carry a diff role");
      }
      // Word level: the changed run of a 1:1 pair is the SAME colour with bold —
      // an emphasis on its line, which is why it is not a must-differ pair (Style.hpp).
      check(sgr.find("\x1b[0;1;38;2;192;106;100;48;2;20;22;26m100") != std::string::npos &&
                sgr.find("\x1b[0;1;38;2;173;238;174;48;2;20;22;26m200") != std::string::npos,
            "the changed WORD of a paired line takes the _word role on BOTH sides, bold on the line's own colour");
    }

    // ---- milestone 15: --check, --generate, the Check popup ----
    check(ed_check.find("\xE2\x95\xAD report ") != std::string::npos && ed_check.find("badges: dark") != std::string::npos && ed_check.find("roles (fg on bg") != std::string::npos,
          "Check opens the report popup with the badges and the per-role numbers");
    check(ed_fixes.find("nothing to fix") != std::string::npos, "the shipped default has nothing to fix");
    {
      int rc = 0;
      const std::string bin = std::string("'") + ROLLTUI_STUDIO_BIN + "'";
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
      RolltuiJsonValue* lying = rolltui_json_parse(g1.data(), g1.size(), nullptr);
      RolltuiJsonValue* claims = rolltui_json_array();
      rolltui_json_array_push(claims, json_string_v("high-contrast"));
      rolltui_json_array_push(claims, json_string_v("mono"));
      RolltuiJsonValue* meta = rolltui_json_clone(json_get_v(lying, "meta"));
      json_set_v(meta, "badges", claims);
      json_set_v(lying, "meta", meta);
      std::ofstream(scratch + "/lying.json", std::ios::binary) << json_dump_text(lying, 2);
      rolltui_json_free(lying);
      const std::string liar = run(bin + " --check '" + scratch + "/lying.json'" + presets, rc);
      check(rc != 0 && liar.find("CLAIM FAILED: mono") != std::string::npos, "a file claiming a badge it does not have fails --check with the claim named");
    }
    // ---- a menu is a FILE, and a dropped one opens with NO REBUILD ----
    // The milestone's Done-when, end to end through the real binary: two files nobody
    // compiled — a layout naming `menu:extra` and the menu it names — put on screen by
    // a studio that has never heard the name 'extra'. Then the same name in the
    // user's own menus/main.json, which shadows the shipped settings menu the F2 popup
    // shows: what ships is a default, not a fixture.
    {
      int rc = 0;
      const std::string p = scratch + "/p5";
      std::filesystem::create_directories(p + "/menus");
      std::ofstream(p + "/menus/extra.json", std::ios::binary) << R"({"id":"root","label":"dropped","items":[
        {"id":"one","label":"a dropped item"},{"id":"two","label":"another one"}]})";
      std::ofstream(scratch + "/dropped-layout.json", std::ios::binary) << R"({"name":"dropped","root":{"column":[
        {"id":"tx","content":"transcript:session"},
        {"id":"m","content":"menu:extra","size":6,"border":"single","title":"dropped menu"},
        {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})";
      const std::string cmd = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
                              "/session/demo.md' --frame 80x24 --theme default-dark --presets '" + p + "' --layout '" + scratch +
                              "/dropped-layout.json'";
      const std::string out = run(cmd, rc);
      check(rc == 0 && out.find("a dropped item") != std::string::npos && out.find("another one") != std::string::npos &&
                out.find("dropped menu") != std::string::npos,
            "a dropped menus/extra.json named by a dropped layout file opens with no rebuild and no host code");
      // The same rung under the name the shipped layouts already use: F2 shows the
      // user's menu instead of the library's, again with nothing rebuilt.
      std::ofstream(p + "/menus/main.json", std::ios::binary) << R"({"id":"root","label":"mine","items":[{"id":"x","label":"my own item"}]})";
      const std::string f2 = run(std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
                                     "/session/demo.md' --frame 80x24 --theme default-dark --presets '" + p + "' --keys \"F2\"",
                                 rc);
      check(rc == 0 && f2.find("my own item") != std::string::npos && f2.find("Ambiguous width") == std::string::npos,
            "…and a user's menus/main.json shadows the shipped settings menu in the F2 popup");
    }
    // ---- the LAYOUT declares the actions, end to end ---------------------------------
    // The milestone's Done-when through the real binary: an action nothing has compiled
    // in — declared by a dropped layout file, given a chord by a dropped bindings file,
    // named by a dropped menu file — appears in the help popup with its live key and in
    // the menu with the same key, and a menu id naming an action nobody declared is
    // reported instead of sitting there dead.
    {
      int rc = 0;
      const std::string p = scratch + "/p6";
      std::filesystem::create_directories(p + "/menus");
      std::filesystem::create_directories(p + "/bindings");
      std::ofstream(p + "/menus/decl.json", std::ios::binary) << R"({"id":"root","label":"declared","items":[
        {"id":"z","label":"Zoom the transcript","action":"app.zoom"},
        {"id":"n","label":"Nothing declares this","action":"app.nowhere"}]})";
      // The shipped default bindings PLUS one chord for the new action. A bindings file
      // is the whole domain, so it names both — and neither the library nor the
      // studio has ever heard of app.zoom.
      {
        RolltuiBindings* b = load_default_bindings();
        RolltuiLayoutAction zoom_action{};
        zoom_action.name = "app.zoom";
        zoom_action.description = "zoom the transcript";
        rolltui_bindings_declare(b, &zoom_action, 1, nullptr, 0);
        RolltuiChord chord{};
        rolltui_chord_parse("ctrl+g", 6, &chord);
        rolltui_bindings_bind(b, "app.zoom", 8, &chord, nullptr, nullptr);
        std::ofstream(p + "/bindings/decl.json", std::ios::binary) << bindings_to_json_text(b, "decl");
        rolltui_bindings_free(b);
      }
      std::ofstream(scratch + "/declared-layout.json", std::ios::binary) << R"({"name":"declared",
        "actions":{"app.help":"open help","app.zoom":"zoom the transcript"},
        "popups":[{"id":"help","x":0,"y":0,"w":"100%","h":"100%","modal":true,
                   "root":{"content":"help","border":"single","title":"help","focusable":true}}],
        "root":{"column":[
          {"id":"m","content":"menu:decl","size":5,"border":"single","title":"declared menu"},
          {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})";
      // Wide and tall on purpose: the studio says a load report on its status line
      // and the help popup lists six scopes, so both answers have to fit on screen.
      const std::string base = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) +
                               "/session/demo.md' --theme default-dark --presets '" + p + "' --bindings decl --layout '" + scratch +
                               "/declared-layout.json'";
      const std::string menu_out = run(base + " --frame 400x10", rc);
      check(rc == 0 && menu_out.find("Zoom the transcript") != std::string::npos && menu_out.find("Ctrl-G") != std::string::npos,
            "a menu item's shortcut is the chord the bindings file gave the action the LAYOUT declared");
      check(menu_out.find("item 'n' names the action 'app.nowhere', which no layout declares") != std::string::npos,
            "…and an item naming an action nobody declared is reported by name, not left dead");
      const std::string help_out = run(base + " --frame 100x90 --keys \"F1\"", rc);
      check(rc == 0 && help_out.find("Ctrl-G      zoom the transcript") != std::string::npos,
            "…and `help` RENDERS an action that exists only because a layout file declared it, with its chord");
    }
    // ---- the tool scopes are NOT in library_actions() --------------------------------
    // The milestone's "every studio key still works", through the real binary and with a
    // preset directory that has no bindings file at all — so the ONLY thing that can be
    // binding these keys is the tool table this binary mounts. A quit is observable
    // because a script stops at one: `CtrlQ F1` renders the frame WITHOUT the help popup
    // that `F1` alone opens, so the assertion fails whether Ctrl-Q stops working OR stops
    // being a quit.
    {
      int rc = 0;
      const std::string p = scratch + "/p11";
      const std::string bin = std::string("'") + ROLLTUI_STUDIO_BIN + "' '" + std::string(ROLLTUI_FIXTURE_DIR) + "/session/demo.md'";
      const std::string base = bin + " --frame 80x24 --theme default-dark --presets '" + p + "'";
      const std::string help = run(base + " --keys \"F1\"", rc);
      const std::string quit = run(base + " --keys \"CtrlQ F1\"", rc);
      const std::string none = run(base, rc);
      check(help.find("focus:help") != std::string::npos && quit == none && quit != help,
            "Ctrl-Q quits the studio with nothing but the mounted tool's own table (the one key a person actually presses)");
      for (const auto& [keys, want] : {std::pair<const char*, const char*>{"F7", "[keys editor]"}, {"F4", "[theme editor]"}, {"F6", "[layout editor]"}})
        check(run(base + " --keys \"" + keys + "\"", rc).find(want) != std::string::npos,
              std::string(keys) + " still opens the " + want + " — editor.* is declared by the host that mounts the editors");
      // "N more" IS A CONTROL, SO IT BEHAVES LIKE ONE. It is drawn like a label at the foot of
      // any scrolled text view, and a person who can see "74 more" and click it expects to arrive
      // there. The transcript had this; `text`, `file` and `help` share one engine that had no
      // mouse handling at all, so in the help popup it was decoration.
      {
        const std::string plain = run(base + " --keys \"F1\"", rc);
        std::size_t row = 0, col = 0;
        {
          std::istringstream in(plain);
          std::size_t y = 0;
          for (std::string r; std::getline(in, r); ++y) {
            const std::size_t at = r.find("\xE2\x96\xBC");
            if (at == std::string::npos) continue;
            std::size_t cells = 0;
            for (std::size_t i = 0; i < at; ++i)
              if ((r[i] & 0xC0) != 0x80) ++cells;
            row = y; col = cells + 3;  // a few cells into the marker, not its first
          }
        }
        check(row != 0, "the help popup shows a \"N more\" marker to click");
        const std::string clicked = run(base + " --keys \"F1 Click " + std::to_string(col) + "," +
                                        std::to_string(row) + "\"", rc);
        check(clicked != plain, "…and clicking it scrolls the help, rather than being a label that lies");
      }

      // PREVIEWING A DIFFERENT DOCUMENT ACTUALLY SHOWS IT. The parser reuses `e0`, `e1`, … for
      // every document it reads, and the transcript's parse cache is keyed by an entry's id and
      // validated by its VERSION — so a second document arriving under the first one's keys with
      // an unmoved version was served the first one's text. The same defect is why reloading a
      // changed file on disk showed the old content.
      {
        const std::string doc_dir = std::string(ROLLTUI_FIXTURE_DIR) + "/session";
        auto lines_of = [&](const std::string& file, const std::string& keys) {
          const std::string out = run(bin + " '" + doc_dir + "/" + file + "' --frame 90x12 --presets '" + p +
                                      "'" + (keys.empty() ? "" : " --keys \"" + keys + "\""), rc);
          const std::size_t at = out.rfind("line ");
          return at == std::string::npos ? std::string() : out.substr(at, 16);
        };
        const std::string demo = lines_of("demo.md", "");
        const std::string effects = lines_of("effects.md", "");
        check(!demo.empty() && demo != effects,
              "the two fixture documents are different lengths, so a swap is visible at all");
        // Ctrl-E opens the picker on the document's own directory; `..` is row 0, so two Downs
        // reach the second entry, and the entries are sorted with directories first.
        const std::string picked = lines_of("demo.md", "CtrlE Down Down Enter");
        check(picked == effects,
              "…and choosing another one SHOWS it, rather than keeping the first [" + picked + " vs " + effects + "]");
      }

      // THE THUMB'S SHAPE IS THE THEME'S, not a literal in the drawing code. A thumb is drawn as
      // a capsule — `single` alone, else `top`, `middle`…, `bottom` — and a theme may replace all
      // four. The high-contrast designs do: a capsule's half-height caps trade visible mass for
      // softness, and mass is the point of a high-contrast design.
      {
        // Finds the thumb wherever it is: the caps and body appear in one column and nowhere
        // else on the frame, so reading the first one per row spells the thumb top to bottom
        // without this test needing to know the layout's geometry.
        auto thumb_of = [&](const char* theme) {
          const std::string out = run(bin + " '" + std::string(ROLLTUI_FIXTURE_DIR) +
                                      "/session/demo.md' --frame 120x40 --presets '" + p +
                                      "' --theme " + theme + " --keys \"PageUp\"", rc);
          static const char* kCells[] = {"\xE2\x94\x83", "\xE2\x95\xBB", "\xE2\x95\xB9", "\xE2\x80\xA2"};
          std::string col;
          std::istringstream in(out);
          for (std::string r; std::getline(in, r);) {
            std::size_t best = std::string::npos;
            const char* which = nullptr;
            for (const char* c : kCells) {
              const std::size_t at = r.find(c);
              if (at != std::string::npos && (best == std::string::npos || at < best)) { best = at; which = c; }
            }
            if (which) col += which;
          }
          return col;
        };
        const std::string capsule = thumb_of("ink");
        const std::string solid = thumb_of("contrast");
        check(capsule.find("\xE2\x95\xBB") == 0, "the shipped thumb is a CAPSULE: its first cell is the half-height DOWN stroke [" + capsule + "]");
        check(capsule.size() > 2 && capsule.rfind("\xE2\x95\xB9") == capsule.size() - 3,
              "…and its last is the half-height UP stroke, so a bar of any length has soft ends");
        check(!solid.empty() && solid.find("\xE2\x95\xBB") == std::string::npos,
              "…while a theme that asks for a SOLID thumb gets one, with no caps at all [" + solid + "]");
      }

      // THE WHEEL MOVES ANYTHING THAT SHOWS A SCROLLBAR. A kind reporting a scroll extent gets a
      // bar drawn for it, and a bar a person can see but not move is a control that lies. The
      // window does this rather than each kind, so a kind cannot forget: `help` never handled a
      // mouse event at all, and its bar was exactly that.
      {
        const std::string plain = run(base + " --keys \"F1\"", rc);
        const std::string rolled = run(base + " --keys \"F1 WheelDown WheelDown\"", rc);
        check(plain.find("focus:help") != std::string::npos, "the help popup opens");
        check(rolled != plain, "…and the WHEEL scrolls it, though `help` implements no mouse handling of its own");
        const std::string back = run(base + " --keys \"F1 WheelDown WheelDown WheelUp WheelUp\"", rc);
        check(back == plain, "…and rolling back the same distance returns the same frame, so it is a position and not a drift");
      }

      // A CYCLE IS THE CLAIM, SO A CYCLE IS WHAT IS ASSERTED. Naming the theme two presses along
      // tests which themes happen to ship, so adding one breaks a test about the KEY. Press it
      // until the starting theme comes back: that is what "cycles" means, and it holds for any
      // shipped set. The bound is a failure, not a limit — a key that never returns is not
      // cycling.
      {
        auto theme_after = [&](int presses) {
          std::string keys;
          for (int k = 0; k < presses; ++k) keys += (k ? " F3" : "F3");
          const std::string out = run(base + (presses ? " --keys \"" + keys + "\"" : ""), rc);
          const std::size_t nl = out.find_last_of('\n', out.size() - 2);
          const std::string line = nl == std::string::npos ? out : out.substr(nl + 1);
          const std::size_t a = line.find_first_not_of(' ');
          const std::size_t b = line.find(' ', a);
          return a == std::string::npos ? std::string() : line.substr(a, b - a);
        };
        const std::string start = theme_after(0);
        check(start == "default-dark", "the studio starts on the theme it was given [" + start + "]");
        check(theme_after(1) != start, "F3 changes the theme (studio.cycle_theme)");
        int closed_at = 0;
        std::vector<std::string> seen{start};
        for (int k = 1; k <= 24 && !closed_at; ++k) {
          const std::string t = theme_after(k);
          if (t == start) { closed_at = k; break; }
          seen.push_back(t);
        }
        check(closed_at > 1, "…and pressing it returns to where it started, which is what cycling means (closed at " +
                                 std::to_string(closed_at) + ")");
        std::vector<std::string> sorted = seen;
        std::sort(sorted.begin(), sorted.end());
        check(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(),
              "…visiting each shipped theme once before it comes round, never revisiting one early (" +
                  std::to_string(seen.size()) + " themes)");
      }
      // The keys editor lists what the studio declares, and `app` is in it even while empty.
      // It edits the LIVE table; handing it the store's undeclared working copy instead is
      // what made the app scope unrebindable.
      const std::string scopes = run(base.substr(0, base.find("--frame")) + " --frame 120x40 --keys \"F7 Enter\"", rc);
      check(scopes.find("editor") != std::string::npos && scopes.find("studio") != std::string::npos && scopes.find("app") != std::string::npos,
            "the keys editor lists the app, editor and studio scopes");
      const std::string app_scope = run(base.substr(0, base.find("--frame")) + " --frame 120x40 --keys \"F7 Enter Down Down Down Down Down Enter\"", rc);
      check(app_scope.find("Actions by scope \xE2\x80\xBA app") != std::string::npos && app_scope.find("help  F1, ?") != std::string::npos,
            "…and the app scope is REBINDABLE at last: the editor now edits the live table, not the store's undeclared copy");
      // A USER'S OWN bindings file — every one of the eleven rows in it — loads clean and
      // still drives the studio. Which side of the table the scope sits on is what decides
      // this: a library scope would have made every `studio.*`/`editor.*` row an unknown
      // action and thrown the user's keys away.
      const std::string p10 = " --bindings '" + std::string(ROLLTUI_FIXTURE_DIR) + "/bindings/vim-ish.json'";
      const std::string old_quit = run(base + p10 + " --keys \"CtrlQ F1\"", rc);
      const std::string old_none = run(base + p10, rc);
      check(rc == 0 && old_quit == old_none && old_none.find("bindings:") == std::string::npos,
            "a user's own bindings file loads with no complaint and its Ctrl-Q still quits");
      check(run(base + p10 + " --keys \"F7\"", rc).find("[keys editor]") != std::string::npos, "…and its F7 still opens the keys editor");
    }
    // ---- THE RENAME IS FINISHED: NO SOURCE SAYS `playground` AT ALL -----------------
    // One grep answers whether a rename actually happened or whether it was done in the
    // places a reader would look and left in the places they would not. Every source either
    // binary is built from is scanned — the library, its tools, roll's own — plus the
    // shipped preset FILES, which are compiled into the binary as bytes and are exactly
    // where an action name would survive unnoticed (menus/main.json named two).
    //
    // THE THRESHOLD IS NOW ZERO, and this control got STRICTLY
    // STRONGER rather than going quiet when its subject was deleted. It used to permit the
    // three-row migration table in `rolltui/c/rolltui_bindings.c` and required it to be
    // there (`table >= 3`), which was what armed it. That table is retired, so the exemption
    // is gone and the expected count is 0 everywhere.
    //
    // A CONTROL THAT EXPECTS ZERO MUST PROVE IT CAN SEE, which is this repo's most-repeated
    // failure and the reason the second word below exists: the same walk, the same
    // `getline`, the same `find` counts a word that MUST be everywhere. If the scanner
    // silently reads nothing — a wrong root, a bad extension filter, an unreadable file —
    // `sentinel` collapses to 0 and this fails, instead of `playground` reporting a clean 0
    // because nothing was ever looked at.
    {
      namespace fs = std::filesystem;
      const std::string root = std::string(ROLLTUI_SOURCE_DIR) + "/..";
      std::vector<std::string> scanned, hits;
      int sentinel = 0;  // the arming word: `rolltui` appears in every source here
      for (const fs::directory_entry& e : fs::recursive_directory_iterator(root)) {
        const std::string rel = fs::relative(e.path(), root).string();
        if (rel.rfind("build", 0) == 0 || rel.rfind(".git", 0) == 0 || rel.rfind("plan/", 0) == 0 ||
            rel.rfind("journal/", 0) == 0 || rel.rfind("artifacts/", 0) == 0 || rel.rfind("rolltui/tests/", 0) == 0 ||
            rel.rfind("tests/", 0) == 0 || rel.rfind("rolltui/third_party/", 0) == 0 || rel.rfind("rolltui/ucd/", 0) == 0)
          continue;
        const std::string ext = e.path().extension().string();
        const bool preset = rel.rfind("rolltui/presets/", 0) == 0 && ext == ".json";
        if (!preset && ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".c" && e.path().filename() != "CMakeLists.txt")
          continue;
        scanned.push_back(rel);
        std::ifstream in(e.path(), std::ios::binary);
        std::string line;
        int ln = 0;
        while (std::getline(in, line)) {
          ++ln;
          if (line.find("playground") != std::string::npos) hits.push_back(rel + ":" + std::to_string(ln));
          if (line.find("rolltui") != std::string::npos) ++sentinel;
        }
      }
      check(scanned.size() >= 60, "scanned every source and shipped preset both binaries are built from (" +
                                      std::to_string(scanned.size()) + " files)");
      // THE ARMING CHECK, and it runs before the one it arms: this same walk found the word
      // `rolltui` on thousands of lines, so a zero below is an absence and not a blindness.
      check(sentinel > 500, "the scanner can see: the same pass matched `rolltui` on " + std::to_string(sentinel) + " lines");
      check(hits.empty(), "no source says `playground` anywhere — the rename is finished and nothing keeps a table of it" +
                              (hits.empty() ? "" : ": " + hits.front()));
    }
    check(row_of(menu_open, "\xE2\x95\xAD menu ") == 5 && row_of(menu_big, "\xE2\x95\xAD menu ") == 8,
          "the menu popup re-places itself: top edge on row 5 at 80x24 (60% of 23 = 13 rows, centred: 11 - 6) and row 8 at 120x40 (23 rows: 19 - 11) (" +
              std::to_string(row_of(menu_open, "\xE2\x95\xAD menu ")) + ", " + std::to_string(row_of(menu_big, "\xE2\x95\xAD menu ")) + ")");
  }
  std::filesystem::remove_all(scratch);
  return report("rolltui studio_golden_test");
}
