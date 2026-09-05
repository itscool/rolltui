/* THE LIBRARY'S CLOSED ACTION TABLE, moved here 2026-09-04 and the reason is a
 * measurement rather than a preference.
 *
 * It lived in `Bindings.cpp` and deliberately never crossed, on the rule that the C is
 * TOLD which scopes are the library's through a callback rather than storing the table.
 * That rule is right about SCOPES and was wrong about the TABLE: with the shim gone,
 * consumers that need to enumerate the library's actions had nowhere to ask, so they
 * copied it. Counted 2026-09-04: FOUR copies (bindings_test, deliverability_test,
 * input_test, tools/tool_actions.hpp), each a verbatim duplicate of 59 rows including
 * their English. The hosts would have made it seven.
 *
 * **A vocabulary written down twice is a second thing to drift — which is the rule that
 * kept it out, and the rule it broke.** A vocabulary the C refuses to carry does not
 * disappear; it relocates into every caller. So it is here, once, as literals: the
 * accessors below hand back BORROWS into static storage with no allocation and no
 * lifetime question, and `Bindings.cpp` now reads this rather than owning it.
 *
 * The scope callback stays exactly as it was. "Which scopes are the library's" is still
 * derived, not stored twice. */
#include "rolltui/c/rolltui_bindings.h"

#include <stddef.h>

#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_widget_kinds.h"

typedef struct { const char* name; const char* desc; } LibAction;

/* GROUP, FIELD, name, description. The group and the field are what let the four
 * per-widget tables below expand this SAME list instead of re-spelling it (Phase 17 m2a);
 * the field is the one in that group's `Rolltui*Actions` struct — which is why `input.delete`
 * carries the field `del`: these headers are compiled by C++ too, where `delete` is a keyword.
 * The NAME is the vocabulary; the field is only how a struct spells it. */
#define ROLLTUI_LIBRARY_ACTION_LIST(X) \
  X(input,     submit,            "input.submit",                "send the line (always Enter)") \
  X(input,     newline,           "input.newline",               "insert a newline") \
  X(input,     backspace,         "input.backspace",             "erase before the caret (or the selection)") \
  X(input,     del,               "input.delete",                "erase after the caret (or the selection)") \
  X(input,     kill_word_backward, "input.kill_word_backward",    "kill the word before the caret") \
  X(input,     kill_word_forward, "input.kill_word_forward",     "kill the word after the caret") \
  X(input,     kill_to_line_start, "input.kill_to_line_start",    "kill to the start of the line") \
  X(input,     kill_to_line_end,  "input.kill_to_line_end",      "kill to the end of the line") \
  X(input,     left,              "input.left",                  "move one grapheme left") \
  X(input,     right,             "input.right",                 "move one grapheme right") \
  X(input,     word_left,         "input.word_left",             "move one word left") \
  X(input,     word_right,        "input.word_right",            "move one word right") \
  X(input,     line_start,        "input.line_start",            "start of the line (scrolls when empty)") \
  X(input,     line_end,          "input.line_end",              "end of the line (scrolls when empty)") \
  X(input,     up,                "input.up",                    "up a row, or the previous history entry") \
  X(input,     down,              "input.down",                  "down a row, or the next history entry") \
  X(input,     select_left,       "input.select_left",           "extend the selection one grapheme left") \
  X(input,     select_right,      "input.select_right",          "extend the selection one grapheme right") \
  X(input,     select_word_left,  "input.select_word_left",      "extend the selection one word left") \
  X(input,     select_word_right, "input.select_word_right",     "extend the selection one word right") \
  X(input,     select_line_start, "input.select_line_start",     "extend the selection to the start of the line") \
  X(input,     select_line_end,   "input.select_line_end",       "extend the selection to the end of the line") \
  X(input,     select_up,         "input.select_up",             "extend the selection up a row") \
  X(input,     select_down,       "input.select_down",           "extend the selection down a row") \
  X(input,     select_all,        "input.select_all",            "select all") \
  X(input,     clear_selection,   "input.clear_selection",       "clear the selection") \
  X(input,     copy,              "input.copy",                  "copy the selection") \
  X(input,     eof,               "input.eof",                   "end of input on an empty line, else delete") \
  X(input,     undo,              "input.undo",                  "undo the last group of edits") \
  X(input,     redo,              "input.redo",                  "redo") \
  X(transcript, page_up,           "transcript.page_up",          "scroll a page up") \
  X(transcript, page_down,         "transcript.page_down",        "scroll a page down") \
  X(transcript, top,               "transcript.top",              "scroll to the top") \
  X(transcript, bottom,            "transcript.bottom",           "scroll to the bottom") \
  X(transcript, line_up,           "transcript.line_up",          "scroll a line up") \
  X(transcript, line_down,         "transcript.line_down",        "scroll a line down") \
  X(transcript, find_next,         "transcript.find_next",        "go to the next match") \
  X(transcript, find_prev,         "transcript.find_prev",        "go to the previous match") \
  X(transcript, fold,              "transcript.fold",             "toggle the first folded block in view") \
  X(transcript, copy,              "transcript.copy",             "copy the selection again") \
  X(transcript, clear_selection,   "transcript.clear_selection",  "clear the selection") \
  X(menu,      up,                "menu.up",                     "previous item") \
  X(menu,      down,              "menu.down",                   "next item") \
  X(menu,      page_up,           "menu.page_up",                "a page of items up") \
  X(menu,      page_down,         "menu.page_down",              "a page of items down") \
  X(menu,      first,             "menu.first",                  "the first item") \
  X(menu,      last,              "menu.last",                   "the last item") \
  X(menu,      activate,          "menu.activate",               "act on the item") \
  X(menu,      descend,           "menu.descend",                "descend into a submenu or choice") \
  X(menu,      ascend,            "menu.ascend",                 "up one level") \
  X(menu,      back,              "menu.back",                   "clear the filter / up a level / close") \
  X(menu,      erase,             "menu.erase",                  "erase the last filter character") \
  X(edit,      commit,            "edit.commit",                 "commit the value being edited") \
  X(edit,      cancel,            "edit.cancel",                 "cancel the edit (the value returns)") \
  X(edit,      step_up,           "edit.step_up",                "a number field: step up") \
  X(edit,      step_down,         "edit.step_down",              "a number field: step down") \
  X(stack,     close_popup,       "stack.close_popup",           "close the topmost popup") \
  X(stack,     focus_next,        "stack.focus_next",            "move focus to the next window") \
  X(stack,     focus_prev,        "stack.focus_prev",            "move focus to the previous window")

