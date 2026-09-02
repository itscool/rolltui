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
//                              and exit — the golden-frame harness and screenshot tool.
//                              If the scripted input copied a selection, the copied
//                              text follows the frame after a "--- copied ---" line.
//     --frame-sgr WxH          the same frame with colours, for a terminal `cat`
//     --keys "K K K"           scripted input applied before the frame (or before the
//                              interactive loop): Up Down PageUp PageDown Home End
//                              Tab ShiftTab Escape Enter WheelUp WheelDown CtrlO AltC,
//                              mouse as Click X,Y · ShiftClick X,Y · DblClick X,Y ·
//                              TripleClick X,Y · Drag X,Y · Release X,Y, Tick (one
//                              auto-scroll step while a drag is past an edge), or a
//                              single character (p opens the help popup, l cycles
//                              layouts, t cycles themes). Scripted events are one
//                              second apart except the presses of a DblClick /
//                              TripleClick, which share a timestamp.
//
// Fixture format: a markdown file cut into entries by marker lines
//   <!-- user -->   <!-- assistant -->   <!-- note -->   <!-- tool: summary text -->
// (text before any marker is an assistant entry). User entries render verbatim with a
// "> " prefix in the prompt role; notes render verbatim in the note role; a tool
// entry is a FOLDABLE verbatim block whose summary is the marker's text (folded to
// start with).
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
// Up/Down scroll only while the transcript has focus. Mouse: click and drag select
// (auto-scrolling past an edge), double-click a word, triple-click a line, release
// copies (the playground shows the byte count — it has no clipboard of its own),
// Alt-C copies again, a click on a folded block's summary line unfolds it, Ctrl-O
// toggles the first fold in view. Every event goes through WindowStack::route, so
// what the playground does is what a host would do. The status line shows theme,
// layout, size, scroll position, focus and the last frame's render time
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
  std::string kind = "assistant", summary;
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
    else if (kind == "tool") { e.markdown = false; e.role = Role::text_muted; e.foldable = true; e.summary = summary; e.folded = true; }
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
    if (line.rfind("<!-- tool:", 0) == 0 && line.size() >= 14 && line.compare(line.size() - 3, 3, "-->") == 0) {
      flush();
      kind = "tool";
      summary = line.substr(10, line.size() - 13);
      std::size_t a = summary.find_first_not_of(' '), b = summary.find_last_not_of(' ');
      summary = (a == std::string::npos) ? "" : summary.substr(a, b - a + 1);
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
    "Up/Down  scroll while the transcript has focus\n"
    "drag  select (auto-scrolls past an edge); release copies\n"
    "double-click  word   triple-click  line   Alt-C  copy again\n"
    "click a folded block / Ctrl-O  toggle a fold";

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
  Transcript transcript;
  std::string copied;       // the last copy (the playground has no clipboard)
  bool copied_any = false;
  std::uint64_t clock_ms = 0;  // the clock handed to the widget (real or scripted)
  long last_frame_us = 0;
  std::size_t builtin_theme_index = 0, builtin_layout_index = 0;

  App() {
    transcript.on_copy = [this](const std::string& s) { copied = s; copied_any = true; };
  }

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
    return true;
  }
  // Text slots keep one column clear on each side of a bordered window — a widget
  // choice (the transcript owns its inset; Layout.hpp: a border is the only spacing).
  static Rect text_area(const ResolvedNode& rn) {
    Rect r = rn.inner;
    if (rn.node->border != Border::None && r.w >= 3) { r.x += 1; r.w -= 2; }
    return r;
  }
  TranscriptOptions transcript_options(const ResolvedNode& rn) const {
    TranscriptOptions o;
    o.ambiguous_wide = ambiguous;
    o.inset = rn.node->border != Border::None ? 1 : 0;
    return o;
  }
  // Lays the transcript out for the current size so an event can be hit-tested
  // against the same geometry the frame will draw (the cache makes this free).
  void ensure_transcript_layout() {
    for (const ResolvedNode& rn : stack.resolve(layout_area()))
      if (rn.node->is_window() && rn.node->content == "transcript") {
        transcript.layout(doc, rn.inner, transcript_options(rn));
        return;
      }
  }
  void toggle_help() {
    if (stack.has_popup("help")) { while (stack.depth() > 1 && stack.layers().back().id != "help") stack.pop(); stack.pop(); return; }
    if (const Layer* p = effective_layout().popup("help")) stack.push(*p);
  }

  // ---- slot renderers ----
  void draw_status(const ResolvedNode& rn, Frame& f, bool with_timing) {
    const Rect r = rn.inner;
    struct Row { std::string label, value; };
    const std::size_t total = transcript.total_lines();
    std::vector<Row> rows = {
        {"theme", theme.name},
        {"layout", effective_layout().name + (stacked_fallback ? " (fallback)" : "")},
        {"size", std::to_string(w) + "x" + std::to_string(h)},
        {"line", std::to_string(total == 0 ? 0 : transcript.top_line() + 1) + "/" + std::to_string(total)},
        {"follow", transcript.scroll().follow ? "yes" : "no"},
        {"depth", std::string(color_depth_name(depth))},
        {"focus", stack.focused() ? stack.focused()->id : "-"},
    };
    if (with_timing) rows.push_back({"frame", std::to_string(last_frame_us) + " us"});
    if (copied_any) rows.push_back({"copied", std::to_string(copied.size()) + " bytes"});
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
      transcript.layout(doc, rn.inner, transcript_options(rn));
      transcript.draw(f, theme);
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
      const std::size_t total = transcript.total_lines();
      std::string status = " " + theme.name + "  " + effective_layout().name + "  " + std::to_string(w) + "x" + std::to_string(h) +
                           "  line " + std::to_string(total == 0 ? 0 : transcript.top_line() + 1) + "/" + std::to_string(total) +
                           (transcript.scroll().follow ? "  follow" : "") + "  " + std::string(color_depth_name(depth)) +
                           "  focus:" + (stack.focused() ? stack.focused()->id : "-");
      if (with_timing) status += "  " + std::to_string(last_frame_us) + " us";
      if (copied_any) status += "  copied " + std::to_string(copied.size()) + "B";
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
      if (k->key == Key::Char && k->ctrl && k->ch == 'l') return true;  // the loop repaints
    }
    ensure_transcript_layout();
    Route r = stack.route(ev, layout_area());
    if (r.kind != Route::Kind::Deliver) return true;
    const bool to_transcript = r.window == "transcript";
    const bool to_input = r.window == "input";
    if (to_transcript) { transcript.handle(ev, doc, clock_ms); return true; }
    if (const KeyEvent* k = std::get_if<KeyEvent>(&ev); k && to_input) {
      // The input never steals the transcript's keys (plan: typing never touches the
      // offset; Ctrl-O and Alt-C act on the transcript from wherever focus is).
      if (k->key == Key::PageUp) transcript.scroll_page(-1);
      else if (k->key == Key::PageDown) transcript.scroll_page(1);
      else if (k->key == Key::Home) transcript.scroll_to_top();
      else if (k->key == Key::End) transcript.scroll_to_bottom();
      else if (k->key == Key::Char && k->ctrl && k->ch == 'o') transcript.toggle_fold_nearest_top(doc);
      else if (k->key == Key::Char && k->alt && k->ch == 'c') transcript.copy_selection();
    }
    return true;
  }
  void tick() {
    ensure_transcript_layout();
    transcript.tick();
  }
};

