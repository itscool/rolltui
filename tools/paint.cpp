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
//   - ONE STRUCT, `Canvas`, which is a real widget PLUGIN with data of its own (its
//     pixels) — not a rows/text binding, deliberately, because the mechanism must not
//     depend on the shortcut the library happens to ship for stateless composites.
//   - ONE REGISTRATION, `register_canvas_kind()`, after which `canvas:sheet` behaves
//     exactly like `input:prompt`: created on demand, owned by `RolltuiWindows`, keyed
//     by content, and handed every event the stack routes to its window — INCLUDING the
//     drags that leave the window, because a press captures the pointer (Phase 11 m3).
//   - NOTHING ELSE. There is no name switch, no `custom_at`, and no line anywhere below
//     that knows what the studio is. The screen this app runs in the proof was authored
//     in the studio, by a person who had only this app's PROFILE — and the word for that
//     screen appears in no source file, which `files_only_test`'s grep asserts.
//
// `--profile` is how it publishes itself: the same generated-never-hand-maintained rule
// `roll profile` follows. Its kinds, its menus and its min sizes are read from the same
// places this binary reads them, so a profile cannot drift from the app it describes.
//
// `--frame WxH` prints one frame and exits (the studio's convention, and what the tests
// read); `--stroke x,y-x,y` synthesises a press, the drags between the two points and a
// release, so a test can prove the canvas received them without a terminal. Everything
// else is the ordinary interactive loop.
//
// ============================================================================================
// PHASE 17 m3: THIS FILE CALLS THE C, AND IT IS THE FIRST HOST TO — so what it holds is the
// answer the other five copy rather than each invent. Three decisions, all forced by the
// plan's own measurements rather than chosen here:
//
//   1. **NO PER-FRAME FRAME AT ALL.** `rolltui_swap` owns both frames for the whole run and
//      lends the back one per repaint. `Frame prev; bool have_prev;` — which all three hosts
//      had written identically — is gone, and with it the ONE hot RAII site a host had
//      (m4's measurement). This is why the swap could not be a bolt-on: `rolltui::Frame` had
//      no borrowing constructor, so adopting it IS the draw path's C transition.
//   2. **APP-LIFETIME HANDLES ARE PLAIN MEMBERS RELEASED IN ONE DESTRUCTOR.** Not a
//      `Handle<T, New, Free>` template — that is the wrapper `rolltui.h` rule 5 forbids
//      three hosts from each writing. `App` owning its own resources is ordinary C++ and is
//      ONE place; a missed release leaks once and `rolltui_shutdown`'s `live_bytes == 0` is
//      what catches it (m4: app lifetime is "the easy 90% and it needs no machinery").
//   3. **NO JSON TYPE ANYWHERE.** `--profile` used to be `json::dump(app_profile_to_json(p))`
//      — the cleanest evidence in the phase that `json::Value` was leaking through the C++
//      surface, since paint depended on the parser only because a profile came back as a
//      tree. `rolltui_app_profile_dump` hands back TEXT, so the dependency is gone by
//      construction rather than by a decision.
//
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

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

constexpr const char* kCanvasKind = "canvas";
constexpr const char* kCanvasSource = "sheet";
constexpr const char* kCanvasDescribes = "a sheet the app paints on";

// ---- the one widget ----------------------------------------------------------------------
// A plugin with DATA of its own, which is the case the vtable exists for and the case a
// two-callback adapter cannot serve: the pixels belong to the canvas, not to a document
// the host bound. It keeps them in its own coordinates, so a resize or a layout reload
// moves the viewport and not the picture.
//
// Three of the nine slots are filled: `destroy`, `layout` and `draw` are required, plus
// `problem` (a source this app does not have is a NAMED problem and an error panel, exactly
// as an unbound `rows:` source is — a host's own kind is not exempt) and `handle`. The other
// four are NULL, and `rolltui_widgets.h` states what each NULL means; that is the whole
// difference from a virtual nobody was asked about.
struct Canvas {
  std::string source;            // what the layout named after the colon
  const std::string* brush;      // the host's current tool, read at paint time — BORROWED
  RolltuiWindows* windows;       // BORROWED: where `draw` asks for the frame's style table
  RolltuiDrawScratch* draw_scratch = nullptr;
  RolltuiRect inner{};
  std::map<std::pair<int, int>, char> pixels;
};

