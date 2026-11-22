//
// image_test.cpp — PICTURES: decoded with limits, fitted, drawn in blocks or as kitty placeholders, and sent at present the way
// each protocol wants: kitty's pixels once and its placement when its size changes; sixel and iTerm2 when their cells are whole,
// not while something covers them, and erased when they go. How a terminal draws them is found from its name and its DA reply.
//
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_image.h"     /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_preview.h"
#include "rolltui/c/rolltui_render.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/unicode_tables.h"
#include "rolltui_test.hpp"

using testkit::check;

namespace {

// A PNM is the one format a test can write by hand: a header and the bytes.
std::string ppm(int w, int h, const std::vector<unsigned char>& rgb) {
  std::string s = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
  s.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
  return s;
}

RolltuiImage* decode(const std::string& bytes, std::string* why = nullptr) {
  RolltuiStr err;
  RolltuiImage* img = rolltui_image_decode(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), &err);
  if (why) *why = std::string(err.data(), err.size());
  return img;
}

// Red, green / blue, white.
RolltuiImage* four() {
  return decode(ppm(2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255}));
}

struct Frame {
  RolltuiFrame* f;
  Frame(int w, int h) : f(rolltui_frame_new(w, h, RolltuiStyle{})) {}
  Frame(const Frame&) = delete;
  ~Frame() { rolltui_frame_free(f); }
  std::string glyph(int x, int y) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_glyph(f, x, y, &n);
    return std::string(p, n);
  }
  RolltuiCell cell(int x, int y) const {
    RolltuiCell c{};
    rolltui_frame_cell(f, x, y, &c);
    return c;
  }
};

bool rgb_is(const RolltuiStyleColorRaw& c, int r, int g, int b) { return static_cast<int>(c.kind) == 2 && c.r == r && c.g == g && c.b == b; }

void test_decode() {
  RolltuiStr err;
  RolltuiImage* blob = rolltui_image_load(ROLLTUI_FIXTURE_DIR "/blob.png", std::strlen(ROLLTUI_FIXTURE_DIR "/blob.png"), &err);
  int w = 0, h = 0;
  check(blob && rolltui_image_rgba(blob, &w, &h) && w > 0 && h > 0, "a PNG loads from a path [" + std::to_string(w) + "x" + std::to_string(h) + "]");
  rolltui_image_free(blob);
  std::string why;
  check(!decode("not a picture at all", &why) && why.find("not a picture this library reads") == 0, "garbage is refused, by name");
  check(!decode("P6\n20000 20000\n255\n", &why) && why == "too large to decode: 20000x20000",
        "a picture larger than the cap is refused before it is decoded [" + why + "]");
  RolltuiImage* img = four();
  const unsigned char* px = rolltui_image_rgba(img, &w, &h);
  check(w == 2 && h == 2 && px[0] == 255 && px[1] == 0 && px[3] == 255 && px[8] == 0 && px[10] == 255,
        "decoded to RGBA, row by row, opaque");
  rolltui_image_free(img);
  const unsigned char rgba[4] = {1, 2, 3, 4};
  RolltuiImage* one = rolltui_image_from_rgba(rgba, 1, 1);
  check(one && rolltui_image_rgba(one, &w, &h)[3] == 4, "from RGBA, copied");
  check(rolltui_image_from_rgba(rgba, 0, 1) == nullptr, "…and a size a picture may not have is refused");
  rolltui_image_free(one);
}

void test_fit() {
  const std::vector<unsigned char> px(100 * 50 * 3, 128);
  RolltuiImage* img = decode(ppm(100, 50, px));
  int uc = 0, ur = 0, pw = 0, ph = 0;
  rolltui_image_fit(img, 20, 20, 10, 20, &uc, &ur, &pw, &ph);
  check(pw == 100 && ph == 50 && uc == 10 && ur == 3, "a picture smaller than the box keeps its size: 100x50 px in 10x3 cells of 10x20");
  rolltui_image_fit(img, 5, 20, 10, 20, &uc, &ur, &pw, &ph);
  check(pw == 50 && ph == 25 && uc == 5 && ur == 2, "…and shrinks to the width, its aspect kept");
  rolltui_image_free(img);
}

