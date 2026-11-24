//
// input_test.cpp — the input widget's state machine (rolltui_widget_input.h), table-tested with
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
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include <filesystem>
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
#include "rolltui/c/rolltui_syntax.h"
#include "rolltui/c/rolltui_widget_input.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_layout_tree.h"   /* INTERNAL: the focused node's id */
#include "rolltui_test.hpp"

using testkit::check;

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

// THE LIBRARY'S THIRTY-SEVEN, not a copy of them. This block used to hold a verbatim
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
    ROLLTUI_ROLE_TEXT_MUTED,
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
// select_all's own chord: ctrl+shift+<letter> cannot be told apart from ctrl+<letter> on a
// legacy terminal (see the default bindings), so the shipped default is alt+shift, the one
// multi-modifier combination the legacy encoding does carry for a letter.
RolltuiEvent alt_shift(char c) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = static_cast<char32_t>(c);
  e.key.alt = true;
  e.key.shift = true;
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

// Up and Down browse every entry, whatever was typed first: what these history tests are about.
void browse_all(RolltuiInput* in) {
  RolltuiInputOptions o;
  rolltui_input_options_copy(&o, rolltui_input_options(in));
  o.history_prefix = 0;
  rolltui_input_set_options(in, &o);
  rolltui_input_options_release(&o);
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
  browse_all(in.get());
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
  browse_all(m.get());
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
  handle(in.get(), alt_shift('a'));
  check(sel_of(in.get()) == "abc" && rolltui_input_caret(in.get()) == 3, "Alt+Shift+A selects all, caret at the end");
  handle(in.get(), key(ROLLTUI_KEY_DELETE));
  check(text_of(in.get()).empty(), "Delete removes the selection");
  set_text(in.get(), "abc");
  handle(in.get(), alt_shift('a'));
  check(handle(in.get(), key(ROLLTUI_KEY_ESCAPE)) == InputAction::Handled && selection_of(in.get()).empty() &&
            text_of(in.get()) == "abc",
        "Escape clears the selection and keeps the text");
  check(handle(in.get(), key(ROLLTUI_KEY_ESCAPE)) == InputAction::Ignored,
        "Escape with no selection is Ignored (the stack closes popups with it)");
  std::string copied;
  CopyCtx ctx{[&](const std::string& s) { copied = s; }};
  rolltui_input_set_copy(in.get(), call_copy, &ctx);
  check(handle(in.get(), alt('c')) == InputAction::Ignored, "Alt+C with no selection is Ignored (the transcript may copy)");
  handle(in.get(), alt_shift('a'));
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

// ONE ROW: a `single_line` input never wraps — the row slides so the caret is in view, and a
// press maps back through the same slide.
void test_one_row() {
  ThemeFixture th;
  builtin_theme_c("default-dark", th);
  InputPtr in = fresh(10, 1);
  RolltuiInputOptions o;
  init_options(o);
  o.single_line = 1;
  o.prompt = "";
  rolltui_input_set_options(in.get(), &o);
  rolltui_input_options_release(&o);
  set_text(in.get(), "abcdefghijklmnop");
  rolltui_input_layout(in.get(), {0, 0, 10, 1});
  check(rolltui_input_rows_for(in.get(), 10) == 1, "a one-row input never wraps: sixteen cells in ten columns are one row");
  FrameC f(10, 1);
  rolltui_input_draw(in.get(), f, draw_scratch(), th.styles, &kRoles, true);
  check(f.glyph(0, 0) == "h" && f.glyph(8, 0) == "p" && f.cursor().visible && f.cursor().x == 9,
        "…drawn slid so the caret is in view: the tail of the text, the cursor after it [" + std::string(f.glyph(0, 0)) + std::string(f.glyph(8, 0)) + "]");
  std::size_t b = 0, e = 0;
  rolltui_input_hit(in.get(), 0, 0, &b, &e);
  check(b == 7, "a press maps through the slide: the first column is the eighth grapheme [" + std::to_string(b) + "]");
  rolltui_input_move_line_start(in.get(), 0);
  FrameC g(10, 1);
  rolltui_input_draw(in.get(), g, draw_scratch(), th.styles, &kRoles, true);
  check(g.glyph(0, 0) == "a" && g.glyph(9, 0) == "j" && g.cursor().x == 0, "…and the caret at the start brings the head back into view");
  rolltui_input_hit(in.get(), 3, 0, &b, &e);
  check(b == 3, "…where a press maps straight");
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

// undo/redo. The grouping rule, stated in rolltui_widget_input.h, as a table driven
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
  browse_all(in.get());
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
        if (f.glyph(x, y) != " " && !contains(a, x, y)) outside = true;
    check(!outside, name + ": nothing is drawn outside the area");
    check(!f.cursor().visible || contains(a, f.cursor().x, f.cursor().y) || (a.w == 0 && f.cursor().x == a.x) ||
              (a.h == 0 && f.cursor().y == a.y),
          name + ": the cursor is inside the area or on its collapsed edge");
    set_caret(in.get(), 0);
    rolltui_input_layout(in.get(), a);
    check(rolltui_input_top_row(in.get()) == 0, name + ": the scroll follows the caret back to the top");
    // Undo/redo survive a degenerate area too: nothing crashes, and redo restores the paste
    // byte-for-byte.
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


// Options built from the defaults, changed by `edit`, applied.
void with_options(RolltuiInput* in, const std::function<void(RolltuiInputOptions&)>& edit) {
  RolltuiInputOptions o;
  rolltui_input_options_copy(&o, rolltui_input_options(in));
  edit(o);
  rolltui_input_set_options(in, &o);
  rolltui_input_options_release(&o);
}
std::string killed_of(const RolltuiInput* in) {
  std::size_t n = 0;
  const char* p = rolltui_input_killed(in, &n);
  return std::string(p ? p : "", n);
}
std::string wrapped_of(RolltuiInput* in, int column) {
  RolltuiStr out;
  rolltui_input_wrapped_text(in, column, &out);
  return std::string(out.data(), out.size());
}
std::string row_text(const FrameC& f, int y, int w) {
  std::string r;
  for (int x = 0; x < w; ++x) r += std::string(f.glyph(x, y));
  while (!r.empty() && r.back() == ' ') r.pop_back();
  return r;
}

void test_kill_and_yank() {
  InputPtr in = fresh();
  type(in.get(), "one two three");
  handle(in.get(), ctrl('w'));
  handle(in.get(), ctrl('w'));
  check(text_of(in.get()) == "one " && killed_of(in.get()) == "two three",
        "two backward kills in a row are one kill: the buffer holds both words, in order");
  handle(in.get(), alt('y'));
  check(text_of(in.get()) == "one two three", "Alt-Y yanks them back at the caret");
  handle(in.get(), key(ROLLTUI_KEY_HOME));
  handle(in.get(), ctrl('k'));
  check(text_of(in.get()).empty() && killed_of(in.get()) == "one two three", "a move ends the run: Ctrl-K starts a new kill");
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()) == "one two three", "a kill undoes as one step");
  set_text(in.get(), "abc def");
  set_caret(in.get(), 0);
  handle(in.get(), alt('d'));
  handle(in.get(), alt('d'));
  check(text_of(in.get()).empty() && killed_of(in.get()) == "abc def", "forward kills add to the end of the run");
  InputPtr none = fresh();
  check(handle(none.get(), alt('y')) == InputAction::Ignored, "a yank with nothing killed is Ignored");
}

