# rolltui

A small terminal-UI library, in C, with a C++ shape for consumers who want one.

`rolltui` draws a full-screen terminal application from a widget tree, a theme, key bindings
and a layout — all as data a person can edit, not code a host has to write. It grew inside
[roll](https://github.com/itscool), a local-first AI router, as the library behind that
project's own screen, and now stands on its own: it links against roll's `include/` and
`src/` nowhere, and `rolltui-boundary-test` proves that on every build rather than promising it.

## What's here

- **The library** (`c/`, `rolltui.h`) — the widget kinds (transcript, input, menu, a file
  picker, a markdown document), the effects system (a widget marks a span of cells with a
  state; a theme maps state to motion), themes, key bindings, and the layout format that ties
  a screen together as one JSON file. `rolltui.h` is the whole public API; everything under
  `c/` is internal.
- **Four hosts**, each proving a different shape of consumer:
  - `dirktui` (`examples/`) — a directory picker for the shell, in the spirit of `zoxide` or
    `atuin`: `dirk` browses, prints the chosen path, and a shell function does the rest.
    Signed, notarized, and installable via Homebrew (`brew install itscool/tap/dirktui`).
  - `rolltui-paint` (`tools/`) — a text-mode painting app with no transcript and no input,
    proving the library serves apps that aren't chat-shaped.
  - `rolltui-studio` (`tools/`) — an authoring tool for rolltui's own screens: themes,
    layouts, key bindings and menus, all editable live.
  - `tests/c_consumer_test.c` — a program compiled as plain C, proving the public header
    needs nothing else.

## Building

```sh
cmake -B build -S .
cmake --build build -j
ctest --test-dir build
```

No external dependencies beyond system `zlib` (used by the vendored `md4c` Markdown parser
under `third_party/`). Everything else — the Unicode tables, the terminal handling, the
widgets — is this repository's own.

```sh
./build/dirktui ~/src            # the directory picker
./build/rolltui-studio           # author themes, layouts, key files and menus
./build/rolltui-paint            # the text-mode painting app
```

## What the terminal is, so no host has to ask

A terminal is not what its environment says it is. Apple's Terminal on macOS 15 cannot draw
24-bit colour and is sent it anyway by any program that trusts `COLORTERM`, which a shell rc
file may export for every terminal it ever meets; the colours come out as nonsense.
`rolltui_terminal_new` therefore finds out what the terminal is — how many colours it draws,
whether its background is light or dark, how wide it draws an ambiguous-width glyph — in the
same round trip that negotiates the keyboard, remembers the answers per terminal, and applies
them itself:

- **the depth** `rolltui_swap_present` draws at is a request, never more than the terminal has;
- **the width** of every glyph is measured the way this terminal draws it;
- **a theme that says `auto`** follows the terminal's own light or dark.

A host does nothing. The answers are filed under the terminal's names and versions, the
operating system, the program's own build, and whether the session came over ssh or a
multiplexer, so a new terminal, release or machine asks again; a remembered answer is asked
about nothing at entry and re-checked after the first frame. Measured, a terminal that answers
costs 0.6 ms fresh and 0.2 ms remembered; one that answers nothing costs 80 ms once.

When it is wrong, say so: `ROLL_COLOR_DEPTH=truecolor|256|16|mono`, `ROLL_AMBIGUOUS_WIDE=1`,
`ROLL_TERM_PROBE=0` (ask only the keyboard question). `dirktui check-terminal` asks the terminal again
and says what it found and where each answer came from.

## The loop, so no host has to write it

`rolltui_run` is the loop every terminal app used to write for itself. It enters the terminal, draws each frame at
the depth the terminal has, waits for input, copies out what arrived (the terminal's events are borrowed, and
handling one can resize the app), repaints whole after a resize or a stale fact, repaints whole when a child program
such as an editor hands the terminal back, and puts the terminal back before it returns, so what the app prints
next lands on the person's own screen. An app supplies callbacks: `render` and `event` are required; `start`,
`ground`, `resized`, `facts_changed` and `settle` are not. `dirktui`, `rolltui-paint` and `rolltui-studio` all run
this way, and `rolltui-run-test` drives it on a real pty. A host of a different shape, with its own threads say,
still has `rolltui_terminal_poll` and `rolltui_terminal_present` underneath.

## What is shown is watched

Every folder that has its insides on screen, and the file being previewed, is looked at again about twice a second, so
the screen keeps up with the disk without a keypress. A file that is added, grows, is removed or renamed is on screen
within the interval; the cursor and the scroll stay where they were (by name — a selection that is gone falls to the entry
now in its place, and a reader at the top of a list stays at the top); a folder that goes takes the columns that listed it,
and the eye comes back to the deepest folder still there; a file replaced by a folder, or the other way, is followed; a
preview keeps the same line at the top even when the head of the file changes, and a reader at the foot of a log follows it
as it grows. The look is a `stat` of each folder and of the rows on screen, no descriptor held, so it costs nothing when
nothing changed (a folder of twenty thousand entries: about a tenth of a millisecond), and a folder that keeps changing
backs the interval off rather than eating the frame. It is on unless `RolltuiPickerOptions.no_watch` says otherwise, and
it needs a frame clock, so a golden frame stays a still.

## What a file looks like, and what a diagram in it looks like

The column browser can show the file under the cursor in the right half (`RolltuiPickerOptions.preview`; in
dirktui, Settings → Preview a file). A text file is its lines, read whole up to 256 KB; a Markdown file is
*rendered* — headings, lists, tables, code boxes; a binary file is offsets, byte pairs and an ASCII gutter, read a
screenful at a time so any size scrolls at once. Nothing a file holds reaches the terminal as itself: a control
character is drawn as its picture (`␛`), a stray byte as `�`.

A ```` ```mermaid ```` block in Markdown — in a preview, in a transcript, anywhere the library renders Markdown — is
drawn as the diagram it describes, in the theme's own colours, from box-drawing glyphs (or `+ - | > v` where an
ambiguous glyph is two cells). Flowcharts (all four directions, every node shape and edge style, subgraphs as
frames), sequence diagrams (with their loop / alt / opt frames, notes and activations), state diagrams, class and
ER diagrams, mind maps, timelines, user journeys, Gantt charts and pie charts are drawn; one that is not (a git
graph, say), or is wider than the room it has, is shown as its source with a line saying why. A diagram that is
too wide as written is first drawn with its nodes a column closer, then with its labels wrapped (as Mermaid wraps
its own) to a narrower and narrower width; nothing a label says is dropped, and a diagram that fits as written is
never touched.

## Source colour

A file in the preview that is in a language the library knows is drawn with its keywords, strings, comments, numbers,
functions and so on in the theme's colours, and the head of the preview names the language (`Python · 27 lines · 722 B`
instead of `text · …`). A fenced block in a Markdown document is coloured by its tag (```` ```rust ````, ```` ```yaml ````).
It is on unless `RolltuiPickerOptions.no_syntax` says otherwise (dirktui: Settings → Colour source code in the preview).

**What ships:** C, C++, C#, Java, JavaScript, TypeScript, Python, Rust, Go, Swift, Lua, Luau, shell (sh, bash, zsh), Windows
batch, PowerShell, CMake, HTML, CSS, XML, JSON, YAML, INI, TOML, the ignore files (`.gitignore` and its family:
`.dockerignore`, `.npmignore`, …) and `.gitattributes`. A file is found by its whole name (`CMakeLists.txt`, `.bashrc`), then
by its longest extension (`.d.ts` before `.ts`), then by its first line (a `#!` shebang, `<?xml`, `<!DOCTYPE html`).

**How it looks is the theme's.** Every kind of text (keyword, type, function, string, escape, number, constant, comment,
documentation, punctuation, preprocessor line, variable, attribute, tag, property, section) is drawn in a role the theme
already has, so every theme colours code and no theme had to change. On top of the role a keyword asks for **bold** and a
comment or annotation for *italic*, so a theme with no attributes of its own still has weight and slant in its code, and
`mono`, which has no colour at all, tells the kinds apart by bold, italic, underline and dim alone.

