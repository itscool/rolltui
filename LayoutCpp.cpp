// rolltui/LayoutCpp.cpp — the C++ side of placement, composition and the stack, behind the
// same boundary as `c/rolltui_layout.c` (rolltui/c/rolltui_layout.h, Phase 15 m5). One CMake
// flag picks which of the two links; both satisfy `rolltui/tests/layout_test.cpp`, every
// golden frame, the files-only proof and the authored screen, so a behavioural difference is
// a test failure on the day it appears rather than a review comment.
//
// This is the code Phase 9 m8 wrote and Phase 13 m5b thinned, moved behind the boundary. It
// is deliberately NOT a transliteration of the C: it keeps `std::vector` for the layer array
// and the per-layer node buffer, `std::string_view` comparisons, and lambdas where the C uses
// out-params — the comparison m5 is here to make is between two languages writing the same
// design naturally, not between one language and the other's shadow.
//
// THE TREE ITSELF IS C IN BOTH CONFIGURATIONS (`c/rolltui_layout_tree.h`), for the reason the
// span store is: it is DATA both implementations walk, and two copies of it would be two
// things that can disagree about what a node is.
#include "rolltui/c/rolltui_layout.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_lifetime.h"

namespace {

// The two enums the tree carries, in this file's own spelling. They are `rolltui::Border`
// and `RolltuiLayoutNode::Kind` (rolltui_layout_tree.h); the boundary carries a BYTE, so the
// conversion happens once here rather than at each of the twenty comparisons.
using Kind = RolltuiLayoutNode::Kind;
using Border = rolltui::Border;
unsigned char byte_of(Border b) { return static_cast<unsigned char>(b); }

// The inner rect as an expression, since the boundary's own form fills a caller's rect (the
// return-type-c-linkage rule at `rolltui_layout.h`).
RolltuiRect inner_of(RolltuiRect outer, Border b) {
  RolltuiRect r;
  rolltui_inner_rect(outer, byte_of(b), &r);
  return r;
}

RolltuiRect placement_of(const RolltuiPlacement& p, RolltuiRect parent) {
  RolltuiRect r;
  rolltui_placement_resolve(&p, parent, &r);
  return r;
}

std::string_view view(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }
std::string_view view(const char* p, std::size_t n) { return std::string_view(p ? p : "", n); }

enum class Align { Start, Center, End };

// The anchor ordinals are a 3x3 grid: column is the horizontal alignment, row the vertical.
Align align_h(unsigned char a) { return static_cast<Align>(a % 3); }
Align align_v(unsigned char a) { return static_cast<Align>(a / 3); }

// One axis of resolve(): {start, size} relative to the parent.
std::pair<int, int> resolve_axis(RolltuiDim pos, RolltuiDim size, Align align, const RolltuiOptDim& min,
                                 const RolltuiOptDim& max, bool clamp, int extent) {
  int start, len;
  if (align == Align::Start) {
    start = rolltui_resolve_dim(pos, extent);
    len = rolltui_resolve_dim(pos + size, extent) - start;
  } else {
    len = rolltui_resolve_dim(size, extent);
    const int point = rolltui_resolve_dim(pos, extent);
    start = (align == Align::Center) ? point - len / 2 : point - len;
  }
  if (len < 0) len = 0;
  if (max) len = std::min(len, std::max(rolltui_resolve_dim(*max, extent), 0));
  if (min) len = std::max(len, rolltui_resolve_dim(*min, extent));
  if (clamp) {
    len = std::min(len, std::max(extent, 0));
    start = std::clamp(start, 0, std::max(extent - len, 0));
  }
  return {start, len};
}

enum class Side { Left, Right, Top, Bottom };

bool edge_bordered(const RolltuiLayoutNode& n, Side side) {
  if (n.border != Border::None) return true;
  if (n.kind == Kind::Window) return false;
  // No vector: this only ever needs the FIRST visible child, the LAST, or a walk over all of
  // them, and it is recursive AND called O(children²) from place()'s shared-edge pass.
  const RolltuiLayoutNode* first = nullptr;
  const RolltuiLayoutNode* last = nullptr;
  for (const RolltuiLayoutNode& c : n.children)
    if (c.visible) {
      if (!first) first = &c;
      last = &c;
    }
  if (!first) return false;
  const bool along = (n.kind == Kind::Row) ? (side == Side::Left || side == Side::Right)
                                      : (side == Side::Top || side == Side::Bottom);
  if (along) return edge_bordered(*((side == Side::Left || side == Side::Top) ? first : last), side);
  for (const RolltuiLayoutNode& c : n.children)
    if (c.visible && !edge_bordered(c, side)) return false;
  return true;
}

// The per-container scratch of place(), as ONE inline array instead of three heap vectors
// (Phase 13 m5b). place() RECURSES, so a reused shared buffer would alias across depth — an
// inline array cannot, because each frame of the recursion has its own. More than twelve
// visible children spills to the heap: the same NAMED EXCEPTION shape `Cell`'s glyph spill
// uses, and the same number the C spills at so the two agree.
struct ChildSlot {
  const RolltuiLayoutNode* node = nullptr;
  bool shared = false;  // this child shares its facing border edge with the next
  int size = 0;
};
class ChildScratch {
 public:
  explicit ChildScratch(std::size_t n) : n_(n) {
    if (n > kInline) spill_.resize(n);
  }
  std::size_t size() const { return n_; }
  ChildSlot& operator[](std::size_t i) { return n_ > kInline ? spill_[i] : inline_[i]; }
  const ChildSlot& operator[](std::size_t i) const { return n_ > kInline ? spill_[i] : inline_[i]; }

