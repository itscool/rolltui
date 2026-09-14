#ifndef ROLLTUI_C_WIDGETS_H
#define ROLLTUI_C_WIDGETS_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_widgets.h — THE WIDGET VTABLE AND THE WINDOW HOST.
 *
 * The rules — one widget per CONTENT, a window is never blank, focus and routing are the
 * stack's, a widget may size its window, the scroll's two optional halves — are stated in
 * `rolltui/Widgets.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of them is
 * repeated here.
 *
 * ============================================================================================
 * THE PLUGIN, AND THE RULE FOR ADDING TO IT
 * ============================================================================================
 *
 * `rolltui::Widget` is the one place the library uses inheritance for real: nine virtuals a
 * host overrides. In C that is a STRUCT OF FUNCTION POINTERS the implementor fills — a PLUGIN
 * CONTRACT: the set of functions a widget supplies so a window can drive it, never a service
 * the widget asks of the window. It is what `register_kind` already was in spirit, since a
 * host's kind was always a factory producing something the library would only ever call
 * through those nine slots.
 *
 * **Making it a struct changes one thing that matters, and it is not the calling convention:
 * a plugin you can SEE is a set you have to CLOSE.** A virtual function is added by typing
 * one line in a header and costs nothing at the moment of writing; every existing override
 * silently keeps the base's behaviour, and nobody is asked what that behaviour should BE for
 * a widget that has never heard of the new question. `Widget` grew from four virtuals to nine
 * that way across four phases. So:
 *
 *   1. **A SLOT IS A QUESTION THE WINDOW ASKS ITS WIDGET, NEVER A SERVICE THE WIDGET ASKS OF
 *      THE WINDOW.** Services go the other way — a widget reaches the host's bindings through
 *      the environment it was built with, and nothing it needs belongs here. The test is
 *      whether the WINDOW can be written without knowing which kind answered.
 *
 *   2. **THREE SLOTS ARE REQUIRED AND THE REST ARE OPTIONAL, and NULL means a DEFAULT THIS
 *      FILE STATES** — never "undefined", never "the caller decides". `destroy`, `layout` and
 *      `draw` are required because a widget that cannot be freed, placed or drawn is not a
 *      widget. Every other slot documents, on its own line, exactly what NULL does.
 *
 *   3. **A NEW SLOT IS ADDED HERE, WITH ITS NULL BEHAVIOUR, OR IT IS NOT ADDED.** That is the
 *      whole difference from a virtual: the set is enumerable, so "what does a widget that
 *      does not care do?" has to be answered once, in writing, before the first caller exists.
 *
 *   4. **A CAPABILITY SOME WIDGETS HAVE AND OTHERS DO NOT IS TWO SLOTS, NOT ONE WITH A FLAG.**
 *      `scroll_extent` REPORTS and `scroll_to` ACCEPTS, because a menu's scroll is derived
 *      from its selection: it wants an accurate bar that is not a handle. Collapsing them
 *      would force every widget into a behaviour only some of them want. The shape enforces
 *      that rather than asking for it.
 *
 *   5. **THE LIBRARY'S OWN SEVEN KINDS FILL THIS PLUGIN EXACTLY AS A HOST'S DOES.** There is
 *      one mechanism and no privileged path: `rolltui::Windows` registers its built-ins
 *      through `rolltui_windows_register_kind` at construction, so "a transcript window" and
 *      "roll's approval modal" are built, owned, drawn and routed by the same code. The one
 *      asymmetry is the one Layout.hpp already states: a library kind NAME cannot be
 *      shadowed, and that is enforced in the layout registry, not here.
 *
 * ============================================================================================
 *
 * ---- WHAT A HOST BINDS, AND WHY THAT CROSSES NOW TOO ----------------------------------------
 *
 * The tempting argument is that a document, a row source, a submit target, a note and a menu
 * file should stay with the C++ kinds that read them, because moving a `std::function` map
 * across a C boundary buys nothing when the only code that calls it is on the other side.
 * **It stops holding the moment the caller IS the other side**, so the map belongs at the
 * boundary with the widget table it already shares an owner with. A `std::function` crosses
 * the same way a host's widget kind already did (`rolltui_windows_register_kind`, below) and
 * the way `rolltui_effect_register`'s host kinds do: as {a function pointer, a `void* ctx`,
 * an optional `void (*free_ctx)(void*)`}. The C dereferences neither the row/note object nor
 * the document it is handed for a name — it only ever hands the pointer back to whichever
 * function was registered with it, opaque both ways.
 *
 * **What still does NOT cross:** the help lead/note/scope LIST (`set_help`). Nothing there is
 * a callable — it is a few plain strings a host sets once at startup, and the reference
 * `Windows::help_scopes()` already hands back costs no allocation per frame. Moving the bytes
 * here would only ever be a second copy behind that same reference, never a `std::function`
 * removed, so it stays a `Widgets.cpp` member beside `Windows`.
 *
 * What crosses regardless of any of this is the OWNERSHIP: the widget table keyed by content,
 * the kind registry, the per-window routing table, and the scrollbar's memo of where each
 * track was drawn.
 */
/* ---- OWNERSHIP HAS THREE SHAPES AND NO FOURTH -----------------------------------------------
 * Stated here, beside the type that does the owning, because that is where a reader (and a
 * model) meets it — `ownership_test` checks that this header carries it, and CLAUDE.md carries
 * the same rule for every session. It lived in `rolltui/Widgets.hpp` previously and came
 * across with the type: half of it had already arrived (three uses of OWNED) and the BORROWED
 * half had not, which the test caught by name.
 *
 *   OWNED     one owner. `RolltuiWindows` owns every widget it builds (keyed by content, and
 *             destroyed only with the table); `RolltuiWindowStack` owns its layers by value;
 *             a `RolltuiFrame` owns its cells. In C that owner is a `_new`/`_free` pair, and
 *             `rolltui_shutdown`'s `live_bytes == 0` is what catches a missed release.
 *   BORROWED  every raw `T*` and every (pointer, length) pair crossing this API. A borrow
 *             never owns and never frees: `rolltui_windows_bind_document` takes the HOST's
 *             document and the host keeps it alive; `RolltuiWidgetEnv`'s bindings point at
 *             the table for THIS frame; `rolltui_windows_widget_for` hands back a widget this
 *             table still owns. Each one's window is stated on its own line.
 *   VALUE     everything else — `RolltuiContent`, `RolltuiStyle`, `RolltuiRect`, `RolltuiRow`.
 *             Copied, not referenced.
 *
 * **THERE IS NO SHARED OWNERSHIP, and a test fails if one appears.** Shared ownership makes a
 * lifetime a runtime question and every lifetime here is structural. Every `shared_ptr` in the
 * repository belongs to a HOST — which is the right place for one, and outside this library. */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* THE TWO FALLBACKS, and they are two because their ARGUMENT means two different things —
 * which is exactly the implicit resolution CLAUDE.md's corollary says to spell out rather
 * than let one function guess between.
 *   error   is given a CONTENT nothing could build, and works out the reason itself;
 *   panel   is given a REASON, for a widget that builds fine and then reports a problem at
 *           draw time.
 * Without them an unknown kind draws nothing, which is the one outcome Layout.hpp says must
 * never happen. */
void rolltui_context_set_error_factory(RolltuiContext* ctx, RolltuiWidgetFactory factory, void* factory_ctx);
void rolltui_context_set_panel_factory(RolltuiContext* ctx, RolltuiWidgetFactory factory, void* factory_ctx);

const RolltuiDocument* rolltui_windows_document(const RolltuiWindows* w, const char* name, size_t len);

int rolltui_windows_has_rows(const RolltuiWindows* w, const char* name, size_t len);
/* 1 when something was bound and got called; 0 (a no-op) when nothing is bound to `name`. */
int rolltui_windows_call_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRows* out);

int rolltui_windows_has_submit(const RolltuiWindows* w, const char* name, size_t len);
int rolltui_windows_call_submit(RolltuiWindows* w, const char* name, size_t len, const char* text, size_t tlen);
/* 0 (SendAndClear) when nothing is bound to `name` — `Windows::on_submit_for`'s own default. */
int rolltui_windows_on_submit(const RolltuiWindows* w, const char* name, size_t len);

int rolltui_windows_call_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNote* out);

const char* rolltui_windows_host_menu(const RolltuiWindows* w, const char* name, size_t len, size_t* out_len);

size_t rolltui_windows_help_scope_count(const RolltuiWindows* w);
/* A BORROW, valid until the scope list next changes. */
const char* rolltui_windows_help_scope_at(const RolltuiWindows* w, size_t i, size_t* len);
const char* rolltui_windows_help_lead(const RolltuiWindows* w, size_t* len);
const char* rolltui_windows_help_note(const RolltuiWindows* w, size_t* len);

const RolltuiWidgetEnv* rolltui_windows_env(const RolltuiWindows* w);

const RolltuiBindings* rolltui_windows_bindings(const RolltuiWindows* w);


/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
RolltuiWidget* rolltui_windows_at(const RolltuiWindows* w, const char* window, size_t len);
RolltuiInput* rolltui_windows_input_at(const RolltuiWindows* w, const char* window, size_t len);
RolltuiTranscript* rolltui_windows_transcript_at(const RolltuiWindows* w, const char* window, size_t len);

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
void rolltui_note_clear(RolltuiNote* n); /* text = "", state = None, since_ms = 0; keeps the buffer */

const char* rolltui_windows_dir(const RolltuiWindows* w, size_t* len);

size_t rolltui_windows_host_menu_count(const RolltuiWindows* w);
const char* rolltui_windows_host_menu_name_at(const RolltuiWindows* w, size_t i, size_t* len);

/* The shipped capsule, and what is in force. INTERNAL: a host reads a theme's answer with
 * `rolltui_theme_scrollbar_glyphs` and applies it with `rolltui_context_set_scrollbar_glyphs`,
 * and never needs to ask for the default or read one back. */
void rolltui_scrollbar_glyphs_default(RolltuiScrollbarGlyphs* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WIDGETS_H */
