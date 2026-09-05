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

typedef struct { const char* name; const char* desc; } LibAction;

static const LibAction kLibraryActions[] = {
    {"input.submit", "send the line (always Enter)"},
    {"input.newline", "insert a newline"},
    {"input.backspace", "erase before the caret (or the selection)"},
    {"input.delete", "erase after the caret (or the selection)"},
    {"input.kill_word_backward", "kill the word before the caret"},
    {"input.kill_word_forward", "kill the word after the caret"},
    {"input.kill_to_line_start", "kill to the start of the line"},
    {"input.kill_to_line_end", "kill to the end of the line"},
    {"input.left", "move one grapheme left"},
    {"input.right", "move one grapheme right"},
    {"input.word_left", "move one word left"},
    {"input.word_right", "move one word right"},
    {"input.line_start", "start of the line (scrolls when empty)"},
    {"input.line_end", "end of the line (scrolls when empty)"},
    {"input.up", "up a row, or the previous history entry"},
    {"input.down", "down a row, or the next history entry"},
    {"input.select_left", "extend the selection one grapheme left"},
    {"input.select_right", "extend the selection one grapheme right"},
    {"input.select_word_left", "extend the selection one word left"},
    {"input.select_word_right", "extend the selection one word right"},
    {"input.select_line_start", "extend the selection to the start of the line"},
    {"input.select_line_end", "extend the selection to the end of the line"},
    {"input.select_up", "extend the selection up a row"},
    {"input.select_down", "extend the selection down a row"},
    {"input.select_all", "select all"},
    {"input.clear_selection", "clear the selection"},
    {"input.copy", "copy the selection"},
    {"input.eof", "end of input on an empty line, else delete"},
    {"input.undo", "undo the last group of edits"},
    {"input.redo", "redo"},
    {"transcript.page_up", "scroll a page up"},
    {"transcript.page_down", "scroll a page down"},
    {"transcript.top", "scroll to the top"},
    {"transcript.bottom", "scroll to the bottom"},
    {"transcript.line_up", "scroll a line up"},
    {"transcript.line_down", "scroll a line down"},
    {"transcript.find_next", "go to the next match"},
    {"transcript.find_prev", "go to the previous match"},
    {"transcript.fold", "toggle the first folded block in view"},
    {"transcript.copy", "copy the selection again"},
    {"transcript.clear_selection", "clear the selection"},
    {"menu.up", "previous item"},
    {"menu.down", "next item"},
    {"menu.page_up", "a page of items up"},
    {"menu.page_down", "a page of items down"},
    {"menu.first", "the first item"},
    {"menu.last", "the last item"},
    {"menu.activate", "act on the item"},
    {"menu.descend", "descend into a submenu or choice"},
    {"menu.ascend", "up one level"},
    {"menu.back", "clear the filter / up a level / close"},
    {"menu.erase", "erase the last filter character"},
    {"edit.commit", "commit the value being edited"},
    {"edit.cancel", "cancel the edit (the value returns)"},
    {"edit.step_up", "a number field: step up"},
    {"edit.step_down", "a number field: step down"},
    {"stack.close_popup", "close the topmost popup"},
    {"stack.focus_next", "move focus to the next window"},
    {"stack.focus_prev", "move focus to the previous window"},
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
