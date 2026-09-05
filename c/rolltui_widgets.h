#ifndef ROLLTUI_C_WIDGETS_H
#define ROLLTUI_C_WIDGETS_H
/*
 * rolltui/c/rolltui_widgets.h — THE WIDGET VTABLE AND THE WINDOW HOST (Phase 15 m5).
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
 *      would force every widget into a behaviour only some of them want — the rule Phase 12
 *      m5 wrote and this shape now enforces rather than asks for.
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
 * ---- WHAT A HOST BINDS, AND WHY THAT CROSSES NOW TOO (Phase 15 m6) -------------------------
 *
 * Through m5 this said a document, a row source, a submit target, a note and a menu file
 * stay in `Widgets.cpp` with the kinds that read them, on the argument that moving a
 * `std::function` map across a C boundary buys nothing when the only code that calls it is
 * on the other side. **That argument stopped holding the moment `Windows` itself became a
 * thin C++ shim over this file: the caller IS the other side now**, so the map belongs at the
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
 * What crosses regardless of any of this is the ownership Phase 15 m5 was about: the widget
 * table keyed by content, the kind registry, the per-window routing table, and the
 * scrollbar's memo of where each track was drawn.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

/* Declared, not defined: the vocabulary lives in `rolltui/Effects.hpp`, and this file names
 * no state (the same m2 rule `rolltui_diff.h` and `rolltui_document.h` state). An opaque,
 * already-complete enum with a fixed underlying type is a complete type wherever only a
 * member's type is needed. */
#ifdef __cplusplus
namespace rolltui {
enum class EffectState : unsigned char;
}  // namespace rolltui
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the scroll, as the window sees it ------------------------------------------------------ */

#define ROLLTUI_AXIS_VERTICAL 0
#define ROLLTUI_AXIS_HORIZONTAL 1

typedef struct RolltuiScrollExtent {
  size_t first ROLLTUI_DEFAULT(0);   /* the first visible line */
  size_t visible ROLLTUI_DEFAULT(0); /* how many lines the viewport shows */
  size_t total ROLLTUI_DEFAULT(0);   /* how many there are */
} RolltuiScrollExtent;

/* Where the thumb sits and how long it is, in the track's own cells. 0 when no bar should be
 * drawn at all. A pure function, so the degenerate sizes are a table test. */
typedef struct RolltuiScrollThumb {
  int offset ROLLTUI_DEFAULT(0); /* cells from the track's start */
  int length ROLLTUI_DEFAULT(0); /* cells, always >= 1 when drawn */
} RolltuiScrollThumb;

int rolltui_scroll_thumb(const RolltuiScrollExtent* e, int track, RolltuiScrollThumb* out);
/* The inverse, for a click or a drag: the `first` line that puts the thumb's START at `cell`
 * of the track. Clamped to a valid first line. */
size_t rolltui_scroll_first_for_cell(const RolltuiScrollExtent* e, int track, int cell);

/* The rect a widget draws in: the window's inner rect, less one column on each side when it
 * is bordered — the widget owns the breathing room inside the frame. */
void rolltui_content_rect(const RolltuiResolvedNode* rn, RolltuiRect* out);

/* ---- THE VTABLE ------------------------------------------------------------------------------ */

typedef struct RolltuiWidgetPlugin {
  /* ---- REQUIRED (rule 2) ---- */
  /* Frees `ctx`. The window table calls this and nothing else ever does. */
  void (*destroy)(void* ctx);
  void (*layout)(void* ctx, const RolltuiResolvedNode* rn);
  void (*draw)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

  /* ---- OPTIONAL: NULL means the behaviour stated on the line (rule 2) ---- */

  /* Why this widget cannot draw, into `out`; 0 when it can. NULL: it always can. */
  int (*problem)(void* ctx, RolltuiStr* out);
  /* Note `i`, into `out`; 0 when there is no i-th note. A note does NOT stop the widget
   * drawing — a menu file's unknown key is named in the report and the menu still shows.
   * NULL: no notes. */
  int (*note_at)(void* ctx, size_t i, RolltuiStr* out);
  /* The outer extent this widget wants along its parent's axis, into `out`; 0 to let the
   * layout decide. NULL: the layout decides, which is every widget but the input. */
  int (*desired_outer)(void* ctx, int inner_w, int parent_extent, int border, int* out);
  /* 1 when the event was consumed. NULL: nothing is consumed — the kinds whose whole
   * interaction is scrolling implement this; a host drives an input or a menu itself. */
  int (*handle)(void* ctx, const RolltuiEvent* e);
  /* REPORTS its extent, so the window may draw a bar; 0 for none. NULL: no bar. */
  int (*scroll_extent)(void* ctx, unsigned char axis, RolltuiScrollExtent* out);
  /* ACCEPTS a new first line, so the bar may be dragged; 0 to decline being driven. Anything
   * that returns 1 must CLAMP. NULL: reports but will not be driven (rule 4). */
  int (*scroll_to)(void* ctx, unsigned char axis, size_t first);
} RolltuiWidgetPlugin;

