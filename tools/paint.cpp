//
// rolltui/tools/paint.cpp — `rolltui-paint`, the library's THIRD host (plan/phase-11.md,
// milestone 6) and the phase's proof that a rolltui app need not be chat-shaped.
//
// It is a text-mode painting app: a canvas you drag on, a tool palette that is a menu
// FILE, and NO TRANSCRIPT AND NO INPUT. That absence is the point. roll and the studio
// are both a document with a prompt under it, so every property the library grew could
// quietly have been a property of that shape; this host has neither, and it is one
// screen's worth of code:
//
//   - ONE CLASS, `Canvas`, which is a real `Widget` with data of its own (its pixels) —
//     not a `CallbackWidget`, deliberately, because the mechanism must not depend on the
//     shortcut the library happens to ship for stateless composites.
//   - ONE REGISTRATION, `windows.register_kind("canvas", …)`, after which `canvas:sheet`
//     behaves exactly like `input:prompt`: created on demand, owned by `Windows`, keyed
//     by content, and handed every event the stack routes to its window — INCLUDING the
//     drags that leave the window, because a press captures the pointer (Phase 11 m3).
//   - NOTHING ELSE. There is no name switch, no `custom_at`, and no line anywhere below
//     that knows what the studio is. The screen this app runs in the proof was authored
//     in the studio, by a person who had only this app's PROFILE — and the word for that
//     screen appears in no source file, which `files_only_test`'s grep asserts.
//
// `--profile` is how it publishes itself: the same generated-never-hand-maintained rule
// `roll profile` follows (rolltui/AppProfile.hpp). Its kinds, its menus and its min sizes
// are read from the same places this binary reads them, so a profile cannot drift from
// the app it describes.
//
// `--frame WxH` prints one frame and exits (the studio's convention, and what the tests
// read); `--stroke x,y-x,y` synthesises a press, the drags between the two points and a
// release, so a test can prove the canvas received them without a terminal. Everything
// else is the ordinary interactive loop.
//
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/AppProfile.hpp"
#include "rolltui/Bindings.hpp"
#include "rolltui/Effects.hpp"
#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Terminal.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Widgets.hpp"

using namespace rolltui;

namespace {

// The tool palette. A MENU FILE the app carries in its own binary — the middle of Phase
// 10 m3's three rungs — so a user can shadow it with menus/tools.json and the studio can
// preview it verbatim from the profile.
constexpr const char* kToolsMenu = R"({
  "id": "root", "label": "tools", "items": [
    { "id": "brush", "label": "Brush", "kind": "choice",
      "items": [ { "id": "#", "label": "block  #" }, { "id": "*", "label": "star   *" },
                 { "id": "." , "label": "dot    ." }, { "id": "o", "label": "ring   o" } ] },
    { "id": "clear", "label": "Clear the sheet" } ] }
)";

// The app's own screen, for a run with no --layout: one canvas and the palette beside it.
// A file, in the sense that matters — it is parsed by the same loader as any other, and
// the profile's min sizes are read back OUT of it rather than restated.
constexpr const char* kDefaultLayout = R"({
  "name": "paint", "min_width": 20, "min_height": 6, "focus": "sheet",
  "actions": {},
  "root": { "row": [
    { "id": "sheet", "content": "canvas:sheet", "border": "single", "title": "sheet", "focusable": true },
    { "id": "tools", "content": "menu:tools", "size": 22, "border": "single", "title": "tools", "focusable": true } ] }
})";

// ---- the one class ---------------------------------------------------------------------
// A widget with DATA of its own, which is the case `Widget` exists for and the case a
// two-callback adapter cannot serve: the pixels belong to the canvas, not to a document
// the host bound. It keeps them in its own coordinates, so a resize or a layout reload
// moves the viewport and not the picture.
class Canvas : public Widget {
 public:
  Canvas(std::string sheet, const std::string* brush) : sheet_(std::move(sheet)), brush_(brush) {}

  // A source this app does not have is a NAMED problem and an error panel, exactly as an
  // unbound `rows:` source is — a host's own kind is not exempt from the rule.
  std::string problem() const override {
    return content.source == sheet_ ? std::string() : "nothing is bound to '" + content.source + "'";
  }
  void layout(const ResolvedNode& rn) override { inner_ = rn.inner; }
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Rect r = rn.inner;
    const Style ink = theme.style(Role::text);
    for (const auto& [at, ch] : pixels_) {
      const auto [x, y] = at;
      if (x < 0 || y < 0 || x >= r.w || y >= r.h) continue;  // the picture outlives the viewport
      f.put_text(r.x + x, r.y + y, std::string(1, ch), ink, 1);
    }
  }
  // Press, every Drag, and the Release — the drags past the window's own edge included,
  // because the press captured the pointer. Nothing here clamps to the window: a stroke
  // that leaves the canvas keeps its shape and simply is not drawn until it comes back.
  bool handle(const Event& e) override {
    const MouseEvent* m = std::get_if<MouseEvent>(&e);
    if (!m) return false;
    using K = MouseEvent::Kind;
    if (m->kind != K::Press && m->kind != K::Drag && m->kind != K::Release) return false;
    if (m->kind != K::Release) pixels_[{m->x - inner_.x, m->y - inner_.y}] = brush_->front();
    return true;
  }
  void clear() { pixels_.clear(); }
  std::size_t painted() const { return pixels_.size(); }

 private:
  std::string sheet_;
  const std::string* brush_;  // the host's current tool, read at paint time
  Rect inner_{};
  std::map<std::pair<int, int>, char> pixels_;
};

