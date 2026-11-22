// dialog_test.cpp — A QUESTION THE APP ASKS (`rolltui_windows_ask`, the `dialog` kind, `rolltui_windows_answer`), driven the way a
// host drives one: every event routed through the stack with the shipped bindings, then handed to the window it names, and the
// answer collected after. What is asserted is read back off the composed frame.
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read cells back */
#include "rolltui_test.hpp"

using namespace testkit;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

constexpr std::string_view kScreen = R"({ "name": "d", "root": { "column": [ { "id": "body", "content": "text:the app" } ] } })";

struct Screen {
  RolltuiLayout* layout = nullptr;
  RolltuiWindows* w = nullptr;
  RolltuiWindowStack* stack = nullptr;
  int width, height;
  const char* theme = "default-dark";
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  std::vector<RolltuiCell> cells;
  std::vector<std::string> rows;
  int cur_x = -1, cur_y = -1, cur_vis = 0;
  Screen(int wd, int ht) : width(wd), height(ht) {
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
    layout = rolltui_load_layout_text(kScreen.data(), kScreen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
    rolltui_layout_report_release(&rep);
    w = rolltui_windows_new(ctx());
    stack = rolltui_window_stack_new();
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
  }
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;
  ~Screen() {
    rolltui_windows_free(w);
    rolltui_window_stack_free(stack);
    rolltui_layout_free(layout);
  }
  const RolltuiCell& cell(int x, int y) const { return cells[static_cast<std::size_t>(y * width + x)]; }
  void paint() {
    RolltuiEffectMap* fx = rolltui_theme_builtin_fill(theme, std::strlen(theme), styles, ROLLTUI_ROLE_COUNT);
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
    rolltui_frame_cursor(f, &cur_x, &cur_y, &cur_vis);
    cells.assign(static_cast<std::size_t>(width * height), RolltuiCell{});
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) rolltui_frame_cell(f, x, y, &cells[static_cast<std::size_t>(y * width + x)]);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    const std::string all = str_of(text);
    rolltui_str_free(&text);
    rows.clear();
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
    rolltui_effect_map_free(fx);
  }
  bool has(std::string_view s) const {
    for (const std::string& r : rows)
      if (r.find(s) != std::string::npos) return true;
    return false;
  }
  int row_of(std::string_view s) const {
    for (std::size_t y = 0; y < rows.size(); ++y)
      if (rows[y].find(s) != std::string::npos) return static_cast<int>(y);
    return -1;
  }
  // The column `s` (ASCII) starts at on row `y`, read cell by cell; -1 when it is not there.
  int col_of(int y, std::string_view s) const {
    for (int x = 0; x + static_cast<int>(s.size()) <= width; ++x) {
      bool ok = true;
      for (std::size_t i = 0; i < s.size() && ok; ++i) {
        const RolltuiCell& c = cell(x + static_cast<int>(i), y);
        ok = c.len == 1 && c.bytes[0] == s[i];
      }
      if (ok) return x;
    }
    return -1;
  }
  // As a host does it: routed through the stack, then handed to the window the route names.
  unsigned char send(const RolltuiEvent& e) {
    RolltuiStr window{};
    const unsigned char kind = rolltui_window_stack_route(stack, &e, RolltuiRect{0, 0, width, height}, rolltui_bindings_default(ctx()),
                                                          rolltui_stack_default_actions(), &window);
    if (kind == ROLLTUI_ROUTE_DELIVER) rolltui_windows_handle(w, window.p ? window.p : "", window.n, &e);
    rolltui_str_free(&window);
    return kind;
  }
  unsigned char key(unsigned char k, bool shift = false, bool alt = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    e.key.shift = shift ? 1 : 0;
    e.key.alt = alt ? 1 : 0;
    return send(e);
  }
  unsigned char chr(char32_t ch, bool alt = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = ROLLTUI_KEY_CHAR;
    e.key.ch = ch;
    e.key.alt = alt ? 1 : 0;
    return send(e);
  }
  void type(std::string_view s) {
    for (char c : s) chr(static_cast<unsigned char>(c));
  }
  void click(int x, int y) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.button = 1;
    send(e);
  }
  int answer(const char* id, int* button, std::string* copy = nullptr) {
    RolltuiStr c{};
    const int r = rolltui_windows_answer(w, stack, id, std::strlen(id), button, &c);
    if (copy) *copy = str_of(c);
    rolltui_str_free(&c);
    return r;
  }
  void ask(const char* id, RolltuiDialog* d) {
    rolltui_windows_ask(w, stack, id, std::strlen(id), d);
    rolltui_windows_sync(w, stack);
  }
};

