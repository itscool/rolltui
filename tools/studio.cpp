//
// studio.cpp — the rolltui studio : renders
// a fixture transcript in a theme and a layout, so trying a layout or theme idea and
// asserting it are the same command.
//
// WHAT A PERSON CAN DESIGN HERE WITHOUT WRITING JSON: a screen is FOUR
// files and this tool authors all four — a theme (F4), a layout (F6), a bindings file (F7) and,
// now, a menu (F8). Started with no arguments at all it comes up on the shipped
// `default` screen with a placeholder in it, so building an app from nothing does not begin by
// being handed a file. Every key the layout and menu loaders accept has a field; that is not a
// claim in a comment but `rolltui-designable-test`, which reads the loaders' own source and
// fails when one grows a key no editor grows a field for.
//
// AND EXACTLY TWO THINGS IT CANNOT DO, both about VERIFICATION and neither about authoring:
//
//   1. A WIDGET KIND FROM ANOTHER APP PREVIEWS AS A LABELLED PLACEHOLDER. You can type
//      `canvas:sheet`, the file keeps it, and the target app builds it — this binary has no
//      canvas, so it draws `[canvas:sheet]` and says so rather than refusing the content or
//      quietly rewriting it to something it can draw.
//   2. AN ACTION NAME CANNOT BE CHECKED, because the action belongs to the app. A menu item's
//      `action` and an input's `validator` are the same case and not a third exception: they
// are typed, written, and the app reports at start-up what nothing reaches.
//
// Neither is a gap to close. Both are the same rule the library already runs on — the screen is
// the intent and the code catches up — and the alternative in each case is the tool refusing to
// record a design because it is not the app the design is for.
//
//   rolltui-studio FIXTURE.md [options]
//     --presets DIR            the preset store's directory (rolltui/Presets.hpp):
//                              the Theme working copy, user presets, layout files.
//                              Default $ROLL_CONFIG_DIR/rolltui, else ~/.config/roll/
//                              rolltui — the studio is a rolltui host like roll,
//                              and a runtime change it makes autosaves there
//     --shipped DIR            the shipped presets ROOT (themes/ and bindings/ under
//                              it) that "write a shipped preset" writes into — the
//                              editor's privilege; default: the source tree's presets/
//     --bindings NAME|FILE     a bindings preset (default, or a user preset) or a
//                              bindings file (milestone 17), filling this run's
//                              Bindings working copy without writing it
//     --theme NAME|FILE.json   a preset (default | default-dark | default-light |
//                              mono | a user preset) or a preset file or a
//                              colours-only theme file, filling this run's working
//                              copy without writing it (a file is re-read whenever
//                              its mtime changes — edit it in another window and
//                              watch)
//     --layout NAME|FILE.json  a built-in, a layouts/ file name, or a layout file
//                              (hot-reloaded the same way; see Layout.hpp for the
//                              format). Below the layout's own min_width / min_height
//                              the `stacked` built-in is used instead and the status
//                              line says so.
//     --mode dark|light        which variant a theme's {dark,light} values use
//                              (default: the working copy's mode; auto asks the
//                              terminal in interactive mode, dark under --frame)
//     --check NAME|FILE        milestone 15: print the analysis report (contrast, APCA,
//                              colour-vision simulations, badges) for a preset or a
//                              theme file, at both modes for a preset with pairs, and
//                              exit 1 when a badge the file CLAIMS in "meta" does not
//                              hold — so it can sit in a CI step
//     --generate RULESET --seed N --chaos X   print a generated theme file (dark and
//                              light variants as pairs) to stdout; the same inputs
//                              always print the same file
//     --dump-role ROLE         after a --frame, print the effective style of ROLE
//                              ("md_heading fg=#6ca0e0 bg=#14161a bold") — the
//                              golden harness's way to see a colour
//     --depth truecolor|256|16|mono   colour depth (default: detect from the env)
//     --ambiguous-wide         East Asian ambiguous width = 2 in a headless frame (a live
//                              terminal is measured; ROLL_AMBIGUOUS_WIDE=1 says it outright)
//     --code-fold FOLD,CAP     milestone 5b: fold a code block over FOLD lines to one
//                              summary row, and cap an open one at CAP (0 disables
//                              either). Defaults to this host's own 30,100.
//     --tick MS                milestone 6: render the frame AT elapsed MS, so an
//                              effect (Effects.hpp) is a golden frame like any other.
//                              Default 0. Interactively the real clock is used and this
//                              flag does nothing.
//     --dump-tick              after a --frame, print the wakeup this frame ASKS FOR
//                              ("--- tick ---" then the ms, or "none") — the harness's
//                              way to see "the tick runs only while something is marked"
//     --frame WxH              render exactly one frame at that size to stdout as
//                              plain text (one row per line, trailing spaces trimmed)
//                              and exit — the golden-frame harness and screenshot tool.
//                              If the scripted input copied a selection, the copied
//                              text follows the frame after a "--- copied ---" line.
//                              Under --frame the working copy is never written (a
//                              manual save-as still is: it is explicit).
//     --frame-sgr WxH          the same frame with colours, for a terminal `cat`
//     --keys "K K K"           scripted input applied before the frame (or before the
//                              interactive loop): Up Down Left Right PageUp PageDown
//                              (in Type:/Paste:, `_` is a space and `\_` a literal underscore)
//                              Home End Tab Escape Enter Backspace Delete, each with
//                              an optional Shift / Ctrl / Alt prefix (ShiftLeft,
//                              CtrlHome, AltEnter, AltBackspace, ShiftTab), CtrlA/U/K/
//                              W/D/O, AltC, AltD, F1-F8, WheelUp WheelDown,
//                              Type:text (typed one code point at a time; `_` is a
//                              space), Paste:text (one bracketed paste; `_` a space,
//                              `\n` a newline), mouse as Click X,Y · ShiftClick X,Y ·
//                              DblClick X,Y · TripleClick X,Y · Drag X,Y · Release
//                              X,Y, Tick (one auto-scroll step while a drag is past
//                              an edge), or a single character (typed). Scripted
//                              events are one second apart except the presses of a
//                              DblClick / TripleClick, which share a timestamp.
//
// Fixture format: a markdown file cut into entries by marker lines
//   <!-- user -->   <!-- assistant -->   <!-- note -->   <!-- tool: summary text -->
//   <!-- state: waiting -->   <!-- state: progress 0.4 -->
// (text before any marker is an assistant entry). User entries render verbatim with a
// "> " prefix in the prompt role; notes render verbatim in the note role; a tool
// entry is a FOLDABLE verbatim block whose summary is the marker's text (folded to
// start with). A `state:` entry is an assistant entry MARKED with one of Effects.hpp's
// states and an optional fraction — a document saying what is happening, never what it
// looks like, which is the whole of milestone 6's split written into a file.
//
// WIDGETS BY KIND, SOURCES BY NAME: a window's "content" is
// `kind[:source]` from the library's table, and the library's window table
// instantiates the widget and draws it — the studio only BINDS what is its own,
// by name: the fixture document as `session` (transcript:session), its facts as
// `status` (rows:status), the prompt as `prompt` (input:prompt), and its three
// composites, registered as the kinds `editor`, `confirm` and `report`. `help`, `text:<literal>`
// and `file:<path>` need no binding at all, so a layout file can put a label, a document
// or the key list on screen with no code here. A window naming something unbound draws
// the reason and says it in the status line; the studio never asks what a slot means.
//
// The settings menu is a FILE: `menu:main` in the layout resolves to
// <presets>/menus/main.json if the user has one, else to the library's shipped
// rolltui/presets/menus/main.json — which IS this menu. The studio only fills the
// choices whose options are runtime facts (the theme and layout presets it can see) and
// acts on the ids; a user may edit or shadow the file with no rebuild.
//
// KEYS ARE DATA (milestone 17): every key below is the default of an action — the app.*
// ones from rolltui/presets/bindings/default.json, and now the editor.* and
// studio.* ones from the tools this binary MOUNTS (tools/tool_actions.hpp), because a
// tool's keys are not every host's. The studio looks its own keys up in the
// Bindings working copy (app.*, editor.*, studio.* scopes), hands the same table to
// every widget, and renders the help popup and the status-bar hints from it, so a
// rebinding shows everywhere at once. F7 opens the KEYS EDITOR (tools/keys_editor.hpp:
// scope › action › add / remove / clear a chord; the next key pressed is the chord; a
// conflict moves and says so; Enter stays on submit).
//
// Keys, as shipped: Ctrl-C / Ctrl-Q quit · Tab / Shift-Tab cycle focus · Esc closes the top popup
// · F1 toggles the help popup · F2 opens the settings menu (milestone 11: a
// rolltui::Menu in the layout's "menu" popup — Theme / Layout / Depth as choices,
// ambiguous width as a toggle, reload / help / quit as actions; arrows, Enter, Left,
// Esc, typing filters) · Ctrl-P the same menu flattened as a command palette · F3
// cycles the shipped theme presets · F4 opens the THEME EDITOR (milestone 14:
// tools/theme_editor.hpp — a side popup drawn in the theme being edited; Roles ›
// role › fg › palette entry with a live preview; Enter commits, Esc cancels, Ctrl-Z /
// Ctrl-Y undo and redo; resets and shipped writes confirm in a popup; the transcript
// is the preview) · F6 opens the LAYOUT EDITOR (milestone 16: tools/layout_editor.hpp —
// the same side popup over the layout being edited; the selected node is drawn in
// border_active; Tab selects the next node, a click selects a window, a drag on a
// shared edge resizes it; split / swap / hide / border / title / slot / size / delete /
// popups through the menu; Save writes layouts/<name>.json) · F5 re-reads the
// fixture · Ctrl-L repaints. (Letters type, since
// milestone 10 — the app keys moved off them.) Scrolling: PgUp/PgDn, Ctrl-Home/End
// and the wheel always scroll the transcript; Home/End scroll it only while the input
// is empty (otherwise they move the caret); Up/Down scroll only while the transcript
// has focus. Mouse: click and drag select (auto-scrolling past an edge), double-click
// a word, triple-click a line, release copies (the studio shows the byte count —
// it has no clipboard of its own), Alt-C copies again, a click on a folded block's
// summary line unfolds it, Ctrl-O toggles the first fold in view; the same selection
// gestures work inside the input. Every event goes through the window stack's
// routing, so what the studio does is what a host would do. The status line shows
// theme, layout, size, scroll position, focus and the last frame's render time
// (instrumented from the first line — a slow frame is a number, not a feeling).
//
// ============================================================================================
// THIS FILE CALLS THE C DIRECTLY, in the idiom `paint.cpp`'s own header names. The three
// shapes that matter:
//
//   1. **NO PER-FRAME FRAME.** `rolltui_swap` owns both frames for the run and lends the
//      back one per repaint; `Frame prev; bool have_prev;` is gone. The studio had TWO
//      invalidation sites (a resize, and Ctrl-L's explicit repaint) — both are now
//      `rolltui_swap_invalidate(swap)`, which stays the host's policy exactly as before.
//   2. **APP-LIFETIME HANDLES ARE PLAIN MEMBERS RELEASED IN ONE DESTRUCTOR.** `App` owns
//      its own resources directly (no `rolltui::` wrapper classes); a missed release leaks
//      once and `rolltui_shutdown`'s `live_bytes == 0` catches it.
//   3. **THE THREE PRESET STORES ARE THE LIBRARY'S OWN CONCRETE TYPES** (`RolltuiThemeStore`,
//      `RolltuiLayoutStore`, `RolltuiBindingsStore` — `rolltui.h`), with this binary's own
//      editing of them — `_edit`, `_save_as`, `_add`, `_set_working` — reached through
//      `rolltui_studio.h`, the one header only the studio and its editors may include. No
//      `PresetStore<Domain>` adapter and no app-side RAII wrapper stand between this file and
//      the C API: a missed `_free` leaks once, the same as every other handle here.
//
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/rolltui.h"
#include "rolltui/rolltui_studio.h"

// ADDITIVE, NOT SUBTRACTIVE. `rolltui-studio` is the product and cannot drive itself; the script
// vocabulary is not compiled into it. `rolltui-studio-selftest` is this same source plus it, and
// is what renders a golden frame. `--check` and `--generate` are NOT here: they run the theme
// analyser and the generator, which the theme editor also offers, so they are a headless entry
// point to a shipped feature rather than a test hook.
#ifdef ROLLTUI_SELFTEST
#include "rolltui/selftest/script.hpp"
#endif

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_diff.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_presets.h"
#include "rolltui/c/rolltui_render.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"
#include "rolltui/c/rolltui_widget_transcript.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_widgets.h"
#include "rolltui/str.hpp"
#include "tool_str.hpp"
#include "keys_editor.hpp"
#include "layout_editor.hpp"
#include "menu_editor.hpp"
#include "theme_editor.hpp"
#include "tool_actions.hpp"

using rolltui::tools::KeysEditor;
using rolltui::tools::LayoutEditor;
using rolltui::tools::MenuEditor;
using rolltui::tools::MenuItem;
using rolltui::tools::ThemeEditor;
using rolltui::tools::builtin_layout;
using rolltui::StrVec;
using rolltui::StrView;

namespace {

constexpr std::size_t kRoleCount = ROLLTUI_ROLE_COUNT;

RolltuiStr read_file(const RolltuiStr& path, bool& ok) {
  RolltuiStr out;
  FILE* f = std::fopen(path.c_str(), "rb");
  ok = f != nullptr;
  if (!f) return out;
  char buf[4096];
  for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
  std::fclose(f);
  return out;
}

long mtime_of(const RolltuiStr& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) return -1;
  return static_cast<long>(st.st_mtime);
}

RolltuiStr color_to_string(RolltuiStyleColor c) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  const std::size_t n = rolltui_color_to_string(c, buf, sizeof buf);
  return rolltui::own(StrView(buf, n));
}
// A BORROW of the library's literal, which is what the C hands back: a copy per call would run on the status line
// and the rows source every frame.
StrView depth_name(unsigned char d) {
  std::size_t n = 0;
  const char* p = rolltui_color_depth_name(d, &n);
  return StrView(p, n);
}
std::optional<unsigned char> mode_from_setting(StrView s) {
  const int m = rolltui_theme_mode_from_name(s.data(), s.size());
  return m < 0 ? std::nullopt : std::optional<unsigned char>(static_cast<unsigned char>(m));
}

// `rolltui::Role`/`rolltui::EffectState` (the C++ scoped enums `RolltuiDocEntry` etc. carry,
// from Style.hpp/Effects.hpp, still unported) are distinct types from the C `RolltuiRole`/
// `RolltuiEffectState` enums with the same values — no implicit conversion between an
// `enum class` and a plain C enum, even with a matching underlying type. These two casts are
// the one place that gap is bridged.
rolltui::Role to_role(unsigned char r) { return static_cast<rolltui::Role>(r); }
rolltui::EffectState to_effect_state(unsigned char s) { return static_cast<rolltui::EffectState>(s); }

RolltuiRect content_rect(const RolltuiResolvedNode& rn) {
  RolltuiRect r{};
  rolltui_content_rect(&rn, &r);
  return r;
}
void collect_resolved(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<RolltuiResolvedNode>*>(ctx)->push_back(*rn);
}

// ---- the preset stores studio builds directly from the C API ----------------
// `rolltui::PresetStore<Domain>` and `Presets.hpp`'s three domain traits are the C++ ADAPTER
// this file no longer needs. The three domain descriptors are the LIBRARY's now
// (`rolltui_preset_domain`): this file had assembled them itself, identically to roll and to
// three tests, and — unlike roll — never released their parsed cache.

// ---- the three domain-specific report shapes, as RAII over the transparent C structs -------
struct ThemePresetReport : RolltuiThemePresetReport {
  ThemePresetReport() : RolltuiThemePresetReport{} {}
  ThemePresetReport(const ThemePresetReport&) = delete;
  ~ThemePresetReport() { rolltui_theme_preset_report_release(this); }
  // the JUDGEMENT is the library's — roll had written the identical six.
  bool clean() const { return rolltui_theme_preset_report_clean(this) != 0; }
  RolltuiStr summary() const {
    RolltuiStr out;
    rolltui_theme_preset_report_summary(this, &out);
    return out;
  }
};
struct LayoutPresetReport : RolltuiLayoutPresetReport {
  LayoutPresetReport() : RolltuiLayoutPresetReport{} {}
  LayoutPresetReport(const LayoutPresetReport&) = delete;
  ~LayoutPresetReport() { rolltui_layout_preset_report_release(this); }
  bool clean() const { return rolltui_layout_preset_report_clean(this) != 0; }
  RolltuiStr summary() const {
    RolltuiStr out;
    rolltui_layout_preset_report_summary(this, &out);
    return out;
  }
};
struct BindingsPresetReport : RolltuiBindingsPresetReport {
  BindingsPresetReport() : RolltuiBindingsPresetReport{} {}
  BindingsPresetReport(const BindingsPresetReport&) = delete;
  ~BindingsPresetReport() { rolltui_bindings_preset_report_release(this); }
  bool clean() const { return rolltui_bindings_preset_report_clean(this) != 0; }
  RolltuiStr summary() const {
    RolltuiStr out;
    rolltui_bindings_preset_report_summary(this, &out);
    return out;
  }
};

// THE STUDIO'S ONE SESSION. A host owns its context; this binary runs one screen at a time, and
// two of `App`'s own member initializers (`ctx`, `stacked_layout_`) need it before the
// constructor body runs — so it is a function-local static freed at exit rather than an `App`
// member.
inline RolltuiContext* studio_ctx() {
  struct Holder {
    RolltuiContext* c = rolltui_context_new();
    Holder() = default;
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    ~Holder() { rolltui_context_free(c); }
  };
  static Holder h;
  return h.c;
}

// A shipped-name table read into the vector the editors want, the one step every
// `_shipped_names` call needs done to it.
StrVec shipped_names_of(const RolltuiStrList& names) {
  StrVec out;
  for (const RolltuiStr& n : names) out.add(StrView(n));
  return out;
}

unsigned long long g_load_seq = 0;  // bumped per read: see the version note below

RolltuiDocument parse_fixture(StrView text) {
  const unsigned long long load_seq = ++g_load_seq;
  RolltuiDocument doc;
  RolltuiStr kind = "assistant", summary;
  unsigned char state = ROLLTUI_EFFECT_STATE_NONE;  // m6: what the NEXT entry is doing, if anything
  double fraction = 0;
  RolltuiStr buf;
  int n = 0;
  auto flush = [&]() {
    // Trim leading/trailing blank lines of the entry.
    const StrView all = buf;
    std::size_t a = 0, b = all.size();
    while (a < b && all[a] == '\n') ++a;
    while (b > a && all[b - 1] == '\n') --b;
    const StrView body = all.substr(a, b - a);
    if (body.empty()) { buf.clear(); return; }
    RolltuiDocEntry* e = rolltui_document_add(&doc);
    e->id = rolltui::format("e%d", n++);
    // THE VERSION MOVES PER LOAD, and it has to. An id is a stable identity across FRAMES, and
    // this parser reuses `e0`, `e1`, … for every document it reads — so a second document arrives
    // under the first one's keys, and the transcript's parse cache, which is keyed by id and
    // validated by version, serves the previous document's text. That is why reloading a changed
    // file, or previewing a different one, showed the old content.
    e->version = load_seq;
    rolltui::assign(e->text, body);
    if (kind == "user") { e->markdown = 0; e->role = to_role(ROLLTUI_ROLE_TEXT); e->prefix = "> "; e->prefix_role = to_role(ROLLTUI_ROLE_PROMPT); }
    else if (kind == "note") { e->markdown = 0; e->role = to_role(ROLLTUI_ROLE_NOTE); }
    else if (kind == "tool") { e->markdown = 0; e->role = to_role(ROLLTUI_ROLE_TEXT_MUTED); e->foldable = 1; e->summary.assign(summary); e->folded = 1; }
    else { e->markdown = 1; e->role = to_role(ROLLTUI_ROLE_TEXT); }
    // A marked entry. The fixture says WHICH STATE and nothing else.
    e->state = to_effect_state(state);
    e->progress = fraction;
    state = ROLLTUI_EFFECT_STATE_NONE;
    fraction = 0;
    buf.clear();
  };
  for (std::size_t pos = 0; pos < text.size();) {  // a line per '\n'; the last one needs none
    const std::size_t nl = text.find('\n', pos);
    const StrView line = text.substr(pos, nl == StrView::npos ? StrView::npos : nl - pos);
    pos = nl == StrView::npos ? text.size() : nl + 1;
    if (line == "<!-- user -->" || line == "<!-- assistant -->" || line == "<!-- note -->") {
      flush();
      kind = rolltui::own(line.substr(5, line.size() - 9));
      continue;
    }
    if (line.starts_with("<!-- state:") && line.size() >= 15 && line.ends_with("-->")) {
      flush();
      kind = "assistant";
      StrView spec = line.substr(11, line.size() - 14);
      std::size_t a = 0;
      while (a < spec.size() && spec[a] == ' ') ++a;
      spec = spec.drop_front(a);
      const std::size_t sp = spec.find(' ');
      const StrView name = spec.substr(0, sp);
      const int st = rolltui_effect_state_from_name(name.data(), name.size());
      state = st < 0 ? ROLLTUI_EFFECT_STATE_NONE : static_cast<unsigned char>(st);
      fraction = sp == StrView::npos ? 0 : std::strtod(rolltui::own(spec.drop_front(sp + 1)).c_str(), nullptr);
      continue;
    }
    if (line.starts_with("<!-- tool:") && line.size() >= 14 && line.ends_with("-->")) {
      flush();
      kind = "tool";
      StrView sum = line.substr(10, line.size() - 13);
      std::size_t a = 0, b = sum.size();
      while (a < b && sum[a] == ' ') ++a;
      while (b > a && sum[b - 1] == ' ') --b;
      summary = rolltui::own(sum.substr(a, b - a));
      continue;
    }
    buf += line;
    buf += '\n';
  }
  flush();
  return doc;
}

