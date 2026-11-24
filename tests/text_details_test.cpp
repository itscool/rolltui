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
#include "rolltui/c/rolltui_widget_input.h"
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
                               const char* focus = nullptr, RolltuiWindowStack* held = nullptr) {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  RolltuiWindowStack* stack = held ? held : rolltui_window_stack_new();
  if (!held) rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
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
  if (!held) rolltui_window_stack_free(stack);
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
  RolltuiLayout* l = load(R"({ "resizable": false, "name": "f", "root": { "row": [
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
  // A TAB'S FOOTER IS ITS OWN: shown in place of the list, a page with nothing to say leaves the border bare.
  l = load(R"({ "resizable": false, "name": "t", "root": { "id": "main", "focusable": true, "border": "single",
      "tabs": [ { "title": "Rows", "content": "list:things" }, { "title": "Note", "content": "text:a note" } ] } })");
  w = rolltui_windows_new(ctx());
  rolltui_windows_bind_list(w, "things", 6, count_fn, row_fn, nullptr, nullptr);
  RolltuiWindowStack* tabs = rolltui_window_stack_new();
  rolltui_window_stack_set_base(tabs, rolltui_layout_base(l));
  rows = paint(w, l, 40, 8, nullptr, nullptr, tabs);
  const bool listed = rows[7].find("1 of 100") != std::string::npos;
  rolltui_window_stack_set_tab(tabs, "main", 4, 1);
  rows = paint(w, l, 40, 8, nullptr, nullptr, tabs);
  check(listed && rows[7].find(" of ") == std::string::npos, "the list's footer goes with the list when another tab is shown [" + rows[7] + "]");
  rolltui_window_stack_free(tabs);
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

// ---- an underline's shapes and colour, blink, hidden and overline -------------------------------------------------------------------

std::string sgr_of(const RolltuiStyle& st, unsigned char depth) {
  char b[ROLLTUI_SGR_MAX];
  const std::size_t n = rolltui_sgr(&st, depth, b, sizeof b);
  return std::string(b, n);
}

