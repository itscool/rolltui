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
//   7. Phase 10 m2 — CONTENT AND WIDGETS: the kind table and every way a content
//      string can be wrong, said by name; the Phase 9 slot names migrated once by the
//      loader; rolltui::Windows instantiating every kind, drawing each of them, and
//      turning every failure (unknown kind, unbound source, unreadable file) into a
//      named report entry AND a visible error panel; the input sizing its own window;
//      one widget per content, kept across a layout reload.
//   8. Phase 10 m3 — MENUS FROM FILES: the three rungs in order (the user's directory,
//      the host's own embedded menus, the library's shipped ones) with the origin said;
//      a file dropped in after the fact opens with no rebuild and a changed one is
//      re-read; a missing menu and an unparsable one are named bad values drawn in the
//      window; an unknown key is reported without stopping the menu drawing.
//
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <dirent.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

// PHASE 17 m2 (test-porting), re-verified against the CURRENT library rather than trusting
// the previous pass's comment here (both `load_layout`/`layout_to_json`/`builtin_layout*` and
// Windows' bind_*/set_dir/add_menu/menu_origin/menu_names/prepare had since gained full C
// forms — `rolltui_load_layout_text`, `rolltui_windows_bind_document`, etc. — that the old
// comment called blocked; `Layout.cpp`/`Widgets.cpp` are now thin shims over exactly those
// calls). Every one of those is called directly below through local `_c` helpers that mirror
// the shim's own body verbatim, the same pattern `place()`/`resolve_tree_c()` already used.
//
// What GENUINELY has no C form, verified by reading the boundary headers rather than the
// prior comment, and stays on the two headers below:
//   - `rolltui::Layout`/`ActionDecl`'s OWN shape (`shipped_default_actions()`, `action_decls()`,
//     the `RolltuiActionList`-vs-`vector<ActionDecl>` comparison) — `rolltui_layout.h`'s own
//     header comment states these keep real std::string/vector fields for callers outside
//     this port's scope (the same exception `rolltui_json.h`'s `Value` took), so Layout.hpp
//     stays included for exactly this bridge.
//   - `Windows`' TYPED accessors — `transcript(source)`/`input(source)`/`menu(source)` hand
//     back a LIVE `Transcript&`/`Input&`/`Menu&` backed by maps `Windows` itself still owns
//     (`Widgets.hpp`'s own header comment: "Phase 17... `Windows` OWNS every `input:<source>`
//     here now"). `rolltui_windows_at`/`_widget_for` hand back a generic `RolltuiWidget{vt,
//     ctx}` with an OPAQUE `ctx` — there is no accessor that recovers a typed `RolltuiInput*`
//     from it, so a test (or a host) that needs to drive a specific widget's own operations
//     (`set_text`, `scroll_to_top`, a menu's `find`/`set_options`) has no raw-C path and keeps
//     `Windows` for it. Everything Windows/WindowStack forward with NO state of their own
//     (routing, composing, resolving, the widget-kind registry, content parsing) is called
//     through `rolltui_window_stack_*`/`rolltui_widget_kind_*`/`rolltui_content_*` directly
//     instead, including inside the sections that also use `Windows` for its typed half.
#include "rolltui/Layout.hpp"
#include "rolltui/Widgets.hpp"
#include "rolltui/c/rolltui_embedded.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_widgets.h"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

// The input's text as a view. `Windows::input()` hands back the library's own handle now
// (Phase 17 m1c), and the C's text accessor is pointer+length like every other borrow here.
std::string_view input_text(const RolltuiInput* in) {
  std::size_t n = 0;
  const char* p = rolltui_input_text(in, &n);
  return std::string_view(p, n);
}
void set_input_text(RolltuiInput* in, std::string_view t) { rolltui_input_set_text(in, t.data(), t.size()); }
bool transcript_selection_active(const RolltuiTranscript* t) {
  RolltuiSelection sel;
  rolltui_transcript_selection(t, &sel);
  return sel.active != 0;
}

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

std::string cell(const Frame& f, int x, int y) { return std::string(f.glyph(x, y)); }

// ---- direct C calls, replacing the rolltui::-namespaced free functions below (Phase 17 m2) ----
// Each mirrors the C++ shim's own body exactly (rolltui/Layout.hpp's inline definitions and
// rolltui/Layout.cpp) — the shim IS the mapping. Named with a `_c`/`Node::`-free spelling so
// they do not collide with the still-included rolltui:: originals used elsewhere in this file.

Rect place(const Placement& p, Rect parent) {
  RolltuiRect r;
  rolltui_placement_resolve(&p, parent, &r);
  return r;
}

std::optional<Dim> parse_dim_c(std::string_view text) {
  Dim d;
  if (!rolltui_parse_dim(text.data(), text.size(), &d)) return std::nullopt;
  return d;
}

std::string dim_to_string_c(Dim d) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_dim_to_string(d, buf, sizeof buf));
}

std::optional<SplitSize> parse_split_size_c(std::string_view text) {
  SplitSize s;
  if (!rolltui_parse_split_size(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

std::string split_size_to_string_c(SplitSize s) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_split_size_to_string(s, buf, sizeof buf));
}

void push_node_c(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<ResolvedNode>*>(ctx)->push_back(*rn);
}

std::vector<ResolvedNode> resolve_tree_c(const Node& root, Rect box, Rect screen, std::size_t layer = 0) {
  std::vector<ResolvedNode> out;
  rolltui_resolve_tree(&root, box, screen, layer, push_node_c, &out);
  return out;
}

std::string cell_c(const RolltuiFrame* f, int x, int y) {
  std::size_t n = 0;
  const char* p = rolltui_frame_glyph(f, x, y, &n);
  return std::string(p, n);
}

// Node::row/column/window/window_id are LayoutTree.cpp statics (one of "the 3 C++ data
// edges" Phase 17 m2 deletes); these build the same shapes through the plain C entry points
// (rolltui_layout_tree.h) instead: rolltui_layout_node_init zeroes/defaults exactly as the
// struct's own defaults would, rolltui_node_list_add is the list's emplace_back, and
// rolltui_layout_node_copy fills the slot it returns.
Node win(const char* content, SplitSize size = {}, Border b = Border::Single, bool focusable = false) {
  Node n;
  rolltui_layout_node_init(&n);
  n.kind = Node::Kind::Window;
  n.id = content;
  n.content = content;
  n.size = size;
  n.border = b;
  n.focusable = focusable;
  return n;
}

Node row_of(std::vector<Node> children, SplitSize size = {}) {
  Node n;
  rolltui_layout_node_init(&n);
  n.kind = Node::Kind::Row;
  n.size = size;
  for (const Node& c : children) rolltui_layout_node_copy(rolltui_node_list_add(&n.children), &c);
  return n;
}

Node column_of(std::vector<Node> children, SplitSize size = {}) {
  Node n = row_of(std::move(children), size);
  n.kind = Node::Kind::Column;
  return n;
}

// ---- direct C calls, continued: anchor/border names -----------------------------------
std::string_view anchor_name_c(Anchor a) {
  std::size_t n = 0;
  const char* p = rolltui_anchor_name(static_cast<unsigned char>(a), &n);
  return {p, n};
}
std::optional<Anchor> anchor_from_name_c(std::string_view name) {
  unsigned char out = 0;
  if (rolltui_anchor_from_name(name.data(), name.size(), &out)) return static_cast<Anchor>(out);
  return std::nullopt;
}
std::string_view border_name_c(Border b) {
  std::size_t n = 0;
  const char* p = rolltui_border_name(static_cast<unsigned char>(b), &n);
  return {p, n};
}
std::optional<Border> border_from_name_c(std::string_view name) {
  unsigned char out = 0;
  if (rolltui_border_from_name(name.data(), name.size(), &out)) return static_cast<Border>(out);
  return std::nullopt;
}

// ---- direct C calls, continued: content and the widget-kind registry ------------------
// Each mirrors Layout.cpp's own body exactly, same as parse_dim_c and friends above.
std::string_view widget_kind_name_c(WidgetKind k) {
  std::size_t n = 0;
  const char* p = rolltui_widget_kind_library_name(static_cast<std::size_t>(k), &n);
  return {p, n};
}
std::optional<WidgetKind> widget_kind_from_name_c(std::string_view name) {
  unsigned char ordinal = 0, rule = 0;
  if (rolltui_widget_kind_resolve(name.data(), name.size(), &ordinal, &rule, nullptr, nullptr) == ROLLTUI_KIND_LIBRARY)
    return static_cast<WidgetKind>(ordinal);
  return std::nullopt;
}
std::optional<Content> parse_content_c(std::string_view text, std::string* why = nullptr, ContentProblem* what = nullptr) {
  unsigned char ordinal = 0, problem = ROLLTUI_CONTENT_PROBLEM_NONE;
  int is_host = 0;
  const char *name = nullptr, *source = nullptr;
  std::size_t name_len = 0, source_len = 0;
  Str why_str;
  const int ok = rolltui_content_parse(text.data(), text.size(), &ordinal, &is_host, &name, &name_len, &source,
                                       &source_len, &problem, &why_str);
  if (what) *what = static_cast<ContentProblem>(problem);
  if (why) *why = why_str.str();
  if (!ok) return std::nullopt;
  Content c;
  c.source.assign(std::string_view(source, source_len));
  if (is_host) {
    c.kind = WidgetKind::Registered;
    c.registered_name.assign(std::string_view(name, name_len));
  } else {
    c.kind = static_cast<WidgetKind>(ordinal);
  }
  return c;
}
std::string content_to_string_c(const Content& c) {
  unsigned char ordinal = 0, rule = 0;
  std::string_view kind_name;
  if (c.kind == WidgetKind::Registered) {
    kind_name = c.registered_name;
    rolltui_widget_kind_resolve(c.registered_name.data(), c.registered_name.size(), &ordinal, &rule, nullptr, nullptr);
  } else {
    kind_name = widget_kind_name_c(c.kind);
    rule = static_cast<unsigned char>(source_rule(c.kind));
  }
  Str out;
  rolltui_content_format(kind_name.data(), kind_name.size(), c.source.data(), c.source.size(), rule, &out);
  return out.str();
}
bool register_widget_kind_c(std::string_view name, SourceRule rule, std::string_view source_is, std::string* why = nullptr) {
  const int refusal = rolltui_widget_kind_register(name.data(), name.size(), static_cast<unsigned char>(rule),
                                                   source_is.data(), source_is.size());
  if (why) {
    switch (refusal) {
      case ROLLTUI_REGISTER_EMPTY: *why = "a kind name must not be empty"; break;
      case ROLLTUI_REGISTER_HAS_COLON: *why = "a kind name must not contain ':'"; break;
      case ROLLTUI_REGISTER_IS_LIBRARY: *why = "'" + std::string(name) + "' is one of the library's own kinds"; break;
      case ROLLTUI_REGISTER_RULE_DIFFERS: *why = "'" + std::string(name) + "' is already registered with a different rule"; break;
      default: why->clear(); break;
    }
  }
  return refusal == ROLLTUI_REGISTER_OK;
}
void clear_registered_widget_kinds_c() { rolltui_widget_kind_clear(); }
std::optional<std::string> migrated_content_c(std::string_view legacy) {
  std::size_t n = 0;
  const char* p = rolltui_migrated_content(legacy.data(), legacy.size(), &n);
  if (!p) return std::nullopt;
  return std::string(p, n);
}
std::vector<std::string> widget_kind_names_c() {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) out.emplace_back(widget_kind_name_c(static_cast<WidgetKind>(i)));
  for (std::size_t i = 0; i < rolltui_widget_kind_host_count(); ++i) {
    std::size_t n = 0;
    const char* p = rolltui_widget_kind_host_name(i, &n);
    out.emplace_back(p, n);
  }
  return out;
}

// ---- direct C calls, continued: the loader (load_layout/layout_to_json/builtin_layout*) ----
// `LayoutLoadReport`/`Layout`/`ActionDecl` stay rolltui::-side (see the include comment); only
// the ALGORITHM these wrap moves. Every function here mirrors Layout.cpp's own body verbatim
// — `report_from_c_c`/`actions_to_c_c` are that file's private `report_from_c`/`actions_to_c`,
// copied because they are not exported. (`kHooks_c` and `loaded_to_layout_c` were too, and
// both are gone: see below and `rolltui_loaded_layout_to_layout`. The justification that used
// to sit here — "the shim's OWN plumbing is not part of its public surface either" — was true
// while the shim existed and is exactly what stops being true when the shim is deleted.)
// PHASE 17 m2a: `kHooks_c` is gone. It was a VERBATIM copy of `Layout.cpp`'s private
// `kHooks`, justified in the comment above as "the shim's OWN plumbing is not part of its
// public surface either" — true while the shim existed, and wrong once the shim is the thing
// being deleted. The three questions it answered (role from name, role name, is this scope the
// library's) are all C now, so `rolltui_layout_default_hooks()` answers them and neither this
// file nor any host in m3 needs a table.

