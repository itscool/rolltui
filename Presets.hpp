#pragma once
//
// rolltui/Presets.hpp — the preset system (plan/phase-9.md, milestone 11 / 11e). The
// LIBRARY's, not any host's: a host hands it one directory and a may-write-shipped
// flag and gets a Theme back. Nothing of roll is in here.
//
// TWO DOMAINS, identical mechanics in each:
//   the Theme     colours (+ mode), the layout, the colour depth — how it looks and is
//                 arranged, one concept. Panel width is not a knob: it is the status
//                 window's `size` inside the layout part.
//   the Bindings  every key and mouse action, the whole input setup (Bindings.hpp) —
//                 milestone 17; a session is "Theme X + Bindings Y", two independent
//                 choices: changing keys never touches the look, and vice versa.
// One implementation of the mechanics, PresetStore<Domain> (PresetStore.hpp), and two
// Domain traits below; rolltui-presets-test asserts the five rules on BOTH.
//
// THE FIVE RULES, per domain (asserted in rolltui/tests/presets_test.cpp):
//   1. A preset is the ENTIRE domain, saved and loaded as one unit, never as pieces:
//      themes/<name>.json holds colours + layout + depth + mode.
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
// running — you edited it, so it is yours now. `kThemeSettings` is the table: the
// host supplies the flag value and the environment value (with its own prefix); the
// working-copy field and the built-in default are the library's.
//
// DIRECTORY (`Options::dir`, e.g. ~/.config/<host>/rolltui):
//   theme.working.json     the Theme working copy (rule 2), plus "preset": its origin
//   themes/<name>.json     user presets (rule 3)
//   layouts/<name>.json    layout FILES a user drops in — offered by name wherever a
//                          layout is chosen (plan Done-when j); choosing one copies it
//                          into the working copy's layout part
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

struct ThemePreset {
  json::Value colours;          // a theme file object (Theme.hpp's format; dark/light pairs allowed)
  Layout layout;                // Layout.hpp's format; the status window's size is the panel width
  std::string mode = "auto";    // auto | dark | light — auto: the host asks the terminal (OSC 11), dark when it cannot
  std::string depth = "auto";   // auto | truecolor | 256 | 16 | mono — auto: detect_color_depth over the environment
  bool operator==(const ThemePreset&) const = default;
};

// File format: { "name", "mode", "depth", "colours": {theme object}, "layout": {layout
// object} }. "colours" is required; a missing "layout" is the built-in default with a
// note. Values outside the sets above are bad values (the default kept).
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
  void set_layout(Layout l, bool persist = true) { edit([&](ThemePreset& p) { p.layout = std::move(l); }, persist); }
  void set_mode(std::string mode, bool persist = true) { edit([&](ThemePreset& p) { p.mode = std::move(mode); }, persist); }
  void set_depth(std::string depth, bool persist = true) { edit([&](ThemePreset& p) { p.depth = std::move(depth); }, persist); }
  // ---- layouts ----
  std::vector<std::string> layout_files() const;  // names in <dir>/layouts (without .json), sorted
  // A built-in layout name, a layouts/ file name, or a path.
  std::optional<Layout> find_layout(std::string_view name_or_path, LayoutLoadReport& report) const;
};

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

// The table. `env_suffix` is what the host's prefix is joined to ("ROLL_" + "THEME").
struct SettingSpec {
  std::string_view key;
  std::string_view env_suffix;
  std::string_view builtin;
  std::string_view values;  // for help text
};
inline constexpr SettingSpec kThemeSettings[] = {
    {"theme", "THEME", "default", "a preset name (see `theme list`) or a preset file"},
    {"layout", "LAYOUT", "default", "a built-in layout, a layouts/ file name, or a layout file"},
    {"theme_mode", "THEME_MODE", "auto", "auto | dark | light"},
    {"color_depth", "COLOR_DEPTH", "auto", "auto | truecolor | 256 | 16 | mono"},
};
inline constexpr SettingSpec kBindingsSettings[] = {
    {"bindings", "BINDINGS", "default", "a bindings preset name (see `bindings list`) or a bindings file"},
};
const SettingSpec* theme_setting(std::string_view key);      // nullptr when unknown
const SettingSpec* bindings_setting(std::string_view key);   // nullptr when unknown
// The working copy's value of a setting (the origin for "theme", the layout's name for
// "layout", the mode, the depth).
std::string working_value(const ThemePresets& store, std::string_view key);
std::string working_value(const BindingsPresets& store, std::string_view key);  // "bindings": the origin

}  // namespace rolltui