void canvas_destroy(void* ctx) {
  Canvas* c = static_cast<Canvas*>(ctx);
  rolltui_draw_scratch_free(c->draw_scratch);
  delete c;
}

int canvas_problem(void* ctx, RolltuiStr* out) {
  const Canvas* c = static_cast<const Canvas*>(ctx);
  if (c->source == kCanvasSource) return 0;
  const std::string why = "nothing is bound to '" + c->source + "'";
  rolltui_str_set(out, why.data(), why.size());
  return 1;
}

void canvas_layout(void* ctx, const RolltuiResolvedNode* rn) { static_cast<Canvas*>(ctx)->inner = rn->inner; }

void canvas_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Canvas* c = static_cast<Canvas*>(ctx);
  const RolltuiRect r = rn->inner;
  // The frame's style table for THIS draw, which is what lets a widget ask "what does
  // Role::text look like" without the vtable carrying a fourth parameter every kind must
  // accept (`rolltui_widgets.h`'s note on `rolltui_windows_styles`).
  const RolltuiStyle* styles = rolltui_windows_styles(c->windows);
  const RolltuiStyle ink = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  for (const auto& [at, ch] : c->pixels) {
    const auto [x, y] = at;
    if (x < 0 || y < 0 || x >= r.w || y >= r.h) continue;  // the picture outlives the viewport
    rolltui_frame_put_text(f, c->draw_scratch, r.x + x, r.y + y, &ch, 1, ink, 1, 0, 0);
  }
}

// Press, every Drag, and the Release — the drags past the window's own edge included,
// because the press captured the pointer. Nothing here clamps to the window: a stroke
// that leaves the canvas keeps its shape and simply is not drawn until it comes back.
int canvas_handle(void* ctx, const RolltuiEvent* e) {
  Canvas* c = static_cast<Canvas*>(ctx);
  if (e->kind != ROLLTUI_EVENT_MOUSE) return 0;
  // `RolltuiMouseEvent::Kind` is spelled per language (rolltui_keys.h): the scoped enum in
  // C++, eight bare bytes plus a comment in C. Worth recording for m4 rather than fixing
  // here — this host is C++ and names them, but a pure-C consumer has no word for any of the
  // eight, which is the shape `ROLLTUI_ROLE_LIST` fixed one level up.
  using K = RolltuiMouseEvent::Kind;
  const K k = e->mouse.kind;
  if (k != K::Press && k != K::Drag && k != K::Release) return 0;
  if (k != K::Release) c->pixels[{e->mouse.x - c->inner.x, e->mouse.y - c->inner.y}] = c->brush->front();
  return 1;
}

// The plugin is a BORROW of a table the implementor keeps — one `static const` per kind, the
// shape `rolltui_widgets.h` states and the library's own eight already use.
constexpr RolltuiWidgetPlugin kCanvasPlugin = {
    /*destroy=*/canvas_destroy,
    /*layout=*/canvas_layout,
    /*draw=*/canvas_draw,
    /*problem=*/canvas_problem,
    /*note_at=*/nullptr,
    /*desired_outer=*/nullptr,
    /*handle=*/canvas_handle,
    /*scroll_extent=*/nullptr,
    /*scroll_to=*/nullptr,
};

// What the factory is registered WITH: the two borrows a canvas needs and nothing else. One
// per app, held by `App`, so the factory's `ctx` outlives every widget it builds.
struct CanvasFactoryCtx {
  const std::string* brush;
  RolltuiWindows* windows;
};

RolltuiWidget canvas_factory(void* ctx, const char* content, std::size_t len) {
  const CanvasFactoryCtx* fc = static_cast<const CanvasFactoryCtx*>(ctx);
  const char* source = nullptr;
  std::size_t source_len = 0;
  RolltuiStr why{};
  unsigned char problem = 0;
  // A content the registry cannot parse is not an error path — a zeroed widget means "I
  // cannot build this", and `Windows` draws the error panel and names it in the report.
  if (!rolltui_content_parse(content, len, nullptr, nullptr, nullptr, nullptr, &source, &source_len, &problem, &why))
    return RolltuiWidget{};
  Canvas* c = new Canvas{std::string(source, source_len), fc->brush, fc->windows, rolltui_draw_scratch_new(), {}, {}};
  return RolltuiWidget{&kCanvasPlugin, c};
}

