/* rolltui/c/rolltui_layout.c — the C side of placement, composition and the stack. See
 * rolltui_layout.h; the rules are rolltui/Layout.hpp's. */
#include "rolltui/c/rolltui_os.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_str.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_context.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_terminal.h"
#include "rolltui/c/rolltui_termfacts.h"
#include "rolltui/nsplit.h"
#include "rolltui/rolltui.h"
#include "rolltui/testkit/testctl.h"


static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- placement --------------------------------------------------------------------------- */

int rolltui_resolve_dim(RolltuiDim d, int extent) {
  return (int)floor(d.fraction * extent + 1e-6) + d.cells;
}

/* The alignment an anchor implies on each axis. Two tables rather than two switches: the
 * anchor ordinals are a 3x3 grid, and saying so is shorter than nine cases. */
static int align_h(unsigned char a) { return (int)(a % 3); } /* 0 start, 1 centre, 2 end */
static int align_v(unsigned char a) { return (int)(a / 3); }

/* One axis of resolve(): {start, size} relative to the parent. `min`/`max` must land BEFORE a
 * centre or end alignment turns `len` into `start`: a popup that grows past its natural size to
 * fit its content (`min_w`) still has to centre on the GROWN width, not the width it grew from —
 * center on the pre-growth len and only the growth's own half shows up on one side, off-centre by
 * exactly what it grew. */
static void resolve_axis(RolltuiDim pos, RolltuiDim size, int align, const RolltuiOptDim* min,
                         const RolltuiOptDim* max, int clamp, int margin, int extent, int* out_start,
                         int* out_len) {
  int start = 0, len;
  if (align == 0) {
    RolltuiDim sum;
    sum.fraction = pos.fraction + size.fraction;
    sum.cells = pos.cells + size.cells;
    start = rolltui_resolve_dim(pos, extent);
    len = rolltui_resolve_dim(sum, extent) - start;
  } else {
    len = rolltui_resolve_dim(size, extent);
  }
  if (len < 0) len = 0;
  if (max->present) len = imin(len, imax(rolltui_resolve_dim(max->d, extent), 0));
  if (min->present) len = imax(len, rolltui_resolve_dim(min->d, extent));
  if (align != 0) {
    const int point = rolltui_resolve_dim(pos, extent);
    start = (align == 1) ? point - len / 2 : point - len;
  }
  if (clamp) {
    /* `margin` shrinks itself on an extent too narrow to hold two of it AND at least one cell
     * between them, rather than push the popup off the edge it exists to stay clear of. */
    const int m = extent > 2 * margin ? margin : 0;
    len = imin(len, imax(extent - 2 * m, 0));
    start = iclamp(start, m, imax(extent - m - len, m));
  }
  *out_start = start;
  *out_len = len;
}

void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out) {
  int x, w, y, h;
  resolve_axis(p->x, p->w, align_h(p->anchor), &p->min_w, &p->max_w, p->clamp, p->edge_margin, parent.w, &x, &w);
  resolve_axis(p->y, p->h, align_v(p->anchor), &p->min_h, &p->max_h, p->clamp, 0, parent.h, &y, &h);
  out->x = parent.x + x;
  out->y = parent.y + y;
  out->w = w;
  out->h = h;
}

void rolltui_inner_rect(RolltuiRect outer, unsigned char border, RolltuiRect* out) {
  if (border == ROLLTUI_BORDER_NONE) {
    *out = outer;
    return;
  }
  out->x = outer.x + 1;
  out->y = outer.y + 1;
  out->w = outer.w - 2;
  out->h = outer.h - 2;
  if (out->w < 0) out->w = 0;
  if (out->h < 0) out->h = 0;
}

static RolltuiRect rect_intersect(RolltuiRect a, RolltuiRect b) {
  RolltuiRect r;
  int out[4];
  rolltui_rect_intersect(a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h, out);
  r.x = out[0];
  r.y = out[1];
  r.w = out[2];
  r.h = out[3];
  return r;
}

static int rect_empty(RolltuiRect r) { return r.w <= 0 || r.h <= 0; }
static int rect_contains(RolltuiRect r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ---- the text forms ---------------------------------------------------------------------- */

static const char* trim_span(const char* s, size_t len, size_t* out_len) {
  size_t b = 0, e = len;
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
  *out_len = e - b;
  return s + b;
}

static int parse_int_span(const char* s, size_t len, int* out) {
  size_t i = 0;
  int neg = 0;
  long v = 0;
  s = trim_span(s, len, &len);
  if (len == 0) return 0;
  if (s[0] == '-' || s[0] == '+') {
    neg = s[0] == '-';
    i = 1;
  }
  if (i >= len) return 0;
  for (; i < len; ++i) {
    if (s[i] < '0' || s[i] > '9') return 0;
    v = v * 10 + (s[i] - '0');
    if (v > 1000000) return 0;
  }
  *out = (int)(neg ? -v : v);
  return 1;
}

int rolltui_parse_dim(const char* text, size_t len, RolltuiDim* out) {
  size_t pct = 0, i, num_len, rest_len;
  const char* num;
  const char* rest;
  char buf[64];
  char* end = NULL;
  double f;
  int cells = 0;
  text = trim_span(text, len, &len);
  for (i = 0; i < len; ++i)
    if (text[i] == '%') break;
  if (i == len) return 0;
  pct = i;
  num = trim_span(text, pct, &num_len);
  if (num_len == 0 || num_len >= sizeof buf) return 0;
  for (i = 0; i < num_len; ++i)
    if (!((num[i] >= '0' && num[i] <= '9') || num[i] == '.' || num[i] == '-' || num[i] == '+')) return 0;
  memcpy(buf, num, num_len);
  buf[num_len] = '\0';
  f = strtod(buf, &end);
  if (end != buf + num_len) return 0;
  rest = trim_span(text + pct + 1, len - pct - 1, &rest_len);
  if (rest_len) {
    int mag;
    if (rest[0] != '+' && rest[0] != '-') return 0;
    if (!parse_int_span(rest + 1, rest_len - 1, &mag) || mag < 0) return 0;
    cells = rest[0] == '-' ? -mag : mag;
  }
  out->fraction = f / 100.0;
  out->cells = cells;
  return 1;
}

size_t rolltui_dim_to_string(RolltuiDim d, char* out, size_t cap) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  size_t n;
  double pct = d.fraction * 100.0;
  if (d.fraction == 0) {
    n = (size_t)snprintf(buf, sizeof buf, "%d", d.cells);
  } else if (fabs(pct - floor(pct + 0.5)) < 1e-9) {
    n = (size_t)snprintf(buf, sizeof buf, "%d%%", (int)floor(pct + 0.5));
  } else {
    n = (size_t)snprintf(buf, sizeof buf, "%g%%", pct);
  }
  if (d.fraction != 0 && d.cells > 0) n += (size_t)snprintf(buf + n, sizeof buf - n, " + %d", d.cells);
  else if (d.fraction != 0 && d.cells < 0) n += (size_t)snprintf(buf + n, sizeof buf - n, " - %d", -d.cells);
  if (n >= cap) n = cap ? cap - 1 : 0;
  if (cap) {
    memcpy(out, buf, n);
    out[n] = '\0';
  }
  return n;
}

int rolltui_parse_split_size(const char* text, size_t len, RolltuiSplitSizeRaw* out) {
  RolltuiDim d;
  text = trim_span(text, len, &len);
  if (len == 4 && memcmp(text, "fill", 4) == 0) {
    out->fill = 1;
    out->weight = 1;
    out->dim.fraction = 0;
    out->dim.cells = 0;
    return 1;
  }
  if (len > 4 && memcmp(text, "fill", 4) == 0) {
    int w;
    if (parse_int_span(text + 4, len - 4, &w) && w >= 1) {
      out->fill = 1;
      out->weight = w;
      out->dim.fraction = 0;
      out->dim.cells = 0;
      return 1;
    }
    return 0;
  }
  if (rolltui_parse_dim(text, len, &d)) {
    out->fill = 0;
    out->weight = 1;
    out->dim = d;
    return 1;
  }
  return 0;
}

int rolltui_parse_size_text(const char* text, size_t len, RolltuiSplitSizeRaw* out) {
  size_t i;
  if (rolltui_parse_split_size(text, len, out)) return 1;
  if (len == 0 || len > 9) return 0;
  for (i = 0; i < len; ++i)
    if (text[i] < '0' || text[i] > '9') return 0;
  out->fill = 0;
  out->weight = 1;
  out->dim.fraction = 0;
  out->dim.cells = 0;
  for (i = 0; i < len; ++i) out->dim.cells = out->dim.cells * 10 + (text[i] - '0');
  return 1;
}

size_t rolltui_split_size_to_string(RolltuiSplitSizeRaw s, char* out, size_t cap) {
  if (!s.fill) return rolltui_dim_to_string(s.dim, out, cap);
  if (s.weight == 1) {
    const size_t n = 4 < cap ? 4 : (cap ? cap - 1 : 0);
    if (cap) {
      memcpy(out, "fill", n);
      out[n] = '\0';
    }
    return n;
  }
  {
    char buf[ROLLTUI_DIM_STRING_MAX];
    size_t n = (size_t)snprintf(buf, sizeof buf, "fill %d", s.weight);
    if (n >= cap) n = cap ? cap - 1 : 0;
    if (cap) {
      memcpy(out, buf, n);
      out[n] = '\0';
    }
    return n;
  }
}

/* ---- the split --------------------------------------------------------------------------- */

/* THE SPLIT ITSELF now runs in `nsplit_resolve` (nsplit.h/.c, repo root) — a dependency-free
 * n-ary space-partitioning engine any project can take as-is, ported line-for-line from what
 * used to be this file's own `place()`. What stays HERE is entirely rolltui's own vocabulary:
 * `kind` (Window/Row/Column, not "axis"), `border` (a drawn STYLE — none/single/rounded/double/
 * heavy — not a numeric inset), screen-clipping and the `layer`/`RolltuiResolvedNode` shape a
 * caller's `emit` actually wants. Translating between the two is `sync_nsplit_tree`'s whole job. */

/* A border STYLE's numeric inset. `nsplit`'s own `seam` is a plain width — it has no notion of
 * "rounded" vs "double" and should not: which glyphs a style draws is `draw_border_impl`'s, not
 * a geometry question. Every drawn style reserves exactly one cell each side, matching
 * `rolltui_inner_rect`'s own boolean-width behaviour exactly (asserted there, not re-asserted
 * here — this is the one other place that fact is load-bearing). */
static unsigned char seam_of(unsigned char border) { return border == ROLLTUI_BORDER_NONE ? 0 : 1; }

/* Rewrites `n->base` (and every reachable descendant's) from this node's OWN, real fields —
 * `base` carries no information of its own, so this is a translation, never a second source of
 * truth. ALIASES `n->children.v` into `base.children` rather than copying it (same array, same
 * pointers, zero allocation): nsplit_resolve only reads, never mutates or frees, what it walks. A
 * WINDOW is a leaf by TAG, not by having zero children — enforced here explicitly (never derived
 * from whatever `n->children` happens to hold) so a hypothetical malformed tree cannot make a
 * window's contents draw as if they were a container's; the JSON loader and the layout editor
 * both already guarantee a window never has children, but this is the one place that matters if
 * that were ever untrue, so it does not merely assume it. */
/* Shown on a screen this big: visible, and the screen at its breakpoint or past it. */
static int shown_at(const RolltuiLayoutNode* n, int sw, int sh) {
  return n->visible && (n->show_width <= 0 || sw >= n->show_width) && (n->show_height <= 0 || sh >= n->show_height);
}

/* `n` is, or holds, `focused`. */
static int holds(const RolltuiLayoutNode* n, const RolltuiLayoutNode* focused) {
  size_t i;
  if (!focused) return 0;
  if (n == focused) return 1;
  for (i = 0; i < n->children.n; ++i)
    if (holds(n->children.v[i], focused)) return 1;
  return 0;
}

static void sync_nsplit_tree_at(RolltuiLayoutNode* n, int sw, int sh, const RolltuiLayoutNode* focused, int weight) {
  size_t i;
  const int grid = n->kind == ROLLTUI_NODE_COLUMN && (n->grid_cols > 0 || n->grid_cell_h > 0);
  n->base.axis = (unsigned char)(grid ? 2 : n->kind == ROLLTUI_NODE_ROW);
  n->base.seam = seam_of(n->border);
  n->base.visible = (unsigned char)shown_at(n, sw, sh);
  n->base.size.fill = n->size.fill;
  n->base.size.weight = weight > 0 ? weight : n->size.weight;
  n->base.size.fraction = n->size.dim.fraction;
  n->base.size.cells = n->size.dim.cells;
  n->base.size.min = n->size_min;
  n->base.size.max = n->size_max;
  n->base.pad = n->padding;
  n->base.gap = n->gap;
  n->base.stack_below = n->stack_below;
  n->base.parent_min_w = n->pane_width;
  n->base.parent_min_h = n->pane_height;
  n->base.cols = n->grid_cols;
  n->base.cell_h = n->grid_cell_h;
  n->base.span_r = n->span_r;
  n->base.span_c = n->span_c;
  if (n->kind == ROLLTUI_NODE_WINDOW) {
    n->base.children = NULL;
    n->base.n = 0;
    n->base.cap = 0;
    return;
  }
  n->base.children = (struct NSplitNode**)n->children.v;
  n->base.n = n->children.n;
  n->base.cap = n->children.cap;
  for (i = 0; i < n->children.n; ++i) /* THE ACCORDION: the child holding the focus takes the split's `focus_weight` */
    sync_nsplit_tree_at(n->children.v[i], sw, sh, focused,
                        n->focus_weight > 0 && n->children.v[i]->size.fill && holds(n->children.v[i], focused) ? n->focus_weight : 0);
}

/* What `nsplit_resolve`'s sink threads through `ctx`: everything `place()`'s own signature used
 * to carry as plain parameters, since `NSplitSink` (nsplit.h) has no room for a caller's own
 * `layer`/screen-clip/final sink — exactly the "a caller does that itself in emit" the header
 * documents. */
typedef struct ResolveCtx {
  RolltuiRect screen;
  size_t layer;
  RolltuiResolvedSink emit;
  void* user_ctx;
} ResolveCtx;

static void resolve_sink(void* ctx, const NSplitNode* node, NSplitRect outer, NSplitRect inner) {
  ResolveCtx* rc = (ResolveCtx*)ctx;
  /* Sound only because `base` is `RolltuiLayoutNode`'s first member (see its declaration): a
   * pointer to a struct and a pointer to its first member are the same address and convert to
   * one another, so this is the exact pointer `sync_nsplit_tree` started from, recovered. */
  const RolltuiLayoutNode* n = (const RolltuiLayoutNode*)node;
  RolltuiResolvedNode rn;
  RolltuiRect router, rinner;
  router.x = outer.x;
  router.y = outer.y;
  router.w = outer.w;
  router.h = outer.h;
  rinner.x = inner.x;
  rinner.y = inner.y;
  rinner.w = inner.w;
  rinner.h = inner.h;
  rn.node = n;
  rn.outer = router;                              /* UNCLIPPED — place() never clipped outer either */
  rn.inner = rect_intersect(rinner, rc->screen);   /* the one thing nsplit correctly does not know */
  rn.focused = 0;
  rn.hover = 0;
  rn.hover_x = 0;
  rn.hover_y = 0;
  rn.layer = rc->layer;
  rc->emit(rc->user_ctx, &rn);
}

static void resolve_tree_focused(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                                 const RolltuiLayoutNode* focused, RolltuiResolvedSink emit, void* ctx);
void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx) {
  resolve_tree_focused(root, box, screen, layer, NULL, emit, ctx);
}

static void resolve_tree_focused(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                                 const RolltuiLayoutNode* focused, RolltuiResolvedSink emit, void* ctx) {
  ResolveCtx rc;
  NSplitRect nbox;
  if (!shown_at(root, screen.w, screen.h)) return;
  sync_nsplit_tree_at((RolltuiLayoutNode*)root, screen.w, screen.h, focused, 0); /* `base` is scratch (see its declaration): rewriting
                                               * it through a tree the caller lent us `const` is not
                                               * a mutation of the node's own logical state. */
  rc.screen = screen;
  rc.layer = layer;
  rc.emit = emit;
  rc.user_ctx = ctx;
  nbox.x = box.x;
  nbox.y = box.y;
  nbox.w = box.w;
  nbox.h = box.h;
  nsplit_resolve(&root->base, nbox, resolve_sink, &rc);
}

/* ---- drawing ----------------------------------------------------------------------------- */

/* Box-drawing arms: U D L R. */
#define ARM_U 1
#define ARM_D 2
#define ARM_L 4
#define ARM_R 8

/* Light glyph for an arm mask (index = mask); "" for 0. */
static const char* const kLight[16] = {
    "",  "\xe2\x95\xb5", "\xe2\x95\xb7", "\xe2\x94\x82", /* -, U, D, UD */
    "\xe2\x95\xb4", "\xe2\x94\x98", "\xe2\x94\x90", "\xe2\x94\xa4", /* L, UL, DL, UDL */
    "\xe2\x95\xb6", "\xe2\x94\x94", "\xe2\x94\x8c", "\xe2\x94\x9c", /* R, UR, DR, UDR */
    "\xe2\x94\x80", "\xe2\x94\xb4", "\xe2\x94\xac", "\xe2\x94\xbc", /* LR, ULR, DLR, UDLR */
};

/* Box-drawing glyphs are East Asian AMBIGUOUS width, so with `ascii` every border set falls
 * back to + - | — the rule is Layout.hpp's. */
static const char* glyph_for(unsigned char b, unsigned char mask, int ascii) {
  if (mask == 0) return "";
  if (ascii) {
    if (mask == (ARM_L | ARM_R) || mask == ARM_L || mask == ARM_R) return "-";
    if (mask == (ARM_U | ARM_D) || mask == ARM_U || mask == ARM_D) return "|";
    return "+";
  }
  if (b == ROLLTUI_BORDER_ROUNDED) {
    switch (mask) {
      case ARM_D | ARM_R: return "\xe2\x95\xad";
      case ARM_D | ARM_L: return "\xe2\x95\xae";
      case ARM_U | ARM_R: return "\xe2\x95\xb0";
      case ARM_U | ARM_L: return "\xe2\x95\xaf";
      default: break;
    }
  }
  if (b == ROLLTUI_BORDER_DOUBLE) {
    switch (mask) {
      case ARM_L | ARM_R: return "\xe2\x95\x90";
      case ARM_U | ARM_D: return "\xe2\x95\x91";
      case ARM_D | ARM_R: return "\xe2\x95\x94";
      case ARM_D | ARM_L: return "\xe2\x95\x97";
      case ARM_U | ARM_R: return "\xe2\x95\x9a";
      case ARM_U | ARM_L: return "\xe2\x95\x9d";
      default: return kLight[mask]; /* a joined double border is not modelled */
    }
  }
  if (b == ROLLTUI_BORDER_HEAVY) {
    switch (mask) {
      case ARM_L | ARM_R: return "\xe2\x94\x81";
      case ARM_U | ARM_D: return "\xe2\x94\x83";
      case ARM_D | ARM_R: return "\xe2\x94\x8f";
      case ARM_D | ARM_L: return "\xe2\x94\x93";
      case ARM_U | ARM_R: return "\xe2\x94\x97";
      case ARM_U | ARM_L: return "\xe2\x94\x9b";
      default: return kLight[mask];
    }
  }
  return kLight[mask];
}

/* The arm mask this window's own border wants at ring cell (x, y) of `o`. */
static unsigned char own_mask(RolltuiRect o, int x, int y) {
  const int left = x == o.x, right = x == o.x + o.w - 1;
  const int top = y == o.y, bottom = y == o.y + o.h - 1;
  if (o.w == 1 && o.h == 1) return 0;
  if (o.w == 1) return (unsigned char)((top ? 0 : ARM_U) | (bottom ? 0 : ARM_D));
  if (o.h == 1) return (unsigned char)((left ? 0 : ARM_L) | (right ? 0 : ARM_R));
  if (top && left) return ARM_D | ARM_R;
  if (top && right) return ARM_D | ARM_L;
  if (bottom && left) return ARM_U | ARM_R;
  if (bottom && right) return ARM_U | ARM_L;
  if (top || bottom) return ARM_L | ARM_R;
  return ARM_U | ARM_D;
}

/* A FOOTER on the bottom edge, at its right, inside the corner: " text ", cut at its start where it does not fit, since its end (a
 * count, a position) is what says most. */
static void draw_footer(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, const char* text, size_t n, RolltuiStyle st,
                        int aw) {
  const int fh = rolltui_frame_height(f);
  const int y = outer.y + outer.h - 1;
  int avail, w;
  if (n == 0 || outer.w < 7 || outer.h < 2 || y < 0 || y >= fh) return;
  avail = outer.w - 4; /* a corner and a cell of the line each side */
  w = rolltui_frame_text_width(draw, text, n, aw);
  if (w > avail) w = avail;
  {
    const int x = outer.x + outer.w - 2 - w - 2;
    rolltui_frame_put_text(f, draw, x, y, " ", 1, st, 1, aw, 0);
    rolltui_frame_put_text_cut(f, draw, x + 1, y, text, n, st, w, ROLLTUI_CUT_START, aw, 0);
    rolltui_frame_put_text(f, draw, x + 1 + w, y, " ", 1, st, 1, aw, 0);
  }
}

/* A BLOCK BORDER's glyph at (x, y) of `outer`: half cells drawn inside the edge (▗▄▖ ▐ ▌ ▝▀▘) or outside it (▛▀▜ ▌ ▐ ▙▄▟). Which
 * edge a cell is on is by its place, since the top and the bottom draw different halves. */
