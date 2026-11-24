/* rolltui/c/rolltui_widget_kinds.c — see rolltui_widget_kinds.h. The library's own `rows`,
 * `text`, `file`, `help`, `input`, `transcript`, `menu`, `theme` and `keys` kinds, plus the error/panel
 * fallbacks, each filling `rolltui/c/rolltui_widgets.h`'s plugin contract in real C11 —
 * calling only the C engines (`rolltui_widget_input.h`, `rolltui_widget_transcript.h`, `rolltui_widget_menu.h`,
 * `rolltui_wrap.h`, `rolltui_frame_ops.h`, `rolltui_bindings.h`, `rolltui_marker.h`,
 * `rolltui_unicode.h`, `rolltui_embedded.h`), never a C++ header. */
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_feedback.h"
#include "rolltui/c/rolltui_widget_list.h"
#include "rolltui/c/rolltui_widget_dialog.h"
#include "rolltui/c/rolltui_chart.h"
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_widget_picker.h"

#include <stdio.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_keys_editor.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_unicode.h"
#include "rolltui/c/rolltui_widget_input.h"
#include "rolltui/c/rolltui_widget_menu.h"
#include "rolltui/c/rolltui_widget_menu_tree.h"
#include "rolltui/c/rolltui_presets.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_theme_editor.h"
#include "rolltui/c/rolltui_theme_gen.h"
#include "rolltui/c/rolltui_widget_transcript.h"
#include "rolltui/c/rolltui_widgets.h"

/* ---- the key scope each kind reads, and whether a digit is a count ------------------------------------------------------------- */
static int keys_transcript(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "transcript", *len = 10;
  return 1; /* nothing is typed into a page: `5` then Down is five lines */
}
static int keys_picker(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "picker", *len = 6;
  return 0; /* a digit jumps to a name that starts with it */
}
static int keys_input(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "input", *len = 5;
  return 0; /* a digit is text */
}
static int keys_menu(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "menu", *len = 4;
  return 0; /* a digit filters the level */
}

/* ---- WHAT A FOCUSED WIDGET TAKES before the program's own keys (`takes`) ---------------------------------------------------- */

/* A character that is typed: no Ctrl or Alt with it. */
static int takes_typed(const RolltuiChord* k) { return k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && !k->super; }
/* The action `k` is bound to in `scope` of the table the windows route with, or NULL. */
static const char* takes_action(const RolltuiBindings* b, const RolltuiChord* k, const char* scope, size_t* n) {
  *n = 0;
  return b ? rolltui_bindings_action_for(b, k, scope, strlen(scope), n) : NULL;
}
static int takes_is(const char* a, size_t n, const char* want) { return a && n == strlen(want) && memcmp(a, want, n) == 0; }
static int takes_bound(const RolltuiBindings* b, const RolltuiChord* k, const char* scope);
/* A PAGE'S KEYS: its scrolling (the `transcript` scope) and its wrap and sideways scrolling (the `view` scope) */
static int takes_viewer(const RolltuiWindows* w, const RolltuiChord* k) {
  const RolltuiBindings* b = rolltui_windows_bindings(w);
  return takes_bound(b, k, "transcript") || takes_bound(b, k, "view");
}
static int takes_bound(const RolltuiBindings* b, const RolltuiChord* k, const char* scope) {
  size_t n = 0;
  return takes_action(b, k, scope, &n) != NULL;
}
/* A list's own keys: its going back one level only while it has a filter or marks to go back from. */
static int takes_list(const RolltuiBindings* b, RolltuiList* l, const RolltuiChord* k) {
  size_t n = 0;
  const char* a = takes_action(b, k, "list", &n);
  if (!a || !l) return 0;
  if (rolltui_list_typing(l) && (takes_typed(k) || k->key == ROLLTUI_KEY_BACKSPACE)) return 1;
  return rolltui_list_can(l, rolltui_list_default_actions(), a, n);
}


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
  /* `view.wrap` off: each line whole, `left` cells scrolled off the left, as far as the widest line's end */
  unsigned char nowrap;
  int left, widest;
} RolltuiScrollTextBase;

/* Lines kept whole are wrapped at a width no line reaches. */
#define SCROLL_TEXT_WHOLE (1 << 20)
#define SCROLL_TEXT_STEP 4 /* the cells `view.left`, `view.right` and the wheel's sideways turns move */

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
  rolltui_wrap(b->wrap, text, text_len, b->nowrap ? SCROLL_TEXT_WHOLE : b->area.w > 0 ? b->area.w : 1, wo);
  b->total = (int)rolltui_wrap_line_count(b->wrap);
  max_top = b->total - (b->area.h > 0 ? b->area.h : 1);
  if (max_top < 0) max_top = 0;
  if (b->top < 0) b->top = 0;
  if (b->top > max_top) b->top = max_top;
  b->widest = 0;
  if (b->nowrap) {
    size_t i;
    for (i = 0; i < (size_t)b->total; ++i) {
      const char* lt;
      size_t ll, gn;
      const RolltuiWrapGrapheme* g;
      int w, ind, hard;
      rolltui_wrap_line(b->wrap, i, &lt, &ll, &g, &gn, &w, &ind, &hard);
      if (w + ind > b->widest) b->widest = w + ind;
    }
  }
  {
    const int most = b->widest > b->area.w ? b->widest - b->area.w : 0;
    if (b->left > most) b->left = most;
    if (b->left < 0 || !b->nowrap) b->left = 0;
  }
}

/* A LINE KEPT WHOLE, scrolled sideways: the part from `left` cells in, and the blank a wide glyph cut at the edge leaves */
static const char* scroll_text_shift(const RolltuiScrollTextBase* b, const char* text, size_t* len, const RolltuiWrapGrapheme* g, size_t gn,
                                     int* pad) {
  size_t gi = 0;
  int skipped = 0;
  *pad = 0;
  if (!b->nowrap || b->left <= 0) return text;
  while (gi < gn && skipped + g[gi].width <= b->left) skipped += g[gi].width, ++gi;
  if (gi < gn && skipped < b->left) *pad = skipped + g[gi].width - b->left, ++gi;
  if (gi >= gn) {
    *len = 0;
    return text;
  }
  *len -= g[gi].offset;
  return text + g[gi].offset;
}

/* A KIND THAT PAINTS ITS OWN LINES — the help window, whose chords and headings are not the description's style. It
 * is handed each visible wrapped line with the source line it belongs to (a `\n`-separated line of the text
 * given), whether this wrapped line BEGINS that source line, and `plain`: the text role's style already standing
 * on the ground the line is drawn over. */
typedef void (*ScrollLinePaint)(void* ctx, RolltuiFrame* f, RolltuiDrawScratch* draw, int x, int y, const char* text,
                                size_t len, int cells, size_t src_line, int first, RolltuiStyle plain,
                                RolltuiStyleColorRaw ground, int ambiguous_wide);

/* TEXT IS DRAWN ON THE WINDOW'S OWN GROUND. A window's fill is whatever the layout gave it — the screen's
 * background, or a popup's panel — and the text role's background is the SCREEN's; drawing every line with it
 * cut a darker slab out of a popup, a box of text on a box. Only the text role's foreground and attributes are
 * taken, the way a menu row takes its value's; the ground is what is already in the frame under the line. */
static RolltuiStyleColorRaw scroll_text_ground(const RolltuiFrame* f, int x, int y) {
  RolltuiCellRaw under;
  RolltuiStyleColorRaw none;
  memset(&none, 0, sizeof none);
  if (x < 0 || y < 0 || x >= rolltui_frame_width(f) || y >= rolltui_frame_height(f)) return none;
  memset(&under, 0, sizeof under);
  rolltui_frame_cell(f, x, y, &under);
  return under.style.bg;
}

/* Mirrors `rolltui::draw_scrolled_text` exactly, over the C engines. Returns the total
 * wrapped line count, as that function does. */
static int scroll_text_base_draw_with(RolltuiScrollTextBase* b, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                                      const char* text, size_t text_len, ScrollLinePaint paint, void* paint_ctx) {
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
  rolltui_wrap(b->wrap, text, text_len, b->nowrap ? SCROLL_TEXT_WHOLE : r.w > 0 ? r.w : 1, wo);
  n = rolltui_wrap_line_count(b->wrap);
  total = (int)n;
  if (r.w <= 0 || r.h <= 0) return total;

  y = r.y;
  i = (size_t)(b->top > 0 ? b->top : 0);
  {
    size_t src = 0;
    int first = 1;
    if (paint) {
      /* the lines scrolled off above still count: which source line a visible one belongs to is a count from the top */
      size_t k;
      for (k = 0; k < i && k < n; ++k) {
        const char* lt;
        size_t ll;
        const RolltuiWrapGrapheme* gg;
        size_t gnn;
        int ww, ii, hh;
        rolltui_wrap_line(b->wrap, k, &lt, &ll, &gg, &gnn, &ww, &ii, &hh);
        first = hh;
        if (hh) ++src;
      }
    }
    for (; i < n && y < r.y + r.h; ++i) {
      const char* ltext;
      size_t ltext_len;
      const RolltuiWrapGrapheme* g;
      size_t gn;
      int width, indent, hard;
      RolltuiStyle plain = styles[b->roles.text];
      const RolltuiStyleColorRaw ground = scroll_text_ground(f, r.x, y);
      int pad = 0;
      rolltui_wrap_line(b->wrap, i, &ltext, &ltext_len, &g, &gn, &width, &indent, &hard);
      ltext = scroll_text_shift(b, ltext, &ltext_len, g, gn, &pad);
      indent += pad;
      plain.bg = ground;
      if (paint)
        paint(paint_ctx, f, b->draw, r.x + indent, y, ltext, ltext_len, r.w - indent > 0 ? r.w - indent : 0, src, first,
              plain, ground, env->ambiguous_wide);
      else
        rolltui_frame_put_text(f, b->draw, r.x + indent, y, ltext, ltext_len, plain,
                               r.w - indent > 0 ? r.w - indent : 0, env->ambiguous_wide, 0);
      ++y;
      first = hard;
      if (hard) ++src;
    }
  }
  below = total - (b->top > 0 ? b->top : 0) - r.h;
  mlen = rolltui_scroll_marker_text(below > 0 ? (size_t)below : 0, r.w, env->ambiguous_wide, marker, sizeof marker);
  if (mlen > 0) {
    int mx, lit;
    mw = rolltui_u_display_width(b->uscratch, marker, mlen, env->ambiguous_wide);
    mx = r.x + (r.w - mw > 0 ? r.w - mw : 0);
    /* The marker is a button (`scroll_text_marker_hit`), so the pointer on it lights it. */
    lit = rn->hover && rn->hover_y == r.y + r.h - 1 && rn->hover_x >= mx && rn->hover_x < r.x + r.w;
    rolltui_frame_put_text(f, b->draw, mx, r.y + r.h - 1, marker, mlen, styles[lit ? b->roles.hover : b->roles.scroll_marker], mw,
                           env->ambiguous_wide, 0);
  }
  return total;
}

static int scroll_text_base_draw(RolltuiScrollTextBase* b, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                                 const char* text, size_t text_len) {
  return scroll_text_base_draw_with(b, rn, f, text, text_len, NULL, NULL);
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

/* `view.wrap` turns wrapping off and on; `view.left` and `view.right` scroll lines kept whole sideways. */
static int scroll_text_view(RolltuiScrollTextBase* b, const RolltuiChord* k) {
  size_t n = 0;
  const char* a = rolltui_bindings_action_for(rolltui_windows_bindings(b->w), k, "view", 4, &n);
  const int most = b->widest > b->area.w ? b->widest - b->area.w : 0;
  if (!a) return 0;
  if (str_eq_lit(a, n, "view.wrap")) {
    b->nowrap = (unsigned char)!b->nowrap;
    b->left = 0;
    return 1;
  }
  if (str_eq_lit(a, n, "view.left") || str_eq_lit(a, n, "view.right")) {
    b->left += str_eq_lit(a, n, "view.left") ? -SCROLL_TEXT_STEP : SCROLL_TEXT_STEP;
    if (b->widest > 0 && b->left > most) b->left = most; /* not measured yet (wrapping was just turned off): the next layout clamps */
    if (b->left < 0 || !b->nowrap) b->left = 0;
    return 1;
  }
  return 0;
}

static int scroll_text_base_handle(RolltuiScrollTextBase* b, const RolltuiEvent* e) {
  if (e->kind == ROLLTUI_EVENT_MOUSE && e->mouse.kind == 0 /* press */ &&
      scroll_text_marker_hit(b, e->mouse.x, e->mouse.y)) {
    const int max_top = b->total - (b->area.h > 0 ? b->area.h : 1);
    b->top = max_top > 0 ? max_top : 0;
    return 1;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE && b->nowrap && (e->mouse.kind == 6 || e->mouse.kind == 7)) { /* the wheel's sideways turns */
    const int most = b->widest > b->area.w ? b->widest - b->area.w : 0;
    b->left += e->mouse.kind == 6 ? -SCROLL_TEXT_STEP : SCROLL_TEXT_STEP;
    if (b->left < 0) b->left = 0;
    if (b->left > most) b->left = most;
    return 1;
  }
  if (e->kind != ROLLTUI_EVENT_KEY) return 0;
  if (scroll_text_view(b, &e->key)) return 1;
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

static int text_ctx_takes(void* ctx, const RolltuiChord* k) { return takes_viewer(((RolltuiTextCtx*)ctx)->base.w, k); }

static const RolltuiWidgetPlugin kTextPlugin = {
    text_ctx_destroy, text_ctx_layout, text_ctx_draw, NULL, NULL, NULL, text_ctx_handle, text_ctx_scroll_extent,
    text_ctx_scroll_to, NULL /* title: the layout's */, NULL /* back: no levels */,
    keys_transcript,
    NULL,
    NULL,
    text_ctx_takes /* its scrolling keys and the `view` scope's */
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
  RolltuiOsStat st;
  if (rolltui_os_stat(path, 1, &st) != 0) return -1;
  return st.mtime_ns + st.size;
}

/* Appends the whole file at `path` to `out` (which the caller has already cleared); 1 when it
 * could be opened. Shared by `file_ctx_refresh` below and the `menu` kind's own re-read. */
static int read_whole_file(const char* path, RolltuiStr* out) {
  FILE* fh = rolltui_os_fopen(path, "rb");
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

static int file_ctx_takes(void* ctx, const RolltuiChord* k) { return takes_viewer(((RolltuiFileCtx*)ctx)->base.w, k); }

static const RolltuiWidgetPlugin kFilePlugin = {
    file_ctx_destroy, file_ctx_layout, file_ctx_draw, file_ctx_problem, NULL, NULL, file_ctx_handle,
    file_ctx_scroll_extent, file_ctx_scroll_to, NULL /* title: the layout's */, NULL /* back: no levels */,
    keys_transcript,
    NULL,
    NULL,
    file_ctx_takes /* its scrolling keys and the `view` scope's */
};

/* ============================================================================================
 * help — the key list, rendered from the LIVE bindings. Mirrors `help_lines`/`help_document`
 * (Bindings.cpp/Widgets.cpp) exactly, over `rolltui_bindings.h` — a separate C implementation
 * of the same two small algorithms, the same duplication `Input.cpp`'s `kActions` already is,
 * because neither function crosses the C++/C boundary today.
 * ============================================================================================ */

/* WHAT EACH LINE OF THE BUILT TEXT IS, so it can be drawn as what it is: a scope's heading, or an entry whose chord is
 * the highlighted part. Indexed by source line (a `\n`-separated line of `built`); a line with no entry is plain. */
enum { HELP_PLAIN = 0, HELP_HEADING = 1, HELP_ENTRY = 2, HELP_UNBOUND = 3 };
typedef struct RolltuiHelpLine {
  unsigned char kind;
  size_t chord_from, chord_to; /* bytes within the line: the chord's run, after the indent */
} RolltuiHelpLine;

typedef struct RolltuiHelpCtx {
  RolltuiScrollTextBase base;
  RolltuiStr scope; /* content.source; "" = every configured scope */
  RolltuiStr built; /* text(), rebuilt each call and reused */
  RolltuiHelpLine* meta; /* OWNED: one per source line up to the last that is not plain */
  size_t meta_n, meta_cap;
} RolltuiHelpCtx;

static size_t count_lines_of(const RolltuiStr* s) {
  size_t i, n = 0;
  for (i = 0; i < s->n; ++i)
    if (s->p[i] == '\n') ++n;
  return n;
}

static void help_meta_set(RolltuiHelpCtx* h, size_t line, int kind, size_t from, size_t to) {
  if (line >= h->meta_n) {
    h->meta = (RolltuiHelpLine*)rolltui_grow(h->meta, &h->meta_cap, line + 1, sizeof *h->meta);
    memset(h->meta + h->meta_n, 0, (line + 1 - h->meta_n) * sizeof *h->meta);
    h->meta_n = line + 1;
  }
  h->meta[line].kind = (unsigned char)kind;
  h->meta[line].chord_from = from;
  h->meta[line].chord_to = to;
}

/* Appends "  <chord-or-'(unbound)'><pad>description\n" for every action of `scope`, column
 * aligned to the widest chord (capped at 22, minimum column 12) — `help_lines`'s own rule. */
static void help_scope_lines_impl(const RolltuiBindings* b, const char* scope, size_t slen, const char* const* actions,
                                  const size_t* action_lens, size_t actions_n, const char* indent, size_t indent_len,
                                  RolltuiStr* out, RolltuiHelpCtx* meta) {
  const size_t first_line = meta ? count_lines_of(out) : 0;
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
    if (meta)
      help_meta_set(meta, first_line + i, chord.n == 0 ? HELP_UNBOUND : HELP_ENTRY, indent ? indent_len : 0,
                    (indent ? indent_len : 0) + linelen);
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

void rolltui_help_scope_lines(const RolltuiBindings* b, const char* scope, size_t slen, const char* const* actions,
                              const size_t* action_lens, size_t actions_n, const char* indent, size_t indent_len,
                              RolltuiStr* out) {
  help_scope_lines_impl(b, scope, slen, actions, action_lens, actions_n, indent, indent_len, out, NULL);
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
      int off = 0; /* a colour's square and its space, before the value's first line and every line under it */
      rolltui_frame_put_text(f, rc->draw, r.x + 1, y, row->label.p ? row->label.p : "", row->label.n,
                             styles[rc->role_label], labw, env->ambiguous_wide, 0);
      if (row->has_swatch && r.x + r.w - valx >= 4) {
        off = rolltui_frame_put_swatch(f, rc->draw, valx, y, row->swatch, styles[rc->role_value],
                                       r.x + r.w - valx, env->ambiguous_wide);
        if (off) ++off;
      }
      rolltui_wrap(rc->wrap, row->value.p ? row->value.p : "", row->value.n,
                   r.x + r.w - valx - off > 0 ? r.x + r.w - valx - off : 1, wo);
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
        rolltui_frame_put_text(f, rc->draw, valx + off, y, ltext, ltext_len, styles[rc->role_value],
                               r.x + r.w - valx - off > 0 ? r.x + r.w - valx - off : 0, env->ambiguous_wide, 0);
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
    rows_ctx_destroy, rows_ctx_layout, rows_ctx_draw, rows_ctx_problem, NULL, NULL, NULL, NULL, NULL, NULL /* title: the layout's */, NULL /* back: no levels */,
    NULL,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
};


/* ============================================================================================
 * gauge:<source> — how far something has got, from a gauge the host binds.
 * ============================================================================================ */

typedef struct RolltuiGaugeCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  RolltuiGauge g;
  RolltuiDrawScratch* draw;
} RolltuiGaugeCtx;

static int gauge_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiGaugeCtx* gc = (RolltuiGaugeCtx*)ctx;
  if (rolltui_windows_has_gauge(gc->w, gc->source.p ? gc->source.p : "", gc->source.n)) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &gc->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}
static void gauge_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) { (void)ctx; (void)rn; }
static void gauge_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiGaugeCtx* gc = (RolltuiGaugeCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(gc->w);
  RolltuiRect r;
  rolltui_str_clear(&gc->g.label);
  gc->g.fraction = 0;
  gc->g.failed = 0;
  gc->g.since_ms = 0;
  if (!rolltui_windows_call_gauge(gc->w, gc->source.p ? gc->source.p : "", gc->source.n, &gc->g)) return;
  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  rolltui_gauge_draw_roles(f, gc->draw, r.x, r.y + (r.h - 1) / 2, r.w, &gc->g, rolltui_windows_styles(gc->w),
                           rolltui_windows_builtin_roles(gc->w), env->ambiguous_wide);
}
static void gauge_ctx_destroy(void* ctx) {
  RolltuiGaugeCtx* gc = (RolltuiGaugeCtx*)ctx;
  rolltui_str_free(&gc->source);
  rolltui_gauge_release(&gc->g);
  rolltui_draw_scratch_free(gc->draw);
  rolltui_mem_free(gc);
}

static const RolltuiWidgetPlugin kGaugePlugin = {
    gauge_ctx_destroy, gauge_ctx_layout, gauge_ctx_draw, gauge_ctx_problem, NULL, NULL, NULL, NULL, NULL, NULL /* title: the layout's */, NULL /* back: no levels */,
    NULL,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
};

/* ============================================================================================
 * messages — what the app posted (`rolltui_windows_post`), newest at the bottom. One row tall, the newest while its toast would show.
 * ============================================================================================ */

typedef struct RolltuiMessagesCtx {
  RolltuiScrollTextBase base;
  RolltuiStr built;      /* one line per message: its glyph, a space, its text with newlines as spaces */
  unsigned char* levels; /* per line of `built` */
  size_t levels_cap;
  size_t seen; /* how many messages `built` holds: a new one brings the view down to it */
} RolltuiMessagesCtx;

static void messages_ctx_build(RolltuiMessagesCtx* m) {
  const RolltuiMessageLog* log = rolltui_windows_messages(m->base.w);
  const size_t n = rolltui_message_log_count(log);
  size_t i, k;
  rolltui_str_clear(&m->built);
  m->levels = (unsigned char*)rolltui_grow(m->levels, &m->levels_cap, n ? n : 1, 1);
  for (i = 0; i < n; ++i) {
    const RolltuiMessage* msg = rolltui_message_log_at(log, i);
    size_t glen;
    const char* g = rolltui_message_glyph(msg->level, &glen);
    m->levels[i] = (unsigned char)msg->level;
    if (i) rolltui_str_append(&m->built, "\n", 1);
    rolltui_str_append(&m->built, g, glen);
    rolltui_str_append(&m->built, " ", 1);
    for (k = 0; k < msg->text.n; ++k) {
      const char c = msg->text.p[k];
      rolltui_str_append(&m->built, c == '\n' || c == '\r' ? " " : &msg->text.p[k], 1);
    }
  }
}

static void messages_paint(void* ctx, RolltuiFrame* f, RolltuiDrawScratch* draw, int x, int y, const char* t, size_t n,
                           int cells, size_t src, int first, RolltuiStyle plain, RolltuiStyleColorRaw ground, int aw) {
  RolltuiMessagesCtx* m = (RolltuiMessagesCtx*)ctx;
  const RolltuiStyle* styles = rolltui_windows_styles(m->base.w);
  size_t glen = 0;
  if (first && src < rolltui_message_log_count(rolltui_windows_messages(m->base.w))) {
    RolltuiStyle mark = styles[rolltui_message_role(&m->base.roles, m->levels[src])];
    int used;
    rolltui_message_glyph(m->levels[src], &glen);
    if (glen > n) glen = n;
    rolltui_style_on(&mark, ground);
    used = rolltui_frame_put_text(f, draw, x, y, t, glen, mark, cells, aw, 0);
    rolltui_frame_put_text(f, draw, x + used, y, t + glen, n - glen, plain, cells - used > 0 ? cells - used : 0, aw, 0);
    return;
  }
  rolltui_frame_put_text(f, draw, x, y, t, n, plain, cells, aw, 0);
}

static void messages_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiMessagesCtx* m = (RolltuiMessagesCtx*)ctx;
  const size_t n = rolltui_message_log_count(rolltui_windows_messages(m->base.w));
  const int was_at_end = m->base.top >= m->base.total - (m->base.area.h > 0 ? m->base.area.h : 1);
  messages_ctx_build(m);
  scroll_text_base_layout(&m->base, rn, m->built.p ? m->built.p : "", m->built.n);
  if (n != m->seen || was_at_end) { /* a new message, or a view already at the newest: the newest stays in view */
    const int max_top = m->base.total - (m->base.area.h > 0 ? m->base.area.h : 1);
    m->base.top = max_top > 0 ? max_top : 0;
  }
  m->seen = n;
}

