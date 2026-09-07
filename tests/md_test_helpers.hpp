// rolltui/tests/md_test_helpers.hpp — the markdown and transcript tests' own views over the
// line store's C structs. These were C++ members naming std::span and
// std::string_view; the library's shape no longer names either, so the tests that read the
// store make their views here.
#pragma once
#include <cstddef>
#include <span>
#include <string_view>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_md_lines.h"  /* INTERNAL: this test opts in (Phase 19 m2) */

inline std::span<const RolltuiMdLine> lines_of(const RolltuiEntryLayout& e) {
  const RolltuiMdLine* p = e.store ? rolltui_md_lines_all(e.store) : nullptr;
  const std::size_t n = e.store ? rolltui_md_lines_count(e.store) : 0;
  return {p + (p ? e.body : 0), n > e.body ? n - e.body : 0};
}
inline std::string_view text_of(const RolltuiEntryLayout& e) {
  return e.store ? std::string_view(rolltui_md_lines_text(e.store), rolltui_md_lines_text_size(e.store)) : std::string_view();
}
inline std::span<const RolltuiMdCodeBlock> code_blocks_of(const RolltuiEntryLayout& e) {
  const RolltuiMdCodeBlock* p = e.store ? rolltui_md_lines_code_blocks(e.store) : nullptr;
  return {p, p ? rolltui_md_lines_code_block_count(e.store) : 0};
}
inline std::span<const RolltuiMdSpan> spans_of(const RolltuiMdLine& l) { return {l.span_p, l.span_n}; }
inline std::string_view text_of(const RolltuiMdSpan& s) { return {s.text_p, s.text_n}; }
inline std::string_view href_of(const RolltuiMdSpan& s) { return {s.href_p, s.href_n}; }
inline std::span<const std::uint32_t> sources_of(const RolltuiMdSpan& s) { return {s.src_p, s.src_n}; }
inline std::string_view lang_of(const RolltuiMdCodeBlock& b) { return {b.lang_p, b.lang_n}; }
