// viewer_test.cpp — A PAGE OF TEXT, WRAPPED OR KEPT WHOLE: `view.wrap` turns wrapping off and on in the `text`, `file` and `output`
// windows, and `view.left`, `view.right` and the wheel's sideways turns scroll lines kept whole. Each case composes a real screen and
// reads the frame back as text.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h"  /* INTERNAL: a cell's link and style, read back */
#include "rolltui/c/rolltui_widgets.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read the binding back */
#include "rolltui_test.hpp"

using namespace testkit;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

struct Screen {
  RolltuiLayout* layout = nullptr;
  RolltuiWindows* w = nullptr;
  RolltuiWindowStack* stack = nullptr;
  int width, height;
  std::vector<std::string> rows;
  std::vector<std::string> links;     /* each cell's link, row by row: "" for none */
  std::vector<unsigned char> under;   /* …and whether it is underlined */
  Screen(std::string_view screen, int wd, int ht) : width(wd), height(ht) {
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &nd);
    layout = rolltui_load_layout_text(screen.data(), screen.size(), defaults, nd, rolltui_layout_default_hooks(), &rep);
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
  void paint() {
    RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
    const RolltuiRect area{0, 0, width, height};
    rolltui_windows_sync(w, stack);
    rolltui_windows_autosize(w, stack, area);
    rolltui_windows_layout(w, stack, area);
    RolltuiFrame* f = rolltui_frame_new(width, height, styles[0]);
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
    rows.clear();
    std::size_t at = 0;
    while (at < all.size()) {
      const std::size_t nl = all.find('\n', at);
      rows.push_back(all.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
      if (nl == std::string::npos) break;
      at = nl + 1;
    }
    rows.resize(static_cast<std::size_t>(height));
    links.clear();
    under.clear();
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        RolltuiCellRaw c;
        rolltui_frame_cell(f, x, y, &c);
        std::size_t n = 0;
        const char* u = rolltui_frame_link(f, c.link, &n);
        links.emplace_back(u, n);
        under.push_back(c.style.underline);
      }
    rolltui_compose_scratch_free(cs);
    rolltui_frame_free(f);
  }
  const std::string& link(int x, int y) const { return links[static_cast<std::size_t>(y * width + x)]; }
  bool underlined(int x, int y) const { return under[static_cast<std::size_t>(y * width + x)] != 0; }
  // As a host does it: routed through the stack (which keeps where the pointer is), then handed to the window the route names.
  int send(const RolltuiEvent& e) {
    RolltuiStr window{};
    const unsigned char kind = rolltui_window_stack_route(stack, &e, RolltuiRect{0, 0, width, height}, rolltui_bindings_default(ctx()),
                                                          rolltui_stack_default_actions(), &window);
    int took = 0;
    if (kind == ROLLTUI_ROUTE_DELIVER) took = rolltui_windows_handle(w, window.p ? window.p : "", window.n, &e);
    rolltui_str_free(&window);
    return took;
  }
  void key(unsigned char k, char32_t ch = 0, bool alt = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    e.key.ch = ch;
    e.key.alt = alt ? 1 : 0;
    rolltui_windows_handle(w, "page", 4, &e);
  }
  void wrap_key() { key(ROLLTUI_KEY_CHAR, 'w', true); }
  void wheel(bool right) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.kind = static_cast<RolltuiMouseEvent::Kind>(right ? 7 : 6);
    rolltui_windows_handle(w, "page", 4, &e);
  }
  std::string row(int y) const {
    std::string r = rows[static_cast<std::size_t>(y)];
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
  }
};

constexpr const char* kLong = "alpha beta gamma delta epsilon zeta";

void check_page(Screen& s, const std::string& what) {
  s.paint();
  check(s.row(0) == "alpha beta" && s.row(1) == "gamma" && s.row(2) == "delta", what + ": wrapped by default [" + s.row(0) + "|" + s.row(1) + "]");
  RolltuiChord altw{};
  altw.key = ROLLTUI_KEY_CHAR;
  altw.ch = 'w';
  altw.alt = 1;
  rolltui_window_stack_focus(s.stack, "page", 4);
  rolltui_windows_sync(s.w, s.stack);
  check(rolltui_windows_focused_takes(s.w, &altw) == 1, what + ": the wrap key is the window's own, which nothing around it takes");
  s.wrap_key();
  s.paint();
  check(s.row(0) == "alpha beta" && s.row(1) == "second" && s.row(2).empty(), what + ": Alt-W keeps each line whole, cut at the edge [" + s.row(0) + "|" + s.row(1) + "]");
  s.key(ROLLTUI_KEY_RIGHT);
  s.paint();
  check(s.row(0) == "a beta gam", what + ": Right scrolls it four cells [" + s.row(0) + "]");
  s.wheel(true);
  s.paint();
  check(s.row(0) == "ta gamma d", what + ": …and the wheel's sideways turn four more [" + s.row(0) + "]");
  for (int i = 0; i < 20; ++i) s.key(ROLLTUI_KEY_RIGHT);
  s.paint();
  check(s.row(0) == "silon zeta", what + ": …as far as the widest line's end [" + s.row(0) + "]");
  s.key(ROLLTUI_KEY_LEFT);
  s.paint();
  check(s.row(0) == "a epsilon", what + ": Left scrolls back [" + s.row(0) + "]");
  s.wrap_key();
  s.paint();
  check(s.row(0) == "alpha beta" && s.row(1) == "gamma", what + ": Alt-W again wraps, from the left edge");
}

