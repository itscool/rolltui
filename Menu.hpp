#pragma once
//
// rolltui/Menu.hpp — the menu widget (plan/phase-9.md, milestone 11): a tree of items
// navigated one level at a time, filtered by typing, with a breadcrumb line saying
// where you are. A PURE state machine over decoded events, like the input widget: the
// host owns the tree, routes events in, reads one MenuEvent out per event, and draws
// the frame it asks for. Nothing here knows what an action does.
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
//   Input     a one-line text value; Enter starts editing, Enter again emits
//             Input{id, text}, Escape cancels the edit (the value returns to what it
//             was when editing began)
// Actions are IDS THE HOST BINDS, never code in the data, which is what lets a menu's
// structure live in a file (menu_from_json / menu_to_json below) and be shared between
// hosts that bind the ids differently.
//
// KEYS (table-tested in rolltui/tests/menu_test.cpp; anything not listed returns a
// None event and changes nothing):
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
//   while editing an Input  printable text appends, Backspace erases, Ctrl-U clears the
//                           whole value, Enter submits, Escape cancels; nothing else moves
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
// is typed); the rows below are the visible items, the selected one filling the row in
// `menu_selected`, the others in `menu_item` (disabled: `text_muted`), a Submenu or
// Choice ending in " ▸", a Choice showing its value before the arrow, a Toggle as
// "[x] label", an Input as "label: value", and a shortcut right-aligned in
// `menu_shortcut`. The items scroll to keep the selection in view. DEGENERATE SIZES
// (the standing rule): 0 rows draws nothing; 1 row shows the selected item alone
// (the breadcrumb yields to the thing you can act on); a width that cannot hold a
// label clips it, and nothing is ever drawn outside the area.
//
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

struct MenuItem {
  enum class Kind : std::uint8_t { Action, Submenu, Toggle, Choice, Input };
  Kind kind = Kind::Action;
  std::string id;                  // the action id the host binds; an option's value id
  std::string label;
  std::string shortcut;            // display only ("F1"); the host decides what keys do
  bool enabled = true;
  bool checked = false;            // Toggle
  std::string value;               // Choice: the current option id; Input: the text
  std::vector<MenuItem> children;  // Submenu: items; Choice: options
  bool operator==(const MenuItem&) const = default;

  static MenuItem action(std::string id, std::string label, std::string shortcut = {});
  static MenuItem submenu(std::string id, std::string label, std::vector<MenuItem> children);
  static MenuItem toggle(std::string id, std::string label, bool checked);
  static MenuItem choice(std::string id, std::string label, std::vector<MenuItem> options, std::string value);
  static MenuItem input(std::string id, std::string label, std::string value = {});
};

struct MenuEvent {
  enum class Kind : std::uint8_t { None, Activate, Toggle, Choose, Input, Closed };
  Kind kind = Kind::None;
  std::string id;       // the acted-on item (Choose: the Choice item)
  std::string value;    // Choose: the option id; Input: the text
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
// The root is one submenu item whose label heads the breadcrumb. Unknown keys are
// reported, not ignored; a duplicate id is a bad value (the layout loader's standard).
std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report);
std::string menu_to_json(const MenuItem& root);

class Menu {
 public:
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
  void set_palette(bool on);
  bool palette() const { return palette_; }
  struct FlatEntry {
    std::vector<std::size_t> path;  // to the leaf (a Choice option's path ends at the option)
    std::string label;              // "settings › theme › mono"
  };
  const std::vector<FlatEntry>& flat() const { return flat_; }

  // ---- events (already routed to this window by the host) ----
  MenuEvent handle(const Event& e);

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
  MenuEvent handle_key(const KeyEvent& k);
  MenuEvent handle_mouse(const MouseEvent& m);
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
  std::string edit_backup_;
  bool palette_ = false;
  std::vector<FlatEntry> flat_;
  MenuOptions opt_;
  Rect area_;
  int top_ = 0;  // first visible item row's index into visible()
};

}  // namespace rolltui
