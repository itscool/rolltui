#pragma once
//
// rolltui/tools/layout_editor.hpp — the layout editor (the plan, milestone 16,
// formerly 11d): the sibling of the theme editor, the same shape — a MODEL with no
// terminal in it, a rolltui::Menu over the SELECTED NODE of the split tree, the same
// recovery model (live preview; Enter commits, Escape cancels the focused change;
// Ctrl-Z / Ctrl-Y over whole-layout snapshots on the one UndoStack type; a reset only
// through the host's confirm popup). Every operation is a pure edit of the tree
// milestone 8 built, and the layout being edited is the base layer the host draws, so
// the preview is the real thing floating under the editor's side popup.
//
// SELECTION: one node id (a window or a split), drawn by the host in border_active
// (a borderless node is tinted with the selection role). Tab / Shift-Tab step through
// the tree in order; a click on a window selects it (the host maps the pointer).
//
// OPERATIONS on the selected node (the menu's top level; ids the host never binds —
// the editor applies them itself and reports Committed):
//   split into a row / a column   the node becomes [node, a copy of it] in a Row/Column
//                                 (the copy is "<id>-2" with the same content)
//   swap with the previous / next sibling
//   hide / show                    a hidden node takes no space (Layout.hpp)
//   border                         choice: none | single | rounded | double | heavy (live)
//   title                          input (live as typed)
//   widget kind                    input: ANY kind name (live as typed). The kinds this
//                                 binary can preview are the field's HINT (set_kinds); a
//                                 name outside them is written and previews as a labelled
// placeholder
//   source                         input, TYPED BY THE KIND (below)
//   menu file                      input: any menu name, the resolvable ones as its hint
//   size                           input: fill | fill N | N% | N cells (live as typed);
//                                 Alt+arrows nudge the size by one cell (a fill becomes
//                                 its current extent first), each nudge a commit
//   focusable                      toggle
//   delete                         removes the node; a split left with one child collapses
//   popups                         a level per popup: x, y, w, h (dims), anchor (choice),
//                                 modal (toggle), remove; and "add a popup" (input: id)
//   minimum width / height        input, Int — the smallest screen this SCREEN is designed
//                                 for (0 = it states none)
//   focused window                choice over the base layer's focusable windows, plus
//                                 "(none)" for "the first in tree order"
//   actions                        a level per declared action (description, remove) and
//                                 "add an action" — the app-scope actions this SCREEN
//                                 emits (Layout.hpp); a host re-declares them into its
//                                 bindings table when the layout changes, so a key and a
//                                 help line exist for an action added here in the next
//                                 frame
//   new layout / load layout / save layout as / reset to loaded / undo / redo
//
// CREATING A LAYOUT IS NOT INHERITING ONE. "New layout" replaces the whole
// layout with a STATED MINIMAL SKELETON — one bordered `text:` window (the one kind that
// names nothing a host must have bound), no popups, no actions, and no thresholds — never
// the open screen with its parts stripped out. The measurement that scoped this: stripping
// `no-panel` down to one window and saving it as `myapp` produced a file carrying roll's
// five `app.*` actions, four popups pointing at roll's own composites, and `no-panel`'s
// min sizes — none of which the author chose, and all of which the target app then reads.
// NOTHING ELSE IS INHERITED, INCLUDING THE THRESHOLDS. A new layout takes the host's
// `set_default_min` and nothing more. A designer states the size their screen needs the way
// they state everything else about it — by typing it — which is one field against a whole
// mechanism whose only job would be to answer it, and the screen is where the answer belongs.
//
// CONTENT IS TWO FIELDS, AND EXACTLY ONE WRITES THE SOURCE. A window's
// content is `kind[:source]`, so the editor shows the kind as a choice over the offered
// kinds and the source beside it. Which field owns the source is a stated function of
// the kind, never a guess — the other is drawn disabled, so a screen never offers two
// ways to say one thing:
//   menu                        the MENU FILE choice owns it (the names that actually
//                               resolve, from the host: Windows::menu_names())
//   help                        the SOURCE input, Name, OPTIONAL — one key scope, or
// every scope the app has when empty. It is
//                               still the one kind that DROPS a source carried over from
//                               another kind, and now for a stated reason rather than
//                               because the source was forbidden: every other kind's
//                               source is a bound NAME, and a scope is not one, so
//                               carrying one in makes a window that draws nothing
//   text                        the SOURCE input, Text, optional (a literal may be empty)
//   file                        the SOURCE input, Text (a path)
//   transcript, input, rows     the SOURCE input, Name (a bound name)
//   a REGISTERED kind           the SOURCE input, Name, obeying the rule ITS HOST gave it
//                               — so roll's `approval` (Forbidden) disables the field
//                               exactly as `help` does, while a `canvas:main` names a
//                               source and is hinted with the profile's own words. The
//                               editor asks the REGISTRY rather than a table of its own
//: which kinds exist is a fact about the
//                               target app, and the tool is not the app.
// Changing the kind KEEPS the source verbatim (the `help` rule above is the one
// exception): a kind that requires a source and has none is left saying so — the window
// draws its error panel and the status line names it — rather than the editor inventing
// a name that happens to bind. The host's own offered contents (set_sources) are the
// source field's HINT, never a substitute for what is typed.
//
// AND NEITHER FIELD IS BOUNDED BY WHAT THIS BINARY CAN BUILD. The kind was a
// closed choice and `set_content` refused a name outside it, which made the tool the
// authority on what an app may be asked to provide. A screen is the intent: it names what it
// needs, this tool previews what it can, and the app REPORTS the rest at start-up
// (`rolltui_gaps_collect`).
// DRAGGING A SHARED EDGE (the host maps the pointer to a seam and calls begin_drag /
// drag_to / end_drag): the child before the seam takes an absolute size equal to the
// pointer's distance from its start; the release commits once.
//
#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_layout_tree.h"  /* INTERNAL: this editor opts in — it walks and MUTATES a tree */
#include "rolltui/c/rolltui_menu.h"
#include "tool_str.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tool_actions.hpp"
#include "undo_stack.hpp"