 private:
  static constexpr std::size_t kInline = 12;
  std::size_t n_;
  ChildSlot inline_[kInline]{};
  std::vector<ChildSlot> spill_;
};

void place(const RolltuiLayoutNode& n, RolltuiRect box, RolltuiRect screen, std::size_t layer,
           RolltuiResolvedSink emit, void* ctx) {
  RolltuiResolvedNode rn;
  rn.node = &n;
  rn.outer = box;
  rn.inner = inner_of(box, n.border).intersect(screen);
  rn.focused = 0;
  rn.layer = layer;
  emit(ctx, &rn);
  if (n.kind == Kind::Window) return;

  const bool row = n.kind == Kind::Row;
  std::size_t visible = 0;
  for (const RolltuiLayoutNode& c : n.children)
    if (c.visible) ++visible;
  if (visible == 0) return;
  ChildScratch kid(visible);
  {
    std::size_t i = 0;
    for (const RolltuiLayoutNode& c : n.children)
      if (c.visible) kid[i++].node = &c;
  }
  const RolltuiRect in = inner_of(box, n.border);  // unclipped: the true inner box
  const int extent = row ? in.w : in.h;

  // Shared edges between adjacent bordered siblings.
  int shared_count = 0;
  for (std::size_t i = 0; i + 1 < visible; ++i) {
    kid[i].shared = row ? (edge_bordered(*kid[i].node, Side::Right) && edge_bordered(*kid[i + 1].node, Side::Left))
                        : (edge_bordered(*kid[i].node, Side::Bottom) && edge_bordered(*kid[i + 1].node, Side::Top));
    if (kid[i].shared) ++shared_count;
  }
  const int ext = std::max(extent, 0) + shared_count;

  // Fixed children: edges of the cumulative Dim sum.
  RolltuiDim cum;
  int prev_edge = 0, fixed_total = 0, weight_total = 0;
  for (std::size_t i = 0; i < visible; ++i) {
    if (kid[i].node->size.fill) {
      weight_total += std::max(kid[i].node->size.weight, 1);
      continue;
    }
    cum = cum + kid[i].node->size.dim;
    const int edge = rolltui_resolve_dim(cum, ext);
    kid[i].size = std::max(edge - prev_edge, 0);
    prev_edge = std::max(edge, prev_edge);
    fixed_total += kid[i].size;
  }
  // Fills: cumulative weight edges over the remainder.
  const int remainder = std::max(ext - fixed_total, 0);
  int cum_w = 0, prev_fill_edge = 0;
  for (std::size_t i = 0; i < visible; ++i) {
    if (!kid[i].node->size.fill) continue;
    cum_w += std::max(kid[i].node->size.weight, 1);
    const int edge = rolltui_resolve_dim(RolltuiDim::rel(static_cast<double>(cum_w) / weight_total), remainder);
    kid[i].size = edge - prev_fill_edge;
    prev_fill_edge = edge;
  }
  // Positions, sharing one cell per shared edge; clip to the extent in order.
  int pos = 0;
  for (std::size_t i = 0; i < visible; ++i) {
    if (pos + kid[i].size > std::max(extent, 0)) kid[i].size = std::max(std::max(extent, 0) - pos, 0);
    const RolltuiRect r = row ? RolltuiRect{in.x + pos, in.y, kid[i].size, in.h}
                              : RolltuiRect{in.x, in.y + pos, in.w, kid[i].size};
    place(*kid[i].node, r, screen, layer, emit, ctx);
    pos += kid[i].size;
    if (i + 1 < visible && kid[i].shared && kid[i].size > 0) pos -= 1;
  }
}

// ---- drawing --------------------------------------------------------------------------------

// Box-drawing arms: U D L R.
constexpr unsigned char U = 1, D = 2, L = 4, R = 8;

// Light glyph for an arm mask (index = mask); "" for 0.
constexpr const char* kLight[16] = {
    "",  "╵", "╷", "│",  // -, U, D, UD
    "╴", "┘", "┐", "┤",  // L, UL, DL, UDL
    "╶", "└", "┌", "├",  // R, UR, DR, UDR
    "─", "┴", "┬", "┼",  // LR, ULR, DLR, UDLR
};

// Box-drawing glyphs are East Asian AMBIGUOUS width (U+2500-257F): a terminal that renders
// ambiguous characters wide draws every border two cells wide and the whole frame garbles.
// So under `ascii` every border set falls back to + - |, the way vim's `ambiwidth=double`
// does — one cell everywhere, guaranteed.
const char* glyph_for(Border b, unsigned char mask, bool ascii) {
  if (mask == 0) return "";
  if (ascii) {
    if (mask == (L | R) || mask == L || mask == R) return "-";
    if (mask == (U | D) || mask == U || mask == D) return "|";
    return "+";
  }
  if (b == Border::Rounded) {
    switch (mask) {
      case D | R: return "╭";
      case D | L: return "╮";
      case U | R: return "╰";
      case U | L: return "╯";
      default: break;
    }
  }
  if (b == Border::Double) {
    switch (mask) {
      case L | R: return "═";
      case U | D: return "║";
      case D | R: return "╔";
      case D | L: return "╗";
      case U | R: return "╚";
      case U | L: return "╝";
      default: return kLight[mask];  // a joined double border is not modelled; light junctions
    }
  }
  if (b == Border::Heavy) {
    switch (mask) {
      case L | R: return "━";
      case U | D: return "┃";
      case D | R: return "┏";
      case D | L: return "┓";
      case U | R: return "┗";
      case U | L: return "┛";
      default: return kLight[mask];
    }
  }
  return kLight[mask];
}

bool joins(Border b) { return b == Border::Single || b == Border::Rounded; }

// The arm mask this window's own border wants at ring cell (x, y) of `o`.
unsigned char own_mask(RolltuiRect o, int x, int y) {
  const bool left = x == o.x, right = x == o.x + o.w - 1, top = y == o.y, bottom = y == o.y + o.h - 1;
  if (o.w == 1 && o.h == 1) return 0;
  if (o.w == 1) return static_cast<unsigned char>((top ? 0 : U) | (bottom ? 0 : D));
  if (o.h == 1) return static_cast<unsigned char>((left ? 0 : L) | (right ? 0 : R));
  if (top && left) return D | R;
  if (top && right) return D | L;
  if (bottom && left) return U | R;
  if (bottom && right) return U | L;
  if (top || bottom) return L | R;
  return U | D;
}

// Draws the border of `outer` clipped to the frame; `map` (W×H arm masks, or null) is the
// layer's join map: the previous masks on the ring are OR'd in and the result written back.
void draw_border_impl(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, Border b, RolltuiStyle line,
                      std::string_view title, RolltuiStyle title_style, bool ascii, unsigned char* map,
                      const unsigned char* ring_before) {
  if (b == Border::None || outer.w <= 0 || outer.h <= 0) return;
  const int fw = rolltui_frame_width(f), fh = rolltui_frame_height(f);
  const RolltuiRect clip = outer.intersect(RolltuiRect{0, 0, fw, fh});
  if (clip.empty()) return;
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      const bool ring = x == outer.x || x == outer.x + outer.w - 1 || y == outer.y || y == outer.y + outer.h - 1;
      if (!ring) continue;
      unsigned char m = own_mask(outer, x, y);
      if (map && ring_before && joins(b)) m |= ring_before[static_cast<std::size_t>(y * fw + x)];
      std::string_view g(glyph_for(b, m, ascii));
      if (g.empty()) g = " ";
      rolltui_frame_put(f, x, y, g.data(), g.size(), 1, line, 0);
      if (map) map[static_cast<std::size_t>(y * fw + x)] = joins(b) ? m : 0;
    }
  }
  // Title on the top edge, inside the corners. THREE RUNS, NOT A BUILT STRING: `" " + title
  // + " "` was a `std::string` per bordered window per frame, and the only reason it existed
  // is that put_text takes one run at a time.
  if (!title.empty() && outer.w >= 5 && outer.y >= 0 && outer.y < fh) {
    const int avail = outer.w - 2;
    int used = rolltui_frame_put_text(f, draw, outer.x + 1, outer.y, " ", 1, title_style, avail, ascii, 0);
    used += rolltui_frame_put_text(f, draw, outer.x + 1 + used, outer.y, title.data(), title.size(), title_style,
                                   avail - used, ascii, 0);
    used += rolltui_frame_put_text(f, draw, outer.x + 1 + used, outer.y, " ", 1, title_style, avail - used, ascii,
                                   0);
    if (map)
      for (int x = outer.x + 1; x < outer.x + 1 + used && x < fw; ++x)
        if (x >= 0) map[static_cast<std::size_t>(outer.y * fw + x)] = 0;
  }
}

