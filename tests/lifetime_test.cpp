//
// lifetime_test.cpp — Phase 14 m6a: THE RELEASE POINT, AS AN ASSERTION.
//
// The claim `rolltui_shutdown()` makes is not "we tidy up" — it is a NUMBER:
//
//     after shutdown(), rolltui_mem_stats()'s live_bytes == 0 and live_blocks == 0
//
// **WHY THIS IS WORTH A TEST BINARY OF ITS OWN.** Until m6a, "did the library leak?" had no
// answer, because by-design retention and a real leak look identical to any checker: the
// effect-kind registry, the host-kind registry and the parsed built-in layout cache are all
// allocated on first use and kept forever, and a leak checker cannot tell those from a bug.
// A release point does not make the library tidier — it makes the question ANSWERABLE, which
// is what m6b's sanitizer run needs in order to mean anything.
//
// **WHAT THIS ASSERTS IS NOW TOTAL, and it was not always.** While a C++ implementation of
// the library existed, `live_bytes == 0` was weaker than it looked in that build: `std::string`
// and `std::vector` reach the global `operator new`, never `rolltui::mem`, so a zero here could
// coexist with memory the gauge simply could not see. The C++ implementations were deleted on
// 2026-09-04; every allocation the library makes is now an explicit call through one entry
// point, so the zero below means the library holds nothing.
//
// ---- PHASE 17 m2c: THIS FILE CALLS THE C DIRECTLY ---------------------------------------
//
// The eleven C++ headers this file used to include (Bindings/Diff/Document/Effects/Layout/
// Lifetime/Memory/Presets/Screen/Theme/Widgets) are thin BINDINGS over `rolltui/c/*.h` and are
// about to be deleted; this file now reaches for the umbrella (`rolltui/rolltui.h`) instead,
// with ONE stated exception below. Three shapes are worth naming up front, because each is a
// SHAPE this file mirrors, never a rule or a word the library owns:
//
//   - `Theme` (styles + an effect map) and the two rebuildable, `rolltui_on_shutdown`-
//     registered caches (`builtin_theme`, `builtin_layout`) mirror `Theme.cpp`'s and
//     `Layout.cpp`'s own STORAGE-vs-ACCESSOR pattern in miniature, for just the names this
//     file needs. There is no C-side cache of "a parsed built-in Theme/Layout by name" — only
//     `rolltui_theme_builtin_fill` (fills a caller's table, no cache) and
//     `rolltui_layout_builtin_json` (hands back unparsed embedded TEXT) — so a caller that
//     wants the ORIGINAL property under test ("this cache empties at shutdown() and rebuilds
//     on next use") has to hold it, the same way any future C host would.
//   - the three preset domains are the library's own (`rolltui_preset_domain`, Phase 18 m3),
//     which registers their releaser at cache-build time for the same reason the two caches
//     above do. Until then this file built its own three, with a STUB `reason` callback; the
//     library's carries the real one, and the shipped "default" bindings preset has no
//     undeliverable chord (the library aborts its own build if it ever did), so the two are
//     observationally identical for this one lookup.
//   - `diff_spans`' `RolltuiDiffRoles` is filled with ONE placeholder role for all seven
//     slots, not the theme's real added/removed/context/... mapping: the assertion below only
//     counts spans, never inspects which role one carries, and the real mapping
//     (`Diff.cpp`'s `kRoles`) is ALREADY duplicated once, verbatim, in
//     `markdown_test.cpp`'s `kDiffRoles` — a second copy here would be a third. See the report.
//
// **THE ONE THING THAT WAS BLOCKED IS NOW FIXED AT THE LIBRARY, and the block is what found
// it.** `mem::alloc`/`mem::free` — the raw allocate/free pair the first control below needs —
// were declared only in `rolltui/c/rolltui_alloc.h`, which the umbrella excludes as internal,
// while that same header's own text said "`alloc` and `free` stay available everywhere". This
// file could not reach them and said so instead of working around it; they moved to the public
// `rolltui/c/rolltui_mem.h` on 2026-09-05, one day after `rolltui_mem_stats` moved for exactly
// the same reason and was found the same way. `rolltui_mem_realloc` stayed behind on purpose —
// growth is the restricted one, and an internal header makes that structural.
//
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/rolltui.h"

#include "rolltui_test.hpp"

using namespace rolltui_test;

namespace {

// ---- memory: the two reads every check below wants, over rolltui_mem_stats directly -------
std::size_t live_bytes() {
  std::size_t v = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &v, nullptr, nullptr);
  return v;
}
std::size_t live_blocks() {
  std::size_t v = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, nullptr, nullptr, &v);
  return v;
}

