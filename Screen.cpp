// rolltui/Screen.cpp — see Screen.hpp.
#include "rolltui/Screen.hpp"

#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_render.h"

#include "rolltui/Scratch.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

// PUT_TEXT / FILL / TINT WERE IMPLEMENTED TWICE, and this deletes the second one.
//
// `c/rolltui_frame_ops.c` has held the C versions since Phase 15 m5 — they had to exist the
// moment the widgets became C — and these three C++ methods went on looping over graphemes
// themselves beside them. Two implementations of the same drawing primitive, each correct,
// neither aware of the other: exactly the shape this library refuses everywhere else, and
// the reason `rolltui_md_lines.h` and `rolltui_layout_tree.h` are C in one definition rather
// than one per language. `rolltui_layout.h` states the rule for its error text — "a
// vocabulary written down twice is a second thing to drift" — and a drawing primitive is a
// vocabulary too. Found 2026-09-04 while sorting the C++ that is BINDING from the C++ that is
// unported LOGIC: this looked like logic and was really a duplicate.
//
// The scratch is the library's own per-thread handle, the same `ThreadHandle` pattern
// `Diff.cpp` and `Unicode.cpp` use — the C wants somewhere to decode clusters into, and that
// is a CALLER-FILLED handle rather than storage the callee invents (CLAUDE.md's third
// strategy, widened for working memory).
namespace {
RolltuiDrawScratch* draw_scratch() {
  static thread_local ThreadHandle<RolltuiDrawScratch, rolltui_draw_scratch_new, rolltui_draw_scratch_free> h;
  return h.get();
}
}  // namespace

int Frame::put_text(int x, int y, std::string_view utf8, const Style& style, int max_cells,
                    bool ambiguous_wide, std::uint32_t link) {
  return rolltui_frame_put_text(handle(), draw_scratch(), x, y, utf8.data(), utf8.size(), style,
                                max_cells, ambiguous_wide ? 1 : 0, link);
}

void Frame::fill(Rect r, const Style& style, std::string_view grapheme) {
  rolltui_frame_fill(handle(), draw_scratch(), r, style, grapheme.data(), grapheme.size());
}

void Frame::tint(Rect r, const Style& style) { rolltui_frame_tint(handle(), r, style); }

// THE THREE CONSUMERS NOW FORWARD TO C (`rolltui/c/rolltui_render.h`). `rolltui_screen.h`
// deferred them on purpose — "porting them is its own step and moves no behaviour when it
// happens" — and this is that step. What is left here is the std::string shape a C++ caller
// still writes against; the loops, the SGR state and the run-finding are all in the C.
//
// HOW THE PORT WAS VERIFIED, because "moves no behaviour" is a claim and not a hope: the
// golden-frame suites record the BYTES these produce, and they were recorded from the C++
// implementation. Forwarding to the C and keeping 61+ goldens green is a byte-for-byte
// equivalence check against every one of them, which is a stronger control than any
// differential test written for the occasion.
namespace {
// The library's own growing buffer, lent to the C and copied out once. A caller that wants
// the allocation gone entirely uses `rolltui_render_diff` directly with a buffer it keeps —
// which is what `rolltui_swap.h` exists to make the normal thing.
std::string take(RolltuiStr& s) {
  std::size_t len = 0;
  const char* p = rolltui_str_get(&s, &len);
  std::string out(p ? p : "", len);
  rolltui_str_free(&s);
  return out;
}
}  // namespace

std::string render_full(const Frame& next, ColorDepth depth) {
  RolltuiStr s{};
  rolltui_render_full(next.handle(), static_cast<unsigned char>(depth), &s);
  return take(s);
}

std::string frame_to_text(const Frame& f) {
  RolltuiStr s{};
  rolltui_frame_to_text(f.handle(), &s);
  return take(s);
}

std::string render_diff(const Frame* prev, const Frame& next, ColorDepth depth) {
  RolltuiStr s{};
  rolltui_render_diff(prev ? prev->handle() : nullptr, next.handle(), static_cast<unsigned char>(depth), &s);
  return take(s);
}

}  // namespace rolltui