static const char* quadrant_for(unsigned char b, RolltuiRect o, int x, int y, int ascii) {
  const int left = x == o.x, right = x == o.x + o.w - 1, top = y == o.y, bottom = y == o.y + o.h - 1;
  const int in = b == ROLLTUI_BORDER_QUADRANT_INSIDE;
  if (ascii) return (left || right) && (top || bottom) ? "+" : top || bottom ? "-" : "|";
  if (top && left) return in ? "\xe2\x96\x97" : "\xe2\x96\x9b";     /* ▗ ▛ */
  if (top && right) return in ? "\xe2\x96\x96" : "\xe2\x96\x9c";    /* ▖ ▜ */
  if (bottom && left) return in ? "\xe2\x96\x9d" : "\xe2\x96\x99";  /* ▝ ▙ */
  if (bottom && right) return in ? "\xe2\x96\x98" : "\xe2\x96\x9f"; /* ▘ ▟ */
  if (top) return in ? "\xe2\x96\x84" : "\xe2\x96\x80";             /* ▄ ▀ */
  if (bottom) return in ? "\xe2\x96\x80" : "\xe2\x96\x84";          /* ▀ ▄ */
  if (left) return in ? "\xe2\x96\x90" : "\xe2\x96\x8c";            /* ▐ ▌ */
  return in ? "\xe2\x96\x8c" : "\xe2\x96\x90";                      /* ▌ ▐ */
}

static void draw_border_impl(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char b,
                             RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                             int ascii) {
  RolltuiRect clip;
  const int fw = rolltui_frame_width(f), fh = rolltui_frame_height(f);
  RolltuiRect bounds;
  int x, y;
  if (b == ROLLTUI_BORDER_NONE || outer.w <= 0 || outer.h <= 0) return;
  bounds.x = 0;
  bounds.y = 0;
  bounds.w = fw;
  bounds.h = fh;
  clip = rect_intersect(outer, bounds);
  if (rect_empty(clip)) return;
  for (y = clip.y; y < clip.y + clip.h; ++y) {
    for (x = clip.x; x < clip.x + clip.w; ++x) {
      unsigned char m;
      const char* g;
      const int ring = x == outer.x || x == outer.x + outer.w - 1 || y == outer.y || y == outer.y + outer.h - 1;
      if (!ring) continue;
      m = own_mask(outer, x, y);
      g = b == ROLLTUI_BORDER_QUADRANT_INSIDE || b == ROLLTUI_BORDER_QUADRANT_OUTSIDE ? quadrant_for(b, outer, x, y, ascii)
          : b == ROLLTUI_BORDER_HIDDEN                                              ? " "
                                                                                    : glyph_for(b, m, ascii);
      if (!*g) g = " ";
      rolltui_frame_put(f, x, y, g, strlen(g), 1, line, 0);
    }
  }
  /* Title on the top edge, inside the corners. */
  if (title_n && outer.w >= 5 && outer.y >= 0 && outer.y < fh) {
    /* SPACE-PAD IN PLACE, never a built string: " " + title + " " was a `std::string` per
     * bordered window per frame in the C++, and the only reason it existed is that
     * `put_text` takes one run. Three calls take three runs. */
    const int avail = outer.w - 2;
    int used = rolltui_frame_put_text(f, draw, outer.x + 1, outer.y, " ", 1, title_style, avail, ascii, 0);
    used += rolltui_frame_put_text(f, draw, outer.x + 1 + used, outer.y, title, title_n, title_style,
                                   avail - used, ascii, 0);
    rolltui_frame_put_text(f, draw, outer.x + 1 + used, outer.y, " ", 1, title_style, avail - used, ascii, 0);
  }
}

void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border,
                         RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                         int ambiguous_wide) {
  draw_border_impl(f, draw, outer, border, line, title, title_n, title_style, ambiguous_wide);
}

/* ---- tabs ------------------------------------------------------------------------------- */

/* THE STRIP'S LABELS on a tabbed window's top border, from just inside its corner: " title " each, one border cell between. `xs[i]`
 * and `ws[i]` place label i; a label that does not fit, and every one after it, is left out. Returns how many are placed. Drawing and
 * a press on the strip both go through here, so a label is pressed where it is drawn. */
#define TAB_MAX 32
static size_t tab_labels(const RolltuiLayoutNode* n, RolltuiRect outer, RolltuiDrawScratch* draw, int aw, int* xs, int* ws) {
  int x = outer.x + 1;
  const int end = outer.x + outer.w - 1;
  size_t i, k = 0;
  for (i = 0; i < n->tab_count && k < TAB_MAX; ++i) {
    const char* t;
    size_t tl = 0;
    int w;
    rolltui_node_tab_at(n, i, &t, &tl, NULL, NULL);
    w = rolltui_frame_text_width(draw, t, tl, aw) + 2;
    if (x + w > end) break;
    xs[k] = x, ws[k] = w, ++k;
    x += w + 1;
  }
  return k;
}

/* The strip: the shown tab's title in the title's role, bold and underlined so it reads in every theme; the others in the border's. */
static void draw_tabs(RolltuiFrame* f, RolltuiDrawScratch* draw, const RolltuiLayoutNode* n, RolltuiRect outer, RolltuiStyle line,
                      RolltuiStyle title, int aw) {
  int xs[TAB_MAX], ws[TAB_MAX];
  const size_t k = tab_labels(n, outer, draw, aw, xs, ws);
  size_t i;
  for (i = 0; i < k; ++i) {
    const char* t;
    size_t tl = 0;
    RolltuiStyle st = i == n->tab ? title : line;
    rolltui_node_tab_at(n, i, &t, &tl, NULL, NULL);
    rolltui_frame_put_text(f, draw, xs[i], outer.y, " ", 1, st, 1, aw, 0);
    if (i == n->tab) st.bold = 1, st.underline = 1;
    rolltui_frame_put_text(f, draw, xs[i] + 1, outer.y, t, tl, st, ws[i] - 2, aw, 0);
    st.underline = 0;
    rolltui_frame_put_text(f, draw, xs[i] + ws[i] - 1, outer.y, " ", 1, st, 1, aw, 0);
  }
}

/* ---- compose ----------------------------------------------------------------------------- */

/* A resolved-node buffer, GROWING AMORTISED. Declared before the scratch because the
 * scratch holds one — a compose collects ONE layer at a time, and the buffer is the
 * caller's so that a per-frame path never allocates it. */
typedef struct NodeCollect {
  RolltuiResolvedNode* v;
  size_t n, cap;
} NodeCollect;

static void collect_sink(void* ctx, const RolltuiResolvedNode* rn) {
  NodeCollect* c = (NodeCollect*)ctx;
  c->v = (RolltuiResolvedNode*)rolltui_grow(c->v, &c->cap, c->n + 1, sizeof *c->v);
  c->v[c->n++] = *rn;
}

struct RolltuiComposeScratch {
  NodeCollect nodes;        /* ONE layer's resolved nodes, reused per layer */
  RolltuiDrawScratch* draw; /* OWNED: the cluster walk a title needs */
  const RolltuiLayoutNode* numbered[9]; /* the focus layer's windows in focus order, while the stack numbers them */
  size_t numbered_n;
  RolltuiStr title;         /* a numbered title, built here */
  RolltuiStr parts;         /* a title in parts, joined */
  int numbered_sup;         /* the numbers are superscripts */
};

RolltuiComposeScratch* rolltui_compose_scratch_new(void) {
  RolltuiComposeScratch* s = (RolltuiComposeScratch*)rolltui_mem_alloc(sizeof *s);
  memset(s, 0, sizeof *s); /* every field starts empty: a new one cannot be left out */
  s->draw = rolltui_draw_scratch_new();
  return s;
}

void rolltui_compose_scratch_free(RolltuiComposeScratch* s) {
  if (!s) return;
  rolltui_str_free(&s->title);
  rolltui_str_free(&s->parts);
  rolltui_mem_free(s->nodes.v);
  rolltui_draw_scratch_free(s->draw);
  rolltui_mem_free(s);
}

/* A NODE'S TITLE PART `i`: its role, text and action (NULL: not a label). 0 past the last. */
static int title_part(const RolltuiLayoutNode* n, size_t i, unsigned char* role, const char** text, size_t* len, const char** action) {
  const char* p = n->title_parts.p;
  const char* end = p ? p + n->title_parts.n : NULL;
  size_t k;
  if (!p || i >= n->title_part_n) return 0;
  for (k = 0; k < i; ++k) { /* past a role byte, a text and an action, each NUL-ended */
    p += 1;
    p += strlen(p) + 1;
    p += strlen(p) + 1;
    if (p >= end) return 0;
  }
  *role = (unsigned char)p[0];
  *text = p + 1;
  *len = strlen(p + 1);
  p += 1 + *len + 1;
  *action = *p ? p : NULL;
  return 1;
}

/* WHAT GOES BEFORE A TITLE: the window's number (a digit and a space, or a superscript) and a spinner's cell, into `out`. Where the
 * spinner goes, as cells into the title, in `*spin_at` (-1 none). */
static void title_prefix(const RolltuiComposeScratch* scratch, const RolltuiLayoutNode* n, int superscript, int has_text, RolltuiStr* out,
                         int* spin_at) {
  static const char* const kSup[9] = {"\xc2\xb9", "\xc2\xb2", "\xc2\xb3", "\xe2\x81\xb4", "\xe2\x81\xb5",
                                      "\xe2\x81\xb6", "\xe2\x81\xb7", "\xe2\x81\xb8", "\xe2\x81\xb9"};
  size_t k;
  int cells = 0;
  rolltui_str_clear(out);
  *spin_at = -1;
  for (k = 0; k < scratch->numbered_n; ++k) /* THE WINDOW'S NUMBER, which Alt and that digit focuses */
    if (scratch->numbered[k] == n) {
      if (superscript) {
        rolltui_str_append(out, kSup[k], strlen(kSup[k]));
        cells = 1;
      } else {
        char d[2];
        d[0] = (char)('1' + k), d[1] = ' ';
        rolltui_str_append(out, d, has_text || n->busy ? 2 : 1);
        cells = has_text || n->busy ? 2 : 1;
      }
      break;
    }
  if (n->busy) { /* the spinner's cell, then the title */
    *spin_at = cells;
    rolltui_str_append(out, has_text ? "  " : " ", has_text ? 2 : 1);
  }
}

/* WHERE A TITLE'S PARTS FALL on the top edge: each part's first cell and its width, from `x0`, the title's first cell. */
static size_t title_part_cells(const RolltuiLayoutNode* n, RolltuiDrawScratch* draw, int x0, int aw, int* xs, int* ws, size_t cap) {
  size_t i, got = 0;
  int x = x0;
  for (i = 0; i < n->title_part_n && got < cap; ++i) {
    unsigned char role;
    const char *text, *action;
    size_t len;
    if (!title_part(n, i, &role, &text, &len, &action)) break;
    xs[got] = x;
    ws[got] = rolltui_frame_text_width(draw, text, len, aw);
    x += ws[got];
    ++got;
  }
  return got;
}

/* A SUBTITLE on the top edge at its right, inside the corner, after the title's `title_end` cell: cut at its start where it must. */
static void draw_subtitle(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, const RolltuiStr* t, int title_end, RolltuiStyle st,
                          int aw) {
  int avail, w, x;
  if (!t->n || outer.w < 7 || outer.y < 0 || outer.y >= rolltui_frame_height(f)) return;
  avail = outer.x + outer.w - 2 - (title_end + 1) - 2; /* a cell of line after the title, a space each side */
  if (avail < 1) return;
  w = rolltui_frame_text_width(draw, t->p, t->n, aw);
  if (w > avail) w = avail;
  x = outer.x + outer.w - 2 - w - 1;
  rolltui_frame_put_text(f, draw, x - 1, outer.y, " ", 1, st, 1, aw, 0);
  rolltui_frame_put_text_cut(f, draw, x, outer.y, t->p, t->n, st, w, ROLLTUI_CUT_START, aw, 0);
  rolltui_frame_put_text(f, draw, x + w, outer.y, " ", 1, st, 1, aw, 0);
}

void rolltui_compose_layer(RolltuiFrame* f, const RolltuiResolvedNode* nodes, size_t count,
                           const RolltuiStyle* styles, const RolltuiLayoutRoles* roles, RolltuiSlotFn render,
                           void* ctx, int ambiguous_wide, RolltuiComposeScratch* scratch) {
  size_t i;

  /* TWO PASSES: every node's ground and border first, then every window's CONTENT, so a container's ground
   * never lands on a child's content whatever order the nodes came in. */
  for (i = 0; i < count; ++i) {
    const RolltuiResolvedNode* rn = &nodes[i];
    const RolltuiLayoutNode* n = rn->node;
    const int draws = n->kind == ROLLTUI_NODE_WINDOW || n->border != ROLLTUI_BORDER_NONE;
    RolltuiStyle ground, line, title;
    if (!draws) continue;
    ground = styles[n->background];
    rolltui_frame_fill(f, scratch->draw, rn->outer, ground, NULL, 0);
    line = styles[n->border_role ? n->border_role : rn->focused ? roles->border_active : roles->border];
    line.bg = ground.bg;
    title = styles[roles->title];
    title.bg = ground.bg;
    /* A tabbed window's strip in place of a title; else the widget's own title when it has one this frame, else the file's. */
    if (n->tab_count) {
      draw_border_impl(f, scratch->draw, rn->outer, n->border, line, NULL, 0, title, ambiguous_wide);
      if (n->border != ROLLTUI_BORDER_NONE) draw_tabs(f, scratch->draw, n, rn->outer, line, title, ambiguous_wide);
    } else {
      const RolltuiStr* t = n->live_title.n ? &n->live_title : &n->title;
      int spin_at = -1, title_end;
      size_t k;
      const int sup = scratch->numbered_sup && !ambiguous_wide;
      if (n->title_part_n) { /* the parts, joined, as the title; their roles over it below */
        rolltui_str_clear(&scratch->parts);
        for (k = 0; k < n->title_part_n; ++k) {
          unsigned char role;
          const char *text, *action;
          size_t len;
          if (title_part(n, k, &role, &text, &len, &action)) rolltui_str_append(&scratch->parts, text, len);
        }
        t = &scratch->parts;
      }
      title_prefix(scratch, n, sup, t->n != 0, &scratch->title, &spin_at);
      if (n->border == ROLLTUI_BORDER_NONE || rn->outer.w < 7) spin_at = -1;
      if (scratch->title.n) {
        rolltui_str_append_str(&scratch->title, t);
        t = &scratch->title;
      }
      draw_border_impl(f, scratch->draw, rn->outer, n->border, line, t->p, t->n, title, ambiguous_wide);
      if (spin_at >= 0) rolltui_spinner_draw(f, rn->outer.x + 2 + spin_at, rn->outer.y, title, n->busy_since_ms);
      title_end = rn->outer.x + 1 + (t->n ? 1 + rolltui_frame_text_width(scratch->draw, t->p, t->n, ambiguous_wide) : 0);
      if (n->title_part_n && n->border != ROLLTUI_BORDER_NONE) { /* each part in its role, a label underlined */
        int xs[32], ws[32];
        const int x0 = rn->outer.x + 2 + (t == &scratch->title ? rolltui_frame_text_width(scratch->draw, scratch->title.p,
                                                                                            scratch->title.n - scratch->parts.n,
                                                                                            ambiguous_wide)
                                                              : 0);
        const size_t np = title_part_cells(n, scratch->draw, x0, ambiguous_wide, xs, ws, 32);
        for (k = 0; k < np; ++k) {
          unsigned char role;
          const char *text, *action;
          size_t len;
          RolltuiStyle ps;
          RolltuiRect pr;
          int right = rn->outer.x + rn->outer.w - 1;
          if (!title_part(n, k, &role, &text, &len, &action)) break;
          ps = role ? styles[role] : title;
          ps.bg.kind = 0; /* the border's ground stays */
          if (action) ps.underline = ps.underline ? ps.underline : ROLLTUI_UNDERLINE_SINGLE;
          pr.x = xs[k], pr.y = rn->outer.y, pr.w = xs[k] + ws[k] > right ? right - xs[k] : ws[k], pr.h = 1;
          if (pr.w > 0) rolltui_frame_tint(f, pr, ps);
        }
      }
      if (n->subtitle.n && n->border != ROLLTUI_BORDER_NONE) draw_subtitle(f, scratch->draw, rn->outer, &n->subtitle, title_end, title, ambiguous_wide);
    }
    if (n->border != ROLLTUI_BORDER_NONE) { /* the widget's footer this frame, else the file's, in the border's colour */
      const RolltuiStr* ft = n->live_footer.n ? &n->live_footer : &n->footer;
      if (ft->n) draw_footer(f, scratch->draw, rn->outer, ft->p, ft->n, line, ambiguous_wide);
    }
  }
  /* SEPARATORS: a line in each gap of a split that asks for them, the length of the split's inner box */
  for (i = 0; i < count; ++i) {
    const RolltuiResolvedNode* rn = &nodes[i];
    const RolltuiLayoutNode* n = rn->node;
    const RolltuiResolvedNode* prev = NULL;
    size_t k, j;
    RolltuiStyle line;
    if (!n->separators || n->gap == 0 || n->kind == ROLLTUI_NODE_WINDOW) continue;
    line = styles[roles->border];
    {
      RolltuiStyle g;
      rolltui_frame_ground(f, &g);
      line.bg = styles[n->background].bg.kind ? styles[n->background].bg : g.bg;
    }
    for (k = 0; k < n->children.n; ++k)
      for (j = i + 1; j < count; ++j) {
        const RolltuiResolvedNode* c = &nodes[j];
        if (c->node != n->children.v[k]) continue;
        if (prev) {
          const int across = c->outer.x > prev->outer.x; /* side by side: a line down the gap; stacked: along it */
          int p;
          if (across) {
            const int x = prev->outer.x + prev->outer.w + (n->gap - 1) / 2;
            for (p = rn->inner.y; p < rn->inner.y + rn->inner.h; ++p)
              rolltui_frame_put(f, x, p, ambiguous_wide ? "|" : "\xe2\x94\x82", ambiguous_wide ? 1 : 3, 1, line, 0);
          } else {
            const int y = prev->outer.y + prev->outer.h + (n->gap - 1) / 2;
            for (p = rn->inner.x; p < rn->inner.x + rn->inner.w; ++p)
              rolltui_frame_put(f, p, y, ambiguous_wide ? "-" : "\xe2\x94\x80", ambiguous_wide ? 1 : 3, 1, line, 0);
          }
        }
        prev = c;
        break;
      }
  }
  if (!render) return;
  for (i = 0; i < count; ++i)
    if (nodes[i].node->kind == ROLLTUI_NODE_WINDOW && !rect_empty(nodes[i].inner)) {
      /* WHILE A WINDOW DRAWS, ITS GROUND IS ITS OWN: a list's rows or a chart's dots on a panel background stay on it. */
      RolltuiStyle screen;
      rolltui_frame_ground(f, &screen);
      rolltui_frame_set_ground(f, styles[nodes[i].node->background]);
      render(ctx, &nodes[i], f);
      rolltui_frame_set_ground(f, screen);
    }
}

/* ---- the widget-kind registry ------------------------------------------------------------ */

typedef struct KindRow {
  const char* name;
  unsigned char rule;
  unsigned char shape;
  const char* source_is;
} KindRow;

/* THE TABLE. One definition site AND THE ONLY NUMBERING: the names, the source rule, the
 * source shape and what a source means all come from here, and a kind's row IS its position
 * in this array. NOTHING IN ANY LANGUAGE NUMBERS THESE A SECOND TIME — an enum beside this
 * table would be a C table pinned by something outside it (the header's registry section has
 * the decision). */
static const KindRow kKinds[] = {
    {"transcript", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "a document the host binds"},
    {"input", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "the target a submitted line goes to"},
    {"menu", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "a menu file"},
    {"rows", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "a row source the host binds"},
    {"text", ROLLTUI_SOURCE_OPTIONAL, ROLLTUI_SOURCE_SHAPE_TEXT, "the literal text"},
    {"file", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_TEXT, "a path"},
    {"help", ROLLTUI_SOURCE_OPTIONAL, ROLLTUI_SOURCE_SHAPE_NAME, "one key scope, or every one when empty"},
    /* The two editors. FORBIDDEN, not optional: what each edits is handed over by the host as a
     * store, so a source on the window would be a second way to say the same thing and the two
     * could disagree. A screen names `theme` or `keys` and the host hands over a store, or the
     * editor draws with nowhere to commit and says so. */
    {"theme", ROLLTUI_SOURCE_FORBIDDEN, ROLLTUI_SOURCE_SHAPE_NAME, ""},
    {"keys", ROLLTUI_SOURCE_FORBIDDEN, ROLLTUI_SOURCE_SHAPE_NAME, ""},
    /* OPTIONAL and a PATH: where a picker starts is the one thing a screen can usefully say about
     * it, and a screen that says nothing gets the host's answer instead. */
    {"filepicker", ROLLTUI_SOURCE_OPTIONAL, ROLLTUI_SOURCE_SHAPE_TEXT, "the directory it opens in"},
    {"gauge", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "a gauge the host binds"},
    /* FORBIDDEN: the history is the window table's own, which every `messages` window on the screen shows alike. */
    {"messages", ROLLTUI_SOURCE_FORBIDDEN, ROLLTUI_SOURCE_SHAPE_NAME, ""},
    {"output", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "another program's output the host binds"},
    {"image", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_TEXT, "a picture's path, or a picture the host binds"},
    {"list", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "a list the host binds"},
    {"chart", ROLLTUI_SOURCE_REQUIRED, ROLLTUI_SOURCE_SHAPE_NAME, "numbers the host binds"},
};
#define KIND_COUNT (sizeof kKinds / sizeof kKinds[0])

/* the old slot names and `custom:` contents, mapped to their current form. A closed, one-way
 * table; the five composites are why it is a MAP rather than a rule. */
/* RUNG 2: a kind is a SESSION's vocabulary, not one screen's — a host registers once at startup
 * and every `Windows` sharing that context parses layout files the same way. Released by
 * `rolltui_context_free`, by name. */
