//
// keys_editor_test.cpp — the keys editor's model (milestone 17): the tree of scopes
// and actions with their chords in the labels, capture (the next key becomes the
// chord; Escape cancels), a conflict inside the scope moves the chord and the status
// names the loser, the Enter rule refused by name, remove and clear, undo/redo as
// commits, the host-facing outcomes, and the edited table round-tripping through the
// file format.
//
// Phase 17 m1d: drives the editor through `rolltui/c/*.h` directly — no `rolltui/*.hpp`.
//
#include <optional>
#include <string>
#include <vector>

#include "keys_editor.hpp"
#include "rolltui_test.hpp"

using namespace rolltui::tools;
using namespace rolltui_test;

namespace {

RolltuiChord key(unsigned char k, bool ctrl = false, bool alt = false, bool shift = false) {
  RolltuiChord e{};
  e.key = k;
  e.ctrl = ctrl ? 1 : 0;
  e.alt = alt ? 1 : 0;
  e.shift = shift ? 1 : 0;
  return e;
}
RolltuiChord ch(char32_t c, bool ctrl = false, bool alt = false) {
  RolltuiChord e{};
  e.key = ROLLTUI_KEY_CHAR;
  e.ch = c;
  e.ctrl = ctrl ? 1 : 0;
  e.alt = alt ? 1 : 0;
  return e;
}
RolltuiEvent key_event(const RolltuiChord& k) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key = k;
  return e;
}
void type(KeysEditor& ed, const std::string& s) {
  for (char c : s) {
    const RolltuiEvent e = key_event(ch(static_cast<char32_t>(c)));
    ed.handle(&e, editor_bindings());
  }
}
KeysEditor::Outcome go(KeysEditor& ed, const RolltuiChord& k) {
  const RolltuiEvent e = key_event(k);
  return ed.handle(&e, editor_bindings());
}
std::string action_for(const RolltuiBindings* b, const RolltuiChord& k, std::string_view scope) {
  std::size_t len = 0;
  const char* p = rolltui_bindings_action_for(b, &k, scope.data(), scope.size(), &len);
  return p ? std::string(p, len) : std::string();
}
std::size_t chord_count(const RolltuiBindings* b, const std::string& action) {
  return rolltui_bindings_chord_count(b, action.c_str(), action.size());
}
std::string scope_of(const std::string& action) {
  std::size_t n = 0;
  const char* p = rolltui_bindings_scope_of(action.c_str(), action.size(), &n);
  return std::string(p, n);
}
RolltuiMenuItem* find(RolltuiMenu* m, std::string_view id) { return rolltui_menu_find(m, id.data(), id.size()); }

}  // namespace

