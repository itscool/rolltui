#pragma once
//
// rolltui/tools/keys_editor.hpp — THE C++ SHAPE OVER THE LIBRARY'S KEYS EDITOR.
//
// The editor itself is `rolltui/c/rolltui_keys_editor.h`, and everything it does and refuses to
// do is stated there. This file is what a C++ host writes against: one handle and the same
// method names the studio already calls.
//
// IT IS A SHAPE, NEVER A SECOND IMPLEMENTATION. Every method below forwards; nothing here
// decides anything. The editor moved into the library because a widget kind is a model plus a
// draw and a handle, and an app that names `keys` in its layout gets all three with no code —
// which is exactly what one program owning this model made impossible. Every app has a binding
// table, the library ships the default one, the Bindings preset domain and its store already
// exist beside the Theme one, and "which key does this" is as much a user's preference as "what
// colour is a warning".
//
#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_keys_editor.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "tool_str.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "tool_actions.hpp"

namespace rolltui::tools {

// The C tree types, under the names every editor already writes them as.
using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;

class KeysEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, WriteShipped, LoadPreset, ResetLoaded, Closed };
    Kind kind = Kind::None;
    std::string value;  // SaveAs: the name; WriteShipped / LoadPreset: the preset chosen
    bool operator==(const Outcome&) const = default;
  };

  // `ctx` supplies the table this editor starts on — the session's default. The model needs no
  // session; the parameter stays because a host holds one and the studio's other editors take
  // it.
  explicit KeysEditor(RolltuiContext* ctx) : e_(rolltui_keys_editor_new(rolltui_bindings_default(ctx))) {}
  ~KeysEditor() { rolltui_keys_editor_free(e_); }
  KeysEditor(const KeysEditor&) = delete;
  KeysEditor& operator=(const KeysEditor&) = delete;

  void load(const RolltuiBindings* b) { rolltui_keys_editor_load(e_, b); }
  void set_presets(const std::vector<std::string>& names);
  void set_shipped(const std::vector<std::string>& names, bool may_write);

  // BORROWS of the model's own storage, valid until the next edit.
  const RolltuiBindings* current() const { return rolltui_keys_editor_current(e_); }
  const RolltuiBindings* committed() const { return rolltui_keys_editor_committed(e_); }
  bool capturing() const { return rolltui_keys_editor_capturing(e_) != 0; }
  std::string_view capturing_action() const;

  RolltuiMenu* menu() { return rolltui_keys_editor_menu(e_); }
  const RolltuiMenu* menu() const { return rolltui_keys_editor_menu(const_cast<RolltuiKeysEditor*>(e_)); }

  // Events already routed to the editor's window. `nav` is the table the HOST is running on, so
  // that rebinding a menu key mid-edit does not strand the editor; the table being edited is
  // `current()`.
  Outcome handle(const RolltuiEvent* e, const RolltuiBindings* nav);
  bool undo() { return rolltui_keys_editor_undo(e_) != 0; }
  bool redo() { return rolltui_keys_editor_redo(e_) != 0; }
  std::size_t undo_depth() const { return rolltui_keys_editor_undo_depth(e_); }
  std::size_t redo_depth() const { return rolltui_keys_editor_redo_depth(e_); }
  void replace(RolltuiBindings* b) { rolltui_keys_editor_replace(e_, b); }  // ADOPTS

  // REFILLED into a string the caller keeps: the studio draws this every frame an editor is
  // open. The returning form is one copy over it, for a test that reads it.
  void status_line(std::string& out) const;
  std::string status_line() const;

 private:
  RolltuiKeysEditor* e_;
};

}  // namespace rolltui::tools