**Your own languages are files.** A language is a JSON file; the shipped ones are in `presets/syntax/` and are the best
examples. Put yours in `<config>/rolltui/syntax/` (one with the name of a shipped language replaces it, and every language
that includes it follows), and it is read when the browser starts:

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

Rules are tried in order at each position, and the first that matches takes it. A rule is a `match` (a pattern, with
`captures` to colour groups differently, and `push` / `pop` / `set` to move between contexts), a set of whole `words`, a
`region` (begin to end, with an `escape` and nested `rules`; `\\1` in `end` is what `begin` captured, which is how a
here-document and a Lua long bracket end), or an `include` of another context or another language. A context may be given a
`class` of its own (`"block": { "class": "comment", "rules": [...] }`): text no rule takes is drawn in it. The patterns are a
small, safe subset (classes, groups, `* + ? {n,m}`, lazy forms, `^ $ \\b`, look-ahead, back-references; **no** look-behind, and
a pattern that would run away is stopped, not waited for). A backslash in a pattern is written twice in JSON.

`dirktui languages` is how you find out a file of yours did not load (a language with a mistake in it is skipped without a
word on the screen): it lists what is shipped and what of yours loaded, and for each file that did not, its language, the rule
and the reason; `dirktui languages my.json` checks a file you are still writing, and it exits 1 when anything did not load.
The library's own function for it is `rolltui_syntax_check`.

**Limits, said plainly.** It is a highlighter, not a parser: it follows one line at a time from the state the last one left
(a comment or string that spans lines, a here-document), so it is right for the code people write and can be wrong for the
odd construct (a YAML block scalar's body is drawn as keys if it looks like keys). A line is coloured for its first 4000
bytes and plain after, a file is read for colour up to the same 256 KB the preview reads, and coloured text is exactly the
same characters in the same cells as plain text: colour changes a style, never a glyph.

## Ownership, in three shapes and no fourth

Every pointer in this library is one of three things, and `rolltui-ownership-test` checks it:

- **OWNED** — one owner, freed by a named `_free` function.
- **BORROWED** — a raw pointer or a view crossing an API, which never owns and never frees.
- **VALUE** — everything else, copied.

No `shared_ptr` anywhere in this library: a shared owner makes a lifetime a runtime question,
and every lifetime here is structural. A `shared_ptr` belongs to whichever host consumes this
library, never to the library itself.

## Using this inside another project

This repository's own `CMakeLists.txt` doubles as the file a parent `add_subdirectory()`s:
`if(NOT PROJECT_NAME)` guards the `project()` call and the C++ standard, so the same file
works whether it is the top of the build or one level under someone else's. `roll` does the
latter, mounting this whole repository as a git submodule at its own `rolltui/` path — a
consumer writes `#include "rolltui/rolltui.h"`, which resolves through that mount point with
no extra step; standing alone, a self-referential symlink named `rolltui` (pointing at `.`)
supplies the same prefix. The one thing the library needs that isn't under this repository is
`testkit/`, a small negative-control test harness shared by whatever else consumes it; a host
that already defines a `testkit` target is assumed to have it right, and only a build with
none yet — this repository, standing alone — vendors the copy that lives here.

## License

MIT. See [LICENSE](LICENSE).