static const LibAction kLibraryActions[] = {
#define ROLLTUI_LA_ROW_(group, field, name, desc) {name, desc},
    ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_ROW_)
#undef ROLLTUI_LA_ROW_
};

size_t rolltui_library_action_count(void) {
  return sizeof kLibraryActions / sizeof kLibraryActions[0];
}

/* Both BORROW into static storage: valid for the life of the process, never freed. */
const char* rolltui_library_action_name(size_t i, size_t* len) {
  if (i >= rolltui_library_action_count()) { if (len) *len = 0; return NULL; }
  const char* s = kLibraryActions[i].name;
  if (len) { size_t n = 0; while (s[n]) ++n; *len = n; }
  return s;
}

const char* rolltui_library_action_description(size_t i, size_t* len) {
  if (i >= rolltui_library_action_count()) { if (len) *len = 0; return NULL; }
  const char* s = kLibraryActions[i].desc;
  if (len) { size_t n = 0; while (s[n]) ++n; *len = n; }
  return s;
}

/* ---- THE FOUR PER-WIDGET TABLES, expanded from the ONE list above (Phase 17 m2a) ----------
 *
 * `rolltui_input_handle`, `rolltui_menu_handle`, `rolltui_transcript_handle` and the
 * `scroll_text` kinds each take their action names as a mandatory struct, and each header says
 * why: "this file knows what each command DOES and none of the words". That trade is right and
 * is untouched — what was wrong is that the words then lived in `Input.cpp`, `Menu.cpp`,
 * `Transcript.cpp` and `Widgets.cpp`, and `Menu.cpp`'s was in an ANONYMOUS namespace. So a C
 * consumer could not call `rolltui_menu_handle` at all, and the tell had already fired:
 * `menu_test.cpp:452` hand-wrote its own copy of both the menu and the input table to get past
 * exactly that. Found 2026-09-05 by an agent converting the three editors, which is the same
 * discovery route as every other gap this phase — a consumer hitting a wall, never a survey.
 *
 * The C now HANDS THE CALLER ITS OWN DEFAULT rather than storing a second copy: one list, four
 * expansions, and a row of the wrong group expands to nothing. Adding an action stays one edit.
 * A host that wants different words still passes its own struct; nothing became mandatory. */

