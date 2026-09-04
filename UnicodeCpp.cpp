// rolltui/UnicodeCpp.cpp — THE C++ SIDE OF THE UNICODE ALGORITHMS (Phase 14 m5).
//
// `rolltui/c/rolltui_unicode.c` is the other one, and `-DROLLTUI_C` picks which links. Same
// header, same symbols, same answers — and here the oracle is not ours: three published
// Unicode conformance suites run in full against whichever is linked (GraphemeBreakTest,
// WordBreakTest, LineBreakTest — 19,339 line-break cases alone), plus a width table checked
// against libc `wcwidth` over the whole BMP with every disagreement listed rather than
// tolerated.
//
// This is the pre-port implementation, moved rather than rewritten. Only the OUTER shape
// changed: what used to fill a `std::vector` now fills a caller's buffer, because that is
// what the boundary takes. The rules, the rule numbers and the comments explaining them are
// untouched, which is the point — the suites can only say a change is correct if the change
// did not move the algorithm.
//
// It does NOT include `Unicode.hpp`: this file is what that header is a view OF, and the
// enums it needs come from the generated tables both languages share.
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/unicode_tables.h"

#include <cstring>
#include <memory>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace rolltui::unicode;
}  // namespace

// One line-break unit. At file scope so the caller's handle can hold the vector of them,
// which is what the C side does; it was local to the function until Phase 14 m5.
struct Unit {
  LineBreak cls;
  bool east_asian;    // ea ∈ {F, W, H} — $EastAsian in LB19a / LB30
  bool pi, pf;        // gc of a QU base
  bool dotted;        // U+25CC, the [◌] of LB28a
  bool pict_cn;       // Extended_Pictographic ∧ Cn, for LB30b
  bool ends_zwj;      // last code point is U+200D, for LB8a
  std::size_t first;  // index of the base in cps
};

// The caller's working memory — the same handle the C side takes, and the reason six
// `thread_local` buffers left this file. A vector per ROLE, so a function that calls another
// (graphemes → boundaries, display_width → graphemes) cannot alias its own caller's scratch.
// It is this side's OWN struct, so the vectors stay TYPED and every rule below reads exactly
// as it did before the port.
struct RolltuiUnicodeScratch {
  std::vector<RolltuiDecodedChar> dc;
  std::vector<char32_t> cps;
  std::vector<unsigned char> bounds;
  std::vector<GraphemeBreak> gb;
  std::vector<IndicConjunctBreak> incb;
  std::vector<unsigned char> pict;
  std::vector<WordBreak> wb;
  std::vector<unsigned char> wpict;
  std::vector<RolltuiUnicodeGrapheme> gr;
  std::vector<Unit> units;
};

extern "C" RolltuiUnicodeScratch* rolltui_u_scratch_new(void) {
  return std::make_unique<RolltuiUnicodeScratch>().release();
}
extern "C" void rolltui_u_scratch_free(RolltuiUnicodeScratch* s) {
  const std::unique_ptr<RolltuiUnicodeScratch> owned(s);
}

namespace {
// The typed enum stays, defined FROM the boundary's constants, so the two hundred lines of
// UAX #14 rules below read exactly as they did before the port. `Unicode.hpp` declares the
// caller's copy of the same three values and asserts them against the same constants.
enum class Break : unsigned char {
  Prohibited = ROLLTUI_BREAK_PROHIBITED,
  Allowed = ROLLTUI_BREAK_ALLOWED,
  Mandatory = ROLLTUI_BREAK_MANDATORY,
};

std::uint8_t lookup(const Range* table, std::size_t n, char32_t cp, std::uint8_t def) {
  std::size_t lo = 0, hi = n;
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo) / 2;
    if (table[mid].last < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < n && table[lo].first <= cp && cp <= table[lo].last) return table[lo].value;
  return def;
}

