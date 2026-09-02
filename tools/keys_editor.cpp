// rolltui/tools/keys_editor.cpp — see keys_editor.hpp.
#include "keys_editor.hpp"

#include <algorithm>

namespace rolltui::tools {

namespace {
const char* kScopes[] = {"input", "transcript", "menu", "edit", "stack", "app", "editor", "playground"};
}

KeysEditor::KeysEditor() : current_(default_bindings()) {
  undo_.reset(current_);
  rebuild_menu();
}

void KeysEditor::load(const Bindings& b) {
  current_ = b;
  undo_.reset(current_);
  capture_.reset();
  rebuild_menu();
  status_ = "loaded";
}

void KeysEditor::set_presets(std::vector<std::string> names) {
  presets_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : presets_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("load", std::move(opts));
}

void KeysEditor::set_shipped(std::vector<std::string> names, bool may_write) {
  shipped_ = std::move(names);
  may_write_shipped_ = may_write;
  std::vector<MenuItem> opts;
  for (const std::string& n : shipped_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("write_shipped", std::move(opts));
  menu_.set_enabled("write_shipped", may_write && !shipped_.empty());
}

const std::string& KeysEditor::capturing_action() const {
  static const std::string none;
  return capture_ ? *capture_ : none;
}

void KeysEditor::rebuild_menu() {
  std::vector<MenuItem> scopes;
  for (const char* scope : kScopes) {
    std::vector<MenuItem> actions;
    for (const std::string& a : current_.actions()) {
      if (scope_of(a) != scope) continue;
      std::vector<MenuItem> items;
      items.push_back(MenuItem::action("bind." + a, "add a chord (press it)\xE2\x80\xA6"));
      for (const KeyEvent& k : current_.chords_for(a)) items.push_back(MenuItem::action("unbind." + a + "." + chord_to_string(k), "remove " + chord_display(k)));
      items.push_back(MenuItem::action("clear." + a, "clear every chord"));
      const std::string chords = current_.chords_text(a);
      actions.push_back(MenuItem::submenu("action." + a, a.substr(a.find('.') + 1) + "  " + (chords.empty() ? "(unbound)" : chords), std::move(items)));
    }
    scopes.push_back(MenuItem::submenu(std::string("scope.") + scope, scope, std::move(actions)));
  }
  std::vector<MenuItem> loads, ships;
  for (const std::string& n : presets_) loads.push_back(MenuItem::action(n, n));
  for (const std::string& n : shipped_) ships.push_back(MenuItem::action(n, n));
  InputSpec name;
  name.type = InputType::Name;
  MenuItem root = MenuItem::submenu(
      "root", "keys editor",
      {MenuItem::submenu("scopes", "Actions by scope", std::move(scopes)),
       MenuItem::action("undo", "Undo", "Ctrl-Z"), MenuItem::action("redo", "Redo", "Ctrl-Y"),
       MenuItem::choice("load", "Load preset", std::move(loads), ""), MenuItem::input("save", "Save as preset", name),
       MenuItem::choice("write_shipped", "Write a SHIPPED preset (the editor's privilege)", std::move(ships), ""),
       MenuItem::action("reset_loaded", "Reset to the loaded preset\xE2\x80\xA6")});
  menu_.set_root(std::move(root));
  menu_.set_enabled("write_shipped", may_write_shipped_ && !shipped_.empty());
}

// Refreshes one action's level in place (its label and its remove items), keeping the
// navigation where it is.
void KeysEditor::rebuild_action(std::string_view action) {
  std::vector<MenuItem> items;
  const std::string a(action);
  items.push_back(MenuItem::action("bind." + a, "add a chord (press it)\xE2\x80\xA6"));
  for (const KeyEvent& k : current_.chords_for(a)) items.push_back(MenuItem::action("unbind." + a + "." + chord_to_string(k), "remove " + chord_display(k)));
  items.push_back(MenuItem::action("clear." + a, "clear every chord"));
  menu_.set_options("action." + a, std::move(items));
  if (MenuItem* it = menu_.find("action." + a)) {
    const std::string chords = current_.chords_text(a);
    it->label = a.substr(a.find('.') + 1) + "  " + (chords.empty() ? "(unbound)" : chords);
  }
}

KeysEditor::Outcome KeysEditor::commit() {
  if (current_ == undo_.current()) return {Outcome::Kind::Changed, {}};
  undo_.commit(current_);
  return {Outcome::Kind::Committed, {}};
}

void KeysEditor::replace(Bindings b) {
  capture_.reset();
  current_ = std::move(b);
  undo_.commit(current_);
  rebuild_menu();
}

bool KeysEditor::undo() {
  capture_.reset();
  if (!undo_.undo()) return false;
  current_ = undo_.current();
  rebuild_menu();
  status_ = "undone";
  return true;
}

bool KeysEditor::redo() {
  capture_.reset();
  if (!undo_.redo()) return false;
  current_ = undo_.current();
  rebuild_menu();
  status_ = "redone";
  return true;
}

std::string KeysEditor::status_line() const {
  // While capturing, a refusal (an unnameable key) is shown WITH the prompt, so the
  // reason and what to do next are both on the line.
  const std::string prompt = capture_ ? "press the chord for " + *capture_ + " (Esc cancels)" : "";
  std::string s = capture_ ? (status_.empty() ? prompt : status_ + " \xE2\x80\x94 " + prompt)
                           : (status_.empty() ? "Enter on an action: add, remove or clear its chords" : status_);
  s += " \xC2\xB7 undo " + std::to_string(undo_.undo_depth()) + " \xC2\xB7 redo " + std::to_string(undo_.redo_depth());
  return s;
}

KeysEditor::Outcome KeysEditor::handle(const Event& e, const Bindings& nav) {
  using K = MenuEvent::Kind;
  using O = Outcome::Kind;
  if (capture_) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k) return {O::None, {}};
    if (k->key == Key::Escape && !k->ctrl && !k->alt && !k->shift) { capture_.reset(); status_ = "capture cancelled"; return {O::Changed, {}}; }
    if (k->key == Key::Unknown) { status_ = "that key has no chord name; try another"; return {O::Changed, {}}; }
    const std::string action = *capture_;
    capture_.reset();
    std::string moved;
    if (!current_.bind(action, *k, &moved)) {
      status_ = "refused: " + chord_display(*k) + " cannot be bound to " + action + (k->key == Key::Enter ? " (Enter is always input.submit)" : "");
      return {O::Changed, {}};
    }
    // Short, for a 48-column popup: the scope is in the breadcrumb already.
    auto verb = [](const std::string& a) { return a.substr(a.find('.') + 1); };
    status_ = "bound " + chord_display(*k) + " \xE2\x86\x92 " + verb(action) + (moved.empty() ? "" : " (was " + verb(moved) + ")");
    rebuild_action(action);
    if (!moved.empty()) rebuild_action(moved);
    return commit();
  }
  if (const auto* k = std::get_if<KeyEvent>(&e)) {
    const std::string_view ed = nav.action_for(*k, "editor");
    if (ed == "editor.undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ed == "editor.redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
  }
  status_.clear();
  const MenuEvent ev = menu_.handle(e, nav);
  if (ev.kind == K::Activate) {
    if (ev.id.rfind("bind.", 0) == 0) { capture_ = ev.id.substr(5); status_.clear(); return {O::Changed, {}}; }
    if (ev.id.rfind("unbind.", 0) == 0) {
      const std::string rest = ev.id.substr(7);
      // "<action>.<chord>": the action has one dot; the chord is what follows the second.
      const std::size_t first = rest.find('.');
      const std::size_t second = rest.find('.', first + 1);
      if (second == std::string::npos) return {O::None, {}};
      const std::string action = rest.substr(0, second), chord = rest.substr(second + 1);
      if (const std::optional<KeyEvent> k = parse_chord(chord)) {
        if (!current_.unbind(action, *k)) { status_ = "refused: " + chord_display(*k) + " stays on " + action; return {O::Changed, {}}; }
        status_ = "removed " + chord_display(*k) + " from " + action;
        rebuild_action(action);
        return commit();
      }
      return {O::None, {}};
    }
    if (ev.id.rfind("clear.", 0) == 0) {
      const std::string action = ev.id.substr(6);
      current_.clear(action);
      status_ = action == "input.submit" ? "input.submit keeps Enter" : "cleared " + action;
      rebuild_action(action);
      return commit();
    }
    if (ev.id == "undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ev.id == "redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ev.id == "reset_loaded") return {O::ResetLoaded, {}};
    return {O::None, {}};
  }
  if (ev.kind == K::Choose) {
    if (ev.id == "load") return {O::LoadPreset, ev.value};
    if (ev.id == "write_shipped") return {O::WriteShipped, ev.value};
    return {O::None, {}};
  }
  if (ev.kind == K::Input && ev.id == "save") return {O::SaveAs, ev.value};
  if (ev.kind == K::Closed) return {O::Closed, {}};
  return {O::None, {}};
}

}  // namespace rolltui::tools
