#pragma once
//
// rolltui/Layout.hpp — windows, layers, placement and focus (plan/phase-9.md,
// requirements 10-12, milestone 8). Two concepts, deliberately no more:
//
//   1. A LAYER is a placed box holding a SPLIT TREE. The tree is Row/Column nodes
//      whose children are windows (a content slot the host fills) or further splits;
//      each child takes a fixed size (abs cells or rel percent) or `fill` (a share of
//      the remainder). This is how the base screen is laid out top-to-bottom and
//      left-to-right at any size, and how a dialog gets its parts.
//   2. A POPUP is a layer above the base, placed by a Placement — {x, y, w, h} each
//      independently abs or rel, an anchor, a clamp — usually modal, usually one
//      window. The base layer's placement is just the whole screen.
//
// Not here, on purpose (plan: "no flexbox/constraint solver"): no grow/shrink pairs,
// no margins or padding (a border is the only spacing), no wrapping, no z on base
// windows (a tree cannot overlap, so z meant nothing; popups stack by push order).
// Sizes are explicit numbers or `fill`, which is what keeps a layout explainable
// when it looks wrong.
//
// Dim — every dimension is `floor(fraction * extent) + cells`: Dim::abs(3) is {0, 3},
// Dim::rel(0.5) is {0.5, 0}, Dim::rel(1, -32) is "everything but 32 cells". The
// rounding rule, stated once and asserted in rolltui/tests/layout_test.cpp:
//   - a relative coordinate is an EDGE, floor(fraction * extent) + cells, with a 1e-6
//     tolerance before the floor so 1/3 + 1/3 + 1/3 fills;
//   - a start-anchored placement runs from the edge of `x` to the edge of `x + w`
//     (Dims added component-wise), so the width absorbs the rounding remainder and
//     a neighbour placed at `x + w` starts exactly where this one ends: two rel(0.5)
//     halves never overlap or gap at any width, rel(1.0) always exactly fills, and a
//     rel width is floor(fraction * extent) + cells or one more — never less;
//   - a centre- or end-anchored placement sizes itself as floor(fraction * extent) +
//     cells and is positioned from the anchor point (centre: point - size / 2 with
//     integer division; end: point - size);
//   - then max, then min (min wins), then clamp: size ≤ extent, start moved into
//     [0, extent - size] — a popup that resolves off-screen at a small size is shown
//     at the edge, not lost. With clamp off the rect may exceed the parent; the
//     compositor clips it.
//
// Split — a Row divides its inner width (a Column its height) among its visible
// children by the same edge rule: fixed children are the edges of their cumulative
// Dim sum, so 50% + 50% fills exactly; fills divide what remains by weight, also as
// cumulative edges, so the parts always sum to the whole. Two adjacent siblings that
// are BOTH bordered on the facing side share that edge (they overlap by one cell and
// their borders join: ┬ ├ ┴ ┤ ┼), so a row of [transcript, status] is one frame, not
// two lines. A container is bordered on a side when it has a border itself, or when
// every child along that side is. Fixed sizes that exceed the extent are clipped in
// order (later children get 0); fixed sizes short of the extent with no fill leave
// empty space at the end, drawn as the screen background — never a stretch nobody
// asked for. Hidden nodes take no space.
//
// Composition — layers draw bottom-up, nodes in tree order; a later cell overwrites.
// A window fills its outer rect with its `background` role, draws its border in
// `border` (`border_active` when focused) on the window's own ground, writes
// " title " into the top edge, then hands its inner rect to the host's slot renderer.
// Borders join through a side map of box-drawing arms kept by compose() per layer —
// never by reading glyphs back from the frame, so a popup's border cannot grow arms
// off a markdown table beneath it, and a new layer never joins the one below. A
// modal layer first applies the theme's `overlay` role (its set colours and
// attribute bits) to every cell of the screen beneath it; a theme that wants nothing
// dimmed gives `overlay` no colour and no attributes. Box-drawing glyphs are East
// Asian AMBIGUOUS width, so with `ambiguous_wide` every border set is drawn in ASCII
// (+ - |) — one cell everywhere, the way vim's `ambiwidth=double` does it — instead
// of a frame that a wide-ambiguous terminal would draw two cells wide.
//
// Focus — the focused window is the focus layer's named focus (a layout's "focus"
// key, or what Tab / a click chose), else that layer's first focusable window in
// tree order. The focus layer is the topmost layer that contains a focusable window,
// except that a modal layer on top confines focus to itself (no focusable window
// there → keys are dropped, never leaked beneath the modal). route(): Escape closes
// the topmost popup layer when there is one, else is delivered; Tab / Shift-Tab
// cycle within the focus layer when it has more than one focusable window, else are
// delivered (an input window may want Tab); other keys go to the focused window; a
// mouse event goes to the topmost visible window under the pointer (dropped under a
// modal when the hit is not in its layer), and a press on a focusable window in the
// focus layer also focuses it. A delivered press CAPTURES the pointer for its window:
// every Drag and the Release that follow go to that window wherever the pointer is —
// off the window, off the screen — so a selection can be dragged past an edge to
// auto-scroll (milestone 9); the release ends the capture.
//
// File format (layouts/<name>.json; the built-ins are written in it and parsed by
// the same loader, so the format is exercised every run):
//   {
//     "name": "default", "min_width": 60, "min_height": 10, "focus": "input",
//     "root": { "row": [
//       { "column": [
//         { "id": "transcript", "content": "transcript", "border": "single",
//           "title": "transcript", "focusable": true },
//         { "id": "input", "content": "input", "size": 3, "border": "single",
//           "focusable": true } ] },
//       { "id": "status", "content": "status", "size": 32, "border": "single",
//         "title": "status", "background": "panel_background" } ] },
//     "popups": [
//       { "id": "help", "x": "50%", "y": "50%", "w": "60%", "h": 12,
//         "anchor": "center", "modal": true,
//         "root": { "content": "help", "border": "rounded", "title": "help",
//                   "focusable": true } } ]
//   }
// A node is a window (has "content") or a split (has "row" or "column": an array of
// nodes). "size" is a JSON integer (cells), "N%" with an optional "± cells", "fill"
// or "fill N" (weight); absent → fill 1. A dim in a popup's placement is an integer
// or a percent string ("50%", "100% - 32", "25%+2"). Window keys: id (defaults to
// content), content, border (none | single | rounded | double | heavy), title,
// focusable, visible, background (a role name). Popup keys: id, x, y, w, h, anchor
// (top-left | top | top-right | left | center | right | bottom-left | bottom |
// bottom-right), clamp, min_w, min_h, max_w, max_h, modal, focus, root. Unknown
// keys are reported, not ignored; a duplicate id is a bad value — the theme loader's
// standard.
//
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Json.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/Theme.hpp"

