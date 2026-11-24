//
// feedback_test.cpp — WHAT THE APP TELLS THE PERSON: a gauge, a spinner, the message history and its toasts.
//
// Every case composes a real screen from a layout file and reads the frame back as text, so what is asserted is what a person sees.
//
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h" /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN, to read cells back */
#include "rolltui_test.hpp"

using testkit::check;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

struct Screen {
  RolltuiLayout* l = nullptr;
  explicit Screen(RolltuiLayout* p) : l(p) {}
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;
  ~Screen() { rolltui_layout_free(l); }
};

Screen load(std::string_view json) {
  RolltuiLayoutReport rep{};
  std::size_t n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &n);
  RolltuiLayout* l = rolltui_load_layout_text(json.data(), json.size(), defaults, n, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  return Screen(l);
}

// One composed frame, read back as text and kept for style checks.
struct Painted {
  RolltuiSwap* swap = nullptr;
  RolltuiFrame* f = nullptr;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = nullptr;
  std::vector<std::string> rows;
  int toast_wait = -1;
  Painted() = default;
  Painted(const Painted&) = delete;
  ~Painted() {
    rolltui_swap_free(swap);
    rolltui_effect_map_free(fx);
  }
  std::string row(int y) const { return y >= 0 && y < static_cast<int>(rows.size()) ? rows[static_cast<std::size_t>(y)] : ""; }
  bool has(std::string_view s) const {
    for (const std::string& r : rows)
      if (r.find(s) != std::string::npos) return true;
    return false;
  }
  RolltuiCell cell(int x, int y) const {
    RolltuiCell c{};
    rolltui_frame_cell(f, x, y, &c);
    return c;
  }
};

void paint(Painted& p, RolltuiWindows* w, const RolltuiLayout* l, int width, int height, unsigned long long now, bool toasts,
           bool still = false) {
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
  p.fx = rolltui_theme_builtin_fill("default-dark", 12, p.styles, ROLLTUI_ROLE_COUNT);
  const RolltuiRect area{0, 0, width, height};
  RolltuiWidgetEnv env{};
  env.now_ms = now;
  env.still = still ? 1 : 0;
  rolltui_context_set_env(ctx(), &env);
  rolltui_windows_sync(w, stack);
  rolltui_windows_autosize(w, stack, area);
  rolltui_windows_layout(w, stack, area);
  p.swap = rolltui_swap_new(width, height, p.styles[ROLLTUI_ROLE_BACKGROUND]);
  p.f = rolltui_swap_begin(p.swap, width, height, p.styles[ROLLTUI_ROLE_BACKGROUND]);
  RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
  struct Slot {
    RolltuiWindows* w;
    RolltuiStyle* styles;
  } slot{w, p.styles};
  rolltui_window_stack_compose(
      stack, p.f, area, p.styles, rolltui_layout_default_roles(),
      [](void* c, const RolltuiResolvedNode* rn, RolltuiFrame* fr) {
        Slot* s = static_cast<Slot*>(c);
        rolltui_windows_draw(s->w, rn, fr, s->styles, rolltui_windows_default_roles());
      },
      &slot, 0, cs);
  if (toasts) p.toast_wait = rolltui_windows_draw_toasts(w, p.f, area, p.styles, now);
  RolltuiStr text{};
  rolltui_frame_to_text(p.f, &text);
  std::string all = str_of(text);
  rolltui_str_free(&text);
  std::size_t at = 0;
  while (at <= all.size()) {
    const std::size_t nl = all.find('\n', at);
    p.rows.push_back(all.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  rolltui_compose_scratch_free(cs);
  rolltui_window_stack_free(stack);
}

struct GaugeState {
  double fraction = 0;
  const char* label = "";
  bool failed = false;
};

void gauge_fn(void* c, RolltuiGauge* out) {
  const GaugeState* g = static_cast<const GaugeState*>(c);
  out->fraction = g->fraction;
  rolltui_str_set(&out->label, g->label, std::strlen(g->label));
  out->failed = g->failed ? 1 : 0;
  out->since_ms = 1000;
}

const char* kGaugeScreen = R"({ "resizable": false, "name": "g", "root": { "column": [
  { "id": "bar", "content": "gauge:copy", "size": 1 },
  { "id": "rest", "content": "text:" } ] } })";

