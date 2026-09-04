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

#include "rolltui/Diff.hpp"
#include "rolltui/Document.hpp"
#include "rolltui/Effects.hpp"
#include "rolltui/Bindings.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Lifetime.hpp"
#include "rolltui/Presets.hpp"
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

// PHASE 15 m2: THE EFFECT-KIND REGISTRY, POPULATED — and this function is the reason the
// zero below means anything. The registry is the first PROCESS-WIDE RETAINER in the ported
// slice, and it holds three things per host kind: a table slot, a COPY of the name, and the
// host's own callable, which a `void*` cannot destroy without being told how. A test that
// asserted `live_bytes == 0` over a registry NOBODY EVER FILLED would be this repo's oldest
// failure — an instrument reporting zero because it was pointed at nothing.
//
// It also exercises the two per-thread handles the same milestone added (the effects
// scratch and the diff scratch), which `release_thread()` has to hand back for the same
// number to come out.
void use_the_ported_modules(const char* when) {
  std::string why;
  check(register_effect_kind("lifetime-probe",
                             [](const EffectSpec&, const Theme&, const EffectCell&, EffectOut& out) {
                               out.set_glyph("*");
                             },
                             &why),
        std::string("a host kind registers, so the registry HOLDS something — ") + when + " [" + why + "]");
  const Theme theme = *builtin_theme("default-dark");
  Frame f(40, 4, theme.style(Role::text));
  f.put_text(0, 1, "waiting for the model", theme.style(Role::text), 40);
  f.mark(0, 1, 8, EffectState::Waiting);
  const EffectReport rep = apply_effects(f, theme, 137);
  check(rep.marks_drawn == 1 && rep.clean(),
        std::string("…and an effect is APPLIED, so its scratch is populated too — ") + when);
  check(effect_tick_ms(f, theme).has_value(), std::string("…and the frame asks for a wakeup — ") + when);
  const std::vector<std::string_view> block = {"-one two three", "+one TWO three"};
  check(diff_spans("diff", block, 1).size() == 3,
        std::string("…and a diff line is coloured, which is the other new handle — ") + when);

  // PHASE 15 m3: THE THREE RETAINERS THE PORT ADDED, all touched here for the reason the
  // registry above is — a zero over something nobody ever filled is this repo's oldest
  // failure, and each of these holds C memory now where it used to hold `std::vector`s the
  // gauge could not see.
  //   - the BUILT-IN THEMES: three `Theme`s, each owning a C effect map;
  //   - the SHIPPED PRESETS of all three domains, parsed once per domain into a cache the
  //     descriptor owns (the Bindings one owns a C table per preset);
  //   - the DEFAULT BINDINGS, which is one more of those tables plus the shipped layout's
  //     declarations.
  check(builtin_theme("mono") != nullptr, std::string("…and the built-in themes are built — ") + when);
  check(!default_bindings().actions().empty(), std::string("…and the shipped default bindings parsed — ") + when);
  check(ThemePresets::shipped("default") != nullptr && LayoutPresets::shipped("default") != nullptr &&
            BindingsPresets::shipped("default") != nullptr,
        std::string("…and every domain's shipped presets are parsed and cached — ") + when);

  // PHASE 15 m5: THE HOST WIDGET-KIND REGISTRY, which is the layout port's process-wide
  // retainer and the same shape m2's effect registry is — a table slot plus a COPY of the
  // name and of what its source is called. Registered here for the reason every line above
  // it is: a zero over a registry nobody ever filled is the failure this whole file exists
  // to make impossible.
  check(register_widget_kind("lifetime-probe-kind", SourceRule::Optional, "a probe", &why),
        std::string("…and a host WIDGET kind registers, so the layout registry holds something — ") + when +
            " [" + why + "]");
  check(parse_content("lifetime-probe-kind:x").has_value(),
        std::string("…and a content resolves through it, so rung 2 is really reached — ") + when);
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
  const std::size_t before_registry = mem::stats().live_bytes;
  use_the_ported_modules("first time");
#ifdef ROLLTUI_C_BUILD
  // THE ARMING CHECK for the retainer this milestone added: with the registry in C every
  // byte of it is an explicit allocation, so the gauge must SEE the retention appear before
  // it is trusted to report it gone.
  check(mem::stats().live_bytes > before_registry,
        "ROLLTUI_C=ON: the gauge SEES the effect registry's retention [" + std::to_string(before_registry) + " → " +
            std::to_string(mem::stats().live_bytes) + " B]");
#else
  // ROLLTUI_C=OFF the registry is a std::map, so the gauge cannot see it at all — the same
  // honest limit budget_test asserts. The zero below is correspondingly weaker here, which
  // is exactly what Lifetime.hpp says and what the port is steadily fixing.
  (void)before_registry;
#endif

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
  // …and THE SECOND REGISTRATION IS THE PROOF THE FIRST WAS RELEASED, not merely
  // unaccounted (m2's shape): a name still live in the registry with a different source rule
  // is refused, so this succeeding means the table really was handed back.
  {
    std::string why;
    check(register_widget_kind("lifetime-probe-kind", SourceRule::Required, "a probe", &why),
          "…and the widget-kind registry took the same name with a DIFFERENT rule, which is only "
          "possible because shutdown() really released it [" + why + "]");
    // …and put it back the way it was found, so the pass below registers into an empty
    // registry rather than into this proof's leftovers.
    clear_registered_widget_kinds();
  }
  paint_something();
  check(true, "…including painting a whole frame again");
  // REGISTERING THE SAME NAME AGAIN IS THE PROOF THE REGISTRY WAS REALLY EMPTIED: a second
  // registration of a live name is refused by design, so this can only pass if `shutdown()`
  // released the entry rather than merely leaving the bytes unaccounted.
  use_the_ported_modules("after a shutdown");

  shutdown();
  check(mem::stats().live_bytes == 0, "a second shutdown() is safe and still lands on zero");

  return report("rolltui lifetime_test");
}
