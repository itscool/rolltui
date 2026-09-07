/* rolltui/c/rolltui_widgets.c — the C side of the widget vtable's host. See
 * rolltui_widgets.h; the rules are rolltui/Widgets.hpp's. */
#include "rolltui/c/rolltui_widgets.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_transcript.h"

static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- the scrollbar's geometry ------------------------------------------------------------ */

int rolltui_scroll_thumb(const RolltuiScrollExtent* e, int track, RolltuiScrollThumb* out) {
  double frac;
  int len, span, off;
  size_t max_first, first;
  /* No bar when there is nothing to scroll, or nowhere to draw one. Both are answers, not
   * edge cases: a bar on a document that fits is a lie about there being more. */
  if (track <= 0 || e->total == 0 || e->visible == 0 || e->total <= e->visible) return 0;
  frac = (double)e->visible / (double)e->total;
  len = (int)(frac * track + 0.5);
  if (len < 1) len = 1; /* always visible: a 1-cell thumb still says where you are */
  if (len > track) len = track;
  max_first = e->total - e->visible;
  first = e->first > max_first ? max_first : e->first;
  span = track - len;
  off = span <= 0 ? 0 : (int)((double)first / (double)max_first * span + 0.5);
  if (off < 0) off = 0;
  if (off > span) off = span;
  /* THE GUARANTEE: the thumb touches an end IF AND ONLY IF the view is at that end. The end
   * cells are RESERVED for the ends and everything between is squeezed into what is left;
   * below a 2-cell span there is nothing to reserve and the honest answer is the ends alone.
   * (Widgets.hpp states the whole of it.) */
  if (span >= 2) {
    if (first == 0) off = 0;
    else if (first == max_first) off = span;
    else off = iclamp(off, 1, span - 1);
  } else {
    off = (first == max_first) ? span : 0;
  }
  out->offset = off;
  out->length = len;
  return 1;
}

size_t rolltui_scroll_first_for_cell(const RolltuiScrollExtent* e, int track, int cell) {
  size_t max_first, first;
  RolltuiScrollThumb t;
  int len, span, c;
  double f;
  if (e->total <= e->visible || track <= 0) return 0;
  max_first = e->total - e->visible;
  len = rolltui_scroll_thumb(e, track, &t) ? t.length : 1;
  span = track - len;
  if (span <= 0) return cell <= 0 ? 0 : max_first;
  c = cell;
  if (c < 0) c = 0;
  if (c > span) c = span;
  f = (double)c / (double)span * (double)max_first + 0.5;
  first = (size_t)f;
  return first > max_first ? max_first : first;
}

void rolltui_content_rect(const RolltuiResolvedNode* rn, RolltuiRect* out) {
  *out = rn->inner;
  if (rn->node->border != 0 /* Border::None */ && out->w >= 3) {
    out->x += 1;
    out->w -= 2;
  }
}

/* ---- the host ---------------------------------------------------------------------------- */

/* One registered kind: a name, a factory and the context it was registered with, plus how to
 * release that context — NULL for the library's own seven, whose `ctx` is the `Windows`
 * object and not this table's to free (rule 5 made literal: they are in here beside a
 * host's, but a host's alone is owned here). */
typedef struct KindRow {
  RolltuiStr name;
  RolltuiWidgetFactory factory;
  void* ctx;
  void (*free_ctx)(void*);
} KindRow;

/* ---- what a host BOUND, by name -------------------------------------------------------------
 * One heap-allocated binding per bound name, stored as a `RolltuiMap` value (STRATEGY 5:
 * GROWING HEAP, alongside every other per-entry allocation in this file — `WindowSlot`, the
 * report's `RolltuiStr`s). `free_binding_ctx` is shared by all three callback kinds; each
 * struct also carries its OWN function-pointer type so a caller can never hand a rows
 * callback to `bind_note` and have it silently compile. */
typedef struct RowsBinding {
  RolltuiRowsFn fn;
  void* ctx;
  void (*free_ctx)(void*);
} RowsBinding;

typedef struct SubmitBinding {
  RolltuiSubmitFn fn;
  void* ctx;
  void (*free_ctx)(void*);
  int on_submit;
} SubmitBinding;

typedef struct NoteBinding {
  RolltuiNoteFn fn;
  void* ctx;
  void (*free_ctx)(void*);
} NoteBinding;

static void free_binding_ctx(void* ctx, void (*free_ctx)(void*)) {
  if (free_ctx) free_ctx(ctx);
}

/* Where a window's scrollbar track WAS on the last frame, so `handle` — which is given a
 * window id and no geometry — can tell a press on the thumb from a press on the text. */
typedef struct Track {
  int x, y, h;
} Track;

/* What a window holds, this frame: its widget (BORROWED from `by_content`), its content
 * string, and its track. One entry per window, rebuilt in place by `sync`. */
typedef struct WindowSlot {
  RolltuiWidget* widget;
  RolltuiStr content;
  Track track;
  int live; /* this sync found it */
} WindowSlot;

/* ---- WHAT A SESSION CONFIGURES, ONCE --------------------------------------------------------
 * The half of `RolltuiWindows` that was never about what is on screen: a program's widget-kind
 * factories, its menus, its key table, its help scopes, its highlighter, and the vocabularies the
 * built-in kinds read back. Set at startup and identical for every screen the program runs, which
 * is the definition of a session's rather than a screen's — so it is owned by the CONTEXT and a
 * `RolltuiWindows` borrows it.
 *
 * THE FACTORY HALF OF RUNG 2 IS WHY THIS COULD NOT WAIT. m2 moved a host kind's NAME and source
 * rule into the context and left its FACTORY here, so one registration was owned by two things —
 * the "TWO SPELLINGS of one identity" shape CLAUDE.md names, created by a milestone boundary.
 *
 * LIFETIME: every widget in `by_content` borrows from here, and `rolltui_windows_free` runs
 * before `rolltui_context_free` because a context must outlive the windows made against it
 * (stated at `rolltui_windows_new`). Borrowers die first, which is the order this needs. */
struct RolltuiWindowConfig {
  KindRow* kinds;
  size_t kind_n, kind_cap;
  RolltuiWidgetFactory error_factory;
  void* error_ctx;
  RolltuiWidgetFactory panel_factory;
  void* panel_ctx;
  RolltuiWidgetEnv env;
  RolltuiMap host_menus; /* name -> RolltuiStr* (a menu file's text), OWNED */
  RolltuiStr dir;        /* the preset directory; "" until set_dir */

  /* ---- what a `help` window renders (moved to the boundary so `help` can be a plugin) ---- */
  RolltuiStr help_lead, help_note;
  RolltuiStr* help_scopes; /* GROWING AMORTISED, like `report` */
  size_t help_scopes_n, help_scopes_cap;

