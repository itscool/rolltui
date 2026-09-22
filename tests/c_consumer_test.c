/*
 * rolltui/tests/c_consumer_test.c — THE CLAIM THIS LIBRARY HAD NEVER EXECUTED ONCE
 *.
 *
 * THE FINDING THAT PUT THIS FILE HERE, and it was checkable in one command. the plan
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
 *   2. **IT INCLUDES `rolltui/rolltui.h` AND NOTHING ELSE OF THE LIBRARY'S.** `<stdio.h>`,
 *      `<stdlib.h>` and `<string.h>` are the C standard library and `<unistd.h>` is POSIX —
 *      not rolltui; printing is how a test speaks, and `mkdtemp`/`rmdir` are how section 4b
 *      gets a directory of its own for a preset store and proves what the store left in it.
 *      It does NOT include `rolltui/tests/rolltui_test.hpp` — that harness is C++ (it is built
 *      out of `std::string`), so the fifteen lines below are this file's own — deliberately
 *      the smallest thing that can count, not a third copy of anything.
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rolltui/rolltui.h"
/* THE HARNESS IS THE SHARED ONE, AND IT IS THE REASON THAT MODULE IS C. This file used to
 * carry its own fifteen-line copy — a THIRD implementation of the same four ideas — because
 * every other harness in the repository was C++ and this file may include no C++ header. That
 * copy was the argument for extracting a C core rather than sharing a C++ one, and now that
 * the core exists the copy has no reason to. `testkit/` is a leaf: it includes nothing of
 * rolltui's and nothing of roll's, so including it here breaks nothing this file asserts.
 * The zero-assertion rule the copy carried is the module's now, for every suite on both sides.
 * `check` and `report` below are one-line names for the module's calls, kept so the ~70 call
 * sites in this file read as they did. */
#include "testkit/testkit.h"

static void check(int cond, const char* name) { testkit_check(cond, name); }
static int report(const char* suite) { return testkit_report(suite); }

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

/* ---- the app: everything a screen needs, held by a plain C struct ---------------------------
 * `rolltui.h` rule 1 says a handle is created and released in a pair and there is no RAII to
 * lean on. In C there is no RAII to DECLINE either, which is the whole point of running this:
 * every one of these is freed by hand at the bottom of `main`, and `live_bytes == 0` is what
 * says the hand was right. */
