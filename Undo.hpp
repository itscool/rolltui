#pragma once
//
// rolltui/Undo.hpp — one undo stack type for every editor (plan/phase-9.md, the
// theme and layout editors share it by rule: "one undo-stack type for both editors,
// not two"). A stack of WHOLE snapshots: cheap for a 44-style theme or a split tree,
// and the reason undo works blind — there is no inverse operation to get wrong, only a
// value to put back.
//
//   UndoStack<Theme> u(theme);        // the baseline
//   u.commit(edited);                 // a committed change: the previous value becomes undoable
//   if (u.undo()) theme = u.current();   // the value before the last commit
//   if (u.redo()) theme = u.current();   // forward again; a new commit drops the redo branch
//
#include <cstddef>
#include <vector>

namespace rolltui {

template <class T>
class UndoStack {
 public:
  UndoStack() = default;
  explicit UndoStack(T baseline, std::size_t limit = 200) : limit_(limit) { history_.push_back(std::move(baseline)); }

  // Restarts from a new baseline (a load, a reset): nothing to undo or redo.
  void reset(T baseline) {
    history_.clear();
    history_.push_back(std::move(baseline));
    at_ = 0;
  }
  // Records a new current value; the redo branch (if any) is dropped.
  void commit(T value) {
    if (history_.empty()) { history_.push_back(std::move(value)); at_ = 0; return; }
    history_.resize(at_ + 1);
    history_.push_back(std::move(value));
    if (history_.size() > limit_) history_.erase(history_.begin());
    at_ = history_.size() - 1;
  }
  // Overwrites the CURRENT value in place: no new step, the redo branch untouched. For
  // state that rides along with the current position but must never become its own
  // undo step (Input.hpp: a caret/selection move closes a group without being one
  // itself, yet the position it leaves behind has to be what the NEXT real edit's
  // "before" snapshot restores to — not a stale one from before the move).
  void replace_current(T value) {
    if (history_.empty()) { history_.push_back(std::move(value)); at_ = 0; return; }
    history_[at_] = std::move(value);
  }
  bool undo() { if (at_ == 0) return false; --at_; return true; }
  bool redo() { if (at_ + 1 >= history_.size()) return false; ++at_; return true; }
  bool can_undo() const { return at_ > 0; }
  bool can_redo() const { return at_ + 1 < history_.size(); }
  std::size_t undo_depth() const { return at_; }
  std::size_t redo_depth() const { return history_.empty() ? 0 : history_.size() - 1 - at_; }
  const T& current() const { return history_[at_]; }
  bool empty() const { return history_.empty(); }

 private:
  std::vector<T> history_;
  std::size_t at_ = 0;
  std::size_t limit_ = 200;
};

}  // namespace rolltui
