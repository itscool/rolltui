#ifndef ROLLTUI_C_TRANSCRIPT_H
#define ROLLTUI_C_TRANSCRIPT_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
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

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_input.h" /* RolltuiCopyFn: one clipboard seam, not two */
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_md_lines.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif
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

int rolltui_selection_range_in(const RolltuiSelection* s, size_t entry, size_t len, size_t* begin, size_t* end);

/* THE SYNTAX HIGHLIGHTER, forwarded straight to `rolltui_md_render` — one seam, not two.
 * Setting it bumps an epoch, so a LATER highlighter re-lays everything rather than being
 * silently ignored (set-once is a host convention, not a guarantee). */
void rolltui_transcript_set_highlight(RolltuiTranscript* t, RolltuiMdHighlightFn fn, void* ctx);

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

int rolltui_transcript_viewport_height(const RolltuiTranscript* t);


/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
RolltuiTranscript* rolltui_transcript_new(void);
void rolltui_transcript_free(RolltuiTranscript* t);
/* ---- per frame ------------------------------------------------------------------------------------ */
void rolltui_transcript_layout(RolltuiTranscript* t, const RolltuiDocument* doc, RolltuiRect area,
                               const RolltuiTranscriptOptions* opt);
void rolltui_transcript_draw(const RolltuiTranscript* t, RolltuiFrame* f, RolltuiDrawScratch* draw,
                             const RolltuiStyle* styles);
void rolltui_transcript_scroll_by(RolltuiTranscript* t, long lines);
size_t rolltui_transcript_lines_below(const RolltuiTranscript* t);
int rolltui_transcript_is_folded(const RolltuiTranscript* t, const RolltuiDocEntry* e);
void rolltui_transcript_set_folded(RolltuiTranscript* t, const char* id, size_t len, int folded);
void rolltui_transcript_set_code_folded(RolltuiTranscript* t, const char* id, size_t len, size_t block,
                                        int folded);
void rolltui_transcript_set_code_uncapped(RolltuiTranscript* t, const char* id, size_t len, size_t block,
                                          int uncapped);
int rolltui_transcript_match_at(const RolltuiTranscript* t, size_t i, RolltuiFindMatch* out);
int rolltui_transcript_current_match(const RolltuiTranscript* t, RolltuiFindMatch* out);
void rolltui_transcript_selection(const RolltuiTranscript* t, RolltuiSelection* out);
void rolltui_transcript_clear_selection(RolltuiTranscript* t);
void rolltui_transcript_select(RolltuiTranscript* t, RolltuiTextPos anchor, RolltuiTextPos head);
int rolltui_transcript_hit(const RolltuiTranscript* t, int x, int y, RolltuiTextPos* out);
void rolltui_transcript_selected_text(const RolltuiTranscript* t, RolltuiStr* out);
/* ---- introspection ------------------------------------------------------------------------------------- */
void rolltui_transcript_stats(const RolltuiTranscript* t, RolltuiTranscriptStats* out);
/* A BORROW, valid until the next layout(). */
const RolltuiEntryLayout* rolltui_transcript_layout_of(const RolltuiTranscript* t, size_t entry);
void rolltui_transcript_text_area(const RolltuiTranscript* t, RolltuiRect* out);

/* ---- INTERNAL: not part of the public API ---------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
void rolltui_transcript_scroll(const RolltuiTranscript* t, RolltuiScrollAnchor* out);
size_t rolltui_transcript_total_lines(const RolltuiTranscript* t);
size_t rolltui_transcript_top_line(const RolltuiTranscript* t);

const char* rolltui_transcript_query(const RolltuiTranscript* t, size_t* len);
size_t rolltui_transcript_match_count(const RolltuiTranscript* t);

size_t rolltui_transcript_current_match_number(const RolltuiTranscript* t);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_TRANSCRIPT_H */
