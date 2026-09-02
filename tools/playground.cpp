//
// playground.cpp — the rolltui playground (plan/phase-9.md, requirement 12): renders
// a fixture transcript in a theme, so trying a layout or theme idea and asserting it
// are the same command.
//
//   rolltui-playground FIXTURE.md [options]
//     --theme NAME|FILE.json   default-dark | default-light | mono, or a theme file
//                              (a file is re-read whenever its mtime changes — edit
//                              it in another window and watch)
//     --mode dark|light        which variant a theme file's {dark,light} values use
//     --depth truecolor|256|16|mono   colour depth (default: detect from the env)
//     --ambiguous-wide         East Asian ambiguous width = 2
//     --frame WxH              render exactly one frame at that size to stdout as
//                              plain text (one row per line, trailing spaces trimmed)
//                              and exit — the golden-frame harness and screenshot tool
//     --frame-sgr WxH          the same frame with colours, for a terminal `cat`
//     --keys "K K K"           scripted input applied before the frame (or before the
//                              interactive loop): Up Down PageUp PageDown Home End
//                              WheelUp WheelDown, or a single character
//
// Fixture format: a markdown file cut into entries by marker lines
//   <!-- user -->   <!-- assistant -->   <!-- note -->
// (text before any marker is an assistant entry). User entries render verbatim with a
// "> " prefix in the prompt role; notes render verbatim in the note role.
//
// Interactive keys: q / Ctrl-C quit · Up/Down/PgUp/PgDn/Home/End and the wheel scroll ·
// t cycles the built-in themes · r re-reads the fixture · Ctrl-L repaints.
// The status line shows theme, size, scroll position and the last frame's render time
// (instrumented from the first line — a slow frame is a number, not a feeling).
//
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "rolltui/Document.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Terminal.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Transcript.hpp"
#include "rolltui/Unicode.hpp"

using namespace rolltui;

namespace {

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

long mtime_of(const std::string& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) return -1;
  return static_cast<long>(st.st_mtime);
}

Document parse_fixture(const std::string& text) {
  Document doc;
  std::string kind = "assistant";
  std::string buf;
  int n = 0;
  auto flush = [&]() {
    // Trim leading/trailing blank lines of the entry.
    std::size_t a = buf.find_first_not_of("\n");
    std::size_t b = buf.find_last_not_of("\n");
    std::string body = (a == std::string::npos) ? "" : buf.substr(a, b - a + 1);
    if (body.empty()) { buf.clear(); return; }
    DocEntry e;
    e.id = "e" + std::to_string(n++);
    e.text = body;
    if (kind == "user") { e.markdown = false; e.role = Role::text; e.prefix = "> "; e.prefix_role = Role::prompt; }
    else if (kind == "note") { e.markdown = false; e.role = Role::note; }
    else { e.markdown = true; e.role = Role::text; }
    doc.entries.push_back(std::move(e));
    buf.clear();
  };
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line == "<!-- user -->" || line == "<!-- assistant -->" || line == "<!-- note -->") {
      flush();
      kind = line.substr(5, line.size() - 9);
      continue;
    }
    buf += line + "\n";
  }
  flush();
  return doc;
}

struct App {
  std::string fixture_path, theme_arg = "default-dark";
  ThemeMode mode = ThemeMode::Dark;
  ColorDepth depth = ColorDepth::TrueColor;
  bool ambiguous = false;
  Document doc;
  Theme theme;
  std::string theme_note;
  long theme_mtime = -1;
  std::vector<TranscriptLine> lines;
  int laid_out_width = -1;
  ScrollState scroll;
  long last_frame_us = 0;
  std::size_t builtin_index = 0;

