/*
 * rolltui/examples/tour.c — `rolltui-tour`: the library in one screen. A table of processes that sorts, filters and marks; a
 * dashboard of live charts beside it; a page of Markdown with code and a diagram; every shipped theme, light and dark; the default
 * keys or vim's. Every window is one of the library's own kinds, so this file is the data and the few lines that wire it.
 *
 * It is also the web demo. Built for wasm32-wasi (tools/web/build.sh), the same file runs in a page behind xterm.js: the page
 * hands it keys and sizes and drives the loop a step at a time (`rolltui_run_begin`, `_step`, `_finish`), since a page may never
 * block. The processes are made up, so the numbers move the same way on a terminal and in a browser.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/rolltui.h"

#define LEN(s) (sizeof(s) - 1)
#define PROCS 72
#define HISTORY 160
#define TICK_MS 500

/* ---- the screen ------------------------------------------------------------------------------ */

static const char kLayout[] =
    "{ \"name\": \"tour\", \"min_width\": 48, \"min_height\": 12, \"focus\": \"main\",\n"
    "  \"actions\": { \"app.help\": \"every key\", \"app.menu\": \"settings: the theme, light or dark, the keys\",\n"
    "               \"app.theme\": \"edit the theme\", \"app.keys\": \"edit the keys\", \"app.quit\": \"quit\" },\n"
    "  \"popups\": [\n"
    "    { \"id\": \"menu\", \"dismiss\": true, \"x\": \"50%\", \"y\": \"50%\", \"w\": 48, \"h\": 8, \"anchor\": \"center\",\n"
    "      \"modal\": true,\n"
    "      \"root\": { \"id\": \"menu\", \"content\": \"menu:settings\", \"border\": \"rounded\", \"title\": \"settings\",\n"
    "                \"focusable\": true, \"background\": \"panel_background\" } },\n"
    "    { \"id\": \"help\", \"dismiss\": true, \"x\": \"100%\", \"y\": 0, \"w\": \"50%\", \"h\": \"100%\", \"anchor\": \"top-right\",\n"
    "      \"min_w\": 40, \"modal\": true,\n"
    "      \"root\": { \"id\": \"help\", \"content\": \"help\", \"border\": \"rounded\", \"title\": \"keys\", \"focusable\": true,\n"
    "                \"background\": \"panel_background\" } },\n"
    "    { \"id\": \"theme\", \"dismiss\": false, \"x\": \"100%\", \"y\": 0, \"w\": \"50%\", \"h\": \"100%\", \"anchor\": \"top-right\",\n"
    "      \"min_w\": 40, \"modal\": true,\n"
    "      \"root\": { \"id\": \"theme\", \"content\": \"theme\", \"border\": \"rounded\", \"focusable\": true,\n"
    "                \"background\": \"panel_background\" } },\n"
    "    { \"id\": \"keys\", \"dismiss\": false, \"x\": \"100%\", \"y\": 0, \"w\": \"50%\", \"h\": \"100%\", \"anchor\": \"top-right\",\n"
    "      \"min_w\": 40, \"modal\": true,\n"
    "      \"root\": { \"id\": \"keys\", \"content\": \"keys\", \"border\": \"rounded\", \"focusable\": true,\n"
    "                \"background\": \"panel_background\" } } ],\n"
    "  \"root\": { \"row\": [\n"
    "    { \"id\": \"main\", \"focusable\": true, \"border\": \"rounded\",\n"
    "      \"tabs\": [ { \"title\": \"Processes\", \"content\": \"list:procs\" },\n"
    "                { \"title\": \"Read me\", \"content\": \"transcript:readme\" } ] },\n"
    "    { \"id\": \"board\", \"size\": \"42%\", \"breakpoint\": { \"width\": 96 }, \"column\": [\n"
    "      { \"id\": \"cpu\", \"content\": \"chart:cpu\", \"border\": \"rounded\", \"title\": \"cpu\" },\n"
    "      { \"id\": \"disk\", \"content\": \"chart:disk\", \"border\": \"rounded\", \"title\": \"disk\", \"size\": \"30%\" },\n"
    "      { \"id\": \"net\", \"content\": \"chart:net\", \"border\": \"rounded\", \"title\": \"network\", \"size\": 5 } ] } ] } }\n";

/* This program's own chords, for its own actions: the shipped table is every host's. */
static const char kOwnBindings[] = "{ \"bindings\": { \"app.quit\": [\"ctrl+q\"] } }";