typedef struct HostKind {
  RolltuiStr name;
  RolltuiStr source_is;
  unsigned char rule;
} HostKind;

/* RUNG 2 IS A CONTEXT'S, NOT THE PROCESS'S. It was four file-scope statics and a
 * shutdown hook; two apps in one process registering different kinds shared one table, and the
 * only reason nothing had noticed is that nothing had ever built two. The type stays private to
 * this file — `rolltui_context.h` knows only the pointer. */
struct RolltuiKindRegistry {
  HostKind* v;
  size_t n, cap;
};

RolltuiKindRegistry* rolltui_kind_registry_new(void) {
  RolltuiKindRegistry* r = (RolltuiKindRegistry*)rolltui_mem_alloc(sizeof *r);
  memset(r, 0, sizeof *r);
  return r;
}

void rolltui_kind_registry_free(RolltuiKindRegistry* r) {
  size_t i;
  if (r == NULL) return;
  for (i = 0; i < r->n; ++i) {
    rolltui_str_free(&r->v[i].name);
    rolltui_str_free(&r->v[i].source_is);
  }
  rolltui_mem_free(r->v);
  rolltui_mem_free(r);
}

/* Created on first use, so a context that registers no kind allocates nothing for one. */
static RolltuiKindRegistry* kinds_of(RolltuiContext* c) {
  if (c->kinds == NULL) c->kinds = rolltui_kind_registry_new();
  return c->kinds;
}

static const HostKind* host_kind(const RolltuiContext* c, const char* name, size_t len) {
  size_t i;
  const RolltuiKindRegistry* r = c ? c->kinds : NULL;
  if (r == NULL) return NULL;
  for (i = 0; i < r->n; ++i)
    if (rolltui_str_eq(&r->v[i].name, name, len)) return &r->v[i];
  return NULL;
}

static int library_kind_index(const char* name, size_t len, size_t* out) {
  size_t i;
  for (i = 0; i < KIND_COUNT; ++i)
    if (strlen(kKinds[i].name) == len && memcmp(kKinds[i].name, name, len) == 0) {
      if (out) *out = i;
      return 1;
    }
  return 0;
}

int rolltui_widget_kind_resolve(const RolltuiContext* c, const char* name, size_t len, size_t* row,
                                unsigned char* rule, const char** source_is, size_t* source_is_len) {
  size_t i;
  const HostKind* h;
  /* THE RESOLUTION ORDER (the header's registry section). Rung 1 first, unconditionally — the
   * guard that survives even if a shadowing registration somehow existed. */
  if (library_kind_index(name, len, &i)) {
    if (row) *row = i;
    if (rule) *rule = kKinds[i].rule;
    if (source_is) *source_is = kKinds[i].source_is;
    if (source_is_len) *source_is_len = strlen(kKinds[i].source_is);
    return ROLLTUI_KIND_LIBRARY;
  }
  h = host_kind(c, name, len);
  if (h) {
    if (row) *row = KIND_COUNT + (size_t)(h - c->kinds->v);
    if (rule) *rule = h->rule;
    if (source_is) *source_is = rolltui_str_get(&h->source_is, source_is_len);
    return ROLLTUI_KIND_HOST;
  }
  return ROLLTUI_KIND_UNKNOWN;
}

/* THE ONE ENUMERATION: library rows first, then the host's. Each accessor answers the
 * out-of-range case itself ("" / REQUIRED / NAME) rather than reading past either table.
 *
 * A NULL CONTEXT MEANS "NO HOST KINDS", the same contract `rolltui_content_parse` states for its
 * own `c` — the layout loader parses a file with no session, so the accessors it reaches through
 * must answer for one. Found by the explorer segfaulting the moment the loader met an unknown
 * kind and the suggestion loop asked how many kinds there were. */
static size_t host_n(const RolltuiContext* c) { return (c && c->kinds) ? c->kinds->n : 0; }

size_t rolltui_widget_kind_count(const RolltuiContext* c) { return KIND_COUNT + host_n(c); }
size_t rolltui_widget_kind_library_count(void) { return KIND_COUNT; }

const char* rolltui_widget_kind_name(const RolltuiContext* c, size_t row, size_t* len) {
  if (row < KIND_COUNT) {
    if (len) *len = strlen(kKinds[row].name);
    return kKinds[row].name;
  }
  row -= KIND_COUNT;
  if (row < host_n(c)) return rolltui_str_get(&c->kinds->v[row].name, len);
  if (len) *len = 0;
  return "";
}

unsigned char rolltui_widget_kind_rule(const RolltuiContext* c, size_t row) {
  if (row < KIND_COUNT) return kKinds[row].rule;
  row -= KIND_COUNT;
  return row < host_n(c) ? c->kinds->v[row].rule : ROLLTUI_SOURCE_REQUIRED;
}

unsigned char rolltui_widget_kind_source_shape(size_t row) {
  /* A host row's shape is NAME — the header's stated default, not a policy. */
  return row < KIND_COUNT ? kKinds[row].shape : ROLLTUI_SOURCE_SHAPE_NAME;
}

const char* rolltui_widget_kind_source_is(const RolltuiContext* c, size_t row, size_t* len) {
  if (row < KIND_COUNT) {
    if (len) *len = strlen(kKinds[row].source_is);
    return kKinds[row].source_is;
  }
  row -= KIND_COUNT;
  if (row < host_n(c)) return rolltui_str_get(&c->kinds->v[row].source_is, len);
  if (len) *len = 0;
  return "";
}

int rolltui_widget_kind_register(RolltuiContext* c, const char* name, size_t len, unsigned char rule,
                                 const char* source_is, size_t source_is_len) {
  RolltuiKindRegistry* r;
  const HostKind* h;
  size_t i;
  if (len == 0) return ROLLTUI_REGISTER_EMPTY;
  for (i = 0; i < len; ++i)
    if (name[i] == ':') return ROLLTUI_REGISTER_HAS_COLON;
  if (library_kind_index(name, len, NULL)) return ROLLTUI_REGISTER_IS_LIBRARY;
  h = host_kind(c, name, len);
  if (h) return h->rule == rule ? ROLLTUI_REGISTER_OK : ROLLTUI_REGISTER_RULE_DIFFERS;
  r = kinds_of(c);
  r->v = (HostKind*)rolltui_grow_zeroed(r->v, &r->cap, r->n + 1, sizeof *r->v);
  rolltui_str_set(&r->v[r->n].name, name, len);
  rolltui_str_set(&r->v[r->n].source_is, source_is, source_is_len);
  r->v[r->n].rule = rule;
  ++r->n;
  /* NO RELEASER TO REGISTER ANY MORE, and that is the phase's point: the storage belongs to the
   * context, `rolltui_context_free` releases it by name, and the shutdown hook that used to
   * stand in for an owner is gone along with the flag that tracked whether it had been added. */
  return ROLLTUI_REGISTER_OK;
}

void rolltui_widget_kind_clear(RolltuiContext* c) {
  rolltui_kind_registry_free(c->kinds);
  c->kinds = NULL;
}

/* ---- small local helpers shared by content-parsing and the loader below ---------------------
 * `K`/`streq` mirror `rolltui_widget_menu.c`'s own (its own copy, not shared: a static helper
 * has no external linkage, and each is a two-line wrapper, not a strategy worth a header). */
#define K(s) (s), strlen(s)

static int streq(const char* s, size_t slen, const char* lit) {
  size_t litlen = strlen(lit);
  return slen == litlen && (litlen == 0 || memcmp(s, lit, litlen) == 0);
}

/* Appends a literal / a span to a message being built. Every report sentence below is built
 * with these two into a local `RolltuiStr msg = {0};`, then handed to `add_bad`/`add_unknown`/
 * `add_note`, which MOVE it into the report — one allocation per piece appended (GROWING
 * EXACT, `rolltui_str_append`'s own strategy), no fixed-size buffer to truncate a long content
 * string or file path against. */
static void app(RolltuiStr* s, const char* lit) { rolltui_str_append(s, lit, strlen(lit)); }
static void appn(RolltuiStr* s, const char* p, size_t n) { rolltui_str_append(s, p, n); }

/* ---- content: parsing and formatting, as C ----------------------------------------------- */

int rolltui_content_parse(const RolltuiContext* c, const char* text, size_t len, size_t* row, int* is_host,
                          const char** name, size_t* name_len, const char** source, size_t* source_len,
                          unsigned char* problem, RolltuiStr* why) {
  size_t colon = len, i;
  unsigned char rule = ROLLTUI_SOURCE_REQUIRED;
  const char* describes = "";
  size_t describes_n = 0;
  int rung;

  if (why) rolltui_str_clear(why);
  if (problem) *problem = ROLLTUI_CONTENT_PROBLEM_NONE;
  for (i = 0; i < len; ++i)
    if (text[i] == ':') {
      colon = i;
      break;
    }
  if (name) *name = text;
  if (name_len) *name_len = colon;

  /* A NULL CONTEXT IS A CONTEXT WITH NO HOST KINDS — rung 1 still answers, rung 2 is empty. It is
   * the LOADER's case rather than a defensive allowance: a layout FILE is parsed before any host
   * has registered anything, so what it can be judged against is exactly the library's closed
   * table, and every host-shaped question waits for `rolltui_windows_sync`, where a context is.
   * This is contract point 3 (a layout is plain data, portable between contexts) falling out of
   * the signature, and it is STRICTER than resolving against a process-wide registry was: whether
   * `modal:x` was a bad value used to depend on whether the host had registered `modal` yet. */
  rung = rolltui_widget_kind_resolve(c, text, colon, row, &rule, &describes, &describes_n);
  if (rung == ROLLTUI_KIND_LIBRARY) {
    if (is_host) *is_host = 0;
  } else if (rung == ROLLTUI_KIND_HOST) {
    if (is_host) *is_host = 1;
  } else {
    /* UnknownKind: neither rung. THE RESOLUTION ORDER is the C's (rolltui_widget_kind_
     * resolve already walked it); this is rung 3 failing with a named reason, which is the
     * half that has to be in a language with sentences. */
    if (problem) *problem = ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND;
    if (source) *source = text + len;
    if (source_len) *source_len = 0;
    if (why) {
      {
        size_t k, kc = rolltui_widget_kind_count(c);
        app(why, "'");
        appn(why, text, colon);
        app(why, "' is not a widget kind (");
        for (k = 0; k < kc; ++k) {
          size_t ln = 0;
          const char* kn = rolltui_widget_kind_name(c, k, &ln);
          if (k) app(why, " | ");
          appn(why, kn, ln);
        }
        app(why, ")");
      }
    }
    return 0;
  }

  if (colon < len) {
    if (source) *source = text + colon + 1;
    if (source_len) *source_len = len - colon - 1;
  } else {
    if (source) *source = text + len;
    if (source_len) *source_len = 0;
  }
  {
    const size_t src_len = (colon < len) ? (len - colon - 1) : 0;
    if (rule == ROLLTUI_SOURCE_FORBIDDEN && colon < len) {
      if (problem) *problem = ROLLTUI_CONTENT_PROBLEM_FORBIDDEN_SOURCE;
      if (why) {
        app(why, "'");
        appn(why, text, colon);
        app(why, "' takes no source; write '");
        appn(why, text, colon);
        app(why, "'");
      }
      return 0;
    }
    if (rule == ROLLTUI_SOURCE_REQUIRED && src_len == 0) {
      if (problem) *problem = ROLLTUI_CONTENT_PROBLEM_MISSING_SOURCE;
      if (why) {
        {
          app(why, "'");
          appn(why, text, colon);
          app(why, "' needs a source (");
          appn(why, describes, describes_n);
          app(why, "): write '");
          appn(why, text, colon);
          app(why, ":<name>'");
        }
      }
      return 0;
    }
  }
  return 1;
}

void rolltui_content_format(const char* kind_name, size_t kind_name_len, const char* source, size_t source_len,
                            unsigned char rule, RolltuiStr* out) {
  /* An OPTIONAL source that is empty writes no colon at all: `help` and `text` are then
   * spelled the way every file already spells them, and both forms parse to the same
   * Content. A REQUIRED source that is empty keeps its colon — `transcript:` says out loud
   * that it needs a name. */
  rolltui_str_set(out, kind_name, kind_name_len);
  if (rule == ROLLTUI_SOURCE_REQUIRED || (rule == ROLLTUI_SOURCE_OPTIONAL && source_len != 0)) {
    rolltui_str_append(out, ":", 1);
    rolltui_str_append(out, source, source_len);
  }
}

/* ---- RolltuiContent: the value itself, for a pure C caller (C++ needs none of these — see
 * the header comment) --------------------------------------------------------------------------- */

void rolltui_content_init(RolltuiContent* c) { memset(c, 0, sizeof *c); /* an empty kind names nothing */ }

void rolltui_content_release(RolltuiContent* c) {
  if (!c) return;
  rolltui_str_free(&c->kind);
  rolltui_str_free(&c->source);
  rolltui_content_init(c);
}

void rolltui_content_copy(RolltuiContent* to, const RolltuiContent* from) {
  if (to == from) return;
  rolltui_str_set(&to->kind, from->kind.p, from->kind.n);
  rolltui_str_set(&to->source, from->source.p, from->source.n);
}

int rolltui_content_equal(const RolltuiContent* a, const RolltuiContent* b) {
  return rolltui_str_eq(&a->kind, b->kind.p, b->kind.n) && rolltui_str_eq(&a->source, b->source.p, b->source.n);
}

/* ---- names: anchors and borders — Layout's OWN vocabulary, unlike Role ------------------- */

static const char* const kAnchorNames[9] = {"top-left",     "top",    "top-right", "left",   "center",
                                            "right",        "bottom-left", "bottom", "bottom-right"};
static const char* const kBorderNames[ROLLTUI_BORDER_COUNT] = {"none",   "single",          "rounded",         "double",
                                                              "heavy",  "hidden",          "quadrant-inside", "quadrant-outside"};

const char* rolltui_anchor_name(unsigned char a, size_t* len) {
  if (a >= 9) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = strlen(kAnchorNames[a]);
  return kAnchorNames[a];
}

int rolltui_anchor_from_name(const char* name, size_t len, unsigned char* out) {
  size_t i;
  for (i = 0; i < 9; ++i)
    if (streq(name, len, kAnchorNames[i])) {
      *out = (unsigned char)i;
      return 1;
    }
  return 0;
}

const char* rolltui_border_name(unsigned char b, size_t* len) {
  if (b >= ROLLTUI_BORDER_COUNT) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = strlen(kBorderNames[b]);
  return kBorderNames[b];
}

int rolltui_border_from_name(const char* name, size_t len, unsigned char* out) {
  size_t i;
  for (i = 0; i < ROLLTUI_BORDER_COUNT; ++i)
    if (streq(name, len, kBorderNames[i])) {
      *out = (unsigned char)i;
      return 1;
    }
  return 0;
}

/* ---- the loader: a layout file's JSON, both directions, as C ----------------------------- */

void rolltui_action_decl_problem(const char* name, size_t len, const RolltuiLayoutHooks* hooks, RolltuiStr* out) {
  size_t scope_len = 0;
  const char* scope = rolltui_bindings_scope_of(name, len, &scope_len);
  rolltui_str_clear(out);
  if ((scope_len == len && (len == 0 || memcmp(scope, name, len) == 0)) || scope_len == 0 || len <= scope_len + 1) {
    app(out, "an action is \"<scope>.<verb>\", both parts non-empty");
    return;
  }
  if (hooks && hooks->is_library_scope && hooks->is_library_scope(hooks->scope_ctx, scope, scope_len)) {
    app(out, "the '");
    appn(out, scope, scope_len);
    app(out, "' scope is the library's and cannot be declared");
  }
  /* else: *out is already cleared — empty means "no problem", matching action_decl_problem's
   * "" return. */
}

/* ---- the report -------------------------------------------------------------------------- */

void rolltui_layout_report_release(RolltuiLayoutReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->bad_values_n; ++i) rolltui_str_free(&r->bad_values[i]);
  rolltui_mem_free(r->bad_values);
  for (i = 0; i < r->notes_n; ++i) rolltui_str_free(&r->notes[i]);
  rolltui_mem_free(r->notes);
  memset(r, 0, sizeof *r);
}

int rolltui_layout_report_clean(const RolltuiLayoutReport* r) {
  /* notes are deliberately NOT part of "clean" — LayoutLoadReport::clean()'s own rule: the
   * layout loaded, and a migration is a note rather than a problem. */
  return r->error.n == 0 && r->unknown_keys_n == 0 && r->bad_values_n == 0;
}

/* MOVE variants: take ownership of an already-built message, leaving it empty — every call
 * site below builds one local `RolltuiStr msg` from pieces and hands it straight over, so
 * there is exactly one copy of the bytes rather than a build-then-copy-then-free. GROWING
 * AMORTISED arrays of small owned strings, the same shape `rolltui_bindings.c`'s report
 * and `rolltui_presets.c`'s `NameList` already use. */
static void add_bad(RolltuiLayoutReport* r, RolltuiStr* msg) {
  r->bad_values =
      (RolltuiStr*)rolltui_grow_zeroed(r->bad_values, &r->bad_values_cap, r->bad_values_n + 1, sizeof *r->bad_values);
  rolltui_str_move(&r->bad_values[r->bad_values_n++], msg);
}
static void add_unknown(RolltuiLayoutReport* r, RolltuiStr* msg) {
  r->unknown_keys = (RolltuiStr*)rolltui_grow_zeroed(r->unknown_keys, &r->unknown_keys_cap, r->unknown_keys_n + 1,
                                                     sizeof *r->unknown_keys);
  rolltui_str_move(&r->unknown_keys[r->unknown_keys_n++], msg);
}
static void add_note(RolltuiLayoutReport* r, RolltuiStr* msg) {
  r->notes = (RolltuiStr*)rolltui_grow_zeroed(r->notes, &r->notes_cap, r->notes_n + 1, sizeof *r->notes);
  rolltui_str_move(&r->notes[r->notes_n++], msg);
}
/* The common "<where><literal suffix>" shape — most report sentences below are exactly this
 * (an empty `where` composes a bare literal, reused for the few messages that have no path
 * prefix at all, e.g. "popups: expected an array"). */
static void bad_at(RolltuiLayoutReport* r, const char* where, size_t where_len, const char* suffix) {
  RolltuiStr msg = {0};
  appn(&msg, where, where_len);
  app(&msg, suffix);
  add_bad(r, &msg);
}
static void unknown_at(RolltuiLayoutReport* r, const char* where, size_t where_len) {
  RolltuiStr msg = {0};
  appn(&msg, where, where_len);
  add_unknown(r, &msg);
}

/* ---- RolltuiLoadedLayout: the transient carrier (see the header comment) ----------------- */

void rolltui_loaded_layout_init(RolltuiLoadedLayout* l) {
  memset(l, 0, sizeof *l);
  rolltui_layer_init(&l->base);
}

void rolltui_loaded_layout_release(RolltuiLoadedLayout* l) {
  size_t i;
  if (!l) return;
  rolltui_str_free(&l->name);
  for (i = 0; i < l->actions_n; ++i) {
    rolltui_str_free(&l->actions[i].name);
    rolltui_str_free(&l->actions[i].description);
  }
  rolltui_mem_free(l->actions);
  rolltui_layer_release(&l->base);
  for (i = 0; i < l->popups_n; ++i) rolltui_layer_release(&l->popups[i]);
  rolltui_mem_free(l->popups);
  memset(l, 0, sizeof *l);
}

/* `RolltuiLoadedLayout` and `RolltuiLayout` share `name`/`actions`(list)/`base`/`popups`(list)
 * byte for byte (this file's own header comment on `RolltuiLayout` says so), so every field
 * below is a MOVE, never a copy: `loaded` is about to be released by the caller either way. */
void rolltui_loaded_layout_to_layout(RolltuiLoadedLayout* loaded, RolltuiLayout* out) {
  if (!loaded || !out) return;
  rolltui_str_move(&out->name, &loaded->name);
  out->min_width = loaded->min_width;
  out->min_height = loaded->min_height;
  out->inline_mode = loaded->inline_mode;
  out->actions.v = loaded->actions;
  out->actions.n = loaded->actions_n;
  out->actions.cap = loaded->actions_cap;
  loaded->actions = NULL;
  loaded->actions_n = loaded->actions_cap = 0;
  rolltui_layer_move(&out->base, &loaded->base);
  out->popups.v = loaded->popups;
  out->popups.n = loaded->popups_n;
  out->popups.cap = loaded->popups_cap;
  loaded->popups = NULL;
  loaded->popups_n = loaded->popups_cap = 0;
}

/* ---- RolltuiActionList: an owned array of RolltuiLayoutAction values --------------------- */
/* GROWING AMORTISED, exactly like RolltuiLayerList right beside it: a RolltuiLayoutAction is
 * two RolltuiStrs and nothing else, so it is trivially relocatable the same way. */

RolltuiLayoutAction* rolltui_action_list_add(RolltuiActionList* l) {
  l->v = (RolltuiLayoutAction*)rolltui_grow_zeroed(l->v, &l->cap, l->n + 1, sizeof *l->v);
  return &l->v[l->n++];
}

void rolltui_action_list_remove(RolltuiActionList* l, size_t i) {
  if (i >= l->n) return;
  rolltui_str_free(&l->v[i].name);
  rolltui_str_free(&l->v[i].description);
  memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof *l->v);
  --l->n;
}

void rolltui_action_list_remove_name(RolltuiActionList* l, const char* name, size_t len) {
  size_t i;
  for (i = 0; i < l->n; ++i)
    if (rolltui_str_eq(&l->v[i].name, name, len)) {
      rolltui_action_list_remove(l, i);
      return;
    }
}