// ---- the widget-kind registry ------------------------------------------------------------

struct KindRow {
  const char* name;
  unsigned char rule;
  const char* source_is;
};

// THE TABLE (Layout.hpp's header comment is its documentation). One definition site per
// implementation, and the ORDER is `rolltui::WidgetKind`'s.
constexpr KindRow kKinds[] = {
    {"transcript", ROLLTUI_SOURCE_REQUIRED, "a document the host binds"},
    {"input", ROLLTUI_SOURCE_REQUIRED, "the target a submitted line goes to"},
    {"menu", ROLLTUI_SOURCE_REQUIRED, "a menu file"},
    {"rows", ROLLTUI_SOURCE_REQUIRED, "a row source the host binds"},
    {"text", ROLLTUI_SOURCE_OPTIONAL, "the literal text"},
    {"file", ROLLTUI_SOURCE_REQUIRED, "a path"},
    {"help", ROLLTUI_SOURCE_OPTIONAL, "one key scope, or every one when empty"},
};

// Phase 9's slot names and Phase 10's `custom:` contents → their m3 form. A closed, one-way
// table; the five composites are why it is a MAP rather than a rule (Layout.hpp).
constexpr std::pair<const char*, const char*> kLegacy[] = {
    {"transcript", "transcript:session"},  {"input", "input:prompt"},        {"status", "rows:status"},
    {"menu", "menu:main"},                 {"custom:approval", "approval"},  {"custom:details", "details"},
    {"custom:editor", "editor"},           {"custom:confirm", "confirm"},    {"custom:report", "report"},
};

