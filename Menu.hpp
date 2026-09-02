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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Input.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

enum class InputType : std::uint8_t { Text, Int, Float, Color, Size, Dim, Name };
std::string_view input_type_name(InputType t);
std::optional<InputType> input_type_from_name(std::string_view name);

struct InputSpec {
  InputType type = InputType::Text;
  double min = -1e15, max = 1e15;  // Int / Float, inclusive
  double step = 1;                 // Up / Down while editing a number
  int precision = -1;              // Float: most digits after the point; -1 = any
  std::size_t max_len = 0;         // Text: 0 = no cap; Name: the type's own 64
  std::size_t min_len = 0;         // Text: checked at commit
  bool optional = false;           // empty commits as "" instead of being refused
  std::string validator;           // Text: a host-registered check by name, at commit
  std::string hint;                // shown beside the field; input_hint(spec) when empty
  bool operator==(const InputSpec&) const = default;
};

struct InputCheck {
  bool prefix_ok = false;   // the text could still become a valid value
  bool valid = false;       // the text IS a valid value now
  std::string reason;       // why not (prefix or valid), "" when valid
  std::string canonical;    // the value as it would be committed (when valid)
};
// Pure: the type's own rules (a Text validator is the host's; see Menu::set_validator).
InputCheck check_input(const InputSpec& spec, std::string_view text);
std::string input_hint(const InputSpec& spec);  // "1..100", "0.0..1.0 (2 digits)", "#rrggbb | 0-255 | none", ...

struct MenuItem {
  enum class Kind : std::uint8_t { Action, Submenu, Toggle, Choice, Input };
  Kind kind = Kind::Action;
  std::string id;                  // the action id the host binds; an option's value id
  std::string label;
  std::string shortcut;            // display only ("F1"); the host decides what keys do
  bool enabled = true;
  bool checked = false;            // Toggle
  std::string value;               // Choice: the current option id; Input: the COMMITTED text
  InputSpec spec;                  // Input: the type and its constraints
  std::vector<MenuItem> children;  // Submenu: items; Choice: options
  bool operator==(const MenuItem&) const = default;

  static MenuItem action(std::string id, std::string label, std::string shortcut = {});
  static MenuItem submenu(std::string id, std::string label, std::vector<MenuItem> children);
  static MenuItem toggle(std::string id, std::string label, bool checked);
  static MenuItem choice(std::string id, std::string label, std::vector<MenuItem> options, std::string value);
  static MenuItem input(std::string id, std::string label, std::string value = {});
  static MenuItem input(std::string id, std::string label, InputSpec spec, std::string value = {});
};

struct MenuEvent {
  enum class Kind : std::uint8_t { None, Activate, Toggle, Choose, Input, Closed };
  Kind kind = Kind::None;
  std::string id;       // the acted-on item (Choose: the Choice item)
  std::string value;    // Choose: the option id; Input: the committed (canonical) text
  bool checked = false; // Toggle: the new state
  bool operator==(const MenuEvent&) const = default;
};

struct MenuOptions {
  bool ambiguous_wide = false;
  int inset = 0;  // columns kept clear on each side of the area
  bool operator==(const MenuOptions&) const = default;
};

struct MenuLoadReport {
  std::string error;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// File format (menus/<name>.json): an item is {"id", "label", "kind": action |
// submenu | toggle | choice | input, "shortcut", "enabled", "checked", "value",
// "items": [...]} — "kind" defaults to submenu when "items" is present, else action.
// An input also takes "type" (text | int | float | color | size | dim | name), "min",
// "max", "step", "precision", "max_len", "min_len", "optional", "validator", "hint".
// The root is one submenu item whose label heads the breadcrumb. Unknown keys are
// reported, not ignored; a duplicate id is a bad value (the layout loader's standard).
std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report);
std::string menu_to_json(const MenuItem& root);

class Menu {
 public:
  // A Text field's host validator: the reason the text is refused, or nullopt when fine.
  using Validator = std::function<std::optional<std::string>(std::string_view)>;

  Menu();
  explicit Menu(MenuItem root);

