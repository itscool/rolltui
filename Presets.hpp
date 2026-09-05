#pragma once
//
// rolltui/Presets.hpp — the preset system (plan/phase-9.md, milestone 11 / 11e). The
// LIBRARY's, not any host's: a host hands it one directory and a may-write-shipped
// flag and gets a Theme back. Nothing of roll is in here.
//
// THREE DOMAINS, identical mechanics in each:
//   the Theme     colours, mode and colour depth — how it LOOKS, and nothing
//                 structural (Phase 10 m1; see THE SPLIT below).
//   the Layout    the design of a screen: the window tree, the popups, the sizes —
//                 one file per screen (Phase 10 m1). Panel width is not a knob: it is
//                 the status window's `size` in the layout.
//   the Bindings  every key and mouse action, the whole input setup (Bindings.hpp) —
//                 milestone 17; a session is "Theme X + Layout Y + Bindings Z", three
//                 independent choices: changing keys never touches the look, and so on.
// One implementation of the mechanics, PresetStore<Domain> (PresetStore.hpp), and three
// Domain traits below; rolltui-presets-test asserts the five rules on ALL THREE.
//
// THE SPLIT (Phase 10 m1, plan/phase-10.md; supersedes the m17 wording that put the
// layout inside the Theme): a layout was a look preference only while it rearranged
// windows the host had coded. Once a layout says which widgets exist and what they
// show, it is the APPLICATION, and an application's structure does not belong in a
// user's colour preset. The one coupling rule: a layout may name theme ROLES, never a
// colour; a theme never names a window. That is what lets any theme render any layout.
// `migrate_theme_layout()` moves a Phase 9 working copy across ONCE, and a theme preset
// file that still carries a layout part loads its colours and says the part was ignored.
//
// THE FIVE RULES, per domain (asserted in rolltui/tests/presets_test.cpp):
//   1. A preset is the ENTIRE domain, saved and loaded as one unit, never as pieces:
//      themes/<name>.json holds colours + depth + mode; layouts/<name>.json holds a
//      whole screen.
//   2. Each domain has exactly ONE WORKING COPY, and it is the only thing anyone ever
//      edits, in every rolltui host. Every runtime change lands in it and it AUTOSAVES
//      (<dir>/theme.working.json, written by rename like every state file here) —
//      nothing is lost on quit, nothing asks.
//   3. A preset is a named, READ-ONLY snapshot of a whole domain. Load copies it into
//      the working copy. Save-as is a MANUAL act that snapshots the working copy under
//      a name (save over an existing user preset asks once: SaveResult::ExistsAsk).
//   4. The working copy knows which preset it came from (`origin`). While identical to
//      it the label is the preset's name; the first edit makes it "<name> (modified)".
//      Identity is BY COMPARISON with the preset's content, not a dirty flag that can
//      drift — so an edit that puts everything back exactly shows the plain name again.
//   5. Shipped presets are read-only for users: compiled in from rolltui/presets/ and
//      parsed at start like the built-in layouts; "default" always exists and is what
//      a fresh install runs. Save-as over a shipped name is refused BY NAME — except
//      when `may_write_shipped` is set (the editor, whose job is producing what ships),
//      which writes the file into `shipped_dir` (the source tree) for the next build.
//
// PRECEDENCE, per setting, stated once here and asserted in the same test:
//   flag > environment > working copy > built-in default.
// `resolve_setting` is the whole rule: the first NON-EMPTY rung wins, and the answer
// says which rung it was, so a surprising value has exactly one place to be traced
// from. `--theme X` / `<PREFIX>_THEME=X` name a preset to fill the working copy for
// THIS RUN: the in-memory working copy becomes X (label "X") without being written; a
// flag never persists by itself. The first in-session edit then writes what you were
// running — you edited it, so it is yours now. `kSettings` is the table — ONE table for
// all three domains, each row naming its own (Phase 10 m1, so that `--layout` resolves
// by the same function against the Layout store): the host supplies the flag value and
// the environment value (with its own prefix); the working-copy field and the built-in
// default are the library's.
//
// DIRECTORY (`Options::dir`, e.g. ~/.config/<host>/rolltui):
//   theme.working.json     the Theme working copy (rule 2), plus "preset": its origin
//   themes/<name>.json     user theme presets (rule 3)
//   layout.working.json    the Layout working copy, plus "preset": its origin
//   layouts/<name>.json    user layouts — a file dropped in is a preset, offered by
//                          name wherever a layout is chosen
//   bindings.working.json  the Bindings working copy
//   bindings/<name>.json   user bindings presets
// THREADS: every method takes the store's own lock and returns copies; a host may edit
// from one thread and render from another. `version()` bumps on every change so a
// renderer re-reads only when it must.
//
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/PresetStore.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

