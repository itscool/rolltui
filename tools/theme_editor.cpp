// rolltui/tools/theme_editor.cpp — see theme_editor.hpp.
#include "tool_str.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_TESTS (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"
#include "theme_editor.hpp"

#include <algorithm>
#include <cstdlib>

namespace rolltui::tools {

namespace {

const char* kAttrs[] = {"bold", "italic", "underline", "dim", "reverse"};

// `unsigned char&` and not `bool&` since Phase 14 m2: a Style's attribute bits are the C
// struct's bytes now (rolltui/c/rolltui_style.h says why they are not `_Bool`).
unsigned char& attr_of(Style& s, std::string_view name) {
  if (name == "bold") return s.bold;
  if (name == "italic") return s.italic;
  if (name == "underline") return s.underline;
  if (name == "dim") return s.dim;
  return s.reverse;
}
bool attr_value(const Style& s, std::string_view name) {
  if (name == "bold") return s.bold;
  if (name == "italic") return s.italic;
  if (name == "underline") return s.underline;
  if (name == "dim") return s.dim;
  return s.reverse;
}

// ---- menu tree glue (mechanical; the identical shape keys_editor.cpp / layout_editor.cpp
// already carry — see either's comment for why this is not a header of its own). -------------
void set_options(RolltuiMenu* m, std::string_view id, std::vector<MenuItem>&& options) {
  RolltuiMenuItemList list;
  for (MenuItem& it : options) list.push_back(std::move(it));
  rolltui_menu_set_options(m, id.data(), id.size(), &list);
}
void set_enabled(RolltuiMenu* m, std::string_view id, bool enabled) { rolltui_menu_set_enabled(m, id.data(), id.size(), enabled ? 1 : 0); }
void set_value(RolltuiMenu* m, std::string_view id, std::string_view value) {
  rolltui_menu_set_value(m, id.data(), id.size(), value.data(), value.size());
}
void set_checked(RolltuiMenu* m, std::string_view id, bool checked) { rolltui_menu_set_checked(m, id.data(), id.size(), checked ? 1 : 0); }
MenuItem* find(RolltuiMenu* m, std::string_view id) { return rolltui_menu_find(m, id.data(), id.size()); }

// ---- role names: direct, bounded C calls. --------------------------------------------------
std::string role_name(unsigned char r) {
  std::size_t len = 0;
  const char* p = rolltui_role_name(r, &len);
  return p ? std::string(p, len) : std::string();
}
std::optional<unsigned char> role_from_name(std::string_view name) {
  const int r = rolltui_role_from_name(name.data(), name.size());
  return r >= 0 ? std::optional<unsigned char>(static_cast<unsigned char>(r)) : std::nullopt;
}

// ---- colour text: direct, bounded C calls. -------------------------------------------------
std::optional<Color> parse_color(std::string_view text) {
  Color c{};
  if (!rolltui_color_parse(text.data(), text.size(), &c)) return std::nullopt;
  return c;
}
std::string color_to_string(Color c) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  return std::string(buf, rolltui_color_to_string(c, buf, sizeof buf));
}

// A variant's own "meta" out of an already-parsed colours tree — rolltui_theme_load never
// touches "meta" (rolltui_theme.h's own header comment: it and "name" are the two fields
// that stay outside that file), so this mirrors Theme.cpp's `theme_from_c_root` exactly:
// clone whatever "meta" object is present, then resolve a colour-pair-shaped "badges"
// ({"dark":[...], "light":[...]}) down to the one list for `mode`, the same way a role's
// fg/bg pair resolves.
RolltuiJsonValue* extract_meta(const RolltuiJsonValue* colours, unsigned char mode) {
  const RolltuiJsonValue* src = rolltui_json_get(colours, "meta", 4);
  if (!rolltui_json_is_object(src)) return nullptr;
  RolltuiJsonValue* meta = rolltui_json_clone(src);
  const RolltuiJsonValue* b = rolltui_json_get(meta, "badges", 6);
  const char* key = mode == ROLLTUI_MODE_DARK ? "dark" : "light";
  if (rolltui_json_is_object(b) && rolltui_json_has(b, key, std::string_view(key).size()))
    rolltui_json_set(meta, "badges", 6, rolltui_json_clone(rolltui_json_get(b, key, std::string_view(key).size())));
  return meta;
}

// ---- one theme's styles + effects, built from a built-in name. The light variant's effect
// map is never consulted (rolltui_theme.h states the asymmetry: "effects" is always dark's
// alone), so a caller filling the light variant frees what comes back unread.
RolltuiEffectMap* fill_builtin(std::string_view name, std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT>& styles) {
  return rolltui_theme_builtin_fill(name.data(), name.size(), styles.data(), ROLLTUI_ROLE_COUNT);
}

}  // namespace

