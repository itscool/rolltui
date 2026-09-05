#pragma once
//
// rolltui/tools/keys_editor.hpp — the keys editor (plan/phase-9.md, milestone 17): the
// third editor, the same shape as the other two — a MODEL over a rolltui::Menu with the
// shared UndoStack, no terminal in it. The tree is scope › action › {add a chord, one
// "remove <chord>" per chord, clear}; "add a chord" puts the editor in CAPTURE: the next
// key pressed becomes the chord (Escape cancels), a chord already bound to another
// action of the same scope MOVES and the status says from where, and a chord that
// breaks the Enter rule is refused by name. Every change is a commit on the undo stack
// (there is no half-typed state to preview). Load / Save as / Write shipped / Reset are
// the host's, as in the theme editor.
//
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Menu.hpp"
#include "tool_actions.hpp"
#include "undo_stack.hpp"

namespace rolltui::tools {

class KeysEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, WriteShipped, LoadPreset, ResetLoaded, Closed };
    Kind kind = Kind::None;
    std::string value;
    bool operator==(const Outcome&) const = default;
  };

  KeysEditor();
  void load(const Bindings& b);                        // the baseline; undo restarts
  void set_presets(std::vector<std::string> names);
  void set_shipped(std::vector<std::string> names, bool may_write);

  const Bindings& current() const { return current_; }
  const Bindings& committed() const { return undo_.current(); }
  bool capturing() const { return capture_.has_value(); }
  const std::string& capturing_action() const;

  Menu& menu() { return menu_; }
  const Menu& menu() const { return menu_; }
  // The editor's own menu is driven by `nav` — the bindings the HOST runs on (so a
  // rebinding of menu.down mid-edit does not strand the editor); the table being edited
  // is current().
  Outcome handle(const Event& e, const Bindings& nav);
  bool undo();
  bool redo();
  std::size_t undo_depth() const { return undo_.undo_depth(); }
  std::size_t redo_depth() const { return undo_.redo_depth(); }
  void replace(Bindings b);

  std::string status_line() const;

 private:
  void rebuild_menu();
  void rebuild_action(std::string_view action);
  Outcome commit();

  Menu menu_;
  Bindings current_;
  UndoStack<Bindings> undo_;
  std::optional<std::string> capture_;  // the action awaiting its chord
  std::vector<std::string> presets_, shipped_;
  bool may_write_shipped_ = false;
  std::string status_;
};

}  // namespace rolltui::tools