// ---- the app -----------------------------------------------------------------------------

struct App {
  Theme theme = *builtin_theme("default-dark");
  Bindings bindings = default_bindings();
  Windows windows;
  WindowStack stack;
  Layout layout;
  int w = 80, h = 24;
  std::string brush = "#";
  std::string note;
  // m6: the clock effects are applied at — 0 under --frame, so a frame dump stays a pure
  // function of state; the real one in the event loop.
  std::uint64_t effect_ms = 0;

  Rect area() const { return {0, 0, w, std::max(h - 1, 0)}; }

  void mount() {
    // ONE registration. After this the library builds `canvas:<source>` like any built-in,
    // and this host never sees a window id again.
    windows.register_kind(
        "canvas", [this] { return std::make_unique<Canvas>("sheet", &brush); }, SourceRule::Required,
        "a sheet the app paints on");
    windows.add_menu("tools", kToolsMenu);
    windows.bind_rows("brush", [this](Rows& out) { out.add("brush", brush); out.add("marks", std::to_string(marks())); });
    windows.set_help("", help_scopes(), "");
    stack.set_base(layout.base);
    bindings.declare(action_decls(layout.actions), {});  // the SCREEN says what this app can do (Phase 10 m4)
  }
  static const std::vector<std::string>& help_scopes() {
    // The SCREEN's own actions first: they are the ones a person came to this app for,
    // and they are the ones no source here names.
    static const std::vector<std::string> s = {"app", "menu", "stack"};
    return s;
  }
  Canvas* canvas() { return static_cast<Canvas*>(windows.registered("canvas", "sheet")); }
  std::size_t marks() { return canvas() ? canvas()->painted() : 0; }

  void set_layout(Layout l) {
    layout = std::move(l);
    stack.set_base(layout.base);
    bindings.declare(action_decls(layout.actions), {});
  }

  void prepare() {
    WidgetEnv env;
    env.bindings = &bindings;
    windows.set_env(env);
    const WindowsReport rep = windows.prepare(stack, area());
    note = rep.clean() ? "" : rep.summary();
  }

  void handle(const Event& e) {
    const Route r = stack.route(e, area(), bindings);
    if (r.kind != Route::Kind::Deliver) return;
    if (windows.handle(r.window, e)) return;
    // A menu window is the host's to drive, exactly as in every other host.
    if (RolltuiMenu* m = windows.menu_at(r.window)) {
      const MenuEvent ev = menu_handle(m, e, bindings);
      if (ev.kind == MenuEvent::Kind::Choose && ev.id == "brush" && !ev.value.empty()) brush = ev.value;
      if (ev.kind == MenuEvent::Kind::Activate && ev.id == "clear" && canvas()) canvas()->clear();
    }
  }

  Frame render() {
    prepare();
    Frame f(w, h, theme.style(Role::background));
    stack.compose(f, area(), theme, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, theme); });
    if (h > 1) {
      f.fill({0, h - 1, w, 1}, theme.style(Role::panel_background));
      std::string status = " " + layout.name + "  " + std::to_string(w) + "x" + std::to_string(h) + "  brush " +
                           brush + "  marks " + std::to_string(marks()) + "  focus:" +
                           (stack.focused() ? stack.focused()->id : "-");
      if (!note.empty()) status += "  [" + note + "]";
      f.put_text(0, h - 1, status, theme.style(Role::value), w);
    }
    // Phase 12 m6, in the THIRD host too — one line, and it is the same line roll and the
    // studio have. A paint app marks nothing today, so this frame is unchanged; the point
    // is that a `canvas` that DID mark a span would move here with no library change.
    apply_effects(f, theme, effect_ms);
    return f;
  }
};