static void messages_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiMessagesCtx* m = (RolltuiMessagesCtx*)ctx;
  const RolltuiMessageLog* log = rolltui_windows_messages(m->base.w);
  const RolltuiWidgetEnv* env = rolltui_windows_env(m->base.w);
  const RolltuiStyle* styles = rolltui_windows_styles(m->base.w);
  const size_t n = rolltui_message_log_count(log);
  RolltuiRect r;
  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  if (r.h == 1) { /* a status line's message: the newest, while its toast would show */
    const RolltuiMessage* last = n ? rolltui_message_log_at(log, n - 1) : NULL;
    if (last && env->now_ms < last->at_ms + rolltui_toast_ms(last->level)) {
      RolltuiStyle plain = styles[m->base.roles.text];
      plain.bg = scroll_text_ground(f, r.x, r.y);
      messages_ctx_build(m);
      {
        const char* t = m->built.p ? m->built.p : "";
        size_t start = m->built.n, len;
        while (start > 0 && t[start - 1] != '\n') --start;
        len = m->built.n - start;
        messages_paint(m, f, m->base.draw, r.x, r.y, t + start, len, r.w, n - 1, 1, plain, plain.bg, env->ambiguous_wide);
      }
    }
    return;
  }
  messages_ctx_layout(ctx, rn);
  if (n == 0) { /* AN EMPTY HISTORY SAYS SO, centred, rather than a blank that reads as broken */
    static const char kEmpty[] = "no messages yet";
    RolltuiStyle muted = styles[m->base.roles.text_muted];
    const int w = (int)sizeof kEmpty - 1;
    muted.bg = scroll_text_ground(f, r.x, r.y + r.h / 2);
    rolltui_frame_put_text(f, m->base.draw, r.x + (r.w > w ? (r.w - w) / 2 : 0), r.y + r.h / 2, kEmpty, sizeof kEmpty - 1, muted,
                           r.w, env->ambiguous_wide, 0);
    return;
  }
  m->base.total = scroll_text_base_draw_with(&m->base, rn, f, m->built.p ? m->built.p : "", m->built.n, messages_paint, m);
}
static int messages_ctx_handle(void* ctx, const RolltuiEvent* e) {
  return scroll_text_base_handle(&((RolltuiMessagesCtx*)ctx)->base, e);
}
static int messages_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  return scroll_text_base_scroll_extent(&((RolltuiMessagesCtx*)ctx)->base, axis, out);
}
static int messages_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  return scroll_text_base_scroll_to(&((RolltuiMessagesCtx*)ctx)->base, axis, first);
}
static void messages_ctx_destroy(void* ctx) {
  RolltuiMessagesCtx* m = (RolltuiMessagesCtx*)ctx;
  scroll_text_base_release(&m->base);
  rolltui_str_free(&m->built);
  rolltui_mem_free(m->levels);
  rolltui_mem_free(m);
}

static const RolltuiWidgetPlugin kMessagesPlugin = {
    messages_ctx_destroy, messages_ctx_layout, messages_ctx_draw, NULL, NULL, NULL, messages_ctx_handle,
    messages_ctx_scroll_extent, messages_ctx_scroll_to, NULL /* title: the layout's */, NULL /* back: no levels */,
    keys_transcript,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
};


/* ============================================================================================
 * output:<source> — another program's output, following its end.
 * ============================================================================================ */

typedef struct RolltuiOutputCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  RolltuiRect area;      /* the content rect */
  RolltuiRect text_area; /* the content rect past the gutter */
  int gutter;            /* the cells before the text: each line's time, its source's label */
  size_t top;   /* the first row (wrapped) or line (not) of the view */
  size_t total; /* rows or lines the view has */
  int left;     /* columns scrolled off the left, not wrapped */
  int width;    /* the widest line, not wrapped */
  int follow;   /* the view is at the end, and stays there as the text grows */
  int wrap;
  size_t below; /* lines past the view's last, for the footer */
  RolltuiScrollTextActions actions;
  /* THE FILTERED VIEW: the numbers (`RolltuiAnsiLineInfo.seq`) of the finished lines the filter matched, in order (OWNED); the number
   * of the first line not yet looked at; the filter's version they were matched with. The line being written is matched afresh. */
  unsigned long long* hits;
  size_t hit_n, hit_cap;
  unsigned long long scanned, filter_version;
  int tail_hit;
  RolltuiDrawScratch* ds; /* OWNED, made at the first gutter drawn */
} RolltuiOutputCtx;

static const RolltuiAnsiText* output_text(RolltuiOutputCtx* o) {
  return rolltui_windows_output(o->w, o->source.p ? o->source.p : "", o->source.n, &o->wrap);
}
static const RolltuiOutputOptions* output_opt(RolltuiOutputCtx* o, const RolltuiFilter** f, unsigned long long* version) {
  return rolltui_windows_output_options(o->w, o->source.p ? o->source.p : "", o->source.n, f, version);
}

static int output_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  if (output_text(o)) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &o->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}

static size_t output_max_top(const RolltuiOutputCtx* o) {
  const size_t h = (size_t)(o->area.h > 0 ? o->area.h : 1);
  return o->total > h ? o->total - h : 0;
}

/* THE FILTER'S HITS brought up to date: lines the ring let go of dropped, the finished lines since the last look matched, the line
 * being written matched afresh. Rules are always hits. */
static void output_scan(RolltuiOutputCtx* o, const RolltuiAnsiText* t, const RolltuiFilter* f, unsigned long long version) {
  const size_t n = rolltui_ansi_line_count(t);
  RolltuiAnsiLineInfo first;
  size_t i, drop = 0;
  memset(&first, 0, sizeof first);
  rolltui_ansi_line_info(t, 0, &first);
  if (version != o->filter_version) { /* another filter: matched again from the start */
    o->hit_n = 0;
    o->scanned = 0;
    o->filter_version = version;
  }
  while (drop < o->hit_n && o->hits[drop] < first.seq) ++drop;
  if (drop) {
    memmove(o->hits, o->hits + drop, (o->hit_n - drop) * sizeof *o->hits);
    o->hit_n -= drop;
  }
  if (o->scanned < first.seq) o->scanned = first.seq;
  for (i = (size_t)(o->scanned - first.seq); i + 1 < n; ++i) { /* the finished lines: every one but the last */
    RolltuiAnsiLineInfo info;
    size_t len = 0;
    const char* line = rolltui_ansi_line(t, i, &len);
    int score = 0;
    memset(&info, 0, sizeof info);
    rolltui_ansi_line_info(t, i, &info);
    if (info.rule || rolltui_filter_match(f, line, len, &score)) {
      o->hits = (unsigned long long*)rolltui_grow(o->hits, &o->hit_cap, o->hit_n + 1, sizeof *o->hits);
      o->hits[o->hit_n++] = info.seq;
    }
  }
  o->scanned = first.seq + (n ? n - 1 : 0);
  o->tail_hit = 0;
  if (n) {
    RolltuiAnsiLineInfo info;
    size_t len = 0;
    const char* line = rolltui_ansi_line(t, n - 1, &len);
    int score = 0;
    memset(&info, 0, sizeof info);
    rolltui_ansi_line_info(t, n - 1, &info);
    o->tail_hit = info.rule || rolltui_filter_match(f, line, len, &score);
  }
}

/* THE VIEW'S LINES: every line, or the filter's hits. How many, and the line index of the `k`th. */
static size_t view_count(const RolltuiOutputCtx* o, const RolltuiAnsiText* t, const RolltuiFilter* f) {
  return f ? o->hit_n + (size_t)(o->tail_hit != 0) : rolltui_ansi_line_count(t);
}
static size_t view_line(const RolltuiOutputCtx* o, const RolltuiAnsiText* t, const RolltuiFilter* f, size_t k) {
  RolltuiAnsiLineInfo first;
  if (!f) return k;
  if (k == o->hit_n) return rolltui_ansi_line_count(t) - 1; /* the line being written */
  memset(&first, 0, sizeof first);
  rolltui_ansi_line_info(t, 0, &first);
  return (size_t)(o->hits[k] - first.seq);
}

static void output_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  const RolltuiAnsiText* t;
  const RolltuiWidgetEnv* env = rolltui_windows_env(o->w);
  const RolltuiFilter* f = NULL;
  unsigned long long version = 0;
  const RolltuiOutputOptions* opt = output_opt(o, &f, &version);
  const int was_end = o->follow || o->top >= output_max_top(o);
  const size_t label_w = rolltui_windows_output_label_width(o->w, o->source.p ? o->source.p : "", o->source.n);
  int want;
  rolltui_content_rect(rn, &o->area);
  o->gutter = (opt && opt->timestamps ? 9 : 0) + (label_w ? (int)label_w + 1 : 0);
  if (o->gutter > o->area.w / 2) o->gutter = 0; /* the text keeps most of the window */
  o->text_area = o->area;
  o->text_area.x += o->gutter;
  o->text_area.w -= o->gutter;
  want = rolltui_windows_output_pump(o->w, o->source.p ? o->source.p : "", o->source.n, o->text_area.w, o->area.h);
  if (want >= 0) rolltui_windows_want_frame(o->w, want); /* what a running program writes is drawn as it comes */
  t = output_text(o);
  if (!t) {
    o->total = 0;
    return;
  }
  if (f) output_scan(o, t, f, version);
  if (!f && !o->gutter) { /* every line, no gutter: the text's own counts */
    o->total = o->wrap ? rolltui_ansi_rows(t, o->area.w, env->ambiguous_wide) : rolltui_ansi_line_count(t);
  } else {
    const size_t n = view_count(o, t, f);
    size_t k;
    o->total = 0;
    if (!o->wrap) o->total = n;
    else
      for (k = 0; k < n; ++k) o->total += rolltui_ansi_line_rows(t, view_line(o, t, f, k), o->text_area.w, env->ambiguous_wide);
  }
  o->width = o->wrap ? 0 : rolltui_ansi_width(t);
  if (was_end) o->top = output_max_top(o); /* at the end, it stays there as the program writes more */
  if (o->top > output_max_top(o)) o->top = output_max_top(o);
  o->follow = o->top >= output_max_top(o);
  if (o->wrap) o->left = 0;
}

/* A LINE'S GUTTER at (x, y): its time, then its source's label in that source's role. */
static void output_gutter(RolltuiOutputCtx* o, const RolltuiAnsiText* t, size_t li, RolltuiFrame* f, int y, const RolltuiStyle* styles,
                          RolltuiStyle muted, int aw) {
  const RolltuiOutputOptions* opt = output_opt(o, NULL, NULL);
  RolltuiAnsiLineInfo info;
  int x = o->area.x;
  if (!o->ds) o->ds = rolltui_draw_scratch_new();
  memset(&info, 0, sizeof info);
  rolltui_ansi_line_info(t, li, &info);
  if (opt && opt->timestamps) {
    struct tm tm;
    char hms[16];
    if (info.at_ms && rolltui_os_localtime((long long)(info.at_ms / 1000ULL), &tm) && strftime(hms, sizeof hms, "%H:%M:%S", &tm) == 8)
      rolltui_frame_put_text(f, o->ds, x, y, hms, 8, muted, 8, aw, 0);
    x += 9;
  }
  {
    const char* label = NULL;
    size_t ll = 0;
    unsigned char role = 0;
    if (rolltui_windows_output_source(o->w, o->source.p ? o->source.p : "", o->source.n, info.tag, &label, &ll, &role)) {
      RolltuiStyle ls = styles[role < ROLLTUI_ROLE_COUNT ? role : ROLLTUI_ROLE_TEXT];
      ls.bg = muted.bg;
      ls.bold = 1;
      rolltui_frame_put_text(f, o->ds, x, y, label, ll, ls, o->area.x + o->gutter - 1 - x, aw, 0);
    }
  }
}

static void output_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  const RolltuiAnsiText* t;
  const RolltuiWidgetEnv* env = rolltui_windows_env(o->w);
  const RolltuiStyle* styles = rolltui_windows_styles(o->w);
  const RolltuiFilter* filt = NULL;
  RolltuiStyle base, muted;
  output_ctx_layout(ctx, rn);
  output_opt(o, &filt, NULL);
  t = output_text(o);
  if (!t || o->area.w <= 0 || o->area.h <= 0) return;
  base = styles[rolltui_windows_builtin_roles(o->w)->text];
  base.bg = scroll_text_ground(f, o->area.x, o->area.y); /* the program's default colours are the window's own */
  muted = styles[ROLLTUI_ROLE_TEXT_MUTED];
  muted.bg = base.bg;
  if (!filt && !o->gutter) { /* every line, no gutter */
    const size_t n = rolltui_ansi_line_count(t);
    rolltui_ansi_draw(t, f, o->area, o->top, o->left, o->wrap, base, env->ambiguous_wide);
    if (o->wrap) {
      const size_t end = o->top + (size_t)o->area.h;
      o->below = 0;
      if (end < o->total) { /* the lines past the view: counted from the end, row by row */
        size_t rows = o->total, k = n;
        while (k > 0 && rows > end) rows -= rolltui_ansi_line_rows(t, --k, o->area.w, env->ambiguous_wide), ++o->below;
      }
    } else {
      o->below = o->top + (size_t)o->area.h < n ? n - (o->top + (size_t)o->area.h) : 0;
    }
    return;
  }
  {
    const size_t n = view_count(o, t, filt);
    size_t k = 0, rows = 0, lit_b[24], lit_e[24], lit[48];
    int skip = 0, y = o->area.y;
    RolltuiAnsiLineDraw ld;
    memset(&ld, 0, sizeof ld);
    ld.left = o->left;
    ld.wrap = (unsigned char)(o->wrap != 0);
    ld.ambiguous_wide = (unsigned char)(env->ambiguous_wide != 0);
    ld.base = base;
    ld.lit_style.bold = 1;
    ld.lit_style.underline = 1;
    if (o->wrap) { /* the line the view's first row is in, and its rows above the view */
      while (k < n) {
        const size_t r = rolltui_ansi_line_rows(t, view_line(o, t, filt, k), o->text_area.w, env->ambiguous_wide);
        if (rows + r > o->top) {
          skip = (int)(o->top - rows);
          break;
        }
        rows += r;
        ++k;
      }
    } else {
      k = o->top;
    }
    for (; k < n && y < o->area.y + o->area.h; ++k) {
      const size_t li = view_line(o, t, filt, k);
      RolltuiRect r = o->text_area;
      size_t len = 0, m = 0, j;
      const char* line = rolltui_ansi_line(t, li, &len);
      int drawn;
      r.y = y;
      r.h = o->area.y + o->area.h - y;
      if (filt) { /* the matches, lit */
        m = rolltui_filter_spans(filt, line, len, lit_b, lit_e, 24);
        for (j = 0; j < m; ++j) lit[2 * j] = lit_b[j], lit[2 * j + 1] = lit_e[j];
      }
      ld.lit = m ? lit : NULL;
      ld.lit_n = m;
      ld.skip = skip;
      drawn = rolltui_ansi_draw_line(t, li, f, r, &ld);
      if (skip == 0 && o->gutter) output_gutter(o, t, li, f, y, styles, muted, env->ambiguous_wide);
      y += drawn > 0 ? drawn : 1;
      skip = 0;
    }
    o->below = n > k ? n - k : 0;
  }
}

static int output_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  if (e->kind == ROLLTUI_EVENT_MOUSE && !o->wrap && (e->mouse.kind == 6 || e->mouse.kind == 7)) { /* wheel left and right */
    const int max_left = o->width > o->text_area.w ? o->width - o->text_area.w : 0;
    o->left += e->mouse.kind == 6 ? -4 : 4;
    if (o->left < 0) o->left = 0;
    if (o->left > max_left) o->left = max_left;
    return 1;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    int top = (int)o->top;
    size_t n = 0;
    const char* a = rolltui_bindings_action_for(rolltui_windows_bindings(o->w), &e->key, "view", 4, &n);
    if (a && str_eq_lit(a, n, "view.wrap")) { /* the binding's, so the host's call and the key agree */
      rolltui_windows_set_output_wrap(o->w, o->source.p ? o->source.p : "", o->source.n, !o->wrap);
      o->wrap = !o->wrap;
      o->left = 0;
      return 1;
    }
    if (a && (str_eq_lit(a, n, "view.left") || str_eq_lit(a, n, "view.right"))) {
      const int max_left = !o->wrap && o->width > o->text_area.w ? o->width - o->text_area.w : 0;
      o->left += str_eq_lit(a, n, "view.left") ? -SCROLL_TEXT_STEP : SCROLL_TEXT_STEP;
      if (o->left < 0) o->left = 0;
      if (o->left > max_left) o->left = max_left;
      return 1;
    }
    if (rolltui_scroll_by_action(&o->actions, rolltui_windows_bindings(o->w), &e->key, o->area.h, (int)o->total, &top)) {
      o->top = (size_t)top;
      o->follow = o->top >= output_max_top(o);
      return 1;
    }
  }
  return 0;
}

static int output_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  out->first = o->top;
  out->visible = (size_t)(o->area.h > 0 ? o->area.h : 0);
  out->total = o->total;
  return 1;
}

static int output_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  o->top = first > output_max_top(o) ? output_max_top(o) : first;
  o->follow = o->top >= output_max_top(o);
  return 1;
}

