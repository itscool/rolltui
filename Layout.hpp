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
//     "actions": { "app.help": "open help", "app.menu": "open the menu" },
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
// CONTENT is `kind[:source]` (Phase 10 milestone 2) — a WIDGET KIND from the closed
// table below, and the name of the thing it shows, which the host binds
// (rolltui/Widgets.hpp). Before m2 a content string was a SLOT NAME each host
// resolved in an if-chain, so a layout file could rearrange the windows a host had
// coded and could not introduce a fourth thing; now the layout says what a window
// IS. The table is closed on purpose: an unknown kind is a reported bad value drawn
// as an error panel, never a blank window that looks like a layout mistake.
//
//   transcript:<document>  a Document the host bound by that name
//   input:<target>         the line editor; a submitted line goes to that target
//   menu:<name>            the menu in menus/<name>.json — the user's directory, then
//                          the host's own embedded menus, then the library's shipped
//                          ones (Widgets.hpp has the order). The host binds what the
//                          ids MEAN, never the tree.
//   rows:<source>          label/value rows the host supplies (the status panel,
//                          generalised)
//   text:<literal>         the literal text after the colon (may be empty)
//   file:<path>            the file's text (relative paths resolve against the
//                          preset directory)
//   help                   the key list, rendered from the LIVE bindings; takes no
//                          source
//
// A HOST MAY REGISTER A KIND, and the library then builds it like any other (Phase 11
// milestone 3). `custom:<name>` is GONE — it took a draw function and nothing else, so a
// host's own widget could never receive an event and the host had to route by window id
// (`custom_at`) — which is the wrong shape for any app whose interaction IS that window.
// A registered kind is a FACTORY (Widgets.hpp's `Windows::register_kind`), so
// `canvas:main` behaves exactly like `input:prompt`: created on demand, owned by
// `Windows`, keyed by content, and handed every event the stack routes to its window.
// There is ONE mechanism, not two ways to say "a window this host draws itself".
//
// THE RESOLUTION ORDER, stated so nothing resolves by fallback (the same shape as the
// menu's three rungs and Phase 11 m1's scope split):
//   1. THE LIBRARY'S TABLE, always first and never shadowed. A host that registers a
//      library kind is REFUSED by name, and lookup would not reach it even if it were
//      not — two independent guards, because this is the one place a host could quietly
//      replace the library's own input widget.
//   2. THE HOST'S REGISTERED KINDS, explicit and enumerable (`widget_kind_names()`).
//   3. NEITHER: a named bad value, with the window drawn as an error panel — exactly as
//      an unknown kind has been since Phase 10 m2.
//
// Phase 9's bare slot names ("transcript", "status", "input", …) are MIGRATED once by
// the loader into their kind[:source] form and said so in the report's `migrated`;
// they are not a second spelling that keeps working (a fallback here is exactly the
// implicit resolution order this milestone removes).
//
// ACTIONS ARE DECLARED HERE (Phase 10 milestone 4). `"actions"` is an object of action
// name → what it does: the actions THIS SCREEN emits, which a host looks up by key
// (Bindings.hpp's `app` scope above all). The layout DECLARES them; a bindings file
// SUPPLIES their chords; a menu item may name one. That is what makes "F2 opens the
// menu" a fact of roll's screen rather than of the library, and what lets `help` list
// an action no host has ever compiled in. Rules, each a named bad value:
//   - the name is "<scope>.<verb>", both parts non-empty
//   - the scope is not one of the library's own (Bindings.hpp: library_scope) — those
//     are closed, and a layout may not add to or shadow them
//   - no duplicates; the description is a string
// A file with NO "actions" key at all is a file written before they existed, so the
// loader gives it the SHIPPED DEFAULT's actions and says so in `migrated` — a Phase 9
// layout must not silently lose every app key. An explicit `"actions": {}` is a
// deliberate "none" and is left empty: present-but-empty and absent are different
// answers, which is the only reason the fill-in is safe.
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

// ---- content: the widget kind and its source -----------------------------------------
// The library's table is the closed rung 1 of the header comment's resolution order.
// `Registered` is not a kind a layout can name — it is what `Content::kind` says when
// one spelling of a kind for every purpose that is not the library's own switch.
enum class WidgetKind : std::uint8_t { Transcript, Input, Menu, Rows, Text, File, Help, Registered };

struct Content {
  WidgetKind kind = WidgetKind::Text;
  std::string source;  // the part after the first ':' — a bound name, a literal, a path
  // The host's kind name, and ONLY when `kind` is Registered — empty for every library
  // kind, whose name is a function of the enum. There is still one spelling of a kind:
  // `content_kind_name(c)` is it, and this field is where that function gets its answer
  // in the one case the enum cannot carry. Deliberately LAST so that the two-field
  // `Content{WidgetKind::Rows, "status"}` that every call site already writes keeps
  // meaning what it says — a middle field would have silently made "status" the KIND.
  std::string registered_name;
  bool operator==(const Content&) const = default;
};

std::string_view widget_kind_name(WidgetKind k);  // library kinds only; "" for Registered
// THE one spelling of a content's kind, library or host's.
std::string_view content_kind_name(const Content& c);
std::optional<WidgetKind> widget_kind_from_name(std::string_view name);

