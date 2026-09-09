/* rolltui/c/rolltui_widget_kinds.c — see rolltui_widget_kinds.h. The library's own `rows`,
 * `text`, `file`, `help`, `input`, `transcript`, `menu`, `theme` and `keys` kinds, plus the error/panel
 * fallbacks, each filling `rolltui/c/rolltui_widgets.h`'s plugin contract in real C11 —
 * calling only the C engines (`rolltui_input.h`, `rolltui_transcript.h`, `rolltui_menu.h`,
 * `rolltui_wrap.h`, `rolltui_frame_ops.h`, `rolltui_bindings.h`, `rolltui_marker.h`,
 * `rolltui_unicode.h`, `rolltui_embedded.h`), never a C++ header. */
#include "rolltui/c/rolltui_widget_kinds.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_keys_editor.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_presets.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_theme_editor.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_widgets.h"

/* ============================================================================================
 * SHARED: a scrolled, wrapped, marker-carrying text view — `text`/`file`/`help`'s one engine
 * (Widgets.hpp's `ScrollTextWidget`, ported once rather than three times).
 * ============================================================================================ */

typedef struct RolltuiScrollTextBase {
  RolltuiWindows* w; /* BORROWED */
  RolltuiRect area;
  int top, total;
  RolltuiWrapLines* wrap;         /* OWNED, reused across calls */
  RolltuiUnicodeScratch* uscratch; /* OWNED */
  RolltuiDrawScratch* draw;       /* OWNED */
  RolltuiBuiltinRoles roles;
  RolltuiScrollTextActions actions;
} RolltuiScrollTextBase;

static void scroll_text_base_init(RolltuiScrollTextBase* b, RolltuiWindows* w) {
  memset(b, 0, sizeof *b);
  b->w = w;
  b->wrap = rolltui_wrap_new();
  b->uscratch = rolltui_u_scratch_new();
  b->draw = rolltui_draw_scratch_new();
  b->roles = *rolltui_windows_builtin_roles(w);
  b->actions = *rolltui_windows_scroll_text_actions(w);
}

static void scroll_text_base_release(RolltuiScrollTextBase* b) {
  rolltui_wrap_free(b->wrap);
  rolltui_u_scratch_free(b->uscratch);
  rolltui_draw_scratch_free(b->draw);
}

static void scroll_text_base_layout(RolltuiScrollTextBase* b, const RolltuiResolvedNode* rn, const char* text,
                                   size_t text_len) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(b->w);
  RolltuiWrapOptions wo;
  int max_top;
  memset(&wo, 0, sizeof wo);
  wo.ambiguous_wide = env->ambiguous_wide;
  wo.tab_width = 8;
  rolltui_content_rect(rn, &b->area);
  rolltui_wrap(b->wrap, text, text_len, b->area.w > 0 ? b->area.w : 1, wo);
  b->total = (int)rolltui_wrap_line_count(b->wrap);
  max_top = b->total - (b->area.h > 0 ? b->area.h : 1);
  if (max_top < 0) max_top = 0;
  if (b->top < 0) b->top = 0;
  if (b->top > max_top) b->top = max_top;
}

/* Mirrors `rolltui::draw_scrolled_text` exactly, over the C engines. Returns the total
 * wrapped line count, as that function does. */
static int scroll_text_base_draw(RolltuiScrollTextBase* b, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                                 const char* text, size_t text_len) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(b->w);
  const RolltuiStyle* styles = rolltui_windows_styles(b->w);
  RolltuiRect r;
  RolltuiWrapOptions wo;
  size_t n, i;
  int y, total, below, mw;
  char marker[ROLLTUI_MARKER_MAX];
  size_t mlen;

  rolltui_content_rect(rn, &r);
  memset(&wo, 0, sizeof wo);
  wo.ambiguous_wide = env->ambiguous_wide;
  wo.tab_width = 8;
  rolltui_wrap(b->wrap, text, text_len, r.w > 0 ? r.w : 1, wo);
  n = rolltui_wrap_line_count(b->wrap);
  total = (int)n;
  if (r.w <= 0 || r.h <= 0) return total;

  y = r.y;
  i = (size_t)(b->top > 0 ? b->top : 0);
  for (; i < n && y < r.y + r.h; ++i) {
    const char* ltext;
    size_t ltext_len;
    const RolltuiWrapGrapheme* g;
    size_t gn;
    int width, indent, hard;
    rolltui_wrap_line(b->wrap, i, &ltext, &ltext_len, &g, &gn, &width, &indent, &hard);
    rolltui_frame_put_text(f, b->draw, r.x + indent, y++, ltext, ltext_len, styles[b->roles.text],
                           r.w - indent > 0 ? r.w - indent : 0, env->ambiguous_wide, 0);
  }
  below = total - (b->top > 0 ? b->top : 0) - r.h;
  mlen = rolltui_scroll_marker_text(below > 0 ? (size_t)below : 0, r.w, env->ambiguous_wide, marker, sizeof marker);
  if (mlen > 0) {
    mw = rolltui_u_display_width(b->uscratch, marker, mlen, env->ambiguous_wide);
    rolltui_frame_put_text(f, b->draw, r.x + (r.w - mw > 0 ? r.w - mw : 0), r.y + r.h - 1, marker, mlen,
                           styles[b->roles.scroll_marker], mw, env->ambiguous_wide, 0);
  }
  return total;
}

#define K(s) (s), strlen(s)

static int str_eq_lit(const char* a, size_t alen, const char* b) { return b && alen == strlen(b) && memcmp(a, b, alen) == 0; }

/* Mirrors `rolltui::scroll_by_action` exactly, over `rolltui_bindings.h`. */
int rolltui_scroll_by_action(const RolltuiScrollTextActions* actions, const RolltuiBindings* bindings,
                             const RolltuiChord* k, int page, int total, int* top) {
  size_t alen = 0;
  const char* a;
  int max_top;
  if (page < 1) page = 1;
  a = rolltui_bindings_action_for(bindings, k, "transcript", 10, &alen);
  if (!a) return 0;
  if (str_eq_lit(a, alen, actions->line_up)) *top -= 1;
  else if (str_eq_lit(a, alen, actions->line_down)) *top += 1;
  else if (str_eq_lit(a, alen, actions->page_up)) *top -= page;
  else if (str_eq_lit(a, alen, actions->page_down)) *top += page;
  else if (str_eq_lit(a, alen, actions->top)) *top = 0;
  else if (str_eq_lit(a, alen, actions->bottom)) *top = total;
  else return 0;
  max_top = total - page;
  if (max_top < 0) max_top = 0;
  if (*top < 0) *top = 0;
  if (*top > max_top) *top = max_top;
  return 1;
}

/* A CLICK ON "N more" JUMPS TO THE END. The marker is drawn like a label and behaves like a
 * control, so it has to BE one — a person who can see "32 more" and click it expects to arrive
 * there. Its cells are recomputed rather than remembered: the same `below` and the same width
 * that drew it, so the region tested is the region painted and the two cannot drift. */
static int scroll_text_marker_hit(RolltuiScrollTextBase* b, int x, int y) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(b->w);
  char marker[ROLLTUI_MARKER_MAX];
  size_t mlen;
  int below, mw, left;
  if (b->area.w <= 0 || b->area.h <= 0) return 0;
  if (y != b->area.y + b->area.h - 1) return 0;
  below = b->total - (b->top > 0 ? b->top : 0) - b->area.h;
  mlen = rolltui_scroll_marker_text(below > 0 ? (size_t)below : 0, b->area.w, env->ambiguous_wide, marker,
                                    sizeof marker);
  if (mlen == 0) return 0;
  mw = rolltui_u_display_width(b->uscratch, marker, mlen, env->ambiguous_wide);
  left = b->area.x + (b->area.w - mw > 0 ? b->area.w - mw : 0);
  return x >= left && x < b->area.x + b->area.w;
}

static int scroll_text_base_handle(RolltuiScrollTextBase* b, const RolltuiEvent* e) {
  if (e->kind == ROLLTUI_EVENT_MOUSE && e->mouse.kind == 0 /* press */ &&
      scroll_text_marker_hit(b, e->mouse.x, e->mouse.y)) {
    const int max_top = b->total - (b->area.h > 0 ? b->area.h : 1);
    b->top = max_top > 0 ? max_top : 0;
    return 1;
  }
  if (e->kind != ROLLTUI_EVENT_KEY) return 0;
  return rolltui_scroll_by_action(&b->actions, rolltui_windows_bindings(b->w), &e->key, b->area.h, b->total,
                                      &b->top);
}

static int scroll_text_base_scroll_extent(RolltuiScrollTextBase* b, unsigned char axis, RolltuiScrollExtent* out) {
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  out->first = (size_t)(b->top > 0 ? b->top : 0);
  out->visible = (size_t)(b->area.h > 0 ? b->area.h : 0);
  out->total = (size_t)(b->total > 0 ? b->total : 0);
  return 1;
}

static int scroll_text_base_scroll_to(RolltuiScrollTextBase* b, unsigned char axis, size_t first) {
  int max_top;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  max_top = b->total - (b->area.h > 0 ? b->area.h : 1);
  if (max_top < 0) max_top = 0;
  b->top = (int)first;
  if (b->top < 0) b->top = 0;
  if (b->top > max_top) b->top = max_top;
  return 1;
}

/* ============================================================================================
 * text:<literal> — the layout file's own words.
 * ============================================================================================ */

typedef struct RolltuiTextCtx {
  RolltuiScrollTextBase base;
  RolltuiStr literal;
} RolltuiTextCtx;

static void text_ctx_destroy(void* ctx) {
  RolltuiTextCtx* t = (RolltuiTextCtx*)ctx;
  scroll_text_base_release(&t->base);
  rolltui_str_free(&t->literal);
  rolltui_mem_free(t);
}
static void text_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiTextCtx* t = (RolltuiTextCtx*)ctx;
  scroll_text_base_layout(&t->base, rn, t->literal.p ? t->literal.p : "", t->literal.n);
}
static void text_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiTextCtx* t = (RolltuiTextCtx*)ctx;
  text_ctx_layout(ctx, rn);
  t->base.total = scroll_text_base_draw(&t->base, rn, f, t->literal.p ? t->literal.p : "", t->literal.n);
}
static int text_ctx_handle(void* ctx, const RolltuiEvent* e) { return scroll_text_base_handle(&((RolltuiTextCtx*)ctx)->base, e); }
static int text_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  return scroll_text_base_scroll_extent(&((RolltuiTextCtx*)ctx)->base, axis, out);
}
static int text_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  return scroll_text_base_scroll_to(&((RolltuiTextCtx*)ctx)->base, axis, first);
}

static const RolltuiWidgetPlugin kTextPlugin = {
    text_ctx_destroy, text_ctx_layout, text_ctx_draw, NULL, NULL, NULL, text_ctx_handle, text_ctx_scroll_extent,
    text_ctx_scroll_to,
};

/* ============================================================================================
 * file:<path> — re-read when the file's mtime changes, so a dropped-in file shows up.
 * ============================================================================================ */

typedef struct RolltuiFileCtx {
  RolltuiScrollTextBase base;
  RolltuiStr configured; /* content.source, the configured path */
  int read, ok;
  long long stamp;
  RolltuiStr read_path;
  RolltuiStr body;
} RolltuiFileCtx;

static void file_ctx_path(const RolltuiFileCtx* fc, RolltuiStr* out) {
  const char* dir;
  size_t dir_len = 0;
  if (fc->configured.n > 0 && fc->configured.p[0] == '/') {
    rolltui_str_set(out, fc->configured.p, fc->configured.n);
    return;
  }
  dir = rolltui_windows_dir(fc->base.w, &dir_len);
  if (dir_len == 0) {
    rolltui_str_set(out, fc->configured.p ? fc->configured.p : "", fc->configured.n);
    return;
  }
  rolltui_str_set(out, dir, dir_len);
  rolltui_str_append(out, "/", 1);
  rolltui_str_append_str(out, &fc->configured);
}

/* The one file's identity for change detection: mtime to the NANOSECOND, plus size — whole
 * seconds alone miss a rewrite inside one second (CLAUDE.md's cmake-staleness note is the
 * same granularity trap), and a `file:`/menu file edited twice in a second is exactly what
 * someone iterating on one does. Shared by `file` below and by the `menu` kind further down:
 * one definition of "has this path changed" for both re-read rules. */
static long long file_ctx_stamp(const char* path) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  return (long long)st.st_mtimespec.tv_sec * 1000000000LL + (long long)st.st_mtimespec.tv_nsec +
         (long long)st.st_size;
}

/* Appends the whole file at `path` to `out` (which the caller has already cleared); 1 when it
 * could be opened. Shared by `file_ctx_refresh` below and the `menu` kind's own re-read. */
static int read_whole_file(const char* path, RolltuiStr* out) {
  FILE* fh = fopen(path, "rb");
  char buf[4096];
  size_t got;
  if (!fh) return 0;
  while ((got = fread(buf, 1, sizeof buf, fh)) > 0) rolltui_str_append(out, buf, got);
  fclose(fh);
  return 1;
}

static void file_ctx_refresh(RolltuiFileCtx* fc) {
  RolltuiStr p;
  long long m;
  memset(&p, 0, sizeof p);
  file_ctx_path(fc, &p);
  m = file_ctx_stamp(p.p ? p.p : "");
  if (fc->read && m == fc->stamp && rolltui_str_eq(&fc->read_path, p.p ? p.p : "", p.n)) {
    rolltui_str_free(&p);
    return;
  }
  fc->read = 1;
  fc->stamp = m;
  rolltui_str_move(&fc->read_path, &p);
  rolltui_str_clear(&fc->body);
  fc->ok = read_whole_file(fc->read_path.p ? fc->read_path.p : "", &fc->body);
}

