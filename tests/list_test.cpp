//
// list_test.cpp — A LIST, A TABLE AND A TREE THE HOST FEEDS BY INDEX, and the ordering and finding they stand on: natural order,
// fuzzy matching with positions, smart case and accents aside.
//
// Every widget case composes a real screen from a layout file and reads the frame back as text, so what is asserted is what a
// person sees.
//
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read cells back */
#include "rolltui_test.hpp"

using testkit::check;


namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

int nat(std::string_view a, std::string_view b) { return rolltui_natural_compare(a.data(), a.size(), b.data(), b.size()); }

void test_natural() {
  check(nat("file2", "file10") < 0 && nat("file10", "file2") > 0, "file2 before file10");
  check(nat("a", "B") < 0 && nat("B", "c") < 0, "case aside: a, B, c");
  check(nat("7", "07") < 0 && nat("07", "8") < 0, "07 beside 7, both before 8");
  check(nat("Caf\xC3\xA9", "cafe") != 0 && nat("caf\xC3\xA9 b", "cafe c") < 0, "an accent aside, until it is the only difference");
  check(nat("x", "x") == 0, "the same text is equal");
  check(nat("img12.png", "img12.jpg") > 0, "after the number, the rest");
}

void test_fuzzy() {
  int score = 0;
  std::size_t pos[8] = {};
  check(rolltui_fuzzy_match("fb", 2, "foo_bar", 7, &score, pos, 8) && pos[0] == 0 && pos[1] == 4,
        "letters in order match, and say where: f at 0, b at 4");
  check(!rolltui_fuzzy_match("fz", 2, "foo_bar", 7, nullptr, nullptr, 0), "letters that are not there do not");
  check(!rolltui_fuzzy_match("FB", 2, "foo_bar", 7, nullptr, nullptr, 0) && rolltui_fuzzy_match("FB", 2, "FooBar", 6, nullptr, nullptr, 0),
        "smart case: an uppercase letter in the pattern means case matters");
  check(rolltui_fuzzy_match("cafe", 4, "Caf\xC3\xA9 au lait", 13, nullptr, nullptr, 0), "accents aside: cafe finds Café");
  int tight = 0, loose = 0, start = 0, middle = 0;
  rolltui_fuzzy_match("abc", 3, "abc", 3, &tight, nullptr, 0);
  rolltui_fuzzy_match("abc", 3, "axbxc", 5, &loose, nullptr, 0);
  rolltui_fuzzy_match("bar", 3, "foo/bar", 7, &start, nullptr, 0);
  rolltui_fuzzy_match("bar", 3, "foobarz", 7, &middle, nullptr, 0);
  check(tight > loose, "a run scores above letters spread out");
  check(start > middle, "a match at a word's start scores above one inside a word");
  std::size_t p2[4] = {};
  check(rolltui_fuzzy_match("abc", 3, "a_b_abc", 7, nullptr, p2, 4) && p2[0] == 4, "the tightest place is the one reported");
  check(rolltui_fuzzy_match("", 0, "anything", 8, nullptr, nullptr, 0), "an empty pattern matches everything");
  check(rolltui_fuzzy_match("\xCC\x81", 2, "x", 1, nullptr, nullptr, 0), "a pattern of a mark alone folds to nothing, and matches");
}

// ---- the widget -------------------------------------------------------------------------------

struct Data {
  std::vector<std::string> keys, a, b;
  std::vector<int> depth;
  std::size_t asked = 0;
};
std::size_t count_fn(void* c) { return static_cast<Data*>(c)->keys.size(); }
void row_fn(void* c, std::size_t i, RolltuiListRow* out) {
  Data* d = static_cast<Data*>(c);
  ++d->asked;
  rolltui_str_set(&out->key, d->keys[i].data(), d->keys[i].size());
  rolltui_list_row_cell(out, d->a[i].data(), d->a[i].size());
  if (i < d->b.size()) rolltui_list_row_cell(out, d->b[i].data(), d->b[i].size());
  if (i < d->depth.size()) out->depth = static_cast<unsigned char>(d->depth[i]);
}

