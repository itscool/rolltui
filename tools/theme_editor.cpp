// rolltui/tools/theme_editor.cpp — see theme_editor.hpp. Everything here forwards to
// `rolltui/c/rolltui_theme_editor.h`; the only real work is turning the model's borrows into
// the two C++ value types a host asked for.
#include "theme_editor.hpp"

#include "rolltui/c/rolltui_str.h"
#include "tool_str.hpp"

namespace rolltui::tools {

// The C++ enum IS the C's numbering — cast, never translated, so a new outcome cannot be
// silently mismapped by a switch nobody updated.
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::None) == ROLLTUI_THEME_EDIT_NONE);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::Changed) == ROLLTUI_THEME_EDIT_CHANGED);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::Committed) == ROLLTUI_THEME_EDIT_COMMITTED);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::SaveAs) == ROLLTUI_THEME_EDIT_SAVE_AS);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::WriteShipped) == ROLLTUI_THEME_EDIT_WRITE_SHIPPED);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::LoadPreset) == ROLLTUI_THEME_EDIT_LOAD_PRESET);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::ResetLoaded) == ROLLTUI_THEME_EDIT_RESET_LOADED);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::ResetBuiltin) == ROLLTUI_THEME_EDIT_RESET_BUILTIN);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::Check) == ROLLTUI_THEME_EDIT_CHECK);
static_assert(static_cast<int>(ThemeEditor::Outcome::Kind::Closed) == ROLLTUI_THEME_EDIT_CLOSED);

namespace {

std::string owned(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }

// A caller's `RolltuiStr` filled by the model, as a std::string.
std::string filled_by(void (*fn)(const RolltuiThemeEditor*, RolltuiStr*), const RolltuiThemeEditor* e) {
  RolltuiStr s{};
  fn(e, &s);
  std::string out = owned(s);
  rolltui_str_free(&s);
  return out;
}

}  // namespace

// A `RolltuiStrList` lives for the length of one call: the model COPIES what it is handed.
void ThemeEditor::set_presets(const std::vector<std::string>& names) {
  RolltuiStrList l{};
  for (const std::string& n : names) rolltui_str_list_add(&l, n.data(), n.size());
  rolltui_theme_editor_set_presets(e_, &l);
  rolltui_str_list_release(&l);
}

void ThemeEditor::set_shipped(const std::vector<std::string>& names, bool may_write) {
  RolltuiStrList l{};
  for (const std::string& n : names) rolltui_str_list_add(&l, n.data(), n.size());
  rolltui_theme_editor_set_shipped(e_, &l, may_write ? 1 : 0);
  rolltui_str_list_release(&l);
}

ThemeEdit ThemeEditor::committed() const {
  ThemeEdit v;
  std::size_t dn = 0, ln = 0;
  const RolltuiStyle* d = rolltui_theme_editor_styles(e_, ROLLTUI_MODE_DARK, 1);
  const RolltuiStyle* l = rolltui_theme_editor_styles(e_, ROLLTUI_MODE_LIGHT, 1);
  for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    v.dark[i] = d[i];
    v.light[i] = l[i];
  }
  const char* dname = rolltui_theme_editor_variant_name(e_, ROLLTUI_MODE_DARK, &dn);
  const char* lname = rolltui_theme_editor_variant_name(e_, ROLLTUI_MODE_LIGHT, &ln);
  v.dark_name.assign(dname ? dname : "", dn);
  v.light_name.assign(lname ? lname : "", ln);
  v.dark_meta = rolltui_json_clone(rolltui_theme_editor_variant_meta(e_, ROLLTUI_MODE_DARK));
  v.light_meta = rolltui_json_clone(rolltui_theme_editor_variant_meta(e_, ROLLTUI_MODE_LIGHT));
  return v;
}

void ThemeEditor::replace(ThemeEdit v, RolltuiEffectMap* dark_effects) {
  // The metas are ADOPTED by the model, so they leave `v` rather than being freed with it.
  RolltuiJsonValue* dm = v.dark_meta;
  RolltuiJsonValue* lm = v.light_meta;
  v.dark_meta = nullptr;
  v.light_meta = nullptr;
  rolltui_theme_editor_replace(e_, v.dark.data(), v.light.data(), v.dark_name.data(), v.dark_name.size(),
                               v.light_name.data(), v.light_name.size(), dm, lm, dark_effects);
}

PaletteEntry ThemeEditor::palette_at(std::size_t i) const {
  PaletteEntry p{};
  std::size_t id_len = 0, label_len = 0;
  const char* id = rolltui_theme_editor_palette_id(e_, i, &id_len);
  const char* label = rolltui_theme_editor_palette_label(e_, i, &label_len);
  rolltui_theme_editor_palette_color(e_, i, &p.color);
  p.id.assign(id ? id : "", id_len);
  p.label.assign(label ? label : "", label_len);
  return p;
}

ThemeEditor::Outcome ThemeEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  RolltuiThemeEditorOutcome raw{};
  rolltui_theme_editor_handle(e_, e, nav, &raw);
  Outcome out;
  out.kind = static_cast<Outcome::Kind>(raw.kind);
  out.value = owned(raw.value);
  rolltui_theme_editor_outcome_release(&raw);
  return out;
}

void ThemeEditor::status_line(std::string& out) const {
  RolltuiStr s{};
  rolltui_theme_editor_status_line(e_, &s);
  out.assign(s.p ? s.p : "", s.n);
  rolltui_str_free(&s);
}

std::string ThemeEditor::status_line() const { return filled_by(rolltui_theme_editor_status_line, e_); }
std::string ThemeEditor::badges_line() const { return filled_by(rolltui_theme_editor_badges_line, e_); }
std::string ThemeEditor::report() const { return filled_by(rolltui_theme_editor_report, e_); }

}  // namespace rolltui::tools
