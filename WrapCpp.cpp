// rolltui/WrapCpp.cpp — THE C++ SIDE OF THE WRAP ENGINE (Phase 14 m3).
//
// `rolltui/c/rolltui_wrap.c` is the other one, and `-DROLLTUI_C` picks which links. Same
// header, same symbols, same answers; the wrap property tests at every width 0..48 over a
// corpus and 3,600 seeded random cases are the oracle for both (rolltui/tests/wrap_test.cpp).
//
// Same algorithm as before the port — same UAX #14 breaks, same rules, same reused buffers —
// with ONE change to the DATA MODEL, made on both sides because it is a property of the
// design and not of the language. **A LINE NO LONGER OWNS ITS BYTES.** There are three
// buffers per handle — every line's text end to end, every line's graphemes end to end, and
// a record per line saying where its slice starts — instead of a `std::string` and a
// `std::vector` per line.
//
// THE PORT IS WHAT FORCED IT, and it paid for itself twice:
//   - a C line cannot hold a `std::string`, so the alternative was two mallocs per line and
//     a free loop, which is more C code to do less;
//   - the line under construction now lives at the END of the shared text buffer, so the
//     old **tail copy is gone**: cutting at a soft break used to build a fresh
//     `std::vector<WrapGrapheme>` and a fresh `std::string` for the tail, emit the head, and
//     re-append the tail — two allocations per wrapped line, on the hottest path in the
//     library, which Phase 13 walked past three times. Emitting a PREFIX and erasing the
//     dropped spaces between head and tail does the same thing with none.
//     "Why does this copy exist?" (CLAUDE.md) had an answer, and the answer was "it doesn't".
//
// AND ONE OF PHASE 13's RULES STOPS APPLYING HERE, which is worth saying because it applied
// so loudly before: `clear()` was the wrong reset for the old line vector because each `Line`
// OWNED a string and a vector, and clearing freed exactly the storage being reused. A line
// record owns nothing now, so `clear()` on all three buffers is the right reset and the
// live-count bookkeeping the old code needed is gone.
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_wrap.h"

struct RolltuiWrapLines {
  // Where one line's slice of the two shared buffers is. Owns nothing.
  struct Rec {
    std::size_t text_off, text_len, g_off, g_count;
    int width, indent;
    bool hard;
  };
  // One source grapheme cluster, before it is placed. `brk` is a ROLLTUI_BREAK_* value.
  struct Cluster {
    std::size_t byte0, byte1;
    int width;
    unsigned char brk, space, tab, newline;
  };

  std::string text;                        // every line's bytes, in order
  std::vector<RolltuiWrapGrapheme> gs;     // every line's clusters, in order
  std::vector<Rec> lines;

  // SCRATCH, owned by this handle and reused across every wrap into it. It lives here
  // rather than in a thread_local because the C side is required to hold no global state
  // (rolltui/c/rolltui_wrap.h), and matching that is what keeps the two implementations
  // comparable — a difference in WHERE the buffers live is a difference in what the budget
  // measures.
  std::vector<char32_t> cps;
  std::vector<std::size_t> coff, clen;
  std::vector<unsigned char> brk, bounds;
  std::vector<Cluster> clusters;
  // The Unicode algorithms' working memory, owned here for the same reason everything else in
  // this handle is: a wrap is the only thing that asks for it, so a caller that keeps one
  // handle keeps one of these, and nothing holds hidden per-thread state for it.
  //
  // **MADE LAZY BY THE BUDGET, which is what the budget is for.** Creating it eagerly cost one
  // allocation per handle, and `wrap()` mints a fresh handle per call to hand the lines over —
  // so a resize frame went up by exactly 281, the number of `wrap()` calls in it. A handed-over
  // result is READ, never wrapped into, so it never needs this at all.
  RolltuiUnicodeScratch* uni = nullptr;
  RolltuiWrapLines() = default;
  RolltuiWrapLines(const RolltuiWrapLines&) = delete;
  RolltuiWrapLines& operator=(const RolltuiWrapLines&) = delete;
  ~RolltuiWrapLines() { rolltui_u_scratch_free(uni); }
};

// ---- lifetime ----------------------------------------------------------------------------

extern "C" RolltuiWrapLines* rolltui_wrap_new(void) { return std::make_unique<RolltuiWrapLines>().release(); }

extern "C" void rolltui_wrap_free(RolltuiWrapLines* w) {
  const std::unique_ptr<RolltuiWrapLines> owned(w);  // takes it back, and frees it on the way out
}

extern "C" void rolltui_wrap_reset(RolltuiWrapLines* w) {
  w->text.clear();
  w->gs.clear();
  w->lines.clear();
}

// ---- the engine ---------------------------------------------------------------------------

