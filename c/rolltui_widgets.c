/* rolltui/c/rolltui_widgets.c — the C side of the widget vtable's host. See
 * rolltui_widgets.h; the rules are rolltui/Widgets.hpp's. */
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_feedback.h"
#include "rolltui/c/rolltui_widget_list.h"
#include "rolltui/c/rolltui_syntax.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_widget_input.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_widget_transcript.h"
#include "rolltui/testkit/testctl.h"

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
  /* ON = the reservation gone, so the rounded position stands as-is: a view one line from
   * the top rounds to offset 0 and the thumb touches the end while the document does not.
   * The thumb is still inside the track and still the right length - it simply lies about
   * being at the end, which is the one thing a scrollbar is for. */
  if (testkit_ctl_on("widgets.scrollbar_ends_are_not_reserved")) {
    /* the rounded offset stands */
  } else if (span >= 2) {
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

typedef struct GaugeBinding {
  RolltuiGaugeFn fn;
  void* ctx;
  void (*free_ctx)(void*);
} GaugeBinding;
typedef struct ChartBinding {
  RolltuiChartFn fn;
  void* ctx;
  void (*free_ctx)(void*);
  RolltuiChart* chart; /* OWNED: refilled before each frame */
} ChartBinding;

/* A PROGRAM RUN INTO AN OUTPUT WINDOW: its handle (OWNED; NULL once it ended), its label and role, what it wrote since its last
 * newline (merged with others, a line goes in whole), and how it ended. */
typedef struct OutputSource {
  RolltuiChild* child;
  RolltuiStr label;
  unsigned char role;
  RolltuiStr partial;
} OutputSource;

typedef struct OutputBinding {
  const RolltuiAnsiText* text; /* BORROWED: the host's, or `owned` */
  RolltuiAnsiText* owned;      /* OWNED: what the programs run into it wrote */
  OutputSource* src;           /* OWNED */
  size_t src_n, src_cap;
  int cols, rows;              /* the size the programs were last told */
  int status;                  /* the last to end: its exit status, -1 stopped */
  int shown;                   /* a window showed it at a sync: one that no longer does stops its programs */
  int opt_set;                 /* the host set its options */
  RolltuiOutputOptions opt;    /* `filter` points at `filter_text` */
  RolltuiStr filter_text;
  RolltuiFilter* filter;       /* OWNED, made from `filter_text` */
  unsigned long long filter_version;
  unsigned char wrap;
} OutputBinding;

/* `content` is `output:<name>`, the name `name`. */
static int content_is_output(const char* content, size_t cl, const char* name, size_t len) {
  return cl == 7 + len && memcmp(content, "output:", 7) == 0 && memcmp(content + 7, name, len) == 0;
}
static void output_stop(OutputBinding* b, int status) {
  size_t i;
  for (i = 0; i < b->src_n; ++i) {
    if (b->src[i].child) {
      rolltui_child_free(b->src[i].child); /* stops it */
      b->src[i].child = NULL;
      b->status = status;
    }
  }
}

static void output_binding_free(OutputBinding* b) {
  size_t i;
  if (!b) return;
  output_stop(b, -1);
  for (i = 0; i < b->src_n; ++i) {
    rolltui_str_free(&b->src[i].label);
    rolltui_str_free(&b->src[i].partial);
  }
  rolltui_mem_free(b->src);
  rolltui_ansi_free(b->owned);
  rolltui_str_free(&b->filter_text);
  rolltui_filter_free(b->filter);
  rolltui_mem_free(b);
}

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
/* A LINK A WINDOW SHOWED: a run of its cells on one row, the frame's id for it then, and its URL in the slot's `link_urls`. */
typedef struct LinkRun {
  int x, y, w;
  unsigned int id;
  size_t url;
} LinkRun;

typedef struct WindowSlot {
  RolltuiWidget* widget;
  RolltuiStr content;
  Track track;
  int live; /* this sync found it */
  /* VIEWS PUSHED INTO IT, the top drawn in its place: each a content and a title, NUL after each, and its widget (BORROWED from
   * `by_content`, as `widget` is) */
  RolltuiStr views;
  RolltuiWidget** view_w;
  size_t view_n, view_cap;
  /* THE LINKS ITS LAST DRAW SHOWED, for a press and the pointer: runs, and their URLs a NUL after each */
  LinkRun* links;
  size_t link_n, link_cap;
  RolltuiStr link_urls;
} WindowSlot;

static void slot_free(WindowSlot* s) {
  rolltui_str_free(&s->content);
  rolltui_str_free(&s->views);
  rolltui_mem_free(s->view_w);
  rolltui_mem_free(s->links);
  rolltui_str_free(&s->link_urls);
  rolltui_mem_free(s);
}

/* View `i` of a slot: its content and its title. 0 past the last. */
static int slot_view(const WindowSlot* s, size_t i, const char** content, size_t* cl, const char** title, size_t* tl) {
  const char* p = s->views.p;
  size_t k;
  if (!p || i >= s->view_n) return 0;
  for (k = 0; k < i; ++k) {
    p += strlen(p) + 1;
    p += strlen(p) + 1;
  }
  *content = p;
  *cl = strlen(p);
  p += *cl + 1;
  *title = p;
  *tl = strlen(p);
  return 1;
}


/* ---- WHAT A SESSION CONFIGURES, ONCE --------------------------------------------------------
 * The half of `RolltuiWindows` that was never about what is on screen: a program's widget-kind
 * factories, its menus, its key table, its help scopes, its highlighter, and the vocabularies the
 * built-in kinds read back. Set at startup and identical for every screen the program runs, which
 * is the definition of a session's rather than a screen's — so it is owned by the CONTEXT and a
 * `RolltuiWindows` borrows it.
 *
 * THE FACTORY HALF OF RUNG 2 LIVES WITH THE OTHER HALF. A host kind's NAME, its source rule and
 * its FACTORY are one registration; splitting them across the context and this file makes one
 * registration owned by two things, which is the "TWO SPELLINGS of one identity" shape CLAUDE.md
 * names.
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
  RolltuiScrollbarGlyphs scrollbar_glyphs;
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
  /* A program that sets no glyphs still draws a shaped thumb; zeroed slots would draw nothing. */
  rolltui_scrollbar_glyphs_default(&c->scrollbar_glyphs);
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
  const RolltuiWindowStack* stack; /* BORROWED: the stack last synced, which says which window has focus */
  int want_ms; /* the soonest a window drawn this frame wants the next (`rolltui_windows_want_frame`); -1 none */
  struct RolltuiPresetStore* keys_store; /* BORROWED: the bindings store a host handed over for its keys, which every keys sheet edits into */
  int keys_persist;
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
  /* …and the OWNED half: name ->
   * RolltuiDocumentRaw*, OWNED. A sample a tool binds from an app profile has no live document to
   * point at, so this table keeps one; `documents` above then carries the borrow of it, which is
   * why the two maps are separate rather than one map with an ownership flag. */
  RolltuiMap owned_documents;
  RolltuiMap rows;       /* name -> RowsBinding*, OWNED */
  RolltuiMap gauges;     /* name -> GaugeBinding*, OWNED */
  RolltuiMap charts;     /* name -> ChartBinding*, OWNED: the fill and the chart it fills */
  RolltuiMap outputs;    /* name -> OutputBinding*, OWNED (the text it names is the host's) */
  RolltuiMap images;     /* name -> const RolltuiImage*, BORROWED (the host's) */
  RolltuiMap dialogs;    /* id -> RolltuiDialog*, OWNED: asked and not yet answered */
  unsigned char timing;  /* each window's draw time in its top border (`rolltui_windows_set_timing`, ROLLTUI_TIMING=1) */
  RolltuiWindowsLinkFn link_fn; /* a press on a link, offered to the host first (`rolltui_windows_set_link_handler`) */
  void* link_ctx;
  RolltuiDrawScratch* scratch_draw; /* OWNED: the timing label's, made when it is first drawn */
  RolltuiMap lists;      /* name -> RolltuiList*, OWNED: made when a host binds one or a window first shows one */
  RolltuiTasks* tasks;   /* BORROWED: the workers the built-in kinds read files on; NULL reads on this thread */
  /* INPUT HELD (`rolltui_windows_hold`): while `held`, keys and the pointer reach no window; `hold_job` is cancelled by Escape and
   * ends the hold when it ends */
  int held;
  unsigned int hold_job;
  RolltuiStr hold_label;
  RolltuiMessageLog messages; /* what the app posted, and the toasts showing it */
  /* A KEY SEQUENCE ON THE WAY in one window's scope: the keys pressed so far, a count typed before them, and since when. */
  RolltuiChord seq_keys[ROLLTUI_SEQUENCE_MAX];
  size_t seq_n;
  int seq_count;
  int count_arg; /* a count handed to the next key as a row (`5` then `list.last`): read by the widget while it handles that key */
  RolltuiChord any_key; /* the key an `any` slot or an ask took */
  int has_any;
  RolltuiStr ask_action, ask_prompt; /* a key asked for (`rolltui_windows_ask_key`); `asking` while it is awaited */
  int asking;
  RolltuiStr seq_window;
  char seq_scope[32];
  size_t seq_scope_n;
  unsigned long long seq_since;
  RolltuiMap submits;    /* name -> SubmitBinding*, OWNED */
  RolltuiMap changes;    /* name -> SubmitBinding*, OWNED: what an input's text changing calls */
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
  unsigned char highlight_on_block; /* the library's own languages stand on the block; a host's highlighter does not */
  /* THE LIBRARY'S LANGUAGES, read once and shared: the default highlighter's, and what an input that names a `language` is
   * highlighted by. Made on first use, so a screen that shows no code pays nothing for them. */
  RolltuiSyntax* syntax;

  RolltuiMap inputs;      /* source -> RolltuiInput*, OWNED */
  RolltuiMap transcripts; /* source -> RolltuiTranscript*, OWNED */
  RolltuiMap menus;       /* source -> RolltuiMenu*, OWNED */
};

/* THE LIBRARY'S OWN LANGUAGES, the highlighter a table starts with: the shipped ones and the person's from
 * `<config>/rolltui/syntax/`, the set the picker's preview uses. Parsed when the first code line is drawn, so a screen that
 * shows no code pays nothing for them. */
typedef struct DefaultHighlight {
  RolltuiWindows* w; /* BORROWED: whose languages it reads */
  RolltuiSyntaxMd* md;
} DefaultHighlight;

static void default_highlight(void* ctx, const char* lang, size_t lang_n, const RolltuiMdCodeLine* lines, size_t line_count,
                              size_t index, RolltuiMdSpanSink emit, void* sink) {
  DefaultHighlight* d = (DefaultHighlight*)ctx;
  if (!d->md) d->md = rolltui_syntax_md_new(rolltui_windows_syntax(d->w), rolltui_md_roles()->code_block);
  rolltui_syntax_md_highlight(d->md, lang, lang_n, lines, line_count, index, emit, sink);
}

static void default_highlight_free(void* ctx) {
  DefaultHighlight* d = (DefaultHighlight*)ctx;
  if (!d) return;
  rolltui_syntax_md_free(d->md);
  rolltui_mem_free(d);
}

