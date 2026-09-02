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
//                                 (the copy is "<id>-2" with the same content slot)
//   swap with the previous / next sibling
//   hide / show                    a hidden node takes no space (Layout.hpp)
//   border                         choice: none | single | rounded | double | heavy (live)
//   title                          input (live as typed)
//   content slot                   choice over the host's slots (live)
//   size                           input: fill | fill N | N% | N cells (live as typed);
//                                 Alt+arrows nudge the size by one cell (a fill becomes
//                                 its current extent first), each nudge a commit
//   focusable                      toggle
//   delete                         removes the node; a split left with one child collapses
//   popups                         a level per popup: x, y, w, h (dims), anchor (choice),
//                                 modal (toggle), remove; and "add a popup" (input: id)
//   load layout / save layout as / reset to loaded / undo / redo
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
  void set_slots(std::vector<std::string> slots);      // the host's content slots, for the Content choice
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
  Outcome handle(const Event& e) { return handle(e, default_bindings()); }   // Alt+arrows nudge; Ctrl-Z/Ctrl-Y; Tab / Shift-Tab select; the rest is the menu's
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

  // Tree helpers, exposed for the tests and the host's hit-testing.
  static Node* find_node(Node& root, std::string_view id);
  static const Node* find_node(const Node& root, std::string_view id);
  static Node* parent_of(Node& root, std::string_view id, std::size_t* index = nullptr);
  static std::vector<std::string> ids_in_order(const Node& root);  // every node id, tree order

 private:
  enum class Op { SplitRow, SplitColumn, SwapPrev, SwapNext, ToggleVisible, Delete, ToggleFocusable };
  bool apply_op(Op op);
  void rebuild_menu();
  void sync_values();
  void begin_preview();
  void cancel_preview();
  Outcome commit_current();
  Node* sel_node();
  std::string unique_id(const std::string& base) const;

  Menu menu_;
  Layout current_;
  UndoStack<Layout> undo_;
  std::optional<Layout> preview_;
  std::string sel_;
  std::vector<std::string> slots_{"transcript", "status", "input", "help"};
  std::vector<std::string> layouts_;
  std::optional<std::string> drag_;
  std::string status_;
};

}  // namespace rolltui::tools
