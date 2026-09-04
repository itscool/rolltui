// rolltui/Diff.cpp — see Diff.hpp.
#include "rolltui/Diff.hpp"

#include "rolltui/Unicode.hpp"

namespace rolltui {
namespace {

// What one line of a diff IS, for the pairing rule. The file headers are tested before
// the markers because "+++ b/x" starts with '+' and is not an added line — getting that
// order wrong looks right in every screenshot that happens to start at a hunk.
enum class Kind { Added, Removed, Other };

Kind kind_of(std::string_view line) {
  if (line.empty()) return Kind::Other;
  if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) return Kind::Other;
  if (line[0] == '+') return Kind::Added;
  if (line[0] == '-') return Kind::Removed;
  return Kind::Other;
}

// UAX #29 word tokens of `s` as byte ranges — the same segmentation double-click uses,
// so "a word" means one thing in this library. A run of spaces is one token (WB3d) and
// punctuation is one token per character (WB999), which is what makes the common-affix
// comparison below land on boundaries a reader would call words.
std::vector<unicode::ByteRange> tokens(std::string_view s) {
  const std::vector<unicode::DecodedChar> cs = unicode::decode_utf8(s);
  std::vector<char32_t> cps;
  cps.reserve(cs.size());
  for (const unicode::DecodedChar& c : cs) cps.push_back(c.cp);
  const std::vector<bool> b = unicode::word_boundaries(cps);
  std::vector<unicode::ByteRange> out;
  std::size_t start = 0;
  for (std::size_t i = 1; i <= cs.size(); ++i) {
    if (i < b.size() && !b[i]) continue;
    const std::size_t begin = cs[start].offset;
    const std::size_t end = i < cs.size() ? cs[i].offset : s.size();
    out.push_back({begin, end});
    start = i;
  }
  return out;
}

// The changed middle of `s` against `other`, both WITHOUT their marker byte, as a byte
// range in `s`'s own space; {0, 0} when there is nothing to mark. `off` is added to the
// result so callers get offsets in the full line.
bool changed_run(std::string_view s, std::string_view other, std::size_t off, std::size_t& begin, std::size_t& end) {
  const std::vector<unicode::ByteRange> a = tokens(s), o = tokens(other);
  if (a.empty() || o.empty()) return false;
  auto text = [](std::string_view src, unicode::ByteRange r) { return src.substr(r.begin, r.end - r.begin); };
  const std::size_t n = std::min(a.size(), o.size());
  std::size_t p = 0;
  while (p < n && text(s, a[p]) == text(other, o[p])) ++p;
  std::size_t suf = 0;
  while (suf < n - p && text(s, a[a.size() - 1 - suf]) == text(other, o[o.size() - 1 - suf])) ++suf;
  // Nothing common at either end: the whole line changed, and the line's own role
  // already says so. Marking it a second time is noise, not information.
  if (p == 0 && suf == 0) return false;
  if (p + suf >= a.size()) return false;  // this side's middle is empty: nothing of ITS own changed
  begin = off + a[p].begin;
  end = off + a[a.size() - 1 - suf].end;
  return true;
}

// The partner line of a changed line, under the pairing rule in Diff.hpp: a maximal run
// of k removals immediately followed by a run of k additions pairs i with i. Returns
// nullptr when this line is not in such a pair.
const std::string* partner_of(std::span<const std::string> lines, std::size_t index) {
  const Kind k = kind_of(lines[index]);
  if (k == Kind::Other) return nullptr;
  // The removal run [rs, re) and the addition run [as, ae) that must abut it.
  std::size_t rs, re, as, ae;
  if (k == Kind::Removed) {
    rs = index;
    while (rs > 0 && kind_of(lines[rs - 1]) == Kind::Removed) --rs;
    re = index + 1;
    while (re < lines.size() && kind_of(lines[re]) == Kind::Removed) ++re;
    as = re;
  } else {
    as = index;
    while (as > 0 && kind_of(lines[as - 1]) == Kind::Added) --as;
    re = as;
    rs = as;
    while (rs > 0 && kind_of(lines[rs - 1]) == Kind::Removed) --rs;
  }
  ae = as;
  while (ae < lines.size() && kind_of(lines[ae]) == Kind::Added) ++ae;
  const std::size_t removals = re - rs, additions = ae - as;
  if (removals == 0 || removals != additions) return nullptr;
  return k == Kind::Removed ? &lines[as + (index - rs)] : &lines[rs + (index - as)];
}

}  // namespace

bool is_diff_language(std::string_view lang) { return lang == "diff" || lang == "patch" || lang == "udiff"; }

std::vector<markdown::HighlightSpan> diff_spans(std::string_view lang, std::span<const std::string> lines,
                                                std::size_t index) {
  std::vector<markdown::HighlightSpan> out;
  if (!is_diff_language(lang)) return out;  // the fence decides; content is never sniffed
  if (index >= lines.size()) return out;
  const std::string_view line = lines[index];
  // The WHOLE line takes the role, not just its marker: a half-coloured line reads as a
  // rendering bug, and the marker is doing separate work (it is the non-colour signal).
  auto whole = [&](Role r) { out.push_back({0, line.size(), r}); };
  if (line.empty()) { whole(Role::diff_context); return out; }
  // The file headers come BEFORE the +/- test, because "+++ b/x" starts with '+' and is
  // not an added line. Getting this order wrong is the kind of thing that looks right in
  // every screenshot with a hunk in it.
  if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) { whole(Role::text_muted); return out; }
  if (line.rfind("@@", 0) == 0) { whole(Role::accent_1); return out; }
  const Kind k = kind_of(line);
  if (k == Kind::Other) { whole(Role::diff_context); return out; }
  const Role line_role = k == Kind::Added ? Role::diff_added : Role::diff_removed;
  const Role word_role = k == Kind::Added ? Role::diff_added_word : Role::diff_removed_word;
  std::size_t b = 0, e = 0;
  const std::string* partner = partner_of(lines, index);
  if (partner && changed_run(line.substr(1), std::string_view(*partner).substr(1), 1, b, e)) {
    // Three NON-OVERLAPPING spans. The renderer resolves an overlap by dropping the
    // later span and reporting it, so a highlighter that relied on being clamped into
    // shape would be one that is wrong (Diff.hpp).
    if (b > 0) out.push_back({0, b, line_role});
    out.push_back({b, e, word_role});
    if (e < line.size()) out.push_back({e, line.size(), line_role});
    return out;
  }
  whole(line_role);
  return out;
}

}  // namespace rolltui
