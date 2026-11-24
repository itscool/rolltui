/* tools/bench/rolltui_bench.c — THE SCREENS tools/bench/ratatui DRAWS, drawn the way a rolltui program draws them, timed, and the
 * bytes a terminal would be sent counted.
 *
 *   rolltui_bench W H SCENARIO FRAMES
 *
 * move    a 10,000-row table of three columns (a `list:` window in a layout, composed into the double buffer), its cursor walking
 *         down and back inside the first screenful: two rows change a frame.
 * scroll  the same table, its cursor walking on past the screen: every row moves a frame.
 * tick    a page of text that does not change, written into the frame directly, and a counter in its corner that does.
 *
 * Each frame is the whole of what a program does for one: input handled, laid out, drawn, diffed and encoded. The first frame is
 * not timed (it is the full paint). Prints microseconds and bytes a frame. tools/bench/run.sh runs both benches side by side. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rolltui/rolltui.h"

static double now_us(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

#define N 10000
static char names[N][24], sizes[N][8], dates[N][24];

static size_t count_fn(void* c) {
  (void)c;
  return N;
}
static void row_fn(void* c, size_t i, RolltuiListRow* out) {
  char key[16];
  const int kn = snprintf(key, sizeof key, "%zu", i);
  (void)c;
  rolltui_str_set(&out->key, key, (size_t)kn);
  rolltui_list_row_cell(out, names[i], strlen(names[i]));
  rolltui_list_row_cell(out, sizes[i], strlen(sizes[i]));
  rolltui_list_row_cell(out, dates[i], strlen(dates[i]));
}

typedef struct Slot {
  RolltuiWindows* w;
  RolltuiStyle* styles;
} Slot;
static void draw_window(void* c, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Slot* s = (Slot*)c;
  rolltui_windows_draw(s->w, rn, f, s->styles, rolltui_windows_default_roles());
}

static RolltuiStyleColorRaw rgb(int r, int g, int b) {
  RolltuiStyleColorRaw c;
  memset(&c, 0, sizeof c);
  c.kind = 2;
  c.r = (unsigned char)r, c.g = (unsigned char)g, c.b = (unsigned char)b;
  return c;
}

int main(int argc, char** argv) {
  const int w = atoi(argv[1]), h = atoi(argv[2]);
  const char* scenario = argv[3];
  const int frames = atoi(argv[4]);
  const int body = h - 1;
  RolltuiContext* ctx = rolltui_context_new();
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT];
  RolltuiEffectMap* fx;
  RolltuiSwap* swap;
  RolltuiStr out = {0};
  size_t total = 0;
  double t0 = 0;
  int f, i;
  memset(styles, 0, sizeof styles);
  rolltui_context_set_library_defaults(ctx);
  fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
  for (i = 0; i < N; ++i) {
    snprintf(names[i], sizeof names[i], "file%d.txt", i);
    snprintf(sizes[i], sizeof sizes[i], "%dK", (i * 37) % 1000);
    snprintf(dates[i], sizeof dates[i], "2026-11-%02d 12:%02d", i % 28 + 1, i % 60);
  }
  swap = rolltui_swap_new(w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
  if (strcmp(scenario, "tick") == 0) {
    RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
    RolltuiStyle text, accent, counter;
    memset(&text, 0, sizeof text);
    text.fg = rgb(220, 220, 220), text.bg = rgb(24, 24, 28);
    accent = text;
    accent.fg = rgb(130, 170, 255);
    counter = text;
    counter.fg = rgb(255, 200, 80), counter.bold = 1;
    for (f = 0; f <= frames; ++f) {
      RolltuiFrame* fr;
      char line[256];
      size_t n = 0;
      if (f == 1) t0 = now_us();
      fr = rolltui_swap_begin(swap, w, h, text);
      for (i = 0; i < h; ++i) {
        const int ln = snprintf(line, sizeof line, "%5d  the quick brown fox jumps over the lazy dog, line %d of the page that does not move", i, i);
        rolltui_frame_put_text(fr, ds, 0, i, line, (size_t)ln, i % 3 == 0 ? accent : text, w, 0, 0);
      }
      {
        const int cn = snprintf(line, sizeof line, "%8d", f);
        rolltui_frame_put_text(fr, ds, w - 8, 0, line, (size_t)cn, counter, 8, 0, 0);
      }
      rolltui_str_clear(&out);
      rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &out);
      n = out.n;
      if (f > 0) total += n;
    }
    rolltui_draw_scratch_free(ds);
  } else {
    static const char kScreen[] = "{ \"name\": \"b\", \"resizable\": false, \"root\": { \"column\": [ { \"id\": \"rows\", \"content\": \"list:files\" } ] } }";
    RolltuiLayoutReport rep;
    size_t nd = 0;
    const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &nd);
    RolltuiLayout* layout;
    RolltuiWindows* win = rolltui_windows_new(ctx);
    RolltuiWindowStack* stack = rolltui_window_stack_new();
    RolltuiComposeScratch* cs = rolltui_compose_scratch_new();
    RolltuiListColumn cols[3];
    RolltuiEvent down, up;
    const RolltuiRect area = {0, 0, w, h};
    Slot slot;
    const int scroll = strcmp(scenario, "scroll") == 0;
    memset(&rep, 0, sizeof rep);
    layout = rolltui_load_layout_text(kScreen, sizeof kScreen - 1, defaults, nd, rolltui_layout_default_hooks(), &rep);
    rolltui_layout_report_release(&rep);
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    rolltui_windows_bind_list(win, "files", 5, count_fn, row_fn, NULL, NULL);
    memset(cols, 0, sizeof cols);
    rolltui_str_set(&cols[0].title, "Name", 4);
    rolltui_str_set(&cols[1].title, "Size", 4);
    cols[1].width = 6;
    cols[1].align = ROLLTUI_LIST_ALIGN_RIGHT;
    rolltui_str_set(&cols[2].title, "Modified", 8);
    cols[2].width = 16;
    rolltui_windows_set_list_columns(win, "files", 5, cols, 3);
    for (i = 0; i < 3; ++i) rolltui_str_free(&cols[i].title);
    memset(&down, 0, sizeof down);
    down.kind = ROLLTUI_EVENT_KEY;
    down.key.key = ROLLTUI_KEY_DOWN;
    up = down;
    up.key.key = ROLLTUI_KEY_UP;
    slot.w = win;
    slot.styles = styles;
    for (f = 0; f <= frames; ++f) {
      RolltuiWidgetEnv env;
      RolltuiFrame* fr;
      size_t n = 0;
      if (f == 1) t0 = now_us();
      if (f > 0) {
        if (scroll) rolltui_windows_handle(win, "rows", 4, &down);
        else {
          const int p = (f - 1) % (2 * (body - 1));
          rolltui_windows_handle(win, "rows", 4, p < body - 1 ? &down : &up);
        }
      }
      memset(&env, 0, sizeof env);
      env.now_ms = 1000;
      env.still = 1;
      rolltui_context_set_env(ctx, &env);
      rolltui_windows_sync(win, stack);
      rolltui_windows_layout(win, stack, area);
      fr = rolltui_swap_begin(swap, w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
      rolltui_window_stack_compose(stack, fr, area, styles, rolltui_layout_default_roles(), draw_window, &slot, 0, cs);
      rolltui_str_clear(&out);
      rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &out);
      n = out.n;
      if (f > 0) total += n;
      if (f == 0 && scroll) /* the cursor starts at the bottom row, so every frame after scrolls */
        for (i = 0; i < body - 1; ++i) rolltui_windows_handle(win, "rows", 4, &down);
    }
    rolltui_compose_scratch_free(cs);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(win);
    rolltui_layout_free(layout);
  }
  printf("rolltui %s %dx%d: %.1f us/frame, %.0f bytes/frame\n", scenario, w, h, (now_us() - t0) / frames, (double)total / frames);
  rolltui_str_free(&out);
  rolltui_swap_free(swap);
  rolltui_effect_map_free(fx);
  rolltui_context_free(ctx);
  return 0;
}
