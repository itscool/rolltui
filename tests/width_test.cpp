//
// width_test.cpp — the display-width functions: a hand table of code points and
// clusters whose width the renderer depends on, and a cross-check of
// codepoint_width() against libc wcwidth() over the whole BMP in which EVERY
// disagreement is listed with its reason (the plan: "the table is
// cross-checked against libc wcwidth over the BMP and every disagreement is listed in
// the test, not hidden").
//
// The list is live in both directions: a disagreeing code point no entry claims is a
// FAIL naming it (a new, unexplained difference), and an entry that claims nothing is
// a FAIL naming it (a stale explanation — e.g. macOS updated its table). Both are the
// point: the list is only worth having if it cannot drift.
//
// Measured on macOS 26 (Darwin 25.6.0) libc under C.UTF-8:
//   1,571 of the BMP's 63,488 non-surrogate code points disagree; all fall into the
//   seven entries below; the remaining ~62,000 agree, including every CJK, Hangul
//   syllable, kana, combining mark, emoji and control in the BMP.
//
#include <clocale>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/unicode_tables.h"  // ROLLTUI_GENERALCATEGORY_Cn: the C constants, not the C++ enum
#include "rolltui_test.hpp"

using namespace rolltui_test;

namespace {

std::string hex(char32_t c) {
  char b[16];
  std::snprintf(b, sizeof b, "U+%04X", static_cast<unsigned>(c));
  return b;
}

int cw(std::vector<char32_t> cps, bool amb = false) { return rolltui_u_cluster_width(cps.data(), cps.size(), amb); }

// One explained disagreement: which code points it covers (by predicate over the
// code point and the two answers) and why.
struct Known {
  const char* why;
  std::function<bool(char32_t cp, int ours, int libc)> claims;
};

}  // namespace

