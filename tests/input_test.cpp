//
// input_test.cpp — the input widget's state machine (rolltui_input.h), table-tested with
// no terminal: the key table line by line, grapheme-boundary caret and erasure, word
// motions, the logical-line keys, vertical movement with a goal column, history with
// a draft and immutable entries, selection by keys and by mouse (both drag
// directions, double/triple click, Shift+press, release-copies), paste sanitising,
// the cell-wrap layout (full rows, a wide glyph at the edge, tabs, newlines), the
// caret's scroll, hit-testing, and what a frame shows.
//
// calls the C API (rolltui/rolltui.h) directly rather than through ANY C++
// binding header. An earlier pass here argued Screen.hpp/Theme.hpp/Unicode.hpp/Bindings.hpp
// were "not part of that layer" because Frame/Theme/RolltuiRect/Role/unicode:: are either
// one-definition aliases of C structs already or permanent C++-only vocabulary — true of each
// TYPE, and irrelevant to the actual rule: the task is "no rolltui/*.hpp", full stop, and
// those four are rolltui/*.hpp files regardless of how thin their contents are. `RolltuiRect`/
// `RolltuiStyle`/`RolltuiStyleColor`/`RolltuiMouseEvent`/`RolltuiDecodedChar` are reachable as
// their bare `Rolltui*` names straight from the C headers already included; `Frame` and
// `Theme` were real C++ wrapper CLASSES with no header alias (Screen.hpp's `Frame`,
// Theme.hpp's `Theme` struct) and get a small fixture each, same idiom as `rolltui-paint`'s
// and `layout_test.cpp`'s; `Role`'s NAMED enumerators are Style.hpp-only (rolltui_layout_tree.h
// forward-declares the type but not its values) and become `ROLLTUI_ROLE_*`; `unicode::
// decode_utf8` becomes `rolltui_u_decode_utf8_chars` into a caller-owned buffer.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_input.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

using rolltui_test::check;

namespace {

// `rolltui::Frame` (Screen.hpp) was a thin `unique_ptr<RolltuiFrame, Handle>` RAII wrapper;
// this is that same wrapper, written here (rolltui.h rule 5).
struct FrameC {
  RolltuiFrame* f;
  explicit FrameC(int w, int h, RolltuiStyle fill = {}) : f(rolltui_frame_new(w, h, fill)) {}
  FrameC(const FrameC&) = delete;
  ~FrameC() { rolltui_frame_free(f); }
  operator RolltuiFrame*() const { return f; }
  RolltuiCell at(int x, int y) const {
    RolltuiCell c{};
    rolltui_frame_cell(f, x, y, &c);
    return c;
  }
  std::string_view glyph(int x, int y) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_glyph(f, x, y, &n);
    return {p, n};
  }
  struct Cursor {
    int x = 0, y = 0;
    bool visible = false;
  };
  Cursor cursor() const {
    int x = 0, y = 0, visible = 0;
    rolltui_frame_cursor(f, &x, &y, &visible);
    return {x, y, visible != 0};
  }
};

// `rolltui::Theme` (Theme.hpp) bundled a style table with an effect map, a name and meta;
// none of that is ported (rolltui_json.h's own note on why). This test only ever reads
// styles, so the fixture is just the table `rolltui_theme_builtin_fill` fills.
struct ThemeFixture {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  ThemeFixture() = default;
  ThemeFixture(const ThemeFixture&) = delete;
  ~ThemeFixture() { rolltui_effect_map_free(effects); }
  const RolltuiStyle& style(unsigned char role) const { return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role); }
};
bool builtin_theme_c(std::string_view name, ThemeFixture& out) {
  out.effects = rolltui_theme_builtin_fill(name.data(), name.size(), out.styles, ROLLTUI_ROLE_COUNT);
  return out.effects != nullptr;
}

// `unicode::decode_utf8(s)` (Unicode.hpp) returned a freshly built `std::vector<DecodedChar>`;
// `rolltui_u_decode_utf8_chars` fills a caller-owned buffer instead (rolltui.h rule 4) — `out`
// must hold at least `len` entries because decoding is total (a malformed byte is one scalar
// of length 1), so a buffer this test builds fresh per call is the direct, if not per-frame,
// translation of that contract.
std::vector<RolltuiDecodedChar> decode_utf8_c(std::string_view s) {
  std::vector<RolltuiDecodedChar> out(s.size());
  const std::size_t n = rolltui_u_decode_utf8_chars(s.data(), s.size(), out.data());
  out.resize(n);
  return out;
}

// ---- the widget handle: OWNED, an explicit new/free pair (CLAUDE.md's "owned handles
// get explicit _new/_free pairs"). A unique_ptr rather than a hand-rolled try/finally, so
// an early return would still free it — there happen to be none in this file, but the
// class this replaces (rolltui::Input) was RAII for the same reason and there is no
// reason this test should be less careful than the thing it is standing in for. ----
using InputPtr = std::unique_ptr<RolltuiInput, void (*)(RolltuiInput*)>;
InputPtr new_input() { return InputPtr(rolltui_input_new(), rolltui_input_free); }

// `rolltui_input_new()` already gives a widget sane default options; a caller only has to
// build a `RolltuiInputOptions` itself to OVERRIDE them (three sites below). That struct
// still declares the C++ default constructor that used to live in Input.cpp (put there,
// not in either implementation, because the C build needs the struct's plain members
// initialised too and a deleted file cannot supply that). A stack `RolltuiInputOptions o;`
// still runs it — release what it allocates and re-init through the C entry point, the
// call a plain-C caller (and this test, once that constructor is gone) makes instead.
void init_options(RolltuiInputOptions& o) {
  rolltui_input_options_release(&o);
  rolltui_input_options_init(&o);
}

// THE LIBRARY'S THIRTY, not a copy of them. This block used to hold a verbatim
// copy of `Input.cpp`'s table, with the comment "since a caller of `rolltui_input_handle` has
// to hand this table over itself" — true, and the reason `rolltui_library_actions.c` counted
// FOUR copies of the 59-row vocabulary. `rolltui_input_default_actions()` is the caller's
// answer now, expanded from the one list; a host with different words still passes its own.
const RolltuiInputActions& kActions_ref() { return *rolltui_input_default_actions(); }
// The three roles a draw needs — copied from Input.cpp's `kRoles`.
constexpr RolltuiInputRoles kRoles = {
    ROLLTUI_ROLE_INPUT_TEXT,
    ROLLTUI_ROLE_SELECTION,
    ROLLTUI_ROLE_INPUT_PLACEHOLDER,
};

// The one draw scratch this test binary needs (Input.cpp kept one per thread; this binary
// is single-threaded, so one for the process, freed at exit via the static's destructor).
RolltuiDrawScratch* draw_scratch() {
  static std::unique_ptr<RolltuiDrawScratch, void (*)(RolltuiDrawScratch*)> s(rolltui_draw_scratch_new(),
                                                                              rolltui_draw_scratch_free);
  return s.get();
}

// What `rolltui_input_handle` answers, spelled the way rolltui::InputAction used to.
enum class InputAction : unsigned char {
  Ignored = ROLLTUI_INPUT_IGNORED,
  Handled = ROLLTUI_INPUT_HANDLED,
  Submit = ROLLTUI_INPUT_SUBMIT,
  Eof = ROLLTUI_INPUT_EOF,
};