void test_gauge() {
  const Screen l = load(kGaugeScreen);
  RolltuiWindows* w = rolltui_windows_new(ctx());
  {
    Painted p;
    paint(p, w, l.l, 40, 4, 0, false);
    check(p.has("nothing is bound to 'copy'"), "an unbound gauge says so, as every unbound source does [" + p.row(0) + "]");
  }
  GaugeState g{0.5, "copying"};
  rolltui_windows_bind_gauge(w, "copy", 4, gauge_fn, &g, nullptr);
  {
    Painted p;
    paint(p, w, l.l, 40, 4, 0, false);
    const std::string r = p.row(0);
    check(r.rfind("copying ", 0) == 0 && r.size() >= 3 && r.substr(r.size() - 3) == "50%",
          "half done: the label, the bar, the percent at the row's end [" + r + "]");
    // The bar is 40 - "50%" - 1 - "copying" - 1 = 28 cells: 14 full blocks, then the track.
    check(r.find("\xE2\x96\x88\xE2\x96\x88") != std::string::npos && r.find("\xE2\x94\x80") != std::string::npos,
          "…filled in full blocks, the rest a thin track");
    check(p.cell(8, 0).style == p.styles[ROLLTUI_ROLE_ACCENT_2], "…the fill in the gauge role");
  }
  g.fraction = 0.997;
  {
    Painted p;
    paint(p, w, l.l, 40, 4, 0, false);
    check(p.row(0).find("99%") != std::string::npos, "99.7% says 99%: 100% is said only when it is done [" + p.row(0) + "]");
  }
  g.fraction = 0.3;
  g.failed = true;
  {
    Painted p;
    paint(p, w, l.l, 40, 4, 0, false);
    check(p.cell(8, 0).style == p.styles[ROLLTUI_ROLE_ERROR] && p.cell(37, 0).style == p.styles[ROLLTUI_ROLE_ERROR],
          "failed: the bar and the percent in the error role");
  }
  g.failed = false;
  g.label = "a label far too long to leave the bar room";
  {
    Painted p;
    paint(p, w, l.l, 20, 4, 0, false);
    check(p.row(0).find("a label") == std::string::npos && p.row(0).find("30%") != std::string::npos,
          "a narrow row drops the label before the bar [" + p.row(0) + "]");
  }
  g.fraction = -1;
  g.label = "reading";
  {
    Painted p;
    paint(p, w, l.l, 40, 4, 0, false);
    check(p.row(0).rfind("\xE2\xA0\x8B reading", 0) == 0, "not known: a spinner and the label, no bar [" + p.row(0) + "]");
    check(rolltui_frame_mark_count(p.f) >= 1, "…the spinner is marked `waiting`, for the theme to turn");
    RolltuiEffectScratch* es = rolltui_effect_scratch_new();
    RolltuiEffectReport rep{};
    rolltui_effects_apply(ctx(), p.f, es, p.styles, nullptr, p.fx, 1000 + 80 * 3, 0, &rep, nullptr, nullptr);
    std::size_t gl = 0;
    const char* glyph = rolltui_frame_glyph(p.f, 0, 0, &gl);
    check(std::string(glyph, gl) != "\xE2\xA0\x8B", "…and the shipped theme's spinner turns it [" + std::string(glyph, gl) + "]");
    rolltui_effect_scratch_free(es);
  }
  rolltui_windows_free(w);
}

const char* kMessagesScreen = R"({ "resizable": false, "name": "m", "root": { "column": [
  { "id": "log", "content": "messages" },
  { "id": "line", "content": "messages", "size": 1 } ] } })";

