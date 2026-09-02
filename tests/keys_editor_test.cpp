//
// keys_editor_test.cpp — the keys editor's model (milestone 17): the tree of scopes
// and actions with their chords in the labels, capture (the next key becomes the
// chord; Escape cancels), a conflict inside the scope moves the chord and the status
// names the loser, the Enter rule refused by name, remove and clear, undo/redo as
// commits, the host-facing outcomes, and the edited table round-tripping through the
// file format.
//
#include <string>

#include "keys_editor.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui::tools;
using namespace rolltui_test;

namespace {
KeyEvent key(Key k, bool ctrl = false, bool alt = false, bool shift = false) { KeyEvent e; e.key = k; e.ctrl = ctrl; e.alt = alt; e.shift = shift; return e; }
KeyEvent ch(char32_t c, bool ctrl = false, bool alt = false) { KeyEvent e; e.key = Key::Char; e.ch = c; e.ctrl = ctrl; e.alt = alt; return e; }
void type(KeysEditor& ed, const std::string& s) { for (char c : s) ed.handle(ch(static_cast<char32_t>(c)), default_bindings()); }
KeysEditor::Outcome go(KeysEditor& ed, const KeyEvent& k) { return ed.handle(k, default_bindings()); }
}  // namespace

int main() {
  KeysEditor ed;
  ed.load(default_bindings());
  using O = KeysEditor::Outcome::Kind;
  check(ed.menu().find("scope.input") && ed.menu().find("action.input.word_left") && ed.menu().find("bind.input.word_left") && ed.menu().find("unbind.input.word_left.ctrl+left"),
        "the tree is scope › action › {add, remove <chord>, clear}");
  check(ed.menu().find("action.input.word_left")->label == "word_left  Ctrl-Left, Alt-Left", "an action's label shows its chords [" + ed.menu().find("action.input.word_left")->label + "]");
  // ---- capture: Alt-B onto word_left ----
  go(ed, key(Key::Enter));          // scopes
  go(ed, key(Key::Enter));          // input
  type(ed, "word_left");
  go(ed, key(Key::Enter));          // the action's level
  KeysEditor::Outcome o = go(ed, key(Key::Enter));  // add a chord
  check(o.kind == O::Changed && ed.capturing() && ed.capturing_action() == "input.word_left" && ed.status_line().find("press the chord for input.word_left") == 0,
        "Enter on 'add a chord' starts capture and says so");
  o = go(ed, key(Key::Escape));
  check(!ed.capturing() && o.kind == O::Changed && ed.status_line().find("cancelled") != std::string::npos, "Escape cancels the capture");
  go(ed, key(Key::Enter));
  o = go(ed, ch('b', false, true));  // Alt-B
  check(o.kind == O::Committed && !ed.capturing() && ed.current().action_for(ch('b', false, true), "input") == "input.word_left" && ed.undo_depth() == 1,
        "the next key becomes the chord: Alt-B → word_left, one commit");
  check(ed.status_line().find("bound Alt-B \xE2\x86\x92 word_left") == 0 && ed.menu().find("action.input.word_left")->label.find("Alt-B") != std::string::npos,
        "the status and the action's label show the new chord [" + ed.status_line() + "]");
  check(ed.menu().find("unbind.input.word_left.alt+b") != nullptr, "…and a remove item for it appears");
  // ---- a conflict moves ----
  go(ed, key(Key::Home));
  o = go(ed, key(Key::Enter));      // add another chord
  o = go(ed, ch('d', false, true)); // Alt-D: currently kill_word_forward's
  check(o.kind == O::Committed && ed.status_line().find("(was kill_word_forward)") != std::string::npos &&
            ed.current().action_for(ch('d', false, true), "input") == "input.word_left" && ed.current().chords_for("input.kill_word_forward").size() == 1,
        "a chord bound elsewhere in the scope moves, and the status names the loser [" + ed.status_line() + "]");
  // ---- the Enter rule ----
  go(ed, key(Key::Home));
  go(ed, key(Key::Enter));
  o = go(ed, key(Key::Enter));      // Enter as the chord
  check(o.kind == O::Changed && ed.status_line().find("refused") == 0 && ed.status_line().find("Enter is always input.submit") != std::string::npos &&
            ed.current().action_for(key(Key::Enter), "input") == "input.submit",
        "Enter cannot be captured for another input action: refused by name [" + ed.status_line() + "]");
  // ---- remove, clear ----
  go(ed, key(Key::End));            // clear every chord
  go(ed, key(Key::Up));             // remove Alt-D (the last remove item)
  const std::string label = ed.menu().selected_item()->label;
  o = go(ed, key(Key::Enter));
  check(label == "remove Alt-D" && o.kind == O::Committed && ed.current().action_for(ch('d', false, true), "input").empty(), "remove takes a chord off [" + label + "]");
  go(ed, key(Key::End));
  o = go(ed, key(Key::Enter));
  check(o.kind == O::Committed && ed.current().chords_for("input.word_left").empty() && ed.menu().find("action.input.word_left")->label.find("(unbound)") != std::string::npos,
        "clear empties the action and the label says (unbound)");
  // ---- undo / redo ----
  const std::size_t depth = ed.undo_depth();
  o = go(ed, ch('z', true));
  check(o.kind == O::Committed && ed.undo_depth() == depth - 1 && !ed.current().chords_for("input.word_left").empty(), "Ctrl-Z brings the chords back as a commit");
  o = go(ed, ch('y', true));
  check(o.kind == O::Committed && ed.current().chords_for("input.word_left").empty(), "Ctrl-Y clears them again");
  // ---- the outcomes ----
  ed.set_presets({"default", "vim-ish"});
  ed.set_shipped({"default"}, true);
  for (int i = 0; i < 4; ++i) go(ed, key(Key::Escape));
  go(ed, key(Key::Home));
  type(ed, "load");
  go(ed, key(Key::Enter));
  go(ed, key(Key::Down));
  o = go(ed, key(Key::Enter));
  check(o == KeysEditor::Outcome{O::LoadPreset, "vim-ish"}, "Load preset asks the host");
  go(ed, key(Key::Escape));
  go(ed, key(Key::Home));
  type(ed, "save");
  go(ed, key(Key::Enter));
  type(ed, "mine");
  o = go(ed, key(Key::Enter));
  check(o == KeysEditor::Outcome{O::SaveAs, "mine"}, "Save as asks the host with the name");
  go(ed, key(Key::Escape));
  go(ed, key(Key::Home));
  type(ed, "shipped");
  go(ed, key(Key::Enter));
  o = go(ed, key(Key::Enter));
  check(o == KeysEditor::Outcome{O::WriteShipped, "default"}, "Write a shipped preset asks the host");
  // ---- round trip ----
  BindingsLoadReport rep;
  std::optional<Bindings> back = Bindings::from_json(ed.committed().to_json("edited"), rep);
  check(back && rep.clean() && *back == ed.committed(), "the edited table round-trips through the file format");
  return report("rolltui keys_editor_test");
}