// ---- the app -----------------------------------------------------------------------------

// APP LIFETIME, RELEASED IN ONE DESTRUCTOR — see the header note. Every handle below is
// created once here and freed once there; none of them is per-frame, which is why no wrapper
// type is needed for any of them.
struct App {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  RolltuiEffectScratch* effect_scratch = rolltui_effect_scratch_new();
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  RolltuiBindings* bindings = rolltui_bindings_clone(rolltui_bindings_default());
  RolltuiWindows* windows = rolltui_windows_new();
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  RolltuiLayout layout{};
  CanvasFactoryCtx factory_ctx{};
  int w = 80, h = 24;
  std::string brush = "#";
  std::string note;
  // m6: the clock effects are applied at — 0 under --frame, so a frame dump stays a pure
  // function of state; the real one in the event loop.
  std::uint64_t effect_ms = 0;

  App() {
    rolltui_layout_init(&layout);
    // The eight built-in kinds and the five vocabularies they draw with. A bare
    // `rolltui_windows_new()` has neither (m1c's recorded gap, closed in m2a) — this is the
    // one line that makes a pure-C window table usable, and every host calls it.
    rolltui_windows_set_library_defaults(windows);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
    rolltui_layout_release(&layout);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_bindings_free(bindings);
    rolltui_draw_scratch_free(draw_scratch);
    rolltui_effect_scratch_free(effect_scratch);
    rolltui_effect_map_free(effects);
  }

