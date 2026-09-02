//
// menu_test.cpp — the menu widget (milestone 11): every key in Menu.hpp's table, the
// five item kinds, the filter, the breadcrumb, palette mode, the JSON round trip and
// the degenerate-size rule (0 or 1 cells in either dimension draws nothing outside
// the area and never crashes).
//
#include <string>
#include <vector>

#include "rolltui/Menu.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

KeyEvent key(Key k) { KeyEvent e; e.key = k; return e; }
KeyEvent ch(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); return e; }

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
    if (!f.at(x, y).continuation) s += f.at(x, y).text;
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
    check(m.find("save")->value == "mine" && m.selected() == 3, "typing while editing edits the value, not the filter");
    m.handle(key(Key::Down));
    check(m.selected() == 3, "Down while editing does not move");
    ev = m.handle(key(Key::Enter));
    check(ev.kind == MenuEvent::Kind::Input && ev.id == "save" && ev.value == "mine" && !m.editing(), "Enter submits the text and ends the edit");
    m.handle(key(Key::Enter));
    m.handle(ch('x'));
    m.handle(key(Key::Escape));
    check(m.find("save")->value == "mine" && !m.editing(), "Escape cancels the edit and restores the value");
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
    check(g.at(29, 1).text == "\xE2\x96\xB2", "a ▲ marker says items are hidden above");
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
    check(m.flat().size() == 9 && vis.size() == 9, "the palette flattens every actionable leaf (3 theme options, 2 layouts, toggle, input, quit, disabled) = 9 [" +
                                                    std::to_string(m.flat().size()) + "]");
    check(m.flat()[1].label == "Theme \xE2\x80\xBA mono" && m.flat()[4].label == "Layout \xE2\x80\xBA stacked", "rows are labelled with their path");
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
          if (!a.contains(x, y) && !(f.at(x, y).text == " " && f.at(x, y).style == fill)) ok = false;
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