void test_transpose() {
  InputPtr in = fresh();
  set_text(in.get(), "abcd");
  set_caret(in.get(), 2);
  handle(in.get(), ctrl('t'));
  check(text_of(in.get()) == "acbd" && rolltui_input_caret(in.get()) == 3, "Ctrl-T swaps the graphemes either side and moves past them");
  set_caret(in.get(), 4);
  handle(in.get(), ctrl('t'));
  check(text_of(in.get()) == "acdb", "at the line's end Ctrl-T swaps the last two");
  set_text(in.get(), std::string("x") + kEacute + "\ny");
  set_caret(in.get(), 4);
  handle(in.get(), ctrl('t'));
  check(text_of(in.get()) == std::string("x") + kEacute + "\ny", "at a line's start it does nothing: lines never trade");
  set_caret(in.get(), 3);
  handle(in.get(), ctrl('t'));
  check(text_of(in.get()) == std::string(kEacute) + "x\ny", "a two-byte grapheme moves whole");
  set_text(in.get(), "first second third");
  set_caret(in.get(), 5);
  handle(in.get(), alt('t'));
  check(text_of(in.get()) == "second first third" && rolltui_input_caret(in.get()) == 12,
        "Alt-T swaps the word before the caret with the next, the caret after both");
  set_caret(in.get(), 0);
  handle(in.get(), alt('t'));
  check(text_of(in.get()) == "first second third", "at the first word it swaps with the next");
  set_caret(in.get(), 18);
  handle(in.get(), alt('t'));
  check(text_of(in.get()) == "first third second", "at the end it swaps the last two");
}

void test_word_case() {
  InputPtr in = fresh();
  set_text(in.get(), "hello world");
  set_caret(in.get(), 0);
  handle(in.get(), alt('u'));
  check(text_of(in.get()) == "HELLO world" && rolltui_input_caret(in.get()) == 5, "Alt-U uppercases to the word's end");
  handle(in.get(), alt_shift('c'));
  check(text_of(in.get()) == "HELLO World", "Alt-Shift-C capitalises the next word");
  set_caret(in.get(), 0);
  handle(in.get(), alt('l'));
  check(text_of(in.get()) == "hello World", "Alt-L lowercases");
  set_text(in.get(), "\xC3\xA9lan \xCF\x83\xCE\xBF\xCF\x86\xCE\xAF\xCE\xB1");  // élan σοφία
  rolltui_input_select_all(in.get());
  handle(in.get(), alt('u'));
  check(text_of(in.get()) == "\xC3\x89LAN \xCE\xA3\xCE\x9F\xCE\xA6\xCE\x8A\xCE\x91" && sel_of(in.get()) == std::string(text_of(in.get())),
        "with a selection the whole selection is uppercased, by Unicode's mappings, and stays selected");
  RolltuiUnicodeScratch* u = rolltui_u_scratch_new();
  RolltuiStr folded;
  rolltui_u_case(u, "Stra\xC3\x9F" "E \xE1\xBA\x9E", 11, ROLLTUI_CASE_FOLD, &folded);
  check(std::string(folded.data(), folded.size()) == "stra\xC3\x9F" "e \xC3\x9F", "FOLD maps capital sharp s to sharp s (simple folding)");
  rolltui_u_case(u, "\xFF" "Ab", 3, ROLLTUI_CASE_LOWER, &folded);
  check(std::string(folded.data(), folded.size()) == "\xFF" "ab", "a byte that is not UTF-8 passes through");
  rolltui_u_case(u, "the QUICK fox", 13, ROLLTUI_CASE_TITLE, &folded);
  check(std::string(folded.data(), folded.size()) == "The Quick Fox", "TITLE titles each word and lowers the rest");
  rolltui_u_scratch_free(u);
}

void test_overwrite() {
  InputPtr in = fresh();
  set_text(in.get(), "abc\nd");
  set_caret(in.get(), 1);
  handle(in.get(), key(ROLLTUI_KEY_INSERT));
  check(rolltui_input_overwrite(in.get()) != 0, "Insert turns overwrite on");
  type(in.get(), "XYZ");
  check(text_of(in.get()) == "aXYZ\nd", "typing replaces what is under the caret, and inserts at the line's end, never eating the newline");
  check(rolltui_input_undo(in.get()) != 0 && text_of(in.get()) == "abc\nd", "the run undoes as one step");
  handle(in.get(), key(ROLLTUI_KEY_INSERT));
  set_caret(in.get(), 1);
  type(in.get(), "Q");
  check(text_of(in.get()) == "aQbc\nd", "Insert again: typing inserts");
}

void test_secret() {
  ThemeFixture th;
  builtin_theme_c("default-dark", th);
  InputPtr in = fresh(20, 1);
  with_options(in.get(), [](RolltuiInputOptions& o) { o.secret = ROLLTUI_INPUT_SHOW_MASKED; });
  std::string copied;
  rolltui_input_set_copy(in.get(), call_copy, &copied);
  type(in.get(), "pa ss");
  rolltui_input_layout(in.get(), {0, 0, 20, 1});
  FrameC f(20, 1);
  rolltui_input_draw(in.get(), f, draw_scratch(), th.styles, &kRoles, true);
  check(f.glyph(2, 0) == "\xE2\x80\xA2" && f.glyph(4, 0) == "\xE2\x80\xA2" && f.glyph(6, 0) == "\xE2\x80\xA2" &&
            f.cursor().x == 7,
        "masked: one bullet a grapheme, the space too, and the caret after them");
  rolltui_input_select_all(in.get());
  check(handle(in.get(), alt('c')) == InputAction::Ignored && copied.empty(), "a secret is never copied");
  handle(in.get(), key(ROLLTUI_KEY_END));
  handle(in.get(), key(ROLLTUI_KEY_LEFT, false, true));
  check(rolltui_input_caret(in.get()) == 0, "a word motion moves by the whole line: where the space is is not spelled out");
  rolltui_input_push_history(in.get(), "pa ss", 5);
  check(rolltui_input_history_count(in.get()) == 0, "a secret is never kept in the history");
  handle(in.get(), ctrl('w'));
  check(killed_of(in.get()).empty(), "…nor in the kill buffer");
  InputPtr hid = fresh(20, 1);
  with_options(hid.get(), [](RolltuiInputOptions& o) { o.secret = ROLLTUI_INPUT_SHOW_NOTHING; });
  type(hid.get(), "secret");
  rolltui_input_layout(hid.get(), {0, 0, 20, 1});
  FrameC g(20, 1);
  rolltui_input_draw(hid.get(), g, draw_scratch(), th.styles, &kRoles, true);
  check(g.glyph(2, 0) == " " && g.cursor().x == 2 && text_of(hid.get()) == "secret",
        "nothing shown: the text is kept, nothing is drawn, the caret stays after the prompt");
}

