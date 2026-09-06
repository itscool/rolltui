#ifndef ROLLTUI_C_INPUT_H
#define ROLLTUI_C_INPUT_H
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
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
namespace rolltui {
enum class Role : unsigned char;  // declared, not defined: this file names no role
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- what a handle() call answers ------------------------------------------------------- */
#define ROLLTUI_INPUT_IGNORED 0
#define ROLLTUI_INPUT_HANDLED 1
#define ROLLTUI_INPUT_SUBMIT 2
#define ROLLTUI_INPUT_EOF 3

/* ---- options ---------------------------------------------------------------------------- */
/* `rolltui::InputOptions` IS this struct (one definition). The two strings are OWNED, which
 * the C++ said with `std::string` and never had to think about; here they are `RolltuiStr`
 * and the copy is a function with a name. */
typedef struct RolltuiInputOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(4);
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
  RolltuiStr prompt;            /* drawn before the first row, in prompt_role */
#ifdef __cplusplus
  rolltui::Role prompt_role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_PROMPT);
#else
  unsigned char prompt_role;
#endif
  RolltuiStr placeholder; /* drawn after the prompt while the text is empty */
  unsigned long long multi_click_ms ROLLTUI_DEFAULT(400);
  size_t history_limit ROLLTUI_DEFAULT(1000);
  unsigned char single_line ROLLTUI_DEFAULT(0); /* a newline is dropped — a menu field */

#ifdef __cplusplus
  RolltuiInputOptions();
  bool operator==(const RolltuiInputOptions& o) const;
#endif
} RolltuiInputOptions;

/* The defaults, for a C caller — `= {0}` would give a zero tab width and no prompt role,
 * which is Phase 14's design lens exactly. */
void rolltui_input_options_init(RolltuiInputOptions* o);
void rolltui_input_options_release(RolltuiInputOptions* o);
void rolltui_input_options_copy(RolltuiInputOptions* to, const RolltuiInputOptions* from);
int rolltui_input_options_equal(const RolltuiInputOptions* a, const RolltuiInputOptions* b);

/* ---- the selection ------------------------------------------------------------------------ */
/* `rolltui::InputSelection` IS this struct. */
typedef struct RolltuiInputSelection {
  size_t anchor ROLLTUI_DEFAULT(0), head ROLLTUI_DEFAULT(0);
  unsigned char active ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  size_t begin() const { return anchor < head ? anchor : head; }
  size_t end() const { return anchor < head ? head : anchor; }
  bool empty() const { return !active || anchor == head; }
  bool operator==(const RolltuiInputSelection&) const = default;
#endif
} RolltuiInputSelection;

/* ---- the handle ---------------------------------------------------------------------------- */

typedef struct RolltuiInput RolltuiInput;
RolltuiInput* rolltui_input_new(void);
void rolltui_input_free(RolltuiInput* in); /* a no-op on NULL */

/* THE HOST'S CLIPBOARD, as a function pointer and a context rather than a `std::function`.
 * Set once; NULL turns it off. The text is a BORROW for the call. */
typedef void (*RolltuiCopyFn)(void* ctx, const char* text, size_t len);
void rolltui_input_set_copy(RolltuiInput* in, RolltuiCopyFn fn, void* ctx);

/* ---- content -------------------------------------------------------------------------------- */
/* A BORROW, valid until the text next changes. */
const char* rolltui_input_text(const RolltuiInput* in, size_t* len);
void rolltui_input_set_text(RolltuiInput* in, const char* text, size_t len);
void rolltui_input_clear(RolltuiInput* in);
size_t rolltui_input_caret(const RolltuiInput* in);
void rolltui_input_set_caret(RolltuiInput* in, size_t byte, int extend);
void rolltui_input_selection(const RolltuiInput* in, RolltuiInputSelection* out);
/* The selected bytes, a BORROW into the text; `*len` 0 when there is no selection. */
const char* rolltui_input_selected_text(const RolltuiInput* in, size_t* len);
void rolltui_input_select_all(RolltuiInput* in);
void rolltui_input_clear_selection(RolltuiInput* in);

