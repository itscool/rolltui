//
// text_details_test.cpp — THE SMALL THINGS TEXT NEEDS: a name cut to fit with an ellipsis where it was cut, a window's footer in
// its bottom border, a line through text that was struck out, and a selection that dims when keys go elsewhere.
//
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_render.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to render and read cells back */
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui_test.hpp"
#include "vt_model.hpp"

using testkit::check;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

std::string cut(std::string_view text, int max, int where, int aw = 0) {
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  RolltuiStr out{};
  rolltui_text_cut(ds, text.data(), text.size(), max, where, aw, &out);
  std::string s = str_of(out);
  rolltui_str_free(&out);
  rolltui_draw_scratch_free(ds);
  return s;
}

void test_cut() {
  check(cut("a short name", 20, ROLLTUI_CUT_END) == "a short name", "text that fits is whole");
  check(cut("a name far too long", 10, ROLLTUI_CUT_END) == "a name fa\xE2\x80\xA6", "cut at its end: nine cells and the ellipsis");
  check(cut("~/src/rolltui/c/main.c", 14, ROLLTUI_CUT_MIDDLE) == "~/src/r\xE2\x80\xA6main.c", "in its middle, a path keeps both ends [" +
                                                                                                    cut("~/src/rolltui/c/main.c", 14, ROLLTUI_CUT_MIDDLE) + "]");
  check(cut("~/src/rolltui/c/main.c", 10, ROLLTUI_CUT_START) == "\xE2\x80\xA6/c/main.c", "at its start, the end kept");
  check(cut("\xE6\xBC\xA2\xE5\xAD\x97\xE6\xBC\xA2\xE5\xAD\x97", 6, ROLLTUI_CUT_END) == "\xE6\xBC\xA2\xE5\xAD\x97\xE2\x80\xA6",
        "a wide glyph is never cut in half: two of them and the ellipsis in six cells, not five and a half");
  check(cut("a name far too long", 10, ROLLTUI_CUT_END, 1) == "a name f\xE2\x80\xA6", "where ambiguous glyphs are wide the ellipsis takes two cells");
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiFrame* f = rolltui_frame_new(12, 1, styles[0]);
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  const int used = rolltui_frame_put_text_cut(f, ds, 0, 0, "a name far too long", 19, styles[0], 10, ROLLTUI_CUT_END, 0, 0);
  RolltuiStr text{};
  rolltui_frame_to_text(f, &text);
  check(used == 10 && str_of(text) == "a name fa\xE2\x80\xA6\n", "drawn, the same cut in the same cells");
  rolltui_str_free(&text);
  rolltui_draw_scratch_free(ds);
  rolltui_frame_free(f);
}

// ---- footers -----------------------------------------------------------------------------------------------------------------------

std::size_t count_fn(void*) { return 100; }
void row_fn(void*, std::size_t i, RolltuiListRow* out) {
  const std::string t = "row " + std::to_string(i);
  rolltui_list_row_cell(out, t.data(), t.size());
}

std::vector<std::string> paint(RolltuiWindows* w, const RolltuiLayout* l, int wd, int ht, RolltuiFrame** keep = nullptr,
                               const char* focus = nullptr) {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
  if (focus) rolltui_window_stack_focus(stack, focus, std::strlen(focus));
  const RolltuiRect area{0, 0, wd, ht};
  rolltui_windows_sync(w, stack);
  rolltui_windows_autosize(w, stack, area);
  rolltui_windows_layout(w, stack, area);
  RolltuiFrame* f = rolltui_frame_new(wd, ht, styles[ROLLTUI_ROLE_BACKGROUND]);
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  struct Slot {
    RolltuiWindows* w;
    RolltuiStyle* s;
  } slot{w, styles};
  rolltui_window_stack_compose(
      stack, f, area, styles, rolltui_layout_default_roles(),
      [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
        Slot* sl = static_cast<Slot*>(c);
        rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
      },
      &slot, 0, cs);
  RolltuiStr t{};
  rolltui_frame_to_text(f, &t);
  const std::string all = str_of(t);
  rolltui_str_free(&t);
  std::vector<std::string> rows;
  std::size_t at = 0;
  while (at < all.size()) {
    const std::size_t nl = all.find('\n', at);
    rows.push_back(all.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  rolltui_compose_scratch_free(cs);
  if (keep) *keep = f;
  else rolltui_frame_free(f);
  rolltui_window_stack_free(stack);
  rolltui_effect_map_free(fx);
  return rows;
}

RolltuiLayout* load(std::string_view json) {
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
  RolltuiLayout* l = rolltui_load_layout_text(json.data(), json.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  return l;
}

void test_footers() {
  RolltuiLayout* l = load(R"({ "name": "f", "root": { "row": [
      { "id": "rows", "content": "list:things", "border": "single", "focusable": true },
      { "id": "note", "content": "text:a note", "border": "single", "footer": "read only" } ] } })");
  RolltuiWindows* w = rolltui_windows_new(ctx());
  rolltui_windows_bind_list(w, "things", 6, count_fn, row_fn, nullptr, nullptr);
  auto rows = paint(w, l, 60, 10);
  check(rows[9].find(" 1 of 100 ") != std::string::npos, "a list that shows fewer rows than it has says where the cursor is, in its footer [" + rows[9] + "]");
  check(rows[9].find(" read only ") != std::string::npos, "…and a window's footer from the file is at the right of its bottom border");
  check(rows[9].rfind("read only") > rows[9].find("1 of 100"), "…each in its own window's border");
  RolltuiListOptions o{};
  o.marks = 1;
  rolltui_windows_set_list_options(w, "things", 6, &o);
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = ' ';
  rolltui_windows_handle(w, "rows", 4, &e);
  rolltui_windows_handle(w, "rows", 4, &e);
  rows = paint(w, l, 60, 10);
  check(rows[9].find("3 of 100 \xC2\xB7 2 marked") != std::string::npos, "…and how many are marked [" + rows[9] + "]");
  rolltui_windows_free(w);
  rolltui_layout_free(l);
}