void test_submit_rules_and_modes() {
  InputPtr in = fresh();
  with_options(in.get(), [](RolltuiInputOptions& o) { o.refuse_empty = 1; });
  type(in.get(), " \t ");
  check(handle(in.get(), key(ROLLTUI_KEY_ENTER)) == InputAction::Handled, "refuse_empty: Enter on blanks submits nothing");
  type(in.get(), "x");
  check(handle(in.get(), key(ROLLTUI_KEY_ENTER)) == InputAction::Submit, "…and submits once there is text");
  InputPtr nm = fresh();
  with_options(nm.get(), [](RolltuiInputOptions& o) { o.insert_typed = 0; });
  set_text(nm.get(), "abc");
  check(handle(nm.get(), chr('x')) == InputAction::Ignored && text_of(nm.get()) == "abc",
        "insert_typed off: an unbound printable key types nothing");
  check(rolltui_input_perform(nm.get(), rolltui_input_default_actions(), "input.line_start", 16, 0) == ROLLTUI_INPUT_HANDLED &&
            rolltui_input_caret(nm.get()) == 0,
        "rolltui_input_perform runs a command by its action name");
  check(rolltui_input_perform(nm.get(), rolltui_input_default_actions(), "input.nope", 10, 0) == ROLLTUI_INPUT_IGNORED,
        "…and a name that is none of them is Ignored");
  InputPtr ai = fresh(80, 4);
  with_options(ai.get(), [](RolltuiInputOptions& o) { o.auto_indent = 1; });
  type(ai.get(), "  if (x) {");
  handle(ai.get(), key(ROLLTUI_KEY_ENTER, false, false, true));
  check(text_of(ai.get()) == "  if (x) {\n  ", "auto_indent: a newline carries the line's own indent");
}

void test_history_by_prefix() {
  InputPtr in = fresh();
  for (const char* e : {"git status", "ls -la", "git log", "make"}) push_history(in.get(), e);
  type(in.get(), "git");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "git log", "Up finds the newest entry beginning with what was typed");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "git status", "…then the one before, skipping what does not match");
  check(handle(in.get(), key(ROLLTUI_KEY_UP)) == InputAction::Handled && text_of(in.get()) == "git status",
        "…and stays at the oldest match");
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(text_of(in.get()) == "git", "Down past the newest match restores what was typed");
  set_caret(in.get(), 0);
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "make", "with the caret at the start every entry is a match");
}

void test_history_file() {
  char tmpl[] = "/tmp/rolltui_input_hist_XXXXXX";
  const char* dir = mkdtemp(tmpl);
  check(dir != nullptr, "a scratch directory");
  if (!dir) return;
  const std::string path = std::string(dir) + "/sub/history.json";
  InputPtr a = fresh(), b = fresh();
  check(rolltui_input_history_load(a.get(), path.data(), path.size(), nullptr) && rolltui_input_history_count(a.get()) == 0,
        "a missing file loads as an empty history");
  rolltui_input_history_load(b.get(), path.data(), path.size(), nullptr);
  push_history(a.get(), "from a");
  push_history(b.get(), "from b\nwith a second line");
  check(rolltui_input_history_save(a.get(), path.data(), path.size(), nullptr) != 0, "a saves (making the folder)");
  check(rolltui_input_history_save(b.get(), path.data(), path.size(), nullptr) != 0, "b saves after it");
  InputPtr c = fresh();
  rolltui_input_history_load(c.get(), path.data(), path.size(), nullptr);
  check(history_of(c.get()) == std::vector<std::string>{"from a", "from b\nwith a second line"},
        "the file keeps both instances' entries, a's then b's, a newline inside an entry intact");
  check(history_of(b.get()) == history_of(c.get()), "…and b now holds what the file holds");
  RolltuiInputOptions o;
  init_options(o);
  o.history_limit = 1;
  rolltui_input_set_options(c.get(), &o);
  rolltui_input_options_release(&o);
  push_history(c.get(), "newest");
  rolltui_input_history_save(c.get(), path.data(), path.size(), nullptr);
  InputPtr d = fresh();
  rolltui_input_history_load(d.get(), path.data(), path.size(), nullptr);
  check(history_of(d.get()) == std::vector<std::string>{"newest"}, "a save keeps at most history_limit, the newest");
  const std::string bad = std::string(dir) + "/bad.json";
  std::FILE* fp = std::fopen(bad.c_str(), "wb");
  std::fputs("{\"not\": \"an array\"}", fp);
  std::fclose(fp);
  RolltuiStr err;
  check(!rolltui_input_history_load(d.get(), bad.data(), bad.size(), &err) && std::string(err.data(), err.size()).find("not a history file") != std::string::npos,
        "a file that is not a history is refused, and says so");
  std::remove(bad.c_str());
  std::remove(path.c_str());
  rmdir((std::string(dir) + "/sub").c_str());
  rmdir(dir);
}

void test_word_wrap_and_wrapped_text() {
  InputPtr in = fresh(12, 4);
  with_options(in.get(), [](RolltuiInputOptions& o) { o.wrap_words = 1; });
  type(in.get(), "hello world again");
  rolltui_input_layout(in.get(), {0, 0, 12, 4});
  check(rolltui_input_rows(in.get()) == 3 && cell_of(in.get(), 6).row == 1 && cell_of(in.get(), 6).col == 2 &&
            cell_of(in.get(), 12).row == 2,
        "wrap_words: a word that would not fit starts the next row whole");
  check(wrapped_of(in.get(), 0) == "hello\nworld\nagain", "the wrapped text has a newline at each soft break, the space before it dropped");
  check(wrapped_of(in.get(), 40) == "hello world again", "…and at a wider column, none");
  check(rolltui_input_rows(in.get()) == 3, "asking for the wrapped text leaves the drawn flow as it was");
  InputPtr col = fresh(80, 4);
  with_options(col.get(), [](RolltuiInputOptions& o) {
    o.wrap_words = 1;
    o.wrap_column = 10;
  });
  type(col.get(), "hello world again");
  rolltui_input_layout(col.get(), {0, 0, 80, 4});
  check(rolltui_input_rows(col.get()) == 3, "wrap_column: rows wrap at ten text columns in an 80-wide area");
  InputPtr longw = fresh(8, 4);
  with_options(longw.get(), [](RolltuiInputOptions& o) { o.wrap_words = 1; });
  type(longw.get(), "abcdefghij");
  rolltui_input_layout(longw.get(), {0, 0, 8, 4});
  check(rolltui_input_rows(longw.get()) == 2 && cell_of(longw.get(), 6).row == 1, "a word longer than the row is still broken at the edge");
}

