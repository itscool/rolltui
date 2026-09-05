#ifndef ROLLTUI_C_WIDGET_KINDS_H
#define ROLLTUI_C_WIDGET_KINDS_H
/*
 * rolltui/c/rolltui_widget_kinds.h — THE LIBRARY'S OWN WIDGET KINDS, IN C (Phase 15/17).
 *
 * `rows`, `text`, `file`, `help`, `input`, `transcript` and `menu`, and the error/panel
 * fallbacks, fill the plugin contract (`rolltui/c/rolltui_widgets.h`) here, in real C11,
 * calling only the already-C engines (`rolltui_input.h`, `rolltui_transcript.h`,
 * `rolltui_menu.h`, `rolltui_wrap.h`, `rolltui_frame_ops.h`, `rolltui_bindings.h`,
 * `rolltui_marker.h`, `rolltui_embedded.h`) — never `rolltui::Transcript`/`Input`/`Menu`/
 * `Theme`/`Bindings`. Every rule these kinds obey (the input's auto-sizing and note
 * placement, the `file:`/`help` re-read and scope rules, the never-blank error panel, the
 * transcript's scroll-by-anchor and the menu's three-rung file resolution) is stated in
 * `rolltui/Widgets.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it is
 * repeated here.
 *
 * ---- WHY `transcript` AND `menu` ARE HERE TOO, AND WHAT STILL ISN'T (Phase 17 m1c) --------
 *
 * Through m1b these two stayed the C++ `Widget` subclasses in `Widgets.cpp`, on the ground
 * that `Windows::transcript(source)`/`.menu(source)` — public, host-facing, called directly
 * by both hosts to drive scrolling, selection and navigation, not only by the window that
 * draws them — must hand back a LIVE `rolltui::Transcript&`/`Menu&` backed by the SAME state
 * the window draws, and neither class had a way to lend a C plugin that same state without
 * constructing a second, DIVERGENT one. `Transcript::handle()`/`Menu::handle()` (BORROWS of
 * their `RolltuiTranscript*`/`RolltuiMenu*`, added 2026-09-05 mirroring `Input::handle()`)
 * close that gap exactly as `Input::handle()` already had: `Windows` owns the `Transcript`/
 * `Menu` object (in its own `transcripts_`/`menus_` maps, the same shape as `inputs_`), and
 * this file's ctx only BORROWS the handle, so `windows.transcript("main")` and the drawn
 * widget are one object.
 *
 * CONSTRUCTION still crosses from Widgets.cpp, for the same reason `input`'s does: building
 * a `Transcript`/`Menu` needs `Windows::transcripts_`/`menus_`, C++ maps this file never
 * sees. Every PER-FRAME call (layout/draw/handle/problem/note_at/scroll_extent/scroll_to) is
 * this file's own, over the already-C `rolltui_transcript.h`/`rolltui_menu.h` engines. What
 * stays C++ and is asked for rather than moved: the transcript's syntax HIGHLIGHTER (a
 * `std::function`, pushed straight onto the `RolltuiTranscript` by `Windows::set_highlighter`/
 * `transcript()` — no epoch to poll here, because the ctx never touches it); the menu's text
 * VALIDATORS (`rolltui::Menu`'s own registry, asked through `rolltui_menu_set_validator_fn`
 * exactly as before); and `apply_shortcuts`'s tree walk, re-derived here in C over
 * `rolltui_bindings_has`/`rolltui_menu_list_*` — the same one-file duplication `help`'s own
 * chord-joining loop below already is, not a new kind of trade.
 *
 * ---- INTERNAL: NOT PART OF THE PUBLIC API -------------------------------------------------
 *
 * `rolltui/rolltui.h` does not include this header and `rolltui/tests/public_header_test.cpp`
 * does not name it — the same footing as `rolltui_alloc.h`/`rolltui_map.h`/`rolltui_marker.h`.
 * Nobody outside this library ever constructs a `RowsWidget` or a `TextWidget` by name;
 * `rolltui::Windows` builds them from the kind table when a layout names `rows:status` or
 * `text:...`. A consumer binds data (`bind_rows`, `bind_document`, `bind_submit`, `bind_note`)
 * and lets the layout name the kind — exactly as `WidgetKind` (public, in `rolltui_layout.h`)
 * and the plugin contract (public, in `rolltui_widgets.h`) already say.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* THE ROLE BYTES these kinds draw with, handed over ONCE at registration — this file names
 * no role, the same rule every other C header in this port states for itself. Role ordinals
 * are process-wide constants (`rolltui/Style.hpp`), so a value copied in at construction
 * never goes stale. */