void test_text() {
  const std::string screen = std::string(R"({ "resizable": false, "name": "t", "root": { "id": "page", "content": "text:)") + kLong + R"(\nsecond", "focusable": true } })";
  Screen s(screen, 10, 6);
  check_page(s, "a text window");
}

void test_file() {
  char dir[] = "/tmp/rolltui_viewer_XXXXXX";
  if (!mkdtemp(dir)) {
    check(false, "a scratch directory");
    return;
  }
  const std::string path = std::string(dir) + "/page.txt";
  if (FILE* f = std::fopen(path.c_str(), "w")) {
    std::fprintf(f, "%s\nsecond\n", kLong);
    std::fclose(f);
  }
  {
    const std::string screen = R"({ "resizable": false, "name": "f", "root": { "id": "page", "content": "file:)" + path + R"(", "focusable": true } })";
    Screen s(screen, 10, 6);
    check_page(s, "a file window");
  }
  std::remove(path.c_str());
  rmdir(dir);
}

void test_wide_glyph_at_the_edge() {
  // "a中文字": four cells in, 文 would be cut in half, so its cell left of the edge is blank and 字 follows
  const std::string screen = R"({ "resizable": false, "name": "t", "root": { "id": "page", "content": "text:a中文字 and more", "focusable": true } })";
  Screen s(screen, 10, 3);
  s.paint();
  s.wrap_key();
  s.key(ROLLTUI_KEY_RIGHT); /* straight after: no frame has measured the lines kept whole yet */
  s.paint();
  check(s.row(0).rfind(" \xe5\xad\x97 and", 0) == 0, "a wide glyph the edge would cut is a blank, and the next one follows whole [" + s.row(0) + "]");
}

void test_output() {
  constexpr std::string_view screen = R"({ "resizable": false, "name": "o", "root": { "id": "page", "content": "output:run", "focusable": true } })";
  Screen s(screen, 10, 6);
  RolltuiAnsiText* t = rolltui_ansi_new();
  const std::string bytes = std::string(kLong) + "\nsecond\n";
  rolltui_ansi_feed(t, bytes.data(), bytes.size());
  rolltui_windows_bind_output(s.w, "run", 3, t);
  rolltui_windows_set_output_wrap(s.w, "run", 3, 0);
  s.paint();
  check(s.row(0) == "alpha beta" && s.row(1) == "second", "an output window the host keeps whole [" + s.row(0) + "|" + s.row(1) + "]");
  s.key(ROLLTUI_KEY_RIGHT);
  s.paint();
  check(s.row(0) == "a beta gam", "…Right scrolls it four cells [" + s.row(0) + "]");
  s.wrap_key();
  s.paint();
  check(s.row(0) == "alpha beta" && s.row(1) == " gamma del", "…Alt-W wraps it, by cells as a program's output wraps [" + s.row(0) + "|" + s.row(1) + "]");
  int wrap = -1;
  rolltui_windows_output(s.w, "run", 3, &wrap);
  check(wrap == 1, "…and the binding says so, so the host's own call and the key agree");
  rolltui_windows_bind_output(s.w, "run", 3, nullptr);
  rolltui_ansi_free(t);
}

// LINKS ANOTHER PROGRAM WROTE (OSC 8): read onto the text they cover, drawn as the frame's links (sent on as OSC 8), the one under the
// pointer underlined, and a press on one offered to the host before the library opens it.
void test_links() {
  constexpr std::string_view screen = R"({ "resizable": false, "name": "o", "root": { "id": "page", "content": "output:run", "focusable": true } })";
  Screen s(screen, 30, 5);
  RolltuiAnsiText* t = rolltui_ansi_new();
  // ST and BEL ends, parameters, a link kept across a newline, a title OSC swallowed, a URL that is not one, and an OSC past 4 KB
  const std::string bytes = std::string("see \x1b]8;;https://example.test/a\x1b\\the docs\x1b]8;;\x1b\\ now\n") +
                            "\x1b]0;a title\x07\x1b]8;id=7;lazygit-edit://x\x07two\nthree\x1b]8;;\x07 four\n" +
                            "\x1b]8;;bad\x01url\x1b\\none\x1b]8;;\x1b\\\n" + "\x1b]8;;https://" + std::string(5000, 'x') + "\x1b\\long\n";
  const std::size_t half = bytes.find("https") + 5; /* the stream split inside a URL */
  rolltui_ansi_feed(t, bytes.data(), half);
  rolltui_ansi_feed(t, bytes.data() + half, bytes.size() - half);
  rolltui_windows_bind_output(s.w, "run", 3, t);
  rolltui_windows_set_output_wrap(s.w, "run", 3, 0);
  s.paint();
  check(s.rows[0].rfind("see the docs now", 0) == 0 && s.rows[1].rfind("two", 0) == 0 && s.rows[2].rfind("three four", 0) == 0 &&
            s.rows[3].rfind("none", 0) == 0 && s.rows[4].rfind("long", 0) == 0,
        "a program's links leave its text as it was, and every other OSC is swallowed [" + s.rows[0] + "|" + s.rows[1] + "]");
  check(s.link(4, 0) == "https://example.test/a" && s.link(11, 0) == "https://example.test/a" && s.link(3, 0).empty() &&
            s.link(12, 0).empty(),
        "an OSC 8 link covers the text from its opening to its closing, split across two feeds [" + s.link(4, 0) + "]");
  check(s.link(0, 1) == "lazygit-edit://x" && s.link(4, 2) == "lazygit-edit://x" && s.link(6, 2).empty(),
        "…ended by BEL as well as ST, with its parameters, and kept across a line as a terminal keeps it");
  check(s.link(0, 3).empty() && s.link(0, 4).empty(), "a URL that is not printable ASCII, or an OSC past 4 KB, is no link");
  check(!s.underlined(4, 0), "a link the pointer is not on is drawn as the program drew it");

  // THE POINTER on a link underlines it, all of it
  RolltuiEvent move{};
  move.kind = ROLLTUI_EVENT_MOUSE;
  move.mouse.kind = RolltuiMouseEvent::Kind::Move;
  move.mouse.x = 6;
  move.mouse.y = 0;
  s.send(move);
  s.paint();
  check(s.underlined(4, 0) && s.underlined(11, 0) && !s.underlined(3, 0) && !s.underlined(0, 1),
        "the link under the pointer is underlined across its cells, and nothing else");

  // A PRESS: the host's first, then the library's for a scheme it opens
  struct Seen {
    std::string window, url;
    int take = 1;
  } seen;
  rolltui_windows_set_link_handler(
      s.w,
      [](void* c, const char* win, std::size_t wn, const char* url, std::size_t un) {
        Seen* z = static_cast<Seen*>(c);
        z->window.assign(win, wn);
        z->url.assign(url, un);
        return z->take;
      },
      &seen);
  RolltuiEvent press{};
  press.kind = ROLLTUI_EVENT_MOUSE;
  press.mouse.kind = RolltuiMouseEvent::Kind::Press;
  press.mouse.button = 1;
  press.mouse.x = 8;
  press.mouse.y = 0;
  check(s.send(press) == 1 && seen.window == "page" && seen.url == "https://example.test/a",
        "a press on a link is offered to the host with the window and the URL, and taken [" + seen.url + "]");
  seen = Seen{};
  seen.take = 0;
  press.mouse.x = 1;
  press.mouse.y = 1;
  RolltuiEvent plain = press;
  plain.mouse.x = 20;
  const int plain_took = s.send(plain);
  check(s.send(press) == plain_took && seen.url == "lazygit-edit://x",
        "a scheme the library does not open, which the host left, is a press like any other [" + seen.url + "]");
  seen = Seen{};
  press.mouse.button = 3;
  press.mouse.x = 8;
  press.mouse.y = 0;
  s.send(press);
  check(seen.url.empty(), "…and only the left button opens one");
  rolltui_windows_bind_output(s.w, "run", 3, nullptr);
  rolltui_ansi_free(t);
}

// THE WHEEL'S STEP IS THE PERSON'S (`RolltuiWidgetEnv.wheel_lines`): three lines a notch, or what it says, in every window that
// scrolls by lines.
void test_wheel_step() {
  std::string lines;
  for (int i = 1; i <= 30; ++i) lines += std::string(i < 10 ? "l0" : "l") + std::to_string(i) + "\\n";
  const std::string screen = R"({ "resizable": false, "name": "t", "root": { "id": "page", "content": "text:)" + lines + R"(", "focusable": true } })";
  Screen s(screen, 10, 5);
  s.paint();
  RolltuiEvent down{};
  down.kind = ROLLTUI_EVENT_MOUSE;
  down.mouse.kind = static_cast<RolltuiMouseEvent::Kind>(5);
  rolltui_windows_handle(s.w, "page", 4, &down);
  s.paint();
  check(s.row(0) == "l04", "a notch of the wheel: three lines [" + s.row(0) + "]");
  RolltuiWidgetEnv env{};
  env.wheel_lines = 1;
  rolltui_context_set_env(ctx(), &env);
  rolltui_windows_handle(s.w, "page", 4, &down);
  s.paint();
  check(s.row(0) == "l05", "…or the person's step: one [" + s.row(0) + "]");
  env.wheel_lines = 0;
  rolltui_context_set_env(ctx(), &env);
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_text();
  test_file();
  test_wide_glyph_at_the_edge();
  test_output();
  test_links();
  test_wheel_step();
  return report("viewer_test");
}
