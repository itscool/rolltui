#pragma once
//
// rolltui/tools/undo_stack.hpp — ONE UNDO STACK TYPE FOR EVERY EDITOR (theme, layout,
// keys): the C++ adapter over rolltui/c/rolltui_undo.h. `rolltui/Undo.hpp` made this same
// promise as a `std::vector<T>` of its own — the last pure-C++ template in the library
// with no C form — and is deleted; this is what the three editors that depended on it
// were ported to. The public shape is UNCHANGED on purpose, so nothing outside this file
// had to change beyond the #include line:
//
//   UndoStack<ThemeEdit> u(theme);       // the baseline
//   u.commit(edited);                   // a committed change: the previous value becomes undoable
//   if (u.undo()) theme = u.current();  // the value before the last commit
//   if (u.redo()) theme = u.current();  // forward again; a new commit drops the redo branch
//
// WHY THIS IS A tools/ FILE AND NOT rolltui/Undo.hpp REBORN: `rolltui/rolltui.h` says a
// C++ consumer that wants RAII writes its own wrapper rather than the library shipping
// one, and names "roll, the studio, paint and the editors" as exactly that kind of
// consumer. theme_editor.hpp, keys_editor.hpp and layout_editor.hpp already share
// tool_actions.hpp for the same reason — one host-side file for three files that are one
// product — rather than each rewriting it, which is the two-consumers-wrote-the-same-
// wrapper case that same header calls evidence the API is wrong. A shared undo stack is
// not that: three EDITORS sharing ONE adapter they all equally need is the tool_actions.
// hpp case, not a library header none of rolltui's own C code ever reaches for.
//
// WHY ONE CALLBACK (free) AND NOT TWO (clone + free): see rolltui_undo.h's header
// comment for the full reasoning. Short version: commit()/reset()/replace_current()
// below always hand the C stack a snapshot THIS HEADER just heap-allocated from the
// caller's value (one move, the same one `std::vector<T>::push_back` already cost), so
// the stack never needs to duplicate one of its own — only ever to free one when
// history moves past it.
//
#include "rolltui/rolltui.h"
#include <cstddef>
#include <memory>


namespace rolltui::tools {

namespace detail {
// Generated ONCE per T, generically, from T's own destructor — the same captureless-
// lambda idiom `rolltui/PresetStore.hpp` uses for its `clone`/`destroy` pair. A plain
// function pointer, so a stack's descriptor costs no closure (RolltuiUndoFreeFn takes no
// context: freeing a value needs nothing but the value).
template <class T>
RolltuiUndoFreeFn undo_free_fn() {
  return [](void* v) { const std::unique_ptr<T> owned(static_cast<T*>(v)); };
}
}  // namespace detail

template <class T>
class UndoStack {
 public:
  // OWNED, through a `unique_ptr` with a deleter that calls the C free — the same shape
  // `rolltui::Bindings` and `rolltui::Layout`'s own handles use.
  struct Handle {
    void operator()(RolltuiUndoStack* p) const { rolltui_undo_free(p); }
  };

  UndoStack() : u_(rolltui_undo_new(200, detail::undo_free_fn<T>())) {}
  explicit UndoStack(T baseline, std::size_t limit = 200) : u_(rolltui_undo_new(limit, detail::undo_free_fn<T>())) {
    rolltui_undo_reset(u_.get(), std::make_unique<T>(std::move(baseline)).release());
  }

  // Restarts from a new baseline (a load, a reset): nothing to undo or redo.
  void reset(T baseline) { rolltui_undo_reset(u_.get(), std::make_unique<T>(std::move(baseline)).release()); }
  // Records a new current value; the redo branch (if any) is dropped.
  void commit(T value) { rolltui_undo_commit(u_.get(), std::make_unique<T>(std::move(value)).release()); }
  // Overwrites the CURRENT value in place: no new step, the redo branch untouched. For
  // state that rides along with the current position but must never become its own
  // undo step (see rolltui_undo.h's replace_current comment for the worked example).
  void replace_current(T value) {
    rolltui_undo_replace_current(u_.get(), std::make_unique<T>(std::move(value)).release());
  }
  bool undo() { return rolltui_undo_undo(u_.get()) != 0; }
  bool redo() { return rolltui_undo_redo(u_.get()) != 0; }
  bool can_undo() const { return rolltui_undo_can_undo(u_.get()) != 0; }
  bool can_redo() const { return rolltui_undo_can_redo(u_.get()) != 0; }
  std::size_t undo_depth() const { return rolltui_undo_undo_depth(u_.get()); }
  std::size_t redo_depth() const { return rolltui_undo_redo_depth(u_.get()); }
  const T& current() const { return *static_cast<const T*>(rolltui_undo_current(u_.get())); }
  bool empty() const { return rolltui_undo_empty(u_.get()) != 0; }

 private:
  std::unique_ptr<RolltuiUndoStack, Handle> u_;
};

}  // namespace rolltui::tools