struct App;
// Forward declared so `App::bind_windows()` (defined inline, inside the class) can register
// them; the plugin bodies are defined after `App` since they call its methods.
RolltuiWidget editor_factory(void* ctx, RolltuiWindows* w, const char* content, size_t len);
RolltuiWidget confirm_factory(void* ctx, RolltuiWindows* w, const char* content, size_t len);
RolltuiWidget report_factory(void* ctx, RolltuiWindows* w, const char* content, size_t len);
RolltuiWidget placeholder_factory(void* ctx, RolltuiWindows* w, const char* content, size_t len);

struct App {
  RolltuiContext* ctx = studio_ctx();  // BORROWED: the binary's one session (see studio_ctx)
  RolltuiStr fixture_path, theme_arg, layout_arg;
  std::optional<unsigned char> mode_flag;   // --mode; else the working copy's mode
  unsigned char mode = ROLLTUI_MODE_DARK;     // the variant in use this frame
  unsigned char detected_mode = ROLLTUI_MODE_DARK;  // OSC 11's answer (interactive), dark otherwise
  unsigned char depth = ROLLTUI_DEPTH_TRUECOLOR;
  bool ambiguous = false;
  // this host's own thresholds for a long code block, overridable with
  // --code-fold so a golden can exercise the cap without a hundred-line fixture.
  int code_fold_over = 30, code_cap = 100;
  int w = 80, h = 24;  // the screen
  RolltuiDocument doc;
  // The look comes from the preset store's Theme working copy, exactly as in roll; the
  // editor, when open, previews its own current theme.
  RolltuiThemeStore* store = nullptr;
  RolltuiLayoutStore* lstore = nullptr;    // the Layout working copy (Phase 10 m1)
  RolltuiBindingsStore* bstore = nullptr;  // the Bindings working copy (milestone 17)
  // `_new`'s own `shipped_dir`, kept for the WriteShipped confirm text: no store getter reads
  // it back. `may_write_shipped` is not tracked at all — every store this App builds passes
  // `1` (the studio's own privilege: it writes what ships), so the two editors that ask are
  // just told `true` directly.
  RolltuiStr theme_shipped_dir, bindings_shipped_dir;
  RolltuiBindings* bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));  // what this frame runs on
  std::uint64_t bstore_seen = 0;
  RolltuiStr bindings_arg;
  bool persist = true;                  // false under --frame: the working copy is never written
  std::uint64_t store_seen = 0;
  RolltuiStyle resolved_styles[ROLLTUI_ROLE_COUNT]{};  // the working copy's colours at `mode`
  RolltuiStr resolved_name;
  // Per-frame text, held and REFILLED rather than rebuilt: the status line, the editors'
  // "preset:" line, the rows' layout cell and the two store labels, so a warm frame allocates
  // nothing for them.
  RolltuiStr status_line, editor_line, layout_row, editor_status;
  // CALLER-FILLED, one per run: the layout editor's tree view, refilled every frame the panel
  // is drawn so the rows keep their buffers.
  RolltuiRows tree_rows_{};
  RolltuiStr theme_label_str, keys_label_str, layout_label_str;
  RolltuiStyle theme_styles[ROLLTUI_ROLE_COUNT]{};     // what this frame draws with (resolved, or the editor's preview)
  RolltuiEffectMap* effects_map = nullptr;             // OWNED: the resolved theme's effects
  std::uint64_t lstore_seen = 0;
  RolltuiStr theme_note;
  long theme_mtime = -1;
  RolltuiLayout layout{};
  RolltuiStr layout_note;
  long layout_mtime = -1;
  bool stacked_fallback = false;
  RolltuiLayout stacked_layout_ = builtin_layout(studio_ctx(), "stacked");  // cached: the shipped fallback screen
  // The theme editor (milestone 14) and the layout editor (milestone 16) share the
  // side popup; one is open at a time.
  enum class EditorMode { None, Theme, Layout, Keys, Menu };
  EditorMode editor_mode = EditorMode::None;
  ThemeEditor teditor{ctx};
  LayoutEditor leditor{ctx};  // resolves kinds against this session
  KeysEditor keditor{ctx};
  MenuEditor meditor{ctx};  // Phase 27 m2: the fourth file type
  bool editor_open = false;
  bool editor_left = false;  // which side the layout editor's panel is on
  RolltuiStr pending_save;              // a save-as awaiting its overwrite confirmation
  RolltuiStr confirm_arg;               // what the pending confirmation acts on: `std::function` cannot hold a move-only capture
  RolltuiStr confirm_text;
  std::function<void()> confirm_action;
  RolltuiStr report_text_;   // the Check popup's text
  int report_top = 0;        // the Check report is a registered kind: the studio scrolls it
  int report_lines = 0;      // its wrapped length, from the last draw
  RolltuiStr hint;
  RolltuiStr note;  // the status line's last word about a file dialog, until the next key
  RolltuiStr window_note;   // a window that cannot draw (an unbound source, a bad kind)
  bool show_timing = false;  // the frame-time row/field (interactive only)
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  // Every window's widget comes from its content: the studio binds the fixture document,
  // its status rows, the prompt and its own composites by name, and never asks what a
  // slot means.
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiTranscript* transcript() { return rolltui_windows_transcript(windows, "session", 7); }
  RolltuiInput* editor() { return rolltui_windows_input(windows, "prompt", 6); }
  RolltuiMenu* menu() { return rolltui_windows_menu(windows, "main", 4); }  // menus/main.json (Phase 10 m3)
  void set_menu_value(StrView id, StrView v) {
    rolltui_menu_set_value(menu(), id.data(), id.size(), v.data(), v.size());
  }
  int submitted = 0;        // entries the input added to the document
  RolltuiStr copied;        // the last copy (the studio has no clipboard)
  bool copied_any = false;
  std::uint64_t clock_ms = 0;  // the clock handed to the widgets (real or scripted)
  // The clock EFFECTS are applied at, kept apart from clock_ms on purpose — the
  // widgets' clock is scripted (a click pair is one second after the last), and motion
  // wants the real one interactively and `--tick N` under --frame. One field each beats
  // one field meaning two things at two times.
  std::uint64_t effect_ms = 0;
  long last_frame_us = 0;
  // What the last frame's marks came to: an unknown kind is said, not swallowed. Read by
  // the STATUS LINE before it is overwritten (one frame stale, on purpose — see render_into).
  StrVec effects_unknown_kinds;
  std::size_t shipped_theme_index = 0;

  // App-lifetime working memory, one of each, released in the destructor (CLAUDE.md's
  // strategy 4 / rule 4 of rolltui.h): nothing here is per-frame storage.
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  RolltuiEffectScratch* effect_scratch = rolltui_effect_scratch_new();
  RolltuiUnicodeScratch* u_scratch = rolltui_u_scratch_new();
  RolltuiWrapLines* wrap_scratch = rolltui_wrap_new();
  RolltuiDiffScratch* diff_scratch = rolltui_diff_scratch_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();

  // The studio has no clipboard: a copy is remembered so a test can assert it. One trampoline
  // for both widgets, since both take the same {fn, ctx} pair.
  static void remember_copy(void* ctx, const char* text, std::size_t len) {
    App& self = *static_cast<App*>(ctx);
    self.copied.assign(text, len);
    self.copied_any = true;
  }

  App() {
    rolltui_layout_init(&layout);
    rolltui_context_set_library_defaults(ctx);
    rolltui_transcript_set_copy(transcript(), remember_copy, this);
    rolltui_input_set_copy(editor(), remember_copy, this);
    RolltuiInputOptions o;
    o.placeholder = "type here";
    rolltui_input_set_options(editor(), &o);
    bind_windows();
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
    rolltui_rows_release(&tree_rows_);
    rolltui_layout_release(&layout);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_diff_scratch_free(diff_scratch);
    rolltui_wrap_free(wrap_scratch);
    rolltui_u_scratch_free(u_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_bindings_free(bindings);
    rolltui_draw_scratch_free(draw_scratch);
    rolltui_effect_scratch_free(effect_scratch);
    rolltui_effect_map_free(effects_map);
    rolltui_theme_store_free(store);
    rolltui_layout_store_free(lstore);
    rolltui_bindings_store_free(bstore);
    // The context is NOT freed here: it is the binary's (see `studio_ctx`), not this App's.
  }

  const RolltuiStyle& style(unsigned char role) const { return *rolltui_theme_style(theme_styles, kRoleCount, role); }
  int put_text(RolltuiFrame* f, int x, int y, StrView s, RolltuiStyle sty, int max_cells) {
    return rolltui_frame_put_text(f, draw_scratch, x, y, s.data(), s.size(), sty, max_cells, ambiguous ? 1 : 0, 0);
  }
  void fill(RolltuiFrame* f, RolltuiRect r, RolltuiStyle sty) { rolltui_frame_fill(f, draw_scratch, r, sty, nullptr, 0); }
  void tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle sty) { rolltui_frame_tint(f, r, sty); }

  // The action of `scope` this chord serves, or "" — a BORROW valid until the table next
  // changes, which every call site below reads before it can.
  StrView action_for(const RolltuiChord& k, StrView scope) {
    std::size_t n = 0;
    const char* p = rolltui_bindings_action_for(bindings, &k, scope.data(), scope.size(), &n);
    return p ? StrView(p, n) : StrView();
  }

  // THE LIBRARY'S OWN menu roles, read back rather than kept as a second copy (see the
  // task's note on `editor_menu_roles`/`editor_input_roles` — resolved by evidence below):
  // `rolltui_windows_set_library_defaults` (called once, in the constructor) already installs
  // a `RolltuiMenuRoles`/the input roles inside `RolltuiBuiltinRoles` that are BYTE-FOR-BYTE
  // what this file used to hardcode a second time, and both are reachable through
  // `rolltui_windows_menu_roles`/`rolltui_windows_builtin_roles`. Reading them here means a
  // change to the library's default is never a second edit.
  const RolltuiMenuRoles* editor_menu_roles() const { return rolltui_windows_menu_roles(windows); }
  RolltuiInputRoles editor_input_roles() const {
    const RolltuiBuiltinRoles* r = rolltui_windows_builtin_roles(windows);
    return {r->input_text, r->input_selection, r->input_placeholder};
  }
  static RolltuiDrawScratch*& editor_draw_scratch_slot() {
    static RolltuiDrawScratch* s = nullptr;
    return s;
  }
  static RolltuiDrawScratch* editor_draw_scratch() {
    RolltuiDrawScratch*& slot = editor_draw_scratch_slot();
    if (!slot) {
      slot = rolltui_draw_scratch_new();
      rolltui_on_shutdown([] {
        RolltuiDrawScratch*& s = editor_draw_scratch_slot();
        rolltui_draw_scratch_free(s);
        s = nullptr;
      });
    }
    return slot;
  }
  // A raw `RolltuiMenu*`'s draw — the one-line convenience every editor's own draw call needs.
  void draw_raw_menu(RolltuiMenu* m, RolltuiFrame* f, bool focused) const {
    const RolltuiInputRoles ir = editor_input_roles();
    rolltui_menu_draw(m, f, editor_draw_scratch(), theme_styles, editor_menu_roles(), &ir, focused ? 1 : 0);
  }

  // The sources a layout may name. Everything a window can show in the studio is here, by
  // name, once — a layout file that says `rows:status` or `text:hello` needs no code at
  // all, and one that names something unbound draws the reason instead of nothing.
  void bind_windows() {
    rolltui_windows_bind_document(windows, "session", 7, &doc);
    rolltui_windows_bind_rows(
        windows, "status", 6, [](void* ctx, RolltuiRows* out) { static_cast<App*>(ctx)->status_rows(*out); }, this,
        nullptr);
    rolltui_windows_bind_submit(
        windows, "save_name", 9,
        [](void* ctx, const char* text, std::size_t len) { static_cast<App*>(ctx)->save_preset_file(StrView(text, len)); },
        this, nullptr, /*Keep=*/1);
    rolltui_windows_bind_submit(
        windows, "prompt", 6,
        [](void* ctx, const char* text, std::size_t len) { static_cast<App*>(ctx)->append_prompt(StrView(text, len)); },
        this, nullptr, /*SendAndClear=*/0);
    // The find bar's Enter is "next match" — the universal find-bar convention. sync_find()
    // first, because the matches must exist before stepping through them.
    rolltui_windows_bind_submit(
        windows, "find", 4,
        [](void* ctx, const char*, std::size_t) {
          App* a = static_cast<App*>(ctx);
          a->sync_find();
          rolltui_transcript_find_next(a->transcript());
        },
        this, nullptr, /*Keep=*/1);
    // The studio's three composites are REGISTERED KINDS, not draw callbacks
    // bound by name: each is a plugin the studio owns, its widget receives its own events,
    // and nothing below dispatches by window name. Each takes no source.
    rolltui_widget_kind_register(ctx, "editor", 6, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_context_register_kind(ctx, "editor", 6, editor_factory, this, nullptr);
    rolltui_widget_kind_register(ctx, "confirm", 7, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_context_register_kind(ctx, "confirm", 7, confirm_factory, this, nullptr);
    rolltui_widget_kind_register(ctx, "report", 6, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_context_register_kind(ctx, "report", 6, report_factory, this, nullptr);
    // A CONTENT THIS BINARY CANNOT BUILD PREVIEWS AS A LABELLED PLACEHOLDER, and
    // that is this tool's decision rather than the library's. roll and paint want the error
    // panel: a window naming a kind they never registered is their own bug. A DESIGN TOOL is
    // the one host for which it is not a bug at all — a screen for another app names that
    // app's kinds by definition, and the studio replacing the library's error factory is how
    // it says "this window is correct and I am the one who cannot draw it".
    rolltui_context_set_error_factory(ctx, placeholder_factory, this);
    // Diff colouring: this host DECLARING that a ```diff fence in its
    // documents means a diff — never a sniff of what a block holds.
    rolltui_windows_set_highlight(windows, diff_highlight, diff_scratch, nullptr);
    constexpr const char* kMouseHelp =
        "mouse: drag selects (auto-scrolls past an edge); release copies; double-click a word; "
        "triple-click a line;\n"
        "click a folded block's summary to toggle it; in the layout editor a click selects, a "
        "drag on a seam resizes";
    rolltui_context_set_help(ctx, "", 0, kMouseHelp, std::strlen(kMouseHelp));
    rolltui_context_clear_help_scopes(ctx);
    for (const char* s : {"input", "transcript", "app", "editor", "studio", "stack"})
      rolltui_context_add_help_scope(ctx, s, std::strlen(s));
  }

  // A syntax highlighter over `rolltui_diff_spans`, matching `RolltuiMdHighlightFn`'s shape
  // directly (the code lines already arrive as an array, so no line-lookup callback is
  // needed the way `rolltui_diff_spans`'s own `RolltuiDiffLineFn` asks for).
  static void diff_highlight(void* ctx, const char* lang, std::size_t lang_n, const RolltuiMdCodeLine* lines,
                             std::size_t line_count, std::size_t index, RolltuiMdSpanSink emit, void* sink) {
    RolltuiDiffScratch* scratch = static_cast<RolltuiDiffScratch*>(ctx);
    if (!rolltui_diff_is_language(lang, lang_n)) return;
    RolltuiDiffSpan spans[ROLLTUI_DIFF_MAX_SPANS];
    const std::size_t n = rolltui_diff_spans(
        scratch, lang, lang_n, lines, line_count,
        [](const void* block, std::size_t i, std::size_t* len) -> const char* {
          const RolltuiMdCodeLine* ls = static_cast<const RolltuiMdCodeLine*>(block);
          *len = ls[i].n;
          return ls[i].p;
        },
        index, rolltui_diff_default_roles(), spans, ROLLTUI_DIFF_MAX_SPANS);
    for (std::size_t i = 0; i < n; ++i) emit(sink, spans[i].begin, spans[i].end, spans[i].role);
  }

  // The menu's STRUCTURE is menus/main.json; what is left here is the part a file cannot
  // hold — the options that are runtime facts (which presets exist) and the current values.
  // What a theme SAYS it is. `meta.badges` is either one list, or a dark/light pair when the
  // variants differ — the same pair form a role's colours use. Read rather than recomputed: the
  // loader already verifies the declaration against the colours, so a stale one is a reported
  // problem elsewhere and not this menu's to re-derive.
  static RolltuiStr declared_badges(const RolltuiJsonValue* colours, int for_mode) {
    const RolltuiJsonValue* meta = colours ? rolltui_json_get(colours, "meta", 4) : nullptr;
    const RolltuiJsonValue* b = meta ? rolltui_json_get(meta, "badges", 6) : nullptr;
    if (!b) return {};
    if (rolltui_json_array_size(b) == 0) {
      const char* k = for_mode == ROLLTUI_MODE_LIGHT ? "light" : "dark";
      b = rolltui_json_get(b, k, std::strlen(k));
    }
    RolltuiStr out;
    for (std::size_t i = 0; b && i < rolltui_json_array_size(b); ++i) {
      std::size_t n = 0;
      const char* w = rolltui_json_as_string(rolltui_json_array_at(b, i), "", 0, &n);
      out.append(w, n);
      out += ' ';
    }
    return out;
  }

  void refresh_menu() {
    RolltuiMenuItemList themes, layouts;
    RolltuiPresetList tl, ll;
    // A THEME'S CLASSIFICATION BELONGS WHERE IT IS CHOSEN. Every theme declares what it is and
    // the loader verifies it, so the fact is already trustworthy and was only ever invisible.
    // One short phrase, most-important first: a theme that is NOT readable is a warning and
    // outranks anything good about it; high contrast and colour-vision safety are the two
    // reasons a person reaches for a particular theme.
    if (store) {
      rolltui_theme_store_list(store, &tl);
      for (const RolltuiPresetInfo& p : tl) {
        const RolltuiStr& name = p.name;
        RolltuiStr note;
        {
          ThemePresetReport rep;
          RolltuiThemePresetValue* v = rolltui_theme_store_get(store, name.data(), name.size(), &rep);
          if (v) {
            const RolltuiStr b = declared_badges(v->colours, mode);
            const auto says = [&](const char* w) { return StrView(b).contains(w); };
            // `mono` FIRST: a monochrome theme carries no colour on purpose and separates by
            // attribute instead, so reading its missing `readable` as a fault would call a
            // design decision a defect.
            if (says("mono")) note = "monochrome";
            else if (!b.empty() && !says("readable")) note = "low contrast";
            else if (says("high-contrast")) note = "high contrast";
            else if (says("cvd-safe")) note = "colour-vision safe";
            rolltui_theme_preset_value_free(v);
          }
        }
        if (!p.shipped) {
          if (note.empty()) note = "yours";
          else note += " · yours";
        }
        // The note rides in the SHORTCUT column, right-aligned and muted, so the option's label is
        // its name alone — which is what the choice row shows as its value once one is chosen.
        themes.push_back(RolltuiMenuItem::action(name.c_str(), name.c_str(), note.c_str()));
      }
    }
    if (lstore) { rolltui_layout_store_list(lstore, &ll); for (const RolltuiPresetInfo& p : ll) layouts.push_back(RolltuiMenuItem::action(p.name.c_str(), p.name.c_str(), p.shipped ? "" : "yours")); }
    rolltui_menu_set_options(menu(), "theme", 5, &themes);
    rolltui_menu_set_options(menu(), "layout", 6, &layouts);
    RolltuiStr label;
    if (store) { rolltui_theme_store_label(store, &label); set_menu_value("theme", label); }
    else set_menu_value("theme", "");
    if (lstore) { rolltui_layout_store_label(lstore, &label); set_menu_value("layout", label); }
    else set_menu_value("layout", layout.name);
    set_menu_value("depth", depth_name(depth));
    { const RolltuiThemePresetValue* w = store ? rolltui_theme_store_working(store) : nullptr;
      set_menu_value("mode", w ? StrView(w->mode) : StrView("auto")); }
    rolltui_menu_set_checked(menu(), "ambiguous", 9, ambiguous);
  }
  void open_menu(bool palette) {
    if (rolltui_window_stack_has_popup(stack, "menu", 4)) { close_popup("menu"); return; }
    refresh_menu();  // presets and layout files may have changed
    rolltui_menu_reset(menu());
    rolltui_menu_set_palette(menu(), palette ? 1 : 0);
    rolltui_window_stack_push_popup(stack, &effective_layout(), "menu", 4);
  }
  void close_popup(StrView id) {
    while (rolltui_window_stack_depth(stack) > 1) {
      const RolltuiLayer* top = rolltui_window_stack_layer(stack, rolltui_window_stack_depth(stack) - 1);
      if (StrView(top->id) == id) break;
      rolltui_window_stack_pop(stack);
    }
    if (rolltui_window_stack_depth(stack) > 1) rolltui_window_stack_pop(stack);
  }
  // Returns false to quit.
  bool menu_event(const RolltuiMenuEvent& ev) {
    switch (ev.kind) {
      case ROLLTUI_MENU_EVENT_NONE: return true;
      case ROLLTUI_MENU_EVENT_CLOSED: close_popup("menu"); return true;
      case ROLLTUI_MENU_EVENT_CHOOSE:
        if (ev.id == "theme") { theme_arg.clear(); ThemePresetReport rep; if (!rolltui_theme_store_load(store, ev.value.data(), ev.value.size(), &rep, persist ? 1 : 0)) hint.assign(rep.error); else hint = rep.summary(); }
        else if (ev.id == "layout") { layout_arg.clear(); LayoutPresetReport rep; if (!rolltui_layout_store_load(lstore, ev.value.data(), ev.value.size(), &rep, persist ? 1 : 0)) hint.assign(rep.error); else hint = rep.summary(); }
        else if (ev.id == "depth") depth = rolltui_detect_color_depth(nullptr, nullptr, ev.value.c_str());
        else if (ev.id == "mode") { mode_flag.reset(); if (store) rolltui_theme_store_set_mode(store, ev.value.data(), ev.value.size(), persist ? 1 : 0); }
        return true;
      case ROLLTUI_MENU_EVENT_TOGGLE:
        if (ev.id == "ambiguous") ambiguous = ev.checked != 0;
        return true;
      case ROLLTUI_MENU_EVENT_ACTIVATE:
        close_popup("menu");
        if (ev.id == "reload") load_fixture();
        else if (ev.id == "open") rolltui_window_stack_action_popup(stack, &effective_layout(), "app.filepicker", 14);
        else if (ev.id == "save_as") open_save_dialog();
        else if (ev.id == "help") toggle_help();
        else if (ev.id == "editor") toggle_editor();
        else if (ev.id == "quit") return false;
        return true;
      case ROLLTUI_MENU_EVENT_INPUT: return true;
    }
    return true;
  }

  // --theme X / --layout X fill this run's working copy without writing it; a file is
  // re-read on mtime change.
  bool load_theme_arg() {
    if (theme_arg.empty()) return true;
    ThemePresetReport rep;
    theme_mtime = mtime_of(theme_arg);
    if (!rolltui_theme_store_load(store, theme_arg.data(), theme_arg.size(), &rep, /*persist=*/0)) { theme_note.assign(rep.error); return false; }
    theme_note = rep.summary();
    if (rep.colours.missing_roles_n != 0) theme_note = rolltui::format("%zu roles missing (inherit text)", static_cast<std::size_t>(rep.colours.missing_roles_n));
    return true;
  }
  void maybe_reload_theme() {
    if (theme_arg.empty() || rolltui_theme_is_shipped(ctx, theme_arg.data(), theme_arg.size())) return;
    if (!StrView(theme_arg).contains('/') && !StrView(theme_arg).contains(".json")) return;
    long m = mtime_of(theme_arg);
    if (m != theme_mtime) load_theme_arg();
  }
  bool load_layout_arg() {
    if (!layout_arg.empty()) {
      LayoutPresetReport rep;
      layout_mtime = mtime_of(layout_arg);
      if (!rolltui_layout_store_load(lstore, layout_arg.data(), layout_arg.size(), &rep, /*persist=*/0)) layout_note.assign(rep.error);
      else {
        layout_note.clear();
        if (rep.layout.unknown_keys_n != 0) rolltui::appendf(layout_note, "unknown: %s; ", rep.layout.unknown_keys[0].c_str());
        if (rep.layout.bad_values_n != 0) rolltui::appendf(layout_note, "bad: %s; ", rep.layout.bad_values[0].c_str());
      }
    }
    sync_look();
    apply_layout();
    return layout_note.empty();
  }
  void maybe_reload_layout() {
    if (layout_arg.empty() || rolltui_layout_is_shipped(ctx, layout_arg.data(), layout_arg.size())) return;
    if (!StrView(layout_arg).contains('/') && !StrView(layout_arg).contains(".json")) return;
    long m = mtime_of(layout_arg);
    if (m != layout_mtime) load_layout_arg();
  }
  // Re-resolves the look from the working copy when the store changed; the editor's
  // preview wins while it is open.
  void sync_look() {
    if (store && rolltui_theme_store_version(store) != store_seen) {
      store_seen = rolltui_theme_store_version(store);
      const RolltuiThemePresetValue* working = rolltui_theme_store_working(store);
      mode = mode_flag ? *mode_flag : mode_from_setting(working->mode).value_or(detected_mode);
      RolltuiThemeReport rep{};
      RolltuiStyle new_styles[ROLLTUI_ROLE_COUNT]{};
      RolltuiStr new_name{};
      RolltuiEffectMap* new_effects = rolltui_theme_load(working->colours, mode, rolltui_theme_default_vocab(), new_styles, &new_name, &rep);
      // The thumb's shape is the theme's, the same as its colour.
      { RolltuiScrollbarGlyphs g; rolltui_theme_scrollbar_glyphs(working->colours, &g);
        rolltui_context_set_scrollbar_glyphs(ctx, &g); }
      if (new_effects) {
        std::copy(std::begin(new_styles), std::end(new_styles), resolved_styles);
        resolved_name.assign(new_name);
        rolltui_effect_map_free(effects_map);
        effects_map = new_effects;
      } else {
        theme_note = RolltuiStr("colours unusable: ") + rep.error;
        RolltuiEffectMap* fallback = rolltui_theme_builtin_fill("default-dark", 12, resolved_styles, ROLLTUI_ROLE_COUNT);
        resolved_name = "default-dark";
        rolltui_effect_map_free(effects_map);
        effects_map = fallback;
      }
      rolltui_str_free(&new_name);
      rolltui_theme_report_release(&rep);
    }
    if (lstore && rolltui_layout_store_version(lstore) != lstore_seen) {
      lstore_seen = rolltui_layout_store_version(lstore);
      const RolltuiLayout* working_layout = rolltui_layout_store_working(lstore);
      if (!(layout == *working_layout)) { layout = working_layout->clone(); apply_layout(); }
    }
    // teditor.current() is a BORROW of just the styles now (the theme editor's own edit
    // buffer carries no name/effects) — this frame's own name/effects stay whatever the
    // preset store last resolved, which only the styles-driven role rendering reads.
    if (editor_mode == EditorMode::Theme) std::copy_n(teditor.current(), kRoleCount, theme_styles);
    else std::copy(std::begin(resolved_styles), std::end(resolved_styles), theme_styles);
    if (editor_mode == EditorMode::Layout && !(layout == leditor.current())) { layout = leditor.current().clone(); apply_layout(); }
    if (bstore && rolltui_bindings_store_version(bstore) != bstore_seen) {
      bstore_seen = rolltui_bindings_store_version(bstore);
      rolltui_bindings_free(bindings);
      bindings = rolltui_bindings_clone(rolltui_bindings_store_working(bstore));
      declare_actions();
    }
    if (editor_mode == EditorMode::Keys) {
      rolltui_bindings_free(bindings);
      bindings = rolltui_bindings_clone(keditor.current());
      declare_actions();
    }
  }
  bool load_bindings_arg() {
    if (bindings_arg.empty()) return true;
    BindingsPresetReport rep;
    if (!rolltui_bindings_store_load(bstore, bindings_arg.data(), bindings_arg.size(), &rep, /*persist=*/0)) { hint.assign(rep.error); return false; }
    if (!rep.clean()) hint = RolltuiStr("bindings: ") + rep.summary();
    return true;
  }
  // ---- the theme editor (milestone 14) ----
  // WIDE ENOUGH FOR WHAT THE EDITORS SAY. Narrowing this was tried and reverted: the keys
  // editor's status names the action a chord was taken FROM, and at 42 columns that sentence
  // is cut off — a panel that saves eight columns by hiding the reason for an edit is a bad
  // trade. Which side it sits on is what keeps the design visible, not how wide it is.
  static constexpr int kEditorPanelW = 50;
  // WHICH SIDE, and it is a question because the panel floats over the design. A node under
  // the panel is a node you are editing blind, which is the one thing a design tool may not
  // do — so the panel moves rather than the selection being lost behind it.
  static RolltuiLayer editor_popup(const char* title, bool left = false, const char* content = "editor") {
    RolltuiLayer l;
    l.id = "editor";
    l.placement = {left ? RolltuiDim::abs(0) : RolltuiDim::rel(1), RolltuiDim::abs(0),
                   RolltuiDim::abs(kEditorPanelW), RolltuiDim::rel(1),
                   left ? rolltui::Anchor::TopLeft : rolltui::Anchor::TopRight,
                   true, 0, RolltuiDim::abs(24), RolltuiDim::abs(6), {}, {}};
    l.modal = false;
    // The window id stays `editor` so focus and routing are unchanged; the CONTENT is what
    // decides who draws. The theme editor names the library's `theme` kind rather than the
    // studio's own, which is how there came to be one implementation instead of two.
    RolltuiLayoutNode n = RolltuiLayoutNode::window_id("editor", content);
    n.border = rolltui::Border::Single;
    n.title = title;
    n.focusable = true;
    n.background = to_role(ROLLTUI_ROLE_PANEL_BACKGROUND);
    l.root = (n).clone();
    l.focus = "editor";
    return l;
  }
  static RolltuiLayer report_popup() {
    RolltuiLayer l;
    l.id = "report";
    l.placement = {RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::rel(0.8), RolltuiDim::rel(0.85),
                   rolltui::Anchor::Center, true, 0, RolltuiDim::abs(30), RolltuiDim::abs(5), {}, {}};
    l.modal = true;
    RolltuiLayoutNode n = RolltuiLayoutNode::window_id("report", "report");
    n.border = rolltui::Border::Rounded;
    n.title = "report";
    n.focusable = true;
    n.background = to_role(ROLLTUI_ROLE_PANEL_BACKGROUND);
    l.root = (n).clone();
    return l;
  }
  // THE SAVE DIALOG: a name over a folder, the shape paint's is — a second picker instance
  // (`filepicker:save`) beside the open dialog's, so each keeps its own place. A question a
  // person answers, so it does not dismiss on a click outside.
  static RolltuiLayer save_popup() {
    RolltuiLayer l;
    l.id = "save";
    l.placement = {RolltuiDim::rel(1), RolltuiDim::abs(0), RolltuiDim::abs(kEditorPanelW), RolltuiDim::rel(1),
                   rolltui::Anchor::TopRight, true, 0, RolltuiDim::abs(30), RolltuiDim::abs(8), {}, {}};
    l.modal = true;
    l.dismiss = 0;
    RolltuiLayoutNode col = RolltuiLayoutNode::column();
    RolltuiLayoutNode name = RolltuiLayoutNode::window_id("save_name", "input:save_name", RolltuiSplitSize::fixed(RolltuiDim::abs(3)));
    name.border = rolltui::Border::Rounded;
    name.title = "save as: the name";
    name.focusable = true;
    name.background = to_role(ROLLTUI_ROLE_PANEL_BACKGROUND);
    RolltuiLayoutNode folder = RolltuiLayoutNode::window_id("save_folder", "filepicker:save");
    folder.border = rolltui::Border::Rounded;
    folder.title = "in the folder";
    folder.focusable = true;
    folder.background = to_role(ROLLTUI_ROLE_PANEL_BACKGROUND);
    col.children.push_back(std::move(name));
    col.children.push_back(std::move(folder));
    l.root = std::move(col);
    l.focus = "save_name";
    return l;
  }
  // THE FILE SAVE WRITES THE PRESET BEING EDITED — the open editor's working copy through its
  // domain's own serialiser, so the file is what the store would have written by name — to a
  // folder and a name a person chose. With no editor open there is nothing to write, and the
  // status line says so rather than guessing.
  bool preset_text(RolltuiStr& out, RolltuiStr& what) {
    // The preset name is NEVER written over a value's own "name" field where it has one (a
    // layout carries its own; a save that silently renamed what was saved would break
    // `modified()`) — theme and bindings have no such field, so the label IS the name, its
    // "(modified)" suffix trimmed off.
    switch (editor_mode) {
      case EditorMode::Theme: {
        what = "theme";
        const RolltuiThemePresetValue* w = store ? rolltui_theme_store_working(store) : nullptr;
        if (!w) return false;
        RolltuiStr label;
        rolltui_theme_store_label(store, &label);
        StrView name = label;
        if (const std::size_t sp = name.find(" ("); sp != StrView::npos) name = name.first(sp);
        RolltuiJsonValue* tree = rolltui_theme_preset_to_json(rolltui_json_clone(w->colours), w->mode.p, w->mode.n,
                                                              w->depth.p, w->depth.n, name.data(), name.size());
        rolltui_json_dump(tree, 2, &out);
        rolltui_json_free(tree);
        return true;
      }
      case EditorMode::Layout: {
        what = "layout";
        const RolltuiLayout* l = lstore ? rolltui_layout_store_working(lstore) : nullptr;
        if (!l) return false;
        rolltui_layout_to_json_text(l->name.p, l->name.n, l->min_width, l->min_height, l->actions.v, l->actions.n,
                                    &l->base, l->popups.v, l->popups.n, rolltui_layout_default_hooks(), &out);
        return true;
      }
      case EditorMode::Keys: {
        what = "key bindings";
        const RolltuiBindings* w = bstore ? rolltui_bindings_store_working(bstore) : nullptr;
        if (!w) return false;
        RolltuiStr label;
        rolltui_bindings_store_label(bstore, &label);
        StrView name = label;
        if (const std::size_t sp = name.find(" ("); sp != StrView::npos) name = name.first(sp);
        rolltui_bindings_dump_json(w, name.data(), name.size(), &out);
        return true;
      }
      case EditorMode::Menu: what = "menu"; out = meditor.to_json(); return true;
      case EditorMode::None: return false;
    }
    return false;
  }
  void open_save_dialog() {
    RolltuiStr text, what;
    if (!preset_text(text, what)) { note = "open an editor first: a file save writes the preset being edited"; return; }
    if (rolltui_window_stack_has_popup(stack, "save", 4)) return;
    const std::size_t slash = StrView(fixture_path).rfind('/');
    const StrView at = fixture_path.empty() ? StrView(".") : StrView(fixture_path).first(slash == StrView::npos ? 0 : slash + 1);
    rolltui_windows_set_picker_dir(windows, "filepicker:save", 15, at.data(), at.size());
    RolltuiLayer popup = save_popup();
    rolltui_window_stack_push(stack, &popup);
    note = RolltuiStr("save the ") + what + ": a name, Enter";
  }
  void save_preset_file(StrView name) {
    RolltuiStr text, what;
    if (name.empty()) { note = "a name, then Enter"; return; }
    if (!preset_text(text, what)) { note = "nothing being edited to save"; close_popup("save"); return; }
    RolltuiStr dir;
    if (!rolltui_windows_picker_dir(windows, "filepicker:save", 15, &dir)) { note = "no folder chosen"; return; }
    RolltuiStr path = rolltui::own(dir == "/" ? StrView() : StrView(dir));
    path += "/";
    path += name;
    FILE* out = std::fopen(path.c_str(), "wb");
    const bool wrote = out && std::fwrite(text.data(), 1, text.size(), out) == text.size();
    if (out) std::fclose(out);
    if (wrote) note = RolltuiStr("wrote the ") + what + " to " + path;
    else note = RolltuiStr("could not write ") + path;
    if (wrote) close_popup("save");
  }

  static RolltuiLayer confirm_popup() {
    RolltuiLayer l;
    l.id = "confirm";
    l.placement = {RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(5),
                   rolltui::Anchor::Center, true, 0, RolltuiDim::abs(20), {}, RolltuiDim::abs(70), {}};
    l.modal = true;
    RolltuiLayoutNode n = RolltuiLayoutNode::window_id("confirm", "confirm");
    n.border = rolltui::Border::Rounded;
    n.title = "confirm";
    n.focusable = true;
    n.background = to_role(ROLLTUI_ROLE_PANEL_BACKGROUND);
    l.root = (n).clone();
    return l;
  }
  void close_editor() {
    if (!editor_open) return;
    close_popup("editor");
    editor_open = false;
    editor_mode = EditorMode::None;
    store_seen = 0;  // re-resolve the look from the working copy
    sync_look();
  }
  void toggle_editor() {
    if (editor_mode == EditorMode::Theme) { close_editor(); return; }
    close_editor();
    RolltuiThemeReport rep{};
    { const RolltuiThemePresetValue* wc = rolltui_theme_store_working(store); teditor.load(wc->colours, &rep); }
    rolltui_theme_report_release(&rep);
    StrVec names;
    RolltuiPresetList pl;
    rolltui_theme_store_list(store, &pl);
    for (const RolltuiPresetInfo& p : pl) names.add(StrView(p.name));
    RolltuiStrList sn;
    rolltui_theme_shipped_names(ctx, &sn);
    teditor.set_presets(names);
    teditor.set_shipped(shipped_names_of(sn), true);  // may_write_shipped: this App's stores always may
    teditor.set_mode(mode == ROLLTUI_MODE_DARK ? ROLLTUI_MODE_DARK : ROLLTUI_MODE_LIGHT);
    editor_open = true;
    editor_mode = EditorMode::Theme;
    { RolltuiLayer popup = editor_popup("theme editor", false, "theme"); rolltui_window_stack_push(stack, &popup); }
    // ONE EDITOR, and the window table owns it. `set_theme_store` also CREATES the widget if this
    // screen has none yet, so the editor exists to be adopted before the first draw.
    rolltui_windows_set_theme_store(windows, "theme", 5, store, 0);
    if (RolltuiThemeEditor* borrowed = rolltui_windows_theme_editor(windows, "theme", 5)) teditor.adopt(borrowed);
    push_theme_hint();
    sync_look();
  }
  void toggle_keys_editor() {
    if (editor_mode == EditorMode::Keys) { close_editor(); return; }
    close_editor();
    // The LIVE table, not the store's working copy: an action is editable here only if
    // something declared it. What this hands over is what the studio is actually running.
    keditor.load(bindings);
    StrVec names;
    RolltuiPresetList pl;
    rolltui_bindings_store_list(bstore, &pl);
    for (const RolltuiPresetInfo& p : pl) names.add(StrView(p.name));
    RolltuiStrList sn;
    rolltui_bindings_shipped_names(ctx, &sn);
    keditor.set_presets(names);
    keditor.set_shipped(shipped_names_of(sn), true);  // may_write_shipped: this App's stores always may
    editor_open = true;
    editor_mode = EditorMode::Keys;
    { RolltuiLayer popup = editor_popup("keys editor"); rolltui_window_stack_push(stack, &popup); }
    sync_look();
  }
  // The menus a `menu:` window could resolve, and the actions this binary knows — both HINTS.
  void toggle_menu_editor() {
    // The second of two independent guards: an unmounted editor is unreachable by NAME as
    // well as by chord, so no route (the F2 menu, the palette, a bindings file that binds
    // `editor.menu` anyway) can open what this binary says it does not have.
    if (!menu_editor_mounted()) { hint = "this build has no menu editor"; return; }
    if (editor_mode == EditorMode::Menu) { close_editor(); return; }
    close_editor();
    meditor.set_menus(menu_names());
    StrVec actions;
    for (std::size_t i = 0; i < rolltui_bindings_row_count(bindings); ++i) {
      std::size_t n = 0;
      const char* p = rolltui_bindings_row_at(bindings, i, &n);
      actions.add(StrView(p, n));
    }
    meditor.set_actions(actions);
    editor_open = true;
    editor_mode = EditorMode::Menu;
    { RolltuiLayer popup = editor_popup("menu editor"); rolltui_window_stack_push(stack, &popup); }
    sync_look();
  }
  // A menu is the one screen file with no PRESET DOMAIN behind it — `menu:<name>` resolves
  // against `<presets>/menus/<name>.json`, the host's embedded table and the library's shipped
  // ones (rolltui.h), and none of those is a store. So this writes the file itself, which is
  // what the layout save-as did before it had a store to go through. There is no working copy
  // to keep in step and no origin to record, so there is nothing a store would have added.
  void menu_outcome(const MenuEditor::Outcome& o) {
    using K = MenuEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: case K::Committed: break;
      case K::SaveAs: {
        if (o.value.empty()) { hint = "a menu file needs a name"; break; }
        std::size_t dir_len = 0;
        const char* dir_p = rolltui_windows_dir(windows, &dir_len);
        const RolltuiStr dir = rolltui::own(StrView(dir_p, dir_len));
        if (dir.empty()) { hint = "no preset directory to write a menu into"; break; }
        std::error_code ec;
        std::filesystem::create_directories((dir + "/menus").c_str(), ec);
        const RolltuiStr path = dir + "/menus/" + o.value + ".json";
        FILE* out = std::fopen(path.c_str(), "wb");
        if (!out) { hint = RolltuiStr("cannot write ") + path; break; }
        RolltuiStr json = meditor.to_json();
        json += '\n';
        std::fwrite(json.data(), 1, json.size(), out);
        std::fclose(out);
        hint = RolltuiStr("saved menu file ") + path;
        meditor.set_menus(menu_names());  // it resolves now, so the Load list and the hints say so
        break;
      }
      case K::LoadMenu: {
        const RolltuiStr text = menu_json(o.value);
        if (text.empty()) { hint = RolltuiStr("no menu file '") + o.value + "'"; break; }
        const char* json = text.data();
        const std::size_t json_len = text.size();
        MenuItem root;
        RolltuiMenuLoadReport rep{};
        if (rolltui_menu_parse_json(json, json_len, &root, &rep)) {
          meditor.load(root);
          if (rolltui_menu_load_report_clean(&rep)) hint = RolltuiStr("loaded menu ") + o.value;
          else hint = RolltuiStr("loaded '") + o.value + "' with problems";
        } else {
          hint = RolltuiStr("cannot read menu '") + o.value + "'";
        }
        rolltui_menu_load_report_release(&rep);
        break;
      }
      case K::ResetLoaded:
        ask("Reset the menu to what was loaded? (y/n)", [this] {
          meditor.replace(meditor.committed().clone());
          hint = "reset";
        });
        break;
      case K::Closed:
        close_editor();
        break;
    }
  }
  void draw_menu_editor(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    RolltuiRect r{};
    rolltui_content_rect(&rn, &r);
    RolltuiRect m = r;
    m.h = r.h > 3 ? r.h - 3 : 0;
    RolltuiMenuOptions mo{};
    mo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_menu_set_options_struct(meditor.menu(), &mo);
    rolltui_menu_layout(meditor.menu(), m);
    if (m.h > 0) draw_raw_menu(meditor.menu(), f, rn.focused != 0);
    int y = r.y + m.h;
    const RolltuiStyle label = style(ROLLTUI_ROLE_LABEL), value = style(ROLLTUI_ROLE_VALUE);
    if (const RolltuiStr line = meditor.selection_line(); !line.empty() && y < r.y + r.h)
      put_text(f, r.x, y++, line, label, r.w);
    if (y < r.y + r.h) { meditor.status_line(editor_status); put_text(f, r.x, y++, editor_status, value, r.w); }
    if (y < r.y + r.h && !hint.empty()) put_text(f, r.x, y++, hint, style(ROLLTUI_ROLE_WARNING), r.w);
  }
  void keys_outcome(const KeysEditor::Outcome& o) {
    using K = KeysEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        rolltui_bindings_store_set_working(bstore, rolltui_bindings_clone(keditor.committed()), persist ? 1 : 0);
        break;
      case K::SaveAs: {
        RolltuiStr err;
        const int r = rolltui_bindings_store_save_as(bstore, o.value.data(), o.value.size(), (pending_save == o.value) ? 1 : 0, &err);
        if (r == ROLLTUI_SAVE_EXISTS_ASK) { pending_save.assign(o.value); hint = RolltuiStr("bindings preset '") + o.value + "' exists; Enter the same name again to overwrite"; }
        else {
          pending_save.clear();
          if (r == ROLLTUI_SAVE_SAVED) hint = RolltuiStr("saved bindings preset '") + o.value + "'";
          else hint = std::move(err);
        }
        if (r == ROLLTUI_SAVE_SAVED) { StrVec names; RolltuiPresetList pl; rolltui_bindings_store_list(bstore, &pl); for (const RolltuiPresetInfo& p : pl) names.add(StrView(p.name)); keditor.set_presets(names); }
        break;
      }
      case K::WriteShipped:
        confirm_arg.assign(o.value);
        ask(RolltuiStr("Write the SHIPPED bindings preset '") + o.value + "' into " + bindings_shipped_dir + "? (y/n)", [this] {
          RolltuiStr err;
          if (rolltui_bindings_store_save_as(bstore, confirm_arg.data(), confirm_arg.size(), 1, &err) == ROLLTUI_SAVE_SAVED) hint = RolltuiStr("wrote shipped bindings preset '") + confirm_arg + "' (rebuild to embed it)";
          else hint = std::move(err);
        });
        break;
      case K::LoadPreset: {
        BindingsPresetReport rep;
        if (!rolltui_bindings_store_load(bstore, o.value.data(), o.value.size(), &rep, persist ? 1 : 0)) hint.assign(rep.error);
        else {
          keditor.load(rolltui_bindings_store_working(bstore));
          if (rep.clean()) hint = RolltuiStr("loaded bindings '") + o.value + "'";
          else hint = RolltuiStr("loaded '") + o.value + "' with problems: " + rep.summary();
        }
        break;
      }
      case K::ResetLoaded: {
        std::size_t on = 0; const char* op = rolltui_bindings_store_origin(bstore, &on);
        ask(RolltuiStr("Reset every binding to the preset '") + StrView(op, on) + "'? (y/n)", [this] {
          BindingsPresetReport rep;
          std::size_t on2 = 0; const char* op2 = rolltui_bindings_store_origin(bstore, &on2);
          if (RolltuiBindings* b = rolltui_bindings_store_get(bstore, op2, on2, &rep)) { keditor.replace(b); keys_outcome({K::Committed, {}}); hint = "reset (undoable)"; }
          else hint.assign(rep.error);
        });
        break;
      }
      case K::Closed:
        close_editor();
        break;
    }
  }
  void draw_keys_editor(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    RolltuiRect r = content_rect(rn);
    if (r.w <= 0 || r.h <= 0) return;
    const int box = std::min(3, r.h);
    RolltuiRect m = r;
    m.h = r.h - box;
    RolltuiMenuOptions mo{};
    mo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_menu_set_options_struct(keditor.menu(), &mo);
    rolltui_menu_layout(keditor.menu(), m);
    if (m.h > 0) draw_raw_menu(keditor.menu(), f, rn.focused != 0);
    int y = r.y + m.h;
    const RolltuiStyle label = style(ROLLTUI_ROLE_LABEL), value = style(ROLLTUI_ROLE_VALUE);
    if (y < r.y + r.h) {
      editor_line.assign("preset: ");
      rolltui_bindings_store_label(bstore, &keys_label_str);
      editor_line += keys_label_str;
      editor_line += " \xC2\xB7 Enter on an action, then press the chord";
      put_text(f, r.x, y++, editor_line, label, r.w);
    }
    if (y < r.y + r.h) { keditor.status_line(editor_status); put_text(f, r.x, y++, editor_status, keditor.capturing() ? style(ROLLTUI_ROLE_WARNING) : value, r.w); }
    if (y < r.y + r.h && !hint.empty()) put_text(f, r.x, y++, hint, style(ROLLTUI_ROLE_WARNING), r.w);
  }
  void toggle_layout_editor() {
    if (editor_mode == EditorMode::Layout) { close_editor(); return; }
    close_editor();
    leditor.load(*rolltui_layout_store_working(lstore));
    StrVec names;
    RolltuiPresetList pl;
    rolltui_layout_store_list(lstore, &pl);
    for (const RolltuiPresetInfo& p : pl) names.add(StrView(p.name));  // shipped first, then the user's
    leditor.set_layouts(names);
    // what this list is has changed, and the change is the milestone. It used to be
    // the set of contents the author was ALLOWED to name — the target app's under `--app`, the
    // studio's own without one. It is now what this binary can PREVIEW, offered as a hint under
    // a field that accepts anything: a designer names what the screen needs, and a name this
    // tool cannot build previews as a labelled placeholder instead of being refused.
    leditor.set_sources({"transcript:session", "rows:status", "input:prompt", "text:pane", "editor"});
    StrVec kinds;
    for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) {
      std::size_t n = 0;
      const char* p = rolltui_widget_kind_name(ctx, i, &n);
      kinds.add(StrView(p, n));
    }
    for (const char* mine : {"editor", "confirm", "report"}) kinds.add(StrView(mine));
    leditor.set_kinds(kinds);
    // The menu files that RESOLVE right now — the preset directory the target app and this
    // tool share, this binary's own embedded menus, and the library's shipped ones. A name
    // outside all three is still typeable, because the target may embed a menu of its own.
    leditor.set_menus(menu_names());
    editor_open = true;
    editor_mode = EditorMode::Layout;
    { RolltuiLayer popup = editor_popup("layout editor", editor_left); rolltui_window_stack_push(stack, &popup); }
    reposition_editor();
    sync_look();
  }

  // The panel's rect for a given side, in the same coordinates the base tree resolves into.
  RolltuiRect editor_rect(bool left) const {
    const RolltuiRect a = layout_area();
    const int pw = std::min(kEditorPanelW, a.w);
    return {left ? a.x : a.x + a.w - pw, a.y, pw, a.h};
  }
  // MOVE THE PANEL, NEVER THE SELECTION. Called whenever the selected node changes: if the
  // node sits under the panel and the other side is clear, the panel goes there. A node too
  // wide to escape either side stays where it is — flipping forever would be worse than
  // being covered, and the tree view above still says where the selection is.
  void reposition_editor() {
    if (editor_mode != EditorMode::Layout || !rolltui_window_stack_has_popup(stack, "editor", 6)) return;
    std::vector<RolltuiResolvedNode> nodes;
    rolltui_resolve_tree(&rolltui_window_stack_base(stack)->root, layout_area(), layout_area(), 0, collect_resolved,
                         &nodes);
    RolltuiRect sel{};
    bool found = false;
    for (const RolltuiResolvedNode& rn : nodes)
      if (rn.node->id == leditor.selected()) { sel = rn.outer; found = true; break; }
    if (!found) return;
    if (!sel.intersect(editor_rect(editor_left)).empty()) {
      if (sel.intersect(editor_rect(!editor_left)).empty()) {
        editor_left = !editor_left;
        close_popup("editor");
        RolltuiLayer popup = editor_popup("layout editor", editor_left);
        rolltui_window_stack_push(stack, &popup);
      }
    }
  }
  // Every menu name a `menu:` window could resolve right now — the union of the preset
  // directory's menus/*.json, the host's own embedded ones, and the library's shipped ones,
  // deduplicated and sorted.
  StrVec menu_names() const {
    StrVec out;
    auto add = [&out](StrView name) {
      if (!out.contains(name)) out.add(name);
    };
    std::size_t dir_len = 0;
    const char* dir_p = rolltui_windows_dir(windows, &dir_len);
    if (const StrView d(dir_p, dir_len); !d.empty()) {
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator((rolltui::own(d) + "/menus").c_str(), ec))
        if (e.path().extension() == ".json") add(e.path().stem().string());
    }
    for (std::size_t i = 0; i < rolltui_windows_host_menu_count(windows); ++i) {
      std::size_t n = 0;
      const char* p = rolltui_windows_host_menu_name_at(windows, i, &n);
      add(StrView(p, n));
    }
    // the library's own shipped menus (menus/*.json under presets/): the domain the
    // preset store's own directory listing does not otherwise reach.
    for (std::size_t i = 0; i < rolltui_kMenuCount; ++i) add(rolltui_kMenus[i].name);
    std::sort(out.begin(), out.end(), [](const RolltuiStr& a, const RolltuiStr& b) { return StrView(a) < StrView(b); });
    return out;
  }
  // One menu file's TEXT, through the same three rungs `menu:<name>` itself resolves through
  // and in the same order (rolltui.h): the preset directory, this binary's embedded menus, the
  // library's shipped ones. `menu_names()` beside this lists exactly these three.
  RolltuiStr menu_json(StrView name) const {
    std::size_t dir_len = 0;
    const char* dir_p = rolltui_windows_dir(windows, &dir_len);
    if (const StrView d(dir_p, dir_len); !d.empty()) {
      bool ok = false;
      RolltuiStr text = read_file(rolltui::own(d) + "/menus/" + name + ".json", ok);
      if (ok) return text;
    }
    std::size_t n = 0;
    if (const char* p = rolltui_windows_host_menu(windows, name.data(), name.size(), &n); p) return rolltui::own(StrView(p, n));
    for (std::size_t i = 0; i < rolltui_kMenuCount; ++i)
      if (name == rolltui_kMenus[i].name) return rolltui::own(rolltui_kMenus[i].text);
    return {};
  }
  void layout_outcome(const LayoutEditor::Outcome& o) {
    reposition_editor();
    using K = LayoutEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        rolltui_layout_store_set_working(lstore, rolltui_layout_clone(&leditor.committed()), persist ? 1 : 0);
        break;
      case K::SaveAs: {
        // Through the Layout store, as the theme and keys editors already save: the store learns
        // the new origin (its label reads the new name, the working copy records it, as a manual
        // save does even under --frame) and the path rule stays the library's. Hand-building
        // the path and writing the file directly leaves the store never learning it happened.
        if (o.value.empty()) { hint = "a layout file needs a name"; break; }
        RolltuiLayout l = (leditor.committed()).clone();
        l.name.assign(o.value);
        rolltui_layout_store_set_working(lstore, rolltui_layout_clone(&l), persist ? 1 : 0);
        RolltuiStr err;
        const int r = rolltui_layout_store_save_as(lstore, o.value.data(), o.value.size(), (pending_save == o.value) ? 1 : 0, &err);
        if (r == ROLLTUI_SAVE_EXISTS_ASK) { pending_save.assign(o.value); hint = RolltuiStr("layout '") + o.value + "' exists; Enter the same name again to overwrite"; }
        else if (r != ROLLTUI_SAVE_SAVED) { pending_save.clear(); hint = std::move(err); }
        else {
          pending_save.clear();
          RolltuiStr path;
          rolltui_layout_store_path(lstore, o.value.data(), o.value.size(), &path);
          hint = RolltuiStr("saved layout file ") + path;
          StrVec names;
          RolltuiPresetList pl;
          rolltui_layout_store_list(lstore, &pl);
          for (const RolltuiPresetInfo& p : pl) names.add(StrView(p.name));
          leditor.set_layouts(names);
        }
        break;
      }
      case K::LoadLayout: {
        LayoutPresetReport rep;
        if (!rolltui_layout_store_load(lstore, o.value.data(), o.value.size(), &rep, persist ? 1 : 0)) hint.assign(rep.error);
        else {
          leditor.replace(rolltui_layout_store_working(lstore)->clone());
          RolltuiStr label; rolltui_layout_store_label(lstore, &label);
          hint = RolltuiStr("loaded layout ") + label;
        }
        break;
      }
      case K::ResetLoaded: {
        RolltuiStr label; rolltui_layout_store_label(lstore, &label);
        ask(RolltuiStr("Reset the layout to the working copy's '") + label + "'? (y/n)", [this] {
          leditor.replace(rolltui_layout_store_working(lstore)->clone());
          hint = "reset (undoable)";
        });
        break;
      }
      case K::Closed:
        close_editor();
        break;
    }
  }
  // The window under a pointer in the base layer, and the seam a press may be on: the
  // right/bottom edge cell of a child that has a following sibling in its Row/Column.
  std::optional<RolltuiStr> window_at(int x, int y) {
    std::optional<RolltuiStr> best;
    std::vector<RolltuiResolvedNode> nodes;
    rolltui_resolve_tree(&rolltui_window_stack_base(stack)->root, layout_area(), layout_area(), 0, collect_resolved, &nodes);
    for (const RolltuiResolvedNode& rn : nodes)
      if (rn.node->is_window() && rn.outer.contains(x, y)) best = rolltui::own(rn.node->id);
    return best;
  }
  // A seam: the two edge cells where two visible siblings meet (each owns one). The node that takes the new
  // size is the FIXED-size one when the other fills (dragging the fill would leave a
  // gap the fixed sibling never closes); otherwise the one before the seam.
  struct Seam { RolltuiStr id; bool after; bool horizontal; };
  std::optional<Seam> seam_at(int x, int y) {
    std::vector<RolltuiResolvedNode> nodes;
    rolltui_resolve_tree(&rolltui_window_stack_base(stack)->root, layout_area(), layout_area(), 0, collect_resolved, &nodes);
    for (const RolltuiResolvedNode& rn : nodes) {
      if (rn.node->is_window()) continue;
      const bool horizontal = rn.node->kind == RolltuiLayoutNode::Kind::Row;
      for (std::size_t i = 0; i + 1 < rn.node->children.size(); ++i) {
        const RolltuiLayoutNode& child = rn.node->children[i];
        const RolltuiLayoutNode& next = rn.node->children[i + 1];
        if (!child.visible || !next.visible) continue;
        for (const RolltuiResolvedNode& c : nodes) {
          if (c.node != &child) continue;
          const int edge = horizontal ? c.outer.x + c.outer.w - 1 : c.outer.y + c.outer.h - 1;
          const bool on = horizontal ? (x == edge || x == edge + 1) && y >= c.outer.y && y < c.outer.y + c.outer.h
                                     : (y == edge || y == edge + 1) && x >= c.outer.x && x < c.outer.x + c.outer.w;
          if (!on) continue;
          const bool size_after = child.size.fill && !next.size.fill;
          return Seam{rolltui::own(size_after ? next.id : child.id), size_after, horizontal};
        }
      }
    }
    return std::nullopt;
  }
  std::optional<Seam> drag_seam;
  // The layout editor's selection, drawn from the slot callback. It RECOLOURS the border
  // ring rather than redrawing it: a highlight's whole difference from the border it
  // highlights is its COLOUR, so tinting is both the smaller act and the correct one.
  void draw_selection(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    if (editor_mode != EditorMode::Layout || rn.layer != 0 || !(rn.node->id == leditor.selected())) return;
    if (rn.node->border == rolltui::Border::None) {
      tint(f, rn.outer.intersect(layout_area()), style(ROLLTUI_ROLE_SELECTION));
      return;
    }
    RolltuiStyle hl{};
    hl.fg = style(ROLLTUI_ROLE_BORDER_ACTIVE).fg;  // fg only: the window keeps its own ground
    const RolltuiRect o = rn.outer;
    for (const RolltuiRect& edge : {RolltuiRect{o.x, o.y, o.w, 1}, RolltuiRect{o.x, o.y + o.h - 1, o.w, 1},
                                    RolltuiRect{o.x, o.y, 1, o.h}, RolltuiRect{o.x + o.w - 1, o.y, 1, o.h}})
      tint(f, edge.intersect(layout_area()), hl);
  }
  void ask(StrView text, std::function<void()> action) {
    confirm_text = rolltui::own(text);
    confirm_action = std::move(action);
    RolltuiLayer popup = confirm_popup();
    rolltui_window_stack_push(stack, &popup);
  }
  void editor_outcome(const ThemeEditor::Outcome& o) {
    using K = ThemeEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed: {
        std::size_t on = 0; const char* op = rolltui_theme_store_origin(store, &on);
        RolltuiJsonValue* colours = teditor.colours_json(StrView(op, on));
        rolltui_theme_store_edit(store, [](RolltuiThemePresetValue* v, void* c) {
          rolltui_json_free(v->colours);
          v->colours = static_cast<RolltuiJsonValue*>(c);
        }, colours, persist ? 1 : 0);
        break;
      }
      case K::SaveAs: {
        RolltuiStr err;
        const int r = rolltui_theme_store_save_as(store, o.value.data(), o.value.size(), (pending_save == o.value) ? 1 : 0, &err);
        if (r == ROLLTUI_SAVE_EXISTS_ASK) { pending_save.assign(o.value); hint = RolltuiStr("preset '") + o.value + "' exists; Enter the same name again to overwrite"; }
        else {
          pending_save.clear();
          if (r == ROLLTUI_SAVE_SAVED) hint = RolltuiStr("saved preset '") + o.value + "'";
          else hint = std::move(err);
        }
        if (r == ROLLTUI_SAVE_SAVED) { StrVec names; RolltuiPresetList pl; rolltui_theme_store_list(store, &pl); for (const RolltuiPresetInfo& p : pl) names.add(StrView(p.name)); teditor.set_presets(names); }
        break;
      }
      case K::WriteShipped:
        confirm_arg.assign(o.value);
        ask(RolltuiStr("Write the SHIPPED preset '") + o.value + "' into " + theme_shipped_dir + "? (y/n)", [this] {
          RolltuiStr err;
          if (rolltui_theme_store_save_as(store, confirm_arg.data(), confirm_arg.size(), 1, &err) == ROLLTUI_SAVE_SAVED) hint = RolltuiStr("wrote shipped preset '") + confirm_arg + "' (rebuild to embed it)";
          else hint = std::move(err);
        });
        break;
      case K::LoadPreset: {
        ThemePresetReport rep;
        if (!rolltui_theme_store_load(store, o.value.data(), o.value.size(), &rep, persist ? 1 : 0)) hint.assign(rep.error);
        else {
          RolltuiThemeReport tr{};
          const RolltuiThemePresetValue* wc = rolltui_theme_store_working(store);
          teditor.load(wc->colours, &tr);
          rolltui_theme_report_release(&tr);
          hint = RolltuiStr("loaded '") + o.value + "'";
        }
        break;
      }
      case K::ResetLoaded: {
        std::size_t on = 0; const char* op = rolltui_theme_store_origin(store, &on);
        ask(RolltuiStr("Reset every role to the preset '") + StrView(op, on) + "'? (y/n)", [this] {
          ThemePresetReport rep;
          std::size_t on2 = 0; const char* op2 = rolltui_theme_store_origin(store, &on2);
          if (RolltuiThemePresetValue* p = rolltui_theme_store_get(store, op2, on2, &rep)) {
            RolltuiThemeReport tr{};
            RolltuiStyle dstyles[ROLLTUI_ROLE_COUNT]{}, lstyles[ROLLTUI_ROLE_COUNT]{};
            RolltuiStr dname{}, lname{};
            RolltuiEffectMap* deff = rolltui_theme_load(p->colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), dstyles, &dname, &tr);
            RolltuiEffectMap* leff = rolltui_theme_load(p->colours, ROLLTUI_MODE_LIGHT, rolltui_theme_default_vocab(), lstyles, &lname, &tr);
            if (deff && leff) {
              teditor.replace({std::to_array(dstyles), std::to_array(lstyles), std::move(dname), std::move(lname)}, deff);
              rolltui_effect_map_free(leff);
              editor_outcome({K::Committed, {}});
              std::size_t on3 = 0; const char* op3 = rolltui_theme_store_origin(store, &on3);
              hint = RolltuiStr("reset to '") + StrView(op3, on3) + "' (undoable)";
            } else {
              rolltui_effect_map_free(deff);
              rolltui_effect_map_free(leff);
            }
            rolltui_str_free(&dname);
            rolltui_str_free(&lname);
            rolltui_theme_report_release(&tr);
            rolltui_theme_preset_value_free(p);
          } else hint.assign(rep.error);
        });
        break;
      }
      case K::ResetBuiltin:
        ask("Reset every role to the built-in default? (y/n)", [this] {
          RolltuiStyle dstyles[ROLLTUI_ROLE_COUNT]{}, lstyles[ROLLTUI_ROLE_COUNT]{};
          RolltuiEffectMap* deff = rolltui_theme_builtin_fill("default-dark", 12, dstyles, ROLLTUI_ROLE_COUNT);
          RolltuiEffectMap* leff = rolltui_theme_builtin_fill("default-light", 13, lstyles, ROLLTUI_ROLE_COUNT);
          teditor.replace({std::to_array(dstyles), std::to_array(lstyles), "default-dark", "default-light"}, deff);
          rolltui_effect_map_free(leff);
          editor_outcome({K::Committed, {}});
          hint = "reset to the built-in default (undoable)";
        });
        break;
      case K::Check:
        report_text_ = teditor.report();
        report_top = 0;
        { RolltuiLayer popup = report_popup(); rolltui_window_stack_push(stack, &popup); }
        break;
      case K::Closed:
        toggle_editor();
        break;
    }
  }
  // The Check report popup is a REGISTERED kind — the studio's own text, drawn and
  // scrolled by the library's shared helpers.
  void report_key(const RolltuiChord& k) {
    rolltui_scroll_by_action(rolltui_scroll_text_default_actions(), bindings, &k, popup_rows("report"), report_lines, &report_top);
  }
  int popup_rows(const char* window) {
    std::vector<RolltuiResolvedNode> nodes;
    rolltui_window_stack_resolve(stack, layout_area(), collect_resolved, &nodes);
    for (const RolltuiResolvedNode& rn : nodes)
      if (rn.node->is_window() && rn.node->id == window) return rn.inner.h;
    return 10;
  }
  // The editor's sample box: the focused role's fields, a sample in its style, swatches.
  void draw_layout_editor(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    RolltuiRect r = content_rect(rn);
    if (r.w <= 0 || r.h <= 0) return;
    const RolltuiStyle label = style(ROLLTUI_ROLE_LABEL), value = style(ROLLTUI_ROLE_VALUE);
    // THE TREE FIRST, because a layout IS a tree and this editor was the one place you could
    // not see it. A list of fields says what the selected node is without ever saying where it
    // sits, so splitting a row and swapping siblings were moves made blind. It takes at most a
    // third of the panel and shrinks with it — the fields below are what you came to change.
    leditor.tree_rows(tree_rows_);
    const std::size_t sel = leditor.tree_selected();
    int y = r.y;
    const int tree_h = std::min(static_cast<int>(tree_rows_.n), std::max(r.h / 3, 1));
    // Scrolled so the selected row is always on screen: a tree you have to guess the position
    // of is the thing this replaced.
    std::size_t first = 0;
    if (sel < tree_rows_.n && static_cast<int>(sel) >= tree_h) first = sel - static_cast<std::size_t>(tree_h) + 1;
    for (int i = 0; i < tree_h && first + static_cast<std::size_t>(i) < tree_rows_.n; ++i, ++y) {
      const std::size_t k = first + static_cast<std::size_t>(i);
      const RolltuiRow& row = tree_rows_.v[k];
      const bool is_sel = k == sel;
      RolltuiStyle nm = is_sel ? style(ROLLTUI_ROLE_MENU_SELECTED) : label;
      RolltuiStyle vl = is_sel ? nm : value;
      if (is_sel) rolltui_frame_fill(f, draw_scratch, RolltuiRect{r.x, y, r.w, 1}, nm, nullptr, 0);
      RolltuiRows one{};
      rolltui_rows_add(&one, row.label.p ? row.label.p : "", row.label.n, row.value.p ? row.value.p : "", row.value.n);
      rolltui_frame_put_fields(f, draw_scratch, r.x, y, &one, nm, vl, r.w, ambiguous ? 1 : 0);
      rolltui_rows_release(&one);
    }
    const int box = std::min(4, std::max(r.y + r.h - y, 0));
    RolltuiRect m = r;
    m.y = y;
    m.h = r.y + r.h - y - box;
    RolltuiMenuOptions mo{};
    mo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_menu_set_options_struct(leditor.menu(), &mo);
    rolltui_menu_layout(leditor.menu(), m);
    if (m.h > 0) draw_raw_menu(leditor.menu(), f, rn.focused != 0);
    y = m.y + std::max(m.h, 0);
    // The selected node IN WORDS, which the tree above does not repeat: its size, its border,
    // whether it is hidden, and — the one thing nothing else says — whether this binary can
    // preview its content at all.
    if (const RolltuiStr line = leditor.selection_line(); !line.empty() && y < r.y + r.h)
      put_text(f, r.x, y++, line, label, r.w);
    // SHORT ENOUGH TO FINISH. A hint cut off mid-instruction is worse than a shorter one that
    // ends: "drag an edge r" teaches nobody anything and looks like a defect. The panel is
    // narrow by design, so the hint is written to the width it actually gets.
    if (y < r.y + r.h)
      put_text(f, r.x, y++, "Tab \xC2\xB7 click \xC2\xB7 drag an edge \xC2\xB7 Alt+arrows", value, r.w);
    if (y < r.y + r.h) { leditor.status_line(editor_status); put_text(f, r.x, y++, editor_status, value, r.w); }
    if (y < r.y + r.h && !hint.empty()) put_text(f, r.x, y++, hint, style(ROLLTUI_ROLE_WARNING), r.w);
  }
  // ONE dispatch from `editor_mode` to the open editor, because there were FOUR hand-written
  // copies of this if-chain (the widget plugin's `handle`, the paste branch, the Escape
  // branch, and `draw_editor`) and the menu editor was added to three of them. The one it
  // was missing from is the one every ordinary key flows through, so F8 opened the menu
  // editor, drew it correctly, and typed into the THEME editor — a silent wrong branch of
  // exactly the shape CLAUDE.md's "explicit over implicit" paragraph describes. A fifth
  // editor now cannot be half-wired: there is one place to add it, and drawing is the other.
  void route_editor_event(const RolltuiEvent* e) {
    switch (editor_mode) {
      case EditorMode::Layout: layout_outcome(leditor.handle(e, bindings)); return;
      case EditorMode::Keys:   keys_outcome(keditor.handle(e, bindings)); return;
      case EditorMode::Menu:   menu_outcome(meditor.handle(e, bindings)); return;
      case EditorMode::Theme:  editor_outcome(teditor.handle(e, bindings)); return;
      case EditorMode::None:   return;
    }
  }
  void draw_editor(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    // THE THEME EDITOR IS NOT HERE ANY MORE, and its absence is the point. This file drew one
    // beside the library's `theme` kind — two implementations of one screen — and they drifted:
    // three of this file's copies of the shared strings were UTF-8 read as Latin-1 and shipped
    // that way for months while the library's stayed correct, which only a visibly broken glyph
    // caught. `toggle_editor` names `theme` as the popup's CONTENT now and adopts the editor the
    // window table owns, so there is one editor and one drawing of it.
    //
    // The other three stay: the layout, keys and menu editors are this application's, not the
    // library's, and no kind draws them.
    if (editor_mode == EditorMode::Layout) { draw_layout_editor(rn, f); return; }
    if (editor_mode == EditorMode::Keys) { draw_keys_editor(rn, f); return; }
    if (editor_mode == EditorMode::Menu) { draw_menu_editor(rn, f); return; }
  }

  // The editors' menus carry their state in the root label ("layout editor • transcript"), so
  // the window's title is that breadcrumb and not the layout's own name for the popup.
  int editor_title(RolltuiStr* out) {
    RolltuiMenu* m = editor_mode == EditorMode::Layout ? leditor.menu()
                   : editor_mode == EditorMode::Keys   ? keditor.menu()
                   : editor_mode == EditorMode::Menu   ? meditor.menu()
                                                       : nullptr;
    if (!m) return 0;
    rolltui_menu_breadcrumb(m, out);
    return out->n != 0;
  }

  void draw_confirm(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    const RolltuiRect r = content_rect(rn);
    RolltuiWrapOptions wo{};
    wo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_wrap(wrap_scratch, confirm_text.data(), confirm_text.size(), std::max(r.w, 1), wo);
    int y = r.y;
    const std::size_t n = rolltui_wrap_line_count(wrap_scratch);
    for (std::size_t i = 0; i < n && y < r.y + r.h; ++i) {
      const char* text_p = nullptr; std::size_t text_n = 0;
      const RolltuiWrapGrapheme* gs = nullptr; std::size_t gn = 0;
      int width = 0, indent = 0, hard = 0;
      rolltui_wrap_line(wrap_scratch, i, &text_p, &text_n, &gs, &gn, &width, &indent, &hard);
      put_text(f, r.x + indent, y++, StrView(text_p, text_n), style(ROLLTUI_ROLE_WARNING), std::max(r.w - indent, 0));
    }
    if (y < r.y + r.h) put_text(f, r.x, y, "y = yes    n / Esc = no", style(ROLLTUI_ROLE_PROMPT), r.w);
  }
  // The base layer for the current screen: the chosen layout, or `stacked` below its
  // stated minimum. Re-applied whenever the size or the layout changes.
  void apply_layout() {
    const RolltuiRect area = layout_area();
    const bool want_fallback = area.w < layout.min_width || area.h < layout.min_height;
    stacked_fallback = want_fallback;
    const RolltuiLayer& base = want_fallback ? stacked_layout_.base : layout.base;
    rolltui_window_stack_set_base(stack, &base);
    rolltui_window_stack_set_level_fn(stack, rolltui_windows_back, windows);  // Escape closes one level: a dropdown before the menu
    declare_actions();
  }
  // The `app.*` actions are the LAYOUT's: whatever the loaded file
  // declares, however it was loaded. `editor.*` and `studio.*` are the TOOLS' this
  // binary mounts. Nothing else: an action the TARGET app declares is a
  // thing the target's own layout says, and this tool has no business asserting it.
  void declare_actions() {
    std::vector<RolltuiLayoutAction> declared;
    const RolltuiLayout& lay = effective_layout();
    for (std::size_t i = 0; i < lay.actions.size(); ++i) {
      RolltuiLayoutAction a{};
      a.name.assign(lay.actions[i].name);
      a.description.assign(lay.actions[i].description);
      declared.push_back(std::move(a));
    }
    const std::vector<RolltuiToolAction>& tools = mounted_tools();
    rolltui_bindings_declare(bindings, declared.data(), declared.size(), tools.data(), tools.size());
  }
  // The tools this binary MOUNTS: the four editors, and its own three keys. A host that
  // mounted only the theme editor would list only that one.
  //
  // AND THAT IS THE CONTROL, expressed in the library's own mounting mechanism rather than
  // as a test hack. `ROLLTUI_NO_MENU_EDITOR` makes this binary a
  // designer that did not mount a menu editor: `editor.menu` is not declared, so F8 resolves
  // to no action at all, the F2 menu shows the item with no shortcut, and `toggle_menu_editor`
  // refuses independently. The from-nothing test runs the IDENTICAL keystrokes with it set and
  // gets three files instead of four, which is what proves the fourth file is the menu
  // editor's doing and not some other path's.
  static bool menu_editor_mounted() { return std::getenv("ROLLTUI_NO_MENU_EDITOR") == nullptr; }
  static const std::vector<RolltuiToolAction>& mounted_tools() {
    static const std::vector<RolltuiToolAction> all = [] {
      std::vector<RolltuiToolAction> out;
      auto add = [&out](std::span<const RolltuiToolAction> ts) { for (const RolltuiToolAction& a : ts) out.push_back(a); };
      for (const RolltuiToolAction& a : rolltui::tools::editor_actions())
        if (menu_editor_mounted() || StrView(a.name) != "editor.menu") out.push_back(a);
      add(rolltui::tools::studio_actions());
      return out;
    }();
    return all;
  }
  const RolltuiLayout& effective_layout() const { return stacked_fallback ? stacked_layout_ : layout; }
  RolltuiRect layout_area() const { return {0, 0, w, h > 1 ? h - 1 : h}; }
  void resize(int nw, int nh) {
    w = nw;
    h = nh;
    apply_layout();
  }
  // THE TOOL WHOSE JOB IS DESIGNING AN APP FROM NOTHING MUST ITSELF START FROM NOTHING, so a
  // document argument is optional. Requiring one makes the first step of "build an app from
  // nothing" be handing the designer a file. A document is still how real content is
  // previewed; it is not how the tool starts.
  //
  // THE BARE SCREEN IS THE SHIPPED `default` LAYOUT WITH A PLACEHOLDER DOCUMENT, so what you
  // see with no argument is the screen you see with one and only the content differs.
  // (Rejected: a minimal screen of its own. The layout editor would then be editing against a
  // screen you only ever see when you pass no argument — "what you see bare is not what you
  // get", which is the hidden second path this repo keeps removing.) The placeholder is not
  // empty on purpose: a blank transcript is indistinguishable from a broken one.
  static const char* placeholder() {
    return "# rolltui designer\n\n"
           "Nothing is open. This is the shipped `default` screen with a placeholder in it.\n\n"
           "- **F4** theme  ·  **F6** layout  ·  **F7** keys  ·  **F8** menu\n"
           "- **F2** the menu  ·  **F1** help\n\n"
           "Pass a markdown file to preview real content instead of this.\n";
  }
  bool load_fixture() {
    if (fixture_path.empty()) {
      doc = parse_fixture(placeholder());
      return true;
    }
    bool ok;
    const RolltuiStr text = read_file(fixture_path, ok);
    if (!ok) return false;
    doc = parse_fixture(text);
    return true;
  }
  // The frame's terminal facts and clock, then: instantiate each window's widget from
  // its content, let the widgets that size their window do so, and lay them all out — so
  // an event is hit-tested against exactly the geometry the frame will draw.
  // WHAT THE PICKER ANSWERS WITH. Polled where the preset stores' versions are polled, which is
  // the same shape for the same reason: a widget the layout owns tells the host something once,
  // and the host decides what it means. Here it means "preview this instead", which is the thing
  // that stopped needing a relaunch.
  // WHAT THIS FILE WANTS SAID on the editor's hint line. The kind draws the line; this file
  // handles the outcomes, so it is the only one that knows there was anything to say.
  void push_theme_hint() {
    // This file has no problem/notice distinction on `hint` — it drew every one the same way —
    // so it does not invent one here.
    rolltui_windows_set_theme_hint(windows, "theme", 5, hint.data(), hint.size(), 0);
  }

  void take_picked_file() {
    RolltuiStr got{};
    if (rolltui_windows_picker_taken(windows, "filepicker", 10, &got) && got.n) {
      const RolltuiStr& path = got;
      // THE FILE SAYS WHAT IT IS. A theme, a layout and a bindings file each parse as exactly one
      // domain and as nothing else, so offering a second chord for "add" rather than "preview"
      // would ask a person to classify a file the library can classify itself. Each store refuses
      // by name what is not its own, which is the whole mechanism.
      bool handled = false;
      if (store && !handled) {
        RolltuiStr err{};
        const int r = rolltui_theme_store_add(store, path.data(), path.size(), nullptr, 0, &err);
        if (r == ROLLTUI_SAVE_SAVED) { hint = RolltuiStr("added the theme ") + path; handled = true; }
        // It IS this domain's — the name is what stopped it, and saying so is the point.
        else if (r == ROLLTUI_SAVE_EXISTS_ASK || r == ROLLTUI_SAVE_REFUSED_SHIPPED) { hint = RolltuiStr("that theme name is taken: ") + err; handled = true; }
        rolltui_str_free(&err);
      }
      if (lstore && !handled) {
        RolltuiStr err{};
        const int r = rolltui_layout_store_add(lstore, path.data(), path.size(), nullptr, 0, &err);
        if (r == ROLLTUI_SAVE_SAVED) { hint = RolltuiStr("added the layout ") + path; handled = true; }
        else if (r == ROLLTUI_SAVE_EXISTS_ASK || r == ROLLTUI_SAVE_REFUSED_SHIPPED) { hint = RolltuiStr("that layout name is taken: ") + err; handled = true; }
        rolltui_str_free(&err);
      }
      if (bstore && !handled) {
        RolltuiStr err{};
        const int r = rolltui_bindings_store_add(bstore, path.data(), path.size(), nullptr, 0, &err);
        if (r == ROLLTUI_SAVE_SAVED) { hint = RolltuiStr("added the bindings ") + path; handled = true; }
        else if (r == ROLLTUI_SAVE_EXISTS_ASK || r == ROLLTUI_SAVE_REFUSED_SHIPPED) { hint = RolltuiStr("that bindings name is taken: ") + err; handled = true; }
        rolltui_str_free(&err);
      }
      // Not a preset of any kind, so it is content to look at.
      if (!handled) {
        fixture_path.assign(path);
        if (load_fixture()) hint = RolltuiStr("previewing ") + path;
        else hint = RolltuiStr("cannot read ") + path;
      }
      refresh_menu();  // an added preset joins the chooser without waiting for anything else
      while (rolltui_window_stack_depth(stack) > 1) rolltui_window_stack_pop(stack);
    }
    rolltui_str_free(&got);
  }

  void ensure_layout() {
    take_picked_file();
    // Every frame, not just on open: the hint changes when an action lands, and the kind draws it.
    if (editor_open && editor_mode == EditorMode::Theme) push_theme_hint();
    RolltuiWidgetEnv env{static_cast<unsigned char>(ambiguous ? 1 : 0), clock_ms};
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, layout_area());
    rolltui_windows_layout(windows, stack, layout_area());
    if (rolltui_windows_report_count(windows) == 0) {
      window_note.clear();
    } else {
      RolltuiStr s{};
      rolltui_windows_report_summary(windows, &s);
      window_note.assign(s.p ? s.p : "", s.n);
      rolltui_str_free(&s);
    }
  }
  void toggle_help() {
    if (rolltui_window_stack_has_popup(stack, "help", 4)) {
      while (rolltui_window_stack_depth(stack) > 1) {
        const RolltuiLayer* top = rolltui_window_stack_layer(stack, rolltui_window_stack_depth(stack) - 1);
        if (top->id == "help") break;
        rolltui_window_stack_pop(stack);
      }
      rolltui_window_stack_pop(stack);
      return;
    }
    rolltui_window_stack_push_popup(stack, &effective_layout(), "help", 4);
  }

  // The find bar. It is an ordinary `input:` window in an ordinary popup —
  // the studio does not implement a find MODE, it opens a layout's popup and pipes that
  // input's text to the transcript. Closing it clears the query, so the highlights go
  // with the bar rather than outliving it invisibly.
  void sync_find() {
    if (!rolltui_window_stack_has_popup(stack, "find", 4)) return;
    std::size_t n = 0;
    const char* q = rolltui_input_text(rolltui_windows_input(windows, "find", 4), &n);
    if (rolltui_transcript_set_query(transcript(), q, n)) ensure_layout();
  }

  void toggle_find() {
    if (rolltui_window_stack_has_popup(stack, "find", 4)) {
      while (rolltui_window_stack_depth(stack) > 1) {
        const RolltuiLayer* top = rolltui_window_stack_layer(stack, rolltui_window_stack_depth(stack) - 1);
        if (top->id == "find") break;
        rolltui_window_stack_pop(stack);
      }
      rolltui_window_stack_pop(stack);
      rolltui_input_clear(rolltui_windows_input(windows, "find", 4));
      rolltui_transcript_set_query(transcript(), "", 0);
      return;
    }
    if (rolltui_window_stack_push_popup(stack, &effective_layout(), "find", 4))
      rolltui_window_stack_focus(stack, "find", 4);
  }

  // A layout's state in ONE spelling. The panel and the status line show the same subject and
  // drew it two different ways: the panel said "(fallback)" and never "(modified)", the line said
  // "(modified)" and never "(fallback)", so one layout could carry two labels in one frame.
  // When a fallback is in force the DRAWN layout is not the chosen one, so the chosen one's
  // modified flag describes something that is not on screen and is left unsaid.
  // Fills a caller-held string, so a warm frame allocates nothing here.
  void layout_status(RolltuiStr& out) {
    if (stacked_fallback) {
      out.assign(effective_layout().name);
      out += " (fallback)";
      return;
    }
    if (lstore) { rolltui_layout_store_label(lstore, &layout_label_str); out.assign(layout_label_str); }
    else out.assign(effective_layout().name);
  }

  // ---- the sources the studio binds ----
  // `rows:status`: the studio's own facts. The widget draws them — this says only what
  // they are.
  // A LABEL TEACHES AND A VALUE INFORMS, in one row and with nothing to dismiss. The panel already
  // named the three editable things and was the one part of the screen that told you nothing about
  // reaching them — the keys lived only in the placeholder document, which the user's first real
  // action deletes and the menu popup covers. Carrying the chord in the label means a beginner
  // reads the left column and an expert reads the right.
  //
  // The chord is the BINDINGS' to say, never this file's: an action nobody bound shows no chord
  // at all rather than a key that does nothing.
  void label_with_chord(const char* text, const char* action, RolltuiStr& into) {
    into.assign(text);
    const std::size_t n = rolltui_bindings_chord_count(bindings, action, std::strlen(action));
    if (n == 0) return;
    RolltuiChord c{};
    rolltui_bindings_chord_at(bindings, action, std::strlen(action), 0, &c);
    char buf[ROLLTUI_CHORD_STRING_MAX];
    const std::size_t bn = rolltui_chord_display(&c, buf, sizeof buf);
    into += ' ';
    into.append(buf, bn);
  }

  RolltuiStr theme_key_label, keys_key_label, layout_key_label;

  void status_rows(RolltuiRows& out) {
    // Formatted on the stack or refilled into held strings; the rows copy once into their own
    // reused buffers, so a warm frame allocates nothing here.
    const std::size_t total = rolltui_transcript_total_lines(transcript());
    char b[64];
    label_with_chord("theme", "editor.theme", theme_key_label);
    label_with_chord("keys", "editor.keys", keys_key_label);
    label_with_chord("layout", "editor.layout", layout_key_label);
    if (store) { rolltui_theme_store_label(store, &theme_label_str); out.add(theme_key_label.c_str(), theme_label_str); }
    else out.add(theme_key_label.c_str(), resolved_name.data(), resolved_name.size());
    if (bstore) { rolltui_bindings_store_label(bstore, &keys_label_str); out.add(keys_key_label.c_str(), keys_label_str); }
    else out.add(keys_key_label.c_str(), "default");
    layout_status(layout_row);
    out.add(layout_key_label.c_str(), layout_row.data(), layout_row.size());
    std::snprintf(b, sizeof b, "%dx%d", w, h);
    out.add("size", b);
    std::snprintf(b, sizeof b, "%llu/%llu", static_cast<unsigned long long>(total == 0 ? 0 : rolltui_transcript_top_line(transcript()) + 1),
                  static_cast<unsigned long long>(total));
    out.add("line", b);
    RolltuiScrollAnchor anchor{};
    rolltui_transcript_scroll(transcript(), &anchor);
    out.add("follow", anchor.follow ? "yes" : "no");
    out.add("depth", depth_name(depth).data(), depth_name(depth).size());
    const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
    if (focused) out.add("focus", focused->id); else out.add("focus", "-");
    if (show_timing) { std::snprintf(b, sizeof b, "%lld us", static_cast<long long>(last_frame_us)); out.add("frame", b); }
    if (copied_any) { std::snprintf(b, sizeof b, "%llu bytes", static_cast<unsigned long long>(copied.size())); out.add("copied", b); }
  }

  // `input:prompt`: a submitted line becomes a user entry at the end of the document,
  // so the studio exercises a growing transcript too.
  void append_prompt(StrView text) {
    if (text.empty()) return;
    RolltuiDocEntry* e = rolltui_document_add(&doc);
    e->id = rolltui::format("input%d", submitted++);
    rolltui::assign(e->text, text);
    e->markdown = 0;
    e->prefix = "> ";
    e->prefix_role = to_role(ROLLTUI_ROLE_PROMPT);
  }

  static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    App& a = *static_cast<App*>(ctx);
    rolltui_windows_draw(a.windows, rn, f, a.theme_styles, rolltui_windows_default_roles());
    a.draw_selection(*rn, f);
  }

  // Wrapped text from line `top`, with the transcript's "▼ N more" marker when there is
  // more below. Returns the total wrapped line count (what `top` must be clamped to).
  int draw_scrolled_text(const RolltuiResolvedNode& rn, RolltuiFrame* f, StrView text, int top) {
    const RolltuiRect r = content_rect(rn);
    RolltuiWrapOptions wo{};
    wo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_wrap(wrap_scratch, text.data(), text.size(), std::max(r.w, 1), wo);
    const int total = static_cast<int>(rolltui_wrap_line_count(wrap_scratch));
    if (r.w <= 0 || r.h <= 0) return total;
    int y = r.y;
    for (std::size_t i = static_cast<std::size_t>(std::max(top, 0)); i < static_cast<std::size_t>(total) && y < r.y + r.h; ++i) {
      const char* text_p = nullptr; std::size_t text_n = 0;
      const RolltuiWrapGrapheme* gs = nullptr; std::size_t gn = 0;
      int width = 0, indent = 0, hard = 0;
      rolltui_wrap_line(wrap_scratch, i, &text_p, &text_n, &gs, &gn, &width, &indent, &hard);
      put_text(f, r.x + indent, y++, StrView(text_p, text_n), style(ROLLTUI_ROLE_TEXT), std::max(r.w - indent, 0));
    }
    const int below = total - std::max(top, 0) - r.h;
    char marker[ROLLTUI_MARKER_MAX];
    const std::size_t marker_len = rolltui_scroll_marker_text(below > 0 ? static_cast<std::size_t>(below) : 0, r.w, ambiguous ? 1 : 0, marker, sizeof marker);
    if (marker_len != 0) {
      const int mw = rolltui_u_display_width(u_scratch, marker, marker_len, ambiguous ? 1 : 0);
      put_text(f, r.x + std::max(r.w - mw, 0), r.y + r.h - 1, StrView(marker, marker_len), style(ROLLTUI_ROLE_SCROLL_MARKER), mw);
    }
    return total;
  }

  // The frame: the layout above a one-line status bar of the studio's own. Draws into
  // the frame the swap LENT — this host owns no frame at all.
  void render_into(RolltuiFrame* f, bool with_timing) {
    auto t0 = std::chrono::steady_clock::now();
    show_timing = with_timing;
    sync_look();
    ensure_layout();  // the input window's size follows its text
    sync_find();
    const RolltuiRect area = layout_area();
    rolltui_window_stack_compose(stack, f, area, theme_styles, rolltui_layout_default_roles(), draw_slot, this, ambiguous ? 1 : 0, compose_scratch);
    if (h > 1) {
      fill(f, RolltuiRect{0, h - 1, w, 1}, style(ROLLTUI_ROLE_PANEL_BACKGROUND));
      const std::size_t total = rolltui_transcript_total_lines(transcript());
      RolltuiScrollAnchor anchor{};
      rolltui_transcript_scroll(transcript(), &anchor);
      std::size_t query_len = 0;
      rolltui_transcript_query(transcript(), &query_len);
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      // The status line is REFILLED into a string this struct keeps, piece by piece, with the
      // numbers formatted on the stack: a warm frame allocates nothing for it. Rebuilding it
      // with `+` costs a dozen temporaries a frame, and reading the Theme store's label by
      // value costs a deep compare with them.
      RolltuiStr& status = status_line;
      status.clear();
      status += ' ';
      // A FILE DIALOG'S LAST WORD FIRST: the line is cut from the right, and "wrote the theme
      // to …" is what a person just asked for.
      if (!note.empty()) { status += note; status += "  "; }
      if (store) { rolltui_theme_store_label(store, &theme_label_str); status += theme_label_str; } else status += resolved_name;
      status += editor_mode == EditorMode::Theme    ? " [theme editor]"
              : editor_mode == EditorMode::Layout ? " [layout editor]"
              : editor_mode == EditorMode::Keys   ? " [keys editor]"
              : editor_mode == EditorMode::Menu   ? " [menu editor]"
                                                  : "";
      status += "  ";
      layout_status(layout_row);
      status += layout_row;
      status += "  ";
      rolltui::appendf(status, "%dx%d", w, h);
      rolltui::appendf(status, "  line %zu/%zu", static_cast<std::size_t>(total == 0 ? 0 : rolltui_transcript_top_line(transcript()) + 1), static_cast<std::size_t>(total));
      if (anchor.follow) status += "  follow";
      status += "  ";
      status += depth_name(depth);
      status += "  focus:";
      if (focused) status += focused->id; else status += '-';
      if (with_timing) rolltui::appendf(status, "  %ld us", last_frame_us);
      // The match count and position: the widget owns finding, a host owns saying so.
      if (query_len != 0)
        rolltui::appendf(status, "  find %zu/%zu", static_cast<std::size_t>(rolltui_transcript_current_match_number(transcript())),
                         static_cast<std::size_t>(rolltui_transcript_match_count(transcript())));
      if (copied_any) rolltui::appendf(status, "  copied %zuB", copied.size());
      if (stacked_fallback) rolltui::appendf(status, "  [stacked: below %dx%d]", layout.min_width, layout.min_height);
      if (!theme_note.empty()) { status += "  ["; status += theme_note; status += ']'; }
      if (!layout_note.empty()) { status += "  ["; status += layout_note; status += ']'; }
      if (!window_note.empty()) { status += "  ["; status += window_note; status += ']'; }
      // An effect kind no host registered is SAID. It cannot draw an error panel —
      // an effect has no window — so the status line is where it surfaces. One frame
      // stale, on purpose: it reads the PREVIOUS frame's `rolltui_effects_apply` result,
      // updated again below only after this line is drawn.
      if (!effects_unknown_kinds.empty()) { status += "  [no effect kind '"; status += effects_unknown_kinds[0]; status += "']"; }
      put_text(f, 0, h - 1, status, style(ROLLTUI_ROLE_LABEL), w);
      // The hints come from the live table too.
      auto hk = [&](const char* action) {
        const std::size_t n = rolltui_bindings_chord_count(bindings, action, std::strlen(action));
        if (n == 0) return RolltuiStr("-");
        RolltuiChord c{};
        rolltui_bindings_chord_at(bindings, action, std::strlen(action), 0, &c);
        char buf[ROLLTUI_CHORD_STRING_MAX];
        const std::size_t bn = rolltui_chord_display(&c, buf, sizeof buf);
        return rolltui::own(StrView(buf, bn));
      };
      const RolltuiStr help = RolltuiStr("^C quit  ") + hk("app.help") + " help  " + hk("app.menu") + " menu  " + hk("app.palette") + " palette  " + hk("editor.theme") + " theme  " +
                              hk("editor.layout") + " layout  " + hk("editor.keys") + " keys ";
      const int hw = rolltui_u_display_width(u_scratch, help.data(), help.size(), ambiguous ? 1 : 0);
      const int sw = rolltui_u_display_width(u_scratch, status.data(), status.size(), ambiguous ? 1 : 0);
      if (hw + sw + 2 <= w) put_text(f, w - hw, h - 1, help, style(ROLLTUI_ROLE_TEXT_MUTED), hw);
    }
    // THE ONE PLACE this host applies an effect — after the whole screen has
    // composed, so a marked span under a modal's overlay animates over what the reader
    // actually sees, and before the frame diff.
    RolltuiEffectReport rep{};
    effects_unknown_kinds.clear();
    rolltui_effects_apply(ctx, f, effect_scratch, theme_styles, nullptr, effects_map, effect_ms, ambiguous ? 1 : 0, &rep,
        [](void* ctx, const char* kind, std::size_t len) { static_cast<App*>(ctx)->effects_unknown_kinds.add(StrView(kind, len)); },
        this);
    last_frame_us = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
  }

  // Returns false to quit.
  bool handle(const RolltuiEvent& ev) {
    // App-level keys first; everything else is routed by the stack.
    sync_look();
    if (ev.kind == ROLLTUI_EVENT_KEY) note.clear();  // a file dialog's last word lasts until the next key
    if (ev.kind == ROLLTUI_EVENT_KEY) {
      const RolltuiChord& k = ev.key;
      if (k.key == ROLLTUI_KEY_CHAR && k.ctrl && !k.alt && k.ch == 'c') return false;  // Ctrl-C is the host's, not an action
      // The keys editor is capturing: every key is the chord, nothing else acts.
      if (editor_mode == EditorMode::Keys && keditor.capturing()) {
        keys_outcome(keditor.handle(&ev, bindings));
        return true;
      }
      const StrView st = action_for(k, "studio"), app_a = action_for(k, "app"), ed = action_for(k, "editor");
      if (st == "studio.quit") return false;
      if (st == "studio.cycle_theme") {
        RolltuiStrList sn;
        rolltui_theme_shipped_names(ctx, &sn);
        const StrVec names = shipped_names_of(sn);
        shipped_theme_index = (shipped_theme_index + 1) % names.size();
        const StrView name = names[shipped_theme_index];
        ThemePresetReport rep;
        theme_arg.clear();
        rolltui_theme_store_load(store, name.data(), name.size(), &rep, persist ? 1 : 0);
        if (editor_mode == EditorMode::Theme) { RolltuiThemeReport tr{}; const RolltuiThemePresetValue* wc = rolltui_theme_store_working(store); teditor.load(wc->colours, &tr); rolltui_theme_report_release(&tr); }
        return true;
      }
      if (st == "studio.reload") { load_fixture(); return true; }
      if (st == "studio.save_as") { open_save_dialog(); return true; }
      if (ed == "editor.theme") { toggle_editor(); return true; }
      if (ed == "editor.layout") { toggle_layout_editor(); return true; }
      if (ed == "editor.keys") { toggle_keys_editor(); return true; }
      if (ed == "editor.menu") { toggle_menu_editor(); return true; }
      std::size_t draft_n = 0;
      rolltui_input_text(editor(), &draft_n);
      if (app_a == "app.help" && !(k.key == ROLLTUI_KEY_CHAR && !k.ctrl && !k.alt && draft_n != 0)) { toggle_help(); return true; }
      if (app_a == "app.menu") { open_menu(false); return true; }
      if (app_a == "app.palette") { open_menu(true); return true; }
      if (app_a == "app.find") { toggle_find(); return true; }
      if (app_a == "app.repaint") return true;  // the loop repaints
      // Any other `app.<id>` naming a popup this screen declares is the library's to open. The
      // four above stay here because each does more than push — building menu items, syncing the
      // find query, choosing between two help sources.
      if (rolltui_window_stack_action_popup(stack, &effective_layout(), app_a.data(), app_a.size()))
        return true;
    }
    ensure_layout();
    if (ev.kind == ROLLTUI_EVENT_PASTE) {
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      if (rolltui_window_stack_has_popup(stack, "editor", 6) && focused && focused->id == "editor") {
        route_editor_event(&ev);
        return true;
      }
      input_event("prompt", ev);  // a paste goes to the prompt whatever has focus
      return true;
    }
    // Escape belongs to the editor while it has focus (it cancels the focused change or
    // ascends; the editor asks to close only from its top level) — the stack would
    // otherwise close the popup first.
    if (ev.kind == ROLLTUI_EVENT_KEY && editor_open) {
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      if (focused && focused->id == "editor") {
        const StrView sa = action_for(ev.key, "stack");
        if (sa == "stack.close_popup" || sa == "stack.focus_next" || sa == "stack.focus_prev") {
          hint.clear();
          // The theme editor is the one that ignores a focus move; the tree editors read it.
          if (editor_mode != EditorMode::Theme || sa == "stack.close_popup") route_editor_event(&ev);
          return true;
        }
      }
    }
    // THE LIBRARY KIND DRAWS THE THEME EDITOR; THIS FILE DRIVES IT, and they share one editor
    // object — adopted in `toggle_editor` from the window that owns it. The kind's own `handle`
    // must not also consume these keys, because the studio reacts to OUTCOMES the kind has no
    // notion of: a reset to the built-in that must ASK before it applies, and a check that opens
    // a report popup. Deleting this branch turns both back into silent no-ops, which is exactly
    // what two assertions caught when the drawing first moved.
    if (ev.kind == ROLLTUI_EVENT_KEY && editor_open && editor_mode == EditorMode::Theme) {
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      if (focused && focused->id == "editor") {
        hint.clear();
        route_editor_event(&ev);
        return true;
      }
    }
    // The layout editor's mouse: a press on a seam starts a resize drag, a press on a
    // window selects it — in the BASE layer, under the editor's own popup.
    if (editor_mode == EditorMode::Layout && ev.kind == ROLLTUI_EVENT_MOUSE) {
      const RolltuiMouseEvent& m = ev.mouse;
      const bool in_editor_popup = [&] {
        std::vector<RolltuiResolvedNode> nodes;
        rolltui_window_stack_resolve(stack, layout_area(), collect_resolved, &nodes);
        for (const RolltuiResolvedNode& rn : nodes)
          if (rn.layer > 0 && rn.node->is_window() && rn.outer.contains(m.x, m.y)) return true;
        return false;
      }();
      if (!in_editor_popup) {
        if (m.kind == RolltuiMouseEvent::Kind::Press && m.button == 1) {
          if (std::optional<Seam> seam = seam_at(m.x, m.y)) { drag_seam = std::move(seam); leditor.begin_drag(drag_seam->id); return true; }
          if (std::optional<RolltuiStr> win = window_at(m.x, m.y)) { leditor.select(*win); return true; }
        }
        if (m.kind == RolltuiMouseEvent::Kind::Drag && leditor.dragging() && drag_seam) {
          std::vector<RolltuiResolvedNode> nodes2;
          rolltui_resolve_tree(&leditor.current().base.root, layout_area(), layout_area(), 0, collect_resolved, &nodes2);
          for (const RolltuiResolvedNode& rn : nodes2)
            if (rn.node->id == leditor.selected()) {
              // The pointer is the seam's new place: the sized node runs from its start
              // to the pointer (before the seam) or from the pointer to its end (after).
              const int extent = drag_seam->horizontal ? (drag_seam->after ? rn.outer.x + rn.outer.w - m.x : m.x - rn.outer.x + 1)
                                                       : (drag_seam->after ? rn.outer.y + rn.outer.h - m.y : m.y - rn.outer.y + 1);
              leditor.drag_to(extent);
            }
          sync_look();
          return true;
        }
        if (m.kind == RolltuiMouseEvent::Kind::Release && leditor.dragging()) { drag_seam.reset(); layout_outcome(leditor.end_drag()); return true; }
      }
    }
    RolltuiStr window{};
    const unsigned char route_kind = rolltui_window_stack_route(stack, &ev, layout_area(), bindings, rolltui_stack_default_actions(), &window);
    const StrView target = window;  // the route's answer, freed with `window` when this returns
    if (route_kind == ROLLTUI_ROUTE_CLOSED_POPUP && target == "editor") { editor_open = false; editor_mode = EditorMode::None; store_seen = 0; bstore_seen = 0; sync_look(); return true; }
    if (route_kind == ROLLTUI_ROUTE_CLOSED_POPUP && target == "confirm") { confirm_action = nullptr; return true; }
    if (route_kind != ROLLTUI_ROUTE_DELIVER) return true;
    // The event goes to the window's WIDGET, by kind — never by a window name, so a
    // layout file may call its windows anything. That holds for the studio's OWN three as
    // well: they are registered kinds, so they take their events through the same routing as
    // every built-in.
    if (RolltuiMenu* m = rolltui_windows_menu_at(windows, target.data(), target.size())) {
      RolltuiMenuEvent mev{};
      rolltui_menu_handle(m, &ev, bindings, rolltui_menu_default_actions(), &mev);
      const bool cont = menu_event(mev);
      rolltui_menu_event_release(&mev);
      return cont;
    }
    std::size_t content_len = 0;
    const char* content_p = rolltui_windows_content_at(windows, target.data(), target.size(), &content_len);
    if (content_p) {
      std::size_t row = 0; int is_host = 0;
      const char *cname = nullptr, *csource = nullptr; std::size_t cname_len = 0, csource_len = 0;
      unsigned char problem = 0; RolltuiStr why{};
      if (rolltui_content_parse(ctx, content_p, content_len, &row, &is_host, &cname, &cname_len, &csource,
                                &csource_len, &problem, &why)) {
        const StrView kind(cname, cname_len); // the kind's NAME is its identity
        if (is_host) { rolltui_windows_handle(windows, target.data(), target.size(), &ev); rolltui_str_free(&why); return true; }
        if (kind == "transcript") {
          rolltui_str_free(&why);
          if (rolltui_windows_handle(windows, target.data(), target.size(), &ev)) return true;
          // typing while the transcript has focus still types (falls through to the prompt)
          if (ev.kind != ROLLTUI_EVENT_KEY) return true;
          return input_event("prompt", ev);
        }
        if (kind == "input") {
          const RolltuiStr source = rolltui::own(StrView(csource, csource_len));
          rolltui_str_free(&why);
          return input_event(source, ev);
        }
      }
      rolltui_str_free(&why);
    }
    rolltui_windows_handle(windows, target.data(), target.size(), &ev);  // help / text / file scroll themselves; rows take nothing
    return true;
  }
  // An event for the input: the editor first; what it Ignores is the transcript's.
  // Returns false to quit (Ctrl-D on an empty buffer).
  bool input_event(StrView target, const RolltuiEvent& ev) {
    RolltuiInput* ed = rolltui_windows_input(windows, target.data(), target.size());
    switch (rolltui_input_kind_process_event(ed, windows, target.data(), target.size(), &ev)) {  // Submit has already reached append_prompt
      case ROLLTUI_INPUT_SUBMIT: return true;
      case ROLLTUI_INPUT_EOF: return false;
      case ROLLTUI_INPUT_HANDLED: return true;
      case ROLLTUI_INPUT_IGNORED: break;
    }
    // What the input Ignored is offered to the transcript (its own scope of the table).
    if (ev.kind == ROLLTUI_EVENT_KEY) {
      const StrView a = action_for(ev.key, "transcript");
      if (a == "transcript.page_up") rolltui_transcript_scroll_page(transcript(), -1);
      else if (a == "transcript.page_down") rolltui_transcript_scroll_page(transcript(), 1);
      else if (a == "transcript.top") rolltui_transcript_scroll_to_top(transcript());
      else if (a == "transcript.bottom") rolltui_transcript_scroll_to_bottom(transcript());
      else if (a == "transcript.fold") rolltui_transcript_toggle_fold_nearest_top(transcript(), &doc);
      else if (a == "transcript.find_next") { sync_find(); rolltui_transcript_find_next(transcript()); }
      else if (a == "transcript.find_prev") { sync_find(); rolltui_transcript_find_prev(transcript()); }
      else if (a == "transcript.copy") rolltui_transcript_copy_selection(transcript());
    } else if (ev.kind == ROLLTUI_EVENT_MOUSE && (ev.mouse.kind == RolltuiMouseEvent::Kind::WheelUp || ev.mouse.kind == RolltuiMouseEvent::Kind::WheelDown)) {
      rolltui_transcript_handle(transcript(), &ev, &doc, clock_ms, bindings, rolltui_transcript_default_actions());
    }
    return true;
  }
  void tick() {
    ensure_layout();
    rolltui_transcript_tick(transcript());
  }
  // The redraw interval this frame's motion asks for, or `idle_ms` when nothing moves.
  int poll_timeout_ms(const RolltuiFrame* f, int idle_ms) const {
    if (!f || rolltui_frame_mark_count(f) == 0 || !effects_map || rolltui_effect_map_empty(effects_map)) return idle_ms;
    const int tick = rolltui_effects_tick_ms(ctx, f, effects_map);
    if (tick <= 0) return idle_ms;
    return idle_ms <= 0 ? tick : (idle_ms < tick ? idle_ms : tick);
  }
};