void test_blocks() {
  rolltui_termfacts_set_active_images(0, 0, 0);
  RolltuiImage* img = four();
  Frame f(10, 4);
  RolltuiStyle ground{};
  ground.bg.kind = decltype(ground.bg.kind)(2);
  RolltuiRect used{};
  rolltui_frame_image(f.f, RolltuiRect{0, 0, 10, 4}, img, ground, &used);
  check(used.w == 2 && used.h == 1, "with no terminal entered, blocks: two pixels a cell, the shape kept [" + std::to_string(used.w) + "x" +
                                        std::to_string(used.h) + "]");
  check(f.glyph(0, 0) == "\xE2\x96\x80" && rgb_is(f.cell(0, 0).style.fg, 255, 0, 0) && rgb_is(f.cell(0, 0).style.bg, 0, 0, 255) &&
            rgb_is(f.cell(1, 0).style.fg, 0, 255, 0) && rgb_is(f.cell(1, 0).style.bg, 255, 255, 255),
        "the upper half block: its foreground the top pixel, its background the bottom one");
  check(rolltui_frame_image_count(f.f) == 0, "…and nothing for the terminal to send");
  rolltui_image_free(img);
  const unsigned char clear[8] = {255, 0, 0, 255, 0, 0, 0, 0};
  RolltuiImage* half = rolltui_image_from_rgba(clear, 1, 2);
  Frame g(4, 2);
  RolltuiStyle gr{};
  gr.bg.kind = decltype(gr.bg.kind)(2);
  gr.bg.r = 10;
  rolltui_frame_image(g.f, RolltuiRect{0, 0, 4, 2}, half, gr, nullptr);
  check(rgb_is(g.cell(0, 0).style.bg, 10, 0, 0), "a transparent pixel shows the ground");
  rolltui_image_free(half);
}

void test_kitty_cells() {
  rolltui_termfacts_set_active_images(ROLLTUI_IMAGES_KITTY, 10, 20);
  const std::vector<unsigned char> px(20 * 20 * 3, 200);
  RolltuiImage* img = decode(ppm(20, 20, px));
  const unsigned id = rolltui_image_id(img);
  Frame f(10, 4);
  RolltuiRect used{};
  rolltui_frame_image(f.f, RolltuiRect{0, 0, 10, 4}, img, RolltuiStyle{}, &used);
  check(used.w == 2 && used.h == 1, "kitty: 20x20 px in cells of 10x20 is 2x1");
  // U+10EEEE, then row 0's diacritic (U+0305), then column 1's (U+030D)
  check(f.glyph(1, 0) == "\xF4\x8E\xBB\xAE\xCC\x85\xCC\x8D", "a placeholder cell names its row and column with diacritics");
  check(rgb_is(f.cell(1, 0).style.fg, (id >> 16) & 255, (id >> 8) & 255, id & 255), "…its foreground the picture's id");
  check(rolltui_frame_image_count(f.f) == 1, "…and the picture is recorded for the terminal to send");
  rolltui_image_free(img);
  bool ok = rolltui_image_diacritic_count() == 297;
  for (std::size_t i = 0; i < rolltui_image_diacritic_count(); ++i) {
    const unsigned cp = rolltui_image_diacritic(i);
    ok = ok && rolltui_u_combining_class(cp) == 230 && rolltui_u_general_category(cp) == ROLLTUI_GENERALCATEGORY_Mn &&
         (i == 0 || cp > rolltui_image_diacritic(i - 1));
  }
  check(ok, "kitty's 297 diacritics are each a nonspacing mark of combining class 230, in order");
  rolltui_termfacts_set_active_images(0, 0, 0);
}

void test_detection() {
  auto pick = [](const char* term, const char* prog, const char* tmux, const char* kitty, const char* force) {
    RolltuiTermEnv e{};
    e.term = term;
    e.term_program = prog;
    e.tmux = tmux;
    e.kitty_window_id = kitty;
    e.force_images = force;
    RolltuiTermFacts f{};
    rolltui_termfacts_from_env(&e, &f);
    return static_cast<int>(f.images);
  };
  check(pick("xterm-kitty", nullptr, nullptr, nullptr, nullptr) == ROLLTUI_IMAGES_KITTY, "kitty draws with placeholders");
  check(pick("xterm-ghostty", "ghostty", nullptr, nullptr, nullptr) == ROLLTUI_IMAGES_KITTY, "…and so does Ghostty");
  check(pick("xterm-256color", "iTerm.app", nullptr, nullptr, nullptr) == ROLLTUI_IMAGES_ITERM2, "iTerm2 draws with OSC 1337");
  check(pick("foot", nullptr, nullptr, nullptr, nullptr) == ROLLTUI_IMAGES_SIXEL, "foot draws sixel");
  check(pick("xterm-256color", nullptr, nullptr, nullptr, nullptr) == ROLLTUI_IMAGES_BLOCKS, "a terminal nothing names draws blocks");
  check(pick("tmux-256color", "tmux", "/tmp/tmux,1,0", nullptr, nullptr) == ROLLTUI_IMAGES_BLOCKS, "inside tmux, blocks");
  check(pick("tmux-256color", "tmux", "/tmp/tmux,1,0", "3", nullptr) == ROLLTUI_IMAGES_KITTY, "…unless the window is kitty's");
  check(pick("xterm-kitty", nullptr, nullptr, nullptr, "sixel") == ROLLTUI_IMAGES_SIXEL, "ROLLTUI_IMAGES chooses outright");
  auto pick_in = [](const char* sty, const char* zellij, const char* nvim) {
    RolltuiTermEnv e{};
    e.term = "xterm-kitty";
    e.kitty_window_id = "3";
    e.sty = sty;
    e.zellij = zellij;
    e.nvim = nvim;
    RolltuiTermFacts f{};
    rolltui_termfacts_from_env(&e, &f);
    return static_cast<int>(f.images);
  };
  check(pick_in("1234.pts-0.host", nullptr, nullptr) == ROLLTUI_IMAGES_BLOCKS, "inside GNU screen in a kitty window, blocks: screen passes no tmux wrapper");
  check(pick_in(nullptr, "0", nullptr) == ROLLTUI_IMAGES_BLOCKS, "…inside Zellij, blocks");
  check(pick_in(nullptr, nullptr, "/tmp/nvim.sock") == ROLLTUI_IMAGES_BLOCKS, "…and inside Neovim's terminal, blocks, whatever it inherited");
  RolltuiTermReplies r{};
  char da[] = "\x1b[?62;4;22c";
  rolltui_termreplies_take(da, std::strlen(da), 0, &r);
  RolltuiTermFacts f{};
  RolltuiTermEnv e{};
  e.term = "xterm-256color";
  rolltui_termfacts_from_env(&e, &f);
  rolltui_termfacts_apply_replies(&f, &r, ROLLTUI_TERMQ_ALL);
  check(r.da1_sixel && f.images == ROLLTUI_IMAGES_SIXEL && f.images_source == ROLLTUI_FACT_PROBE,
        "a terminal whose Primary DA names attribute 4 draws sixel");
}

