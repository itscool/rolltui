//
// studio.cpp — the rolltui studio (plan/phase-9.md, requirement 12): renders
// a fixture transcript in a theme and a layout, so trying a layout or theme idea and
// asserting it are the same command.
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
//     --ambiguous-wide         East Asian ambiguous width = 2
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
// (text before any marker is an assistant entry). User entries render verbatim with a
// "> " prefix in the prompt role; notes render verbatim in the note role; a tool
// entry is a FOLDABLE verbatim block whose summary is the marker's text (folded to
// start with).
//
// WIDGETS BY KIND, SOURCES BY NAME (Phase 10 m2): a window's "content" is
// `kind[:source]` from the library's table (rolltui/Layout.hpp), and rolltui::Windows
// instantiates the widget and draws it — the studio only BINDS what is its own,
// by name: the fixture document as `session` (transcript:session), its facts as
// `status` (rows:status), the prompt as `prompt` (input:prompt), and its three
// composites (custom:editor, custom:confirm, custom:report). `help`, `text:<literal>`
// and `file:<path>` need no binding at all, so a layout file can put a label, a document
// or the key list on screen with no code here. A window naming something unbound draws
// the reason and says it in the status line; the studio never asks what a slot means.
//
// The settings menu is a FILE (Phase 10 m3): `menu:main` in the layout resolves to
// <presets>/menus/main.json if the user has one, else to the library's shipped
// rolltui/presets/menus/main.json — which IS this menu. The studio only fills the
// choices whose options are runtime facts (the theme and layout presets it can see) and
// acts on the ids; a user may edit or shadow the file with no rebuild.
//
// KEYS ARE DATA (milestone 17): every key below is the default of an action — the app.*
// ones from rolltui/presets/bindings/default.json, and since Phase 11 m1 the editor.* and
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
// gestures work inside the input. Every event goes through WindowStack::route, so
// what the studio does is what a host would do. The status line shows theme,
// layout, size, scroll position, focus and the last frame's render time
// (instrumented from the first line — a slow frame is a number, not a feeling).
//
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/Document.hpp"
#include "rolltui/Input.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Menu.hpp"
#include "rolltui/Presets.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Terminal.hpp"
#include "rolltui/ThemeAnalysis.hpp"
#include "rolltui/ThemeGen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Transcript.hpp"
#include "rolltui/Widgets.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"
#include "keys_editor.hpp"
#include "layout_editor.hpp"
#include "theme_editor.hpp"
#include "tool_actions.hpp"

using namespace rolltui;
using rolltui::tools::KeysEditor;
using rolltui::tools::LayoutEditor;
using rolltui::tools::ThemeEditor;

namespace {

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

long mtime_of(const std::string& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) return -1;
  return static_cast<long>(st.st_mtime);
}

Document parse_fixture(const std::string& text) {
  Document doc;
  std::string kind = "assistant", summary;
  std::string buf;
  int n = 0;
  auto flush = [&]() {
    // Trim leading/trailing blank lines of the entry.
    std::size_t a = buf.find_first_not_of("\n");
    std::size_t b = buf.find_last_not_of("\n");
    std::string body = (a == std::string::npos) ? "" : buf.substr(a, b - a + 1);
    if (body.empty()) { buf.clear(); return; }
    DocEntry e;
    e.id = "e" + std::to_string(n++);
    e.text = body;
    if (kind == "user") { e.markdown = false; e.role = Role::text; e.prefix = "> "; e.prefix_role = Role::prompt; }
    else if (kind == "note") { e.markdown = false; e.role = Role::note; }
    else if (kind == "tool") { e.markdown = false; e.role = Role::text_muted; e.foldable = true; e.summary = summary; e.folded = true; }
    else { e.markdown = true; e.role = Role::text; }
    doc.entries.push_back(std::move(e));
    buf.clear();
  };
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line == "<!-- user -->" || line == "<!-- assistant -->" || line == "<!-- note -->") {
      flush();
      kind = line.substr(5, line.size() - 9);
      continue;
    }
    if (line.rfind("<!-- tool:", 0) == 0 && line.size() >= 14 && line.compare(line.size() - 3, 3, "-->") == 0) {
      flush();
      kind = "tool";
      summary = line.substr(10, line.size() - 13);
      std::size_t a = summary.find_first_not_of(' '), b = summary.find_last_not_of(' ');
      summary = (a == std::string::npos) ? "" : summary.substr(a, b - a + 1);
      continue;
    }
    buf += line + "\n";
  }
  flush();
  return doc;
}

struct App {
  std::string fixture_path, theme_arg, layout_arg;
  std::optional<ThemeMode> mode_flag;   // --mode; else the working copy's mode
  ThemeMode mode = ThemeMode::Dark;     // the variant in use this frame
  ThemeMode detected_mode = ThemeMode::Dark;  // OSC 11's answer (interactive), dark otherwise
  ColorDepth depth = ColorDepth::TrueColor;
  bool ambiguous = false;
  int w = 80, h = 24;  // the screen
  Document doc;
  // The look comes from the preset store's Theme working copy (rolltui/Presets.hpp),
  // exactly as in roll; the editor, when open, previews its own current theme.
  std::shared_ptr<ThemePresets> store;
  std::shared_ptr<LayoutPresets> lstore;    // the Layout working copy (Phase 10 m1)
  std::shared_ptr<BindingsPresets> bstore;  // the Bindings working copy (milestone 17)
  Bindings bindings = default_bindings();   // what this frame runs on
  std::uint64_t bstore_seen = 0;
  std::string bindings_arg;
  bool persist = true;                  // false under --frame: the working copy is never written
  std::uint64_t store_seen = 0;
  std::uint64_t lstore_seen = 0;
  Theme resolved;                       // the working copy's colours at `mode`
  Theme theme;                          // what this frame draws with (resolved, or the editor's preview)
  std::string theme_note;
  long theme_mtime = -1;
  Layout layout;
  std::string layout_note;
  long layout_mtime = -1;
  bool stacked_fallback = false;
  // The theme editor (milestone 14) and the layout editor (milestone 16) share the
  // side popup; one is open at a time.
  enum class EditorMode { None, Theme, Layout, Keys };
  EditorMode editor_mode = EditorMode::None;
  ThemeEditor teditor;
  LayoutEditor leditor;
  KeysEditor keditor;
  bool editor_open = false;
  std::string pending_save;             // a save-as awaiting its overwrite confirmation
  std::string confirm_text;
  std::function<void()> confirm_action;
  std::string report_text_;  // the Check popup's text
  int report_top = 0;        // the Check report is a custom window: the studio scrolls it
  int report_lines = 0;      // its wrapped length, from the last draw
  std::string hint;
  std::string window_note;   // a window that cannot draw (an unbound source, a bad kind)
  bool show_timing = false;  // the frame-time row/field (interactive only)
  WindowStack stack;
  // Every window's widget comes from its content (rolltui/Widgets.hpp): the studio
  // binds the fixture document, its status rows, the prompt and its own composites by
  // name, and never asks what a slot means.
  Windows windows;
  Transcript& transcript() { return windows.transcript("session"); }
  Input& editor() { return windows.input("prompt"); }
  Menu& menu() { return windows.menu("main"); }  // menus/main.json (Phase 10 m3)
  int submitted = 0;        // entries the input added to the document
  std::string copied;       // the last copy (the studio has no clipboard)
  bool copied_any = false;
  std::uint64_t clock_ms = 0;  // the clock handed to the widgets (real or scripted)
  long last_frame_us = 0;
  std::size_t shipped_theme_index = 0;

  App() {
    transcript().on_copy = [this](const std::string& s) { copied = s; copied_any = true; };
    editor().on_copy = transcript().on_copy;
    InputOptions o;
    o.placeholder = "type here";
    editor().set_options(o);
    bind_windows();
  }

  // The sources a layout may name (Widgets.hpp). Everything a window can show in the
  // studio is here, by name, once — a layout file that says `rows:status` or
  // `text:hello` needs no code at all, and one that names something unbound draws the
  // reason instead of nothing.
  void bind_windows() {
    // `file:` windows resolve a relative path against the preset directory (set once
    // the store exists, below).
    windows.bind_document("session", &doc);
    windows.bind_rows("status", [this] { return status_rows(); });
    windows.bind_submit("prompt", [this](const std::string& text) { append_prompt(text); });
    windows.bind_custom("editor", [this](const ResolvedNode& rn, Frame& f, const Theme&) { draw_editor(rn, f); });
    windows.bind_custom("confirm", [this](const ResolvedNode& rn, Frame& f, const Theme&) { draw_confirm(rn, f); });
    windows.bind_custom("report", [this](const ResolvedNode& rn, Frame& f, const Theme& th) {
      report_lines = draw_scrolled_text(rn, f, th, report_text_, report_top, ambiguous);
    });
    windows.set_help("", {"input", "transcript", "app", "editor", "studio", "stack"},
                     "mouse: drag selects (auto-scrolls past an edge); release copies; double-click a word; triple-click a line;\n"
                     "click a folded block's summary to toggle it; in the layout editor a click selects, a drag on a seam resizes");
  }

