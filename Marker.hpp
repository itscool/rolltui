#pragma once
//
// rolltui/Marker.hpp — the "▼ N more" marker's text, and nothing else.
//
// It lives in a header of its own because it now has THREE callers at two different
// layers: the Transcript widget and draw_scrolled_text (Phase 12 m5), and the markdown
// renderer's capped code block (m5b). Transcript.hpp includes Markdown.hpp, so the
// renderer cannot reach a definition that lives in the widget — and duplicating it is
// exactly what the "ONE definition, so the two cannot drift" note on it was written to
// prevent. A twenty-line header is the cheapest way to keep that note true.
//
#include <cstddef>
#include <string>

#include "rolltui/Unicode.hpp"

namespace rolltui {

// It SHORTENS rather than eating the line (Phase 12 m5). The marker writes over CONTENT
// cells, and at 20 cells wide the full form took most of the row ("│   Ent▼ 187 more │").
// Shortening is only safe because the scrollbar now carries the proportion: the two are
// KEPT TOGETHER on purpose — the bar is the positional signal and the marker is the
// NON-GRAPHICAL one, which is the first thing a mono theme, a low colour depth or a
// borderless window still has. Returns "" when there is nothing below or no room at all.
inline std::string scroll_marker_text(std::size_t below, int max_width, bool ambiguous_wide) {
  if (below == 0 || max_width <= 0) return {};
  const std::string full = "\xE2\x96\xBC " + std::to_string(below) + " more ";
  // The full form only when it costs at most HALF the width; then the count alone; then
  // the arrow, which still says "there is more" and costs one cell.
  if (unicode::display_width(full, ambiguous_wide) * 2 <= max_width) return full;
  const std::string small = "\xE2\x96\xBC" + std::to_string(below);
  if (unicode::display_width(small, ambiguous_wide) <= max_width) return small;
  return max_width >= 1 ? "\xE2\x96\xBC" : "";
}

}  // namespace rolltui