// A terminal on a pty that draws pictures `how`, and what it is sent. A thread reads the master all along, as a terminal would:
// a picture's bytes are more than a pty holds, and a write nobody reads waits forever.
struct Pty {
  int master = -1, slave = -1;
  RolltuiTerminal* t = nullptr;
  RolltuiSwap* swap = nullptr;
  std::mutex mu;
  std::string got;
  std::atomic<bool> stop{false};
  std::thread reader;
  explicit Pty(const char* how, bool tmux = false, int delay_ms = -1) { /* -1: sent at once, as most checks here want */
    winsize ws{};
    ws.ws_row = 12;
    ws.ws_col = 40;
    ws.ws_xpixel = 400;
    ws.ws_ypixel = 240;
    openpty(&master, &slave, nullptr, nullptr, &ws);
    reader = std::thread([this] {
      while (!stop) {
        pollfd p{master, POLLIN, 0};
        if (::poll(&p, 1, 20) <= 0) continue;
        char buf[65536];
        const ssize_t n = ::read(master, buf, sizeof buf);
        if (n <= 0) continue;
        std::lock_guard<std::mutex> g(mu);
        got.append(buf, static_cast<std::size_t>(n));
      }
    });
    setenv("ROLLTUI_IMAGES", how, 1);
    if (tmux) setenv("TMUX", "/tmp/tmux-test,1,0", 1);
    if (tmux) setenv("KITTY_WINDOW_ID", "1", 1);
    RolltuiTerminalOptions o{};
    o.alt_screen = 0;
    o.mouse = 0;
    o.bracketed_paste = 0;
    o.no_probe = 1;
    o.no_cache = 1;
    o.picture_delay_ms = delay_ms;
    t = rolltui_terminal_new(slave, slave, o);
    unsetenv("ROLLTUI_IMAGES");
    unsetenv("TMUX");
    unsetenv("KITTY_WINDOW_ID");
    swap = rolltui_swap_new(40, 12, RolltuiStyle{});
    read();
  }
  Pty(const Pty&) = delete;
  ~Pty() {
    rolltui_swap_free(swap);
    if (t) rolltui_terminal_free(t);
    stop = true;
    reader.join();
    ::close(master);
    ::close(slave);
  }
  // What arrived since the last read, once the line has been quiet for a moment.
  std::string read(int ms = 150) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::lock_guard<std::mutex> g(mu);
    std::string out;
    out.swap(got);
    return out;
  }
  // One frame: the picture at (x, y) in a 10x4 area, and optionally a cell drawn over it.
  std::string present(const RolltuiImage* img, int x, int y, bool cover = false) {
    RolltuiFrame* f = rolltui_swap_begin(swap, 40, 12, RolltuiStyle{});
    if (img) rolltui_frame_image(f, RolltuiRect{x, y, 10, 4}, img, RolltuiStyle{}, nullptr);
    if (cover) rolltui_frame_put(f, x, y, "X", 1, 1, RolltuiStyle{}, 0);
    RolltuiStr scratch;
    rolltui_terminal_present(t, swap, &scratch);
    return read();
  }
};

