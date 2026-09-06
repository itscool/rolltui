/*
 * rolltui/tests/c_consumer_test.c — THE CLAIM THIS LIBRARY HAD NEVER EXECUTED ONCE
 * (plan/phase-16.md m6).
 *
 * THE FINDING THAT PUT THIS FILE HERE, and it was checkable in one command. `plan/phase-17.md`
 * reasons about *"a pure-C host"* five separate times and uses it as the standard for whether
 * something is REACHABLE rather than merely ported. Until this file existed,
 * `find . -name '*.c'` outside `rolltui/c/` returned exactly one result and it was a generated
 * Unicode table: 34,823 lines of C, 39 headers, a phase whose thesis is *the C API is the API*,
 * and not one consumer of it in the language it is written in.
 *
 * WHAT IT ASSERTS, AND WHAT IT DELIBERATELY DOES NOT. Almost nothing about CONTENT. Its whole
 * job is that the API is CALLABLE AND COMPLETE FROM C — a property no C++ consumer can test,
 * because every C++ consumer compiles the same headers under `__cplusplus`, where the structs
 * have constructors, `RolltuiStr` has `operator std::string_view`, and a missing C-callable
 * entry point is silently supplied by a method. The golden frames belong to
 * `studio_golden_test`; the numbers belong to `budget_test`; the leak gauge belongs to
 * `lifetime_test`. This file belongs to the LANGUAGE.
 *
 * ---- THE THREE THINGS THAT MAKE IT A CONTROL RATHER THAN A DEMO -----------------------------
 *
 *   1. **IT IS COMPILED AS C, AND THAT IS ENFORCED TWICE.** `rolltui/CMakeLists.txt` sets the
 *      target's `LANGUAGE C` (a C++ compiler accepting this file would prove nothing), and the
 *      `#error` below fails the build if it is ever fed to a C++ compiler anyway. Two
 *      independent guards, because a build property that has never been violated is exactly
 *      the reports-zero shape CLAUDE.md keeps recording.
 *
 *   2. **IT INCLUDES `rolltui/rolltui.h` AND NOTHING ELSE OF THE LIBRARY'S.** `<stdio.h>` and
 *      `<string.h>` are the C standard library, not rolltui, and printing is how a test speaks.
 *      It does NOT include `rolltui/tests/rolltui_test.hpp` — that harness is C++ (it is built
 *      out of `std::string`), so the fifteen lines below are this file's own. When Phase 16 m1
 *      extracts the shared testing module, its control-point core is C by design and this file
 *      is one of its consumers; the harness here is deliberately the smallest thing that can
 *      count, not a third copy of anything.
 *
 *   3. **THE GAUGE IS PROVED ARMED BEFORE THE ZERO IS BELIEVED.** `live_bytes == 0` after
 *      `rolltui_shutdown()` is the assertion this file exists to make, and a counter that is
 *      blind reports zero just as cheerfully as a library that holds nothing. So the first
 *      check deliberately retains 64 KB through the library's own entry point, watches the
 *      number rise, and watches it fall again — `lifetime_test.cpp`'s own rule, restated in C
 *      because a control that is only true in the other language is not a control here.
 */
#ifdef __cplusplus
#error "c_consumer_test.c must be compiled as C. A C++ compiler accepting it proves nothing: \
under __cplusplus every struct below gains constructors and every RolltuiStr gains conversions, \
which is precisely the padding this file exists to run without."
#endif

#include <stdio.h>
#include <string.h>

#include "rolltui/rolltui.h"

/* ---- the harness: fifteen lines, and it fails on zero assertions --------------------------
 * The same rule roll's `tests/test_util.hpp` has and `rolltui/tests/rolltui_test.hpp` does not
 * (measured 2026-09-04, and it is Phase 16 m2's second mechanism): a suite that ran NOTHING
 * must not read as success. It is three lines here, so it is here now rather than after m2 —
 * a file whose whole subject is a check that never fired should not ship without it. */
static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char* name) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  if (cond) ++g_pass; else ++g_fail;
}

static int report(const char* suite) {
  if (g_pass == 0 && g_fail == 0) {
    printf("\n%s: ZERO ASSERTIONS — that is a failure, not a pass\n", suite);
    return 1;
  }
  printf("\n%s: %d passed, %d failed — %s\n", suite, g_pass, g_fail, g_fail == 0 ? "ALL PASS" : "FAILURES");
  return g_fail == 0 ? 0 : 1;
}

static size_t live_bytes(void) {
  size_t v = 0;
  rolltui_mem_stats(NULL, NULL, NULL, &v, NULL, NULL);
  return v;
}

