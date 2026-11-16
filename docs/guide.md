# rolltui guide

- [Strings](#strings)
- [Terminal detection](#terminal-detection)
- [The run loop](#the-run-loop)
- [File watching](#file-watching)
- [Previews](#previews)
- [Mermaid diagrams](#mermaid-diagrams)
- [File info row](#file-info-row)
- [Syntax highlighting](#syntax-highlighting)
- [Ownership](#ownership)
- [Embedding rolltui](#embedding-rolltui)

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

## Cut text, footers, strikethrough and focus

`rolltui_frame_put_text_cut(frame, scratch, x, y, text, len, style, max_cells, where,
ambiguous_wide, link)` draws text in at most `max_cells`, with an ellipsis where it was cut:
`ROLLTUI_CUT_END` ("a long na…"), `ROLLTUI_CUT_MIDDLE` ("~/src/r…main.c", a path keeping both
ends) or `ROLLTUI_CUT_START` ("…/c/main.c"). Text that fits is drawn whole. The ellipsis is one cell,
or two where ambiguous glyphs are wide, and a wide glyph is never cut in half.
`rolltui_text_cut` gives the same cut as text. The menu, the list and the column browser cut
their text this way.

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

Windows' own console sets no `TERM` and is drawn in 24-bit colour.

Answers are cached per terminal, keyed by terminal name and version, operating system, program
build, and whether the session runs over ssh or inside a multiplexer. A cached answer is used at
startup and checked again after the first frame. A probe costs 0.6 ms fresh and 0.2 ms from the
cache; a terminal that does not answer costs 80 ms, once.

| Variable | Effect |
|---|---|
| `ROLL_COLOR_DEPTH` | `truecolor`, `256`, `16` or `mono` |
| `ROLL_AMBIGUOUS_WIDE=1` | ambiguous-width characters take two cells |
| `ROLL_TERM_PROBE=0` | probe only the keyboard |

`dirktui check-terminal` probes again and reports each answer and where it came from.

Every theme has a light and a dark side. Mode (auto, dark or light) and colour depth are the
person's settings, stored beside the chosen theme and kept when the theme changes.

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

The erases and the scrolled region stand on the terminal filling an erase with the current
background colour, which every terminal in use does, and tmux too. Directly under GNU screen
(`STY` set, no `TMUX`) they are not used. `tests/render_replay_test.cpp` replays the bytes of
thousands of random frames into a model of a terminal (`tests/vt_model.hpp`) and checks the
screen against each frame, cell by cell.

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
(`app.suspend`) wherever nothing is being typed; in a line, Ctrl-Z is the line's undo.

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

## The layout at run time

A layout file says what a screen holds. The window stack changes what it shows while the program
runs, by window or split id; each call answers 0 when nothing has that id.

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
left out.

### Hiding, resizing and maximising

- `rolltui_window_stack_set_visible(stack, id, len, visible)` shows or hides a node. A hidden node
  takes no space.
- `rolltui_window_stack_set_size(stack, id, len, size, size_len)` sizes a node as the file spells
  it: `"30"` cells, `"40%"`, `"fill"` or `"fill 2"`.
- `rolltui_window_stack_maximize(stack, id, len)` makes one window fill its layer, and gives it the
  focus; a NULL id puts the layout back. `stack.maximize` (Alt-Z) does both for the focused window.
  `rolltui_window_stack_maximized` names the window.

Visibility and size last until the next `rolltui_window_stack_set_base`; the maximised window carries
over to a new base while its window is in it.

A press or key that changed what is shown makes `rolltui_window_stack_route` answer
`ROLLTUI_ROUTE_LAYOUT_CHANGED`, naming the window.

### A screen too small

On a screen smaller than the layout's `min_width` by `min_height`, the stack draws a note in place
of the layout: "the screen is 40×6; this needs 60×8", or "40×6 < 60×8" where the sentence does not
fit. Presses reach nothing; keys still reach the focused window, so a quit key works. A host with
a layout for smaller screens sets it as the base first: rolltui-chat and the studio switch to the
shipped `stacked` layout.

The studio's layout editor edits tabs (typed `Title = content | Title = content`), the shown tab,
and a breakpoint (typed `width`, `width x height` or `x height`).

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
- A sequence's first chord cannot also be bound alone in the same scope, and two actions cannot
  share a sequence: the loader reports the conflict and keeps what came first.
- A COUNT is digits typed before an action: `5` then Down moves five. Digits are a count only in a
  window where a digit is not text or a jump: a list (unless its filter is being typed) and a
  scrolling page count; the column browser, a menu and an input do not.
- After 300 ms with keys on the way, `rolltui_windows_draw_toasts` draws a panel at the screen's
  bottom left: the keys so far in its border, and a row for each key that can follow, with what
  it does or that more follows. `rolltui_windows_pending_keys` gives the keys on the way as text,
  for a status line.
- A widget kind joins in by answering the plugin's `keys` slot with its scope and whether a digit
  is a count; a completed sequence hands it a chord bound to the same action, or calls `perform`
  with the action's name when only a sequence is bound to it.

A chord is spelled without case, so a capital letter is `shift+g`. A terminal sends a typed capital
as the letter itself, and every lookup matches it as shift and the letter. The help shows a bare
letter as typed (`g`) and shift with a letter as the capital (`G`).

The shipped `vim` bindings preset is the default table with j and k, h and l, `g g` and `G`,
Ctrl-D and Ctrl-U added in lists, the column browser and scrolling pages. In the column browser
those letters are keys rather than jumps to a name.

## Hover

Set `RolltuiTerminalOptions.hover` and the terminal also reports the pointer as it moves with no
button held (mode 1003). It needs `mouse`, which is on by default. While the pointer rests on
something a click would act on, that thing is drawn in the theme's `hover` role:

- a menu row, an option in an open dropdown, and a menu's ▲ and ▼ scroll markers;
- an entry in the column browser;
- the "N more" marker of a text, file or help window;
- a hint in a hint bar.

The keyboard's cursor never moves with the pointer, and hover sends no menu event. The cursor's
own row keeps its look under the pointer. Headings and disabled items are not lit. Under a modal
popup, or a popup that closes on a press outside it, nothing below the popup is lit.

The window stack learns where the pointer is from the mouse events it routes, and marks the one
window a press there would reach: `RolltuiResolvedNode.hover`, with the pointer's cell in
`hover_x` and `hover_y`. A host's own widget reads them in its `draw`. For what a host draws
outside the windows, `rolltui_window_stack_pointer` gives the pointer's cell and
`rolltui_hint_bar_hover` lights a hint bar from it.

- Nothing is lit until the terminal first reports the pointer moving. A click alone does not
  say where the pointer rests afterwards.
- A terminal does not report the pointer leaving its window, so the last thing lit stays lit
  until the pointer comes back.
- The shipped themes lift the panel colour toward the text colour. At 16 colours the lift
  collapses into the panel, as the panel collapses into the background. `mono` draws a
  hovered row bold.

dirktui, `rolltui-chat`, paint and the studio turn hover on.

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
`rolltui-chat` keeps its prompt's history in `<config>/rolltui/chat/history.json`.

## Lists, tables and trees

A `list:<source>` window shows data the host holds. The host binds the source with
`rolltui_windows_bind_list(windows, name, len, count, row, ctx, free_ctx)`: `count(ctx)` says how
many rows there are, and `row(ctx, i, out)` fills row `i`: its `key`, its cells
(`rolltui_list_row_cell`), a `role` for the whole row, a tree's `depth`, and `busy` with
`busy_since_ms` for a spinner at the row's end.

- A plain list asks only for the rows it draws, so a hundred thousand rows cost a screenful.
  Sorting, a filter and a tree ask for every row once, when the data or the question changes:
  `rolltui_windows_list_refresh` says the data changed, and a count that moves says so too.
- A row is its key. The cursor, the marks and which nodes are open follow it through a refresh,
  a sort or a filter, and the cursor's row keeps its place on screen when rows arrive above it. A
  row with no key is keyed by its index.
- A list with nothing to show says "no rows", or "no rows match “…”" under a filter.

`rolltui_windows_set_list_columns` makes it a table. The first column takes the width the others
leave. The rest are `width` cells, or as wide as the widest cell they have shown; that width only
grows, so the columns stay put as the list scrolls. `align` puts a column's text at its right
edge, and `min_screen` shows a column only while the list is at least that wide. When the first
column would be narrower than 8 cells, columns go from the right. A cell that does not fit ends in
an ellipsis. The header row names the columns in bold, the sorted one marked ▲ or ▼.

`rolltui_windows_set_list_options`:

- `tree`: rows nest by `depth`, in tree order, a parent before its children. Guide lines draw the
  shape, and ▾ and ▸ mark open and closed nodes. Nodes shallower than `open_depth` start open
  and the rest closed; 0, the default, starts every node closed. Enter opens and closes a node; with `activate_nodes`, Enter on a node is an
  activation like any other row's.
- `marks`: several rows chosen at once. A marked row is bold in the `accent_1` colour with ▌ in
  the mark column, so it reads without colour.
- `line_numbers`: 1 numbers each row; 2 counts from the cursor's row, which shows its own number.
- `scrolloff`: rows kept between the cursor and the window's edge. Past half the window, the
  cursor stays in the middle.
- `wrap`: the cursor goes from the last row to the first and back.
- `hide_header`: no header row.

Where the terminal draws ambiguous-width glyphs two cells wide, the guides, the openers, the sort
arrows and the mark are ASCII.

### Keys and the mouse

| Action | Keys | Does |
|---|---|---|
| `list.up`, `list.down` | Up, Down, Ctrl-P, Ctrl-N | the row above, the row below |
| `list.page_up`, `list.page_down` | PageUp, PageDown | a page up, a page down |
| `list.first`, `list.last` | Home, End | the first row, the last row |
| `list.activate` | Enter | acts on the row; a tree's node opens or closes |
| `list.back` | Escape | clears the filter, then the marks |
| `list.mark` | Space | marks or unmarks the row, and moves to the next |
| `list.mark_up`, `list.mark_down` | Shift-Up, Shift-Down | marks from where the range began |
| `list.mark_all`, `list.unmark_all`, `list.invert_marks` | Alt-A, Alt-U, Alt-I | every row shown, none, the others |
| `list.open`, `list.close` | Right, Left | opens a node or goes into it; closes a node or goes up to its parent |
| `list.open_all`, `list.close_all` | Alt-Right, Alt-Left | every node |
| `list.sort` | Alt-S | sorts by the next column, then by the host's order |
| `list.reverse` | Alt-R | the sort the other way |
| `list.filter` | / | types a filter in the list's last row |

A press moves the cursor and a double click activates the row. A press in the mark column marks
the row, and a drag marks the rows it passes over. A press on ▸ or ▾ opens or closes the node. A
press on a column's title sorts by it, and a second press sorts the other way. The wheel scrolls.

A filter is space-separated terms, and a row is shown when every term matches within one of its
cells: a plain term fuzzily (`rolltui_fuzzy_match`), `'term` as written, and `!term` not at all.
Case is ignored. Enter keeps the filter and gives the keys back to the list; Escape clears it. In a
tree, a filter keeps the parents of what it matches, and shows them open.

A `ROLLTUI_LIST_SORT_TEXT` column sorts in natural order. A `ROLLTUI_LIST_SORT_NUMBER` column
sorts by the number a cell starts with, reading K, M, G and T as powers of 1024 and s, m, h and d
as an age; a cell with no number goes last in either direction. Equal cells keep the host's
order. A tree keeps the host's order and does not sort.

`rolltui_windows_list_event` pops what happened, oldest first: `MOVED`, `ACTIVATE`, `MARKED`,
`SORTED`, `OPENED`, `CLOSED` and `FILTERED`, each with the row's key (the filter's text for
`FILTERED`). `rolltui_windows_list_cursor` and `rolltui_windows_list_marked` give the keys to act
on, and `_set_cursor` and `_set_filter` set them. `rolltui_windows_list_perform` runs an action
by name, for a host's own menu entry. In a popup, Escape clears the filter, then the marks, then
closes the popup.

### Ordering and finding text

`rolltui_natural_compare(a, alen, b, blen)` orders text as a person reads it. A run of digits
compares by value, so `file2` comes before `file10` and `7` before `07`. The rest compares
case-folded with accents removed, and the bytes as written break a tie. The column browser sorts
names this way.

`rolltui_fuzzy_match(pattern, plen, text, tlen, &score, positions, cap)` answers whether the
pattern's letters appear in the text in order. Case is ignored unless the pattern has an
uppercase letter, and accents are always ignored. The score is higher for a run of letters, a
match at the start of a word, and a match early in the text, so candidates sorted by it put the
likeliest first. `positions` receives each matched letter's byte offset in the text, for
lighting what matched.

## Charts and dashboards

A `chart:<source>` window draws numbers the host binds with
`rolltui_windows_bind_chart(windows, name, len, fill, ctx, free_ctx)`. Before each frame `fill` is
handed the window's chart, cleared, and sets:

- the kind, with `rolltui_chart_set_kind`: `ROLLTUI_CHART_LINE` (the default), `ROLLTUI_CHART_BARS`
  or `ROLLTUI_CHART_SPARK`;
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

`rolltui_chart_new`, the same setters and `rolltui_chart_draw(chart, frame, scratch, area, styles,
ambiguous_wide)` draw a chart inside a host's own widget.

A dashboard is a `"grid"` in the layout file: rows of cells, each cell a node.

```json
{ "grid": [ [ { "content": "chart:cpu" }, { "content": "chart:mem" } ],
            [ { "content": "list:procs" }, { "content": "output:log" } ] ] }
```

A grid is a column whose children are rows; it is written back as that column of rows.

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

dirktui, paint and the studio post a failure as an error toast. dirktui keeps its own status-line
note for what went right.

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
reads on the drawing thread, which is what a frame dump does. dirktui lends it the run's.

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
the terminal back with the screen repainted whole. `rolltui_open_with_system` opens a file or a
URL with what the system opens it with (`open`, `xdg-open`, ShellExecuteW).

### Coloured output

A `RolltuiAnsiText` reads what a program wrote to a terminal as lines to draw: `bat`, `delta`,
`ls --color`, a log, a man page.

- Text, newlines, tabs (stops every 8 columns) and SGR are read: 16, 256 and 24-bit colour, bold,
  dim, italic, underline and reverse. A sequence or a character split across two feeds is joined.
- A carriage return not followed by a newline writes the line again, so a progress counter shows
  its last state. Erase-in-line clears the line being written.
- Backspace overstrike is read as a man page writes it: `X\bX` bold, `_\bX` underlined.
- Cursor moves, erase-in-display, OSC, DCS, APC and charset designations are swallowed. The output
  reads as what it says and never commands the app's screen.
- The lines are a ring of `ROLLTUI_ANSI_DEFAULT_LIMIT` (100,000), or what
  `rolltui_ansi_set_limit` says, so a log keeps its newest.

An `output:<source>` window shows one. The host binds it with `rolltui_windows_bind_output`.
The window follows the end as the text grows; scrolled back, it holds still until it is scrolled
to the end again. Lines wrap at its edge; `rolltui_windows_set_output_wrap(w, name, len, 0)`
clips them instead, and the wheel's left and right scroll sideways. A cell with no colour of its
own takes the window's text colour and ground, so the program's defaults are the theme's.
`rolltui_ansi_draw` draws the same text inside a host's own widget.

`rolltui-chat` runs a `!command` once it is approved: the output streams into the conversation
as a code block and the exit status follows; Esc stops it. A frame dump runs nothing.

## The clipboard, the title and the terminal

`rolltui_clipboard_copy(terminal, text, len)` sets the clipboard two ways at once. OSC 52 goes to
the terminal, the one way that reaches the person's own machine over ssh; inside tmux it is
passed through, and above 74,994 encoded bytes it is not sent. The first native tool that works
gets the text too: pbcopy; wl-copy, xclip or xsel; the Windows clipboard. `$ROLLTUI_CLIPBOARD`
names a command to feed the text to instead of the native tools.

`rolltui_terminal_set_title` sets the window's title (OSC 2). The terminal's own title is pushed
first and put back when the app leaves or hands the terminal to a program, and the app's is set
again when it takes the terminal back. Only a change is written. `rolltui_terminal_report_cwd`
reports a folder with OSC 7, so a new tab or split opens there; dirktui reports the folder it is
browsing.

`rolltui_open_terminal(&in, &out)` opens the controlling terminal by name (`/dev/tty`; the
console's CONIN$ and CONOUT$), whatever stdin and stdout are. A program whose stdout is its answer
to a shell (a file chooser, cd-on-exit) draws there and prints its answer to stdout. dirktui runs
this way.

## Pictures

`rolltui_image_load(path)` and `rolltui_image_decode(bytes)` decode PNG, JPEG, GIF (its first
frame), BMP, TGA and PNM to RGBA. A picture larger than `ROLLTUI_IMAGE_MAX_SIDE` (10,000) pixels
on a side, or than `ROLLTUI_IMAGE_MAX_BYTES` (512 MB) decoded, is refused before it is decoded,
with the reason. `rolltui_image_from_rgba` takes pixels a host made.

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

The terminal is chosen by its name (`TERM`, `TERM_PROGRAM` and the like), then by its Primary DA
reply naming sixel. Inside tmux, pictures are drawn in blocks unless the window is kitty's, when
the placeholders and their pixels are passed through. `ROLLTUI_IMAGES` (`blocks`, `kitty`,
`iterm2`, `sixel`) or `RolltuiTerminalOptions.images` chooses outright, and
`RolltuiTermFacts.images` says what was chosen. A cell's size in pixels comes from the
terminal's window size (`cell_w`, `cell_h`); where it does not say, a cell is taken to be about
twice as tall as wide.

- A kitty picture's pixels are sent once, before the frame that first shows it, and its
  placement whenever its cells change size. Its cells are text to everything else: a popup over
  it covers it, the diff sends only what changed, and leaving deletes what was sent.
- A sixel or iTerm2 picture is drawn after the diff, the cursor kept, when every one of its
  cells is still its own blank and it was not already on the screen. While anything is drawn
  over part of it, it is not sent; once that is gone, it is sent whole again. When it goes, its
  cells are written again, which erases it.
- A sixel is kept off the screen's last row, where the terminal would scroll the screen to move
  the cursor below it.

An `image:<path>` window shows a picture, centred: the one `rolltui_windows_bind_image` bound
under that name, or the file the path names (relative to the preset directory, as `file:` is),
read again when it changes. The column browser's preview shows a picture file the same way, with
its size in pixels in the head.

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

## Previews

`RolltuiPickerOptions.preview` shows the file under the cursor in the right half of the browser
(dirktui: Settings › General › Preview).

| File | Preview |
|---|---|
| Text | its lines, read whole up to 256 KB |
| Markdown | rendered: headings, lists, tables, code blocks |
| Binary | offsets, hex byte pairs and an ASCII column, read one screen at a time |

Control characters are drawn as their symbols (`␛`) and invalid bytes as `�`.

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

## File info row

`RolltuiPickerOptions.info` (dirktui: Settings › General, on by default) adds a row under the
column headings showing the mode and modification time, local and to the minute, of the entry to
the right of the cursor: a folder's above its column (`drwxr-x--- 2026-06-01 12:34`), a file's
under its preview heading. With no preview, a file gets a small pane with its name, its size
(unless a size column shows it) and the same row.

The values come from the directory listing, with no extra `stat`, and the watcher keeps them
current. The row takes one line from the columns and is left out when the browser is too short.

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

## Ownership

Every pointer in the library is one of three kinds:

- **OWNED**: one owner, freed by a named `_free` function.
- **BORROWED**: a raw pointer or view passed across the API; it never owns and never frees.
- **VALUE**: copied.

The library has no `shared_ptr`. `rolltui-ownership-test` enforces these rules.

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
