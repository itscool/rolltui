//
// layout_test.cpp — milestone 8: placement, the split tree, layout
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
//   7. CONTENT AND WIDGETS: the kind table and every way a content
//      string can be wrong, said by name; rolltui::Windows instantiating every kind,
//      drawing each of them, and
//      turning every failure (unknown kind, unbound source, unreadable file) into a
//      named report entry AND a visible error panel; the input sizing its own window;
//      one widget per content, kept across a layout reload.
//   8. MENUS FROM FILES: the three rungs in order (the user's directory,
//      the host's own embedded menus, the library's shipped ones) with the origin said;
//      a file dropped in after the fact opens with no rebuild and a changed one is
//      re-read; a missing menu and an unparsable one are named bad values drawn in the
//      window; an unknown key is reported without stopping the menu drawing.
//
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

// FINISHED. `rolltui/Layout.hpp` and `rolltui/Widgets.hpp` are gone from this
// file's includes — the two things a prior pass's comment here said "GENUINELY has no C
// form" were re-checked against the current headers and both had one:
//   - `rolltui::Layout`/`ActionDecl`'s OWN shape: `RolltuiLayout` IS `rolltui::Layout`
//     (`rolltui_layout.h`'s own header comment, "the same one-definition rule as
//     Node/Layer/Dim") — a plain C++ value type with real copy/move/destroy/`==` through its
//     members' own. `RolltuiLayoutAction` (`RolltuiStr name/description`) is the file
//     format's own element type now, and `rolltui_layout_shipped_default_actions()` is the
//     library's cached "default" screen's actions, so `action_decls()`'s conversion has no
//     counterpart to call: `RolltuiLayout::actions` (a `RolltuiActionList`) already IS the
//     flat `RolltuiLayoutAction*`+count `rolltui_bindings_declare` takes.
//   - `Windows`' typed accessors: `rolltui_windows_input`/`_transcript`/`_menu` (and their
// `_at` twins) are the library's OWN typed handles now — created on
//     demand, owned by `RolltuiWindows`, and the SAME object a window's widget draws through,
//     which is exactly the property the old comment said only `Windows`' C++ maps could give.
// Every remaining `rolltui::`-namespaced type below (`RolltuiLayoutNode`/`RolltuiLayer`/
// `RolltuiDim`/`RolltuiPlacement`/`RolltuiSplitSize`/`RolltuiResolvedNode`/`RolltuiContent`/
// `Border`/`Anchor`; `WidgetKind` was one previously retired it) is declared inside `rolltui/c/*.h`'s own `#ifdef __cplusplus`
// blocks — reachable from `rolltui/rolltui.h` alone, no C++ binding header needed. `Rect`,
// `RolltuiStyle`/`Color`, `Theme`, `Role`'s named enumerators, `Frame`, `Windows`/`WindowStack`
// (the class), `Key`, `RolltuiMouseEvent`'s short alias and `Route` WERE Layout.hpp/Widgets.hpp's own
// C++ convenience wrappers with no such in-header alias; this file now spells the bare C names
// (`RolltuiRect`, `RolltuiStyleColor`, a local `ThemeFixture`, `ROLLTUI_ROLE_*`, a `FrameC`/
// `WindowsC` RAII fixture, `ROLLTUI_KEY_*`, `RolltuiMouseEvent`, a local `RouteC`) — the same
// idiom `rolltui-paint` and `authored_screen_test.cpp` already use.
#include "rolltui/rolltui.h"
// INTERNAL: this test opts in. `rolltui_input_max_rows`/`_window_rows` are the input
// window's sizing rules, applied by `Windows`. They are internal because no consumer
// applies them, and this suite asserts the rule itself.
#include "rolltui/c/rolltui_widget_kinds.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_input.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_transcript.h"
#include "rolltui/c/rolltui_layout.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_presets.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_widgets.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

// The input's text as a view. `Windows::input()` hands back the library's own handle now
//, and the C's text accessor is pointer+length like every other borrow here.
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

std::string rect_str(RolltuiRect r) {
  return "{" + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h) + "}";
}

void expect_rect(const std::string& name, RolltuiRect got, RolltuiRect want) {
  check(got == want, name + ": " + rect_str(got) + (got == want ? "" : " (want " + rect_str(want) + ")"));
}

RolltuiPlacement P(RolltuiDim x, RolltuiDim y, RolltuiDim w, RolltuiDim h, Anchor a = Anchor::TopLeft, bool clamp = true) {
  RolltuiPlacement p;
  p.x = x; p.y = y; p.w = w; p.h = h; p.anchor = a; p.clamp = clamp;
  return p;
}

const RolltuiResolvedNode* by_id(const std::vector<RolltuiResolvedNode>& v, std::string_view id) {
  for (const RolltuiResolvedNode& rn : v)
    if (view_of(rn.node->id) == id) return &rn;
  return nullptr;
}

// `rolltui::Frame` (Screen.hpp) was a thin `unique_ptr<RolltuiFrame, Handle>` RAII wrapper
// with no C form of its own; this is that same wrapper, written here instead (rolltui.h rule
// 5: a consumer that wants RAII writes it in its own file). A single scratch is enough for a
// whole test binary — CLAUDE.md strategy 3, one handle reused rather than invented per call.
RolltuiDrawScratch* g_draw_scratch = rolltui_draw_scratch_new();
struct FrameC {
  RolltuiFrame* f;
  explicit FrameC(int w, int h, RolltuiStyle fill = {}) : f(rolltui_frame_new(w, h, fill)) {}
  FrameC(const FrameC& o) : f(rolltui_frame_clone(o.f)) {}
  FrameC(FrameC&& o) noexcept : f(o.f) { o.f = nullptr; }
  ~FrameC() { rolltui_frame_free(f); }
  operator RolltuiFrame*() const { return f; }
  RolltuiCell at(int x, int y) const {
    RolltuiCell c{};
    rolltui_frame_cell(f, x, y, &c);
    return c;
  }
  std::string_view glyph(int x, int y) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_glyph(f, x, y, &n);
    return {p, n};
  }
  int put_text(int x, int y, std::string_view utf8, RolltuiStyle style, int max_cells) {
    return rolltui_frame_put_text(f, g_draw_scratch, x, y, utf8.data(), utf8.size(), style, max_cells, 0, 0);
  }
};

std::string cell(const FrameC& f, int x, int y) { return std::string(f.glyph(x, y)); }

// `rolltui::Theme` (Theme.hpp) bundled a `std::array<Style, kRoleCount>` with an `EffectMap`
// and a name/meta — none of it ported (rolltui/c/rolltui_json.h's own header comment: the six
// modules still reading `json::Value` by real container ops, `Theme.cpp` among them, are out
// of this task's scope). This is that same bundle, as a fixture built from the pieces that ARE
// C: `rolltui_theme_builtin_fill` fills a caller's own style table and hands back the effect
// map it built.
struct ThemeFixture {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  ThemeFixture() = default;
  ThemeFixture(const ThemeFixture&) = delete;
  ~ThemeFixture() { rolltui_effect_map_free(effects); }
  const RolltuiStyle& style(unsigned char role) const { return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role); }
};
bool builtin_theme_c(std::string_view name, ThemeFixture& out) {
  out.effects = rolltui_theme_builtin_fill(name.data(), name.size(), out.styles, ROLLTUI_ROLE_COUNT);
  return out.effects != nullptr;
}

// `rolltui::Route` (Widgets.hpp) paired `rolltui_window_stack_route`'s two outputs (which of
// the four ROLLTUI_ROUTE_* outcomes, and the target window id) as one comparable value.
struct RouteC {
  unsigned char kind;
  std::string window;
  bool operator==(const RouteC& o) const { return kind == o.kind && window == o.window; }
  struct Kind {
    static constexpr unsigned char Deliver = ROLLTUI_ROUTE_DELIVER, ClosedPopup = ROLLTUI_ROUTE_CLOSED_POPUP,
                                    FocusMoved = ROLLTUI_ROUTE_FOCUS_MOVED, Dropped = ROLLTUI_ROUTE_DROPPED;
  };
};

// `rolltui::LayoutLoadReport` was a real C++ class (`clean()`, `std::vector<std::string>`
// fields); `RolltuiLayoutReport` is the transparent C struct those fields now come from
// directly (`.unknown_keys_n`/`.bad_values_n`/`.notes_n`, and `RolltuiStr` elements that
// already compare equal to a `string_view`/literal). This adds back only the two pieces of
// sugar every call site here still wants — a zeroing default constructor and `.clean()` — a
// SHAPE, never a rule: `rolltui_layout_report_clean` is the library's own, called through.
struct LayoutReport : RolltuiLayoutReport {
  LayoutReport() : RolltuiLayoutReport{} {}
  bool clean() const { return rolltui_layout_report_clean(this) != 0; }
};
// `for (const std::string& s : rep.bad_values)`-style loops need an index; this is the
// `RolltuiStr*`/`_n` pair as a tiny range so the loop body itself does not have to change.
struct StrSpan {
  const RolltuiStr* v;
  std::size_t n;
  const RolltuiStr* begin() const { return v; }
  const RolltuiStr* end() const { return v + n; }
  bool empty() const { return n == 0; }
  std::size_t size() const { return n; }
  const RolltuiStr& operator[](std::size_t i) const { return v[i]; }
};

// ---- direct C calls, once mirroring the rolltui::-namespaced free functions Layout.hpp/
// Layout.cpp defined, kept `_c`-suffixed now that the originals are gone too —
// renaming the call sites back to the plain names is left as pure churn, not a behaviour
// change, so this phase's diff stays about ownership rather than about spelling.

RolltuiRect place(const RolltuiPlacement& p, RolltuiRect parent) {
  RolltuiRect r;
  rolltui_placement_resolve(&p, parent, &r);
  return r;
}

std::optional<RolltuiDim> parse_dim_c(std::string_view text) {
  RolltuiDim d;
  if (!rolltui_parse_dim(text.data(), text.size(), &d)) return std::nullopt;
  return d;
}

std::string dim_to_string_c(RolltuiDim d) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_dim_to_string(d, buf, sizeof buf));
}