  const RolltuiBindings* bindings; /* BORROWED; NULL only before the first set_env */

  /* ---- what rolltui_widget_kinds.c's built-in kinds read back through the Windows -------- */
  RolltuiBuiltinRoles builtin_roles;
  RolltuiScrollTextActions scroll_actions;
  RolltuiCodeFold code_fold;
  RolltuiTranscriptActions transcript_actions;
  RolltuiMenuRoles menu_roles;
  const RolltuiInputActions* input_actions; /* BORROWED, process lifetime */
};

/* The session's configuration, made on first use — the same shape every other subsystem a
 * context owns uses, so a host never has to create one. */
RolltuiWindowConfig* rolltui_context_window_config(RolltuiContext* ctx) {
  if (ctx->window_config == NULL) ctx->window_config = rolltui_window_config_new();
  return ctx->window_config;
}

RolltuiWindowConfig* rolltui_window_config_new(void) {
  RolltuiWindowConfig* c = (RolltuiWindowConfig*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  return c;
}

void rolltui_window_config_free(RolltuiWindowConfig* c) {
  size_t i;
  if (c == NULL) return;
  for (i = 0; i < c->kind_n; ++i) {
    free_binding_ctx(c->kinds[i].ctx, c->kinds[i].free_ctx);
    rolltui_str_free(&c->kinds[i].name);
  }
  rolltui_mem_free(c->kinds);
  for (i = 0; i < rolltui_map_count(&c->host_menus); ++i) {
    RolltuiStr* s = (RolltuiStr*)rolltui_map_value_at(&c->host_menus, i);
    rolltui_str_free(s);
    rolltui_mem_free(s);
  }
  rolltui_map_release(&c->host_menus);
  rolltui_str_free(&c->dir);
  rolltui_str_free(&c->help_lead);
  rolltui_str_free(&c->help_note);
  for (i = 0; i < c->help_scopes_cap; ++i) rolltui_str_free(&c->help_scopes[i]);
  rolltui_mem_free(c->help_scopes);
  rolltui_mem_free(c);
}

struct RolltuiWindows {
  /* BORROWED, and it must outlive this: the session whose widget-kind registry
   * `sync` resolves a window's `kind[:source]` against. A layout is plain data and portable
   * between contexts precisely because that resolution happens HERE, per frame, and not at load. */
  RolltuiContext* ctx;
  /* BORROWED from `ctx`: what this program configured once. See `RolltuiWindowConfig`. */
  RolltuiWindowConfig* cfg;
  const RolltuiStyle* styles; /* set at the top of rolltui_windows_draw; NULL outside one */
  RolltuiMap by_content; /* content → RolltuiWidget*, OWNED (this table destroys them) */
  RolltuiMap by_window;  /* window id → WindowSlot*, OWNED */

  RolltuiStr* report; /* the bad values `sync` collected; BORROWED out */
  size_t report_n, report_cap;
  RolltuiStr scratch; /* the "window 'x' (content 'y'): " prefix, built only when needed */

  RolltuiStr bar_drag; /* the window whose thumb is being dragged, "" for none */
  int bar_grab;        /* cells from the thumb's start to where it was grabbed */

  RolltuiResolvedNode* nodes; /* the per-frame resolve buffer, reused */
  size_t node_n, node_cap;

  /* ---- what a host BOUND, by name -------------------------------------------------------- */
  RolltuiMap documents;  /* name -> BORROWED doc pointer, opaque to C; never freed by this table */
  /* …and the OWNED half (Phase 17 m3, from `rolltui::Windows::owned_documents_`): name ->
   * RolltuiDocument*, OWNED. A sample a tool binds from an app profile has no live document to
   * point at, so this table keeps one; `documents` above then carries the borrow of it, which is
   * why the two maps are separate rather than one map with an ownership flag. */
  RolltuiMap owned_documents;
  RolltuiMap rows;       /* name -> RowsBinding*, OWNED */
  RolltuiMap submits;    /* name -> SubmitBinding*, OWNED */
  RolltuiMap notes;      /* name -> NoteBinding*, OWNED */

  /* ---- THE TYPED WIDGETS THIS TABLE OWNS, BY SOURCE -----------------------------------------
   * The three maps `rolltui::Windows` used to keep on the C++ side, moved here in the same
   * change that repointed the `input`/`transcript`/`menu` factories at them — the header says
   * why the two halves could not be done separately. Created on demand by the accessors and
   * by those factories alike, and destroyed only with `w`. STRATEGY 5 (GROWING HEAP): each
   * handle is its own module's `_new`/`_free` pair, and the map owns nothing but the keys. */
  /* THE HIGHLIGHTER IS A SCREEN'S, NOT A SESSION'S — and the reason is a RULE this milestone
   * found rather than a judgement: its setter MUTATES LIVE WIDGETS, pushing onto
   * every transcript that already exists so neither order of `set_highlight` and `transcript`
   * can lose it. A session-owned setter has no screen to push to, and the context deliberately
   * does not know its windows. `set_input_min_outer` stayed for exactly the same reason: a
   * config call that must reach live widgets is a screen's. */
  RolltuiMdHighlightFn highlight_fn;
  void* highlight_ctx;
  void (*highlight_free_ctx)(void*);

  RolltuiMap inputs;      /* source -> RolltuiInput*, OWNED */
  RolltuiMap transcripts; /* source -> RolltuiTranscript*, OWNED */
  RolltuiMap menus;       /* source -> RolltuiMenu*, OWNED */
};

RolltuiWindows* rolltui_windows_new(RolltuiContext* ctx) {
  RolltuiWindows* w = (RolltuiWindows*)rolltui_mem_alloc(sizeof *w);
  memset(w, 0, sizeof *w);
  w->ctx = ctx;
  /* The session's configuration, made on first use like every other subsystem it owns. A second
   * `RolltuiWindows` on the same context gets the SAME one, which is the point of the split:
   * two screens in one session are configured once, not twice. */
  w->cfg = rolltui_context_window_config(ctx);
  return w;
}

/* The session this was made for — what the built-in kinds resolve a content string against. */
RolltuiContext* rolltui_windows_context(const RolltuiWindows* w) { return w ? w->ctx : NULL; }

static void widget_destroy(RolltuiWidget* wd) {
  if (!wd) return;
  if (wd->vt && wd->vt->destroy && wd->ctx) wd->vt->destroy(wd->ctx);
  rolltui_mem_free(wd);
}

void rolltui_windows_free(RolltuiWindows* w) {
  size_t i;
  if (!w) return;
  for (i = 0; i < rolltui_map_count(&w->by_content); ++i)
    widget_destroy((RolltuiWidget*)rolltui_map_value_at(&w->by_content, i));
  rolltui_map_release(&w->by_content);
  for (i = 0; i < rolltui_map_count(&w->by_window); ++i) {
    WindowSlot* s = (WindowSlot*)rolltui_map_value_at(&w->by_window, i);
    rolltui_str_free(&s->content);
    rolltui_mem_free(s);
  }
  rolltui_map_release(&w->by_window);
  for (i = 0; i < w->report_cap; ++i) rolltui_str_free(&w->report[i]);
  rolltui_mem_free(w->report);
  rolltui_str_free(&w->scratch);
  rolltui_str_free(&w->bar_drag);
  rolltui_mem_free(w->nodes);
  free_binding_ctx(w->highlight_ctx, w->highlight_free_ctx);
  /* the host-binding surface (m6): `documents` is BORROWS only, nothing to free per entry —
   * but `owned_documents` holds the samples those borrows may point INTO, so it is released
   * after it rather than before. */
  rolltui_map_release(&w->documents);
  for (i = 0; i < rolltui_map_count(&w->owned_documents); ++i) {
    RolltuiDocument* d = (RolltuiDocument*)rolltui_map_value_at(&w->owned_documents, i);
    rolltui_document_release(d);
    rolltui_mem_free(d);
  }
  rolltui_map_release(&w->owned_documents);
  for (i = 0; i < rolltui_map_count(&w->rows); ++i) {
    RowsBinding* b = (RowsBinding*)rolltui_map_value_at(&w->rows, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->rows);
  for (i = 0; i < rolltui_map_count(&w->submits); ++i) {
    SubmitBinding* b = (SubmitBinding*)rolltui_map_value_at(&w->submits, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->submits);
  for (i = 0; i < rolltui_map_count(&w->notes); ++i) {
    NoteBinding* b = (NoteBinding*)rolltui_map_value_at(&w->notes, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->notes);
  /* The typed widgets (m1c), AFTER `by_content` above: every widget ctx BORROWS one of these,
   * so the borrowers have to be gone before the owners are. */
  for (i = 0; i < rolltui_map_count(&w->inputs); ++i)
    rolltui_input_free((RolltuiInput*)rolltui_map_value_at(&w->inputs, i));
  rolltui_map_release(&w->inputs);
  for (i = 0; i < rolltui_map_count(&w->transcripts); ++i)
    rolltui_transcript_free((RolltuiTranscript*)rolltui_map_value_at(&w->transcripts, i));
  rolltui_map_release(&w->transcripts);
  for (i = 0; i < rolltui_map_count(&w->menus); ++i)
    rolltui_menu_free((RolltuiMenu*)rolltui_map_value_at(&w->menus, i));
  rolltui_map_release(&w->menus);
  rolltui_mem_free(w);
}

void rolltui_context_register_kind(RolltuiContext* ctx, const char* name, size_t len,
                                   RolltuiWidgetFactory factory, void* kind_ctx, void (*free_ctx)(void*)) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  size_t i;
  for (i = 0; i < cfg->kind_n; ++i)
    if (rolltui_str_eq(&cfg->kinds[i].name, name, len)) {
      /* REPLACING: release what this name owned before taking the new one. */
      free_binding_ctx(cfg->kinds[i].ctx, cfg->kinds[i].free_ctx);
      cfg->kinds[i].factory = factory;
      cfg->kinds[i].ctx = ctx;
      cfg->kinds[i].free_ctx = free_ctx;
      return;
    }
  cfg->kinds = (KindRow*)rolltui_grow_zeroed(cfg->kinds, &cfg->kind_cap, cfg->kind_n + 1, sizeof *cfg->kinds);
  rolltui_str_set(&cfg->kinds[cfg->kind_n].name, name, len);
  cfg->kinds[cfg->kind_n].factory = factory;
  cfg->kinds[cfg->kind_n].ctx = kind_ctx;
  cfg->kinds[cfg->kind_n].free_ctx = free_ctx;
  ++cfg->kind_n;
}

void rolltui_context_set_error_factory(RolltuiContext* ctx, RolltuiWidgetFactory factory, void* factory_ctx) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  cfg->error_factory = factory;
  cfg->error_ctx = factory_ctx;
}

void rolltui_context_set_panel_factory(RolltuiContext* ctx, RolltuiWidgetFactory factory, void* factory_ctx) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  cfg->panel_factory = factory;
  cfg->panel_ctx = factory_ctx;
}

void rolltui_context_set_env(RolltuiContext* ctx, const RolltuiWidgetEnv* env) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx); cfg->env = *env; }
const RolltuiWidgetEnv* rolltui_windows_env(const RolltuiWindows* w) { return &w->cfg->env; }

