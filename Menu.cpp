// rolltui/Menu.cpp — see Menu.hpp.
#include "rolltui/Menu.hpp"

#include <algorithm>
#include <cctype>

#include "rolltui/Json.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

// ---- items ---------------------------------------------------------------------------

MenuItem MenuItem::action(std::string id, std::string label, std::string shortcut) {
  MenuItem m;
  m.kind = Kind::Action;
  m.id = std::move(id);
  m.label = std::move(label);
  m.shortcut = std::move(shortcut);
  return m;
}
MenuItem MenuItem::submenu(std::string id, std::string label, std::vector<MenuItem> children) {
  MenuItem m;
  m.kind = Kind::Submenu;
  m.id = std::move(id);
  m.label = std::move(label);
  m.children = std::move(children);
  return m;
}
MenuItem MenuItem::toggle(std::string id, std::string label, bool checked) {
  MenuItem m;
  m.kind = Kind::Toggle;
  m.id = std::move(id);
  m.label = std::move(label);
  m.checked = checked;
  return m;
}
MenuItem MenuItem::choice(std::string id, std::string label, std::vector<MenuItem> options, std::string value) {
  MenuItem m;
  m.kind = Kind::Choice;
  m.id = std::move(id);
  m.label = std::move(label);
  m.children = std::move(options);
  m.value = std::move(value);
  return m;
}
MenuItem MenuItem::input(std::string id, std::string label, std::string value) {
  MenuItem m;
  m.kind = Kind::Input;
  m.id = std::move(id);
  m.label = std::move(label);
  m.value = std::move(value);
  return m;
}

// ---- JSON ----------------------------------------------------------------------------

namespace {

using json::Value;

const char* kind_name(MenuItem::Kind k) {
  switch (k) {
    case MenuItem::Kind::Action: return "action";
    case MenuItem::Kind::Submenu: return "submenu";
    case MenuItem::Kind::Toggle: return "toggle";
    case MenuItem::Kind::Choice: return "choice";
    case MenuItem::Kind::Input: return "input";
  }
  return "action";
}

std::optional<MenuItem::Kind> kind_from_name(std::string_view s) {
  if (s == "action") return MenuItem::Kind::Action;
  if (s == "submenu") return MenuItem::Kind::Submenu;
  if (s == "toggle") return MenuItem::Kind::Toggle;
  if (s == "choice") return MenuItem::Kind::Choice;
  if (s == "input") return MenuItem::Kind::Input;
  return std::nullopt;
}

// `ids` is the tree-wide set of action ids; a Choice's OPTIONS are values, unique only
// within their choice (two choices may both offer "auto"), so they get their own set.
MenuItem item_from_json(const Value& v, const std::string& where, MenuLoadReport& rep, std::vector<std::string>& ids) {
  MenuItem it;
  if (!v.is_object()) { rep.bad_values.push_back(where + ": expected an item object"); return it; }
  bool kind_given = false;
  for (const auto& [k, x] : v.obj) {
    const std::string at = where + "." + k;
    if (k == "id" || k == "label" || k == "shortcut" || k == "value") {
      if (!x.is_string()) { rep.bad_values.push_back(at + ": expected a string"); continue; }
      if (k == "id") it.id = x.str;
      else if (k == "label") it.label = x.str;
      else if (k == "shortcut") it.shortcut = x.str;
      else it.value = x.str;
    } else if (k == "kind") {
      auto kd = x.is_string() ? kind_from_name(x.str) : std::nullopt;
      if (!kd) rep.bad_values.push_back(at + ": expected action | submenu | toggle | choice | input");
      else { it.kind = *kd; kind_given = true; }
    } else if (k == "enabled" || k == "checked") {
      if (!x.is_bool()) { rep.bad_values.push_back(at + ": expected true or false"); continue; }
      (k == "enabled" ? it.enabled : it.checked) = x.b;
    } else if (k == "items") {
      if (!x.is_array()) { rep.bad_values.push_back(at + ": expected an array of items"); continue; }
      const bool choice = v.get("kind").as_string() == "choice";
      std::vector<std::string> option_ids;
      for (std::size_t i = 0; i < x.arr.size(); ++i)
        it.children.push_back(item_from_json(x.arr[i], at + "[" + std::to_string(i) + "]", rep, choice ? option_ids : ids));
    } else {
      rep.unknown_keys.push_back(at);
    }
  }
  if (!kind_given) it.kind = v.has("items") ? MenuItem::Kind::Submenu : MenuItem::Kind::Action;
  if (it.id.empty()) rep.bad_values.push_back(where + ": an item needs an \"id\"");
  else if (std::find(ids.begin(), ids.end(), it.id) != ids.end()) rep.bad_values.push_back(where + ".id: duplicate id '" + it.id + "'");
  else ids.push_back(it.id);
  if (it.label.empty()) it.label = it.id;
  return it;
}

Value item_to_json(const MenuItem& it) {
  Value o = Value::object();
  o.set("id", Value::string(it.id));
  if (it.label != it.id) o.set("label", Value::string(it.label));
  const bool implied = (it.kind == MenuItem::Kind::Submenu && !it.children.empty()) ||
                       (it.kind == MenuItem::Kind::Action && it.children.empty());
  if (!implied) o.set("kind", Value::string(kind_name(it.kind)));
  if (!it.shortcut.empty()) o.set("shortcut", Value::string(it.shortcut));
  if (!it.enabled) o.set("enabled", Value::boolean(false));
  if (it.checked) o.set("checked", Value::boolean(true));
  if (!it.value.empty()) o.set("value", Value::string(it.value));
  if (!it.children.empty()) {
    Value arr = Value::array();
    for (const MenuItem& c : it.children) arr.arr.push_back(item_to_json(c));
    o.set("items", std::move(arr));
  }
  return o;
}

bool contains_ci(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  auto lower = [](std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
  };
  return lower(hay).find(lower(needle)) != std::string::npos;
}

}  // namespace

