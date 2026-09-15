// rolltui/tools/menu_editor.cpp — see menu_editor.hpp.
#include "tool_str.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_widget_menu_tree.h"
#include "menu_editor.hpp"

#include <algorithm>
#include <cstdio>

namespace rolltui::tools {

namespace {

// ---- menu tree glue (mechanical; the identical shape layout_editor.cpp and keys_editor.cpp
// each carry — see keys_editor.cpp's comment on why this is not a header of its own). There is
// no `set_options` here as there is there: this editor's one dynamic option list (Load) changes
// only when the host hands it new names, and `rebuild_menu` already runs then. --------------
void set_enabled(RolltuiMenu* m, std::string_view id, bool enabled) {
  rolltui_menu_set_enabled(m, id.data(), id.size(), enabled ? 1 : 0);
}
void set_value(RolltuiMenu* m, std::string_view id, std::string_view value) {
  rolltui_menu_set_value(m, id.data(), id.size(), value.data(), value.size());
}
void set_checked(RolltuiMenu* m, std::string_view id, bool checked) {
  rolltui_menu_set_checked(m, id.data(), id.size(), checked ? 1 : 0);
}

// ---- the item's own vocabulary, each one C call, none of them a decision of this file's ----
std::string kind_name(unsigned char k) {
  switch (k) {
    case ROLLTUI_MENU_SUBMENU: return "submenu";
    case ROLLTUI_MENU_TOGGLE: return "toggle";
    case ROLLTUI_MENU_CHOICE: return "choice";
    case ROLLTUI_MENU_INPUT: return "input";
    case ROLLTUI_MENU_SECTION: return "section";
    case ROLLTUI_MENU_SEPARATOR: return "separator";
    default: return "action";
  }
}
std::optional<unsigned char> kind_from_name(std::string_view s) {
  if (s == "action") return ROLLTUI_MENU_ACTION;
  if (s == "submenu") return ROLLTUI_MENU_SUBMENU;
  if (s == "toggle") return ROLLTUI_MENU_TOGGLE;
  if (s == "choice") return ROLLTUI_MENU_CHOICE;
  if (s == "input") return ROLLTUI_MENU_INPUT;
  if (s == "section") return ROLLTUI_MENU_SECTION;
  if (s == "separator") return ROLLTUI_MENU_SEPARATOR;
  return std::nullopt;
}
std::string input_type_name(unsigned char t) {
  std::size_t len = 0;
  const char* p = rolltui_input_type_name(t, &len);
  return std::string(p ? p : "", len);
}
std::optional<unsigned char> input_type_from_name(std::string_view s) {
  unsigned char out = 0;
  if (!rolltui_input_type_from_name(s.data(), s.size(), &out)) return std::nullopt;
  return out;
}
// A number as a field's text. `%g` so 0 reads "0" and not "0.000000" — a range a person
// typed as 9 must read back as 9, which is what makes the round trip legible.
std::string num_text(double v) {
  char b[32];
  const int n = std::snprintf(b, sizeof b, "%g", v);
  return std::string(b, n > 0 ? static_cast<std::size_t>(n) : 0);
}
// The offered names as one line, for a field's hint — layout_editor.cpp's `joined`, and every
// list here is short enough that a plain join is the whole of it.
std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& n : names) out += (out.empty() ? "" : " | ") + n;
  return out;
}
// A child list rebuilt from a predicate over its indices. `RolltuiMenuItemList` has no erase
// (`RolltuiLayerList::erase_id` and `RolltuiActionList::erase_name` exist; this list never
// needed one), so removal and reorder are both "keep these, in this order" — one mechanism
// for both, and it clones rather than moving pointers because the list owns them.
void rebuild_children(RolltuiMenuItemList& list, const std::vector<std::size_t>& order) {
  std::vector<MenuItem> kept;
  kept.reserve(order.size());
  for (const std::size_t i : order) kept.push_back(list[i].clone());
  list.clear();
  for (MenuItem& it : kept) list.push_back(std::move(it));
}

}  // namespace

MenuEditor::MenuEditor(RolltuiContext* ctx) : ctx_(ctx) {
  current_ = skeleton("main");
  undo_.reset(current_.clone());
  rebuild_menu();
}

// ---- the tree ------------------------------------------------------------------------------