RolltuiDialog* delete_three(const char* phrase = nullptr) {
  const char title[] = "Delete 3 files?";
  const char body[] = "They go to the Trash, where they can be put back.";
  RolltuiDialog* d = rolltui_dialog_new(ROLLTUI_MESSAGE_WARNING, 0, title, sizeof title - 1, body, sizeof body - 1);
  for (const char* item : {"notes.txt", "draft.md", "old photo.jpg"}) rolltui_dialog_add_item(d, item, std::strlen(item));
  rolltui_dialog_add_button(d, "Delete", 6, phrase ? ROLLTUI_DIALOG_DEFAULT : 0);
  rolltui_dialog_add_button(d, "Cancel", 6, ROLLTUI_DIALOG_CANCEL | (phrase ? 0 : ROLLTUI_DIALOG_DEFAULT));
  if (phrase) rolltui_dialog_set_phrase(d, phrase, std::strlen(phrase));
  return d;
}

void test_shown() {
  Screen s(60, 20);
  s.ask("del", delete_three());
  s.paint();
  const int top = s.row_of("Delete 3 files?"), btn = s.row_of("[ Delete ]");
  check(top > 0 && s.has("They go to the Trash") && s.has("notes.txt") && s.has("old photo.jpg") && btn > top,
        "the title in its border, the body, the items and the buttons are on screen [" + std::to_string(top) + ", " + std::to_string(btn) + "]");
  check(s.rows[static_cast<std::size_t>(top)].find("! Delete 3 files?") != std::string::npos,
        "…the title after the level's glyph, so a warning reads as one without colour [" + s.rows[static_cast<std::size_t>(top)] + "]");
  // AS TALL AS WHAT IT HOLDS: the body (two lines at 34 cells), a gap, three items, a gap, the buttons, and the two border rows:
  // 10 rows, centred on 20
  const int bottom = btn + 1;
  check(bottom - top + 1 == 10 && top == (20 - 10) / 2, "as tall as what it holds, and centred [" + std::to_string(top) + ".." + std::to_string(bottom) + "]");
  const int dx = s.col_of(btn, "[ Delete ]"), cx = s.col_of(btn, "[ Cancel ]");
  check(cx > dx && s.cell(cx + 3, btn).style == [&] {
          RolltuiStyle st = s.styles[ROLLTUI_ROLE_MENU_SELECTED];
          st.bold = 1;
          return st;
        }(),
        "the default button is chosen, in the menu's selection and bold");
  check(s.cell(dx + 2, btn).style.underline && s.cell(cx + 2, btn).style.underline && !s.cell(dx + 3, btn).style.underline,
        "each button's letter is underlined, and only it");
  const int lx = s.col_of(top, "!");
  check(lx > 0 && s.cell(lx - 2, top).style.fg == s.styles[ROLLTUI_ROLE_WARNING].fg,
        "the border is drawn in the level's role");
}

