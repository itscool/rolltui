<!-- user -->
write me a plan for the wrap engine, with a table and some code
<!-- assistant -->
# Plan

1. First step
   - a nested bullet with **bold** and `code`
   - another, with a [link](https://example.com/docs)
2. Second step

   A loose paragraph inside the second item.

   ```sh
   cmake --build build -j
   ```
3. Third ~~struck~~ step

- [ ] todo item
- [x] done item

> A quote with *emphasis*
> that continues on a second line
>
> and a second paragraph.

---

Hard break here  
next line. Entities: &amp; &lt; &gt; &#65; &#x42; &copy; and a NUL: ab.
Autolink https://example.com/path and <https://x.y/z> too.

<!-- note -->
[roll] escalated to cloud: capability gap (tools)
<!-- user -->
now the comparison table from the plan please
<!-- assistant -->
**Unicode data libraries — measured 2026-09-01, shallow clones at HEAD:**

| library | what we'd use | size at HEAD | verdict |
|---|---|---|---|
| utf8proc (MIT) | `charwidth`, grapheme breaks | **2.4 MB** of C, 17,141 lines of it a normalization/case table we would never call | too heavy for two functions — **rejected** |
| libunibreak (zlib) | UAX #14 line breaks | 15 C files, 774 KB, autoconf build, Unicode 15.0 for line breaking | not single-header, lags Unicode — **rejected** |
| md4c (MIT) | CommonMark 0.31 + GFM parse | **one `.c` (7,341 lines) + one `.h` (495)**, ~280 KB, zero deps, SAX callbacks, last commit 2026-08-30 | lightweight, current, and the alternative is re-deriving 650+ spec examples — **vendored** |

**So: one third-party file pair (md4c, vendored under `rolltui/third_party/`, behind our
own block-tree API so it is an implementation detail); everything else is ours.**
The Unicode knowledge the two rejected libraries carry is a few thousand codepoint
ranges, and Unicode publishes both the data and a conformance suite for it —
`auxiliary/LineBreakTest.txt` alone is **19,339 cases** (counted 2026-09-01), plus
`GraphemeBreakTest.txt`. That makes "generate thin glue and validate it" the correct tier:
a generator in `tools/` reads the UCD files, emits **one checked-in header** of range
tables, and the conformance suites run in `ctest`. When Unicode releases, re-run the
generator and re-run the suites — the same re-fetch + re-verify rung the languages
table describes for grammars.

| component | decision | shape |
|---|---|---|
| Unicode tables (line-break class, East-Asian width, grapheme property, Extended_Pictographic, InCB) | **generate** | `tools/gen_unicode_tables.cpp` (C++, not Python) → `rolltui/unicode_tables.hpp`, checked in |
| UAX #14 line breaking, UAX #29 grapheme clusters, display width | **build** | single header, validated by the Unicode conformance suites |
| CommonMark + GFM parsing | **vendor md4c** | two files; our `Markdown.hpp` block tree is the API |
| Terminal I/O (raw, alt screen, size, SIGWINCH, bracketed paste, mouse mode) | **build** | the one compiled unit; half exists in `LineReader`/`StatusPane` |
| Key + mouse decoding | **build** | single header, pure `bytes → Event`, table-tested |
| Cell grid + frame diff + SGR/colour downgrade | **build** | single header, pure, golden-tested |
| Wrap engine | **build** | single header over the Unicode header |
| Windows, layers, placement (`Dim` abs/rel per axis), focus | **build** | single header; `resolve(Placement, parent) → Rect` is a pure function |
| Menu widget (hierarchical, filterable) + list/text widgets | **build** | single header each; the widget set is deliberately this small |
| Layout files (JSON: windows + placements + content slots) | **build** | reuses the theme loader's hand parser; hot-reloaded |
| Markdown block renderer (blocks → styled wrapped lines) | **build** | single header over md4c + wrap |
| Theme (roles → styles, JSON loader, built-ins) | **build** | single header |

<!-- assistant -->
Here is the function you asked for:

```cpp
int add(int a, int b) {
  return a + b;