void rolltui_action_list_clear(RolltuiActionList* l) {
  size_t i;
  for (i = 0; i < l->n; ++i) {
    rolltui_str_free(&l->v[i].name);
    rolltui_str_free(&l->v[i].description);
  }
  l->n = 0; /* the array stays */
}

void rolltui_action_list_release(RolltuiActionList* l) {
  rolltui_action_list_clear(l);
  rolltui_mem_free(l->v);
  l->v = NULL;
  l->cap = 0;
}

void rolltui_action_list_copy(RolltuiActionList* to, const RolltuiActionList* from) {
  size_t i;
  if (to == from) return;
  rolltui_action_list_clear(to);
  to->v = (RolltuiLayoutAction*)rolltui_grow_zeroed(to->v, &to->cap, from->n, sizeof *to->v);
  for (i = 0; i < from->n; ++i) {
    rolltui_str_set(&to->v[i].name, from->v[i].name.p, from->v[i].name.n);
    rolltui_str_set(&to->v[i].description, from->v[i].description.p, from->v[i].description.n);
  }
  to->n = from->n;
}

int rolltui_action_list_equal(const RolltuiActionList* a, const RolltuiActionList* b) {
  size_t i;
  if (a->n != b->n) return 0;
  for (i = 0; i < a->n; ++i) {
    if (!rolltui_str_eq(&a->v[i].name, b->v[i].name.p, b->v[i].name.n)) return 0;
    if (!rolltui_str_eq(&a->v[i].description, b->v[i].description.p, b->v[i].description.n)) return 0;
  }
  return 1;
}

/* ---- RolltuiLayout: the enduring value (see the header comment) -------------------------- */

void rolltui_layout_init(RolltuiLayout* l) {
  memset(l, 0, sizeof *l);
  rolltui_layer_init(&l->base);
}

void rolltui_layout_release(RolltuiLayout* l) {
  if (!l) return;
  rolltui_str_free(&l->name);
  rolltui_action_list_release(&l->actions);
  rolltui_layer_release(&l->base);
  rolltui_layer_list_release(&l->popups);
  rolltui_layout_init(l);
}

void rolltui_layout_copy(RolltuiLayout* to, const RolltuiLayout* from) {
  if (to == from) return;
  rolltui_str_set(&to->name, from->name.p, from->name.n);
  to->min_width = from->min_width;
  to->min_height = from->min_height;
  to->inline_mode = from->inline_mode;
  rolltui_action_list_copy(&to->actions, &from->actions);
  rolltui_layer_copy(&to->base, &from->base);
  rolltui_layer_list_copy(&to->popups, &from->popups);
}

int rolltui_layout_equal(const RolltuiLayout* a, const RolltuiLayout* b) {
  return rolltui_str_eq(&a->name, b->name.p, b->name.n) && a->min_width == b->min_width &&
         a->min_height == b->min_height && a->inline_mode == b->inline_mode && rolltui_action_list_equal(&a->actions, &b->actions) &&
         rolltui_layer_equal(&a->base, &b->base) && rolltui_layer_list_equal(&a->popups, &b->popups);
}

const RolltuiLayer* rolltui_layout_popup(const RolltuiLayout* l, const char* id, size_t len) {
  size_t i;
  if (!l) return NULL; /* Phase 23: a door takes a handle, and a handle may be NULL — every
                        * other accessor guards, and this one segfaulted a test that passed a
                        * failed load straight in. */
  for (i = 0; i < l->popups.n; ++i)
    if (rolltui_str_eq(&l->popups.v[i].id, id, len)) return &l->popups.v[i];
  return NULL;
}

/* ---- the opaque handle's lifecycle, and the seven doors ---------------------------------- */

RolltuiLayout* rolltui_layout_new(void) {
  RolltuiLayout* l = (RolltuiLayout*)rolltui_mem_alloc(sizeof *l);  /* OWNED, LONG-LIVED */
  memset(l, 0, sizeof *l);
  rolltui_layout_init(l);
  return l;
}

void rolltui_layout_free(RolltuiLayout* l) {
  if (!l) return;
  rolltui_layout_release(l);
  rolltui_mem_free(l);
}

RolltuiLayout* rolltui_layout_clone(const RolltuiLayout* l) {
  RolltuiLayout* out;
  if (!l) return NULL;
  out = rolltui_layout_new();
  rolltui_layout_copy(out, l);
  return out;
}

const RolltuiLayer* rolltui_layout_base(const RolltuiLayout* l) { return l ? &l->base : NULL; }

const RolltuiLayoutAction* rolltui_layout_actions(const RolltuiLayout* l, size_t* n) {
  if (n) *n = l ? l->actions.n : 0;
  return l ? l->actions.v : NULL;
}

int rolltui_layout_inline(const RolltuiLayout* l) { return l && l->inline_mode; }

void rolltui_layout_min_size(const RolltuiLayout* l, int* w, int* h) {
  if (w) *w = l ? l->min_width : 0;
  if (h) *h = l ? l->min_height : 0;
}

const char* rolltui_layout_name(const RolltuiLayout* l, size_t* len) {
  if (len) *len = l ? l->name.n : 0;
  return l && l->name.p ? l->name.p : "";
}

const RolltuiPlacement* rolltui_layer_placement(const RolltuiLayer* layer) { return layer ? &layer->placement : NULL; }

const char* rolltui_layer_id(const RolltuiLayer* layer, size_t* len) {
  if (len) *len = layer ? layer->id.n : 0;
  return layer && layer->id.p ? layer->id.p : "";
}
const char* rolltui_layer_focus(const RolltuiLayer* layer, size_t* len) {
  if (!layer) { if (len) *len = 0; return ""; }
  return rolltui_str_get(&layer->focus, len);
}

const char* rolltui_layout_node_id(const RolltuiLayoutNode* n, size_t* len) {
  if (len) *len = n ? n->id.n : 0;
  return n && n->id.p ? n->id.p : "";
}

int rolltui_layout_node_is_window(const RolltuiLayoutNode* n) {
  return n && n->kind == ROLLTUI_NODE_WINDOW;
}

/* The public loader: parses, unpacks and hands back an OWNED layout. The carrier is the
 * library's own business now — three consumers wrote the four-line dance. */
RolltuiLayout* rolltui_load_layout_text(const char* text, size_t len,
                                        const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                                        const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report) {
  RolltuiLoadedLayout loaded;
  RolltuiLayout* out;
  rolltui_loaded_layout_init(&loaded);
  if (!rolltui_load_layout_text_into(text, len, &loaded, default_actions, default_actions_n, hooks, report)) {
    rolltui_loaded_layout_release(&loaded);
    return NULL;
  }
  out = rolltui_layout_new();
  rolltui_loaded_layout_to_layout(&loaded, out);
  rolltui_loaded_layout_release(&loaded);
  return out;
}

void rolltui_layout_read_actions_key(const RolltuiJsonValue* root, RolltuiLayoutAction** actions, size_t* actions_n,
                                    size_t* actions_cap) {
  const RolltuiJsonValue* acts;
  size_t i, n;
  if (!rolltui_json_is_object(root)) return;
  acts = rolltui_json_get(root, K("actions"));
  if (!rolltui_json_is_object(acts)) return;
  n = rolltui_json_object_size(acts);
  for (i = 0; i < n; ++i) {
    size_t namelen = 0;
    const char* name = rolltui_json_object_key_at(acts, i, &namelen);
    const RolltuiJsonValue* d = rolltui_json_object_value_at(acts, i);
    if (rolltui_json_is_string(d)) {
      size_t dlen = 0;
      const char* dstr = rolltui_json_as_string(d, "", 0, &dlen);
      RolltuiLayoutAction* a;
      *actions = (RolltuiLayoutAction*)rolltui_grow_zeroed(*actions, actions_cap, *actions_n + 1, sizeof **actions);
      a = &(*actions)[*actions_n];
      rolltui_str_set(&a->name, name, namelen);
      rolltui_str_set(&a->description, dstr, dlen);
      ++*actions_n;
    }
  }
}

void rolltui_layout_actions_free(RolltuiLayoutAction* actions, size_t n) {
  size_t i;
  if (!actions) return;
  for (i = 0; i < n; ++i) {
    rolltui_str_free(&actions[i].name);
    rolltui_str_free(&actions[i].description);
  }
  rolltui_mem_free(actions);
}

/* ---- the loader's own JSON walk: node, layer, dim, bool ---------------------------------- */

static void set_str_field(RolltuiStr* field, const RolltuiJsonValue* x) {
  size_t sl = 0;
  const char* s = rolltui_json_as_string(x, "", 0, &sl);
  rolltui_str_set(field, s, sl);
}

static int dim_from_json(const RolltuiJsonValue* v, const char* where, size_t where_len, RolltuiLayoutReport* report,
                         RolltuiDim* out) {
  if (rolltui_json_is_number(v)) {
    const double num = rolltui_json_as_number(v, 0);
    if (num != floor(num) || fabs(num) > 1000000) {
      bad_at(report, where, where_len, ": a number is whole cells; use \"N%\" for a fraction");
      return 0;
    }
    out->fraction = 0;
    out->cells = (int)num;
    return 1;
  }
  if (rolltui_json_is_string(v)) {
    size_t sl = 0;
    const char* s = rolltui_json_as_string(v, "", 0, &sl);
    if (rolltui_parse_dim(s, sl, out)) return 1;
    {
      RolltuiStr msg = {0};
      appn(&msg, where, where_len);
      app(&msg, ": '");
      appn(&msg, s, sl);
      app(&msg, "' is not a dim (an integer, or \"N%\" with an optional \"\xC2\xB1 cells\")");
      add_bad(report, &msg);
    }
    return 0;
  }
  bad_at(report, where, where_len, ": expected an integer or a \"N%\" string");
  return 0;
}

static int bool_from_json(const RolltuiJsonValue* v, const char* where, size_t where_len, RolltuiLayoutReport* report,
                          unsigned char* out) {
  if (!rolltui_json_is_bool(v)) {
    bad_at(report, where, where_len, ": expected true or false");
    return 0;
  }
  *out = rolltui_json_as_bool(v, 0) ? 1 : 0;
  return 1;
}

/* An array of small owned strings seen so far in ONE layer's tree — GROWING AMORTISED,
 * freed at the end of `layer_from_json`'s walk. */
typedef struct SeenIds {
  RolltuiStr* v;
  size_t n, cap;
} SeenIds;

static void collect_ids(const RolltuiLayoutNode* n, SeenIds* seen, const char* where, size_t where_len,
                        RolltuiLayoutReport* report) {
  size_t i;
  if (n->id.n) {
    int dup = 0;
    for (i = 0; i < seen->n; ++i)
      if (rolltui_str_eq(&seen->v[i], n->id.p, n->id.n)) {
        dup = 1;
        break;
      }
    if (dup) {
      RolltuiStr msg = {0};
      appn(&msg, where, where_len);
      app(&msg, ".id: duplicate id '");
      appn(&msg, n->id.p, n->id.n);
      app(&msg, "'");
      add_bad(report, &msg);
    } else {
      seen->v = (RolltuiStr*)rolltui_grow_zeroed(seen->v, &seen->cap, seen->n + 1, sizeof *seen->v);
      rolltui_str_set(&seen->v[seen->n++], n->id.p, n->id.n);
    }
  }
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* c = n->children.v[i];
    RolltuiStr at = {0};
    char buf[24];
    int bl;
    appn(&at, where, where_len);
    app(&at, n->kind == ROLLTUI_NODE_ROW ? ".row[" : ".column[");
    bl = snprintf(buf, sizeof buf, "%zu", i);
    appn(&at, buf, (size_t)bl);
    app(&at, "]");
    collect_ids(c, seen, at.p, at.n, report);
    rolltui_str_free(&at);
  }
}

/* Fills an ALREADY-INITIALISED `n` (the caller's: `rolltui_node_list_add`'s result, or a
 * layer's pre-inited `root` — never re-inited here, so a caller reusing storage is never
 * silently leaked). Direct port of `node_from_json`. */
static void node_from_json(const RolltuiJsonValue* v, const char* where, size_t where_len,
                           const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report, RolltuiLayoutNode* n) {
  int has_row, has_col, has_content, has_tabs, has_grid, tab_given = 0, border_given = 0;
  size_t i, cnt;

  if (!rolltui_json_is_object(v)) {
    bad_at(report, where, where_len, ": expected a node object");
    return;
  }
  has_row = rolltui_json_has(v, K("row"));
  has_col = rolltui_json_has(v, K("column"));
  has_content = rolltui_json_has(v, K("content"));
  has_tabs = rolltui_json_has(v, K("tabs"));
  has_grid = rolltui_json_has(v, K("grid"));
  if (has_content && has_tabs) {
    bad_at(report, where, where_len, ": a window has \"content\" or \"tabs\", not both");
    return;
  }
  if (has_row + has_col + has_grid + (has_content || has_tabs) != 1) {
    bad_at(report, where, where_len, ": a node has exactly one of \"content\", \"row\", \"column\"");
    return;
  }
  /* A GRID is rows of cells: a column whose children are rows, each cell a node */
  n->kind = (unsigned char)(has_row ? ROLLTUI_NODE_ROW : (has_col || has_grid) ? ROLLTUI_NODE_COLUMN : ROLLTUI_NODE_WINDOW);

  cnt = rolltui_json_object_size(v);
  for (i = 0; i < cnt; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, i);
    RolltuiStr at = {0};
    appn(&at, where, where_len);
    app(&at, ".");
    appn(&at, k, klen);

    if (streq(k, klen, "row") || streq(k, klen, "column")) {
      if (!rolltui_json_is_array(x)) {
        bad_at(report, at.p, at.n, ": expected an array of nodes");
      } else {
        size_t an = rolltui_json_array_size(x), ai;
        for (ai = 0; ai < an; ++ai) {
          RolltuiStr cat = {0};
          char buf[24];
          int bl;
          RolltuiLayoutNode* child;
          appn(&cat, at.p, at.n);
          app(&cat, "[");
          bl = snprintf(buf, sizeof buf, "%zu", ai);
          appn(&cat, buf, (size_t)bl);
          app(&cat, "]");
          child = rolltui_node_list_add(&n->children); /* the C's emplace_back: fresh, already inited */
          node_from_json(rolltui_json_array_at(x, ai), cat.p, cat.n, hooks, report, child);
          rolltui_str_free(&cat);
        }
      }
    } else if (streq(k, klen, "grid") && rolltui_json_is_object(x)) {
      /* A GRID WITH SPANS: { "columns": 3 or "auto", "cell_height": rows, "cells": [ nodes, each may have "span": [rows, cols] ] } */
      const RolltuiJsonValue* cols = rolltui_json_get(x, K("columns"));
      const RolltuiJsonValue* cells = rolltui_json_get(x, K("cells"));
      const RolltuiJsonValue* ch = rolltui_json_get(x, K("cell_height"));
      const size_t cn = rolltui_json_is_array(cells) ? rolltui_json_array_size(cells) : 0;
      size_t ci;
      if (rolltui_json_is_number(cols) && rolltui_json_as_number(cols, 0) >= 1) n->grid_cols = (int)rolltui_json_as_number(cols, 1);
      else if (!rolltui_json_is_null(cols) && !(rolltui_json_is_string(cols) && rolltui_json_has(x, K("cell_height"))))
        bad_at(report, at.p, at.n, ".columns: expected a count, or \"auto\" with a cell_height");
      if (rolltui_json_is_number(ch) && rolltui_json_as_number(ch, 0) >= 1) n->grid_cell_h = (int)rolltui_json_as_number(ch, 1);
      if (!n->grid_cols && !n->grid_cell_h) n->grid_cols = 1;
      if (!rolltui_json_is_array(cells)) bad_at(report, at.p, at.n, ".cells: expected an array of nodes");
      for (ci = 0; ci < cn; ++ci) {
        RolltuiStr cat = {0};
        char buf[24];
        const int bl = snprintf(buf, sizeof buf, ".cells[%zu]", ci);
        appn(&cat, at.p, at.n);
        appn(&cat, buf, (size_t)(bl > 0 ? bl : 0));
        node_from_json(rolltui_json_array_at(cells, ci), cat.p, cat.n, hooks, report, rolltui_node_list_add(&n->children));
        rolltui_str_free(&cat);
      }
    } else if (streq(k, klen, "grid")) {
      size_t rn = rolltui_json_is_array(x) ? rolltui_json_array_size(x) : 0, ri;
      if (!rolltui_json_is_array(x)) bad_at(report, at.p, at.n, ": expected rows, each an array of nodes");
      for (ri = 0; ri < rn; ++ri) {
        const RolltuiJsonValue* row = rolltui_json_array_at(x, ri);
        RolltuiLayoutNode* rnode = rolltui_node_list_add(&n->children);
        size_t cn = rolltui_json_is_array(row) ? rolltui_json_array_size(row) : 0, ci;
        rnode->kind = ROLLTUI_NODE_ROW;
        if (!rolltui_json_is_array(row)) bad_at(report, at.p, at.n, ": each row of a grid is an array of nodes");
        for (ci = 0; ci < cn; ++ci) {
          RolltuiStr cat = {0};
          char buf[48];
          const int bl = snprintf(buf, sizeof buf, "[%zu][%zu]", ri, ci);
          appn(&cat, at.p, at.n);
          appn(&cat, buf, (size_t)(bl > 0 ? bl : 0));
          node_from_json(rolltui_json_array_at(row, ci), cat.p, cat.n, hooks, report, rolltui_node_list_add(&rnode->children));
          rolltui_str_free(&cat);
        }
      }
    } else if (streq(k, klen, "content")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->content, x);
    } else if (streq(k, klen, "tabs")) {
      const size_t tn = rolltui_json_is_array(x) ? rolltui_json_array_size(x) : 0;
      size_t ti;
      if (tn == 0) bad_at(report, at.p, at.n, ": expected an array of tabs, each { \"title\": ..., \"content\": ... }");
      for (ti = 0; ti < tn; ++ti) {
        const RolltuiJsonValue* t = rolltui_json_array_at(x, ti);
        const RolltuiJsonValue* tc = rolltui_json_is_object(t) && rolltui_json_has(t, K("content")) ? rolltui_json_get(t, K("content")) : NULL;
        const RolltuiJsonValue* tt = rolltui_json_is_object(t) && rolltui_json_has(t, K("title")) ? rolltui_json_get(t, K("title")) : NULL;
        size_t cl = 0, tl = 0;
        const char* cs = tc && rolltui_json_is_string(tc) ? rolltui_json_as_string(tc, "", 0, &cl) : NULL;
        const char* ts = tt && rolltui_json_is_string(tt) ? rolltui_json_as_string(tt, "", 0, &tl) : NULL;
        if (!cs || (tt && !ts) || memchr(cs, 0, cl) || (ts && memchr(ts, 0, tl)) || n->tab_count == 255) {
          bad_at(report, at.p, at.n, ": each tab is { \"title\": a string, \"content\": a string }");
          continue;
        }
        rolltui_node_tab_add(n, ts ? ts : cs, ts ? tl : cl, cs, cl); /* untitled: its content names it */
      }
    } else if (streq(k, klen, "tab")) {
      if (!rolltui_json_is_number(x) || rolltui_json_as_number(x, 0) < 0) bad_at(report, at.p, at.n, ": expected a tab's index");
      else n->tab = (unsigned char)rolltui_json_as_number(x, 0), tab_given = 1;
    } else if (streq(k, klen, "breakpoint")) {
      const size_t bn = rolltui_json_is_object(x) ? rolltui_json_object_size(x) : 0;
      size_t bi;
      if (!rolltui_json_is_object(x)) bad_at(report, at.p, at.n, ": expected { \"width\": cells, \"height\": cells }");
      for (bi = 0; bi < bn; ++bi) {
        size_t bkl = 0;
        const char* bk = rolltui_json_object_key_at(x, bi, &bkl);
        const RolltuiJsonValue* bv = rolltui_json_object_value_at(x, bi);
        RolltuiStr bat = {0};
        appn(&bat, at.p, at.n);
        app(&bat, ".");
        appn(&bat, bk, bkl);
        if (!streq(bk, bkl, "width") && !streq(bk, bkl, "height") && !streq(bk, bkl, "pane_width") && !streq(bk, bkl, "pane_height"))
          unknown_at(report, bat.p, bat.n);
        else if (!rolltui_json_is_number(bv) || rolltui_json_as_number(bv, 0) < 0) bad_at(report, bat.p, bat.n, ": expected cells");
        else if (streq(bk, bkl, "width")) n->show_width = (int)rolltui_json_as_number(bv, 0);
        else if (streq(bk, bkl, "height")) n->show_height = (int)rolltui_json_as_number(bv, 0);
        else if (streq(bk, bkl, "pane_width")) n->pane_width = (int)rolltui_json_as_number(bv, 0);
        else n->pane_height = (int)rolltui_json_as_number(bv, 0);
        rolltui_str_free(&bat);
      }
    } else if (streq(k, klen, "id")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->id, x);
    } else if (streq(k, klen, "title")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->title, x);
    } else if (streq(k, klen, "footer")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->footer, x);
    } else if (streq(k, klen, "border")) {
      unsigned char b;
      size_t sl = 0;
      const char* s = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &sl) : NULL;
      if (!s || !rolltui_border_from_name(s, sl, &b))
        bad_at(report, at.p, at.n, ": expected none | single | rounded | double | heavy | hidden | quadrant-inside | quadrant-outside");
      else n->border = b, border_given = 1;
    } else if (streq(k, klen, "background")) {
      unsigned char role;
      size_t sl = 0;
      const char* s = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &sl) : NULL;
      if (!s || !hooks || !hooks->role_from_name || !hooks->role_from_name(hooks->role_from_name_ctx, s, sl, &role))
        bad_at(report, at.p, at.n, ": expected a role name");
      else n->background = role;
    } else if (streq(k, klen, "min") || streq(k, klen, "max") || streq(k, klen, "padding") || streq(k, klen, "gap") ||
               streq(k, klen, "stack_below") || streq(k, klen, "focus_weight")) {
      if (!rolltui_json_is_number(x) || rolltui_json_as_number(x, 0) < 0 || rolltui_json_as_number(x, 0) > 10000) {
        bad_at(report, at.p, at.n, ": expected cells");
      } else {
        const int v = (int)rolltui_json_as_number(x, 0);
        if (streq(k, klen, "min")) n->size_min = v;
        else if (streq(k, klen, "max")) n->size_max = v;
        else if (streq(k, klen, "padding")) n->padding = (unsigned char)(v > 255 ? 255 : v);
        else if (streq(k, klen, "gap")) n->gap = (unsigned char)(v > 255 ? 255 : v);
        else if (streq(k, klen, "stack_below")) n->stack_below = v;
        else n->focus_weight = v;
      }
    } else if (streq(k, klen, "separators")) {
      bool_from_json(x, at.p, at.n, report, &n->separators);
    } else if (streq(k, klen, "span")) {
      if (!rolltui_json_is_array(x) || rolltui_json_array_size(x) != 2 || !rolltui_json_is_number(rolltui_json_array_at(x, 0)) ||
          !rolltui_json_is_number(rolltui_json_array_at(x, 1)) || rolltui_json_as_number(rolltui_json_array_at(x, 0), 0) < 1 ||
          rolltui_json_as_number(rolltui_json_array_at(x, 1), 0) < 1)
        bad_at(report, at.p, at.n, ": expected [rows, columns]");
      else {
        const double r = rolltui_json_as_number(rolltui_json_array_at(x, 0), 1), c = rolltui_json_as_number(rolltui_json_array_at(x, 1), 1);
        n->span_r = (unsigned char)(r > 255 ? 255 : r);
        n->span_c = (unsigned char)(c > 255 ? 255 : c);
      }
    } else if (streq(k, klen, "focusable")) {
      bool_from_json(x, at.p, at.n, report, &n->focusable);
    } else if (streq(k, klen, "visible")) {
      bool_from_json(x, at.p, at.n, report, &n->visible);
    } else if (streq(k, klen, "size")) {
      if (rolltui_json_is_number(x)) {
        RolltuiDim d;
        if (dim_from_json(x, at.p, at.n, report, &d)) {
          n->size.fill = 0;
          n->size.weight = 1;
          n->size.dim = d;
        }
      } else if (rolltui_json_is_string(x)) {
        size_t sl = 0;
        const char* s = rolltui_json_as_string(x, "", 0, &sl);
        RolltuiSplitSizeRaw ss;
        if (streq(s, sl, "content")) { /* as tall (or wide) as what the window holds, set each frame */
          n->fit = 1;
          n->size.fill = 0;
          n->size.weight = 1;
          n->size.dim.fraction = 0;
          n->size.dim.cells = 3;
        } else if (rolltui_parse_split_size(s, sl, &ss)) {
          n->size = ss;
        } else {
          RolltuiStr msg = {0};
          appn(&msg, at.p, at.n);
          app(&msg, ": '");
          appn(&msg, s, sl);
          app(&msg, "' is not a size (an integer, \"N%\", \"fill\" or \"fill N\")");
          add_bad(report, &msg);
        }
      } else {
        bad_at(report, at.p, at.n, ": expected an integer, \"N%\", \"fill\" or \"fill N\"");
      }
    } else {
      unknown_at(report, at.p, at.n);
    }
    rolltui_str_free(&at);
  }

  /* A TABBED WINDOW SHOWS ONE: its content is the shown tab's, and its strip is drawn in its top border, so it has one. */
  if (n->tab_count) {
    const char* c;
    size_t cl = 0;
    if (!border_given) n->border = ROLLTUI_BORDER_SINGLE;
    else if (n->border == ROLLTUI_BORDER_NONE) bad_at(report, where, where_len, ".border: a window with tabs draws them in its border");
    if (n->tab >= n->tab_count) {
      if (tab_given) bad_at(report, where, where_len, ".tab: there is no tab of that index");
      n->tab = 0;
    }
    rolltui_node_tab_at(n, n->tab, NULL, NULL, &c, &cl);
    rolltui_str_set(&n->content, c, cl);
  } else if (tab_given) {
    bad_at(report, where, where_len, ".tab: only a window with \"tabs\" has a tab");
  }
  /* Content is kind[:source] (Layout.hpp). Anything the table does not know is a bad value
   * that names the fix. Every tab's content is checked, shown or not. */
  if (n->kind == ROLLTUI_NODE_WINDOW) {
    size_t ci;
    for (ci = 0; ci < (n->tab_count ? n->tab_count : 1); ++ci) {
      RolltuiStr shown_content = n->content;
      const RolltuiStr* content = &shown_content;
      if (n->tab_count) rolltui_node_tab_at(n, ci, NULL, NULL, (const char**)&shown_content.p, &shown_content.n);
      /* An UNKNOWN KIND is deliberately NOT a bad value here — rung 2 belongs to the host,
       * and this loader runs before a host has necessarily registered anything; `Windows`
       * reports it, by name, with the error panel drawn. Every other problem is a fact
       * about the STRING and is the loader's to name. */
      unsigned char problem = ROLLTUI_CONTENT_PROBLEM_NONE;
      size_t row = 0;
      int is_host = 0;
      const char *cname = NULL, *csrc = NULL;
      size_t cnamelen = 0, csrclen = 0;
      RolltuiStr why = {0};
      const int ok =
          rolltui_content_parse(NULL, content->p, content->n, &row, &is_host, &cname, &cnamelen, &csrc,
                                &csrclen, &problem, &why);
      if (!ok && problem != ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND) {
        RolltuiStr msg = {0};
        appn(&msg, where, where_len);
        app(&msg, ".content: ");
        appn(&msg, why.p, why.n);
        add_bad(report, &msg);
      }
      rolltui_str_free(&why);
    }
  }
  if (n->kind == ROLLTUI_NODE_WINDOW && n->id.n == 0) rolltui_str_set(&n->id, n->content.p, n->content.n);
}