/* The kind half of a content string, which is everything before the first ':'. */
static size_t kind_len(const char* content, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i)
    if (content[i] == ':') return i;
  return len;
}

RolltuiWidget* rolltui_windows_widget_for(RolltuiWindows* w, const char* content, size_t len) {
  RolltuiWidget* wd = (RolltuiWidget*)rolltui_map_get(&w->by_content, content, len);
  const size_t kl = kind_len(content, len);
  RolltuiWidget built;
  size_t i;
  if (wd) return wd;
  /* THE ROW IS CLAIMED BEFORE THE FACTORY RUNS, and that ordering is load-
   * bearing rather than tidy: a factory may ask this table for the typed object its own
   * content names — `rolltui_windows_menu` does, because resolving a menu's FILE goes through
   * the very widget being built — and without the row already present that call would come
   * straight back in here and recurse forever. With it present but ZEROED, the re-entrant call
   * reads "not built yet" and answers accordingly; the outer call fills it in below. A zeroed
   * widget is already a legal state here — it is exactly what an unbuildable content with no
   * error factory leaves — so nothing downstream learns a new case. */
  wd = (RolltuiWidget*)rolltui_mem_alloc(sizeof *wd);
  memset(wd, 0, sizeof *wd);
  rolltui_map_put(&w->by_content, content, len, wd);
  memset(&built, 0, sizeof built);
  for (i = 0; i < w->cfg->kind_n; ++i)
    if (rolltui_str_eq(&w->cfg->kinds[i].name, content, kl)) {
      built = w->cfg->kinds[i].factory(w->cfg->kinds[i].ctx, w, content, len);
      break;
    }
  /* NOTHING BUILT IS NOT AN ERROR PATH: the error factory draws the reason, which is
   * Layout.hpp's "a window is never blank because its content was not understood". */
  if (!built.vt && w->cfg->error_factory) built = w->cfg->error_factory(w->cfg->error_ctx, w, content, len);
  *wd = built;
  return wd;
}

static WindowSlot* slot_of(const RolltuiWindows* w, const char* window, size_t len) {
  return (WindowSlot*)rolltui_map_get(&w->by_window, window, len);
}

RolltuiWidget* rolltui_windows_at(const RolltuiWindows* w, const char* window, size_t len) {
  WindowSlot* s = slot_of(w, window, len);
  return s ? s->widget : NULL;
}

