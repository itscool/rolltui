/* rolltui/c/rolltui_widget_kinds.c — see rolltui_widget_kinds.h. The library's own `rows`,
 * `text`, `file`, `help` and `input` kinds, plus the error/panel fallbacks, each filling
 * `rolltui/c/rolltui_widgets.h`'s plugin contract in real C11 — calling only the C engines
 * (`rolltui_input.h`, `rolltui_wrap.h`, `rolltui_frame_ops.h`, `rolltui_bindings.h`,
 * `rolltui_marker.h`, `rolltui_unicode.h`), never a C++ header. `transcript` and `menu` stay
 * in `Widgets.cpp` — see this file's own header comment for why. */
#include "rolltui/c/rolltui_widget_kinds.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_marker.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_wrap.h"

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

static int str_eq_lit(const char* a, size_t alen, const char* b) { return b && alen == strlen(b) && memcmp(a, b, alen) == 0; }

/* Mirrors `rolltui::scroll_by_action` exactly, over `rolltui_bindings.h`. */
static int scroll_text_scroll_by_action(const RolltuiScrollTextActions* actions, const RolltuiBindings* bindings,
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

static int scroll_text_base_handle(RolltuiScrollTextBase* b, const RolltuiEvent* e) {
  if (e->kind != ROLLTUI_EVENT_KEY) return 0;
  return scroll_text_scroll_by_action(&b->actions, rolltui_windows_bindings(b->w), &e->key, b->area.h, b->total,
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

/* Mirrors `file_stamp` in Widgets.cpp exactly: mtime to the nanosecond, plus size. */
static long long file_ctx_stamp(const char* path) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  return (long long)st.st_mtimespec.tv_sec * 1000000000LL + (long long)st.st_mtimespec.tv_nsec +
         (long long)st.st_size;
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
  fc->ok = 0;
  {
    FILE* fh = fopen(fc->read_path.p ? fc->read_path.p : "", "rb");
    if (fh) {
      char buf[4096];
      size_t got;
      while ((got = fread(buf, 1, sizeof buf, fh)) > 0) rolltui_str_append(&fc->body, buf, got);
      fclose(fh);
      fc->ok = 1;
    }
  }
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

static void help_chords_text(const RolltuiBindings* b, const char* action, size_t alen, RolltuiStr* out) {
  unsigned char proto = rolltui_key_active_protocol();
  size_t n = rolltui_bindings_chord_count(b, action, alen);
  size_t i;
  rolltui_str_clear(out);
  for (i = 0; i < n; ++i) {
    RolltuiChord c;
    char buf[ROLLTUI_CHORD_STRING_MAX];
    size_t len;
    if (!rolltui_bindings_chord_at(b, action, alen, i, &c)) continue;
    if (!rolltui_key_deliverable(&c, proto)) continue;
    len = rolltui_chord_display(&c, buf, sizeof buf);
    if (out->n > 0) rolltui_str_append(out, ", ", 2);
    rolltui_str_append(out, buf, len);
  }
}

/* Appends "  <chord-or-'(unbound)'><pad>description\n" for every action of `scope`, column
 * aligned to the widest chord (capped at 22, minimum column 12) — `help_lines`'s own rule. */
static void help_append_scope_lines(const RolltuiBindings* b, const char* scope, size_t slen, RolltuiStr* out) {
  size_t count = rolltui_bindings_action_count(b);
  size_t* idx = NULL;
  size_t idx_n = 0, idx_cap = 0;
  size_t width = 0, column, i;
  RolltuiStr chord;
  memset(&chord, 0, sizeof chord);
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
  for (i = 0; i < idx_n; ++i) {
    size_t alen = 0;
    const char* a = rolltui_bindings_action_at(b, idx[i], &alen);
    size_t clen;
    help_chords_text(b, a, alen, &chord);
    clen = chord.n;
    if (clen > 22) clen = 22;
    if (clen > width) width = clen;
  }
  column = width + 2;
  if (column < 12) column = 12;
  for (i = 0; i < idx_n; ++i) {
    size_t alen = 0;
    const char* a = rolltui_bindings_action_at(b, idx[i], &alen);
    size_t dlen = 0;
    const char* d;
    size_t linelen;
    rolltui_str_append(out, "  ", 2); /* help_document's own indent, ahead of every line */
    help_chords_text(b, a, alen, &chord);
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
      help_append_scope_lines(b, s, slen, &h->built);
    }
    {
      size_t note_len = 0;
      const char* note = rolltui_windows_help_note(h->base.w, &note_len);
      rolltui_str_append(&h->built, note, note_len);
    }
  } else {
    rolltui_str_append_str(&h->built, &h->scope);
    rolltui_str_append(&h->built, ":\n", 2);
    help_append_scope_lines(b, h->scope.p ? h->scope.p : "", h->scope.n, &h->built);
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
    rolltui_str_clear(&rc->line);
    for (i = 0; i < rc->rows.n; ++i) {
      if (rc->line.n > 0) rolltui_str_append(&rc->line, "  ", 2);
      rolltui_str_append_str(&rc->line, &rc->rows.v[i].label);
      rolltui_str_append(&rc->line, " ", 1);
      rolltui_str_append_str(&rc->line, &rc->rows.v[i].value);
    }
    rolltui_frame_put_text(f, rc->draw, r.x + 1, r.y, rc->line.p ? rc->line.p : "", rc->line.n,
                           styles[rc->role_value], r.w - 1 > 0 ? r.w - 1 : 0, env->ambiguous_wide, 0);
    return;
  }
  {
    RolltuiWrapOptions wo;
    int y = r.y;
    memset(&wo, 0, sizeof wo);
    wo.ambiguous_wide = env->ambiguous_wide;
    wo.tab_width = 8;
    for (i = 0; i < rc->rows.n; ++i) {
      const RolltuiRow* row = &rc->rows.v[i];
      size_t n, j;
      if (y >= r.y + r.h) break;
      rolltui_frame_put_text(f, rc->draw, r.x + 1, y, row->label.p ? row->label.p : "", row->label.n,
                             styles[rc->role_label], r.w - 1 > 0 ? r.w - 1 : 0, env->ambiguous_wide, 0);
      rolltui_wrap(rc->wrap, row->value.p ? row->value.p : "", row->value.n, r.w - 9 > 0 ? r.w - 9 : 1, wo);
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
        rolltui_frame_put_text(f, rc->draw, r.x + 9, y, ltext, ltext_len, styles[rc->role_value],
                               r.w - 9 > 0 ? r.w - 9 : 0, env->ambiguous_wide, 0);
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

static RolltuiWidget rows_widget_factory(void* c, const char* content, size_t n) {
  RolltuiWindows* w = (RolltuiWindows*)c;
  unsigned char ordinal = 0;
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
  if (!rolltui_content_parse(content, n, &ordinal, &is_host, &name, &name_len, &source, &source_len, &problem,
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

static RolltuiWidget text_widget_factory(void* c, const char* content, size_t n) {
  RolltuiWindows* w = (RolltuiWindows*)c;
  unsigned char ordinal = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiTextCtx* tc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(content, n, &ordinal, &is_host, &name, &name_len, &source, &source_len, &problem,
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

static RolltuiWidget file_widget_factory(void* c, const char* content, size_t n) {
  RolltuiWindows* w = (RolltuiWindows*)c;
  unsigned char ordinal = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiFileCtx* fc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(content, n, &ordinal, &is_host, &name, &name_len, &source, &source_len, &problem,
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

static RolltuiWidget help_widget_factory(void* c, const char* content, size_t n) {
  RolltuiWindows* w = (RolltuiWindows*)c;
  unsigned char ordinal = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiHelpCtx* hc;
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(content, n, &ordinal, &is_host, &name, &name_len, &source, &source_len, &problem,
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
static RolltuiWidget error_widget_factory(void* c, const char* content, size_t n) {
  RolltuiWindows* w = (RolltuiWindows*)c;
  unsigned char ordinal = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why, msg;
  RolltuiWidget out;
  memset(&why, 0, sizeof why);
  memset(&msg, 0, sizeof msg);
  if (rolltui_content_parse(content, n, &ordinal, &is_host, &name, &name_len, &source, &source_len, &problem,
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
static RolltuiWidget panel_widget_factory(void* c, const char* why, size_t n) {
  RolltuiWindows* w = (RolltuiWindows*)c;
  return make_error_widget(w, rolltui_windows_builtin_roles(w)->error, why, n);
}

void rolltui_widget_kinds_register(RolltuiWindows* w) {
  rolltui_windows_register_kind(w, "rows", 4, rows_widget_factory, w, NULL);
  rolltui_windows_register_kind(w, "text", 4, text_widget_factory, w, NULL);
  rolltui_windows_register_kind(w, "file", 4, file_widget_factory, w, NULL);
  rolltui_windows_register_kind(w, "help", 4, help_widget_factory, w, NULL);
  rolltui_windows_set_error_factory(w, error_widget_factory, w);
  rolltui_windows_set_panel_factory(w, panel_widget_factory, w);
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
  const RolltuiInputActions* actions; /* BORROWED, process lifetime (rolltui::input_actions()) */
  RolltuiUnicodeScratch* uscratch;
  RolltuiDrawScratch* draw;
  RolltuiNote note; /* reused scratch for note_info() */
} RolltuiInputCtx;

static int input_kind_max_rows(int parent_extent, int border_rows) {
  int v = parent_extent / 2 - border_rows;
  return v > 1 ? v : 1;
}
static int input_kind_rows(int text_rows, int end_col, int note_width, int width, int max_rows) {
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
  return input_kind_rows(text_rows, input_ctx_end_col(ic), nw, width, ic->max_rows);
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
  ic->max_rows = input_kind_max_rows(parent_extent, border);
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
                                     const RolltuiInputActions* actions, const RolltuiEvent* e) {
  const RolltuiBindings* b = rolltui_windows_bindings(w);
  const RolltuiWidgetEnv* env = rolltui_windows_env(w);
  int action = (int)rolltui_input_handle(ed, e, b, actions, env->now_ms);
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
  return rolltui_input_kind_process_event(ic->ed, ic->w, ic->source.p ? ic->source.p : "", ic->source.n, ic->actions,
                                         e) != ROLLTUI_INPUT_IGNORED;
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

void* rolltui_input_widget_ctx_new(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                    const RolltuiBuiltinRoles* roles, const RolltuiInputActions* actions) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)rolltui_mem_alloc(sizeof *ic);
  memset(ic, 0, sizeof *ic);
  ic->ed = ed;
  ic->w = w;
  rolltui_str_set(&ic->source, source, source_len);
  ic->roles = *roles;
  ic->actions = actions;
  ic->uscratch = rolltui_u_scratch_new();
  ic->draw = rolltui_draw_scratch_new();
  ic->max_rows = 1;
  return ic;
}
void rolltui_input_widget_ctx_set_min_outer(void* ctx, int rows) { ((RolltuiInputCtx*)ctx)->min_outer = rows; }
