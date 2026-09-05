// rolltui/Menu.cpp — the SHIM over `rolltui/c/rolltui_menu.h`: the JSON loader, the shipped
// menu files, the styling vocabulary, the thirteen action names, and the validator registry
// that stays where its callables are. The two implementations live in `MenuCpp.cpp` and
// `c/rolltui_menu.c`, and this file is the C++ API over it (Phase 15 m5).
//
// WHAT STAYS HERE AND WHY: the loader and the shipped table, the m3 split for `Theme`; and
// three TREE WALKS with no widget state in them (`item_actions`, `unknown_validators`,
// `apply_shortcuts`), because the C would gain nothing from them but a second place to know
// what `Bindings::chords_text` means.
#include "rolltui/Menu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Scratch.hpp"  // rolltui::ThreadHandle
#include "rolltui/Unicode.hpp"

namespace rolltui {

namespace {

// WORKING MEMORY for the pure checks, owned per thread by the shim: `check_input` counts
// GRAPHEMES for a Text field's max_len, and the cluster walk needs somewhere to work.
RolltuiUnicodeScratch* check_scratch() {
  static thread_local ThreadHandle<RolltuiUnicodeScratch, rolltui_u_scratch_new, rolltui_u_scratch_free> h;
  return h.get();
}

RolltuiDrawScratch* draw_scratch() {
  static thread_local ThreadHandle<RolltuiDrawScratch, rolltui_draw_scratch_new, rolltui_draw_scratch_free> h;
  return h.get();
}

// THE SEVEN ROLES A DRAW NEEDS, handed in — `rolltui/Style.hpp` is the one place these names
// exist (Phase 15 m2's rule).
constexpr RolltuiMenuRoles kRoles = {
    /*item=*/static_cast<unsigned char>(Role::menu_item),
    /*selected=*/static_cast<unsigned char>(Role::menu_selected),
    /*breadcrumb=*/static_cast<unsigned char>(Role::menu_breadcrumb),
    /*shortcut=*/static_cast<unsigned char>(Role::menu_shortcut),
    /*text_muted=*/static_cast<unsigned char>(Role::text_muted),
    /*warning=*/static_cast<unsigned char>(Role::warning),
    /*scroll_marker=*/static_cast<unsigned char>(Role::scroll_marker),
};

constexpr RolltuiInputRoles kInputRoles = {
    /*text=*/static_cast<unsigned char>(Role::input_text),
    /*selection=*/static_cast<unsigned char>(Role::selection),
    /*placeholder=*/static_cast<unsigned char>(Role::input_placeholder),
};

// THE FIFTEEN ACTION NAMES, in the order `RolltuiMenuActions` declares. The last one is a
// POINTER to the input widget's own table, so there is one table of those names in the
// library and a typed field forwards a key to the editor without a second spelling.
const RolltuiMenuActions& menu_actions() {
  static const RolltuiMenuActions a = {
      "menu.up",        "menu.down",   "menu.page_up",  "menu.page_down", "menu.first",
      "menu.last",      "menu.activate", "menu.descend", "menu.ascend",   "menu.back",
      "menu.erase",     "edit.commit", "edit.cancel",   "edit.step_up",   "edit.step_down",
      input_actions(),
  };
  return a;
}

InputCheck to_check(const RolltuiInputCheck& c) {
  InputCheck out;
  out.prefix_ok = c.prefix_ok != 0;
  out.valid = c.valid != 0;
  out.reason = c.reason.str();
  out.canonical = c.canonical.str();
  return out;
}

}  // namespace

std::string_view input_type_name(InputType t) {
  std::size_t n = 0;
  const char* p = rolltui_input_type_name(static_cast<unsigned char>(t), &n);
  return std::string_view(p, n);
}

std::optional<InputType> input_type_from_name(std::string_view name) {
  unsigned char t = 0;
  if (!rolltui_input_type_from_name(name.data(), name.size(), &t)) return std::nullopt;
  return static_cast<InputType>(t);
}

InputCheck check_input(const InputSpec& spec, std::string_view text) {
  RolltuiInputCheck c{};
  rolltui_check_input(&spec, text.data(), text.size(), check_scratch(), &c);
  const InputCheck out = to_check(c);
  rolltui_input_check_release(&c);
  return out;
}

std::string input_hint(const InputSpec& spec) {
  Str s;
  rolltui_input_hint(&spec, &s);
  return s.str();
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
  std::vector<std::pair<std::string, const Value*>> spec_keys;
  for (const auto& [k, x] : v.obj) {
    const std::string at = where + "." + k;
    if (k == "id" || k == "label" || k == "shortcut" || k == "value" || k == "action") {
      if (!x.is_string()) { rep.bad_values.push_back(at + ": expected a string"); continue; }
      if (k == "id") it.id = x.str;
      else if (k == "label") it.label = x.str;
      else if (k == "shortcut") it.shortcut = x.str;
      else if (k == "action") it.action_name = x.str;
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
    } else if (k == "type" || k == "min" || k == "max" || k == "step" || k == "precision" || k == "max_len" || k == "min_len" || k == "optional" ||
               k == "validator" || k == "hint") {
      spec_keys.emplace_back(k, &x);
    } else {
      rep.unknown_keys.push_back(at);
    }
  }
  if (!kind_given) it.kind = v.has("items") ? MenuItem::Kind::Submenu : MenuItem::Kind::Action;
  // An action's shortcut is the bindings' to say (Menu.hpp): a file that also spells one
  // out is stating the same fact twice, and the second copy is what goes stale.
  if (!it.action_name.empty() && !it.shortcut.empty()) {
    rep.bad_values.push_back(where + ".shortcut: an item with an \"action\" takes its shortcut from the bindings (ignored)");
    it.shortcut.clear();
  }
  for (const auto& [k, xp] : spec_keys) {
    const Value& x = *xp;
    const std::string at = where + "." + k;
    if (it.kind != MenuItem::Kind::Input) { rep.unknown_keys.push_back(at + " (only an input has it)"); continue; }
    if (k == "type") {
      auto t = x.is_string() ? input_type_from_name(x.str) : std::nullopt;
      if (!t) rep.bad_values.push_back(at + ": expected text | int | float | color | size | dim | name");
      else it.spec.type = *t;
    } else if (k == "min" || k == "max" || k == "step") {
      if (!x.is_number()) { rep.bad_values.push_back(at + ": expected a number"); continue; }
      (k == "min" ? it.spec.min : k == "max" ? it.spec.max : it.spec.step) = x.num;
    } else if (k == "precision" || k == "max_len" || k == "min_len") {
      if (!x.is_number() || x.num < 0 || x.num != std::floor(x.num)) { rep.bad_values.push_back(at + ": expected a whole number ≥ 0"); continue; }
      if (k == "precision") it.spec.precision = static_cast<int>(x.num);
      else if (k == "max_len") it.spec.max_len = static_cast<std::size_t>(x.num);
      else it.spec.min_len = static_cast<std::size_t>(x.num);
    } else if (k == "optional") {
      if (!x.is_bool()) { rep.bad_values.push_back(at + ": expected true or false"); continue; }
      it.spec.optional = x.b;
    } else if (k == "validator" || k == "hint") {
      if (!x.is_string()) { rep.bad_values.push_back(at + ": expected a string"); continue; }
      (k == "validator" ? it.spec.validator : it.spec.hint) = x.str;
    }
  }
  if (it.kind == MenuItem::Kind::Input && it.spec.min > it.spec.max) rep.bad_values.push_back(where + ": min is above max");
  if (it.kind == MenuItem::Kind::Input && !it.spec.validator.empty() && it.spec.type != InputType::Text)
    rep.bad_values.push_back(where + ".validator: only a text input takes a validator (a typed input validates itself)");
  if (it.id.empty()) rep.bad_values.push_back(where + ": an item needs an \"id\"");
  else if (std::find(ids.begin(), ids.end(), it.id.view()) != ids.end()) rep.bad_values.push_back(where + ".id: duplicate id '" + it.id + "'");
  else ids.emplace_back(it.id.view());
  if (it.label.empty()) it.label = it.id;
  return it;
}

Value item_to_json(const MenuItem& it) {
  Value o = Value::object();
  o.set("id", Value::string(it.id.str()));
  if (!(it.label == it.id)) o.set("label", Value::string(it.label.str()));
  const bool implied = (it.kind == MenuItem::Kind::Submenu && !it.children.empty()) ||
                       (it.kind == MenuItem::Kind::Action && it.children.empty());
  if (!implied) o.set("kind", Value::string(kind_name(it.kind)));
  if (!it.action_name.empty()) o.set("action", Value::string(it.action_name.str()));
  // An action's shortcut is derived and is never written back (apply_shortcuts fills it
  // from the live chords), so a round trip cannot bake one moment's keys into a file.
  if (it.action_name.empty() && !it.shortcut.empty()) o.set("shortcut", Value::string(it.shortcut.str()));
  if (!it.enabled) o.set("enabled", Value::boolean(false));
  if (it.checked) o.set("checked", Value::boolean(true));
  if (!it.value.empty()) o.set("value", Value::string(it.value.str()));
  if (it.kind == MenuItem::Kind::Input) {
    const InputSpec d;
    const InputSpec& s = it.spec;
    if (s.type != d.type) o.set("type", Value::string(std::string(input_type_name(s.type))));
    if (s.min != d.min) o.set("min", Value::number(s.min));
    if (s.max != d.max) o.set("max", Value::number(s.max));
    if (s.step != d.step) o.set("step", Value::number(s.step));
    if (s.precision != d.precision) o.set("precision", Value::number(s.precision));
    if (s.max_len != d.max_len) o.set("max_len", Value::number(static_cast<double>(s.max_len)));
    if (s.min_len != d.min_len) o.set("min_len", Value::number(static_cast<double>(s.min_len)));
    if (s.optional) o.set("optional", Value::boolean(true));
    if (!s.validator.empty()) o.set("validator", Value::string(s.validator.str()));
    if (!s.hint.empty()) o.set("hint", Value::string(s.hint.str()));
  }
  if (!it.children.empty()) {
    Value arr = Value::array();
    for (const MenuItem& c : it.children) arr.arr.push_back(item_to_json(c));
    o.set("items", std::move(arr));
  }
  return o;
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

// The shipped menu files, embedded by cmake/embed_presets.cmake from
// rolltui/presets/menus/ — the same machinery as the shipped presets, so a menu that
// ships is a real file in the source tree and not a string in a .cpp (Phase 10 m3).
namespace embedded {
extern const std::pair<std::string_view, std::string_view> kMenus[];
extern const std::size_t kMenuCount;
}  // namespace embedded

std::string_view shipped_menu(std::string_view name) {
  for (std::size_t i = 0; i < embedded::kMenuCount; ++i)
    if (embedded::kMenus[i].first == name) return embedded::kMenus[i].second;
  return {};
}

std::vector<std::string_view> shipped_menu_names() {
  std::vector<std::string_view> out;
  for (std::size_t i = 0; i < embedded::kMenuCount; ++i) out.push_back(embedded::kMenus[i].first);
  return out;
}


// ---- the widget ------------------------------------------------------------------------

namespace {

// What crosses instead of the `std::function` map: one callback that ANSWERS for the whole
// registry. The callables stay where they are (Menu.hpp), which is the same trade
// `rolltui_bindings.h` makes for "is this scope the library's".
int call_validator(void* ctx, const char* name, std::size_t nlen, const char* text, std::size_t tlen,
                   RolltuiStr* why) {
  auto* vs = static_cast<const std::vector<std::pair<std::string, Menu::Validator>>*>(ctx);
  for (const auto& [n, fn] : *vs)
    if (n == std::string_view(name, nlen)) {
      if (std::optional<std::string> w = fn(std::string_view(text, tlen))) *why = *w;
      return 1;
    }
  return 0;
}

void collect_validators(const MenuItem& it, std::vector<std::string>& out) {
  if (static_cast<unsigned char>(it.kind) == ROLLTUI_MENU_INPUT && !it.spec.validator.empty() &&
      std::find(out.begin(), out.end(), it.spec.validator.view()) == out.end())
    out.emplace_back(it.spec.validator.view());
  for (const MenuItem& c : it.children) collect_validators(c, out);
}

void collect_item_actions(const MenuItem& it, std::vector<std::pair<std::string, std::string>>& out) {
  if (!it.action_name.empty()) out.emplace_back(it.id.str(), it.action_name.str());
  for (const MenuItem& c : it.children) collect_item_actions(c, out);
}

void fill_shortcuts(MenuItem& it, const Bindings& b) {
  // An action no layout declares is INERT — the table keeps its chords but nothing can emit
  // it (Bindings.hpp) — so it has no shortcut to show. Printing its chords anyway promises a
  // key that cannot fire, which is exactly the lie apply_shortcuts exists to remove.
  if (!it.action_name.empty())
    it.shortcut = b.has(it.action_name.view()) ? b.chords_text(it.action_name.view()) : std::string();
  for (MenuItem& c : it.children) fill_shortcuts(c, b);
}

}  // namespace

Menu::Menu() {
  InputOptions o;
  o.single_line = 1;
  o.prompt.clear();
  edit_.set_options(o);
  rolltui_menu_set_validator_fn(m_.get(), call_validator, &validators_);
}

Menu::Menu(MenuItem root) : Menu() { set_root(std::move(root)); }

void Menu::set_root(MenuItem root) { rolltui_menu_set_root(m_.get(), &root); }

MenuItem* Menu::find(std::string_view id) { return rolltui_menu_find(m_.get(), id.data(), id.size()); }
const MenuItem* Menu::find(std::string_view id) const {
  return rolltui_menu_find(const_cast<RolltuiMenu*>(m_.get()), id.data(), id.size());
}

bool Menu::set_value(std::string_view id, std::string value) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->value = std::move(value);
  return true;
}
bool Menu::set_checked(std::string_view id, bool checked) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->checked = static_cast<unsigned char>(checked);
  return true;
}
bool Menu::set_enabled(std::string_view id, bool enabled) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->enabled = static_cast<unsigned char>(enabled);
  return true;
}

