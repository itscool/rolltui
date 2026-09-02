//
// layout_test.cpp — milestone 8 (plan/phase-9.md): placement, the split tree, layout
// files, composition with joining borders, and focus routing.
//
//   1. resolve(): the table, then the tiling properties at every width 0..300
//      (halves and quarters never overlap or gap; rel(1) fills; a start-anchored rel
//      width is floor or floor+1, never less).
//   2. Text forms: parse_dim / dim_to_string / parse_split_size round trips and rejects.
//   3. The loader: the built-ins load clean; an unknown key, a bad dim, a duplicate id
//      and a dangling focus are each named by path; layout_to_json round-trips.
//   4. The split: fixed + fill sums to the extent; 50% + 50% tiles at odd widths;
//      fills divide by weight; shared edges overlap by one cell; hidden nodes take no
//      space; overflow clips in order; shortfall leaves space.
//   5. Composition: the default layout at 80x24 has ┬ ├ ┴ ┤ at the junctions; a popup
//      never joins the base; a modal tints what is beneath and not itself; the focused
//      window's border is border_active.
//   6. The stack: initial focus from the layout, Tab / Shift-Tab cycle, a modal popup
//      takes focus and Escape returns it, a non-focusable notice leaves focus alone,
//      a mouse press under a modal is dropped, a press on a focusable base window
//      focuses it, set_base keeps focus across a reload when the id survives.
//
#include <cmath>
#include <string>
#include <vector>

#include "rolltui/Layout.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

std::string rect_str(Rect r) {
  return "{" + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h) + "}";
}

void expect_rect(const std::string& name, Rect got, Rect want) {
  check(got == want, name + ": " + rect_str(got) + (got == want ? "" : " (want " + rect_str(want) + ")"));
}

Placement P(Dim x, Dim y, Dim w, Dim h, Anchor a = Anchor::TopLeft, bool clamp = true) {
  Placement p;
  p.x = x; p.y = y; p.w = w; p.h = h; p.anchor = a; p.clamp = clamp;
  return p;
}

const ResolvedNode* by_id(const std::vector<ResolvedNode>& v, std::string_view id) {
  for (const ResolvedNode& rn : v)
    if (rn.node->id == id) return &rn;
  return nullptr;
}

std::string cell(const Frame& f, int x, int y) { return f.at(x, y).text; }

Node win(const char* content, SplitSize size = {}, Border b = Border::Single, bool focusable = false) {
  Node n = Node::window(content, size);
  n.border = b;
  n.focusable = focusable;
  return n;
}

}  // namespace