static int file_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiFileCtx* fc = (RolltuiFileCtx*)ctx;
  RolltuiStr p;
  file_ctx_refresh(fc);
  if (fc->ok) return 0;
  memset(&p, 0, sizeof p);
  file_ctx_path(fc, &p);
  rolltui_str_clear(out);
  rolltui_str_append(out, "cannot read '", sizeof("cannot read '") - 1);
  rolltui_str_append_str(out, &p);
  rolltui_str_append(out, "'", 1);
  rolltui_str_free(&p);
  return 1;
}
static void file_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiFileCtx* fc = (RolltuiFileCtx*)ctx;
  file_ctx_refresh(fc);
  scroll_text_base_layout(&fc->base, rn, fc->ok && fc->body.p ? fc->body.p : "", fc->ok ? fc->body.n : 0);
}
static void file_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiFileCtx* fc = (RolltuiFileCtx*)ctx;
  file_ctx_layout(ctx, rn);
  fc->base.total =
      scroll_text_base_draw(&fc->base, rn, f, fc->ok && fc->body.p ? fc->body.p : "", fc->ok ? fc->body.n : 0);
}
static int file_ctx_handle(void* ctx, const RolltuiEvent* e) { return scroll_text_base_handle(&((RolltuiFileCtx*)ctx)->base, e); }
static int file_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  return scroll_text_base_scroll_extent(&((RolltuiFileCtx*)ctx)->base, axis, out);
}
static int file_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  return scroll_text_base_scroll_to(&((RolltuiFileCtx*)ctx)->base, axis, first);
}
static void file_ctx_destroy(void* ctx) {
  RolltuiFileCtx* fc = (RolltuiFileCtx*)ctx;
  scroll_text_base_release(&fc->base);
  rolltui_str_free(&fc->configured);
  rolltui_str_free(&fc->read_path);
  rolltui_str_free(&fc->body);
  rolltui_mem_free(fc);
}

static const RolltuiWidgetPlugin kFilePlugin = {
    file_ctx_destroy, file_ctx_layout, file_ctx_draw, file_ctx_problem, NULL, NULL, file_ctx_handle,
    file_ctx_scroll_extent, file_ctx_scroll_to,
};

/* ============================================================================================
 * help — the key list, rendered from the LIVE bindings. Mirrors `help_lines`/`help_document`
 * (Bindings.cpp/Widgets.cpp) exactly, over `rolltui_bindings.h` — a separate C implementation
 * of the same two small algorithms, the same duplication `Input.cpp`'s `kActions` already is,
 * because neither function crosses the C++/C boundary today.
 * ============================================================================================ */

typedef struct RolltuiHelpCtx {
  RolltuiScrollTextBase base;
  RolltuiStr scope; /* content.source; "" = every configured scope */
  RolltuiStr built; /* text(), rebuilt each call and reused */
} RolltuiHelpCtx;

/* Appends "  <chord-or-'(unbound)'><pad>description\n" for every action of `scope`, column
 * aligned to the widest chord (capped at 22, minimum column 12) — `help_lines`'s own rule. */
void rolltui_help_scope_lines(const RolltuiBindings* b, const char* scope, size_t slen, const char* const* actions,
                              const size_t* action_lens, size_t actions_n, const char* indent, size_t indent_len,
                              RolltuiStr* out) {
  size_t count = rolltui_bindings_action_count(b);
  size_t* idx = NULL;
  size_t idx_n = 0, idx_cap = 0;
  size_t width = 0, column, i;
  RolltuiStr chord;
  memset(&chord, 0, sizeof chord);
  /* An explicit list names the rows and their order; without one, every action of `scope` in
   * table order. Both are what `rolltui::help_lines`' optional `actions` argument already
   * meant — the two paths differ only in where the names come from. */
  if (actions_n == 0) {
    for (i = 0; i < count; ++i) {
      size_t alen = 0;
      const char* a = rolltui_bindings_action_at(b, i, &alen);
      size_t oslen = 0;
      const char* os = rolltui_bindings_scope_of(a, alen, &oslen);
      if (oslen == slen && memcmp(os, scope, slen) == 0) {
        idx = (size_t*)rolltui_grow(idx, &idx_cap, idx_n + 1, sizeof *idx);
        idx[idx_n++] = i;
      }
    }
  }
  for (i = 0; i < (actions_n ? actions_n : idx_n); ++i) {
    size_t alen = actions_n ? action_lens[i] : 0;
    const char* a = actions_n ? actions[i] : rolltui_bindings_action_at(b, idx[i], &alen);
    size_t clen;
    rolltui_bindings_chords_text(b, a, alen, &chord);
    clen = chord.n;
    if (clen > 22) clen = 22;
    if (clen > width) width = clen;
  }
  column = width + 2;
  if (column < 12) column = 12;
  for (i = 0; i < (actions_n ? actions_n : idx_n); ++i) {
    size_t alen = actions_n ? action_lens[i] : 0;
    const char* a = actions_n ? actions[i] : rolltui_bindings_action_at(b, idx[i], &alen);
    size_t dlen = 0;
    const char* d;
    size_t linelen;
    rolltui_str_append(out, indent ? indent : "", indent ? indent_len : 0);
    rolltui_bindings_chords_text(b, a, alen, &chord);
    if (chord.n == 0) {
      rolltui_str_append(out, "(unbound)", sizeof("(unbound)") - 1);
      linelen = sizeof("(unbound)") - 1;
    } else {
      rolltui_str_append_str(out, &chord);
      linelen = chord.n;
    }
    if (linelen + 2 <= column) {
      size_t pad = column - linelen, k;
      for (k = 0; k < pad; ++k) rolltui_str_append(out, " ", 1);
    } else {
      rolltui_str_append(out, "  ", 2);
    }
    d = rolltui_bindings_description(b, a, alen, &dlen);
    rolltui_str_append(out, d, dlen);
    rolltui_str_append(out, "\n", 1);
  }
  rolltui_str_free(&chord);
  rolltui_mem_free(idx);
}

/* `rolltui::help_document`'s port: the lead, one "<scope>:\n" section per
 * scope with `rolltui_help_scope_lines`'s rows under it, then the note. `help_ctx_build_text`
 * below is the same document over the WINDOW's own scope list, and calls the same two pieces —
 * so the `help` window and a host's own `//help` cannot disagree about a table. */
void rolltui_help_document(const RolltuiBindings* b, const char* lead, size_t lead_len, const char* const* scopes,
                           const size_t* scope_lens, size_t scopes_n, const char* note, size_t note_len,
                           RolltuiStr* out) {
  size_t i;
  if (!out) return;
  rolltui_str_append(out, lead ? lead : "", lead ? lead_len : 0);
  for (i = 0; i < scopes_n; ++i) {
    rolltui_str_append(out, scopes[i], scope_lens[i]);
    rolltui_str_append(out, ":\n", 2);
    rolltui_help_scope_lines(b, scopes[i], scope_lens[i], NULL, NULL, 0, "  ", 2, out);
  }
  rolltui_str_append(out, note ? note : "", note ? note_len : 0);
}

static void help_ctx_build_text(RolltuiHelpCtx* h) {
  const RolltuiBindings* b = rolltui_windows_bindings(h->base.w);
  const int all = (h->scope.n == 0);
  rolltui_str_clear(&h->built);
  if (all) {
    size_t lead_len = 0;
    const char* lead = rolltui_windows_help_lead(h->base.w, &lead_len);
    size_t nscopes = rolltui_windows_help_scope_count(h->base.w);
    size_t i;
    rolltui_str_append(&h->built, lead, lead_len);
    for (i = 0; i < nscopes; ++i) {
      size_t slen = 0;
      const char* s = rolltui_windows_help_scope_at(h->base.w, i, &slen);
      rolltui_str_append(&h->built, s, slen);
      rolltui_str_append(&h->built, ":\n", 2);
      rolltui_help_scope_lines(b, s, slen, NULL, NULL, 0, "  ", 2, &h->built);
    }
    {
      size_t note_len = 0;
      const char* note = rolltui_windows_help_note(h->base.w, &note_len);
      rolltui_str_append(&h->built, note, note_len);
    }
  } else {
    rolltui_str_append_str(&h->built, &h->scope);
    rolltui_str_append(&h->built, ":\n", 2);
    rolltui_help_scope_lines(b, h->scope.p ? h->scope.p : "", h->scope.n, NULL, NULL, 0, "  ", 2, &h->built);
  }
}

static int help_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiHelpCtx* h = (RolltuiHelpCtx*)ctx;
  size_t n, i;
  int found = 0;
  RolltuiStr known;
  if (h->scope.n == 0) return 0;
  n = rolltui_windows_help_scope_count(h->base.w);
  for (i = 0; i < n; ++i) {
    size_t slen = 0;
    const char* s = rolltui_windows_help_scope_at(h->base.w, i, &slen);
    if (slen == h->scope.n && memcmp(s, h->scope.p ? h->scope.p : "", slen) == 0) {
      found = 1;
      break;
    }
  }
  if (found) return 0;
  memset(&known, 0, sizeof known);
  for (i = 0; i < n; ++i) {
    size_t slen = 0;
    const char* s = rolltui_windows_help_scope_at(h->base.w, i, &slen);
    if (known.n > 0) rolltui_str_append(&known, " | ", 3);
    rolltui_str_append(&known, s, slen);
  }
  rolltui_str_clear(out);
  rolltui_str_append(out, "'", 1);
  rolltui_str_append_str(out, &h->scope);
  rolltui_str_append(out, "' is not one of this app's key scopes (", sizeof("' is not one of this app's key scopes (") - 1);
  rolltui_str_append_str(out, &known);
  rolltui_str_append(out, ")", 1);
  rolltui_str_free(&known);
  return 1;
}
static void help_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiHelpCtx* h = (RolltuiHelpCtx*)ctx;
  help_ctx_build_text(h);
  scroll_text_base_layout(&h->base, rn, h->built.p ? h->built.p : "", h->built.n);
}
static void help_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiHelpCtx* h = (RolltuiHelpCtx*)ctx;
  help_ctx_layout(ctx, rn);
  h->base.total = scroll_text_base_draw(&h->base, rn, f, h->built.p ? h->built.p : "", h->built.n);
}
static int help_ctx_handle(void* ctx, const RolltuiEvent* e) { return scroll_text_base_handle(&((RolltuiHelpCtx*)ctx)->base, e); }
static int help_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  return scroll_text_base_scroll_extent(&((RolltuiHelpCtx*)ctx)->base, axis, out);
}
static int help_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  return scroll_text_base_scroll_to(&((RolltuiHelpCtx*)ctx)->base, axis, first);
}
static void help_ctx_destroy(void* ctx) {
  RolltuiHelpCtx* h = (RolltuiHelpCtx*)ctx;
  scroll_text_base_release(&h->base);
  rolltui_str_free(&h->scope);
  rolltui_str_free(&h->built);
  rolltui_mem_free(h);
}

static const RolltuiWidgetPlugin kHelpPlugin = {
    help_ctx_destroy, help_ctx_layout, help_ctx_draw, help_ctx_problem, NULL, NULL, help_ctx_handle,
    help_ctx_scroll_extent, help_ctx_scroll_to,
};

/* ============================================================================================
 * rows:<source> — label/value rows a host supplies.
 * ============================================================================================ */

typedef struct RolltuiRowsCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  RolltuiRows rows;
  RolltuiStr line; /* the one-row-high concatenated form, reused */
  RolltuiWrapLines* wrap;
  RolltuiDrawScratch* draw;
  unsigned char role_label, role_value;
} RolltuiRowsCtx;

static int rows_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiRowsCtx* rc = (RolltuiRowsCtx*)ctx;
  if (rolltui_windows_has_rows(rc->w, rc->source.p ? rc->source.p : "", rc->source.n)) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &rc->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}
static void rows_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) { (void)ctx; (void)rn; }
static void rows_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiRowsCtx* rc = (RolltuiRowsCtx*)ctx;
  const RolltuiWidgetEnv* env;
  const RolltuiStyle* styles;
  RolltuiRect r;
  size_t i;
  if (!rolltui_windows_has_rows(rc->w, rc->source.p ? rc->source.p : "", rc->source.n)) return;
  rolltui_rows_reset(&rc->rows);
  rolltui_windows_call_rows(rc->w, rc->source.p ? rc->source.p : "", rc->source.n, &rc->rows);
  env = rolltui_windows_env(rc->w);
  styles = rolltui_windows_styles(rc->w);
  r = rn->inner; /* NOT content_rect: rows draws directly in the inner rect, +1 col itself */
  if (r.h == 1) {
    /* THE SAME TWO ROLES AS THE TALL CASE. A single-row `rows:` window is the shape a status
     * line has, and drawing it in one style made the same data read as structure when the
     * window was tall and as one run of words when it was one row high. */
    rolltui_frame_put_fields(f, rc->draw, r.x + 1, r.y, &rc->rows, styles[rc->role_label],
                             styles[rc->role_value], r.w - 1 > 0 ? r.w - 1 : 0, env->ambiguous_wide);
    return;
  }
  {
    RolltuiWrapOptions wo;
    int y = r.y;
    /* THE LABEL COLUMN IS THE WIDEST LABEL, not a number. It was 8 cells, which is invisible
     * until a host writes a label that teaches — "layout F6" is nine — and then the value runs
     * into the label or the label is cut mid-word. A panel one column wide must still draw
     * something, so the width is clamped to half the inner rect. */
    int labw = 0, valx;
    for (i = 0; i < rc->rows.n; ++i) {
      const RolltuiRow* lr = &rc->rows.v[i];
      const int lw = rolltui_frame_text_width(rc->draw, lr->label.p ? lr->label.p : "", lr->label.n,
                                              env->ambiguous_wide);
      if (lw > labw) labw = lw;
    }
    /* Two cells of gutter, and a FLOOR of eight. The floor is what keeps a panel steady: rows
     * come and go between frames (a count that only appears once there is something to count),
     * so a column sized purely by what is present today would step left and right as they do. */
    labw += 2;
    if (labw < 8) labw = 8;
    if (r.w - 1 > 1 && labw > (r.w - 1) / 2) labw = (r.w - 1) / 2;
    if (labw < 1) labw = 1;
    valx = r.x + 1 + labw;
    memset(&wo, 0, sizeof wo);
    wo.ambiguous_wide = env->ambiguous_wide;
    wo.tab_width = 8;
    for (i = 0; i < rc->rows.n; ++i) {
      const RolltuiRow* row = &rc->rows.v[i];
      size_t n, j;
      if (y >= r.y + r.h) break;
      rolltui_frame_put_text(f, rc->draw, r.x + 1, y, row->label.p ? row->label.p : "", row->label.n,
                             styles[rc->role_label], labw, env->ambiguous_wide, 0);
      rolltui_wrap(rc->wrap, row->value.p ? row->value.p : "", row->value.n,
                   r.x + r.w - valx > 0 ? r.x + r.w - valx : 1, wo);
      n = rolltui_wrap_line_count(rc->wrap);
      if (n == 0) {
        ++y;
        continue;
      }
      for (j = 0; j < n; ++j) {
        const char* ltext;
        size_t ltext_len;
        const RolltuiWrapGrapheme* g;
        size_t gn;
        int width, indent, hard;
        if (y >= r.y + r.h) break;
        rolltui_wrap_line(rc->wrap, j, &ltext, &ltext_len, &g, &gn, &width, &indent, &hard);
        rolltui_frame_put_text(f, rc->draw, valx, y, ltext, ltext_len, styles[rc->role_value],
                               r.x + r.w - valx > 0 ? r.x + r.w - valx : 0, env->ambiguous_wide, 0);
        ++y;
      }
    }
  }
}
static void rows_ctx_destroy(void* ctx) {
  RolltuiRowsCtx* rc = (RolltuiRowsCtx*)ctx;
  rolltui_str_free(&rc->source);
  rolltui_rows_release(&rc->rows);
  rolltui_str_free(&rc->line);
  rolltui_wrap_free(rc->wrap);
  rolltui_draw_scratch_free(rc->draw);
  rolltui_mem_free(rc);
}