// ---- a private Theme fixture: styles + the effect map this file actually reads, and never
// more (Theme::name is kept only because it is the cache's own lookup key) -------------------
struct Theme {
  std::string name;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  const RolltuiStyle& style(unsigned char role) const {
    return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role);
  }
};

// THE STORAGE, touched by the releaser and never by the accessor below — the same split
// `Theme.cpp`'s own `theme_cache_storage()` states the reason for: a releaser written against
// the accessor would find the cache it just emptied and refill it on the spot.
std::vector<Theme>& theme_cache_storage() {
  static std::vector<Theme> cache;
  return cache;
}

// THE ACCESSOR: filled when empty, with the releaser RE-REGISTERED on every rebuild —
// `rolltui_shutdown()` drains its own releaser list as it runs, so a `static bool once` guard
// would release this cache the first time and never again.
std::vector<Theme>& builtin_theme_cache() {
  std::vector<Theme>& cache = theme_cache_storage();
  if (cache.empty()) {
    rolltui_on_shutdown([] {
      for (Theme& t : theme_cache_storage()) rolltui_effect_map_free(t.effects);
      theme_cache_storage().clear();
    });
    const std::size_t n = rolltui_theme_builtin_count();
    cache.reserve(n);  // pointer stability: builtin_theme() hands back &t into this vector
    for (std::size_t i = 0; i < n; ++i) {
      const char* name = rolltui_theme_builtin_name(i);
      Theme t;
      t.name = name;
      t.effects = rolltui_theme_builtin_fill(name, std::strlen(name), t.styles, ROLLTUI_ROLE_COUNT);
      cache.push_back(std::move(t));
    }
  }
  return cache;
}

const Theme* builtin_theme(std::string_view name) {
  for (const Theme& t : builtin_theme_cache())
    if (t.name == name) return &t;
  return nullptr;
}

// ---- the one built-in layout this file needs, cached the same shutdown-aware way -----------
// Only "default" — unlike the theme cache above, nothing here ever asks for a second name, so
// there is no reason to mirror Layout.cpp's full by-name table.
std::optional<RolltuiLayout>& layout_cache_storage() {
  static std::optional<RolltuiLayout> cache;
  return cache;
}

const RolltuiLayout* builtin_layout(std::string_view name) {
  if (name != "default") return nullptr;  // "Unknown name -> nullptr", same as rolltui::builtin_layout
  std::optional<RolltuiLayout>& cache = layout_cache_storage();
  if (!cache) {
    rolltui_on_shutdown([] {
      if (layout_cache_storage()) rolltui_layout_release(&*layout_cache_storage());
      layout_cache_storage().reset();
    });
    std::size_t text_len = 0;
    const char* text = rolltui_layout_builtin_json("default", 7, &text_len);
    if (text != nullptr && text_len != 0) {
      RolltuiLoadedLayout loaded{};
      rolltui_loaded_layout_init(&loaded);
      std::size_t defaults_n = 0;
      const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(&defaults_n);
      RolltuiLayoutReport rep{};
      if (rolltui_load_layout_text(text, text_len, &loaded, defaults, defaults_n, rolltui_layout_default_hooks(),
                                    &rep) != 0) {
        RolltuiLayout l{};
        rolltui_layout_init(&l);
        rolltui_loaded_layout_to_layout(&loaded, &l);
        cache = std::move(l);
      }
      rolltui_layout_report_release(&rep);
      rolltui_loaded_layout_release(&loaded);
    }
  }
  return cache ? &*cache : nullptr;
}

// ---- the three preset domains are the LIBRARY's (`rolltui_preset_domain`, Phase 18 m3) --------
// This file had built its own three as function-local statics — with a `reason_noop` stub in
// place of the library's own reason table, and its own `rolltui_on_shutdown` registration — as
// had four other consumers. The release at shutdown is the library's now, which is exactly what
// the zero this test measures after `rolltui_shutdown()` proves: the caches this scene populates
// through `rolltui_preset_shipped` below are let go of by a hook the library registered itself.

// ---- painting a real scene: the caches, registries and per-call scratch this test is about
// are all actually populated rather than assumed to be. -------------------------------------

void bind_status_rows(void*, RolltuiRows* out) {
  constexpr std::string_view kLabel = "theme", kValue = "default-dark";
  rolltui_rows_add(out, kLabel.data(), kLabel.size(), kValue.data(), kValue.size());
}
void bind_prompt_submit(void*, const char*, std::size_t) {}

