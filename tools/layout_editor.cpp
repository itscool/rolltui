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
    if (!n.id.empty()) out.emplace_back(n.id.view());
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

LayoutEditor::LayoutEditor() {
  // The library's own table until a host says otherwise: a tool that has been told
  // nothing about a target app can only honestly offer the kinds every host has.
  for (WidgetKind k : widget_kinds()) kinds_.emplace_back(widget_kind_name(k));
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

void LayoutEditor::set_sources(std::vector<std::string> contents) {
  sources_ = std::move(contents);
  sync_content_fields();
}

void LayoutEditor::set_kinds(std::vector<std::string> names) {
  kinds_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : kinds_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("kind", std::move(opts));
  sync_content_fields();
}

void LayoutEditor::set_menus(std::vector<std::string> names) {
  menus_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : menus_) opts.push_back(MenuItem::action(n, n));
  menu_.set_options("menu_file", std::move(opts));
  sync_content_fields();
}

void LayoutEditor::set_default_min(int width, int height) {
  default_min_w_ = std::max(0, width);
  default_min_h_ = std::max(0, height);
}

// The whole of "not inheriting one" is here, and it is deliberately a value rather than a
// series of edits to the open layout: there is nothing to forget to clear.
Layout LayoutEditor::skeleton(std::string name) const {
  Layout l;
  l.name = std::move(name);
  l.min_width = default_min_w_;   // the TARGET's — see the header. Everything else is empty.
  l.min_height = default_min_h_;
  Node w = Node::window("text:");  // the one kind that names nothing a host must have bound
  w.id = "main";
  w.border = Border::Single;
  w.focusable = true;
  l.base.root = std::move(w);
  l.base.focus = "main";
  return l;
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

// The Actions level: one submenu per declared action (its description, and remove),
// plus the "add" input. Rebuilt whenever the list changes, like the popups level.
std::vector<MenuItem> LayoutEditor::action_items() const {
  InputSpec desc, name;
  desc.type = InputType::Text;
  name.type = InputType::Name;  // "<scope>.<verb>": dots are Name characters
  name.hint = "app.<verb> (not a library scope)";
  std::vector<MenuItem> items;
  for (const ActionDecl& d : current_.actions)
    items.push_back(MenuItem::submenu("action." + d.name, d.name,
                                      {MenuItem::input("action." + d.name + ".desc", "what it does", desc, d.description),
                                       MenuItem::action("action." + d.name + ".remove", "remove this action")}));
  items.push_back(MenuItem::input("action.add", "add an action (name)", name));
  return items;
}

void LayoutEditor::rebuild_menu() {
  std::vector<MenuItem> borders, anchors, kinds, menus, loads;
  for (const char* b : {"none", "single", "rounded", "double", "heavy"}) borders.push_back(MenuItem::action(b, b));
  for (const char* a : {"top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right"}) anchors.push_back(MenuItem::action(a, a));
  for (const std::string& n : kinds_) kinds.push_back(MenuItem::action(n, n));
  for (const std::string& n : menus_) menus.push_back(MenuItem::action(n, n));
  for (const std::string& n : layouts_) loads.push_back(MenuItem::action(n, n));
  InputSpec dim, size, name, text, threshold;
  dim.type = InputType::Dim;
  size.type = InputType::Size;
  name.type = InputType::Name;
  text.type = InputType::Text;
  text.optional = true;  // a window may have no title
  threshold.type = InputType::Int;
  threshold.min = 0;
  threshold.max = 500;
  threshold.hint = "0 = this screen states none";
  std::vector<MenuItem> popups;
  for (const Layer& p : current_.popups) {
    popups.push_back(MenuItem::submenu("popup." + p.id, p.id.str(),
                                       {MenuItem::input("popup." + p.id + ".x", "x", dim, dim_to_string(p.placement.x)), MenuItem::input("popup." + p.id + ".y", "y", dim, dim_to_string(p.placement.y)),
                                        MenuItem::input("popup." + p.id + ".w", "w", dim, dim_to_string(p.placement.w)), MenuItem::input("popup." + p.id + ".h", "h", dim, dim_to_string(p.placement.h)),
                                        MenuItem::choice("popup." + p.id + ".anchor", "anchor", anchors, std::string(anchor_name(p.placement.anchor))),
                                        MenuItem::toggle("popup." + p.id + ".modal", "modal", p.modal), MenuItem::action("popup." + p.id + ".remove", "remove this popup")}));
  }
  popups.push_back(MenuItem::input("popup.add", "add a popup (id)", name));
  MenuItem root = MenuItem::submenu(
      "root", "layout editor",
      {MenuItem::action("next", "Select the next node", "Tab"), MenuItem::action("prev", "Select the previous node", "Shift-Tab"),
       MenuItem::action("split_row", "Split into a row (side by side)"), MenuItem::action("split_column", "Split into a column (stacked)"),
       MenuItem::action("swap_prev", "Swap with the previous sibling"), MenuItem::action("swap_next", "Swap with the next sibling"),
       MenuItem::toggle("visible", "Visible", true), MenuItem::choice("border", "Border", borders, "single"), MenuItem::input("title", "Title", text),
       MenuItem::choice("kind", "Widget kind", std::move(kinds), "transcript"), MenuItem::input("source", "Source", text),
       MenuItem::choice("menu_file", "Menu file", std::move(menus), ""), MenuItem::input("size", "Size (Alt+arrows nudge)", size),
       MenuItem::toggle("focusable", "Focusable", false), MenuItem::action("delete", "Delete this node"),
       MenuItem::submenu("popups", "Popups", std::move(popups)),
       MenuItem::submenu("actions", "Actions this screen emits", action_items()),
       MenuItem::input("min_width", "Minimum width this screen needs", threshold),
       MenuItem::input("min_height", "Minimum height this screen needs", threshold),
       MenuItem::choice("focus", "Focused window", {}, ""),
       MenuItem::action("undo", "Undo", "Ctrl-Z"), MenuItem::action("redo", "Redo", "Ctrl-Y"),
       MenuItem::input("new", "New layout, from an empty screen (name)", name),
       MenuItem::choice("load", "Load layout", std::move(loads), ""), MenuItem::input("save", "Save layout file as (layouts/<name>.json)", name),
       MenuItem::action("reset_loaded", "Reset to the loaded layout\xE2\x80\xA6")});
  menu_.set_root(std::move(root));
  sync_values();
}

LayoutEditor::ContentParts LayoutEditor::parts_of(const Node* n) {
  ContentParts p;
  if (!n || !n->is_window()) return p;
  const std::string_view content = n->content.view();
  const std::size_t colon = content.find(':');
  p.kind_text = content.substr(0, colon);
  if (colon != std::string_view::npos) p.source = content.substr(colon + 1);
  // Through the registry's own two rungs, and deliberately not through parse_content: a
  // window whose source is missing or forbidden is exactly what this editor exists to
  // repair, and it cannot repair what it refuses to hold (Layout.hpp, content_for_kind).
  p.content = content_for_kind(p.kind_text, p.source);
  return p;
}

LayoutEditor::ContentParts LayoutEditor::content_parts() const { return parts_of(selected_node()); }

// The source as it stood BEFORE the live preview began. Stepping down the kind list
// past `help` (which takes no source) would otherwise drop it for every kind after it.
std::string LayoutEditor::base_source() const {
  return parts_of(find_node((preview_ ? *preview_ : current_).base.root, sel_)).source;
}

// The source a change of kind CARRIES OVER, which is the source as it stood before the
// preview began — with one named exception. Every kind's source is a BOUND NAME (a
// document, a row source, a path, a literal) except `help`, whose source is a key SCOPE,
// so carrying one into `help` produces a window that draws nothing and reports itself.
// There is no Forbidden case to handle here: `content_to_string` already drops a source a
// kind may not have, so that rule lives in Layout.cpp once.
std::string LayoutEditor::carried_source(std::string_view kind_name) const {
  return kind_name == widget_kind_name(WidgetKind::Help) ? std::string() : base_source();
}

// A kind NAME and a source in, `kind[:source]` out — through content_to_string, so the
// one rule about which kinds carry a colon lives in Layout.cpp and not here as well.
// A name in neither rung of the registry writes nothing and says so: the picker offers
// what a target can build, and a kind that does not exist is not one of them.
bool LayoutEditor::set_content(const std::string& kind_name, const std::string& source) {
  Node* n = sel_node();
  if (!n || !n->is_window()) return false;
  const std::optional<Content> c = content_for_kind(kind_name, source);
  if (!c) {
    status_ = "'" + kind_name + "' is not a widget kind this app can build";
    return false;
  }
  n->content = content_to_string(*c);
  return true;
}

// Which field owns the source, and what it accepts, are functions of the kind (the
// header comment's table). Disabled is drawn muted, so exactly one of Source / Menu
// file is offered at a time and neither is a second spelling of the other.
void LayoutEditor::sync_content_fields() {
  const Node* n = selected_node();
  const bool window = n && n->is_window();
  const ContentParts p = content_parts();
  const bool is_menu = p.content && p.content->kind == WidgetKind::Menu;
  menu_.set_value("kind", p.kind_text);
  menu_.set_value("source", p.source);
  menu_.set_value("menu_file", is_menu ? p.source : std::string());
  menu_.set_enabled("kind", window);
  menu_.set_enabled("menu_file", window && is_menu);
  // The rule is the KIND's, whichever rung it came from — a registered kind that takes no
  // source disables the field exactly as `help` does, because its host said so.
  const SourceRule rule = p.content ? content_source_rule(*p.content) : SourceRule::Required;
  const bool source_field = window && p.content && !is_menu && rule != SourceRule::Forbidden;
  menu_.set_enabled("source", source_field);
  if (MenuItem* it = menu_.find("source"); it && source_field) {
    // A path is not a Name; a literal is anything and may be empty.
    const WidgetKind k = p.content->kind;
    it->spec.type = (k == WidgetKind::Text || k == WidgetKind::File) ? InputType::Text : InputType::Name;
    it->spec.optional = rule == SourceRule::Optional;
    it->spec.hint.clear();
    for (const std::string& c : sources_)
      if (std::optional<Content> oc = parse_content(c); oc && content_kind_name(*oc) == p.kind_text && !oc->source.empty())
        it->spec.hint += (it->spec.hint.empty() ? "" : " | ") + oc->source;
    if (it->spec.hint.empty()) it->spec.hint = content_source_describes(*p.content);
  }
}

void LayoutEditor::sync_values() {
  // The three LAYOUT-WIDE fields first, because they are true whether or not a node is
  // selected — and because the focus choice's options are the tree's, which every split,
  // delete and rename changes. Rebuilding them here is what keeps a stale window id from
  // sitting in the list after the window is gone.
  menu_.set_value("min_width", std::to_string(current_.min_width));
  menu_.set_value("min_height", std::to_string(current_.min_height));
  {
    std::vector<MenuItem> focusable;
    focusable.push_back(MenuItem::action("", "(none \xE2\x80\x94 the first focusable window in tree order)"));
    for (const std::string& id : ids_in_order(current_.base.root))
      if (const Node* w = find_node(current_.base.root, id); w && w->is_window() && w->focusable)
        focusable.push_back(MenuItem::action(id, id));
    menu_.set_options("focus", std::move(focusable));
    menu_.set_value("focus", current_.base.focus.str());
  }
  const Node* n = selected_node();
  if (!n) return;
  menu_.set_checked("visible", n->visible);
  menu_.set_value("border", std::string(border_name(n->border)));
  menu_.set_value("title", n->title.str());
  menu_.set_value("size", split_size_to_string(n->size));
  menu_.set_checked("focusable", n->focusable);
  menu_.set_enabled("focusable", n->is_window());
  sync_content_fields();
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
      copy.id = unique_id(n->id.str());
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
  std::string s = menu_.editing() && !menu_.edit_reason().empty() ? "refused: " + menu_.edit_reason()
                  : preview_                                       ? "previewing \xE2\x80\x94 Enter commits, Esc cancels"
                                                                   : (status_.empty() ? "Enter commits, Esc cancels" : status_);
  s += " \xC2\xB7 undo " + std::to_string(undo_.undo_depth()) + " \xC2\xB7 redo " + std::to_string(undo_.redo_depth());
  return s;
}

std::string LayoutEditor::selection_line() const {
  const Node* n = selected_node();
  if (!n) return {};
  // Size and border come first: a content is kind[:source] (Phase 10 m2) and can be
  // long, and it is the one field the menu above always shows in full.
  std::string s = "selected: " + n->id + "  size " + split_size_to_string(n->size) + "  border " + std::string(border_name(n->border)) +
                  (n->visible ? "" : "  hidden") +
                  (n->is_window() ? "  " + n->content : n->kind == Node::Kind::Row ? "  (row)" : "  (column)");
  // A content that does not parse is said HERE as well as in the window's error panel:
  // the editor is where it gets repaired, so the reason belongs beside the fields.
  if (n->is_window())
    if (std::string why; !parse_content(n->content, &why)) s += " \xE2\x80\x94 " + why;
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
    // "action.<name>.remove" — the name itself holds dots, so it is the id with the
    // fixed prefix and the fixed suffix taken off, never a split on a dot.
    if (ev.id.rfind("action.", 0) == 0 && ev.id.size() > 14 && ev.id.substr(ev.id.size() - 7) == ".remove") {
      const std::string name = ev.id.substr(7, ev.id.size() - 14);
      begin_preview();
      current_.actions.erase(std::remove_if(current_.actions.begin(), current_.actions.end(), [&](const ActionDecl& d) { return d.name == name; }),
                             current_.actions.end());
      status_ = "removed action " + name + " (a chord for it is kept and inert)";
      Outcome o = commit_current();
      menu_.set_options("actions", action_items());
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
    if (ev.id == "kind") {
      begin_preview();
      if (!set_content(ev.value, carried_source(ev.value))) cancel_preview();
      return commit_current();
    }
    if (ev.id == "menu_file") {
      begin_preview();
      set_content("menu", ev.value);
      return commit_current();
    }
    if (ev.id == "focus") {
      begin_preview();
      current_.base.focus = ev.value;  // "" is a real answer: the first focusable in tree order
      status_ = ev.value.empty() ? "the first focusable window in tree order takes focus" : "focus starts on " + ev.value;
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
    if (ev.id == "new") {
      if (ev.value.empty()) { status_ = "a layout needs a name"; return {O::Changed, {}}; }
      replace(skeleton(ev.value));
      status_ = "new layout '" + ev.value + "' \xE2\x80\x94 one window, no popups, no actions" +
                (current_.min_width || current_.min_height
                     ? "; min " + std::to_string(current_.min_width) + "x" + std::to_string(current_.min_height) + " from the app"
                     : "; no size threshold");
      return {O::Committed, {}};
    }
    if (ev.id == "min_width" || ev.id == "min_height") {
      // The Int spec already refused anything that is not a number in range, so a commit
      // here is a number: the only question left is which of the two it is.
      int& target = ev.id == "min_width" ? current_.min_width : current_.min_height;
      begin_preview();
      target = std::atoi(ev.value.c_str());
      return commit_current();
    }
    if (ev.id == "title") { begin_preview(); if (Node* n = sel_node()) n->title = ev.value; return commit_current(); }
    if (ev.id == "source") {
      const ContentParts p = content_parts();
      if (!p.content) { status_ = "'" + p.kind_text + "' is not a widget kind \xE2\x80\x94 set the kind first"; return {O::Changed, {}}; }
      begin_preview();
      set_content(p.kind_text, ev.value);
      return commit_current();
    }
    if (ev.id == "action.add") {
      const std::string name = ev.value;
      const std::string why = name.empty() ? "an action needs a name" : action_decl_problem(name);
      if (!why.empty()) { status_ = name.empty() ? why : "'" + name + "': " + why; return {O::Changed, {}}; }
      if (std::find_if(current_.actions.begin(), current_.actions.end(), [&](const ActionDecl& d) { return d.name == name; }) != current_.actions.end()) {
        status_ = "'" + name + "' is already declared";
        return {O::Changed, {}};
      }
      begin_preview();
      current_.actions.push_back({name, {}});  // the description is the next field, not a placeholder invented here
      status_ = "declared " + name + " \xE2\x80\x94 say what it does, then bind a key to it";
      Outcome o = commit_current();
      menu_.set_options("actions", action_items());
      return o;
    }
    if (ev.id.rfind("action.", 0) == 0 && ev.id.size() > 12 && ev.id.substr(ev.id.size() - 5) == ".desc") {
      const std::string name = ev.id.substr(7, ev.id.size() - 12);
      for (ActionDecl& d : current_.actions)
        if (d.name == name) { begin_preview(); d.description = ev.value; return commit_current(); }
      return {O::None, {}};
    }
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
  if (sel && !menu_.editing() && level == "kind") {
    begin_preview();
    if (!set_content(sel->id, carried_source(sel->id))) cancel_preview();
    return {O::Changed, {}};
  }
  if (sel && !menu_.editing() && level == "menu_file") {
    begin_preview();
    set_content("menu", sel->id);
    return {O::Changed, {}};
  }
  if (menu_.editing() && sel) {
    // The editing text, never the item's value: that is the committed one.
    if (sel->id == "title") { begin_preview(); if (Node* n = sel_node()) n->title = menu_.editing_text(); return {O::Changed, {}}; }
    if (sel->id == "source") {
      const ContentParts p = parts_of(find_node((preview_ ? *preview_ : current_).base.root, sel_));
      if (p.content) {
        begin_preview();
        set_content(p.kind_text, menu_.editing_text());
      }
      return {O::Changed, {}};
    }
    if (sel->id == "size") {
      begin_preview();
      if (std::optional<SplitSize> s = parse_size_text(menu_.editing_text())) { if (Node* n = sel_node()) n->size = *s; }
      else if (Node* n = sel_node()) n->size = find_node(preview_->base.root, sel_)->size;
      return {O::Changed, {}};
    }
  }
  if (preview_ && !drag_) { cancel_preview(); return {O::Changed, {}}; }
  return {O::None, {}};
}

}  // namespace rolltui::tools
