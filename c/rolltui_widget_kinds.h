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
 * ---- NOTHING'S CONSTRUCTION CROSSES ANY MORE (Phase 17 m1c, 2026-09-05) -------------------
 *
 * Through the first half of m1c all eight kinds were C, and three of them — `input`,
 * `transcript`, `menu` — still had their FACTORY in `Widgets.cpp`, because building one meant
 * reaching `Windows::inputs_`/`transcripts_`/`menus_`, C++ maps this file could not see. That
 * left those kinds PORTED AND NOT REACHABLE: a pure-C host could draw an `input:prompt` window
 * and had no way to get the `RolltuiInput*` behind it, because the object was owned on the
 * other side of the boundary. Accessors alone would have made a SECOND owner (see
 * `rolltui_widgets.h`'s own note), so the three maps moved into `RolltuiWindows` and these
 * three factories were repointed at them in ONE change. They are ordinary static factories
 * here now, registered by `rolltui_widget_kinds_register` beside `rows`/`text`/`file`/`help`,
 * and each asks the table for its source's object exactly as a host does
 * (`rolltui_windows_input`/`_transcript`/`_menu`). One owner, one table, one lookup.
 *
 * What is ASKED FOR rather than moved, and stays a host's: the transcript's syntax HIGHLIGHTER
 * (a `std::function` on the C++ side, crossing as {fn, ctx} through
 * `rolltui_windows_set_highlight` and pushed by the table onto every transcript it owns — no
 * epoch to poll, because this file never touches it); the menu's text VALIDATORS
 * (`rolltui_menu_set_validator_fn`, unset for a table-owned menu, which reads as "no validator
 * of that name" exactly as the empty registry it replaces did); and `apply_shortcuts`'s tree
 * walk, re-derived here in C over `rolltui_bindings_has`/`rolltui_menu_list_*` — the same
 * one-file duplication `help`'s own chord-joining loop below already is, not a new trade.
 *
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

/* THE THIRTY ACTION NAMES the `input` kind's own keys use, carried the same way — BORROWED,
 * valid for the process (`rolltui::input_actions()`'s `constexpr` table today). It joins the
 * two above because the `input` FACTORY is this file's now: there is no caller left in the
 * middle to hand them down per construction. */
void rolltui_windows_set_input_actions(RolltuiWindows* w, const RolltuiInputActions* a);
const RolltuiInputActions* rolltui_windows_input_actions(const RolltuiWindows* w);

/* Registers ALL EIGHT built-in kinds — `rows`, `text`, `file`, `help`, `input`, `transcript`,
 * `menu` — and the error/panel fallbacks. Called once, at construction, AFTER every setter in
 * this file: each kind reads the roles and action names back through `w`, not through its own
 * `ctx`, which is `w` itself for all of them. */
void rolltui_widget_kinds_register(RolltuiWindows* w);

/* ---- input: the one slot of a built-in kind's ctx a caller still reaches by hand — a host's
 * floor on the window's height regardless of what the text says (roll holds the prompt as tall
 * as the modal placed over it). `ctx` must be one the `input` factory built; a host reaches it
 * by NAME through `rolltui_windows_set_input_min_outer`, which is the call to make. --------- */
void rolltui_input_widget_ctx_set_min_outer(void* ctx, int rows);
const RolltuiWidgetPlugin* rolltui_input_widget_plugin(void);

/* Shared by the input plugin's own `handle` slot and by a host asking for the action back
 * (`Windows::input_event`): handle `e` against the live bindings, and on Submit either
 * clear-and-push-history or keep the text (`rolltui_windows_on_submit` says which), then call
 * whatever is bound to `source`. Returns one of ROLLTUI_INPUT_IGNORED/HANDLED/SUBMIT/EOF. */
int rolltui_input_kind_process_event(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                      const RolltuiEvent* e);

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

/* ---- menu: the FILE-resolution half of its ctx, which `rolltui_windows_menu`/`_menu_origin`
 * drive from `rolltui_widgets.c`. The `RolltuiMenu` itself belongs to the window table (one per
 * source, created on demand); what the menu object alone cannot do is re-resolve its file, and
 * that state — loaded/stamp/origin/problem — is this ctx's. -------------------------------- */
const RolltuiWidgetPlugin* rolltui_menu_widget_plugin(void);
/* Re-resolves the menu FILE if its rung or mtime changed — a no-op otherwise (a stat() and a
 * string compare). `rolltui_windows_menu(source)` calls this through the ctx
 * `rolltui_windows_widget_for` returns, so asking for the menu before any window has ever shown
 * it still reads the file NOW rather than at the next draw — the same guarantee the
 * file/problem/note_at/layout/scroll_extent slots give each other by all calling it. */
void rolltui_menu_widget_ctx_refresh(void* ctx);
/* Which rung answered: "" | a path | "the host's" | "a shipped menu". Refreshes first, then a
 * BORROW valid until the ctx's next refresh. */
const char* rolltui_menu_widget_ctx_origin(void* ctx, size_t* len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WIDGET_KINDS_H */