void test_line_numbers_and_highlighting() {
  ThemeFixture th;
  builtin_theme_c("default-dark", th);
  InputPtr in = fresh(30, 12);
  with_options(in.get(), [](RolltuiInputOptions& o) {
    o.line_numbers = 1;
    o.prompt = "";
    o.language = "c";
  });
  std::string text;
  for (int i = 0; i < 10; ++i) text += "int x" + std::to_string(i) + ";\n";
  text += "return 0;";
  set_text(in.get(), text);
  RolltuiSyntax* syn = rolltui_syntax_new_standard(nullptr);
  rolltui_input_set_syntax(in.get(), syn);
  rolltui_input_layout(in.get(), {0, 0, 30, 12});
  FrameC f(30, 12);
  rolltui_input_draw(in.get(), f, draw_scratch(), th.styles, &kRoles, true);
  check(row_text(f, 0, 30) == " 1 int x0;" && row_text(f, 9, 30) == "10 int x9;" && row_text(f, 10, 30) == "11 return 0;",
        "line_numbers: each line's number, right-aligned in a gutter as wide as the widest [" + row_text(f, 0, 30) + "]");
  check(f.at(1, 0).style == th.style(ROLLTUI_ROLE_TEXT_MUTED), "…drawn in the line-number role");
  check(f.at(3, 0).style != th.style(ROLLTUI_ROLE_INPUT_TEXT) && f.at(7, 0).style == th.style(ROLLTUI_ROLE_INPUT_TEXT),
        "language c: the keyword is highlighted, the name after it is plain text");
  check(cell_of(in.get(), 0).col == 3, "the caret's column counts the gutter");
  rolltui_input_set_syntax(in.get(), nullptr);
  rolltui_syntax_free(syn);
}

}  // namespace

// ---- VIM'S MODES: with the vim preset, Escape leaves typing for normal mode --------------------------------------------------
const RolltuiBindings* vim_keys() { return rolltui_bindings_shipped(rolltui_test::test_context(), "vim", 3); }
InputAction vhandle(RolltuiInput* in, const RolltuiEvent& e) {
  return static_cast<InputAction>(rolltui_input_handle(in, &e, vim_keys(), &kActions_ref(), 0));
}
void vkeys(RolltuiInput* in, std::string_view s) {
  for (char c : s) vhandle(in, chr(static_cast<unsigned char>(c)));
}
InputAction vmodal(RolltuiInput* in, const char* action) { return static_cast<InputAction>(rolltui_input_perform_modal(in, action, std::strlen(action))); }
int caret_shape(RolltuiInput* in) {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiFrame* f = rolltui_frame_new(40, 1, styles[0]);
  rolltui_input_draw(in, f, draw_scratch(), styles, &kRoles, 1);
  const int shape = rolltui_frame_cursor_shape(f);
  rolltui_frame_free(f);
  return shape;
}

void test_vim_modes() {
  check(vim_keys() != nullptr, "the vim preset is shipped");
  {
    InputPtr in = fresh();
    set_text(in.get(), "hello world");
    handle(in.get(), key(ROLLTUI_KEY_ESCAPE));
    check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_INSERT, "with the default keys Escape leaves nothing: there are no modes");
  }
  InputPtr in = fresh();
  set_text(in.get(), "hello world");
  check(caret_shape(in.get()) == ROLLTUI_CARET_BAR, "typing: a bar");
  vhandle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_NORMAL && rolltui_input_caret(in.get()) == 10,
        "Escape: normal mode, the caret back onto the last character [" + std::to_string(rolltui_input_caret(in.get())) + "]");
  check(caret_shape(in.get()) == ROLLTUI_CARET_BLOCK, "…a block");
  vkeys(in.get(), "z");
  check(text_of(in.get()) == "hello world", "a key normal mode does not bind types nothing");
  vkeys(in.get(), "x");
  check(text_of(in.get()) == "hello worl" && rolltui_input_caret(in.get()) == 9, "x deletes under the caret, which stays on a character");
  vkeys(in.get(), "0");
  check(rolltui_input_caret(in.get()) == 0, "0: the line's start");
  vkeys(in.get(), "w");
  check(rolltui_input_caret(in.get()) == 6, "w: the start of the next word");
  vkeys(in.get(), "e");
  check(rolltui_input_caret(in.get()) == 9, "e: the end of the word");
  vkeys(in.get(), "b");
  check(rolltui_input_caret(in.get()) == 6, "b: back to its start");
  vkeys(in.get(), "i");
  check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_INSERT, "i: typing again");
  vkeys(in.get(), "big ");
  vhandle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(text_of(in.get()) == "hello big worl" && rolltui_input_caret(in.get()) == 9, "typed before the caret; Escape steps back onto it");
  vkeys(in.get(), "A");
  vkeys(in.get(), "d");
  vhandle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(text_of(in.get()) == "hello big world", "A: typing at the line's end");
  vkeys(in.get(), "0");
  check(vmodal(in.get(), "normal.delete_word") == InputAction::Handled && text_of(in.get()) == "big world",
        "`d w` deletes to the next word's start [" + std::string(text_of(in.get())) + "]");
  vkeys(in.get(), "P");
  check(text_of(in.get()) == "hello big world", "P puts back what was deleted, before the caret");
  vkeys(in.get(), "u");
  check(text_of(in.get()) == "big world", "u undoes");
  RolltuiEvent ctrl_r = chr('r');
  ctrl_r.key.ctrl = 1;
  vhandle(in.get(), ctrl_r);
  check(text_of(in.get()) == "hello big world", "Ctrl-R does it again");
  vkeys(in.get(), "$");
  check(rolltui_input_caret(in.get()) == 14, "$: the last character");
  vkeys(in.get(), "rD");
  check(text_of(in.get()) == "hello big worlD" && rolltui_input_mode(in.get()) == ROLLTUI_INPUT_NORMAL, "r and a character replaces the one under the caret");
  vkeys(in.get(), "0R");
  check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_REPLACE && caret_shape(in.get()) == ROLLTUI_CARET_UNDERLINE, "R: replace mode, an underline");
  vkeys(in.get(), "J");
  vhandle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(text_of(in.get()) == "Jello big worlD", "…typing over what is there");
  vkeys(in.get(), "wv");
  check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_VISUAL && sel_of(in.get()) == "b", "v: visual, the character under the caret selected");
  vkeys(in.get(), "e");
  check(sel_of(in.get()) == "big", "…a motion grows it, both ends included");
  vkeys(in.get(), "y");
  check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_NORMAL && rolltui_input_caret(in.get()) == 6, "y copies it and goes back to normal, the caret at its start");
  vkeys(in.get(), "$p");
  check(text_of(in.get()) == "Jello big worlDbig", "p puts it after the caret");
  vkeys(in.get(), "0vlld");
  check(text_of(in.get()) == "lo big worlDbig", "visual d deletes the selection");
  vkeys(in.get(), "vl");
  vhandle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(rolltui_input_mode(in.get()) == ROLLTUI_INPUT_NORMAL && sel_of(in.get()).empty(), "Escape in visual: back to normal, nothing selected");
  vkeys(in.get(), "0C");
  check(text_of(in.get()).empty() && rolltui_input_mode(in.get()) == ROLLTUI_INPUT_INSERT, "C: deletes to the end and types");
  set_text(in.get(), "one\ntwo\nthree");
  set_caret(in.get(), 5);
  vhandle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(vmodal(in.get(), "normal.delete_line") == InputAction::Handled && text_of(in.get()) == "one\nthree", "`d d` deletes the line and its newline");
  vkeys(in.get(), "i");
  check(vmodal(in.get(), "normal.delete_line") == InputAction::Ignored && text_of(in.get()) == "one\nthree", "a normal command while typing is nothing");
}