std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report) {
  report = MenuLoadReport{};
  std::string err;
  Value root = json::parse(json_text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  if (!root.is_object()) { report.error = "menu file must be a JSON object"; return std::nullopt; }
  std::vector<std::string> ids;
  MenuItem it = item_from_json(root, "", report, ids);
  for (std::vector<std::string>* list : {&report.unknown_keys, &report.bad_values})
    for (std::string& s : *list)
      if (!s.empty() && s[0] == '.') s.erase(0, 1);
  if (it.kind != MenuItem::Kind::Submenu) report.bad_values.push_back("kind: the root must be a submenu (it holds the top level)");
  return it;
}

std::string menu_to_json(const MenuItem& root) { return json::dump(item_to_json(root), 2) + "\n"; }

// ---- the widget ----------------------------------------------------------------------

Menu::Menu() { root_.kind = MenuItem::Kind::Submenu; }
Menu::Menu(MenuItem root) { set_root(std::move(root)); }

void Menu::set_root(MenuItem root) {
  root_ = std::move(root);
  root_.kind = MenuItem::Kind::Submenu;
  reset();
}

void Menu::reset() {
  path_.clear();
  sel_ = 0;
  top_ = 0;
  filter_.clear();
  editing_ = false;
  palette_ = false;
  rebuild_flat();
}

namespace {
MenuItem* find_in(MenuItem& it, std::string_view id) {
  if (it.id == id) return &it;
  for (MenuItem& c : it.children)
    if (MenuItem* f = find_in(c, id)) return f;
  return nullptr;
}
}  // namespace

MenuItem* Menu::find(std::string_view id) { return find_in(root_, id); }
const MenuItem* Menu::find(std::string_view id) const { return find_in(const_cast<MenuItem&>(root_), id); }

bool Menu::set_value(std::string_view id, std::string value) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->value = std::move(value);
  return true;
}
bool Menu::set_checked(std::string_view id, bool checked) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->checked = checked;
  return true;
}
bool Menu::set_enabled(std::string_view id, bool enabled) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->enabled = enabled;
  return true;
}
bool Menu::set_options(std::string_view id, std::vector<MenuItem> options) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->children = std::move(options);
  rebuild_flat();
  clamp_selection();
  return true;
}

