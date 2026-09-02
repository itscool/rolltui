#pragma once
//
// ucd_test_file.hpp — reader for the Unicode conformance-suite format shared by
// GraphemeBreakTest.txt and LineBreakTest.txt:
//
//     ÷ 0061 × 0308 ÷ 0062 ÷    #  ÷ [0.2] LATIN SMALL LETTER A (Other) × [9.0] ...
//
// A line is a sequence of marks (÷ break, × no break) alternating with hex code
// points, starting and ending with a mark, then an optional `#` comment that names
// the rule that decided each mark. The comment is kept so a failing case can be
// reported with Unicode's own explanation next to ours.
//
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace rolltui_test {

struct UcdCase {
  std::vector<char32_t> cps;
  std::vector<bool> breaks;  // breaks[i] before cps[i]; breaks[n] at the end
  std::string comment;
  int line_number = 0;
};

inline std::vector<UcdCase> read_ucd_cases(const std::string& path) {
  std::vector<UcdCase> cases;
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    std::exit(2);
  }
  std::string line;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    if (line.empty() || line[0] == '#') continue;
    UcdCase c;
    c.line_number = lineno;
    std::size_t hash = line.find('#');
    if (hash != std::string::npos) {
      c.comment = line.substr(hash + 1);
      line = line.substr(0, hash);
    }
    // Tokenise on whitespace; the marks are UTF-8 "÷" (C3 B7) and "×" (C3 97).
    std::size_t i = 0;
    while (i < line.size()) {
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
      if (i >= line.size()) break;
      std::size_t j = i;
      while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
      std::string tok = line.substr(i, j - i);
      i = j;
      if (tok == "\xC3\xB7") c.breaks.push_back(true);
      else if (tok == "\xC3\x97") c.breaks.push_back(false);
      else c.cps.push_back(static_cast<char32_t>(std::strtoul(tok.c_str(), nullptr, 16)));
    }
    if (c.breaks.size() != c.cps.size() + 1) {
      std::fprintf(stderr, "%s:%d: malformed case (%zu marks, %zu code points)\n",
                   path.c_str(), lineno, c.breaks.size(), c.cps.size());
      std::exit(2);
    }
    cases.push_back(std::move(c));
  }
  return cases;
}

inline std::string cps_to_hex(const std::vector<char32_t>& cps) {
  std::string s;
  char buf[16];
  for (char32_t c : cps) {
    std::snprintf(buf, sizeof buf, "%s%04X", s.empty() ? "" : " ", static_cast<unsigned>(c));
    s += buf;
  }
  return s;
}

// Render a mark sequence the way the suite prints it, so a failure shows
// expected/actual in the same notation as the file.
inline std::string marks_to_string(const std::vector<char32_t>& cps,
                                   const std::vector<bool>& breaks) {
  std::string s;
  char buf[16];
  for (std::size_t i = 0; i <= cps.size(); ++i) {
    s += breaks[i] ? "\xC3\xB7" : "\xC3\x97";
    if (i < cps.size()) {
      std::snprintf(buf, sizeof buf, " %04X ", static_cast<unsigned>(cps[i]));
      s += buf;
    }
  }
  return s;
}

}  // namespace rolltui_test