std::optional<RolltuiSplitSize> parse_split_size_c(std::string_view text) {
  RolltuiSplitSize s;
  if (!rolltui_parse_split_size(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

std::string split_size_to_string_c(RolltuiSplitSize s) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_split_size_to_string(s, buf, sizeof buf));
}

void push_node_c(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<RolltuiResolvedNode>*>(ctx)->push_back(*rn);
}

std::vector<RolltuiResolvedNode> resolve_tree_c(const RolltuiLayoutNode& root, RolltuiRect box, RolltuiRect screen, std::size_t layer = 0) {
  std::vector<RolltuiResolvedNode> out;
  rolltui_resolve_tree(&root, box, screen, layer, push_node_c, &out);
  return out;
}

std::string cell_c(const RolltuiFrame* f, int x, int y) {
  std::size_t n = 0;
  const char* p = rolltui_frame_glyph(f, x, y, &n);
  return std::string(p, n);
}

// These build a node tree through the plain C entry points (rolltui_layout_tree.h): rolltui_layout_node_init zeroes/defaults exactly as the
// struct's own defaults would, rolltui_node_list_add is the list's emplace_back, and
// rolltui_layout_node_copy fills the slot it returns.
RolltuiLayoutNode win(const char* content, RolltuiSplitSize size = {}, Border b = Border::Single, bool focusable = false) {
  RolltuiLayoutNode n;
  rolltui_layout_node_init(&n);
  n.kind = RolltuiLayoutNode::Kind::Window;
  n.id = content;
  n.content = content;
  n.size = size;
  n.border = b;
  n.focusable = focusable;
  return n;
}

// A batch of nodes MOVED into a container, so `row_of({win(...), win(...)})` still reads as it
// did while a node is no longer copyable by `=`: brace-init picks this variadic
// constructor, and each prvalue child is moved, never copied.
struct Nodes {
  std::vector<RolltuiLayoutNode> v;
  template <class... N>
  Nodes(N&&... kids) {  // NOLINT(google-explicit-constructor)
    (v.push_back(std::move(kids)), ...);
  }
};

RolltuiLayoutNode row_of(Nodes children, RolltuiSplitSize size = {}) {
  RolltuiLayoutNode n;
  rolltui_layout_node_init(&n);
  n.kind = RolltuiLayoutNode::Kind::Row;
  n.size = size;
  for (RolltuiLayoutNode& c : children.v) n.children.push_back(std::move(c));
  return n;
}

RolltuiLayoutNode column_of(Nodes children, RolltuiSplitSize size = {}) {
  RolltuiLayoutNode n = row_of(std::move(children), size);
  n.kind = RolltuiLayoutNode::Kind::Column;
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
// A kind's NAME is its identity, so each of these is one C call over a name; the rung comes
// back as `rolltui_widget_kind_resolve`'s return, and the row is where the kind's rules live
// whichever rung it came from.
//
// The widget-kind registry belongs to a CONTEXT, so this suite has one session that
// every shim below resolves against — `rolltui_test.hpp`'s, because the effects suite needed
// the same thing and a wrapper written twice means the helper belongs in one place (rule 5).
RolltuiContext* test_ctx() { return rolltui_test::test_context(); }

std::string_view widget_kind_name_c(std::size_t row) {
  std::size_t n = 0;
  const char* p = rolltui_widget_kind_name(test_ctx(), row, &n);
  return {p, n};
}
// The ROW a name resolves to (either rung), nullopt for neither; `*rung` says which.
std::optional<std::size_t> widget_kind_row_c(std::string_view name, int* rung = nullptr) {
  std::size_t row = 0;
  unsigned char rule = 0;
  const int r = rolltui_widget_kind_resolve(test_ctx(), name.data(), name.size(), &row, &rule, nullptr, nullptr);
  if (rung) *rung = r;
  if (r == ROLLTUI_KIND_UNKNOWN) return std::nullopt;
  return row;
}
std::optional<RolltuiContent> parse_content_c(std::string_view text, std::string* why = nullptr, unsigned char* what = nullptr) {
  unsigned char problem = ROLLTUI_CONTENT_PROBLEM_NONE;
  std::size_t row = 0;
  int is_host = 0;
  const char *name = nullptr, *source = nullptr;
  std::size_t name_len = 0, source_len = 0;
  Str why_str;
  const int ok = rolltui_content_parse(test_ctx(), text.data(), text.size(), &row, &is_host, &name, &name_len,
                                       &source, &source_len, &problem, &why_str);
  if (what) *what = problem;
  if (why) *why = str_of(why_str);
  if (!ok) return std::nullopt;
  RolltuiContent c;
  c.kind.assign(name, name_len);
  c.source.assign(source, source_len);
  return c;
}
std::string content_to_string_c(const RolltuiContent& c) {
  unsigned char rule = 0;
  rolltui_widget_kind_resolve(test_ctx(), c.kind.data(), c.kind.size(), nullptr, &rule, nullptr, nullptr);
  Str out;
  rolltui_content_format(c.kind.data(), c.kind.size(), c.source.data(), c.source.size(), rule, &out);
  return str_of(out);
}
bool register_widget_kind_c(std::string_view name, unsigned char rule, std::string_view source_is, std::string* why = nullptr) {
  const int refusal =
      rolltui_widget_kind_register(test_ctx(), name.data(), name.size(), rule, source_is.data(), source_is.size());
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
void clear_registered_widget_kinds_c() { rolltui_widget_kind_clear(test_ctx()); }
std::vector<std::string> widget_kind_names_c() {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < rolltui_widget_kind_count(test_ctx()); ++i) out.emplace_back(widget_kind_name_c(i));
  return out;
}

// ---- direct C calls, continued: the loader (load_layout/layout_to_json/builtin_layout*) ----
// `RolltuiLayoutReport`/`RolltuiLayout`/`RolltuiLayoutAction` ARE now the types
// this file holds throughout (rolltui_layout.h's own "one definition" section), so the
// bridging this block used to do (`report_from_c_c` copying a C report into a C++
// `LayoutLoadReport`, `actions_to_c_c` converting `ActionDecl`s) has nothing left to bridge —
// `rolltui_load_layout_text` and `rolltui_loaded_layout_to_layout` write directly into the
// caller's own report/layout now. `rolltui_layout_shipped_default_actions()` is the library's
// own cached "default" screen's actions, so there is no local copy of that table
// either. `report` is NOT reset here, matching `rolltui_load_layout[_text]`'s own convention
// (the caller starts one fresh per call — every call site below does).

std::optional<RolltuiLayout> load_layout_c(std::string_view json_text, RolltuiLayoutReport& report) {
  RolltuiLoadedLayout loaded;
  rolltui_loaded_layout_init(&loaded);
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(test_ctx(), &defaults_n);
  const int ok = rolltui_load_layout_text_into(json_text.data(), json_text.size(), &loaded, defaults, defaults_n,
                                          rolltui_layout_default_hooks(), &report);
  if (!ok) {
    rolltui_loaded_layout_release(&loaded);
    return std::nullopt;
  }
  RolltuiLayout out;
  rolltui_layout_init(&out);
  rolltui_loaded_layout_to_layout(&loaded, &out);
  rolltui_loaded_layout_release(&loaded);
  return out;
}
std::string layout_to_json_c(const RolltuiLayout& layout) {
  Str out;
  rolltui_layout_to_json_text(layout.name.data(), layout.name.size(), layout.min_width, layout.min_height,
                              layout.actions.data(), layout.actions.size(), &layout.base, layout.popups.data(),
                              layout.popups.size(), rolltui_layout_default_hooks(), &out);
  return str_of(out);
}
// The built-ins: the embedded table directly, parsed with load_layout_c. A fresh parse per
// lookup (this cache is the test's own, not Layout.cpp's `builtin_layout_cache()` — nothing
// exported reaches that one) rather than re-deriving its lazy-init subtleties for a path that
// only ever runs a handful of times per test binary.
const RolltuiLayout* builtin_layout_c(std::string_view name) {
  static const std::vector<std::pair<std::string, RolltuiLayout>> cache = [] {
    std::vector<std::pair<std::string, RolltuiLayout>> out;
    for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i) {
      const RolltuiEmbeddedFile& f = rolltui_kLayoutPresets[i];
      LayoutReport rep;
      std::optional<RolltuiLayout> l = load_layout_c(f.text, rep);
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

// ---- direct C calls, continued: the stack, used on its own (all sections) -------------

RolltuiEvent key_ev(unsigned char k, bool shift = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = k;
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
RolltuiEvent mouse_ev(const RolltuiMouseEvent& m) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse = m;
  return e;
}

// the LIBRARY's three, not a copy. This file had hand-written them, which made
// two consumers with the same table (`Layout.cpp` was the other) — rule 5's tell, fired before
// a single host had been converted.
const RolltuiStackActions& kStackActions_c = *rolltui_stack_default_actions();

RouteC route_c(RolltuiWindowStack* s, const RolltuiEvent& e, RolltuiRect screen) {
  Str window;
  const unsigned char kind = rolltui_window_stack_route(s, &e, screen, rolltui_bindings_default(test_ctx()), &kStackActions_c, &window);
  return {kind, str_of(window)};
}
std::string_view captured_c(const RolltuiWindowStack* s) {
  std::size_t n = 0;
  const char* p = rolltui_window_stack_captured(s, &n);
  return {p, n};
}
const RolltuiLayoutRoles& kRoles_c = *rolltui_layout_default_roles();  // likewise (m3)
using SlotRendererC = std::function<void(const RolltuiResolvedNode&, FrameC&)>;
void call_slot_c(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame*) {
  auto* p = static_cast<std::pair<const SlotRendererC*, FrameC*>*>(ctx);
  (*p->first)(*rn, *p->second);
}
void compose_c(RolltuiWindowStack* s, FrameC& frame, RolltuiRect screen, const ThemeFixture& theme, const SlotRendererC& render) {
  std::pair<const SlotRendererC*, FrameC*> ctx{&render, &frame};
  RolltuiComposeScratch* scratch = rolltui_compose_scratch_new();
  rolltui_window_stack_compose(s, frame, screen, theme.styles, &kRoles_c, render ? call_slot_c : nullptr, &ctx, false,
                               scratch);
  rolltui_compose_scratch_free(scratch);
}
std::vector<RolltuiResolvedNode> resolve_stack_c(const RolltuiWindowStack* s, RolltuiRect screen) {
  std::vector<RolltuiResolvedNode> out;
  rolltui_window_stack_resolve(s, screen, push_node_c, &out);
  return out;
}
// The handful of stack methods keyed by a string id, spelled out once so a call site reads
// `focus_c(s.s, "input")` rather than repeating `.data(), .size()` at every one.
void focus_c(RolltuiWindowStack* s, std::string_view id) { rolltui_window_stack_focus(s, id.data(), id.size()); }
bool has_popup_c(const RolltuiWindowStack* s, std::string_view id) {
  return rolltui_window_stack_has_popup(s, id.data(), id.size()) != 0;
}
RolltuiLayoutNode* find_c(RolltuiWindowStack* s, std::string_view id) { return rolltui_window_stack_find(s, id.data(), id.size()); }

// `rolltui::WindowStack` (Widgets.hpp) was a thin class wrapping exactly these same `_c`
// calls plus the handle's lifetime; this IS that wrapper, with the free functions above doing
// the work. `.s`/an implicit `RolltuiWindowStack*` conversion cover the sites that used to
// reach `Windows::prepare(stack, box)` etc. by reference.
struct StackC {
  RolltuiWindowStack* s = rolltui_window_stack_new();
  StackC() = default;
  explicit StackC(const RolltuiLayout& l) { rolltui_window_stack_set_base(s, &l.base); }
  StackC(const StackC&) = delete;
  StackC& operator=(const StackC&) = delete;
  ~StackC() { rolltui_window_stack_free(s); }
  operator RolltuiWindowStack*() const { return s; }

  void set_base(const RolltuiLayer& l) { rolltui_window_stack_set_base(s, &l); }
  std::vector<RolltuiResolvedNode> resolve(RolltuiRect screen) const { return resolve_stack_c(s, screen); }
  void compose(FrameC& f, RolltuiRect screen, const ThemeFixture& theme, const SlotRendererC& render) {
    compose_c(s, f, screen, theme, render);
  }
  RouteC route(const RolltuiMouseEvent& m, RolltuiRect screen) { return route_c(s, mouse_ev(m), screen); }
  RouteC route(const RolltuiEvent& e, RolltuiRect screen) { return route_c(s, e, screen); }
  std::string_view captured() const { return captured_c(s); }
  RolltuiLayoutNode* find(std::string_view id) const { return find_c(s, id); }
};

// ---- direct C calls, continued: RolltuiLayout::actions vs. the shipped default's ------------
// `rolltui::shipped_default_actions_c()` returned a `std::vector<ActionDecl>`; the library's own
// cached copy is `rolltui_layout_shipped_default_actions()` now, and
// `RolltuiLayout::actions` (a `RolltuiActionList`) is already the flat array/count pair that
// and `rolltui_bindings_declare` both take — there is no `action_decls()` conversion left to
// call (see the include-comment note at the top of the file).
struct ShippedActionsC {
  const RolltuiLayoutAction* v;
  std::size_t n;
  std::size_t size() const { return n; }
};
ShippedActionsC shipped_default_actions_c() {
  std::size_t n = 0;
  const RolltuiLayoutAction* v = rolltui_layout_shipped_default_actions(test_ctx(), &n);
  return {v, n};
}
bool operator==(const RolltuiActionList& a, const ShippedActionsC& b) {
  if (a.size() != b.n) return false;
  for (std::size_t i = 0; i < b.n; ++i)
    if (!(a[i].name == b.v[i].name) || !(a[i].description == b.v[i].description)) return false;
  return true;
}

// ---- direct C calls, continued: Bindings (used in sections 8/9) ----------------------------
// `rolltui::Bindings` (Bindings.hpp) was `unique_ptr<RolltuiBindings, Handle>` plus methods
// that are each one call below; this is that wrapper, written here (rolltui.h rule 5).
struct BindingsC {
  RolltuiBindings* b;
  explicit BindingsC(RolltuiBindings* p) : b(p) {}
  BindingsC(const BindingsC& o) : b(rolltui_bindings_clone(o.b)) {}
  BindingsC(BindingsC&& o) noexcept : b(o.b) { o.b = nullptr; }
  BindingsC& operator=(BindingsC&& o) noexcept {
    if (this != &o) {
      rolltui_bindings_free(b);
      b = o.b;
      o.b = nullptr;
    }
    return *this;
  }
  ~BindingsC() { rolltui_bindings_free(b); }
  operator RolltuiBindings*() const { return b; }
  bool has(std::string_view action) const { return rolltui_bindings_has(b, action.data(), action.size()) != 0; }
  bool bind(std::string_view action, const RolltuiChord& c) {
    return rolltui_bindings_bind(b, action.data(), action.size(), &c, nullptr, nullptr) != 0;
  }
  std::string action_for(const RolltuiChord& k, std::string_view scope) const {
    std::size_t n = 0;
    const char* p = rolltui_bindings_action_for(b, &k, scope.data(), scope.size(), &n);
    return p ? std::string(p, n) : std::string();
  }
  void declare(std::vector<std::pair<std::string, std::string>> actions) {
    std::vector<RolltuiLayoutAction> v;
    for (auto& [n, d] : actions) v.push_back(RolltuiLayoutAction{Str(n.c_str()), Str(d.c_str())});
    rolltui_bindings_declare(b, v.data(), v.size(), nullptr, 0);
  }
  void declare(const RolltuiActionList& actions) { rolltui_bindings_declare(b, actions.data(), actions.size(), nullptr, 0); }
  std::size_t chords_for_count(std::string_view action) const { return rolltui_bindings_chord_count(b, action.data(), action.size()); }
};
BindingsC default_bindings_c() { return BindingsC(rolltui_bindings_clone(rolltui_bindings_default(test_ctx()))); }
std::optional<RolltuiChord> parse_chord_c(std::string_view s) {
  RolltuiChord c{};
  if (rolltui_chord_parse(s.data(), s.size(), &c)) return c;
  return std::nullopt;
}
// `rolltui::BindingsLoadReport`: the FILE FORMAT's own report (rolltui_bindings.h), distinct
// from the preset-domain reports in Presets.hpp/rolltui_presets.h — this suite never opens a
// bindings PRESET, only a bindings FILE, so this is the report that type actually needs.
struct BindingsFileReport : RolltuiBindingsReport {
  BindingsFileReport() : RolltuiBindingsReport{} {}
  bool clean() const { return rolltui_bindings_report_clean(this) != 0; }
};
std::optional<BindingsC> bindings_from_json_c(std::string_view text, BindingsFileReport& report) {
  RolltuiBindings* b = rolltui_bindings_new_seeded();
  // reason NULL: the same posture `rolltui_bindings_default(test_ctx())` itself takes (rolltui_bindings.c's
  // own comment on that call) — this suite's fixtures name no undeliverable chord.
  const int ok = rolltui_bindings_load_json(b, text.data(), text.size(), rolltui_key_active_protocol(),
                                            rolltui_bindings_library_scope, nullptr, nullptr, nullptr, &report);
  if (!ok) {
    rolltui_bindings_free(b);
    return std::nullopt;
  }
  return BindingsC(b);
}

// ---- direct C calls, continued: the shipped menus, and a menu file's own report ------------
std::string shipped_menu_c(std::string_view name) {
  for (std::size_t i = 0; i < rolltui_kMenuCount; ++i)
    if (std::string_view(rolltui_kMenus[i].name) == name) return rolltui_kMenus[i].text;
  return "";
}

// ---- the window host: `rolltui::Windows` was a thin class over exactly the calls below -----
// (see the include-comment note: its ONE genuinely missing piece, `menu_names()`'s three-rung
// union, has no single C call and is reconstructed here from primitives that all exist —
// `rolltui_preset_json_names_in` for the user's directory, `rolltui_windows_host_menu_*` for
// the host's own, `rolltui_kMenus` for the shipped ones.)
struct WindowsReportC {
  std::vector<std::string> bad_values;
  std::string summary_text;
  bool clean() const { return bad_values.empty(); }
  const std::string& summary() const { return summary_text; }
};
WindowsReportC windows_prepare_c(RolltuiWindows* w, RolltuiWindowStack* s, RolltuiRect box) {
  rolltui_windows_sync(w, s);
  rolltui_windows_autosize(w, s, box);
  rolltui_windows_layout(w, s, box);
  WindowsReportC out;
  for (std::size_t i = 0; i < rolltui_windows_report_count(w); ++i) {
    std::size_t n = 0;
    const char* p = rolltui_windows_report_at(w, i, &n);
    out.bad_values.emplace_back(p, n);
  }
  RolltuiStr s2{};
  rolltui_windows_report_summary(w, &s2);
  out.summary_text.assign(s2.p ? s2.p : "", s2.n);
  rolltui_str_free(&s2);
  return out;
}

struct WindowsC {
  RolltuiWindows* w = rolltui_windows_new(test_ctx());
  // A GENUINE C API GAP found by this conversion (see this task's final report): the `help`
  // kind's `layout()` calls `rolltui_bindings_action_count`/friends on whatever
  // `rolltui_windows_bindings(w)` returns with NO NULL check, and that pointer is NULL until
  // a host calls `set_env`+`set_bindings` — so a bare `rolltui_windows_new()` crashes the
  // moment `prepare()` sizes a `help` window, before any test gets to choose its own table.
  // `rolltui::Windows`'s C++ constructor evidently defaulted to a live table for exactly this
  // reason; this restores that default (the library's own shipped one) rather than requiring
  // every block below that shows a `help` window to remember to call `set_bindings` first.
  // ONE LINE, which is the point: `set_library_defaults` installs the live table too, so a
  // pure-C window table is usable after it and is never one call short of it.
  WindowsC() { rolltui_context_set_library_defaults(test_ctx()); }
  WindowsC(const WindowsC&) = delete;
  ~WindowsC() { rolltui_windows_free(w); }
  operator RolltuiWindows*() const { return w; }

  void set_dir(std::string_view dir) { rolltui_context_set_dir(test_ctx(), dir.data(), dir.size()); }
  void bind_document(std::string_view name, const RolltuiDocument* doc) {
    rolltui_windows_bind_document(w, name.data(), name.size(), doc);
  }
  void bind_rows(std::string_view name, RolltuiRowsFn fn, void* ctx = nullptr, void (*free_ctx)(void*) = nullptr) {
    rolltui_windows_bind_rows(w, name.data(), name.size(), fn, ctx, free_ctx);
  }
  void bind_submit(std::string_view name, RolltuiSubmitFn fn, void* ctx = nullptr, void (*free_ctx)(void*) = nullptr,
                   int on_submit = 0) {
    rolltui_windows_bind_submit(w, name.data(), name.size(), fn, ctx, free_ctx, on_submit);
  }
  void bind_note(std::string_view name, RolltuiNoteFn fn, void* ctx = nullptr, void (*free_ctx)(void*) = nullptr) {
    rolltui_windows_bind_note(w, name.data(), name.size(), fn, ctx, free_ctx);
  }
  void set_help(std::string_view lead, std::initializer_list<std::string_view> scopes, std::string_view note) {
    rolltui_context_set_help(test_ctx(), lead.data(), lead.size(), note.data(), note.size());
    rolltui_context_clear_help_scopes(test_ctx());
    for (std::string_view sc : scopes) rolltui_context_add_help_scope(test_ctx(), sc.data(), sc.size());
  }
  bool register_kind(std::string_view name, RolltuiWidgetFactory factory, void* ctx,
                     unsigned char rule = ROLLTUI_SOURCE_REQUIRED, std::string_view source_is = "",
                     std::string* why = nullptr) {
    if (!register_widget_kind_c(name, rule, source_is, why)) return false;
    rolltui_context_register_kind(test_ctx(), name.data(), name.size(), factory, ctx, nullptr);
    return true;
  }
  void set_env(const RolltuiWidgetEnv& env) { rolltui_context_set_env(test_ctx(), &env); }
  void set_bindings(const RolltuiBindings* b) { rolltui_context_set_bindings(test_ctx(), b); }
  RolltuiWindows* handle() const { return w; }
  RolltuiTranscript* transcript(std::string_view source) const { return rolltui_windows_transcript(w, source.data(), source.size()); }
  RolltuiTranscript* transcript_at(std::string_view win) const { return rolltui_windows_transcript_at(w, win.data(), win.size()); }
  RolltuiInput* input(std::string_view source) const { return rolltui_windows_input(w, source.data(), source.size()); }
  RolltuiInput* input_at(std::string_view win) const { return rolltui_windows_input_at(w, win.data(), win.size()); }
  RolltuiMenu* menu(std::string_view source) const { return rolltui_windows_menu(w, source.data(), source.size()); }
  RolltuiMenu* menu_at(std::string_view win) const { return rolltui_windows_menu_at(w, win.data(), win.size()); }
  RolltuiWidget* at(std::string_view win) const { return rolltui_windows_at(w, win.data(), win.size()); }
  RolltuiWidget* registered(std::string_view kind, std::string_view source = "") const {
    // A NAME that is not a rung-2 (host-registered) kind at all is nullptr, never the
    // "unknown kind" error widget `widget_for` would build for it — `content_at`'s own
    // reported-by-name path is the place that error belongs, not this typed accessor.
    unsigned char rule = 0;
    if (rolltui_widget_kind_resolve(test_ctx(), kind.data(), kind.size(), nullptr, &rule, nullptr, nullptr) !=
        ROLLTUI_KIND_HOST)
      return nullptr;
    Str content;
    rolltui_content_format(kind.data(), kind.size(), source.data(), source.size(), rule, &content);
    return rolltui_windows_widget_for(w, content.data(), content.size());
  }
  std::optional<std::string> content_at(std::string_view win) const {
    std::size_t n = 0;
    const char* p = rolltui_windows_content_at(w, win.data(), win.size(), &n);
    if (!p) return std::nullopt;
    return std::string(p, n);
  }
  bool handle(std::string_view win, const RolltuiEvent& e) { return rolltui_windows_handle(w, win.data(), win.size(), &e) != 0; }
  bool handle(std::string_view win, const RolltuiMouseEvent& m) {
    const RolltuiEvent e = mouse_ev(m);
    return handle(win, e);
  }
  void draw(const RolltuiResolvedNode& rn, FrameC& f, const ThemeFixture& theme) {
    rolltui_windows_draw(w, &rn, f, theme.styles, rolltui_windows_default_roles());
  }
  void add_menu(std::string_view name, std::string_view json) {
    rolltui_context_add_menu(test_ctx(), name.data(), name.size(), json.data(), json.size());
  }
  std::string menu_origin(std::string_view source) const {
    std::size_t n = 0;
    const char* p = rolltui_windows_menu_origin(w, source.data(), source.size(), &n);
    return p ? std::string(p, n) : std::string();
  }
  std::vector<std::string> menu_names() const {
    std::vector<std::string> out;
    std::size_t n = 0;
    const char* d = rolltui_windows_dir(w, &n);
    if (d && n) {
      const std::string menus_dir = std::string(d, n) + "/menus";
      RolltuiStrList names;
      rolltui_preset_json_names_in(menus_dir.data(), menus_dir.size(), &names);
      for (const RolltuiStr& s : names) out.push_back(str_of(s));
    }
    for (std::size_t i = 0; i < rolltui_windows_host_menu_count(w); ++i) {
      std::size_t hn = 0;
      const char* hp = rolltui_windows_host_menu_name_at(w, i, &hn);
      out.emplace_back(hp, hn);
    }
    for (std::size_t i = 0; i < rolltui_kMenuCount; ++i) out.emplace_back(rolltui_kMenus[i].name);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  }
  WindowsReportC prepare(RolltuiWindowStack* s, RolltuiRect box) { return windows_prepare_c(w, s, box); }
};

// ---- a REGISTERED kind, draw-only (section 7): `rolltui::CallbackWidget` (Widgets.hpp) was a
// `Widget` override that ran a stored `std::function` from its `draw()`; the vtable plugin
// here is that same one-slot-filled shape (rolltui.h's own worked example is paint.cpp's
// `Canvas`, which fills more slots for a widget that also handles input — this one only draws).
struct MineCtx {
  int* drew_own;      // BORROWED: the test's own counter
  const ThemeFixture* theme;  // BORROWED: for the role its "mine!" text draws with
};
void mine_destroy(void* ctx) { delete static_cast<MineCtx*>(ctx); }
void mine_layout(void*, const RolltuiResolvedNode*) {}
void mine_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  MineCtx* m = static_cast<MineCtx*>(ctx);
  ++*m->drew_own;
  rolltui_frame_put_text(f, g_draw_scratch, rn->inner.x, rn->inner.y, "mine!", 5, m->theme->style(ROLLTUI_ROLE_TEXT),
                         rn->inner.w, 0, 0);
}
constexpr RolltuiWidgetPlugin kMinePlugin = {
    /*destroy=*/mine_destroy, /*layout=*/mine_layout, /*draw=*/mine_draw,
    /*problem=*/nullptr,      /*note_at=*/nullptr,    /*desired_outer=*/nullptr,
    /*handle=*/nullptr,       /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr,
};
RolltuiWidget mine_factory(void* ctx, RolltuiWindows*, const char*, std::size_t) {
  return RolltuiWidget{&kMinePlugin, new MineCtx(*static_cast<const MineCtx*>(ctx))};
}

// ---- a REGISTERED kind, full plugin (section 9): `rolltui::Widget` (Widgets.hpp) was a base
// class with nine virtuals; the vtable below fills five of the nine slots (destroy/layout/draw
// required, problem/desired_outer/handle optional — `note_at`/`scroll_extent`/`scroll_to` stay
// NULL, matching the original's un-overridden defaults). Same shape as `rolltui-paint`'s own
// `Canvas`, plus the two slots this test's own coverage needs that paint's does not.
struct Canvas {
  std::string source;       // set at factory time from the content string, like paint.cpp's
  RolltuiWindows* windows;  // BORROWED: where `draw` asks for the frame's style table
  std::vector<std::string> got;  // "press 3,4", "drag -5,99", "release 1,1"
  bool broken = false;
  int want_rows = 0;
};
void canvas_destroy(void* ctx) { delete static_cast<Canvas*>(ctx); }
void canvas_layout(void*, const RolltuiResolvedNode*) {}
void canvas_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Canvas* c = static_cast<Canvas*>(ctx);
  const RolltuiStyle* styles = rolltui_windows_styles(c->windows);
  const RolltuiStyle ink = *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_TEXT);
  const std::string text = "canvas " + c->source;
  rolltui_frame_put_text(f, g_draw_scratch, rn->inner.x, rn->inner.y, text.data(), text.size(), ink, rn->inner.w, 0, 0);
}
int canvas_problem(void* ctx, RolltuiStr* out) {
  Canvas* c = static_cast<Canvas*>(ctx);
  if (!c->broken) return 0;
  static const char kMsg[] = "the canvas is broken";
  rolltui_str_set(out, kMsg, sizeof(kMsg) - 1);
  return 1;
}
int canvas_desired_outer(void* ctx, int, int, int, int* out) {
  Canvas* c = static_cast<Canvas*>(ctx);
  if (c->want_rows <= 0) return 0;
  *out = c->want_rows;
  return 1;
}
int canvas_handle(void* ctx, const RolltuiEvent* e) {
  Canvas* c = static_cast<Canvas*>(ctx);
  if (e->kind != ROLLTUI_EVENT_MOUSE) return 0;
  using K = RolltuiMouseEvent::Kind;
  const K k = e->mouse.kind;
  if (k != K::Press && k != K::Drag && k != K::Release) return 0;
  const char* kind_name = k == K::Press ? "press" : (k == K::Drag ? "drag" : "release");
  c->got.push_back(std::string(kind_name) + " " + std::to_string(e->mouse.x) + "," + std::to_string(e->mouse.y));
  return 1;
}
constexpr RolltuiWidgetPlugin kCanvasPlugin = {
    /*destroy=*/canvas_destroy,   /*layout=*/canvas_layout, /*draw=*/canvas_draw,
    /*problem=*/canvas_problem,   /*note_at=*/nullptr,      /*desired_outer=*/canvas_desired_outer,
    /*handle=*/canvas_handle,     /*scroll_extent=*/nullptr, /*scroll_to=*/nullptr,
};
struct CanvasFactoryCtx {
  RolltuiWindows* windows;
  int* built;  // BORROWED: bumped once per real construction
};
RolltuiWidget canvas_factory(void* ctx, RolltuiWindows* /*w*/, const char* content, size_t len) {
  const CanvasFactoryCtx* fc = static_cast<const CanvasFactoryCtx*>(ctx);
  const char* source = nullptr;
  std::size_t source_len = 0;
  RolltuiStr why{};
  unsigned char problem = 0;
  if (!rolltui_content_parse(test_ctx(), content, len, nullptr, nullptr, nullptr, nullptr, &source, &source_len,
                             &problem, &why))
    return RolltuiWidget{};
  ++*fc->built;
  Canvas* c = new Canvas{std::string(source, source_len), fc->windows, {}, false, 0};
  return RolltuiWidget{&kCanvasPlugin, c};
}

}  // namespace