  // ---- the tree ----
  void set_root(MenuItem root);  // also resets navigation
  const MenuItem& root() const { return root_; }
  MenuItem* find(std::string_view id);  // depth-first, any level; nullptr when absent
  const MenuItem* find(std::string_view id) const;
  bool set_value(std::string_view id, std::string value);
  bool set_checked(std::string_view id, bool checked);
  bool set_enabled(std::string_view id, bool enabled);
  bool set_options(std::string_view id, std::vector<MenuItem> options);  // a Choice's options / a Submenu's items
  void set_validator(std::string_view name, Validator v);
  // Validator names the tree refers to that have not been registered (a host's check).
  std::vector<std::string> unknown_validators() const;

  // ---- navigation state ----
  void reset();  // the top level, no filter, the first item selected, palette off
  const std::vector<std::size_t>& path() const { return path_; }  // indices from the root down
  const MenuItem& level() const;         // the item whose children are shown
  std::size_t selected() const { return sel_; }  // index into visible()
  const MenuItem* selected_item() const;         // nullptr when nothing is visible
  // The current level's children passing the filter (indices into level().children),
  // or in palette mode indices into the flattened list (flat()).
  std::vector<std::size_t> visible() const;
  const std::string& filter() const { return filter_; }
  std::string breadcrumb() const;  // "settings › theme"
  bool editing() const { return editing_; }
  const std::string& editing_text() const { return edit_.text(); }  // what is typed (the item's value is the committed one)
  const std::string& edit_reason() const { return edit_reason_; }    // a refused key's or an invalid text's reason
  const Input& editor() const { return edit_; }
  void set_palette(bool on);
  bool palette() const { return palette_; }
  struct FlatEntry {
    std::vector<std::size_t> path;  // to the leaf (a Choice option's path ends at the option)
    std::string label;              // "settings › theme › mono"
  };
  const std::vector<FlatEntry>& flat() const { return flat_; }

  // ---- events (already routed to this window by the host) ----
  MenuEvent handle(const Event& e, const Bindings& bindings);
  MenuEvent handle(const Event& e) { return handle(e, default_bindings()); }

  // ---- layout + drawing ----
  void set_options(const MenuOptions& o) { opt_ = o; }
  const MenuOptions& options() const { return opt_; }
  void layout(Rect area);
  void draw(Frame& f, const Theme& theme, bool focused) const;
  // Rows the whole level needs: the breadcrumb plus every visible item (≥ 1), for a
  // host sizing a popup.
  int rows_for() const;
  Rect area() const { return area_; }

 private:
  MenuItem& level_mut();
  const MenuItem* item_at(std::size_t vis_index) const;  // the item behind visible()[i]
  MenuItem* item_at_mut(std::size_t vis_index);
  MenuItem* by_path(const std::vector<std::size_t>& p);
  const MenuItem* by_path(const std::vector<std::size_t>& p) const;
  void rebuild_flat();
  void clamp_selection();
  MenuEvent act(std::size_t vis_index);
  MenuEvent handle_key(const KeyEvent& k, const Bindings& b);
  MenuEvent handle_edit(const Event& e, const Bindings& b);
  MenuEvent handle_mouse(const MouseEvent& m);
  void begin_edit(MenuItem& it);
  bool try_insert(std::string_view text);  // prefix-checked insertion into the field
  void step(int direction);
  void refresh_reason();
  void descend(std::size_t child);
  bool ascend();
  int item_rows() const;  // rows available to items in the current area
  void ensure_visible();
  std::string row_text(const MenuItem& it, bool in_palette, std::size_t vis_index) const;

  MenuItem root_;
  std::vector<std::size_t> path_;
  std::size_t sel_ = 0;
  std::string filter_;
  bool editing_ = false;
  Input edit_;               // the field being edited (single-line)
  std::string edit_reason_;
  bool palette_ = false;
  std::vector<FlatEntry> flat_;
  std::vector<std::pair<std::string, Validator>> validators_;
  MenuOptions opt_;
  Rect area_;
  int top_ = 0;  // first visible item row's index into visible()
};

}  // namespace rolltui
