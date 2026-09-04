#pragma once
//
// rolltui/Input.hpp — the input widget (plan/phase-9.md, milestone 10): a multi-line
// text field with a caret, a selection, history, mouse and paste, as a PURE state
// machine over decoded events. roll's single-line, byte-at-a-time `LineEditor` grew
// into this; like it, the widget knows nothing of a file descriptor — the host routes
// events in, reads the text out, and draws the frame it asks for.
//
// MODEL. The text is UTF-8 and never holds a CR or a control character other than
// '\n' and '\t' (insert() drops them: a stray control byte in a prompt is invisible
// on screen and corrupts the request). Every position — the caret, a selection end —
// is a byte offset that always lies on an extended grapheme cluster boundary (UAX
// #29), snapped forward after every mutation: a Backspace never strands the lead byte
// of a two-byte character, never splits an emoji ZWJ sequence, and a Left/Right step
// is one drawn glyph. A selection is {anchor, head}, active or not; its range is
// [min, max) of the two boundaries. History is a list of submitted texts the widget
// NEVER modifies; browsing keeps the text being typed as a draft that comes back
// after the newest entry.
//
// LAYOUT — cell wrap, not word wrap. The graphemes of each logical line ('\n'-
// separated) fill rows of the width left after the prompt (first row) or after a
// hanging indent of the prompt's width (every later row), breaking at ANY grapheme —
// the way readline and every shell wrap a command line. Deliberate: it makes cell ↔
// position a bijection (the caret after a trailing space, the caret at the end of a
// full row — which is the start of the next —, a 2-cell glyph that does not fit the
// last column), which is what a caret and a hit test need; the transcript's UAX #14
// wrap drops trailing spaces at a soft break and would leave the caret nowhere. A tab
// expands to the next multiple of `tab_width` from the text's first column. A glyph
// wider than the room left on a row starts the next row. Rows beyond the area scroll
// so the caret's row stays visible; `rows_for(width)` tells a host how tall the
// window would have to be to show everything, so it can grow the window instead.
//
// KEYS are DATA (milestone 17): handle() takes a `const Bindings&` (Bindings.hpp) and
// asks it which input.* ACTION a key is; the shipped default table (rolltui/presets/
// bindings/default.json) is exactly the list below, and handle(e, now) without a table
// uses it. Every key not bound is returned as Ignored, so the host can offer it to
// another window. A printable character without Ctrl or Alt is TEXT and is never looked
// up. The default, action by action:
//   input.submit             Enter — the text is the host's to take. Enter is always
//                            submit (plan/phase-9.md); no file may bind it elsewhere
//   input.newline            Alt+Enter
//   input.backspace / delete Backspace / Delete: the selection if any, else a grapheme
//   input.kill_word_backward Ctrl+W, Alt+Backspace;  kill_word_forward  Alt+D, Ctrl+Delete.
//                            A "word" motion skips spaces, then one UAX #29 word
//   input.kill_to_line_start / _end   Ctrl+U / Ctrl+K
//   input.left / right       one grapheme;  word_left / word_right  Ctrl+ or Alt+arrow;
//   input.select_*           the Shift+ forms extend the selection instead
//   input.line_start / _end  Home / End: on an EMPTY buffer there is no caret to move
//                            and both are Ignored (roll scrolls the transcript with them)
//   input.up / down          one row, keeping a goal column, when the caret is not on
//                            the first / last row; there, the previous / next history
//                            entry.  select_up / _down (Shift) extend by a row, never browse
//   input.select_all         Ctrl+A
//   input.clear_selection    Escape (Ignored when there is none)
//   input.copy               Alt+C through on_copy (Ignored when none)
//   input.eof                Ctrl+D: Eof on an empty buffer, else delete forward (readline)
//   bracketed paste          inserted literally after the same sanitising as typed
//                            text: CR LF and CR become LF, other controls are dropped
//   mouse (button 1)         a press places the caret (Shift+press extends the
//                            selection); a drag selects, including the grapheme under
//                            the pointer at BOTH ends — the transcript's convention, so
//                            a drag from the h to the o of "hello" selects "hello" in
//                            either direction; double-click a word, triple-click a
//                            logical line, and a drag after either grows by whole
//                            units; a release where the pointer already is changes
//                            nothing; release fires on_copy (copy-on-select); a click
//                            that did not drag selects nothing. The wheel and the
//                            other buttons are Ignored
// Ctrl+C is not here: what it means (clear the line, cancel a turn, exit) is the
// host's. So is Ctrl+L. Tab is the WindowStack's (stack.focus_next).
//   input.undo / input.redo   Ctrl+Z / Ctrl+Y — see UNDO below.
//
// UNDO (Phase 12 m1). One `UndoStack<InputSnapshot>` (Undo.hpp) of whole {text, caret,
// selection} snapshots — cheap, and there is no inverse edit to get wrong, only a value
// to put back. The GROUPING RULE, stated once so it can be asserted rather than tuned
// by feel:
//   - ORDINARY EDITING — insert() with no active selection (typing, Alt+Enter's
//     newline) and erase_backward()/erase_forward() with no active selection
//     (Backspace/Delete of one grapheme) — MERGES into whatever ordinary-editing group
//     is already open: neither is in the closing list below, so a typo fixed with a
//     Backspace two keys later is still one undo, not three.
//   - FOUR THINGS CLOSE THE GROUP, and are never themselves merged with a neighbour or
//     with each other: a KILL (kill_word_backward/forward, kill_to_line_start/end), a
//     PASTE (bracketed paste), a SELECTION-REPLACE (any edit that first consumes an
//     active selection — typing over a selection, Backspace/Delete/kill on one), and a
//     CARET/SELECTION MOVE with no text change (set_caret, the move_*/select_* family,
//     select_all, a mouse press/drag) — the move itself is never a separate undo step,
//     only a boundary: nothing recorded if it changed nothing. Where the move DID
//     change something (a Shift+move's selection, or a second move right after a group
//     already closed) and the group it closed had nothing open, that state is folded
//     into the EXISTING checkpoint (UndoStack::replace_current, no new step) rather
//     than lost — so undoing the edit that follows a bare move restores the caret and
//     selection exactly as the move left them, not a stale position from before it.
//   - TIMEOUT: two edits that would otherwise merge close the group anyway once more
//     than the internal coalescing window has passed between them, judged from the
//     `now_ms` given to handle() — a caller that never passes a clock never times out,
//     so grouping then rests on kind and boundaries alone.
// undo()/redo() restore the text, the caret and the selection byte-for-byte; undo()
// below the bottom of the stack is a no-op (the value already there is kept, never
// emptied). set_text() — and so clear() and history_prev()/history_next(), both built
// on it — resets the WHOLE undo stack to the new text as a fresh baseline (Undo.hpp's
// own "a load, a reset: nothing to undo or redo"): the history mechanism (a different
// one, milestone 10) and the undo mechanism never read or write each other's state.
//
// The goal column and the scroll row are memos of the caret's recent history, never
// inputs to what the text is; everything drawn is a function of (text, caret,
// selection, options, area, top row).
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/c/rolltui_input.h"

