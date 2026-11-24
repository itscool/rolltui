# rolltui guide

How to build a terminal program with rolltui: the parts and how they fit, a first program and its test, the file
formats a screen is made of, and each part of the library in turn. `rolltui.h` has every function's contract.

- **Getting started**: [How the parts fit](#how-the-parts-fit) · [A first program](#a-first-program) · [Testing a program](#testing-a-program) · [Embedding rolltui](#embedding-rolltui) · [Ownership](#ownership) · [Strings](#strings)
- **Screens**: [Layout files](#layout-files) · [The layout at run time](#the-layout-at-run-time) · [Theme files](#theme-files) · [Cut text, footers, strikethrough and focus](#cut-text-footers-strikethrough-and-focus) · [Hover](#hover)
- **Keys**: [Bindings files](#bindings-files) · [Key sequences and counts](#key-sequences-and-counts) · [The keys sheet](#the-keys-sheet) · [The theme sheet](#the-theme-sheet)
- **Widgets**: [Lists, tables and trees](#lists-tables-and-trees) · [The editor](#the-editor) · [Menu files](#menu-files) · [Charts and dashboards](#charts-and-dashboards) · [Progress](#progress) · [Messages](#messages) · [Dialogs](#dialogs) · [Pictures](#pictures) · [The column browser](#the-column-browser) · [Previews](#previews) · [Mermaid diagrams](#mermaid-diagrams) · [Syntax highlighting](#syntax-highlighting) · [File info row](#file-info-row)
- **Running**: [The run loop](#the-run-loop) · [Inline mode](#inline-mode) · [In a web page](#in-a-web-page) · [Work off the UI thread](#work-off-the-ui-thread) · [Other programs](#other-programs) · [File watching](#file-watching) · [The clipboard, the title and the terminal](#the-clipboard-the-title-and-the-terminal) · [Diagnostics](#diagnostics)
- **The terminal**: [Terminal detection](#terminal-detection) · [What a frame sends](#what-a-frame-sends)

# Getting started

## How the parts fit

A rolltui program is a few objects, each with one job:

- **The context** (`RolltuiContext`): the session's vocabulary. Its key bindings, its menus, the
  content kinds a layout may name, its help. `rolltui_context_set_library_defaults` gives it the
  library's own, and every other object is made for one.
- **The terminal** (`RolltuiTerminal`): entered, asked what it draws ([Terminal
  detection](#terminal-detection)), and sent each frame as the bytes that change the screen.
  `rolltui_run` makes one and runs the loop.
- **The layout** (`RolltuiLayout`): a screen, read from a [layout file](#layout-files): its
  windows, how they share the room, and the popups over them.
- **The window stack** (`RolltuiWindowStack`): the layout's layers as they are now. It holds the
  focus, routes each key and press to the window it is for, and composes the frame: borders,
  titles, and each window's inside, drawn by its widget.
- **The windows** (`RolltuiWindows`): a widget for each window, made from the window's content.
  `list:files` is a list whose rows the host binds as `files`; the list keeps its own cursor,
  scroll, marks and filter.
- **The theme**: a `RolltuiStyle` for each role (the background, a title, the cursor's row, an
  error). A [theme file](#theme-files) names the colours for each role on a light and a dark side;
  the library chooses the side and maps the colours down to what the terminal draws.
- **The stores** (`RolltuiThemeStore`, `RolltuiBindingsStore`, `RolltuiLayoutStore`): a person's
  own copies of themes, key bindings and layouts, kept beside the shipped presets and edited in
  the program's sheets and in `rolltui-studio`.

A frame is the stack laid out on the screen, each window drawn by its widget, then what sits over
them: messages, a status line the host draws. An event is routed by the stack to a window and
handled by its widget first; what the widget does not take is the host's. [A first
program](#a-first-program) is exactly that.

## A first program

`examples/hello.c` is a whole program: a screen from a layout file, a list the program fills, a
page of text, a message when a row is chosen, and q to quit. `cmake --build build --target
rolltui-hello` builds it.

<!-- quoted whole: examples/hello.c -->
```c
/* examples/hello.c — the smallest whole rolltui program: a screen from a layout file, a list the program fills, a page of text,
 * a message when a row is chosen, and q to quit. docs/guide.md quotes it whole; tests/hello_test.c runs it with no terminal. */
#include <stdio.h>
#include <string.h>

#include "rolltui/rolltui.h"

/* THE SCREEN, a layout file. A program usually ships it beside itself and reads it in; it is here to keep this to one file. */
static const char kLayout[] =
    "{ \"name\": \"hello\", \"focus\": \"fruit\", \"root\": { \"row\": [\n"
    "  { \"id\": \"fruit\", \"content\": \"list:fruit\", \"size\": 24, \"border\": \"rounded\", \"title\": \"fruit\",\n"
    "    \"focusable\": true },\n"
    "  { \"id\": \"about\", \"content\": \"text:Up and Down move, Enter chooses, q quits.\", \"border\": \"rounded\",\n"
    "    \"title\": \"hello\" } ] } }";

static const char* const kFruit[] = {"apple", "banana", "cherry", "damson", "elderberry"};

typedef struct Hello {
  RolltuiContext* ctx;        /* the session: its keys, menus, kinds */
  RolltuiWindows* windows;    /* each window's widget, made from its content */
  RolltuiWindowStack* stack;  /* the layout's layers, the focus, where a key or a press goes */
  RolltuiLayout* layout;
  RolltuiComposeScratch* compose;
  RolltuiEffectMap* effects;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]; /* the theme: a style for every role */
  int w, h;
  unsigned long long now_ms;
} Hello;

/* THE LIST'S ROWS, asked for as they are drawn */
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

/* A FRAME: the windows laid out on the screen and drawn, then the messages over them. Returns how long to wait. */
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

/* AN EVENT: routed to the window it is for, which takes it first; a q no window took quits. Then what the list says happened. */
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
  if (rep.error.n) fprintf(stderr, "hello: %s\n", rep.error.p); /* every problem the file has is named in the report */
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
```

- The layout names two windows by their content: `list:fruit`, whose rows `fruit_count` and
  `fruit_row` give, and `text:`, whose text is in the file.
- `render` syncs the windows with the stack (a window that appeared gets its widget), lays them
  out on the screen, composes the frame and draws the messages over it. It returns how long to
  wait: -1 until the next event, or the soonest a message moves.
- `event` routes each event. The focused list takes Up, Down and Enter itself; Enter on a row
  becomes a list event the program reads. A key no window took is the program's.
- `main` enters the terminal with `rolltui_run`, which leaves it as it found it, whatever
  happens.

## Testing a program

A program's real loop runs with no terminal: `rolltui_run_begin_headless(w, h, &app, &result)`
makes the same app run at a size, and the test steps it, sends it keys and reads the screen back
as text. `tests/hello_test.c` drives the first program this way, as a program's own test does:

<!-- quoted whole: tests/hello_test.c -->
```c
/* hello_test.c — examples/hello.c run with no terminal, as a program's own end-to-end test runs it: the same app, keys sent to it,
 * the screen read back as text. docs/guide.md quotes it. */
#define _POSIX_C_SOURCE 200809L /* nanosleep */
#define HELLO_NO_MAIN
#include "rolltui/examples/hello.c"

#include <time.h>

#include "testkit/testkit.h"

/* Whether the screen shows `text` anywhere. */
static int shows(RolltuiRun* run, const char* text) {
  RolltuiStr screen;
  int found;
  memset(&screen, 0, sizeof screen);
  rolltui_run_screen_text(run, &screen);
  found = screen.p && strstr(screen.p, text) != NULL;
  rolltui_str_free(&screen);
  return found;
}

/* Steps until the screen shows `text`, for at most `ms`: what moves in, as a message slides in, takes a few frames. */
static int shows_within(RolltuiRun* run, const char* text, int ms) {
  const struct timespec frame = {0, 20 * 1000000};
  int waited;
  for (waited = 0; !shows(run, text) && waited < ms; waited += 20) {
    nanosleep(&frame, NULL);
    rolltui_run_step(run);
  }
  return shows(run, text);
}

/* A key, as the person would press it, and the frame it leads to. */
static int press(RolltuiRun* run, unsigned char key, unsigned int ch) {
  RolltuiEvent e;
  memset(&e, 0, sizeof e);
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = key;
  e.key.ch = ch;
  rolltui_run_inject(run, &e);
  return rolltui_run_step(run);
}

int main(void) {
  Hello h;
  RolltuiRunApp app;
  RolltuiRun* run;
  int result = -1;
  hello_init(&h);
  app = hello_app(&h);
  run = rolltui_run_begin_headless(60, 12, &app, &result);
  testkit_check(run != NULL, "hello runs with no terminal");
  if (run) {
    rolltui_run_step(run);
    testkit_check(shows(run, "apple") && shows(run, "Enter chooses"), "its first frame shows the list and the page");
    press(run, ROLLTUI_KEY_DOWN, 0);
    press(run, ROLLTUI_KEY_ENTER, 0);
    testkit_check(shows_within(run, "You chose banana.", 1000), "Down and Enter choose the second fruit, and the message says so");
    testkit_check(press(run, ROLLTUI_KEY_CHAR, 'q') == ROLLTUI_RUN_STEP_STOPPED, "q quits");
    testkit_check(rolltui_run_finish(run) == ROLLTUI_RUN_STOPPED, "and the run finishes");
  }
  hello_free(&h);
  return testkit_report("hello_test");
}
```

- `HELLO_NO_MAIN` leaves out the program's `main`, so the test builds the same app the program
  runs.
- What moves in takes a few frames: a message slides in. The test steps until the screen shows
  what it waits for, within a bound.

### A program's own end-to-end tests

`rolltui_run_begin_headless(w, h, &app, &result)` runs a program's real loop with no terminal, at
`w` by `h`: what it sends goes nowhere, its colours are 24-bit and nothing in the environment
changes what it is. A test drives it as the person would and reads back what is on screen:

```c
RolltuiRun* run = rolltui_run_begin_headless(80, 24, &app, &result);
rolltui_run_step(run);                      /* the first frame */
rolltui_run_inject(run, &key);              /* a key, a press, a paste or focus */
rolltui_run_step(run);
rolltui_run_screen_text(run, &text);        /* the screen, a row a line */
rolltui_run_resize(run, 100, 30);           /* reported at the next step, as a window's is */
rolltui_tasks_wait_idle(rolltui_run_tasks(run), 2000); /* its workers done */
rolltui_run_finish(run);
```

Where the system reads only a console (Windows), it returns NULL with
`ROLLTUI_RUN_NOT_A_TERMINAL`.

### Recordings

`rolltui_terminal_record(term, path, len)` writes every byte sent to the terminal from then on,
with when it was sent, into an asciicast (v2) file, which asciinema plays and its tools turn into a
GIF; a change of size is in it too. `ROLLTUI_RECORD=demo.cast` in the environment records any
program from before it enters, so the script a test drives a headless run with records the demo
as well. A recording started part way through begins with whatever the next frame sends;
`rolltui_run_invalidate` makes that frame whole.

What the terminal answered and how long each frame took are in the log ([Diagnostics](#diagnostics)).

## Embedding rolltui

Add the repository with `add_subdirectory()`. Its `CMakeLists.txt` guards `project()` and the
C++ standard with `if(NOT PROJECT_NAME)`, so it works standalone or nested.

Place the repository in a folder named `rolltui` (a submodule or a copy) and include
`rolltui/rolltui.h`. Built standalone, CMake creates the `rolltui/` prefix in the build directory.
Every include inside the library is spelled `rolltui/...`; `rolltui-boundary-test` checks that the
library includes only its own headers.

The library links only the platform's C and C++ libraries. It compiles against `testkit/`, a
small test harness shipped in this repository; a host that already defines a `testkit` target
uses its own. Nested inside another build, rolltui registers no tests (`ROLLTUI_BUILD_TESTS` is on
only at the top level).

## Ownership

Every pointer in the library is one of three kinds:

- **OWNED**: one owner, freed by a named `_free` function.
- **BORROWED**: a raw pointer or view passed across the API; it never owns and never frees.
- **VALUE**: copied.

The library has no `shared_ptr`. `rolltui-ownership-test` enforces these rules.

## Strings

`RolltuiStr` is the library's string type: owned, NUL-terminated, allocated by the library's
allocator. `clear()` keeps the buffer for the next fill.

`rolltui/str.hpp` is the C++ toolkit over it. It is header-only and adds nothing to `rolltui.h`.

| | |
|---|---|
| `StrView` | a borrowed view: `find`, `rfind`, `substr`, `starts_with`, `ends_with`, `compare` |
| `a + b + c` | concatenation into one buffer |
| `appendf` | formatted append |
| `StrVec` | a growing list |
| `path_base`, `path_dir` | path parts |

Use these rather than converting to and from `std::string`.

# Screens

## Layout files

A layout file is JSON: a screen's windows, how they share the room, and the popups over them.
`rolltui_load_layout_text(text, len, actions, n, hooks, &report)` reads one. Every key it does not
know and every value it cannot take is named in the report (`RolltuiLayoutReport`), and the rest of
the screen loads. The studio's layout editor edits the same file.

```json
{
  "name": "browser",
  "min_width": 60, "min_height": 10,
  "focus": "files",
  "root": { "row": [
    { "id": "files", "content": "list:files", "size": "40%", "border": "rounded", "title": "files",
      "focusable": true },
    { "id": "preview", "content": "output:preview", "border": "rounded", "title": "preview" }
  ] },
  "popups": [
    { "id": "find", "dismiss": false, "anchor": "top", "x": "50%", "y": 0, "w": "60%", "h": 3,
      "root": { "id": "find", "content": "input:find", "border": "rounded", "title": "find" } }
  ]
}
```

### The file

| Key | Value |
|---|---|
| `root` | the screen's node (required) |
| `name` | the layout's name |
| `focus` | the id of the window focused first |
| `popups` | layers over the screen, each as below |
| `min_width`, `min_height` | the least screen it is drawn on; on a smaller one a note says so ([A screen too small](#a-screen-too-small)) |
| `inline` | `true`: drawn below the shell prompt in the normal screen ([Inline mode](#inline-mode)) |
| `actions` | the host's actions its keys may name, each with what it does: `{ "app.find": "find in the transcript" }` |

### A node

A node is a window or a split, by which one of these it has:

| Key | The node is |
|---|---|
| `content` | a window showing `kind:source` (below) |
| `tabs` | a window showing one of several contents, `[{ "title": …, "content": … }]` ([Tabs](#tabs)) |
| `row`, `column` | a split: its children side by side, or one above the other |
| `grid` | rows of cells, `[[node, …], …]`, or `{ "columns": N or "auto", "cell_height": rows, "cells": [ … ] }` |

Any node may have:

| Key | Value |
|---|---|
| `id` | its name; a window without one is named by its content |
| `size` | its share along its split: cells (`30`), `"40%"`, `"fill"`, `"fill 2"`, or `"content"` for as tall as what it holds |
| `min`, `max` | the cells its share is held between |
| `visible` | `false`: hidden until the host shows it |
| `breakpoint` | `{ "width", "height", "pane_width", "pane_height" }`: shown only while there is that much room ([Breakpoints](#breakpoints)) |
| `padding` | cells kept clear inside its border |
| `span` | in a grid of cells, `[rows, columns]` |

A window also has `title`, `footer`, `focusable`, `background` (a theme role's name), `border`
(`none`, `single`, `rounded`, `double`, `heavy`, `hidden`, `quadrant-inside`, `quadrant-outside`;
[Borders](#borders)) and, with tabs, `tab`, the one shown first. A split also has `gap`,
`separators`, `stack_below` and `focus_weight` ([Room and spacing](#room-and-spacing)).

### Content

A window's `content` is `kind:source`:

| Kind | Source | Shows |
|---|---|---|
| `text` | the text itself | a page of text |
| `file` | a path | a file |
| `output` | a name the host binds | another program's output ([Coloured output](#coloured-output)) |
| `transcript` | a name the host binds | a document: Markdown, code, diagrams |
| `input` | the name its lines go to | a line or several to type in ([The editor](#the-editor)) |
| `list` | a name the host binds | a list, table or tree ([Lists, tables and trees](#lists-tables-and-trees)) |
| `rows` | a name the host binds | labelled values the host fills |
| `chart` | a name the host binds | a chart ([Charts and dashboards](#charts-and-dashboards)) |
| `gauge` | a name the host binds | a progress bar ([Progress](#progress)) |
| `image` | a path, or a name the host binds | a picture ([Pictures](#pictures)) |
| `menu` | a [menu file](#menu-files)'s name | a menu |
| `help` | a key scope, or none for all | the keys, editable ([The keys sheet](#the-keys-sheet)) |
| `keys` | none | the keys editor |
| `theme` | none | the theme editor ([The theme sheet](#the-theme-sheet)) |
| `filepicker` | the folder it opens in | a file picker |
| `messages` | none | the messages the program has shown ([Messages](#messages)) |
| `dialog` | the dialog's id | a dialog the host asks ([Dialogs](#dialogs)) |

A host adds kinds of its own with `rolltui_widget_kind_register` and
`rolltui_context_register_kind`; a host's kind never shadows the library's.

### A popup

A popup is a layer: its own `root` and `focus`, an `id`, and where it sits on the screen.

| Key | Value |
|---|---|
| `dismiss` | required: `true` closes it on a press outside it; `false` keeps it until it is answered |
| `modal` | `true`: the focus stays in it, the screen under it is shaded, and presses under it are dropped |
| `x`, `y`, `w`, `h` | cells, or `"N%"` of the screen; `w` and `h` are the whole screen unless said |
| `anchor` | the point of the popup placed at (`x`, `y`): `top-left` (the default), `top`, `top-right`, `left`, `center`, `right`, `bottom-left`, `bottom`, `bottom-right` |
| `min_w`, `min_h`, `max_w`, `max_h` | bounds on its size, in cells or `"N%"` |
| `clamp` | `false` lets it run off the screen; by default it is moved to fit |
| `margin` | columns kept clear at the screen's left and right edges while it is moved to fit |

## The layout at run time

A layout file says what a screen holds. The window stack changes what it shows while the program
runs, by window or split id; each call answers 0 when nothing has that id.

### Borders

A window's `"border"` is one of `none`, `single`, `rounded`, `double`, `heavy`, `hidden` (a ring
of the window's ground that keeps its room and its title), `quadrant-inside` (half blocks drawn
inside the edge, ▗▄▖) or `quadrant-outside` (▛▀▜). Where ambiguous glyphs are wide every kind is
drawn with `+ - |`. `rolltui_draw_border(frame, scratch, rect, ROLLTUI_BORDER_*, line, title, n,
title_style, ambiguous_wide)` draws one inside a widget's own content, for a group or a nested
section.

A theme's `"glyphs": { "cursor": "caps" }` rounds the ends of the cursor's row in lists, menus and
the column browser with a half block each (▐…▌); `"edges"` draws a bar at each end and leaves the
row on its own ground, its text in the cursor's colour, so a terminal's own background shows
through; `"fill"`, the default, draws the row in the cursor's style. A list keeps a cell clear at
each side for the ends; a menu draws them in its inset and draws a fill without one.
`rolltui_cursor_text_style` and `rolltui_frame_cursor_ends` draw the same in a host's own widget.

### Tabs

A window with `"tabs"` in place of `"content"` shows one of several contents:

```json
{ "id": "side", "focusable": true,
  "tabs": [ { "title": "Files", "content": "list:files" },
            { "title": "Log", "content": "output:log" } ] }
```

- The titles are a strip in the window's top border: the shown one bold and underlined in the
  `title` role, the others in the border's. A window with tabs has a border: single unless the
  file names another, and `"none"` is refused. `"tab"` names the tab shown first.
- A press on a title shows that tab. `stack.next_tab` (Ctrl-PageDown) and `stack.prev_tab`
  (Ctrl-PageUp) cycle the focused window's tabs. `rolltui_window_stack_set_tab` shows one, and
  `rolltui_window_stack_tab` says which is shown and how many there are.
- Each tab's widget keeps its state while another is shown: a list's cursor, a transcript's
  scroll.
- A new base keeps each window's tab.

### Breakpoints

`"breakpoint": { "width": 100, "height": 30 }` on any node shows it only while the screen is at
least that big. Below it, the node takes no space and focus passes over it. Either bound may be
left out. `"pane_width"` and `"pane_height"` judge the same by the room the node's parent has, so a
column of a split drops out when its split is narrow, whatever the screen is.

### Room and spacing

- `"min"` and `"max"` hold a node's share to a number of cells. A fill held at a bound gives what
  it does not take to the other fills, by their weights.
- `"size": "content"` makes a window as tall as what it holds and a spare line, within its `min`
  and `max`, each frame; the fill windows share what it leaves. Lists, text and output windows
  size themselves this way.
- `"padding"` keeps cells clear inside a split's or a window's border; `"gap"` keeps cells between
  a split's children, and `"separators": true` draws a line in each gap.
- `"stack_below": 80` on a row places its children as a column while it is narrower than 80 cells:
  two diffs side by side on a wide screen, one above the other on a narrow one.
- `"focus_weight": 2` on a split gives the child holding the focus that weight: the focused panel
  grows as focus moves.
- `"grid": { "columns": 3, "cells": [ … ] }` lays cells on three equal columns in reading order, a
  cell with `"span": [2, 2]` taking two rows and two columns. `"columns": "auto"` with
  `"cell_height": 3` takes as many columns as it needs to fit every cell three rows tall: a core a
  cell, in one to three columns as the height allows.

`rolltui_layout_fits(layout, w, h)` answers whether a layout fits a screen: its minimums, fixed
sizes, borders, padding and gaps, and its own `min_width` and `min_height`. A host refuses a preset
that would not fit with it, and `rolltui_window_stack_set_visible` refuses to show a node that would
make the layout need more than the screen (`ROLLTUI_STACK_REFUSED`).

### Hiding, resizing and maximising

- `rolltui_window_stack_set_visible(stack, id, len, visible)` shows or hides a node. A hidden node
  takes no space.
- `rolltui_window_stack_set_size(stack, id, len, size, size_len)` sizes a node as the file spells
  it: `"30"` cells, `"40%"`, `"fill"` or `"fill 2"`.
- `rolltui_window_stack_maximize(stack, id, len)` makes one window fill its layer, and gives it the
  focus; a NULL id puts the layout back. `stack.maximize` (Alt-Z) does both for the focused window.
  `rolltui_window_stack_maximized` names the window.
- `rolltui_window_stack_set_half(stack, id, len)` gives a window half the screen: the rest of the
  layout in one half and the window in the other, side by side on a wide screen and stacked on a
  tall one. `stack.half` (Alt-Shift-Z) does it for the focused window, and again puts it back.
- `rolltui_window_stack_anchor(stack, rect)` places the topmost popup just under `rect`, above it
  where there is no room below, kept on the screen: a rename box under the file it renames.
- `stack.focus_number` (Alt-1 to Alt-9) focuses the window with that number, counting the
  focusable windows of the focus layer in order. `rolltui_window_stack_set_numbered(stack, 1)`
  puts each one's number before its title. `stack.focus_left`, `_right`, `_up` and `_down`
  (Alt-Shift-H, L, K, J) focus the nearest window that way.
- `rolltui_window_stack_set_busy(stack, id, len, 1, since_ms)` puts a spinner before a window's
  title while it works, turning from `since_ms`; 0 takes it away.
- `rolltui_window_stack_set_numbered(stack, 2)` puts each number as a superscript before the title
  ("²files"), and plain digits where ambiguous glyphs are wide.
- `rolltui_window_stack_set_border_role(stack, id, len, role)` draws a window's border in a role
  of its own while it is in a state: searching, an error, a filter applied. 0 gives it back the
  focus colours.
- `rolltui_window_stack_set_subtitle(stack, id, len, text, n)` writes a subtitle at the right of
  the top edge: a live count, a mode.
- `rolltui_window_stack_set_title_parts(stack, id, len, parts, n)` makes the title of
  `RolltuiTitlePart`s, each in its own role. A part with an `action` is a label, drawn underlined:
  a press on it routes as `ROLLTUI_ROUTE_LABEL`, naming the window, and
  `rolltui_window_stack_pressed_label` gives the action, once.

Visibility and size last until the next `rolltui_window_stack_set_base`; the maximised window carries
over to a new base while its window is in it.

### Views in a window

`rolltui_windows_push_view(windows, window, len, content, clen, title, tlen)` draws another content
in a window's place, with its own keys: a drill-down, a pager, a log. The window's own content keeps
its state underneath. The title reads the views' titles after the window's own ("files › details").
Escape pops the top view once it has nothing of its own to back out of, as does
`rolltui_windows_pop_view`; `rolltui_windows_view_depth` says how deep the window is.

A press or key that changed what is shown makes `rolltui_window_stack_route` answer
`ROLLTUI_ROUTE_LAYOUT_CHANGED`, naming the window.

### A screen too small

On a screen smaller than the layout's `min_width` by `min_height`, the stack draws a note in place
of the layout: "the screen is 40×6; this needs 60×8", or "40×6 < 60×8" where the sentence does not
fit, the screen's size bold in the `error` role and the size needed bold in the `ok` role
(`RolltuiLayoutRoles`). A layout with no window in it shows the program's keys instead of a blank
screen. Presses reach nothing; keys still reach the focused window, so a quit key works. A host with
a layout for smaller screens sets it as the base first: rolltui-chat and the studio switch to the
shipped `stacked` layout.

The studio's layout editor edits tabs (typed `Title = content | Title = content`), the shown tab,
a breakpoint (typed `width`, `width x height` or `x height`), and under Spacing and bounds a node's
min, max, padding, gap, separators, stack_below, focus_weight and grid span.

## Theme files

A role's `fg`, `bg` and `underline_color` take `#rrggbb`, `#rgb`, `#BW` (a grey: `#80` is
`#808080`), `"R G B"` in decimals, 0 to 255 for a palette colour, a colour's name, `none` or
`default` (the terminal's own), or a name from the file's `defs`. The terminal's eight are names
for its palette, so a theme for a 16-colour terminal follows the person's palette: `black`,
`red`, `green`, `yellow`, `blue`, `magenta`, `cyan` and `white` are 0 to 7, and `bright-red` and
the rest 8 to 15. Every other W3C name is the colour it names (`cornflowerblue`, `rebeccapurple`).
A role may also be written as SGR's parameters, `"sgr": "1;38;5;208"`, read as another program's
output would set it; `rolltui_style_from_sgr` reads the same for a host.

`"extends": "default"` makes a theme of a shipped one: the file states only the roles it changes,
each over the shipped role, and its effects over the shipped ones.

`"gradients"` names colour ramps for meters, graphs and numbers:

```json
"gradients": { "cpu": { "start": "green", "mid": "#ffd700", "end": "#d03030" } }
```

`rolltui_theme_gradient(colours, mode, "cpu", 3, &gradient)` reads one by name, and
`rolltui_gradient_at(&gradient, t, &colour)` gives its colour at `t` from 0 to 1: a number coloured
by where it falls. A gradient with no `end` is its start alone; with no `mid`, two stops.

`rolltui_colors_distinct(ground, n, 4.5, colours)` makes `n` colours told apart on a ground, each a
hue of its own and each reading on it at 4.5:1: a colour an author, a host or a series.

## Cut text, footers, strikethrough and focus

`rolltui_frame_put_text_cut(frame, scratch, x, y, text, len, style, max_cells, where,
ambiguous_wide, link)` draws text in at most `max_cells`, with an ellipsis where it was cut:
`ROLLTUI_CUT_END` ("a long na…"), `ROLLTUI_CUT_MIDDLE` ("~/src/r…main.c", a path keeping both
ends) or `ROLLTUI_CUT_START` ("…/c/main.c"). Text that fits is drawn whole. The ellipsis is one cell,
or two where ambiguous glyphs are wide, and a wide glyph is never cut in half.
`rolltui_text_cut` gives the same cut as text. The menu, the list and the column browser cut
their text this way.

`rolltui_text_cut_with` and `rolltui_frame_put_text_cut_with` take a `RolltuiCutOptions`:
`marker` in place of the ellipsis (`"~"`), `keep_extension` to keep a file name's extension
whole after the cut ("a_long_rep….pdf"), and `split`, the percent of the room a middle cut gives
the head. `rolltui_path_shorten` shortens each folder of a path from the left to its first
letter until it fits ("~/g/r/c/frame.c", a leading dot kept: ".c" for ".config"), then cuts the
name in its middle with its extension kept.

`rolltui_format_size(n, flags, out, cap)` writes a size the way the column browser, the preview
and a code block's head do: steps of 1024 (K, M, G, T, P, E), one decimal under 10 and none from
10 ("1.5K", "12M", "512"), and a value that rounds up written in the next step ("1.0M"). Its
flags: `ROLLTUI_SIZE_DECIMAL` for steps of 1000 (k, M, G…), `_BITS` for a count of bytes written
as bits ("12Mb"), `_RATE` for "/s", `_BYTE_UNIT` for the unit under the first step ("512 B").
`rolltui_format_count(n, sep, len, out, cap)` groups a count's thousands, by a comma unless
`sep` says otherwise ("12,345,678").

`rolltui_frame_put_spans(frame, scratch, rect, spans, n, align, overflow, ambiguous_wide)` lays a
line of `RolltuiTextSpan`s, each its own style, into a rect: at its left, centre or right
(`ROLLTUI_ALIGN_*`), and where it is wider, cut at the edge, cut with an ellipsis in the style
of the span it cuts, or wrapped at word breaks onto the rows below (`ROLLTUI_OVERFLOW_*`). It
returns the rows used. `rolltui_hotkey_at(label, len, ch, &n)` finds the letter a key names in a
label, to light it: the first word that starts with it, else its first place.

A window's FOOTER is at the right of its bottom border: `"footer"` in the layout file, or what the
widget says that frame through the plugin's `footer` slot. A list says where the cursor is, "12
of 340", while it shows fewer rows than it has or a filter narrows it, and how many rows are
marked. A footer too long for the border is cut at its start, since its end says most.

A selection dims while keys go elsewhere. A list's cursor, a menu's selected row and the column
browser's cursor draw in the `selection` role while their window does not have focus
(`RolltuiMenuRoles.selected_away`; 0 draws it as `selected`).

A style's `strike` draws a line through the text (SGR 9). A theme file sets it per role with
`"strikethrough": true`, the theme editor has it beside bold and italic, and every shipped theme
strikes Markdown's `~~struck~~` text (`md_strikethrough`). A terminal that has no strikethrough
draws the text plain.

A style also has `blink` (1 slow, 2 rapid), `hidden`, `overline`, and an underline with a shape
and a colour of its own: `underline` is 0 or a `ROLLTUI_UNDERLINE_*` (single, double, curly,
dotted, dashed), and `underline_color` its colour, none for the text's. A theme file writes
`"underline": "curly"`, `"underline_color": "#d04040"`, `"blink": true` and `"overline": true`.
The shapes and the colour are sent to terminals that draw them: kitty, Ghostty, WezTerm, foot,
iTerm2, Alacritty, VS Code, mintty, Windows Terminal and VTE from 0.52, by what the terminal
answers to XTVERSION, else what the environment calls it. Every other terminal, and every
terminal behind a multiplexer, gets a plain underline in the text's colour.

`rolltui_style_patch(&base, &over)` puts one style over another: `over`'s colours where it has
them, its underline's shape and colour where it has one, its other attributes added.
`rolltui_frame_tint` patches every cell of a rect: a row dimmed, a match lit.

## Hover

Set `RolltuiTerminalOptions.hover` and the terminal also reports the pointer as it moves with no
button held (mode 1003). It needs `mouse`, which is on by default. While the pointer rests on
something a click would act on, that thing is drawn in the theme's `hover` role:

- a menu row, an option in an open dropdown, and a menu's ▲ and ▼ scroll markers;
- an entry in the column browser;
- the "N more" marker of a text or file window;
- a hint in a hint bar.

The keyboard's cursor never moves with the pointer, and hover sends no menu event. The cursor's
own row keeps its look under the pointer. Headings and disabled items are not lit. Under a modal
popup, or a popup that closes on a press outside it, nothing below the popup is lit.

The window stack learns where the pointer is from the mouse events it routes, and marks the one
window a press there would reach: `RolltuiResolvedNode.hover`, with the pointer's cell in
`hover_x` and `hover_y`. A host's own widget reads them in its `draw`. For what a host draws
outside the windows, `rolltui_window_stack_pointer` gives the pointer's cell and
`rolltui_hint_bar_hover` lights a hint bar from it. `rolltui_hint_bar_add_scope(bar, bindings, "app",
3)` fills a bar with a scope's actions, each its first key the terminal can send and what it does. Short of
room, a bar leaves out whichever hints do not fit; `rolltui_hint_bar_set_priority(bar, action, len,
rank)` makes the lowest-ranked go first, the rest keeping their order.

- Nothing is lit until the terminal first reports the pointer moving. A click alone does not
  say where the pointer rests afterwards.
- A terminal does not report the pointer leaving its window, so the last thing lit stays lit
  until the pointer comes back.
- The shipped themes lift the panel colour toward the text colour. At 16 colours the lift
  collapses into the panel, as the panel collapses into the background. `mono` draws a
  hovered row bold.

dirktui, `rolltui-chat`, paint and the studio turn hover on.

# Keys

## Bindings files

A bindings file says which keys do each action. `default` and `vim` ship with the library; the keys
sheet (F1) and the studio edit a person's own copy, which the bindings store keeps.

```json
{ "name": "mine", "bindings": {
    "list.first": ["home", "g g"],
    "app.save": ["ctrl+s", "ctrl+x ctrl+s"],
    "app.quit": [] } }
```

- `bindings` maps each action to its keys. A key is a chord with its modifiers before it
  (`ctrl+` or `control+`; `alt+`, `option+` or `meta+`; `shift+`; `super+`, `cmd+` or `win+`), or a
  sequence of up to four chords apart by spaces ([Key sequences and counts](#key-sequences-and-counts)).
  `[]` binds the action to nothing.
- An action is `scope.name`: the library's scopes (`input`, `list`, `menu`, `transcript`, `stack`,
  `sheet` and the others) and a host's own, such as `app`, which its layout declares in `actions`.
- `name` is the file's name. `preset`, written by the store, is the preset an edited copy came
  from.
- A file that does not name a library action takes the shipped default's keys for it, and every
  key the loader cannot read is named in its report.

## Key sequences and counts

A bindings file binds a SEQUENCE as it binds a chord: up to `ROLLTUI_SEQUENCE_MAX` (4) chords
apart by spaces, as in `"list.first": ["home", "g g"]` or `"app.save": ["ctrl+x ctrl+s"]`.

- The window table holds the keys on the way for the focused window. A key that begins a
  sequence of its scope waits for the next; a key no sequence goes on with ends it and is what it
  would have been alone. Escape lets go of what was on the way.
- A host's own scope (`app`) gets the same through `rolltui_windows_app_key(windows, event,
  "app", 3, &action, &len)`, called for each key before the key is routed to a window: DONE
  names the action a chord or sequence finished, PREFIX says the key is on the way to one, and
  NONE hands the key on. A sequence on the way in a window is left to that window.
- The window with focus comes first. A key it takes is NONE to the host's scope: a key its own
  scope binds (Ctrl-E the end of a line, Ctrl-P the row above), anything typed into it, and every
  key while an editor sheet waits for one. A list's or an input's Escape is its own only while it
  has a filter, marks or a selection to clear. A widget kind answers this with the plugin's
  `takes` slot; a host matching keys of its own outside `rolltui_windows_app_key` asks
  `rolltui_windows_focused_takes` first. A program's own keys are therefore ones no window scope
  uses: the shipped tables open the file picker on Alt-O and the palette on Alt-P.
- A sequence's first chord cannot also be bound alone in the same scope, and two actions cannot
  share a sequence: the loader reports the conflict and keeps what came first.
- A COUNT is digits typed before an action: `5` then Down moves five. Before an action that names a
  place, it is the place: `5` then `list.last` (End, or `G` in the vim keys) is the fifth row, and
  `50` then `list.percent` (`%`) is half way down. Digits are a count only in a window where a
  digit is not text or a jump: a list (unless its filter is being typed) and a scrolling page
  count; the column browser, a menu and an input do not.
- `any` as a sequence's last key takes whichever character is typed there: `"app.mark":
  ["m any"]` is m, then a letter, and `rolltui_windows_any_key(windows, &chord)` gives the letter
  once `rolltui_windows_app_key` answers DONE. `any` alone, or first, is refused.
- `rolltui_windows_ask_key(windows, action, len, prompt, len)` asks for one key: the panel shows
  the prompt, the next key makes `rolltui_windows_app_key` answer DONE with `action`, and
  `rolltui_windows_any_key` gives the key. Escape lets go of the question.
- After 300 ms with keys on the way, `rolltui_windows_draw_toasts` draws a panel at the screen's
  bottom left: the keys so far in its border, and a row for each key that can follow, in key
  order, with what it does or that more follows. Rows that do not fit one column flow into more,
  and what still does not fit ends in "… N more". `rolltui_windows_pending_keys` gives the keys on the way as text,
  for a status line.
- A widget kind joins in by answering the plugin's `keys` slot with its scope and whether a digit
  is a count; a completed sequence hands it a chord bound to the same action, or calls `perform`
  with the action's name when only a sequence is bound to it.

A chord is spelled without case, so a capital letter is `shift+g`. A terminal sends a typed capital
as the letter itself, and every lookup matches it as shift and the letter. The help shows a bare
letter as typed (`g`) and shift with a letter as the capital (`G`).

The shipped `vim` bindings preset is the default table with j and k, h and l, `g g` and `G`,
Ctrl-D and Ctrl-U added in scrolling pages (both tables move half a page with them in a list and the column browser),
H, M and L for the top, middle and bottom of a list's view, and Ctrl-E and Ctrl-Y to scroll it.
In the column browser those letters are keys rather than jumps to a name.

### Vim's modes in a text field

The `vim` preset gives every text field vim's modes. It binds `input.normal_mode` to Escape; the
default preset binds it to nothing, so a field there has no modes.

- Typing (insert mode) is as in any field, the caret a bar. Escape leaves for normal mode and puts
  the caret back onto the character before it.
- In normal mode a printable key types nothing: it is a command of the `normal` scope, or nothing,
  and never reaches the program around the field. The caret is a block and sits on a character.
  Key sequences (`d w`) and counts (`3 w`) resolve in this scope. The arrows, Enter and the
  field's Ctrl keys work as they do while typing.
- Escape in normal mode is not the field's: a popup the field is in closes, so a second Escape
  closes what the first left in normal mode.
- `v` starts visual mode, which selects from where it began to the caret, both characters
  included; the motions grow it, and `y`, `d` (or `x`) and `c` act on it. Escape or `v` goes back
  to normal mode.
- `R` types over what is there (replace mode, the caret an underline); `r` and a character
  replaces the one under the caret.
- What `x`, `d` and `c` take, and what `y` copies, goes to the field's register, which `p` puts
  after the caret and `P` before it.
- A bordered field names its mode in the bottom border outside typing: NORMAL, VISUAL or REPLACE.
  `rolltui_input_mode` gives it to a host.

| Keys | What they do |
|---|---|
| `i`, `a`, `I`, `A` | type before the caret, after it, at the line's start, at its end |
| `h`, `l`, `w`, `b`, `e`, `0` or `^`, `$`, `j`, `k` | move: a character, a word's start, back, a word's end, the line's ends, a line |
| `x`, `X`, `d w`, `d b`, `d d`, `D` or `d $`, `d 0` | erase: the character, the one before, to the next word, back to this word's start, the line, to its end, to its start |
| `c w`, `c c` or `S`, `C` or `c $`, `s` | erase and type: to the word's end, the line, to its end, the character |
| `y y` or `Y`, `y w`, `p`, `P` | copy the line, to the next word; put after the caret, before it |
| `u`, Ctrl-R | undo, redo |
| `v`, `r`, `R` | visual mode, replace one character, replace mode |

Elsewhere in the column browser a typed letter jumps to the next name starting with it, case and
accents aside, and letters typed within a second of each other are the start of one name: `av`
reaches `avocado.txt` past `alpha`.

F13 to F24 and Super (`super+x`, also spelled `cmd+x`) are keys a bindings file may name. Only the
kitty keyboard protocol sends them, and on other terminals they are reported as undeliverable.

On a terminal with the kitty keyboard protocol the library asks for each chord whole
(disambiguate) and for the key's place on a US layout (alternate keys). A chord no binding
matches is matched again on that key, so Ctrl and the key that is C on a US keyboard is
`ctrl+c` on a Cyrillic or Greek layout too, alone and in a sequence. Typed text is still sent as
text: the library does not ask for every key as an escape. The flags are put back when the
program leaves, is suspended or is stopped.

Inside tmux, keys with modifiers reach the program only where tmux's `extended-keys` is on;
the library reads it and never sets it, and `dirktui check-terminal` says when it is off.

## The keys sheet

A `keys` window and a `help` window are one sheet: every key the program has, under a heading for
each scope, the program's own (`app`) first. A row says what the action does, and its keys stand
beside it in the shortcut role. The line under the rows names the cursor's action as a bindings
file spells it, the whole description when the row is cut, and what is wrong with its keys.
`help:<scope>` shows one scope.

- `rolltui_windows_set_bindings_store(windows, "keys", 4, store, persist)` says where an edit
  goes: the app's Bindings preset store. Every keys sheet on the screen, `help` included, then
  edits into it. Without a store the sheet only shows the keys, and says so.
- Enter on a row takes the next key pressed as a new key for that action, and Escape cancels.
  Delete removes the row's last key. Ctrl-Z and Ctrl-Y undo and redo, Ctrl-O loads a preset,
  Ctrl-S saves the keys as one, and Ctrl-R goes back to the preset that was loaded. These are the
  `sheet.*` actions, which every editor sheet shares, bound in a bindings file like any other.
- An edit lands in the store's working copy, labelled "(modified)" in the sheet's footer until it
  equals its preset again. Only what the person changed is written: the keys a program suggests
  for its own actions stay the program's.
- A key the program has for one of its own actions that another scope also has is marked on both
  rows, each naming the other. An editor sheet's own keys are not marked: they apply only while
  the sheet has focus.
- The lead of `rolltui_context_set_help` is the sheet's first section, "About", and its note the
  last, "More".
- `/` filters the sheet as it filters any list.
- `rolltui_windows_set_keys_editor_show_presets(windows, "keys", 4, 0)` takes the load and save
  keys away from a program with one table; editing, undo and reset stay.

A bindings file that does not name a library action takes the shipped default's keys for it, each
one its scope has free. A saved file names every action, an unbound one as `[]`, so an action it
does not name is newer than the file.

## The theme sheet

A `theme` window is the theme as one sheet: every role under a heading for the part of the
screen it colours, each row saying what the role is for in that role's own colours, then its fg
and bg, each after a swatch, and its attributes. The footer names the preset, the variant and how
many problems the theme has.

- `rolltui_windows_set_theme_store(windows, "theme", 5, store, persist)` says where an edit goes.
- Enter edits the row where it is. Type a colour (any form a theme file takes: `#rrggbb`, a name,
  0 to 255, `none`), or step through the theme's own colours with Up and Down; Left and Right move
  between fg, bg and the attributes, and Space turns an attribute on or off. The whole screen shows the edit as it is made. Enter
  keeps it, and Escape puts back what was kept.
- A role whose colours are too close to read is marked on its row, with what would fix it.
  Ctrl-F (`theme.fix`) applies the fix; Ctrl-Z takes it back.
- Ctrl-D (`theme.variant`) edits the other variant, light or dark. Ctrl-G (`theme.generate`) makes
  a theme from the next seed. Undo, load, save and reset are the `sheet.*` keys every editor
  sheet shares.

# Widgets

## Lists, tables and trees

A `list:<source>` window shows data the host holds. The host binds the source with
`rolltui_windows_bind_list(windows, name, len, count, row, ctx, free_ctx)`: `count(ctx)` says how
many rows there are, and `row(ctx, i, out)` fills row `i`: its `key`, its cells
(`rolltui_list_row_cell`), a `role` for the whole row, a tree's `depth`, and `busy` with
`busy_since_ms` for a spinner at the row's end.

- `rolltui_list_row_span(out, cell, begin, end, role)` draws bytes `begin` to `end` of a cell in a
  role of its own, over the row's style: a permissions string a letter at a time, a branch head in
  a log line. A row holds up to `ROLLTUI_LIST_SPAN_MAX` spans. Colours come from the theme, so a
  span reads in every theme and in mono; a script's ANSI colours are mapped to roles by the host.
- `gutter` is one glyph of the row's own in the mark column, in `gutter_role`: copied, cut, tagged.
  It stands in place of the marks' ▌.

- A plain list asks only for the rows it draws, so a hundred thousand rows cost a screenful.
  Sorting, a filter and a tree ask for every row once, when the data or the question changes:
  `rolltui_windows_list_refresh` says the data changed, and a count that moves says so too.
- A row is its key. The cursor, the marks and which nodes are open follow it through a refresh,
  a sort or a filter, and the cursor's row keeps its place on screen when rows arrive above it. A
  row with no key is keyed by its index.
- A list with nothing to show says "no rows", or "no rows match “…”" under a filter.
- `rolltui_windows_list_set_loading(windows, name, len, 1)` says the rows are still coming. An
  empty list then shows a spinner and "loading…", after 100 ms so rows that come at once never
  flash it, and the footer says "loading…" while rows arrive. 0 when they have all come.
- `rolltui_windows_list_freeze(windows, name, len, 1)` pauses one list: it keeps the rows it has
  while the host's change, and its footer says "paused". The other windows keep updating. 0 takes
  the host's rows again.
- `rolltui_windows_draw_toasts` returns the soonest a window wants its next frame along with the
  toasts': a drag held past a list's edge, a "loading" about to show, a changed row settling. A
  render that returns it as its wait keeps these moving.

`rolltui_windows_set_list_columns` makes it a table. The first column takes the width the others
leave. The rest are `width` cells, or as wide as the widest cell they have shown; that width only
grows, so the columns stay put as the list scrolls. `align` puts a column's text at its right
edge, and `min_screen` shows a column only while the list is at least that wide. When the first
column would be narrower than 8 cells, columns go from the right, except a column marked `keep`:
the first is cut instead. `max_share` holds a measured column to that percentage of the width.
`role` draws a column's cells in a role of their own on a plain row, as the keys sheet draws its
keys in the shortcut role. `rolltui_sparkline_text(values, n, lo, hi, ascii, &out)` writes a
sparkline as text, one cell a value from ▁ to █, for a cell or a status line; with `ascii`, where
block elements are wide, `_.-~^`. The values' history is the host's. A row with fewer cells than the table has columns draws its last cell
across the columns it has none for. A cell that does not fit ends in an ellipsis. The header row
names the columns in bold, the sorted one marked ▲ or ▼.

`rolltui_windows_set_list_options`:

- `tree`: rows nest by `depth`, in tree order, a parent before its children. Guide lines draw the
  shape, and ▾ and ▸ mark open and closed nodes. Nodes shallower than `open_depth` start open
  and the rest closed; 0, the default, starts every node closed. Enter opens and closes a node; with `activate_nodes`, Enter on a node is an
  activation like any other row's.
- `marks`: several rows chosen at once. A marked row is bold in the `accent_1` colour with ▌ in
  the mark column, so it reads without colour.
- `gutter`: the mark column for rows' own glyphs, with `marks` off.
- `close_over`: a node with more children than this starts closed, whatever `open_depth` says.
- `column_cursor`: in a table, Left and Right move a column cursor. Its title is drawn reversed
  and its cell on the cursor's row underlined, and `list.sort` sorts by its column, then the
  other way, then the host's order. `rolltui_windows_list_cursor_column` says which column it is
  on, for copying or editing that cell.
- `reorder`: the person moves rows, and the list asks the host (see below).
- `changes_ms`: a row that appeared or changed at a refresh is drawn bold, italic and in the
  `info` role for this many milliseconds. Without a frame clock, until the next refresh. A row
  that vanished is gone from the host's data; a host that wants it seen going keeps it for a
  moment in a muted role.
- `fade`: each row this many percent fainter for every row it is from the cursor, to at most 70.
  Only RGB colours fade, so a 16- or 256-colour theme draws every row alike.
- `line_numbers`: 1 numbers each row; 2 counts from the cursor's row, which shows its own number.
- `scrolloff`: rows kept between the cursor and the window's edge. Past half the window, the
  cursor stays in the middle.
- `wrap`: the cursor goes from the last row to the first and back.

  `RolltuiMenuOptions` and `RolltuiPickerOptions` take `scrolloff` and `wrap` too.
- `hide_header`: no header row.
- `headings`: with `tree`, a row at depth 0 is a section heading, drawn bold with a rule after its
  name. The cursor passes over headings, a heading is always open, and a filter keeps a heading
  while a row under it matches.

Where the terminal draws ambiguous-width glyphs two cells wide, the guides, the openers, the sort
arrows and the mark are ASCII.

### Keys and the mouse

| Action | Keys | Does |
|---|---|---|
| `list.up`, `list.down` | Up, Down, Ctrl-P, Ctrl-N | the row above, the row below |
| `list.page_up`, `list.page_down` | PageUp, PageDown | a page up, a page down |
| `list.first`, `list.last` | Home, End | the first row, the last row; after a count, that row |
| `list.half_up`, `list.half_down` | Ctrl-U, Ctrl-D | half a page up, half a page down |
| `list.view_top`, `list.view_middle`, `list.view_bottom` | Alt-Home, Alt-M, Alt-End | the top, middle and bottom rows in view |
| `list.scroll_up`, `list.scroll_down` | Alt-Up, Alt-Down | the view a row, the cursor staying on its row while it can |
| `list.percent` | % | after a count, that percent of the way down |
| `list.activate` | Enter | acts on the row; a tree's node opens or closes |
| `list.back` | Escape | ends a visual range, then clears the filter, then the marks |
| `list.mark` | Space | marks or unmarks the row, and moves to the next |
| `list.mark_up`, `list.mark_down` | Shift-Up, Shift-Down | marks from where the range began |
| `list.mark_all`, `list.unmark_all`, `list.invert_marks` | Alt-A, Alt-U, Alt-I | every row shown, none, the others |
| `list.visual` | Alt-V | marks from here to wherever the cursor goes; again, keeps the range |
| `list.visual_swap` | Alt-; | goes to the range's other end |
| `list.open`, `list.close` | Right, Left | opens a node or goes into it; closes a node or goes up to its parent; in a table with `column_cursor`, the next and previous column |
| `list.open_deep` | Shift-Right | opens the node and every node under it |
| `list.open_all`, `list.close_all` | Alt-Right, Alt-Left | every node |
| `list.move_up`, `list.move_down` | Alt-Shift-Up, Alt-Shift-Down | with `reorder`, moves the row |
| `list.sort` | Alt-S | sorts by the next column, then by the host's order |
| `list.reverse` | Alt-R | the sort the other way |
| `list.filter` | / | types a filter in the list's last row |

A press moves the cursor and a double click activates the row. A press in the mark column marks
the row, and a drag marks the rows it passes over. A drag held past the top or bottom of the view
goes on, a row every 50 ms. A press on ▸ or ▾ opens or closes the node. A press on a column's
title sorts by it, and a second press sorts the other way. The wheel scrolls.

A visual range marks every row from where it began to the cursor, over the marks there were
before it. The visual key again keeps what it marked, and Escape ends it with the marks as they
were. Shift-Up and Shift-Down add to the marks and the marks stay: they are what the host acts
on, and a plain arrow does not drop them.

With `reorder`, Alt-Shift-Up and Alt-Shift-Down move the cursor's row, and a drag that starts on
a row moves it, drawn where it would land as the pointer goes. With `marks`, a drag that starts
on a marked row moves, and one that starts on an unmarked row marks a range. The list does not
reorder the host's data: it reports `ROLLTUI_LIST_EVENT_REORDER` with the row's `key` and
`before`, the key of the row it now goes before ("" for the end), and the host moves its rows and
refreshes. A moved row that is marked means every marked row. The cursor follows the key. A
sorted list and a tree do not move rows, and their keys stay the program's.

A filter is space-separated terms, and a row is shown when every term matches within one of its
cells: a plain term fuzzily (`rolltui_fuzzy_match`), `'term` as written, `^term` at the start,
`term$` at the end, `/re/` as a regular expression, and `!term` not at all. Case and accents are
ignored unless a term has a capital. What matched is drawn bold and underlined. Each new query puts
the cursor on the row that matches it best, with the rows in their own order. Enter keeps the
filter and gives the keys back to the list. Escape while editing a kept filter goes back to it;
otherwise it clears the filter. In a tree, a filter keeps the parents of what it matches, and shows
them open. Menus filter the same way, the selection landing on the best match, and the command
palette lists its rows best match first.

A `ROLLTUI_LIST_SORT_TEXT` column sorts in natural order. A `ROLLTUI_LIST_SORT_NUMBER` column
sorts by the number a cell starts with, reading K, M, G and T as powers of 1024 and s, m, h and d
as an age; a cell with no number goes last in either direction. Equal cells keep the host's
order. A sorted tree sorts the rows of each level and keeps every row under its parent; with
`headings`, the headings keep the host's order and their rows sort. A tree whose chains of single
children should read as one row (`src/main/java`) gets that row from the host, which knows what
joins the names.

`rolltui_windows_list_event` pops what happened, oldest first: `MOVED`, `ACTIVATE`, `MARKED`,
`SORTED`, `OPENED`, `CLOSED`, `FILTERED` and `REORDER`, each with the row's key (the filter's text
for `FILTERED`). `rolltui_windows_list_cursor` and `rolltui_windows_list_marked` give the keys to
act on, and `_set_cursor` and `_set_filter` set them. `rolltui_windows_list_sort` and
`_sort_state` set and read the sort. `rolltui_windows_list_mark_matching(windows, name, len,
query, qlen, on)` marks or unmarks every row a filter query matches, shown or not: `/\.log$/`,
`^tmp`. `rolltui_windows_list_reveal` goes to a row by its key, opening its parents, and
`_set_open` opens or closes a node. `rolltui_windows_list_perform` runs an action
by name, for a host's own menu entry. In a popup, Escape clears the filter, then the marks, then
closes the popup.

### Ordering and finding text

`rolltui_natural_compare(a, alen, b, blen)` orders text as a person reads it. A run of digits
compares by value, so `file2` comes before `file10` and `7` before `07`. The rest compares
case-folded with accents removed, and the bytes as written break a tie. The column browser sorts
names this way.

`rolltui_text_find(hay, hlen, needle, nlen, from, &at, &len)` finds text as a person means it:
case and accents aside unless the needle has a capital, the answer the bytes of what was found.
`rolltui_text_find_each` reports every match. The transcript's find works this way, and takes a
query written `/re/` as a regular expression.

`rolltui_filter_new(query, len)` makes the filter lists and menus read, for a host's own rows:
`rolltui_filter_match` tests a text and scores it, `rolltui_filter_term` tests one term (a row of
several cells matches a term within one of them), and `rolltui_filter_spans` gives the byte
ranges that matched, to light them.

`rolltui_fuzzy_match(pattern, plen, text, tlen, &score, positions, cap)` answers whether the
pattern's letters appear in the text in order. Case is ignored unless the pattern has an
uppercase letter, and accents are always ignored. The score is higher for a run of letters, a
match at the start of a word, and a match early in the text, so candidates sorted by it put the
likeliest first. `positions` receives each matched letter's byte offset in the text, for
lighting what matched.

## The editor

An `input:<target>` window is a text editor. With `single_line` off (the default) its text
wraps and its window grows with it. `RolltuiInputOptions` sets the rest:

- `wrap_words`: a row breaks after a space, so a word moves to the next row whole. A word longer
  than the row is still broken at the edge.
- `wrap_column`: rows wrap at this many columns of text, or at the window's edge when that is
  nearer. `rolltui_input_wrapped_text(in, column, out)` gives the text with a newline at each
  soft break and the spaces before it dropped: a commit message typed at 72 columns is read back
  with the breaks it was seen with.
- `line_numbers`: a gutter numbering each line, in the `text_muted` role. A wrapped line's later
  rows leave it blank.
- `language`: a language name the syntax engine knows (`"c"`, `"json"`, a fence's tag). The text
  is highlighted as code by the shipped languages and the person's own.
- `auto_indent`: a newline starts with the spaces and tabs the broken line begins with.
- `secret`: `ROLLTUI_INPUT_SHOW_MASKED` draws one `•` per grapheme (`*` where that glyph is two
  cells wide); `ROLLTUI_INPUT_SHOW_NOTHING` draws nothing and keeps the caret after the prompt.
  A secret is never copied, never kept in the history or the kill buffer, and word motions move
  by whole lines.
- `refuse_empty`: Enter does nothing while the text is only spaces, tabs and newlines.
- `insert_typed` off: a printable key is looked up in the bindings like any other key, and types
  nothing when none is bound.

The editing keys are the readline set. Each is an action in the `input` scope, so a bindings
file rebinds it:

| Action | Default | Does |
|---|---|---|
| `input.kill_word_backward` | Ctrl-W, Alt-Backspace | kill the word before the caret |
| `input.kill_word_forward` | Alt-D, Ctrl-Delete | kill the word after the caret |
| `input.kill_to_line_start` | Ctrl-U | kill to the line's start |
| `input.kill_to_line_end` | Ctrl-K | kill to the line's end |
| `input.yank` | Alt-Y | put back what the last run of kills took |
| `input.transpose_chars` | Ctrl-T | swap the graphemes either side of the caret |
| `input.transpose_words` | Alt-T | swap the word before the caret with the next |
| `input.upcase_word` | Alt-U | uppercase to the end of the word, or the selection |
| `input.downcase_word` | Alt-L | lowercase the same way |
| `input.capitalize_word` | Alt-Shift-C | capitalise the same way |
| `input.overwrite` | Insert | typing replaces the grapheme under the caret, until pressed again |

A run of kills is one kill: killing three words one at a time yanks back as the three. Ctrl-Y is
redo here, so yank is Alt-Y. Overwrite never replaces a newline. Case follows Unicode's simple
mappings, so `é` uppercases to `É`. `rolltui_u_case` maps a whole text the same way, and its
`ROLLTUI_CASE_FOLD` is for matching without case.

`rolltui_input_perform(in, actions, name, len, now)` runs one command by its action name, as a
bound key would. A modal editor's normal mode is a bindings table over these commands with
`insert_typed` off.

`rolltui_input_set_caret(in, byte, extend)` places the caret; a second call with `extend` 1
selects up to the first. A rename puts the caret before a file's extension this way, or selects
the name without it.

### History

Enter keeps what was sent in the input's history, the newest last, a repeated entry kept once,
at most `history_limit` entries. Up and Down browse it from the first and last rows. With
`history_prefix` on (the default), they recall only entries that begin with the text before the
caret: type `git` and Up finds the last `git` line. With the caret at the start, every entry
matches.

`rolltui_input_history_load` and `rolltui_input_history_save` keep the history in a file, a JSON
array of strings. A save reads the file again first and writes its entries followed by the ones
this input gained since it last loaded or saved, through a temporary file and a rename. Two
instances saving in turn keep both their entries, and a reader never sees half a file.
`rolltui-chat` keeps its prompt's history in its state directory, `<state>/chat/history.json`.

### What an app keeps between runs

`rolltui_state_dir(app, len, &out)` is where an app keeps what it remembers rather than what a
person configures: `ROLL_STATE_DIR/<app>`; under a scratch home, `ROLL_CONFIG_DIR/state/<app>`;
else `XDG_STATE_HOME/roll/<app>`, else `~/.local/state/roll/<app>` (Windows:
`%LOCALAPPDATA%\roll\<app>`). Nothing is made there until something is saved.

A state file keeps names and their text values in a JSON object: marks, tags, the tabs that were
open, the folder last visited. `rolltui_state_file_open(path, len, &err)` reads it (a file not
there yet is an empty one), `_get` and `_set` read and change a name (a NULL value removes it),
`_count` and `_name_at` walk them, and `rolltui_state_file_save(file, &err)` reads the file again
and lays this handle's changes over what is there, through a temporary file and a rename. Two
instances saving in turn keep each other's names; a name both changed keeps the one saved last.
After a save the handle holds what the file holds. A file that is not a state file is reported
and never written over. dirktui keeps its update check's last answer in its state directory.

## Menu files

A menu file is a tree of items; `menu:<name>` shows one, and the studio's menu editor edits it.

```json
{ "id": "root", "label": "menu", "items": [
  { "id": "editors", "label": "Editors", "items": [
    { "id": "theme_editor", "label": "Theme editor", "action": "editor.theme" } ] },
  { "kind": "section", "id": "look", "label": "Look" },
  { "id": "mode", "label": "Light or dark", "kind": "choice", "dropdown": true, "value": "auto",
    "items": [ { "id": "auto", "label": "auto" }, { "id": "dark", "label": "dark" },
               { "id": "light", "label": "light" } ] },
  { "id": "wrap", "label": "Wrap long lines", "kind": "toggle", "checked": true },
  { "id": "tab", "label": "Tab width", "kind": "input", "type": "int", "min": 1, "max": 16, "value": "8" }
] }
```

| Key | Value |
|---|---|
| `id` | what the host is told when the item is chosen; an option's value |
| `label` | what the row says |
| `kind` | `action` (the default), `submenu` (the default for an item with `items`), `toggle`, `choice`, `input`, `section`, `separator` |
| `items` | a submenu's items, or a choice's options |
| `action` | a bindings action the item does the same as: its keys are shown beside it, as bound now |
| `shortcut` | keys to show, for an item with no `action` |
| `description` | a line a host may show for it |
| `enabled` | `false`: listed, muted, never chosen |
| `checked` | a toggle's state |
| `value` | a choice's chosen option; an input's text |
| `dropdown` | a choice's options open in a box over the menu |
| `popup` | a submenu opens in a box over its level |
| `swatch` | a choice's or an input's value is a colour, shown as one |
| `type` | an input's: `text`, `int`, `float`, `color`, `size`, `dim`, `name` |
| `min`, `max`, `step`, `precision` | a number's bounds, the step the arrows take, its decimals |
| `min_len`, `max_len`, `optional` | a text's length, and whether it may be empty |
| `validator`, `hint` | a check the host registered, by name; the line beside the field |
| `columns` | a level that is a table: `[{ "width": cells, "align": "left" or "right" }]` |
| `cells` | a row's text in its level's columns |

A section is a heading over the rows below it and a separator a rule; neither takes the cursor. An
item with both `action` and `shortcut` is named in the report: its keys are the bindings' to say.

## Charts and dashboards

A `chart:<source>` window draws numbers the host binds with
`rolltui_windows_bind_chart(windows, name, len, fill, ctx, free_ctx)`. Before each frame `fill` is
handed the window's chart, cleared, and sets:

- the kind, with `rolltui_chart_set_kind`: `ROLLTUI_CHART_LINE` (the default), `ROLLTUI_CHART_AREA`
  (the line filled to its foot), `ROLLTUI_CHART_BARS`, `ROLLTUI_CHART_SPARK`, `ROLLTUI_CHART_METER`
  (a row a series, its newest value as a bar of ■ along a track), or `ROLLTUI_CHART_AUTO` (a meter
  on one row, a sparkline on two or three, a line on more);
- up to four series, with `rolltui_chart_set_series(chart, i, label, len, role, values, n)`: a name
  (or none), a role (0 for the accents in order) and the values, copied;
- the range, with `rolltui_chart_set_range(chart, lo, hi)`, when it is fixed (a percentage is 0 to
  100 however high it has been). Otherwise it is the values' own, and bars and sparklines stand on
  zero.

A line is drawn in braille, two dots across and four down a cell, each point joined to the next so
a steep change is a line. Bars are a group a value, a bar a series in it, in eighth blocks. A
sparkline is one row a series, a block a value. More values than fit shows the newest, and a NaN
value is a gap. The range's labels are on the left, the most at the top; the series' names, each
in its colour and bold, are on the top row. Braille is narrow in every terminal; where ambiguous
glyphs are two cells wide, the blocks are ASCII.

- `rolltui_chart_set_symbols` draws a line or an area in braille, quadrant blocks (two by two a
  cell) or shades (one a cell, ░▒▓█ by how full). A chart that sets none takes the theme's
  `"glyphs": { "chart": "blocks" }`, braille when it says nothing.
- In an area, a row covers a band of values: full above the value, empty below it, the row the
  value is in filled to it. A value above the least shows at least one dot, and a bar for one at
  least the least block.
- `rolltui_chart_set_gradient(chart, i, &gradient)` colours series `i` by its gradient: a line or
  area a row at a time by height, bars and a sparkline a value at a time, a meter along its length.
- `rolltui_chart_set_flags` takes `ROLLTUI_CHART_FLOW` (a time series: a value a column, the newest
  at the right and the left empty while there are fewer than fit, never stretched),
  `ROLLTUI_CHART_AXES` (a line up the plot's left and one along its foot) and
  `ROLLTUI_CHART_INVERT` (a meter fills from its right). With axes,
  `rolltui_chart_set_time(chart, newest_s, step_s)` writes HH:MM under the foot every ten columns.
- A `RolltuiHistory` keeps the newest values pushed (`rolltui_history_new`, `_push`, `_resize`):
  `rolltui_chart_capacity(chart, cells)` is how many a chart of that width shows, and
  `rolltui_chart_set_series_history` makes a series of it.

`rolltui_chart_new`, the same setters and `rolltui_chart_draw(chart, frame, scratch, area, styles,
ambiguous_wide)` draw a chart inside a host's own widget.

A dashboard is a `"grid"` in the layout file: rows of cells, each cell a node.

```json
{ "grid": [ [ { "content": "chart:cpu" }, { "content": "chart:mem" } ],
            [ { "content": "list:procs" }, { "content": "output:log" } ] ] }
```

A grid is a column whose children are rows; it is written back as that column of rows. A grid with
spans, `{ "grid": { "columns": 3, "cells": [ … ] } }`, is under Room and spacing.

## Progress

A `gauge:<source>` window shows how far something has got, on one row: the label, a bar of
eighth blocks in the `accent_2` role, and the percent. The host binds the source with
`rolltui_windows_bind_gauge` and fills a `RolltuiGauge` each frame:

- `fraction` from 0 to 1. The percent is rounded down, so 100% is said only when it is done.
- `fraction` below 0: how far is not known. The window draws a spinner and the label.
- `failed`: the bar and the percent are drawn in the `error` role.
- `since_ms`: when the work began, the spinner's phase.

A narrow window drops the label before the bar. Where the terminal draws ambiguous-width glyphs
two cells wide, the bar is `#` and `-`.

`rolltui_gauge_draw` draws the same row inside a host's own widget. `rolltui_spinner_draw` puts
a spinner in one cell: a still glyph marked `waiting`, which the theme's `waiting` effect turns.
Every shipped theme maps `waiting`. A menu row whose action is running sets `busy` and
`busy_since_ms`, and the row shows a spinner after its label until the host clears it.

## Messages

`rolltui_windows_post(windows, level, text, len, now)` tells the person something. The levels
are `ROLLTUI_MESSAGE_INFO` (`accent_1`, `i`), `_OK` (`accent_2`, `✓`), `_WARNING` (`warning`,
`!`) and `_ERROR` (`error`, `✗`), so a level reads without colour.

- The window table keeps the newest `ROLLTUI_MESSAGE_HISTORY` (500) messages.
  `rolltui_windows_message_count` and `_message_at` read them.
- Each message shows as a TOAST in the screen's bottom-right corner for `ROLLTUI_TOAST_MS`
  (6 seconds), an error for twice that. The newest is at the bottom; the stack takes at most a
  third of the screen, and the newest always shows. A toast slides in from the right unless the
  env is `still`.
- A `messages` window lists the history, newest at the bottom, and scrolls. One row tall, it
  shows the newest message while its toast would: a status line's message.

A host draws the toasts last, after compose:
`rolltui_windows_draw_toasts(windows, frame, screen, styles, now)` returns how long until they
next change, which the render's wait takes into account. A press on a toast dismisses it:
`rolltui_windows_toast_event` answers 1 for one, and the host stops there.
`rolltui_windows_dismiss_toasts` clears them all.

`RolltuiWidgetEnv.still` says the person asked for no motion: what the library moves by itself
is placed where it ends. A frame dump is one moment, and the hosts here draw it still. dirktui
sets it from its motion setting.

`RolltuiWidgetEnv.wheel_lines` is how many lines a notch of the wheel scrolls, in every window
that scrolls by lines and in the column browser's columns and preview (3 when 0). A menu moves
a row a notch.

dirktui, paint and the studio post a failure as an error toast. dirktui keeps its own status-line
note for what went right.

## Dialogs

A dialog asks the person something and waits for the answer. It opens modal over the screen,
focused, and is as tall as what it holds, up to nine tenths of the screen.

```c
RolltuiDialog* d = rolltui_dialog_new(ROLLTUI_MESSAGE_WARNING, 0, "Delete 3 files?", 15,
                                      "They go to the Trash.", 21);
rolltui_dialog_add_item(d, "notes.txt", 9);
rolltui_dialog_add_button(d, "Delete", 6, 0);
rolltui_dialog_add_button(d, "Cancel", 6, ROLLTUI_DIALOG_DEFAULT | ROLLTUI_DIALOG_CANCEL);
rolltui_windows_ask(windows, stack, "delete", 6, d); /* the table owns it now */
rolltui_windows_sync(windows, stack);
```

- The title sits in the border after the level's glyph, the border and title in the level's role.
- The body wraps to the dialog's width. The items it is about follow, one line each; when they do
  not fit they scroll, and the bottom border says which are shown ("3–8 of 40").
- The buttons sit in a row at the bottom, framed. `ROLLTUI_DIALOG_DEFAULT` marks the one chosen
  when it opens (else the first), `ROLLTUI_DIALOG_CANCEL` the one the close key answers (else the
  last). A dialog given no buttons has one, OK.
- `ROLLTUI_DIALOG_FULL_SCREEN` fills the screen: an error page.
- `rolltui_dialog_set_phrase` adds a field under the body. Every button but the cancel one is
  drawn inert and answers nothing until the field holds the phrase exactly, and letters are typed
  into the field rather than answering.

| Action | Keys | What it does |
|---|---|---|
| `dialog.next`, `dialog.prev` | Tab, Right; Shift-Tab, Left | the next button, the previous one |
| `dialog.answer` | Enter | answer with the button chosen |
| `dialog.up`, `dialog.down` | Up, Down | scroll the items a row |
| `dialog.page_up`, `dialog.page_down` | PageUp, PageDown | scroll the items a page |
| `dialog.copy` | Alt-C | hand the host the dialog's text |

A button's letter answers it: the first letter of its label that no button before it has,
underlined. A press on a button answers it too, and the close key answers the cancel button.

After each event is routed and delivered, the host asks `rolltui_windows_answer(windows, stack,
id, len, &button, &copy)`:

- `ROLLTUI_DIALOG_ANSWERED`: the button's index, and the dialog is closed. A dialog closed some
  other way (the close key, the host popping it) answers its cancel button. Told once.
- `ROLLTUI_DIALOG_COPY`: `dialog.copy` was pressed; `copy` holds the title, the body and the
  items, for `rolltui_clipboard_copy`. The dialog stays open.
- 0: still open, or no dialog of that id.

Asking again under an open dialog's id replaces it. A layout file that names `dialog:<id>` with
no dialog asked under it shows that as a problem, as an unbound source does.

## Pictures

`rolltui_image_load(path)` and `rolltui_image_decode(bytes)` decode PNG, JPEG, GIF (its first
frame), BMP, TGA and PNM to RGBA. A picture larger than `ROLLTUI_IMAGE_MAX_SIDE` (10,000) pixels
on a side, or than `ROLLTUI_IMAGE_MAX_BYTES` (512 MB) decoded, is refused before it is decoded,
with the reason. `rolltui_image_from_rgba` takes pixels a host made. A JPEG whose EXIF says the
camera was held sideways is turned upright as it is decoded.

A picture is scaled to its cells by averaging the pixels each one covers.
`rolltui_image_set_filter(img, ROLLTUI_IMAGE_NEAREST)` takes the pixel under each one's centre
instead, so pixel art and a screenshot of text stay hard-edged.

`rolltui_frame_image(frame, area, img, ground, &used)` fits a picture into a rectangle of cells,
its aspect kept and never enlarged past its own pixels, and draws it the way the terminal draws
pictures. `ground` shows through transparent pixels. `rolltui_image_cells` gives the cells it
would take, for centring.

| Way | Terminals | How |
|---|---|---|
| `ROLLTUI_IMAGES_BLOCKS` | every terminal, tmux, ssh | half blocks, two pixels to a cell in their colours |
| `ROLLTUI_IMAGES_KITTY` | kitty, Ghostty | Unicode placeholders: the picture is in the cells |
| `ROLLTUI_IMAGES_ITERM2` | iTerm2, WezTerm | OSC 1337, drawn by the terminal at a cell |
| `ROLLTUI_IMAGES_SIXEL` | foot, mlterm, Konsole, Windows Terminal, xterm with sixel | sixel, drawn by the terminal at a cell |

The way is chosen by the terminal's name (`TERM`, `TERM_PROGRAM`, then its XTVERSION answer). A
terminal no name tells of is asked: kitty's protocol where it answers kitty's picture query, then
sixel where its Primary DA names it, then blocks. What it said is remembered with the rest of its
answers. Inside tmux, tmux is asked what terminal is outside it and whether the pane passes
escapes through (`allow-passthrough`, read and never set): kitty's placeholders and their pixels
are passed through to kitty or Ghostty where it does; tmux draws sixel itself where the outer
terminal draws sixel and tmux's Primary DA names it; anything else is drawn in blocks. Inside
GNU screen, Zellij and Neovim's terminal, pictures are drawn in blocks. `ROLLTUI_IMAGES` (`blocks`, `kitty`,
`iterm2`, `sixel`) or `RolltuiTerminalOptions.images` chooses outright, and
`RolltuiTermFacts.images` says what was chosen, and `dirktui check-terminal` prints it. A cell's
size in pixels (`cell_w`, `cell_h`) is the terminal's own report (`CSI 16 t`) where its window
size says nothing or disagrees with it, else its window size divided by its cells, read again at
every resize; `rolltui_terminal_set_cell_pixels` sets it outright. Where nothing says, a cell is
taken to be about twice as tall as wide.

A new picture is held until it has stayed in the same cells for 60 ms
(`RolltuiTerminalOptions.picture_delay_ms`; -1 sends at once), so a cursor moving over files sends
none of the pictures it passes. `rolltui_terminal_pending_ms` says when a held one is due;
`rolltui_run` waits on it.

- A kitty picture's pixels are sent once, before the frame that first shows it, and its
  placement whenever its cells change size. On this machine they go in a temp file kitty reads
  and deletes; over ssh, in the escapes. What a terminal did not take is removed when the app
  leaves. Its cells are text to everything else: a popup over
  it covers it, the diff sends only what changed, and leaving deletes what was sent.
- A sixel or iTerm2 picture is drawn after the diff, the cursor kept, when every one of its
  cells is still its own blank and it was not already on the screen. While anything is drawn
  over part of it, it is not sent; once that is gone, it is sent whole again. When it goes, its
  cells are written again, which erases it.
- A sixel is kept off the screen's last row, where the terminal would scroll the screen to move
  the cursor below it. Its palette is fitted to the picture: up to 255 colours, each the mean of
  the pixels it stands for.
- iTerm2 is sent a PNG, JPEG or GIF file as it was read. A picture with none (a host's pixels, a
  BMP, a TGA) is sent as a PNG made for it, its transparency kept.

A picture another program draws (`kitten icat --place`, chafa's sixel, a window over the
terminal such as Überzug++) is the host's to draw and clear; the library says when.
`rolltui_frame_outside_picture(frame, id, cells, ground)` holds the cells blank under the host's
id, and `rolltui_terminal_set_outside_pictures(term, fn, user)` gives the function told:

- `ROLLTUI_OUTSIDE_DRAW` once the cells are blank on the screen and have stayed in place for
  `picture_delay_ms`, after the frame is written, with the rectangle in the terminal's rows and
  columns. The cursor is put back when `fn` returns. It is told again after anything removed the
  picture: a resize or repaint, something drawn over it that has gone, the terminal entered again.
- `ROLLTUI_OUTSIDE_CLEAR` when the picture is no longer where it was drawn: gone from the frame,
  moved, drawn over, the screen about to be written whole, or the terminal left or handed to a
  program. It comes before the frame that writes the cells again; the host removes what text does
  not cover, such as a kitty placement or a window over the terminal. Freeing the terminal clears
  what is still drawn.

A stop sent from the shell (Ctrl-Z) leaves from a signal handler, which tells nothing; the clear
comes when the program continues, then the draw.

An `image:<path>` window shows a picture, centred: the one `rolltui_windows_bind_image` bound
under that name, or the file the path names (relative to the preset directory, as `file:` is),
read again when it changes. With workers lent (`rolltui_windows_set_tasks`) the file is read and
decoded on them, and the picture before it stays on screen until the new one is back. The column browser's preview shows a picture file the same way, with
its size in pixels in the head.

## The column browser

A `picker:` window shows one column per folder from the root to the cursor's, and the folder or file
under the cursor beside them.

| Keys | Action |
|---|---|
| Up, Down, Home, End, Page Up, Page Down | move in the folder |
| Ctrl-U, Ctrl-D | half a page |
| Left, Right | the folder above; into the folder under the cursor |
| Alt-Left, Alt-Right | back and forward through the folders visited |
| `-` | the folder last left |
| Enter, Escape | take the entry; cancel |

A folder keeps the entry its cursor was last on, and the column beside the cursor draws that entry
in the selection's colour, as the path behind the cursor is drawn.

With workers lent (`rolltui_windows_set_tasks`), the folder beside the cursor is read on them as the
cursor moves. A move waits up to 30 ms for it, so a folder read quickly is there in the same frame.
A slower one (a network share, a very large folder) says "loading…" from 100 ms and fills in when its
read is back; going into it meanwhile puts the cursor in it at that point. A folder a host points
the browser at (`rolltui_windows_set_picker_dir`), and each folder on the way to it, is read at once,
as are the folders Alt-Left, Alt-Right and `-` return to.

`RolltuiPickerOptions.ratio_parent`, `ratio_current` and `ratio_next` set the panes' widths as
shares: 1, 2 and 1 give each folder above a quarter, the cursor's folder a half and the pane beside
it a quarter. A share of 0 leaves that pane at the width of its names.

### Marks

With `RolltuiPickerOptions.marks`, entries are marked by path, in any folder: Space marks or unmarks
the entry and moves to the next, Shift-Up and Shift-Down mark as they move, Alt-U clears every mark.
A marked entry is bold in the `accent_1` role with ▌ before it (`*` where ambiguous-width glyphs
are drawn two cells wide). `rolltui_windows_picker_marked` gives the marked paths, for a host to
copy, move or open together.

A host marks entries of its own with `rolltui_windows_set_picker_decor`. Its function is asked for
each entry drawn and fills a `RolltuiPickerDecor`: `gutter`, a glyph in the cell before the name
(a copied or cut tag); `cell`, a few characters right of the name (a git status); `role`, the
name's role. A host's glyph takes the place of the mark's.

### Events

`rolltui_windows_picker_event` reads what the person did: an entry taken, a cancel, a copy. With
`RolltuiPickerOptions.report_changes` it also reads `ROLLTUI_PICKER_EVENT_MOVED` when the cursor is
on another entry and `ROLLTUI_PICKER_EVENT_FOLDER` when the focused folder changes, each with its
path, after those outcomes.

## Previews

`RolltuiPickerOptions.preview` shows the file under the cursor in the right half of the browser
(dirktui: Settings › General › Preview).

| File | Preview |
|---|---|
| Text | its lines, read whole up to 256 KB |
| Markdown | rendered: headings, lists, tables, code blocks |
| Binary | offsets, hex byte pairs and an ASCII column, read one screen at a time |

Control characters are drawn as their symbols (`␛`) and invalid bytes as `�`. A tab reaches the
next stop of `RolltuiPickerOptions.preview_tab` columns (4 when 0). With `preview_wrap`, a line
wider than the preview carries on in the rows below; without it, the line ends in an ellipsis, and
Shift-Left and Shift-Right (from the list or in the preview) and the wheel's sideways turns scroll a
text file's lines sideways, as far as the widest line's end. Alt-Up and Alt-Down (`picker.preview_up`,
`picker.preview_down`; also K and J in the vim preset) scroll the preview a wheel's notch from the
columns, the focus staying where it is.

With workers lent (`rolltui_windows_set_tasks`; `rolltui_run_tasks` gives the run's own), a file is
read off the UI thread and the files above and below the cursor are read ahead, unless a host's
previewer is set. The last four previews are kept, so a file looked at before shows at once; a kept
file is read again only when it changed on disk.

### A host's previewer

`rolltui_windows_set_picker_previewer` puts a host's function before the built-in preview, for a
document's pages or another program's output. It is asked for each file previewed. It fills the
`RolltuiAnsiText` with text and SGR colour and returns 1, or returns 0 and the built-in preview
draws the file.

```c
static int pdf_pages(void* ctx, const char* path, size_t len, size_t offset, RolltuiAnsiText* out, size_t* bound) {
  Pdf* pdf;
  if (len < 4 || memcmp(path + len - 4, ".pdf", 4) != 0) return 0;
  pdf = pdf_open(ctx, path, len);
  if (!pdf) return 0;
  *bound = pdf_page_count(pdf);           /* offset is a page, 0 to *bound - 1 */
  pdf_page_text(pdf, offset, out);        /* rolltui_ansi_feed(out, ...) */
  return 1;
}
rolltui_windows_set_picker_previewer(w, "picker:files", sizeof "picker:files" - 1, pdf_pages, docs);
```

`offset` is where the person has scrolled the preview. With `*bound` left at 0 it is a line of what
the function filled, and scrolling moves through those lines. With `*bound` set it is a place of
the function's own (a page, a percent) from 0 to `*bound` - 1: each scroll asks again at the new
offset, and Page Up and Page Down move one place. The text stays the preview's until the next ask,
so a function may go on feeding it, on the thread that draws, as a command's output arrives.

### A command as the previewer

`rolltui_windows_set_picker_preview_command` runs a program for each file previewed:

```c
static const char* const pv[] = {"pistol", NULL};
rolltui_windows_set_picker_preview_command(w, "picker:files", sizeof "picker:files" - 1, pv);
```

The program gets the file's path, the preview's width and its height after its own arguments
(`pistol notes.md 50 30`). It runs on a pseudo-terminal that size, so it writes colour, and what it
writes is the preview, drawn as it arrives. The cursor resting on a file for 30 ms starts it, so a
cursor moving over files starts nothing; moving away stops it. A program that cannot start leaves
the built-in preview. A host's previewer function is asked before the command.

## Mermaid diagrams

A ```` ```mermaid ```` block is drawn as a diagram wherever Markdown is rendered: previews,
transcripts, documents. Diagrams use box-drawing characters in the theme's colours, or
`+ - | > v` where the terminal draws ambiguous-width characters two cells wide.

Supported: flowcharts (all four directions, every node shape and edge style, subgraphs), sequence
diagrams (loop, alt and opt frames, notes, activations), state diagrams, class diagrams, ER
diagrams, mind maps, timelines, user journeys, Gantt charts and pie charts. Other diagram types
are shown as source, with a line naming the reason.

A diagram wider than its space is drawn with its nodes one column closer, then with its labels
wrapped to narrower and narrower widths. Labels are never cut. A diagram that still does not fit
is shown as source.

## Syntax highlighting

A previewed file in a known language is highlighted, and the preview heading names the language
(`Python · 27 lines · 722 B`). A fenced block in Markdown is highlighted by its tag (```` ```rust ````,
```` ```yaml ````). `RolltuiPickerOptions.no_syntax` turns highlighting off (dirktui: Settings ›
Look › Colour source code in the preview).

### Languages

C, C++, C#, Java, JavaScript, TypeScript, Python, Rust, Go, Swift, Lua, Luau, shell (sh, bash,
zsh), Windows batch, PowerShell, CMake, HTML, CSS, XML, JSON, YAML, INI, TOML, ignore files
(`.gitignore`, `.dockerignore`, `.npmignore` and the rest), `.gitattributes`, DagOnC (`.jc`) and
shader (`.shader`) files.

A file's language is found by its whole name (`CMakeLists.txt`, `.bashrc`), then its longest
extension (`.d.ts` before `.ts`), then its first line (a `#!` line, `<?xml`, `<!DOCTYPE html`).

### Styles

Each token class (keyword, type, function, string, escape, number, constant, comment,
documentation, punctuation, preprocessor, variable, attribute, tag, property, section) is drawn
in an existing theme role, so every theme highlights code. Keywords are bold and comments italic
on top of their role. `mono` tells classes apart by bold, italic, underline and dim.

### Grammar files

A language is a JSON file. The shipped grammars are in `presets/syntax/`. Your own go in
`<config>/rolltui/syntax/` and are read when the browser starts. A file with the name of a
shipped language replaces it, and every language that includes it follows.

```json
{
  "name": "Mine", "aliases": ["mine"], "extensions": ["mine"], "filenames": ["Minefile"],
  "first_line": "^#!.*\\bmine\\b", "ignore_case": false, "not_after": ["."], "word_chars": "-",
  "contexts": {
    "main": [
      { "region": { "begin": "#", "class": "comment" } },
      { "region": { "begin": "\"", "end": "\"", "class": "string", "escape": "\\\\.", "single_line": true } },
      { "words": ["if", "then", "else"], "class": "keyword" },
      { "match": "\\b\\d+\\b", "class": "number" },
      { "match": "\\b([a-z_]\\w*)\\s*(?=\\()", "captures": { "1": "function" } },
      { "include": "lang:C/main" }
    ]
  }
}
```

Rules are tried in order at each position; the first that matches wins.

| Rule | |
|---|---|
| `match` | a pattern; `captures` colours groups separately; `push`, `pop` and `set` change context |
| `words` | a set of whole words |
| `region` | from `begin` to `end`, with an optional `escape` and nested `rules`; `\\1` in `end` is what `begin` captured (here-documents, Lua long brackets) |
| `include` | another context, or another language |

A context can carry its own `class` (`"block": { "class": "comment", "rules": [...] }`), used for
text no rule matches.

Patterns support character classes, groups, `* + ? {n,m}`, lazy quantifiers, `^ $ \\b`,
look-ahead and back-references. Look-behind is not supported. A pattern that runs away is
stopped. Backslashes are doubled in JSON.

### Checking a grammar

A grammar with an error is skipped. `dirktui languages` lists the shipped languages and yours,
and for each file that failed to load, its language, the rule and the reason.
`dirktui languages my.json` checks one file and exits 1 if anything failed. The library function
is `rolltui_syntax_check`.

### Limits

Highlighting works line by line, carrying state from one line to the next (multi-line comments
and strings, here-documents). It is not a parser, so an unusual construct can be miscoloured: a
YAML block scalar whose body looks like keys is drawn as keys. Highlighting covers the first
4,000 bytes of a line and the first 256 KB of a file. It changes styles only, never characters or
their positions.

## File info row

`RolltuiPickerOptions.info` (dirktui: Settings › General, on by default) adds a row under the
column headings showing the mode and modification time, local and to the minute, of the entry to
the right of the cursor: a folder's above its column (`drwxr-x--- 2026-06-01 12:34`), a file's
under its preview heading. With no preview, a file gets a small pane with its name, its size
(unless a size column shows it) and the same row.

The values come from the directory listing, with no extra `stat`, and the watcher keeps them
current. The row takes one line from the columns and is left out when the browser is too short.

# Running

## The run loop

`rolltui_run(in_fd, out_fd, options, app)`:

1. enters the terminal;
2. draws each frame at the terminal's colour depth;
3. waits for input and copies each event before passing it to the app;
4. repaints in full after a resize, after a cached terminal answer proves stale, and when a child
   program such as an editor returns the terminal;
5. restores the terminal before returning.

The app is a `RolltuiRunApp`: `render` and `event` are required; `start`, `ground`, `resized`,
`facts_changed`, `settle` and `focus` are optional. `rolltui_run` returns `ROLLTUI_RUN_STOPPED`,
`ROLLTUI_RUN_NOT_A_TERMINAL` or `ROLLTUI_RUN_BAD_APP`.

Every terminal entered is asked to report two things as they change:

- Its window gaining or losing the person's focus (mode 1004). `rolltui_run` calls the app's
  `focus` with 1 or 0; a loop of its own gets a `ROLLTUI_TERM_EVENT_FOCUS` event with `focused`.
  While the window has lost focus no window on the screen is focused: every border, cursor and
  selection draws as away until it comes back.
- Its background turning light or dark (mode 2031), when the system's appearance changes. The
  terminal's facts change and the app's `facts_changed` re-resolves the theme, as for a stale
  cached answer, so a theme in `auto` mode follows.

A terminal that has neither mode ignores the request.

`rolltui_run_suspend_job(run)` does what Ctrl-Z does in a shell: the terminal is left as it was
found, the job stops, and when the shell continues it (`fg`) the terminal is entered again and the
screen repainted whole. A stop sent from outside (`kill -TSTP`) does the same with no app code.
On Windows there is no job control, and it answers 0. dirktui binds it to Ctrl-Z
(`app.suspend`); where a line has focus, Ctrl-Z is the line's undo.

`rolltui_run_inject(run, event)` hands the app an event it made itself (a key a macro replays),
copied, at the start of the next wake and before what the terminal sent.
`rolltui_run_flush_input(run)`, called while handling a key, drops the rest of the wake's events
and what has been typed and not yet read, so a key held down over a slow action does not run on.

`rolltui_terminal_set_mouse(term, mouse, hover)` turns mouse reporting on or off while the app
runs; off, the terminal selects text with the pointer itself. It holds across a suspend.

An app with threads of its own builds its loop from `rolltui_terminal_new`,
`rolltui_terminal_poll`, `rolltui_terminal_present`, `rolltui_terminal_wake` and
`rolltui_terminal_suspend` / `_resume`. `examples/chat.cpp` does this.

### A step at a time

A host whose own loop must never block (a web page, a game engine, a toolkit's event loop) runs
the same loop a step at a time:

```c
int result;
RolltuiRun* run = rolltui_run_begin(in_fd, out_fd, opts, &app, &result);
/* in the host's loop, when input arrives and when the last step asked: */
int ms = rolltui_run_step(run);
if (ms == ROLLTUI_RUN_STEP_STOPPED) rolltui_run_finish(run);
```

- `rolltui_run_begin` enters the terminal and calls `start`. It returns NULL, with `result` set as
  `rolltui_run` would return it, for an app or a descriptor it refuses.
- Each `rolltui_run_step` takes what has arrived without waiting (events, workers' results, due
  timers, `settle`), then draws and presents one frame. It returns the milliseconds the frame
  asked to wait, sooner where a timer is due, or -1 for "until input arrives". Step again then,
  or as soon as input arrives.
- Once the app stops, every step returns `ROLLTUI_RUN_STEP_STOPPED`. `rolltui_run_finish` leaves
  the terminal as it was found and frees the run.
- `rolltui_run_program` and `rolltui_run_suspend_job` wait, so a stepped host does not call them.

## Inline mode

An inline program is drawn in the normal screen, below the shell prompt, instead of on the
alternate screen. Set `RolltuiTerminalOptions.inline_rows` to run inline. `rolltui_run` draws
frames of that many rows, at most the terminal's height. A host with its own loop draws each
frame at the height it wants, and the region follows it.

- The region starts on the cursor's row, or on the next row when the cursor is past column 1.
  A terminal that does not report the cursor gets the region at the bottom of the screen.
- A region taller than the space below the cursor scrolls the screen up to make room.
- `rolltui_terminal_commit(t, lines)` writes a frame's rows above the region as ordinary lines,
  each trimmed after its last cell. They stay in the terminal's history, where the person
  scrolls, selects and copies them with the terminal's own means. The region is drawn again
  below them.
- Cells in the frame's ground colour are drawn on the terminal's own background. Panels and
  other cells keep their colours.
- Mouse rows are counted from the region's top. A press above the region is dropped.
- After a resize the region is found again. On a terminal that rewraps lines when it narrows
  (every common one except xterm, st and the Linux console), the rows the rewrap added are
  counted.
- Leaving erases the region and leaves the cursor where the region began.

A layout with `"inline": true` is drawn inline, and `rolltui_layout_inline` reports it. In the
studio's layout editor it is the "Drawn inline" toggle under This screen. The shipped `inline`
layout is the conversation and the prompt.

`rolltui-chat` runs inline when its layout is `inline`: chosen in the menu, with
`/set layout inline`, or for one run with `CHAT_LAYOUT=inline`. A finished message is drawn
with `rolltui_transcript_entry_frame`, committed, and removed from the document with
`rolltui_document_remove`. Choosing a whole-screen layout shows the conversation from its start
on the alternate screen. Choosing `inline` again writes only what the terminal does not already
hold. Leaving commits a reply still arriving, as far as it got.

## In a web page

`tools/web/build.sh` builds `rolltui-tour` (`examples/tour.c`) for wasm32-wasi with zig and puts
it beside a page that runs it on xterm.js:

```sh
cmake -S . -B build && cmake --build build
tools/web/build.sh build              # writes build/web
python3 -m http.server -d build/web   # a page loads a .wasm over HTTP, not from file://
```

The library's sources come from the native build's own list (`rolltui_sources.txt`, written by
CMake), so the page is built from what the native build compiles. In the browser:

- Descriptor 1 is the terminal. `tools/web/rolltui_web.js` hands what xterm.js sends to the
  program with `rolltui_web_input`, and a new size with `rolltui_web_resize`, and steps the
  loop when input arrives and when the last step asked.
- The OS layer is `c/rolltui_os_web.c`. A page runs no other programs and has no threads, so a
  task pool runs each job on the thread that drains it. It has no files: every preset is the
  library's embedded copy, and nothing a person changes is kept.
- A page cannot wait for the terminal's answers while the program enters, so the program asks
  nothing (`no_probe`) and the page answers DA1 for xterm.js before it starts. Colour depth and
  light or dark come from the environment the page sets: `COLORTERM=truecolor`, and `COLORFGBG`
  from the browser's colour scheme.

A host of its own built this way exports a start and a step and calls `rolltui_run_begin` and
`rolltui_run_step`, as `examples/tour.c` does under `__wasi__`.

## Work off the UI thread

A `RolltuiTasks` is a few worker threads, the jobs they run, and the queue of what comes back.
A job's `work` runs on a worker; its `done`, and anything posted, runs on the thread that calls
`rolltui_tasks_drain`, in the order it arrived. A host's state is only ever touched by that one
thread.

- `rolltui_tasks_start(t, work, done, ctx)` queues a job and gives its id. `done(ctx, cancelled)`
  is called exactly once: the place `ctx` is freed.
- `rolltui_tasks_cancel(t, id)`: a job still queued never runs; a running one finds
  `rolltui_job_cancelled(job)` true and stops early if it looks. Either way `done` is told
  `cancelled`, and what it made is dropped. Cancelling an id that has ended does nothing.
- `rolltui_tasks_post(t, fn, ctx)`, from any thread: `fn(ctx)` runs at the next drain.
- `rolltui_tasks_call(t, job, fn, ctx)` does the same and waits until `fn` has run, so a job reads
  what only the UI thread may touch (the selection, a store). It returns 0, and `fn` never runs,
  when the job is cancelled or the tasks freed first. Called from the draining thread it runs `fn`
  at once.
- `rolltui_job_ask(t, job, ask, ctx, &answer)` pauses a job on a question for the person: `ask(ctx,
  question)` runs on the UI thread and opens a prompt; whatever takes the answer calls
  `rolltui_tasks_answer(t, question, text, len, yes)`. The job carries on with 1 and the text, 0
  for a refusal, or -1 when it was cancelled first. An answer to a cancelled job's question returns
  0, so the host closes its prompt.
- `rolltui_tasks_running(t, id)`: the job has not ended.
- A finished job and every post call the `wake` the tasks were made with, so a loop asleep in its
  input wait drains at once.
- `rolltui_tasks_busy` counts jobs whose `done` has not run; `rolltui_tasks_wait_idle` drains until
  it is 0, which is what a test waits on.
- `rolltui_tasks_free` cancels what is queued, waits for what is running, and calls every
  outstanding `done` before it returns.

`rolltui_run` makes its own on first use: `rolltui_run_tasks(run)` gives two workers, woken
through the terminal and drained every wake before `settle`, freed when the run ends.

The run also keeps timers. `rolltui_run_after(run, ms, fn, ctx)` fires once and
`rolltui_run_every` repeatedly, both on the run's thread after a wake's events; the wait a render
returns is shortened to the next one due. A periodic timer that falls behind is due a period on
from now, never in a burst. `rolltui_run_cancel_timer` cancels by id.
`rolltui_run_debounce(run, &slot, ms, fn, ctx)` cancels the timer in `slot` and sets a new one,
so a burst of calls fires once, after the last.

`rolltui_windows_bind_changed(windows, name, …)` calls a function with an `input:<name>` window's
text each time it changes: filtering as a person types, debounced with `rolltui_run_debounce`.

`rolltui_windows_set_tasks(windows, tasks)` lends the built-in kinds the workers. The column
browser's preview then reads a file and decodes a picture on a worker while the cursor moves on,
says "loading…" once a read has taken 100 ms, and drops a read the cursor left. Without them it
reads on the drawing thread, which is what a frame dump does. dirktui lends it the run's. A read the
cursor left stops between 64 KB steps, and the lines are indexed and coloured, and Markdown parsed,
on the worker that read them.

### Holding input

`rolltui_windows_hold(windows, label, len, job)` holds input while work the person waits on runs
(a pull, a save over the network). No window gets a key or the pointer, and a box with a spinner,
the label and "Esc cancels" sits over the screen. Escape cancels the job and lets go at once. The
hold ends by itself when the job has ended (on the tasks lent with `rolltui_windows_set_tasks`),
or at `rolltui_windows_release`; with `job` 0 only those two end it. The host still sees every key
first, so its own keys, quit among them, keep working; `rolltui_windows_app_key` leaves Escape to
the hold.

## Other programs

`rolltui_child_spawn` starts a program and hands back a handle that is read and written without
waiting. `argv[0]` is found on PATH (on Windows with PATHEXT's extensions; a script runs through
cmd.exe, which refuses an argument holding `%` or `"`). `RolltuiChildOptions.mode` says how its
streams are wired:

- `ROLLTUI_CHILD_PIPES`: stdin a pipe the host writes, stdout and stderr one stream the host
  reads. The program has its own process group.
- `ROLLTUI_CHILD_PTY`: a pseudo-terminal `cols` x `rows`, so the program writes colour and
  progress as it would to a terminal. `rolltui_child_resize` follows the window it is shown in.
  On Windows it is pipes with `COLUMNS` and `LINES` set, because ConPTY reorders what a program
  wrote.
- `ROLLTUI_CHILD_QUIET`: the null device for all three streams and its own session. Ctrl-C in the
  app never reaches it, and freeing the handle lets it go instead of stopping it.

`cwd` and `env` (`"NAME=value"` entries over the app's environment) are optional. A program that
cannot start gives NULL and the reason ("not found: bat").

- `rolltui_child_pump(c, text, max_lines)` reads what has arrived into a `RolltuiAnsiText` and
  stops once it holds `max_lines`: a view reads what it shows, the program waits on its full
  pipe, and the rest is read when more lines are wanted.
- `rolltui_child_read` gives the bytes as they came. `rolltui_child_write` and
  `rolltui_child_close_input` feed its input.
- `rolltui_child_done` asks whether it has ended without waiting; `rolltui_child_wait` waits.
  The status is its exit code, or 128 plus the signal that ended it.
- `rolltui_child_stop` sends SIGTERM to its group and SIGKILL 200 ms later if it is still there
  (TerminateProcess on Windows). `rolltui_child_free` stops a PIPES or PTY program still running.
- While a program runs, a render waits at most `rolltui_child_wait_ms(c)`, so what it writes is
  drawn as it arrives.

`rolltui_run_program(run, argv, cwd, &status, &err)` hands the terminal to a program that takes
it over (an editor, a pager): the app waits, ignoring the Ctrl-C the program receives, and takes
the terminal back with the screen repainted whole. A SIGTERM sent to the app meanwhile is passed on
to the program, and once the program has ended the app ends by the same signal. `rolltui_open_with_system` opens a file or a
URL with what the system opens it with (`open`, `xdg-open`, ShellExecuteW).

### Coloured output

A `RolltuiAnsiText` reads what a program wrote to a terminal as lines to draw: `bat`, `delta`,
`ls --color`, a log, a man page.

- Text, newlines, tabs (stops every 8 columns, or what `rolltui_ansi_set_tab_width` says) and SGR
  are read: 16, 256 and 24-bit colour, bold, dim, italic, an underline's shape (4:1 to 4:5, 21)
  and colour (58), blink, reverse, hidden, strikethrough and overline. A sequence or a character
  split across two feeds is joined.
- A carriage return not followed by a newline writes the line again, so a progress counter shows
  its last state. Erase-in-line clears the line being written.
- Backspace overstrike is read as a man page writes it: `X\bX` bold, `_\bX` underlined.
- OSC 8 hyperlinks are read onto the text they cover, from the link's opening to its closing,
  across lines as a terminal keeps them, and drawn as links. A URL that is not printable ASCII, or
  an OSC longer than 4 KB, is no link.
- Cursor moves, erase-in-display, every other OSC, DCS, APC and charset designations are
  swallowed. The output reads as what it says and never commands the app's screen.
- The lines are a ring of `ROLLTUI_ANSI_DEFAULT_LIMIT` (100,000), or what
  `rolltui_ansi_set_limit` says, so a log keeps its newest.

An `output:<source>` window shows one. The host binds it with `rolltui_windows_bind_output`.
The window follows the end as the text grows; scrolled back, it holds still until it is scrolled
to the end again. Lines wrap at its edge; `rolltui_windows_set_output_wrap(w, name, len, 0)`
keeps them whole instead. A cell with no colour of its own takes the window's text colour and
ground, so the program's defaults are the theme's. `rolltui_ansi_draw` draws the same text
inside a host's own widget.

### Links in a window

A link in any window (one another program wrote, a Markdown link, one a host drew with
`rolltui_frame_link_id`) is sent to the terminal as an OSC 8 hyperlink, and the window acts on it
too, since a terminal reporting the mouse to the app does not:

- While the terminal reports hover, the link under the pointer is underlined across its cells.
- A press of the left button on a link goes first to the host's handler,
  `rolltui_windows_set_link_handler(w, fn, ctx)`, with the window and the URL; `fn` returning 1
  takes it, for a scheme of the host's own (an editor's link, a program's internal one).
- Otherwise an http, https, mailto or file link is opened with `rolltui_open_with_system`, and a
  link of any other scheme is a press like any other.

### Wrapping and scrolling sideways

The `text`, `file` and `output` windows wrap long lines at their edge. `view.wrap` (Alt-W)
keeps each line whole instead, cut at the edge, and turns wrapping back on; `view.left` and
`view.right` (Left and Right, and h and l in the vim preset) and the wheel's sideways turns
scroll lines kept whole four cells at a time, as far as the widest line's end. A wide glyph the
edge would cut in half is drawn as a blank. In an `output` window the key sets the binding's
wrap, so it and `rolltui_windows_set_output_wrap` agree.

`rolltui_windows_set_output_options` sets an output window's ways:

- `timestamps`: each line's time (HH:MM:SS) in a gutter before it.
- `status`: the footer says "following", or "paused · 12 below" once the window is scrolled back,
  and whether a program run into it is running or how it exited.
- `filter`: only the lines that match are shown, with the lists' filter language, and the matches
  are bold and underlined. Rules always show.

A line's time and source come from `rolltui_ansi_set_stamp(text, ms, tag)`, which stamps the lines
begun after it. `rolltui_ansi_mark(text, label, len)` puts a rule across the text after what is
there. `rolltui_ansi_line_info`, `rolltui_ansi_line_rows` and `rolltui_ansi_draw_line` give a host's
own view the same pieces: a line's time, source and number, the rows it wraps to, and one line
drawn with ranges lit.

### Programs in a window

`rolltui_windows_run_output(w, name, len, &opts, label, role, &err)` runs a program into an
`output:<name>` window, as `opts` says (`ROLLTUI_CHILD_PTY` for its colours). The window reads what
the program writes as it draws, and a pseudo-terminal is told the window's content size, then
each new size. When the window leaves the layout, or the name is bound to something else, the
program is stopped. Where no pseudo-terminal can be opened, the program gets pipes, with the size
in `COLUMNS` and `LINES`.

Several programs run into one name are merged a line at a time, each line led by its program's
`label` in `role` (`web GET /`, `db  SELECT 1`). The footer shows how they are doing unless the
host set the window's options. `rolltui_windows_output_running` says whether one is still running
and how the last one ended; `rolltui_windows_output_mark` puts a rule across the window at a
boundary, such as the start of a build.

`rolltui-chat` runs a `!command` once it is approved: the output streams into the conversation
as a code block and the exit status follows; Esc stops it. A frame dump runs nothing.

## File watching

The column browser re-reads every folder on screen, and the previewed file, about twice a second.

- Files that are added, grow, are removed or are renamed appear within that interval.
- The cursor and scroll position are kept by name. If the selected entry is gone, the cursor moves
  to the entry now in its place.
- A removed folder closes its columns, and focus returns to the deepest folder that still exists.
- A file replaced by a folder, or the reverse, is followed.
- The preview keeps its top line when the start of the file changes, and follows the end of a
  growing file when it is scrolled to the bottom.

Each check is one `stat` per folder and per visible row; no file descriptors are held. An
unchanged folder of 20,000 entries costs about 0.1 ms. A folder that keeps changing is checked
less often. `RolltuiPickerOptions.no_watch` turns watching off.

### Watching files of your own

A `RolltuiWatch` tells a program when files or folders it names have changed:

```c
static void wake(void* term) { rolltui_terminal_wake((RolltuiTerminal*)term); }

RolltuiWatch* w = rolltui_watch_new(wake, term);   /* or NULL, NULL: asked, not told */
unsigned int log = rolltui_watch_add(w, path, strlen(path));
...
unsigned int ids[8];
size_t n = rolltui_watch_changed(w, ids, 8);       /* each change told once */
```

A change is a path's signature moving: its device, inode, size or times. A save that writes a new
file and renames it over the old one, a path that arrives after it was watched, a file removed,
and a folder's entries arriving or leaving are all told. The system says when to look (kqueue on
macOS and the BSDs, inotify on Linux, change notifications on Windows), so a change is seen within
milliseconds; where it cannot, a thread looks twice a second, and in a browser the call itself
looks. `rolltui_watch_how` names which. `wake` is called from the watcher's thread, so a loop
asleep in its input wait looks at once.

### Settings changed elsewhere

`rolltui_run_watch_stores(run, theme, layout, keys)` watches the stores' files while the run lasts:
the working copy, and the preset it follows in the person's folder and the shipped one. A file
another program changes (the studio, an editor) is read again and the screen drawn again, and
SIGUSR1 reads them all again. A store with edits of its own keeps them: only a change to its
working file replaces them. Each store's version moves as an edit's would, so an app that follows
its stores' versions takes the change; dirktui does, for its theme and its keys.
`rolltui_theme_store_refresh` and the layout and bindings stores' own do the same for a host that
reads its files on its own schedule. A store's own writes never count as a change.

## The clipboard, the title and the terminal

`rolltui_clipboard_copy(terminal, text, len)` sets the clipboard two ways at once. OSC 52 goes to
the terminal, the one way that reaches the person's own machine over ssh; inside tmux it is
passed through, inside GNU screen it goes in pieces screen passes on, and above 74,994 encoded
bytes it is not sent, except to kitty, which takes any size. The first native tool that works
gets the text too: pbcopy; wl-copy, xclip or xsel; the Windows clipboard. `$ROLLTUI_CLIPBOARD`
names a command to feed the text to instead of the native tools.

`rolltui_clipboard_paste(terminal, &out)` reads it back through the first tool that answers
(pbpaste; wl-paste, xclip or xsel; the Windows clipboard), or the command `$ROLLTUI_CLIPBOARD_PASTE`
names. Over ssh, where no tool can reach the person's clipboard, it gives what the program last
copied, so a copy and a paste inside one program work there too.

`rolltui_terminal_set_title` sets the window's title (OSC 2). The terminal's own title is pushed
first and put back when the app leaves or hands the terminal to a program, and the app's is set
again when it takes the terminal back. Only a change is written. `rolltui_terminal_report_cwd`
reports a folder with OSC 7, so a new tab or split opens there; dirktui reports the folder it is
browsing.

`rolltui_terminal_set_window_name` names the window in tmux's list (and screen's), which the title
does not. tmux takes the escape for it while its `allow-rename` is on; with it off, the name is set
with `tmux rename-window` instead of being refused unsaid. Leaving the terminal puts back the name
the window had, and tmux's naming it by itself.

`rolltui_open_terminal(&in, &out)` opens the controlling terminal by name (`/dev/tty`; the
console's CONIN$ and CONOUT$), whatever stdin and stdout are. A program whose stdout is its answer
to a shell (a file chooser, cd-on-exit) draws there and prints its answer to stdout. dirktui runs
this way.

## Diagnostics

The terminal is the screen's while a program runs, so what the library and a host say for whoever
is looking goes to a log file: the one `rolltui_log_to(path, len)` names, else the one the
environment's `ROLLTUI_LOG` names. With neither, nothing is written. A line is the local time, the
seconds since the log began, the level and the text:

```
2026-11-24 09:41:07    0.000s INFO  terminal: kitty 0.43.1, 120x40, depth truecolor, keyboard kitty, pictures kitty
```

- `rolltui_log(level, text, len)` and `rolltui_logf(level, fmt, ...)` write a line; a newline in the
  text is a space, so each call is one line, and lines from workers never land inside one another.
- `ROLLTUI_LOG_LEVEL` (`error`, `warn`, `info`, `debug`) or `rolltui_log_set_level` says what is let
  through; INFO by default. `rolltui_log_wants(level)` says whether a line would be written, before
  one that costs is built.
- The library says what terminal it entered and how it draws, a store read again because another
  program changed its files, and at DEBUG what the terminal answered its questions, byte for byte
  and how long it took, and each frame's time: the host's drawing, the diff and the write, and the
  bytes sent.

`rolltui_windows_set_timing(windows, 1)`, or `ROLLTUI_TIMING=1` for any program, puts each
window's draw time in its top border (" 0.4 ms "), muted, and bold in the warning role past half
of a frame at 60 a second. `rolltui_mem_stats` gives the library's live and peak bytes; a CPU
profile is the system profiler's (Instruments, `perf`).

`tools/bench/frame_times.py` times a real program's frames from that log: dirktui on a
pseudo-terminal at 200x50, the cursor walked down a folder. On an M5 Max a frame is drawn in a
median 0.6 ms and diffed and written in 0.2 ms, about 500 bytes. Every window is drawn every
frame: at that cost a spinner turning ten times a second takes under 1% of a core, and a window
that drew only when it said it had changed would show stale contents the one time it did not say.

# The terminal

## Terminal detection

`rolltui_terminal_new` asks the terminal, in the same round trip that sets up the keyboard, how
many colours it draws, whether its background is light or dark, how wide it draws
ambiguous-width characters, whether it gives a combining accent a cell of its own, and whether it
draws a frame whole. The library applies the answers:

- `rolltui_swap_present` never draws at more colour depth than the terminal has.
- Every character is measured at the width the terminal draws it.
- A theme in `auto` mode follows the terminal's background.
- A terminal that has synchronized output (DEC mode 2026) is sent each frame between the mode's
  begin and end, in one write, and draws it whole. A terminal that does not answer is never sent
  the mode.
- On a terminal that gives every code point its own cells (Windows' console host), each grapheme
  cluster is sent as one code point: its first after canonical composition, so `e` and a
  combining acute is sent as `é`.

Windows' own console sets no `TERM` and is drawn in 24-bit colour. A Windows program in Windows
Terminal is asked what any terminal is and hears its answers: synchronized output, kitty's
keyboard, its background, a cell of 10 by 20 pixels, which is the cell it draws sixel to whatever
the font. A picture is drawn at its cell there, natively and from WSL.

The Linux console is known by `TERM=linux` or by its tty, `/dev/ttyN`, whatever `TERM` says.
Its font has the PC's glyphs only, so the frame is sent with the nearest it has: braille as a
shade by how many of its dots are set, quadrants and eighths as half and full blocks, rounded
corners square, heavy lines double, `…` as `~`, `✓` as `√`, superscripts four to nine as digits.
It is drawn in 16 colours, and the cursor's shape is not sent.

When nothing in the environment names the terminal, its answer to XTVERSION chooses how pictures
are drawn: kitty's protocol for kitty and Ghostty, iTerm2's for WezTerm and iTerm2, sixel for foot,
mlterm and Konsole.

Answers are cached per terminal, keyed by terminal name and version, operating system, program
build, and whether the session runs over ssh. Behind a multiplexer (tmux, GNU screen, Zellij) the
answers are the multiplexer's and the terminal under it can change between attaches: they are used,
never cached, and each question waits 200 ms rather than 80. The terminal's own variables
are part of the key where `TERM` does not tell terminals apart: `KONSOLE_VERSION` and
`VTE_VERSION` by their value, `WT_SESSION` and `KITTY_WINDOW_ID` by being set, so Windows Terminal
and Windows' console window are two terminals and a new tab is the same one. A cached answer is used at
startup and checked again after the first frame. A probe costs 0.6 ms fresh and 0.2 ms from the
cache; a terminal that does not answer costs 80 ms, once.

| Variable | Effect |
|---|---|
| `ROLL_COLOR_DEPTH` | `truecolor`, `256`, `16` or `mono` |
| `ROLL_AMBIGUOUS_WIDE=1` | ambiguous-width characters take two cells |
| `ROLL_TERM_PROBE=0` | probe only the keyboard |
| `ROLLTUI_CONSOLE=1`, `=0` | the terminal is, or is not, the Linux console |

`dirktui check-terminal` probes again and reports each answer and where it came from.

Every theme has a light and a dark side. Mode (auto, dark or light) and colour depth are the
person's settings, stored beside the chosen theme and kept when the theme changes. In `auto` the
terminal's background decides: light when its luminance is above 0.5. A terminal that reports its
colour scheme (DSR 996) and not its background is drawn for the scheme it reports; one that reports
both is drawn for its background.

## What a frame sends

`rolltui_swap_present` sends the terminal what changed since the last frame, in the fewest bytes it
knows:

- Unchanged cells are not sent. A gap of up to three unchanged cells between two changes is
  written again; a longer gap is stepped over with a cursor move.
- The cursor takes the shortest move from where the last write left it: a CR and line feeds to the
  start of a row below, a cursor-forward along a row, an absolute move otherwise.
- A style change names only what changed (a colour, an attribute turned on or off), or resets
  and names the whole style where that is shorter.
- A run of 12 or more blank cells is erased (ECH) and stepped over; a row's blank rest of 4 or
  more cells is erased to the end of the line (EL). A blank is a space with no link, underline or
  reverse.
- When a block of three or more rows moved up or down, the terminal scrolls that region (DECSTBM
  with SU or SD) and only the rows the scroll brought in are written.

The cursor's shape (`rolltui_frame_set_cursor_shape`, `ROLLTUI_CARET_*`: block, underline or
bar, each steady or blinking) is sent when it changes, and the terminal's own is put back when the
program leaves. An input field asks for a bar while printable keys type and a block while they
are actions (`RolltuiInputOptionsRaw.caret` sets one). A focused list, menu or column browser
leaves the hidden cursor on its cursor's row, where a screen reader follows it.

The erases and the scrolled region stand on the terminal filling an erase with the current
background colour, which every terminal in use does, and tmux too. Directly under GNU screen
(`STY` set, no `TMUX`) they are not used. `tests/render_replay_test.cpp` replays the bytes of
thousands of random frames into a model of a terminal (`tests/vt_model.hpp`) and checks the
screen against each frame, cell by cell.
