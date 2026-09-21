// rolltui/tests/syntax_test_helpers.hpp — what the two syntax suites share: a text shown as `{class|text}` runs, so a test
// can be READ, and small helpers around adding and finding a language.
#pragma once
#include <cstddef>
#include <string>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_syntax.h"  /* INTERNAL: the suites that include this are in ROLLTUI_INTERNAL_OPT_IN */

namespace syntest {

inline std::size_t live_bytes() {
  std::size_t live = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &live, nullptr, nullptr);
  return live;
}

// The text with each coloured run marked `{class|text}` and the rest as it was.
inline std::string marked(RolltuiSyntax* s, int lang, const std::string& text) {
  RolltuiHighlight* h = rolltui_highlight_new();
  std::string out;
  if (rolltui_highlight_run(h, s, lang, text.data(), text.size()) < 0) { rolltui_highlight_free(h); return "<no language>"; }
  std::size_t at = 0;
  for (std::size_t li = 0; li < rolltui_highlight_line_count(h); ++li) {
    const std::size_t nl = text.find('\n', at);
    std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const RolltuiSyntaxRun* runs = nullptr;
    const std::size_t n = rolltui_highlight_line(h, li, &runs);
    std::size_t pos = 0;
    for (std::size_t i = 0; i < n; ++i) {
      out += line.substr(pos, runs[i].begin - pos);
      out += std::string("{") + rolltui_syntax_class_name(runs[i].cls) + "|" + line.substr(runs[i].begin, runs[i].end - runs[i].begin) + "}";
      pos = runs[i].end;
    }
    out += line.substr(pos);
    if (li + 1 < rolltui_highlight_line_count(h)) out += "\n";
    at = nl == std::string::npos ? text.size() : nl + 1;
  }
  rolltui_highlight_free(h);
  return out;
}

inline int add(RolltuiSyntax* s, const std::string& json, std::string* why = nullptr) {
  RolltuiStr err{};
  const int ok = rolltui_syntax_add(s, json.data(), json.size(), &err);
  if (why) *why = std::string(err.p ? err.p : "", err.n);
  rolltui_str_free(&err);
  return ok;
}

inline int lang_of(RolltuiSyntax* s, const std::string& name) { return rolltui_syntax_find_name(s, name.data(), name.size()); }

}  // namespace syntest