MenuItem* MenuEditor::at_path(MenuItem& root, const MenuPath& p) {
  MenuItem* it = &root;
  for (const std::size_t i : p) {
    if (i >= it->children.size()) return nullptr;
    it = &it->children[i];
  }
  return it;
}
const MenuItem* MenuEditor::at_path(const MenuItem& root, const MenuPath& p) {
  return at_path(const_cast<MenuItem&>(root), p);
}

std::vector<MenuPath> MenuEditor::paths_in_order(const MenuItem& root) {
  std::vector<MenuPath> out;
  MenuPath here;
  const auto walk = [&](auto&& self, const MenuItem& it) -> void {
    out.push_back(here);
    for (std::size_t i = 0; i < it.children.size(); ++i) {
      here.push_back(i);
      self(self, it.children[i]);
      here.pop_back();
    }
  };
  walk(walk, root);
  return out;
}

MenuItem* MenuEditor::sel_item() { return at_path(current_, sel_); }
const MenuItem* MenuEditor::selected_item() const { return at_path(current_, sel_); }

void MenuEditor::select(MenuPath p) {
  if (at_path(current_, p)) sel_ = std::move(p);
  sync_values();
}

void MenuEditor::select_next(bool backwards) {
  const std::vector<MenuPath> order = paths_in_order(current_);
  if (order.empty()) return;
  const auto at = std::find(order.begin(), order.end(), sel_);
  std::size_t i = at == order.end() ? 0 : static_cast<std::size_t>(at - order.begin());
  i = backwards ? (i == 0 ? order.size() - 1 : i - 1) : (i + 1) % order.size();
  sel_ = order[i];
  sync_values();
}

// A root submenu with the typed name and ONE action item — the whole skeleton, stated here
// rather than rendered from whatever was open. See the header for why nothing is inherited.
MenuItem MenuEditor::skeleton(std::string name) const {
  MenuItem root = MenuItem::submenu("root", name.c_str());
  root.children.push_back(MenuItem::action("item", "New item"));
  return root;
}

void MenuEditor::load(const MenuItem& root) {
  current_ = root.clone();
  undo_.reset(current_.clone());
  sel_.clear();
  status_.clear();
  rebuild_menu();
}

void MenuEditor::replace(MenuItem root) {
  current_ = std::move(root);
  undo_.reset(current_.clone());
  if (!at_path(current_, sel_)) sel_.clear();
  rebuild_menu();
}

void MenuEditor::set_menus(std::vector<std::string> names) {
  menus_ = std::move(names);
  rebuild_menu();
}
void MenuEditor::set_actions(std::vector<std::string> names) {
  actions_ = std::move(names);
  sync_values();
}

std::string MenuEditor::to_json() const {
  RolltuiStr out{};
  rolltui_menu_dump_json(&current_, &out);
  const std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}

// ---- operations ----------------------------------------------------------------------------

// An id no sibling already has. A Choice's options have their OWN id namespace (the loader's
// `option_ids`), so uniqueness is asked of the SIBLINGS and never of the whole tree — which is
// also why the selection is a path and not an id.
std::string MenuEditor::unique_id(const MenuItem& parent, const std::string& base) const {
  const auto taken = [&parent](const std::string& id) {
    for (std::size_t i = 0; i < parent.children.size(); ++i)
      if (view_of(parent.children[i].id) == id) return true;
    return false;
  };
  if (!taken(base)) return base;
  for (int n = 2; n < 1000; ++n) {
    const std::string candidate = base + "-" + std::to_string(n);
    if (!taken(candidate)) return candidate;
  }
  return base;
}

