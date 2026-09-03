//
// menu_test.cpp — the menu widget (milestone 11): every key in Menu.hpp's table, the
// five item kinds, the filter, the breadcrumb, palette mode, the JSON round trip and
// the degenerate-size rule (0 or 1 cells in either dimension draws nothing outside
// the area and never crashes).
//
#include <fstream>
#include <sstream>
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
    KeyEvent ctrl_u = ch('u');
    ctrl_u.ctrl = true;
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
    m.apply_shortcuts(default_bindings());
    auto sc = [&](const char* id) { const MenuItem* it = m.find(id); return it ? it->shortcut : std::string("(missing)"); };
    check(sc("a") == "F1, ?" && sc("b") == "F2" && sc("c") == "F5",
          "apply_shortcuts fills them from the LIVE chords and leaves a plain shortcut alone [" + sc("a") + "]");
    Bindings rebound = default_bindings();
    rebound.clear("app.help");
    rebound.bind("app.help", *parse_chord("f8"));
    m.apply_shortcuts(rebound);
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
         MenuItem::input("opt", "Optional", opt, "")});                                // 10
    Menu m(typed);
    auto open = [&](int index) {
      m.reset();
      for (int i = 0; i < index; ++i) m.handle(key(Key::Down));
      m.handle(key(Key::Enter));
    };
    auto type = [&](std::string_view s) { for (char c : s) m.handle(ch(c)); };
    KeyEvent ctrl_u = ch('u');
    ctrl_u.ctrl = true;
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
    };
    for (const Row& r : rows) {
      open(r.item);
      m.handle(ctrl_u);
      type(r.typed);
      const std::string text = m.editing_text();
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
    m.handle(Event(good));
    check(m.editing_text() == "12", "a pasted '12' replaces the selection [" + m.editing_text() + "]");
    m.handle(Event(bad));
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
