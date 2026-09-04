#ifndef ROLLTUI_C_TRANSCRIPT_H
#define ROLLTUI_C_TRANSCRIPT_H
/*
 * rolltui/c/rolltui_transcript.h — THE TRANSCRIPT WIDGET (Phase 15 m5e).
 *
 * A document laid out into styled lines, scrolled by an ANCHOR rather than a line number,
 * selected in logical coordinates, searched, folded, and drawn. Every rule — why the anchor
 * is (entry, line) and not an offset, why a match's count is stable across a fold, what a
 * click on a summary row does, that the selection wins where it overlaps a find highlight —
 * is stated in `rolltui/Transcript.hpp` and asserted in `rolltui/tests/transcript_test.cpp`;
 * none of it is repeated here.
 *
 * ---- WHAT THIS MODULE OWNS, WHICH IS WHY IT IS THE LAST ONE -----------------------------
 *
 * **FIVE string-keyed caches.** The parse tree per entry, the laid-out lines per entry, the
 * unfolded text find searches, the fold overrides, and the code-fold states. The C++ spelled
 * all five `std::unordered_map<std::string, T>` without anyone choosing a lookup strategy
 * once; they are `RolltuiMap` here (`rolltui_map.h`), which is a linear scan with the sizes
 * that justify it written down. Four of them are SWEPT per frame, which is why that header
 * has a mark/unmark pair rather than each caller doing it by hand.
 *
 * ---- THE BOUNDARY'S RULES, all inherited and none new -----------------------------------
 *
 *   1. **THE CALLER OWNS EVERY BUFFER.** The transcript is a handle; the selected text and
 *      the searchable text are filled into a caller's `RolltuiStr`.
 *   2. **TEXT OUT IS A BORROW** with a stated window — an entry layout's lines and text are
 *      valid until that entry is laid out again.
 *   3. **NO `std::function` CROSSES.** The clipboard and the syntax highlighter are function
 *      pointers plus a `void*`; the highlighter's is forwarded straight to
 *      `rolltui_md_render`, so there is one seam and not two.
 *   4. **THIS FILE NAMES NO ROLE AND NO ACTION.** Five roles and eleven action names are
 *      handed in as structs.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_document.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_input.h" /* RolltuiCopyFn: one clipboard seam, not two */
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_md_lines.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
#include <cstddef>
#include <span>
#include <string_view>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- options ------------------------------------------------------------------------------ */
/* `rolltui::TranscriptOptions` IS this struct. */
typedef struct RolltuiTranscriptOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(8);
  int gap ROLLTUI_DEFAULT(1);   /* blank lines between entries */
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
  int wheel_lines ROLLTUI_DEFAULT(3);
  int code_fold_over_lines ROLLTUI_DEFAULT(0); /* 0 disables, as in markdown's fold options */
  int code_cap_lines ROLLTUI_DEFAULT(0);
  unsigned long long multi_click_ms ROLLTUI_DEFAULT(400);
#ifdef __cplusplus
  bool operator==(const RolltuiTranscriptOptions&) const = default;
#endif
} RolltuiTranscriptOptions;

/* ---- one entry's cached layout ---------------------------------------------------------------- */
/* `rolltui::EntryLayout` IS this struct. It OWNS one span store: the markdown render puts the
 * entry's BODY lines at the front, and this layout's own drawn lines — the body behind the
 * entry's prefix, with the fold summary above them — are appended after and REFERENCE the
 * body's spans by index (Phase 15 m4). `body` is where the drawn ones begin. */
typedef struct RolltuiEntryLayout {
  RolltuiMdLines* store ROLLTUI_DEFAULT(nullptr); /* OWNED */
  size_t body ROLLTUI_DEFAULT(0);
  unsigned char folded ROLLTUI_DEFAULT(0);
  size_t hidden_lines ROLLTUI_DEFAULT(0); /* body lines a fold hides */

#ifdef __cplusplus
  // As drawn: the summary line first when foldable.
  std::span<const RolltuiMdLine> lines() const {
    const RolltuiMdLine* p = store ? rolltui_md_lines_all(store) : nullptr;
    const std::size_t n = store ? rolltui_md_lines_count(store) : 0;
    return {p + (p ? body : 0), n > body ? n - body : 0};
  }
  // Logical text; a folded entry's is its summary.
  std::string_view text() const {
    return store ? std::string_view(rolltui_md_lines_text(store), rolltui_md_lines_text_size(store))
                 : std::string_view();
  }
  // The entry's code blocks, with header_line/marker_line already shifted onto THIS layout's
  // line numbering (the entry's own summary row moves everything by one).
  std::span<const RolltuiMdCodeBlock> code_blocks() const {
    const RolltuiMdCodeBlock* p = store ? rolltui_md_lines_code_blocks(store) : nullptr;
    return {p, p ? rolltui_md_lines_code_block_count(store) : 0};
  }
#endif
} RolltuiEntryLayout;

