//
// sequences_test.cpp — KEYS PRESSED ONE AFTER ANOTHER: a sequence (`g g`) and a count (`5` then Down) bound as a chord is, and the
// panel that says what can follow a key on the way; and the shipped vim preset that stands on them.
//
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read sequences back */
#include "rolltui/c/rolltui_screen.h"
#include "rolltui_test.hpp"

using testkit::check;

namespace {

std::string report_text(const RolltuiBindingsReport& r) {
  std::string out;
  auto add = [&](const RolltuiStr* v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) out += str_of(v[i]) + "; ";
  };
  add(r.bad_chords, r.bad_chords_n);
  add(r.conflicts, r.conflicts_n);
  add(r.unknown_actions, r.unknown_actions_n);
  add(r.undeliverable, r.undeliverable_n);
  add(r.bad_values, r.bad_values_n);
  return out + str_of(r.error);
}

RolltuiBindings* load(std::string_view json, std::string* problems) {
  RolltuiBindings* b = rolltui_bindings_new_seeded();
  RolltuiBindingsReport rep{};
  rolltui_bindings_load_json(b, json.data(), json.size(), ROLLTUI_PROTOCOL_LEGACY, rolltui_bindings_library_scope, nullptr, nullptr,
                             nullptr, &rep);
  if (problems) *problems = report_text(rep);
  rolltui_bindings_report_release(&rep);
  return b;
}

void test_bindings() {
  std::string problems;
  RolltuiBindings* b = load(R"({ "name": "t", "bindings": { "list.first": ["home", "g g"], "list.last": ["shift+g"],
    "list.sort": ["ctrl+x s"] } })", &problems);
  check(problems.find("list.") == std::string::npos, "a sequence loads beside chords [" + problems + "]");
  check(rolltui_bindings_sequence_count(b, "list.first", 10) == 1 && rolltui_bindings_chord_count(b, "list.first", 10) == 1,
        "it is held as a sequence, the chord as a chord");
  RolltuiChord g{};
  g.key = ROLLTUI_KEY_CHAR;
  g.ch = 'g';
  const RolltuiChord gg[2] = {g, g};
  const char* action = nullptr;
  std::size_t alen = 0;
  check(rolltui_bindings_sequence_for(b, gg, 1, "list", 4, nullptr, nullptr) == ROLLTUI_SEQUENCE_PREFIX &&
            rolltui_bindings_sequence_for(b, gg, 2, "list", 4, &action, &alen) == ROLLTUI_SEQUENCE_DONE &&
            std::string(action, alen) == "list.first",
        "g begins it, g g is list.first");
  RolltuiStr text{};
  rolltui_bindings_chords_text(b, "list.first", 10, &text);
  check(str_of(text) == "Home, g g", "the help says it as it is pressed [" + str_of(text) + "]");
  rolltui_bindings_dump_json(b, "t", 1, &text);
  check(str_of(text).find("\"g g\"") != std::string::npos && str_of(text).find("\"ctrl+x s\"") != std::string::npos,
        "it is written back as the file spelled it");
  rolltui_str_free(&text);
  rolltui_bindings_free(b);
  // the ambiguous are refused
  b = load(R"({ "name": "t", "bindings": { "list.mark": ["g"], "list.first": ["g g"] } })", &problems);
  check(problems.find("begins with a chord bound alone to list.mark") != std::string::npos && rolltui_bindings_sequence_count(b, "list.first", 10) == 0,
        "a sequence whose first chord is bound alone is refused [" + problems + "]");
  rolltui_bindings_free(b);
  b = load(R"({ "name": "t", "bindings": { "list.first": ["g g"], "list.mark": ["g"] } })", &problems);
  check(problems.find("begins a sequence of list.first") != std::string::npos, "…whichever comes first in the file");
  rolltui_bindings_free(b);
  b = load(R"({ "name": "t", "bindings": { "list.first": ["g g"], "list.last": ["g g"] } })", &problems);
  check(problems.find("bound to both list.first and list.last") != std::string::npos, "one sequence for two actions is a conflict");
  rolltui_bindings_free(b);
  b = load(R"({ "name": "t", "bindings": { "list.first": ["g g g g g"] } })", &problems);
  check(problems.find("is not a sequence") != std::string::npos, "more than four chords is not a sequence");
  rolltui_bindings_free(b);
}

// ---- the table: a list driven by sequences and counts ----------------------------------------------------------------------------

std::size_t count_fn(void*) { return 100; }
void row_fn(void*, std::size_t i, RolltuiListRow* out) {
  const std::string t = "row " + std::to_string(i);
  rolltui_list_row_cell(out, t.data(), t.size());
}

