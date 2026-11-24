# rolltui guide

This guide covers building terminal programs with rolltui. Read the first three sections, then the
rest as you need it. Every function is documented in `rolltui.h`.

- **Getting started**: [How the parts fit](#how-the-parts-fit) · [A first program](#a-first-program) · [Testing a program](#testing-a-program) · [Embedding rolltui](#embedding-rolltui) · [Ownership](#ownership) · [Strings](#strings)
- **Screens**: [Layout files](#layout-files) · [The layout at run time](#the-layout-at-run-time) · [Theme files](#theme-files) · [Cut text, footers, strikethrough and focus](#cut-text-footers-strikethrough-and-focus) · [Hover](#hover)
- **Keys**: [Bindings files](#bindings-files) · [Key sequences and counts](#key-sequences-and-counts) · [The keys sheet](#the-keys-sheet) · [The theme sheet](#the-theme-sheet)
- **Widgets**: [Lists, tables and trees](#lists-tables-and-trees) · [The editor](#the-editor) · [Menu files](#menu-files) · [Charts and dashboards](#charts-and-dashboards) · [Progress](#progress) · [Messages](#messages) · [Dialogs](#dialogs) · [Pictures](#pictures) · [The column browser](#the-column-browser) · [Previews](#previews) · [Mermaid diagrams](#mermaid-diagrams) · [Syntax highlighting](#syntax-highlighting) · [File info row](#file-info-row)
- **Running**: [The run loop](#the-run-loop) · [Inline mode](#inline-mode) · [In a web page](#in-a-web-page) · [Work off the UI thread](#work-off-the-ui-thread) · [Other programs](#other-programs) · [File watching](#file-watching) · [The clipboard, the title and the terminal](#the-clipboard-the-title-and-the-terminal) · [Diagnostics](#diagnostics)
- **The terminal**: [Terminal detection](#terminal-detection) · [What a frame sends](#what-a-frame-sends)

# Getting started

## How the parts fit

A rolltui program is built from a handful of objects:

- `RolltuiContext` holds the program's key bindings, menus, help and the content kinds its layouts
  can use. `rolltui_context_set_library_defaults` fills it with the library's own. Everything else
  is created for a context.
- `RolltuiTerminal` is the terminal. It is queried for what it supports ([Terminal
  detection](#terminal-detection)) and sent the changes each frame. `rolltui_run` creates one and
  runs the event loop for you.
- `RolltuiLayout` is a screen read from a [layout file](#layout-files): its windows, how they share
  the space, and any popups.
- `RolltuiWindowStack` is the live state of the layout's layers. It tracks focus, sends each key
  and mouse event to the right window, and composes the frame: borders, titles, and the inside of
  each window.
- `RolltuiWindows` creates a widget for each window from its `content`. A window with
  `list:files` shows a list whose rows your program supplies under the name `files`; the list
  keeps track of its own cursor, scroll position, marks and filter.
- The theme is an array of `RolltuiStyle`, one per role (background, title, selected row, error
  and so on). A [theme file](#theme-files) gives the colours for a light and a dark variant; the
  library picks the variant and reduces the colours to what the terminal can show.
- `RolltuiThemeStore`, `RolltuiBindingsStore` and `RolltuiLayoutStore` keep the user's own copies
  of themes, key bindings and layouts next to the built-in presets. The program's settings screens
  and `rolltui-studio` edit them.

Each frame, the stack lays the windows out on the screen, each window's widget draws into its area,
and then anything on top is drawn (notifications, a status line). Each event goes from the stack to
a window, the window's widget gets the first chance to handle it, and whatever it doesn't handle
goes back to your program. [A first program](#a-first-program) shows all of this in about 140
lines.

## A first program

`examples/hello.c` is a complete program: a screen loaded from a layout, a list the program fills,
a page of text, a notification when you pick a row, and q to quit. Build it with
`cmake --build build --target rolltui-hello`.

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

Some notes on how it works:

- The layout has two windows. `list:fruit` gets its rows from `fruit_count` and `fruit_row`, and
  `text:` shows the text written in the layout.
- `render` brings the windows up to date with the stack (a new window gets its widget here), lays
  them out, composes the frame and draws notifications on top. It returns how long to wait before
  the next frame: -1 means until the next event, otherwise the time until a notification needs to
  move.
- `event` routes each event. The focused list handles Up, Down and Enter itself, and Enter on a
  row produces a list event that the program reads afterwards. Keys no window handles come back to
  the program.
- `main` hands everything to `rolltui_run`, which restores the terminal however the program ends.

## Testing a program

`rolltui_run_begin_headless(w, h, &app, &result)` runs your program's real event loop at a given
size without a terminal. Output goes nowhere, colours are 24-bit, and nothing in the environment
changes how it behaves. A test then steps the loop, sends it keys and reads the screen back as text.
This is the test for the first program:

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

- Defining `HELLO_NO_MAIN` leaves out the program's `main`, so the test runs exactly the app the
  program runs.
- Some things take a few frames to appear, like a notification sliding in. The test keeps stepping
  until the text shows up, with a time limit.

The other calls a test needs:

```c
RolltuiRun* run = rolltui_run_begin_headless(80, 24, &app, &result);
rolltui_run_step(run);                      /* draw the first frame */
rolltui_run_inject(run, &key);              /* send a key, a mouse event, a paste or a focus change */
rolltui_run_step(run);
rolltui_run_screen_text(run, &text);        /* the screen, one line per row */
rolltui_run_resize(run, 100, 30);           /* reported at the next step like a real resize */
rolltui_tasks_wait_idle(rolltui_run_tasks(run), 2000); /* wait for background work */
rolltui_run_finish(run);
```

On Windows, where input only comes from a console, `rolltui_run_begin_headless` returns NULL with
`ROLLTUI_RUN_NOT_A_TERMINAL`.

### Recordings

`rolltui_terminal_record(term, path, len)` writes everything sent to the terminal from that point
on, with timestamps and size changes, to an asciicast v2 file. asciinema plays it, and its tools
can turn it into a GIF. Setting `ROLLTUI_RECORD=demo.cast` records any program from the moment it
starts, so the same script that drives a headless test can produce a demo. A recording started
partway through only gets what later frames send; call `rolltui_run_invalidate` first to make the
next frame a full one.

The log has the terminal's replies and how long each frame took; see [Diagnostics](#diagnostics).

## Embedding rolltui

Put the repository in a folder named `rolltui` (a submodule or a copy), add it with
`add_subdirectory()`, and include `rolltui/rolltui.h`. The `CMakeLists.txt` only calls `project()`
and sets the C++ standard when it is the top-level project, so it works either way. When built on
its own, CMake creates the `rolltui/` include prefix in the build directory.

All includes inside the library are written as `rolltui/...`, and `rolltui-boundary-test` checks
that the library only includes its own headers. It links nothing but the platform's C and C++
libraries.

The tests use `testkit/`, a small harness in this repository. If your project already has a
`testkit` target, that one is used. rolltui only registers its tests when it is the top-level
project (`ROLLTUI_BUILD_TESTS`).

## Ownership

Every pointer in the API is one of:

- OWNED: has a single owner and is freed with the matching `_free` function.
- BORROWED: a pointer or view you are lent for a while; it never owns or frees anything.
- VALUE: copied.

There is no `shared_ptr`. `rolltui-ownership-test` checks the header against these rules.

## Strings

`RolltuiStr` is the library's string: owned, NUL-terminated, and allocated with the library's
allocator. `clear()` keeps the buffer so it can be refilled without allocating.

`rolltui/str.hpp` adds a header-only C++ toolkit on top. It doesn't add anything to `rolltui.h`.

| | |
|---|---|
| `StrView` | a borrowed view with `find`, `rfind`, `substr`, `starts_with`, `ends_with` and `compare` |
| `a + b + c` | concatenation into one buffer |
| `appendf` | formatted append |
| `StrVec` | a growable list |
| `path_base`, `path_dir` | the parts of a path |

Prefer these to converting back and forth with `std::string`.

# Screens

## Layout files

A layout file is JSON describing a screen: its windows, how they divide the space, and any popups.
Load one with `rolltui_load_layout_text(text, len, actions, n, hooks, &report)`. Unknown keys and
invalid values are listed in the report (`RolltuiLayoutReport`) and the rest of the layout still
loads. The studio's layout editor works on the same files.

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
| `root` | the top node of the screen (required) |
| `name` | the layout's name |
| `focus` | the id of the window that starts with focus |
| `popups` | popup layers, described below |
| `min_width`, `min_height` | the smallest screen it supports; below that a message is shown instead ([A screen too small](#a-screen-too-small)) |
| `inline` | `true` to draw below the shell prompt instead of on the alternate screen ([Inline mode](#inline-mode)) |
| `actions` | the program's own actions that key bindings can use, with descriptions: `{ "app.find": "find in the transcript" }` |

### A node

A node is a window or a split, depending on which of these keys it has:

| Key | The node is |
|---|---|
| `content` | a window showing `kind:source` (see below) |
| `tabs` | a window with tabs, `[{ "title": …, "content": … }]` ([Tabs](#tabs)) |
| `row`, `column` | a split, with its children side by side or stacked |
| `grid` | rows of cells, `[[node, …], …]`, or `{ "columns": N or "auto", "cell_height": rows, "cells": [ … ] }` |

Any node can also have:

| Key | Value |
|---|---|
| `id` | the node's name; a window without one is named after its content |
| `size` | its size within its split: cells (`30`), `"40%"`, `"fill"`, `"fill 2"`, or `"content"` to fit what it shows |
| `min`, `max` | limits on its size, in cells |
| `visible` | `false` hides it until the program shows it |
| `breakpoint` | `{ "width", "height", "pane_width", "pane_height" }`, to show it only when there is that much space ([Breakpoints](#breakpoints)) |
| `padding` | empty cells inside its border |
| `span` | `[rows, columns]` for a cell in a grid |

Windows can also have `title`, `footer`, `focusable`, `background` (the name of a theme role),
`border` (`none`, `single`, `rounded`, `double`, `heavy`, `hidden`, `quadrant-inside` or
`quadrant-outside`; see [Borders](#borders)) and, for a window with tabs, `tab` to pick the one shown
first. Splits can also have `gap`, `separators`, `stack_below` and `focus_weight` (see [Room and
spacing](#room-and-spacing)).

### Content

A window's `content` has the form `kind:source`:

| Kind | Source | Shows |
|---|---|---|
| `text` | the text itself | a page of text |
| `file` | a path | a file |
| `output` | a name your program binds | another program's output ([Coloured output](#coloured-output)) |
| `transcript` | a name your program binds | a document with Markdown, code and diagrams |
| `input` | the name its text is submitted to | a single- or multi-line text field ([The editor](#the-editor)) |
| `list` | a name your program binds | a list, table or tree ([Lists, tables and trees](#lists-tables-and-trees)) |
| `rows` | a name your program binds | label and value pairs |
| `chart` | a name your program binds | a chart ([Charts and dashboards](#charts-and-dashboards)) |
| `gauge` | a name your program binds | a progress bar ([Progress](#progress)) |
| `image` | a path, or a name your program binds | an image ([Pictures](#pictures)) |
| `menu` | a [menu file](#menu-files)'s name | a menu |
| `help` | a key scope, or nothing for all of them | the key bindings, editable ([The keys sheet](#the-keys-sheet)) |
| `keys` | none | the keys editor |
| `theme` | none | the theme editor ([The theme sheet](#the-theme-sheet)) |
| `filepicker` | the directory it starts in | a file picker |
| `messages` | none | the history of notifications ([Messages](#messages)) |
| `dialog` | the dialog's id | a dialog your program opens ([Dialogs](#dialogs)) |

Programs can add their own kinds with `rolltui_widget_kind_register` and
`rolltui_context_register_kind`. They can't replace the library's kinds.

### A popup

A popup is a separate layer with its own `root`, `focus` and `id`, plus its position on the screen:

| Key | Value |
|---|---|
| `dismiss` | required. `true` closes it when you click outside it; `false` keeps it open until it's answered |
| `modal` | `true` keeps focus in the popup, shades the screen behind it and ignores clicks outside it |
| `x`, `y`, `w`, `h` | position and size, in cells or `"N%"` of the screen; `w` and `h` default to the full screen |
| `anchor` | which point of the popup goes at (`x`, `y`): `top-left` (the default), `top`, `top-right`, `left`, `center`, `right`, `bottom-left`, `bottom`, `bottom-right` |
| `min_w`, `min_h`, `max_w`, `max_h` | size limits, in cells or `"N%"` |
| `clamp` | `false` lets it extend past the screen edge; by default it's moved to fit |
| `margin` | columns to leave free at the left and right edges when it's moved to fit |

## The layout at run time

The layout file sets up the screen; the window stack can change what is shown while the program
runs, addressing windows and splits by id. Each of these calls returns 0 if no node has that id.

### Borders

A window's `"border"` can be `none`, `single`, `rounded`, `double`, `heavy`, `hidden` (an invisible
border in the window's background colour that still takes up space and shows the title),
`quadrant-inside` (half blocks drawn just inside the edge, ▗▄▖) or `quadrant-outside` (▛▀▜). Where
ambiguous characters are wide, every style is drawn with `+ - |` instead.
`rolltui_draw_border(frame, scratch, rect, ROLLTUI_BORDER_*, line, title, n, title_style,
ambiguous_wide)` draws a border inside your own widget, for a group or a nested section.

A theme's `"glyphs": { "cursor": "caps" }` rounds the ends of the cursor row in lists, menus and the
column browser with a half block at each end (▐…▌). `"edges"` draws a bar at each end and leaves the
row's background alone, with the text in the cursor colour, so the terminal's background shows
through. `"fill"`, the default, draws the whole row in the cursor style. Lists keep a cell free on
each side for the ends, and menus draw them in their padding. `rolltui_cursor_text_style` and
`rolltui_frame_cursor_ends` do the same in your own widgets.

### Tabs

A window can have `"tabs"` instead of `"content"`, and shows one of them at a time:

```json
{ "id": "side", "focusable": true,
  "tabs": [ { "title": "Files", "content": "list:files" },
            { "title": "Log", "content": "output:log" } ] }
```

- The tab titles are shown in the window's top border, with the current one bold and underlined in
  the `title` role and the rest in the border colour. A window with tabs always has a border
  (`single` unless the file says otherwise; `"none"` is rejected). `"tab"` picks the tab shown
  first.
- Clicking a title switches to that tab. `stack.next_tab` (Ctrl-PageDown) and `stack.prev_tab`
  (Ctrl-PageUp) cycle through the focused window's tabs. `rolltui_window_stack_set_tab` switches
  from code, and `rolltui_window_stack_tab` returns the current tab and the number of tabs.
- Each tab's widget keeps its state while hidden, such as a list's cursor or a transcript's scroll
  position.
- Setting a new base layout keeps each window's current tab.

### Breakpoints

`"breakpoint": { "width": 100, "height": 30 }` on any node shows it only while the screen is at
least that size. Below that, the node takes no space and focus skips it. Either value can be
omitted. `"pane_width"` and `"pane_height"` work the same way but compare against the space the
node's parent has, so a column can drop out when its split gets narrow, whatever the screen size.

### Room and spacing

- `"min"` and `"max"` limit a node's size in cells. If a fill node is held at a limit, the space it
  doesn't use goes to the other fill nodes, by weight.
- `"size": "content"` makes a window just tall enough for what it shows plus one line, within its
  `min` and `max`, recalculated every frame. Fill windows share whatever is left. Lists, text and
  output windows support this.
- `"padding"` leaves empty cells inside a split's or a window's border. `"gap"` leaves cells between
  a split's children, and `"separators": true` draws a line in each gap.
- `"stack_below": 80` on a row lays its children out as a column while the row is narrower than 80
  cells, so two diffs side by side on a wide screen end up one above the other on a narrow one.
- `"focus_weight": 2` on a split gives that weight to whichever child has focus, so the focused
  panel grows as focus moves.
- `"grid": { "columns": 3, "cells": [ … ] }` places cells on three equal columns in reading order;
  a cell with `"span": [2, 2]` takes two rows and two columns. `"columns": "auto"` with
  `"cell_height": 3` uses as many columns as it takes to give every cell three rows, for example one
  cell per CPU core in one to three columns depending on the height.

`rolltui_layout_fits(layout, w, h)` checks whether a layout fits a screen, taking into account its
minimums, fixed sizes, borders, padding and gaps, and its `min_width` and `min_height`. Use it to
refuse a preset that wouldn't fit. `rolltui_window_stack_set_visible` likewise refuses to show a
node that would make the layout too big for the screen (`ROLLTUI_STACK_REFUSED`).

### Hiding, resizing and maximising

- `rolltui_window_stack_set_visible(stack, id, len, visible)` shows or hides a node. A hidden node
  takes no space.
- `rolltui_window_stack_set_size(stack, id, len, size, size_len)` resizes a node, using the same
  syntax as the layout file: `"30"`, `"40%"`, `"fill"` or `"fill 2"`.
- `rolltui_window_stack_maximize(stack, id, len)` makes one window fill its layer and gives it
  focus; passing NULL restores the layout. `stack.maximize` (Alt-Z) toggles this for the focused
  window, and `rolltui_window_stack_maximized` returns which window is maximised.
- `rolltui_window_stack_set_half(stack, id, len)` gives a window half the screen, with the rest of
  the layout in the other half: side by side on a wide screen, stacked on a tall one.
  `stack.half` (Alt-Shift-Z) toggles it for the focused window.
- `rolltui_window_stack_anchor(stack, rect)` places the top popup just below `rect` (or above it if
  there's no room), kept on screen. It's meant for things like a rename box under the file being
  renamed.
- `stack.focus_number` (Alt-1 to Alt-9) focuses a window by number, counting the focusable windows
  of the focused layer in order. `rolltui_window_stack_set_numbered(stack, 1)` shows each window's
  number before its title, and `2` shows it as a superscript ("²files"), falling back to plain
  digits where ambiguous characters are wide. `stack.focus_left`, `_right`, `_up` and `_down`
  (Alt-Shift-H, L, K, J) move focus to the nearest window in that direction.
- `rolltui_window_stack_set_busy(stack, id, len, 1, since_ms)` shows a spinner before a window's
  title while it's busy, animated from `since_ms`. Pass 0 to remove it.
- `rolltui_window_stack_set_border_role(stack, id, len, role)` draws a window's border in another
  role while it's in some state, such as searching, an error or an active filter. 0 goes back to
  the normal focus colours.
- `rolltui_window_stack_set_subtitle(stack, id, len, text, n)` shows text at the right of the top
  border, such as a live count or a mode.
- `rolltui_window_stack_set_title_parts(stack, id, len, parts, n)` builds the title from
  `RolltuiTitlePart`s, each in its own role. A part with an `action` is a clickable label, drawn
  underlined. Clicking it makes the route return `ROLLTUI_ROUTE_LABEL` with the window's id, and
  `rolltui_window_stack_pressed_label` returns the action once.

Visibility and size changes last until the next `rolltui_window_stack_set_base`. A maximised window
stays maximised under a new base if it's in it.

### Views in a window

`rolltui_windows_push_view(windows, window, len, content, clen, title, tlen)` shows a different
content in a window, with its own keys, for things like drilling down, a pager or a log. The
window's own content keeps its state underneath. The title shows the view titles after the
window's ("files › details"). Escape pops the top view once the view has nothing of its own to
back out of, and so does `rolltui_windows_pop_view`. `rolltui_windows_view_depth` returns how many
views are pushed.

When a click or key changes what is shown, `rolltui_window_stack_route` returns
`ROLLTUI_ROUTE_LAYOUT_CHANGED` with the window's id.

### A screen too small

When the screen is smaller than the layout's `min_width` by `min_height`, the stack shows a message
instead of the layout, "the screen is 40×6; this needs 60×8", or just "40×6 < 60×8" if the
sentence doesn't fit. The current size is bold in the `error` role and the required size bold in
the `ok` role (`RolltuiLayoutRoles`). A layout with no windows at all shows the program's key
bindings instead of a blank screen. Clicks are ignored, but keys still go to the focused window, so
a quit key keeps working. A program with a layout for small screens can switch to it first;
rolltui-chat and the studio switch to the built-in `stacked` layout.

The studio's layout editor edits tabs (typed as `Title = content | Title = content`), the starting
tab, breakpoints (typed as `width`, `width x height` or `x height`), and, under Spacing and bounds,
a node's min, max, padding, gap, separators, stack_below, focus_weight and grid span.

## Theme files

A role's `fg`, `bg` and `underline_color` accept any of these:

- `#rrggbb` or `#rgb`
- `#BW` for a grey, so `#80` is `#808080`
- `"R G B"` in decimal
- a number from 0 to 255 for a palette colour
- a colour name, `none`, or `default` for the terminal's own colour
- a name defined in the file's `defs`

The eight basic names (`black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`) are
palette entries 0 to 7, and `bright-red` and the others are 8 to 15. That way a theme for a
16-colour terminal follows the user's own palette. Every other W3C colour name means that colour
(`cornflowerblue`, `rebeccapurple`). A role can also be given as SGR parameters,
`"sgr": "1;38;5;208"`, the way another program's output would set it; `rolltui_style_from_sgr`
parses the same thing in code.

`"extends": "default"` bases a theme on a built-in one. The file then only lists the roles and
effects it changes.

`"gradients"` defines named colour ramps for meters, graphs and numbers:

```json
"gradients": { "cpu": { "start": "green", "mid": "#ffd700", "end": "#d03030" } }
```

`rolltui_theme_gradient(colours, mode, "cpu", 3, &gradient)` looks one up by name, and
`rolltui_gradient_at(&gradient, t, &colour)` returns its colour at `t` between 0 and 1, which is
how a value gets coloured by where it falls. A gradient without `end` is a single colour; without
`mid` it has two stops.

`rolltui_colors_distinct(ground, n, 4.5, colours)` generates `n` colours with distinct hues that
all have at least 4.5:1 contrast against `ground`, for things like chat authors or chart series.

## Cut text, footers, strikethrough and focus

`rolltui_frame_put_text_cut(frame, scratch, x, y, text, len, style, max_cells, where,
ambiguous_wide, link)` draws text in at most `max_cells`, with an ellipsis where it was cut.
`where` is `ROLLTUI_CUT_END` ("a long na…"), `ROLLTUI_CUT_MIDDLE` ("~/src/r…main.c", which keeps
both ends of a path) or `ROLLTUI_CUT_START` ("…/c/main.c"). Text that fits is drawn as is. The
ellipsis takes one cell, or two where ambiguous characters are wide, and wide characters are never
split. `rolltui_text_cut` returns the cut text instead of drawing it. Menus, lists and the column
browser all cut text this way.

`rolltui_text_cut_with` and `rolltui_frame_put_text_cut_with` take a `RolltuiCutOptions` with:

- `marker`, a replacement for the ellipsis (such as `"~"`)
- `keep_extension`, to keep a file name's extension after the cut ("a_long_rep….pdf")
- `split`, the percentage of the space a middle cut gives to the start

`rolltui_path_shorten` shortens each directory of a path to its first letter, from the left, until
it fits ("~/g/r/c/frame.c"; a leading dot is kept, so ".config" becomes ".c"). If that isn't
enough it cuts the file name in the middle and keeps its extension.

`rolltui_format_size(n, flags, out, cap)` formats a size the way the column browser and previews
show it: steps of 1024 (K, M, G, T, P, E), one decimal below 10 and none from 10 ("1.5K", "12M",
"512"), and a value that rounds up shown in the next unit ("1.0M"). Flags:

- `ROLLTUI_SIZE_DECIMAL` for steps of 1000 (k, M, G…)
- `_BITS` to show a byte count as bits ("12Mb")
- `_RATE` to add "/s"
- `_BYTE_UNIT` to add the unit below the first step ("512 B")

`rolltui_format_count(n, sep, len, out, cap)` groups thousands with a comma, or with `sep`
("12,345,678").

`rolltui_frame_put_spans(frame, scratch, rect, spans, n, align, overflow, ambiguous_wide)` lays out
a line of `RolltuiTextSpan`s, each with its own style. `align` is one of `ROLLTUI_ALIGN_*`. When
the line is too wide, `overflow` (`ROLLTUI_OVERFLOW_*`) either clips it, cuts it with an ellipsis
in the style of the span being cut, or wraps it at word breaks onto the following rows. It returns
the number of rows used. `rolltui_hotkey_at(label, len, ch, &n)` finds where a key's letter appears
in a label so it can be highlighted: at the start of a word if possible, otherwise its first
occurrence.

A window's footer sits at the right of its bottom border. It comes from `"footer"` in the layout
file, or from the widget's `footer` callback each frame. A list shows its position there ("12 of
340") when it has more rows than fit or a filter is active, along with the number of marked rows.
A footer too long for the border loses its start, since the end is the informative part.

A selection is drawn dimmer while focus is elsewhere. The cursor row of a list, a menu or the
column browser uses the `selection` role while its window is unfocused
(`RolltuiMenuRoles.selected_away`; 0 keeps the `selected` look).

A style's `strike` draws a line through the text (SGR 9). Theme files set it per role with
`"strikethrough": true`, the theme editor has it next to bold and italic, and all the built-in
themes use it for Markdown's `~~struck~~` text (`md_strikethrough`). Terminals without
strikethrough show the text plain.

Styles also have `blink` (1 slow, 2 fast), `hidden` and `overline`, and underlines can have a shape
and a colour of their own. `underline` is 0 or a `ROLLTUI_UNDERLINE_*` value (single, double,
curly, dotted, dashed), and `underline_color` sets its colour, or none to use the text colour. In a
theme file these are `"underline": "curly"`, `"underline_color": "#d04040"`, `"blink": true` and
`"overline": true`. Underline shapes and colours are only sent to terminals that support them:
kitty, Ghostty, WezTerm, foot, iTerm2, Alacritty, VS Code, mintty, Windows Terminal and VTE 0.52
or later, identified by their XTVERSION reply or their environment. Other terminals, and any
terminal behind a multiplexer, get a plain underline in the text colour.

`rolltui_style_patch(&base, &over)` layers one style over another: `over`'s colours where it has
them, its underline shape and colour if it has one, and its other attributes added to the base.
`rolltui_frame_tint` applies a patch to every cell in a rectangle, to dim a row or highlight a match.

## Hover

With `RolltuiTerminalOptions.hover` set, the terminal also reports the pointer moving with no
button pressed (mode 1003). It needs `mouse`, which is on by default. Whatever a click at the
pointer would act on is drawn in the theme's `hover` role:

- menu rows, options in an open dropdown, and a menu's ▲ and ▼ scroll markers
- entries in the column browser
- the "N more" marker in text and file windows
- hints in a hint bar
- links, which are also underlined ([Links in a window](#links-in-a-window))

The keyboard cursor doesn't follow the pointer, and hovering sends no menu event. The cursor's own
row keeps its look when the pointer is on it. Headings and disabled items aren't highlighted, and
nothing under a modal popup (or a popup that closes when you click outside it) is.

The window stack works out where the pointer is from the mouse events it routes, and marks the
window a click would reach: `RolltuiResolvedNode.hover`, with the pointer's cell in `hover_x` and
`hover_y`. Your own widgets can read these in `draw`. For things drawn outside the windows,
`rolltui_window_stack_pointer` gives the pointer's cell and `rolltui_hint_bar_hover` highlights a
hint bar. `rolltui_hint_bar_add_scope(bar, bindings, "app", 3)` fills a bar with a scope's actions,
each with its first key the terminal can send. If the bar is too narrow, hints are left out;
`rolltui_hint_bar_set_priority(bar, action, len, rank)` decides which go first, and the rest keep
their order.

Some limits come from the terminal:

- Nothing is highlighted until the terminal first reports the pointer moving. A click alone doesn't
  say where the pointer is afterwards.
- Terminals don't report the pointer leaving the window, so the last highlight stays until the
  pointer comes back.

The built-in themes make `hover` a lighter version of the panel colour. At 16 colours that
difference disappears, the same way the panel colour merges with the background; `mono` shows
hovered rows in bold.

dirktui, `rolltui-chat`, paint and the studio all turn hover on.

# Keys

## Bindings files

A bindings file maps actions to keys. The library ships `default` and `vim`. The keys sheet (F1)
and the studio edit the user's own copy, which the bindings store keeps.

```json
{ "name": "mine", "bindings": {
    "list.first": ["home", "g g"],
    "app.save": ["ctrl+s", "ctrl+x ctrl+s"],
    "app.quit": [] } }
```

- `bindings` maps each action to a list of keys. A key is a chord with modifiers in front (`ctrl+`
  or `control+`; `alt+`, `option+` or `meta+`; `shift+`; `super+`, `cmd+` or `win+`), or a sequence
  of up to four chords separated by spaces ([Key sequences and counts](#key-sequences-and-counts)).
  An empty list leaves the action unbound.
- Actions are named `scope.name`. The library has scopes such as `input`, `list`, `menu`,
  `transcript`, `stack` and `sheet`; a program's own scope, usually `app`, is declared in its
  layout's `actions`.
- `name` is the file's name. `preset` is written by the store and records which preset an edited
  copy came from.
- Library actions a file doesn't mention get their keys from the default preset. Keys the loader
  can't parse are listed in its report.

## Key sequences and counts

A bindings file can bind a sequence the same way as a single chord: up to `ROLLTUI_SEQUENCE_MAX` (4)
chords separated by spaces, as in `"list.first": ["home", "g g"]` or
`"app.save": ["ctrl+x ctrl+s"]`.

- The window table keeps track of a sequence in progress for the focused window. A key that starts
  a sequence in its scope waits for the next one; a key that doesn't continue any sequence ends it
  and is handled as it would have been on its own. Escape cancels a sequence in progress.
- Your program's own scope (`app`) gets the same handling from `rolltui_windows_app_key(windows,
  event, "app", 3, &action, &len)`, called for each key before it's routed to a window. It returns
  DONE with the action when a chord or sequence completes, PREFIX when the key is part of a
  sequence in progress, and NONE when the key should be passed on. A sequence that started in a
  window is left to that window.
- The focused window comes first. Keys it handles come back as NONE for your scope: keys its own
  scope binds (Ctrl-E for end of line, Ctrl-P for the previous row), anything typed into it, and
  every key while an editor sheet is waiting for one. Lists and inputs only take Escape while they
  have a filter, marks or a selection to clear. Widget kinds report this through the plugin's
  `takes` callback, and if you match keys yourself outside `rolltui_windows_app_key`, check
  `rolltui_windows_focused_takes` first. In practice your program's keys should be ones no window
  scope uses; the built-in tables use Alt-O for the file picker and Alt-P for the palette.
- The first chord of a sequence can't also be bound on its own in the same scope, and two actions
  can't share a sequence. The loader reports the conflict and keeps the first binding.
- A count is a number typed before an action: `5` then Down moves down five. For actions that go to
  a place, the count is the place: `5` then `list.last` (End, or `G` with the vim keys) goes to the
  fifth row, and `50` then `list.percent` (`%`) goes halfway down. Digits only count in windows
  where they aren't text or a jump: lists (unless the filter is being typed) and scrolling pages.
  The column browser, menus and inputs don't take counts.
- `any` as the last key of a sequence matches whatever character is typed there. With
  `"app.mark": ["m any"]`, m followed by a letter completes the action, and
  `rolltui_windows_any_key(windows, &chord)` returns the letter once `rolltui_windows_app_key`
  returns DONE. `any` can't be used on its own or first.
- `rolltui_windows_ask_key(windows, action, len, prompt, len)` asks for a single key. The panel
  shows the prompt, the next key makes `rolltui_windows_app_key` return DONE with `action`, and
  `rolltui_windows_any_key` returns the key. Escape cancels.
- When a sequence has been in progress for 300 ms, `rolltui_windows_draw_toasts` shows a panel at
  the bottom left with the keys so far in its border and a row for each key that can follow, in
  key order, with its action or a note that more keys follow. Rows that don't fit in one column
  flow into more columns, and anything that still doesn't fit ends with "… N more".
  `rolltui_windows_pending_keys` returns the keys so far as text, for a status line.
- A widget kind takes part by answering the plugin's `keys` callback with its scope and whether
  digits count. A completed sequence is delivered as a chord bound to the same action, or through
  `perform` with the action's name when only a sequence is bound.

Chords are case-insensitive, so a capital letter is written `shift+g`. Terminals send a typed
capital as the letter itself, and lookups treat it as shift plus the letter. Help shows a bare
letter as typed (`g`) and shift plus a letter as the capital (`G`).

The `vim` preset is the default table plus j and k, h and l, `g g` and `G`, Ctrl-D and Ctrl-U in
scrolling pages (in lists and the column browser both presets move half a page with them), H, M and
L for the top, middle and bottom of a list's view, and Ctrl-E and Ctrl-Y to scroll it. In the
column browser these letters are bound as keys, not used to jump to a name.

### Vim modes in text fields

The `vim` preset gives every text field vim's modes, by binding `input.normal_mode` to Escape. The
default preset leaves it unbound, so fields there have no modes.

- Insert mode works like any text field, with a bar cursor. Escape switches to normal mode and
  moves the cursor back onto the previous character.
- In normal mode printable keys don't type anything. They're commands in the `normal` scope (or
  nothing) and never reach the program around the field. The cursor is a block on a character.
  Sequences (`d w`) and counts (`3 w`) work in this scope, and the arrow keys, Enter and the
  field's Ctrl keys behave as in insert mode.
- Escape in normal mode is passed on, so a popup containing the field closes. Pressing Escape twice
  from insert mode closes the popup.
- `v` starts visual mode, selecting from where it started to the cursor, both ends included.
  Motions extend it, and `y`, `d` (or `x`) and `c` act on it. Escape or `v` returns to normal mode.
- `R` overwrites text (replace mode, with an underline cursor), and `r` followed by a character
  replaces the one under the cursor.
- Text removed by `x`, `d` and `c`, or copied by `y`, goes into the field's register. `p` pastes it
  after the cursor and `P` before.
- Outside insert mode, a field with a border shows its mode in the bottom border: NORMAL, VISUAL or
  REPLACE. `rolltui_input_mode` returns it.

| Keys | Action |
|---|---|
| `i`, `a`, `I`, `A` | insert before the cursor, after it, at the start of the line, at the end |
| `h`, `l`, `w`, `b`, `e`, `0` or `^`, `$`, `j`, `k` | move by character, to the next word, back a word, to the end of a word, to either end of the line, by line |
| `x`, `X`, `d w`, `d b`, `d d`, `D` or `d $`, `d 0` | delete the character, the one before, to the next word, back to the start of the word, the line, to the end of the line, to its start |
| `c w`, `c c` or `S`, `C` or `c $`, `s` | change to the end of the word, the line, to the end of the line, the character |
| `y y` or `Y`, `y w`, `p`, `P` | copy the line, copy to the next word, paste after, paste before |
| `u`, Ctrl-R | undo, redo |
| `v`, `r`, `R` | visual mode, replace one character, replace mode |

In the column browser, typing a letter jumps to the next name starting with it, ignoring case and
accents. Letters typed within a second of each other are treated as the start of one name, so `av`
gets to `avocado.txt` past `alpha`.

Bindings files can also use F13 to F24 and Super (`super+x`, or `cmd+x`). Only terminals with the
kitty keyboard protocol send these; elsewhere they're reported as undeliverable.

With the kitty keyboard protocol, the library asks the terminal to report each chord unambiguously
(disambiguate) and to include the key's position on a US layout (alternate keys). A chord with no
binding is then matched again by that position, so Ctrl plus the key that is C on a US keyboard is
still `ctrl+c` with a Cyrillic or Greek layout, on its own or in a sequence. Typed text still comes
through as text, since the library doesn't ask for every key as an escape sequence. The terminal's
settings are restored when the program exits, is suspended or is stopped.

Inside tmux, keys with modifiers only reach the program if tmux's `extended-keys` option is on. The
library reads the option but never changes it, and `dirktui check-terminal` tells you when it's off.

## The keys sheet

`keys` and `help` windows show the same sheet: every key the program has, grouped by scope, with
the program's own (`app`) first. Each row describes the action and shows its keys next to it in the
shortcut role. The line below the rows shows the selected action's name as it would appear in a
bindings file, its full description if the row is cut off, and any problem with its keys.
`help:<scope>` shows a single scope.

- `rolltui_windows_set_bindings_store(windows, "keys", 4, store, persist)` sets where edits go,
  normally the program's bindings preset store. Every keys sheet on screen, `help` included, then
  edits that store. Without a store the sheet is read-only and says so.
- Enter on a row records the next key pressed as an extra key for that action; Escape cancels.
  Delete removes the row's last key. Ctrl-Z and Ctrl-Y undo and redo, Ctrl-O loads a preset,
  Ctrl-S saves the current keys as a preset, and Ctrl-R reverts to the preset that was loaded.
  These are the `sheet.*` actions shared by all the editor sheets, and they can be rebound like any
  other action.
- Edits go into the store's working copy, which the footer labels "(modified)" until it matches its
  preset again. Only the user's changes are saved; the keys a program suggests for its own actions
  stay with the program.
- When one of the program's own keys is also used by another scope, both rows are marked and each
  names the other. The editor sheets' own keys aren't marked, since they only apply while the sheet
  has focus.
- The text given to `rolltui_context_set_help` appears as the sheet's first section, "About", and
  its note as the last one, "More".
- `/` filters the sheet like any list.
- `rolltui_windows_set_keys_editor_show_presets(windows, "keys", 4, 0)` removes the load and save
  keys, for a program with only one table. Editing, undo and reset still work.

When a bindings file doesn't mention a library action, the action gets the default preset's keys,
minus any that its scope already uses for something else. Saved files list every action, with
unbound ones as `[]`, so an action missing from a file is one that was added after the file was
saved.

## The theme sheet

A `theme` window shows the whole theme as a sheet. Roles are grouped under the part of the screen
they colour, and each row describes the role in that role's own colours, followed by its foreground
and background (each after a swatch) and its attributes. The footer shows the preset, the variant
and how many problems the theme has.

- `rolltui_windows_set_theme_store(windows, "theme", 5, store, persist)` sets where edits go.
- Enter edits the row in place. Type a colour in any form a theme file accepts (`#rrggbb`, a name,
  0 to 255, `none`) or step through the theme's colours with Up and Down. Left and Right move
  between the foreground, background and attributes, and Space toggles an attribute. The whole
  screen updates as you edit. Enter keeps the change and Escape reverts it.
- Rows whose colours are too close to read are marked, with a suggested fix. Ctrl-F (`theme.fix`)
  applies it and Ctrl-Z undoes it.
- Ctrl-D (`theme.variant`) switches to editing the other variant, light or dark. Ctrl-G
  (`theme.generate`) generates a theme from the next seed. Undo, load, save and reset are the
  `sheet.*` keys shared by all the editor sheets.

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
