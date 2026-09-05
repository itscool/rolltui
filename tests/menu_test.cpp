//
// menu_test.cpp — the menu widget (milestone 11): every key in Menu.hpp's table, the
// five item kinds, the filter, the breadcrumb, palette mode, the JSON round trip and
// the degenerate-size rule (0 or 1 cells in either dimension draws nothing outside
// the area and never crashes).
//
// PHASE 17 m2: converted off the C++ shim (`rolltui/Menu.hpp`/`Menu.cpp`, and the
// `rolltui/Bindings.hpp`/`Bindings.cpp` it in turn depended on for `default_bindings()`),
// both being deleted — this file now calls `rolltui/c/rolltui_menu.h`,
// `rolltui/c/rolltui_menu_tree.h` and `rolltui/c/rolltui_bindings.h` directly, reached
// only through the umbrella `rolltui/rolltui.h`. `Frame`/`Theme`/`Rect`/`Role`/`Style`
// (Screen.hpp/Theme.hpp) are NOT part of that layer and are unchanged — they are either
// one-definition aliases of the C structs already, or permanent C++-only vocabulary with
// no C counterpart, the same finding `input_test.cpp` recorded for its own conversion.
// `MenuItem`/`InputSpec` likewise ARE `RolltuiMenuItem`/`RolltuiInputSpec` (one
// definition, Phase 15 m5), so the aliases below reproduce exactly what Menu.hpp's own
// `using` declarations gave every call site — `MenuItem::toggle(...)`,
// `it.children.push_back(...)` and the rest are unchanged text below for that reason.
// The shim's own composition (the widget class, the action vocabulary,
// `default_bindings()`'s assembly) has no home yet on the C side, so it is reproduced
// here as local fixture code mirroring `Menu.cpp`/`Bindings.cpp` line for line, per the
// instruction to treat the shim as the mapping rather than guess at one.
//
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

// The embedded preset tables: permanent generated C++ data (rolltui/cmake/embed_presets.cmake),
// redeclared here exactly as Bindings.cpp/Layout.cpp/Menu.cpp/Presets.cpp each already do
// independently. Declared before every use below.
namespace rolltui::embedded {
extern const std::pair<std::string_view, std::string_view> kBindingsPresets[];
extern const std::size_t kBindingsPresetCount;
extern const std::pair<std::string_view, std::string_view> kLayoutPresets[];
extern const std::size_t kLayoutPresetCount;
extern const std::pair<std::string_view, std::string_view> kMenus[];
extern const std::size_t kMenuCount;
}  // namespace rolltui::embedded

