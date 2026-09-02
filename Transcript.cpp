// rolltui/Transcript.cpp — see Transcript.hpp.
#include "rolltui/Transcript.hpp"

#include <algorithm>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"

namespace rolltui {

std::vector<TranscriptLine> layout_transcript(const Document& doc, const TranscriptLayoutOptions& opt) {
  std::vector<TranscriptLine> out;
  const int width = std::max(opt.width, 1);
  for (std::size_t i = 0; i < doc.entries.size(); ++i) {
    const DocEntry& e = doc.entries[i];
    if (i > 0)
      for (int g = 0; g < opt.gap; ++g) out.push_back({markdown::StyledLine{}, i});
    int prefix_w = unicode::display_width(e.prefix, opt.ambiguous_wide);
    int inner = std::max(width - prefix_w, 1);
    std::vector<markdown::StyledLine> lines;
    if (e.markdown) {
      markdown::RenderOptions ro;
      ro.width = inner;
      ro.ambiguous_wide = opt.ambiguous_wide;
      ro.tab_width = opt.tab_width;
      ro.base = e.role;
      lines = markdown::render(e.text, ro);
    } else {
      WrapOptions wo;
      wo.ambiguous_wide = opt.ambiguous_wide;
      wo.tab_width = opt.tab_width;
      for (const Line& l : wrap(e.text, inner, wo)) {
        markdown::StyledLine sl;
        if (l.indent > 0) sl.spans.push_back({std::string(static_cast<std::size_t>(l.indent), ' '), l.indent, e.role});
        sl.spans.push_back({l.text, l.width, e.role});
        sl.width = l.width + l.indent;
        lines.push_back(std::move(sl));
      }
    }
    if (lines.empty()) lines.push_back({});
    for (std::size_t k = 0; k < lines.size(); ++k) {
      markdown::StyledLine sl;
      if (prefix_w > 0) {
        if (k == 0) sl.spans.push_back({e.prefix, prefix_w, e.prefix_role});
        else sl.spans.push_back({std::string(static_cast<std::size_t>(prefix_w), ' '), prefix_w, e.role});
        sl.width = prefix_w;
      }
      for (const markdown::Span& s : lines[k].spans) sl.spans.push_back(s);
      sl.width += lines[k].width;
      out.push_back({std::move(sl), i});
    }
  }
  return out;
}

namespace {
std::size_t max_top(std::size_t total, int h) {
  std::size_t hh = static_cast<std::size_t>(std::max(h, 0));
  return total > hh ? total - hh : 0;
}
}  // namespace

void scroll_by(ScrollState& s, long delta, std::size_t total_lines, int viewport_height) {
  std::size_t mt = max_top(total_lines, viewport_height);
  long t = static_cast<long>(s.top) + delta;
  if (t < 0) t = 0;
  if (static_cast<std::size_t>(t) > mt) t = static_cast<long>(mt);
  s.top = static_cast<std::size_t>(t);
  s.follow = (s.top >= mt);
}

void scroll_to_bottom(ScrollState& s, std::size_t total_lines, int viewport_height) {
  s.top = max_top(total_lines, viewport_height);
  s.follow = true;
}

void scroll_to_top(ScrollState& s) {
  s.top = 0;
  s.follow = false;
}

void reconcile_scroll(ScrollState& s, std::size_t total_lines, int viewport_height) {
  std::size_t mt = max_top(total_lines, viewport_height);
  if (s.follow || s.top > mt) s.top = mt;
  if (s.top >= mt) s.follow = true;
}

std::size_t draw_transcript(Frame& frame, Rect area, const std::vector<TranscriptLine>& lines,
                            const ScrollState& s, const Theme& theme, bool ambiguous_wide) {
  area = area.intersect(frame.bounds());
  if (area.empty()) return 0;
  frame.fill(area, theme.style(Role::background));
  for (int row = 0; row < area.h; ++row) {
    std::size_t idx = s.top + static_cast<std::size_t>(row);
    if (idx >= lines.size()) break;
    int x = area.x;
    for (const markdown::Span& sp : lines[idx].line.spans) {
      if (x >= area.x + area.w) break;
      x += frame.put_text(x, area.y + row, sp.text, theme.style(sp.role), area.x + area.w - x, ambiguous_wide);
    }
  }
  std::size_t shown_end = s.top + static_cast<std::size_t>(area.h);
  return lines.size() > shown_end ? lines.size() - shown_end : 0;
}

}  // namespace rolltui