static const char kReadme[] =
    "# rolltui\n"
    "\n"
    "A terminal UI library in C. What you are looking at is **one program**, `rolltui-tour`, built from the library's own "
    "window kinds: the table, the charts, this page, the menu and the help are each a line in a layout file.\n"
    "\n"
    "## Try\n"
    "\n"
    "- **F2**: the settings. Every shipped theme, light and dark, and vim's keys.\n"
    "- **F4**: the theme editor; **F5**: the keys editor.\n"
    "- **F1**: every key, as the bindings say.\n"
    "- **Tab**: the next window. A click on a tab's title shows it.\n"
    "- In the table: `/` filters (fuzzy), **Alt-S** sorts by the next column and **Alt-R** reverses it, Space marks, Enter opens "
    "a row.\n"
    "\n"
    "## What the library does for a program\n"
    "\n"
    "| part | what it does |\n"
    "|---|---|\n"
    "| frames | a diff of cells, in the fewest bytes |\n"
    "| terminals | asked what they can do, and remembered |\n"
    "| text | Unicode measured as the terminal draws it |\n"
    "| themes | checked for contrast, light and dark |\n"
    "\n"
    "## A window, in C\n"
    "\n"
    "```c\n"
    "static size_t count(void* ctx) { return 72; }\n"
    "static void row(void* ctx, size_t i, RolltuiListRow* out) {\n"
    "  rolltui_list_row_cell(out, names[i], strlen(names[i]));\n"
    "}\n"
    "rolltui_windows_bind_list(windows, \"procs\", 5, count, row, NULL, NULL);\n"
    "```\n"
    "\n"
    "## How a frame gets out\n"
    "\n"
    "```mermaid\n"
    "flowchart LR\n"
    "  A[layout] --> B[windows]\n"
    "  B --> C[cells]\n"
    "  C --> D[diff]\n"
    "  D --> E[terminal]\n"
    "```\n"
    "\n"
    "~~Configuration flags~~: presets, a settings menu and an editor app instead.\n";

/* ---- the made-up machine -------------------------------------------------------------------- */

typedef struct Proc {
  char name[32];
  const char* user;
  int pid;
  double cpu, mem_mb, weight;
  int running;
} Proc;

typedef struct Tour {
  RolltuiContext* ctx;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT];
  RolltuiEffectMap* effects;
  RolltuiEffectScratch* fx;
  RolltuiDrawScratch* draw;
  RolltuiBindings* bindings;
  RolltuiWindows* windows;
  RolltuiWindowStack* stack;
  RolltuiComposeScratch* compose;
  RolltuiLayout* layout;
  RolltuiThemeStore* themes;   /* what the theme editor edits and the settings choose from: nothing is written */
  RolltuiBindingsStore* keys;  /* …and the keys' */
  unsigned long long themes_seen, keys_seen;
  RolltuiStr settings_menu;
  RolltuiRows status;
  Proc procs[PROCS];
  double cpu_user[HISTORY], cpu_sys[HISTORY], rx[HISTORY], tx[HISTORY], reads[16], writes[16];
  size_t hist_n, disk_n;
  unsigned long long seed, now_ms, busy_since;
  int w, h, toast_wait;
} Tour;

static double rnd(Tour* t) { /* xorshift: the same machine every run */
  t->seed ^= t->seed << 13;
  t->seed ^= t->seed >> 7;
  t->seed ^= t->seed << 17;
  return (double)(t->seed % 1000003ULL) / 1000003.0;
}

/* The four histories move together: the oldest value goes when a new one arrives. */
static void push_history(Tour* t, double user, double sys, double rx, double tx) {
  if (t->hist_n == HISTORY) {
    memmove(t->cpu_user, t->cpu_user + 1, (HISTORY - 1) * sizeof(double));
    memmove(t->cpu_sys, t->cpu_sys + 1, (HISTORY - 1) * sizeof(double));
    memmove(t->rx, t->rx + 1, (HISTORY - 1) * sizeof(double));
    memmove(t->tx, t->tx + 1, (HISTORY - 1) * sizeof(double));
    --t->hist_n;
  }
  t->cpu_user[t->hist_n] = user;
  t->cpu_sys[t->hist_n] = sys;
  t->rx[t->hist_n] = rx;
  t->tx[t->hist_n] = tx;
  ++t->hist_n;
}

static void push_disk(Tour* t, double reads, double writes) {
  if (t->disk_n == 16) {
    memmove(t->reads, t->reads + 1, 15 * sizeof(double));
    memmove(t->writes, t->writes + 1, 15 * sizeof(double));
    --t->disk_n;
  }
  t->reads[t->disk_n] = reads;
  t->writes[t->disk_n] = writes;
  ++t->disk_n;
}