namespace {

// ---- PHASE 15 m5: MenuItem/InputSpec ARE the C structs (one definition) — the same
// aliases Menu.hpp declared, reproduced here so every `MenuItem::toggle(...)`,
// `spec.precision = ...` and `it.children.push_back(...)` below is unchanged text. ----
using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;

// `rolltui::Key` (Keys.hpp) is gone with the shim; only the values this file actually
// uses are reproduced, against the same ROLLTUI_KEY_* constants Keys.hpp itself checked
// its enum against (rolltui_keys.h's own static_assert).
enum class Key : unsigned char {
  Char = ROLLTUI_KEY_CHAR,
  Backspace = ROLLTUI_KEY_BACKSPACE,
  Delete = ROLLTUI_KEY_DELETE,
  Down = ROLLTUI_KEY_DOWN,
  End = ROLLTUI_KEY_END,
  Enter = ROLLTUI_KEY_ENTER,
  Escape = ROLLTUI_KEY_ESCAPE,
  Home = ROLLTUI_KEY_HOME,
  Left = ROLLTUI_KEY_LEFT,
  PageUp = ROLLTUI_KEY_PAGEUP,
  Right = ROLLTUI_KEY_RIGHT,
  Up = ROLLTUI_KEY_UP,
};
// `rolltui::MouseEvent` (Keys.hpp) was `using MouseEvent = RolltuiMouseEvent;` — the
// struct itself is one-definition and permanent (rolltui_keys.h), only the alias name
// was the shim's; reproduced for the same reason the two above are.
using MouseEvent = RolltuiMouseEvent;
// `rolltui::PasteEvent` was a bare `{ std::string text; }` with no C counterpart (a
// paste's bytes travel in `RolltuiEvent::text`/`text_len`, never as a standalone type) —
// reproduced as local fixture data.
struct PasteEvent {
  std::string text;
};

// ---- mirrors rolltui::MenuEvent (Menu.hpp) — `Kind` as a NESTED enum so every
// `MenuEvent::Kind::X` below is unchanged text. ----
struct MenuEvent {
  enum class Kind : unsigned char {
    None = ROLLTUI_MENU_EVENT_NONE,
    Activate = ROLLTUI_MENU_EVENT_ACTIVATE,
    Toggle = ROLLTUI_MENU_EVENT_TOGGLE,
    Choose = ROLLTUI_MENU_EVENT_CHOOSE,
    Input = ROLLTUI_MENU_EVENT_INPUT,
    Closed = ROLLTUI_MENU_EVENT_CLOSED,
  };
  Kind kind = Kind::None;
  std::string id;
  std::string value;
  bool checked = false;
};

// ---- mirrors rolltui::MenuLoadReport (Menu.hpp). ----
struct MenuLoadReport {
  std::string error;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// ---- the library's 59 actions, READ FROM THE C rather than copied ----------------------
// One table, in `c/rolltui_library_actions.c`. This file carried a verbatim duplicate until
// 2026-09-04; it was the fourth. The accessors BORROW into static literals, so a
// string_view onto them is correct and a std::string in between would dangle.
struct ActionInfo {
  std::string_view name, description;
};
const std::vector<ActionInfo>& library_actions() {
  static const std::vector<ActionInfo> t = [] {
    std::vector<ActionInfo> v;
    const size_t n = rolltui_library_action_count();
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      size_t nl = 0, dl = 0;
      const char* nm = rolltui_library_action_name(i, &nl);
      const char* ds = rolltui_library_action_description(i, &dl);
      v.push_back({std::string_view(nm, nl), std::string_view(ds, dl)});
    }
    return v;
  }();
  return t;
}
std::string_view scope_of(std::string_view action) {
  const std::size_t dot = action.find('.');
  return dot == std::string_view::npos ? action : action.substr(0, dot);
}
bool library_scope(std::string_view scope) {
  for (const ActionInfo& a : library_actions())
    if (scope_of(a.name) == scope) return true;
  return false;
}
int is_library_scope_cb(void*, const char* scope, std::size_t len) {
  return library_scope(std::string_view(scope, len)) ? 1 : 0;
}
constexpr std::pair<const char*, const char*> kLegacyActions[] = {
    {"playground.cycle_theme", "studio.cycle_theme"},
    {"playground.reload", "studio.reload"},
    {"playground.quit", "studio.quit"},
};
std::optional<std::string> migrated_action(std::string_view legacy) {
  for (const auto& [from, to] : kLegacyActions)
    if (legacy == from) return std::string(to);
  return std::nullopt;
}
int migrate_cb(void*, const char* legacy, std::size_t len, char* out, std::size_t* out_len) {
  const std::optional<std::string> to = migrated_action(std::string_view(legacy, len));
  if (!to) return 0;
  const std::size_t n = std::min(to->size(), static_cast<std::size_t>(ROLLTUI_ACTION_NAME_MAX));
  std::memcpy(out, to->data(), n);
  *out_len = n;
  return 1;
}
std::size_t reason_cb(void*, const RolltuiChord* k, unsigned char protocol, char* out, std::size_t cap) {
  // Mirrors rolltui::undeliverable_reason (Keys.cpp) — see bindings_test.cpp's identical
  // copy for why this stays test-adjacent fixture data rather than a shared header.
  static constexpr std::string_view kReasons[6] = {
      "",
      "it is not a key",
      "shift on a character key is the shifted character itself, which no terminal reports as a chord",
      "it needs the kitty keyboard protocol or xterm's modifyOtherKeys",
      "it needs the kitty keyboard protocol",
      "no keyboard protocol this library speaks can report it",
  };
  const int code = rolltui_key_undeliverable_reason(k, protocol);
  const std::string_view r = kReasons[static_cast<std::size_t>(code) < 6 ? static_cast<std::size_t>(code) : 0];
  const std::size_t n = std::min(r.size(), cap);
  std::memcpy(out, r.data(), n);
  return n;
}
constexpr std::string_view kEnterAction = "input.submit";
RolltuiBindings* bindings_new() {
  RolltuiBindings* b = rolltui_bindings_new();
  rolltui_bindings_set_enter_rule(b, kEnterAction.data(), kEnterAction.size());
  for (const ActionInfo& a : library_actions())
    rolltui_bindings_add_action(b, a.name.data(), a.name.size(), a.description.data(), a.description.size());
  return b;
}
struct ActionDecl {
  std::string name, description;
};
// The `tools` half of Bindings::declare()/suggest() is never exercised here (menu_test
// only needs the shipped default's own app.* actions), so it is not reproduced — an
// empty-tools declare() is exactly rolltui_bindings_undeclare_others + the add_action loop.
void bindings_declare(RolltuiBindings* b, const std::vector<ActionDecl>& declared) {
  rolltui_bindings_undeclare_others(b, is_library_scope_cb, nullptr);
  for (const ActionDecl& d : declared)
    rolltui_bindings_add_action(b, d.name.data(), d.name.size(), d.description.data(), d.description.size());
}
std::string bindings_chords_text(const RolltuiBindings* b, std::string_view action) {
  const unsigned char p = rolltui_key_active_protocol();
  const std::size_t n = rolltui_bindings_chord_count(b, action.data(), action.size());
  std::string s;
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c{};
    if (!rolltui_bindings_chord_at(b, action.data(), action.size(), i, &c)) continue;
    if (!rolltui_key_deliverable(&c, p)) continue;
    char buf[ROLLTUI_CHORD_STRING_MAX];
    const std::size_t len = rolltui_chord_display(&c, buf, sizeof buf);
    if (!s.empty()) s += ", ";
    s.append(buf, len);
  }
  return s;
}
std::string_view default_bindings_json() {
  for (std::size_t i = 0; i < rolltui::embedded::kBindingsPresetCount; ++i)
    if (rolltui::embedded::kBindingsPresets[i].first == "default") return rolltui::embedded::kBindingsPresets[i].second;
  return "";
}
std::string_view builtin_layout_json(std::string_view name) {
  for (std::size_t i = 0; i < rolltui::embedded::kLayoutPresetCount; ++i)
    if (rolltui::embedded::kLayoutPresets[i].first == name) return rolltui::embedded::kLayoutPresets[i].second;
  return "";
}
const std::vector<ActionDecl>& shipped_default_actions() {
  static const std::vector<ActionDecl> decls = [] {
    std::vector<ActionDecl> out;
    const std::string_view text = builtin_layout_json("default");
    RolltuiJsonValue* v = rolltui_json_parse(text.data(), text.size(), nullptr);
    if (v) {
      RolltuiLayoutAction* actions = nullptr;
      std::size_t n = 0, cap = 0;
      rolltui_layout_read_actions_key(v, &actions, &n, &cap);
      out.reserve(n);
      for (std::size_t i = 0; i < n; ++i) out.push_back({actions[i].name.str(), actions[i].description.str()});
      rolltui_layout_actions_free(actions, n);
      rolltui_json_free(v);
    }
    return out;
  }();
  return decls;
}
// Mirrors Bindings.cpp's default_bindings(): builds the shipped table fresh, aborting on
// the same two build mistakes it does. OWNED — every caller below frees it.
RolltuiBindings* default_bindings() {
  RolltuiBindings* d = bindings_new();
  RolltuiBindingsReport rep{};
  const int ok = rolltui_bindings_load_json(d, default_bindings_json().data(), default_bindings_json().size(),
                                            ROLLTUI_PROTOCOL_LEGACY, is_library_scope_cb, nullptr, migrate_cb, nullptr,
                                            reason_cb, nullptr, &rep);
  if (!ok || !rolltui_bindings_report_clean(&rep)) {
    RolltuiStr summary;
    rolltui_bindings_report_summary(&rep, &summary);
    std::fprintf(stderr, "rolltui: the shipped default bindings are broken: %s\n", summary.c_str());
    std::abort();
  }
  rolltui_bindings_report_release(&rep);
  bindings_declare(d, shipped_default_actions());
  return d;
}
// A shared, process-wide copy for the hot path (Menu::handle's single-argument overload,
// called on nearly every keystroke below) — default_bindings() is a pure function of
// static embedded data, so caching it changes nothing it can produce, only how often it
// is rebuilt. Freed at process exit via the static's own destructor.
const RolltuiBindings* cached_default_bindings() {
  static std::unique_ptr<RolltuiBindings, void (*)(RolltuiBindings*)> b(default_bindings(), rolltui_bindings_free);
  return b.get();
}

std::optional<RolltuiChord> parse_chord(std::string_view text) {
  RolltuiChord c{};
  if (!rolltui_chord_parse(text.data(), text.size(), &c)) return std::nullopt;
  return c;
}

