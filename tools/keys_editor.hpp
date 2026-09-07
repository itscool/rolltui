#pragma once
//
// rolltui/tools/keys_editor.hpp — the keys editor : the
// third editor, the same shape as the other two — a MODEL over a rolltui menu with the
// shared UndoStack, no terminal in it. The tree is scope › action › {add a chord, one
// "remove <chord>" per chord, clear}; "add a chord" puts the editor in CAPTURE: the next
// key pressed becomes the chord (Escape cancels), a chord already bound to another
// action of the same scope MOVES and the status says from where, and a chord that
// breaks the Enter rule is refused by name. Every change is a commit on the undo stack
// (there is no half-typed state to preview). Load / Save as / Write shipped / Reset are
// the host's, as in the theme editor.
//
// calls `rolltui/c/*.h` directly — no `rolltui/*.hpp`.
//
#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_bindings.h"
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

// The C tree types, under the names every editor already writes them as (Menu.hpp's own
// aliases, before it went away).
using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;

// The keys editor's OWN value: an owned, clone-on-copy `RolltuiBindings*`, so
// UndoStack<KeyTable> (undo_stack.hpp) can snapshot the table being edited exactly as
// ThemeEditor's ThemeEdit snapshots a pair of themes. RolltuiBindings is opaque (Phase 15
// m3), so — unlike Layout, which IS its C struct — a plain member cannot hold one by
// value; this is the RAII a C++ consumer writes for itself (rolltui/rolltui.h), with NO
// method beyond lifetime and equality. Every real edit (bind/unbind/clear/…) is a direct
// rolltui_bindings_* call on .get() from KeysEditor's own methods, which is what keeps
// this from becoming a second Bindings.
class KeyTable {
 public:
  KeyTable() : b_(rolltui_bindings_new()) {}
  explicit KeyTable(RolltuiBindings* owned) : b_(owned) {}
  KeyTable(const KeyTable& o) : b_(rolltui_bindings_clone(o.b_)) {}
  KeyTable& operator=(const KeyTable& o) {
    if (this != &o) {
      rolltui_bindings_free(b_);
      b_ = rolltui_bindings_clone(o.b_);
    }
    return *this;
  }
  KeyTable(KeyTable&& o) noexcept : b_(o.b_) { o.b_ = nullptr; }
  KeyTable& operator=(KeyTable&& o) noexcept {
    if (this != &o) {
      rolltui_bindings_free(b_);
      b_ = o.b_;
      o.b_ = nullptr;
    }
    return *this;
  }
  ~KeyTable() { rolltui_bindings_free(b_); }
  RolltuiBindings* get() { return b_; }
  const RolltuiBindings* get() const { return b_; }
  bool operator==(const KeyTable& o) const { return rolltui_bindings_equal(b_, o.b_) != 0; }

 private:
  RolltuiBindings* b_ = nullptr;
};

class KeysEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, WriteShipped, LoadPreset, ResetLoaded, Closed };
    Kind kind = Kind::None;
    std::string value;
    bool operator==(const Outcome&) const = default;
  };

  // The session is needed only to CLONE the shipped table here; nothing after construction
  // reads it, so it is not kept. (Both other editors do keep one: their `handle(e)`
  // convenience looks a chord up in `editor_bindings`, and this one has no such overload.)
  explicit KeysEditor(RolltuiContext* ctx);
  ~KeysEditor() { rolltui_menu_free(menu_); }
  KeysEditor(const KeysEditor&) = delete;
  KeysEditor& operator=(const KeysEditor&) = delete;

  void load(const RolltuiBindings* b);                  // the baseline; undo restarts
  void set_presets(std::vector<std::string> names);
  void set_shipped(std::vector<std::string> names, bool may_write);

  const RolltuiBindings* current() const { return current_.get(); }
  const RolltuiBindings* committed() const { return undo_.current().get(); }
  bool capturing() const { return capture_.has_value(); }
  const std::string& capturing_action() const;

  RolltuiMenu* menu() { return menu_; }
  const RolltuiMenu* menu() const { return menu_; }
  // The editor's own menu is driven by `nav` — the bindings the HOST runs on (so a
  // rebinding of menu.down mid-edit does not strand the editor); the table being edited
  // is current().
  Outcome handle(const RolltuiEvent* e, const RolltuiBindings* nav);
  bool undo();
  bool redo();
  std::size_t undo_depth() const { return undo_.undo_depth(); }
  std::size_t redo_depth() const { return undo_.redo_depth(); }
  void replace(RolltuiBindings* b);  // ADOPTS: takes ownership, as KeyTable's owning ctor does

  // REFILLED into a string the caller keeps: the studio draws this every frame an editor is
  // open. The returning form is one copy over it, for a test that reads it.
  void status_line(std::string& out) const;
  std::string status_line() const;

 private:
  void rebuild_menu();
  void rebuild_action(std::string_view action);
  Outcome commit();

  RolltuiMenu* menu_ = rolltui_menu_new();
  KeyTable current_;
  UndoStack<KeyTable> undo_;
  std::optional<std::string> capture_;  // the action awaiting its chord
  std::vector<std::string> presets_, shipped_;
  bool may_write_shipped_ = false;
  std::string status_;
};

}  // namespace rolltui::tools