/* THE FOOTER, with `status`: following, or paused with what is below; and a program running into it, or how it ended. */
static int output_ctx_footer(void* ctx, RolltuiStr* out) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  const RolltuiOutputOptions* opt = output_opt(o, NULL, NULL);
  const char* name = o->source.p ? o->source.p : "";
  int status = 0;
  char buf[64];
  if (!opt || !opt->status) return 0;
  rolltui_str_clear(out);
  if (o->follow) rolltui_str_append(out, "following", sizeof "following" - 1);
  else {
    snprintf(buf, sizeof buf, "paused \xc2\xb7 %zu below", o->below);
    rolltui_str_append(out, buf, strlen(buf));
  }
  if (rolltui_windows_output_running(o->w, name, o->source.n, &status)) {
    rolltui_str_append(out, " \xc2\xb7 running", sizeof " \xc2\xb7 running" - 1);
  } else if (rolltui_windows_output_ran(o->w, name, o->source.n)) {
    if (status < 0) snprintf(buf, sizeof buf, " \xc2\xb7 stopped");
    else snprintf(buf, sizeof buf, " \xc2\xb7 exited %d", status);
    rolltui_str_append(out, buf, strlen(buf));
  }
  return 1;
}

static void output_ctx_destroy(void* ctx) {
  RolltuiOutputCtx* o = (RolltuiOutputCtx*)ctx;
  rolltui_str_free(&o->source);
  rolltui_mem_free(o->hits);
  rolltui_draw_scratch_free(o->ds);
  rolltui_mem_free(o);
}

static int output_ctx_takes(void* ctx, const RolltuiChord* k) { return takes_viewer(((RolltuiOutputCtx*)ctx)->w, k); }

static const RolltuiWidgetPlugin kOutputPlugin = {
    output_ctx_destroy, output_ctx_layout, output_ctx_draw, output_ctx_problem, NULL, NULL, output_ctx_handle,
    output_ctx_scroll_extent, output_ctx_scroll_to, NULL /* title: the layout's */, NULL /* back: no levels */,
    keys_transcript,
    NULL,
    output_ctx_footer,
    output_ctx_takes /* its scrolling keys and the `view` scope's */
};


/* ============================================================================================
 * image:<path> — a picture: the one the host bound under that name, or the file, read again when it changes.
 * ============================================================================================ */

struct ImageRead;
typedef struct RolltuiImageCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  RolltuiImage* file; /* OWNED: what the file held when it was last read */
  RolltuiStr read_path, why;
  long long stamp;
  int read;
  RolltuiDrawScratch* draw;
  struct ImageRead* pending; /* a read on the workers (its job's): the picture before it stays on screen until it is back */
} RolltuiImageCtx;

/* A PICTURE FILE READ AND DECODED ON A WORKER. OWNED by its job; `ic` NULL once the window went. */
typedef struct ImageRead {
  RolltuiImageCtx* ic;
  RolltuiTasks* tasks;
  unsigned int job;
  RolltuiStr path, why;
  RolltuiImage* img;
} ImageRead;

static void image_read_work(void* ctx, const RolltuiJob* job) {
  ImageRead* r = (ImageRead*)ctx;
  if (!rolltui_job_cancelled(job)) r->img = rolltui_image_load(r->path.p ? r->path.p : "", r->path.n, &r->why);
}
static void image_read_done(void* ctx, int cancelled) {
  ImageRead* r = (ImageRead*)ctx;
  if (r->ic && r->ic->pending == r) {
    r->ic->pending = NULL;
    if (!cancelled) { /* the new picture, in the old one's place */
      rolltui_image_free(r->ic->file);
      r->ic->file = r->img;
      r->img = NULL;
      rolltui_str_set(&r->ic->why, r->why.p ? r->why.p : "", r->why.n);
    }
  }
  rolltui_image_free(r->img);
  rolltui_str_free(&r->path);
  rolltui_str_free(&r->why);
  rolltui_mem_free(r);
}

static void image_ctx_refresh(RolltuiImageCtx* ic) {
  RolltuiStr p;
  const char* dir;
  size_t dir_len = 0;
  long long m;
  memset(&p, 0, sizeof p);
  dir = rolltui_windows_dir(ic->w, &dir_len);
  if ((ic->source.n && ic->source.p[0] == '/') || dir_len == 0) {
    rolltui_str_set(&p, ic->source.p ? ic->source.p : "", ic->source.n);
  } else {
    rolltui_str_set(&p, dir, dir_len);
    rolltui_str_append(&p, "/", 1);
    rolltui_str_append_str(&p, &ic->source);
  }
  m = file_ctx_stamp(p.p ? p.p : "");
  if (ic->read && m == ic->stamp && rolltui_str_eq(&ic->read_path, p.p ? p.p : "", p.n)) {
    rolltui_str_free(&p);
    return;
  }
  ic->read = 1;
  ic->stamp = m;
  rolltui_str_move(&ic->read_path, &p);
  {
    RolltuiTasks* tasks = rolltui_windows_tasks(ic->w);
    if (ic->pending) { /* a read of what the file was: dropped */
      ic->pending->ic = NULL;
      rolltui_tasks_cancel(ic->pending->tasks, ic->pending->job);
      ic->pending = NULL;
    }
    if (tasks && rolltui_tasks_workers(tasks) > 0) { /* decoded off the drawing thread: a large photo never holds a frame */
      ImageRead* r = (ImageRead*)rolltui_mem_alloc(sizeof *r);
      memset(r, 0, sizeof *r);
      r->ic = ic;
      r->tasks = tasks;
      rolltui_str_set(&r->path, ic->read_path.p ? ic->read_path.p : "", ic->read_path.n);
      ic->pending = r;
      r->job = rolltui_tasks_start(tasks, image_read_work, image_read_done, r);
      return;
    }
  }
  rolltui_image_free(ic->file);
  ic->file = rolltui_image_load(ic->read_path.p ? ic->read_path.p : "", ic->read_path.n, &ic->why);
}

static const RolltuiImage* image_ctx_picture(RolltuiImageCtx* ic) {
  const RolltuiImage* bound = rolltui_windows_image(ic->w, ic->source.p ? ic->source.p : "", ic->source.n);
  if (bound) return bound;
  image_ctx_refresh(ic);
  return ic->file;
}

static int image_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiImageCtx* ic = (RolltuiImageCtx*)ctx;
  if (image_ctx_picture(ic) || ic->pending) return 0; /* (one being read is not a problem: it is on its way) */
  rolltui_str_set(out, ic->why.p ? ic->why.p : "", ic->why.n);
  return 1;
}
static void image_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) { (void)ctx; (void)rn; }
static void image_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiImageCtx* ic = (RolltuiImageCtx*)ctx;
  const RolltuiImage* img = image_ctx_picture(ic);
  const RolltuiStyle* styles = rolltui_windows_styles(ic->w);
  RolltuiRect r, at;
  RolltuiStyle ground;
  int uc = 0, ur = 0;
  rolltui_content_rect(rn, &r);
  if (!img || r.w <= 0 || r.h <= 0) return;
  ground = styles[rolltui_windows_builtin_roles(ic->w)->text];
  ground.bg = scroll_text_ground(f, r.x, r.y); /* what shows through a transparent pixel is the window's own ground */
  rolltui_image_cells(img, r.w, r.h, &uc, &ur);
  at.x = r.x + (r.w - uc) / 2; /* centred in the window */
  at.y = r.y + (r.h - ur) / 2;
  at.w = uc;
  at.h = ur;
  rolltui_frame_image(f, at, img, ground, NULL);
}
static void image_ctx_destroy(void* ctx) {
  RolltuiImageCtx* ic = (RolltuiImageCtx*)ctx;
  if (ic->pending) { /* its read finishes unseen */
    ic->pending->ic = NULL;
    rolltui_tasks_cancel(ic->pending->tasks, ic->pending->job);
  }
  rolltui_str_free(&ic->source);
  rolltui_str_free(&ic->read_path);
  rolltui_str_free(&ic->why);
  rolltui_image_free(ic->file);
  rolltui_draw_scratch_free(ic->draw);
  rolltui_mem_free(ic);
}

static const RolltuiWidgetPlugin kImagePlugin = {
    image_ctx_destroy, image_ctx_layout, image_ctx_draw, image_ctx_problem, NULL, NULL, NULL, NULL, NULL, NULL /* title: the layout's */, NULL /* back: no levels */,
    NULL,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
};


/* ============================================================================================
 * list:<source> — a list, a table or a tree the host feeds by index.
 * ============================================================================================ */

typedef struct RolltuiListCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
} RolltuiListCtx;

/* The table's list of that name: made by the first bind or option, and kept until the table is freed. NULL until then. */
static RolltuiList* list_of(RolltuiListCtx* lc) {
  return rolltui_windows_has_list(lc->w, lc->source.p ? lc->source.p : "", lc->source.n)
             ? rolltui_windows_list(lc->w, lc->source.p ? lc->source.p : "", lc->source.n)
             : NULL;
}

static int list_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiListCtx* lc = (RolltuiListCtx*)ctx;
  if (rolltui_list_has_source(list_of(lc))) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &lc->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}
static void list_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiListCtx* lc = (RolltuiListCtx*)ctx;
  RolltuiList* l = list_of(lc);
  RolltuiRect r;
  if (!l) return;
  rolltui_content_rect(rn, &r);
  rolltui_list_layout(l, r, rolltui_windows_env(lc->w)->ambiguous_wide);
}
static void list_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiListCtx* lc = (RolltuiListCtx*)ctx;
  RolltuiList* l = list_of(lc);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(lc->w);
  RolltuiListRoles roles;
  if (!l) return;
  memset(&roles, 0, sizeof roles);
  roles.changed = br->info;
  roles.text = br->text;
  roles.text_muted = br->text_muted;
  roles.header = br->label;
  roles.cursor = br->cursor;
  roles.cursor_away = br->cursor_away;
  roles.mark = br->mark;
  roles.hover = br->hover;
  roles.prompt = br->prompt;
  roles.heading = rolltui_windows_menu_roles(lc->w)->breadcrumb;
  rolltui_list_set_cursor_shape(l, rolltui_windows_scrollbar_glyphs(lc->w)->cursor);
  list_ctx_layout(ctx, rn);
  rolltui_windows_want_frame(lc->w, rolltui_list_tick(l, rolltui_windows_env(lc->w)->now_ms));
  rolltui_list_draw(l, f, rolltui_windows_styles(lc->w), &roles, rn->focused, rn->hover ? rn->hover_y : -1,
                    rolltui_windows_env(lc->w)->ambiguous_wide);
}
static int list_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiListCtx* lc = (RolltuiListCtx*)ctx;
  RolltuiList* l = list_of(lc);
  int r;
  if (!l) return 0;
  rolltui_list_set_count(l, rolltui_windows_count_arg(lc->w)); /* `5 G`: a count that names a row, for this key only */
  r = rolltui_list_handle(l, e, rolltui_windows_bindings(lc->w), rolltui_list_default_actions());
  rolltui_list_set_count(l, 0);
  return r;
}
static int list_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiList* l = list_of((RolltuiListCtx*)ctx);
  return l && axis == ROLLTUI_AXIS_VERTICAL && rolltui_list_scroll_extent(l, out);
}
static int list_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiList* l = list_of((RolltuiListCtx*)ctx);
  return l && axis == ROLLTUI_AXIS_VERTICAL && rolltui_list_scroll_to(l, first);
}
static int list_ctx_back(void* ctx) {
  RolltuiList* l = list_of((RolltuiListCtx*)ctx);
  return l ? rolltui_list_back(l) : 0;
}
static int list_ctx_keys(void* ctx, const char** scope, size_t* len) {
  RolltuiList* l = list_of((RolltuiListCtx*)ctx);
  *scope = "list", *len = 4;
  return l && !rolltui_list_typing(l); /* a digit typed into the filter is the filter's */
}
static int list_ctx_footer(void* ctx, RolltuiStr* out) {
  RolltuiList* l = list_of((RolltuiListCtx*)ctx);
  return l ? rolltui_list_footer(l, out) : 0;
}
/* A filter being typed takes what is typed; otherwise the keys its scope binds. */
static int list_ctx_takes(void* ctx, const RolltuiChord* k) {
  RolltuiListCtx* lc = (RolltuiListCtx*)ctx;
  RolltuiList* l = list_of(lc);
  if (l && rolltui_list_typing(l) && (takes_typed(k) || k->key == ROLLTUI_KEY_BACKSPACE)) return 1;
  return takes_list(rolltui_windows_bindings(lc->w), l, k);
}
static int list_ctx_perform(void* ctx, const char* action, size_t len) {
  RolltuiList* l = list_of((RolltuiListCtx*)ctx);
  return l ? rolltui_list_perform(l, rolltui_list_default_actions(), action, len) : 0;
}
static void list_ctx_destroy(void* ctx) {
  RolltuiListCtx* lc = (RolltuiListCtx*)ctx;
  rolltui_str_free(&lc->source);
  rolltui_mem_free(lc);
}

static const RolltuiWidgetPlugin kListPlugin = {
    list_ctx_destroy, list_ctx_layout, list_ctx_draw, list_ctx_problem, NULL, NULL, list_ctx_handle,
    list_ctx_scroll_extent, list_ctx_scroll_to, NULL /* title: the layout's */, list_ctx_back /* the filter, then the marks */,
    list_ctx_keys, list_ctx_perform,
    list_ctx_footer,
    list_ctx_takes
};


/* ============================================================================================
 * chart:<source> — numbers the host binds, drawn.
 * ============================================================================================ */

typedef struct RolltuiChartCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiStr source;
  RolltuiDrawScratch* draw;
} RolltuiChartCtx;

static int chart_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiChartCtx* cc = (RolltuiChartCtx*)ctx;
  if (rolltui_windows_has_chart(cc->w, cc->source.p ? cc->source.p : "", cc->source.n)) return 0;
  rolltui_str_clear(out);
  rolltui_str_append(out, "nothing is bound to '", sizeof("nothing is bound to '") - 1);
  rolltui_str_append_str(out, &cc->source);
  rolltui_str_append(out, "'", 1);
  return 1;
}
static void chart_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  (void)ctx;
  (void)rn;
}
static void chart_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiChartCtx* cc = (RolltuiChartCtx*)ctx;
  RolltuiChart* c = rolltui_windows_fill_chart(cc->w, cc->source.p ? cc->source.p : "", cc->source.n);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(cc->w);
  RolltuiChartRoles roles;
  RolltuiRect r;
  size_t i;
  if (!c) return;
  roles.axis = br->text_muted;
  for (i = 0; i < ROLLTUI_CHART_SERIES_MAX; ++i) roles.series[i] = br->series[i];
  rolltui_content_rect(rn, &r);
  rolltui_chart_set_default_symbols(c, rolltui_windows_scrollbar_glyphs(cc->w)->chart);
  rolltui_chart_draw_roles(c, f, cc->draw, r, rolltui_windows_styles(cc->w), &roles, rolltui_windows_env(cc->w)->ambiguous_wide);
}
static void chart_ctx_destroy(void* ctx) {
  RolltuiChartCtx* cc = (RolltuiChartCtx*)ctx;
  rolltui_str_free(&cc->source);
  rolltui_draw_scratch_free(cc->draw);
  rolltui_mem_free(cc);
}

static const RolltuiWidgetPlugin kChartPlugin = {
    chart_ctx_destroy, chart_ctx_layout, chart_ctx_draw, chart_ctx_problem, NULL, NULL, NULL,
    NULL, NULL, NULL /* title: the layout's */, NULL /* back: no levels */,
    NULL,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
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
    error_ctx_destroy, error_ctx_layout, error_ctx_draw, error_ctx_problem, NULL, NULL, NULL, NULL, NULL, NULL /* title: the layout's */, NULL /* back: no levels */,
    NULL,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
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

static RolltuiWidget gauge_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiGaugeCtx* gc;
  RolltuiWidget out;
  (void)c;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host, &name, &name_len, &source, &source_len,
                             &problem, &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  gc = (RolltuiGaugeCtx*)rolltui_mem_alloc(sizeof *gc);
  memset(gc, 0, sizeof *gc);
  gc->w = w;
  rolltui_str_set(&gc->source, source, source_len);
  gc->draw = rolltui_draw_scratch_new();
  out.vt = &kGaugePlugin;
  out.ctx = gc;
  return out;
}

static RolltuiWidget output_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiOutputCtx* o;
  RolltuiWidget out;
  (void)c;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host, &name, &name_len, &source, &source_len,
                             &problem, &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  o = (RolltuiOutputCtx*)rolltui_mem_alloc(sizeof *o);
  memset(o, 0, sizeof *o);
  o->w = w;
  o->wrap = 1;
  o->follow = 1;
  rolltui_str_set(&o->source, source, source_len);
  o->actions = *rolltui_windows_scroll_text_actions(w);
  out.vt = &kOutputPlugin;
  out.ctx = o;
  return out;
}

static RolltuiWidget image_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiImageCtx* ic;
  RolltuiWidget out;
  (void)c;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host, &name, &name_len, &source, &source_len,
                             &problem, &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  ic = (RolltuiImageCtx*)rolltui_mem_alloc(sizeof *ic);
  memset(ic, 0, sizeof *ic);
  ic->w = w;
  rolltui_str_set(&ic->source, source, source_len);
  ic->draw = rolltui_draw_scratch_new();
  out.vt = &kImagePlugin;
  out.ctx = ic;
  return out;
}

static RolltuiWidget list_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiListCtx* lc;
  RolltuiWidget out;
  (void)c;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host, &name, &name_len, &source, &source_len,
                             &problem, &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  lc = (RolltuiListCtx*)rolltui_mem_alloc(sizeof *lc);
  memset(lc, 0, sizeof *lc);
  lc->w = w;
  rolltui_str_set(&lc->source, source, source_len);
  out.vt = &kListPlugin;
  out.ctx = lc;
  return out;
}

static RolltuiWidget chart_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  RolltuiChartCtx* cc;
  RolltuiWidget out;
  (void)c;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  if (!rolltui_content_parse(rolltui_windows_context(w), content, n, &row, &is_host, &name, &name_len, &source, &source_len,
                             &problem, &why)) {
    rolltui_str_free(&why);
    return out;
  }
  rolltui_str_free(&why);
  cc = (RolltuiChartCtx*)rolltui_mem_alloc(sizeof *cc);
  memset(cc, 0, sizeof *cc);
  cc->w = w;
  rolltui_str_set(&cc->source, source, source_len);
  cc->draw = rolltui_draw_scratch_new();
  out.vt = &kChartPlugin;
  out.ctx = cc;
  return out;
}

static RolltuiWidget messages_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  RolltuiMessagesCtx* m;
  RolltuiWidget out;
  (void)c;
  (void)content;
  (void)n;
  m = (RolltuiMessagesCtx*)rolltui_mem_alloc(sizeof *m);
  memset(m, 0, sizeof *m);
  scroll_text_base_init(&m->base, w);
  out.vt = &kMessagesPlugin;
  out.ctx = m;
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

static RolltuiWidget keys_sheet_widget(RolltuiWindows* w, const char* scope, size_t scope_len, int is_help);
static RolltuiWidget help_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  size_t row = 0;
  int is_host = 0;
  const char *name = NULL, *source = NULL;
  size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
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
  (void)out;
  return keys_sheet_widget(w, source, source_len, 1); /* F1 is the keys sheet: read, and edited where a store was handed over */
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

/* ---- `filepicker`: the column browser, adapted ------------------------------------------------
 * The widget is `rolltui_widget_picker.c`; what is here is the plugin table and the four window calls.
 * The ANSWER is polled rather than pushed: a host asks on the frame after it opened the panel,
 * which is where it already asks a preset store for its version. A callback would need a host to
 * keep one alive across a screen swap for a widget it may not own. */
typedef struct RolltuiPickerCtx {
  RolltuiWindows* w; /* BORROWED */
  RolltuiPicker* p;  /* OWNED */
} RolltuiPickerCtx;

static void picker_ctx_destroy(void* ctx) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  rolltui_picker_free(c->p);
  rolltui_mem_free(c);
}
static void picker_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  RolltuiRect inner;
  rolltui_content_rect(rn, &inner);
  rolltui_picker_set_tasks(c->p, rolltui_windows_tasks(c->w));
  rolltui_picker_set_now(c->p, rolltui_windows_env(c->w)->now_ms);
  rolltui_picker_layout(c->p, inner);
}
static void picker_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(c->w);
  picker_ctx_layout(ctx, rn);
  rolltui_picker_set_hover(c->p, rn->hover, rn->hover_x, rn->hover_y);
  rolltui_picker_draw(c->p, f, rolltui_windows_styles(c->w), rolltui_windows_scrollbar_glyphs(c->w), env->ambiguous_wide, rn->focused);
}
/* Notes do NOT stop the widget drawing — the dotfile count is exactly that kind of remark. */
static int picker_ctx_note_at(void* ctx, size_t i, RolltuiStr* out) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  char buf[32];
  const size_t hidden = rolltui_picker_hidden_count(c->p);
  if (i != 0 || hidden == 0) return 0;
  snprintf(buf, sizeof buf, "%zu hidden", hidden);
  rolltui_str_set(out, buf, strlen(buf));
  return 1;
}
static int picker_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  rolltui_picker_set_tasks(c->p, rolltui_windows_tasks(c->w));
  rolltui_picker_set_now(c->p, rolltui_windows_env(c->w)->now_ms);
  rolltui_picker_set_wheel_lines(c->p, rolltui_windows_env(c->w)->wheel_lines);
  return rolltui_picker_handle(c->p, e, rolltui_windows_bindings(c->w), rolltui_picker_default_actions());
}
static int picker_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  return rolltui_picker_scroll_extent(c->p, out);
}
static int picker_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiPickerCtx* c = (RolltuiPickerCtx*)ctx;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  return rolltui_picker_scroll_to(c->p, first);
}