static const RolltuiWidgetPlugin kRowsPlugin = {
    rows_ctx_destroy, rows_ctx_layout, rows_ctx_draw, rows_ctx_problem, NULL, NULL, NULL, NULL, NULL,
};

/* ============================================================================================
 * the error panel — the window that could not be understood, never blank (Layout.hpp).
 * ============================================================================================ */

typedef struct RolltuiErrorCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr why;       /* the raw reason, for problem() */
  RolltuiStr bracketed; /* "[why]", precomputed once, for draw() */
  RolltuiWrapLines* wrap;
  RolltuiDrawScratch* draw;
  unsigned char role_error;
} RolltuiErrorCtx;

static void error_ctx_destroy(void* ctx) {
  RolltuiErrorCtx* e = (RolltuiErrorCtx*)ctx;
  rolltui_str_free(&e->why);
  rolltui_str_free(&e->bracketed);
  rolltui_wrap_free(e->wrap);
  rolltui_draw_scratch_free(e->draw);
  rolltui_mem_free(e);
}
static int error_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiErrorCtx* e = (RolltuiErrorCtx*)ctx;
  rolltui_str_set(out, e->why.p ? e->why.p : "", e->why.n);
  return 1; /* an error widget always has a problem — that is what it draws */
}
static void error_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) { (void)ctx; (void)rn; }
static void error_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiErrorCtx* e = (RolltuiErrorCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(e->w);
  const RolltuiStyle* styles = rolltui_windows_styles(e->w);
  RolltuiRect r;
  RolltuiWrapOptions wo;
  size_t n, i;
  int y;
  rolltui_content_rect(rn, &r);
  memset(&wo, 0, sizeof wo);
  wo.ambiguous_wide = env->ambiguous_wide;
  wo.tab_width = 8;
  rolltui_wrap(e->wrap, e->bracketed.p ? e->bracketed.p : "", e->bracketed.n, r.w > 0 ? r.w : 1, wo);
  n = rolltui_wrap_line_count(e->wrap);
  y = r.y;
  for (i = 0; i < n && y < r.y + r.h; ++i) {
    const char* text;
    size_t text_len;
    const RolltuiWrapGrapheme* g;
    size_t gn;
    int width, indent, hard;
    rolltui_wrap_line(e->wrap, i, &text, &text_len, &g, &gn, &width, &indent, &hard);
    rolltui_frame_put_text(f, e->draw, r.x + indent, y++, text, text_len, styles[e->role_error],
                           r.w - indent > 0 ? r.w - indent : 0, env->ambiguous_wide, 0);
  }
}

static const RolltuiWidgetPlugin kErrorPlugin = {
    error_ctx_destroy, error_ctx_layout, error_ctx_draw, error_ctx_problem, NULL, NULL, NULL, NULL, NULL,
};

static RolltuiWidget make_error_widget(RolltuiWindows* w, unsigned char role_error, const char* why, size_t len) {
  RolltuiErrorCtx* e = (RolltuiErrorCtx*)rolltui_mem_alloc(sizeof *e);
  RolltuiWidget out;
  memset(e, 0, sizeof *e);
  e->w = w;
  e->role_error = role_error;
  rolltui_str_set(&e->why, why, len);
  rolltui_str_append(&e->bracketed, "[", 1);
  rolltui_str_append(&e->bracketed, why, len);
  rolltui_str_append(&e->bracketed, "]", 1);
  e->wrap = rolltui_wrap_new();
  e->draw = rolltui_draw_scratch_new();
  out.vt = &kErrorPlugin;
  out.ctx = e;
  return out;
}

/* ============================================================================================
 * THE FACTORIES, registered with `ctx = w` — the library's own kinds are context the same way
 * a host's kind is (rule 5).
 * ============================================================================================ */

static RolltuiWidget rows_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiRowsCtx* rc;
  const RolltuiBuiltinRoles* roles;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out; /* rolltui_windows_widget_for falls back to the error factory */
  }
  rolltui_str_free(&why);
  rc = (RolltuiRowsCtx*)rolltui_mem_alloc(sizeof *rc);
  memset(rc, 0, sizeof *rc);
  rc->w = w;
  rolltui_str_set(&rc->source, source, source_len);
  rc->wrap = rolltui_wrap_new();
  rc->draw = rolltui_draw_scratch_new();
  roles = rolltui_windows_builtin_roles(w);
  rc->role_label = roles->label;
  rc->role_value = roles->value;
  out.vt = &kRowsPlugin;
  out.ctx = rc;
  return out;
}

static RolltuiWidget text_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiTextCtx* tc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  tc = (RolltuiTextCtx*)rolltui_mem_alloc(sizeof *tc);
  memset(tc, 0, sizeof *tc);
  scroll_text_base_init(&tc->base, w);
  rolltui_str_set(&tc->literal, source, source_len);
  out.vt = &kTextPlugin;
  out.ctx = tc;
  return out;
}

static RolltuiWidget file_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiFileCtx* fc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  fc = (RolltuiFileCtx*)rolltui_mem_alloc(sizeof *fc);
  memset(fc, 0, sizeof *fc);
  scroll_text_base_init(&fc->base, w);
  fc->stamp = -1;
  rolltui_str_set(&fc->configured, source, source_len);
  out.vt = &kFilePlugin;
  out.ctx = fc;
  return out;
}

static RolltuiWidget help_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiHelpCtx* hc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  hc = (RolltuiHelpCtx*)rolltui_mem_alloc(sizeof *hc);
  memset(hc, 0, sizeof *hc);
  scroll_text_base_init(&hc->base, w);
  rolltui_str_set(&hc->scope, source, source_len);
  out.vt = &kHelpPlugin;
  out.ctx = hc;
  return out;
}

/* THE TWO FALLBACKS. `error` is given a CONTENT nothing could build and works out the reason;
 * `panel` is given a REASON directly. Mirrors `Windows::register_builtin_kinds()`'s two
 * lambdas exactly, over `rolltui_content_parse` instead of `rolltui::parse_content`. */
static RolltuiWidget error_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why, msg;
  RolltuiWidget out;
  memset(&why, 0, sizeof why);
  memset(&msg, 0, sizeof msg);
  if (rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                            &name, &name_len, &source, &source_len, &problem,
                            &why)) {
    /* it PARSES, so its kind is in one of the two rungs and nothing built it — a registered
     * kind this host has no factory for. A named panel, never a blank window. */
    rolltui_str_append(&msg, "kind '", sizeof("kind '") - 1);
    rolltui_str_append(&msg, name, name_len);
    rolltui_str_append(&msg, "' is registered but this host has no factory for it",
                      sizeof("' is registered but this host has no factory for it") - 1);
  } else {
    rolltui_str_move(&msg, &why);
  }
  rolltui_str_free(&why);
  out = make_error_widget(w, rolltui_windows_builtin_roles(w)->error, msg.p ? msg.p : "", msg.n);
  rolltui_str_free(&msg);
  return out;
}
static RolltuiWidget panel_widget_factory(void* c, RolltuiWindows* w, const char* why, size_t n) {
  (void)c;
  return make_error_widget(w, rolltui_windows_builtin_roles(w)->error, why, n);
}

/* The three below are defined further down, with the kinds they build. Declared here so the
 * one registration point stays one function rather than three scattered ones. */
static RolltuiWidget input_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n);
static RolltuiWidget transcript_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n);
static RolltuiWidget menu_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n);
static RolltuiWidget theme_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n);
static RolltuiWidget keys_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n);

/* ---- `filepicker`: choosing a path -----------------------------------------------------------
 *
 * A picker is a LIST, not a tree: one directory at a time, `..` to leave it, Enter to go in or to
 * take. That is deliberately less than a browser — a browser is for looking, and a picker is for
 * one answer, so the columns, the metadata and the sorting a browser earns would be in the way.
 * Both read a directory through `rolltui_dir_read`, which is where the sharing belongs.
 *
 * The ANSWER is polled rather than pushed: a host asks `rolltui_windows_picker_taken` on the frame
 * after it opened the panel, which is where it already asks a preset store for its version. A
 * callback would need a host to keep one alive across a screen swap for a widget it may not own.
 */
typedef struct RolltuiPickerCtx {
  RolltuiWindows* w;      /* BORROWED */
  RolltuiDrawScratch* draw;
  RolltuiUnicodeScratch* u;
  RolltuiStr dir;         /* where it is looking */
  RolltuiDirList list;
  RolltuiStr err;         /* why the directory could not be read, if it could not */
  RolltuiStr taken;       /* the chosen path, until a host collects it */
  int has_taken;
  int sel, top;
  RolltuiRect area;
  RolltuiBuiltinRoles roles;
} RolltuiPickerCtx;

static void picker_reload(RolltuiPickerCtx* p) {
  rolltui_dir_read(p->dir.p ? p->dir.p : ".", p->dir.n, ROLLTUI_SORT_NAME, 0, &p->list, &p->err);
  p->sel = 0;
  p->top = 0;
}

static void picker_set_dir(RolltuiPickerCtx* p, const char* d, size_t n) {
  while (n > 1 && d[n - 1] == '/') --n; /* a trailing slash makes ".." climb nowhere */
  rolltui_str_set(&p->dir, d, n);
  picker_reload(p);
}

static void picker_ctx_destroy(void* ctx) {
  RolltuiPickerCtx* p = (RolltuiPickerCtx*)ctx;
  rolltui_dir_list_release(&p->list);
  rolltui_str_free(&p->dir);
  rolltui_str_free(&p->err);
  rolltui_str_free(&p->taken);
  rolltui_draw_scratch_free(p->draw);
  rolltui_u_scratch_free(p->u);
  rolltui_mem_free(p);
}

static void picker_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  rolltui_content_rect(rn, &((RolltuiPickerCtx*)ctx)->area);
}

static void picker_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiPickerCtx* p = (RolltuiPickerCtx*)ctx;
  const RolltuiStyle* styles = rolltui_windows_styles(p->w);
  const RolltuiWidgetEnv* env = rolltui_windows_env(p->w);
  RolltuiRect r;
  int y, i;
  rolltui_content_rect(rn, &r);
  p->area = r;
  if (r.w <= 0 || r.h <= 0) return;
  /* WHERE IT IS LOOKING, always, on the first row. A picker that shows only names asks a person
   * to choose a file without saying which directory they are in. */
  rolltui_frame_put_text(f, p->draw, r.x, r.y, p->dir.p ? p->dir.p : "", p->dir.n,
                         styles[p->roles.text_muted], r.w, env->ambiguous_wide, 0);
  if (p->err.n) {
    rolltui_frame_put_text(f, p->draw, r.x, r.y + 1, p->err.p, p->err.n, styles[ROLLTUI_ROLE_ERROR], r.w,
                           env->ambiguous_wide, 0);
    return;
  }
  y = r.y + 1;
  /* THE SELECTION IS A GLYPH, not only a colour. A row marked by style alone is invisible at
   * `mono` and to a colour-blind reader, which is the same rule the browser's trailing chevron
   * already follows for directories. */
  if (y < r.y + r.h) {
    const int on = p->sel == 0;
    rolltui_frame_put_text(f, p->draw, r.x, y, on ? "\xE2\x80\xBA " : "  ", on ? 4 : 2,
                           styles[on ? p->roles.input_selection : p->roles.text_muted], 2,
                           env->ambiguous_wide, 0);
    /* `..` is a row rather than a key nobody was told about. */
    rolltui_frame_put_text(f, p->draw, r.x + 2, y++, "..", 2,
                           styles[on ? p->roles.input_selection : p->roles.text], r.w - 2,
                           env->ambiguous_wide, 0);
  }
  for (i = p->top; i < (int)p->list.n && y < r.y + r.h; ++i, ++y) {
    const RolltuiDirEntry* e = &p->list.v[i];
    const int on = p->sel == i + 1;
    const RolltuiStyle st = styles[on ? p->roles.input_selection : p->roles.text];
    rolltui_frame_put_text(f, p->draw, r.x, y, on ? "\xE2\x80\xBA " : "  ", on ? 4 : 2, st, 2,
                           env->ambiguous_wide, 0);
    rolltui_frame_put_text(f, p->draw, r.x + 2, y, e->name.p ? e->name.p : "", e->name.n, st, r.w - 2,
                           env->ambiguous_wide, 0);
    /* A trailing separator is how a directory says so without a second column. */
    if (e->is_dir)
      rolltui_frame_put_text(f, p->draw, r.x + 2 + (int)e->name.n, y, "/", 1,
                             styles[on ? p->roles.input_selection : p->roles.text_muted], 1,
                             env->ambiguous_wide, 0);
  }
}

static void picker_join(const RolltuiStr* dir, const RolltuiStr* name, RolltuiStr* out) {
  rolltui_str_set(out, dir->p ? dir->p : "", dir->n);
  if (!(dir->n == 1 && dir->p && dir->p[0] == '/')) rolltui_str_append(out, "/", 1);
  rolltui_str_append(out, name->p ? name->p : "", name->n);
}

static int picker_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiPickerCtx* p = (RolltuiPickerCtx*)ctx;
  const int rows = (int)p->list.n + 1;
  if (e->kind != ROLLTUI_EVENT_KEY) return 0;
  if (e->key.key == ROLLTUI_KEY_DOWN) { if (p->sel + 1 < rows) ++p->sel; return 1; }
  if (e->key.key == ROLLTUI_KEY_UP) { if (p->sel > 0) --p->sel; return 1; }
  if (e->key.key == ROLLTUI_KEY_ENTER) {
    if (p->sel == 0) {
      /* Out. The parent of "/" is "/", so climbing past the root stays put rather than emptying. */
      size_t n = p->dir.n;
      while (n > 1 && p->dir.p[n - 1] != '/') --n;
      while (n > 1 && p->dir.p[n - 1] == '/') --n;
      picker_set_dir(p, p->dir.p, n ? n : 1);
      return 1;
    }
    {
      const RolltuiDirEntry* sel = &p->list.v[p->sel - 1];
      RolltuiStr full = {0};
      picker_join(&p->dir, &sel->name, &full);
      if (sel->is_dir) {
        picker_set_dir(p, full.p, full.n);
      } else {
        /* THE ANSWER, held until a host collects it. */
        rolltui_str_set(&p->taken, full.p, full.n);
        p->has_taken = 1;
      }
      rolltui_str_free(&full);
      return 1;
    }
  }
  return 0;
}

