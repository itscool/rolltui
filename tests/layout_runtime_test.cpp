//
// layout_runtime_test.cpp — WHAT A SCREEN SHOWS, CHANGED WHILE IT RUNS: tabs in a window's border, a node shown only on a screen
// big enough for it, a node hidden or resized, a window maximised, and a screen too small for its layout told so.
//
// Every case composes a real screen from a layout file and reads the frame back as text, so what is asserted is what a person sees.
//
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_layout.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to write a layout back and read cells */
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui_test.hpp"

using testkit::check;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

struct Screen {
  RolltuiLayout* layout = nullptr;
  RolltuiWindows* w = nullptr;
  RolltuiWindowStack* stack = nullptr;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = nullptr;
  std::string problems;
  int width = 80, height = 12;
  explicit Screen(std::string_view json) {
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
    layout = rolltui_load_layout_text(json.data(), json.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
    for (std::size_t i = 0; i < rep.bad_values_n; ++i) problems += str_of(rep.bad_values[i]) + "; ";
    for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) problems += "unknown " + str_of(rep.unknown_keys[i]) + "; ";
    if (rep.error.n) problems += str_of(rep.error) + "; ";
    rolltui_layout_report_release(&rep);
    w = rolltui_windows_new(ctx());
    stack = rolltui_window_stack_new();
    if (layout) rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  }
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;
  ~Screen() {
    if (last) rolltui_frame_free(last);
    rolltui_effect_map_free(fx);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(w);
    rolltui_layout_free(layout);
  }
  RolltuiFrame* last = nullptr;
  std::vector<std::string> paint(int wd = 0, int ht = 0) {
    if (wd) width = wd;
    if (ht) height = ht;
    const RolltuiRect area{0, 0, width, height};
    RolltuiWidgetEnv env{};
    env.now_ms = 1000;
    env.still = 1;
    rolltui_context_set_env(ctx(), &env);
    rolltui_windows_sync(w, stack);
    rolltui_windows_autosize(w, stack, area);
    rolltui_windows_layout(w, stack, area);
    if (last) rolltui_frame_free(last);
    last = rolltui_frame_new(width, height, styles[ROLLTUI_ROLE_BACKGROUND]);
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    struct Slot {
      RolltuiWindows* w;
      RolltuiStyle* s;
    } slot{w, styles};
    rolltui_window_stack_compose(
        stack, last, area, styles, rolltui_layout_default_roles(),
        [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* sl = static_cast<Slot*>(c);
          rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    rolltui_compose_scratch_free(cs);
    RolltuiStr text{};
    rolltui_frame_to_text(last, &text);
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
    return rows;
  }
  unsigned char route(const RolltuiEvent& e, std::string* window = nullptr) {
    RolltuiStr win{};
    const unsigned char v = rolltui_window_stack_route(stack, &e, RolltuiRect{0, 0, width, height}, rolltui_bindings_default(ctx()),
                                                       rolltui_stack_default_actions(), &win);
    if (window) *window = str_of(win);
    rolltui_str_free(&win);
    return v;
  }
  unsigned char key(unsigned char k, bool ctrl = false, bool alt = false, char32_t ch = 0) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    e.key.ch = ch;
    e.key.ctrl = ctrl ? 1 : 0;
    e.key.alt = alt ? 1 : 0;
    return route(e);
  }
  unsigned char press(int x, int y, std::string* window = nullptr) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.kind = RolltuiMouseEvent::Kind::Press;
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.button = 1;
    return route(e, window);
  }
  RolltuiCellRaw cell(int x, int y) const {
    RolltuiCellRaw c{};
    rolltui_frame_cell(last, x, y, &c);
    return c;
  }
};

// The cell a byte offset of a row of text is in: every byte that does not continue a UTF-8 character begins one cell (the rows here
// have no wide glyphs).
int cell_of(const std::string& row, std::size_t byte) {
  int cells = 0;
  for (std::size_t i = 0; i < byte && i < row.size(); ++i) cells += (static_cast<unsigned char>(row[i]) & 0xC0) != 0x80;
  return cells;
}