// ---- the three composite kinds' plugins, defined after App since they call its methods ----

void editor_destroy(void*) {}
void editor_layout(void*, const RolltuiResolvedNode*) {}
void editor_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) { static_cast<App*>(ctx)->draw_editor(*rn, f); }
int editor_title(void* ctx, const char*, std::size_t, RolltuiStr* out) { return static_cast<App*>(ctx)->editor_title(out); }
int editor_handle(void* ctx, const RolltuiEvent* e) {
  App* app = static_cast<App*>(ctx);
  app->hint.clear();
  app->route_editor_event(e);
  return 1;
}
// Registered FORBIDDEN and never scrollable/sized on its own — the editor draws its own
// menu and scrolls it internally. `ctx` is `this` (App*), which the widget table does not
// own: App outlives it, so `destroy` is a no-op.
constexpr RolltuiWidgetPlugin kEditorPlugin = {
    /*destroy=*/editor_destroy, /*layout=*/editor_layout, /*draw=*/editor_draw,
    /*problem=*/nullptr, /*note_at=*/nullptr, /*desired_outer=*/nullptr,
    /*handle=*/editor_handle, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr, /*title=*/editor_title,
};
RolltuiWidget editor_factory(void* ctx, RolltuiWindows*, const char*, std::size_t) { return RolltuiWidget{&kEditorPlugin, ctx}; }