/* Fills an ALREADY-INITIALISED `l` (a fresh popup, or the caller's pre-inited base layer).
 * Direct port of `layer_from_json`. */
static void layer_from_json(const RolltuiJsonValue* v, const char* where, size_t where_len, int is_popup,
                            const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report, RolltuiLayer* l) {
  int have_root = 0, have_dismiss = 0;
  size_t i, n;
  SeenIds ids = {0};

  if (!rolltui_json_is_object(v)) {
    bad_at(report, where, where_len, ": expected an object");
    return;
  }
  n = rolltui_json_object_size(v);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(v, i, &klen);
    const RolltuiJsonValue* x = rolltui_json_object_value_at(v, i);
    RolltuiStr at = {0};
    appn(&at, where, where_len);
    app(&at, ".");
    appn(&at, k, klen);

    if (streq(k, klen, "root")) {
      node_from_json(x, at.p, at.n, hooks, report, &l->root);
      have_root = 1;
    } else if (streq(k, klen, "focus")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a window id");
      else set_str_field(&l->focus, x);
    } else if (is_popup && streq(k, klen, "id")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&l->id, x);
    } else if (is_popup && streq(k, klen, "modal")) {
      bool_from_json(x, at.p, at.n, report, &l->modal);
    } else if (is_popup && streq(k, klen, "dismiss")) {
      bool_from_json(x, at.p, at.n, report, &l->dismiss);
      have_dismiss = 1;
    } else if (is_popup && streq(k, klen, "clamp")) {
      bool_from_json(x, at.p, at.n, report, &l->placement.clamp);
    } else if (is_popup && streq(k, klen, "margin")) {
      const double num = rolltui_json_is_number(x) ? rolltui_json_as_number(x, -1) : -1;
      if (num != floor(num) || num < 0 || num > 255)
        bad_at(report, at.p, at.n, ": expected a whole number of columns, 0 to 255");
      else l->placement.edge_margin = (unsigned char)num;
    } else if (is_popup && streq(k, klen, "anchor")) {
      unsigned char a;
      size_t sl = 0;
      const char* s = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &sl) : NULL;
      if (!s || !rolltui_anchor_from_name(s, sl, &a))
        bad_at(report, at.p, at.n,
               ": expected top-left | top | top-right | left | center | right | bottom-left | bottom | "
               "bottom-right");
      else l->placement.anchor = a;
    } else if (is_popup && (streq(k, klen, "x") || streq(k, klen, "y") || streq(k, klen, "w") || streq(k, klen, "h"))) {
      RolltuiDim d;
      if (dim_from_json(x, at.p, at.n, report, &d)) {
        if (streq(k, klen, "x")) l->placement.x = d;
        else if (streq(k, klen, "y")) l->placement.y = d;
        else if (streq(k, klen, "w")) l->placement.w = d;
        else l->placement.h = d;
      }
    } else if (is_popup && (streq(k, klen, "min_w") || streq(k, klen, "min_h") || streq(k, klen, "max_w") ||
                            streq(k, klen, "max_h"))) {
      RolltuiDim d;
      if (dim_from_json(x, at.p, at.n, report, &d)) {
        RolltuiOptDim od;
        od.present = 1;
        od.d = d;
        if (streq(k, klen, "min_w")) l->placement.min_w = od;
        else if (streq(k, klen, "min_h")) l->placement.min_h = od;
        else if (streq(k, klen, "max_w")) l->placement.max_w = od;
        else l->placement.max_h = od;
      }
    } else {
      unknown_at(report, at.p, at.n);
    }
    rolltui_str_free(&at);
  }
  if (!have_root) bad_at(report, where, where_len, ": no \"root\" node");
  /* WHETHER A CLICK OUTSIDE CLOSES A POPUP IS NEVER A DEFAULT: a popup that asks a question
   * must be answered and one that only shows something may be clicked away, and only the file
   * knows which this one is. A popup that does not say is a named problem, loaded as if it
   * said false. */
  if (is_popup && !have_dismiss) bad_at(report, where, where_len, ": says nothing about \"dismiss\" — a popup states whether a click outside closes it: true | false");
  {
    RolltuiStr rwhere = {0};
    appn(&rwhere, where, where_len);
    app(&rwhere, ".root");
    collect_ids(&l->root, &ids, rwhere.p, rwhere.n, report);
    rolltui_str_free(&rwhere);
  }
  if (l->focus.n) {
    int found = 0;
    size_t fi;
    for (fi = 0; fi < ids.n; ++fi)
      if (rolltui_str_eq(&ids.v[fi], l->focus.p, l->focus.n)) {
        found = 1;
        break;
      }
    if (!found) {
      RolltuiStr msg = {0};
      appn(&msg, where, where_len);
      app(&msg, ".focus: no window with id '");
      appn(&msg, l->focus.p, l->focus.n);
      app(&msg, "'");
      add_bad(report, &msg);
    }
  }
  {
    size_t fi;
    for (fi = 0; fi < ids.n; ++fi) rolltui_str_free(&ids.v[fi]);
    rolltui_mem_free(ids.v);
  }
}

static void strip_leading_dot(RolltuiStr* s) {
  /* The base layer's report paths begin with "." because its keys sit at the top level
   * (`layer_from_json` is called with `where=""`, so its own `at = where + "." + k` starts
   * with the dot) — stripped here so the report reads "root.column[0]...", not
   * ".root.column[0]...". Deliberately NOT applied to `notes`: the original C++ only
   * ever strips `unknown_keys`/`bad_values`, so a base window's migration note keeps its
   * leading dot — preserved as-is rather than "fixed", per this port's own rule. */
  if (s->n && s->p[0] == '.') {
    memmove(s->p, s->p + 1, s->n - 1);
    s->n -= 1;
    s->p[s->n] = '\0';
  }
}

int rolltui_load_layout(const RolltuiJsonValue* root, RolltuiLoadedLayout* out,
                        const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                        const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report) {
  RolltuiJsonValue* base;
  int have_actions = 0;
  size_t i, n;

  if (!rolltui_json_is_object(root)) {
    rolltui_str_set(&report->error, K("layout file must be a JSON object"));
    return 0;
  }
  if (!rolltui_json_has(root, K("root"))) {
    rolltui_str_set(&report->error, K("layout file has no \"root\" node"));
    return 0;
  }

  base = rolltui_json_object();
  n = rolltui_json_object_size(root);
  for (i = 0; i < n; ++i) {
    size_t klen = 0;
    const char* k = rolltui_json_object_key_at(root, i, &klen);
    const RolltuiJsonValue* v = rolltui_json_object_value_at(root, i);

    if (streq(k, klen, "name")) {
      if (!rolltui_json_is_string(v)) bad_at(report, "", 0, "name: expected a string");
      else set_str_field(&out->name, v);
    } else if (streq(k, klen, "min_width") || streq(k, klen, "min_height")) {
      const double num = rolltui_json_as_number(v, -1);
      if (!rolltui_json_is_number(v) || num < 0 || num != floor(num)) {
        RolltuiStr msg = {0};
        appn(&msg, k, klen);
        app(&msg, ": expected a whole number of cells");
        add_bad(report, &msg);
      } else if (streq(k, klen, "min_width")) {
        out->min_width = (int)num;
      } else {
        out->min_height = (int)num;
      }
    } else if (streq(k, klen, "inline")) {
      if (!rolltui_json_is_bool(v)) bad_at(report, "", 0, "inline: expected true or false");
      else out->inline_mode = (unsigned char)(rolltui_json_as_bool(v, 0) != 0);
    } else if (streq(k, klen, "root") || streq(k, klen, "focus")) {
      rolltui_json_set(base, k, klen, rolltui_json_clone(v));
    } else if (streq(k, klen, "actions")) {
      have_actions = 1;
      if (!rolltui_json_is_object(v)) {
        bad_at(report, "", 0, "actions: expected an object of action name \xE2\x86\x92 description");
      } else {
        size_t an = rolltui_json_object_size(v), ai;
        for (ai = 0; ai < an; ++ai) {
          size_t namelen = 0;
          const char* name = rolltui_json_object_key_at(v, ai, &namelen);
          const RolltuiJsonValue* desc = rolltui_json_object_value_at(v, ai);
          RolltuiStr at = {0};
          RolltuiStr why = {0};
          app(&at, "actions.");
          appn(&at, name, namelen);
          rolltui_action_decl_problem(name, namelen, hooks, &why);
          if (why.n) {
            RolltuiStr msg = {0};
            appn(&msg, at.p, at.n);
            app(&msg, ": ");
            appn(&msg, why.p, why.n);
            add_bad(report, &msg);
          } else {
            size_t dup_i;
            int dup = 0;
            for (dup_i = 0; dup_i < out->actions_n; ++dup_i)
              if (rolltui_str_eq(&out->actions[dup_i].name, name, namelen)) {
                dup = 1;
                break;
              }
            if (dup) {
              bad_at(report, at.p, at.n, ": declared twice");
            } else if (!rolltui_json_is_string(desc)) {
              bad_at(report, at.p, at.n, ": expected a description string");
            } else {
              size_t dlen = 0;
              const char* dstr = rolltui_json_as_string(desc, "", 0, &dlen);
              RolltuiLayoutAction* a;
              out->actions = (RolltuiLayoutAction*)rolltui_grow_zeroed(out->actions, &out->actions_cap,
                                                                       out->actions_n + 1, sizeof *out->actions);
              a = &out->actions[out->actions_n++];
              rolltui_str_set(&a->name, name, namelen);
              rolltui_str_set(&a->description, dstr, dlen);
            }
          }
          rolltui_str_free(&why);
          rolltui_str_free(&at);
        }
      }
    } else if (streq(k, klen, "popups")) {
      if (!rolltui_json_is_array(v)) {
        bad_at(report, "", 0, "popups: expected an array");
      } else {
        size_t pn = rolltui_json_array_size(v), pi;
        for (pi = 0; pi < pn; ++pi) {
          const RolltuiJsonValue* pv = rolltui_json_array_at(v, pi);
          RolltuiStr where = {0};
          RolltuiLayer p;
          char buf[24];
          int bl;
          app(&where, "popups[");
          bl = snprintf(buf, sizeof buf, "%zu", pi);
          appn(&where, buf, (size_t)bl);
          app(&where, "]");

          rolltui_layer_init(&p);
          layer_from_json(pv, where.p, where.n, 1, hooks, report, &p);
          if (p.id.n == 0) {
            bad_at(report, where.p, where.n, ": a popup needs an \"id\"");
          } else {
            int found = 0;
            size_t ci;
            for (ci = 0; ci < out->popups_n; ++ci)
              if (rolltui_str_eq(&out->popups[ci].id, p.id.p, p.id.n)) {
                found = 1;
                break;
              }
            if (found) {
              RolltuiStr msg = {0};
              appn(&msg, where.p, where.n);
              app(&msg, ".id: duplicate popup id '");
              appn(&msg, p.id.p, p.id.n);
              app(&msg, "'");
              add_bad(report, &msg);
            }
          }
          out->popups = (RolltuiLayer*)rolltui_grow_zeroed(out->popups, &out->popups_cap, out->popups_n + 1,
                                                           sizeof *out->popups);
          rolltui_layer_init(&out->popups[out->popups_n]);
          rolltui_layer_move(&out->popups[out->popups_n], &p);
          ++out->popups_n;
          rolltui_layer_release(&p); /* moved-from: a safe no-op, kept for symmetry with every other release */
          rolltui_str_free(&where);
        }
      }
    } else {
      unknown_at(report, k, klen);
    }
  }

  layer_from_json(base, "", 0, 0, hooks, report, &out->base);
  out->base.min_width = out->min_width;
  out->base.min_height = out->min_height;
  rolltui_json_free(base);

  /* A file that declares no actions — a hand-written one, or any saved before the key
   * existed — would silently lose every app key. It is given the shipped default's and told
   * so in `notes`; the next save writes them into the file. An explicit "actions": {} means
   * none and is kept. */
  if (!have_actions && default_actions_n) {
    RolltuiStr names = {0};
    size_t di;
    out->actions = (RolltuiLayoutAction*)rolltui_grow_zeroed(out->actions, &out->actions_cap, default_actions_n,
                                                             sizeof *out->actions);
    for (di = 0; di < default_actions_n; ++di) {
      rolltui_str_set(&out->actions[di].name, default_actions[di].name.p, default_actions[di].name.n);
      rolltui_str_set(&out->actions[di].description, default_actions[di].description.p,
                     default_actions[di].description.n);
      if (di) app(&names, ", ");
      appn(&names, default_actions[di].name.p, default_actions[di].name.n);
    }
    out->actions_n = default_actions_n;
    {
      RolltuiStr msg = {0};
      app(&msg, "actions: none declared; the shipped default's were added (");
      appn(&msg, names.p, names.n);
      app(&msg, ")");
      add_note(report, &msg);
    }
    rolltui_str_free(&names);
  }

  /* The base's report paths begin with "." because its keys sit at the top level. */
  for (i = 0; i < report->unknown_keys_n; ++i) strip_leading_dot(&report->unknown_keys[i]);
  for (i = 0; i < report->bad_values_n; ++i) strip_leading_dot(&report->bad_values[i]);
  return 1;
}

int rolltui_load_layout_text_into(const char* text, size_t len, RolltuiLoadedLayout* out,
                                  const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                                  const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report) {
  RolltuiStr jerr = {0};
  RolltuiJsonValue* root = rolltui_json_parse(text, len, &jerr);
  int ok;
  if (!root) {
    rolltui_str_move(&report->error, &jerr);
    return 0;
  }
  rolltui_str_free(&jerr);
  ok = rolltui_load_layout(root, out, default_actions, default_actions_n, hooks, report);
  rolltui_json_free(root);
  return ok;
}

/* ---- serialising: back to JSON, as C ----------------------------------------------------- */

static RolltuiJsonValue* dim_to_json(RolltuiDim d) {
  if (d.fraction == 0) return rolltui_json_number(d.cells);
  {
    char buf[ROLLTUI_DIM_STRING_MAX];
    const size_t n = rolltui_dim_to_string(d, buf, sizeof buf);
    return rolltui_json_string(buf, n);
  }
}

