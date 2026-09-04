//
// lifetime_test.cpp — Phase 14 m6a: THE RELEASE POINT, AS AN ASSERTION.
//
// The claim `rolltui::shutdown()` makes is not "we tidy up" — it is a NUMBER:
//
//     after shutdown(), rolltui::mem::stats().live_bytes == 0 and live_blocks == 0
//
// **WHY THIS IS WORTH A TEST BINARY OF ITS OWN.** Until m6a, "did the library leak?" had no
// answer, because by-design retention and a real leak look identical to any checker: the
// effect-kind registry, the host-kind registry and the parsed built-in layout cache are all
// allocated on first use and kept forever, and a leak checker cannot tell those from a bug.
// A release point does not make the library tidier — it makes the question ANSWERABLE, which
// is what m6b's sanitizer run needs in order to mean anything.
//
// **WHAT THIS ASSERTS IS WEAKER UNDER `ROLLTUI_C=OFF`, and that is stated rather than
// glossed.** `std::` containers do not route through `rolltui::mem`, so with the flag off the
// gauge sees only the library's own explicit allocations — very few. With it on, the gauge
// sees the entire ported slice. **The assertion therefore gets STRONGER with every module
// that ports, and this file does not change**; that property is the reason it is written
// against `mem::stats()` rather than against a count taken here.
//
// THE CONTROL is the same shape the budget uses: a deliberate retention, made and then
// released, so the run proves the gauge can SEE a non-zero before it trusts a zero. A test
// that reports "nothing live" with an instrument that cannot see is this project's oldest
// failure, aimed here at its newest instrument.
//
#include <string>
#include <vector>

#include "rolltui/Document.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Lifetime.hpp"
#include "rolltui/Memory.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Widgets.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

// A real scene, painted — so the caches, registries and per-thread scratch this test is about
// are all actually populated rather than assumed to be.
void paint_something() {
  Document doc;
  for (int i = 0; i < 8; ++i) {
    DocEntry e;
    e.id = "e" + std::to_string(i);
    e.markdown = true;
    e.text = "## Entry " + std::to_string(i) +
             "\n\nSome prose that is long enough to wrap, with `code` and a "
             "[link](https://example.invalid/p).\n\n- one\n- two\n";
    doc.entries.push_back(std::move(e));
  }
  Windows windows;
  WindowStack stack(*builtin_layout("default"));
  const Theme theme = *builtin_theme("default-dark");
  windows.bind_document("session", &doc);
  windows.bind_rows("status", [](Rows& out) { out.add("theme", "default-dark"); });
  windows.bind_submit("prompt", [](const std::string&) {});
  WidgetEnv env;
  env.now_ms = 1;
  windows.set_env(env);
  Frame f;
  const Rect box{0, 0, 100, 30};
  windows.prepare(stack, box);
  f.reset(box.w, box.h, theme.style(Role::text));
  stack.compose(f, box, theme, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, theme); });
  (void)render_full(f, ColorDepth::TrueColor);
}

}  // namespace

int main() {
  // ---- THE CONTROL FIRST: prove the gauge can see a retention ------------------------
  // Before trusting a zero, make a non-zero and watch it appear and go.
  {
    const std::size_t base = mem::stats().live_bytes;
    void* held = mem::alloc(64 * 1024);
    check(mem::stats().live_bytes >= base + 64 * 1024,
          "the gauge SEES a deliberate retention [" + std::to_string(base) + " → " +
              std::to_string(mem::stats().live_bytes) + " B]");
    mem::free(held);
    check(mem::stats().live_bytes == base, "…and sees it released again, so a zero below means something");
  }

  // ---- shutdown() with nothing to do, before anything has run ------------------------
  // It has no init, so it must be safe with no history at all.
  shutdown();
  check(true, "shutdown() on a library that has done nothing does not crash");

  // ---- the real thing ----------------------------------------------------------------
  paint_something();
  check(builtin_layout("default") != nullptr, "a scene painted, so the caches and scratch are populated");

  shutdown();
  const mem::Stats after = mem::stats();
  check(after.live_bytes == 0, "AFTER shutdown() THE LIBRARY HOLDS NOTHING: live_bytes == 0 [" +
                                   std::to_string(after.live_bytes) + " B]");
  check(after.live_blocks == 0,
        "…and no blocks either [" + std::to_string(after.live_blocks) + "]");

  // ---- and it is safe to carry on afterwards ------------------------------------------
  // The caches rebuild. This is what makes shutdown() callable at any moment rather than only
  // at the very end — and it is a live defect if a releaser ever clears something whose
  // accessor was a one-shot static initializer, which is exactly what `builtin_layout` was
  // for about ten minutes.
  check(builtin_layout("default") != nullptr, "…and the caches REBUILD, so the library still works after it");
  paint_something();
  check(true, "…including painting a whole frame again");

  shutdown();
  check(mem::stats().live_bytes == 0, "a second shutdown() is safe and still lands on zero");

  return report("rolltui lifetime_test");
}
