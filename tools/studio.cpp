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
// WIDGETS BY KIND, SOURCES BY NAME (Phase 10 m2): a window's "content" is
// `kind[:source]` from the library's table, and the library's window table
// instantiates the widget and draws it — the studio only BINDS what is its own,
// by name: the fixture document as `session` (transcript:session), its facts as
// `status` (rows:status), the prompt as `prompt` (input:prompt), and its three
// composites, registered as the kinds `editor`, `confirm` and `report`. `help`, `text:<literal>`
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
// gestures work inside the input. Every event goes through the window stack's
// routing, so what the studio does is what a host would do. The status line shows
// theme, layout, size, scroll position, focus and the last frame's render time
// (instrumented from the first line — a slow frame is a number, not a feeling).
//
// ============================================================================================
// PHASE 17 m3: THIS FILE CALLS THE C DIRECTLY, following `rolltui-paint`'s lead (the
// library's first host to do so, m3's worked example). What changed from the C++-shim
// version, in the same three shapes `paint.cpp`'s own header names:
//
//   1. **NO PER-FRAME FRAME.** `rolltui_swap` owns both frames for the run and lends the
//      back one per repaint; `Frame prev; bool have_prev;` is gone. The studio had TWO
//      invalidation sites (a resize, and Ctrl-L's explicit repaint) — both are now
//      `rolltui_swap_invalidate(swap)`, which stays the host's policy exactly as before.
//   2. **APP-LIFETIME HANDLES ARE PLAIN MEMBERS RELEASED IN ONE DESTRUCTOR.** `App` owns
//      its own resources directly (no `rolltui::` wrapper classes); a missed release leaks
//      once and `rolltui_shutdown`'s `live_bytes == 0` catches it.
//   3. **THE THREE PRESET STORES ARE BUILT FROM THE C API DIRECTLY** (`rolltui_preset_store_new`
//      plus a domain descriptor per store), the same mechanics `rolltui/tests/presets_test.cpp`
//      already drives with no C++ in the chain — `PresetStore<Domain>`/`Presets.hpp`'s three
//      domain traits are C++ this file no longer depends on.
//
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_app_profile.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_diff.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_presets.h"
#include "rolltui/c/rolltui_render.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_widgets.h"
#include "tool_str.hpp"
#include "keys_editor.hpp"
#include "layout_editor.hpp"
#include "theme_editor.hpp"
#include "tool_actions.hpp"

using rolltui::tools::KeysEditor;
using rolltui::tools::LayoutEditor;
using rolltui::tools::ThemeEditor;
using rolltui::tools::builtin_layout;

namespace {

constexpr std::size_t kRoleCount = ROLLTUI_ROLE_COUNT;

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

void put_str(void* ctx, const char* s, std::size_t len) { static_cast<std::string*>(ctx)->append(s, len); }

std::string color_to_string(RolltuiStyleColor c) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  const std::size_t n = rolltui_color_to_string(c, buf, sizeof buf);
  return std::string(buf, n);
}
// A BORROW of the library's literal, which is what the C hands back; it was copied into a
// std::string per call until 2026-09-06, on the status line and the rows source every frame.
std::string_view depth_name(unsigned char d) {
  std::size_t n = 0;
  const char* p = rolltui_color_depth_name(d, &n);
  return std::string_view(p, n);
}
// A number appended in place — std::to_string's temporary, without the temporary.
std::optional<unsigned char> mode_from_setting(std::string_view s) {
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

// ---- the preset stores studio builds directly from the C API (Phase 17 m3) ----------------
// `rolltui::PresetStore<Domain>` and `Presets.hpp`'s three domain traits are the C++ ADAPTER
// this file no longer needs. The three domain descriptors are the LIBRARY's since Phase 18 m3
// (`rolltui_preset_domain`): this file had assembled them itself, identically to roll and to
// three tests, and — unlike roll — never released their parsed cache.

// ---- the three domain-specific report shapes, as RAII over the transparent C structs -------
struct ThemePresetReport : RolltuiThemePresetReport {
  ThemePresetReport() : RolltuiThemePresetReport{} {}
  ThemePresetReport(const ThemePresetReport&) = delete;
  ~ThemePresetReport() { rolltui_theme_preset_report_release(this); }
  // Phase 17 m3: the JUDGEMENT is the library's — roll had written the identical six.
  bool clean() const { return rolltui_theme_preset_report_clean(this) != 0; }
  std::string summary() const {
    RolltuiStr out{};
    rolltui_theme_preset_report_summary(this, &out);
    std::string s = str_of(out);
    rolltui_str_free(&out);
    return s;
  }
};
struct LayoutPresetReport : RolltuiLayoutPresetReport {
  LayoutPresetReport() : RolltuiLayoutPresetReport{} {}
  LayoutPresetReport(const LayoutPresetReport&) = delete;
  ~LayoutPresetReport() { rolltui_layout_preset_report_release(this); }
  bool clean() const { return rolltui_layout_preset_report_clean(this) != 0; }
  std::string summary() const {
    RolltuiStr out{};
    rolltui_layout_preset_report_summary(this, &out);
    std::string s = str_of(out);
    rolltui_str_free(&out);
    return s;
  }
};
struct BindingsPresetReport : RolltuiBindingsPresetReport {
  BindingsPresetReport() : RolltuiBindingsPresetReport{} {}
  BindingsPresetReport(const BindingsPresetReport&) = delete;
  ~BindingsPresetReport() { rolltui_bindings_preset_report_release(this); }
  bool clean() const { return rolltui_bindings_preset_report_clean(this) != 0; }
  std::string summary() const {
    RolltuiStr out{};
    rolltui_bindings_preset_report_summary(this, &out);
    std::string s = str_of(out);
    rolltui_str_free(&out);
    return s;
  }
};

struct PresetStoreOptions {
  std::string dir;
  bool may_write_shipped = false;
  std::string shipped_dir;
};

class PresetStoreBase {
 public:
  PresetStoreBase(RolltuiPresetStore* s, PresetStoreOptions opt) : s_(s), opt_(std::move(opt)) {}
  PresetStoreBase(const PresetStoreBase&) = delete;
  ~PresetStoreBase() { rolltui_preset_store_free(s_); }

  std::string origin() const {
    std::size_t n = 0;
    const char* p = rolltui_preset_store_origin(s_, &n);
    return std::string(p, n);
  }
  bool modified() const { return rolltui_preset_store_modified(s_) != 0; }
  std::string label() const {
    RolltuiStr out{};
    rolltui_preset_store_label(s_, &out);
    std::string v = str_of(out);
    rolltui_str_free(&out);
    return v;
  }
  std::uint64_t version() const { return rolltui_preset_store_version(s_); }
  // A frame's form: REPLACED into a buffer the caller keeps. `label()` above is an event's.
  void label(RolltuiStr& out) const { rolltui_preset_store_label(s_, &out); }
  void list(RolltuiPresetList& out) const { rolltui_preset_store_list(s_, &out); }
  int save_as(std::string_view name, bool overwrite, RolltuiStr& error) {
    return rolltui_preset_store_save_as(s_, name.data(), name.size(), overwrite ? 1 : 0, &error);
  }
  const PresetStoreOptions& options() const { return opt_; }
  RolltuiPresetStore* handle() const { return s_; }

 protected:
  RolltuiPresetStore* s_;
  PresetStoreOptions opt_;
};

// A move-only borrow of a Theme domain value: the store hands back a fresh CLONE on every
// `working()`/`get()`, and this is what frees it.
struct ThemeValueHandle {
  const RolltuiPresetStore* s;
  RolltuiThemePresetValue* v;
  ThemeValueHandle(const RolltuiPresetStore* store, void* p) : s(store), v(static_cast<RolltuiThemePresetValue*>(p)) {}
  ThemeValueHandle(const ThemeValueHandle&) = delete;
  ThemeValueHandle(ThemeValueHandle&& o) noexcept : s(o.s), v(o.v) { o.v = nullptr; }
  ~ThemeValueHandle() { rolltui_preset_store_value_free(s, v); }
  const RolltuiThemePresetValue* operator->() const { return v; }
  explicit operator bool() const { return v != nullptr; }
};

// THE STUDIO'S ONE SESSION (Phase 25). A host owns its context; this binary runs one screen at
// a time, and the preset-store wrappers below reach it from class statics that exist before
// `App` does — so it is a function-local static freed at exit rather than an `App` member.
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

class ThemeStore : public PresetStoreBase {
  static RolltuiPresetStore* make(const std::string& dir, bool may_write_shipped, const std::string& shipped_dir) {
    return rolltui_preset_store_new(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_THEME), dir.data(), dir.size(),
                                    may_write_shipped ? 1 : 0, shipped_dir.data(), shipped_dir.size());
  }

 public:
  ThemeStore(std::string dir, bool may_write_shipped = false, std::string shipped_dir = "")
      : PresetStoreBase(make(dir, may_write_shipped, shipped_dir), {dir, may_write_shipped, shipped_dir}) {}

  void start(ThemePresetReport& rep) { rolltui_preset_store_start(s_, &rep); }
  ThemeValueHandle working() const { return ThemeValueHandle(s_, rolltui_preset_store_working(s_)); }
  void set_colours(RolltuiJsonValue* colours, bool persist = true) {
    ThemeValueHandle v = working();
    RolltuiThemePresetValue* raw = v.v;
    v.v = nullptr;
    rolltui_json_free(raw->colours);
    raw->colours = colours;
    rolltui_preset_store_set_working(s_, raw, persist ? 1 : 0);
  }
  ThemeValueHandle get(std::string_view name, ThemePresetReport& rep) const {
    return ThemeValueHandle(s_, rolltui_preset_store_get(s_, name.data(), name.size(), &rep));
  }
  bool load(std::string_view name, ThemePresetReport& rep, bool persist = true) {
    return rolltui_preset_store_load(s_, name.data(), name.size(), &rep, persist ? 1 : 0) != 0;
  }

  static bool is_shipped(std::string_view name) {
    return rolltui_preset_is_shipped(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_THEME), name.data(), name.size()) != 0;
  }
  static std::vector<std::string> shipped_names() {
    RolltuiStrList names;
    rolltui_preset_shipped_names(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_THEME), &names);
    std::vector<std::string> out;
    for (const RolltuiStr& n : names) out.push_back(str_of(n));
    return out;
  }
};

class LayoutStore : public PresetStoreBase {
  static RolltuiPresetStore* make(const std::string& dir, bool may_write_shipped, const std::string& shipped_dir) {
    return rolltui_preset_store_new(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_LAYOUT), dir.data(), dir.size(),
                                    may_write_shipped ? 1 : 0, shipped_dir.data(), shipped_dir.size());
  }

 public:
  LayoutStore(std::string dir, bool may_write_shipped = false, std::string shipped_dir = "")
      : PresetStoreBase(make(dir, may_write_shipped, shipped_dir), {dir, may_write_shipped, shipped_dir}) {}

  void start(LayoutPresetReport& rep) { rolltui_preset_store_start(s_, &rep); }
  RolltuiLayout working() const {
    void* v = rolltui_preset_store_working(s_);
    RolltuiLayout out = (*static_cast<RolltuiLayout*>(v)).clone();
    rolltui_preset_store_value_free(s_, v);
    return out;
  }
  void set_working(const RolltuiLayout& l, bool persist = true) {
    RolltuiLayout* v = new RolltuiLayout();
    rolltui_layout_copy(v, &l);
    rolltui_preset_store_set_working(s_, v, persist ? 1 : 0);
  }
  bool load(std::string_view name, LayoutPresetReport& rep, bool persist = true) {
    return rolltui_preset_store_load(s_, name.data(), name.size(), &rep, persist ? 1 : 0) != 0;
  }

  static bool is_shipped(std::string_view name) {
    return rolltui_preset_is_shipped(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_LAYOUT), name.data(), name.size()) != 0;
  }
};