namespace rolltui {

// ---- placement (popups) --------------------------------------------------------------

struct Dim {
  double fraction = 0;  // of the parent's extent on this axis
  int cells = 0;        // added after the fraction is floored
  static constexpr Dim abs(int cells) { return {0, cells}; }
  static constexpr Dim rel(double fraction, int cells = 0) { return {fraction, cells}; }
  constexpr Dim operator+(const Dim& o) const { return {fraction + o.fraction, cells + o.cells}; }
  constexpr bool operator==(const Dim&) const = default;
};

// floor(fraction * extent + 1e-6) + cells.
int resolve_dim(Dim d, int extent);

enum class Anchor : std::uint8_t {
  TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight
};

struct Placement {
  Dim x = Dim::abs(0), y = Dim::abs(0), w = Dim::rel(1), h = Dim::rel(1);
  Anchor anchor = Anchor::TopLeft;
  bool clamp = true;
  std::optional<Dim> min_w, min_h, max_w, max_h;
  bool operator==(const Placement&) const = default;
};

// The rule in the header comment; absolute coordinates (parent.x/y added).
Rect resolve(const Placement& p, Rect parent);

// ---- the split tree ------------------------------------------------------------------

enum class Border : std::uint8_t { None, Single, Rounded, Double, Heavy };

// How much of the parent split's axis a node takes.
struct SplitSize {
  bool fill = true;   // a share of the remainder, by `weight`
  int weight = 1;
  Dim dim;            // when !fill
  static constexpr SplitSize fixed(Dim d) { return {false, 1, d}; }
  static constexpr SplitSize filling(int weight = 1) { return {true, weight, {}}; }
  constexpr bool operator==(const SplitSize&) const = default;
};

struct Node {
  enum class Kind : std::uint8_t { Window, Row, Column };
  Kind kind = Kind::Window;
  std::string id;             // defaults to `content` for windows; optional on splits
  std::string content;        // windows: the slot the host fills ("transcript", "menu:settings", ...)
  Border border = Border::None;
  std::string title;
  bool focusable = false;
  bool visible = true;        // hidden: takes no space in its split, draws nothing
  Role background = Role::background;
  SplitSize size;
  std::vector<Node> children; // Row / Column