  RolltuiRect area() const { return {0, 0, w, h > 1 ? h - 1 : 0}; }
  const RolltuiStyle& style(unsigned char role) const {
    return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role);
  }

  void set_theme(const char* name) {
    rolltui_effect_map_free(effects);
    effects = rolltui_theme_builtin_fill(name, std::strlen(name), styles, ROLLTUI_ROLE_COUNT);
  }

  void mount() {
    // ONE registration, in the two halves `rolltui_widgets.h` describes: the NAME with the
    // layout vocabulary (rung 2, which refuses a library name) and the FACTORY with the
    // window table. After this the library builds `canvas:<source>` like any built-in, and
    // this host never sees a window id again.
    factory_ctx = {&brush, windows};
    register_canvas_kind();
    rolltui_windows_register_kind(windows, kCanvasKind, std::strlen(kCanvasKind), canvas_factory, &factory_ctx,
                                  nullptr);
    rolltui_windows_add_menu(windows, "tools", 5, kToolsMenu, std::strlen(kToolsMenu));
    rolltui_windows_bind_rows(
        windows, "brush", 5,
        [](void* ctx, RolltuiRows* out) {
          App& a = *static_cast<App*>(ctx);
          rolltui_rows_add(out, "brush", 5, a.brush.data(), a.brush.size());
          const std::string marks = std::to_string(a.marks());
          rolltui_rows_add(out, "marks", 5, marks.data(), marks.size());
        },
        this, nullptr);
    set_help_scopes();
    rolltui_window_stack_set_base(stack, &layout.base);
    declare_actions();  // the SCREEN says what this app can do (Phase 10 m4)
  }

  // The library's own kind table is process-wide, so registering twice (the app, and the
  // profile writer) has to be idempotent — `rolltui_widget_kind_register` says so itself by
  // refusing only a name already registered with ANOTHER source rule.
  static void register_canvas_kind() {
    rolltui_widget_kind_register(kCanvasKind, std::strlen(kCanvasKind), ROLLTUI_SOURCE_REQUIRED, kCanvasDescribes,
                                 std::strlen(kCanvasDescribes));
  }

  // The SCREEN's own actions first: they are the ones a person came to this app for,
  // and they are the ones no source here names.
  static const std::vector<std::string>& help_scopes() {
    static const std::vector<std::string> s = {"app", "menu", "stack"};
    return s;
  }
  void set_help_scopes() {
    rolltui_windows_set_help(windows, "", 0, "", 0);
    rolltui_windows_clear_help_scopes(windows);
    for (const std::string& s : help_scopes()) rolltui_windows_add_help_scope(windows, s.data(), s.size());
  }
  // `RolltuiLayout::actions` is already the flat array `rolltui_bindings_declare` takes, so
  // the `action_decls()` conversion the C++ shim needed has no counterpart here at all.
  void declare_actions() { rolltui_bindings_declare(bindings, layout.actions.v, layout.actions.n, nullptr, 0); }

  // The app's own widget, by the CONTENT the library would key it under — composed by
  // `rolltui_content_format` from the same two words registered above rather than spelled a
  // third time, since the join rule (a colon exactly when the source rule says so) is the
  // library's and not this host's to restate. `vt == &kCanvasPlugin` is what makes the cast
  // safe: `widget_for` hands back an opaque {vt, ctx} and this host only ever recognises its
  // own table.
  Canvas* canvas() {
    RolltuiStr content{};
    rolltui_content_format(kCanvasKind, std::strlen(kCanvasKind), kCanvasSource, std::strlen(kCanvasSource),
                           ROLLTUI_SOURCE_REQUIRED, &content);
    RolltuiWidget* w = rolltui_windows_widget_for(windows, content.p ? content.p : "", content.n);
    rolltui_str_free(&content);
    return w && w->vt == &kCanvasPlugin ? static_cast<Canvas*>(w->ctx) : nullptr;
  }
  std::size_t marks() {
    const Canvas* c = canvas();
    return c ? c->pixels.size() : 0;
  }

  void set_layout(RolltuiLayout l) {
    rolltui_layout_release(&layout);
    layout = l;  // MOVED: `l` was filled by `rolltui_loaded_layout_to_layout` and is not released
    rolltui_window_stack_set_base(stack, &layout.base);
    declare_actions();
  }

  void prepare() {
    const RolltuiWidgetEnv env{0, effect_ms};
    rolltui_windows_set_env(windows, &env);
    rolltui_windows_set_bindings(windows, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, area());
    rolltui_windows_layout(windows, stack, area());
    if (rolltui_windows_report_count(windows) == 0) {
      note.clear();
    } else {
      RolltuiStr s{};
      rolltui_windows_report_summary(windows, &s);
      note.assign(s.p ? s.p : "", s.n);
      rolltui_str_free(&s);
    }
  }

  void handle(const RolltuiEvent& e) {
    RolltuiStr window{};
    const unsigned char kind =
        rolltui_window_stack_route(stack, &e, area(), bindings, rolltui_stack_default_actions(), &window);
    const std::string target = window.str();
    rolltui_str_free(&window);
    if (kind != ROLLTUI_ROUTE_DELIVER) return;
    if (rolltui_windows_handle(windows, target.data(), target.size(), &e)) return;
    // A menu window is the host's to drive, exactly as in every other host.
    if (RolltuiMenu* m = rolltui_windows_menu_at(windows, target.data(), target.size())) {
      RolltuiMenuEvent ev{};
      rolltui_menu_handle(m, &e, bindings, rolltui_menu_default_actions(), &ev);
      if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && ev.id.view() == "brush" && ev.value.n != 0) brush = ev.value.str();
      if (ev.kind == ROLLTUI_MENU_EVENT_ACTIVATE && ev.id.view() == "clear" && canvas()) canvas()->pixels.clear();
      rolltui_menu_event_release(&ev);
    }
  }

  // Draws into the frame the swap LENT — this host owns no frame at all, which is the whole
  // of what adopting `rolltui_swap` bought (see the header note).
  void render_into(RolltuiFrame* f) {
    prepare();
    rolltui_window_stack_compose(stack, f, area(), styles, rolltui_layout_default_roles(), draw_slot, this,
                                 /*ambiguous_wide=*/0, compose_scratch);
    if (h > 1) {
      rolltui_frame_fill(f, draw_scratch, RolltuiRect{0, h - 1, w, 1}, style(ROLLTUI_ROLE_PANEL_BACKGROUND),
                         nullptr, 0);
      const RolltuiLayoutNode* focused = rolltui_window_stack_focused(stack);
      std::string status = " " + layout.name.str() + "  " + std::to_string(w) + "x" + std::to_string(h) + "  brush " +
                           brush + "  marks " + std::to_string(marks()) + "  focus:" +
                           (focused ? focused->id.str() : std::string("-"));
      if (!note.empty()) status += "  [" + note + "]";
      rolltui_frame_put_text(f, draw_scratch, 0, h - 1, status.data(), status.size(), style(ROLLTUI_ROLE_VALUE), w, 0,
                             0);
    }
    // Phase 12 m6, in the THIRD host too — one line, and it is the same line roll and the
    // studio have. A paint app marks nothing today, so this frame is unchanged; the point
    // is that a `canvas` that DID mark a span would move here with no library change.
    if (rolltui_frame_mark_count(f) != 0 && effects && !rolltui_effect_map_empty(effects)) {
      RolltuiEffectReport rep{};
      rolltui_effects_apply(f, effect_scratch, styles, nullptr, effects, effect_ms, 0, &rep, nullptr, nullptr);
    }
  }

  static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    App& a = *static_cast<App*>(ctx);
    rolltui_windows_draw(a.windows, rn, f, a.styles, rolltui_windows_default_roles());
  }

  // The redraw interval this frame's motion asks for, or `idle_ms` when nothing moves — the
  // whole of a host's animation loop (`rolltui_effects.h`).
  int poll_timeout_ms(const RolltuiFrame* f, int idle_ms) const {
    if (!f || rolltui_frame_mark_count(f) == 0 || !effects || rolltui_effect_map_empty(effects)) return idle_ms;
    const int tick = rolltui_effects_tick_ms(f, effects);
    if (tick <= 0) return idle_ms;
    return idle_ms <= 0 ? tick : (idle_ms < tick ? idle_ms : tick);
  }
};

