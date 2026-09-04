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

#include "rolltui/c/rolltui_marker.h"

namespace rolltui {

// It SHORTENS rather than eating the line (Phase 12 m5). The marker writes over CONTENT
// cells, and at 20 cells wide the full form took most of the row ("│   Ent▼ 187 more │").
// Shortening is only safe because the scrollbar now carries the proportion: the two are
// KEPT TOGETHER on purpose — the bar is the positional signal and the marker is the
// NON-GRAPHICAL one, which is the first thing a mono theme, a low colour depth or a
// borderless window still has. Returns "" when there is nothing below or no room at all.
// PHASE 15 m4 — THE RULE MOVED TO `rolltui/c/rolltui_marker.h` AND THIS IS THE SPELLING.
// The header note above said the rule has three callers and must have one definition; m4
// made one of the three C, so a C definition was the only way to keep that true. Nothing
// about the rule changed — see that file.
inline std::string scroll_marker_text(std::size_t below, int max_width, bool ambiguous_wide) {
  char buf[ROLLTUI_MARKER_MAX];
  return std::string(buf, rolltui_scroll_marker_text(below, max_width, ambiguous_wide ? 1 : 0, buf, sizeof buf));
}
// The same, into a caller's buffer, for a draw path that must not build a string per frame.
inline std::size_t scroll_marker_text_into(std::size_t below, int max_width, bool ambiguous_wide, char* out,
                                           std::size_t cap) {
  return rolltui_scroll_marker_text(below, max_width, ambiguous_wide ? 1 : 0, out, cap);
}

}  // namespace rolltui
