// rolltui/Diff.cpp — see Diff.hpp.
#include "rolltui/Diff.hpp"

namespace rolltui {

bool is_diff_language(std::string_view lang) { return lang == "diff" || lang == "patch" || lang == "udiff"; }

std::vector<markdown::HighlightSpan> diff_spans(std::string_view lang, std::string_view line) {
  std::vector<markdown::HighlightSpan> out;
  if (!is_diff_language(lang)) return out;  // the fence decides; content is never sniffed
  // The WHOLE line takes the role, not just its marker: a half-coloured line reads as a
  // rendering bug, and the marker is doing separate work (it is the non-colour signal).
  auto whole = [&](Role r) { out.push_back({0, line.size(), r}); };
  if (line.empty()) { whole(Role::diff_context); return out; }
  // The file headers come BEFORE the +/- test, because "+++ b/x" starts with '+' and is
  // not an added line. Getting this order wrong is the kind of thing that looks right in
  // every screenshot with a hunk in it.
  if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) { whole(Role::text_muted); return out; }
  if (line.rfind("@@", 0) == 0) { whole(Role::accent_1); return out; }
  switch (line[0]) {
    case '+': whole(Role::diff_added); break;
    case '-': whole(Role::diff_removed); break;
    default: whole(Role::diff_context); break;  // ' ', '\\', and anything else
  }
  return out;
}

}  // namespace rolltui