unsigned char ThemeEditor::default_effect_fallback_role() {
  // rolltui::EffectMap's own default constructor's fallback (Effects.hpp) — accent_1.
  const int r = rolltui_role_from_name("accent_1", 8);
  return r >= 0 ? static_cast<unsigned char>(r) : 0;
}

ThemeEditor::ThemeEditor() {
  rolltui_effect_map_free(dark_effects_);
  dark_effects_ = fill_builtin("default-dark", current_.dark);
  current_.dark_name = "default-dark";
  rolltui_effect_map_free(fill_builtin("default-light", current_.light));
  current_.light_name = "default-light";
  undo_.reset(current_);
  rebuild_palette();
  rebuild_menu();
}

std::optional<ThemeEditor::Field> ThemeEditor::field_of(std::string_view id) {
  if (id.rfind("role.", 0) != 0) return std::nullopt;
  std::string_view rest = id.substr(5);
  const std::size_t dot = rest.find('.');
  if (dot == std::string_view::npos) return std::nullopt;
  const std::optional<unsigned char> r = role_from_name(rest.substr(0, dot));
  if (!r) return std::nullopt;
  std::string_view f = rest.substr(dot + 1);
  if (f.size() > 7 && f.substr(f.size() - 7) == ".custom") f = f.substr(0, f.size() - 7);
  return Field{*r, std::string(f)};
}

bool ThemeEditor::load(const RolltuiJsonValue* colours, RolltuiThemeReport* report) {
  std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> d{}, l{};
  RolltuiStr d_name{}, l_name{};
  RolltuiThemeReport light_rep{};
  RolltuiEffectMap* d_eff = rolltui_theme_load(colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), d.data(), &d_name, report);
  RolltuiEffectMap* l_eff = rolltui_theme_load(colours, ROLLTUI_MODE_LIGHT, rolltui_theme_default_vocab(), l.data(), &l_name, &light_rep);
  rolltui_theme_report_release(&light_rep);
  if (!d_eff || !l_eff) {
    rolltui_str_free(&d_name);
    rolltui_str_free(&l_name);
    rolltui_effect_map_free(d_eff);
    rolltui_effect_map_free(l_eff);
    return false;
  }
  current_.dark = d;
  current_.light = l;
  current_.dark_name = str_of(d_name);
  current_.light_name = str_of(l_name);
  rolltui_str_free(&d_name);
  rolltui_str_free(&l_name);
  rolltui_json_free(current_.dark_meta);
  current_.dark_meta = extract_meta(colours, ROLLTUI_MODE_DARK);
  rolltui_json_free(current_.light_meta);
  current_.light_meta = extract_meta(colours, ROLLTUI_MODE_LIGHT);
  rolltui_effect_map_free(dark_effects_);
  dark_effects_ = d_eff;
  rolltui_effect_map_free(l_eff);  // discarded: only dark's effects are ever dumped
  rolltui_json_free(defs_);
  defs_ = rolltui_json_clone(rolltui_json_get(colours, "defs", 4));
  undo_.reset(current_);
  preview_.reset();
  rebuild_palette();
  rebuild_menu();
  status_ = "loaded";
  return true;
}

void ThemeEditor::set_presets(std::vector<std::string> names) {
  presets_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : presets_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "load", std::move(opts));
}

void ThemeEditor::set_shipped(std::vector<std::string> names, bool may_write) {
  shipped_ = std::move(names);
  may_write_shipped_ = may_write;
  std::vector<MenuItem> opts;
  for (const std::string& n : shipped_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "write_shipped", std::move(opts));
  set_enabled(menu_, "write_shipped", may_write && !shipped_.empty());
}