// ---- the Theme domain ----------------------------------------------------------------

// `colours` STAYS a `json::Value`. Its own parse/dump ALGORITHM moved to C
// (`theme_preset_from_json`/`_to_json`/`ThemeDomain::parse_partial`, `Presets.cpp` — this
// header comment is the finding that port left behind): each now crosses "colours" as a
// `RolltuiJsonValue*` for the one call it needs it. The FIELD stayed a `json::Value` because
// real callers reach into it with real `json::Value` operations a C-backed proxy cannot
// honestly offer: `theme_editor.cpp`'s `defs_ = preset.colours.get("defs")`, `studio.cpp`'s
// `store->set_colours(teditor.colours_json(store->origin()))`, and this struct's own
// `ThemePresets::set_colours(json::Value, bool)` below. None of those three call sites is
// this task's to change. This is the SAME position `rolltui/c/rolltui_json.h`'s header
// comment states for the five other C++ modules still holding a real `Value`, applied to one
// field of one struct rather than to a whole file.
struct ThemePreset {
  json::Value colours;          // a theme file object (Theme.hpp's format; dark/light pairs allowed)
  std::string mode = "auto";    // auto | dark | light — auto: the host asks the terminal (OSC 11), dark when it cannot
  std::string depth = "auto";   // auto | truecolor | 256 | 16 | mono — auto: detect_color_depth over the environment
  bool operator==(const ThemePreset&) const = default;
};

// File format: { "name", "mode", "depth", "colours": {theme object} }. "colours" is
// required. Values outside the sets above are bad values (the default kept). A Phase 9
// file's "layout" part is IGNORED with a note naming it — the Layout domain owns it
// now, and `migrate_theme_layout()` is what moves a working copy across.
std::optional<ThemePreset> theme_preset_from_json(const json::Value& v, PresetLoadReport& report);
json::Value theme_preset_to_json(const ThemePreset& p, std::string_view name);

// The colours part resolved for a mode. `mode` "auto" is the host's to settle first.
std::optional<Theme> resolve_colours(const ThemePreset& p, ThemeMode mode, ThemeLoadReport& report);
std::optional<ThemeMode> mode_from_setting(std::string_view s);    // "dark"|"light"; nullopt for "auto"/bad
std::optional<ColorDepth> depth_from_setting(std::string_view s);  // "truecolor"|"256"|"16"|"mono"; nullopt for "auto"/bad
bool valid_mode_setting(std::string_view s);
bool valid_depth_setting(std::string_view s);

struct ThemeDomain {
  using Value = ThemePreset;
  static constexpr std::string_view kind = "theme";
  static constexpr std::string_view working_file = "theme.working.json";
  static constexpr std::string_view subdir = "themes";
  static std::size_t shipped_count();
  static std::pair<std::string_view, std::string_view> shipped_at(std::size_t i);
  static std::optional<Value> parse(const json::Value& v, PresetLoadReport& report) { return theme_preset_from_json(v, report); }
  // A colours-only theme file (Theme.hpp's format, "roles" at the top) fills the
  // colours part; layout, mode and depth are kept.
  static std::optional<Value> parse_partial(const json::Value& v, const Value& working, PresetLoadReport& report);
  static json::Value to_json(const Value& p, std::string_view name) { return theme_preset_to_json(p, name); }
};

class ThemePresets : public PresetStore<ThemeDomain> {
 public:
  using PresetStore<ThemeDomain>::PresetStore;
  // Part edits (rule 2: every runtime change lands in the working copy).
  void set_colours(json::Value colours, bool persist = true) { edit([&](ThemePreset& p) { p.colours = std::move(colours); }, persist); }
  void set_mode(std::string mode, bool persist = true) { edit([&](ThemePreset& p) { p.mode = std::move(mode); }, persist); }
  void set_depth(std::string depth, bool persist = true) { edit([&](ThemePreset& p) { p.depth = std::move(depth); }, persist); }
};

// ---- the Layout domain (Phase 10 m1) -------------------------------------------------
//
// The Value is a whole `Layout` (Layout.hpp's file format, unchanged). The shipped
// presets ARE the built-ins: rolltui/presets/layouts/*.json, embedded once and read by
// both `builtin_layout()` and this trait, so the two can never disagree. A file a user
// drops into <dir>/layouts is therefore a preset like any other — the same directory
// Phase 9 discovered by hand.