MenuItem* Menu::by_path(const std::vector<std::size_t>& p) {
  MenuItem* it = &root_;
  for (std::size_t i : p) {
    if (i >= it->children.size()) return nullptr;
    it = &it->children[i];
  }
  return it;
}
const MenuItem* Menu::by_path(const std::vector<std::size_t>& p) const { return const_cast<Menu*>(this)->by_path(p); }

const MenuItem& Menu::level() const {
  const MenuItem* it = by_path(path_);
  return it ? *it : root_;
}
MenuItem& Menu::level_mut() {
  MenuItem* it = by_path(path_);
  return it ? *it : root_;
}

void Menu::rebuild_flat() {
  flat_.clear();
  std::vector<std::size_t> p;
  std::string crumbs;
  auto walk = [&](auto& self, const MenuItem& it, const std::string& prefix) -> void {
    for (std::size_t i = 0; i < it.children.size(); ++i) {
      const MenuItem& c = it.children[i];
      p.push_back(i);
      const std::string label = prefix.empty() ? c.label : prefix + " \xE2\x80\xBA " + c.label;
      switch (c.kind) {
        case MenuItem::Kind::Submenu:
          self(self, c, label);
          break;
        case MenuItem::Kind::Choice:
          for (std::size_t j = 0; j < c.children.size(); ++j) {
            p.push_back(j);
            flat_.push_back({p, label + " \xE2\x80\xBA " + c.children[j].label});
            p.pop_back();
          }
          break;
        default:
          flat_.push_back({p, label});
      }
      p.pop_back();
    }
  };
  walk(walk, root_, "");
}

std::vector<std::size_t> Menu::visible() const {
  std::vector<std::size_t> out;
  if (palette_) {
    for (std::size_t i = 0; i < flat_.size(); ++i)
      if (contains_ci(flat_[i].label, filter_)) out.push_back(i);
    return out;
  }
  const MenuItem& lv = level();
  for (std::size_t i = 0; i < lv.children.size(); ++i)
    if (contains_ci(lv.children[i].label, filter_)) out.push_back(i);
  return out;
}

const MenuItem* Menu::item_at(std::size_t vis_index) const { return const_cast<Menu*>(this)->item_at_mut(vis_index); }

MenuItem* Menu::item_at_mut(std::size_t vis_index) {
  std::vector<std::size_t> vis = visible();
  if (vis_index >= vis.size()) return nullptr;
  if (palette_) return by_path(flat_[vis[vis_index]].path);
  return &level_mut().children[vis[vis_index]];
}

const MenuItem* Menu::selected_item() const { return item_at(sel_); }

void Menu::clamp_selection() {
  const std::size_t n = visible().size();
  if (n == 0) sel_ = 0;
  else if (sel_ >= n) sel_ = n - 1;
  ensure_visible();
}

std::string Menu::breadcrumb() const {
  if (palette_) return root_.label.empty() ? "\xE2\x80\xBA" : root_.label + " \xE2\x80\xBA \xE2\x80\xA6";
  std::string s = root_.label;
  const MenuItem* it = &root_;
  for (std::size_t i : path_) {
    if (i >= it->children.size()) break;
    it = &it->children[i];
    if (!s.empty()) s += " \xE2\x80\xBA ";
    s += it->label;
  }
  return s;
}

void Menu::set_palette(bool on) {
  if (palette_ == on) return;
  palette_ = on;
  path_.clear();
  sel_ = 0;
  top_ = 0;
  filter_.clear();
  editing_ = false;
  rebuild_flat();
}