// …AND IN A WINDOW, as a host has it: a sequence (`d w`) and a count (`2 w`) resolve in normal mode's scope, Escape is typing's to
// leave and normal mode's to give up, so a second one closes the popup the field is in, and the bottom border names the mode.
void test_vim_modes_in_a_window() {
  RolltuiContext* c = rolltui_context_new();
  rolltui_context_set_library_defaults(c);
  rolltui_context_set_bindings(c, rolltui_bindings_shipped(c, "vim", 3));
  constexpr std::string_view screen = R"({ "resizable": false, "name": "v", "root": { "content": "text:the app" },
    "popups": [ { "id": "ask", "x": "50%", "y": 2, "w": 30, "h": 3, "anchor": "top",
                  "root": { "id": "field", "content": "input:q", "border": "rounded", "focusable": true } } ] })";
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(c, &nd);
  RolltuiLayout* l = rolltui_load_layout_text(screen.data(), screen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(c);
  RolltuiWindowStack* st = rolltui_window_stack_new();
  rolltui_window_stack_set_base(st, rolltui_layout_base(l));
  rolltui_window_stack_set_level_fn(st, rolltui_windows_back, w); /* Escape closes one level, as every host wires it */
  rolltui_window_stack_push_popup(st, l, "ask", 3);
  rolltui_window_stack_focus(st, "field", 5);
  rolltui_windows_bind_submit(w, "q", 1, [](void*, const char*, std::size_t) {}, nullptr, nullptr, 1); /* a field with somewhere to go */
  rolltui_windows_sync(w, st);
  RolltuiInput* in = rolltui_windows_input(w, "q", 1);
  rolltui_input_set_text(in, "one two three", 13);
  auto send = [&](const RolltuiEvent& e) {
    RolltuiStr window{};
    const unsigned char kind = rolltui_window_stack_route(st, &e, RolltuiRect{0, 0, 40, 10}, rolltui_bindings_shipped(c, "vim", 3),
                                                          rolltui_stack_default_actions(), &window);
    if (kind == ROLLTUI_ROUTE_DELIVER) rolltui_windows_handle(w, window.p ? window.p : "", window.n, &e);
    rolltui_str_free(&window);
    rolltui_windows_sync(w, st);
    return kind;
  };
  auto keys = [&](std::string_view k) {
    for (char ch : k) send(chr(static_cast<unsigned char>(ch)));
  };
  auto footer = [&]() {
    RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
    rolltui_windows_autosize(w, st, RolltuiRect{0, 0, 40, 10});
    rolltui_windows_layout(w, st, RolltuiRect{0, 0, 40, 10});
    RolltuiFrame* f = rolltui_frame_new(40, 10, styles[0]);
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    struct Slot { RolltuiWindows* w; RolltuiStyle* styles; } slot{w, styles};
    rolltui_window_stack_compose(
        st, f, RolltuiRect{0, 0, 40, 10}, styles, rolltui_layout_default_roles(),
        [](void* cx, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* sl = static_cast<Slot*>(cx);
          rolltui_windows_draw(sl->w, rn, fr, sl->styles, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    const std::string all = str_of(text);
    rolltui_str_free(&text);
    rolltui_compose_scratch_free(cs);
    rolltui_frame_free(f);
    return all;
  };
  check(send(key(ROLLTUI_KEY_ESCAPE)) == ROLLTUI_ROUTE_CLOSED_LEVEL && rolltui_input_mode(in) == ROLLTUI_INPUT_NORMAL &&
            rolltui_window_stack_depth(st) == 2,
        "Escape while typing in a popup closes a level: normal mode, the popup still open");
  check(footer().find("NORMAL") != std::string::npos, "…the bottom border says NORMAL");
  keys("0dw");
  check(text_of(in) == "two three", "`d w` resolves as a sequence in normal mode's scope [" + std::string(text_of(in)) + "]");
  keys("02w");
  check(rolltui_input_caret(in) == 8, "…the caret two words on [" + std::to_string(rolltui_input_caret(in)) + "]");
  keys("q");
  check(text_of(in) == "two three" && rolltui_window_stack_depth(st) == 2, "a key normal mode does not bind is the field's, and reaches nothing else");
  const unsigned char kind = send(key(ROLLTUI_KEY_ESCAPE));
  check(kind == ROLLTUI_ROUTE_CLOSED_POPUP && rolltui_window_stack_depth(st) == 1, "a second Escape is not the field's: the popup closes");
  rolltui_windows_free(w);
  rolltui_window_stack_free(st);
  rolltui_layout_free(l);
  rolltui_context_free(c);
}


// ---- completion ---------------------------------------------------------------------------

// A completer over a fixed word list: the word before the caret, fuzzily.
void complete_words(void* ctx, const char* text, std::size_t, std::size_t caret, RolltuiCompletions* out) {
  const auto* words = static_cast<const std::vector<const char*>*>(ctx);
  std::size_t b = caret;
  while (b > 0 && text[b - 1] != ' ') --b;
  rolltui_complete_words(out, text + b, caret - b, words->data(), words->size());
}

std::string suggestion_of(RolltuiInput* in) {
  std::size_t n = 0;
  const char* p = rolltui_input_suggestion(in, &n);
  return std::string(p, n);
}

std::vector<std::string> candidates_of(RolltuiInput* in) {
  std::vector<std::string> v;
  std::size_t n = 0;
  for (std::size_t i = 0; const char* p = rolltui_input_completion_at(in, i, &n); ++i) v.emplace_back(p, n);
  return v;
}

void test_completion() {
  static const std::vector<const char*> kWords = {"help", "hello", "history", "theme", "themes"};
  InputPtr in = fresh(40, 1);
  check(handle(in.get(), key(ROLLTUI_KEY_TAB)) == InputAction::Ignored, "with no completer the field does not take Tab");
  rolltui_input_set_completer(in.get(), complete_words, const_cast<std::vector<const char*>*>(&kWords));

  set_text(in.get(), "his");
  check(suggestion_of(in.get()) == "tory", "the rest of the one candidate the word begins is suggested [" + suggestion_of(in.get()) + "]");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(in.get()) == "history" && rolltui_input_caret(in.get()) == 7, "Tab takes the only candidate the word begins");
  check(handle(in.get(), ctrl('z')) == InputAction::Handled && text_of(in.get()) == "his", "…and one undo puts the word back");
  {
    InputPtr typed = fresh(40, 1);
    rolltui_input_set_completer(typed.get(), complete_words, const_cast<std::vector<const char*>*>(&kWords));
    for (char ch : std::string("his")) handle(typed.get(), chr(static_cast<unsigned char>(ch)));
    handle(typed.get(), key(ROLLTUI_KEY_TAB));
    handle(typed.get(), ctrl('z'));
    const std::string one(text_of(typed.get()));
    handle(typed.get(), ctrl('z'));
    check(one == "his" && text_of(typed.get()).empty(),
          "typed and then completed, the completion undoes first and the typing after it [" + one + "]");
  }

  set_text(in.get(), "he");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(in.get()) == "hel", "Tab takes what several candidates agree on past the word [" + std::string(text_of(in.get())) + "]");
  check(rolltui_input_completion_open(in.get(), nullptr) == 0, "…and leaves the list closed");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  std::size_t chosen = 9;
  check(rolltui_input_completion_open(in.get(), &chosen) == 2 && chosen == 0 && text_of(in.get()) == "help",
        "the next Tab opens the list with the first candidate in the word's place");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(in.get()) == "hello", "Tab steps to the next candidate in place");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(in.get()) == "help", "…and wraps round");
  handle(in.get(), key(ROLLTUI_KEY_TAB, /*shift=*/true));
  check(text_of(in.get()) == "hello", "Shift-Tab steps back");
  handle(in.get(), key(ROLLTUI_KEY_UP));
  check(text_of(in.get()) == "help", "Up steps back too while the list is open");
  handle(in.get(), key(ROLLTUI_KEY_ESCAPE));
  check(text_of(in.get()) == "hel" && rolltui_input_completion_open(in.get(), nullptr) == 0, "Escape puts the word back and closes the list");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  handle(in.get(), key(ROLLTUI_KEY_DOWN));
  check(handle(in.get(), key(ROLLTUI_KEY_ENTER)) == InputAction::Handled && text_of(in.get()) == "hello" &&
            rolltui_input_completion_open(in.get(), nullptr) == 0,
        "Enter keeps the one shown and does not submit");
  set_text(in.get(), "hel");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  handle(in.get(), chr('!'));
  check(text_of(in.get()) == "help!" && rolltui_input_completion_open(in.get(), nullptr) == 0,
        "typing closes the list, keeping the candidate shown, and types [" + std::string(text_of(in.get())) + "]");

  set_text(in.get(), "th");
  check(suggestion_of(in.get()) == "eme", "the suggestion is the first candidate's rest [" + suggestion_of(in.get()) + "]");
  handle(in.get(), key(ROLLTUI_KEY_RIGHT));
  check(text_of(in.get()) == "theme", "Right at the end of the text takes the suggestion");
  set_text(in.get(), "hist");
  handle(in.get(), ctrl('f'));
  check(text_of(in.get()) == "history", "Ctrl-F takes it too");
  set_text(in.get(), "hist");
  set_caret(in.get(), 2);
  check(suggestion_of(in.get()).empty(), "no suggestion with the caret inside the text");
  set_caret(in.get(), 4);
  check(suggestion_of(in.get()) == "ory", "CONTROL: back at the end, it is suggested again");
  rolltui_input_select_all(in.get());
  check(suggestion_of(in.get()).empty(), "…but not once the text is selected, the caret still at its end (an address bar reached)");
  set_text(in.get(), "x ");
  check(suggestion_of(in.get()).empty(), "…nor for an empty word");
  set_text(in.get(), "hmm");
  check(candidates_of(in.get()).empty() && handle(in.get(), key(ROLLTUI_KEY_TAB)) == InputAction::Ignored,
        "a word nothing matches has no candidates, and Tab is not the field's");
  set_text(in.get(), "hst");
  check(candidates_of(in.get()) == std::vector<std::string>{"history"} && suggestion_of(in.get()).empty(),
        "a fuzzy match is a candidate, but is never suggested: the word does not begin it");

  // THE SUGGESTION IS DRAWN after the caret, faded and in italic, and only in the focused field.
  ThemeFixture th;
  builtin_theme_c("default-dark", th);
  set_text(in.get(), "his");
  rolltui_input_layout(in.get(), {0, 0, 40, 1});
  FrameC f(40, 1);
  rolltui_input_draw(in.get(), f, draw_scratch(), th.styles, &kRoles, true);
  check(f.glyph(5, 0) == "t" && f.at(5, 0).style.italic && f.at(5, 0).style.fg == th.style(ROLLTUI_ROLE_INPUT_PLACEHOLDER).fg &&
            f.cursor().x == 5,
        "the suggestion is drawn after the caret, in the placeholder's colour and italic");
  FrameC g(40, 1);
  rolltui_input_draw(in.get(), g, draw_scratch(), th.styles, &kRoles, false);
  check(g.glyph(5, 0) == " ", "…and not in a field without the focus");
  // THE LIST, drawn over the screen under the word
  set_text(in.get(), "hel");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  rolltui_input_layout(in.get(), {0, 0, 40, 1});
  FrameC s(40, 8);
  const int rows = rolltui_input_draw_completions(in.get(), s, draw_scratch(), th.styles, RolltuiRect{0, 0, 40, 8});
  RolltuiStr t{};
  rolltui_frame_to_text(s, &t);
  const std::string shown = str_of(t);
  rolltui_str_free(&t);
  check(rows == 4 && shown.find("1 of 2") != std::string::npos && shown.find("hello") != std::string::npos,
        "the open list is drawn below the field: a title with the count and a row for each candidate");
  int hx = -1, hy = -1;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x + 4 < 40; ++x)
      if (s.glyph(x, y) == "h" && s.glyph(x + 1, y) == "e" && s.glyph(x + 2, y) == "l" && s.glyph(x + 3, y) == "p" && s.glyph(x + 4, y) == " ")
        hx = x, hy = y;
  check(hy > 0 && hx == 3 && s.at(hx, hy).style == th.style(ROLLTUI_ROLE_MENU_SELECTED),
        "…the one shown in the menu's selected role, under the word [" + std::to_string(hx) + "," + std::to_string(hy) + "]");
}

