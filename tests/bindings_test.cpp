//
// bindings_test.cpp — key bindings as data (milestone 17): chords parse and print in
// every modifier order and round-trip; the shipped default embeds its file verbatim,
// loads clean, and binds every library action; lookup is by scope so one chord serves
// several widgets; a conflict inside a scope is reported and the first binding wins;
// the Enter rule refuses a file that moves Enter and restores it; bind() moves a chord
// off a conflicting action and says so; to_json round-trips; help_lines renders the
// live table.
//
#include <fstream>
#include <string>

#include "rolltui/Bindings.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

#ifndef ROLLTUI_BINDINGS_DIR
#error "ROLLTUI_BINDINGS_DIR must point at rolltui/presets/bindings"
#endif

namespace {
KeyEvent key(Key k, bool ctrl = false, bool alt = false, bool shift = false) { KeyEvent e; e.key = k; e.ctrl = ctrl; e.alt = alt; e.shift = shift; return e; }
KeyEvent ch(char32_t c, bool ctrl = false, bool alt = false) { KeyEvent e; e.key = Key::Char; e.ch = c; e.ctrl = ctrl; e.alt = alt; return e; }
std::string read_file(const std::string& p) { std::ifstream in(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }
}  // namespace

int main() {
  // ---- chords ----
  check(parse_chord("ctrl+w") == ch('w', true) && parse_chord("Ctrl+W") == ch('w', true) && parse_chord("C+w") == ch('w', true), "ctrl+w in any case or abbreviation");
  check(parse_chord("shift+ctrl+left") == key(Key::Left, true, false, true) && parse_chord("ctrl+shift+left") == key(Key::Left, true, false, true), "modifiers in any order");
  check(parse_chord("alt+enter") == key(Key::Enter, false, true) && parse_chord("meta+enter") == key(Key::Enter, false, true) && parse_chord("option+enter") == key(Key::Enter, false, true), "alt, meta and option are one modifier");
  check(parse_chord("f1") == key(Key::F1) && parse_chord("F12") == key(Key::F12) && parse_chord("escape") == key(Key::Escape) && parse_chord("esc") == key(Key::Escape) && parse_chord("pgdn") == key(Key::PageDown), "named keys and their short forms");
  check(parse_chord("?") == ch(U'?') && parse_chord("space") == ch(U' ') && parse_chord("+") == ch(U'+') && parse_chord("ctrl++") == ch(U'+', true), "a printable character is itself; '+' and 'ctrl++' parse");
  check(!parse_chord("") && !parse_chord("ctrl+") && !parse_chord("hyper+x") && !parse_chord("ctrl+meta+x+y") && !parse_chord("f13"), "empty, dangling, unknown modifier, two keys, f13: not chords");
  check(chord_to_string(key(Key::Left, true, false, true)) == "ctrl+shift+left" && chord_to_string(ch(U'?')) == "?" && chord_to_string(ch(U' ')) == "space" && chord_to_string(key(Key::PageUp)) == "pageup",
        "chord_to_string is canonical (ctrl, alt, shift; key names)");
  {
    bool all = true;
    for (const char* c : {"ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space", "shift+tab", "ctrl+home", "alt+backspace", "pagedown"})
      all &= chord_to_string(*parse_chord(c)) == c;
    check(all, "canonical chords round-trip through parse and print");
    KeyEvent raw = key(Key::Left, true);
    raw.raw = "\x1b[1;5D";
    check(chord_to_string(raw) == "ctrl+left", "a KeyEvent's raw bytes are never part of a chord");
    check(chord_display(*parse_chord("ctrl+shift+left")) == "Ctrl-Shift-Left" && chord_display(*parse_chord("alt+enter")) == "Alt-Enter" && chord_display(*parse_chord("?")) == "?" &&
              chord_display(*parse_chord("pageup")) == "PgUp" && chord_display(*parse_chord("f1")) == "F1",
          "chord_display is the help form (Ctrl-Shift-Left, Alt-Enter, PgUp)");
    check(chord_to_string(key(Key::Unknown)).empty(), "an Unknown key has no chord");
  }
  // ---- the shipped default ----
  {
    check(default_bindings_json() == read_file(std::string(ROLLTUI_BINDINGS_DIR) + "/default.json") && !default_bindings_json().empty(), "the default embeds presets/bindings/default.json verbatim");
    BindingsLoadReport rep;
    std::optional<Bindings> d = Bindings::from_json(default_bindings_json(), rep);
    check(d && rep.clean(), "the shipped default loads clean [" + rep.summary() + "]");
    bool every_bound = true;
    std::string unbound;
    for (const std::string& a : d->actions())
      if (d->chords_for(a).empty()) { every_bound = false; unbound += " " + a; }
    check(every_bound, "every library action has at least one chord in the default" + unbound);
    check(d->actions().size() == library_actions().size(), "the table lists exactly the library's actions (" + std::to_string(d->actions().size()) + ")");
    const Bindings& b = default_bindings();
    check(b.action_for(key(Key::Enter), "input") == "input.submit" && b.action_for(key(Key::Enter), "menu") == "menu.activate" && b.action_for(key(Key::Enter), "transcript").empty(),
          "Enter is input.submit in the input scope, menu.activate in the menu scope, nothing in the transcript");
    check(b.action_for(key(Key::Up), "input") == "input.up" && b.action_for(key(Key::Up), "transcript") == "transcript.line_up" && b.action_for(key(Key::Up), "menu") == "menu.up",
          "Up serves three scopes");
    check(b.action_for(ch('w', true), "input") == "input.kill_word_backward" && b.action_for(key(Key::Backspace, false, true), "input") == "input.kill_word_backward", "two chords, one action");
    check(b.action_for(key(Key::Left, true, false, true), "input") == "input.select_word_left" && b.action_for(key(Key::Left, false, true, true), "input") == "input.select_word_left", "ctrl+shift+left and alt+shift+left both extend by a word");
    check(b.action_for(ch(U'?'), "app") == "app.help" && b.action_for(key(Key::F1), "app") == "app.help" && b.action_for(ch(U'?'), "input").empty(), "'?' is app.help and is not an input action (typing it inserts)");
    KeyEvent with_raw = ch('w', true);
    with_raw.raw = "\x17";
    check(b.action_for(with_raw, "input") == "input.kill_word_backward", "lookup ignores raw bytes");
    check(b.chords_text("input.kill_word_backward") == "Ctrl-W, Alt-Backspace", "chords_text joins the display forms [" + b.chords_text("input.kill_word_backward") + "]");
    check(scope_of("input.submit") == "input" && scope_of("app.help") == "app", "scope_of");
  }
  // ---- the loader's report ----
  {
    BindingsLoadReport rep;
    std::optional<Bindings> b = Bindings::from_json(R"({"name":"x","bindings":{"input.left":["left","ctrl+b"],"input.right":["left"],"input.nothing":["x"],"input.up":["meta+hyper+z"],"input.newline":["enter"],"input.submit":["ctrl+j"]},"extra":1})", rep);
    check(b && rep.error.empty(), "a file with problems still loads");
    check(rep.conflicts.size() == 1 && rep.conflicts[0].find("'left' bound to both input.left and input.right") == 0 && b->action_for(key(Key::Left), "input") == "input.left",
          "a chord bound twice in one scope is a conflict; the first binding wins [" + (rep.conflicts.empty() ? "" : rep.conflicts[0]) + "]");
    check(rep.unknown_actions == std::vector<std::string>{"input.nothing"}, "an unknown action is reported by name");
    check(rep.bad_chords.size() == 1 && rep.bad_chords[0].find("input.up: 'meta+hyper+z'") == 0, "an unparseable chord is reported with its action");
    check(rep.bad_values.size() == 2 && rep.bad_values[0].find("input.newline: 'enter' is always input.submit") == 0 && rep.bad_values[1].find("input.submit: 'enter' is always bound") == 0,
          "the Enter rule: binding Enter elsewhere in the input scope is refused by name, and input.submit gets Enter back");
    check(b->action_for(key(Key::Enter), "input") == "input.submit" && b->action_for(ch('j', true), "input") == "input.submit", "…so Enter submits, and ctrl+j too");
    check(rep.unknown_keys == std::vector<std::string>{"extra"}, "an unknown top-level key is reported");
    check(!Bindings::from_json("[1]", rep) && !rep.error.empty(), "a non-object is unusable");
    check(!Bindings::from_json(R"({"name":"x"})", rep) && rep.error.find("bindings") != std::string::npos, "a file without a bindings object is unusable");
    std::optional<Bindings> empty = Bindings::from_json(R"({"name":"e","bindings":{}})", rep);
    check(empty && empty->chords_for("input.left").empty() && !empty->chords_for("input.submit").empty(), "an empty file binds nothing but Enter → submit (a file is the whole domain)");
  }
  // ---- bind / unbind ----
  {
    Bindings b = default_bindings();
    std::string moved;
    check(b.bind("input.word_left", *parse_chord("alt+b"), &moved) && moved.empty() && b.action_for(ch('b', false, true), "input") == "input.word_left", "bind adds a chord");
    check(b.bind("input.word_right", *parse_chord("alt+d"), &moved) && moved == "input.kill_word_forward" && b.action_for(ch('d', false, true), "input") == "input.word_right" &&
              b.action_for(key(Key::Delete, true), "input") == "input.kill_word_forward",
          "a chord bound elsewhere in the scope moves, and moved_from names the loser");
    check(!b.bind("input.newline", key(Key::Enter)) && b.action_for(key(Key::Enter), "input") == "input.submit", "Enter cannot be bound to another input action");
    check(!b.bind("input.nope", key(Key::F9)), "an unknown action is refused");
    check(b.bind("transcript.top", key(Key::Enter)) && b.action_for(key(Key::Enter), "transcript") == "transcript.top", "…but Enter may serve another scope");
    check(!b.unbind("input.submit", key(Key::Enter)) && b.unbind("input.word_left", *parse_chord("alt+b")) && !b.unbind("input.word_left", *parse_chord("alt+b")), "unbind: Enter stays on submit; a chord removes once");
    b.clear("input.copy");
    check(b.chords_for("input.copy").empty(), "clear empties an action");
    b.clear("input.submit");
    check(!b.chords_for("input.submit").empty(), "…except input.submit");
    b.add_action("mine.thing", "my host's own");
    check(b.bind("mine.thing", key(Key::F9)) && b.action_for(key(Key::F9), "mine") == "mine.thing" && b.description("mine.thing") == "my host's own", "a host may add its own actions");
  }
  // ---- round trip ----
  {
    const Bindings& d = default_bindings();
    BindingsLoadReport rep;
    std::optional<Bindings> back = Bindings::from_json(d.to_json("default"), rep);
    check(back && rep.clean() && *back == d, "to_json / from_json round-trips the default exactly");
    Bindings edited = d;
    edited.bind("input.word_left", *parse_chord("alt+b"));
    check(!(edited == d), "an edit makes the tables unequal (the label-by-comparison rule can stand on this)");
  }
  // ---- help ----
  {
    const std::vector<std::string> lines = help_lines(default_bindings(), "input", {"input.submit", "input.kill_word_backward"});
    check(lines.size() == 2 && lines[0].find("Enter") == 0 && lines[0].find("send the line") != std::string::npos && lines[1].find("Ctrl-W, Alt-Backspace") == 0,
          "help_lines: the chords, then the description [" + (lines.empty() ? "" : lines[0]) + "]");
    const std::vector<std::string> all = help_lines(default_bindings(), "menu");
    check(all.size() == 11 && all[0].find("Up") == 0, "an empty list means every action of the scope (menu: 11)");
    Bindings vim = default_bindings();
    vim.bind("input.word_left", *parse_chord("alt+b"));
    check(help_lines(vim, "input", {"input.word_left"})[0].find("Alt-B") != std::string::npos, "help follows a rebinding: it is rendered from the live table");
    // The chord column is capped at 24 cells: one long chord list does not push every
    // other description across a narrow popup (found by a 46-column golden).
    Bindings wide = default_bindings();
    wide.bind("input.word_right", *parse_chord("ctrl+shift+f12"));
    wide.bind("input.word_right", *parse_chord("alt+shift+f11"));
    const std::vector<std::string> capped = help_lines(wide, "input", {"input.left", "input.word_right"});
    check(capped[0].find("move one grapheme left") <= 24, "a short chord's description starts within the 24-cell column (at " + std::to_string(capped[0].find("move one grapheme left")) + ")");
    check(capped[1].find("  move one word right") != std::string::npos && capped[1].find("move one word right") > 24,
          "a chord list longer than the column is followed by two spaces, not padded");
  }
  return report("rolltui bindings_test");
}