static const RolltuiWidgetPlugin kPickerPlugin = {
    picker_ctx_destroy, picker_ctx_layout, picker_ctx_draw, NULL, picker_ctx_note_at,
    NULL,               picker_ctx_handle, picker_ctx_scroll_extent, picker_ctx_scroll_to, NULL /* title: the layout's */, NULL /* back: no levels */,
    keys_picker,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
};

static RolltuiWidget picker_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  RolltuiPickerCtx* pc;
  RolltuiWidget out;
  const char *source = NULL;
  size_t source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why;
  (void)c;
  memset(&out, 0, sizeof out);
  memset(&why, 0, sizeof why);
  /* The source is where it starts — a layout may say `filepicker:~/src` and need no host code —
   * and a host's `rolltui_windows_set_picker_dir` overrides it at any time. */
  rolltui_content_parse(rolltui_windows_context(w), content, n, NULL, NULL, NULL, NULL, &source, &source_len, &problem, &why);
  rolltui_str_free(&why);
  pc = (RolltuiPickerCtx*)rolltui_mem_alloc(sizeof *pc);
  memset(pc, 0, sizeof *pc);
  pc->w = w;
  pc->p = rolltui_picker_new();
  rolltui_picker_go_to(pc->p, source && source_len ? source : ".", source && source_len ? source_len : 1);
  out.vt = &kPickerPlugin;
  out.ctx = pc;
  return out;
}

static RolltuiPicker* picker_for(RolltuiWindows* w, const char* content, size_t len) {
  RolltuiWidget* widget = rolltui_windows_widget_for(w, content, len);
  if (!widget || widget->vt != &kPickerPlugin) return NULL;
  return ((RolltuiPickerCtx*)widget->ctx)->p;
}

void rolltui_windows_set_picker_dir(RolltuiWindows* w, const char* content, size_t len, const char* dir,
                                    size_t dir_len) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (!p) return;
  rolltui_picker_go_to(p, dir && dir_len ? dir : ".", dir && dir_len ? dir_len : 1);
}
void rolltui_windows_set_picker_options(RolltuiWindows* w, const char* content, size_t len,
                                        const RolltuiPickerOptions* o) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (p) rolltui_picker_set_options(p, o);
}
size_t rolltui_windows_picker_marked(RolltuiWindows* w, const char* content, size_t len, RolltuiStrListRaw* out) {
  RolltuiPicker* p = picker_for(w, content, len);
  return p ? rolltui_picker_marked(p, out) : 0;
}
void rolltui_windows_set_picker_decor(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerDecorFn fn, void* ctx) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (p) rolltui_picker_set_decor(p, fn, ctx);
}
void rolltui_windows_set_picker_preview_command(RolltuiWindows* w, const char* content, size_t len, const char* const* argv) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (p) rolltui_picker_set_preview_command(p, argv);
}
void rolltui_windows_set_picker_previewer(RolltuiWindows* w, const char* content, size_t len, RolltuiPreviewerFn fn, void* ctx) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (p) rolltui_picker_set_previewer(p, fn, ctx);
}
int rolltui_windows_picker_event(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerEvent* out) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (!p) return 0;
  return rolltui_picker_event(p, out);
}
int rolltui_windows_picker_taken(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* out) {
  RolltuiPickerEvent ev;
  int taken;
  memset(&ev, 0, sizeof ev);
  if (!rolltui_windows_picker_event(w, content, len, &ev)) return 0;
  taken = ev.kind == ROLLTUI_PICKER_EVENT_TAKEN;
  if (taken && out) rolltui_str_set(out, ev.path.p ? ev.path.p : "", ev.path.n);
  rolltui_picker_event_release(&ev);
  return taken;
}
int rolltui_windows_picker_selected(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* path,
                                    int* is_dir) {
  RolltuiPicker* p = picker_for(w, content, len);
  return p ? rolltui_picker_selected(p, path, is_dir) : 0;
}
int rolltui_windows_picker_status(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerStatus* out) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (!p) return 0;
  rolltui_picker_status(p, out);
  return 1;
}
int rolltui_windows_picker_dir(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* out) {
  RolltuiPicker* p = picker_for(w, content, len);
  return p ? rolltui_picker_dir(p, out) : 0;
}
void rolltui_windows_picker_focus_column(RolltuiWindows* w, const char* content, size_t len, size_t column) {
  RolltuiPicker* p = picker_for(w, content, len);
  if (p) rolltui_picker_focus_column(p, column);
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
  rolltui_context_register_kind(ctx, "gauge", 5, gauge_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "messages", 8, messages_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "output", 6, output_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "image", 5, image_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "list", 4, list_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "chart", 5, chart_widget_factory, NULL, NULL);
  rolltui_context_register_kind(ctx, "dialog", 6, rolltui_dialog_widget_factory, NULL, NULL);
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
      /*hover=*/ROLLTUI_ROLE_HOVER,
      /*gauge=*/ROLLTUI_ROLE_ACCENT_2,
      /*panel=*/ROLLTUI_ROLE_PANEL_BACKGROUND,
      /*border=*/ROLLTUI_ROLE_BORDER,
      /*info=*/ROLLTUI_ROLE_ACCENT_1,
      /*ok=*/ROLLTUI_ROLE_ACCENT_2,
      /*warning=*/ROLLTUI_ROLE_WARNING,
      /*cursor=*/ROLLTUI_ROLE_MENU_SELECTED,
      /*cursor_away=*/ROLLTUI_ROLE_SELECTION,
      /*mark=*/ROLLTUI_ROLE_ACCENT_1,
      /*prompt=*/ROLLTUI_ROLE_PROMPT,
      /*series=*/{ROLLTUI_ROLE_ACCENT_1, ROLLTUI_ROLE_ACCENT_2, ROLLTUI_ROLE_ACCENT_3, ROLLTUI_ROLE_ACCENT_4},
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
      /*hover=*/ROLLTUI_ROLE_HOVER,
      /*selected_away=*/ROLLTUI_ROLE_SELECTION,
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
  RolltuiNoteRaw note; /* reused scratch for note_info() */
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

static RolltuiNoteRaw* input_ctx_note_info(RolltuiInputCtx* ic) {
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
    const RolltuiInputOptionsRaw* o = rolltui_input_options(ic->ed);
    int pw = rolltui_u_display_width(ic->uscratch, o->prompt.p ? o->prompt.p : "", o->prompt.n, env->ambiguous_wide);
    int phw =
        rolltui_u_display_width(ic->uscratch, o->placeholder.p ? o->placeholder.p : "", o->placeholder.n, env->ambiguous_wide);
    return pw + phw;
  }
}
static int input_ctx_rows_with_note(RolltuiInputCtx* ic, int width, int text_rows) {
  RolltuiNoteRaw* note = input_ctx_note_info(ic);
  const RolltuiWidgetEnv* env = rolltui_windows_env(ic->w);
  int nw = note->text.n == 0
               ? 0
               : rolltui_u_display_width(ic->uscratch, note->text.p ? note->text.p : "", note->text.n,
                                        env->ambiguous_wide);
  return rolltui_input_window_rows(text_rows, input_ctx_end_col(ic), nw, width, ic->max_rows);
}
static int input_ctx_note_owns_row(RolltuiInputCtx* ic, int width) {
  RolltuiNoteRaw* note = input_ctx_note_info(ic);
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
  const RolltuiInputOptionsRaw* cur = rolltui_input_options(ic->ed);
  const RolltuiWidgetEnv* env = rolltui_windows_env(ic->w);
  unsigned char aw = env->ambiguous_wide ? 1 : 0;
  int inset = rn->node->border != 0 ? 1 : 0;
  if (cur->ambiguous_wide != aw || cur->inset != inset) {
    RolltuiInputOptionsRaw o;
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
  RolltuiNoteRaw* note;
  int nw, used;
  iroles.text = ic->roles.input_text;
  iroles.selection = ic->roles.input_selection;
  iroles.placeholder = ic->roles.input_placeholder;
  iroles.line_number = ic->roles.text_muted;
  if (rolltui_input_options(ic->ed)->language.n) rolltui_input_set_syntax(ic->ed, rolltui_windows_syntax(ic->w));
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
  RolltuiStr before;
  int action;
  memset(&before, 0, sizeof before);
  {
    size_t blen = 0;
    const char* bt = rolltui_input_text(ed, &blen);
    rolltui_str_set(&before, bt, blen);
  }
  action = (int)rolltui_input_handle(ed, e, b, rolltui_windows_input_actions(w), env->now_ms);
  {
    size_t alen = 0;
    const char* at = rolltui_input_text(ed, &alen);
    if (!rolltui_str_eq(&before, at, alen)) rolltui_windows_call_changed(w, source, source_len, at, alen);
  }
  rolltui_str_free(&before);
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

/* Typing is the input's, and so is every key its scope binds (Ctrl-E the end of the line, Ctrl-Z its undo). In normal and visual
 * mode a printable key is still the field's, as a command or nothing, so none reaches the program around it; Escape is visual's
 * (back to normal) and not normal's (the popup closes). */
static int input_ctx_takes(void* ctx, const RolltuiChord* k) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  size_t n = 0, sl = 0;
  const int mode = rolltui_input_mode(ic->ed);
  const char* a;
  if (mode == ROLLTUI_INPUT_NORMAL || mode == ROLLTUI_INPUT_VISUAL) {
    const RolltuiBindings* b = rolltui_windows_bindings(ic->w);
    if (takes_typed(k)) return 1;
    if (mode == ROLLTUI_INPUT_VISUAL && takes_action(b, k, "visual", &n)) return 1;
    if (takes_action(b, k, "normal", &n)) return 1;
    if (k->key == ROLLTUI_KEY_ESCAPE) return 0;
  }
  a = takes_action(rolltui_windows_bindings(ic->w), k, "input", &n);
  if (takes_typed(k) || k->key == ROLLTUI_KEY_BACKSPACE || k->key == ROLLTUI_KEY_DELETE) return 1;
  /* COMPLETION'S KEYS are the field's only where there is something to complete: otherwise Tab moves the focus as before */
  if (rolltui_input_completion_open(ic->ed, NULL)) {
    if (k->key == ROLLTUI_KEY_ESCAPE || k->key == ROLLTUI_KEY_ENTER) return 1;
    if (takes_is(a, n, "input.complete") || takes_is(a, n, "input.complete_prev")) return 1;
  }
  if (takes_is(a, n, "input.complete") || takes_is(a, n, "input.complete_prev"))
    return rolltui_input_has_completer(ic->ed) && rolltui_input_completion_at(ic->ed, 0, NULL) != NULL;
  if (takes_is(a, n, "input.accept_suggestion")) {
    size_t gl = 0;
    rolltui_input_suggestion(ic->ed, &gl);
    return gl != 0;
  }
  if (takes_is(a, n, "input.clear_selection")) return rolltui_input_selected_text(ic->ed, &sl) && sl != 0; /* nothing selected: not its */
  return a != NULL;
}

/* THE CLOSE KEY CLOSES ONE LEVEL (the stack asks this first): typing and visual mode are levels above normal mode where the keys preset
 * has the modes, so Escape leaves them and a second one closes the popup; without the modes the field has no levels. */
static int input_ctx_back(void* ctx) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  const int mode = rolltui_input_mode(ic->ed);
  const char* name = rolltui_input_default_actions()->normal_mode;
  if (rolltui_input_completion_cancel(ic->ed)) return 1; /* an open list is the first level: its word goes back */
  if (mode == ROLLTUI_INPUT_NORMAL) return 0;
  if (mode == ROLLTUI_INPUT_INSERT && !rolltui_bindings_chord_count(rolltui_windows_bindings(ic->w), name, strlen(name))) return 0;
  return rolltui_input_perform(ic->ed, rolltui_input_default_actions(), name, strlen(name), 0) != ROLLTUI_INPUT_IGNORED ||
         (mode == ROLLTUI_INPUT_VISUAL && rolltui_input_perform_modal(ic->ed, "visual.normal", 13) == ROLLTUI_INPUT_HANDLED);
}
/* The scope the field's keys resolve in, which is its mode's: a key sequence (`d w`) and a count (`3 w`) are normal mode's. */
static int input_ctx_keys(void* ctx, const char** scope, size_t* len) {
  const int mode = rolltui_input_mode(((RolltuiInputCtx*)ctx)->ed);
  if (mode == ROLLTUI_INPUT_NORMAL) return *scope = "normal", *len = 6, 1;
  if (mode == ROLLTUI_INPUT_VISUAL) return *scope = "visual", *len = 6, 0;
  return keys_input(ctx, scope, len);
}
/* What a key sequence resolves to, in the mode's scope. */
static int input_ctx_perform(void* ctx, const char* action, size_t len) {
  RolltuiInputCtx* ic = (RolltuiInputCtx*)ctx;
  if (rolltui_input_perform_modal(ic->ed, action, len) == ROLLTUI_INPUT_HANDLED) return 1;
  return rolltui_input_perform(ic->ed, rolltui_input_default_actions(), action, len, rolltui_windows_env(ic->w)->now_ms) !=
         ROLLTUI_INPUT_IGNORED;
}
/* THE MODE, in the bottom border, while it is not typing: the caret's shape says it too, and this says it in words. */
static int input_ctx_footer(void* ctx, RolltuiStr* out) {
  const int mode = rolltui_input_mode(((RolltuiInputCtx*)ctx)->ed);
  const char* name = mode == ROLLTUI_INPUT_NORMAL ? "NORMAL" : mode == ROLLTUI_INPUT_VISUAL ? "VISUAL" : mode == ROLLTUI_INPUT_REPLACE ? "REPLACE" : NULL;
  if (!name) return 0;
  rolltui_str_set(out, name, strlen(name));
  return 1;
}

static const RolltuiWidgetPlugin kInputPlugin = {
    input_ctx_destroy,  input_ctx_layout, input_ctx_draw, input_ctx_problem, NULL,
    input_ctx_desired_outer, input_ctx_handle, NULL, NULL, NULL /* title: the layout's */, input_ctx_back /* vim's modes */,
    input_ctx_keys,
    input_ctx_perform,
    input_ctx_footer,
    input_ctx_takes
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
  out->wheel_lines = env->wheel_lines ? env->wheel_lines : 3; /* the person's, as every window's */
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
  const RolltuiDocumentRaw* doc = rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n);
  RolltuiTranscriptOptions o;
  if (!doc) return;
  transcript_ctx_options(tc, rn, &o);
  rolltui_transcript_layout(tc->t, doc, rn->inner, &o);
}
static void transcript_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  const RolltuiDocumentRaw* doc = rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n);
  RolltuiTranscriptOptions o;
  if (!doc) return;
  transcript_ctx_options(tc, rn, &o);
  rolltui_transcript_layout(tc->t, doc, rn->inner, &o);
  rolltui_transcript_draw(tc->t, f, tc->draw, rolltui_windows_styles(tc->w));
}
static int transcript_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiTranscriptCtx* tc = (RolltuiTranscriptCtx*)ctx;
  const RolltuiDocumentRaw* doc = rolltui_windows_document(tc->w, tc->source.p ? tc->source.p : "", tc->source.n);
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
    NULL,                   transcript_ctx_handle, transcript_ctx_scroll_extent, transcript_ctx_scroll_to, NULL /* title: the layout's */, NULL /* back: no levels */,
    keys_transcript,
    NULL,
    NULL,
    NULL /* takes: the keys scope's */
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

static int menu_ctx_back(void* ctx) { return rolltui_menu_back(((RolltuiMenuCtx*)ctx)->m); }

static int menu_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  rolltui_menu_scroll_to(mc->m, first > 0x7fffffff ? 0x7fffffff : (int)first);
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
  o.cursor_shape = rolltui_windows_scrollbar_glyphs(mc->w)->cursor;
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
  iroles.line_number = br->text_muted;
  rolltui_menu_set_hover(mc->m, rn->hover, rn->hover_x, rn->hover_y);
  rolltui_menu_draw(mc->m, f, mc->draw, rolltui_windows_styles(mc->w), rolltui_windows_menu_roles(mc->w), &iroles,
                    rn->focused);
}

/* The breadcrumb IS the window's title: "settings › Sort by". A menu never draws it inside
 * itself, so a window with a menu in it names the level once, on its border. */
static int menu_ctx_title(void* ctx, const char* given, size_t given_len, RolltuiStr* out) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  menu_ctx_refresh(mc);
  rolltui_menu_title(mc->m, given, given_len, out);
  return out->n != 0;
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

/* What is typed filters the menu, or goes into the field being edited; and the keys its scope binds. */
static int menu_ctx_takes(void* ctx, const RolltuiChord* k) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  return takes_typed(k) || takes_bound(rolltui_windows_bindings(mc->w), k, "menu");
}

static const RolltuiWidgetPlugin kMenuPlugin = {
    menu_ctx_destroy, menu_ctx_layout, menu_ctx_draw, menu_ctx_problem, menu_ctx_note_at,
    NULL,             NULL,            menu_ctx_scroll_extent, menu_ctx_scroll_to, menu_ctx_title, menu_ctx_back,
    keys_menu,
    NULL,
    NULL,
    menu_ctx_takes
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
  int show_presets;               /* the load and save keys are offered */
  RolltuiList* list;              /* OWNED: the roles, under their headings */
  RolltuiList* presets;           /* OWNED: the Load choice */
  RolltuiInput* name;             /* OWNED: a name for Save as */
  RolltuiStrListRaw preset_names; /* what the Load choice lists */
  struct ThemeRow* rows;
  size_t row_n, row_cap, row_init; /* rows filled, allocated, and ever zeroed */
  int sheet_mode;                 /* THEME_SHEET, _EDITING, _CHOOSING or _NAMING */
  unsigned char edit_role;        /* the role whose row is being edited */
  int field;                      /* its field: fg, bg, then each attribute */
  int typing;                     /* a colour is being typed into the field */
  RolltuiStr typed, scratch;
  unsigned long long seed;        /* the last seed a generated theme came from */
} RolltuiThemeCtx;

/* The store's working colours into the editor, and the store's preset names into the Load
 * choice. A no-op with no store: the editor keeps the built-in pair it starts on. */