typedef struct RolltuiBuiltinRoles {
  unsigned char text, text_muted, error, scroll_marker, label, value;
  unsigned char input_text, input_selection, input_placeholder;
} RolltuiBuiltinRoles;

/* THE SIX ACTION NAMES the transcript SCOPE's scroll keys use ("this file knows the rule and
 * none of the words" — the same trade `rolltui_transcript.h`'s `RolltuiTranscriptActions`
 * makes). `rolltui::scroll_by_action` (Widgets.hpp, unchanged and still used by two hosts
 * directly) hardcodes the identical six strings; this is the same one-file duplication
 * `Input.cpp`'s `kActions` and `Transcript.cpp`'s own action table already are, not a new
 * one — the alternative (an action name crossing the C boundary) is what `rolltui_bindings.h`
 * says not to do. */
typedef struct RolltuiScrollTextActions {
  const char *line_up, *line_down, *page_up, *page_down, *top, *bottom;
} RolltuiScrollTextActions;

/* `w` keeps its own copy of both — set once, read by every kind below through `ctx = w`
 * (rule 5: the library's own kinds are context the same way a host's are). The alternative,
 * a small heap block per kind holding a copy, would leak for `error`/`panel`: `rolltui_
 * windows_set_error_factory`/`set_panel_factory` take no `free_ctx`, because their `ctx` was
 * always `Windows` itself and never this table's to release. */
void rolltui_windows_set_builtin_roles(RolltuiWindows* w, const RolltuiBuiltinRoles* r);
const RolltuiBuiltinRoles* rolltui_windows_builtin_roles(const RolltuiWindows* w);
void rolltui_windows_set_scroll_text_actions(RolltuiWindows* w, const RolltuiScrollTextActions* a);
const RolltuiScrollTextActions* rolltui_windows_scroll_text_actions(const RolltuiWindows* w);

/* Registers `rows`, `text`, `file`, `help` and the error/panel fallbacks — every built-in
 * kind that needs nothing from `Windows`' own C++ members. Called once, at construction,
 * AFTER the two calls above (each kind reads the roles/actions back through `w`, not through
 * its own `ctx`, which is `w` itself for all six). */
void rolltui_widget_kinds_register(RolltuiWindows* w);

/* ---- input: the one built-in kind whose FACTORY needs `Windows::inputs_` (a C++ map this
 * file never sees) — so CONSTRUCTION crosses here from Widgets.cpp, and only construction;
 * every per-frame call (layout/draw/handle) is this file's own, on the ctx below. --------- */

const RolltuiWidgetPlugin* rolltui_input_widget_plugin(void);

/* `ed` is BORROWED — `Windows`' own `Input` map owns it and outlives every widget content
 * keys into it. `actions` is BORROWED, valid for the process (`rolltui::input_actions()`'s
 * own `constexpr` table, handed across the one C++/C call this needs). `source` is copied. */
void* rolltui_input_widget_ctx_new(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                    const RolltuiBuiltinRoles* roles, const RolltuiInputActions* actions);
/* `ctx` must be one this function built; asserts otherwise. A host's floor regardless of
 * what the text says (roll holds the prompt as tall as the modal placed over it). */