void Menu::descend(std::size_t child) {
  path_.push_back(child);
  filter_.clear();
  sel_ = 0;
  top_ = 0;
  // A Choice opens on its current option.
  const MenuItem& lv = level();
  if (lv.kind == MenuItem::Kind::Choice)
    for (std::size_t i = 0; i < lv.children.size(); ++i)
      if (lv.children[i].id == lv.value) { sel_ = i; break; }
  ensure_visible();
}

bool Menu::ascend() {
  if (path_.empty()) return false;
  const std::size_t was = path_.back();
  path_.pop_back();
  filter_.clear();
  sel_ = std::min(was, level().children.empty() ? std::size_t{0} : level().children.size() - 1);
  top_ = 0;
  ensure_visible();
  return true;
}

MenuEvent Menu::act(std::size_t vis_index) {
  MenuItem* it = item_at_mut(vis_index);
  if (!it || !it->enabled) return {};
  // In palette mode the leaf may be a Choice option: its parent is the Choice.
  if (palette_) {
    const FlatEntry& fe = flat_[visible()[vis_index]];
    if (fe.path.size() >= 2) {
      std::vector<std::size_t> parent(fe.path.begin(), fe.path.end() - 1);
      MenuItem* p = by_path(parent);
      if (p && p->kind == MenuItem::Kind::Choice) {
        p->value = it->id;
        return {MenuEvent::Kind::Choose, p->id, it->id, false};
      }
    }
  } else if (level().kind == MenuItem::Kind::Choice) {
    MenuItem& choice = level_mut();
    choice.value = it->id;
    MenuEvent ev{MenuEvent::Kind::Choose, choice.id, it->id, false};
    ascend();
    return ev;
  }
  switch (it->kind) {
    case MenuItem::Kind::Action:
      return {MenuEvent::Kind::Activate, it->id, {}, false};
    case MenuItem::Kind::Toggle:
      it->checked = !it->checked;
      return {MenuEvent::Kind::Toggle, it->id, {}, it->checked};
    case MenuItem::Kind::Submenu:
    case MenuItem::Kind::Choice:
      if (palette_) return {};
      descend(visible()[vis_index]);
      return {};
    case MenuItem::Kind::Input:
      editing_ = true;
      edit_backup_ = it->value;
      sel_ = vis_index;
      return {};
  }
  return {};
}

MenuEvent Menu::handle(const Event& e) {
  if (const auto* k = std::get_if<KeyEvent>(&e)) return handle_key(*k);
  if (const auto* m = std::get_if<MouseEvent>(&e)) return handle_mouse(*m);
  if (const auto* p = std::get_if<PasteEvent>(&e)) {
    // Pasted text goes where typed text would: the edit, else the filter.
    for (char c : p->text)
      if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
        KeyEvent ke;
        ke.key = Key::Char;
        ke.ch = static_cast<char32_t>(static_cast<unsigned char>(c));
        handle_key(ke);
      }
    return {};
  }
  return {};
}