static void theme_ctx_sync_store(RolltuiThemeCtx* tc) {
  RolltuiThemePresetValue* v;
  RolltuiPresetListRaw list;
  RolltuiStrListRaw names;
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

static void theme_set_mode(void* value, void* ctx) {
  RolltuiThemePresetValue* v = (RolltuiThemePresetValue*)value;
  const char* mode = (const char*)ctx;
  rolltui_str_set(&v->mode, mode, strlen(mode));
}

/* "Preview variant" picks which of a theme's two looks the EDITOR shows while you work on
 * either — the sample box, the role list's own colours. On its own that is a preview of
 * nothing a person watching the rest of the screen can see, which is the gap this closes: the
 * variant being looked at is pushed to the store's WORKING value too, UNPERSISTED (`persist=0`,
 * regardless of what this mount was given), so the whole app shows it live the same way a
 * committed colour already does, but a restart still comes back to whatever was actually
 * saved — a preview stays a preview. */
static void theme_ctx_preview_mode(RolltuiThemeCtx* tc) {
  const char* word = rolltui_theme_editor_mode(tc->ed) == ROLLTUI_MODE_LIGHT ? "light" : "dark";
  if (!tc->store) return;
  rolltui_preset_store_edit(tc->store, theme_set_mode, (void*)word, 0);
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

/* ---- THE THEME SHEET ---------------------------------------------------------------------------
 * Every role on one list, under the headings the roles fall into: what the role is for, drawn in the role itself, its fg and bg
 * with a swatch of each, and its attributes. Enter edits the row where it is; the screen behind shows the edit as it is made. */

#define THEME_SHEET 0
#define THEME_EDITING 1
#define THEME_CHOOSING 2
#define THEME_NAMING 3

/* the fields of a row being edited: fg, bg, then the attributes */
#define THEME_FIELD_FG 0
#define THEME_FIELD_BG 1
#define THEME_FIELD_ATTR 2

typedef struct ThemeRow {
  unsigned char heading;
  unsigned char role;
  int fix; /* the fix proposed for the role, or -1 */
  RolltuiStr key, words, fg, bg, attrs;
} ThemeRow;

static ThemeRow* theme_sheet_row(RolltuiThemeCtx* tc) {
  ThemeRow* r;
  tc->rows = (ThemeRow*)rolltui_grow(tc->rows, &tc->row_cap, tc->row_n + 1, sizeof *tc->rows);
  if (tc->row_n == tc->row_init) memset(&tc->rows[tc->row_init++], 0, sizeof *tc->rows);
  r = &tc->rows[tc->row_n++];
  r->heading = 0;
  r->role = 0;
  r->fix = -1;
  rolltui_str_clear(&r->key);
  rolltui_str_clear(&r->words);
  rolltui_str_clear(&r->fg);
  rolltui_str_clear(&r->bg);
  rolltui_str_clear(&r->attrs);
  return r;
}

static void theme_colour_text(RolltuiStyleColorRaw c, RolltuiStr* out) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  rolltui_str_set(out, buf, rolltui_color_to_string(c, buf, sizeof buf));
}

static void theme_sheet_build(RolltuiThemeCtx* tc) {
  const RolltuiStyle* st = rolltui_theme_editor_styles(tc->ed, rolltui_theme_editor_mode(tc->ed), 0);
  const size_t fixes = rolltui_theme_editor_fix_count(tc->ed);
  size_t i, k;
  tc->row_n = 0;
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    size_t sl = 0, wl = 0, rn = 0;
    const char* sec = rolltui_theme_editor_section_at((unsigned char)i, &sl);
    const char* words = rolltui_role_words((unsigned char)i, &wl);
    const char* name = rolltui_role_name((unsigned char)i, &rn);
    const unsigned char on[6] = {st[i].bold, st[i].italic, st[i].underline, st[i].dim, st[i].reverse, st[i].strike};
    ThemeRow* r;
    if (sec) {
      ThemeRow* h = theme_sheet_row(tc);
      h->heading = 1;
      rolltui_str_set(&h->key, K("section:"));
      rolltui_str_append(&h->key, sec, sl);
      rolltui_str_set(&h->words, sec, sl);
    }
    r = theme_sheet_row(tc);
    r->role = (unsigned char)i;
    rolltui_str_set(&r->key, name, rn);
    rolltui_str_set(&r->words, words, wl);
    theme_colour_text(st[i].fg, &r->fg);
    theme_colour_text(st[i].bg, &r->bg);
    for (k = 0; k < 6; ++k)
      if (on[k]) {
        size_t al = 0;
        const char* a = rolltui_theme_editor_attr_name(k, &al);
        if (r->attrs.n) rolltui_str_append(&r->attrs, K(", "));
        rolltui_str_append(&r->attrs, a, al);
      }
    for (k = 0; k < fixes; ++k)
      if (rolltui_theme_editor_fix_at(tc->ed, k)->role == i) {
        r->fix = (int)k;
        break;
      }
  }
  rolltui_list_refresh(tc->list);
}

static size_t theme_sheet_count(void* ctx) { return ((RolltuiThemeCtx*)ctx)->row_n; }
static void theme_sheet_fill(void* ctx, size_t i, RolltuiListRow* out) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const ThemeRow* r = &tc->rows[i];
  rolltui_str_set(&out->key, r->key.p ? r->key.p : "", r->key.n);
  rolltui_list_row_cell(out, r->words.p ? r->words.p : "", r->words.n);
  out->depth = r->heading ? 0 : 1;
  if (r->heading) return;
  rolltui_list_row_cell(out, r->fg.p ? r->fg.p : "", r->fg.n);
  rolltui_list_row_cell(out, r->bg.p ? r->bg.p : "", r->bg.n);
  rolltui_list_row_cell(out, r->attrs.p ? r->attrs.p : "", r->attrs.n);
  out->styled = 1;            /* what the role is for, drawn in the role */
  out->cell_role[0] = r->role;
  out->swatch = 2 | 4;        /* fg and bg, each after a swatch of itself */
  if (r->fix >= 0) out->role = rolltui_windows_builtin_roles(tc->w)->warning;
}

/* The row under the cursor, or NULL on a heading. */
static const ThemeRow* theme_sheet_cursor(RolltuiThemeCtx* tc) {
  size_t i;
  if (!rolltui_list_cursor(tc->list, &tc->scratch) || tc->scratch.n == 0) return NULL;
  for (i = 0; i < tc->row_n; ++i)
    if (!tc->rows[i].heading && rolltui_str_eq(&tc->rows[i].key, tc->scratch.p, tc->scratch.n)) return &tc->rows[i];
  return NULL;
}

/* THE SCREEN SHOWS THE EDIT AS IT IS MADE: what is previewed goes to the store's working copy unpersisted, so the whole program is
 * drawn in it, and a restart still comes back to what was kept. */
static void theme_ctx_push(RolltuiThemeCtx* tc, int persist) {
  size_t olen = 0;
  const char* origin;
  if (!tc->store) return;
  origin = rolltui_preset_store_origin(tc->store, &olen);
  rolltui_preset_store_edit(tc->store, theme_set_colours,
                            rolltui_theme_editor_shown_json(tc->ed, origin ? origin : "", olen), persist);
}

static void theme_edit_begin(RolltuiThemeCtx* tc, const ThemeRow* r) {
  tc->sheet_mode = THEME_EDITING;
  tc->edit_role = r->role;
  tc->field = THEME_FIELD_FG;
  tc->typing = 0;
  rolltui_str_clear(&tc->typed);
  rolltui_str_clear(&tc->hint);
}

static void theme_edit_end(RolltuiThemeCtx* tc, int keep) {
  RolltuiThemeEditorOutcome o;
  memset(&o, 0, sizeof o);
  tc->sheet_mode = THEME_SHEET;
  tc->typing = 0;
  if (keep) {
    rolltui_theme_editor_commit(tc->ed, &o);
    if (o.kind == ROLLTUI_THEME_EDIT_COMMITTED) {
      theme_ctx_push(tc, tc->persist);
      rolltui_str_set(&tc->hint, K("kept"));
    }
    rolltui_theme_editor_outcome_release(&o);
  } else if (rolltui_theme_editor_previewing(tc->ed)) {
    rolltui_theme_editor_drop_preview(tc->ed);
    theme_ctx_push(tc, 0);
    rolltui_str_set(&tc->hint, K("dropped"));
  }
}

/* The colour a field being edited shows: what is typed, else what the role has now. */
static RolltuiStyleColorRaw theme_field_colour(RolltuiThemeCtx* tc) {
  const RolltuiStyle* s = &rolltui_theme_editor_styles(tc->ed, rolltui_theme_editor_mode(tc->ed), 0)[tc->edit_role];
  return tc->field == THEME_FIELD_BG ? s->bg : s->fg;
}

static void theme_field_typed(RolltuiThemeCtx* tc) {
  RolltuiStyleColorRaw c;
  const int ok = rolltui_color_parse(tc->typed.p ? tc->typed.p : "", tc->typed.n, &c);
  rolltui_theme_editor_preview_colour(tc->ed, tc->edit_role, tc->field == THEME_FIELD_BG, ok ? &c : NULL);
  theme_ctx_push(tc, 0);
}

/* Up and Down on a colour field: the theme's own colours, one step at a time. */
static void theme_field_step(RolltuiThemeCtx* tc, int by) {
  const size_t n = rolltui_theme_editor_palette_count(tc->ed);
  const RolltuiStyleColorRaw now = theme_field_colour(tc);
  size_t i, at = n;
  RolltuiStyleColorRaw c;
  if (n == 0) return;
  for (i = 0; i < n; ++i)
    if (rolltui_theme_editor_palette_color(tc->ed, i, &c) && c.kind == now.kind && c.index == now.index && c.r == now.r &&
        c.g == now.g && c.b == now.b) {
      at = i;
      break;
    }
  at = at == n ? (by > 0 ? 0 : n - 1) : (at + n + (size_t)(by > 0 ? 1 : n - 1)) % n;
  if (!rolltui_theme_editor_palette_color(tc->ed, at, &c)) return;
  rolltui_theme_editor_preview_colour(tc->ed, tc->edit_role, tc->field == THEME_FIELD_BG, &c);
  theme_colour_text(c, &tc->typed);
  tc->typing = 0;
  theme_ctx_push(tc, 0);
}

static int theme_attr_on(RolltuiThemeCtx* tc, size_t a) {
  const RolltuiStyle* s = &rolltui_theme_editor_styles(tc->ed, rolltui_theme_editor_mode(tc->ed), 0)[tc->edit_role];
  const unsigned char on[6] = {s->bold, s->italic, s->underline, s->dim, s->reverse, s->strike};
  return a < 6 && on[a];
}

static int theme_action(const RolltuiBindings* nav, const RolltuiChord* k, const char* scope, const char* want) {
  size_t an = 0;
  const char* a = rolltui_bindings_action_for(nav, k, scope, strlen(scope), &an);
  return a && an == strlen(want) && memcmp(a, want, an) == 0;
}

static int theme_edit_handle(RolltuiThemeCtx* tc, const RolltuiEvent* e) {
  const RolltuiBindings* nav = rolltui_windows_bindings(tc->w);
  const size_t fields = THEME_FIELD_ATTR + rolltui_theme_editor_attr_count();
  const RolltuiChord* k = &e->key;
  if (e->kind != ROLLTUI_EVENT_KEY) return 1;
  rolltui_str_clear(&tc->hint);
  if (k->key == ROLLTUI_KEY_ENTER && !k->ctrl && !k->alt) {
    theme_edit_end(tc, 1);
    return 1;
  }
  if (k->key == ROLLTUI_KEY_LEFT || k->key == ROLLTUI_KEY_RIGHT || k->key == ROLLTUI_KEY_TAB) {
    const int fwd = k->key == ROLLTUI_KEY_RIGHT || (k->key == ROLLTUI_KEY_TAB && !k->shift);
    tc->field = (tc->field + (fwd ? 1 : (int)fields - 1)) % (int)fields;
    tc->typing = 0;
    rolltui_str_clear(&tc->typed);
    return 1;
  }
  if (tc->field >= THEME_FIELD_ATTR) {
    if (k->key == ROLLTUI_KEY_CHAR && k->ch == ' ' && !k->ctrl && !k->alt) {
      const size_t a = (size_t)(tc->field - THEME_FIELD_ATTR);
      rolltui_theme_editor_preview_attr(tc->ed, tc->edit_role, a, !theme_attr_on(tc, a));
      theme_ctx_push(tc, 0);
    }
    return 1;
  }
  if (k->key == ROLLTUI_KEY_UP || k->key == ROLLTUI_KEY_DOWN) {
    theme_field_step(tc, k->key == ROLLTUI_KEY_DOWN ? 1 : -1);
    return 1;
  }
  if (k->key == ROLLTUI_KEY_BACKSPACE) {
    if (!tc->typing) {
      theme_colour_text(theme_field_colour(tc), &tc->typed);
      tc->typing = 1;
    }
    if (tc->typed.n) --tc->typed.n;
    theme_field_typed(tc);
    return 1;
  }
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt && k->ch > ' ' && k->ch < 0x7f) {
    const char ch = (char)k->ch;
    if (!tc->typing) { /* the first key typed replaces the colour, as in a spreadsheet's cell */
      rolltui_str_clear(&tc->typed);
      tc->typing = 1;
    }
    if (tc->typed.n < 32) rolltui_str_append(&tc->typed, &ch, 1);
    theme_field_typed(tc);
    return 1;
  }
  (void)nav;
  return 1; /* every key is the row's while it is being edited */
}

static void theme_sheet_presets(RolltuiThemeCtx* tc) {
  RolltuiPresetListRaw list;
  size_t i;
  memset(&list, 0, sizeof list);
  rolltui_str_list_clear(&tc->preset_names);
  if (tc->store) rolltui_preset_store_list(tc->store, &list);
  for (i = 0; i < list.n; ++i) rolltui_str_list_add(&tc->preset_names, list.v[i].name.p, list.v[i].name.n);
  rolltui_preset_list_release(&list);
  rolltui_list_refresh(tc->presets);
}
static size_t theme_presets_count(void* ctx) { return ((RolltuiThemeCtx*)ctx)->preset_names.n; }
static void theme_presets_fill(void* ctx, size_t i, RolltuiListRow* out) {
  const RolltuiStr* s = &((RolltuiThemeCtx*)ctx)->preset_names.v[i];
  rolltui_str_set(&out->key, s->p ? s->p : "", s->n);
  rolltui_list_row_cell(out, s->p ? s->p : "", s->n);
}

static int theme_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const RolltuiBindings* nav = rolltui_windows_bindings(tc->w);
  RolltuiThemeEditorOutcome o;
  RolltuiListEvent le;
  int handled;
  memset(&o, 0, sizeof o);
  if (tc->sheet_mode == THEME_EDITING) return theme_edit_handle(tc, e);
  if (tc->sheet_mode == THEME_NAMING) {
    const unsigned char r = rolltui_input_handle(tc->name, e, nav, rolltui_input_default_actions(), rolltui_windows_env(tc->w)->now_ms);
    if (r == ROLLTUI_INPUT_SUBMIT) {
      size_t n = 0;
      const char* t = rolltui_input_text(tc->name, &n);
      tc->sheet_mode = THEME_SHEET;
      if (n) {
        o.kind = ROLLTUI_THEME_EDIT_SAVE_AS;
        rolltui_str_set(&o.value, t, n);
        theme_ctx_apply(tc, &o);
      }
    }
    rolltui_theme_editor_outcome_release(&o);
    return 1;
  }
  if (tc->sheet_mode == THEME_CHOOSING) {
    handled = rolltui_list_handle(tc->presets, e, nav, rolltui_list_default_actions());
    memset(&le, 0, sizeof le);
    while (rolltui_list_event(tc->presets, &le))
      if (le.kind == ROLLTUI_LIST_EVENT_ACTIVATE && le.key.n) {
        o.kind = ROLLTUI_THEME_EDIT_LOAD_PRESET;
        rolltui_str_set(&o.value, le.key.p, le.key.n);
        tc->sheet_mode = THEME_SHEET;
        theme_ctx_apply(tc, &o);
        rolltui_theme_editor_outcome_release(&o);
      }
    rolltui_list_event_release(&le);
    return handled || e->kind == ROLLTUI_EVENT_KEY;
  }
  if (e->kind == ROLLTUI_EVENT_KEY && !rolltui_list_typing(tc->list)) {
    const RolltuiChord* k = &e->key;
    rolltui_str_clear(&tc->hint);
    tc->hint_is_problem = 0;
    if (theme_action(nav, k, "sheet", "sheet.undo") || theme_action(nav, k, "sheet", "sheet.redo")) {
      const int undo = theme_action(nav, k, "sheet", "sheet.undo");
      if (undo ? rolltui_theme_editor_undo(tc->ed) : rolltui_theme_editor_redo(tc->ed)) {
        theme_ctx_push(tc, tc->persist);
        if (undo) rolltui_str_set(&tc->hint, K("undone"));
        else rolltui_str_set(&tc->hint, K("redone"));
      } else if (undo) {
        rolltui_str_set(&tc->hint, K("nothing to undo"));
      } else {
        rolltui_str_set(&tc->hint, K("nothing to redo"));
      }
      return 1;
    }
    if (theme_action(nav, k, "sheet", "sheet.load") && tc->show_presets && tc->store) {
      theme_sheet_presets(tc);
      tc->sheet_mode = THEME_CHOOSING;
      return 1;
    }
    if (theme_action(nav, k, "sheet", "sheet.save") && tc->show_presets && tc->store) {
      rolltui_input_set_text(tc->name, "", 0);
      tc->sheet_mode = THEME_NAMING;
      return 1;
    }
    if (theme_action(nav, k, "sheet", "sheet.reset") && tc->store) {
      o.kind = ROLLTUI_THEME_EDIT_RESET_LOADED;
      theme_ctx_apply(tc, &o);
      return 1;
    }
    if (theme_action(nav, k, "theme", "theme.variant")) {
      rolltui_theme_editor_set_mode(tc->ed, rolltui_theme_editor_mode(tc->ed) == ROLLTUI_MODE_LIGHT ? ROLLTUI_MODE_DARK : ROLLTUI_MODE_LIGHT);
      theme_ctx_preview_mode(tc);
      if (rolltui_theme_editor_mode(tc->ed) == ROLLTUI_MODE_LIGHT) rolltui_str_set(&tc->hint, K("editing the light variant"));
      else rolltui_str_set(&tc->hint, K("editing the dark variant"));
      return 1;
    }
    if (theme_action(nav, k, "theme", "theme.generate")) {
      const uint64_t seed = ++tc->seed;
      size_t nl = 0;
      const char* nm;
      char buf[160];
      rolltui_theme_editor_generate(tc->ed, (unsigned char)(seed % ROLLTUI_RULESET_COUNT), seed, 0.0, &o);
      if (o.kind == ROLLTUI_THEME_EDIT_COMMITTED) theme_ctx_push(tc, tc->persist);
      nm = rolltui_theme_editor_variant_name(tc->ed, rolltui_theme_editor_mode(tc->ed), &nl);
      snprintf(buf, sizeof buf, "made %.*s from seed %llu; Ctrl-Z goes back", (int)nl, nm ? nm : "", (unsigned long long)seed);
      rolltui_str_set(&tc->hint, buf, strlen(buf));
      rolltui_theme_editor_outcome_release(&o);
      return 1;
    }
    if (theme_action(nav, k, "theme", "theme.fix")) {
      const ThemeRow* r = theme_sheet_cursor(tc);
      if (r && r->fix >= 0 && rolltui_theme_editor_apply_fix(tc->ed, (size_t)r->fix, &o)) {
        if (o.kind == ROLLTUI_THEME_EDIT_COMMITTED) theme_ctx_push(tc, tc->persist);
        rolltui_str_set(&tc->hint, K("fixed; Ctrl-Z puts it back"));
      } else {
        rolltui_str_set(&tc->hint, K("nothing to fix on this row"));
      }
      rolltui_theme_editor_outcome_release(&o);
      return 1;
    }
  }
  handled = rolltui_list_handle(tc->list, e, nav, rolltui_list_default_actions());
  memset(&le, 0, sizeof le);
  while (rolltui_list_event(tc->list, &le))
    if (le.kind == ROLLTUI_LIST_EVENT_ACTIVATE) {
      const ThemeRow* r = theme_sheet_cursor(tc);
      if (r) theme_edit_begin(tc, r);
      handled = 1;
    }
  rolltui_list_event_release(&le);
  return handled;
}

/* ONE LEVEL BACK: a row being edited drops what it previewed, a load or a save is left, then the list's filter goes; only then is
 * the popup the stack's to close. */
static int theme_ctx_back(void* ctx) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  if (tc->sheet_mode == THEME_EDITING) {
    theme_edit_end(tc, 0);
    return 1;
  }
  if (tc->sheet_mode != THEME_SHEET) {
    tc->sheet_mode = THEME_SHEET;
    return 1;
  }
  rolltui_str_clear(&tc->hint);
  return rolltui_list_back(tc->list);
}

static void theme_sheet_rects(const RolltuiResolvedNode* rn, RolltuiRect* list, int* detail_y, int* hint_y) {
  RolltuiRect r;
  int bottom;
  rolltui_content_rect(rn, &r);
  bottom = r.h >= 6 ? 2 : r.h >= 4 ? 1 : 0;
  *list = r;
  list->h = r.h - bottom;
  *detail_y = bottom == 2 ? r.y + r.h - 2 : -1;
  *hint_y = bottom >= 1 ? r.y + r.h - 1 : -1;
}

static void theme_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(tc->w);
  RolltuiRect lr;
  int dy, hy;
  theme_sheet_build(tc);
  theme_sheet_rects(rn, &lr, &dy, &hy);
  rolltui_list_layout(tc->sheet_mode == THEME_CHOOSING ? tc->presets : tc->list, lr, env->ambiguous_wide);
  if (tc->sheet_mode == THEME_NAMING && hy >= 0) {
    RolltuiRect ir;
    rolltui_content_rect(rn, &ir);
    ir.y = hy, ir.h = 1;
    ir.x += 9, ir.w -= 9; /* after "save as: " */
    rolltui_input_layout(tc->name, ir);
  }
}

static int theme_put(RolltuiThemeCtx* tc, RolltuiFrame* f, int x, int y, const char* text, size_t len, RolltuiStyle st,
                     int max_cells) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(tc->w);
  if (max_cells <= 0) return 0;
  return rolltui_frame_put_text_cut(f, tc->draw, x, y, text, len, st, max_cells, ROLLTUI_CUT_END, env->ambiguous_wide, 0);
}