  // The menu's STRUCTURE is menus/main.json; what is left here is the part a file
  // cannot hold — the options that are runtime facts (which presets exist) and the
  // current values. Ids are bound below in menu_event(); the file knows nothing of what
  // they do.
  void refresh_menu() {
    std::vector<MenuItem> themes, layouts;
    if (store) for (const PresetInfo& p : store->list()) themes.push_back(MenuItem::action(p.name, p.name + (p.shipped ? "" : "  (yours)")));
    if (lstore) for (const PresetInfo& p : lstore->list()) layouts.push_back(MenuItem::action(p.name, p.name + (p.shipped ? "" : "  (yours)")));
    menu().set_options("theme", themes);
    menu().set_options("layout", layouts);
    menu().set_value("theme", store ? store->label() : "");
    menu().set_value("layout", lstore ? lstore->label() : layout.name);
    menu().set_value("depth", std::string(color_depth_name(depth)));
    menu().set_checked("ambiguous", ambiguous);
  }
  void open_menu(bool palette) {
    if (stack.has_popup("menu")) { close_popup("menu"); return; }
    refresh_menu();  // presets and layout files may have changed
    menu().reset();
    menu().set_palette(palette);
    if (const Layer* p = effective_layout().popup("menu")) stack.push(*p);
  }
  void close_popup(const std::string& id) {
    while (stack.depth() > 1 && stack.layers().back().id != id) stack.pop();
    if (stack.depth() > 1) stack.pop();
  }
  // Returns false to quit.
  bool menu_event(const MenuEvent& ev) {
    using K = MenuEvent::Kind;
    switch (ev.kind) {
      case K::None: return true;
      case K::Closed: close_popup("menu"); return true;
      case K::Choose:
        if (ev.id == "theme") { theme_arg.clear(); PresetLoadReport rep; if (!store->load(ev.value, rep, persist)) hint = rep.error; else hint = rep.summary(); }
        else if (ev.id == "layout") { layout_arg.clear(); PresetLoadReport rep; if (!lstore->load(ev.value, rep, persist)) hint = rep.error; else hint = rep.summary(); }
        else if (ev.id == "depth") depth = detect_color_depth(nullptr, nullptr, ev.value.c_str());
        return true;
      case K::Toggle:
        if (ev.id == "ambiguous") ambiguous = ev.checked;
        return true;
      case K::Activate:
        close_popup("menu");
        if (ev.id == "reload") load_fixture();
        else if (ev.id == "help") toggle_help();
        else if (ev.id == "editor") toggle_editor();
        else if (ev.id == "quit") return false;
        return true;
      case K::Input: return true;
    }
    return true;
  }

  // --theme X / --layout X fill this run's working copy without writing it (the
  // precedence rule in Presets.hpp); a file is re-read on mtime change.
  bool load_theme_arg() {
    if (theme_arg.empty()) return true;
    PresetLoadReport rep;
    theme_mtime = mtime_of(theme_arg);
    if (!store->load(theme_arg, rep, /*persist=*/false)) { theme_note = rep.error; return false; }
    theme_note = rep.summary();
    if (!rep.colours.missing_roles.empty()) theme_note = std::to_string(rep.colours.missing_roles.size()) + " roles missing (inherit text)";
    return true;
  }
  void maybe_reload_theme() {
    if (theme_arg.empty() || ThemePresets::is_shipped(theme_arg)) return;
    if (theme_arg.find('/') == std::string::npos && theme_arg.find(".json") == std::string::npos) return;
    long m = mtime_of(theme_arg);
    if (m != theme_mtime) load_theme_arg();
  }
  bool load_layout_arg() {
    if (!layout_arg.empty()) {
      PresetLoadReport rep;
      layout_mtime = mtime_of(layout_arg);
      if (!lstore->load(layout_arg, rep, /*persist=*/false)) layout_note = rep.error;
      else {
        layout_note.clear();
        if (!rep.layout.unknown_keys.empty()) layout_note += "unknown: " + rep.layout.unknown_keys[0] + "; ";
        if (!rep.layout.bad_values.empty()) layout_note += "bad: " + rep.layout.bad_values[0] + "; ";
      }
    }
    sync_look();
    apply_layout();
    return layout_note.empty();
  }
  void maybe_reload_layout() {
    if (layout_arg.empty() || LayoutPresets::is_shipped(layout_arg)) return;
    if (layout_arg.find('/') == std::string::npos && layout_arg.find(".json") == std::string::npos) return;
    long m = mtime_of(layout_arg);
    if (m != layout_mtime) load_layout_arg();
  }
  // Re-resolves the look from the working copy when the store changed; the editor's
  // preview wins while it is open.
  void sync_look() {
    if (store && store->version() != store_seen) {
      store_seen = store->version();
      const ThemePreset working = store->working();
      mode = mode_flag ? *mode_flag : mode_from_setting(working.mode).value_or(detected_mode);
      ThemeLoadReport rep;
      std::optional<Theme> t = resolve_colours(working, mode, rep);
      resolved = t ? *t : *builtin_theme("default-dark");
      if (!t) theme_note = "colours unusable: " + rep.error;
    }
    if (lstore && lstore->version() != lstore_seen) {
      lstore_seen = lstore->version();
      const Layout working_layout = lstore->working();
      if (!(layout == working_layout)) { layout = working_layout; apply_layout(); }
    }
    theme = editor_mode == EditorMode::Theme ? teditor.current() : resolved;
    if (editor_mode == EditorMode::Layout && !(layout == leditor.current())) { layout = leditor.current(); apply_layout(); }
    if (bstore && bstore->version() != bstore_seen) { bstore_seen = bstore->version(); bindings = bstore->working(); declare_actions(); }
    if (editor_mode == EditorMode::Keys) { bindings = keditor.current(); declare_actions(); }
  }
  bool load_bindings_arg() {
    if (bindings_arg.empty()) return true;
    PresetLoadReport rep;
    if (!bstore->load(bindings_arg, rep, /*persist=*/false)) { hint = rep.error; return false; }
    say_migrations(rep);
    if (!rep.clean()) hint = "bindings: " + rep.summary();
    return true;
  }
  // A renamed action rewritten by the loader (Phase 11 m2). Said on stderr rather than
  // in the status line, for the same reason the layout loader's contents are: it is not
  // a problem, the file still works, and a golden frame must not move because a user's
  // key file is one phase old.
  static void say_migrations(const PresetLoadReport& rep) {
    for (const std::string& m : rep.bindings.migrated) std::fprintf(stderr, "rolltui: bindings: action %s\n", m.c_str());
  }