static RolltuiJsonValue* node_to_json(const RolltuiLayoutNode* n, const RolltuiLayoutHooks* hooks) {
  RolltuiJsonValue* o = rolltui_json_object();
  if (n->kind == ROLLTUI_NODE_WINDOW && n->tab_count) { /* tabs: the id always, since the shown content changes */
    RolltuiJsonValue* arr = rolltui_json_array();
    size_t i;
    rolltui_json_set(o, K("id"), rolltui_json_string(n->id.p, n->id.n));
    for (i = 0; i < n->tab_count; ++i) {
      RolltuiJsonValue* t = rolltui_json_object();
      const char *tt, *tc;
      size_t tl = 0, cl = 0;
      rolltui_node_tab_at(n, i, &tt, &tl, &tc, &cl);
      rolltui_json_set(t, K("title"), rolltui_json_string(tt, tl));
      rolltui_json_set(t, K("content"), rolltui_json_string(tc, cl));
      rolltui_json_array_push(arr, t);
    }
    rolltui_json_set(o, K("tabs"), arr);
    if (n->tab) rolltui_json_set(o, K("tab"), rolltui_json_number(n->tab));
  } else if (n->kind == ROLLTUI_NODE_WINDOW) {
    const int same = n->id.n == n->content.n && (n->id.n == 0 || memcmp(n->id.p, n->content.p, n->id.n) == 0);
    if (!same) rolltui_json_set(o, K("id"), rolltui_json_string(n->id.p, n->id.n));
    rolltui_json_set(o, K("content"), rolltui_json_string(n->content.p, n->content.n));
  } else if (n->id.n) {
    rolltui_json_set(o, K("id"), rolltui_json_string(n->id.p, n->id.n));
  }
  if (n->border != ROLLTUI_BORDER_NONE) {
    size_t bl = 0;
    const char* bn = rolltui_border_name(n->border, &bl);
    rolltui_json_set(o, K("border"), rolltui_json_string(bn, bl));
  }
  if (n->title.n) rolltui_json_set(o, K("title"), rolltui_json_string(n->title.p, n->title.n));
  if (n->footer.n) rolltui_json_set(o, K("footer"), rolltui_json_string(n->footer.p, n->footer.n));
  if (n->focusable) rolltui_json_set(o, K("focusable"), rolltui_json_bool(1));
  if (!n->visible) rolltui_json_set(o, K("visible"), rolltui_json_bool(0));
  if (n->show_width || n->show_height || n->pane_width || n->pane_height) {
    RolltuiJsonValue* bp = rolltui_json_object();
    if (n->show_width) rolltui_json_set(bp, K("width"), rolltui_json_number(n->show_width));
    if (n->show_height) rolltui_json_set(bp, K("height"), rolltui_json_number(n->show_height));
    if (n->pane_width) rolltui_json_set(bp, K("pane_width"), rolltui_json_number(n->pane_width));
    if (n->pane_height) rolltui_json_set(bp, K("pane_height"), rolltui_json_number(n->pane_height));
    rolltui_json_set(o, K("breakpoint"), bp);
  }
  if (n->size_min) rolltui_json_set(o, K("min"), rolltui_json_number(n->size_min));
  if (n->size_max) rolltui_json_set(o, K("max"), rolltui_json_number(n->size_max));
  if (n->padding) rolltui_json_set(o, K("padding"), rolltui_json_number(n->padding));
  if (n->gap) rolltui_json_set(o, K("gap"), rolltui_json_number(n->gap));
  if (n->separators) rolltui_json_set(o, K("separators"), rolltui_json_bool(1));
  if (n->stack_below) rolltui_json_set(o, K("stack_below"), rolltui_json_number(n->stack_below));
  if (n->focus_weight) rolltui_json_set(o, K("focus_weight"), rolltui_json_number(n->focus_weight));
  if (n->span_r > 1 || n->span_c > 1) {
    RolltuiJsonValue* sp = rolltui_json_array();
    rolltui_json_array_push(sp, rolltui_json_number(n->span_r ? n->span_r : 1));
    rolltui_json_array_push(sp, rolltui_json_number(n->span_c ? n->span_c : 1));
    rolltui_json_set(o, K("span"), sp);
  }
  if (n->background != ROLLTUI_ROLE_DEFAULT_BACKGROUND) {
    char namebuf[ROLLTUI_ROLE_NAME_MAX];
    size_t nl = 0;
    if (hooks && hooks->role_name) nl = hooks->role_name(hooks->role_name_ctx, n->background, namebuf, sizeof namebuf);
    rolltui_json_set(o, K("background"), rolltui_json_string(namebuf, nl));
  }
  {
    /* RolltuiSplitSizeRaw{}'s own default (rolltui_layout_tree.h: fill=1, weight=1, dim={0,0}). */
    const int is_default = n->size.fill && n->size.weight == 1 && n->size.dim.fraction == 0 && n->size.dim.cells == 0;
    if (n->fit) {
      rolltui_json_set(o, K("size"), rolltui_json_string(K("content")));
    } else if (!is_default) {
      if (!n->size.fill && n->size.dim.fraction == 0) {
        rolltui_json_set(o, K("size"), rolltui_json_number(n->size.dim.cells));
      } else {
        char buf[ROLLTUI_DIM_STRING_MAX];
        const size_t bl = rolltui_split_size_to_string(n->size, buf, sizeof buf);
        rolltui_json_set(o, K("size"), rolltui_json_string(buf, bl));
      }
    }
  }
  if (n->kind != ROLLTUI_NODE_WINDOW) {
    RolltuiJsonValue* arr = rolltui_json_array();
    size_t i;
    for (i = 0; i < n->children.n; ++i) rolltui_json_array_push(arr, node_to_json(n->children.v[i], hooks));
    if (n->kind == ROLLTUI_NODE_COLUMN && (n->grid_cols || n->grid_cell_h)) { /* a grid with spans */
      RolltuiJsonValue* g = rolltui_json_object();
      if (n->grid_cols) rolltui_json_set(g, K("columns"), rolltui_json_number(n->grid_cols));
      else rolltui_json_set(g, K("columns"), rolltui_json_string(K("auto")));
      if (n->grid_cell_h) rolltui_json_set(g, K("cell_height"), rolltui_json_number(n->grid_cell_h));
      rolltui_json_set(g, K("cells"), arr);
      rolltui_json_set(o, K("grid"), g);
    } else if (n->kind == ROLLTUI_NODE_ROW) {
      rolltui_json_set(o, K("row"), arr);
    } else {
      rolltui_json_set(o, K("column"), arr);
    }
  }
  return o;
}

static RolltuiJsonValue* layer_to_json(const RolltuiLayer* l, int is_popup, const RolltuiLayoutHooks* hooks) {
  RolltuiJsonValue* o = rolltui_json_object();
  if (is_popup) {
    rolltui_json_set(o, K("id"), rolltui_json_string(l->id.p, l->id.n));
    rolltui_json_set(o, K("x"), dim_to_json(l->placement.x));
    rolltui_json_set(o, K("y"), dim_to_json(l->placement.y));
    rolltui_json_set(o, K("w"), dim_to_json(l->placement.w));
    rolltui_json_set(o, K("h"), dim_to_json(l->placement.h));
    if (l->placement.anchor != 0 /* TopLeft */) {
      size_t al = 0;
      const char* an = rolltui_anchor_name(l->placement.anchor, &al);
      rolltui_json_set(o, K("anchor"), rolltui_json_string(an, al));
    }
    if (!l->placement.clamp) rolltui_json_set(o, K("clamp"), rolltui_json_bool(0));
    if (l->placement.edge_margin) rolltui_json_set(o, K("margin"), rolltui_json_number(l->placement.edge_margin));
    if (l->placement.min_w.present) rolltui_json_set(o, K("min_w"), dim_to_json(l->placement.min_w.d));
    if (l->placement.min_h.present) rolltui_json_set(o, K("min_h"), dim_to_json(l->placement.min_h.d));
    if (l->placement.max_w.present) rolltui_json_set(o, K("max_w"), dim_to_json(l->placement.max_w.d));
    if (l->placement.max_h.present) rolltui_json_set(o, K("max_h"), dim_to_json(l->placement.max_h.d));
    if (l->modal) rolltui_json_set(o, K("modal"), rolltui_json_bool(1));
    rolltui_json_set(o, K("dismiss"), rolltui_json_bool(l->dismiss ? 1 : 0)); /* always: it is never a default */
  }
  if (l->focus.n) rolltui_json_set(o, K("focus"), rolltui_json_string(l->focus.p, l->focus.n));
  rolltui_json_set(o, K("root"), node_to_json(&l->root, hooks));
  return o;
}

RolltuiJsonValue* rolltui_layout_to_json_value(const char* name, size_t name_len, int min_width, int min_height,
                                               int inline_mode, const RolltuiLayoutAction* actions, size_t actions_n,
                                               const RolltuiLayer* base, const RolltuiLayer* popups,
                                               size_t popups_n, const RolltuiLayoutHooks* hooks) {
  RolltuiJsonValue* o = rolltui_json_object();
  size_t i;
  rolltui_json_set(o, K("name"), rolltui_json_string(name, name_len));
  if (min_width) rolltui_json_set(o, K("min_width"), rolltui_json_number(min_width));
  if (min_height) rolltui_json_set(o, K("min_height"), rolltui_json_number(min_height));
  if (inline_mode) rolltui_json_set(o, K("inline"), rolltui_json_bool(1));
  /* Always written, even when empty: an absent "actions" key means "a file from before they
   * existed" and is filled in by the loader, so a layout that deliberately declares none has
   * to be able to say so. */
  {
    RolltuiJsonValue* acts = rolltui_json_object();
    for (i = 0; i < actions_n; ++i)
      rolltui_json_set(acts, actions[i].name.p, actions[i].name.n,
                       rolltui_json_string(actions[i].description.p, actions[i].description.n));
    rolltui_json_set(o, K("actions"), acts);
  }
  /* The base layer's own two keys, spliced straight onto `o` — never through layer_to_json's
   * popup-only wrapper (is_popup=0 there only ever sets exactly these two), which would cost
   * a temporary object and a clone to unwrap again for no observable difference. */
  if (base->focus.n) rolltui_json_set(o, K("focus"), rolltui_json_string(base->focus.p, base->focus.n));
  rolltui_json_set(o, K("root"), node_to_json(&base->root, hooks));
  if (popups_n) {
    RolltuiJsonValue* arr = rolltui_json_array();
    for (i = 0; i < popups_n; ++i) rolltui_json_array_push(arr, layer_to_json(&popups[i], 1, hooks));
    rolltui_json_set(o, K("popups"), arr);
  }
  return o;
}

void rolltui_layout_to_json_text(const char* name, size_t name_len, int min_width, int min_height, int inline_mode,
                                const RolltuiLayoutAction* actions, size_t actions_n, const RolltuiLayer* base,
                                const RolltuiLayer* popups, size_t popups_n, const RolltuiLayoutHooks* hooks,
                                RolltuiStr* out) {
  RolltuiJsonValue* v =
      rolltui_layout_to_json_value(name, name_len, min_width, min_height, inline_mode, actions, actions_n, base, popups,
                                   popups_n, hooks);
  rolltui_json_dump(v, 2, out);
  rolltui_json_free(v);
}

/* ---- the stack --------------------------------------------------------------------------- */

struct RolltuiWindowStack {
  RolltuiLayer* layers; /* OWNED; always at least one (the base) */
  size_t n, cap;
  RolltuiStr captured;
  RolltuiStackLevelFn level_fn; /* BORROWED with its ctx: the window table's `back` */
  void* level_ctx;
  /* THE POINTER, where the terminal last reported it. Known from the first report of it moving with no button held, which a
   * terminal sends only when asked for hover: until then a press's cell is not where the pointer rests, since nothing says when it
   * left. */
  unsigned char pointer_known;
  int pointer_x, pointer_y;
  /* AT RUN TIME: the window that fills its layer while maximised ("" for none), and the screen the stack last routed for, which a
   * breakpoint is judged against when focus moves (0 until then: every breakpoint met). */
  RolltuiStr maximized;
  int screen_w, screen_h;
  int numbered; /* each focusable window's title starts with its number (`rolltui_window_stack_set_numbered`): 1 a digit, 2 a superscript */
  RolltuiStr half; /* the window given half the screen (`rolltui_window_stack_set_half`); "" none */
  RolltuiStr label;  /* the action of the title label last pressed */
  int label_pressed; /* …and whether it is still to be taken */
};

void rolltui_window_stack_set_border_role(RolltuiWindowStack* s, const char* id, size_t len, unsigned char role) {
  RolltuiLayoutNode* n = s && id ? rolltui_window_stack_find(s, id, len) : NULL;
  if (n) n->border_role = role < ROLLTUI_ROLE_COUNT ? role : 0;
}

void rolltui_window_stack_set_subtitle(RolltuiWindowStack* s, const char* id, size_t len, const char* text, size_t text_len) {
  RolltuiLayoutNode* n = s && id ? rolltui_window_stack_find(s, id, len) : NULL;
  if (n) rolltui_str_set(&n->subtitle, text ? text : "", text ? text_len : 0);
}

void rolltui_window_stack_set_title_parts(RolltuiWindowStack* s, const char* id, size_t len, const RolltuiTitlePart* parts, size_t n) {
  RolltuiLayoutNode* nd = s && id ? rolltui_window_stack_find(s, id, len) : NULL;
  size_t i;
  if (!nd) return;
  rolltui_str_clear(&nd->title_parts);
  nd->title_part_n = 0;
  for (i = 0; i < n && i < 32 && parts; ++i) {
    const unsigned char role = parts[i].role < ROLLTUI_ROLE_COUNT ? parts[i].role : 0;
    rolltui_str_append(&nd->title_parts, (const char*)&role, 1);
    rolltui_str_append(&nd->title_parts, parts[i].text ? parts[i].text : "", parts[i].text ? parts[i].len : 0);
    rolltui_str_append(&nd->title_parts, "", 1);
    if (parts[i].action) rolltui_str_append(&nd->title_parts, parts[i].action, strlen(parts[i].action));
    rolltui_str_append(&nd->title_parts, "", 1);
    ++nd->title_part_n;
  }
}

int rolltui_window_stack_pressed_label(RolltuiWindowStack* s, RolltuiStr* out) {
  if (!s || !s->label_pressed) return 0;
  s->label_pressed = 0;
  if (out) rolltui_str_set(out, s->label.p ? s->label.p : "", s->label.n);
  return 1;
}

void rolltui_window_stack_set_busy(RolltuiWindowStack* s, const char* id, size_t len, int on, unsigned long long since_ms) {
  RolltuiLayoutNode* n = s && id ? rolltui_window_stack_find(s, id, len) : NULL;
  if (!n) return;
  n->busy = on ? 1 : 0;
  n->busy_since_ms = on ? since_ms : 0;
}

void rolltui_window_stack_set_numbered(RolltuiWindowStack* s, int on) {
  if (s) s->numbered = on == 2 ? 2 : on ? 1 : 0;
}

int rolltui_window_stack_pointer(const RolltuiWindowStack* s, int* x, int* y) {
  if (x) *x = s->pointer_known ? s->pointer_x : -1;
  if (y) *y = s->pointer_known ? s->pointer_y : -1;
  return s->pointer_known;
}

void rolltui_window_stack_set_level_fn(RolltuiWindowStack* s, RolltuiStackLevelFn fn, void* ctx) {
  s->level_fn = fn;
  s->level_ctx = ctx;
}
/* ONE LEVEL AT A TIME: the focused widget closes an inner level of its own if it has one. */
static int close_a_level(RolltuiWindowStack* s, RolltuiStr* window) {
  const RolltuiLayoutNode* f = s->level_fn ? rolltui_window_stack_focused(s) : NULL;
  if (!f || !s->level_fn(s->level_ctx, f->id.p, f->id.n)) return 0;
  rolltui_str_set(window, f->id.p, f->id.n);
  return 1;
}

static const RolltuiLayoutNode* find_in(const RolltuiLayoutNode* n, const char* id, size_t len) {
  size_t i;
  if (len && rolltui_str_eq(&n->id, id, len)) return n;
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* f = find_in(n->children.v[i], id, len);
    if (f) return f;
  }
  return NULL;
}

/* What a layer shows: its root, or while a window of it is maximised, that window alone. */
static const RolltuiLayoutNode* shown_root(const RolltuiWindowStack* s, size_t i) {
  if (s->maximized.n) {
    const RolltuiLayoutNode* m = find_in(&s->layers[i].root, s->maximized.p, s->maximized.n);
    if (m) return m;
  }
  return &s->layers[i].root;
}

/* A LAYER'S BOX: its placement on the screen, or for a popup put at a place, under that place (above it with no room below), kept on
 * the screen. */
static void layer_box(const RolltuiWindowStack* s, size_t i, RolltuiRect screen, RolltuiRect* box) {
  const RolltuiLayer* l = &s->layers[i];
  rolltui_placement_resolve(&l->placement, screen, box);
  if (!l->anchored || i == 0) return;
  box->x = l->anchor.x;
  box->y = l->anchor.y + l->anchor.h;
  if (box->y + box->h > screen.y + screen.h && l->anchor.y - box->h >= screen.y) box->y = l->anchor.y - box->h;
  if (box->x + box->w > screen.x + screen.w) box->x = screen.x + screen.w - box->w;
  if (box->x < screen.x) box->x = screen.x;
  if (box->y + box->h > screen.y + screen.h) box->y = screen.y + screen.h - box->h;
  if (box->y < screen.y) box->y = screen.y;
}

static const RolltuiLayoutNode* layer_focused(const RolltuiWindowStack* s, size_t i);

/* RESOLVES LAYER `i` INTO `screen`: its root, or the maximised window alone, or with a window given half the screen, the rest of the
 * layout in one half and that window in the other. The window holding the focus is the one a split's `focus_weight` grows. */
static void resolve_layer(const RolltuiWindowStack* s, size_t i, RolltuiRect screen, RolltuiResolvedSink sink, void* ctx) {
  RolltuiRect box;
  const RolltuiLayoutNode* focused = layer_focused(s, i);
  RolltuiLayoutNode* half = s->half.n && !s->maximized.n ? (RolltuiLayoutNode*)find_in(&s->layers[i].root, s->half.p, s->half.n) : NULL;
  layer_box(s, i, screen, &box);
  if (half && half != &s->layers[i].root && half->visible) {
    const int wide = box.w >= box.h * 2; /* a cell is about twice as tall as it is wide */
    RolltuiRect rest = box, own = box;
    if (wide) rest.w = box.w / 2, own.x = box.x + rest.w, own.w = box.w - rest.w;
    else rest.h = box.h / 2, own.y = box.y + rest.h, own.h = box.h - rest.h;
    half->visible = 0; /* the rest of the layout without it; then it alone */
    resolve_tree_focused(&s->layers[i].root, rest, screen, i, focused, sink, ctx);
    half->visible = 1;
    resolve_tree_focused(half, own, screen, i, focused, sink, ctx);
    return;
  }
  resolve_tree_focused(shown_root(s, i), box, screen, i, focused, sink, ctx);
}

int rolltui_window_stack_set_half(RolltuiWindowStack* s, const char* id, size_t len) {
  if (!s) return 0;
  if (!id) {
    rolltui_str_clear(&s->half);
    return 1;
  }
  if (!rolltui_window_stack_find(s, id, len)) return 0;
  rolltui_str_set(&s->half, id, len);
  return 1;
}

const char* rolltui_window_stack_half(const RolltuiWindowStack* s, size_t* len) {
  if (len) *len = s ? s->half.n : 0;
  return s && s->half.n ? s->half.p : NULL;
}

void rolltui_window_stack_anchor(RolltuiWindowStack* s, RolltuiRect at) {
  if (!s || s->n < 2) return;
  s->layers[s->n - 1].anchored = 1;
  s->layers[s->n - 1].anchor = at;
}

/* THE LEAST A NODE NEEDS, across (`w`) and down (`h`): its border, padding and gaps, and its children's least along its axis (summed)
 * and across it (the most); a window at least a cell; a node's own fixed size or `min` along its parent's axis. */
static void least_of(const RolltuiLayoutNode* n, int parent_row, int* w, int* h) {
  const int inset = 2 * ((n->border != ROLLTUI_BORDER_NONE ? 1 : 0) + n->padding);
  size_t i, shown = 0;
  int cw = 0, ch = 0;
  if (n->kind == ROLLTUI_NODE_WINDOW || n->children.n == 0) {
    cw = 1, ch = 1;
  } else {
    const int row = n->kind == ROLLTUI_NODE_ROW;
    for (i = 0; i < n->children.n; ++i) {
      int kw, kh;
      if (!n->children.v[i]->visible) continue;
      least_of(n->children.v[i], row, &kw, &kh);
      if (row) cw += kw, ch = kh > ch ? kh : ch;
      else ch += kh, cw = kw > cw ? kw : cw;
      ++shown;
    }
    if (shown > 1) {
      if (row) cw += n->gap * (int)(shown - 1);
      else ch += n->gap * (int)(shown - 1);
    }
  }
  *w = cw + inset;
  *h = ch + inset;
  { /* along the parent's axis, its own size or bound */
    int* along = parent_row ? w : h;
    if (!n->size.fill && n->size.dim.fraction == 0 && n->size.dim.cells > *along) *along = n->size.dim.cells;
    if (n->size_min > *along) *along = n->size_min;
  }
}

int rolltui_layout_fits(const RolltuiLayout* l, int w, int h) {
  int lw, lh;
  const RolltuiLayer* base = l ? rolltui_layout_base(l) : NULL;
  if (!base) return 1;
  least_of(&base->root, 0, &lw, &lh);
  if (base->min_width > lw) lw = base->min_width;
  if (base->min_height > lh) lh = base->min_height;
  return w >= lw && h >= lh;
}

/* No list. This runs once per layer per resolve — three times a frame — and only ever needs
 * the FIRST focusable or the one matching a name. */
static const RolltuiLayoutNode* first_focusable(const RolltuiLayoutNode* n, int sw, int sh) {
  size_t i;
  if (!n->visible || (sw > 0 && !shown_at(n, sw, sh))) return NULL;
  if (n->kind == ROLLTUI_NODE_WINDOW) return n->focusable ? n : NULL;
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* f = first_focusable(n->children.v[i], sw, sh);
    if (f) return f;
  }
  return NULL;
}

static const RolltuiLayoutNode* focusable_named(const RolltuiLayoutNode* n, const char* id, size_t len, int sw, int sh) {
  size_t i;
  if (!n->visible || (sw > 0 && !shown_at(n, sw, sh))) return NULL;
  if (n->kind == ROLLTUI_NODE_WINDOW) return (n->focusable && rolltui_str_eq(&n->id, id, len)) ? n : NULL;
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* f = focusable_named(n->children.v[i], id, len, sw, sh);
    if (f) return f;
  }
  return NULL;
}

/* The window that holds the keys in layer `i`: the one named, while it is shown, else the first shown. */
static const RolltuiLayoutNode* layer_focused(const RolltuiWindowStack* s, size_t i) {
  const RolltuiLayer* l = &s->layers[i];
  const RolltuiLayoutNode* root = shown_root(s, i);
  if (l->focus.n) {
    const RolltuiLayoutNode* named = focusable_named(root, l->focus.p, l->focus.n, s->screen_w, s->screen_h);
    if (named) return named;
  }
  return first_focusable(root, s->screen_w, s->screen_h);
}

/* The focusables of one layer, in tree order, into a caller's array. Returns how many there
 * ARE, so a caller that only wants the count passes a zero cap — the shape that lets
 * `cycle_focus` and `route` share one walk without either building a list. */
