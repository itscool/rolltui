// rolltui/tools/keys_editor.cpp — see keys_editor.hpp.
#include "tool_str.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_menu.h"
#include "keys_editor.hpp"

#include <algorithm>
#include <cstddef>

namespace rolltui::tools {

namespace {
const char* kScopes[] = {"input", "transcript", "menu", "edit", "stack", "app", "editor", "studio"};

// Mechanical std::vector -> RolltuiMenuItemList glue for rolltui_menu_set_options, which
// (unlike the tree-building factories, which already take a std::vector) wants the
// library's own owned-list type. No domain knowledge — every editor needs the same loop
// because rolltui_menu_set_options does, so it is not worth a shared header over.
void set_options(RolltuiMenu* m, std::string_view id, std::vector<MenuItem>&& options) {
  RolltuiMenuItemList list;
  for (MenuItem& it : options) list.push_back(std::move(it));
  rolltui_menu_set_options(m, id.data(), id.size(), &list);
}
void set_enabled(RolltuiMenu* m, std::string_view id, bool enabled) {
  rolltui_menu_set_enabled(m, id.data(), id.size(), enabled ? 1 : 0);
}

// A chord's canonical / display spelling, built into a caller-owned std::string — the
// bounded C form (rolltui_bindings.h's rule 3a) with no logic of its own.
std::string chord_str(const RolltuiChord& k) {
  char buf[ROLLTUI_CHORD_STRING_MAX];
  return std::string(buf, rolltui_chord_to_string(&k, buf, sizeof buf));
}
std::string chord_disp(const RolltuiChord& k) {
  char buf[ROLLTUI_CHORD_STRING_MAX];
  return std::string(buf, rolltui_chord_display(&k, buf, sizeof buf));
}
std::string scope_of_str(const std::string& action) {
  std::size_t n = 0;
  const char* p = rolltui_bindings_scope_of(action.c_str(), action.size(), &n);
  return std::string(p, n);
}
// Every declared action name, in table order — rolltui::Bindings::actions()'s shape.
std::vector<std::string> action_names(const RolltuiBindings* b) {
  std::vector<std::string> out;
  const std::size_t n = rolltui_bindings_action_count(b);
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t len = 0;
    const char* p = rolltui_bindings_action_at(b, i, &len);
    out.emplace_back(p, len);
  }
  return out;
}
// Every chord bound to `action`, deliverable or not — rolltui::Bindings::chords_for()'s shape.
std::vector<RolltuiChord> chords_for(const RolltuiBindings* b, const std::string& action) {
  std::vector<RolltuiChord> out;
  const std::size_t n = rolltui_bindings_chord_count(b, action.c_str(), action.size());
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord k{};
    if (rolltui_bindings_chord_at(b, action.c_str(), action.size(), i, &k)) out.push_back(k);
  }
  return out;
}
// The HELP form: only the chords the active protocol can actually deliver — a thin
// RolltuiStr-to-std::string convenience over rolltui_bindings_chords_text, which is the
// library's own (the filtering used to be a third independent copy of this; it is not
// one any more).
std::string chords_text(const RolltuiBindings* b, const std::string& action) {
  RolltuiStr s{};
  rolltui_bindings_chords_text(b, action.c_str(), action.size(), &s);
  std::string out(s.p ? s.p : "", s.n);
  rolltui_str_free(&s);
  return out;
}
}  // namespace

KeysEditor::KeysEditor(RolltuiContext* ctx) : current_(rolltui_bindings_clone(rolltui_bindings_default(ctx))) {
  undo_.reset(current_);
  rebuild_menu();
}

void KeysEditor::load(const RolltuiBindings* b) {
  current_ = KeyTable(rolltui_bindings_clone(b));
  undo_.reset(current_);
  capture_.reset();
  rebuild_menu();
  status_ = "loaded";
}

void KeysEditor::set_presets(std::vector<std::string> names) {
  presets_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : presets_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "load", std::move(opts));
}

void KeysEditor::set_shipped(std::vector<std::string> names, bool may_write) {
  shipped_ = std::move(names);
  may_write_shipped_ = may_write;
  std::vector<MenuItem> opts;
  for (const std::string& n : shipped_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "write_shipped", std::move(opts));
  set_enabled(menu_, "write_shipped", may_write && !shipped_.empty());
}