// EVERY SHIPPED THEME, both modes: the list's chosen row stands apart from the others, and the suggestion from the text typed.
void test_completion_in_every_theme() {
  namespace fs = std::filesystem;
  static const std::vector<const char*> kWords = {"help", "hello"};
  std::size_t scanned = 0;
  std::string bad;
  for (const fs::directory_entry& de : fs::directory_iterator(ROLLTUI_PRESETS_DIR)) {
    if (de.path().extension() != ".json") continue;
    std::FILE* fp = std::fopen(de.path().string().c_str(), "rb");
    if (!fp) continue;
    std::string text;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) text.append(buf, n);
    std::fclose(fp);
    RolltuiJsonValue* root = rolltui_json_parse(text.data(), text.size(), nullptr);
    if (!root) continue;
    for (int mode : {ROLLTUI_MODE_DARK, ROLLTUI_MODE_LIGHT}) {
      RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
      RolltuiStr name{};
      RolltuiThemeReport rep{};
      const RolltuiJsonValue* colours = rolltui_json_get(root, "colours", 7); /* a preset file holds its theme there */
      RolltuiEffectMap* eff = rolltui_theme_load(colours ? colours : root, mode, rolltui_theme_default_vocab(), styles, &name, &rep);
      if (rep.error.n) bad += " " + de.path().stem().string() + " did not load;";
      ++scanned;
      InputPtr in = fresh(40, 1);
      rolltui_input_set_completer(in.get(), complete_words, const_cast<std::vector<const char*>*>(&kWords));
      set_text(in.get(), "hel");
      rolltui_input_layout(in.get(), {0, 0, 40, 1});
      FrameC f(40, 1);
      rolltui_input_draw(in.get(), f, draw_scratch(), styles, &kRoles, true);
      const std::string label = de.path().stem().string() + (mode == ROLLTUI_MODE_DARK ? " dark" : " light");
      if (f.at(5, 0).style == f.at(4, 0).style) bad += " " + label + ": the suggestion looks typed;";
      handle(in.get(), key(ROLLTUI_KEY_TAB));
      rolltui_input_layout(in.get(), {0, 0, 40, 1});
      FrameC l(40, 6);
      rolltui_input_draw_completions(in.get(), l, draw_scratch(), styles, RolltuiRect{0, 0, 40, 6});
      // the chosen row (help, the first) and the other (hello), a letter of each
      if (l.at(3, 2).style == l.at(3, 3).style) bad += " " + label + ": the chosen row looks like the others;";
      rolltui_effect_map_free(eff);
      rolltui_theme_report_release(&rep);
      rolltui_str_free(&name);
    }
    rolltui_json_free(root);
  }
  check(scanned >= 16, "scanned every shipped theme in both modes (" + std::to_string(scanned) + ")");
  check(bad.empty(), "in every one the suggestion and the list's chosen row are told apart [" + bad + "]");
}

