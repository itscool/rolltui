#pragma once
//
// rolltui/tools/tool_actions.hpp — the actions of the library's own TOOLS, and the chords
// they suggest. Calls the C API directly, like every other C++ consumer.
//
// These used to sit in library_actions() beside the widget scopes, which made every
// rolltui host declare them whether or not it mounted the tool: roll listed eight keys it
// could not press, and `roll bindings save` wrote them into the user's key file. A widget
// scope is universal — every host that draws an input has input.* — and a TOOL's scope is
// not, so it lives here and a host that mounts the tool declares it:
//
//     rolltui_bindings_declare(b, my_layout_actions, my_layout_actions_n, tools, tools_n);
//
// — ONE call, because rolltui_bindings_declare is authoritative over every non-library
// scope and a second call would undeclare the first's (rolltui_bindings.h).
//
// The chord in each row is a SUGGESTION and never an override: declare() installs it only
// where the table carries no row for the action at all. See rolltui_bindings.h's
// "DECLARING, SUGGESTING, AND THE SHIPPED TABLE" for why a tool states its keys in code
// while a layout must not.
//
#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_lifetime.h"
#include "tool_str.hpp"
#include <cstddef>
#include <span>


namespace rolltui::tools {

// The `editor` scope: what a host that mounts the FOUR editors (theme, layout, keys, menu)
// declares. undo/redo are handled by all four; the other four OPEN one, and so belong
// to the host doing the mounting rather than to any editor. All FOUR are here because a
// screen is four file types, and a tool that can author three of them cannot author a screen.
inline std::span<const RolltuiToolAction> editor_actions() {
  static const RolltuiToolAction t[] = {
      {"editor.undo", "undo the last committed change", "ctrl+z"},
      {"editor.redo", "redo", "ctrl+y"},
      {"editor.theme", "open the theme editor", "f4"},
      {"editor.layout", "open the layout editor", "f6"},
      {"editor.keys", "open the keys editor", "f7"},
      {"editor.menu", "open the menu editor", "f8"},
  };
  return t;
}

namespace detail {
// The one home for editor_bindings()'s clone, so the shutdown releaser below can reach
// the same slot it was registered from — the shape `rolltui::default_bindings()`
// (Bindings.cpp) already uses for its own process-wide cache.
inline RolltuiBindings*& editor_bindings_slot() {
  static RolltuiBindings* b = nullptr;
  return b;
}
}  // namespace detail

// The shipped default table with the three editors MOUNTED: what a host that mounts them
// and has loaded nothing of its own is running. It is what an editor's convenience
// `handle(e)` overload looks its own Ctrl-Z up in. The library's own table does NOT know
// `editor.undo` — an editor is a tool's, not the library's — so an editor with no host table
// around it has no undo key at all.
//
// A CLONE of rolltui_bindings_default(), because the shipped cache is borrowed and this
// table is then declared into — freed at rolltui_shutdown() (Lifetime's rule: register a
// releaser where the retained thing is made), so it never shows as a leak.
//
// it takes the SESSION whose shipped table it clones. The slot stays one per
// process because this is a HOST-side convenience over one binary's three editors, and a
// binary runs one editor set; what changed is that the table it copies is a context's, so
// the context has to be named rather than assumed.
inline const RolltuiBindings* editor_bindings(RolltuiContext* c) {
  RolltuiBindings*& slot = detail::editor_bindings_slot();
  if (slot) return slot;
  slot = rolltui_bindings_clone(rolltui_bindings_default(c));
  std::size_t layout_n = 0;
  const RolltuiLayoutAction* layout_actions = rolltui_layout_shipped_default_actions(c, &layout_n);
  const std::span<const RolltuiToolAction> tools = editor_actions();
  rolltui_bindings_declare(slot, layout_actions, layout_n, tools.data(), tools.size());
  rolltui_on_shutdown([] {
    RolltuiBindings*& s = detail::editor_bindings_slot();
    rolltui_bindings_free(s);
    s = nullptr;
  });
  return slot;
}

// The `studio` scope: the studio binary's own three. A bindings file naming their older
// spellings is rewritten once by the loader — the migration table in the bindings module is the
// only place those old names still exist, so an old Ctrl-Q still quits.
inline std::span<const RolltuiToolAction> studio_actions() {
  static const RolltuiToolAction t[] = {
      {"studio.cycle_theme", "cycle the shipped theme presets", "f3"},
      {"studio.reload", "reload the fixture", "f5"},
      {"studio.save_as", "save the preset being edited as a file", "alt+s"},
      {"studio.quit", "quit", "ctrl+q"},
  };
  return t;
}

}  // namespace rolltui::tools