int main() {
  const RolltuiRect scr{0, 0, 80, 24};
  const RolltuiDim A0 = RolltuiDim::abs(0);

  // ---- 1. resolve(): the table -----------------------------------------------------------
  std::printf("-- resolve table\n");
  expect_rect("all absolute", place(P(RolltuiDim::abs(2), RolltuiDim::abs(3), RolltuiDim::abs(10), RolltuiDim::abs(5)), scr), {2, 3, 10, 5});
  expect_rect("rel(1) fills 81x25", place(P(A0, A0, RolltuiDim::rel(1), RolltuiDim::rel(1)), {0, 0, 81, 25}), {0, 0, 81, 25});
  expect_rect("rel(1) fills 1x1", place(P(A0, A0, RolltuiDim::rel(1), RolltuiDim::rel(1)), {0, 0, 1, 1}), {0, 0, 1, 1});
  expect_rect("rel(0.5) width floors at 81", place(P(A0, A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), {0, 0, 81, 25}), {0, 0, 40, 25});
  expect_rect("second half starts at 40 and takes the remainder at 81",
              place(P(RolltuiDim::rel(0.5), A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), {0, 0, 81, 25}), {40, 0, 41, 25});
  expect_rect("halves at width 7: first", place(P(A0, A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), {0, 0, 7, 1}), {0, 0, 3, 1});
  expect_rect("halves at width 7: second", place(P(RolltuiDim::rel(0.5), A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), {0, 0, 7, 1}), {3, 0, 4, 1});
  expect_rect("thirds at 10: first", place(P(A0, A0, RolltuiDim::rel(1.0 / 3), RolltuiDim::rel(1)), {0, 0, 10, 1}), {0, 0, 3, 1});
  expect_rect("thirds at 10: second", place(P(RolltuiDim::rel(1.0 / 3), A0, RolltuiDim::rel(1.0 / 3), RolltuiDim::rel(1)), {0, 0, 10, 1}), {3, 0, 3, 1});
  expect_rect("thirds at 10: third fills to the edge (1e-6 tolerance)",
              place(P(RolltuiDim::rel(2.0 / 3), A0, RolltuiDim::rel(1.0 / 3), RolltuiDim::rel(1)), {0, 0, 10, 1}), {6, 0, 4, 1});
  expect_rect("rel(1, -32): the rest minus the panel", place(P(A0, A0, RolltuiDim::rel(1, -32), RolltuiDim::rel(1, -3)), scr), {0, 0, 48, 21});
  expect_rect("panel at rel(1, -32) with abs 32", place(P(RolltuiDim::rel(1, -32), A0, RolltuiDim::abs(32), RolltuiDim::rel(1)), scr), {48, 0, 32, 24});
  expect_rect("input at y rel(1, -3) h 3 tiles with h rel(1, -3)", place(P(A0, RolltuiDim::rel(1, -3), RolltuiDim::rel(1), RolltuiDim::abs(3)), scr), {0, 21, 80, 3});
  expect_rect("negative size clamps to 0", place(P(A0, A0, RolltuiDim::rel(1, -32), RolltuiDim::rel(1)), {0, 0, 20, 5}), {0, 0, 0, 5});
  {
    RolltuiPlacement p = P(A0, A0, RolltuiDim::rel(1, -32), RolltuiDim::rel(1));
    p.min_w = RolltuiDim::abs(10);
    expect_rect("min_w lifts a negative size", place(p, {0, 0, 20, 5}), {0, 0, 10, 5});
  }
  expect_rect("center anchor, even size", place(P(RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(20), RolltuiDim::abs(10), Anchor::Center), scr), {30, 7, 20, 10});
  expect_rect("center anchor at 81x25 lands on the same cell", place(P(RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(20), RolltuiDim::abs(10), Anchor::Center), {0, 0, 81, 25}), {30, 7, 20, 10});
  expect_rect("center anchor, odd size (integer half)", place(P(RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(21), RolltuiDim::abs(11), Anchor::Center), scr), {30, 7, 21, 11});
  expect_rect("bottom-right anchor", place(P(RolltuiDim::rel(1), RolltuiDim::rel(1), RolltuiDim::abs(10), RolltuiDim::abs(3), Anchor::BottomRight), scr), {70, 21, 10, 3});
  expect_rect("right anchor with a rel width floors (no edge rule)", place(P(RolltuiDim::rel(1), A0, RolltuiDim::rel(0.25), RolltuiDim::rel(1), Anchor::Right), {0, 0, 81, 1}), {61, 0, 20, 1});
  expect_rect("right anchor at x rel(0.5) w rel(0.5) sizes 40 at 81", place(P(RolltuiDim::rel(0.5), A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1), Anchor::Right), {0, 0, 81, 1}), {0, 0, 40, 1});
  expect_rect("top anchor centres horizontally only", place(P(RolltuiDim::rel(0.5), RolltuiDim::abs(2), RolltuiDim::abs(10), RolltuiDim::abs(4), Anchor::Top), scr), {35, 2, 10, 4});
  expect_rect("clamp moves an overflowing x back", place(P(RolltuiDim::abs(75), A0, RolltuiDim::abs(10), RolltuiDim::abs(2)), scr), {70, 0, 10, 2});
  expect_rect("clamp off leaves the overflow", place(P(RolltuiDim::abs(75), A0, RolltuiDim::abs(10), RolltuiDim::abs(2), Anchor::TopLeft, false), scr), {75, 0, 10, 2});
  expect_rect("clamp shrinks an oversize width to the parent", place(P(A0, A0, RolltuiDim::abs(100), RolltuiDim::abs(2)), scr), {0, 0, 80, 2});
  expect_rect("clamp lifts a negative x", place(P(RolltuiDim::abs(-5), RolltuiDim::abs(-1), RolltuiDim::abs(10), RolltuiDim::abs(2)), scr), {0, 0, 10, 2});
  expect_rect("clamp off keeps a negative x", place(P(RolltuiDim::abs(-5), A0, RolltuiDim::abs(10), RolltuiDim::abs(2), Anchor::TopLeft, false), scr), {-5, 0, 10, 2});
  {
    RolltuiPlacement p = P(A0, A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1));
    p.max_w = RolltuiDim::abs(30);
    expect_rect("max_w caps", place(p, scr), {0, 0, 30, 24});
    p.min_w = RolltuiDim::abs(50);
    expect_rect("min wins over max", place(p, scr), {0, 0, 50, 24});
    RolltuiPlacement q = P(A0, A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1));
    q.max_w = RolltuiDim::rel(0.25);
    expect_rect("max in rel units", place(q, scr), {0, 0, 20, 24});
  }
  expect_rect("parent offset is added", place(P(RolltuiDim::rel(0.5), A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), {10, 5, 60, 10}), {40, 5, 30, 10});
  expect_rect("zero parent: rel → empty", place(P(A0, A0, RolltuiDim::rel(1), RolltuiDim::rel(1)), {0, 0, 0, 0}), {0, 0, 0, 0});
  expect_rect("zero parent: abs clamps to empty", place(P(A0, A0, RolltuiDim::abs(5), RolltuiDim::abs(5)), {3, 3, 0, 0}), {3, 3, 0, 0});
  expect_rect("centred popup wider than a small parent clamps to it", place(P(RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(30), RolltuiDim::abs(30), Anchor::Center), {0, 0, 20, 10}), {0, 0, 20, 10});
  {
    // The help popup as every built-in declares it: mixed units, centred, min/max.
    RolltuiPlacement p = P(RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::rel(0.6), RolltuiDim::abs(12), Anchor::Center);
    p.min_w = RolltuiDim::abs(24);
    p.max_w = RolltuiDim::abs(72);
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
      RolltuiRect par{0, 0, W, 1};
      RolltuiRect a = place(P(A0, A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), par);
      RolltuiRect b = place(P(RolltuiDim::rel(0.5), A0, RolltuiDim::rel(0.5), RolltuiDim::rel(1)), par);
      if (!(a.x == 0 && b.x == a.w && a.w + b.w == W)) ++bad_halves;
      int pos = 0;
      for (int q = 0; q < 4; ++q) {
        RolltuiRect r = place(P(RolltuiDim::rel(q * 0.25), A0, RolltuiDim::rel(0.25), RolltuiDim::rel(1)), par);
        if (r.x != pos) ++bad_quarters;
        pos += r.w;
      }
      if (pos != W) ++bad_quarters;
      if (place(P(A0, A0, RolltuiDim::rel(1), RolltuiDim::rel(1)), par).w != W) ++bad_fill;
      for (double f : fs) {
        RolltuiRect r = place(P(RolltuiDim::rel(0.5), A0, RolltuiDim::rel(f), RolltuiDim::rel(1)), par);
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
  check(parse_dim_c("50%") == RolltuiDim::rel(0.5), "parse_dim 50%");
  check(parse_dim_c("100% - 32") == RolltuiDim::rel(1, -32), "parse_dim '100% - 32'");
  check(parse_dim_c("100%-32") == RolltuiDim::rel(1, -32), "parse_dim '100%-32'");
  check(parse_dim_c("25% + 2") == RolltuiDim::rel(0.25, 2), "parse_dim '25% + 2'");
  check(parse_dim_c(" 0% ") == RolltuiDim::rel(0), "parse_dim ' 0% '");
  check(parse_dim_c("12.5%") == RolltuiDim::rel(0.125), "parse_dim 12.5%");
  check(!parse_dim_c("32"), "parse_dim rejects a bare number (cells are a JSON number, not a string)");
  check(!parse_dim_c("abc"), "parse_dim rejects 'abc'");
  check(!parse_dim_c("50% * 2"), "parse_dim rejects an unknown operator");
  check(!parse_dim_c("%"), "parse_dim rejects '%'");
  check(dim_to_string_c(RolltuiDim::abs(32)) == "32", "dim_to_string abs");
  check(dim_to_string_c(RolltuiDim::rel(0.5)) == "50%", "dim_to_string 50%");
  check(dim_to_string_c(RolltuiDim::rel(1, -32)) == "100% - 32", "dim_to_string '100% - 32'");
  check(dim_to_string_c(RolltuiDim::rel(0.25, 2)) == "25% + 2", "dim_to_string '25% + 2'");
  check(parse_split_size_c("fill") == RolltuiSplitSize::filling(1), "parse_split_size fill");
  check(parse_split_size_c("fill 3") == RolltuiSplitSize::filling(3), "parse_split_size 'fill 3'");
  check(parse_split_size_c("40%") == RolltuiSplitSize::fixed(RolltuiDim::rel(0.4)), "parse_split_size 40%");
  check(!parse_split_size_c("fill 0"), "parse_split_size rejects a zero weight");
  check(!parse_split_size_c("3"), "parse_split_size rejects a bare number string");
  check(split_size_to_string_c(RolltuiSplitSize::filling(2)) == "fill 2" && split_size_to_string_c(RolltuiSplitSize::fixed(RolltuiDim::abs(3))) == "3",
        "split_size_to_string");
  check(anchor_from_name_c("bottom-right") == Anchor::BottomRight && anchor_name_c(Anchor::Center) == "center" && !anchor_from_name_c("middle"),
        "anchor names");
  check(border_from_name_c("rounded") == Border::Rounded && border_name_c(Border::Heavy) == "heavy" && !border_from_name_c("thick"),
        "border names");

  // ---- 3. the loader -----------------------------------------------------------------------
  std::printf("-- loader\n");
  for (std::string_view name : builtin_layout_names_c()) {
    const RolltuiLayout* l = builtin_layout_c(name);
    check(l != nullptr && view_of(l->name) == name, "built-in '" + std::string(name) + "' exists");
    if (!l) continue;
    LayoutReport rep;
    std::optional<RolltuiLayout> back = load_layout_c(layout_to_json_c(*l), rep);
    check(back && rep.clean() && *back == *l, "built-in '" + std::string(name) + "' round-trips through layout_to_json");
    check(l->popup("help", 4) != nullptr && l->popup("help", 4)->modal, "built-in '" + std::string(name) + "' declares the modal help popup");
    check(l->popup("approval", 8) != nullptr && l->popup("approval", 8)->modal && l->popup("approval", 8)->placement.anchor == Anchor::Bottom,
          "built-in '" + std::string(name) + "' declares the modal approval popup, anchored to the bottom (milestone 10)");
    StackC s(*l);
    const RolltuiLayoutNode* f = rolltui_window_stack_focused(s.s);
    check(f && f->id == "input", "built-in '" + std::string(name) + "' focuses input initially");
  }
  check(builtin_layout_c("nope") == nullptr, "unknown built-in → nullptr");
  {
    LayoutReport rep;
    auto l = load_layout_c(R"({"name": "x", "colour": 1, "root": {"content": "a", "size": "50", "shade": true, "border": "thick"},
                             "popups": [{"id": "p", "x": "50%", "w": 3.5, "anchor": "middle", "root": {"content": "b"}}]})", rep);
    check(l.has_value(), "a layout with problems still loads");
    auto has = [&](StrSpan v, std::string_view s) {
      for (const RolltuiStr& x : v) if (view_of(x).find(s) != std::string::npos) return true;
      return false;
    };
    check(has(StrSpan{rep.unknown_keys, rep.unknown_keys_n}, "colour") && has(StrSpan{rep.unknown_keys, rep.unknown_keys_n}, "root.shade"), "unknown keys named by path (colour, root.shade)");
    check(has(StrSpan{rep.bad_values, rep.bad_values_n}, "root.size: '50' is not a size"), "a bare-number size string is a bad value with its path");
    check(has(StrSpan{rep.bad_values, rep.bad_values_n}, "root.border"), "a bad border name is a bad value");
    check(has(StrSpan{rep.bad_values, rep.bad_values_n}, "popups[0].w: a number is whole cells"), "a fractional cell count is a bad value");
    check(has(StrSpan{rep.bad_values, rep.bad_values_n}, "popups[0].anchor"), "a bad anchor is a bad value");
    check(l && l->base.root.content == "a" && l->base.root.border == Border::None, "bad fields keep their defaults");
  }
  {
    LayoutReport rep;
    auto l = load_layout_c(R"({"root": {"row": [{"content": "a"}, {"content": "a"}]}, "focus": "zzz"})", rep);
    bool dup = false, dangling = false;
    for (const RolltuiStr& s : StrSpan{rep.bad_values, rep.bad_values_n}) { if (view_of(s).find("duplicate id 'a'") != std::string::npos) dup = true; if (view_of(s).find("focus: no window with id 'zzz'") != std::string::npos) dangling = true; }
    check(l && dup, "a duplicate id is reported");
    check(l && dangling, "a focus naming no window is reported");
  }
  {
    LayoutReport rep;
    check(!load_layout_c("{", rep) && view_of(rep.error).find("line") != std::string::npos, "unparseable JSON → nullopt with a line");
    LayoutReport rep2;
    check(!load_layout_c(R"({"name": "x"})", rep2) && view_of(rep2.error).find("root") != std::string::npos, "no root → nullopt, error names it");
    LayoutReport rep3;
    auto l = load_layout_c(R"({"root": {"content": "a", "row": []}})", rep3);
    check(l && rep3.bad_values_n != 0 && view_of(rep3.bad_values[0]).find("exactly one") != std::string::npos, "a node with both content and row is a bad value");
  }

  // ---- 4. the split ------------------------------------------------------------------------
  std::printf("-- split\n");
  {
    // Fixed + fill: sums to the extent. No borders: no sharing.
    RolltuiLayoutNode root = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::abs(10)), Border::None), win("b", {}, Border::None), win("c", RolltuiSplitSize::fixed(RolltuiDim::rel(0.25)), Border::None)});
    auto v = resolve_tree_c(root, {0, 0, 81, 5}, {0, 0, 81, 5});
    expect_rect("fixed abs 10", by_id(v, "a")->outer, {0, 0, 10, 5});
    expect_rect("rel 25% of 81 floors to 20 (cumulative edge: 10+25% → 30)", by_id(v, "c")->outer, {61, 0, 20, 5});
    expect_rect("fill takes the remainder", by_id(v, "b")->outer, {10, 0, 51, 5});
  }
  {
    RolltuiLayoutNode root = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::rel(0.5)), Border::None), win("b", RolltuiSplitSize::fixed(RolltuiDim::rel(0.5)), Border::None)});
    int bad = 0;
    for (int W = 0; W <= 200; ++W) {
      auto v = resolve_tree_c(root, {0, 0, W, 1}, {0, 0, W, 1});
      const RolltuiResolvedNode *a = by_id(v, "a"), *b = by_id(v, "b");
      if (!(a->outer.x == 0 && b->outer.x == a->outer.w && a->outer.w + b->outer.w == W)) ++bad;
    }
    check(bad == 0, "50% + 50% in a row tiles at every width 0..200 (" + std::to_string(bad) + " bad)");
  }
  {
    RolltuiLayoutNode root = column_of({win("a", RolltuiSplitSize::filling(1), Border::None), win("b", RolltuiSplitSize::filling(2), Border::None), win("c", RolltuiSplitSize::fixed(RolltuiDim::abs(3)), Border::None)});
    int bad = 0;
    for (int H = 0; H <= 200; ++H) {
      auto v = resolve_tree_c(root, {0, 0, 10, H}, {0, 0, 10, H});
      const RolltuiResolvedNode *a = by_id(v, "a"), *b = by_id(v, "b"), *c = by_id(v, "c");
      int rem = std::max(H - 3, 0);
      if (!(a->outer.h + b->outer.h == rem && a->outer.h == rem / 3 && c->outer.h == std::min(3, H) && c->outer.y == a->outer.h + b->outer.h)) ++bad;
    }
    check(bad == 0, "fill 1 : fill 2 divide the remainder by weight at every height (" + std::to_string(bad) + " bad)");
  }
  {
    // Shared edge: both bordered → overlap by one; the pair spans the extent exactly.
    RolltuiLayoutNode root = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::abs(32))), win("b")});
    auto v = resolve_tree_c(root, {0, 0, 80, 10}, {0, 0, 80, 10});
    expect_rect("bordered a keeps its 32", by_id(v, "a")->outer, {0, 0, 32, 10});
    expect_rect("bordered b starts on a's right border and reaches the edge", by_id(v, "b")->outer, {31, 0, 49, 10});
    expect_rect("b's inner excludes both borders", by_id(v, "b")->inner, {32, 1, 47, 8});
    RolltuiLayoutNode root2 = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::abs(32)), Border::None), win("b")});
    auto v2 = resolve_tree_c(root2, {0, 0, 80, 10}, {0, 0, 80, 10});
    expect_rect("one side unbordered: no sharing", by_id(v2, "b")->outer, {32, 0, 48, 10});
  }
  {
    // A column whose children are all bordered is bordered on its side; a mixed one is not.
    RolltuiLayoutNode all = row_of({column_of({win("t"), win("i", RolltuiSplitSize::fixed(RolltuiDim::abs(3)))}), win("s", RolltuiSplitSize::fixed(RolltuiDim::abs(32)))});
    auto v = resolve_tree_c(all, {0, 0, 80, 24}, {0, 0, 80, 24});
    expect_rect("status shares the column's right edge", by_id(v, "s")->outer, {48, 0, 32, 24});
    expect_rect("transcript spans to the shared column", by_id(v, "t")->outer, {0, 0, 49, 22});
    expect_rect("input shares the transcript's bottom edge", by_id(v, "i")->outer, {0, 21, 49, 3});
    RolltuiLayoutNode mixed = row_of({column_of({win("t"), win("i", RolltuiSplitSize::fixed(RolltuiDim::abs(3)), Border::None)}), win("s", RolltuiSplitSize::fixed(RolltuiDim::abs(32)))});
    auto v2 = resolve_tree_c(mixed, {0, 0, 80, 24}, {0, 0, 80, 24});
    expect_rect("a column with an unbordered child does not share", by_id(v2, "s")->outer, {48, 0, 32, 24});
    expect_rect("…so the transcript stops short of it", by_id(v2, "t")->outer, {0, 0, 48, 21});
  }
  {
    RolltuiLayoutNode root = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::abs(10)), Border::None), win("hidden", {}, Border::None), win("b", {}, Border::None)});
    root.children[1].visible = false;
    auto v = resolve_tree_c(root, {0, 0, 50, 1}, {0, 0, 50, 1});
    check(by_id(v, "hidden") == nullptr, "a hidden node is not resolved");
    expect_rect("…and takes no space", by_id(v, "b")->outer, {10, 0, 40, 1});
  }
  {
    RolltuiLayoutNode root = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::abs(30)), Border::None), win("b", RolltuiSplitSize::fixed(RolltuiDim::abs(30)), Border::None), win("c", RolltuiSplitSize::fixed(RolltuiDim::abs(30)), Border::None)});
    auto v = resolve_tree_c(root, {0, 0, 50, 1}, {0, 0, 50, 1});
    expect_rect("overflow: second is clipped", by_id(v, "b")->outer, {30, 0, 20, 1});
    expect_rect("overflow: third gets 0", by_id(v, "c")->outer, {50, 0, 0, 1});
    RolltuiLayoutNode root2 = row_of({win("a", RolltuiSplitSize::fixed(RolltuiDim::abs(10)), Border::None), win("b", RolltuiSplitSize::fixed(RolltuiDim::abs(10)), Border::None)});
    auto v2 = resolve_tree_c(root2, {0, 0, 50, 1}, {0, 0, 50, 1});
    expect_rect("shortfall with no fill leaves space, no stretch", by_id(v2, "b")->outer, {10, 0, 10, 1});
  }
  {
    RolltuiLayoutNode root = column_of({win("a"), win("b", RolltuiSplitSize::fixed(RolltuiDim::abs(3)))});
    root.border = Border::Single;
    auto v = resolve_tree_c(root, {0, 0, 40, 10}, {0, 0, 40, 10});
    expect_rect("a bordered container splits its inner rect", by_id(v, "a")->outer, {1, 1, 38, 6});
    expect_rect("…bottom child shares a's edge", by_id(v, "b")->outer, {1, 6, 38, 3});
    check(v.front().node == &root && v.size() == 3, "tree order: container first, then children");
  }

  // ---- 5. composition ------------------------------------------------------------------------
  std::printf("-- composition\n");
  ThemeFixture dark;
  builtin_theme_c("default-dark", dark);
  {
    StackC s(*builtin_layout_c("default"));
    FrameC f(80, 24, dark.style(ROLLTUI_ROLE_BACKGROUND));
    int slots = 0;
    compose_c(s.s, f, scr, dark, [&](const RolltuiResolvedNode& rn, FrameC& fr) { ++slots; fr.put_text(rn.inner.x, rn.inner.y, view_of(rn.node->content), dark.style(ROLLTUI_ROLE_TEXT), rn.inner.w); });
    check(slots == 3, "three slots rendered (" + std::to_string(slots) + ")");
    check(cell(f, 0, 0) == "┌" && cell(f, 79, 0) == "┐" && cell(f, 0, 23) == "└" && cell(f, 79, 23) == "┘", "outer corners");
    check(cell(f, 48, 0) == "┬", "top junction where status meets transcript is ┬ (" + cell(f, 48, 0) + ")");
    check(cell(f, 48, 21) == "┤", "the input's top edge meets the status column from the left: ┤ (" + cell(f, 48, 21) + ")");
    check(cell(f, 0, 21) == "├" && cell(f, 79, 21) == "│", "input's top edge joins the outer frame at ├; the far edge is a plain │");
    check(cell(f, 48, 23) == "┴", "bottom junction under the status column is ┴ (" + cell(f, 48, 23) + ")");
    check(cell(f, 2, 0) == "t" && cell(f, 1, 0) == " " && cell(f, 12, 0) == " " && cell(f, 13, 0) == "─", "title ' transcript ' sits in the top edge after the corner");
    check(cell(f, 1, 1) == "t" && cell(f, 49, 1) == "r" && cell(f, 1, 22) == "i",
          "each slot drew at its inner origin (the contents are transcript:session, rows:status, input:prompt)");
    check(f.at(60, 10).style.bg == dark.style(ROLLTUI_ROLE_PANEL_BACKGROUND).bg, "the status window is filled with panel_background");
    check(f.at(48, 5).style.bg == dark.style(ROLLTUI_ROLE_PANEL_BACKGROUND).bg && f.at(48, 5).style.fg == dark.style(ROLLTUI_ROLE_BORDER).fg,
          "a border takes its colour from `border` and its ground from the window it belongs to");
    check(f.at(0, 22).style.fg == dark.style(ROLLTUI_ROLE_BORDER_ACTIVE).fg && f.at(0, 5).style.fg == dark.style(ROLLTUI_ROLE_BORDER).fg,
          "the focused input's border is border_active; the transcript's is border");
    // `title` was the ONE of the four compose roles nothing asserted — the cell
    // it lands in was checked, its colour was not. That is the whole reason a role table can
    // move house and still be wrong: three of the four would have failed loudly and this one
    // would have gone quietly. Pointing the compose at `warning` instead turns this red.
    check(f.at(2, 0).style.fg == dark.style(ROLLTUI_ROLE_TITLE).fg && f.at(2, 0).style.fg != dark.style(ROLLTUI_ROLE_BORDER).fg,
          "…and the title text is painted with `title`, which is not the border's colour");
  }
  {
    // A popup whose ring crosses the status's left border: no join across layers, and
    // the modal overlay tints only what is beneath.
    StackC s(*builtin_layout_c("default"));
    RolltuiLayer help = (*builtin_layout_c("default")->popup("help", 4)).clone();
    rolltui_window_stack_push(s.s, &help);
    FrameC f(80, 24, dark.style(ROLLTUI_ROLE_BACKGROUND));
    compose_c(s.s, f, scr, dark, [&](const RolltuiResolvedNode&, FrameC&) {});
    auto v = resolve_stack_c(s.s, scr);
    const RolltuiResolvedNode* h = by_id(v, "help");
    check(h && h->outer == RolltuiRect{16, 6, 48, 12}, "the help popup lands where the table says at 80x24");
    check(cell(f, 48, 6) == "─" && cell(f, 48, 17) == "─", "the popup's edge crossing the status border stays ─ (never joins the layer below)");
    check(cell(f, 16, 6) == "╭" && cell(f, 63, 17) == "╯", "rounded corners");
    check(f.at(2, 2).style.dim && f.at(70, 2).style.dim, "cells beneath a modal are tinted with `overlay` (dim in default-dark)");
    check(!f.at(20, 8).style.dim && !f.at(16, 6).style.dim, "cells of the modal itself are not tinted");
    check(cell(f, 18, 6) == "h", "popup title drawn");
    FrameC g(120, 40, dark.style(ROLLTUI_ROLE_BACKGROUND));
    compose_c(s.s, g, {0, 0, 120, 40}, dark, [&](const RolltuiResolvedNode&, FrameC&) {});
    check(cell(g, 24, 14) == "╭" && cell(g, 95, 25) == "╯", "the same popup re-places itself at 120x40 (24,14)-(95,25)");
  }
  {
    // draw_border on its own, and the degenerate sizes.
    RolltuiDrawScratch* scratch = rolltui_draw_scratch_new();
    RolltuiFrame* f = rolltui_frame_new(10, 3, RolltuiStyle{});
    rolltui_draw_border(f, scratch, RolltuiRect{0, 0, 10, 3}, static_cast<unsigned char>(Border::Double), RolltuiStyle{}, "ab", 2,
                       RolltuiStyle{}, 0);
    check(cell_c(f, 0, 0) == "╔" && cell_c(f, 9, 2) == "╝" && cell_c(f, 2, 0) == "a" && cell_c(f, 5, 0) == "═" && cell_c(f, 0, 1) == "║", "double border with title");
    RolltuiFrame* g = rolltui_frame_new(10, 3, RolltuiStyle{});
    rolltui_draw_border(g, scratch, RolltuiRect{0, 0, 1, 3}, static_cast<unsigned char>(Border::Single), RolltuiStyle{}, "", 0,
                       RolltuiStyle{}, 0);
    check(cell_c(g, 0, 0) == "╷" && cell_c(g, 0, 1) == "│" && cell_c(g, 0, 2) == "╵", "a 1-wide border is a vertical line");
    rolltui_draw_border(g, scratch, RolltuiRect{2, 0, 3, 1}, static_cast<unsigned char>(Border::Single), RolltuiStyle{}, "", 0,
                       RolltuiStyle{}, 0);
    check(cell_c(g, 2, 0) == "╶" && cell_c(g, 3, 0) == "─" && cell_c(g, 4, 0) == "╴", "a 1-high border is a horizontal line");
    rolltui_draw_border(g, scratch, RolltuiRect{-2, -1, 5, 3}, static_cast<unsigned char>(Border::Single), RolltuiStyle{}, "", 0,
                       RolltuiStyle{}, 0);
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
    check(route_c(s.s, chr_ev('x'), scr) == RouteC{RouteC::Kind::Deliver, "input"}, "a key goes to the focused window");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_TAB), scr) == RouteC{RouteC::Kind::FocusMoved, "transcript"}, "Tab cycles to the transcript");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_TAB), scr) == RouteC{RouteC::Kind::FocusMoved, "input"}, "Tab wraps back to the input");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_TAB, true), scr) == RouteC{RouteC::Kind::FocusMoved, "transcript"}, "Shift-Tab cycles backwards");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_ESCAPE), scr) == RouteC{RouteC::Kind::Deliver, "transcript"}, "Escape with no popup is delivered");
    RolltuiMouseEvent m;
    m.kind = RolltuiMouseEvent::Kind::Press;
    m.x = 60; m.y = 5;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Deliver, "status"} && rolltui_window_stack_focused(s.s)->id == "transcript",
          "a press on a non-focusable window is delivered and leaves focus alone");
    m.x = 5; m.y = 22;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Deliver, "input"} && rolltui_window_stack_focused(s.s)->id == "input", "a press on a focusable base window focuses it");
    RolltuiMouseEvent wheel;
    wheel.kind = RolltuiMouseEvent::Kind::WheelUp;
    wheel.x = 5; wheel.y = 5;
    check(route_c(s.s, mouse_ev(wheel), scr) == RouteC{RouteC::Kind::Deliver, "transcript"} && rolltui_window_stack_focused(s.s)->id == "input", "a wheel goes to the window under the pointer, focus unchanged");
    // Pointer capture (milestone 9): a press captures; drags and the release follow it
    // wherever the pointer goes; after the release routing is by position again.
    m.x = 5; m.y = 5;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Deliver, "transcript"} && captured_c(s.s) == "transcript", "a press captures the pointer for its window");
    RolltuiMouseEvent drag = m;
    drag.kind = RolltuiMouseEvent::Kind::Drag;
    drag.x = 60; drag.y = 30;  // over the status panel, and below the screen
    check(route_c(s.s, mouse_ev(drag), scr) == RouteC{RouteC::Kind::Deliver, "transcript"}, "a drag off the window (even off the screen) still goes to the captured window");
    RolltuiMouseEvent release = drag;
    release.kind = RolltuiMouseEvent::Kind::Release;
    check(route_c(s.s, mouse_ev(release), scr) == RouteC{RouteC::Kind::Deliver, "transcript"} && captured_c(s.s).empty(), "the release goes there too and ends the capture");
    check(route_c(s.s, mouse_ev(drag), scr) == RouteC{RouteC::Kind::Dropped, ""}, "a drag with no capture and no window under it is dropped");
    check(rolltui_window_stack_focused(s.s)->id == "transcript", "the press focused the transcript");
    focus_c(s.s, "input");

    // A modal popup.
    RolltuiLayer help_popup = (*builtin_layout_c("default")->popup("help", 4)).clone();
    rolltui_window_stack_push(s.s, &help_popup);
    check(rolltui_window_stack_depth(s.s) == 2 && has_popup_c(s.s, "help") && rolltui_window_stack_focused(s.s)->id == "help" && rolltui_window_stack_focus_layer(s.s) == 1, "a modal focusable popup takes focus");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_TAB), scr) == RouteC{RouteC::Kind::Deliver, "help"}, "Tab with one focusable window in the layer is delivered");
    m.x = 5; m.y = 22;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Dropped, ""}, "a press outside a modal is dropped");
    m.x = 20; m.y = 8;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Deliver, "help"}, "a press inside the modal is delivered to it");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_ESCAPE), scr) == RouteC{RouteC::Kind::ClosedPopup, "help"} && rolltui_window_stack_depth(s.s) == 1, "Escape closes the topmost popup");
    check(rolltui_window_stack_focused(s.s)->id == "input", "focus returns to the base layer's window");
    check(!rolltui_window_stack_pop(s.s), "pop() on the base alone is false");

    // the same push, BY ID out of the layout — the operation two hosts had each
    // hand-written and a pure-C consumer could not write at all (the copy above is a deep one
    // only because C++ synthesises it; see `rolltui_window_stack_push_popup`'s declaration).
    // The layout is BORROWED and must be unchanged by the push, which is what the second
    // check is for: a move out of the layout's own popup would leave the second call failing.
    const RolltuiLayout* dflt = builtin_layout_c("default");
    check(rolltui_window_stack_push_popup(s.s, dflt, "help", 4) &&
              rolltui_window_stack_depth(s.s) == 2 && has_popup_c(s.s, "help"),
          "push_popup() pushes the popup the layout declares, by id");
    check(rolltui_window_stack_push_popup(s.s, dflt, "help", 4) && rolltui_window_stack_depth(s.s) == 3,
          "…by COPY: the layout still declares it after a push");
    check(!rolltui_window_stack_push_popup(s.s, dflt, "nosuch", 6) && rolltui_window_stack_depth(s.s) == 3,
          "…and an id the layout does not declare pushes nothing and says so");
    rolltui_window_stack_pop(s.s);
    rolltui_window_stack_pop(s.s);
    check(rolltui_window_stack_depth(s.s) == 1 && rolltui_window_stack_focused(s.s)->id == "input",
          "…both pop cleanly and focus returns");

    // A non-focusable, non-modal notice above the base.
    RolltuiLayer notice;
    notice.id = "notice";
    notice.placement = P(RolltuiDim::rel(1), RolltuiDim::abs(0), RolltuiDim::abs(20), RolltuiDim::abs(1), Anchor::TopRight);
    notice.root = RolltuiLayoutNode::window("text:hi");
    rolltui_window_stack_push(s.s, &notice);
    check(rolltui_window_stack_focused(s.s)->id == "input" && rolltui_window_stack_focus_layer(s.s) == 0, "a non-focusable notice leaves focus with the base");
    check(route_c(s.s, chr_ev('x'), scr) == RouteC{RouteC::Kind::Deliver, "input"}, "keys still reach the base under a non-modal popup");
    m.x = 60; m.y = 5;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Deliver, "status"}, "a press beneath a non-modal popup reaches the base window");
    m.x = 70; m.y = 0;
    check(route_c(s.s, mouse_ev(m), scr) == RouteC{RouteC::Kind::Deliver, "text:hi"}, "a press on the notice hits it");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_ESCAPE), scr) == RouteC{RouteC::Kind::ClosedPopup, "notice"}, "Escape closes a notice too");

    // A modal layer with no focusable window drops keys rather than leaking them.
    RolltuiLayer wall;
    wall.id = "wall";
    wall.modal = true;
    wall.root = RolltuiLayoutNode::window("text:wait");
    rolltui_window_stack_push(s.s, &wall);
    check(rolltui_window_stack_focused(s.s) == nullptr && route_c(s.s, chr_ev('x'), scr) == RouteC{RouteC::Kind::Dropped, ""}, "a modal with nothing focusable drops keys");
    rolltui_window_stack_pop(s.s);

    // Hot reload: focus survives when the id does; falls back when it does not.
    focus_c(s.s, "transcript");
    RolltuiLayer reloaded = (builtin_layout_c("panel-left")->base).clone();
    reloaded.focus.clear();
    rolltui_window_stack_set_base(s.s, &reloaded);
    check(rolltui_window_stack_focused(s.s)->id == "transcript", "set_base keeps the focused id across a reload when it still exists");
    RolltuiLayer other;
    other.root = RolltuiLayoutNode::column();
    other.root.children.push_back(win("a", {}, Border::None, true));
    other.root.children.push_back(win("b", {}, Border::None, true));
    rolltui_window_stack_set_base(s.s, &other);
    check(rolltui_window_stack_focused(s.s)->id == "a", "…and falls back to the first focusable when it does not");
    RolltuiLayer named = (builtin_layout_c("default")->base).clone();
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
    RolltuiLayer dlg;
    dlg.id = "dlg";
    dlg.modal = true;
    dlg.placement = P(RolltuiDim::rel(0.5), RolltuiDim::rel(0.5), RolltuiDim::abs(40), RolltuiDim::abs(10), Anchor::Center);
    dlg.root = RolltuiLayoutNode::column();
    dlg.root.children.push_back(win("text", {}, Border::None, false));
    dlg.root.children.push_back(win("field", RolltuiSplitSize::fixed(RolltuiDim::abs(1)), Border::None, true));
    dlg.root.children.push_back(win("buttons", RolltuiSplitSize::fixed(RolltuiDim::abs(1)), Border::None, true));
    dlg.root.border = Border::Rounded;
    rolltui_window_stack_push(s.s, &dlg);
    check(rolltui_window_stack_focused(s.s)->id == "field", "a dialog focuses its first focusable part");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_TAB), scr) == RouteC{RouteC::Kind::FocusMoved, "buttons"}, "Tab moves to the next part");
    check(route_c(s.s, key_ev(ROLLTUI_KEY_TAB), scr) == RouteC{RouteC::Kind::FocusMoved, "field"}, "…and wraps within the dialog, never into the base");
    auto v = resolve_stack_c(s.s, scr);
    const RolltuiResolvedNode* frame_node = nullptr;
    for (const RolltuiResolvedNode& rn : v)
      if (rn.layer == 1 && !frame_node) frame_node = &rn;
    expect_rect("the dialog's frame", frame_node->outer, {20, 7, 40, 10});
    expect_rect("its text part inside the frame", by_id(v, "text")->outer, {21, 8, 38, 6});
    expect_rect("its buttons at the bottom", by_id(v, "buttons")->outer, {21, 15, 38, 1});
  }

  // ---- 7. content and widgets --------------------------------------------------
  std::printf("-- content: the kind table\n");
  {
    // Every library kind is in the table under its own name, AT ITS OWN ROW, and nothing
    // else is: the name is the identity, and the row is where the kind's rules live.
    const std::size_t lib = rolltui_widget_kind_library_count();
    check(lib == 7 && rolltui_widget_kind_count(test_ctx()) == lib, "the library's closed table has seven kinds and, before any host registers, they are the whole enumeration (" + std::to_string(lib) + ")");
    for (std::size_t i = 0; i < lib; ++i) {
      int rung = ROLLTUI_KIND_UNKNOWN;
      check(widget_kind_row_c(widget_kind_name_c(i), &rung) == i && rung == ROLLTUI_KIND_LIBRARY,
            "kind '" + std::string(widget_kind_name_c(i)) + "' round-trips to row " + std::to_string(i) + " at rung 1");
    }
    check(!widget_kind_row_c("dialog") && !widget_kind_row_c("") && !widget_kind_row_c("Transcript"),
          "an unknown kind name is not in the table (and it is case-sensitive)");
    // The per-kind rules are the ROW's, as data. The source SHAPE was the one rule the retired
    // enum had been carrying silently (the design editor compared `Text || File`).
    {
      bool shapes = true;
      for (std::size_t i = 0; i < lib; ++i) {
        const std::string_view n = widget_kind_name_c(i);
        const bool text = n == "text" || n == "file";
        shapes = shapes && rolltui_widget_kind_source_shape(i) == (text ? ROLLTUI_SOURCE_SHAPE_TEXT : ROLLTUI_SOURCE_SHAPE_NAME);
      }
      check(shapes, "text and file take free text (a literal, a path); every other library kind's source is a Name");
      check(widget_kind_name_c(lib + 500).empty() && rolltui_widget_kind_rule(test_ctx(), lib + 500) == ROLLTUI_SOURCE_REQUIRED &&
                rolltui_widget_kind_source_shape(lib + 500) == ROLLTUI_SOURCE_SHAPE_NAME &&
                std::string_view(rolltui_widget_kind_source_is(test_ctx(), lib + 500, nullptr)).empty(),
            "a row past the end is empty, REQUIRED and a Name — never a read past either table");
    }

    // Every kind parses with the source its rule demands.
    struct Case { const char* text; const char* kind; const char* source; };
    for (const Case& c : {Case{"transcript:session", "transcript", "session"},
                          Case{"input:prompt", "input", "prompt"},
                          Case{"menu:main", "menu", "main"},
                          Case{"rows:status", "rows", "status"},
                          Case{"text: a label", "text", " a label"},
                          Case{"text", "text", ""},
                          Case{"text:", "text", ""},
                          Case{"file:/tmp/x.md", "file", "/tmp/x.md"},
                          Case{"help", "help", ""},
                          // `help` takes an OPTIONAL scope, so which keys a
                          // window lists is the layout's and not only the host's.
                          Case{"help:app", "help", "app"},

                          Case{"text:a:b", "text", "a:b"}}) {
      std::string why;
      const std::optional<RolltuiContent> got = parse_content_c(c.text, &why);
      check(got && got->kind == c.kind && got->source == c.source,
            std::string("'") + c.text + "' parses as " + c.kind + " + '" + c.source + "'" + (got ? "" : " (" + why + ")"));
    }
    check(content_to_string_c({"rows", "status"}) == "rows:status" && content_to_string_c({"help", ""}) == "help" &&
              content_to_string_c({"help", "app"}) == "help:app" && content_to_string_c({"text", ""}) == "text",
          "content_to_string is the inverse, and an OPTIONAL source that is empty writes no colon — one spelling, not two");
    check(content_to_string_c({"rows", ""}) == "rows:",
          "…while a REQUIRED source that is empty keeps its colon: the window says out loud that it needs a name");

    // Every way it can be wrong SAYS SO, by name.
    // A FORBIDDEN source is now only a registered kind's rule to state — no library kind
    // forbids one, since `help` takes an optional scope — so the case is tested through
    // one, registered and cleared right here so nothing after it inherits the vocabulary.
    std::string kind_why;
    check(register_widget_kind_c("modal", ROLLTUI_SOURCE_FORBIDDEN, "", &kind_why), "a host kind that takes no source registers [" + kind_why + "]");
    struct Bad { const char* text; const char* names; };
    for (const Bad& b : {Bad{"dialog:x", "'dialog' is not a widget kind"},
                         Bad{"rows", "'rows' needs a source"},
                         Bad{"modal:x", "'modal' takes no source"},
                         Bad{"", "'' is not a widget kind"}}) {
      std::string why;
      const bool bad = !parse_content_c(b.text, &why);
      check(bad && why.find(b.names) != std::string::npos, std::string("'") + b.text + "' is refused: " + why);
    }
    // A bare kind that needs a source is refused with the SOURCE it needs, and one that is
    // not a kind at all is refused with the list — no table of older spellings in between.
    std::string why;
    check(!parse_content_c("transcript", &why) && why.find("needs a source") != std::string::npos,
          "a bare kind that needs a source says so: " + why);
    check(!parse_content_c("status", &why) && why.find("is not a widget kind") != std::string::npos,
          "…and a name that is no kind at all gets the list: " + why);
    clear_registered_widget_kinds_c();
  }
  {
    // A file with no "actions" key is given the shipped default's — it must not
    // silently lose every app key — and the loader says so in `notes`, which is the one
    // thing that array still carries.
    LayoutReport rep;
    const std::optional<RolltuiLayout> l = load_layout_c(R"({"name":"old","focus":"input","root":{"column":[
        {"content":"transcript:session","id":"transcript","focusable":true},
        {"content":"input:prompt","id":"input","size":3,"focusable":true}]}})", rep);
    check(l && rep.clean(), "a layout declaring no actions still loads, clean");
    check(l && l->actions == shipped_default_actions_c() && rep.notes_n == 1 &&
              view_of(rep.notes[0]).find("actions: none declared") == 0,
          "…and it is given the shipped default's, named in the report [" + (rep.notes_n == 0 ? std::string() : str_of(rep.notes[0])) + "]");
    const RolltuiLayoutNode& first = l->base.root.children[0];
    check(first.id == "transcript" && first.content == "transcript:session", "the window keeps its id and its content");
    check(l->base.focus == "input" && l->base.root.children[1].id == "input", "…so the layout's own focus id still names a window");

    // THE LOADER DOES NOT JUDGE AN UNKNOWN KIND, AND DOES NOT JUDGE A HOST KIND'S SOURCE
    // RULE EITHER; `Windows` does both. Rung 2 belongs to a CONTEXT and a layout file is
    // plain data portable between contexts, so the loader runs with no session at all and
    // judges exactly what the library's closed table can answer.
    // What that retires is a registration-ORDER dependency: whether `modal:x` was a bad value
    // used to depend on whether the host had registered `modal` before the file was read.
    LayoutReport rep2;
    std::string modal_why;
    register_widget_kind_c("modal", ROLLTUI_SOURCE_FORBIDDEN, "", &modal_why);  // see the parse block above
    const std::optional<RolltuiLayout> l2 = load_layout_c(R"({"name":"bad","root":{"column":[
        {"content":"dialog:x","id":"a"},{"content":"modal:x","id":"b"},{"content":"transcript","id":"c"}]}})", rep2);
    check(l2 && !rep2.clean() && rep2.bad_values_n == 1 &&
              view_of(rep2.bad_values[0]).find("root.column[2].content: 'transcript' needs a source") !=
                  std::string::npos,
          "a LIBRARY kind's source rule is the loader's to name, by PATH, and the layout still loads [" +
              (rep2.bad_values_n == 0 ? std::string() : str_of(rep2.bad_values[0])) + "]");

    // …AND THE HOST KIND'S RULE IS NOT LOST, it is reported where the registry actually is.
    // `modal:x` and the unknown `dialog:x` both reach `sync` and both get named there.
    {
      WindowsC windows;
      windows.set_dir("");
      StackC st(*l2);
      const WindowsReportC wrep = windows.prepare(st, RolltuiRect{0, 0, 40, 12});
      std::string all;
      for (const std::string& b : wrep.bad_values) all += b + " | ";
      check(!wrep.clean() && all.find("'modal' takes no source") != std::string::npos,
            "…and the host kind's forbidden source is named at sync, by the session that has the registry [" + all + "]");
      check(all.find("'dialog' is not a widget kind") != std::string::npos,
            "…alongside the unknown kind Phase 11 m3 moved here, so one stage names both");
    }
    clear_registered_widget_kinds_c();
    std::string dwhy;
    unsigned char dwhat = ROLLTUI_CONTENT_PROBLEM_NONE;
    check(!parse_content_c("dialog:x", &dwhy, &dwhat) && dwhat == ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND &&
              dwhy.find("'dialog' is not a widget kind") != std::string::npos,
          "…and the unknown kind is still parse_content's named refusal, tagged so the loader can leave it to the host");
  }
  {
    // ---- the layout DECLARES the actions its screen emits -------------------------
    LayoutReport rep;
    const std::optional<RolltuiLayout> l = load_layout_c(R"({"name":"acts","actions":{"app.zoom":"zoom in","mine.thing":"my own"},
        "root":{"content":"help"}})", rep);
    check(l && rep.clean() && l->actions.size() == 2 && l->actions[0].name == "app.zoom" &&
              l->actions[0].description == "zoom in" && l->actions[1].name == "mine.thing",
          "\"actions\" is an object of name → description, in file order");
    check(rep.notes_n == 0, "…and a file that declares actions is not given the shipped default's");

    LayoutReport er;
    const std::optional<RolltuiLayout> bad = load_layout_c(R"({"name":"bad","actions":{"app.a":"ok","nodot":"x","app.":"x",
        "input.frob":"x","menu.frob":"x","app.b":7},"root":{"content":"help"}})", er);
    check(bad && bad->actions.size() == 1 && bad->actions[0].name == "app.a", "…and only the well-formed ones are declared");
    // Indexed through a bounds-checked helper: a control that SEGFAULTS reports nothing
    // (CLAUDE.md), and every one of these expects a specific position in the list.
    auto v = [&](std::size_t i) { return i < er.bad_values_n ? str_of(er.bad_values[i]) : std::string("(none)"); };
    check(er.bad_values_n == 5 && v(0).find("actions.nodot: an action is \"<scope>.<verb>\"") == 0 &&
              v(1).find("actions.app.: an action is") == 0,
          "a name that is not \"<scope>.<verb>\" is a bad value [" + v(0) + "]");
    check(v(2).find("actions.input.frob: the 'input' scope is the library's") == 0 &&
              v(3).find("actions.menu.frob: the 'menu' scope is the library's") == 0,
          "a library scope cannot be declared into [" + v(2) + "]");
    check(v(4).find("actions.app.b: expected a description string") == 0,
          "a non-string description is a bad value [" + v(4) + "]");

    // Present-but-empty is a deliberate "none" and must differ from absent, or the
    // fill-in above would quietly re-add what someone deliberately removed.
    LayoutReport nr;
    const std::optional<RolltuiLayout> none = load_layout_c(R"({"name":"none","actions":{},"root":{"content":"help"}})", nr);
    check(none && none->actions.empty() && nr.notes_n == 0, "an explicit \"actions\": {} declares none and is left alone");

    // Round trip, including the empty case (which is why "actions" is always written).
    LayoutReport rr;
    const std::optional<RolltuiLayout> back = load_layout_c(layout_to_json_c(*l), rr);
    check(back && rr.clean() && back->actions == l->actions && *back == *l, "a layout's actions round-trip through layout_to_json");
    LayoutReport rn2;
    const std::optional<RolltuiLayout> none_back = load_layout_c(layout_to_json_c(*none), rn2);
    check(none_back && none_back->actions.empty() && rn2.notes_n == 0, "…and so does declaring none");

    // Every shipped layout declares the same app scope: switching arrangement must not
    // change which keys work.
    bool same = true;
    for (std::string_view n : builtin_layout_names_c()) same &= builtin_layout_c(n)->actions == shipped_default_actions_c();
    check(same && shipped_default_actions_c().size() == 7, "every shipped layout declares the same seven app actions");
  }

  std::printf("-- widgets: one per content, by kind\n");
  {
    // A layout with every kind in it, and a host that binds every source.
    const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/rolltui_widgets_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/menus");
    { std::ofstream(dir + "/note.md") << "from a file"; }
    { std::ofstream(dir + "/menus/main.json") << R"({"id":"root","label":"menu","items":[{"id":"act","label":"an action"}]})"; }

    LayoutReport lr;
    std::optional<RolltuiLayout> lay = load_layout_c(R"({"name":"every-kind","focus":"prompt","root":{"column":[
        {"id":"tx","content":"transcript:session"},
        {"id":"panel","content":"rows:status","size":2},
        {"id":"label","content":"text:a literal","size":1},
        {"id":"doc","content":"file:note.md","size":1},
        {"id":"keys","content":"help","size":1},
        {"id":"m","content":"menu:main","size":1},
        {"id":"own","content":"mine:one","size":1},
        {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})", lr);
    check(lay && lr.clean(), "the every-kind layout loads clean");

    RolltuiDocument doc;
    RolltuiDocEntry e;
    e.id = "e0";
    e.text = "hello transcript";
    doc.push_back(e);
    int drew_own = 0;
    WindowsC windows;
    windows.set_dir(dir);
    windows.bind_document("session", &doc);
    windows.bind_rows("status", [](void*, RolltuiRows* out) { out->add("label", "value"); });
    windows.bind_submit("prompt", [](void*, const char*, std::size_t) {});
    // a kind this test registers, built by the library like any other.
    MineCtx mine_proto{&drew_own, &dark};
    windows.register_kind("mine", mine_factory, &mine_proto);
    windows.set_help("", {"transcript"}, "");

    StackC s(*lay);
    const RolltuiRect box{0, 0, 40, 12};
    const WindowsReportC rep = windows.prepare(s, box);
    check(rep.clean(), "every source is bound: nothing to report [" + rep.summary() + "]");

    FrameC f(40, 12, dark.style(ROLLTUI_ROLE_BACKGROUND));
    s.compose(f, box, dark, [&](const RolltuiResolvedNode& rn, FrameC& fr) { windows.draw(rn, fr, dark); });
    auto row = [&](int y) {
      std::string out;
      for (int x = 0; x < 40; ++x) out += f.glyph(x, y);
      while (!out.empty() && out.back() == ' ') out.pop_back();
      return out;
    };
    const std::vector<RolltuiResolvedNode> v = s.resolve(box);
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
    check(windows.content_at("panel") == content_to_string_c(RolltuiContent{"rows", "status"}) && !windows.content_at("nope"),
          "content_at names what a window holds");

    // THE CHECK THAT SEPARATES "PORTED" FROM "REACHABLE". Reaching an accessor through a
    // C++ wrapper proves nothing about whether a C consumer can get the typed handle out of
    // the window table at all. So this drives the input through the LIBRARY's entry points
    // alone,
    // on the table's own handle, and looks for the bytes on the drawn frame — an accessor that
    // handed back a different object would pass every check above and fail this one.
    RolltuiWindows* wh = windows.handle();
    rolltui_input_set_text(rolltui_windows_input(wh, "prompt", 6), "typed through the C", 19);
    windows.prepare(s, box);
    FrameC f2(40, 12, dark.style(ROLLTUI_ROLE_BACKGROUND));
    s.compose(f2, box, dark, [&](const RolltuiResolvedNode& rn, FrameC& fr) { windows.draw(rn, fr, dark); });
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
    LayoutReport lr2;
    std::optional<RolltuiLayout> two = load_layout_c(R"({"name":"two","root":{"column":[
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
    WindowsC windows;
    LayoutReport lr;
    std::optional<RolltuiLayout> lay = load_layout_c(R"({"name":"unbound","root":{"column":[
        {"id":"a","content":"transcript:nope","size":1},
        {"id":"b","content":"rows:nope","size":1},
        {"id":"c","content":"nope:x","size":1},
        {"id":"d","content":"input:nope","size":1},
        {"id":"e","content":"menu:nope","size":1},
        {"id":"f","content":"file:/nope/nothing.md","size":1},
        {"id":"g","content":"dialog:x","size":1}]}})", lr);
    check(lay && lr.clean(), "no bad value from the LOADER: an unbound source and an unknown kind are both host facts (Phase 11 m3)");
    StackC s(*lay);
    const RolltuiRect box{0, 0, 60, 7};
    const WindowsReportC rep = windows.prepare(s, box);
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

    FrameC f(60, 7, dark.style(ROLLTUI_ROLE_BACKGROUND));
    s.compose(f, box, dark, [&](const RolltuiResolvedNode& rn, FrameC& fr) { windows.draw(rn, fr, dark); });
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
    // ---- a menu is a FILE, resolved through three rungs ---------------------------
    // The order is the whole point: a user's own file shadows the host's, which shadows
    // the library's shipped one, and each is named so a surprising menu has one place
    // to be traced from.
    const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/rolltui_menus_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/menus");

    LayoutReport lr;
    std::optional<RolltuiLayout> lay = load_layout_c(R"({"name":"menus","root":{"column":[
        {"id":"a","content":"menu:main"},
        {"id":"b","content":"menu:extra"}]}})", lr);
    check(lay && lr.clean(), "a layout naming two menu files loads clean");
    StackC s(*lay);
    const RolltuiRect box{0, 0, 44, 8};

    WindowsC windows;
    windows.set_dir(dir);
    // Read a loaded tree without ever indexing into one that did not load: a control
    // that SEGFAULTS reports nothing (CLAUDE.md), and every rung here can be empty.
    auto first_label = [&](const char* name) {
      const RolltuiMenuItem& r = *rolltui_menu_root(windows.menu(name));
      return r.children.empty() ? std::string("(no items)") : str_of(r.children.front().label);
    };
    auto option_count = [&](const char* name, const char* id) {
      const RolltuiMenuItem* it = rolltui_menu_find(windows.menu(name), id, std::strlen(id));
      return it ? static_cast<int>(it->children.size()) : -1;
    };
    // Rung 3, with nothing else present: the library's own shipped menus/main.json.
    check(!shipped_menu_c("main").empty() && shipped_menu_c("nothing-ships-this").empty(),
          "the library ships menus/main.json and nothing under a name it has no file for");
    // `menu_origin` is what says WHICH file this came from; the root's label is a word on a
    // breadcrumb and pinning it here makes an editorial change to the shipped menu look like a
    // resolution bug. What is checked beside the origin is that the file actually parsed into
    // something with sections in it.
    check(windows.menu_origin("main") == "a shipped menu" && !rolltui_menu_root(windows.menu("main"))->children.empty(),
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
    WindowsReportC rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("window 'b' (content 'menu:extra'): no menu file 'extra'") != std::string::npos &&
              rep.bad_values[0].find(dir + "/menus/extra.json") != std::string::npos,
          "a menu file nobody has is reported by name, with where it was looked for [" + rep.summary() + "]");

    // The Done-when's second half, at the library level: DROP the file in and it opens.
    // Nothing is rebuilt, nothing restarts, no host code knows the name 'extra'.
    { std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"dropped","items":[{"id":"d","label":"dropped item"}]})"; }
    rep = windows.prepare(s, box);
    check(rep.clean(), "a menu file dropped in after the fact resolves with no rebuild [" + rep.summary() + "]");
    // what the design editor offers as the menu-file choice is the UNION
    // of the three rungs, deduplicated and sorted — a name is offered because a rung
    // has it, never because a host listed it. 'extra' and 'main' are the user's here;
    // 'main' is also the host's and the shipped one, and appears once.
    {
      const std::vector<std::string> names = windows.menu_names();
      std::string joined;
      for (const std::string& n : names) joined += (joined.empty() ? "" : ",") + n;
      check(joined == "extra,main", "menu_names() is the three rungs' union, deduplicated and sorted [" + joined + "]");
    }
    FrameC f(44, 8, dark.style(ROLLTUI_ROLE_BACKGROUND));
    s.compose(f, box, dark, [&](const RolltuiResolvedNode& rn, FrameC& fr) { windows.draw(rn, fr, dark); });
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
    two_options.push_back(RolltuiMenuItem::action("one", "one"));
    two_options.push_back(RolltuiMenuItem::action("two", "two"));
    rolltui_menu_set_options(windows.menu("extra"), "d", 1, &two_options);
    check(option_count("extra", "d") == 2, "a host fills a file-loaded item's options by id (" + std::to_string(option_count("extra", "d")) + ")");

    // ---- a menu item may NAME an action, and it is checked against the live table.
    { std::ofstream(dir + "/menus/extra.json") << R"({"id":"root","label":"x","items":[
        {"id":"d","label":"Details","action":"app.details"},{"id":"z","label":"Zoom","action":"app.zoom"}]})"; }
    std::filesystem::last_write_time(dir + "/menus/extra.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(8));
    BindingsC binds = default_bindings_c();   // declares the shipped layout's app scope, not app.zoom
    RolltuiWidgetEnv wenv{};
    windows.set_env(wenv);
    windows.set_bindings(binds);
    rep = windows.prepare(s, box);
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("item 'z' names the action 'app.zoom', which no layout declares") != std::string::npos,
          "a menu item naming an UNDECLARED action is a bad value, by item and action [" + rep.summary() + "]");
    check(rolltui_menu_find(windows.menu("extra"), "d", 1) != nullptr, "…and the item that names a DECLARED action is not reported");

    // declare() is the whole screen's list, so app.details has to be named again
    // here or it stops being declared — which the very next assertion relies on.
    binds.declare({{"app.details", "open the session details"}, {"app.zoom", "zoom in"}});
    rep = windows.prepare(s, box);
    check(rep.clean(), "…and declaring the action clears it [" + rep.summary() + "]");
    // The point of naming the action: the shortcut is the LIVE chords, so a rebinding
    // can never leave a stale key in a menu file.
    auto shortcut_of = [&](const char* id) {
      const RolltuiMenuItem* it = rolltui_menu_find(windows.menu("extra"), id, std::strlen(id));
      return it ? str_of(it->shortcut) : std::string("(no such item)");
    };
    check(shortcut_of("d") == "F3" && shortcut_of("z").empty(),
          "an item's shortcut is rendered from the live chords [" + shortcut_of("d") + "]");
    binds.bind("app.details", *parse_chord_c("f9"));
    windows.prepare(s, box);
    check(shortcut_of("d") == "F3, F9", "…and follows a rebinding immediately [" + shortcut_of("d") + "]");
    // An action the CURRENT layout no longer declares is inert, so its item shows no
    // shortcut at all — the table still keeps its two chords. A menu that advertised them
    // would promise a key that cannot fire.
    binds.declare({{"app.zoom", "zoom in"}});
    rep = windows.prepare(s, box);
    check(binds.chords_for_count("app.details") == 2 && binds.action_for(*parse_chord_c("f3"), "app").empty() &&
              shortcut_of("d").empty() && !rep.clean(),
          "an UNdeclared action keeps its chords, emits nothing, shows no shortcut and is reported again [" +
              shortcut_of("d") + "]");
  }
  {
    // ---- `help` renders an action declared ONLY in a layout file.
    // Nothing here compiles the action in: the layout declares it, the bindings file
    // gives it a chord, and the help window is the only thing that draws it.
    LayoutReport lr;
    const std::optional<RolltuiLayout> lay = load_layout_c(R"({"name":"declared","actions":{"app.zoom":"zoom the transcript"},
        "root":{"content":"help"}})", lr);
    check(lay && lr.clean(), "a layout declaring one app action loads clean [" + (lr.bad_values_n == 0 ? std::string() : str_of(lr.bad_values[0])) + "]");
    BindingsFileReport br;
    std::optional<BindingsC> binds = bindings_from_json_c(R"({"name":"b","bindings":{"input.submit":["enter"],"app.zoom":["ctrl+g"]}})", br);
    check(binds && br.clean() && !binds->has("app.zoom"), "a bindings file alone does not make the action exist");
    binds->declare(lay->actions);  // the one line a host runs

    WindowsC windows;
    windows.set_help("", {"app"}, "");
    RolltuiWidgetEnv wenv{};
    windows.set_env(wenv);
    windows.set_bindings(*binds);
    StackC s(*lay);
    const RolltuiRect box{0, 0, 46, 4};
    check(windows.prepare(s, box).clean(), "the help window has nothing to report");
    FrameC f(46, 4, dark.style(ROLLTUI_ROLE_BACKGROUND));
    s.compose(f, box, dark, [&](const RolltuiResolvedNode& rn, FrameC& fr) { windows.draw(rn, fr, dark); });
    std::string screen;
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 46; ++x) screen += f.glyph(x, y);
    check(screen.find("Ctrl-G") != std::string::npos && screen.find("zoom the transcript") != std::string::npos,
          "help RENDERS an action that exists only because a layout file declared it [" + screen.substr(0, 46) + "]");
  }
  {
    // The input sizes its own window: it grows with its text, capped at half the
    // parent, and a bound note takes a row when it cannot sit beside one.
    check(rolltui_input_max_rows(24, 2) == 10 && rolltui_input_max_rows(23, 2) == 9 && rolltui_input_max_rows(24, 0) == 12 && rolltui_input_max_rows(3, 2) == 1,
          "input_max_rows is half the parent less the border, at least 1");
    check(rolltui_input_window_rows(1, 10, 0, 40, 10) == 1 && rolltui_input_window_rows(1, 10, 8, 40, 10) == 1, "one row, and a note that fits beside it, stay one row");
    check(rolltui_input_window_rows(1, 35, 8, 40, 10) == 2 && rolltui_input_window_rows(3, 5, 8, 40, 10) == 4, "a note that would overlap gets its own row");
    check(rolltui_input_window_rows(12, 5, 0, 40, 10) == 10 && rolltui_input_window_rows(12, 5, 5, 40, 10) == 10, "the cap wins over both the text and the note");
    check(rolltui_input_window_rows(0, 0, 0, 40, 10) == 1 && rolltui_input_window_rows(5, 0, 0, 40, 0) == 1, "never fewer than one row, whatever the cap");

    RolltuiDocument doc;
    WindowsC windows;
    windows.bind_document("session", &doc);
    windows.bind_submit("prompt", [](void*, const char*, std::size_t) {});
    LayoutReport lr;
    std::optional<RolltuiLayout> lay = load_layout_c(R"({"name":"grow","focus":"prompt","root":{"column":[
        {"id":"tx","content":"transcript:session"},
        {"id":"prompt","content":"input:prompt","size":1,"focusable":true}]}})", lr);
    StackC s(*lay);
    const RolltuiRect box{0, 0, 20, 20};
    windows.prepare(s, box);
    check(s.find("prompt")->size == RolltuiSplitSize::fixed(RolltuiDim::abs(1)), "an empty input takes one row");
    set_input_text(windows.input("prompt"), "one\ntwo\nthree");
    windows.prepare(s, box);
    check(s.find("prompt")->size == RolltuiSplitSize::fixed(RolltuiDim::abs(3)), "three lines of text: three rows");
    set_input_text(windows.input("prompt"), std::string(30, 'x') + "\n" + std::string(30, 'y') + "\n" + std::string(200, 'z'));
    windows.prepare(s, box);
    check(s.find("prompt")->size == RolltuiSplitSize::fixed(RolltuiDim::abs(10)), "…and never more than half the parent's height");
    windows.bind_note("prompt", [](void*, RolltuiNote* out) { out->text = "working"; });
    set_input_text(windows.input("prompt"), "hi");
    windows.prepare(s, box);
    check(s.find("prompt")->size == RolltuiSplitSize::fixed(RolltuiDim::abs(1)), "a note that fits beside one row of text adds nothing");
    set_input_text(windows.input("prompt"), "a text that is much longer than the window");
    windows.prepare(s, box);
    check(s.find("prompt")->size == RolltuiSplitSize::fixed(RolltuiDim::abs(4)), "…and takes its own row under a wrapped one (3 text rows + 1)");
  }

  // ---- the grep control: no host resolves a content string itself ----------------------
  // WINDOWS ANSWERS "what does this slot mean?", from one table; a host never answers it
  // with an if-chain over `rn.node->content`. A host may still READ a content — the layout
  // editor SHOWS it — so the control is a count with a stated allowance, not a ban: an alias
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


  // ---- 9. a HOST REGISTERS A KIND --------------------------------------
  // The milestone's Done-when, driven through the real Windows. The whole claim is that
  // a host writes ONE CLASS and ONE REGISTRATION and the library then treats its widget
  // exactly like a built-in — so this test writes a real Widget (a two-cell "canvas"
  // that records the drags it is given and asks for a size), never a draw callback.
  std::printf("-- registered kinds: a host's own widget, built by the library\n");
  {
    clear_registered_widget_kinds_c();
    WindowsC windows;
    int built = 0;
    CanvasFactoryCtx canvas_ctx{windows, &built};
    std::string why;
    check(windows.register_kind("canvas", canvas_factory, &canvas_ctx, ROLLTUI_SOURCE_REQUIRED, "a drawing surface", &why),
          "a host registers a kind with one call [" + why + "]");

    // Rung 1 is never shadowed — guard one, the refusal, by name.
    why.clear();
    check(!windows.register_kind("input", canvas_factory, &canvas_ctx, ROLLTUI_SOURCE_REQUIRED, "", &why) &&
              why.find("'input' is one of the library's own kinds") != std::string::npos,
          "registering a LIBRARY kind is refused, by name [" + why + "]");
    // …and guard two, independently: even after that attempt, `input:prompt` is still the
    // library's input. This is the assertion the milestone's control breaks.
    {
      int rung = ROLLTUI_KIND_UNKNOWN;
      const std::optional<std::size_t> row = widget_kind_row_c("input", &rung);
      check(parse_content_c("input:prompt")->kind == "input" && row && *row < rolltui_widget_kind_library_count() &&
                rung == ROLLTUI_KIND_LIBRARY,
            "…and `input` still resolves at rung 1, inside the library's boundary: the library's rows are searched FIRST, whatever a host tried to register");
    }
    check(widget_kind_names_c().back() == "canvas" && widget_kind_names_c().size() == rolltui_widget_kind_library_count() + 1 &&
              rolltui_widget_kind_count(test_ctx()) == rolltui_widget_kind_library_count() + 1,
          "the registered kind is enumerable, after the library's, in resolution order");
    {
      // a host kind is a ROW past the library's boundary, and its rules are the
      // row's — the rung as an index, not a type; the shape as a stated default, not a guess.
      int rung = ROLLTUI_KIND_UNKNOWN;
      const std::optional<std::size_t> row = widget_kind_row_c("canvas", &rung);
      check(row && *row == rolltui_widget_kind_library_count() && rung == ROLLTUI_KIND_HOST && widget_kind_name_c(*row) == "canvas" &&
                rolltui_widget_kind_rule(test_ctx(), *row) == ROLLTUI_SOURCE_REQUIRED &&
                rolltui_widget_kind_source_shape(*row) == ROLLTUI_SOURCE_SHAPE_NAME &&
                std::string_view(rolltui_widget_kind_source_is(test_ctx(), *row, nullptr)) == "a drawing surface",
            "a registered kind is a row past the library's boundary, at rung 2, carrying its own rule, a Name-shaped source and its description");
    }

    LayoutReport lr;
    const std::optional<RolltuiLayout> lay = load_layout_c(R"({"name":"paint","root":{"column":[
        {"id":"a","content":"canvas:main","size":3,"focusable":true},
        {"id":"b","content":"canvas:main","size":3},
        {"id":"c","content":"canvas:other","size":3},
        {"id":"d","content":"nosuch:x","size":1}]}})", lr);
    check(lay && lr.clean(), "a layout naming a registered kind loads clean [" + (lr.bad_values_n == 0 ? std::string() : str_of(lr.bad_values[0])) + "]");
    StackC st(*lay);
    const RolltuiRect box{0, 0, 30, 10};
    const WindowsReportC rep = windows.prepare(st, box);
    check(built == 2, "two windows on ONE content share ONE instance; a second content is a second (" + std::to_string(built) + " built)");
    check(windows.at("a") == windows.at("b") && windows.at("a") != windows.at("c"), "…and that is what the two windows hold");
    check(windows.registered("canvas", "main") == windows.at("a"), "registered(kind, source) reaches it, like transcript(source)");
    // An unregistered kind gets the same answer as an unknown one: named in the report,
    // error panel drawn — never a blank window.
    check(rep.bad_values.size() == 1 && rep.bad_values[0].find("window 'd'") != std::string::npos &&
              rep.bad_values[0].find("'nosuch' is not a widget kind") != std::string::npos,
          "an UNregistered kind is a named bad value on the window [" + rep.summary() + "]");

    // Press / drag / release, including a drag far outside the window and off the screen:
    // the stack captures for the pressed window, so a registered kind gets drag-to-paint
    // with edge handling for free.
    Canvas* c = static_cast<Canvas*>(windows.at("a")->ctx);
    RolltuiMouseEvent m;
    m.kind = RolltuiMouseEvent::Kind::Press;
    m.x = 1; m.y = 1;
    RouteC r = st.route(m, box);
    check(r.kind == RouteC::Kind::Deliver && r.window == "a" && windows.handle(r.window, m), "a press reaches the registered widget");
    RolltuiMouseEvent d = m;
    d.kind = RolltuiMouseEvent::Kind::Drag;
    d.x = -5; d.y = 99;
    r = st.route(d, box);
    check(r.window == "a" && windows.handle(r.window, d), "…and a drag past its own edge, off the screen, still does");
    RolltuiMouseEvent up = d;
    up.kind = RolltuiMouseEvent::Kind::Release;
    r = st.route(up, box);
    check(r.window == "a" && windows.handle(r.window, up) && st.captured().empty(), "…and the release, which ends the capture");
    check(c->got == std::vector<std::string>{"press 1,1", "drag -5,99", "release -5,99"},
          "the widget saw all three, in order, with the coordinates it was given");

    // It sizes itself, and it reports its own problem — both through the same paths a
    // built-in uses (the input is the only built-in that asks for a size).
    c->want_rows = 5;
    windows.prepare(st, box);
    const std::vector<RolltuiResolvedNode> v = st.resolve(box);
    check(by_id(v, "a")->outer.h == 5, "a registered kind SIZES its window, like the input (" + std::to_string(by_id(v, "a")->outer.h) + ")");
    c->broken = true;
    const WindowsReportC pr = windows.prepare(st, box);
    check(pr.bad_values.size() == 3 && pr.summary().find("the canvas is broken") != std::string::npos,
          "…and its own problem() is reported like any built-in's — once per WINDOW showing it, plus the unregistered one [" +
              pr.summary() + "]");
    c->broken = false;

    // A layout RELOAD keeps the instance, for the reason it keeps a half-typed line: the
    // widget belongs to its content, not to the window that happened to show it.
    LayoutReport lr2;
    const std::optional<RolltuiLayout> other = load_layout_c(R"({"name":"paint2","root":{"column":[
        {"id":"z","content":"canvas:main","size":3,"focusable":true}]}})", lr2);
    StackC st2(*other);
    const int before = built;
    windows.prepare(st2, box);
    check(built == before && windows.at("z")->ctx == c, "a layout reload keeps the pixels: same instance under a new window id");
    clear_registered_widget_kinds_c();
  }

  // ---- the scrollbar's geometry ----------------------------------------
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
    // A widget OPTS IN, the window owns the bar, and a widget that only REPORTS gets a bar
    // that is not a handle.
    {
      RolltuiDocument doc;
      for (int i = 0; i < 200; ++i) {
        RolltuiDocEntry e;
        set_str(e.id, "b" + std::to_string(i));
        set_str(e.text, "line " + std::to_string(i));
        e.markdown = false;
        doc.push_back(e);
      }
      WindowsC windows;
      windows.bind_document("session", &doc);
      LayoutReport lr;
      const std::optional<RolltuiLayout> lay = load_layout_c(
          R"({"name":"bar","min_width":0,"min_height":0,"actions":{},"root":{"column":[
             {"id":"t","content":"transcript:session","border":"single","focusable":true}]}})", lr);
      check(lay && lr.clean(), "a one-window layout for the bar");
      StackC s(*lay);
      const RolltuiRect box{0, 0, 40, 12};
      windows.prepare(s, box);
      ThemeFixture th;
      builtin_theme_c("default-dark", th);
      FrameC f(40, 12);
      for (const RolltuiResolvedNode& rn : s.resolve(box)) windows.draw(rn, f, th);
      RolltuiTranscript* tr = windows.transcript("session");
      rolltui_transcript_scroll_to_top(tr);
      windows.prepare(s, box);
      for (const RolltuiResolvedNode& rn : s.resolve(box)) windows.draw(rn, f, th);
      check(rolltui_transcript_top_line(tr) == 0, "at the top");
      // The thumb is IN the right border column, which the widget never sees.
      const int track_x = 39;
      bool thumb_drawn = false, thumb_coloured = false;
      // ANY OF THE CAPSULE'S FOUR CELLS COUNTS. A thumb is `single` alone, or `top`/`middle`/
      // `bottom` — asserting one glyph would make this test a statement about the thumb's LENGTH,
      // which is the layout's business and not this check's.
      const RolltuiScrollbarGlyphs* sg = rolltui_windows_scrollbar_glyphs(windows.handle());
      for (int y = 1; y < 11; ++y) {
        const std::string_view got = f.glyph(track_x, y);
        if (got == sg->single || got == sg->top || got == sg->middle || got == sg->bottom) {
          thumb_drawn = true;
          // THE GLYPH IS NOT ENOUGH. With the glyph asserted and the colour not,
          // `RolltuiWindowRoles::scrollbar` could name any role at all and every suite would
          // stay green. Pointing it at `error` turns this red.
          if (f.at(track_x, y).style.fg == th.style(ROLLTUI_ROLE_SCROLLBAR).fg) thumb_coloured = true;
        }
      }
      check(thumb_drawn, "the window drew a thumb in its right border column");
      check(thumb_coloured && th.style(ROLLTUI_ROLE_SCROLLBAR).fg != th.style(ROLLTUI_ROLE_ERROR).fg,
            "…painted with `scrollbar`, the role the window draws its own chrome with");
      // A press near the BOTTOM of the track scrolls the transcript — the window
      // commanding a widget that accepted scroll_to().
      RolltuiMouseEvent m;
      m.kind = RolltuiMouseEvent::Kind::Press;
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
      m.kind = RolltuiMouseEvent::Kind::Drag;
      m.y = 1;
      check(windows.handle("t", m), "a drag on the thumb keeps being consumed");
      windows.prepare(s, box);
      check(rolltui_transcript_top_line(tr) < after_press, "…and dragging up scrolls up");
      m.kind = RolltuiMouseEvent::Kind::Release;
      check(windows.handle("t", m), "the release ends the drag");
      // A press one column INSIDE the track is the text's, not the bar's.
      m.kind = RolltuiMouseEvent::Kind::Press;
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