struct DrawSlotCtx {
  RolltuiWindows* windows;
  const Theme* theme;
};
void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  const DrawSlotCtx* d = static_cast<const DrawSlotCtx*>(ctx);
  rolltui_windows_draw(d->windows, rn, f, d->theme->styles, rolltui_windows_default_roles());
}

void paint_something() {
  RolltuiDocument doc{};
  for (int i = 0; i < 8; ++i) {
    RolltuiDocEntry* e = rolltui_document_add(&doc);
    e->id = "e" + std::to_string(i);
    e->markdown = 1;
    e->text = "## Entry " + std::to_string(i) +
              "\n\nSome prose that is long enough to wrap, with `code` and a "
              "[link](https://example.invalid/p).\n\n- one\n- two\n";
  }
  RolltuiWindows* windows = rolltui_windows_new();
  rolltui_windows_set_library_defaults(windows);
  rolltui_windows_set_bindings(windows, rolltui_bindings_default());
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  rolltui_window_stack_set_base(stack, &builtin_layout("default")->base);
  const Theme* theme = builtin_theme("default-dark");
  rolltui_windows_bind_document(windows, "session", 7, &doc);
  rolltui_windows_bind_rows(windows, "status", 6, bind_status_rows, nullptr, nullptr);
  rolltui_windows_bind_submit(windows, "prompt", 6, bind_prompt_submit, nullptr, nullptr, /*SendAndClear=*/0);
  const RolltuiWidgetEnv env{0, 1};
  rolltui_windows_set_env(windows, &env);
  const RolltuiRect box{0, 0, 100, 30};
  rolltui_windows_sync(windows, stack);
  rolltui_windows_autosize(windows, stack, box);
  rolltui_windows_layout(windows, stack, box);
  RolltuiFrame* f = rolltui_frame_new(0, 0, RolltuiStyle{});
  rolltui_frame_reset(f, box.w, box.h, theme->style(ROLLTUI_ROLE_TEXT));
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  DrawSlotCtx ctx{windows, theme};
  rolltui_window_stack_compose(stack, f, box, theme->styles, rolltui_layout_default_roles(), draw_slot, &ctx,
                               /*ambiguous_wide=*/0, compose_scratch);
  RolltuiStr rendered_text{};
  rolltui_render_full(f, ROLLTUI_DEPTH_TRUECOLOR, &rendered_text);
  rolltui_str_free(&rendered_text);

  rolltui_compose_scratch_free(compose_scratch);
  rolltui_frame_free(f);
  rolltui_window_stack_free(stack);
  rolltui_windows_free(windows);
  rolltui_document_release(&doc);
}

// PHASE 15 m2/m3: THE EFFECT-KIND REGISTRY AND THE THREE OTHER RETAINERS THIS FUNCTION TOUCHES
// (built-in themes, shipped bindings, every domain's shipped presets), plus the widget-kind
// registry — see the original file header (kept above) for why each is exercised rather than
// assumed populated: a zero over a registry nobody ever filled is this repo's oldest failure.

void probe_effect(void*, const RolltuiEffectSpec*, const RolltuiStyle*, const void*, const RolltuiEffectCell*,
                  RolltuiEffectOut* out) {
  out->set_glyph("*");
}
void note_unknown_effect(void* ctx, const char*, std::size_t) { *static_cast<bool*>(ctx) = true; }

const char* diff_line_at(const void* block, std::size_t i, std::size_t* len) {
  const std::string_view s = (*static_cast<const std::vector<std::string_view>*>(block))[i];
  *len = s.size();
  return s.data();
}

