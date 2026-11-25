/* rolltui/c/rolltui_texts.h — the words the library puts on screen, by id, in the person's language when a texts file gives them
 * (`rolltui_texts_load`) and in English otherwise. INTERNAL; the loader and the template are in rolltui.h. */
#ifndef ROLLTUI_C_TEXTS_H
#define ROLLTUI_C_TEXTS_H

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EVERY TEXT THE LIBRARY DRAWS, its id in a texts file (`text.` and a name, apart from the actions' names) and its English. `{1}`
 * and `{2}` are what the text names (a source, a count), which a translation may move. An action's description is a text too, by the action's own name; those are the bindings', not here. */
#define ROLLTUI_TEXT_LIST(X)                                                                                                          \
  X(NOTHING_BOUND, "text.window.nothing_bound", "nothing is bound to '{1}'")                                                              \
  X(CANNOT_READ, "text.window.cannot_read", "cannot read '{1}'")                                                                          \
  X(UNBOUND, "text.keys.unbound", "(unbound)")                                                                                            \
  X(NO_MESSAGES, "text.messages.none", "no messages yet")                                                                                 \
  X(FOLLOWING, "text.output.following", "following")                                                                                      \
  X(PAUSED, "text.output.paused", "paused \xc2\xb7 {1} below")                                                                            \
  X(RUNNING, "text.output.running", "running")                                                                                        \
  X(STOPPED, "text.output.stopped", "stopped")                                                                                        \
  X(EXITED, "text.output.exited", "exited {1}")                                                                                       \
  X(EMPTY, "text.list.empty", "(empty)")                                                                                                  \
  X(EMPTY_FILE, "text.preview.empty_file", "(empty file)")                                                                                \
  X(NO_MATCH, "text.menu.no_match", "(no match for /{1})")                                                                                \
  X(TOO_SMALL, "text.screen.too_small", "the screen is {1}; this needs {2}")                                                              \
  X(LOCALE_NOT_UTF8, "text.locale.not_utf8",                                                                                              \
    "The locale isn't UTF-8 ({1}), so the terminal may not show some characters. Set {2} to a UTF-8 locale such as en_US.UTF-8.")      \
  X(LOCALE_NONE, "text.locale.none",                                                                                                      \
    "No locale is set (LANG, LC_CTYPE and LC_ALL are empty), so the terminal may not show some characters. Set LANG to a UTF-8 "     \
    "locale such as en_US.UTF-8.")                                                                                                   \
  X(COMPLETIONS, "text.input.completions", "{1} of {2}")                                                                        \
  X(NOT_A_KEY, "text.keys.not_a_key", "it is not a key")                                                                         \
  X(SHIFT_ON_CHAR, "text.keys.shift_on_char",                                                                                    \
    "shift on a character key other than a letter is the shifted character itself, which no terminal reports as a chord")        \
  X(NEEDS_KITTY_OR_MOK, "text.keys.needs_kitty_or_modify_other_keys", "it needs the kitty keyboard protocol or xterm's modifyOtherKeys") \
  X(NEEDS_KITTY, "text.keys.needs_kitty", "it needs the kitty keyboard protocol")                                                \
  X(NO_PROTOCOL, "text.keys.no_protocol", "no keyboard protocol this library speaks can report it")

typedef enum RolltuiTextId {
#define ROLLTUI_TEXT_ENUM_(sym, id, en) ROLLTUI_TEXT_##sym,
  ROLLTUI_TEXT_LIST(ROLLTUI_TEXT_ENUM_)
#undef ROLLTUI_TEXT_ENUM_
      ROLLTUI_TEXT_COUNT
} RolltuiTextId;

/* The text in the person's language, else its English. A BORROW, valid until the next `rolltui_texts_load` or `_clear`. */
const char* rolltui_text(int id, size_t* len);
/* The text with `{1}` and `{2}` put in, APPENDED to `out`. */
void rolltui_text_format(RolltuiStr* out, int id, const char* a1, size_t n1, const char* a2, size_t n2);
/* A text by its id in a texts file (an action's description is its name), or NULL when the file gives none. */
const char* rolltui_text_named(const char* id, size_t len, size_t* out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_TEXTS_H */