struct Screen {
  RolltuiContext* ctx = rolltui_context_new();
  RolltuiBindings* b = nullptr;
  RolltuiLayout* layout = nullptr;
  RolltuiWindows* w = nullptr;
  RolltuiWindowStack* stack = nullptr;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = nullptr;
  unsigned long long now = 1000;
  explicit Screen(RolltuiBindings* bindings) : b(bindings) {
    rolltui_context_set_library_defaults(ctx);
    rolltui_context_set_bindings(ctx, b);
    static const char kScreen[] = R"({ "name": "s", "root": { "id": "rows", "content": "list:things", "focusable": true } })";
    RolltuiLayoutReport rep{};
    std::size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &nd);
    layout = rolltui_load_layout_text(kScreen, sizeof kScreen - 1, defaults, nd, rolltui_layout_default_hooks(), &rep);
    rolltui_layout_report_release(&rep);
    w = rolltui_windows_new(ctx);
    stack = rolltui_window_stack_new();
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    rolltui_windows_bind_list(w, "things", 6, count_fn, row_fn, nullptr, nullptr);
    RolltuiListOptions o{};
    o.hide_header = 1;
    rolltui_windows_set_list_options(w, "things", 6, &o);
  }
  Screen(const Screen&) = delete;
  ~Screen() {
    rolltui_effect_map_free(fx);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(w);
    rolltui_layout_free(layout);
    rolltui_bindings_free(b);
    rolltui_context_free(ctx);
  }
  std::string paint() {
    const RolltuiRect area{0, 0, 40, 12};
    RolltuiWidgetEnv env{};
    env.now_ms = now;
    env.still = 1;
    rolltui_context_set_env(ctx, &env);
    rolltui_windows_sync(w, stack);
    rolltui_windows_layout(w, stack, area);
    RolltuiFrame* f = rolltui_frame_new(40, 12, styles[ROLLTUI_ROLE_BACKGROUND]);
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
    rolltui_windows_draw_toasts(w, f, area, styles, now);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    std::string out = str_of(text);
    rolltui_str_free(&text);
    rolltui_compose_scratch_free(cs);
    rolltui_frame_free(f);
    return out;
  }
  void key(char32_t ch, unsigned char k = ROLLTUI_KEY_CHAR) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    e.key.ch = ch;
    rolltui_windows_handle(w, "rows", 4, &e);
  }
  std::string cursor() {
    RolltuiStr k{};
    rolltui_windows_list_cursor(w, "things", 6, &k);
    std::string out = str_of(k);
    rolltui_str_free(&k);
    return out;
  }
  std::string pending() {
    RolltuiStr t{};
    rolltui_windows_pending_keys(w, &t);
    std::string out = str_of(t);
    rolltui_str_free(&t);
    return out;
  }
};

std::string read_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void test_table() {
  std::string problems;
  Screen s(load(read_file(ROLLTUI_SOURCE_DIR "/presets/bindings/vim.json"), &problems));
  check(problems.empty(), "the shipped vim preset loads clean [" + problems + "]");
  s.paint();
  s.key('j');
  s.key('j');
  check(s.cursor() == "#2", "j moves down, as a chord does [" + s.cursor() + "]");
  s.key('5');
  check(s.pending() == "5", "a digit is a count, on the way [" + s.pending() + "]");
  s.key('j');
  check(s.cursor() == "#7" && s.pending().empty(), "5 then j moves five");
  s.key('1');
  s.key('2');
  s.key(0, ROLLTUI_KEY_DOWN);
  check(s.cursor() == "#19", "12 then Down, a chord the count repeats, moves twelve");
  s.key('G');
  check(s.cursor() == "#99", "G is the last row");
  s.key('g');
  check(s.pending() == "g" && s.cursor() == "#99", "g alone is on the way, and moves nothing");
  std::string frame = s.paint();
  check(frame.find("first row") == std::string::npos, "the panel of what can follow waits a moment, for a sequence typed quickly");
  s.now += 400;
  frame = s.paint();
  check(frame.find("the first row") != std::string::npos, "…then says what can follow: g, the first row [" + frame.substr(frame.size() > 400 ? frame.size() - 400 : 0) + "]");
  s.key('g');
  check(s.cursor() == "#0" && s.pending().empty(), "g g is the first row");
  frame = s.paint();
  check(frame.find("the first row") == std::string::npos, "…and the panel goes");
  s.key('g');
  s.key(0, ROLLTUI_KEY_ESCAPE);
  check(s.pending().empty() && s.cursor() == "#0", "Escape lets go of what was on the way");
  s.key('g');
  s.key('x');
  check(s.pending().empty(), "a key no sequence goes on with ends it");
  s.key('/');
  s.key('3');
  check(s.pending().empty(), "a digit typed into the list's filter is the filter's, never a count");
}