InputAction handle(RolltuiInput* in, const RolltuiEvent& e, std::uint64_t now_ms = 0) {
  return static_cast<InputAction>(rolltui_input_handle(in, &e, rolltui_bindings_default(rolltui_test::test_context()), &kActions_ref(), now_ms));
}

// A BORROW of the C's buffer, valid until the text next changes — same contract
// rolltui::Input::text() documented.
std::string_view text_of(const RolltuiInput* in) {
  std::size_t n = 0;
  const char* p = rolltui_input_text(in, &n);
  return {p, n};
}
void set_text(RolltuiInput* in, std::string_view t) { rolltui_input_set_text(in, t.data(), t.size()); }
void set_caret(RolltuiInput* in, std::size_t byte, bool extend = false) {
  rolltui_input_set_caret(in, byte, extend ? 1 : 0);
}
RolltuiInputSelection selection_of(const RolltuiInput* in) {
  RolltuiInputSelection s;
  rolltui_input_selection(in, &s);
  return s;
}
std::string sel_of(const RolltuiInput* in) {
  std::size_t n = 0;
  const char* p = rolltui_input_selected_text(in, &n);
  return std::string(p, n);
}
void insert(RolltuiInput* in, std::string_view s) { rolltui_input_insert(in, s.data(), s.size()); }
void push_history(RolltuiInput* in, std::string_view entry) {
  rolltui_input_push_history(in, entry.data(), entry.size());
}
std::string_view history_at(const RolltuiInput* in, std::size_t i) {
  std::size_t n = 0;
  const char* p = rolltui_input_history_at(in, i, &n);
  return {p, n};
}
struct CellPos {
  int row = 0, col = 0;
};
CellPos cell_of(const RolltuiInput* in, std::size_t offset) {
  CellPos p;
  rolltui_input_cell_of(in, offset, &p.row, &p.col);
  return p;
}

// What crosses instead of a `std::function` clipboard: the host's callable behind a
// `void*`, exactly as Input.cpp's own trampoline does it (the ctx is a plain per-test
// struct here rather than the widget object itself, since there is no wrapper object).
struct CopyCtx {
  std::function<void(const std::string&)> fn;
};
void call_copy(void* ctx, const char* text, std::size_t len) {
  auto* c = static_cast<CopyCtx*>(ctx);
  if (c->fn) c->fn(std::string(text, len));
}

// Builds the RolltuiEvent a decoded key press would deliver. There is no KeyEvent on this
// side of the boundary (rolltui_keys.h's note: it carried a std::string only a C++ type
// could hold), so the test builds the RolltuiChord directly; `k` is one of the
// ROLLTUI_KEY_* constants.
RolltuiEvent key(int k, bool shift = false, bool ctrl = false, bool alt = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = static_cast<unsigned char>(k);
  e.key.shift = shift;
  e.key.ctrl = ctrl;
  e.key.alt = alt;
  return e;
}
RolltuiEvent chr(char32_t c) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = c;
  return e;
}
RolltuiEvent ctrl(char c) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = static_cast<char32_t>(c);
  e.key.ctrl = true;
  return e;
}
RolltuiEvent alt(char c) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = static_cast<char32_t>(c);
  e.key.alt = true;
  return e;
}
RolltuiEvent mouse(RolltuiMouseEvent::Kind k, int x, int y, bool shift = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse.kind = k;
  e.mouse.x = x;
  e.mouse.y = y;
  e.mouse.button = (k == RolltuiMouseEvent::Kind::WheelUp || k == RolltuiMouseEvent::Kind::WheelDown) ? 0 : 1;
  e.mouse.shift = shift;
  return e;
}
RolltuiEvent paste(std::string_view text) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_PASTE;
  e.text = text.data();
  e.text_len = text.size();
  return e;
}

// Types a string one code point at a time, as the decoder would deliver it.
InputAction type(RolltuiInput* in, std::string_view s) {
  InputAction last = InputAction::Ignored;
  for (const RolltuiDecodedChar& d : decode_utf8_c(s)) last = handle(in, chr(d.cp));
  return last;
}

// A repeated undo() call, recording text() after each successful call until undo()
// returns false (the bottom of the stack). The sequence of texts IS the group boundary
// the widget actually drew — this is what makes the grouping rule below a table driven
// through the widget rather than a description of intent.
std::vector<std::string> undo_trace(RolltuiInput* in) {
  std::vector<std::string> trace;
  while (rolltui_input_undo(in) != 0) trace.emplace_back(text_of(in));
  return trace;
}

// the history is `history_count()` + `history_at(i)` — nothing on the C side
// can hand back a `std::vector<std::string>` without building one per call (the same reason
// `Frame`'s marks are). This test wants the whole list to compare, so it builds one HERE,
// where the copy is the test's own and visible.
std::vector<std::string> history_of(const RolltuiInput* in) {
  std::vector<std::string> out;
  const std::size_t n = rolltui_input_history_count(in);
  for (std::size_t i = 0; i < n; ++i) out.emplace_back(history_at(in, i));
  return out;
}
std::string joined(const std::vector<std::string>& v) {
  std::string s = "[";
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) s += "|";
    s += v[i];
  }
  return s + "]";
}

InputPtr fresh(int width = 80, int height = 1) {
  InputPtr in = new_input();
  rolltui_input_layout(in.get(), {0, 0, width, height});
  return in;
}

const char* kWoman = "\xF0\x9F\x91\xA9";   // U+1F469
const char* kZwj = "\xE2\x80\x8D";         // U+200D
const char* kLaptop = "\xF0\x9F\x92\xBB";  // U+1F4BB
const char* kEacute = "\xC3\xA9";          // U+00E9