void confirm_destroy(void*) {}
void confirm_layout(void*, const RolltuiResolvedNode*) {}
void confirm_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) { static_cast<App*>(ctx)->draw_confirm(*rn, f); }
int confirm_handle(void* ctx, const RolltuiEvent* e) {
  App* app = static_cast<App*>(ctx);
  if (e->kind == ROLLTUI_EVENT_KEY && e->key.key == ROLLTUI_KEY_CHAR && !e->key.ctrl && !e->key.alt) {
    if (e->key.ch == 'y' || e->key.ch == 'Y') { app->close_popup("confirm"); if (app->confirm_action) app->confirm_action(); app->confirm_action = nullptr; }
    else if (e->key.ch == 'n' || e->key.ch == 'N') { app->close_popup("confirm"); app->confirm_action = nullptr; }
  }
  return 1;
}
constexpr RolltuiWidgetPlugin kConfirmPlugin = {
    /*destroy=*/confirm_destroy, /*layout=*/confirm_layout, /*draw=*/confirm_draw,
    /*problem=*/nullptr, /*note_at=*/nullptr, /*desired_outer=*/nullptr,
    /*handle=*/confirm_handle, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr, nullptr /* title: the layout's */, nullptr /* back: no levels */
};
RolltuiWidget confirm_factory(void* ctx, RolltuiWindows*, const char*, std::size_t) { return RolltuiWidget{&kConfirmPlugin, ctx}; }