// RUNG 2, and the one thing in this file that is RETAINED: a kind is a program's vocabulary,
// not one screen's. Released at `rolltui::shutdown()`, registered AT FILL TIME and touching
// the storage rather than the accessor — the rule m3 wrote after two latent defects in the
// built-in layout cache.
struct HostKind {
  std::string name;
  unsigned char rule;
  std::string source_is;
};
std::vector<HostKind>& host_kinds() {
  static std::vector<HostKind> v;
  return v;
}
bool& kind_releaser_registered() {
  static bool b = false;
  return b;
}
const HostKind* host_kind(std::string_view name) {
  for (const HostKind& h : host_kinds())
    if (h.name == name) return &h;
  return nullptr;
}
const KindRow* library_kind(std::string_view name, std::size_t* index) {
  for (std::size_t i = 0; i < std::size(kKinds); ++i)
    if (name == kKinds[i].name) {
      if (index) *index = i;
      return &kKinds[i];
    }
  return nullptr;
}

}  // namespace

// ---- placement -------------------------------------------------------------------------------

extern "C" int rolltui_resolve_dim(RolltuiDim d, int extent) {
  return static_cast<int>(std::floor(d.fraction * extent + 1e-6)) + d.cells;
}

extern "C" void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out) {
  auto [x, w] = resolve_axis(p->x, p->w, align_h(static_cast<unsigned char>(p->anchor)), p->min_w, p->max_w,
                             p->clamp != 0, parent.w);
  auto [y, h] = resolve_axis(p->y, p->h, align_v(static_cast<unsigned char>(p->anchor)), p->min_h, p->max_h,
                             p->clamp != 0, parent.h);
  *out = RolltuiRect{parent.x + x, parent.y + y, w, h};
}

extern "C" void rolltui_inner_rect(RolltuiRect outer, unsigned char border, RolltuiRect* out) {
  if (static_cast<Border>(border) == Border::None) {
    *out = outer;
    return;
  }
  *out = RolltuiRect{outer.x + 1, outer.y + 1, outer.w - 2, std::max(outer.h - 2, 0)};
  if (out->w < 0) out->w = 0;
}

extern "C" void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen,
                                     std::size_t layer, RolltuiResolvedSink emit, void* ctx) {
  if (root->visible) place(*root, box, screen, layer, emit, ctx);
}


// ---- the text forms ---------------------------------------------------------------------------

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

bool parse_int(std::string_view s, int& out) {
  s = trim(s);
  if (s.empty()) return false;
  std::size_t i = 0;
  bool neg = false;
  if (s[i] == '-' || s[i] == '+') {
    neg = s[i] == '-';
    ++i;
  }
  if (i >= s.size()) return false;
  long v = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
    if (v > 1000000) return false;
  }
  out = static_cast<int>(neg ? -v : v);
  return true;
}

std::size_t fill_out(std::string_view s, char* out, std::size_t cap) {
  std::size_t n = std::min(s.size(), cap ? cap - 1 : 0);
  if (cap) {
    std::memcpy(out, s.data(), n);
    out[n] = '\0';
  }
  return n;
}

}  // namespace

extern "C" int rolltui_parse_dim(const char* text, std::size_t len, RolltuiDim* out) {
  std::string_view t = trim(view(text, len));
  const std::size_t pct = t.find('%');
  if (pct == std::string_view::npos) return 0;
  const std::string_view num = trim(t.substr(0, pct));
  if (num.empty()) return 0;
  // The percentage: an integer or a decimal, optionally signed.
  const std::string tmp(num);
  char* end = nullptr;
  const double f = std::strtod(tmp.c_str(), &end);
  if (end != tmp.c_str() + tmp.size()) return 0;
  for (char c : tmp)
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+')) return 0;
  const std::string_view rest = trim(t.substr(pct + 1));
  int cells = 0;
  if (!rest.empty()) {
    if (rest[0] != '+' && rest[0] != '-') return 0;
    int mag = 0;
    if (!parse_int(rest.substr(1), mag) || mag < 0) return 0;
    cells = rest[0] == '-' ? -mag : mag;
  }
  *out = RolltuiDim::rel(f / 100.0, cells);
  return 1;
}

extern "C" std::size_t rolltui_dim_to_string(RolltuiDim d, char* out, std::size_t cap) {
  if (d.fraction == 0) return fill_out(std::to_string(d.cells), out, cap);
  char buf[64];
  const double pct = d.fraction * 100.0;
  if (std::fabs(pct - std::round(pct)) < 1e-9)
    std::snprintf(buf, sizeof buf, "%d%%", static_cast<int>(std::round(pct)));
  else
    std::snprintf(buf, sizeof buf, "%g%%", pct);
  std::string s = buf;
  if (d.cells > 0) s += " + " + std::to_string(d.cells);
  else if (d.cells < 0) s += " - " + std::to_string(-d.cells);
  return fill_out(s, out, cap);
}