static const RolltuiWidgetPlugin kPickerPlugin = {
    picker_ctx_destroy, picker_ctx_layout, picker_ctx_draw, NULL, NULL,
    NULL,               picker_ctx_handle, NULL,            NULL,
};

static RolltuiWidget picker_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  RolltuiPickerCtx* p;
  RolltuiWidget out;
  (void)c;
  (void)content;
  (void)n;
  memset(&out, 0, sizeof out);
  p = (RolltuiPickerCtx*)rolltui_mem_alloc(sizeof *p);
  memset(p, 0, sizeof *p);
  p->w = w;
  p->draw = rolltui_draw_scratch_new();
  p->u = rolltui_u_scratch_new();
  p->roles = *rolltui_windows_builtin_roles(w);
  picker_set_dir(p, ".", 1);
  out.vt = &kPickerPlugin;
  out.ctx = p;
  return out;
}

static RolltuiPickerCtx* picker_ctx_for(RolltuiWindows* w, const char* content, size_t len) {
  RolltuiWidget* widget = rolltui_windows_widget_for(w, content, len);
  if (!widget || widget->vt != &kPickerPlugin) return NULL;
  return (RolltuiPickerCtx*)widget->ctx;
}

void rolltui_windows_set_picker_dir(RolltuiWindows* w, const char* content, size_t len, const char* dir,
                                    size_t dir_len) {
  RolltuiPickerCtx* p = picker_ctx_for(w, content, len);
  if (!p) return;
  picker_set_dir(p, dir && dir_len ? dir : ".", dir && dir_len ? dir_len : 1);
}

int rolltui_windows_picker_taken(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* out) {
  RolltuiPickerCtx* p = picker_ctx_for(w, content, len);
  if (!p || !p->has_taken) return 0;
  if (out) rolltui_str_set(out, p->taken.p ? p->taken.p : "", p->taken.n);
  p->has_taken = 0; /* collected once: a host that asks every frame must not act twice */
  return 1;
}

void rolltui_widget_kinds_register(RolltuiContext* ctx) {
  /* THE LIBRARY'S OWN KINDS REGISTER WITH A NULL CTX, and that is the split working rather than
   * a shortcut: these factories need the SCREEN, which they are handed as `w`, and they need
   * nothing that belongs to one registration. A host's kind may still carry its own ctx. */
  rolltui_context_register_kind(ctx, "rows", 4, rows_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "text", 4, text_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "file", 4, file_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "help", 4, help_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "input", 5, input_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "transcript", 10, transcript_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "menu", 4, menu_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "theme", 5, theme_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "keys", 4, keys_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "filepicker", 10, picker_widget_factory, NULL, NULL);
  rolltui_context_set_error_factory(ctx, error_widget_factory, NULL);
  rolltui_context_set_panel_factory(ctx, panel_widget_factory, NULL);
}

/* THE FIVE VOCABULARIES AND THE KINDS, IN ONE CALL.
 *
 * THE GAP THIS CLOSES: with `rolltui_widget_kinds_register` and the five setters above called
 * only from a C++ CONSTRUCTOR, a bare `rolltui_windows_new()` comes back with no kinds
 * registered and NULL action names — ported but not REACHABLE, which is the distinction that
 * matters. What made it possible to close is the vocabularies being C data: the role ordinals
 * from `rolltui_style.h`'s X-macro and the action names from `rolltui_library_actions.c`'s.
 *
 * A host that wants its OWN words still calls the setters afterwards; this is a default, not a
 * policy. It is idempotent, and `Windows` is now one line over it. */
void rolltui_context_set_library_defaults(RolltuiContext* ctx) {
  static const RolltuiBuiltinRoles kRoles = {
      /*text=*/ROLLTUI_ROLE_TEXT,
      /*text_muted=*/ROLLTUI_ROLE_TEXT_MUTED,
      /*error=*/ROLLTUI_ROLE_ERROR,
      /*scroll_marker=*/ROLLTUI_ROLE_SCROLL_MARKER,
      /*label=*/ROLLTUI_ROLE_LABEL,
      /*value=*/ROLLTUI_ROLE_VALUE,
      /*input_text=*/ROLLTUI_ROLE_INPUT_TEXT,
      /*input_selection=*/ROLLTUI_ROLE_SELECTION,
      /*input_placeholder=*/ROLLTUI_ROLE_INPUT_PLACEHOLDER,
  };
  static const RolltuiMenuRoles kMenuRoles = {
      /*item=*/ROLLTUI_ROLE_MENU_ITEM,
      /*selected=*/ROLLTUI_ROLE_MENU_SELECTED,
      /*breadcrumb=*/ROLLTUI_ROLE_MENU_BREADCRUMB,
      /*shortcut=*/ROLLTUI_ROLE_MENU_SHORTCUT,
      /*text_muted=*/ROLLTUI_ROLE_TEXT_MUTED,
      /*warning=*/ROLLTUI_ROLE_WARNING,
      /*scroll_marker=*/ROLLTUI_ROLE_SCROLL_MARKER,
      /*label=*/ROLLTUI_ROLE_LABEL,
      /*value=*/ROLLTUI_ROLE_VALUE,
  };
  if (!ctx) return;
  /* THE LIVE TABLE, and it belongs in this function for the same reason the roles and the kinds
   * do. `rolltui_windows_bindings()` was NULL until a host called
   * `set_bindings`, and the `help` kind's sizing pass reads it unguarded — so a pure-C host
   * that registered the built-in kinds and then laid out a `help` window segfaulted before
   * drawing anything. `rolltui::Windows`' C++ constructor had always defaulted it, which is
   * exactly why nothing catches it: a C++ constructor that always defaults it leaves the C
   * path with no caller to fail.
   *
   * A default, not a policy — a host with its own table calls `set_bindings` afterwards, and
   * this is a BORROW of the library's shipped table, which lives for the process. */
  rolltui_context_set_bindings(ctx, rolltui_bindings_default(ctx));
  rolltui_context_set_builtin_roles(ctx, &kRoles);
  rolltui_context_set_menu_roles(ctx, &kMenuRoles);
  rolltui_context_set_scroll_text_actions(ctx, rolltui_scroll_text_default_actions());
  rolltui_context_set_transcript_actions(ctx, rolltui_transcript_default_actions());
  rolltui_context_set_input_actions(ctx, rolltui_input_default_actions());
  rolltui_widget_kinds_register(ctx);
}

/* ============================================================================================
 * input:<target> — the line editor. `Windows`' own `Input` map (C++) owns the `RolltuiInput*`
 * this ctx borrows, so `windows.input(source)` and this widget read the one underlying state
 * (see this file's header comment for why `transcript`/`menu` cannot do the same).
 * ============================================================================================ */

typedef struct RolltuiInputCtx {
  RolltuiInput* ed;  /* BORROWED — Windows' own Input map owns it */
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  int min_outer;
  int max_rows; /* this frame's cap, from the last desired_outer() */
  RolltuiBuiltinRoles roles;
  RolltuiUnicodeScratch* uscratch;
  RolltuiDrawScratch* draw;
  RolltuiNote note; /* reused scratch for note_info() */
} RolltuiInputCtx;

int rolltui_input_max_rows(int parent_extent, int border_rows) {
  int v = parent_extent / 2 - border_rows;
  return v > 1 ? v : 1;
}
int rolltui_input_window_rows(int text_rows, int end_col, int note_width, int width, int max_rows) {
  int rows;
  (void)width;
  if (max_rows < 1) max_rows = 1;
  rows = text_rows < 1 ? 1 : text_rows;
  if (rows > max_rows) rows = max_rows;
  if (note_width <= 0) return rows;
  if (rows == 1 && end_col + 2 + note_width <= width) return 1;
  return (rows + 1 < max_rows) ? rows + 1 : max_rows;
}

static RolltuiNote* input_ctx_note_info(RolltuiInputCtx* ic) {
  rolltui_note_clear(&ic->note);
  rolltui_windows_call_note(ic->w, ic->source.p ? ic->source.p : "", ic->source.n, &ic->note);
  return &ic->note;
}
static int input_ctx_end_col(RolltuiInputCtx* ic) {
  size_t tlen = 0;
  const char* t = rolltui_input_text(ic->ed, &tlen);
  const RolltuiWidgetEnv* env = rolltui_windows_env(ic->w);
  (void)t;
  if (tlen > 0) {
    int row = 0, col = 0;
    rolltui_input_cell_of(ic->ed, tlen, &row, &col);
    return col;
  }
  {
    const RolltuiInputOptions* o = rolltui_input_options(ic->ed);
    int pw = rolltui_u_display_width(ic->uscratch, o->prompt.p ? o->prompt.p : "", o->prompt.n, env->ambiguous_wide);
    int phw =
        rolltui_u_display_width(ic->uscratch, o->placeholder.p ? o->placeholder.p : "", o->placeholder.n, env->ambiguous_wide);
    return pw + phw;
  }
}
static int input_ctx_rows_with_note(RolltuiInputCtx* ic, int width, int text_rows) {
  RolltuiNote* note = input_ctx_note_info(ic);
  const RolltuiWidgetEnv* env = rolltui_windows_env(ic->w);
  int nw = note->text.n == 0
               ? 0
               : rolltui_u_display_width(ic->uscratch, note->text.p ? note->text.p : "", note->text.n,
                                        env->ambiguous_wide);
  return rolltui_input_window_rows(text_rows, input_ctx_end_col(ic), nw, width, ic->max_rows);
}
static int input_ctx_note_owns_row(RolltuiInputCtx* ic, int width) {
  RolltuiNote* note = input_ctx_note_info(ic);
  if (note->text.n == 0) return 0;
  return input_ctx_rows_with_note(ic, width, rolltui_input_rows_for(ic->ed, width)) > 1;
}
static RolltuiRect input_ctx_text_rect(RolltuiInputCtx* ic, RolltuiRect r) {
  if (r.h > 1 && input_ctx_note_owns_row(ic, r.w)) r.h -= 1;
  return r;
}

static int input_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  if (rolltui_windows_has_submit(ic->w, ic->source.p ? ic->source.p : "", ic->source.n)) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &ic->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}
static int input_ctx_desired_outer(void* ctx, int inner_w, int parent_extent, int border, int* out) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  int v;
  ic->max_rows = rolltui_input_max_rows(parent_extent, border);
  v = input_ctx_rows_with_note(ic, inner_w, rolltui_input_rows_for(ic->ed, inner_w)) + border;
  *out = v > ic->min_outer ? v : ic->min_outer;
  return 1;
}
static void input_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  const RolltuiInputOptions* cur = rolltui_input_options(ic->ed);
  const RolltuiWidgetEnv* env = rolltui_windows_env(ic->w);
  unsigned char aw = env->ambiguous_wide ? 1 : 0;
  int inset = rn->node->border != 0 ? 1 : 0;
  if (cur->ambiguous_wide != aw || cur->inset != inset) {
    RolltuiInputOptions o;
    memset(&o, 0, sizeof o);
    rolltui_input_options_copy(&o, cur);
    o.ambiguous_wide = aw;
    o.inset = inset;
    rolltui_input_set_options(ic->ed, &o);
    rolltui_input_options_release(&o);
  }
  rolltui_input_layout(ic->ed, input_ctx_text_rect(ic, rn->inner));
}
static void input_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(ic->w);
  const RolltuiStyle* styles = rolltui_windows_styles(ic->w);
  RolltuiRect r = rn->inner;
  RolltuiRect tr = input_ctx_text_rect(ic, r);
  RolltuiInputRoles iroles;
  RolltuiNote* note;
  int nw, used;
  iroles.text = ic->roles.input_text;
  iroles.selection = ic->roles.input_selection;
  iroles.placeholder = ic->roles.input_placeholder;
  rolltui_input_layout(ic->ed, tr);
  rolltui_input_draw(ic->ed, f, ic->draw, styles, &iroles, rn->focused);
  note = input_ctx_note_info(ic);
  if (note->text.n == 0) return;
  nw = rolltui_u_display_width(ic->uscratch, note->text.p ? note->text.p : "", note->text.n, env->ambiguous_wide);
  if (tr.h < r.h) { /* its own row, under the text */
    used = rolltui_frame_put_text(f, ic->draw, r.x, r.y + tr.h, note->text.p ? note->text.p : "", note->text.n,
                                  styles[ic->roles.text_muted], r.w > 0 ? r.w : 0, env->ambiguous_wide, 0);
    rolltui_frame_mark(f, r.x, r.y + tr.h, used, note->state, note->since_ms, 0.0);
    return;
  }
  {
    int endc = input_ctx_end_col(ic);
    int nx = r.x + r.w - nw;
    int alt = r.x + endc + 2;
    if (alt > nx) nx = alt;
    used = rolltui_frame_put_text(f, ic->draw, nx, r.y, note->text.p ? note->text.p : "", note->text.n,
                                  styles[ic->roles.text_muted], (r.x + r.w - nx) > 0 ? (r.x + r.w - nx) : 0,
                                  env->ambiguous_wide, 0);
    rolltui_frame_mark(f, nx, r.y, used, note->state, note->since_ms, 0.0);
  }
}

int rolltui_input_kind_process_event(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                     const RolltuiEvent* e) {
  const RolltuiBindings* b = rolltui_windows_bindings(w);
  const RolltuiWidgetEnv* env = rolltui_windows_env(w);
  /* The action names are `w`'s rather than a parameter: with the `input`
   * FACTORY on this side there is no caller in the middle holding them, and one source beats
   * two ways to say the same thing. */
  int action = (int)rolltui_input_handle(ed, e, b, rolltui_windows_input_actions(w), env->now_ms);
  if (action != ROLLTUI_INPUT_SUBMIT) return action;
  {
    size_t tlen = 0;
    const char* t = rolltui_input_text(ed, &tlen);
    RolltuiStr text;
    memset(&text, 0, sizeof text);
    rolltui_str_set(&text, t, tlen);
    /* A prompt sends and starts fresh; a find bar keeps its standing query (Widgets.hpp). */
    if (!rolltui_windows_on_submit(w, source, source_len)) {
      rolltui_input_push_history(ed, text.p ? text.p : "", text.n);
      rolltui_input_clear(ed);
    }
    rolltui_windows_call_submit(w, source, source_len, text.p ? text.p : "", text.n);
    rolltui_str_free(&text);
  }
  return action;
}
static int input_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  return rolltui_input_kind_process_event(ic->ed, ic->w, ic->source.p ? ic->source.p : "", ic->source.n, e) !=
         ROLLTUI_INPUT_IGNORED;
}
static void input_ctx_destroy(void* ctx) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  rolltui_str_free(&ic->source);
  rolltui_str_free(&ic->note.text);
  rolltui_u_scratch_free(ic->uscratch);
  rolltui_draw_scratch_free(ic->draw);
  rolltui_mem_free(ic);
}

