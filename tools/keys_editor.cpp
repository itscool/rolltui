// rolltui/tools/keys_editor.cpp — see keys_editor.hpp. Everything here forwards to
// `rolltui/c/rolltui_keys_editor.h`; the only real work is turning the model's borrows into the
// std:: types a C++ host composes with.
#include "keys_editor.hpp"

#include "rolltui/c/rolltui_str.h"
#include "tool_str.hpp"

namespace rolltui::tools {

// The C++ enum IS the C's numbering — cast, never translated, so a new outcome cannot be
// silently mismapped by a switch nobody updated.
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::None) == ROLLTUI_KEYS_EDIT_NONE);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::Changed) == ROLLTUI_KEYS_EDIT_CHANGED);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::Committed) == ROLLTUI_KEYS_EDIT_COMMITTED);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::SaveAs) == ROLLTUI_KEYS_EDIT_SAVE_AS);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::WriteShipped) == ROLLTUI_KEYS_EDIT_WRITE_SHIPPED);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::LoadPreset) == ROLLTUI_KEYS_EDIT_LOAD_PRESET);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::ResetLoaded) == ROLLTUI_KEYS_EDIT_RESET_LOADED);
static_assert(static_cast<int>(KeysEditor::Outcome::Kind::Closed) == ROLLTUI_KEYS_EDIT_CLOSED);

// A `RolltuiStrList` lives for the length of one call: the model COPIES what it is handed.
void KeysEditor::set_presets(const std::vector<std::string>& names) {
  RolltuiStrList l{};
  for (const std::string& n : names) rolltui_str_list_add(&l, n.data(), n.size());
  rolltui_keys_editor_set_presets(e_, &l);
  rolltui_str_list_release(&l);
}

void KeysEditor::set_shipped(const std::vector<std::string>& names, bool may_write) {
  RolltuiStrList l{};
  for (const std::string& n : names) rolltui_str_list_add(&l, n.data(), n.size());
  rolltui_keys_editor_set_shipped(e_, &l, may_write ? 1 : 0);
  rolltui_str_list_release(&l);
}

std::string_view KeysEditor::capturing_action() const {
  std::size_t len = 0;
  const char* p = rolltui_keys_editor_capturing_action(e_, &len);
  return std::string_view(p ? p : "", len);
}

KeysEditor::Outcome KeysEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  RolltuiKeysEditorOutcome raw{};
  rolltui_keys_editor_handle(e_, e, nav, &raw);
  Outcome out;
  out.kind = static_cast<Outcome::Kind>(raw.kind);
  out.value.assign(raw.value.p ? raw.value.p : "", raw.value.n);
  rolltui_keys_editor_outcome_release(&raw);
  return out;
}

void KeysEditor::status_line(std::string& out) const {
  RolltuiStr s{};
  rolltui_keys_editor_status_line(e_, &s);
  out.assign(s.p ? s.p : "", s.n);
  rolltui_str_free(&s);
}

std::string KeysEditor::status_line() const {
  std::string s;
  status_line(s);
  return s;
}

}  // namespace rolltui::tools