static void make_procs(Tour* t) {
  static const char* const kNamed[] = {"kernel_task", "launchd", "postgres: writer", "postgres: walwriter", "redis-server",
                                       "node server.js", "python3 train.py", "zsh", "rolltui-tour", "sshd", "cron", "syslogd"};
  static const char* const kUsers[] = {"root", "www", "postgres", "you", "daemon"};
  size_t i, k = 0;
  for (i = 0; i < sizeof kNamed / sizeof kNamed[0]; ++i, ++k) snprintf(t->procs[k].name, sizeof t->procs[k].name, "%s", kNamed[i]);
  for (i = 1; k < PROCS && i <= 12; ++i, ++k) snprintf(t->procs[k].name, sizeof t->procs[k].name, "nginx: worker %zu", i);
  for (i = 1; k < PROCS && i <= 24; ++i, ++k) snprintf(t->procs[k].name, sizeof t->procs[k].name, "worker-%zu", i);
  for (i = 1; k < PROCS; ++i, ++k) snprintf(t->procs[k].name, sizeof t->procs[k].name, "chrome-helper %zu", i);
  for (k = 0; k < PROCS; ++k) {
    Proc* p = &t->procs[k];
    p->user = kUsers[(size_t)(rnd(t) * 5)];
    p->pid = 100 + (int)(rnd(t) * 60000);
    p->weight = pow(rnd(t), 3) * 80;
    p->mem_mb = 4 + pow(rnd(t), 2) * 2400;
  }
}

/* Half a second of the machine: every process's load drifts, a few start and stop, and the charts take the totals. */
static void advance(Tour* t) {
  double user = 0, sys = 0;
  size_t i;
  const double wave = sin((double)t->now_ms / 4000.0);
  for (i = 0; i < PROCS; ++i) {
    Proc* p = &t->procs[i];
    p->cpu = p->weight * (0.6 + 0.4 * rnd(t)) * (1 + 0.5 * wave * (i % 3 == 0));
    if (p->cpu > 100) p->cpu = 100;
    p->running = p->cpu > 20;
    p->mem_mb += (rnd(t) - 0.48) * 8;
    if (p->mem_mb < 2) p->mem_mb = 2;
    if (i % 4 == 0) sys += p->cpu / 24;
    else user += p->cpu / 24;
  }
  push_history(t, user > 100 ? 100 : user, sys > 100 ? 100 : sys, 40 + 30 * sin((double)t->now_ms / 1700.0) + 25 * rnd(t),
               15 + 10 * rnd(t));
  push_disk(t, 20 + 60 * rnd(t), 10 + 40 * rnd(t) * (1 + wave));
  rolltui_windows_list_refresh(t->windows, "procs", 5);
}

/* ---- the windows' data ------------------------------------------------------------------------ */

static size_t procs_count(void* ctx) {
  (void)ctx;
  return PROCS;
}

static void procs_row(void* ctx, size_t i, RolltuiListRow* out) {
  Tour* t = (Tour*)ctx;
  const Proc* p = &t->procs[i];
  char b[32];
  int n;
  n = snprintf(b, sizeof b, "%d", p->pid);
  rolltui_str_set(&out->key, b, (size_t)n);
  rolltui_list_row_cell(out, p->name, strlen(p->name));
  rolltui_list_row_cell(out, b, (size_t)n);
  rolltui_list_row_cell(out, p->user, strlen(p->user));
  n = snprintf(b, sizeof b, "%.1f", p->cpu);
  rolltui_list_row_cell(out, b, (size_t)n);
  n = p->mem_mb >= 1024 ? snprintf(b, sizeof b, "%.1fG", p->mem_mb / 1024) : snprintf(b, sizeof b, "%.0fM", p->mem_mb);
  rolltui_list_row_cell(out, b, (size_t)n);
  rolltui_list_row_cell(out, p->running ? "running" : "sleeping", p->running ? 7 : 8);
  out->role = p->cpu >= 60 ? ROLLTUI_ROLE_WARNING : 0;
  out->busy = (unsigned char)p->running;
  out->busy_since_ms = t->busy_since;
}

static void cpu_chart(void* ctx, RolltuiChart* c) {
  Tour* t = (Tour*)ctx;
  rolltui_chart_set_kind(c, ROLLTUI_CHART_LINE);
  rolltui_chart_set_series(c, 0, "user", 4, 0, t->cpu_user, t->hist_n);
  rolltui_chart_set_series(c, 1, "system", 6, 0, t->cpu_sys, t->hist_n);
  rolltui_chart_set_range(c, 0, 100);
}

static void disk_chart(void* ctx, RolltuiChart* c) {
  Tour* t = (Tour*)ctx;
  rolltui_chart_set_kind(c, ROLLTUI_CHART_BARS);
  rolltui_chart_set_series(c, 0, "read", 4, 0, t->reads, t->disk_n);
  rolltui_chart_set_series(c, 1, "write", 5, 0, t->writes, t->disk_n);
}