// ---- the profile: generated, never hand-maintained ----------------------------------------
// Every part is read from where this binary reads it — the kinds it registers, the menu it
// embeds, and the min sizes of its own screen. The samples are the one thing written here,
// because sample content is the only thing a running app cannot supply.
//
// The caller OWNS what this returns.
RolltuiAppProfile* paint_profile() {
  RolltuiAppProfile* p = rolltui_app_profile_new();
  rolltui_app_profile_set_app(p, "paint", 5);
  RolltuiLoadedLayout loaded{};
  RolltuiLayoutReport rep{};
  rolltui_loaded_layout_init(&loaded);
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(&defaults_n);
  if (rolltui_load_layout_text(kDefaultLayout, std::strlen(kDefaultLayout), &loaded, defaults, defaults_n,
                               rolltui_layout_default_hooks(), &rep)) {
    RolltuiLayout own{};
    rolltui_layout_init(&own);
    rolltui_loaded_layout_to_layout(&loaded, &own);
    rolltui_app_profile_set_min_size(p, own.min_width, own.min_height);
    for (std::size_t i = 0; i < own.actions.n; ++i)
      rolltui_app_profile_add_action(p, own.actions.v[i].name.p, own.actions.v[i].name.n,
                                     own.actions.v[i].description.p, own.actions.v[i].description.n);
    rolltui_layout_release(&own);
  }
  rolltui_loaded_layout_release(&loaded);
  rolltui_layout_report_release(&rep);

  rolltui_app_profile_add_kind(p, kCanvasKind, std::strlen(kCanvasKind), ROLLTUI_SOURCE_REQUIRED, kCanvasDescribes,
                               std::strlen(kCanvasDescribes));
  const std::size_t row = rolltui_app_profile_add_row(p, "brush", 5);
  rolltui_app_profile_row_add_sample(p, row, "brush", 5, "#", 1);
  rolltui_app_profile_row_add_sample(p, row, "marks", 5, "0", 1);
  // the same list this binary hands its own Windows
  for (const std::string& s : App::help_scopes()) rolltui_app_profile_add_help_scope(p, s.data(), s.size());

  rolltui_app_profile_add_menu(p, "tools", 5, kToolsMenu, std::strlen(kToolsMenu));
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

// One load, from TEXT, to the enduring `RolltuiLayout` a host holds. The report is the
// caller's to read and release; the loaded carrier is never retained past the call, which is
// `rolltui_loaded_layout_to_layout`'s own contract.
bool load_layout_text(std::string_view text, RolltuiLayout* out, RolltuiLayoutReport* rep) {
  RolltuiLoadedLayout loaded{};
  rolltui_loaded_layout_init(&loaded);
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(&defaults_n);
  const bool ok = rolltui_load_layout_text(text.data(), text.size(), &loaded, defaults, defaults_n,
                                           rolltui_layout_default_hooks(), rep) != 0;
  if (ok) rolltui_loaded_layout_to_layout(&loaded, out);
  rolltui_loaded_layout_release(&loaded);
  return ok;
}

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-paint [--presets DIR] [--layout NAME|FILE] [--theme NAME] [--frame WxH]\n"
               "                     [--stroke X,Y-X,Y]  a press, the drags between, and a release\n"
               "       rolltui-paint --profile [PATH]     write this app's profile (what a layout may name in it)\n");
  return 2;
}