typedef struct App {
  RolltuiContext* ctx; /* Phase 25: the session every registry hangs off */
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT];
  RolltuiEffectMap* effects;
  RolltuiWindows* windows;
  RolltuiWindowStack* stack;
  RolltuiBindings* bindings;
  RolltuiComposeScratch* compose_scratch;
  RolltuiLayout* layout; /* OWNED: a handle since Phase 23 */
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
  app.ctx = rolltui_context_new();

  /* ---- 0. THE GAUGE, ARMED — before any zero below is believed --------------------------- */
  {
    const size_t base = live_bytes();
    void* held = rolltui_mem_alloc(64 * 1024);
    check(live_bytes() >= base + 64 * 1024, "the gauge SEES a deliberate retention");
    rolltui_mem_free(held);
    check(live_bytes() == base, "…and sees it released again, so a zero at the end means something");
  }

  /* ---- 1. A SHIPPED THEME, from the embedded file a fresh install runs ------------------- */
  app.effects = rolltui_theme_builtin_fill("default-dark", 12, app.styles, ROLLTUI_ROLE_COUNT);
  check(app.effects != NULL, "a shipped theme fills a C caller's own style table");

  /* THE SAME FILE THE OTHER WAY ROUND: its BYTES, through the whole preset chain, with no C++
   * anywhere in it. This is the rung a host that ships its own theme files stands on, and it
   * is three calls rather than one because a PRESET FILE IS NOT A THEME FILE — the shipped
   * `default.json` (the built-in `default-dark` reads it at a pinned mode — `builtin_source`,
   * rolltui_theme.c — rather than shipping its own file any more) is
   * `{name, mode, depth, colours:{roles:{…}}}` and `rolltui_theme_load` wants the `colours`
   * object. `rolltui_theme_preset_parse` is what knows that, and it takes the two setting
   * validators as callbacks; a C caller passes the library's own, which `rolltui_theme.h` says
   * is exactly what they became once a C file could spell the names. */
  {
    const char* text = rolltui_embedded_text(rolltui_kThemePresets, rolltui_kThemePresetCount, "default", 7);
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

  /* ---- 2. A SHIPPED LAYOUT, through the loader a host walks ------------------------------ */
  {
    size_t text_len = 0;
    const char* text = rolltui_layout_builtin_json("default", 7, &text_len);
    RolltuiLayoutReport rep;
    size_t defaults_n = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(app.ctx, &defaults_n);
    size_t actions_n = 0;
    check(text != NULL && text_len != 0, "the shipped `default` layout's bytes are reachable from C");
    memset(&rep, 0, sizeof rep);
    check(defaults != NULL && defaults_n != 0, "the shipped screen's own actions are reachable from C");
    /* ONE call, and the layout comes back OWNED. A carrier to init, a load, an unpack and a
     * release is four calls every consumer writes identically. */
    app.layout = rolltui_load_layout_text(text != NULL ? text : "", text_len, defaults, defaults_n,
                                          rolltui_layout_default_hooks(), &rep);
    check(app.layout != NULL, "it loads, and the loader hands back an OWNED layout");
    check(rolltui_layout_report_clean(&rep) != 0, "…with a clean report");
    rolltui_layout_report_release(&rep);
    rolltui_layout_actions(app.layout, &actions_n);
    check(actions_n != 0, "the layout a C host holds carries the screen's declared actions");
  }

  /* ---- 3. THE WINDOW STACK --------------------------------------------------------------- */
  app.windows = rolltui_windows_new(app.ctx);
  rolltui_context_set_library_defaults(app.ctx); /* the eight kinds and the five vocabularies */
  app.stack = rolltui_window_stack_new();
  app.bindings = rolltui_bindings_clone(rolltui_bindings_default(app.ctx));
  app.compose_scratch = rolltui_compose_scratch_new();

  rolltui_window_stack_set_base(app.stack, rolltui_layout_base(app.layout));
  {
    size_t an = 0;
    const RolltuiLayoutAction* av = rolltui_layout_actions(app.layout, &an);
    rolltui_bindings_declare(app.bindings, av, an, NULL, 0);
  }

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
    rolltui_context_set_env(app.ctx, &env);
    rolltui_context_set_bindings(app.ctx, app.bindings);
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
    check(rolltui_window_stack_push_popup(app.stack, app.layout, "help", 4) != 0,
          "a C host can open a popup the SCREEN declared");
    check(rolltui_window_stack_depth(app.stack) == before + 1, "…the stack is one deeper");
    /* A DOOR TAKES A HANDLE, AND A HANDLE MAY BE NULL. Every accessor guards; this asserts the
     * one that did not previously — a failed load handed straight to `_popup` segfaulted a
     * host test, and the guard was added without a check until this line. */
    check(rolltui_layout_popup(NULL, "help", 4) == NULL, "a door takes a NULL handle and answers NULL");
    check(rolltui_window_stack_push_popup(app.stack, app.layout, "nosuch", 6) == 0,
          "…and an id the screen does not declare pushes nothing");
    check(rolltui_window_stack_depth(app.stack) == before + 1, "…the stack is unchanged by that");
    check(rolltui_window_stack_pop(app.stack) != 0, "…and it pops again");
  }

  /* ---- 3d. THE GUARD THAT NEEDS NO CLOSED TYPE ------------------------------- *
   * `enum class WidgetKind` is retired: a kind's NAME is its identity, and the registry is one
   * enumeration with the library's rows first. The safety property — rung 1 is never shadowed —
   * is two guards BY NAME, planted here from C, where no C++ special member can absorb the
   * answer (`layout_test.cpp` plants `input` through its C++ shims; this is `transcript`, raw). */
  {
    size_t row = (size_t)-1;
    unsigned char rule = 0;
    const size_t rows_before = rolltui_widget_kind_count(app.ctx);
    check(rolltui_widget_kind_register(app.ctx, "transcript", 10, ROLLTUI_SOURCE_REQUIRED, "", 0) ==
              ROLLTUI_REGISTER_IS_LIBRARY,
          "registering a library kind's name is refused, by name, from C");
    check(rolltui_widget_kind_resolve(app.ctx, "transcript", 10, &row, &rule, NULL, NULL) == ROLLTUI_KIND_LIBRARY &&
              row < rolltui_widget_kind_library_count() && rule == ROLLTUI_SOURCE_REQUIRED,
          "...and it still resolves at rung 1, as a row inside the library's boundary, with its own rule");
    check(rolltui_widget_kind_count(app.ctx) == rows_before, "...and the refused registration added no row");
    check(rolltui_widget_kind_source_shape(row) == ROLLTUI_SOURCE_SHAPE_NAME,
          "...and its source SHAPE is a row of the registry, readable from C, not a C++ enum compare");
  }

  /* ---- 3e. TWO CONTEXTS, AND THEY SHARE NOTHING ------------------------------ *
   * A `RolltuiContext` is a real single rolltui session, and nothing in the library is global.
   * With the widget-kind registry as file-scope statics, two apps in one process share one
   * table and cannot be told apart. **This is the assertion that says they can.** It is written
   * from C on purpose, for the same reason 3d is: no C++ special member can absorb the answer.
   *
   * WHAT IT DOES NOT ASSERT, and the header says why: not `live_bytes == 0` PER context. The
   * allocator counters are one process-wide atomic sum (contract point 6), so the zero is taken
   * after freeing BOTH, at the end of this file — which also proves neither leaked into the
   * other. Making an allocation carry a context would touch every allocation in the library for
   * a property nothing needs. */
  {
    RolltuiContext* a = rolltui_context_new();
    RolltuiContext* b = rolltui_context_new();
    size_t row_a = (size_t)-1, row_b = (size_t)-1;
    check(a != NULL && b != NULL && a != b, "two contexts in one process");
    check(rolltui_widget_kind_register(a, "gauge", 5, ROLLTUI_SOURCE_REQUIRED, "a number", 8) == ROLLTUI_REGISTER_OK,
          "…a host kind registers into the first");
    check(rolltui_widget_kind_resolve(a, "gauge", 5, &row_a, NULL, NULL, NULL) == ROLLTUI_KIND_HOST &&
              row_a >= rolltui_widget_kind_library_count(),
          "…the first resolves it, as a row past the library's boundary");
    check(rolltui_widget_kind_resolve(b, "gauge", 5, &row_b, NULL, NULL, NULL) == ROLLTUI_KIND_UNKNOWN,
          "…AND THE SECOND DOES NOT SEE IT AT ALL — the registries are separate");
    check(rolltui_widget_kind_count(a) == rolltui_widget_kind_library_count() + 1 &&
              rolltui_widget_kind_count(b) == rolltui_widget_kind_library_count(),
          "…one row in the first, none in the second");
    /* And rung 1 is identical in both: a context owns rung 2 and never the library's own. */
    check(rolltui_widget_kind_resolve(a, "transcript", 10, NULL, NULL, NULL, NULL) == ROLLTUI_KIND_LIBRARY &&
              rolltui_widget_kind_resolve(b, "transcript", 10, NULL, NULL, NULL, NULL) == ROLLTUI_KIND_LIBRARY,
          "…and both resolve the library's own rung identically");
    rolltui_context_free(b);
    check(rolltui_widget_kind_resolve(a, "gauge", 5, NULL, NULL, NULL, NULL) == ROLLTUI_KIND_HOST,
          "…freeing one leaves the other's registry intact");
    rolltui_context_free(a);
  }

  /* ---- 3f. WHAT THE SPLIT ACTUALLY BUYS -----------------------------------------------------
   * A session is CONFIGURED ONCE and every screen it runs sees that configuration. Making the
   * same calls against a `RolltuiWindows` instead makes a second screen a second copy of the
   * setup, and splits a kind's NAME (in the context) from its FACTORY (on one screen), which is
   * one identity with two owners. */
  {
    RolltuiContext* c = rolltui_context_new();
    RolltuiWindows* w1;
    RolltuiWindows* w2;
    RolltuiWidget* got1;
    RolltuiWidget* got2;
    rolltui_context_set_library_defaults(c); /* ONCE, on the session */
    w1 = rolltui_windows_new(c);
    w2 = rolltui_windows_new(c);
    got1 = rolltui_windows_widget_for(w1, "text:hello", 10);
    got2 = rolltui_windows_widget_for(w2, "text:hello", 10);
    check(got1 != NULL && got1->vt != NULL,
          "one session configured ONCE builds a widget on its first screen");
    check(got2 != NULL && got2->vt != NULL,
          "…and on a SECOND screen with no second configuration — the kinds are the session's");
    check(got1 != got2, "…while the two INSTANCES are distinct: a kind is a session's, an instance a screen's");
    rolltui_windows_free(w2);
    check(rolltui_windows_widget_for(w1, "text:hello", 10) != NULL,
          "…and freeing one screen leaves the other's widgets standing, because the table it read is the session's");
    rolltui_windows_free(w1);
    rolltui_context_free(c);
  }

  /* ---- 3c. AN EVENT, ROUTED AND DELIVERED ------------------------------------------------ */
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

  /* ---- 4. ONE FRAME, THROUGH THE LIBRARY'S OWN DOUBLE BUFFER ----------------------------- */
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

  /* ---- 4b. A PRESET STORE, OPENED FROM C ------------------------------------ *
   * A CONTROL THAT EXISTS BUT DOES NOT REACH ITS GUARANTEE IS INDISTINGUISHABLE FROM ONE THAT
   * DOES. Section 1 reaches the shipped theme through `rolltui_theme_builtin_fill` and the
   * embedded bytes, which is a different rung of the same domain — so it says nothing about a
   * `RolltuiThemeStore`, the stateful handle every real host opens instead. This section opens
   * one.
   *
   * What it does is what an ORDINARY HOST does, in the order a host does it, with no C++
   * anywhere in the chain: open a Theme store on a directory with nothing in it; start it;
   * read the working copy and USE it (its colours fill a style table, which is what a host
   * draws from); list; choose light-or-dark, a SETTING and not an edit; get a preset by name
   * without switching to it; load a different existing preset; then open a SECOND store on the
   * same directory and see the working copy — origin and the person's chosen mode both — come
   * back through the file. Then a Layout and a Bindings store, each compared against a rung
   * this file already stood on, so the two paths are proved to agree.
   *
   * AUTHORING A PRESET'S CONTENT — `_edit`/`_save_as`/`_add` — is NOT here, on purpose: nothing
   * this file does is shaped like rolltui's own studio, the one program that ever calls them,
   * and they live in `rolltui_studio.h`, a header only the studio and its editors may include.
   * `tests/presets_test.cpp` is where that half is proved, from C++, through that header.
   *
   * THE WALLS THIS SECTION HIT ON ITS FIRST RUN — five, each counted across every
   * consumer before anything was designed, and each moved INTO the C API rather than into a
   * sixth wrapper (`rolltui_presets.h` carries the case at each declaration):
   *   - the three domain descriptors were assembled BY EVERY CONSUMER from the same
   *     library-owned arguments (five consumers, fifteen `_init` calls, two never releasing
   *     the cache) — a concrete `_new` is the library's own assembly, and the descriptor
   *     itself is no longer a caller's to see;
   *   - `_new` and `_start` each took a SECOND report the caller had to make and throw away
   *     (twenty-six call sites) — the domain makes its own;
   *   - a value from `_working` could only be freed through the DOMAIN's `destroy` (sixteen
   *     call sites reaching past the store) — `_working` is a BORROW now, nothing to free;
   *   - `save_as` answered a refusal with a CODE and left `err` empty, so three consumers
   *     folded `rolltui_preset_save_result_text` in by hand, identically — `err` carries it;
   *   - `working_value` had to be TOLD which domain the store is, in an enum every caller
   *     kept in step with the store — gone with the generic engine's public half; a mode/depth
   *     setting is read from the working copy this file already holds.
   * What did NOT move is the milestone's other half, and the reason is NOT the language: what a
   * C++ host still wraps is the conversion of a borrow or a caller-filled buffer into an owning
   * `std::string`, which this file needs none of — and neither does C++, since `RolltuiStr` is
   * the C++ type already. That residue is a host DEFAULT, judged per call site: on an event a
   * choice, on a frame an allocation. */
  {
    char dir[512];
    size_t dir_len = 0;

    /* A scratch directory of this file's own. `mkdtemp` is POSIX, not rolltui; the store
     * takes a directory it did not create and creates the files under it itself. */
    {
      const char* tmp = getenv("TMPDIR");
      snprintf(dir, sizeof dir, "%s/rolltui-c-consumer-XXXXXX", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
      check(mkdtemp(dir) != NULL, "a scratch directory for the store");
      dir_len = strlen(dir);
    }

    /* ---- the Theme store, through the USE surface an ordinary host has -------------------- */
    {
      RolltuiThemePresetReport rep;
      RolltuiThemeStore* ts;
      RolltuiStr label = {0};
      RolltuiPresetList list = {0};
      RolltuiStrList shipped = {0};
      unsigned long long v0;
      memset(&rep, 0, sizeof rep);

      ts = rolltui_theme_store_new(app.ctx, dir, dir_len, /*may_write_shipped=*/0, "", 0);
      check(ts != NULL, "a Theme store opens from C on a directory with nothing in it, with no report to supply");
      rolltui_theme_store_start(ts, &rep);
      check(rolltui_theme_preset_report_clean(&rep) != 0 && rep.notes_n == 1,
            "…starts clean, with one note saying it began from the shipped 'default'");
      {
        size_t n = 0;
        const char* o = rolltui_theme_store_origin(ts, &n);
        check(n == 7 && memcmp(o, "default", 7) == 0, "…its origin is 'default'");
      }
      check(rolltui_theme_store_modified(ts) == 0, "…and it is not modified");
      rolltui_theme_store_label(ts, &label);
      check(rolltui_str_eq(&label, "default", 7) != 0, "…so its label is the origin alone");
      rolltui_theme_store_label(ts, &label); /* again, into the same buffer, with no clear */
      check(rolltui_str_eq(&label, "default", 7) != 0,
            "…and a second call into the same buffer REPLACES it — a host's frame can refill one buffer");
      v0 = rolltui_theme_store_version(ts);

      /* THE WORKING COPY, read and USED: a BORROW, valid until the next call that changes it —
       * never a caller's to free, unlike the generic engine's clone. */
      {
        const RolltuiThemePresetValue* tv = rolltui_theme_store_working(ts);
        check(tv != NULL && tv->colours != NULL && rolltui_str_eq(&tv->mode, "auto", 4) != 0 &&
                  rolltui_str_eq(&tv->depth, "auto", 4) != 0,
              "the working copy is a value a C caller can read: colours, mode, depth");
        if (tv != NULL) {
          RolltuiStyle table[ROLLTUI_ROLE_COUNT];
          RolltuiStr name = {0};
          RolltuiThemeReport tr = {0};
          RolltuiEffectMap* fx =
              rolltui_theme_load(tv->colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), table, &name, &tr);
          check(fx != NULL && tr.missing_roles_n == 0,
                "…and its colours fill a style table with every role defined: the store's value is the loader's input");
          rolltui_effect_map_free(fx);
          rolltui_theme_report_release(&tr);
          rolltui_str_free(&name);
        }
      }

      /* THE LISTING: the library's own list type, replaced per call, checked against the
       * shipped names this store's own context can name — no domain descriptor needed. */
      rolltui_theme_shipped_names(app.ctx, &shipped);
      rolltui_theme_store_list(ts, &list);
      {
        size_t i = 0;
        int all_shipped = list.n != 0;
        for (; i < list.n; ++i)
          if (!list.v[i].shipped || list.v[i].path.n != 0) all_shipped = 0;
        check(list.n == shipped.n && all_shipped && rolltui_str_eq(&list.v[0].name, "default", 7) != 0,
              "the listing is exactly the shipped themes, 'default' first, each shipped and pathless");
      }

      /* LIGHT OR DARK IS THE PERSON'S: a named call, not an edit — it moves the version and
       * leaves the theme unmodified, and the choice AUTOSAVES beside the preset's own colours. */
      rolltui_theme_store_set_mode(ts, "light", 5, /*persist=*/1);
      check(rolltui_theme_store_modified(ts) == 0 && rolltui_theme_store_version(ts) > v0,
            "choosing light from C bumps the version and is NOT an edit of the theme");
      {
        RolltuiStr path = {0};
        RolltuiStr text = {0};
        rolltui_theme_store_path(ts, NULL, 0, &path);
        check(rolltui_preset_read_file(path.p, path.n, rolltui_str_put, &text) != 0 && text.n != 0,
              "…and the choice AUTOSAVED: the working file is on disk at the path the store names");
        rolltui_str_free(&path);
        rolltui_str_free(&text);
      }

      /* GET a preset by name WITHOUT switching to it — reading a theme's mode before choosing
       * it, say. OWNED: freed with `rolltui_theme_preset_value_free`, unlike `_working` above. */
      {
        RolltuiThemePresetReport grep_;
        RolltuiThemePresetValue* g;
        memset(&grep_, 0, sizeof grep_);
        g = rolltui_theme_store_get(ts, "mono", 4, &grep_);
        check(g != NULL && rolltui_theme_preset_report_clean(&grep_) != 0 && rolltui_str_eq(&g->mode, "auto", 4) != 0,
              "get by name reads a shipped preset's own value without switching to it");
        if (g != NULL) rolltui_theme_preset_value_free(g);
        rolltui_theme_preset_report_release(&grep_);
        g = rolltui_theme_store_get(ts, "nosuch", 6, &grep_);
        check(g == NULL && grep_.error.n != 0, "…and a name that is not shipped and not saved is NULL, with the report saying why");
        rolltui_theme_preset_report_release(&grep_);
      }

      /* LOAD a shipped preset into the working copy: the origin follows it, and the person's
       * own chosen mode carries over onto it — a preset never brings settings of its own. */
      check(rolltui_theme_store_load(ts, "mono", 4, &rep, /*persist=*/1) != 0 &&
                rolltui_theme_preset_report_clean(&rep) != 0 && rolltui_theme_store_modified(ts) == 0,
            "load a shipped preset from C: the working copy is replaced whole, unmodified");
      {
        size_t n = 0;
        const char* o = rolltui_theme_store_origin(ts, &n);
        const RolltuiThemePresetValue* tv = rolltui_theme_store_working(ts);
        check(n == 4 && memcmp(o, "mono", 4) == 0 && tv != NULL && rolltui_str_eq(&tv->mode, "light", 5) != 0,
              "…the origin followed it, and the person's chosen mode carried over onto the new preset");
      }

      /* THE ROUND TRIP: a second store on the same directory starts from what the first one
       * autosaved — the origin AND the person's own setting both — with no C++ anywhere
       * between the write and the read. */
      {
        RolltuiThemeStore* again = rolltui_theme_store_new(app.ctx, dir, dir_len, 0, "", 0);
        RolltuiThemePresetReport rep2;
        size_t n = 0;
        const char* o;
        const RolltuiThemePresetValue* tv;
        memset(&rep2, 0, sizeof rep2);
        rolltui_theme_store_start(again, &rep2);
        o = rolltui_theme_store_origin(again, &n);
        tv = rolltui_theme_store_working(again);
        if (rolltui_theme_preset_report_clean(&rep2) == 0 || n != 4 || memcmp(o, "mono", 4) != 0 ||
            rolltui_theme_store_modified(again) != 0 || tv == NULL || !rolltui_str_eq(&tv->mode, "light", 5)) {
          RolltuiStr why = {0};
          rolltui_theme_preset_report_summary(&rep2, &why);
          printf("  note: second store: origin '%.*s', modified %d, mode '%.*s', report '%.*s', %zu note(s)\n", (int)n, o,
                 rolltui_theme_store_modified(again), tv != NULL ? (int)tv->mode.n : 0, tv != NULL ? tv->mode.p : "",
                 (int)why.n, why.p != NULL ? why.p : "", rep2.notes_n);
          rolltui_str_free(&why);
        }
        check(rolltui_theme_preset_report_clean(&rep2) != 0 && n == 4 && memcmp(o, "mono", 4) == 0 &&
                  rolltui_theme_store_modified(again) == 0 && tv != NULL && rolltui_str_eq(&tv->mode, "light", 5) != 0,
              "a SECOND store on the same directory starts from the autosaved working copy, origin AND setting both");
        rolltui_theme_store_free(again);
        rolltui_theme_preset_report_release(&rep2);
      }

      rolltui_theme_store_free(ts);
      rolltui_preset_list_release(&list);
      rolltui_str_list_release(&shipped);
      rolltui_str_free(&label);
      rolltui_theme_preset_report_release(&rep);
    }

    /* ---- the Layout store: its working copy IS the layout section 2 loaded by hand -------- */
    {
      RolltuiLayoutPresetReport rep;
      RolltuiLayoutStore* ls;
      RolltuiPresetList list = {0};
      RolltuiStrList shipped = {0};
      const RolltuiLayout* w;
      memset(&rep, 0, sizeof rep);
      ls = rolltui_layout_store_new(app.ctx, dir, dir_len, 0, "", 0);
      rolltui_layout_store_start(ls, &rep);
      w = rolltui_layout_store_working(ls);
      {
        size_t wn = 0, an = 0, w_actions_n = 0, a_actions_n = 0;
        const char* wname = w != NULL ? rolltui_layout_name(w, &wn) : NULL;
        const char* aname = rolltui_layout_name(app.layout, &an);
        if (w != NULL) rolltui_layout_actions(w, &w_actions_n);
        rolltui_layout_actions(app.layout, &a_actions_n);
        check(ls != NULL && w != NULL && rolltui_layout_preset_report_clean(&rep) != 0 && wn == an &&
                  (wn == 0 || memcmp(wname, aname, wn) == 0) && w_actions_n == a_actions_n,
              "a Layout store opens from C, and its working copy names the same screen, with the same actions, as "
              "the shipped default section 2 loaded through the standalone loader");
      }
      rolltui_layout_shipped_names(app.ctx, &shipped);
      rolltui_layout_store_list(ls, &list);
      check(list.n == shipped.n && list.n != 0 && rolltui_str_eq(&list.v[0].name, "default", 7) != 0,
            "…and it lists the shipped layouts, 'default' first");
      rolltui_preset_list_release(&list);
      rolltui_str_list_release(&shipped);
      rolltui_layout_store_free(ls);
      rolltui_layout_preset_report_release(&rep);
    }

    /* ---- the Bindings store: its working copy IS the library's default table -------------- */
    {
      RolltuiBindingsPresetReport rep;
      RolltuiBindingsStore* bs;
      const RolltuiBindings* w;
      const RolltuiBindings* def;
      RolltuiStr wj = {0}, dj = {0};
      memset(&rep, 0, sizeof rep);
      bs = rolltui_bindings_store_new(app.ctx, dir, dir_len, 0, "", 0);
      rolltui_bindings_store_start(bs, &rep);
      w = rolltui_bindings_store_working(bs);
      def = rolltui_bindings_default(app.ctx);
      /* No public equality over `RolltuiBindings*`: dump both to the same JSON shape under the
       * same name and compare the TEXT — publicly achievable, and every bit as exact. */
      if (w != NULL) rolltui_bindings_dump_json(w, "x", 1, &wj);
      if (def != NULL) rolltui_bindings_dump_json(def, "x", 1, &dj);
      check(bs != NULL && w != NULL && rolltui_bindings_preset_report_clean(&rep) != 0 &&
                rolltui_str_eq(&wj, dj.p, dj.n) != 0,
            "a Bindings store opens from C, and its working copy matches the library's own default table");
      rolltui_str_free(&wj);
      rolltui_str_free(&dj);
      rolltui_bindings_store_free(bs);
      rolltui_bindings_preset_report_release(&rep);
    }

    /* THE DIRECTORY HELD EXACTLY WHAT THIS FILE CAUSED — `rmdir` refuses a directory with
     * anything left in it, so a store that wrote something unexpected fails here by name. */
    {
      char path[600];
      int ok = 1;
      snprintf(path, sizeof path, "%s/theme.working.json", dir);
      ok = remove(path) == 0 && ok;
      ok = rmdir(dir) == 0 && ok;
      check(ok, "…and the directory held exactly the one file this section caused, and nothing else");
    }
    /* No domain release here: the three are the library's, and section 5's zero after
     * `rolltui_shutdown()` is what says the library let go of what it built for this section. */
  }

  /* ---- 4c. A MENU A C CONSUMER BUILDS, AND CAN FREE --------------------------------------
   * `rolltui_menu_item_init` is public and `rolltui_menu_load_json` FILLS a caller-owned item,
   * allocating into it. The matching `_release` was INTERNAL, so a C consumer
   * could build one and had no way to free it — `rolltui.h` rule 1 broken in the surface itself.
   *
   * IT SURVIVED A PROBE THAT REIMPLEMENTED THIS ENTIRE WIDGET. C++'s implicit destructor absorbs
   * the missing call, so the parity test could not feel the gap; only a consumer written in C
   * can. That is the same absorber that hid `rolltui_layer_copy`, one surface further out. */
  {
    const char* kMenu =
        "{\"root\":{\"kind\":\"submenu\",\"id\":\"root\",\"label\":\"top\","
        "\"items\":[{\"kind\":\"action\",\"id\":\"go\",\"label\":\"Go\"}]}}";
    const size_t before = live_bytes();
    RolltuiMenuItem it;
    RolltuiMenuLoadReport rep;
    memset(&rep, 0, sizeof rep);
    rolltui_menu_item_init(&it);
    check(rolltui_menu_parse_json(kMenu, strlen(kMenu), &it, &rep) != 0,
          "a C consumer parses a menu file into an item it owns");
    check(live_bytes() > before, "…and that allocated, so there is something to free");
    rolltui_menu_item_release(&it);
    rolltui_menu_load_report_release(&rep);
    check(live_bytes() == before,
          "…and `rolltui_menu_item_release` gives every byte back — the pair `_init` needs, "
          "reachable from C at last");
  }

  /* ---- 5. RELEASE, BY HAND, AND THE NUMBER THAT SAYS THE HAND WAS RIGHT ------------------ */
  rolltui_context_free(app.ctx);
  rolltui_compose_scratch_free(app.compose_scratch);
  rolltui_window_stack_free(app.stack);
  rolltui_windows_free(app.windows);
  rolltui_bindings_free(app.bindings);
  rolltui_layout_free(app.layout);
  rolltui_effect_map_free(app.effects);

  rolltui_shutdown();
  /* This zero also covers the three preset domains' parsed caches, which section 4b populated
   * and never released: they are the library's, and the library releases them. */
  {
    const size_t b = live_bytes();
    const size_t n = live_blocks();
    if (b != 0 || n != 0) printf("  note: %zu bytes in %zu blocks still held\n", b, n);
    check(b == 0 && n == 0, "AFTER shutdown() THE LIBRARY HOLDS NOTHING, for a C consumer too");
  }

  return report("c_consumer_test");
}