int main() {
  KeysEditor ed;
  ed.load(rolltui_bindings_default());
  using O = KeysEditor::Outcome::Kind;
  check(find(ed.menu(), "scope.input") && find(ed.menu(), "action.input.word_left") && find(ed.menu(), "bind.input.word_left") &&
            find(ed.menu(), "unbind.input.word_left.ctrl+left"),
        "the tree is scope › action › {add, remove <chord>, clear}");
  check(find(ed.menu(), "action.input.word_left")->label == "word_left  Ctrl-Left, Alt-Left",
        "an action's label shows its chords [" + find(ed.menu(), "action.input.word_left")->label.str() + "]");
  // ---- capture: Alt-B onto word_left ----
  go(ed, key(ROLLTUI_KEY_ENTER));          // scopes
  go(ed, key(ROLLTUI_KEY_ENTER));          // input
  type(ed, "word_left");
  go(ed, key(ROLLTUI_KEY_ENTER));          // the action's level
  KeysEditor::Outcome o = go(ed, key(ROLLTUI_KEY_ENTER));  // add a chord
  check(o.kind == O::Changed && ed.capturing() && ed.capturing_action() == "input.word_left" && ed.status_line().find("press the chord for input.word_left") == 0,
        "Enter on 'add a chord' starts capture and says so");
  RolltuiChord unknown{};
  unknown.key = ROLLTUI_KEY_UNKNOWN;
  o = go(ed, unknown);
  check(ed.capturing() && o.kind == O::Changed && ed.status_line().find("no chord name") != std::string::npos, "a key with no chord name is refused and the capture continues");
  o = go(ed, key(ROLLTUI_KEY_ESCAPE));
  check(!ed.capturing() && o.kind == O::Changed && ed.status_line().find("cancelled") != std::string::npos, "Escape cancels the capture");
  go(ed, key(ROLLTUI_KEY_ENTER));
  o = go(ed, ch('b', false, true));  // Alt-B
  check(o.kind == O::Committed && !ed.capturing() && action_for(ed.current(), ch('b', false, true), "input") == "input.word_left" && ed.undo_depth() == 1,
        "the next key becomes the chord: Alt-B → word_left, one commit");
  check(ed.status_line().find("bound Alt-B \xE2\x86\x92 word_left") == 0 && find(ed.menu(), "action.input.word_left")->label.find("Alt-B") != std::string::npos,
        "the status and the action's label show the new chord [" + ed.status_line() + "]");
  check(find(ed.menu(), "unbind.input.word_left.alt+b") != nullptr, "…and a remove item for it appears");
  // ---- a conflict moves ----
  go(ed, key(ROLLTUI_KEY_HOME));
  o = go(ed, key(ROLLTUI_KEY_ENTER));      // add another chord
  o = go(ed, ch('d', false, true)); // Alt-D: currently kill_word_forward's
  check(o.kind == O::Committed && ed.status_line().find("(was kill_word_forward)") != std::string::npos &&
            action_for(ed.current(), ch('d', false, true), "input") == "input.word_left" && chord_count(ed.current(), "input.kill_word_forward") == 1,
        "a chord bound elsewhere in the scope moves, and the status names the loser [" + ed.status_line() + "]");
  // ---- the Enter rule ----
  go(ed, key(ROLLTUI_KEY_HOME));
  go(ed, key(ROLLTUI_KEY_ENTER));
  o = go(ed, key(ROLLTUI_KEY_ENTER));      // Enter as the chord
  check(o.kind == O::Changed && ed.status_line().find("refused") == 0 && ed.status_line().find("Enter is always input.submit") != std::string::npos &&
            action_for(ed.current(), key(ROLLTUI_KEY_ENTER), "input") == "input.submit",
        "Enter cannot be captured for another input action: refused by name [" + ed.status_line() + "]");
  // ---- remove, clear ----
  go(ed, key(ROLLTUI_KEY_END));            // clear every chord
  go(ed, key(ROLLTUI_KEY_UP));             // remove Alt-D (the last remove item)
  const std::string label(rolltui_menu_selected_item(ed.menu())->label.view());
  o = go(ed, key(ROLLTUI_KEY_ENTER));
  check(label == "remove Alt-D" && o.kind == O::Committed && action_for(ed.current(), ch('d', false, true), "input").empty(), "remove takes a chord off [" + label + "]");
  go(ed, key(ROLLTUI_KEY_END));
  o = go(ed, key(ROLLTUI_KEY_ENTER));
  check(o.kind == O::Committed && chord_count(ed.current(), "input.word_left") == 0 && find(ed.menu(), "action.input.word_left")->label.find("(unbound)") != std::string::npos,
        "clear empties the action and the label says (unbound)");
  // ---- undo / redo ----
  const std::size_t depth = ed.undo_depth();
  o = go(ed, ch('z', true));
  check(o.kind == O::Committed && ed.undo_depth() == depth - 1 && chord_count(ed.current(), "input.word_left") != 0, "Ctrl-Z brings the chords back as a commit");
  o = go(ed, ch('y', true));
  check(o.kind == O::Committed && chord_count(ed.current(), "input.word_left") == 0, "Ctrl-Y clears them again");
  // ---- the outcomes ----
  ed.set_presets({"default", "vim-ish"});
  ed.set_shipped({"default"}, true);
  for (int i = 0; i < 4; ++i) go(ed, key(ROLLTUI_KEY_ESCAPE));
  go(ed, key(ROLLTUI_KEY_HOME));
  type(ed, "load");
  go(ed, key(ROLLTUI_KEY_ENTER));
  go(ed, key(ROLLTUI_KEY_DOWN));
  o = go(ed, key(ROLLTUI_KEY_ENTER));
  check(o == KeysEditor::Outcome{O::LoadPreset, "vim-ish"}, "Load preset asks the host");
  go(ed, key(ROLLTUI_KEY_ESCAPE));
  go(ed, key(ROLLTUI_KEY_HOME));
  type(ed, "save");
  go(ed, key(ROLLTUI_KEY_ENTER));
  type(ed, "mine");
  o = go(ed, key(ROLLTUI_KEY_ENTER));
  check(o == KeysEditor::Outcome{O::SaveAs, "mine"}, "Save as asks the host with the name");
  go(ed, key(ROLLTUI_KEY_ESCAPE));
  go(ed, key(ROLLTUI_KEY_HOME));
  type(ed, "shipped");
  go(ed, key(ROLLTUI_KEY_ENTER));
  o = go(ed, key(ROLLTUI_KEY_ENTER));
  check(o == KeysEditor::Outcome{O::WriteShipped, "default"}, "Write a shipped preset asks the host");
  // ---- round trip ----
  {
    RolltuiStr dumped{};
    rolltui_bindings_dump_json(ed.committed(), "edited", 6, &dumped);
    // Seeded (the 59 library actions declared, no chords yet) — NOT a clone of
    // rolltui_bindings_default(), which already carries the shipped file's real chords:
    // starting there would let the loader's ADD semantics double up every untouched
    // chord. This is rolltui::Bindings::Bindings()'s own starting point (Bindings.cpp),
    // which is what Bindings::from_json used to seed itself with before the C port.
    RolltuiBindings* back = rolltui_bindings_new_seeded();
    RolltuiBindingsReport rep{};
    const int ok = rolltui_bindings_load_json(back, dumped.p ? dumped.p : "", dumped.n, ROLLTUI_PROTOCOL_LEGACY,
                                              rolltui_bindings_library_scope, nullptr, nullptr, nullptr, &rep);
    check(ok != 0 && rolltui_bindings_report_clean(&rep) != 0 && rolltui_bindings_equal(back, ed.committed()) != 0,
          "the edited table round-trips through the file format");
    rolltui_bindings_report_release(&rep);
    rolltui_bindings_free(back);
    rolltui_str_free(&dumped);
  }

  // ---- Phase 11 m1: the tools' own tables ------------------------------------------
  // tool_actions.hpp is where the library's tools' keys live now that library_actions()
  // is the widget scopes only. Every row of it has to actually work when mounted, and
  // nothing in the shipped bindings file can say so any more — so it is said here: mount
  // both tables into a table that has never heard of them and press every chord.
  {
    RolltuiBindings* b = rolltui_bindings_new();
    std::vector<RolltuiToolAction> both;
    for (const RolltuiToolAction& t : editor_actions()) both.push_back(t);
    for (const RolltuiToolAction& t : studio_actions()) both.push_back(t);
    rolltui_bindings_declare(b, nullptr, 0, both.data(), both.size());
    std::string dead;
    for (const RolltuiToolAction& t : both) {
      const std::string name(t.name), chord_text(t.chord ? t.chord : "");
      RolltuiChord k{};
      const bool parsed = rolltui_chord_parse(chord_text.c_str(), chord_text.size(), &k) != 0;
      const std::string got = action_for(b, k, scope_of(name));
      if (!parsed || !rolltui_bindings_has(b, name.c_str(), name.size()) || got != name) dead += " " + name;
      if (!t.description || !t.description[0]) dead += " (no description) " + name;
    }
    check(dead.empty(), "every action the library's tools state is declared by mounting them, and its suggested chord answers that key —" + (dead.empty() ? " all 8" : dead));
    check(!rolltui_bindings_has(b, "app.help", 8) && action_for(b, key(ROLLTUI_KEY_F1), "app").empty(),
          "…and mounting a tool declares nothing else: the app scope is a layout's");
    rolltui_bindings_free(b);
  }
  return report("rolltui keys_editor_test");
}