std::string b64_decode(const std::string& in) {
  static const std::string kAlpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, bits = -8;
  for (char c : in) {
    const std::size_t k = kAlpha.find(c);
    if (k == std::string::npos) break;
    val = (val << 6) + static_cast<int>(k);
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

bool has(const std::string& s, std::string_view n) { return s.find(n) != std::string::npos; }

void test_present() {
  const std::vector<unsigned char> px(20 * 20 * 3, 90);
  RolltuiImage* img = decode(ppm(20, 20, px));
  const std::string id = std::to_string(rolltui_image_id(img));
  const char* ssh_was = getenv("SSH_CONNECTION"); /* (a test run over ssh, as a VM's shell is, says where kitty is) */
  const std::string ssh_saved = ssh_was ? ssh_was : "";
  unsetenv("SSH_CONNECTION");
  {
    Pty k("kitty");
    const std::string first = k.present(img, 1, 1);
    check(has(first, "\x1b_Ga=t,t=t,f=32,s=20,v=20,i=" + id + ",q=2;") && has(first, "\x1b_Ga=p,U=1,i=" + id + ",c=2,r=1,q=2\x1b\\"),
          "kitty: the pixels go once, then the placement the cells name");
    std::string sent_file;
    {
      const std::size_t a = first.find(",q=2;", first.find("t=t")) + 5, b = first.find("\x1b\\", a);
      sent_file = b64_decode(first.substr(a, b - a));
      struct stat sb {};
      check(stat(sent_file.c_str(), &sb) == 0 && sb.st_size == 20 * 20 * 4 && sent_file.find("tty-graphics-protocol") != std::string::npos,
            "…by a temp file on this machine, which kitty reads and deletes [" + sent_file + "]");
    }
    check(first.find("\x1b_Ga=t") < first.find("\xF4\x8E\xBB\xAE"), "…before the placeholder cells");
    const std::string again = k.present(img, 1, 1);
    check(!has(again, "\x1b_G"), "the same picture again sends nothing more");
    // THE TERMINAL KEEPS ONLY THE LAST FEW: eight more pictures, each drawn once, and the first is deleted from it.
    std::vector<RolltuiImage*> more;
    std::string sent;
    for (int n = 0; n < 8; ++n) {
      more.push_back(decode(ppm(20, 20, std::vector<unsigned char>(20 * 20 * 3, static_cast<unsigned char>(10 + n)))));
      sent += k.present(more.back(), 1, 1);
    }
    check(has(sent, "\x1b_Ga=d,d=I,i=" + id + ",q=2\x1b\\") &&
              !has(sent, "\x1b_Ga=d,d=I,i=" + std::to_string(rolltui_image_id(more.back())) + ","),
          "past eight pictures sent, the least recently drawn is deleted from the terminal, and the one on screen is not");
    const std::string back = k.present(img, 1, 1);
    check(has(back, "\x1b_Ga=t,t=t,f=32,s=20,v=20,i=" + id + ","), "…and drawn again it is sent again");
    rolltui_terminal_free(k.t);
    k.t = nullptr;
    check(has(k.read(), "\x1b_Ga=d,d=I,i=" + id + ",q=2\x1b\\"), "leaving deletes what was sent");
    struct stat gone {};
    check(stat(sent_file.c_str(), &gone) != 0, "…and the temp files a terminal did not take are removed");
    for (RolltuiImage* m : more) rolltui_image_free(m);
  }
  {
    // A PICTURE HELD: nothing is sent until it has stayed in its cells, and one that goes first is never sent
    Pty k("sixel", false, 80);
    const std::string first = k.present(img, 1, 1, false);
    check(!has(first, "\x1bP0;1;0q") && rolltui_terminal_pending_ms(k.t) > 0, "a new picture is held, and the terminal says when it is due");
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(120 * rolltui_test::kClockScale)));
    const std::string second = k.present(img, 1, 1, false);
    check(has(second, "\x1bP0;1;0q") && rolltui_terminal_pending_ms(k.t) < 0, "…and sent once it has stayed that long");
    RolltuiImage* passing = decode(ppm(20, 20, std::vector<unsigned char>(20 * 20 * 3, 200)));
    k.present(passing, 12, 1, false);
    const std::string gone = k.present(nullptr, 0, 0, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(120 * rolltui_test::kClockScale)));
    const std::string later = k.present(nullptr, 0, 0, false);
    check(!has(gone, "\x1bP0;1;0q") && !has(later, "\x1bP0;1;0q") && rolltui_terminal_pending_ms(k.t) < 0,
          "a picture the cursor passed, gone before it was due, is never sent");
    rolltui_image_free(passing);
  }
  {
    // OVER SSH the pixels go in the escapes: a temp file here is no use to a kitty on another machine
    setenv("SSH_CONNECTION", "10.0.0.1 5000 10.0.0.2 22", 1);
    Pty k("kitty");
    if (ssh_was) setenv("SSH_CONNECTION", ssh_saved.c_str(), 1);
    else unsetenv("SSH_CONNECTION");
    const std::string first = k.present(img, 1, 1);
    check(has(first, "\x1b_Ga=t,f=32,s=20,v=20,i=" + id + ",q=2,m=0;") && !has(first, "t=t"), "over ssh, kitty's pixels go in the escapes");
  }
  {
    Pty k("kitty", /*tmux=*/true);
    const std::string first = k.present(img, 1, 1);
    check(has(first, "\x1bPtmux;\x1b\x1b_Ga=t"), "inside tmux it is passed through, its ESC doubled");
  }
  {
    Pty s("sixel");
    const std::string first = s.present(img, 2, 3);
    check(has(first, "\x1b" "7\x1b[4;3H\x1bP0;1;0q\"1;1;20;20") && has(first, "\x1b\\\x1b" "8"),
          "sixel: drawn at its cell, the cursor kept, after the diff");
    check(!has(s.present(img, 2, 3), "\x1bP"), "…not again while it is still there");
    check(!has(s.present(img, 2, 3, /*cover=*/true), "\x1bP"), "…nor while something is drawn over it");
    check(has(s.present(img, 2, 3), "\x1bP0;1;0q"), "…and again once what covered it has gone");
    const std::string gone = s.present(nullptr, 0, 0);
    check(!has(gone, "\x1bP") && has(gone, "\x1b[4;3H"), "when it goes, its cells are written again over it");
  }
  {
    Pty i("iterm2");
    const std::string first = i.present(img, 0, 0);
    check(has(first, "\x1b]1337;File=inline=1;size=") && has(first, ";width=2;height=1;preserveAspectRatio=1:"),
          "iTerm2: OSC 1337 with the picture's own size in cells");
  }
  rolltui_image_free(img);
}

