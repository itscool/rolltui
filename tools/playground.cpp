//
// playground.cpp — the rolltui playground (plan/phase-9.md, requirement 12): renders
// a fixture transcript in a theme and a layout, so trying a layout or theme idea and
// asserting it are the same command.
//
//   rolltui-playground FIXTURE.md [options]
//     --theme NAME|FILE.json   default-dark | default-light | mono, or a theme file
//                              (a file is re-read whenever its mtime changes — edit
//                              it in another window and watch)
//     --layout NAME|FILE.json  default | panel-left | no-panel | stacked, or a layout
//                              file (hot-reloaded the same way; see Layout.hpp for
//                              the format). Below the layout's own min_width /
//                              min_height the `stacked` built-in is used instead and
//                              the status line says so.
//     --mode dark|light        which variant a theme file's {dark,light} values use
//     --depth truecolor|256|16|mono   colour depth (default: detect from the env)
//     --ambiguous-wide         East Asian ambiguous width = 2
//     --frame WxH              render exactly one frame at that size to stdout as
//                              plain text (one row per line, trailing spaces trimmed)
//                              and exit — the golden-frame harness and screenshot tool
//     --frame-sgr WxH          the same frame with colours, for a terminal `cat`
//     --keys "K K K"           scripted input applied before the frame (or before the
//                              interactive loop): Up Down PageUp PageDown Home End
//                              Tab ShiftTab Escape Enter WheelUp WheelDown, or a
//                              single character (p opens the help popup, l cycles
//                              layouts, t cycles themes)
//
// Fixture format: a markdown file cut into entries by marker lines
//   <!-- user -->   <!-- assistant -->   <!-- note -->
// (text before any marker is an assistant entry). User entries render verbatim with a
// "> " prefix in the prompt role; notes render verbatim in the note role.
//
// Slots the playground fills (a layout names them in "content"): transcript, status
// (the playground's own facts as label/value rows, or one line when the slot is a
// single row), input (a placeholder — typing is milestone 10), help (the key list),
// text:<literal> (the literal, so a layout file can put a label on screen). Any other
// slot draws "(no content for slot 'x')" — visible, never silent.
//
// Keys: q / Ctrl-C quit · Tab / Shift-Tab cycle focus · Esc closes the top popup ·
// p toggles the help popup · l cycles the built-in layouts · t cycles the built-in
// themes · r re-reads the fixture · Ctrl-L repaints. Scrolling: PgUp/PgDn/Home/End
// and the wheel over the transcript always scroll it (the input never steals them);
// Up/Down scroll only while the transcript has focus. Every key goes through
// WindowStack::route, so what the playground does is what a host would do.
// The status line shows theme, layout, size, scroll position, focus and the last
// frame's render time (instrumented from the first line — a slow frame is a number,
// not a feeling).
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
#include "rolltui/Layout.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Terminal.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Transcript.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"

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

const char* kHelpText =
    "q / Ctrl-C  quit\n"
    "Tab / Shift-Tab  cycle focus\n"
    "Esc  close the top popup\n"
    "p  toggle this help\n"
    "l  cycle layouts   t  cycle themes\n"
    "r  reload the fixture   Ctrl-L  repaint\n"
    "PgUp/PgDn/Home/End, wheel  scroll the transcript\n"
    "Up/Down  scroll while the transcript has focus";

struct App {
  std::string fixture_path, theme_arg = "default-dark", layout_arg = "default";
  ThemeMode mode = ThemeMode::Dark;
  ColorDepth depth = ColorDepth::TrueColor;
  bool ambiguous = false;
  int w = 80, h = 24;  // the screen
  Document doc;
  Theme theme;
  std::string theme_note;
  long theme_mtime = -1;
  Layout layout;
  std::string layout_note;
  long layout_mtime = -1;
  bool stacked_fallback = false;
  WindowStack stack;
  std::vector<TranscriptLine> lines;
  int laid_out_width = -1;
  ScrollState scroll;
  long last_frame_us = 0;
  std::size_t builtin_theme_index = 0, builtin_layout_index = 0;

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