int main() {
  // Working memory for display_width (rolltui/c/rolltui_unicode.h): CALLER-OWNED, made
  // once and reused for every call below, freed before the one return.
  RolltuiUnicodeScratch* scratch = rolltui_u_scratch_new();

  // ---- hand table: single code points --------------------------------------------
  check(rolltui_u_codepoint_width('a', false) == 1, "a: 1");
  check(rolltui_u_codepoint_width(0x4E2D, false) == 2, "U+4E2D 中 (W): 2");
  check(rolltui_u_codepoint_width(0xFF21, false) == 2, "U+FF21 fullwidth A (F): 2");
  check(rolltui_u_codepoint_width(0xFF61, false) == 1, "U+FF61 halfwidth ideographic full stop (H): 1");
  check(rolltui_u_codepoint_width(0x3000, false) == 2, "U+3000 ideographic space (F): 2");
  check(rolltui_u_codepoint_width(0x1F600, false) == 2, "U+1F600 😀 (W): 2");
  check(rolltui_u_codepoint_width(0x263A, false) == 1, "U+263A ☺ text-presentation (N): 1");
  check(rolltui_u_codepoint_width(0x0301, false) == 0, "U+0301 combining acute (Mn): 0");
  check(rolltui_u_codepoint_width(0x093F, false) == 0, "U+093F Devanagari vowel sign I (Mc): 0 — macOS libc's answer");
  check(rolltui_u_codepoint_width(0x20DD, false) == 0, "U+20DD combining enclosing circle (Me): 0");
  check(rolltui_u_codepoint_width(0x200B, false) == 0, "U+200B zero width space (Cf): 0");
  check(rolltui_u_codepoint_width(0x200D, false) == 0, "U+200D ZWJ: 0");
  check(rolltui_u_codepoint_width(0xFE0F, false) == 0, "U+FE0F VS16: 0");
  check(rolltui_u_codepoint_width(0x00AD, false) == 0, "U+00AD soft hyphen (Default_Ignorable): 0");
  check(rolltui_u_codepoint_width(0x115F, false) == 0, "U+115F Hangul choseong filler (Default_Ignorable): 0");
  check(rolltui_u_codepoint_width(0x1100, false) == 2, "U+1100 Hangul choseong kiyeok (L, W): 2");
  check(rolltui_u_codepoint_width(0x1161, false) == 0, "U+1161 Hangul jungseong A (V): 0");
  check(rolltui_u_codepoint_width(0x11A8, false) == 0, "U+11A8 Hangul jongseong kiyeok (T): 0");
  check(rolltui_u_codepoint_width(0xAC01, false) == 2, "U+AC01 각 precomposed syllable (W): 2");
  check(rolltui_u_codepoint_width(0x0000, false) == 0, "U+0000 (Cc): 0");
  check(rolltui_u_codepoint_width('\t', false) == 0, "TAB (Cc): 0 — the wrap engine expands tabs itself");
  check(rolltui_u_codepoint_width(0x2028, false) == 0, "U+2028 line separator (Zl): 0");
  check(rolltui_u_codepoint_width(0x00A1, false) == 1, "U+00A1 ¡ (A): 1 by default");
  check(rolltui_u_codepoint_width(0x00A1, true) == 2, "U+00A1 ¡ (A): 2 with ambiguous_wide");
  check(rolltui_u_codepoint_width(0xE000, false) == 1, "U+E000 private use (A): 1 by default");
  check(rolltui_u_codepoint_width(0xFFFD, false) == 1, "U+FFFD replacement character (A): 1");
  check(rolltui_u_codepoint_width(0x20AC, false) == 1, "U+20AC € (A): 1 by default");
  check(rolltui_u_codepoint_width(0x0378, false) == 1, "U+0378 unassigned (N): 1");
  check(rolltui_u_codepoint_width(0x2A6E0, false) == 2, "U+2A6E0 unassigned in Plane 2 (W by @missing): 2");
  check(rolltui_u_codepoint_width(0x1F3FD, false) == 2, "U+1F3FD skin-tone modifier alone (Sk, W): 2, a colour swatch");

  // ---- hand table: clusters ------------------------------------------------------
  check(cw({'e', 0x0301}) == 1, "e + U+0301: 1");
  check(cw({0x1100, 0x1161, 0x11A8}) == 2, "L V T jamo sequence: 2");
  check(cw({0x1F44B, 0x1F3FD}) == 2, "👋🏽 waving hand + modifier: 2 (the modifier is Extend: 0 attached)");
  check(cw({0x0915, 0x0301}) == 1, "consonant + combining mark: 1");
  check(cw({0x1F469, 0x200D, 0x1F4BB}) == 2, "👩‍💻 ZWJ sequence: 2");
  check(cw({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466}) == 2, "family of four ZWJ sequence: 2");
  check(cw({0x1F1EF, 0x1F1F5}) == 2, "🇯🇵 flag: 2");
  check(cw({0x1F1EF}) == 1, "a lone regional indicator: 1");
  check(cw({'#', 0xFE0F, 0x20E3}) == 2, "#️⃣ keycap: 2");
  check(cw({0x2764, 0xFE0F}) == 2, "❤️ heart + VS16: 2");
  check(cw({0x2764}) == 1, "❤ heart alone (N): 1");
  check(cw({0x2764, 0xFE0E}) == 1, "❤︎ heart + VS15: 1");
  check(cw({0x1F600, 0xFE0E}) == 2, "😀 + VS15: still 2 (W stays W; terminals do not narrow it)");
  check(cw({0x0915, 0x094D, 0x0937}) == 2, "क्ष conjunct (one GB9c cluster): 2 — the sum of its consonants");
  check(cw({0x0915, 0x093F}) == 1, "कि consonant + spacing vowel sign: 1");
  check(cw({0x0301}) == 0, "a lone combining mark: 0");
  check(cw({}) == 0, "empty cluster: 0");
  check(cw({0x00A1, 0x0301}, true) == 2, "ambiguous_wide flows through cluster_width");
  check(rolltui_u_display_width(scratch, "hello", std::strlen("hello"), false) == 5, "display_width(\"hello\") = 5");
  check(rolltui_u_display_width(scratch, "", std::strlen(""), false) == 0, "display_width(\"\") = 0");
  check(rolltui_u_display_width(scratch, "\xE4\xB8\xAD\xE6\x96\x87", std::strlen("\xE4\xB8\xAD\xE6\x96\x87"), false) == 4,
        "display_width(\"中文\") = 4");
  check(rolltui_u_display_width(scratch, "a\xCC\x81" "b", std::strlen("a\xCC\x81" "b"), false) == 2,
        "display_width(a + U+0301 + b) = 2");
  check(rolltui_u_display_width(scratch, "\xFF", std::strlen("\xFF"), false) == 1, "an invalid byte shows as U+FFFD: 1");
  check(rolltui_u_display_width(scratch, "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xAF",
                                std::strlen("\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xAF"), false) == 3,
        "flag + lone RI = 2 + 1");

  // ---- the libc cross-check over the BMP -----------------------------------------
  // The control must be seen to work: under the C locale wcwidth returns -1 for
  // everything non-ASCII, which would make every wide character a "disagreement"
  // and, worse, would make a broken locale look like a broken table.
  setlocale(LC_CTYPE, "C.UTF-8");
  check(wcwidth(L'\x4E2D') == 2 && wcwidth(L'\x0301') == 0,
        "libc wcwidth is live under C.UTF-8 (中 = 2, U+0301 = 0)");

  const std::vector<Known> known = {
      {"unassigned code points: we follow East_Asian_Width (1, or 2 inside the CJK "
       "blocks); libc returns 0 for anything its table does not know",
       [](char32_t cp, int, int libc) { return rolltui_u_general_category(cp) == ROLLTUI_GENERALCATEGORY_Cn && libc == 0; }},
      {"assigned after libc's table was built (libc returns 0, its answer for "
       "unassigned): U+088F, 0C5C, 0CDC, 1B4E-1B4F, 1B7F, 1C89-1C8A, 20C1, 2427-2429, "
       "2B96, A7CB-A7CF, A7D2, A7D4, A7DA-A7DC, A7F1, FBC3-FBD2, FD90-FD91, FDC8-FDCE",
       [](char32_t cp, int ours, int libc) {
         static const char32_t r[][2] = {
             {0x088F, 0x088F}, {0x0C5C, 0x0C5C}, {0x0CDC, 0x0CDC}, {0x1B4E, 0x1B4F},
             {0x1B7F, 0x1B7F}, {0x1C89, 0x1C8A}, {0x20C1, 0x20C1}, {0x2427, 0x2429},
             {0x2B96, 0x2B96}, {0xA7CB, 0xA7CF}, {0xA7D2, 0xA7D2}, {0xA7D4, 0xA7D4},
             {0xA7DA, 0xA7DC}, {0xA7F1, 0xA7F1}, {0xFBC3, 0xFBD2}, {0xFD90, 0xFD91},
             {0xFDC8, 0xFDCE}};
         if (ours != 1 || libc != 0) return false;
         for (auto& x : r) if (cp >= x[0] && cp <= x[1]) return true;
         return false;
       }},
      {"ideographic description characters added in Unicode 15.1/16 (W): U+2FFC-2FFF, "
       "31E4-31E5, 31EF — libc returns 0 (unknown to its table)",
       [](char32_t cp, int ours, int libc) {
         return ours == 2 && libc == 0 &&
                ((cp >= 0x2FFC && cp <= 0x2FFF) || cp == 0x31E4 || cp == 0x31E5 || cp == 0x31EF);
       }},
      {"East_Asian_Width became W for the Yijing trigrams, mono/digrams and hexagrams "
       "(U+2630-2637, 268A-268F, 4DC0-4DFF); libc's older table has them narrow",
       [](char32_t cp, int ours, int libc) {
         return ours == 2 && libc == 1 &&
                ((cp >= 0x2630 && cp <= 0x2637) || (cp >= 0x268A && cp <= 0x268F) ||
                 (cp >= 0x4DC0 && cp <= 0x4DFF));
       }},
      {"Hangul fillers U+115F, 3164, FFA0 are Default_Ignorable (0); libc gives them "
       "their East_Asian_Width (2, 2, 1)",
       [](char32_t cp, int ours, int) {
         return ours == 0 && (cp == 0x115F || cp == 0x3164 || cp == 0xFFA0);
       }},
      {"Hangul Jamo Extended-B medial vowels and final consonants (U+D7B0-D7C6, "
       "D7CB-D7FB) conjoin with a leading consonant, so 0; libc says 1",
       [](char32_t cp, int ours, int libc) {
         return ours == 0 && libc == 1 &&
                ((cp >= 0xD7B0 && cp <= 0xD7C6) || (cp >= 0xD7CB && cp <= 0xD7FB));
       }},
      {"U+302E-302F Hangul tone marks are Mc (spacing marks, 0 by our rule) with "
       "East_Asian_Width W; libc says 2",
       [](char32_t cp, int ours, int libc) {
         return ours == 0 && libc == 2 && (cp == 0x302E || cp == 0x302F);
       }},
  };

  std::vector<int> claimed(known.size(), 0);
  int disagreements = 0, unexplained = 0, scanned = 0;
  for (char32_t cp = 0; cp <= 0xFFFF; ++cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) continue;
    ++scanned;
    int libc = wcwidth(static_cast<wchar_t>(cp));
    if (libc < 0) libc = 0;  // "not printable" and "zero cells" are the same advance
    int ours = rolltui_u_codepoint_width(cp, false);
    if (ours == libc) continue;
    ++disagreements;
    int owners = 0;
    for (std::size_t i = 0; i < known.size(); ++i)
      if (known[i].claims(cp, ours, libc)) { ++owners; ++claimed[i]; }
    if (owners == 1) continue;
    if (++unexplained <= 25)
      check(false, hex(cp) + ": ours=" + std::to_string(ours) + " libc=" +
                       std::to_string(libc) + (owners ? " claimed by MORE THAN ONE entry"
                                                      : " — NOT in the known list"));
  }
  check(unexplained == 0, "every wcwidth disagreement over the BMP is in the known list (" +
                              std::to_string(disagreements) + " disagreements over " +
                              std::to_string(scanned) + " code points, " +
                              std::to_string(unexplained) + " unexplained)");
  for (std::size_t i = 0; i < known.size(); ++i)
    check(claimed[i] > 0, "known entry still applies (" + std::to_string(claimed[i]) +
                              " code points): " + known[i].why);

  rolltui_u_scratch_free(scratch);
  return report("rolltui width_test");
}
