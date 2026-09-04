#pragma once
//
// rolltui/Unicode.hpp — the Unicode knowledge a terminal renderer needs, as pure
// functions over the generated tables (unicode_tables.hpp/.cpp). Nothing from roll.
//
//   property lookups     line_break_class, east_asian_width, grapheme_break, ...
//   UTF-8                decode_utf8 (lossy: each bad byte becomes U+FFFD, one byte
//                        consumed, so a byte offset is always recoverable)
//   widths               codepoint_width, cluster_width — cells on a terminal
//   UAX #29              grapheme_boundaries / graphemes: extended grapheme clusters,
//                        incl. GB9c (Indic conjuncts) and GB11 (emoji ZWJ sequences);
//                        word_boundaries / word_range: word boundaries (WB1-WB999),
//                        for double-click selection
//   UAX #14              line_break_opportunities: every rule LB1-LB31 as published
//                        for Unicode 17.0 (tr14-55), untailored
//   sanitising           strip_escape_sequences: removes ESC/C1-introduced control
//                        sequences so model output can never be terminal input
//
// Implementations live in Unicode.cpp (the rolltui static library). Verified by the
// three Unicode conformance suites in full (rolltui/tests/), and the width function by
// a hand table plus a cross-check against libc wcwidth over the BMP in which every
// disagreement is LISTED, not tolerated (rolltui/tests/width_test).
//
// Width is a prediction of the terminal's cursor advance, and the terminal's own
// choice is unobservable, so two things are knobs rather than facts: East Asian
// ambiguous-width characters are narrow unless `ambiguous_wide` (UAX #11 leaves the
// choice to the environment; most terminals default narrow), and grapheme clusters
// are measured the way grapheme-aware terminals (kitty, WezTerm, Ghostty, iTerm2)
// measure them — an emoji ZWJ sequence is one 2-cell cluster, a flag is 2 cells, a
// text-presentation symbol followed by VS16 is 2 cells. A legacy terminal that sums
// per-code-point widths will disagree on exactly those sequences; that disagreement is
// inherent to the sequences, not a table error.
//
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/unicode_tables.hpp"

