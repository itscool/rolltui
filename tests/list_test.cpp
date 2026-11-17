//
// list_test.cpp — A LIST, A TABLE AND A TREE THE HOST FEEDS BY INDEX, and the ordering and finding they stand on: natural order,
// fuzzy matching with positions, smart case and accents aside.
//
// Every widget case composes a real screen from a layout file and reads the frame back as text, so what is asserted is what a
// person sees.
//
#include <cmath>
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
  unsigned long long now = 1000;  // the frame clock each paint hands the windows
  int wait = -1;                  // what the frame said it wants next (`rolltui_windows_draw_toasts`)
  bool busy = false;              // the window's title carries a spinner
  std::vector<RolltuiCell> cells;  // the last frame painted, a cell at a time
  const RolltuiCell& cell(int x, int y) const { return cells[static_cast<std::size_t>(y * width + x)]; }
  Screen(int wd, int ht, std::string_view screen = kScreen) : width(wd), height(ht) {
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
    layout = rolltui_load_layout_text(screen.data(), screen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
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
    if (busy) rolltui_window_stack_set_busy(stack, "rows", 4, 1, now);
    const RolltuiRect area{0, 0, width, height};
    RolltuiWidgetEnv env{};
    env.now_ms = now;
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
    wait = rolltui_windows_draw_toasts(w, f, area, styles, now);
    cells.assign(static_cast<std::size_t>(width * height), RolltuiCell{});
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) rolltui_frame_cell(f, x, y, &cells[static_cast<std::size_t>(y * width + x)]);
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
  void chord(unsigned char k, bool shift, bool alt) {
    RolltuiChord c{};
    c.key = k;
    c.shift = shift ? 1 : 0;
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

// GROUPED UNDER HEADINGS: the rows of each group under its heading, the cursor passing over headings, and the filter keeping a heading
// while a row under it matches.
void test_headings() {
  Data d;
  const char* names[] = {"Lists", "the row above", "the row below", "The editor", "move one word left", "move one word right"};
  const int depth[] = {0, 1, 1, 0, 1, 1};
  for (int i = 0; i < 6; ++i) {
    d.keys.push_back(names[i]);
    d.a.push_back(names[i]);
    d.depth.push_back(depth[i]);
  }
  Screen s(30, 12);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.tree = 1;
  o.headings = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  auto rows = s.paint();
  check(starts(rows[0], "Lists \xE2\x94\x80") && starts(rows[1], "the row above") && starts(rows[3], "The editor \xE2\x94\x80"),
        "a heading is its name and a rule; its rows under it, with no guide lines [" + rows[0] + "|" + rows[1] + "]");
  check(s.cursor() == "the row above", "the cursor starts on the first row, not on a heading");
  s.key(ROLLTUI_KEY_DOWN);
  s.key(ROLLTUI_KEY_DOWN);
  check(s.cursor() == "move one word left", "Down passes over a heading");
  s.key(ROLLTUI_KEY_UP);
  check(s.cursor() == "the row below", "…and so does Up");
  s.chr('/');
  for (char c : std::string("word")) s.chr(static_cast<char32_t>(c));
  rows = s.paint();
  check(starts(rows[0], "The editor") && starts(rows[1], "move one word left") && !starts(rows[0], "Lists"),
        "a filter keeps a heading while a row under it matches, and drops the others [" + rows[0] + "]");
}

// THE COLUMNS OF A SHEET: a column that is kept however narrow, one held to a share of the width, and a row with fewer cells than
// columns spreading its last one across the rest.
void test_sheet_columns() {
  auto columns = [](Screen& s, unsigned char keep, unsigned char share, int width) {
    RolltuiListColumn cols[2]{};
    cols[1].keep = keep;
    cols[1].max_share = share;
    cols[1].width = width;
    rolltui_windows_set_list_columns(s.w, "things", 6, cols, 2);
  };
  RolltuiListOptions o{};
  o.hide_header = 1;
  {
    Data d;
    d.keys = {"a"};
    d.a = {"what this key does, at length"};
    d.b = {"Ctrl-K"};
    Screen dropped(14, 3), kept(14, 3);
    rolltui_windows_bind_list(dropped.w, "things", 6, count_fn, row_fn, &d, nullptr);
    rolltui_windows_bind_list(kept.w, "things", 6, count_fn, row_fn, &d, nullptr);
    rolltui_windows_set_list_options(dropped.w, "things", 6, &o);
    rolltui_windows_set_list_options(kept.w, "things", 6, &o);
    columns(dropped, 0, 0, 0);
    columns(kept, 1, 0, 0);
    check(dropped.paint()[0].find("Ctrl-K") == std::string::npos, "(the control: a narrow list drops its second column)");
    const std::string row = kept.paint()[0];
    check(row.find("Ctrl-K") != std::string::npos && row.find("\xE2\x80\xA6") != std::string::npos,
          "a kept column stays however narrow the list, and the first is cut instead [" + row + "]");
  }
  {
    Data d;
    d.keys = {"a"};
    d.a = {"extend the selection"};
    d.b = {"Ctrl-Shift-Right, Alt-Shift-Right, Alt-Shift-F"};
    Screen s(50, 3);
    rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
    rolltui_windows_set_list_options(s.w, "things", 6, &o);
    columns(s, 0, 40, 0);
    const std::string row = s.paint()[0];
    check(row.find("extend the selection") != std::string::npos && row.find("Alt-Shift-F") == std::string::npos &&
              row.find("\xE2\x80\xA6") != std::string::npos,
          "a measured column held to 40% of the width is cut, and the first keeps the rest [" + row + "]");
  }
  {
    Data d;
    const std::string line = "a line of text long enough for both columns";
    d.keys = {"two", "one"};
    d.a = {line, line};
    d.b = {"K"};  // the second row has one cell
    Screen s(50, 3);
    rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
    rolltui_windows_set_list_options(s.w, "things", 6, &o);
    columns(s, 0, 0, 20);
    const auto rows = s.paint();
    check(rows[0].find(line) == std::string::npos, "(the control: a row with both cells has its first cut at the column) [" + rows[0] + "]");
    check(rows[1].find(line) != std::string::npos, "a row with one cell draws it across the columns it has none for [" + rows[1] + "]");
  }
}

// A NEW QUERY PUTS THE CURSOR ON ITS BEST MATCH. "kettle" is spelled, letter by letter, by "kill to the start of the line" too,
// which comes first; the row that says it is the better match.
void test_best_match() {
  Data d;
  d.keys = {"kill", "kettle"};
  d.a = {"kill to the start of the line", "put the kettle on"};
  Screen s(40, 6);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  s.chr('/');
  for (char c : std::string("kettle")) s.chr(static_cast<char32_t>(c));
  auto rows = s.paint();
  check(starts(rows[0], "kill to the start") && starts(rows[1], "put the kettle"), "both rows match, in their own order [" + rows[0] + "]");
  check(s.cursor() == "kettle", "…and the cursor is on the better match, not the first [" + s.cursor() + "]");
  s.key(ROLLTUI_KEY_ENTER);
  s.key(ROLLTUI_KEY_UP);
  s.paint();
  check(s.cursor() == "kill", "a cursor moved after the query stays where it was put, through the next frame");
}

// MOTIONS IN VIEW: half a page, the view's top, middle and bottom, scrolling with the cursor kept, and a count that names a row
// (`5` then End is the fifth row, `50` then `%` halfway), as a list's keys do in a pager.
void test_motions() {
  Data d;
  for (int i = 0; i < 40; ++i) {
    d.keys.push_back("r" + std::to_string(i));
    d.a.push_back("row " + std::to_string(i));
  }
  Screen s(30, 10);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  auto press = [&](const char* spelled) {
    RolltuiChord k{};
    rolltui_chord_parse(spelled, std::strlen(spelled), &k);
    s.send(k);
    s.paint();
  };
  press("ctrl+d");
  const std::string half = s.cursor();
  check(half != "r0" && half != "r9" && half != "r1", "Ctrl-D moves half a page, not a row and not a page [" + half + "]");
  press("alt+end");
  const std::string bottom = s.cursor();
  press("alt+home");
  const std::string top = s.cursor();
  press("alt+m");
  const std::string middle = s.cursor();
  check(top != bottom && middle != top && middle != bottom, "the view's top, middle and bottom rows are three rows [" + top + " " + middle + " " + bottom + "]");
  press("home");
  press("alt+down");
  check(s.cursor() == "r1", "scrolling down with the cursor at the top row carries it, as the view leaves its row [" + s.cursor() + "]");
  s.chr('5');
  press("end");
  check(s.cursor() == "r4", "5 then End: the fifth row, not the last [" + s.cursor() + "]");
  s.chr('5');
  s.chr('0');
  press("%");
  check(s.cursor() == "r19", "50 then %: half way down the forty rows [" + s.cursor() + "]");
  press("end");
  check(s.cursor() == "r39", "End with no count is still the last row (the control)");
}

// THE FILTER every list, menu and palette reads: its kinds of term, folding, scores, and what it says matched.
void test_filter() {
  auto passes = [](const char* q, const char* text) {
    RolltuiFilter* f = rolltui_filter_new(q, std::strlen(q));
    const bool ok = rolltui_filter_match(f, text, std::strlen(text), nullptr) != 0;
    rolltui_filter_free(f);
    return ok;
  };
  check(passes("rdm", "readme.md") && !passes("rdz", "readme.md"), "a plain term matches fuzzily");
  check(passes("'read", "readme.md") && !passes("'rdm", "readme.md"), "'term matches as written");
  check(passes("^read", "readme.md") && !passes("^me", "readme.md"), "^term matches at the start");
  check(passes(".md$", "readme.md") && !passes("read$", "readme.md"), "term$ matches at the end");
  check(passes("/re+a/", "readme.md") && !passes("/^me/", "readme.md"), "/re/ is a regular expression");
  check(passes("!txt", "readme.md") && !passes("!read", "readme.md"), "!term must not appear");
  check(passes("'cafe", "Caf\xC3\xA9 au lait") && passes("^CAF", "CAFE") && !passes("^CAF", "cafe"),
        "accents aside, and case aside unless a term has a capital");
  check(passes("re md", "readme.md") && !passes("re zz", "readme.md"), "every term must match");
  std::size_t at = 0, len = 0;
  check(rolltui_text_find("une caf\xC3\xA9 noire", 16, "cafe", 4, 0, &at, &len) && at == 4 && len == 5,
        "find folds accents and answers the bytes of what it found: caf\xC3\xA9 is five");
  check(rolltui_text_find_each("aXaXa", 5, "a", 1, nullptr, nullptr) == 3, "find each counts every match, none overlapping");
  RolltuiFilter* f = rolltui_filter_new("rme", 3);
  std::size_t b[8], e[8];
  const std::size_t n = rolltui_filter_spans(f, "readme", 6, b, e, 8);
  check(n == 3 && b[0] == 0 && b[1] == 4 && b[2] == 5, "what matched is said: r, m and e of readme");
  rolltui_filter_free(f);
}

// WHAT MATCHED IS LIT in the rows: bold and underlined, so it reads with no colour at all.
void test_lit_matches() {
  Data d;
  d.keys = {"a", "b"};
  d.a = {"readme.md", "makefile"};
  Screen s(30, 6);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  s.chr('/');
  s.chr('r');
  s.chr('m');
  auto rows = s.paint();
  check(starts(rows[0], "readme.md"), "the filter keeps readme.md");
  check(s.cell(0, 0).style.bold && s.cell(0, 0).style.underline && s.cell(4, 0).style.underline,
        "its r and its m are lit, bold and underlined");
  check(!s.cell(1, 0).style.underline, "…and a letter that did not match is not");
  // ESCAPE GOES BACK TO THE FILTER THAT WAS KEPT, then clears it
  s.key(ROLLTUI_KEY_ENTER);  // "rm" kept
  s.chr('/');
  s.chr('x');
  s.key(ROLLTUI_KEY_ESCAPE);
  rows = s.paint();
  check(has(rows, "readme.md") && !has(rows, "makefile"), "Escape while editing a kept filter goes back to it [" + rows[0] + "]");
  s.key(ROLLTUI_KEY_ESCAPE);
  rows = s.paint();
  check(has(rows, "readme.md") && has(rows, "makefile"), "…and the next Escape clears it: every row again");
}


// ---- lists and tables, the third batch ----------------------------------------------------------

// A ROW'S OWN PARTS: a span of a cell in a role of its own, a mark of the row's own in the gutter, and rows fading with distance.
struct Spanned {
  std::vector<std::string> keys;
};
std::size_t spanned_count(void* c) { return static_cast<Spanned*>(c)->keys.size(); }
void spanned_row(void* c, std::size_t i, RolltuiListRow* out) {
  const std::string& k = static_cast<Spanned*>(c)->keys[i];
  rolltui_str_set(&out->key, k.data(), k.size());
  rolltui_list_row_cell(out, "rwxr-x", 6);
  rolltui_list_row_span(out, 0, 0, 1, ROLLTUI_ROLE_ACCENT_1); // the r
  rolltui_list_row_span(out, 0, 2, 3, ROLLTUI_ROLE_ACCENT_2); // the x
  if (i == 1) {
    std::memcpy(out->gutter, "C", 2);
    out->gutter_role = ROLLTUI_ROLE_ACCENT_3;
  }
}

bool same_fg(const RolltuiCell& a, const RolltuiCell& b) {
  return a.style.fg.kind == b.style.fg.kind && a.style.fg.r == b.style.fg.r && a.style.fg.g == b.style.fg.g &&
         a.style.fg.b == b.style.fg.b && a.style.fg.index == b.style.fg.index;
}

void test_spans_gutter_fade() {
  Spanned d{{"a", "b", "c", "d", "e", "f"}};
  Screen s(20, 6);
  rolltui_windows_bind_list(s.w, "things", 6, spanned_count, spanned_row, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.gutter = 1;
  o.fade = 15;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  auto rows = s.paint();
  check(starts(rows[1], "C rwxr-x") && starts(rows[2], "  rwxr-x"), "a row's own mark stands in the gutter, a row without one blank [" +
                                                                         rows[1] + "]");
  check(!same_fg(s.cell(2, 2), s.cell(3, 2)) && !same_fg(s.cell(4, 2), s.cell(3, 2)) && !same_fg(s.cell(2, 2), s.cell(4, 2)),
        "a cell's spans draw in their roles: the r, the w between and the x each their own colour");
  check(!same_fg(s.cell(3, 1), s.cell(3, 4)), "rows further from the cursor are fainter");
  o.fade = 0;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  check(same_fg(s.cell(3, 1), s.cell(3, 4)), "…and with no fade, every row alike");
}

// A COLUMN CURSOR in a table: Left and Right move it, the sort key sorts by its column, then the other way, then the host's order;
// and a host sets and reads the sort.
void test_column_cursor() {
  Data d;
  const char* names[] = {"b", "a", "c"};
  const char* sizes[] = {"20", "30", "10"};
  for (int i = 0; i < 3; ++i) {
    d.keys.push_back(names[i]);
    d.a.push_back(names[i]);
    d.b.push_back(sizes[i]);
  }
  Screen s(24, 6);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListColumn cols[2]{};
  rolltui_str_set(&cols[0].title, "Name", 4);
  rolltui_str_set(&cols[1].title, "Size", 4);
  cols[1].sort = ROLLTUI_LIST_SORT_NUMBER;
  rolltui_windows_set_list_columns(s.w, "things", 6, cols, 2);
  rolltui_str_free(&cols[0].title);
  rolltui_str_free(&cols[1].title);
  RolltuiListOptions o{};
  o.column_cursor = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  auto rows = s.paint();
  check(s.cell(0, 0).style.reverse, "the column cursor's title is drawn reversed: Name");
  s.key(ROLLTUI_KEY_RIGHT);
  rows = s.paint();
  const int size_x = static_cast<int>(rows[0].find("Size"));
  check(rolltui_windows_list_cursor_column(s.w, "things", 6) == 1 && s.cell(size_x, 0).style.reverse && !s.cell(0, 0).style.reverse,
        "Right moves it to Size");
  check(s.cell(size_x, 1).style.underline, "…and the cursor row's cell under it is underlined");
  s.chr('s', true);
  rows = s.paint();
  int col = -1, rev = -1;
  check(rolltui_windows_list_sort_state(s.w, "things", 6, &col, &rev) == 1 && col == 1 && rev == 0 && starts(rows[1], "c") &&
            starts(rows[3], "a"),
        "the sort key sorts by the cursor's column: 10, 20, 30");
  check(s.cell(size_x, 0).style.underline, "…and the sorted column's title is underlined");
  s.chr('s', true);
  rows = s.paint();
  check(rolltui_windows_list_sort_state(s.w, "things", 6, &col, &rev) == 1 && rev == 1 && starts(rows[1], "a"), "again: the other way");
  s.chr('s', true);
  rows = s.paint();
  check(rolltui_windows_list_sort_state(s.w, "things", 6, &col, &rev) == 0 && col == -1 && starts(rows[1], "b"),
        "again: the host's own order");
  rolltui_windows_list_sort(s.w, "things", 6, 0, 0);
  rows = s.paint();
  check(starts(rows[1], "a") && starts(rows[3], "c") && s.events(ROLLTUI_LIST_EVENT_SORTED) >= 1,
        "a host sets the sort by name, and the list says it changed");
}

// MARKS BY PATTERN, and A VISUAL RANGE that follows the cursor, swaps ends, and on Escape puts the marks back as they were.
void test_pattern_and_visual() {
  Data d;
  for (int i = 0; i < 8; ++i) {
    d.keys.push_back("m" + std::to_string(i));
    d.a.push_back("item " + std::to_string(i));
  }
  Screen s(30, 10);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.marks = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  check(rolltui_windows_list_mark_matching(s.w, "things", 6, "/[13]$/", 7, 1) == 2 && rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 2,
        "rows a pattern matches are marked: item 1 and item 3");
  check(rolltui_windows_list_mark_matching(s.w, "things", 6, "3$", 2, 0) == 1 && rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 1,
        "…and unmarked the same way");
  s.chr('v', true);  // a range from m0
  s.key(ROLLTUI_KEY_DOWN);
  s.key(ROLLTUI_KEY_DOWN);
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 3 && s.cursor() == "m2", "a visual range marks from where it began to the cursor");
  s.chr(';', true);  // to the other end
  check(s.cursor() == "m0", "the swap goes to the range's other end");
  s.key(ROLLTUI_KEY_DOWN);
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 2, "…and the range now runs from m2 back to the cursor: m1 (marked before) and m2");
  check(rolltui_windows_back(s.w, "rows", 4) == 1 && rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 1,
        "Escape ends the range and puts the marks back as they were before it");
  s.chr('v', true);
  s.key(ROLLTUI_KEY_DOWN);
  s.chr('v', true);  // kept
  s.key(ROLLTUI_KEY_DOWN);
  check(rolltui_windows_list_marked(s.w, "things", 6, nullptr) == 2 && s.cursor() == "m3",
        "the visual key again keeps what the range marked (m1, m2), and the cursor then moves freely");
}

// A TREE: sorted within each level, a node opened with everything under it, a big node closed at the start, a row revealed.
void test_tree_levels() {
  Data d;
  const char* names[] = {"src", "zeta.c", "util", "two.c", "one.c", "alpha.c", "docs", "x.md"};
  const int depth[] = {0, 1, 1, 2, 2, 1, 0, 1};
  for (int i = 0; i < 8; ++i) {
    d.keys.push_back(names[i]);
    d.a.push_back(names[i]);
    d.depth.push_back(depth[i]);
  }
  Screen s(30, 12);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListColumn col{};
  rolltui_str_set(&col.title, "Name", 4);
  rolltui_windows_set_list_columns(s.w, "things", 6, &col, 1);
  rolltui_str_free(&col.title);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.tree = 1;
  o.open_depth = 3;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  rolltui_windows_list_sort(s.w, "things", 6, 0, 0);
  auto rows = s.paint();
  check(row_of(rows, "docs") == 0 && row_of(rows, "x.md") == 1 && row_of(rows, "src") == 2 && row_of(rows, "alpha.c") == 3 &&
            row_of(rows, "util") == 4 && row_of(rows, "one.c") == 5 && row_of(rows, "two.c") == 6 && row_of(rows, "zeta.c") == 7,
        "a sorted tree sorts each level and keeps every row under its parent [" + rows[3] + "|" + rows[7] + "]");
  check(rows[7].find("\xE2\x94\x94\xE2\x94\x80") != std::string::npos && rows[3].find("\xE2\x94\x9C\xE2\x94\x80") != std::string::npos,
        "…and draws its guides by the sorted order: zeta.c is the last child └─, alpha.c ├─");
  rolltui_windows_list_sort(s.w, "things", 6, -1, 0);
  s.chord(ROLLTUI_KEY_LEFT, false, true);  // close every node
  rows = s.paint();
  check(!has(rows, "one.c") && !has(rows, "zeta.c"), "every node closed");
  rolltui_windows_list_set_cursor(s.w, "things", 6, "src", 3);
  s.chord(ROLLTUI_KEY_RIGHT, true, false);  // src and all under it
  rows = s.paint();
  check(has(rows, "one.c") && has(rows, "zeta.c") && !has(rows, "x.md"), "Shift-Right opens the node and everything under it, and no other");
  s.chord(ROLLTUI_KEY_LEFT, false, true);
  check(rolltui_windows_list_reveal(s.w, "things", 6, "two.c", 5) == 1 && s.cursor() == "two.c", "reveal goes to a row inside closed nodes");
  rows = s.paint();
  check(has(rows, "two.c") && has(rows, "util"), "…opening its parents");
  check(rolltui_windows_list_set_open(s.w, "things", 6, "docs", 4, 1) == 1, "a host opens a node by its key");
  rows = s.paint();
  check(has(rows, "x.md"), "…and its rows show");
  check(rolltui_windows_list_reveal(s.w, "things", 6, "nothing", 7) == 0, "a key no row has is not revealed");
  // A NODE WITH MORE CHILDREN THAN `close_over` starts closed
  o.close_over = 2;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  Data big;
  big.keys = {"many", "1", "2", "3", "few", "4"};
  big.a = big.keys;
  big.depth = {0, 1, 1, 1, 0, 1};
  Screen t(30, 10);
  rolltui_windows_bind_list(t.w, "things", 6, count_fn, row_fn, &big, nullptr);
  rolltui_windows_set_list_options(t.w, "things", 6, &o);
  rows = t.paint();
  check(has(rows, "many") && !has(rows, "\xE2\x94\x80  1") && has(rows, "4"), "three children over a limit of two: closed; one child: open");
}

// LOADING, CHANGED ROWS AND A PAUSED LIST, each on the frame clock.
void test_states() {
  Data d;
  constexpr std::string_view kBordered =
      R"({ "name": "l", "root": { "column": [ { "id": "rows", "content": "list:things", "border": "single", "title": "jobs" } ] } })";
  Screen s(30, 8, kBordered);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.changes_ms = 500;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  rolltui_windows_list_set_loading(s.w, "things", 6, 1);
  auto rows = s.paint();
  check(!has(rows, "loading") && !has(rows, "no rows") && s.wait > 0 && s.wait <= 100,
        "rows still coming say nothing for the first 100 ms, and the frame asks to come back then");
  s.now += 150;
  rows = s.paint();
  check(has(rows, "loading\xE2\x80\xA6") && !has(rows, "no rows"), "…then \"loading\", not \"no rows\"");
  rolltui_windows_list_set_loading(s.w, "things", 6, 0);
  rows = s.paint();
  check(has(rows, "no rows"), "once they have all come, an empty list says so");
  for (int i = 0; i < 4; ++i) {
    d.keys.push_back("j" + std::to_string(i));
    d.a.push_back("job " + std::to_string(i) + " waiting");
  }
  rolltui_windows_list_refresh(s.w, "things", 6);
  rows = s.paint();
  check(s.cell(2, row_of(rows, "job 1")).style.italic, "rows that appeared are drawn as changed");
  s.now += 600;
  s.paint();
  d.a[2] = "job 2 running";
  rolltui_windows_list_refresh(s.w, "things", 6);
  rows = s.paint();
  const int r2 = row_of(rows, "job 2"), r1 = row_of(rows, "job 1");
  check(r2 > 0 && s.cell(2, r2).style.italic && s.cell(2, r2).style.bold && !s.cell(2, r1).style.italic,
        "a row that changed at a refresh is drawn bold and italic; one that did not, plain");
  check(s.wait > 0 && s.wait <= 500, "…and the frame asks to come back when it settles");
  s.now += 600;
  rows = s.paint();
  check(!s.cell(2, r2).style.italic, "…and after the time, it reads as any row again");
  rolltui_windows_list_freeze(s.w, "things", 6, 1);
  d.a[0] = "job 0 gone";
  rolltui_windows_list_refresh(s.w, "things", 6);
  rows = s.paint();
  check(has(rows, "job 0 waiting") && !has(rows, "gone") && has(rows, "paused"), "a paused list keeps its rows and its footer says paused");
  rolltui_windows_list_freeze(s.w, "things", 6, 0);
  rows = s.paint();
  check(has(rows, "job 0 gone") && !has(rows, "paused"), "…and taking the pause off shows the host's rows again");
  s.busy = true;
  rows = s.paint();
  check(rows[0].find("\xE2\xA0\x8B jobs") != std::string::npos, "a busy window's title starts with a spinner [" + rows[0] + "]");
  s.busy = false;
  rows = s.paint();
  check(rows[0].find(" jobs ") != std::string::npos && rows[0].find("\xE2\xA0\x8B") == std::string::npos, "…and without it, the title alone");
}

// ROWS THE PERSON MOVES: by key and by a drag, the list asking the host; and a drag held past the view's edge going on scrolling.
void test_reorder_and_edge() {
  Data d;
  for (int i = 0; i < 5; ++i) {
    d.keys.push_back("r" + std::to_string(i));
    d.a.push_back("row " + std::to_string(i));
  }
  Screen s(20, 8);
  rolltui_windows_bind_list(s.w, "things", 6, count_fn, row_fn, &d, nullptr);
  RolltuiListOptions o{};
  o.hide_header = 1;
  o.reorder = 1;
  rolltui_windows_set_list_options(s.w, "things", 6, &o);
  s.paint();
  s.events(0);
  auto take = [&](std::string* key, std::string* before) {
    RolltuiListEvent ev{};
    bool got = false;
    while (rolltui_windows_list_event(s.w, "things", 6, &ev))
      if (ev.kind == ROLLTUI_LIST_EVENT_REORDER) {
        *key = str_of(ev.key);
        *before = str_of(ev.before);
        got = true;
      }
    rolltui_list_event_release(&ev);
    return got;
  };
  std::string key, before;
  s.chord(ROLLTUI_KEY_DOWN, true, true);
  check(take(&key, &before) && key == "r0" && before == "r2", "Alt-Shift-Down asks the host to move r0 to before r2");
  // the host moves it, and the cursor follows the key
  std::swap(d.keys[0], d.keys[1]);
  std::swap(d.a[0], d.a[1]);
  rolltui_windows_list_refresh(s.w, "things", 6);
  auto rows = s.paint();
  check(s.cursor() == "r0" && starts(rows[1], "row 0"), "…and once the host has, the cursor is still on r0, one row down");
  s.chord(ROLLTUI_KEY_UP, true, true);
  check(take(&key, &before) && key == "r0" && before == "r1", "Alt-Shift-Up: before the row above");
  s.click(2, 0);
  s.click(2, 3, 2);
  rows = s.paint();
  check(starts(rows[3], "row 1") && starts(rows[0], "row 0") && starts(rows[2], "row 3"),
        "a drag from a row shows it where it would land, the others closing up behind it [" + rows[0] + "|" + rows[3] + "]");
  s.click(2, 3, 1);
  check(take(&key, &before) && key == "r1" && before == "r4", "…and letting go asks the host to move it there");
  // EDGE SCROLL: a range drag held below the view goes on, a row every 50 ms
  Data many;
  for (int i = 0; i < 30; ++i) {
    many.keys.push_back("m" + std::to_string(i));
    many.a.push_back("item " + std::to_string(i));
  }
  Screen t(20, 6);
  rolltui_windows_bind_list(t.w, "things", 6, count_fn, row_fn, &many, nullptr);
  RolltuiListOptions m{};
  m.hide_header = 1;
  m.marks = 1;
  rolltui_windows_set_list_options(t.w, "things", 6, &m);
  t.paint();
  t.click(4, 0);
  t.click(4, 9, 2);  // below the view
  t.paint();
  check(t.cursor() == "m5" && t.wait > 0 && t.wait <= 50, "a drag below the view reaches its last row and asks for the next frame");
  t.now += 200;
  t.paint();
  check(t.cursor() == "m9" && rolltui_windows_list_marked(t.w, "things", 6, nullptr) == 10,
        "…and held there, it goes on: four rows in 200 ms, every one marked");
  t.click(4, 2, 2);  // back inside
  const std::string inside = t.cursor();
  t.now += 200;
  t.paint();
  check(t.cursor() == inside && t.wait < 0, "inside the view again, it stops, and asks for no frame [" + inside + "]");
  t.click(4, 2, 1);
  // A SORTED LIST DOES NOT MOVE ROWS: the sort says where each one goes
  RolltuiListColumn c{};
  rolltui_str_set(&c.title, "Name", 4);
  rolltui_windows_set_list_columns(s.w, "things", 6, &c, 1);
  rolltui_str_free(&c.title);
  rolltui_windows_list_sort(s.w, "things", 6, 0, 0);
  s.paint();
  s.events(0);
  s.chord(ROLLTUI_KEY_DOWN, true, true);
  check(!take(&key, &before), "a sorted list does not move rows");
}

void test_sparkline() {
  const double up[] = {0, 1, 2, 3, 4, 5, 6, 7};
  RolltuiStr out{};
  rolltui_sparkline_text(up, 8, 0, 0, 0, &out);
  check(str_of(out) == "\xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x84\xE2\x96\x85\xE2\x96\x86\xE2\x96\x87\xE2\x96\x88",
        "a sparkline as text, over the values' own range: ▁ to █");
  rolltui_str_clear(&out);
  const double gap[] = {1, NAN, 1};
  rolltui_sparkline_text(gap, 3, 0, 2, 0, &out);
  check(str_of(out) == "\xE2\x96\x85 \xE2\x96\x85", "a NaN is a gap; a set range places the values in it [" + str_of(out) + "]");
  rolltui_str_clear(&out);
  rolltui_sparkline_text(up, 8, 0, 0, 1, &out);
  check(str_of(out).size() == 8 && str_of(out)[0] == '_' && str_of(out)[7] == '^', "where block elements are wide, ASCII");
  rolltui_str_free(&out);
}

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
  test_headings();
  test_sheet_columns();
  test_best_match();
  test_motions();
  test_filter();
  test_lit_matches();
  test_spans_gutter_fade();
  test_column_cursor();
  test_pattern_and_visual();
  test_tree_levels();
  test_states();
  test_reorder_and_edge();
  test_sparkline();
  return testkit::report("list_test");
}