const char* rolltui_windows_content_at(const RolltuiWindows* w, const char* window, size_t len,
                                       size_t* out_len) {
  WindowSlot* s = slot_of(w, window, len);
  if (!s) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  return rolltui_str_get(&s->content, out_len);
}

/* ---- THE TYPED WIDGETS, OWNED HERE (Phase 17 m1c; the header states why) --------------------
 *
 * One object per SOURCE, created on demand and kept until `w` is freed — the same rule the
 * widget table itself follows, and the reason a host driving `input:prompt` and the window
 * drawing it are one object rather than two that pass each other's tests separately. */

RolltuiInput* rolltui_windows_input(RolltuiWindows* w, const char* source, size_t len) {
  RolltuiInput* in = (RolltuiInput*)rolltui_map_get(&w->inputs, source, len);
  if (in) return in;
  in = rolltui_input_new();
  rolltui_map_put(&w->inputs, source, len, in);
  return in;
}

RolltuiTranscript* rolltui_windows_transcript(RolltuiWindows* w, const char* source, size_t len) {
  RolltuiTranscript* t = (RolltuiTranscript*)rolltui_map_get(&w->transcripts, source, len);
  if (t) return t;
  t = rolltui_transcript_new(); /* its roles are its own defaults now (rolltui_transcript.c) */
  /* The live highlighter, applied at CREATION so a transcript first asked for after
   * `set_highlight` is not silently the one that misses it. */
  if (w->highlight_fn) rolltui_transcript_set_highlight(t, w->highlight_fn, w->highlight_ctx);
  rolltui_map_put(&w->transcripts, source, len, t);
  return t;
}

void rolltui_windows_set_highlight(RolltuiWindows* w, RolltuiMdHighlightFn fn, void* ctx,
                                   void (*free_ctx)(void*)) {
  size_t i;
  free_binding_ctx(w->highlight_ctx, w->highlight_free_ctx);
  w->highlight_fn = fn;
  w->highlight_ctx = ctx;
  w->highlight_free_ctx = free_ctx;
  /* PUSHED onto every transcript that already exists, rather than left for each to pull on
   * its own next frame: nothing here polls an epoch, so nothing is left to miss a change. */
  for (i = 0; i < rolltui_map_count(&w->transcripts); ++i)
    rolltui_transcript_set_highlight((RolltuiTranscript*)rolltui_map_value_at(&w->transcripts, i), fn, ctx);
}

/* The menu's own map half, WITHOUT the file refresh — this is what the `menu` factory calls,
 * and it must not reach back into `rolltui_windows_widget_for` (which is what built it). */
static RolltuiMenu* menu_for_source(RolltuiWindows* w, const char* source, size_t len) {
  RolltuiMenu* m = (RolltuiMenu*)rolltui_map_get(&w->menus, source, len);
  if (m) return m;
  m = rolltui_menu_new(); /* single-line editor, no prompt: the menu's own (rolltui_menu.c) */
  rolltui_map_put(&w->menus, source, len, m);
  return m;
}

/* The widget ctx that resolves `menu:<source>`'s FILE, or NULL when this content builds
 * something else (an unparsable content gets the error widget, whose ctx is not a menu's —
 * checking the vtable is what makes that a NULL rather than a misread struct). */
static void* menu_ctx_for_source(RolltuiWindows* w, const char* source, size_t len) {
  /* A LOCAL key, not a scratch member on `w`: the factory this call can reach re-enters here
   * for the same source, and a shared buffer would be re-set — possibly reallocated — under
   * the outer call that is still reading from it. Nine bytes on a path that already stat()s a
   * file is not the allocation to save. */
  RolltuiStr key;
  RolltuiWidget* wd;
  void* ctx = NULL;
  memset(&key, 0, sizeof key);
  rolltui_str_set(&key, "menu:", 5);
  rolltui_str_append(&key, source, len);
  wd = rolltui_windows_widget_for(w, key.p, key.n);
  if (wd && wd->ctx && wd->vt == rolltui_menu_widget_plugin()) ctx = wd->ctx;
  rolltui_str_free(&key);
  return ctx;
}

RolltuiMenu* rolltui_windows_menu(RolltuiWindows* w, const char* source, size_t len) {
  RolltuiMenu* m = menu_for_source(w, source, len);
  /* What the `RolltuiMenu` alone cannot do is re-resolve its FILE: that state (loaded/stamp/
   * origin/problem) is the widget ctx's. Asking for a menu before any window has shown it —
   * every `layout_test.cpp` case does — reads the file NOW rather than at the next draw. */
  void* ctx = menu_ctx_for_source(w, source, len);
  if (ctx) rolltui_menu_widget_ctx_refresh(ctx);
  return m;
}

const char* rolltui_windows_menu_origin(RolltuiWindows* w, const char* source, size_t len, size_t* out_len) {
  void* ctx;
  menu_for_source(w, source, len);
  ctx = menu_ctx_for_source(w, source, len);
  if (!ctx) {
    if (out_len) *out_len = 0;
    return "";
  }
  return rolltui_menu_widget_ctx_origin(ctx, out_len);
}

/* ---- …and the same three by WINDOW id -------------------------------------------------------
 *
 * The window's CONTENT is what says which kind it holds, so this parses it exactly as
 * `rolltui_windows_content_at` + `rolltui::parse_content` did on the C++ side: a window whose
 * content is a different kind (or does not parse at all) answers NULL rather than a widget of
 * the wrong type read through the right pointer. */
static void* typed_at(const RolltuiWindows* w, const RolltuiMap* by_source, const char* kind, size_t kind_n,
                      const char* window, size_t len) {
  WindowSlot* s = slot_of(w, window, len);
  unsigned char problem = 0;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  RolltuiStr why;
  void* out = NULL;
  if (!s) return NULL;
  memset(&why, 0, sizeof why);
  if (rolltui_content_parse(w->ctx, s->content.p ? s->content.p : "", s->content.n, &row, &is_host, &name, &name_len,
                            &source, &source_len, &problem, &why) &&
      !is_host && name_len == kind_n && memcmp(name, kind, kind_n) == 0)
    out = rolltui_map_get(by_source, source, source_len);
  rolltui_str_free(&why);
  return out;
}

RolltuiInput* rolltui_windows_input_at(const RolltuiWindows* w, const char* window, size_t len) {
  return (RolltuiInput*)typed_at(w, &w->inputs, "input", 5, window, len);
}
RolltuiTranscript* rolltui_windows_transcript_at(const RolltuiWindows* w, const char* window, size_t len) {
  return (RolltuiTranscript*)typed_at(w, &w->transcripts, "transcript", 10, window, len);
}
RolltuiMenu* rolltui_windows_menu_at(const RolltuiWindows* w, const char* window, size_t len) {
  return (RolltuiMenu*)typed_at(w, &w->menus, "menu", 4, window, len);
}