/* One widget: what it IS and how to talk to it. The plugin is a BORROW of a table the
 * implementor keeps (a `static const` per kind); `ctx` is OWNED by whoever holds this. */
typedef struct RolltuiWidget {
  const RolltuiWidgetPlugin* vt;
  void* ctx;
} RolltuiWidget;

/* ---- the window host --------------------------------------------------------------------------- */

typedef struct RolltuiWindows RolltuiWindows;

/* Builds a widget for `content` (the whole string, "kind:source"). Returns a widget whose
 * `ctx` the table then OWNS, or a zeroed one to mean "I cannot build this" — which is not an
 * error path: `Windows` draws the error panel and names it in the report. */
typedef RolltuiWidget (*RolltuiWidgetFactory)(void* ctx, const char* content, size_t len);

RolltuiWindows* rolltui_windows_new(void);
void rolltui_windows_free(RolltuiWindows* w);

/* Registers a kind NAME with the factory that builds it. The library's own seven go through
 * this call at construction, exactly as a host's does (rule 5) — with `free_ctx` NULL, since
 * their `ctx` is the `Windows` object itself and is not this table's to release. A host's own
 * kind passes a real `free_ctx`, which runs when the row is replaced (a second `register_kind`
 * for the same name) and at `rolltui_windows_free` — the same shape `rolltui_effect_register`
 * uses for a host's effect kinds. The layout vocabulary's half of the registration — and the
 * refusal to shadow a library kind — is `rolltui_widget_kind_register` in `rolltui_layout.h`. */
void rolltui_windows_register_kind(RolltuiWindows* w, const char* name, size_t len,
                                   RolltuiWidgetFactory factory, void* ctx, void (*free_ctx)(void*));
/* THE TWO FALLBACKS, and they are two because their ARGUMENT means two different things —
 * which is exactly the implicit resolution CLAUDE.md's corollary says to spell out rather
 * than let one function guess between.
 *   error   is given a CONTENT nothing could build, and works out the reason itself;
 *   panel   is given a REASON, for a widget that builds fine and then reports a problem at
 *           draw time.
 * Without them an unknown kind draws nothing, which is the one outcome Layout.hpp says must
 * never happen. */
void rolltui_windows_set_error_factory(RolltuiWindows* w, RolltuiWidgetFactory factory, void* ctx);
void rolltui_windows_set_panel_factory(RolltuiWindows* w, RolltuiWidgetFactory factory, void* ctx);

/* The widget for `content`, created on demand and never destroyed until this `Windows` is
 * (Widgets.hpp: two windows on one content are two views of one widget). */
RolltuiWidget* rolltui_windows_widget_for(RolltuiWindows* w, const char* content, size_t len);
/* The widget a WINDOW holds, or NULL — filled by `sync`. */
RolltuiWidget* rolltui_windows_at(const RolltuiWindows* w, const char* window, size_t len);
/* That window's content string, a BORROW valid until the next `sync`. */
const char* rolltui_windows_content_at(const RolltuiWindows* w, const char* window, size_t len,
                                       size_t* out_len);