/* "Enter edit · Ctrl-F fix · …": the sheet's own keys as the table binds them now. */
static void theme_sheet_hints(RolltuiThemeCtx* tc, RolltuiStr* out) {
  const ThemeRow* r = theme_sheet_cursor(tc);
  const char* other = rolltui_theme_editor_mode(tc->ed) == ROLLTUI_MODE_LIGHT ? "dark" : "light";
  const struct { const char* action; const char* say; int show; } hints[] = {
      {"list.activate", "edit", 1},
      {"theme.fix", "fix", r && r->fix >= 0},
      {"sheet.undo", "undo", 1},
      {"theme.variant", other, 1},
      {"theme.generate", "generate", 1},
      {"sheet.load", "load", tc->show_presets && tc->store != NULL},
      {"sheet.save", "save as", tc->show_presets && tc->store != NULL},
      {"sheet.reset", "reset", tc->store != NULL},
      {"list.filter", "find", 1},
  };
  const RolltuiBindings* live = rolltui_windows_bindings(tc->w);
  size_t i;
  rolltui_str_clear(out);
  for (i = 0; i < sizeof hints / sizeof hints[0]; ++i) {
    RolltuiChord k;
    char disp[ROLLTUI_CHORD_STRING_MAX];
    if (!hints[i].show) continue;
    memset(&k, 0, sizeof k);
    if (!rolltui_bindings_chord_at(live, hints[i].action, strlen(hints[i].action), 0, &k)) continue;
    if (out->n) rolltui_str_append(out, K("  "));
    rolltui_str_append(out, disp, rolltui_chord_display(&k, disp, sizeof disp));
    rolltui_str_append(out, K(" "));
    rolltui_str_append(out, hints[i].say, strlen(hints[i].say));
  }
}

/* THE ROW BEING EDITED, drawn over itself: what the role is for in the role, then fg and bg with their swatches, then the
 * attributes, each its letter drawn as the attribute does. The field being edited is in the menu's cursor colours. */
static void theme_edit_draw(RolltuiThemeCtx* tc, RolltuiFrame* f, const RolltuiRect* r, int y, const RolltuiStyle* styles,
                            RolltuiStyle ground) {
  static const char kLetters[6] = {'B', 'I', 'U', 'D', 'R', 'S'};
  const RolltuiMenuRoles* mr = rolltui_windows_menu_roles(tc->w);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(tc->w);
  const RolltuiStyle* s = &rolltui_theme_editor_styles(tc->ed, rolltui_theme_editor_mode(tc->ed), 0)[tc->edit_role];
  RolltuiStyle plain = styles[br->text], cur = styles[mr->selected], muted = styles[br->text_muted], sample = *s;
  const int tight = r->w < 60;          /* a narrow sheet packs the attribute letters together */
  const int strip = tight ? 36 : 42;    /* "fg ▮▮ #rrggbb  bg ▮▮ #rrggbb  B I U D R S", each field 15 cells */
  int x = r->x, wx, i;
  size_t wl = 0;
  const char* words = rolltui_role_words(tc->edit_role, &wl);
  const RolltuiRect band = {r->x, y, r->w, 1};
  plain.bg = ground.bg;
  muted.bg = ground.bg;
  if (sample.bg.kind == 0) sample.bg = ground.bg;
  rolltui_frame_fill(f, tc->draw, band, plain, NULL, 0);
  wx = r->w - strip > 8 ? r->w - strip : 8;
  x += theme_put(tc, f, x, y, words, wl, sample, wx - 1);
  x = r->x + wx;
  for (i = 0; i < 2; ++i) {
    const int focused = tc->field == i;
    const RolltuiStyleColorRaw c = i ? s->bg : s->fg;
    RolltuiStyle sw = plain;
    RolltuiStr* text = &tc->line;
    if (focused && (tc->typing || tc->typed.n)) text = &tc->typed;
    else theme_colour_text(c, &tc->line);
    x += theme_put(tc, f, x, y, i ? "bg " : "fg ", 3, focused ? cur : muted, r->x + r->w - x);
    sw.bg = c;
    if (c.kind != 0) x += theme_put(tc, f, x, y, "  ", 2, sw, r->x + r->w - x);
    x += theme_put(tc, f, x, y, " ", 1, plain, r->x + r->w - x);
    {
      const int tx = x;
      x += theme_put(tc, f, x, y, text->p ? text->p : "", text->n, focused ? cur : plain, r->x + r->w - x);
      if (focused && tc->typing) rolltui_frame_set_cursor(f, tx + (x - tx), y, 1);
    }
    x += theme_put(tc, f, x, y, "  ", 2, plain, r->x + r->w - x);
  }
  for (i = 0; i < 6; ++i) {
    RolltuiStyle a = theme_attr_on(tc, (size_t)i) ? plain : muted;
    if (tc->field == THEME_FIELD_ATTR + i) a = cur;
    a.bold = i == 0, a.italic = i == 1, a.underline = i == 2, a.dim = i == 3, a.reverse = i == 4, a.strike = i == 5;
    x += theme_put(tc, f, x, y, &kLetters[i], 1, a, r->x + r->w - x);
    if (!tight) x += theme_put(tc, f, x, y, " ", 1, plain, r->x + r->w - x);
  }
}

static void theme_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const RolltuiStyle* styles = rolltui_windows_styles(tc->w);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(tc->w);
  const RolltuiWidgetEnv* env = rolltui_windows_env(tc->w);
  RolltuiStyle ground;
  RolltuiListRoles roles;
  RolltuiStyle muted, value;
  RolltuiRect r, lr;
  int dy, hy;
  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  theme_ctx_layout(ctx, rn);
  theme_sheet_rects(rn, &lr, &dy, &hy);
  memset(&roles, 0, sizeof roles);
  roles.text = br->text;
  roles.text_muted = br->text_muted;
  roles.header = br->label;
  roles.cursor = br->cursor;
  roles.cursor_away = br->cursor_away;
  roles.mark = br->mark;
  roles.hover = br->hover;
  roles.prompt = br->prompt;
  roles.heading = rolltui_windows_menu_roles(tc->w)->breadcrumb;
  rolltui_list_draw(tc->sheet_mode == THEME_CHOOSING ? tc->presets : tc->list, f, styles, &roles, rn->focused,
                    rn->hover && tc->sheet_mode != THEME_EDITING ? rn->hover_y : -1, env->ambiguous_wide);
  rolltui_frame_ground(f, &ground);
  if (tc->sheet_mode == THEME_EDITING) {
    const int y = rolltui_list_cursor_y(tc->list);
    if (y >= 0) theme_edit_draw(tc, f, &lr, y, styles, ground);
  }
  muted = styles[br->text_muted];
  value = styles[br->text];
  muted.bg = ground.bg;
  value.bg = ground.bg;
  if (dy >= 0) { /* THE DETAIL LINE: the role as a theme file names it, and what is wrong with it */
    const ThemeRow* cr = theme_sheet_cursor(tc);
    rolltui_str_clear(&tc->line);
    if (tc->sheet_mode == THEME_CHOOSING) {
      rolltui_str_append(&tc->line, K("load a theme: Enter loads, Esc goes back"));
    } else if (tc->sheet_mode == THEME_EDITING) {
      if (tc->field < THEME_FIELD_ATTR) {
        rolltui_str_append(&tc->line, tc->field == THEME_FIELD_FG ? "the text colour" : "the ground colour",
                           tc->field == THEME_FIELD_FG ? 15 : 17);
        rolltui_str_append(&tc->line, K(": type #rrggbb, 0-255 or none"));
      } else {
        size_t al = 0;
        const int on = theme_attr_on(tc, (size_t)(tc->field - THEME_FIELD_ATTR));
        const char* a = rolltui_theme_editor_attr_name((size_t)(tc->field - THEME_FIELD_ATTR), &al);
        rolltui_str_append(&tc->line, a, al);
        if (on) rolltui_str_append(&tc->line, K(": on, Space turns it off"));
        else rolltui_str_append(&tc->line, K(": off, Space turns it on"));
      }
    } else if (cr) {
      rolltui_str_append_str(&tc->line, &cr->key);
      if (cr->attrs.n && rolltui_list_column_width(tc->list, 3) == 0) { /* the attributes column did not fit */
        rolltui_str_append(&tc->line, K("  \xC2\xB7  "));
        rolltui_str_append_str(&tc->line, &cr->attrs);
      }
      if (cr->fix >= 0) {
        const RolltuiFix* fx = rolltui_theme_editor_fix_at(tc->ed, (size_t)cr->fix);
        const char* w = fx && fx->what.p ? fx->what.p : "";
        const char* colon = memchr(w, ':', fx ? fx->what.n : 0);
        RolltuiChord fk;
        char disp[ROLLTUI_CHORD_STRING_MAX];
        rolltui_str_append(&tc->line, K("  \xC2\xB7  "));
        if (colon) rolltui_str_append(&tc->line, colon + 2, (size_t)(w + fx->what.n - colon - 2));
        else if (fx) rolltui_str_append_str(&tc->line, &fx->what);
        memset(&fk, 0, sizeof fk);
        if (rolltui_bindings_chord_at(rolltui_windows_bindings(tc->w), K("theme.fix"), 0, &fk)) {
          rolltui_str_append(&tc->line, K("  \xC2\xB7  "));
          rolltui_str_append(&tc->line, disp, rolltui_chord_display(&fk, disp, sizeof disp));
          rolltui_str_append(&tc->line, K(" fixes it"));
        }
      }
    }
    theme_put(tc, f, r.x, dy, tc->line.p, tc->line.n, muted, r.w);
  }
  if (hy >= 0) { /* THE BOTTOM LINE: what just happened, else the sheet's own keys */
    if (tc->sheet_mode == THEME_NAMING) {
      RolltuiInputRoles iroles;
      theme_put(tc, f, r.x, hy, K("save as: "), styles[br->label], 9);
      iroles.text = br->input_text;
      iroles.selection = br->input_selection;
      iroles.placeholder = br->input_placeholder;
      iroles.line_number = br->text_muted;
      rolltui_input_draw(tc->name, f, tc->draw, styles, &iroles, rn->focused);
    } else {
      rolltui_str_clear(&tc->line);
      if (tc->hint.n) rolltui_str_append_str(&tc->line, &tc->hint);
      else if (tc->sheet_mode == THEME_EDITING && tc->field < THEME_FIELD_ATTR)
        rolltui_str_append(&tc->line, K("Enter keeps  Esc drops  Left Right fields  Up Down the theme's colours"));
      else if (tc->sheet_mode == THEME_EDITING) rolltui_str_append(&tc->line, K("Enter keeps  Esc drops  Left Right fields  Space on, off"));
      else theme_sheet_hints(tc, &tc->line);
      theme_put(tc, f, r.x, hy, tc->line.p, tc->line.n, tc->hint_is_problem ? styles[br->error] : value, r.w);
    }
  }
}

static int theme_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  return axis == ROLLTUI_AXIS_VERTICAL && rolltui_list_scroll_extent(tc->sheet_mode == THEME_CHOOSING ? tc->presets : tc->list, out);
}
static int theme_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  return axis == ROLLTUI_AXIS_VERTICAL && rolltui_list_scroll_to(tc->sheet_mode == THEME_CHOOSING ? tc->presets : tc->list, first);
}

/* THE TITLE: the layout's, else "theme", what the sheet is. */
static int theme_ctx_title(void* ctx, const char* given, size_t given_len, RolltuiStr* out) {
  (void)ctx;
  (void)given;
  if (given_len) return 0;
  rolltui_str_set(out, K("theme"));
  return 1;
}

/* THE FOOTER: which theme, which variant, whether it is changed, and how many problems it has. */
static int theme_ctx_footer(void* ctx, RolltuiStr* out) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const size_t problems = rolltui_theme_editor_fix_count(tc->ed);
  char buf[48];
  rolltui_str_clear(out);
  if (tc->store) rolltui_preset_store_label(tc->store, out);
  if (out->n) rolltui_str_append(out, K(" \xC2\xB7 "));
  rolltui_str_append(out, rolltui_theme_editor_mode(tc->ed) == ROLLTUI_MODE_LIGHT ? "light" : "dark",
                     rolltui_theme_editor_mode(tc->ed) == ROLLTUI_MODE_LIGHT ? 5 : 4);
  if (problems) {
    snprintf(buf, sizeof buf, " \xC2\xB7 %zu problem%s", problems, problems == 1 ? "" : "s");
    rolltui_str_append(out, buf, strlen(buf));
  }
  return 1;
}

/* The sheet's own scope: no counts, since a digit is a colour being typed. */
static int theme_sheet_keys(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "theme", *len = 5;
  return 0;
}

static void theme_ctx_destroy(void* ctx) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  size_t i;
  rolltui_theme_editor_free(tc->ed);
  rolltui_draw_scratch_free(tc->draw);
  rolltui_list_free(tc->list);
  rolltui_list_free(tc->presets);
  rolltui_input_free(tc->name);
  for (i = 0; i < tc->row_init; ++i) {
    rolltui_str_free(&tc->rows[i].key);
    rolltui_str_free(&tc->rows[i].words);
    rolltui_str_free(&tc->rows[i].fg);
    rolltui_str_free(&tc->rows[i].bg);
    rolltui_str_free(&tc->rows[i].attrs);
  }
  rolltui_mem_free(tc->rows);
  rolltui_str_list_release(&tc->preset_names);
  rolltui_str_free(&tc->typed);
  rolltui_str_free(&tc->scratch);
  rolltui_str_free(&tc->line);
  rolltui_str_free(&tc->hint);
  rolltui_mem_free(tc);
}

/* A row being edited or a name being typed takes every key; otherwise the sheet's, the theme's and the list's own. */
static int theme_ctx_takes(void* ctx, const RolltuiChord* k) {
  RolltuiThemeCtx* tc = (RolltuiThemeCtx*)ctx;
  const RolltuiBindings* b = rolltui_windows_bindings(tc->w);
  if (tc->sheet_mode == THEME_EDITING || tc->sheet_mode == THEME_NAMING) return 1;
  return takes_bound(b, k, "theme") || takes_bound(b, k, "sheet") ||
         takes_list(b, tc->sheet_mode == THEME_CHOOSING ? tc->presets : tc->list, k);
}

static const RolltuiWidgetPlugin kThemePlugin = {
    theme_ctx_destroy, theme_ctx_layout, theme_ctx_draw, NULL, NULL,
    NULL,              theme_ctx_handle, theme_ctx_scroll_extent, theme_ctx_scroll_to, theme_ctx_title, theme_ctx_back,
    theme_sheet_keys,
    NULL,
    theme_ctx_footer,
    theme_ctx_takes
};

static RolltuiWidget theme_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  RolltuiThemeCtx* tc;
  RolltuiWidget out;
  RolltuiListOptions lo;
  RolltuiListColumn cols[4];
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
  tc->show_presets = 1;
  tc->list = rolltui_list_new();
  rolltui_list_set_source(tc->list, theme_sheet_count, theme_sheet_fill, tc, NULL);
  memset(&lo, 0, sizeof lo);
  lo.tree = 1;
  lo.headings = 1;
  lo.hide_header = 1;
  lo.scrolloff = 2;
  rolltui_list_set_options(tc->list, &lo);
  memset(cols, 0, sizeof cols);
  rolltui_str_set(&cols[0].title, K("role"));
  rolltui_str_set(&cols[1].title, K("fg"));
  rolltui_str_set(&cols[2].title, K("bg"));
  rolltui_str_set(&cols[3].title, K("attributes"));
  cols[3].max_share = 25;
  cols[3].min_screen = 64; /* narrower, the words keep the room and the detail line names the attributes */
  rolltui_list_set_columns(tc->list, cols, 4);
  rolltui_str_free(&cols[0].title);
  rolltui_str_free(&cols[1].title);
  rolltui_str_free(&cols[2].title);
  rolltui_str_free(&cols[3].title);
  tc->presets = rolltui_list_new();
  rolltui_list_set_source(tc->presets, theme_presets_count, theme_presets_fill, tc, NULL);
  memset(&lo, 0, sizeof lo);
  lo.hide_header = 1;
  rolltui_list_set_options(tc->presets, &lo);
  tc->name = rolltui_input_new();
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

/* THE HINT LINE, SET BY THE HOST. The kind writes this itself for outcomes it handles; a host
 * that DRIVES the editor (rolltui's studio does, because it also saves and lists presets) handles
 * those outcomes instead, and then has no way to say what happened. Without this the kind draws
 * its own stale hint and a host's "saved preset 'mine'" never reaches the screen — which is a
 * silent loss, since the save itself worked. */
void rolltui_windows_set_theme_hint(RolltuiWindows* w, const char* content, size_t len, const char* text,
                                    size_t text_len, int is_problem) {
  RolltuiThemeCtx* tc = theme_ctx_for(w, content, len);
  if (!tc) return;
  rolltui_str_clear(&tc->hint);
  if (text && text_len) rolltui_str_append(&tc->hint, text, text_len);
  tc->hint_is_problem = is_problem ? 1 : 0;
}

/* The editor BEHIND a `theme` window, BORROWED — the window table owns it, and it outlives the
 * popup being closed and reopened, because a widget is keyed by content rather than by whether
 * some popup is currently on the stack.
 *
 * This exists so rolltui's own studio can stop drawing a second theme editor beside the library's.
 * A HOST does not need it: a host names `theme` in a layout, hands over a store, and the kind
 * does the rest. The studio needs the editor OBJECT because it also saves, lists presets and
 * previews the screen it is editing. */
RolltuiThemeEditor* rolltui_windows_theme_editor(RolltuiWindows* w, const char* content, size_t len) {
  RolltuiThemeCtx* tc = theme_ctx_for(w, content, len);
  return tc ? tc->ed : NULL;
}

void rolltui_windows_set_theme_store(RolltuiWindows* w, const char* content, size_t len, RolltuiThemeStore* store,
                                     int persist) {
  RolltuiThemeCtx* tc = theme_ctx_for(w, content, len);
  if (!tc) return;
  /* The widget's own plumbing stays over the generic engine (every rolltui host's theme/keys editor widget runs through it,
   * studio's own theme editor included) — the concrete type is the public boundary's, cast away the moment it crosses. */
  tc->store = (RolltuiPresetStore*)store;
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
  /* THE SHEET (docs/design.md): one list, grouped by scope, read and edited in place. */
  int editable;      /* a store was handed over: edits are kept; without one the sheet is read */
  int is_help;       /* made as `help`: the program's help scopes, or the one `help:<scope>` names */
  int show_presets;  /* load and save as are offered */
  int mode;          /* KEYS_SHEET, KEYS_CHOOSING a preset to load, KEYS_NAMING one to save as */
  RolltuiList* list;
  RolltuiList* presets;
  RolltuiInput* name;
  RolltuiBindings* sent; /* OWNED: the table last committed to the store, or loaded; a commit sends what changed since */
  int rows_stale;        /* the table changed since the rows were built: built again before the next key */
  struct KeysRow* rows;
  size_t row_n, row_cap, row_init; /* rows filled now, allocated, and ever zeroed (each is zeroed the first time it is used) */
  RolltuiStrListRaw scopes, preset_names;
  RolltuiStr scope, scratch;
} RolltuiKeysCtx;

#define KEYS_SHEET 0
#define KEYS_CHOOSING 1
#define KEYS_NAMING 2

/* What the editor holds now is what the store has: the next commit sends what changes after it. */
static void keys_ctx_mark_sent(RolltuiKeysCtx* kc) {
  rolltui_bindings_free(kc->sent);
  kc->sent = rolltui_bindings_clone(rolltui_keys_editor_committed(kc->ed));
}

/* THE BASELINE IS THE LIVE TABLE, AND IT IS RE-TAKEN WHEN THE SCREEN'S ACTIONS ARRIVE. A host
 * declares what its screen can do AFTER it has built its windows, so a widget built during that
 * construction has seen only the library's own actions and could never offer an `app.` one. The
 * count of declared actions is what says a declaration happened; the reload is skipped whenever
 * there is something to lose — a capture in progress or an undoable edit — because taking the
 * table again would throw a person's work away to fix a staleness they cannot see. */
static int keys_ctx_refresh(RolltuiKeysCtx* kc) {
  const RolltuiBindings* live = rolltui_windows_bindings(kc->w);
  const size_t n = rolltui_bindings_action_count(live);
  if (n == kc->declared_seen) return 0;
  if (rolltui_keys_editor_capturing(kc->ed) || rolltui_keys_editor_undo_depth(kc->ed) != 0 ||
      rolltui_keys_editor_redo_depth(kc->ed) != 0)
    return 0;
  kc->declared_seen = n;
  rolltui_keys_editor_load(kc->ed, live);
  keys_ctx_mark_sent(kc);
  kc->rows_stale = 1;
  return 1;
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
  keys_ctx_mark_sent(kc);
  kc->rows_stale = 1;
}

/* The store's preset names into the Load choice. A no-op with no store: the editor still edits
 * the live table, it just has nowhere to put the result. */