bool MenuEditor::add_item(bool as_child, const std::string& id) {
  if (id.empty()) { status_ = "an item needs an id"; return false; }
  // As a CHILD: into the selected item. As a SIBLING: into its parent, after it. The root has
  // no parent, so a sibling of the root is a child of the root — said, not silently ignored.
  MenuPath parent_path = sel_;
  std::size_t after = 0;
  if (as_child) {
    after = at_path(current_, sel_) ? at_path(current_, sel_)->children.size() : 0;
  } else if (sel_.empty()) {
    status_ = "the root has no sibling — added as a child";
    after = current_.children.size();
  } else {
    after = sel_.back() + 1;
    parent_path.pop_back();
  }
  MenuItem* parent = at_path(current_, parent_path);
  if (!parent) return false;
  const std::string fresh = unique_id(*parent, id);
  // The order the rebuild takes: every existing index, with a gap where the new one goes.
  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < parent->children.size(); ++i) order.push_back(i);
  std::vector<MenuItem> kept;
  for (const std::size_t i : order) kept.push_back(parent->children[i].clone());
  kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(std::min(after, kept.size())),
              MenuItem::action(fresh.c_str(), fresh.c_str()));
  parent->children.clear();
  for (MenuItem& it : kept) parent->children.push_back(std::move(it));
  parent_path.push_back(std::min(after, parent->children.size() - 1));
  sel_ = parent_path;
  // A Choice's children ARE its options, so what was just added is named for what it is —
  // one operation, and the PARENT's kind is what says which. See the header.
  status_ = (parent->kind == MenuItem::Kind::Choice ? "added option " : "added item ") + fresh;
  return true;
}

bool MenuEditor::move_item(int delta) {
  if (sel_.empty()) { status_ = "the root cannot move"; return false; }
  MenuPath parent_path = sel_;
  const std::size_t i = parent_path.back();
  parent_path.pop_back();
  MenuItem* parent = at_path(current_, parent_path);
  if (!parent) return false;
  const std::size_t n = parent->children.size();
  if ((delta < 0 && i == 0) || (delta > 0 && i + 1 >= n)) {
    status_ = delta < 0 ? "already first" : "already last";
    return false;
  }
  const std::size_t j = static_cast<std::size_t>(static_cast<int>(i) + delta);
  std::vector<std::size_t> order;
  for (std::size_t k = 0; k < n; ++k) order.push_back(k);
  std::swap(order[i], order[j]);
  rebuild_children(parent->children, order);
  parent_path.push_back(j);
  sel_ = parent_path;
  status_ = delta < 0 ? "moved up" : "moved down";
  return true;
}

bool MenuEditor::remove_item() {
  if (sel_.empty()) { status_ = "the root cannot be removed — a menu file is one tree"; return false; }
  MenuPath parent_path = sel_;
  const std::size_t i = parent_path.back();
  parent_path.pop_back();
  MenuItem* parent = at_path(current_, parent_path);
  if (!parent) return false;
  const std::string gone = str_of(parent->children[i].id);
  std::vector<std::size_t> order;
  for (std::size_t k = 0; k < parent->children.size(); ++k)
    if (k != i) order.push_back(k);
  rebuild_children(parent->children, order);
  if (!parent->children.empty()) parent_path.push_back(std::min(i, parent->children.size() - 1));
  sel_ = parent_path;
  status_ = "removed " + gone + " and everything under it";
  return true;
}

// ---- the editor's own menu -------------------------------------------------------------------

