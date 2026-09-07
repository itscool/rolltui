// rolltui/tests/source_scan.hpp — reading the library's OWN SOURCE, for the two meta-tests that
// do it.
//
// `public_header_test` scans the header to hold it to `api_classes.inc`; `ownership_test` scans
// the `.c` files to hold their process-wide state to `globals.inc`. Both need the same thing —
// C source with comments and literals taken out — and before this file existed only the first
// had it. **`rolltui.h` rule 5: if two consumers write the same wrapper the API is wrong, not
// the consumers**, and a test helper is no exception; the second consumer is what moved this
// out of one file rather than copying it into the other.
//
// It deliberately includes NOTHING of the library. `public_header_test`'s first assertion is
// that `rolltui/rolltui.h` alone suffices, so a helper it includes must not smuggle anything in.
#pragma once
#include <cstddef>
#include <regex>
#include <string>
#include <vector>

// Everything at BRACE DEPTH 0 — a struct's inline C++ member bodies removed, so a call inside
// one is not mistaken for a declaration. `extern "C" {` and `namespace x {` open no depth,
// because what follows them is still the file's own top level. Lived in
// `public_header_test.cpp` until the duplicate-declaration check became its second consumer
// (rule 5 again, the same way the strippers below got here).
inline std::string depth0(const std::string& t) {
  std::string out;
  std::vector<bool> counted;
  int depth = 0;
  for (std::size_t i = 0; i < t.size(); ++i) {
    const char ch = t[i];
    if (ch == '{') {
      const std::string before = t.substr(i >= 40 ? i - 40 : 0, i >= 40 ? 40 : i);
      const bool linkage = std::regex_search(before, std::regex(R"((extern\s+"C"|namespace\s+\w+)\s*$)"));
      counted.push_back(!linkage);
      if (!linkage) ++depth;
      continue;
    }
    if (ch == '}') {
      if (!counted.empty()) {
        if (counted.back()) --depth;
        counted.pop_back();
      }
      continue;
    }
    if (depth == 0) out += ch;
  }
  return out;
}

// ---- section 6's instruments: a literal-aware comment stripper, a recursive lister, and the
// identifier scan the class table is checked against ---------------------------
// Comments are stripped RESPECTING string and char literals — a "/*" inside a JSON string or a
// "//" inside a URL would otherwise eat real code, which is how the first census of this
// surface under-counted the library's own reach by a third.
std::string strip_all_comments(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size();) {
    const char c = src[i];
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < src.size() && src[j] != c) j += (src[j] == '\\') ? 2 : 1;
      out.append(src, i, std::min(j + 1, src.size()) - i);
      i = j + 1;
    } else if (src.compare(i, 2, "/*") == 0) {
      const size_t j = src.find("*/", i + 2);
      out += ' ';
      i = (j == std::string::npos) ? src.size() : j + 2;
    } else if (src.compare(i, 2, "//") == 0) {
      const size_t j = src.find('\n', i);
      i = (j == std::string::npos) ? src.size() : j;
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

// for REACH, a string literal's CONTENTS are blanked as well. An identifier inside
// a literal is DATA, never a call — `ownership_test.cpp` names `rolltui_mem_realloc` inside a
// regex and `budget_test.cpp` names it in prose, and the first run of the public-only rule below
// reported both as consumers of an internal function. Comments already went; literals had to go
// too, and this is general rather than a special case for the meta-tests: nothing anywhere calls
// a function by naming it in a string.
// ONE PASS, because comments and literals cannot be stripped in either order — and the census
// this feeds is what every class in `api_classes.inc` is held to, so an under-count here is a
// wrong CLASS, silently. Three ways to get it wrong, all measured:
//   - literals first: an apostrophe in prose ("don't") opens a bogus char literal.
//   - comments first, which is what this function did: **roll's own commands are the string
//     literals "//status", "//set" and "//theme"**, so the line-comment rule truncated them and
//     the dangling quote paired with a later one — eating most of `src/main.cpp` and making roll,
//     the primary consumer, appear not to reach seven preset functions it plainly calls.
//   - treating a backslash-newline as not an escape (a `#error "... \<newline>"` continuation).
// A scanner has no order to get wrong. The control below plants all three shapes.
std::string strip_comments_and_literals(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size();) {
    const char c = src[i];
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
      const size_t j = src.find("*/", i + 2);
      i = (j == std::string::npos) ? src.size() : j + 2;
      out += ' ';
    } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      const size_t j = src.find('\n', i);
      i = (j == std::string::npos) ? src.size() : j;
      out += ' ';
    } else if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < src.size() && src[j] != c) j += (src[j] == '\\') ? 2 : 1;
      out += ' ';
      i = (j >= src.size()) ? src.size() : j + 1;
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

