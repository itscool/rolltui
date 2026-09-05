/* rolltui/c/rolltui_layout.c — the C side of placement, composition and the stack. See
 * rolltui_layout.h; the rules are rolltui/Layout.hpp's. */
#include "rolltui/c/rolltui_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_lifetime.h"

#define ROLLTUI_NODE_WINDOW 0
#define ROLLTUI_NODE_ROW 1
#define ROLLTUI_NODE_COLUMN 2

#define ROLLTUI_BORDER_NONE 0
#define ROLLTUI_BORDER_SINGLE 1
#define ROLLTUI_BORDER_ROUNDED 2
#define ROLLTUI_BORDER_DOUBLE 3
#define ROLLTUI_BORDER_HEAVY 4

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- placement ------------------------------------------------------------------------------ */

int rolltui_resolve_dim(RolltuiDim d, int extent) {
  return (int)floor(d.fraction * extent + 1e-6) + d.cells;
}

/* The alignment an anchor implies on each axis. Two tables rather than two switches: the
 * anchor ordinals are a 3x3 grid, and saying so is shorter than nine cases. */
static int align_h(unsigned char a) { return (int)(a % 3); } /* 0 start, 1 centre, 2 end */
static int align_v(unsigned char a) { return (int)(a / 3); }

/* One axis of resolve(): {start, size} relative to the parent. */
static void resolve_axis(RolltuiDim pos, RolltuiDim size, int align, const RolltuiOptDim* min,
                         const RolltuiOptDim* max, int clamp, int extent, int* out_start, int* out_len) {
  int start, len;
  if (align == 0) {
    RolltuiDim sum;
    sum.fraction = pos.fraction + size.fraction;
    sum.cells = pos.cells + size.cells;
    start = rolltui_resolve_dim(pos, extent);
    len = rolltui_resolve_dim(sum, extent) - start;
  } else {
    int point = rolltui_resolve_dim(pos, extent);
    len = rolltui_resolve_dim(size, extent);
    start = (align == 1) ? point - len / 2 : point - len;
  }
  if (len < 0) len = 0;
  if (max->present) len = imin(len, imax(rolltui_resolve_dim(max->d, extent), 0));
  if (min->present) len = imax(len, rolltui_resolve_dim(min->d, extent));
  if (clamp) {
    len = imin(len, imax(extent, 0));
    start = iclamp(start, 0, imax(extent - len, 0));
  }
  *out_start = start;
  *out_len = len;
}