extern "C" int rolltui_parse_split_size(const char* text, std::size_t len, RolltuiSplitSize* out) {
  const std::string_view t = trim(view(text, len));
  if (t == "fill") {
    *out = RolltuiSplitSize::filling(1);
    return 1;
  }
  if (t.rfind("fill", 0) == 0) {
    int w = 0;
    if (parse_int(t.substr(4), w) && w >= 1) {
      *out = RolltuiSplitSize::filling(w);
      return 1;
    }
    return 0;
  }
  RolltuiDim d;
  if (rolltui_parse_dim(t.data(), t.size(), &d)) {
    *out = RolltuiSplitSize::fixed(d);
    return 1;
  }
  return 0;
}

extern "C" int rolltui_parse_size_text(const char* text, std::size_t len, RolltuiSplitSize* out) {
  if (rolltui_parse_split_size(text, len, out)) return 1;
  const std::string_view t = view(text, len);
  if (t.empty() || t.size() > 9) return 0;
  for (char c : t)
    if (c < '0' || c > '9') return 0;
  *out = RolltuiSplitSize::fixed(RolltuiDim::abs(std::atoi(std::string(t).c_str())));
  return 1;
}

extern "C" std::size_t rolltui_split_size_to_string(RolltuiSplitSize s, char* out, std::size_t cap) {
  if (!s.fill) return rolltui_dim_to_string(s.dim, out, cap);
  return fill_out(s.weight == 1 ? std::string("fill") : "fill " + std::to_string(s.weight), out, cap);
}

// ---- drawing ---------------------------------------------------------------------------------

struct RolltuiComposeScratch {
  std::vector<unsigned char> map, before;    // the join arms, and what was under the ring
  std::vector<RolltuiResolvedNode> nodes;    // ONE layer's resolved nodes, reused per layer
  RolltuiDrawScratch* draw = nullptr;        // OWNED: the cluster walk a title needs
};

// OWNERSHIP CROSSES HERE, spelled with a `unique_ptr` on both sides of the crossing rather
// than a bare `new`/`delete` — the shape `rolltui_frame_new` already uses, and the one the
// ownership test refuses to let anything else be.
extern "C" RolltuiComposeScratch* rolltui_compose_scratch_new(void) {
  std::unique_ptr<RolltuiComposeScratch> s = std::make_unique<RolltuiComposeScratch>();
  s->draw = rolltui_draw_scratch_new();
  return s.release();
}

extern "C" void rolltui_compose_scratch_free(RolltuiComposeScratch* s) {
  const std::unique_ptr<RolltuiComposeScratch> owned(s);  // takes it back, frees it on the way out
  if (s) rolltui_draw_scratch_free(s->draw);
}

extern "C" void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer,
                                    unsigned char border, RolltuiStyle line, const char* title,
                                    std::size_t title_n, RolltuiStyle title_style, int ambiguous_wide) {
  draw_border_impl(f, draw, outer, static_cast<Border>(border), line, view(title, title_n), title_style,
                   ambiguous_wide != 0, nullptr, nullptr);
}

extern "C" void rolltui_compose_layer(RolltuiFrame* f, const RolltuiResolvedNode* nodes, std::size_t count,
                                      const RolltuiStyle* styles, const RolltuiLayoutRoles* roles,
                                      RolltuiSlotFn render, void* ctx, int ambiguous_wide,
                                      RolltuiComposeScratch* scratch) {
  const int fw = rolltui_frame_width(f), fh = rolltui_frame_height(f);
  const RolltuiRect bounds{0, 0, fw, fh};
  scratch->map.assign(static_cast<std::size_t>(std::max(fw, 0) * std::max(fh, 0)), 0);
  scratch->before.assign(scratch->map.size(), 0);
  // TWO PASSES: every node's ground and border first, then every window's CONTENT (Phase 12
  // m7). One pass was correct while a slot only ever drew inside `rn.inner`, which excludes
  // its own border — but m5's scrollbar deliberately draws into the window's right BORDER
  // column, and two adjacent bordered siblings SHARE that column. In one pass the next
  // sibling's border was then drawn over the thumb, so a transcript with a panel to its right
  // had a scrollbar that was computed, positioned, hit-tested and INVISIBLE.
  for (std::size_t i = 0; i < count; ++i) {
    const RolltuiResolvedNode& rn = nodes[i];
    const RolltuiLayoutNode& n = *rn.node;
    if (n.kind != Kind::Window && n.border == Border::None) continue;
    const RolltuiStyle ground = styles[static_cast<unsigned char>(n.background)];
    // Remember the join map under this window's ring, then clear the outer rect.
    const RolltuiRect clip = rn.outer.intersect(bounds);
    for (int y = clip.y; y < clip.y + clip.h; ++y)
      for (int x = clip.x; x < clip.x + clip.w; ++x) {
        const std::size_t k = static_cast<std::size_t>(y * fw + x);
        scratch->before[k] = scratch->map[k];
        scratch->map[k] = 0;
      }
    rolltui_frame_fill(f, scratch->draw, rn.outer, ground, nullptr, 0);
    RolltuiStyle line = styles[rn.focused ? roles->border_active : roles->border];
    line.bg = ground.bg;
    RolltuiStyle title = styles[roles->title];
    title.bg = ground.bg;
    draw_border_impl(f, scratch->draw, rn.outer, n.border, line, view(n.title), title, ambiguous_wide != 0,
                     scratch->map.data(), scratch->before.data());
  }
  if (!render) return;
  for (std::size_t i = 0; i < count; ++i)
    if (nodes[i].node->kind == Kind::Window && !nodes[i].inner.empty()) render(ctx, &nodes[i], f);
}