void MenuEditor::rebuild_menu() {
  std::vector<MenuItem> kinds, types, loads;
  for (const char* k : {"action", "submenu", "toggle", "choice", "input", "section", "separator"})
    kinds.push_back(MenuItem::action(k, k));
  for (unsigned char t = 0; t < ROLLTUI_INPUT_TYPE_COUNT; ++t) {
    const std::string n = input_type_name(t);
    types.push_back(MenuItem::action(n.c_str(), n.c_str()));
  }
  for (const std::string& n : menus_) loads.push_back(MenuItem::action(n.c_str(), n.c_str()));

  InputSpec name, text, number;
  name.type = InputType::Name;
  text.type = InputType::Text;
  text.optional = true;
  number.type = InputType::Float;

  std::vector<MenuItem> top;
  InputSpec count;
  count.type = InputType::Int;
  count.min = 0;
  count.max = 1e6;
  top.push_back(MenuItem::action("next", "Select the next item", "Tab"));
  top.push_back(MenuItem::action("prev", "Select the previous item", "Shift-Tab"));
  top.push_back(choice_of("kind", "Kind", std::move(kinds), "action"));
  top.push_back(MenuItem::input("id", "Item id", name.clone()));
  top.push_back(MenuItem::input("label", "Label", text.clone()));
  top.push_back(MenuItem::input("action", "Action it invokes", text.clone()));
  top.push_back(MenuItem::toggle("checked", "Checked", false));
  top.push_back(MenuItem::toggle("dropdown", "Dropdown (options in a box over the menu)", false));
  top.push_back(MenuItem::input("value", "Value", text.clone()));
  top.push_back(MenuItem::toggle("enabled", "Enabled", true));
  top.push_back(choice_of("input_type", "Input type", std::move(types), "text"));
  top.push_back(MenuItem::input("min", "Minimum", number.clone()));
  top.push_back(MenuItem::input("max", "Maximum", number.clone()));
  top.push_back(MenuItem::input("step", "Step", number.clone()));
  top.push_back(MenuItem::input("precision", "Decimal places (-1: any)", number.clone()));
  top.push_back(MenuItem::input("hint", "Hint", text.clone()));
  top.push_back(MenuItem::input("min_len", "Shortest allowed (0: no floor)", count.clone()));
  top.push_back(MenuItem::input("max_len", "Longest allowed (0: no cap)", count.clone()));
  // A VALIDATOR IS A NAME IN THE APP, exactly as an item's action is, and this tool can no more
  // check it than it can check the action — the same permanent exception, not a second one.
  top.push_back(MenuItem::input("validator", "Validator the app registers", text.clone()));
  top.push_back(MenuItem::toggle("optional", "Optional", false));
  // A SHORTCUT is display only. With an action set, the live chords win and this is ignored;
  // it exists for an item that names no action and still wants to advertise a key.
  top.push_back(MenuItem::input("shortcut", "Shortcut text (display only)", text.clone()));
  top.push_back(MenuItem::input("add_child", "Add a child item (id)", name.clone()));
  top.push_back(MenuItem::input("add_sibling", "Add a sibling item (id)", name.clone()));
  top.push_back(MenuItem::action("move_up", "Move up"));
  top.push_back(MenuItem::action("move_down", "Move down"));
  top.push_back(MenuItem::action("remove", "Remove this item"));
  top.push_back(MenuItem::action("undo", "Undo", "Ctrl-Z"));
  top.push_back(MenuItem::action("redo", "Redo", "Ctrl-Y"));
  top.push_back(MenuItem::input("new", "New menu, from an empty tree", name.clone()));
  top.push_back(choice_of("load", "Load menu", std::move(loads), ""));
  top.push_back(MenuItem::input("save", "Save menu as", name.clone()));
  top.push_back(MenuItem::action("reset_loaded", "Reset to the loaded menu\xE2\x80\xA6"));
  MenuItem root = submenu_of("root", "menu editor", std::move(top));
  rolltui_menu_set_root(menu_, &root);
  sync_values();
}

// Which fields this item's KIND offers. Disabled is drawn muted, so a Toggle never shows a
// range and an Action never shows a `checked` — the same "one screen, one way to say a thing"
// rule the layout editor holds between Source and Menu file.
void MenuEditor::sync_fields() {
  const MenuItem* it = selected_item();
  const bool have = it != nullptr;
  const unsigned char k = have ? static_cast<unsigned char>(it->kind) : ROLLTUI_MENU_ACTION;
  const bool is_input = have && k == ROLLTUI_MENU_INPUT;
  const bool numeric = is_input && (it->spec.type == InputType::Int || it->spec.type == InputType::Float);
  const bool root = sel_.empty();
  set_enabled(menu_, "kind", have && !root);   // the root is the file's tree: always a submenu
  set_enabled(menu_, "id", have);
  set_enabled(menu_, "label", have);
  set_enabled(menu_, "action", have && (k == ROLLTUI_MENU_ACTION || k == ROLLTUI_MENU_TOGGLE));
  set_enabled(menu_, "checked", have && k == ROLLTUI_MENU_TOGGLE);
  set_enabled(menu_, "dropdown", have && k == ROLLTUI_MENU_CHOICE);
  set_enabled(menu_, "value", have && (k == ROLLTUI_MENU_CHOICE || is_input));
  set_enabled(menu_, "enabled", have);
  set_enabled(menu_, "input_type", is_input);
  set_enabled(menu_, "min", numeric);
  set_enabled(menu_, "max", numeric);
  set_enabled(menu_, "step", numeric);
  set_enabled(menu_, "hint", is_input);
  // `precision` is a Float's alone; the two lengths and the validator are a Text's or a Name's,
  // which is what the spec's own comments say each field is checked for.
  set_enabled(menu_, "precision", is_input && it->spec.type == InputType::Float);
  const bool textual = is_input && (it->spec.type == InputType::Text || it->spec.type == InputType::Name);
  set_enabled(menu_, "min_len", textual);
  set_enabled(menu_, "max_len", textual);
  set_enabled(menu_, "validator", textual);
  set_enabled(menu_, "optional", is_input);
  set_enabled(menu_, "shortcut", have);
  set_enabled(menu_, "move_up", !root);
  set_enabled(menu_, "move_down", !root);
  set_enabled(menu_, "remove", !root);
}

