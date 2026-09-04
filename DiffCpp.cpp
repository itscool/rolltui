// rolltui/DiffCpp.cpp — the C++ side of the unified-diff colouriser, behind the same
// boundary as `c/rolltui_diff.c` (rolltui/c/rolltui_diff.h, Phase 15 m2). One CMake flag
// picks which of the two links; both satisfy the per-line table in
// `rolltui/tests/markdown_test.cpp`, so a behavioural difference is a test failure on the
// day it appears rather than a review comment.
//
// This is the code Phase 12 m5b wrote, moved behind the boundary and given the handle's
// buffers to reuse. It is deliberately NOT a transliteration of the C: the comparison m2
// is here to make is between two languages writing the same design naturally, not between
// one language and the other's shadow.
#include "rolltui/c/rolltui_diff.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include "rolltui/Unicode.hpp"

namespace {

// What one line of a diff IS, for the pairing rule. The file headers are tested before the
// markers because "+++ b/x" starts with '+' and is not an added line — getting that order
// wrong looks right in every screenshot that happens to start at a hunk.
enum class Kind { Added, Removed, Other };

Kind kind_of(std::string_view line) {
  if (line.empty()) return Kind::Other;
  if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) return Kind::Other;
  if (line[0] == '+') return Kind::Added;
  if (line[0] == '-') return Kind::Removed;
  return Kind::Other;
}

}  // namespace

// The caller's working memory, in the shape C++ keeps it: containers that grow to a
// high-water mark and are cleared rather than freed. One buffer per ROLE, so the two sides
// of a pair cannot alias each other's token ranges.
struct RolltuiDiffScratch {
  std::vector<rolltui::unicode::DecodedChar> cs;
  std::vector<char32_t> cps;
  std::vector<rolltui::unicode::ByteRange> ta, tb;
};

namespace {

std::string_view line_of(const void* block, RolltuiDiffLineFn line_at, std::size_t i) {
  std::size_t len = 0;
  const char* p = line_at(block, i, &len);
  return std::string_view(p, len);
}

Kind kind_at(const void* block, RolltuiDiffLineFn line_at, std::size_t i) {
  return kind_of(line_of(block, line_at, i));
}

// UAX #29 word tokens of `s` as byte ranges — the same segmentation double-click uses, so
// "a word" means one thing in this library. A run of spaces is one token (WB3d) and
// punctuation is one token per character (WB999), which is what makes the common-affix
// comparison below land on boundaries a reader would call words.
void tokens(RolltuiDiffScratch& s, std::string_view text, std::vector<rolltui::unicode::ByteRange>& out) {
  using namespace rolltui;
  out.clear();
  if (text.empty()) return;
  unicode::decode_utf8_into(text, s.cs);
  s.cps.clear();
  for (const unicode::DecodedChar& c : s.cs) s.cps.push_back(c.cp);
  const std::vector<bool> b = unicode::word_boundaries(s.cps);
  std::size_t start = 0;
  for (std::size_t i = 1; i <= s.cs.size(); ++i) {
    if (i < b.size() && !b[i]) continue;
    const std::size_t begin = s.cs[start].offset;
    const std::size_t end = i < s.cs.size() ? s.cs[i].offset : text.size();
    out.push_back({begin, end});
    start = i;
  }
}

// The changed middle of `a` against `b`, both WITHOUT their marker byte, as a byte range in
// `a`'s own space; `off` is added so the caller gets offsets in the full line.
bool changed_run(RolltuiDiffScratch& s, std::string_view a, std::string_view b, std::size_t off, std::size_t& begin,
                 std::size_t& end) {
  tokens(s, a, s.ta);
  tokens(s, b, s.tb);
  if (s.ta.empty() || s.tb.empty()) return false;
  auto text = [](std::string_view src, rolltui::unicode::ByteRange r) { return src.substr(r.begin, r.end - r.begin); };
  const std::size_t n = std::min(s.ta.size(), s.tb.size());
  std::size_t p = 0;
  while (p < n && text(a, s.ta[p]) == text(b, s.tb[p])) ++p;
  std::size_t suf = 0;
  while (suf < n - p && text(a, s.ta[s.ta.size() - 1 - suf]) == text(b, s.tb[s.tb.size() - 1 - suf])) ++suf;
  // Nothing common at either end: the whole line changed, and the line's own role already
  // says so. Marking it a second time is noise, not information.
  if (p == 0 && suf == 0) return false;
  if (p + suf >= s.ta.size()) return false;  // this side's middle is empty: nothing of ITS own changed
  begin = off + s.ta[p].begin;
  end = off + s.ta[s.ta.size() - 1 - suf].end;
  return true;
}

constexpr std::size_t kNoPartner = static_cast<std::size_t>(-1);

// The partner line of a changed line, under the pairing rule in Diff.hpp: a maximal run of
// k removals immediately followed by a run of k additions pairs i with i.
std::size_t partner_of(const void* block, std::size_t line_count, RolltuiDiffLineFn line_at, std::size_t index) {
  const Kind k = kind_at(block, line_at, index);
  if (k == Kind::Other) return kNoPartner;
  // The removal run [rs, re) and the addition run [as, ae) that must abut it.
  std::size_t rs, re, as, ae;
  if (k == Kind::Removed) {
    rs = index;
    while (rs > 0 && kind_at(block, line_at, rs - 1) == Kind::Removed) --rs;
    re = index + 1;
    while (re < line_count && kind_at(block, line_at, re) == Kind::Removed) ++re;
    as = re;
  } else {
    as = index;
    while (as > 0 && kind_at(block, line_at, as - 1) == Kind::Added) --as;
    re = as;
    rs = as;
    while (rs > 0 && kind_at(block, line_at, rs - 1) == Kind::Removed) --rs;
  }
  ae = as;
  while (ae < line_count && kind_at(block, line_at, ae) == Kind::Added) ++ae;
  const std::size_t removals = re - rs, additions = ae - as;
  if (removals == 0 || removals != additions) return kNoPartner;
  return k == Kind::Removed ? as + (index - rs) : rs + (index - as);
}

}  // namespace