bool has(const std::vector<std::string>& rows, std::string_view s) {
  for (const std::string& r : rows)
    if (r.find(s) != std::string::npos) return true;
  return false;
}

constexpr std::string_view kTabs = R"({ "name": "t", "root": { "row": [
  { "id": "side", "tabs": [ { "title": "Files", "content": "text:the files" }, { "title": "Log", "content": "text:the log" } ],
    "focusable": true },
  { "id": "main", "content": "text:the main window", "focusable": true } ] } })";

void test_tabs() {
  Screen s(kTabs);
  check(s.layout && s.problems.empty(), "a window with tabs loads [" + s.problems + "]");
  auto rows = s.paint();
  check(rows[0].find(" Files ") != std::string::npos && rows[0].find(" Log ") != std::string::npos,
        "its titles are a strip in its top border [" + rows[0] + "]");
  check(has(rows, "the files") && !has(rows, "the log"), "it shows the first tab");
  const int files_x = cell_of(rows[0], rows[0].find("Files")), log_x = cell_of(rows[0], rows[0].find("Log"));
  check(s.cell(files_x, 0).style.bold && s.cell(files_x, 0).style.underline && !s.cell(log_x, 0).style.bold,
        "the shown tab's title is bold and underlined, the others not: it reads without colour");
  std::string window;
  check(s.press(log_x + 1, 0, &window) == ROLLTUI_ROUTE_LAYOUT_CHANGED && window == "side",
        "a press on a title answers that the layout changed, naming the window");
  rows = s.paint();
  check(has(rows, "the log") && !has(rows, "the files"), "…and shows that tab");
  std::size_t count = 0;
  check(rolltui_window_stack_tab(s.stack, "side", 4, &count) == 1 && count == 2, "the stack says which tab is shown, of how many");
  rolltui_window_stack_focus(s.stack, "side", 4);
  check(s.key(ROLLTUI_KEY_PAGEDOWN, true) == ROLLTUI_ROUTE_LAYOUT_CHANGED, "Ctrl-PageDown shows the focused window's next tab");
  rows = s.paint();
  check(has(rows, "the files"), "…round to the first after the last");
  rolltui_window_stack_set_tab(s.stack, "side", 4, 1);
  rolltui_window_stack_set_base(s.stack, rolltui_layout_base(s.layout));
  rows = s.paint();
  check(has(rows, "the log"), "a new base keeps the tab the window showed");
  rolltui_window_stack_focus(s.stack, "main", 4);
  check(s.key(ROLLTUI_KEY_PAGEDOWN, true) != ROLLTUI_ROUTE_LAYOUT_CHANGED, "a window without tabs lets the key go on");
  // round trip: written back as the file wrote it
  RolltuiStr json{};
  const RolltuiLayout* L = s.layout;
  rolltui_layout_to_json_text(L->name.p, L->name.n, L->min_width, L->min_height, L->inline_mode, L->actions.v, L->actions.n, &L->base,
                              L->popups.v, L->popups.n, rolltui_layout_default_hooks(), &json);
  const std::string text = str_of(json);
  rolltui_str_free(&json);
  check(text.find("\"tabs\"") != std::string::npos && text.find("\"Files\"") != std::string::npos && text.find("text:the log") != std::string::npos,
        "the tabs are written back [" + text.substr(0, 120) + "]");
  // problems named
  Screen both(R"({ "name": "b", "root": { "id": "x", "content": "text:a", "tabs": [ { "content": "text:b" } ] } })");
  check(both.problems.find("\"content\" or \"tabs\", not both") != std::string::npos, "content and tabs together is named [" + both.problems + "]");
  Screen none(R"({ "name": "n", "root": { "id": "x", "border": "none", "tabs": [ { "content": "text:b" } ] } })");
  check(none.problems.find("draws them in its border") != std::string::npos, "tabs with no border is named [" + none.problems + "]");
}