namespace rolltui {

// PHASE 15 m5: the whole state machine is behind `rolltui/c/rolltui_input.h`, in one of two
// implementations chosen by `-DROLLTUI_C` (`InputCpp.cpp` or `c/rolltui_input.c`). This
// header is the C++ shape every host already writes against; `InputOptions` and
// `InputSelection` ARE the C structs (one definition), and the class below is RAII plus the
// two translations a C boundary cannot do for itself — a `std::function` clipboard into a
// function pointer, and the thirty action NAMES the widget's table is keyed by.
//
// TWO THINGS A CALLER CAN SEE, both forced by the handle rather than chosen:
//   - `text()` returns a `std::string_view`, not a `const std::string&`: the bytes live in
//     the C's buffer and the window is stated (until the text next changes).
//   - `history()` is `history_count()` + `history_at(i)`, for the same reason `Frame`'s
//     marks are — nothing on the C side can hand back a `std::vector<std::string>` without
//     building one per call.
using InputOptions = RolltuiInputOptions;
using InputSelection = RolltuiInputSelection;

enum class InputAction : unsigned char {
  Ignored = ROLLTUI_INPUT_IGNORED,
  Handled = ROLLTUI_INPUT_HANDLED,
  Submit = ROLLTUI_INPUT_SUBMIT,
  Eof = ROLLTUI_INPUT_EOF,
};

class Input {
 public:
  // OWNED, through a `unique_ptr` with a deleter that calls the C free — the same shape
  // `Frame`, `Bindings`, `KeyDecoder` and `WindowStack` use.
  struct Handle {
    void operator()(RolltuiInput* p) const { rolltui_input_free(p); }
  };
  Input();
  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;
  // MOVABLE, and the two are written out rather than defaulted for one reason: the C holds
  // a `void*` back to THIS object for the clipboard trampoline, so a move has to re-point
  // it. A defaulted move would leave the moved-from address in the handle and copy through
  // a dead object — the kind of thing an opaque handle makes possible and a `std::function`
  // member hid.
  Input(Input&& o) noexcept;
  Input& operator=(Input&& o) noexcept;

  std::function<void(const std::string&)> on_copy;  // the host's clipboard

  // ---- content ----
  // A BORROW of the C's buffer, valid until the text next changes.
  std::string_view text() const;
  void set_text(std::string_view t);  // sanitised; caret at the end; selection cleared
  void clear();                       // text, caret, selection and the history cursor (entries kept)
  std::size_t caret() const { return rolltui_input_caret(in_.get()); }
  // Moves the caret to the boundary at or after `byte`; `extend` grows the selection
  // from the old caret (or the existing anchor) instead of clearing it.
  void set_caret(std::size_t byte, bool extend = false) { rolltui_input_set_caret(in_.get(), byte, extend); }
  InputSelection selection() const;
  std::string selected_text() const;
  void select_all() { rolltui_input_select_all(in_.get()); }
  void clear_selection() { rolltui_input_clear_selection(in_.get()); }