static const RolltuiWidgetPlugin kInputPlugin = {
    input_ctx_destroy,  input_ctx_layout, input_ctx_draw, input_ctx_problem, NULL,
    input_ctx_desired_outer, input_ctx_handle, NULL, NULL,
};

const RolltuiWidgetPlugin* rolltui_input_widget_plugin(void) { return &kInputPlugin; }

/* The editor is the window TABLE's, asked for by source — the same object
 * `rolltui_windows_input` hands a host, never a second one built here. */
static RolltuiWidget input_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  unsigned char problem = 0;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  RolltuiStr why;
  RolltuiInputCtx* ic;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  ic = (RolltuiInputCtx*)rolltui_mem_alloc(sizeof *ic);
  memset(ic, 0, sizeof *ic);
  ic->ed = rolltui_windows_input(w, source, source_len);
  ic->w = w;
  rolltui_str_set(&ic->source, source, source_len);
  ic->roles = *rolltui_windows_builtin_roles(w);
  ic->uscratch = rolltui_u_scratch_new();
  ic->draw = rolltui_draw_scratch_new();
  ic->max_rows = 1;
  out.vt = &kInputPlugin;
  out.ctx = ic;
  return out;
}
void rolltui_input_widget_ctx_set_min_outer(void* ctx, int rows) { ((RolltuiInputCtx*)ctx)->min_outer = rows; }

/* ============================================================================================
 * transcript:<document> — BORROWS the RolltuiTranscript* Windows' own `transcripts_` map owns
 *. What still does NOT cross: the syntax highlighter, pushed straight onto the
 * `RolltuiTranscript` by `Windows::set_highlighter`/`transcript()` in Widgets.cpp, so this ctx
 * never touches it and needs no epoch to poll.
 * ============================================================================================ */

typedef struct RolltuiTranscriptCtx {
  RolltuiTranscript* t; /* BORROWED — Windows' own transcripts_ map owns it */
  RolltuiWindows* w;    /* BORROWED */
  RolltuiStr source;
  RolltuiDrawScratch* draw;
} RolltuiTranscriptCtx;

static void transcript_ctx_options(const RolltuiTranscriptCtx* tc, const RolltuiResolvedNode* rn,
                                   RolltuiTranscriptOptions* out) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(tc->w);
  const RolltuiCodeFold* cf = rolltui_windows_code_fold(tc->w);
  memset(out, 0, sizeof *out);
  out->ambiguous_wide = env->ambiguous_wide;
  out->tab_width = 8;
  out->gap = 1;
  out->inset = rn->node->border != 0 ? 1 : 0;
  out->wheel_lines = 3;
  out->code_fold_over_lines = cf->fold_over_lines;
  out->code_cap_lines = cf->cap_lines;
  out->multi_click_ms = 400;
}

static int transcript_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  if (rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n)) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &tc->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}
static void transcript_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  const RolltuiDocument* doc = rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n);
  RolltuiTranscriptOptions o;
  if (!doc) return;
  transcript_ctx_options(tc, rn, &o);
  rolltui_transcript_layout(tc->t, doc, rn->inner, &o);
}
static void transcript_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  const RolltuiDocument* doc = rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n);
  RolltuiTranscriptOptions o;
  if (!doc) return;
  transcript_ctx_options(tc, rn, &o);
  rolltui_transcript_layout(tc->t, doc, rn->inner, &o);
  rolltui_transcript_draw(tc->t, f, tc->draw, rolltui_windows_styles(tc->w));
}
static int transcript_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  const RolltuiDocument* doc = rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n);
  const RolltuiWidgetEnv* env;
  if (!doc) return 0;
  env = rolltui_windows_env(tc->w);
  return rolltui_transcript_handle(tc->t, e, doc, env->now_ms, rolltui_windows_bindings(tc->w),
                                   rolltui_windows_transcript_actions(tc->w));
}
static int transcript_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  int vh;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  vh = rolltui_transcript_viewport_height(tc->t);
  out->first = rolltui_transcript_top_line(tc->t);
  out->visible = (size_t)(vh > 0 ? vh : 0);
  out->total = rolltui_transcript_total_lines(tc->t);
  return 1;
}
static int transcript_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  long delta;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  delta = (long)first - (long)rolltui_transcript_top_line(tc->t);
  rolltui_transcript_scroll_by(tc->t, delta);
  return 1;
}
static void transcript_ctx_destroy(void* ctx) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  rolltui_str_free(&tc->source);
  rolltui_draw_scratch_free(tc->draw);
  rolltui_mem_free(tc);
}

static const RolltuiWidgetPlugin kTranscriptPlugin = {
    transcript_ctx_destroy, transcript_ctx_layout, transcript_ctx_draw, transcript_ctx_problem, NULL,
    NULL,                   transcript_ctx_handle, transcript_ctx_scroll_extent, transcript_ctx_scroll_to,
};

static const RolltuiWidgetPlugin* transcript_widget_plugin(void) { return &kTranscriptPlugin; }

static RolltuiWidget transcript_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  unsigned char problem = 0;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  RolltuiStr why;
  RolltuiTranscriptCtx* tc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  tc = (RolltuiTranscriptCtx*)rolltui_mem_alloc(sizeof *tc);
  memset(tc, 0, sizeof *tc);
  tc->t = rolltui_windows_transcript(w, source, source_len);
  tc->w = w;
  rolltui_str_set(&tc->source, source, source_len);
  tc->draw = rolltui_draw_scratch_new();
  out.vt = transcript_widget_plugin();
  out.ctx = tc;
  return out;
}

/* ============================================================================================
 * menu:<name> — a menu FILE (Widgets.hpp's three rungs), re-read like `file:` when it changes
 * on disk. BORROWS the RolltuiMenu* Windows' own `menus_` map owns; what still does NOT cross:
 * the text-field VALIDATOR REGISTRY (`rolltui::Menu`'s own, asked through
 * `rolltui_menu_set_validator_fn` exactly as before this ctx existed).
 * ============================================================================================ */

typedef struct RolltuiMenuCtx {
  RolltuiMenu* m;    /* BORROWED — Windows' own menus_ map owns it */
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  int loaded;
  long long stamp;
  RolltuiStr origin;  /* "" | a path | "the host's" | "a shipped menu" */
  RolltuiStr problem; /* "" when the file resolved and parsed fine */
  /* The file's own unknown_keys + bad_values, formatted, refreshed only when the FILE changes. */
  RolltuiStr* notes;
  size_t notes_n, notes_cap;
  /* An item naming an undeclared action — recomputed against the LIVE bindings at the start
   * of every note_at(ctx, 0, …) pass, since this can change with no file re-read at all. */
  RolltuiStr* live_notes;
  size_t live_notes_n, live_notes_cap;
  RolltuiDrawScratch* draw;
} RolltuiMenuCtx;

static void menu_ctx_user_path(const RolltuiMenuCtx* mc, RolltuiStr* out) {
  size_t dir_len = 0;
  const char* dir = rolltui_windows_dir(mc->w, &dir_len);
  rolltui_str_clear(out);
  if (dir_len == 0) return; /* "" — no preset directory set */
  rolltui_str_set(out, dir, dir_len);
  rolltui_str_append(out, "/menus/", sizeof("/menus/") - 1);
  rolltui_str_append_str(out, &mc->source);
  rolltui_str_append(out, ".json", sizeof(".json") - 1);
}

/* Which rung answers, and with what — the three-rung order Widgets.hpp states. A file the
 * user has is preferred even when it is unreadable garbage: shadowing must not fail over to
 * a different menu, or a typo in one's own file is a silent substitution. */
static void menu_ctx_resolve(RolltuiMenuCtx* mc, RolltuiStr* text, RolltuiStr* origin, long long* stamp) {
  RolltuiStr p;
  memset(&p, 0, sizeof p);
  menu_ctx_user_path(mc, &p);
  *stamp = p.n == 0 ? -1 : file_ctx_stamp(p.p);
  if (*stamp >= 0) {
    rolltui_str_clear(text);
    read_whole_file(p.p, text); /* an unreadable race after a successful stat is ignored, as
                                  * the C++ original did: the empty text then reports as a
                                  * bad-JSON "is unusable", an honest if rare outcome. */
    rolltui_str_move(origin, &p);
    return;
  }
  rolltui_str_free(&p);
  {
    size_t hlen = 0;
    const char* h = rolltui_windows_host_menu(mc->w, mc->source.p ? mc->source.p : "", mc->source.n, &hlen);
    if (h) {
      rolltui_str_set(text, h, hlen);
      rolltui_str_set(origin, "the host's", sizeof("the host's") - 1);
      return;
    }
  }
  {
    const char* s =
        rolltui_embedded_text(rolltui_kMenus, rolltui_kMenuCount, mc->source.p ? mc->source.p : "", mc->source.n);
    if (s) {
      rolltui_str_set(text, s, strlen(s));
      rolltui_str_set(origin, "a shipped menu", sizeof("a shipped menu") - 1);
      return;
    }
  }
  rolltui_str_clear(text);
  rolltui_str_clear(origin);
}

static RolltuiStr* menu_ctx_note_add(RolltuiMenuCtx* mc) {
  mc->notes = (RolltuiStr*)rolltui_grow_zeroed(mc->notes, &mc->notes_cap, mc->notes_n + 1, sizeof *mc->notes);
  return &mc->notes[mc->notes_n++];
}

/* Re-resolves and re-parses only when the rung or the file's stamp changed — the same
 * loaded_/stamp_/origin_ check `file_ctx_refresh` makes for `file:`, one rung wider. Called
 * from every slot that needs current state (problem/note_at/layout/scroll_extent), so it is
 * cheap when nothing changed: a stat() and a string compare. */
static void menu_ctx_refresh(RolltuiMenuCtx* mc) {
  RolltuiStr text, origin;
  long long stamp = -1;
  size_t i;
  memset(&text, 0, sizeof text);
  memset(&origin, 0, sizeof origin);
  menu_ctx_resolve(mc, &text, &origin, &stamp);
  if (mc->loaded && rolltui_str_eq(&mc->origin, origin.p ? origin.p : "", origin.n) && stamp == mc->stamp) {
    rolltui_str_free(&text);
    rolltui_str_free(&origin);
    return;
  }
  mc->loaded = 1;
  rolltui_str_move(&mc->origin, &origin);
  mc->stamp = stamp;
  for (i = 0; i < mc->notes_n; ++i) rolltui_str_free(&mc->notes[i]);
  mc->notes_n = 0;
  if (mc->origin.n == 0) {
    /* No rung answered: an empty submenu named after itself, and a problem that says
     * exactly where all three rungs were looked for. */
    RolltuiMenuItem root;
    RolltuiStr up;
    memset(&up, 0, sizeof up);
    rolltui_menu_item_init(&root);
    root.kind = ROLLTUI_MENU_SUBMENU;
    rolltui_str_set(&root.id, mc->source.p ? mc->source.p : "", mc->source.n);
    rolltui_str_set(&root.label, mc->source.p ? mc->source.p : "", mc->source.n);
    rolltui_menu_set_root(mc->m, &root);
    rolltui_menu_item_release(&root);
    menu_ctx_user_path(mc, &up);
    rolltui_str_clear(&mc->problem);
    rolltui_str_append(&mc->problem, "no menu file '", sizeof("no menu file '") - 1);
    rolltui_str_append_str(&mc->problem, &mc->source);
    rolltui_str_append(&mc->problem, "' (looked for ", sizeof("' (looked for ") - 1);
    if (up.n == 0) {
      rolltui_str_append(&mc->problem, "menus/", sizeof("menus/") - 1);
      rolltui_str_append_str(&mc->problem, &mc->source);
      rolltui_str_append(&mc->problem, ".json under a preset directory (none set)",
                         sizeof(".json under a preset directory (none set)") - 1);
    } else {
      rolltui_str_append(&mc->problem, "'", 1);
      rolltui_str_append_str(&mc->problem, &up);
      rolltui_str_append(&mc->problem, "'", 1);
    }
    rolltui_str_append(&mc->problem, ", the host's menus and the shipped ones)",
                       sizeof(", the host's menus and the shipped ones)") - 1);
    rolltui_str_free(&up);
    rolltui_str_free(&text);
    return;
  }
  {
    RolltuiMenuItem root;
    RolltuiMenuLoadReport rep;
    int ok;
    memset(&rep, 0, sizeof rep);
    rolltui_menu_item_init(&root);
    ok = rolltui_menu_parse_json(text.p ? text.p : "", text.n, &root, &rep);
    if (!ok) {
      RolltuiMenuItem empty;
      rolltui_menu_item_init(&empty);
      empty.kind = ROLLTUI_MENU_SUBMENU;
      rolltui_str_set(&empty.id, mc->source.p ? mc->source.p : "", mc->source.n);
      rolltui_str_set(&empty.label, mc->source.p ? mc->source.p : "", mc->source.n);
      rolltui_menu_set_root(mc->m, &empty);
      rolltui_menu_item_release(&empty);
      rolltui_str_clear(&mc->problem);
      rolltui_str_append(&mc->problem, "menu file (", sizeof("menu file (") - 1);
      rolltui_str_append_str(&mc->problem, &mc->origin);
      rolltui_str_append(&mc->problem, ") is unusable: ", sizeof(") is unusable: ") - 1);
      rolltui_str_append_str(&mc->problem, &rep.error);
    } else {
      rolltui_str_clear(&mc->problem);
      for (i = 0; i < rep.unknown_keys_n; ++i) {
        RolltuiStr* n = menu_ctx_note_add(mc);
        rolltui_str_clear(n);
        rolltui_str_append(n, "menu file (", sizeof("menu file (") - 1);
        rolltui_str_append_str(n, &mc->origin);
        rolltui_str_append(n, "): ", sizeof("): ") - 1);
        rolltui_str_append_str(n, &rep.unknown_keys[i]);
      }
      for (i = 0; i < rep.bad_values_n; ++i) {
        RolltuiStr* n = menu_ctx_note_add(mc);
        rolltui_str_clear(n);
        rolltui_str_append(n, "menu file (", sizeof("menu file (") - 1);
        rolltui_str_append_str(n, &mc->origin);
        rolltui_str_append(n, "): ", sizeof("): ") - 1);
        rolltui_str_append_str(n, &rep.bad_values[i]);
      }
      rolltui_menu_set_root(mc->m, &root);
    }
    rolltui_menu_item_release(&root);
    rolltui_menu_load_report_release(&rep);
  }
  rolltui_str_free(&text);
}