/* The store, made on FIRST USE — a default-constructed layout (a cache entry that has not
 * been laid into yet) holds nothing and costs nothing. `static inline` so both
 * implementations share ONE definition of "when does a layout acquire its store". */
static ROLLTUI_INLINE RolltuiMdLines* rolltui_entry_layout_store(RolltuiEntryLayout* L) {
  if (!L->store) L->store = rolltui_md_lines_new();
  return L->store;
}
static ROLLTUI_INLINE void rolltui_entry_layout_release(RolltuiEntryLayout* L) {
  rolltui_md_lines_free(L->store);
  L->store = ROLLTUI_NULL;
  L->body = 0;
  L->folded = 0;
  L->hidden_lines = 0;
}

/* ---- scroll, positions, selection, matches ------------------------------------------------------ */

typedef struct RolltuiScrollAnchor {
  size_t entry ROLLTUI_DEFAULT(0); /* index into the document */
  size_t line ROLLTUI_DEFAULT(0);  /* line within that entry's block (gap lines first) */
  unsigned char follow ROLLTUI_DEFAULT(1);
} RolltuiScrollAnchor;

/* A position in the logical text: the grapheme starting at `offset` of `entry`'s text,
 * `length` bytes long (0 at the end of the text or for a boundary position). */
typedef struct RolltuiTextPos {
  size_t entry ROLLTUI_DEFAULT(0);
  size_t offset ROLLTUI_DEFAULT(0);
  size_t length ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool operator==(const RolltuiTextPos&) const = default;
#endif
} RolltuiTextPos;

/* Entry, then offset, then length — so of two positions at the same offset the one that
 * covers a grapheme is `last()`, and the range includes it. */
int rolltui_text_pos_less(const RolltuiTextPos* a, const RolltuiTextPos* b);

typedef struct RolltuiSelection {
  RolltuiTextPos anchor, head;
  unsigned char active ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool empty() const { return !active; }
  RolltuiTextPos first() const { return rolltui_text_pos_less(&head, &anchor) ? head : anchor; }
  RolltuiTextPos last() const { return rolltui_text_pos_less(&head, &anchor) ? anchor : head; }
  // The selected byte range within `entry`'s text of length `len`: [begin, end).
  bool range_in(std::size_t entry, std::size_t len, std::size_t& begin, std::size_t& end) const;
#endif
} RolltuiSelection;

int rolltui_selection_range_in(const RolltuiSelection* s, size_t entry, size_t len, size_t* begin, size_t* end);

/* One find hit, in the same logical space as a position. */
typedef struct RolltuiFindMatch {
  size_t entry ROLLTUI_DEFAULT(0);
  size_t offset ROLLTUI_DEFAULT(0);
  size_t length ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool operator==(const RolltuiFindMatch&) const = default;
#endif
} RolltuiFindMatch;

typedef struct RolltuiTranscriptStats {
  long layout_us ROLLTUI_DEFAULT(0);      /* the last layout() call */
  size_t entries_relaid ROLLTUI_DEFAULT(0);
  size_t total_lines ROLLTUI_DEFAULT(0);
  size_t cache_size ROLLTUI_DEFAULT(0);
} RolltuiTranscriptStats;

/* ---- the handle ---------------------------------------------------------------------------------- */

typedef struct RolltuiTranscript RolltuiTranscript;
RolltuiTranscript* rolltui_transcript_new(void);
void rolltui_transcript_free(RolltuiTranscript* t);

/* THE HOST'S CLIPBOARD, as a function pointer and a context. NULL turns it off. */
void rolltui_transcript_set_copy(RolltuiTranscript* t, RolltuiCopyFn fn, void* ctx);
/* THE SYNTAX HIGHLIGHTER, forwarded straight to `rolltui_md_render` — one seam, not two.
 * Setting it bumps an epoch, so a LATER highlighter re-lays everything rather than being
 * silently ignored (set-once is a host convention, not a guarantee). */
void rolltui_transcript_set_highlight(RolltuiTranscript* t, RolltuiMdHighlightFn fn, void* ctx);

/* ---- per frame ------------------------------------------------------------------------------------ */
void rolltui_transcript_layout(RolltuiTranscript* t, const RolltuiDocument* doc, RolltuiRect area,
                               const RolltuiTranscriptOptions* opt);

/* THE SIX ROLES THIS WIDGET NEEDS, handed over ONCE rather than per draw — because one of
 * them (`text_muted`, the fold summary's ▸/▾ and its " (N lines)") is used while LAYING OUT
 * and there is no draw call to hand it in at. Every other role a line is drawn in travels on
 * the SPAN, which the markdown renderer already tagged. */
typedef struct RolltuiTranscriptRoles {
  unsigned char background;
  unsigned char selection;
  unsigned char find_match;
  unsigned char find_current;
  unsigned char scroll_marker;
  unsigned char text_muted;
} RolltuiTranscriptRoles;