// A HOST'S OWN SCOPE: `app.save` on `ctrl+x ctrl+s`, as the guide spells it, through `rolltui_windows_app_key`.
void test_app_scope() {
  std::string problems;
  Screen s(load(R"({ "name": "t", "bindings": { "app.save": ["ctrl+x ctrl+s"], "app.quit": ["ctrl+q"], "list.first": ["g g"] } })",
                &problems));
  RolltuiToolAction declared[2] = {{"app.save", "Save", nullptr}, {"app.quit", "Quit", nullptr}};
  rolltui_bindings_declare(s.b, nullptr, 0, declared, 2); /* a host's actions are the ones its screen declares */
  s.paint();
  auto app = [&](char32_t ch, bool ctrl, std::string* action) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = ROLLTUI_KEY_CHAR;
    e.key.ch = ch;
    e.key.ctrl = ctrl ? 1 : 0;
    const char* a = nullptr;
    std::size_t n = 0;
    const int r = rolltui_windows_app_key(s.w, &e, "app", 3, &a, &n);
    if (action) *action = a ? std::string(a, n) : std::string();
    return r;
  };
  std::string a;
  check(app('x', true, &a) == ROLLTUI_SEQUENCE_PREFIX && s.pending() == "Ctrl-X", "Ctrl-X begins the host's sequence, and is shown on the way");
  check(app('s', true, &a) == ROLLTUI_SEQUENCE_DONE && a == "app.save" && s.pending().empty(), "…Ctrl-S finishes it: app.save");
  check(app('q', true, &a) == ROLLTUI_SEQUENCE_DONE && a == "app.quit", "a chord of the scope alone is done at once");
  check(app('a', false, &a) == ROLLTUI_SEQUENCE_NONE, "a key the scope takes no part in is the windows'");
  app('x', true, nullptr);
  check(app('z', false, &a) == ROLLTUI_SEQUENCE_NONE && s.pending().empty(), "a key no sequence goes on with ends it, and is what it is alone");
  s.key('g');
  check(app('g', false, &a) == ROLLTUI_SEQUENCE_NONE && s.pending() == "g", "a sequence on the way in a window is left to the window");
  s.key('g');
  check(s.cursor() == "#0", "…which finishes it");
}

// THE WINDOW WITH FOCUS FIRST. A program's own keys never take a key the focused window has for itself: being unable to use the
// window in front of you is a hard break. The list here has Ctrl-P for the row above and Escape to go back from its filter; the
// program has Ctrl-P for its palette and Escape to stop.
void test_focus_first() {
  std::string problems;
  Screen s(load(R"({ "name": "t", "bindings": { "app.palette": ["ctrl+p"], "app.stop": ["escape"], "app.quit": ["ctrl+q"],
                     "list.up": ["ctrl+p"], "list.back": ["escape"], "list.filter": ["/"] } })",
                &problems));
  RolltuiToolAction declared[3] = {{"app.palette", "Palette", nullptr}, {"app.stop", "Stop", nullptr}, {"app.quit", "Quit", nullptr}};
  rolltui_bindings_declare(s.b, nullptr, 0, declared, 3);
  s.paint();
  auto app = [&](unsigned char key, char32_t ch, bool ctrl, std::string* action) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = key;
    e.key.ch = ch;
    e.key.ctrl = ctrl ? 1 : 0;
    const char* a = nullptr;
    std::size_t n = 0;
    const int r = rolltui_windows_app_key(s.w, &e, "app", 3, &a, &n);
    if (action) *action = a ? std::string(a, n) : std::string();
    return r;
  };
  std::string a;
  RolltuiChord ctrl_p{};
  ctrl_p.key = ROLLTUI_KEY_CHAR;
  ctrl_p.ch = 'p';
  ctrl_p.ctrl = 1;
  check(rolltui_windows_focused_takes(s.w, &ctrl_p) == 1, "the focused list takes Ctrl-P: its scope binds it");
  check(app(ROLLTUI_KEY_CHAR, 'p', true, &a) == ROLLTUI_SEQUENCE_NONE && a.empty(), "…so the program's palette does not get it");
  check(app(ROLLTUI_KEY_CHAR, 'q', true, &a) == ROLLTUI_SEQUENCE_DONE && a == "app.quit",
        "a key the list has nothing for is the program's (the control)");
  check(app(ROLLTUI_KEY_ESCAPE, 0, false, &a) == ROLLTUI_SEQUENCE_DONE && a == "app.stop",
        "Escape with nothing in the list to go back from is the program's");
  s.key('/');
  s.key('x');
  check(app(ROLLTUI_KEY_ESCAPE, 0, false, &a) == ROLLTUI_SEQUENCE_NONE, "…and with a filter typed it is the list's, to clear the filter");
  check(app(ROLLTUI_KEY_CHAR, 'q', false, &a) == ROLLTUI_SEQUENCE_NONE, "what is typed into the filter is the filter's");
}

}  // namespace

int main() {
  test_bindings();
  test_table();
  test_app_scope();
  test_focus_first();
  return testkit::report("sequences_test");
}
