# Unicode Character Database files

Unicode 17.0.0, fetched by `tools/fetch_ucd.sh`, which pins the version and the sha256 of every
file. They are checked in so the Unicode tables can be regenerated and the conformance suites run
offline.

| File | Read by | For |
|---|---|---|
| `DerivedLineBreak.txt` | generator | UAX #14 Line_Break, including the `@missing` defaults for unassigned CJK and Plane 1–3 code points |
| `DerivedEastAsianWidth.txt` | generator | UAX #11 East_Asian_Width, including its `@missing` defaults |
| `GraphemeBreakProperty.txt` | generator | UAX #29 Grapheme_Cluster_Break |
| `DerivedCoreProperties.txt` | generator | Indic_Conjunct_Break (GB9c) and Default_Ignorable_Code_Point (width 0) |
| `DerivedGeneralCategory.txt` | generator | General_Category, for LB1, LB15a–b, LB30b and display width |
| `emoji-data.txt` | generator | Extended_Pictographic (GB11, LB30b, emoji width) |
| `WordBreakProperty.txt` | generator | UAX #29 Word_Break, for word boundaries |
| `GraphemeBreakTest.txt` | `rolltui-grapheme-break-test` | the UAX #29 grapheme conformance suite, in full |
| `LineBreakTest.txt` | `rolltui-line-break-test` | the UAX #14 conformance suite, in full |
| `WordBreakTest.txt` | `rolltui-word-break-test` | the UAX #29 word conformance suite, in full |

The generator is `tools/gen_unicode_tables.cpp`; it writes `unicode_tables.c` and
`unicode_tables.h`. The `rolltui-unicode-tables-current` test regenerates both and fails if the
checked-in files differ.

Terms of use: <https://www.unicode.org/terms_of_use.html> (the Unicode License v3).