static size_t focusables_at(const RolltuiLayoutNode* n, const RolltuiLayoutNode** out, size_t cap, size_t at, int sw, int sh) {
  size_t i;
  if (!n->visible || (sw > 0 && !shown_at(n, sw, sh))) return at;
  if (n->kind == ROLLTUI_NODE_WINDOW) {
    if (!n->focusable) return at;
    if (at < cap) out[at] = n;
    return at + 1;
  }
  for (i = 0; i < n->children.n; ++i) at = focusables_at(n->children.v[i], out, cap, at, sw, sh);
  return at;
}

/* The focusable windows layer `i` shows, on the screen last routed for. */
static size_t focusables(const RolltuiWindowStack* s, size_t i, const RolltuiLayoutNode** out, size_t cap) {
  return focusables_at(shown_root(s, i), out, cap, 0, s->screen_w, s->screen_h);
}

RolltuiWindowStack* rolltui_window_stack_new(void) {
  RolltuiWindowStack* s = (RolltuiWindowStack*)rolltui_mem_alloc(sizeof *s);
  memset(s, 0, sizeof *s);
  s->layers = (RolltuiLayer*)rolltui_grow_zeroed(s->layers, &s->cap, 1, sizeof *s->layers);
  rolltui_layer_init(&s->layers[0]);
  s->n = 1;
  s->level_fn = NULL;
  s->level_ctx = NULL;
  return s;
}

void rolltui_window_stack_free(RolltuiWindowStack* s) {
  size_t i;
  if (!s) return;
  for (i = 0; i < s->n; ++i) rolltui_layer_release(&s->layers[i]);
  rolltui_mem_free(s->layers);
  rolltui_str_free(&s->captured);
  rolltui_str_free(&s->maximized);
  rolltui_str_free(&s->label);
  rolltui_str_free(&s->half);
  rolltui_mem_free(s);
}

/* The tab each tabbed window of `n` shows, by window id, into `ids`/`tabs`. */
#define KEEP_TABS 32
static size_t tabs_of(const RolltuiLayoutNode* n, RolltuiStr* ids, unsigned char* tabs, size_t at) {
  size_t i;
  if (n->tab_count && at < KEEP_TABS) {
    memset(&ids[at], 0, sizeof ids[at]);
    rolltui_str_set(&ids[at], n->id.p, n->id.n);
    tabs[at++] = n->tab;
  }
  for (i = 0; i < n->children.n; ++i) at = tabs_of(n->children.v[i], ids, tabs, at);
  return at;
}

static void show_tab(RolltuiLayoutNode* n, size_t tab) {
  const char* c;
  size_t cl = 0;
  if (!rolltui_node_tab_at(n, tab, NULL, NULL, &c, &cl)) return;
  n->tab = (unsigned char)tab;
  rolltui_str_set(&n->content, c, cl);
}

/* A NEW BASE keeps what the person chose on the old one where it still applies: the focus, each tabbed window's tab, the maximised
 * window. What the stack was told at run time about a window's visibility or size is the file's again. */
void rolltui_window_stack_set_base(RolltuiWindowStack* s, const RolltuiLayer* base) {
  RolltuiStr keep, ids[KEEP_TABS];
  unsigned char tabs[KEEP_TABS];
  size_t i, nt;
  memset(&keep, 0, sizeof keep);
  rolltui_str_set(&keep, s->layers[0].focus.p, s->layers[0].focus.n);
  nt = tabs_of(&s->layers[0].root, ids, tabs, 0);
  rolltui_layer_copy(&s->layers[0], base);
  if (base->focus.n == 0 && keep.n && find_in(&s->layers[0].root, keep.p, keep.n))
    rolltui_str_move(&s->layers[0].focus, &keep);
  rolltui_str_free(&keep);
  for (i = 0; i < nt; ++i) {
    RolltuiLayoutNode* n = (RolltuiLayoutNode*)find_in(&s->layers[0].root, ids[i].p, ids[i].n);
    if (n && tabs[i] < n->tab_count) show_tab(n, tabs[i]);
    rolltui_str_free(&ids[i]);
  }
  if (s->maximized.n && !find_in(&s->layers[0].root, s->maximized.p, s->maximized.n)) rolltui_str_clear(&s->maximized);
}

int rolltui_window_stack_set_visible(RolltuiWindowStack* s, const char* id, size_t len, int visible) {
  RolltuiLayoutNode* n = rolltui_window_stack_find(s, id, len);
  if (!n) return 0;
  if (visible && !n->visible && s->screen_w > 0 && s->screen_h > 0) { /* shown, would the layout still fit the screen it has? */
    int lw, lh;
    n->visible = 1;
    least_of(&s->layers[0].root, 0, &lw, &lh);
    if (lw > s->screen_w || lh > s->screen_h) {
      n->visible = 0;
      return ROLLTUI_STACK_REFUSED;
    }
    return 1;
  }
  n->visible = (unsigned char)(visible != 0);
  return 1;
}

int rolltui_window_stack_set_size(RolltuiWindowStack* s, const char* id, size_t len, const char* size, size_t size_len) {
  RolltuiLayoutNode* n = rolltui_window_stack_find(s, id, len);
  RolltuiSplitSizeRaw ss;
  size_t i;
  int cells = 0;
  if (!n || !size || size_len == 0) return 0;
  for (i = 0; i < size_len && size[i] >= '0' && size[i] <= '9' && cells < 100000; ++i) cells = cells * 10 + (size[i] - '0');
  if (i == size_len) { /* cells, which a file writes as a number */
    n->size.fill = 0;
    n->size.weight = 1;
    n->size.dim.fraction = 0;
    n->size.dim.cells = cells;
    return 1;
  }
  if (!rolltui_parse_split_size(size, size_len, &ss)) return 0;
  n->size = ss;
  return 1;
}

int rolltui_window_stack_maximize(RolltuiWindowStack* s, const char* id, size_t len) {
  if (!id || len == 0) {
    rolltui_str_clear(&s->maximized);
    return 1;
  }
  {
    const RolltuiLayoutNode* n = rolltui_window_stack_find(s, id, len);
    if (!n || n->kind != ROLLTUI_NODE_WINDOW) return 0;
    rolltui_str_set(&s->maximized, id, len);
    if (n->focusable) rolltui_window_stack_focus(s, id, len); /* the keys go where everything else went */
  }
  return 1;
}

const char* rolltui_window_stack_maximized(const RolltuiWindowStack* s, size_t* len) { return rolltui_str_get(&s->maximized, len); }

int rolltui_window_stack_set_tab(RolltuiWindowStack* s, const char* id, size_t len, size_t tab) {
  RolltuiLayoutNode* n = rolltui_window_stack_find(s, id, len);
  if (!n || tab >= n->tab_count) return 0;
  show_tab(n, tab);
  return 1;
}

size_t rolltui_window_stack_tab(const RolltuiWindowStack* s, const char* id, size_t len, size_t* count) {
  const RolltuiLayoutNode* n = rolltui_window_stack_find((RolltuiWindowStack*)s, id, len);
  if (count) *count = n ? n->tab_count : 0;
  return n ? n->tab : 0;
}

RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s) { return &s->layers[0]; }

void rolltui_window_stack_push(RolltuiWindowStack* s, RolltuiLayer* popup) {
  s->layers = (RolltuiLayer*)rolltui_grow_zeroed(s->layers, &s->cap, s->n + 1, sizeof *s->layers);
  rolltui_layer_init(&s->layers[s->n]);
  rolltui_layer_move(&s->layers[s->n], popup);
  ++s->n;
}

/* The copy is made straight into the stack's own new slot rather than into a local that is
 * then moved out of: there is no intermediate to get the ownership of wrong, which is the
 * whole point of the call existing (see the header). */
int rolltui_window_stack_push_popup(RolltuiWindowStack* s, const RolltuiLayout* layout, const char* id, size_t len) {
  const RolltuiLayer* p = layout != NULL ? rolltui_layout_popup(layout, id, len) : NULL;
  if (p == NULL) return 0;
  s->layers = (RolltuiLayer*)rolltui_grow_zeroed(s->layers, &s->cap, s->n + 1, sizeof *s->layers);
  rolltui_layer_init(&s->layers[s->n]);
  rolltui_layer_copy(&s->layers[s->n], p);
  ++s->n;
  return 1;
}

int rolltui_window_stack_pop(RolltuiWindowStack* s) {
  if (s->n <= 1) return 0;
  rolltui_layer_release(&s->layers[--s->n]);
  return 1;
}

size_t rolltui_window_stack_depth(const RolltuiWindowStack* s) { return s->n; }

const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, size_t i) {
  return i < s->n ? &s->layers[i] : NULL;
}

int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, size_t len) {
  size_t i;
  for (i = 1; i < s->n; ++i)
    if (rolltui_str_eq(&s->layers[i].id, id, len)) return 1;
  return 0;
}

/* Three hosts hand-wrote these six lines, one per panel, so they are here instead. An `app.<id>`
 * whose id names a popup the screen declares toggles it; anything else is not this function's and
 * returns 0 so the caller can go on to its own actions. */
int rolltui_window_stack_action_popup(RolltuiWindowStack* s, const RolltuiLayout* layout,
                                      const char* action, size_t len) {
  const char* id;
  size_t id_len;
  if (!s || !layout || !action || len <= 4) return 0;
  if (memcmp(action, "app.", 4) != 0) return 0;
  id = action + 4;
  id_len = len - 4;
  if (rolltui_layout_popup(layout, id, id_len) == NULL) return 0;
  if (rolltui_window_stack_has_popup(s, id, id_len)) {
    /* Pop back to the base rather than popping once: a panel key pressed over a stack of panels
     * means "put this away", and popping one would leave whatever it was covering. */
    while (rolltui_window_stack_depth(s) > 1) rolltui_window_stack_pop(s);
    return 1;
  }
  if (!rolltui_window_stack_push_popup(s, layout, id, id_len)) return 1;
  /* AND FOCUS IT. A panel that opens without the focus is a panel a person cannot use: the next
   * key goes wherever it was going before, which for a screen with an input means their choice is
   * typed into the prompt. Focusing by the popup's own id works because a popup's root carries the
   * id the layout gave it; a popup whose root is not focusable keeps the focus where it was, which
   * is the right answer for one that only displays. */
  rolltui_window_stack_focus(s, id, id_len);
  return 1;
}

RolltuiLayoutNode* rolltui_window_stack_find(const RolltuiWindowStack* s, const char* id, size_t len) {
  size_t i;
  for (i = 0; i < s->n; ++i) {
    const RolltuiLayoutNode* f = find_in(&s->layers[i].root, id, len);
    if (f) return (RolltuiLayoutNode*)f;
  }
  return NULL;
}

size_t rolltui_window_stack_focus_layer(const RolltuiWindowStack* s) {
  const size_t top = s->n - 1;
  size_t i;
  /* ON = the confinement gone, so focus falls through a modal to whatever layer under it
   * happens to hold it. The modal is still drawn and still tints the screen; keys just go
   * somewhere behind it, which looks like the modal ignoring input rather than like a bug. */
  if (!testkit_ctl_on("layout.a_modal_does_not_confine_focus") && s->layers[top].modal) return top;
  for (i = s->n; i-- > 0;)
    if (layer_focused(s, i)) return i;
  return top;
}

const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s) {
  return layer_focused(s, rolltui_window_stack_focus_layer(s));
}

void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, size_t len) {
  const size_t li = rolltui_window_stack_focus_layer(s);
  RolltuiLayer* l = &s->layers[li];
  if (focusable_named(shown_root(s, li), id, len, s->screen_w, s->screen_h)) rolltui_str_set(&l->focus, id, len);
}

/* THE ONE PLACE A LIST OF FOCUSABLES IS ACTUALLY NEEDED — cycling has to know the NEXT one.
 * An inline array with a stated spill, the same shape `place` uses, because a screen with
 * more than sixteen focusable windows is possible and must not be assumed away. */
#define FOCUS_INLINE 16

void rolltui_window_stack_cycle_focus(RolltuiWindowStack* s, int backwards) {
  const size_t li = rolltui_window_stack_focus_layer(s);
  RolltuiLayer* l = &s->layers[li];
  const RolltuiLayoutNode* inline_f[FOCUS_INLINE];
  const RolltuiLayoutNode** f = inline_f;
  const RolltuiLayoutNode** spill = NULL;
  const RolltuiLayoutNode* cur;
  size_t count = focusables(s, li, NULL, 0);
  size_t i;
  if (count == 0) return;
  if (count > FOCUS_INLINE) {
    spill = (const RolltuiLayoutNode**)rolltui_mem_alloc(count * sizeof *spill);
    f = spill;
  }
  focusables(s, li, f, count);
  cur = layer_focused(s, li);
  for (i = 0; i < count; ++i)
    if (f[i] == cur) break;
  if (i >= count) i = 0;
  i = backwards ? (i + count - 1) % count : (i + 1) % count;
  rolltui_str_set(&l->focus, f[i]->id.p, f[i]->id.n);
  rolltui_mem_free(spill);
}

/* The sink wrapper the stack uses to stamp `focused` and the layer index onto every node on
 * its way past — so a caller's own sink sees a finished node and the stack never buffers. */
typedef struct StackSink {
  RolltuiResolvedSink emit;
  void* ctx;
  const RolltuiLayoutNode* focused;
  const RolltuiLayoutNode* hover; /* the window the pointer rests on; NULL for none */
  int hover_x, hover_y;
} StackSink;

static void stack_sink(void* ctx, const RolltuiResolvedNode* rn) {
  StackSink* s = (StackSink*)ctx;
  RolltuiResolvedNode out = *rn;
  /* While the terminal's window has lost the person's focus, no window here has it: every border and cursor draws as away. */
  out.focused = (unsigned char)(rn->node == s->focused && rolltui_termfacts_active_focused());
  if (s->hover && rn->node == s->hover) {
    out.hover = 1;
    out.hover_x = s->hover_x;
    out.hover_y = s->hover_y;
  }
  s->emit(s->ctx, &out);
}

typedef struct HoverFind {
  RolltuiRect screen;
  int x, y;
  const RolltuiLayoutNode* node;
  size_t layer;
} HoverFind;

/* Every window is visited in draw order, so the last one under the pointer is the one drawn over the rest. */
static void hover_sink(void* ctx, const RolltuiResolvedNode* rn) {
  HoverFind* h = (HoverFind*)ctx;
  if (rn->node->kind == ROLLTUI_NODE_WINDOW && rect_contains(rect_intersect(rn->outer, h->screen), h->x, h->y)) {
    h->node = rn->node;
    h->layer = rn->layer;
  }
}

/* THE WINDOW THE POINTER RESTS ON is the one a press there would reach, by `route`'s own rules: the topmost window under it, and
 * none at all when that window lies under a popup that is modal or closes on a press outside it, since a press there would be
 * dropped or would close the popup, and a row lit there would promise a click it will not get. */
static void stack_hover(const RolltuiWindowStack* s, RolltuiRect screen, StackSink* w) {
  HoverFind h;
  size_t i;
  const size_t top = s->n - 1;
  w->hover = NULL;
  w->hover_x = w->hover_y = 0;
  if (!s->pointer_known) return;
  h.screen = screen;
  h.x = s->pointer_x;
  h.y = s->pointer_y;
  h.node = NULL;
  h.layer = 0;
  for (i = 0; i < s->n; ++i) {
    resolve_layer(s, i, screen, hover_sink, &h);
  }
  if (!h.node || (h.layer != top && (s->layers[top].modal || s->layers[top].dismiss))) return;
  w->hover = h.node;
  w->hover_x = h.x;
  w->hover_y = h.y;
}

void rolltui_window_stack_resolve(const RolltuiWindowStack* s, RolltuiRect screen, RolltuiResolvedSink emit,
                                  void* ctx) {
  StackSink w;
  size_t i;
  w.emit = emit;
  w.ctx = ctx;
  w.focused = rolltui_window_stack_focused(s);
  stack_hover(s, screen, &w);
  for (i = 0; i < s->n; ++i) {
    resolve_layer(s, i, screen, stack_sink, &w);
  }
}

void rolltui_window_stack_compose(const RolltuiWindowStack* s, RolltuiFrame* f, RolltuiRect screen,
                                  const RolltuiStyle* styles, const RolltuiLayoutRoles* roles,
                                  RolltuiSlotFn render, void* ctx, int ambiguous_wide,
                                  RolltuiComposeScratch* scratch) {
  /* THE PER-LAYER BUFFER IS THE SCRATCH'S, not a local: a compose runs every frame, and a
   * buffer allocated here is one allocation plus its growth per frame — exactly what the
   * budget exists to make impossible to leave in. */
  StackSink w;
  size_t i;
  /* A SCREEN SMALLER THAN THE LAYOUT NEEDS is told so, centred on the layout's ground, rather than given a layout squeezed past use. */
  if (s->layers[0].min_width > screen.w || s->layers[0].min_height > screen.h) {
    char msg[96];
    const char* x = ambiguous_wide ? "x" : "\xC3\x97"; /* × */
    int n = snprintf(msg, sizeof msg, "the screen is %d%s%d; this needs %d%s%d", screen.w, x, screen.h, s->layers[0].min_width, x,
                     s->layers[0].min_height);
    int mw;
    RolltuiStyle ground = styles[s->layers[0].root.background], text = styles[roles->title];
    text.bg = ground.bg;
    rolltui_frame_fill(f, scratch->draw, screen, ground, NULL, 0);
    if (n < 0) return;
    mw = rolltui_frame_text_width(scratch->draw, msg, (size_t)n, ambiguous_wide);
    if (mw > screen.w) { /* the short form */
      n = snprintf(msg, sizeof msg, "%d%s%d < %d%s%d", screen.w, x, screen.h, s->layers[0].min_width, x, s->layers[0].min_height);
      mw = n > 0 ? rolltui_frame_text_width(scratch->draw, msg, (size_t)n, ambiguous_wide) : 0;
    }
    if (n > 0 && mw <= screen.w) {
      const int nx = screen.x + (screen.w - mw) / 2, ny = screen.y + (screen.h - 1) / 2;
      char now[24], need[24];
      const int nn = snprintf(now, sizeof now, "%d%s%d", screen.w, x, screen.h);
      const int dn = snprintf(need, sizeof need, "%d%s%d", s->layers[0].min_width, x, s->layers[0].min_height);
      rolltui_frame_put_text(f, scratch->draw, nx, ny, msg, (size_t)n, text, mw, ambiguous_wide, 0);
      { /* THE TWO SIZES IN THEIR OWN ROLES: what the screen is, and what it needs; bold, so they read in every theme */
        const char* hit = nn > 0 ? strstr(msg, now) : NULL;
        const char* want = dn > 0 ? strstr(hit ? hit + nn : msg, need) : NULL;
        RolltuiStyle es = styles[roles->error ? roles->error : roles->title], os = styles[roles->ok ? roles->ok : roles->title];
        es.bg = ground.bg, os.bg = ground.bg;
        es.bold = os.bold = 1;
        if (hit) {
          const RolltuiRect r = {nx + rolltui_frame_text_width(scratch->draw, msg, (size_t)(hit - msg), ambiguous_wide), ny,
                                 rolltui_frame_text_width(scratch->draw, now, (size_t)nn, ambiguous_wide), 1};
          rolltui_frame_tint(f, r, es);
        }
        if (want) {
          const RolltuiRect r = {nx + rolltui_frame_text_width(scratch->draw, msg, (size_t)(want - msg), ambiguous_wide), ny,
                                 rolltui_frame_text_width(scratch->draw, need, (size_t)dn, ambiguous_wide), 1};
          rolltui_frame_tint(f, r, os);
        }
      }
    }
    return;
  }
  w.emit = collect_sink;
  w.ctx = &scratch->nodes;
  w.focused = rolltui_window_stack_focused(s);
  stack_hover(s, screen, &w);
  for (i = 0; i < s->n; ++i) {
    RolltuiRect box;
    if (s->layers[i].modal) rolltui_frame_shade(f, screen, styles[roles->overlay], ROLLTUI_SHADE_MODAL);
    scratch->nodes.n = 0;
    layer_box(s, i, screen, &box);
    /* The cells this popup covers are its now: a mark a lower layer left on them would put
     * that layer's motion onto the popup when the effects are applied to the finished frame. */
    if (i > 0) rolltui_frame_unmark_rect(f, box);
    resolve_layer(s, i, screen, stack_sink, &w);
    scratch->numbered_n = s->numbered && i == rolltui_window_stack_focus_layer(s) ? focusables(s, i, scratch->numbered, 9) : 0;
    scratch->numbered_sup = s->numbered == 2;
    if (scratch->numbered_n > 9) scratch->numbered_n = 9;
    rolltui_compose_layer(f, scratch->nodes.v, scratch->nodes.n, styles, roles, render, ctx, ambiguous_wide,
                          scratch);
    scratch->numbered_n = 0;
  }
}

/* FOCUS BY DIRECTION (0 left, 1 right, 2 up, 3 down): the focusable window of the focus layer nearest the focused one that way, by
 * how far it lies along that way, with how far it lies across counting double. 1 when the focus moved. */