void MenuEditor::sync_values() {
  const MenuItem* it = selected_item();
  if (!it) return;
  set_value(menu_, "kind", kind_name(static_cast<unsigned char>(it->kind)));
  set_value(menu_, "id", str_of(it->id));
  set_value(menu_, "label", str_of(it->label));
  set_value(menu_, "action", str_of(it->action_name));
  set_checked(menu_, "checked", it->checked != 0);
  set_checked(menu_, "dropdown", it->dropdown != 0);
  set_value(menu_, "value", str_of(it->value));
  set_checked(menu_, "enabled", it->enabled != 0);
  set_value(menu_, "input_type", input_type_name(static_cast<unsigned char>(it->spec.type)));
  set_value(menu_, "min", num_text(it->spec.min));
  set_value(menu_, "max", num_text(it->spec.max));
  set_value(menu_, "step", num_text(it->spec.step));
  set_value(menu_, "hint", str_of(it->spec.hint));
  set_value(menu_, "precision", std::to_string(it->spec.precision));
  set_value(menu_, "min_len", std::to_string(it->spec.min_len));
  set_value(menu_, "max_len", std::to_string(it->spec.max_len));
  set_value(menu_, "validator", str_of(it->spec.validator));
  set_checked(menu_, "optional", it->spec.optional != 0);
  set_value(menu_, "shortcut", str_of(it->shortcut));
  // The actions THIS BINARY knows are the field's HINT and never its option list: an item may
  // name an action the app declares and this tool has never heard of, which is the
  // direction — the screen is the intent and the app reports what it cannot reach.
  if (MenuItem* field = rolltui_menu_find(menu_, "action", 6); field && !actions_.empty())
    set_str(field->spec.hint, joined(actions_));
  sync_fields();
}

// ---- undo / commit -----------------------------------------------------------------------

MenuEditor::Outcome MenuEditor::commit_current() {
  undo_.commit(current_.clone());
  sync_values();
  return {Outcome::Kind::Committed, {}};
}

bool MenuEditor::undo() {
  if (!undo_.undo()) return false;
  current_ = undo_.current().clone();
  if (!at_path(current_, sel_)) sel_.clear();
  sync_values();
  return true;
}
bool MenuEditor::redo() {
  if (!undo_.redo()) return false;
  current_ = undo_.current().clone();
  if (!at_path(current_, sel_)) sel_.clear();
  sync_values();
  return true;
}

// ---- the lines the host draws --------------------------------------------------------------

void MenuEditor::status_line(std::string& out) const {
  out.clear();
  out += status_.empty() ? "Enter commits, Esc cancels" : status_;
  out += " \xC2\xB7 undo ";
  append_count(out, undo_.undo_depth());
  out += " \xC2\xB7 redo ";
  append_count(out, undo_.redo_depth());
}
std::string MenuEditor::status_line() const {
  std::string s;
  status_line(s);
  return s;
}

std::string MenuEditor::selection_line() const {
  const MenuItem* it = selected_item();
  if (!it) return {};
  std::string s = "selected: " + str_of(it->id) + "  " + kind_name(static_cast<unsigned char>(it->kind));
  if (it->kind == MenuItem::Kind::Choice) s += "  " + std::to_string(it->children.size()) + " options";
  else if (it->children.size() != 0) s += "  " + std::to_string(it->children.size()) + " items";
  if (it->kind == MenuItem::Kind::Input) s += "  " + input_type_name(static_cast<unsigned char>(it->spec.type));
  if (it->action_name.n != 0) s += "  \xE2\x86\x92 " + str_of(it->action_name);
  return s;
}

// ---- events ---------------------------------------------------------------------------------