/* ---- what a host BINDS, by name ---------------------------------------------------------- */

void rolltui_windows_bind_document(RolltuiWindows* w, const char* name, size_t len, const RolltuiDocument* doc) {
  /* A BORROW: nothing to release on replace, unlike the callback maps below. */
  rolltui_map_put(&w->documents, name, len, (void*)doc);
}

const RolltuiDocument* rolltui_windows_document(const RolltuiWindows* w, const char* name, size_t len) {
  return (const RolltuiDocument*)rolltui_map_get(&w->documents, name, len);
}

void rolltui_windows_bind_sample_document(RolltuiWindows* w, const char* name, size_t len, const char* markdown,
                                          size_t markdown_len) {
  RolltuiDocument* d = (RolltuiDocument*)rolltui_map_get(&w->owned_documents, name, len);
  RolltuiDocEntry* e;
  if (!d) {
    /* STRATEGY 5 (GROWING HEAP): one document per named sample, for the table's life — the
     * borrow below needs an address that survives every later bind of another name. */
    d = (RolltuiDocument*)rolltui_mem_alloc(sizeof *d);
    memset(d, 0, sizeof *d);
    rolltui_map_put(&w->owned_documents, name, len, d);
  }
  /* Rebinding the same name REPLACES the sample; `clear` keeps the array, which is the whole
   * of why this is not a release-and-new. */
  rolltui_document_clear(d);
  e = rolltui_document_add(d);
  rolltui_str_set(&e->id, "sample", 6);
  rolltui_str_set(&e->text, markdown, markdown_len);
  rolltui_windows_bind_document(w, name, len, d);
}

void rolltui_windows_bind_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRowsFn fn, void* ctx,
                               void (*free_ctx)(void*)) {
  RowsBinding* b = (RowsBinding*)rolltui_mem_alloc(sizeof *b);
  RowsBinding* prev;
  b->fn = fn;
  b->ctx = ctx;
  b->free_ctx = free_ctx;
  prev = (RowsBinding*)rolltui_map_put(&w->rows, name, len, b);
  if (prev) {
    free_binding_ctx(prev->ctx, prev->free_ctx);
    rolltui_mem_free(prev);
  }
}

int rolltui_windows_has_rows(const RolltuiWindows* w, const char* name, size_t len) {
  return rolltui_map_get(&w->rows, name, len) != NULL;
}

int rolltui_windows_call_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRows* out) {
  RowsBinding* b = (RowsBinding*)rolltui_map_get(&w->rows, name, len);
  if (!b || !b->fn) return 0;
  b->fn(b->ctx, out);
  return 1;
}

void rolltui_windows_bind_submit(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                 void (*free_ctx)(void*), int on_submit) {
  SubmitBinding* b = (SubmitBinding*)rolltui_mem_alloc(sizeof *b);
  SubmitBinding* prev;
  b->fn = fn;
  b->ctx = ctx;
  b->free_ctx = free_ctx;
  b->on_submit = on_submit;
  prev = (SubmitBinding*)rolltui_map_put(&w->submits, name, len, b);
  if (prev) {
    free_binding_ctx(prev->ctx, prev->free_ctx);
    rolltui_mem_free(prev);
  }
}

int rolltui_windows_has_submit(const RolltuiWindows* w, const char* name, size_t len) {
  return rolltui_map_get(&w->submits, name, len) != NULL;
}

int rolltui_windows_call_submit(RolltuiWindows* w, const char* name, size_t len, const char* text, size_t tlen) {
  SubmitBinding* b = (SubmitBinding*)rolltui_map_get(&w->submits, name, len);
  if (!b || !b->fn) return 0;
  b->fn(b->ctx, text, tlen);
  return 1;
}

int rolltui_windows_on_submit(const RolltuiWindows* w, const char* name, size_t len) {
  SubmitBinding* b = (SubmitBinding*)rolltui_map_get(&w->submits, name, len);
  return b ? b->on_submit : 0; /* 0: SendAndClear, the default nothing-bound also means */
}

void rolltui_windows_bind_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNoteFn fn, void* ctx,
                               void (*free_ctx)(void*)) {
  NoteBinding* b = (NoteBinding*)rolltui_mem_alloc(sizeof *b);
  NoteBinding* prev;
  b->fn = fn;
  b->ctx = ctx;
  b->free_ctx = free_ctx;
  prev = (NoteBinding*)rolltui_map_put(&w->notes, name, len, b);
  if (prev) {
    free_binding_ctx(prev->ctx, prev->free_ctx);
    rolltui_mem_free(prev);
  }
}

int rolltui_windows_call_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNote* out) {
  NoteBinding* b = (NoteBinding*)rolltui_map_get(&w->notes, name, len);
  if (!b || !b->fn) return 0;
  b->fn(b->ctx, out);
  return 1;
}

void rolltui_context_set_dir(RolltuiContext* ctx, const char* dir, size_t len) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx); rolltui_str_set(&cfg->dir, dir, len); }

const char* rolltui_windows_dir(const RolltuiWindows* w, size_t* len) { return rolltui_str_get(&w->cfg->dir, len); }

void rolltui_context_add_menu(RolltuiContext* ctx, const char* name, size_t len, const char* json, size_t json_len) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  RolltuiStr* s = (RolltuiStr*)rolltui_map_get(&cfg->host_menus, name, len);
  if (!s) {
    s = (RolltuiStr*)rolltui_mem_alloc(sizeof *s);
    memset(s, 0, sizeof *s);
    rolltui_map_put(&cfg->host_menus, name, len, s);
  }
  rolltui_str_set(s, json, json_len);
}

const char* rolltui_windows_host_menu(const RolltuiWindows* w, const char* name, size_t len, size_t* out_len) {
  RolltuiStr* s = (RolltuiStr*)rolltui_map_get(&w->cfg->host_menus, name, len);
  if (!s) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  return rolltui_str_get(s, out_len);
}

size_t rolltui_windows_host_menu_count(const RolltuiWindows* w) { return rolltui_map_count(&w->cfg->host_menus); }

const char* rolltui_windows_host_menu_name_at(const RolltuiWindows* w, size_t i, size_t* len) {
  return rolltui_map_key_at(&w->cfg->host_menus, i, len);
}

/* ---- help (Phase 15 m5e: moved to the boundary so `help` can be a plugin) ---------------- */

void rolltui_context_set_help(RolltuiContext* ctx, const char* lead, size_t lead_len, const char* note,
                              size_t note_len) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  rolltui_str_set(&cfg->help_lead, lead, lead_len);
  rolltui_str_set(&cfg->help_note, note, note_len);
}

