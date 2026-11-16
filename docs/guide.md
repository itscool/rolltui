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

## The run loop

`rolltui_run(in_fd, out_fd, options, app)`:

1. enters the terminal;
2. draws each frame at the terminal's colour depth;
3. waits for input and copies each event before passing it to the app;
4. repaints in full after a resize, after a cached terminal answer proves stale, and when a child
   program such as an editor returns the terminal;
5. restores the terminal before returning.

The app is a `RolltuiRunApp`: `render` and `event` are required; `start`, `ground`, `resized`,
`facts_changed` and `settle` are optional. `rolltui_run` returns `ROLLTUI_RUN_STOPPED`,
`ROLLTUI_RUN_NOT_A_TERMINAL` or `ROLLTUI_RUN_BAD_APP`.

An app with threads of its own builds its loop from `rolltui_terminal_new`,
`rolltui_terminal_poll`, `rolltui_terminal_present`, `rolltui_terminal_wake` and
`rolltui_terminal_suspend` / `_resume`. `examples/chat.cpp` does this.

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
