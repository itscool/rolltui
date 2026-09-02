
---

## 2026-09-01 — Phase 9 m3: the wrap engine, property-tested at every width (Fable 5.1 @ default)
**Did:** `rolltui/Wrap.hpp` and `rolltui/tests/wrap_test.cpp` (details in the plan's
milestone note). The user asked to "chug through to milestone 7"; this is the first of
m3-m7 in one session, each its own commit.
**Decisions:** three rules the plan's list under-specified, settled here and stated in
the header: a grapheme wider than the width overflows rather than vanishes (the
properties say so explicitly); trailing spaces drop at soft breaks only, so a typed
trailing space keeps its cell; a trailing newline is a line terminator, not a line.
The "300-cell URL" case became a hex hash — UAX #14 breaks a URL at its slashes, which
is right, and my first expected output was wrong, not the engine.
**Broke / open:** the first end-of-text rule lost the last line after a soft break
(`pending` was cleared by the break and never re-set by the append that followed) —
caught by property 2 on the very first corpus string, fixed by setting it in `append`.
ESC sequences are not interpreted here; ESC is stripped as a control, so the adapter
(m9) must strip model output before wrapping, as the plan already says. Next: m4.

---

## 2026-09-01 — Phase 9 m2: Unicode tables; both conformance suites green; the width list is live (Fable 5.1 @ default)
**Did:** milestone 2 of `plan/phase-9.md`, in two commits so the header can name the
generator that made it.
- **Commit 1 — the acquisition.** `tools/fetch_ucd.sh` pins Unicode **17.0.0** (18.0.0
  on unicode.org is a draft redirect) and the sha256 of eight UCD files, checked in
  under `rolltui/ucd/` (5.3 MB, `LineBreakTest.txt` alone 3.1 MB). `tools/
  gen_unicode_tables.cpp` reads them and emits one header of seven range tables, with
  `--check` as a regenerate-and-diff. A value name it does not know is an error, not an
  "other" bucket.
- **Commit 2 — the library and its proof.** `rolltui/unicode_tables.hpp` (generated,
  276 KB), `rolltui/Unicode.hpp` (UTF-8, UAX #29 clusters, UAX #14 all rules LB1-LB31
  per tr14-55, widths), `rolltui/CMakeLists.txt` (INTERFACE target, include path = repo
  root, tests with their own harness so roll's `include/` is absent by construction),
  and four `ctest` tests: the two conformance suites **in full** (19,338 + 766 cases),
  the width hand table + BMP cross-check, and the header-is-current control.
  `ctest` 20/20.
- **Every new assertion was seen to fail by name first** (CLAUDE.md's control rule):
  the line-break suite failed 71 cases on its own before the fix below; then, against
  patched copies in the scratchpad, GB11 disabled → 4 grapheme cases; the Mc width rule
  dropped → the hand-table line and 26 unlisted disagreements; a known entry narrowed
  → U+31EF unlisted; a known entry emptied → "still applies (0 code points)"; one byte
  changed in the header → stale at line 1164.
**Decisions:**
- **The plan's file list was wrong twice, and the work says so.** (1) The 71 failures
  were all U+1F8FF, unassigned + Extended_Pictographic: `LineBreak.txt`'s header prose
  says U+1F000..U+1FAFF default to ID, but `extracted/DerivedLineBreak.txt` carries the
  same defaults as `@missing` data lines and says 1F000..1F7FF and 1F900..1FAFF — the
  suite follows the data. My first generator hard-coded the prose ranges and even