MenuEvent Menu::handle_key(const KeyEvent& k) {
  using K = MenuEvent::Kind;
  if (editing_) {
    MenuItem* it = item_at_mut(sel_);
    if (!it) { editing_ = false; return {}; }
    switch (k.key) {
      case Key::Enter: {
        editing_ = false;
        return {K::Input, it->id, it->value, false};
      }
      case Key::Escape:
        it->value = edit_backup_;
        editing_ = false;
        return {};
      case Key::Backspace: {
        if (it->value.empty()) return {};
        std::vector<unicode::Grapheme> g = unicode::graphemes(it->value);
        it->value.erase(g.back().offset);
        return {};
      }
      case Key::Char:
        if (!k.ctrl && !k.alt && k.ch >= 0x20 && k.ch != 0x7F) unicode::append_utf8(it->value, k.ch);
        return {};
      default:
        return {};
    }
  }
  const std::vector<std::size_t> vis = visible();
  const std::size_t n = vis.size();
  auto move_to = [&](std::size_t i) {
    if (n == 0) { sel_ = 0; return; }
    sel_ = std::min(i, n - 1);
    ensure_visible();
  };
  switch (k.key) {
    case Key::Up: move_to(sel_ == 0 ? 0 : sel_ - 1); return {};
    case Key::Down: move_to(sel_ + 1); return {};
    case Key::PageUp: { const std::size_t step = static_cast<std::size_t>(std::max(item_rows(), 1)); move_to(sel_ < step ? 0 : sel_ - step); return {}; }
    case Key::PageDown: move_to(sel_ + static_cast<std::size_t>(std::max(item_rows(), 1))); return {};
    case Key::Home: move_to(0); return {};
    case Key::End: move_to(n == 0 ? 0 : n - 1); return {};
    case Key::Enter: return act(sel_);
    case Key::Right: {
      const MenuItem* it = item_at(sel_);
      if (it && it->enabled && !palette_ && (it->kind == MenuItem::Kind::Submenu || it->kind == MenuItem::Kind::Choice)) descend(vis[sel_]);
      return {};
    }
    case Key::Left:
      if (!filter_.empty()) { filter_.clear(); clamp_selection(); return {}; }
      ascend();
      return {};
    case Key::Escape:
      if (!filter_.empty()) { filter_.clear(); clamp_selection(); return {}; }
      if (ascend()) return {};
      return {K::Closed, {}, {}, false};
    case Key::Backspace:
      if (!filter_.empty()) {
        std::vector<unicode::Grapheme> g = unicode::graphemes(filter_);
        filter_.erase(g.back().offset);
        clamp_selection();
      }
      return {};
    case Key::Char:
      if (!k.ctrl && !k.alt && k.ch >= 0x20 && k.ch != 0x7F) {
        unicode::append_utf8(filter_, k.ch);
        sel_ = 0;
        top_ = 0;
        ensure_visible();
      }
      return {};
    default:
      return {};
  }
}

MenuEvent Menu::handle_mouse(const MouseEvent& m) {
  using Kd = MouseEvent::Kind;
  if (m.kind == Kd::WheelUp) { if (sel_ > 0) { --sel_; ensure_visible(); } return {}; }
  if (m.kind == Kd::WheelDown) { const std::size_t n = visible().size(); if (n && sel_ + 1 < n) { ++sel_; ensure_visible(); } return {}; }
  if (m.kind != Kd::Press || m.button != 1) return {};
  if (!area_.contains(m.x, m.y)) return {};
  const int first_item_row = area_.h >= 2 ? area_.y + 1 : area_.y;
  if (m.y < first_item_row) return {};
  const std::size_t idx = static_cast<std::size_t>(top_) + static_cast<std::size_t>(m.y - first_item_row);
  if (idx >= visible().size()) return {};
  if (editing_) editing_ = false;
  sel_ = idx;
  return act(sel_);
}

int Menu::item_rows() const { return area_.h >= 2 ? area_.h - 1 : area_.h; }

void Menu::ensure_visible() {
  const int rows = item_rows();
  if (rows <= 0) { top_ = 0; return; }
  if (static_cast<int>(sel_) < top_) top_ = static_cast<int>(sel_);
  if (static_cast<int>(sel_) >= top_ + rows) top_ = static_cast<int>(sel_) - rows + 1;
  const int n = static_cast<int>(visible().size());
  top_ = std::clamp(top_, 0, std::max(0, n - rows));
}

int Menu::rows_for() const { return 1 + std::max<int>(1, static_cast<int>(visible().size())); }

void Menu::layout(Rect area) {
  area_ = area;
  ensure_visible();
}

std::string Menu::row_text(const MenuItem& it, bool in_palette, std::size_t vis_index) const {
  std::string s;
  if (in_palette) {
    s = flat_[visible()[vis_index]].label;
    if (it.kind == MenuItem::Kind::Toggle) s = std::string(it.checked ? "[x] " : "[ ] ") + s;
    return s;
  }
  switch (it.kind) {
    case MenuItem::Kind::Toggle: s = std::string(it.checked ? "[x] " : "[ ] ") + it.label; break;
    case MenuItem::Kind::Input: s = it.label + ": " + it.value; break;
    default: s = it.label;
  }
  if (level().kind == MenuItem::Kind::Choice && it.id == level().value) s = "\xE2\x80\xA2 " + s;  // • the current option
  return s;
}

