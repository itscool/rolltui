// rolltui/tools/layout_editor.cpp — see layout_editor.hpp.
#include "layout_editor.hpp"

#include <algorithm>
#include <cstdlib>

namespace rolltui::tools {

// ---- tree helpers ---------------------------------------------------------------------

Node* LayoutEditor::find_node(Node& root, std::string_view id) {
  if (root.id == id) return &root;
  for (Node& c : root.children)
    if (Node* f = find_node(c, id)) return f;
  return nullptr;
}
const Node* LayoutEditor::find_node(const Node& root, std::string_view id) { return find_node(const_cast<Node&>(root), id); }

Node* LayoutEditor::parent_of(Node& root, std::string_view id, std::size_t* index) {
  for (std::size_t i = 0; i < root.children.size(); ++i) {
    if (root.children[i].id == id) { if (index) *index = i; return &root; }
    if (Node* p = parent_of(root.children[i], id, index)) return p;
  }
  return nullptr;
}

std::vector<std::string> LayoutEditor::ids_in_order(const Node& root) {
  std::vector<std::string> out;
  auto walk = [&](auto& self, const Node& n) -> void {
    if (!n.id.empty()) out.push_back(n.id);
    for (const Node& c : n.children) self(self, c);
  };
  walk(walk, root);
  return out;
}

std::string LayoutEditor::unique_id(const std::string& base) const {
  const std::vector<std::string> ids = ids_in_order(current_.base.root);
  auto taken = [&](const std::string& s) { return std::find(ids.begin(), ids.end(), s) != ids.end(); };
  if (!taken(base)) return base;
  for (int n = 2;; ++n) {
    const std::string s = base + "-" + std::to_string(n);
    if (!taken(s)) return s;
  }
}

// ---- construction ---------------------------------------------------------------------

// A size as typed: fill | fill N | N% (± cells) | a bare integer, which is cells (the
// file format says so with a JSON number; typed, it can only be a string).
static std::optional<SplitSize> parse_size_text(const std::string& text) {
  if (std::optional<SplitSize> s = parse_split_size(text)) return s;
  char* end = nullptr;
  const long v = std::strtol(text.c_str(), &end, 10);
  if (!text.empty() && end && *end == '\0' && v >= 0) return SplitSize::fixed(Dim::abs(static_cast<int>(v)));
  return std::nullopt;
}

LayoutEditor::LayoutEditor() {
  current_ = *builtin_layout("default");
  undo_.reset(current_);
  select_next();
  rebuild_menu();
}

void LayoutEditor::load(const Layout& layout) {
  current_ = layout;
  undo_.reset(current_);
  preview_.reset();
  sel_.clear();
  select_next();
  rebuild_menu();
  status_ = "loaded " + layout.name;
}

void LayoutEditor::set_slots(std::vector<std::string> slots) {
  slots_ = std::move(slots);
  std::vector<MenuItem> opts;
  for (const std::string& s : slots_) opts.push_back(MenuItem::action(s, s));
  menu_.set_options("content", std::move(opts));
}

void LayoutEditor::set_layouts(std::vector<std::string> names) {
  layouts_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : layouts_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("load", std::move(opts));
}

// ---- selection ------------------------------------------------------------------------

const Node* LayoutEditor::selected_node() const { return find_node(current_.base.root, sel_); }
Node* LayoutEditor::sel_node() { return find_node(current_.base.root, sel_); }

void LayoutEditor::select(std::string_view id) {
  if (find_node(current_.base.root, id)) { sel_ = std::string(id); sync_values(); }
}

void LayoutEditor::select_next(bool backwards) {
  const std::vector<std::string> ids = ids_in_order(current_.base.root);
  if (ids.empty()) { sel_.clear(); return; }
  auto it = std::find(ids.begin(), ids.end(), sel_);
  std::size_t i = it == ids.end() ? (backwards ? ids.size() - 1 : 0) : static_cast<std::size_t>(it - ids.begin());
  if (it != ids.end()) i = backwards ? (i == 0 ? ids.size() - 1 : i - 1) : (i + 1) % ids.size();
  sel_ = ids[i];
  sync_values();
}

// ---- the menu -------------------------------------------------------------------------

void LayoutEditor::rebuild_menu() {
  std::vector<MenuItem> borders, anchors, slots, loads;
  for (const char* b : {"none", "single", "rounded", "double", "heavy"}) borders.push_back(MenuItem::action(b, b));
  for (const char* a : {"top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right"}) anchors.push_back(MenuItem::action(a, a));
  for (const std::string& s : slots_) slots.push_back(MenuItem::action(s, s));
  for (const std::string& n : layouts_) loads.push_back(MenuItem::action(n, n));
  std::vector<MenuItem> popups;
  for (const Layer& p : current_.popups) {
    popups.push_back(MenuItem::submenu("popup." + p.id, p.id,
                                       {MenuItem::input("popup." + p.id + ".x", "x", dim_to_string(p.placement.x)), MenuItem::input("popup." + p.id + ".y", "y", dim_to_string(p.placement.y)),
                                        MenuItem::input("popup." + p.id + ".w", "w", dim_to_string(p.placement.w)), MenuItem::input("popup." + p.id + ".h", "h", dim_to_string(p.placement.h)),
                                        MenuItem::choice("popup." + p.id + ".anchor", "anchor", anchors, std::string(anchor_name(p.placement.anchor))),
                                        MenuItem::toggle("popup." + p.id + ".modal", "modal", p.modal), MenuItem::action("popup." + p.id + ".remove", "remove this popup")}));
  }
  popups.push_back(MenuItem::input("popup.add", "add a popup (id)"));
  MenuItem root = MenuItem::submenu(
      "root", "layout editor",
      {MenuItem::action("next", "Select the next node", "Tab"), MenuItem::action("prev", "Select the previous node", "Shift-Tab"),
       MenuItem::action("split_row", "Split into a row (side by side)"), MenuItem::action("split_column", "Split into a column (stacked)"),
       MenuItem::action("swap_prev", "Swap with the previous sibling"), MenuItem::action("swap_next", "Swap with the next sibling"),
       MenuItem::toggle("visible", "Visible", true), MenuItem::choice("border", "Border", borders, "single"), MenuItem::input("title", "Title"),
       MenuItem::choice("content", "Content slot", slots, "transcript"), MenuItem::input("size", "Size (fill | fill N | N% | cells; Alt+arrows nudge)"),
       MenuItem::toggle("focusable", "Focusable", false), MenuItem::action("delete", "Delete this node"),
       MenuItem::submenu("popups", "Popups", std::move(popups)),
       MenuItem::action("undo", "Undo", "Ctrl-Z"), MenuItem::action("redo", "Redo", "Ctrl-Y"),
       MenuItem::choice("load", "Load layout", std::move(loads), ""), MenuItem::input("save", "Save layout file as (layouts/<name>.json)"),
       MenuItem::action("reset_loaded", "Reset to the loaded layout\xE2\x80\xA6")});
  menu_.set_root(std::move(root));
  sync_values();
}

void LayoutEditor::sync_values() {
  const Node* n = selected_node();
  if (!n) return;
  menu_.set_checked("visible", n->visible);
  menu_.set_value("border", std::string(border_name(n->border)));
  menu_.set_value("title", n->title);
  menu_.set_value("content", n->content);
  menu_.set_value("size", split_size_to_string(n->size));
  menu_.set_checked("focusable", n->focusable);
  menu_.set_enabled("content", n->is_window());
  menu_.set_enabled("focusable", n->is_window());
  if (MenuItem* root = menu_.find("root")) root->label = "layout editor \xE2\x80\xA2 " + sel_ + (n->is_window() ? "" : n->kind == Node::Kind::Row ? " (row)" : " (column)");
}

// ---- preview / commit -----------------------------------------------------------------

void LayoutEditor::begin_preview() { if (!preview_) preview_ = current_; }

void LayoutEditor::cancel_preview() {
  if (!preview_) return;
  current_ = *preview_;
  preview_.reset();
}

LayoutEditor::Outcome LayoutEditor::commit_current() {
  preview_.reset();
  if (current_ == undo_.current()) { sync_values(); return {Outcome::Kind::Changed, {}}; }
  undo_.commit(current_);
  sync_values();
  return {Outcome::Kind::Committed, {}};
}

void LayoutEditor::replace(Layout l) {
  preview_.reset();
  current_ = std::move(l);
  undo_.commit(current_);
  if (!find_node(current_.base.root, sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
}

bool LayoutEditor::undo() {
  cancel_preview();
  if (!undo_.undo()) return false;
  current_ = undo_.current();
  if (!find_node(current_.base.root, sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
  status_ = "undone";
  return true;
}

bool LayoutEditor::redo() {
  cancel_preview();
  if (!undo_.redo()) return false;
  current_ = undo_.current();
  if (!find_node(current_.base.root, sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
  status_ = "redone";
  return true;
}

// ---- operations -----------------------------------------------------------------------

bool LayoutEditor::apply_op(Op op) {
  Node& root = current_.base.root;
  std::size_t idx = 0;
  Node* parent = parent_of(root, sel_, &idx);
  Node* n = sel_node();
  if (!n) return false;
  switch (op) {
    case Op::SplitRow:
    case Op::SplitColumn: {
      Node copy = *n;
      copy.id = unique_id(n->id);
      copy.size = SplitSize::filling();
      if (!copy.title.empty()) copy.title = copy.id;  // so the two panes read apart
      Node first = *n;
      first.size = SplitSize::filling();
      Node split = op == Op::SplitRow ? Node::row({first, copy}) : Node::column({first, copy});
      split.size = n->size;
      split.id = unique_id(n->id + (op == Op::SplitRow ? "-row" : "-column"));
      *n = std::move(split);
      status_ = "split " + sel_ + (op == Op::SplitRow ? " side by side" : " stacked");
      sel_ = n->children[0].id;
      return true;
    }
    case Op::SwapPrev:
    case Op::SwapNext: {
      if (!parent) { status_ = "the root has no siblings"; return false; }
      const std::size_t other = op == Op::SwapPrev ? (idx == 0 ? idx : idx - 1) : idx + 1;
      if (other == idx || other >= parent->children.size()) { status_ = "no sibling on that side"; return false; }
      std::swap(parent->children[idx], parent->children[other]);
      return true;
    }
    case Op::ToggleVisible:
      n->visible = !n->visible;
      return true;
    case Op::ToggleFocusable:
      n->focusable = !n->focusable;
      return true;
    case Op::Delete: {
      if (!parent) { status_ = "the root cannot be deleted"; return false; }
      parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(idx));
      // A split with one child left collapses into that child (keeping the split's size).
      if (parent->children.size() == 1 && parent != &root) {
        Node only = parent->children[0];
        only.size = parent->size;
        *parent = std::move(only);
      } else if (parent->children.size() == 1) {
        Node only = parent->children[0];
        only.size = SplitSize::filling();
        root = std::move(only);
      }
      sel_.clear();
      select_next();
      return true;
    }
  }
  return false;
}

void LayoutEditor::begin_drag(std::string_view id) {
  if (!find_node(current_.base.root, id)) return;
  begin_preview();
  drag_ = std::string(id);
  sel_ = std::string(id);
  sync_values();
}

void LayoutEditor::drag_to(int extent) {
  if (!drag_) return;
  if (Node* n = find_node(current_.base.root, *drag_)) n->size = SplitSize::fixed(Dim::abs(std::max(extent, 1)));
}

LayoutEditor::Outcome LayoutEditor::end_drag() {
  if (!drag_) return {Outcome::Kind::None, {}};
  drag_.reset();
  return commit_current();
}

std::string LayoutEditor::status_line() const {
  std::string s = preview_ ? "previewing \xE2\x80\x94 Enter commits, Esc cancels" : (status_.empty() ? "Enter commits, Esc cancels" : status_);
  s += " \xC2\xB7 undo " + std::to_string(undo_.undo_depth()) + " \xC2\xB7 redo " + std::to_string(undo_.redo_depth());
  return s;
}

// ---- events ---------------------------------------------------------------------------

LayoutEditor::Outcome LayoutEditor::handle(const Event& e, const Bindings& nav) {
  using K = MenuEvent::Kind;
  using O = Outcome::Kind;
  if (const auto* k = std::get_if<KeyEvent>(&e)) {
    // An undo or redo changes the COMMITTED value: the host writes it to the store.
    const std::string_view ed = nav.action_for(*k, "editor");
    if (ed == "editor.undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ed == "editor.redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    const std::string_view st = nav.action_for(*k, "stack");
    if ((st == "stack.focus_next" || st == "stack.focus_prev") && !menu_.editing()) { select_next(st == "stack.focus_prev"); return {O::Changed, {}}; }
    if (k->alt && !k->ctrl && (k->key == Key::Left || k->key == Key::Right || k->key == Key::Up || k->key == Key::Down)) {
      // Nudge the selected node's size by one cell along its parent's axis.
      Node* n = sel_node();
      Node* parent = parent_of(current_.base.root, sel_);
      if (!n || !parent) { status_ = "the root has no size to nudge"; return {O::Changed, {}}; }
      const bool horizontal = parent->kind == Node::Kind::Row;
      const int delta = (k->key == Key::Right || k->key == Key::Down) ? 1 : -1;
      if ((horizontal && (k->key == Key::Up || k->key == Key::Down)) || (!horizontal && (k->key == Key::Left || k->key == Key::Right))) {
        status_ = std::string("this node sizes ") + (horizontal ? "left/right" : "up/down");
        return {O::Changed, {}};
      }
      begin_preview();
      if (n->size.fill) n->size = SplitSize::fixed(Dim::abs(20));  // a fill has no number; start from a visible one
      n->size.dim.cells = std::max(1, n->size.dim.cells + delta);
      status_ = "size " + split_size_to_string(n->size);
      return commit_current();
    }
  }
  status_.clear();
  const MenuEvent ev = menu_.handle(e, nav);
  if (ev.kind == K::Activate) {
    if (ev.id == "next") { select_next(); return {O::Changed, {}}; }
    if (ev.id == "prev") { select_next(true); return {O::Changed, {}}; }
    if (ev.id == "undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ev.id == "redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ev.id == "reset_loaded") return {O::ResetLoaded, {}};
    static const std::pair<const char*, Op> ops[] = {{"split_row", Op::SplitRow}, {"split_column", Op::SplitColumn}, {"swap_prev", Op::SwapPrev},
                                                     {"swap_next", Op::SwapNext}, {"delete", Op::Delete}};
    for (const auto& [id, op] : ops)
      if (ev.id == id) {
        begin_preview();
        if (!apply_op(op)) { cancel_preview(); return {O::Changed, {}}; }
        Outcome o = commit_current();
        rebuild_menu();
        return o;
      }
    if (ev.id.rfind("popup.", 0) == 0 && ev.id.size() > 7 && ev.id.substr(ev.id.size() - 7) == ".remove") {
      const std::string pid = ev.id.substr(6, ev.id.size() - 13);
      begin_preview();
      current_.popups.erase(std::remove_if(current_.popups.begin(), current_.popups.end(), [&](const Layer& p) { return p.id == pid; }), current_.popups.end());
      status_ = "removed popup " + pid;
      Outcome o = commit_current();
      rebuild_menu();
      return o;
    }
    return {O::None, {}};
  }
  if (ev.kind == K::Toggle) {
    if (ev.id == "visible" || ev.id == "focusable") {
      begin_preview();
      apply_op(ev.id == "visible" ? Op::ToggleVisible : Op::ToggleFocusable);
      return commit_current();
    }
    if (ev.id.rfind("popup.", 0) == 0) {
      const std::string pid = ev.id.substr(6, ev.id.size() - 12);
      for (Layer& p : current_.popups)
        if (p.id == pid) { begin_preview(); p.modal = ev.checked; return commit_current(); }
    }
    return {O::None, {}};
  }
  if (ev.kind == K::Choose) {
    if (ev.id == "border") {
      if (auto b = border_from_name(ev.value)) { begin_preview(); if (Node* n = sel_node()) n->border = *b; }
      return commit_current();
    }
    if (ev.id == "content") {
      begin_preview();
      if (Node* n = sel_node()) n->content = ev.value;
      return commit_current();
    }
    if (ev.id == "load") return {O::LoadLayout, ev.value};
    if (ev.id.rfind("popup.", 0) == 0 && ev.id.size() > 7 && ev.id.substr(ev.id.size() - 7) == ".anchor") {
      const std::string pid = ev.id.substr(6, ev.id.size() - 13);
      for (Layer& p : current_.popups)
        if (p.id == pid) { if (auto a = anchor_from_name(ev.value)) { begin_preview(); p.placement.anchor = *a; } return commit_current(); }
    }
    return {O::None, {}};
  }
  if (ev.kind == K::Input) {
    if (ev.id == "save") return {O::SaveAs, ev.value};
    if (ev.id == "title") { begin_preview(); if (Node* n = sel_node()) n->title = ev.value; return commit_current(); }
    if (ev.id == "size") {
      if (std::optional<SplitSize> s = parse_size_text(ev.value)) { begin_preview(); if (Node* n = sel_node()) n->size = *s; return commit_current(); }
      cancel_preview();
      status_ = "'" + ev.value + "' is not a size (fill | fill N | N% | cells)";
      return {O::Changed, {}};
    }
    if (ev.id == "popup.add") {
      if (ev.value.empty() || current_.popup(ev.value)) { status_ = ev.value.empty() ? "a popup needs an id" : "a popup named '" + ev.value + "' exists"; return {O::Changed, {}}; }
      begin_preview();
      Layer l;
      l.id = ev.value;
      l.placement = {Dim::rel(0.5), Dim::rel(0.5), Dim::rel(0.5), Dim::abs(8), Anchor::Center, true, {}, {}, {}, {}};
      l.modal = true;
      Node n = Node::window("text:" + ev.value);
      n.id = ev.value;
      n.border = Border::Rounded;
      n.title = ev.value;
      n.focusable = true;
      l.root = n;
      current_.popups.push_back(std::move(l));
      status_ = "added popup " + ev.value;
      Outcome o = commit_current();
      rebuild_menu();
      return o;
    }
    if (ev.id.rfind("popup.", 0) == 0) {
      const std::size_t dot = ev.id.rfind('.');
      const std::string pid = ev.id.substr(6, dot - 6), field = ev.id.substr(dot + 1);
      std::optional<Dim> d = parse_dim(ev.value);
      if (!d) {
        // A bare integer is cells (parse_dim refuses it by design: a size is explicit).
        char* end = nullptr;
        const long v = std::strtol(ev.value.c_str(), &end, 10);
        if (end && *end == '\0' && !ev.value.empty()) d = Dim::abs(static_cast<int>(v));
      }
      if (!d) { cancel_preview(); status_ = "'" + ev.value + "' is not a dim (N | N% | N% ± cells)"; return {O::Changed, {}}; }
      for (Layer& p : current_.popups)
        if (p.id == pid) {
          begin_preview();
          if (field == "x") p.placement.x = *d;
          else if (field == "y") p.placement.y = *d;
          else if (field == "w") p.placement.w = *d;
          else if (field == "h") p.placement.h = *d;
          return commit_current();
        }
    }
    return {O::None, {}};
  }
  if (ev.kind == K::Closed) return {O::Closed, {}};
  // ---- live preview while a choice is highlighted or an input is being typed ----
  const MenuItem* sel = menu_.selected_item();
  const std::string& level = menu_.level().id;
  if (sel && !menu_.editing() && level == "border") {
    if (auto b = border_from_name(sel->id)) { begin_preview(); if (Node* n = sel_node()) n->border = *b; return {O::Changed, {}}; }
  }
  if (sel && !menu_.editing() && level == "content") {
    begin_preview();
    if (Node* n = sel_node()) n->content = sel->id;
    return {O::Changed, {}};
  }
  if (menu_.editing() && sel) {
    if (sel->id == "title") { begin_preview(); if (Node* n = sel_node()) n->title = sel->value; return {O::Changed, {}}; }
    if (sel->id == "size") {
      begin_preview();
      if (std::optional<SplitSize> s = parse_size_text(sel->value)) { if (Node* n = sel_node()) n->size = *s; }
      else if (Node* n = sel_node()) n->size = find_node(preview_->base.root, sel_)->size;
      return {O::Changed, {}};
    }
  }
  if (preview_ && !drag_) { cancel_preview(); return {O::Changed, {}}; }
  return {O::None, {}};
}

}  // namespace rolltui::tools