// ---- the widget-kind registry ------------------------------------------------------------------

extern "C" int rolltui_widget_kind_resolve(const char* name, std::size_t len, unsigned char* ordinal,
                                           unsigned char* rule, const char** source_is,
                                           std::size_t* source_is_len) {
  const std::string_view n = view(name, len);
  std::size_t i = 0;
  // THE RESOLUTION ORDER (Layout.hpp). Rung 1 first, unconditionally.
  if (const KindRow* r = library_kind(n, &i)) {
    if (ordinal) *ordinal = static_cast<unsigned char>(i);
    if (rule) *rule = r->rule;
    if (source_is) *source_is = r->source_is;
    if (source_is_len) *source_is_len = std::strlen(r->source_is);
    return ROLLTUI_KIND_LIBRARY;
  }
  if (const HostKind* h = host_kind(n)) {
    if (rule) *rule = h->rule;
    if (source_is) *source_is = h->source_is.c_str();
    if (source_is_len) *source_is_len = h->source_is.size();
    return ROLLTUI_KIND_HOST;
  }
  return ROLLTUI_KIND_UNKNOWN;
}

extern "C" std::size_t rolltui_widget_kind_library_count(void) { return std::size(kKinds); }

extern "C" const char* rolltui_widget_kind_library_name(std::size_t i, std::size_t* len) {
  const char* s = i < std::size(kKinds) ? kKinds[i].name : "";
  if (len) *len = std::strlen(s);
  return s;
}

extern "C" unsigned char rolltui_widget_kind_library_rule(std::size_t i) {
  return i < std::size(kKinds) ? kKinds[i].rule : ROLLTUI_SOURCE_REQUIRED;
}

extern "C" const char* rolltui_widget_kind_library_source_is(std::size_t i, std::size_t* len) {
  const char* s = i < std::size(kKinds) ? kKinds[i].source_is : "";
  if (len) *len = std::strlen(s);
  return s;
}

extern "C" std::size_t rolltui_widget_kind_host_count(void) { return host_kinds().size(); }

extern "C" const char* rolltui_widget_kind_host_name(std::size_t i, std::size_t* len) {
  if (i >= host_kinds().size()) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = host_kinds()[i].name.size();
  return host_kinds()[i].name.c_str();
}

extern "C" int rolltui_widget_kind_register(const char* name, std::size_t len, unsigned char rule,
                                            const char* source_is, std::size_t source_is_len) {
  const std::string_view n = view(name, len);
  if (n.empty()) return ROLLTUI_REGISTER_EMPTY;
  if (n.find(':') != std::string_view::npos) return ROLLTUI_REGISTER_HAS_COLON;
  if (library_kind(n, nullptr)) return ROLLTUI_REGISTER_IS_LIBRARY;
  if (const HostKind* h = host_kind(n))
    return h->rule == rule ? ROLLTUI_REGISTER_OK : ROLLTUI_REGISTER_RULE_DIFFERS;
  host_kinds().push_back({std::string(n), rule, std::string(view(source_is, source_is_len))});
  if (!kind_releaser_registered()) {
    kind_releaser_registered() = true;
    rolltui_on_shutdown(rolltui_widget_kind_clear);
  }
  return ROLLTUI_REGISTER_OK;
}

extern "C" void rolltui_widget_kind_clear(void) {
  // The releaser touches the STORAGE, and the flag is cleared so a registry emptied by
  // `shutdown()` and used again registers itself again (m3's rule, stated at its two defects).
  host_kinds().clear();
  host_kinds().shrink_to_fit();
  kind_releaser_registered() = false;
}

extern "C" const char* rolltui_migrated_content(const char* legacy, std::size_t len, std::size_t* out_len) {
  const std::string_view l = view(legacy, len);
  for (const auto& [from, to] : kLegacy)
    if (l == from) {
      if (out_len) *out_len = std::strlen(to);
      return to;
    }
  return nullptr;
}

// ---- the stack ------------------------------------------------------------------------------------