void test_unasked() {
  // A SCREEN NAMING A DIALOG NO HOST ASKED says so in the window, as an unbound source does
  constexpr std::string_view screen = R"({ "name": "u", "root": { "column": [ { "id": "q", "content": "dialog:nobody" } ] } })";
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
  RolltuiLayout* l = rolltui_load_layout_text(screen.data(), screen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(ctx());
  RolltuiWindowStack* st = rolltui_window_stack_new();
  rolltui_window_stack_set_base(st, rolltui_layout_base(l));
  rolltui_windows_sync(w, st);
  RolltuiStr sum{};
  rolltui_windows_report_summary(w, &sum);
  check(str_of(sum).find("no dialog is asked under 'nobody'") != std::string::npos, "a dialog no host asked is named as missing [" + str_of(sum) + "]");
  rolltui_str_free(&sum);
  rolltui_windows_free(w);
  rolltui_window_stack_free(st);
  rolltui_layout_free(l);
}

void test_answers() {
  int b = -9;
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.paint();
    check(s.answer("del", &b) == 0, "open and unanswered: nothing to collect");
    s.key(ROLLTUI_KEY_TAB);
    s.key(ROLLTUI_KEY_ENTER);
    check(s.answer("del", &b) == ROLLTUI_DIALOG_ANSWERED && b == 0 && rolltui_window_stack_depth(s.stack) == 1,
          "Tab chooses the next button and Enter answers with it; the dialog is closed [" + std::to_string(b) + "]");
    check(s.answer("del", &b) == 0, "…and told once");
  }
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.paint();
    s.chr('d');
    check(s.answer("del", &b) == ROLLTUI_DIALOG_ANSWERED && b == 0, "a button's letter answers with it");
  }
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.paint();
    const unsigned char kind = s.key(ROLLTUI_KEY_ESCAPE);
    check(kind == ROLLTUI_ROUTE_CLOSED_POPUP && s.answer("del", &b) == ROLLTUI_DIALOG_ANSWERED && b == 1,
          "the close key answers the cancel button");
  }
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.paint();
    const int y = s.row_of("[ Delete ]");
    s.click(s.col_of(y, "[ Delete ]") + 3, y);
    check(s.answer("del", &b) == ROLLTUI_DIALOG_ANSWERED && b == 0, "a press on a button answers with it");
  }
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.paint();
    rolltui_window_stack_pop(s.stack);
    check(s.answer("del", &b) == ROLLTUI_DIALOG_ANSWERED && b == 1, "closed by the host: the cancel button's answer");
  }
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.ask("del", delete_three());
    check(rolltui_window_stack_depth(s.stack) == 2, "asked again under the same id: the open one is replaced, not stacked");
    const char t[] = "Saved";
    s.ask("note", rolltui_dialog_new(ROLLTUI_MESSAGE_OK, 0, t, sizeof t - 1, nullptr, 0));
    s.paint();
    check(s.has("[ OK ]") && rolltui_window_stack_depth(s.stack) == 3, "a dialog with no buttons has one, OK, and stacks over another");
    s.key(ROLLTUI_KEY_ENTER);
    check(s.answer("note", &b) == ROLLTUI_DIALOG_ANSWERED && b == 0 && s.answer("del", &b) == 0 && rolltui_window_stack_depth(s.stack) == 2,
          "…answered, the one under it is open still");
  }
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    // freed with a dialog open: nothing is left behind (LeakSanitizer)
  }
}

void test_phrase() {
  int b = -9;
  Screen s(60, 20);
  s.ask("wipe", delete_three("notes"));
  s.paint();
  check(s.has("Type notes to confirm"), "a phrase says what to type");
  const int y = s.row_of("[ Delete ]");
  const int dx = s.col_of(y, "[ Delete ]");
  check(s.cell(dx + 2, y).style.dim && !s.cell(dx + 2, y).style.underline, "…the buttons but cancel inert until it is typed, no letter offered");
  s.key(ROLLTUI_KEY_ENTER);
  check(s.answer("wipe", &b) == 0, "Enter on an inert button answers nothing");
  s.type("notez");
  s.key(ROLLTUI_KEY_BACKSPACE);
  s.chr('c'); /* a letter is typed, not Cancel's answer */
  check(s.answer("wipe", &b) == 0, "…letters are typed into the field, not answers");
  s.key(ROLLTUI_KEY_BACKSPACE);
  s.type("s");
  s.paint();
  check(s.has("notes") && !s.cell(dx + 2, y).style.dim && s.cur_vis && s.cur_y == y - 2,
        "typed exactly: the button answers, and the caret is in the field [" + std::to_string(s.cur_y) + "]");
  s.key(ROLLTUI_KEY_ENTER);
  check(s.answer("wipe", &b) == ROLLTUI_DIALOG_ANSWERED && b == 0, "…Enter answers with it");

  s.ask("wipe", delete_three("notes"));
  RolltuiEvent paste{};
  paste.kind = ROLLTUI_EVENT_PASTE;
  paste.text = "no\ntes";
  paste.text_len = 6;
  s.send(paste);
  s.key(ROLLTUI_KEY_ENTER);
  check(s.answer("wipe", &b) == ROLLTUI_DIALOG_ANSWERED && b == 0, "a paste is typed, its newline dropped");
}