void report_from_c_c(const RolltuiLayoutReport& r, LayoutLoadReport& report) {
  report.error = r.error.str();
  for (std::size_t i = 0; i < r.unknown_keys_n; ++i) report.unknown_keys.emplace_back(r.unknown_keys[i].str());
  for (std::size_t i = 0; i < r.bad_values_n; ++i) report.bad_values.emplace_back(r.bad_values[i].str());
  for (std::size_t i = 0; i < r.migrated_n; ++i) report.migrated.emplace_back(r.migrated[i].str());
}
std::vector<RolltuiLayoutAction> actions_to_c_c(const std::vector<ActionDecl>& actions) {
  std::vector<RolltuiLayoutAction> out;
  out.reserve(actions.size());
  for (const ActionDecl& d : actions) out.push_back(RolltuiLayoutAction{Str(d.name), Str(d.description)});
  return out;
}
// PHASE 17 m2a: the FOURTH copy of this conversion, and the last. `Layout.cpp` had one,
// `rolltui_presets.c` had a second, an agent converting `layout_editor.cpp` needed a third —
// and this one, element by element instead of by field-adopt, was the one nobody had counted.
// `rolltui_loaded_layout_to_layout` MOVES rather than copies, which is what the caller wanted
// every time: the loaded carrier is released immediately after.
Layout loaded_to_layout_c(RolltuiLoadedLayout& loaded) {
  Layout out;
  rolltui_loaded_layout_to_layout(&loaded, &out);
  return out;
}
std::optional<Layout> load_layout_c(std::string_view json_text, LayoutLoadReport& report) {
  const std::vector<RolltuiLayoutAction> default_actions = actions_to_c_c(shipped_default_actions());
  RolltuiLoadedLayout loaded;
  RolltuiLayoutReport rep{};
  rolltui_loaded_layout_init(&loaded);
  const int ok = rolltui_load_layout_text(json_text.data(), json_text.size(), &loaded, default_actions.data(),
                                          default_actions.size(), rolltui_layout_default_hooks(), &rep);
  report_from_c_c(rep, report);
  rolltui_layout_report_release(&rep);
  if (!ok) {
    rolltui_loaded_layout_release(&loaded);
    return std::nullopt;
  }
  std::optional<Layout> out = loaded_to_layout_c(loaded);
  rolltui_loaded_layout_release(&loaded);
  return out;
}
std::string layout_to_json_c(const Layout& layout) {
  Str out;
  rolltui_layout_to_json_text(layout.name.data(), layout.name.size(), layout.min_width, layout.min_height,
                              layout.actions.data(), layout.actions.size(), &layout.base, layout.popups.data(),
                              layout.popups.size(), rolltui_layout_default_hooks(), &out);
  return out.str();
}
// The built-ins: the embedded table directly, parsed with load_layout_c. A fresh parse per
// lookup (this cache is the test's own, not Layout.cpp's `builtin_layout_cache()` — nothing
// exported reaches that one) rather than re-deriving its lazy-init subtleties for a path that
// only ever runs a handful of times per test binary.
const Layout* builtin_layout_c(std::string_view name) {
  static const std::vector<std::pair<std::string, Layout>> cache = [] {
    std::vector<std::pair<std::string, Layout>> out;
    for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i) {
      const RolltuiEmbeddedFile& f = rolltui_kLayoutPresets[i];
      LayoutLoadReport rep;
      std::optional<Layout> l = load_layout_c(f.text, rep);
      if (l) out.emplace_back(std::string(f.name), std::move(*l));
    }
    return out;
  }();
  for (const auto& [n, l] : cache)
    if (n == name) return &l;
  return nullptr;
}
std::vector<std::string_view> builtin_layout_names_c() {
  std::vector<std::string_view> out;
  for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i) out.push_back(rolltui_kLayoutPresets[i].name);
  return out;
}

// ---- direct C calls, continued: the stack, used on its own (sections 3/5/6) -----------
// `rolltui::WindowStack` stays in scope for sections 8/9, where `Windows::prepare()` etc.
// take a `WindowStack&` by their own signature (see the include comment) — but nothing
// stops those same sections calling these `_c` functions too where it is convenient; the
// choice below is per call site, not per section.
struct StackC {
  RolltuiWindowStack* s = rolltui_window_stack_new();
  StackC() = default;
  explicit StackC(const Layout& l) { rolltui_window_stack_set_base(s, &l.base); }
  StackC(const StackC&) = delete;
  StackC& operator=(const StackC&) = delete;
  ~StackC() { rolltui_window_stack_free(s); }
};

RolltuiEvent key_ev(Key k, bool shift = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = static_cast<unsigned char>(k);
  e.key.shift = shift;
  return e;
}
RolltuiEvent chr_ev(char32_t c) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = ROLLTUI_KEY_CHAR;
  e.key.ch = c;
  return e;
}
RolltuiEvent mouse_ev(const MouseEvent& m) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse = m;
  return e;
}

// PHASE 17 m3: the LIBRARY's three, not a copy. This file had hand-written them, which made
// two consumers with the same table (`Layout.cpp` was the other) — rule 5's tell, fired before
// a single host had been converted.
const RolltuiStackActions& kStackActions_c = *rolltui_stack_default_actions();

Route route_c(RolltuiWindowStack* s, const RolltuiEvent& e, Rect screen) {
  Str window;
  const unsigned char kind = rolltui_window_stack_route(s, &e, screen, default_bindings().handle(), &kStackActions_c, &window);
  return {static_cast<Route::Kind>(kind), window.str()};
}
std::string_view captured_c(const RolltuiWindowStack* s) {
  std::size_t n = 0;
  const char* p = rolltui_window_stack_captured(s, &n);
  return {p, n};
}
const RolltuiLayoutRoles& kRoles_c = *rolltui_layout_default_roles();  // likewise (m3)
using SlotRendererC = std::function<void(const ResolvedNode&, Frame&)>;
void call_slot_c(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame*) {
  auto* p = static_cast<std::pair<const SlotRendererC*, Frame*>*>(ctx);
  (*p->first)(*rn, *p->second);
}
void compose_c(RolltuiWindowStack* s, Frame& frame, Rect screen, const Theme& theme, const SlotRendererC& render) {
  std::pair<const SlotRendererC*, Frame*> ctx{&render, &frame};
  RolltuiComposeScratch* scratch = rolltui_compose_scratch_new();
  rolltui_window_stack_compose(s, frame.handle(), screen, theme.styles.data(), &kRoles_c, render ? call_slot_c : nullptr,
                               &ctx, false, scratch);
  rolltui_compose_scratch_free(scratch);
}
std::vector<ResolvedNode> resolve_stack_c(const RolltuiWindowStack* s, Rect screen) {
  std::vector<ResolvedNode> out;
  rolltui_window_stack_resolve(s, screen, push_node_c, &out);
  return out;
}
// The handful of stack methods keyed by a string id, spelled out once so a call site reads
// `focus_c(s.s, "input")` rather than repeating `.data(), .size()` at every one.
void focus_c(RolltuiWindowStack* s, std::string_view id) { rolltui_window_stack_focus(s, id.data(), id.size()); }
bool has_popup_c(const RolltuiWindowStack* s, std::string_view id) {
  return rolltui_window_stack_has_popup(s, id.data(), id.size()) != 0;
}
Node* find_c(RolltuiWindowStack* s, std::string_view id) { return rolltui_window_stack_find(s, id.data(), id.size()); }

}  // namespace