  bool load_theme_arg() {
    if (const Theme* b = builtin_theme(theme_arg)) {
      theme = *b;
      theme_note.clear();
      return true;
    }
    bool ok;
    std::string text = read_file(theme_arg, ok);
    if (!ok) { theme_note = "theme file not readable: " + theme_arg; theme = *builtin_theme("default-dark"); return false; }
    ThemeLoadReport rep;
    auto t = load_theme(text, mode, rep);
    theme_mtime = mtime_of(theme_arg);
    if (!t) { theme_note = "theme error: " + rep.error; theme = *builtin_theme("default-dark"); return false; }
    theme = *t;
    theme_note.clear();
    if (!rep.missing_roles.empty()) theme_note += std::to_string(rep.missing_roles.size()) + " roles missing (inherit text); ";
    if (!rep.unknown_keys.empty()) theme_note += "unknown: " + rep.unknown_keys[0] + "; ";
    if (!rep.bad_values.empty()) theme_note += "bad: " + rep.bad_values[0] + "; ";
    return true;
  }
  void maybe_reload_theme() {
    if (builtin_theme(theme_arg)) return;
    long m = mtime_of(theme_arg);
    if (m != theme_mtime) load_theme_arg();
  }
  bool load_fixture() {
    bool ok;
    std::string text = read_file(fixture_path, ok);
    if (!ok) return false;
    doc = parse_fixture(text);
    laid_out_width = -1;
    return true;
  }
  void relayout(int width, int viewport_h) {
    if (width == laid_out_width) return;
    TranscriptLayoutOptions o;
    o.width = width;
    o.ambiguous_wide = ambiguous;
    lines = layout_transcript(doc, o);
    laid_out_width = width;
    reconcile_scroll(scroll, lines.size(), viewport_h);
  }
  // The frame: transcript above a one-line status bar.
  Frame render(int w, int h, bool with_timing) {
    auto t0 = std::chrono::steady_clock::now();
    Frame f(w, h, theme.style(Role::background));
    int body_h = h > 1 ? h - 1 : h;
    relayout(w, body_h);
    reconcile_scroll(scroll, lines.size(), body_h);
    std::size_t below = draw_transcript(f, {0, 0, w, body_h}, lines, scroll, theme, ambiguous);
    if (below > 0 && w >= 8) {
      std::string marker = "\xE2\x96\xBC " + std::to_string(below) + " more ";
      int mw = unicode::display_width(marker);
      f.put_text(w - mw, body_h - 1, marker, theme.style(Role::scroll_marker), mw, ambiguous);
    }
    if (h > 1) {
      f.fill({0, h - 1, w, 1}, theme.style(Role::panel_background));
      std::string status = " " + theme.name + "  " + std::to_string(w) + "x" + std::to_string(h) + "  line " +
                           std::to_string(lines.empty() ? 0 : scroll.top + 1) + "/" + std::to_string(lines.size()) +
                           (scroll.follow ? "  follow" : "") + "  " + std::string(color_depth_name(depth));
      if (with_timing) status += "  " + std::to_string(last_frame_us) + " us";
      if (!theme_note.empty()) status += "  [" + theme_note + "]";
      f.put_text(0, h - 1, status, theme.style(Role::label), w, ambiguous);
      std::string help = "q quit  t theme  r reload  PgUp/PgDn ";
      int hw = unicode::display_width(help);
      if (hw + unicode::display_width(status) + 2 <= w) f.put_text(w - hw, h - 1, help, theme.style(Role::text_muted), hw, ambiguous);
    }
    last_frame_us = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    return f;
  }
  // Returns false to quit.
  bool handle(const Event& ev, int body_h) {
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev)) {
      if (k->key == Key::Char && k->ctrl && k->ch == 'c') return false;
      if (k->key == Key::Char && !k->ctrl && !k->alt && k->ch == 'q') return false;
      if (k->key == Key::Char && !k->ctrl && k->ch == 't') {
        std::vector<std::string_view> names = builtin_theme_names();
        builtin_index = (builtin_index + 1) % names.size();
        theme_arg = std::string(names[builtin_index]);
        load_theme_arg();
      } else if (k->key == Key::Char && !k->ctrl && k->ch == 'r') {
        load_fixture();
      } else if (k->key == Key::Char && k->ctrl && k->ch == 'l') {
        laid_out_width = -1;
      } else if (k->key == Key::Up) scroll_by(scroll, -1, lines.size(), body_h);
      else if (k->key == Key::Down) scroll_by(scroll, 1, lines.size(), body_h);
      else if (k->key == Key::PageUp) scroll_by(scroll, -(body_h - 1), lines.size(), body_h);
      else if (k->key == Key::PageDown) scroll_by(scroll, body_h - 1, lines.size(), body_h);
      else if (k->key == Key::Home) scroll_to_top(scroll);
      else if (k->key == Key::End) scroll_to_bottom(scroll, lines.size(), body_h);
    } else if (const MouseEvent* m = std::get_if<MouseEvent>(&ev)) {
      if (m->kind == MouseEvent::Kind::WheelUp) scroll_by(scroll, -3, lines.size(), body_h);
      if (m->kind == MouseEvent::Kind::WheelDown) scroll_by(scroll, 3, lines.size(), body_h);
    }
    return true;
  }
};