void test_attributes() {
  RolltuiStyle curl{};
  curl.underline = ROLLTUI_UNDERLINE_CURLY;
  curl.underline_color.kind = RolltuiStyleColorRaw::Kind::Rgb;
  curl.underline_color.r = 255;
  const unsigned char ext = ROLLTUI_DEPTH_TRUECOLOR | ROLLTUI_SGR_EXTENDED;
  check(sgr_of(curl, ROLLTUI_DEPTH_TRUECOLOR) == "\x1b[0;4m", "a curly underline is a plain one where the terminal draws no shapes");
  check(sgr_of(curl, ext) == "\x1b[0;4:3;58:2::255:0:0m", "…and 4:3 in its own colour, 58, where it does");
  check(sgr_of(curl, ROLLTUI_DEPTH_MONO | ROLLTUI_SGR_EXTENDED) == "\x1b[0;4:3m", "at mono, the shape and no colour");
  RolltuiStyle other{};
  other.blink = 1;
  other.hidden = 1;
  other.overline = 1;
  check(sgr_of(other, ROLLTUI_DEPTH_TRUECOLOR) == "\x1b[0;5;8;53m", "blink is 5, hidden 8, overline 53");
  other.blink = 2;
  check(sgr_of(other, ROLLTUI_DEPTH_TRUECOLOR).find(";6") != std::string::npos, "a rapid blink is 6");
  char b[ROLLTUI_SGR_MAX];
  RolltuiStyle plain{};
  std::size_t d = rolltui_sgr_delta(&curl, &plain, ext, b, sizeof b);
  check(std::string(b, d) == "\x1b[24;59m" || std::string(b, d) == "\x1b[0m", "the underline and its colour go by 24 and 59, or a reset");
  RolltuiStyle single = curl;
  single.underline = ROLLTUI_UNDERLINE_SINGLE;
  d = rolltui_sgr_delta(&curl, &single, ext, b, sizeof b);
  check(std::string(b, d) == "\x1b[4m", "a curly underline made plain sends 4 alone [" + std::string(b, d) + "]");
  d = rolltui_sgr_delta(&curl, &single, ROLLTUI_DEPTH_TRUECOLOR, b, sizeof b);
  check(d == 0, "…and where shapes are not drawn the two are the same, and nothing is sent");
  RolltuiStyle longest{};
  longest.bold = longest.dim = longest.italic = longest.reverse = longest.strike = longest.hidden = longest.overline = 1;
  longest.blink = 2;
  longest.underline = ROLLTUI_UNDERLINE_DASHED;
  for (RolltuiStyleColorRaw* c : {&longest.fg, &longest.bg, &longest.underline_color}) {
    c->kind = RolltuiStyleColorRaw::Kind::Rgb;
    c->r = c->g = c->b = 255;
  }
  check(sgr_of(longest, ext).size() == 77, "the longest sequence is 77 bytes, inside ROLLTUI_SGR_MAX");

  // ANOTHER PROGRAM'S: the shapes, the colour, blink, hidden and overline are read
  RolltuiAnsiText* at = rolltui_ansi_new();
  static const char kRich[] = "\x1b[4:3;58:2::1:2:3mA\x1b[4:0;59;5mB\x1b[25;8mC\x1b[28;53mD\x1b[55;21mE\x1b[0mF\n";
  rolltui_ansi_feed(at, kRich, sizeof kRich - 1);
  RolltuiFrame* f = rolltui_frame_new(8, 1, plain);
  rolltui_ansi_draw(at, f, RolltuiRect{0, 0, 8, 1}, 0, 0, 0, plain, 0);
  RolltuiCellRaw c[6]{};
  for (int x = 0; x < 6; ++x) rolltui_frame_cell(f, x, 0, &c[x]);
  check(c[0].style.underline == ROLLTUI_UNDERLINE_CURLY && c[0].style.underline_color.r == 1 && c[0].style.underline_color.b == 3,
        "another program's 4:3 and 58 are a curly underline in its colour");
  check(!c[1].style.underline && c[1].style.blink == 1 && c[1].style.underline_color.kind == RolltuiStyleColorRaw::Kind::None, "4:0 ends it, 59 drops its colour, 5 blinks");
  check(!c[2].style.blink && c[2].style.hidden, "25 ends the blink, 8 hides");
  check(!c[3].style.hidden && c[3].style.overline, "28 shows, 53 draws a line over");
  check(!c[4].style.overline && c[4].style.underline == ROLLTUI_UNDERLINE_DOUBLE, "55 ends it, 21 is a double underline");
  check(!c[5].style.underline && !c[5].style.blink, "and 0 resets every one");
  rolltui_frame_free(f);
  rolltui_ansi_free(at);

  // A THEME FILE names a shape, an underline's colour, blink and overline, and writes them back the same
  static const char kTheme[] = R"({ "name": "t", "roles": { "error": { "fg": "#ff0000", "underline": "curly", "underline_color": "#00ff00",
    "blink": true, "overline": true }, "text": { "underline": { "dark": "dotted", "light": true } } } })";
  RolltuiStr err{};
  RolltuiJsonValue* root = rolltui_json_parse(kTheme, sizeof kTheme - 1, &err);
  rolltui_str_free(&err);
  RolltuiStyle dark[ROLLTUI_ROLE_COUNT]{}, light[ROLLTUI_ROLE_COUNT]{};
  RolltuiStr name{};
  RolltuiThemeReport rep{};
  RolltuiEffectMap* e1 = rolltui_theme_load(root, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), dark, &name, &rep);
  check(rep.bad_values_n == 0, "the theme's attributes are all read without complaint");
  rolltui_theme_report_release(&rep);
  RolltuiThemeReport rep2{};
  RolltuiEffectMap* e2 = rolltui_theme_load(root, ROLLTUI_MODE_LIGHT, rolltui_theme_default_vocab(), light, &name, &rep2);
  rolltui_theme_report_release(&rep2);
  const RolltuiStyle& er = dark[ROLLTUI_ROLE_ERROR];
  check(er.underline == ROLLTUI_UNDERLINE_CURLY && er.underline_color.g == 255 && er.blink && er.overline,
        "a role's underline shape, its colour, blink and overline");
  check(dark[ROLLTUI_ROLE_TEXT].underline == ROLLTUI_UNDERLINE_DOTTED && light[ROLLTUI_ROLE_TEXT].underline == ROLLTUI_UNDERLINE_SINGLE,
        "…per variant: dotted in the dark, plain in the light");
  RolltuiJsonValue* back = rolltui_theme_dump(dark, e1, light, e2, rolltui_theme_default_vocab());
  RolltuiStr text{};
  rolltui_json_dump(back, 0, &text);
  const std::string out = str_of(text);
  check(out.find("\"underline\":\"curly\"") != std::string::npos && out.find("\"underline_color\":\"#00ff00\"") != std::string::npos &&
            out.find("\"blink\":true") != std::string::npos && out.find("\"overline\":true") != std::string::npos,
        "…and written back the same way");
  check(out.find("\"dark\":\"dotted\"") != std::string::npos, "…a shape per variant too");
  rolltui_str_free(&text);
  rolltui_json_free(back);
  rolltui_json_free(root);
  rolltui_str_free(&name);
  rolltui_effect_map_free(e1);
  rolltui_effect_map_free(e2);

  // THE PATCH: a shape is replaced, never OR'd into another
  RolltuiStyle base{}, over{};
  base.underline = ROLLTUI_UNDERLINE_CURLY;
  base.bold = 1;
  over.underline = ROLLTUI_UNDERLINE_DOUBLE;
  over.overline = 1;
  rolltui_style_patch(&base, &over);
  check(base.underline == ROLLTUI_UNDERLINE_DOUBLE && base.bold && base.overline, "a patched underline is the patch's shape; the rest add");
  RolltuiStyle none{};
  rolltui_style_patch(&base, &none);
  check(base.underline == ROLLTUI_UNDERLINE_DOUBLE, "…and a patch with none leaves the shape as it was");

  // WHICH TERMINALS draw the shapes, by name
  RolltuiTermEnv env{};
  env.term = "xterm-kitty";
  check(rolltui_termfacts_underline_shapes(&env, "xterm-kitty"), "kitty draws the shapes");
  env.term = "xterm-256color";
  check(!rolltui_termfacts_underline_shapes(&env, "XTerm(380)"), "xterm does not");
  check(rolltui_termfacts_underline_shapes(&env, "WezTerm 20240203"), "what the terminal answered to XTVERSION decides");
  env.vte_version = "7600";
  check(rolltui_termfacts_underline_shapes(&env, "xterm-256color"), "VTE from 0.52 does");
  env.tmux = "/tmp/tmux-501/default,1,0";
  check(!rolltui_termfacts_underline_shapes(&env, "kitty(0.32)"), "behind tmux, plain underlines");
}

