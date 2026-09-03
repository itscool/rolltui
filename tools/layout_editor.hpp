#pragma once
//
// rolltui/tools/layout_editor.hpp — the layout editor (plan/phase-9.md, milestone 16,
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
//   widget kind                    choice over Layout.hpp's CLOSED table (live)
//   source                         input, TYPED BY THE KIND (below)
//   menu file                      choice over the menu files that resolve (below)
//   size                           input: fill | fill N | N% | N cells (live as typed);
//                                 Alt+arrows nudge the size by one cell (a fill becomes
//                                 its current extent first), each nudge a commit
//   focusable                      toggle
//   delete                         removes the node; a split left with one child collapses
//   popups                         a level per popup: x, y, w, h (dims), anchor (choice),
//                                 modal (toggle), remove; and "add a popup" (input: id)
//   actions                        a level per declared action (description, remove) and
//                                 "add an action" — the app-scope actions this SCREEN
//                                 emits (Layout.hpp); a host re-declares them into its
//                                 bindings table when the layout changes, so a key and a
//                                 help line exist for an action added here in the next
//                                 frame
//   load layout / save layout as / reset to loaded / undo / redo
//
// CONTENT IS TWO FIELDS, AND EXACTLY ONE WRITES THE SOURCE (Phase 10 m5). A window's
// content is `kind[:source]`, so the editor shows the kind as a choice over the closed
// table and the source beside it. Which field owns the source is a stated function of
// the kind, never a guess — the other is drawn disabled, so a screen never offers two
// ways to say one thing:
//   menu                        the MENU FILE choice owns it (the names that actually
//                               resolve, from the host: Windows::menu_names())
//   help                        neither: a source is forbidden, and changing the kind
//                               to `help` DROPS the source rather than making the
//                               content unparseable
//   text                        the SOURCE input, Text, optional (a literal may be empty)
//   file                        the SOURCE input, Text (a path)
//   transcript, input, rows, custom
//                               the SOURCE input, Name (a bound name)
// Changing the kind KEEPS the source verbatim (the `help` rule above is the one
// exception): a kind that requires a source and has none is left saying so — the window
// draws its error panel and the status line names it — rather than the editor inventing
// a name that happens to bind. The host's own offered contents (set_sources) are the
// source field's HINT, never a substitute for what is typed.
// DRAGGING A SHARED EDGE (the host maps the pointer to a seam and calls begin_drag /
// drag_to / end_drag): the child before the seam takes an absolute size equal to the
// pointer's distance from its start; the release commits once.
//
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Menu.hpp"
#include "rolltui/Undo.hpp"
#include "tool_actions.hpp"

namespace rolltui::tools {

class LayoutEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, LoadLayout, ResetLoaded, Closed };
    Kind kind = Kind::None;
    std::string value;  // SaveAs: the file name; LoadLayout: the name or path
    bool operator==(const Outcome&) const = default;
  };

  LayoutEditor();
  void load(const Layout& layout);                      // the baseline; undo restarts; selection: the first window
  // The contents the host OFFERS ("transcript:session", "rows:status", …) — the hint
  // beside the source field for the selected kind. A hint, not a menu: a source the
  // host has not bound is still typeable, and reports itself in the window.
  void set_sources(std::vector<std::string> contents);
  void set_menus(std::vector<std::string> names);      // the Menu file choice's options (Windows::menu_names())
  void set_layouts(std::vector<std::string> names);    // the Load choice's options

  const Layout& current() const { return current_; }   // committed + any live change
  const Layout& committed() const { return undo_.current(); }
  bool previewing() const { return preview_.has_value(); }
  const std::string& selected() const { return sel_; }
  void select(std::string_view id);
  void select_next(bool backwards = false);
  const Node* selected_node() const;

  Menu& menu() { return menu_; }
  const Menu& menu() const { return menu_; }
  Outcome handle(const Event& e, const Bindings& nav);  // `nav`: the host's bindings (menu + editor scopes)
  Outcome handle(const Event& e) { return handle(e, editor_bindings()); }   // Alt+arrows nudge; Ctrl-Z/Ctrl-Y; Tab / Shift-Tab select; the rest is the menu's
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

  // The selected window's content split at the first ':' — WITHOUT requiring it to
  // parse, so a content typed by hand into a file can be shown and repaired here. `kind`
  // is nullopt when the text before the colon is not in the table.
  struct ContentParts {
    std::optional<WidgetKind> kind;
    std::string kind_text, source;
  };
  ContentParts content_parts() const;

 private:
  static ContentParts parts_of(const Node* n);
  std::string base_source() const;  // the source before the live preview began
  enum class Op { SplitRow, SplitColumn, SwapPrev, SwapNext, ToggleVisible, Delete, ToggleFocusable };
  bool apply_op(Op op);
  void rebuild_menu();
  void sync_values();
  void sync_content_fields();  // the kind/source/menu-file values, specs and enabled-ness
  void set_content(WidgetKind kind, const std::string& source);  // writes kind[:source] into the selected window
  void begin_preview();
  void cancel_preview();
  Outcome commit_current();
  Node* sel_node();
  std::string unique_id(const std::string& base) const;
  std::vector<MenuItem> action_items() const;

  Menu menu_;
  Layout current_;
  UndoStack<Layout> undo_;
  std::optional<Layout> preview_;
  std::string sel_;
  std::vector<std::string> sources_;   // the host's offered kind[:source] contents
  std::vector<std::string> menus_;     // the menu files that resolve
  std::vector<std::string> layouts_;
  std::optional<std::string> drag_;
  std::string status_;
};

}  // namespace rolltui::tools