void report_destroy(void*) {}
void report_layout(void*, const RolltuiResolvedNode*) {}
void report_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  App* app = static_cast<App*>(ctx);
  app->report_lines = app->draw_scrolled_text(*rn, f, app->report_text_, app->report_top);
}
int report_handle(void* ctx, const RolltuiEvent* e) {
  App* app = static_cast<App*>(ctx);
  if (e->kind == ROLLTUI_EVENT_KEY) app->report_key(e->key);
  return 1;
}
// Scrolling is driven by report_key's own keys (page/line/top/bottom), not by the
// scroll_extent/scroll_to capability the window's bar would otherwise use — the report
// has no bar of its own, matching the original CallbackWidget's behaviour exactly.
constexpr RolltuiWidgetPlugin kReportPlugin = {
    /*destroy=*/report_destroy, /*layout=*/report_layout, /*draw=*/report_draw,
    /*problem=*/nullptr, /*note_at=*/nullptr, /*desired_outer=*/nullptr,
    /*handle=*/report_handle, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr, nullptr /* title: the layout's */, nullptr /* back: no levels */
};
RolltuiWidget report_factory(void* ctx, RolltuiWindows*, const char*, std::size_t) { return RolltuiWidget{&kReportPlugin, ctx}; }

// ---- the placeholder: a foreign kind, drawn as what it is ---------------------------------
//
// Installed as the CONTEXT's error factory (see `bind_windows`), so it stands in for exactly
// the case the library leaves to the host: a content whose kind matched no row of either rung.
// A known kind with an unbound source is NOT this — it builds, and says what it is missing
// through its own `problem()`, which is where that sentence belongs.
//
// It draws `[kind:source]`, centred vertically, muted. The whole content and not just the kind,
// because the source is half of what the author typed and a preview that silently dropped it
// would be lying about the screen. `problem` is deliberately NULL: a placeholder has nothing
// to report — the gap report is where a MISSING kind gets said, in the app that lacks it.
struct PlaceholderCtx {
  App* app;              // BORROWED: outlives every widget in its own table
  RolltuiStr label;      // "[content]", built once at construction
};
void placeholder_destroy(void* ctx) { delete static_cast<PlaceholderCtx*>(ctx); }
void placeholder_layout(void*, const RolltuiResolvedNode*) {}
void placeholder_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  PlaceholderCtx* pc = static_cast<PlaceholderCtx*>(ctx);
  RolltuiRect r{};
  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  pc->app->put_text(f, r.x, r.y + r.h / 2, pc->label, pc->app->style(ROLLTUI_ROLE_TEXT_MUTED), r.w);
}
constexpr RolltuiWidgetPlugin kPlaceholderPlugin = {
    /*destroy=*/placeholder_destroy, /*layout=*/placeholder_layout, /*draw=*/placeholder_draw,
    /*problem=*/nullptr, /*note_at=*/nullptr, /*desired_outer=*/nullptr,
    /*handle=*/nullptr, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr, nullptr /* title: the layout's */, nullptr /* back: no levels */
};
RolltuiWidget placeholder_factory(void* ctx, RolltuiWindows*, const char* content, std::size_t len) {
  PlaceholderCtx* pc = new PlaceholderCtx{static_cast<App*>(ctx), RolltuiStr("[") + StrView(content, len) + "]"};
  return RolltuiWidget{&kPlaceholderPlugin, pc};
}