void rolltui_input_widget_ctx_set_min_outer(void* ctx, int rows);

/* Shared by the input plugin's own `handle` slot and by `Windows::input_event` (a host asking
 * for the action back): handle `e` against the live bindings, and on Submit either
 * clear-and-push-history or keep the text (`rolltui_windows_on_submit` says which), then call
 * whatever is bound to `source`. Returns one of ROLLTUI_INPUT_IGNORED/HANDLED/SUBMIT/EOF. */
int rolltui_input_kind_process_event(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                      const RolltuiInputActions* actions, const RolltuiEvent* e);

/* ---- transcript/menu-shared: the config `Windows` carries for them (Phase 17 m1c), handed
 * over ONCE and read back through `w` the same way `RolltuiBuiltinRoles`/
 * `RolltuiScrollTextActions` above already are. --------------------------------------------- */

/* The two ints a transcript's code-block folding needs — `Windows::set_code_fold`'s own,
 * mirrored to the boundary so the transcript kind below can read them at layout time. */
typedef struct RolltuiCodeFold {
  int fold_over_lines, cap_lines;
} RolltuiCodeFold;
void rolltui_windows_set_code_fold(RolltuiWindows* w, const RolltuiCodeFold* c);
const RolltuiCodeFold* rolltui_windows_code_fold(const RolltuiWindows* w);

/* The eleven action names `rolltui_transcript_handle` needs. */
void rolltui_windows_set_transcript_actions(RolltuiWindows* w, const RolltuiTranscriptActions* a);
const RolltuiTranscriptActions* rolltui_windows_transcript_actions(const RolltuiWindows* w);

/* The seven roles a menu draw needs. Unlike a transcript's (baked into the `RolltuiTranscript`
 * once, at construction, by `rolltui_transcript_set_roles`), `rolltui_menu_draw` takes them as
 * a per-call parameter, so the menu kind below reads them back through `w` on every draw. */
void rolltui_windows_set_menu_roles(RolltuiWindows* w, const RolltuiMenuRoles* r);
const RolltuiMenuRoles* rolltui_windows_menu_roles(const RolltuiWindows* w);

/* ---- transcript: BORROWS the RolltuiTranscript* `Windows::transcripts_` (a C++ map) owns,
 * exactly as `input` borrows its editor's handle above — construction crosses from
 * Widgets.cpp for the same reason input's does (the map is a C++ member this file never
 * sees); every per-frame call is this file's own. -------------------------------------------- */
void* rolltui_transcript_widget_ctx_new(RolltuiTranscript* t, RolltuiWindows* w, const char* source,
                                        size_t source_len);
const RolltuiWidgetPlugin* rolltui_transcript_widget_plugin(void);

/* ---- menu: BORROWS the RolltuiMenu* `Windows::menus_` owns, same shape. -------------------- */
void* rolltui_menu_widget_ctx_new(RolltuiMenu* m, RolltuiWindows* w, const char* source, size_t source_len);
const RolltuiWidgetPlugin* rolltui_menu_widget_plugin(void);
/* Re-resolves the menu FILE if its rung or mtime changed — a no-op otherwise (a stat() and a
 * string compare). `Windows::menu(source)` calls this directly through the ctx
 * `rolltui_windows_widget_for` returns, so asking for the `Menu&` before any window has ever
 * shown it still reads the file NOW rather than at the next draw — the same guarantee the
 * file/problem/note_at/layout/scroll_extent slots below already give each other by all
 * calling it themselves. */
void rolltui_menu_widget_ctx_refresh(void* ctx);
/* Which rung answered: "" | a path | "the host's" | "a shipped menu" — mirrors
 * `rolltui::Menu`'s own (removed) origin() accessor. Refreshes first, then a BORROW valid
 * until the ctx's next refresh. */
const char* rolltui_menu_widget_ctx_origin(void* ctx, size_t* len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WIDGET_KINDS_H */