void use_the_ported_modules(const char* when) {
  // THE BUILT-IN LAYOUT CACHE, filled HERE and not only after shutdown (Phase 17 m3). It was
  // touched exactly once in this file — at line ~437, to prove the caches REBUILD — which is
  // after the `live_bytes == 0` assertion, so the cache was never live while anything was
  // measuring. Deleting its releaser left this suite 29/29 green: a leak of it would have
  // shipped. Filling it here puts it inside the window the existing assertions already cover,
  // which is why this adds a CALL and not a check.
  check(rolltui_layout_builtin("default", 7) != nullptr,
        std::string("the built-in layout cache fills, so the library HOLDS it — ") + when);

  constexpr std::string_view kProbeName = "lifetime-probe";
  const int effect_code =
      rolltui_effect_register(kProbeName.data(), kProbeName.size(), probe_effect, nullptr, nullptr);
  const std::string effect_why =
      effect_code == ROLLTUI_EFFECT_OK ? std::string() : ("code " + std::to_string(effect_code));
  check(effect_code == ROLLTUI_EFFECT_OK,
        std::string("a host kind registers, so the registry HOLDS something — ") + when + " [" + effect_why + "]");

  const Theme* theme = builtin_theme("default-dark");
  RolltuiFrame* f = rolltui_frame_new(40, 4, theme->style(ROLLTUI_ROLE_TEXT));
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  constexpr std::string_view kWaitingText = "waiting for the model";
  rolltui_frame_put_text(f, draw_scratch, 0, 1, kWaitingText.data(), kWaitingText.size(),
                         theme->style(ROLLTUI_ROLE_TEXT), 40, 0, 0);
  rolltui_draw_scratch_free(draw_scratch);
  rolltui_frame_mark(f, 0, 1, 8, ROLLTUI_EFFECT_STATE_WAITING, 0, 0);

  RolltuiEffectScratch* effect_scratch = rolltui_effect_scratch_new();
  RolltuiEffectReport rep{};
  bool any_unknown = false;
  rolltui_effects_apply(f, effect_scratch, theme->styles, theme, theme->effects, 137, 0, &rep, note_unknown_effect,
                        &any_unknown);
  rolltui_effect_scratch_free(effect_scratch);
  check(rep.marks_drawn == 1 && rep.glyphs_refused == 0 && !any_unknown,
        std::string("…and an effect is APPLIED, so its scratch is populated too — ") + when);

  const int tick_ms = (rolltui_frame_mark_count(f) != 0 && rolltui_effect_map_empty(theme->effects) == 0)
                          ? rolltui_effects_tick_ms(f, theme->effects)
                          : 0;
  check(tick_ms > 0, std::string("…and the frame asks for a wakeup — ") + when);
  rolltui_frame_free(f);

  RolltuiDiffScratch* diff_scratch = rolltui_diff_scratch_new();
  // ONE placeholder role for all seven slots — see the file header: this assertion counts
  // spans and never reads which role one carries, and the real added/removed/context/...
  // mapping is already mirrored once, verbatim, in markdown_test.cpp's kDiffRoles.
  constexpr RolltuiDiffRoles kDiffRoles = {ROLLTUI_ROLE_TEXT, ROLLTUI_ROLE_TEXT, ROLLTUI_ROLE_TEXT, ROLLTUI_ROLE_TEXT,
                                           ROLLTUI_ROLE_TEXT, ROLLTUI_ROLE_TEXT, ROLLTUI_ROLE_TEXT};
  const std::vector<std::string_view> block = {"-one two three", "+one TWO three"};
  RolltuiDiffSpan span_buf[ROLLTUI_DIFF_MAX_SPANS];
  constexpr std::string_view kDiffLang = "diff";
  const std::size_t span_count =
      rolltui_diff_spans(diff_scratch, kDiffLang.data(), kDiffLang.size(), &block, block.size(), diff_line_at, 1,
                         &kDiffRoles, span_buf, ROLLTUI_DIFF_MAX_SPANS);
  rolltui_diff_scratch_free(diff_scratch);
  check(span_count == 3, std::string("…and a diff line is coloured, which is the other new handle — ") + when);

  check(builtin_theme("mono") != nullptr, std::string("…and the built-in themes are built — ") + when);
  check(rolltui_bindings_action_count(rolltui_bindings_default()) != 0,
        std::string("…and the shipped default bindings parsed — ") + when);

  constexpr std::string_view kDefaultPreset = "default";
  const bool presets_ok =
      rolltui_preset_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME), kDefaultPreset.data(),
                             kDefaultPreset.size()) != nullptr &&
      rolltui_preset_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT), kDefaultPreset.data(),
                             kDefaultPreset.size()) != nullptr &&
      rolltui_preset_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS), kDefaultPreset.data(),
                             kDefaultPreset.size()) != nullptr;
  check(presets_ok, std::string("…and every domain's shipped presets are parsed and cached — ") + when);

  constexpr std::string_view kProbeKind = "lifetime-probe-kind", kProbeDescribes = "a probe";
  const int register_ok = rolltui_widget_kind_register(kProbeKind.data(), kProbeKind.size(), ROLLTUI_SOURCE_OPTIONAL,
                                                       kProbeDescribes.data(), kProbeDescribes.size());
  const std::string register_why =
      register_ok == ROLLTUI_REGISTER_OK ? std::string() : ("code " + std::to_string(register_ok));
  check(register_ok == ROLLTUI_REGISTER_OK,
        std::string("…and a host WIDGET kind registers, so the layout registry holds something — ") + when + " [" +
            register_why + "]");

  unsigned char problem = 0;
  std::size_t row = 0;
  int is_host = 0;
  const char *name = nullptr, *source = nullptr;
  std::size_t name_len = 0, source_len = 0;
  RolltuiStr why{};
  constexpr std::string_view kProbeContent = "lifetime-probe-kind:x";
  const int parsed = rolltui_content_parse(kProbeContent.data(), kProbeContent.size(), &row, &is_host, &name,
                                           &name_len, &source, &source_len, &problem, &why);
  rolltui_str_free(&why);
  check(parsed != 0, std::string("…and a content resolves through it, so rung 2 is really reached — ") + when);
}

}  // namespace