// A move-only owning `RolltuiBindings*`.
struct BindingsHandle {
  RolltuiBindings* b;
  explicit BindingsHandle(RolltuiBindings* p) : b(p) {}
  BindingsHandle(const BindingsHandle&) = delete;
  BindingsHandle(BindingsHandle&& o) noexcept : b(o.b) { o.b = nullptr; }
  ~BindingsHandle() { rolltui_bindings_free(b); }
  RolltuiBindings* handle() const { return b; }
  explicit operator bool() const { return b != nullptr; }
};

class BindingsStore : public PresetStoreBase {
  static RolltuiPresetStore* make(const std::string& dir, bool may_write_shipped, const std::string& shipped_dir) {
    return rolltui_preset_store_new(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_BINDINGS), dir.data(), dir.size(),
                                    may_write_shipped ? 1 : 0, shipped_dir.data(), shipped_dir.size());
  }

 public:
  BindingsStore(std::string dir, bool may_write_shipped = false, std::string shipped_dir = "")
      : PresetStoreBase(make(dir, may_write_shipped, shipped_dir), {dir, may_write_shipped, shipped_dir}) {}

  void start(BindingsPresetReport& rep) { rolltui_preset_store_start(s_, &rep); }
  BindingsHandle working() const { return BindingsHandle(static_cast<RolltuiBindings*>(rolltui_preset_store_working(s_))); }
  void set_working(RolltuiBindings* b, bool persist = true) { rolltui_preset_store_set_working(s_, b, persist ? 1 : 0); }
  std::optional<BindingsHandle> get(std::string_view name, BindingsPresetReport& rep) const {
    void* v = rolltui_preset_store_get(s_, name.data(), name.size(), &rep);
    if (!v) return std::nullopt;
    return BindingsHandle(static_cast<RolltuiBindings*>(v));
  }
  bool load(std::string_view name, BindingsPresetReport& rep, bool persist = true) {
    return rolltui_preset_store_load(s_, name.data(), name.size(), &rep, persist ? 1 : 0) != 0;
  }

  static std::vector<std::string> shipped_names() {
    RolltuiStrList names;
    rolltui_preset_shipped_names(rolltui_preset_domain(studio_ctx(), ROLLTUI_PRESET_DOMAIN_BINDINGS), &names);
    std::vector<std::string> out;
    for (const RolltuiStr& n : names) out.push_back(str_of(n));
    return out;
  }
};