bool parse_size(const std::string& s, int& w, int& h) {
  std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

// One scripted step: an event with the clock it happens at, or a tick.
struct Step {
  bool tick = false;
  Event ev;
  std::uint64_t ms = 0;
};

std::vector<Step> scripted_keys(const std::string& spec, int w, int h) {
  std::vector<Step> out;
  std::istringstream in(spec);
  std::string tok;
  std::uint64_t clock = 1000;
  auto key = [](Key k, bool shift = false) { KeyEvent e; e.key = k; e.shift = shift; return e; };
  auto ctrl = [](char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.ctrl = true; return e; };
  auto alt = [](char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.alt = true; return e; };
  auto mouse = [](MouseEvent::Kind k, int x, int y, int button = 1, bool shift = false) {
    MouseEvent m;
    m.kind = k;
    m.x = x;
    m.y = y;
    m.button = button;
    m.shift = shift;
    return m;
  };
  auto push = [&](Event e, bool advance = true) {
    out.push_back({false, std::move(e), clock});
    if (advance) clock += 1000;
  };
  auto xy = [&](int& x, int& y) {
    std::string pos;
    if (!(in >> pos)) return false;
    std::size_t comma = pos.find(',');
    if (comma == std::string::npos) return false;
    x = std::atoi(pos.substr(0, comma).c_str());
    y = std::atoi(pos.substr(comma + 1).c_str());
    return true;
  };
  while (in >> tok) {
    int x = 0, y = 0;
    if (tok == "Up") push(key(Key::Up));
    else if (tok == "Down") push(key(Key::Down));
    else if (tok == "PageUp") push(key(Key::PageUp));
    else if (tok == "PageDown") push(key(Key::PageDown));
    else if (tok == "Home") push(key(Key::Home));
    else if (tok == "End") push(key(Key::End));
    else if (tok == "Enter") push(key(Key::Enter));
    else if (tok == "Escape") push(key(Key::Escape));
    else if (tok == "Tab") push(key(Key::Tab));
    else if (tok == "ShiftTab") push(key(Key::Tab, true));
    else if (tok == "CtrlO") push(ctrl('o'));
    else if (tok == "AltC") push(alt('c'));
    else if (tok == "Tick") out.push_back({true, {}, clock});
    else if (tok == "WheelUp" || tok == "WheelDown") {
      // Over the middle of the screen, which every built-in layout gives to the transcript.
      push(mouse(tok == "WheelUp" ? MouseEvent::Kind::WheelUp : MouseEvent::Kind::WheelDown, w / 4, h / 3, 0));
    } else if (tok == "Click" || tok == "ShiftClick") {
      if (xy(x, y)) push(mouse(MouseEvent::Kind::Press, x, y, 1, tok == "ShiftClick"));
    } else if (tok == "DblClick" || tok == "TripleClick") {
      if (xy(x, y)) {
        const int presses = tok == "DblClick" ? 2 : 3;
        for (int i = 0; i < presses; ++i) {
          push(mouse(MouseEvent::Kind::Press, x, y), false);
          if (i + 1 < presses) push(mouse(MouseEvent::Kind::Release, x, y), false);
        }
        clock += 1000;
      }
    } else if (tok == "Drag") {
      if (xy(x, y)) push(mouse(MouseEvent::Kind::Drag, x, y));
    } else if (tok == "Release") {
      // Optional position; without one the release lands where the last event was.
      std::streampos here = in.tellg();
      std::string maybe;
      if (in >> maybe && maybe.find(',') != std::string::npos) {
        x = std::atoi(maybe.substr(0, maybe.find(',')).c_str());
        y = std::atoi(maybe.substr(maybe.find(',') + 1).c_str());
      } else {
        in.clear();
        in.seekg(here);
        const MouseEvent* last = nullptr;
        for (const Step& s : out)
          if (const MouseEvent* m = std::get_if<MouseEvent>(&s.ev)) last = m;
        if (last) { x = last->x; y = last->y; }
      }
      push(mouse(MouseEvent::Kind::Release, x, y));
    } else {
      std::vector<unicode::DecodedChar> d = unicode::decode_utf8(tok);
      if (!d.empty()) { KeyEvent e; e.key = Key::Char; e.ch = d[0].cp; push(e); }
    }
  }
  return out;
}

void run_steps(App& app, const std::vector<Step>& steps) {
  for (const Step& s : steps) {
    app.clock_ms = s.ms;
    if (s.tick) app.tick();
    else app.handle(s.ev);
  }
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
               "       [--keys \"Up Down PageDown Tab p Click 5,3 Drag 20,6 Release ...\"]\n");
  return 2;
}

