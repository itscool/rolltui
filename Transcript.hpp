#pragma once
//
// rolltui/Transcript.hpp — the transcript widget, first cut (milestone 7; milestone 9
// grows it: entry-anchored scroll offset, the layout cache, the new-lines marker,
// folding, selection). Pure: layout() turns a Document into styled lines at a width;
// draw() paints a window of those lines into a Frame with a theme; the scroll state
// is a small value the host owns and updates from events.
//
#include <cstddef>
#include <string>
#include <vector>

#include "rolltui/Document.hpp"
#include "rolltui/Markdown.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

struct TranscriptLine {
  markdown::StyledLine line;
  std::size_t entry;  // index into Document::entries
};

struct TranscriptLayoutOptions {
  int width = 80;
  bool ambiguous_wide = false;
  int tab_width = 8;
  int gap = 1;  // blank lines between entries
};

// Every entry rendered at `width`, in order, with `gap` blank lines between entries.
std::vector<TranscriptLine> layout_transcript(const Document& doc, const TranscriptLayoutOptions& opt);

struct ScrollState {
  std::size_t top = 0;  // first visible line
  bool follow = true;   // pinned to the bottom: new lines scroll the view
};

// Clamps and applies a relative scroll; `delta` < 0 scrolls up. A view that reaches
// the bottom re-engages follow; any upward scroll disengages it.
void scroll_by(ScrollState& s, long delta, std::size_t total_lines, int viewport_height);
void scroll_to_bottom(ScrollState& s, std::size_t total_lines, int viewport_height);
void scroll_to_top(ScrollState& s);
// Called after a relayout: keeps a following view at the bottom and clamps the rest.
void reconcile_scroll(ScrollState& s, std::size_t total_lines, int viewport_height);

// Paints lines [s.top, s.top + area.h) into `area`, filling with the theme's
// `background` style. Returns the number of lines below the viewport (for a "▼ N"
// marker the caller may draw).
std::size_t draw_transcript(Frame& frame, Rect area, const std::vector<TranscriptLine>& lines,
                            const ScrollState& s, const Theme& theme, bool ambiguous_wide = false);

}  // namespace rolltui