void test_typing_and_grapheme_boundaries() {
  InputPtr in = fresh();
  check(type(in.get(), "ab") == InputAction::Handled && text_of(in.get()) == "ab" && rolltui_input_caret(in.get()) == 2,
        "typing accumulates and moves the caret");
  type(in.get(), kEacute);
  check(text_of(in.get()) == std::string("ab") + kEacute && rolltui_input_caret(in.get()) == 4,
        "a two-byte character accumulates whole");
  handle(in.get(), key(ROLLTUI_KEY_BACKSPACE));
  check(text_of(in.get()) == "ab" && rolltui_input_caret(in.get()) == 2, "Backspace removes the whole character, not one byte");
  type(in.get(), std::string(kWoman) + kZwj + kLaptop);  // one grapheme cluster, three key events
  check(text_of(in.get()).size() == 2 + 4 + 3 + 4, "an emoji ZWJ sequence typed as three code points is stored whole");
  handle(in.get(), key(ROLLTUI_KEY_LEFT));
  check(rolltui_input_caret(in.get()) == 2, "Left steps over the whole ZWJ sequence (one grapheme)");
  handle(in.get(), key(ROLLTUI_KEY_RIGHT));
  check(rolltui_input_caret(in.get()) == text_of(in.get()).size(), "Right steps back over it");
  handle(in.get(), key(ROLLTUI_KEY_BACKSPACE));
  check(text_of(in.get()) == "ab", "Backspace erases the whole ZWJ sequence in one keystroke");
  handle(in.get(), key(ROLLTUI_KEY_LEFT));
  handle(in.get(), key(ROLLTUI_KEY_DELETE));
  check(text_of(in.get()) == "a" && rolltui_input_caret(in.get()) == 1, "Delete erases the grapheme after the caret");
  InputPtr e = fresh();
  handle(e.get(), key(ROLLTUI_KEY_BACKSPACE));
  handle(e.get(), key(ROLLTUI_KEY_DELETE));
  handle(e.get(), key(ROLLTUI_KEY_LEFT));
  check(text_of(e.get()).empty() && rolltui_input_caret(e.get()) == 0, "Backspace/Delete/Left on an empty buffer are harmless");
  // A combining mark typed after a base merges with it; the caret stays on a boundary.
  InputPtr c = fresh();
  type(c.get(), "e");
  type(c.get(), "\xCC\x81");  // U+0301 combining acute
  check(text_of(c.get()) == "e\xCC\x81" && rolltui_input_caret(c.get()) == 3,
        "a combining mark joins the base and the caret is after the cluster");
  handle(c.get(), key(ROLLTUI_KEY_LEFT));
  check(rolltui_input_caret(c.get()) == 0, "Left crosses the base + mark as one grapheme");
  // Inserting a base before an existing lone combining mark: the caret snaps to the
  // boundary after the new cluster rather than sitting inside it.
  InputPtr m = fresh();
  set_text(m.get(), "\xCC\x81");
  set_caret(m.get(), 0);
  insert(m.get(), "e");
  check(rolltui_input_caret(m.get()) == 3,
        "the caret is snapped past a cluster it landed inside (" + std::to_string(rolltui_input_caret(m.get())) + ")");
  InputPtr none = fresh();
  check(type(none.get(), "") == InputAction::Ignored, "nothing typed, nothing handled");
}

void test_control_characters_never_enter() {
  InputPtr in = fresh();
  insert(in.get(), "x\x01y\x7Fz");
  check(text_of(in.get()) == "xyz", "insert drops control characters and DEL [" + std::string(text_of(in.get())) + "]");
  handle(in.get(), paste("a\r\nb\rc\x07"
                         "d\te"));
  check(text_of(in.get()) == "xyza\nb\ncd\te",
        "a paste is literal after sanitising: CR LF and CR become LF, BEL dropped, tab kept [" +
            std::string(text_of(in.get())) + "]");
  InputPtr s = fresh();
  set_text(s.get(), "q\rw");
  check(text_of(s.get()) == "q\nw", "set_text sanitises the same way");
  check(handle(fresh().get(), ctrl('g')) == InputAction::Ignored, "an unbound Ctrl+letter is Ignored, never inserted");
}

void test_word_motions() {
  InputPtr in = fresh();
  set_text(in.get(), "hello, big world");
  std::vector<std::size_t> lefts;
  for (int i = 0; i < 5; ++i) {
    handle(in.get(), key(ROLLTUI_KEY_LEFT, false, true));
    lefts.push_back(rolltui_input_caret(in.get()));
  }
  check(lefts == std::vector<std::size_t>{11, 7, 5, 0, 0}, "Ctrl+Left: world, big, the comma, hello, then stays");
  std::vector<std::size_t> rights;
  for (int i = 0; i < 5; ++i) {
    handle(in.get(), key(ROLLTUI_KEY_RIGHT, false, false, true));
    rights.push_back(rolltui_input_caret(in.get()));
  }
  check(rights == std::vector<std::size_t>{5, 6, 10, 16, 16}, "Alt+Right: hello, the comma, big, world, then stays");
  set_text(in.get(), "one two   ");
  handle(in.get(), ctrl('w'));
  check(text_of(in.get()) == "one " && rolltui_input_caret(in.get()) == 4,
        "Ctrl+W kills the word before the caret, trailing spaces included [" + std::string(text_of(in.get())) + "]");
  handle(in.get(), key(ROLLTUI_KEY_BACKSPACE, false, false, true));
  check(text_of(in.get()).empty(), "Alt+Backspace kills the same way");
  set_text(in.get(), "one two");
  set_caret(in.get(), 0);
  handle(in.get(), alt('d'));
  check(text_of(in.get()) == " two" && rolltui_input_caret(in.get()) == 0,
        "Alt+D kills the word after the caret [" + std::string(text_of(in.get())) + "]");
  handle(in.get(), key(ROLLTUI_KEY_DELETE, false, true));
  check(text_of(in.get()).empty(), "Ctrl+Delete kills the spaces and the word after");
  InputPtr u = fresh();
  set_text(u.get(), "caf"
                     "\xC3\xA9"
                     " au lait");
  handle(u.get(), key(ROLLTUI_KEY_LEFT, false, true));
  handle(u.get(), key(ROLLTUI_KEY_LEFT, false, true));
  handle(u.get(), key(ROLLTUI_KEY_LEFT, false, true));
  check(rolltui_input_caret(u.get()) == 0, "word motion over a non-ASCII word lands on the byte boundary at its start");
}

void test_logical_line_keys() {
  InputPtr in = fresh();
  set_text(in.get(), "one\ntwo");
  set_caret(in.get(), 6);
  handle(in.get(), key(ROLLTUI_KEY_HOME));
  check(rolltui_input_caret(in.get()) == 4, "Home goes to the start of the logical line, not the text");
  handle(in.get(), key(ROLLTUI_KEY_END));
  check(rolltui_input_caret(in.get()) == 7, "End goes to the end of the logical line");
  set_caret(in.get(), 2);
  handle(in.get(), key(ROLLTUI_KEY_END));
  check(rolltui_input_caret(in.get()) == 3, "End on the first line stops at its newline");
  set_caret(in.get(), 6);
  handle(in.get(), ctrl('u'));
  check(text_of(in.get()) == "one\no" && rolltui_input_caret(in.get()) == 4,
        "Ctrl+U kills to the start of the logical line [" + std::string(text_of(in.get())) + "]");
  set_text(in.get(), "hello world");
  set_caret(in.get(), 5);
  handle(in.get(), ctrl('k'));
  check(text_of(in.get()) == "hello", "Ctrl+K kills to the end of the line");
  InputPtr e = fresh();
  check(handle(e.get(), key(ROLLTUI_KEY_HOME)) == InputAction::Ignored &&
            handle(e.get(), key(ROLLTUI_KEY_END)) == InputAction::Ignored,
        "Home/End on an EMPTY buffer are Ignored (the host may scroll with them)");
  check(handle(in.get(), key(ROLLTUI_KEY_HOME, false, true)) == InputAction::Ignored &&
            handle(in.get(), key(ROLLTUI_KEY_END, false, true)) == InputAction::Ignored,
        "Ctrl+Home / Ctrl+End are always Ignored");
  InputPtr n = fresh();
  type(n.get(), "one");
  check(handle(n.get(), key(ROLLTUI_KEY_ENTER, false, false, true)) == InputAction::Handled && text_of(n.get()) == "one\n",
        "Alt+Enter inserts a newline");
  type(n.get(), "two");
  check(handle(n.get(), key(ROLLTUI_KEY_ENTER)) == InputAction::Submit && text_of(n.get()) == "one\ntwo",
        "Enter submits and leaves the text for the host to take");
  check(rolltui_input_rows(n.get()) == 2, "two logical lines are two rows");
}