void ThemeEditor::set_mode(unsigned char m) {
  if (preview_) cancel_preview();
  mode_ = m;
  set_value(menu_, "mode", m == ROLLTUI_MODE_DARK ? "dark" : "light");
  sync_values();
  refresh_fixes();
}

RolltuiJsonValue* ThemeEditor::colours_json(std::string_view name) const {
  // theme_pair_to_json_value's algorithm (Theme.cpp), verbatim: {"name", ["meta"], "roles",
  // ["effects"]} built in THAT order — rolltui_theme_dump only ever returns {roles,
  // [effects]}, so "name" (and "meta") are written before it, not appended after: a preset
  // store compares this tree against what it loaded, and a key-order difference alone
  // reads as "modified" (studio_golden_test's editor.*.heading-undo caught this).
  const ThemeEdit& c = undo_.current();
  RolltuiJsonValue* root = rolltui_json_object();
  rolltui_json_set(root, "name", 4, rolltui_json_string(name.data(), name.size()));
  if (c.dark_meta && rolltui_json_is_object(c.dark_meta)) {
    RolltuiJsonValue* meta = rolltui_json_clone(c.dark_meta);
    // Each variant's claimed badges, as a pair when they differ.
    if (c.light_meta && rolltui_json_is_object(c.light_meta)) {
      const RolltuiJsonValue* db = rolltui_json_get(c.dark_meta, "badges", 6);
      const RolltuiJsonValue* lb = rolltui_json_get(c.light_meta, "badges", 6);
      if (!rolltui_json_equal(db, lb)) {
        RolltuiJsonValue* pair = rolltui_json_object();
        rolltui_json_set(pair, "dark", 4, rolltui_json_clone(db));
        rolltui_json_set(pair, "light", 5, rolltui_json_clone(lb));
        rolltui_json_set(meta, "badges", 6, pair);
      }
    }
    rolltui_json_set(root, "meta", 4, meta);
  }
  // ONE effects object for both variants (motion is the theme's, not the terminal
  // background's): rolltui_theme_dump takes a light effects map only to document that it
  // is never consulted, and always dumps dark's alone.
  RolltuiJsonValue* dump = rolltui_theme_dump(c.dark.data(), dark_effects_, c.light.data(), nullptr, rolltui_theme_default_vocab());
  rolltui_json_set(root, "roles", 5, rolltui_json_clone(rolltui_json_get(dump, "roles", 5)));
  const RolltuiJsonValue* fx = rolltui_json_get(dump, "effects", 7);
  if (!rolltui_json_is_null(fx)) rolltui_json_set(root, "effects", 7, rolltui_json_clone(fx));
  rolltui_json_free(dump);
  return root;
}

void ThemeEditor::rebuild_palette() {
  palette_.clear();
  auto has = [&](const std::string& id) {
    return std::any_of(palette_.begin(), palette_.end(), [&](const PaletteEntry& p) { return p.id == id; });
  };
  // defs names for labels: a defs entry that is a plain colour string.
  std::vector<std::pair<std::string, std::string>> names;  // colour id → def name
  for (std::size_t i = 0, n = defs_ ? rolltui_json_object_size(defs_) : 0; i < n; ++i) {
    std::size_t klen = 0;
    const char* k = rolltui_json_object_key_at(defs_, i, &klen);
    const RolltuiJsonValue* v = rolltui_json_object_value_at(defs_, i);
    if (!rolltui_json_is_string(v)) continue;
    std::size_t vlen = 0;
    const char* vs = rolltui_json_as_string(v, "", 0, &vlen);
    if (std::optional<Color> c = parse_color(std::string_view(vs, vlen))) names.emplace_back(color_to_string(*c), std::string(k, klen));
  }
  auto label_for = [&](const std::string& id) {
    for (const auto& [cid, n] : names)
      if (cid == id) return n + " = " + id;
    return id;
  };
  palette_.push_back({Color::none(), "none", "none"});
  for (const std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT>* t : {&current_.dark, &current_.light})
    for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
      const RolltuiStyle& s = (*t)[i];
      for (Color c : {s.fg, s.bg}) {
        const std::string id = color_to_string(c);
        if (!has(id)) palette_.push_back({c, id, label_for(id)});
      }
    }
}

