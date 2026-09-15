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