RolltuiWindows* rolltui_windows_new(RolltuiContext* ctx) {
  RolltuiWindows* w = (RolltuiWindows*)rolltui_mem_alloc(sizeof *w);
  memset(w, 0, sizeof *w);
  w->want_ms = -1;
  w->ctx = ctx;
  {
    const char* t = getenv("ROLLTUI_TIMING");
    w->timing = (unsigned char)(t && *t && strcmp(t, "0") != 0);
  }
  w->highlight_fn = default_highlight;
  w->highlight_ctx = rolltui_mem_alloc(sizeof(DefaultHighlight));
  memset(w->highlight_ctx, 0, sizeof(DefaultHighlight));
  ((DefaultHighlight*)w->highlight_ctx)->w = w;
  w->highlight_free_ctx = default_highlight_free;
  w->highlight_on_block = 1;
  /* The session's configuration, made on first use like every other subsystem it owns. A second
   * `RolltuiWindows` on the same context gets the SAME one, which is the point of the split:
   * two screens in one session are configured once, not twice. */
  w->cfg = rolltui_context_window_config(ctx);
  rolltui_message_log_init(&w->messages);
  return w;
}

RolltuiSyntax* rolltui_windows_syntax(RolltuiWindows* w) {
  if (!w) return NULL;
  if (!w->syntax) w->syntax = rolltui_syntax_new_standard(NULL);
  return w->syntax;
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
  rolltui_str_free(&w->hold_label);
  for (i = 0; i < rolltui_map_count(&w->by_content); ++i)
    widget_destroy((RolltuiWidget*)rolltui_map_value_at(&w->by_content, i));
  rolltui_map_release(&w->by_content);
  for (i = 0; i < rolltui_map_count(&w->by_window); ++i) slot_free((WindowSlot*)rolltui_map_value_at(&w->by_window, i));
  rolltui_map_release(&w->by_window);
  for (i = 0; i < w->report_cap; ++i) rolltui_str_free(&w->report[i]);
  rolltui_mem_free(w->report);
  rolltui_str_free(&w->scratch);
  rolltui_str_free(&w->ask_action);
  rolltui_str_free(&w->ask_prompt);
  rolltui_str_free(&w->seq_window);
  rolltui_str_free(&w->bar_drag);
  rolltui_mem_free(w->nodes);
  free_binding_ctx(w->highlight_ctx, w->highlight_free_ctx);
  rolltui_syntax_free(w->syntax); /* after the highlighter that borrowed it */
  /* the host-binding surface: `documents` is BORROWS only, nothing to free per entry —
   * but `owned_documents` holds the samples those borrows may point INTO, so it is released
   * after it rather than before. */
  rolltui_map_release(&w->documents);
  for (i = 0; i < rolltui_map_count(&w->owned_documents); ++i) {
    RolltuiDocumentRaw* d = (RolltuiDocumentRaw*)rolltui_map_value_at(&w->owned_documents, i);
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
  for (i = 0; i < rolltui_map_count(&w->gauges); ++i) {
    GaugeBinding* b = (GaugeBinding*)rolltui_map_value_at(&w->gauges, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->gauges);
  for (i = 0; i < rolltui_map_count(&w->charts); ++i) {
    ChartBinding* b = (ChartBinding*)rolltui_map_value_at(&w->charts, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_chart_free(b->chart);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->charts);
  for (i = 0; i < rolltui_map_count(&w->outputs); ++i) output_binding_free((OutputBinding*)rolltui_map_value_at(&w->outputs, i));
  rolltui_map_release(&w->outputs);
  rolltui_map_release(&w->images);
  rolltui_draw_scratch_free(w->scratch_draw);
  {
    size_t i;
    for (i = 0; i < rolltui_map_count(&w->dialogs); ++i) rolltui_dialog_free((RolltuiDialog*)rolltui_map_value_at(&w->dialogs, i));
    rolltui_map_release(&w->dialogs);
  }
  for (i = 0; i < rolltui_map_count(&w->lists); ++i) rolltui_list_free((RolltuiList*)rolltui_map_value_at(&w->lists, i));
  rolltui_map_release(&w->lists);
  rolltui_message_log_release(&w->messages);
  for (i = 0; i < rolltui_map_count(&w->submits); ++i) {
    SubmitBinding* b = (SubmitBinding*)rolltui_map_value_at(&w->submits, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  for (i = 0; i < rolltui_map_count(&w->changes); ++i) {
    SubmitBinding* b = (SubmitBinding*)rolltui_map_value_at(&w->changes, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->changes);
  rolltui_map_release(&w->submits);
  for (i = 0; i < rolltui_map_count(&w->notes); ++i) {
    NoteBinding* b = (NoteBinding*)rolltui_map_value_at(&w->notes, i);
    free_binding_ctx(b->ctx, b->free_ctx);
    rolltui_mem_free(b);
  }
  rolltui_map_release(&w->notes);
  /* The typed widgets, AFTER `by_content` above: every widget ctx BORROWS one of these,
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
  /* ON = the lookup always misses, so a content string builds a FRESH widget every time it is
   * asked for instead of returning the one this table already owns. Nothing crashes and every
   * widget drawn is a valid widget - it is a new one, so scroll position, selection and edit
   * state are reset on every frame that asks. */
  RolltuiWidget* wd = testkit_ctl_on("widgets.widget_lookup_always_misses")
                          ? NULL
                          : (RolltuiWidget*)rolltui_map_get(&w->by_content, content, len);
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
  {
    /* A row this replaces is freed. Only a lookup that missed a row it holds (the control above)
     * replaces one; `wd` is allocated first, so the freed address is never the one handed back. */
    RolltuiWidget* replaced = (RolltuiWidget*)rolltui_map_put(&w->by_content, content, len, wd);
    if (replaced) widget_destroy(replaced);
  }
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
  if (s && s->view_n) return s->view_w[s->view_n - 1]; /* a pushed view, in the window's place */
  return s ? s->widget : NULL;
}

int rolltui_windows_push_view(RolltuiWindows* w, const char* window, size_t len, const char* content, size_t clen, const char* title,
                              size_t tlen) {
  WindowSlot* s = w && window ? slot_of(w, window, len) : NULL;
  RolltuiWidget* wd;
  if (!s || !content || clen == 0 || memchr(content, 0, clen) || (title && memchr(title, 0, tlen))) return 0;
  wd = rolltui_windows_widget_for(w, content, clen);
  s->view_w = (RolltuiWidget**)rolltui_grow(s->view_w, &s->view_cap, s->view_n + 1, sizeof *s->view_w);
  s->view_w[s->view_n++] = wd;
  rolltui_str_append(&s->views, content, clen);
  rolltui_str_append(&s->views, "", 1);
  rolltui_str_append(&s->views, title ? title : "", title ? tlen : 0);
  rolltui_str_append(&s->views, "", 1);
  return 1;
}

int rolltui_windows_pop_view(RolltuiWindows* w, const char* window, size_t len) {
  WindowSlot* s = w && window ? slot_of(w, window, len) : NULL;
  const char *c, *t;
  size_t cl, tl;
  if (!s || !s->view_n || !slot_view(s, s->view_n - 1, &c, &cl, &t, &tl)) return 0;
  s->views.n = (size_t)(c - s->views.p); /* the last content and title go */
  if (s->views.p) s->views.p[s->views.n] = 0;
  --s->view_n;
  return 1;
}

size_t rolltui_windows_view_depth(const RolltuiWindows* w, const char* window, size_t len) {
  const WindowSlot* s = w && window ? slot_of(w, window, len) : NULL;
  return s ? s->view_n : 0;
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

/* ---- THE TYPED WIDGETS, OWNED HERE (the header states why) ----------------------------------
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
  t = rolltui_transcript_new(); /* its roles are its own defaults now (rolltui_widget_transcript.c) */
  /* The live highlighter, applied at CREATION so a transcript first asked for after
   * `set_highlight` is not silently the one that misses it. */
  if (w->highlight_fn) rolltui_transcript_set_highlight(t, w->highlight_fn, w->highlight_ctx, w->highlight_on_block);
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
  w->highlight_on_block = 0;
  /* PUSHED onto every transcript that already exists, rather than left for each to pull on
   * its own next frame: nothing here polls an epoch, so nothing is left to miss a change. */
  for (i = 0; i < rolltui_map_count(&w->transcripts); ++i)
    rolltui_transcript_set_highlight((RolltuiTranscript*)rolltui_map_value_at(&w->transcripts, i), fn, ctx, 0);
}

/* The menu's own map half, WITHOUT the file refresh — this is what the `menu` factory calls,
 * and it must not reach back into `rolltui_windows_widget_for` (which is what built it). */
static RolltuiMenu* menu_for_source(RolltuiWindows* w, const char* source, size_t len) {
  RolltuiMenu* m = (RolltuiMenu*)rolltui_map_get(&w->menus, source, len);
  if (m) return m;
  m = rolltui_menu_new(); /* single-line editor, no prompt: the menu's own (rolltui_widget_menu.c) */
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
int rolltui_windows_window_rect(const RolltuiWindows* w, const char* window, size_t len, RolltuiRect* out) {
  size_t i;
  for (i = 0; i < w->node_n; ++i) {
    const RolltuiResolvedNode* rn = &w->nodes[i];
    if (rn->node->kind == 0 && rolltui_str_eq(&rn->node->id, window, len)) { *out = rn->outer; return 1; }
  }
  return 0;
}

int rolltui_windows_back(void* windows, const char* window, size_t len) {
  RolltuiWindows* w = (RolltuiWindows*)windows;
  RolltuiWidget* wd = rolltui_windows_at(w, window, len);
  if (wd && wd->vt && wd->vt->back && wd->vt->back(wd->ctx)) return 1;
  return rolltui_windows_pop_view(w, window, len); /* the view has nothing left to back out of: it is the level, and goes */
}

RolltuiMenu* rolltui_windows_menu_at(const RolltuiWindows* w, const char* window, size_t len) {
  return (RolltuiMenu*)typed_at(w, &w->menus, "menu", 4, window, len);
}

/* ---- what a host BINDS, by name ---------------------------------------------------------- */

void rolltui_windows_bind_document(RolltuiWindows* w, const char* name, size_t len, const RolltuiDocumentRaw* doc) {
  /* A BORROW: nothing to release on replace, unlike the callback maps below. */
  rolltui_map_put(&w->documents, name, len, (void*)doc);
}

const RolltuiDocumentRaw* rolltui_windows_document(const RolltuiWindows* w, const char* name, size_t len) {
  return (const RolltuiDocumentRaw*)rolltui_map_get(&w->documents, name, len);
}

void rolltui_windows_bind_sample_document(RolltuiWindows* w, const char* name, size_t len, const char* markdown,
                                          size_t markdown_len) {
  RolltuiDocumentRaw* d = (RolltuiDocumentRaw*)rolltui_map_get(&w->owned_documents, name, len);
  RolltuiDocEntryRaw* e;
  if (!d) {
    /* STRATEGY 5 (GROWING HEAP): one document per named sample, for the table's life — the
     * borrow below needs an address that survives every later bind of another name. */
    d = (RolltuiDocumentRaw*)rolltui_mem_alloc(sizeof *d);
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

void rolltui_windows_bind_chart(RolltuiWindows* w, const char* name, size_t len, RolltuiChartFn fn, void* ctx,
                                void (*free_ctx)(void*)) {
  ChartBinding* b = (ChartBinding*)rolltui_mem_alloc(sizeof *b);
  ChartBinding* prev;
  b->fn = fn;
  b->ctx = ctx;
  b->free_ctx = free_ctx;
  b->chart = rolltui_chart_new();
  prev = (ChartBinding*)rolltui_map_put(&w->charts, name, len, b);
  if (prev) {
    free_binding_ctx(prev->ctx, prev->free_ctx);
    rolltui_chart_free(prev->chart);
    rolltui_mem_free(prev);
  }
}

int rolltui_windows_has_chart(const RolltuiWindows* w, const char* name, size_t len) {
  return rolltui_map_get(&w->charts, name, len) != NULL;
}

RolltuiChart* rolltui_windows_fill_chart(RolltuiWindows* w, const char* name, size_t len) {
  ChartBinding* b = (ChartBinding*)rolltui_map_get(&w->charts, name, len);
  if (!b) return NULL;
  rolltui_chart_clear(b->chart);
  if (b->fn) b->fn(b->ctx, b->chart);
  return b->chart;
}

void rolltui_windows_bind_gauge(RolltuiWindows* w, const char* name, size_t len, RolltuiGaugeFn fn, void* ctx,
                                void (*free_ctx)(void*)) {
  GaugeBinding* b = (GaugeBinding*)rolltui_mem_alloc(sizeof *b);
  GaugeBinding* prev;
  b->fn = fn;
  b->ctx = ctx;
  b->free_ctx = free_ctx;
  prev = (GaugeBinding*)rolltui_map_put(&w->gauges, name, len, b);
  if (prev) {
    free_binding_ctx(prev->ctx, prev->free_ctx);
    rolltui_mem_free(prev);
  }
}

int rolltui_windows_has_gauge(const RolltuiWindows* w, const char* name, size_t len) {
  return rolltui_map_get(&w->gauges, name, len) != NULL;
}

int rolltui_windows_call_gauge(RolltuiWindows* w, const char* name, size_t len, RolltuiGauge* out) {
  GaugeBinding* b = (GaugeBinding*)rolltui_map_get(&w->gauges, name, len);
  if (!b || !b->fn) return 0;
  b->fn(b->ctx, out);
  return 1;
}

static OutputBinding* output_binding(RolltuiWindows* w, const char* name, size_t len) {
  OutputBinding* b = (OutputBinding*)rolltui_map_get(&w->outputs, name, len);
  if (!b) {
    b = (OutputBinding*)rolltui_mem_alloc(sizeof *b);
    memset(b, 0, sizeof *b);
    b->wrap = 1;
    b->opt.wrap = 1;
    rolltui_map_put(&w->outputs, name, len, b);
  }
  return b;
}

void rolltui_windows_bind_output(RolltuiWindows* w, const char* name, size_t len, const RolltuiAnsiText* text) {
  OutputBinding* b = output_binding(w, name, len);
  if (text != b->owned) { /* the host's text: what ran into the name goes */
    output_stop(b, -1);
    b->src_n = 0;
  }
  b->text = text;
}

void rolltui_windows_set_output_wrap(RolltuiWindows* w, const char* name, size_t len, int wrap) {
  OutputBinding* b = output_binding(w, name, len);
  b->wrap = wrap != 0;
  b->opt.wrap = b->wrap;
}

void rolltui_windows_set_output_options(RolltuiWindows* w, const char* name, size_t len, const RolltuiOutputOptions* o) {
  OutputBinding* b;
  const char* q;
  if (!w || !o) return;
  b = output_binding(w, name, len);
  q = o->filter ? o->filter : "";
  b->opt = *o;
  b->wrap = o->wrap != 0;
  b->opt_set = 1;
  if (!rolltui_str_eq(&b->filter_text, q, strlen(q))) {
    rolltui_str_set(&b->filter_text, q, strlen(q));
    rolltui_filter_free(b->filter);
    b->filter = b->filter_text.n ? rolltui_filter_new(b->filter_text.p, b->filter_text.n) : NULL;
    ++b->filter_version;
  }
  b->opt.filter = b->filter_text.p;
}

/* THE SIZE OF THE WINDOW SHOWING `output:<name>`, its content's, as the layout last placed it: 0 when none does. */
static int output_window_size(const RolltuiWindows* w, const char* name, size_t len, int* cols, int* rows) {
  size_t i;
  for (i = 0; i < w->node_n; ++i) {
    const RolltuiLayoutNode* n = w->nodes[i].node;
    RolltuiRect r;
    if (!n || !n->content.p || !content_is_output(n->content.p, n->content.n, name, len)) continue;
    rolltui_content_rect(&w->nodes[i], &r);
    if (r.w <= 0 || r.h <= 0) continue;
    *cols = r.w;
    *rows = r.h;
    return 1;
  }
  return 0;
}

int rolltui_windows_run_output(RolltuiWindows* w, const char* name, size_t len, const RolltuiChildOptions* o, const char* label,
                               unsigned char role, RolltuiStr* err) {
  OutputBinding* b;
  RolltuiChildOptions co;
  RolltuiChild* c;
  OutputSource* s;
  if (err) rolltui_str_clear(err);
  if (!w || !o) return 0;
  b = output_binding(w, name, len);
  co = *o;
  if (b->cols > 0 && b->rows > 0) co.cols = b->cols, co.rows = b->rows; /* the size the programs were told */
  else if (output_window_size(w, name, len, &co.cols, &co.rows)) b->cols = co.cols, b->rows = co.rows; /* the window's, as placed */
  c = rolltui_child_spawn(&co, err);
  if (!c) return 0;
  if (!b->owned) b->owned = rolltui_ansi_new();
  if (b->text != b->owned) b->src_n = 0; /* a host's text was bound: replaced */
  b->text = b->owned;
  b->src = (OutputSource*)rolltui_grow_zeroed(b->src, &b->src_cap, b->src_n + 1, sizeof *b->src);
  s = &b->src[b->src_n++];
  memset(s, 0, sizeof *s);
  s->child = c;
  s->role = role;
  if (label) rolltui_str_set(&s->label, label, strlen(label));
  b->status = 0;
  if (!b->opt_set) b->opt.status = 1;
  return 1;
}

int rolltui_windows_output_ran(const RolltuiWindows* w, const char* name, size_t len) {
  const OutputBinding* b = w ? (const OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  return b && b->src_n > 0;
}

int rolltui_windows_output_running(RolltuiWindows* w, const char* name, size_t len, int* status) {
  const OutputBinding* b = w ? (const OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  size_t i;
  int running = 0;
  if (status) *status = b ? b->status : 0;
  if (!b) return 0;
  for (i = 0; i < b->src_n; ++i) running = running || b->src[i].child != NULL;
  return running;
}

void rolltui_windows_output_mark(RolltuiWindows* w, const char* name, size_t len, const char* label, size_t label_len) {
  OutputBinding* b = w ? (OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  if (!b || !b->owned || b->text != b->owned) return; /* a host's own text: its own to mark (`rolltui_ansi_mark`) */
  rolltui_ansi_set_stamp(b->owned, (unsigned long long)time(NULL) * 1000ULL, 0);
  rolltui_ansi_mark(b->owned, label, label_len);
}

/* THE PROGRAMS' TURN at a frame of a window `cols` x `rows`: told a new size, what they wrote read in (one program's as it comes;
 * several, a line at a time, each line stamped with its source), and the ones that ended noted. The ms until the next frame they
 * want, or -1. */
int rolltui_windows_output_pump(RolltuiWindows* w, const char* name, size_t len, int cols, int rows) {
  OutputBinding* b = w ? (OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  const unsigned long long now = (unsigned long long)time(NULL) * 1000ULL;
  size_t i;
  int want = -1;
  if (!b || !b->owned) return -1;
  if (cols > 0 && rows > 0 && (cols != b->cols || rows != b->rows)) {
    b->cols = cols;
    b->rows = rows;
    for (i = 0; i < b->src_n; ++i)
      if (b->src[i].child) rolltui_child_resize(b->src[i].child, cols, rows);
  }
  for (i = 0; i < b->src_n; ++i) {
    OutputSource* s = &b->src[i];
    int st = 0;
    if (!s->child) continue;
    rolltui_ansi_set_stamp(b->owned, now, (unsigned char)(i + 1 > 255 ? 255 : i + 1));
    if (b->src_n == 1) {
      rolltui_child_pump(s->child, b->owned, 0);
    } else {
      char buf[4096];
      size_t got;
      while ((got = rolltui_child_read(s->child, buf, sizeof buf)) > 0) {
        const char* nl;
        rolltui_str_append(&s->partial, buf, got);
        nl = s->partial.p ? (const char*)memchr(s->partial.p, '\n', s->partial.n) : NULL;
        if (nl) { /* the whole lines go in; the rest waits for its newline */
          size_t last = s->partial.n;
          while (last > 0 && s->partial.p[last - 1] != '\n') --last;
          rolltui_ansi_feed(b->owned, s->partial.p, last);
          memmove(s->partial.p, s->partial.p + last, s->partial.n - last);
          s->partial.n -= last;
        }
      }
    }
    if (rolltui_child_drained(s->child) && rolltui_child_done(s->child, &st)) {
      if (s->partial.n) { /* the last line, unended */
        rolltui_ansi_feed(b->owned, s->partial.p, s->partial.n);
        rolltui_ansi_feed(b->owned, "\n", 1);
        rolltui_str_clear(&s->partial);
      }
      rolltui_child_free(s->child);
      s->child = NULL;
      b->status = st;
      continue;
    }
    {
      const int ms = rolltui_child_wait_ms(s->child);
      if (ms >= 0 && (want < 0 || ms < want)) want = ms;
    }
  }
  return want;
}

const RolltuiOutputOptions* rolltui_windows_output_options(const RolltuiWindows* w, const char* name, size_t len, const RolltuiFilter** filter,
                                                           unsigned long long* version) {
  const OutputBinding* b = w ? (const OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  if (filter) *filter = b ? b->filter : NULL;
  if (version) *version = b ? b->filter_version : 0;
  return b ? &b->opt : NULL;
}

/* The label and role of source `tag` (1-based) of `name`; 0 when it has none to show. */
int rolltui_windows_output_source(const RolltuiWindows* w, const char* name, size_t len, unsigned char tag, const char** label,
                                  size_t* label_len, unsigned char* role) {
  const OutputBinding* b = w ? (const OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  if (!b || tag == 0 || tag > b->src_n || !b->src[tag - 1].label.n) return 0;
  *label = b->src[tag - 1].label.p;
  *label_len = b->src[tag - 1].label.n;
  *role = b->src[tag - 1].role;
  return 1;
}

/* The widest label of `name`'s sources, in bytes (labels are a few letters). */
size_t rolltui_windows_output_label_width(const RolltuiWindows* w, const char* name, size_t len) {
  const OutputBinding* b = w ? (const OutputBinding*)rolltui_map_get(&w->outputs, name, len) : NULL;
  size_t i, most = 0;
  if (!b) return 0;
  for (i = 0; i < b->src_n; ++i)
    if (b->src[i].label.n > most) most = b->src[i].label.n;
  return most;
}

RolltuiList* rolltui_windows_list(RolltuiWindows* w, const char* name, size_t len) {
  RolltuiList* l = (RolltuiList*)rolltui_map_get(&w->lists, name, len);
  if (!l) {
    l = rolltui_list_new();
    rolltui_map_put(&w->lists, name, len, l);
  }
  return l;
}
void rolltui_windows_bind_list(RolltuiWindows* w, const char* name, size_t len, RolltuiListCountFn count, RolltuiListRowFn row,
                               void* ctx, void (*free_ctx)(void*)) {
  rolltui_list_set_source(rolltui_windows_list(w, name, len), count, row, ctx, free_ctx);
}
int rolltui_windows_has_list(const RolltuiWindows* w, const char* name, size_t len) {
  return rolltui_map_get(&w->lists, name, len) != NULL;
}
void rolltui_windows_set_list_options(RolltuiWindows* w, const char* name, size_t len, const RolltuiListOptions* o) {
  if (o) rolltui_list_set_options(rolltui_windows_list(w, name, len), o);
}
void rolltui_windows_set_list_columns(RolltuiWindows* w, const char* name, size_t len, const RolltuiListColumn* cols, size_t n) {
  rolltui_list_set_columns(rolltui_windows_list(w, name, len), cols, n);
}
void rolltui_windows_list_refresh(RolltuiWindows* w, const char* name, size_t len) {
  rolltui_list_refresh(rolltui_windows_list(w, name, len));
}
void rolltui_windows_list_set_filter(RolltuiWindows* w, const char* name, size_t len, const char* query, size_t qlen) {
  rolltui_list_set_filter(rolltui_windows_list(w, name, len), query, qlen);
}
void rolltui_windows_list_set_cursor(RolltuiWindows* w, const char* name, size_t len, const char* key, size_t klen) {
  rolltui_list_set_cursor(rolltui_windows_list(w, name, len), key, klen);
}
int rolltui_windows_list_cursor(RolltuiWindows* w, const char* name, size_t len, RolltuiStr* key) {
  return rolltui_list_cursor(rolltui_windows_list(w, name, len), key);
}
size_t rolltui_windows_list_marked(RolltuiWindows* w, const char* name, size_t len, RolltuiStrListRaw* keys) {
  return rolltui_list_marked(rolltui_windows_list(w, name, len), keys);
}
int rolltui_windows_list_event(RolltuiWindows* w, const char* name, size_t len, RolltuiListEvent* out) {
  return rolltui_list_event(rolltui_windows_list(w, name, len), out);
}
int rolltui_windows_list_set_open(RolltuiWindows* w, const char* name, size_t len, const char* key, size_t klen, int open) {
  return rolltui_list_set_open(rolltui_windows_list(w, name, len), key, klen, open);
}
void rolltui_windows_list_set_loading(RolltuiWindows* w, const char* name, size_t len, int loading) {
  rolltui_list_set_loading(rolltui_windows_list(w, name, len), loading);
}
void rolltui_windows_list_freeze(RolltuiWindows* w, const char* name, size_t len, int frozen) {
  rolltui_list_freeze(rolltui_windows_list(w, name, len), frozen);
}
void rolltui_windows_list_sort(RolltuiWindows* w, const char* name, size_t len, int column, int reverse) {
  rolltui_list_sort(rolltui_windows_list(w, name, len), column, reverse);
}
int rolltui_windows_list_sort_state(RolltuiWindows* w, const char* name, size_t len, int* column, int* reverse) {
  return rolltui_list_sort_state(rolltui_windows_list(w, name, len), column, reverse);
}
size_t rolltui_windows_list_cursor_column(RolltuiWindows* w, const char* name, size_t len) {
  return rolltui_list_cursor_column(rolltui_windows_list(w, name, len));
}
size_t rolltui_windows_list_mark_matching(RolltuiWindows* w, const char* name, size_t len, const char* query, size_t qlen, int on) {
  return rolltui_list_mark_matching(rolltui_windows_list(w, name, len), query, qlen, on);
}
int rolltui_windows_list_reveal(RolltuiWindows* w, const char* name, size_t len, const char* key, size_t klen) {
  return rolltui_list_reveal(rolltui_windows_list(w, name, len), key, klen);
}
int rolltui_windows_list_perform(RolltuiWindows* w, const char* name, size_t len, const char* action, size_t alen) {
  return action ? rolltui_list_perform(rolltui_windows_list(w, name, len), rolltui_list_default_actions(), action, alen) : 0;
}
void rolltui_list_event_release(RolltuiListEvent* e) {
  if (!e) return;
  rolltui_str_free(&e->key);
  rolltui_str_free(&e->before);
}
void rolltui_list_row_span(RolltuiListRow* r, size_t cell, size_t begin, size_t end, unsigned char role) {
  RolltuiListSpan* sp;
  if (!r || r->span_n >= ROLLTUI_LIST_SPAN_MAX || end <= begin) return;
  sp = &r->spans[r->span_n++];
  sp->cell = (unsigned char)cell;
  sp->role = role;
  sp->begin = (unsigned short)(begin > 0xFFFF ? 0xFFFF : begin);
  sp->end = (unsigned short)(end > 0xFFFF ? 0xFFFF : end);
}

void rolltui_list_row_cell(RolltuiListRow* r, const char* text, size_t len) {
  if (r) rolltui_str_list_add(&r->cells, text ? text : "", text ? len : 0);
}

void rolltui_windows_set_tasks(RolltuiWindows* w, RolltuiTasks* tasks) {
  if (w) w->tasks = tasks;
}
RolltuiTasks* rolltui_windows_tasks(const RolltuiWindows* w) { return w ? w->tasks : NULL; }

void rolltui_windows_bind_image(RolltuiWindows* w, const char* name, size_t len, const RolltuiImage* img) {
  if (!w) return;
  if (img) rolltui_map_put(&w->images, name, len, (void*)img);
  else rolltui_map_take(&w->images, name, len);
}
const RolltuiImage* rolltui_windows_image(const RolltuiWindows* w, const char* name, size_t len) {
  return (const RolltuiImage*)rolltui_map_get(&w->images, name, len);
}

RolltuiDialog* rolltui_windows_dialog(const RolltuiWindows* w, const char* id, size_t len) {
  return w ? (RolltuiDialog*)rolltui_map_get(&w->dialogs, id, len) : NULL;
}
void rolltui_windows_put_dialog(RolltuiWindows* w, const char* id, size_t len, RolltuiDialog* d) {
  RolltuiDialog* was = (RolltuiDialog*)rolltui_map_take(&w->dialogs, id, len);
  if (was != d) rolltui_dialog_free(was);
  if (d) rolltui_map_put(&w->dialogs, id, len, d);
}

const RolltuiAnsiText* rolltui_windows_output(const RolltuiWindows* w, const char* name, size_t len, int* wrap) {
  const OutputBinding* b = (const OutputBinding*)rolltui_map_get(&w->outputs, name, len);
  if (wrap) *wrap = b ? b->wrap : 1;
  return b ? b->text : NULL;
}

/* ---- messages ------------------------------------------------------------------------------ */

void rolltui_windows_post(RolltuiWindows* w, int level, const char* text, size_t len, unsigned long long now_ms) {
  if (w) rolltui_message_log_post(&w->messages, level, text, len, now_ms);
}
size_t rolltui_windows_message_count(const RolltuiWindows* w) { return w ? rolltui_message_log_count(&w->messages) : 0; }
const char* rolltui_windows_message_at(const RolltuiWindows* w, size_t i, size_t* len, int* level, unsigned long long* at_ms) {
  const RolltuiMessage* m = w ? rolltui_message_log_at(&w->messages, i) : NULL;
  if (!m) {
    if (len) *len = 0;
    return NULL;
  }
  if (len) *len = m->text.n;
  if (level) *level = m->level;
  if (at_ms) *at_ms = m->at_ms;
  return m->text.p ? m->text.p : "";
}
static int draw_which_key(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles, unsigned long long now);

/* A LAYOUT WITH NO WINDOW IN IT shows the program's keys instead of a blank screen, so there is always a way on: a key a row, centred,
 * in the key role and the text's. */
static void draw_empty_keys(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles) {
  const RolltuiBindings* b = rolltui_windows_bindings(w);
  const unsigned char proto = rolltui_key_active_protocol();
  const int aw = w->cfg->env.ambiguous_wide;
  const size_t n = b ? rolltui_bindings_action_count(b) : 0;
  size_t i, rows = 0, k;
  int y;
  RolltuiDrawScratch* ds;
  if (!b || !n) return;
  for (i = 0; i < w->node_n; ++i)
    if (w->nodes[i].node->kind == ROLLTUI_NODE_WINDOW) return; /* a window: the layout is not empty */
  ds = rolltui_draw_scratch_new(); /* only an empty layout reaches here */
  for (k = 0; k < 2; ++k) { /* counted, then drawn */
    y = screen.y + (screen.h - (int)rows) / 2;
    rows = 0;
    for (i = 0; i < n && (int)rows < screen.h; ++i) {
      size_t al = 0, sl = 0, dl = 0, c;
      const char* a = rolltui_bindings_action_at(b, i, &al);
      const char* sc = rolltui_bindings_scope_of(a, al, &sl);
      const char* d;
      if (sl != 3 || memcmp(sc, "app", 3) != 0) continue;
      for (c = 0; c < rolltui_bindings_chord_count(b, a, al); ++c) {
        RolltuiChord ch;
        char disp[ROLLTUI_CHORD_STRING_MAX];
        size_t dn;
        memset(&ch, 0, sizeof ch);
        if (!rolltui_bindings_chord_at(b, a, al, c, &ch) || !rolltui_key_deliverable(&ch, proto)) continue;
        if (k == 1) {
          RolltuiStyle ks = styles[ROLLTUI_ROLE_MENU_SHORTCUT], ts = styles[ROLLTUI_ROLE_TEXT];
          const int x = screen.x + (screen.w > 40 ? (screen.w - 40) / 2 : 0);
          d = rolltui_bindings_description(b, a, al, &dl);
          if (!d || !dl) d = a + sl + 1, dl = al - sl - 1;
          dn = rolltui_chord_display(&ch, disp, sizeof disp);
          ks.bold = 1;
          rolltui_frame_put_text(f, ds, x, y + (int)rows, disp, dn, ks, 12, aw, 0);
          rolltui_frame_put_text(f, ds, x + 13, y + (int)rows, d, dl, ts, screen.x + screen.w - (x + 13), aw, 0);
        }
        ++rows;
        break;
      }
    }
  }
  rolltui_draw_scratch_free(ds);
}

void rolltui_windows_hold(RolltuiWindows* w, const char* label, size_t len, unsigned int job) {
  if (!w) return;
  w->held = 1;
  w->hold_job = job;
  rolltui_str_set(&w->hold_label, label ? label : "", label ? len : 0);
}
void rolltui_windows_release(RolltuiWindows* w) {
  if (!w) return;
  w->held = 0;
  w->hold_job = 0;
  rolltui_str_clear(&w->hold_label);
}
int rolltui_windows_held(RolltuiWindows* w) {
  if (!w || !w->held) return 0;
  if (w->hold_job && w->tasks && !rolltui_tasks_running(w->tasks, w->hold_job)) rolltui_windows_release(w); /* its work ended */
  return w->held;
}

/* THE HOLD'S BOX, centred: a spinner and the label, and how to get out of it. */
static void draw_hold(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles, unsigned long long now_ms) {
  static const char kHint[] = "Esc cancels";
  const int aw = w->cfg->env.ambiguous_wide;
  RolltuiDrawScratch* ds = rolltui_draw_scratch_new();
  const int label_w = rolltui_frame_text_width(ds, w->hold_label.p ? w->hold_label.p : "", w->hold_label.n, aw);
  int bw = 2 + 2 + label_w + 2, bh = 5;
  RolltuiRect box, fill;
  RolltuiStyle ground = styles[ROLLTUI_ROLE_PANEL_BACKGROUND], text = styles[ROLLTUI_ROLE_TEXT], muted = styles[ROLLTUI_ROLE_TEXT_MUTED],
               spin = styles[ROLLTUI_ROLE_ACCENT_1], line = styles[ROLLTUI_ROLE_BORDER_ACTIVE];
  (void)now_ms;
  if (bw < (int)sizeof kHint - 1 + 4) bw = (int)sizeof kHint - 1 + 4;
  if (bw > screen.w) bw = screen.w;
  if (bh > screen.h) bh = screen.h;
  box.x = screen.x + (screen.w - bw) / 2;
  box.y = screen.y + (screen.h - bh) / 2;
  box.w = bw;
  box.h = bh;
  text.bg = muted.bg = spin.bg = line.bg = ground.bg;
  fill = box;
  rolltui_frame_fill(f, ds, fill, ground, NULL, 0);
  rolltui_draw_border(f, ds, box, ROLLTUI_BORDER_ROUNDED, line, NULL, 0, text, aw);
  if (bh >= 3 && bw >= 5) {
    rolltui_spinner_draw(f, box.x + 2, box.y + 1, spin, 1);
    rolltui_frame_put_text(f, ds, box.x + 4, box.y + 1, w->hold_label.p ? w->hold_label.p : "", w->hold_label.n, text, bw - 6, aw, 0);
  }
  if (bh >= 5) {
    muted.italic = 1;
    rolltui_frame_put_text(f, ds, box.x + 2, box.y + 3, kHint, sizeof kHint - 1, muted, bw - 4, aw, 0);
  }
  rolltui_draw_scratch_free(ds);
}

/* THE FOCUSED FIELD'S COMPLETIONS, open over the screen under the toasts. A scratch only while a list is open. */
static void draw_completions(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles) {
  const RolltuiLayoutNode* n = w->stack ? rolltui_window_stack_focused(w->stack) : NULL;
  RolltuiInput* in;
  RolltuiDrawScratch* draw;
  if (!n) return;
  in = rolltui_windows_input_at(w, n->id.p, n->id.n);
  if (!in || !rolltui_input_completion_open(in, NULL)) return;
  draw = rolltui_draw_scratch_new();
  rolltui_input_draw_completions(in, f, draw, styles, screen);
  rolltui_draw_scratch_free(draw);
}

int rolltui_windows_draw_toasts(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles,
                                unsigned long long now_ms) {
  int toasts, which;
  if (!w || !f || !styles) return -1;
  draw_completions(w, f, screen, styles);
  toasts = rolltui_message_log_draw_toasts(&w->messages, f, screen, styles, &w->cfg->builtin_roles, now_ms, w->cfg->env.ambiguous_wide,
                                           w->cfg->env.still);
  draw_empty_keys(w, f, screen, styles);
  which = draw_which_key(w, f, screen, styles, now_ms);
  if (rolltui_windows_held(w)) {
    draw_hold(w, f, screen, styles, now_ms);
    if (w->hold_job && (toasts < 0 || toasts > 100)) toasts = 100; /* looked at again for its job's end */
  }
  if (which > 0 && (toasts < 0 || which < toasts)) toasts = which;
  if (w->want_ms >= 0 && (toasts < 0 || w->want_ms < toasts)) toasts = w->want_ms; /* a window's own: the next frame it wants */
  w->want_ms = -1;
  return toasts;
}
int rolltui_windows_toast_event(RolltuiWindows* w, const RolltuiEvent* e) {
  return w && e ? rolltui_message_log_toast_event(&w->messages, e) : 0;
}
void rolltui_windows_dismiss_toasts(RolltuiWindows* w) {
  if (w) rolltui_message_log_dismiss(&w->messages);
}
const RolltuiMessageLog* rolltui_windows_messages(const RolltuiWindows* w) { return &w->messages; }

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

void rolltui_windows_bind_changed(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                  void (*free_ctx)(void*)) {
  SubmitBinding* b = (SubmitBinding*)rolltui_mem_alloc(sizeof *b);
  SubmitBinding* prev;
  b->fn = fn;
  b->ctx = ctx;
  b->free_ctx = free_ctx;
  b->on_submit = 0;
  prev = (SubmitBinding*)rolltui_map_put(&w->changes, name, len, b);
  if (prev) {
    free_binding_ctx(prev->ctx, prev->free_ctx);
    rolltui_mem_free(prev);
  }
}

int rolltui_windows_call_changed(RolltuiWindows* w, const char* name, size_t len, const char* text, size_t tlen) {
  SubmitBinding* b = (SubmitBinding*)rolltui_map_get(&w->changes, name, len);
  if (!b || !b->fn) return 0;
  b->fn(b->ctx, text, tlen);
  return 1;
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

int rolltui_windows_call_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNoteRaw* out) {
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

/* ---- help (at the boundary, so `help` can be a plugin) ----------------------------------- */

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

void rolltui_scrollbar_glyphs_default(RolltuiScrollbarGlyphs* out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
  /* BOX DRAWING, NOT BLOCK ELEMENTS, and the reason is coverage rather than taste. Every window
   * here is already framed in box drawing, so a terminal that shows this library at all shows
   * `│ ┌ ┐ └`; the Block Elements a solid thumb is made of are a separate range that some fonts
   * ship without, and a thumb nobody can see is worse than a plain one.
   * The heavy set draws the same capsule: ╻ is a half-height stroke going DOWN, so a thumb starts
   * mid-cell; ╹ goes UP, so it ends mid-cell; ┃ between them is the body, heavy enough to read
   * against the light │ of the border it sits in. • is the one-cell case. */
  memcpy(out->single, "\xE2\x80\xA2", 4);
  memcpy(out->top, "\xE2\x95\xBB", 4);
  memcpy(out->middle, "\xE2\x94\x83", 4);
  memcpy(out->bottom, "\xE2\x95\xB9", 4);
  memcpy(out->ascii_single, "o", 2);
  memcpy(out->ascii_top, "#", 2);
  memcpy(out->ascii_middle, "#", 2);
  memcpy(out->ascii_bottom, "#", 2);
}

void rolltui_context_set_scrollbar_glyphs(RolltuiContext* ctx, const RolltuiScrollbarGlyphs* g) {
  RolltuiWindowConfig* cfg = rolltui_context_window_config(ctx);
  if (g) cfg->scrollbar_glyphs = *g;
  else rolltui_scrollbar_glyphs_default(&cfg->scrollbar_glyphs);
}

const RolltuiScrollbarGlyphs* rolltui_windows_scrollbar_glyphs(const RolltuiWindows* w) {
  return &w->cfg->scrollbar_glyphs;
}
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

/* ---- rows (at the boundary, so `rows` can be a plugin) ----------------------------------- */

void rolltui_rows_reset(RolltuiRows* r) { r->n = 0; /* keeps v's storage and every row's buffers */ }

void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len) {
  r->v = (RolltuiRow*)rolltui_grow_zeroed(r->v, &r->cap, r->n + 1, sizeof *r->v);
  rolltui_str_set(&r->v[r->n].label, label, label_len);
  rolltui_str_set(&r->v[r->n].value, value, value_len);
  memset(&r->v[r->n].swatch, 0, sizeof r->v[r->n].swatch); /* a slot is reused across frames: what it held is not this row's */
  r->v[r->n].has_swatch = 0;
  ++r->n;
}

void rolltui_rows_add_colour(RolltuiRows* r, const char* label, size_t label_len, RolltuiStyleColorRaw colour) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  const size_t n = rolltui_color_to_string(colour, buf, sizeof buf);
  rolltui_rows_add(r, label, label_len, buf, n);
  r->v[r->n - 1].swatch = colour;
  r->v[r->n - 1].has_swatch = 1;
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

/* ---- note (at the boundary, so `input` can be a plugin) ---------------------------------- */

void rolltui_note_clear(RolltuiNoteRaw* n) {
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
  if (s->view_n) wd = s->view_w[s->view_n - 1]; /* what the window shows is its top view */
  { /* FOCUS WINS over the focus keys too: a window whose widget takes Tab (a completing field) is given it rather than left */
    const RolltuiStackActions* sa = rolltui_stack_default_actions();
    const RolltuiBindings* b = w->cfg->bindings;
    unsigned char bits = 0;
    RolltuiChord c;
    if (wd->vt && wd->vt->takes && b) {
      for (i = 0; rolltui_bindings_chord_at(b, sa->focus_next, strlen(sa->focus_next), i, &c); ++i)
        if (wd->vt->takes(wd->ctx, &c)) {
          bits |= 1;
          break;
        }
      for (i = 0; rolltui_bindings_chord_at(b, sa->focus_prev, strlen(sa->focus_prev), i, &c); ++i)
        if (wd->vt->takes(wd->ctx, &c)) {
          bits |= 2;
          break;
        }
    }
    ((RolltuiLayoutNode*)n)->takes_cycle = bits; /* the stack's routing state, not its layout: see the field */
  }
  /* THE "window 'x' (content 'y'): " PREFIX IS BUILT ONLY WHEN THERE IS SOMETHING TO SAY.
   * It used to be built for every window of every frame and thrown away. */
  if (wd->vt && wd->vt->problem && wd->vt->problem(wd->ctx, &w->scratch)) {
    note_problem(w, n, &w->scratch);
    return;
  }
  if (wd->vt && wd->vt->note_at)
    for (i = 0; wd->vt->note_at(wd->ctx, i, &w->scratch); ++i) note_problem(w, n, &w->scratch);
}

/* A window in the layout shows `output:<name>`, as its content or a view pushed into it. */
static int output_shown(const RolltuiWindows* w, const char* name, size_t len) {
  size_t i, v;
  for (i = 0; i < rolltui_map_count(&w->by_window); ++i) {
    const WindowSlot* s = (const WindowSlot*)rolltui_map_value_at(&w->by_window, i);
    const char *c, *t;
    size_t cl, tl;
    if (s->content.p && content_is_output(s->content.p, s->content.n, name, len)) return 1;
    for (v = 0; v < s->view_n; ++v)
      if (slot_view(s, v, &c, &cl, &t, &tl) && content_is_output(c, cl, name, len)) return 1;
  }
  return 0;
}

void rolltui_windows_sync(RolltuiWindows* w, const RolltuiWindowStack* stack) {
  const size_t depth = rolltui_window_stack_depth(stack);
  size_t i;
  w->stack = stack;
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
    slot_free(s);
  }
  for (i = 0; i < rolltui_map_count(&w->outputs); ++i) { /* A PROGRAM'S WINDOW GONE: the program goes with it */
    size_t kl = 0;
    const char* k = rolltui_map_key_at(&w->outputs, i, &kl);
    OutputBinding* b = (OutputBinding*)rolltui_map_value_at(&w->outputs, i);
    const int now = output_shown(w, k, kl);
    if (b->shown && !now) output_stop(b, -1);
    b->shown = now;
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
    int row, extent, border, want = 0, sized;
    if (rn->node->kind != 0) continue;
    wd = rolltui_windows_at(w, rn->node->id.p, rn->node->id.n);
    if (!wd || !wd->vt) continue;
    sized = 0;
    if (rn->layer > 0 && rn->node->fit && wd->vt->desired_outer) { /* A POPUP AS TALL AS WHAT IT HOLDS: its root window says so */
      RolltuiLayer* l = rolltui_window_stack_layer_mut(stack, rn->layer);
      if (l && &l->root == rn->node && wd->vt->desired_outer(wd->ctx, rn->inner.w, box.h, rn->node->border != 0 ? 2 : 0, &want)) {
        l->placement.h.fraction = 0;
        l->placement.h.cells = want;
        sized = 1; /* before its title and footer are asked, which may say what fits */
      }
    }
    {
      /* Into the node's own buffers, reused frame to frame. A widget answering 0, or one with no say (a tab shown in place of a
       * widget that had one), hands the title and the footer back to the layout. */
      RolltuiLayoutNode* nd = rolltui_window_stack_find(stack, rn->node->id.p, rn->node->id.n);
      const WindowSlot* sl = slot_of(w, rn->node->id.p, rn->node->id.n);
      if (nd && !(wd->vt->title && wd->vt->title(wd->ctx, nd->title.p ? nd->title.p : "", nd->title.n, &nd->live_title)))
        rolltui_str_clear(&nd->live_title);
      if (nd && sl && sl->view_n) { /* THE CRUMBS: the window's own title, then each view's */
        size_t v;
        if (!nd->live_title.n) rolltui_str_set(&nd->live_title, nd->title.p ? nd->title.p : "", nd->title.n);
        for (v = 0; v < sl->view_n; ++v) {
          const char *c, *t;
          size_t cl, tl;
          if (!slot_view(sl, v, &c, &cl, &t, &tl)) break;
          if (nd->live_title.n) rolltui_str_append(&nd->live_title, " \xe2\x80\xba ", 5);
          if (tl) rolltui_str_append(&nd->live_title, t, tl);
          else rolltui_str_append(&nd->live_title, c, kind_len(c, cl));
        }
      }
      if (nd && !(wd->vt->footer && wd->vt->footer(wd->ctx, &nd->live_footer))) rolltui_str_clear(&nd->live_footer);
    }
    if (sized) continue;
    if (rn->node->fit && wd->vt->scroll_extent) { /* AS TALL AS WHAT IT HOLDS, and a line, within its own bounds */
      RolltuiScrollExtent ext;
      memset(&ext, 0, sizeof ext);
      if (wd->vt->scroll_extent(wd->ctx, ROLLTUI_AXIS_VERTICAL, &ext)) {
        RolltuiLayoutNode* nd = rolltui_window_stack_find(stack, rn->node->id.p, rn->node->id.n);
        const int chrome = ext.visible > 0 && (int)ext.visible <= rn->inner.h ? rn->inner.h - (int)ext.visible : 0;
        int want = (int)(ext.total > 100000 ? 100000 : ext.total) + chrome + 1 + (rn->outer.h - rn->inner.h);
        if (nd) {
          if (nd->size_max > 0 && want > nd->size_max) want = nd->size_max;
          if (want < nd->size_min) want = nd->size_min;
          nd->size.fill = 0;
          nd->size.dim.fraction = 0;
          nd->size.dim.cells = want;
        }
      }
      continue;
    }
    if (!wd->vt->desired_outer) continue;
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
  rolltui_style_on_window(&style, styles, ground.bg); /* in the border, on the window's ground */
  /* A CAPSULE, not a run of one glyph: the ends are half-height so a bar of any length reads as
   * a shape rather than a gap punched in the border. Every glyph worth using is East Asian
   * AMBIGUOUS, exactly like the box-drawing set the border is made of, so it takes the border's
   * rule — with `ambiguous_wide` the thumb is ASCII. */
  {
    const RolltuiScrollbarGlyphs* g = &w->cfg->scrollbar_glyphs;
    const int wide = w->cfg->env.ambiguous_wide;
    for (i = 0; i < t.length; ++i) {
      const int y = rn->outer.y + 1 + t.offset + i;
      const char* cell;
      if (y >= rn->outer.y + rn->outer.h - 1) break;
      if (t.length == 1) cell = wide ? g->ascii_single : g->single;
      else if (i == 0) cell = wide ? g->ascii_top : g->top;
      else if (i == t.length - 1) cell = wide ? g->ascii_bottom : g->bottom;
      else cell = wide ? g->ascii_middle : g->middle;
      if (!cell[0]) cell = wide ? "#" : "\xE2\x96\x88"; /* an empty slot falls back, never draws nothing */
      rolltui_frame_put(f, x, y, cell, strlen(cell), 1, style, 0);
    }
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

/* A WINDOW'S DRAW TIME, in its top border at the right (or its first row where it has no border): muted, and bold in the warning
 * role past half of a frame at 60 a second, so a slow window shows itself where it is. */
static void draw_timing(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f, const RolltuiStyle* styles,
                        unsigned long long us) {
  char label[24];
  const int n = snprintf(label, sizeof label, " %.1f ms ", (double)us / 1000.0);
  const int border = rn->node->border != 0;
  const RolltuiRect r = border ? rn->outer : rn->inner;
  const int x = r.x + r.w - (border ? 1 : 0) - n, y = r.y;
  RolltuiStyle st = styles[us > 8333 ? ROLLTUI_ROLE_WARNING : ROLLTUI_ROLE_TEXT_MUTED];
  RolltuiCellRaw under;
  if (n <= 0 || x < r.x + (border ? 1 : 0) || r.h <= 0) return;
  memset(&under, 0, sizeof under);
  rolltui_frame_cell(f, x, y, &under);
  st.bg = under.style.bg;
  if (us > 8333) st.bold = 1;
  if (!w->scratch_draw) w->scratch_draw = rolltui_draw_scratch_new();
  rolltui_frame_put_text(f, w->scratch_draw, x, y, label, (size_t)n, st, n, 0, 0);
}

/* THE LINKS A WINDOW SHOWS, kept from its draw: a press on one opens it, and the one under the pointer is underlined across its cells,
 * which reads as a link in every theme. A frame that holds no link is not looked through. */
static void links_drawn(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  WindowSlot* s = slot_of(w, rn->node->id.p, rn->node->id.n);
  const RolltuiRect r = rn->inner;
  unsigned int hot = 0;
  size_t i;
  int x, y;
  if (!s) return;
  s->link_n = 0;
  rolltui_str_clear(&s->link_urls);
  if (!rolltui_frame_link_count(f)) return;
  for (y = r.y; y < r.y + r.h && y < rolltui_frame_height(f); ++y)
    for (x = r.x; x < r.x + r.w && x < rolltui_frame_width(f); ++x) {
      RolltuiCellRaw c;
      LinkRun* last = s->link_n ? &s->links[s->link_n - 1] : NULL;
      if (x < 0 || y < 0) continue;
      rolltui_frame_cell(f, x, y, &c);
      if (!c.link) continue;
      if (last && last->y == y && last->x + last->w == x && last->id == c.link) {
        ++last->w;
        continue;
      }
      s->links = (LinkRun*)rolltui_grow(s->links, &s->link_cap, s->link_n + 1, sizeof *s->links);
      s->links[s->link_n].x = x;
      s->links[s->link_n].y = y;
      s->links[s->link_n].w = 1;
      s->links[s->link_n].id = c.link;
      if (last && last->id == c.link) {
        s->links[s->link_n].url = last->url;
      } else {
        size_t n;
        const char* u = rolltui_frame_link(f, c.link, &n);
        s->links[s->link_n].url = s->link_urls.n;
        rolltui_str_append(&s->link_urls, u, n);
        rolltui_str_append(&s->link_urls, "", 1);
      }
      ++s->link_n;
    }
  if (!rn->hover) return;
  for (i = 0; i < s->link_n && !hot; ++i)
    if (rn->hover_y == s->links[i].y && rn->hover_x >= s->links[i].x && rn->hover_x < s->links[i].x + s->links[i].w) hot = s->links[i].id;
  for (i = 0; hot && i < s->link_n; ++i)
    if (s->links[i].id == hot)
      for (x = s->links[i].x; x < s->links[i].x + s->links[i].w; ++x) {
        RolltuiCellRaw c;
        rolltui_frame_cell(f, x, s->links[i].y, &c);
        if (!c.style.underline) c.style.underline = 1;
        rolltui_frame_set_style(f, x, s->links[i].y, c.style);
      }
}

/* The schemes the library opens on a press: what a terminal opens. Any other is the host's to take. */
static int opens_by_default(const char* url, size_t n) {
  static const char* const kSchemes[] = {"http://", "https://", "mailto:", "file://"};
  size_t i, k;
  for (i = 0; i < sizeof kSchemes / sizeof *kSchemes; ++i) {
    const size_t m = strlen(kSchemes[i]);
    if (n <= m) continue;
    for (k = 0; k < m && (url[k] | 0x20) == kSchemes[i][k]; ++k) {
    }
    if (k == m) return 1;
  }
  return 0;
}

/* A PRESS ON A LINK this window showed: the host's, else the system's for a scheme it opens. 1 when the press was taken. */
static int press_link(RolltuiWindows* w, const char* window, size_t len, int x, int y) {
  WindowSlot* s = slot_of(w, window, len);
  size_t i;
  if (!s) return 0;
  for (i = 0; i < s->link_n; ++i) {
    const LinkRun* r = &s->links[i];
    if (y == r->y && x >= r->x && x < r->x + r->w) {
      const char* url = s->link_urls.p + r->url;
      const size_t n = strlen(url);
      if (w->link_fn && w->link_fn(w->link_ctx, window, len, url, n)) return 1;
      if (!opens_by_default(url, n)) return 0;
      rolltui_open_with_system(url, n, NULL);
      return 1;
    }
  }
  return 0;
}

void rolltui_windows_set_link_handler(RolltuiWindows* w, RolltuiWindowsLinkFn fn, void* ctx) {
  if (!w) return;
  w->link_fn = fn;
  w->link_ctx = ctx;
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
  {
    const unsigned long long t0 = w->timing ? rolltui_os_monotonic_us() : 0;
    wd->vt->draw(wd->ctx, rn, f);
    links_drawn(w, rn, f);
    draw_scrollbar(w, rn, wd, f, styles, roles);
    if (w->timing) draw_timing(w, rn, f, styles, rolltui_os_monotonic_us() - t0);
  }
}

void rolltui_windows_set_timing(RolltuiWindows* w, int on) {
  if (w) w->timing = (unsigned char)(on != 0);
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

/* THE WHEEL MOVES ANYTHING THAT SHOWS A SCROLLBAR, and the WINDOW does it so that no widget
 * has to remember to. A kind that reports a scroll extent gets a bar drawn for it, and a person
 * who can see a bar expects the wheel to move it; leaving that to each kind means a kind that
 * forgets has a bar which looks like a control and is not one. `text`, `file` and `help` were
 * exactly that — their shared handler took key events only and dropped every mouse event.
 *
 * The widget is asked FIRST, so one with better wheel behaviour of its own keeps it: the
 * transcript scrolls by ENTRY rather than by line, which a generic line step would break. This
 * is the floor, not the policy.
 *
 * Requires `scroll_to` as well as `scroll_extent`, the same rule the scrollbar track uses: a
 * widget that only REPORTS gets an accurate bar that is not a handle, and the wheel must not
 * pretend otherwise. */
static int handle_wheel(const RolltuiWindows* w, RolltuiWidget* wd, const RolltuiEvent* e) {
  RolltuiScrollExtent ex;
  long long first, max_first;
  int dir;
  /* One notch, the person's (`RolltuiWidgetEnv.wheel_lines`), the distance the transcript and the column browser take too, so the
   * wheel does not travel two different distances in two windows of one screen. */
  const int lines = rolltui_windows_env(w)->wheel_lines ? rolltui_windows_env(w)->wheel_lines : 3;
  if (!wd->vt || e->kind != ROLLTUI_EVENT_MOUSE) return 0;
  if (e->mouse.kind == 4) dir = -1;        /* wheel up scrolls toward the start */
  else if (e->mouse.kind == 5) dir = 1;
  else return 0;
  if (!wd->vt->scroll_extent || !wd->vt->scroll_to) return 0;
  if (!wd->vt->scroll_extent(wd->ctx, ROLLTUI_AXIS_VERTICAL, &ex)) return 0;
  if (ex.total <= ex.visible) return 0; /* nothing to scroll; leave the event for someone else */
  max_first = (long long)(ex.total - ex.visible);
  first = (long long)ex.first + (long long)dir * lines;
  if (first < 0) first = 0;
  if (first > max_first) first = max_first;
  if ((size_t)first == ex.first) return 0; /* already at that end */
  return wd->vt->scroll_to(wd->ctx, ROLLTUI_AXIS_VERTICAL, (size_t)first);
}

static int deliver(RolltuiWindows* w, const char* window, size_t len, RolltuiWidget* wd, const RolltuiEvent* e) {
  if (handle_scrollbar(w, window, len, wd, e)) return 1;
  if (wd->vt->handle && wd->vt->handle(wd->ctx, e)) return 1;
  return handle_wheel(w, wd, e);
}

static void seq_reset(RolltuiWindows* w) {
  w->seq_n = 0;
  w->seq_count = 0;
}

/* The widget does `action`, `times` over: through a single chord bound to it, as that key would, else by name. */
static void do_action(RolltuiWindows* w, const char* window, size_t len, RolltuiWidget* wd, const char* action, size_t alen, int times) {
  const RolltuiBindings* b = rolltui_windows_bindings(w);
  const unsigned char proto = rolltui_key_active_protocol();
  RolltuiChord k;
  size_t i;
  int t;
  for (i = 0; rolltui_bindings_chord_at(b, action, alen, i, &k); ++i)
    if (rolltui_key_deliverable(&k, proto)) {
      RolltuiEvent e;
      memset(&e, 0, sizeof e);
      e.kind = ROLLTUI_EVENT_KEY;
      e.key = k;
      for (t = 0; t < times; ++t) deliver(w, window, len, wd, &e);
      return;
    }
  if (wd->vt->perform)
    for (t = 0; t < times; ++t) wd->vt->perform(wd->ctx, action, alen);
}

static int bare_digit(const RolltuiChord* k) {
  return k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && !k->super && k->ch >= '0' && k->ch <= '9';
}

static int ends_with(const char* a, size_t n, const char* tail) {
  const size_t t = strlen(tail);
  return n >= t && memcmp(a + n - t, tail, t) == 0;
}

int rolltui_windows_handle(RolltuiWindows* w, const char* window, size_t len, const RolltuiEvent* e) {
  RolltuiWidget* wd = rolltui_windows_at(w, window, len);
  const char* scope = NULL;
  size_t slen = 0, alen = 0;
  const char* action = NULL;
  int counts;
  if (rolltui_windows_held(w)) { /* HELD: Escape cancels the work and lets go; nothing else reaches a window */
    if (e->kind == ROLLTUI_EVENT_KEY && e->key.key == ROLLTUI_KEY_ESCAPE && !e->key.ctrl && !e->key.alt && !e->key.shift) {
      if (w->hold_job && w->tasks) rolltui_tasks_cancel(w->tasks, w->hold_job);
      rolltui_windows_release(w);
    }
    return 1;
  }
  if (!wd || !wd->vt) return 0;
  if (wd->vt->problem && wd->vt->problem(wd->ctx, &w->scratch)) return 0;
  if (e->kind != ROLLTUI_EVENT_KEY || !wd->vt->keys) {
    if (e->kind == ROLLTUI_EVENT_MOUSE && e->mouse.kind == 0) seq_reset(w); /* a press is a new start */
    if (e->kind == ROLLTUI_EVENT_MOUSE && e->mouse.kind == 0 && e->mouse.button == 1 && press_link(w, window, len, e->mouse.x, e->mouse.y))
      return 1;
    return deliver(w, window, len, wd, e);
  }
  /* SEQUENCES AND COUNTS, in the widget's own scope: what is on the way belongs to one window */
  counts = wd->vt->keys(wd->ctx, &scope, &slen);
  if (!rolltui_str_eq(&w->seq_window, window, len) || slen != w->seq_scope_n || (slen && memcmp(scope, w->seq_scope, slen) != 0)) {
    seq_reset(w);
    rolltui_str_set(&w->seq_window, window, len);
    w->seq_scope_n = slen < sizeof w->seq_scope ? slen : 0;
    if (w->seq_scope_n) memcpy(w->seq_scope, scope, slen);
  }
  {
    const RolltuiBindings* b = rolltui_windows_bindings(w);
    const RolltuiChord* k = &e->key;
    if (w->seq_n > 0) {
      int r;
      if (k->key == ROLLTUI_KEY_ESCAPE && !k->ctrl && !k->alt && !k->shift) { /* Escape lets go of what was on the way */
        seq_reset(w);
        return 1;
      }
      w->seq_keys[w->seq_n] = *k;
      r = rolltui_bindings_sequence_for(b, w->seq_keys, w->seq_n + 1, scope, slen, &action, &alen);
      if (r == ROLLTUI_SEQUENCE_DONE) {
        const int times = w->seq_count > 0 ? w->seq_count : 1;
        w->has_any = rolltui_bindings_sequence_ends_any(b, w->seq_keys, w->seq_n + 1, scope, slen);
        if (w->has_any) w->any_key = *k;
        seq_reset(w);
        do_action(w, window, len, wd, action, alen, times);
        return 1;
      }
      if (r == ROLLTUI_SEQUENCE_PREFIX && w->seq_n + 1 < ROLLTUI_SEQUENCE_MAX) {
        ++w->seq_n;
        return 1;
      }
      seq_reset(w); /* not a sequence after all: the key is what it would have been alone */
      return deliver(w, window, len, wd, e);
    }
    if (counts && bare_digit(k) && (k->ch != '0' || w->seq_count > 0) && !rolltui_bindings_action_for(b, k, scope, slen, &alen) &&
        rolltui_bindings_sequence_for(b, k, 1, scope, slen, NULL, NULL) == ROLLTUI_SEQUENCE_NONE) {
      if (w->seq_count < 100000) w->seq_count = w->seq_count * 10 + (int)(k->ch - '0');
      w->seq_since = rolltui_windows_env(w)->now_ms;
      return 1;
    }
    if (rolltui_bindings_sequence_for(b, k, 1, scope, slen, NULL, NULL) == ROLLTUI_SEQUENCE_PREFIX) {
      w->seq_keys[0] = *k;
      w->seq_n = 1;
      w->seq_since = rolltui_windows_env(w)->now_ms;
      return 1;
    }
    if (w->seq_count > 0) { /* a count, then a key: the key that many times, or the row it names */
      const int times = w->seq_count;
      int t, any = 0;
      const char* act = rolltui_bindings_action_for(b, k, scope, slen, &alen);
      seq_reset(w);
      if (act && (ends_with(act, alen, ".first") || ends_with(act, alen, ".last") || ends_with(act, alen, ".percent"))) {
        w->count_arg = times; /* `5 G`: the fifth row, not the last row five times */
        any = deliver(w, window, len, wd, e);
        w->count_arg = 0;
        return any;
      }
      for (t = 0; t < times; ++t) any |= deliver(w, window, len, wd, e);
      return any;
    }
  }
  if (deliver(w, window, len, wd, e)) return 1;
  /* ESCAPE THAT NOTHING TOOK, in a window showing a view: the view is the level, and goes */
  if (e->kind == ROLLTUI_EVENT_KEY && e->key.key == ROLLTUI_KEY_ESCAPE && !e->key.ctrl && !e->key.alt && !e->key.shift &&
      rolltui_windows_view_depth(w, window, len))
    return rolltui_windows_pop_view(w, window, len);
  return 0;
}

/* Whether the window with focus takes `k` itself: its widget says, else a key bound in its own scope. */
static int focused_takes(const RolltuiWindows* w, const RolltuiChord* k) {
  const RolltuiLayoutNode* n = w->stack ? rolltui_window_stack_focused(w->stack) : NULL;
  const RolltuiWidget* wd = n && n->id.n ? rolltui_windows_at(w, n->id.p, n->id.n) : NULL;
  const char* scope = NULL;
  size_t slen = 0, an = 0;
  if (!wd || !wd->vt) return 0;
  if (wd->vt->takes) return wd->vt->takes(wd->ctx, k);
  if (!wd->vt->keys) return 0;
  (void)wd->vt->keys(wd->ctx, &scope, &slen);
  return scope && slen && rolltui_bindings_action_for(rolltui_windows_bindings(w), k, scope, slen, &an) != NULL;
}

int rolltui_windows_count_arg(const RolltuiWindows* w) { return w ? w->count_arg : 0; }
void rolltui_windows_want_frame(RolltuiWindows* w, int ms) {
  if (w && ms >= 0 && (w->want_ms < 0 || ms < w->want_ms)) w->want_ms = ms;
}

int rolltui_windows_any_key(const RolltuiWindows* w, RolltuiChord* out) {
  if (!w || !w->has_any) return 0;
  if (out) *out = w->any_key;
  return 1;
}

void rolltui_windows_ask_key(RolltuiWindows* w, const char* action, size_t action_len, const char* prompt, size_t prompt_len) {
  if (!w) return;
  rolltui_str_set(&w->ask_action, action ? action : "", action ? action_len : 0);
  rolltui_str_set(&w->ask_prompt, prompt ? prompt : "", prompt ? prompt_len : 0);
  w->asking = 1;
  w->has_any = 0;
}

static int hold_takes(RolltuiWindows* w, const RolltuiChord* k) { /* a hold's Escape, which cancels it, is never the program's */
  return k->key == ROLLTUI_KEY_ESCAPE && !k->ctrl && !k->alt && !k->shift && rolltui_windows_held(w);
}
int rolltui_windows_focused_takes(const RolltuiWindows* w, const RolltuiChord* key) {
  return w && key && (hold_takes((RolltuiWindows*)w, key) || focused_takes(w, key));
}

int rolltui_windows_app_key(RolltuiWindows* w, const RolltuiEvent* e, const char* scope, size_t slen, const char** action,
                            size_t* alen) {
  const RolltuiBindings* b;
  const RolltuiChord* k;
  const char* got = NULL;
  size_t got_n = 0;
  int r;
  if (action) *action = NULL;
  if (alen) *alen = 0;
  if (!w || !e || e->kind != ROLLTUI_EVENT_KEY || !scope) return ROLLTUI_SEQUENCE_NONE;
  b = rolltui_windows_bindings(w);
  k = &e->key;
  if (hold_takes(w, k)) return ROLLTUI_SEQUENCE_NONE; /* the hold's: it goes to the window, which cancels the work */
  if (w->asking) { /* A KEY ASKED FOR: this one is the answer, or Escape lets go of the question */
    w->asking = 0;
    if (k->key == ROLLTUI_KEY_ESCAPE && !k->ctrl && !k->alt && !k->shift) return ROLLTUI_SEQUENCE_PREFIX;
    w->any_key = *k;
    w->has_any = 1;
    if (action) *action = w->ask_action.p ? w->ask_action.p : "";
    if (alen) *alen = w->ask_action.n;
    return ROLLTUI_SEQUENCE_DONE;
  }
  if (w->seq_n > 0 || w->seq_count > 0) {
    /* SOMETHING IS ON THE WAY. A window's is the window's; the host's own goes on here. */
    if (w->seq_window.n != 0 || w->seq_scope_n != slen || memcmp(w->seq_scope, scope, slen) != 0 || w->seq_n == 0)
      return ROLLTUI_SEQUENCE_NONE;
    if (k->key == ROLLTUI_KEY_ESCAPE && !k->ctrl && !k->alt && !k->shift) {
      seq_reset(w);
      return ROLLTUI_SEQUENCE_PREFIX;
    }
    w->seq_keys[w->seq_n] = *k;
    r = rolltui_bindings_sequence_for(b, w->seq_keys, w->seq_n + 1, scope, slen, &got, &got_n);
    if (r == ROLLTUI_SEQUENCE_PREFIX && w->seq_n + 1 < ROLLTUI_SEQUENCE_MAX) {
      ++w->seq_n;
      return ROLLTUI_SEQUENCE_PREFIX;
    }
    w->has_any = rolltui_bindings_sequence_ends_any(b, w->seq_keys, w->seq_n + 1, scope, slen);
    if (w->has_any) w->any_key = *k;
    seq_reset(w);
    if (r != ROLLTUI_SEQUENCE_DONE) return ROLLTUI_SEQUENCE_NONE; /* not a sequence after all: the key is what it is alone */
    if (action) *action = got;
    if (alen) *alen = got_n;
    return ROLLTUI_SEQUENCE_DONE;
  }
  if (focused_takes(w, k)) return ROLLTUI_SEQUENCE_NONE; /* the window with focus first: the program never takes its keys */
  got = rolltui_bindings_action_for(b, k, scope, slen, &got_n);
  if (got) {
    if (action) *action = got;
    if (alen) *alen = got_n;
    return ROLLTUI_SEQUENCE_DONE;
  }
  if (slen < sizeof w->seq_scope && rolltui_bindings_sequence_for(b, k, 1, scope, slen, NULL, NULL) == ROLLTUI_SEQUENCE_PREFIX) {
    rolltui_str_clear(&w->seq_window); /* the host's own: no window holds it */
    memcpy(w->seq_scope, scope, slen);
    w->seq_scope_n = slen;
    w->seq_keys[0] = *k;
    w->seq_n = 1;
    w->seq_since = rolltui_windows_env(w)->now_ms;
    return ROLLTUI_SEQUENCE_PREFIX;
  }
  return ROLLTUI_SEQUENCE_NONE;
}

/* ---- what can follow: the keys on the way, and the panel that lists what to press next ---------------------------------------- */

void rolltui_windows_remember_keys_store(RolltuiWindows* w, struct RolltuiPresetStore* s, int persist) {
  if (!w) return;
  w->keys_store = s;
  w->keys_persist = persist;
}
struct RolltuiPresetStore* rolltui_windows_keys_store(const RolltuiWindows* w, int* persist) {
  if (!w) return NULL;
  if (persist) *persist = w->keys_persist;
  return w->keys_store;
}

size_t rolltui_windows_pending_keys(const RolltuiWindows* w, RolltuiStr* text) {
  size_t i;
  if (w->asking) { /* a key asked for: the question is what is on the way */
    if (text) rolltui_str_set(text, w->ask_prompt.p ? w->ask_prompt.p : "", w->ask_prompt.n);
    return 1;
  }
  if (text) {
    rolltui_str_clear(text);
    if (w->seq_count > 0) {
      char buf[16];
      const int n = snprintf(buf, sizeof buf, "%d", w->seq_count);
      if (n > 0) rolltui_str_append(text, buf, (size_t)n);
    }
    for (i = 0; i < w->seq_n; ++i) {
      char buf[ROLLTUI_CHORD_STRING_MAX];
      const size_t n = rolltui_chord_display(&w->seq_keys[i], buf, sizeof buf);
      if (text->n) rolltui_str_append(text, " ", 1);
      rolltui_str_append(text, buf, n);
    }
  }
  return w->seq_n + (w->seq_count > 0);
}

#define WHICH_KEY_MS 300 /* a sequence typed quickly is not interrupted by a panel */
#define WHICH_KEY_ROWS 96

/* The panel, at the screen's bottom left: the keys pressed so far in its border, and a row for each key that can follow, with what it
 * does (or that more follows). Returns how long until it appears, 0 when it is up or nothing is on the way, -1 for nothing. */
static int draw_which_key(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles, unsigned long long now) {
  const RolltuiBindings* b = rolltui_windows_bindings(w);
  const RolltuiBuiltinRoles* r = &w->cfg->builtin_roles;
  const int aw = w->cfg->env.ambiguous_wide;
  RolltuiChord next[WHICH_KEY_ROWS];
  const char* actions[WHICH_KEY_ROWS];
  size_t alens[WHICH_KEY_ROWS];
  unsigned char more[WHICH_KEY_ROWS];
  size_t n, i;
  int width = 0, key_w = 0, h, x, y;
  RolltuiStyle ground, line, key, text;
  RolltuiRect box;
  RolltuiDrawScratch* draw;
  if (w->seq_n == 0 && !w->asking) return -1;
  if (!w->asking && now < w->seq_since + WHICH_KEY_MS) return (int)(w->seq_since + WHICH_KEY_MS - now);
  n = w->asking ? 0
                : rolltui_bindings_sequence_next(b, w->seq_keys, w->seq_n, w->seq_scope, w->seq_scope_n, next, actions, alens, more,
                                                 WHICH_KEY_ROWS);
  if ((n == 0 && !w->asking) || screen.w < 12 || screen.h < 4) return -1;
  draw = rolltui_draw_scratch_new();
  {
    /* IN KEY ORDER, and in COLUMNS when one would not fit: a long submap flows across, and what still does not fit ends in
     * "… N more" rather than being dropped without a word. */
    size_t ord[WHICH_KEY_ROWS], shown, k, rows, ncol, max_cols, col;
    char kb[ROLLTUI_CHORD_STRING_MAX], ob[ROLLTUI_CHORD_STRING_MAX];
    int desc_w = 0, colw;
    for (i = 0; i < n; ++i) { /* stable insertion by the key as shown */
      const size_t kl = rolltui_chord_display(&next[i], kb, sizeof kb);
      k = i;
      while (k > 0) {
        const size_t ol = rolltui_chord_display(&next[ord[k - 1]], ob, sizeof ob);
        const int c = memcmp(ob, kb, ol < kl ? ol : kl);
        if (c < 0 || (c == 0 && ol <= kl)) break;
        ord[k] = ord[k - 1];
        --k;
      }
      ord[k] = i;
    }
    for (i = 0; i < n; ++i) {
      size_t dl = 0;
      const size_t kl = rolltui_chord_display(&next[i], kb, sizeof kb);
      const char* d = actions[i] ? rolltui_bindings_description(b, actions[i], alens[i], &dl) : NULL;
      const int kw = rolltui_frame_text_width(draw, kb, kl, aw);
      const int dw = (d ? rolltui_frame_text_width(draw, d, dl, aw) : 0) + (more[i] ? 2 : 0);
      if (kw > key_w) key_w = kw;
      if (dw > desc_w) desc_w = dw;
    }
    if (desc_w > 40) desc_w = 40;
    colw = key_w + 2 + desc_w;
    rows = (size_t)(screen.h - 3 > 1 ? screen.h - 3 : 1);
    max_cols = (size_t)((screen.w - 4) / (colw + 2) > 0 ? (screen.w - 4) / (colw + 2) : 1);
    ncol = n ? (n + rows - 1) / rows : 1;
    shown = n;
    if (ncol > max_cols) { /* the last place says how many more */
      ncol = max_cols;
      shown = ncol * rows - 1;
    }
    if (n && ncol > 1) rows = (shown + (shown < n ? 1 : 0) + ncol - 1) / ncol;
    else rows = shown + (shown < n ? 1 : 0);
    width = (int)ncol * (colw + 2) + 2;
    if (w->asking) { /* the question is the title: room for it */
      const int pw = rolltui_frame_text_width(draw, w->ask_prompt.p ? w->ask_prompt.p : "", w->ask_prompt.n, aw) + 6;
      if (pw > width) width = pw;
    }
    if (width > screen.w - 2) width = screen.w - 2;
    h = (int)rows + 2;
    if (h > screen.h - 1) h = screen.h - 1;
    box.x = screen.x + 1;
    box.y = screen.y + screen.h - h - 1;
    box.w = width;
    box.h = h;
    ground = styles[r->panel];
    line = styles[r->border];
    line.bg = ground.bg;
    key = styles[r->info];
    key.bg = ground.bg;
    key.bold = 1; /* the key reads as one in every theme */
    text = styles[r->text];
    text.bg = ground.bg;
    rolltui_frame_fill(f, draw, box, ground, NULL, 0);
    {
      RolltuiStr title;
      unsigned char rounded = 1;
      memset(&title, 0, sizeof title);
      rolltui_windows_pending_keys(w, &title);
      rolltui_border_from_name("rounded", 7, &rounded);
      rolltui_draw_border(f, draw, box, rounded, line, title.p ? title.p : "", title.n, key, aw);
      rolltui_str_free(&title);
    }
    for (k = 0; k < shown; ++k) {
      size_t dl = 0;
      const size_t e = ord[k];
      const size_t kl = rolltui_chord_display(&next[e], kb, sizeof kb);
      const char* d = actions[e] ? rolltui_bindings_description(b, actions[e], alens[e], &dl) : "";
      int right;
      col = rows ? k / rows : 0;
      y = box.y + 1 + (int)(rows ? k % rows : 0);
      x = box.x + 2 + (int)col * (colw + 2);
      right = x + colw < box.x + box.w - 1 ? x + colw : box.x + box.w - 1;
      if (y >= box.y + box.h - 1) continue;
      rolltui_frame_put_text(f, draw, x, y, kb, kl, key, key_w, aw, 0);
      x += key_w + 2;
      x += rolltui_frame_put_text_cut(f, draw, x, y, d ? d : "", d ? dl : 0, text, right - x - (more[e] ? 2 : 0),
                                      ROLLTUI_CUT_END, aw, 0);
      if (more[e]) {
        RolltuiStyle m = styles[r->text_muted];
        m.bg = ground.bg;
        rolltui_frame_put_text(f, draw, x + (d && dl ? 1 : 0), y, aw ? "..." : "\xe2\x80\xa6", 3, m, right - x, aw, 0);
      }
    }
    if (shown < n) {
      char tail[48];
      RolltuiStyle m = styles[r->text_muted];
      const int tn = snprintf(tail, sizeof tail, "%s %zu more", aw ? "..." : "\xe2\x80\xa6", n - shown);
      m.bg = ground.bg;
      col = rows ? shown / rows : 0;
      y = box.y + 1 + (int)(rows ? shown % rows : 0);
      x = box.x + 2 + (int)col * (colw + 2);
      if (tn > 0 && y < box.y + box.h - 1) rolltui_frame_put_text(f, draw, x, y, tail, (size_t)tn, m, box.x + box.w - 1 - x, aw, 0);
    }
  }
  rolltui_draw_scratch_free(draw);
  return 0;
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
