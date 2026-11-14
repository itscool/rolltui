# rolltui

A terminal UI library in C, with a C++ API on top.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/dirktui-dark.png">
  <img alt="dirktui, a directory browser built on rolltui, previewing a project's README: a heading, a flowchart drawn in box-drawing characters, a block of colour-highlighted C, and a table" src="docs/images/dirktui-light.png">
</picture>

*dirktui previewing a README: a Mermaid flowchart, highlighted C, and a table.*

## How it compares (November 2026)

| | rolltui | Textual | ratatui | Bubble Tea | notcurses | FTXUI |
|---|---|---|---|---|---|---|
| Platforms | 🍎 🐧 🪟 | 🍎 🐧 🪟 🌐 | 🍎 🐧 🪟 🌐 | 🍎 🐧 🪟 | 🍎 🐧 (🪟) | 🍎 🐧 🪟 🌐 |
| Language | C, C++ API | Python | Rust | Go | C | C++ |
| Dependencies | none | rich, Pygments, markdown-it, … | crossterm, ~15 crates | 10+ Go modules | terminfo, libunistring, libdeflate | none |
| **Configuration** | | | | | | |
| Layouts, themes and menus as files | ✓ | stylesheets | ✗ | ✗ | ✗ | ✗ |
| Key bindings changeable without code | ✓ | ✓ | ✗ | partial | ✗ | ✗ |
| Editor app for all of it | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Theme contrast and colour-blindness checks | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Animation chosen by the theme | ✓ | in code | tachyonfx, in code | in code | in code | in code |
| **Content** | | | | | | |
| Markdown | ✓ | ✓ | ✗ | Glamour | ✗ | ✗ |
| Mermaid diagrams | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Syntax highlighting | ✓ | ✓ | ✗ | in Glamour | ✗ | ✗ |
| File browser with live preview | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Images | ✗ | ✗ | ratatui-image | ✗ | ✓ | ✗ |
| **Widgets** | | | | | | |
| Data table | partial | ✓ | ✓ | ✓ | ✗ | partial |
| Tree | ✗ | ✓ | ✗ | ✓ | ✓ | partial |
| Tabs | ✗ | ✓ | ✓ | ✗ | ✓ | ✓ |
| Charts | ✗ | ✓ | ✓ | ✗ | ✓ | ✓ |
| Multi-line text editor | partial | ✓ | ratatui-textarea | ✓ | ✓ | partial |
| Command palette | ✓ | ✓ | ✗ | partial | ✗ | ✗ |
| **Terminal** | | | | | | |
| Capabilities probed and remembered per terminal | ✓ | ✗ | ✗ | probed | probed | ✗ |
| Line breaking tested against Unicode's test files | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Synchronized output | ✗ | ✓ | ✗ | ✓ | ✓ | ✗ |
| Inline mode | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Hyperlinks | ✓ | ✓ | ✗ | ✓ | ✗ | ✓ |

🍎 macOS · 🐧 Linux · 🪟 Windows · 🌐 browser · (🪟) partial

| | |
|---|---|
| ![rolltui-chat: a conversation with Markdown, a quoted message and a code block](docs/images/chat.png) | ![dirktui: the settings menu with the theme list open over it](docs/images/settings.png) |
| rolltui-chat · catppuccin | dirktui settings · solarized, light |
| ![dirktui: the key table over the browser](docs/images/help.png) | ![rolltui-studio: the theme editor beside a rendered document](docs/images/studio.png) |
| dirktui keys · ember | rolltui-studio theme editor · orchard |
| ![dirktui previewing a Python file](docs/images/code.png) | ![rolltui-chat: the command palette filtering for "theme"](docs/images/palette.png) |
| dirktui code preview · harbour, light | rolltui-chat command palette · mono |

## What's in the repository

- **The library**: `rolltui.h` and `c/`. `rolltui.h` is the whole public API, and everything under
  `c/` is internal. Every pointer is OWNED, BORROWED or a plain VALUE, and there is no `shared_ptr`.
- **dirktui** (`examples/dirktui.cpp`): a directory browser for the shell. `dirk` browses, prints
  the chosen path, and a shell function does the rest. `brew install itscool/tap/dirktui`; on
  Windows, `dirktui install` puts `dirk` into PowerShell's profile.
- **rolltui-studio** (`tools/studio.cpp`, `rolltui_studio.h`): edits themes, layouts, key bindings
  and menus live.
- **Examples**: `rolltui-chat` (`examples/chat.cpp`), `rolltui-paint` (`examples/paint.cpp`).

## Building

Requires CMake 3.24 or later and a C++23 compiler.

```sh
cmake -B build -S .
cmake --build build -j
ctest --test-dir build
```

```sh
./build/dirktui ~/src            # the directory browser
./build/rolltui-studio           # author themes, layouts, key bindings and menus
./build/rolltui-chat             # the chat screen with a pretend assistant
./build/rolltui-paint            # the text-mode painting app
```

### Windows

The library and the programs cross-compile with MinGW-w64. The programs link their runtime in and
need nothing else installed on Windows 10 or 11. The test suite drives programs through ptys and is
off for Windows builds.

```sh
cmake -B build-windows -S . -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build build-windows -j
```

## Documentation

[docs/guide.md](docs/guide.md): strings, terminal detection, the run loop, file watching,
previews, Mermaid diagrams, syntax highlighting and grammar files, ownership, and embedding.

## License

MIT. See [LICENSE](LICENSE). Vendored under `third_party/`, each under its own licence: md4c
(Markdown), stb_image (PNG decoding in rolltui-paint), and the Catppuccin and Solarized palettes.