namespace {

const RolltuiLayoutNode* find_in(const RolltuiLayoutNode& n, std::string_view id) {
  if (!id.empty() && view(n.id) == id) return &n;
  for (const RolltuiLayoutNode& c : n.children)
    if (const RolltuiLayoutNode* f = find_in(c, id)) return f;
  return nullptr;
}

// No vector: this runs once per layer per resolve — three times a frame — and only ever needs
// the FIRST focusable or the one matching a name (Phase 13 m5b).
const RolltuiLayoutNode* first_focusable(const RolltuiLayoutNode& n) {
  if (!n.visible) return nullptr;
  if (n.kind == Kind::Window) return n.focusable ? &n : nullptr;
  for (const RolltuiLayoutNode& c : n.children)
    if (const RolltuiLayoutNode* f = first_focusable(c)) return f;
  return nullptr;
}

const RolltuiLayoutNode* focusable_named(const RolltuiLayoutNode& n, std::string_view id) {
  if (!n.visible) return nullptr;
  if (n.kind == Kind::Window) return (n.focusable && view(n.id) == id) ? &n : nullptr;
  for (const RolltuiLayoutNode& c : n.children)
    if (const RolltuiLayoutNode* f = focusable_named(c, id)) return f;
  return nullptr;
}

const RolltuiLayoutNode* layer_focused(const RolltuiLayer& l) {
  if (l.focus.n)
    if (const RolltuiLayoutNode* named = focusable_named(l.root, view(l.focus))) return named;
  return first_focusable(l.root);
}

void focusables(const RolltuiLayoutNode& n, std::vector<const RolltuiLayoutNode*>& out) {
  if (!n.visible) return;
  if (n.kind == Kind::Window) {
    if (n.focusable) out.push_back(&n);
    return;
  }
  for (const RolltuiLayoutNode& c : n.children) focusables(c, out);
}

// The sink wrapper that stamps `focused` on every node on its way past, so the stack never
// buffers what a caller is about to consume anyway.
struct StackSink {
  RolltuiResolvedSink emit;
  void* ctx;
  const RolltuiLayoutNode* focused;
};
void stack_sink(void* ctx, const RolltuiResolvedNode* rn) {
  auto* s = static_cast<StackSink*>(ctx);
  RolltuiResolvedNode out = *rn;
  out.focused = static_cast<unsigned char>(rn->node == s->focused);
  s->emit(s->ctx, &out);
}
void collect_sink(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<RolltuiResolvedNode>*>(ctx)->push_back(*rn);
}

}  // namespace

struct RolltuiWindowStack {
  std::vector<RolltuiLayer> layers{1};  // OWNED; always at least the base
  RolltuiStr captured;
};

extern "C" RolltuiWindowStack* rolltui_window_stack_new(void) {
  return std::make_unique<RolltuiWindowStack>().release();
}
extern "C" void rolltui_window_stack_free(RolltuiWindowStack* s) {
  const std::unique_ptr<RolltuiWindowStack> owned(s);
}

extern "C" void rolltui_window_stack_set_base(RolltuiWindowStack* s, const RolltuiLayer* base) {
  RolltuiStr keep = s->layers.front().focus;
  s->layers.front() = *base;
  if (base->focus.n == 0 && keep.n && find_in(s->layers.front().root, view(keep)))
    s->layers.front().focus = std::move(keep);
}

extern "C" RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s) { return &s->layers.front(); }

extern "C" void rolltui_window_stack_push(RolltuiWindowStack* s, RolltuiLayer* popup) {
  s->layers.push_back(std::move(*popup));
}

extern "C" int rolltui_window_stack_pop(RolltuiWindowStack* s) {
  if (s->layers.size() <= 1) return 0;
  s->layers.pop_back();
  return 1;
}

extern "C" std::size_t rolltui_window_stack_depth(const RolltuiWindowStack* s) { return s->layers.size(); }

extern "C" const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, std::size_t i) {
  return i < s->layers.size() ? &s->layers[i] : nullptr;
}

extern "C" int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, std::size_t len) {
  for (std::size_t i = 1; i < s->layers.size(); ++i)
    if (view(s->layers[i].id) == view(id, len)) return 1;
  return 0;
}

extern "C" RolltuiLayoutNode* rolltui_window_stack_find(const RolltuiWindowStack* s, const char* id,
                                                       std::size_t len) {
  for (const RolltuiLayer& l : s->layers)
    if (const RolltuiLayoutNode* n = find_in(l.root, view(id, len)))
      return const_cast<RolltuiLayoutNode*>(n);
  return nullptr;
}

extern "C" std::size_t rolltui_window_stack_focus_layer(const RolltuiWindowStack* s) {
  const std::size_t top = s->layers.size() - 1;
  if (s->layers[top].modal) return top;
  for (std::size_t i = s->layers.size(); i-- > 0;)
    if (layer_focused(s->layers[i])) return i;
  return top;
}

extern "C" const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s) {
  return layer_focused(s->layers[rolltui_window_stack_focus_layer(s)]);
}

extern "C" void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, std::size_t len) {
  RolltuiLayer& l = s->layers[rolltui_window_stack_focus_layer(s)];
  if (focusable_named(l.root, view(id, len))) l.focus = view(id, len);
}

extern "C" void rolltui_window_stack_cycle_focus(RolltuiWindowStack* s, int backwards) {
  RolltuiLayer& l = s->layers[rolltui_window_stack_focus_layer(s)];
  std::vector<const RolltuiLayoutNode*> f;
  focusables(l.root, f);
  if (f.empty()) return;
  const RolltuiLayoutNode* cur = layer_focused(l);
  std::size_t i = 0;
  for (; i < f.size(); ++i)
    if (f[i] == cur) break;
  if (i >= f.size()) i = 0;
  i = backwards ? (i + f.size() - 1) % f.size() : (i + 1) % f.size();
  l.focus = view(f[i]->id);
}