static int menu_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  menu_ctx_refresh(mc);
  if (mc->problem.n == 0) return 0;
  rolltui_str_set(out, mc->problem.p ? mc->problem.p : "", mc->problem.n);
  return 1;
}

static RolltuiStr* menu_ctx_live_note_add(RolltuiMenuCtx* mc) {
  mc->live_notes =
      (RolltuiStr*)rolltui_grow_zeroed(mc->live_notes, &mc->live_notes_cap, mc->live_notes_n + 1, sizeof *mc->live_notes);
  return &mc->live_notes[mc->live_notes_n++];
}

/* Mirrors `Menu::item_actions()` + the undeclared-action check `MenuWidget::notes()` made of
 * it: every item naming an `action_name` the live bindings do not declare, named by item id
 * and by action — never silently dropped. */
static void menu_ctx_collect_action_notes(RolltuiMenuCtx* mc, RolltuiMenuItem* it, const RolltuiBindings* b) {
  size_t i, n;
  if (it->action_name.n > 0 && !rolltui_bindings_has(b, it->action_name.p, it->action_name.n)) {
    RolltuiStr* note = menu_ctx_live_note_add(mc);
    rolltui_str_clear(note);
    rolltui_str_append(note, "menu file (", sizeof("menu file (") - 1);
    rolltui_str_append_str(note, &mc->origin);
    rolltui_str_append(note, "): item '", sizeof("): item '") - 1);
    rolltui_str_append_str(note, &it->id);
    rolltui_str_append(note, "' names the action '", sizeof("' names the action '") - 1);
    rolltui_str_append_str(note, &it->action_name);
    rolltui_str_append(note, "', which no layout declares", sizeof("', which no layout declares") - 1);
  }
  n = rolltui_menu_list_count(&it->children);
  for (i = 0; i < n; ++i) menu_ctx_collect_action_notes(mc, rolltui_menu_list_at(&it->children, i), b);
}

static int menu_ctx_note_at(void* ctx, size_t i, RolltuiStr* out) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  menu_ctx_refresh(mc);
  if (i == 0) {
    size_t k;
    for (k = 0; k < mc->live_notes_n; ++k) rolltui_str_free(&mc->live_notes[k]);
    mc->live_notes_n = 0;
    menu_ctx_collect_action_notes(mc, rolltui_menu_root(mc->m), rolltui_windows_bindings(mc->w));
  }
  if (i < mc->notes_n) {
    rolltui_str_set(out, mc->notes[i].p ? mc->notes[i].p : "", mc->notes[i].n);
    return 1;
  }
  {
    const size_t j = i - mc->notes_n;
    if (j < mc->live_notes_n) {
      rolltui_str_set(out, mc->live_notes[j].p ? mc->live_notes[j].p : "", mc->live_notes[j].n);
      return 1;
    }
  }
  return 0;
}

/* Mirrors `Bindings::chords_text` exactly, over `rolltui_bindings.h` — the same chord-joining
 * loop `help_chords_text` above already is, reused directly rather than written a third time. */
static void menu_kind_fill_shortcuts(RolltuiMenuItem* it, const RolltuiBindings* b) {
  size_t i, n;
  if (it->action_name.n > 0) {
    if (rolltui_bindings_has(b, it->action_name.p, it->action_name.n))
      rolltui_bindings_chords_text(b, it->action_name.p, it->action_name.n, &it->shortcut);
    else
      rolltui_str_clear(&it->shortcut);
  }
  n = rolltui_menu_list_count(&it->children);
  for (i = 0; i < n; ++i) menu_kind_fill_shortcuts(rolltui_menu_list_at(&it->children, i), b);
}

static int menu_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  menu_ctx_refresh(mc);
  out->first = (size_t)rolltui_menu_scroll_first(mc->m);
  out->visible = (size_t)rolltui_menu_scroll_visible(mc->m);
  out->total = rolltui_menu_visible(mc->m, NULL);
  return 1;
}

static void menu_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(mc->w);
  RolltuiMenuOptions o;
  menu_ctx_refresh(mc);
  menu_kind_fill_shortcuts(rolltui_menu_root(mc->m), rolltui_windows_bindings(mc->w));
  o = *rolltui_menu_options(mc->m);
  o.ambiguous_wide = env->ambiguous_wide;
  o.inset = rn->node->border != 0 ? 1 : 0;
  rolltui_menu_set_options_struct(mc->m, &o);
  rolltui_menu_layout(mc->m, rn->inner);
}

static void menu_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(mc->w);
  RolltuiInputRoles iroles;
  menu_ctx_layout(ctx, rn);
  iroles.text = br->input_text;
  iroles.selection = br->input_selection;
  iroles.placeholder = br->input_placeholder;
  rolltui_menu_draw(mc->m, f, mc->draw, rolltui_windows_styles(mc->w), rolltui_windows_menu_roles(mc->w), &iroles,
                    rn->focused);
}

static void menu_ctx_destroy(void* ctx) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  size_t i;
  rolltui_str_free(&mc->source);
  rolltui_str_free(&mc->origin);
  rolltui_str_free(&mc->problem);
  for (i = 0; i < mc->notes_n; ++i) rolltui_str_free(&mc->notes[i]);
  rolltui_mem_free(mc->notes);
  for (i = 0; i < mc->live_notes_n; ++i) rolltui_str_free(&mc->live_notes[i]);
  rolltui_mem_free(mc->live_notes);
  rolltui_draw_scratch_free(mc->draw);
  rolltui_mem_free(mc);
}

static const RolltuiWidgetPlugin kMenuPlugin = {
    menu_ctx_destroy, menu_ctx_layout, menu_ctx_draw, menu_ctx_problem, menu_ctx_note_at,
    NULL,             NULL,            menu_ctx_scroll_extent, NULL,
};

const RolltuiWidgetPlugin* rolltui_menu_widget_plugin(void) { return &kMenuPlugin; }

/* `rolltui_windows_menu` re-resolves the FILE through this very ctx, so it reaches back into
 * `rolltui_windows_widget_for` for the widget being built here. That terminates because the
 * table claims the row BEFORE running a factory (rolltui_widgets.c says so at the claim): the
 * re-entrant call sees a zeroed widget, skips the refresh, and hands back the menu object —
 * which is all this factory wants. The outer caller refreshes once this returns. */
static RolltuiWidget menu_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  unsigned char problem = 0;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  RolltuiStr why;
  RolltuiMenuCtx* mc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host,
                             &name, &name_len, &source, &source_len, &problem,
                             &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  mc = (RolltuiMenuCtx*)rolltui_mem_alloc(sizeof *mc);
  memset(mc, 0, sizeof *mc);
  mc->m = rolltui_windows_menu(w, source, source_len);
  mc->w = w;
  rolltui_str_set(&mc->source, source, source_len);
  mc->stamp = -1;
  mc->draw = rolltui_draw_scratch_new();
  out.vt = &kMenuPlugin;
  out.ctx = mc;
  return out;
}


/* ============================================================================================
 * theme — THE THEME EDITOR AS A WIDGET KIND.
 *
 * An app gets a theme editor by naming `theme` in a layout and binding a key to the popup that
 * holds it. It draws the editor's menu with a role sample, a status line and the badges the
 * theme computes as, and it answers the model's outcomes against the preset store the app
 * handed over — save, load, reset, write-shipped — so an app writes no editor code at all.
 *
 * THE THEME IS THE APP'S, AND THAT IS WHY THERE IS ONE CALL. The styles a frame is drawn with
 * are passed into `rolltui_windows_draw` by the host, so no widget can reach them: an app hands
 * over the Theme preset store it already keeps (`rolltui_windows_set_theme_store`) and every
 * commit lands in it, which an app watching the store's version picks up like any other change
 * to its theme. That call is not editor code and does not grow when the editor does.
 * ============================================================================================ */

typedef struct RolltuiThemeCtx {
  RolltuiWindows* w;         /* BORROWED */
  RolltuiThemeEditor* ed;    /* OWNED */
  RolltuiPresetStore* store; /* BORROWED; NULL until a host hands one over */
  RolltuiDrawScratch* draw;  /* OWNED */
  /* CALLER-FILLED working strings the ctx owns and REFILLS: the draw path builds a role line
   * and two status lines every frame, and a fresh string per frame would be three allocations
   * a frame forever. */
  RolltuiStr line, hint;
  int hint_is_problem; /* a refusal is drawn in `error`; "saved preset 'x'" is not a refusal */
  int persist;
} RolltuiThemeCtx;

/* The store's working colours into the editor, and the store's preset names into the Load
 * choice. A no-op with no store: the editor keeps the built-in pair it starts on. */
static void theme_ctx_sync_store(RolltuiThemeCtx* tc) {
  RolltuiThemePresetValue* v;
  RolltuiPresetList list;
  RolltuiStrList names;
  RolltuiThemeReport rep;
  size_t i;
  if (!tc->store) return;
  v = (RolltuiThemePresetValue*)rolltui_preset_store_working(tc->store);
  if (v) {
    memset(&rep, 0, sizeof rep);
    rolltui_theme_editor_load(tc->ed, v->colours, &rep);
    rolltui_theme_report_release(&rep);
    rolltui_preset_store_value_free(tc->store, v);
  }
  memset(&list, 0, sizeof list);
  memset(&names, 0, sizeof names);
  rolltui_preset_store_list(tc->store, &list);
  for (i = 0; i < list.n; ++i) rolltui_str_list_add(&names, list.v[i].name.p, list.v[i].name.n);
  rolltui_theme_editor_set_presets(tc->ed, &names);
  rolltui_str_list_release(&names);
  rolltui_preset_list_release(&list);
}

/* `rolltui_preset_store_edit` hands the domain's value to a callback; for the Theme domain
 * that is a `RolltuiThemePresetValue`, whose `colours` this replaces. */
static void theme_set_colours(void* value, void* c) {
  RolltuiThemePresetValue* v = (RolltuiThemePresetValue*)value;
  rolltui_json_free(v->colours);
  v->colours = (RolltuiJsonValue*)c;
}

static void theme_ctx_write_back(RolltuiThemeCtx* tc) {
  size_t olen = 0;
  const char* origin;
  if (!tc->store) return;
  origin = rolltui_preset_store_origin(tc->store, &olen);
  rolltui_preset_store_edit(tc->store, theme_set_colours,
                            rolltui_theme_editor_colours_json(tc->ed, origin ? origin : "", olen), tc->persist);
}

