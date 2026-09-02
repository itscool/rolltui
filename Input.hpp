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
//
// The goal column and the scroll row are memos of the caret's recent history, never
// inputs to what the text is; everything drawn is a function of (text, caret,
// selection, options, area, top row).
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

struct InputOptions {
  bool ambiguous_wide = false;
  int tab_width = 4;
  int inset = 0;                   // columns kept clear on each side of the area
  std::string prompt = "> ";       // drawn before the first row, in prompt_role
  Role prompt_role = Role::prompt;
  std::string placeholder;         // drawn after the prompt while the text is empty
  std::uint64_t multi_click_ms = 400;
  std::size_t history_limit = 1000;
  bool operator==(const InputOptions&) const = default;
};

enum class InputAction { Ignored, Handled, Submit, Eof };

struct InputSelection {
  std::size_t anchor = 0, head = 0;
  bool active = false;
  std::size_t begin() const { return anchor < head ? anchor : head; }
  std::size_t end() const { return anchor < head ? head : anchor; }
  bool empty() const { return !active || anchor == head; }
  bool operator==(const InputSelection&) const = default;
};

class Input {
 public:
  std::function<void(const std::string&)> on_copy;  // the host's clipboard

  // ---- content ----
  const std::string& text() const { return text_; }
  void set_text(std::string t);   // sanitised; caret at the end; selection cleared
  void clear();                    // text, caret, selection and the history cursor (entries kept)
  std::size_t caret() const { return caret_; }
  // Moves the caret to the boundary at or after `byte`; `extend` grows the selection
  // from the old caret (or the existing anchor) instead of clearing it.
  void set_caret(std::size_t byte, bool extend = false);
  const InputSelection& selection() const { return sel_; }
  std::string selected_text() const;
  void select_all();
  void clear_selection() { sel_ = {}; }

  // ---- editing primitives (what the keys call; a host may call them too) ----
  void insert(std::string_view utf8);  // replaces the selection; sanitised
  bool erase_selection();               // false when there is none
  void erase_backward();
  void erase_forward();
  void kill_word_backward();
  void kill_word_forward();
  void kill_to_line_start();
  void kill_to_line_end();
  void move_left(bool extend = false);
  void move_right(bool extend = false);
  void move_word_left(bool extend = false);
  void move_word_right(bool extend = false);
  void move_line_start(bool extend = false);
  void move_line_end(bool extend = false);
  bool move_up(bool extend = false);    // false when the caret is on the first row
  bool move_down(bool extend = false);  // false when the caret is on the last row
  // Word boundaries as the motions see them (exposed for the tests).
  std::size_t word_left_of(std::size_t pos) const;
  std::size_t word_right_of(std::size_t pos) const;

  // ---- history ----
  void push_history(std::string entry);  // skips an empty entry and a repeat of the newest
  const std::vector<std::string>& history() const { return hist_; }
  std::size_t history_cursor() const { return hist_pos_; }  // history().size() = the draft
  bool history_prev();
  bool history_next();

  // ---- events (already routed to this window by the host) ----
  InputAction handle(const Event& e, const Bindings& bindings, std::uint64_t now_ms = 0);
  InputAction handle(const Event& e, std::uint64_t now_ms = 0) { return handle(e, default_bindings(), now_ms); }

  // ---- layout + drawing ----
  void set_options(const InputOptions& o);
  const InputOptions& options() const { return opt_; }
  // Rows the whole text needs at this window width (≥ 1), for a host that grows the
  // window with the text.
  int rows_for(int width) const;
  void layout(Rect area);  // wraps at the area's width, keeps the caret's row in view
  void draw(Frame& f, const Theme& theme, bool focused) const;
  struct CellPos { int row = 0, col = 0; };
  // Where a position is drawn: a text row (before scrolling) and a column from the
  // area's left edge (the prompt / indent included).
  CellPos cell_of(std::size_t offset) const;
  // The grapheme under a screen cell as [begin, end) (begin == end at a row's end, at
  // the end of the text, or on a newline); rows are clamped to the text, so a pointer
  // dragged off the area still resolves. nullopt only before any layout().
  struct Hit { std::size_t begin = 0, end = 0; };
  std::optional<Hit> hit(int x, int y) const;
  int rows() const;
  int top_row() const { return top_; }
  Rect area() const { return area_; }

 private:
  struct Cell { int row = 0, col = 0, width = 0; };
  struct Flow {
    std::vector<Cell> cells;             // one per grapheme
    std::vector<std::size_t> row_end;    // per row: the position at its end
    int end_row = 0, end_col = 0;        // where the caret sits at the end of the text
    int rows = 1;
  };
  Flow flow(int width) const;
  void ensure() const;
  void retext(std::string t, std::size_t caret);
  std::size_t snap(std::size_t pos) const;
  std::size_t prev_boundary(std::size_t pos) const;
  std::size_t next_boundary(std::size_t pos) const;
  std::size_t line_start(std::size_t pos) const;
  std::size_t line_end(std::size_t pos) const;
  std::size_t pos_at(int row, int col) const;
  void place(std::size_t pos, bool extend);
  void erase_range(std::size_t b, std::size_t e);
  void unit_around(std::size_t off, bool word, std::size_t& b, std::size_t& e) const;
  InputAction handle_key(const KeyEvent& k, const Bindings& b);
  InputAction handle_mouse(const MouseEvent& m, std::uint64_t now_ms);
  void drag_to(int x, int y);

  std::string text_;
  std::vector<unicode::Grapheme> g_;
  std::size_t caret_ = 0;
  InputSelection sel_;
  std::optional<int> goal_col_;
  InputOptions opt_;
  int prompt_w_ = 2;

  std::vector<std::string> hist_;
  std::size_t hist_pos_ = 0;
  std::string draft_;

  Rect area_;
  int width_ = 80;
  int top_ = 0;
  mutable bool dirty_ = true;
  mutable Flow flow_;

  struct Drag {
    bool active = false;
    std::size_t begin = 0, end = 0;  // the pressed unit
    int x = 0, y = 0;
  } drag_;
  struct Click {
    std::uint64_t at_ms = 0;
    int x = -1, y = -1, count = 0;
  } click_;
};

}  // namespace rolltui
