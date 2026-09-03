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
// SCOPES: an action's name is "<scope>.<verb>". The same chord may serve different
// scopes (Up moves the caret in the input, the selection in a menu, the view in the
// transcript); a chord bound to two actions of ONE scope is a CONFLICT and the loader
// reports it. Lookup is by scope: action_for(event, "input") answers with an input.*
// action or "".
//
// WHO OWNS A SCOPE (Phase 10 milestone 4; split one level further in Phase 11 milestone
// 1). The library's own are CLOSED and listed in library_actions(): `input`,
// `transcript`, `menu`, `edit` (a menu field being edited: commit, cancel, step; the
// caret keys are the input scope's) and `stack` — the scopes the library's WIDGETS and
// window stack look up, and nothing else. Nothing may add to one of those scopes.
//
// A WIDGET's scope is universal, which is what makes it closed: every host that draws an
// input has input.*. A TOOL's is not. `editor.*` and `studio.*` belong to the ONE
// application that mounts the library's editors and its studio, and until Phase 11
// m1 they sat in this table beside the widget scopes — so EVERY host declared eight
// actions it could not perform, roll advertised Ctrl-Q / F4 / F6 / F7 and five more that
// did nothing, and `roll bindings save` wrote another program's keys into the user's own
// file. The rule that replaces it: A SCOPE IS CLOSED BECAUSE THE LIBRARY DEFINES IT,
// NEVER BECAUSE THE LIBRARY HAPPENS TO SHIP THE TOOL.
//
// EVERY OTHER SCOPE IS DECLARED BY WHOEVER SUPPLIES IT. A layout file lists the actions
// its screen emits with what they do (`app` above all — Layout.hpp's "actions"); a host
// that MOUNTS a tool declares that tool's (ToolAction below, and the tables in
// rolltui/tools/tool_actions.hpp). Both reach a table through declare(), which is
// AUTHORITATIVE over every non-library scope — so a host declares its layout's actions
// and its mounted tools' in ONE call, or the second call undeclares the first's. A host
// that mounts no tool declares no tool action and so advertises none: that is the whole
// of the milestone.
//
// A TOOL ALSO BRINGS THE CHORDS IT SUGGESTS; A LAYOUT NEVER DOES. Stated here because it
// reads as an inconsistency and is not. A layout is a FILE, and the bindings file beside
// it is exactly where its keys belong — the layout DECLARES an action, the bindings
// SUPPLY its keys, which is why m6 deleted `"shortcut": "F3"` from a layout file as a
// lie. A tool is CODE a host mounts, and no file can speak for it: the shipped bindings
// file belongs to every host, so a tool row in it is a key that every host NOT mounting
// that tool advertises and cannot press — the defect above, in file form. So the tool
// carries its own default chord, suggest() installs it into a gap, and a bindings file
// always wins (see suggest()).
//
// A CHORD FOR AN UNDECLARED ACTION IS KEPT AND DOES NOTHING. A bindings file is global
// and the user's; the actions are the screen's. So a file written while one layout was
// loaded must not lose its keys under another: the row survives load, save and the
// working copy's round trip, action_for() never answers with it (nothing can emit it),
// and declaring the action later makes it live — while UNdeclaring it (loading a layout
// that does not list it; declare() is authoritative) makes it inert again without
// touching its chords. Inert must also mean invisible: a menu item naming such an action
// shows no shortcut, or the menu promises a key that cannot fire. The one exception is a
// name in a LIBRARY scope that the library does not define ("input.sumbit"): that scope
// is closed, so the loader reports it as an unknown action instead of keeping a dead row.
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
// AN ACTION THAT WAS RENAMED IS MIGRATED BY THE LOADER, ONCE, AND SAID SO (Phase 11 m2,
// the shape of the layout loader's migrated_content). A bindings file is the user's, and
// renaming the studio binary renamed its three actions out from under every file already
// written. The old row would otherwise be kept-and-inert — the mercy rule above doing
// exactly the wrong thing, because the row LOOKS like another screen's when it is really
// this one's under its old name, and a person's Ctrl-Q would quietly stop quitting. So
// from_json() rewrites the name through migrated_action() before anything else reads it,
// and names the rewrite in `report.migrated`; the next save writes the new name.
// The old names themselves live in ONE table, in Bindings.cpp, deliberately the only
// place in any source that still carries them — asserted by a grep control.
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
// One action a LAYOUT declares (Layout.hpp), with owned strings because it comes from a
// file that may be reloaded under the table using it.
struct ActionDecl {
  std::string name;         // "app.help"
  std::string description;  // "open help"
  bool operator==(const ActionDecl&) const = default;
};
// One action of a TOOL a host MOUNTS — the library's own three editors and its studio
// (rolltui/tools/tool_actions.hpp), or a host's own — with the chord the tool
// suggests for it. A tool states its keys here because no bindings file can: see A TOOL
// ALSO BRINGS THE CHORDS IT SUGGESTS above.
struct ToolAction {
  std::string_view name;         // "studio.quit"
  std::string_view description;  // "quit"
  std::string_view chord;        // "ctrl+q" — a suggestion, never an override
};
// Every action the library's own WIDGETS and window stack act on, with what it does —
// the closed set (see WHO OWNS A SCOPE above). A tool's actions are NOT here, and
// neither is `app.*`: everything else reaches a table through Bindings::declare().
const std::vector<ActionInfo>& library_actions();
std::string_view scope_of(std::string_view action);  // "input" of "input.submit"
// Whether `scope` is one of the library's closed scopes. A layout that declares into one
// is a reported bad value, and a bindings file naming an action that does not exist in
// one is an unknown action rather than a kept-but-dead row.
bool library_scope(std::string_view scope);
// The new name of an action this library used to call something else, or nullopt. The
// loader rewrites through this and says so; see AN ACTION THAT WAS RENAMED above. One
// table, checked by name, never by scope prefix — a rename is a fact about three
// specific actions, not a rule about a word.
std::optional<std::string> migrated_action(std::string_view legacy);

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
  // Rows whose action was RENAMED and has been rewritten: "'<old>' → 'studio.quit'", so
  // a reader sees both names. Not a problem — the file loaded, every chord in it is live, and the
  // next save writes the new name — so `clean()` ignores it and a host says it once, the
  // way LayoutLoadReport::migrated is said.
  std::vector<std::string> migrated;
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
  // The actions this table has CHORDS for but nothing has declared — a bindings file's
  // rows for another screen's actions. They are kept and written back; they never match
  // a key. Named for a host that wants to say so; not a problem by itself.
  std::vector<std::string> undeclared() const;

  // ---- what THIS SCREEN can do: its layout's actions and its mounted tools' ----
  // The non-library actions this table knows become EXACTLY `declared` (the layout's)
  // plus every action of `tools` (the ones this host mounts), with their descriptions:
  // what is new is added, what the previous screen declared and this one does not is
  // undeclared again (its chords are kept — see the kept-and-inert rule above — but
  // nothing can emit it). Authoritative rather than additive because the layout is what
  // says which actions a screen has; merely adding leaves the last screen's keys live
  // under the next one. The library's own scopes are never touched. Idempotent and cheap,
  // so a host may call it every frame; call it after replacing the table from a preset
  // store and after the layout changes.
  //
  // BOTH IN ONE CALL, and the tools in this one rather than a second: declare() is
  // authoritative, so a separate call would undeclare whatever the first declared. A
  // tool's SUGGESTED CHORD is installed here too, for the same reason — the two cannot be
  // ordered wrongly if there is only one order. A suggestion fills a GAP and never
  // overrides: it is taken only when this table has no row for the action at all, so a
  // bindings file's row wins, EMPTY INCLUDED (an empty row is a file, or a user in the
  // keys editor, saying "unbound"; re-suggesting over it would bring a cleared key back
  // on the next frame). It is also declined when its chord already serves another action
  // of the same scope, or does not parse — the action is then declared UNBOUND, visible
  // as "(unbound)", rather than quietly sharing a chord that lookup would resolve by
  // table order.
  void declare(const std::vector<ActionDecl>& declared, const std::vector<ToolAction>& tools = {});

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
  // rest. load() starts from an empty table. A name outside the library's scopes is
  // kept as an UNDECLARED row (see the header comment) — the layout, not this file,
  // says which actions exist.
  static std::optional<Bindings> from_json(const json::Value& v, BindingsLoadReport& report);
  static std::optional<Bindings> from_json(std::string_view text, BindingsLoadReport& report);
  json::Value to_json(std::string_view name) const;

 private:
  std::vector<std::string> actions_;
  std::vector<std::string> descriptions_;
  std::vector<std::pair<std::string, std::vector<KeyEvent>>> table_;  // action → chords, table order
  std::vector<KeyEvent>& chords_mut(std::string_view action);
  void suggest(const std::vector<ToolAction>& tools);  // declare()'s gap-filling half
};

// The shipped default (rolltui/presets/bindings/default.json), parsed once, with the
// SHIPPED DEFAULT LAYOUT's actions declared into it — the two files ship together and
// are the library's one complete "default screen plus default keys", so a mismatch
// between them is a build error and aborts here (the shipped-preset standard). Every
// widget's compiled-in behaviour before milestone 17 is exactly this table — the widget
// tests drive the widgets through it.
const Bindings& default_bindings();
std::string_view default_bindings_json();

// Help text from the live table: one line per action in `actions` (an empty list means
// every action of `scope`): "Enter            send the line".
std::vector<std::string> help_lines(const Bindings& b, std::string_view scope, const std::vector<std::string>& actions = {});

}  // namespace rolltui