void Menu::draw(Frame& f, const Theme& theme, bool focused) const {
  const Rect a = area_;
  if (a.w <= 0 || a.h <= 0) return;
  const int x0 = a.x + opt_.inset, w = a.w - 2 * opt_.inset;
  if (w <= 0) return;
  const std::vector<std::size_t> vis = visible();
  int y = a.y;
  if (a.h >= 2) {
    std::string crumb = breadcrumb();
    int used = f.put_text(x0, y, crumb, theme.style(Role::menu_breadcrumb), w, opt_.ambiguous_wide);
    if (!filter_.empty() || editing_) {
      const std::string tail = editing_ ? "  (editing: Enter saves, Esc cancels)" : "  /" + filter_;
      f.put_text(x0 + used, y, tail, theme.style(Role::menu_shortcut), std::max(w - used, 0), opt_.ambiguous_wide);
    }
    ++y;
  }
  const int rows = item_rows();
  const Style sel = theme.style(Role::menu_selected), item = theme.style(Role::menu_item), muted = theme.style(Role::text_muted);
  const Style shortcut = theme.style(Role::menu_shortcut), marker = theme.style(Role::scroll_marker);
  if (vis.empty()) {
    if (rows > 0) f.put_text(x0, y, filter_.empty() ? "(empty)" : "(no match for /" + filter_ + ")", muted, w, opt_.ambiguous_wide);
    return;
  }
  for (int r = 0; r < rows; ++r) {
    const std::size_t i = static_cast<std::size_t>(top_ + r);
    if (i >= vis.size()) break;
    const MenuItem* it = item_at(i);
    if (!it) break;
    const bool is_sel = i == sel_;
    const Style& base = is_sel ? sel : (it->enabled ? item : muted);
    f.fill({x0, y + r, w, 1}, base);
    std::string text = row_text(*it, palette_, i);
    // The right-hand side: a Choice's value and/or the descend arrow, or a shortcut.
    std::string right;
    if (!palette_) {
      if (it->kind == MenuItem::Kind::Choice) right = it->value + " \xE2\x96\xB8";
      else if (it->kind == MenuItem::Kind::Submenu) right = "\xE2\x96\xB8";
      else if (!it->shortcut.empty()) right = it->shortcut;
    }
    const int rw = right.empty() ? 0 : unicode::display_width(right, opt_.ambiguous_wide);
    const int left_max = right.empty() ? w : std::max(w - rw - 1, 0);
    const int used = f.put_text(x0, y + r, text, base, left_max, opt_.ambiguous_wide);
    if (rw > 0 && rw <= w) {
      const Style rs = is_sel ? sel : (it->kind == MenuItem::Kind::Choice || it->kind == MenuItem::Kind::Submenu ? base : shortcut);
      f.put_text(x0 + std::max(w - rw, used + 1), y + r, right, rs, std::max(w - std::max(w - rw, used + 1), 0), opt_.ambiguous_wide);
    }
    if (is_sel && editing_ && focused) {
      const int cx = std::min(x0 + used, x0 + w - 1);
      f.set_cursor(cx, y + r, true);
    }
  }
  // Scroll markers on the right edge when items are hidden above or below.
  if (w >= 1 && rows >= 1) {
    if (top_ > 0) f.put(x0 + w - 1, y, "\xE2\x96\xB2", 1, marker);
    if (static_cast<std::size_t>(top_ + rows) < vis.size()) f.put(x0 + w - 1, y + rows - 1, "\xE2\x96\xBC", 1, marker);
  }
  (void)focused;
}

}  // namespace rolltui
