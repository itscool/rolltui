#pragma once
//
// rolltui/Menu.hpp — the menu widget (plan/phase-9.md, milestone 11; typed inputs in
// milestone 18): a tree of items navigated one level at a time, filtered by typing,
// with a breadcrumb line saying where you are. A PURE state machine over decoded
// events, like the input widget: the host owns the tree, routes events in, reads one
// MenuEvent out per event, and draws the frame it asks for. Nothing here knows what
// an action does.
//
// MODEL. A MenuItem is one of five kinds:
//   Action    an id the host binds — Enter emits Activate{id}
//   Submenu   holds children; Enter (or Right) descends into them
//   Toggle    a checked flag; Enter flips it and emits Toggle{id, checked}
//   Choice    holds its OPTIONS as children; Enter descends into them like a submenu,
//             and Enter on an option sets the choice's `value` to the option's id,
//             emits Choose{choice id, option id} and ascends — so "Settings › Theme ›
//             mono" is the same navigation as "Commands › Models › use" (the plan's
//             rule), and a choice's current value is drawn beside its label
//   Input     a one-line TYPED text value (InputSpec below); Enter starts editing,
//             Enter again commits and emits Input{id, canonical text}, Escape cancels
// Actions are IDS THE HOST BINDS, never code in the data, which is what lets a menu's
// structure live in a file (menu_from_json / menu_to_json below) and be shared between
// hosts that bind the ids differently.
//
// TYPED INPUTS (milestone 18) — three states, not one. The item's `value` is the
// COMMITTED value: always valid, the only thing a host reads. While editing there is
// an EDITING TEXT (editing_text()), exactly what was typed, never coerced; and the
// host derives its live PREVIEW from that text only when it parses (check_input). The
// rule that follows: a keystroke is refused only when the text could not become a
// prefix of ANY valid value (prefix validity); a commit is refused when the text is not
// yet a valid value, with the reason. Never a clamp, never a coercion, never "erasing
// everything means 0" — empty is empty. Concretely, per type:
//   int    digits; '-' only when min < 0; refused once no continuation fits the range
//          (max 100: "100" fine, a fourth digit refused; 5..9: "1" refused outright);
//          leading zeros allowed and normalised on commit
//   float  the same, plus one '.', at most `precision` digits after it
//   color  a prefix of "none", "#" + up to six hex digits, or an index 0-255
//   size   fill | fill N | N% | N% ± cells | cells (Layout.hpp's split sizes)
//   dim    N | N% | N% ± cells (Layout.hpp's placement dims)
//   name   letters, digits, - _ . ; not a leading dot; at most 64
//   text   anything up to max_len; min_len and a host VALIDATOR (by name) at commit —
//          a validator cannot know "could still become valid", so text fields never
//          refuse a key and validate on commit only
// Empty commits as "" only when the spec says `optional`; otherwise it is refused
// ("a value is needed"). Starting an edit SELECTS THE WHOLE VALUE so typing replaces it;
// a first arrow key places the caret. Up / Down on a number field step by `step`,
// clamped to the range (stepping is a request to move, not a claim about a value);
// from empty or invalid text the first step lands on the committed value, else min.
// The constraint is shown beside the field (input_hint), so a refused key is never a
// mystery; a refused key's reason and an invalid text's reason are edit_reason().
// A deletion is never refused either — the caret can leave the text off every valid
// path (10..19: "15" minus the 1) and the commit then says so.
//
// KEYS are DATA (milestone 17): handle() looks a key up in the Bindings' menu scope
// (menu.up/down/page_up/page_down/first/last/activate/descend/ascend/back/erase);
// while editing, the edit scope (edit.commit / cancel / step_up / step_down) and the
// INPUT scope for the caret (input.left/right/word_*/line_*/select_*/backspace/delete/
// kill_*/select_all) — the editing field IS a rolltui::Input, single-line. The shipped
// default is the list below, table-tested in rolltui/tests/menu_test.cpp; anything
// not bound returns a None event and changes nothing. A printable character without
// Ctrl or Alt types into the filter (or the field) and is never looked up:
//   Up / Down               move the selection by one visible item (clamped, no wrap —
//                           predictable at either end)
//   PageUp / PageDown       move by the area's item rows;  Home / End  first / last
//   Enter                   act on the selected item as above; Right descends into a
//                           Submenu or Choice and does nothing else
//   Left                    ascend one level; at the top level nothing
//   Escape                  clear the filter if there is one; else ascend; at the top
//                           level with no filter emit Closed (the host closes the popup)
//   Backspace               erase the last character of the filter
//   printable text          append to the filter for the CURRENT level (case-insensitive
//                           substring on the label); the selection returns to the first
//                           match. The filter is dropped on descend and ascend
//   while editing an Input  Enter commits, Escape cancels, Up/Down step a number, the
//                           input widget's keys move and edit the text
//   mouse                   a press on an item row selects it and acts as Enter; the
//                           wheel moves the selection
// A disabled item is drawn muted and Enter on it emits None.
//
// PALETTE MODE (set_palette): the same widget over the FLATTENED tree — every Action,
// Toggle, Input and every Choice option becomes one row labelled with its path
// ("settings › theme › mono"), the filter matches the whole path, and Enter acts on
// the leaf as if it had been reached by navigation. This is the plan's command
// palette: nice-to-have 8's "command completion" is this widget with a filter.
//
// LAYOUT. Row 0 is the breadcrumb ("settings › theme", plus " /filter" while a filter
// is typed); while editing it yields to the field's guidance instead — the refusal
// reason when there is one, else the constraint — because a popup is narrow and the
// highlighted field with the caret already says where you are; the rows below are the
// visible items, the selected one filling the row in `menu_selected`, the others in
// `menu_item` (disabled: `text_muted`), a Submenu or Choice ending in " ▸", a Choice
// showing its value before the arrow, a Toggle as "[x] label", an Input as "label:
// value", and a shortcut right-aligned in `menu_shortcut`. The items scroll to keep the
// selection in view. DEGENERATE SIZES (the standing rule): 0 rows draws nothing; 1 row
// shows the selected item alone (the breadcrumb yields to the thing you can act on); a
// width that cannot hold a label clips it, and nothing is ever drawn outside the area.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Input.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/c/rolltui_menu.h"

