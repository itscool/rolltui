// rolltui/WidgetsCpp.cpp — the C++ side of the widget vtable's host, behind the same
// boundary as `c/rolltui_widgets.c` (rolltui/c/rolltui_widgets.h, Phase 15 m5). One CMake flag
// picks which of the two links; both satisfy `rolltui/tests/layout_test.cpp`, the files-only
// proof, the authored screen and every golden frame.
//
// It is deliberately NOT a transliteration of the C: it keeps `std::map`, `std::vector` and
// `std::string`, and the per-window slot is a value in a map rather than a pointer in a table.
#include "rolltui/c/rolltui_widgets.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/c/rolltui_layout.h"

namespace {

std::string_view view(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }
std::string_view view(const char* p, std::size_t n) { return std::string_view(p ? p : "", n); }

}  // namespace

// ---- the scrollbar's geometry ------------------------------------------------------------

extern "C" int rolltui_scroll_thumb(const RolltuiScrollExtent* e, int track, RolltuiScrollThumb* out) {
  // No bar when there is nothing to scroll, or nowhere to draw one. Both are answers, not
  // edge cases: a bar on a document that fits is a lie about there being more.
  if (track <= 0 || e->total == 0 || e->visible == 0 || e->total <= e->visible) return 0;
  const double frac = static_cast<double>(e->visible) / static_cast<double>(e->total);
  int len = static_cast<int>(frac * track + 0.5);
  if (len < 1) len = 1;  // always visible: a 1-cell thumb still says where you are
  if (len > track) len = track;
  const std::size_t max_first = e->total - e->visible;
  const std::size_t first = e->first > max_first ? max_first : e->first;
  const int span = track - len;
  int off = span <= 0 ? 0
                      : static_cast<int>(static_cast<double>(first) / static_cast<double>(max_first) * span + 0.5);
  if (off < 0) off = 0;
  if (off > span) off = span;
  // THE GUARANTEE: the thumb touches an end IF AND ONLY IF the view is at that end. The end
  // cells are RESERVED for the ends and everything between is squeezed into what is left;
  // below a 2-cell span there is nothing to reserve and the honest answer is the ends alone.
  if (span >= 2) {
    if (first == 0) off = 0;
    else if (first == max_first) off = span;
    else off = std::clamp(off, 1, span - 1);
  } else {
    off = (first == max_first) ? span : 0;
  }
  out->offset = off;
  out->length = len;
  return 1;
}

extern "C" std::size_t rolltui_scroll_first_for_cell(const RolltuiScrollExtent* e, int track, int cell) {
  if (e->total <= e->visible || track <= 0) return 0;
  const std::size_t max_first = e->total - e->visible;
  RolltuiScrollThumb t;
  const int len = rolltui_scroll_thumb(e, track, &t) ? t.length : 1;
  const int span = track - len;
  if (span <= 0) return cell <= 0 ? 0 : max_first;
  const int c = std::clamp(cell, 0, span);
  const double f = static_cast<double>(c) / static_cast<double>(span) * static_cast<double>(max_first) + 0.5;
  const std::size_t first = static_cast<std::size_t>(f);
  return first > max_first ? max_first : first;
}

extern "C" void rolltui_content_rect(const RolltuiResolvedNode* rn, RolltuiRect* out) {
  *out = rn->inner;
  if (rn->node->border != rolltui::Border::None && out->w >= 3) {
    out->x += 1;
    out->w -= 2;
  }
}

// ---- the host ------------------------------------------------------------------------------

namespace {

struct Kind {
  RolltuiWidgetFactory factory = nullptr;
  void* ctx = nullptr;
};

struct Track {
  int x = 0, y = 0, h = 0;
};

struct Slot {
  RolltuiWidget* widget = nullptr;  // BORROWED from `by_content`
  std::string content;
  Track track;
  bool live = false;
};

// OWNED: the table destroys every widget it built, through the vtable's `destroy`.
struct OwnedWidget {
  RolltuiWidget w{};
  ~OwnedWidget() {
    if (w.vt && w.vt->destroy && w.self) w.vt->destroy(w.self);
  }
};

}  // namespace

struct RolltuiWindows {
  std::map<std::string, std::unique_ptr<OwnedWidget>> by_content;
  std::map<std::string, Slot> by_window;
  std::map<std::string, Kind> kinds;
  RolltuiWidgetFactory error_factory = nullptr;
  void* error_ctx = nullptr;
  RolltuiWidgetFactory panel_factory = nullptr;
  void* panel_ctx = nullptr;
  RolltuiWidgetEnv env;
  std::vector<std::string> report;
  RolltuiStr scratch;  // the widget's answer, and the prefix, built only when there is one
  std::string bar_drag;
  int bar_grab = 0;
  std::vector<RolltuiResolvedNode> nodes;
};