/* ---- what a host BINDS, by name (Phase 15 m6) ------------------------------------------------
 *
 * `bind_*` replaces whatever was bound to `name` before (releasing its `ctx` through the OLD
 * `free_ctx`, if it had one), and `rolltui_windows_free` releases whatever is left. Every
 * `has_*`/`call_*` pair is the same two questions `WidgetBase` always asked: "is anything
 * bound here" (a `problem()` check, which must not invoke a host's callable just to find out)
 * and "answer, if something is" (`call_*`, a no-op when nothing is bound). A CALLER that
 * checked `has_*` need not check the return of `call_*` too; it exists for a caller that did
 * not, the same defensive shape the C++ side had with `fn && *fn`.
 */

/* documents: `doc` is a BORROW this table never frees — the host, or `Windows`'
 * `owned_documents_` for a sample built from markdown, keeps it alive. Opaque to C: always a
 * `const rolltui::Document*`, handed back exactly as given. */
void rolltui_windows_bind_document(RolltuiWindows* w, const char* name, size_t len, const void* doc);
const void* rolltui_windows_document(const RolltuiWindows* w, const char* name, size_t len);

/* Forward declarations: `RolltuiRows`' own inline C++ methods below call these before their
 * full declarations (right after the struct) would otherwise be seen. */
typedef struct RolltuiRows RolltuiRows;
void rolltui_rows_reset(RolltuiRows* r);
void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len);
void rolltui_rows_release(RolltuiRows* r);

/* rows: one row of a `rows:` window — a label column and a value that wraps under it.
 * `rolltui::Row` IS this struct. */
typedef struct RolltuiRow {
  RolltuiStr label, value;
} RolltuiRow;

/* WHAT A HOST FILLS instead of returning a fresh vector every frame (Phase 13 m5b, and
 * CLAUDE.md's per-frame-API rule). `rolltui_rows_reset` keeps the array's capacity AND every
 * row's string buffers, so `rolltui_rows_add` on a warm frame assigns into storage that
 * already exists and allocates nothing. `rolltui::Rows` IS this struct. */
typedef struct RolltuiRows {
  RolltuiRow* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);   /* rows live; v[0..n) */
  size_t cap ROLLTUI_DEFAULT(0); /* rows allocated — v keeps its storage past n */
#ifdef __cplusplus
  void reset() { rolltui_rows_reset(this); }
  void add(std::string_view label, std::string_view value) {
    rolltui_rows_add(this, label.data(), label.size(), value.data(), value.size());
  }
  std::size_t size() const { return n; }
  const RolltuiRow& operator[](std::size_t i) const { return v[i]; }
  RolltuiRows() = default;
  RolltuiRows(const RolltuiRows&) = delete;
  RolltuiRows& operator=(const RolltuiRows&) = delete;
  ~RolltuiRows() { rolltui_rows_release(this); }
#endif
} RolltuiRows;
/* `rolltui_rows_reset`/`_add`/`_release` are forward-declared above, before this struct's own
 * inline C++ methods that call them: `n = 0` keeps every row's buffers (`reset`); frees every
 * row and the array and zeroes it (`release`). */

/* rows: `out` is the caller's `RolltuiRows`, filled in place — never stored past the call. */
typedef void (*RolltuiRowsFn)(void* ctx, RolltuiRows* out);
void rolltui_windows_bind_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRowsFn fn, void* ctx,
                               void (*free_ctx)(void*));
int rolltui_windows_has_rows(const RolltuiWindows* w, const char* name, size_t len);
/* 1 when something was bound and got called; 0 (a no-op) when nothing is bound to `name`. */
int rolltui_windows_call_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRows* out);

/* submit: `on_submit` is `Windows::OnSubmit` as an int (0 SendAndClear, 1 Keep) — this header
 * does not know the enum's name, only its two values, bound alongside the callable because a
 * host always sets both together. */
typedef void (*RolltuiSubmitFn)(void* ctx, const char* text, size_t len);
void rolltui_windows_bind_submit(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                 void (*free_ctx)(void*), int on_submit);
int rolltui_windows_has_submit(const RolltuiWindows* w, const char* name, size_t len);
int rolltui_windows_call_submit(RolltuiWindows* w, const char* name, size_t len, const char* text, size_t tlen);
/* 0 (SendAndClear) when nothing is bound to `name` — `Windows::on_submit_for`'s own default. */
int rolltui_windows_on_submit(const RolltuiWindows* w, const char* name, size_t len);