static void keys_ctx_sync_store(RolltuiKeysCtx* kc) {
  RolltuiPresetListRaw list;
  RolltuiStrListRaw names;
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

/* THE PERSON'S CHANGES, ONTO THE STORE'S COPY. The editor holds the live table, keys this program suggests for its own actions
 * included; the store's copy is the person's file. A commit makes there only what changed since the last one, so an undo back to
 * the start leaves the copy equal to its preset again, and no suggestion is pinned into the file. */
static void keys_ctx_write_back(RolltuiKeysCtx* kc) {
  const RolltuiBindings* now = rolltui_keys_editor_committed(kc->ed);
  RolltuiBindings* into;
  if (!kc->store) return;
  into = (RolltuiBindings*)rolltui_preset_store_working(kc->store);
  if (into && kc->sent) {
    rolltui_bindings_take_changes(into, kc->sent, now);
  } else {
    if (into) rolltui_preset_store_value_free(kc->store, into);
    into = rolltui_bindings_clone(now);
  }
  rolltui_preset_store_set_working(kc->store, into, kc->persist);
  keys_ctx_mark_sent(kc);
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
        rolltui_preset_store_set_working(kc->store, v, kc->persist); /* the preset itself: the copy is no longer modified */
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


/* ---- THE KEYS SHEET: every key, under the thing it works in, read and changed in one place -----------------------------------
 * docs/design.md: the help a person reads (F1) is the keys editor. One list, grouped under headings by scope, each row what an action
 * does in words and the keys that do it. Enter on a row takes the next key pressed as another key for it, the row's last key goes
 * with `keys.remove`, and undo, load, save as and reset are keys named along the bottom line. A key the terminal cannot send, or an
 * action with none, is marked on its own row. With no bindings store handed over the sheet is read, not edited. */

typedef struct KeysRow {
  unsigned char heading;
  unsigned char role;
  RolltuiStr key;    /* the action; a heading's is "scope:<name>" */
  RolltuiStr what;   /* what it does, in words */
  RolltuiStr chords; /* its keys as they are shown */
  RolltuiStr problem; /* what is wrong with its keys, for the detail line; empty when nothing is */
} KeysRow;

static const struct { const char* scope; const char* title; } kScopeTitles[] = {
    {"app", "This program"},     {"input", "Typing"},        {"list", "Lists and tables"}, {"menu", "Menus"},
    {"picker", "Files"},         {"transcript", "Reading"},  {"stack", "Windows"},         {"edit", "Editing a value"},
    {"sheet", "Editing a sheet"},   {"editor", "The editor"},   {"studio", "The studio"},
};

static const char* scope_title(const char* s, size_t n, size_t* len) {
  size_t i;
  for (i = 0; i < sizeof kScopeTitles / sizeof kScopeTitles[0]; ++i)
    if (strlen(kScopeTitles[i].scope) == n && memcmp(kScopeTitles[i].scope, s, n) == 0) {
      *len = strlen(kScopeTitles[i].title);
      return kScopeTitles[i].title;
    }
  *len = n;
  return s;
}

/* The table the sheet shows: the one being edited, or with nothing to edit into, the one the program runs on. */
static const RolltuiBindings* keys_sheet_table(const RolltuiKeysCtx* kc) {
  return kc->editable ? rolltui_keys_editor_current(kc->ed) : rolltui_windows_bindings(kc->w);
}

static KeysRow* keys_sheet_row(RolltuiKeysCtx* kc) {
  KeysRow* r;
  kc->rows = (KeysRow*)rolltui_grow(kc->rows, &kc->row_cap, kc->row_n + 1, sizeof *kc->rows);
  if (kc->row_n == kc->row_init) memset(&kc->rows[kc->row_init++], 0, sizeof *kc->rows);
  r = &kc->rows[kc->row_n++];
  r->heading = 0;
  r->role = 0;
  rolltui_str_clear(&r->key);
  rolltui_str_clear(&r->what);
  rolltui_str_clear(&r->chords);
  rolltui_str_clear(&r->problem);
  return r;
}

/* The scopes shown, in order: the one a `help:<scope>` window names, else the ones the program lists for help, else every scope its
 * table has, as they first appear. */
static void keys_sheet_scopes(RolltuiKeysCtx* kc, const RolltuiBindings* b, RolltuiStrListRaw* out) {
  size_t i, n;
  rolltui_str_list_clear(out);
  if (kc->scope.n) {
    rolltui_str_list_add(out, kc->scope.p, kc->scope.n);
    return;
  }
  n = rolltui_windows_help_scope_count(kc->w);
  for (i = 0; i < n; ++i) {
    size_t sl = 0;
    const char* s = rolltui_windows_help_scope_at(kc->w, i, &sl);
    rolltui_str_list_add(out, s, sl);
  }
  if (out->n) return;
  n = rolltui_bindings_action_count(b);
  for (i = 0; i < n; ++i) { /* THE PROGRAM'S OWN KEYS FIRST: they are what a person opened this to find */
    size_t al = 0;
    const char* a = rolltui_bindings_action_at(b, i, &al);
    if (al > 4 && memcmp(a, "app.", 4) == 0) {
      rolltui_str_list_add(out, K("app"));
      break;
    }
  }
  for (i = 0; i < n; ++i) {
    size_t al = 0, d = 0, k;
    const char* a = rolltui_bindings_action_at(b, i, &al);
    int seen = 0;
    while (d < al && a[d] != '.') ++d;
    if (d == al) continue;
    for (k = 0; k < out->n && !seen; ++k) seen = out->v[k].n == d && memcmp(out->v[k].p, a, d) == 0;
    if (!seen) rolltui_str_list_add(out, a, d);
  }
}

/* A ROW THAT IS AN ACTION: not a heading ("scope:…") and not a line of the help's text ("note:…"). */
static int keys_row_is_action(const char* key, size_t n) {
  return n && !(n > 6 && memcmp(key, "scope:", 6) == 0) && !(n > 5 && memcmp(key, "note:", 5) == 0);
}

/* An action in words: its description, else its name. */
static const char* keys_words(const RolltuiBindings* b, const char* a, size_t al, size_t* out) {
  const char* d = rolltui_bindings_description(b, a, al, out);
  if (d && *out) return d;
  *out = al;
  return a;
}

static KeysRow* keys_row_of(RolltuiKeysCtx* kc, const char* a, size_t al) {
  size_t i;
  for (i = 0; i < kc->row_n; ++i)
    if (!kc->rows[i].heading && rolltui_str_eq(&kc->rows[i].key, a, al)) return &kc->rows[i];
  return NULL;
}

static int chord_same(const RolltuiChord* x, const RolltuiChord* y) {
  return x->key == y->key && x->ch == y->ch && x->ctrl == y->ctrl && x->alt == y->alt && x->shift == y->shift && x->super == y->super;
}

static void keys_problem_add(KeysRow* r, const char* text, size_t n) {
  if (!r) return;
  if (r->problem.n) rolltui_str_append(&r->problem, K("; "));
  rolltui_str_append(&r->problem, text, n);
}

/* ONE KEY, TWO ACTIONS. A program's own key works everywhere a window with focus does not take it for something of its own; a key
 * two of the program's scopes hold is the program's to settle. Both rows say so, each naming the other, so the person sees it where
 * they would look. An editor sheet's own keys are left out: they apply only while the sheet has focus, which is when a person is
 * using them. */
static void keys_sheet_shadows(RolltuiKeysCtx* kc, const RolltuiBindings* b, unsigned char warning) {
  const size_t n = rolltui_bindings_action_count(b);
  size_t i, j, ci, cj;
  RolltuiStr say;
  memset(&say, 0, sizeof say);
  for (i = 0; i < n; ++i) {
    size_t al = 0;
    const char* a = rolltui_bindings_action_at(b, i, &al);
    const size_t ac = al > 4 && memcmp(a, "app.", 4) == 0 ? rolltui_bindings_chord_count(b, a, al) : 0;
    for (ci = 0; ci < ac; ++ci) {
      RolltuiChord k;
      memset(&k, 0, sizeof k);
      if (!rolltui_bindings_chord_at(b, a, al, ci, &k)) continue;
      for (j = 0; j < n; ++j) {
        size_t ol = 0, oc, d = 0;
        const char* o = rolltui_bindings_action_at(b, j, &ol);
        if (ol > 4 && memcmp(o, "app.", 4) == 0) continue;
        if ((ol > 6 && memcmp(o, "sheet.", 6) == 0) || (ol > 6 && memcmp(o, "theme.", 6) == 0)) continue;
        oc = rolltui_bindings_chord_count(b, o, ol);
        for (cj = 0; cj < oc; ++cj) {
          RolltuiChord ok;
          char disp[ROLLTUI_CHORD_STRING_MAX];
          size_t wl = 0, tl = 0, dl;
          const char* w;
          const char* t;
          KeysRow* ra;
          KeysRow* ro;
          memset(&ok, 0, sizeof ok);
          if (!rolltui_bindings_chord_at(b, o, ol, cj, &ok) || !chord_same(&k, &ok)) continue;
          dl = rolltui_chord_display(&k, disp, sizeof disp);
          while (d < ol && o[d] != '.') ++d;
          t = scope_title(o, d, &tl);
          ra = keys_row_of(kc, a, al);
          ro = keys_row_of(kc, o, ol);
          /* each row names the other: "F3 also: cycle the shipped theme presets (The studio)" */
          w = keys_words(b, o, ol, &wl);
          rolltui_str_set(&say, disp, dl);
          rolltui_str_append(&say, K(" also: "));
          rolltui_str_append(&say, w, wl);
          rolltui_str_append(&say, K(" ("));
          rolltui_str_append(&say, t, tl);
          rolltui_str_append(&say, K(")"));
          keys_problem_add(ra, say.p, say.n);
          if (ra) ra->role = warning;
          w = keys_words(b, a, al, &wl);
          t = scope_title(K("app"), &tl);
          rolltui_str_set(&say, disp, dl);
          rolltui_str_append(&say, K(" also: "));
          rolltui_str_append(&say, w, wl);
          rolltui_str_append(&say, K(" ("));
          rolltui_str_append(&say, t, tl);
          rolltui_str_append(&say, K(")"));
          keys_problem_add(ro, say.p, say.n);
          if (ro) ro->role = warning;
        }
      }
    }
  }
  rolltui_str_free(&say);
}

/* The help's own words as a section: its heading, then a row a line, none of which takes a key. */
static void keys_sheet_text(RolltuiKeysCtx* kc, const char* title, size_t tl, char tag, const char* text, size_t n) {
  size_t at = 0, line = 0;
  KeysRow* h;
  if (!n) return;
  h = keys_sheet_row(kc);
  h->heading = 1;
  rolltui_str_set(&h->key, K("scope:"));
  rolltui_str_append(&h->key, &tag, 1);
  rolltui_str_set(&h->what, title, tl);
  while (at < n) {
    size_t e = at;
    char buf[32];
    KeysRow* r;
    while (e < n && text[e] != '\n') ++e;
    if (e > at) {
      r = keys_sheet_row(kc);
      snprintf(buf, sizeof buf, "note:%c%zu", tag, line++);
      rolltui_str_set(&r->key, buf, strlen(buf));
      rolltui_str_set(&r->what, text + at, e - at);
    }
    at = e + 1;
  }
}

static void keys_sheet_build(RolltuiKeysCtx* kc) {
  const RolltuiBindings* b = keys_sheet_table(kc);
  const unsigned char proto = rolltui_key_active_protocol();
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(kc->w);
  size_t cap_len = 0, i, s;
  const char* capturing = kc->editable && rolltui_keys_editor_capturing(kc->ed) ? rolltui_keys_editor_capturing_action(kc->ed, &cap_len) : NULL;
  kc->rows_stale = 0;
  kc->row_n = 0;
  if (!kc->scope.n) { /* what the program says before its keys */
    size_t ln = 0;
    const char* lead = rolltui_windows_help_lead(kc->w, &ln);
    keys_sheet_text(kc, K("About"), 'a', lead, lead ? ln : 0);
  }
  keys_sheet_scopes(kc, b, &kc->scopes);
  for (s = 0; s < kc->scopes.n; ++s) {
    const RolltuiStr* sc = &kc->scopes.v[s];
    const size_t n = rolltui_bindings_action_count(b);
    size_t tl = 0;
    const char* title = scope_title(sc->p ? sc->p : "", sc->n, &tl);
    KeysRow* h = keys_sheet_row(kc);
    h->heading = 1;
    rolltui_str_set(&h->key, K("scope:"));
    rolltui_str_append(&h->key, sc->p ? sc->p : "", sc->n);
    rolltui_str_set(&h->what, title, tl);
    for (i = 0; i < n; ++i) {
      size_t al = 0, dl = 0, c, chords;
      const char* a = rolltui_bindings_action_at(b, i, &al);
      const char* d;
      KeysRow* r;
      int stuck = 0;
      if (al <= sc->n || memcmp(a, sc->p ? sc->p : "", sc->n) != 0 || a[sc->n] != '.') continue;
      r = keys_sheet_row(kc);
      rolltui_str_set(&r->key, a, al);
      d = rolltui_bindings_description(b, a, al, &dl);
      if (d && dl) rolltui_str_set(&r->what, d, dl);
      else { /* no description: the action's own word, its underscores spaces */
        size_t k;
        rolltui_str_set(&r->what, a + sc->n + 1, al - sc->n - 1);
        for (k = 0; k < r->what.n; ++k)
          if (r->what.p[k] == '_') r->what.p[k] = ' ';
      }
      if (capturing && cap_len == al && memcmp(capturing, a, al) == 0) {
        rolltui_str_set(&r->chords, K("press a key \xE2\x80\xA6"));
        r->role = kc->capture_role;
        continue;
      }
      rolltui_bindings_chords_text(b, a, al, &r->chords);
      chords = rolltui_bindings_chord_count(b, a, al);
      for (c = 0; c < chords && !stuck; ++c) {
        RolltuiChord k;
        memset(&k, 0, sizeof k);
        stuck = rolltui_bindings_chord_at(b, a, al, c, &k) && !rolltui_key_deliverable(&k, proto);
      }
      if (stuck) r->role = br->warning;
      else if (r->chords.n == 0) {
        rolltui_str_set(&r->chords, K("no key"));
        r->role = br->text_muted;
      }
    }
  }
  keys_sheet_shadows(kc, b, br->warning);
  if (!kc->scope.n) { /* and what it says after them */
    size_t nn = 0;
    const char* note = rolltui_windows_help_note(kc->w, &nn);
    keys_sheet_text(kc, K("More"), 'm', note, note ? nn : 0);
  }
  rolltui_list_refresh(kc->list);
}

static size_t keys_sheet_count(void* ctx) { return ((RolltuiKeysCtx*)ctx)->row_n; }
static void keys_sheet_fill(void* ctx, size_t i, RolltuiListRow* out) {
  const KeysRow* r = &((RolltuiKeysCtx*)ctx)->rows[i];
  rolltui_str_set(&out->key, r->key.p ? r->key.p : "", r->key.n);
  rolltui_list_row_cell(out, r->what.p ? r->what.p : "", r->what.n);
  if (!r->heading && keys_row_is_action(r->key.p, r->key.n)) rolltui_list_row_cell(out, r->chords.p ? r->chords.p : "", r->chords.n);
  out->depth = r->heading ? 0 : 1;
  out->role = r->role;
}

/* The action under the cursor, into `kc->scratch`; 0 on a heading or with nothing shown. */
static int keys_sheet_cursor(RolltuiKeysCtx* kc) {
  if (!rolltui_list_cursor(kc->list, &kc->scratch) || kc->scratch.n == 0) return 0;
  return keys_row_is_action(kc->scratch.p, kc->scratch.n);
}

/* "Enter add a key · Del remove · …": each of the sheet's own keys as the table binds it now. */
static void keys_sheet_hints(RolltuiKeysCtx* kc, RolltuiStr* out) {
  static const struct { const char* action; const char* say; } kHints[] = {
      {"list.activate", "add a key"}, {"sheet.remove", "remove"}, {"sheet.undo", "undo"},
      {"sheet.load", "load"},          {"sheet.save", "save as"},  {"sheet.reset", "reset"}, {"list.filter", "find"},
  };
  const RolltuiBindings* live = rolltui_windows_bindings(kc->w);
  size_t i;
  rolltui_str_clear(out);
  for (i = 0; i < sizeof kHints / sizeof kHints[0]; ++i) {
    RolltuiChord k;
    char disp[ROLLTUI_CHORD_STRING_MAX];
    const size_t al = strlen(kHints[i].action);
    if (!kc->editable && i < 6) continue;
    if (!kc->show_presets && (i == 3 || i == 4)) continue;
    memset(&k, 0, sizeof k);
    if (!rolltui_bindings_chord_at(live, kHints[i].action, al, 0, &k)) continue;
    if (out->n) rolltui_str_append(out, K("  "));
    rolltui_str_append(out, disp, rolltui_chord_display(&k, disp, sizeof disp));
    rolltui_str_append(out, K(" "));
    rolltui_str_append(out, kHints[i].say, strlen(kHints[i].say));
  }
  if (!kc->editable) rolltui_str_append(out, K("  \xC2\xB7  this program keeps no changes to its keys"));
}

static void keys_sheet_rects(const RolltuiKeysCtx* kc, const RolltuiResolvedNode* rn, RolltuiRect* list, int* detail_y,
                             int* hint_y) {
  RolltuiRect r;
  int bottom;
  (void)kc;
  rolltui_content_rect(rn, &r);
  /* A SMALL WINDOW GIVES ITS ROWS TO THE KEYS FIRST: the bottom line from four rows, the detail line from six. */
  bottom = r.h >= 6 ? 2 : r.h >= 4 ? 1 : 0;
  *list = r;
  list->h = r.h - bottom;
  *detail_y = bottom == 2 ? r.y + r.h - 2 : -1;
  *hint_y = bottom >= 1 ? r.y + r.h - 1 : -1;
}

static void keys_ctx_layout(void* ctx, const RolltuiResolvedNode* rn) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiWidgetEnv* env = rolltui_windows_env(kc->w);
  RolltuiRect lr;
  int dy, hy;
  keys_ctx_refresh(kc);
  keys_sheet_build(kc);
  keys_sheet_rects(kc, rn, &lr, &dy, &hy);
  rolltui_list_layout(kc->mode == KEYS_CHOOSING ? kc->presets : kc->list, lr, env->ambiguous_wide);
  if (kc->mode == KEYS_NAMING && hy >= 0) {
    RolltuiRect ir;
    rolltui_content_rect(rn, &ir);
    ir.y = hy, ir.h = 1;
    ir.x += 9, ir.w -= 9; /* after "save as: " */
    rolltui_input_layout(kc->name, ir);
  }
}

static int keys_put(RolltuiKeysCtx* kc, RolltuiFrame* f, int x, int y, const char* text, size_t len, RolltuiStyle st,
                    int max_cells) {
  const RolltuiWidgetEnv* env = rolltui_windows_env(kc->w);
  if (max_cells <= 0) return 0;
  return rolltui_frame_put_text_cut(f, kc->draw, x, y, text, len, st, max_cells, ROLLTUI_CUT_END, env->ambiguous_wide, 0);
}

static void keys_ctx_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiStyle* styles = rolltui_windows_styles(kc->w);
  const RolltuiBuiltinRoles* br = rolltui_windows_builtin_roles(kc->w);
  const RolltuiWidgetEnv* env = rolltui_windows_env(kc->w);
  RolltuiListRoles roles;
  RolltuiStyle muted, value, ground;
  RolltuiRect lr, r;
  int dy, hy;
  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;
  keys_ctx_layout(ctx, rn);
  keys_sheet_rects(kc, rn, &lr, &dy, &hy);
  memset(&roles, 0, sizeof roles);
  roles.text = br->text;
  roles.text_muted = br->text_muted;
  roles.header = br->label;
  roles.cursor = br->cursor;
  roles.cursor_away = br->cursor_away;
  roles.mark = br->mark;
  roles.hover = br->hover;
  roles.prompt = br->prompt;
  roles.heading = rolltui_windows_menu_roles(kc->w)->breadcrumb;
  rolltui_list_draw(kc->mode == KEYS_CHOOSING ? kc->presets : kc->list, f, styles, &roles, rn->focused, rn->hover ? rn->hover_y : -1,
                    env->ambiguous_wide);
  rolltui_frame_ground(f, &ground);
  muted = styles[br->text_muted];
  value = styles[br->value];
  muted.bg = ground.bg;
  value.bg = ground.bg;
  if (dy >= 0) { /* THE DETAIL LINE: the row's action as a file names it, and what is wrong with it */
    rolltui_str_clear(&kc->line);
    if (kc->mode == KEYS_CHOOSING) {
      rolltui_str_append(&kc->line, K("load a set of keys: Enter loads, Esc goes back"));
    } else if (keys_sheet_cursor(kc)) {
      const RolltuiBindings* b = keys_sheet_table(kc);
      const size_t n = rolltui_bindings_chord_count(b, kc->scratch.p, kc->scratch.n);
      size_t c;
      rolltui_str_append_str(&kc->line, &kc->scratch);
      for (c = 0; c < kc->row_n; ++c) { /* a row cut short is read here in full, and what is wrong with its keys */
        const KeysRow* kr = &kc->rows[c];
        if (kr->heading || !rolltui_str_eq(&kr->key, kc->scratch.p, kc->scratch.n)) continue;
        if (rolltui_frame_text_width(kc->draw, kr->what.p ? kr->what.p : "", kr->what.n, rolltui_windows_env(kc->w)->ambiguous_wide) >
            rolltui_list_column_width(kc->list, 0)) {
          rolltui_str_append(&kc->line, K(": "));
          rolltui_str_append_str(&kc->line, &kr->what);
        }
        if (kr->problem.n) {
          rolltui_str_append(&kc->line, K("  \xC2\xB7  "));
          rolltui_str_append_str(&kc->line, &kr->problem);
        }
        break;
      }
      for (c = 0; c < n; ++c) {
        RolltuiChord k;
        char disp[ROLLTUI_CHORD_STRING_MAX];
        memset(&k, 0, sizeof k);
        if (!rolltui_bindings_chord_at(b, kc->scratch.p, kc->scratch.n, c, &k) || rolltui_key_deliverable(&k, rolltui_key_active_protocol()))
          continue;
        rolltui_str_append(&kc->line, K("  \xC2\xB7  "));
        rolltui_str_append(&kc->line, disp, rolltui_chord_display(&k, disp, sizeof disp));
        rolltui_str_append(&kc->line, K(" cannot be sent by this terminal"));
      }
    } else if (kc->scratch.n > 5 && memcmp(kc->scratch.p, "note:", 5) == 0) { /* a line of the help's text: in full, if cut */
      size_t c;
      for (c = 0; c < kc->row_n; ++c) {
        const KeysRow* kr = &kc->rows[c];
        if (!rolltui_str_eq(&kr->key, kc->scratch.p, kc->scratch.n)) continue;
        if (rolltui_frame_text_width(kc->draw, kr->what.p ? kr->what.p : "", kr->what.n, rolltui_windows_env(kc->w)->ambiguous_wide) >
            rolltui_list_column_width(kc->list, 0))
          rolltui_str_append_str(&kc->line, &kr->what);
        break;
      }
    }
    keys_put(kc, f, r.x, dy, kc->line.p, kc->line.n, muted, r.w);
  }
  if (hy >= 0) { /* THE BOTTOM LINE: what just happened, else the sheet's own keys */
    if (kc->mode == KEYS_NAMING) {
      RolltuiInputRoles iroles;
      keys_put(kc, f, r.x, hy, K("save as: "), styles[br->label], 9);
      iroles.text = br->input_text;
      iroles.selection = br->input_selection;
      iroles.placeholder = br->input_placeholder;
      iroles.line_number = br->text_muted;
      rolltui_input_draw(kc->name, f, kc->draw, styles, &iroles, rn->focused);
    } else {
      rolltui_str_clear(&kc->line);
      if (kc->editable && rolltui_keys_editor_capturing(kc->ed)) {
        rolltui_str_append(&kc->line, K("press the key to add; Esc cancels"));
      } else if (kc->hint.n) {
        rolltui_str_append_str(&kc->line, &kc->hint);
      } else {
        keys_sheet_hints(kc, &kc->line);
      }
      keys_put(kc, f, r.x, hy, kc->line.p, kc->line.n,
               kc->hint_is_problem ? styles[br->error] : (kc->editable && rolltui_keys_editor_capturing(kc->ed)) ? styles[kc->capture_role]
                                                                                                                 : value,
               r.w);
    }
  }
}