constexpr std::string_view kScreen = R"({ "name": "l", "root": { "column": [ { "id": "rows", "content": "list:things" } ] } })";

struct Screen {
  RolltuiLayout* layout = nullptr;
  RolltuiWindows* w = nullptr;
  int width, height;
  Screen(int wd, int ht) : width(wd), height(ht) {
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
    layout = rolltui_load_layout_text(kScreen.data(), kScreen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
    rolltui_layout_report_release(&rep);
    w = rolltui_windows_new(ctx());
  }
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;
  ~Screen() {
    rolltui_windows_free(w);
    rolltui_layout_free(layout);
  }
  std::vector<std::string> paint() {
    RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
    RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    RolltuiWindowStack* stack = rolltui_window_stack_new();
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    const RolltuiRect area{0, 0, width, height};
    RolltuiWidgetEnv env{};
    env.now_ms = 1000;
    rolltui_context_set_env(ctx(), &env);
    rolltui_windows_sync(w, stack);
    rolltui_windows_autosize(w, stack, area);
    rolltui_windows_layout(w, stack, area);
    RolltuiFrame* f = rolltui_frame_new(width, height, styles[ROLLTUI_ROLE_BACKGROUND]);
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    struct Slot {
      RolltuiWindows* w;
      RolltuiStyle* styles;
    } slot{w, styles};
    rolltui_window_stack_compose(
        stack, f, area, styles, rolltui_layout_default_roles(),
        [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* s = static_cast<Slot*>(c);
          rolltui_windows_draw(s->w, rn, fr, s->styles, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    const std::string all = str_of(text);
    rolltui_str_free(&text);
    std::vector<std::string> rows;
    std::size_t at = 0;
    while (at < all.size()) {
      const std::size_t nl = all.find('\n', at);
      rows.push_back(all.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
      if (nl == std::string::npos) break;
      at = nl + 1;
    }
    rows.resize(static_cast<std::size_t>(height));
    rolltui_compose_scratch_free(cs);
    rolltui_frame_free(f);
    rolltui_window_stack_free(stack);
    rolltui_effect_map_free(fx);
    return rows;
  }
  void send(const RolltuiChord& k) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key = k;
    rolltui_windows_handle(w, "rows", 4, &e);
  }
  void key(unsigned char k, bool shift = false) {
    RolltuiChord c{};
    c.key = k;
    c.shift = shift ? 1 : 0;
    send(c);
  }
  void chr(char32_t ch, bool alt = false) {
    RolltuiChord c{};
    c.key = ROLLTUI_KEY_CHAR;
    c.ch = ch;
    c.alt = alt ? 1 : 0;
    send(c);
  }
  void click(int x, int y, unsigned char kind = 0) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.kind = static_cast<RolltuiMouseEvent::Kind>(kind);
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.button = 1;
    rolltui_windows_handle(w, "rows", 4, &e);
  }
  std::string cursor() {
    RolltuiStr k{};
    std::string out;
    if (rolltui_windows_list_cursor(w, "things", 6, &k)) out = str_of(k);
    rolltui_str_free(&k);
    return out;
  }
  int events(unsigned char kind) {
    RolltuiListEvent ev{};
    int n = 0;
    while (rolltui_windows_list_event(w, "things", 6, &ev)) n += ev.kind == kind;
    rolltui_list_event_release(&ev);
    return n;
  }
};

bool has(const std::vector<std::string>& rows, std::string_view s) {
  for (const std::string& r : rows)
    if (r.find(s) != std::string::npos) return true;
  return false;
}
int row_of(const std::vector<std::string>& rows, std::string_view s) {
  for (std::size_t i = 0; i < rows.size(); ++i)
    if (rows[i].find(s) != std::string::npos) return static_cast<int>(i);
  return -1;
}
bool starts(const std::string& row, std::string_view s) { return row.rfind(s, 0) == 0; }

void test_unbound() {
  Screen s(30, 4);
  check(has(s.paint(), "nothing is bound to 'things'"), "a list nothing is bound to says so, as every unbound source does");
}

void test_virtual_and_moving() {
  Data d;
  for (int i = 0; i < 100000; ++i) {
    d.keys.push_back("k" + std::to_string(i));
    d.a.push_back("row " + std::to_string(i));
  }
  Screen s(30, 10);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.scrolloff = 2;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  auto rows = s.paint();
  check(starts(rows[0], "row 0") && starts(rows[9], "row 9"), "a hundred thousand rows: the screenful is drawn");
  check(d.asked < 100, "…and only about a screenful is asked for [" + std::to_string(d.asked) + "]");
  for (int i = 0; i < 8; ++i) s.key(ROLLTUI_KEY_DOWN);
  rows = s.paint();
  check(s.cursor() == "k8" && row_of(rows, "row 8") == 7,
        "scrolloff 2: the cursor stays two rows from the edge [" + std::to_string(row_of(rows, "row 8")) + "]");
  s.key(ROLLTUI_KEY_END);
  check(s.cursor() == "k99999", "End: the last row");
  check(s.events(ROLLTUI_LIST_EVENT_MOVED) == 9, "each move says so");
  // the cursor is a key: rows inserted above its row move it, and the cursor follows
  d.keys.insert(d.keys.begin(), {"new1", "new2"});
  d.a.insert(d.a.begin(), {"inserted 1", "inserted 2"});
  rolltui_windows_list_refresh(s.w, "things", 6);
  rows = s.paint();
  check(s.cursor() == "k99999" && starts(rows[9], "row 99999"), "rows inserted above: the cursor stays on its row, on the same screen row");
  o.wrap = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.key(ROLLTUI_KEY_DOWN);
  check(s.cursor() == "new1", "wrap: down from the last row is the first");
  rolltui_windows_list_set_cursor(s.w, "things", 6, "k500", 4);
  rows = s.paint();
  check(s.cursor() == "k500" && has(rows, "row 500"), "the host puts the cursor on a row by its key, and the view follows");
}

void test_table_sort_filter() {
  Data d;
  const char* names[] = {"file10", "file2", "File1", "notes", "zeta"};
  const char* sizes[] = {"2K", "10", "1M", "n/a", "3"};
  for (int i = 0; i < 5; ++i) {
    d.keys.push_back(names[i]);
    d.a.push_back(names[i]);
    d.b.push_back(sizes[i]);
  }
  Screen s(30, 8);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListColumn cols[2]{};
  rolltui_str_set(&cols[0].title, "Name", 4);
  rolltui_str_set(&cols[1].title, "Size", 4);
  cols[1].align = ROLLTUI_LIST_ALIGN_RIGHT;
  cols[1].sort = ROLLTUI_LIST_SORT_NUMBER;
  rolltui_windows_set_list_columns(s.w, "things", 6, cols, 2);
  rolltui_str_free(&cols[0].title);
  rolltui_str_free(&cols[1].title);
  auto rows = s.paint();
  check(starts(rows[0], "Name") && rows[0].find("Size") == 26, "a header names the columns, a right-aligned one at the right [" + rows[0] + "]");
  check(starts(rows[1], "file10") && rows[1].find("2K") == 28, "the host's order, the size right-aligned [" + rows[1] + "]");
  s.chr('s', true);  // Alt-S: sort by the first column
  rows = s.paint();
  check(starts(rows[1], "File1") && starts(rows[2], "file2") && starts(rows[3], "file10") && starts(rows[0], "Name \xE2\x96\xB2"),
        "sorted by name, naturally and without case, the header marked ▲");
  s.chr('s', true);  // the size column
  rows = s.paint();
  check(starts(rows[1], "zeta") && starts(rows[2], "file2") && starts(rows[3], "file10") && starts(rows[4], "File1") &&
            starts(rows[5], "notes"),
        "sorted by size as numbers (3, 10, 2K, 1M), n/a last");
  s.chr('r', true);
  rows = s.paint();
  check(starts(rows[1], "File1") && starts(rows[5], "notes") && rows[0].find("\xE2\x96\xBC") != std::string::npos,
        "reversed, ▼, and n/a still last");
  check(s.events(ROLLTUI_LIST_EVENT_SORTED) == 3, "each sort says so");
  s.chr('/');
  s.chr('f');
  s.chr('1');
  rows = s.paint();
  check(has(rows, "file10") && has(rows, "File1") && !has(rows, "file2") && !has(rows, "zeta") && starts(rows[7], "/f1"),
        "a filter typed after /, fuzzy within a cell, shown in the last row");
  s.key(ROLLTUI_KEY_ENTER);
  s.chr('/');
  s.chr(' ');
  s.chr('!');
  s.chr('0');
  rows = s.paint();
  check(!has(rows, "file10") && has(rows, "File1"), "!term leaves out what has it");
  rolltui_windows_list_set_filter(s.w, "things", 6, "nothing here", 12);
  rows = s.paint();
  check(has(rows, "no rows match \xE2\x80\x9cnothing here\xE2\x80\x9d"), "a filter that matches nothing says so");
  s.key(ROLLTUI_KEY_ESCAPE);
  rows = s.paint();
  check(has(rows, "zeta") && !has(rows, "/nothing"), "Escape clears the filter: every row again");
  s.click(2, 0);  // the Name header
  rows = s.paint();
  check(starts(rows[1], "File1") && starts(rows[0], "Name \xE2\x96\xB2"), "a click on a column's title sorts by it");
  s.click(2, 0);
  rows = s.paint();
  check(starts(rows[1], "zeta") && starts(rows[0], "Name \xE2\x96\xBC"), "…and again, the other way");
}

void test_narrow() {
  Data d;
  d.keys = {"a"};
  d.a = {"a name far too long to fit in its column"};
  d.b = {"1234567"};
  Screen s(20, 3);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListColumn cols[2]{};
  rolltui_str_set(&cols[0].title, "Name", 4);
  rolltui_str_set(&cols[1].title, "Size", 4);
  cols[1].min_screen = 30;
  rolltui_windows_set_list_columns(s.w, "things", 6, cols, 2);
  rolltui_str_free(&cols[0].title);
  rolltui_str_free(&cols[1].title);
  auto rows = s.paint();
  check(rows[0].find("Size") == std::string::npos && rows[1].find("1234567") == std::string::npos,
        "a column asking for a wider screen than this one is left out");
  check(rows[1].find("\xE2\x80\xA6") != std::string::npos && rows[1].find("fit") == std::string::npos,
        "a cell too long for its column ends in an ellipsis [" + rows[1] + "]");
}

void test_marks() {
  Data d;
  for (int i = 0; i < 10; ++i) {
    d.keys.push_back("m" + std::to_string(i));
    d.a.push_back("item " + std::to_string(i));
  }
  Screen s(30, 12);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.marks = 1;
  o.line_numbers = 2;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  s.chr(' ');
  s.chr(' ');
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 2 && s.cursor() == "m2", "Space marks and moves on");
  s.key(ROLLTUI_KEY_DOWN, true);
  s.key(ROLLTUI_KEY_DOWN, true);
  RolltuiStrList keys;
  check(rolltui_windows_list_marked(s.w, "things", 6, &keys) == 5 && keys.n == 5, "Shift-Down marks a range from where it began");
  auto rows = s.paint();
  check(starts(rows[0], "\xE2\x96\x8C") && rows[0].find(" 4 item 0") != std::string::npos && rows[3].find(" 1 item 3") != std::string::npos &&
            rows[4].find(" 5 item 4") != std::string::npos,
        "a marked row shows in the mark column; the numbers count from the cursor's, which shows its own [" + rows[0] + "]");
  s.chr('i', true);
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 5, "invert: the other five");
  check(rolltui_windows_back(s.w, "rows", 4) == 1 && rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 0,
        "back clears the marks before anything closes");
  check(rolltui_windows_back(s.w, "rows", 4) == 0, "…and with nothing left to clear, says so");
  s.paint();
  s.click(0, 1);  // the mark column of the second row
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 1 && s.cursor() == "m1", "a press in the mark column marks that row");
  s.click(6, 4, 2);  // dragged down to the fifth
  s.click(6, 4, 1);
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 4 && s.cursor() == "m4", "a drag marks the rows it passes over");
  // A ROW THAT LEFT THE DATA IS NOT MARKED: the host drops two of the marked rows and refreshes.
  d.keys.erase(d.keys.begin() + 2, d.keys.begin() + 4);
  d.a.erase(d.a.begin() + 2, d.a.begin() + 4);
  rolltui_windows_list_refresh(s.w, "things", 6);
  s.paint();
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 2, "rows the host no longer has are no longer marked, nor counted");
  s.events(0);
  s.click(6, 6, 0);
  s.click(6, 6, 8);
  check(s.events(ROLLTUI_LIST_EVENT_ACTIVATE) == 1, "a double click activates the row");
}