// ---- the profile: generated, never hand-maintained ----------------------------------------
// Every part is read from where this binary reads it — the kinds it registers, the menu it
// embeds, and the min sizes of its own screen. The samples are the one thing written here,
// because sample content is the only thing a running app cannot supply.
AppProfile paint_profile() {
  AppProfile p;
  p.app = "paint";
  LayoutLoadReport rep;
  if (const std::optional<Layout> own = load_layout(kDefaultLayout, rep)) {
    p.min_width = own->min_width;
    p.min_height = own->min_height;
    p.actions = action_decls(own->actions);
  }
  p.kinds.push_back({"canvas", SourceRule::Required, "a sheet the app paints on"});
  p.rows.push_back({"brush", {{"brush", "#"}, {"marks", "0"}}});
  p.help.scopes = App::help_scopes();  // the same list this binary hands its own Windows

  p.menus.push_back({"tools", kToolsMenu});
  return p;
}

// ---- plumbing ------------------------------------------------------------------------------

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool parse_size(const std::string& s, int& w, int& h) {
  const std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-paint [--presets DIR] [--layout NAME|FILE] [--theme NAME] [--frame WxH]\n"
               "                     [--stroke X,Y-X,Y]  a press, the drags between, and a release\n"
               "       rolltui-paint --profile [PATH]     write this app's profile (what a layout may name in it)\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string presets_dir, layout_arg, theme_arg = "default-dark", frame_spec, stroke_spec, profile_path;
  bool want_profile = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--presets") presets_dir = next();
    else if (a == "--layout") layout_arg = next();
    else if (a == "--theme") theme_arg = next();
    else if (a == "--frame") frame_spec = next();
    else if (a == "--stroke") stroke_spec = next();
    else if (a == "--profile") { want_profile = true; if (i + 1 < argc && argv[i + 1][0] != '-') profile_path = next(); }
    else return usage();
  }

  if (want_profile) {
    const std::string text = json::dump(app_profile_to_json(paint_profile()), 2) + "\n";
    if (profile_path.empty()) { std::fwrite(text.data(), 1, text.size(), stdout); return 0; }
    std::ofstream out(profile_path, std::ios::binary | std::ios::trunc);
    if (!out) { std::fprintf(stderr, "rolltui-paint: cannot write %s\n", profile_path.c_str()); return 1; }
    out << text;
    return 0;
  }

  App app;
  if (const Theme* t = builtin_theme(theme_arg)) app.theme = *t;
  app.windows.set_dir(presets_dir);

  LayoutLoadReport rep;
  std::optional<Layout> l;
  if (!layout_arg.empty()) {
    const bool path = layout_arg.find('/') != std::string::npos || layout_arg.find(".json") != std::string::npos;
    const std::string file = path ? layout_arg : presets_dir + "/layouts/" + layout_arg + ".json";
    bool ok = false;
    const std::string text = read_file(file, ok);
    if (ok) l = load_layout(text, rep);
    else if (const Layout* b = builtin_layout(layout_arg)) l = *b;
    if (!l) { std::fprintf(stderr, "rolltui-paint: no layout '%s' (%s)\n", layout_arg.c_str(), rep.error.c_str()); return 1; }
  } else {
    l = load_layout(kDefaultLayout, rep);
  }
  app.layout = *l;
  app.mount();
  for (const std::string& b : rep.bad_values) std::fprintf(stderr, "rolltui-paint: %s\n", b.c_str());

  if (!frame_spec.empty()) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    app.prepare();
    if (!stroke_spec.empty()) {
      int x1, y1, x2, y2;
      if (std::sscanf(stroke_spec.c_str(), "%d,%d-%d,%d", &x1, &y1, &x2, &y2) != 4) return usage();
      MouseEvent m;
      m.button = 1;
      m.kind = MouseEvent::Kind::Press;
      m.x = x1;
      m.y = y1;
      app.handle(m);
      const int steps = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
      for (int s = 1; s <= steps; ++s) {
        m.kind = MouseEvent::Kind::Drag;
        m.x = x1 + (x2 - x1) * s / std::max(steps, 1);
        m.y = y1 + (y2 - y1) * s / std::max(steps, 1);
        app.handle(m);
      }
      m.kind = MouseEvent::Kind::Release;
      app.handle(m);
    }
    const Frame f = app.render();
    const std::string text = frame_to_text(f);
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }

  Terminal term(STDIN_FILENO, STDOUT_FILENO);
  if (!term.is_tty()) { std::fprintf(stderr, "not a terminal; use --frame WxH\n"); return 1; }
  app.w = term.width();
  app.h = term.height();
  Frame prev;
  bool have_prev = false;
  for (;;) {
    app.effect_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    Frame f = app.render();
    term.write(render_diff(have_prev ? &prev : nullptr, f, ColorDepth::TrueColor));
    const int timeout = poll_timeout_ms(f, app.theme, 250);
    prev = std::move(f);
    have_prev = true;
    for (const Event& e : term.poll(timeout)) {
      if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
        if (k->ctrl && k->key == Key::Char && k->ch == 'q') return 0;
      }
      if (const ResizeEvent* r = std::get_if<ResizeEvent>(&e)) { app.w = r->w; app.h = r->h; continue; }
      app.handle(e);
    }
  }
}
