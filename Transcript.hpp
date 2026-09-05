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
#include <memory>
#include <optional>
#include <string>
#include <span>
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
#include "rolltui/c/rolltui_transcript.h"

namespace rolltui {

// PHASE 15 m5e: the whole widget — the layout cache, find, selection, folding, the drag and
// the draw — is behind `rolltui/c/rolltui_transcript.h`. `TranscriptOptions`,
// `EntryLayout`, `ScrollAnchor`, `TextPos`, `Selection`, `FindMatch` and `TranscriptStats` ARE
// the C structs (one definition). Two things a caller can see, both forced by the handle:
// `query()` and `selected_text()` hand back a `std::string_view` / a fresh `std::string` where
// they used to hand back a `const std::string&`, and `matches()` is `match_count()` +
// `match_at(i)` — nothing on the C side can return a `std::vector` without building one.
using TranscriptOptions = RolltuiTranscriptOptions;
using EntryLayout = RolltuiEntryLayout;
using ScrollAnchor = RolltuiScrollAnchor;
using TextPos = RolltuiTextPos;
using Selection = RolltuiSelection;
using FindMatch = RolltuiFindMatch;
using TranscriptStats = RolltuiTranscriptStats;

inline bool operator<(const TextPos& a, const TextPos& b) { return rolltui_text_pos_less(&a, &b) != 0; }

class Transcript {
 public:
  // OWNED, through a `unique_ptr` with a deleter that calls the C free.
  struct Handle {
    void operator()(RolltuiTranscript* p) const { rolltui_transcript_free(p); }
  };
  Transcript();
  ~Transcript();
  Transcript(const Transcript&) = delete;
  Transcript& operator=(const Transcript&) = delete;

  std::function<void(const std::string&)> on_copy;  // the host's clipboard

  // ---- per frame ----
  void layout(const Document& doc, Rect area, const TranscriptOptions& opt);
  void draw(Frame& frame, const Theme& theme) const;

  // ---- events (already routed to this window by the host) ----
  // `now_ms` is any monotonic millisecond clock, used only to pair clicks. Returns
  // true when the event was consumed.
  bool handle(const Event& e, const Document& doc, std::uint64_t now_ms, const Bindings& bindings);
  bool handle(const Event& e, const Document& doc, std::uint64_t now_ms) {
    return handle(e, doc, now_ms, default_bindings());
  }
  // True while a drag holds the pointer outside the area: call tick() at a steady
  // rate (≈50 ms) and re-layout/draw after each.
  bool wants_tick() const { return rolltui_transcript_wants_tick(t_.get()) != 0; }
  void tick() { rolltui_transcript_tick(t_.get()); }

  // ---- scrolling ----
  void scroll_by(long lines) { rolltui_transcript_scroll_by(t_.get(), lines); }
  void scroll_page(int direction) { rolltui_transcript_scroll_page(t_.get(), direction); }
  void scroll_to_top() { rolltui_transcript_scroll_to_top(t_.get()); }
  void scroll_to_bottom() { rolltui_transcript_scroll_to_bottom(t_.get()); }
  ScrollAnchor scroll() const;
  std::size_t total_lines() const { return rolltui_transcript_total_lines(t_.get()); }
  std::size_t top_line() const { return rolltui_transcript_top_line(t_.get()); }
  std::size_t lines_below() const { return rolltui_transcript_lines_below(t_.get()); }
  int viewport_height() const { return rolltui_transcript_viewport_height(t_.get()); }

  // ---- folding ----
  bool is_folded(const DocEntry& e) const { return rolltui_transcript_is_folded(t_.get(), &e) != 0; }
  void set_folded(std::string_view id, bool folded) {
    rolltui_transcript_set_folded(t_.get(), id.data(), id.size(), folded);
  }
  void toggle_fold(const DocEntry& e) { set_folded(e.id.view(), !is_folded(e)); }
  // Toggles the first visible summary line from the top of the viewport — an entry's or
  // a code block's, whichever comes first; false if there is none.
  bool toggle_fold_nearest_top(const Document& doc);

  // ---- code-block folding (m5b) ----
  void set_code_folded(std::string_view id, std::size_t block, bool folded) {
    rolltui_transcript_set_code_folded(t_.get(), id.data(), id.size(), block, folded);
  }
  void set_code_uncapped(std::string_view id, std::size_t block, bool uncapped) {
    rolltui_transcript_set_code_uncapped(t_.get(), id.data(), id.size(), block, uncapped);
  }
  // The highlighter the entries' markdown renders through; unset (the default) means the
  // renderer is never asked, which is m2's whole opt-in. Changing it re-lays everything.
  void set_highlight(markdown::Highlighter h);

  // ---- find (see FIND above) ----
  bool set_query(std::string_view q) {
    return rolltui_transcript_set_query(t_.get(), q.data(), q.size()) != 0;
  }
  std::string_view query() const;
  std::size_t match_count() const { return rolltui_transcript_match_count(t_.get()); }
  // ONE MATCH AT A TIME, never the whole list: nothing on the C side can hand back a
  // `std::vector<FindMatch>` without building one per call (Phase 15 m5e).
  FindMatch match_at(std::size_t i) const;
  // The current match's 1-BASED position, for "3/17"; 0 when there is none.
  std::size_t current_match_number() const { return rolltui_transcript_current_match_number(t_.get()); }
  std::optional<FindMatch> current_match() const;
  bool find_next() { return rolltui_transcript_find_next(t_.get()) != 0; }
  bool find_prev() { return rolltui_transcript_find_prev(t_.get()) != 0; }

  // ---- selection ----
  Selection selection() const;
  void clear_selection() { rolltui_transcript_clear_selection(t_.get()); }
  void select(TextPos anchor, TextPos head) { rolltui_transcript_select(t_.get(), anchor, head); }
  // The logical position under screen cell (x, y): the grapheme there, the nearest
  // text on that row when the cell is chrome or past the line's end, the end of the
  // previous entry on a gap row, the end of the last entry below the text. nullopt
  // only for an empty document or a row above the area.
  std::optional<TextPos> hit(int x, int y) const;
  std::string selected_text() const;
  bool copy_selection();  // fires on_copy; false when nothing is selected

  // ---- introspection ----
  TranscriptStats stats() const;
  const EntryLayout* layout_of(std::size_t entry) const {
    return rolltui_transcript_layout_of(t_.get(), entry);
  }
  Rect area() const;
  Rect text_area() const;


  // THE HANDLE, so a C plugin and this object can be the SAME state rather than two.
  // `Windows::transcript()` hands a host a live reference to the widget the window draws; a pure-C
  // widget plugin needs the same object, not a copy. `Input::handle()` already existed and is
  // exactly why the `input` kind could port while this one could not — added 2026-09-05 to
  // close that asymmetry (`plan/phase-17.md` m1c). Borrowed: valid while this object is.
  RolltuiTranscript* handle() { return t_.get(); }

 private:
  std::unique_ptr<RolltuiTranscript, Handle> t_{rolltui_transcript_new()};
  // The host's highlighter stays HERE, where its callable is, and the boundary is handed the
  // trampoline — the same trade the menu's validators make (Phase 15 m5c).
  markdown::Highlighter highlight_;
  std::vector<std::string_view> hl_lines_;
  std::vector<markdown::HighlightSpan> hl_spans_;
  std::unique_ptr<struct TranscriptHighlightCtx> hl_ctx_;  // OWNED: the trampoline needs a stable address
};

}  // namespace rolltui