extern "C" void rolltui_wrap(RolltuiWrapLines* w, const char* utf8, size_t len, int width,
                             RolltuiWrapOptions opt) {
  using Cluster = RolltuiWrapLines::Cluster;
  using Rec = RolltuiWrapLines::Rec;

  // Decode straight into three parallel arrays. The seam narrowed what the engine asks for
  // (rolltui/c/rolltui_unicode.h): there is no array of structs and therefore no copy loop
  // to get a contiguous code-point array back out of one.
  w->cps.resize(len);
  w->coff.resize(len);
  w->clen.resize(len);
  const std::size_t n = rolltui_u_decode_utf8(utf8, len, w->cps.data(), w->coff.data(), w->clen.data());
  w->brk.resize(n + 1);
  w->bounds.resize(n + 1);
  if (!w->uni) w->uni = rolltui_u_scratch_new();  // this handle is being used as an engine
  rolltui_u_line_break_opportunities(w->uni, w->cps.data(), n, w->brk.data());
  rolltui_u_grapheme_boundaries(w->uni, w->cps.data(), n, w->bounds.data());

  // One entry per grapheme cluster, in source order.
  w->clusters.clear();
  for (std::size_t i = 0, start = 0; i < n; ++i) {
    if (!w->bounds[i + 1]) continue;
    Cluster c;
    c.byte0 = w->coff[start];
    c.byte1 = w->coff[i] + w->clen[i];
    c.width = rolltui_u_cluster_width(w->cps.data() + start, i + 1 - start, opt.ambiguous_wide);
    c.brk = w->brk[start];
    const char32_t c0 = w->cps[start];
    c.space = (i == start && c0 == 0x20) ? 1u : 0u;
    c.tab = (i == start && c0 == U'\t') ? 1u : 0u;
    // CR LF is one cluster (GB3).
    c.newline = (c0 == 0x0A || c0 == 0x0B || c0 == 0x0C || c0 == 0x0D || c0 == 0x85 || c0 == 0x2028 ||
                 c0 == 0x2029)
                    ? 1u
                    : 0u;
    w->clusters.push_back(c);
    start = i + 1;
  }

  const bool nothing = (width <= 0);
  auto indent_for = [&](bool first) {
    int ind = first ? opt.first_indent : opt.hanging_indent;
    if (ind < 0) ind = 0;
    if (!nothing && ind >= width) ind = width - 1;  // keep at least one cell of text
    return ind;
  };

  rolltui_wrap_reset(w);
  // THE LINE UNDER CONSTRUCTION lives at the END of the two shared buffers: its bytes are
  // `text[committed_text ..]` and its clusters are `gs[committed_gs ..]`. That is what makes
  // emitting a line free — the bytes are already where they belong — and it is the whole
  // reason the tail copy could go.
  std::size_t committed_text = 0, committed_gs = 0;
  int cur_width = 0;
  int cur_indent = indent_for(true);
  int avail = nothing ? 0 : width - cur_indent;
  bool any_emitted = false;
  bool pending = false;   // content seen since the last emitted line (even at width 0)
  bool dropping = false;  // inside a run of spaces being dropped at a soft break
  std::size_t last_opportunity = 0;  // cluster index within the current line

  auto cur_count = [&] { return w->gs.size() - committed_gs; };
  auto cur_bytes = [&] { return w->text.size() - committed_text; };
  auto cur_at = [&](std::size_t k) -> RolltuiWrapGrapheme& { return w->gs[committed_gs + k]; };

  // Emits the first `head` clusters of the current line as a finished line, then makes the
  // current line the clusters from `keep` on — the ones between are dropped (trailing spaces
  // at a soft break). A whole line is `head == keep == cur_count()`.
  auto emit = [&](std::size_t head, std::size_t keep, bool hard) {
    const std::size_t head_bytes = (head == cur_count()) ? cur_bytes() : cur_at(head).offset;
    const std::size_t tail0 = (keep == cur_count()) ? cur_bytes() : cur_at(keep).offset;
    int head_width = 0, dropped_width = 0;
    for (std::size_t k = 0; k < keep; ++k) (k < head ? head_width : dropped_width) += cur_at(k).width;
    w->lines.push_back(Rec{committed_text, head_bytes, committed_gs, head, head_width, cur_indent, hard});

    // Close the gap between the head and the tail. Both are erases from the middle of a
    // buffer whose tail then sits exactly where the next line starts — no copy, no temporary.
    w->text.erase(committed_text + head_bytes, tail0 - head_bytes);
    w->gs.erase(w->gs.begin() + static_cast<std::ptrdiff_t>(committed_gs + head),
                w->gs.begin() + static_cast<std::ptrdiff_t>(committed_gs + keep));
    committed_text += head_bytes;
    committed_gs += head;
    for (std::size_t k = 0; k < cur_count(); ++k) cur_at(k).offset -= tail0;
    cur_width -= head_width + dropped_width;

    cur_indent = indent_for(false);
    avail = nothing ? 0 : width - cur_indent;
    any_emitted = true;
    pending = cur_count() > 0;
    last_opportunity = 0;
    dropping = false;
  };
  auto emit_all = [&](bool hard) { emit(cur_count(), cur_count(), hard); };
  auto append = [&](const char* bytes, std::size_t nb, std::size_t source_offset, int gw, unsigned char space) {
    w->gs.push_back(RolltuiWrapGrapheme{cur_bytes(), nb, source_offset, gw, space});
    w->text.append(bytes, nb);
    cur_width += gw;
    pending = true;
  };
  auto drop_trailing_spaces = [&]() {
    while (cur_count() > 0 && cur_at(cur_count() - 1).space) {
      cur_width -= cur_at(cur_count() - 1).width;
      w->text.resize(committed_text + cur_at(cur_count() - 1).offset);
      w->gs.pop_back();
    }
    if (last_opportunity > cur_count()) last_opportunity = cur_count();
  };

  for (const Cluster& g : w->clusters) {
    if (g.newline) {  // terminates the current line, even an empty one
      emit_all(true);
      continue;
    }
    if (g.brk == ROLLTUI_BREAK_ALLOWED) last_opportunity = cur_count();
    if (g.width > 0 || g.tab || g.space) pending = true;
    if (g.tab) {
      if (nothing) continue;
      const int tw = opt.tab_width > 0 ? opt.tab_width : 8;
      const int stop = tw - (cur_width % tw);
      for (int k = 0; k < stop; ++k) {
        if (cur_width + 1 > avail) { dropping = true; break; }  // overflowing spaces: dropped
        if (dropping) break;
        append(" ", 1, g.byte0, 1, 1);
      }
      continue;
    }
    if (g.width == 0) continue;  // draws nothing
    if (nothing) continue;
    const char* bytes = utf8 + g.byte0;
    const std::size_t nb = g.byte1 - g.byte0;
    if (g.space) {
      if (dropping || cur_width + g.width > avail) { dropping = true; continue; }
      append(bytes, nb, g.byte0, g.width, 1);
      continue;
    }
    if (dropping) {
      // A non-space after dropped spaces starts the next line (SP ÷ is the opportunity).
      drop_trailing_spaces();
      emit_all(false);
    }
    if (cur_width + g.width > avail && cur_count() > 0) {
      if (last_opportunity > 0 && last_opportunity < cur_count()) {
        // Cut at the last opportunity: the head goes out, the tail stays. The head's
        // trailing spaces are dropped by emitting fewer clusters than the cut point.
        const std::size_t keep = last_opportunity;
        std::size_t head = keep;
        while (head > 0 && cur_at(head - 1).space) --head;
        emit(head, keep, false);
        if (g.brk == ROLLTUI_BREAK_ALLOWED) last_opportunity = cur_count();
        // g still has to be placed; the tail holds no opportunity but its start, so
        // if it does not fit now the only remaining cut is right before it.
        if (cur_width + g.width > avail && cur_count() > 0) {
          drop_trailing_spaces();
          emit_all(false);
        }
      } else {
        // No opportunity inside the line (or only at its start): hard-break here.
        drop_trailing_spaces();
        emit_all(false);
      }
    }
    append(bytes, nb, g.byte0, g.width, 0);
  }
  // End of text: the unterminated last segment is a line if it has content, or if it
  // is the whole (empty) text. A trailing newline has already emitted its line.
  if (pending || !any_emitted) emit_all(true);
}