namespace rolltui::tools {

// The C tree types, under the names every editor already writes them as (Layout.hpp's own
// aliases, before it went away). Layout/Node/Layer/Dim/SplitSize/Content ARE their C structs
// (one definition), so these stay ordinary value members below — RolltuiBindings/RolltuiMenu
// do not, which is the one real split in this file's own state.
using Layout = RolltuiLayout;
using Node = RolltuiLayoutNode;
using Layer = RolltuiLayer;
using Dim = RolltuiDim;
using SplitSize = RolltuiSplitSize;
using Content = RolltuiContent;
using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;

// One of the library's shipped built-in screens ("default", "panel-left", "no-panel",
// "stacked"), freshly parsed — BY VALUE, unlike the deleted `rolltui::builtin_layout`,
// which cached a pointer into a process-wide table. A tool calls this a handful of times
// (once at construction, a handful more in its own tests), so the cache that made sense
// for every host asking every frame is not worth carrying here; composed from
// `rolltui_layout_builtin_json` + `rolltui_load_layout_text` +
// `rolltui_loaded_layout_to_layout` (rolltui/c/rolltui_layout.h) — all three permanent C
// entry points, the last one promoted for exactly this call. An unknown
// name parses the empty string and comes back an empty Layout; every caller here only
// ever asks for "default", which always exists.
Layout builtin_layout(RolltuiContext* ctx, std::string_view name);

class LayoutEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, LoadLayout, ResetLoaded, Closed };
    Kind kind = Kind::None;
    std::string value;  // SaveAs: the file name; LoadLayout: the name or path
    bool operator==(const Outcome&) const = default;
  };

  // the kind registry belongs to a CONTEXT, and this editor types a source field by
  // asking it what a kind's shape is — so it holds the session it was opened for. BORROWED: the
  // studio owns it and outlives every editor.
  explicit LayoutEditor(RolltuiContext* ctx);
  ~LayoutEditor() { rolltui_menu_free(menu_); }
  LayoutEditor(const LayoutEditor&) = delete;
  LayoutEditor& operator=(const LayoutEditor&) = delete;

  void load(const Layout& layout);                      // the baseline; undo restarts; selection: the first window
  // The contents the host OFFERS ("transcript:session", "rows:status", …) — the hint
  // beside the source field for the selected kind. A hint, not a menu: a source the
  // host has not bound is still typeable, and reports itself in the window.
  void set_sources(std::vector<std::string> contents);
  // The kinds THIS BINARY can preview — the widget-kind field's HINT, and nothing more. Making
  // it the field's closed option list, with a name outside it refused, is a design tool
  // deciding what an app may be asked for.
  void set_kinds(std::vector<std::string> names);
  void set_menus(std::vector<std::string> names);      // the menu names that RESOLVE, as that field's hint
  void set_layouts(std::vector<std::string> names);    // the Load choice's options
  // What "New layout" starts its thresholds at. 0/0 (the default) means the screen states
  // none, and a designer types the size their screen needs — see the header.
  void set_default_min(int width, int height);
  // The minimal skeleton "New layout" starts from, exposed so a test can assert what it
  // is rather than what it renders as.
  Layout skeleton(std::string name) const;

  const Layout& current() const { return current_; }   // committed + any live change
  const Layout& committed() const { return undo_.current(); }
  bool previewing() const { return preview_.has_value(); }
  const std::string& selected() const { return sel_; }
  void select(std::string_view id);
  void select_next(bool backwards = false);
  const Node* selected_node() const;

  RolltuiMenu* menu() { return menu_; }
  const RolltuiMenu* menu() const { return menu_; }
  Outcome handle(const RolltuiEvent* e, const RolltuiBindings* nav);  // `nav`: the host's bindings (menu + editor scopes)
  Outcome handle(const RolltuiEvent* e) { return handle(e, editor_bindings(ctx_)); }   // Alt+arrows nudge; Ctrl-Z/Ctrl-Y; Tab / Shift-Tab select; the rest is the menu's
  bool undo();
  bool redo();
  std::size_t undo_depth() const { return undo_.undo_depth(); }
  std::size_t redo_depth() const { return undo_.redo_depth(); }
  void replace(Layout l);           // a whole new committed layout (a reset, a load)

  // The seam drag: `id` is the child BEFORE the seam (its parent is a Row when
  // `horizontal`, else a Column); extents are cells from the child's start.
  void begin_drag(std::string_view id);
  void drag_to(int extent);         // live
  Outcome end_drag();               // commits
  bool dragging() const { return drag_.has_value(); }

  // REFILLED into a string the caller keeps: the studio draws this every frame an editor is
  // open. The returning form is one copy over it, for a test that reads it.
  void status_line(std::string& out) const;
  std::string status_line() const;
  // What the selected node IS, in words: "selected: input  size 3  border single
  // input:prompt". The editor composes it because it is the one place that reads a
  // window's content — every other host resolves it through rolltui::Windows.
  std::string selection_line() const;

  // Tree helpers, exposed for the tests and the host's hit-testing.
  static Node* find_node(Node& root, std::string_view id);
  static const Node* find_node(const Node& root, std::string_view id);
  static Node* parent_of(Node& root, std::string_view id, std::size_t* index = nullptr);
  static std::vector<std::string> ids_in_order(const Node& root);  // every node id, tree order
  // EVERY node the editor can select: the base tree, then each popup's tree. A popup's root IS
  // a node — it has an id, a content, a border, a title, a background — and previously
  // it was the one node nothing could reach, so a popup drew `text:<its own id>` for ever.
  // Rejected: repeating the per-node fields inside the Popups submenu — `rolltui.h` rule 5, if
  // two consumers write the same wrapper the API is wrong, not the consumers.
  std::vector<std::string> all_ids() const;
  Node* find_any(std::string_view id);
  const Node* find_any(std::string_view id) const;
  Layer* popup_of(std::string_view id);              // the popup whose tree holds it, else null
  const Layer* popup_of(std::string_view id) const;

  // The selected window's content split at the first ':' — WITHOUT requiring it to parse, so
  // a content typed by hand into a file can be shown and repaired here.
  //
  // `content` is no longer optional and `known` is the separate answer. It went
  // empty for a kind neither rung of the registry had, and every field that read it then went
  // inert — the tool refusing to hold a screen it could not preview. `known` says whether THIS
  // binary can build the kind, which is all this tool ever knew; whether the app being designed
  // for can is the developer's answer, and the gap report is where they get asked for it.
  struct ContentParts {
    Content content;    // the kind name and source as typed, always
    bool known = false; // …and whether either rung of THIS binary's registry has that kind
    bool window = false;
    std::string kind_text, source;
  };
  ContentParts content_parts() const;

 private:
  RolltuiContext* ctx_;        // BORROWED: the session this editor resolves kinds against
  ContentParts parts_of(const Node* n) const;  // Phase 25: resolves kinds against `ctx_`
  std::string base_source() const;  // the source before the live preview began
  std::string carried_source(std::string_view kind_name) const;  // …and whether that kind takes it
  enum class Op { SplitRow, SplitColumn, SwapPrev, SwapNext, ToggleVisible, Delete, ToggleFocusable };
  bool apply_op(Op op);
  void rebuild_menu();
  void sync_values();
  void sync_content_fields();  // the kind/source/menu-file values, specs and enabled-ness
  void sync_hints();           // the kind and menu-file hints: what this binary can preview
  // Writes kind[:source] into the selected window. By NAME, because a kind's name is the only
  // thing that identifies it. A name in neither rung writes nothing and is reported.
  bool set_content(const std::string& kind_name, const std::string& source);
  void begin_preview();
  void cancel_preview();
  Outcome commit_current();
  Node* sel_node();
  std::string unique_id(const std::string& base) const;
  std::vector<MenuItem> action_items() const;

  RolltuiMenu* menu_ = rolltui_menu_new();
  Layout current_;
  UndoStack<Layout> undo_;
  std::optional<Layout> preview_;
  std::string sel_;
  std::vector<std::string> sources_;   // the host's offered kind[:source] contents
  std::vector<std::string> kinds_;     // the kinds the target can build (library's by default)
  std::vector<std::string> menus_;     // the menu files that resolve
  std::vector<std::string> layouts_;
  int default_min_w_ = 0, default_min_h_ = 0;  // the target app's, for a NEW layout only
  std::optional<std::string> drag_;
  std::string status_;
};

}  // namespace rolltui::tools