// ---- cuts with their own rules, shortened paths, a line of spans, a hotkey's letter -------------------------------------------------

std::string row_text(const RolltuiFrame* f, int y, int w) {
  std::string out;
  for (int x = 0; x < w; ++x) {
    RolltuiCellRaw c{};
    rolltui_frame_cell(f, x, y, &c);
    if (c.continuation) continue;
    out.append(c.bytes, c.len);
  }
  return out;
}

void test_text_rules() {
  RolltuiDrawScratch* ds = ds2();
  RolltuiStr out{};
  RolltuiCutOptions co{};
  co.keep_extension = 1;
  rolltui_text_cut_with(ds, "a_long_report_name.pdf", 22, 14, &co, 0, &out);
  check(str_of(out) == "a_long_re\xE2\x80\xA6.pdf", "a name cut at its end keeps its extension [" + str_of(out) + "]");
  co.where = ROLLTUI_CUT_MIDDLE;
  rolltui_text_cut_with(ds, "a_long_report_name.pdf", 22, 14, &co, 0, &out);
  check(str_of(out) == "a_lon\xE2\x80\xA6name.pdf", "…and in its middle, the head given the odd cell [" + str_of(out) + "]");
  co = RolltuiCutOptions{};
  co.where = ROLLTUI_CUT_MIDDLE;
  co.split = 25;
  co.marker = "~";
  rolltui_text_cut_with(ds, "abcdefghijklmnopqrst", 20, 9, &co, 0, &out);
  check(str_of(out) == "ab~opqrst", "a marker of its own, and a split that gives the head a quarter of the room [" + str_of(out) + "]");
  co = RolltuiCutOptions{};
  rolltui_text_cut_with(ds, ".bashrc", 7, 20, &co, 0, &out);
  check(str_of(out) == ".bashrc", "text that fits is left whole");
  co.keep_extension = 1;
  rolltui_text_cut_with(ds, ".bashrc_with_a_long_tail", 24, 10, &co, 0, &out);
  check(str_of(out).size() > 0 && str_of(out).find(".bashrc") == 0, "a name that starts with its dot has no extension to keep [" + str_of(out) + "]");
  rolltui_path_shorten(ds, "~/git/rolltui/c/frame.c", 23, 15, 0, &out);
  check(str_of(out) == "~/g/r/c/frame.c", "a path shortens each folder to its first letter [" + str_of(out) + "]");
  rolltui_path_shorten(ds, "~/git/rolltui/c/frame.c", 23, 19, 0, &out);
  check(str_of(out) == "~/g/rolltui/c/frame.c" || str_of(out) == "~/g/r/c/frame.c", "…from the left, only as far as it must [" + str_of(out) + "]");
  rolltui_path_shorten(ds, "/home/me/.config/app/settings.json", 34, 23, 0, &out);
  check(str_of(out) == "/h/m/.c/a/settings.json", "…a leading dot kept [" + str_of(out) + "]");
  rolltui_path_shorten(ds, "/a/b/a_very_long_file_name.txt", 30, 16, 0, &out);
  check(str_of(out).find("/a/b/") == 0 && str_of(out).size() > 4 && str_of(out).substr(str_of(out).size() - 4) == ".txt",
        "…and then the name cut in its middle, its extension kept [" + str_of(out) + "]");
  rolltui_str_free(&out);

  RolltuiStyle plain{}, bold{};
  bold.bold = 1;
  RolltuiFrame* f = rolltui_frame_new(12, 3, plain);
  RolltuiTextSpan spans[] = {{"one ", 4, plain}, {"two", 3, bold}};
  check(rolltui_frame_put_spans(f, ds, RolltuiRect{0, 0, 12, 1}, spans, 2, ROLLTUI_ALIGN_RIGHT, ROLLTUI_OVERFLOW_CLIP, 0) == 1 &&
            row_text(f, 0, 12) == "     one two",
        "spans at the right of their row");
  RolltuiCellRaw cell{};
  rolltui_frame_cell(f, 9, 0, &cell);
  check(cell.style.bold, "…each in its own style");
  rolltui_frame_put_spans(f, ds, RolltuiRect{0, 1, 12, 1}, spans, 2, ROLLTUI_ALIGN_CENTER, ROLLTUI_OVERFLOW_CLIP, 0);
  check(row_text(f, 1, 12) == "  one two   ", "…in its middle [" + row_text(f, 1, 12) + "]");
  rolltui_frame_free(f);
  f = rolltui_frame_new(6, 3, plain);
  check(rolltui_frame_put_spans(f, ds, RolltuiRect{0, 0, 6, 1}, spans, 2, ROLLTUI_ALIGN_LEFT, ROLLTUI_OVERFLOW_ELLIPSIS, 0) == 1 &&
            row_text(f, 0, 6) == "one t\xE2\x80\xA6",
        "wider than its row: cut with an ellipsis [" + row_text(f, 0, 6) + "]");
  rolltui_frame_free(f);
  f = rolltui_frame_new(6, 3, plain);
  check(rolltui_frame_put_spans(f, ds, RolltuiRect{0, 0, 6, 3}, spans, 2, ROLLTUI_ALIGN_LEFT, ROLLTUI_OVERFLOW_WRAP, 0) == 2 &&
            row_text(f, 0, 6).find("one") == 0 && row_text(f, 1, 6).find("two") == 0,
        "…or wrapped at the word onto the next row");
  rolltui_frame_cell(f, 0, 1, &cell);
  check(cell.style.bold, "…the wrapped word still in its span's style");
  rolltui_frame_free(f);

  std::size_t n = 0;
  check(rolltui_hotkey_at("Save as", 7, 'a', &n) == 5 && n == 1, "a hotkey's letter: the first word that starts with it");
  check(rolltui_hotkey_at("Exit", 4, 'x', &n) == 1, "…else the first place it is at all");
  check(rolltui_hotkey_at("Open", 4, 'O', &n) == 0, "…in either case");
  check(rolltui_hotkey_at("Open", 4, 'z', &n) == static_cast<std::size_t>(-1), "…and none when the label has no such letter");

  RolltuiAnsiText* at = rolltui_ansi_new();
  rolltui_ansi_set_tab_width(at, 4);
  rolltui_ansi_feed(at, "a\tb\n", 4);
  std::size_t ln = 0;
  const char* line = rolltui_ansi_line(at, 0, &ln);
  check(std::string(line, ln) == "a   b", "another program's tab reaches the tab stop set for it [" + std::string(line, ln) + "]");
  rolltui_ansi_free(at);
}

