/* THE LIBRARY'S CLOSED ACTION TABLE, and it lives here because of a measurement rather than
 * a preference.
 *
 * The rule that the C is TOLD which scopes are the library's, through a callback rather than
 * by storing a table, is right about SCOPES and wrong about the TABLE. A consumer that needs
 * to ENUMERATE the library's actions has nowhere to ask, so it copies them: FOUR copies
 * accumulated before this file existed (bindings_test, deliverability_test, input_test,
 * tools/tool_actions.hpp), each a verbatim duplicate of 59 rows including their English, with
 * the hosts on course to make it seven.
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

#include "rolltui/c/rolltui_widget_input.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_widget_transcript.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_widget_list.h"

typedef struct { const char* name; const char* desc; } LibAction;

/* GROUP, FIELD, name, description. The group and the field are what let the four
 * per-widget tables below expand this SAME list instead of re-spelling it;
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
  X(input,     yank,              "input.yank",                  "put back what the last kills took") \
  X(input,     transpose_chars,   "input.transpose_chars",       "swap the graphemes either side of the caret") \
  X(input,     transpose_words,   "input.transpose_words",       "swap the word before the caret with the next") \
  X(input,     upcase_word,       "input.upcase_word",           "uppercase to the end of the word") \
  X(input,     downcase_word,     "input.downcase_word",         "lowercase to the end of the word") \
  X(input,     capitalize_word,   "input.capitalize_word",       "capitalise to the end of the word") \
  X(input,     overwrite,         "input.overwrite",             "typing replaces what is under the caret, or inserts again") \
  X(input,     normal_mode,       "input.normal_mode",           "from typing to normal mode, where the keys preset has one") \
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
  X(picker,    up,                "picker.up",                   "the row above") \
  X(picker,    down,              "picker.down",                 "the row below") \
  X(picker,    page_up,           "picker.page_up",              "a page up") \
  X(picker,    page_down,         "picker.page_down",            "a page down") \
  X(picker,    first,             "picker.first",                "the first row") \
  X(picker,    last,              "picker.last",                 "the last row") \
  X(picker,    into,              "picker.into",                 "into the selected folder") \
  X(picker,    out,               "picker.out",                  "out to the parent folder") \
  X(picker,    take,              "picker.take",                 "choose what is under the cursor") \
  X(picker,    cancel,            "picker.cancel",               "choose nothing") \
  X(picker,    copy,              "picker.copy",                 "copy the selection's path") \
  X(picker,    copy_inverse,      "picker.copy_inverse",         "copy the path the other way round (absolute / relative)") \
  X(picker,    half_up,           "picker.half_up",              "half a page up") \
  X(picker,    half_down,         "picker.half_down",            "half a page down") \
  X(picker,    back,              "picker.back",                 "the folder visited before") \
  X(picker,    forward,           "picker.forward",              "the folder visited after") \
  X(picker,    previous,          "picker.previous",             "the folder last left") \
  X(picker,    mark,              "picker.mark",                 "mark or unmark the entry, and move to the next") \
  X(picker,    mark_up,           "picker.mark_up",              "mark the entry and the one above") \
  X(picker,    mark_down,         "picker.mark_down",            "mark the entry and the one below") \
  X(picker,    unmark_all,        "picker.unmark_all",           "clear every mark") \
  X(picker,    preview_left,      "picker.preview_left",         "scroll the preview's lines left") \
  X(picker,    preview_right,     "picker.preview_right",        "scroll the preview's lines right") \
  X(list,      up,                "list.up",                     "the row above") \
  X(list,      down,              "list.down",                   "the row below") \
  X(list,      page_up,           "list.page_up",                "a page up") \
  X(list,      page_down,         "list.page_down",              "a page down") \
  X(list,      first,             "list.first",                  "the first row; after a count, that row") \
  X(list,      last,              "list.last",                   "the last row; after a count, that row") \
  X(list,      half_up,           "list.half_up",                "half a page up") \
  X(list,      half_down,         "list.half_down",              "half a page down") \
  X(list,      view_top,          "list.view_top",               "the top row in view") \
  X(list,      view_middle,       "list.view_middle",            "the middle row in view") \
  X(list,      view_bottom,       "list.view_bottom",            "the bottom row in view") \
  X(list,      scroll_up,         "list.scroll_up",              "scroll up a row, the cursor staying on its row while it can") \
  X(list,      scroll_down,       "list.scroll_down",            "scroll down a row, the cursor staying on its row while it can") \
  X(list,      percent,           "list.percent",                "after a count, that percent of the way down") \
  X(list,      activate,          "list.activate",               "act on the row (a tree's node opens or closes)") \
  X(list,      back,              "list.back",                   "clear the filter, then the marks") \
  X(list,      mark,              "list.mark",                   "mark or unmark the row, and move on") \
  X(list,      mark_up,           "list.mark_up",                "mark from where the range began up a row") \
  X(list,      mark_down,         "list.mark_down",              "mark from where the range began down a row") \
  X(list,      mark_all,          "list.mark_all",               "mark every row shown") \
  X(list,      unmark_all,        "list.unmark_all",             "unmark every row") \
  X(list,      invert_marks,      "list.invert_marks",           "mark what is unmarked and the other way round") \
  X(list,      open,              "list.open",                   "open the node, or into it") \
  X(list,      close,             "list.close",                  "close the node, or up to its parent") \
  X(list,      open_all,          "list.open_all",               "open every node") \
  X(list,      open_deep,         "list.open_deep",              "open the node and everything under it") \
  X(list,      visual,            "list.visual",                 "mark from here to wherever the cursor goes") \
  X(list,      visual_swap,       "list.visual_swap",            "go to the other end of the range") \
  X(list,      move_up,           "list.move_up",                "move the row up") \
  X(list,      move_down,         "list.move_down",              "move the row down") \
  X(list,      close_all,         "list.close_all",              "close every node") \
  X(list,      sort,              "list.sort",                   "sort by the next column, then the list's own order") \
  X(list,      reverse,           "list.reverse",                "the sort the other way") \
  X(list,      filter,            "list.filter",                 "type to narrow the rows") \
  X(stack,     close_popup,       "stack.close_popup",           "close the topmost popup") \
  X(stack,     focus_next,        "stack.focus_next",            "move focus to the next window") \
  X(stack,     focus_prev,        "stack.focus_prev",            "move focus to the previous window") \
  X(stack,     focus_left,        "stack.focus_left",            "move focus to the window on the left") \
  X(stack,     focus_right,       "stack.focus_right",           "move focus to the window on the right") \
  X(stack,     focus_up,          "stack.focus_up",              "move focus to the window above") \
  X(stack,     focus_down,        "stack.focus_down",            "move focus to the window below") \
  X(stack,     focus_number,      "stack.focus_number",          "move focus to the window with that number (Alt and a digit)") \
  X(stack,     next_tab,          "stack.next_tab",              "show the focused window's next tab") \
  X(stack,     prev_tab,          "stack.prev_tab",              "show the focused window's previous tab") \
  X(stack,     maximize,          "stack.maximize",              "make the focused window fill the screen, and put it back") \
  X(stack,     half,              "stack.half",                  "give the focused window half the screen, and put it back") \
  X(sheet,     remove,            "sheet.remove",                "remove the row's last key") \
  X(sheet,     undo,              "sheet.undo",                  "undo the last change") \
  X(sheet,     redo,              "sheet.redo",                  "do it again") \
  X(sheet,     load,              "sheet.load",                  "load a preset") \
  X(sheet,     save,              "sheet.save",                  "save as a preset under a name") \
  X(sheet,     reset,             "sheet.reset",                 "go back to the preset loaded") \
  X(theme,     variant,           "theme.variant",               "edit the other variant, light or dark") \
  X(theme,     generate,          "theme.generate",              "make a theme from the next seed") \
  X(theme,     fix,               "theme.fix",                   "fix the problem on the role's row") \
  X(dialog,    next,              "dialog.next",                 "choose the next button") \
  X(dialog,    prev,              "dialog.prev",                 "choose the previous button") \
  X(dialog,    answer,            "dialog.answer",               "answer with the button chosen") \
  X(dialog,    up,                "dialog.up",                   "scroll the items up a row") \
  X(dialog,    down,              "dialog.down",                 "scroll the items down a row") \
  X(dialog,    page_up,           "dialog.page_up",              "scroll the items up a page") \
  X(dialog,    page_down,         "dialog.page_down",            "scroll the items down a page") \
  X(dialog,    copy,              "dialog.copy",                 "copy the dialog's text") \
  X(normal,    insert,            "normal.insert",                 "type before the caret") \
  X(normal,    append,            "normal.append",                 "type after the caret") \
  X(normal,    insert_start,      "normal.insert_start",           "type at the start of the line") \
  X(normal,    append_end,        "normal.append_end",             "type at the end of the line") \
  X(normal,    visual,            "normal.visual",                 "select from here, for an operator") \
  X(normal,    replace_char,      "normal.replace_char",           "replace the character under the caret with the next one typed") \
  X(normal,    replace,           "normal.replace",                "type over what is there") \
  X(normal,    left,              "normal.left",                   "the character to the left") \
  X(normal,    right,             "normal.right",                  "the character to the right") \
  X(normal,    word_right,        "normal.word_right",             "the start of the next word") \
  X(normal,    word_left,         "normal.word_left",              "the start of this word, or the one before") \
  X(normal,    word_end,          "normal.word_end",               "the end of this word, or the next one") \
  X(normal,    line_start,        "normal.line_start",             "the start of the line") \
  X(normal,    line_end,          "normal.line_end",               "the last character of the line") \
  X(normal,    up,                "normal.up",                     "the line above") \
  X(normal,    down,              "normal.down",                   "the line below") \
  X(normal,    del,               "normal.delete",                 "erase the character under the caret") \
  X(normal,    backspace,         "normal.backspace",              "erase the character before the caret") \
  X(normal,    delete_word,       "normal.delete_word",            "erase to the start of the next word") \
  X(normal,    delete_word_back,  "normal.delete_word_back",       "erase to the start of this word") \
  X(normal,    delete_line,       "normal.delete_line",            "erase the line") \
  X(normal,    delete_to_end,     "normal.delete_to_end",          "erase to the end of the line") \
  X(normal,    delete_to_start,   "normal.delete_to_start",        "erase to the start of the line") \
  X(normal,    change_word,       "normal.change_word",            "replace to the end of the word, typing") \
  X(normal,    change_line,       "normal.change_line",            "replace the line, typing") \
  X(normal,    change_to_end,     "normal.change_to_end",          "replace to the end of the line, typing") \
  X(normal,    substitute,        "normal.substitute",             "replace the character under the caret, typing") \
  X(normal,    yank_line,         "normal.yank_line",              "copy the line") \
  X(normal,    yank_word,         "normal.yank_word",              "copy to the start of the next word") \
  X(normal,    paste_after,       "normal.paste_after",            "put what was copied or erased after the caret") \
  X(normal,    paste_before,      "normal.paste_before",           "put what was copied or erased before the caret") \
  X(normal,    undo,              "normal.undo",                   "undo the last change") \
  X(normal,    redo,              "normal.redo",                   "do it again") \
  X(visual,    yank,              "visual.yank",                   "copy the selection") \
  X(visual,    del,               "visual.delete",                 "erase the selection") \
  X(visual,    change,            "visual.change",                 "replace the selection, typing") \
  X(visual,    normal,            "visual.normal",                 "back to normal mode") \
  X(view,      wrap,              "view.wrap",                   "wrap long lines, or keep each whole and scroll sideways") \
  X(view,      left,              "view.left",                   "scroll left, where lines are kept whole") \
  X(view,      right,             "view.right",                  "scroll right, where lines are kept whole")

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

/* ---- THE FOUR PER-WIDGET TABLES, expanded from the ONE list above ---------------------------
 *
 * `rolltui_input_handle`, `rolltui_menu_handle`, `rolltui_transcript_handle` and the
 * `scroll_text` kinds each take their action names as a mandatory struct, and each header says
 * why: "this file knows what each command DOES and none of the words". That trade is right and
 * is untouched — what fails is the words living in a C++ file, one of them in an ANONYMOUS
 * namespace, where a C consumer cannot reach them and so cannot call `rolltui_menu_handle` at
 * all. The tell fires before anyone notices: a suite hand-writes its own copy of both the menu
 * and the input table to get past exactly that. Every gap of this kind has been found by a
 * consumer hitting a wall, never by a survey.
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
#define ROLLTUI_LA_picker(field, name)
#define ROLLTUI_LA_list(field, name)
#define ROLLTUI_LA_sheet(field, name)
#define ROLLTUI_LA_theme(field, name)
#define ROLLTUI_LA_dialog(field, name)
#define ROLLTUI_LA_normal(field, name)
#define ROLLTUI_LA_visual(field, name)
#define ROLLTUI_LA_view(field, name)
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

/* The window stack's three — the FIFTH expansion of the one list, and here for the same reason
 * as the other four: every host calls `rolltui_window_stack_route`, and a vocabulary with no
 * home becomes one copy per caller. `ROLLTUI_LA_stack` was already declared (and empty) above
 * because the group exists in the list; this is the target that asks for it. */