/* One emit macro per group; the three targets below redefine only the group they want. */
#define ROLLTUI_LA_input(field, name)
#define ROLLTUI_LA_transcript(field, name)
#define ROLLTUI_LA_menu(field, name)
#define ROLLTUI_LA_edit(field, name)
#define ROLLTUI_LA_stack(field, name)
#define ROLLTUI_LA_PICK_(group, field, name, desc) ROLLTUI_LA_##group(field, name)

#undef ROLLTUI_LA_input
#define ROLLTUI_LA_input(field, name) .field = name,
static const RolltuiInputActions kInputActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_input
#define ROLLTUI_LA_input(field, name)

#undef ROLLTUI_LA_transcript
#define ROLLTUI_LA_transcript(field, name) .field = name,
static const RolltuiTranscriptActions kTranscriptActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_transcript

/* The scroll-text kinds take the SIX of the eleven a plain scrolling view can use — the
 * transcript's own find/fold/copy have no meaning without a transcript. Named per field rather
 * than by position, so a reordering of either struct cannot silently shift them. */
#define ROLLTUI_LA_ST_line_up(name) .line_up = name,
#define ROLLTUI_LA_ST_line_down(name) .line_down = name,
#define ROLLTUI_LA_ST_page_up(name) .page_up = name,
#define ROLLTUI_LA_ST_page_down(name) .page_down = name,
#define ROLLTUI_LA_ST_top(name) .top = name,
#define ROLLTUI_LA_ST_bottom(name) .bottom = name,
#define ROLLTUI_LA_ST_find_next(name)
#define ROLLTUI_LA_ST_find_prev(name)
#define ROLLTUI_LA_ST_fold(name)
#define ROLLTUI_LA_ST_copy(name)
#define ROLLTUI_LA_ST_clear_selection(name)
#define ROLLTUI_LA_transcript(field, name) ROLLTUI_LA_ST_##field(name)
static const RolltuiScrollTextActions kScrollTextActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_transcript
#define ROLLTUI_LA_transcript(field, name)

/* The menu's fifteen are `menu.*` plus the four `edit.*` a typed field opens, and the last
 * member is a POINTER to the input table above — the header's own rule that there is one table
 * of input action names in the library, now literally true. */
#undef ROLLTUI_LA_menu
#undef ROLLTUI_LA_edit
#define ROLLTUI_LA_menu(field, name) .field = name,
#define ROLLTUI_LA_edit(field, name) .field = name,
static const RolltuiMenuActions kMenuActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_) .input = &kInputActions};
#undef ROLLTUI_LA_menu
#undef ROLLTUI_LA_edit
#define ROLLTUI_LA_menu(field, name)
#define ROLLTUI_LA_edit(field, name)

/* The window stack's three — the FIFTH expansion of the one list, added in m3 for the reason
 * the other four were added in m2a. They were `Layout.cpp`'s `kStackActions`, a file m2c
 * deletes, and all three hosts call `rolltui_window_stack_route`: a vocabulary with no home
 * becomes one copy per caller. `ROLLTUI_LA_stack` was already declared (and empty) above
 * because the group exists in the list; this is the target that asks for it. */
#undef ROLLTUI_LA_stack
#define ROLLTUI_LA_stack(field, name) .field = name,
static const RolltuiStackActions kStackActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_stack
#define ROLLTUI_LA_stack(field, name)

/* All five BORROW static storage, valid for the life of the process, never freed. */
const RolltuiInputActions* rolltui_input_default_actions(void) { return &kInputActions; }
const RolltuiMenuActions* rolltui_menu_default_actions(void) { return &kMenuActions; }
const RolltuiTranscriptActions* rolltui_transcript_default_actions(void) { return &kTranscriptActions; }
const RolltuiScrollTextActions* rolltui_scroll_text_default_actions(void) { return &kScrollTextActions; }
const RolltuiStackActions* rolltui_stack_default_actions(void) { return &kStackActions; }
