//
// menu_editor_test.cpp — the menu editor's model (plan/phase-27.md m2), driven headless the
// way its three siblings are: the item tree (add as a child and as a sibling, remove, reorder),
// the kind and the fields each kind offers, a choice's options, an input's type and range,
// undo/redo, the outcomes, and — the one that matters — THE TREE ROUND-TRIPPING through the
// library's own writer and loader unchanged.
//
// THE PROPERTY TO HOLD ON TO is the fourth editor's reason for existing: what this editor
// saves, `rolltui_menu_parse_json` must read back as the same tree. It is the only assertion
// here that could not be satisfied by an editor that merely looked right.
//
// SELECTION IS AN INDEX PATH, and this file asserts why rather than taking it on trust: a
// Choice's options have their own id namespace (the loader's `option_ids`), so two items in
// one file may legitimately share an id and an id is not a selector.
//
// Every menu lookup goes through value_of / enabled_of, which NAME a missing item instead of
// dereferencing a null — layout_editor_test.cpp segfaulted on exactly that once, and a control
// that crashes reports nothing.
//
#include <optional>
#include <string>

#include "menu_editor.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_menu.h"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui::tools;
using namespace rolltui_test;

namespace {

RolltuiChord key(unsigned char k, bool shift = false) {
  RolltuiChord e{};
  e.key = k;
  e.shift = shift ? 1 : 0;
  return e;
}
RolltuiChord ch(char c) {
  RolltuiChord e{};
  e.key = ROLLTUI_KEY_CHAR;
  e.ch = static_cast<char32_t>(c);
  return e;
}
RolltuiChord ctrl(char c) {
  RolltuiChord e = ch(c);
  e.ctrl = 1;
  return e;
}
MenuEditor::Outcome handle(MenuEditor& ed, const RolltuiChord& k) {
  const RolltuiEvent e{ROLLTUI_EVENT_KEY, k, {}, nullptr, 0};
  return ed.handle(&e);
}
void type(MenuEditor& ed, const std::string& s) {
  for (char c : s) handle(ed, ch(c));
}
// Reaches a top-level item by filter and acts on it — layout_editor_test.cpp's `act`, and for
// the same reason: the editor's own menu is a menu, so a test drives it the way a person does.
MenuEditor::Outcome act(MenuEditor& ed, const std::string& filter) {
  handle(ed, key(ROLLTUI_KEY_ESCAPE));
  handle(ed, key(ROLLTUI_KEY_ESCAPE));
  handle(ed, key(ROLLTUI_KEY_HOME));
  type(ed, filter);
  return handle(ed, key(ROLLTUI_KEY_ENTER));
}
// An INPUT field: reach it, clear what it is prefilled with, type, commit. The clear is not
// optional — a menu input opens holding its current value, so typing alone appends.
MenuEditor::Outcome set_input(MenuEditor& ed, const std::string& filter, const std::string& value) {
  act(ed, filter);
  for (int i = 0; i < 40; ++i) handle(ed, key(ROLLTUI_KEY_BACKSPACE));
  type(ed, value);
  return handle(ed, key(ROLLTUI_KEY_ENTER));
}
// A CHOICE: reach it (it opens on the current value), filter to the option, commit.
MenuEditor::Outcome pick(MenuEditor& ed, const std::string& filter, const std::string& option) {
  act(ed, filter);
  type(ed, option);
  return handle(ed, key(ROLLTUI_KEY_ENTER));
}
MenuItem* find(RolltuiMenu* m, std::string_view id) { return rolltui_menu_find(m, id.data(), id.size()); }
std::string value_of(const MenuEditor& ed, const char* id) {
  const MenuItem* it = find(const_cast<MenuEditor&>(ed).menu(), id);
  return it ? str_of(it->value) : "(no item '" + std::string(id) + "')";
}
bool enabled_of(const MenuEditor& ed, const char* id) {
  const MenuItem* it = find(const_cast<MenuEditor&>(ed).menu(), id);
  return it && it->enabled;
}
// The library's own reader, over the editor's own writer: the round trip this file exists for.
std::optional<MenuItem> parse_menu(const std::string& text, bool* clean) {
  MenuItem out;
  RolltuiMenuLoadReport rep{};
  const int ok = rolltui_menu_parse_json(text.data(), text.size(), &out, &rep);
  if (clean) *clean = ok && rolltui_menu_load_report_clean(&rep);
  rolltui_menu_load_report_release(&rep);
  if (!ok) return std::nullopt;
  return out.clone();
}

}  // namespace

