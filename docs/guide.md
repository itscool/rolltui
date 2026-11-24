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
/* examples/hello.c: a minimal rolltui program. It loads its screen from a layout, fills a list, shows a page of text, posts a
 * message when you pick a row, and quits on q. docs/guide.md quotes this file, and tests/hello_test.c runs it headless. */
#include <stdio.h>
#include <string.h>

#include "rolltui/rolltui.h"

/* The screen layout. A real program would normally load this from a file; it's inline to keep the example to one file. */
static const char kLayout[] =
    "{ \"name\": \"hello\", \"resizable\": true, \"focus\": \"fruit\", \"root\": { \"row\": [\n"
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
/* hello_test.c: an end-to-end test of examples/hello.c with no terminal. It runs the same app, sends it keys and checks the
 * screen as text. docs/guide.md quotes this file. */
#define _POSIX_C_SOURCE 200809L /* nanosleep */
#define HELLO_NO_MAIN
#include "rolltui/examples/hello.c"

#include <time.h>

#include "testkit/testkit.h"

/* Whether `text` appears anywhere on the screen. */
static int shows(RolltuiRun* run, const char* text) {
  RolltuiStr screen;
  int found;
  memset(&screen, 0, sizeof screen);
  rolltui_run_screen_text(run, &screen);
  found = screen.p && strstr(screen.p, text) != NULL;
  rolltui_str_free(&screen);
  return found;
}

/* Keep stepping until `text` appears, for at most `ms`. Some things, like a message sliding in, take a few frames. */
static int shows_within(RolltuiRun* run, const char* text, int ms) {
  const struct timespec frame = {0, 20 * 1000000};
  int waited;
  for (waited = 0; !shows(run, text) && waited < ms; waited += 20) {
    nanosleep(&frame, NULL);
    rolltui_run_step(run);
  }
  return shows(run, text);
}

