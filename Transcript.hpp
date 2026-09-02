#pragma once
//
// rolltui/Transcript.hpp — the transcript widget (plan/phase-9.md, milestone 9): a
// Document rendered into a window, scrolled, folded, selected and copied.
//
// The host owns the Document and calls, once per frame and in this order,
//   layout(doc, area, options)   lay out (cache-aware), reconcile the scroll position
//   draw(frame, theme)           paint the visible lines, the selection, the marker
// and hands the widget the events the WindowStack routed to its window (handle), plus
// a tick() while a drag is auto-scrolling. Everything drawn is a function of (doc,
// area, options, scroll, selection, fold toggles); the layout cache is a memo of that
// function, never an input to it.
//
// RULES, each asserted in rolltui/tests/transcript_test.cpp:
//
//   Layout cache — every entry's lines are cached under (id, version, width,
//   ambiguous_wide, tab_width, folded). A frame re-lays only entries whose key changed:
//   the streaming entry (its version bumps per chunk) and nothing else. A width or
//   option change re-lays everything once. The cache is invalidated by its key, never
//   by an event; stale ids are swept when the cache outgrows the document.
//
//   Scroll position is an ANCHOR (entry index, line within that entry's block), not a
//   line number, so a re-wrap at a new width shows the same entry at the top of the
//   viewport. An entry's block is its gap lines (the blank lines before it) followed
//   by its content lines. `follow` is on when the viewport is at the bottom: new lines
//   then scroll the view; while it is off nothing moves the anchor but the user (a
//   streaming answer or a growing document never does), and the "▼ N more" marker in
//   the bottom-right corner counts the lines below the viewport. Scrolling to the
//   bottom (End, PageDown, wheel, drag) re-engages follow; any upward scroll clears it.
//
//   Folding — a foldable entry (Document.hpp) draws a summary line, "▸ summary (N
//   lines)" folded or "▾ summary" plus its body unfolded. A click on the summary line
//   toggles it; Ctrl-O toggles the first visible fold from the top of the viewport.
//   The toggle is kept by entry id, over the entry's own initial state. The summary
//   line is one line: it is clipped at the width, never wrapped (a summary is a
//   label, and a label that wraps stops being one).
//
//   Selection is a model in LOGICAL coordinates — (entry, byte offset into the entry's
//   logical text; Markdown.hpp) — so it survives a re-wrap, a resize and a scroll, and
//   what is copied is the entries' text with no wrap artefacts: no line breaks the
//   width introduced, no continuation indentation, no borders. A press places the
//   anchor, a drag moves the head, and the grapheme under the pointer is included
//   whichever way the drag runs (the tmux/xterm convention); a double-click selects the
//   UAX #29 word, a triple-click the logical line; Shift+press extends. A drag past the
//   top or bottom edge auto-scrolls on each tick() by as many lines as the pointer is
//   past the edge, and may span any number of entries. Release fires `on_copy` with the
//   selected text (copy-on-select); Alt-C copies again. The widget never touches a
//   clipboard itself — the host binds `on_copy` (plan: roll binds pbcopy). Chrome cells
//   (borders, markers on continuation lines) highlight with the line when the line's
//   text is wholly inside the selection and never carry text into the copy. A folded
//   entry contributes its summary.
//
//   Hyperlinks — a span with an href puts its cells under that URL (Screen.hpp emits
//   OSC 8); the URL comes from the parsed document, never from the text.
//
//   Inset — `TranscriptOptions::inset` columns are kept clear on each side of the
//   area; the host sets 1 for a bordered window (a border is the only spacing the
//   layout has; the widget owns the breathing room inside it).
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rolltui/Document.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Markdown.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

struct TranscriptOptions {
  bool ambiguous_wide = false;
  int tab_width = 8;
  int gap = 1;                 // blank lines between entries
  int inset = 0;               // columns kept clear on each side of the area
  int wheel_lines = 3;         // lines per wheel tick
  std::uint64_t multi_click_ms = 400;  // two presses within this (and one cell) are a double-click
  bool operator==(const TranscriptOptions&) const = default;
};

// One entry's cached layout.
struct EntryLayout {
  std::vector<markdown::StyledLine> lines;  // as drawn: the summary line first when foldable
  std::string text;                          // logical text; a folded entry's is its summary
  bool folded = false;
  std::size_t hidden_lines = 0;              // body lines a fold hides
};

struct ScrollAnchor {
  std::size_t entry = 0;  // index into Document::entries
  std::size_t line = 0;   // line within that entry's block (gap lines first)
  bool follow = true;
};

// A position in the logical text: the grapheme starting at `offset` of `entry`'s text,
// `length` bytes long (0 at the end of the text or for a boundary position).
struct TextPos {
  std::size_t entry = 0;
  std::size_t offset = 0;
  std::size_t length = 0;
  bool operator==(const TextPos&) const = default;
};
bool operator<(const TextPos& a, const TextPos& b);