int main() {
  RolltuiContext* ctx = test_context();

  // ---- the skeleton: what "new" starts from, asserted as what it IS ----------------------
  {
    MenuEditor ed(ctx);
    const MenuItem s = ed.skeleton("tools");
    check(s.kind == MenuItem::Kind::Submenu && str_of(s.label) == "tools" && s.children.size() == 1,
          "the skeleton is a root submenu carrying the typed name, with one item");
    check(ed.selected().empty() && ed.selected_item() && str_of(ed.selected_item()->id) == "root",
          "a fresh editor has the root selected");
    check(MenuEditor::paths_in_order(ed.current()).size() == 2, "…and the tree is the root plus that one item");
  }

  // ---- add as a child, add as a sibling, and what each one means -------------------------
  {
    MenuEditor ed(ctx);
    handle(ed, key(ROLLTUI_KEY_TAB));  // select the skeleton's one item
    check(ed.selected().size() == 1 && str_of(ed.selected_item()->id) == "item", "Tab selects the first child");

    const MenuEditor::Outcome o = set_input(ed, "add a sibling", "second");
    check(o.kind == MenuEditor::Outcome::Kind::Committed, "adding a sibling commits");
    check(ed.current().children.size() == 2 && str_of(ed.current().children[1].id) == "second",
          "…and it lands after the selected item, under the same parent");
    check(ed.selected().size() == 1 && ed.selected()[0] == 1, "…and the new item is selected");

    set_input(ed, "add a child", "nested");
    check(ed.current().children[1].children.size() == 1 &&
              str_of(ed.current().children[1].children[0].id) == "nested",
          "adding a child NESTS: that is how a submenu is built");
    check(ed.selected() == MenuPath{1, 0}, "…and the selection follows it down");
  }

  // ---- an id is not a selector, which is why a path is ------------------------------------
  {
    MenuEditor ed(ctx);
    handle(ed, key(ROLLTUI_KEY_TAB));
    pick(ed, "kind", "choice");
    set_input(ed, "add a child", "16");
    // A sibling of the CHOICE (not of the option) may legitimately take the same id, because
    // the loader gives a choice's options their own namespace.
    ed.select({});
    set_input(ed, "add a child", "16");
    const MenuItem& root = ed.current();
    check(root.children.size() == 2 && str_of(root.children[1].id) == "16" &&
              str_of(root.children[0].children[0].id) == "16",
          "an option and a top-level item may share an id — so an id could not select either");
    bool clean = false;
    const auto back = parse_menu(ed.to_json(), &clean);
    check(back && clean, "…and the library reads that file back with no complaint");
  }

  // ---- the kind decides which fields exist -----------------------------------------------
  {
    MenuEditor ed(ctx);
    handle(ed, key(ROLLTUI_KEY_TAB));
    check(enabled_of(ed, "action") && !enabled_of(ed, "checked") && !enabled_of(ed, "input_type"),
          "an Action offers its action name, and neither a checked nor an input type");
    pick(ed, "kind", "toggle");
    check(enabled_of(ed, "checked") && !enabled_of(ed, "input_type"), "a Toggle offers checked");
    pick(ed, "kind", "input");
    check(enabled_of(ed, "input_type") && enabled_of(ed, "hint") && !enabled_of(ed, "checked"),
          "an Input offers its type and hint, and no checked");
    check(!enabled_of(ed, "min") && !enabled_of(ed, "max"),
          "…and a TEXT input offers no range: a range is a number's, and the field says so by being off");
    pick(ed, "input type", "int");
    check(enabled_of(ed, "min") && enabled_of(ed, "max") && enabled_of(ed, "step"),
          "…while an Int input does offer one");
    ed.select({});
    check(!enabled_of(ed, "kind") && !enabled_of(ed, "remove") && !enabled_of(ed, "move_up"),
          "the root is the file's tree: its kind is fixed and it cannot be removed or moved");
  }

  // ---- an input's type and range survive the round trip -----------------------------------
  {
    MenuEditor ed(ctx);
    handle(ed, key(ROLLTUI_KEY_TAB));
    set_input(ed, "item id", "level");
    set_input(ed, "label", "Level");
    pick(ed, "kind", "input");
    pick(ed, "input type", "int");
    set_input(ed, "minimum", "0");
    set_input(ed, "maximum", "9");
    set_input(ed, "hint", "0 lightest, 9 darkest");
    check(value_of(ed, "min") == "0" && value_of(ed, "max") == "9",
          "a range types back as it was typed [" + value_of(ed, "min") + ".." + value_of(ed, "max") + "]");

    bool clean = false;
    const std::string text = ed.to_json();
    const auto back = parse_menu(text, &clean);
    check(back && clean, "the file the editor writes loads clean through the library's own reader");
    check(back && back->children.size() == 1 && back->children[0].kind == MenuItem::Kind::Input &&
              back->children[0].spec.type == InputType::Int && back->children[0].spec.min == 0 &&
              back->children[0].spec.max == 9,
          "…and the typed input comes back with its type and its range intact");
    check(back && str_of(back->children[0].spec.hint) == "0 lightest, 9 darkest", "…and its hint");
    check(back && *back == ed.current(), "…and the whole tree compares equal: a menu is a FILE, not a claim");
  }

  // ---- reorder, and remove takes the subtree with it --------------------------------------
  {
    MenuEditor ed(ctx);
    handle(ed, key(ROLLTUI_KEY_TAB));
    set_input(ed, "add a sibling", "b");
    set_input(ed, "add a sibling", "c");
    check(str_of(ed.current().children[1].id) == "b" && str_of(ed.current().children[2].id) == "c",
          "three items, in the order they were added");
    act(ed, "move up");
    check(str_of(ed.current().children[1].id) == "c" && str_of(ed.current().children[2].id) == "b",
          "move up swaps with the previous sibling");
    check(ed.selected() == MenuPath{1}, "…and the selection travels with the item, not with the slot");
    ed.select({2});
    set_input(ed, "add a child", "deep");
    ed.select({2});
    act(ed, "remove this");
    check(ed.current().children.size() == 2, "remove takes the item…");
    check(MenuEditor::paths_in_order(ed.current()).size() == 3, "…and everything under it");
  }

  // ---- undo and redo, over whole-tree snapshots -------------------------------------------
  {
    MenuEditor ed(ctx);
    handle(ed, key(ROLLTUI_KEY_TAB));
    set_input(ed, "label", "First");
    set_input(ed, "add a sibling", "gone");
    check(ed.current().children.size() == 2 && ed.undo_depth() >= 2, "two commits");
    check(handle(ed, ctrl('z')).kind == MenuEditor::Outcome::Kind::Committed, "Ctrl-Z is a committed change");
    check(ed.current().children.size() == 1, "…and the added item is gone");
    check(str_of(ed.current().children[0].label) == "First", "…while the earlier edit stands");
    handle(ed, ctrl('y'));
    check(ed.current().children.size() == 2, "Ctrl-Y brings it back");
  }

  // ---- the outcomes the host acts on --------------------------------------------------------
  {
    MenuEditor ed(ctx);
    ed.set_menus({"main", "tools"});
    const MenuEditor::Outcome save = set_input(ed, "save menu", "palette");
    check(save.kind == MenuEditor::Outcome::Kind::SaveAs && save.value == "palette",
          "Save as reports the file name and writes nothing itself — the host owns the store");
    const MenuEditor::Outcome load = act(ed, "load menu");
    check(load.kind == MenuEditor::Outcome::Kind::LoadMenu || load.kind == MenuEditor::Outcome::Kind::None,
          "Load offers the host's menu names");
    check(act(ed, "reset to").kind == MenuEditor::Outcome::Kind::ResetLoaded,
          "Reset asks the host, which is where the confirm popup lives");
    const MenuEditor::Outcome fresh = set_input(ed, "new menu", "palette");
    check(fresh.kind == MenuEditor::Outcome::Kind::Committed && ed.current().children.size() == 1 &&
              str_of(ed.current().label) == "palette",
          "New menu replaces the tree with the stated skeleton, carrying the typed name");
  }

  // ---- what this editor CANNOT verify, and says so by not pretending ------------------------
  {
    MenuEditor ed(ctx);
    ed.set_actions({"app.help", "app.menu"});
    handle(ed, key(ROLLTUI_KEY_TAB));
    set_input(ed, "action it", "app.easel");
    check(str_of(ed.current().children[0].action_name) == "app.easel",
          "an action this tool has never heard of is WRITTEN — the app owns that name, not the designer");
    const MenuItem* field = find(ed.menu(), "action");
    check(field && str_of(field->spec.hint).find("app.help") != std::string::npos,
          "…and what this binary knows is the field's HINT, never its option list");
    bool clean = false;
    check(parse_menu(ed.to_json(), &clean) && clean,
          "…and the file carrying it is a clean menu file: the gap is the APP's to report at start-up");
  }

  return report("rolltui menu_editor_test");
}