  // ---- the theme editor (milestone 14) ----
  static Layer editor_popup(const char* title) {
    Layer l;
    l.id = "editor";
    l.placement = {Dim::rel(1), Dim::abs(0), Dim::abs(50), Dim::rel(1), Anchor::TopRight, true, Dim::abs(24), Dim::abs(6), {}, {}};
    l.modal = false;
    Node n = Node::window_id("editor", "custom:editor");
    n.border = Border::Single;
    n.title = title;
    n.focusable = true;
    n.background = Role::panel_background;
    l.root = n;
    l.focus = "editor";
    return l;
  }
  static Layer report_popup() {
    Layer l;
    l.id = "report";
    l.placement = {Dim::rel(0.5), Dim::rel(0.5), Dim::rel(0.8), Dim::rel(0.85), Anchor::Center, true, Dim::abs(30), Dim::abs(5), {}, {}};
    l.modal = true;
    Node n = Node::window_id("report", "custom:report");
    n.border = Border::Rounded;
    n.title = "report";
    n.focusable = true;
    n.background = Role::panel_background;
    l.root = n;
    return l;
  }
  static Layer confirm_popup() {
    Layer l;
    l.id = "confirm";
    l.placement = {Dim::rel(0.5), Dim::rel(0.5), Dim::rel(0.5), Dim::abs(5), Anchor::Center, true, Dim::abs(20), {}, Dim::abs(70), {}};
    l.modal = true;
    Node n = Node::window_id("confirm", "custom:confirm");
    n.border = Border::Rounded;
    n.title = "confirm";
    n.focusable = true;
    n.background = Role::panel_background;
    l.root = n;
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
    ThemeLoadReport rep;
    teditor.load(store->working(), rep);
    std::vector<std::string> names, shipped;
    for (const PresetInfo& p : store->list()) names.push_back(p.name);
    for (std::string_view n : ThemePresets::shipped_names()) shipped.push_back(std::string(n));
    teditor.set_presets(names);
    teditor.set_shipped(shipped, store->options().may_write_shipped);
    teditor.set_mode(mode);
    editor_open = true;
    editor_mode = EditorMode::Theme;
    stack.push(editor_popup("theme editor"));
    sync_look();
  }
  void toggle_keys_editor() {
    if (editor_mode == EditorMode::Keys) { close_editor(); return; }
    close_editor();
    // The LIVE table, not the store's working copy: an action is editable here only if
    // something declared it, and the store's copy is raw rows with no declarations at
    // all. Before Phase 11 m1 that left the editor's `app` scope empty — five keys a
    // layout declares and nobody could rebind — and would have taken `editor` and
    // `studio` with it the moment they stopped being library actions. What this
    // hands over is what the studio is actually running.
    keditor.load(bindings);
    std::vector<std::string> names, shipped;
    for (const PresetInfo& p : bstore->list()) names.push_back(p.name);
    for (std::string_view n : BindingsPresets::shipped_names()) shipped.push_back(std::string(n));
    keditor.set_presets(names);
    keditor.set_shipped(shipped, bstore->options().may_write_shipped);
    editor_open = true;
    editor_mode = EditorMode::Keys;
    stack.push(editor_popup("keys editor"));
    sync_look();
  }
  void keys_outcome(const KeysEditor::Outcome& o) {
    using K = KeysEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        bstore->set_working(keditor.committed(), persist);
        break;
      case K::SaveAs: {
        std::string err;
        const SaveResult r = bstore->save_as(o.value, pending_save == o.value, err);
        if (r == SaveResult::ExistsAsk) { pending_save = o.value; hint = "bindings preset '" + o.value + "' exists; Enter the same name again to overwrite"; }
        else { pending_save.clear(); hint = r == SaveResult::Saved ? "saved bindings preset '" + o.value + "'" : err; }
        if (r == SaveResult::Saved) { std::vector<std::string> names; for (const PresetInfo& p : bstore->list()) names.push_back(p.name); keditor.set_presets(names); }
        break;
      }
      case K::WriteShipped:
        ask("Write the SHIPPED bindings preset '" + o.value + "' into " + bstore->options().shipped_dir + "? (y/n)", [this, name = o.value] {
          std::string err;
          hint = bstore->save_as(name, true, err) == SaveResult::Saved ? "wrote shipped bindings preset '" + name + "' (rebuild to embed it)" : err;
        });
        break;
      case K::LoadPreset: {
        PresetLoadReport rep;
        if (!bstore->load(o.value, rep, persist)) hint = rep.error;
        else { keditor.load(bstore->working()); hint = rep.clean() ? "loaded bindings '" + o.value + "'" : "loaded '" + o.value + "' with problems: " + rep.summary(); }
        break;
      }
      case K::ResetLoaded:
        ask("Reset every binding to the preset '" + bstore->origin() + "'? (y/n)", [this] {
          PresetLoadReport rep;
          if (std::optional<Bindings> b = bstore->get(bstore->origin(), rep)) { keditor.replace(*b); keys_outcome({K::Committed, {}}); hint = "reset (undoable)"; }
          else hint = rep.error;
        });
        break;
      case K::Closed:
        close_editor();
        break;
    }
  }
  void draw_keys_editor(const ResolvedNode& rn, Frame& f) {
    Rect r = content_rect(rn);
    if (r.w <= 0 || r.h <= 0) return;
    const int box = std::min(3, r.h);
    Rect m = r;
    m.h = r.h - box;
    MenuOptions mo;
    mo.ambiguous_wide = ambiguous;
    keditor.menu().set_options(mo);
    keditor.menu().layout(m);
    if (m.h > 0) keditor.menu().draw(f, theme, rn.focused);
    int y = r.y + m.h;
    const Style label = theme.style(Role::label), value = theme.style(Role::value);
    if (y < r.y + r.h) f.put_text(r.x, y++, "preset: " + bstore->label() + " \xC2\xB7 Enter on an action, then press the chord", label, r.w, ambiguous);
    if (y < r.y + r.h) f.put_text(r.x, y++, keditor.status_line(), keditor.capturing() ? theme.style(Role::warning) : value, r.w, ambiguous);
    if (y < r.y + r.h && !hint.empty()) f.put_text(r.x, y++, hint, theme.style(Role::warning), r.w, ambiguous);
  }
  void toggle_layout_editor() {
    if (editor_mode == EditorMode::Layout) { close_editor(); return; }
    close_editor();
    leditor.load(lstore->working());
    std::vector<std::string> names;
    for (const PresetInfo& p : lstore->list()) names.push_back(p.name);  // shipped first, then the user's
    leditor.set_layouts(names);
    leditor.set_sources({"transcript:session", "rows:status", "input:prompt", "text:pane", "custom:editor"});
    leditor.set_menus(windows.menu_names());
    editor_open = true;
    editor_mode = EditorMode::Layout;
    stack.push(editor_popup("layout editor"));
    sync_look();
  }
  void layout_outcome(const LayoutEditor::Outcome& o) {
    using K = LayoutEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        lstore->set_working(leditor.committed(), persist);
        break;
      case K::SaveAs: {
        if (o.value.empty()) { hint = "a layout file needs a name"; break; }
        const std::string path = store->options().dir + "/layouts/" + o.value + ".json";
        std::error_code ec;
        std::filesystem::create_directories(store->options().dir + "/layouts", ec);
        Layout l = leditor.committed();
        l.name = o.value;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) { hint = "cannot write " + path; break; }
        out << layout_to_json(l) << "\n";
        hint = "saved layout file " + path;
        std::vector<std::string> names;
        for (const PresetInfo& p : lstore->list()) names.push_back(p.name);
        leditor.set_layouts(names);
        break;
      }
      case K::LoadLayout: {
        PresetLoadReport rep;
        if (!lstore->load(o.value, rep, persist)) hint = rep.error;
        else { leditor.replace(lstore->working()); hint = "loaded layout " + lstore->label(); }
        break;
      }
      case K::ResetLoaded:
        ask("Reset the layout to the working copy's '" + lstore->label() + "'? (y/n)", [this] {
          leditor.replace(lstore->working());
          hint = "reset (undoable)";
        });
        break;
      case K::Closed:
        close_editor();
        break;
    }
  }
  // The window under a pointer in the base layer, and the seam a press may be on: the
  // right/bottom edge cell of a child that has a following sibling in its Row/Column.
  std::optional<std::string> window_at(int x, int y) {
    std::optional<std::string> best;
    for (const ResolvedNode& rn : resolve_tree(stack.base().root, layout_area(), layout_area()))
      if (rn.node->is_window() && rn.outer.contains(x, y)) best = rn.node->id;
    return best;
  }
  // A seam: the shared edge between two visible siblings. The node that takes the new
  // size is the FIXED-size one when the other fills (dragging the fill would leave a
  // gap the fixed sibling never closes); otherwise the one before the seam.
  struct Seam { std::string id; bool after; bool horizontal; };
  std::optional<Seam> seam_at(int x, int y) {
    const std::vector<ResolvedNode> nodes = resolve_tree(stack.base().root, layout_area(), layout_area());
    for (const ResolvedNode& rn : nodes) {
      if (rn.node->is_window()) continue;
      const bool horizontal = rn.node->kind == Node::Kind::Row;
      for (std::size_t i = 0; i + 1 < rn.node->children.size(); ++i) {
        const Node& child = rn.node->children[i];
        const Node& next = rn.node->children[i + 1];
        if (!child.visible || !next.visible) continue;
        for (const ResolvedNode& c : nodes) {
          if (c.node != &child) continue;
          const int edge = horizontal ? c.outer.x + c.outer.w - 1 : c.outer.y + c.outer.h - 1;
          const bool on = horizontal ? (x == edge || x == edge + 1) && y >= c.outer.y && y < c.outer.y + c.outer.h
                                     : (y == edge || y == edge + 1) && x >= c.outer.x && x < c.outer.x + c.outer.w;
          if (!on) continue;
          const bool size_after = child.size.fill && !next.size.fill;
          return Seam{size_after ? next.id : child.id, size_after, horizontal};
        }
      }
    }
    return std::nullopt;
  }
  std::optional<Seam> drag_seam;
  // The layout editor's selection, drawn from the slot callback (after the window's own
  // border, before the popups above it compose — a highlight drawn after composition
  // would paint over the editor's popup). A split node shows only in the breadcrumb.
  void draw_selection(const ResolvedNode& rn, Frame& f) {
    if (editor_mode != EditorMode::Layout || rn.layer != 0 || rn.node->id != leditor.selected()) return;
    if (rn.node->border != Border::None) draw_border(f, rn.outer, rn.node->border, theme.style(Role::border_active), rn.node->title, theme.style(Role::title), ambiguous);
    else f.tint(rn.outer.intersect(layout_area()), theme.style(Role::selection));
  }
  void ask(std::string text, std::function<void()> action) {
    confirm_text = std::move(text);
    confirm_action = std::move(action);
    stack.push(confirm_popup());
  }
  void editor_outcome(const ThemeEditor::Outcome& o) {
    using K = ThemeEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        store->set_colours(teditor.colours_json(store->origin()), persist);
        break;
      case K::SaveAs: {
        std::string err;
        const SaveResult r = store->save_as(o.value, pending_save == o.value, err);
        if (r == SaveResult::ExistsAsk) { pending_save = o.value; hint = "preset '" + o.value + "' exists; Enter the same name again to overwrite"; }
        else { pending_save.clear(); hint = r == SaveResult::Saved ? "saved preset '" + o.value + "'" : err; }
        if (r == SaveResult::Saved) { std::vector<std::string> names; for (const PresetInfo& p : store->list()) names.push_back(p.name); teditor.set_presets(names); }
        break;
      }
      case K::WriteShipped:
        ask("Write the SHIPPED preset '" + o.value + "' into " + store->options().shipped_dir + "? (y/n)", [this, name = o.value] {
          std::string err;
          hint = store->save_as(name, true, err) == SaveResult::Saved ? "wrote shipped preset '" + name + "' (rebuild to embed it)" : err;
        });
        break;
      case K::LoadPreset: {
        PresetLoadReport rep;
        if (!store->load(o.value, rep, persist)) hint = rep.error;
        else { ThemeLoadReport tr; teditor.load(store->working(), tr); hint = "loaded '" + o.value + "'"; }
        break;
      }
      case K::ResetLoaded:
        ask("Reset every role to the preset '" + store->origin() + "'? (y/n)", [this] {
          PresetLoadReport rep;
          if (std::optional<ThemePreset> p = store->get(store->origin(), rep)) {
            ThemeLoadReport tr;
            std::optional<Theme> d = resolve_colours(*p, ThemeMode::Dark, tr), l = resolve_colours(*p, ThemeMode::Light, tr);
            if (d && l) { teditor.replace({*d, *l}); editor_outcome({K::Committed, {}}); hint = "reset to '" + store->origin() + "' (undoable)"; }
          } else hint = rep.error;
        });
        break;
      case K::ResetBuiltin:
        ask("Reset every role to the built-in default? (y/n)", [this] {
          teditor.replace({*builtin_theme("default-dark"), *builtin_theme("default-light")});
          editor_outcome({K::Committed, {}});
          hint = "reset to the built-in default (undoable)";
        });
        break;
      case K::Check:
        report_text_ = teditor.report();
        report_top = 0;
        stack.push(report_popup());
        break;
      case K::Closed:
        toggle_editor();
        break;
    }
  }
  // The Check report popup is a `custom:` window — the studio's own text, drawn
  // and scrolled by the library's shared helpers (rolltui/Widgets.hpp), which is what
  // every scrolling text window in the library uses.
  void report_key(const KeyEvent& k) {
    scroll_by_action(k, bindings, popup_rows("report"), report_lines, report_top);
  }
  int popup_rows(const char* window) {
    for (const ResolvedNode& rn : stack.resolve(layout_area()))
      if (rn.node->is_window() && rn.node->id == window) return rn.inner.h;
    return 10;
  }
  // The editor's sample box: the focused role's fields, a sample in its style, swatches.
  void draw_layout_editor(const ResolvedNode& rn, Frame& f) {
    Rect r = content_rect(rn);
    if (r.w <= 0 || r.h <= 0) return;
    const int box = std::min(4, r.h);
    Rect m = r;
    m.h = r.h - box;
    MenuOptions mo;
    mo.ambiguous_wide = ambiguous;
    leditor.menu().set_options(mo);
    leditor.menu().layout(m);
    if (m.h > 0) leditor.menu().draw(f, theme, rn.focused);
    int y = r.y + m.h;
    const Style label = theme.style(Role::label), value = theme.style(Role::value);
    if (const std::string line = leditor.selection_line(); !line.empty() && y < r.y + r.h)
      f.put_text(r.x, y++, line, label, r.w, ambiguous);
    if (y < r.y + r.h) f.put_text(r.x, y++, "Tab next node \xC2\xB7 click selects \xC2\xB7 drag an edge resizes \xC2\xB7 Alt+arrows nudge", value, r.w, ambiguous);
    if (y < r.y + r.h) f.put_text(r.x, y++, leditor.status_line(), value, r.w, ambiguous);
    if (y < r.y + r.h && !hint.empty()) f.put_text(r.x, y++, hint, theme.style(Role::warning), r.w, ambiguous);
  }
  void draw_editor(const ResolvedNode& rn, Frame& f) {
    if (editor_mode == EditorMode::Layout) { draw_layout_editor(rn, f); return; }
    if (editor_mode == EditorMode::Keys) { draw_keys_editor(rn, f); return; }
    Rect r = content_rect(rn);
    if (r.w <= 0 || r.h <= 0) return;
    const int box = std::min(6, r.h);
    Rect m = r;
    m.h = r.h - box;
    MenuOptions mo;
    mo.ambiguous_wide = ambiguous;
    teditor.menu().set_options(mo);
    teditor.menu().layout(m);
    if (m.h > 0) teditor.menu().draw(f, theme, rn.focused);
    int y = r.y + m.h;
    const Style label = theme.style(Role::label), value = theme.style(Role::value);
    if (std::optional<Role> role = teditor.focused_role()) {
      const Style& s = theme.style(*role);
      std::string line = std::string(role_name(*role)) + "  fg " + color_to_string(s.fg) + "  bg " + color_to_string(s.bg);
      for (const char* a : {"bold", "italic", "underline", "dim", "reverse"}) {
        const bool on = std::string_view(a) == "bold" ? s.bold : std::string_view(a) == "italic" ? s.italic : std::string_view(a) == "underline" ? s.underline : std::string_view(a) == "dim" ? s.dim : s.reverse;
        if (on) line += std::string("  ") + a;
      }
      if (y < r.y + r.h) f.put_text(r.x, y++, line, label, r.w, ambiguous);
      if (y < r.y + r.h) f.put_text(r.x, y++, " Aa  the quick brown fox â sample in this role ", s, r.w, ambiguous);
      if (y < r.y + r.h) {
        int x = r.x;
        x += f.put_text(x, y, "fg ", label, std::max(r.w - (x - r.x), 0), ambiguous);
        Style sw; sw.bg = s.fg; x += f.put_text(x, y, "      ", sw, std::max(r.w - (x - r.x), 0), ambiguous);
        x += f.put_text(x, y, "  bg ", label, std::max(r.w - (x - r.x), 0), ambiguous);
        Style sb; sb.bg = s.bg; x += f.put_text(x, y, "      ", sb, std::max(r.w - (x - r.x), 0), ambiguous);
        if (std::optional<Color> hc = teditor.highlighted_color()) {
          x += f.put_text(x, y, "  â¶ ", label, std::max(r.w - (x - r.x), 0), ambiguous);
          Style sh; sh.bg = *hc; f.put_text(x, y, "      ", sh, std::max(r.w - (x - r.x), 0), ambiguous);
        }
        ++y;
      }
    } else {
      if (y < r.y + r.h) f.put_text(r.x, y++, "preset: " + store->label(), label, r.w, ambiguous);
      if (y < r.y + r.h) f.put_text(r.x, y++, "Roles âº a role âº fg âº a colour; the transcript is the preview", value, r.w, ambiguous);
      if (y < r.y + r.h) f.put_text(r.x, y++, "type to filter Â· Enter commits Â· Esc cancels Â· Ctrl-Z / Ctrl-Y", value, r.w, ambiguous);
    }
    if (y < r.y + r.h) f.put_text(r.x, y++, teditor.status_line(), value, r.w, ambiguous);
    if (y < r.y + r.h) f.put_text(r.x, y++, hint.empty() ? teditor.badges_line() : hint, hint.empty() ? label : theme.style(Role::warning), r.w, ambiguous);
  }
  void draw_confirm(const ResolvedNode& rn, Frame& f) {
    const Rect r = content_rect(rn);
    WrapOptions wo;
    int y = r.y;
    for (const Line& l : wrap(confirm_text, std::max(r.w, 1), wo)) {
      if (y >= r.y + r.h) break;
      f.put_text(r.x + l.indent, y++, l.text, theme.style(Role::warning), std::max(r.w - l.indent, 0), ambiguous);
    }
    if (y < r.y + r.h) f.put_text(r.x, y, "y = yes    n / Esc = no", theme.style(Role::prompt), r.w, ambiguous);
  }
  // The base layer for the current screen: the chosen layout, or `stacked` below its
  // stated minimum. Re-applied whenever the size or the layout changes.
  void apply_layout() {
    const Rect area = layout_area();
    bool want_fallback = area.w < layout.min_width || area.h < layout.min_height;
    stacked_fallback = want_fallback;
    const Layer& base = want_fallback ? builtin_layout("stacked")->base : layout.base;
    stack.set_base(base);
    declare_actions();
  }
  // The `app.*` actions are the LAYOUT's (Phase 10 m4): whatever the loaded file
  // declares, however it was loaded. `editor.*` and `studio.*` are the TOOLS' this
  // binary mounts (Phase 11 m1): they left library_actions(), so the host that mounts a
  // tool is what declares its actions — one authoritative declare() for both, then the
  // tools' own suggested chords into whatever the bindings file left unsaid.
  void declare_actions() { bindings.declare(effective_layout().actions, mounted_tools()); }
  // The tools this binary MOUNTS: the three editors, and its own three keys. A host that
  // mounted only the theme editor would list only that one — which is the point of the
  // milestone, and why this list is here rather than in the library.
  static const std::vector<ToolAction>& mounted_tools() {
    static const std::vector<ToolAction> all = [] {
      std::vector<ToolAction> out = tools::editor_actions();
      for (const ToolAction& a : tools::studio_actions()) out.push_back(a);
      return out;
    }();
    return all;
  }
  const Layout& effective_layout() const { return stacked_fallback ? *builtin_layout("stacked") : layout; }
  Rect layout_area() const { return {0, 0, w, h > 1 ? h - 1 : h}; }
  void resize(int nw, int nh) {
    w = nw;
    h = nh;
    apply_layout();
  }
  bool load_fixture() {
    bool ok;
    std::string text = read_file(fixture_path, ok);
    if (!ok) return false;
    doc = parse_fixture(text);
    return true;
  }
  TranscriptOptions transcript_options(const ResolvedNode& rn) const {
    TranscriptOptions o;
    o.ambiguous_wide = ambiguous;
    o.inset = rn.node->border != Border::None ? 1 : 0;
    return o;
  }
  // The frame's terminal facts and clock, then: instantiate each window's widget from
  // its content, let the widgets that size their window do so (the input grows with
  // its text), and lay them all out — so an event is hit-tested against exactly the
  // geometry the frame will draw. rolltui::Windows does all three; what used to be
  // here was the same walk with the slot names written into it.
  void ensure_layout() {
    WidgetEnv env;
    env.ambiguous_wide = ambiguous;
    env.bindings = &bindings;
    env.now_ms = clock_ms;
    windows.set_env(env);
    const WindowsReport rep = windows.prepare(stack, layout_area());
    window_note = rep.clean() ? "" : rep.summary();
  }
  void toggle_help() {
    if (stack.has_popup("help")) { while (stack.depth() > 1 && stack.layers().back().id != "help") stack.pop(); stack.pop(); return; }
    if (const Layer* p = effective_layout().popup("help")) stack.push(*p);
  }

  // ---- the sources the studio binds (rolltui/Widgets.hpp) ----
  // `rows:status`: the studio's own facts. The widget draws them (one row per
  // fact, or one line when the window is a single row) — this says only what they are.
  std::vector<Row> status_rows() {
    const std::size_t total = transcript().total_lines();
    std::vector<Row> rows = {
        {"theme", store ? store->label() : theme.name},
        {"keys", bstore ? bstore->label() : "default"},
        {"layout", effective_layout().name + (stacked_fallback ? " (fallback)" : "")},
        {"size", std::to_string(w) + "x" + std::to_string(h)},
        {"line", std::to_string(total == 0 ? 0 : transcript().top_line() + 1) + "/" + std::to_string(total)},
        {"follow", transcript().scroll().follow ? "yes" : "no"},
        {"depth", std::string(color_depth_name(depth))},
        {"focus", stack.focused() ? stack.focused()->id : "-"},
    };
    if (show_timing) rows.push_back({"frame", std::to_string(last_frame_us) + " us"});
    if (copied_any) rows.push_back({"copied", std::to_string(copied.size()) + " bytes"});
    return rows;
  }
  // `input:prompt`: a submitted line becomes a user entry at the end of the document,
  // so the studio exercises a growing transcript too. (The history is the
  // widget's; this is only what the line MEANS here.)
  void append_prompt(const std::string& text) {
    if (text.empty()) return;
    DocEntry e;
    e.id = "input" + std::to_string(submitted++);
    e.text = text;
    e.markdown = false;
    e.prefix = "> ";
    e.prefix_role = Role::prompt;
    doc.entries.push_back(std::move(e));
  }

  // The frame: the layout above a one-line status bar of the studio's own.
  Frame render(bool with_timing) {
    auto t0 = std::chrono::steady_clock::now();
    show_timing = with_timing;
    sync_look();
    ensure_layout();  // the input window's size follows its text (found by the paste golden: a lone
                      // event left the size one event behind)
    Frame f(w, h, theme.style(Role::background));
    const Rect area = layout_area();
    stack.compose(f, area, theme, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, theme); draw_selection(rn, fr); }, ambiguous);
    if (h > 1) {
      f.fill({0, h - 1, w, 1}, theme.style(Role::panel_background));
      const std::size_t total = transcript().total_lines();
      std::string status = " " + (store ? store->label() : theme.name) + (editor_mode == EditorMode::Theme ? " [theme editor]" : editor_mode == EditorMode::Layout ? " [layout editor]" : editor_mode == EditorMode::Keys ? " [keys editor]" : "") + "  " + effective_layout().name + (lstore && lstore->modified() ? " (modified)" : "") + "  " + std::to_string(w) + "x" + std::to_string(h) +
                           "  line " + std::to_string(total == 0 ? 0 : transcript().top_line() + 1) + "/" + std::to_string(total) +
                           (transcript().scroll().follow ? "  follow" : "") + "  " + std::string(color_depth_name(depth)) +
                           "  focus:" + (stack.focused() ? stack.focused()->id : "-");
      if (with_timing) status += "  " + std::to_string(last_frame_us) + " us";
      if (copied_any) status += "  copied " + std::to_string(copied.size()) + "B";
      if (stacked_fallback) status += "  [stacked: below " + std::to_string(layout.min_width) + "x" + std::to_string(layout.min_height) + "]";
      if (!theme_note.empty()) status += "  [" + theme_note + "]";
      if (!layout_note.empty()) status += "  [" + layout_note + "]";
      if (!window_note.empty()) status += "  [" + window_note + "]";
      f.put_text(0, h - 1, status, theme.style(Role::label), w, ambiguous);
      // The hints come from the live table too.
      auto hk = [&](const char* action) { const std::vector<KeyEvent>& c = bindings.chords_for(action); return c.empty() ? std::string("-") : chord_display(c[0]); };
      std::string help = "^C quit  " + hk("app.help") + " help  " + hk("app.menu") + " menu  " + hk("app.palette") + " palette  " + hk("editor.theme") + " theme  " +
                         hk("editor.layout") + " layout  " + hk("editor.keys") + " keys ";
      int hw = unicode::display_width(help);
      if (hw + unicode::display_width(status) + 2 <= w) f.put_text(w - hw, h - 1, help, theme.style(Role::text_muted), hw, ambiguous);
    }
    last_frame_us = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    return f;
  }

  // Returns false to quit.
  bool handle(const Event& ev) {
    // App-level keys first; everything else is routed by the stack.
    sync_look();
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev)) {
      if (k->key == Key::Char && k->ctrl && !k->alt && k->ch == 'c') return false;  // Ctrl-C is the host's, not an action
      // The keys editor is capturing: every key is the chord, nothing else acts.
      if (editor_mode == EditorMode::Keys && keditor.capturing()) { keys_outcome(keditor.handle(ev, bindings)); return true; }
      const std::string_view st = bindings.action_for(*k, "studio"), app = bindings.action_for(*k, "app"), ed = bindings.action_for(*k, "editor");
      if (st == "studio.quit") return false;
      if (st == "studio.cycle_theme") {
        std::vector<std::string_view> names = ThemePresets::shipped_names();
        shipped_theme_index = (shipped_theme_index + 1) % names.size();
        PresetLoadReport rep;
        theme_arg.clear();
        store->load(names[shipped_theme_index], rep, persist);
        if (editor_mode == EditorMode::Theme) { ThemeLoadReport tr; teditor.load(store->working(), tr); }
        return true;
      }
      if (st == "studio.reload") { load_fixture(); return true; }
      if (ed == "editor.theme") { toggle_editor(); return true; }
      if (ed == "editor.layout") { toggle_layout_editor(); return true; }
      if (ed == "editor.keys") { toggle_keys_editor(); return true; }
      if (app == "app.help" && !(k->key == Key::Char && !k->ctrl && !k->alt && !editor().text().empty())) { toggle_help(); return true; }
      if (app == "app.menu") { open_menu(false); return true; }
      if (app == "app.palette") { open_menu(true); return true; }
      if (app == "app.repaint") return true;  // the loop repaints
    }
    ensure_layout();
    if (const PasteEvent* p = std::get_if<PasteEvent>(&ev)) {
      if (stack.has_popup("editor") && stack.focused() && stack.focused()->id == "editor") {
        if (editor_mode == EditorMode::Layout) layout_outcome(leditor.handle(*p, bindings));
        else if (editor_mode == EditorMode::Keys) keys_outcome(keditor.handle(*p, bindings));
        else editor_outcome(teditor.handle(*p, bindings));
        return true;
      }
      windows.input_event("prompt", *p);  // a paste goes to the prompt whatever has focus
      return true;
    }
    // Escape belongs to the editor while it has focus (it cancels the focused change or
    // ascends; the editor asks to close only from its top level) — the stack would
    // otherwise close the popup first.
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev);
        k && editor_open && stack.focused() && stack.focused()->id == "editor" &&
        (bindings.action_for(*k, "stack") == "stack.close_popup" || bindings.action_for(*k, "stack") == "stack.focus_next" || bindings.action_for(*k, "stack") == "stack.focus_prev")) {
      hint.clear();
      if (editor_mode == EditorMode::Layout) layout_outcome(leditor.handle(ev, bindings));
      else if (editor_mode == EditorMode::Keys) keys_outcome(keditor.handle(ev, bindings));
      else if (bindings.action_for(*k, "stack") == "stack.close_popup") editor_outcome(teditor.handle(ev, bindings));
      return true;
    }
    // The layout editor's mouse: a press on a seam starts a resize drag, a press on a
    // window selects it — in the BASE layer, under the editor's own popup.
    if (editor_mode == EditorMode::Layout) {
      if (const MouseEvent* m = std::get_if<MouseEvent>(&ev)) {
        const bool in_editor_popup = [&] {
          for (const ResolvedNode& rn : stack.resolve(layout_area()))
            if (rn.layer > 0 && rn.node->is_window() && rn.outer.contains(m->x, m->y)) return true;
          return false;
        }();
        if (!in_editor_popup) {
          if (m->kind == MouseEvent::Kind::Press && m->button == 1) {
            if (std::optional<Seam> seam = seam_at(m->x, m->y)) { drag_seam = seam; leditor.begin_drag(seam->id); return true; }
            if (std::optional<std::string> w = window_at(m->x, m->y)) { leditor.select(*w); return true; }
          }
          if (m->kind == MouseEvent::Kind::Drag && leditor.dragging() && drag_seam) {
            for (const ResolvedNode& rn : resolve_tree(leditor.current().base.root, layout_area(), layout_area()))
              if (rn.node->id == leditor.selected()) {
                // The pointer is the seam's new place: the sized node runs from its start
                // to the pointer (before the seam) or from the pointer to its end (after).
                const int extent = drag_seam->horizontal ? (drag_seam->after ? rn.outer.x + rn.outer.w - m->x : m->x - rn.outer.x + 1)
                                                         : (drag_seam->after ? rn.outer.y + rn.outer.h - m->y : m->y - rn.outer.y + 1);
                leditor.drag_to(extent);
              }
            sync_look();
            return true;
          }
          if (m->kind == MouseEvent::Kind::Release && leditor.dragging()) { drag_seam.reset(); layout_outcome(leditor.end_drag()); return true; }
        }
      }
    }
    Route r = stack.route(ev, layout_area(), bindings);
    if (r.kind == Route::Kind::ClosedPopup && r.window == "editor") { editor_open = false; editor_mode = EditorMode::None; store_seen = 0; bstore_seen = 0; sync_look(); return true; }
    if (r.kind == Route::Kind::ClosedPopup && r.window == "confirm") { confirm_action = nullptr; return true; }
    if (r.kind != Route::Kind::Deliver) return true;
    // The event goes to the window's WIDGET, by kind — never by a window name, so a
    // layout file may call its windows anything (Phase 10 m2). A `custom:` window is
    // the studio's own, dispatched by the name it bound.
    if (Menu* m = windows.menu_at(r.window)) return menu_event(m->handle(ev, bindings));
    const std::string own = windows.custom_at(r.window);
    if (own == "editor") {
      hint.clear();
      if (editor_mode == EditorMode::Layout) layout_outcome(leditor.handle(ev, bindings));
      else if (editor_mode == EditorMode::Keys) keys_outcome(keditor.handle(ev, bindings));
      else editor_outcome(teditor.handle(ev, bindings));
      return true;
    }
    if (own == "report") {
      if (const KeyEvent* k = std::get_if<KeyEvent>(&ev)) report_key(*k);
      return true;
    }
    if (own == "confirm") {
      if (const KeyEvent* k = std::get_if<KeyEvent>(&ev); k && k->key == Key::Char && !k->ctrl && !k->alt) {
        if (k->ch == 'y' || k->ch == 'Y') { close_popup("confirm"); if (confirm_action) confirm_action(); confirm_action = nullptr; }
        else if (k->ch == 'n' || k->ch == 'N') { close_popup("confirm"); confirm_action = nullptr; }
      }
      return true;
    }
    const std::optional<Content> c = windows.content_at(r.window);
    if (c && c->kind == WidgetKind::Transcript) {
      if (windows.handle(r.window, ev)) return true;
      if (!std::holds_alternative<KeyEvent>(ev)) return true;
      // typing while the transcript has focus still types (falls through to the prompt)
      return input_event("prompt", ev);
    }
    if (c && c->kind == WidgetKind::Input) return input_event(c->source, ev);
    windows.handle(r.window, ev);  // help / text / file scroll themselves; rows take nothing
    return true;
  }
  // An event for the input: the editor first; what it Ignores is the transcript's
  // (plan: typing never touches the offset; PgUp/PgDn, Ctrl-Home/End, Home/End on an
  // empty buffer, Ctrl-O, Alt-C without a selection and the wheel act on it from
  // wherever focus is). Returns false to quit (Ctrl-D on an empty buffer).
  bool input_event(std::string_view target, const Event& ev) {
    switch (windows.input_event(target, ev)) {  // Submit has already reached append_prompt
      case InputAction::Submit: return true;
      case InputAction::Eof: return false;
      case InputAction::Handled: return true;
      case InputAction::Ignored: break;
    }
    // What the input Ignored is offered to the transcript (its own scope of the table).
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev)) {
      const std::string_view a = bindings.action_for(*k, "transcript");
      if (a == "transcript.page_up") transcript().scroll_page(-1);
      else if (a == "transcript.page_down") transcript().scroll_page(1);
      else if (a == "transcript.top") transcript().scroll_to_top();
      else if (a == "transcript.bottom") transcript().scroll_to_bottom();
      else if (a == "transcript.fold") transcript().toggle_fold_nearest_top(doc);
      else if (a == "transcript.copy") transcript().copy_selection();
    } else if (const MouseEvent* m = std::get_if<MouseEvent>(&ev);
               m && (m->kind == MouseEvent::Kind::WheelUp || m->kind == MouseEvent::Kind::WheelDown)) {
      transcript().handle(ev, doc, clock_ms, bindings);
    }
    return true;
  }
  void tick() {
    ensure_layout();
    transcript().tick();
  }
};