void test_copy_and_scroll() {
  int b = -9;
  std::string copied;
  {
    Screen s(60, 20);
    s.ask("del", delete_three());
    s.chr('c', true); /* alt+c, `dialog.copy` */
    check(s.answer("del", &b, &copied) == ROLLTUI_DIALOG_COPY &&
              copied == "Delete 3 files?\n\nThey go to the Trash, where they can be put back.\n\nnotes.txt\ndraft.md\nold photo.jpg",
          "`dialog.copy` hands the host the title, the body and the items [" + copied + "]");
    check(s.answer("del", &b) == 0 && rolltui_window_stack_depth(s.stack) == 2, "…and the dialog stays open");
  }
  {
    Screen s(60, 15);
    const char t[] = "Remove 30 tags?";
    RolltuiDialog* d = rolltui_dialog_new(ROLLTUI_MESSAGE_ERROR, 0, t, sizeof t - 1, nullptr, 0);
    for (int i = 1; i <= 30; ++i) {
      const std::string it = "tag-" + std::to_string(i);
      rolltui_dialog_add_item(d, it.data(), it.size());
    }
    s.ask("tags", d);
    s.paint();
    const int top = s.row_of("Remove 30 tags?"), bottom = s.row_of("[ OK ]") + 1;
    check(bottom - top + 1 <= 15 * 9 / 10 && s.has("tag-1") && !s.has("tag-30") && s.has("of 30"),
          "more items than fit: the dialog stops at nine tenths of the screen and says which it shows [" + s.rows[static_cast<std::size_t>(bottom)] + "]");
    s.key(ROLLTUI_KEY_DOWN);
    s.paint();
    check(!s.has("tag-1 ") && s.has("tag-2") && s.has("2\xE2\x80\x93"), "Down scrolls the items a row");
    s.key(ROLLTUI_KEY_PAGEDOWN);
    s.key(ROLLTUI_KEY_PAGEDOWN);
    s.key(ROLLTUI_KEY_PAGEDOWN);
    s.paint();
    check(s.has("tag-30") && s.has("of 30"), "PageDown goes a page at a time, and stops at the last");
  }
  {
    Screen s(60, 15);
    const char t[] = "The configuration could not be read";
    const char body[] = "line 3: an unknown key \"colour\"";
    s.ask("err", rolltui_dialog_new(ROLLTUI_MESSAGE_ERROR, ROLLTUI_DIALOG_FULL_SCREEN, t, sizeof t - 1, body, sizeof body - 1));
    s.paint();
    check(s.row_of("The configuration could not be read") == 0 && s.row_of("[ OK ]") == 13, "FULL_SCREEN fills the screen: an error page");
  }
}

void test_every_theme_tells_the_states_apart() {
  for (const char* theme : {"default-dark", "default-light", "mono"}) {
    Screen s(60, 20);
    s.theme = theme;
    s.ask("wipe", delete_three("x"));
    s.type("y");
    s.paint();
    const int y = s.row_of("[ Delete ]");
    const RolltuiStyle chosen = s.cell(s.col_of(y, "[ Delete ]") + 2, y).style, other = s.cell(s.col_of(y, "[ Cancel ]") + 2, y).style;
    check(chosen.dim && !(chosen == other), std::string(theme) + ": an inert chosen button is dim, and drawn apart from the others");
    s.key(ROLLTUI_KEY_BACKSPACE);
    s.type("x");
    s.paint();
    const RolltuiStyle live = s.cell(s.col_of(y, "[ Delete ]") + 2, y).style;
    check(live.bold && !live.dim && !(live == other), std::string(theme) + ": the chosen button is bold, apart from the others");
  }
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_shown();
  test_answers();
  test_unasked();
  test_phrase();
  test_copy_and_scroll();
  test_every_theme_tells_the_states_apart();
  return report("dialog_test");
}