void test_vertical_movement_keeps_a_goal_column() {
  InputPtr in = fresh(80, 3);
  set_text(in.get(), "abcdef\nxy\nabcdef");
  set_caret(in.get(), 3);  // row 0, col 5 (after the "> " prompt)
  check(cell_of(in.get(), 3).row == 0 && cell_of(in.get(), 3).col == 5, "the caret's cell is (0, 5)");
  check(handle(in.get(), key(ROLLTUI_KEY_DOWN)) == InputAction::Handled && rolltui_input_caret(in.get()) == 9,
        "Down onto a shorter row lands at its end (" + std::to_string(rolltui_input_caret(in.get())) + ")");
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(rolltui_input_caret(in.get()) == 13, "Down again returns to the goal column (offset 13, col 5)");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(rolltui_input_caret(in.get()) == 3, "Up twice comes back to where it started");
  handle(in.get(), key(ROLLTUI_KEY_RIGHT));
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(rolltui_input_caret(in.get()) == 14, "a horizontal move resets the goal column (col 6 → offset 14)");
  // Shift+Up/Down extend and never browse history.
  push_history(in.get(), "older");
  set_caret(in.get(), 13);
  handle(in.get(), key(ROLLTUI_KEY_UP, true));
  check(selection_of(in.get()).begin() == 9 && selection_of(in.get()).end() == 13 &&
            text_of(in.get()) == "abcdef\nxy\nabcdef",
        "Shift+Up extends the selection by a row");
  set_caret(in.get(), 2);
  handle(in.get(), key(ROLLTUI_KEY_UP, true));
  check(text_of(in.get()) == "abcdef\nxy\nabcdef", "Shift+Up on the first row does not browse history");
  check(handle(in.get(), key(ROLLTUI_KEY_UP, false, true)) == InputAction::Ignored, "Ctrl+Up is Ignored");
}

void test_history_with_a_draft() {
  InputPtr in = fresh();
  push_history(in.get(), "one");
  push_history(in.get(), "two");
  push_history(in.get(), "two");
  push_history(in.get(), "");
  check(rolltui_input_history_count(in.get()) == 2, "a repeat of the newest entry and an empty entry are not pushed");
  type(in.get(), "draft");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "two", "Up on the first row recalls the newest entry");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "one", "Up again the one before");
  check(handle(in.get(), key(ROLLTUI_KEY_UP)) == InputAction::Handled && text_of(in.get()) == "one",
        "Up at the oldest stays (Handled, nothing changes)");
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(text_of(in.get()) == "two", "Down goes forward");
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(text_of(in.get()) == "draft" && rolltui_input_caret(in.get()) == 5, "Down past the newest restores the draft, caret at its end");
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(text_of(in.get()) == "draft", "Down at the draft stays");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  type(in.get(), "x");
  check(text_of(in.get()) == "twox", "a recalled entry can be edited");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(text_of(in.get()) == "two" && history_at(in.get(), 1) == "two", "the entry itself was never modified (the edit is gone)");
  rolltui_input_clear(in.get());
  check(text_of(in.get()).empty() && rolltui_input_history_cursor(in.get()) == 2,
        "clear() empties the text and rewinds the history cursor to the draft");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "two", "and Up after clear recalls the newest again");
  // Up in a multi-line buffer moves rows first; only the first row browses.
  InputPtr m = fresh(80, 3);
  set_text(m.get(), "a\nb");
  push_history(m.get(), "h");
  set_text(m.get(), "a\nb");
  handle(m.get(), key(ROLLTUI_KEY_UP));
  check(text_of(m.get()) == "a\nb" && rolltui_input_caret(m.get()) == 1, "Up from the second row moves to the first row");
  handle(m.get(), key(ROLLTUI_KEY_UP));
  check(text_of(m.get()) == "h", "Up from the first row browses history");
  InputPtr lim = fresh();
  RolltuiInputOptions o;
  init_options(o);
  o.history_limit = 2;
  rolltui_input_set_options(lim.get(), &o);
  rolltui_input_options_release(&o);
  push_history(lim.get(), "1");
  push_history(lim.get(), "2");
  push_history(lim.get(), "3");
  check(history_of(lim.get()) == std::vector<std::string>{"2", "3"}, "the history limit drops the oldest");
}

void test_selection_by_keys() {
  InputPtr in = fresh();
  set_text(in.get(), "hello world");
  set_caret(in.get(), 0);
  for (int i = 0; i < 5; ++i) handle(in.get(), key(ROLLTUI_KEY_RIGHT, true));
  check(sel_of(in.get()) == "hello" && rolltui_input_caret(in.get()) == 5, "Shift+Right five times selects hello");
  type(in.get(), "X");
  check(text_of(in.get()) == "X world" && rolltui_input_caret(in.get()) == 1 && selection_of(in.get()).empty(),
        "typing over a selection replaces it");
  handle(in.get(), key(ROLLTUI_KEY_RIGHT, true, true));
  check(sel_of(in.get()) == " world", "Shift+Ctrl+Right extends by a word");
  handle(in.get(), key(ROLLTUI_KEY_LEFT));
  check(rolltui_input_caret(in.get()) == 1 && selection_of(in.get()).empty(), "Left with a selection collapses to its start");
  handle(in.get(), key(ROLLTUI_KEY_END, true));
  check(sel_of(in.get()) == " world", "Shift+End extends to the end of the line");
  handle(in.get(), key(ROLLTUI_KEY_BACKSPACE));
  check(text_of(in.get()) == "X", "Backspace removes the selection");
  set_text(in.get(), "abc");
  handle(in.get(), ctrl('a'));
  check(sel_of(in.get()) == "abc" && rolltui_input_caret(in.get()) == 3, "Ctrl+A selects all, caret at the end");
  handle(in.get(), key(ROLLTUI_KEY_DELETE));
  check(text_of(in.get()).empty(), "Delete removes the selection");
  set_text(in.get(), "abc");
  handle(in.get(), ctrl('a'));
  check(handle(in.get(), key(ROLLTUI_KEY_ESCAPE)) == InputAction::Handled && selection_of(in.get()).empty() &&
            text_of(in.get()) == "abc",
        "Escape clears the selection and keeps the text");
  check(handle(in.get(), key(ROLLTUI_KEY_ESCAPE)) == InputAction::Ignored,
        "Escape with no selection is Ignored (the stack closes popups with it)");
  std::string copied;
  CopyCtx ctx{[&](const std::string& s) { copied = s; }};
  rolltui_input_set_copy(in.get(), call_copy, &ctx);
  check(handle(in.get(), alt('c')) == InputAction::Ignored, "Alt+C with no selection is Ignored (the transcript may copy)");
  handle(in.get(), ctrl('a'));
  check(handle(in.get(), alt('c')) == InputAction::Handled && copied == "abc", "Alt+C copies the selection through on_copy");
  set_caret(in.get(), 1, true);
  check(sel_of(in.get()) == "a" && rolltui_input_caret(in.get()) == 1,
        "set_caret(extend) keeps the anchor (0, from select-all) and moves the head");
  set_caret(in.get(), 3);
  handle(in.get(), key(ROLLTUI_KEY_HOME, true));
  check(sel_of(in.get()) == "abc" && rolltui_input_caret(in.get()) == 0, "Shift+Home extends to the line start");
  set_text(in.get(), "ab");
  handle(in.get(), key(ROLLTUI_KEY_LEFT, true));
  handle(in.get(), key(ROLLTUI_KEY_RIGHT, true));
  check(selection_of(in.get()).empty(), "a selection shrunk back to nothing is empty");
}