bool parse_size(const std::string& s, int& w, int& h) {
  std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

// One scripted step: an event with the clock it happens at, or a tick.
struct Step {
  bool tick = false;
  Event ev;
  std::uint64_t ms = 0;
};

std::vector<Step> scripted_keys(const std::string& spec, int w, int h) {
  std::vector<Step> out;
  std::istringstream in(spec);
  std::string tok;
  std::uint64_t clock = 1000;
  auto key = [](Key k, bool shift = false, bool ctrl = false, bool alt = false) {
    KeyEvent e;
    e.key = k;
    e.shift = shift;
    e.ctrl = ctrl;
    e.alt = alt;
    return e;
  };
  auto ctrl = [](char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.ctrl = true; return e; };
  auto alt = [](char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.alt = true; return e; };
  // "Type:hello_world" → h e l l o ␠ w o r l d; "Paste:a\nb" → one paste event.
  auto unescape = [](std::string s, bool newlines) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '_') out.push_back(' ');
      else if (newlines && s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out.push_back('\n'); ++i; }
      else out.push_back(s[i]);
    }
    return out;
  };
  // A named key with an optional Shift/Ctrl/Alt prefix: "ShiftLeft", "CtrlHome", "AltEnter".
  auto named = [&](std::string name, KeyEvent& out) {
    bool shift = false, c = false, a = false;
    for (;;) {
      if (name.rfind("Shift", 0) == 0) { shift = true; name.erase(0, 5); }
      else if (name.rfind("Ctrl", 0) == 0) { c = true; name.erase(0, 4); }
      else if (name.rfind("Alt", 0) == 0) { a = true; name.erase(0, 3); }
      else break;
    }
    static const std::pair<const char*, Key> keys[] = {
        {"Up", Key::Up}, {"Down", Key::Down}, {"Left", Key::Left}, {"Right", Key::Right}, {"PageUp", Key::PageUp},
        {"PageDown", Key::PageDown}, {"Home", Key::Home}, {"End", Key::End}, {"Enter", Key::Enter}, {"Escape", Key::Escape},
        {"Tab", Key::Tab}, {"Backspace", Key::Backspace}, {"Delete", Key::Delete}, {"F1", Key::F1}, {"F2", Key::F2},
        {"F3", Key::F3}, {"F4", Key::F4}, {"F5", Key::F5}, {"F6", Key::F6}, {"F7", Key::F7}, {"F8", Key::F8}};
    for (const auto& [n, k] : keys)
      if (name == n) { out = key(k, shift, c, a); return true; }
    if (name.size() == 1 && (c || a) && name[0] >= 'A' && name[0] <= 'Z') {  // CtrlA, AltC, ...
      out = c ? ctrl(static_cast<char>(name[0] - 'A' + 'a')) : alt(static_cast<char>(name[0] - 'A' + 'a'));
      out.shift = shift;
      return true;
    }
    return false;
  };
  auto mouse = [](MouseEvent::Kind k, int x, int y, int button = 1, bool shift = false) {
    MouseEvent m;
    m.kind = k;
    m.x = x;
    m.y = y;
    m.button = button;
    m.shift = shift;
    return m;
  };
  auto push = [&](Event e, bool advance = true) {
    out.push_back({false, std::move(e), clock});
    if (advance) clock += 1000;
  };
  auto xy = [&](int& x, int& y) {
    std::string pos;
    if (!(in >> pos)) return false;
    std::size_t comma = pos.find(',');
    if (comma == std::string::npos) return false;
    x = std::atoi(pos.substr(0, comma).c_str());
    y = std::atoi(pos.substr(comma + 1).c_str());
    return true;
  };
  while (in >> tok) {
    int x = 0, y = 0;
    KeyEvent k;
    if (tok == "Tick") out.push_back({true, {}, clock});
    else if (tok.rfind("Type:", 0) == 0) {
      for (const unicode::DecodedChar& d : unicode::decode_utf8(unescape(tok.substr(5), false))) {
        KeyEvent e;
        e.key = Key::Char;
        e.ch = d.cp;
        push(e, false);
      }
      clock += 1000;
    } else if (tok.rfind("Paste:", 0) == 0) push(PasteEvent{unescape(tok.substr(6), true)});
    else if (named(tok, k)) push(k);
    else if (tok == "WheelUp" || tok == "WheelDown") {
      // Over the middle of the screen, which every built-in layout gives to the transcript.
      push(mouse(tok == "WheelUp" ? MouseEvent::Kind::WheelUp : MouseEvent::Kind::WheelDown, w / 4, h / 3, 0));
    } else if (tok == "Click" || tok == "ShiftClick") {
      if (xy(x, y)) push(mouse(MouseEvent::Kind::Press, x, y, 1, tok == "ShiftClick"));
    } else if (tok == "DblClick" || tok == "TripleClick") {
      if (xy(x, y)) {
        const int presses = tok == "DblClick" ? 2 : 3;
        for (int i = 0; i < presses; ++i) {
          push(mouse(MouseEvent::Kind::Press, x, y), false);
          if (i + 1 < presses) push(mouse(MouseEvent::Kind::Release, x, y), false);
        }
        clock += 1000;
      }
    } else if (tok == "Drag") {
      if (xy(x, y)) push(mouse(MouseEvent::Kind::Drag, x, y));
    } else if (tok == "Release") {
      // Optional position; without one the release lands where the last event was.
      std::streampos here = in.tellg();
      std::string maybe;
      if (in >> maybe && maybe.find(',') != std::string::npos) {
        x = std::atoi(maybe.substr(0, maybe.find(',')).c_str());
        y = std::atoi(maybe.substr(maybe.find(',') + 1).c_str());
      } else {
        in.clear();
        in.seekg(here);
        const MouseEvent* last = nullptr;
        for (const Step& s : out)
          if (const MouseEvent* m = std::get_if<MouseEvent>(&s.ev)) last = m;
        if (last) { x = last->x; y = last->y; }
      }
      push(mouse(MouseEvent::Kind::Release, x, y));
    } else {
      std::vector<unicode::DecodedChar> d = unicode::decode_utf8(tok);
      if (!d.empty()) { KeyEvent e; e.key = Key::Char; e.ch = d[0].cp; push(e); }
    }
  }
  return out;
}