// ---- the Linux console's glyphs, the cursor's shape -------------------------------------------------------------------------------

void test_console_and_caret() {
  RolltuiStyle plain{};
  RolltuiFrame* f = rolltui_frame_new(8, 1, plain);
  rolltui_frame_put_text(f, ds2(), 0, 0, "\xE2\x95\xAD\xE2\x94\x81\xE2\xA3\xBF\xE2\x96\x9F\xE2\x80\xA6\xE2\x9C\x93" "a",
                         std::strlen("\xE2\x95\xAD\xE2\x94\x81\xE2\xA3\xBF\xE2\x96\x9F\xE2\x80\xA6\xE2\x9C\x93" "a"), plain, 8, 0, 0);
  RolltuiStr bytes{};
  rolltui_render_full(f, ROLLTUI_DEPTH_ANSI16 | ROLLTUI_RENDER_CONSOLE, &bytes);
  std::string out = str_of(bytes);
  check(out.find("\xE2\x94\x8C\xE2\x95\x90\xE2\x96\x88\xE2\x96\x88~\xE2\x88\x9A" "a") != std::string::npos,
        "on the Linux console: a rounded corner square, a heavy line double, full braille and a quadrant solid, the ellipsis ~, a tick √");
  rolltui_str_clear(&bytes);
  rolltui_render_full(f, ROLLTUI_DEPTH_ANSI16, &bytes);
  check(str_of(bytes).find("\xE2\x95\xAD\xE2\x94\x81\xE2\xA3\xBF") != std::string::npos, "…and anywhere else, each as it is");
  rolltui_str_clear(&bytes);
  rolltui_frame_clear(f, plain);
  static const char kDots[] = "\xE2\xA0\x81\xE2\xA0\x83\xE2\xA0\x87"; /* one dot, two, three */
  rolltui_frame_put_text(f, ds2(), 0, 0, kDots, sizeof kDots - 1, plain, 8, 0, 0);
  rolltui_render_full(f, ROLLTUI_DEPTH_ANSI16 | ROLLTUI_RENDER_CONSOLE, &bytes);
  check(str_of(bytes).find("\xE2\x96\x91\xE2\x96\x91\xE2\x96\x92") != std::string::npos, "braille is a shade by how many of its dots are set");
  rolltui_str_clear(&bytes);
  // THE CURSOR'S SHAPE: sent where the terminal takes it, again only when it changes, and put back to the terminal's own
  RolltuiFrame* g = rolltui_frame_new(8, 1, plain);
  rolltui_frame_set_cursor(g, 2, 0, 1);
  rolltui_frame_set_cursor_shape(g, ROLLTUI_CARET_BAR);
  rolltui_render_full(g, ROLLTUI_DEPTH_TRUECOLOR | ROLLTUI_RENDER_CARET, &bytes);
  check(str_of(bytes).find("\x1b[6 q") != std::string::npos, "a bar cursor is CSI 6 SP q");
  rolltui_str_clear(&bytes);
  rolltui_render_full(g, ROLLTUI_DEPTH_TRUECOLOR, &bytes);
  check(str_of(bytes).find(" q") == std::string::npos, "…never sent where the terminal does not take it");
  rolltui_str_clear(&bytes);
  RolltuiFrame* h = rolltui_frame_clone(g);
  rolltui_render_diff(g, h, ROLLTUI_DEPTH_TRUECOLOR | ROLLTUI_RENDER_CARET, &bytes);
  check(str_of(bytes).find(" q") == std::string::npos, "…not sent again while it stays");
  rolltui_frame_set_cursor_shape(h, ROLLTUI_CARET_DEFAULT);
  rolltui_render_diff(g, h, ROLLTUI_DEPTH_TRUECOLOR | ROLLTUI_RENDER_CARET, &bytes);
  check(str_of(bytes).find("\x1b[0 q") != std::string::npos, "…and the terminal's own put back when no field asks for one");
  rolltui_str_free(&bytes);
  rolltui_frame_free(f);
  rolltui_frame_free(g);
  rolltui_frame_free(h);
  // THE INPUT'S CARET by its mode: a bar while keys type, a block while they are actions
  RolltuiInput* in = rolltui_input_new();
  rolltui_input_layout(in, RolltuiRect{0, 0, 10, 1});
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiInputRoles roles{};
  RolltuiFrame* c = rolltui_frame_new(10, 1, plain);
  rolltui_input_draw(in, c, ds2(), styles, &roles, 1);
  check(rolltui_frame_cursor_shape(c) == ROLLTUI_CARET_BAR, "a field that types draws a bar caret");
  RolltuiInputOptions o;
  rolltui_input_options_copy(&o, rolltui_input_options(in));
  o.insert_typed = 0;
  rolltui_input_set_options(in, &o);
  rolltui_frame_set_cursor_shape(c, 0);
  rolltui_input_draw(in, c, ds2(), styles, &roles, 1);
  check(rolltui_frame_cursor_shape(c) == ROLLTUI_CARET_BLOCK, "…a field whose keys are actions, a block");
  o.caret = ROLLTUI_CARET_UNDERLINE_BLINK;
  rolltui_input_set_options(in, &o);
  rolltui_input_draw(in, c, ds2(), styles, &roles, 1);
  check(rolltui_frame_cursor_shape(c) == ROLLTUI_CARET_UNDERLINE_BLINK, "…and a host's own choice over either");
  rolltui_frame_free(c);
  rolltui_input_free(in);
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
  RolltuiLayout* l = load(R"({ "resizable": false, "name": "a", "root": { "id": "rows", "content": "list:things", "border": "single", "focusable": true } })");
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
  // A WINDOW'S OWN BACKGROUND IS ITS GROUND: a list in a panel draws its rows on the panel, not on the screen behind it.
  RolltuiLayout* panel = load(R"({ "resizable": false, "name": "p", "root": { "id": "rows", "content": "list:things", "focusable": true,
      "background": "panel_background" } })");
  RolltuiWindows* w3 = rolltui_windows_new(ctx());
  rolltui_windows_bind_list(w3, "things", 6, count_fn, row_fn, nullptr, nullptr);
  RolltuiFrame* pf = nullptr;
  rows = paint(w3, panel, 30, 6, &pf, "rows");
  check(style_at(pf, 4, 3).bg == styles[ROLLTUI_ROLE_PANEL_BACKGROUND].bg &&
            !(styles[ROLLTUI_ROLE_PANEL_BACKGROUND].bg == styles[ROLLTUI_ROLE_BACKGROUND].bg),
        "a list in a window with a panel background draws its rows on the panel [" + rows[3] + "]");
  rolltui_frame_free(pf);
  rolltui_windows_free(w3);
  rolltui_layout_free(panel);
  rolltui_draw_scratch_free(ds);
  rolltui_menu_free(m);
  rolltui_menu_item_release(&root);
  rolltui_menu_load_report_release(&rep);
  rolltui_effect_map_free(fx);
}

}  // namespace

