#pragma once
//
// rolltui/Bindings.hpp — key bindings as DATA (plan/phase-9.md, milestone 17, formerly
// 11e). Every key a widget or a host acts on is a named ACTION bound to one or more
// CHORDS; a widget's handle() takes a `const Bindings&` and asks "what action is this
// key?" instead of switching on keys. The library's default is a FILE in the same
// format (rolltui/presets/bindings/default.json), embedded at build time and parsed at
// start like the built-in layouts — so `roll bindings show default` emits the real
// default, a custom file starts from it, and the help popup is RENDERED from the live
// table (help_lines) and can never lie about a rebinding.
//
// CHORDS are written "ctrl+shift+left", "alt+enter", "f1", "escape", "?", in any
// modifier order, case-insensitive; a printable character is itself ("?", "y"). The
// key names are the KeyEvent's: enter tab backspace escape up down left right home
// end pageup pagedown insert delete f1..f12, or a single character. parse_chord and
// chord_to_string round-trip; a KeyEvent's `raw` is never part of a chord.
//
// SCOPES: an action's name is "<scope>.<verb>" — input, transcript, menu, edit (a menu
// field being edited: commit, cancel, step; the caret keys are the input scope's), stack,
// app, editor, playground. The same chord may serve different scopes (Up moves the caret in
// the input, the selection in a menu, the view in the transcript); a chord bound to two
// actions of ONE scope is a CONFLICT and the loader reports it. Lookup is by scope:
// action_for(event, "input") answers with an input.* action or "".
//
// THE ONE RULE THAT CANNOT BE REBOUND (the plan's standing rule, "Enter is always
// submit"): `enter` must be a chord of input.submit and of no other input.* action; a
// file that tries is a reported bad value and the default binding is kept. Ctrl-C is
// the host's and is not an action here.
//
// A PRINTABLE CHARACTER WITHOUT CTRL OR ALT IS TEXT for the input widget — it inserts
// and is never looked up there — but any other scope may bind one (roll binds "?" to
// app.help while the input is empty). Stated so no file can make typing a letter do
// something else in the input.
//
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/Json.hpp"
#include "rolltui/Keys.hpp"

namespace rolltui {

struct ActionInfo {
  std::string_view name;         // "input.submit"
  std::string_view description;  // "send the line"
};
// Every action the library's widgets and a typical host act on, with what it does — the
// help popup is rendered from this list and the live chords. A host may add its own
// scope's actions (Bindings::add_action) before loading a file.
const std::vector<ActionInfo>& library_actions();
std::string_view scope_of(std::string_view action);  // "input" of "input.submit"

std::optional<KeyEvent> parse_chord(std::string_view text);
std::string chord_to_string(const KeyEvent& k);   // "ctrl+shift+left"; "" for an Unknown key
std::string chord_display(const KeyEvent& k);     // "Ctrl-Shift-Left" for help text

struct BindingsLoadReport {
  std::string error;                        // unusable
  std::vector<std::string> unknown_actions; // named in the file, not known
  std::vector<std::string> bad_chords;      // "input.left: 'ctrl+meta+x' is not a chord"
  std::vector<std::string> conflicts;       // "'up' bound to both input.up and input.history_prev"
  std::vector<std::string> bad_values;      // the Enter rule, a non-array, ...
  std::vector<std::string> unknown_keys;
  bool clean() const {
    return error.empty() && unknown_actions.empty() && bad_chords.empty() && conflicts.empty() && bad_values.empty() && unknown_keys.empty();
  }
  std::string summary() const;
};

class Bindings {
 public:
  Bindings();  // empty: nothing bound (use default_bindings() for the shipped table)

  // ---- lookup ----
  // The action of `scope` bound to this key, or "" when none. `k.raw` is ignored.
  std::string_view action_for(const KeyEvent& k, std::string_view scope) const;
  const std::vector<KeyEvent>& chords_for(std::string_view action) const;
  std::string chords_text(std::string_view action) const;  // "Ctrl-W, Alt-Backspace" for help
  bool has(std::string_view action) const;
  const std::vector<std::string>& actions() const { return actions_; }  // known actions, in table order

  // ---- edits (an editor's; the loader uses them too) ----
  void add_action(std::string_view action, std::string_view description);  // a host's own; no-op when known
  std::string_view description(std::string_view action) const;
  // Binds `chord` to `action`; a chord already bound to another action of the same
  // scope moves (the conflict resolves to the new binding; `moved_from` says which).
  // Refused (false) for an unknown action or a chord that breaks the Enter rule.
  bool bind(std::string_view action, const KeyEvent& chord, std::string* moved_from = nullptr);
  bool unbind(std::string_view action, const KeyEvent& chord);
  void clear(std::string_view action);
  bool operator==(const Bindings& o) const { return table_ == o.table_; }

  // ---- file format ----
  //   { "name": "default", "bindings": { "input.submit": ["enter"], "input.newline": ["alt+enter"], ... } }
  // An action absent from the file keeps NO chords (a file is the whole domain — rule
  // 1 of the preset system); the loader reports what it could not use and keeps the
  // rest. load() starts from an empty table.
  static std::optional<Bindings> from_json(const json::Value& v, BindingsLoadReport& report);
  static std::optional<Bindings> from_json(std::string_view text, BindingsLoadReport& report);
  json::Value to_json(std::string_view name) const;

 private:
  std::vector<std::string> actions_;
  std::vector<std::string> descriptions_;
  std::vector<std::pair<std::string, std::vector<KeyEvent>>> table_;  // action → chords, table order
  std::vector<KeyEvent>& chords_mut(std::string_view action);
};

// The shipped default (rolltui/presets/bindings/default.json), parsed once. Every
// widget's compiled-in behaviour before this milestone is exactly this table — the
// widget tests drive the widgets through it.
const Bindings& default_bindings();
std::string_view default_bindings_json();

// Help text from the live table: one line per action in `actions` (an empty list means
// every action of `scope`): "Enter            send the line".
std::vector<std::string> help_lines(const Bindings& b, std::string_view scope, const std::vector<std::string>& actions = {});

}  // namespace rolltui