// handle() returns false only to QUIT (Ctrl-C, or the studio.quit action), so a
// script stops there exactly as the interactive loop does. That is also the only way a
// `--frame` run can show whether a quit key landed: the keys after it do not happen, so
// `--keys "CtrlQ F1"` renders the frame WITHOUT the help popup that `--keys "F1"` opens.
void run_steps(App& app, const std::vector<Step>& steps) {
  for (const Step& s : steps) {
    app.clock_ms = s.ms;
    if (s.tick) app.tick();
    else if (!app.handle(s.ev)) return;
  }
}

void print_frame_plain(const Frame& f) {
  const std::string text = frame_to_text(f);
  std::fwrite(text.data(), 1, text.size(), stdout);
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-studio --check NAME|FILE | --generate RULESET [--seed N] [--chaos X]\n"
               "       rolltui-studio FIXTURE.md [--presets DIR] [--shipped DIR] [--theme NAME|FILE] [--layout NAME|FILE] [--bindings NAME|FILE]\n"
               "       [--mode dark|light] [--depth truecolor|256|16|mono] [--ambiguous-wide] [--frame WxH | --frame-sgr WxH]\n"
               "       [--dump-role ROLE] [--keys \"Up Down PageDown Tab F1 F4 Type:hello_world ShiftLeft AltEnter Click 5,3 Drag 20,6 Release ...\"]\n");
  return 2;
}