namespace rolltui::unicode {

// ---- table lookup ------------------------------------------------------------------

// Binary search over a generated table: the ranges are sorted and disjoint, so the
// first range whose `last` is >= cp is the only candidate.
std::uint8_t lookup(const Range* table, std::size_t n, char32_t cp, std::uint8_t def);

inline LineBreak line_break_class(char32_t cp) {
  return static_cast<LineBreak>(lookup(kLineBreak, kLineBreakCount, cp,
                                       static_cast<std::uint8_t>(kLineBreakDefault)));
}
inline EastAsianWidth east_asian_width(char32_t cp) {
  return static_cast<EastAsianWidth>(lookup(kEastAsianWidth, kEastAsianWidthCount, cp,
                                            static_cast<std::uint8_t>(kEastAsianWidthDefault)));
}
inline GraphemeBreak grapheme_break(char32_t cp) {
  return static_cast<GraphemeBreak>(lookup(kGraphemeBreak, kGraphemeBreakCount, cp,
                                           static_cast<std::uint8_t>(kGraphemeBreakDefault)));
}
inline WordBreak word_break(char32_t cp) {
  return static_cast<WordBreak>(lookup(kWordBreak, kWordBreakCount, cp,
                                       static_cast<std::uint8_t>(kWordBreakDefault)));
}
inline IndicConjunctBreak indic_conjunct_break(char32_t cp) {
  return static_cast<IndicConjunctBreak>(
      lookup(kIndicConjunctBreak, kIndicConjunctBreakCount, cp,
             static_cast<std::uint8_t>(kIndicConjunctBreakDefault)));
}
inline GeneralCategory general_category(char32_t cp) {
  return static_cast<GeneralCategory>(lookup(kGeneralCategory, kGeneralCategoryCount, cp,
                                             static_cast<std::uint8_t>(kGeneralCategoryDefault)));
}
inline bool is_extended_pictographic(char32_t cp) {
  return lookup(kExtendedPictographic, kExtendedPictographicCount, cp, 0) != 0;
}
inline bool is_default_ignorable(char32_t cp) {
  return lookup(kDefaultIgnorable, kDefaultIgnorableCount, cp, 0) != 0;
}

// ---- UTF-8 -------------------------------------------------------------------------

struct DecodedChar {
  char32_t cp;
  std::size_t offset;  // byte offset into the source
  std::size_t length;  // bytes consumed (1 for an invalid byte)
  bool valid;          // false: cp is U+FFFD standing in for one malformed byte
};

// Decodes one scalar at `pos`. Overlong forms, surrogates, > U+10FFFF and truncated
// sequences are each reported as ONE invalid byte, so decoding is total and every
// byte of the input is accounted for exactly once.
DecodedChar decode_one(std::string_view s, std::size_t pos);
std::vector<DecodedChar> decode_utf8(std::string_view s);
void append_utf8(std::string& out, char32_t cp);

// ---- width -------------------------------------------------------------------------

// Cells one code point occupies on its own: 0, 1 or 2. Order of the tests matters and
// each line is a decision, not a derivation:
//   Cc / Cs                 0  controls and lone surrogates draw nothing (wcwidth says
//                              -1; a renderer strips them, so 0 is the useful answer)
//   Default_Ignorable       0  soft hyphen, ZWJ/ZWNJ, variation selectors, Hangul
//                              fillers, tags — defined to render nothing
//   Mn / Mc / Me / Cf       0  combining and format characters. Mc (spacing marks,
//                              e.g. Devanagari vowel signs) is a judgment call —
//                              Markus Kuhn's wcwidth gives them 1, macOS libc gives
//                              them 0 — settled toward the platform this runs on:
//                              measured 2026-09-01, all 258 BMP Mc are 0 in macOS
//                              wcwidth, so a terminal that defers to libc advances 0
//   Hangul V / T jamo       0  medial vowels and final consonants conjoin with the
//                              leading consonant (U+1160..11FF, D7B0..D7FF)
//   Zl / Zp                 0  line/paragraph separators (a renderer breaks on them)
//   East_Asian_Width W / F  2
//   East_Asian_Width A      2 only if ambiguous_wide
//   everything else         1
int codepoint_width(char32_t cp, bool ambiguous_wide = false);

// Cells one extended grapheme cluster occupies. The sum of its code points' widths,
// except for the sequences a grapheme-aware terminal draws as a single 2-cell glyph:
//   regional-indicator pair (a flag)                       2   (a lone RI stays 1)
//   keycap sequence (base, VS16, U+20E3)                   2
//   Extended_Pictographic base followed by VS16 anywhere   2   (emoji presentation)
//   Extended_Pictographic base with a ZWJ in the cluster   2   (emoji ZWJ sequence)
// Everything else sums, with one structural rule: a Grapheme_Cluster_Break=Extend
// code point after the base adds no cells (that is what "extend" means — it covers
// combining marks, which are already 0 alone, and the emoji skin-tone modifiers,
// which are 2 alone as a colour swatch but 0 attached to a hand). So base +
// combining marks = base, hand + modifier = 2, and an Indic conjunct = its consonants
// (kitty, WezTerm and wcwidth-summing terminals all agree on that one).
int cluster_width(std::span<const char32_t> cps, bool ambiguous_wide = false);

// ---- UAX #29: extended grapheme clusters -------------------------------------------

// boundaries[i] is true when a cluster boundary lies before cps[i]; boundaries[n] is
// the end of text. For a non-empty input boundaries[0] and boundaries[n] are true
// (GB1, GB2); for empty input the single entry is true.
std::vector<bool> grapheme_boundaries(std::span<const char32_t> cps);

struct Grapheme {
  std::size_t offset;  // byte offset of the cluster in the source string
  std::size_t length;  // bytes
  int width;           // cells (cluster_width)
};

// Clusters of a UTF-8 string with their byte spans and cell widths. Invalid bytes
// decode to U+FFFD and form clusters of their own (width 1) — a renderer shows the
// replacement character, never drops the byte.
std::vector<Grapheme> graphemes(std::string_view utf8, bool ambiguous_wide = false);
// The same, filling a vector the CALLER owns, so a draw loop can hoist it and reuse its
// capacity (Phase 13 m3). The intermediates this needs are reused internally either way,
// so `graphemes()` costs one allocation and this one costs none in a warm loop. Same
// algorithm, same answers — the conformance suites are what say so.
void graphemes_into(std::string_view utf8, bool ambiguous_wide, std::vector<Grapheme>& out);
int display_width(std::string_view utf8, bool ambiguous_wide = false);

// ---- UAX #29: word boundaries ------------------------------------------------------

// boundaries[i] is true when a word boundary lies before cps[i]; boundaries[n] is the
// end of text. Every rule WB1-WB999 as published for Unicode 17.0 (tr29-45),
// untailored: WB4 treats Extend/Format/ZWJ as transparent, WB6/WB7 and WB11/WB12 look
// one word character past a mid-letter/mid-number, WB15/WB16 pair regional
// indicators. Note that a run of spaces is one "word" (WB3d) and punctuation is one
// boundary per character (WB999) — what double-click needs, not a tokeniser.
std::vector<bool> word_boundaries(std::span<const char32_t> cps);

struct ByteRange {
  std::size_t begin = 0, end = 0;  // [begin, end) in the source string
};

// The word (per word_boundaries) containing byte `offset` of a UTF-8 string, as a byte
// range; an offset past the end returns {size, size}. Selection's double-click.
ByteRange word_range(std::string_view utf8, std::size_t offset);

// ---- sanitising ---------------------------------------------------------------------

// Removes terminal control sequences from text that will be RENDERED, never written
// through: ESC-introduced CSI (ESC [ … final), OSC (ESC ] … BEL | ESC \), DCS / SOS /
// PM / APC strings to their ST, two- and three-byte ESC sequences (ESC + intermediate*
// + final), and their 8-bit C1 forms (U+009B CSI, U+009D OSC, U+0090/98/9E/9F strings).
// The whole sequence goes, parameters and payload included — a bare strip of ESC would
// leave "[31m" on screen as text. A lone ESC with nothing that could follow it is
// dropped too. Everything else, invalid UTF-8 included, passes through byte for byte.
// (plan/phase-9.md: "Model output is data, never terminal input" — the adapter calls
// this before any text reaches a renderer.)
std::string strip_escape_sequences(std::string_view text);

// ---- UAX #14: line break opportunities ---------------------------------------------

enum class Break : std::uint8_t { Prohibited, Allowed, Mandatory };

// result[i] is the opportunity before cps[i]; result[n] is the end of text, always
// Mandatory (LB3); result[0] is always Prohibited (LB2). Untailored: LB1 resolves
// AI/SG/XX → AL, CJ → NS, SA → CM for Mn/Mc else AL, and CB is left to LB20.
std::vector<Break> line_break_opportunities(std::span<const char32_t> cps);

// Convenience over UTF-8: opportunities indexed by decoded code point, alongside the
// decode so a caller can map them back to bytes.
struct LineBreaks {
  std::vector<DecodedChar> chars;
  std::vector<Break> before;  // before[i] for chars[i]; before[chars.size()] is eot
};
LineBreaks line_breaks(std::string_view utf8);

}  // namespace rolltui::unicode