static void theme_ctx_apply(RolltuiThemeCtx* tc, const RolltuiThemeEditorOutcome* o) {
  char buf[192];
  rolltui_str_clear(&tc->hint);
  tc->hint_is_problem = 0;
  switch (o->kind) {
    case ROLLTUI_THEME_EDIT_COMMITTED:
      theme_ctx_write_back(tc);
      break;
    case ROLLTUI_THEME_EDIT_SAVE_AS: {
      RolltuiStr err;
      int r;
      if (!tc->store) break;
      memset(&err, 0, sizeof err);
      theme_ctx_write_back(tc);
      /* NEVER overwrite: a name already taken comes back as its own answer and the person
       * types another. Replacing someone's preset because they reused a name is a data loss
       * the store deliberately offers to refuse, and a widget has nowhere to ask. */
      r = rolltui_preset_store_save_as(tc->store, o->value.p ? o->value.p : "", o->value.n, /*overwrite=*/0, &err);
      if (r == ROLLTUI_SAVE_SAVED) {
        snprintf(buf, sizeof buf, "saved preset '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
        rolltui_str_set(&tc->hint, buf, strlen(buf));
        theme_ctx_sync_store(tc); /* the new name joins the Load choice */
      } else if (r == ROLLTUI_SAVE_EXISTS_ASK) {
        snprintf(buf, sizeof buf, "'%.*s' already exists \xE2\x80\x94 choose another name",
                 (int)o->value.n, o->value.p ? o->value.p : "");
        rolltui_str_set(&tc->hint, buf, strlen(buf));
        tc->hint_is_problem = 1;
      } else {
        rolltui_str_set(&tc->hint, K("cannot save: "));
        rolltui_str_append_str(&tc->hint, &err);
        tc->hint_is_problem = 1;
      }
      rolltui_str_free(&err);
      break;
    }
    case ROLLTUI_THEME_EDIT_LOAD_PRESET: {
      RolltuiThemePresetReport rep;
      if (!tc->store) break;
      memset(&rep, 0, sizeof rep);
      if (rolltui_preset_store_load(tc->store, o->value.p ? o->value.p : "", o->value.n, &rep, tc->persist)) {
        theme_ctx_sync_store(tc);
        snprintf(buf, sizeof buf, "loaded '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
      } else {
        snprintf(buf, sizeof buf, "cannot load '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
        tc->hint_is_problem = 1;
      }
      rolltui_str_set(&tc->hint, buf, strlen(buf));
      rolltui_theme_preset_report_release(&rep);
      break;
    }
    case ROLLTUI_THEME_EDIT_RESET_LOADED: {
      /* THE ORIGIN PRESET, re-read — not the working copy, which is the edited value this is
       * meant to throw away. */
      RolltuiThemePresetReport rep;
      RolltuiThemePresetValue* v;
      size_t olen = 0;
      const char* origin;
      if (!tc->store) break;
      memset(&rep, 0, sizeof rep);
      origin = rolltui_preset_store_origin(tc->store, &olen);
      v = (RolltuiThemePresetValue*)rolltui_preset_store_get(tc->store, origin ? origin : "", olen, &rep);
      if (v) {
        RolltuiThemeReport tr;
        memset(&tr, 0, sizeof tr);
        rolltui_theme_editor_load(tc->ed, v->colours, &tr);
        rolltui_theme_report_release(&tr);
        rolltui_preset_store_value_free(tc->store, v);
        theme_ctx_write_back(tc);
        rolltui_str_set(&tc->hint, K("reset to the loaded preset (undoable)"));
      } else {
        rolltui_str_set(&tc->hint, K("the loaded preset is no longer readable"));
        tc->hint_is_problem = 1;
      }
      rolltui_theme_preset_report_release(&rep);
      break;
    }
    case ROLLTUI_THEME_EDIT_RESET_BUILTIN: {
      RolltuiStyle dark[ROLLTUI_ROLE_COUNT], light[ROLLTUI_ROLE_COUNT];
      RolltuiEffectMap* eff = rolltui_theme_builtin_fill(K("default-dark"), dark, ROLLTUI_ROLE_COUNT);
      rolltui_effect_map_free(rolltui_theme_builtin_fill(K("default-light"), light, ROLLTUI_ROLE_COUNT));
      rolltui_theme_editor_replace(tc->ed, dark, light, K("default-dark"), K("default-light"), NULL, NULL, eff);
      theme_ctx_write_back(tc);
      rolltui_str_set(&tc->hint, K("reset to the built-in default (undoable)"));
      break;
    }
    case ROLLTUI_THEME_EDIT_WRITE_SHIPPED:
      /* A shipped preset is read-only unless the store was opened with the privilege; the
       * store refuses on its own and says so. Overwriting IS the operation here — the name
       * chosen is one that already ships — so this is the one save that asks for it. */
      if (!tc->store) break;
      {
        RolltuiStr err;
        memset(&err, 0, sizeof err);
        theme_ctx_write_back(tc);
        if (rolltui_preset_store_save_as(tc->store, o->value.p ? o->value.p : "", o->value.n, /*overwrite=*/1,
                                         &err) == ROLLTUI_SAVE_SAVED) {
          snprintf(buf, sizeof buf, "wrote shipped '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
        } else {
          snprintf(buf, sizeof buf, "refused: %.*s", (int)err.n, err.p ? err.p : "");
          tc->hint_is_problem = 1;
        }
        rolltui_str_set(&tc->hint, buf, strlen(buf));
        rolltui_str_free(&err);
      }
      break;
    case ROLLTUI_THEME_EDIT_CHECK:
      /* The report is the badges line's long form; a window this size cannot hold it, so the
       * one-line answer stands and a host that wants the whole thing draws a `text:` window
       * over `rolltui_theme_editor_report`. */
      rolltui_theme_editor_badges_line(tc->ed, &tc->hint);
      break;
    default:
      break;
  }
}

static int theme_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  RolltuiThemeEditorOutcome o;
  memset(&o, 0, sizeof o);
  rolltui_theme_editor_handle(tc->ed, e, rolltui_windows_bindings(tc->w), &o);
  theme_ctx_apply(tc, &o);
  {
    const int consumed = o.kind != ROLLTUI_THEME_EDIT_NONE;
    rolltui_theme_editor_outcome_release(&o);
    return consumed;
  }
}

/* The bottom six rows are the sample box and the two status lines, exactly as the menu's own
 * scrolling assumes: the menu is laid out into what is left. */
#define ROLLTUI_THEME_BOX_ROWS 6

static void theme_ctx_menu_rect(const RolltuiResolvedNode* rn, RolltuiRect* out) {
  int box;
  rolltui_content_rect(rn, out);
  box = out->h < ROLLTUI_THEME_BOX_ROWS ? out->h : ROLLTUI_THEME_BOX_ROWS;
  out->h -= box;
}

static void theme_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(tc->w);
  RolltuiMenu* m = rolltui_theme_editor_menu(tc->ed);
  RolltuiMenuOptions o = *rolltui_menu_options(m);
  RolltuiRect r;
  theme_ctx_menu_rect(rn, &r);
  o.ambiguous_wide = env->ambiguous_wide;
  o.inset = 0;
  rolltui_menu_set_options_struct(m, &o);
  rolltui_menu_layout(m, r);
}

static int theme_put(RolltuiThemeCtx* tc, RolltuiFrame* f, int x, int y, const char* text, size_t len,
                     RolltuiStyle st, int max_cells) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(tc->w);
  if (max_cells <= 0) return 0;
  return rolltui_frame_put_text(f, tc->draw, x, y, text, len, st, max_cells, env->ambiguous_wide, 0);
}

/* One colour swatch: a run of spaces whose BACKGROUND is the colour, which is the only way to
 * show a colour that does not depend on a glyph being legible in it. */
static int theme_swatch(RolltuiThemeCtx* tc, RolltuiFrame* f, int x, int y, RolltuiStyleColor c, int max_cells) {
  RolltuiStyle st;
  memset(&st, 0, sizeof st);
  st.bg = c;
  return theme_put(tc, f, x, y, "      ", 6, st, max_cells);
}

static void theme_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const RolltuiStyle* styles = rolltui_windows_styles(tc->w);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(tc->w);
  RolltuiMenu* m = rolltui_theme_editor_menu(tc->ed);
  RolltuiRect r, mr;
  RolltuiStyle label, value;
  unsigned char role = 0;
  int y;

  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  theme_ctx_layout(ctx, rn);
  theme_ctx_menu_rect(rn, &mr);
  if (mr.h > 0) {
    RolltuiInputRoles iroles;
    iroles.text = br->input_text;
    iroles.selection = br->input_selection;
    iroles.placeholder = br->input_placeholder;
    rolltui_menu_draw(m, f, tc->draw, styles, rolltui_windows_menu_roles(tc->w), &iroles, rn->focused);
  }
  label = styles[br->label];
  value = styles[br->value];
  y = r.y + mr.h;

  if (rolltui_theme_editor_focused_role(tc->ed, &role)) {
    const RolltuiStyle s = rolltui_theme_editor_styles(tc->ed, rolltui_theme_editor_mode(tc->ed), 0)[role];
    size_t rn_len = 0, a;
    const char* rn_p = rolltui_role_name(role, &rn_len);
    char buf[ROLLTUI_COLOR_STRING_MAX];
    size_t clen = 0;
    RolltuiStyleColor highlighted;
    rolltui_str_clear(&tc->line);
    rolltui_str_append(&tc->line, rn_p ? rn_p : "", rn_len);
    rolltui_str_append(&tc->line, K("  fg "));
    clen = rolltui_color_to_string(s.fg, buf, sizeof buf);
    rolltui_str_append(&tc->line, buf, clen);
    rolltui_str_append(&tc->line, K("  bg "));
    clen = rolltui_color_to_string(s.bg, buf, sizeof buf);
    rolltui_str_append(&tc->line, buf, clen);
    {
      /* The five attributes, in the order the editor's menu lists them. */
      const char* const attrs[5] = {"bold", "italic", "underline", "dim", "reverse"};
      const unsigned char on[5] = {s.bold, s.italic, s.underline, s.dim, s.reverse};
      for (a = 0; a < 5; ++a)
        if (on[a]) {
          rolltui_str_append(&tc->line, K("  "));
          rolltui_str_append(&tc->line, attrs[a], strlen(attrs[a]));
        }
    }
    if (y < r.y + r.h) theme_put(tc, f, r.x, y++, tc->line.p, tc->line.n, label, r.w);
    if (y < r.y + r.h)
      theme_put(tc, f, r.x, y++, K(ROLLTUI_THEME_EDITOR_SAMPLE), s, r.w);
    if (y < r.y + r.h) {
      int x = r.x;
      x += theme_put(tc, f, x, y, K("fg "), label, r.w - (x - r.x));
      x += theme_swatch(tc, f, x, y, s.fg, r.w - (x - r.x));
      x += theme_put(tc, f, x, y, K("  bg "), label, r.w - (x - r.x));
      x += theme_swatch(tc, f, x, y, s.bg, r.w - (x - r.x));
      if (rolltui_theme_editor_highlighted_color(tc->ed, &highlighted)) {
        x += theme_put(tc, f, x, y, K(ROLLTUI_THEME_EDITOR_SWATCH_MARK), label, r.w - (x - r.x));
        theme_swatch(tc, f, x, y, highlighted, r.w - (x - r.x));
      }
      ++y;
    }
  } else {
    if (y < r.y + r.h) {
      rolltui_str_clear(&tc->line);
      rolltui_str_append(&tc->line, K("preset: "));
      if (tc->store) rolltui_preset_store_label(tc->store, &tc->line);
      else rolltui_str_append(&tc->line, K("(this app keeps no theme presets)"));
      theme_put(tc, f, r.x, y++, tc->line.p, tc->line.n, label, r.w);
    }
    if (y < r.y + r.h)
      theme_put(tc, f, r.x, y++, K(ROLLTUI_THEME_EDITOR_BREADCRUMB), value, r.w);
    if (y < r.y + r.h)
      theme_put(tc, f, r.x, y++, K(ROLLTUI_THEME_EDITOR_KEYS_HINT), value, r.w);
  }
  if (y < r.y + r.h) {
    rolltui_str_clear(&tc->line);
    rolltui_theme_editor_status_line(tc->ed, &tc->line);
    theme_put(tc, f, r.x, y++, tc->line.p, tc->line.n, value, r.w);
  }
  if (y < r.y + r.h) {
    if (tc->hint.n != 0) {
      theme_put(tc, f, r.x, y++, tc->hint.p, tc->hint.n, styles[tc->hint_is_problem ? br->error : br->value], r.w);
    } else {
      rolltui_str_clear(&tc->line);
      rolltui_theme_editor_badges_line(tc->ed, &tc->line);
      theme_put(tc, f, r.x, y++, tc->line.p, tc->line.n, label, r.w);
    }
  }
}

static int theme_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  RolltuiMenu* m = rolltui_theme_editor_menu(tc->ed);
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  out->first = (size_t)rolltui_menu_scroll_first(m);
  out->visible = (size_t)rolltui_menu_scroll_visible(m);
  out->total = rolltui_menu_visible(m, NULL);
  return 1;
}

static void theme_ctx_destroy(void* ctx) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  rolltui_theme_editor_free(tc->ed);
  rolltui_draw_scratch_free(tc->draw);
  rolltui_str_free(&tc->line);
  rolltui_str_free(&tc->hint);
  rolltui_mem_free(tc);
}

static const RolltuiWidgetPlugin kThemePlugin = {
    theme_ctx_destroy, theme_ctx_layout, theme_ctx_draw, NULL, NULL,
    NULL,              theme_ctx_handle, theme_ctx_scroll_extent, NULL,
};

static RolltuiWidget theme_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  RolltuiThemeCtx* tc;
  RolltuiWidget out;
  (void)c;
  (void)content;
  (void)n;
  memset(&out, 0, sizeof out);
  tc = (RolltuiThemeCtx*)rolltui_mem_alloc(sizeof *tc);
  memset(tc, 0, sizeof *tc);
  tc->w = w;
  tc->ed = rolltui_theme_editor_new();
  tc->draw = rolltui_draw_scratch_new();
  tc->persist = 1;
  out.vt = &kThemePlugin;
  out.ctx = tc;
  return out;
}

/* The widget for `content` when it is a `theme` one, else NULL. `rolltui_windows_widget_for`
 * creates it if this screen has none, which is what lets a host wire the store before the first
 * draw; the kind check is what stops the call acting on some other kind's ctx. */
static RolltuiThemeCtx* theme_ctx_for(RolltuiWindows* w, const char* content, size_t len) {
  RolltuiWidget* widget;
  if (!w) return NULL;
  if (!content || len == 0) { content = "theme"; len = 5; }
  widget = rolltui_windows_widget_for(w, content, len);
  if (!widget || widget->vt != &kThemePlugin) return NULL;
  return (RolltuiThemeCtx*)widget->ctx;
}

void rolltui_windows_set_theme_store(RolltuiWindows* w, const char* content, size_t len, RolltuiPresetStore* store,
                                     int persist) {
  RolltuiThemeCtx* tc = theme_ctx_for(w, content, len);
  if (!tc) return;
  tc->store = store;
  tc->persist = persist;
  theme_ctx_sync_store(tc);
}


/* ============================================================================================
 * keys — THE KEYS EDITOR AS A WIDGET KIND.
 *
 * An app gets a keys editor by naming `keys` in a layout and binding a key to the popup that
 * holds it. It draws the editor's menu, which preset the table came from and one status line,
 * and it answers the model's outcomes against the preset store the app handed over — save,
 * load, reset — so an app writes no editor code at all.
 *
 * WHAT IT EDITS IS WHAT THE APP IS RUNNING, and that is the whole reason the baseline is the
 * live table rather than the store's working copy: an action is editable here only because
 * something DECLARED it, so a screen's own `app.*` actions are in the tree when the table is
 * the one the window stack is routing keys through, and absent when it is a file just parsed.
 *
 * WHERE A COMMIT GOES IS THE APP'S, AND THAT IS WHY THERE IS ONE CALL. The table a frame is
 * routed with is passed into the library by the host, so no widget can replace it: an app hands
 * over the Bindings preset store it already keeps (`rolltui_windows_set_bindings_store`) and
 * every commit lands in it, which an app watching the store's version picks up like any other
 * change to its keys. That call is not editor code and does not grow when the editor does.
 *
 * WRITE-SHIPPED IS NOT OFFERED HERE. Whether a store may overwrite a preset in the app's own
 * install directory is a fact the store does not expose, and writing there is a developer's act
 * with a rebuild behind it — so the choice stays empty and disabled rather than being offered
 * and refused. `rolltui-studio`, which is that developer's tool, drives the same model and
 * populates it itself.
 * ============================================================================================ */

typedef struct RolltuiKeysCtx {
  RolltuiWindows* w;         /* BORROWED */
  RolltuiKeysEditor* ed;     /* OWNED */
  RolltuiPresetStore* store; /* BORROWED; NULL until a host hands one over */
  RolltuiDrawScratch* draw;  /* OWNED */
  /* CALLER-FILLED working strings the ctx owns and REFILLS: the draw path builds a preset line
   * and a status line every frame, and a fresh string per frame would be two allocations a
   * frame forever. */
  RolltuiStr line, hint;
  int hint_is_problem; /* a refusal is drawn in `error`; "saved preset 'x'" is not a refusal */
  size_t declared_seen;  /* how many actions the live table declared when it was last taken */
  unsigned char capture_role;
  int persist;
} RolltuiKeysCtx;

/* THE BASELINE IS THE LIVE TABLE, AND IT IS RE-TAKEN WHEN THE SCREEN'S ACTIONS ARRIVE. A host
 * declares what its screen can do AFTER it has built its windows, so a widget built during that
 * construction has seen only the library's own actions and could never offer an `app.` one. The
 * count of declared actions is what says a declaration happened; the reload is skipped whenever
 * there is something to lose — a capture in progress or an undoable edit — because taking the
 * table again would throw a person's work away to fix a staleness they cannot see. */
