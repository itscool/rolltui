/* examples/hello.c: a minimal rolltui program. It loads its screen from a layout, fills a list, shows a page of text, posts a
 * message when you pick a row, and quits on q. docs/guide.md quotes this file, and tests/hello_test.c runs it headless. */
#include <stdio.h>
#include <string.h>

#include "rolltui/rolltui.h"

/* The screen layout. A real program would normally load this from a file; it's inline to keep the example to one file. */
static const char kLayout[] =
    "{ \"name\": \"hello\", \"focus\": \"fruit\", \"root\": { \"row\": [\n"
    "  { \"id\": \"fruit\", \"content\": \"list:fruit\", \"size\": 24, \"border\": \"rounded\", \"title\": \"fruit\",\n"
    "    \"focusable\": true },\n"
    "  { \"id\": \"about\", \"content\": \"text:Up and Down move, Enter chooses, q quits.\", \"border\": \"rounded\",\n"
    "    \"title\": \"hello\" } ] } }";

static const char* const kFruit[] = {"apple", "banana", "cherry", "damson", "elderberry"};

typedef struct Hello {
  RolltuiContext* ctx;        /* key bindings, menus and content kinds */
  RolltuiWindows* windows;    /* a widget for each window, created from its content */
  RolltuiWindowStack* stack;  /* the layout's layers, focus, and event routing */
  RolltuiLayout* layout;
  RolltuiComposeScratch* compose;
  RolltuiEffectMap* effects;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]; /* the theme, one style per role */
  int w, h;
  unsigned long long now_ms;
} Hello;

/* The list's rows, requested as they're drawn. */
static size_t fruit_count(void* ctx) {
  (void)ctx;
  return sizeof kFruit / sizeof *kFruit;
}
static void fruit_row(void* ctx, size_t i, RolltuiListRow* out) {
  (void)ctx;
  rolltui_str_set(&out->key, kFruit[i], strlen(kFruit[i]));
  rolltui_list_row_cell(out, kFruit[i], strlen(kFruit[i]));
}

static void draw_window(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Hello* h = (Hello*)ctx;
  rolltui_windows_draw(h->windows, rn, f, h->styles, rolltui_windows_default_roles());
}

/* Draw one frame: lay out and draw the windows, then any messages on top. Returns how long to wait before the next frame. */
static int render(void* ctx, RolltuiRun* run, RolltuiFrame* f, int w, int h_, unsigned long long now_ms) {
  Hello* h = (Hello*)ctx;
  const RolltuiRect screen = {0, 0, w, h_};
  (void)run;
  h->w = w, h->h = h_, h->now_ms = now_ms;
  rolltui_windows_sync(h->windows, h->stack);
  rolltui_windows_layout(h->windows, h->stack, screen);
  rolltui_window_stack_compose(h->stack, f, screen, h->styles, rolltui_layout_default_roles(), draw_window, h, 0, h->compose);
  return rolltui_windows_draw_toasts(h->windows, f, screen, h->styles, now_ms);
}

static void ground(void* ctx, RolltuiRun* run, RolltuiStyle* out) {
  (void)run;
  *out = ((Hello*)ctx)->styles[ROLLTUI_ROLE_BACKGROUND];
}

/* Handle one event: route it to its window, quit on a q that no window used, then read the list's events. */
static void event(void* ctx, RolltuiRun* run, const RolltuiEvent* e) {
  Hello* h = (Hello*)ctx;
  const RolltuiRect screen = {0, 0, h->w, h->h};
  RolltuiStr window;
  RolltuiListEvent le;
  int taken = 0;
  memset(&window, 0, sizeof window);
  if (rolltui_window_stack_route(h->stack, e, screen, rolltui_bindings_default(h->ctx), rolltui_stack_default_actions(), &window) ==
      ROLLTUI_ROUTE_DELIVER)
    taken = rolltui_windows_handle(h->windows, window.p, window.n, e);
  rolltui_str_free(&window);
  if (!taken && e->kind == ROLLTUI_EVENT_KEY && e->key.key == ROLLTUI_KEY_CHAR && e->key.ch == 'q') rolltui_run_stop(run);
  memset(&le, 0, sizeof le);
  while (rolltui_windows_list_event(h->windows, "fruit", 5, &le))
    if (le.kind == ROLLTUI_LIST_EVENT_ACTIVATE) {
      char said[64];
      const int n = snprintf(said, sizeof said, "You chose %.*s.", (int)le.key.n, le.key.p);
      rolltui_windows_post(h->windows, ROLLTUI_MESSAGE_INFO, said, (size_t)n, h->now_ms);
    }
  rolltui_list_event_release(&le);
}

static void hello_init(Hello* h) {
  RolltuiLayoutReport rep;
  size_t n = 0;
  memset(h, 0, sizeof *h);
  h->w = 80, h->h = 24;
  h->ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(h->ctx);
  h->effects = rolltui_theme_builtin_fill("default-dark", 12, h->styles, ROLLTUI_ROLE_COUNT);
  h->windows = rolltui_windows_new(h->ctx);
  h->stack = rolltui_window_stack_new();
  h->compose = rolltui_compose_scratch_new();
  memset(&rep, 0, sizeof rep);
  h->layout = rolltui_load_layout_text(kLayout, sizeof kLayout - 1, rolltui_layout_shipped_default_actions(h->ctx, &n), n,
                                       rolltui_layout_default_hooks(), &rep);
  if (rep.error.n) fprintf(stderr, "hello: %s\n", rep.error.p); /* the report lists any problems in the layout */
  rolltui_layout_report_release(&rep);
  rolltui_window_stack_set_base(h->stack, rolltui_layout_base(h->layout));
  rolltui_windows_bind_list(h->windows, "fruit", 5, fruit_count, fruit_row, h, NULL);
}

static void hello_free(Hello* h) {
  rolltui_windows_free(h->windows);
  rolltui_window_stack_free(h->stack);
  rolltui_layout_free(h->layout);
  rolltui_compose_scratch_free(h->compose);
  rolltui_effect_map_free(h->effects);
  rolltui_context_free(h->ctx);
}

static RolltuiRunApp hello_app(Hello* h) {
  RolltuiRunApp app;
  memset(&app, 0, sizeof app);
  app.ctx = h;
  app.render = render;
  app.event = event;
  app.ground = ground;
  return app;
}

#ifndef HELLO_NO_MAIN
int main(void) {
  Hello h;
  RolltuiRunApp app;
  RolltuiTerminalOptions o;
  int rc;
  hello_init(&h);
  app = hello_app(&h);
  memset(&o, 0, sizeof o);
  o.alt_screen = 1;
  o.mouse = 1;
  o.hover = 1;
  o.bracketed_paste = 1;
  o.hide_cursor = 1;
  o.handle_signals = 1;
  rc = rolltui_run(0, 1, o, &app);
  hello_free(&h);
  return rc == ROLLTUI_RUN_STOPPED ? 0 : 1;
}
#endif