void rolltui_transcript_set_roles(RolltuiTranscript* t, const RolltuiTranscriptRoles* roles);
void rolltui_transcript_draw(const RolltuiTranscript* t, RolltuiFrame* f, RolltuiDrawScratch* draw,
                             const RolltuiStyle* styles);

/* ---- events ---------------------------------------------------------------------------------------- */

/* THE ELEVEN ACTION NAMES, handed over once. This file knows the RULES and none of the words. */
typedef struct RolltuiTranscriptActions {
  const char* line_up;
  const char* line_down;
  const char* page_up;
  const char* page_down;
  const char* top;
  const char* bottom;
  const char* find_next;
  const char* find_prev;
  const char* fold;
  const char* copy;
  const char* clear_selection;
} RolltuiTranscriptActions;

int rolltui_transcript_handle(RolltuiTranscript* t, const RolltuiEvent* e, const RolltuiDocument* doc,
                              unsigned long long now_ms, const RolltuiBindings* bindings,
                              const RolltuiTranscriptActions* actions);
/* True while a drag holds the pointer outside the area: tick at ~50 ms and re-layout. */
int rolltui_transcript_wants_tick(const RolltuiTranscript* t);
void rolltui_transcript_tick(RolltuiTranscript* t);

/* ---- scrolling --------------------------------------------------------------------------------------- */
void rolltui_transcript_scroll_by(RolltuiTranscript* t, long lines);
void rolltui_transcript_scroll_page(RolltuiTranscript* t, int direction);
void rolltui_transcript_scroll_to_top(RolltuiTranscript* t);
void rolltui_transcript_scroll_to_bottom(RolltuiTranscript* t);
void rolltui_transcript_scroll(const RolltuiTranscript* t, RolltuiScrollAnchor* out);
size_t rolltui_transcript_total_lines(const RolltuiTranscript* t);
size_t rolltui_transcript_top_line(const RolltuiTranscript* t);
size_t rolltui_transcript_lines_below(const RolltuiTranscript* t);
int rolltui_transcript_viewport_height(const RolltuiTranscript* t);

/* ---- folding ------------------------------------------------------------------------------------------ */
int rolltui_transcript_is_folded(const RolltuiTranscript* t, const RolltuiDocEntry* e);
void rolltui_transcript_set_folded(RolltuiTranscript* t, const char* id, size_t len, int folded);
int rolltui_transcript_toggle_fold_nearest_top(RolltuiTranscript* t, const RolltuiDocument* doc);
void rolltui_transcript_set_code_folded(RolltuiTranscript* t, const char* id, size_t len, size_t block,
                                        int folded);
void rolltui_transcript_set_code_uncapped(RolltuiTranscript* t, const char* id, size_t len, size_t block,
                                          int uncapped);

/* ---- find -------------------------------------------------------------------------------------------- */
/* "" clears. Never scrolls: the reveal it asks for happens in layout(). 1 when it CHANGED. */
int rolltui_transcript_set_query(RolltuiTranscript* t, const char* q, size_t len);
const char* rolltui_transcript_query(const RolltuiTranscript* t, size_t* len);
size_t rolltui_transcript_match_count(const RolltuiTranscript* t);
int rolltui_transcript_match_at(const RolltuiTranscript* t, size_t i, RolltuiFindMatch* out);
/* The current match's 1-BASED position, for "3/17"; 0 when there is none. */
size_t rolltui_transcript_current_match_number(const RolltuiTranscript* t);
int rolltui_transcript_current_match(const RolltuiTranscript* t, RolltuiFindMatch* out);
int rolltui_transcript_find_next(RolltuiTranscript* t);
int rolltui_transcript_find_prev(RolltuiTranscript* t);

/* ---- selection ----------------------------------------------------------------------------------------- */
void rolltui_transcript_selection(const RolltuiTranscript* t, RolltuiSelection* out);
void rolltui_transcript_clear_selection(RolltuiTranscript* t);
void rolltui_transcript_select(RolltuiTranscript* t, RolltuiTextPos anchor, RolltuiTextPos head);
/* The logical position under screen cell (x, y). 0 only for an empty document or a row above
 * the area. */
int rolltui_transcript_hit(const RolltuiTranscript* t, int x, int y, RolltuiTextPos* out);
void rolltui_transcript_selected_text(const RolltuiTranscript* t, RolltuiStr* out);
int rolltui_transcript_copy_selection(RolltuiTranscript* t);

/* ---- introspection ------------------------------------------------------------------------------------- */
void rolltui_transcript_stats(const RolltuiTranscript* t, RolltuiTranscriptStats* out);
/* A BORROW, valid until the next layout(). */
const RolltuiEntryLayout* rolltui_transcript_layout_of(const RolltuiTranscript* t, size_t entry);
void rolltui_transcript_area(const RolltuiTranscript* t, RolltuiRect* out);
void rolltui_transcript_text_area(const RolltuiTranscript* t, RolltuiRect* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_TRANSCRIPT_H */