typedef struct { const RolltuiLayoutNode* node; RolltuiRect r; } FocusRect;
typedef struct { FocusRect* v; size_t n, cap; size_t layer; } FocusRects;
static void focus_rect_collect(void* ctx, const RolltuiResolvedNode* rn) {
  FocusRects* fr = (FocusRects*)ctx;
  if (rn->layer != fr->layer || !rn->node || rn->node->kind != ROLLTUI_NODE_WINDOW || !rn->node->focusable) return;
  fr->v = (FocusRect*)rolltui_grow(fr->v, &fr->cap, fr->n + 1, sizeof *fr->v);
  fr->v[fr->n].node = rn->node;
  fr->v[fr->n].r = rn->outer;
  ++fr->n;
}
static int focus_toward(RolltuiWindowStack* s, RolltuiRect screen, int dir, RolltuiStr* window) {
  FocusRects fr;
  const RolltuiLayoutNode* f = rolltui_window_stack_focused(s);
  const FocusRect* from = NULL;
  const FocusRect* best = NULL;
  long best_score = 0;
  size_t i;
  if (!f) return 0;
  memset(&fr, 0, sizeof fr);
  fr.layer = rolltui_window_stack_focus_layer(s);
  rolltui_window_stack_resolve(s, screen, focus_rect_collect, &fr);
  for (i = 0; i < fr.n; ++i)
    if (fr.v[i].node == f || rolltui_str_eq(&fr.v[i].node->id, f->id.p, f->id.n)) from = &fr.v[i];
  if (from) {
    const long fx = from->r.x * 2 + from->r.w, fy = from->r.y * 2 + from->r.h; /* centres, doubled */
    for (i = 0; i < fr.n; ++i) {
      const FocusRect* c = &fr.v[i];
      const long cx = c->r.x * 2 + c->r.w, cy = c->r.y * 2 + c->r.h;
      long along, across, score;
      if (c == from) continue;
      if (dir == 0) along = fx - cx, across = cy - fy;
      else if (dir == 1) along = cx - fx, across = cy - fy;
      else if (dir == 2) along = fy - cy, across = cx - fx;
      else along = cy - fy, across = cx - fx;
      if (along <= 0) continue;
      if (across < 0) across = -across;
      score = along + across * 2;
      if (!best || score < best_score) best = c, best_score = score;
    }
  }
  if (best) {
    rolltui_str_set(&s->layers[fr.layer].focus, best->node->id.p, best->node->id.n);
    rolltui_str_set(window, best->node->id.p, best->node->id.n);
  }
  rolltui_mem_free(fr.v);
  return best != NULL;
}

unsigned char rolltui_window_stack_route(RolltuiWindowStack* s, const RolltuiEvent* e, RolltuiRect screen,
                                         const RolltuiBindings* bindings, const RolltuiStackActions* actions,
                                         RolltuiStr* window) {
  rolltui_str_clear(window);
  s->screen_w = screen.w; /* what a breakpoint is judged against when focus moves */
  s->screen_h = screen.h;
  if (s->layers[0].min_width > screen.w || s->layers[0].min_height > screen.h) { /* the note is up: keys still reach the focus */
    if (e->kind == ROLLTUI_EVENT_MOUSE) return ROLLTUI_ROUTE_DROPPED;
  } else if (e->kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent* m = &e->mouse;
    NodeCollect c;
    const size_t top = s->n - 1;
    size_t k;
    unsigned char verdict = ROLLTUI_ROUTE_DROPPED;
    if (m->kind == 3 /* Move */) s->pointer_known = 1;
    if (s->pointer_known) {
      s->pointer_x = m->x;
      s->pointer_y = m->y;
    }
    /* A captured pointer: drags and the release go to the pressed window, wherever the
     * pointer is now (the window may even have gone: then the capture just ends). */
    if (s->captured.n && (m->kind == 2 /* Drag */ || m->kind == 1 /* Release */)) {
      RolltuiStr target;
      memset(&target, 0, sizeof target);
      rolltui_str_set(&target, s->captured.p, s->captured.n);
      if (m->kind == 1) rolltui_str_clear(&s->captured);
      if (rolltui_window_stack_find(s, target.p, target.n)) {
        rolltui_str_move(window, &target);
        return ROLLTUI_ROUTE_DELIVER;
      }
      rolltui_str_free(&target);
      return ROLLTUI_ROUTE_DROPPED;
    }
    c.v = NULL;
    c.n = 0;
    c.cap = 0;
    {
      StackSink w;
      size_t i;
      w.emit = collect_sink;
      w.ctx = &c;
      w.focused = rolltui_window_stack_focused(s);
      w.hover = NULL;
      w.hover_x = w.hover_y = 0;
      for (i = 0; i < s->n; ++i) {
        resolve_layer(s, i, screen, stack_sink, &w);
      }
    }
    for (k = c.n; k-- > 0;) {
      const RolltuiResolvedNode* rn = &c.v[k];
      if (rn->node->kind != ROLLTUI_NODE_WINDOW || !rect_contains(rect_intersect(rn->outer, screen), m->x, m->y))
        continue;
      if (rn->layer != top && m->kind == 0 && s->layers[top].dismiss) break; /* outside: closes, below */
      if (s->layers[top].modal && rn->layer != top) break; /* dropped under a modal */
      if (m->kind == 0 /* Press */ && m->button == 1 && rn->node->tab_count && m->y == rn->outer.y && rn->node->border) {
        /* A PRESS ON THE STRIP shows that tab: the strip is the window's own border, not its content */
        int xs[TAB_MAX], ws[TAB_MAX];
        RolltuiDrawScratch* draw = rolltui_draw_scratch_new();
        const size_t nl = tab_labels(rn->node, rn->outer, draw, rolltui_termfacts_active_wide(), xs, ws);
        size_t t;
        rolltui_draw_scratch_free(draw);
        for (t = 0; t < nl; ++t)
          if (m->x >= xs[t] && m->x < xs[t] + ws[t]) {
            RolltuiLayoutNode* nd = rolltui_window_stack_find(s, rn->node->id.p, rn->node->id.n);
            if (nd) show_tab(nd, t);
            if (rn->node->focusable && rn->layer == rolltui_window_stack_focus_layer(s))
              rolltui_window_stack_focus(s, rn->node->id.p, rn->node->id.n);
            rolltui_str_set(window, rn->node->id.p, rn->node->id.n);
            rolltui_mem_free(c.v);
            return ROLLTUI_ROUTE_LAYOUT_CHANGED;
          }
      }
      if (m->kind == 0 /* Press */ && m->button == 1 && rn->node->title_part_n && m->y == rn->outer.y && rn->node->border) {
        /* A PRESS ON A LABEL in the title runs its action: the host takes it */
        int xs[32], ws[32];
        RolltuiDrawScratch* draw = rolltui_draw_scratch_new();
        const int aw = rolltui_termfacts_active_wide();
        size_t np, t;
        int x0 = rn->outer.x + 2;
        {
          RolltuiComposeScratch pre;
          int spin_at;
          RolltuiStr prefix;
          memset(&pre, 0, sizeof pre);
          memset(&prefix, 0, sizeof prefix);
          pre.numbered_n = s->numbered && rn->layer == rolltui_window_stack_focus_layer(s) ? focusables(s, rn->layer, pre.numbered, 9) : 0;
          if (pre.numbered_n > 9) pre.numbered_n = 9;
          title_prefix(&pre, rn->node, s->numbered == 2 && !aw, 1, &prefix, &spin_at);
          x0 += rolltui_frame_text_width(draw, prefix.p ? prefix.p : "", prefix.n, aw);
          rolltui_str_free(&prefix);
        }
        np = title_part_cells(rn->node, draw, x0, aw, xs, ws, 32);
        rolltui_draw_scratch_free(draw);
        for (t = 0; t < np; ++t) {
          unsigned char role;
          const char *text, *action;
          size_t len;
          if (m->x < xs[t] || m->x >= xs[t] + ws[t]) continue;
          if (!title_part(rn->node, t, &role, &text, &len, &action) || !action) break;
          rolltui_str_set(&s->label, action, strlen(action));
          s->label_pressed = 1;
          rolltui_str_set(window, rn->node->id.p, rn->node->id.n);
          rolltui_mem_free(c.v);
          return ROLLTUI_ROUTE_LABEL;
        }
      }
      if (m->kind == 0 /* Press */) {
        if (rn->node->focusable && rn->layer == rolltui_window_stack_focus_layer(s))
          rolltui_window_stack_focus(s, rn->node->id.p, rn->node->id.n);
        rolltui_str_set(&s->captured, rn->node->id.p, rn->node->id.n);
      }
      rolltui_str_set(window, rn->node->id.p, rn->node->id.n);
      verdict = ROLLTUI_ROUTE_DELIVER;
      break;
    }
    rolltui_mem_free(c.v);
    /* A PRESS OUTSIDE A POPUP THAT DISMISSES closes it, exactly as the close key does: the
     * popup's id is the answer and the press is spent — it does not also land on what was
     * under it, so closing a menu never chooses a file. A drag or a release is not a press. */
    if (verdict == ROLLTUI_ROUTE_DROPPED && m->kind == 0 && top > 0 && s->layers[top].dismiss) {
      if (close_a_level(s, window)) return ROLLTUI_ROUTE_CLOSED_LEVEL;
      rolltui_str_set(window, s->layers[top].id.p, s->layers[top].id.n);
      rolltui_window_stack_pop(s);
      return ROLLTUI_ROUTE_CLOSED_POPUP;
    }
    return verdict;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    size_t alen = 0;
    const char* action = rolltui_bindings_action_for(bindings, &e->key, "stack", 5, &alen);
    if (action) {
      if (alen == strlen(actions->close_popup) && memcmp(action, actions->close_popup, alen) == 0 && s->n > 1) {
        if (close_a_level(s, window)) return ROLLTUI_ROUTE_CLOSED_LEVEL;
        rolltui_str_set(window, s->layers[s->n - 1].id.p, s->layers[s->n - 1].id.n);
        rolltui_window_stack_pop(s);
        return ROLLTUI_ROUTE_CLOSED_POPUP;
      }
      {
        /* THE FOCUSED WINDOW'S TABS, and filling its layer: a window that has no tabs lets the key go on to it */
        const RolltuiLayoutNode* f = rolltui_window_stack_focused(s);
        const int next_tab = actions->next_tab && alen == strlen(actions->next_tab) && memcmp(action, actions->next_tab, alen) == 0;
        const int prev_tab = actions->prev_tab && alen == strlen(actions->prev_tab) && memcmp(action, actions->prev_tab, alen) == 0;
        const int maximize = actions->maximize && alen == strlen(actions->maximize) && memcmp(action, actions->maximize, alen) == 0;
        const int half = actions->half && alen == strlen(actions->half) && memcmp(action, actions->half, alen) == 0;
        if (f && (next_tab || prev_tab) && f->tab_count > 1) {
          RolltuiLayoutNode* nd = rolltui_window_stack_find(s, f->id.p, f->id.n);
          const size_t count = f->tab_count;
          show_tab(nd, next_tab ? (f->tab + 1) % count : (f->tab + count - 1) % count);
          rolltui_str_set(window, nd->id.p, nd->id.n);
          return ROLLTUI_ROUTE_LAYOUT_CHANGED;
        }
        if (f && half) { /* half the screen, and back */
          const int same = s->half.n && rolltui_str_eq(&s->half, f->id.p, f->id.n);
          rolltui_window_stack_set_half(s, same ? NULL : f->id.p, f->id.n);
          rolltui_str_set(window, f->id.p, f->id.n);
          return ROLLTUI_ROUTE_LAYOUT_CHANGED;
        }
        if (f && maximize) {
          size_t ml = 0;
          const char* m = rolltui_window_stack_maximized(s, &ml);
          if (ml) rolltui_window_stack_maximize(s, NULL, 0);
          else rolltui_window_stack_maximize(s, f->id.p, f->id.n);
          (void)m;
          rolltui_str_set(window, f->id.p, f->id.n);
          return ROLLTUI_ROUTE_LAYOUT_CHANGED;
        }
      }
      if (actions->focus_number && alen == strlen(actions->focus_number) && memcmp(action, actions->focus_number, alen) == 0 &&
          e->key.key == ROLLTUI_KEY_CHAR && e->key.ch >= '1' && e->key.ch <= '9') { /* THE WINDOW WITH THAT NUMBER, in focus order */
        const RolltuiLayoutNode* list[9];
        const size_t li = rolltui_window_stack_focus_layer(s), n = focusables(s, li, list, 9), want = (size_t)(e->key.ch - '1');
        if (want < n && want < 9) {
          rolltui_str_set(&s->layers[li].focus, list[want]->id.p, list[want]->id.n);
          rolltui_str_set(window, list[want]->id.p, list[want]->id.n);
          return ROLLTUI_ROUTE_FOCUS_MOVED;
        }
      }
      {
        const char* names[4];
        int d;
        names[0] = actions->focus_left, names[1] = actions->focus_right, names[2] = actions->focus_up, names[3] = actions->focus_down;
        for (d = 0; d < 4; ++d)
          if (names[d] && alen == strlen(names[d]) && memcmp(action, names[d], alen) == 0 && focus_toward(s, screen, d, window))
            return ROLLTUI_ROUTE_FOCUS_MOVED;
      }
      {
        const int next = alen == strlen(actions->focus_next) && memcmp(action, actions->focus_next, alen) == 0;
        const int prev = alen == strlen(actions->focus_prev) && memcmp(action, actions->focus_prev, alen) == 0;
        if (next || prev) {
          if (focusables(s, rolltui_window_stack_focus_layer(s), NULL, 0) > 1) {
            const RolltuiLayoutNode* f;
            rolltui_window_stack_cycle_focus(s, prev);
            f = rolltui_window_stack_focused(s);
            rolltui_str_set(window, f->id.p, f->id.n);
            return ROLLTUI_ROUTE_FOCUS_MOVED;
          }
        }
      }
    }
  }
  {
    const RolltuiLayoutNode* f = rolltui_window_stack_focused(s);
    if (!f) return ROLLTUI_ROUTE_DROPPED;
    rolltui_str_set(window, f->id.p, f->id.n);
    return ROLLTUI_ROUTE_DELIVER;
  }
}

const char* rolltui_window_stack_captured(const RolltuiWindowStack* s, size_t* len) {
  return rolltui_str_get(&s->captured, len);
}

/* ---- THE SHIPPED SCREEN'S OWN ACTIONS AND THE BUILT-IN LAYOUTS — see the header -------------
 * OWNED, LONG-LIVED (CLAUDE.md strategy 4), and A SESSION'S rather than the process's.
 * Both are parsed once from the same embedded `const` bytes, so every context
 * ends up with identical CONTENT and its own STORAGE — which is contract point 4: a cached
 * built-in belongs to the context that cached it, and a `RolltuiLayout*` handed out here must
 * not outlive the session that parsed it. The shutdown hooks both used to register went with
 * the globals; `rolltui_context_free` releases the cache by name. */

typedef struct {
  const char* name; /* BORROWED: the embedded table's own literal */
  size_t name_len;
  RolltuiLayout layout;
} BuiltinLayout;

struct RolltuiLayoutCache {
  RolltuiLayoutAction* actions;
  size_t actions_n, actions_cap;
  int actions_done;
  BuiltinLayout* layouts;
  size_t layouts_n;
};

RolltuiLayoutCache* rolltui_layout_cache_new(void) {
  RolltuiLayoutCache* c = (RolltuiLayoutCache*)rolltui_mem_alloc(sizeof *c);
  memset(c, 0, sizeof *c);
  return c;
}

void rolltui_layout_cache_free(RolltuiLayoutCache* c) {
  size_t i;
  if (c == NULL) return;
  rolltui_layout_actions_free(c->actions, c->actions_n);
  for (i = 0; i < c->layouts_n; ++i) rolltui_layout_release(&c->layouts[i].layout);
  rolltui_mem_free(c->layouts);
  rolltui_mem_free(c);
}

static RolltuiLayoutCache* cache_of(RolltuiContext* c) {
  if (c->layouts == NULL) c->layouts = rolltui_layout_cache_new();
  return c->layouts;
}

const char* rolltui_layout_builtin_json(const char* name, size_t len, size_t* out_len) {
  size_t i;
  for (i = 0; i < rolltui_kLayoutPresetCount; ++i) {
    const char* n = rolltui_kLayoutPresets[i].name;
    const size_t nl = strlen(n);
    if (nl == len && memcmp(n, name, len) == 0) {
      const char* text = rolltui_kLayoutPresets[i].text;
      if (out_len) *out_len = strlen(text);
      return text;
    }
  }
  if (out_len) *out_len = 0;
  return "";
}

const RolltuiLayoutAction* rolltui_layout_shipped_default_actions(RolltuiContext* c, size_t* n) {
  RolltuiLayoutCache* lc;
  if (c == NULL) {
    if (n) *n = 0;
    return NULL;
  }
  lc = cache_of(c);
  if (!lc->actions_done) {
    size_t tlen = 0;
    const char* text = rolltui_layout_builtin_json("default", 7, &tlen);
    RolltuiJsonValue* v = tlen ? rolltui_json_parse(text, tlen, NULL) : NULL;
    lc->actions_done = 1;
    if (v) {
      rolltui_layout_read_actions_key(v, &lc->actions, &lc->actions_n, &lc->actions_cap);
      rolltui_json_free(v);
    }
  }
  if (n) *n = lc->actions_n;
  return lc->actions;
}

/* ---- the library's own hooks ----------------------------------------------------------------
 * See rolltui_layout.h for why these three questions no longer have to be asked back. */

static int default_role_from_name(void* ctx, const char* name, size_t len, unsigned char* out) {
  const int r = rolltui_role_from_name(name, len);
  (void)ctx;
  if (r < 0) return 0;
  *out = (unsigned char)r;
  return 1;
}

static size_t default_role_name(void* ctx, unsigned char role, char* out, size_t cap) {
  size_t n = 0;
  const char* name = rolltui_role_name(role, &n);
  (void)ctx;
  if (n >= cap) n = cap ? cap - 1 : 0;
  if (cap) {
    memcpy(out, name, n);
    out[n] = '\0';
  }
  return n;
}

const RolltuiLayoutHooks* rolltui_layout_default_hooks(void) {
  static const RolltuiLayoutHooks h = {
      /*is_library_scope=*/rolltui_bindings_library_scope,
      /*scope_ctx=*/NULL,
      /*role_from_name=*/default_role_from_name,
      /*role_from_name_ctx=*/NULL,
      /*role_name=*/default_role_name,
      /*role_name_ctx=*/NULL,
  };
  return &h;
}

/* The four bytes a compose paints its chrome with, NAMED from the role list this file can
 * now reach — see the header for why they stopped being `Layout.cpp`'s. */
const RolltuiLayoutRoles* rolltui_layout_default_roles(void) {
  static const RolltuiLayoutRoles r = {
      /*border=*/ROLLTUI_ROLE_BORDER,
      /*border_active=*/ROLLTUI_ROLE_BORDER_ACTIVE,
      /*title=*/ROLLTUI_ROLE_TITLE,
      /*overlay=*/ROLLTUI_ROLE_OVERLAY,
      /*error=*/ROLLTUI_ROLE_ERROR,
      /*ok=*/ROLLTUI_ROLE_ACCENT_2,
  };
  return &r;
}

/* ============================================================================================
 * THE BUILT-IN LAYOUTS, PARSED AND CACHED — `Layout.cpp`'s cache, moved. See the
 * header for the two properties it carries over and the defect each one cost first.
 * ============================================================================================ */

/* Shipped order: "default" first — it is what a fresh install runs — then the table's own. */
static const char* builtin_name_at(size_t i) {
  size_t k, seen = 0;
  for (k = 0; k < rolltui_kLayoutPresetCount; ++k)
    if (strcmp(rolltui_kLayoutPresets[k].name, "default") == 0) {
      if (i == 0) return rolltui_kLayoutPresets[k].name;
      break;
    }
  if (i == 0) return rolltui_kLayoutPresetCount ? rolltui_kLayoutPresets[0].name : NULL;
  for (k = 0; k < rolltui_kLayoutPresetCount; ++k) {
    if (strcmp(rolltui_kLayoutPresets[k].name, "default") == 0) continue;
    if (++seen == i) return rolltui_kLayoutPresets[k].name;
  }
  return NULL;
}

static void builtin_layouts_fill(RolltuiContext* c) {
  size_t i;
  size_t n = rolltui_kLayoutPresetCount;
  RolltuiLayoutCache* lc = cache_of(c);
  if (lc->layouts_n != 0) return;
  if (n == 0) return;
  lc->layouts = (BuiltinLayout*)rolltui_mem_alloc(n * sizeof *lc->layouts);
  memset(lc->layouts, 0, n * sizeof *lc->layouts);
  for (i = 0; i < n; ++i) {
    const char* name = builtin_name_at(i);
    size_t json_len = 0;
    const char* json;
    RolltuiLoadedLayout loaded;
    RolltuiLayoutReport rep;
    size_t na = 0;
    const RolltuiLayoutAction* da = rolltui_layout_shipped_default_actions(c, &na);
    if (!name) break;
    json = rolltui_layout_builtin_json(name, strlen(name), &json_len);
    rolltui_loaded_layout_init(&loaded);
    memset(&rep, 0, sizeof rep);
    if (!rolltui_load_layout_text_into(json, json_len, &loaded, da, na, rolltui_layout_default_hooks(), &rep) ||
        !rolltui_layout_report_clean(&rep)) {
      /* A built-in that does not load cleanly is a PROGRAMMING ERROR: say so loudly rather
       * than serve half a layout. The C++ did the same, and its test asserts clean() per name. */
      rolltui_fatal("built-in layout '%s' is broken: %s", name, rep.error.n ? rep.error.p : "(unclean report)");
    }
    rolltui_layout_report_release(&rep);
    lc->layouts[i].name = name;
    lc->layouts[i].name_len = strlen(name);
    rolltui_layout_init(&lc->layouts[i].layout);
    rolltui_loaded_layout_to_layout(&loaded, &lc->layouts[i].layout);
    ++lc->layouts_n;
  }
}

const RolltuiLayout* rolltui_layout_builtin(RolltuiContext* c, const char* name, size_t len) {
  size_t i;
  const RolltuiLayoutCache* lc;
  if (c == NULL) return NULL;
  /* FILLED WHEN EMPTY, never in a static initializer — a session may clear its cache and a
   * once-only fill would leave every later call answering NULL. */
  builtin_layouts_fill(c);
  lc = c->layouts;
  for (i = 0; i < lc->layouts_n; ++i)
    if (lc->layouts[i].name_len == len && memcmp(lc->layouts[i].name, name, len) == 0) return &lc->layouts[i].layout;
  return NULL;
}