static void keys_ctx_refresh(RolltuiKeysCtx* kc) {
  const RolltuiBindings* live = rolltui_windows_bindings(kc->w);
  const size_t n = rolltui_bindings_action_count(live);
  if (n == kc->declared_seen) return;
  if (rolltui_keys_editor_capturing(kc->ed) || rolltui_keys_editor_undo_depth(kc->ed) != 0 ||
      rolltui_keys_editor_redo_depth(kc->ed) != 0)
    return;
  kc->declared_seen = n;
  rolltui_keys_editor_load(kc->ed, live);
}

/* THE DECLARATIONS ARE THE SCREEN'S AND THE CHORDS ARE THE FILE'S. A preset file carries rows
 * and knows nothing about which screen is running; the live table carries the declarations that
 * make an action editable at all. Loading one into the editor therefore takes the live table's
 * declarations and the file's chords, rather than either alone — a file alone would drop every
 * `app.*` action out of the tree the moment a preset was chosen. */
static void keys_ctx_load_rows(RolltuiKeysCtx* kc, const RolltuiBindings* rows) {
  RolltuiBindings* merged = rolltui_bindings_clone(rolltui_windows_bindings(kc->w));
  size_t i, j;
  for (i = 0; i < rolltui_bindings_row_count(merged); ++i) {
    size_t len = 0;
    const char* name = rolltui_bindings_row_at(merged, i, &len);
    rolltui_bindings_clear(merged, name, len);
  }
  for (i = 0; i < rolltui_bindings_row_count(rows); ++i) {
    size_t len = 0;
    const char* name = rolltui_bindings_row_at(rows, i, &len);
    const size_t chords = rolltui_bindings_chord_count(rows, name, len);
    /* `name` BORROWS into `rows`, which nothing below mutates. */
    for (j = 0; j < chords; ++j) {
      RolltuiChord k;
      memset(&k, 0, sizeof k);
      if (rolltui_bindings_chord_at(rows, name, len, j, &k)) rolltui_bindings_add_chord(merged, name, len, &k);
    }
  }
  rolltui_keys_editor_load(kc->ed, merged);
  rolltui_bindings_free(merged);
}

/* The store's preset names into the Load choice. A no-op with no store: the editor still edits
 * the live table, it just has nowhere to put the result. */
static void keys_ctx_sync_store(RolltuiKeysCtx* kc) {
  RolltuiPresetList list;
  RolltuiStrList names;
  size_t i;
  if (!kc->store) return;
  memset(&list, 0, sizeof list);
  memset(&names, 0, sizeof names);
  rolltui_preset_store_list(kc->store, &list);
  for (i = 0; i < list.n; ++i) rolltui_str_list_add(&names, list.v[i].name.p, list.v[i].name.n);
  rolltui_keys_editor_set_presets(kc->ed, &names);
  rolltui_str_list_release(&names);
  rolltui_preset_list_release(&list);
}

/* The Bindings domain's value IS a `RolltuiBindings*`, so the working copy is REPLACED rather
 * than edited in place: there is no member to reach through. */
static void keys_ctx_write_back(RolltuiKeysCtx* kc) {
  if (!kc->store) return;
  rolltui_preset_store_set_working(kc->store, rolltui_bindings_clone(rolltui_keys_editor_committed(kc->ed)),
                                   kc->persist);
}

static void keys_ctx_apply(RolltuiKeysCtx* kc, const RolltuiKeysEditorOutcome* o) {
  char buf[192];
  rolltui_str_clear(&kc->hint);
  kc->hint_is_problem = 0;
  switch (o->kind) {
    case ROLLTUI_KEYS_EDIT_COMMITTED:
      keys_ctx_write_back(kc);
      break;
    case ROLLTUI_KEYS_EDIT_SAVE_AS: {
      RolltuiStr err;
      int r;
      if (!kc->store) break;
      memset(&err, 0, sizeof err);
      keys_ctx_write_back(kc);
      /* NEVER overwrite: a name already taken comes back as its own answer and the person types
       * another. Replacing someone's preset because they reused a name is a data loss the store
       * deliberately offers to refuse, and a widget has nowhere to ask. */
      r = rolltui_preset_store_save_as(kc->store, o->value.p ? o->value.p : "", o->value.n, /*overwrite=*/0, &err);
      if (r == ROLLTUI_SAVE_SAVED) {
        snprintf(buf, sizeof buf, "saved preset '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
        rolltui_str_set(&kc->hint, buf, strlen(buf));
        keys_ctx_sync_store(kc); /* the new name joins the Load choice */
      } else if (r == ROLLTUI_SAVE_EXISTS_ASK) {
        snprintf(buf, sizeof buf, "'%.*s' already exists \xE2\x80\x94 choose another name", (int)o->value.n,
                 o->value.p ? o->value.p : "");
        rolltui_str_set(&kc->hint, buf, strlen(buf));
        kc->hint_is_problem = 1;
      } else {
        rolltui_str_set(&kc->hint, K("cannot save: "));
        rolltui_str_append_str(&kc->hint, &err);
        kc->hint_is_problem = 1;
      }
      rolltui_str_free(&err);
      break;
    }
    case ROLLTUI_KEYS_EDIT_LOAD_PRESET: {
      RolltuiBindingsPresetReport rep;
      if (!kc->store) break;
      memset(&rep, 0, sizeof rep);
      if (rolltui_preset_store_load(kc->store, o->value.p ? o->value.p : "", o->value.n, &rep, kc->persist)) {
        RolltuiBindings* v = (RolltuiBindings*)rolltui_preset_store_working(kc->store);
        if (v) {
          keys_ctx_load_rows(kc, v);
          rolltui_preset_store_value_free(kc->store, v);
        }
        keys_ctx_sync_store(kc);
        snprintf(buf, sizeof buf, "loaded '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
      } else {
        snprintf(buf, sizeof buf, "cannot load '%.*s'", (int)o->value.n, o->value.p ? o->value.p : "");
        kc->hint_is_problem = 1;
      }
      rolltui_str_set(&kc->hint, buf, strlen(buf));
      rolltui_bindings_preset_report_release(&rep);
      break;
    }
    case ROLLTUI_KEYS_EDIT_RESET_LOADED: {
      /* THE ORIGIN PRESET, re-read — not the working copy, which is the edited value this is
       * meant to throw away. */
      RolltuiBindingsPresetReport rep;
      RolltuiBindings* v;
      size_t olen = 0;
      const char* origin;
      if (!kc->store) break;
      memset(&rep, 0, sizeof rep);
      origin = rolltui_preset_store_origin(kc->store, &olen);
      v = (RolltuiBindings*)rolltui_preset_store_get(kc->store, origin ? origin : "", olen, &rep);
      if (v) {
        keys_ctx_load_rows(kc, v);
        rolltui_preset_store_value_free(kc->store, v);
        keys_ctx_write_back(kc);
        rolltui_str_set(&kc->hint, K("reset to the loaded preset"));
      } else {
        rolltui_str_set(&kc->hint, K("the loaded preset is no longer readable"));
        kc->hint_is_problem = 1;
      }
      rolltui_bindings_preset_report_release(&rep);
      break;
    }
    default:
      break;
  }
}

static int keys_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  RolltuiKeysEditorOutcome o;
  keys_ctx_refresh(kc);
  memset(&o, 0, sizeof o);
  rolltui_keys_editor_handle(kc->ed, e, rolltui_windows_bindings(kc->w), &o);
  keys_ctx_apply(kc, &o);
  {
    const int consumed = o.kind != ROLLTUI_KEYS_EDIT_NONE;
    rolltui_keys_editor_outcome_release(&o);
    return consumed;
  }
}

/* The bottom three rows are which preset this is, what the editor is doing, and whatever the
 * last outcome had to say — exactly as the menu's own scrolling assumes: the menu is laid out
 * into what is left. */
#define ROLLTUI_KEYS_BOX_ROWS 3

static void keys_ctx_menu_rect(const RolltuiResolvedNode* rn, RolltuiRect* out) {
  int box;
  rolltui_content_rect(rn, out);
  box = out->h < ROLLTUI_KEYS_BOX_ROWS ? out->h : ROLLTUI_KEYS_BOX_ROWS;
  out->h -= box;
}

static void keys_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(kc->w);
  RolltuiMenu* m;
  RolltuiMenuOptions o;
  RolltuiRect r;
  keys_ctx_refresh(kc);
  m = rolltui_keys_editor_menu(kc->ed);
  o = *rolltui_menu_options(m);
  keys_ctx_menu_rect(rn, &r);
  o.ambiguous_wide = env->ambiguous_wide;
  o.inset = 0;
  rolltui_menu_set_options_struct(m, &o);
  rolltui_menu_layout(m, r);
}

static int keys_put(RolltuiKeysCtx* kc, RolltuiFrame* f, int x, int y, const char* text, size_t len, RolltuiStyle st,
                    int max_cells) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(kc->w);
  if (max_cells <= 0) return 0;
  return rolltui_frame_put_text(f, kc->draw, x, y, text, len, st, max_cells, env->ambiguous_wide, 0);
}

static void keys_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiStyle* styles = rolltui_windows_styles(kc->w);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(kc->w);
  RolltuiMenu* m;
  RolltuiRect r, mr;
  RolltuiStyle label, value;
  int y;

  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  keys_ctx_layout(ctx, rn);
  m = rolltui_keys_editor_menu(kc->ed);
  keys_ctx_menu_rect(rn, &mr);
  if (mr.h > 0) {
    RolltuiInputRoles iroles;
    iroles.text = br->input_text;
    iroles.selection = br->input_selection;
    iroles.placeholder = br->input_placeholder;
    rolltui_menu_draw(m, f, kc->draw, styles, rolltui_windows_menu_roles(kc->w), &iroles, rn->focused);
  }
  label = styles[br->label];
  value = styles[br->value];
  y = r.y + mr.h;

  if (y < r.y + r.h) {
    rolltui_str_clear(&kc->line);
    rolltui_str_append(&kc->line, K("preset: "));
    if (kc->store) rolltui_preset_store_label(kc->store, &kc->line);
    else rolltui_str_append(&kc->line, K("(this app keeps no bindings presets)"));
    keys_put(kc, f, r.x, y++, kc->line.p, kc->line.n, label, r.w);
  }
  if (y < r.y + r.h) {
    rolltui_str_clear(&kc->line);
    rolltui_keys_editor_status_line(kc->ed, &kc->line);
    keys_put(kc, f, r.x, y++, kc->line.p, kc->line.n,
             rolltui_keys_editor_capturing(kc->ed) ? styles[kc->capture_role] : value, r.w);
  }
  if (y < r.y + r.h) {
    if (kc->hint.n != 0)
      keys_put(kc, f, r.x, y++, kc->hint.p, kc->hint.n, styles[kc->hint_is_problem ? br->error : br->value], r.w);
    else
      keys_put(kc, f, r.x, y++, K("Actions by scope \xE2\x80\xBA a scope \xE2\x80\xBA an action \xE2\x80\xBA add a chord"), value, r.w);
  }
}

static int keys_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  RolltuiMenu* m = rolltui_keys_editor_menu(kc->ed);
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  out->first = (size_t)rolltui_menu_scroll_first(m);
  out->visible = (size_t)rolltui_menu_scroll_visible(m);
  out->total = rolltui_menu_visible(m, NULL);
  return 1;
}

static void keys_ctx_destroy(void* ctx) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  rolltui_keys_editor_free(kc->ed);
  rolltui_draw_scratch_free(kc->draw);
  rolltui_str_free(&kc->line);
  rolltui_str_free(&kc->hint);
  rolltui_mem_free(kc);
}

static const RolltuiWidgetPlugin kKeysPlugin = {
    keys_ctx_destroy, keys_ctx_layout, keys_ctx_draw, NULL, NULL,
    NULL,             keys_ctx_handle, keys_ctx_scroll_extent, NULL,
};

static RolltuiWidget keys_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  RolltuiKeysCtx* kc;
  RolltuiWidget out;
  int warning;
  (void)c;
  (void)content;
  (void)n;
  memset(&out, 0, sizeof out);
  kc = (RolltuiKeysCtx*)rolltui_mem_alloc(sizeof *kc);
  memset(kc, 0, sizeof *kc);
  kc->w = w;
  /* The LIVE table is the baseline: it is what this app is running on, declarations included. */
  kc->ed = rolltui_keys_editor_new(rolltui_windows_bindings(w));
  kc->declared_seen = rolltui_bindings_action_count(rolltui_windows_bindings(w));
  kc->draw = rolltui_draw_scratch_new();
  /* A capture prompt is not an error and not an ordinary value, so it is drawn in the theme's
   * own `warning`. Resolved by NAME once here rather than being a tenth field on
   * `RolltuiBuiltinRoles` that only this kind would read. */
  warning = rolltui_role_from_name(K("warning"));
  kc->capture_role = warning >= 0 ? (unsigned char)warning : rolltui_windows_builtin_roles(w)->value;
  kc->persist = 1;
  out.vt = &kKeysPlugin;
  out.ctx = kc;
  return out;
}

/* The widget for `content` when it is a `keys` one, else NULL. `rolltui_windows_widget_for`
 * creates it if this screen has none, which is what lets a host wire the store before the first
 * draw; the kind check is what stops the call acting on some other kind's ctx. */
static RolltuiKeysCtx* keys_ctx_for(RolltuiWindows* w, const char* content, size_t len) {
  RolltuiWidget* widget;
  if (!w) return NULL;
  if (!content || len == 0) { content = "keys"; len = 4; }
  widget = rolltui_windows_widget_for(w, content, len);
  if (!widget || widget->vt != &kKeysPlugin) return NULL;
  return (RolltuiKeysCtx*)widget->ctx;
}

void rolltui_windows_set_bindings_store(RolltuiWindows* w, const char* content, size_t len, RolltuiPresetStore* store,
                                        int persist) {
  RolltuiKeysCtx* kc = keys_ctx_for(w, content, len);
  if (!kc) return;
  kc->store = store;
  kc->persist = persist;
  keys_ctx_refresh(kc);
  keys_ctx_sync_store(kc);
}


void rolltui_menu_widget_ctx_refresh(void* ctx) { menu_ctx_refresh((RolltuiMenuCtx*)ctx); }

const char* rolltui_menu_widget_ctx_origin(void* ctx, size_t* len) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  menu_ctx_refresh(mc);
  return rolltui_str_get(&mc->origin, len);
}