void rolltui_context_clear_help_scopes(RolltuiContext* ctx) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx); cfg->help_scopes_n = 0; /* keeps every buffer */ }

void rolltui_context_add_help_scope(RolltuiContext* ctx, const char* scope, size_t len) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  cfg->help_scopes = (RolltuiStr*)rolltui_grow_zeroed(cfg->help_scopes, &cfg->help_scopes_cap, cfg->help_scopes_n + 1,
                                                    sizeof *cfg->help_scopes);
  rolltui_str_set(&cfg->help_scopes[cfg->help_scopes_n++], scope, len);
}

size_t rolltui_windows_help_scope_count(const RolltuiWindows* w) { return w->cfg->help_scopes_n; }

const char* rolltui_windows_help_scope_at(const RolltuiWindows* w, size_t i, size_t* len) {
  if (i >= w->cfg->help_scopes_n) {
    if (len) *len = 0;
    return "";
  }
  return rolltui_str_get(&w->cfg->help_scopes[i], len);
}

const char* rolltui_windows_help_lead(const RolltuiWindows* w, size_t* len) { return rolltui_str_get(&w->cfg->help_lead, len); }
const char* rolltui_windows_help_note(const RolltuiWindows* w, size_t* len) { return rolltui_str_get(&w->cfg->help_note, len); }

/* ---- the live bindings table and the current frame's styles ------------------------------ */

void rolltui_context_set_bindings(RolltuiContext* ctx, const RolltuiBindings* b) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx); cfg->bindings = b; }
const RolltuiBindings* rolltui_windows_bindings(const RolltuiWindows* w) { return w->cfg->bindings; }
const RolltuiStyle* rolltui_windows_styles(const RolltuiWindows* w) { return w->styles; }

/* ---- what rolltui_widget_kinds.c's built-in kinds read back through ctx = this ----------- */

void rolltui_context_set_builtin_roles(RolltuiContext* ctx, const RolltuiBuiltinRoles* r) {
  rolltui_context_window_config(ctx)->builtin_roles = *r;
}
const RolltuiBuiltinRoles* rolltui_windows_builtin_roles(const RolltuiWindows* w) { return &w->cfg->builtin_roles; }
void rolltui_context_set_scroll_text_actions(RolltuiContext* ctx, const RolltuiScrollTextActions* a) {
  rolltui_context_window_config(ctx)->scroll_actions = *a;
}
const RolltuiScrollTextActions* rolltui_windows_scroll_text_actions(const RolltuiWindows* w) {
  return &w->cfg->scroll_actions;
}
void rolltui_context_set_code_fold(RolltuiContext* ctx, const RolltuiCodeFold* c) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx); cfg->code_fold = *c; }
const RolltuiCodeFold* rolltui_windows_code_fold(const RolltuiWindows* w) { return &w->cfg->code_fold; }
void rolltui_context_set_transcript_actions(RolltuiContext* ctx, const RolltuiTranscriptActions* a) {
  rolltui_context_window_config(ctx)->transcript_actions = *a;
}
const RolltuiTranscriptActions* rolltui_windows_transcript_actions(const RolltuiWindows* w) {
  return &w->cfg->transcript_actions;
}
void rolltui_context_set_menu_roles(RolltuiContext* ctx, const RolltuiMenuRoles* r) {
  rolltui_context_window_config(ctx)->menu_roles = *r;
}
const RolltuiMenuRoles* rolltui_windows_menu_roles(const RolltuiWindows* w) { return &w->cfg->menu_roles; }
void rolltui_context_set_input_actions(RolltuiContext* ctx, const RolltuiInputActions* a) {
  rolltui_context_window_config(ctx)->input_actions = a;
}
const RolltuiInputActions* rolltui_windows_input_actions(const RolltuiWindows* w) { return w->cfg->input_actions; }

/* The floor a host holds an input's window at whatever its text says — per-WINDOW sizing the
 * `input` plugin's ctx keeps, not part of the edited text `inputs` owns, so this reaches the
 * widget rather than the editor. Created on demand like any other `widget_for`: a host may set
 * the floor before a window has ever shown this input. */
void rolltui_windows_set_input_min_outer(RolltuiWindows* w, const char* source, size_t len, int rows) {
  RolltuiStr key;
  RolltuiWidget* wd;
  memset(&key, 0, sizeof key);
  rolltui_str_set(&key, "input:", 6);
  rolltui_str_append(&key, source, len);
  wd = rolltui_windows_widget_for(w, key.p, key.n);
  if (wd && wd->ctx && wd->vt == rolltui_input_widget_plugin()) rolltui_input_widget_ctx_set_min_outer(wd->ctx, rows);
  rolltui_str_free(&key);
}

/* ---- rows (Phase 15 m5e: moved to the boundary so `rows` can be a plugin) ---------------- */

void rolltui_rows_reset(RolltuiRows* r) { r->n = 0; /* keeps v's storage and every row's buffers */ }

void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len) {
  r->v = (RolltuiRow*)rolltui_grow_zeroed(r->v, &r->cap, r->n + 1, sizeof *r->v);
  rolltui_str_set(&r->v[r->n].label, label, label_len);
  rolltui_str_set(&r->v[r->n].value, value, value_len);
  ++r->n;
}

void rolltui_rows_release(RolltuiRows* r) {
  size_t i;
  for (i = 0; i < r->cap; ++i) {
    rolltui_str_free(&r->v[i].label);
    rolltui_str_free(&r->v[i].value);
  }
  rolltui_mem_free(r->v);
  r->v = NULL;
  r->n = 0;
  r->cap = 0;
}

/* ---- note (Phase 15 m5e: moved to the boundary so `input` can be a plugin) --------------- */

void rolltui_note_clear(RolltuiNote* n) {
  rolltui_str_clear(&n->text); /* keeps the buffer — the reuse this call exists for */
  n->state = 0;                /* EffectState::None */
  n->since_ms = 0;
}

/* ---- sync -------------------------------------------------------------------------------- */

static RolltuiStr* report_add(RolltuiWindows* w) {
  w->report = (RolltuiStr*)rolltui_grow_zeroed(w->report, &w->report_cap, w->report_n + 1, sizeof *w->report);
  return &w->report[w->report_n++];
}

size_t rolltui_windows_report_count(const RolltuiWindows* w) { return w->report_n; }

const char* rolltui_windows_report_at(const RolltuiWindows* w, size_t i, size_t* len) {
  if (i >= w->report_n) {
    if (len) *len = 0;
    return "";
  }
  return rolltui_str_get(&w->report[i], len);
}