struct LayoutDomain {
  using Value = Layout;
  static constexpr std::string_view kind = "layout";
  static constexpr std::string_view working_file = "layout.working.json";
  static constexpr std::string_view subdir = "layouts";
  static std::size_t shipped_count();
  static std::pair<std::string_view, std::string_view> shipped_at(std::size_t i);
  static std::optional<Value> parse(const json::Value& v, PresetLoadReport& report);
  static std::optional<Value> parse_partial(const json::Value&, const Value&, PresetLoadReport&) { return std::nullopt; }  // a layout file is always whole
  // Writes the layout verbatim; the preset name is NOT written over the layout's own
  // "name" (see the .cpp for why the two are allowed to differ).
  static json::Value to_json(const Value& l, std::string_view name);
};
using LayoutPresets = PresetStore<LayoutDomain>;

// ---- the Phase 9 → Phase 10 migration ------------------------------------------------
//
// ONCE, before either store starts: a `theme.working.json` that still carries a "layout"
// part has it copied into `layout.working.json` — but ONLY when there is no layout
// working copy yet, so a second run can never overwrite what the user has since done —
// and the theme file is then rewritten without the part. Everything else in the theme
// file is preserved verbatim. A host says the notes out loud at startup: this happens
// once per install and silently losing a layout is exactly the failure the split risks.
struct MigrationReport {
  bool moved = false;              // the layout part became the layout working copy
  bool rewrote_theme = false;      // the theme working copy no longer carries a layout
  std::string layout_name;         // the layout that was moved, when one was
  std::string error;               // non-empty: nothing was changed
  std::vector<std::string> notes;  // what happened, in words
};
MigrationReport migrate_theme_layout(const std::string& dir);

// ---- the Bindings domain (milestone 17) ----------------------------------------------

struct BindingsDomain {
  using Value = Bindings;
  static constexpr std::string_view kind = "bindings";
  static constexpr std::string_view working_file = "bindings.working.json";
  static constexpr std::string_view subdir = "bindings";
  static std::size_t shipped_count();
  static std::pair<std::string_view, std::string_view> shipped_at(std::size_t i);
  static std::optional<Value> parse(const json::Value& v, PresetLoadReport& report);
  static std::optional<Value> parse_partial(const json::Value&, const Value&, PresetLoadReport&) { return std::nullopt; }  // a bindings file is always whole
  static json::Value to_json(const Value& b, std::string_view name) { return b.to_json(name); }
};
using BindingsPresets = PresetStore<BindingsDomain>;

// ---- precedence ----------------------------------------------------------------------

enum class Rung : std::uint8_t { Flag, Env, Working, Builtin };
std::string_view rung_name(Rung r);  // "flag" | "environment" | "working copy" | "built-in default"

struct Resolved {
  std::string value;
  Rung rung = Rung::Builtin;
  bool operator==(const Resolved&) const = default;
};

// The rule. An EMPTY string at a rung means "not given there".
Resolved resolve_setting(std::string_view flag, std::string_view env, std::string_view working, std::string_view builtin);

// Which store a setting lives in. The column exists so that ONE table and ONE
// `resolve_setting` serve every domain (Phase 10 m1): before it, "layout" was a Theme
// row only because the layout was part of the Theme, and a host had to know that.
enum class Domain : std::uint8_t { Theme, Layout, Bindings };
std::string_view domain_name(Domain d);  // "theme" | "layout" | "bindings"

// The table. `env_suffix` is what the host's prefix is joined to ("ROLL_" + "THEME").
struct SettingSpec {
  std::string_view key;
  Domain domain;
  std::string_view env_suffix;
  std::string_view builtin;
  std::string_view values;  // for help text
};
inline constexpr SettingSpec kSettings[] = {
    {"theme", Domain::Theme, "THEME", "default", "a preset name (see `theme list`) or a preset file"},
    {"layout", Domain::Layout, "LAYOUT", "default", "a shipped layout, a layouts/ file name, or a layout file"},
    {"theme_mode", Domain::Theme, "THEME_MODE", "auto", "auto | dark | light"},
    {"color_depth", Domain::Theme, "COLOR_DEPTH", "auto", "auto | truecolor | 256 | 16 | mono"},
    {"bindings", Domain::Bindings, "BINDINGS", "default", "a bindings preset name (see `bindings list`) or a bindings file"},
};
const SettingSpec* setting(std::string_view key);  // nullptr when unknown
// The working copy's value of a setting: the origin for "theme" / "layout" / "bindings",
// the mode and the depth from the Theme working copy.
std::string working_value(const ThemePresets& store, std::string_view key);
std::string working_value(const LayoutPresets& store, std::string_view key);    // "layout": the origin
std::string working_value(const BindingsPresets& store, std::string_view key);  // "bindings": the origin

}  // namespace rolltui