/* note: an input's one-line note, and what STATE it is in. A host with no motion to report
 * fills only `text`; `state` defaults to None, which is what makes the implicit conversion
 * from a bare string do the whole of "no motion" for a host that never mentions it.
 * `rolltui::Note` IS this struct. */
typedef struct RolltuiNote {
  RolltuiStr text;
#ifdef __cplusplus
  rolltui::EffectState state = static_cast<rolltui::EffectState>(0); /* None */
#else
  unsigned char state;
#endif
  unsigned long long since_ms ROLLTUI_DEFAULT(0); /* when it entered `state` */
#ifdef __cplusplus
  // No constructor, destructor or assignment of this type's OWN is declared beyond the
  // converting ones below: `text` (a `RolltuiStr`) already has correct copy/move/destroy, so
  // the compiler-generated special members already do the right thing by construction — the
  // same reasoning `RolltuiContent` states for itself.
  RolltuiNote() = default;
  // Implicit on purpose, and the three overloads (matching `RolltuiStr`'s own) are what
  // keeps every host that has no motion to report writing exactly what it wrote before:
  // `return "working";`. All three, not just `string_view`, because a `std::string` argument
  // reaching `string_view` would be a SECOND user-defined conversion on top of this
  // constructor's own — disallowed implicitly, and exactly the trap `rolltui::Str` avoids by
  // declaring the same three.
  RolltuiNote(std::string_view t) : text(t) {}                                        // NOLINT(google-explicit-constructor)
  RolltuiNote(const char* t) : text(t ? std::string_view(t) : std::string_view()) {}  // NOLINT(google-explicit-constructor)
  RolltuiNote(const std::string& t) : text(t) {}                                      // NOLINT(google-explicit-constructor)
  RolltuiNote(std::string_view t, rolltui::EffectState s, unsigned long long since = 0)
      : text(t), state(s), since_ms(since) {}
  RolltuiNote& operator=(std::string_view t) {
    text = t;
    state = static_cast<rolltui::EffectState>(0);
    since_ms = 0;
    return *this;
  }
  RolltuiNote& operator=(const std::string& t) { return *this = std::string_view(t); }
  RolltuiNote& operator=(const char* t) { return *this = (t ? std::string_view(t) : std::string_view()); }
#endif
} RolltuiNote;

void rolltui_note_clear(RolltuiNote* n); /* text = "", state = None, since_ms = 0; keeps the buffer */

/* note: `out` is the caller's `RolltuiNote`, already cleared, filled in place. */
typedef void (*RolltuiNoteFn)(void* ctx, RolltuiNote* out);
void rolltui_windows_bind_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNoteFn fn, void* ctx,
                               void (*free_ctx)(void*));
int rolltui_windows_has_note(const RolltuiWindows* w, const char* name, size_t len);
int rolltui_windows_call_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNote* out);

/* the preset directory: `file:`, `menu:`'s user rung and `menus/<name>.json` resolve against
 * this. A BORROW out, "" (never NULL) until `set_dir` is first called. */
void rolltui_windows_set_dir(RolltuiWindows* w, const char* dir, size_t len);
const char* rolltui_windows_dir(const RolltuiWindows* w, size_t* len);

/* a menu file the HOST carries in its own binary — `menu:<name>`'s middle rung (the order is
 * `Widgets.hpp`'s). The text is copied in; the borrow out is valid until that name is bound
 * again or `w` is freed. `_count`/`_name_at` enumerate every host menu for `Windows::menu_names`. */
void rolltui_windows_add_menu(RolltuiWindows* w, const char* name, size_t len, const char* json, size_t json_len);
const char* rolltui_windows_host_menu(const RolltuiWindows* w, const char* name, size_t len, size_t* out_len);
size_t rolltui_windows_host_menu_count(const RolltuiWindows* w);
const char* rolltui_windows_host_menu_name_at(const RolltuiWindows* w, size_t i, size_t* len);