bool parse_size(const RolltuiStr& s, int& w, int& h) {
  const std::size_t x = StrView(s).find('x');
  if (x == StrView::npos) return false;
  w = std::atoi(s.c_str());
  h = std::atoi(s.c_str() + x + 1);
  return w > 0 && h > 0;
}

// One scripted step: an event with the clock it happens at, or a tick. `owned_text`
// backs a Paste step's bytes: `RolltuiEvent::text` is a BORROW (rolltui_keys.h's rule),
// so it is filled in fresh by run_steps() right before dispatch, from THIS stable member
// — never baked in while the vector holding these steps may still grow and relocate it.
// The script vocabulary is the library's self-test header, not this file's: three apps had
// written three dialects of "drive me with no terminal", and one of them said so in its own
// comment. It is included only by binaries that drive an app; no product binary sees it.
#ifdef ROLLTUI_SELFTEST
using rolltui_selftest::Step;
using rolltui_selftest::scripted_keys;
#endif

#ifdef ROLLTUI_SELFTEST
// handle() returns false only to QUIT (Ctrl-C, or the studio.quit action), so a
// script stops there exactly as the interactive loop does.
void run_steps(App& app, std::vector<Step>& steps) {
  for (Step& s : steps) {
    app.clock_ms = s.ms;
    if (s.tick) { app.tick(); continue; }
    if (s.ev.kind == ROLLTUI_EVENT_PASTE) { s.ev.text = s.owned_text.data(); s.ev.text_len = s.owned_text.size(); }
    if (!app.handle(s.ev)) return;
  }
}
#endif