/* What the editor says after a change, as the bottom line's hint: a binding moved, a key refused, an undo. */
static void keys_sheet_said(RolltuiKeysCtx* kc) {
  RolltuiStr s;
  memset(&s, 0, sizeof s);
  rolltui_keys_editor_status_line(kc->ed, &s);
  {
    /* the status ends " · undo N · redo M": the hint keeps what happened */
    const char* cut = s.p ? strstr(s.p, " \xC2\xB7 undo ") : NULL;
    if (cut) s.n = (size_t)(cut - s.p);
  }
  if (s.n) rolltui_str_set(&kc->hint, s.p, s.n);
  kc->hint_is_problem = s.n > 8 && memcmp(s.p, "refused:", 8) == 0;
  rolltui_str_free(&s);
}

static void keys_sheet_done(RolltuiKeysCtx* kc, RolltuiKeysEditorOutcome* o) {
  keys_ctx_apply(kc, o); /* writes a commit back to the store; sets a hint of its own for a load, a save or a reset */
  kc->rows_stale = 1;
  if (kc->hint.n == 0) keys_sheet_said(kc);
  rolltui_keys_editor_outcome_release(o);
}

static void keys_sheet_presets(RolltuiKeysCtx* kc) {
  RolltuiPresetListRaw list;
  size_t i;
  rolltui_str_list_clear(&kc->preset_names);
  if (!kc->store) return;
  memset(&list, 0, sizeof list);
  rolltui_preset_store_list(kc->store, &list);
  for (i = 0; i < list.n; ++i) rolltui_str_list_add(&kc->preset_names, list.v[i].name.p, list.v[i].name.n);
  rolltui_preset_list_release(&list);
  rolltui_list_refresh(kc->presets);
}
static size_t keys_presets_count(void* ctx) { return ((RolltuiKeysCtx*)ctx)->preset_names.n; }
static void keys_presets_fill(void* ctx, size_t i, RolltuiListRow* out) {
  const RolltuiStr* n = &((RolltuiKeysCtx*)ctx)->preset_names.v[i];
  rolltui_str_set(&out->key, n->p ? n->p : "", n->n);
  rolltui_list_row_cell(out, n->p ? n->p : "", n->n);
}

static int keys_ctx_handle(void* ctx, const RolltuiEvent* e) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiBindings* nav = rolltui_windows_bindings(kc->w);
  RolltuiKeysEditorOutcome o;
  RolltuiListEvent le;
  int handled;
  memset(&o, 0, sizeof o);
  keys_ctx_refresh(kc);
  if (kc->rows_stale) keys_sheet_build(kc); /* an action declared since the last frame is a row for this key already */
  if (kc->editable && rolltui_keys_editor_capturing(kc->ed)) { /* EVERY key is the new one while a row waits for it */
    if (e->kind != ROLLTUI_EVENT_KEY) return 1;
    rolltui_str_clear(&kc->hint);
    rolltui_keys_editor_take_key(kc->ed, &e->key, &o);
    keys_sheet_done(kc, &o);
    return 1;
  }
  if (kc->mode == KEYS_NAMING) {
    const unsigned char r = rolltui_input_handle(kc->name, e, nav, rolltui_input_default_actions(), rolltui_windows_env(kc->w)->now_ms);
    if (r == ROLLTUI_INPUT_SUBMIT) {
      size_t n = 0;
      const char* t = rolltui_input_text(kc->name, &n);
      kc->mode = KEYS_SHEET;
      if (n) {
        o.kind = ROLLTUI_KEYS_EDIT_SAVE_AS;
        rolltui_str_set(&o.value, t, n);
        rolltui_str_clear(&kc->hint);
        keys_sheet_done(kc, &o);
      }
    }
    return 1;
  }
  if (kc->mode == KEYS_CHOOSING) {
    handled = rolltui_list_handle(kc->presets, e, nav, rolltui_list_default_actions());
    memset(&le, 0, sizeof le);
    while (rolltui_list_event(kc->presets, &le))
      if (le.kind == ROLLTUI_LIST_EVENT_ACTIVATE && le.key.n) {
        o.kind = ROLLTUI_KEYS_EDIT_LOAD_PRESET;
        rolltui_str_set(&o.value, le.key.p, le.key.n);
        kc->mode = KEYS_SHEET;
        rolltui_str_clear(&kc->hint);
        keys_sheet_done(kc, &o);
      }
    rolltui_list_event_release(&le);
    return handled || e->kind == ROLLTUI_EVENT_KEY;
  }
  if (e->kind == ROLLTUI_EVENT_KEY && !rolltui_list_typing(kc->list)) {
    size_t an = 0;
    const char* a = rolltui_bindings_action_for(nav, &e->key, K("sheet"), &an);
    if (a) {
      rolltui_str_clear(&kc->hint);
      kc->hint_is_problem = 0;
      if (!kc->editable) {
        rolltui_str_set(&kc->hint, K("this program keeps no changes to its keys"));
        return 1;
      }
      if (an == 12 && memcmp(a, "sheet.remove", 12) == 0) {
        if (keys_sheet_cursor(kc)) {
          const RolltuiBindings* b = rolltui_keys_editor_current(kc->ed);
          const size_t n = rolltui_bindings_chord_count(b, kc->scratch.p, kc->scratch.n);
          RolltuiChord k;
          memset(&k, 0, sizeof k);
          if (n && rolltui_bindings_chord_at(b, kc->scratch.p, kc->scratch.n, n - 1, &k)) {
            rolltui_keys_editor_unbind(kc->ed, kc->scratch.p, kc->scratch.n, &k, &o);
            keys_sheet_done(kc, &o);
          } else {
            rolltui_str_set(&kc->hint, K("it has no key to remove"));
          }
        }
        return 1;
      }
      if ((an == 10 && memcmp(a, "sheet.undo", 10) == 0) || (an == 10 && memcmp(a, "sheet.redo", 10) == 0)) {
        const int undo = a[6] == 'u';
        const int did = undo ? rolltui_keys_editor_undo(kc->ed) : rolltui_keys_editor_redo(kc->ed);
        if (did) {
          o.kind = ROLLTUI_KEYS_EDIT_COMMITTED;
          keys_sheet_done(kc, &o);
        } else {
          if (undo) rolltui_str_set(&kc->hint, K("nothing to undo"));
          else rolltui_str_set(&kc->hint, K("nothing to redo"));
        }
        return 1;
      }
      if (an == 10 && memcmp(a, "sheet.load", 10) == 0 && kc->show_presets && kc->store) {
        keys_sheet_presets(kc);
        kc->mode = KEYS_CHOOSING;
        return 1;
      }
      if (an == 10 && memcmp(a, "sheet.save", 10) == 0 && kc->show_presets && kc->store) {
        rolltui_input_set_text(kc->name, "", 0);
        kc->mode = KEYS_NAMING;
        return 1;
      }
      if (an == 11 && memcmp(a, "sheet.reset", 11) == 0 && kc->store) {
        o.kind = ROLLTUI_KEYS_EDIT_RESET_LOADED;
        keys_sheet_done(kc, &o);
        return 1;
      }
    }
  }
  handled = rolltui_list_handle(kc->list, e, nav, rolltui_list_default_actions());
  memset(&le, 0, sizeof le);
  while (rolltui_list_event(kc->list, &le))
    if (le.kind == ROLLTUI_LIST_EVENT_ACTIVATE && keys_row_is_action(le.key.p, le.key.n)) {
      rolltui_str_clear(&kc->hint);
      if (kc->editable) rolltui_keys_editor_capture(kc->ed, le.key.p, le.key.n);
      else rolltui_str_set(&kc->hint, K("this program keeps no changes to its keys"));
      handled = 1;
    }
  rolltui_list_event_release(&le);
  return handled;
}

/* ONE LEVEL BACK: a key being taken is abandoned, a load or a save is left, then the list's filter and marks go; only then is the
 * popup the stack's to close. */
static int keys_ctx_back(void* ctx) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  if (kc->editable && rolltui_keys_editor_capturing(kc->ed)) {
    RolltuiChord esc;
    RolltuiKeysEditorOutcome o;
    memset(&esc, 0, sizeof esc);
    memset(&o, 0, sizeof o);
    esc.key = ROLLTUI_KEY_ESCAPE;
    rolltui_keys_editor_take_key(kc->ed, &esc, &o);
    rolltui_keys_editor_outcome_release(&o);
    rolltui_str_set(&kc->hint, K("no key added"));
    return 1;
  }
  if (kc->mode != KEYS_SHEET) {
    kc->mode = KEYS_SHEET;
    return 1;
  }
  rolltui_str_clear(&kc->hint);
  return rolltui_list_back(kc->list);
}

static int keys_ctx_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  return axis == ROLLTUI_AXIS_VERTICAL && rolltui_list_scroll_extent(kc->mode == KEYS_CHOOSING ? kc->presets : kc->list, out);
}
static int keys_ctx_scroll_to(void* ctx, unsigned char axis, size_t first) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  return axis == ROLLTUI_AXIS_VERTICAL && rolltui_list_scroll_to(kc->mode == KEYS_CHOOSING ? kc->presets : kc->list, first);
}

/* THE FOOTER: where the cursor is, and which set of keys this is ("default (modified)"). */
static int keys_ctx_footer(void* ctx, RolltuiStr* out) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  RolltuiStr at;
  memset(&at, 0, sizeof at);
  rolltui_str_clear(out);
  if (rolltui_list_footer(kc->list, &at)) rolltui_str_append_str(out, &at);
  if (kc->store) {
    if (out->n) rolltui_str_append(out, K(" \xC2\xB7 "));
    rolltui_preset_store_label(kc->store, out);
  }
  rolltui_str_free(&at);
  return out->n != 0;
}

/* `help:<scope>` naming a scope this program does not have is a named problem, as an unbound source is. */
static int keys_ctx_problem(void* ctx, RolltuiStr* out) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiBindings* b = keys_sheet_table(kc);
  size_t i, n;
  if (!kc->scope.n) return 0;
  n = rolltui_bindings_action_count(b);
  for (i = 0; i < n; ++i) {
    size_t al = 0;
    const char* a = rolltui_bindings_action_at(b, i, &al);
    if (al > kc->scope.n && memcmp(a, kc->scope.p, kc->scope.n) == 0 && a[kc->scope.n] == '.') return 0;
  }
  rolltui_str_set(out, K("'"));
  rolltui_str_append_str(out, &kc->scope);
  rolltui_str_append(out, K("' is not one of this program's key scopes"));
  return 1;
}

/* THE TITLE: the layout's, else "keys", what the sheet is. */
static int keys_ctx_title(void* ctx, const char* given, size_t given_len, RolltuiStr* out) {
  (void)ctx;
  if (given_len) return 0;
  (void)given;
  rolltui_str_set(out, K("keys"));
  return 1;
}

/* The sheet's own scope: no counts, since a digit is a key a row may be waiting for. */
static int keys_sheet_keys(void* ctx, const char** scope, size_t* len) {
  (void)ctx;
  *scope = "sheet", *len = 5;
  return 0;
}

static void keys_ctx_destroy(void* ctx) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  size_t i;
  rolltui_keys_editor_free(kc->ed);
  rolltui_bindings_free(kc->sent);
  rolltui_draw_scratch_free(kc->draw);
  rolltui_list_free(kc->list);
  rolltui_list_free(kc->presets);
  rolltui_input_free(kc->name);
  for (i = 0; i < kc->row_init; ++i) {
    rolltui_str_free(&kc->rows[i].key);
    rolltui_str_free(&kc->rows[i].what);
    rolltui_str_free(&kc->rows[i].problem);
    rolltui_str_free(&kc->rows[i].chords);
  }
  rolltui_mem_free(kc->rows);
  rolltui_str_list_release(&kc->scopes);
  rolltui_str_list_release(&kc->preset_names);
  rolltui_str_free(&kc->scope);
  rolltui_str_free(&kc->scratch);
  rolltui_str_free(&kc->line);
  rolltui_str_free(&kc->hint);
  rolltui_mem_free(kc);
}

/* A row waiting for its key, or a name being typed, takes every key; otherwise the sheet's and the list's own. */
static int keys_ctx_takes(void* ctx, const RolltuiChord* k) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)ctx;
  const RolltuiBindings* b = rolltui_windows_bindings(kc->w);
  if ((kc->editable && rolltui_keys_editor_capturing(kc->ed)) || kc->mode == KEYS_NAMING) return 1;
  return takes_bound(b, k, "sheet") || takes_list(b, kc->mode == KEYS_CHOOSING ? kc->presets : kc->list, k);
}

static const RolltuiWidgetPlugin kKeysPlugin = {
    keys_ctx_destroy, keys_ctx_layout, keys_ctx_draw, keys_ctx_problem, NULL,
    NULL,             keys_ctx_handle, keys_ctx_scroll_extent, keys_ctx_scroll_to, keys_ctx_title, keys_ctx_back,
    keys_sheet_keys,
    NULL,
    keys_ctx_footer,
    keys_ctx_takes
};

/* ONE SHEET FOR BOTH KINDS: `keys` edits, and `help` is the same sheet, editable too when this program handed a store over for its
 * keys, and limited to one scope when it names one. */
static RolltuiKeysCtx* keys_sheet_new(RolltuiWindows* w, const char* scope, size_t scope_len, int is_help) {
  RolltuiKeysCtx* kc = (RolltuiKeysCtx*)rolltui_mem_alloc(sizeof *kc);
  RolltuiListOptions lo;
  RolltuiListColumn cols[2];
  int warning;
  memset(kc, 0, sizeof *kc);
  kc->w = w;
  kc->is_help = is_help;
  /* The LIVE table is the baseline: it is what this app is running on, declarations included. */
  kc->ed = rolltui_keys_editor_new(rolltui_windows_bindings(w));
  keys_ctx_mark_sent(kc);
  kc->rows_stale = 1;
  kc->declared_seen = rolltui_bindings_action_count(rolltui_windows_bindings(w));
  kc->draw = rolltui_draw_scratch_new();
  warning = rolltui_role_from_name(K("warning"));
  kc->capture_role = warning >= 0 ? (unsigned char)warning : rolltui_windows_builtin_roles(w)->value;
  kc->persist = 1;
  kc->show_presets = 1;
  if (scope_len) rolltui_str_set(&kc->scope, scope, scope_len);
  kc->list = rolltui_list_new();
  rolltui_list_set_source(kc->list, keys_sheet_count, keys_sheet_fill, kc, NULL);
  memset(&lo, 0, sizeof lo);
  lo.tree = 1;
  lo.headings = 1;
  lo.hide_header = 1;
  lo.scrolloff = 2;
  rolltui_list_set_options(kc->list, &lo);
  memset(cols, 0, sizeof cols);
  rolltui_str_set(&cols[0].title, K("what it does"));
  rolltui_str_set(&cols[1].title, K("keys"));
  cols[1].role = rolltui_windows_menu_roles(w)->shortcut; /* keys are drawn as a shortcut is, everywhere */
  cols[1].max_share = 40; /* what a key does is the row's subject: a long list of keys is cut first */
  cols[1].keep = 1;       /* …but however narrow the sheet, its keys are shown */
  rolltui_list_set_columns(kc->list, cols, 2);
  rolltui_str_free(&cols[0].title);
  rolltui_str_free(&cols[1].title);
  kc->presets = rolltui_list_new();
  rolltui_list_set_source(kc->presets, keys_presets_count, keys_presets_fill, kc, NULL);
  memset(&lo, 0, sizeof lo);
  lo.hide_header = 1;
  rolltui_list_set_options(kc->presets, &lo);
  kc->name = rolltui_input_new();
  {
    RolltuiInputOptionsRaw o;
    rolltui_input_options_init(&o);
    o.single_line = 1;
    rolltui_str_clear(&o.prompt);
    rolltui_input_set_options(kc->name, &o);
    rolltui_input_options_release(&o);
  }
  {
    int persist = 1;
    RolltuiPresetStore* s = rolltui_windows_keys_store(w, &persist); /* a store handed over for any keys sheet of this screen */
    if (s) kc->store = s, kc->persist = persist;
  }
  kc->editable = kc->store != NULL;
  return kc;
}

static RolltuiWidget keys_sheet_widget(RolltuiWindows* w, const char* scope, size_t scope_len, int is_help) {
  RolltuiWidget out;
  memset(&out, 0, sizeof out);
  out.vt = &kKeysPlugin;
  out.ctx = keys_sheet_new(w, scope, scope_len, is_help);
  return out;
}

static RolltuiWidget keys_widget_factory(void* c, RolltuiWindows* w, const char* content, size_t n) {
  (void)c;
  (void)content;
  (void)n;
  return keys_sheet_widget(w, NULL, 0, 0);
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

void rolltui_windows_set_bindings_store(RolltuiWindows* w, const char* content, size_t len, RolltuiBindingsStore* store,
                                        int persist) {
  RolltuiKeysCtx* kc = keys_ctx_for(w, content, len);
  rolltui_windows_remember_keys_store(w, (RolltuiPresetStore*)store, persist); /* a `help` sheet made later edits into it too */
  if (!kc) return;
  kc->store = (RolltuiPresetStore*)store;
  kc->persist = persist;
  kc->editable = store != NULL;
  keys_ctx_refresh(kc);
  keys_ctx_sync_store(kc);
}

void rolltui_windows_set_keys_editor_show_presets(RolltuiWindows* w, const char* content, size_t len, int show) {
  RolltuiKeysCtx* kc = keys_ctx_for(w, content, len);
  if (!kc) return;
  rolltui_keys_editor_set_show_presets(kc->ed, show);
  kc->show_presets = show;
}


void rolltui_menu_widget_ctx_refresh(void* ctx) { menu_ctx_refresh((RolltuiMenuCtx*)ctx); }

const char* rolltui_menu_widget_ctx_origin(void* ctx, size_t* len) {
  RolltuiMenuCtx* mc = (RolltuiMenuCtx*)ctx;
  menu_ctx_refresh(mc);
  return rolltui_str_get(&mc->origin, len);
}