int main() {
  // ---- THE CONTROL FIRST: prove the gauge can see a retention ------------------------
  // Before trusting a zero, make a non-zero and watch it appear and go.
  {
    const std::size_t base = live_bytes();
    void* held = rolltui_mem_alloc(64 * 1024);
    check(live_bytes() >= base + 64 * 1024, "the gauge SEES a deliberate retention [" + std::to_string(base) +
                                                " → " + std::to_string(live_bytes()) + " B]");
    rolltui_mem_free(held);
    check(live_bytes() == base, "…and sees it released again, so a zero below means something");
  }

  // ---- shutdown() with nothing to do, before anything has run ------------------------
  // It has no init, so it must be safe with no history at all.
  rolltui_shutdown();
  check(true, "shutdown() on a library that has done nothing does not crash");

  // ---- the real thing ----------------------------------------------------------------
  paint_something();
  check(builtin_layout("default") != nullptr, "a scene painted, so the caches and scratch are populated");
  const std::size_t before_registry = live_bytes();
  use_the_ported_modules("first time");
  // THE ARMING CHECK for the process-wide retainers: every byte of them is an explicit
  // allocation, so the gauge must SEE the retention appear before it is trusted to report it
  // gone.
  check(live_bytes() > before_registry, "the gauge SEES the effect registry's retention [" +
                                            std::to_string(before_registry) + " → " + std::to_string(live_bytes()) +
                                            " B]");

  rolltui_shutdown();
  check(live_bytes() == 0,
        "AFTER shutdown() THE LIBRARY HOLDS NOTHING: live_bytes == 0 [" + std::to_string(live_bytes()) + " B]");
  check(live_blocks() == 0, "…and no blocks either [" + std::to_string(live_blocks()) + "]");

  // ---- and it is safe to carry on afterwards ------------------------------------------
  // The caches rebuild. This is what makes shutdown() callable at any moment rather than only
  // at the very end.
  check(builtin_layout("default") != nullptr, "…and the caches REBUILD, so the library still works after it");
  // …and THE SECOND REGISTRATION IS THE PROOF THE FIRST WAS RELEASED, not merely
  // unaccounted (m2's shape): a name still live in the registry with a different source rule
  // is refused, so this succeeding means the table really was handed back.
  {
    constexpr std::string_view kProbeKind = "lifetime-probe-kind", kProbeDescribes = "a probe";
    const int register_ok = rolltui_widget_kind_register(
        kProbeKind.data(), kProbeKind.size(), ROLLTUI_SOURCE_REQUIRED, kProbeDescribes.data(), kProbeDescribes.size());
    const std::string why =
        register_ok == ROLLTUI_REGISTER_OK ? std::string() : ("code " + std::to_string(register_ok));
    check(register_ok == ROLLTUI_REGISTER_OK,
          "…and the widget-kind registry took the same name with a DIFFERENT rule, which is only "
          "possible because shutdown() really released it [" +
              why + "]");
    // …and put it back the way it was found, so the pass below registers into an empty
    // registry rather than into this proof's leftovers.
    rolltui_widget_kind_clear();
  }
  paint_something();
  check(true, "…including painting a whole frame again");
  // REGISTERING THE SAME NAME AGAIN IS THE PROOF THE REGISTRY WAS REALLY EMPTIED: a second
  // registration of a live name is refused by design, so this can only pass if `shutdown()`
  // released the entry rather than merely leaving the bytes unaccounted.
  use_the_ported_modules("after a shutdown");

  rolltui_shutdown();
  check(live_bytes() == 0, "a second shutdown() is safe and still lands on zero");

  return report("rolltui lifetime_test");
}