static size_t live_blocks(void) {
  size_t v = 0;
  rolltui_mem_stats(NULL, NULL, NULL, NULL, NULL, &v);
  return v;
}

/* ---- the app: everything a screen needs, held by a plain C struct --------------------------
 * `rolltui.h` rule 1 says a handle is created and released in a pair and there is no RAII to
 * lean on. In C there is no RAII to DECLINE either, which is the whole point of running this:
 * every one of these is freed by hand at the bottom of `main`, and `live_bytes == 0` is what
 * says the hand was right. */
typedef struct App {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT];
  RolltuiEffectMap* effects;
  RolltuiWindows* windows;
  RolltuiWindowStack* stack;
  RolltuiBindings* bindings;
  RolltuiComposeScratch* compose_scratch;
  RolltuiLayout layout;
  int w, h;
} App;

/* The `rows:status` source the shipped `default` layout names. A DECISION going in, in
 * `rolltui.h` rule 5's terms — the library asks the host what its rows are, per frame. */
static void status_rows(void* ctx, RolltuiRows* out) {
  (void)ctx;
  rolltui_rows_add(out, "host", 4, "c_consumer_test", 15);
  rolltui_rows_add(out, "language", 8, "C", 1);
}

/* The `input:prompt` source's other half. A source with no submit bound is a NAMED problem
 * and a visible error panel — not a crash and not silence, which is the library behaving as
 * `rolltui_widgets.h` says and which this file found out by being the first consumer that
 * had never read a host's setup code. */
static void on_submit(void* ctx, const char* text, size_t len) {
  (void)ctx;
  (void)text;
  (void)len;
}

/* The slot the composer calls per resolved window. */
static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  App* a = (App*)ctx;
  rolltui_windows_draw(a->windows, rn, f, a->styles, rolltui_windows_default_roles());
}

static RolltuiRect screen_rect(const App* a) {
  RolltuiRect r;
  r.x = 0;
  r.y = 0;
  r.w = a->w;
  r.h = a->h;
  return r;
}