extern "C" RolltuiWindows* rolltui_windows_new(void) {
  return std::make_unique<RolltuiWindows>().release();
}

extern "C" void rolltui_windows_free(RolltuiWindows* w) { const std::unique_ptr<RolltuiWindows> owned(w); }

extern "C" void rolltui_windows_register_kind(RolltuiWindows* w, const char* name, std::size_t len,
                                              RolltuiWidgetFactory factory, void* ctx) {
  w->kinds[std::string(view(name, len))] = Kind{factory, ctx};
}

extern "C" void rolltui_windows_set_error_factory(RolltuiWindows* w, RolltuiWidgetFactory factory, void* ctx) {
  w->error_factory = factory;
  w->error_ctx = ctx;
}

extern "C" void rolltui_windows_set_panel_factory(RolltuiWindows* w, RolltuiWidgetFactory factory, void* ctx) {
  w->panel_factory = factory;
  w->panel_ctx = ctx;
}

extern "C" void rolltui_windows_set_env(RolltuiWindows* w, const RolltuiWidgetEnv* env) { w->env = *env; }
extern "C" const RolltuiWidgetEnv* rolltui_windows_env(const RolltuiWindows* w) { return &w->env; }

extern "C" RolltuiWidget* rolltui_windows_widget_for(RolltuiWindows* w, const char* content,
                                                     std::size_t len) {
  const std::string key(view(content, len));
  auto it = w->by_content.find(key);
  if (it != w->by_content.end()) return &it->second->w;
  const std::size_t colon = key.find(':');
  const std::string kind = key.substr(0, colon);
  RolltuiWidget built{};
  if (auto k = w->kinds.find(kind); k != w->kinds.end()) built = k->second.factory(k->second.ctx, content, len);
  // NOTHING BUILT IS NOT AN ERROR PATH: the error factory draws the reason.
  if (!built.vt && w->error_factory) built = w->error_factory(w->error_ctx, content, len);
  auto owned = std::make_unique<OwnedWidget>();
  owned->w = built;
  RolltuiWidget* raw = &owned->w;
  w->by_content[key] = std::move(owned);
  return raw;
}

extern "C" RolltuiWidget* rolltui_windows_at(const RolltuiWindows* w, const char* window, std::size_t len) {
  auto it = w->by_window.find(std::string(view(window, len)));
  return it == w->by_window.end() ? nullptr : it->second.widget;
}

extern "C" const char* rolltui_windows_content_at(const RolltuiWindows* w, const char* window,
                                                  std::size_t len, std::size_t* out_len) {
  auto it = w->by_window.find(std::string(view(window, len)));
  if (it == w->by_window.end()) {
    if (out_len) *out_len = 0;
    return nullptr;
  }
  if (out_len) *out_len = it->second.content.size();
  return it->second.content.c_str();
}

// ---- sync ------------------------------------------------------------------------------------

extern "C" std::size_t rolltui_windows_report_count(const RolltuiWindows* w) { return w->report.size(); }

extern "C" const char* rolltui_windows_report_at(const RolltuiWindows* w, std::size_t i, std::size_t* len) {
  if (i >= w->report.size()) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = w->report[i].size();
  return w->report[i].c_str();
}

namespace {

void sync_node(RolltuiWindows* w, const RolltuiLayoutNode& n) {
  if (n.kind != RolltuiLayoutNode::Kind::Window) {
    for (const RolltuiLayoutNode& c : n.children) sync_node(w, c);
    return;
  }
  RolltuiWidget* wd = rolltui_windows_widget_for(w, n.content.p, n.content.n);
  Slot& s = w->by_window[std::string(view(n.id))];
  s.widget = wd;
  s.live = true;
  s.content.assign(view(n.content));
  // The "window 'x' (content 'y'): " prefix is built ONLY when there is something to say.
  auto say = [&](std::string_view what) {
    w->report.push_back("window '" + std::string(view(n.id)) + "' (content '" + std::string(view(n.content)) +
                        "'): " + std::string(what));
  };
  if (wd->vt && wd->vt->problem && wd->vt->problem(wd->self, &w->scratch)) {
    say(view(w->scratch));
    return;
  }
  if (wd->vt && wd->vt->note_at)
    for (std::size_t i = 0; wd->vt->note_at(wd->self, i, &w->scratch); ++i) say(view(w->scratch));
}

}  // namespace