bool Menu::set_options(std::string_view id, std::vector<MenuItem> options) {
  RolltuiMenuItemList list;
  for (MenuItem& o : options) list.push_back(std::move(o));
  return rolltui_menu_set_options(m_.get(), id.data(), id.size(), &list) != 0;
}

void Menu::set_validator(std::string_view name, Validator v) {
  for (auto& [n, fn] : validators_)
    if (n == name) {
      fn = std::move(v);
      return;
    }
  validators_.emplace_back(std::string(name), std::move(v));
}

std::vector<std::string> Menu::unknown_validators() const {
  std::vector<std::string> used, out;
  collect_validators(root(), used);
  for (const std::string& u : used) {
    bool known = false;
    for (const auto& [n, fn] : validators_) known |= n == u;
    if (!known) out.push_back(u);
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> Menu::item_actions() const {
  std::vector<std::pair<std::string, std::string>> out;
  collect_item_actions(root(), out);
  return out;
}

void Menu::apply_shortcuts(const Bindings& b) { fill_shortcuts(*rolltui_menu_root(m_.get()), b); }

void Menu::reset() { rolltui_menu_reset(m_.get()); }

std::vector<std::size_t> Menu::path() const {
  const std::size_t* p = nullptr;
  const std::size_t n = rolltui_menu_path(m_.get(), &p);
  return std::vector<std::size_t>(p, p + n);
}

std::vector<std::size_t> Menu::visible() const {
  const std::size_t* v = nullptr;
  const std::size_t n = rolltui_menu_visible(m_.get(), &v);
  return std::vector<std::size_t>(v, v + n);
}

std::size_t Menu::scroll_total() const { return rolltui_menu_visible(m_.get(), nullptr); }

std::string_view Menu::filter() const {
  std::size_t n = 0;
  const char* p = rolltui_menu_filter(m_.get(), &n);
  return std::string_view(p, n);
}

std::string Menu::breadcrumb() const {
  Str s;
  rolltui_menu_breadcrumb(m_.get(), &s);
  return s.str();
}

std::string_view Menu::edit_reason() const {
  std::size_t n = 0;
  const char* p = rolltui_menu_edit_reason(m_.get(), &n);
  return std::string_view(p, n);
}

std::string_view Menu::flat_label(std::size_t i) const {
  std::size_t n = 0;
  const char* p = rolltui_menu_flat_label(m_.get(), i, &n);
  return std::string_view(p, n);
}

MenuEvent Menu::handle(const Event& e, const Bindings& bindings) {
  RolltuiEvent ev{};
  std::string_view paste;
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_KEY;
    ev.key = chord_of(*k);
  } else if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_MOUSE;
    ev.mouse = *m;
  } else if (const PasteEvent* p = std::get_if<PasteEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_PASTE;
    paste = p->text;
    ev.text = paste.data();
    ev.text_len = paste.size();
  } else {
    return {};
  }
  RolltuiMenuEvent out{};
  rolltui_menu_handle(m_.get(), &ev, bindings.handle(), &menu_actions(), &out);
  MenuEvent r{static_cast<MenuEvent::Kind>(out.kind), out.id.str(), out.value.str(), out.checked != 0};
  rolltui_menu_event_release(&out);
  return r;
}

void Menu::draw(Frame& f, const Theme& theme, bool focused) const {
  rolltui_menu_draw(m_.get(), f.handle(), draw_scratch(), theme.styles.data(), &kRoles, &kInputRoles,
                    focused);
}

Rect Menu::area() const {
  Rect r;
  rolltui_menu_area(m_.get(), &r);
  return r;
}

}  // namespace rolltui