// The property accessors. They were `inline` in Unicode.hpp until m5; the algorithms below
// are their only heavy user, and they are on this side of the boundary now so that drawing a
// string crosses it once rather than once per code point.
LineBreak line_break_class(char32_t cp) {
  return static_cast<LineBreak>(lookup(kLineBreak, kLineBreakCount, cp, static_cast<std::uint8_t>(kLineBreakDefault)));
}
EastAsianWidth east_asian_width(char32_t cp) {
  return static_cast<EastAsianWidth>(
      lookup(kEastAsianWidth, kEastAsianWidthCount, cp, static_cast<std::uint8_t>(kEastAsianWidthDefault)));
}
GraphemeBreak grapheme_break(char32_t cp) {
  return static_cast<GraphemeBreak>(
      lookup(kGraphemeBreak, kGraphemeBreakCount, cp, static_cast<std::uint8_t>(kGraphemeBreakDefault)));
}
WordBreak word_break(char32_t cp) {
  return static_cast<WordBreak>(lookup(kWordBreak, kWordBreakCount, cp, static_cast<std::uint8_t>(kWordBreakDefault)));
}
IndicConjunctBreak indic_conjunct_break(char32_t cp) {
  return static_cast<IndicConjunctBreak>(
      lookup(kIndicConjunctBreak, kIndicConjunctBreakCount, cp, static_cast<std::uint8_t>(kIndicConjunctBreakDefault)));
}
GeneralCategory general_category(char32_t cp) {
  return static_cast<GeneralCategory>(
      lookup(kGeneralCategory, kGeneralCategoryCount, cp, static_cast<std::uint8_t>(kGeneralCategoryDefault)));
}
bool is_extended_pictographic(char32_t cp) {
  return lookup(kExtendedPictographic, kExtendedPictographicCount, cp, 0) != 0;
}
bool is_default_ignorable(char32_t cp) { return lookup(kDefaultIgnorable, kDefaultIgnorableCount, cp, 0) != 0; }

// ---- UTF-8 -------------------------------------------------------------------------

using DecodedChar = RolltuiDecodedChar;
using Grapheme = RolltuiUnicodeGrapheme;