// THE ONE PLACE THE TWO IMPLEMENTATIONS ARE NOT THE SAME DESIGN, and it is not for want of
// trying. The C carves the handle and all three arrays out of ONE allocation, because their
// sizes are known the moment a result exists; three `std::` containers structurally cannot —
// each owns its own block by definition, and the only way to match it here is to stop being
// three containers, which is C code written in C++ and answers a question nobody asked (m2's
// rule for `ScreenCpp.cpp`). So this stays idiomatic and costs one allocation per container
// that is not empty or short enough to sit inline, plus the handle. **That asymmetry is the
// finding, and it points the other way from the one m3 first wrote down:** the interesting
// thing is not that C lacks the small-string optimisation, it is that C can put the whole
// result in one block and a container-based C++ cannot.
extern "C" RolltuiWrapLines* rolltui_wrap_clone(const RolltuiWrapLines* src) {
  std::unique_ptr<RolltuiWrapLines> w = std::make_unique<RolltuiWrapLines>();
  w->text = src->text;    // the scratch is deliberately not copied: a clone is read, not
  w->gs = src->gs;        // wrapped into, and carrying the decode buffers would be so much
  w->lines = src->lines;  // dead weight per handed-over result
  return w.release();
}

// ---- reading the lines ---------------------------------------------------------------------

extern "C" size_t rolltui_wrap_line_count(const RolltuiWrapLines* w) { return w->lines.size(); }

extern "C" void rolltui_wrap_line(const RolltuiWrapLines* w, size_t i, const char** text, size_t* text_len,
                                  const RolltuiWrapGrapheme** graphemes, size_t* grapheme_count, int* width,
                                  int* indent, int* hard) {
  const RolltuiWrapLines::Rec& r = w->lines[i];
  *text = w->text.data() + r.text_off;  // BORROW into the handle; data() is never null
  *text_len = r.text_len;
  *graphemes = w->gs.data() + r.g_off;
  *grapheme_count = r.g_count;
  *width = r.width;
  *indent = r.indent;
  *hard = r.hard ? 1 : 0;
}