/* what a `help` window renders (Phase 15 m5, moved to the boundary so the `help` kind can be
 * a plugin like the rest): an optional lead line, the scopes to list in order, an optional
 * trailing note. `set_help` REPLACES lead/note; the scope list is built separately
 * (`clear_help_scopes` then `add_help_scope` per entry) because it is a `std::vector` at the
 * one C++ call site (`Windows::set_help`) and this is the same shape `rolltui_windows_add_menu`
 * already uses for a list built one call at a time. */
void rolltui_windows_set_help(RolltuiWindows* w, const char* lead, size_t lead_len, const char* note,
                              size_t note_len);
void rolltui_windows_clear_help_scopes(RolltuiWindows* w);
void rolltui_windows_add_help_scope(RolltuiWindows* w, const char* scope, size_t len);
size_t rolltui_windows_help_scope_count(const RolltuiWindows* w);
/* A BORROW, valid until the scope list next changes. */
const char* rolltui_windows_help_scope_at(const RolltuiWindows* w, size_t i, size_t* len);
const char* rolltui_windows_help_lead(const RolltuiWindows* w, size_t* len);
const char* rolltui_windows_help_note(const RolltuiWindows* w, size_t* len);

/* ---- the frame ------------------------------------------------------------------------------- */

typedef struct RolltuiWidgetEnv {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  unsigned long long now_ms ROLLTUI_DEFAULT(0);
} RolltuiWidgetEnv;

void rolltui_windows_set_env(RolltuiWindows* w, const RolltuiWidgetEnv* env);
const RolltuiWidgetEnv* rolltui_windows_env(const RolltuiWindows* w);

/* THE LIVE BINDINGS TABLE, as a handle (Phase 15 m5e): a widget looks up an action's chords
 * or asks whether a key is one of the transcript scope's without knowing `rolltui::Bindings`
 * exists. A BORROW — `w` never frees it — set once per frame alongside `set_env` from
 * `Bindings::handle()` (the live table, or `default_bindings().handle()`). NULL only before
 * the first `set_env`. */
void rolltui_windows_set_bindings(RolltuiWindows* w, const RolltuiBindings* b);
const RolltuiBindings* rolltui_windows_bindings(const RolltuiWindows* w);

/* THE CURRENT FRAME'S STYLE TABLE, indexed by Role ordinal — a BORROW valid for the length of
 * one `rolltui_windows_draw` call, set at its top from the `styles` it is already handed (the
 * same array `draw_scrollbar` inside this module reads). This is what lets a widget's `draw`
 * ask "what does Role::text look like" without the vtable's `draw` slot carrying a fourth
 * parameter every kind must accept whether or not it draws text — the `RolltuiWindowRoles`
 * shape one level up, generalised to the one thing every drawing kind needs. NULL outside a
 * draw call. */
const RolltuiStyle* rolltui_windows_styles(const RolltuiWindows* w);

/* Instantiates/reuses a widget per window and collects what each one says is wrong. The
 * report's lines are BORROWS, valid until the next sync. */
void rolltui_windows_sync(RolltuiWindows* w, const RolltuiWindowStack* stack);
size_t rolltui_windows_report_count(const RolltuiWindows* w);
const char* rolltui_windows_report_at(const RolltuiWindows* w, size_t i, size_t* len);

/* Asks each widget for the outer extent it wants and writes it into the node (the only thing
 * a widget writes back into the layout tree). */
void rolltui_windows_autosize(RolltuiWindows* w, RolltuiWindowStack* stack, RolltuiRect box);
void rolltui_windows_layout(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box);

/* THE TWO ROLES THE WINDOW ITSELF DRAWS WITH. The widget draws with its own; these are the
 * scrollbar's track and thumb, which live in the window's border column and which a widget
 * never sees (Phase 12 m5). */
typedef struct RolltuiWindowRoles {
  unsigned char scrollbar;
  unsigned char border;
  unsigned char border_active;
} RolltuiWindowRoles;

void rolltui_windows_draw(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                          const RolltuiStyle* styles, const RolltuiWindowRoles* roles);
/* An event the stack routed to `window`; 1 when the widget (or its scrollbar) consumed it. */
int rolltui_windows_handle(RolltuiWindows* w, const char* window, size_t len, const RolltuiEvent* e);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_WIDGETS_H */
