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
//   Keys are DATA (milestone 17): handle() looks a key up in the Bindings' transcript
//   scope — page_up/page_down, top/bottom, line_up/line_down, fold, copy,
//   clear_selection; the shipped default is PgUp/PgDn, Home/End (and Ctrl+Home/End),
//   Up/Down, Ctrl-O, Alt-C, Escape.
//
//   LONG CODE BLOCKS fold too, one rung down and by a different mechanism (Phase 12
//   m5b, the rules in Markdown.hpp): the renderer folds a block over
//   TranscriptOptions::code_fold_over_lines to one "▸ diff · 42 lines · 1.2 kB" row and
//   caps an open one over code_cap_lines with the "▼ N more" marker. This widget keeps
//   those toggles the way it keeps entry folds — by id, over the renderer's threshold —
//   with the block addressed by its index in the entry. The header row toggles the fold
//   and the marker row lifts the cap, each over its WHOLE row: those rows carry nothing
//   else, so there is no arithmetic to get wrong at a degenerate width, and neither is
//   inert chrome that merely looks like a control.
//     Because a folded code block hides LINES and never TEXT (Markdown.hpp), the find
//     rule below needs NO second cache for it: the drawn logical text already contains
//     every byte, so the match count cannot move when a block opens or closes. What
//     that costs is one thing, and it is paid in reveal_current: a match inside a
//     folded or capped block has to OPEN the block before it can be scrolled to.
//
//   Folding — a foldable entry (Document.hpp) draws a summary line, "▸ summary (N
//   lines)" folded or "▾ summary" plus its body unfolded. A click on the summary line
//   toggles it; transcript.fold (Ctrl-O) toggles the first visible fold from the top.
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
//   FIND (Phase 12 milestone 4) — a query, a match list, and one current match. Where
//   the query is TYPED is not here: it is an `input:` window a layout places, because
//   where a find bar sits is a layout file's business and a mode of this widget would be
//   a second input implementation. The host pipes that input's text in through
//   set_query() and renders match_count()/current_match_number() wherever it likes; this
//   widget owns finding, revealing and highlighting. The rules:
//
//     Matches are in LOGICAL text — the same (entry, byte offset) space as the
//     selection — so a match that wraps across two rows highlights on both, for free:
//     every cell already carries the source offset of its grapheme, and a highlight is
//     just a range test on it. Nothing in find knows what a row is.
//
//     A FOLDED entry is searched UNFOLDED. Its drawn logical text is only its summary,
//     so searching what is on screen would silently miss every match inside a folded
//     block — the counted total would depend on which blocks happened to be open. So
//     the search runs over each entry's text as if unfolded (cached per id + version +
//     width alongside the layout cache), and revealing a match in a folded entry
//     UNFOLDS it. That is why the count is stable while you fold and unfold.
//
//     ASCII-case-insensitive, byte-exact otherwise, non-overlapping, left to right.
//     Stated rather than inferred: there is no Unicode case folding in this library, and
//     a search that folded only some scripts would be a rule nobody could predict. A
//     match may begin inside a grapheme cluster (a combining mark); the cell test then
//     highlights that whole grapheme, which is the only thing a cell grid can do.
//
//     set_query() NEVER scrolls by itself and an empty query clears without moving the
//     view. A non-empty query makes current the first match at or after the top line, and
//     asks for it to be revealed; find_next()/find_prev() step and wrap. Revealing
//     happens in the next layout() — it may have to unfold, which changes the line
//     count — so a caller that wants to see the result calls layout() first, exactly as
//     it does for every other state change here.
//
//     A selection WINS over both find roles where they overlap: it is the user's most
//     recent direct act, and "what did I just select" is the question it answers.
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

#include "rolltui/Bindings.hpp"
#include "rolltui/Document.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Markdown.hpp"
#include "rolltui/Marker.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

struct TranscriptOptions {
  bool ambiguous_wide = false;
  int tab_width = 8;
  int gap = 1;                 // blank lines between entries
  int inset = 0;               // columns kept clear on each side of the area
  int wheel_lines = 3;         // lines per wheel tick
  int code_fold_over_lines = 0;  // m5b; 0 disables, as in markdown::CodeFoldOptions
  int code_cap_lines = 0;
  std::uint64_t multi_click_ms = 400;  // two presses within this (and one cell) are a double-click
  bool operator==(const TranscriptOptions&) const = default;
};