const std::string& KeysEditor::capturing_action() const {
  static const std::string none;
  return capture_ ? *capture_ : none;
}

void KeysEditor::rebuild_menu() {
  std::vector<MenuItem> scopes;
  for (const char* scope : kScopes) {
    std::vector<MenuItem> actions;
    for (const std::string& a : action_names(current_.get())) {
      if (scope_of_str(a) != scope) continue;
      std::vector<MenuItem> items;
      items.push_back(MenuItem::action(std::string("bind." + a).c_str(), "add a chord (press it)\xE2\x80\xA6"));
      for (const RolltuiChord& k : chords_for(current_.get(), a))
        items.push_back(MenuItem::action(std::string("unbind." + a + "." + chord_str(k)).c_str(), std::string("remove " + chord_disp(k)).c_str()));
      items.push_back(MenuItem::action(std::string("clear." + a).c_str(), "clear every chord"));
      const std::string chords = chords_text(current_.get(), a);
      actions.push_back(submenu_of(("action." + a).c_str(), (a.substr(a.find('.') + 1) + "  " + (chords.empty() ? "(unbound)" : chords)).c_str(), std::move(items)));
    }
    scopes.push_back(submenu_of((std::string("scope.") + scope).c_str(), scope, std::move(actions)));
  }
  std::vector<MenuItem> loads, ships;
  for (const std::string& n : presets_) loads.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  for (const std::string& n : shipped_) ships.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  InputSpec name;
  name.type = InputType::Name;
  std::vector<MenuItem> top;
  top.push_back(submenu_of("scopes", "Actions by scope", std::move(scopes)));
  top.push_back(MenuItem::action("undo", "Undo", "Ctrl-Z"));
  top.push_back(MenuItem::action("redo", "Redo", "Ctrl-Y"));
  top.push_back(choice_of("load", "Load preset", std::move(loads), ""));
  top.push_back(MenuItem::input("save", "Save as preset", name.clone()));
  top.push_back(choice_of("write_shipped", "Write a SHIPPED preset (the editor's privilege)", std::move(ships), ""));
  top.push_back(MenuItem::action("reset_loaded", "Reset to the loaded preset\xE2\x80\xA6"));
  MenuItem root = submenu_of("root", "keys editor", std::move(top));
  rolltui_menu_set_root(menu_, &root);
  set_enabled(menu_, "write_shipped", may_write_shipped_ && !shipped_.empty());
}

// Refreshes one action's level in place (its label and its remove items), keeping the
// navigation where it is.
void KeysEditor::rebuild_action(std::string_view action) {
  std::vector<MenuItem> items;
  const std::string a(action);
  items.push_back(MenuItem::action(std::string("bind." + a).c_str(), "add a chord (press it)\xE2\x80\xA6"));
  for (const RolltuiChord& k : chords_for(current_.get(), a))
    items.push_back(MenuItem::action(std::string("unbind." + a + "." + chord_str(k)).c_str(), std::string("remove " + chord_disp(k)).c_str()));
  items.push_back(MenuItem::action(std::string("clear." + a).c_str(), "clear every chord"));
  const std::string level = "action." + a;
  set_options(menu_, level, std::move(items));
  if (MenuItem* it = rolltui_menu_find(menu_, level.c_str(), level.size())) {
    const std::string chords = chords_text(current_.get(), a);
    set_str(it->label, a.substr(a.find('.') + 1) + "  " + (chords.empty() ? "(unbound)" : chords));
  }
}

KeysEditor::Outcome KeysEditor::commit() {
  if (current_ == undo_.current()) return {Outcome::Kind::Changed, {}};
  undo_.commit(current_);
  return {Outcome::Kind::Committed, {}};
}