void test_preview_and_window() {
  rolltui_termfacts_set_active_images(0, 0, 0);
  char tmpl[] = "/tmp/rolltui_image_XXXXXX";
  const std::string dir = mkdtemp(tmpl);
  const std::string path = dir + "/four.ppm";
  {
    std::ofstream out(path, std::ios::binary);
    out << ppm(2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
  }
  RolltuiPreview* pv = rolltui_preview_new();
  rolltui_preview_set_path(pv, path.data(), path.size());
  check(rolltui_preview_kind(pv) == ROLLTUI_PREVIEW_IMAGE, "the preview knows a picture and decodes it");
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  Frame f(44, 8);
  rolltui_preview_draw(pv, f.f, RolltuiRect{0, 0, 44, 8}, styles, 0, 0);
  std::string head;
  for (int x = 0; x < 44; ++x) head += f.glyph(x, 0);
  check(head.find("picture \xC2\xB7 2x2") != std::string::npos && f.glyph(1, 1) == "\xE2\x96\x80",
        "…its head says so, and the body draws it [" + head + "]");
  rolltui_preview_free(pv);
  // An `image:` window reads the file its source names, relative to the preset folder.
  RolltuiContext* ctx = rolltui_test::test_context();
  rolltui_context_set_library_defaults(ctx);
  RolltuiLayoutReport rep{};
  std::size_t nd = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &nd);
  static const char kScreen[] = R"({ "name": "i", "root": { "column": [ { "id": "pic", "content": "image:four.ppm" } ] } })";
  RolltuiLayout* l = rolltui_load_layout_text(kScreen, sizeof kScreen - 1, defaults, nd, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  RolltuiWindows* w = rolltui_windows_new(ctx);
  rolltui_context_set_dir(ctx, dir.data(), dir.size());
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
  auto paint = [&](Frame& out) {
    const RolltuiRect area{0, 0, 10, 4};
    RolltuiWidgetEnv env{};
    rolltui_context_set_env(ctx, &env);
    rolltui_windows_sync(w, stack);
    rolltui_windows_layout(w, stack, area);
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    struct Slot {
      RolltuiWindows* w;
      RolltuiStyle* s;
    } slot{w, styles};
    rolltui_window_stack_compose(
        stack, out.f, area, styles, rolltui_layout_default_roles(),
        [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
          Slot* sl = static_cast<Slot*>(c);
          rolltui_windows_draw(sl->w, rn, fr, sl->s, rolltui_windows_default_roles());
        },
        &slot, 0, cs);
    rolltui_compose_scratch_free(cs);
  };
  Frame g(10, 4);
  paint(g);
  check(g.glyph(4, 1) == "\xE2\x96\x80" && rgb_is(g.cell(4, 1).style.fg, 255, 0, 0), "an image: window shows the file, centred");
  const unsigned char white[4] = {255, 255, 255, 255};
  RolltuiImage* bound = rolltui_image_from_rgba(white, 1, 1);
  rolltui_windows_bind_image(w, "four.ppm", 8, bound);
  Frame b(10, 4);
  paint(b);
  check(rgb_is(b.cell(4, 1).style.fg, 255, 255, 255) || rgb_is(b.cell(4, 2).style.fg, 255, 255, 255),
        "…and a picture the host bound under that name instead");
  rolltui_windows_bind_image(w, "four.ppm", 8, nullptr);
  rolltui_image_free(bound);
  {
    // WITH WORKERS LENT, the file is read and decoded on them: the frame draws what it has meanwhile, and the picture once it is back
    RolltuiWindows* w2 = rolltui_windows_new(ctx);
    RolltuiTasks* tasks = rolltui_tasks_new(1, nullptr, nullptr);
    rolltui_windows_set_tasks(w2, tasks);
    std::swap(w, w2);
    Frame first(10, 4);
    paint(first);
    rolltui_tasks_wait_idle(tasks, static_cast<int>(5000 * rolltui_test::kClockScale));
    Frame later(10, 4);
    paint(later);
    check(first.glyph(4, 1) != "\xE2\x96\x80" && later.glyph(4, 1) == "\xE2\x96\x80" && rgb_is(later.cell(4, 1).style.fg, 255, 0, 0),
          "with workers lent, an image: window decodes off the drawing thread and shows the picture once it is back");
    std::swap(w, w2);
    rolltui_windows_set_tasks(w2, nullptr);
    rolltui_windows_free(w2);
    rolltui_tasks_free(tasks);
  }
  rolltui_window_stack_free(stack);
  rolltui_windows_free(w);
  rolltui_layout_free(l);
  rolltui_effect_map_free(fx);
  rolltui_context_set_dir(ctx, "", 0);
  std::remove(path.c_str());
  rmdir(dir.c_str());
}

}  // namespace