void ThemeEditor::rebuild_menu() {
  std::vector<MenuItem> palette_opts;
  for (const PaletteEntry& p : palette_) palette_opts.push_back(MenuItem::action(std::string(p.id).c_str(), std::string(p.label).c_str()));
  std::vector<MenuItem> roles;
  for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    const std::string name = role_name(static_cast<unsigned char>(i));
    const std::string base = "role." + name;
    std::vector<MenuItem> fields;
    fields.push_back(choice_of((base + ".fg").c_str(), "fg", clone_items(palette_opts), ""));
    fields.push_back(choice_of((base + ".bg").c_str(), "bg", clone_items(palette_opts), ""));
    InputSpec colour;
    colour.type = InputType::Color;
    fields.push_back(MenuItem::input((base + ".fg.custom").c_str(), "custom fg", colour.clone()));
    fields.push_back(MenuItem::input((base + ".bg.custom").c_str(), "custom bg", colour.clone()));
    for (const char* a : kAttrs) fields.push_back(MenuItem::toggle(std::string(base + "." + a).c_str(), std::string(a).c_str(), false));
    roles.push_back(submenu_of(base.c_str(), name.c_str(), std::move(fields)));
  }
  std::vector<MenuItem> load_opts, shipped_opts;
  for (const std::string& n : presets_) load_opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  for (const std::string& n : shipped_) shipped_opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  std::vector<MenuItem> rulesets;
  for (unsigned char r = 0; r < ROLLTUI_RULESET_COUNT; ++r) {
    std::size_t len = 0;
    const char* rn = rolltui_ruleset_name(r, &len);
    rulesets.push_back(MenuItem::action(std::string(std::string(rn, len)).c_str(), std::string(std::string(rn, len)).c_str()));
  }
  InputSpec seed, chaos, name;
  seed.type = InputType::Int;
  seed.min = 0;
  chaos.type = InputType::Float;
  chaos.min = 0;
  chaos.max = 1;
  chaos.step = 0.1;
  chaos.precision = 2;
  name.type = InputType::Name;
  std::vector<MenuItem> top;
  top.push_back(submenu_of("roles", "Roles", std::move(roles)));
  {
    std::vector<MenuItem> modes;
    modes.push_back(MenuItem::action("dark", "dark"));
    modes.push_back(MenuItem::action("light", "light"));
    top.push_back(choice_of("mode", "Mode (edit + preview)", std::move(modes), mode_ == ROLLTUI_MODE_DARK ? "dark" : "light"));
  }
  top.push_back(MenuItem::action("check", "Check: contrast, colour-vision, badges"));
  top.push_back(MenuItem::submenu("fixes", "Fixes (proposals; Enter applies one, undoable)"));
  {
    std::vector<MenuItem> gen;
    gen.push_back(choice_of("gen.ruleset", "Ruleset", std::move(rulesets), "analogous"));
    gen.push_back(MenuItem::input("gen.seed", "Seed", seed.clone(), "1"));
    gen.push_back(MenuItem::input("gen.chaos", "Chaos", chaos.clone(), "0"));
    gen.push_back(MenuItem::action("gen.run", "Generate (replaces both variants, undoable)"));
    top.push_back(submenu_of("generate", "Generate a theme (seeded)", std::move(gen)));
  }
  top.push_back(MenuItem::action("undo", "Undo", "Ctrl-Z"));
  top.push_back(MenuItem::action("redo", "Redo", "Ctrl-Y"));
  top.push_back(choice_of("load", "Load preset", std::move(load_opts), ""));
  top.push_back(MenuItem::input("save", "Save as preset", name.clone()));
  top.push_back(choice_of("write_shipped", "Write a SHIPPED preset (the editor's privilege)", std::move(shipped_opts), ""));
  top.push_back(MenuItem::action("reset_loaded", "Reset to the loaded preset\xE2\x80\xA6"));
  top.push_back(MenuItem::action("reset_builtin", "Reset to the built-in default\xE2\x80\xA6"));
  MenuItem root = submenu_of("root", "theme editor", std::move(top));
  // A rebuild (a load, a replace) starts at the top level; a committed custom colour
  // only refreshes the option lists in place (commit_current), keeping the position.
  rolltui_menu_set_root(menu_, &root);
  set_enabled(menu_, "write_shipped", may_write_shipped_ && !shipped_.empty());
  sync_values();
  refresh_fixes();
}