void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out) {
  int x, w, y, h;
  resolve_axis(p->x, p->w, align_h(p->anchor), &p->min_w, &p->max_w, p->clamp, parent.w, &x, &w);
  resolve_axis(p->y, p->h, align_v(p->anchor), &p->min_h, &p->max_h, p->clamp, parent.h, &y, &h);
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

static RolltuiRect inner_rect(RolltuiRect outer, unsigned char border) {
  RolltuiRect r;
  rolltui_inner_rect(outer, border, &r);
  return r;
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


/* ---- the text forms ------------------------------------------------------------------------ */

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

int rolltui_parse_split_size(const char* text, size_t len, RolltuiSplitSize* out) {
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

int rolltui_parse_size_text(const char* text, size_t len, RolltuiSplitSize* out) {
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

size_t rolltui_split_size_to_string(RolltuiSplitSize s, char* out, size_t cap) {
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

/* ---- the split ------------------------------------------------------------------------------- */

#define SIDE_LEFT 0
#define SIDE_RIGHT 1
#define SIDE_TOP 2
#define SIDE_BOTTOM 3

static int edge_bordered(const RolltuiLayoutNode* n, int side) {
  const RolltuiLayoutNode* first = NULL;
  const RolltuiLayoutNode* last = NULL;
  size_t i;
  int along;
  if (n->border != ROLLTUI_BORDER_NONE) return 1;
  if (n->kind == ROLLTUI_NODE_WINDOW) return 0;
  /* Only ever the FIRST visible child, the LAST, or a walk over all of them — none of which
   * is a reason to build a list (Phase 13 m5b, and this function is recursive AND called
   * O(children²) from the shared-edge pass below). */
  for (i = 0; i < n->children.n; ++i) {
    RolltuiLayoutNode* c = n->children.v[i];
    if (!c->visible) continue;
    if (!first) first = c;
    last = c;
  }
  if (!first) return 0;
  along = (n->kind == ROLLTUI_NODE_ROW) ? (side == SIDE_LEFT || side == SIDE_RIGHT)
                                        : (side == SIDE_TOP || side == SIDE_BOTTOM);
  if (along) return edge_bordered((side == SIDE_LEFT || side == SIDE_TOP) ? first : last, side);
  for (i = 0; i < n->children.n; ++i)
    if (n->children.v[i]->visible && !edge_bordered(n->children.v[i], side)) return 0;
  return 1;
}

/* THE PER-CONTAINER SCRATCH, and it is an INLINE ARRAY with a stated SPILL (CLAUDE.md
 * strategy 1). `place` RECURSES, so a shared reused buffer would alias across depth — each
 * frame of the recursion has its own inline array and cannot. Twelve is the C++'s number,
 * kept so the two implementations spill at the same size. */
#define PLACE_INLINE 12

typedef struct ChildSlot {
  const RolltuiLayoutNode* node;
  int shared; /* this child shares its facing border edge with the next */
  int size;
} ChildSlot;

static void place(const RolltuiLayoutNode* n, RolltuiRect box, RolltuiRect screen, size_t layer,
                  RolltuiResolvedSink emit, void* ctx) {
  RolltuiResolvedNode rn;
  ChildSlot inline_slots[PLACE_INLINE];
  ChildSlot* kid = inline_slots;
  ChildSlot* spill = NULL;
  size_t visible = 0, i, w;
  int row, extent, shared_count = 0, ext, prev_edge = 0, fixed_total = 0, weight_total = 0;
  int remainder, cum_w = 0, prev_fill_edge = 0, pos = 0;
  RolltuiDim cum;
  RolltuiRect in;

  rn.node = n;
  rn.outer = box;
  rn.inner = rect_intersect(inner_rect(box, n->border), screen);
  rn.focused = 0;
  rn.layer = layer;
  emit(ctx, &rn);
  if (n->kind == ROLLTUI_NODE_WINDOW) return;

  row = n->kind == ROLLTUI_NODE_ROW;
  for (i = 0; i < n->children.n; ++i)
    if (n->children.v[i]->visible) ++visible;
  if (visible == 0) return;
  if (visible > PLACE_INLINE) {
    spill = (ChildSlot*)rolltui_mem_alloc(visible * sizeof *spill);
    kid = spill;
  }
  w = 0;
  for (i = 0; i < n->children.n; ++i)
    if (n->children.v[i]->visible) {
      kid[w].node = n->children.v[i];
      kid[w].shared = 0;
      kid[w].size = 0;
      ++w;
    }
  in = inner_rect(box, n->border); /* unclipped: children resolve against the true box */
  extent = row ? in.w : in.h;

  /* Shared edges between adjacent bordered siblings. */
  for (i = 0; i + 1 < visible; ++i) {
    kid[i].shared = row ? (edge_bordered(kid[i].node, SIDE_RIGHT) && edge_bordered(kid[i + 1].node, SIDE_LEFT))
                        : (edge_bordered(kid[i].node, SIDE_BOTTOM) && edge_bordered(kid[i + 1].node, SIDE_TOP));
    if (kid[i].shared) ++shared_count;
  }
  ext = imax(extent, 0) + shared_count;

  /* Fixed children: edges of the cumulative Dim sum. */
  cum.fraction = 0;
  cum.cells = 0;
  for (i = 0; i < visible; ++i) {
    int edge;
    if (kid[i].node->size.fill) {
      weight_total += imax(kid[i].node->size.weight, 1);
      continue;
    }
    cum.fraction += kid[i].node->size.dim.fraction;
    cum.cells += kid[i].node->size.dim.cells;
    edge = rolltui_resolve_dim(cum, ext);
    kid[i].size = imax(edge - prev_edge, 0);
    prev_edge = imax(edge, prev_edge);
    fixed_total += kid[i].size;
  }
  /* Fills: cumulative weight edges over the remainder. */
  remainder = imax(ext - fixed_total, 0);
  for (i = 0; i < visible; ++i) {
    RolltuiDim share;
    int edge;
    if (!kid[i].node->size.fill) continue;
    cum_w += imax(kid[i].node->size.weight, 1);
    share.fraction = (double)cum_w / weight_total;
    share.cells = 0;
    edge = rolltui_resolve_dim(share, remainder);
    kid[i].size = edge - prev_fill_edge;
    prev_fill_edge = edge;
  }
  /* Positions, sharing one cell per shared edge; clip to the extent in order. */
  for (i = 0; i < visible; ++i) {
    RolltuiRect r;
    if (pos + kid[i].size > imax(extent, 0)) kid[i].size = imax(imax(extent, 0) - pos, 0);
    if (row) {
      r.x = in.x + pos;
      r.y = in.y;
      r.w = kid[i].size;
      r.h = in.h;
    } else {
      r.x = in.x;
      r.y = in.y + pos;
      r.w = in.w;
      r.h = kid[i].size;
    }
    place(kid[i].node, r, screen, layer, emit, ctx);
    pos += kid[i].size;
    if (i + 1 < visible && kid[i].shared && kid[i].size > 0) pos -= 1;
  }
  rolltui_mem_free(spill); /* NULL in the steady case: the inline array is the whole of it */
}

void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx) {
  if (root->visible) place(root, box, screen, layer, emit, ctx);
}

/* ---- drawing --------------------------------------------------------------------------------- */

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

static int joins(unsigned char b) { return b == ROLLTUI_BORDER_SINGLE || b == ROLLTUI_BORDER_ROUNDED; }

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

static void draw_border_impl(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char b,
                             RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                             int ascii, unsigned char* map, const unsigned char* ring_before) {
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
      if (map && ring_before && joins(b)) m |= ring_before[(size_t)(y * fw + x)];
      g = glyph_for(b, m, ascii);
      if (!*g) g = " ";
      rolltui_frame_put(f, x, y, g, strlen(g), 1, line, 0);
      if (map) map[(size_t)(y * fw + x)] = joins(b) ? m : 0;
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
    used += rolltui_frame_put_text(f, draw, outer.x + 1 + used, outer.y, " ", 1, title_style, avail - used,
                                   ascii, 0);
    if (map)
      for (x = outer.x + 1; x < outer.x + 1 + used && x < fw; ++x)
        if (x >= 0) map[(size_t)(outer.y * fw + x)] = 0;
  }
}

void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border,
                         RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                         int ambiguous_wide) {
  draw_border_impl(f, draw, outer, border, line, title, title_n, title_style, ambiguous_wide, NULL, NULL);
}

/* ---- compose ----------------------------------------------------------------------------------- */

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
  unsigned char* map;    /* the join arms written so far, this layer */
  unsigned char* before; /* what was under the ring before this window cleared it */
  size_t map_cap, before_cap;
  NodeCollect nodes;        /* ONE layer's resolved nodes — a third ROLE, reused per layer */
  RolltuiDrawScratch* draw; /* OWNED: the cluster walk a title needs — a fourth */
};

RolltuiComposeScratch* rolltui_compose_scratch_new(void) {
  RolltuiComposeScratch* s = (RolltuiComposeScratch*)rolltui_mem_alloc(sizeof *s);
  s->map = NULL;
  s->before = NULL;
  s->map_cap = 0;
  s->before_cap = 0;
  s->nodes.v = NULL;
  s->nodes.n = 0;
  s->nodes.cap = 0;
  s->draw = rolltui_draw_scratch_new();
  return s;
}

void rolltui_compose_scratch_free(RolltuiComposeScratch* s) {
  if (!s) return;
  rolltui_mem_free(s->map);
  rolltui_mem_free(s->before);
  rolltui_mem_free(s->nodes.v);
  rolltui_draw_scratch_free(s->draw);
  rolltui_mem_free(s);
}

void rolltui_compose_layer(RolltuiFrame* f, const RolltuiResolvedNode* nodes, size_t count,
                           const RolltuiStyle* styles, const RolltuiLayoutRoles* roles, RolltuiSlotFn render,
                           void* ctx, int ambiguous_wide, RolltuiComposeScratch* scratch) {
  const int fw = rolltui_frame_width(f), fh = rolltui_frame_height(f);
  const size_t cells = (size_t)(fw > 0 ? fw : 0) * (size_t)(fh > 0 ? fh : 0);
  RolltuiRect bounds;
  size_t i;
  bounds.x = 0;
  bounds.y = 0;
  bounds.w = fw;
  bounds.h = fh;
  /* GROWING, EXACT: a frame's cell count is known and a doubled map would be up to 70%
   * overshoot on the biggest buffer here — the rolltui_alloc.h note about the cell grid,
   * one level up. */
  scratch->map = (unsigned char*)rolltui_fit(scratch->map, &scratch->map_cap, cells, 1);
  scratch->before = (unsigned char*)rolltui_fit(scratch->before, &scratch->before_cap, cells, 1);
  memset(scratch->map, 0, cells);
  memset(scratch->before, 0, cells);

  /* TWO PASSES: every node's ground and border first, then every window's CONTENT (Phase 12
   * m7 — the reason is in Layout.cpp's own comment and in the phase file). */
  for (i = 0; i < count; ++i) {
    const RolltuiResolvedNode* rn = &nodes[i];
    const RolltuiLayoutNode* n = rn->node;
    const int draws = n->kind == ROLLTUI_NODE_WINDOW || n->border != ROLLTUI_BORDER_NONE;
    RolltuiStyle ground, line, title;
    RolltuiRect clip;
    int x, y;
    if (!draws) continue;
    ground = styles[n->background];
    clip = rect_intersect(rn->outer, bounds);
    for (y = clip.y; y < clip.y + clip.h; ++y)
      for (x = clip.x; x < clip.x + clip.w; ++x) {
        const size_t k = (size_t)(y * fw + x);
        scratch->before[k] = scratch->map[k];
        scratch->map[k] = 0;
      }
    rolltui_frame_fill(f, scratch->draw, rn->outer, ground, NULL, 0);
    line = styles[rn->focused ? roles->border_active : roles->border];
    line.bg = ground.bg;
    title = styles[roles->title];
    title.bg = ground.bg;
    draw_border_impl(f, scratch->draw, rn->outer, n->border, line, n->title.p, n->title.n, title,
                     ambiguous_wide, scratch->map, scratch->before);
  }
  if (!render) return;
  for (i = 0; i < count; ++i)
    if (nodes[i].node->kind == ROLLTUI_NODE_WINDOW && !rect_empty(nodes[i].inner)) render(ctx, &nodes[i], f);
}

/* ---- the widget-kind registry --------------------------------------------------------------------- */

typedef struct KindRow {
  const char* name;
  unsigned char rule;
  const char* source_is;
} KindRow;

/* THE TABLE. One definition site: the names, the source rule and what a source means all
 * come from here, and the ORDER is `rolltui::WidgetKind`'s (asserted on the C++ side). */
static const KindRow kKinds[] = {
    {"transcript", ROLLTUI_SOURCE_REQUIRED, "a document the host binds"},
    {"input", ROLLTUI_SOURCE_REQUIRED, "the target a submitted line goes to"},
    {"menu", ROLLTUI_SOURCE_REQUIRED, "a menu file"},
    {"rows", ROLLTUI_SOURCE_REQUIRED, "a row source the host binds"},
    {"text", ROLLTUI_SOURCE_OPTIONAL, "the literal text"},
    {"file", ROLLTUI_SOURCE_REQUIRED, "a path"},
    {"help", ROLLTUI_SOURCE_OPTIONAL, "one key scope, or every one when empty"},
};
#define KIND_COUNT (sizeof kKinds / sizeof kKinds[0])

/* Phase 9's slot names and Phase 10's `custom:` contents → their m3 form. A closed, one-way
 * table; the five composites are why it is a MAP rather than a rule (Layout.hpp). */
static const char* const kLegacy[][2] = {
    {"transcript", "transcript:session"}, {"input", "input:prompt"},       {"status", "rows:status"},
    {"menu", "menu:main"},                {"custom:approval", "approval"}, {"custom:details", "details"},
    {"custom:editor", "editor"},          {"custom:confirm", "confirm"},   {"custom:report", "report"},
};
#define LEGACY_COUNT (sizeof kLegacy / sizeof kLegacy[0])

/* RUNG 2, and the first thing in this file that is RETAINED: a kind is a program's
 * vocabulary, not one screen's, so a host registers once at startup and every `Windows` in
 * the process parses layout files the same way. Released at `rolltui::shutdown()`. */
typedef struct HostKind {
  RolltuiStr name;
  RolltuiStr source_is;
  unsigned char rule;
} HostKind;

static HostKind* g_host_kinds;
static size_t g_host_count, g_host_cap;
static int g_kind_releaser_registered;

static const HostKind* host_kind(const char* name, size_t len) {
  size_t i;
  for (i = 0; i < g_host_count; ++i)
    if (rolltui_str_eq(&g_host_kinds[i].name, name, len)) return &g_host_kinds[i];
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

int rolltui_widget_kind_resolve(const char* name, size_t len, unsigned char* ordinal, unsigned char* rule,
                                const char** source_is, size_t* source_is_len) {
  size_t i;
  const HostKind* h;
  /* THE RESOLUTION ORDER (Layout.hpp). Rung 1 first, unconditionally — the guard that
   * survives even if a shadowing registration somehow existed. */
  if (library_kind_index(name, len, &i)) {
    if (ordinal) *ordinal = (unsigned char)i;
    if (rule) *rule = kKinds[i].rule;
    if (source_is) *source_is = kKinds[i].source_is;
    if (source_is_len) *source_is_len = strlen(kKinds[i].source_is);
    return ROLLTUI_KIND_LIBRARY;
  }
  h = host_kind(name, len);
  if (h) {
    if (rule) *rule = h->rule;
    if (source_is) *source_is = rolltui_str_get(&h->source_is, source_is_len);
    return ROLLTUI_KIND_HOST;
  }
  return ROLLTUI_KIND_UNKNOWN;
}

size_t rolltui_widget_kind_library_count(void) { return KIND_COUNT; }

const char* rolltui_widget_kind_library_name(size_t i, size_t* len) {
  if (i >= KIND_COUNT) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = strlen(kKinds[i].name);
  return kKinds[i].name;
}

unsigned char rolltui_widget_kind_library_rule(size_t i) {
  return i < KIND_COUNT ? kKinds[i].rule : ROLLTUI_SOURCE_REQUIRED;
}

const char* rolltui_widget_kind_library_source_is(size_t i, size_t* len) {
  if (i >= KIND_COUNT) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = strlen(kKinds[i].source_is);
  return kKinds[i].source_is;
}

size_t rolltui_widget_kind_host_count(void) { return g_host_count; }

const char* rolltui_widget_kind_host_name(size_t i, size_t* len) {
  if (i >= g_host_count) {
    if (len) *len = 0;
    return "";
  }
  return rolltui_str_get(&g_host_kinds[i].name, len);
}

int rolltui_widget_kind_register(const char* name, size_t len, unsigned char rule, const char* source_is,
                                 size_t source_is_len) {
  const HostKind* h;
  size_t i;
  if (len == 0) return ROLLTUI_REGISTER_EMPTY;
  for (i = 0; i < len; ++i)
    if (name[i] == ':') return ROLLTUI_REGISTER_HAS_COLON;
  if (library_kind_index(name, len, NULL)) return ROLLTUI_REGISTER_IS_LIBRARY;
  h = host_kind(name, len);
  if (h) return h->rule == rule ? ROLLTUI_REGISTER_OK : ROLLTUI_REGISTER_RULE_DIFFERS;
  g_host_kinds = (HostKind*)rolltui_grow_zeroed(g_host_kinds, &g_host_cap, g_host_count + 1, sizeof *g_host_kinds);
  rolltui_str_set(&g_host_kinds[g_host_count].name, name, len);
  rolltui_str_set(&g_host_kinds[g_host_count].source_is, source_is, source_is_len);
  g_host_kinds[g_host_count].rule = rule;
  ++g_host_count;
  /* REGISTER THE RELEASER AT FILL TIME, and let it touch the STORAGE — the rule m3 wrote
   * after two latent defects in `Layout.cpp`'s built-in cache. Cleared by `clear` so a
   * registry emptied by `shutdown()` and used again says so again. */
  if (!g_kind_releaser_registered) {
    g_kind_releaser_registered = 1;
    rolltui_on_shutdown(rolltui_widget_kind_clear);
  }
  return ROLLTUI_REGISTER_OK;
}

void rolltui_widget_kind_clear(void) {
  size_t i;
  for (i = 0; i < g_host_count; ++i) {
    rolltui_str_free(&g_host_kinds[i].name);
    rolltui_str_free(&g_host_kinds[i].source_is);
  }
  rolltui_mem_free(g_host_kinds);
  g_host_kinds = NULL;
  g_host_count = 0;
  g_host_cap = 0;
  g_kind_releaser_registered = 0;
}

const char* rolltui_migrated_content(const char* legacy, size_t len, size_t* out_len) {
  size_t i;
  for (i = 0; i < LEGACY_COUNT; ++i)
    if (strlen(kLegacy[i][0]) == len && memcmp(kLegacy[i][0], legacy, len) == 0) {
      if (out_len) *out_len = strlen(kLegacy[i][1]);
      return kLegacy[i][1];
    }
  return NULL;
}

/* ---- small local helpers shared by content-parsing and the loader below -----------------------
 * `K`/`streq` mirror `rolltui_app_profile.c`'s own (its own copy, not shared: a static helper
 * has no external linkage, and each is a two-line wrapper, not a strategy worth a header). */
#define K(s) (s), strlen(s)

static int streq(const char* s, size_t slen, const char* lit) {
  size_t litlen = strlen(lit);
  return slen == litlen && (litlen == 0 || memcmp(s, lit, litlen) == 0);
}

/* Appends a literal / a span to a message being built. Every report sentence below is built
 * with these two into a local `RolltuiStr msg = {0};`, then handed to `add_bad`/`add_unknown`/
 * `add_migrated`, which MOVE it into the report — one allocation per piece appended (GROWING
 * EXACT, `rolltui_str_append`'s own strategy), no fixed-size buffer to truncate a long content
 * string or file path against. */
static void app(RolltuiStr* s, const char* lit) { rolltui_str_append(s, lit, strlen(lit)); }
static void appn(RolltuiStr* s, const char* p, size_t n) { rolltui_str_append(s, p, n); }

/* ---- content: parsing and formatting, as C (Phase 17 m2) --------------------------------------- */

int rolltui_content_parse(const char* text, size_t len, unsigned char* ordinal, int* is_host, const char** name,
                         size_t* name_len, const char** source, size_t* source_len, unsigned char* problem,
                         RolltuiStr* why) {
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

  rung = rolltui_widget_kind_resolve(text, colon, ordinal, &rule, &describes, &describes_n);
  if (rung == ROLLTUI_KIND_LIBRARY) {
    if (is_host) *is_host = 0;
  } else if (rung == ROLLTUI_KIND_HOST) {
    if (is_host) *is_host = 1;
  } else {
    /* UnknownKind: neither rung. THE RESOLUTION ORDER is the C's (rolltui_widget_kind_
     * resolve already walked it); this is rung 3 failing with a named reason, which is the
     * half that has to be in a language with sentences. */
    size_t mlen = 0;
    const char* m = rolltui_migrated_content(text, len, &mlen);
    if (problem) *problem = ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND;
    if (source) *source = text + len;
    if (source_len) *source_len = 0;
    if (why) {
      if (m) {
        app(why, "'");
        appn(why, text, len);
        app(why, "' is an older spelling, not a widget kind; write '");
        appn(why, m, mlen);
        app(why, "'");
      } else {
        size_t k, kc = rolltui_widget_kind_library_count(), hc = rolltui_widget_kind_host_count();
        app(why, "'");
        appn(why, text, colon);
        app(why, "' is not a widget kind (");
        for (k = 0; k < kc; ++k) {
          size_t ln = 0;
          const char* kn = rolltui_widget_kind_library_name(k, &ln);
          if (k) app(why, " | ");
          appn(why, kn, ln);
        }
        for (k = 0; k < hc; ++k) {
          size_t ln = 0;
          const char* kn = rolltui_widget_kind_host_name(k, &ln);
          if (kc + k) app(why, " | ");
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
      size_t mlen = 0;
      const char* m = rolltui_migrated_content(text, len, &mlen); /* an older name that is also a kind name */
      if (problem) *problem = ROLLTUI_CONTENT_PROBLEM_MISSING_SOURCE;
      if (why) {
        if (m) {
          app(why, "'");
          appn(why, text, len);
          app(why, "' is an older spelling, not a content; write '");
          appn(why, m, mlen);
          app(why, "'");
        } else {
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

/* ---- names: anchors and borders — Layout's OWN vocabulary, unlike Role ------------------------- */

static const char* const kAnchorNames[9] = {"top-left",     "top",    "top-right", "left",   "center",
                                            "right",        "bottom-left", "bottom", "bottom-right"};
static const char* const kBorderNames[5] = {"none", "single", "rounded", "double", "heavy"};

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
  if (b >= 5) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = strlen(kBorderNames[b]);
  return kBorderNames[b];
}

int rolltui_border_from_name(const char* name, size_t len, unsigned char* out) {
  size_t i;
  for (i = 0; i < 5; ++i)
    if (streq(name, len, kBorderNames[i])) {
      *out = (unsigned char)i;
      return 1;
    }
  return 0;
}

/* ---- the loader: a layout file's JSON, both directions, as C (Phase 17 m2) --------------------- */

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

/* ---- the report ---------------------------------------------------------------------------------- */

void rolltui_layout_report_release(RolltuiLayoutReport* r) {
  size_t i;
  if (!r) return;
  rolltui_str_free(&r->error);
  for (i = 0; i < r->unknown_keys_n; ++i) rolltui_str_free(&r->unknown_keys[i]);
  rolltui_mem_free(r->unknown_keys);
  for (i = 0; i < r->bad_values_n; ++i) rolltui_str_free(&r->bad_values[i]);
  rolltui_mem_free(r->bad_values);
  for (i = 0; i < r->migrated_n; ++i) rolltui_str_free(&r->migrated[i]);
  rolltui_mem_free(r->migrated);
  memset(r, 0, sizeof *r);
}

int rolltui_layout_report_clean(const RolltuiLayoutReport* r) {
  /* migrated is deliberately NOT part of "clean" — LayoutLoadReport::clean()'s own rule: the
   * layout loaded, and a migration is a note rather than a problem. */
  return r->error.n == 0 && r->unknown_keys_n == 0 && r->bad_values_n == 0;
}

/* MOVE variants: take ownership of an already-built message, leaving it empty — every call
 * site below builds one local `RolltuiStr msg` from pieces and hands it straight over, so
 * there is exactly one copy of the bytes rather than a build-then-copy-then-free. GROWING
 * AMORTISED arrays of small owned strings, the same shape `rolltui_app_profile.c`'s report
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
static void add_migrated(RolltuiLayoutReport* r, RolltuiStr* msg) {
  r->migrated =
      (RolltuiStr*)rolltui_grow_zeroed(r->migrated, &r->migrated_cap, r->migrated_n + 1, sizeof *r->migrated);
  rolltui_str_move(&r->migrated[r->migrated_n++], msg);
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

/* ---- RolltuiLoadedLayout: the transient carrier (see the header comment) ----------------------- */

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

/* ---- the loader's own JSON walk: node, layer, dim, bool ----------------------------------------- */

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
  int has_row, has_col, has_content;
  size_t i, cnt;

  if (!rolltui_json_is_object(v)) {
    bad_at(report, where, where_len, ": expected a node object");
    return;
  }
  has_row = rolltui_json_has(v, K("row"));
  has_col = rolltui_json_has(v, K("column"));
  has_content = rolltui_json_has(v, K("content"));
  if (has_row + has_col + has_content != 1) {
    bad_at(report, where, where_len, ": a node has exactly one of \"content\", \"row\", \"column\"");
    return;
  }
  n->kind = (unsigned char)(has_row ? ROLLTUI_NODE_ROW : has_col ? ROLLTUI_NODE_COLUMN : ROLLTUI_NODE_WINDOW);

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
    } else if (streq(k, klen, "content")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->content, x);
    } else if (streq(k, klen, "id")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->id, x);
    } else if (streq(k, klen, "title")) {
      if (!rolltui_json_is_string(x)) bad_at(report, at.p, at.n, ": expected a string");
      else set_str_field(&n->title, x);
    } else if (streq(k, klen, "border")) {
      unsigned char b;
      size_t sl = 0;
      const char* s = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &sl) : NULL;
      if (!s || !rolltui_border_from_name(s, sl, &b))
        bad_at(report, at.p, at.n, ": expected none | single | rounded | double | heavy");
      else n->border = b;
    } else if (streq(k, klen, "background")) {
      unsigned char role;
      size_t sl = 0;
      const char* s = rolltui_json_is_string(x) ? rolltui_json_as_string(x, "", 0, &sl) : NULL;
      if (!s || !hooks || !hooks->role_from_name || !hooks->role_from_name(hooks->role_from_name_ctx, s, sl, &role))
        bad_at(report, at.p, at.n, ": expected a role name");
      else n->background = role;
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
        RolltuiSplitSize ss;
        if (rolltui_parse_split_size(s, sl, &ss)) {
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

  /* Content is kind[:source] (Layout.hpp). A Phase 9 slot name is rewritten once and said
   * so; anything else the table does not know is a bad value that names the fix. */
  if (n->kind == ROLLTUI_NODE_WINDOW) {
    size_t mlen = 0;
    const char* m = rolltui_migrated_content(n->content.p, n->content.n, &mlen);
    if (m) {
      RolltuiStr msg = {0};
      appn(&msg, where, where_len);
      app(&msg, ".content: '");
      appn(&msg, n->content.p, n->content.n);
      app(&msg, "' \xE2\x86\x92 '");
      appn(&msg, m, mlen);
      app(&msg, "'");
      add_migrated(report, &msg);
      if (n->id.n == 0) rolltui_str_set(&n->id, n->content.p, n->content.n); /* the id it had before the rewrite */
      rolltui_str_set(&n->content, m, mlen);
    }
    {
      /* An UNKNOWN KIND is deliberately NOT a bad value here — rung 2 belongs to the host,
       * and this loader runs before a host has necessarily registered anything; `Windows`
       * reports it, by name, with the error panel drawn. Every other problem is a fact
       * about the STRING and is the loader's to name. */
      unsigned char ordinal = 0, problem = ROLLTUI_CONTENT_PROBLEM_NONE;
      int is_host = 0;
      const char *cname = NULL, *csrc = NULL;
      size_t cnamelen = 0, csrclen = 0;
      RolltuiStr why = {0};
      const int ok =
          rolltui_content_parse(n->content.p, n->content.n, &ordinal, &is_host, &cname, &cnamelen, &csrc, &csrclen,
                                &problem, &why);
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
  int have_root = 0;
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
    } else if (is_popup && streq(k, klen, "clamp")) {
      bool_from_json(x, at.p, at.n, report, &l->placement.clamp);
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
   * ".root.column[0]...". Deliberately NOT applied to `migrated`: the original C++ only
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
  rolltui_json_free(base);

  /* A file written before actions existed (Phase 9, and every layout a user has saved
   * since) declares none — and would silently lose every app key. It is given the shipped
   * default's, named in `migrated` the way a Phase 9 content string is; the next save
   * writes them into the file. An explicit "actions": {} means none and is kept. */
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
      add_migrated(report, &msg);
    }
    rolltui_str_free(&names);
  }

  /* The base's report paths begin with "." because its keys sit at the top level. */
  for (i = 0; i < report->unknown_keys_n; ++i) strip_leading_dot(&report->unknown_keys[i]);
  for (i = 0; i < report->bad_values_n; ++i) strip_leading_dot(&report->bad_values[i]);
  return 1;
}

int rolltui_load_layout_text(const char* text, size_t len, RolltuiLoadedLayout* out,
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

/* ---- serialising: back to JSON, as C ------------------------------------------------------------- */

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
  if (n->kind == ROLLTUI_NODE_WINDOW) {
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
  if (n->focusable) rolltui_json_set(o, K("focusable"), rolltui_json_bool(1));
  if (!n->visible) rolltui_json_set(o, K("visible"), rolltui_json_bool(0));
  if (n->background != ROLLTUI_ROLE_DEFAULT_BACKGROUND) {
    char namebuf[ROLLTUI_ROLE_NAME_MAX];
    size_t nl = 0;
    if (hooks && hooks->role_name) nl = hooks->role_name(hooks->role_name_ctx, n->background, namebuf, sizeof namebuf);
    rolltui_json_set(o, K("background"), rolltui_json_string(namebuf, nl));
  }
  {
    /* RolltuiSplitSize{}'s own default (rolltui_layout_tree.h: fill=1, weight=1, dim={0,0}). */
    const int is_default = n->size.fill && n->size.weight == 1 && n->size.dim.fraction == 0 && n->size.dim.cells == 0;
    if (!is_default) {
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
    if (n->kind == ROLLTUI_NODE_ROW) rolltui_json_set(o, K("row"), arr);
    else rolltui_json_set(o, K("column"), arr);
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
    if (l->placement.min_w.present) rolltui_json_set(o, K("min_w"), dim_to_json(l->placement.min_w.d));
    if (l->placement.min_h.present) rolltui_json_set(o, K("min_h"), dim_to_json(l->placement.min_h.d));
    if (l->placement.max_w.present) rolltui_json_set(o, K("max_w"), dim_to_json(l->placement.max_w.d));
    if (l->placement.max_h.present) rolltui_json_set(o, K("max_h"), dim_to_json(l->placement.max_h.d));
    if (l->modal) rolltui_json_set(o, K("modal"), rolltui_json_bool(1));
  }
  if (l->focus.n) rolltui_json_set(o, K("focus"), rolltui_json_string(l->focus.p, l->focus.n));
  rolltui_json_set(o, K("root"), node_to_json(&l->root, hooks));
  return o;
}

RolltuiJsonValue* rolltui_layout_to_json_value(const char* name, size_t name_len, int min_width, int min_height,
                                               const RolltuiLayoutAction* actions, size_t actions_n,
                                               const RolltuiLayer* base, const RolltuiLayer* popups,
                                               size_t popups_n, const RolltuiLayoutHooks* hooks) {
  RolltuiJsonValue* o = rolltui_json_object();
  size_t i;
  rolltui_json_set(o, K("name"), rolltui_json_string(name, name_len));
  if (min_width) rolltui_json_set(o, K("min_width"), rolltui_json_number(min_width));
  if (min_height) rolltui_json_set(o, K("min_height"), rolltui_json_number(min_height));
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

void rolltui_layout_to_json_text(const char* name, size_t name_len, int min_width, int min_height,
                                const RolltuiLayoutAction* actions, size_t actions_n, const RolltuiLayer* base,
                                const RolltuiLayer* popups, size_t popups_n, const RolltuiLayoutHooks* hooks,
                                RolltuiStr* out) {
  RolltuiJsonValue* v =
      rolltui_layout_to_json_value(name, name_len, min_width, min_height, actions, actions_n, base, popups,
                                   popups_n, hooks);
  rolltui_json_dump(v, 2, out);
  rolltui_json_free(v);
}

/* ---- the stack ------------------------------------------------------------------------------------ */

struct RolltuiWindowStack {
  RolltuiLayer* layers; /* OWNED; always at least one (the base) */
  size_t n, cap;
  RolltuiStr captured;
};

static const RolltuiLayoutNode* find_in(const RolltuiLayoutNode* n, const char* id, size_t len) {
  size_t i;
  if (len && rolltui_str_eq(&n->id, id, len)) return n;
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* f = find_in(n->children.v[i], id, len);
    if (f) return f;
  }
  return NULL;
}

/* No list. This runs once per layer per resolve — three times a frame — and only ever needs
 * the FIRST focusable or the one matching a name (Phase 13 m5b). */
static const RolltuiLayoutNode* first_focusable(const RolltuiLayoutNode* n) {
  size_t i;
  if (!n->visible) return NULL;
  if (n->kind == ROLLTUI_NODE_WINDOW) return n->focusable ? n : NULL;
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* f = first_focusable(n->children.v[i]);
    if (f) return f;
  }
  return NULL;
}

static const RolltuiLayoutNode* focusable_named(const RolltuiLayoutNode* n, const char* id, size_t len) {
  size_t i;
  if (!n->visible) return NULL;
  if (n->kind == ROLLTUI_NODE_WINDOW) return (n->focusable && rolltui_str_eq(&n->id, id, len)) ? n : NULL;
  for (i = 0; i < n->children.n; ++i) {
    const RolltuiLayoutNode* f = focusable_named(n->children.v[i], id, len);
    if (f) return f;
  }
  return NULL;
}

static const RolltuiLayoutNode* layer_focused(const RolltuiLayer* l) {
  if (l->focus.n) {
    const RolltuiLayoutNode* named = focusable_named(&l->root, l->focus.p, l->focus.n);
    if (named) return named;
  }
  return first_focusable(&l->root);
}

/* The focusables of one layer, in tree order, into a caller's array. Returns how many there
 * ARE, so a caller that only wants the count passes a zero cap — the shape that lets
 * `cycle_focus` and `route` share one walk without either building a list. */
static size_t focusables(const RolltuiLayoutNode* n, const RolltuiLayoutNode** out, size_t cap, size_t at) {
  size_t i;
  if (!n->visible) return at;
  if (n->kind == ROLLTUI_NODE_WINDOW) {
    if (!n->focusable) return at;
    if (at < cap) out[at] = n;
    return at + 1;
  }
  for (i = 0; i < n->children.n; ++i) at = focusables(n->children.v[i], out, cap, at);
  return at;
}

RolltuiWindowStack* rolltui_window_stack_new(void) {
  RolltuiWindowStack* s = (RolltuiWindowStack*)rolltui_mem_alloc(sizeof *s);
  memset(s, 0, sizeof *s);
  s->layers = (RolltuiLayer*)rolltui_grow_zeroed(s->layers, &s->cap, 1, sizeof *s->layers);
  rolltui_layer_init(&s->layers[0]);
  s->n = 1;
  return s;
}

void rolltui_window_stack_free(RolltuiWindowStack* s) {
  size_t i;
  if (!s) return;
  for (i = 0; i < s->n; ++i) rolltui_layer_release(&s->layers[i]);
  rolltui_mem_free(s->layers);
  rolltui_str_free(&s->captured);
  rolltui_mem_free(s);
}

void rolltui_window_stack_set_base(RolltuiWindowStack* s, const RolltuiLayer* base) {
  RolltuiStr keep;
  memset(&keep, 0, sizeof keep);
  rolltui_str_set(&keep, s->layers[0].focus.p, s->layers[0].focus.n);
  rolltui_layer_copy(&s->layers[0], base);
  if (base->focus.n == 0 && keep.n && find_in(&s->layers[0].root, keep.p, keep.n))
    rolltui_str_move(&s->layers[0].focus, &keep);
  rolltui_str_free(&keep);
}

RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s) { return &s->layers[0]; }

void rolltui_window_stack_push(RolltuiWindowStack* s, RolltuiLayer* popup) {
  s->layers = (RolltuiLayer*)rolltui_grow_zeroed(s->layers, &s->cap, s->n + 1, sizeof *s->layers);
  rolltui_layer_init(&s->layers[s->n]);
  rolltui_layer_move(&s->layers[s->n], popup);
  ++s->n;
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
  if (s->layers[top].modal) return top;
  for (i = s->n; i-- > 0;)
    if (layer_focused(&s->layers[i])) return i;
  return top;
}

const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s) {
  return layer_focused(&s->layers[rolltui_window_stack_focus_layer(s)]);
}

void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, size_t len) {
  RolltuiLayer* l = &s->layers[rolltui_window_stack_focus_layer(s)];
  if (focusable_named(&l->root, id, len)) rolltui_str_set(&l->focus, id, len);
}

/* THE ONE PLACE A LIST OF FOCUSABLES IS ACTUALLY NEEDED — cycling has to know the NEXT one.
 * An inline array with a stated spill, the same shape `place` uses, because a screen with
 * more than sixteen focusable windows is possible and must not be assumed away. */
#define FOCUS_INLINE 16

void rolltui_window_stack_cycle_focus(RolltuiWindowStack* s, int backwards) {
  RolltuiLayer* l = &s->layers[rolltui_window_stack_focus_layer(s)];
  const RolltuiLayoutNode* inline_f[FOCUS_INLINE];
  const RolltuiLayoutNode** f = inline_f;
  const RolltuiLayoutNode** spill = NULL;
  const RolltuiLayoutNode* cur;
  size_t count = focusables(&l->root, NULL, 0, 0);
  size_t i;
  if (count == 0) return;
  if (count > FOCUS_INLINE) {
    spill = (const RolltuiLayoutNode**)rolltui_mem_alloc(count * sizeof *spill);
    f = spill;
  }
  focusables(&l->root, f, count, 0);
  cur = layer_focused(l);
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
} StackSink;

static void stack_sink(void* ctx, const RolltuiResolvedNode* rn) {
  StackSink* s = (StackSink*)ctx;
  RolltuiResolvedNode out = *rn;
  out.focused = (unsigned char)(rn->node == s->focused);
  s->emit(s->ctx, &out);
}

void rolltui_window_stack_resolve(const RolltuiWindowStack* s, RolltuiRect screen, RolltuiResolvedSink emit,
                                  void* ctx) {
  StackSink w;
  size_t i;
  w.emit = emit;
  w.ctx = ctx;
  w.focused = rolltui_window_stack_focused(s);
  for (i = 0; i < s->n; ++i) {
    RolltuiRect box;
    rolltui_placement_resolve(&s->layers[i].placement, screen, &box);
    rolltui_resolve_tree(&s->layers[i].root, box, screen, i, stack_sink, &w);
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
  w.emit = collect_sink;
  w.ctx = &scratch->nodes;
  w.focused = rolltui_window_stack_focused(s);
  for (i = 0; i < s->n; ++i) {
    RolltuiRect box;
    if (s->layers[i].modal) rolltui_frame_tint(f, screen, styles[roles->overlay]);
    scratch->nodes.n = 0;
    rolltui_placement_resolve(&s->layers[i].placement, screen, &box);
    rolltui_resolve_tree(&s->layers[i].root, box, screen, i, stack_sink, &w);
    rolltui_compose_layer(f, scratch->nodes.v, scratch->nodes.n, styles, roles, render, ctx, ambiguous_wide,
                          scratch);
  }
}

unsigned char rolltui_window_stack_route(RolltuiWindowStack* s, const RolltuiEvent* e, RolltuiRect screen,
                                         const RolltuiBindings* bindings, const RolltuiStackActions* actions,
                                         RolltuiStr* window) {
  rolltui_str_clear(window);
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent* m = &e->mouse;
    NodeCollect c;
    const size_t top = s->n - 1;
    size_t k;
    unsigned char verdict = ROLLTUI_ROUTE_DROPPED;
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
      for (i = 0; i < s->n; ++i) {
        RolltuiRect box;
        rolltui_placement_resolve(&s->layers[i].placement, screen, &box);
        rolltui_resolve_tree(&s->layers[i].root, box, screen, i, stack_sink, &w);
      }
    }
    for (k = c.n; k-- > 0;) {
      const RolltuiResolvedNode* rn = &c.v[k];
      if (rn->node->kind != ROLLTUI_NODE_WINDOW || !rect_contains(rect_intersect(rn->outer, screen), m->x, m->y))
        continue;
      if (s->layers[top].modal && rn->layer != top) break; /* dropped under a modal */
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
    return verdict;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    size_t alen = 0;
    const char* action = rolltui_bindings_action_for(bindings, &e->key, "stack", 5, &alen);
    if (action) {
      if (alen == strlen(actions->close_popup) && memcmp(action, actions->close_popup, alen) == 0 && s->n > 1) {
        rolltui_str_set(window, s->layers[s->n - 1].id.p, s->layers[s->n - 1].id.n);
        rolltui_window_stack_pop(s);
        return ROLLTUI_ROUTE_CLOSED_POPUP;
      }
      {
        const int next = alen == strlen(actions->focus_next) && memcmp(action, actions->focus_next, alen) == 0;
        const int prev = alen == strlen(actions->focus_prev) && memcmp(action, actions->focus_prev, alen) == 0;
        if (next || prev) {
          const RolltuiLayer* l = &s->layers[rolltui_window_stack_focus_layer(s)];
          if (focusables(&l->root, NULL, 0, 0) > 1) {
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