// One entry's cached layout.
struct EntryLayout {
  std::vector<markdown::StyledLine> lines;  // as drawn: the summary line first when foldable
  std::string text;                          // logical text; a folded entry's is its summary
  bool folded = false;
  std::size_t hidden_lines = 0;              // body lines a fold hides
  // m5b: the entry's code blocks, with header_line/marker_line already shifted onto
  // THIS layout's line numbering (the entry's own summary row moves everything by one).
  std::vector<markdown::CodeBlockInfo> code_blocks;
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

// One find hit, in the same logical space as TextPos: `length` bytes of `entry`'s
// unfolded text starting at `offset`.
struct FindMatch {
  std::size_t entry = 0;
  std::size_t offset = 0;
  std::size_t length = 0;
  bool operator==(const FindMatch&) const = default;
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
  bool handle(const Event& e, const Document& doc, std::uint64_t now_ms, const Bindings& bindings);
  bool handle(const Event& e, const Document& doc, std::uint64_t now_ms) { return handle(e, doc, now_ms, default_bindings()); }
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
  // Toggles the first visible summary line from the top of the viewport — an entry's or
  // a code block's, whichever comes first; false if there is none.
  bool toggle_fold_nearest_top(const Document& doc);

  // ---- code-block folding (m5b) ----
  // `block` is the block's index within the entry (Markdown.hpp: a document fact, not a
  // width-dependent one). Both are host-callable, and both are what a click does.
  void set_code_folded(std::string_view id, std::size_t block, bool folded);
  void set_code_uncapped(std::string_view id, std::size_t block, bool uncapped);
  // The highlighter the entries' markdown renders through; unset (the default) means the
  // renderer is never asked, which is m2's whole opt-in. Changing it re-lays everything.
  void set_highlight(markdown::Highlighter h);

  // ---- find (see FIND above) ----
  // The query; "" clears. Never scrolls: the reveal it asks for happens in layout().
  // Returns whether it CHANGED, so a host can skip the re-layout when it did not.
  bool set_query(std::string_view q);
  const std::string& query() const { return query_; }
  const std::vector<FindMatch>& matches() const { return matches_; }
  std::size_t match_count() const { return matches_.size(); }
  // The current match's 1-BASED position, for "3/17"; 0 when there is none. One-based
  // because it is a display number, and the only caller is a host printing it.
  std::size_t current_match_number() const { return current_ ? *current_ + 1 : 0; }
  const FindMatch* current_match() const { return current_ && *current_ < matches_.size() ? &matches_[*current_] : nullptr; }
  bool find_next();  // wraps; false when there is nothing to find
  bool find_prev();

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
    // m5b. The code epoch is per ENTRY (a toggle re-lays only its own entry); the
    // highlight epoch is global (a new highlighter re-lays everything). Both are in the
    // key rather than in an event, so the frame stays a pure function of state.
    std::uint64_t code_epoch = 0;
    std::uint64_t highlight_epoch = 0;
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

  EntryLayout lay_out(const DocEntry& e, int width, const TranscriptOptions& opt, bool folded) const;
  markdown::CodeFoldOptions code_fold_for(const std::string& id, const TranscriptOptions& opt) const;
  // The code block of `entry` holding `offset`, when that block is hiding it; nullptr
  // when the offset is on a drawn line. Reveal's one job beyond scrolling.
  const markdown::CodeBlockInfo* hiding_block(std::size_t entry, std::size_t offset) const;
  // The per-frame build of layouts_/starts_/total_, extracted so a reveal that has to
  // unfold can re-run it in the same layout() call rather than leaving one frame drawn
  // against line numbers that no longer exist.
  void build(const Document& doc, int width);
  // The entry's logical text AS IF UNFOLDED — what find searches. Equal to the drawn
  // layout's text for everything except a folded entry, whose drawn text is its summary.
  const std::string& searchable_text(const DocEntry& e, std::size_t entry, int width);
  void recompute_matches(const Document& doc, int width);
  // The line within the entry's layout holding `offset`, or the last line.
  std::size_t line_of_offset(std::size_t entry, std::size_t offset) const;
  void reveal_current(const Document& doc, int width);
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
  struct CodeFolds {
    std::uint64_t epoch = 0;
    std::vector<markdown::CodeFoldState> states;
  };
  std::unordered_map<std::string, CodeFolds> code_folds_;
  markdown::Highlighter highlight_;
  std::uint64_t highlight_epoch_ = 0;
  // Find state. `find_text_` is the unfolded-text cache, keyed like the layout cache
  // minus `folded` — the whole point is that it does not vary with folding.
  std::string query_;
  std::vector<FindMatch> matches_;
  std::optional<std::size_t> current_;
  bool find_dirty_ = false;   // the query changed: recompute in the next layout()
  bool reveal_ = false;       // …and scroll to (and unfold) the current match
  struct FindText {
    CacheKey key;
    std::string text;
  };
  std::unordered_map<std::string, FindText> find_text_;
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