int main(void) {
  App app;
  memset(&app, 0, sizeof app);
  app.w = 100;
  app.h = 30;

  /* ---- 0. THE GAUGE, ARMED — before any zero below is believed ---------------------------- */
  {
    const size_t base = live_bytes();
    void* held = rolltui_mem_alloc(64 * 1024);
    check(live_bytes() >= base + 64 * 1024, "the gauge SEES a deliberate retention");
    rolltui_mem_free(held);
    check(live_bytes() == base, "…and sees it released again, so a zero at the end means something");
  }

  /* ---- 1. A SHIPPED THEME, from the embedded file a fresh install runs --------------------- */
  app.effects = rolltui_theme_builtin_fill("default-dark", 12, app.styles, ROLLTUI_ROLE_COUNT);
  check(app.effects != NULL, "a shipped theme fills a C caller's own style table");

  /* THE SAME FILE THE OTHER WAY ROUND: its BYTES, through the whole preset chain, with no C++
   * anywhere in it. This is the rung a host that ships its own theme files stands on, and it
   * is three calls rather than one because a PRESET FILE IS NOT A THEME FILE — the shipped
   * `default-dark.json` is `{name, mode, depth, colours:{roles:{…}}}` and `rolltui_theme_load`
   * wants the `colours` object. `rolltui_theme_preset_parse` is what knows that, and it takes
   * the two setting validators as callbacks; a C caller passes the library's own, which
   * `rolltui_theme.h` says is exactly what they became once a C file could spell the names. */
  {
    const char* text = rolltui_embedded_text(rolltui_kThemePresets, rolltui_kThemePresetCount, "default-dark", 12);
    check(text != NULL && text[0] != '\0', "the shipped theme FILE's bytes are reachable from C");
    if (text != NULL) {
      RolltuiStr err = {0};
      RolltuiJsonValue* root = rolltui_json_parse(text, strlen(text), &err);
      check(root != NULL, "…and parse as JSON");
      if (root != NULL) {
        RolltuiStr mode = {0};
        RolltuiStr depth = {0};
        const RolltuiJsonValue* colours = NULL;
        RolltuiThemePresetReport prep = {0};
        const int ok =
            rolltui_theme_preset_parse(root, rolltui_theme_default_vocab(), rolltui_theme_mode_setting_valid,
                                       rolltui_color_depth_setting_valid, &mode, &depth, &colours, &prep);
        check(ok != 0 && colours != NULL, "…and parse as a theme PRESET, with the library's own validators");
        if (colours != NULL) {
          RolltuiStyle parsed[ROLLTUI_ROLE_COUNT];
          RolltuiStr name = {0};
          RolltuiThemeReport rep = {0};
          RolltuiEffectMap* fx =
              rolltui_theme_load(colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), parsed, &name, &rep);
          check(fx != NULL && rep.missing_roles_n == 0, "…and its colours load with every role defined");
          rolltui_effect_map_free(fx);
          rolltui_theme_report_release(&rep);
          rolltui_str_free(&name);
        }
        rolltui_theme_preset_report_release(&prep);
        rolltui_str_free(&mode);
        rolltui_str_free(&depth);
      }
      rolltui_json_free(root);
      rolltui_str_free(&err);
    }
  }

  /* ---- 2. A SHIPPED LAYOUT, through the loader a host walks ------------------------------- */
  rolltui_layout_init(&app.layout);
  {
    size_t text_len = 0;
    const char* text = rolltui_layout_builtin_json("default", 7, &text_len);
    check(text != NULL && text_len != 0, "the shipped `default` layout's bytes are reachable from C");

    RolltuiLoadedLayout loaded;
    RolltuiLayoutReport rep;
    size_t defaults_n = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(&defaults_n);
    int ok = 0;
    memset(&rep, 0, sizeof rep);
    rolltui_loaded_layout_init(&loaded);
    check(defaults != NULL && defaults_n != 0, "the shipped screen's own actions are reachable from C");
    ok = rolltui_load_layout_text(text != NULL ? text : "", text_len, &loaded, defaults, defaults_n,
                                  rolltui_layout_default_hooks(), &rep);
    check(ok != 0, "it loads");
    check(rolltui_layout_report_clean(&rep) != 0, "…with a clean report");
    if (ok != 0) rolltui_loaded_layout_to_layout(&loaded, &app.layout);
    rolltui_loaded_layout_release(&loaded);
    rolltui_layout_report_release(&rep);
  }
  check(app.layout.actions.n != 0, "the layout a C host holds carries the screen's declared actions");

  /* ---- 3. THE WINDOW STACK ---------------------------------------------------------------- */
  app.windows = rolltui_windows_new();
  rolltui_windows_set_library_defaults(app.windows); /* the eight kinds and the five vocabularies */
  app.stack = rolltui_window_stack_new();
  app.bindings = rolltui_bindings_clone(rolltui_bindings_default());
  app.compose_scratch = rolltui_compose_scratch_new();

  rolltui_window_stack_set_base(app.stack, &app.layout.base);
  rolltui_bindings_declare(app.bindings, app.layout.actions.v, app.layout.actions.n, NULL, 0);

  /* The three sources the shipped screen names. Unbound, each is a NAMED problem and a visible
   * error panel, which is the library working as designed — this file binds them so that the
   * report count below is an assertion about the API rather than about this file's screen. */
  rolltui_windows_bind_sample_document(app.windows, "session", 7, "# hello from C\n\nA pure-C consumer.\n", 36);
  rolltui_windows_bind_rows(app.windows, "status", 6, status_rows, &app, NULL);
  rolltui_windows_bind_submit(app.windows, "prompt", 6, on_submit, &app, NULL, /*on_submit=*/0);

  {
    RolltuiWidgetEnv env;
    env.ambiguous_wide = 0;
    env.now_ms = 0;
    rolltui_windows_set_env(app.windows, &env);
    rolltui_windows_set_bindings(app.windows, app.bindings);
    rolltui_windows_sync(app.windows, app.stack);
    rolltui_windows_autosize(app.windows, app.stack, screen_rect(&app));
    rolltui_windows_layout(app.windows, app.stack, screen_rect(&app));
  }
  {
    size_t n = rolltui_windows_report_count(app.windows);
    if (n != 0) {
      size_t i = 0;
      for (; i < n; ++i) {
        size_t len = 0;
        const char* line = rolltui_windows_report_at(app.windows, i, &len);
        printf("  note: window report: %.*s\n", (int)len, line != NULL ? line : "");
      }
    }
    check(n == 0, "every window of the shipped screen resolves for a C host");
  }
  check(rolltui_window_stack_focused(app.stack) != NULL, "the stack has a focused window");

  /* ---- 3b. THE GAP THIS FILE FOUND ON ITS FIRST RUN --------------------------------------- *
   * Opening a popup the SCREEN declared. Two hosts had hand-written it at six call sites, and
   * `studio.cpp` wrote three of them as `RolltuiLayer copy = *p;` — a deep copy in C++ and a
   * shallow one in C, so the identical line here would alias the layout's own buffers and
   * double-free at `rolltui_shutdown()`. Measured before it was fixed; the story is at
   * `rolltui_window_stack_push_popup`'s declaration. The assertion is that a C consumer can do
   * the operation AT ALL, which until this call it could not without knowing the move
   * contract of a function it had no reason to read. */
  {
    const size_t before = rolltui_window_stack_depth(app.stack);
    check(rolltui_window_stack_push_popup(app.stack, &app.layout, "help", 4) != 0,
          "a C host can open a popup the SCREEN declared");
    check(rolltui_window_stack_depth(app.stack) == before + 1, "…the stack is one deeper");
    check(rolltui_window_stack_push_popup(app.stack, &app.layout, "nosuch", 6) == 0,
          "…and an id the screen does not declare pushes nothing");
    check(rolltui_window_stack_depth(app.stack) == before + 1, "…the stack is unchanged by that");
    check(rolltui_window_stack_pop(app.stack) != 0, "…and it pops again");
  }

  /* ---- 3c. AN EVENT, ROUTED AND DELIVERED ------------------------------------------------- */
  {
    RolltuiEvent e;
    RolltuiStr window = {0};
    unsigned char route;
    memset(&e, 0, sizeof e);
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = ROLLTUI_KEY_CHAR;
    e.key.ch = (RolltuiCodepoint)'h';
    route = rolltui_window_stack_route(app.stack, &e, screen_rect(&app), app.bindings,
                                       rolltui_stack_default_actions(), &window);
    check(route == ROLLTUI_ROUTE_DELIVER, "a key routes to a window");
    check(rolltui_windows_handle(app.windows, window.p != NULL ? window.p : "", window.n, &e) != 0,
          "…and the focused widget consumes it");
    {
      const RolltuiInput* in = rolltui_windows_input(app.windows, "prompt", 6);
      size_t len = 0;
      const char* text = in != NULL ? rolltui_input_text(in, &len) : NULL;
      check(len == 1 && text != NULL && text[0] == 'h', "…and the input holds what was typed");
    }
    rolltui_str_free(&window);
  }

  /* ---- 4. ONE FRAME, THROUGH THE LIBRARY'S OWN DOUBLE BUFFER ------------------------------ */
  {
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, *rolltui_theme_style(app.styles, ROLLTUI_ROLE_COUNT,
                                                                           ROLLTUI_ROLE_BACKGROUND));
    RolltuiStr bytes = {0};
    RolltuiStr text = {0};
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h,
                                         *rolltui_theme_style(app.styles, ROLLTUI_ROLE_COUNT,
                                                              ROLLTUI_ROLE_BACKGROUND));
    check(f != NULL, "the swap lends a back frame");
    rolltui_window_stack_compose(app.stack, f, screen_rect(&app), app.styles, rolltui_layout_default_roles(),
                                 draw_slot, &app, /*ambiguous_wide=*/0, app.compose_scratch);

    /* Rendered to text, which is the shape with no terminal in it. */
    rolltui_frame_to_text(f, &text);
    {
      size_t lines = 0;
      size_t i = 0;
      for (; i < text.n; ++i)
        if (text.p[i] == '\n') ++lines;
      check(text.n != 0, "the frame renders to text");
      check(lines == (size_t)app.h, "…one line per row");
      check(strstr(text.p != NULL ? text.p : "", "c_consumer_test") != NULL,
            "…and the row this C file bound is on it");
    }

    rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &bytes);
    check(bytes.n != 0, "the first present emits bytes");
    check(rolltui_swap_front(swap) != NULL, "…and the presented frame is readable back");

    /* An unchanged second frame emits nothing — the diff, from C. */
    rolltui_str_clear(&bytes);
    f = rolltui_swap_begin(swap, app.w, app.h,
                           *rolltui_theme_style(app.styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND));
    rolltui_window_stack_compose(app.stack, f, screen_rect(&app), app.styles, rolltui_layout_default_roles(),
                                 draw_slot, &app, /*ambiguous_wide=*/0, app.compose_scratch);
    rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &bytes);
    check(bytes.n == 0, "an identical second frame emits nothing");

    rolltui_str_free(&text);
    rolltui_str_free(&bytes);
    rolltui_swap_free(swap);
  }

  /* ---- 5. RELEASE, BY HAND, AND THE NUMBER THAT SAYS THE HAND WAS RIGHT -------------------- */
  rolltui_compose_scratch_free(app.compose_scratch);
  rolltui_window_stack_free(app.stack);
  rolltui_windows_free(app.windows);
  rolltui_bindings_free(app.bindings);
  rolltui_layout_release(&app.layout);
  rolltui_effect_map_free(app.effects);

  rolltui_shutdown();
  {
    const size_t b = live_bytes();
    const size_t n = live_blocks();
    if (b != 0 || n != 0) printf("  note: %zu bytes in %zu blocks still held\n", b, n);
    check(b == 0 && n == 0, "AFTER shutdown() THE LIBRARY HOLDS NOTHING, for a C consumer too");
  }

  return report("c_consumer_test");
}
