// rolltui/Diff.cpp — THE C++ SHAPE of the unified-diff colouriser. See Diff.hpp for the
// contract and the rules; the colouriser itself is behind `rolltui/c/rolltui_diff.h`, in
// `DiffCpp.cpp` or `c/rolltui_diff.c`, one CMake flag apart (plan/phase-15.md m2).
//
// There is very little here, which is the shape `Wrap.cpp` and `Unicode.cpp` arrived at
// too: a handle per thread, one accessor over whatever the caller is holding, and the
// ROLE TABLE — the only thing in this module that names a colour vocabulary at all.
#include "rolltui/Diff.hpp"

#include "rolltui/Scratch.hpp"
#include "rolltui/c/rolltui_diff.h"

namespace rolltui {
namespace {

// THE ROLE TABLE, and it is the whole reason the boundary takes roles as data. `Role` is
// the styling vocabulary of a layer that has not been ported, and mirroring the enum in a
// C header would be a second definition of it — this project's most repeated failure
// shape. So the C names no role, and this one initializer is where the mapping lives. It
// is the same table `Diff.hpp` states in prose, and `markdown_test`'s per-line-kind
// assertions are its oracle: swap two fields and the test fails.
constexpr RolltuiDiffRoles kRoles = {
    /*added=*/static_cast<unsigned char>(Role::diff_added),
    /*removed=*/static_cast<unsigned char>(Role::diff_removed),
    /*context=*/static_cast<unsigned char>(Role::diff_context),
    /*file_header=*/static_cast<unsigned char>(Role::text_muted),
    /*hunk=*/static_cast<unsigned char>(Role::accent_1),
    /*added_word=*/static_cast<unsigned char>(Role::diff_added_word),
    /*removed_word=*/static_cast<unsigned char>(Role::diff_removed_word),
};

// The block, read on demand: a BORROW of one line, never a copy and never an index. The
// block is the caller's `std::span<const std::string_view>` and outlives the call.
const char* line_at(const void* block, std::size_t i, std::size_t* len) {
  const std::string_view s = (*static_cast<const std::span<const std::string_view>*>(block))[i];
  *len = s.size();
  return s.data();
}

RolltuiDiffScratch* scratch() {
  static thread_local ThreadHandle<RolltuiDiffScratch, rolltui_diff_scratch_new, rolltui_diff_scratch_free> h;
  return h.get();
}

}  // namespace

bool is_diff_language(std::string_view lang) { return rolltui_diff_is_language(lang.data(), lang.size()) != 0; }

std::vector<markdown::HighlightSpan> diff_spans(std::string_view lang, std::span<const std::string_view> lines,
                                                std::size_t index) {
  // The bound is known WITHOUT asking: a line takes its whole role, or splits into
  // line / word / line. So the buffer is a fixed array and there is no measure-then-fill
  // round trip — the same property every caller-buffer call on these boundaries has.
  RolltuiDiffSpan buf[ROLLTUI_DIFF_MAX_SPANS];
  const std::size_t n = rolltui_diff_spans(scratch(), lang.data(), lang.size(), &lines, lines.size(), line_at, index,
                                           &kRoles, buf, ROLLTUI_DIFF_MAX_SPANS);
  std::vector<markdown::HighlightSpan> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) out.push_back({buf[i].begin, buf[i].end, static_cast<Role>(buf[i].role)});
  return out;
}

}  // namespace rolltui
