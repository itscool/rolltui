// normalization_test.cpp — UAX #15 canonical normalization against Unicode's own conformance suite,
// NormalizationTest.txt, in full: every line's NFC and NFD columns, and every code point the file does
// not list (Part 1's rule: those are unchanged by both). Then the use the library makes of it: the lead
// of a grapheme cluster, which is what a terminal that splits clusters is sent.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

using namespace testkit;

#ifndef ROLLTUI_UCD_DIR
#error "ROLLTUI_UCD_DIR must point at rolltui/ucd"
#endif

namespace {

using Cps = std::vector<RolltuiCodepoint>;

Cps hex_list(const std::string& field) {
  Cps out;
  std::istringstream in(field);
  std::string h;
  while (in >> h) out.push_back(static_cast<RolltuiCodepoint>(std::strtoul(h.c_str(), nullptr, 16)));
  return out;
}

std::string show(const Cps& v) {
  std::string out;
  char buf[16];
  for (RolltuiCodepoint c : v) {
    std::snprintf(buf, sizeof buf, "%s%04X", out.empty() ? "" : " ", static_cast<unsigned>(c));
    out += buf;
  }
  return out;
}

Cps nfd(const Cps& in) {
  Cps out(4 * in.size() + 1);
  const std::size_t n = rolltui_u_nfd(in.data(), in.size(), out.data(), out.size());
  out.resize(n);
  return out;
}

Cps nfc(const Cps& in) {
  Cps out(4 * in.size() + 1);
  const std::size_t n = rolltui_u_nfc(in.data(), in.size(), out.data(), out.size());
  out.resize(n);
  return out;
}

std::string lead(const std::string& utf8) {
  char out[4];
  return std::string(out, rolltui_u_cluster_lead(utf8.data(), utf8.size(), out));
}

}  // namespace

int main() {
  const std::string path = std::string(ROLLTUI_UCD_DIR) + "/NormalizationTest.txt";
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 2;
  }

  // ---- 1. every line, both forms --------------------------------------------------------------
  // c2 == NFC(c1) == NFC(c2) == NFC(c3); c4 == NFC(c4) == NFC(c5);
  // c3 == NFD(c1) == NFD(c2) == NFD(c3); c5 == NFD(c4) == NFD(c5).
  std::vector<bool> in_part1(0x110000, false);
  std::string line;
  int part = -1, lineno = 0;
  std::size_t cases = 0, failures = 0;
  std::string first_failure;
  while (std::getline(in, line)) {
    ++lineno;
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '@') {
      part = std::atoi(line.c_str() + 5);  // "@Part1 # ..."
      continue;
    }
    std::vector<std::string> f;
    std::istringstream fields(line.substr(0, line.find('#')));
    std::string one;
    while (std::getline(fields, one, ';')) f.push_back(one);
    if (f.size() < 5) continue;
    const Cps c1 = hex_list(f[0]), c2 = hex_list(f[1]), c3 = hex_list(f[2]), c4 = hex_list(f[3]), c5 = hex_list(f[4]);
    if (part == 1 && c1.size() == 1) in_part1[c1[0]] = true;
    ++cases;
    const bool ok = nfc(c1) == c2 && nfc(c2) == c2 && nfc(c3) == c2 && nfc(c4) == c4 && nfc(c5) == c4 && nfd(c1) == c3 &&
                    nfd(c2) == c3 && nfd(c3) == c3 && nfd(c4) == c5 && nfd(c5) == c5;
    if (!ok && failures++ == 0)
      first_failure = "line " + std::to_string(lineno) + ": " + show(c1) + " -> NFC " + show(nfc(c1)) + " (want " + show(c2) +
                      "), NFD " + show(nfd(c1)) + " (want " + show(c3) + ")";
  }
  check(cases > 19000, "NormalizationTest.txt was read: " + std::to_string(cases) + " cases");
  check(failures == 0, "every case's NFC and NFD columns hold (" + std::to_string(failures) + " of " + std::to_string(cases) +
                           " failed" + (first_failure.empty() ? "" : "; first: " + first_failure) + ")");

  // ---- 2. every code point Part 1 does not list is its own NFC and NFD ------------------------
  // …and no code point decomposes to more than four, the bound the functions' `cap` rests on.
  std::size_t invariant_bad = 0, longest = 0;
  RolltuiCodepoint first_bad = 0;
  for (RolltuiCodepoint cp = 0; cp <= 0x10FFFF; ++cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) continue;
    const Cps one = {cp};
    const Cps d = nfd(one);
    if (d.size() > longest) longest = d.size();
    if (in_part1[cp]) continue;
    if (d != one || nfc(one) != one) {
      if (invariant_bad++ == 0) first_bad = cp;
    }
  }
  char bad_hex[16];
  std::snprintf(bad_hex, sizeof bad_hex, "%04X", static_cast<unsigned>(first_bad));
  check(invariant_bad == 0, "every code point not in Part 1 is unchanged by NFC and NFD (" + std::to_string(invariant_bad) +
                                " are not" + (invariant_bad ? std::string("; first U+") + bad_hex : "") + ")");
  check(longest == 4, "no code point decomposes to more than four (the longest: " + std::to_string(longest) + ")");

  // ---- 3. the cluster's lead -------------------------------------------------------------------
  check(lead("e\xCC\x81") == "\xC3\xA9", "`e` and a combining acute lead with `é`");
  check(lead("e\xCC\xA3\xCC\x82") == "\xE1\xBB\x87", "`e`, a dot below and a circumflex lead with `ệ`, whichever order the marks came in");
  check(lead("e\xCC\x82\xCC\xA3") == "\xE1\xBB\x87", "…and in the other order");
  check(lead("e\xCC\x81\xCC\xA3") == "\xE1\xBA\xB9", "`e`, an acute and a dot below lead with `ẹ`: the acute has no precomposed form with it");
  check(lead("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8") == "\xEA\xB0\x81", "Hangul jamo L, V and T lead with their syllable (각)");
  check(lead("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == "\xF0\x9F\x91\x8D", "a skin-toned emoji leads with the emoji");
  check(lead("\xCC\x81") == "\xCC\x81" && lead("x") == "x" && lead("").empty(), "a lone accent leads with itself; one code point is itself");

  return report("normalization_test");
}