void print_frame_plain(const RolltuiFrame* f) {
  RolltuiStr text{};
  rolltui_frame_to_text(f, &text);
  std::fwrite(text.p ? text.p : "", 1, text.n, stdout);
  rolltui_str_free(&text);
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-studio [FIXTURE.md]\n"
               "       --check NAME|FILE | --generate RULESET [--seed N] [--chaos X]\n"
#ifdef ROLLTUI_SELFTEST
               "       [--ambiguous-wide] [--presets DIR] [--shipped DIR] [--theme NAME|FILE] [--layout NAME|FILE] [--bindings NAME|FILE]\n"
               "       [--mode dark|light] [--depth truecolor|256|16|mono] [--frame WxH | --frame-sgr WxH]\n"
               "       [--tick MS] [--dump-tick] [--dump-role ROLE] [--code-fold FOLD,CAP]\n"
               "       [--keys \"Up Down PageDown Tab F1 F4 Type:hello_world ShiftLeft AltEnter Click 5,3 Drag 20,6 Release ...\"]\n"
#endif
               );
  return 2;
}

RolltuiStr default_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return RolltuiStr(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return RolltuiStr(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return RolltuiStr(home && *home ? home : ".") + "/.config/roll/rolltui";
}

RolltuiStr style_dump(StrView role, const RolltuiStyle& s) {
  RolltuiStr out = rolltui::own(role) + " fg=" + color_to_string(s.fg) + " bg=" + color_to_string(s.bg);
  if (s.bold) out += " bold";
  if (s.italic) out += " italic";
  if (s.underline) out += " underline";
  if (s.dim) out += " dim";
  if (s.reverse) out += " reverse";
  return out;
}

std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// WHAT THE TERMINAL IS — its colour depth, its light or dark, how wide it draws an ambiguous glyph —
// was asked by the library when the terminal was entered, and is remembered per terminal.
void apply_terminal_facts(App& app, RolltuiRun* run) {
  RolltuiTermFacts tf;
  rolltui_terminal_facts(rolltui_run_terminal(run), &tf);
  app.depth = tf.depth;
  if (!app.mode_flag && rolltui_theme_store_working(app.store)->mode == "auto") app.detected_mode = tf.mode;
  app.ambiguous = tf.ambiguous_wide != 0;
}

// `default-dark`/`default-light` are not shipped presets of their own any more (see
// rolltui_theme.c's `builtin_source`): both are `default.json` at a pinned mode. A host filling
// a style table directly (`rolltui_theme_builtin_fill`) already resolves that; a name reaching
// the preset STORE — `--theme`, `--check` — has no `default-dark` to find there, so the alias is
// resolved here instead, one level up, to the same (preset, mode) pair. `mode` is set only when
// non-null: `--check` reports both variants of whatever it is given and has no single mode to pin.
RolltuiStr resolve_builtin_theme_alias(StrView name, std::optional<unsigned char>* mode) {
  if (name == "default-dark") {
    if (mode) *mode = ROLLTUI_MODE_DARK;
    return "default";
  }
  if (name == "default-light") {
    if (mode) *mode = ROLLTUI_MODE_LIGHT;
    return "default";
  }
  return rolltui::own(name);
}

}  // namespace