void ThemeEditor::refresh_fixes() {
  RolltuiFixArray fixes{};
  rolltui_propose_fixes((mode_ == ROLLTUI_MODE_DARK ? undo_.current().dark : undo_.current().light).data(), ROLLTUI_ROLE_COUNT, rolltui_theme_default_vocab(), &fixes);
  // Deep-copies each RolltuiFix (its one RolltuiStr member, `what`, has a real C++ copy
  // constructor, so this clones rather than aliasing); the ORIGINAL array — its own
  // `what` buffers included — is then released in full, the same as any other owned
  // array this file is handed.
  fixes_.clear();
  for (std::size_t i = 0; i < fixes.n; ++i) {
    RolltuiFix f;
    f.role = fixes.v[i].role;
    f.before = fixes.v[i].before;
    f.after = fixes.v[i].after;
    f.before_value = fixes.v[i].before_value;
    f.after_value = fixes.v[i].after_value;
    f.what = std::move(fixes.v[i].what);  // taken, not copied; the array's release frees the emptied one
    fixes_.push_back(std::move(f));
  }
  rolltui_fix_array_release(&fixes);
  std::vector<MenuItem> items;
  for (std::size_t i = 0; i < fixes_.size(); ++i) items.push_back(MenuItem::action(std::string("fix." + std::to_string(i)).c_str(), std::string(str_of(fixes_[i].what)).c_str()));
  if (items.empty()) { items.push_back(MenuItem::action("fix.none", "(nothing to fix in this variant)")); items.back().enabled = false; }
  set_options(menu_, "fixes", std::move(items));
}

std::string ThemeEditor::badges_line() const {
  std::vector<RolltuiRoleCheck> roles(ROLLTUI_ROLE_COUNT);
  std::vector<RolltuiPairCheck> pairs(rolltui_must_differ_count());
  RolltuiBadges badges{};
  rolltui_theme_analyse(current(), ROLLTUI_ROLE_COUNT, roles.data(), pairs.data(), &badges);
  RolltuiStrArray names{};
  rolltui_badge_names(&badges, &names);
  std::string s = "badges:";
  for (std::size_t i = 0; i < names.n; ++i) s += " " + str_of(names.v[i]);
  if (names.n == 0) s += " (none)";
  rolltui_str_array_release(&names);
  return s;
}

std::string ThemeEditor::report() const {
  std::vector<RolltuiRoleCheck> roles(ROLLTUI_ROLE_COUNT);
  std::vector<RolltuiPairCheck> pairs(rolltui_must_differ_count());
  RolltuiBadges badges{};
  rolltui_theme_analyse(current(), ROLLTUI_ROLE_COUNT, roles.data(), pairs.data(), &badges);
  RolltuiStr out{};
  rolltui_theme_report_text(roles.data(), ROLLTUI_ROLE_COUNT, pairs.data(), pairs.size(), &badges, nullptr, 0, rolltui_theme_default_vocab(), &out);
  const std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}

void ThemeEditor::sync_values() {
  const std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT>& t = mode_ == ROLLTUI_MODE_DARK ? undo_.current().dark : undo_.current().light;
  for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    const std::string base = "role." + role_name(static_cast<unsigned char>(i));
    const Style& s = t[i];
    set_value(menu_, base + ".fg", color_to_string(s.fg));
    set_value(menu_, base + ".bg", color_to_string(s.bg));
    for (const char* a : kAttrs) set_checked(menu_, base + "." + a, attr_value(s, a));
  }
  set_value(menu_, "mode", mode_ == ROLLTUI_MODE_DARK ? "dark" : "light");
}