bool parse_size(const std::string& s, int& w, int& h) {
  std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

std::vector<Event> scripted_keys(const std::string& spec) {
  std::vector<Event> out;
  std::istringstream in(spec);
  std::string tok;
  auto key = [](Key k) { KeyEvent e; e.key = k; return e; };
  while (in >> tok) {
    if (tok == "Up") out.emplace_back(key(Key::Up));
    else if (tok == "Down") out.emplace_back(key(Key::Down));
    else if (tok == "PageUp") out.emplace_back(key(Key::PageUp));
    else if (tok == "PageDown") out.emplace_back(key(Key::PageDown));
    else if (tok == "Home") out.emplace_back(key(Key::Home));
    else if (tok == "End") out.emplace_back(key(Key::End));
    else if (tok == "Enter") out.emplace_back(key(Key::Enter));
    else if (tok == "Escape") out.emplace_back(key(Key::Escape));
    else if (tok == "WheelUp") { MouseEvent m; m.kind = MouseEvent::Kind::WheelUp; out.emplace_back(m); }
    else if (tok == "WheelDown") { MouseEvent m; m.kind = MouseEvent::Kind::WheelDown; out.emplace_back(m); }
    else {
      std::vector<unicode::DecodedChar> d = unicode::decode_utf8(tok);
      if (!d.empty()) { KeyEvent e; e.key = Key::Char; e.ch = d[0].cp; out.emplace_back(e); }
    }
  }
  return out;
}

void print_frame_plain(const Frame& f) {
  for (int y = 0; y < f.height(); ++y) {
    std::string row;
    for (int x = 0; x < f.width(); ++x) {
      const Cell& c = f.at(x, y);
      if (!c.continuation) row += c.text;
    }
    std::size_t end = row.find_last_not_of(' ');
    row = (end == std::string::npos) ? "" : row.substr(0, end + 1);
    std::printf("%s\n", row.c_str());
  }
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-playground FIXTURE.md [--theme NAME|FILE] [--mode dark|light]\n"
               "       [--depth truecolor|256|16|mono] [--ambiguous-wide] [--frame WxH | --frame-sgr WxH]\n"
               "       [--keys \"Up Down PageDown ...\"]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  App app;
  app.depth = detect_color_depth(std::getenv("COLORTERM"), std::getenv("TERM"), std::getenv("ROLL_COLOR_DEPTH"));
  std::string frame_spec, keys_spec;
  bool frame_sgr = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--theme") app.theme_arg = next();
    else if (a == "--mode") app.mode = (next() == "light") ? ThemeMode::Light : ThemeMode::Dark;
    else if (a == "--depth") {
      std::string d = next();
      app.depth = detect_color_depth(nullptr, nullptr, d.c_str());
    } else if (a == "--ambiguous-wide") app.ambiguous = true;
    else if (a == "--frame") frame_spec = next();
    else if (a == "--frame-sgr") { frame_spec = next(); frame_sgr = true; }
    else if (a == "--keys") keys_spec = next();
    else if (a.rfind("--", 0) == 0) return usage();
    else app.fixture_path = a;
  }
  if (app.fixture_path.empty()) return usage();
  if (!app.load_fixture()) { std::fprintf(stderr, "cannot read %s\n", app.fixture_path.c_str()); return 1; }
  app.load_theme_arg();

  if (!frame_spec.empty()) {
    int w, h;
    if (!parse_size(frame_spec, w, h)) return usage();
    int body_h = h > 1 ? h - 1 : h;
    app.relayout(w, body_h);
    for (const Event& e : scripted_keys(keys_spec)) app.handle(e, body_h);
    Frame f = app.render(w, h, false);
    if (frame_sgr) {
      std::string bytes = render_full(f, app.depth);
      // A screenshot, not a screen: strip the cursor/clear preamble so it cats cleanly.
      std::fwrite(bytes.data(), 1, bytes.size(), stdout);
      std::printf("\x1b[0m\n");
    } else {
      print_frame_plain(f);
    }
    return 0;
  }

  Terminal term(STDIN_FILENO, STDOUT_FILENO);
  if (!term.is_tty()) { std::fprintf(stderr, "not a terminal; use --frame WxH\n"); return 1; }
  int w = term.width(), h = term.height();
  for (const Event& e : scripted_keys(keys_spec)) app.handle(e, h - 1);
  Frame prev;
  bool have_prev = false;
  bool running = true;
  while (running) {
    app.maybe_reload_theme();
    Frame f = app.render(w, h, true);
    term.write(render_diff(have_prev ? &prev : nullptr, f, app.depth));
    prev = std::move(f);
    have_prev = true;
    for (const Event& e : term.poll(250)) {
      if (const ResizeEvent* r = std::get_if<ResizeEvent>(&e)) {
        w = r->w;
        h = r->h;
        have_prev = false;
        continue;
      }
      if (const KeyEvent* k = std::get_if<KeyEvent>(&e); k && k->key == Key::Char && k->ctrl && k->ch == 'l')
        have_prev = false;
      if (!app.handle(e, h - 1)) { running = false; break; }
    }
  }
  return 0;
}