  // Loads the layout named/pathed by layout_arg into `layout` and the stack's base
  // (popups stay open across a reload).
  bool load_layout_arg() {
    if (const Layout* b = builtin_layout(layout_arg)) {
      layout = *b;
      layout_note.clear();
    } else {
      bool ok;
      std::string text = read_file(layout_arg, ok);
      layout_mtime = mtime_of(layout_arg);
      if (!ok) { layout_note = "layout file not readable: " + layout_arg; layout = *builtin_layout("default"); }
      else {
        LayoutLoadReport rep;
        auto l = load_layout(text, rep);
        if (!l) { layout_note = "layout error: " + rep.error; layout = *builtin_layout("default"); }
        else {
          layout = *l;
          layout_note.clear();
          if (!rep.unknown_keys.empty()) layout_note += "unknown: " + rep.unknown_keys[0] + "; ";
          if (!rep.bad_values.empty()) layout_note += "bad: " + rep.bad_values[0] + "; ";
        }
      }
    }
    apply_layout();
    return layout_note.empty();
  }
  void maybe_reload_layout() {
    if (builtin_layout(layout_arg)) return;
    long m = mtime_of(layout_arg);
    if (m != layout_mtime) load_layout_arg();
  }
  // The base layer for the current screen: the chosen layout, or `stacked` below its
  // stated minimum. Re-applied whenever the size or the layout changes.
  void apply_layout() {
    const Rect area = layout_area();
    bool want_fallback = area.w < layout.min_width || area.h < layout.min_height;
    stacked_fallback = want_fallback;
    const Layer& base = want_fallback ? builtin_layout("stacked")->base : layout.base;
    stack.set_base(base);
    laid_out_width = -1;
  }
  const Layout& effective_layout() const { return stacked_fallback ? *builtin_layout("stacked") : layout; }
  Rect layout_area() const { return {0, 0, w, h > 1 ? h - 1 : h}; }
  void resize(int nw, int nh) {
    w = nw;
    h = nh;
    apply_layout();
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
  // Text slots keep one column clear on each side of a bordered window — a widget
  // choice made here, not a layout knob (Layout.hpp: a border is the only spacing).
  static Rect text_area(const ResolvedNode& rn) {
    Rect r = rn.inner;
    if (rn.node->border != Border::None && r.w >= 3) { r.x += 1; r.w -= 2; }
    return r;
  }
  // The transcript slot's text area at the current size (empty when the layout has none).
  Rect transcript_rect() const {
    for (const ResolvedNode& rn : stack.resolve(layout_area()))
      if (rn.node->is_window() && rn.node->content == "transcript") return text_area(rn);
    return {};
  }
  void toggle_help() {
    if (stack.has_popup("help")) { while (stack.depth() > 1 && stack.layers().back().id != "help") stack.pop(); stack.pop(); return; }
    if (const Layer* p = effective_layout().popup("help")) stack.push(*p);
  }

  // ---- slot renderers ----
  void draw_status(const ResolvedNode& rn, Frame& f, bool with_timing) {
    const Rect r = rn.inner;
    struct Row { std::string label, value; };
    std::vector<Row> rows = {
        {"theme", theme.name},
        {"layout", effective_layout().name + (stacked_fallback ? " (fallback)" : "")},
        {"size", std::to_string(w) + "x" + std::to_string(h)},
        {"line", std::to_string(lines.empty() ? 0 : scroll.top + 1) + "/" + std::to_string(lines.size())},
        {"follow", scroll.follow ? "yes" : "no"},
        {"depth", std::string(color_depth_name(depth))},
        {"focus", stack.focused() ? stack.focused()->id : "-"},
    };
    if (with_timing) rows.push_back({"frame", std::to_string(last_frame_us) + " us"});
    const Style label = theme.style(Role::label), value = theme.style(Role::value);
    if (r.h == 1) {  // a strip: everything on one line
      std::string s;
      for (const Row& row : rows) s += (s.empty() ? "" : "  ") + row.label + " " + row.value;
      f.put_text(r.x + 1, r.y, s, value, r.w - 1, ambiguous);
      return;
    }
    for (std::size_t i = 0; i < rows.size() && static_cast<int>(i) < r.h; ++i) {
      int y = r.y + static_cast<int>(i);
      int used = f.put_text(r.x + 1, y, rows[i].label, label, std::max(r.w - 1, 0), ambiguous);
      f.put_text(r.x + 1 + 8, y, rows[i].value, value, std::max(r.w - 9 - (used > 8 ? used - 8 : 0), 0), ambiguous);
    }
  }
  void draw_input(const ResolvedNode& rn, Frame& f) {
    const Rect r = rn.inner;
    int used = f.put_text(r.x, r.y, "> ", theme.style(Role::prompt), r.w, ambiguous);
    f.put_text(r.x + used, r.y, "type here (input is milestone 10)", theme.style(Role::input_placeholder), std::max(r.w - used, 0), ambiguous);
    if (rn.focused) f.set_cursor(r.x + used, r.y, true);
  }
  void draw_text(const ResolvedNode& rn, Frame& f, std::string_view text, Role role) {
    const Rect r = text_area(rn);
    WrapOptions wo;
    wo.ambiguous_wide = ambiguous;
    int y = r.y;
    for (const Line& l : wrap(text, r.w, wo)) {
      if (y >= r.y + r.h) break;
      f.put_text(r.x + l.indent, y++, l.text, theme.style(role), std::max(r.w - l.indent, 0), ambiguous);
    }
  }
  void draw_slot(const ResolvedNode& rn, Frame& f, bool with_timing) {
    const std::string& c = rn.node->content;
    if (c == "transcript") {
      const Rect r = text_area(rn);
      relayout(r.w, r.h);
      reconcile_scroll(scroll, lines.size(), r.h);
      std::size_t below = draw_transcript(f, r, lines, scroll, theme, ambiguous);
      if (below > 0 && r.w >= 8) {
        std::string marker = "\xE2\x96\xBC " + std::to_string(below) + " more ";
        int mw = unicode::display_width(marker);
        f.put_text(r.x + r.w - mw, r.y + r.h - 1, marker, theme.style(Role::scroll_marker), mw, ambiguous);
      }
    } else if (c == "status") {
      draw_status(rn, f, with_timing);
    } else if (c == "input") {
      draw_input(rn, f);
    } else if (c == "help") {
      draw_text(rn, f, kHelpText, Role::text);
    } else if (c.rfind("text:", 0) == 0) {
      draw_text(rn, f, std::string_view(c).substr(5), Role::text);
    } else {
      draw_text(rn, f, "(no content for slot '" + c + "')", Role::text_muted);
    }
  }

  // The frame: the layout above a one-line status bar of the playground's own.
  Frame render(bool with_timing) {
    auto t0 = std::chrono::steady_clock::now();
    Frame f(w, h, theme.style(Role::background));
    const Rect area = layout_area();
    stack.compose(f, area, theme, [&](const ResolvedNode& rn, Frame& fr) { draw_slot(rn, fr, with_timing); }, ambiguous);
    if (h > 1) {
      f.fill({0, h - 1, w, 1}, theme.style(Role::panel_background));
      std::string status = " " + theme.name + "  " + effective_layout().name + "  " + std::to_string(w) + "x" + std::to_string(h) +
                           "  line " + std::to_string(lines.empty() ? 0 : scroll.top + 1) + "/" + std::to_string(lines.size()) +
                           (scroll.follow ? "  follow" : "") + "  " + std::string(color_depth_name(depth)) +
                           "  focus:" + (stack.focused() ? stack.focused()->id : "-");
      if (with_timing) status += "  " + std::to_string(last_frame_us) + " us";
      if (stacked_fallback) status += "  [stacked: below " + std::to_string(layout.min_width) + "x" + std::to_string(layout.min_height) + "]";
      if (!theme_note.empty()) status += "  [" + theme_note + "]";
      if (!layout_note.empty()) status += "  [" + layout_note + "]";
      f.put_text(0, h - 1, status, theme.style(Role::label), w, ambiguous);
      std::string help = "q quit  p help  l layout  t theme ";
      int hw = unicode::display_width(help);
      if (hw + unicode::display_width(status) + 2 <= w) f.put_text(w - hw, h - 1, help, theme.style(Role::text_muted), hw, ambiguous);
    }
    last_frame_us = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    return f;
  }

  // Returns false to quit.
  bool handle(const Event& ev) {
    // App-level keys first; everything else is routed by the stack.
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev)) {
      if (k->key == Key::Char && k->ctrl && k->ch == 'c') return false;
      if (k->key == Key::Char && !k->ctrl && !k->alt) {
        if (k->ch == 'q') return false;
        if (k->ch == 't') {
          std::vector<std::string_view> names = builtin_theme_names();
          builtin_theme_index = (builtin_theme_index + 1) % names.size();
          theme_arg = std::string(names[builtin_theme_index]);
          load_theme_arg();
          return true;
        }
        if (k->ch == 'l') {
          std::vector<std::string_view> names = builtin_layout_names();
          builtin_layout_index = (builtin_layout_index + 1) % names.size();
          layout_arg = std::string(names[builtin_layout_index]);
          load_layout_arg();
          return true;
        }
        if (k->ch == 'r') { load_fixture(); return true; }
        if (k->ch == 'p') { toggle_help(); return true; }
      }
      if (k->key == Key::Char && k->ctrl && k->ch == 'l') { laid_out_width = -1; return true; }
    }
    const Rect tr = transcript_rect();
    const int body_h = tr.h;
    Route r = stack.route(ev, layout_area());
    if (r.kind != Route::Kind::Deliver) return true;
    const bool to_transcript = r.window == "transcript";
    const bool to_input = r.window == "input";
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev)) {
      if (to_transcript && k->key == Key::Up) scroll_by(scroll, -1, lines.size(), body_h);
      else if (to_transcript && k->key == Key::Down) scroll_by(scroll, 1, lines.size(), body_h);
      else if ((to_transcript || to_input) && k->key == Key::PageUp) scroll_by(scroll, -(body_h - 1), lines.size(), body_h);
      else if ((to_transcript || to_input) && k->key == Key::PageDown) scroll_by(scroll, body_h - 1, lines.size(), body_h);
      else if ((to_transcript || to_input) && k->key == Key::Home) scroll_to_top(scroll);
      else if ((to_transcript || to_input) && k->key == Key::End) scroll_to_bottom(scroll, lines.size(), body_h);
    } else if (const MouseEvent* m = std::get_if<MouseEvent>(&ev)) {
      if (to_transcript && m->kind == MouseEvent::Kind::WheelUp) scroll_by(scroll, -3, lines.size(), body_h);
      if (to_transcript && m->kind == MouseEvent::Kind::WheelDown) scroll_by(scroll, 3, lines.size(), body_h);
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

std::vector<Event> scripted_keys(const std::string& spec, int w, int h) {
  std::vector<Event> out;
  std::istringstream in(spec);
  std::string tok;
  auto key = [](Key k, bool shift = false) { KeyEvent e; e.key = k; e.shift = shift; return e; };
  while (in >> tok) {
    if (tok == "Up") out.emplace_back(key(Key::Up));
    else if (tok == "Down") out.emplace_back(key(Key::Down));
    else if (tok == "PageUp") out.emplace_back(key(Key::PageUp));
    else if (tok == "PageDown") out.emplace_back(key(Key::PageDown));
    else if (tok == "Home") out.emplace_back(key(Key::Home));
    else if (tok == "End") out.emplace_back(key(Key::End));
    else if (tok == "Enter") out.emplace_back(key(Key::Enter));
    else if (tok == "Escape") out.emplace_back(key(Key::Escape));
    else if (tok == "Tab") out.emplace_back(key(Key::Tab));
    else if (tok == "ShiftTab") out.emplace_back(key(Key::Tab, true));
    else if (tok == "WheelUp" || tok == "WheelDown") {
      // Over the middle of the screen, which every built-in layout gives to the transcript.
      MouseEvent m;
      m.kind = tok == "WheelUp" ? MouseEvent::Kind::WheelUp : MouseEvent::Kind::WheelDown;
      m.x = w / 4;
      m.y = h / 3;
      out.emplace_back(m);
    } else {
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
               "usage: rolltui-playground FIXTURE.md [--theme NAME|FILE] [--layout NAME|FILE] [--mode dark|light]\n"
               "       [--depth truecolor|256|16|mono] [--ambiguous-wide] [--frame WxH | --frame-sgr WxH]\n"
               "       [--keys \"Up Down PageDown Tab p ...\"]\n");
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
    else if (a == "--layout") app.layout_arg = next();
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
    app.resize(w, h);
    app.load_layout_arg();
    Rect tr = app.transcript_rect();
    app.relayout(tr.w, tr.h);
    for (const Event& e : scripted_keys(keys_spec, w, h)) app.handle(e);
    Frame f = app.render(false);
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
  app.resize(term.width(), term.height());
  app.load_layout_arg();
  for (const Event& e : scripted_keys(keys_spec, app.w, app.h)) app.handle(e);
  Frame prev;
  bool have_prev = false;
  bool running = true;
  while (running) {
    app.maybe_reload_theme();
    app.maybe_reload_layout();
    Frame f = app.render(true);
    term.write(render_diff(have_prev ? &prev : nullptr, f, app.depth));
    prev = std::move(f);
    have_prev = true;
    for (const Event& e : term.poll(250)) {
      if (const ResizeEvent* r = std::get_if<ResizeEvent>(&e)) {
        app.resize(r->w, r->h);
        have_prev = false;
        continue;
      }
      if (const KeyEvent* k = std::get_if<KeyEvent>(&e); k && k->key == Key::Char && k->ctrl && k->ch == 'l')
        have_prev = false;
      if (!app.handle(e)) { running = false; break; }
    }
  }
  return 0;
}