void ThemeEditor::apply(Field f, Color c) {
  Style& s = style_at(f.role);
  if (f.name == "fg") s.fg = c;
  else if (f.name == "bg") s.bg = c;
}

void ThemeEditor::begin_preview() {
  if (!preview_) preview_ = current_;
}

void ThemeEditor::cancel_preview() {
  if (!preview_) return;
  current_ = *preview_;
  preview_.reset();
}

ThemeEditor::Outcome ThemeEditor::commit_current() {
  preview_.reset();
  if (current_ == undo_.current()) return {Outcome::Kind::Changed, {}};
  undo_.commit(current_);
  const std::size_t before = palette_.size();
  rebuild_palette();
  if (palette_.size() != before) {
    std::vector<MenuItem> opts;
    for (const PaletteEntry& p : palette_) opts.push_back(MenuItem::action(std::string(p.id).c_str(), std::string(p.label).c_str()));
    for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
      const std::string base = "role." + role_name(static_cast<unsigned char>(i));
      set_options(menu_, base + ".fg", clone_items(opts));
      set_options(menu_, base + ".bg", clone_items(opts));
    }
  }
  sync_values();
  refresh_fixes();
  return {Outcome::Kind::Committed, {}};
}

void ThemeEditor::replace(ThemeEdit e, RolltuiEffectMap* dark_effects) {
  preview_.reset();
  current_ = std::move(e);
  if (dark_effects) {
    rolltui_effect_map_free(dark_effects_);
    dark_effects_ = dark_effects;
  }
  undo_.commit(current_);
  rebuild_palette();
  rebuild_menu();
}

bool ThemeEditor::undo() {
  cancel_preview();
  if (!undo_.undo()) return false;
  current_ = undo_.current();
  sync_values();
  refresh_fixes();
  status_ = "undone";
  return true;
}

bool ThemeEditor::redo() {
  cancel_preview();
  if (!undo_.redo()) return false;
  current_ = undo_.current();
  sync_values();
  refresh_fixes();
  status_ = "redone";
  return true;
}

std::optional<unsigned char> ThemeEditor::focused_role() const {
  auto role_in = [](std::string_view id) -> std::optional<unsigned char> {
    if (id.rfind("role.", 0) != 0) return std::nullopt;
    std::string_view rest = id.substr(5);
    const std::size_t dot = rest.find('.');
    return role_from_name(dot == std::string_view::npos ? rest : rest.substr(0, dot));
  };
  if (auto r = role_in(view_of(rolltui_menu_level(menu_)->id))) return r;
  if (const MenuItem* it = rolltui_menu_selected_item(menu_))
    if (auto r = role_in(view_of(it->id))) return r;
  return std::nullopt;
}

std::optional<Color> ThemeEditor::highlighted_color() const {
  const MenuItem* it = rolltui_menu_selected_item(menu_);
  if (!it) return std::nullopt;
  const bool editing = rolltui_menu_editing(menu_) != 0;
  if (editing && it->id.size() > 7 && view_of(it->id).substr(it->id.size() - 7) == ".custom") {
    std::size_t len = 0;
    const char* p = rolltui_input_text(rolltui_menu_editor(menu_), &len);
    return parse_color(std::string_view(p, len));
  }
  if (const std::optional<Field> f = field_of(view_of(rolltui_menu_level(menu_)->id)); f && (f->name == "fg" || f->name == "bg")) return parse_color(view_of(it->id));
  return std::nullopt;
}

void ThemeEditor::status_line(std::string& out) const {
  const bool editing = rolltui_menu_editing(menu_) != 0;
  std::size_t reason_len = 0;
  const char* reason = rolltui_menu_edit_reason(menu_, &reason_len);
  out.clear();
  if (editing && reason_len > 0) { out += "refused: "; out.append(reason, reason_len); }
  else if (preview_) out += "previewing \xE2\x80\x94 Enter commits, Esc cancels";
  else if (status_.empty()) out += "Enter commits, Esc cancels";
  else out += status_;
  out += " \xC2\xB7 undo ";
  append_count(out, undo_.undo_depth());
  out += " \xC2\xB7 redo ";
  append_count(out, undo_.redo_depth());
  out += " \xC2\xB7 ";
  out += mode_ == ROLLTUI_MODE_DARK ? "dark" : "light";
}
std::string ThemeEditor::status_line() const {
  std::string s;
  status_line(s);
  return s;
}