namespace rolltui {

// PHASE 15 m5: the widget and the typed-field rules are behind `rolltui/c/rolltui_menu.h`;
// the TREE is `c/rolltui_menu_tree.h`. `InputType`, `InputSpec`, `MenuItem` and `MenuOptions` ARE those types (one
// definition), so a host still writes `MenuItem::toggle(...)` and `it.children.push_back(...)`.
// What a caller can see that is new: text out of the widget is a BORROW with a stated window,
// and the flattened palette list is `flat_count()` + `flat_label(i)` rather than a vector
// nothing on the C side could hand back without building one per call.
using InputSpec = RolltuiInputSpec;
using MenuItem = RolltuiMenuItem;
using MenuOptions = RolltuiMenuOptions;

std::string_view input_type_name(InputType t);
std::optional<InputType> input_type_from_name(std::string_view name);

struct InputCheck {
  bool prefix_ok = false;   // the text could still become a valid value
  bool valid = false;       // the text IS a valid value now
  std::string reason;       // why not (prefix or valid), "" when valid
  std::string canonical;    // the value as it would be committed (when valid)
};
// Pure: the type's own rules (a Text validator is the host's; see Menu::set_validator).
InputCheck check_input(const InputSpec& spec, std::string_view text);
std::string input_hint(const InputSpec& spec);  // "1..100", "0.0..1.0 (2 digits)", …

struct MenuEvent {
  enum class Kind : std::uint8_t {
    None = ROLLTUI_MENU_EVENT_NONE,
    Activate = ROLLTUI_MENU_EVENT_ACTIVATE,
    Toggle = ROLLTUI_MENU_EVENT_TOGGLE,
    Choose = ROLLTUI_MENU_EVENT_CHOOSE,
    Input = ROLLTUI_MENU_EVENT_INPUT,
    Closed = ROLLTUI_MENU_EVENT_CLOSED,
  };
  Kind kind = Kind::None;
  std::string id;       // the acted-on item (Choose: the Choice item)
  std::string value;    // Choose: the option id; Input: the committed (canonical) text
  bool checked = false; // Toggle: the new state
  bool operator==(const MenuEvent&) const = default;
};

struct MenuLoadReport {
  std::string error;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// File format (menus/<name>.json): an item is {"id", "label", "kind": action |
// submenu | toggle | choice | input, "action", "shortcut", "enabled", "checked",
// "value", "items": [...]} — "kind" defaults to submenu when "items" is present, else
// action. An input also takes "type" (text | int | float | color | size | dim | name),
// "min", "max", "step", "precision", "max_len", "min_len", "optional", "validator",
// "hint". "action" and "shortcut" together is a BAD VALUE: an action's shortcut is the
// bindings' to say, and a second spelling of it in the file is the lie this key exists
// to remove. The root is one submenu item whose label heads the breadcrumb. Unknown keys
// are reported, not ignored; a duplicate id is a bad value (the layout loader's standard).
std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report);
std::string menu_to_json(const MenuItem& root);

// The menu files that SHIP with the library (rolltui/presets/menus/*.json), embedded at
// build time like the shipped theme, layout and bindings presets (Phase 10 m3). A menu
// is NOT a preset domain — it has no working copy and nothing edits it at runtime; it is
// a file a layout names (`menu:<name>`, Layout.hpp) and these are the last rung of the
// three the library looks in (rolltui/Widgets.hpp has the order).
std::string_view shipped_menu(std::string_view name);  // "" when there is no such file
std::vector<std::string_view> shipped_menu_names();

class Menu {
 public:
  // A Text field's host validator: the reason the text is refused, or nullopt when fine.
  using Validator = std::function<std::optional<std::string>(std::string_view)>;
  // OWNED, through a `unique_ptr` with a deleter that calls the C free.
  struct Handle {
    void operator()(RolltuiMenu* p) const { rolltui_menu_free(p); }
  };

