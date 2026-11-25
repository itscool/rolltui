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
  X(NO_PROTOCOL, "text.keys.no_protocol", "no keyboard protocol this library speaks can report it") \
  X(ED_UNDO, "text.editor.undo", "Undo") \
  X(ED_REDO, "text.editor.redo", "Redo") \
  X(ED_LOADED, "text.editor.loaded", "loaded") \
  X(ED_UNDONE, "text.editor.undone", "undone") \
  X(ED_REDONE, "text.editor.redone", "redone") \
  X(ED_NOTHING_TO_UNDO, "text.editor.nothing_to_undo", "nothing to undo") \
  X(ED_NOTHING_TO_REDO, "text.editor.nothing_to_redo", "nothing to redo") \
  X(ED_REFUSED, "text.editor.refused", "refused: ") \
  X(ED_WRITE_SHIPPED, "text.editor.write_shipped", "Write a SHIPPED preset (the editor's privilege)") \
  X(ED_RESET_LOADED, "text.editor.reset_loaded", "Reset to the loaded preset\xE2\x80\xA6") \
  X(ED_SAVED_PRESET, "text.editor.saved_preset", "saved preset '{1}'") \
  X(ED_EXISTS, "text.editor.exists", "'{1}' already exists \xE2\x80\x94 choose another name") \
  X(ED_CANNOT_SAVE, "text.editor.cannot_save", "cannot save: ") \
  X(ED_LOADED_PRESET, "text.editor.loaded_preset", "loaded '{1}'") \
  X(ED_CANNOT_LOAD, "text.editor.cannot_load", "cannot load '{1}'") \
  X(ED_RESET_TO_LOADED, "text.editor.reset_to_loaded", "reset to the loaded preset") \
  X(ED_RESET_TO_LOADED_UNDOABLE, "text.editor.reset_to_loaded_undoable", "reset to the loaded preset (undoable)") \
  X(ED_LOADED_UNREADABLE, "text.editor.loaded_unreadable", "the loaded preset is no longer readable") \
  X(ED_WROTE_SHIPPED, "text.editor.wrote_shipped", "wrote shipped '{1}'") \
  X(ED_REFUSED_WHY, "text.editor.refused_why", "refused: {1}") \
  X(ED_SAVE_AS, "text.editor.save_as", "save as: ") \
  X(ED_UNDO_REDO, "text.editor.undo_redo", "undo {1} \xC2\xB7 redo {2}") \
  X(TH_SEC_EDIT, "text.theme.section_edit", "Edit \xE2\x80\x94 applies live, everywhere") \
  X(TH_ROLES, "text.theme.roles", "Roles") \
  X(TH_MODE, "text.theme.mode", "Preview variant (dark/light)") \
  X(TH_CHECK, "text.theme.check", "Check: contrast, colour-vision, badges") \
  X(TH_FIXES, "text.theme.fixes", "Fixes (proposals; Enter applies one, undoable)") \
  X(TH_RULESET, "text.theme.ruleset", "Ruleset") \
  X(TH_SEED, "text.theme.seed", "Seed") \
  X(TH_CHAOS, "text.theme.chaos", "Chaos") \
  X(TH_GEN_RUN, "text.theme.generate_run", "Generate (replaces both variants, undoable)") \
  X(TH_GENERATE, "text.theme.generate", "Generate a theme (seeded)") \
  X(TH_SEC_HISTORY, "text.theme.section_history", "History") \
  X(TH_SEC_FILE, "text.theme.section_file", "File") \
  X(TH_LOAD, "text.theme.load", "Load theme") \
  X(TH_SAVE, "text.theme.save", "Save theme as") \
  X(TH_RESET_BUILTIN, "text.theme.reset_builtin", "Reset to the built-in default\xE2\x80\xA6") \
  X(TH_NOTHING_TO_FIX, "text.theme.nothing_to_fix", "(nothing to fix in this variant)") \
  X(TH_BADGES, "text.theme.badges", "badges:") \
  X(TH_PREVIEWING, "text.theme.previewing", "previewing \xE2\x80\x94 Enter commits, Esc cancels") \
  X(TH_ENTER_COMMITS, "text.theme.enter_commits", "Enter commits, Esc cancels") \
  X(TH_PICK_RULESET, "text.theme.pick_ruleset", "pick a ruleset first") \
  X(TH_GENERATED, "text.theme.generated", "generated ") \
  X(TH_APPLIED, "text.theme.applied", "applied: ") \
  X(TH_RESET_TO_BUILTIN, "text.theme.reset_to_builtin", "reset to the built-in default (undoable)") \
  X(TH_DROPPED, "text.theme.dropped", "dropped") \
  X(TH_EDITING_LIGHT, "text.theme.editing_light", "editing the light variant") \
  X(TH_EDITING_DARK, "text.theme.editing_dark", "editing the dark variant") \
  X(TH_MADE, "text.theme.made", "made {1} from seed {2}; Ctrl-Z goes back") \
  X(TH_FIXED, "text.theme.fixed", "fixed; Ctrl-Z puts it back") \
  X(TH_NOTHING_TO_FIX_ROW, "text.theme.nothing_to_fix_row", "nothing to fix on this row") \
  X(TH_LOAD_HINT, "text.theme.load_hint", "load a theme: Enter loads, Esc goes back") \
  X(TH_KEYS_COLOURS, "text.theme.keys_colours", "Enter keeps  Esc drops  Left Right fields  Up Down the theme's colours") \
  X(TH_KEYS_TOGGLES, "text.theme.keys_toggles", "Enter keeps  Esc drops  Left Right fields  Space on, off") \
  X(TH_ATTRIBUTES, "text.theme.attributes", "attributes") \
  X(TH_CUSTOM_FG, "text.theme.custom_fg", "custom fg") \
  X(TH_CUSTOM_BG, "text.theme.custom_bg", "custom bg") \
  X(TH_TEXT_COLOUR, "text.theme.text_colour", "the text colour") \
  X(TH_GROUND_COLOUR, "text.theme.ground_colour", "the ground colour") \
  X(TH_SEC_TEXT, "text.theme.section.text", "Text & backgrounds") \
  X(TH_SEC_BORDER, "text.theme.section.border", "Borders & labels") \
  X(TH_SEC_ACCENT, "text.theme.section.accent", "Accents & status") \
  X(TH_SEC_MD, "text.theme.section.markdown", "Markdown") \
  X(TH_SEC_DIFF, "text.theme.section.diff", "Diff") \
  X(TH_SEC_INPUT, "text.theme.section.input", "Input & selection") \
  X(TH_SEC_MENU, "text.theme.section.menu", "Menus, find & scrollbar") \
  X(KE_ACTIONS_BY_SCOPE, "text.keys.actions_by_scope", "Actions by scope") \
  X(KE_LOAD, "text.keys.load", "Load keys") \
  X(KE_SAVE, "text.keys.save", "Save keys as") \
  X(KE_REMOVE, "text.keys.remove", "remove ") \
  X(KE_PRESS_FOR, "text.keys.press_for", "press the chord for {1} (Esc cancels)") \
  X(KE_ENTER_ON_ACTION, "text.keys.enter_on_action", "Enter on an action: add, remove or clear its chords") \
  X(KE_CAPTURE_CANCELLED, "text.keys.capture_cancelled", "capture cancelled") \
  X(KE_NO_CHORD_NAME, "text.keys.no_chord_name", "that key has no chord name; try another") \
  X(KE_BOUND, "text.keys.bound", "bound ") \
  X(KE_REMOVED, "text.keys.removed", "removed ") \
  X(KE_SUBMIT_KEEPS_ENTER, "text.keys.submit_keeps_enter", "input.submit keeps Enter") \
  X(KE_CLEARED, "text.keys.cleared", "cleared ") \
  X(KE_ABOUT, "text.keys.about", "About") \
  X(KE_PRESS_A_KEY, "text.keys.press_a_key", "press a key \xE2\x80\xA6") \
  X(KE_NO_KEY, "text.keys.no_key", "no key") \
  X(KE_LOAD_HINT, "text.keys.load_hint", "load a set of keys: Enter loads, Esc goes back") \
  X(KE_PRESS_TO_ADD, "text.keys.press_to_add", "press the key to add; Esc cancels") \
  X(KE_KEEPS_NO_CHANGES, "text.keys.keeps_no_changes", "this program keeps no changes to its keys") \
  X(KE_NOTHING_TO_REMOVE, "text.keys.nothing_to_remove", "it has no key to remove") \
  X(KE_NO_KEY_ADDED, "text.keys.no_key_added", "no key added") \
  X(KE_WHAT_IT_DOES, "text.keys.what_it_does", "what it does") \
  X(KE_SC_APP, "text.keys.scope.app", "This program") \
  X(KE_SC_INPUT, "text.keys.scope.input", "Typing") \
  X(KE_SC_LIST, "text.keys.scope.list", "Lists and tables") \
  X(KE_SC_MENU, "text.keys.scope.menu", "Menus") \
  X(KE_SC_PICKER, "text.keys.scope.picker", "Files") \
  X(KE_SC_TRANSCRIPT, "text.keys.scope.transcript", "Reading") \
  X(KE_SC_STACK, "text.keys.scope.stack", "Windows") \
  X(KE_SC_EDIT, "text.keys.scope.edit", "Editing a value") \
  X(KE_SC_SHEET, "text.keys.scope.sheet", "Editing a sheet") \
  X(KE_SC_EDITOR, "text.keys.scope.editor", "The editor") \
  X(KE_SC_STUDIO, "text.keys.scope.studio", "The studio") \
  X(TH_ATTR_BOLD, "text.theme.attribute.bold", "bold") \
  X(TH_ATTR_ITALIC, "text.theme.attribute.italic", "italic") \
  X(TH_ATTR_UNDERLINE, "text.theme.attribute.underline", "underline") \
  X(TH_ATTR_DIM, "text.theme.attribute.dim", "dim") \
  X(TH_ATTR_REVERSE, "text.theme.attribute.reverse", "reverse") \
  X(TH_ATTR_STRIKE, "text.theme.attribute.strikethrough", "strikethrough") \
  X(TH_LIGHT, "text.theme.light", "light") \
  X(TH_DARK, "text.theme.dark", "dark") \
  X(TH_EDITOR, "text.theme.editor", "theme editor") \
  X(KE_EDITOR, "text.keys.editor", "keys editor") \
  X(KE_ADD_CHORD, "text.keys.add_chord", "add a chord (press it)\xE2\x80\xA6") \
  X(KE_CLEAR_EVERY, "text.keys.clear_every", "clear every chord") \
  X(TH_TITLE, "text.theme.title", "theme") \
  X(KE_TITLE, "text.keys.title", "keys") \
  X(TH_NO_BADGES, "text.theme.no_badges", "no badges") \
  X(TH_BROKEN, "text.theme.broken", "broken: ") \
  X(TH_NONE, "text.theme.none", "(none)") \
  X(TH_TYPE_COLOUR, "text.theme.type_colour", "{1}: type #rrggbb, 0-255 or none") \
  X(TH_ATTR_ON, "text.theme.attribute_on", "{1}: on, Space turns it off") \
  X(TH_ATTR_OFF, "text.theme.attribute_off", "{1}: off, Space turns it on") \
  X(TH_PROBLEM, "text.theme.problem", "{1} problem") \
  X(TH_PROBLEMS, "text.theme.problems", "{1} problems") \
  X(TH_FIXES_IT, "text.theme.fixes_it", "{1} fixes it") \
  X(KE_CANNOT_BE_SENT, "text.keys.cannot_be_sent", "{1} cannot be sent by this terminal") \
  X(KE_NOT_A_SCOPE, "text.keys.not_a_scope", "'{1}' is not one of this program's key scopes") \
  X(MV_NO_NEGATIVES, "text.menu.value.no_negatives", "no negatives ({1})") \
  X(MV_WHOLE_NUMBER, "text.menu.value.whole_number", "a whole number ({1})") \
  X(MV_NUMBER, "text.menu.value.number", "a number ({1})") \
  X(MV_COLOUR_START, "text.menu.value.colour_start", "not the start of a colour ({1})") \
  X(MV_COLOUR_NEEDED, "text.menu.value.colour_needed", "a colour is needed ({1})") \
  X(MV_COLOUR_YET, "text.menu.value.colour_yet", "not a colour yet ({1})") \
  X(MV_SIZE_START, "text.menu.value.size_start", "not the start of a size ({1})") \
  X(MV_SIZE_NEEDED, "text.menu.value.size_needed", "a size is needed ({1})") \
  X(MV_SIZE_YET, "text.menu.value.size_yet", "not a size yet ({1})") \
  X(MV_DIM_START, "text.menu.value.dim_start", "not the start of a dim ({1})") \
  X(MV_DIM_NEEDED, "text.menu.value.dim_needed", "a dim is needed ({1})") \
  X(MV_DIM_YET, "text.menu.value.dim_yet", "not a dim yet ({1})") \
  X(MV_NAME_DOT, "text.menu.value.name_dot", "a name cannot start with a dot ({1})") \
  X(MV_ONLY_DIGITS, "text.menu.value.only_digits", "only digits ({1})") \
  X(MV_ONLY_DIGITS_SIGNED, "text.menu.value.only_digits_signed", "only digits and a leading '-' ({1})") \
  X(MV_ONLY_DIGITS_POINT, "text.menu.value.only_digits_point", "only digits, one '.' ({1})") \
  X(MV_ONLY_DIGITS_POINT_SIGNED, "text.menu.value.only_digits_point_signed", "only digits, one '.' and a leading '-' ({1})") \
  X(MV_NOTHING_FITS, "text.menu.value.nothing_fits", "nothing starting with '{1}' fits {2}") \
  X(MV_WHOLE_NUMBER_IN, "text.menu.value.whole_number_in", "a whole number {1}") \
  X(MV_NUMBER_IN, "text.menu.value.number_in", "a number {1}") \
  X(MV_AT_MOST_CHARS, "text.menu.value.at_most_chars", "at most {1} characters") \
  X(MV_AT_MOST_DIGITS, "text.menu.value.at_most_digits", "at most {1} digits after the point") \
  X(MV_ONLY, "text.menu.value.only", "only {1}") \
  X(MV_NAME_NEEDED, "text.menu.value.name_needed", "a name is needed") \
  X(MV_VALUE_NEEDED, "text.menu.value.value_needed", "a value is needed") \
  X(MV_NAME_HINT, "text.menu.value.name_hint", "a name: letters, digits, - _ .") \
  X(MV_UP_TO_CHARS, "text.menu.value.up_to_chars", "up to {1} characters") \
  X(MV_DIGITS, "text.menu.value.digits", "{1} digits") \
  X(MV_AT_LEAST_CHARS, "text.menu.value.at_least_chars", "at least {1} characters") \
  X(TYPE_TO_CONFIRM, "text.dialog.type_to_confirm", "Type {1} to confirm") \
  X(NO_DIALOG, "text.dialog.none", "no dialog is asked under '{1}'")

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