void test_path_completion() {
  char tmpl[] = "/tmp/rolltui_input_paths_XXXXXX";
  const char* made = mkdtemp(tmpl);
  check(made != nullptr, "a scratch directory");
  if (!made) return;
  const std::string dir = made;
  struct RemoveAtEnd {
    std::string dir;
    ~RemoveAtEnd() {
      for (int i = 0; i < 24; ++i) ::unlink((dir + "/many/n" + std::to_string(100 + i)).c_str());
      ::rmdir((dir + "/many").c_str());
      for (const char* n : {"/alpha_one", "/alpha_two"}) ::rmdir((dir + n).c_str());
      for (const char* n : {"/beta.txt", "/.hidden"}) ::unlink((dir + n).c_str());
      ::rmdir(dir.c_str());
    }
  } remove_at_end{dir};
  ::mkdir((dir + "/alpha_one").c_str(), 0700);
  ::mkdir((dir + "/alpha_two").c_str(), 0700);
  for (const char* n : {"/beta.txt", "/.hidden"}) std::fclose(std::fopen((dir + n).c_str(), "w"));
  struct Ctx { std::string base; } rel{dir};
  InputPtr in = fresh(80, 1);
  // The whole line is one path, as dirktui's path line is.
  rolltui_input_set_completer(in.get(), [](void*, const char* t, std::size_t, std::size_t caret, RolltuiCompletions* out) {
    rolltui_completions_replace(out, 0, caret);
    rolltui_complete_path(out, t, caret, nullptr, 0);
  }, nullptr);
  set_text(in.get(), dir + "/al");
  check(candidates_of(in.get()) == std::vector<std::string>{dir + "/alpha_one/", dir + "/alpha_two/"},
        "a folder's names matching the last part, in name order, each folder with a slash");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(in.get()) == dir + "/alpha_", "Tab takes what they agree on");
  set_text(in.get(), dir + "/b");
  handle(in.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(in.get()) == dir + "/beta.txt", "a file is taken whole, with no slash");
  set_text(in.get(), dir + "/");
  const auto all = candidates_of(in.get());
  check(all.size() == 3 && suggestion_of(in.get()).empty(), "a hidden name is left out, and a bare folder suggests nothing");
  set_text(in.get(), dir + "/.h");
  check(candidates_of(in.get()) == std::vector<std::string>{dir + "/.hidden"}, "…unless the word starts with a dot");
  // A RELATIVE PATH from the folder the host names.
  InputPtr r = fresh(80, 1);
  rolltui_input_set_completer(r.get(), [](void* c, const char* t, std::size_t, std::size_t caret, RolltuiCompletions* out) {
    const std::string& base = static_cast<Ctx*>(c)->base;
    std::size_t b = caret;
    while (b > 0 && t[b - 1] != ' ') --b;
    rolltui_complete_path(out, t + b, caret - b, base.data(), base.size());
  }, &rel);
  set_text(r.get(), "cd alpha_t");
  handle(r.get(), key(ROLLTUI_KEY_TAB));
  check(text_of(r.get()) == "cd alpha_two/", "a relative path completes from the host's folder, replacing only the last word");
  // NAMES IN ORDER where they match as well: made backwards, the folder's own order is not the answer.
  ::mkdir((dir + "/many").c_str(), 0700);
  for (int i = 23; i >= 0; --i) std::fclose(std::fopen((dir + "/many/n" + std::to_string(100 + i)).c_str(), "w"));
  set_text(in.get(), dir + "/many/n");
  const auto many = candidates_of(in.get());
  bool sorted = many.size() == 24;
  for (std::size_t i = 0; sorted && i < many.size(); ++i) sorted = many[i] == dir + "/many/n" + std::to_string(100 + i);
  check(sorted, "candidates that match as well are in name order [" + std::to_string(many.size()) + (many.empty() ? "" : ", " + many[0]) + "]");
}