int main() {
  const Rect scr{0, 0, 80, 24};
  const Dim A0 = Dim::abs(0);

  // ---- 1. resolve(): the table -----------------------------------------------------------
  std::printf("-- resolve table\n");
  expect_rect("all absolute", place(P(Dim::abs(2), Dim::abs(3), Dim::abs(10), Dim::abs(5)), scr), {2, 3, 10, 5});
  expect_rect("rel(1) fills 81x25", place(P(A0, A0, Dim::rel(1), Dim::rel(1)), {0, 0, 81, 25}), {0, 0, 81, 25});
  expect_rect("rel(1) fills 1x1", place(P(A0, A0, Dim::rel(1), Dim::rel(1)), {0, 0, 1, 1}), {0, 0, 1, 1});
  expect_rect("rel(0.5) width floors at 81", place(P(A0, A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 81, 25}), {0, 0, 40, 25});
  expect_rect("second half starts at 40 and takes the remainder at 81",
              place(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 81, 25}), {40, 0, 41, 25});
  expect_rect("halves at width 7: first", place(P(A0, A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 7, 1}), {0, 0, 3, 1});
  expect_rect("halves at width 7: second", place(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), {0, 0, 7, 1}), {3, 0, 4, 1});
  expect_rect("thirds at 10: first", place(P(A0, A0, Dim::rel(1.0 / 3), Dim::rel(1)), {0, 0, 10, 1}), {0, 0, 3, 1});
  expect_rect("thirds at 10: second", place(P(Dim::rel(1.0 / 3), A0, Dim::rel(1.0 / 3), Dim::rel(1)), {0, 0, 10, 1}), {3, 0, 3, 1});
  expect_rect("thirds at 10: third fills to the edge (1e-6 tolerance)",
              place(P(Dim::rel(2.0 / 3), A0, Dim::rel(1.0 / 3), Dim::rel(1)), {0, 0, 10, 1}), {6, 0, 4, 1});
  expect_rect("rel(1, -32): the rest minus the panel", place(P(A0, A0, Dim::rel(1, -32), Dim::rel(1, -3)), scr), {0, 0, 48, 21});
  expect_rect("panel at rel(1, -32) with abs 32", place(P(Dim::rel(1, -32), A0, Dim::abs(32), Dim::rel(1)), scr), {48, 0, 32, 24});
  expect_rect("input at y rel(1, -3) h 3 tiles with h rel(1, -3)", place(P(A0, Dim::rel(1, -3), Dim::rel(1), Dim::abs(3)), scr), {0, 21, 80, 3});
  expect_rect("negative size clamps to 0", place(P(A0, A0, Dim::rel(1, -32), Dim::rel(1)), {0, 0, 20, 5}), {0, 0, 0, 5});
  {
    Placement p = P(A0, A0, Dim::rel(1, -32), Dim::rel(1));
    p.min_w = Dim::abs(10);
    expect_rect("min_w lifts a negative size", place(p, {0, 0, 20, 5}), {0, 0, 10, 5});
  }
  expect_rect("center anchor, even size", place(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(20), Dim::abs(10), Anchor::Center), scr), {30, 7, 20, 10});
  expect_rect("center anchor at 81x25 lands on the same cell", place(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(20), Dim::abs(10), Anchor::Center), {0, 0, 81, 25}), {30, 7, 20, 10});
  expect_rect("center anchor, odd size (integer half)", place(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(21), Dim::abs(11), Anchor::Center), scr), {30, 7, 21, 11});
  expect_rect("bottom-right anchor", place(P(Dim::rel(1), Dim::rel(1), Dim::abs(10), Dim::abs(3), Anchor::BottomRight), scr), {70, 21, 10, 3});
  expect_rect("right anchor with a rel width floors (no edge rule)", place(P(Dim::rel(1), A0, Dim::rel(0.25), Dim::rel(1), Anchor::Right), {0, 0, 81, 1}), {61, 0, 20, 1});
  expect_rect("right anchor at x rel(0.5) w rel(0.5) sizes 40 at 81", place(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1), Anchor::Right), {0, 0, 81, 1}), {0, 0, 40, 1});
  expect_rect("top anchor centres horizontally only", place(P(Dim::rel(0.5), Dim::abs(2), Dim::abs(10), Dim::abs(4), Anchor::Top), scr), {35, 2, 10, 4});
  expect_rect("clamp moves an overflowing x back", place(P(Dim::abs(75), A0, Dim::abs(10), Dim::abs(2)), scr), {70, 0, 10, 2});
  expect_rect("clamp off leaves the overflow", place(P(Dim::abs(75), A0, Dim::abs(10), Dim::abs(2), Anchor::TopLeft, false), scr), {75, 0, 10, 2});
  expect_rect("clamp shrinks an oversize width to the parent", place(P(A0, A0, Dim::abs(100), Dim::abs(2)), scr), {0, 0, 80, 2});
  expect_rect("clamp lifts a negative x", place(P(Dim::abs(-5), Dim::abs(-1), Dim::abs(10), Dim::abs(2)), scr), {0, 0, 10, 2});
  expect_rect("clamp off keeps a negative x", place(P(Dim::abs(-5), A0, Dim::abs(10), Dim::abs(2), Anchor::TopLeft, false), scr), {-5, 0, 10, 2});
  {
    Placement p = P(A0, A0, Dim::rel(0.5), Dim::rel(1));
    p.max_w = Dim::abs(30);
    expect_rect("max_w caps", place(p, scr), {0, 0, 30, 24});
    p.min_w = Dim::abs(50);
    expect_rect("min wins over max", place(p, scr), {0, 0, 50, 24});
    Placement q = P(A0, A0, Dim::rel(0.5), Dim::rel(1));
    q.max_w = Dim::rel(0.25);
    expect_rect("max in rel units", place(q, scr), {0, 0, 20, 24});
  }
  expect_rect("parent offset is added", place(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), {10, 5, 60, 10}), {40, 5, 30, 10});
  expect_rect("zero parent: rel → empty", place(P(A0, A0, Dim::rel(1), Dim::rel(1)), {0, 0, 0, 0}), {0, 0, 0, 0});
  expect_rect("zero parent: abs clamps to empty", place(P(A0, A0, Dim::abs(5), Dim::abs(5)), {3, 3, 0, 0}), {3, 3, 0, 0});
  expect_rect("centred popup wider than a small parent clamps to it", place(P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(30), Dim::abs(30), Anchor::Center), {0, 0, 20, 10}), {0, 0, 20, 10});
  {
    // The help popup as every built-in declares it: mixed units, centred, min/max.
    Placement p = P(Dim::rel(0.5), Dim::rel(0.5), Dim::rel(0.6), Dim::abs(12), Anchor::Center);
    p.min_w = Dim::abs(24);
    p.max_w = Dim::abs(72);
    expect_rect("help popup at 80x24", place(p, scr), {16, 6, 48, 12});
    expect_rect("help popup re-placed at 120x40", place(p, {0, 0, 120, 40}), {24, 14, 72, 12});
    expect_rect("help popup at 40x12 (min_w, clamp)", place(p, {0, 0, 40, 12}), {8, 0, 24, 12});
    expect_rect("help popup at 20x8 (clamped to the parent)", place(p, {0, 0, 20, 8}), {0, 0, 20, 8});
  }

  // ---- 1b. the tiling properties at every width ----------------------------------------
  std::printf("-- tiling properties over widths 0..300\n");
  {
    int bad_halves = 0, bad_quarters = 0, bad_fill = 0, bad_floor = 0;
    const double fs[] = {0.1, 1.0 / 3, 0.5, 0.75, 0.9};
    for (int W = 0; W <= 300; ++W) {
      Rect par{0, 0, W, 1};
      Rect a = place(P(A0, A0, Dim::rel(0.5), Dim::rel(1)), par);
      Rect b = place(P(Dim::rel(0.5), A0, Dim::rel(0.5), Dim::rel(1)), par);
      if (!(a.x == 0 && b.x == a.w && a.w + b.w == W)) ++bad_halves;
      int pos = 0;
      for (int q = 0; q < 4; ++q) {
        Rect r = place(P(Dim::rel(q * 0.25), A0, Dim::rel(0.25), Dim::rel(1)), par);
        if (r.x != pos) ++bad_quarters;
        pos += r.w;
      }
      if (pos != W) ++bad_quarters;
      if (place(P(A0, A0, Dim::rel(1), Dim::rel(1)), par).w != W) ++bad_fill;
      for (double f : fs) {
        Rect r = place(P(Dim::rel(0.5), A0, Dim::rel(f), Dim::rel(1)), par);
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
  check(parse_dim_c("50%") == Dim::rel(0.5), "parse_dim 50%");
  check(parse_dim_c("100% - 32") == Dim::rel(1, -32), "parse_dim '100% - 32'");
  check(parse_dim_c("100%-32") == Dim::rel(1, -32), "parse_dim '100%-32'");
  check(parse_dim_c("25% + 2") == Dim::rel(0.25, 2), "parse_dim '25% + 2'");
  check(parse_dim_c(" 0% ") == Dim::rel(0), "parse_dim ' 0% '");
  check(parse_dim_c("12.5%") == Dim::rel(0.125), "parse_dim 12.5%");
  check(!parse_dim_c("32"), "parse_dim rejects a bare number (cells are a JSON number, not a string)");
  check(!parse_dim_c("abc"), "parse_dim rejects 'abc'");
  check(!parse_dim_c("50% * 2"), "parse_dim rejects an unknown operator");
  check(!parse_dim_c("%"), "parse_dim rejects '%'");
  check(dim_to_string_c(Dim::abs(32)) == "32", "dim_to_string abs");
  check(dim_to_string_c(Dim::rel(0.5)) == "50%", "dim_to_string 50%");
  check(dim_to_string_c(Dim::rel(1, -32)) == "100% - 32", "dim_to_string '100% - 32'");
  check(dim_to_string_c(Dim::rel(0.25, 2)) == "25% + 2", "dim_to_string '25% + 2'");
  check(parse_split_size_c("fill") == SplitSize::filling(1), "parse_split_size fill");
  check(parse_split_size_c("fill 3") == SplitSize::filling(3), "parse_split_size 'fill 3'");
  check(parse_split_size_c("40%") == SplitSize::fixed(Dim::rel(0.4)), "parse_split_size 40%");
  check(!parse_split_size_c("fill 0"), "parse_split_size rejects a zero weight");
  check(!parse_split_size_c("3"), "parse_split_size rejects a bare number string");
  check(split_size_to_string_c(SplitSize::filling(2)) == "fill 2" && split_size_to_string_c(SplitSize::fixed(Dim::abs(3))) == "3",
        "split_size_to_string");
  check(anchor_from_name_c("bottom-right") == Anchor::BottomRight && anchor_name_c(Anchor::Center) == "center" && !anchor_from_name_c("middle"),
        "anchor names");
  check(border_from_name_c("rounded") == Border::Rounded && border_name_c(Border::Heavy) == "heavy" && !border_from_name_c("thick"),
        "border names");

  // ---- 3. the loader -----------------------------------------------------------------------
  std::printf("-- loader\n");
  for (std::string_view name : builtin_layout_names_c()) {
    const Layout* l = builtin_layout_c(name);
    check(l != nullptr && l->name == name, "built-in '" + std::string(name) + "' exists");
    if (!l) continue;
    LayoutLoadReport rep;
    std::optional<Layout> back = load_layout_c(layout_to_json_c(*l), rep);
    check(back && rep.clean() && *back == *l, "built-in '" + std::string(name) + "' round-trips through layout_to_json");
    check(l->popup("help") != nullptr && l->popup("help")->modal, "built-in '" + std::string(name) + "' declares the modal help popup");
    check(l->popup("approval") != nullptr && l->popup("approval")->modal && l->popup("approval")->placement.anchor == Anchor::Bottom,
          "built-in '" + std::string(name) + "' declares the modal approval popup, anchored to the bottom (milestone 10)");
    StackC s(*l);
    const Node* f = rolltui_window_stack_focused(s.s);
    check(f && f->id == "input", "built-in '" + std::string(name) + "' focuses input initially");
  }
  check(builtin_layout_c("nope") == nullptr, "unknown built-in → nullptr");
  {
    LayoutLoadReport rep;
    auto l = load_layout_c(R"({"name": "x", "colour": 1, "root": {"content": "a", "size": "50", "shade": true, "border": "thick"},
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
    auto l = load_layout_c(R"({"root": {"row": [{"content": "a"}, {"content": "a"}]}, "focus": "zzz"})", rep);
    bool dup = false, dangling = false;
    for (const std::string& s : rep.bad_values) { if (s.find("duplicate id 'a'") != std::string::npos) dup = true; if (s.find("focus: no window with id 'zzz'") != std::string::npos) dangling = true; }
    check(l && dup, "a duplicate id is reported");
    check(l && dangling, "a focus naming no window is reported");
  }
  {
    LayoutLoadReport rep;
    check(!load_layout_c("{", rep) && rep.error.find("line") != std::string::npos, "unparseable JSON → nullopt with a line");
    LayoutLoadReport rep2;
    check(!load_layout_c(R"({"name": "x"})", rep2) && rep2.error.find("root") != std::string::npos, "no root → nullopt, error names it");
    LayoutLoadReport rep3;
    auto l = load_layout_c(R"({"root": {"content": "a", "row": []}})", rep3);
    check(l && !rep3.bad_values.empty() && rep3.bad_values[0].find("exactly one") != std::string::npos, "a node with both content and row is a bad value");
  }

  // ---- 4. the split ------------------------------------------------------------------------
  std::printf("-- split\n");
  {
    // Fixed + fill: sums to the extent. No borders: no sharing.
    Node root = row_of({win("a", SplitSize::fixed(Dim::abs(10)), Border::None), win("b", {}, Border::None), win("c", SplitSize::fixed(Dim::rel(0.25)), Border::None)});
    auto v = resolve_tree_c(root, {0, 0, 81, 5}, {0, 0, 81, 5});
    expect_rect("fixed abs 10", by_id(v, "a")->outer, {0, 0, 10, 5});
    expect_rect("rel 25% of 81 floors to 20 (cumulative edge: 10+25% → 30)", by_id(v, "c")->outer, {61, 0, 20, 5});
    expect_rect("fill takes the remainder", by_id(v, "b")->outer, {10, 0, 51, 5});
  }
  {
    Node root = row_of({win("a", SplitSize::fixed(Dim::rel(0.5)), Border::None), win("b", SplitSize::fixed(Dim::rel(0.5)), Border::None)});
    int bad = 0;
    for (int W = 0; W <= 200; ++W) {
      auto v = resolve_tree_c(root, {0, 0, W, 1}, {0, 0, W, 1});
      const ResolvedNode *a = by_id(v, "a"), *b = by_id(v, "b");
      if (!(a->outer.x == 0 && b->outer.x == a->outer.w && a->outer.w + b->outer.w == W)) ++bad;
    }
    check(bad == 0, "50% + 50% in a row tiles at every width 0..200 (" + std::to_string(bad) + " bad)");
  }
  {
    Node root = column_of({win("a", SplitSize::filling(1), Border::None), win("b", SplitSize::filling(2), Border::None), win("c", SplitSize::fixed(Dim::abs(3)), Border::None)});
    int bad = 0;
    for (int H = 0; H <= 200; ++H) {
      auto v = resolve_tree_c(root, {0, 0, 10, H}, {0, 0, 10, H});
      const ResolvedNode *a = by_id(v, "a"), *b = by_id(v, "b"), *c = by_id(v, "c");
      int rem = std::max(H - 3, 0);
      if (!(a->outer.h + b->outer.h == rem && a->outer.h == rem / 3 && c->outer.h == std::min(3, H) && c->outer.y == a->outer.h + b->outer.h)) ++bad;
    }
    check(bad == 0, "fill 1 : fill 2 divide the remainder by weight at every height (" + std::to_string(bad) + " bad)");
  }
  {
    // Shared edge: both bordered → overlap by one; the pair spans the extent exactly.
    Node root = row_of({win("a", SplitSize::fixed(Dim::abs(32))), win("b")});
    auto v = resolve_tree_c(root, {0, 0, 80, 10}, {0, 0, 80, 10});
    expect_rect("bordered a keeps its 32", by_id(v, "a")->outer, {0, 0, 32, 10});
    expect_rect("bordered b starts on a's right border and reaches the edge", by_id(v, "b")->outer, {31, 0, 49, 10});
    expect_rect("b's inner excludes both borders", by_id(v, "b")->inner, {32, 1, 47, 8});
    Node root2 = row_of({win("a", SplitSize::fixed(Dim::abs(32)), Border::None), win("b")});
    auto v2 = resolve_tree_c(root2, {0, 0, 80, 10}, {0, 0, 80, 10});
    expect_rect("one side unbordered: no sharing", by_id(v2, "b")->outer, {32, 0, 48, 10});
  }
  {
    // A column whose children are all bordered is bordered on its side; a mixed one is not.
    Node all = row_of({column_of({win("t"), win("i", SplitSize::fixed(Dim::abs(3)))}), win("s", SplitSize::fixed(Dim::abs(32)))});
    auto v = resolve_tree_c(all, {0, 0, 80, 24}, {0, 0, 80, 24});
    expect_rect("status shares the column's right edge", by_id(v, "s")->outer, {48, 0, 32, 24});
    expect_rect("transcript spans to the shared column", by_id(v, "t")->outer, {0, 0, 49, 22});
    expect_rect("input shares the transcript's bottom edge", by_id(v, "i")->outer, {0, 21, 49, 3});
    Node mixed = row_of({column_of({win("t"), win("i", SplitSize::fixed(Dim::abs(3)), Border::None)}), win("s", SplitSize::fixed(Dim::abs(32)))});
    auto v2 = resolve_tree_c(mixed, {0, 0, 80, 24}, {0, 0, 80, 24});
    expect_rect("a column with an unbordered child does not share", by_id(v2, "s")->outer, {48, 0, 32, 24});
    expect_rect("…so the transcript stops short of it", by_id(v2, "t")->outer, {0, 0, 48, 21});
  }
  {
    Node root = row_of({win("a", SplitSize::fixed(Dim::abs(10)), Border::None), win("hidden", {}, Border::None), win("b", {}, Border::None)});
    root.children[1].visible = false;
    auto v = resolve_tree_c(root, {0, 0, 50, 1}, {0, 0, 50, 1});
    check(by_id(v, "hidden") == nullptr, "a hidden node is not resolved");
    expect_rect("…and takes no space", by_id(v, "b")->outer, {10, 0, 40, 1});
  }
  {
    Node root = row_of({win("a", SplitSize::fixed(Dim::abs(30)), Border::None), win("b", SplitSize::fixed(Dim::abs(30)), Border::None), win("c", SplitSize::fixed(Dim::abs(30)), Border::None)});
    auto v = resolve_tree_c(root, {0, 0, 50, 1}, {0, 0, 50, 1});
    expect_rect("overflow: second is clipped", by_id(v, "b")->outer, {30, 0, 20, 1});
    expect_rect("overflow: third gets 0", by_id(v, "c")->outer, {50, 0, 0, 1});
    Node root2 = row_of({win("a", SplitSize::fixed(Dim::abs(10)), Border::None), win("b", SplitSize::fixed(Dim::abs(10)), Border::None)});
    auto v2 = resolve_tree_c(root2, {0, 0, 50, 1}, {0, 0, 50, 1});
    expect_rect("shortfall with no fill leaves space, no stretch", by_id(v2, "b")->outer, {10, 0, 10, 1});
  }
  {
    Node root = column_of({win("a"), win("b", SplitSize::fixed(Dim::abs(3)))});
    root.border = Border::Single;
    auto v = resolve_tree_c(root, {0, 0, 40, 10}, {0, 0, 40, 10});
    expect_rect("a bordered container splits its inner rect", by_id(v, "a")->outer, {1, 1, 38, 6});
    expect_rect("…bottom child shares a's edge", by_id(v, "b")->outer, {1, 6, 38, 3});
    check(v.front().node == &root && v.size() == 3, "tree order: container first, then children");
  }

  // ---- 5. composition ------------------------------------------------------------------------
  std::printf("-- composition\n");
  const Theme& dark = *builtin_theme("default-dark");
  {
    StackC s(*builtin_layout_c("default"));
    Frame f(80, 24, dark.style(Role::background));
    int slots = 0;
    compose_c(s.s, f, scr, dark, [&](const ResolvedNode& rn, Frame& fr) { ++slots; fr.put_text(rn.inner.x, rn.inner.y, rn.node->content, dark.style(Role::text), rn.inner.w); });
    check(slots == 3, "three slots rendered (" + std::to_string(slots) + ")");
    check(cell(f, 0, 0) == "┌" && cell(f, 79, 0) == "┐" && cell(f, 0, 23) == "└" && cell(f, 79, 23) == "┘", "outer corners");
    check(cell(f, 48, 0) == "┬", "top junction where status meets transcript is ┬ (" + cell(f, 48, 0) + ")");
    check(cell(f, 48, 21) == "┤", "the input's top edge meets the status column from the left: ┤ (" + cell(f, 48, 21) + ")");
    check(cell(f, 0, 21) == "├" && cell(f, 79, 21) == "│", "input's top edge joins the outer frame at ├; the far edge is a plain │");
    check(cell(f, 48, 23) == "┴", "bottom junction under the status column is ┴ (" + cell(f, 48, 23) + ")");
    check(cell(f, 2, 0) == "t" && cell(f, 1, 0) == " " && cell(f, 12, 0) == " " && cell(f, 13, 0) == "─", "title ' transcript ' sits in the top edge after the corner");
    check(cell(f, 1, 1) == "t" && cell(f, 49, 1) == "r" && cell(f, 1, 22) == "i",
          "each slot drew at its inner origin (the contents are transcript:session, rows:status, input:prompt)");
    check(f.at(60, 10).style.bg == dark.style(Role::panel_background).bg, "the status window is filled with panel_background");
    check(f.at(48, 5).style.bg == dark.style(Role::panel_background).bg && f.at(48, 5).style.fg == dark.style(Role::border).fg,
          "a border takes its colour from `border` and its ground from the window it belongs to");
    check(f.at(0, 22).style.fg == dark.style(Role::border_active).fg && f.at(0, 5).style.fg == dark.style(Role::border).fg,
          "the focused input's border is border_active; the transcript's is border");
    // PHASE 17 m3: `title` was the ONE of the four compose roles nothing asserted — the cell
    // it lands in was checked, its colour was not. That is the whole reason a role table can
    // move house and still be wrong: three of the four would have failed loudly and this one
    // would have gone quietly. Pointing the compose at `warning` instead turns this red.
    check(f.at(2, 0).style.fg == dark.style(Role::title).fg && f.at(2, 0).style.fg != dark.style(Role::border).fg,
          "…and the title text is painted with `title`, which is not the border's colour");
  }
  {
    // A popup whose ring crosses the status's left border: no join across layers, and
    // the modal overlay tints only what is beneath.
    StackC s(*builtin_layout_c("default"));
    Layer help = *builtin_layout_c("default")->popup("help");
    rolltui_window_stack_push(s.s, &help);
    Frame f(80, 24, dark.style(Role::background));
    compose_c(s.s, f, scr, dark, [&](const ResolvedNode&, Frame&) {});
    auto v = resolve_stack_c(s.s, scr);
    const ResolvedNode* h = by_id(v, "help");
    check(h && h->outer == Rect{16, 6, 48, 12}, "the help popup lands where the table says at 80x24");
    check(cell(f, 48, 6) == "─" && cell(f, 48, 17) == "─", "the popup's edge crossing the status border stays ─ (never joins the layer below)");
    check(cell(f, 16, 6) == "╭" && cell(f, 63, 17) == "╯", "rounded corners");
    check(f.at(2, 2).style.dim && f.at(70, 2).style.dim, "cells beneath a modal are tinted with `overlay` (dim in default-dark)");
    check(!f.at(20, 8).style.dim && !f.at(16, 6).style.dim, "cells of the modal itself are not tinted");
    check(cell(f, 18, 6) == "h", "popup title drawn");
    Frame g(120, 40, dark.style(Role::background));
    compose_c(s.s, g, {0, 0, 120, 40}, dark, [&](const ResolvedNode&, Frame&) {});
    check(cell(g, 24, 14) == "╭" && cell(g, 95, 25) == "╯", "the same popup re-places itself at 120x40 (24,14)-(95,25)");
  }
  {
    // draw_border on its own, and the degenerate sizes.
    RolltuiDrawScratch* scratch = rolltui_draw_scratch_new();
    RolltuiFrame* f = rolltui_frame_new(10, 3, Style{});
    rolltui_draw_border(f, scratch, Rect{0, 0, 10, 3}, static_cast<unsigned char>(Border::Double), Style{}, "ab", 2,
                       Style{}, 0);
    check(cell_c(f, 0, 0) == "╔" && cell_c(f, 9, 2) == "╝" && cell_c(f, 2, 0) == "a" && cell_c(f, 5, 0) == "═" && cell_c(f, 0, 1) == "║", "double border with title");
    RolltuiFrame* g = rolltui_frame_new(10, 3, Style{});
    rolltui_draw_border(g, scratch, Rect{0, 0, 1, 3}, static_cast<unsigned char>(Border::Single), Style{}, "", 0,
                       Style{}, 0);
    check(cell_c(g, 0, 0) == "╷" && cell_c(g, 0, 1) == "│" && cell_c(g, 0, 2) == "╵", "a 1-wide border is a vertical line");
    rolltui_draw_border(g, scratch, Rect{2, 0, 3, 1}, static_cast<unsigned char>(Border::Single), Style{}, "", 0,
                       Style{}, 0);
    check(cell_c(g, 2, 0) == "╶" && cell_c(g, 3, 0) == "─" && cell_c(g, 4, 0) == "╴", "a 1-high border is a horizontal line");
    rolltui_draw_border(g, scratch, Rect{-2, -1, 5, 3}, static_cast<unsigned char>(Border::Single), Style{}, "", 0,
                       Style{}, 0);
    check(cell_c(g, 2, 1) == "┘" && cell_c(g, 0, 1) == "─", "a border partly off-frame is clipped, not wrapped");
    rolltui_draw_scratch_free(scratch);
    rolltui_frame_free(f);
    rolltui_frame_free(g);
  }

  // ---- 6. the stack ----------------------------------------------------------------------------
  std::printf("-- stack\n");
  {
    StackC s(*builtin_layout_c("default"));
    check(rolltui_window_stack_focused(s.s)->id == "input" && rolltui_window_stack_focus_layer(s.s) == 0, "initial focus is the layout's 'focus'");
    check(route_c(s.s, chr_ev('x'), scr) == Route{Route::Kind::Deliver, "input"}, "a key goes to the focused window");
    check(route_c(s.s, key_ev(Key::Tab), scr) == Route{Route::Kind::FocusMoved, "transcript"}, "Tab cycles to the transcript");
    check(route_c(s.s, key_ev(Key::Tab), scr) == Route{Route::Kind::FocusMoved, "input"}, "Tab wraps back to the input");
    check(route_c(s.s, key_ev(Key::Tab, true), scr) == Route{Route::Kind::FocusMoved, "transcript"}, "Shift-Tab cycles backwards");
    check(route_c(s.s, key_ev(Key::Escape), scr) == Route{Route::Kind::Deliver, "transcript"}, "Escape with no popup is delivered");
    MouseEvent m;
    m.kind = MouseEvent::Kind::Press;
    m.x = 60; m.y = 5;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Deliver, "status"} && rolltui_window_stack_focused(s.s)->id == "transcript",
          "a press on a non-focusable window is delivered and leaves focus alone");
    m.x = 5; m.y = 22;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Deliver, "input"} && rolltui_window_stack_focused(s.s)->id == "input", "a press on a focusable base window focuses it");
    MouseEvent wheel;
    wheel.kind = MouseEvent::Kind::WheelUp;
    wheel.x = 5; wheel.y = 5;
    check(route_c(s.s, mouse_ev(wheel), scr) == Route{Route::Kind::Deliver, "transcript"} && rolltui_window_stack_focused(s.s)->id == "input", "a wheel goes to the window under the pointer, focus unchanged");
    // Pointer capture (milestone 9): a press captures; drags and the release follow it
    // wherever the pointer goes; after the release routing is by position again.
    m.x = 5; m.y = 5;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Deliver, "transcript"} && captured_c(s.s) == "transcript", "a press captures the pointer for its window");
    MouseEvent drag = m;
    drag.kind = MouseEvent::Kind::Drag;
    drag.x = 60; drag.y = 30;  // over the status panel, and below the screen
    check(route_c(s.s, mouse_ev(drag), scr) == Route{Route::Kind::Deliver, "transcript"}, "a drag off the window (even off the screen) still goes to the captured window");
    MouseEvent release = drag;
    release.kind = MouseEvent::Kind::Release;
    check(route_c(s.s, mouse_ev(release), scr) == Route{Route::Kind::Deliver, "transcript"} && captured_c(s.s).empty(), "the release goes there too and ends the capture");
    check(route_c(s.s, mouse_ev(drag), scr) == Route{Route::Kind::Dropped, ""}, "a drag with no capture and no window under it is dropped");
    check(rolltui_window_stack_focused(s.s)->id == "transcript", "the press focused the transcript");
    focus_c(s.s, "input");

    // A modal popup.
    Layer help_popup = *builtin_layout_c("default")->popup("help");
    rolltui_window_stack_push(s.s, &help_popup);
    check(rolltui_window_stack_depth(s.s) == 2 && has_popup_c(s.s, "help") && rolltui_window_stack_focused(s.s)->id == "help" && rolltui_window_stack_focus_layer(s.s) == 1, "a modal focusable popup takes focus");
    check(route_c(s.s, key_ev(Key::Tab), scr) == Route{Route::Kind::Deliver, "help"}, "Tab with one focusable window in the layer is delivered");
    m.x = 5; m.y = 22;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Dropped, ""}, "a press outside a modal is dropped");
    m.x = 20; m.y = 8;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Deliver, "help"}, "a press inside the modal is delivered to it");
    check(route_c(s.s, key_ev(Key::Escape), scr) == Route{Route::Kind::ClosedPopup, "help"} && rolltui_window_stack_depth(s.s) == 1, "Escape closes the topmost popup");
    check(rolltui_window_stack_focused(s.s)->id == "input", "focus returns to the base layer's window");
    check(!rolltui_window_stack_pop(s.s), "pop() on the base alone is false");

    // A non-focusable, non-modal notice above the base.
    Layer notice;
    notice.id = "notice";
    notice.placement = P(Dim::rel(1), Dim::abs(0), Dim::abs(20), Dim::abs(1), Anchor::TopRight);
    notice.root = Node::window("text:hi");
    rolltui_window_stack_push(s.s, &notice);
    check(rolltui_window_stack_focused(s.s)->id == "input" && rolltui_window_stack_focus_layer(s.s) == 0, "a non-focusable notice leaves focus with the base");
    check(route_c(s.s, chr_ev('x'), scr) == Route{Route::Kind::Deliver, "input"}, "keys still reach the base under a non-modal popup");
    m.x = 60; m.y = 5;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Deliver, "status"}, "a press beneath a non-modal popup reaches the base window");
    m.x = 70; m.y = 0;
    check(route_c(s.s, mouse_ev(m), scr) == Route{Route::Kind::Deliver, "text:hi"}, "a press on the notice hits it");
    check(route_c(s.s, key_ev(Key::Escape), scr) == Route{Route::Kind::ClosedPopup, "notice"}, "Escape closes a notice too");

    // A modal layer with no focusable window drops keys rather than leaking them.
    Layer wall;
    wall.id = "wall";
    wall.modal = true;
    wall.root = Node::window("text:wait");
    rolltui_window_stack_push(s.s, &wall);
    check(rolltui_window_stack_focused(s.s) == nullptr && route_c(s.s, chr_ev('x'), scr) == Route{Route::Kind::Dropped, ""}, "a modal with nothing focusable drops keys");
    rolltui_window_stack_pop(s.s);

    // Hot reload: focus survives when the id does; falls back when it does not.
    focus_c(s.s, "transcript");
    Layer reloaded = builtin_layout_c("panel-left")->base;
    reloaded.focus.clear();
    rolltui_window_stack_set_base(s.s, &reloaded);
    check(rolltui_window_stack_focused(s.s)->id == "transcript", "set_base keeps the focused id across a reload when it still exists");
    Layer other;
    other.root = Node::column({win("a", {}, Border::None, true), win("b", {}, Border::None, true)});
    rolltui_window_stack_set_base(s.s, &other);
    check(rolltui_window_stack_focused(s.s)->id == "a", "…and falls back to the first focusable when it does not");
    Layer named = builtin_layout_c("default")->base;
    rolltui_window_stack_set_base(s.s, &named);
    check(rolltui_window_stack_focused(s.s)->id == "input", "a reloaded layout's own 'focus' wins");
    check(find_c(s.s, "status") != nullptr && find_c(s.s, "nope") == nullptr, "find() by id");
    find_c(s.s, "status")->visible = false;
    auto v = resolve_stack_c(s.s, scr);
    check(by_id(v, "status") == nullptr && by_id(v, "transcript")->outer.w == 80, "hiding the status node gives the transcript the width (no-panel by a flag)");
  }
  {
    // A popup layer that is itself a split (a dialog with parts): Tab cycles inside it.
    StackC s(*builtin_layout_c("default"));
    Layer dlg;
    dlg.id = "dlg";
    dlg.modal = true;
    dlg.placement = P(Dim::rel(0.5), Dim::rel(0.5), Dim::abs(40), Dim::abs(10), Anchor::Center);
    dlg.root = Node::column({win("text", {}, Border::None, false), win("field", SplitSize::fixed(Dim::abs(1)), Border::None, true), win("buttons", SplitSize::fixed(Dim::abs(1)), Border::None, true)});
    dlg.root.border = Border::Rounded;
    rolltui_window_stack_push(s.s, &dlg);
    check(rolltui_window_stack_focused(s.s)->id == "field", "a dialog focuses its first focusable part");
    check(route_c(s.s, key_ev(Key::Tab), scr) == Route{Route::Kind::FocusMoved, "buttons"}, "Tab moves to the next part");
    check(route_c(s.s, key_ev(Key::Tab), scr) == Route{Route::Kind::FocusMoved, "field"}, "…and wraps within the dialog, never into the base");
    auto v = resolve_stack_c(s.s, scr);
    const ResolvedNode* frame_node = nullptr;
    for (const ResolvedNode& rn : v)
      if (rn.layer == 1 && !frame_node) frame_node = &rn;
    expect_rect("the dialog's frame", frame_node->outer, {20, 7, 40, 10});
    expect_rect("its text part inside the frame", by_id(v, "text")->outer, {21, 8, 38, 6});
    expect_rect("its buttons at the bottom", by_id(v, "buttons")->outer, {21, 15, 38, 1});
  }

  // ---- 7. content and widgets (Phase 10 m2) --------------------------------------------------
  std::printf("-- content: the kind table\n");
  {
    // Every kind is in the table under its own name, and nothing else is.
    for (WidgetKind k : {WidgetKind::Transcript, WidgetKind::Input, WidgetKind::Menu, WidgetKind::Rows, WidgetKind::Text,
                         WidgetKind::File, WidgetKind::Help})
      check(widget_kind_from_name_c(widget_kind_name_c(k)) == k, "kind '" + std::string(widget_kind_name_c(k)) + "' round-trips");
    check(!widget_kind_from_name_c("dialog") && !widget_kind_from_name_c("") && !widget_kind_from_name_c("Transcript"),
          "an unknown kind name is not in the table (and it is case-sensitive)");

    // Every kind parses with the source its rule demands.
    struct Case { const char* text; WidgetKind kind; const char* source; };
    for (const Case& c : {Case{"transcript:session", WidgetKind::Transcript, "session"},
                          Case{"input:prompt", WidgetKind::Input, "prompt"},
                          Case{"menu:main", WidgetKind::Menu, "main"},
                          Case{"rows:status", WidgetKind::Rows, "status"},
                          Case{"text: a label", WidgetKind::Text, " a label"},
                          Case{"text", WidgetKind::Text, ""},
                          Case{"text:", WidgetKind::Text, ""},
                          Case{"file:/tmp/x.md", WidgetKind::File, "/tmp/x.md"},
                          Case{"help", WidgetKind::Help, ""},
                          // Phase 11 m5b: `help` takes an OPTIONAL scope, so which keys a
                          // window lists is the layout's and not only the host's.
                          Case{"help:app", WidgetKind::Help, "app"},

                          Case{"text:a:b", WidgetKind::Text, "a:b"}}) {
      std::string why;
      const std::optional<Content> got = parse_content_c(c.text, &why);
      check(got && got->kind == c.kind && got->source == c.source,
            std::string("'") + c.text + "' parses as " + std::string(widget_kind_name_c(c.kind)) + " + '" + c.source + "'" +
                (got ? "" : " (" + why + ")"));
    }
    check(content_to_string_c({WidgetKind::Rows, "status"}) == "rows:status" && content_to_string_c({WidgetKind::Help, ""}) == "help" &&
              content_to_string_c({WidgetKind::Help, "app"}) == "help:app" && content_to_string_c({WidgetKind::Text, ""}) == "text",
          "content_to_string is the inverse, and an OPTIONAL source that is empty writes no colon — one spelling, not two");
    check(content_to_string_c({WidgetKind::Rows, ""}) == "rows:",
          "…while a REQUIRED source that is empty keeps its colon: the window says out loud that it needs a name");

    // Every way it can be wrong SAYS SO, by name.
    // A FORBIDDEN source is now only a registered kind's rule to state — no library kind
    // forbids one since m5b gave `help` an optional scope — so the case is tested through
    // one, registered and cleared right here so nothing after it inherits the vocabulary.
    std::string kind_why;
    check(register_widget_kind_c("modal", SourceRule::Forbidden, "", &kind_why), "a host kind that takes no source registers [" + kind_why + "]");
    struct Bad { const char* text; const char* names; };
    for (const Bad& b : {Bad{"dialog:x", "'dialog' is not a widget kind"},
                         Bad{"rows", "'rows' needs a source"},
                         Bad{"modal:x", "'modal' takes no source"},
                         Bad{"", "'' is not a widget kind"}}) {
      std::string why;
      const bool bad = !parse_content_c(b.text, &why);
      check(bad && why.find(b.names) != std::string::npos, std::string("'") + b.text + "' is refused: " + why);
    }
    std::string why;
    check(!parse_content_c("transcript", &why) && why.find("transcript:session") != std::string::npos,
          "a bare Phase 9 slot name that is also a kind name says what to write instead: " + why);
    check(!parse_content_c("status", &why) && why.find("rows:status") != std::string::npos,
          "…and one that is not: " + why);
    clear_registered_widget_kinds_c();
  }
  {
    // The Phase 9 slot names, migrated once by the loader.
    check(migrated_content_c("transcript") == "transcript:session" && migrated_content_c("status") == "rows:status" &&
              migrated_content_c("input") == "input:prompt" && migrated_content_c("menu") == "menu:main",
          "every Phase 9 slot name that still needs one has a migration");
    // Phase 11 m3: `custom:X` became the registered kind `X`. The five composites'
    // PHASE 9 spelling is now valid again — `approval` is a kind name — so their old
    // rows are gone from the table rather than pointing at a spelling that no longer
    // parses, and migrating a valid name would be a rewrite loop.
    check(migrated_content_c("custom:approval") == "approval" && migrated_content_c("custom:details") == "details" &&
              migrated_content_c("custom:editor") == "editor" && migrated_content_c("custom:confirm") == "confirm" &&
              migrated_content_c("custom:report") == "report",
          "every Phase 10 `custom:` content migrates to the registered kind of the same name");
    check(!migrated_content_c("approval") && !migrated_content_c("details") && !migrated_content_c("report"),
          "…and the Phase 9 spelling of those five is NOT migrated: it is the m3 name already");
    check(!migrated_content_c("help") && !migrated_content_c("transcript:session") && !migrated_content_c("banana"),
          "help never moved, an m2 content is not re-migrated, and an unknown name has no migration");

    LayoutLoadReport rep;
    const std::optional<Layout> l = load_layout_c(R"({"name":"old","focus":"input","root":{"column":[
        {"content":"transcript","focusable":true},{"content":"input","size":3,"focusable":true}]}})", rep);
    check(l && rep.clean(), "a Phase 9 layout still loads, clean");
    check(rep.migrated.size() == 3 && rep.migrated[0].find("'transcript' \xE2\x86\x92 'transcript:session'") != std::string::npos,
          "…and every rewritten content is named in the report [" + (rep.migrated.empty() ? "" : rep.migrated[0]) + "]");
    // m4: it also declared no actions, so it was given the shipped default's — a Phase 9
    // layout must not silently lose every app key.
    check(l && l->actions == shipped_default_actions() && rep.migrated.back().find("actions: none declared") == 0,
          "…and a file with no \"actions\" key is given the shipped default's, named in the report");
    const Node& first = l->base.root.children[0];
    check(first.id == "transcript" && first.content == "transcript:session",
          "the window KEEPS its Phase 9 id (host lookups and 'focus' still work) and gains the new content");
    check(l->base.focus == "input" && l->base.root.children[1].id == "input", "…so the layout's own focus id still names a window");

    LayoutLoadReport rep2;
    std::string modal_why;
    register_widget_kind_c("modal", SourceRule::Forbidden, "", &modal_why);  // see the parse block above
    const std::optional<Layout> l2 = load_layout_c(R"({"name":"bad","root":{"column":[{"content":"dialog:x"},{"content":"modal:x"}]}})", rep2);
    // Phase 11 m3 moved ONE of these. A forbidden source is a fact about the string and
    // is still the loader's to name; an UNKNOWN KIND is not, because rung 2 of the
    // vocabulary belongs to a host that may not have registered yet — the library's own
    // shipped `default` layout names roll's `approval`, so judging it here would abort
    // the build. Windows reports it instead, where the registry actually is (below).
    check(l2 && !rep2.clean() && rep2.bad_values.size() == 1 &&
              rep2.bad_values[0].find("root.column[1].content: 'modal' takes no source") != std::string::npos,
          "a forbidden source is a bad value named by PATH, and the layout still loads");
    clear_registered_widget_kinds_c();
    std::string dwhy;
    ContentProblem dwhat = ContentProblem::None;
    check(!parse_content_c("dialog:x", &dwhy, &dwhat) && dwhat == ContentProblem::UnknownKind &&
              dwhy.find("'dialog' is not a widget kind") != std::string::npos,
          "…and the unknown kind is still parse_content's named refusal, tagged so the loader can leave it to the host");
  }
  {
    // ---- m4: the layout DECLARES the actions its screen emits ---------------------
    LayoutLoadReport rep;
    const std::optional<Layout> l = load_layout_c(R"({"name":"acts","actions":{"app.zoom":"zoom in","mine.thing":"my own"},
        "root":{"content":"help"}})", rep);
    check(l && rep.clean() && l->actions.size() == 2 && l->actions[0].name == "app.zoom" &&
              l->actions[0].description == "zoom in" && l->actions[1].name == "mine.thing",
          "\"actions\" is an object of name → description, in file order");
    check(rep.migrated.empty(), "…and a file that declares actions is not given the shipped default's");

    LayoutLoadReport er;
    const std::optional<Layout> bad = load_layout_c(R"({"name":"bad","actions":{"app.a":"ok","nodot":"x","app.":"x",
        "input.frob":"x","menu.frob":"x","app.b":7},"root":{"content":"help"}})", er);
    check(bad && bad->actions.size() == 1 && bad->actions[0].name == "app.a", "…and only the well-formed ones are declared");
    // Indexed through a bounds-checked helper: a control that SEGFAULTS reports nothing
    // (CLAUDE.md), and every one of these expects a specific position in the list.
    auto v = [&](std::size_t i) { return i < er.bad_values.size() ? er.bad_values[i] : std::string("(none)"); };
    check(er.bad_values.size() == 5 && v(0).find("actions.nodot: an action is \"<scope>.<verb>\"") == 0 &&
              v(1).find("actions.app.: an action is") == 0,
          "a name that is not \"<scope>.<verb>\" is a bad value [" + v(0) + "]");
    check(v(2).find("actions.input.frob: the 'input' scope is the library's") == 0 &&
              v(3).find("actions.menu.frob: the 'menu' scope is the library's") == 0,
          "a library scope cannot be declared into [" + v(2) + "]");
    check(v(4).find("actions.app.b: expected a description string") == 0,
          "a non-string description is a bad value [" + v(4) + "]");

    // Present-but-empty is a deliberate "none" and must differ from absent, or the
    // migration above would quietly re-add what someone deliberately removed.
    LayoutLoadReport nr;
    const std::optional<Layout> none = load_layout_c(R"({"name":"none","actions":{},"root":{"content":"help"}})", nr);
    check(none && none->actions.empty() && nr.migrated.empty(), "an explicit \"actions\": {} declares none and is left alone");

    // Round trip, including the empty case (which is why "actions" is always written).
    LayoutLoadReport rr;
    const std::optional<Layout> back = load_layout_c(layout_to_json_c(*l), rr);
    check(back && rr.clean() && back->actions == l->actions && *back == *l, "a layout's actions round-trip through layout_to_json");
    LayoutLoadReport rn2;
    const std::optional<Layout> none_back = load_layout_c(layout_to_json_c(*none), rn2);
    check(none_back && none_back->actions.empty() && rn2.migrated.empty(), "…and so does declaring none");

    // Every shipped layout declares the same app scope: switching arrangement must not
    // change which keys work.
    bool same = true;
    for (std::string_view n : builtin_layout_names_c()) same &= builtin_layout_c(n)->actions == shipped_default_actions();
    check(same && shipped_default_actions().size() == 6, "every shipped layout declares the same six app actions");
  }

  std::printf("-- widgets: one per content, by kind\n");
  {
    // A layout with every kind in it, and a host that binds every source.
    const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/rolltui_widgets_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/menus");
    { std::ofstream(dir + "/note.md") << "from a file"; }
    { std::ofstream(dir + "/menus/main.json") << R"({"id":"root","label":"menu","items":[{"id":"act","label":"an action"}]})"; }

    LayoutLoadReport lr;
    std::optional<Layout> lay = load_layout_c(R"({"name":"every-kind","focus":"prompt","root":{"column":[
        {"id":"tx","content":"transcript:session"},
        {"id":"panel","content":"rows:status","size":2},
        {"id":"label","content":"text:a literal","size":1},
        {"id":"doc","content":"file:note.md","size":1},
        {"id":"keys","content":"help","size":1},
        {"id":"m","content":"menu:main","size":1},
        {"id":"own","content":"mine:one","size":1},
        {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})", lr);
    check(lay && lr.clean(), "the every-kind layout loads clean");

    Document doc;
    DocEntry e;
    e.id = "e0";
    e.text = "hello transcript";
    doc.entries.push_back(e);
    int drew_own = 0;
    Windows windows;
    windows.set_dir(dir);
    windows.bind_document("session", &doc);
    windows.bind_rows("status", [](Rows& out) { out.add("label", "value"); });
    windows.bind_submit("prompt", [](const std::string&) {});
    // Phase 11 m3: a kind this test registers, built by the library like any other.
    windows.register_kind("mine", [&] {
      return std::make_unique<CallbackWidget>(
          [&](const ResolvedNode& rn, Frame& f, const Theme& th) {
            ++drew_own;
            f.put_text(rn.inner.x, rn.inner.y, "mine!", th.style(Role::text), rn.inner.w);
          },
          nullptr);
    });
    windows.set_help("", {"transcript"}, "");

    WindowStack s(*lay);
    const Rect box{0, 0, 40, 12};
    const WindowsReport rep = windows.prepare(s, box);
    check(rep.clean(), "every source is bound: nothing to report [" + rep.summary() + "]");

    Frame f(40, 12, dark.style(Role::background));
    s.compose(f, box, dark, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, dark); });
    auto row = [&](int y) {
      std::string out;
      for (int x = 0; x < 40; ++x) out += f.glyph(x, y);
      while (!out.empty() && out.back() == ' ') out.pop_back();
      return out;
    };
    const std::vector<ResolvedNode> v = s.resolve(box);
    check(row(by_id(v, "tx")->inner.y).find("hello transcript") != std::string::npos, "transcript: the bound document [" + row(0) + "]");
    check(row(by_id(v, "panel")->inner.y).find("label") != std::string::npos &&
              row(by_id(v, "panel")->inner.y).find("value") != std::string::npos,
          "rows: the bound rows [" + row(by_id(v, "panel")->inner.y) + "]");
    check(row(by_id(v, "label")->inner.y) == "a literal", "text: the literal from the layout file [" + row(by_id(v, "label")->inner.y) + "]");
    check(row(by_id(v, "doc")->inner.y) == "from a file", "file: the file's text [" + row(by_id(v, "doc")->inner.y) + "]");
    check(row(by_id(v, "keys")->inner.y).rfind("transcript:", 0) == 0 &&
              row(by_id(v, "keys")->inner.y).find("\xE2\x96\xBC") != std::string::npos,
          "help: the live bindings, scrolling (the scope heading and the marker) [" + row(by_id(v, "keys")->inner.y) + "]");
    check(row(by_id(v, "m")->inner.y).find("an action") != std::string::npos,
          "menu: the menu FILE's items [" + row(by_id(v, "m")->inner.y) + "]");
    check(drew_own == 1 && row(by_id(v, "own")->inner.y) == "mine!", "a REGISTERED kind draws, once (Phase 11 m3)");
    check(row(by_id(v, "prompt")->inner.y).find(">") == 0, "input: the line editor's prompt [" + row(by_id(v, "prompt")->inner.y) + "]");

    // The typed accessors a host routes with.
    check(windows.transcript_at("tx") == windows.transcript("session") && windows.input_at("prompt") == windows.input("prompt") &&
              windows.menu_at("m") == windows.menu("main") && windows.at("own") == windows.registered("mine", "one"),
          "a window's widget is reachable by window id, typed by kind — a registered one through registered(), the same shape as transcript(source)");
    check(!windows.transcript_at("prompt") && !windows.input_at("tx") && !windows.menu_at("own") && !windows.registered("nope"),
          "…and never as the wrong kind, and an unregistered name is nullptr rather than an empty instance");
    check(windows.content_at("panel") == Content{WidgetKind::Rows, "status"} && !windows.content_at("nope"),
          "content_at names what a window holds");

    // PHASE 17 m1c — THE CHECK THAT SEPARATES "PORTED" FROM "REACHABLE", and the reason the
    // milestone was reopened after being ticked. The three accessors above were reachable
    // through `Windows` for four phases; what was NOT was getting the typed handle out of the
    // C table, because the objects lived in C++ maps and a C-side map beside them would have
    // been a second owner. So this drives the input through the LIBRARY's entry points alone,
    // on the table's own handle, and looks for the bytes on the drawn frame — an accessor that
    // handed back a different object would pass every check above and fail this one.
    RolltuiWindows* wh = windows.handle();
    rolltui_input_set_text(rolltui_windows_input(wh, "prompt", 6), "typed through the C", 19);
    windows.prepare(s, box);
    Frame f2(40, 12, dark.style(Role::background));
    s.compose(f2, box, dark, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, dark); });
    auto row2 = [&](int y) {
      std::string out;
      for (int x = 0; x < 40; ++x) out += f2.glyph(x, y);
      while (!out.empty() && out.back() == ' ') out.pop_back();
      return out;
    };
    const int prompt_y = by_id(s.resolve(box), "prompt")->inner.y;
    check(row2(prompt_y).find("typed through the C") != std::string::npos,
          "the widget a window draws IS the object the C table hands back by source — one owner, "
          "asked for through `rolltui_windows_input` and nothing else [" + row2(prompt_y) + "]");
    rolltui_input_clear(rolltui_windows_input(wh, "prompt", 6));

    // One widget per CONTENT: two windows on one source are one widget, and a layout
    // reload keeps what the user typed.
    rolltui_input_set_text(windows.input("prompt"), "half-typed", 10);
    LayoutLoadReport lr2;
    std::optional<Layout> two = load_layout_c(R"({"name":"two","root":{"column":[
        {"id":"a","content":"transcript:session"},{"id":"b","content":"transcript:session"},
        {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})", lr2);
    check(two && lr2.clean(), "a layout showing one document in two windows loads");
    s.set_base(two->base);
    windows.prepare(s, box);
    check(windows.transcript_at("a") == windows.transcript_at("b"), "two windows on one content are ONE widget");
    check(input_text(windows.input("prompt")) == "half-typed", "a layout reload keeps the input's text (the widget belongs to the content)");
    rolltui_input_clear(windows.input("prompt"));
  }
  {
    // Every failure, by name AND on screen — never a blank window.
    Windows windows;
    LayoutLoadReport lr;
    std::optional<Layout> lay = load_layout_c(R"({"name":"unbound","root":{"column":[
        {"id":"a","content":"transcript:nope","size":1},
        {"id":"b","content":"rows:nope","size":1},
        {"id":"c","content":"nope:x","size":1},
        {"id":"d","content":"input:nope","size":1},
        {"id":"e","content":"menu:nope","size":1},
        {"id":"f","content":"file:/nope/nothing.md","size":1},
        {"id":"g","content":"dialog:x","size":1}]}})", lr);
    check(lay && lr.clean(), "no bad value from the LOADER: an unbound source and an unknown kind are both host facts (Phase 11 m3)");
    WindowStack s(*lay);
    const Rect box{0, 0, 60, 7};
    const WindowsReport rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 7, "all seven windows are reported (" + std::to_string(rep.bad_values.size()) + ")");
    const std::string all = [&] {
      std::string j;
      for (const std::string& b : rep.bad_values) j += b + "\n";
      return j;
    }();
    for (const char* named : {"window 'a' (content 'transcript:nope'): nothing is bound to 'nope'",
                              "window 'b' (content 'rows:nope'): nothing is bound to 'nope'",
                              "'nope' is not a widget kind",
                              "window 'd' (content 'input:nope'): nothing is bound to 'nope'",
                              "window 'e' (content 'menu:nope'): no menu file 'nope'",
                              "cannot read '/nope/nothing.md'", "'dialog' is not a widget kind"})
      check(all.find(named) != std::string::npos, std::string("reported by name: ") + named);

    Frame f(60, 7, dark.style(Role::background));
    s.compose(f, box, dark, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, dark); });
    int drawn = 0;
    for (int y = 0; y < 7; ++y) {
      std::string out;
      for (int x = 0; x < 60; ++x) out += f.glyph(x, y);
      if (out.find("[") != std::string::npos && out.find("nothing is bound") == std::string::npos &&
          out.find("cannot read") == std::string::npos && out.find("not a widget kind") == std::string::npos &&
          out.find("no menu file") == std::string::npos)
        continue;
      if (out.find("[") != std::string::npos) ++drawn;
    }
    check(drawn == 7, "…and every one of them DREW its reason (" + std::to_string(drawn) + " of 7): a bad window is never blank");
  }
  {
    // ---- m3: a menu is a FILE, resolved through three rungs -----------------------
    // The order is the whole point: a user's own file shadows the host's, which shadows
    // the library's shipped one, and each is named so a surprising menu has one place
    // to be traced from.
    const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/rolltui_menus_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/menus");

    LayoutLoadReport lr;
    std::optional<Layout> lay = load_layout_c(R"({"name":"menus","root":{"column":[
        {"id":"a","content":"menu:main"},
        {"id":"b","content":"menu:extra"}]}})", lr);
    check(lay && lr.clean(), "a layout naming two menu files loads clean");
    WindowStack s(*lay);
    const Rect box{0, 0, 44, 8};

    Windows windows;
    windows.set_dir(dir);
    // Read a loaded tree without ever indexing into one that did not load: a control
    // that SEGFAULTS reports nothing (CLAUDE.md), and every rung here can be empty.
    auto first_label = [&](const char* name) {
      const MenuItem& r = *rolltui_menu_root(windows.menu(name));
      return r.children.empty() ? std::string("(no items)") : r.children.front().label;
    };
    auto option_count = [&](const char* name, const char* id) {
      const MenuItem* it = rolltui_menu_find(windows.menu(name), id, std::strlen(id));
      return it ? static_cast<int>(it->children.size()) : -1;
    };
    // Rung 3, with nothing else present: the library's own shipped menus/main.json.
    check(!shipped_menu("main").empty() && shipped_menu("nothing-ships-this").empty(),
          "the library ships menus/main.json and nothing under a name it has no file for");
    check(windows.menu_origin("main") == "a shipped menu" && rolltui_menu_root(windows.menu("main"))->label == "settings",
          "with no user file and no host menu, `menu:main` is the SHIPPED one [" + windows.menu_origin("main") + "]");

    // Rung 2: a menu the host carries in its binary shadows the shipped one.
    windows.add_menu("main", R"({"id":"root","label":"the host's","items":[{"id":"h","label":"host item"}]})");
    check(windows.menu_origin("main") == "the host's" && rolltui_menu_root(windows.menu("main"))->label == "the host's",
          "a host's add_menu() shadows the shipped file [" + windows.menu_origin("main") + "]");

    // Rung 1: the user's own file shadows both — and is picked up with no rebuild and
    // no restart, which is what a menu file being a file is FOR.
    { std::ofstream(dir + "/menus/main.json") << R"({"id":"root","label":"mine","items":[{"id":"u","label":"user item"}]})"; }
    check(windows.menu_origin("main") == dir + "/menus/main.json" && rolltui_menu_root(windows.menu("main"))->label == "mine",
          "a user's menus/main.json shadows the host's and the shipped one [" + windows.menu_origin("main") + "]");
    check(rolltui_menu_root(windows.menu("main"))->children.size() == 1 && first_label("main") == "user item",
          "…and it is the user's tree that is loaded [" + first_label("main") + "]");

    // A window naming a menu nobody has is a named bad value AND a drawn reason.
    WindowsReport rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("window 'b' (content 'menu:extra'): no menu file 'extra'") != std::string::npos &&
              rep.bad_values[0].find(dir + "/menus/extra.json") != std::string::npos,
          "a menu file nobody has is reported by name, with where it was looked for [" + rep.summary() + "]");

    // The Done-when's second half, at the library level: DROP the file in and it opens.
    // Nothing is rebuilt, nothing restarts, no host code knows the name 'extra'.
    { std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"dropped","items":[{"id":"d","label":"dropped item"}]})"; }
    rep = windows.prepare(s, box);
    check(rep.clean(), "a menu file dropped in after the fact resolves with no rebuild [" + rep.summary() + "]");
    // Phase 10 m5: what the design editor offers as the menu-file choice is the UNION
    // of the three rungs, deduplicated and sorted — a name is offered because a rung
    // has it, never because a host listed it. 'extra' and 'main' are the user's here;
    // 'main' is also the host's and the shipped one, and appears once.
    {
      const std::vector<std::string> names = windows.menu_names();
      std::string joined;
      for (const std::string& n : names) joined += (joined.empty() ? "" : ",") + n;
      check(joined == "extra,main", "menu_names() is the three rungs' union, deduplicated and sorted [" + joined + "]");
    }
    Frame f(44, 8, dark.style(Role::background));
    s.compose(f, box, dark, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, dark); });
    const std::string screen = [&] {
      std::string out;
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 44; ++x) out += f.glyph(x, y);
      return out;
    }();
    check(screen.find("dropped item") != std::string::npos && screen.find("user item") != std::string::npos,
          "…and both menus DRAW their items: the dropped one and the user's");

    // A file that CHANGES is re-read, like `file:` — one rule for both.
    { std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"dropped","items":[{"id":"d","label":"edited item"}]})"; }
    std::filesystem::last_write_time(dir + "/menus/extra.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2));
    check(first_label("extra") == "edited item", "an edited menu file is re-read [" + first_label("extra") + "]");
    // …including a rewrite the clock cannot separate: same modification SECOND, which is
    // all a plain st_mtime carries. Forced to the identical timestamp here, so this is a
    // property and not a race — a whole-second stamp keeps serving the old tree.
    {
      const std::filesystem::file_time_type when = std::filesystem::last_write_time(dir + "/menus/extra.json");
      std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"dropped","items":[{"id":"d","label":"edited twice in one second"}]})";
      std::filesystem::last_write_time(dir + "/menus/extra.json", when);
      check(first_label("extra") == "edited twice in one second",
            "…and one rewritten within the same second, at an identical timestamp [" + first_label("extra") + "]");
    }

    // Every way a menu file can be wrong, by name. Unusable JSON stops it drawing; an
    // unknown key does not — the loaders report rather than ignore (Layout.hpp's rule).
    { std::ofstream(dir + "/menus/extra.json") << "not json at all"; }
    std::filesystem::last_write_time(dir + "/menus/extra.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(4));
    rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("is unusable") != std::string::npos &&
              rep.bad_values[0].find(dir + "/menus/extra.json") != std::string::npos,
          "an unparsable menu file is a bad value naming the file [" + rep.summary() + "]");
    { std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"x","colour":"red","items":[{"id":"d","label":"still here"}]})"; }
    std::filesystem::last_write_time(dir + "/menus/extra.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(6));
    rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("colour") != std::string::npos,
          "an unknown key is reported… [" + rep.summary() + "]");
    check(windows.menu_at("b") && rolltui_menu_root(windows.menu_at("b"))->children.size() == 1 && first_label("extra") == "still here",
          "…and the menu still loads and draws (an unknown key is not fatal) [" + first_label("extra") + "]");

    // A host's set_options on a file-loaded tree: the structure is the file's, the
    // options are the host's runtime facts — the split both hosts now live on.
    RolltuiMenuItemList two_options;
    two_options.push_back(MenuItem::action("one", "one"));
    two_options.push_back(MenuItem::action("two", "two"));
    rolltui_menu_set_options(windows.menu("extra"), "d", 1, &two_options);
    check(option_count("extra", "d") == 2, "a host fills a file-loaded item's options by id (" + std::to_string(option_count("extra", "d")) + ")");

    // ---- m4: a menu item may NAME an action, and it is checked against the live table.
    { std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"x","items":[
        {"id":"d","label":"Details","action":"app.details"},{"id":"z","label":"Zoom","action":"app.zoom"}]})"; }
    std::filesystem::last_write_time(dir + "/menus/extra.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(8));
    Bindings binds = default_bindings();   // declares the shipped layout's app scope, not app.zoom
    WidgetEnv wenv;
    wenv.bindings = &binds;
    windows.set_env(wenv);
    rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("item 'z' names the action 'app.zoom', which no layout declares") != std::string::npos,
          "a menu item naming an UNDECLARED action is a bad value, by item and action [" + rep.summary() + "]");
    check(rolltui_menu_find(windows.menu("extra"), "d", 1) != nullptr, "…and the item that names a DECLARED action is not reported");

    // declare() is the whole screen's list (m6), so app.details has to be named again
    // here or it stops being declared — which the very next assertion relies on.
    binds.declare({{"app.details", "open the session details"}, {"app.zoom", "zoom in"}});
    rep = windows.prepare(s, box);
    check(rep.clean(), "…and declaring the action clears it [" + rep.summary() + "]");
    // The point of naming the action: the shortcut is the LIVE chords, so a rebinding
    // can never leave a stale key in a menu file.
    auto shortcut_of = [&](const char* id) {
      const MenuItem* it = rolltui_menu_find(windows.menu("extra"), id, std::strlen(id));
      return it ? it->shortcut : std::string("(no such item)");
    };
    check(shortcut_of("d") == "F3" && shortcut_of("z").empty(),
          "an item's shortcut is rendered from the live chords [" + shortcut_of("d") + "]");
    binds.bind("app.details", *parse_chord("f9"));
    windows.prepare(s, box);
    check(shortcut_of("d") == "F3, F9", "…and follows a rebinding immediately [" + shortcut_of("d") + "]");
    // m6: an action the CURRENT layout no longer declares is inert, so its item shows no
    // shortcut at all — the table still keeps its two chords. A menu that advertised them
    // would promise a key that cannot fire (found by the files-only proof screen).
    binds.declare({{"app.zoom", "zoom in"}});
    rep = windows.prepare(s, box);
    check(binds.chords_for("app.details").size() == 2 && binds.action_for(*parse_chord("f3"), "app").empty() &&
              shortcut_of("d").empty() && !rep.clean(),
          "an UNdeclared action keeps its chords, emits nothing, shows no shortcut and is reported again [" +
              shortcut_of("d") + "]");
  }
  {
    // ---- m4, the Done-when: `help` renders an action declared ONLY in a layout file.
    // Nothing here compiles the action in: the layout declares it, the bindings file
    // gives it a chord, and the help window is the only thing that draws it.
    LayoutLoadReport lr;
    const std::optional<Layout> lay = load_layout_c(R"({"name":"declared","actions":{"app.zoom":"zoom the transcript"},
        "root":{"content":"help"}})", lr);
    check(lay && lr.clean(), "a layout declaring one app action loads clean [" + (lr.bad_values.empty() ? "" : lr.bad_values[0]) + "]");
    BindingsLoadReport br;
    std::optional<Bindings> binds = Bindings::from_json(R"({"name":"b","bindings":{"input.submit":["enter"],"app.zoom":["ctrl+g"]}})", br);
    check(binds && br.clean() && !binds->has("app.zoom"), "a bindings file alone does not make the action exist");
    binds->declare(action_decls(lay->actions));  // the one line a host runs

    Windows windows;
    windows.set_help("", {"app"}, "");
    WidgetEnv wenv;
    wenv.bindings = &*binds;
    windows.set_env(wenv);
    WindowStack s(*lay);
    const Rect box{0, 0, 46, 4};
    check(windows.prepare(s, box).clean(), "the help window has nothing to report");
    Frame f(46, 4, dark.style(Role::background));
    s.compose(f, box, dark, [&](const ResolvedNode& rn, Frame& fr) { windows.draw(rn, fr, dark); });
    std::string screen;
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 46; ++x) screen += f.glyph(x, y);
    check(screen.find("Ctrl-G") != std::string::npos && screen.find("zoom the transcript") != std::string::npos,
          "help RENDERS an action that exists only because a layout file declared it [" + screen.substr(0, 46) + "]");
  }
  {
    // The input sizes its own window: it grows with its text, capped at half the
    // parent, and a bound note takes a row when it cannot sit beside one.
    check(input_max_rows(24, 2) == 10 && input_max_rows(23, 2) == 9 && input_max_rows(24, 0) == 12 && input_max_rows(3, 2) == 1,
          "input_max_rows is half the parent less the border, at least 1");
    check(input_rows(1, 10, 0, 40, 10) == 1 && input_rows(1, 10, 8, 40, 10) == 1, "one row, and a note that fits beside it, stay one row");
    check(input_rows(1, 35, 8, 40, 10) == 2 && input_rows(3, 5, 8, 40, 10) == 4, "a note that would overlap gets its own row");
    check(input_rows(12, 5, 0, 40, 10) == 10 && input_rows(12, 5, 5, 40, 10) == 10, "the cap wins over both the text and the note");
    check(input_rows(0, 0, 0, 40, 10) == 1 && input_rows(5, 0, 0, 40, 0) == 1, "never fewer than one row, whatever the cap");

    Document doc;
    Windows windows;
    windows.bind_document("session", &doc);
    windows.bind_submit("prompt", [](const std::string&) {});
    LayoutLoadReport lr;
    std::optional<Layout> lay = load_layout_c(R"({"name":"grow","focus":"prompt","root":{"column":[
        {"id":"tx","content":"transcript:session"},
        {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})", lr);
    WindowStack s(*lay);
    const Rect box{0, 0, 20, 20};
    windows.prepare(s, box);
    check(s.find("prompt")->size == SplitSize::fixed(Dim::abs(1)), "an empty input takes one row");
    set_input_text(windows.input("prompt"), "one\ntwo\nthree");
    windows.prepare(s, box);
    check(s.find("prompt")->size == SplitSize::fixed(Dim::abs(3)), "three lines of text: three rows");
    set_input_text(windows.input("prompt"), std::string(30, 'x') + "\n" + std::string(30, 'y') + "\n" + std::string(200, 'z'));
    windows.prepare(s, box);
    check(s.find("prompt")->size == SplitSize::fixed(Dim::abs(10)), "…and never more than half the parent's height");
    windows.bind_note("prompt", [](Note& out) { out.text = "working"; });
    set_input_text(windows.input("prompt"), "hi");
    windows.prepare(s, box);
    check(s.find("prompt")->size == SplitSize::fixed(Dim::abs(1)), "a note that fits beside one row of text adds nothing");
    set_input_text(windows.input("prompt"), "a text that is much longer than the window");
    windows.prepare(s, box);
    check(s.find("prompt")->size == SplitSize::fixed(Dim::abs(4)), "…and takes its own row under a wrapped one (3 text rows + 1)");
  }

  // ---- the grep control: no host resolves a content string itself ----------------------
  // The m2 Done-when. Before this milestone every host answered "what does this slot
  // mean?" with an if-chain over `rn.node->content`; after it, Windows does, from one
  // table. A host may still READ a content — the layout editor SHOWS it — so the
  // control is a count with a stated allowance, not a ban: an alias
  // (`const std::string& c = rn.node->content;`) cannot slip past a count.
  {
    const std::string dir = std::string(ROLLTUI_SOURCE_DIR) + "/tools";
    std::vector<std::string> files;
    if (DIR* d = opendir(dir.c_str())) {
      while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 4 && (n.substr(n.size() - 4) == ".cpp" || n.substr(n.size() - 4) == ".hpp")) files.push_back(n);
      }
      closedir(d);
    }
    check(files.size() >= 4, "scanned the library's tools (" + std::to_string(files.size()) + " files)");
    const std::regex reads(R"((->|\.)content\b)");
    std::vector<std::string> hits;
    for (const std::string& f : files) {
      std::ifstream in(dir + "/" + f);
      std::stringstream ss;
      ss << in.rdbuf();
      std::istringstream lines(ss.str());
      std::string line;
      int ln = 0;
      while (std::getline(lines, line)) {
        ++ln;
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 2, "//") == 0) continue;  // a comment is not code
        if (std::regex_search(line, reads)) hits.push_back(f + ":" + std::to_string(ln) + ":" + line.substr(first == std::string::npos ? 0 : first));
      }
    }
    // The allowance, named: layout_editor.cpp — an EDITOR of layouts must read what a
    // window holds, to offer it and to show it. Nothing else may.
    std::vector<std::string> unexpected;
    for (const std::string& h : hits)
      if (h.rfind("layout_editor.cpp:", 0) != 0) unexpected.push_back(h);
    check(unexpected.empty(), "no rolltui tool resolves a content itself; the only reads are the layout editor's (" +
                                  std::to_string(hits.size()) + " reads, " + std::to_string(unexpected.size()) + " unexpected)" +
                                  (unexpected.empty() ? "" : ": " + unexpected.front()));
  }


  // ---- 9. a HOST REGISTERS A KIND (Phase 11 m3) --------------------------------------
  // The milestone's Done-when, driven through the real Windows. The whole claim is that
  // a host writes ONE CLASS and ONE REGISTRATION and the library then treats its widget
  // exactly like a built-in — so this test writes a real Widget (a two-cell "canvas"
  // that records the drags it is given and asks for a size), never a draw callback.
  std::printf("-- registered kinds: a host's own widget, built by the library\n");
  {
    struct Canvas : Widget {
      std::vector<std::string> got;   // "press 3,4", "drag -5,99", "release 1,1"
      bool broken = false;
      int want_rows = 0;
      std::string problem() const override { return broken ? "the canvas is broken" : std::string(); }
      std::optional<int> desired_outer(int, int, int) const override {
        return want_rows > 0 ? std::optional<int>(want_rows) : std::nullopt;
      }
      void layout(const ResolvedNode&) override {}
      void draw(const ResolvedNode& rn, Frame& f, const Theme& th) override {
        f.put_text(rn.inner.x, rn.inner.y, "canvas " + content.source, th.style(Role::text), rn.inner.w);
      }
      bool handle(const Event& e) override {
        const MouseEvent* m = std::get_if<MouseEvent>(&e);
        if (!m) return false;
        const char* k = m->kind == MouseEvent::Kind::Press ? "press" : m->kind == MouseEvent::Kind::Drag ? "drag" : "release";
        got.push_back(std::string(k) + " " + std::to_string(m->x) + "," + std::to_string(m->y));
        return true;
      }
    };
    clear_registered_widget_kinds_c();
    Windows windows;
    int built = 0;
    std::string why;
    check(windows.register_kind("canvas", [&] { ++built; return std::make_unique<Canvas>(); }, SourceRule::Required,
                                "a drawing surface", &why),
          "a host registers a kind with one call [" + why + "]");

    // Rung 1 is never shadowed — guard one, the refusal, by name.
    why.clear();
    check(!windows.register_kind("input", [] { return std::make_unique<Canvas>(); }, SourceRule::Required, "", &why) &&
              why.find("'input' is one of the library's own kinds") != std::string::npos,
          "registering a LIBRARY kind is refused, by name [" + why + "]");
    // …and guard two, independently: even after that attempt, `input:prompt` is still the
    // library's input. This is the assertion the milestone's control breaks.
    check(parse_content_c("input:prompt")->kind == WidgetKind::Input,
          "…and `input` still resolves at rung 1: the library's table is searched FIRST, whatever a host tried to register");
    check(widget_kind_names_c().back() == "canvas" && widget_kind_names_c().size() == rolltui_widget_kind_library_count() + 1,
          "the registered kind is enumerable, after the library's, in resolution order");

    LayoutLoadReport lr;
    const std::optional<Layout> lay = load_layout_c(R"({"name":"paint","root":{"column":[
        {"id":"a","content":"canvas:main","size":3,"focusable":true},
        {"id":"b","content":"canvas:main","size":3},
        {"id":"c","content":"canvas:other","size":3},
        {"id":"d","content":"nosuch:x","size":1}]}})", lr);
    check(lay && lr.clean(), "a layout naming a registered kind loads clean [" + (lr.bad_values.empty() ? std::string() : lr.bad_values[0]) + "]");
    WindowStack st(*lay);
    const Rect box{0, 0, 30, 10};
    const WindowsReport rep = windows.prepare(st, box);
    check(built == 2, "two windows on ONE content share ONE instance; a second content is a second (" + std::to_string(built) + " built)");
    check(windows.at("a") == windows.at("b") && windows.at("a") != windows.at("c"), "…and that is what the two windows hold");
    check(windows.registered("canvas", "main") == windows.at("a"), "registered(kind, source) reaches it, like transcript(source)");
    // An unregistered kind is still the Phase 10 m2 answer: named in the report, error
    // panel drawn — never a blank window.
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("window 'd'") != std::string::npos &&
              rep.bad_values[0].find("'nosuch' is not a widget kind") != std::string::npos,
          "an UNregistered kind is a named bad value on the window [" + rep.summary() + "]");

    // Press / drag / release, including a drag far outside the window and off the screen:
    // the stack captures for the pressed window, so a registered kind gets drag-to-paint
    // with edge handling for free.
    Canvas* c = static_cast<Canvas*>(windows.at("a"));
    MouseEvent m;
    m.kind = MouseEvent::Kind::Press;
    m.x = 1; m.y = 1;
    Route r = st.route(m, box);
    check(r.kind == Route::Kind::Deliver && r.window == "a" && windows.handle(r.window, m), "a press reaches the registered widget");
    MouseEvent d = m;
    d.kind = MouseEvent::Kind::Drag;
    d.x = -5; d.y = 99;
    r = st.route(d, box);
    check(r.window == "a" && windows.handle(r.window, d), "…and a drag past its own edge, off the screen, still does");
    MouseEvent up = d;
    up.kind = MouseEvent::Kind::Release;
    r = st.route(up, box);
    check(r.window == "a" && windows.handle(r.window, up) && st.captured().empty(), "…and the release, which ends the capture");
    check(c->got == std::vector<std::string>{"press 1,1", "drag -5,99", "release -5,99"},
          "the widget saw all three, in order, with the coordinates it was given");

    // It sizes itself, and it reports its own problem — both through the same paths a
    // built-in uses (the input is the only built-in that asks for a size).
    c->want_rows = 5;
    windows.prepare(st, box);
    const std::vector<ResolvedNode> v = st.resolve(box);
    check(by_id(v, "a")->outer.h == 5, "a registered kind SIZES its window, like the input (" + std::to_string(by_id(v, "a")->outer.h) + ")");
    c->broken = true;
    const WindowsReport pr = windows.prepare(st, box);
    check(pr.bad_values.size() == 3 && pr.summary().find("the canvas is broken") != std::string::npos,
          "…and its own problem() is reported like any built-in's — once per WINDOW showing it, plus the unregistered one [" +
              pr.summary() + "]");
    c->broken = false;

    // A layout RELOAD keeps the instance, for the reason it keeps a half-typed line: the
    // widget belongs to its content, not to the window that happened to show it.
    LayoutLoadReport lr2;
    const std::optional<Layout> other = load_layout_c(R"({"name":"paint2","root":{"column":[
        {"id":"z","content":"canvas:main","size":3,"focusable":true}]}})", lr2);
    WindowStack st2(*other);
    const int before = built;
    windows.prepare(st2, box);
    check(built == before && windows.at("z") == c, "a layout reload keeps the pixels: same instance under a new window id");
    clear_registered_widget_kinds_c();
  }

  // ---- the scrollbar's geometry (Phase 12 m5) ----------------------------------------
  // A pure function of (first, visible, total) and the track — a TABLE, because the
  // milestone's Done-when asks for one and because every interesting case here is a
  // boundary: the degenerate sizes this project insists on, and the two ends, where
  // rounding must not be allowed to answer "am I at the bottom?".
  {
    struct Case {
      const char* name;
      RolltuiScrollExtent e;
      int track;
      bool drawn;
      int offset, length;
    };
    const Case cases[] = {
        // nothing to scroll → NO BAR. A bar on a document that fits is a lie.
        {"content fits exactly", {0, 10, 10}, 8, false, 0, 0},
        {"content shorter than the viewport", {0, 10, 3}, 8, false, 0, 0},
        {"an empty document", {0, 10, 0}, 8, false, 0, 0},
        {"a zero-height viewport", {0, 0, 100}, 8, false, 0, 0},
        {"a zero-cell track", {0, 10, 100}, 0, false, 0, 0},
        {"a negative track", {0, 10, 100}, -3, false, 0, 0},
        // the ends TOUCH the ends, whatever the arithmetic rounds to
        {"at the top", {0, 10, 100}, 10, true, 0, 1},
        {"at the bottom", {90, 10, 100}, 10, true, 9, 1},
        {"one line from the bottom is NOT the bottom", {89, 10, 100}, 10, true, 8, 1},
        {"one line from the top is NOT the top", {1, 10, 100}, 10, true, 1, 1},
        // proportion
        {"half the document visible", {0, 50, 100}, 10, true, 0, 5},
        {"half, scrolled to the end", {50, 50, 100}, 10, true, 5, 5},
        {"half, scrolled halfway", {25, 50, 100}, 10, true, 3, 5},
        // a 1-cell track and a 1-cell thumb still say something
        {"a 1-cell track", {50, 10, 100}, 1, true, 0, 1},
        {"a huge document keeps a 1-cell thumb", {0, 1, 100000}, 20, true, 0, 1},
        // a `first` past the end is CLAMPED, not trusted — the window passes what the
        // pointer implies, and the pointer can be anywhere
        {"first past the end clamps to the bottom", {999, 10, 100}, 10, true, 9, 1},
    };
    for (const Case& c : cases) {
      RolltuiScrollThumb t{-1, -1};
      const bool drawn = rolltui_scroll_thumb(&c.e, c.track, &t) != 0;
      const bool ok = drawn == c.drawn && (!drawn || (t.offset == c.offset && t.length == c.length));
      check(ok, std::string("thumb: ") + c.name + " → " + (drawn ? std::to_string(t.offset) + "+" + std::to_string(t.length) : "no bar"));
      if (drawn) check(t.offset >= 0 && t.length >= 1 && t.offset + t.length <= c.track,
                       std::string("thumb: ") + c.name + " stays inside the track");
    }
    // The inverse round-trips at both ends, which is what makes a drag land where the
    // pointer is rather than one cell off.
    const RolltuiScrollExtent e{0, 10, 100};
    check(rolltui_scroll_first_for_cell(&e, 10, 0) == 0, "cell 0 of the track is the first line");
    check(rolltui_scroll_first_for_cell(&e, 10, 9) == 90, "the last cell is the last scroll position");
    check(rolltui_scroll_first_for_cell(&e, 10, -5) == 0 && rolltui_scroll_first_for_cell(&e, 10, 99) == 90,
          "a cell outside the track clamps rather than running off either end");
    const RolltuiScrollExtent fits{0, 10, 10};
    check(rolltui_scroll_first_for_cell(&fits, 10, 5) == 0, "…and a document that fits has one position: 0");

    // ---- the bar END TO END: the window draws it and drives the widget ----------------
    // The point of the milestone as the user re-scoped it: a widget OPTS IN, the window
    // owns the bar, and a widget that only REPORTS gets a bar that is not a handle.
    {
      Document doc;
      for (int i = 0; i < 200; ++i) {
        DocEntry e;
        e.id = "b" + std::to_string(i);
        e.text = "line " + std::to_string(i);
        e.markdown = false;
        doc.entries.push_back(e);
      }
      Windows windows;
      windows.bind_document("session", &doc);
      LayoutLoadReport lr;
      const std::optional<Layout> lay = load_layout_c(
          R"({"name":"bar","min_width":0,"min_height":0,"actions":{},"root":{"column":[
             {"id":"t","content":"transcript:session","border":"single","focusable":true}]}})", lr);
      check(lay && lr.clean(), "a one-window layout for the bar");
      WindowStack s(*lay);
      const Rect box{0, 0, 40, 12};
      windows.prepare(s, box);
      const Theme& th = *builtin_theme("default-dark");
      Frame f(40, 12);
      for (const ResolvedNode& rn : s.resolve(box)) windows.draw(rn, f, th);
      RolltuiTranscript* tr = windows.transcript("session");
      rolltui_transcript_scroll_to_top(tr);
      windows.prepare(s, box);
      for (const ResolvedNode& rn : s.resolve(box)) windows.draw(rn, f, th);
      check(rolltui_transcript_top_line(tr) == 0, "at the top");
      // The thumb is IN the right border column, which the widget never sees.
      const int track_x = 39;
      bool thumb_drawn = false, thumb_coloured = false;
      for (int y = 1; y < 11; ++y)
        if (f.glyph(track_x, y) == "\xE2\x96\x88") {
          thumb_drawn = true;
          // PHASE 17 m3, and the same gap as the title above: the thumb's GLYPH was asserted
          // and its colour was not, so `RolltuiWindowRoles::scrollbar` could name any role at
          // all and every suite stayed green. Pointing it at `error` turns this red.
          if (f.at(track_x, y).style.fg == th.style(Role::scrollbar).fg) thumb_coloured = true;
        }
      check(thumb_drawn, "the window drew a thumb in its right border column");
      check(thumb_coloured && th.style(Role::scrollbar).fg != th.style(Role::error).fg,
            "…painted with `scrollbar`, the role the window draws its own chrome with");
      // A press near the BOTTOM of the track scrolls the transcript — the window
      // commanding a widget that accepted scroll_to().
      MouseEvent m;
      m.kind = MouseEvent::Kind::Press;
      m.button = 1;
      m.x = track_x;
      m.y = 10;
      // Consumed by the WINDOW, not the text: the transcript would also return true for a
      // press (it starts a drag-select), so "handled" alone proves nothing — what
      // discriminates is that no selection began. Found by the negative control, which
      // passed this line while the bar was inert.
      check(windows.handle("t", m), "a press on the track is handled");
      check(!transcript_selection_active(tr), "…by the WINDOW: no drag-selection started, which is what a press on the text would do");
      windows.prepare(s, box);
      check(rolltui_transcript_top_line(tr) > 0, "…and it moved the transcript (" + std::to_string(rolltui_transcript_top_line(tr)) + ")");
      const std::size_t after_press = rolltui_transcript_top_line(tr);
      // A drag back up keeps driving it: the press captured the pointer.
      m.kind = MouseEvent::Kind::Drag;
      m.y = 1;
      check(windows.handle("t", m), "a drag on the thumb keeps being consumed");
      windows.prepare(s, box);
      check(rolltui_transcript_top_line(tr) < after_press, "…and dragging up scrolls up");
      m.kind = MouseEvent::Kind::Release;
      check(windows.handle("t", m), "the release ends the drag");
      // A press one column INSIDE the track is the text's, not the bar's.
      m.kind = MouseEvent::Kind::Press;
      m.x = track_x - 1;
      m.y = 5;
      const std::size_t before_text = rolltui_transcript_top_line(tr);
      windows.handle("t", m);
      windows.prepare(s, box);
      check(rolltui_transcript_top_line(tr) == before_text, "a press one column inside the track does not scroll: the bar owns ONE column");
    }
    // THE property, over a sweep rather than the two cases above — and stated with the
    // limit the sweep itself found: it holds WHEN THERE IS ROOM TO SAY. A thumb with
    // fewer than two cells of travel fills the track and touches both ends at once, and
    // no arithmetic fixes that: with one spare cell there is nowhere to put "nearly the
    // bottom". Writing the property without the guard would have been a claim the
    // geometry cannot keep, which is worse than the weaker true one.
    bool ends_exact = true, degenerate_seen = false;
    std::string first_bad;
    for (std::size_t total = 12; total <= 400; total += 7)
      for (int track = 3; track <= 24; ++track)
        for (std::size_t first = 0; first + 10 <= total; ++first) {
          RolltuiScrollThumb t;
          const RolltuiScrollExtent extent{first, 10, total};
          if (!rolltui_scroll_thumb(&extent, track, &t)) continue;
          if (track - t.length < 2) { degenerate_seen = true; continue; }
          const bool at_top = first == 0, at_bottom = first == total - 10;
          if ((t.offset == 0) != at_top || (t.offset + t.length == track) != at_bottom) {
            ends_exact = false;
            if (first_bad.empty())
              first_bad = " (first " + std::to_string(first) + "/" + std::to_string(total) + " on " +
                          std::to_string(track) + " cells → " + std::to_string(t.offset) + "+" + std::to_string(t.length) + ")";
          }
        }
    check(ends_exact, "wherever the thumb has 2+ cells of travel: it touches an end IF AND ONLY IF the view is at that end" + first_bad);
    check(degenerate_seen, "…and the sweep did reach the too-short case the guard excludes, so the guard is not hiding an empty set");
  }

  return report("rolltui layout_test");
}