int main() {
  const Rect scr{0, 0, 80, 24};
  const Dim A0 = Dim::abs(0);

  // ---- 1. resolve(): the table -----------------------------------------------------------
  std::printf("-- resolve table\n");
  expect_rect("all absolute", resolve(P(Dim::abs(2), Dim::abs(3), Dim::abs(10), Dim::abs(5)), scr), {2, 3, 10, 5});
  expect_rect("rel(1) fills 81x25", resolve(P(A0, A0, Dim::rel(1), Dim::rel(1)), {0, 0, 81, 25}), {0, 0, 81, 25});
  expect_rect("rel(1) fills 1x1", resolve(P(A0, A0, Dim::rel(1), Dim::rel(1)), {0, 0, 1, 1}), {0, 0, 1, 1});
  expect_rect("rel(0.5) width floors at 81", resolve(P(A0, A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 81, 25}), {0, 0, 40, 25});
  expect_rect("second half starts at 40 and takes the remainder at 81",
              resolve(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 81, 25}), {40, 0, 41, 25});
  expect_rect("halves at width 7: first", resolve(P(A0, A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 7, 1}), {0, 0, 3, 1});
  expect_rect("halves at width 7: second", resolve(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 7, 1}), {3, 0, 4, 1});
  expect_rect("thirds at 10: first", resolve(P(A0, A0, Dim::rel(1.0 / 3), Dim::rel(1)), {0, 0, 10, 1}), {0, 0, 3, 1});
  expect_rect("thirds at 10: second", resolve(P(Dim::rel(1.0 / 3), A0, Dim::rel(1.0 / 3), Dim::rel(1)), {0, 0, 10, 1}), {3, 0, 3, 1});
  expect_rect("thirds at 10: third fills to the edge (1e-6 tolerance)",
              resolve(P(Dim::rel(2.0 / 3), A0, Dim::rel(1.0 / 3), Dim::rel(1)), {0, 0, 10, 1}), {6, 0, 4, 1});
  expect_rect("rel(1, -32): the rest minus the panel", resolve(P(A0, A0, Dim::rel(1, -32), Dim::rel(1, -3)), scr), {0, 0, 48, 21});
  expect_rect("panel at rel(1, -32) with abs 32", resolve(P(Dim::rel(1, -32), A0, Dim::abs(32), Dim::rel(1)), scr), {48, 0, 32, 24});
  expect_rect("input at y rel(1, -3) h 3 tiles with h rel(1, -3)", resolve(P(A0, Dim::rel(1, -3), Dim::rel(1), Dim::abs(3)), scr), {0, 21, 80, 3});
  expect_rect("negative size clamps to 0", resolve(P(A0, A0, Dim::rel(1, -32), Dim::rel(1)), {0, 0, 20, 5}), {0, 0, 0, 5});
  {
    Placement p = P(A0, A0, Dim::rel(1, -32), Dim::rel(1));
    p.min_w = Dim::abs(10);
    expect_rect("min_w lifts a negative size", resolve(p, {0, 0, 20, 5}), {0, 0, 10, 5});
  }
  expect_rect("center anchor, even size", resolve(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(20), Dim::abs(10), Anchor::Center), scr), {30, 7, 20, 10});
  expect_rect("center anchor at 81x25 lands on the same cell", resolve(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(20), Dim::abs(10), Anchor::Center), {0, 0, 81, 25}), {30, 7, 20, 10});
  expect_rect("center anchor, odd size (integer half)", resolve(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(21), Dim::abs(11), Anchor::Center), scr), {30, 7, 21, 11});
  expect_rect("bottom-right anchor", resolve(P(Dim::rel(1), Dim::rel(1), Dim::abs(10), Dim::abs(3), Anchor::BottomRight), scr), {70, 21, 10, 3});
  expect_rect("right anchor with a rel width floors (no edge rule)", resolve(P(Dim::rel(1), A0, Dim::rel(0.25), Dim::rel(1), Anchor::Right), {0, 0, 81, 1}), {61, 0, 20, 1});
  expect_rect("right anchor at x rel(0.5) w rel(0.5) sizes 40 at 81", resolve(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1), Anchor::Right), {0, 0, 81, 1}), {0, 0, 40, 1});
  expect_rect("top anchor centres horizontally only", resolve(P(Dim::rel(0.5), Dim::abs(2), Dim::abs(10), Dim::abs(4), Anchor::Top), scr), {35, 2, 10, 4});
  expect_rect("clamp moves an overflowing x back", resolve(P(Dim::abs(75), A0, Dim::abs(10), Dim::abs(2)), scr), {70, 0, 10, 2});
  expect_rect("clamp off leaves the overflow", resolve(P(Dim::abs(75), A0, Dim::abs(10), Dim::abs(2), Anchor::TopLeft, false), scr), {75, 0, 10, 2});
  expect_rect("clamp shrinks an oversize width to the parent", resolve(P(A0, A0, Dim::abs(100), Dim::abs(2)), scr), {0, 0, 80, 2});
  expect_rect("clamp lifts a negative x", resolve(P(Dim::abs(-5), Dim::abs(-1), Dim::abs(10), Dim::abs(2)), scr), {0, 0, 10, 2});
  expect_rect("clamp off keeps a negative x", resolve(P(Dim::abs(-5), A0, Dim::abs(10), Dim::abs(2), Anchor::TopLeft, false), scr), {-5, 0, 10, 2});
  {
    Placement p = P(A0, A0, Dim::rel(0.5), Dim::rel(1));
    p.max_w = Dim::abs(30);
    expect_rect("max_w caps", resolve(p, scr), {0, 0, 30, 24});
    p.min_w = Dim::abs(50);
    expect_rect("min wins over max", resolve(p, scr), {0, 0, 50, 24});
    Placement q = P(A0, A0, Dim::rel(0.5), Dim::rel(1));
    q.max_w = Dim::rel(0.25);
    expect_rect("max in rel units", resolve(q, scr), {0, 0, 20, 24});
  }
  expect_rect("parent offset is added", resolve(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), {10, 5, 60, 10}), {40, 5, 30, 10});
  expect_rect("zero parent: rel → empty", resolve(P(A0, A0, Dim::rel(1), Dim::rel(1)), {0, 0, 0, 0}), {0, 0, 0, 0});
  expect_rect("zero parent: abs clamps to empty", resolve(P(A0, A0, Dim::abs(5), Dim::abs(5)), {3, 3, 0, 0}), {3, 3, 0, 0});
  expect_rect("centred popup wider than a small parent clamps to it", resolve(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(30), Dim::abs(30), Anchor::Center), {0, 0, 20, 10}), {0, 0, 20, 10});
  {
    // The help popup as every built-in declares it: mixed units, centred, min/max.
    Placement p = P(Dim::rel(0.5), Dim::rel(0.5), Dim::rel(0.6), Dim::abs(12), Anchor::Center);
    p.min_w = Dim::abs(24);
    p.max_w = Dim::abs(72);
    expect_rect("help popup at 80x24", resolve(p, scr), {16, 6, 48, 12});
    expect_rect("help popup re-placed at 120x40", resolve(p, {0, 0, 120, 40}), {24, 14, 72, 12});
    expect_rect("help popup at 40x12 (min_w, clamp)", resolve(p, {0, 0, 40, 12}), {8, 0, 24, 12});
    expect_rect("help popup at 20x8 (clamped to the parent)", resolve(p, {0, 0, 20, 8}), {0, 0, 20, 8});
  }

  // ---- 1b. the tiling properties at every width ----------------------------------------
  std::printf("-- tiling properties over widths 0..300\n");
  {
    int bad_halves = 0, bad_quarters = 0, bad_fill = 0, bad_floor = 0;
    const double fs[] = {0.1, 1.0 / 3, 0.5, 0.75, 0.9};
    for (int W = 0; W <= 300; ++W) {
      Rect par{0, 0, W, 1};
      Rect a = resolve(P(A0, A0, Dim::rel(0.5), Dim::rel(1)), par);
      Rect b = resolve(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), par);
      if (!(a.x == 0 && b.x == a.w && a.w + b.w == W)) ++bad_halves;
      int pos = 0;
      for (int q = 0; q < 4; ++q) {
        Rect r = resolve(P(Dim::rel(q * 0.25), A0, Dim::rel(0.25), Dim::rel(1)), par);
        if (r.x != pos) ++bad_quarters;
        pos += r.w;
      }
      if (pos != W) ++bad_quarters;
      if (resolve(P(A0, A0, Dim::rel(1), Dim::rel(1)), par).w != W) ++bad_fill;
      for (double f : fs) {
        Rect r = resolve(P(Dim::rel(0.5), A0, Dim::rel(f), Dim::rel(1)), par);
        int fl = static_cast<int>(std::floor(f * W + 1e-6));
        int want_max = std::min(fl + 1, W - r.x);
        if (r.w < std::min(fl, W - r.x) || r.w > want_max) ++bad_floor;
      }
    }
    check(bad_halves == 0, "two rel(0.5) halves tile at every width (" + std::to_string(bad_halves) + " bad)");
    check(bad_quarters == 0, "four rel(0.25) quarters tile at every width (" + std::to_string(bad_quarters) + " bad)");
    check(bad_fill == 0, "rel(1) exactly fills at every width");
    check(bad_floor == 0, "a start-anchored rel width is floor or floor+1, never less (" + std::to_string(bad_floor) + " bad)");
  }

  // ---- 2. text forms -----------------------------------------------------------------------
  std::printf("-- text forms\n");
  check(parse_dim("50%") == Dim::rel(0.5), "parse_dim 50%");
  check(parse_dim("100% - 32") == Dim::rel(1, -32), "parse_dim '100% - 32'");
  check(parse_dim("100%-32") == Dim::rel(1, -32), "parse_dim '100%-32'");
  check(parse_dim("25% + 2") == Dim::rel(0.25, 2), "parse_dim '25% + 2'");
  check(parse_dim(" 0% ") == Dim::rel(0), "parse_dim ' 0% '");
  check(parse_dim("12.5%") == Dim::rel(0.125), "parse_dim 12.5%");
  check(!parse_dim("32"), "parse_dim rejects a bare number (cells are a JSON number, not a string)");
  check(!parse_dim("abc"), "parse_dim rejects 'abc'");
  check(!parse_dim("50% * 2"), "parse_dim rejects an unknown operator");
  check(!parse_dim("%"), "parse_dim rejects '%'");
  check(dim_to_string(Dim::abs(32)) == "32", "dim_to_string abs");
  check(dim_to_string(Dim::rel(0.5)) == "50%", "dim_to_string 50%");
  check(dim_to_string(Dim::rel(1, -32)) == "100% - 32", "dim_to_string '100% - 32'");
  check(dim_to_string(Dim::rel(0.25, 2)) == "25% + 2", "dim_to_string '25% + 2'");
  check(parse_split_size("fill") == SplitSize::filling(1), "parse_split_size fill");
  check(parse_split_size("fill 3") == SplitSize::filling(3), "parse_split_size 'fill 3'");
  check(parse_split_size("40%") == SplitSize::fixed(Dim::rel(0.4)), "parse_split_size 40%");
  check(!parse_split_size("fill 0"), "parse_split_size rejects a zero weight");
  check(!parse_split_size("3"), "parse_split_size rejects a bare number string");
  check(split_size_to_string(SplitSize::filling(2)) == "fill 2" && split_size_to_string(SplitSize::fixed(Dim::abs(3))) == "3",
        "split_size_to_string");
  check(anchor_from_name("bottom-right") == Anchor::BottomRight && anchor_name(Anchor::Center) == "center" && !anchor_from_name("middle"),
        "anchor names");
  check(border_from_name("rounded") == Border::Rounded && border_name(Border::Heavy) == "heavy" && !border_from_name("thick"),
        "border names");

  // ---- 3. the loader -----------------------------------------------------------------------
  std::printf("-- loader\n");
  for (std::string_view name : builtin_layout_names()) {
    const Layout* l = builtin_layout(name);
    check(l != nullptr && l->name == name, "built-in '" + std::string(name) + "' exists");
    if (!l) continue;
    LayoutLoadReport rep;
    std::optional<Layout> back = load_layout(layout_to_json(*l), rep);
    check(back && rep.clean() && *back == *l, "built-in '" + std::string(name) + "' round-trips through layout_to_json");
    check(l->popup("help") != nullptr && l->popup("help")->modal, "built-in '" + std::string(name) + "' declares the modal help popup");
    WindowStack s(*l);
    const Node* f = s.focused();
    check(f && f->id == "input", "built-in '" + std::string(name) + "' focuses input initially");
  }
  check(builtin_layout("nope") == nullptr, "unknown built-in → nullptr");
  {
    LayoutLoadReport rep;
    auto l = load_layout(R"({"name": "x", "colour": 1, "root": {"content": "a", "size": "50", "shade": true, "border": "thick"},
                             "popups": [{"id": "p", "x": "50%", "w": 3.5, "anchor": "middle", "root": {"content": "b"}}]})", rep);
    check(l.has_value(), "a layout with problems still loads");
    auto has = [&](const std::vector<std::string>& v, std::string_view s) {
      for (const std::string& x : v) if (x.find(s) != std::string::npos) return true;
      return false;
    };
    check(has(rep.unknown_keys, "colour") && has(rep.unknown_keys, "root.shade"), "unknown keys named by path (colour, root.shade)");
    check(has(rep.bad_values, "root.size: '50' is not a size"), "a bare-number size string is a bad value with its path");
    check(has(rep.bad_values, "root.border"), "a bad border name is a bad value");
    check(has(rep.bad_values, "popups[0].w: a number is whole cells"), "a fractional cell count is a bad value");
    check(has(rep.bad_values, "popups[0].anchor"), "a bad anchor is a bad value");
    check(l && l->base.root.content == "a" && l->base.root.border == Border::None, "bad fields keep their defaults");
  }
  {
    LayoutLoadReport rep;
    auto l = load_layout(R"({"root": {"row": [{"content": "a"}, {"content": "a"}]}, "focus": "zzz"})", rep);
    bool dup = false, dangling = false;
    for (const std::string& s : rep.bad_values) { if (s.find("duplicate id 'a'") != std::string::npos) dup = true; if (s.find("focus: no window with id 'zzz'") != std::string::npos) dangling = true; }
    check(l && dup, "a duplicate id is reported");
    check(l && dangling, "a focus naming no window is reported");
  }
  {
    LayoutLoadReport rep;
    check(!load_layout("{", rep) && rep.error.find("line") != std::string::npos, "unparseable JSON → nullopt with a line");
    LayoutLoadReport rep2;
    check(!load_layout(R"({"name": "x"})", rep2) && rep2.error.find("root") != std::string::npos, "no root → nullopt, error names it");
    LayoutLoadReport rep3;
    auto l = load_layout(R"({"root": {"content": "a", "row": []}})", rep3);
    check(l && !rep3.bad_values.empty() && rep3.bad_values[0].find("exactly one") != std::string::npos, "a node with both content and row is a bad value");
  }

  // ---- 4. the split ------------------------------------------------------------------------
  std::printf("-- split\n");
  {
    // Fixed + fill: sums to the extent. No borders: no sharing.
    Node root = Node::row({win("a", SplitSize::fixed(Dim::abs(10)), Border::None), win("b", {}, Border::None), win("c", SplitSize::fixed(Dim::rel(0.25)), Border::None)});
    auto v = resolve_tree(root, {0, 0, 81, 5}, {0, 0, 81, 5});
    expect_rect("fixed abs 10", by_id(v, "a")->outer, {0, 0, 10, 5});
    expect_rect("rel 25% of 81 floors to 20 (cumulative edge: 10+25% → 30)", by_id(v, "c")->outer, {61, 0, 20, 5});
    expect_rect("fill takes the remainder", by_id(v, "b")->outer, {10, 0, 51, 5});
  }
  {
    Node root = Node::row({win("a", SplitSize::fixed(Dim::rel(0.5)), Border::None), win("b", SplitSize::fixed(Dim::rel(0.5)), Border::None)});
    int bad = 0;
    for (int W = 0; W <= 200; ++W) {
      auto v = resolve_tree(root, {0, 0, W, 1}, {0, 0, W, 1});
      const ResolvedNode *a = by_id(v, "a"), *b = by_id(v, "b");
      if (!(a->outer.x == 0 && b->outer.x == a->outer.w && a->outer.w + b->outer.w == W)) ++bad;
    }
    check(bad == 0, "50% + 50% in a row tiles at every width 0..200 (" + std::to_string(bad) + " bad)");
  }
  {
    Node root = Node::column({win("a", SplitSize::filling(1), Border::None), win("b", SplitSize::filling(2), Border::None), win("c", SplitSize::fixed(Dim::abs(3)), Border::None)});
    int bad = 0;
    for (int H = 0; H <= 200; ++H) {
      auto v = resolve_tree(root, {0, 0, 10, H}, {0, 0, 10, H});
      const ResolvedNode *a = by_id(v, "a"), *b = by_id(v, "b"), *c = by_id(v, "c");
      int rem = std::max(H - 3, 0);
      if (!(a->outer.h + b->outer.h == rem && a->outer.h == rem / 3 && c->outer.h == std::min(3, H) && c->outer.y == a->outer.h + b->outer.h)) ++bad;
    }
    check(bad == 0, "fill 1 : fill 2 divide the remainder by weight at every height (" + std::to_string(bad) + " bad)");
  }
  {
    // Shared edge: both bordered → overlap by one; the pair spans the extent exactly.
    Node root = Node::row({win("a", SplitSize::fixed(Dim::abs(32))), win("b")});
    auto v = resolve_tree(root, {0, 0, 80, 10}, {0, 0, 80, 10});
    expect_rect("bordered a keeps its 32", by_id(v, "a")->outer, {0, 0, 32, 10});
    expect_rect("bordered b starts on a's right border and reaches the edge", by_id(v, "b")->outer, {31, 0, 49, 10});
    expect_rect("b's inner excludes both borders", by_id(v, "b")->inner, {32, 1, 47, 8});
    Node root2 = Node::row({win("a", SplitSize::fixed(Dim::abs(32)), Border::None), win("b")});
    auto v2 = resolve_tree(root2, {0, 0, 80, 10}, {0, 0, 80, 10});
    expect_rect("one side unbordered: no sharing", by_id(v2, "b")->outer, {32, 0, 48, 10});
  }
  {
    // A column whose children are all bordered is bordered on its side; a mixed one is not.
    Node all = Node::row({Node::column({win("t"), win("i", SplitSize::fixed(Dim::abs(3)))}), win("s", SplitSize::fixed(Dim::abs(32)))});
    auto v = resolve_tree(all, {0, 0, 80, 24}, {0, 0, 80, 24});
    expect_rect("status shares the column's right edge", by_id(v, "s")->outer, {48, 0, 32, 24});
    expect_rect("transcript spans to the shared column", by_id(v, "t")->outer, {0, 0, 49, 22});
    expect_rect("input shares the transcript's bottom edge", by_id(v, "i")->outer, {0, 21, 49, 3});
    Node mixed = Node::row({Node::column({win("t"), win("i", SplitSize::fixed(Dim::abs(3)), Border::None)}), win("s", SplitSize::fixed(Dim::abs(32)))});
    auto v2 = resolve_tree(mixed, {0, 0, 80, 24}, {0, 0, 80, 24});
    expect_rect("a column with an unbordered child does not share", by_id(v2, "s")->outer, {48, 0, 32, 24});
    expect_rect("…so the transcript stops short of it", by_id(v2, "t")->outer, {0, 0, 48, 21});
  }
  {
    Node root = Node::row({win("a", SplitSize::fixed(Dim::abs(10)), Border::None), win("hidden", {}, Border::None), win("b", {}, Border::None)});
    root.children[1].visible = false;
    auto v = resolve_tree(root, {0, 0, 50, 1}, {0, 0, 50, 1});
    check(by_id(v, "hidden") == nullptr, "a hidden node is not resolved");
    expect_rect("…and takes no space", by_id(v, "b")->outer, {10, 0, 40, 1});
  }
  {
    Node root = Node::row({win("a", SplitSize::fixed(Dim::abs(30)), Border::None), win("b", SplitSize::fixed(Dim::abs(30)), Border::None), win("c", SplitSize::fixed(Dim::abs(30)), Border::None)});
    auto v = resolve_tree(root, {0, 0, 50, 1}, {0, 0, 50, 1});
    expect_rect("overflow: second is clipped", by_id(v, "b")->outer, {30, 0, 20, 1});
    expect_rect("overflow: third gets 0", by_id(v, "c")->outer, {50, 0, 0, 1});
    Node root2 = Node::row({win("a", SplitSize::fixed(Dim::abs(10)), Border::None), win("b", SplitSize::fixed(Dim::abs(10)), Border::None)});
    auto v2 = resolve_tree(root2, {0, 0, 50, 1}, {0, 0, 50, 1});
    expect_rect("shortfall with no fill leaves space, no stretch", by_id(v2, "b")->outer, {10, 0, 10, 1});
  }
  {
    Node root = Node::column({win("a"), win("b", SplitSize::fixed(Dim::abs(3)))});
    root.border = Border::Single;
    auto v = resolve_tree(root, {0, 0, 40, 10}, {0, 0, 40, 10});
    expect_rect("a bordered container splits its inner rect", by_id(v, "a")->outer, {1, 1, 38, 6});
    expect_rect("…bottom child shares a's edge", by_id(v, "b")->outer, {1, 6, 38, 3});
    check(v.front().node == &root && v.size() == 3, "tree order: container first, then children");
  }

  // ---- 5. composition ------------------------------------------------------------------------
  std::printf("-- composition\n");
  const Theme& dark = *builtin_theme("default-dark");
  {
    WindowStack s(*builtin_layout("default"));
    Frame f(80, 24, dark.style(Role::background));
    int slots = 0;
    s.compose(f, scr, dark, [&](const ResolvedNode& rn, Frame& fr) { ++slots; fr.put_text(rn.inner.x, rn.inner.y, rn.node->content, dark.style(Role::text), rn.inner.w); });
    check(slots == 3, "three slots rendered (" + std::to_string(slots) + ")");
    check(cell(f, 0, 0) == "┌" && cell(f, 79, 0) == "┐" && cell(f, 0, 23) == "└" && cell(f, 79, 23) == "┘", "outer corners");
    check(cell(f, 48, 0) == "┬", "top junction where status meets transcript is ┬ (" + cell(f, 48, 0) + ")");
    check(cell(f, 48, 21) == "┤", "the input's top edge meets the status column from the left: ┤ (" + cell(f, 48, 21) + ")");
    check(cell(f, 0, 21) == "├" && cell(f, 79, 21) == "│", "input's top edge joins the outer frame at ├; the far edge is a plain │");
    check(cell(f, 48, 23) == "┴", "bottom junction under the status column is ┴ (" + cell(f, 48, 23) + ")");
    check(cell(f, 2, 0) == "t" && cell(f, 1, 0) == " " && cell(f, 12, 0) == " " && cell(f, 13, 0) == "─", "title ' transcript ' sits in the top edge after the corner");
    check(cell(f, 1, 1) == "t" && cell(f, 49, 1) == "s" && cell(f, 1, 22) == "i", "each slot drew at its inner origin");
    check(f.at(60, 10).style.bg == dark.style(Role::panel_background).bg, "the status window is filled with panel_background");
    check(f.at(48, 5).style.bg == dark.style(Role::panel_background).bg && f.at(48, 5).style.fg == dark.style(Role::border).fg,
          "a border takes its colour from `border` and its ground from the window it belongs to");
    check(f.at(0, 22).style.fg == dark.style(Role::border_active).fg && f.at(0, 5).style.fg == dark.style(Role::border).fg,
          "the focused input's border is border_active; the transcript's is border");
  }
  {
    // A popup whose ring crosses the status's left border: no join across layers, and
    // the modal overlay tints only what is beneath.
    WindowStack s(*builtin_layout("default"));
    Layer help = *builtin_layout("default")->popup("help");
    s.push(help);
    Frame f(80, 24, dark.style(Role::background));
    s.compose(f, scr, dark, [&](const ResolvedNode&, Frame&) {});
    auto v = s.resolve(scr);
    const ResolvedNode* h = by_id(v, "help");
    check(h && h->outer == Rect{16, 6, 48, 12}, "the help popup lands where the table says at 80x24");
    check(cell(f, 48, 6) == "─" && cell(f, 48, 17) == "─", "the popup's edge crossing the status border stays ─ (never joins the layer below)");
    check(cell(f, 16, 6) == "╭" && cell(f, 63, 17) == "╯", "rounded corners");
    check(f.at(2, 2).style.dim && f.at(70, 2).style.dim, "cells beneath a modal are tinted with `overlay` (dim in default-dark)");
    check(!f.at(20, 8).style.dim && !f.at(16, 6).style.dim, "cells of the modal itself are not tinted");
    check(cell(f, 18, 6) == "h", "popup title drawn");
    Frame g(120, 40, dark.style(Role::background));
    s.compose(g, {0, 0, 120, 40}, dark, [&](const ResolvedNode&, Frame&) {});
    check(cell(g, 24, 14) == "╭" && cell(g, 95, 25) == "╯", "the same popup re-places itself at 120x40 (24,14)-(95,25)");
  }
  {
    // draw_border on its own, and the degenerate sizes.
    Frame f(10, 3, {});
    draw_border(f, {0, 0, 10, 3}, Border::Double, {}, "ab", {});
    check(cell(f, 0, 0) == "╔" && cell(f, 9, 2) == "╝" && cell(f, 2, 0) == "a" && cell(f, 5, 0) == "═" && cell(f, 0, 1) == "║", "double border with title");
    Frame g(10, 3, {});
    draw_border(g, {0, 0, 1, 3}, Border::Single, {}, "", {});
    check(cell(g, 0, 0) == "╷" && cell(g, 0, 1) == "│" && cell(g, 0, 2) == "╵", "a 1-wide border is a vertical line");
    draw_border(g, {2, 0, 3, 1}, Border::Single, {}, "", {});
    check(cell(g, 2, 0) == "╶" && cell(g, 3, 0) == "─" && cell(g, 4, 0) == "╴", "a 1-high border is a horizontal line");
    draw_border(g, {-2, -1, 5, 3}, Border::Single, {}, "", {});
    check(cell(g, 2, 1) == "┘" && cell(g, 0, 1) == "─", "a border partly off-frame is clipped, not wrapped");
  }

  // ---- 6. the stack ----------------------------------------------------------------------------
  std::printf("-- stack\n");
  {
    WindowStack s(*builtin_layout("default"));
    auto key = [](Key k, bool shift = false) { KeyEvent e; e.key = k; e.shift = shift; return Event{e}; };
    auto ch = [](char32_t c) { KeyEvent e; e.key = Key::Char; e.ch = c; return Event{e}; };
    check(s.focused()->id == "input" && s.focus_layer() == 0, "initial focus is the layout's 'focus'");
    check(s.route(ch('x'), scr) == Route{Route::Kind::Deliver, "input"}, "a key goes to the focused window");
    check(s.route(key(Key::Tab), scr) == Route{Route::Kind::FocusMoved, "transcript"}, "Tab cycles to the transcript");
    check(s.route(key(Key::Tab), scr) == Route{Route::Kind::FocusMoved, "input"}, "Tab wraps back to the input");
    check(s.route(key(Key::Tab, true), scr) == Route{Route::Kind::FocusMoved, "transcript"}, "Shift-Tab cycles backwards");
    check(s.route(key(Key::Escape), scr) == Route{Route::Kind::Deliver, "transcript"}, "Escape with no popup is delivered");
    MouseEvent m;
    m.kind = MouseEvent::Kind::Press;
    m.x = 60; m.y = 5;
    check(s.route(m, scr) == Route{Route::Kind::Deliver, "status"} && s.focused()->id == "transcript",
          "a press on a non-focusable window is delivered and leaves focus alone");
    m.x = 5; m.y = 22;
    check(s.route(m, scr) == Route{Route::Kind::Deliver, "input"} && s.focused()->id == "input", "a press on a focusable base window focuses it");
    MouseEvent wheel;
    wheel.kind = MouseEvent::Kind::WheelUp;
    wheel.x = 5; wheel.y = 5;
    check(s.route(wheel, scr) == Route{Route::Kind::Deliver, "transcript"} && s.focused()->id == "input", "a wheel goes to the window under the pointer, focus unchanged");
    // Pointer capture (milestone 9): a press captures; drags and the release follow it
    // wherever the pointer goes; after the release routing is by position again.
    m.x = 5; m.y = 5;
    check(s.route(m, scr) == Route{Route::Kind::Deliver, "transcript"} && s.captured() == "transcript", "a press captures the pointer for its window");
    MouseEvent drag = m;
    drag.kind = MouseEvent::Kind::Drag;
    drag.x = 60; drag.y = 30;  // over the status panel, and below the screen
    check(s.route(drag, scr) == Route{Route::Kind::Deliver, "transcript"}, "a drag off the window (even off the screen) still goes to the captured window");
    MouseEvent release = drag;
    release.kind = MouseEvent::Kind::Release;
    check(s.route(release, scr) == Route{Route::Kind::Deliver, "transcript"} && s.captured().empty(), "the release goes there too and ends the capture");
    check(s.route(drag, scr) == Route{Route::Kind::Dropped, ""}, "a drag with no capture and no window under it is dropped");
    check(s.focused()->id == "transcript", "the press focused the transcript");
    s.focus("input");

    // A modal popup.
    s.push(*builtin_layout("default")->popup("help"));
    check(s.depth() == 2 && s.has_popup("help") && s.focused()->id == "help" && s.focus_layer() == 1, "a modal focusable popup takes focus");
    check(s.route(key(Key::Tab), scr) == Route{Route::Kind::Deliver, "help"}, "Tab with one focusable window in the layer is delivered");
    m.x = 5; m.y = 22;
    check(s.route(m, scr) == Route{Route::Kind::Dropped, ""}, "a press outside a modal is dropped");
    m.x = 20; m.y = 8;
    check(s.route(m, scr) == Route{Route::Kind::Deliver, "help"}, "a press inside the modal is delivered to it");
    check(s.route(key(Key::Escape), scr) == Route{Route::Kind::ClosedPopup, "help"} && s.depth() == 1, "Escape closes the topmost popup");
    check(s.focused()->id == "input", "focus returns to the base layer's window");
    check(!s.pop(), "pop() on the base alone is false");

    // A non-focusable, non-modal notice above the base.
    Layer notice;
    notice.id = "notice";
    notice.placement = P(Dim::rel(1), Dim::abs(0), Dim::abs(20), Dim::abs(1), Anchor::TopRight);
    notice.root = Node::window("text:hi");
    s.push(notice);
    check(s.focused()->id == "input" && s.focus_layer() == 0, "a non-focusable notice leaves focus with the base");
    check(s.route(ch('x'), scr) == Route{Route::Kind::Deliver, "input"}, "keys still reach the base under a non-modal popup");
    m.x = 60; m.y = 5;
    check(s.route(m, scr) == Route{Route::Kind::Deliver, "status"}, "a press beneath a non-modal popup reaches the base window");
    m.x = 70; m.y = 0;
    check(s.route(m, scr) == Route{Route::Kind::Deliver, "text:hi"}, "a press on the notice hits it");
    check(s.route(key(Key::Escape), scr) == Route{Route::Kind::ClosedPopup, "notice"}, "Escape closes a notice too");

    // A modal layer with no focusable window drops keys rather than leaking them.
    Layer wall;
    wall.id = "wall";
    wall.modal = true;
    wall.root = Node::window("text:wait");
    s.push(wall);
    check(s.focused() == nullptr && s.route(ch('x'), scr) == Route{Route::Kind::Dropped, ""}, "a modal with nothing focusable drops keys");
    s.pop();

    // Hot reload: focus survives when the id does; falls back when it does not.
    s.focus("transcript");
    Layer reloaded = builtin_layout("panel-left")->base;
    reloaded.focus.clear();
    s.set_base(reloaded);
    check(s.focused()->id == "transcript", "set_base keeps the focused id across a reload when it still exists");
    Layer other;
    other.root = Node::column({win("a", {}, Border::None, true), win("b", {}, Border::None, true)});
    s.set_base(other);
    check(s.focused()->id == "a", "…and falls back to the first focusable when it does not");
    Layer named = builtin_layout("default")->base;
    s.set_base(named);
    check(s.focused()->id == "input", "a reloaded layout's own 'focus' wins");
    check(s.find("status") != nullptr && s.find("nope") == nullptr, "find() by id");
    s.find("status")->visible = false;
    auto v = s.resolve(scr);
    check(by_id(v, "status") == nullptr && by_id(v, "transcript")->outer.w == 80, "hiding the status node gives the transcript the width (no-panel by a flag)");
  }
  {
    // A popup layer that is itself a split (a dialog with parts): Tab cycles inside it.
    WindowStack s(*builtin_layout("default"));
    Layer dlg;
    dlg.id = "dlg";
    dlg.modal = true;
    dlg.placement = P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(40), Dim::abs(10), Anchor::Center);
    dlg.root = Node::column({win("text", {}, Border::None, false), win("field", SplitSize::fixed(Dim::abs(1)), Border::None, true), win("buttons", SplitSize::fixed(Dim::abs(1)), Border::None, true)});
    dlg.root.border = Border::Rounded;
    s.push(dlg);
    KeyEvent tab;
    tab.key = Key::Tab;
    check(s.focused()->id == "field", "a dialog focuses its first focusable part");
    check(s.route(tab, scr) == Route{Route::Kind::FocusMoved, "buttons"}, "Tab moves to the next part");
    check(s.route(tab, scr) == Route{Route::Kind::FocusMoved, "field"}, "…and wraps within the dialog, never into the base");
    auto v = s.resolve(scr);
    const ResolvedNode* frame_node = nullptr;
    for (const ResolvedNode& rn : v)
      if (rn.layer == 1 && !frame_node) frame_node = &rn;
    expect_rect("the dialog's frame", frame_node->outer, {20, 7, 40, 10});
    expect_rect("its text part inside the frame", by_id(v, "text")->outer, {21, 8, 38, 6});
    expect_rect("its buttons at the bottom", by_id(v, "buttons")->outer, {21, 15, 38, 1});
  }

  return report("rolltui layout_test");
}