std::string input_hint(const InputSpec& spec) {
  RolltuiStr s;
  rolltui_input_hint(&spec, &s);
  return s.str();
}

std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report) {
  MenuItem it;  // default: Action kind, everything empty — filled IN PLACE, not allocated
  RolltuiMenuLoadReport rep{};
  const int ok = rolltui_menu_parse_json(json_text.data(), json_text.size(), &it, &rep);
  report.error = rep.error.str();
  report.unknown_keys.clear();
  report.bad_values.clear();
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
std::string_view shipped_menu(std::string_view name) {
  for (std::size_t i = 0; i < rolltui::embedded::kMenuCount; ++i)
    if (rolltui::embedded::kMenus[i].first == name) return rolltui::embedded::kMenus[i].second;
  return {};
}
std::vector<std::string_view> shipped_menu_names() {
  std::vector<std::string_view> out;
  for (std::size_t i = 0; i < rolltui::embedded::kMenuCount; ++i) out.push_back(rolltui::embedded::kMenus[i].first);
  return out;
}

// ---- the widget: mirrors rolltui::Menu (Menu.hpp/Menu.cpp) — a thin local class over
// the C widget and its owned editor, kept under the SAME name and method signatures so
// this file's several hundred `m.foo(...)` call sites stay unchanged; every method body
// is a direct C call, which is the actual point of the conversion. ----

using Validator = std::function<std::optional<std::string>(std::string_view)>;

int call_validator(void* ctx, const char* name, std::size_t nlen, const char* text, std::size_t tlen, RolltuiStr* why) {
  auto* vs = static_cast<const std::vector<std::pair<std::string, Validator>>*>(ctx);
  for (const auto& [n, fn] : *vs)
    if (n == std::string_view(name, nlen)) {
      if (std::optional<std::string> w = fn(std::string_view(text, tlen))) *why = *w;
      return 1;
    }
  return 0;
}

RolltuiDrawScratch* draw_scratch() {
  static std::unique_ptr<RolltuiDrawScratch, void (*)(RolltuiDrawScratch*)> s(rolltui_draw_scratch_new(),
                                                                              rolltui_draw_scratch_free);
  return s.get();
}

constexpr RolltuiMenuRoles kMenuRoles = {
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
// The input widget's thirty action names — copied from Input.cpp's kActions, same as
// input_test.cpp's own copy, since a caller of the menu (which forwards to the editor)
// has to hand this table over itself.
constexpr RolltuiInputActions kInputActions = {
    "input.submit",             "input.newline",            "input.backspace",
    "input.delete",             "input.kill_word_backward", "input.kill_word_forward",
    "input.kill_to_line_start", "input.kill_to_line_end",   "input.left",
    "input.right",              "input.word_left",          "input.word_right",
    "input.line_start",         "input.line_end",           "input.up",
    "input.down",               "input.select_left",        "input.select_right",
    "input.select_word_left",   "input.select_word_right",  "input.select_line_start",
    "input.select_line_end",    "input.select_up",          "input.select_down",
    "input.select_all",         "input.clear_selection",    "input.copy",
    "input.eof",                "input.undo",                "input.redo",
};
const RolltuiMenuActions& menu_actions() {
  static const RolltuiMenuActions a = {
      "menu.up",       "menu.down",     "menu.page_up",  "menu.page_down", "menu.first",
      "menu.last",     "menu.activate", "menu.descend",  "menu.ascend",    "menu.back",
      "menu.erase",    "edit.commit",   "edit.cancel",   "edit.step_up",   "edit.step_down",
      &kInputActions,
  };
  return a;
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
void fill_shortcuts(MenuItem& it, const RolltuiBindings* b) {
  if (!it.action_name.empty())
    it.shortcut = rolltui_bindings_has(b, it.action_name.data(), it.action_name.size())
                      ? bindings_chords_text(b, it.action_name.view())
                      : std::string();
  for (MenuItem& c : it.children) fill_shortcuts(c, b);
}

class Menu {
 public:
  Menu() : editor_(rolltui_input_new()), m_(rolltui_menu_new(editor_)) {
    RolltuiInputOptions o;
    o.single_line = 1;
    o.prompt.clear();
    rolltui_input_set_options(editor_, &o);
    rolltui_menu_set_validator_fn(m_, call_validator, &validators_);
  }
  explicit Menu(MenuItem root) : Menu() { set_root(std::move(root)); }
  Menu(const Menu&) = delete;
  Menu& operator=(const Menu&) = delete;
  ~Menu() {
    rolltui_menu_free(m_);
    rolltui_input_free(editor_);
  }

  // ---- the tree ----
  void set_root(MenuItem root) { rolltui_menu_set_root(m_, &root); }
  const MenuItem& root() const { return *rolltui_menu_root(m_); }
  MenuItem* find(std::string_view id) { return rolltui_menu_find(m_, id.data(), id.size()); }
  const MenuItem* find(std::string_view id) const { return rolltui_menu_find(const_cast<RolltuiMenu*>(m_), id.data(), id.size()); }
  bool set_value(std::string_view id, std::string value) {
    MenuItem* it = find(id);
    if (!it) return false;
    it->value = std::move(value);
    return true;
  }
  void set_validator(std::string_view name, Validator v) {
    for (auto& [n, fn] : validators_)
      if (n == name) {
        fn = std::move(v);
        return;
      }
    validators_.emplace_back(std::string(name), std::move(v));
  }
  std::vector<std::string> unknown_validators() const {
    std::vector<std::string> used, out;
    collect_validators(root(), used);
    for (const std::string& u : used) {
      bool known = false;
      for (const auto& [n, fn] : validators_) known |= n == u;
      if (!known) out.push_back(u);
    }
    return out;
  }
  std::vector<std::pair<std::string, std::string>> item_actions() const {
    std::vector<std::pair<std::string, std::string>> out;
    collect_item_actions(root(), out);
    return out;
  }
  void apply_shortcuts(const RolltuiBindings* b) { fill_shortcuts(*rolltui_menu_root(m_), b); }

  // ---- navigation state ----
  void reset() { rolltui_menu_reset(m_); }
  std::vector<std::size_t> path() const {
    const std::size_t* p = nullptr;
    const std::size_t n = rolltui_menu_path(m_, &p);
    return std::vector<std::size_t>(p, p + n);
  }
  std::size_t selected() const { return rolltui_menu_selected(m_); }
  const MenuItem* selected_item() const { return rolltui_menu_selected_item(m_); }
  std::vector<std::size_t> visible() const {
    const std::size_t* v = nullptr;
    const std::size_t n = rolltui_menu_visible(m_, &v);
    return std::vector<std::size_t>(v, v + n);
  }
  std::string_view filter() const {
    std::size_t n = 0;
    const char* p = rolltui_menu_filter(m_, &n);
    return std::string_view(p, n);
  }
  std::string breadcrumb() const {
    RolltuiStr s;
    rolltui_menu_breadcrumb(m_, &s);
    return s.str();
  }
  bool editing() const { return rolltui_menu_editing(m_) != 0; }
  std::string_view editing_text() const {
    std::size_t n = 0;
    const char* p = rolltui_input_text(editor_, &n);
    return std::string_view(p, n);
  }
  std::string_view edit_reason() const {
    std::size_t n = 0;
    const char* p = rolltui_menu_edit_reason(m_, &n);
    return std::string_view(p, n);
  }
  void set_palette(bool on) { rolltui_menu_set_palette(m_, on ? 1 : 0); }
  bool palette() const { return rolltui_menu_palette(m_) != 0; }
  std::size_t flat_count() const { return rolltui_menu_flat_count(m_); }
  std::string_view flat_label(std::size_t i) const {
    std::size_t n = 0;
    const char* p = rolltui_menu_flat_label(m_, i, &n);
    return std::string_view(p, n);
  }

  // ---- events ----
  MenuEvent handle(const RolltuiEvent& e, const RolltuiBindings* bindings) {
    RolltuiMenuEvent out{};
    rolltui_menu_handle(m_, &e, bindings, &menu_actions(), &out);
    MenuEvent r;
    r.kind = static_cast<MenuEvent::Kind>(out.kind);
    r.id = out.id.str();
    r.value = out.value.str();
    r.checked = out.checked != 0;
    rolltui_menu_event_release(&out);
    return r;
  }
  MenuEvent handle(const RolltuiEvent& e) { return handle(e, cached_default_bindings()); }
  MenuEvent handle(const RolltuiMouseEvent& m) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse = m;
    return handle(e);
  }
  MenuEvent handle(const PasteEvent& p) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_PASTE;
    e.text = p.text.data();
    e.text_len = p.text.size();
    return handle(e);
  }

  // ---- layout + drawing ----
  void layout(Rect area) { rolltui_menu_layout(m_, area); }
  void draw(Frame& f, const Theme& theme, bool focused) const {
    rolltui_menu_draw(m_, f.handle(), draw_scratch(), theme.styles.data(), &kMenuRoles, &kInputRoles, focused ? 1 : 0);
  }

 private:
  RolltuiInput* editor_;  // BORROWED by m_; must outlive it — freed AFTER m_ below
  RolltuiMenu* m_;
  std::vector<std::pair<std::string, Validator>> validators_;
};

RolltuiEvent key(Key k) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = static_cast<unsigned char>(k);
  return e;
}
RolltuiEvent ch(char c) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = static_cast<unsigned char>(Key::Char);
  e.key.ch = static_cast<char32_t>(c);
  return e;
}

