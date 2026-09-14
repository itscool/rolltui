#pragma once
//
// rolltui/tools/menu_editor.hpp — THE MENU EDITOR: the fourth
// editor, and the one that finishes the tool. The other three author a theme, a layout
// and a bindings file; a screen is FOUR file types and until this existed the fourth had
// to be hand-written JSON, so "build an app from nothing" was false by exactly one file.
//
// MENUS ARE THE DESIGNER'S TO AUTHOR, the same reason layouts are. Not "a menu is not a
// user-editable format" — it is one — but that authoring it belongs in the design tool rather
// than in a text editor beside it. That is also why this stays a tool's while the theme
// editor became a library widget kind: the test is whether EVERY APP NEEDS IT, and the
// argument is written once, at `layout_editor.hpp`.
//
// THE SAME SHAPE AS ITS THREE SIBLINGS, deliberately and to the letter: a MODEL with no
// terminal in it, a rolltui::Menu over the SELECTED item, one UndoStack<T>, an Outcome the
// host acts on, and every operation a pure edit of the tree. It is the layout editor's
// sibling more than the other two's — both edit a TREE — so where a choice was already made
// there, it is made the same way here rather than re-derived.
//
// SELECTION IS AN INDEX PATH, NOT AN ID, and that is the one place this file departs from
// layout_editor.hpp. A layout node's id is unique across its tree, so that editor selects by
// id. A MENU id is not: `rolltui_menu_parse_json` gives a Choice's options their OWN id
// namespace (its `option_ids` set), so `{"id":"depth","kind":"choice","items":[{"id":"16"}]}`
// and a sibling `{"id":"16"}` elsewhere are both legal and both "16". An index path is unique
// by construction, survives a rename, and is what the reorder operations move.
// Rejected: selecting by id with a disambiguating parent prefix — a second identity for
// something the tree already identifies positionally, and it would break on every rename.
//
// OPERATIONS on the selected item (the menu's top level; ids the host never binds — the
// editor applies them itself and reports Committed):
//   kind                 choice: action | submenu | toggle | choice | input
//   id                   input, Name — the action id the host binds, or an option's value id
//   label                input, Text, optional — falls back to the id, as the file format does
//   action               input, Name, optional — the BINDINGS action this item stands for
//   checked              toggle (Toggle only)
//   dropdown             toggle (Choice only): the options open as a box over the menu, not as a level
//   value                input, Text, optional — a Choice's current option id, an Input's text
//   enabled              toggle
//   input type           choice: text | int | float | color | size | dim | name (Input only)
//   min / max / step     input, Float (Input only, and only for Int/Float — the range)
//   hint                 input, Text, optional (Input only)
//   optional             toggle (Input only)
//   add child            input (id) — NESTING. On a Choice this adds an OPTION, because a
//                        choice's options ARE its children; one operation, not two, and the
//                        parent's kind is what says which it is
//   add sibling          input (id)
//   move up / move down  reorder within the parent
//   remove               removes the item and everything under it; the root cannot be removed
//   new menu / load menu / save menu as / reset to loaded / undo / redo
//
// CREATING A MENU IS NOT INHERITING ONE, the rule layout_editor.hpp states for layouts and
// this file follows for the same measured reason: "New menu" replaces the whole tree with a
// STATED MINIMAL SKELETON — a root submenu carrying the typed name and ONE action item —
// never the open menu with its items stripped out. A menu carries action names, and an
// inherited tree would hand the author another app's `studio.reload` under a new file name.
//
// WHAT THIS EDITOR DOES NOT KNOW, and does not pretend to: an item's `action` is a name in
// the APP's
// bindings table, and this tool cannot verify it — the action belongs to the app. It is
// typed, written, and the app reports at start-up what nothing reaches. A designer names
// what the screen needs; the code catches up.
//
#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_menu_tree.h" /* INTERNAL: this editor opts in — it walks and MUTATES a tree */
#include "tool_str.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tool_actions.hpp"
#include "undo_stack.hpp"