// ONE FRAME'S INPUT, held by the loop and refilled — never rebuilt per frame, so the three
// arrays keep their capacity for the life of the run (CLAUDE.md's strategy 3, caller-filled).
//
// It exists because `rolltui_terminal_poll`'s events are BORROWED for the length of the emit
// call (`rolltui_terminal.h` rule 3): a handler must not run inside the decoder — handling one
// event can resize the app — so the bytes are copied here first and acted on after.
struct Pending {
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
  std::vector<RolltuiEvent> events;
  std::vector<std::string> texts;    // the borrowed bytes, owned for this frame
  std::vector<std::size_t> text_of;  // events[i]'s entry in `texts`, or kNone
  bool quit = false;
  int w = 0, h = 0;
  bool resized = false;

  void begin(int cur_w, int cur_h) {
    events.clear();
    texts.clear();
    text_of.clear();
    quit = false;
    resized = false;
    w = cur_w;
    h = cur_h;
  }
};

void collect_event(void* ctx, const RolltuiTermEvent* e) {
  Pending& p = *static_cast<Pending*>(ctx);
  if (e->kind == ROLLTUI_TERM_EVENT_RESIZE) {
    p.w = e->w;
    p.h = e->h;
    p.resized = true;
    return;
  }
  if (e->kind == ROLLTUI_TERM_EVENT_KEY && e->key.ctrl && e->key.key == ROLLTUI_KEY_CHAR && e->key.ch == 'q') {
    p.quit = true;
    return;
  }
  RolltuiEvent ev{};
  ev.kind = e->kind;
  ev.key = e->key;
  ev.mouse = e->mouse;
  // `text` is patched by the caller once `texts` has stopped growing — an INDEX is recorded
  // here rather than the length, because an empty-but-present `text` is a real state (an
  // Unknown key with no bytes left) and keying off the length would desynchronise the two
  // vectors the moment one appeared.
  std::size_t slot = Pending::kNone;
  if (e->text) {
    slot = p.texts.size();
    p.texts.emplace_back(e->text, e->text_len);
    ev.text_len = e->text_len;
  }
  p.text_of.push_back(slot);
  p.events.push_back(ev);
}

