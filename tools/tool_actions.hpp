#pragma once
//
// rolltui/tools/tool_actions.hpp — the actions of the library's own TOOLS, and the chords
// they suggest (plan/phase-11.md, milestone 1).
//
// These used to sit in library_actions() beside the widget scopes, which made every
// rolltui host declare them whether or not it mounted the tool: roll listed eight keys it
// could not press, and `roll bindings save` wrote them into the user's key file. A widget
// scope is universal — every host that draws an input has input.* — and a TOOL's scope is
// not, so it lives here and a host that mounts the tool declares it:
//
//     bindings.declare(my_layout.actions, tools::editor_actions());
//
// — ONE call, because declare() is authoritative over every non-library scope and a
// second call would undeclare the first's (Bindings.hpp).
//
// The chord in each row is a SUGGESTION and never an override: declare() installs it only
// where the table carries no row for the action at all. See Bindings.hpp's "A TOOL ALSO
// BRINGS THE CHORDS IT SUGGESTS" for why a tool states its keys in code while a layout
// must not.
//
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Layout.hpp"  // shipped_default_actions(), for editor_bindings() below

namespace rolltui::tools {

// The `editor` scope: what a host that mounts the three editors (theme, layout, keys)
// declares. undo/redo are handled by all three; the other three OPEN one, and so belong
// to the host doing the mounting rather than to any editor.
inline const std::vector<ToolAction>& editor_actions() {
  static const std::vector<ToolAction> t = {
      {"editor.undo", "undo the last committed change", "ctrl+z"},
      {"editor.redo", "redo", "ctrl+y"},
      {"editor.theme", "open the theme editor", "f4"},
      {"editor.layout", "open the layout editor", "f6"},
      {"editor.keys", "open the keys editor", "f7"},
  };
  return t;
}

// The shipped default table with the three editors MOUNTED: what a host that mounts them
// and has loaded nothing of its own is running. It is what an editor's convenience
// `handle(e)` overload looks its own Ctrl-Z up in — before m1 that overload asked
// default_bindings(), which knew `editor.undo` because the library did; now the library
// does not, and an editor with no host table around it would have no undo key at all.
inline const Bindings& editor_bindings() {
  static const Bindings b = [] {
    Bindings x = default_bindings();
    x.declare(shipped_default_actions(), editor_actions());  // the shipped screen, plus the editors
    return x;
  }();
  return b;
}

// The `playground` scope: the playground binary's own three.
inline const std::vector<ToolAction>& playground_actions() {
  static const std::vector<ToolAction> t = {
      {"playground.cycle_theme", "cycle the shipped theme presets", "f3"},
      {"playground.reload", "reload the fixture", "f5"},
      {"playground.quit", "quit", "ctrl+q"},
  };
  return t;
}

}  // namespace rolltui::tools
