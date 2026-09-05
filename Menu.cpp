// rolltui/Menu.cpp — the SHIM over `rolltui/c/rolltui_menu.h`: the shipped menu files, the
// styling vocabulary, the thirteen action names, and the validator registry that stays where
// its callables are. `c/rolltui_menu.c` is the one implementation of the widget, the tree and
// (Phase 17 m1) the JSON loader; this file is the C++ API over it (Phase 15 m5, extended).
//
// WHAT STAYS HERE AND WHY: the shipped table (a table of names, not an algorithm — the m3
// split for `Theme`); the validator registry (a `std::function` map the C asks about rather
// than holds, `rolltui_menu.h`'s own trade for "which scopes are the library's"); and three
// TREE WALKS with no widget state in them (`item_actions`, `unknown_validators`,
// `apply_shortcuts`), because the C would gain nothing from them but a second place to know
// what `Bindings::chords_text` means. The JSON loader (`menu_from_json`/`menu_to_json`) moved
// to C at Phase 17 m1, once `rolltui_json.h` existed to build it on — see that function's own
// note and `rolltui_menu.h`'s header comment for why a menu file, unlike Theme's and Layout's,
// had nothing left behind.
#include "rolltui/Menu.hpp"
#include "rolltui/c/rolltui_embedded.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
//
// PHASE 17 m1: the whole walk — `item_from_json`/`item_to_json`, the kind-name table, the
// tree-wide/per-choice id sets, the leading-dot fix-up — moved to C
// (`rolltui_menu_parse_json`/`rolltui_menu_dump_json`) now that `rolltui_json.h` exists to
// build it on. Unlike Theme's and Layout's loaders, a menu file has no sibling algorithm
// staying C++ to entangle it (Theme.cpp's `load_theme`; `rolltui_layout.h`'s own header
// comment states why theirs stays), so nothing of the walk was left behind here — see
// `rolltui/c/rolltui_menu.h`'s header comment.

std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report) {
  MenuItem it;  // default-constructed: Action, everything else empty — the caller-owned node
                // rolltui_menu_parse_json fills in place rather than allocating.
  RolltuiMenuLoadReport rep{};
  const int ok = rolltui_menu_parse_json(json_text.data(), json_text.size(), &it, &rep);
  report.error = rep.error.str();
  report.unknown_keys.clear();
  report.bad_values.clear();
  report.unknown_keys.reserve(rep.unknown_keys_n);
  report.bad_values.reserve(rep.bad_values_n);
  for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) report.unknown_keys.emplace_back(rep.unknown_keys[i].view());
  for (std::size_t i = 0; i < rep.bad_values_n; ++i) report.bad_values.emplace_back(rep.bad_values[i].view());
  rolltui_menu_load_report_release(&rep);
  if (!ok) return std::nullopt;
  return it;
}

std::string menu_to_json(const MenuItem& root) {
  RolltuiStr out;
  rolltui_menu_dump_json(&root, &out);
  return out.str();
}

// The shipped menu files, embedded by cmake/embed_presets.cmake from
// rolltui/presets/menus/ — the same machinery as the shipped presets, so a menu that
// ships is a real file in the source tree and not a string in a .cpp (Phase 10 m3).

std::string_view shipped_menu(std::string_view name) {
  for (std::size_t i = 0; i < rolltui_kMenuCount; ++i)
    if (std::string_view(rolltui_kMenus[i].name) == name) return rolltui_kMenus[i].text;
  return {};
}

std::vector<std::string_view> shipped_menu_names() {
  std::vector<std::string_view> out;
  for (std::size_t i = 0; i < rolltui_kMenuCount; ++i) out.push_back(rolltui_kMenus[i].name);
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

// The editor's shape (single line, no prompt) is `rolltui_menu_new`'s now — this constructor
// and `menu_test.cpp`'s holder were writing the identical two lines right after it, which is
// rule 5's tell. What is left here is the one thing that cannot cross: the host validator
// registry, ASKED for by name rather than moved (rolltui_menu.h states the trade).
Menu::Menu() { rolltui_menu_set_validator_fn(m_.get(), call_validator, &validators_); }

Menu::Menu(MenuItem root) : Menu() { set_root(std::move(root)); }

void Menu::set_root(MenuItem root) { rolltui_menu_set_root(m_.get(), &root); }

MenuItem* Menu::find(std::string_view id) { return rolltui_menu_find(m_.get(), id.data(), id.size()); }
const MenuItem* Menu::find(std::string_view id) const {
  return rolltui_menu_find(const_cast<RolltuiMenu*>(m_.get()), id.data(), id.size());
}

bool Menu::set_value(std::string_view id, std::string value) {
  return rolltui_menu_set_value(m_.get(), id.data(), id.size(), value.data(), value.size()) != 0;
}
bool Menu::set_checked(std::string_view id, bool checked) {
  return rolltui_menu_set_checked(m_.get(), id.data(), id.size(), checked) != 0;
}
bool Menu::set_enabled(std::string_view id, bool enabled) {
  return rolltui_menu_set_enabled(m_.get(), id.data(), id.size(), enabled) != 0;
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

std::vector<std::pair<std::string, std::string>> item_actions(const MenuItem& root) {
  std::vector<std::pair<std::string, std::string>> out;
  collect_item_actions(root, out);
  return out;
}

void apply_shortcuts(MenuItem& root, const Bindings& b) { fill_shortcuts(root, b); }

std::vector<std::pair<std::string, std::string>> Menu::item_actions() const { return rolltui::item_actions(root()); }

void Menu::apply_shortcuts(const Bindings& b) { rolltui::apply_shortcuts(*rolltui_menu_root(m_.get()), b); }

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

// THE HANDLE-TAKING FORM IS THE REAL ONE (Phase 17 m1c). Three hosts drive a menu the window
// table owns, and what they hold is a `RolltuiMenu*`; the two things they cannot do for
// themselves are the event conversion (Keys.hpp's `c_event_of`, one copy now) and the menu
// scope's ACTION NAMES, which are this file's and which `rolltui_menu.h` says never cross.
MenuEvent menu_handle(RolltuiMenu* m, const Event& e, const Bindings& bindings) {
  if (std::holds_alternative<ResizeEvent>(e)) return {};
  const RolltuiEvent ev = c_event_of(e);
  RolltuiMenuEvent out{};
  rolltui_menu_handle(m, &ev, bindings.handle(), &menu_actions(), &out);
  MenuEvent r{static_cast<MenuEvent::Kind>(out.kind), out.id.str(), out.value.str(), out.checked != 0};
  rolltui_menu_event_release(&out);
  return r;
}

MenuEvent Menu::handle(const Event& e, const Bindings& bindings) { return menu_handle(m_.get(), e, bindings); }

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
