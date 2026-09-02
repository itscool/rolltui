# rolltui/ucd — pinned Unicode Character Database files

**Unicode 17.0.0.** Fetched by `tools/fetch_ucd.sh`, which pins the version and the
sha256 of every file and refuses a mismatch. Checked in so that a clone can regenerate
`rolltui/unicode_tables.hpp` and replay both conformance suites offline.

| file | read by | for |
|---|---|---|
| `DerivedLineBreak.txt` | generator | UAX #14 Line_Break class, with the `@missing` sub-range defaults for unassigned CJK / Plane 1-3 code points as data (LineBreak.txt states them only in prose, imprecisely — see the fetch script) |
| `DerivedEastAsianWidth.txt` | generator | UAX #11 East_Asian_Width, same reason |
| `GraphemeBreakProperty.txt` | generator | UAX #29 Grapheme_Cluster_Break |
| `DerivedCoreProperties.txt` | generator | Indic_Conjunct_Break (GB9c) and Default_Ignorable_Code_Point (width 0) |
| `DerivedGeneralCategory.txt` | generator | General_Category, for LB1 / LB15a-b / LB30b and the width function |
| `emoji-data.txt` | generator | Extended_Pictographic (GB11, LB30b, emoji cluster width) |
| `GraphemeBreakTest.txt` | `rolltui-grapheme-break-test` | the UAX #29 conformance suite, run in full |
| `LineBreakTest.txt` | `rolltui-line-break-test` | the UAX #14 conformance suite, run in full |

`rolltui-unicode-tables-current` (ctest) regenerates `unicode_tables.hpp` and
`unicode_tables.cpp` from these files and fails if the checked-in ones differ, so the
tables can never be edited by hand or fall behind the generator without a red test.

Terms of use: <https://www.unicode.org/terms_of_use.html> (the Unicode License v3).