void test_eof_and_ignored_keys() {
  InputPtr in = fresh();
  check(handle(in.get(), ctrl('d')) == InputAction::Eof, "Ctrl+D on an empty buffer is Eof");
  set_text(in.get(), "ab");
  set_caret(in.get(), 0);
  check(handle(in.get(), ctrl('d')) == InputAction::Handled && text_of(in.get()) == "b", "Ctrl+D on text deletes forward");
  check(handle(in.get(), key(ROLLTUI_KEY_TAB)) == InputAction::Ignored, "Tab is Ignored");
  check(handle(in.get(), key(ROLLTUI_KEY_PAGEUP)) == InputAction::Ignored &&
            handle(in.get(), key(ROLLTUI_KEY_F1)) == InputAction::Ignored,
        "PageUp and F1 are Ignored");
  check(handle(in.get(), ctrl('c')) == InputAction::Ignored && handle(in.get(), ctrl('l')) == InputAction::Ignored,
        "Ctrl+C and Ctrl+L are the host's");
  check(handle(in.get(), mouse(RolltuiMouseEvent::Kind::WheelDown, 3, 0)) == InputAction::Ignored, "the wheel is Ignored");
  check(text_of(in.get()) == "b", "and none of them touched the text");
}

void test_cell_wrap_layout() {
  InputPtr in = fresh(10, 3);  // 10 wide: "> " + 8 cells per row
  set_text(in.get(), "abcdefghij");
  check(rolltui_input_rows(in.get()) == 2 && cell_of(in.get(), 8).row == 1 && cell_of(in.get(), 8).col == 2,
        "ten graphemes at width 10 wrap after eight: the ninth is at (1, 2)");
  check(cell_of(in.get(), 10).row == 1 && cell_of(in.get(), 10).col == 4, "the end of the text is after the last glyph");
  set_text(in.get(), "abcdefgh");
  check(rolltui_input_rows(in.get()) == 2 && cell_of(in.get(), 8).row == 1 && cell_of(in.get(), 8).col == 2,
        "a text that exactly fills a row puts the caret at the start of a new row");
  check(rolltui_input_rows_for(in.get(), 80) == 1 && rolltui_input_rows_for(in.get(), 10) == 2 &&
            rolltui_input_rows_for(in.get(), 6) == 3,
        "rows_for answers for other widths (80: 1; 10: 2; 6: two full rows of four and the caret's row)");
  set_text(in.get(), "abcdefg\xE6\xBC\xA2");  // seven cells then a 2-cell ideograph at col 9
  check(cell_of(in.get(), 7).row == 1 && cell_of(in.get(), 7).col == 2,
        "a 2-cell glyph that does not fit the last column starts the next row");
  set_text(in.get(), "a\tb");
  check(cell_of(in.get(), 1).col == 3 && cell_of(in.get(), 2).col == 6,
        "a tab expands to the next multiple of 4 from the text's first column (a@2, tab 3..5, b@6)");
  set_text(in.get(), "ab\ncd");
  check(cell_of(in.get(), 2).row == 0 && cell_of(in.get(), 2).col == 4 && cell_of(in.get(), 3).row == 1 &&
            cell_of(in.get(), 3).col == 2,
        "a newline sits at the end of its row; the next grapheme starts the next row at the indent");
  set_text(in.get(), "ab\n");
  check(rolltui_input_rows(in.get()) == 2 && cell_of(in.get(), 3).row == 1 && cell_of(in.get(), 3).col == 2,
        "a trailing newline gives an empty last row with the caret on it");
  set_text(in.get(), "");
  check(rolltui_input_rows(in.get()) == 1 && cell_of(in.get(), 0).row == 0 && cell_of(in.get(), 0).col == 2,
        "empty: one row, caret after the prompt");
  InputPtr tiny = fresh(1, 1);
  set_text(tiny.get(), "abc");
  check(rolltui_input_rows(tiny.get()) == 4,
        "a width below the prompt still places one grapheme per row, the caret on a fourth (progress, never a hang)");
}

void test_scroll_keeps_the_caret_visible() {
  InputPtr in = fresh(10, 2);
  set_text(in.get(), "abcdefghijklmnopqrstuvwxyz");  // 26 → rows of 8: 4 rows
  rolltui_input_layout(in.get(), {0, 0, 10, 2});
  check(rolltui_input_rows(in.get()) == 4 && rolltui_input_top_row(in.get()) == 2,
        "with the caret at the end the last two rows are shown (top 2)");
  set_caret(in.get(), 0);
  rolltui_input_layout(in.get(), {0, 0, 10, 2});
  check(rolltui_input_top_row(in.get()) == 0, "moving the caret to the start scrolls back to the top");
  set_caret(in.get(), 9);
  rolltui_input_layout(in.get(), {0, 0, 10, 2});
  check(rolltui_input_top_row(in.get()) == 0, "a caret already in view does not move the scroll");
  set_caret(in.get(), 17);
  rolltui_input_layout(in.get(), {0, 0, 10, 2});
  check(rolltui_input_top_row(in.get()) == 1, "a caret one row below the view scrolls by one");
}