// ---- a line through the text ------------------------------------------------------------------------------------------------------

RolltuiDrawScratch* ds2() {
  static RolltuiDrawScratch* s = rolltui_draw_scratch_new();
  return s;
}

void test_strike() {
  RolltuiStyle plain{}, struck{};
  struck.strike = 1;
  char sgr[ROLLTUI_SGR_MAX];
  const std::size_t n = rolltui_sgr(&struck, ROLLTUI_DEPTH_TRUECOLOR, sgr, sizeof sgr);
  check(std::string(sgr, n) == "\x1b[0;9m", "a struck style is SGR 9");
  const std::size_t d = rolltui_sgr_delta(&struck, &plain, ROLLTUI_DEPTH_TRUECOLOR, sgr, sizeof sgr);
  check(std::string(sgr, d) == "\x1b[29m" || std::string(sgr, d) == "\x1b[0m", "…and off by 29, or a reset where that is as short");
  RolltuiFrame* f = rolltui_frame_new(10, 1, plain);
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  rolltui_frame_put_text(f, ds, 0, 0, "done", 4, struck, 10, 0, 0);
  RolltuiStr bytes{};
  rolltui_render_full(f, ROLLTUI_DEPTH_TRUECOLOR, &bytes);
  vt::Screen screen(10, 1);
  check(screen.apply(str_of(bytes)) && screen.at(0, 0).style.strike && !screen.at(5, 0).style.strike,
        "drawn and replayed, the struck word is struck and the rest is not");
  rolltui_str_free(&bytes);
  rolltui_draw_scratch_free(ds);
  rolltui_frame_free(f);
  // A STRUCK SPACE IS NOT A BLANK: a terminal draws the line across it, and an erase would not.
  rolltui_termfacts_set_active_erase(1);
  f = rolltui_frame_new(20, 1, plain);
  rolltui_frame_put_text(f, ds2(), 0, 0, "a", 1, plain, 20, 0, 0);
  rolltui_frame_put_text(f, ds2(), 1, 0, "          ", 10, struck, 20, 0, 0);
  rolltui_frame_put_text(f, ds2(), 11, 0, "b", 1, plain, 20, 0, 0);
  rolltui_render_full(f, ROLLTUI_DEPTH_TRUECOLOR, &bytes);
  vt::Screen wide(20, 1);
  check(wide.apply(str_of(bytes)) && wide.at(5, 0).style.strike && !wide.at(5, 0).erased,
        "a run of struck spaces is drawn, never sent as an erase that would drop its line");
  rolltui_str_free(&bytes);
  rolltui_frame_free(f);
  rolltui_termfacts_set_active_erase(-1);
  // another program's struck text
  RolltuiAnsiText* at = rolltui_ansi_new();
  static const char kStruck[] = "\x1b[9mgone\x1b[29m kept\n";
  rolltui_ansi_feed(at, kStruck, sizeof kStruck - 1);
  f = rolltui_frame_new(12, 1, plain);
  rolltui_ansi_draw(at, f, RolltuiRect{0, 0, 12, 1}, 0, 0, 0, plain, 0);
  RolltuiCellRaw c0{}, c6{};
  rolltui_frame_cell(f, 0, 0, &c0);
  rolltui_frame_cell(f, 6, 0, &c6);
  check(c0.style.strike && !c6.style.strike, "another program's SGR 9 is struck in the pane, and SGR 29 ends it");
  rolltui_frame_free(f);
  rolltui_ansi_free(at);
  // the shipped themes strike Markdown's struck text
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  check(styles[ROLLTUI_ROLE_MD_STRIKETHROUGH].strike, "the shipped default theme strikes ~~struck~~ Markdown");
  rolltui_effect_map_free(fx);
}