RolltuiDocument parse_fixture(const std::string& text) {
  RolltuiDocument doc;
  std::string kind = "assistant", summary;
  unsigned char state = ROLLTUI_EFFECT_STATE_NONE;  // m6: what the NEXT entry is doing, if anything
  double fraction = 0;
  std::string buf;
  int n = 0;
  auto flush = [&]() {
    // Trim leading/trailing blank lines of the entry.
    std::size_t a = buf.find_first_not_of("\n");
    std::size_t b = buf.find_last_not_of("\n");
    std::string body = (a == std::string::npos) ? "" : buf.substr(a, b - a + 1);
    if (body.empty()) { buf.clear(); return; }
    RolltuiDocEntry* e = rolltui_document_add(&doc);
    set_str(e->id, "e" + std::to_string(n++));
    set_str(e->text, body);
    if (kind == "user") { e->markdown = 0; e->role = to_role(ROLLTUI_ROLE_TEXT); e->prefix = "> "; e->prefix_role = to_role(ROLLTUI_ROLE_PROMPT); }
    else if (kind == "note") { e->markdown = 0; e->role = to_role(ROLLTUI_ROLE_NOTE); }
    else if (kind == "tool") { e->markdown = 0; e->role = to_role(ROLLTUI_ROLE_TEXT_MUTED); e->foldable = 1; set_str(e->summary, summary); e->folded = 1; }
    else { e->markdown = 1; e->role = to_role(ROLLTUI_ROLE_TEXT); }
    // m6: a marked entry. The fixture says WHICH STATE and nothing else.
    e->state = to_effect_state(state);
    e->progress = fraction;
    state = ROLLTUI_EFFECT_STATE_NONE;
    fraction = 0;
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
    if (line.rfind("<!-- state:", 0) == 0 && line.size() >= 15 && line.compare(line.size() - 3, 3, "-->") == 0) {
      flush();
      kind = "assistant";
      std::string spec = line.substr(11, line.size() - 14);
      const std::size_t a = spec.find_first_not_of(' ');
      spec = (a == std::string::npos) ? "" : spec.substr(a);
      const std::size_t sp = spec.find(' ');
      const std::string name = spec.substr(0, sp);
      const int st = rolltui_effect_state_from_name(name.data(), name.size());
      state = st < 0 ? ROLLTUI_EFFECT_STATE_NONE : static_cast<unsigned char>(st);
      fraction = sp == std::string::npos ? 0 : std::strtod(spec.c_str() + sp + 1, nullptr);
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

struct App;
// Forward declared so `App::bind_windows()` (defined inline, inside the class) can register
// them; the plugin bodies are defined after `App` since they call its methods.
RolltuiWidget editor_factory(void* ctx, const char* content, std::size_t len);
RolltuiWidget confirm_factory(void* ctx, const char* content, std::size_t len);
RolltuiWidget report_factory(void* ctx, const char* content, std::size_t len);

struct App {
  RolltuiContext* ctx = studio_ctx();  // BORROWED: the binary's one session (see studio_ctx)
  std::string fixture_path, theme_arg, layout_arg;
  std::optional<unsigned char> mode_flag;   // --mode; else the working copy's mode
  unsigned char mode = ROLLTUI_MODE_DARK;     // the variant in use this frame
  unsigned char detected_mode = ROLLTUI_MODE_DARK;  // OSC 11's answer (interactive), dark otherwise
  unsigned char depth = ROLLTUI_DEPTH_TRUECOLOR;
  bool ambiguous = false;
  // Phase 12 m5b: this host's own thresholds for a long code block, overridable with
  // --code-fold so a golden can exercise the cap without a hundred-line fixture.
  int code_fold_over = 30, code_cap = 100;
  int w = 80, h = 24;  // the screen
  RolltuiDocument doc;
  // The look comes from the preset store's Theme working copy, exactly as in roll; the
  // editor, when open, previews its own current theme.
  std::unique_ptr<ThemeStore> store;
  std::unique_ptr<LayoutStore> lstore;    // the Layout working copy (Phase 10 m1)
  std::unique_ptr<BindingsStore> bstore;  // the Bindings working copy (milestone 17)
  RolltuiBindings* bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));  // what this frame runs on
  std::uint64_t bstore_seen = 0;
  std::string bindings_arg;
  bool persist = true;                  // false under --frame: the working copy is never written
  std::uint64_t store_seen = 0;
  RolltuiStyle resolved_styles[ROLLTUI_ROLE_COUNT]{};  // the working copy's colours at `mode`
  std::string resolved_name;
  // Per-frame text, held and REFILLED rather than rebuilt: the status line, the editors'
  // "preset:" line, the rows' layout cell and the two store labels, so a warm frame allocates
  // nothing for them (the finding of 2026-09-06, measured on roll's status panel first).
  std::string status_line, editor_line, layout_row, editor_status;
  RolltuiStr theme_label_str, keys_label_str;
  RolltuiStyle theme_styles[ROLLTUI_ROLE_COUNT]{};     // what this frame draws with (resolved, or the editor's preview)
  RolltuiEffectMap* effects_map = nullptr;             // OWNED: the resolved theme's effects
  std::uint64_t lstore_seen = 0;
  std::string theme_note;
  long theme_mtime = -1;
  RolltuiLayout layout{};
  std::string layout_note;
  long layout_mtime = -1;
  bool stacked_fallback = false;
  RolltuiLayout stacked_layout_ = builtin_layout(studio_ctx(), "stacked");  // cached: the shipped fallback screen
  // The theme editor (milestone 14) and the layout editor (milestone 16) share the
  // side popup; one is open at a time.
  enum class EditorMode { None, Theme, Layout, Keys };
  EditorMode editor_mode = EditorMode::None;
  ThemeEditor teditor{ctx};
  LayoutEditor leditor{ctx};  // resolves kinds against this session
  KeysEditor keditor{ctx};
  bool editor_open = false;
  std::string pending_save;             // a save-as awaiting its overwrite confirmation
  std::string confirm_text;
  std::function<void()> confirm_action;
  std::string report_text_;  // the Check popup's text
  int report_top = 0;        // the Check report is a registered kind: the studio scrolls it
  int report_lines = 0;      // its wrapped length, from the last draw
  std::string hint;
  std::string window_note;   // a window that cannot draw (an unbound source, a bad kind)
  bool show_timing = false;  // the frame-time row/field (interactive only)
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  // Every window's widget comes from its content: the studio binds the fixture document,
  // its status rows, the prompt and its own composites by name, and never asks what a
  // slot means.
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiTranscript* transcript() { return rolltui_windows_transcript(windows, "session", 7); }
  RolltuiInput* editor() { return rolltui_windows_input(windows, "prompt", 6); }
  RolltuiMenu* menu() { return rolltui_windows_menu(windows, "main", 4); }  // menus/main.json (Phase 10 m3)
  void set_menu_value(std::string_view id, std::string_view v) {
    rolltui_menu_set_value(menu(), id.data(), id.size(), v.data(), v.size());
  }
  int submitted = 0;        // entries the input added to the document
  std::string copied;       // the last copy (the studio has no clipboard)
  // The TARGET app being authored for (Phase 11 m4), or nullptr: the studio previews as
  // itself. It is the studio's own state and never the library's — a profile describes an
  // app, and only a tool that is authoring FOR one has any use for it.
  RolltuiAppProfile* profile = nullptr;  // OWNED, nullable
  bool copied_any = false;
  std::uint64_t clock_ms = 0;  // the clock handed to the widgets (real or scripted)
  // m6: the clock EFFECTS are applied at, kept apart from clock_ms on purpose — the
  // widgets' clock is scripted (a click pair is one second after the last), and motion
  // wants the real one interactively and `--tick N` under --frame. One field each beats
  // one field meaning two things at two times.
  std::uint64_t effect_ms = 0;
  long last_frame_us = 0;
  // What the last frame's marks came to: an unknown kind is said, not swallowed. Read by
  // the STATUS LINE before it is overwritten (one frame stale, on purpose — see render_into).
  std::vector<std::string> effects_unknown_kinds;
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
    rolltui_windows_set_library_defaults(windows);
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
    rolltui_app_profile_free(profile);
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
    // The context is NOT freed here: it is the binary's (see `studio_ctx`), not this App's,
    // and the store wrappers' statics outlive any one App.
  }

  const RolltuiStyle& style(unsigned char role) const { return *rolltui_theme_style(theme_styles, kRoleCount, role); }
  int put_text(RolltuiFrame* f, int x, int y, std::string_view s, RolltuiStyle sty, int max_cells) {
    return rolltui_frame_put_text(f, draw_scratch, x, y, s.data(), s.size(), sty, max_cells, ambiguous ? 1 : 0, 0);
  }
  void fill(RolltuiFrame* f, RolltuiRect r, RolltuiStyle sty) { rolltui_frame_fill(f, draw_scratch, r, sty, nullptr, 0); }
  void tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle sty) { rolltui_frame_tint(f, r, sty); }

  // The action of `scope` this chord serves, or "" — a BORROW valid until the table next
  // changes, which every call site below reads before it can.
  std::string_view action_for(const RolltuiChord& k, std::string_view scope) {
    std::size_t n = 0;
    const char* p = rolltui_bindings_action_for(bindings, &k, scope.data(), scope.size(), &n);
    return p ? std::string_view(p, n) : std::string_view();
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
        windows, "prompt", 6,
        [](void* ctx, const char* text, std::size_t len) { static_cast<App*>(ctx)->append_prompt(std::string(text, len)); },
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
    // The studio's three composites are REGISTERED KINDS (Phase 11 m3), not draw callbacks
    // bound by name: each is a plugin the studio owns, its widget receives its own events,
    // and nothing below dispatches by window name. Each takes no source.
    rolltui_widget_kind_register(ctx, "editor", 6, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_windows_register_kind(windows, "editor", 6, editor_factory, this, nullptr);
    rolltui_widget_kind_register(ctx, "confirm", 7, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_windows_register_kind(windows, "confirm", 7, confirm_factory, this, nullptr);
    rolltui_widget_kind_register(ctx, "report", 6, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_windows_register_kind(windows, "report", 6, report_factory, this, nullptr);
    // Diff colouring (Phase 12 m5b): this host DECLARING that a ```diff fence in its
    // documents means a diff — never a sniff of what a block holds.
    rolltui_windows_set_highlight(windows, diff_highlight, diff_scratch, nullptr);
    constexpr const char* kMouseHelp =
        "mouse: drag selects (auto-scrolls past an edge); release copies; double-click a word; "
        "triple-click a line;\n"
        "click a folded block's summary to toggle it; in the layout editor a click selects, a "
        "drag on a seam resizes";
    rolltui_windows_set_help(windows, "", 0, kMouseHelp, std::strlen(kMouseHelp));
    rolltui_windows_clear_help_scopes(windows);
    for (const char* s : {"input", "transcript", "app", "editor", "studio", "stack"})
      rolltui_windows_add_help_scope(windows, s, std::strlen(s));
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
  void refresh_menu() {
    RolltuiMenuItemList themes, layouts;
    RolltuiPresetList tl, ll;
    if (store) { store->list(tl); for (const RolltuiPresetInfo& p : tl) themes.push_back(RolltuiMenuItem::action(std::string(str_of(p.name)).c_str(), std::string(str_of(p.name) + (p.shipped ? "" : "  (yours)")).c_str())); }
    if (lstore) { lstore->list(ll); for (const RolltuiPresetInfo& p : ll) layouts.push_back(RolltuiMenuItem::action(std::string(str_of(p.name)).c_str(), std::string(str_of(p.name) + (p.shipped ? "" : "  (yours)")).c_str())); }
    rolltui_menu_set_options(menu(), "theme", 5, &themes);
    rolltui_menu_set_options(menu(), "layout", 6, &layouts);
    set_menu_value("theme", store ? store->label() : "");
    set_menu_value("layout", lstore ? lstore->label() : str_of(layout.name));
    set_menu_value("depth", depth_name(depth));
    rolltui_menu_set_checked(menu(), "ambiguous", 9, ambiguous);
  }
  void open_menu(bool palette) {
    if (rolltui_window_stack_has_popup(stack, "menu", 4)) { close_popup("menu"); return; }
    refresh_menu();  // presets and layout files may have changed
    rolltui_menu_reset(menu());
    rolltui_menu_set_palette(menu(), palette ? 1 : 0);
    rolltui_window_stack_push_popup(stack, &effective_layout(), "menu", 4);
  }
  void close_popup(const std::string& id) {
    while (rolltui_window_stack_depth(stack) > 1) {
      const RolltuiLayer* top = rolltui_window_stack_layer(stack, rolltui_window_stack_depth(stack) - 1);
      if (view_of(top->id) == id) break;
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
        if (ev.id == "theme") { theme_arg.clear(); ThemePresetReport rep; if (!store->load(str_of(ev.value), rep, persist)) hint = str_of(rep.error); else hint = rep.summary(); }
        else if (ev.id == "layout") { layout_arg.clear(); LayoutPresetReport rep; if (!lstore->load(str_of(ev.value), rep, persist)) hint = str_of(rep.error); else hint = rep.summary(); }
        else if (ev.id == "depth") depth = rolltui_detect_color_depth(nullptr, nullptr, ev.value.c_str());
        return true;
      case ROLLTUI_MENU_EVENT_TOGGLE:
        if (ev.id == "ambiguous") ambiguous = ev.checked != 0;
        return true;
      case ROLLTUI_MENU_EVENT_ACTIVATE:
        close_popup("menu");
        if (ev.id == "reload") load_fixture();
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
    if (!store->load(theme_arg, rep, /*persist=*/false)) { theme_note = str_of(rep.error); return false; }
    theme_note = rep.summary();
    if (rep.colours.missing_roles_n != 0) theme_note = std::to_string(rep.colours.missing_roles_n) + " roles missing (inherit text)";
    return true;
  }
  void maybe_reload_theme() {
    if (theme_arg.empty() || ThemeStore::is_shipped(theme_arg)) return;
    if (theme_arg.find('/') == std::string::npos && theme_arg.find(".json") == std::string::npos) return;
    long m = mtime_of(theme_arg);
    if (m != theme_mtime) load_theme_arg();
  }
  bool load_layout_arg() {
    if (!layout_arg.empty()) {
      LayoutPresetReport rep;
      layout_mtime = mtime_of(layout_arg);
      if (!lstore->load(layout_arg, rep, /*persist=*/false)) layout_note = str_of(rep.error);
      else {
        layout_note.clear();
        if (rep.layout.unknown_keys_n != 0) layout_note += "unknown: " + str_of(rep.layout.unknown_keys[0]) + "; ";
        if (rep.layout.bad_values_n != 0) layout_note += "bad: " + str_of(rep.layout.bad_values[0]) + "; ";
      }
    }
    sync_look();
    apply_layout();
    return layout_note.empty();
  }
  void maybe_reload_layout() {
    if (layout_arg.empty() || LayoutStore::is_shipped(layout_arg)) return;
    if (layout_arg.find('/') == std::string::npos && layout_arg.find(".json") == std::string::npos) return;
    long m = mtime_of(layout_arg);
    if (m != layout_mtime) load_layout_arg();
  }
  // Re-resolves the look from the working copy when the store changed; the editor's
  // preview wins while it is open.
  void sync_look() {
    if (store && store->version() != store_seen) {
      store_seen = store->version();
      ThemeValueHandle working = store->working();
      mode = mode_flag ? *mode_flag : mode_from_setting(view_of(working->mode)).value_or(detected_mode);
      RolltuiThemeReport rep{};
      RolltuiStyle new_styles[ROLLTUI_ROLE_COUNT]{};
      RolltuiStr new_name{};
      RolltuiEffectMap* new_effects = rolltui_theme_load(working->colours, mode, rolltui_theme_default_vocab(), new_styles, &new_name, &rep);
      if (new_effects) {
        std::copy(std::begin(new_styles), std::end(new_styles), resolved_styles);
        resolved_name = str_of(new_name);
        rolltui_effect_map_free(effects_map);
        effects_map = new_effects;
      } else {
        theme_note = "colours unusable: " + str_of(rep.error);
        RolltuiEffectMap* fallback = rolltui_theme_builtin_fill("default-dark", 12, resolved_styles, ROLLTUI_ROLE_COUNT);
        resolved_name = "default-dark";
        rolltui_effect_map_free(effects_map);
        effects_map = fallback;
      }
      rolltui_str_free(&new_name);
      rolltui_theme_report_release(&rep);
    }
    if (lstore && lstore->version() != lstore_seen) {
      lstore_seen = lstore->version();
      const RolltuiLayout working_layout = lstore->working();
      if (!(layout == working_layout)) { layout = working_layout.clone(); apply_layout(); }
    }
    // teditor.current() is a BORROW of just the styles now (the theme editor's own edit
    // buffer carries no name/effects) — this frame's own name/effects stay whatever the
    // preset store last resolved, which only the styles-driven role rendering reads.
    if (editor_mode == EditorMode::Theme) std::copy_n(teditor.current(), kRoleCount, theme_styles);
    else std::copy(std::begin(resolved_styles), std::end(resolved_styles), theme_styles);
    if (editor_mode == EditorMode::Layout && !(layout == leditor.current())) { layout = leditor.current().clone(); apply_layout(); }
    if (bstore && bstore->version() != bstore_seen) {
      bstore_seen = bstore->version();
      rolltui_bindings_free(bindings);
      bindings = rolltui_bindings_clone(bstore->working().handle());
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
    if (!bstore->load(bindings_arg, rep, /*persist=*/false)) { hint = str_of(rep.error); return false; }
    if (!rep.clean()) hint = "bindings: " + rep.summary();
    return true;
  }
  // ---- the theme editor (milestone 14) ----
  static RolltuiLayer editor_popup(const char* title) {
    RolltuiLayer l;
    l.id = "editor";
    l.placement = {RolltuiDim::rel(1), RolltuiDim::abs(0), RolltuiDim::abs(50), RolltuiDim::rel(1),
                   rolltui::Anchor::TopRight, true, RolltuiDim::abs(24), RolltuiDim::abs(6), {}, {}};
    l.modal = false;
    RolltuiLayoutNode n = RolltuiLayoutNode::window_id("editor", "editor");
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
                   rolltui::Anchor::Center, true, RolltuiDim::abs(30), RolltuiDim::abs(5), {}, {}};
    l.modal = true;
    RolltuiLayoutNode n = RolltuiLayoutNode::window_id("report", "report");
    n.border = rolltui::Border::Rounded;
    n.title = "report";
    n.focusable = true;
    n.background = to_role(ROLLTUI_ROLE_PANEL_BACKGROUND);
    l.root = (n).clone();
    return l;
  }
  static RolltuiLayer confirm_popup() {
    RolltuiLayer l;
    l.id = "confirm";
    l.placement = {RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(5),
                   rolltui::Anchor::Center, true, RolltuiDim::abs(20), {}, RolltuiDim::abs(70), {}};
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
    { ThemeValueHandle wc = store->working(); teditor.load(wc->colours, &rep); }
    rolltui_theme_report_release(&rep);
    std::vector<std::string> names, shipped;
    RolltuiPresetList pl;
    store->list(pl);
    for (const RolltuiPresetInfo& p : pl) names.push_back(str_of(p.name));
    for (const std::string& n : ThemeStore::shipped_names()) shipped.push_back(n);
    teditor.set_presets(names);
    teditor.set_shipped(shipped, store->options().may_write_shipped);
    teditor.set_mode(mode == ROLLTUI_MODE_DARK ? ROLLTUI_MODE_DARK : ROLLTUI_MODE_LIGHT);
    editor_open = true;
    editor_mode = EditorMode::Theme;
    { RolltuiLayer popup = editor_popup("theme editor"); rolltui_window_stack_push(stack, &popup); }
    sync_look();
  }
  void toggle_keys_editor() {
    if (editor_mode == EditorMode::Keys) { close_editor(); return; }
    close_editor();
    // The LIVE table, not the store's working copy: an action is editable here only if
    // something declared it. What this hands over is what the studio is actually running.
    keditor.load(bindings);
    std::vector<std::string> names, shipped;
    RolltuiPresetList pl;
    bstore->list(pl);
    for (const RolltuiPresetInfo& p : pl) names.push_back(str_of(p.name));
    for (const std::string& n : BindingsStore::shipped_names()) shipped.push_back(n);
    keditor.set_presets(names);
    keditor.set_shipped(shipped, bstore->options().may_write_shipped);
    editor_open = true;
    editor_mode = EditorMode::Keys;
    { RolltuiLayer popup = editor_popup("keys editor"); rolltui_window_stack_push(stack, &popup); }
    sync_look();
  }
  void keys_outcome(const KeysEditor::Outcome& o) {
    using K = KeysEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        bstore->set_working(rolltui_bindings_clone(keditor.committed()), persist);
        break;
      case K::SaveAs: {
        RolltuiStr err;
        const int r = bstore->save_as(o.value, pending_save == o.value, err);
        if (r == ROLLTUI_SAVE_EXISTS_ASK) { pending_save = o.value; hint = "bindings preset '" + o.value + "' exists; Enter the same name again to overwrite"; }
        else { pending_save.clear(); hint = r == ROLLTUI_SAVE_SAVED ? "saved bindings preset '" + o.value + "'" : str_of(err); }
        if (r == ROLLTUI_SAVE_SAVED) { std::vector<std::string> names; RolltuiPresetList pl; bstore->list(pl); for (const RolltuiPresetInfo& p : pl) names.push_back(str_of(p.name)); keditor.set_presets(names); }
        break;
      }
      case K::WriteShipped:
        ask("Write the SHIPPED bindings preset '" + o.value + "' into " + bstore->options().shipped_dir + "? (y/n)", [this, name = o.value] {
          RolltuiStr err;
          hint = bstore->save_as(name, true, err) == ROLLTUI_SAVE_SAVED ? "wrote shipped bindings preset '" + name + "' (rebuild to embed it)" : str_of(err);
        });
        break;
      case K::LoadPreset: {
        BindingsPresetReport rep;
        if (!bstore->load(o.value, rep, persist)) hint = str_of(rep.error);
        else { keditor.load(bstore->working().handle()); hint = rep.clean() ? "loaded bindings '" + o.value + "'" : "loaded '" + o.value + "' with problems: " + rep.summary(); }
        break;
      }
      case K::ResetLoaded:
        ask("Reset every binding to the preset '" + bstore->origin() + "'? (y/n)", [this] {
          BindingsPresetReport rep;
          if (std::optional<BindingsHandle> b = bstore->get(bstore->origin(), rep)) { keditor.replace(rolltui_bindings_clone(b->handle())); keys_outcome({K::Committed, {}}); hint = "reset (undoable)"; }
          else hint = str_of(rep.error);
        });
        break;
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
      bstore->label(keys_label_str);
      editor_line += view_of(keys_label_str);
      editor_line += " \xC2\xB7 Enter on an action, then press the chord";
      put_text(f, r.x, y++, editor_line, label, r.w);
    }
    if (y < r.y + r.h) { keditor.status_line(editor_status); put_text(f, r.x, y++, editor_status, keditor.capturing() ? style(ROLLTUI_ROLE_WARNING) : value, r.w); }
    if (y < r.y + r.h && !hint.empty()) put_text(f, r.x, y++, hint, style(ROLLTUI_ROLE_WARNING), r.w);
  }
  void toggle_layout_editor() {
    if (editor_mode == EditorMode::Layout) { close_editor(); return; }
    close_editor();
    leditor.load(lstore->working());
    std::vector<std::string> names;
    RolltuiPresetList pl;
    lstore->list(pl);
    for (const RolltuiPresetInfo& p : pl) names.push_back(str_of(p.name));  // shipped first, then the user's
    leditor.set_layouts(names);
    // Under a profile the offered contents are the TARGET APP's, which is what turns the
    // design editor's Source from a guess into a fact (Phase 11 m4). Without one they are
    // the studio's own, exactly as before.
    if (profile) {
      std::vector<std::string> contents;
      const std::size_t cn = rolltui_app_profile_content_count(profile);
      for (std::size_t i = 0; i < cn; ++i) {
        RolltuiStr s{};
        rolltui_app_profile_content_at(profile, i, &s);
        contents.push_back(str_of(s));
        rolltui_str_free(&s);
      }
      leditor.set_sources(contents);
    } else {
      leditor.set_sources({"transcript:session", "rows:status", "input:prompt", "text:pane", "editor"});
    }
    // …and so are the offered KINDS. Under a profile they are the library's plus the
    // TARGET's registered ones — never the studio's own `editor`/`confirm`/`report`,
    // which the app being authored for cannot build.
    std::vector<std::string> kinds;
    for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) {
      std::size_t n = 0;
      const char* p = rolltui_widget_kind_name(ctx, i, &n);
      kinds.emplace_back(p, n);
    }
    if (profile) {
      const std::size_t kn = rolltui_app_profile_kind_count(profile);
      for (std::size_t i = 0; i < kn; ++i) {
        std::size_t n = 0;
        const char* p = rolltui_app_profile_kind_name(profile, i, &n);
        kinds.emplace_back(p, n);
      }
    } else {
      for (const char* own : {"editor", "confirm", "report"}) kinds.emplace_back(own);
    }
    leditor.set_kinds(std::move(kinds));
    leditor.set_menus(menu_names());
    // The only thing a NEW layout inherits (Phase 11 m5): the TARGET's thresholds, which
    // are a fact about the app being designed for — never the open screen's.
    leditor.set_default_min(profile ? rolltui_app_profile_min_width(profile) : 0, profile ? rolltui_app_profile_min_height(profile) : 0);
    editor_open = true;
    editor_mode = EditorMode::Layout;
    { RolltuiLayer popup = editor_popup("layout editor"); rolltui_window_stack_push(stack, &popup); }
    sync_look();
  }
  // Every menu name a `menu:` window could resolve right now — the union of the preset
  // directory's menus/*.json, the host's own embedded ones, and the library's shipped ones,
  // deduplicated and sorted.
  std::vector<std::string> menu_names() const {
    std::vector<std::string> out;
    auto add = [&out](std::string name) {
      if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
    };
    std::size_t dir_len = 0;
    const char* dir_p = rolltui_windows_dir(windows, &dir_len);
    if (const std::string_view d(dir_p, dir_len); !d.empty()) {
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator(std::string(d) + "/menus", ec))
        if (e.path().extension() == ".json") add(e.path().stem().string());
    }
    for (std::size_t i = 0; i < rolltui_windows_host_menu_count(windows); ++i) {
      std::size_t n = 0;
      const char* p = rolltui_windows_host_menu_name_at(windows, i, &n);
      add(std::string(p, n));
    }
    // the library's own shipped menus (menus/*.json under presets/): the domain the
    // preset store's own directory listing does not otherwise reach.
    for (std::size_t i = 0; i < rolltui_kMenuCount; ++i) add(std::string(rolltui_kMenus[i].name));
    std::sort(out.begin(), out.end());
    return out;
  }
  void layout_outcome(const LayoutEditor::Outcome& o) {
    using K = LayoutEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        lstore->set_working(leditor.committed(), persist);
        break;
      case K::SaveAs: {
        // Through the Layout store, as the theme and keys editors already save: the store learns
        // the new origin (its label reads the new name, the working copy records it, as a manual
        // save does even under --frame) and the path rule stays the library's. Until 2026-09-06
        // this hand-built the path from the THEME store's options and wrote the file itself, so
        // the Layout store never learned the save happened.
        if (o.value.empty()) { hint = "a layout file needs a name"; break; }
        RolltuiLayout l = (leditor.committed()).clone();
        set_str(l.name, o.value);
        lstore->set_working(l, persist);
        RolltuiStr err;
        const int r = lstore->save_as(o.value, pending_save == o.value, err);
        if (r == ROLLTUI_SAVE_EXISTS_ASK) { pending_save = o.value; hint = "layout '" + o.value + "' exists; Enter the same name again to overwrite"; }
        else if (r != ROLLTUI_SAVE_SAVED) { pending_save.clear(); hint = str_of(err); }
        else {
          pending_save.clear();
          RolltuiStr path;
          rolltui_preset_store_preset_path(lstore->handle(), o.value.data(), o.value.size(), &path);
          hint = "saved layout file " + str_of(path);
          std::vector<std::string> names;
          RolltuiPresetList pl;
          lstore->list(pl);
          for (const RolltuiPresetInfo& p : pl) names.push_back(str_of(p.name));
          leditor.set_layouts(names);
        }
        break;
      }
      case K::LoadLayout: {
        LayoutPresetReport rep;
        if (!lstore->load(o.value, rep, persist)) hint = str_of(rep.error);
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
    std::vector<RolltuiResolvedNode> nodes;
    rolltui_resolve_tree(&rolltui_window_stack_base(stack)->root, layout_area(), layout_area(), 0, collect_resolved, &nodes);
    for (const RolltuiResolvedNode& rn : nodes)
      if (rn.node->is_window() && rn.outer.contains(x, y)) best = str_of(rn.node->id);
    return best;
  }
  // A seam: the shared edge between two visible siblings. The node that takes the new
  // size is the FIXED-size one when the other fills (dragging the fill would leave a
  // gap the fixed sibling never closes); otherwise the one before the seam.
  struct Seam { std::string id; bool after; bool horizontal; };
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
          return Seam{str_of((size_after ? next.id : child.id)), size_after, horizontal};
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
    if (editor_mode != EditorMode::Layout || rn.layer != 0 || !(view_of(rn.node->id) == leditor.selected())) return;
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
  void ask(std::string text, std::function<void()> action) {
    confirm_text = std::move(text);
    confirm_action = std::move(action);
    RolltuiLayer popup = confirm_popup();
    rolltui_window_stack_push(stack, &popup);
  }
  void editor_outcome(const ThemeEditor::Outcome& o) {
    using K = ThemeEditor::Outcome::Kind;
    switch (o.kind) {
      case K::None: case K::Changed: break;
      case K::Committed:
        store->set_colours(teditor.colours_json(store->origin()), persist);
        break;
      case K::SaveAs: {
        RolltuiStr err;
        const int r = store->save_as(o.value, pending_save == o.value, err);
        if (r == ROLLTUI_SAVE_EXISTS_ASK) { pending_save = o.value; hint = "preset '" + o.value + "' exists; Enter the same name again to overwrite"; }
        else { pending_save.clear(); hint = r == ROLLTUI_SAVE_SAVED ? "saved preset '" + o.value + "'" : str_of(err); }
        if (r == ROLLTUI_SAVE_SAVED) { std::vector<std::string> names; RolltuiPresetList pl; store->list(pl); for (const RolltuiPresetInfo& p : pl) names.push_back(str_of(p.name)); teditor.set_presets(names); }
        break;
      }
      case K::WriteShipped:
        ask("Write the SHIPPED preset '" + o.value + "' into " + store->options().shipped_dir + "? (y/n)", [this, name = o.value] {
          RolltuiStr err;
          hint = store->save_as(name, true, err) == ROLLTUI_SAVE_SAVED ? "wrote shipped preset '" + name + "' (rebuild to embed it)" : str_of(err);
        });
        break;
      case K::LoadPreset: {
        ThemePresetReport rep;
        if (!store->load(o.value, rep, persist)) hint = str_of(rep.error);
        else { RolltuiThemeReport tr{}; ThemeValueHandle wc = store->working(); teditor.load(wc->colours, &tr); rolltui_theme_report_release(&tr); hint = "loaded '" + o.value + "'"; }
        break;
      }
      case K::ResetLoaded:
        ask("Reset every role to the preset '" + store->origin() + "'? (y/n)", [this] {
          ThemePresetReport rep;
          if (ThemeValueHandle p = store->get(store->origin(), rep)) {
            RolltuiThemeReport tr{};
            RolltuiStyle dstyles[ROLLTUI_ROLE_COUNT]{}, lstyles[ROLLTUI_ROLE_COUNT]{};
            RolltuiStr dname{}, lname{};
            RolltuiEffectMap* deff = rolltui_theme_load(p->colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), dstyles, &dname, &tr);
            RolltuiEffectMap* leff = rolltui_theme_load(p->colours, ROLLTUI_MODE_LIGHT, rolltui_theme_default_vocab(), lstyles, &lname, &tr);
            if (deff && leff) {
              teditor.replace({std::to_array(dstyles), std::to_array(lstyles), str_of(dname), str_of(lname)}, deff);
              rolltui_effect_map_free(leff);
              editor_outcome({K::Committed, {}});
              hint = "reset to '" + store->origin() + "' (undoable)";
            } else {
              rolltui_effect_map_free(deff);
              rolltui_effect_map_free(leff);
            }
            rolltui_str_free(&dname);
            rolltui_str_free(&lname);
            rolltui_theme_report_release(&tr);
          } else hint = str_of(rep.error);
        });
        break;
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
    const int box = std::min(4, r.h);
    RolltuiRect m = r;
    m.h = r.h - box;
    RolltuiMenuOptions mo{};
    mo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_menu_set_options_struct(leditor.menu(), &mo);
    rolltui_menu_layout(leditor.menu(), m);
    if (m.h > 0) draw_raw_menu(leditor.menu(), f, rn.focused != 0);
    int y = r.y + m.h;
    const RolltuiStyle label = style(ROLLTUI_ROLE_LABEL), value = style(ROLLTUI_ROLE_VALUE);
    if (const std::string line = leditor.selection_line(); !line.empty() && y < r.y + r.h) put_text(f, r.x, y++, line, label, r.w);
    if (y < r.y + r.h) put_text(f, r.x, y++, "Tab next node \xC2\xB7 click selects \xC2\xB7 drag an edge resizes \xC2\xB7 Alt+arrows nudge", value, r.w);
    if (y < r.y + r.h) { leditor.status_line(editor_status); put_text(f, r.x, y++, editor_status, value, r.w); }
    if (y < r.y + r.h && !hint.empty()) put_text(f, r.x, y++, hint, style(ROLLTUI_ROLE_WARNING), r.w);
  }
  void draw_editor(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    if (editor_mode == EditorMode::Layout) { draw_layout_editor(rn, f); return; }
    if (editor_mode == EditorMode::Keys) { draw_keys_editor(rn, f); return; }
    RolltuiRect r = content_rect(rn);
    if (r.w <= 0 || r.h <= 0) return;
    const int box = std::min(6, r.h);
    RolltuiRect m = r;
    m.h = r.h - box;
    RolltuiMenuOptions mo{};
    mo.ambiguous_wide = ambiguous ? 1 : 0;
    rolltui_menu_set_options_struct(teditor.menu(), &mo);
    rolltui_menu_layout(teditor.menu(), m);
    if (m.h > 0) draw_raw_menu(teditor.menu(), f, rn.focused != 0);
    int y = r.y + m.h;
    const RolltuiStyle label = style(ROLLTUI_ROLE_LABEL), value = style(ROLLTUI_ROLE_VALUE);
    if (std::optional<unsigned char> role = teditor.focused_role()) {
      const RolltuiStyle& s = style(*role);
      std::size_t rn_len = 0;
      const char* rn_p = rolltui_role_name(*role, &rn_len);
      std::string line = std::string(rn_p, rn_len) + "  fg " + color_to_string(s.fg) + "  bg " + color_to_string(s.bg);
      for (const char* a : {"bold", "italic", "underline", "dim", "reverse"}) {
        const bool on = std::string_view(a) == "bold" ? s.bold : std::string_view(a) == "italic" ? s.italic : std::string_view(a) == "underline" ? s.underline : std::string_view(a) == "dim" ? s.dim : s.reverse;
        if (on) line += std::string("  ") + a;
      }
      if (y < r.y + r.h) put_text(f, r.x, y++, line, label, r.w);
      if (y < r.y + r.h) put_text(f, r.x, y++, " Aa  the quick brown fox â sample in this role ", s, r.w);
      if (y < r.y + r.h) {
        int x = r.x;
        x += put_text(f, x, y, "fg ", label, std::max(r.w - (x - r.x), 0));
        RolltuiStyle sw{}; sw.bg = s.fg; x += put_text(f, x, y, "      ", sw, std::max(r.w - (x - r.x), 0));
        x += put_text(f, x, y, "  bg ", label, std::max(r.w - (x - r.x), 0));
        RolltuiStyle sb{}; sb.bg = s.bg; x += put_text(f, x, y, "      ", sb, std::max(r.w - (x - r.x), 0));
        if (std::optional<RolltuiStyleColor> hc = teditor.highlighted_color()) {
          x += put_text(f, x, y, "  â¶ ", label, std::max(r.w - (x - r.x), 0));
          RolltuiStyle sh{}; sh.bg = *hc; put_text(f, x, y, "      ", sh, std::max(r.w - (x - r.x), 0));
        }
        ++y;
      }
    } else {
      if (y < r.y + r.h) {
        editor_line.assign("preset: ");
        store->label(theme_label_str);
        editor_line += view_of(theme_label_str);
        put_text(f, r.x, y++, editor_line, label, r.w);
      }
      if (y < r.y + r.h) put_text(f, r.x, y++, "Roles âº a role âº fg âº a colour; the transcript is the preview", value, r.w);
      if (y < r.y + r.h) put_text(f, r.x, y++, "type to filter Â· Enter commits Â· Esc cancels Â· Ctrl-Z / Ctrl-Y", value, r.w);
    }
    if (y < r.y + r.h) { teditor.status_line(editor_status); put_text(f, r.x, y++, editor_status, value, r.w); }
    if (y < r.y + r.h) put_text(f, r.x, y++, hint.empty() ? teditor.badges_line() : hint, hint.empty() ? label : style(ROLLTUI_ROLE_WARNING), r.w);
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
      put_text(f, r.x + indent, y++, std::string_view(text_p, text_n), style(ROLLTUI_ROLE_WARNING), std::max(r.w - indent, 0));
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
    declare_actions();
  }
  // The `app.*` actions are the LAYOUT's (Phase 10 m4): whatever the loaded file
  // declares, however it was loaded. `editor.*` and `studio.*` are the TOOLS' this
  // binary mounts (Phase 11 m1). Under a profile, the actions a MENU ITEM may name are
  // the target app's as well as this layout's.
  void declare_actions() {
    std::vector<RolltuiLayoutAction> declared;
    const RolltuiLayout& lay = effective_layout();
    for (std::size_t i = 0; i < lay.actions.size(); ++i) {
      RolltuiLayoutAction a{};
      a.name.assign(lay.actions[i].name);
      a.description.assign(lay.actions[i].description);
      declared.push_back(std::move(a));
    }
    if (profile) {
      const std::size_t n = rolltui_app_profile_action_count(profile);
      for (std::size_t i = 0; i < n; ++i) {
        std::size_t nlen = 0, dlen = 0;
        const char* name = rolltui_app_profile_action_name(profile, i, &nlen);
        const char* desc = rolltui_app_profile_action_description(profile, i, &dlen);
        const std::string_view name_sv(name, nlen);
        const bool exists = std::any_of(declared.begin(), declared.end(), [&](const RolltuiLayoutAction& d) { return view_of(d.name) == name_sv; });
        if (!exists) { RolltuiLayoutAction a{}; set_str(a.name, name_sv); a.description.assign(desc, dlen); declared.push_back(std::move(a)); }
      }
    }
    const std::vector<RolltuiToolAction>& tools = mounted_tools();
    rolltui_bindings_declare(bindings, declared.data(), declared.size(), tools.data(), tools.size());
  }
  // The tools this binary MOUNTS: the three editors, and its own three keys. A host that
  // mounted only the theme editor would list only that one.
  static const std::vector<RolltuiToolAction>& mounted_tools() {
    static const std::vector<RolltuiToolAction> all = [] {
      std::vector<RolltuiToolAction> out;
      auto add = [&out](std::span<const RolltuiToolAction> ts) { for (const RolltuiToolAction& a : ts) out.push_back(a); };
      add(rolltui::tools::editor_actions());
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
  bool load_fixture() {
    bool ok;
    std::string text = read_file(fixture_path, ok);
    if (!ok) return false;
    doc = parse_fixture(text);
    return true;
  }
  // The frame's terminal facts and clock, then: instantiate each window's widget from
  // its content, let the widgets that size their window do so, and lay them all out — so
  // an event is hit-tested against exactly the geometry the frame will draw.
  void ensure_layout() {
    RolltuiWidgetEnv env{static_cast<unsigned char>(ambiguous ? 1 : 0), clock_ms};
    rolltui_windows_set_env(windows, &env);
    rolltui_windows_set_bindings(windows, bindings);
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

  // The find bar (Phase 12 m4). It is an ordinary `input:` window in an ordinary popup —
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

  // ---- the sources the studio binds ----
  // `rows:status`: the studio's own facts. The widget draws them — this says only what
  // they are.
  void status_rows(RolltuiRows& out) {
    // Formatted on the stack or refilled into held strings; the rows copy once into their own
    // reused buffers. Built from std::string temporaries per frame until 2026-09-06.
    const std::size_t total = rolltui_transcript_total_lines(transcript());
    char b[64];
    if (store) { store->label(theme_label_str); out.add("theme", theme_label_str); } else out.add("theme", resolved_name.data(), resolved_name.size());
    if (bstore) { bstore->label(keys_label_str); out.add("keys", keys_label_str); } else out.add("keys", "default");
    layout_row.assign(view_of(effective_layout().name));
    if (stacked_fallback) layout_row += " (fallback)";
    out.add("layout", layout_row.data(), layout_row.size());
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
  void append_prompt(const std::string& text) {
    if (text.empty()) return;
    RolltuiDocEntry* e = rolltui_document_add(&doc);
    set_str(e->id, "input" + std::to_string(submitted++));
    set_str(e->text, text);
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
  int draw_scrolled_text(const RolltuiResolvedNode& rn, RolltuiFrame* f, std::string_view text, int top) {
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
      put_text(f, r.x + indent, y++, std::string_view(text_p, text_n), style(ROLLTUI_ROLE_TEXT), std::max(r.w - indent, 0));
    }
    const int below = total - std::max(top, 0) - r.h;
    char marker[ROLLTUI_MARKER_MAX];
    const std::size_t marker_len = rolltui_scroll_marker_text(below > 0 ? static_cast<std::size_t>(below) : 0, r.w, ambiguous ? 1 : 0, marker, sizeof marker);
    if (marker_len != 0) {
      const int mw = rolltui_u_display_width(u_scratch, marker, marker_len, ambiguous ? 1 : 0);
      put_text(f, r.x + std::max(r.w - mw, 0), r.y + r.h - 1, std::string_view(marker, marker_len), style(ROLLTUI_ROLE_SCROLL_MARKER), mw);
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
      // numbers formatted on the stack: a warm frame allocates nothing for it. It was rebuilt
      // by `+` per frame until 2026-09-06 — a dozen temporaries — and read the Theme store's
      // label through a deep compare each time.
      std::string& status = status_line;
      status.clear();
      status += ' ';
      if (store) { store->label(theme_label_str); status += view_of(theme_label_str); } else status += resolved_name;
      status += editor_mode == EditorMode::Theme ? " [theme editor]" : editor_mode == EditorMode::Layout ? " [layout editor]" : editor_mode == EditorMode::Keys ? " [keys editor]" : "";
      status += "  ";
      status += view_of(effective_layout().name);
      if (lstore && lstore->modified()) status += " (modified)";
      status += "  ";
      append_count(status, w);
      status += 'x';
      append_count(status, h);
      status += "  line ";
      append_count(status, total == 0 ? 0 : rolltui_transcript_top_line(transcript()) + 1);
      status += '/';
      append_count(status, total);
      if (anchor.follow) status += "  follow";
      status += "  ";
      status += depth_name(depth);
      status += "  focus:";
      if (focused) status += view_of(focused->id); else status += '-';
      if (with_timing) { status += "  "; append_count(status, last_frame_us); status += " us"; }
      // The match count and position: the widget owns finding, a host owns saying so.
      if (query_len != 0) {
        status += "  find ";
        append_count(status, rolltui_transcript_current_match_number(transcript()));
        status += '/';
        append_count(status, rolltui_transcript_match_count(transcript()));
      }
      if (copied_any) { status += "  copied "; append_count(status, copied.size()); status += 'B'; }
      if (stacked_fallback) {
        status += "  [stacked: below ";
        append_count(status, layout.min_width);
        status += 'x';
        append_count(status, layout.min_height);
        status += ']';
      }
      if (!theme_note.empty()) { status += "  ["; status += theme_note; status += ']'; }
      if (!layout_note.empty()) { status += "  ["; status += layout_note; status += ']'; }
      if (!window_note.empty()) { status += "  ["; status += window_note; status += ']'; }
      // m6: an effect kind no host registered is SAID. It cannot draw an error panel —
      // an effect has no window — so the status line is where it surfaces. One frame
      // stale, on purpose: it reads the PREVIOUS frame's `rolltui_effects_apply` result,
      // updated again below only after this line is drawn.
      if (!effects_unknown_kinds.empty()) { status += "  [no effect kind '"; status += effects_unknown_kinds[0]; status += "']"; }
      put_text(f, 0, h - 1, status, style(ROLLTUI_ROLE_LABEL), w);
      // The hints come from the live table too.
      auto hk = [&](const char* action) {
        const std::size_t n = rolltui_bindings_chord_count(bindings, action, std::strlen(action));
        if (n == 0) return std::string("-");
        RolltuiChord c{};
        rolltui_bindings_chord_at(bindings, action, std::strlen(action), 0, &c);
        char buf[ROLLTUI_CHORD_STRING_MAX];
        const std::size_t bn = rolltui_chord_display(&c, buf, sizeof buf);
        return std::string(buf, bn);
      };
      std::string help = "^C quit  " + hk("app.help") + " help  " + hk("app.menu") + " menu  " + hk("app.palette") + " palette  " + hk("editor.theme") + " theme  " +
                         hk("editor.layout") + " layout  " + hk("editor.keys") + " keys ";
      const int hw = rolltui_u_display_width(u_scratch, help.data(), help.size(), ambiguous ? 1 : 0);
      const int sw = rolltui_u_display_width(u_scratch, status.data(), status.size(), ambiguous ? 1 : 0);
      if (hw + sw + 2 <= w) put_text(f, w - hw, h - 1, help, style(ROLLTUI_ROLE_TEXT_MUTED), hw);
    }
    // Phase 12 m6: THE ONE PLACE this host applies an effect — after the whole screen has
    // composed, so a marked span under a modal's overlay animates over what the reader
    // actually sees, and before the frame diff.
    RolltuiEffectReport rep{};
    effects_unknown_kinds.clear();
    rolltui_effects_apply(ctx, f, effect_scratch, theme_styles, nullptr, effects_map, effect_ms, ambiguous ? 1 : 0, &rep,
        [](void* ctx, const char* kind, std::size_t len) { static_cast<App*>(ctx)->effects_unknown_kinds.emplace_back(kind, len); },
        this);
    last_frame_us = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
  }

  // Returns false to quit.
  bool handle(const RolltuiEvent& ev) {
    // App-level keys first; everything else is routed by the stack.
    sync_look();
    if (ev.kind == ROLLTUI_EVENT_KEY) {
      const RolltuiChord& k = ev.key;
      if (k.key == ROLLTUI_KEY_CHAR && k.ctrl && !k.alt && k.ch == 'c') return false;  // Ctrl-C is the host's, not an action
      // The keys editor is capturing: every key is the chord, nothing else acts.
      if (editor_mode == EditorMode::Keys && keditor.capturing()) {
        keys_outcome(keditor.handle(&ev, bindings));
        return true;
      }
      const std::string_view st = action_for(k, "studio"), app_a = action_for(k, "app"), ed = action_for(k, "editor");
      if (st == "studio.quit") return false;
      if (st == "studio.cycle_theme") {
        std::vector<std::string> names = ThemeStore::shipped_names();
        shipped_theme_index = (shipped_theme_index + 1) % names.size();
        ThemePresetReport rep;
        theme_arg.clear();
        store->load(names[shipped_theme_index], rep, persist);
        if (editor_mode == EditorMode::Theme) { RolltuiThemeReport tr{}; ThemeValueHandle wc = store->working(); teditor.load(wc->colours, &tr); rolltui_theme_report_release(&tr); }
        return true;
      }
      if (st == "studio.reload") { load_fixture(); return true; }
      if (ed == "editor.theme") { toggle_editor(); return true; }
      if (ed == "editor.layout") { toggle_layout_editor(); return true; }
      if (ed == "editor.keys") { toggle_keys_editor(); return true; }
      std::size_t draft_n = 0;
      rolltui_input_text(editor(), &draft_n);
      if (app_a == "app.help" && !(k.key == ROLLTUI_KEY_CHAR && !k.ctrl && !k.alt && draft_n != 0)) { toggle_help(); return true; }
      if (app_a == "app.menu") { open_menu(false); return true; }
      if (app_a == "app.palette") { open_menu(true); return true; }
      if (app_a == "app.find") { toggle_find(); return true; }
      if (app_a == "app.repaint") return true;  // the loop repaints
    }
    ensure_layout();
    if (ev.kind == ROLLTUI_EVENT_PASTE) {
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      if (rolltui_window_stack_has_popup(stack, "editor", 6) && focused && focused->id == "editor") {
        if (editor_mode == EditorMode::Layout) layout_outcome(leditor.handle(&ev, bindings));
        else if (editor_mode == EditorMode::Keys) keys_outcome(keditor.handle(&ev, bindings));
        else editor_outcome(teditor.handle(&ev, bindings));
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
        const std::string_view sa = action_for(ev.key, "stack");
        if (sa == "stack.close_popup" || sa == "stack.focus_next" || sa == "stack.focus_prev") {
          hint.clear();
          if (editor_mode == EditorMode::Layout) layout_outcome(leditor.handle(&ev, bindings));
          else if (editor_mode == EditorMode::Keys) keys_outcome(keditor.handle(&ev, bindings));
          else if (sa == "stack.close_popup") editor_outcome(teditor.handle(&ev, bindings));
          return true;
        }
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
          if (std::optional<Seam> seam = seam_at(m.x, m.y)) { drag_seam = seam; leditor.begin_drag(seam->id); return true; }
          if (std::optional<std::string> win = window_at(m.x, m.y)) { leditor.select(*win); return true; }
        }
        if (m.kind == RolltuiMouseEvent::Kind::Drag && leditor.dragging() && drag_seam) {
          std::vector<RolltuiResolvedNode> nodes2;
          rolltui_resolve_tree(&leditor.current().base.root, layout_area(), layout_area(), 0, collect_resolved, &nodes2);
          for (const RolltuiResolvedNode& rn : nodes2)
            if (view_of(rn.node->id) == leditor.selected()) {
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
    const std::string target = str_of(window);
    rolltui_str_free(&window);
    if (route_kind == ROLLTUI_ROUTE_CLOSED_POPUP && target == "editor") { editor_open = false; editor_mode = EditorMode::None; store_seen = 0; bstore_seen = 0; sync_look(); return true; }
    if (route_kind == ROLLTUI_ROUTE_CLOSED_POPUP && target == "confirm") { confirm_action = nullptr; return true; }
    if (route_kind != ROLLTUI_ROUTE_DELIVER) return true;
    // The event goes to the window's WIDGET, by kind — never by a window name, so a
    // layout file may call its windows anything (Phase 10 m2). Since Phase 11 m3 that
    // holds for the studio's OWN three as well: they are registered kinds, so they take
    // their events through the same routing as every built-in.
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
        const std::string_view kind(cname, cname_len);  // the kind's NAME is its identity (Phase 18 m2)
        if (is_host) { rolltui_windows_handle(windows, target.data(), target.size(), &ev); rolltui_str_free(&why); return true; }
        if (kind == "transcript") {
          rolltui_str_free(&why);
          if (rolltui_windows_handle(windows, target.data(), target.size(), &ev)) return true;
          // typing while the transcript has focus still types (falls through to the prompt)
          if (ev.kind != ROLLTUI_EVENT_KEY) return true;
          return input_event("prompt", ev);
        }
        if (kind == "input") {
          const std::string source(csource, csource_len);
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
  bool input_event(std::string_view target, const RolltuiEvent& ev) {
    RolltuiInput* ed = rolltui_windows_input(windows, target.data(), target.size());
    switch (rolltui_input_kind_process_event(ed, windows, target.data(), target.size(), &ev)) {  // Submit has already reached append_prompt
      case ROLLTUI_INPUT_SUBMIT: return true;
      case ROLLTUI_INPUT_EOF: return false;
      case ROLLTUI_INPUT_HANDLED: return true;
      case ROLLTUI_INPUT_IGNORED: break;
    }
    // What the input Ignored is offered to the transcript (its own scope of the table).
    if (ev.kind == ROLLTUI_EVENT_KEY) {
      const std::string_view a = action_for(ev.key, "transcript");
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
int editor_handle(void* ctx, const RolltuiEvent* e) {
  App* app = static_cast<App*>(ctx);
  app->hint.clear();
  if (app->editor_mode == App::EditorMode::Layout) app->layout_outcome(app->leditor.handle(e, app->bindings));
  else if (app->editor_mode == App::EditorMode::Keys) app->keys_outcome(app->keditor.handle(e, app->bindings));
  else app->editor_outcome(app->teditor.handle(e, app->bindings));
  return 1;
}
// Registered FORBIDDEN and never scrollable/sized on its own — the editor draws its own
// menu and scrolls it internally. `ctx` is `this` (App*), which the widget table does not
// own: App outlives it, so `destroy` is a no-op.
constexpr RolltuiWidgetPlugin kEditorPlugin = {
    /*destroy=*/editor_destroy, /*layout=*/editor_layout, /*draw=*/editor_draw,
    /*problem=*/nullptr, /*note_at=*/nullptr, /*desired_outer=*/nullptr,
    /*handle=*/editor_handle, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr,
};
RolltuiWidget editor_factory(void* ctx, const char*, std::size_t) { return RolltuiWidget{&kEditorPlugin, ctx}; }

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
    /*handle=*/confirm_handle, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr,
};
RolltuiWidget confirm_factory(void* ctx, const char*, std::size_t) { return RolltuiWidget{&kConfirmPlugin, ctx}; }

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
    /*handle=*/report_handle, /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr,
};
RolltuiWidget report_factory(void* ctx, const char*, std::size_t) { return RolltuiWidget{&kReportPlugin, ctx}; }

bool parse_size(const std::string& s, int& w, int& h) {
  std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

// One scripted step: an event with the clock it happens at, or a tick. `owned_text`
// backs a Paste step's bytes: `RolltuiEvent::text` is a BORROW (rolltui_keys.h's rule),
// so it is filled in fresh by run_steps() right before dispatch, from THIS stable member
// — never baked in while the vector holding these steps may still grow and relocate it.
struct Step {
  bool tick = false;
  RolltuiEvent ev{};
  std::uint64_t ms = 0;
  std::string owned_text;
};

std::vector<Step> scripted_keys(const std::string& spec, int w, int h) {
  std::vector<Step> out;
  std::istringstream in(spec);
  std::string tok;
  std::uint64_t clock = 1000;
  auto key_ev = [](unsigned char k, bool shift = false, bool ctrl = false, bool alt = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    e.key.shift = shift ? 1 : 0;
    e.key.ctrl = ctrl ? 1 : 0;
    e.key.alt = alt ? 1 : 0;
    return e;
  };
  auto ctrl_ev = [](char c) { RolltuiEvent e{}; e.kind = ROLLTUI_EVENT_KEY; e.key.key = ROLLTUI_KEY_CHAR; e.key.ch = static_cast<RolltuiCodepoint>(c); e.key.ctrl = 1; return e; };
  auto alt_ev = [](char c) { RolltuiEvent e{}; e.kind = ROLLTUI_EVENT_KEY; e.key.key = ROLLTUI_KEY_CHAR; e.key.ch = static_cast<RolltuiCodepoint>(c); e.key.alt = 1; return e; };
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
  auto named = [&](std::string name, RolltuiEvent& out_ev) {
    bool shift = false, c = false, a = false;
    for (;;) {
      if (name.rfind("Shift", 0) == 0) { shift = true; name.erase(0, 5); }
      else if (name.rfind("Ctrl", 0) == 0) { c = true; name.erase(0, 4); }
      else if (name.rfind("Alt", 0) == 0) { a = true; name.erase(0, 3); }
      else break;
    }
    // The name -> Key table is `rolltui_key_from_display_name`, which reads the same
    // X-macro list the library's own `to_string` and chord parser do, case-insensitively.
    if (const int k = rolltui_key_from_display_name(name.data(), name.size()); k >= 0) {
      out_ev = key_ev(static_cast<unsigned char>(k), shift, c, a);
      return true;
    }
    if (name.size() == 1 && (c || a) && name[0] >= 'A' && name[0] <= 'Z') {  // CtrlA, AltC, ...
      out_ev = c ? ctrl_ev(static_cast<char>(name[0] - 'A' + 'a')) : alt_ev(static_cast<char>(name[0] - 'A' + 'a'));
      out_ev.key.shift = shift ? 1 : 0;
      return true;
    }
    return false;
  };
  auto mouse_ev = [](RolltuiMouseEvent::Kind k, int x, int y, int button = 1, bool shift = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.kind = k;
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.button = button;
    e.mouse.shift = shift ? 1 : 0;
    return e;
  };
  auto push = [&](RolltuiEvent e, bool advance = true) {
    out.push_back({false, e, clock, ""});
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
    RolltuiEvent k{};
    if (tok == "Tick") out.push_back({true, {}, clock, ""});
    else if (tok.rfind("Type:", 0) == 0) {
      const std::string s = unescape(tok.substr(5), false);
      std::vector<RolltuiDecodedChar> chars(s.size());
      const std::size_t n = rolltui_u_decode_utf8_chars(s.data(), s.size(), chars.data());
      for (std::size_t i = 0; i < n; ++i) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = ROLLTUI_KEY_CHAR;
        e.key.ch = chars[i].cp;
        push(e, false);
      }
      clock += 1000;
    } else if (tok.rfind("Paste:", 0) == 0) {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_PASTE;
      out.push_back({false, e, clock, unescape(tok.substr(6), true)});
      clock += 1000;
    } else if (named(tok, k)) push(k);
    else if (tok == "WheelUp" || tok == "WheelDown") {
      // Over the middle of the screen, which every built-in layout gives to the transcript.
      push(mouse_ev(tok == "WheelUp" ? RolltuiMouseEvent::Kind::WheelUp : RolltuiMouseEvent::Kind::WheelDown, w / 4, h / 3, 0));
    } else if (tok == "Click" || tok == "ShiftClick") {
      if (xy(x, y)) push(mouse_ev(RolltuiMouseEvent::Kind::Press, x, y, 1, tok == "ShiftClick"));
    } else if (tok == "DblClick" || tok == "TripleClick") {
      if (xy(x, y)) {
        const int presses = tok == "DblClick" ? 2 : 3;
        for (int i = 0; i < presses; ++i) {
          push(mouse_ev(RolltuiMouseEvent::Kind::Press, x, y), false);
          if (i + 1 < presses) push(mouse_ev(RolltuiMouseEvent::Kind::Release, x, y), false);
        }
        clock += 1000;
      }
    } else if (tok == "Drag") {
      if (xy(x, y)) push(mouse_ev(RolltuiMouseEvent::Kind::Drag, x, y));
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
        const RolltuiMouseEvent* last = nullptr;
        for (const Step& s : out)
          if (s.ev.kind == ROLLTUI_EVENT_MOUSE) last = &s.ev.mouse;
        if (last) { x = last->x; y = last->y; }
      }
      push(mouse_ev(RolltuiMouseEvent::Kind::Release, x, y));
    } else {
      std::vector<RolltuiDecodedChar> chars(tok.size());
      const std::size_t n = rolltui_u_decode_utf8_chars(tok.data(), tok.size(), chars.data());
      if (n != 0) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = ROLLTUI_KEY_CHAR;
        e.key.ch = chars[0].cp;
        push(e);
      }
    }
  }
  return out;
}

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

void print_frame_plain(const RolltuiFrame* f) {
  RolltuiStr text{};
  rolltui_frame_to_text(f, &text);
  std::fwrite(text.p ? text.p : "", 1, text.n, stdout);
  rolltui_str_free(&text);
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-studio --check NAME|FILE | --generate RULESET [--seed N] [--chaos X]\n"
               "       rolltui-studio FIXTURE.md [--presets DIR] [--shipped DIR] [--theme NAME|FILE] [--layout NAME|FILE] [--bindings NAME|FILE]\n"
               "                                [--app PROFILE.json]  preview AS that app: its sources, samples, menus, actions and kinds\n"
               "       [--mode dark|light] [--depth truecolor|256|16|mono] [--ambiguous-wide] [--frame WxH | --frame-sgr WxH]\n"
               "       [--dump-role ROLE] [--tick MS] [--dump-tick] [--code-fold FOLD,CAP]\n"
               "       [--keys \"Up Down PageDown Tab F1 F4 Type:hello_world ShiftLeft AltEnter Click 5,3 Drag 20,6 Release ...\"]\n");
  return 2;
}

std::string default_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return std::string(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return std::string(home && *home ? home : ".") + "/.config/roll/rolltui";
}

std::string style_dump(std::string_view role, const RolltuiStyle& s) {
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

// The terminal loop calls `rolltui_terminal.h` directly: a raw terminal handle crosses no
// boundary this file must still speak a C++ shape of. `RolltuiEvent` is already the shape
// `App::handle` takes, so this callback only copies the three fields across and folds in
// the two invalidation sites `rolltui_swap` leaves the host's policy (a resize, and
// Ctrl-L's explicit repaint).
struct PollCtx {
  App* app;
  RolltuiSwap* swap;
  bool* running;
  bool stop = false;
};
void on_term_event(void* vctx, const RolltuiTermEvent* te) {
  PollCtx* c = static_cast<PollCtx*>(vctx);
  if (c->stop) return;  // `app.handle` already said stop; ignore the rest of this batch
  c->app->clock_ms = now_ms();
  if (te->kind == ROLLTUI_TERM_EVENT_RESIZE) {
    c->app->resize(te->w, te->h);
    rolltui_swap_invalidate(c->swap);
    return;
  }
  RolltuiEvent e{};
  e.kind = te->kind;
  e.key = te->key;
  e.mouse = te->mouse;
  e.text = te->text;
  e.text_len = te->text_len;
  if (te->kind == ROLLTUI_TERM_EVENT_KEY && e.key.key == ROLLTUI_KEY_CHAR && e.key.ctrl && e.key.ch == 'l')
    rolltui_swap_invalidate(c->swap);
  if (!c->app->handle(e)) {
    *c->running = false;
    c->stop = true;
  }
}

}  // namespace

int main(int argc, char** argv) {
  App app;
  app.depth = rolltui_detect_color_depth(std::getenv("COLORTERM"), std::getenv("TERM"), std::getenv("ROLL_COLOR_DEPTH"));
  std::string frame_spec, keys_spec, dump_role, check_arg, generate_arg, seed_arg = "1", chaos_arg = "0";
  std::uint64_t tick_ms = 0;   // milestone 6: the elapsed time --frame renders at
  bool dump_tick = false;
  std::string presets_dir = default_presets_dir(), shipped_dir = ROLLTUI_SHIPPED_DIR, app_profile_path;
  bool frame_sgr = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--theme") app.theme_arg = next();
    else if (a == "--layout") app.layout_arg = next();
    else if (a == "--presets") presets_dir = next();
    else if (a == "--shipped") shipped_dir = next();
    else if (a == "--bindings") app.bindings_arg = next();
    else if (a == "--app") app_profile_path = next();
    else if (a == "--dump-role") dump_role = next();
    else if (a == "--check") check_arg = next();
    else if (a == "--generate") generate_arg = next();
    else if (a == "--seed") seed_arg = next();
    else if (a == "--chaos") chaos_arg = next();
    else if (a == "--mode") app.mode_flag = (next() == "light") ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK;
    else if (a == "--depth") {
      std::string d = next();
      app.depth = rolltui_detect_color_depth(nullptr, nullptr, d.c_str());
    } else if (a == "--ambiguous-wide") app.ambiguous = true;
    else if (a == "--code-fold") {  // "FOLD,CAP" — the two thresholds, so a golden can
      const std::string v = next();  // exercise them on a small fixture rather than on
      const std::size_t comma = v.find(',');  // a hundred-line one
      app.code_fold_over = std::atoi(v.substr(0, comma).c_str());
      app.code_cap = comma == std::string::npos ? 0 : std::atoi(v.substr(comma + 1).c_str());
    }
    else if (a == "--frame") frame_spec = next();
    else if (a == "--frame-sgr") { frame_spec = next(); frame_sgr = true; }
    else if (a == "--keys") keys_spec = next();
    else if (a == "--tick") tick_ms = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--dump-tick") dump_tick = true;
    else if (a.rfind("--", 0) == 0) return usage();
    else app.fixture_path = a;
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
    const std::string text = std::string(dump.p ? dump.p : "", dump.n) + "\n";
    std::fwrite(text.data(), 1, text.size(), stdout);
    rolltui_str_free(&dump);
    rolltui_json_free(root);
    rolltui_json_free(dmeta);
    rolltui_json_free(lmeta);
    rolltui_str_free(&dname);
    rolltui_str_free(&lname);
    return 0;
  }
  if (!check_arg.empty()) {
    ThemeStore store(presets_dir, false, shipped_dir + "/themes");
    ThemePresetReport rep;
    ThemeValueHandle p = store.get(check_arg, rep);
    if (!p) { std::fprintf(stderr, "%s\n", rep.error.c_str()); return 2; }
    const RolltuiThemeVocab* vocab = rolltui_theme_default_vocab();
    int rc = 0;
    for (unsigned char m : {ROLLTUI_MODE_DARK, ROLLTUI_MODE_LIGHT}) {
      RolltuiThemeReport tr{};
      RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
      RolltuiStr name{};
      RolltuiEffectMap* eff = rolltui_theme_load(p->colours, m, vocab, styles, &name, &tr);
      rolltui_str_free(&name);
      if (!eff) { std::fprintf(stderr, "%s\n", tr.error.p ? tr.error.p : ""); rolltui_theme_report_release(&tr); return 2; }
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
      for (std::size_t i = 0; i < failed.n; ++i) { std::printf("CLAIM FAILED: %s\n", str_of(failed.v[i]).c_str()); rc = 1; }
      const bool claimed_any = rolltui_json_is_array(rolltui_json_get(meta, "badges", 6)) != 0;
      if (!claimed_any) std::printf("(no badges claimed)\n");
      else if (failed.n == 0) std::printf("every claimed badge holds\n");
      rolltui_str_array_release(&failed);
      rolltui_json_free(meta);
      std::printf("\n");
      // A colours object without pairs is the same at both modes: one report is enough.
      RolltuiStr colour_dump{};
      rolltui_json_dump(p->colours, 0, &colour_dump);
      const bool has_dark_pair = std::string(colour_dump.p ? colour_dump.p : "", colour_dump.n).find("\"dark\"") != std::string::npos;
      rolltui_str_free(&colour_dump);
      if (!has_dark_pair) break;
    }
    return rc;
  }
  if (app.fixture_path.empty()) return usage();
  if (!app.load_fixture()) { std::fprintf(stderr, "cannot read %s\n", app.fixture_path.c_str()); return 1; }
  // The preset store: the studio is a rolltui host, with the editor's privilege
  // (it writes what ships). Under --frame nothing autosaves.
  app.store = std::make_unique<ThemeStore>(presets_dir, true, shipped_dir + "/themes");
  app.lstore = std::make_unique<LayoutStore>(presets_dir, true, shipped_dir + "/layouts");
  app.bstore = std::make_unique<BindingsStore>(presets_dir, true, shipped_dir + "/bindings");
  app.persist = frame_spec.empty();
  rolltui_windows_set_dir(app.windows, presets_dir.data(), presets_dir.size());  // a layout's `file:` paths are relative to the preset directory
  // AFTER the flags: this is the one Windows setting --code-fold can change, and
  // bind_windows() runs in App's constructor, before argv has been looked at.
  { const RolltuiCodeFold cf{app.code_fold_over, app.code_cap}; rolltui_windows_set_code_fold(app.windows, &cf); }
  // --app: preview AS the target app (Phase 11 m4). Mounted BEFORE the studio binds its
  // own sources, so a name the profile supplies wins. With no --app the studio previews
  // as itself, exactly as before.
  if (!app_profile_path.empty()) {
    bool ok = false;
    const std::string text = read_file(app_profile_path, ok);
    if (!ok) {
      std::fprintf(stderr, "rolltui: cannot read app profile %s\n", app_profile_path.c_str());
      return 1;
    }
    RolltuiAppProfileReport prep{};
    RolltuiAppProfile* profile = rolltui_app_profile_parse(text.data(), text.size(), &prep);
    if (!profile) {
      RolltuiStr s{};
      rolltui_app_profile_report_summary(&prep, &s);
      std::fprintf(stderr, "rolltui: app profile %s: %s\n", app_profile_path.c_str(), s.p ? s.p : "");
      rolltui_str_free(&s);
      rolltui_app_profile_report_release(&prep);
      return 1;
    }
    if (!rolltui_app_profile_report_clean(&prep)) {
      RolltuiStr s{};
      rolltui_app_profile_report_summary(&prep, &s);
      std::fprintf(stderr, "rolltui: app profile %s loaded with problems: %s\n", app_profile_path.c_str(), s.p ? s.p : "");
      rolltui_str_free(&s);
    }
    rolltui_app_profile_report_release(&prep);
    rolltui_app_profile_mount(profile, app.windows);
    app.profile = profile;
    std::size_t alen = 0;
    const char* an = rolltui_app_profile_app(app.profile, &alen);
    std::fprintf(stderr, "rolltui: previewing as '%s'\n", std::string(an, alen).c_str());
  }
  {
    ThemePresetReport start_rep;
    app.store->start(start_rep);
    if (!start_rep.error.empty()) app.theme_note = str_of(start_rep.error);
    LayoutPresetReport lstart_rep;
    app.lstore->start(lstart_rep);
    if (!lstart_rep.error.empty()) app.layout_note = str_of(lstart_rep.error);
    // What the loader DID that the file did not ask for (today: a file declaring no actions
    // gets the shipped default's) is a note, not a problem — said once, on stderr, so it
    // never moves a golden frame.
    for (std::size_t i = 0; i < lstart_rep.layout.notes_n; ++i)
      std::fprintf(stderr, "rolltui: %s\n", str_of(lstart_rep.layout.notes[i]).c_str());
    BindingsPresetReport bstart_rep;
    app.bstore->start(bstart_rep);
    if (!bstart_rep.error.empty()) app.hint = str_of(bstart_rep.error);
  }
  app.load_theme_arg();
  app.load_bindings_arg();
  app.refresh_menu();

  if (!frame_spec.empty()) {
    int fw, fh;
    if (!parse_size(frame_spec, fw, fh)) return usage();
    app.resize(fw, fh);
    app.load_layout_arg();
    app.sync_look();
    app.ensure_layout();
    std::vector<Step> steps = scripted_keys(keys_spec, fw, fh);
    run_steps(app, steps);
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
      std::printf("--- tick ---\n%s\n", tick > 0 ? std::to_string(tick).c_str() : "none");
    }
    if (!dump_role.empty()) {
      const int r = rolltui_role_from_name(dump_role.data(), dump_role.size());
      if (r < 0) { std::fprintf(stderr, "no role named %s\n", dump_role.c_str()); rolltui_swap_free(swap); return 1; }
      std::printf("--- role ---\n%s\n", style_dump(dump_role, app.style(static_cast<unsigned char>(r))).c_str());
    }
    rolltui_swap_free(swap);
    return 0;
  }

  RolltuiTerminal* term = rolltui_terminal_new(STDIN_FILENO, STDOUT_FILENO, RolltuiTerminalOptions{});
  if (!rolltui_terminal_is_tty(term)) {
    std::fprintf(stderr, "not a terminal; use --frame WxH\n");
    rolltui_terminal_free(term);
    return 1;
  }
  if (!app.mode_flag && app.store->working()->mode == "auto") {
    RolltuiStyleColor bg{};
    const int have_bg = rolltui_terminal_query_background(term, 150, &bg);
    app.detected_mode = have_bg ? rolltui_mode_for_background(bg) : ROLLTUI_MODE_DARK;
  }
  app.resize(rolltui_terminal_width(term), rolltui_terminal_height(term));
  app.load_layout_arg();
  app.sync_look();
  app.ensure_layout();
  { std::vector<Step> steps = scripted_keys(keys_spec, app.w, app.h); run_steps(app, steps); }
  // THE DOUBLE BUFFER IS THE LIBRARY'S: two frames for the whole run, nothing owned inside
  // the loop, and `begin` calls `rolltui_frame_reset` — so this repaint lands INSIDE the
  // budget rather than beside it.
  RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
  bool running = true;
  while (running) {
    app.maybe_reload_theme();
    app.maybe_reload_layout();
    app.effect_ms = now_ms();
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f, true);
    const bool ticking = rolltui_transcript_wants_tick(app.transcript());
    // m6: how long this host may sleep is a function of what the frame MARKED, so an
    // idle screen still costs one wakeup every 250 ms and no more.
    const int timeout = app.poll_timeout_ms(f, ticking ? 50 : 250);
    RolltuiStr out{};
    rolltui_swap_present(swap, app.depth, &out);
    rolltui_terminal_write(term, out.p ? out.p : "", out.n);
    rolltui_str_free(&out);
    PollCtx ctx{&app, swap, &running};
    rolltui_terminal_poll(term, timeout, on_term_event, &ctx);
    if (ticking) app.tick();
  }
  rolltui_swap_free(swap);
  rolltui_terminal_free(term);
  return 0;
}
