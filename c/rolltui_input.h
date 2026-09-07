#ifndef ROLLTUI_C_INPUT_H
#define ROLLTUI_C_INPUT_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
/*
 * rolltui/c/rolltui_input.h — THE INPUT WIDGET, AS A STATE MACHINE (Phase 15 m5).
 *
 * A multi-line text field with a caret, a selection, undo, history, mouse and paste. Every
 * rule — what may enter the text, where a boundary lies, how a row wraps, what closes an
 * undo group — is stated in `rolltui/Input.hpp` and asserted in
 * `rolltui/tests/input_test.cpp`; none of it is repeated here.
 *
 * ---- WHAT THIS MODULE OWNS, WHICH IS THE WHOLE REASON IT IS IN THIS MILESTONE ----------
 *
 * Ten buffers, and the C++ named none of them: the text, the grapheme array, the wrap
 * flow's two arrays, the prompt / placeholder, the history and its draft, and an undo stack
 * of WHOLE SNAPSHOTS — each snapshot a `std::string` of its own. `UndoStack<InputSnapshot>`
 * is a `std::vector<T>` of those, capped at 200, and a `commit` copies the entire text.
 * Nobody decided any of it; it is what you type.
 *
 * ---- THE BOUNDARY'S RULES, all inherited from Phase 14 and none new --------------------
 *
 *   1. **THE CALLER OWNS EVERY BUFFER.** The input is a handle the caller makes and frees.
 *   2. **NOTHING IS RETURNED BY VALUE** except plain scalars — and `RolltuiRect` is not one
 *      of those, so a rect comes back through an out-param (`rolltui_layout.h` has the
 *      `-Wreturn-type-c-linkage` reasoning).
 *   3. **TEXT OUT IS A BORROW** with a stated window: valid until the text next changes.
 *   4. **NO `std::function` CROSSES.** The host's clipboard is a function pointer plus a
 *      `void*`, set once.
 *   5. **THIS FILE NAMES NO ROLE AND NO ACTION.** The four roles a draw needs are bytes in
 *      `RolltuiInputRoles`; the thirty action names are handed over ONCE, in command order,
 *      as `RolltuiInputActions`. The C knows what each command DOES and none of the words —
 *      the same trade `RolltuiStackActions` makes for the three the stack owns.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif
/* WHAT `insert` WOULD PRODUCE, without performing it, into a caller's owned string.
 *
 * It exists because the menu's typed fields check a keystroke as a PREFIX of some valid
 * value before letting it land (Menu.hpp), and the C++ did that by copying the WHOLE Input —
 * text, grapheme array, history, and an undo stack of up to two hundred whole-text snapshots
 * — once per keystroke, to read one string off it and throw the rest away. Nobody decided
 * that either: `Input probe = edit_;` is what you type. */
void rolltui_input_preview_insert(const RolltuiInput* in, const char* utf8, size_t len, RolltuiStr* out);
int rolltui_input_erase_selection(RolltuiInput* in);
void rolltui_input_erase_backward(RolltuiInput* in);
void rolltui_input_erase_forward(RolltuiInput* in);
void rolltui_input_kill_word_backward(RolltuiInput* in);
void rolltui_input_kill_word_forward(RolltuiInput* in);
void rolltui_input_kill_to_line_start(RolltuiInput* in);
void rolltui_input_kill_to_line_end(RolltuiInput* in);
void rolltui_input_move_left(RolltuiInput* in, int extend);
void rolltui_input_move_right(RolltuiInput* in, int extend);
void rolltui_input_move_word_left(RolltuiInput* in, int extend);
void rolltui_input_move_word_right(RolltuiInput* in, int extend);
void rolltui_input_move_line_start(RolltuiInput* in, int extend);
void rolltui_input_move_line_end(RolltuiInput* in, int extend);
int rolltui_input_move_up(RolltuiInput* in, int extend);   /* 0 on the first row */
int rolltui_input_move_down(RolltuiInput* in, int extend); /* 0 on the last row */
size_t rolltui_input_word_left_of(const RolltuiInput* in, size_t pos);
size_t rolltui_input_word_right_of(const RolltuiInput* in, size_t pos);

int rolltui_input_history_prev(RolltuiInput* in);
int rolltui_input_history_next(RolltuiInput* in);

int rolltui_input_rows_for(const RolltuiInput* in, int width);
/* The grapheme under a screen cell as [begin, end). 0 only before any layout(). */
int rolltui_input_hit(const RolltuiInput* in, int x, int y, size_t* begin, size_t* end);


/* ---- PHASE 20 m1/m3: INTERNAL — moved out of the definition ------------------------------
 * A test's reach is never a reason to be public, and nothing but a suite that tests this
 * module's implementation reaches these. They are unchanged; what moved is the PROMISE.
 * A suite that needs one includes this header and names itself in `ROLLTUI_INTERNAL_OPT_IN`. */
void rolltui_input_options_init(RolltuiInputOptions* o);
RolltuiInput* rolltui_input_new(void);
void rolltui_input_free(RolltuiInput* in); /* a no-op on NULL */
void rolltui_input_set_text(RolltuiInput* in, const char* text, size_t len);
size_t rolltui_input_caret(const RolltuiInput* in);
void rolltui_input_set_caret(RolltuiInput* in, size_t byte, int extend);
void rolltui_input_selection(const RolltuiInput* in, RolltuiInputSelection* out);
/* The selected bytes, a BORROW into the text; `*len` 0 when there is no selection. */
const char* rolltui_input_selected_text(const RolltuiInput* in, size_t* len);
void rolltui_input_select_all(RolltuiInput* in);
void rolltui_input_clear_selection(RolltuiInput* in);
/* ---- editing primitives --------------------------------------------------------------------- */
void rolltui_input_insert(RolltuiInput* in, const char* utf8, size_t len);
/* ---- undo / redo ------------------------------------------------------------------------------ */
int rolltui_input_undo(RolltuiInput* in);
int rolltui_input_redo(RolltuiInput* in);
int rolltui_input_can_undo(const RolltuiInput* in);
int rolltui_input_can_redo(const RolltuiInput* in);
/* ---- history ---------------------------------------------------------------------------------- */
void rolltui_input_push_history(RolltuiInput* in, const char* entry, size_t len);
size_t rolltui_input_history_count(const RolltuiInput* in);
/* A BORROW, valid until the history next changes. */
const char* rolltui_input_history_at(const RolltuiInput* in, size_t i, size_t* len);
size_t rolltui_input_history_cursor(const RolltuiInput* in);
unsigned char rolltui_input_handle(RolltuiInput* in, const RolltuiEvent* e, const RolltuiBindings* bindings,
                                   const RolltuiInputActions* actions, unsigned long long now_ms);
int rolltui_input_rows(const RolltuiInput* in);
void rolltui_input_layout(RolltuiInput* in, RolltuiRect area);
int rolltui_input_top_row(const RolltuiInput* in);
/* Where a position is drawn: a text row (before scrolling) and a column from the area's left
 * edge, the prompt / indent included. */
void rolltui_input_cell_of(const RolltuiInput* in, size_t offset, int* row, int* col);
void rolltui_input_draw(const RolltuiInput* in, RolltuiFrame* f, RolltuiDrawScratch* draw,
                        const RolltuiStyle* styles, const RolltuiInputRoles* roles, int focused);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_INPUT_H */