static void note_problem(RolltuiWindows* w, const RolltuiLayoutNode* n, const RolltuiStr* what) {
  RolltuiStr* line = report_add(w);
  rolltui_str_clear(line);
  rolltui_str_append(line, "window '", 8);
  rolltui_str_append_str(line, &n->id);
  rolltui_str_append(line, "' (content '", 12);
  rolltui_str_append_str(line, &n->content);
  rolltui_str_append(line, "'): ", 4);
  rolltui_str_append_str(line, what);
}

static void sync_node(RolltuiWindows* w, const RolltuiLayoutNode* n) {
  RolltuiWidget* wd;
  WindowSlot* s;
  size_t i;
  if (n->kind != ROLLTUI_NODE_WINDOW) {
    for (i = 0; i < n->children.n; ++i) sync_node(w, n->children.v[i]);
    return;
  }
  wd = rolltui_windows_widget_for(w, n->content.p, n->content.n);
  s = slot_of(w, n->id.p, n->id.n);
  if (!s) {
    s = (WindowSlot*)rolltui_mem_alloc(sizeof *s);
    memset(s, 0, sizeof *s);
    rolltui_map_put(&w->by_window, n->id.p, n->id.n, s);
  }
  s->widget = wd;
  s->live = 1;
  rolltui_str_set(&s->content, n->content.p, n->content.n);
  /* THE "window 'x' (content 'y'): " PREFIX IS BUILT ONLY WHEN THERE IS SOMETHING TO SAY.
   * It used to be built for every window of every frame and thrown away. */
  if (wd->vt && wd->vt->problem && wd->vt->problem(wd->ctx, &w->scratch)) {
    note_problem(w, n, &w->scratch);
    return;
  }
  if (wd->vt && wd->vt->note_at)
    for (i = 0; wd->vt->note_at(wd->ctx, i, &w->scratch); ++i) note_problem(w, n, &w->scratch);
}

void rolltui_windows_sync(RolltuiWindows* w, const RolltuiWindowStack* stack) {
  const size_t depth = rolltui_window_stack_depth(stack);
  size_t i;
  w->report_n = 0;
  /* THE MAP IS REBUILT IN PLACE, not cleared: a `clear()` destroys every node and the next
   * frame allocates them again, three a frame, for a window set that almost never changes. */
  for (i = 0; i < rolltui_map_count(&w->by_window); ++i)
    ((WindowSlot*)rolltui_map_value_at(&w->by_window, i))->live = 0;
  for (i = 0; i < depth; ++i) sync_node(w, &rolltui_window_stack_layer(stack, i)->root);
  for (i = rolltui_map_count(&w->by_window); i-- > 0;) {
    WindowSlot* s = (WindowSlot*)rolltui_map_value_at(&w->by_window, i);
    if (s->live) continue;
    rolltui_map_remove_at(&w->by_window, i);
    rolltui_str_free(&s->content);
    rolltui_mem_free(s);
  }
}

/* ---- the per-frame resolve buffer -------------------------------------------------------- */

static void collect(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiWindows* w = (RolltuiWindows*)ctx;
  w->nodes = (RolltuiResolvedNode*)rolltui_grow(w->nodes, &w->node_cap, w->node_n + 1, sizeof *w->nodes);
  w->nodes[w->node_n++] = *rn;
}

static void resolve_into(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box) {
  w->node_n = 0;
  rolltui_window_stack_resolve(stack, box, collect, w);
}

void rolltui_windows_autosize(RolltuiWindows* w, RolltuiWindowStack* stack, RolltuiRect box) {
  size_t i, j;
  resolve_into(w, stack, box);
  for (i = 0; i < w->node_n; ++i) {
    const RolltuiResolvedNode* rn = &w->nodes[i];
    RolltuiWidget* wd;
    const RolltuiResolvedNode* parent = NULL;
    int row, extent, border, want = 0;
    if (rn->node->kind != 0) continue;
    wd = rolltui_windows_at(w, rn->node->id.p, rn->node->id.n);
    if (!wd || !wd->vt || !wd->vt->desired_outer) continue;
    /* The parent split is the INNERMOST one that contains this window — the last in tree
     * order, since a container precedes its children and siblings never overlap. */
    for (j = 0; j < w->node_n; ++j) {
      const RolltuiResolvedNode* p = &w->nodes[j];
      if (p->node->kind != 0 && p->layer == rn->layer &&
          rn->outer.x >= p->inner.x && rn->outer.y >= p->inner.y &&
          rn->outer.x < p->inner.x + p->inner.w && rn->outer.y < p->inner.y + p->inner.h)
        parent = p;
    }
    row = parent && parent->node->kind == 1 /* Row */;
    extent = !parent ? box.h : (row ? parent->inner.w : parent->inner.h);
    border = rn->node->border != 0 ? 2 : 0;
    if (wd->vt->desired_outer(wd->ctx, rn->inner.w, extent, border, &want)) {
      RolltuiLayoutNode* nd = rolltui_window_stack_find(stack, rn->node->id.p, rn->node->id.n);
      if (nd) {
        nd->size.fill = 0;
        nd->size.weight = 1;
        nd->size.dim.fraction = 0;
        nd->size.dim.cells = want;
      }
    }
  }
}

void rolltui_windows_layout(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box) {
  size_t i;
  resolve_into(w, stack, box);
  for (i = 0; i < w->node_n; ++i) {
    const RolltuiResolvedNode* rn = &w->nodes[i];
    RolltuiWidget* wd;
    if (rn->node->kind != 0) continue;
    wd = rolltui_windows_at(w, rn->node->id.p, rn->node->id.n);
    if (wd && wd->vt && wd->vt->layout) wd->vt->layout(wd->ctx, rn);
  }
}

/* ---- drawing ----------------------------------------------------------------------------- */

/* The bar lives in the window's RIGHT BORDER COLUMN, which is why the WINDOW draws it and not
 * the widget: a widget is handed a content rect and knows nothing about whether it has a
 * border. A window without a border has no track and gets no bar. */
