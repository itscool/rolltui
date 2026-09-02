#pragma once
//
// rolltui/Unicode.hpp — the Unicode knowledge a terminal renderer needs, as pure
// functions over the generated tables in unicode_tables.hpp. Header-only, no
// dependencies, nothing from roll.
//
//   property lookups     line_break_class, east_asian_width, grapheme_break, ...
//   UTF-8                decode_utf8 (lossy: each bad byte becomes U+FFFD, one byte
//                        consumed, so a byte offset is always recoverable)
//   widths               codepoint_width, cluster_width — cells on a terminal
//   UAX #29              grapheme_boundaries / graphemes: extended grapheme clusters,
//                        incl. GB9c (Indic conjuncts) and GB11 (emoji ZWJ sequences)
//   UAX #14              line_break_opportunities: every rule LB1-LB31 as published
//                        for Unicode 17.0 (tr14-55), untailored
//
// Verified by the two Unicode conformance suites in full (rolltui/tests/), and the
// width function by a hand table plus a cross-check against libc wcwidth over the
// BMP in which every disagreement is LISTED, not tolerated (rolltui/tests/width_test).
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
template <std::size_t N>
inline std::uint8_t lookup(const Range (&table)[N], char32_t cp, std::uint8_t def) {
  std::size_t lo = 0, hi = N;
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo) / 2;
    if (table[mid].last < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < N && table[lo].first <= cp && cp <= table[lo].last) return table[lo].value;
  return def;
}