void test_messages_and_toasts() {
  const Screen l = load(kMessagesScreen);
  RolltuiWindows* w = rolltui_windows_new(ctx());
  {
    Painted p;
    paint(p, w, l.l, 60, 12, 0, true);
    check(p.has("no messages yet"), "an empty history says so, rather than a blank that reads as broken");
    check(p.toast_wait == -1, "…and no toast is showing: the wait is -1");
  }
  rolltui_windows_post(w, ROLLTUI_MESSAGE_INFO, "indexed 12 files", 16, 1000);
  rolltui_windows_post(w, ROLLTUI_MESSAGE_ERROR, "could not open notes.txt", 24, 2000);
  check(rolltui_windows_message_count(w) == 2, "the history holds both, the oldest first");
  {
    int level = -1;
    unsigned long long at = 0;
    std::size_t n = 0;
    const char* t = rolltui_windows_message_at(w, 1, &n, &level, &at);
    check(t && std::string(t, n) == "could not open notes.txt" && level == ROLLTUI_MESSAGE_ERROR && at == 2000,
          "message_at gives the text, the level and when it was posted");
    check(rolltui_windows_message_at(w, 2, &n, nullptr, nullptr) == nullptr, "…and NULL past the end");
  }
  {
    Painted p;
    paint(p, w, l.l, 60, 12, 2050, true);
    check(p.has("i indexed 12 files") && p.has("\xE2\x9C\x97 could not open notes.txt"),
          "the messages window lists each with its level's glyph");
    check(p.row(11).find("could not open notes.txt") != std::string::npos,
          "a one-row messages window shows the newest [" + p.row(11) + "]");
  }
  {
    Painted p;
    paint(p, w, l.l, 60, 12, 2500, true, /*still=*/true);
    int found_y = -1;
    for (int y = 0; y < 12; ++y)
      if (p.row(y).find("\xE2\x9C\x97 could not open notes.txt") != std::string::npos && p.row(y).find("\xE2\x94\x82") != std::string::npos)
        found_y = y;
    check(found_y >= 0 && found_y < 11, "a toast boxes the newest message in the bottom-right, above the last row");
    check(p.toast_wait > 0 && p.toast_wait <= 6000 - 1500, "the wait is until the next toast ends [" + std::to_string(p.toast_wait) + "]");
    // The press dismisses: find a cell inside the error toast's box and press it.
    const std::string& r = p.rows[static_cast<std::size_t>(found_y)];
    const std::size_t byte = r.find("could not");
    int col = 0;
    for (std::size_t i = 0; i < byte; ++i) col += (static_cast<unsigned char>(r[i]) & 0xC0) != 0x80;
    RolltuiEvent press{};
    press.kind = ROLLTUI_EVENT_MOUSE;
    press.mouse.kind = RolltuiMouseEvent::Kind::Press;
    press.mouse.button = 1;
    press.mouse.x = col;
    press.mouse.y = found_y;
    check(rolltui_windows_toast_event(w, &press) == 1, "a press on a toast is the toast's");
    RolltuiEvent elsewhere = press;
    elsewhere.mouse.x = 0;
    elsewhere.mouse.y = 0;
    check(rolltui_windows_toast_event(w, &elsewhere) == 0, "…and a press elsewhere is not");
  }
  {
    Painted p;
    paint(p, w, l.l, 60, 12, 2600, true, true);
    bool error_toast = false;
    for (const std::string& r : p.rows)
      if (r.find("\xE2\x9C\x97 could not") != std::string::npos && r.find("\xE2\x94\x82") != std::string::npos) error_toast = true;
    check(!error_toast && rolltui_windows_message_count(w) == 2, "a dismissed toast is gone; its message stays in the history");
  }
  {
    Painted p;
    paint(p, w, l.l, 60, 12, 1000 + 6000, true, true);
    check(p.toast_wait == -1, "the info toast ends after ROLLTUI_TOAST_MS: nothing is showing [" + std::to_string(p.toast_wait) + "]");
    check(p.row(11).find("could not") != std::string::npos, "an error's line message lasts twice as long as an info's");
  }
  rolltui_windows_post(w, ROLLTUI_MESSAGE_OK, "saved", 5, 20000);
  {
    Painted moving;
    paint(moving, w, l.l, 60, 12, 20040, true);
    check(moving.toast_wait == 16, "while it slides in, the toast asks for the next frame in 16 ms [" + std::to_string(moving.toast_wait) + "]");
    Painted settled;
    paint(settled, w, l.l, 60, 12, 20040, true, /*still=*/true);
    check(settled.toast_wait > 16, "a still env places it at once, with no frame for the slide");
  }
  for (int i = 0; i < ROLLTUI_MESSAGE_HISTORY + 5; ++i) rolltui_windows_post(w, ROLLTUI_MESSAGE_INFO, "x", 1, 30000);
  check(rolltui_windows_message_count(w) == ROLLTUI_MESSAGE_HISTORY, "the history keeps the newest ROLLTUI_MESSAGE_HISTORY");
  rolltui_windows_dismiss_toasts(w);
  {
    Painted p;
    paint(p, w, l.l, 60, 12, 30001, true);
    check(p.toast_wait == -1, "dismiss_toasts clears every toast");
  }
  rolltui_windows_free(w);
}

void test_menu_busy_spinner() {
  RolltuiWindows* w = rolltui_windows_new(ctx());
  const Screen l = load(R"({ "resizable": false, "name": "b", "root": { "column": [ { "id": "m", "content": "menu:jobs" } ] } })");
  static const char kMenu[] = R"({ "items": [ { "id": "sync", "label": "Sync now" }, { "id": "other", "label": "Other" } ] })";
  rolltui_context_add_menu(ctx(), "jobs", 4, kMenu, sizeof kMenu - 1);
  {
    Painted p;
    paint(p, w, l.l, 40, 6, 0, false);
    check(p.has("Sync now") && !p.has("Sync now \xE2\xA0\x8B"), "a row whose action is not running has no spinner");
  }
  RolltuiMenu* m = rolltui_windows_menu(w, "jobs", 4);
  RolltuiMenuItem* it = m ? rolltui_menu_find(m, "sync", 4) : nullptr;
  check(it != nullptr, "the host finds the row its action came from");
  if (it) {
    it->busy = 1;
    it->busy_since_ms = 500;
    Painted p;
    paint(p, w, l.l, 40, 6, 600, false);
    check(p.has("Sync now \xE2\xA0\x8B"), "busy: a spinner after the label");
  }
  rolltui_windows_free(w);
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());
  test_gauge();
  test_messages_and_toasts();
  test_menu_busy_spinner();
  return testkit::report("feedback_test");
}