std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
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
    app.ensure_transcript_layout();
    run_steps(app, scripted_keys(keys_spec, w, h));
    Frame f = app.render(false);
    if (frame_sgr) {
      std::string bytes = render_full(f, app.depth);
      // A screenshot, not a screen: strip the cursor/clear preamble so it cats cleanly.
      std::fwrite(bytes.data(), 1, bytes.size(), stdout);
      std::printf("\x1b[0m\n");
    } else {
      print_frame_plain(f);
    }
    if (app.copied_any) std::printf("--- copied ---\n%s\n", app.copied.c_str());
    return 0;
  }

  Terminal term(STDIN_FILENO, STDOUT_FILENO);
  if (!term.is_tty()) { std::fprintf(stderr, "not a terminal; use --frame WxH\n"); return 1; }
  app.resize(term.width(), term.height());
  app.load_layout_arg();
  app.ensure_transcript_layout();
  run_steps(app, scripted_keys(keys_spec, app.w, app.h));
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
    const bool ticking = app.transcript.wants_tick();
    for (const Event& e : term.poll(ticking ? 50 : 250)) {
      app.clock_ms = now_ms();
      if (const ResizeEvent* r = std::get_if<ResizeEvent>(&e)) {
        app.resize(r->w, r->h);
        have_prev = false;
        continue;
      }
      if (const KeyEvent* k = std::get_if<KeyEvent>(&e); k && k->key == Key::Char && k->ctrl && k->ch == 'l')
        have_prev = false;
      if (!app.handle(e)) { running = false; break; }
    }
    if (ticking) app.tick();
  }
  return 0;
}