#undef ROLLTUI_LA_stack
#define ROLLTUI_LA_stack(field, name) .field = name,
static const RolltuiStackActions kStackActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_stack
#define ROLLTUI_LA_stack(field, name)

/* All five BORROW static storage, valid for the life of the process, never freed. */
#undef ROLLTUI_LA_picker
#define ROLLTUI_LA_picker(field, name) .field = name,
static const RolltuiPickerActions kPickerActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_picker
#define ROLLTUI_LA_picker(field, name)

#undef ROLLTUI_LA_list
#define ROLLTUI_LA_list(field, name) .field = name,
static const RolltuiListActions kListActions = {ROLLTUI_LIBRARY_ACTION_LIST(ROLLTUI_LA_PICK_)};
#undef ROLLTUI_LA_list
#define ROLLTUI_LA_list(field, name)

const RolltuiPickerActions* rolltui_picker_default_actions(void) { return &kPickerActions; }
const RolltuiListActions* rolltui_list_default_actions(void) { return &kListActions; }
const RolltuiInputActions* rolltui_input_default_actions(void) { return &kInputActions; }
const RolltuiMenuActions* rolltui_menu_default_actions(void) { return &kMenuActions; }
const RolltuiTranscriptActions* rolltui_transcript_default_actions(void) { return &kTranscriptActions; }
const RolltuiScrollTextActions* rolltui_scroll_text_default_actions(void) { return &kScrollTextActions; }
const RolltuiStackActions* rolltui_stack_default_actions(void) { return &kStackActions; }