/* ---- editing primitives --------------------------------------------------------------------- */
void rolltui_input_insert(RolltuiInput* in, const char* utf8, size_t len);
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
int rolltui_input_history_prev(RolltuiInput* in);
int rolltui_input_history_next(RolltuiInput* in);

/* ---- events ------------------------------------------------------------------------------------ */

/* THE THIRTY ACTION NAMES, in command order, handed over by the shim. This file knows what
 * each command DOES and none of the words; `rolltui/Input.hpp` lists them and
 * `library_actions()` in `Bindings.cpp` is where they are written down. */
typedef struct RolltuiInputActions {
  const char* submit;
  const char* newline;
  const char* backspace;
  const char* del;
  const char* kill_word_backward;
  const char* kill_word_forward;
  const char* kill_to_line_start;
  const char* kill_to_line_end;
  const char* left;
  const char* right;
  const char* word_left;
  const char* word_right;
  const char* line_start;
  const char* line_end;
  const char* up;
  const char* down;
  const char* select_left;
  const char* select_right;
  const char* select_word_left;
  const char* select_word_right;
  const char* select_line_start;
  const char* select_line_end;
  const char* select_up;
  const char* select_down;
  const char* select_all;
  const char* clear_selection;
  const char* copy;
  const char* eof;
  const char* undo;
  const char* redo;
} RolltuiInputActions;

/* The LIBRARY'S OWN thirty, so a consumer need not spell them to call `handle` (Phase 17 m2a).
 * BORROWS static storage. `rolltui_library_actions.c` expands one list into this and three
 * siblings; a host with different words still passes its own struct. */
const RolltuiInputActions* rolltui_input_default_actions(void);

unsigned char rolltui_input_handle(RolltuiInput* in, const RolltuiEvent* e, const RolltuiBindings* bindings,
                                   const RolltuiInputActions* actions, unsigned long long now_ms);

/* ---- layout and drawing -------------------------------------------------------------------------- */
void rolltui_input_set_options(RolltuiInput* in, const RolltuiInputOptions* o);
const RolltuiInputOptions* rolltui_input_options(const RolltuiInput* in);
int rolltui_input_rows_for(const RolltuiInput* in, int width);
int rolltui_input_rows(const RolltuiInput* in);
void rolltui_input_layout(RolltuiInput* in, RolltuiRect area);
int rolltui_input_top_row(const RolltuiInput* in);
void rolltui_input_area(const RolltuiInput* in, RolltuiRect* out);
/* Where a position is drawn: a text row (before scrolling) and a column from the area's left
 * edge, the prompt / indent included. */
void rolltui_input_cell_of(const RolltuiInput* in, size_t offset, int* row, int* col);
/* The grapheme under a screen cell as [begin, end). 0 only before any layout(). */
int rolltui_input_hit(const RolltuiInput* in, int x, int y, size_t* begin, size_t* end);

/* THE FOUR ROLES A DRAW NEEDS, handed in as bytes. `prompt` is the OPTIONS' role, which is
 * why it is not in here. */
typedef struct RolltuiInputRoles {
  unsigned char text;
  unsigned char selection;
  unsigned char placeholder;
} RolltuiInputRoles;

void rolltui_input_draw(const RolltuiInput* in, RolltuiFrame* f, RolltuiDrawScratch* draw,
                        const RolltuiStyle* styles, const RolltuiInputRoles* roles, int focused);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
/* `RolltuiInputOptions`' one special member (Phase 17 m3): the default prompt, which is a
 * VALUE the struct must start with and not something a caller should have to know. It was
 * out-of-line in `rolltui/Input.cpp` for the reason `RolltuiActionList`'s were — an inline body
 * inside the struct cannot see `rolltui_str_set` yet — and, like those, it is not part of the
 * deleted binding but part of what makes the C++ type BE the C struct. */
inline RolltuiInputOptions::RolltuiInputOptions() { rolltui_str_set(&prompt, "> ", 2); }
#endif /* __cplusplus */

#endif /* ROLLTUI_C_INPUT_H */
