//
// input_test.cpp — the input widget's state machine (Input.hpp), table-tested with
// no terminal: the key table line by line, grapheme-boundary caret and erasure, word
// motions, the logical-line keys, vertical movement with a goal column, history with
// a draft and immutable entries, selection by keys and by mouse (both drag
// directions, double/triple click, Shift+press, release-copies), paste sanitising,
// the cell-wrap layout (full rows, a wide glyph at the edge, tabs, newlines), the
// caret's scroll, hit-testing, and what a frame shows.
//
#include <string>
#include <vector>

#include "rolltui/Input.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using rolltui_test::check;

namespace {

KeyEvent key(Key k, bool shift = false, bool ctrl = false, bool alt = false) {
  KeyEvent e;
  e.key = k;
  e.shift = shift;
  e.ctrl = ctrl;
  e.alt = alt;
  return e;
}
KeyEvent chr(char32_t c) { KeyEvent e; e.key = Key::Char; e.ch = c; return e; }
KeyEvent ctrl(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.ctrl = true; return e; }
KeyEvent alt(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.alt = true; return e; }
MouseEvent mouse(MouseEvent::Kind k, int x, int y, bool shift = false) {
  MouseEvent m;
  m.kind = k;
  m.x = x;
  m.y = y;
  m.button = (k == MouseEvent::Kind::WheelUp || k == MouseEvent::Kind::WheelDown) ? 0 : 1;
  m.shift = shift;
  return m;
}

// Types a string one code point at a time, as the decoder would deliver it.
InputAction type(Input& in, std::string_view s) {
  InputAction last = InputAction::Ignored;
  for (const unicode::DecodedChar& d : unicode::decode_utf8(s)) last = in.handle(chr(d.cp));
  return last;
}

std::string sel_of(const Input& in) { return in.selected_text(); }

Input fresh(int width = 80, int height = 1) {
  Input in;
  in.layout({0, 0, width, height});
  return in;
}

const char* kWoman = "\xF0\x9F\x91\xA9";          // U+1F469
const char* kZwj = "\xE2\x80\x8D";                // U+200D
const char* kLaptop = "\xF0\x9F\x92\xBB";         // U+1F4BB
const char* kEacute = "\xC3\xA9";                 // U+00E9

void test_typing_and_grapheme_boundaries() {
  Input in = fresh();
  check(type(in, "ab") == InputAction::Handled && in.text() == "ab" && in.caret() == 2, "typing accumulates and moves the caret");
  type(in, kEacute);
  check(in.text() == std::string("ab") + kEacute && in.caret() == 4, "a two-byte character accumulates whole");
  in.handle(key(Key::Backspace));
  check(in.text() == "ab" && in.caret() == 2, "Backspace removes the whole character, not one byte");
  type(in, std::string(kWoman) + kZwj + kLaptop);  // one grapheme cluster, three key events
  check(in.text().size() == 2 + 4 + 3 + 4, "an emoji ZWJ sequence typed as three code points is stored whole");
  in.handle(key(Key::Left));
  check(in.caret() == 2, "Left steps over the whole ZWJ sequence (one grapheme)");
  in.handle(key(Key::Right));
  check(in.caret() == in.text().size(), "Right steps back over it");
  in.handle(key(Key::Backspace));
  check(in.text() == "ab", "Backspace erases the whole ZWJ sequence in one keystroke");
  in.handle(key(Key::Left));
  in.handle(key(Key::Delete));
  check(in.text() == "a" && in.caret() == 1, "Delete erases the grapheme after the caret");
  Input e = fresh();
  e.handle(key(Key::Backspace));
  e.handle(key(Key::Delete));
  e.handle(key(Key::Left));
  check(e.text().empty() && e.caret() == 0, "Backspace/Delete/Left on an empty buffer are harmless");
  // A combining mark typed after a base merges with it; the caret stays on a boundary.
  Input c = fresh();
  type(c, "e");
  type(c, "\xCC\x81");  // U+0301 combining acute
  check(c.text() == "e\xCC\x81" && c.caret() == 3, "a combining mark joins the base and the caret is after the cluster");
  c.handle(key(Key::Left));
  check(c.caret() == 0, "Left crosses the base + mark as one grapheme");
  // Inserting a base before an existing lone combining mark: the caret snaps to the
  // boundary after the new cluster rather than sitting inside it.
  Input m = fresh();
  m.set_text("\xCC\x81");
  m.set_caret(0);
  m.insert("e");
  check(m.caret() == 3, "the caret is snapped past a cluster it landed inside (" + std::to_string(m.caret()) + ")");
  Input none = fresh();
  check(type(none, "") == InputAction::Ignored, "nothing typed, nothing handled");
}

void test_control_characters_never_enter() {
  Input in = fresh();
  in.insert("x\x01y\x7Fz");
  check(in.text() == "xyz", "insert drops control characters and DEL [" + in.text() + "]");
  in.handle(PasteEvent{"a\r\nb\rc\x07" "d\te"});
  check(in.text() == "xyza\nb\ncd\te", "a paste is literal after sanitising: CR LF and CR become LF, BEL dropped, tab kept [" + in.text() + "]");
  Input s = fresh();
  s.set_text("q\rw");
  check(s.text() == "q\nw", "set_text sanitises the same way");
  check(fresh().handle(ctrl('g')) == InputAction::Ignored, "an unbound Ctrl+letter is Ignored, never inserted");
}

void test_word_motions() {
  Input in = fresh();
  in.set_text("hello, big world");
  std::vector<std::size_t> lefts;
  for (int i = 0; i < 5; ++i) { in.handle(key(Key::Left, false, true)); lefts.push_back(in.caret()); }
  check(lefts == std::vector<std::size_t>{11, 7, 5, 0, 0}, "Ctrl+Left: world, big, the comma, hello, then stays");
  std::vector<std::size_t> rights;
  for (int i = 0; i < 5; ++i) { in.handle(key(Key::Right, false, false, true)); rights.push_back(in.caret()); }
  check(rights == std::vector<std::size_t>{5, 6, 10, 16, 16}, "Alt+Right: hello, the comma, big, world, then stays");
  in.set_text("one two   ");
  in.handle(ctrl('w'));
  check(in.text() == "one " && in.caret() == 4, "Ctrl+W kills the word before the caret, trailing spaces included [" + in.text() + "]");
  in.handle(key(Key::Backspace, false, false, true));
  check(in.text().empty(), "Alt+Backspace kills the same way");
  in.set_text("one two");
  in.set_caret(0);
  in.handle(alt('d'));
  check(in.text() == " two" && in.caret() == 0, "Alt+D kills the word after the caret [" + in.text() + "]");
  in.handle(key(Key::Delete, false, true));
  check(in.text().empty(), "Ctrl+Delete kills the spaces and the word after");
  Input u = fresh();
  u.set_text("caf" "\xC3\xA9" " au lait");
  u.handle(key(Key::Left, false, true));
  u.handle(key(Key::Left, false, true));
  u.handle(key(Key::Left, false, true));
  check(u.caret() == 0, "word motion over a non-ASCII word lands on the byte boundary at its start");
}

void test_logical_line_keys() {
  Input in = fresh();
  in.set_text("one\ntwo");
  in.set_caret(6);
  in.handle(key(Key::Home));
  check(in.caret() == 4, "Home goes to the start of the logical line, not the text");
  in.handle(key(Key::End));
  check(in.caret() == 7, "End goes to the end of the logical line");
  in.set_caret(2);
  in.handle(key(Key::End));
  check(in.caret() == 3, "End on the first line stops at its newline");
  in.set_caret(6);
  in.handle(ctrl('u'));
  check(in.text() == "one\no" && in.caret() == 4, "Ctrl+U kills to the start of the logical line [" + in.text() + "]");
  in.set_text("hello world");
  in.set_caret(5);
  in.handle(ctrl('k'));
  check(in.text() == "hello", "Ctrl+K kills to the end of the line");
  Input e = fresh();
  check(e.handle(key(Key::Home)) == InputAction::Ignored && e.handle(key(Key::End)) == InputAction::Ignored,
        "Home/End on an EMPTY buffer are Ignored (the host may scroll with them)");
  check(in.handle(key(Key::Home, false, true)) == InputAction::Ignored && in.handle(key(Key::End, false, true)) == InputAction::Ignored,
        "Ctrl+Home / Ctrl+End are always Ignored");
  Input n = fresh();
  type(n, "one");
  check(n.handle(key(Key::Enter, false, false, true)) == InputAction::Handled && n.text() == "one\n", "Alt+Enter inserts a newline");
  type(n, "two");
  check(n.handle(key(Key::Enter)) == InputAction::Submit && n.text() == "one\ntwo", "Enter submits and leaves the text for the host to take");
  check(n.rows() == 2, "two logical lines are two rows");
}

void test_vertical_movement_keeps_a_goal_column() {
  Input in = fresh(80, 3);
  in.set_text("abcdef\nxy\nabcdef");
  in.set_caret(3);  // row 0, col 5 (after the "> " prompt)
  check(in.cell_of(3).row == 0 && in.cell_of(3).col == 5, "the caret's cell is (0, 5)");
  check(in.handle(key(Key::Down)) == InputAction::Handled && in.caret() == 9, "Down onto a shorter row lands at its end (" + std::to_string(in.caret()) + ")");
  in.handle(key(Key::Down));
  check(in.caret() == 13, "Down again returns to the goal column (offset 13, col 5)");
  in.handle(key(Key::Up));
  in.handle(key(Key::Up));
  check(in.caret() == 3, "Up twice comes back to where it started");
  in.handle(key(Key::Right));
  in.handle(key(Key::Down));
  in.handle(key(Key::Down));
  check(in.caret() == 14, "a horizontal move resets the goal column (col 6 → offset 14)");
  // Shift+Up/Down extend and never browse history.
  in.push_history("older");
  in.set_caret(13);
  in.handle(key(Key::Up, true));
  check(in.selection().begin() == 9 && in.selection().end() == 13 && in.text() == "abcdef\nxy\nabcdef", "Shift+Up extends the selection by a row");
  in.set_caret(2);
  in.handle(key(Key::Up, true));
  check(in.text() == "abcdef\nxy\nabcdef", "Shift+Up on the first row does not browse history");
  check(in.handle(key(Key::Up, false, true)) == InputAction::Ignored, "Ctrl+Up is Ignored");
}

void test_history_with_a_draft() {
  Input in = fresh();
  in.push_history("one");
  in.push_history("two");
  in.push_history("two");
  in.push_history("");
  check(in.history().size() == 2, "a repeat of the newest entry and an empty entry are not pushed");
  type(in, "draft");
  in.handle(key(Key::Up));
  check(in.text() == "two", "Up on the first row recalls the newest entry");
  in.handle(key(Key::Up));
  check(in.text() == "one", "Up again the one before");
  check(in.handle(key(Key::Up)) == InputAction::Handled && in.text() == "one", "Up at the oldest stays (Handled, nothing changes)");
  in.handle(key(Key::Down));
  check(in.text() == "two", "Down goes forward");
  in.handle(key(Key::Down));
  check(in.text() == "draft" && in.caret() == 5, "Down past the newest restores the draft, caret at its end");
  in.handle(key(Key::Down));
  check(in.text() == "draft", "Down at the draft stays");
  in.handle(key(Key::Up));
  type(in, "x");
  check(in.text() == "twox", "a recalled entry can be edited");
  in.handle(key(Key::Up));
  in.handle(key(Key::Down));
  check(in.text() == "two" && in.history()[1] == "two", "the entry itself was never modified (the edit is gone)");
  in.clear();
  check(in.text().empty() && in.history_cursor() == 2, "clear() empties the text and rewinds the history cursor to the draft");
  in.handle(key(Key::Up));
  check(in.text() == "two", "and Up after clear recalls the newest again");
  // Up in a multi-line buffer moves rows first; only the first row browses.
  Input m = fresh(80, 3);
  m.set_text("a\nb");
  m.push_history("h");
  m.set_text("a\nb");
  m.handle(key(Key::Up));
  check(m.text() == "a\nb" && m.caret() == 1, "Up from the second row moves to the first row");
  m.handle(key(Key::Up));
  check(m.text() == "h", "Up from the first row browses history");
  Input lim = fresh();
  InputOptions o;
  o.history_limit = 2;
  lim.set_options(o);
  lim.push_history("1");
  lim.push_history("2");
  lim.push_history("3");
  check(lim.history() == std::vector<std::string>{"2", "3"}, "the history limit drops the oldest");
}

void test_selection_by_keys() {
  Input in = fresh();
  in.set_text("hello world");
  in.set_caret(0);
  for (int i = 0; i < 5; ++i) in.handle(key(Key::Right, true));
  check(sel_of(in) == "hello" && in.caret() == 5, "Shift+Right five times selects hello");
  type(in, "X");
  check(in.text() == "X world" && in.caret() == 1 && in.selection().empty(), "typing over a selection replaces it");
  in.handle(key(Key::Right, true, true));
  check(sel_of(in) == " world", "Shift+Ctrl+Right extends by a word");
  in.handle(key(Key::Left));
  check(in.caret() == 1 && in.selection().empty(), "Left with a selection collapses to its start");
  in.handle(key(Key::End, true));
  check(sel_of(in) == " world", "Shift+End extends to the end of the line");
  in.handle(key(Key::Backspace));
  check(in.text() == "X", "Backspace removes the selection");
  in.set_text("abc");
  in.handle(ctrl('a'));
  check(sel_of(in) == "abc" && in.caret() == 3, "Ctrl+A selects all, caret at the end");
  in.handle(key(Key::Delete));
  check(in.text().empty(), "Delete removes the selection");
  in.set_text("abc");
  in.handle(ctrl('a'));
  check(in.handle(key(Key::Escape)) == InputAction::Handled && in.selection().empty() && in.text() == "abc", "Escape clears the selection and keeps the text");
  check(in.handle(key(Key::Escape)) == InputAction::Ignored, "Escape with no selection is Ignored (the stack closes popups with it)");
  std::string copied;
  in.on_copy = [&](const std::string& s) { copied = s; };
  check(in.handle(alt('c')) == InputAction::Ignored, "Alt+C with no selection is Ignored (the transcript may copy)");
  in.handle(ctrl('a'));
  check(in.handle(alt('c')) == InputAction::Handled && copied == "abc", "Alt+C copies the selection through on_copy");
  in.set_caret(1, true);
  check(sel_of(in) == "a" && in.caret() == 1, "set_caret(extend) keeps the anchor (0, from select-all) and moves the head");
  in.set_caret(3);
  in.handle(key(Key::Home, true));
  check(sel_of(in) == "abc" && in.caret() == 0, "Shift+Home extends to the line start");
  in.set_text("ab");
  in.handle(key(Key::Left, true));
  in.handle(key(Key::Right, true));
  check(in.selection().empty(), "a selection shrunk back to nothing is empty");
}

void test_eof_and_ignored_keys() {
  Input in = fresh();
  check(in.handle(ctrl('d')) == InputAction::Eof, "Ctrl+D on an empty buffer is Eof");
  in.set_text("ab");
  in.set_caret(0);
  check(in.handle(ctrl('d')) == InputAction::Handled && in.text() == "b", "Ctrl+D on text deletes forward");
  check(in.handle(key(Key::Tab)) == InputAction::Ignored, "Tab is Ignored");
  check(in.handle(key(Key::PageUp)) == InputAction::Ignored && in.handle(key(Key::F1)) == InputAction::Ignored, "PageUp and F1 are Ignored");
  check(in.handle(ctrl('c')) == InputAction::Ignored && in.handle(ctrl('l')) == InputAction::Ignored, "Ctrl+C and Ctrl+L are the host's");
  check(in.handle(mouse(MouseEvent::Kind::WheelDown, 3, 0)) == InputAction::Ignored, "the wheel is Ignored");
  check(in.text() == "b", "and none of them touched the text");
}

void test_cell_wrap_layout() {
  Input in = fresh(10, 3);  // 10 wide: "> " + 8 cells per row
  in.set_text("abcdefghij");
  check(in.rows() == 2 && in.cell_of(8).row == 1 && in.cell_of(8).col == 2, "ten graphemes at width 10 wrap after eight: the ninth is at (1, 2)");
  check(in.cell_of(10).row == 1 && in.cell_of(10).col == 4, "the end of the text is after the last glyph");
  in.set_text("abcdefgh");
  check(in.rows() == 2 && in.cell_of(8).row == 1 && in.cell_of(8).col == 2, "a text that exactly fills a row puts the caret at the start of a new row");
  check(in.rows_for(80) == 1 && in.rows_for(10) == 2 && in.rows_for(6) == 3,
        "rows_for answers for other widths (80: 1; 10: 2; 6: two full rows of four and the caret's row)");
  in.set_text("abcdefg\xE6\xBC\xA2");  // seven cells then a 2-cell ideograph at col 9
  check(in.cell_of(7).row == 1 && in.cell_of(7).col == 2, "a 2-cell glyph that does not fit the last column starts the next row");
  in.set_text("a\tb");
  check(in.cell_of(1).col == 3 && in.cell_of(2).col == 6, "a tab expands to the next multiple of 4 from the text's first column (a@2, tab 3..5, b@6)");
  in.set_text("ab\ncd");
  check(in.cell_of(2).row == 0 && in.cell_of(2).col == 4 && in.cell_of(3).row == 1 && in.cell_of(3).col == 2,
        "a newline sits at the end of its row; the next grapheme starts the next row at the indent");
  in.set_text("ab\n");
  check(in.rows() == 2 && in.cell_of(3).row == 1 && in.cell_of(3).col == 2, "a trailing newline gives an empty last row with the caret on it");
  in.set_text("");
  check(in.rows() == 1 && in.cell_of(0).row == 0 && in.cell_of(0).col == 2, "empty: one row, caret after the prompt");
  Input tiny = fresh(1, 1);
  tiny.set_text("abc");
  check(tiny.rows() == 4, "a width below the prompt still places one grapheme per row, the caret on a fourth (progress, never a hang)");
}

void test_scroll_keeps_the_caret_visible() {
  Input in = fresh(10, 2);
  in.set_text("abcdefghijklmnopqrstuvwxyz");  // 26 → rows of 8: 4 rows
  in.layout({0, 0, 10, 2});
  check(in.rows() == 4 && in.top_row() == 2, "with the caret at the end the last two rows are shown (top 2)");
  in.set_caret(0);
  in.layout({0, 0, 10, 2});
  check(in.top_row() == 0, "moving the caret to the start scrolls back to the top");
  in.set_caret(9);
  in.layout({0, 0, 10, 2});
  check(in.top_row() == 0, "a caret already in view does not move the scroll");
  in.set_caret(17);
  in.layout({0, 0, 10, 2});
  check(in.top_row() == 1, "a caret one row below the view scrolls by one");
}

void test_mouse() {
  Input in = fresh(20, 1);
  in.set_text("hello world");
  std::string copied;
  in.on_copy = [&](const std::string& s) { copied = s; };
  in.handle(mouse(MouseEvent::Kind::Press, 3, 0), 1000);
  in.handle(mouse(MouseEvent::Kind::Release, 3, 0), 1050);
  check(in.caret() == 1 && in.selection().empty() && copied.empty(), "a click places the caret before the glyph under it and selects nothing");
  in.handle(mouse(MouseEvent::Kind::Press, 0, 0), 2000);
  check(in.caret() == 0, "a click on the prompt places the caret at the row's start");
  in.handle(mouse(MouseEvent::Kind::Press, 2, 0), 3000);
  in.handle(mouse(MouseEvent::Kind::Drag, 6, 0), 3100);
  check(sel_of(in) == "hello" && in.caret() == 5, "a drag from h to o selects hello — the glyph under the pointer included");
  in.handle(mouse(MouseEvent::Kind::Release, 6, 0), 3200);
  check(copied == "hello", "release copies the selection");
  copied.clear();
  in.handle(mouse(MouseEvent::Kind::Press, 6, 0), 5000);
  in.handle(mouse(MouseEvent::Kind::Drag, 2, 0), 5100);
  in.handle(mouse(MouseEvent::Kind::Release, 2, 0), 5200);
  check(copied == "hello" && in.caret() == 0, "the same drag backwards selects the same hello (pressed glyph included)");
  in.handle(mouse(MouseEvent::Kind::Press, 2, 0), 7000);
  in.handle(mouse(MouseEvent::Kind::Drag, 40, 0), 7100);
  in.handle(mouse(MouseEvent::Kind::Release, 40, 0), 7200);
  check(copied == "hello world", "a drag past the end of the row selects to the end");
  in.handle(mouse(MouseEvent::Kind::Press, 4, 0), 9000);
  in.handle(mouse(MouseEvent::Kind::Drag, 4, 0), 9100);
  check(sel_of(in) == "l", "a drag that stays on the pressed cell selects that one glyph");
  in.handle(mouse(MouseEvent::Kind::Release, 4, 0), 9200);
  // Double-click a word; the release does not narrow it.
  in.handle(mouse(MouseEvent::Kind::Press, 8, 0), 10000);
  in.handle(mouse(MouseEvent::Kind::Release, 8, 0), 10010);
  in.handle(mouse(MouseEvent::Kind::Press, 8, 0), 10100);
  check(sel_of(in) == "world" && in.caret() == 11, "a double-click selects the word");
  in.handle(mouse(MouseEvent::Kind::Release, 8, 0), 10150);
  check(copied == "world", "and the release copies the whole word");
  in.handle(mouse(MouseEvent::Kind::Press, 8, 0), 10300);
  check(sel_of(in) == "hello world", "a third click selects the logical line");
  in.handle(mouse(MouseEvent::Kind::Release, 8, 0), 10350);
  // A double-click then a drag grows by words.
  in.handle(mouse(MouseEvent::Kind::Press, 3, 0), 20000);
  in.handle(mouse(MouseEvent::Kind::Release, 3, 0), 20010);
  in.handle(mouse(MouseEvent::Kind::Press, 3, 0), 20100);
  in.handle(mouse(MouseEvent::Kind::Drag, 8, 0), 20200);
  check(sel_of(in) == "hello world", "a drag after a double-click grows by whole words");
  in.handle(mouse(MouseEvent::Kind::Release, 8, 0), 20300);
  // Shift+press extends from the caret, glyph under the pointer included.
  in.set_caret(0);
  in.handle(mouse(MouseEvent::Kind::Press, 6, 0, true), 30000);
  in.handle(mouse(MouseEvent::Kind::Release, 6, 0, true), 30100);
  check(sel_of(in) == "hello" && copied == "hello", "Shift+click extends the selection from the caret and copies");
  in.set_caret(5);
  in.handle(mouse(MouseEvent::Kind::Press, 2, 0, true), 31000);
  check(sel_of(in) == "hello", "Shift+click before the caret extends backwards, that glyph included");
  in.handle(mouse(MouseEvent::Kind::Release, 2, 0, true), 31100);
  // Two presses too far apart in time are two single clicks.
  in.handle(mouse(MouseEvent::Kind::Press, 8, 0), 40000);
  in.handle(mouse(MouseEvent::Kind::Release, 8, 0), 40010);
  in.handle(mouse(MouseEvent::Kind::Press, 8, 0), 41000);
  check(in.selection().empty() && in.caret() == 6, "presses a second apart are single clicks");
  in.handle(mouse(MouseEvent::Kind::Release, 8, 0), 41010);
  // Multi-row hits.
  Input m = fresh(10, 3);
  m.set_text("abcdefghij");
  m.layout({0, 0, 10, 3});
  m.handle(mouse(MouseEvent::Kind::Press, 2, 1), 1000);
  check(m.caret() == 8, "a click on the second row hits its first glyph");
  m.handle(mouse(MouseEvent::Kind::Press, 9, 1), 3000);
  check(m.caret() == 10, "a click past the end of the last row is the end of the text");
  m.handle(mouse(MouseEvent::Kind::Press, 5, 7), 5000);
  check(m.caret() == 10, "a click below the text is clamped to the last row");
  m.handle(mouse(MouseEvent::Kind::Press, 4, 0), 7000);
  m.handle(mouse(MouseEvent::Kind::Drag, 3, 1), 7100);
  check(m.selected_text() == "cdefghij", "a drag across rows selects across the wrap");
  m.handle(mouse(MouseEvent::Kind::Release, 3, 1), 7200);
  Input nl = fresh(20, 2);
  nl.set_text("ab\ncd");
  nl.layout({0, 0, 20, 2});
  nl.handle(mouse(MouseEvent::Kind::Press, 9, 0), 1000);
  check(nl.caret() == 2, "a click past the end of a line that ends in a newline is before the newline");
  nl.handle(mouse(MouseEvent::Kind::Release, 9, 0), 1010);
  check(nl.handle(mouse(MouseEvent::Kind::Drag, 3, 0), 1500) == InputAction::Ignored, "a drag with no press is Ignored");
  MouseEvent right = mouse(MouseEvent::Kind::Press, 3, 0);
  right.button = 3;
  check(nl.handle(right, 2000) == InputAction::Ignored, "a right-button press is Ignored");
}

void test_frame() {
  const Theme& th = *builtin_theme("default-dark");
  Input in = fresh(12, 1);
  InputOptions o;
  o.placeholder = "type here";
  in.set_options(o);
  in.layout({0, 0, 12, 1});
  Frame f(12, 1);
  in.draw(f, th, true);
  check(f.at(0, 0).text == ">" && f.at(2, 0).text == "t" && f.at(2, 0).style == th.style(Role::input_placeholder),
        "empty: the prompt, then the placeholder in its role");
  check(f.cursor().visible && f.cursor().x == 2 && f.cursor().y == 0, "the cursor sits after the prompt");
  in.set_text("hi");
  in.layout({0, 0, 12, 1});
  Frame g(12, 1);
  in.draw(g, th, true);
  check(g.at(2, 0).text == "h" && g.at(3, 0).text == "i" && g.at(4, 0).text == " " && g.at(2, 0).style == th.style(Role::input_text),
        "the text is drawn after the prompt in input_text; the placeholder is gone");
  check(g.cursor().visible && g.cursor().x == 4, "the cursor is after the text");
  Frame u(12, 1);
  in.draw(u, th, false);
  check(!u.cursor().visible, "an unfocused input shows no cursor");
  in.select_all();
  Frame s(12, 1);
  in.draw(s, th, true);
  check(s.at(2, 0).style == th.style(Role::selection) && s.at(3, 0).style == th.style(Role::selection) && s.at(0, 0).style == th.style(Role::prompt),
        "selected glyphs are in the selection role; the prompt is not");
  Input nl = fresh(12, 2);
  nl.set_text("a\nb");
  nl.select_all();
  nl.layout({0, 0, 12, 2});
  Frame n(12, 2);
  nl.draw(n, th, true);
  check(n.at(3, 0).style == th.style(Role::selection) && n.at(3, 0).text == " " && n.at(2, 1).text == "b",
        "a selected newline shows as one highlighted cell; the next line starts at the indent");
  // Inset and a scrolled view.
  Input sc = fresh(10, 1);
  InputOptions io;
  io.inset = 1;
  sc.set_options(io);
  sc.set_text("abcdefghijklmnop");  // 8 wide inside the inset: 6 per row → 3 rows
  sc.layout({0, 0, 10, 1});
  Frame v(10, 1);
  sc.draw(v, th, true);
  check(sc.rows() == 3 && sc.top_row() == 2 && v.at(3, 0).text == "m" && v.at(0, 0).text == " " && v.at(1, 0).text == " ",
        "with inset 1 and one row the last row is shown one cell in, under the hanging indent (the prompt lives on row 0 only)");
  Input wide = fresh(6, 2);
  wide.set_text("ab\xE6\xBC\xA2");
  wide.layout({0, 0, 6, 2});
  Frame w(6, 2);
  wide.draw(w, th, true);
  check(w.at(4, 0).text == "\xE6\xBC\xA2" && w.at(5, 0).continuation, "a 2-cell glyph is drawn whole at the row's end");
  check(wide.rows() == 2 && w.cursor().x == 2 && w.cursor().y == 1, "and, the row being full, the caret is on the next row");
  Input one = fresh(6, 1);
  one.set_text("ab\xE6\xBC\xA2");
  one.layout({0, 0, 6, 1});
  Frame o1(6, 1);
  one.draw(o1, th, true);
  check(one.top_row() == 1 && o1.at(0, 0).text == " " && o1.cursor().x == 2,
        "a one-row window after a full row shows the caret's (empty) row — the host grows the window instead");
}

void test_degenerate_sizes() {
  // A window can shrink to 1 or 0 cells in either dimension (the user, 2026-09-01):
  // every operation still works, nothing is written outside the area, and the text is
  // untouched by the geometry.
  const Theme& th = *builtin_theme("default-dark");
  for (Rect a : {Rect{0, 0, 0, 0}, Rect{0, 0, 1, 0}, Rect{0, 0, 0, 1}, Rect{0, 0, 1, 1}, Rect{3, 2, 0, 5}, Rect{3, 2, 5, 0}}) {
    const std::string name = std::to_string(a.w) + "x" + std::to_string(a.h);
    Input in;
    in.layout(a);
    type(in, "hello");
    in.handle(key(Key::Enter, false, false, true));
    in.handle(PasteEvent{"wor\xE6\xBC\xA2ld"});
    in.layout(a);
    in.handle(key(Key::Up));
    in.handle(key(Key::Down));
    in.handle(key(Key::Home));
    in.handle(key(Key::Left, true));
    in.handle(mouse(MouseEvent::Kind::Press, a.x, a.y), 1000);
    in.handle(mouse(MouseEvent::Kind::Drag, a.x + 3, a.y + 4), 1100);
    in.handle(mouse(MouseEvent::Kind::Release, a.x + 3, a.y + 4), 1200);
    in.layout(a);
    Frame f(8, 8);
    in.draw(f, th, true);
    check(in.text() == "hello\nwor\xE6\xBC\xA2ld" && in.rows() >= 2, name + ": the text and its rows survive a degenerate area");
    bool outside = false;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
        if (f.at(x, y).text != " " && !a.contains(x, y)) outside = true;
    check(!outside, name + ": nothing is drawn outside the area");
    check(!f.cursor().visible || a.contains(f.cursor().x, f.cursor().y) || (a.w == 0 && f.cursor().x == a.x) || (a.h == 0 && f.cursor().y == a.y),
          name + ": the cursor is inside the area or on its collapsed edge");
    in.set_caret(0);
    in.layout(a);
    check(in.top_row() == 0, name + ": the scroll follows the caret back to the top");
  }
  Input w1;
  w1.layout({0, 0, 1, 3});
  w1.set_text("\xE6\xBC\xA2\xE6\xBC\xA2");  // two 2-cell glyphs in a 1-cell window
  w1.layout({0, 0, 1, 3});
  check(w1.rows() == 3 && w1.cell_of(3).row == 1, "a 1-cell window still advances one glyph per row (a 2-cell glyph overflows rather than stalling)");
}

}  // namespace

int main() {
  test_typing_and_grapheme_boundaries();
  test_control_characters_never_enter();
  test_word_motions();
  test_logical_line_keys();
  test_vertical_movement_keeps_a_goal_column();
  test_history_with_a_draft();
  test_selection_by_keys();
  test_eof_and_ignored_keys();
  test_cell_wrap_layout();
  test_scroll_keeps_the_caret_visible();
  test_mouse();
  test_frame();
  test_degenerate_sizes();
  return rolltui_test::report("rolltui input_test");
}