// A PHOTO TURNED AS ITS CAMERA WAS HELD; A PNG FOR ITERM2 THAT DECODES BACK; A PALETTE FITTED; A FILTER CHOSEN

void test_orientation_png_palette_filter() {
  std::ifstream in(ROLLTUI_FIXTURE_DIR "/halves.jpg", std::ios::binary);
  const std::string jpeg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  RolltuiStr err{};
  RolltuiImage* plain = rolltui_image_decode(reinterpret_cast<const unsigned char*>(jpeg.data()), jpeg.size(), &err);
  int w = 0, h = 0;
  const unsigned char* px = plain ? rolltui_image_rgba(plain, &w, &h) : nullptr;
  check(px && w == 16 && h == 8 && px[(4 * 16 + 2) * 4] < 60 && px[(4 * 16 + 13) * 4] > 200, "(a JPEG, dark on its left, light on its right)");
  // orientation 6, said in an EXIF block put first: "turn it a quarter clockwise to show it"
  std::string exif;
  exif += "\xFF\xE1";
  exif += std::string("\x00\x22", 2);                         /* 34 bytes: the length, then what follows */
  exif += std::string("Exif\0\0", 6);
  exif += std::string("II*\0\x08\0\0\0", 8);                  /* little-endian TIFF, its first IFD at 8 */
  exif += std::string("\x01\0", 2);                            /* one entry */
  exif += std::string("\x12\x01\x03\0\x01\0\0\0\x06\0\0\0", 12); /* 0x0112 orientation, a SHORT: 6 */
  exif += std::string("\0\0\0\0", 4);                         /* no next IFD */
  const std::string turned_bytes = jpeg.substr(0, 2) + exif + jpeg.substr(2);
  RolltuiImage* turned = rolltui_image_decode(reinterpret_cast<const unsigned char*>(turned_bytes.data()), turned_bytes.size(), &err);
  px = turned ? rolltui_image_rgba(turned, &w, &h) : nullptr;
  check(px && w == 8 && h == 16 && px[(2 * 8 + 4) * 4] < 60 && px[(13 * 8 + 4) * 4] > 200,
        "a JPEG whose EXIF says it was taken sideways is shown upright: a quarter turn, its dark side now on top [" + std::to_string(w) + "x" +
            std::to_string(h) + "]");
  rolltui_image_free(turned);
  rolltui_image_free(plain);
  rolltui_str_free(&err);
  // A HOST'S PIXELS TO ITERM2: a PNG, alpha kept, that decodes back to them
  const unsigned char rgba[16] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 10, 20, 30, 255};
  RolltuiImage* own = rolltui_image_from_rgba(rgba, 2, 2);
  RolltuiStr seq{};
  rolltui_image_iterm2(own, 2, 1, 0, &seq);
  const std::string osc(seq.p ? seq.p : "", seq.n);
  const std::size_t colon = osc.find(':'), bel = osc.rfind('\x07');
  const std::string file = colon != std::string::npos && bel != std::string::npos ? b64_decode(osc.substr(colon + 1, bel - colon - 1)) : "";
  RolltuiImage* back = rolltui_image_decode(reinterpret_cast<const unsigned char*>(file.data()), file.size(), nullptr);
  const unsigned char* bp = back ? rolltui_image_rgba(back, &w, &h) : nullptr;
  check(file.compare(0, 4, "\x89PNG") == 0 && bp && w == 2 && h == 2 && std::memcmp(bp, rgba, sizeof rgba) == 0,
        "a host's own pixels go to iTerm2 as a PNG that decodes back to them, alpha and all");
  rolltui_image_free(back);
  rolltui_str_free(&seq);
  rolltui_image_free(own);
  // A PALETTE FITTED: a picture of two colours has those two registers, exactly
  std::vector<unsigned char> two(12 * 12 * 4);
  for (int i = 0; i < 12 * 12; ++i) {
    const bool left = (i % 12) < 6;
    two[i * 4 + 0] = left ? 10 : 250;
    two[i * 4 + 1] = left ? 200 : 250;
    two[i * 4 + 2] = left ? 30 : 250;
    two[i * 4 + 3] = 255;
  }
  RolltuiImage* pair = rolltui_image_from_rgba(two.data(), 12, 12);
  RolltuiStr six{};
  rolltui_image_sixel(pair, 12, 12, &six);
  const std::string sx(six.p ? six.p : "", six.n);
  std::size_t regs = 0;
  for (std::size_t at = sx.find(";2;"); at != std::string::npos; at = sx.find(";2;", at + 1)) ++regs;
  check(regs == 2 && sx.find(";2;4;78;12") != std::string::npos && sx.find(";2;98;98;98") != std::string::npos,
        "a sixel's palette is fitted to its picture: two colours, two registers, each the colour itself [" + std::to_string(regs) + "]");
  rolltui_str_free(&six);
  rolltui_image_free(pair);
  // A FILTER CHOSEN: four stripes made two pixels, averaged or each the pixel at its centre
  const unsigned char stripes[16] = {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255};
  RolltuiImage* st = rolltui_image_from_rgba(stripes, 4, 1);
  unsigned char out[8];
  rolltui_image_scale(st, 2, 1, out);
  check(out[0] > 100 && out[0] < 160, "shrunk smoothly, the stripes average to grey");
  rolltui_image_set_filter(st, ROLLTUI_IMAGE_NEAREST);
  rolltui_image_scale(st, 2, 1, out);
  check(out[0] == 255 && out[4] == 255, "…and with NEAREST each pixel is the one at its centre, hard-edged");
  rolltui_image_free(st);
}

