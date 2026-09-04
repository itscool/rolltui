/* rolltui/c/rolltui_layout.c — the C side of placement, composition and the stack. See
 * rolltui_layout.h; the rules are rolltui/Layout.hpp's. */
#include "rolltui/c/rolltui_layout.h"

#include <math.h>
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