static void net_chart(void* ctx, RolltuiChart* c) {
  Tour* t = (Tour*)ctx;
  rolltui_chart_set_kind(c, ROLLTUI_CHART_SPARK);
  rolltui_chart_set_series(c, 0, "in", 2, 0, t->rx, t->hist_n);
  rolltui_chart_set_series(c, 1, "out", 3, 0, t->tx, t->hist_n);
}

/* ---- settings: themes, light or dark, keys ----------------------------------------------------- */

/* The theme store's working copy, drawn: an edit in the theme editor shows at once, and a frame with nothing new costs nothing. */
static void sync_theme(Tour* t) {
  const RolltuiThemePresetValue* v;
  RolltuiThemeReport rep;
  RolltuiStyle got[ROLLTUI_ROLE_COUNT];
  RolltuiStr name;
  RolltuiEffectMap* fx;
  int mode;
  const unsigned long long seen = rolltui_theme_store_version(t->themes);
  if (seen == t->themes_seen && t->effects) return;
  t->themes_seen = seen;
  v = rolltui_theme_store_working(t->themes);
  if (!v) return;
  memset(&rep, 0, sizeof rep);
  memset(&name, 0, sizeof name);
  mode = rolltui_theme_mode_from_name(v->mode.p ? v->mode.p : "", v->mode.n);
  fx = rolltui_theme_load(v->colours, mode >= 0 ? mode : ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), got, &name, &rep);
  if (fx) {
    RolltuiScrollbarGlyphs g;
    memcpy(t->styles, got, sizeof got);
    rolltui_effect_map_free(t->effects);
    t->effects = fx;
    rolltui_theme_scrollbar_glyphs(v->colours, &g);
    rolltui_context_set_scrollbar_glyphs(t->ctx, &g);
  }
  rolltui_str_free(&name);
  rolltui_theme_report_release(&rep);
}

/* The keys store's working table, with this program's own chord, declared for this screen. */
static void sync_keys(Tour* t) {
  RolltuiBindingsReport rep;
  size_t n = 0;
  const RolltuiLayoutAction* declared;
  const unsigned long long seen = rolltui_bindings_store_version(t->keys);
  if (seen == t->keys_seen && t->bindings) return;
  t->keys_seen = seen;
  rolltui_bindings_free(t->bindings);
  t->bindings = rolltui_bindings_clone(rolltui_bindings_store_working(t->keys));
  memset(&rep, 0, sizeof rep);
  rolltui_bindings_load_json(t->bindings, kOwnBindings, LEN(kOwnBindings), ROLLTUI_PROTOCOL_LEGACY, rolltui_bindings_library_scope,
                             NULL, NULL, NULL, &rep);
  rolltui_bindings_report_release(&rep);
  declared = rolltui_layout_actions(t->layout, &n);
  rolltui_bindings_declare(t->bindings, declared, n, NULL, 0);
  rolltui_context_set_bindings(t->ctx, t->bindings); /* the windows read the table they were given: the freed one, until this */
}

/* The settings menu, made from the shipped themes so a new one is offered with no change here. */
static void make_settings_menu(Tour* t) {
  size_t i;
  RolltuiStr* m = &t->settings_menu;
  rolltui_str_set(m, "{ \"id\": \"root\", \"label\": \"settings\", \"items\": [\n"
                     "  { \"id\": \"theme\", \"label\": \"Theme\", \"kind\": \"choice\", \"value\": \"default\", \"items\": [",
                  LEN("{ \"id\": \"root\", \"label\": \"settings\", \"items\": [\n"
                      "  { \"id\": \"theme\", \"label\": \"Theme\", \"kind\": \"choice\", \"value\": \"default\", \"items\": ["));
  for (i = 0; i < rolltui_kThemePresetCount; ++i) {
    char b[160];
    const char* name = rolltui_kThemePresets[i].name;
    const int n = snprintf(b, sizeof b, "%s{ \"id\": \"%s\", \"label\": \"%s\" }", i ? ", " : "", name, name);
    rolltui_str_append(m, b, (size_t)n);
  }
  {
    static const char kRest[] =
        "] },\n"
        "  { \"id\": \"mode\", \"label\": \"Light or dark\", \"kind\": \"choice\", \"value\": \"dark\",\n"
        "    \"items\": [ { \"id\": \"dark\", \"label\": \"dark\" }, { \"id\": \"light\", \"label\": \"light\" } ] },\n"
        "  { \"id\": \"keys\", \"label\": \"Keys\", \"kind\": \"choice\", \"value\": \"default\",\n"
        "    \"items\": [ { \"id\": \"default\", \"label\": \"default\" }, { \"id\": \"vim\", \"label\": \"vim\" } ] },\n"
        "  { \"id\": \"edit_theme\", \"label\": \"Edit the theme\\u2026\" },\n"
        "  { \"id\": \"edit_keys\", \"label\": \"Edit the keys\\u2026\" } ] }\n";
    rolltui_str_append(m, kRest, LEN(kRest));
  }
}