extern "C" {

// OWNED, through a `unique_ptr` that is released into the caller's hands and taken back
// when it comes home — the same idiom `WrapCpp.cpp` uses, and the only sanctioned way for
// C++ to mint a handle for a C boundary (no hand-rolled new/delete anywhere).
RolltuiDiffScratch* rolltui_diff_scratch_new(void) { return std::make_unique<RolltuiDiffScratch>().release(); }

void rolltui_diff_scratch_free(RolltuiDiffScratch* s) {
  const std::unique_ptr<RolltuiDiffScratch> owned(s);  // takes it back, and frees it on the way out
}

int rolltui_diff_is_language(const char* lang, size_t lang_len) {
  const std::string_view l(lang, lang_len);
  return l == "diff" || l == "patch" || l == "udiff";
}

size_t rolltui_diff_spans(RolltuiDiffScratch* s, const char* lang, size_t lang_len, const void* block,
                          size_t line_count, RolltuiDiffLineFn line_at, size_t index,
                          const RolltuiDiffRoles* roles, RolltuiDiffSpan* out, size_t out_cap) {
  if (!rolltui_diff_is_language(lang, lang_len)) return 0;  // the fence decides; content is never sniffed
  if (index >= line_count || out_cap < ROLLTUI_DIFF_MAX_SPANS) return 0;
  const std::string_view line = line_of(block, line_at, index);
  // The WHOLE line takes the role, not just its marker: a half-coloured line reads as a
  // rendering bug, and the marker is doing separate work (it is the non-colour signal).
  auto whole = [&](unsigned char role) {
    out[0] = {0, line.size(), role};
    return std::size_t{1};
  };
  if (line.empty()) return whole(roles->context);
  // The file headers come BEFORE the +/- test, because "+++ b/x" starts with '+' and is
  // not an added line. Getting this order wrong is the kind of thing that looks right in
  // every screenshot with a hunk in it.
  if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) return whole(roles->file_header);
  if (line.rfind("@@", 0) == 0) return whole(roles->hunk);
  const Kind k = kind_of(line);
  if (k == Kind::Other) return whole(roles->context);
  const unsigned char line_role = k == Kind::Added ? roles->added : roles->removed;
  const unsigned char word_role = k == Kind::Added ? roles->added_word : roles->removed_word;
  std::size_t b = 0, e = 0;
  const std::size_t partner = partner_of(block, line_count, line_at, index);
  // A partner is always a marked line, so both sides have their marker byte to drop.
  if (partner != kNoPartner &&
      changed_run(*s, line.substr(1), line_of(block, line_at, partner).substr(1), 1, b, e)) {
    // Three NON-OVERLAPPING spans. The renderer resolves an overlap by dropping the later
    // span and reporting it, so a highlighter that relied on being clamped into shape
    // would be one that is wrong (Diff.hpp).
    std::size_t n = 0;
    if (b > 0) out[n++] = {0, b, line_role};
    out[n++] = {b, e, word_role};
    if (e < line.size()) out[n++] = {e, line.size(), line_role};
    return n;
  }
  return whole(line_role);
}

}  // extern "C"