// AN OUTSIDE PROGRAM'S PICTURE: the host is told to draw it once its cells are blank on the screen, after the frame is written, and
// to clear it before the frame that writes over it; the cursor is put back after a draw. The host's function writes a mark of what
// it was told into the terminal's stream, so the order against the frame's own bytes is seen.
struct OutsideLog {
  RolltuiTerminal* t = nullptr;
  std::vector<std::string> told;
};
void outside_told(void* user, int what, unsigned int id, RolltuiRect r) {
  auto* log = static_cast<OutsideLog*>(user);
  const std::string e = std::string(what == ROLLTUI_OUTSIDE_DRAW ? "draw " : what == ROLLTUI_OUTSIDE_CLEAR ? "clear " : "? ") +
                        std::to_string(id) + " " + std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" +
                        std::to_string(r.h);
  log->told.push_back(e);
  const std::string mark = "<" + e + ">";
  rolltui_terminal_write(log->t, mark.data(), mark.size());
}
std::string told_since(const OutsideLog& log, std::size_t from) {
  std::string out;
  for (std::size_t i = from; i < log.told.size(); ++i) out += (out.empty() ? "" : " | ") + log.told[i];
  return out;
}

void test_outside_pictures() {
  const RolltuiRect at{2, 1, 5, 3};
  const RolltuiRect moved{3, 1, 5, 3};
  struct Shot { RolltuiRect r; unsigned int id; };
  auto frame = [](Pty& k, std::vector<Shot> shots, const char* cover_at = nullptr) {
    RolltuiFrame* f = rolltui_swap_begin(k.swap, 40, 12, RolltuiStyle{});
    for (const Shot& s : shots) rolltui_frame_outside_picture(f, s.id, s.r, RolltuiStyle{});
    if (cover_at) rolltui_frame_put(f, 3, 2, cover_at, 1, 1, RolltuiStyle{}, 0);
    RolltuiStr scratch;
    rolltui_terminal_present(k.t, k.swap, &scratch);
    return k.read();
  };
  {
    Pty k("sixel");
    OutsideLog log;
    log.t = k.t;
    rolltui_terminal_set_outside_pictures(k.t, outside_told, &log);
    std::size_t n = 0;
    std::string out = frame(k, {{at, 7}});
    check(told_since(log, n) == "draw 7 2,1 5x3", "a picture an outside program draws is the host's to draw once its cells are blank [" + told_since(log, n) + "]");
    {
      const std::size_t m = out.find("<draw 7");
      const std::size_t after = out.find('>', m) + 1;
      check(m != std::string::npos && m > 0 && out.compare(after, std::string::npos, "\x1b[0m\x1b[?25l\x1b[1;1H") == 0,
            "…told after the frame is written, and the cursor put back where the frame left it, hidden");
    }
    n = log.told.size();
    out = frame(k, {{at, 7}});
    check(log.told.size() == n && !has(out, "<"), "the same picture in the same cells is not told again");
    out = frame(k, {{at, 7}}, "X");
    check(told_since(log, n) == "clear 7 2,1 5x3" && out.find("<clear") < out.find('X'),
          "something drawn over it: the host clears it before the frame that writes over it [" + told_since(log, n) + "]");
    n = log.told.size();
    frame(k, {{at, 7}});
    check(told_since(log, n) == "draw 7 2,1 5x3", "…and draws it again once that has gone");
    n = log.told.size();
    frame(k, {{moved, 7}});
    check(told_since(log, n) == "clear 7 2,1 5x3 | draw 7 3,1 5x3", "moved: cleared where it was, drawn where it is [" + told_since(log, n) + "]");
    n = log.told.size();
    frame(k, {});
    check(told_since(log, n) == "clear 7 3,1 5x3", "gone from the frame: cleared");
    frame(k, {{at, 7}});
    n = log.told.size();
    rolltui_swap_invalidate(k.swap);
    out = frame(k, {{at, 7}});
    check(told_since(log, n) == "clear 7 2,1 5x3 | draw 7 2,1 5x3" && out.find("<clear") < out.find("<draw"),
          "the screen written whole: cleared before, drawn after [" + told_since(log, n) + "]");
    n = log.told.size();
    rolltui_terminal_suspend(k.t);
    check(told_since(log, n) == "clear 7 2,1 5x3", "the terminal left (a program handed it, a suspend): cleared while its screen is up");
    rolltui_terminal_resume(k.t);
    rolltui_swap_invalidate(k.swap);
    n = log.told.size();
    frame(k, {{at, 7}});
    check(told_since(log, n) == "draw 7 2,1 5x3", "…and drawn again when the program has the terminal back");
    n = log.told.size();
    frame(k, {{RolltuiRect{37, 10, 5, 3}, 8}, {RolltuiRect{1, 1, 2, 2}, 0}});
    check(told_since(log, n) == "clear 7 2,1 5x3 | draw 8 37,10 3x2", "cells past the frame's edge are clipped, and an id of 0 is no picture [" + told_since(log, n) + "]");
    n = log.told.size();
    rolltui_terminal_free(k.t);
    k.t = nullptr;
    check(told_since(log, n) == "clear 8 37,10 3x2", "the terminal freed: what is still drawn is cleared");
  }
  {
    // A LIBRARY PICTURE BESIDE ONE: each is kept by its own rules.
    const std::vector<unsigned char> px(20 * 20 * 3, 90);
    RolltuiImage* img = decode(ppm(20, 20, px));
    Pty k("sixel");
    OutsideLog log;
    log.t = k.t;
    rolltui_terminal_set_outside_pictures(k.t, outside_told, &log);
    auto both = [&](bool with_outside) {
      RolltuiFrame* f = rolltui_swap_begin(k.swap, 40, 12, RolltuiStyle{});
      rolltui_frame_image(f, RolltuiRect{20, 1, 10, 4}, img, RolltuiStyle{}, nullptr);
      if (with_outside) rolltui_frame_outside_picture(f, 7, at, RolltuiStyle{});
      RolltuiStr scratch;
      rolltui_terminal_present(k.t, k.swap, &scratch);
      return k.read();
    };
    const std::string first = both(true);
    check(has(first, "\x1bP0;1;0q") && told_since(log, 0) == "draw 7 2,1 5x3", "a sixel picture and an outside program's in one frame: each drawn");
    const std::string second = both(false);
    check(!has(second, "\x1bP0;1;0q") && told_since(log, 1) == "clear 7 2,1 5x3", "…and the outside one gone clears only it");
    rolltui_terminal_free(k.t);
    k.t = nullptr;
    rolltui_image_free(img);
  }
  {
    // HELD LIKE THE LIBRARY'S OWN: a cursor passing over files does not start the host's program for each.
    Pty k("sixel", false, 60);
    OutsideLog log;
    log.t = k.t;
    rolltui_terminal_set_outside_pictures(k.t, outside_told, &log);
    auto one = [&](RolltuiRect r) {
      RolltuiFrame* f = rolltui_swap_begin(k.swap, 40, 12, RolltuiStyle{});
      rolltui_frame_outside_picture(f, 7, r, RolltuiStyle{});
      RolltuiStr scratch;
      rolltui_terminal_present(k.t, k.swap, &scratch);
    };
    one(at);
    const int pending = rolltui_terminal_pending_ms(k.t);
    one(moved);
    check(log.told.empty() && pending > 0 && pending <= 60, "held: not told while it has not stayed in place (" + std::to_string(pending) + " ms to go)");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    one(moved);
    check(told_since(log, 0) == "draw 7 3,1 5x3", "…told once it has stayed, and never for the cells it passed through [" + told_since(log, 0) + "]");
    rolltui_terminal_set_outside_pictures(k.t, nullptr, nullptr);
    rolltui_terminal_free(k.t);
    k.t = nullptr;
    check(log.told.size() == 1, "a host that stops listening is told nothing more, not even on free");
  }
}

int main() {
  test_orientation_png_palette_filter();
  test_decode();
  test_fit();
  test_blocks();
  test_kitty_cells();
  test_detection();
  test_present();
  test_outside_pictures();
  test_preview_and_window();
  return testkit::report("image_test");
}