int main(int argc, char** argv) {
  App app;
  app.depth = rolltui_detect_color_depth(std::getenv("COLORTERM"), std::getenv("TERM"), std::getenv("ROLL_COLOR_DEPTH"));
  RolltuiStr frame_spec, keys_spec, dump_role, check_arg, generate_arg, seed_arg = "1", chaos_arg = "0";
  std::uint64_t tick_ms = 0;   // milestone 6: the elapsed time --frame renders at
  bool dump_tick = false;
  RolltuiStr presets_dir = default_presets_dir(), shipped_dir = ROLLTUI_SHIPPED_DIR;
  bool frame_sgr = false;
  for (int i = 1; i < argc; ++i) {
    const StrView a = argv[i];
    auto next = [&]() -> RolltuiStr { return (i + 1 < argc) ? RolltuiStr(argv[++i]) : RolltuiStr(); };
    // WHAT A FLAG ON THIS COMMAND LINE MAY BE, and the three are not close:
    //   1. A SELF-TEST HOOK — compiled in only for `rolltui-studio-selftest`, which is this same
    //      source built again WITH them. The shipped binary does not contain them, so the binary
    //      that gets verified is not the one that ships.
    //   2. A REAL FEATURE RUN HEADLESSLY — a shipped capability reached without a terminal. Stays.
    //   3. CONFIGURATION — a theme, a layout, a bindings file, a preset directory, a mode, a
    //      depth. **These may never come back.** Each names something the preset system already
    //      holds, autosaves and offers a UI for, and a flag beside it is a second configuration
    //      system with neither discoverability nor persistence, competing with the one that has
    //      both — and winning by accident, because a flag is what a person finds first.
    // A FACT ABOUT THE TERMINAL is none of the three: the library asks the terminal, and a person who
    // knows better says so in ROLL_COLOR_DEPTH or ROLL_AMBIGUOUS_WIDE. (`--ambiguous-wide` below is for
    // a headless golden frame, which has no terminal to ask.)
    // `rolltui-product-flags-test` holds all three products to this.
    // `--check` and `--generate` run the analyser and the seeded generator, with `--seed` and
    // `--chaos` as the generator's own parameters. The theme editor offers both from inside the
    // app, which makes these a NON-INTERACTIVE ENTRY TO A SHIPPED FEATURE rather than a test
    // hook: a theme author checks a theme from a script, and CI checks one without a terminal.
    // A feature nothing but a golden frame happens to call today is still a feature — what a
    // caller reaches is evidence about the API, never the reason a capability exists.
    if (a == "--check") check_arg = resolve_builtin_theme_alias(next(), nullptr);
    else if (a == "--generate") generate_arg = next();
    else if (a == "--seed") seed_arg = next();
    else if (a == "--chaos") chaos_arg = next();
#ifdef ROLLTUI_SELFTEST
    // Prints a role's resolved style. Reached by golden frames and nothing else — the theme
    // editor shows the same thing live, which is where a person looks.
    else if (a == "--dump-role") dump_role = next();
    else if (a == "--ambiguous-wide") app.ambiguous = true;
    // A test still has to pin a theme and point at a scratch directory, so these do not vanish;
    // they leave the PRODUCT. Pointing a person at a directory is what ROLL_CONFIG_DIR is for.
    // A later `--mode` on the command line still wins over the alias's own pin, same as it
    // always could.
    else if (a == "--theme") app.theme_arg = resolve_builtin_theme_alias(next(), &app.mode_flag);
    else if (a == "--layout") app.layout_arg = next();
    else if (a == "--presets") presets_dir = next();
    else if (a == "--shipped") shipped_dir = next();
    else if (a == "--bindings") app.bindings_arg = next();
    else if (a == "--mode") app.mode_flag = (next() == "light") ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK;
    else if (a == "--depth") {
      const RolltuiStr d = next();
      app.depth = rolltui_detect_color_depth(nullptr, nullptr, d.c_str());
    }
    else if (a == "--code-fold") {  // "FOLD,CAP" — the two thresholds, so a golden can
      const RolltuiStr v = next();  // exercise them on a small fixture rather than on
      const std::size_t comma = StrView(v).find(',');  // a hundred-line one
      app.code_fold_over = std::atoi(v.c_str());
      app.code_cap = comma == StrView::npos ? 0 : std::atoi(v.c_str() + comma + 1);
    }
    else if (a == "--frame") frame_spec = next();
    else if (a == "--frame-sgr") { frame_spec = next(); frame_sgr = true; }
    else if (a == "--keys") keys_spec = next();
    else if (a == "--tick") tick_ms = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--dump-tick") dump_tick = true;
#endif
    else if (a.starts_with("--")) return usage();
    else app.fixture_path = rolltui::own(a);
  }
  // ---- milestone 15: the CLI checks and the generator need no fixture ----
  if (!generate_arg.empty()) {
    unsigned char ruleset = 0;
    if (!rolltui_ruleset_from_name(generate_arg.data(), generate_arg.size(), &ruleset)) {
      std::fprintf(stderr, "no ruleset named %s (analogous | complementary | triadic | tetradic | monochrome | pastel | neon | earth)\n", generate_arg.c_str());
      return 2;
    }
    const std::uint64_t seed = std::strtoull(seed_arg.c_str(), nullptr, 10);
    const double chaos = std::strtod(chaos_arg.c_str(), nullptr);
    const RolltuiThemeVocab* vocab = rolltui_theme_default_vocab();
    RolltuiStyle dstyles[ROLLTUI_ROLE_COUNT]{}, lstyles[ROLLTUI_ROLE_COUNT]{};
    RolltuiStr dname{}, lname{};
    RolltuiJsonValue* dmeta = nullptr;
    RolltuiJsonValue* lmeta = nullptr;
    int drep = 0, lrep = 0;
    std::vector<RolltuiRoleCheck> droles(ROLLTUI_ROLE_COUNT), lroles(ROLLTUI_ROLE_COUNT);
    std::vector<RolltuiPairCheck> dpairs(rolltui_must_differ_count()), lpairs(rolltui_must_differ_count());
    RolltuiBadges dbadges{}, lbadges{};
    rolltui_theme_generate(seed, ruleset, chaos, /*has_dark=*/1, /*dark_value=*/1, /*max_repair_passes=*/8, vocab,
                           dstyles, ROLLTUI_ROLE_COUNT, &dname, &dmeta, &drep, droles.data(), dpairs.data(), &dbadges);
    rolltui_theme_generate(seed, ruleset, chaos, /*has_dark=*/1, /*dark_value=*/0, /*max_repair_passes=*/8, vocab,
                           lstyles, ROLLTUI_ROLE_COUNT, &lname, &lmeta, &lrep, lroles.data(), lpairs.data(), &lbadges);
    RolltuiJsonValue* root = rolltui_json_object();
    rolltui_json_set(root, "name", 4, rolltui_json_string(dname.p, dname.n));
    if (rolltui_json_is_object(dmeta)) {
      if (rolltui_json_is_object(lmeta)) {
        const RolltuiJsonValue* db = rolltui_json_get(dmeta, "badges", 6);
        const RolltuiJsonValue* lb = rolltui_json_get(lmeta, "badges", 6);
        if (!rolltui_json_equal(db, lb)) {
          RolltuiJsonValue* pair = rolltui_json_object();
          rolltui_json_set(pair, "dark", 4, rolltui_json_clone(db));
          rolltui_json_set(pair, "light", 5, rolltui_json_clone(lb));
          rolltui_json_set(dmeta, "badges", 6, pair);
        }
      }
      rolltui_json_set(root, "meta", 4, dmeta);
      dmeta = nullptr;
    }
    RolltuiJsonValue* c = rolltui_theme_dump(dstyles, nullptr, lstyles, nullptr, vocab);
    rolltui_json_set(root, "roles", 5, rolltui_json_clone(rolltui_json_get(c, "roles", 5)));
    rolltui_json_free(c);
    RolltuiStr dump{};
    rolltui_json_dump(root, 2, &dump);
    dump += '\n';
    std::fwrite(dump.data(), 1, dump.size(), stdout);
    rolltui_str_free(&dump);
    rolltui_json_free(root);
    rolltui_json_free(dmeta);
    rolltui_json_free(lmeta);
    rolltui_str_free(&dname);
    rolltui_str_free(&lname);
    return 0;
  }
  if (!check_arg.empty()) {
    const RolltuiStr check_shipped_dir = shipped_dir + "/themes";
    RolltuiThemeStore* store = rolltui_theme_store_new(app.ctx, presets_dir.data(), presets_dir.size(), 0,
                                                        check_shipped_dir.data(), check_shipped_dir.size());
    ThemePresetReport rep;
    RolltuiThemePresetValue* p = rolltui_theme_store_get(store, check_arg.data(), check_arg.size(), &rep);
    if (!p) { std::fprintf(stderr, "%s\n", rep.error.c_str()); rolltui_theme_store_free(store); return 2; }
    const RolltuiThemeVocab* vocab = rolltui_theme_default_vocab();
    int rc = 0;
    for (unsigned char m : {ROLLTUI_MODE_DARK, ROLLTUI_MODE_LIGHT}) {
      RolltuiThemeReport tr{};
      RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
      RolltuiStr name{};
      RolltuiEffectMap* eff = rolltui_theme_load(p->colours, m, vocab, styles, &name, &tr);
      rolltui_str_free(&name);
      if (!eff) { std::fprintf(stderr, "%s\n", tr.error.p ? tr.error.p : ""); rolltui_theme_report_release(&tr); rolltui_theme_preset_value_free(p); rolltui_theme_store_free(store); return 2; }
      rolltui_theme_report_release(&tr);
      std::vector<RolltuiRoleCheck> roles(ROLLTUI_ROLE_COUNT);
      std::vector<RolltuiPairCheck> pairs(rolltui_must_differ_count());
      RolltuiBadges badges{};
      rolltui_theme_analyse(styles, ROLLTUI_ROLE_COUNT, roles.data(), pairs.data(), &badges);
      rolltui_effect_map_free(eff);
      // The two notes `rolltui::analyse` built in its C++ shim (ThemeAnalysis.cpp): a
      // terminal-own background, and no text role known at all.
      std::vector<RolltuiStr> notes;
      bool all_none = true;
      for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i)
        all_none = all_none && styles[i].fg.kind == RolltuiStyleColor::Kind::None && styles[i].bg.kind == RolltuiStyleColor::Kind::None;
      RolltuiLin bg_lin{};
      if (!rolltui_to_linear(styles[ROLLTUI_ROLE_BACKGROUND].bg, &bg_lin))
        notes.emplace_back("background is the terminal's own colour: dark/light and every contrast against it depend on the terminal");
      bool any_text_known = false;
      for (const RolltuiRoleCheck& rcv : roles) if (rcv.text && !rcv.unknown) any_text_known = true;
      if (!any_text_known && !all_none) notes.emplace_back("no text role has both colours known; readable / high-contrast cannot be awarded");
      RolltuiStr report_out{};
      rolltui_theme_report_text(roles.data(), roles.size(), pairs.data(), pairs.size(), &badges, notes.data(), notes.size(), vocab, &report_out);
      std::printf("== %s, %s variant ==\n%s", check_arg.c_str(), m == ROLLTUI_MODE_DARK ? "dark" : "light", report_out.p ? report_out.p : "");
      rolltui_str_free(&report_out);
      // `rolltui_theme_load` never touches "meta" (it stays outside that file by its own
      // header note); the C++ shim it replaced (`rolltui::load_theme`) resolved a per-variant
      // {"dark":[...],"light":[...]} badges pair to the one THIS mode claims, as a step of its
      // own after calling the loader — replicated here rather than skipped, since skipping it
      // is exactly what silently turned every claim into "none claimed" above.
      RolltuiJsonValue* meta = nullptr;
      const RolltuiJsonValue* meta_src = rolltui_json_get(p->colours, "meta", 4);
      if (rolltui_json_is_object(meta_src)) {
        meta = rolltui_json_clone(meta_src);
        const RolltuiJsonValue* b = rolltui_json_get(meta, "badges", 6);
        const char* key = m == ROLLTUI_MODE_DARK ? "dark" : "light";
        if (rolltui_json_is_object(b) && rolltui_json_has(b, key, std::strlen(key)))
          rolltui_json_set(meta, "badges", 6, rolltui_json_clone(rolltui_json_get(b, key, std::strlen(key))));
      }
      RolltuiStrArray failed{};
      rolltui_check_claims(meta, &badges, &failed);
      for (std::size_t i = 0; i < failed.n; ++i) { std::printf("CLAIM FAILED: %s\n", failed.v[i].c_str()); rc = 1; }
      const bool claimed_any = rolltui_json_is_array(rolltui_json_get(meta, "badges", 6)) != 0;
      if (!claimed_any) std::printf("(no badges claimed)\n");
      else if (failed.n == 0) std::printf("every claimed badge holds\n");
      rolltui_str_array_release(&failed);
      rolltui_json_free(meta);
      std::printf("\n");
      // A colours object without pairs is the same at both modes: one report is enough.
      RolltuiStr colour_dump{};
      rolltui_json_dump(p->colours, 0, &colour_dump);
      const bool has_dark_pair = StrView(colour_dump).contains("\"dark\"");
      rolltui_str_free(&colour_dump);
      if (!has_dark_pair) break;
    }
    rolltui_theme_preset_value_free(p);
    rolltui_theme_store_free(store);
    return rc;
  }
  // No document is a legitimate start: load_fixture() puts the placeholder up. See its comment.
  if (!app.load_fixture()) { std::fprintf(stderr, "cannot read %s\n", app.fixture_path.c_str()); return 1; }
  // The preset store: the studio is a rolltui host, with the editor's privilege
  // (it writes what ships). Under --frame nothing autosaves.
  app.theme_shipped_dir = shipped_dir + "/themes";
  app.bindings_shipped_dir = shipped_dir + "/bindings";
  const RolltuiStr layout_shipped_dir = shipped_dir + "/layouts";
  app.store = rolltui_theme_store_new(app.ctx, presets_dir.data(), presets_dir.size(), 1,
                                      app.theme_shipped_dir.data(), app.theme_shipped_dir.size());
  app.lstore = rolltui_layout_store_new(app.ctx, presets_dir.data(), presets_dir.size(), 1,
                                        layout_shipped_dir.data(), layout_shipped_dir.size());
  app.bstore = rolltui_bindings_store_new(app.ctx, presets_dir.data(), presets_dir.size(), 1,
                                          app.bindings_shipped_dir.data(), app.bindings_shipped_dir.size());
  app.persist = frame_spec.empty();
  rolltui_context_set_dir(app.ctx, presets_dir.data(), presets_dir.size());  // a layout's `file:` paths are relative to the preset directory
  // AFTER the flags: this is the one Windows setting --code-fold can change, and
  // bind_windows() runs in App's constructor, before argv has been looked at.
  { const RolltuiCodeFold cf{app.code_fold_over, app.code_cap}; rolltui_context_set_code_fold(app.ctx, &cf); }
  {
    ThemePresetReport start_rep;
    rolltui_theme_store_start(app.store, &start_rep);
    if (!start_rep.error.empty()) app.theme_note.assign(start_rep.error);
    LayoutPresetReport lstart_rep;
    rolltui_layout_store_start(app.lstore, &lstart_rep);
    if (!lstart_rep.error.empty()) app.layout_note.assign(lstart_rep.error);
    // What the loader DID that the file did not ask for (today: a file declaring no actions
    // gets the shipped default's) is a note, not a problem — said once, on stderr, so it
    // never moves a golden frame.
    for (std::size_t i = 0; i < lstart_rep.layout.notes_n; ++i)
      std::fprintf(stderr, "rolltui: %s\n", lstart_rep.layout.notes[i].c_str());
    BindingsPresetReport bstart_rep;
    rolltui_bindings_store_start(app.bstore, &bstart_rep);
    if (!bstart_rep.error.empty()) app.hint.assign(bstart_rep.error);
  }
  app.load_theme_arg();
  app.load_bindings_arg();
  {
    // Where the picker opens: beside the document being previewed, which is where a person
    // looking for another one is almost always looking. Set once — setting it per frame would
    // throw away wherever they had navigated to.
    const std::size_t slash = StrView(app.fixture_path).rfind('/');
    const StrView at = app.fixture_path.empty() ? StrView(".") : StrView(app.fixture_path).first(slash == StrView::npos ? 0 : slash + 1);
    rolltui_windows_set_picker_dir(app.windows, "filepicker", 10, at.data(), at.empty() ? 0 : at.size());
  }
  app.refresh_menu();

#ifdef ROLLTUI_SELFTEST
  if (!frame_spec.empty()) {
    int fw, fh;
    if (!parse_size(frame_spec, fw, fh)) return usage();
    app.resize(fw, fh);
    app.load_layout_arg();
    app.sync_look();
    app.ensure_layout();
    { std::vector<Step> steps = scripted_keys(keys_spec, fw, fh); run_steps(app, steps); }
    app.ensure_layout();  // collects whatever the script chose, the way a frame of the loop would
    app.effect_ms = tick_ms;  // --tick: the frame is rendered AT this elapsed time
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f, false);
    if (frame_sgr) {
      RolltuiStr bytes{};
      rolltui_render_full(f, app.depth, &bytes);
      // A screenshot, not a screen: no cursor/clear preamble to strip — `rolltui_render_full`
      // never writes one — so it already cats cleanly.
      std::fwrite(bytes.p ? bytes.p : "", 1, bytes.n, stdout);
      rolltui_str_free(&bytes);
      std::printf("\x1b[0m\n");
    } else {
      print_frame_plain(f);
    }
    if (app.copied_any) std::printf("--- copied ---\n%s\n", app.copied.c_str());
    if (dump_tick) {
      // What this frame ASKS FOR, which is the whole of "the tick runs only while
      // something is marked": no marks (or nothing that moves) prints "none".
      const int tick = rolltui_effects_tick_ms(app.ctx, f, app.effects_map);
      std::printf("--- tick ---\n%s\n", tick > 0 ? rolltui::to_str(tick).c_str() : "none");
    }
    if (!dump_role.empty()) {
      const int r = rolltui_role_from_name(dump_role.data(), dump_role.size());
      if (r < 0) { std::fprintf(stderr, "no role named %s\n", dump_role.c_str()); rolltui_swap_free(swap); return 1; }
      std::printf("--- role ---\n%s\n", style_dump(dump_role, app.style(static_cast<unsigned char>(r))).c_str());
    }
    rolltui_swap_free(swap);
    return 0;
  }
#endif

  // THE LOOP IS THE LIBRARY'S: the depth a frame is presented at, the copying of borrowed events, the repaint
  // after a resize or a stale fact. What is here is what only the studio knows.
  struct Session {
    App* app;
    StrView keys_spec;  // main's own, which outlives the loop
    bool ticking = false;
  } session{&app, keys_spec};
  RolltuiRunApp hooks;
  hooks.ctx = &session;
  hooks.start = [](void* c, RolltuiRun* run) {
    Session& x = *static_cast<Session*>(c);
    App& a = *x.app;
    apply_terminal_facts(a, run);
    RolltuiTerminal* term = rolltui_run_terminal(run);
    a.resize(rolltui_terminal_width(term), rolltui_terminal_height(term));
    a.load_layout_arg();
    a.sync_look();
    a.ensure_layout();
#ifdef ROLLTUI_SELFTEST
    { std::vector<Step> steps = scripted_keys(x.keys_spec, a.w, a.h); run_steps(a, steps); }
#endif
  };
  hooks.ground = [](void* c, RolltuiRun*, RolltuiStyle* out) { *out = static_cast<Session*>(c)->app->style(ROLLTUI_ROLE_BACKGROUND); };
  hooks.render = [](void* c, RolltuiRun*, RolltuiFrame* f, int, int, unsigned long long now) {
    Session& x = *static_cast<Session*>(c);
    App& a = *x.app;
    a.effect_ms = now;
    a.render_into(f, true);
    x.ticking = rolltui_transcript_wants_tick(a.transcript());
    // How long this host may sleep is a function of what the frame MARKED, so an
    // idle screen still costs one wakeup every 250 ms and no more.
    return a.poll_timeout_ms(f, x.ticking ? 50 : 250);
  };
  hooks.event = [](void* c, RolltuiRun* run, const RolltuiEvent* e) {
    App& a = *static_cast<Session*>(c)->app;
    a.clock_ms = now_ms();
    // Ctrl-L is the explicit repaint: the loop repaints on a resize and a stale fact, and this is the person's own
    if (e->kind == ROLLTUI_TERM_EVENT_KEY && e->key.key == ROLLTUI_KEY_CHAR && e->key.ctrl && e->key.ch == 'l') rolltui_run_invalidate(run);
    if (!a.handle(*e)) rolltui_run_stop(run);
  };
  hooks.resized = [](void* c, RolltuiRun*, int w, int h) {
    App& a = *static_cast<Session*>(c)->app;
    a.clock_ms = now_ms();
    a.resize(w, h);
  };
  hooks.facts_changed = [](void* c, RolltuiRun* run) {  // a remembered answer about the terminal turned out to be stale
    App& a = *static_cast<Session*>(c)->app;
    apply_terminal_facts(a, run);
    a.sync_look();
  };
  hooks.settle = [](void* c, RolltuiRun*) {
    Session& x = *static_cast<Session*>(c);
    if (x.ticking) x.app->tick();
    x.app->maybe_reload_theme();
    x.app->maybe_reload_layout();
  };
  RolltuiTerminalOptions topts{};
  if (rolltui_run(STDIN_FILENO, STDOUT_FILENO, topts, &hooks) == ROLLTUI_RUN_NOT_A_TERMINAL) {
    std::fprintf(stderr, "not a terminal (rolltui-studio-selftest --frame WxH renders one)\n");
    return 1;
  }
  return 0;
}