void KeysEditor::replace(RolltuiBindings* b) {
  capture_.reset();
  current_ = KeyTable(b);  // adopts
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

void KeysEditor::status_line(std::string& out) const {
  // While capturing, a refusal (an unnameable key) is shown WITH the prompt, so the
  // reason and what to do next are both on the line.
  out.clear();
  if (capture_) {
    if (!status_.empty()) { out += status_; out += " \xE2\x80\x94 "; }
    out += "press the chord for ";
    out += *capture_;
    out += " (Esc cancels)";
  } else if (status_.empty()) {
    out += "Enter on an action: add, remove or clear its chords";
  } else {
    out += status_;
  }
  out += " \xC2\xB7 undo ";
  append_count(out, undo_.undo_depth());
  out += " \xC2\xB7 redo ";
  append_count(out, undo_.redo_depth());
}
std::string KeysEditor::status_line() const {
  std::string s;
  status_line(s);
  return s;
}

KeysEditor::Outcome KeysEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  using O = Outcome::Kind;
  if (capture_) {
    if (e->kind != ROLLTUI_EVENT_KEY) return {O::None, {}};
    const RolltuiChord k = e->key;
    if (k.key == ROLLTUI_KEY_ESCAPE && !k.ctrl && !k.alt && !k.shift) { capture_.reset(); status_ = "capture cancelled"; return {O::Changed, {}}; }
    if (k.key == ROLLTUI_KEY_UNKNOWN) { status_ = "that key has no chord name; try another"; return {O::Changed, {}}; }
    const std::string action = *capture_;
    capture_.reset();
    const char* moved_from_p = nullptr;
    std::size_t moved_from_len = 0;
    if (!rolltui_bindings_bind(current_.get(), action.c_str(), action.size(), &k, &moved_from_p, &moved_from_len)) {
      status_ = "refused: " + chord_disp(k) + " cannot be bound to " + action + (k.key == ROLLTUI_KEY_ENTER ? " (Enter is always input.submit)" : "");
      return {O::Changed, {}};
    }
    const std::string moved = moved_from_p ? std::string(moved_from_p, moved_from_len) : std::string();
    // Short, for a 48-column popup: the scope is in the breadcrumb already.
    auto verb = [](const std::string& a) { return a.substr(a.find('.') + 1); };
    status_ = "bound " + chord_disp(k) + " \xE2\x86\x92 " + verb(action) + (moved.empty() ? "" : " (was " + verb(moved) + ")");
    rebuild_action(action);
    if (!moved.empty()) rebuild_action(moved);
    return commit();
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    const RolltuiChord k = e->key;
    std::size_t len = 0;
    const char* ed_p = rolltui_bindings_action_for(nav, &k, "editor", 6, &len);
    const std::string_view ed = ed_p ? std::string_view(ed_p, len) : std::string_view();
    if (ed == "editor.undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ed == "editor.redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
  }
  status_.clear();
  RolltuiMenuEvent raw{};
  rolltui_menu_handle(menu_, e, nav, rolltui_menu_default_actions(), &raw);
  const unsigned char kind = raw.kind;
  const std::string id = str_of(raw.id);
  const std::string value = str_of(raw.value);
  rolltui_menu_event_release(&raw);
  if (kind == ROLLTUI_MENU_EVENT_ACTIVATE) {
    if (id.rfind("bind.", 0) == 0) { capture_ = id.substr(5); status_.clear(); return {O::Changed, {}}; }
    if (id.rfind("unbind.", 0) == 0) {
      const std::string rest = id.substr(7);
      // "<action>.<chord>": the action has one dot; the chord is what follows the second.
      const std::size_t first = rest.find('.');
      const std::size_t second = rest.find('.', first + 1);
      if (second == std::string::npos) return {O::None, {}};
      const std::string action = rest.substr(0, second), chord = rest.substr(second + 1);
      RolltuiChord k{};
      if (rolltui_chord_parse(chord.c_str(), chord.size(), &k)) {
        if (!rolltui_bindings_unbind(current_.get(), action.c_str(), action.size(), &k)) {
          status_ = "refused: " + chord_disp(k) + " stays on " + action;
          return {O::Changed, {}};
        }
        status_ = "removed " + chord_disp(k) + " from " + action;
        rebuild_action(action);
        return commit();
      }
      return {O::None, {}};
    }
    if (id.rfind("clear.", 0) == 0) {
      const std::string action = id.substr(6);
      rolltui_bindings_clear(current_.get(), action.c_str(), action.size());
      status_ = action == "input.submit" ? "input.submit keeps Enter" : "cleared " + action;
      rebuild_action(action);
      return commit();
    }
    if (id == "undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "reset_loaded") return {O::ResetLoaded, {}};
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    if (id == "load") return {O::LoadPreset, value};
    if (id == "write_shipped") return {O::WriteShipped, value};
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_INPUT && id == "save") return {O::SaveAs, value};
  if (kind == ROLLTUI_MENU_EVENT_CLOSED) return {O::Closed, {}};
  return {O::None, {}};
}

}  // namespace rolltui::tools