static void say(Tour* t, const char* text) { rolltui_windows_post(t->windows, ROLLTUI_MESSAGE_INFO, text, strlen(text), t->now_ms); }

static void chose(Tour* t, const RolltuiMenuEvent* ev) {
  const char* id = ev->id.p ? ev->id.p : "";
  const char* v = ev->value.p ? ev->value.p : "";
  RolltuiStr label;
  if (strcmp(id, "theme") == 0) {
    RolltuiThemePresetReport rep;
    memset(&rep, 0, sizeof rep);
    rolltui_theme_store_load(t->themes, v, ev->value.n, &rep, /*persist=*/0);
    rolltui_theme_preset_report_release(&rep);
  } else if (strcmp(id, "mode") == 0) {
    rolltui_theme_store_set_mode(t->themes, v, ev->value.n, /*persist=*/0);
  } else if (strcmp(id, "keys") == 0) {
    RolltuiBindingsPresetReport rep;
    memset(&rep, 0, sizeof rep);
    rolltui_bindings_store_load(t->keys, v, ev->value.n, &rep, /*persist=*/0);
    rolltui_bindings_preset_report_release(&rep);
    say(t, strcmp(v, "vim") == 0 ? "vim's keys: j and k, g g, G, 5 j, / to filter" : "the default keys");
    return;
  } else {
    return;
  }
  memset(&label, 0, sizeof label);
  rolltui_theme_store_label(t->themes, &label);
  say(t, label.p ? label.p : "");
  rolltui_str_free(&label);
}

/* An item that opens an editor: the menu closes, and the editor's popup opens in its place. */
static void activated(Tour* t, const RolltuiMenuEvent* ev) {
  const char* id = ev->id.p ? ev->id.p : "";
  const char* action = strcmp(id, "edit_theme") == 0 ? "app.theme" : strcmp(id, "edit_keys") == 0 ? "app.keys" : NULL;
  if (!action) return;
  while (rolltui_window_stack_has_popup(t->stack, "menu", 4)) rolltui_window_stack_pop(t->stack);
  if (rolltui_window_stack_action_popup(t->stack, t->layout, action, strlen(action))) rolltui_windows_sync(t->windows, t->stack);
}

/* ---- the loop's hooks ------------------------------------------------------------------------- */

static RolltuiRect screen_area(const Tour* t) {
  RolltuiRect r;
  r.x = 0, r.y = 0, r.w = t->w, r.h = t->h > 1 ? t->h - 1 : 0;
  return r;
}

static void tick(void* ctx, RolltuiRun* run) {
  Tour* t = (Tour*)ctx;
  (void)run;
  advance(t);
}

static void on_start(void* ctx, RolltuiRun* run) {
  Tour* t = (Tour*)ctx;
  rolltui_run_every(run, TICK_MS, tick, t);
}

static void ground(void* ctx, RolltuiRun* run, RolltuiStyle* out) {
  (void)run;
  *out = ((Tour*)ctx)->styles[ROLLTUI_ROLE_BACKGROUND];
}

static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Tour* t = (Tour*)ctx;
  rolltui_windows_draw(t->windows, rn, f, t->styles, rolltui_windows_default_roles());
}

static void status_line(Tour* t, RolltuiFrame* f) {
  const RolltuiRect line = {0, t->h - 1, t->w, 1};
  RolltuiStr theme, keys;
  memset(&theme, 0, sizeof theme);
  memset(&keys, 0, sizeof keys);
  rolltui_theme_store_label(t->themes, &theme);
  rolltui_bindings_store_label(t->keys, &keys);
  rolltui_frame_fill(f, t->draw, line, t->styles[ROLLTUI_ROLE_PANEL_BACKGROUND], NULL, 0);
  rolltui_rows_reset(&t->status);
  rolltui_rows_add(&t->status, "F1", 2, "keys", 4);
  rolltui_rows_add(&t->status, "F2", 2, "settings", 8);
  rolltui_rows_add(&t->status, "Tab", 3, "next window", 11);
  rolltui_rows_add(&t->status, "theme", 5, theme.p ? theme.p : "", theme.n);
  rolltui_rows_add(&t->status, "keys", 4, keys.p ? keys.p : "", keys.n);
  rolltui_str_free(&theme);
  rolltui_str_free(&keys);
  rolltui_frame_put_fields(f, t->draw, 1, t->h - 1, &t->status, t->styles[ROLLTUI_ROLE_LABEL], t->styles[ROLLTUI_ROLE_VALUE], t->w - 1,
                           0);
}