std::string default_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return std::string(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return std::string(home && *home ? home : ".") + "/.config/roll/rolltui";
}

std::string style_dump(std::string_view role, const Style& s) {
  std::string out = std::string(role) + " fg=" + color_to_string(s.fg) + " bg=" + color_to_string(s.bg);
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

}  // namespace

int main(int argc, char** argv) {
  App app;
  app.depth = detect_color_depth(std::getenv("COLORTERM"), std::getenv("TERM"), std::getenv("ROLL_COLOR_DEPTH"));
  std::string frame_spec, keys_spec, dump_role, check_arg, generate_arg, seed_arg = "1", chaos_arg = "0";
  std::string presets_dir = default_presets_dir(), shipped_dir = ROLLTUI_SHIPPED_DIR;
  bool frame_sgr = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--theme") app.theme_arg = next();
    else if (a == "--layout") app.layout_arg = next();
    else if (a == "--presets") presets_dir = next();
    else if (a == "--shipped") shipped_dir = next();
    else if (a == "--bindings") app.bindings_arg = next();
    else if (a == "--dump-role") dump_role = next();
    else if (a == "--check") check_arg = next();
    else if (a == "--generate") generate_arg = next();
    else if (a == "--seed") seed_arg = next();
    else if (a == "--chaos") chaos_arg = next();
    else if (a == "--mode") app.mode_flag = (next() == "light") ? ThemeMode::Light : ThemeMode::Dark;
    else if (a == "--depth") {
      std::string d = next();
      app.depth = detect_color_depth(nullptr, nullptr, d.c_str());
    } else if (a == "--ambiguous-wide") app.ambiguous = true;
    else if (a == "--frame") frame_spec = next();
    else if (a == "--frame-sgr") { frame_spec = next(); frame_sgr = true; }
    else if (a == "--keys") keys_spec = next();
    else if (a.rfind("--", 0) == 0) return usage();
    else app.fixture_path = a;
  }
  // ---- milestone 15: the CLI checks and the generator need no fixture ----
  if (!generate_arg.empty()) {
    const std::optional<Ruleset> rs = ruleset_from_name(generate_arg);
    if (!rs) { std::fprintf(stderr, "no ruleset named %s (analogous | complementary | triadic | tetradic | monochrome | pastel | neon | earth)\n", generate_arg.c_str()); return 2; }
    const std::uint64_t seed = std::strtoull(seed_arg.c_str(), nullptr, 10);
    const double chaos = std::strtod(chaos_arg.c_str(), nullptr);
    GenOptions d, l;
    d.dark = true;
    l.dark = false;
    const Generated gd = generate(seed, *rs, chaos, d), gl = generate(seed, *rs, chaos, l);
    std::string text = json::dump(theme_pair_to_json_value(gd.theme, gl.theme, gd.theme.name), 2) + "\n";
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }
  if (!check_arg.empty()) {
    ThemePresets store(ThemePresets::Options{presets_dir, false, shipped_dir + "/themes"});
    PresetLoadReport rep;
    std::optional<ThemePreset> p = store.get(check_arg, rep);
    if (!p) { std::fprintf(stderr, "%s\n", rep.error.c_str()); return 2; }
    int rc = 0;
    for (ThemeMode m : {ThemeMode::Dark, ThemeMode::Light}) {
      ThemeLoadReport tr;
      std::optional<Theme> t = resolve_colours(*p, m, tr);
      if (!t) { std::fprintf(stderr, "%s\n", tr.error.c_str()); return 2; }
      const ThemeReport r = analyse(*t);
      std::printf("== %s, %s variant ==\n%s", check_arg.c_str(), m == ThemeMode::Dark ? "dark" : "light", report_text(r).c_str());
      const std::vector<std::string> failed = check_claims(*t, r);
      for (const std::string& f : failed) { std::printf("CLAIM FAILED: %s\n", f.c_str()); rc = 1; }
      if (!t->meta.get("badges").is_array()) std::printf("(no badges claimed)\n");
      else if (failed.empty()) std::printf("every claimed badge holds\n");
      std::printf("\n");
      // A colours object without pairs is the same at both modes: one report is enough.
      if (json::dump(p->colours, 0).find("\"dark\"") == std::string::npos) break;
    }
    return rc;
  }
  if (app.fixture_path.empty()) return usage();
  if (!app.load_fixture()) { std::fprintf(stderr, "cannot read %s\n", app.fixture_path.c_str()); return 1; }
  // The preset store: the studio is a rolltui host, with the editor's privilege
  // (it writes what ships). Under --frame nothing autosaves.
  app.store = std::make_shared<ThemePresets>(ThemePresets::Options{presets_dir, true, shipped_dir + "/themes"});
  app.lstore = std::make_shared<LayoutPresets>(LayoutPresets::Options{presets_dir, true, shipped_dir + "/layouts"});
  app.bstore = std::make_shared<BindingsPresets>(BindingsPresets::Options{presets_dir, true, shipped_dir + "/bindings"});
  app.persist = frame_spec.empty();
  app.windows.set_dir(presets_dir);  // a layout's `file:` paths are relative to the preset directory
  {
    const MigrationReport mig = migrate_theme_layout(presets_dir);  // Phase 10 m1, once
    for (const std::string& n : mig.notes) std::fprintf(stderr, "rolltui: %s\n", n.c_str());
    if (!mig.error.empty()) std::fprintf(stderr, "rolltui: %s\n", mig.error.c_str());
    const PresetLoadReport start = app.store->start();
    if (!start.error.empty()) app.theme_note = start.error;
    const PresetLoadReport lstart = app.lstore->start();
    if (!lstart.error.empty()) app.layout_note = lstart.error;
    // A Phase 9 layout's contents rewritten to kind[:source] (Phase 10 m2) is a note,
    // not a problem — said once, on stderr, so it never moves a golden frame.
    for (const std::string& n : lstart.notes) std::fprintf(stderr, "rolltui: %s\n", n.c_str());
    const PresetLoadReport bstart = app.bstore->start();
    if (!bstart.error.empty()) app.hint = bstart.error;
    App::say_migrations(bstart);  // a renamed action in the user's own working copy, rewritten once
  }
  app.load_theme_arg();
  app.load_bindings_arg();
  app.refresh_menu();

  if (!frame_spec.empty()) {
    int w, h;
    if (!parse_size(frame_spec, w, h)) return usage();
    app.resize(w, h);
    app.load_layout_arg();
    app.sync_look();
    app.ensure_layout();
    run_steps(app, scripted_keys(keys_spec, w, h));
    Frame f = app.render(false);
    if (frame_sgr) {
      std::string bytes = render_full(f, app.depth);
      // A screenshot, not a screen: strip the cursor/clear preamble so it cats cleanly.
      std::fwrite(bytes.data(), 1, bytes.size(), stdout);
      std::printf("\x1b[0m\n");
    } else {
      print_frame_plain(f);
    }
    if (app.copied_any) std::printf("--- copied ---\n%s\n", app.copied.c_str());
    if (!dump_role.empty()) {
      const Role r = role_from_name(dump_role);
      if (r == Role::count_) { std::fprintf(stderr, "no role named %s\n", dump_role.c_str()); return 1; }
      std::printf("--- role ---\n%s\n", style_dump(dump_role, app.theme.style(r)).c_str());
    }
    return 0;
  }

  Terminal term(STDIN_FILENO, STDOUT_FILENO);
  if (!term.is_tty()) { std::fprintf(stderr, "not a terminal; use --frame WxH\n"); return 1; }
  if (!app.mode_flag && app.store->working().mode == "auto") {
    const std::optional<Color> bg = term.query_background(150);
    app.detected_mode = bg ? mode_for_background(*bg) : ThemeMode::Dark;
  }
  app.resize(term.width(), term.height());
  app.load_layout_arg();
  app.sync_look();
  app.ensure_layout();
  run_steps(app, scripted_keys(keys_spec, app.w, app.h));
  Frame prev;
  bool have_prev = false;
  bool running = true;
  while (running) {
    app.maybe_reload_theme();
    app.maybe_reload_layout();
    Frame f = app.render(true);
    term.write(render_diff(have_prev ? &prev : nullptr, f, app.depth));
    prev = std::move(f);
    have_prev = true;
    const bool ticking = app.transcript().wants_tick();
    for (const Event& e : term.poll(ticking ? 50 : 250)) {
      app.clock_ms = now_ms();
      if (const ResizeEvent* r = std::get_if<ResizeEvent>(&e)) {
        app.resize(r->w, r->h);
        have_prev = false;
        continue;
      }
      if (const KeyEvent* k = std::get_if<KeyEvent>(&e); k && k->key == Key::Char && k->ctrl && k->ch == 'l')
        have_prev = false;
      if (!app.handle(e)) { running = false; break; }
    }
    if (ticking) app.tick();
  }
  return 0;
}