  // ---- editing primitives (what the keys call; a host may call them too) ----
  void insert(std::string_view utf8) { rolltui_input_insert(in_.get(), utf8.data(), utf8.size()); }
  // What `insert` WOULD produce, into a buffer the caller owns and reuses — for a caller
  // that has to judge a keystroke before letting it land (the menu's typed fields). It
  // replaced `Input probe = edit_;`, which copied the whole editor per keystroke.
  void preview_insert(std::string_view utf8, Str& out) const {
    rolltui_input_preview_insert(in_.get(), utf8.data(), utf8.size(), &out);
  }
  bool erase_selection() { return rolltui_input_erase_selection(in_.get()) != 0; }
  void erase_backward() { rolltui_input_erase_backward(in_.get()); }
  void erase_forward() { rolltui_input_erase_forward(in_.get()); }
  void kill_word_backward() { rolltui_input_kill_word_backward(in_.get()); }
  void kill_word_forward() { rolltui_input_kill_word_forward(in_.get()); }
  void kill_to_line_start() { rolltui_input_kill_to_line_start(in_.get()); }
  void kill_to_line_end() { rolltui_input_kill_to_line_end(in_.get()); }
  void move_left(bool extend = false) { rolltui_input_move_left(in_.get(), extend); }
  void move_right(bool extend = false) { rolltui_input_move_right(in_.get(), extend); }
  void move_word_left(bool extend = false) { rolltui_input_move_word_left(in_.get(), extend); }
  void move_word_right(bool extend = false) { rolltui_input_move_word_right(in_.get(), extend); }
  void move_line_start(bool extend = false) { rolltui_input_move_line_start(in_.get(), extend); }
  void move_line_end(bool extend = false) { rolltui_input_move_line_end(in_.get(), extend); }
  bool move_up(bool extend = false) { return rolltui_input_move_up(in_.get(), extend) != 0; }
  bool move_down(bool extend = false) { return rolltui_input_move_down(in_.get(), extend) != 0; }
  // Word boundaries as the motions see them (exposed for the tests).
  std::size_t word_left_of(std::size_t pos) const { return rolltui_input_word_left_of(in_.get(), pos); }
  std::size_t word_right_of(std::size_t pos) const { return rolltui_input_word_right_of(in_.get(), pos); }

  // ---- undo/redo (see UNDO above) ----
  bool undo() { return rolltui_input_undo(in_.get()) != 0; }  // false (no-op) at the bottom
  bool redo() { return rolltui_input_redo(in_.get()) != 0; }
  bool can_undo() const { return rolltui_input_can_undo(in_.get()) != 0; }
  bool can_redo() const { return rolltui_input_can_redo(in_.get()) != 0; }

  // ---- history ----
  void push_history(std::string_view entry);  // skips an empty entry and a repeat of the newest
  std::size_t history_count() const { return rolltui_input_history_count(in_.get()); }
  std::string_view history_at(std::size_t i) const;
  std::size_t history_cursor() const { return rolltui_input_history_cursor(in_.get()); }  // count = the draft
  bool history_prev() { return rolltui_input_history_prev(in_.get()) != 0; }
  bool history_next() { return rolltui_input_history_next(in_.get()) != 0; }

  // ---- events (already routed to this window by the host) ----
  InputAction handle(const Event& e, const Bindings& bindings, std::uint64_t now_ms = 0);
  InputAction handle(const Event& e, std::uint64_t now_ms = 0) { return handle(e, default_bindings(), now_ms); }

  // ---- layout + drawing ----
  void set_options(const InputOptions& o) { rolltui_input_set_options(in_.get(), &o); }
  const InputOptions& options() const { return *rolltui_input_options(in_.get()); }
  // Rows the whole text needs at this window width (≥ 1), for a host that grows the
  // window with the text.
  int rows_for(int width) const { return rolltui_input_rows_for(in_.get(), width); }
  void layout(Rect area) { rolltui_input_layout(in_.get(), area); }  // keeps the caret's row in view
  void draw(Frame& f, const Theme& theme, bool focused) const;
  struct CellPos {
    int row = 0, col = 0;
  };
  // Where a position is drawn: a text row (before scrolling) and a column from the
  // area's left edge (the prompt / indent included).
  CellPos cell_of(std::size_t offset) const;
  // The grapheme under a screen cell as [begin, end) (begin == end at a row's end, at
  // the end of the text, or on a newline); rows are clamped to the text, so a pointer
  // dragged off the area still resolves. nullopt only before any layout().
  struct Hit {
    std::size_t begin = 0, end = 0;
  };
  std::optional<Hit> hit(int x, int y) const;
  int rows() const { return rolltui_input_rows(in_.get()); }
  int top_row() const { return rolltui_input_top_row(in_.get()); }
  Rect area() const;

 private:
  std::unique_ptr<RolltuiInput, Handle> in_{rolltui_input_new()};
};

}  // namespace rolltui
