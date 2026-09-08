//
// gaps_test.cpp — THE GAP REPORT: what a screen NAMES that an app does not
// PROVIDE, reported to the app's own DEVELOPER and never fatal.
//
// The framing this suite exists to hold: a gap is feedback in the right place — the app was
// designed this way and the code does not support it yet. So every assertion below is one of
// two shapes:
//
//   1. **The report SAYS the right thing** — by name, in the screen's own vocabulary, so a
//      developer can find the window in a file and the missing thing in their own code.
//   2. **THE APP STILL RUNS.** Every gap case composes a real frame afterwards. A gap report
//      that stopped an app would be validation wearing a new name.
//
// AND THE ONE ASSERTION THAT IS THE WHOLE REASON THIS IS NOT `rolltui_windows_sync`: a gap
// inside a popup the layout DECLARES and nobody has PUSHED. `sync` walks the stack and cannot
// see it; a developer wants to know before a user opens it.
//
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using testkit::check;

namespace {

RolltuiContext* ctx() { return rolltui_test::test_context(); }

// A layout is an OPAQUE handle now: the loader hands one back OWNED and this frees
// it. `Screen` is the smallest thing that can hold one and not leak when an assertion returns
// early — a test that leaks its fixture would make `lifetime_test`'s zero a lie about this suite.
struct Screen {
  RolltuiLayout* l = nullptr;
  Screen() = default;
  explicit Screen(RolltuiLayout* p) : l(p) {}
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;
  ~Screen() { rolltui_layout_free(l); }
  explicit operator bool() const { return l != nullptr; }
};

Screen load(std::string_view json) {
  RolltuiLayoutReport rep{};
  std::size_t n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx(), &n);
  RolltuiLayout* l =
      rolltui_load_layout_text(json.data(), json.size(), defaults, n, rolltui_layout_default_hooks(), &rep);
  rolltui_layout_report_release(&rep);
  return Screen(l);
}

std::vector<std::string> lines_of(const RolltuiGapReport& r) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < r.gaps_n; ++i) out.push_back(str_of(r.gaps[i]));
  return out;
}

bool any_contains(const std::vector<std::string>& v, std::string_view needle) {
  for (const std::string& s : v)
    if (s.find(needle) != std::string::npos) return true;
  return false;
}

// Composes the screen to text. Its VALUE is that it runs at all: every gap case calls it, so a
// gap report that ever became fatal fails here rather than in review.
std::string paint(RolltuiWindows* w, const RolltuiLayout* l, int width, int height) {
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, rolltui_layout_base(l));
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  const RolltuiRect area{0, 0, width, height};
  RolltuiWidgetEnv env{};
  rolltui_context_set_env(ctx(), &env);
  rolltui_windows_sync(w, stack);
  rolltui_windows_autosize(w, stack, area);
  rolltui_windows_layout(w, stack, area);
  RolltuiSwap* swap = rolltui_swap_new(width, height, styles[ROLLTUI_ROLE_BACKGROUND]);
  RolltuiFrame* f = rolltui_swap_begin(swap, width, height, styles[ROLLTUI_ROLE_BACKGROUND]);
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
  std::string out = str_of(text);
  rolltui_str_free(&text);
  rolltui_compose_scratch_free(cs);
  rolltui_swap_free(swap);
  rolltui_effect_map_free(fx);
  rolltui_window_stack_free(stack);
  return out;
}

}  // namespace