// Whether a kind takes a source: every library kind does except `help` (Forbidden), and
// `text`'s literal may be empty (Optional). A registered kind states its own.
enum class SourceRule : std::uint8_t { Required, Optional, Forbidden };
SourceRule source_rule(WidgetKind k);
SourceRule content_source_rule(const Content& c);  // the one accessor, registered kinds included

// ---- rung 2: the kinds a HOST registers ------------------------------------------------
// Registration goes through `Windows::register_kind` (Widgets.hpp), which registers the
// NAME here and the FACTORY there — one call, so a name can never exist without something
// to build it. Refused, with `why` set, when the name is one of the library's (rung 1 is
// never shadowed), when it is empty or contains a ':', or when it is already registered
// with a different rule.
bool register_widget_kind(std::string name, SourceRule rule, std::string source_is, std::string* why = nullptr);
void clear_registered_widget_kinds();  // tests, and a host tearing down
// Every kind name a layout may use right now, in RESOLUTION ORDER: the library's, then
// the registered ones. What the design editor's kind picker offers (Phase 11 m4), and
// what a parse error lists.
std::vector<std::string> widget_kind_names();
// What a kind's source NAMES, in words ("a document the host binds", "a path"): the
// parenthetical in the parse error, and the design editor's hint for the source field.
std::string_view source_describes(WidgetKind k);
// Every kind, in table order — the design editor's kind picker reads the table rather
// than listing the kinds a second time.
const std::vector<WidgetKind>& widget_kinds();

// Why a content string did not parse. `UnknownKind` is separated from the rest because
// it is the one failure a LAYOUT FILE cannot be judged on: rung 2 is the host's, and a
// file is loaded before a host has necessarily registered anything (the library's own
// shipped `default` layout names roll's `approval`, and `builtin_layout()` parses it the
// first time anyone asks). So the loader records every other problem as a bad value and
// leaves this one alone; `Windows` — which is where the registry actually lives — reports
// it by name and draws the error panel, exactly as it already does for a source no host
// bound. WHETHER A KIND EXISTS IS A HOST FACT, and it is answered where host facts are.
enum class ContentProblem : std::uint8_t { None, UnknownKind, MissingSource, ForbiddenSource };
// Parses "kind[:source]". nullopt — with `why` set to the reason, which is what a
// report and the error panel say, and `what` to which kind of problem it was — when the
// kind is in neither rung, a required source is missing, or `help` was given one.
std::optional<Content> parse_content(std::string_view text, std::string* why = nullptr, ContentProblem* what = nullptr);
std::string content_to_string(const Content& c);

// Phase 9's bare slot names, mapped ONCE by the loader (see the header comment).
// nullopt when `legacy` is not one of them.
std::optional<std::string> migrated_content(std::string_view legacy);

struct Node {
  enum class Kind : std::uint8_t { Window, Row, Column };
  Kind kind = Kind::Window;
  std::string id;             // defaults to `content` for windows; optional on splits
  std::string content;        // windows: "kind[:source]" — the widget and what it shows
  Border border = Border::None;
  std::string title;
  bool focusable = false;
  bool visible = true;        // hidden: takes no space in its split, draws nothing
  Role background = Role::background;
  SplitSize size;
  std::vector<Node> children; // Row / Column

  bool is_window() const { return kind == Kind::Window; }
  static Node window(std::string content, SplitSize size = {});
  // A window whose id is not its content — which is every window a host looks up by
  // name now that the content says the widget kind ("input" holding "input:prompt").
  static Node window_id(std::string id, std::string content, SplitSize size = {});
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
  // The actions this screen emits, in file order (the order `help` lists them in). A
  // host hands them to its table with Bindings::declare().
  std::vector<ActionDecl> actions;
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

// Why `name` cannot be declared as an action ("" when it can): the three rules above,
// as ONE function, so the loader and the design editor refuse exactly the same names
// with exactly the same words. Duplicates are the caller's to check — it holds the list.
std::string action_decl_problem(std::string_view name);

// The actions the shipped "default" layout declares, read straight out of that file's
// "actions" object rather than through load_layout — which is what makes it safe for
// load_layout itself to use them for a file that declares none (no recursion), and for
// default_bindings() to declare them (Bindings.hpp).
const std::vector<ActionDecl>& shipped_default_actions();

struct LayoutLoadReport {
  std::string error;                      // non-empty: the file was unusable
  std::vector<std::string> unknown_keys;  // "root.row[1].colour", ...
  std::vector<std::string> bad_values;    // "popups[0].w: '50' is not a dim ..."
  // Phase 9 contents rewritten to their kind[:source] form: "root.column[0].content:
  // 'transcript' → 'transcript:session'", and a file with no "actions" key given the
  // shipped default's. Not a problem — the layout loaded, and the next save writes the
  // new form — so `clean()` ignores it; a host says it once.
  std::vector<std::string> migrated;
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
// The same, plus a bare integer as cells — a size as TYPED (the file says cells with a
// JSON number; typed, it can only be a string). The menu's `size` input type uses it.
std::optional<SplitSize> parse_size_text(std::string_view text);
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