void test_mouse() {
  InputPtr in = fresh(20, 1);
  set_text(in.get(), "hello world");
  std::string copied;
  CopyCtx ctx{[&](const std::string& s) { copied = s; }};
  rolltui_input_set_copy(in.get(), call_copy, &ctx);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 3, 0), 1000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 3, 0), 1050);
  check(rolltui_input_caret(in.get()) == 1 && selection_of(in.get()).empty() && copied.empty(),
        "a click places the caret before the glyph under it and selects nothing");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 0, 0), 2000);
  check(rolltui_input_caret(in.get()) == 0, "a click on the prompt places the caret at the row's start");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 2, 0), 3000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Drag, 6, 0), 3100);
  check(sel_of(in.get()) == "hello" && rolltui_input_caret(in.get()) == 5,
        "a drag from h to o selects hello — the glyph under the pointer included");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 6, 0), 3200);
  check(copied == "hello", "release copies the selection");
  copied.clear();
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 6, 0), 5000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Drag, 2, 0), 5100);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 2, 0), 5200);
  check(copied == "hello" && rolltui_input_caret(in.get()) == 0, "the same drag backwards selects the same hello (pressed glyph included)");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 2, 0), 7000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Drag, 40, 0), 7100);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 40, 0), 7200);
  check(copied == "hello world", "a drag past the end of the row selects to the end");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 4, 0), 9000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Drag, 4, 0), 9100);
  check(sel_of(in.get()) == "l", "a drag that stays on the pressed cell selects that one glyph");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 4, 0), 9200);
  // Double-click a word; the release does not narrow it.
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 8, 0), 10000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 8, 0), 10010);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 8, 0), 10100);
  check(sel_of(in.get()) == "world" && rolltui_input_caret(in.get()) == 11, "a double-click selects the word");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 8, 0), 10150);
  check(copied == "world", "and the release copies the whole word");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 8, 0), 10300);
  check(sel_of(in.get()) == "hello world", "a third click selects the logical line");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 8, 0), 10350);
  // A double-click then a drag grows by words.
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 3, 0), 20000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 3, 0), 20010);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 3, 0), 20100);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Drag, 8, 0), 20200);
  check(sel_of(in.get()) == "hello world", "a drag after a double-click grows by whole words");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 8, 0), 20300);
  // Shift+press extends from the caret, glyph under the pointer included.
  set_caret(in.get(), 0);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 6, 0, true), 30000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 6, 0, true), 30100);
  check(sel_of(in.get()) == "hello" && copied == "hello", "Shift+click extends the selection from the caret and copies");
  set_caret(in.get(), 5);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 2, 0, true), 31000);
  check(sel_of(in.get()) == "hello", "Shift+click before the caret extends backwards, that glyph included");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 2, 0, true), 31100);
  // Two presses too far apart in time are two single clicks.
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 8, 0), 40000);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 8, 0), 40010);
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, 8, 0), 41000);
  check(selection_of(in.get()).empty() && rolltui_input_caret(in.get()) == 6, "presses a second apart are single clicks");
  handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, 8, 0), 41010);
  // Multi-row hits.
  InputPtr m = fresh(10, 3);
  set_text(m.get(), "abcdefghij");
  rolltui_input_layout(m.get(), {0, 0, 10, 3});
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Press, 2, 1), 1000);
  check(rolltui_input_caret(m.get()) == 8, "a click on the second row hits its first glyph");
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Press, 9, 1), 3000);
  check(rolltui_input_caret(m.get()) == 10, "a click past the end of the last row is the end of the text");
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Press, 5, 7), 5000);
  check(rolltui_input_caret(m.get()) == 10, "a click below the text is clamped to the last row");
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Press, 4, 0), 7000);
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Drag, 3, 1), 7100);
  check(sel_of(m.get()) == "cdefghij", "a drag across rows selects across the wrap");
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Release, 3, 1), 7200);
  InputPtr nl = fresh(20, 2);
  set_text(nl.get(), "ab\ncd");
  rolltui_input_layout(nl.get(), {0, 0, 20, 2});
  handle(nl.get(), mouse(RolltuiMouseEvent::Kind::Press, 9, 0), 1000);
  check(rolltui_input_caret(nl.get()) == 2, "a click past the end of a line that ends in a newline is before the newline");
  handle(nl.get(), mouse(RolltuiMouseEvent::Kind::Release, 9, 0), 1010);
  check(handle(nl.get(), mouse(RolltuiMouseEvent::Kind::Drag, 3, 0), 1500) == InputAction::Ignored, "a drag with no press is Ignored");
  RolltuiEvent right = mouse(RolltuiMouseEvent::Kind::Press, 3, 0);
  right.mouse.button = 3;
  check(handle(nl.get(), right, 2000) == InputAction::Ignored, "a right-button press is Ignored");
}

void test_frame() {
  ThemeFixture th;
  builtin_theme_c("default-dark", th);
  InputPtr in = fresh(12, 1);
  RolltuiInputOptions o;
  init_options(o);
  o.placeholder = "type here";
  rolltui_input_set_options(in.get(), &o);
  rolltui_input_options_release(&o);
  rolltui_input_layout(in.get(), {0, 0, 12, 1});
  FrameC f(12, 1);
  rolltui_input_draw(in.get(), f, draw_scratch(), th.styles, &kRoles, true);
  check(f.glyph(0, 0) == ">" && f.glyph(2, 0) == "t" && f.at(2, 0).style == th.style(ROLLTUI_ROLE_INPUT_PLACEHOLDER),
        "empty: the prompt, then the placeholder in its role");
  check(f.cursor().visible && f.cursor().x == 2 && f.cursor().y == 0, "the cursor sits after the prompt");
  set_text(in.get(), "hi");
  rolltui_input_layout(in.get(), {0, 0, 12, 1});
  FrameC g(12, 1);
  rolltui_input_draw(in.get(), g, draw_scratch(), th.styles, &kRoles, true);
  check(g.glyph(2, 0) == "h" && g.glyph(3, 0) == "i" && g.glyph(4, 0) == " " && g.at(2, 0).style == th.style(ROLLTUI_ROLE_INPUT_TEXT),
        "the text is drawn after the prompt in input_text; the placeholder is gone");
  check(g.cursor().visible && g.cursor().x == 4, "the cursor is after the text");
  FrameC u(12, 1);
  rolltui_input_draw(in.get(), u, draw_scratch(), th.styles, &kRoles, false);
  check(!u.cursor().visible, "an unfocused input shows no cursor");
  rolltui_input_select_all(in.get());
  FrameC s(12, 1);
  rolltui_input_draw(in.get(), s, draw_scratch(), th.styles, &kRoles, true);
  check(s.at(2, 0).style == th.style(ROLLTUI_ROLE_SELECTION) && s.at(3, 0).style == th.style(ROLLTUI_ROLE_SELECTION) &&
            s.at(0, 0).style == th.style(ROLLTUI_ROLE_PROMPT),
        "selected glyphs are in the selection role; the prompt is not");
  InputPtr nl = fresh(12, 2);
  set_text(nl.get(), "a\nb");
  rolltui_input_select_all(nl.get());
  rolltui_input_layout(nl.get(), {0, 0, 12, 2});
  FrameC n(12, 2);
  rolltui_input_draw(nl.get(), n, draw_scratch(), th.styles, &kRoles, true);
  check(n.at(3, 0).style == th.style(ROLLTUI_ROLE_SELECTION) && n.glyph(3, 0) == " " && n.glyph(2, 1) == "b",
        "a selected newline shows as one highlighted cell; the next line starts at the indent");
  // Inset and a scrolled view.
  InputPtr sc = fresh(10, 1);
  RolltuiInputOptions io;
  init_options(io);
  io.inset = 1;
  rolltui_input_set_options(sc.get(), &io);
  rolltui_input_options_release(&io);
  set_text(sc.get(), "abcdefghijklmnop");  // 8 wide inside the inset: 6 per row → 3 rows
  rolltui_input_layout(sc.get(), {0, 0, 10, 1});
  FrameC v(10, 1);
  rolltui_input_draw(sc.get(), v, draw_scratch(), th.styles, &kRoles, true);
  check(rolltui_input_rows(sc.get()) == 3 && rolltui_input_top_row(sc.get()) == 2 && v.glyph(3, 0) == "m" &&
            v.glyph(0, 0) == " " && v.glyph(1, 0) == " ",
        "with inset 1 and one row the last row is shown one cell in, under the hanging indent (the prompt lives on row 0 only)");
  InputPtr wide = fresh(6, 2);
  set_text(wide.get(), "ab\xE6\xBC\xA2");
  rolltui_input_layout(wide.get(), {0, 0, 6, 2});
  FrameC w(6, 2);
  rolltui_input_draw(wide.get(), w, draw_scratch(), th.styles, &kRoles, true);
  check(w.glyph(4, 0) == "\xE6\xBC\xA2" && w.at(5, 0).continuation, "a 2-cell glyph is drawn whole at the row's end");
  check(rolltui_input_rows(wide.get()) == 2 && w.cursor().x == 2 && w.cursor().y == 1,
        "and, the row being full, the caret is on the next row");
  InputPtr one = fresh(6, 1);
  set_text(one.get(), "ab\xE6\xBC\xA2");
  rolltui_input_layout(one.get(), {0, 0, 6, 1});
  FrameC o1(6, 1);
  rolltui_input_draw(one.get(), o1, draw_scratch(), th.styles, &kRoles, true);
  check(rolltui_input_top_row(one.get()) == 1 && o1.glyph(0, 0) == " " && o1.cursor().x == 2,
        "a one-row window after a full row shows the caret's (empty) row — the host grows the window instead");
}

