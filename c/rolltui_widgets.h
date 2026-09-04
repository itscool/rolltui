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
 * THE VTABLE, AND THE RULE FOR ADDING TO IT
 * ============================================================================================
 *
 * `rolltui::Widget` is the one place the library uses inheritance for real: nine virtuals a
 * host overrides. In C that is a STRUCT OF FUNCTION POINTERS the implementor fills — which is
 * what `register_kind` already was in spirit, since a host's kind was always a factory
 * producing something the library would only ever call through those nine slots.
 *
 * **Making it a struct changes one thing that matters, and it is not the calling convention:
 * a vtable you can SEE is a set you have to CLOSE.** A virtual function is added by typing
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
 *   5. **THE LIBRARY'S OWN SEVEN KINDS FILL THIS VTABLE EXACTLY AS A HOST'S DOES.** There is
 *      one mechanism and no privileged path: `rolltui::Windows` registers its built-ins
 *      through `rolltui_windows_register_kind` at construction, so "a transcript window" and
 *      "roll's approval modal" are built, owned, drawn and routed by the same code. The one
 *      asymmetry is the one Layout.hpp already states: a library kind NAME cannot be
 *      shadowed, and that is enforced in the layout registry, not here.
 *
 * ============================================================================================
 *
 * ---- WHAT THIS BOUNDARY DELIBERATELY DOES NOT KNOW ----------------------------------------
 *
 * **What a host BOUND.** A document, a row source, a submit target, a note, a menu file, the
 * help scopes and the preset directory are host facts bound to host callables, and they stay
 * in `Widgets.cpp` with the kinds that read them — the same trade m5c made for the menu's
 * validator registry, and for the same reason: moving a `std::function` map across a C
 * boundary buys nothing when the only code that calls it is on the other side.
 *
 * What DOES cross is the ownership this milestone is about: the widget table keyed by
 * content, the kind registry, the per-window routing table, and the scrollbar's memo of where
 * each track was drawn.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"

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

typedef struct RolltuiWidgetVTable {
  /* ---- REQUIRED (rule 2) ---- */
  /* Frees `self`. The window table calls this and nothing else ever does. */
  void (*destroy)(void* self);
  void (*layout)(void* self, const RolltuiResolvedNode* rn);
  void (*draw)(void* self, const RolltuiResolvedNode* rn, RolltuiFrame* f);

  /* ---- OPTIONAL: NULL means the behaviour stated on the line (rule 2) ---- */

  /* Why this widget cannot draw, into `out`; 0 when it can. NULL: it always can. */
  int (*problem)(void* self, RolltuiStr* out);
  /* Note `i`, into `out`; 0 when there is no i-th note. A note does NOT stop the widget
   * drawing — a menu file's unknown key is named in the report and the menu still shows.
   * NULL: no notes. */
  int (*note_at)(void* self, size_t i, RolltuiStr* out);
  /* The outer extent this widget wants along its parent's axis, into `out`; 0 to let the
   * layout decide. NULL: the layout decides, which is every widget but the input. */
  int (*desired_outer)(void* self, int inner_w, int parent_extent, int border, int* out);
  /* 1 when the event was consumed. NULL: nothing is consumed — the kinds whose whole
   * interaction is scrolling implement this; a host drives an input or a menu itself. */
  int (*handle)(void* self, const RolltuiEvent* e);
  /* REPORTS its extent, so the window may draw a bar; 0 for none. NULL: no bar. */
  int (*scroll_extent)(void* self, unsigned char axis, RolltuiScrollExtent* out);
  /* ACCEPTS a new first line, so the bar may be dragged; 0 to decline being driven. Anything
   * that returns 1 must CLAMP. NULL: reports but will not be driven (rule 4). */
  int (*scroll_to)(void* self, unsigned char axis, size_t first);
} RolltuiWidgetVTable;

/* One widget: what it IS and how to talk to it. The vtable is a BORROW of a table the
 * implementor keeps (a `static const` per kind); `self` is OWNED by whoever holds this. */
typedef struct RolltuiWidget {
  const RolltuiWidgetVTable* vt;
  void* self;
} RolltuiWidget;

/* ---- the window host --------------------------------------------------------------------------- */

typedef struct RolltuiWindows RolltuiWindows;

/* Builds a widget for `content` (the whole string, "kind:source"). Returns a widget whose
 * `self` the table then OWNS, or a zeroed one to mean "I cannot build this" — which is not an
 * error path: `Windows` draws the error panel and names it in the report. */
typedef RolltuiWidget (*RolltuiWidgetFactory)(void* ctx, const char* content, size_t len);

RolltuiWindows* rolltui_windows_new(void);
void rolltui_windows_free(RolltuiWindows* w);

/* Registers a kind NAME with the factory that builds it. The library's own seven go through
 * this call at construction, exactly as a host's does (rule 5). The layout vocabulary's half
 * of the registration — and the refusal to shadow a library kind — is
 * `rolltui_widget_kind_register` in `rolltui_layout.h`. */
void rolltui_windows_register_kind(RolltuiWindows* w, const char* name, size_t len,
                                   RolltuiWidgetFactory factory, void* ctx);
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

/* ---- the frame ------------------------------------------------------------------------------- */

typedef struct RolltuiWidgetEnv {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  unsigned long long now_ms ROLLTUI_DEFAULT(0);
} RolltuiWidgetEnv;

void rolltui_windows_set_env(RolltuiWindows* w, const RolltuiWidgetEnv* env);
const RolltuiWidgetEnv* rolltui_windows_env(const RolltuiWindows* w);

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