inline LineBreak line_break_class(char32_t cp) {
  return static_cast<LineBreak>(
      lookup(kLineBreak, cp, static_cast<std::uint8_t>(kLineBreakDefault)));
}
inline EastAsianWidth east_asian_width(char32_t cp) {
  return static_cast<EastAsianWidth>(
      lookup(kEastAsianWidth, cp, static_cast<std::uint8_t>(kEastAsianWidthDefault)));
}
inline GraphemeBreak grapheme_break(char32_t cp) {
  return static_cast<GraphemeBreak>(
      lookup(kGraphemeBreak, cp, static_cast<std::uint8_t>(kGraphemeBreakDefault)));
}
inline IndicConjunctBreak indic_conjunct_break(char32_t cp) {
  return static_cast<IndicConjunctBreak>(lookup(
      kIndicConjunctBreak, cp, static_cast<std::uint8_t>(kIndicConjunctBreakDefault)));
}
inline GeneralCategory general_category(char32_t cp) {
  return static_cast<GeneralCategory>(
      lookup(kGeneralCategory, cp, static_cast<std::uint8_t>(kGeneralCategoryDefault)));
}
inline bool is_extended_pictographic(char32_t cp) {
  return lookup(kExtendedPictographic, cp, 0) != 0;
}
inline bool is_default_ignorable(char32_t cp) { return lookup(kDefaultIgnorable, cp, 0) != 0; }

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
inline DecodedChar decode_one(std::string_view s, std::size_t pos) {
  auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
  unsigned char b0 = byte(pos);
  if (b0 < 0x80) return {b0, pos, 1, true};
  std::size_t need;
  char32_t cp;
  char32_t min;
  if ((b0 & 0xE0) == 0xC0) { need = 1; cp = b0 & 0x1F; min = 0x80; }
  else if ((b0 & 0xF0) == 0xE0) { need = 2; cp = b0 & 0x0F; min = 0x800; }
  else if ((b0 & 0xF8) == 0xF0) { need = 3; cp = b0 & 0x07; min = 0x10000; }
  else return {0xFFFD, pos, 1, false};
  if (pos + need >= s.size()) return {0xFFFD, pos, 1, false};  // truncated sequence
  for (std::size_t i = 1; i <= need; ++i) {
    unsigned char b = byte(pos + i);
    if ((b & 0xC0) != 0x80) return {0xFFFD, pos, 1, false};
    cp = (cp << 6) | (b & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
    return {0xFFFD, pos, 1, false};
  return {cp, pos, need + 1, true};
}

inline std::vector<DecodedChar> decode_utf8(std::string_view s) {
  std::vector<DecodedChar> out;
  out.reserve(s.size());
  for (std::size_t pos = 0; pos < s.size();) {
    DecodedChar d = decode_one(s, pos);
    out.push_back(d);
    pos += d.length;
  }
  return out;
}

inline void append_utf8(std::string& out, char32_t cp) {
  if (cp < 0x80) out.push_back(static_cast<char>(cp));
  else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

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
inline int codepoint_width(char32_t cp, bool ambiguous_wide = false) {
  GeneralCategory gc = general_category(cp);
  if (gc == GeneralCategory::Cc || gc == GeneralCategory::Cs) return 0;
  if (is_default_ignorable(cp)) return 0;
  if (gc == GeneralCategory::Mn || gc == GeneralCategory::Mc || gc == GeneralCategory::Me ||
      gc == GeneralCategory::Cf)
    return 0;
  GraphemeBreak gb = grapheme_break(cp);
  if (gb == GraphemeBreak::V || gb == GraphemeBreak::T) return 0;
  if (gc == GeneralCategory::Zl || gc == GeneralCategory::Zp) return 0;
  EastAsianWidth ea = east_asian_width(cp);
  if (ea == EastAsianWidth::W || ea == EastAsianWidth::F) return 2;
  if (ea == EastAsianWidth::A && ambiguous_wide) return 2;
  return 1;
}

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
inline int cluster_width(std::span<const char32_t> cps, bool ambiguous_wide = false) {
  if (cps.empty()) return 0;
  int ri = 0, sum = 0;
  bool vs16 = false, keycap = false, zwj = false;
  for (std::size_t i = 0; i < cps.size(); ++i) {
    char32_t cp = cps[i];
    GraphemeBreak gb = grapheme_break(cp);
    if (gb == GraphemeBreak::Regional_Indicator) ++ri;
    if (cp == 0xFE0F) vs16 = true;
    if (cp == 0x20E3) keycap = true;
    if (cp == 0x200D) zwj = true;
    if (i > 0 && gb == GraphemeBreak::Extend) continue;
    sum += codepoint_width(cp, ambiguous_wide);
  }
  if (ri >= 2) return 2;
  if (keycap) return 2;
  if (is_extended_pictographic(cps[0]) && (vs16 || zwj)) return 2;
  return sum;
}

// ---- UAX #29: extended grapheme clusters -------------------------------------------

// boundaries[i] is true when a cluster boundary lies before cps[i]; boundaries[n] is
// the end of text. For a non-empty input boundaries[0] and boundaries[n] are true
// (GB1, GB2); for empty input the single entry is true.
inline std::vector<bool> grapheme_boundaries(std::span<const char32_t> cps) {
  const std::size_t n = cps.size();
  std::vector<bool> b(n + 1, false);
  b[0] = true;
  b[n] = true;
  if (n < 2) return b;
  std::vector<GraphemeBreak> g(n);
  std::vector<IndicConjunctBreak> incb(n);
  std::vector<bool> pict(n);
  for (std::size_t i = 0; i < n; ++i) {
    g[i] = grapheme_break(cps[i]);
    incb[i] = indic_conjunct_break(cps[i]);
    pict[i] = is_extended_pictographic(cps[i]);
  }
  using GB = GraphemeBreak;
  using IC = IndicConjunctBreak;
  for (std::size_t i = 1; i < n; ++i) {
    GB p = g[i - 1], q = g[i];
    bool brk;
    if (p == GB::CR && q == GB::LF) brk = false;                                   // GB3
    else if (p == GB::Control || p == GB::CR || p == GB::LF) brk = true;           // GB4
    else if (q == GB::Control || q == GB::CR || q == GB::LF) brk = true;           // GB5
    else if (p == GB::L && (q == GB::L || q == GB::V || q == GB::LV || q == GB::LVT))
      brk = false;                                                                 // GB6
    else if ((p == GB::LV || p == GB::V) && (q == GB::V || q == GB::T)) brk = false;  // GB7
    else if ((p == GB::LVT || p == GB::T) && q == GB::T) brk = false;             // GB8
    else if (q == GB::Extend || q == GB::ZWJ) brk = false;                         // GB9
    else if (q == GB::SpacingMark) brk = false;                                    // GB9a
    else if (p == GB::Prepend) brk = false;                                        // GB9b
    else {
      brk = true;
      // GB9c: Consonant [Extend Linker]* Linker [Extend Linker]* × Consonant
      if (incb[i] == IC::Consonant) {
        bool linker = false;
        std::size_t j = i;
        while (j > 0 && (incb[j - 1] == IC::Extend || incb[j - 1] == IC::Linker)) {
          if (incb[j - 1] == IC::Linker) linker = true;
          --j;
        }
        if (linker && j > 0 && incb[j - 1] == IC::Consonant) brk = false;
      }
      // GB11: ExtPict Extend* ZWJ × ExtPict
      if (brk && pict[i] && p == GB::ZWJ) {
        std::size_t j = i - 1;
        while (j > 0 && g[j - 1] == GB::Extend) --j;
        if (j > 0 && pict[j - 1]) brk = false;
      }
      // GB12/13: break between RIs only after an even run of them
      if (brk && p == GB::Regional_Indicator && q == GB::Regional_Indicator) {
        std::size_t run = 0, j = i;
        while (j > 0 && g[j - 1] == GB::Regional_Indicator) { ++run; --j; }
        if (run % 2 == 1) brk = false;
      }
    }
    b[i] = brk;
  }
  return b;
}

struct Grapheme {
  std::size_t offset;  // byte offset of the cluster in the source string
  std::size_t length;  // bytes
  int width;           // cells (cluster_width)
};

// Clusters of a UTF-8 string with their byte spans and cell widths. Invalid bytes
// decode to U+FFFD and form clusters of their own (width 1) — a renderer shows the
// replacement character, never drops the byte.
inline std::vector<Grapheme> graphemes(std::string_view utf8, bool ambiguous_wide = false) {
  std::vector<DecodedChar> dc = decode_utf8(utf8);
  std::vector<char32_t> cps(dc.size());
  for (std::size_t i = 0; i < dc.size(); ++i) cps[i] = dc[i].cp;
  std::vector<bool> b = grapheme_boundaries(cps);
  std::vector<Grapheme> out;
  std::size_t start = 0;
  for (std::size_t i = 1; i <= cps.size(); ++i) {
    if (!b[i]) continue;
    std::size_t byte0 = dc[start].offset;
    std::size_t byte1 = dc[i - 1].offset + dc[i - 1].length;
    out.push_back({byte0, byte1 - byte0,
                   cluster_width(std::span<const char32_t>(cps).subspan(start, i - start),
                                 ambiguous_wide)});
    start = i;
  }
  return out;
}

inline int display_width(std::string_view utf8, bool ambiguous_wide = false) {
  int w = 0;
  for (const Grapheme& g : graphemes(utf8, ambiguous_wide)) w += g.width;
  return w;
}

// ---- UAX #14: line break opportunities ---------------------------------------------

enum class Break : std::uint8_t { Prohibited, Allowed, Mandatory };

// result[i] is the opportunity before cps[i]; result[n] is the end of text, always
// Mandatory (LB3); result[0] is always Prohibited (LB2). Untailored: LB1 resolves
// AI/SG/XX → AL, CJ → NS, SA → CM for Mn/Mc else AL, and CB is left to LB20.
inline std::vector<Break> line_break_opportunities(std::span<const char32_t> cps) {
  using LB = LineBreak;
  using EA = EastAsianWidth;
  using GC = GeneralCategory;
  const std::size_t n = cps.size();
  std::vector<Break> out(n + 1, Break::Prohibited);
  out[n] = Break::Mandatory;
  if (n == 0) return out;

  // LB9/LB10 are applied structurally: the text is first cut into units — a base
  // character with every CM/ZWJ attached to it (LB9), or a lone CM/ZWJ that had no
  // eligible base and so becomes AL with U+0041's properties (LB10). A unit carries
  // its base's class and the properties later rules read (East Asian width, Pi/Pf
  // for quotation marks, the dotted circle, Extended_Pictographic ∧ Cn). Positions
  // inside a unit are never breaks; every rule below runs between units.
  struct Unit {
    LB cls;
    bool east_asian;   // ea ∈ {F, W, H} — $EastAsian in LB19a / LB30
    bool pi, pf;       // gc of a QU base
    bool dotted;       // U+25CC, the [◌] of LB28a
    bool pict_cn;      // Extended_Pictographic ∧ Cn, for LB30b
    bool ends_zwj;     // last code point is U+200D, for LB8a
    std::size_t first; // index of the base in cps
  };
  auto resolved = [&](char32_t cp) -> LB {
    LB c = line_break_class(cp);
    switch (c) {
      case LB::AI: case LB::SG: case LB::XX: return LB::AL;
      case LB::CJ: return LB::NS;
      case LB::SA: {
        GC gc = general_category(cp);
        return (gc == GC::Mn || gc == GC::Mc) ? LB::CM : LB::AL;
      }
      default: return c;
    }
  };
  auto no_attach = [](LB c) {
    return c == LB::BK || c == LB::CR || c == LB::LF || c == LB::NL || c == LB::SP ||
           c == LB::ZW;
  };
  std::vector<Unit> u;
  u.reserve(n);
  std::vector<std::size_t> unit_of(n);
  for (std::size_t i = 0; i < n; ++i) {
    LB c = resolved(cps[i]);
    bool joiner = (c == LB::CM || c == LB::ZWJ);
    if (joiner && !u.empty() && !no_attach(u.back().cls)) {
      u.back().ends_zwj = (c == LB::ZWJ);
      unit_of[i] = u.size() - 1;
      continue;
    }
    Unit x;
    x.first = i;
    x.ends_zwj = (c == LB::ZWJ);
    if (joiner) {  // LB10
      x.cls = LB::AL;
      x.east_asian = false;
      x.pi = x.pf = x.dotted = x.pict_cn = false;
    } else {
      x.cls = c;
      EA ea = east_asian_width(cps[i]);
      x.east_asian = (ea == EA::F || ea == EA::W || ea == EA::H);
      GC gc = general_category(cps[i]);
      x.pi = (c == LB::QU && gc == GC::Pi);
      x.pf = (c == LB::QU && gc == GC::Pf);
      x.dotted = (cps[i] == 0x25CC);
      x.pict_cn = (gc == GC::Cn && is_extended_pictographic(cps[i]));
    }
    unit_of[i] = u.size();
    u.push_back(x);
  }
  // Note: a unit whose base is itself a lone CM (LB10 → AL) still collects later
  // CM/ZWJ into itself, because its class AL is attachable — "SP CM CM" is one AL.

  const std::size_t m = u.size();
  auto cls = [&](std::size_t k) { return u[k].cls; };
  auto is = [](LB c, std::initializer_list<LB> set) {
    for (LB s : set) if (c == s) return true;
    return false;
  };
  // Index of the last unit before k that is not SP (or -1): the "SP*" lookbehind
  // that LB8, LB14, LB15a, LB16 and LB17 share.
  auto before_spaces = [&](std::size_t k) -> long {
    long j = static_cast<long>(k) - 1;
    while (j >= 0 && u[j].cls == LB::SP) --j;
    return j;
  };
  // Index of the last unit before k that is not SY/IS (or -1): LB25's "(SY|IS)*".
  auto before_sy_is = [&](std::size_t k) -> long {
    long j = static_cast<long>(k) - 1;
    while (j >= 0 && (u[j].cls == LB::SY || u[j].cls == LB::IS)) --j;
    return j;
  };

  auto decide = [&](std::size_t k) -> Break {  // boundary before unit k, 1 <= k < m
    const Unit& P = u[k - 1];
    const Unit& N = u[k];
    const LB p = P.cls, q = N.cls;
    const bool has_next = (k + 1 < m);
    const LB next2 = has_next ? cls(k + 1) : LB::XX;  // the unit after N (if any)
    const bool has_prev2 = (k >= 2);
    const LB prev2 = has_prev2 ? cls(k - 2) : LB::XX;  // the unit before P (if any)

    // LB4, LB5
    if (p == LB::BK) return Break::Mandatory;
    if (p == LB::CR && q == LB::LF) return Break::Prohibited;
    if (p == LB::CR || p == LB::LF || p == LB::NL) return Break::Mandatory;
    // LB6
    if (is(q, {LB::BK, LB::CR, LB::LF, LB::NL})) return Break::Prohibited;
    // LB7
    if (q == LB::SP || q == LB::ZW) return Break::Prohibited;
    // LB8: ZW SP* ÷
    {
      long j = before_spaces(k);
      if (j >= 0 && u[j].cls == LB::ZW) return Break::Allowed;
    }
    // LB8a: ZWJ ×
    if (P.ends_zwj) return Break::Prohibited;
    // LB9, LB10: structural (units)
    // LB11
    if (q == LB::WJ || p == LB::WJ) return Break::Prohibited;
    // LB12
    if (p == LB::GL) return Break::Prohibited;
    // LB12a: [^SP BA HY HH] × GL
    if (q == LB::GL && !is(p, {LB::SP, LB::BA, LB::HY, LB::HH})) return Break::Prohibited;
    // LB13
    if (is(q, {LB::CL, LB::CP, LB::EX, LB::SY})) return Break::Prohibited;
    // LB14: OP SP* ×
    {
      long j = before_spaces(k);
      if (j >= 0 && u[j].cls == LB::OP) return Break::Prohibited;
    }
    // LB15a: (sot | BK | CR | LF | NL | OP | QU | GL | SP | ZW) [\p{Pi}&QU] SP* ×
    {
      long j = before_spaces(k);
      if (j >= 0 && u[j].cls == LB::QU && u[j].pi) {
        bool ctx = (j == 0) || is(u[j - 1].cls, {LB::BK, LB::CR, LB::LF, LB::NL, LB::OP,
                                                  LB::QU, LB::GL, LB::SP, LB::ZW});
        if (ctx) return Break::Prohibited;
      }
    }
    // LB15b: × [\p{Pf}&QU] (SP | GL | WJ | CL | QU | CP | EX | IS | SY | BK | CR | LF |
    //                        NL | ZW | eot)
    if (q == LB::QU && N.pf) {
      bool ctx = !has_next || is(next2, {LB::SP, LB::GL, LB::WJ, LB::CL, LB::QU, LB::CP,
                                         LB::EX, LB::IS, LB::SY, LB::BK, LB::CR, LB::LF,
                                         LB::NL, LB::ZW});
      if (ctx) return Break::Prohibited;
    }
    // LB15c: SP ÷ IS NU
    if (p == LB::SP && q == LB::IS && has_next && next2 == LB::NU) return Break::Allowed;
    // LB15d: × IS
    if (q == LB::IS) return Break::Prohibited;
    // LB16: (CL | CP) SP* × NS
    if (q == LB::NS) {
      long j = before_spaces(k);
      if (j >= 0 && (u[j].cls == LB::CL || u[j].cls == LB::CP)) return Break::Prohibited;
    }
    // LB17: B2 SP* × B2
    if (q == LB::B2) {
      long j = before_spaces(k);
      if (j >= 0 && u[j].cls == LB::B2) return Break::Prohibited;
    }
    // LB18: SP ÷
    if (p == LB::SP) return Break::Allowed;
    // LB19: × [QU - \p{Pi}] ; [QU - \p{Pf}] ×
    if (q == LB::QU && !N.pi) return Break::Prohibited;
    if (p == LB::QU && !P.pf) return Break::Prohibited;
    // LB19a: unless surrounded by East Asian characters, do not break either side of QU
    if (q == LB::QU && !P.east_asian) return Break::Prohibited;
    if (q == LB::QU && (!has_next || !u[k + 1].east_asian)) return Break::Prohibited;
    if (p == LB::QU && !N.east_asian) return Break::Prohibited;
    if (p == LB::QU && (!has_prev2 || !u[k - 2].east_asian)) return Break::Prohibited;
    // LB20: ÷ CB ; CB ÷
    if (q == LB::CB || p == LB::CB) return Break::Allowed;
    // LB20a: (sot | BK | CR | LF | NL | SP | ZW | CB | GL) (HY | HH) × (AL | HL)
    if ((p == LB::HY || p == LB::HH) && (q == LB::AL || q == LB::HL)) {
      bool ctx = !has_prev2 ||
                 is(prev2, {LB::BK, LB::CR, LB::LF, LB::NL, LB::SP, LB::ZW, LB::CB, LB::GL});
      if (ctx) return Break::Prohibited;
    }
    // LB21: × BA ; × HH ; × HY ; × NS ; BB ×
    if (is(q, {LB::BA, LB::HH, LB::HY, LB::NS})) return Break::Prohibited;
    if (p == LB::BB) return Break::Prohibited;
    // LB21a: HL (HY | HH) × [^HL]
    if ((p == LB::HY || p == LB::HH) && has_prev2 && prev2 == LB::HL && q != LB::HL)
      return Break::Prohibited;
    // LB21b: SY × HL
    if (p == LB::SY && q == LB::HL) return Break::Prohibited;
    // LB22: × IN
    if (q == LB::IN) return Break::Prohibited;
    // LB23: (AL | HL) × NU ; NU × (AL | HL)
    if ((p == LB::AL || p == LB::HL) && q == LB::NU) return Break::Prohibited;
    if (p == LB::NU && (q == LB::AL || q == LB::HL)) return Break::Prohibited;
    // LB23a: PR × (ID | EB | EM) ; (ID | EB | EM) × PO
    if (p == LB::PR && is(q, {LB::ID, LB::EB, LB::EM})) return Break::Prohibited;
    if (is(p, {LB::ID, LB::EB, LB::EM}) && q == LB::PO) return Break::Prohibited;
    // LB24: (PR | PO) × (AL | HL) ; (AL | HL) × (PR | PO)
    if ((p == LB::PR || p == LB::PO) && (q == LB::AL || q == LB::HL)) return Break::Prohibited;
    if ((p == LB::AL || p == LB::HL) && (q == LB::PR || q == LB::PO)) return Break::Prohibited;
    // LB25 (the fifteen sub-rules)
    {
      // NU (SY | IS)* (CL | CP) × (PO | PR)
      if ((p == LB::CL || p == LB::CP) && (q == LB::PO || q == LB::PR)) {
        long j = before_sy_is(k - 1);
        if (j >= 0 && u[j].cls == LB::NU) return Break::Prohibited;
      }
      // NU (SY | IS)* × (PO | PR)
      if (q == LB::PO || q == LB::PR) {
        long j = before_sy_is(k);
        if (j >= 0 && u[j].cls == LB::NU) return Break::Prohibited;
      }
      // (PO | PR) × OP NU ; (PO | PR) × OP IS NU
      if ((p == LB::PO || p == LB::PR) && q == LB::OP && has_next) {
        if (next2 == LB::NU) return Break::Prohibited;
        if (next2 == LB::IS && k + 2 < m && cls(k + 2) == LB::NU) return Break::Prohibited;
      }
      // (PO | PR) × NU ; HY × NU ; IS × NU
      if (is(p, {LB::PO, LB::PR, LB::HY, LB::IS}) && q == LB::NU) return Break::Prohibited;
      // NU (SY | IS)* × NU
      if (q == LB::NU) {
        long j = before_sy_is(k);
        if (j >= 0 && u[j].cls == LB::NU) return Break::Prohibited;
      }
    }
    // LB26: JL × (JL | JV | H2 | H3) ; (JV | H2) × (JV | JT) ; (JT | H3) × JT
    if (p == LB::JL && is(q, {LB::JL, LB::JV, LB::H2, LB::H3})) return Break::Prohibited;
    if ((p == LB::JV || p == LB::H2) && (q == LB::JV || q == LB::JT)) return Break::Prohibited;
    if ((p == LB::JT || p == LB::H3) && q == LB::JT) return Break::Prohibited;
    // LB27: (JL | JV | JT | H2 | H3) × PO ; PR × (JL | JV | JT | H2 | H3)
    if (is(p, {LB::JL, LB::JV, LB::JT, LB::H2, LB::H3}) && q == LB::PO) return Break::Prohibited;
    if (p == LB::PR && is(q, {LB::JL, LB::JV, LB::JT, LB::H2, LB::H3})) return Break::Prohibited;
    // LB28: (AL | HL) × (AL | HL)
    if ((p == LB::AL || p == LB::HL) && (q == LB::AL || q == LB::HL)) return Break::Prohibited;
    // LB28a: Brahmic orthographic syllables, with [◌] = U+25CC
    {
      auto ak = [&](const Unit& x) { return x.cls == LB::AK || x.dotted; };
      auto ak_as = [&](const Unit& x) { return ak(x) || x.cls == LB::AS; };
      if (p == LB::AP && ak_as(N)) return Break::Prohibited;                       // AP × (AK|◌|AS)
      if (ak_as(P) && (q == LB::VF || q == LB::VI)) return Break::Prohibited;      // (AK|◌|AS) × (VF|VI)
      if (p == LB::VI && has_prev2 && ak_as(u[k - 2]) && ak(N)) return Break::Prohibited;  // (AK|◌|AS) VI × (AK|◌)
      if (ak_as(P) && ak_as(N) && has_next && next2 == LB::VF) return Break::Prohibited;   // (AK|◌|AS) × (AK|◌|AS) VF
    }
    // LB29: IS × (AL | HL)
    if (p == LB::IS && (q == LB::AL || q == LB::HL)) return Break::Prohibited;
    // LB30: (AL | HL | NU) × [OP-$EastAsian] ; [CP-$EastAsian] × (AL | HL | NU)
    if (is(p, {LB::AL, LB::HL, LB::NU}) && q == LB::OP && !N.east_asian) return Break::Prohibited;
    if (p == LB::CP && !P.east_asian && is(q, {LB::AL, LB::HL, LB::NU})) return Break::Prohibited;
    // LB30a: break between RIs only after an even number of them
    if (p == LB::RI && q == LB::RI) {
      std::size_t run = 0;
      for (long j = static_cast<long>(k) - 1; j >= 0 && u[j].cls == LB::RI; --j) ++run;
      if (run % 2 == 1) return Break::Prohibited;
    }
    // LB30b: EB × EM ; [\p{Extended_Pictographic}&\p{Cn}] × EM
    if (q == LB::EM && (p == LB::EB || P.pict_cn)) return Break::Prohibited;
    // LB31
    return Break::Allowed;
  };

  for (std::size_t k = 1; k < m; ++k) out[u[k].first] = decide(k);
  // LB3: eot is a mandatory break, LB2: sot never is — both set at construction.
  return out;
}

// Convenience over UTF-8: opportunities indexed by decoded code point, alongside the
// decode so a caller can map them back to bytes.
struct LineBreaks {
  std::vector<DecodedChar> chars;
  std::vector<Break> before;  // before[i] for chars[i]; before[chars.size()] is eot
};
inline LineBreaks line_breaks(std::string_view utf8) {
  LineBreaks r;
  r.chars = decode_utf8(utf8);
  std::vector<char32_t> cps(r.chars.size());
  for (std::size_t i = 0; i < cps.size(); ++i) cps[i] = r.chars[i].cp;
  r.before = line_break_opportunities(cps);
  return r;
}

}  // namespace rolltui::unicode