extern "C" void rolltui_window_stack_resolve(const RolltuiWindowStack* s, RolltuiRect screen,
                                             RolltuiResolvedSink emit, void* ctx) {
  StackSink w{emit, ctx, rolltui_window_stack_focused(s)};
  for (std::size_t i = 0; i < s->layers.size(); ++i) {
    const RolltuiRect box = placement_of(s->layers[i].placement, screen);
    rolltui_resolve_tree(&s->layers[i].root, box, screen, i, stack_sink, &w);
  }
}

extern "C" void rolltui_window_stack_compose(const RolltuiWindowStack* s, RolltuiFrame* f, RolltuiRect screen,
                                             const RolltuiStyle* styles, const RolltuiLayoutRoles* roles,
                                             RolltuiSlotFn render, void* ctx, int ambiguous_wide,
                                             RolltuiComposeScratch* scratch) {
  // THE PER-LAYER BUFFER IS THE SCRATCH'S, not a local: a compose runs every frame, and a
  // fresh vector here is one allocation plus its growth per layer per frame — which is
  // exactly the kind of thing the budget exists to make impossible to leave in.
  std::vector<RolltuiResolvedNode>& mine = scratch->nodes;
  StackSink w{collect_sink, &mine, rolltui_window_stack_focused(s)};
  for (std::size_t i = 0; i < s->layers.size(); ++i) {
    if (s->layers[i].modal) rolltui_frame_tint(f, screen, styles[roles->overlay]);
    mine.clear();
    const RolltuiRect box = placement_of(s->layers[i].placement, screen);
    rolltui_resolve_tree(&s->layers[i].root, box, screen, i, stack_sink, &w);
    rolltui_compose_layer(f, mine.data(), mine.size(), styles, roles, render, ctx, ambiguous_wide, scratch);
  }
}

extern "C" unsigned char rolltui_window_stack_route(RolltuiWindowStack* s, const RolltuiEvent* e,
                                                    RolltuiRect screen, const RolltuiBindings* bindings,
                                                    const RolltuiStackActions* actions, RolltuiStr* window) {
  window->clear();
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent& m = e->mouse;
    // A captured pointer: drags and the release go to the pressed window, wherever the
    // pointer is now (the window may even have gone: then the capture just ends).
    if (s->captured.n && (m.kind == RolltuiMouseEvent::Kind::Drag || m.kind == RolltuiMouseEvent::Kind::Release)) {
      RolltuiStr target = s->captured;
      if (m.kind == RolltuiMouseEvent::Kind::Release) s->captured.clear();
      if (rolltui_window_stack_find(s, target.p, target.n)) {
        *window = std::move(target);
        return ROLLTUI_ROUTE_DELIVER;
      }
      return ROLLTUI_ROUTE_DROPPED;
    }
    std::vector<RolltuiResolvedNode> all;
    StackSink w{collect_sink, &all, rolltui_window_stack_focused(s)};
    for (std::size_t i = 0; i < s->layers.size(); ++i) {
      const RolltuiRect box = placement_of(s->layers[i].placement, screen);
      rolltui_resolve_tree(&s->layers[i].root, box, screen, i, stack_sink, &w);
    }
    const std::size_t top = s->layers.size() - 1;
    for (std::size_t k = all.size(); k-- > 0;) {
      const RolltuiResolvedNode& rn = all[k];
      if (rn.node->kind != Kind::Window || !rn.outer.intersect(screen).contains(m.x, m.y)) continue;
      if (s->layers[top].modal && rn.layer != top) return ROLLTUI_ROUTE_DROPPED;
      if (m.kind == RolltuiMouseEvent::Kind::Press) {
        if (rn.node->focusable && rn.layer == rolltui_window_stack_focus_layer(s))
          rolltui_window_stack_focus(s, rn.node->id.p, rn.node->id.n);
        s->captured = view(rn.node->id);
      }
      *window = view(rn.node->id);
      return ROLLTUI_ROUTE_DELIVER;
    }
    return ROLLTUI_ROUTE_DROPPED;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_action_for(bindings, &e->key, "stack", 5, &alen);
    const std::string_view action = a ? std::string_view(a, alen) : std::string_view();
    if (action == actions->close_popup && s->layers.size() > 1) {
      *window = view(s->layers.back().id);
      rolltui_window_stack_pop(s);
      return ROLLTUI_ROUTE_CLOSED_POPUP;
    }
    if (!action.empty() && (action == actions->focus_next || action == actions->focus_prev)) {
      std::vector<const RolltuiLayoutNode*> f;
      focusables(s->layers[rolltui_window_stack_focus_layer(s)].root, f);
      if (f.size() > 1) {
        rolltui_window_stack_cycle_focus(s, action == actions->focus_prev);
        *window = view(rolltui_window_stack_focused(s)->id);
        return ROLLTUI_ROUTE_FOCUS_MOVED;
      }
    }
  }
  const RolltuiLayoutNode* f = rolltui_window_stack_focused(s);
  if (!f) return ROLLTUI_ROUTE_DROPPED;
  *window = view(f->id);
  return ROLLTUI_ROUTE_DELIVER;
}

extern "C" const char* rolltui_window_stack_captured(const RolltuiWindowStack* s, std::size_t* len) {
  if (len) *len = s->captured.n;
  return s->captured.c_str();
}