ThemeEditor::Outcome ThemeEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  using O = Outcome::Kind;
  if (e->kind == ROLLTUI_EVENT_KEY) {
    // An undo or redo changes the COMMITTED value: the host writes it to the store.
    std::size_t len = 0;
    const char* ed_p = rolltui_bindings_action_for(nav, &e->key, "editor", 6, &len);
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
  const bool checked = raw.checked != 0;
  rolltui_menu_event_release(&raw);
  // ---- committing events ----
  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    if (const std::optional<Field> f = field_of(id)) {
      if (std::optional<Color> c = parse_color(value)) { begin_preview(); apply(*f, *c); }
      return commit_current();
    }
    if (id == "mode") { set_mode(value == "light" ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK); return {O::Changed, {}}; }
    if (id == "load") return {O::LoadPreset, value};
    if (id == "write_shipped") return {O::WriteShipped, value};
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_TOGGLE) {
    if (const std::optional<Field> f = field_of(id)) {
      begin_preview();
      attr_of(style_at(f->role), f->name) = checked;
      return commit_current();
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_INPUT) {
    if (id == "save") return {O::SaveAs, value};
    if (const std::optional<Field> f = field_of(id)) {
      if (std::optional<Color> c = parse_color(value)) { begin_preview(); apply(*f, *c); return commit_current(); }
      cancel_preview();
      status_ = "'" + value + "' is not a colour (#rrggbb, 0-255, none)";
      return {O::Changed, {}};
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_ACTIVATE) {
    if (id == "undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "reset_loaded") return {O::ResetLoaded, {}};
    if (id == "reset_builtin") return {O::ResetBuiltin, {}};
    if (id == "check") return {O::Check, {}};
    if (id.rfind("fix.", 0) == 0 && id != "fix.none") {
      const std::size_t i = static_cast<std::size_t>(std::atoi(id.c_str() + 4));
      if (i < fixes_.size()) {
        begin_preview();
        std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT>& t = mode_ == ROLLTUI_MODE_DARK ? current_.dark : current_.light;
        rolltui_apply_fix(t.data(), ROLLTUI_ROLE_COUNT, fixes_[i].role, &fixes_[i].after);
        status_ = "applied: " + str_of(fixes_[i].what);
        Outcome o = commit_current();
        const RolltuiChord left{ROLLTUI_KEY_LEFT};
        const RolltuiEvent left_ev{ROLLTUI_EVENT_KEY, left, {}, nullptr, 0};
        RolltuiMenuEvent discard{};
        rolltui_menu_handle(menu_, &left_ev, nav, rolltui_menu_default_actions(), &discard);  // back to the (refreshed) Fixes level's parent
        rolltui_menu_event_release(&discard);
        return o;
      }
      return {O::None, {}};
    }
    if (id == "gen.run") {
      const MenuItem* ruleset_item = find(menu_, "gen.ruleset");
      unsigned char rs = 0;
      const bool has_rs = ruleset_item && rolltui_ruleset_from_name(ruleset_item->value.data(), ruleset_item->value.size(), &rs);
      const std::uint64_t seed = static_cast<std::uint64_t>(std::strtoull(find(menu_, "gen.seed")->value.c_str(), nullptr, 10));
      const double chaos = std::strtod(find(menu_, "gen.chaos")->value.c_str(), nullptr);
      if (!has_rs) { status_ = "pick a ruleset first"; return {O::Changed, {}}; }
      auto run = [&](int dark_value, std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT>& out_styles, std::string& out_name,
                     RolltuiJsonValue*& out_meta, int* out_repairs, RolltuiBadges* out_badges) {
        RolltuiJsonValue* meta = nullptr;
        std::vector<RolltuiRoleCheck> roles(ROLLTUI_ROLE_COUNT);
        std::vector<RolltuiPairCheck> pairs(rolltui_must_differ_count());
        RolltuiStr name_c{};
        rolltui_theme_generate(seed, rs, chaos, 1, dark_value, /*max_repair_passes=*/20, rolltui_theme_default_vocab(), out_styles.data(), ROLLTUI_ROLE_COUNT,
                               &name_c, &meta, out_repairs, roles.data(), pairs.data(), out_badges);
        out_name = str_of(name_c);
        rolltui_str_free(&name_c);
        out_meta = meta;  // ADOPTED — {"generator": {ruleset, seed, chaos}, "badges": [...]}
        std::string broken;
        for (const RolltuiRoleCheck& c : roles)
          if (c.text && !c.unknown && !c.readable) broken += (broken.empty() ? "" : "; ") + role_name(c.role) + " contrast " + std::to_string(c.wcag).substr(0, 4);
        for (const RolltuiPairCheck& c : pairs)
          if (!c.unknown && !(c.distinct && c.cvd_distinct) && !c.attribute_redundant)
            broken += (broken.empty() ? "" : "; ") + role_name(c.a) + "/" + role_name(c.b) + " confusable";
        return broken;
      };
      ThemeEdit gen{};
      int dark_repairs = 0, light_repairs = 0;
      RolltuiBadges dark_badges{}, light_badges{};
      const std::string dark_broken = run(1, gen.dark, gen.dark_name, gen.dark_meta, &dark_repairs, &dark_badges);
      const std::string light_broken = run(0, gen.light, gen.light_name, gen.light_meta, &light_repairs, &light_badges);
      // A generated theme has no motion of its own: replace() with no effects argument
      // keeps whatever this editor already has, which is wrong here specifically — a
      // fresh, empty map is the honest answer, matching `Generated`'s default-constructed
      // (still-UI) `Theme::effects` the C++ shim never touched either.
      replace(gen, rolltui_effect_map_new(ROLLTUI_EFFECT_STATE_COUNT, default_effect_fallback_role()));
      const bool dark_mode = mode_ == ROLLTUI_MODE_DARK;
      RolltuiStrArray badge_names{};
      rolltui_badge_names(dark_mode ? &dark_badges : &light_badges, &badge_names);
      std::string b;
      for (std::size_t i = 0; i < badge_names.n; ++i) b += " " + str_of(badge_names.v[i]);
      rolltui_str_array_release(&badge_names);
      const std::string& broken = dark_mode ? dark_broken : light_broken;
      status_ = "generated " + (dark_mode ? current_.dark_name : current_.light_name) + " \xE2\x80\x94" + (b.empty() ? " no badges" : b) +
                (broken.empty() ? "" : "; broken: " + broken);
      return {O::Committed, {}};
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_CLOSED) return {O::Closed, {}};
  // ---- live preview: the highlighted palette entry, or a custom colour being typed ----
  const MenuItem* sel = rolltui_menu_selected_item(menu_);
  const bool editing = rolltui_menu_editing(menu_) != 0;
  const std::optional<Field> level_field = field_of(view_of(rolltui_menu_level(menu_)->id));
  if (level_field && (level_field->name == "fg" || level_field->name == "bg") && sel && !editing) {
    if (std::optional<Color> c = parse_color(view_of(sel->id))) {
      begin_preview();
      apply(*level_field, *c);
      return {O::Changed, {}};
    }
  }
  if (editing && sel) {
    if (const std::optional<Field> f = field_of(view_of(sel->id))) {
      begin_preview();
      // The editing text, never the item's value: that is the committed colour.
      std::size_t len = 0;
      const char* p = rolltui_input_text(rolltui_menu_editor(menu_), &len);
      if (std::optional<Color> c = parse_color(std::string_view(p, len))) apply(*f, *c);
      else {  // not (yet) a colour: show the committed value while typing continues
        const std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT>& pt = mode_ == ROLLTUI_MODE_DARK ? preview_->dark : preview_->light;
        apply(*f, f->name == "fg" ? pt[f->role].fg : pt[f->role].bg);
      }
      return {O::Changed, {}};
    }
  }
  // Anywhere else with a preview showing: the focused change was abandoned (Escape,
  // Left, a move away) — the field returns to its committed value.
  if (preview_) { cancel_preview(); return {O::Changed, {}}; }
  return {O::None, {}};
}

}  // namespace rolltui::tools