void test_tree() {
  Data d;
  const char* names[] = {"src", "main.c", "util", "a.c", "b.c", "README", "docs", "guide.md"};
  const int depth[] = {0, 1, 1, 2, 2, 0, 0, 1};
  for (int i = 0; i < 8; ++i) {
    d.keys.push_back(names[i]);
    d.a.push_back(names[i]);
    d.depth.push_back(depth[i]);
  }
  Screen s(30, 12);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.tree = 1;
  o.open_depth = 2;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  auto rows = s.paint();
  check(starts(rows[0], "\xE2\x96\xBE src") && starts(rows[1], "\xE2\x94\x9C\xE2\x94\x80  main.c") &&
            starts(rows[2], "\xE2\x94\x94\xE2\x94\x80\xE2\x96\xBE util"),
        "a tree: an open node ▾, a child ├─, the last child └─ [" + rows[1] + "|" + rows[2] + "]");
  check(starts(rows[3], "  \xE2\x94\x9C\xE2\x94\x80  a.c") && starts(rows[5], "  README"), "under a last child, no bar [" + rows[3] + "]");
  s.key(ROLLTUI_KEY_LEFT);  // close src
  rows = s.paint();
  check(starts(rows[0], "\xE2\x96\xB8 src") && starts(rows[1], "  README"), "closed ▸: its children are not shown");
  check(s.events(ROLLTUI_LIST_EVENT_CLOSED) == 1, "…and the host is told");
  s.key(ROLLTUI_KEY_RIGHT);
  s.key(ROLLTUI_KEY_RIGHT);  // open, then into the first child
  check(s.cursor() == "main.c", "Right opens, then goes into the first child");
  s.key(ROLLTUI_KEY_LEFT);
  check(s.cursor() == "src", "Left on a leaf goes up to its parent");
  s.click(0, 0);  // the opener
  rows = s.paint();
  check(starts(rows[0], "\xE2\x96\xB8 src"), "a press on the opener closes the node");
  rolltui_windows_list_set_filter(s.w, "things", 6, "guide", 5);
  rows = s.paint();
  check(has(rows, "docs") && has(rows, "guide.md") && !has(rows, "src") && !has(rows, "README"),
        "a filter keeps the parents of what it matches, and opens them");
  // a deeper tree: a bar for an ancestor with a later sibling
  d.keys = {"r", "a", "a1", "a1x", "b"};
  d.a = d.keys;
  d.depth = {0, 1, 2, 3, 1};
  rolltui_windows_list_set_filter(s.w, "things", 6, "", 0);
  o.open_depth = 4;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  rows = s.paint();
  check(starts(rows[2], "\xE2\x94\x82 \xE2\x94\x94\xE2\x94\x80\xE2\x96\xBE a1") && starts(rows[3], "\xE2\x94\x82   \xE2\x94\x94\xE2\x94\x80  a1x"),
        "an ancestor with a later sibling draws a bar on every row beneath it [" + rows[2] + "|" + rows[3] + "]");
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_natural();
  test_fuzzy();
  test_unbound();
  test_virtual_and_moving();
  test_table_sort_filter();
  test_narrow();
  test_marks();
  test_tree();
  return testkit::report("list_test");
}