extern "C" void rolltui_windows_sync(RolltuiWindows* w, const RolltuiWindowStack* stack) {
  w->report.clear();
  // The map is REBUILT IN PLACE, not cleared: `clear()` destroys every node and the next
  // frame allocates them again for a window set that almost never changes.
  for (auto& [id, s] : w->by_window) s.live = false;
  for (std::size_t i = 0; i < rolltui_window_stack_depth(stack); ++i)
    sync_node(w, rolltui_window_stack_layer(stack, i)->root);
  for (auto it = w->by_window.begin(); it != w->by_window.end();)
    it = it->second.live ? std::next(it) : w->by_window.erase(it);
}

// ---- the per-frame resolve buffer ---------------------------------------------------------------

namespace {

void collect(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<RolltuiResolvedNode>*>(ctx)->push_back(*rn);
}

void resolve_into(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box) {
  w->nodes.clear();
  rolltui_window_stack_resolve(stack, box, collect, &w->nodes);
}

}  // namespace

extern "C" void rolltui_windows_autosize(RolltuiWindows* w, RolltuiWindowStack* stack, RolltuiRect box) {
  resolve_into(w, stack, box);
  for (const RolltuiResolvedNode& rn : w->nodes) {
    if (rn.node->kind != RolltuiLayoutNode::Kind::Window) continue;
    RolltuiWidget* wd = rolltui_windows_at(w, rn.node->id.p, rn.node->id.n);
    if (!wd || !wd->vt || !wd->vt->desired_outer) continue;
    // The parent split is the INNERMOST one that contains this window — the last in tree
    // order, since a container precedes its children and siblings never overlap.
    const RolltuiResolvedNode* parent = nullptr;
    for (const RolltuiResolvedNode& p : w->nodes)
      if (p.node->kind != RolltuiLayoutNode::Kind::Window && p.layer == rn.layer &&
          p.inner.contains(rn.outer.x, rn.outer.y))
        parent = &p;
    const bool row = parent && parent->node->kind == RolltuiLayoutNode::Kind::Row;
    const int extent = !parent ? box.h : (row ? parent->inner.w : parent->inner.h);
    const int border = rn.node->border != rolltui::Border::None ? 2 : 0;
    int want = 0;
    if (wd->vt->desired_outer(wd->self, rn.inner.w, extent, border, &want))
      if (RolltuiLayoutNode* nd = rolltui_window_stack_find(stack, rn.node->id.p, rn.node->id.n))
        nd->size = RolltuiSplitSize::fixed(RolltuiDim::abs(want));
  }
}

extern "C" void rolltui_windows_layout(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box) {
  resolve_into(w, stack, box);
  for (const RolltuiResolvedNode& rn : w->nodes) {
    if (rn.node->kind != RolltuiLayoutNode::Kind::Window) continue;
    RolltuiWidget* wd = rolltui_windows_at(w, rn.node->id.p, rn.node->id.n);
    if (wd && wd->vt && wd->vt->layout) wd->vt->layout(wd->self, &rn);
  }
}

// ---- drawing --------------------------------------------------------------------------------------

namespace {

// The bar lives in the window's RIGHT BORDER COLUMN, which is why the WINDOW draws it and not
// the widget: a widget is handed a content rect and knows nothing about whether it has a
// border. A window without a border has no track and gets no bar.
void draw_scrollbar(RolltuiWindows* w, const RolltuiResolvedNode& rn, RolltuiWidget* wd, RolltuiFrame* f,
                    const RolltuiStyle* styles, const RolltuiWindowRoles* roles) {
  auto it = w->by_window.find(std::string(view(rn.node->id)));
  // The track is ZEROED, not erased: an erase-and-reinsert destroys a map node and allocates
  // a new one every frame for every window with a scrollbar. `h == 0` means no track.
  if (it != w->by_window.end()) it->second.track = Track{};
  if (rn.node->border == rolltui::Border::None) return;
  if (!wd->vt || !wd->vt->scroll_extent) return;
  RolltuiScrollExtent e{};
  if (!wd->vt->scroll_extent(wd->self, ROLLTUI_AXIS_VERTICAL, &e)) return;
  const int track = rn.outer.h - 2;  // between the corners
  const int x = rn.outer.x + rn.outer.w - 1;
  if (track <= 0 || rn.outer.w < 2) return;
  RolltuiScrollThumb t;
  if (!rolltui_scroll_thumb(&e, track, &t)) return;
  if (it != w->by_window.end()) it->second.track = Track{x, rn.outer.y + 1, track};
  RolltuiStyle s = styles[roles->scrollbar];
  const RolltuiStyle ground = styles[static_cast<unsigned char>(rn.node->background)];
  if (s.bg.kind == RolltuiStyleColor::Kind::None) s.bg = ground.bg;
  // █ (U+2588) is East Asian AMBIGUOUS, exactly like the box-drawing set the border is made
  // of — so it follows the border's rule: with `ambiguous_wide` the thumb is ASCII.
  const std::string_view thumb = w->env.ambiguous_wide ? "#" : "\xE2\x96\x88";
  for (int i = 0; i < t.length; ++i) {
    const int y = rn.outer.y + 1 + t.offset + i;
    if (y >= rn.outer.y + rn.outer.h - 1) break;
    rolltui_frame_put(f, x, y, thumb.data(), thumb.size(), 1, s, 0);
  }
}

}  // namespace