RolltuiEvent mouse_event(RolltuiMouseEvent::Kind kind, int x, int y) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse.kind = kind;
  e.mouse.button = 1;
  e.mouse.x = x;
  e.mouse.y = y;
  return e;
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
    // The kind registry is what a profile's `kinds` list is READ from, so it has to be
    // registered before one is written — the same "generated from where the binary reads it"
    // rule the rest of `paint_profile` follows.
    App::register_canvas_kind();
    RolltuiAppProfile* p = paint_profile();
    RolltuiStr dumped{};
    rolltui_app_profile_dump(p, 2, &dumped);
    const std::string text = std::string(dumped.p ? dumped.p : "", dumped.n) + "\n";
    rolltui_str_free(&dumped);
    rolltui_app_profile_free(p);
    if (profile_path.empty()) {
      std::fwrite(text.data(), 1, text.size(), stdout);
      return 0;
    }
    std::ofstream out(profile_path, std::ios::binary | std::ios::trunc);
    if (!out) { std::fprintf(stderr, "rolltui-paint: cannot write %s\n", profile_path.c_str()); return 1; }
    out << text;
    return 0;
  }

  App app;
  app.set_theme(theme_arg.c_str());
  if (!app.effects) app.set_theme("default-dark");  // an unknown --theme keeps the app's own look
  rolltui_windows_set_dir(app.windows, presets_dir.data(), presets_dir.size());

  RolltuiLayoutReport rep{};
  RolltuiLayout loaded{};
  rolltui_layout_init(&loaded);
  bool have = false;
  if (!layout_arg.empty()) {
    const bool path = layout_arg.find('/') != std::string::npos || layout_arg.find(".json") != std::string::npos;
    const std::string file = path ? layout_arg : presets_dir + "/layouts/" + layout_arg + ".json";
    bool ok = false;
    const std::string text = read_file(file, ok);
    if (ok) {
      have = load_layout_text(text, &loaded, &rep);
    } else {
      std::size_t n = 0;
      if (const char* builtin = rolltui_layout_builtin_json(layout_arg.data(), layout_arg.size(), &n))
        if (n != 0) have = load_layout_text(std::string_view(builtin, n), &loaded, &rep);
    }
    if (!have) {
      std::fprintf(stderr, "rolltui-paint: no layout '%s' (%s)\n", layout_arg.c_str(), rep.error.c_str());
      rolltui_layout_release(&loaded);
      rolltui_layout_report_release(&rep);
      return 1;
    }
  } else {
    have = load_layout_text(kDefaultLayout, &loaded, &rep);
  }
  app.set_layout(loaded);
  app.mount();
  for (std::size_t i = 0; i < rep.bad_values_n; ++i)
    std::fprintf(stderr, "rolltui-paint: %s\n", rep.bad_values[i].c_str());
  rolltui_layout_report_release(&rep);

  if (!frame_spec.empty()) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    app.prepare();
    if (!stroke_spec.empty()) {
      int x1, y1, x2, y2;
      if (std::sscanf(stroke_spec.c_str(), "%d,%d-%d,%d", &x1, &y1, &x2, &y2) != 4) return usage();
      app.handle(mouse_event(RolltuiMouseEvent::Kind::Press, x1, y1));
      const int steps = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
      for (int s = 1; s <= steps; ++s)
        app.handle(mouse_event(RolltuiMouseEvent::Kind::Drag, x1 + (x2 - x1) * s / std::max(steps, 1),
                               y1 + (y2 - y1) * s / std::max(steps, 1)));
      app.handle(mouse_event(RolltuiMouseEvent::Kind::Release, x2, y2));
    }
    // Even the one-shot path goes through the swap: it is the only place a frame is made, so
    // there is exactly one draw path rather than a printing one beside a presenting one.
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    std::fwrite(text.p ? text.p : "", 1, text.n, stdout);
    rolltui_str_free(&text);
    rolltui_swap_free(swap);
    return 0;
  }

  RolltuiTerminalOptions opts{};
  RolltuiTerminal* term = rolltui_terminal_new(STDIN_FILENO, STDOUT_FILENO, opts);
  if (!rolltui_terminal_is_tty(term)) {
    rolltui_terminal_free(term);
    std::fprintf(stderr, "not a terminal; use --frame WxH\n");
    return 1;
  }
  app.w = rolltui_terminal_width(term);
  app.h = rolltui_terminal_height(term);

  // THE DOUBLE BUFFER IS THE LIBRARY'S (Phase 17 m4, adopted here in m3, its first caller).
  // What this replaces in all three hosts: `Frame prev; bool have_prev; … Frame f =
  // render(); write(render_diff(have_prev ? &prev : nullptr, f)); prev = std::move(f);`.
  // Two frames for the whole run, nothing owned inside the loop, and `begin` calls
  // `rolltui_frame_reset` — so this repaint lands INSIDE the budget rather than beside it.
  RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
  RolltuiStr out{};  // the caller's buffer, kept across frames and refilled — rule 3(b)
  Pending pending;   // likewise: one per run, refilled — see its definition
  int rc = 0;
  for (;;) {
    app.effect_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    const int timeout = app.poll_timeout_ms(f, 250);
    out.clear();
    rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &out);
    rolltui_terminal_write(term, out.p ? out.p : "", out.n);

    pending.begin(app.w, app.h);
    rolltui_terminal_poll(term, timeout, collect_event, &pending);
    if (pending.quit) break;
    // The pointers only now, because `texts` reallocating would have dangled every earlier one.
    for (std::size_t i = 0; i < pending.events.size(); ++i)
      if (pending.text_of[i] != Pending::kNone) pending.events[i].text = pending.texts[pending.text_of[i]].data();
    for (const RolltuiEvent& e : pending.events) app.handle(e);
    if (pending.resized) {
      app.w = pending.w;
      app.h = pending.h;
    }
  }
  rolltui_str_free(&out);
  rolltui_swap_free(swap);
  rolltui_terminal_free(term);
  return rc;
}