static int render(void* ctx, RolltuiRun* run, RolltuiFrame* f, int w, int h, unsigned long long now_ms) {
  Tour* t = (Tour*)ctx;
  RolltuiWidgetEnv env;
  int wait = -1;
  (void)run;
  t->w = w, t->h = h, t->now_ms = now_ms;
  if (t->busy_since == 0) t->busy_since = now_ms;
  sync_theme(t);
  sync_keys(t);
  memset(&env, 0, sizeof env);
  env.now_ms = now_ms;
  rolltui_context_set_env(t->ctx, &env);
  rolltui_context_set_bindings(t->ctx, t->bindings);
  rolltui_windows_sync(t->windows, t->stack);
  rolltui_windows_autosize(t->windows, t->stack, screen_area(t));
  rolltui_windows_layout(t->windows, t->stack, screen_area(t));
  rolltui_window_stack_compose(t->stack, f, screen_area(t), t->styles, rolltui_layout_default_roles(), draw_slot, t, 0, t->compose);
  if (h > 1) status_line(t, f);
  t->toast_wait = rolltui_windows_draw_toasts(t->windows, f, screen_area(t), t->styles, now_ms);
  if (rolltui_frame_mark_count(f) && t->effects && !rolltui_effect_map_empty(t->effects)) {
    RolltuiEffectReport rep;
    const int fx_ms = rolltui_effects_tick_ms(t->ctx, f, t->effects);
    memset(&rep, 0, sizeof rep);
    rolltui_effects_apply(t->ctx, f, t->fx, t->styles, NULL, t->effects, now_ms, 0, &rep, NULL, NULL);
    if (fx_ms > 0) wait = fx_ms;
  }
  if (t->toast_wait >= 0 && (wait < 0 || t->toast_wait < wait)) wait = t->toast_wait;
  return wait; /* the tick's timer wakes it in any case */
}

static void on_menu(Tour* t, RolltuiMenu* m, const RolltuiEvent* e) {
  RolltuiMenuEvent ev;
  memset(&ev, 0, sizeof ev);
  rolltui_menu_handle(m, e, t->bindings, rolltui_menu_default_actions(), &ev);
  if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && ev.value.n) chose(t, &ev);
  if (ev.kind == ROLLTUI_MENU_EVENT_ACTIVATE) activated(t, &ev);
  rolltui_menu_event_release(&ev);
}

static void on_list(Tour* t) {
  RolltuiListEvent le;
  memset(&le, 0, sizeof le);
  while (rolltui_windows_list_event(t->windows, "procs", 5, &le)) {
    if (le.kind == ROLLTUI_LIST_EVENT_ACTIVATE) {
      size_t i;
      for (i = 0; i < PROCS; ++i) {
        char pid[16], b[96];
        snprintf(pid, sizeof pid, "%d", t->procs[i].pid);
        if (le.key.p && strcmp(pid, le.key.p) == 0) {
          snprintf(b, sizeof b, "%s: pid %d, %.1f%% cpu, %s", t->procs[i].name, t->procs[i].pid, t->procs[i].cpu, t->procs[i].user);
          say(t, b);
        }
      }
    }
  }
  rolltui_list_event_release(&le);
}

static void on_event(void* ctx, RolltuiRun* run, const RolltuiEvent* e) {
  Tour* t = (Tour*)ctx;
  RolltuiStr window;
  unsigned char kind;
  if (rolltui_windows_toast_event(t->windows, e)) return;
  /* The program's own keys, for a key the window with focus does not take itself: the library asks it first, so what is typed into
   * the table's filter or a menu's field is that window's. */
  if (e->kind == ROLLTUI_EVENT_KEY) {
    size_t alen = 0;
    const char* a = NULL;
    if (rolltui_windows_app_key(t->windows, e, "app", 3, &a, &alen) == ROLLTUI_SEQUENCE_PREFIX) return; /* on the way to one */
    if (a && alen == 8 && memcmp(a, "app.quit", 8) == 0) {
      rolltui_run_stop(run);
      return;
    }
    if (a && rolltui_window_stack_action_popup(t->stack, t->layout, a, alen)) {
      rolltui_windows_sync(t->windows, t->stack); /* its widget now, for the keys typed after it in this batch */
      return;
    }
  }
  memset(&window, 0, sizeof window);
  kind = rolltui_window_stack_route(t->stack, e, screen_area(t), t->bindings, rolltui_stack_default_actions(), &window);
  if (kind == ROLLTUI_ROUTE_DELIVER && window.n) {
    if (!rolltui_windows_handle(t->windows, window.p, window.n, e)) {
      RolltuiMenu* m = rolltui_windows_menu_at(t->windows, window.p, window.n);
      if (m) on_menu(t, m, e);
    }
    on_list(t);
  }
  rolltui_str_free(&window);
}