// NUMBERS FOR A PERSON: a size in steps of 1024 or 1000, bits, a rate; a count with its thousands grouped
void test_numbers() {
  auto size = [](double n, unsigned flags) {
    char b[32];
    const std::size_t len = rolltui_format_size(n, flags, b, sizeof b);
    return std::string(b, len);
  };
  check(size(0, 0) == "0" && size(512, 0) == "512" && size(512, ROLLTUI_SIZE_BYTE_UNIT) == "512 B",
        "under the first step, the number, with its unit where asked [" + size(512, ROLLTUI_SIZE_BYTE_UNIT) + "]");
  check(size(1536, 0) == "1.5K" && size(10 * 1024, 0) == "10K" && size(5.0 * 1099511627776.0, 0) == "5.0T" &&
            size(3.0 * 1152921504606846976.0, 0) == "3.0E",
        "steps of 1024: one decimal under 10, none from 10, as far as E [" + size(1536, 0) + " " + size(10 * 1024, 0) + "]");
  check(size(10199, 0) == "10K" && size(1024.0 * 1023.9, 0) == "1.0M", "a value that rounds up is written in the step it rounds to [" + size(10199, 0) + " " + size(1024.0 * 1023.9, 0) + "]");
  check(size(1500, ROLLTUI_SIZE_DECIMAL) == "1.5k" && size(2.5e9, ROLLTUI_SIZE_DECIMAL) == "2.5G", "DECIMAL: steps of 1000, a lower-case k");
  check(size(125000, ROLLTUI_SIZE_DECIMAL | ROLLTUI_SIZE_BITS | ROLLTUI_SIZE_RATE) == "1.0Mb/s" && size(1500, ROLLTUI_SIZE_BITS) == "12Kb",
        "BITS: eight a byte, b after; RATE: /s [" + size(125000, ROLLTUI_SIZE_DECIMAL | ROLLTUI_SIZE_BITS | ROLLTUI_SIZE_RATE) + "]");
  check(size(0.5, ROLLTUI_SIZE_RATE | ROLLTUI_SIZE_BYTE_UNIT) == "0.5 B/s" && size(-1536, 0) == "-1.5K", "a fraction under the first step, and a negative");
  char tiny[4];
  check(rolltui_format_size(123456789, 0, tiny, sizeof tiny) == 3 && tiny[3] == 0, "a small buffer is cut and still terminated");
  auto count = [](long long n, const char* sep) {
    char b[48];
    const std::size_t len = rolltui_format_count(n, sep, sep ? std::strlen(sep) : 0, b, sizeof b);
    return std::string(b, len);
  };
  check(count(0, nullptr) == "0" && count(999, nullptr) == "999" && count(1000, nullptr) == "1,000" && count(12345678, nullptr) == "12,345,678",
        "a count's thousands grouped by a comma [" + count(12345678, nullptr) + "]");
  check(count(-1234567, nullptr) == "-1,234,567" && count(1234, "\xE2\x80\x89") == "1\xE2\x80\x89" "234" &&
            count(-9223372036854775807LL - 1, ".") == "-9.223.372.036.854.775.808",
        "…a negative, another separator, and the smallest there is");
}

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_cut();
  test_footers();
  test_strike();
  test_attributes();
  test_text_rules();
  test_console_and_caret();
  test_away();
  test_numbers();
  return testkit::report("text_details_test");
}