// undo/redo. The grouping rule, stated in rolltui_input.h, as a table driven
// through the widget: each row performs a sequence of edits on a fresh input, then walks
// undo() to the bottom recording the text after every step. The sequence of texts is a
// direct read of where the widget drew a group boundary.
void test_undo_redo_grouping_rule() {
  struct Case {
    const char* name;
    void (*steps)(RolltuiInput*);
    std::vector<std::string> trace;
  };
  static const Case cases[] = {
      {"a run of ordinary insertions is one group", [](RolltuiInput* in) { type(in, "abc"); }, {""}},
      {"a plain Backspace with no selection is ORDINARY too: it merges into the same run, not a boundary of its own",
       [](RolltuiInput* in) {
         type(in, "abcx");
         rolltui_input_erase_backward(in);
       },
       {""}},
      {"a timeout closes the group even between two edits that would otherwise merge",
       [](RolltuiInput* in) {
         handle(in, chr('a'), 1000);
         handle(in, chr('b'), 6000);
       },
       {"a", ""}},
      {"well inside the timeout, the run still merges",
       [](RolltuiInput* in) {
         handle(in, chr('a'), 1000);
         handle(in, chr('b'), 1200);
       },
       {""}},
      {"a caret move closes the group; the move itself is never its own undo step",
       [](RolltuiInput* in) {
         type(in, "ab");
         rolltui_input_move_left(in, 0);
         type(in, "c");
       },
       {"ab", ""}},
      {"a kill (kill_word_backward) is its own atomic group, merging with neither side",
       [](RolltuiInput* in) {
         type(in, "one two");
         rolltui_input_kill_word_backward(in);
         type(in, "X");
       },
       {"one ", "one two", ""}},
      {"a paste is its own atomic group, merging with neither side",
       [](RolltuiInput* in) {
         type(in, "ab");
         handle(in, paste("XY"));
         type(in, "c");
       },
       {"abXY", "ab", ""}},
      {"typing over a selection (a selection-replace) is atomic, merging with neither side",
       [](RolltuiInput* in) {
         type(in, "hello world");
         set_caret(in, 6);
         rolltui_input_move_word_right(in, 1);
         type(in, "X");
         type(in, "Y");
       },
       {"hello X", "hello world", ""}},
      {"erasing an active selection (Backspace on a selection) is also a selection-replace: atomic",
       [](RolltuiInput* in) {
         type(in, "hello world");
         set_caret(in, 6);
         rolltui_input_move_word_right(in, 1);
         rolltui_input_erase_backward(in);
         type(in, "Y");
       },
       {"hello ", "hello world", ""}},
  };
  for (const Case& c : cases) {
    InputPtr in = fresh();
    c.steps(in.get());
    const std::vector<std::string> trace = undo_trace(in.get());
    check(trace == c.trace, std::string(c.name) + " " + joined(trace));
  }
}

void test_undo_redo_restores_caret_and_selection_exactly() {
  // A caret move away from a group is never its own undo step, but it is not amnesia
  // either: the position it leaves behind is exactly what the NEXT edit's undo
  // restores — not a stale caret from before the move (see UNDO's "where the move DID
  // change something" clause; this is what UndoStack::replace_current is for).
  InputPtr in = fresh();
  type(in.get(), "ab");
  rolltui_input_move_left(in.get(), 0);  // caret now 1, not part of any group
  type(in.get(), "c");                   // "acb", caret 2
  check(text_of(in.get()) == "acb" && rolltui_input_caret(in.get()) == 2, "setup: acb, caret after the inserted c");
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()) == "ab" && rolltui_input_caret(in.get()) == 1 &&
            selection_of(in.get()).empty(),
        "undo restores the caret to EXACTLY where it was before the edit (1, where the arrow key had moved it) — not the earlier, stale position (2) from before that move");
  check(rolltui_input_redo(in.get()) != 0 && text_of(in.get()) == "acb" && rolltui_input_caret(in.get()) == 2 &&
            selection_of(in.get()).empty(),
        "redo restores the \"c\" insertion byte-for-byte");
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()) == "ab" && rolltui_input_caret(in.get()) == 1,
        "undo again reaches the same precise pre-edit state");
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()).empty() && rolltui_input_caret(in.get()) == 0,
        "and once more reaches the empty baseline");
  check(rolltui_input_undo(in.get()) == 0 && text_of(in.get()).empty(), "undo below the bottom of the stack is a no-op, not an empty line");

  // The selection is restored exactly too, not just the text and the caret.
  InputPtr sel = fresh();
  type(sel.get(), "hello world");
  set_caret(sel.get(), 6);
  rolltui_input_move_word_right(sel.get(), 1);  // selects "world": anchor 6, head 11
  type(sel.get(), "X");
  type(sel.get(), "Y");
  check(text_of(sel.get()) == "hello XY", "setup: hello XY");
  check(rolltui_input_undo(sel.get()) != 0 && text_of(sel.get()) == "hello X" && rolltui_input_caret(sel.get()) == 7 &&
            selection_of(sel.get()).empty(),
        "undo the Y");
  check(rolltui_input_undo(sel.get()) != 0 && text_of(sel.get()) == "hello world" && rolltui_input_caret(sel.get()) == 11 &&
            selection_of(sel.get()).active && selection_of(sel.get()).begin() == 6 && selection_of(sel.get()).end() == 11,
        "undo the selection-replace restores the SELECTION too, byte-for-byte (\"world\" selected again)");
  check(rolltui_input_redo(sel.get()) != 0 && text_of(sel.get()) == "hello X" && rolltui_input_caret(sel.get()) == 7 &&
            selection_of(sel.get()).empty(),
        "redo replays the replace exactly");
  check(rolltui_input_redo(sel.get()) != 0 && text_of(sel.get()) == "hello XY" && rolltui_input_caret(sel.get()) == 8 &&
            selection_of(sel.get()).empty(),
        "redo replays the Y");
  check(rolltui_input_redo(sel.get()) == 0, "nothing left to redo");
}