static void on_resized(void* ctx, RolltuiRun* run, int w, int h) {
  Tour* t = (Tour*)ctx;
  (void)run;
  t->w = w, t->h = h;
}

/* ---- making and freeing it -------------------------------------------------------------------- */

static void tour_init(Tour* t) {
  RolltuiLayoutReport rep;
  size_t nd = 0;
  const RolltuiLayoutAction* defaults;
  RolltuiListColumn cols[6];
  RolltuiListOptions lo;
  static const struct { const char* title; int width; unsigned char align; int min_screen; unsigned char sort; } kCols[6] = {
      {"name", 0, ROLLTUI_LIST_ALIGN_LEFT, 0, ROLLTUI_LIST_SORT_TEXT},     {"pid", 6, ROLLTUI_LIST_ALIGN_RIGHT, 0, ROLLTUI_LIST_SORT_NUMBER},
      {"user", 8, ROLLTUI_LIST_ALIGN_LEFT, 56, ROLLTUI_LIST_SORT_TEXT},    {"cpu %", 6, ROLLTUI_LIST_ALIGN_RIGHT, 0, ROLLTUI_LIST_SORT_NUMBER},
      {"mem", 7, ROLLTUI_LIST_ALIGN_RIGHT, 44, ROLLTUI_LIST_SORT_NUMBER}, {"state", 8, ROLLTUI_LIST_ALIGN_LEFT, 66, ROLLTUI_LIST_SORT_TEXT}};
  size_t i;
  memset(t, 0, sizeof *t);
  t->seed = 0x9E3779B97F4A7C15ULL;
  t->w = 80, t->h = 24;
  t->ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(t->ctx);
  t->fx = rolltui_effect_scratch_new();
  t->draw = rolltui_draw_scratch_new();
  t->windows = rolltui_windows_new(t->ctx);
  t->stack = rolltui_window_stack_new();
  t->compose = rolltui_compose_scratch_new();
  {
    RolltuiThemePresetReport trep;
    RolltuiBindingsPresetReport brep;
    memset(&trep, 0, sizeof trep);
    memset(&brep, 0, sizeof brep);
    t->themes = rolltui_theme_store_new(t->ctx, "", 0, 0, "", 0);
    t->keys = rolltui_bindings_store_new(t->ctx, "", 0, 0, "", 0);
    rolltui_theme_store_start(t->themes, &trep);
    rolltui_bindings_store_start(t->keys, &brep);
    rolltui_theme_preset_report_release(&trep);
    rolltui_bindings_preset_report_release(&brep);
    rolltui_windows_set_theme_store(t->windows, "theme", 5, t->themes, /*persist=*/0);
    rolltui_windows_set_bindings_store(t->windows, "keys", 4, t->keys, /*persist=*/0);
  }
  make_procs(t);
  for (i = 0; i < 60; ++i, t->now_ms += TICK_MS) advance(t); /* a history to draw from the first frame */
  t->now_ms = 0;

  make_settings_menu(t);
  rolltui_context_add_menu(t->ctx, "settings", 8, t->settings_menu.p, t->settings_menu.n);
  rolltui_context_set_help(t->ctx, "", 0, "", 0);
  rolltui_context_clear_help_scopes(t->ctx);
  rolltui_context_add_help_scope(t->ctx, "app", 3);
  rolltui_context_add_help_scope(t->ctx, "list", 4);
  rolltui_context_add_help_scope(t->ctx, "stack", 5);
  rolltui_context_add_help_scope(t->ctx, "transcript", 10);

  memset(&rep, 0, sizeof rep);
  defaults = rolltui_layout_shipped_default_actions(t->ctx, &nd);
  t->layout = rolltui_load_layout_text(kLayout, LEN(kLayout), defaults, nd, rolltui_layout_default_hooks(), &rep);
  if (rep.error.n) fprintf(stderr, "rolltui-tour: %s\n", rep.error.p);
  for (i = 0; i < rep.bad_values_n; ++i) fprintf(stderr, "rolltui-tour: %s\n", rep.bad_values[i].p ? rep.bad_values[i].p : "");
  rolltui_layout_report_release(&rep);
  rolltui_window_stack_set_base(t->stack, rolltui_layout_base(t->layout));
  rolltui_window_stack_set_level_fn(t->stack, rolltui_windows_back, t->windows);
  sync_theme(t);
  sync_keys(t);

  rolltui_windows_bind_list(t->windows, "procs", 5, procs_count, procs_row, t, NULL);
  memset(cols, 0, sizeof cols);
  for (i = 0; i < 6; ++i) {
    rolltui_str_set(&cols[i].title, kCols[i].title, strlen(kCols[i].title));
    cols[i].width = kCols[i].width;
    cols[i].align = kCols[i].align;
    cols[i].min_screen = kCols[i].min_screen;
    cols[i].sort = kCols[i].sort;
  }
  rolltui_windows_set_list_columns(t->windows, "procs", 5, cols, 6);
  for (i = 0; i < 6; ++i) rolltui_str_free(&cols[i].title);
  memset(&lo, 0, sizeof lo);
  lo.marks = 1;
  lo.scrolloff = 2;
  rolltui_windows_set_list_options(t->windows, "procs", 5, &lo);
  rolltui_windows_bind_sample_document(t->windows, "readme", 6, kReadme, LEN(kReadme));
  rolltui_transcript_scroll_to_top(rolltui_windows_transcript(t->windows, "readme", 6)); /* a page is read from its top */
  rolltui_windows_bind_chart(t->windows, "cpu", 3, cpu_chart, t, NULL);
  rolltui_windows_bind_chart(t->windows, "disk", 4, disk_chart, t, NULL);
  rolltui_windows_bind_chart(t->windows, "net", 3, net_chart, t, NULL);
}