  bool is_window() const { return kind == Kind::Window; }
  static Node window(std::string content, SplitSize size = {});
  static Node row(std::vector<Node> children, SplitSize size = {});
  static Node column(std::vector<Node> children, SplitSize size = {});
  bool operator==(const Node&) const = default;
};

struct Layer {
  std::string id;             // a popup's name in the layout file; "" for the base
  Placement placement;        // where on the screen; the base fills it
  Node root;
  bool modal = false;
  std::string focus;          // the focused window id; "" → first focusable in tree order
  bool operator==(const Layer&) const = default;
};

struct Layout {
  std::string name;
  int min_width = 0, min_height = 0;  // the smallest screen it is designed for; a host may switch below it
  Layer base;
  std::vector<Layer> popups;          // declared placements the host pushes by id
  const Layer* popup(std::string_view id) const;
  bool operator==(const Layout&) const = default;
};

// The inner rect once the border is taken off (a None border takes nothing).
Rect inner_rect(Rect outer, Border b);

// Built-ins, compiled in as layout JSON and parsed once: "default" (panel right),
// "panel-left", "no-panel", "stacked" (the bottom-strip look, the narrow-terminal
// fallback). Every one declares a "help" popup. Unknown name → nullptr.
const Layout* builtin_layout(std::string_view name);
std::vector<std::string_view> builtin_layout_names();

struct LayoutLoadReport {
  std::string error;                      // non-empty: the file was unusable
  std::vector<std::string> unknown_keys;  // "root.row[1].colour", ...
  std::vector<std::string> bad_values;    // "popups[0].w: '50' is not a dim ..."
  bool clean() const { return error.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// Parses a layout file. nullopt only when the JSON is unusable or there is no "root";
// everything else loads with the problems reported (a bad field keeps its default).
std::optional<Layout> load_layout(std::string_view json_text, LayoutLoadReport& report);
// The same over an already-parsed object (a preset file embeds a layout object —
// Presets.hpp).
std::optional<Layout> load_layout(const json::Value& root, LayoutLoadReport& report);
// The layout as a file in the format above; round-trips exactly.
std::string layout_to_json(const Layout& layout);
json::Value layout_to_json_value(const Layout& layout);

// Text forms, exposed for the loader's tests and for config values.
std::optional<Dim> parse_dim(std::string_view text);  // "50%" | "100% - 32" | "25%+2"; NOT "32"
std::string dim_to_string(Dim d);                     // "32" | "50%" | "100% - 32"
std::optional<SplitSize> parse_split_size(std::string_view text);  // "fill" | "fill 2" | a dim string
std::string split_size_to_string(SplitSize s);
std::string_view anchor_name(Anchor a);
std::optional<Anchor> anchor_from_name(std::string_view name);
std::string_view border_name(Border b);
std::optional<Border> border_from_name(std::string_view name);

// ---- composition -----------------------------------------------------------------------

struct ResolvedNode {
  const Node* node = nullptr;
  Rect outer;   // the node's box before clipping to the frame
  Rect inner;   // outer minus the border, clipped to the screen — what a split divides / a slot draws in
  bool focused = false;
  std::size_t layer = 0;
};

// Lays out one tree inside `box` (a layer's resolved placement), in tree order
// (a container precedes its children). Hidden nodes are omitted.
std::vector<ResolvedNode> resolve_tree(const Node& root, Rect box, Rect screen, std::size_t layer = 0);

// Draws one border (no joining) — for widgets that box their own content.
void draw_border(Frame& frame, Rect outer, Border b, const Style& line, std::string_view title,
                 const Style& title_style, bool ambiguous_wide = false);

// The host fills a window's content slot into `rn.inner` (already clipped).
using SlotRenderer = std::function<void(const ResolvedNode& rn, Frame& frame)>;

// Draws the nodes of one layer, in the order given.
void compose_layer(Frame& frame, const std::vector<ResolvedNode>& nodes, const Theme& theme,
                   const SlotRenderer& render, bool ambiguous_wide = false);

// ---- the stack -------------------------------------------------------------------------

struct Route {
  enum class Kind { Deliver, ClosedPopup, FocusMoved, Dropped };
  Kind kind = Kind::Dropped;
  std::string window;  // Deliver: the target; FocusMoved: the newly focused; ClosedPopup: the closed layer's id
  bool operator==(const Route&) const = default;
};

class WindowStack {
 public:
  WindowStack() = default;
  explicit WindowStack(const Layout& layout);  // the base layer; popups are pushed by the host

  // Replaces the base layer (a hot-reloaded layout file). Popup layers stay; the
  // base's focus id is kept when a window with that id still exists.
  void set_base(const Layer& base);
  const Layer& base() const { return layers_.front(); }
  Layer& base() { return layers_.front(); }

  void push(Layer popup);
  bool pop();                 // closes the topmost popup; false when only the base remains
  bool has_popup(std::string_view id) const;
  std::size_t depth() const { return layers_.size(); }
  const std::vector<Layer>& layers() const { return layers_; }

  Node* find(std::string_view id);  // any layer, any node; nullptr when absent
  const Node* find(std::string_view id) const;

  // Every layer resolved against `screen`, in draw order, with `focused` set on the
  // one focused window; also draws them when `frame` is given.
  std::vector<ResolvedNode> resolve(Rect screen) const;
  void compose(Frame& frame, Rect screen, const Theme& theme, const SlotRenderer& render,
               bool ambiguous_wide = false) const;

  const Node* focused() const;
  std::size_t focus_layer() const;  // index into layers()
  void focus(std::string_view id);  // no-op unless id names a focusable visible window in the focus layer
  void cycle_focus(bool backwards = false);

  // Escape and Tab are the stack's by data too (milestone 17): stack.close_popup,
  // stack.focus_next, stack.focus_prev in the Bindings' stack scope.
  Route route(const Event& e, Rect screen, const Bindings& bindings);
  Route route(const Event& e, Rect screen) { return route(e, screen, default_bindings()); }
  // The window a press captured the pointer for, until its release ("" when none).
  const std::string& captured() const { return captured_; }

 private:
  std::vector<Layer> layers_{Layer{}};
  std::string captured_;
};

}  // namespace rolltui