static void draw_scrollbar(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiWidget* wd,
                           RolltuiFrame* f, const RolltuiStyle* styles, const RolltuiWindowRoles* roles) {
  WindowSlot* s = slot_of(w, rn->node->id.p, rn->node->id.n);
  RolltuiScrollExtent e;
  RolltuiScrollThumb t;
  RolltuiStyle style, ground;
  int track, x, i;
  const char* thumb;
  /* THE TRACK IS ZEROED, NOT ERASED: an erase-and-reinsert destroys a table node and
   * allocates a new one every frame for every window with a scrollbar. `h == 0` is what "no
   * track this frame" means. */
  if (s) {
    s->track.x = 0;
    s->track.y = 0;
    s->track.h = 0;
  }
  if (rn->node->border == 0) return;
  if (!wd->vt || !wd->vt->scroll_extent) return;
  memset(&e, 0, sizeof e);
  if (!wd->vt->scroll_extent(wd->ctx, ROLLTUI_AXIS_VERTICAL, &e)) return;
  track = rn->outer.h - 2; /* between the corners */
  x = rn->outer.x + rn->outer.w - 1;
  if (track <= 0 || rn->outer.w < 2) return;
  if (!rolltui_scroll_thumb(&e, track, &t)) return;
  if (s) {
    s->track.x = x;
    s->track.y = rn->outer.y + 1;
    s->track.h = track;
  }
  style = styles[roles->scrollbar];
  ground = styles[rn->node->background];
  if (style.bg.kind == 0 /* Color::Kind::None */) style.bg = ground.bg;
  /* █ (U+2588) is East Asian AMBIGUOUS, exactly like the box-drawing set the border is made
   * of — so it follows the border's rule: with `ambiguous_wide` the thumb is ASCII. */
  thumb = w->cfg->env.ambiguous_wide ? "#" : "\xE2\x96\x88";
  for (i = 0; i < t.length; ++i) {
    const int y = rn->outer.y + 1 + t.offset + i;
    if (y >= rn->outer.y + rn->outer.h - 1) break;
    rolltui_frame_put(f, x, y, thumb, strlen(thumb), 1, style, 0);
  }
}

/* The three bytes the WINDOW itself paints with, NAMED from the role list —
 * see the header for why they stopped being `Widgets.cpp`'s. */
const RolltuiWindowRoles* rolltui_windows_default_roles(void) {
  static const RolltuiWindowRoles r = {
      /*scrollbar=*/ROLLTUI_ROLE_SCROLLBAR,
      /*border=*/ROLLTUI_ROLE_BORDER,
      /*border_active=*/ROLLTUI_ROLE_BORDER_ACTIVE,
  };
  return &r;
}

void rolltui_windows_draw(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                          const RolltuiStyle* styles, const RolltuiWindowRoles* roles) {
  RolltuiWidget* wd;
  w->styles = styles; /* the current frame's table, so a widget's draw may ask for a Role */
  if (rn->node->kind != 0) return;
  wd = rolltui_windows_at(w, rn->node->id.p, rn->node->id.n);
  if (!wd || !wd->vt) return;
  /* A widget that CANNOT draw is replaced by the error panel — the factory's, so there is one
   * definition of what "this window is wrong" looks like. */
  if (wd->vt->problem && wd->vt->problem(wd->ctx, &w->scratch)) {
    if (w->cfg->panel_factory) {
      RolltuiWidget err = w->cfg->panel_factory(w->cfg->panel_ctx, w, w->scratch.p, w->scratch.n);
      if (err.vt) {
        if (err.vt->layout) err.vt->layout(err.ctx, rn);
        err.vt->draw(err.ctx, rn, f);
        if (err.vt->destroy) err.vt->destroy(err.ctx);
      }
    }
    return;
  }
  wd->vt->draw(wd->ctx, rn, f);
  draw_scrollbar(w, rn, wd, f, styles, roles);
}

/* ---- events ------------------------------------------------------------------------------ */

/* A press in the track column drives the widget — but ONLY one that accepted `scroll_to`. One
 * that merely reports gets an accurate bar that is not a handle, which is the whole reason the
 * scroll capability is two optional slots (rule 4). */
static int handle_scrollbar(RolltuiWindows* w, const char* window, size_t len, RolltuiWidget* wd,
                            const RolltuiEvent* ev) {
  const RolltuiMouseEvent* m;
  WindowSlot* s;
  RolltuiScrollExtent e;
  RolltuiScrollThumb t;
  if (ev->kind != ROLLTUI_EVENT_MOUSE) return 0;
  m = &ev->mouse;
  if (m->kind == 1 /* Release */) {
    if (!rolltui_str_eq(&w->bar_drag, window, len)) return 0;
    rolltui_str_clear(&w->bar_drag);
    return 1;
  }
  s = slot_of(w, window, len);
  if (!s || s->track.h <= 0) return 0; /* h == 0: no track drawn */
  if (!wd->vt || !wd->vt->scroll_extent) return 0;
  memset(&e, 0, sizeof e);
  if (!wd->vt->scroll_extent(wd->ctx, ROLLTUI_AXIS_VERTICAL, &e)) return 0;
  if (!rolltui_scroll_thumb(&e, s->track.h, &t)) return 0;
  if (m->kind == 0 /* Press */) {
    int cell;
    if (m->button != 1 || m->x != s->track.x || m->y < s->track.y || m->y >= s->track.y + s->track.h) return 0;
    cell = m->y - s->track.y;
    /* On the thumb: grab it where it was taken, so it does not jump under the pointer. In the
     * trough: jump so the thumb's START lands there, which is the one rule that makes a click
     * and the drag that may follow it agree. */
    w->bar_grab = (cell >= t.offset && cell < t.offset + t.length) ? cell - t.offset : 0;
    if (!wd->vt->scroll_to) return 0;
    if (!wd->vt->scroll_to(wd->ctx, ROLLTUI_AXIS_VERTICAL,
                           rolltui_scroll_first_for_cell(&e, s->track.h, cell - w->bar_grab)))
      return 0;
    rolltui_str_set(&w->bar_drag, window, len);
    return 1;
  }
  if (m->kind == 2 /* Drag */) {
    if (!rolltui_str_eq(&w->bar_drag, window, len)) return 0;
    /* The pointer may be anywhere by now (the press captured it), so only its ROW counts. */
    if (wd->vt->scroll_to)
      wd->vt->scroll_to(wd->ctx, ROLLTUI_AXIS_VERTICAL,
                        rolltui_scroll_first_for_cell(&e, s->track.h, m->y - s->track.y - w->bar_grab));
    return 1;
  }
  return 0;
}

int rolltui_windows_handle(RolltuiWindows* w, const char* window, size_t len, const RolltuiEvent* e) {
  RolltuiWidget* wd = rolltui_windows_at(w, window, len);
  if (!wd || !wd->vt) return 0;
  if (wd->vt->problem && wd->vt->problem(wd->ctx, &w->scratch)) return 0;
  if (handle_scrollbar(w, window, len, wd, e)) return 1;
  return wd->vt->handle ? wd->vt->handle(wd->ctx, e) : 0;
}

/* ---- the report's one-line form ---------------------------------------------------------- */
void rolltui_windows_report_summary(const RolltuiWindows* w, RolltuiStr* out) {
  const size_t n = rolltui_windows_report_count(w);
  size_t flen = 0;
  const char* first;
  if (!out || n == 0) return;
  first = rolltui_windows_report_at(w, 0, &flen);
  rolltui_str_append(out, first, flen);
  if (n > 1) {
    char buf[32];
    const int k = snprintf(buf, sizeof buf, " (+%zu more)", n - 1);
    if (k > 0) rolltui_str_append(out, buf, (size_t)k);
  }
}