int main() {
  rolltui_context_set_library_defaults(ctx());

  // ---- 1. THE DONE-WHEN: three things this app does not provide, three lines, and it runs ----
  // The screen wants a `browser` kind nobody registered, a `rows:telemetry` source nothing is
  // bound to, and declares `app.zoom` that no chord reaches. None of those is an error.
  {
    const Screen l = load(R"({
      "name": "wanted", "actions": { "app.zoom": "zoom a pane" },
      "root": { "column": [
        { "id": "sheet", "content": "browser" },
        { "id": "side", "content": "rows:telemetry" } ] } })");
    check(static_cast<bool>(l), "the fixture screen loads — naming what an app lacks is not a load failure");
    if (!l) return testkit::report("rolltui_gaps_test");

    RolltuiWindows* w = rolltui_windows_new(ctx());
    RolltuiBindings* b = rolltui_bindings_clone(rolltui_bindings_default(ctx()));
    std::size_t an = 0;
    const RolltuiLayoutAction* acts = rolltui_layout_actions(l.l, &an);
    rolltui_bindings_declare(b, acts, an, nullptr, 0);

    RolltuiGapReport g{};
    rolltui_gaps_collect(w, l.l, b, &g);
    const std::vector<std::string> lines = lines_of(g);

    check(g.gaps_n == 3, "three things named, three gaps reported (" + std::to_string(g.gaps_n) + ")");
    check(any_contains(lines, "window 'sheet'") && any_contains(lines, "'browser'"),
          "the unregistered KIND is named, with the window a developer must open to find it");
    check(any_contains(lines, "window 'side'") && any_contains(lines, "telemetry"),
          "the unbound SOURCE is named, in the widget's own words");
    check(any_contains(lines, "app.zoom") && any_contains(lines, "no chord reaches it"),
          "the unreachable ACTION is named, and says WHY rather than just listing it");
    check(!rolltui_gap_report_clean(&g), "a report with gaps is not clean");

    // THE SUMMARY IS A SENTENCE A DEVELOPER CAN ACT ON, not a count.
    RolltuiStr sum{};
    rolltui_gap_report_summary(&g, &sum);
    const std::string s = str_of(sum);
    rolltui_str_free(&sum);
    check(s.find("this screen names 3 things this app must provide and 3 are missing") == 0,
          "the summary counts what was NAMED against what is MISSING [" + s + "]");

    // AND THE HALF THAT MATTERS MOST: it still runs.
    const std::string frame = paint(w, l.l, 40, 8);
    check(!frame.empty() && frame.find('\n') != std::string::npos,
          "THE APP STILL RUNS: a screen with three gaps composes a frame");
    check(frame.find("browser") != std::string::npos,
          "…and the unknown kind draws its reason in the window rather than nothing");

    rolltui_gap_report_release(&g);
    rolltui_bindings_free(b);
    rolltui_windows_free(w);
  }

  // ---- 2. A SCREEN WITH NOTHING MISSING REPORTS NOTHING ----------------------------------
  // The empty case earns its keep: without it "reports three gaps" is satisfied by a function
  // that reports three gaps at all times.
  {
    const Screen l = load(R"({
      "name": "whole", "actions": { "app.help": "open help" },
      "root": { "column": [ { "id": "log", "content": "transcript:session" } ] } })");
    check(static_cast<bool>(l), "the complete screen loads");
    if (l) {
      RolltuiWindows* w = rolltui_windows_new(ctx());
      rolltui_windows_bind_sample_document(w, "session", 7, "# hi\n", 5);
      RolltuiBindings* b = rolltui_bindings_clone(rolltui_bindings_default(ctx()));
      std::size_t an = 0;
      const RolltuiLayoutAction* acts = rolltui_layout_actions(l.l, &an);
      rolltui_bindings_declare(b, acts, an, nullptr, 0);

      RolltuiGapReport g{};
      rolltui_gaps_collect(w, l.l, b, &g);
      check(rolltui_gap_report_clean(&g) && g.gaps_n == 0,
            "an app that provides everything the screen names has NO gaps (" + std::to_string(g.gaps_n) + ")");
      check(g.named == 2, "…and it still counted what it checked: one window, one action (" +
                              std::to_string(g.named) + ")");
      RolltuiStr sum{};
      rolltui_gap_report_summary(&g, &sum);
      check(sum.n == 0, "a clean report summarises as \"\", the rule every report here has");
      rolltui_str_free(&sum);
      rolltui_gap_report_release(&g);
      rolltui_bindings_free(b);
      rolltui_windows_free(w);
    }
  }

  // ---- 3. THE REASON THIS IS NOT `sync`: A GAP INSIDE AN UNPUSHED POPUP -------------------
  // `rolltui_windows_sync` walks the layers currently on the stack. A popup the layout
  // DECLARES and nobody has opened is invisible to it — so its gap surfaces the first time a
  // user opens that popup, which is the wrong moment and the wrong person. This is the
  // assertion that says the two are different jobs rather than two spellings of one.
  {
    const Screen l = load(R"({
      "name": "deferred",
      "root": { "column": [ { "id": "log", "content": "transcript:session" } ] },
      "popups": [
        { "id": "details", "x": "50%", "y": "50%", "w": 20, "h": 6, "anchor": "center",
          "root": { "id": "details", "content": "inspector" } } ] })");
    check(static_cast<bool>(l), "a screen whose gap is inside a declared popup loads");
    if (l) {
      RolltuiWindows* w = rolltui_windows_new(ctx());
      rolltui_windows_bind_sample_document(w, "session", 7, "# hi\n", 5);

      // What `sync` sees: the base only. The popup is declared, not pushed.
      RolltuiWindowStack* stack = rolltui_window_stack_new();
      rolltui_window_stack_set_base(stack, rolltui_layout_base(l.l));
      rolltui_windows_sync(w, stack);
      check(rolltui_windows_report_count(w) == 0,
            "`sync` reports NOTHING — the popup is declared and not pushed, so it is not on the stack");
      rolltui_window_stack_free(stack);

      RolltuiGapReport g{};
      rolltui_gaps_collect(w, l.l, nullptr, &g);
      check(g.gaps_n == 1 && any_contains(lines_of(g), "inspector"),
            "…and the gap report finds it anyway, because it walks the SCREEN rather than the stack");
      check(any_contains(lines_of(g), "window 'details'"),
            "…naming the popup's own window, which is where a developer has to look");
      rolltui_gap_report_release(&g);
      rolltui_windows_free(w);
    }
  }

  // ---- 4. NULL BINDINGS SKIPS THE ACTION HALF, AND SAYS SO BY COUNTING -------------------
  // The action check is the weaker half by design (a menu item may still reach an action, and
  // whether a host HANDLES one is not library-visible). A host with no table gets the exact
  // half that is knowable rather than a wrong answer.
  {
    const Screen l = load(R"({
      "name": "noverbs", "actions": { "app.zoom": "zoom" },
      "root": { "column": [ { "id": "log", "content": "transcript:session" } ] } })");
    if (l) {
      RolltuiWindows* w = rolltui_windows_new(ctx());
      rolltui_windows_bind_sample_document(w, "session", 7, "# hi\n", 5);
      RolltuiGapReport g{};
      rolltui_gaps_collect(w, l.l, nullptr, &g);
      check(g.gaps_n == 0 && g.named == 1,
            "with no bindings table the action half is skipped, and `named` counts only what was checked (" +
                std::to_string(g.named) + ")");
      rolltui_gap_report_release(&g);
      rolltui_windows_free(w);
    }
  }

  // ---- 5. THE REPORT IS REUSABLE, which is the rule every report on this boundary has ----
  {
    const Screen bad = load(R"({ "name": "b",
      "root": { "column": [ { "id": "x", "content": "nosuchkind" } ] } })");
    const Screen good = load(R"({ "name": "g",
      "root": { "column": [ { "id": "x", "content": "text:hello" } ] } })");
    if (bad && good) {
      RolltuiWindows* w = rolltui_windows_new(ctx());
      RolltuiGapReport g{};
      rolltui_gaps_collect(w, bad.l, nullptr, &g);
      check(g.gaps_n == 1, "one gap from the first screen");
      rolltui_gaps_collect(w, good.l, nullptr, &g);
      check(g.gaps_n == 0 && g.named == 1,
            "…and collecting again REPLACES rather than appends (" + std::to_string(g.gaps_n) + ")");
      rolltui_gap_report_release(&g);
      rolltui_windows_free(w);
    }
  }

  return testkit::report("rolltui_gaps_test");
}