struct Selection {
  TextPos anchor, head;
  bool active = false;
  bool empty() const { return !active; }
  TextPos first() const { return head < anchor ? head : anchor; }
  TextPos last() const { return head < anchor ? anchor : head; }
  // The selected byte range within `entry`'s text of length `len`: [begin, end).
  // Includes the grapheme at `last()` (its length), so a drag covers the cell under
  // the pointer in both directions.
  bool range_in(std::size_t entry, std::size_t len, std::size_t& begin, std::size_t& end) const;
};

struct TranscriptStats {
  long layout_us = 0;              // the last layout() call
  std::size_t entries_relaid = 0;  // cache misses in the last layout()
  std::size_t total_lines = 0;
  std::size_t cache_size = 0;
};

class Transcript {
 public:
  std::function<void(const std::string&)> on_copy;  // the host's clipboard

  // ---- per frame ----
  void layout(const Document& doc, Rect area, const TranscriptOptions& opt);
  void draw(Frame& frame, const Theme& theme) const;

  // ---- events (already routed to this window by the host) ----
  // `now_ms` is any monotonic millisecond clock, used only to pair clicks. Returns
  // true when the event was consumed.
  bool handle(const Event& e, const Document& doc, std::uint64_t now_ms);
  // True while a drag holds the pointer outside the area: call tick() at a steady
  // rate (≈50 ms) and re-layout/draw after each.
  bool wants_tick() const { return drag_.active && drag_.outside; }
  void tick();

  // ---- scrolling ----
  void scroll_by(long lines);      // < 0 up; clamps; follow = at bottom
  void scroll_page(int direction); // ±1, by viewport height − 1
  void scroll_to_top();
  void scroll_to_bottom();
  const ScrollAnchor& scroll() const { return scroll_; }
  std::size_t total_lines() const { return total_; }
  std::size_t top_line() const;    // the anchor as a global line index
  std::size_t lines_below() const; // hidden below the viewport (the marker's N)
  int viewport_height() const { return area_.h; }

  // ---- folding ----
  bool is_folded(const DocEntry& e) const;
  void set_folded(std::string_view id, bool folded) { fold_override_[std::string(id)] = folded; }
  void toggle_fold(const DocEntry& e) { set_folded(e.id, !is_folded(e)); }
  // Toggles the first visible summary line from the top of the viewport; false if none.
  bool toggle_fold_nearest_top(const Document& doc);

  // ---- selection ----
  const Selection& selection() const { return sel_; }
  void clear_selection() { sel_ = {}; }
  void select(TextPos anchor, TextPos head) { sel_ = {anchor, head, true}; }
  // The logical position under screen cell (x, y): the grapheme there, the nearest
  // text on that row when the cell is chrome or past the line's end, the end of the
  // previous entry on a gap row, the end of the last entry below the text. nullopt
  // only for an empty document or a row above the area.
  std::optional<TextPos> hit(int x, int y) const;
  std::string selected_text() const;
  bool copy_selection();  // fires on_copy; false when nothing is selected

  // ---- introspection ----
  const TranscriptStats& stats() const { return stats_; }
  const EntryLayout* layout_of(std::size_t entry) const;
  Rect area() const { return area_; }
  Rect text_area() const { return text_area_; }

 private:
  struct CacheKey {
    std::uint64_t version = 0;
    int width = 0;
    bool ambiguous = false;
    int tab = 8;
    bool folded = false;
    bool operator==(const CacheKey&) const = default;
  };
  struct Cached {
    CacheKey key;
    EntryLayout layout;
    bool seen = false;
  };
  struct RowRef {
    std::size_t entry = 0;
    std::size_t line = 0;  // index into EntryLayout::lines
    bool gap = false;
    bool beyond = false;   // past the last line
  };

  static EntryLayout lay_out(const DocEntry& e, int width, const TranscriptOptions& opt, bool folded);
  std::size_t block_len(std::size_t entry) const;
  std::size_t max_top() const;
  void set_top(std::size_t top);
  RowRef row_at(std::size_t global) const;
  std::optional<TextPos> hit_row(const RowRef& r, int x) const;
  void begin_drag(int x, int y, bool shift, std::uint64_t now_ms, const Document& doc);
  void drag_to(int x, int y);
  void end_drag();
  static void unit_around(const std::string& text, std::size_t off, bool word, std::size_t& b, std::size_t& en);

  std::unordered_map<std::string, Cached> cache_;
  std::vector<const EntryLayout*> layouts_;  // per entry, this frame
  std::vector<std::size_t> starts_;          // global line index of each entry's block
  std::size_t total_ = 0;
  Rect area_, text_area_;
  TranscriptOptions opt_;
  ScrollAnchor scroll_;
  Selection sel_;
  std::unordered_map<std::string, bool> fold_override_;
  struct Drag {
    bool active = false, outside = false;
    int x = 0, y = 0;
    std::size_t origin_entry = 0, origin_begin = 0, origin_end = 0;  // the double/triple-clicked unit
  } drag_;
  struct Click {
    std::uint64_t at_ms = 0;
    int x = -1, y = -1, count = 0;
  } click_;
  TranscriptStats stats_;
};

}  // namespace rolltui