MenuItem sample() {
  return MenuItem::submenu(
      "root", "settings",
      {MenuItem::choice("theme", "Theme",
                        {MenuItem::action("default", "default"), MenuItem::action("mono", "mono"), MenuItem::action("light", "light")},
                        "default"),
       MenuItem::submenu("layout", "Layout",
                         {MenuItem::action("layout.default", "default"), MenuItem::action("layout.stacked", "stacked", "F2")}),
       MenuItem::toggle("wrap", "Wrap long lines", false),
       MenuItem::input("save", "Save as"),
       MenuItem::action("quit", "Quit", "Ctrl-Q"),
       MenuItem::action("disabled", "Nothing here")});
}

std::string row(const Frame& f, int y) {
  std::string s;
  for (int x = 0; x < f.width(); ++x)
    if (!f.at(x, y).continuation) s += f.glyph(x, y);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

}  // namespace

int main() {
  const Theme& theme = *builtin_theme("default-dark");

  // ---- navigation ----
  {
    Menu m(sample());
    m.find("disabled")->enabled = false;
    check(m.breadcrumb() == "settings" && m.selected() == 0 && m.visible().size() == 6, "opens at the top level, first item selected, six items");
    m.handle(key(Key::Up));
    check(m.selected() == 0, "Up at the first item stays (clamped, no wrap)");
    for (int i = 0; i < 10; ++i) m.handle(key(Key::Down));
    check(m.selected() == 5, "Down clamps at the last item");
    m.handle(key(Key::Home));
    check(m.selected() == 0, "Home selects the first");
    m.handle(key(Key::End));
    check(m.selected() == 5, "End selects the last");
    MenuEvent ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::None, "Enter on a disabled item emits nothing");
    m.handle(key(Key::Up));  // quit
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Activate && ev.id == "quit", "Enter on an action emits Activate with its id");
    m.handle(key(Key::Home));
    m.handle(key(Key::Down));  // layout
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::None && m.path() == std::vector<std::size_t>{1} && m.breadcrumb() == "settings \xE2\x80\xBA Layout",
          "Enter on a submenu descends; the breadcrumb shows the path [" + m.breadcrumb() + "]");
    m.handle(key(Key::Down));
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Activate && ev.id == "layout.stacked", "an action two levels deep activates by id");
    m.handle(key(Key::Left));
    check(m.path().empty() && m.selected() == 1, "Left ascends and re-selects the submenu it came from");
    m.handle(key(Key::Left));
    check(m.path().empty(), "Left at the top level does nothing");
    ev = m.handle(key(Key::Escape));
    check(ev.kind == MenuEvent::Kind::Closed, "Escape at the top level with no filter emits Closed");
    m.handle(key(Key::Down));
    m.handle(key(Key::Right));
    check(m.path() == std::vector<std::size_t>{2} || m.path().empty(), "Right on a toggle does not descend");
    m.handle(key(Key::Up));
    m.handle(key(Key::Right));
    check(m.path() == std::vector<std::size_t>{1}, "Right on a submenu descends");
    ev = m.handle(key(Key::Escape));
    check(ev.kind == MenuEvent::Kind::None && m.path().empty(), "Escape one level down ascends instead of closing");
  }
  // ---- toggle, choice, input ----
  {
    Menu m(sample());
    m.handle(key(Key::Down));
    m.handle(key(Key::Down));  // wrap
    MenuEvent ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Toggle && ev.id == "wrap" && ev.checked && m.find("wrap")->checked, "Enter on a toggle flips it and reports the new state");
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Toggle && !ev.checked, "…and back");
    m.handle(key(Key::Home));
    ev = m.handle(key(Key::Enter));  // theme (choice)
    check(ev.kind == MenuEvent::Kind::None && m.path() == std::vector<std::size_t>{0} && m.selected() == 0,
          "Enter on a choice descends into its options, the current one selected");
    m.handle(key(Key::Down));
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Choose && ev.id == "theme" && ev.value == "mono", "Enter on an option emits Choose{choice, option}");
    check(m.path().empty() && m.find("theme")->value == "mono" && m.selected() == 0, "…sets the choice's value and ascends to the choice");
    m.handle(key(Key::Enter));
    check(m.selected() == 1, "re-opening the choice selects its current option (mono)");
    m.handle(key(Key::Escape));
    for (int i = 0; i < 3; ++i) m.handle(key(Key::Down));  // save (input)
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::None && m.editing(), "Enter on an input starts editing");
    for (char c : std::string("mine")) m.handle(ch(c));
    m.handle(key(Key::Backspace));
    m.handle(ch('e'));
    check(m.editing_text() == "mine" && m.find("save")->value.empty() && m.selected() == 3, "typing while editing edits the EDITING text, not the filter and not the committed value");
    m.handle(key(Key::Down));
    check(m.selected() == 3, "Down while editing does not move");
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Input && ev.id == "save" && ev.value == "mine" && !m.editing(), "Enter submits the text and ends the edit");
    m.handle(key(Key::Enter));
    m.handle(ch('x'));
    m.handle(key(Key::Escape));
    check(m.find("save")->value == "mine" && !m.editing(), "Escape cancels the edit and restores the value");
    m.handle(key(Key::Enter));
    RolltuiEvent ctrl_u = ch('u');
    ctrl_u.key.ctrl = true;
    m.handle(ctrl_u);
    check(m.editing() && m.editing_text().empty() && m.find("save")->value == "mine", "Ctrl-U (input.kill_to_line_start) while editing clears the text; the value waits for a commit");
    m.handle(key(Key::Escape));
    check(m.find("save")->value == "mine", "…and Escape still restores it");
  }
  // ---- the filter ----
  {
    Menu m(sample());
    m.handle(ch('L'));
    m.handle(ch('a'));
    check(m.filter() == "La" && m.visible() == std::vector<std::size_t>{1} && m.selected() == 0,
          "typing filters the level case-insensitively (\"La\" → Layout) and selects the first match");
    m.handle(ch('z'));
    check(m.visible().empty(), "a filter with no match shows nothing");
    m.handle(key(Key::Backspace));
    check(m.visible().size() == 1, "Backspace erases the last filter character");
    MenuEvent ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::None && m.path() == std::vector<std::size_t>{1} && m.filter().empty(),
          "Enter acts on the filtered selection and the filter is dropped on descend");
    m.handle(ch('s'));
    m.handle(key(Key::Escape));
    check(m.filter().empty() && m.path() == std::vector<std::size_t>{1}, "Escape with a filter clears the filter first, without ascending");
    m.handle(ch('s'));
    m.handle(key(Key::Left));
    check(m.filter().empty() && m.path() == std::vector<std::size_t>{1}, "Left with a filter clears the filter first too");
    m.handle(ch('s'));
    m.handle(ch('t'));
    Frame f(40, 6);
    m.layout({0, 0, 40, 6});
    m.draw(f, theme, true);
    check(row(f, 0) == "settings \xE2\x80\xBA Layout  /st", "the breadcrumb row shows the filter [" + row(f, 0) + "]");
    check(row(f, 1) == "stacked                               F2", "the one match is drawn with its shortcut right-aligned [" + row(f, 1) + "]");
  }
  // ---- drawing ----
  {
    Menu m(sample());
    m.find("disabled")->enabled = false;
    Frame f(30, 8);
    m.layout({0, 0, 30, 8});
    m.draw(f, theme, true);
    check(row(f, 0) == "settings", "row 0 is the breadcrumb");
    check(row(f, 1) == "Theme                default \xE2\x96\xB8", "a choice shows its value and the arrow [" + row(f, 1) + "]");
    check(row(f, 2) == "Layout                       \xE2\x96\xB8", "a submenu ends in the arrow [" + row(f, 2) + "]");
    check(row(f, 3) == "[ ] Wrap long lines", "a toggle shows its box [" + row(f, 3) + "]");
    check(row(f, 4) == "Save as:", "an input shows label: value [" + row(f, 4) + "]");
    check(row(f, 5) == "Quit                    Ctrl-Q", "a shortcut is right-aligned [" + row(f, 5) + "]");
    check(f.at(0, 1).style == theme.style(Role::menu_selected) && f.at(0, 2).style == theme.style(Role::menu_item) &&
              f.at(0, 6).style == theme.style(Role::text_muted),
          "the selected row is menu_selected, others menu_item, a disabled one text_muted");
    // Scrolling: three item rows for six items.
    Frame g(30, 4);
    m.layout({0, 0, 30, 4});
    m.handle(key(Key::End));
    m.draw(g, theme, true);
    check(row(g, 3).rfind("Nothing here", 0) == 0 && row(g, 1).rfind("Save as", 0) == 0,
          "with three item rows and the last selected, the view scrolls to show it [" + row(g, 1) + " | " + row(g, 3) + "]");
    check(g.glyph(29, 1) == "\xE2\x96\xB2", "a ▲ marker says items are hidden above");
    m.handle(key(Key::PageUp));
    check(m.selected() == 2, "PageUp moves by the item rows (5 → 2)");
    m.handle(key(Key::PageUp));
    check(m.selected() == 0, "…and clamps at 0");
  }
  // ---- mouse ----
  {
    Menu m(sample());
    m.layout({2, 1, 30, 8});
    MouseEvent p;
    p.kind = MouseEvent::Kind::Press;
    p.button = 1;
    p.x = 5;
    p.y = 6;  // row 4 of the items: Quit (breadcrumb at y=1, items from y=2)
    MenuEvent ev = m.handle(p);
    check(ev.kind == MenuEvent::Kind::Activate && ev.id == "quit", "a click on an item row selects and activates it");
    p.y = 1;
    ev = m.handle(p);
    check(ev.kind == MenuEvent::Kind::None, "a click on the breadcrumb row does nothing");
    MouseEvent w;
    w.kind = MouseEvent::Kind::WheelDown;
    m.handle(key(Key::Home));
    m.handle(w);
    check(m.selected() == 1, "the wheel moves the selection");
  }
  // ---- palette mode ----
  {
    Menu m(sample());
    m.set_palette(true);
    const std::vector<std::size_t> vis = m.visible();
    check(m.flat_count() == 9 && vis.size() == 9, "the palette flattens every actionable leaf (3 theme options, 2 layouts, toggle, input, quit, disabled) = 9 [" +
                                                    std::to_string(m.flat_count()) + "]");
    check(m.flat_label(1) == "Theme \xE2\x80\xBA mono" && m.flat_label(4) == "Layout \xE2\x80\xBA stacked", "rows are labelled with their path");
    for (char c : std::string("mono")) m.handle(ch(c));
    check(m.visible().size() == 1, "the filter matches the whole path");
    MenuEvent ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Choose && ev.id == "theme" && ev.value == "mono" && m.find("theme")->value == "mono",
          "Enter on a palette row acts on the leaf as navigation would (a choice option chooses)");
    m.set_palette(false);
    check(!m.palette() && m.path().empty() && m.filter().empty(), "leaving palette mode returns to the top level");
  }
  // ---- JSON ----
  {
    const MenuItem root = sample();
    const std::string text = menu_to_json(root);
    MenuLoadReport rep;
    std::optional<MenuItem> back = menu_from_json(text, rep);
    check(back && rep.clean() && *back == root, "menu_to_json / menu_from_json round-trips exactly" + (rep.clean() ? "" : " — " + (rep.bad_values.empty() ? rep.unknown_keys[0] : rep.bad_values[0])));
    std::optional<MenuItem> dup = menu_from_json(R"({"id":"r","items":[{"id":"a"},{"id":"a","colour":1}]})", rep);
    check(dup && rep.bad_values.size() == 1 && rep.bad_values[0].find("duplicate id 'a'") != std::string::npos &&
              rep.unknown_keys.size() == 1 && rep.unknown_keys[0] == "items[1].colour",
          "a duplicate id is a bad value and an unknown key is reported with its path");
    std::optional<MenuItem> shared = menu_from_json(R"({"id":"r","items":[{"id":"a","kind":"choice","items":[{"id":"auto"},{"id":"x"}]},{"id":"b","kind":"choice","items":[{"id":"auto"},{"id":"auto"}]}]})", rep);
    check(shared && rep.bad_values.size() == 1 && rep.bad_values[0].find("items[1].items[1].id: duplicate id 'auto'") != std::string::npos,
          "two choices may both offer 'auto' (options are values, unique per choice); the same option twice in one choice is the duplicate [" + (rep.bad_values.empty() ? "" : rep.bad_values[0]) + "]");
    std::optional<MenuItem> bad = menu_from_json("[1,2]", rep);
    check(!bad && !rep.error.empty(), "a non-object file is unusable");
    std::optional<MenuItem> kinds = menu_from_json(R"({"id":"r","items":[{"id":"t","kind":"toggle","checked":true},{"id":"c","kind":"choice","value":"x","items":[{"id":"x"}]},{"id":"q","kind":"quux"}]})", rep);
    check(kinds && kinds->children[0].kind == MenuItem::Kind::Toggle && kinds->children[0].checked && kinds->children[1].kind == MenuItem::Kind::Choice &&
              kinds->children[1].value == "x" && rep.bad_values.size() == 1 && rep.bad_values[0].find("items[2].kind") == 0,
          "kinds load; an unknown kind is a bad value naming its path [" + (rep.bad_values.empty() ? "" : rep.bad_values[0]) + "]");
  }
  // ---- the shipped menu files (Phase 10 m3) ----
  // The same standard the built-in layouts are held to: what SHIPS must load clean, and
  // it is embedded from a real file, so nothing here is checking a string in a .cpp.
  {
    const std::vector<std::string_view> names = shipped_menu_names();
    std::ifstream in(std::string(ROLLTUI_MENUS_DIR) + "/main.json", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    check(!names.empty() && !ss.str().empty() && shipped_menu("main") == ss.str(),
          "the shipped menus ARE the files in rolltui/presets/menus, embedded byte for byte (" + std::to_string(names.size()) + ")");
    for (std::string_view n : names) {
      MenuLoadReport rep;
      std::optional<MenuItem> m = menu_from_json(shipped_menu(n), rep);
      check(m && rep.clean(), "shipped menu '" + std::string(n) + "' loads clean" +
                                  (rep.clean() ? "" : ": " + (!rep.error.empty() ? rep.error : rep.bad_values.empty() ? rep.unknown_keys[0] : rep.bad_values[0])));
    }
    MenuLoadReport rep;
    std::optional<MenuItem> main = menu_from_json(shipped_menu("main"), rep);
    Menu m(main.value_or(MenuItem::submenu("root", "root", {})));
    check(main && m.find("theme") && m.find("layout") && m.find("depth"),
          "menus/main.json is the settings menu over the three preset domains");
    check(shipped_menu("no-such-menu").empty(), "an unshipped name is empty, never a wrong menu");
  }
  // ---- m4: an item may NAME a bindings action --------------------------------------
  {
    MenuLoadReport rep;
    std::optional<MenuItem> root = menu_from_json(
        R"({"id":"r","items":[{"id":"a","label":"A","action":"app.help"},{"id":"b","label":"B","action":"app.menu","shortcut":"F9"},
            {"id":"c","label":"C","shortcut":"F5"}]})", rep);
    auto id_at = [&](std::size_t i) { return root && i < root->children.size() ? root->children[i].action_name : std::string("(missing)"); };
    check(root && id_at(0) == "app.help" && id_at(2).empty(), "\"action\" is read; an item without one has none");
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find(".shortcut: an item with an \"action\" takes its shortcut from the bindings") != std::string::npos &&
              root && root->children[1].shortcut.empty(),
          "…and spelling a shortcut out beside it is a bad value, dropped [" + (rep.bad_values.empty() ? "" : rep.bad_values[0]) + "]");

    Menu m(*root);
    check(m.item_actions() == (std::vector<std::pair<std::string, std::string>>{{"a", "app.help"}, {"b", "app.menu"}}),
          "item_actions lists every item that names one, by item id");
    RolltuiBindings* b_ah = default_bindings();
    m.apply_shortcuts(b_ah);
    rolltui_bindings_free(b_ah);
    auto sc = [&](const char* id) { const MenuItem* it = m.find(id); return it ? it->shortcut : std::string("(missing)"); };
    check(sc("a") == "F1, ?" && sc("b") == "F2" && sc("c") == "F5",
          "apply_shortcuts fills them from the LIVE chords and leaves a plain shortcut alone [" + sc("a") + "]");
    RolltuiBindings* rebound = default_bindings();
    rolltui_bindings_clear(rebound, "app.help", 8);
    const RolltuiChord f8 = *parse_chord("f8");
    rolltui_bindings_bind(rebound, "app.help", 8, &f8, nullptr, nullptr);
    m.apply_shortcuts(rebound);
    rolltui_bindings_free(rebound);
    check(sc("a") == "F8", "…and it is idempotent, so a rebinding shows immediately [" + sc("a") + "]");

    // A derived shortcut is never written back: a round trip must not bake one moment's
    // keys into the file (the whole reason the item names the action instead).
    MenuLoadReport rr;
    std::optional<MenuItem> back = menu_from_json(menu_to_json(m.root()), rr);
    check(back && rr.clean() && back->children[0].action_name == "app.help" && back->children[0].shortcut.empty() &&
              back->children[2].shortcut == "F5",
          "menu_to_json writes the action, never the chords it happened to have");
  }
  // ---- typed inputs (milestone 18): prefix validity at the keystroke, validity at the commit ----
  {
    auto spec = [](InputType t, double min = -1e15, double max = 1e15) {
      InputSpec s;
      s.type = t;
      s.min = min;
      s.max = max;
      return s;
    };
    InputSpec flt = spec(InputType::Float, 0, 1);
    flt.precision = 2;
    flt.step = 0.1;
    InputSpec txt = spec(InputType::Text);
    txt.max_len = 3;
    txt.min_len = 2;
    txt.validator = "even";
    InputSpec opt = spec(InputType::Int, 0, 9);
    opt.optional = true;
    InputSpec otxt = spec(InputType::Text);
    otxt.optional = true;
    MenuItem typed = MenuItem::submenu(
        "root", "typed",
        {MenuItem::input("pct", "Percent", spec(InputType::Int, 0, 100), "50"),        // 0
         MenuItem::input("delta", "Delta", spec(InputType::Int, -10, 10), "0"),        // 1
         MenuItem::input("digit", "Digit", spec(InputType::Int, 5, 9), "7"),           // 2
         MenuItem::input("teen", "Teen", spec(InputType::Int, 10, 19), "15"),          // 3
         MenuItem::input("chaos", "Chaos", flt, "0.5"),                                // 4
         MenuItem::input("col", "Colour", spec(InputType::Color), "#112233"),          // 5
         MenuItem::input("sz", "Size", spec(InputType::Size), "fill"),                 // 6
         MenuItem::input("dm", "Dim", spec(InputType::Dim), "1"),                      // 7
         MenuItem::input("nm", "Name", spec(InputType::Name), "abc"),                  // 8
         MenuItem::input("txt", "Text", txt, "ok"),                                    // 9
         MenuItem::input("opt", "Optional", opt, ""),                                  // 10
         MenuItem::input("otxt", "Optional text", otxt, "hi"),                         // 11
         MenuItem::input("rtxt", "Required text", spec(InputType::Text), "hi")});       // 12
    Menu m(typed);
    auto open = [&](int index) {
      m.reset();
      for (int i = 0; i < index; ++i) m.handle(key(Key::Down));
      m.handle(key(Key::Enter));
    };
    auto type = [&](std::string_view s) { for (char c : s) m.handle(ch(c)); };
    RolltuiEvent ctrl_u = ch('u');
    ctrl_u.key.ctrl = true;
    // Each row: the item, what is typed into the CLEARED field (Ctrl-U first, so a row
    // whose every key is refused ends empty rather than at the selected-all value), the
    // text that results (refused keys leave no trace), then Enter: whether it commits
    // and to what.
    struct Row { int item; const char* typed; const char* text; bool commits; const char* canonical; const char* why; };
    const Row rows[] = {
        {0, "100", "100", true, "100", "int: the top of the range"},
        {0, "1000", "100", true, "100", "int: a fourth digit is refused (nothing 1000.. fits 0..100)"},
        {0, "-5", "5", true, "5", "int: '-' is refused when min >= 0"},
        {0, "007", "007", true, "7", "int: leading zeros are allowed and normalised on commit"},
        {0, "", "", false, "", "int: empty is a valid prefix but not a value ('a value is needed')"},
        {1, "-10", "-10", true, "-10", "int: '-' is a key when min < 0"},
        {1, "-11", "-1", true, "-1", "int: -11 is out of -10..10, so the second 1 is refused"},
        {1, "-", "-", false, "", "int: a lone '-' is a prefix, not a value"},
        {2, "1", "", false, "", "int 5..9: '1' is refused outright (no continuation fits)"},
        {2, "7", "7", true, "7", "int 5..9: 7"},
        {4, "1.5", "1.", true, "1.00", "float 0..1: '1.' is fine, the 5 is refused"},
        {4, "0.123", "0.12", true, "0.12", "float: a third fraction digit is refused (precision 2)"},
        {4, ".5", ".5", true, "0.50", "float: '.5' commits as 0.50"},
        {4, "2", "", false, "", "float 0..1: 2 is refused"},
        {5, "#1234567", "#123456", true, "#123456", "colour: seven hex digits are refused"},
        {5, "300", "30", true, "30", "colour: an index past 255 is refused at the third digit"},
        {5, "no", "no", false, "", "colour: 'no' is a prefix of none, not a colour yet"},
        {5, "none", "none", true, "none", "colour: none"},
        {5, "orange", "n", false, "", "colour: o r a refused, n accepted (a prefix of none), g e refused ('ng', 'ne' begin no colour)"},
        {6, "fill 2", "fill 2", true, "fill 2", "size: fill N"},
        {6, "fil", "fil", false, "", "size: 'fil' is a prefix, not a size"},
        {6, "50% + 1", "50% + 1", true, "50% + 1", "size: N% ± cells"},
        {6, "5x", "5", true, "5", "size: 'x' is refused"},
        {7, "10%", "10%", true, "10%", "dim: N%"},
        {7, "fill", "", false, "", "dim: fill is not a placement dim"},
        {8, ".a", "a", true, "a", "name: no leading dot"},
        {8, "a b", "ab", true, "ab", "name: no spaces"},
        {8, "my-theme_1.v2", "my-theme_1.v2", true, "my-theme_1.v2", "name: letters, digits, - _ ."},
        {9, "abcd", "abc", false, "", "text: max_len 3 refuses the fourth; commit refused — no validator 'even' registered"},
        {9, "a", "a", false, "", "text: min_len 2 refuses the commit"},
        {10, "", "", true, "", "optional int: empty commits as empty"},
        // Text honours `optional` like every other type. It did not until Phase 10 m5,
        // where a `file:` source is a Text field that must not commit empty; min_len is
        // a LENGTH rule and was never the emptiness rule.
        {12, "", "", false, "", "text: empty is refused when the spec is not optional and there is no min_len"},
        {11, "", "", true, "", "optional text: empty commits as empty"},
    };
    for (const Row& r : rows) {
      open(r.item);
      m.handle(ctrl_u);
      type(r.typed);
      const std::string text(m.editing_text());
      const MenuEvent ev = m.handle(key(Key::Enter));
      const bool committed = ev.kind == MenuEvent::Kind::Input;
      check(text == r.text && committed == r.commits && (!r.commits || ev.value == r.canonical) && m.editing() == !r.commits,
            std::string(r.why) + " [text '" + text + "', " + (committed ? "committed '" + ev.value + "'" : "refused: " + m.edit_reason()) + "]");
      if (r.commits) check(m.selected_item()->value == r.canonical, "…the item's value is the canonical text");
    }
    // A registered validator: consulted at the commit only, never at a key.
    m.set_validator("even", [](std::string_view s) -> std::optional<std::string> {
      if (s.size() % 2 == 0) return std::nullopt;
      return "an even number of characters";
    });
    check(m.unknown_validators().empty(), "the tree's validators are all registered now");
    open(9);
    type("abc");
    MenuEvent ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::None && m.editing() && m.edit_reason() == "an even number of characters", "the validator's reason refuses the commit [" + m.edit_reason() + "]");
    m.handle(key(Key::Backspace));
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Input && ev.value == "ab", "…and a text it accepts commits");
    // Deletions are never refused, even off every valid path; the commit says so.
    open(3);
    m.handle(key(Key::Home));
    m.handle(key(Key::Delete));
    check(m.editing_text() == "5" && !m.edit_reason().empty(), "deleting the 1 of 15 (10..19) is allowed; the reason shows [" + m.edit_reason() + "]");
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::None && m.editing() && m.selected_item()->value == "15", "…and the commit is refused, the value kept");
    // Select-all on open: typing replaces; an arrow places the caret.
    m.set_value("nm", "abc");
    open(8);
    m.handle(key(Key::End));
    type("d");
    m.handle(key(Key::Home));
    type("z");
    check(m.editing_text() == "zabcd", "End / Home place the caret in the selected-all text; typing inserts there [" + m.editing_text() + "]");
    m.handle(key(Key::Escape));
    check(!m.editing() && m.selected_item()->value == "abc", "Escape cancels: the value is untouched");
    // Steppers: from a valid text, ± step, clamped; from an empty text, the committed value.
    open(4);
    m.handle(key(Key::Up));
    check(m.editing_text() == "0.60", "Up on 0.5 (step 0.1, precision 2) is 0.60 [" + m.editing_text() + "]");
    for (int i = 0; i < 6; ++i) m.handle(key(Key::Up));
    check(m.editing_text() == "1.00", "…clamped at max [" + m.editing_text() + "]");
    m.handle(key(Key::Down));
    check(m.editing_text() == "0.90", "Down steps back [" + m.editing_text() + "]");
    m.set_value("pct", "50");
    open(0);
    m.handle(ctrl_u);
    m.handle(key(Key::Up));
    check(m.editing_text() == "50", "Up from an empty text lands on the committed value first [" + m.editing_text() + "]");
    m.handle(key(Key::Up));
    check(m.editing_text() == "51", "…then steps");
    m.handle(key(Key::Escape));
    // Paste is prefix-checked as a whole.
    open(0);
    PasteEvent good, bad;
    good.text = "12";
    bad.text = "abc";
    m.handle(good);
    check(m.editing_text() == "12", "a pasted '12' replaces the selection [" + m.editing_text() + "]");
    m.handle(bad);
    check(m.editing_text() == "12" && !m.edit_reason().empty(), "a pasted 'abc' is refused whole, with the reason");
    m.handle(key(Key::Escape));
    // Hints, and the spec round-tripping through the file format.
    check(input_hint(spec(InputType::Int, 0, 100)) == "0..100" && input_hint(flt) == "0.00..1.00 (2 digits)", "hints name the constraint [" + input_hint(flt) + "]");
    MenuLoadReport rep;
    std::optional<MenuItem> back = menu_from_json(menu_to_json(typed), rep);
    check(back && rep.clean() && *back == typed, "typed inputs round-trip through JSON with their specs [" + (rep.bad_values.empty() ? "" : rep.bad_values[0]) + "]");
    menu_from_json(R"({"id":"r","items":[{"id":"n","kind":"input","type":"int","validator":"even"}]})", rep);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("validator") != std::string::npos, "a validator on a typed (non-text) input is a bad value");
    menu_from_json(R"({"id":"r","items":[{"id":"n","kind":"action","type":"int"}]})", rep);
    check(rep.unknown_keys.size() == 1, "a spec key on a non-input is an unknown key");
    // Drawing while editing: the field row shows the label and the editing text, and the
    // breadcrumb carries the hint and the reason.
    {
      Frame f(48, 4);
      f.clear(theme.style(Role::background));
      open(0);
      type("7x");
      m.layout({0, 0, 48, 4});
      m.draw(f, theme, true);
      check(row(f, 0).find("0..100") != std::string::npos && row(f, 0).find("\xE2\x9C\x97") != std::string::npos, "the breadcrumb shows the hint and the refusal [" + row(f, 0) + "]");
      check(row(f, 1).rfind("Percent: 7", 0) == 0, "the field row is the label and the editing text [" + row(f, 1) + "]");
      m.handle(key(Key::Escape));
    }
  }
  // ---- degenerate sizes ----
  {
    Menu m(sample());
    const Rect areas[] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 0, 5}, {0, 0, 5, 0}, {0, 0, 1, 6}, {0, 0, 40, 1}, {3, 3, 2, 2}};
    bool ok = true;
    for (const Rect& a : areas) {
      Frame f(10, 10);
      const Style fill = theme.style(Role::background);
      f.clear(fill);
      m.layout(a);
      m.handle(key(Key::Down));
      m.handle(ch('t'));
      m.handle(key(Key::Backspace));
      m.handle(key(Key::Enter));
      m.handle(key(Key::Escape));
      m.draw(f, theme, true);
      for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
          if (!a.contains(x, y) && !(f.glyph(x, y) == " " && f.at(x, y).style == fill)) ok = false;
      m.reset();
    }
    check(ok, "0x0, 1x1, 0x5, 5x0, 1x6, 40x1 and an offset 2x2 area: nothing drawn outside, no crash");
    Frame one(20, 1);
    m.reset();
    m.handle(key(Key::Down));
    m.layout({0, 0, 20, 1});
    m.draw(one, theme, true);
    check(row(one, 0).rfind("Layout", 0) == 0, "a one-row area shows the selected item, not the breadcrumb [" + row(one, 0) + "]");
  }
  return report("rolltui menu_test");
}