static void tour_release(Tour* t) {
  rolltui_rows_release(&t->status);
  rolltui_str_free(&t->settings_menu);
  rolltui_layout_free(t->layout);
  rolltui_compose_scratch_free(t->compose);
  rolltui_window_stack_free(t->stack);
  rolltui_windows_free(t->windows);
  rolltui_bindings_free(t->bindings);
  rolltui_draw_scratch_free(t->draw);
  rolltui_effect_scratch_free(t->fx);
  rolltui_effect_map_free(t->effects);
  rolltui_bindings_store_free(t->keys);
  rolltui_theme_store_free(t->themes);
  rolltui_context_free(t->ctx); /* last: the registries every handle above resolved through */
}

static RolltuiRunApp tour_app(Tour* t) {
  RolltuiRunApp app;
  memset(&app, 0, sizeof app);
  app.ctx = t;
  app.start = on_start;
  app.ground = ground;
  app.render = render;
  app.event = on_event;
  app.resized = on_resized;
  return app;
}

static RolltuiTerminalOptions tour_options(void) {
  RolltuiTerminalOptions o;
  memset(&o, 0, sizeof o);
  o.alt_screen = 1;
  o.mouse = 1;
  o.hover = 1;
  o.bracketed_paste = 1;
  o.hide_cursor = 1;
  o.handle_signals = 1;
  return o;
}

#if defined(__wasi__)
/* ---- in a web page: the page drives the loop ---------------------------------------------------- */

static Tour g_tour;
static RolltuiRunApp g_app;
static RolltuiRun* g_run;

/* Starts the tour on the terminal the page sized (`rolltui_web_resize`); the first step draws it. 0 when it could not start. */
__attribute__((export_name("tour_start"))) int tour_start(void) {
  RolltuiTerminalOptions o = tour_options();
  int result = 0;
  tour_init(&g_tour);
  g_app = tour_app(&g_tour);
  o.handle_signals = 0;
  o.no_cache = 1; /* a page keeps no file of what the terminal answered */
  o.no_probe = 1; /* nor can it wait for answers: it says what xterm.js is (tools/web/rolltui_web.js) */
  g_run = rolltui_run_begin(0, 1, o, &g_app, &result);
  return g_run != NULL;
}

/* One step: what arrived, then a frame. The milliseconds until the next step is wanted, -1 for "on input", -2 once it stopped. */
__attribute__((export_name("tour_step"))) int tour_step(void) {
  int ms;
  if (!g_run) return ROLLTUI_RUN_STEP_STOPPED;
  ms = rolltui_run_step(g_run);
  if (ms == ROLLTUI_RUN_STEP_STOPPED) {
    rolltui_run_finish(g_run);
    g_run = NULL;
    tour_release(&g_tour);
  }
  return ms;
}
#else
int main(int argc, char** argv) {
  Tour t;
  RolltuiRunApp app;
  int rc;
  (void)argv;
  if (argc > 1) {
    fprintf(stderr, "usage: rolltui-tour\n");
    return 2;
  }
  tour_init(&t);
  app = tour_app(&t);
  rc = rolltui_run(0, 1, tour_options(), &app);
  tour_release(&t);
  if (rc == ROLLTUI_RUN_NOT_A_TERMINAL) fprintf(stderr, "rolltui-tour: needs a terminal\n");
  return rc == ROLLTUI_RUN_STOPPED ? 0 : 1;
}
#endif