// TAB IN A SCREEN: a completing field keeps Tab for itself; a field without one lets it move the focus, as before.
void test_completion_in_a_window() {
  static const std::vector<const char*> kWords = {"help", "hello"};
  RolltuiContext* c = rolltui_context_new();
  rolltui_context_set_library_defaults(c);
  constexpr std::string_view screen = R"({ "resizable": false, "name": "c", "focus": "field", "root": { "column": [
    { "id": "field", "content": "input:q", "size": 3, "border": "rounded", "focusable": true },
    { "id": "other", "content": "input:r", "border": "rounded", "focusable": true } ] } })";
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(c, &nd);
  RolltuiLayout* l = rolltui_load_layout_text(screen.data(), screen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(c);
  RolltuiWindowStack* st = rolltui_window_stack_new();
  rolltui_window_stack_set_base(st, rolltui_layout_base(l));
  rolltui_window_stack_set_level_fn(st, rolltui_windows_back, w);
  for (const char* src : {"q", "r"}) rolltui_windows_bind_submit(w, src, 1, [](void*, const char*, std::size_t) {}, nullptr, nullptr, 1);
  const RolltuiRect area{0, 0, 40, 12};
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  auto frame = [&]() {
    rolltui_windows_sync(w, st);
    rolltui_windows_layout(w, st, area);
    RolltuiFrame* f = rolltui_frame_new(40, 12, styles[0]);
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    struct Slot { RolltuiWindows* w; RolltuiStyle* styles; } slot{w, styles};
    rolltui_window_stack_compose(
        st, f, area, styles, rolltui_layout_default_roles(),
        [](void* cx, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* sl = static_cast<Slot*>(cx);
          rolltui_windows_draw(sl->w, rn, fr, sl->styles, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    rolltui_windows_draw_toasts(w, f, area, styles, 0);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    const std::string all = str_of(text);
    rolltui_str_free(&text);
    rolltui_compose_scratch_free(cs);
    rolltui_frame_free(f);
    return all;
  };
  auto send = [&](const RolltuiEvent& e) {
    RolltuiStr window{};
    const unsigned char kind =
        rolltui_window_stack_route(st, &e, area, rolltui_bindings_default(c), rolltui_stack_default_actions(), &window);
    if (kind == ROLLTUI_ROUTE_DELIVER) rolltui_windows_handle(w, window.p ? window.p : "", window.n, &e);
    rolltui_str_free(&window);
    frame();
    return kind;
  };
  auto focused = [&]() {
    const RolltuiLayoutNode* n = rolltui_window_stack_focused(st);
    return n ? std::string(n->id.p, n->id.n) : std::string();
  };
  frame();
  RolltuiInput* in = rolltui_windows_input(w, "q", 1);
  rolltui_input_set_text(in, "he", 2);
  frame();
  check(send(key(ROLLTUI_KEY_TAB)) == ROLLTUI_ROUTE_FOCUS_MOVED && focused() == "other", "CONTROL: without a completer, Tab moves the focus");
  send(key(ROLLTUI_KEY_TAB, /*shift=*/true));
  rolltui_input_set_completer(in, complete_words, const_cast<std::vector<const char*>*>(&kWords));
  frame();
  check(send(key(ROLLTUI_KEY_TAB)) == ROLLTUI_ROUTE_DELIVER && focused() == "field" && text_of(in) == "hel",
        "with one, Tab is the field's: the word completes and the focus stays [" + focused() + ", " + std::string(text_of(in)) + "]");
  send(key(ROLLTUI_KEY_TAB));
  const std::string open = frame();
  check(text_of(in) == "help" && open.find("1 of 2") != std::string::npos && open.find("hello") != std::string::npos,
        "the list opens, drawn over the screen by the windows' overlay pass");
  const unsigned char esc = send(key(ROLLTUI_KEY_ESCAPE));
  check(esc == ROLLTUI_ROUTE_DELIVER && text_of(in) == "hel" && frame().find("1 of 2") == std::string::npos && focused() == "field",
        "Escape is the open list's: it closes and the word comes back [" + std::to_string(esc) + ", " + std::string(text_of(in)) + "]");
  check(send(key(ROLLTUI_KEY_TAB, /*shift=*/true)) == ROLLTUI_ROUTE_DELIVER && focused() == "field",
        "Shift-Tab is the field's too");
  rolltui_input_set_text(in, "zzz", 3);
  frame();
  check(send(key(ROLLTUI_KEY_TAB)) == ROLLTUI_ROUTE_FOCUS_MOVED && focused() == "other" && text_of(in) == "zzz",
        "a word with nothing to complete leaves Tab to move the focus");
  rolltui_windows_free(w);
  rolltui_window_stack_free(st);
  rolltui_layout_free(l);
  rolltui_context_free(c);
}

int main() {
  test_typing_and_grapheme_boundaries();
  test_vim_modes();
  test_vim_modes_in_a_window();
  test_completion();
  test_completion_in_every_theme();
  test_path_completion();
  test_completion_in_a_window();
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
  test_one_row();
  test_undo_redo_grouping_rule();
  test_undo_redo_restores_caret_and_selection_exactly();
  test_undo_redo_new_edit_drops_the_redo_branch();
  test_undo_redo_and_history_never_touch_each_other();
  test_undo_group_closes_on_select_all_and_mouse();
  test_degenerate_sizes();
  test_kill_and_yank();
  test_transpose();
  test_word_case();
  test_overwrite();
  test_secret();
  test_submit_rules_and_modes();
  test_history_by_prefix();
  test_history_file();
  test_word_wrap_and_wrapped_text();
  test_line_numbers_and_highlighting();
  return testkit::report("rolltui_input_test");
}