extern "C" void rolltui_windows_draw(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                                     const RolltuiStyle* styles, const RolltuiWindowRoles* roles) {
  if (rn->node->kind != RolltuiLayoutNode::Kind::Window) return;
  RolltuiWidget* wd = rolltui_windows_at(w, rn->node->id.p, rn->node->id.n);
  if (!wd || !wd->vt) return;
  // A widget that CANNOT draw is replaced by the error panel — the factory's, so there is one
  // definition of what "this window is wrong" looks like.
  if (wd->vt->problem && wd->vt->problem(wd->self, &w->scratch)) {
    if (w->panel_factory) {
      RolltuiWidget err = w->panel_factory(w->panel_ctx, w->scratch.p, w->scratch.n);
      if (err.vt) {
        if (err.vt->layout) err.vt->layout(err.self, rn);
        err.vt->draw(err.self, rn, f);
        if (err.vt->destroy) err.vt->destroy(err.self);
      }
    }
    return;
  }
  wd->vt->draw(wd->self, rn, f);
  draw_scrollbar(w, *rn, wd, f, styles, roles);
}

// ---- events ------------------------------------------------------------------------------------------

namespace {

// A press in the track column drives the widget — but ONLY one that accepted `scroll_to`. One
// that merely reports gets an accurate bar that is not a handle (the vtable's rule 4).
int handle_scrollbar(RolltuiWindows* w, std::string_view window, RolltuiWidget* wd, const RolltuiEvent* ev) {
  if (ev->kind != ROLLTUI_EVENT_MOUSE) return 0;
  const RolltuiMouseEvent& m = ev->mouse;
  const std::string id(window);
  if (m.kind == RolltuiMouseEvent::Kind::Release) {
    if (w->bar_drag != id) return 0;
    w->bar_drag.clear();
    return 1;
  }
  auto it = w->by_window.find(id);
  if (it == w->by_window.end() || it->second.track.h <= 0) return 0;  // h == 0: no track drawn
  const Track& tr = it->second.track;
  if (!wd->vt || !wd->vt->scroll_extent) return 0;
  RolltuiScrollExtent e{};
  if (!wd->vt->scroll_extent(wd->self, ROLLTUI_AXIS_VERTICAL, &e)) return 0;
  RolltuiScrollThumb t;
  if (!rolltui_scroll_thumb(&e, tr.h, &t)) return 0;
  if (m.kind == RolltuiMouseEvent::Kind::Press) {
    if (m.button != 1 || m.x != tr.x || m.y < tr.y || m.y >= tr.y + tr.h) return 0;
    const int cell = m.y - tr.y;
    // On the thumb: grab it where it was taken, so it does not jump under the pointer. In the
    // trough: jump so the thumb's START lands there, which is the one rule that makes a click
    // and the drag that may follow it agree.
    w->bar_grab = (cell >= t.offset && cell < t.offset + t.length) ? cell - t.offset : 0;
    if (!wd->vt->scroll_to) return 0;
    if (!wd->vt->scroll_to(wd->self, ROLLTUI_AXIS_VERTICAL,
                           rolltui_scroll_first_for_cell(&e, tr.h, cell - w->bar_grab)))
      return 0;
    w->bar_drag = id;
    return 1;
  }
  if (m.kind == RolltuiMouseEvent::Kind::Drag) {
    if (w->bar_drag != id) return 0;
    // The pointer may be anywhere by now (the press captured it), so only its ROW counts.
    if (wd->vt->scroll_to)
      wd->vt->scroll_to(wd->self, ROLLTUI_AXIS_VERTICAL,
                        rolltui_scroll_first_for_cell(&e, tr.h, m.y - tr.y - w->bar_grab));
    return 1;
  }
  return 0;
}

}  // namespace

extern "C" int rolltui_windows_handle(RolltuiWindows* w, const char* window, std::size_t len,
                                      const RolltuiEvent* e) {
  RolltuiWidget* wd = rolltui_windows_at(w, window, len);
  if (!wd || !wd->vt) return 0;
  if (wd->vt->problem && wd->vt->problem(wd->self, &w->scratch)) return 0;
  if (handle_scrollbar(w, view(window, len), wd, e)) return 1;
  return wd->vt->handle ? wd->vt->handle(wd->self, e) : 0;
}