namespace rolltui::tools {

using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;

// An index path from the root: {} is the root, {0} its first child, {0,2} that child's third.
using MenuPath = std::vector<std::size_t>;

class MenuEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, LoadMenu, ResetLoaded, Closed };
    Kind kind = Kind::None;
    std::string value;  // SaveAs: the file name; LoadMenu: the name
    bool operator==(const Outcome&) const = default;
  };

  // BORROWED, as the other editors take it: the studio owns the session and outlives every
  // editor. This one needs it only for `handle(e)`'s convenience lookup of Ctrl-Z in
  // `editor_bindings`, which is the same reason LayoutEditor and ThemeEditor keep one.
  explicit MenuEditor(RolltuiContext* ctx);
  ~MenuEditor() { rolltui_menu_free(menu_); }
  MenuEditor(const MenuEditor&) = delete;
  MenuEditor& operator=(const MenuEditor&) = delete;

  void load(const MenuItem& root);                  // the baseline; undo restarts; selection: the root
  void set_menus(std::vector<std::string> names);   // the Load choice's options
  void set_actions(std::vector<std::string> names); // the action field's HINT — never a bound list

  const MenuItem& current() const { return current_; }
  const MenuItem& committed() const { return undo_.current(); }
  const MenuPath& selected() const { return sel_; }
  void select(MenuPath p);
  void select_next(bool backwards = false);
  const MenuItem* selected_item() const;
  // The minimal skeleton "New menu" starts from, exposed so a test can assert what it IS
  // rather than what it renders as — layout_editor.hpp's `skeleton()`, same reason.
  MenuItem skeleton(std::string name) const;

  RolltuiMenu* menu() { return menu_; }
  const RolltuiMenu* menu() const { return menu_; }
  Outcome handle(const RolltuiEvent* e, const RolltuiBindings* nav);
  Outcome handle(const RolltuiEvent* e) { return handle(e, editor_bindings(ctx_)); }
  bool undo();
  bool redo();
  std::size_t undo_depth() const { return undo_.undo_depth(); }
  std::size_t redo_depth() const { return undo_.redo_depth(); }
  void replace(MenuItem root);   // a whole new committed tree (a reset, a load)

  // The file's TEXT, through the library's own writer — so what this editor saves and what
  // the library loads are the same bytes by construction, never two spellings of a format.
  std::string to_json() const;

  // REFILLED into a string the caller keeps: the studio draws this every frame an editor is
  // open. The returning form is one copy over it, for a test that reads it.
  void status_line(std::string& out) const;
  std::string status_line() const;
  // What the selected item IS, in words: "selected: depth  choice  3 options". The editor
  // composes it because it is the one place that reads an item's shape.
  std::string selection_line() const;

  // Tree helpers, exposed for the tests and the host.
  static MenuItem* at_path(MenuItem& root, const MenuPath& p);
  static const MenuItem* at_path(const MenuItem& root, const MenuPath& p);
  static std::vector<MenuPath> paths_in_order(const MenuItem& root);  // every item, tree order

 private:
  RolltuiContext* ctx_;   // BORROWED: only for `handle(e)`'s bindings lookup
  void rebuild_menu();
  void sync_values();
  void sync_fields();     // which fields this item's kind offers, and their values
  MenuItem* sel_item();
  Outcome commit_current();
  bool add_item(bool as_child, const std::string& id);
  bool move_item(int delta);
  bool remove_item();
  std::string unique_id(const MenuItem& parent, const std::string& base) const;

  RolltuiMenu* menu_ = rolltui_menu_new();
  MenuItem current_;
  UndoStack<MenuItem> undo_;
  MenuPath sel_;
  std::vector<std::string> menus_;
  std::vector<std::string> actions_;
  std::string status_;
};

}  // namespace rolltui::tools