MenuEditor::Outcome MenuEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  using O = Outcome::Kind;
  if (e->kind == ROLLTUI_EVENT_KEY) {
    const RolltuiChord& k = e->key;
    std::size_t len = 0;
    const char* ed_p = rolltui_bindings_action_for(nav, &k, "editor", 6, &len);
    const std::string_view ed = ed_p ? std::string_view(ed_p, len) : std::string_view();
    if (ed == "editor.undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ed == "editor.redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    const char* st_p = rolltui_bindings_action_for(nav, &k, "stack", 5, &len);
    const std::string_view st = st_p ? std::string_view(st_p, len) : std::string_view();
    if ((st == "stack.focus_next" || st == "stack.focus_prev") && !rolltui_menu_editing(menu_)) {
      select_next(st == "stack.focus_prev");
      return {O::Changed, {}};
    }
  }
  status_.clear();
  RolltuiMenuEvent raw{};
  rolltui_menu_handle(menu_, e, nav, rolltui_menu_default_actions(), &raw);
  const unsigned char kind = raw.kind;
  const std::string id = str_of(raw.id);
  const std::string value = str_of(raw.value);
  const bool checked = raw.checked != 0;
  rolltui_menu_event_release(&raw);

  if (kind == ROLLTUI_MENU_EVENT_ACTIVATE) {
    if (id == "next") { select_next(); return {O::Changed, {}}; }
    if (id == "prev") { select_next(true); return {O::Changed, {}}; }
    if (id == "undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "reset_loaded") return {O::ResetLoaded, {}};
    if (id == "move_up" || id == "move_down") {
      if (!move_item(id == "move_up" ? -1 : 1)) return {O::Changed, {}};
      Outcome o = commit_current();
      return o;
    }
    if (id == "remove") {
      if (!remove_item()) return {O::Changed, {}};
      return commit_current();
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_TOGGLE) {
    MenuItem* it = sel_item();
    if (!it) return {O::None, {}};
    if (id == "checked") { it->checked = checked ? 1 : 0; return commit_current(); }
    if (id == "dropdown") { it->dropdown = checked ? 1 : 0; return commit_current(); }
    if (id == "enabled") { it->enabled = checked ? 1 : 0; return commit_current(); }
    if (id == "optional") { it->spec.optional = checked ? 1 : 0; return commit_current(); }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    MenuItem* it = sel_item();
    if (id == "load") return {O::LoadMenu, value};
    if (!it) return {O::None, {}};
    if (id == "kind") {
      if (const auto k = kind_from_name(value)) {
        it->kind = static_cast<MenuItem::Kind>(*k);
        status_ = "kind " + value;
      }
      return commit_current();
    }
    if (id == "input_type") {
      if (const auto t = input_type_from_name(value)) {
        it->spec.type = static_cast<InputType>(*t);
        status_ = "type " + value;
      }
      return commit_current();
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_INPUT) {
    if (id == "save") return {O::SaveAs, value};
    if (id == "new") {
      replace(skeleton(value.empty() ? "main" : value));
      status_ = "new menu '" + (value.empty() ? std::string("main") : value) + "'";
      return {O::Committed, {}};
    }
    if (id == "add_child" || id == "add_sibling") {
      if (!add_item(id == "add_child", value)) return {O::Changed, {}};
      return commit_current();
    }
    MenuItem* it = sel_item();
    if (!it) return {O::None, {}};
    if (id == "id") { set_str(it->id, value); return commit_current(); }
    if (id == "label") { set_str(it->label, value); return commit_current(); }
    if (id == "action") { set_str(it->action_name, value); return commit_current(); }
    if (id == "value") { set_str(it->value, value); return commit_current(); }
    if (id == "hint") { set_str(it->spec.hint, value); return commit_current(); }
    if (id == "validator") { set_str(it->spec.validator, value); return commit_current(); }
    if (id == "shortcut") { set_str(it->shortcut, value); return commit_current(); }
    if (id == "precision") { it->spec.precision = std::atoi(value.c_str()); return commit_current(); }
    if (id == "min_len" || id == "max_len") {
      const std::size_t n = static_cast<std::size_t>(std::atol(value.c_str()));
      if (id == "min_len") it->spec.min_len = n;
      else it->spec.max_len = n;
      return commit_current();
    }
    if (id == "min" || id == "max" || id == "step") {
      const double d = std::atof(value.c_str());
      if (id == "min") it->spec.min = d;
      else if (id == "max") it->spec.max = d;
      else it->spec.step = d;
      return commit_current();
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_CLOSED) return {O::Closed, {}};
  return {O::None, {}};
}

}  // namespace rolltui::tools