  Menu();
  explicit Menu(MenuItem root);
  Menu(const Menu&) = delete;
  Menu& operator=(const Menu&) = delete;

  // ---- the tree ----
  void set_root(MenuItem root);  // also resets navigation
  const MenuItem& root() const { return *rolltui_menu_root(m_.get()); }
  MenuItem* find(std::string_view id);  // depth-first, any level; nullptr when absent
  const MenuItem* find(std::string_view id) const;
  bool set_value(std::string_view id, std::string value);
  bool set_checked(std::string_view id, bool checked);
  bool set_enabled(std::string_view id, bool enabled);
  bool set_options(std::string_view id, std::vector<MenuItem> options);  // a Choice's / a Submenu's
  void set_validator(std::string_view name, Validator v);
  // Validator names the tree refers to that have not been registered (a host's check).
  std::vector<std::string> unknown_validators() const;
  // Every item that names an `action`, as (item id, action name) — for a host checking
  // them against the declared table.
  std::vector<std::pair<std::string, std::string>> item_actions() const;
  // Rewrites every action-naming item's `shortcut` from the LIVE chords. Idempotent;
  // the menu widget calls it each frame, so a rebinding shows in the menu immediately
  // and no file can disagree with the keyboard. An action no layout declares is inert
  // (Bindings.hpp), so it shows NO shortcut however many chords the table keeps for it.
  void apply_shortcuts(const Bindings& b);

  // ---- navigation state ----
  void reset();  // the top level, no filter, the first item selected, palette off
  // A BORROW of the index path from the root down, valid until the menu next navigates.
  std::vector<std::size_t> path() const;
  const MenuItem& level() const { return *rolltui_menu_level(m_.get()); }
  std::size_t selected() const { return rolltui_menu_selected(m_.get()); }
  const MenuItem* selected_item() const { return rolltui_menu_selected_item(m_.get()); }
  // The current level's children passing the filter (indices into level().children),
  // or in palette mode indices into the flattened list.
  std::vector<std::size_t> visible() const;
  // ---- what a scrollbar may ask, and nothing it could use to MOVE the list ----
  // A menu's scroll is DERIVED from its selection, so it REPORTS and declines to be
  // driven (Widgets.hpp's two optional halves).
  std::size_t scroll_first() const { return static_cast<std::size_t>(rolltui_menu_scroll_first(m_.get())); }
  std::size_t scroll_visible() const { return static_cast<std::size_t>(rolltui_menu_scroll_visible(m_.get())); }
  std::size_t scroll_total() const;
  std::string_view filter() const;
  std::string breadcrumb() const;  // "settings › theme"
  bool editing() const { return rolltui_menu_editing(m_.get()) != 0; }
  // A BORROW of the editor's buffer (Phase 15 m5), valid until the text next changes.
  std::string_view editing_text() const { return edit_.text(); }
  std::string_view edit_reason() const;  // a refused key's or an invalid text's reason
  const Input& editor() const { return edit_; }
  void set_palette(bool on) { rolltui_menu_set_palette(m_.get(), on); }
  bool palette() const { return rolltui_menu_palette(m_.get()) != 0; }
  // The flattened palette list, counted and indexed rather than handed over whole.
  std::size_t flat_count() const { return rolltui_menu_flat_count(m_.get()); }
  std::string_view flat_label(std::size_t i) const;

  // ---- events (already routed to this window by the host) ----
  MenuEvent handle(const Event& e, const Bindings& bindings);
  MenuEvent handle(const Event& e) { return handle(e, default_bindings()); }

  // ---- layout + drawing ----
  void set_options(const MenuOptions& o) { rolltui_menu_set_options_struct(m_.get(), &o); }
  const MenuOptions& options() const { return *rolltui_menu_options(m_.get()); }
  void layout(Rect area) { rolltui_menu_layout(m_.get(), area); }
  void draw(Frame& f, const Theme& theme, bool focused) const;
  // Rows the whole level needs: the breadcrumb plus every visible item (≥ 1), for a
  // host sizing a popup.
  int rows_for() const { return rolltui_menu_rows_for(m_.get()); }
  Rect area() const;

 private:
  // The editor is OWNED here and BORROWED by the C widget — one owner, and `editor()`
  // still hands back the object a host already reads.
  Input edit_;
  std::unique_ptr<RolltuiMenu, Handle> m_{rolltui_menu_new(edit_.handle())};
  std::vector<std::pair<std::string, Validator>> validators_;
};

}  // namespace rolltui
