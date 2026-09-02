// rolltui/tools/theme_editor.cpp — see theme_editor.hpp.
#include "theme_editor.hpp"

#include <algorithm>

namespace rolltui::tools {

namespace {

const char* kAttrs[] = {"bold", "italic", "underline", "dim", "reverse"};

bool& attr_of(Style& s, std::string_view name) {
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

}  // namespace

ThemeEditor::ThemeEditor() {
  current_.dark = *builtin_theme("default-dark");
  current_.light = *builtin_theme("default-light");
  undo_.reset(current_);
  rebuild_palette();
  rebuild_menu();
}

std::optional<ThemeEditor::Field> ThemeEditor::field_of(std::string_view id) {
  if (id.rfind("role.", 0) != 0) return std::nullopt;
  std::string_view rest = id.substr(5);
  const std::size_t dot = rest.find('.');
  if (dot == std::string_view::npos) return std::nullopt;
  const Role r = role_from_name(rest.substr(0, dot));
  if (r == Role::count_) return std::nullopt;
  std::string_view f = rest.substr(dot + 1);
  if (f.size() > 7 && f.substr(f.size() - 7) == ".custom") f = f.substr(0, f.size() - 7);
  return Field{r, std::string(f)};
}

bool ThemeEditor::load(const ThemePreset& preset, ThemeLoadReport& report) {
  ThemeLoadReport light_rep;
  std::optional<Theme> d = resolve_colours(preset, ThemeMode::Dark, report);
  std::optional<Theme> l = resolve_colours(preset, ThemeMode::Light, light_rep);
  if (!d || !l) return false;
  current_ = {*d, *l};
  defs_ = preset.colours.get("defs");
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
  for (const std::string& n : presets_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("load", std::move(opts));
}

void ThemeEditor::set_shipped(std::vector<std::string> names, bool may_write) {
  shipped_ = std::move(names);
  may_write_shipped_ = may_write;
  std::vector<MenuItem> opts;
  for (const std::string& n : shipped_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("write_shipped", std::move(opts));
  menu_.set_enabled("write_shipped", may_write && !shipped_.empty());
}

void ThemeEditor::set_mode(ThemeMode m) {
  if (preview_) cancel_preview();
  mode_ = m;
  menu_.set_value("mode", m == ThemeMode::Dark ? "dark" : "light");
  sync_values();
}

json::Value ThemeEditor::colours_json(std::string_view name) const {
  const ThemeEdit& c = undo_.current();
  return theme_pair_to_json_value(c.dark, c.light, name);
}

void ThemeEditor::rebuild_palette() {
  palette_.clear();
  auto has = [&](const std::string& id) {
    return std::any_of(palette_.begin(), palette_.end(), [&](const PaletteEntry& p) { return p.id == id; });
  };
  // defs names for labels: a defs entry that is a plain colour string.
  std::vector<std::pair<std::string, std::string>> names;  // colour id → def name
  for (const auto& [k, v] : defs_.obj)
    if (v.is_string())
      if (std::optional<Color> c = parse_color(v.str)) names.emplace_back(color_to_string(*c), k);
  auto label_for = [&](const std::string& id) {
    for (const auto& [cid, n] : names)
      if (cid == id) return n + " = " + id;
    return id;
  };
  palette_.push_back({Color::none(), "none", "none"});
  for (const Theme* t : {&current_.dark, &current_.light})
    for (std::size_t i = 0; i < kRoleCount; ++i)
      for (Color c : {t->styles[i].fg, t->styles[i].bg}) {
        const std::string id = color_to_string(c);
        if (!has(id)) palette_.push_back({c, id, label_for(id)});
      }
}

void ThemeEditor::rebuild_menu() {
  std::vector<MenuItem> palette_opts;
  for (const PaletteEntry& p : palette_) palette_opts.push_back(MenuItem::action(p.id, p.label));
  std::vector<MenuItem> roles;
  for (std::size_t i = 0; i < kRoleCount; ++i) {
    const std::string name(kRoleNames[i]);
    const std::string base = "role." + name;
    std::vector<MenuItem> fields;
    fields.push_back(MenuItem::choice(base + ".fg", "fg", palette_opts, ""));
    fields.push_back(MenuItem::choice(base + ".bg", "bg", palette_opts, ""));
    fields.push_back(MenuItem::input(base + ".fg.custom", "custom fg (#rrggbb, 0-255, none)"));
    fields.push_back(MenuItem::input(base + ".bg.custom", "custom bg (#rrggbb, 0-255, none)"));
    for (const char* a : kAttrs) fields.push_back(MenuItem::toggle(base + "." + a, a, false));
    roles.push_back(MenuItem::submenu(base, name, std::move(fields)));
  }
  std::vector<MenuItem> load_opts, shipped_opts;
  for (const std::string& n : presets_) load_opts.push_back(MenuItem::action(n, n));
  for (const std::string& n : shipped_) shipped_opts.push_back(MenuItem::action(n, n));
  MenuItem root = MenuItem::submenu(
      "root", "theme editor",
      {MenuItem::submenu("roles", "Roles", std::move(roles)),
       MenuItem::choice("mode", "Mode (edit + preview)", {MenuItem::action("dark", "dark"), MenuItem::action("light", "light")},
                        mode_ == ThemeMode::Dark ? "dark" : "light"),
       MenuItem::action("undo", "Undo", "Ctrl-Z"), MenuItem::action("redo", "Redo", "Ctrl-Y"),
       MenuItem::choice("load", "Load preset", std::move(load_opts), ""),
       MenuItem::input("save", "Save as preset"),
       MenuItem::choice("write_shipped", "Write a SHIPPED preset (the editor's privilege)", std::move(shipped_opts), ""),
       MenuItem::action("reset_loaded", "Reset to the loaded preset\xE2\x80\xA6"),
       MenuItem::action("reset_builtin", "Reset to the built-in default\xE2\x80\xA6")});
  // A rebuild (a load, a replace) starts at the top level; a committed custom colour
  // only refreshes the option lists in place (commit_current), keeping the position.
  menu_.set_root(std::move(root));
  menu_.set_enabled("write_shipped", may_write_shipped_ && !shipped_.empty());
  sync_values();
}

void ThemeEditor::sync_values() {
  const Theme& t = mode_ == ThemeMode::Dark ? undo_.current().dark : undo_.current().light;
  for (std::size_t i = 0; i < kRoleCount; ++i) {
    const std::string base = "role." + std::string(kRoleNames[i]);
    const Style& s = t.styles[i];
    menu_.set_value(base + ".fg", color_to_string(s.fg));
    menu_.set_value(base + ".bg", color_to_string(s.bg));
    for (const char* a : kAttrs) menu_.set_checked(base + "." + a, attr_value(s, a));
  }
  menu_.set_value("mode", mode_ == ThemeMode::Dark ? "dark" : "light");
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
    for (const PaletteEntry& p : palette_) opts.push_back(MenuItem::action(p.id, p.label));
    for (std::size_t i = 0; i < kRoleCount; ++i) {
      const std::string base = "role." + std::string(kRoleNames[i]);
      menu_.set_options(base + ".fg", opts);
      menu_.set_options(base + ".bg", opts);
    }
  }
  sync_values();
  return {Outcome::Kind::Committed, {}};
}

void ThemeEditor::replace(ThemeEdit e) {
  preview_.reset();
  current_ = std::move(e);
  undo_.commit(current_);
  rebuild_palette();
  rebuild_menu();
}

bool ThemeEditor::undo() {
  cancel_preview();
  if (!undo_.undo()) return false;
  current_ = undo_.current();
  sync_values();
  status_ = "undone";
  return true;
}

bool ThemeEditor::redo() {
  cancel_preview();
  if (!undo_.redo()) return false;
  current_ = undo_.current();
  sync_values();
  status_ = "redone";
  return true;
}

std::optional<Role> ThemeEditor::focused_role() const {
  auto role_in = [](std::string_view id) -> std::optional<Role> {
    if (id.rfind("role.", 0) != 0) return std::nullopt;
    std::string_view rest = id.substr(5);
    const std::size_t dot = rest.find('.');
    const Role r = role_from_name(dot == std::string_view::npos ? rest : rest.substr(0, dot));
    return r == Role::count_ ? std::nullopt : std::optional<Role>(r);
  };
  if (auto r = role_in(menu_.level().id)) return r;
  if (const MenuItem* it = menu_.selected_item())
    if (auto r = role_in(it->id)) return r;
  return std::nullopt;
}

std::optional<Color> ThemeEditor::highlighted_color() const {
  const MenuItem* it = menu_.selected_item();
  if (!it) return std::nullopt;
  if (menu_.editing() && it->id.size() > 7 && it->id.substr(it->id.size() - 7) == ".custom") return parse_color(it->value);
  if (const std::optional<Field> f = field_of(menu_.level().id); f && (f->name == "fg" || f->name == "bg")) return parse_color(it->id);
  return std::nullopt;
}

std::string ThemeEditor::status_line() const {
  std::string s;
  if (preview_) s = "previewing \xE2\x80\x94 Enter commits, Esc cancels";
  else s = status_.empty() ? "Enter commits, Esc cancels" : status_;
  s += " \xC2\xB7 undo " + std::to_string(undo_.undo_depth()) + " \xC2\xB7 redo " + std::to_string(undo_.redo_depth());
  s += std::string(" \xC2\xB7 ") + (mode_ == ThemeMode::Dark ? "dark" : "light");
  return s;
}

ThemeEditor::Outcome ThemeEditor::handle(const Event& e) {
  using K = MenuEvent::Kind;
  using O = Outcome::Kind;
  if (const auto* k = std::get_if<KeyEvent>(&e); k && k->key == Key::Char && k->ctrl && !k->alt) {
    if (k->ch == 'z') { status_ = undo() ? "undone" : "nothing to undo"; return {O::Changed, {}}; }
    if (k->ch == 'y') { status_ = redo() ? "redone" : "nothing to redo"; return {O::Changed, {}}; }
  }
  status_.clear();
  const MenuEvent ev = menu_.handle(e);
  // ---- committing events ----
  if (ev.kind == K::Choose) {
    if (const std::optional<Field> f = field_of(ev.id)) {
      if (std::optional<Color> c = parse_color(ev.value)) { begin_preview(); apply(*f, *c); }
      return commit_current();
    }
    if (ev.id == "mode") { set_mode(ev.value == "light" ? ThemeMode::Light : ThemeMode::Dark); return {O::Changed, {}}; }
    if (ev.id == "load") return {O::LoadPreset, ev.value};
    if (ev.id == "write_shipped") return {O::WriteShipped, ev.value};
    return {O::None, {}};
  }
  if (ev.kind == K::Toggle) {
    if (const std::optional<Field> f = field_of(ev.id)) {
      begin_preview();
      attr_of(style_at(f->role), f->name) = ev.checked;
      return commit_current();
    }
    return {O::None, {}};
  }
  if (ev.kind == K::Input) {
    if (ev.id == "save") return {O::SaveAs, ev.value};
    if (const std::optional<Field> f = field_of(ev.id)) {
      if (std::optional<Color> c = parse_color(ev.value)) { begin_preview(); apply(*f, *c); return commit_current(); }
      cancel_preview();
      status_ = "'" + ev.value + "' is not a colour (#rrggbb, 0-255, none)";
      return {O::Changed, {}};
    }
    return {O::None, {}};
  }
  if (ev.kind == K::Activate) {
    if (ev.id == "undo") { status_ = undo() ? "undone" : "nothing to undo"; return {O::Changed, {}}; }
    if (ev.id == "redo") { status_ = redo() ? "redone" : "nothing to redo"; return {O::Changed, {}}; }
    if (ev.id == "reset_loaded") return {O::ResetLoaded, {}};
    if (ev.id == "reset_builtin") return {O::ResetBuiltin, {}};
    return {O::None, {}};
  }
  if (ev.kind == K::Closed) return {O::Closed, {}};
  // ---- live preview: the highlighted palette entry, or a custom colour being typed ----
  const MenuItem* sel = menu_.selected_item();
  const std::optional<Field> level_field = field_of(menu_.level().id);
  if (level_field && (level_field->name == "fg" || level_field->name == "bg") && sel && !menu_.editing()) {
    if (std::optional<Color> c = parse_color(sel->id)) {
      begin_preview();
      apply(*level_field, *c);
      return {O::Changed, {}};
    }
  }
  if (menu_.editing() && sel) {
    if (const std::optional<Field> f = field_of(sel->id)) {
      begin_preview();
      if (std::optional<Color> c = parse_color(sel->value)) apply(*f, *c);
      else {  // not (yet) a colour: show the committed value while typing continues
        const Theme& pt = mode_ == ThemeMode::Dark ? preview_->dark : preview_->light;
        apply(*f, f->name == "fg" ? pt.style(f->role).fg : pt.style(f->role).bg);
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
