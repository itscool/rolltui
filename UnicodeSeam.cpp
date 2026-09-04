// rolltui/UnicodeSeam.cpp — TODAY'S IMPLEMENTATION OF THE UNICODE SEAM (Phase 14 m3).
//
// `rolltui/c/rolltui_unicode.h` is the contract; this file answers it in C++ so that m3 can
// port `Wrap` while `Unicode` is still C++. m4 replaces this file with C and touches neither
// the header nor a caller — which is the whole point of having stated the seam.
//
// **IT IS COMPILED INTO BOTH CONFIGURATIONS, not only into `ROLLTUI_C=ON`.** A bridge that
// only one build links is a bridge only one build can find a defect in, and the shared-oracle
// property (plan/phase-14.md) is worth more than the handful of bytes.
//
// THE COPY IN THE MIDDLE IS DELIBERATE AND TEMPORARY. `line_break_opportunities_into` fills a
// `std::vector<Break>` and `grapheme_boundaries_into` a `std::vector<bool>`; the seam's
// contract is a caller's `unsigned char` array, so each answer is written twice. The buffers
// are LENT scratch (rolltui/Scratch.hpp), so they allocate once per thread and never again —
// and m4 deletes both the copy and the vectors by making the C the implementation rather
// than the caller. Widening `Unicode.hpp`'s own signatures to take spans instead would have
// been the alternative, and it is m4's job rather than a change to make twice.
#include <span>
#include <vector>

#include "rolltui/Scratch.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/c/rolltui_unicode.h"

namespace {

using rolltui::unicode::Break;

// The header's three constants are these three values, checked by the compiler rather than
// by a reader. If `Break` is ever renumbered, this file stops building instead of quietly
// telling the wrap engine that a mandatory break is an allowed one.
static_assert(static_cast<int>(Break::Prohibited) == ROLLTUI_BREAK_PROHIBITED, "Break::Prohibited moved");
static_assert(static_cast<int>(Break::Allowed) == ROLLTUI_BREAK_ALLOWED, "Break::Allowed moved");
static_assert(static_cast<int>(Break::Mandatory) == ROLLTUI_BREAK_MANDATORY, "Break::Mandatory moved");
// And the seam's scalar is C++'s own code-point type, so nothing below casts a pointer.
static_assert(std::is_same_v<RolltuiCodepoint, char32_t>, "the seam's code point must BE char32_t in C++");

}  // namespace

extern "C" size_t rolltui_u_decode_utf8(const char* s, size_t len, RolltuiCodepoint* cp, size_t* offset,
                                        size_t* length) {
  const std::string_view sv(s, len);
  std::size_t n = 0;
  for (std::size_t pos = 0; pos < len;) {
    const rolltui::unicode::DecodedChar d = rolltui::unicode::decode_one(sv, pos);
    cp[n] = d.cp;
    offset[n] = d.offset;
    length[n] = d.length;
    ++n;
    pos += d.length;
  }
  return n;
}

extern "C" void rolltui_u_line_break_opportunities(const RolltuiCodepoint* cps, size_t n, unsigned char* out) {
  static thread_local rolltui::Scratch<std::vector<Break>> scratch("unicode seam line breaks");
  auto b = scratch.lock();
  rolltui::unicode::line_break_opportunities_into(std::span<const char32_t>(cps, n), *b);
  for (std::size_t i = 0; i < b->size(); ++i) out[i] = static_cast<unsigned char>((*b)[i]);
}

extern "C" void rolltui_u_grapheme_boundaries(const RolltuiCodepoint* cps, size_t n, unsigned char* out) {
  static thread_local rolltui::Scratch<std::vector<bool>> scratch("unicode seam grapheme boundaries");
  auto b = scratch.lock();
  rolltui::unicode::grapheme_boundaries_into(std::span<const char32_t>(cps, n), *b);
  for (std::size_t i = 0; i < b->size(); ++i) out[i] = (*b)[i] ? 1u : 0u;
}

extern "C" int rolltui_u_cluster_width(const RolltuiCodepoint* cps, size_t n, int ambiguous_wide) {
  return rolltui::unicode::cluster_width(std::span<const char32_t>(cps, n), ambiguous_wide != 0);
}
