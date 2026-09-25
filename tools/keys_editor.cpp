// rolltui/tools/keys_editor.cpp — see keys_editor.hpp. Everything here forwards to
// `rolltui/c/rolltui_keys_editor.h`; the only real work is turning the model's borrows into the
// rolltui types a C++ host composes with.
#include "keys_editor.hpp"

// rolltui_str_list_add (rolltui/c/rolltui_str.h) is curated into rolltui_studio.h, included by
// keys_editor.hpp above.
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
void KeysEditor::set_presets(const StrVec& names) {
  RolltuiStrList l{};
  for (const RolltuiStr& n : names) rolltui_str_list_add(&l, n.data(), n.size());
  rolltui_keys_editor_set_presets(e_, &l);
  rolltui_str_list_release(&l);
}

void KeysEditor::set_shipped(const StrVec& names, bool may_write) {
  RolltuiStrList l{};
  for (const RolltuiStr& n : names) rolltui_str_list_add(&l, n.data(), n.size());
  rolltui_keys_editor_set_shipped(e_, &l, may_write ? 1 : 0);
  rolltui_str_list_release(&l);
}

StrView KeysEditor::capturing_action() const {
  std::size_t len = 0;
  const char* p = rolltui_keys_editor_capturing_action(e_, &len);
  return StrView(p, len);
}

KeysEditor::Outcome KeysEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  RolltuiKeysEditorOutcome raw{};
  rolltui_keys_editor_handle(e_, e, nav, &raw);
  Outcome out;
  out.kind = static_cast<Outcome::Kind>(raw.kind);
  out.value = std::move(raw.value);
  rolltui_keys_editor_outcome_release(&raw);
  return out;
}

void KeysEditor::status_line(RolltuiStr& out) const {
  out.clear();  // the C function APPENDS
  rolltui_keys_editor_status_line(e_, &out);
}

RolltuiStr KeysEditor::status_line() const {
  RolltuiStr s;
  status_line(s);
  return s;
}

}  // namespace rolltui::tools