/* Press a key and draw the resulting frame. */
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
  "resizable": true,
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
| `resizable` | required. `true` lets the user resize panes by dragging the edge between them, `false` keeps the sizes the layout gives ([Resizing by dragging](#resizing-by-dragging)) |
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
| `resizable` | on a split, `true` or `false` to override the layout's `resizable` for its children |
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
| `count` | a name your program binds | a number in big digits ([Counts](#counts)) |
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

### Popups and the mouse

A click outside a popup closes it when `dismiss` is true, and is ignored when it's modal. Holding
Alt makes the click reach the window under it instead. The focus, the popup and the window's own
cursor all stay where they are. A link there opens, a list reports `ROLLTUI_LIST_EVENT_PEEK` with
the row's key, and the row is lit in the `menu_selected` role for 200 ms. Your program needs no
code for this beyond passing delivered events to `rolltui_windows_handle`. A program could use it
to show details of a file behind a dialog, or to add the clicked row to what the popup is editing.

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
- `stack.focus_next` and `_prev` (Tab and Shift-Tab) move focus through the focusable windows of
  the focused layer in order. A window whose `takes` callback claims the key keeps it instead, as a
  text field with a completer does.
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

### Resizing by dragging

A layout with `"resizable": true` lets the user resize panes with the mouse. Pressing on the edge
between two panes of a split (the border either pane draws there, or the gap between them) and
dragging moves the edge; releasing lets go. Where splits are nested, the edge belongs to the
innermost split it separates. While the pointer rests on an edge, and while it's being dragged, the
edge is drawn bold in the active border colour.

Each pane keeps the kind of size it had. A size in cells stays in cells and a percentage stays a
percentage. Two fill panes keep filling, with their weights changed to the new proportion, and any
other fill panes in the split keep their share. A `"content"` size becomes the number of cells it
was dragged to. Neither pane can go below its `min`, or below one cell inside its border, or above
its `max`.

A layout must state `"resizable"`, because whether panes should move is a decision about that
particular screen. A layout without it is reported and loaded as `false`. A split can say
`"resizable"` itself to differ from the layout. Like other changes made through the stack, dragged
sizes last until the next `rolltui_window_stack_set_base`. To keep them, read the base layer back
with `rolltui_window_stack_layer(stack, 0)` and save it with the layout. The studio's layout editor has the layout's setting under This screen and a split's
own under Spacing and bounds.

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

A `list:<source>` window shows data your program holds. Bind it with
`rolltui_windows_bind_list(windows, name, len, count, row, ctx, free_ctx)`. `count(ctx)` returns
the number of rows, and `row(ctx, i, out)` fills in row `i`: its `key`, its cells
(`rolltui_list_row_cell`), an optional `role` for the whole row, `depth` for a tree, and `busy`
with `busy_since_ms` to show a spinner at the end of the row.

- `rolltui_list_row_span(out, cell, begin, end, role)` draws bytes `begin` to `end` of a cell in a
  different role, on top of the row's style. Use it to colour a permissions string letter by
  letter, or a branch name inside a log line. A row can have up to `ROLLTUI_LIST_SPAN_MAX` spans.
  The colours come from the theme, so spans work in every theme including mono; if a script's
  output has ANSI colours, your program maps them to roles.
- `gutter` is a single character shown in the mark column in `gutter_role`, for states like copied,
  cut or tagged. It replaces the ▌ that marks use.

How the list gets its data:

- A plain list only asks for the rows it draws, so a hundred thousand rows cost no more than a
  screenful. Sorting, filtering and trees read every row once, when the data or the query changes.
  Call `rolltui_windows_list_refresh` when your data changes; a change in the count is noticed
  automatically.
- Rows are identified by their key. The cursor, the marks and which nodes are open follow the key
  through a refresh, a sort or a filter, and the cursor's row keeps its position on screen when rows
  are inserted above it. A row without a key is identified by its index.
- An empty list shows "no rows", or "no rows match “…”" when a filter is active.
- `rolltui_windows_list_set_loading(windows, name, len, 1)` marks the rows as still loading. An
  empty list then shows a spinner and "loading…" (after 100 ms, so rows that arrive quickly never
  flash it), and the footer shows "loading…" while rows arrive. Set it back to 0 when they're all
  in.
- `rolltui_windows_list_freeze(windows, name, len, 1)` pauses one list. It keeps the rows it has
  while your data changes and shows "paused" in its footer; other windows keep updating. 0 resumes
  it.
- `rolltui_windows_draw_toasts` returns how soon any window needs another frame, for example while
  a drag is held past the edge of a list, before "loading" appears, or while a changed row is
  highlighted. Returning that value from `render` keeps those things moving.

`rolltui_windows_set_list_columns` turns the list into a table. The first column gets whatever
width the others leave. Other columns are either `width` cells or as wide as the widest cell shown
so far; that width only grows, so columns don't shift while scrolling. `align` right-aligns a
column, and `min_screen` only shows a column while the list is at least that wide. When the first
column would get narrower than 8 cells, columns are dropped from the right, except ones marked
`keep`, in which case the first column is cut instead. `max_share` limits a measured column to that
percentage of the width. `role` draws a column's cells in a specific role on plain rows (the keys
sheet uses this to draw keys in the shortcut role). A row with fewer cells than there are columns
stretches its last cell over the rest. Cells that don't fit end in an ellipsis. The header row shows
the column names in bold, with ▲ or ▼ on the sorted column.

`rolltui_sparkline_text(values, n, lo, hi, ascii, &out)` writes a sparkline as text, one cell per
value from ▁ to █, for a table cell or a status line. With `ascii` (for terminals where block
characters are wide) it uses `_.-~^`. Keeping the history of values is up to your program.

`rolltui_windows_set_list_options` sets:

- `tree`: rows nest by `depth`, in tree order with parents before their children. Guide lines show
  the structure, with ▾ and ▸ for open and closed nodes. Nodes shallower than `open_depth` start
  open and the rest closed (0, the default, starts them all closed). Enter opens and closes a node;
  with `activate_nodes`, Enter on a node activates it like any other row.
- `marks`: lets the user select several rows. Marked rows are bold in the `accent_1` colour with ▌
  in the mark column, so they're visible without colour too.
- `gutter`: uses the mark column for the rows' own characters, with `marks` off.
- `close_over`: a node with more children than this starts closed regardless of `open_depth`.
- `column_cursor`: in a table, Left and Right move a column cursor. The current column's title is
  drawn reversed and its cell on the cursor row underlined, and `list.sort` sorts by that column,
  then in reverse, then back to your program's order. `rolltui_windows_list_cursor_column` returns
  the current column, for copying or editing that cell.
- `reorder`: the user can move rows, and the list asks your program to do it (see below).
- `changes_ms`: rows that appeared or changed in a refresh are drawn bold, italic and in the `info`
  role for this many milliseconds (until the next refresh if there's no frame clock). Rows that
  disappeared are simply gone; if you want them to be seen leaving, keep them for a moment in a
  muted role.
- `fade`: each row is this many percent fainter for every row it is away from the cursor, up to 70.
  Only RGB colours fade, so 16- and 256-colour themes draw all rows the same.
- `line_numbers`: 1 numbers the rows; 2 shows the distance from the cursor row, which shows its own
  number.
- `scrolloff`: how many rows to keep between the cursor and the edge of the window. Beyond half the
  window, the cursor stays in the middle.
- `wrap`: moving past the last row goes to the first, and back. `RolltuiMenuOptions` and
  `RolltuiPickerOptions` have `scrolloff` and `wrap` too.
- `hide_header`: hides the header row.
- `headings`: with `tree`, rows at depth 0 are section headings, drawn bold with a line after the
  name. The cursor skips headings, headings are always open, and a filter keeps a heading as long as
  a row under it matches.

When the terminal draws ambiguous-width characters as two cells, the tree lines, open/closed
markers, sort arrows and marks fall back to ASCII.

### Keys and the mouse

| Action | Keys | What it does |
|---|---|---|
| `list.up`, `list.down` | Up, Down, Ctrl-P, Ctrl-N | previous or next row |
| `list.page_up`, `list.page_down` | PageUp, PageDown | a page up or down |
| `list.first`, `list.last` | Home, End | first or last row; with a count, that row |
| `list.half_up`, `list.half_down` | Ctrl-U, Ctrl-D | half a page up or down |
| `list.view_top`, `list.view_middle`, `list.view_bottom` | Alt-Home, Alt-M, Alt-End | top, middle or bottom row in view |
| `list.scroll_up`, `list.scroll_down` | Alt-Up, Alt-Down | scroll by a row, keeping the cursor on its row while possible |
| `list.percent` | % | with a count, that percentage of the way down |
| `list.activate` | Enter | activate the row, or open or close a tree node |
| `list.back` | Escape | end a visual range, then clear the filter, then the marks |
| `list.mark` | Space | toggle the row's mark and move to the next row |
| `list.mark_up`, `list.mark_down` | Shift-Up, Shift-Down | mark from where the range started |
| `list.mark_all`, `list.unmark_all`, `list.invert_marks` | Alt-A, Alt-U, Alt-I | mark every visible row, none, or invert |
| `list.visual` | Alt-V | mark from here to wherever the cursor goes; press again to keep the range |
| `list.visual_swap` | Alt-; | jump to the other end of the range |
| `list.open`, `list.close` | Right, Left | open a node or move into it, close a node or move to its parent; with `column_cursor`, next and previous column |
| `list.open_deep` | Shift-Right | open the node and everything under it |
| `list.open_all`, `list.close_all` | Alt-Right, Alt-Left | open or close every node |
| `list.move_up`, `list.move_down` | Alt-Shift-Up, Alt-Shift-Down | with `reorder`, move the row |
| `list.sort` | Alt-S | sort by the next column, then back to your program's order |
| `list.reverse` | Alt-R | reverse the sort |
| `list.filter` | / | type a filter in the list's last row |

Clicking a row moves the cursor there and double-clicking activates it. Clicking in the mark column
marks the row, and dragging there marks every row it passes. Holding a drag past the top or bottom
keeps scrolling, a row every 50 ms. Clicking ▸ or ▾ opens or closes the node, clicking a column
title sorts by that column (click again to reverse), and the wheel scrolls.

A visual range marks every row from where it started to the cursor, on top of any existing marks.
Pressing the visual key again keeps what it marked, and Escape ends it and restores the marks as
they were. Shift-Up and Shift-Down add to the marks and the marks stay put; they're what your
program acts on, and moving with a plain arrow key doesn't clear them.

With `reorder`, Alt-Shift-Up and Alt-Shift-Down move the cursor row, and dragging a row moves it,
showing where it will land as the pointer moves. With `marks`, dragging a marked row moves it, and
dragging from an unmarked row marks a range instead. The list doesn't reorder your data itself. It
reports `ROLLTUI_LIST_EVENT_REORDER` with the row's `key` and `before`, the key of the row it should
now come before ("" for the end), and your program moves the rows and refreshes. Moving a marked row
means moving all the marked rows. The cursor follows the key. Sorted lists and trees don't allow
moving rows, since their order isn't the user's to change.

A filter is a list of terms separated by spaces, and a row is shown when every term matches within
one of its cells:

- a plain term matches fuzzily (`rolltui_fuzzy_match`)
- `'term` matches exactly
- `^term` matches at the start and `term$` at the end
- `/re/` is a regular expression
- `!term` excludes rows that match

Case and accents are ignored unless a term contains a capital letter. Matched text is drawn bold and
underlined. With each change to the query the cursor moves to the best match, and rows stay in
their original order. Enter keeps the filter and returns the keys to the list. Escape while editing
a kept filter restores it; otherwise it clears the filter. In a tree, the filter keeps the parents of
matching rows and shows them open. Menus filter the same way, with the selection on the best match,
and the command palette sorts its rows by match quality.

`ROLLTUI_LIST_SORT_TEXT` columns sort in natural order. `ROLLTUI_LIST_SORT_NUMBER` columns sort by
the number a cell starts with, reading K, M, G and T as powers of 1024 and s, m, h and d as ages;
cells without a number go last in both directions. Equal cells keep your program's order. A sorted
tree sorts each level separately and keeps every row under its parent, and with `headings` the
headings keep your order while the rows under them sort. To show chains of single children as one
row (`src/main/java`), give the list that row yourself, since only your program knows how the names
join.

`rolltui_windows_list_event` returns what happened, oldest first: `MOVED`, `ACTIVATE`, `MARKED`,
`SORTED`, `OPENED`, `CLOSED`, `FILTERED`, `REORDER` and `PEEK`, each with the row's key (or the
filter text for `FILTERED`). `PEEK` is an Alt-click on the row through a popup (see [Popups and
the mouse](#popups-and-the-mouse)). Other calls:

- `rolltui_windows_list_cursor` and `rolltui_windows_list_marked` return the keys to act on, and
  `_set_cursor` and `_set_filter` set them.
- `rolltui_windows_list_sort` and `_sort_state` set and read the sort.
- `rolltui_windows_list_mark_matching(windows, name, len, query, qlen, on)` marks or unmarks every
  row matching a filter query, visible or not, such as `/\.log$/` or `^tmp`.
- `rolltui_windows_list_reveal` moves to a row by key, opening its parents, and `_set_open` opens or
  closes a node.
- `rolltui_windows_list_perform` runs a list action by name, for your program's own menus.

In a popup, Escape clears the filter, then the marks, then closes the popup.

### Ordering and finding text

`rolltui_natural_compare(a, alen, b, blen)` compares text the way people read it. Runs of digits
compare by value, so `file2` comes before `file10` and `7` before `07`. Everything else compares
case-folded and without accents, with the original bytes as a tiebreaker. The column browser sorts
names this way.

`rolltui_text_find(hay, hlen, needle, nlen, from, &at, &len)` searches ignoring case and accents
unless the needle has a capital letter, and returns the byte range that matched.
`rolltui_text_find_each` reports every match. Find in the transcript works this way, and also
accepts `/re/` as a regular expression.

`rolltui_filter_new(query, len)` creates the same filter that lists and menus use, for your own
rows. `rolltui_filter_match` tests and scores a string, `rolltui_filter_term` tests a single term
(for a row with several cells, a term only has to match one of them), and `rolltui_filter_spans`
returns the byte ranges that matched so you can highlight them.

`rolltui_fuzzy_match(pattern, plen, text, tlen, &score, positions, cap)` checks whether the
pattern's letters appear in the text in order. Case is ignored unless the pattern has a capital,
and accents always are. Consecutive letters, matches at the start of a word and matches early in
the text score higher, so sorting by score puts the likeliest candidates first. `positions`
receives the byte offset of each matched letter, for highlighting.

## The editor

An `input:<target>` window is a text editor. Unless `single_line` is set, the text wraps and the
window grows to fit it. `RolltuiInputOptions` has the other settings:

- `wrap_words`: wrap after spaces, so words move to the next row whole. A word longer than the row
  is still broken at the edge.
- `wrap_column`: wrap at this many columns, or at the window edge if that's narrower.
  `rolltui_input_wrapped_text(in, column, out)` returns the text with a newline at each soft wrap
  (and the spaces before it removed), so a commit message typed at 72 columns comes back with the
  line breaks the user saw.
- `line_numbers`: show line numbers in a gutter, in the `text_muted` role. Continuation rows of a
  wrapped line have no number.
- `language`: a language name the syntax highlighter knows (`"c"`, `"json"`, a code fence's tag).
  The text is highlighted with the built-in grammars and the user's own.
- `auto_indent`: a new line starts with the same spaces and tabs as the line it was split from.
- `secret`: `ROLLTUI_INPUT_SHOW_MASKED` shows a `•` per character (`*` where `•` would be two cells
  wide), and `ROLLTUI_INPUT_SHOW_NOTHING` shows nothing and keeps the cursor after the prompt. A
  secret is never copied, never stored in the history or the kill buffer, and word motions jump over
  the whole line.
- `refuse_empty`: Enter does nothing while the text is only whitespace.
- `insert_typed` off: printable keys are looked up in the bindings like any other key and type
  nothing if unbound.

The editing keys are readline's. Each one is an action in the `input` scope, so it can be rebound:

| Action | Default | What it does |
|---|---|---|
| `input.kill_word_backward` | Ctrl-W, Alt-Backspace | cut the word before the cursor |
| `input.kill_word_forward` | Alt-D, Ctrl-Delete | cut the word after the cursor |
| `input.kill_to_line_start` | Ctrl-U | cut to the start of the line |
| `input.kill_to_line_end` | Ctrl-K | cut to the end of the line |
| `input.yank` | Alt-Y | paste what the last run of cuts removed |
| `input.transpose_chars` | Ctrl-T | swap the characters on either side of the cursor |
| `input.transpose_words` | Alt-T | swap the word before the cursor with the next one |
| `input.upcase_word` | Alt-U | uppercase to the end of the word, or the selection |
| `input.downcase_word` | Alt-L | lowercase the same way |
| `input.capitalize_word` | Alt-Shift-C | capitalise the same way |
| `input.overwrite` | Insert | toggle overwrite mode |

Consecutive cuts count as one, so cutting three words one at a time pastes back all three. Ctrl-Y
is redo here, which is why yank is on Alt-Y. Overwrite mode never replaces a newline. Case changes
use Unicode's simple mappings, so `é` uppercases to `É`. `rolltui_u_case` converts a whole string
the same way, and `ROLLTUI_CASE_FOLD` gives a case-folded form for comparisons.

`rolltui_input_perform(in, actions, name, len, now)` runs an editing command by action name, the
same as pressing its key. A modal editor's normal mode is a bindings table over these commands with
`insert_typed` off.

`rolltui_input_set_caret(in, byte, extend)` moves the cursor; calling it again with `extend` set
selects back to the first position. A rename box uses this to put the cursor before a file's
extension, or to select the name without it.

### Completion

`rolltui_input_set_completer(in, fn, ctx)` gives a field a completer. The field calls `fn` with its
text and the cursor's byte offset whenever either changes, and `fn` adds candidates, best first,
with `rolltui_completions_add(out, text, len, detail, detail_len)`. The detail is optional and is
shown beside the candidate in the list. A candidate replaces the word before the cursor unless
`fn` sets another byte range with `rolltui_completions_replace(out, begin, end)`.

Two helpers produce candidates. `rolltui_complete_words(out, word, len, words, n)` matches `word`
fuzzily against a list and puts the words that start with it first. `rolltui_complete_path(out,
word, len, dir, dir_len)` lists the folder named by `word` up to its last slash and matches the
rest against the names in it. A relative path starts from `dir`, or from the working directory
when `dir` is NULL, and `~/` starts from the home folder. Folders end in `/`, and names starting
with a dot are offered only once the word starts with one.

| Action | Default | What it does |
|---|---|---|
| `input.complete` | Tab | complete the word, or open the list and step through it |
| `input.complete_prev` | Shift-Tab | step back through the list |
| `input.accept_suggestion` | Ctrl-F | take the suggestion shown after the cursor |

When only one candidate starts with the word, Tab takes it. When several do and they agree past
the word, Tab extends the word that far. Otherwise Tab opens a list under the field, or above it
when there is no room below, and puts the first candidate in the word's place. Tab, Shift-Tab, Up
and Down then step through the list, replacing the word each time. Enter keeps the candidate
shown, Escape puts the original word back, and typing keeps the candidate and goes on typing. The
list keeps its width and position while it is open, and the one shown is in the `menu_selected`
role.

While the cursor is at the end of the text with nothing selected, the rest of the first candidate
that starts with the word is drawn after the cursor in the placeholder's colour, in italics.
Ctrl-F takes it, and so does Right.

A field takes Tab and Shift-Tab only while its completer has candidates for the word or the list is
open. Otherwise Tab moves the focus as usual. While it does take them, the focus leaves it with
Alt-1 to Alt-9, Alt-Shift with H, J, K or L, or a click. `rolltui-chat` completes its commands, setting names and values and preset names this way,
and dirktui's path line completes paths.

### History

Enter adds the submitted text to the input's history (newest last, duplicates kept once, at most
`history_limit` entries). Up and Down browse it from the first and last rows. With
`history_prefix` on (the default), they only recall entries that start with the text before the
cursor, so typing `git` and pressing Up finds the last `git` command. With the cursor at the start,
every entry matches.

`rolltui_input_history_load` and `rolltui_input_history_save` store the history in a file as a
JSON array of strings. Saving re-reads the file first and writes its entries plus the ones this
input added since it last loaded or saved, through a temporary file and a rename. Two instances
saving in turn keep both sets of entries, and a reader never sees a half-written file.
`rolltui-chat` keeps its history in its state directory, `<state>/chat/history.json`.

### What an app keeps between runs

`rolltui_state_dir(app, len, &out)` returns the directory for things an app remembers, as opposed
to settings the user configures. It is `ROLL_STATE_DIR/<app>`, or `ROLL_CONFIG_DIR/state/<app>`
under a scratch config directory, else `XDG_STATE_HOME/roll/<app>`, else
`~/.local/state/roll/<app>` (`%LOCALAPPDATA%\roll\<app>` on Windows). Nothing is created there
until something is saved.

A state file holds names and text values in a JSON object, for things like marks, tags, the tabs
that were open or the last directory visited. `rolltui_state_file_open(path, len, &err)` reads it
(a missing file counts as empty), `_get` and `_set` read and change a value (a NULL value removes
it), and `_count` and `_name_at` list the names. `rolltui_state_file_save(file, &err)` re-reads the
file, applies this handle's changes on top, and writes it through a temporary file and a rename.
Two instances saving in turn keep each other's names; when both changed the same name, the last
save wins. After a save, the handle holds what the file holds. A file that isn't a state file is
reported and never overwritten. dirktui keeps the result of its last update check here.

## Menu files

A menu file is a tree of items. A `menu:<name>` window shows one, and the studio's menu editor
edits them.

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
| `id` | what your program receives when the item is chosen; for an option, its value |
| `label` | the text shown |
| `kind` | `action` (the default), `submenu` (the default for an item with `items`), `toggle`, `choice`, `input`, `section`, `separator` |
| `items` | a submenu's items, or a choice's options |
| `action` | a bindings action that does the same thing; its current keys are shown next to the item |
| `shortcut` | keys to show for an item without an `action` |
| `description` | a longer description your program can show |
| `enabled` | `false` shows the item dimmed and makes it unselectable |
| `checked` | the state of a toggle |
| `value` | a choice's selected option, or an input's text |
| `dropdown` | show a choice's options in a dropdown over the menu |
| `popup` | open a submenu in a box over the current level |
| `swatch` | the value is a colour and is shown as a swatch |
| `type` | an input's type: `text`, `int`, `float`, `color`, `size`, `dim` or `name` |
| `min`, `max`, `step`, `precision` | a number's range, the step for the arrow keys, and its decimal places |
| `min_len`, `max_len`, `optional` | a text's length limits, and whether it can be empty |
| `validator`, `hint` | the name of a check your program registered, and a hint shown next to the field |
| `columns` | makes a level a table: `[{ "width": cells, "align": "left" or "right" }]` |
| `cells` | a row's text for each of its level's columns |

A section is a heading over the rows that follow it, and a separator is a horizontal line; the
cursor skips both. An item with both `action` and `shortcut` is reported as a problem, since the
keys for an action come from the bindings.

## Charts and dashboards

A `chart:<source>` window draws numbers your program supplies. Bind it with
`rolltui_windows_bind_chart(windows, name, len, fill, ctx, free_ctx)`. Before each frame, `fill`
gets the window's chart, already cleared, and sets:

- the kind, with `rolltui_chart_set_kind`: `ROLLTUI_CHART_LINE` (the default), `ROLLTUI_CHART_AREA`
  (a line filled down to the axis), `ROLLTUI_CHART_BARS`, `ROLLTUI_CHART_SPARK`,
  `ROLLTUI_CHART_METER` (one row per series, showing its latest value as a bar of ■) or
  `ROLLTUI_CHART_AUTO` (a meter on one row, a sparkline on two or three, a line on more)
- up to four series, with `rolltui_chart_set_series(chart, i, label, len, role, values, n)`: a name
  (optional), a role (0 to use the accent colours in order) and the values, which are copied
- the range, with `rolltui_chart_set_range(chart, lo, hi)`, if it should be fixed (for example 0 to
  100 for a percentage). Otherwise the range comes from the values, and bars and sparklines start
  at zero.

Lines are drawn in braille, two dots across and four down per cell, with each point joined to the
next so steep changes stay continuous. Bars are grouped by value with one bar per series, drawn in
eighth blocks. A sparkline is one row per series with one block per value. When there are more
values than fit, the newest are shown, and NaN leaves a gap. Range labels are on the left with the
maximum at the top, and series names are on the top row in bold, each in its colour. Braille is
narrow in every terminal; where ambiguous characters are two cells wide, the blocks fall back to
ASCII.

- `rolltui_chart_set_symbols` draws lines and areas in braille, quadrant blocks (two by two per
  cell) or shades (░▒▓█ by how full the cell is). Without it the chart uses the theme's
  `"glyphs": { "chart": "blocks" }`, or braille if the theme doesn't say.
- In an area chart each row covers a band of values: full above the value, empty below it, and the
  row containing the value filled up to it. Any value above the minimum shows at least one dot, and
  a bar at least the smallest block.
- `rolltui_chart_set_gradient(chart, i, &gradient)` colours series `i` from a gradient: lines and
  areas by height, bars and sparklines per value, and meters along their length.
- `rolltui_chart_set_flags` takes `ROLLTUI_CHART_FLOW` for a time series (one value per column,
  newest on the right, the left left empty until the chart fills up, never stretched) and
  `ROLLTUI_CHART_AXES` for axis lines on the left and bottom. With axes,
  `rolltui_chart_set_time(chart, newest_s, step_s)` labels the bottom axis with HH:MM every ten
  columns.
- `ROLLTUI_CHART_INVERT` hangs the chart from its top: lines, areas, bars and sparklines grow
  downward, the minimum is labelled at the top, the axis runs along the top and the series names
  go on the bottom row. Part of a hanging cell is drawn as the empty part's lower block in reverse
  video, since fonts don't have the upper eighth blocks. A meter fills from the right instead.
- `ROLLTUI_CHART_MIRROR` draws the first two series as a pair, like a network graph with download
  above and upload below. The first stands on the middle of the chart and the second hangs under
  it. Each half gets its own range unless one is set, and their range labels share a width so the
  columns line up. `ROLLTUI_CHART_MID` adds a rule between the halves. A mirrored chart with one
  series is drawn as a plain one, and the halves swap when the series are given in the other
  order.
- `RolltuiHistory` keeps a rolling window of values (`rolltui_history_new`, `_push`, `_resize`).
  `rolltui_chart_capacity(chart, cells)` says how many values a chart of that width shows, and
  `rolltui_chart_set_series_history` turns a history into a series.

To draw a chart in your own widget, use `rolltui_chart_new`, the same setters, and
`rolltui_chart_draw(chart, frame, scratch, area, styles, ambiguous_wide)`.

A dashboard is a `"grid"` in the layout file, made of rows of cells where each cell is a node:

```json
{ "grid": [ [ { "content": "chart:cpu" }, { "content": "chart:mem" } ],
            [ { "content": "list:procs" }, { "content": "output:log" } ] ] }
```

A grid is a column of rows, and it's saved back in that form. Grids with spans,
`{ "grid": { "columns": 3, "cells": [ … ] } }`, are described under [Room and
spacing](#room-and-spacing).

## Progress

A `gauge:<source>` window shows progress on one row: a label, a bar of eighth blocks in the
`accent_2` role, and a percentage. Bind it with `rolltui_windows_bind_gauge` and fill in a
`RolltuiGauge` each frame:

- `fraction` from 0 to 1. The percentage is rounded down, so 100% only appears when the work is
  done.
- A negative `fraction` means the amount is unknown, and the window shows a spinner and the label.
- `failed` draws the bar and percentage in the `error` role.
- `since_ms` is when the work started, which sets the spinner's phase.

In a narrow window the label is dropped before the bar. Where ambiguous characters are two cells
wide, the bar uses `#` and `-`.

`rolltui_gauge_draw` draws the same row in your own widget. `rolltui_spinner_draw` draws a spinner
in one cell: a static character marked `waiting`, which the theme's `waiting` effect animates. All
the built-in themes define `waiting`. A menu row whose action is running can set `busy` and
`busy_since_ms` to show a spinner after its label until your program clears it.

## Counts

A `count:<source>` window shows a number as a dashboard panel: a legend centred on the top row,
the number in seven-segment digits three rows tall, and optionally a second number and a trend
arrow. Bind it with `rolltui_windows_bind_count` and fill in a `RolltuiCount` each frame:

- `label` is the legend. It's in the `label` role, and in `border_active` while the window has
  focus.
- `value` is the number, in the `accent_2` role (the role `ok` messages use).
- `fault`, when it isn't negative, is a second number after a `⡇` divider, in the `error` role,
  for things like failed pods beside running ones.
- `digits` pads both numbers with zeros to at least that many digits. The padding is drawn in
  `text_muted`.
- `trend` is 1 for ↑ after the numbers, -1 for ↓, or 0 for none.

When the window is too short or narrow for big digits, it shows everything on one line instead.
`rolltui-tour`'s dashboard counts its running processes this way.

`rolltui_big_digits_draw(frame, x, y, text, len, style, leading, ambiguous_wide)` draws big digits
in your own widget, three rows tall from `(x, y)`. Each digit is three cells wide in heavy box
drawing, with a cell between characters. It also draws `-`, `.`, `:` and spaces, and other
characters plainly on the middle row. When `leading` isn't NULL, zeros before the first other
digit are drawn in that style. Where ambiguous characters are two cells wide, the digits are drawn
with `_` and `|`. `rolltui_big_digits_width(text, len)` returns the width.

## Messages

`rolltui_windows_post(windows, level, text, len, now)` shows a message. The levels are
`ROLLTUI_MESSAGE_INFO` (`accent_1`, `i`), `_OK` (`accent_2`, `✓`), `_WARNING` (`warning`, `!`)
and `_ERROR` (`error`, `✗`), so the level is clear without colour too.

- The window table keeps the last `ROLLTUI_MESSAGE_HISTORY` (500) messages, which
  `rolltui_windows_message_count` and `_message_at` read.
- Each message appears as a toast in the bottom-right corner for `ROLLTUI_TOAST_MS` (6 seconds),
  or twice that for errors. The newest is at the bottom, the toasts take at most a third of the
  screen, and the newest is always visible. Toasts slide in from the right unless motion is turned
  off (`still`).
- A `messages` window lists the history, newest at the bottom, and scrolls. If it's one row tall it
  shows the newest message while its toast would be showing, which makes it usable as a status
  line.

Draw the toasts last, after composing the frame:
`rolltui_windows_draw_toasts(windows, frame, screen, styles, now)` returns the time until they next
change, which `render` should take into account. Clicking a toast dismisses it;
`rolltui_windows_toast_event` returns 1 when an event was such a click, and your program should
stop handling the event there. `rolltui_windows_dismiss_toasts` clears them all.

`RolltuiWidgetEnv.still` means the user asked for no motion. Anything the library animates on its
own is drawn in its final position. Frame dumps are a single moment, so the programs here draw
them still, and dirktui sets it from its motion setting.

`RolltuiWidgetEnv.wheel_lines` sets how many lines one notch of the mouse wheel scrolls, in every
window that scrolls by lines and in the column browser's columns and preview (3 if 0). Menus move
one row per notch.

dirktui, paint and the studio show failures as error toasts. dirktui uses its own status-line note
for successes.

## Dialogs

A dialog asks the user a question and waits for the answer. It opens as a focused modal popup and
is as tall as its content, up to 90% of the screen.

```c
RolltuiDialog* d = rolltui_dialog_new(ROLLTUI_MESSAGE_WARNING, 0, "Delete 3 files?", 15,
                                      "They go to the Trash.", 21);
rolltui_dialog_add_item(d, "notes.txt", 9);
rolltui_dialog_add_button(d, "Delete", 6, 0);
rolltui_dialog_add_button(d, "Cancel", 6, ROLLTUI_DIALOG_DEFAULT | ROLLTUI_DIALOG_CANCEL);
rolltui_windows_ask(windows, stack, "delete", 6, d); /* the window table owns it now */
rolltui_windows_sync(windows, stack);
```

- The title goes in the border after the level's symbol, with the border and title in the level's
  role.
- The body wraps to the dialog's width. Items follow, one per line; if they don't fit they scroll,
  and the bottom border shows which ones are visible ("3–8 of 40").
- Buttons sit in a row at the bottom. `ROLLTUI_DIALOG_DEFAULT` marks the one selected initially
  (otherwise the first), and `ROLLTUI_DIALOG_CANCEL` the one the close key picks (otherwise the
  last). A dialog without buttons gets a single OK.
- `ROLLTUI_DIALOG_FULL_SCREEN` fills the screen, for things like an error page.
- `rolltui_dialog_set_phrase` adds a text field under the body. All buttons except cancel are
  disabled until the field contains the phrase exactly, and letters go into the field instead of
  choosing buttons.

| Action | Keys | What it does |
|---|---|---|
| `dialog.next`, `dialog.prev` | Tab, Right; Shift-Tab, Left | next or previous button |
| `dialog.answer` | Enter | choose the selected button |
| `dialog.up`, `dialog.down` | Up, Down | scroll the items by a row |
| `dialog.page_up`, `dialog.page_down` | PageUp, PageDown | scroll the items by a page |
| `dialog.copy` | Alt-C | give your program the dialog's text |

Each button can be chosen with a letter, the first letter of its label not used by an earlier
button, shown underlined. Clicking a button chooses it too, and the close key chooses the cancel
button.

After routing and delivering each event, call `rolltui_windows_answer(windows, stack, id, len,
&button, &copy)`. It returns:

- `ROLLTUI_DIALOG_ANSWERED`, with the button's index, once the dialog has closed. A dialog closed
  some other way (the close key, or your program popping it) answers with its cancel button. Each
  answer is reported once.
- `ROLLTUI_DIALOG_COPY` when `dialog.copy` was pressed, with the title, body and items in `copy`
  for `rolltui_clipboard_copy`. The dialog stays open.
- 0 while the dialog is still open, or if there's no dialog with that id.

Asking again with the id of an open dialog replaces it. A layout with a `dialog:<id>` window but no
dialog asked under that id reports it as a problem, like any unbound source.

## Pictures

`rolltui_image_load(path)` and `rolltui_image_decode(bytes)` decode PNG, JPEG, GIF (first frame),
BMP, TGA and PNM to RGBA. Images larger than `ROLLTUI_IMAGE_MAX_SIDE` (10,000) pixels on a side,
or `ROLLTUI_IMAGE_MAX_BYTES` (512 MB) once decoded, are rejected before decoding, with the reason.
`rolltui_image_from_rgba` takes pixels your program made. JPEGs whose EXIF data says the camera
was rotated are turned upright when decoded.

By default an image is scaled to its cells by averaging the pixels each cell covers.
`rolltui_image_set_filter(img, ROLLTUI_IMAGE_NEAREST)` uses the pixel at each cell's centre
instead, which keeps pixel art and screenshots of text sharp.

`rolltui_frame_image(frame, area, img, ground, &used)` fits an image into a rectangle of cells,
keeping its aspect ratio and never scaling it up past its own size, and draws it with whatever
method the terminal supports. `ground` shows through transparent pixels. `rolltui_image_cells`
returns the cells it would take, for centring.

| Method | Terminals | How it works |
|---|---|---|
| `ROLLTUI_IMAGES_BLOCKS` | every terminal, tmux, ssh | half blocks, two pixels per cell in their colours |
| `ROLLTUI_IMAGES_KITTY` | kitty, Ghostty | Unicode placeholders; the image lives in the cells |
| `ROLLTUI_IMAGES_ITERM2` | iTerm2, WezTerm | OSC 1337, drawn by the terminal at a cell |
| `ROLLTUI_IMAGES_SIXEL` | foot, mlterm, Konsole, Windows Terminal, xterm with sixel | sixel, drawn by the terminal at a cell |

The method is chosen from the terminal's name (`TERM`, `TERM_PROGRAM`, then its XTVERSION reply).
For an unknown terminal, the library asks: kitty's protocol if the terminal answers kitty's image
query, then sixel if its Primary DA mentions it, otherwise blocks. The answer is cached with the
others.

Inside tmux, the library asks tmux which terminal is outside it and whether the pane allows
passthrough (`allow-passthrough`, which it reads but never sets). With passthrough on, kitty
placeholders and their pixels go through to kitty or Ghostty. tmux draws sixel itself when the
outer terminal supports it and tmux's Primary DA says so. Anything else is drawn with blocks.
Inside GNU screen, Zellij and Neovim's terminal, images are always drawn with blocks.

`ROLLTUI_IMAGES` (`blocks`, `kitty`, `iterm2` or `sixel`) or `RolltuiTerminalOptions.images`
forces a method. `RolltuiTermFacts.images` says which one is in use, and `dirktui check-terminal`
prints it.

The size of a cell in pixels (`cell_w`, `cell_h`) comes from the terminal's own report (`CSI 16 t`)
when its window size is missing or disagrees with it, and otherwise from the window size divided by
the number of cells, re-read on every resize. `rolltui_terminal_set_cell_pixels` overrides it.
When nothing is known, cells are assumed to be about twice as tall as they are wide.

A new image is held back until it has stayed in the same cells for 60 ms
(`RolltuiTerminalOptions.picture_delay_ms`; -1 sends it immediately), so moving the cursor quickly
over a list of files doesn't send every image it passes. `rolltui_terminal_pending_ms` says when a
held image is due, and `rolltui_run` waits for it.

- A kitty image's pixels are sent once, before the frame that first shows it, and its placement is
  sent again whenever its size in cells changes. Locally the pixels go through a temporary file
  that kitty reads and deletes; over ssh they go in the escape sequence. Images are removed when the
  program exits. Since the image lives in its cells, a popup on top covers it, the diff only sends
  what changed, and exiting deletes what was sent.
- A sixel or iTerm2 image is drawn after the diff, with the cursor saved and restored, as long as
  all of its cells are still blank and it isn't already on screen. While anything covers part of it,
  it isn't sent; once that goes away, it's sent again in full. When the image goes away, its cells
  are rewritten, which erases it.
- Sixel images are kept off the last row of the screen, where the terminal would scroll to move the
  cursor below them. The palette is fitted to each image, with up to 255 colours, each the average
  of the pixels it stands for.
- iTerm2 gets PNG, JPEG and GIF files as they were read. Images without a file (pixels from your
  program, BMP, TGA) are sent as a generated PNG, keeping transparency.

When another program draws the image (`kitten icat --place`, chafa's sixel output, or a window over
the terminal like Überzug++), your program is responsible for drawing and clearing it, and the
library tells it when. `rolltui_frame_outside_picture(frame, id, cells, ground)` keeps the cells
blank under your id, and `rolltui_terminal_set_outside_pictures(term, fn, user)` registers a
callback that receives:

- `ROLLTUI_OUTSIDE_DRAW` once the cells are blank on screen and have stayed put for
  `picture_delay_ms`, after the frame is written, with the rectangle in terminal rows and columns.
  The cursor is restored when `fn` returns. It is sent again whenever something removed the image:
  a resize or repaint, something covering it that has since gone, or the terminal being re-entered.
- `ROLLTUI_OUTSIDE_CLEAR` when the image is no longer where it was drawn: removed from the frame,
  moved, covered, about to be overwritten by a full repaint, or because the terminal is being left
  or handed to another program. It arrives before the frame that rewrites the cells, and your
  program removes whatever text won't cover, such as a kitty placement or an overlay window.
  Freeing the terminal clears anything still drawn.

A stop from the shell (Ctrl-Z) happens inside a signal handler, which can't call anything, so the
clear arrives when the program continues, followed by a draw.

An `image:<path>` window shows a centred image: either the one bound under that name with
`rolltui_windows_bind_image`, or the file at the path (relative to the preset directory, like
`file:`), reloaded when it changes. If the window table has workers (`rolltui_windows_set_tasks`),
files are read and decoded in the background, and the previous image stays on screen until the new
one is ready. The column browser's preview shows image files the same way, with the size in pixels
in its header.

## The column browser

A `picker:` window shows one column per directory from the root down to the current one, with the
directory or file under the cursor next to them.

| Keys | Action |
|---|---|
| Up, Down, Home, End, Page Up, Page Down | move within the directory |
| Ctrl-U, Ctrl-D | move half a page |
| Left, Right | go to the parent directory, or into the one under the cursor |
| Alt-Left, Alt-Right | back and forward through visited directories |
| `-` | the directory you last left |
| Enter, Escape | choose the entry, or cancel |

Each directory remembers which entry its cursor was on, and the column next to the cursor shows
that entry in the selection colour, like the path leading to the cursor.

If the window table has workers (`rolltui_windows_set_tasks`), the directory next to the cursor is
read in the background as the cursor moves. A move waits up to 30 ms for it, so a fast read shows up
in the same frame. A slower one (a network share or a very large directory) shows "loading…" after
100 ms and fills in when it's done; going into it in the meantime puts the cursor there once it
arrives. A directory your program points the browser at (`rolltui_windows_set_picker_dir`), and each
directory on the way to it, is read straight away, as are the ones Alt-Left, Alt-Right and `-`
go back to.

`RolltuiPickerOptions.ratio_parent`, `ratio_current` and `ratio_next` set the pane widths as
proportions. 1, 2 and 1 give the parent directories a quarter, the current directory half, and the
pane beside it a quarter. A proportion of 0 sizes that pane to fit its names.

### Marks

With `RolltuiPickerOptions.marks`, entries can be marked by path in any directory. Space toggles the
mark and moves to the next entry, Shift-Up and Shift-Down mark while moving, and Alt-U clears all
marks. Marked entries are bold in the `accent_1` role with ▌ in front (`*` where ambiguous-width
characters are two cells wide). `rolltui_windows_picker_marked` returns the marked paths, so your
program can copy, move or open them together.

`rolltui_windows_set_picker_decor` lets your program decorate entries. Its function is called for
each entry drawn and fills in a `RolltuiPickerDecor`: `gutter`, a character before the name (for a
copied or cut tag); `cell`, a few characters right of the name (for a git status); and `role`, the
role for the name. Your `gutter` character replaces the mark's.

### File colours

`rolltui_windows_set_picker_file_colors` colours the names by a `RolltuiFileColors`, the way `ls`
and most file managers do. The cursor's row, the trail and the hover keep their own styles. Make
one with `rolltui_file_colors_new` and fill it with any of these:

- `rolltui_file_colors_add_env` reads `LS_COLORS`, or `LSCOLORS` if that isn't set.
- `rolltui_file_colors_add_gnu` takes GNU's format, such as `di=01;34:*.tar=01;31`.
- `rolltui_file_colors_add_bsd` takes BSD's, such as `exfxcxdxbxegedabagacad`.
- `rolltui_file_colors_add_rule(fc, pattern, len, type, style)` adds a rule of your own. The
  pattern is a glob over the whole name (`*`, `?`, `[a-z]`, `[!x]`, ignoring case), or a regular
  expression after `re:`. The type is `ROLLTUI_FILE_ANY`, `_REGULAR`, `_DIR`, `_LINK` or `_EXEC`.

Your rules are checked first, in the order you added them, and the first match wins. After them,
the `LS_COLORS` rules apply as `ls` applies them. A plain file takes the last `*suffix` entry its
name ends with, ignoring case. An executable is `ex` whatever its suffix, a folder is `di` (or
`tw`, `ow` or `st` by its permissions), and a broken link is `or`, or `ln` when there's no `or`.
The browser lists what a link points at, so a working link is coloured as its target.
`rolltui_file_colors_match` gives the style for a name, its mode and whether it's a link, for a
list of files your program draws itself. dirktui has this as a Look setting, off by default.

### Events

`rolltui_windows_picker_event` returns what the user did: chose an entry, cancelled or copied. With
`RolltuiPickerOptions.report_changes` it also returns `ROLLTUI_PICKER_EVENT_MOVED` when the cursor
moves to another entry and `ROLLTUI_PICKER_EVENT_FOLDER` when the current directory changes, each
with its path.

## Previews

`RolltuiPickerOptions.preview` shows the file under the cursor in the right half of the browser
(in dirktui, Settings › General › Preview).

| File | Preview |
|---|---|
| Text | its lines, reading up to 256 KB |
| Markdown | rendered, with headings, lists, tables and code blocks |
| Binary | offsets, hex bytes and an ASCII column, read a screen at a time |

Control characters are shown as their symbols (`␛`) and invalid bytes as `�`. Tabs stop every
`RolltuiPickerOptions.preview_tab` columns (4 if 0). With `preview_wrap`, long lines continue on
the next rows. Without it they end in an ellipsis, and Shift-Left and Shift-Right (from the list or
in the preview) and horizontal scrolling move a text file sideways, up to the end of its widest
line. Alt-Up and Alt-Down (`picker.preview_up`, `picker.preview_down`; also K and J in the vim
preset) scroll the preview by one wheel notch without moving focus from the columns.

If the window table has workers (`rolltui_windows_set_tasks`, or `rolltui_run_tasks` for the run's
own), files are read off the UI thread, and the files above and below the cursor are read ahead
unless your program sets its own previewer. The last four previews are cached, so going back to a
file shows it immediately; a cached file is only read again if it changed on disk.

### Your own previewer

`rolltui_windows_set_picker_previewer` adds a function that runs before the built-in preview, for
things like document pages or another program's output. It's called for each file previewed. It
fills the `RolltuiAnsiText` with text and SGR colours and returns 1, or returns 0 to let the
built-in preview handle the file.

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

`offset` is how far the user has scrolled the preview. If `*bound` is left at 0, it's a line of
the text the function filled in, and scrolling moves through those lines. If `*bound` is set, it's a
position of the function's own choosing (a page, a percentage) from 0 to `*bound` - 1; each scroll
calls the function again with the new offset, and Page Up and Page Down move by one. The text
belongs to the preview until the next call, so the function can keep adding to it (on the drawing
thread) as a command's output arrives.

### A command as the previewer

`rolltui_windows_set_picker_preview_command` runs a program for each file previewed:

```c
static const char* const pv[] = {"pistol", NULL};
rolltui_windows_set_picker_preview_command(w, "picker:files", sizeof "picker:files" - 1, pv);
```

The program gets the file's path and the preview's width and height after its own arguments
(`pistol notes.md 50 30`). It runs in a pseudo-terminal of that size, so it can write colour, and
its output is shown as it arrives. It starts once the cursor has rested on a file for 30 ms, so
moving quickly over files doesn't start anything, and moving away stops it. If the program can't
start, the built-in preview is used. Your own previewer function, if set, is asked first.

## Mermaid diagrams

A ```` ```mermaid ```` block is drawn as a diagram anywhere Markdown is rendered: previews,
transcripts and documents. Diagrams use box-drawing characters in the theme's colours, or
`+ - | > v` where ambiguous characters are two cells wide.

Supported diagram types are flowcharts (all four directions, every node shape and edge style, and
subgraphs), sequence diagrams (loop, alt and opt frames, notes, activations), state diagrams, class
diagrams, ER diagrams, mind maps, timelines, user journeys, Gantt charts and pie charts. Other types
are shown as source, with a line explaining why.

If a diagram is too wide, it is first drawn with its nodes closer together, then with labels
wrapped to narrower and narrower widths. Labels are never cut off. A diagram that still doesn't fit
is shown as source.

## Syntax highlighting

Previews of files in a known language are highlighted, and the preview heading names the language
(`Python · 27 lines · 722 B`). Fenced code blocks in Markdown are highlighted according to their
tag (```` ```rust ````, ```` ```yaml ````). `RolltuiPickerOptions.no_syntax` turns highlighting off
(in dirktui, Settings › Look › Colour source code in the preview).

### Languages

C, C++, C#, Java, JavaScript, TypeScript, Python, Rust, Go, Swift, Lua, Luau, shell (sh, bash,
zsh), Windows batch, PowerShell, CMake, HTML, CSS, XML, JSON, YAML, INI, TOML, ignore files
(`.gitignore`, `.dockerignore`, `.npmignore` and the rest), `.gitattributes`, DagOnC (`.jc`) and
shader (`.shader`) files.

The language is detected from the full file name (`CMakeLists.txt`, `.bashrc`), then the longest
matching extension (`.d.ts` before `.ts`), then the first line (a `#!` line, `<?xml`,
`<!DOCTYPE html`).

### Styles

Each token class (keyword, type, function, string, escape, number, constant, comment,
documentation, punctuation, preprocessor, variable, attribute, tag, property, section) uses an
existing theme role, so every theme can highlight code. Keywords are also bold and comments italic.
`mono` distinguishes the classes with bold, italic, underline and dim.

### Grammar files

Each language is a JSON file. The built-in grammars are in `presets/syntax/`, and your own go in
`<config>/rolltui/syntax/`, which is read when the browser starts. A file with the same name as a
built-in language replaces it, and languages that include it use the replacement.

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

At each position the rules are tried in order, and the first match wins.

| Rule | |
|---|---|
| `match` | a pattern; `captures` colours groups separately; `push`, `pop` and `set` change context |
| `words` | a set of whole words |
| `region` | from `begin` to `end`, with an optional `escape` and nested `rules`; `\\1` in `end` is what `begin` captured (here-documents, Lua long brackets) |
| `include` | another context, or another language |

A context can have its own `class` (`"block": { "class": "comment", "rules": [...] }`), which
applies to text no rule matches.

Patterns support character classes, groups, `* + ? {n,m}`, lazy quantifiers, `^ $ \\b`,
look-ahead and back-references, but not look-behind. Patterns that take too long are stopped.
Remember to double backslashes in JSON.

### Checking a grammar

Grammars with errors are skipped. `dirktui languages` lists the built-in languages and yours, and
for each file that failed to load, the language, the rule and the reason.
`dirktui languages my.json` checks a single file and exits with 1 if anything failed. The library
function behind it is `rolltui_syntax_check`.

### Limits

Highlighting works line by line, carrying state from one line to the next for multi-line comments,
strings and here-documents. It isn't a parser, so unusual constructs can be miscoloured; for
example, a YAML block scalar whose content looks like keys is coloured as keys. Only the first 4,000
bytes of a line and the first 256 KB of a file are highlighted. Highlighting only changes styles,
never the characters or their positions.

## File info row

`RolltuiPickerOptions.info` (in dirktui, Settings › General, on by default) adds a row under the
column headings with the mode and modification time (local, to the minute) of the entry to the
right of the cursor. For a directory it goes above its column (`drwxr-x--- 2026-06-01 12:34`), and
for a file under its preview heading. Without a preview, a file gets a small pane with its name,
its size (unless a size column already shows it) and the same row.

The values come from the directory listing without any extra `stat` calls, and the watcher keeps
them up to date. The row takes one line from the columns and is left out when the browser is too
short.

# Running

## The run loop

`rolltui_run(in_fd, out_fd, options, app)`:

1. enters the terminal
2. draws each frame at the terminal's colour depth
3. waits for input and passes each event to the app (as a copy)
4. redraws the whole screen after a resize, after a cached answer about the terminal turns out to
   be wrong, and when a program such as an editor gives the terminal back
5. restores the terminal before returning

The app is a `RolltuiRunApp`. `render` and `event` are required; `start`, `ground`, `resized`,
`facts_changed`, `settle` and `focus` are optional. `rolltui_run` returns `ROLLTUI_RUN_STOPPED`,
`ROLLTUI_RUN_NOT_A_TERMINAL` or `ROLLTUI_RUN_BAD_APP`.

The library asks every terminal to report two kinds of change:

- The window gaining or losing focus (mode 1004). `rolltui_run` calls the app's `focus` with 1 or
  0, and a hand-written loop gets a `ROLLTUI_TERM_EVENT_FOCUS` event with `focused`. While the
  terminal window is unfocused, no window on screen is drawn as focused, so borders, cursors and
  selections all show the unfocused look until focus returns.
- The background switching between light and dark when the system appearance changes (mode 2031).
  The terminal's facts change and the app's `facts_changed` re-resolves the theme, as it does when a
  cached answer turns out to be wrong, so themes in `auto` mode follow along.

Terminals that support neither mode ignore the request.

`rolltui_run_suspend_job(run)` does what Ctrl-Z does in a shell: it restores the terminal and stops
the process, and when the shell continues it (`fg`) the terminal is set up again and the screen
redrawn. A stop sent from outside (`kill -TSTP`) does the same without any app code. Windows has no
job control, so there it returns 0. dirktui binds it to Ctrl-Z (`app.suspend`); when a text field
has focus, Ctrl-Z is undo instead.

`rolltui_run_inject(run, event)` gives the app an event the program created itself, such as a key
replayed by a macro. It's copied and delivered at the start of the next wake, before anything from
the terminal. `rolltui_run_flush_input(run)`, called while handling a key, drops the rest of the
wake's events and anything typed but not yet read, so holding a key down over a slow action
doesn't queue up repeats.

`rolltui_terminal_set_mouse(term, mouse, hover)` turns mouse reporting on or off while the app
runs. With it off, the terminal handles text selection itself. The setting survives a suspend.

An app with its own threads can build its own loop from `rolltui_terminal_new`,
`rolltui_terminal_poll`, `rolltui_terminal_present`, `rolltui_terminal_wake` and
`rolltui_terminal_suspend` / `_resume`. `examples/chat.cpp` does this.

### A step at a time

When your program's own loop can't block (a web page, a game engine, a GUI toolkit's event loop),
run the same loop one step at a time:

```c
int result;
RolltuiRun* run = rolltui_run_begin(in_fd, out_fd, opts, &app, &result);
/* in your loop, when input arrives and when the last step asked to be called: */
int ms = rolltui_run_step(run);
if (ms == ROLLTUI_RUN_STEP_STOPPED) rolltui_run_finish(run);
```

- `rolltui_run_begin` enters the terminal and calls `start`. If it rejects the app or the file
  descriptors, it returns NULL and sets `result` to what `rolltui_run` would have returned.
- Each `rolltui_run_step` processes whatever has arrived without waiting (events, results from
  workers, due timers, `settle`), then draws and presents one frame. It returns how many
  milliseconds the frame asked to wait (less if a timer is due sooner), or -1 to wait for input.
  Call it again after that time or as soon as input arrives.
- Once the app stops, every step returns `ROLLTUI_RUN_STEP_STOPPED`. `rolltui_run_finish` restores
  the terminal and frees the run.
- `rolltui_run_program` and `rolltui_run_suspend_job` block, so a stepped program can't use them.

## Inline mode

An inline program draws in the normal screen below the shell prompt, instead of on the alternate
screen. Set `RolltuiTerminalOptions.inline_rows` to run inline. `rolltui_run` then draws frames of
that many rows (at most the terminal's height). With your own loop you can draw each frame at any
height and the region follows.

- The region starts on the cursor's row, or on the next row if the cursor isn't in column 1.
  Terminals that don't report the cursor position get the region at the bottom of the screen.
- If the region is taller than the space below the cursor, the screen scrolls up to make room.
- `rolltui_terminal_commit(t, lines)` writes rows of a frame above the region as ordinary lines,
  trimmed after their last cell. They become part of the terminal's scrollback, where the user can
  scroll, select and copy them as usual. The region is redrawn below them.
- Cells in the frame's ground colour use the terminal's own background. Panels and other cells keep
  their colours.
- Mouse rows count from the top of the region, and clicks above the region are ignored.
- After a resize the region is located again. On terminals that rewrap lines when they get narrower
  (all the common ones except xterm, st and the Linux console), the extra rows from the rewrap are
  taken into account.
- On exit the region is erased and the cursor left where the region started.

A layout with `"inline": true` is drawn inline, and `rolltui_layout_inline` reports it. In the
studio's layout editor it's the "Drawn inline" toggle under This screen. The built-in `inline`
layout has the conversation and the prompt.

`rolltui-chat` runs inline when its layout is `inline`, chosen from the menu, with
`/set layout inline`, or for one run with `CHAT_LAYOUT=inline`. Each finished message is drawn
with `rolltui_transcript_entry_frame`, committed, and removed from the document with
`rolltui_document_remove`. Switching to a full-screen layout shows the whole conversation on the
alternate screen, and switching back to `inline` only writes what the terminal doesn't already
have. On exit, a reply that is still arriving is committed as far as it got.

## In a web page

`tools/web/build.sh` builds `rolltui-tour` (`examples/tour.c`) for wasm32-wasi with zig and puts it
next to a page that runs it in xterm.js:

```sh
cmake -S . -B build && cmake --build build
tools/web/build.sh build              # writes build/web
python3 -m http.server -d build/web   # browsers only load .wasm over HTTP, not from file://
```

The list of library sources comes from the native build (`rolltui_sources.txt`, written by CMake),
so the page is compiled from the same files. In the browser:

- File descriptor 1 is the terminal. `tools/web/rolltui_web.js` passes what xterm.js sends to the
  program with `rolltui_web_input` and size changes with `rolltui_web_resize`, and steps the loop
  when input arrives and when the last step asked.
- On a touch screen, a tap is a click and a swipe scrolls like the mouse wheel, one step per cell
  moved, while the program has asked for mouse input. Pinch zoom still works.
- The OS layer is `c/rolltui_os_web.c`. A web page can't run other programs and has no threads, so
  the task pool runs each job on the thread that drains it. There's no file system either: every
  preset is the library's built-in copy, and nothing the user changes is saved.
- The page can't wait for the terminal's replies while the program starts, so the program doesn't
  ask (`no_probe`), and the page answers DA1 for xterm.js before starting it. Colour depth and light
  or dark mode come from the environment the page sets: `COLORTERM=truecolor`, and `COLORFGBG` from
  the browser's colour scheme.

To build your own program this way, export a start and a step function that call
`rolltui_run_begin` and `rolltui_run_step`, as `examples/tour.c` does under `__wasi__`.

## Work off the UI thread

A `RolltuiTasks` is a small pool of worker threads, the jobs they run, and a queue of results. A
job's `work` function runs on a worker. Its `done` function, and anything posted, runs on whichever
thread calls `rolltui_tasks_drain`, in the order it arrived. That way your program's state is only
ever touched from that one thread.

- `rolltui_tasks_start(t, work, done, ctx)` queues a job and returns its id. `done(ctx, cancelled)`
  is always called exactly once, which makes it the place to free `ctx`.
- `rolltui_tasks_cancel(t, id)` cancels a job. A queued job never runs; a running one sees
  `rolltui_job_cancelled(job)` return true and can stop early if it checks. Either way `done` gets
  `cancelled` set and the job's results are dropped. Cancelling a job that already ended does
  nothing.
- `rolltui_tasks_post(t, fn, ctx)` can be called from any thread, and runs `fn(ctx)` at the next
  drain.
- `rolltui_tasks_call(t, job, fn, ctx)` does the same but waits until `fn` has run, so a job can
  read things only the UI thread may touch (the selection, a store). It returns 0 without running
  `fn` if the job is cancelled or the pool is freed first. Called from the draining thread, it runs
  `fn` immediately.
- `rolltui_job_ask(t, job, ask, ctx, &answer)` pauses a job to ask the user something. `ask(ctx,
  question)` runs on the UI thread and opens a prompt, and whatever receives the answer calls
  `rolltui_tasks_answer(t, question, text, len, yes)`. The job then continues with 1 and the text,
  0 if the user declined, or -1 if it was cancelled in the meantime. Answering a cancelled job's
  question returns 0, so your program knows to close its prompt.
- `rolltui_tasks_running(t, id)` tells you whether a job is still going.
- Each finished job and each post calls the `wake` function the pool was created with, so a loop
  waiting for input drains straight away.
- `rolltui_tasks_busy` counts jobs whose `done` hasn't run yet, and `rolltui_tasks_wait_idle`
  drains until it reaches 0, which is what tests wait on.
- `rolltui_tasks_free` cancels queued jobs, waits for running ones, and calls every outstanding
  `done` before returning.

`rolltui_run` creates its own pool on first use. `rolltui_run_tasks(run)` returns it: two workers,
woken through the terminal and drained on every wake before `settle`, and freed when the run ends.

The run also has timers. `rolltui_run_after(run, ms, fn, ctx)` fires once and `rolltui_run_every`
repeats, both on the run's thread after the events of a wake, and the wait returned by `render` is
shortened to the next one due. A repeating timer that falls behind is rescheduled one period from
now instead of firing in a burst. `rolltui_run_cancel_timer` cancels one by id.
`rolltui_run_debounce(run, &slot, ms, fn, ctx)` cancels the timer in `slot` and starts a new one,
so a burst of calls fires once, after the last.

`rolltui_windows_bind_changed(windows, name, …)` calls a function with an `input:<name>` window's
text whenever it changes, which together with `rolltui_run_debounce` is how you filter as the user
types.

`rolltui_windows_set_tasks(windows, tasks)` gives the built-in widgets workers to use. The column
browser's preview then reads files and decodes images on a worker while the cursor keeps moving,
shows "loading…" once a read has taken 100 ms, and drops reads for files the cursor has left.
Without workers it reads on the drawing thread, which is what frame dumps do. dirktui gives it the
run's pool. An abandoned read stops at the next 64 KB boundary, and indexing lines, highlighting and
parsing Markdown all happen on the worker that did the read.

### Holding input

`rolltui_windows_hold(windows, label, len, job)` blocks input while work the user is waiting for
runs (a pull, a save over the network). No window gets keys or mouse events, and a box with a
spinner, the label and "Esc cancels" is shown over the screen. Escape cancels the job and ends the
hold immediately. Otherwise the hold ends when the job finishes (for jobs on the pool given to
`rolltui_windows_set_tasks`) or when you call `rolltui_windows_release`; with `job` set to 0, only
those two end it. Your program still sees every key first, so its own keys, including quit, keep
working; `rolltui_windows_app_key` leaves Escape to the hold.

## Other programs

`rolltui_child_spawn` starts a program and returns a handle you can read from and write to without
blocking. `argv[0]` is looked up on PATH (on Windows, using PATHEXT's extensions; scripts run through
cmd.exe, which rejects arguments containing `%` or `"`). `RolltuiChildOptions.mode` controls how its
streams are connected:

- `ROLLTUI_CHILD_PIPES`: stdin is a pipe you write to, and stdout and stderr are merged into one
  stream you read. The program gets its own process group.
- `ROLLTUI_CHILD_PTY`: a pseudo-terminal of `cols` by `rows`, so the program writes colours and
  progress output as it would to a terminal. `rolltui_child_resize` keeps it in sync with the window
  showing it. On Windows this uses pipes with `COLUMNS` and `LINES` set, because ConPTY reorders a
  program's output.
- `ROLLTUI_CHILD_QUIET`: all three streams go to the null device and the program gets its own
  session. Ctrl-C in your app never reaches it, and freeing the handle leaves it running.

`cwd` and `env` (`"NAME=value"` entries added to the app's environment) are optional. If the
program can't be started, you get NULL and the reason ("not found: bat").

- `rolltui_child_pump(c, text, max_lines)` reads whatever has arrived into a `RolltuiAnsiText`,
  stopping once it holds `max_lines`. A view only reads what it shows, the program waits on its full
  pipe, and the rest is read when more lines are needed.
- `rolltui_child_read` returns the raw bytes. `rolltui_child_write` and `rolltui_child_close_input`
  feed its input.
- `rolltui_child_done` checks whether it has exited without waiting; `rolltui_child_wait` waits.
  The status is the exit code, or 128 plus the signal that killed it.
- `rolltui_child_stop` sends SIGTERM to its process group, and SIGKILL 200 ms later if it's still
  running (TerminateProcess on Windows). `rolltui_child_free` stops a PIPES or PTY program that is
  still running.
- While a program is running, `render` should wait at most `rolltui_child_wait_ms(c)`, so its
  output is drawn as it arrives.

`rolltui_run_program(run, argv, cwd, &status, &err)` hands the terminal over to a program that takes
it over completely, such as an editor or a pager. The app waits (ignoring the Ctrl-C the program
receives), then takes the terminal back and redraws the screen. A SIGTERM sent to the app in the
meantime is passed on to the program, and once the program exits the app exits with the same
signal. `rolltui_open_with_system` opens a file or URL with the system's handler (`open`,
`xdg-open` or ShellExecuteW).

### Coloured output

A `RolltuiAnsiText` turns what a program wrote to a terminal into lines you can draw: the output of
`bat`, `delta`, `ls --color`, a log or a man page.

- It understands text, newlines, tabs (every 8 columns, or as set by `rolltui_ansi_set_tab_width`)
  and SGR: 16, 256 and 24-bit colour, bold, dim, italic, underline shapes (4:1 to 4:5, 21) and
  colour (58), blink, reverse, hidden, strikethrough and overline. Sequences and characters split
  across two feeds are joined back together.
- A carriage return without a newline rewrites the line, so a progress counter shows its final
  state. Erase-in-line clears the line being written.
- Backspace overstrike is read the way man pages use it: `X\bX` is bold and `_\bX` underlined.
- OSC 8 hyperlinks are attached to the text they cover, from where the link opens to where it
  closes, across lines just like in a terminal, and drawn as links. A URL that isn't printable ASCII,
  or an OSC longer than 4 KB, is ignored.
- Cursor movement, erase-in-display, other OSC sequences, DCS, APC and charset changes are
  discarded, so the output shows what it says and can't take over the app's screen.
- The lines are kept in a ring of `ROLLTUI_ANSI_DEFAULT_LIMIT` (100,000) lines, or the limit set
  with `rolltui_ansi_set_limit`, so a long log keeps its most recent lines.

An `output:<source>` window shows one. Bind it with `rolltui_windows_bind_output`. The window
follows the end of the text as it grows; once scrolled back, it stays put until scrolled to the end
again. Lines wrap at the window's edge, or `rolltui_windows_set_output_wrap(w, name, len, 0)` turns
wrapping off. Cells without their own colour use the window's text and background colours, so the
program's default colours follow the theme. `rolltui_ansi_draw` draws the same text in your own
widget.

### Links in a window

Links in any window, whether written by another program, in Markdown, or drawn by your program
with `rolltui_frame_link_id`, are sent to the terminal as OSC 8 hyperlinks. The window also handles
them itself, because a terminal that reports the mouse to the app doesn't:

- While the terminal reports hover, the link under the pointer is underlined.
- A left click on a link goes to your program's handler first, if you set one with
  `rolltui_windows_set_link_handler(w, fn, ctx)`, along with the window and the URL. Return 1 from
  `fn` to handle it yourself, for example for your own URL scheme.
- Otherwise http, https, mailto and file links are opened with `rolltui_open_with_system`. Clicks
  on links with any other scheme are handled like ordinary clicks.

### Wrapping and scrolling sideways

`text`, `file` and `output` windows wrap long lines at their edge. `view.wrap` (Alt-W) toggles
between wrapping and keeping lines whole (cut off at the edge). When lines are kept whole,
`view.left` and `view.right` (Left and Right, or h and l with the vim preset) and horizontal
scrolling move them four cells at a time, up to the end of the widest line. A wide character that
would be cut in half at the edge is shown as a blank. In an `output` window the key changes the
binding's wrap setting, so it stays consistent with `rolltui_windows_set_output_wrap`.

`rolltui_windows_set_output_options` sets these options for an output window:

- `timestamps`: show each line's time (HH:MM:SS) in a gutter.
- `status`: show "following" in the footer, or "paused · 12 below" once scrolled back, plus whether
  a program running in the window is still going or how it exited.
- `filter`: only show lines that match, using the same filter syntax as lists, with matches in bold
  and underlined. Separator lines are always shown.

`rolltui_ansi_set_stamp(text, ms, tag)` sets the time and source for lines started after the call.
`rolltui_ansi_mark(text, label, len)` adds a separator line after the current text.
`rolltui_ansi_line_info`, `rolltui_ansi_line_rows` and `rolltui_ansi_draw_line` give your own views
the same building blocks: a line's time, source and number, the rows it wraps to, and a way to draw
one line with highlighted ranges.

### Programs in a window

`rolltui_windows_run_output(w, name, len, &opts, label, role, &err)` runs a program and shows its
output in an `output:<name>` window, using `opts` (`ROLLTUI_CHILD_PTY` to get its colours). The
window reads the output as it draws, and a pseudo-terminal is given the window's size and every
later size. The program is stopped when the window leaves the layout or the name is bound to
something else. If no pseudo-terminal can be opened, the program gets pipes with the size in
`COLUMNS` and `LINES`.

Several programs run under the same name are interleaved line by line, each line starting with its
program's `label` in `role` (`web GET /`, `db  SELECT 1`). Unless you've set the window's options,
the footer shows how they're doing. `rolltui_windows_output_running` returns whether one is still
running and how the last one exited, and `rolltui_windows_output_mark` adds a separator line, for
example at the start of a build.

`rolltui-chat` runs a `!command` once it's approved. The output streams into the conversation as a
code block, followed by the exit status, and Esc stops it. Frame dumps never run anything.

## File watching

The column browser re-reads every directory on screen, and the previewed file, about twice a
second.

- Files that are added, grow, get removed or get renamed show up within that interval.
- The cursor and scroll position are kept by name. If the selected entry disappears, the cursor
  moves to whatever is now in its place.
- When a directory is removed its columns close, and focus goes back to the deepest directory that
  still exists.
- A file replaced by a directory, or the other way round, is followed.
- The preview keeps its top line when the start of the file changes, and follows the end of a
  growing file if it's scrolled to the bottom.

Each check is one `stat` per directory and per visible row, and no file descriptors are held open.
An unchanged directory of 20,000 entries takes about 0.1 ms to check. Directories that keep
changing are checked less often. `RolltuiPickerOptions.no_watch` turns watching off.

### Watching your own files

`RolltuiWatch` tells your program when files or directories it cares about change:

```c
static void wake(void* term) { rolltui_terminal_wake((RolltuiTerminal*)term); }

RolltuiWatch* w = rolltui_watch_new(wake, term);   /* or NULL, NULL to poll instead */
unsigned int log = rolltui_watch_add(w, path, strlen(path));
...
unsigned int ids[8];
size_t n = rolltui_watch_changed(w, ids, 8);       /* each change is reported once */
```

A change means the path's device, inode, size or timestamps changed. That covers saving by writing
a new file and renaming it over the old one, a path that only appears after you started watching
it, a file being removed, and entries being added to or removed from a directory. The system
reports when to look (kqueue on macOS and the BSDs, inotify on Linux, change notifications on
Windows), so changes are seen within milliseconds. Where that isn't available a thread checks twice
a second, and in a browser each call checks. `rolltui_watch_how` says which method is in use.
`wake` is called from the watcher's thread, so a loop waiting for input checks straight away.

### Settings changed by another program

`rolltui_run_watch_stores(run, theme, layout, keys)` watches the stores' files for as long as the
run lasts: the working copy, plus the preset it's based on in the user's directory and the built-in
one. When another program (the studio, an editor) changes one of them, it's read again and the
screen redrawn, and SIGUSR1 reloads all of them. A store with its own unsaved edits keeps them; only
a change to its working file replaces them. Each store's version number changes just as it would
for an edit, so an app that tracks store versions picks up the change; dirktui does this for its
theme and key bindings. For programs that check their files on their own schedule,
`rolltui_theme_store_refresh` and the equivalent functions of the layout and bindings stores do the
same thing. A store's own writes never count as changes.

## The clipboard, the title and the terminal

`rolltui_clipboard_copy(terminal, text, len)` sets the clipboard in two ways at once. It sends OSC
52 to the terminal, which is the only way to reach the user's own machine over ssh; inside tmux it's
passed through, inside GNU screen it's split into pieces screen passes on, and above 74,994 encoded
bytes it isn't sent (except to kitty, which accepts any size). It also hands the text to the first
native tool that works: pbcopy, wl-copy, xclip, xsel, or the Windows clipboard.
`$ROLLTUI_CLIPBOARD` names a command to use instead of the native tools.

`rolltui_clipboard_paste(terminal, &out)` reads the clipboard through the first tool that responds
(pbpaste, wl-paste, xclip, xsel, or the Windows clipboard), or the command named by
`$ROLLTUI_CLIPBOARD_PASTE`. Over ssh, where no tool can reach the user's clipboard, it returns what
the program last copied, so copying and pasting within one program still works.

`rolltui_terminal_set_title` sets the window title (OSC 2). The terminal's original title is saved
first and restored when the app exits or hands the terminal to another program, and the app's title
is set again when it takes the terminal back. Only changes are written.
`rolltui_terminal_report_cwd` reports a directory with OSC 7, so new tabs and splits open there;
dirktui reports the directory it's showing.

`rolltui_terminal_set_window_name` sets the window's name in tmux's window list (and screen's),
which the title doesn't affect. tmux accepts the escape sequence for it while `allow-rename` is on;
with it off, the library runs `tmux rename-window` instead. On exit, the window gets its previous
name back and tmux goes back to naming it automatically.

`rolltui_open_terminal(&in, &out)` opens the controlling terminal directly (`/dev/tty`, or CONIN$
and CONOUT$ on Windows), regardless of what stdin and stdout are. This is for programs whose stdout
is their result for the shell, such as a file chooser or cd-on-exit: they draw on the terminal and
print the result to stdout. dirktui works this way.

## Diagnostics

While a program runs the terminal is showing its screen, so diagnostics from the library and your
program go to a log file instead. That's the file passed to `rolltui_log_to(path, len)`, or
otherwise the one named by the `ROLLTUI_LOG` environment variable. With neither, nothing is written.
Each line has the local time, the seconds since the log started, the level and the message:

```
2026-11-24 09:41:07    0.000s INFO  terminal: kitty 0.43.1, 120x40, depth truecolor, keyboard kitty, pictures kitty
```

- `rolltui_log(level, text, len)` and `rolltui_logf(level, fmt, ...)` write a line. Newlines in the
  text become spaces so each call is one line, and lines written from different threads never mix.
- `ROLLTUI_LOG_LEVEL` (`error`, `warn`, `info` or `debug`) or `rolltui_log_set_level` sets which
  lines are written; the default is `info`. `rolltui_log_wants(level)` tells you whether a line at
  that level would be written, so you can skip building an expensive one.
- The library logs which terminal it found and how it draws on it, and when a store is reloaded
  because another program changed its files. At `debug` it also logs the terminal's replies to its
  queries, byte for byte, with how long they took, and each frame's timing: your drawing, the diff,
  the write, and the number of bytes sent.

`rolltui_windows_set_timing(windows, 1)`, or `ROLLTUI_TIMING=1` for any program, shows each
window's draw time in its top border (" 0.4 ms "), in a muted colour, or bold in the warning role
when it's more than half of a 60 fps frame. `rolltui_mem_stats` returns the library's current and
peak memory use. For CPU profiles, use the system profiler (Instruments, `perf`).

`tools/bench/frame_times.py` measures a real program's frames from that log: it runs dirktui in a
pseudo-terminal at 200x50 and moves the cursor down a directory. On an M5 Max, a frame takes a
median 0.6 ms to draw and 0.2 ms to diff and write, about 500 bytes. Every window is redrawn every
frame. At that cost a spinner updating ten times a second uses under 1% of a core, and it avoids the
stale contents you'd get if a window only redrew when it reported a change and once forgot to.

# The terminal

## Terminal detection

`rolltui_terminal_new` asks the terminal, in the same round trip that sets up the keyboard, how
many colours it supports, whether its background is light or dark, how wide it draws
ambiguous-width characters, whether it gives combining accents their own cell, and whether it
supports synchronized output. The library uses the answers:

- `rolltui_swap_present` never uses more colours than the terminal supports.
- Every character is measured at the width the terminal draws it.
- Themes in `auto` mode follow the terminal's background.
- Terminals with synchronized output (DEC mode 2026) get each frame wrapped in the mode, in a single
  write, so they draw it in one go. Terminals that don't answer the query never get the mode.
- On terminals that give every code point its own cell (Windows' console host), each grapheme
  cluster is sent as a single code point after canonical composition, so `e` plus a combining acute
  is sent as `é`.

Windows' own console doesn't set `TERM` and is drawn in 24-bit colour. A Windows program running in
Windows Terminal gets full answers to its queries: synchronized output, the kitty keyboard protocol,
the background colour, and a cell size of 10 by 20 pixels, which is the cell size Windows Terminal
uses for sixel images regardless of the font. Images land in the right cells there, both natively
and from WSL.

The Linux console is recognised by `TERM=linux`, or by its tty (`/dev/ttyN`) whatever `TERM` says.
Its font only has the PC character set, so characters it lacks are replaced with the nearest it
has: braille becomes a shade according to how many dots are set, quadrants and eighths become half
and full blocks, rounded corners become square, heavy lines become double, `…` becomes `~`, `✓`
becomes `√`, and superscript four to nine become plain digits. It's drawn in 16 colours, and the
cursor shape isn't sent.

When nothing in the environment identifies the terminal, its XTVERSION reply decides how images are
drawn: kitty's protocol for kitty and Ghostty, iTerm2's for WezTerm and iTerm2, and sixel for foot,
mlterm and Konsole.

Answers are cached per terminal, keyed by the terminal's name and version, the operating system,
the program's build, and whether the session is over ssh. Where `TERM` doesn't tell terminals apart,
their own environment variables are part of the key: `KONSOLE_VERSION` and `VTE_VERSION` by value,
and `WT_SESSION` and `KITTY_WINDOW_ID` by whether they're set, so Windows Terminal and the old
console window count as different terminals while a new tab counts as the same one. Behind a
multiplexer (tmux, GNU screen, Zellij) the answers come from the multiplexer, and the terminal it's
attached to can change between sessions, so they're used but never cached, and each query waits
200 ms instead of 80. A cached answer is used at startup and checked again after the first frame.
Probing takes 0.6 ms fresh and 0.2 ms from the cache; a terminal that doesn't answer costs 80 ms,
once.

| Variable | Effect |
|---|---|
| `ROLL_COLOR_DEPTH` | `truecolor`, `256`, `16` or `mono` |
| `ROLL_AMBIGUOUS_WIDE=1` | ambiguous-width characters take two cells |
| `ROLL_TERM_PROBE=0` | only probe the keyboard |
| `ROLLTUI_CONSOLE=1`, `=0` | treat the terminal as the Linux console, or not |

The library writes UTF-8 whatever the locale says. When the locale isn't UTF-8 (the first of
`LC_ALL`, `LC_CTYPE` and `LANG` that is set decides, and with none set it's POSIX's own, which
isn't), entering a terminal logs a warning and the first window table to draw its toasts shows it,
naming the variable to change. `rolltui_locale_utf8` answers the same question for your program.

`dirktui check-terminal` probes again and shows each answer and where it came from, the locale
included.

Every theme has a light and a dark variant. The mode (auto, dark or light) and the colour depth are
user settings, stored with the chosen theme and kept when the theme changes. In `auto` mode the
terminal's background decides: light when its luminance is above 0.5. A terminal that reports its
colour scheme (DSR 996) but not its background is drawn for the scheme it reports; one that reports
both is drawn for its background.

## What a frame sends

`rolltui_swap_present` sends the terminal only what changed since the last frame, in as few bytes
as it can:

- Unchanged cells aren't sent. A gap of up to three unchanged cells between two changes is written
  out again; longer gaps are skipped with a cursor move.
- The cursor takes the shortest route from where the last write left it: CR and line feeds to the
  start of a lower row, cursor-forward along a row, or an absolute move.
- A style change only includes what changed (a colour, an attribute turned on or off), or resets and
  sends the whole style when that's shorter.
- Runs of 12 or more blank cells are erased with ECH and skipped. Trailing blanks of 4 or more cells
  are erased to the end of the line with EL. A blank is a space with no link, underline or reverse.
- When a block of three or more rows has moved up or down, the terminal scrolls that region
  (DECSTBM with SU or SD) and only the rows brought in by the scroll are written.

The cursor shape (`rolltui_frame_set_cursor_shape`, `ROLLTUI_CARET_*`: block, underline or bar,
steady or blinking) is sent when it changes, and the terminal's own shape is restored on exit. Text
fields ask for a bar while printable keys type and a block while they're commands
(`RolltuiInputOptionsRaw.caret` sets one). A focused list, menu or column browser leaves the hidden
cursor on its selected row, where screen readers follow it.

The erase and scroll optimisations rely on the terminal filling erased cells with the current
background colour, which every terminal in use does, tmux included. They're turned off directly
under GNU screen (`STY` set without `TMUX`). `tests/render_replay_test.cpp` replays the output of
thousands of random frames into a terminal model (`tests/vt_model.hpp`) and checks the result
against each frame, cell by cell.