constexpr std::string_view kBreak = R"({ "name": "b", "root": { "row": [
  { "id": "list", "content": "text:the list", "focusable": true },
  { "id": "preview", "content": "text:the preview", "focusable": true, "breakpoint": { "width": 100 } } ] } })";

void test_breakpoints() {
  Screen s(kBreak);
  check(s.problems.empty(), "a breakpoint loads [" + s.problems + "]");
  auto rows = s.paint(120, 6);
  check(has(rows, "the list") && has(rows, "the preview"), "at 120 wide, past the breakpoint, both show");
  rows = s.paint(80, 6);
  check(has(rows, "the list") && !has(rows, "the preview") && rows[0].find("the list") == 0, "at 80, below it, the preview takes no space");
  rolltui_window_stack_focus(s.stack, "list", 4);
  s.key(ROLLTUI_KEY_TAB);
  const RolltuiLayoutNode* f = rolltui_window_stack_focused(s.stack);
  std::size_t n = 0;
  const char* id = rolltui_layout_node_id(f, &n);
  check(std::string(id, n) == "list", "…and focus does not move to a window that is not shown");
}

void test_hide_size_maximize() {
  Screen s(kBreak);
  auto rows = s.paint(120, 6);
  check(rolltui_window_stack_set_visible(s.stack, "preview", 7, 0) == 1, "a node is hidden by id");
  rows = s.paint();
  check(!has(rows, "the preview"), "…and takes no space");
  rolltui_window_stack_set_visible(s.stack, "preview", 7, 1);
  check(rolltui_window_stack_set_size(s.stack, "list", 4, "20", 2) == 1, "a node is resized as the file spells it");
  rows = s.paint();
  check(cell_of(rows[0], rows[0].find("the preview")) == 20, "…the list 20 cells, the preview the rest [" + rows[0] + "]");
  check(rolltui_window_stack_set_size(s.stack, "list", 4, "big", 3) == 0, "…and a spelling that is no size is refused");
  rolltui_window_stack_focus(s.stack, "preview", 7);
  check(s.key(ROLLTUI_KEY_CHAR, false, true, 'z') == ROLLTUI_ROUTE_LAYOUT_CHANGED, "Alt-Z maximises the focused window");
  rows = s.paint();
  check(!has(rows, "the list") && rows[0].find("the preview") == 0, "…which fills the screen alone");
  std::size_t ml = 0;
  rolltui_window_stack_maximized(s.stack, &ml);
  check(ml == 7, "the stack says which window is maximised");
  s.key(ROLLTUI_KEY_CHAR, false, true, 'z');
  rows = s.paint();
  check(has(rows, "the list") && has(rows, "the preview"), "Alt-Z again puts the layout back");
  check(rolltui_window_stack_set_visible(s.stack, "nothing", 7, 0) == 0, "an id nothing has is answered 0");
}

void test_too_small() {
  Screen s(R"({ "name": "m", "min_width": 60, "min_height": 8, "root": { "id": "w", "content": "text:the window", "focusable": true } })");
  auto rows = s.paint(40, 6);
  check(has(rows, "the screen is 40\xC3\x97" "6; this needs 60\xC3\x97" "8") && !has(rows, "the window"),
        "a screen smaller than the layout needs says so, in place of the layout");
  rows = s.paint(14, 4);
  check(has(rows, "14\xC3\x97" "4 < 60\xC3\x97" "8"), "…in short where the sentence does not fit");
  check(s.press(2, 2) == ROLLTUI_ROUTE_DROPPED, "a press on the note reaches nothing");
  rows = s.paint(80, 10);
  check(has(rows, "the window"), "with room again, the layout is back");
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_tabs();
  test_breakpoints();
  test_hide_size_maximize();
  test_too_small();
  return testkit::report("layout_runtime_test");
}