DecodedChar decode_one(std::string_view s, std::size_t pos) {
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

void decode_utf8_into(std::string_view s, std::vector<DecodedChar>& out) {
  out.clear();
  out.reserve(s.size());
  for (std::size_t pos = 0; pos < s.size();) {
    DecodedChar d = decode_one(s, pos);
    out.push_back(d);
    pos += d.length;
  }
}

void append_utf8(std::string& out, char32_t cp) {
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

int codepoint_width(char32_t cp, bool ambiguous_wide) {
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

int cluster_width(std::span<const char32_t> cps, bool ambiguous_wide) {
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

// ---- UAX #29 -----------------------------------------------------------------------

void grapheme_boundaries_impl(RolltuiUnicodeScratch* sc, std::span<const char32_t> cps, unsigned char* b) {
  const std::size_t n = cps.size();
  std::memset(b, 0, n + 1);
  b[0] = 1;
  b[n] = 1;
  if (n < 2) return;
  // The three per-call intermediates live on the CALLER'S HANDLE now (Phase 14 m5). They were
  // `thread_local` from Phase 13 m3 — reused for the right reason, with a lifetime nobody owned.
  std::vector<GraphemeBreak>& g = sc->gb;
  std::vector<IndicConjunctBreak>& incb = sc->incb;
  std::vector<unsigned char>& pict = sc->pict;
  g.assign(n, GraphemeBreak{});
  incb.assign(n, IndicConjunctBreak{});
  pict.assign(n, 0);
  for (std::size_t i = 0; i < n; ++i) {
    g[i] = grapheme_break(cps[i]);
    incb[i] = indic_conjunct_break(cps[i]);
    pict[i] = is_extended_pictographic(cps[i]) ? 1u : 0u;
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
    b[i] = brk ? 1u : 0u;
  }
}

// Phase 13 m3: the three intermediates are REUSED buffers, not fresh vectors. This
// function is on the draw path — `Frame::put_text` calls it for every string drawn and the
// transcript for every span of every row — and it was allocating FOUR times per call
// (decode, codepoints, boundaries, result) for a handful of graphemes. The buffers are
// thread_local because a Terminal and a renderer may legitimately live on different
// threads; they are never nested, because nothing between `clear()` and the end of this
// function calls back into it.
//
// The ALGORITHM is untouched: the same decode, the same UAX #29 boundaries, the same
// widths. That is deliberate — the conformance suites (rolltui-grapheme-break-test,
// rolltui-width-test) are what say this is still correct, and they can only say it about
// a change that did not move the algorithm.
std::size_t graphemes_impl(RolltuiUnicodeScratch* sc, std::string_view utf8, bool ambiguous_wide, Grapheme* out) {
  std::vector<DecodedChar>& dc = sc->dc;
  std::vector<char32_t>& cps = sc->cps;
  std::vector<unsigned char>& b = sc->bounds;
  decode_utf8_into(utf8, dc);
  cps.clear();
  cps.reserve(dc.size());
  for (const DecodedChar& d : dc) cps.push_back(d.cp);
  b.resize(cps.size() + 1);
  grapheme_boundaries_impl(sc, cps, b.data());
  std::size_t n = 0, start = 0;
  for (std::size_t i = 1; i <= cps.size(); ++i) {
    if (!b[i]) continue;
    std::size_t byte0 = dc[start].offset;
    std::size_t byte1 = dc[i - 1].offset + dc[i - 1].length;
    out[n++] = Grapheme{byte0, byte1 - byte0,
                        cluster_width(std::span<const char32_t>(cps).subspan(start, i - start), ambiguous_wide)};
    start = i;
  }
  return n;
}

int display_width(RolltuiUnicodeScratch* sc, std::string_view utf8, bool ambiguous_wide) {
  std::vector<Grapheme>& g = sc->gr;
  g.resize(utf8.size());  // there can be no more clusters than bytes
  const std::size_t n = graphemes_impl(sc, utf8, ambiguous_wide, g.data());
  int w = 0;
  for (std::size_t i = 0; i < n; ++i) w += g[i].width;
  return w;
}

// ---- UAX #29: words ----------------------------------------------------------------

void word_boundaries_impl(RolltuiUnicodeScratch* sc, std::span<const char32_t> cps, unsigned char* b) {
  using WB = WordBreak;
  const std::size_t n = cps.size();
  std::memset(b, 0, n + 1);
  b[0] = 1;   // WB1
  b[n] = 1;   // WB2
  if (n < 2) return;
  std::vector<WB>& w = sc->wb;
  std::vector<unsigned char>& pict = sc->wpict;
  w.assign(n, WB{});
  pict.assign(n, 0);
  for (std::size_t i = 0; i < n; ++i) {
    w[i] = word_break(cps[i]);
    pict[i] = is_extended_pictographic(cps[i]) ? 1u : 0u;
  }
  auto ignorable = [](WB c) { return c == WB::Extend || c == WB::Format || c == WB::ZWJ; };
  auto newline = [](WB c) { return c == WB::Newline || c == WB::CR || c == WB::LF; };
  auto ah = [](WB c) { return c == WB::ALetter || c == WB::Hebrew_Letter; };
  auto midnumletq = [](WB c) { return c == WB::MidNumLet || c == WB::Single_Quote; };
  auto word_like = [&](WB c) { return ah(c) || c == WB::Numeric || c == WB::Katakana; };
  // The class of the nearest non-ignorable code point strictly before `i`, per WB4,
  // and its index; `none` when there is none.
  const std::size_t none = static_cast<std::size_t>(-1);
  auto prev_of = [&](std::size_t i) {
    while (i > 0) {
      --i;
      if (!ignorable(w[i])) return i;
    }
    return none;
  };
  auto next_of = [&](std::size_t i) {  // nearest non-ignorable at or after i
    while (i < n && ignorable(w[i])) ++i;
    return i < n ? i : none;
  };
  for (std::size_t i = 1; i < n; ++i) {
    const WB p = w[i - 1], q = w[i];
    bool brk;
    if (p == WB::CR && q == WB::LF) brk = false;                     // WB3
    else if (newline(p)) brk = true;                                 // WB3a
    else if (newline(q)) brk = true;                                 // WB3b
    else if (p == WB::ZWJ && pict[i]) brk = false;                   // WB3c
    else if (p == WB::WSegSpace && q == WB::WSegSpace) brk = false;  // WB3d
    else if (ignorable(q)) brk = false;                              // WB4
    else {
      // From here on, WB4 has already erased Extend/Format/ZWJ: look through them.
      const std::size_t pi = prev_of(i);
      const WB a = pi == none ? WB::Other : w[pi];
      const bool has_a = pi != none;
      const std::size_t ppi = has_a ? prev_of(pi) : none;
      const WB aa = ppi == none ? WB::Other : w[ppi];
      const bool has_aa = ppi != none;
      const WB c = q;
      const std::size_t ni = next_of(i + 1);
      const WB cc = ni == none ? WB::Other : w[ni];
      const bool has_cc = ni != none;
      brk = true;
      if (!has_a) brk = true;                                                        // WB999 (only ignorables before)
      else if (ah(a) && ah(c)) brk = false;                                          // WB5
      else if (ah(a) && (c == WB::MidLetter || midnumletq(c)) && has_cc && ah(cc)) brk = false;   // WB6
      else if (has_aa && ah(aa) && (a == WB::MidLetter || midnumletq(a)) && ah(c)) brk = false;   // WB7
      else if (a == WB::Hebrew_Letter && c == WB::Single_Quote) brk = false;         // WB7a
      else if (a == WB::Hebrew_Letter && c == WB::Double_Quote && has_cc && cc == WB::Hebrew_Letter) brk = false;  // WB7b
      else if (has_aa && aa == WB::Hebrew_Letter && a == WB::Double_Quote && c == WB::Hebrew_Letter) brk = false;  // WB7c
      else if (a == WB::Numeric && c == WB::Numeric) brk = false;                    // WB8
      else if (ah(a) && c == WB::Numeric) brk = false;                               // WB9
      else if (a == WB::Numeric && ah(c)) brk = false;                               // WB10
      else if (has_aa && aa == WB::Numeric && (a == WB::MidNum || midnumletq(a)) && c == WB::Numeric) brk = false;  // WB11
      else if (a == WB::Numeric && (c == WB::MidNum || midnumletq(c)) && has_cc && cc == WB::Numeric) brk = false;  // WB12
      else if (a == WB::Katakana && c == WB::Katakana) brk = false;                  // WB13
      else if ((word_like(a) || a == WB::ExtendNumLet) && c == WB::ExtendNumLet) brk = false;  // WB13a
      else if (a == WB::ExtendNumLet && word_like(c)) brk = false;                   // WB13b
      else if (a == WB::Regional_Indicator && c == WB::Regional_Indicator) {         // WB15/16
        std::size_t run = 0, j = pi;
        while (j != none && w[j] == WB::Regional_Indicator) { ++run; j = prev_of(j); }
        brk = (run % 2 == 0);
      }
    }
    b[i] = brk ? 1u : 0u;
  }
}

void word_range_impl(RolltuiUnicodeScratch* sc, std::string_view utf8, std::size_t offset,
                     std::size_t* out_begin, std::size_t* out_end) {
  std::vector<DecodedChar>& dc = sc->dc;
  decode_utf8_into(utf8, dc);
  if (offset >= utf8.size() || dc.empty()) {
    *out_begin = *out_end = utf8.size();
    return;
  }
  std::vector<char32_t>& cps = sc->cps;
  cps.resize(dc.size());
  for (std::size_t i = 0; i < dc.size(); ++i) cps[i] = dc[i].cp;
  std::vector<unsigned char>& b = sc->bounds;
  b.resize(cps.size() + 1);
  word_boundaries_impl(sc, cps, b.data());
  std::size_t k = 0;  // the code point containing `offset`
  while (k + 1 < dc.size() && dc[k + 1].offset <= offset) ++k;
  std::size_t start = k, end = k + 1;
  while (start > 0 && !b[start]) --start;
  while (end < dc.size() && !b[end]) ++end;
  *out_begin = dc[start].offset;
  *out_end = dc[end - 1].offset + dc[end - 1].length;
}

// ---- sanitising ---------------------------------------------------------------------

std::string strip_escape_sequences(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  const std::size_t n = s.size();
  auto at = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
  // Skips a string sequence's payload up to and including its terminator (BEL, or
  // ESC \, or the C1 ST U+009C); returns the index just past it (n if unterminated).
  auto skip_string = [&](std::size_t i) {
    while (i < n) {
      if (at(i) == 0x07) return i + 1;
      if (at(i) == 0x1B && i + 1 < n && s[i + 1] == '\\') return i + 2;
      if (at(i) == 0xC2 && i + 1 < n && at(i + 1) == 0x9C) return i + 2;
      ++i;
    }
    return n;
  };
  auto skip_csi = [&](std::size_t i) {  // i is just past the introducer
    while (i < n && at(i) >= 0x20 && at(i) <= 0x3F) ++i;   // parameters + intermediates
    if (i < n && at(i) >= 0x40 && at(i) <= 0x7E) ++i;      // final byte
    return i;
  };
  for (std::size_t i = 0; i < n;) {
    const unsigned char c = at(i);
    if (c == 0x1B) {
      if (i + 1 >= n) { ++i; continue; }  // a lone trailing ESC: dropped
      const unsigned char d = at(i + 1);
      if (d == '[') i = skip_csi(i + 2);
      else if (d == ']' || d == 'P' || d == 'X' || d == '^' || d == '_') i = skip_string(i + 2);
      else if (d >= 0x20 && d <= 0x2F) {  // ESC intermediate* final
        std::size_t j = i + 1;
        while (j < n && at(j) >= 0x20 && at(j) <= 0x2F) ++j;
        i = (j < n && at(j) >= 0x30 && at(j) <= 0x7E) ? j + 1 : j;
      } else if (d >= 0x30 && d <= 0x7E) i += 2;  // ESC final (ESC c, ESC 7, ESC = …)
      else ++i;  // ESC before a control or a non-ASCII byte: drop the ESC alone
      continue;
    }
    // 8-bit C1 introducers, as UTF-8 (C2 9B = CSI, C2 9D = OSC, C2 90/98/9E/9F strings).
    if (c == 0xC2 && i + 1 < n) {
      const unsigned char d = at(i + 1);
      if (d == 0x9B) { i = skip_csi(i + 2); continue; }
      if (d == 0x9D || d == 0x90 || d == 0x98 || d == 0x9E || d == 0x9F) { i = skip_string(i + 2); continue; }
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

// ---- UAX #14 -----------------------------------------------------------------------

void line_break_opportunities_impl(RolltuiUnicodeScratch* sc, std::span<const char32_t> cps, unsigned char* out) {
  using LB = LineBreak;
  using EA = EastAsianWidth;
  using GC = GeneralCategory;
  const std::size_t n = cps.size();
  std::memset(out, ROLLTUI_BREAK_PROHIBITED, n + 1);
  out[n] = ROLLTUI_BREAK_MANDATORY;
  if (n == 0) return;

  // LB9/LB10 are applied structurally: the text is first cut into units — a base
  // character with every CM/ZWJ attached to it (LB9), or a lone CM/ZWJ that had no
  // eligible base and so becomes AL with U+0041's properties (LB10). A unit carries
  // its base's class and the properties later rules read (East Asian width, Pi/Pf
  // for quotation marks, the dotted circle, Extended_Pictographic ∧ Cn). Positions
  // inside a unit are never breaks; every rule below runs between units.
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
  // Phase 13 m5b made this a reused buffer; Phase 14 m5 moved it onto the caller's handle, so
  // the reuse has an owner instead of a thread-local lifetime nobody holds.
  std::vector<Unit>& u = sc->units;
  u.clear();
  u.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    LB c = resolved(cps[i]);
    bool joiner = (c == LB::CM || c == LB::ZWJ);
    if (joiner && !u.empty() && !no_attach(u.back().cls)) {
      u.back().ends_zwj = (c == LB::ZWJ);
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

  for (std::size_t k = 1; k < m; ++k) out[u[k].first] = static_cast<unsigned char>(decide(k));
  // LB3: eot is a mandatory break, LB2: sot never is — both set at construction.
}

}  // namespace

// ---- THE BOUNDARY ---------------------------------------------------------------------
// Every one of these is a thin call into the implementation above. The shapes are the
// boundary's, not this file's: caller buffers whose size the caller can compute without
// asking, and out-params rather than returned structs.

extern "C" unsigned char rolltui_u_line_break_class(RolltuiCodepoint cp) {
  return static_cast<unsigned char>(line_break_class(cp));
}
extern "C" unsigned char rolltui_u_east_asian_width(RolltuiCodepoint cp) {
  return static_cast<unsigned char>(east_asian_width(cp));
}
extern "C" unsigned char rolltui_u_grapheme_break(RolltuiCodepoint cp) {
  return static_cast<unsigned char>(grapheme_break(cp));
}
extern "C" unsigned char rolltui_u_word_break(RolltuiCodepoint cp) {
  return static_cast<unsigned char>(word_break(cp));
}
extern "C" unsigned char rolltui_u_indic_conjunct_break(RolltuiCodepoint cp) {
  return static_cast<unsigned char>(indic_conjunct_break(cp));
}
extern "C" unsigned char rolltui_u_general_category(RolltuiCodepoint cp) {
  return static_cast<unsigned char>(general_category(cp));
}
extern "C" int rolltui_u_is_extended_pictographic(RolltuiCodepoint cp) { return is_extended_pictographic(cp) ? 1 : 0; }
extern "C" int rolltui_u_is_default_ignorable(RolltuiCodepoint cp) { return is_default_ignorable(cp) ? 1 : 0; }

extern "C" void rolltui_u_decode_one(const char* s, size_t len, size_t pos, RolltuiDecodedChar* out) {
  *out = decode_one(std::string_view(s, len), pos);
}

extern "C" size_t rolltui_u_decode_utf8(const char* s, size_t len, RolltuiCodepoint* cp, size_t* offset,
                                        size_t* length) {
  const std::string_view sv(s, len);
  std::size_t n = 0;
  for (std::size_t pos = 0; pos < len;) {
    const DecodedChar d = decode_one(sv, pos);
    cp[n] = d.cp;
    offset[n] = d.offset;
    length[n] = d.length;
    ++n;
    pos += d.length;
  }
  return n;
}

extern "C" size_t rolltui_u_decode_utf8_chars(const char* s, size_t len, RolltuiDecodedChar* out) {
  const std::string_view sv(s, len);
  std::size_t n = 0;
  for (std::size_t pos = 0; pos < len;) {
    out[n] = decode_one(sv, pos);
    pos += out[n].length;
    ++n;
  }
  return n;
}

extern "C" size_t rolltui_u_append_utf8(RolltuiCodepoint cp, char* out) {
  std::string s;
  append_utf8(s, cp);
  std::memcpy(out, s.data(), s.size());
  return s.size();
}

extern "C" int rolltui_u_codepoint_width(RolltuiCodepoint cp, int ambiguous_wide) {
  return codepoint_width(cp, ambiguous_wide != 0);
}
extern "C" int rolltui_u_cluster_width(const RolltuiCodepoint* cps, size_t n, int ambiguous_wide) {
  return cluster_width(std::span<const char32_t>(cps, n), ambiguous_wide != 0);
}
extern "C" int rolltui_u_display_width(RolltuiUnicodeScratch* sc, const char* utf8, size_t len,
                                       int ambiguous_wide) {
  return display_width(sc, std::string_view(utf8, len), ambiguous_wide != 0);
}

extern "C" void rolltui_u_grapheme_boundaries(RolltuiUnicodeScratch* sc, const RolltuiCodepoint* cps, size_t n,
                                              unsigned char* out) {
  grapheme_boundaries_impl(sc, std::span<const char32_t>(cps, n), out);
}
extern "C" void rolltui_u_word_boundaries(RolltuiUnicodeScratch* sc, const RolltuiCodepoint* cps, size_t n,
                                          unsigned char* out) {
  word_boundaries_impl(sc, std::span<const char32_t>(cps, n), out);
}
extern "C" size_t rolltui_u_graphemes(RolltuiUnicodeScratch* sc, const char* utf8, size_t len,
                                      int ambiguous_wide, RolltuiUnicodeGrapheme* out) {
  return graphemes_impl(sc, std::string_view(utf8, len), ambiguous_wide != 0, out);
}
extern "C" void rolltui_u_word_range(RolltuiUnicodeScratch* sc, const char* utf8, size_t len, size_t offset,
                                     size_t* begin, size_t* end) {
  word_range_impl(sc, std::string_view(utf8, len), offset, begin, end);
}

extern "C" void rolltui_u_line_break_opportunities(RolltuiUnicodeScratch* sc, const RolltuiCodepoint* cps,
                                                   size_t n, unsigned char* out) {
  line_break_opportunities_impl(sc, std::span<const char32_t>(cps, n), out);
}

extern "C" size_t rolltui_u_strip_escape_sequences(const char* s, size_t len, char* out) {
  const std::string stripped = strip_escape_sequences(std::string_view(s, len));
  std::memcpy(out, stripped.data(), stripped.size());
  return stripped.size();
}