void test_undo_redo_new_edit_drops_the_redo_branch() {
  InputPtr in = fresh();
  type(in.get(), "abc");
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()).empty(), "undo removes the typed run");
  check(rolltui_input_can_redo(in.get()) != 0, "a redo branch is available");
  type(in.get(), "x");  // a fresh edit after an undo
  check(text_of(in.get()) == "x", "typing after an undo replaces the draft");
  check(rolltui_input_can_redo(in.get()) == 0 && rolltui_input_redo(in.get()) == 0,
        "the new edit drops the old redo branch, same as UndoStack::commit()");
}

void test_undo_redo_and_history_never_touch_each_other() {
  InputPtr in = fresh();
  push_history(in.get(), "one");
  push_history(in.get(), "two");
  type(in.get(), "draft");
  handle(in.get(), key(ROLLTUI_KEY_UP));  // recalls "two" (history_prev, built on set_text())
  check(text_of(in.get()) == "two", "history recall");
  check(rolltui_input_can_undo(in.get()) == 0,
        "set_text() (which history recall uses) resets the WHOLE undo stack to a fresh baseline: nothing to undo yet");
  type(in.get(), "X");
  const std::size_t cursor_before = rolltui_input_history_cursor(in.get());
  const std::vector<std::string> hist_before = history_of(in.get());
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()) == "two", "undo reverts the typed X");
  check(rolltui_input_history_cursor(in.get()) == cursor_before && history_of(in.get()) == hist_before,
        "undo never touches the history mechanism (Phase 9 m10, untouched by Phase 12 m1)");
  check(rolltui_input_undo(in.get()) == 0 && text_of(in.get()) == "two",
        "undo cannot reach past the history recall: set_text() is a fresh baseline, not an undoable edit");
  check(rolltui_input_redo(in.get()) != 0 && text_of(in.get()) == "twoX", "redo restores it");
  check(rolltui_input_history_cursor(in.get()) == cursor_before && history_of(in.get()) == hist_before,
        "redo never touches the history mechanism either");
}

void test_undo_group_closes_on_select_all_and_mouse() {
  // select_all() and a mouse press change only the caret/selection, and both close an
  // open group exactly like a keyboard caret move — not just the move_*/set_caret family.
  InputPtr a = fresh();
  type(a.get(), "ab");
  rolltui_input_select_all(a.get());
  type(a.get(), "c");  // replaces the selection: atomic
  const std::vector<std::string> ta = undo_trace(a.get());
  check(ta == std::vector<std::string>{"ab", ""}, "select_all() closes the group " + joined(ta));

  InputPtr m = fresh(20, 1);
  type(m.get(), "hello!");                                                 // one merged group
  handle(m.get(), mouse(RolltuiMouseEvent::Kind::Press, 100, 0), 1000);    // a plain click: caret/selection only
  type(m.get(), "?");                                                      // starts a fresh group
  const std::vector<std::string> tm = undo_trace(m.get());
  check(tm == std::vector<std::string>{"hello!", ""}, "a mouse press closes the group too " + joined(tm));
}

void test_degenerate_sizes() {
  // A window can shrink to 1 or 0 cells in either dimension :
  // every operation still works, nothing is written outside the area, and the text is
  // untouched by the geometry.
  ThemeFixture th;
  builtin_theme_c("default-dark", th);
  for (RolltuiRect a : {RolltuiRect{0, 0, 0, 0}, RolltuiRect{0, 0, 1, 0}, RolltuiRect{0, 0, 0, 1}, RolltuiRect{0, 0, 1, 1}, RolltuiRect{3, 2, 0, 5}, RolltuiRect{3, 2, 5, 0}}) {
    const std::string name = std::to_string(a.w) + "x" + std::to_string(a.h);
    InputPtr in = new_input();
    rolltui_input_layout(in.get(), a);
    type(in.get(), "hello");
    handle(in.get(), key(ROLLTUI_KEY_ENTER, false, false, true));
    handle(in.get(), paste("wor\xE6\xBC\xA2ld"));
    rolltui_input_layout(in.get(), a);
    handle(in.get(), key(ROLLTUI_KEY_UP));
    handle(in.get(), key(ROLLTUI_KEY_DOWN));
    handle(in.get(), key(ROLLTUI_KEY_HOME));
    handle(in.get(), key(ROLLTUI_KEY_LEFT, true));
    handle(in.get(), mouse(RolltuiMouseEvent::Kind::Press, a.x, a.y), 1000);
    handle(in.get(), mouse(RolltuiMouseEvent::Kind::Drag, a.x + 3, a.y + 4), 1100);
    handle(in.get(), mouse(RolltuiMouseEvent::Kind::Release, a.x + 3, a.y + 4), 1200);
    rolltui_input_layout(in.get(), a);
    FrameC f(8, 8);
    rolltui_input_draw(in.get(), f, draw_scratch(), th.styles, &kRoles, true);
    check(text_of(in.get()) == "hello\nwor\xE6\xBC\xA2ld" && rolltui_input_rows(in.get()) >= 2,
          name + ": the text and its rows survive a degenerate area");
    bool outside = false;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
        if (f.glyph(x, y) != " " && !a.contains(x, y)) outside = true;
    check(!outside, name + ": nothing is drawn outside the area");
    check(!f.cursor().visible || a.contains(f.cursor().x, f.cursor().y) || (a.w == 0 && f.cursor().x == a.x) ||
              (a.h == 0 && f.cursor().y == a.y),
          name + ": the cursor is inside the area or on its collapsed edge");
    set_caret(in.get(), 0);
    rolltui_input_layout(in.get(), a);
    check(rolltui_input_top_row(in.get()) == 0, name + ": the scroll follows the caret back to the top");
    // Undo/redo survive a degenerate area too (extended
    // to Phase 12 m1): nothing crashes, and redo restores the paste byte-for-byte.
    const std::string full(text_of(in.get()));
    check(rolltui_input_can_undo(in.get()) != 0, name + ": there is something to undo after editing a degenerate area");
    check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()) != full, name + ": undo runs without crashing in a degenerate area");
    rolltui_input_layout(in.get(), a);
    check(rolltui_input_redo(in.get()) != 0 && text_of(in.get()) == full, name + ": redo restores it byte-for-byte, even at this size");
    rolltui_input_layout(in.get(), a);
  }
  InputPtr w1 = new_input();
  rolltui_input_layout(w1.get(), {0, 0, 1, 3});
  set_text(w1.get(), "\xE6\xBC\xA2\xE6\xBC\xA2");  // two 2-cell glyphs in a 1-cell window
  rolltui_input_layout(w1.get(), {0, 0, 1, 3});
  check(rolltui_input_rows(w1.get()) == 3 && cell_of(w1.get(), 3).row == 1,
        "a 1-cell window still advances one glyph per row (a 2-cell glyph overflows rather than stalling)");
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
  test_undo_redo_grouping_rule();
  test_undo_redo_restores_caret_and_selection_exactly();
  test_undo_redo_new_edit_drops_the_redo_branch();
  test_undo_redo_and_history_never_touch_each_other();
  test_undo_group_closes_on_select_all_and_mouse();
  test_degenerate_sizes();
  return rolltui_test::report("rolltui input_test");
}