// ---- a selection that dims when keys go elsewhere -----------------------------------------------------------------------------------

RolltuiStyle style_at(const RolltuiFrame* f, int x, int y) {
  RolltuiCellRaw c{};
  rolltui_frame_cell(f, x, y, &c);
  return c.style;
}

void test_away() {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  // a list, with its window focused, then with the terminal's window losing the person's focus
  RolltuiLayout* l = load(R"({ "name": "a", "root": { "id": "rows", "content": "list:things", "border": "single", "focusable": true } })");
  RolltuiWindows* w = rolltui_windows_new(ctx());
  rolltui_windows_bind_list(w, "things", 6, count_fn, row_fn, nullptr, nullptr);
  RolltuiFrame* f = nullptr;
  auto rows = paint(w, l, 30, 8, &f, "rows");
  int cy = -1;
  for (int y = 0; y < 8 && cy < 0; ++y)
    if (rows[y].find("row 0") != std::string::npos) cy = y;
  check(cy > 0 && style_at(f, 2, cy) == styles[ROLLTUI_ROLE_MENU_SELECTED] && style_at(f, 0, 0).fg == styles[ROLLTUI_ROLE_BORDER_ACTIVE].fg,
        "focused, the list's cursor and its border draw as focused");
  rolltui_frame_free(f);
  rolltui_termfacts_set_active_focused(0);
  rows = paint(w, l, 30, 8, &f, "rows");
  check(style_at(f, 2, cy) == styles[ROLLTUI_ROLE_SELECTION] && style_at(f, 0, 0).fg == styles[ROLLTUI_ROLE_BORDER].fg,
        "while the terminal's window has lost the person's focus no window is focused: the cursor and border draw as away");
  rolltui_frame_free(f);
  rolltui_termfacts_set_active_focused(1);
  rolltui_windows_free(w);
  rolltui_layout_free(l);
  // a menu's selected row, away
  static const char kMenu[] = R"({ "id": "m", "items": [ { "id": "a", "label": "Alpha" }, { "id": "b", "label": "Beta" } ] })";
  RolltuiMenuItem root{};
  RolltuiMenuLoadReport rep{};
  rolltui_menu_parse_json(kMenu, sizeof kMenu - 1, &root, &rep);
  RolltuiMenu* m = rolltui_menu_new();
  rolltui_menu_set_root(m, &root);
  rolltui_menu_layout(m, RolltuiRect{0, 0, 20, 4});
  RolltuiInputRoles ir{};
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  RolltuiWindows* w2 = rolltui_windows_new(ctx());
  const RolltuiMenuRoles* roles = rolltui_windows_menu_roles(w2);
  std::string found;
  for (int focused = 1; focused >= 0; --focused) {
    RolltuiFrame* mf = rolltui_frame_new(20, 4, styles[ROLLTUI_ROLE_BACKGROUND]);
    rolltui_menu_draw(m, mf, ds, styles, roles, &ir, focused);
    RolltuiStr t{};
    rolltui_frame_to_text(mf, &t);
    const std::string text = str_of(t);
    rolltui_str_free(&t);
    int ay = -1;
    for (int y = 0, at = 0; y < 4; ++y) {
      const std::size_t nl = text.find('\n', static_cast<std::size_t>(at));
      if (text.substr(static_cast<std::size_t>(at), nl - static_cast<std::size_t>(at)).find("Alpha") != std::string::npos) ay = y;
      at = static_cast<int>(nl) + 1;
    }
    const RolltuiStyle want = styles[focused ? ROLLTUI_ROLE_MENU_SELECTED : ROLLTUI_ROLE_SELECTION];
    check(ay >= 0 && style_at(mf, 10, ay).bg == want.bg,
          focused ? "a focused menu's selected row is the selected style" : "…and away from focus it dims to the selection style, as a list's does");
    rolltui_frame_free(mf);
  }
  rolltui_windows_free(w2);
  rolltui_draw_scratch_free(ds);
  rolltui_menu_free(m);
  rolltui_menu_item_release(&root);
  rolltui_menu_load_report_release(&rep);
  rolltui_effect_map_free(fx);
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_cut();
  test_footers();
  test_strike();
  test_away();
  return testkit::report("text_details_test");
}
