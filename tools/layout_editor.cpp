// rolltui/tools/layout_editor.cpp — see layout_editor.hpp.
#include "tool_str.hpp"
#include "layout_editor.hpp"

#include <algorithm>
#include <cstdlib>

namespace rolltui::tools {

namespace {

// ---- menu tree glue (mechanical; identical shape to keys_editor.cpp's, no shared vocabulary
// in it — see that file's comment on why this is not a header of its own). --------------------
void set_options(RolltuiMenu* m, std::string_view id, std::vector<MenuItem>&& options) {
  RolltuiMenuItemList list;
  for (MenuItem& it : options) list.push_back(std::move(it));
  rolltui_menu_set_options(m, id.data(), id.size(), &list);
}
void set_enabled(RolltuiMenu* m, std::string_view id, bool enabled) { rolltui_menu_set_enabled(m, id.data(), id.size(), enabled ? 1 : 0); }
void set_value(RolltuiMenu* m, std::string_view id, std::string_view value) {
  rolltui_menu_set_value(m, id.data(), id.size(), value.data(), value.size());
}
void set_checked(RolltuiMenu* m, std::string_view id, bool checked) { rolltui_menu_set_checked(m, id.data(), id.size(), checked ? 1 : 0); }
MenuItem* find(RolltuiMenu* m, std::string_view id) { return rolltui_menu_find(m, id.data(), id.size()); }

// ---- content: built directly from rolltui_widget_kind_resolve, the primitive Layout.hpp's
// own content_for_kind/content_kind_name/content_source_rule/content_source_describes were
// always "the one accessor" over (its own words) — none of these carry a decision of their
// own beyond what that one C call already answers. Phase 18 m2: a kind's NAME is its identity
// whichever rung it came from, so `Content` holds the name and the source and each helper here
// is one C call over the name. ------------------------------------------------------------------
std::optional<Content> content_for_kind(std::string_view kind_name, std::string source = {}) {
  if (rolltui_widget_kind_resolve(kind_name.data(), kind_name.size(), nullptr, nullptr, nullptr, nullptr) ==
      ROLLTUI_KIND_UNKNOWN)
    return std::nullopt;
  Content c;
  set_str(c.kind, kind_name);
  set_str(c.source, std::move(source));
  return c;
}
std::string content_kind_name(const Content& c) { return str_of(c.kind); }
unsigned char content_source_rule(const Content& c) {
  unsigned char rule = ROLLTUI_SOURCE_REQUIRED;
  rolltui_widget_kind_resolve(c.kind.data(), c.kind.size(), nullptr, &rule, nullptr, nullptr);
  return rule;
}
// The kind's source SHAPE — a row of the registry, whichever rung it came from. This was the
// one per-kind rule the retired enum had been carrying silently (`Text || File`).
unsigned char content_source_shape(const Content& c) {
  std::size_t row = 0;
  if (rolltui_widget_kind_resolve(c.kind.data(), c.kind.size(), &row, nullptr, nullptr, nullptr) == ROLLTUI_KIND_UNKNOWN)
    return ROLLTUI_SOURCE_SHAPE_NAME;
  return rolltui_widget_kind_source_shape(row);
}
std::string content_source_describes(const Content& c) {
  const char* source_is = nullptr;
  std::size_t len = 0;
  rolltui_widget_kind_resolve(c.kind.data(), c.kind.size(), nullptr, nullptr, &source_is, &len);
  return source_is ? std::string(source_is, len) : std::string();
}
std::string content_to_string(const Content& c) {
  RolltuiStr out{};
  rolltui_content_format(c.kind.data(), c.kind.size(), c.source.data(), c.source.size(), content_source_rule(c), &out);
  const std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}
std::optional<Content> parse_content(std::string_view text, std::string* why = nullptr) {
  std::size_t row = 0;
  int is_host = 0;
  const char *name_p = nullptr, *source_p = nullptr;
  std::size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why_c{};
  const int ok = rolltui_content_parse(text.data(), text.size(), &row, &is_host, &name_p, &name_len, &source_p,
                                       &source_len, &problem, &why_c);
  if (!ok) {
    if (why) why->assign(why_c.p ? why_c.p : "", why_c.n);
    rolltui_str_free(&why_c);
    return std::nullopt;
  }
  rolltui_str_free(&why_c);
  Content c;
  set_str(c.kind, std::string_view(name_p, name_len));
  set_str(c.source, std::string_view(source_p, source_len));
  return c;
}
std::string action_decl_problem(std::string_view name) {
  RolltuiStr out{};
  rolltui_action_decl_problem(name.data(), name.size(), rolltui_layout_default_hooks(), &out);
  const std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}

// ---- names: anchors, borders, dims and sizes — direct, bounded C calls (rolltui.h rule 3a).
std::string_view border_name(Border b) {
  std::size_t len = 0;
  const char* p = rolltui_border_name(static_cast<unsigned char>(b), &len);
  return {p, len};
}
std::optional<Border> border_from_name(std::string_view name) {
  unsigned char out = 0;
  if (!rolltui_border_from_name(name.data(), name.size(), &out)) return std::nullopt;
  return static_cast<Border>(out);
}
std::string_view anchor_name(Anchor a) {
  std::size_t len = 0;
  const char* p = rolltui_anchor_name(static_cast<unsigned char>(a), &len);
  return {p, len};
}
std::optional<Anchor> anchor_from_name(std::string_view name) {
  unsigned char out = 0;
  if (!rolltui_anchor_from_name(name.data(), name.size(), &out)) return std::nullopt;
  return static_cast<Anchor>(out);
}
std::string dim_to_string(Dim d) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_dim_to_string(d, buf, sizeof buf));
}
std::string split_size_to_string(SplitSize s) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_split_size_to_string(s, buf, sizeof buf));
}
std::optional<Dim> parse_dim(std::string_view text) {
  Dim d{};
  if (!rolltui_parse_dim(text.data(), text.size(), &d)) return std::nullopt;
  return d;
}
std::optional<SplitSize> parse_size_text(std::string_view text) {
  SplitSize s{};
  if (!rolltui_parse_size_text(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

}  // namespace

// See layout_editor.hpp: a fresh, uncached parse of a shipped built-in.
Layout builtin_layout(std::string_view name) {
  std::size_t text_len = 0;
  const char* text = rolltui_layout_builtin_json(name.data(), name.size(), &text_len);
  std::size_t default_n = 0;
  const RolltuiLayoutAction* default_actions = rolltui_layout_shipped_default_actions(&default_n);
  RolltuiLoadedLayout loaded;
  rolltui_loaded_layout_init(&loaded);
  RolltuiLayoutReport rep{};
  rolltui_load_layout_text(text, text_len, &loaded, default_actions, default_n, rolltui_layout_default_hooks(), &rep);
  Layout out{};
  rolltui_loaded_layout_to_layout(&loaded, &out);
  rolltui_loaded_layout_release(&loaded);
  rolltui_layout_report_release(&rep);
  return out;
}

// ---- tree helpers ---------------------------------------------------------------------

Node* LayoutEditor::find_node(Node& root, std::string_view id) {
  if (view_of(root.id) == id) return &root;
  for (Node& c : root.children)
    if (Node* f = find_node(c, id)) return f;
  return nullptr;
}
const Node* LayoutEditor::find_node(const Node& root, std::string_view id) { return find_node(const_cast<Node&>(root), id); }

Node* LayoutEditor::parent_of(Node& root, std::string_view id, std::size_t* index) {
  for (std::size_t i = 0; i < root.children.size(); ++i) {
    if (view_of(root.children[i].id) == id) { if (index) *index = i; return &root; }
    if (Node* p = parent_of(root.children[i], id, index)) return p;
  }
  return nullptr;
}

std::vector<std::string> LayoutEditor::ids_in_order(const Node& root) {
  std::vector<std::string> out;
  auto walk = [&](auto& self, const Node& n) -> void {
    if (!n.id.empty()) out.emplace_back(view_of(n.id));
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
  for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) {
    std::size_t len = 0;
    const char* name = rolltui_widget_kind_name(i, &len);
    kinds_.emplace_back(name, len);
  }
  current_ = builtin_layout("default");
  undo_.reset(current_.clone());
  select_next();
  rebuild_menu();
}

void LayoutEditor::load(const Layout& layout) {
  current_ = (layout).clone();
  undo_.reset(current_.clone());
  preview_.reset();
  sel_.clear();
  select_next();
  rebuild_menu();
  status_ = "loaded " + str_of(current_.name);
}

void LayoutEditor::set_sources(std::vector<std::string> contents) {
  sources_ = std::move(contents);
  sync_content_fields();
}

void LayoutEditor::set_kinds(std::vector<std::string> names) {
  kinds_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : kinds_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "kind", std::move(opts));
  sync_content_fields();
}

void LayoutEditor::set_menus(std::vector<std::string> names) {
  menus_ = std::move(names);
  std::vector<MenuItem> opts;
  for (const std::string& n : menus_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "menu_file", std::move(opts));
  sync_content_fields();
}

void LayoutEditor::set_default_min(int width, int height) {
  default_min_w_ = std::max(0, width);
  default_min_h_ = std::max(0, height);
}

// The whole of "not inheriting one" is here, and it is deliberately a value rather than a
// series of edits to the open layout: there is nothing to forget to clear.
Layout LayoutEditor::skeleton(std::string name) const {
  Layout l{};
  set_str(l.name, name);
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
  for (const std::string& n : layouts_) opts.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  set_options(menu_, "load", std::move(opts));
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
  // Iterates the loader's OWN element type: `Layout::actions` is a `RolltuiActionList` of
  // `RolltuiLayoutAction`, whose `.name`/`.description` are `RolltuiStr` — a MenuItem
  // factory's by-value `std::string` parameter needs `.str()` where the value crosses
  // whole, exactly as it always needed one from a real `std::string`.
  for (const RolltuiLayoutAction& d : current_.actions) {
    const std::string name = str_of(d.name);
    std::vector<MenuItem> fields;
    fields.push_back(MenuItem::input(("action." + name + ".desc").c_str(), "what it does", desc.clone(), str_of(d.description).c_str()));
    fields.push_back(MenuItem::action(("action." + name + ".remove").c_str(), "remove this action"));
    items.push_back(submenu_of(("action." + name).c_str(), name.c_str(), std::move(fields)));
  }
  items.push_back(MenuItem::input("action.add", "add an action (name)", name.clone()));
  return items;
}

void LayoutEditor::rebuild_menu() {
  std::vector<MenuItem> borders, anchors, kinds, menus, loads;
  for (const char* b : {"none", "single", "rounded", "double", "heavy"}) borders.push_back(MenuItem::action(b, b));
  for (const char* a : {"top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right"}) anchors.push_back(MenuItem::action(a, a));
  for (const std::string& n : kinds_) kinds.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  for (const std::string& n : menus_) menus.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  for (const std::string& n : layouts_) loads.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
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
    const std::string id = str_of(p.id), base = "popup." + id;
    std::vector<MenuItem> fields;
    fields.push_back(MenuItem::input((base + ".x").c_str(), "x", dim.clone(), dim_to_string(p.placement.x).c_str()));
    fields.push_back(MenuItem::input((base + ".y").c_str(), "y", dim.clone(), dim_to_string(p.placement.y).c_str()));
    fields.push_back(MenuItem::input((base + ".w").c_str(), "w", dim.clone(), dim_to_string(p.placement.w).c_str()));
    fields.push_back(MenuItem::input((base + ".h").c_str(), "h", dim.clone(), dim_to_string(p.placement.h).c_str()));
    fields.push_back(choice_of((base + ".anchor").c_str(), "anchor", clone_items(anchors), std::string(anchor_name(p.placement.anchor)).c_str()));
    fields.push_back(MenuItem::toggle((base + ".modal").c_str(), "modal", p.modal != 0));
    fields.push_back(MenuItem::action((base + ".remove").c_str(), "remove this popup"));
    popups.push_back(submenu_of(base.c_str(), id.c_str(), std::move(fields)));
  }
  popups.push_back(MenuItem::input("popup.add", "add a popup (id)", name.clone()));
  std::vector<MenuItem> top;
  top.push_back(MenuItem::action("next", "Select the next node", "Tab"));
  top.push_back(MenuItem::action("prev", "Select the previous node", "Shift-Tab"));
  top.push_back(MenuItem::action("split_row", "Split into a row (side by side)"));
  top.push_back(MenuItem::action("split_column", "Split into a column (stacked)"));
  top.push_back(MenuItem::action("swap_prev", "Swap with the previous sibling"));
  top.push_back(MenuItem::action("swap_next", "Swap with the next sibling"));
  top.push_back(MenuItem::toggle("visible", "Visible", true));
  top.push_back(choice_of("border", "Border", std::move(borders), "single"));
  top.push_back(MenuItem::input("title", "Title", text.clone()));
  top.push_back(choice_of("kind", "Widget kind", std::move(kinds), "transcript"));
  top.push_back(MenuItem::input("source", "Source", text.clone()));
  top.push_back(choice_of("menu_file", "Menu file", std::move(menus), ""));
  top.push_back(MenuItem::input("size", "Size (Alt+arrows nudge)", size.clone()));
  top.push_back(MenuItem::toggle("focusable", "Focusable", false));
  top.push_back(MenuItem::action("delete", "Delete this node"));
  top.push_back(submenu_of("popups", "Popups", std::move(popups)));
  top.push_back(submenu_of("actions", "Actions this screen emits", action_items()));
  top.push_back(MenuItem::input("min_width", "Minimum width this screen needs", threshold.clone()));
  top.push_back(MenuItem::input("min_height", "Minimum height this screen needs", threshold.clone()));
  top.push_back(MenuItem::choice("focus", "Focused window", ""));
  top.push_back(MenuItem::action("undo", "Undo", "Ctrl-Z"));
  top.push_back(MenuItem::action("redo", "Redo", "Ctrl-Y"));
  top.push_back(MenuItem::input("new", "New layout, from an empty screen (name)", name.clone()));
  top.push_back(choice_of("load", "Load layout", std::move(loads), ""));
  top.push_back(MenuItem::input("save", "Save layout file as (layouts/<name>.json)", name.clone()));
  top.push_back(MenuItem::action("reset_loaded", "Reset to the loaded layout\xE2\x80\xA6"));
  MenuItem root = submenu_of("root", "layout editor", std::move(top));
  rolltui_menu_set_root(menu_, &root);
  sync_values();
}

LayoutEditor::ContentParts LayoutEditor::parts_of(const Node* n) {
  ContentParts p;
  if (!n || !n->is_window()) return p;
  const std::string_view content = view_of(n->content);
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
// There is no Forbidden case to handle here: content_to_string already drops a source a
// kind may not have, so that rule lives in one place (rolltui_content_format).
std::string LayoutEditor::carried_source(std::string_view kind_name) const {
  return kind_name == "help" ? std::string() : base_source();
}

// A kind NAME and a source in, `kind[:source]` out — through content_to_string, so the
// one rule about which kinds carry a colon lives in one place, not here as well.
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
  set_str(n->content, content_to_string(*c));
  return true;
}

// Which field owns the source, and what it accepts, are functions of the kind (the
// header comment's table). Disabled is drawn muted, so exactly one of Source / Menu
// file is offered at a time and neither is a second spelling of the other.
void LayoutEditor::sync_content_fields() {
  const Node* n = selected_node();
  const bool window = n && n->is_window();
  const ContentParts p = content_parts();
  const bool is_menu = p.content && p.content->kind == "menu";
  set_value(menu_, "kind", p.kind_text);
  set_value(menu_, "source", p.source);
  set_value(menu_, "menu_file", is_menu ? p.source : std::string());
  set_enabled(menu_, "kind", window);
  set_enabled(menu_, "menu_file", window && is_menu);
  // The rule is the KIND's, whichever rung it came from — a registered kind that takes no
  // source disables the field exactly as `help` does, because its host said so.
  const unsigned char rule = p.content ? content_source_rule(*p.content) : ROLLTUI_SOURCE_REQUIRED;
  const bool source_field = window && p.content && !is_menu && rule != ROLLTUI_SOURCE_FORBIDDEN;
  set_enabled(menu_, "source", source_field);
  if (MenuItem* it = find(menu_, "source"); it && source_field) {
    // A path is not a Name; a literal is anything and may be empty — the kind's SHAPE, read
    // from its registry row whichever rung it came from (Phase 18 m2).
    it->spec.type = content_source_shape(*p.content) == ROLLTUI_SOURCE_SHAPE_TEXT ? InputType::Text : InputType::Name;
    it->spec.optional = rule == ROLLTUI_SOURCE_OPTIONAL;
    it->spec.hint.clear();
    for (const std::string& c : sources_)
      if (std::optional<Content> oc = parse_content(c); oc && content_kind_name(*oc) == p.kind_text && !oc->source.empty())
        set_str(it->spec.hint, str_of(it->spec.hint) + (it->spec.hint.empty() ? "" : " | ") + str_of(oc->source));
    if (it->spec.hint.empty()) set_str(it->spec.hint, content_source_describes(*p.content));
  }
}

void LayoutEditor::sync_values() {
  // The three LAYOUT-WIDE fields first, because they are true whether or not a node is
  // selected — and because the focus choice's options are the tree's, which every split,
  // delete and rename changes. Rebuilding them here is what keeps a stale window id from
  // sitting in the list after the window is gone.
  set_value(menu_, "min_width", std::to_string(current_.min_width));
  set_value(menu_, "min_height", std::to_string(current_.min_height));
  {
    std::vector<MenuItem> focusable;
    focusable.push_back(MenuItem::action("", "(none \xE2\x80\x94 the first focusable window in tree order)"));
    for (const std::string& id : ids_in_order(current_.base.root))
      if (const Node* w = find_node(current_.base.root, id); w && w->is_window() && w->focusable)
        focusable.push_back(MenuItem::action(std::string(id).c_str(), std::string(id).c_str()));
    set_options(menu_, "focus", std::move(focusable));
    set_value(menu_, "focus", str_of(current_.base.focus));
  }
  const Node* n = selected_node();
  if (!n) return;
  set_checked(menu_, "visible", n->visible);
  set_value(menu_, "border", std::string(border_name(n->border)));
  set_value(menu_, "title", str_of(n->title));
  set_value(menu_, "size", split_size_to_string(n->size));
  set_checked(menu_, "focusable", n->focusable);
  set_enabled(menu_, "focusable", n->is_window());
  sync_content_fields();
  if (MenuItem* root = find(menu_, "root")) set_str(root->label, "layout editor \xE2\x80\xA2 " + sel_ + (n->is_window() ? "" : n->kind == Node::Kind::Row ? " (row)" : " (column)"));
}

// ---- preview / commit -----------------------------------------------------------------

void LayoutEditor::begin_preview() { if (!preview_) preview_ = current_.clone(); }

void LayoutEditor::cancel_preview() {
  if (!preview_) return;
  current_ = (*preview_).clone();
  preview_.reset();
}

LayoutEditor::Outcome LayoutEditor::commit_current() {
  preview_.reset();
  if (current_ == undo_.current()) { sync_values(); return {Outcome::Kind::Changed, {}}; }
  undo_.commit(current_.clone());
  sync_values();
  return {Outcome::Kind::Committed, {}};
}

void LayoutEditor::replace(Layout l) {
  preview_.reset();
  current_ = std::move(l);
  undo_.commit(current_.clone());
  if (!find_node(current_.base.root, sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
}

bool LayoutEditor::undo() {
  cancel_preview();
  if (!undo_.undo()) return false;
  current_ = (undo_.current()).clone();
  if (!find_node(current_.base.root, sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
  status_ = "undone";
  return true;
}

bool LayoutEditor::redo() {
  cancel_preview();
  if (!undo_.redo()) return false;
  current_ = (undo_.current()).clone();
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
      Node copy = (*n).clone();
      set_str(copy.id, unique_id(str_of(n->id)));
      copy.size = SplitSize::filling();
      if (!copy.title.empty()) copy.title.assign(copy.id);  // so the two panes read apart
      Node first = (*n).clone();
      first.size = SplitSize::filling();
      Node split = op == Op::SplitRow ? Node::row() : Node::column();
      split.children.push_back(std::move(first));
      split.children.push_back(std::move(copy));
      split.size = n->size;
      set_str(split.id, unique_id(str_of(n->id) + (op == Op::SplitRow ? "-row" : "-column")));
      *n = std::move(split);
      status_ = "split " + sel_ + (op == Op::SplitRow ? " side by side" : " stacked");
      sel_ = str_of(n->children[0].id);
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
        Node only = (parent->children[0]).clone();
        only.size = parent->size;
        *parent = std::move(only);
      } else if (parent->children.size() == 1) {
        Node only = (parent->children[0]).clone();
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

void LayoutEditor::status_line(std::string& out) const {
  std::size_t reason_len = 0;
  const char* reason = rolltui_menu_edit_reason(menu_, &reason_len);
  const bool has_reason = rolltui_menu_editing(menu_) && reason_len > 0;
  out.clear();
  if (has_reason) { out += "refused: "; out.append(reason, reason_len); }
  else if (preview_) out += "previewing \xE2\x80\x94 Enter commits, Esc cancels";
  else if (status_.empty()) out += "Enter commits, Esc cancels";
  else out += status_;
  out += " \xC2\xB7 undo ";
  append_count(out, undo_.undo_depth());
  out += " \xC2\xB7 redo ";
  append_count(out, undo_.redo_depth());
}
std::string LayoutEditor::status_line() const {
  std::string s;
  status_line(s);
  return s;
}

std::string LayoutEditor::selection_line() const {
  const Node* n = selected_node();
  if (!n) return {};
  // Size and border come first: a content is kind[:source] (Phase 10 m2) and can be
  // long, and it is the one field the menu above always shows in full.
  std::string s = "selected: " + str_of(n->id) + "  size " + split_size_to_string(n->size) + "  border " + std::string(border_name(n->border)) +
                  (n->visible ? "" : "  hidden") +
                  (n->is_window() ? "  " + str_of(n->content) : n->kind == Node::Kind::Row ? "  (row)" : "  (column)");
  // A content that does not parse is said HERE as well as in the window's error panel:
  // the editor is where it gets repaired, so the reason belongs beside the fields.
  if (n->is_window())
    if (std::string why; !parse_content(view_of(n->content), &why)) s += " \xE2\x80\x94 " + why;
  return s;
}

// ---- events ---------------------------------------------------------------------------

LayoutEditor::Outcome LayoutEditor::handle(const RolltuiEvent* e, const RolltuiBindings* nav) {
  using O = Outcome::Kind;
  if (e->kind == ROLLTUI_EVENT_KEY) {
    const RolltuiChord& k = e->key;
    // An undo or redo changes the COMMITTED value: the host writes it to the store.
    std::size_t len = 0;
    const char* ed_p = rolltui_bindings_action_for(nav, &k, "editor", 6, &len);
    const std::string_view ed = ed_p ? std::string_view(ed_p, len) : std::string_view();
    if (ed == "editor.undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (ed == "editor.redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    const char* st_p = rolltui_bindings_action_for(nav, &k, "stack", 5, &len);
    const std::string_view st = st_p ? std::string_view(st_p, len) : std::string_view();
    if ((st == "stack.focus_next" || st == "stack.focus_prev") && !rolltui_menu_editing(menu_)) { select_next(st == "stack.focus_prev"); return {O::Changed, {}}; }
    if (k.alt && !k.ctrl && (k.key == ROLLTUI_KEY_LEFT || k.key == ROLLTUI_KEY_RIGHT || k.key == ROLLTUI_KEY_UP || k.key == ROLLTUI_KEY_DOWN)) {
      // Nudge the selected node's size by one cell along its parent's axis.
      Node* n = sel_node();
      Node* parent = parent_of(current_.base.root, sel_);
      if (!n || !parent) { status_ = "the root has no size to nudge"; return {O::Changed, {}}; }
      const bool horizontal = parent->kind == Node::Kind::Row;
      const int delta = (k.key == ROLLTUI_KEY_RIGHT || k.key == ROLLTUI_KEY_DOWN) ? 1 : -1;
      if ((horizontal && (k.key == ROLLTUI_KEY_UP || k.key == ROLLTUI_KEY_DOWN)) || (!horizontal && (k.key == ROLLTUI_KEY_LEFT || k.key == ROLLTUI_KEY_RIGHT))) {
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
  RolltuiMenuEvent raw{};
  rolltui_menu_handle(menu_, e, nav, rolltui_menu_default_actions(), &raw);
  const unsigned char kind = raw.kind;
  const std::string id = str_of(raw.id);
  const std::string value = str_of(raw.value);
  const bool checked = raw.checked != 0;
  rolltui_menu_event_release(&raw);
  if (kind == ROLLTUI_MENU_EVENT_ACTIVATE) {
    if (id == "next") { select_next(); return {O::Changed, {}}; }
    if (id == "prev") { select_next(true); return {O::Changed, {}}; }
    if (id == "undo") { const bool did = undo(); status_ = did ? "undone" : "nothing to undo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "redo") { const bool did = redo(); status_ = did ? "redone" : "nothing to redo"; return {did ? O::Committed : O::Changed, {}}; }
    if (id == "reset_loaded") return {O::ResetLoaded, {}};
    static const std::pair<const char*, Op> ops[] = {{"split_row", Op::SplitRow}, {"split_column", Op::SplitColumn}, {"swap_prev", Op::SwapPrev},
                                                     {"swap_next", Op::SwapNext}, {"delete", Op::Delete}};
    for (const auto& [oid, op] : ops)
      if (id == oid) {
        begin_preview();
        if (!apply_op(op)) { cancel_preview(); return {O::Changed, {}}; }
        Outcome o = commit_current();
        rebuild_menu();
        return o;
      }
    if (id.rfind("popup.", 0) == 0 && id.size() > 7 && id.substr(id.size() - 7) == ".remove") {
      const std::string pid = id.substr(6, id.size() - 13);
      begin_preview();
      current_.popups.erase_id(std::string_view(pid).data(), std::string_view(pid).size());
      status_ = "removed popup " + pid;
      Outcome o = commit_current();
      rebuild_menu();
      return o;
    }
    // "action.<name>.remove" — the name itself holds dots, so it is the id with the
    // fixed prefix and the fixed suffix taken off, never a split on a dot.
    if (id.rfind("action.", 0) == 0 && id.size() > 14 && id.substr(id.size() - 7) == ".remove") {
      const std::string name = id.substr(7, id.size() - 14);
      begin_preview();
      current_.actions.erase_name(std::string_view(name).data(), std::string_view(name).size());
      status_ = "removed action " + name + " (a chord for it is kept and inert)";
      Outcome o = commit_current();
      set_options(menu_, "actions", action_items());
      return o;
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_TOGGLE) {
    if (id == "visible" || id == "focusable") {
      begin_preview();
      apply_op(id == "visible" ? Op::ToggleVisible : Op::ToggleFocusable);
      return commit_current();
    }
    if (id.rfind("popup.", 0) == 0) {
      const std::string pid = id.substr(6, id.size() - 12);
      for (Layer& p : current_.popups)
        if (view_of(p.id) == pid) { begin_preview(); p.modal = checked; return commit_current(); }
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    if (id == "border") {
      if (auto b = border_from_name(value)) { begin_preview(); if (Node* n = sel_node()) n->border = *b; }
      return commit_current();
    }
    if (id == "kind") {
      begin_preview();
      if (!set_content(value, carried_source(value))) cancel_preview();
      return commit_current();
    }
    if (id == "menu_file") {
      begin_preview();
      set_content("menu", value);
      return commit_current();
    }
    if (id == "focus") {
      begin_preview();
      set_str(current_.base.focus, value);  // "" is a real answer: the first focusable in tree order
      status_ = value.empty() ? "the first focusable window in tree order takes focus" : "focus starts on " + value;
      return commit_current();
    }
    if (id == "load") return {O::LoadLayout, value};
    if (id.rfind("popup.", 0) == 0 && id.size() > 7 && id.substr(id.size() - 7) == ".anchor") {
      const std::string pid = id.substr(6, id.size() - 13);
      for (Layer& p : current_.popups)
        if (view_of(p.id) == pid) { if (auto a = anchor_from_name(value)) { begin_preview(); p.placement.anchor = *a; } return commit_current(); }
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_INPUT) {
    if (id == "save") return {O::SaveAs, value};
    if (id == "new") {
      if (value.empty()) { status_ = "a layout needs a name"; return {O::Changed, {}}; }
      replace(skeleton(value));
      status_ = "new layout '" + value + "' \xE2\x80\x94 one window, no popups, no actions" +
                (current_.min_width || current_.min_height
                     ? "; min " + std::to_string(current_.min_width) + "x" + std::to_string(current_.min_height) + " from the app"
                     : "; no size threshold");
      return {O::Committed, {}};
    }
    if (id == "min_width" || id == "min_height") {
      // The Int spec already refused anything that is not a number in range, so a commit
      // here is a number: the only question left is which of the two it is.
      int& target = id == "min_width" ? current_.min_width : current_.min_height;
      begin_preview();
      target = std::atoi(value.c_str());
      return commit_current();
    }
    if (id == "title") { begin_preview(); if (Node* n = sel_node()) set_str(n->title, value); return commit_current(); }
    if (id == "source") {
      const ContentParts p = content_parts();
      if (!p.content) { status_ = "'" + p.kind_text + "' is not a widget kind \xE2\x80\x94 set the kind first"; return {O::Changed, {}}; }
      begin_preview();
      set_content(p.kind_text, value);
      return commit_current();
    }
    if (id == "action.add") {
      const std::string name = value;
      const std::string why = name.empty() ? "an action needs a name" : action_decl_problem(name);
      if (!why.empty()) { status_ = name.empty() ? why : "'" + name + "': " + why; return {O::Changed, {}}; }
      if (std::find_if(current_.actions.begin(), current_.actions.end(),
                       [&](const RolltuiLayoutAction& d) { return view_of(d.name) == name; }) != current_.actions.end()) {
        status_ = "'" + name + "' is already declared";
        return {O::Changed, {}};
      }
      begin_preview();
      { RolltuiLayoutAction a{}; set_str(a.name, name); current_.actions.push_back(a); }  // the description is the next field, not a placeholder invented here
      status_ = "declared " + name + " \xE2\x80\x94 say what it does, then bind a key to it";
      Outcome o = commit_current();
      set_options(menu_, "actions", action_items());
      return o;
    }
    if (id.rfind("action.", 0) == 0 && id.size() > 12 && id.substr(id.size() - 5) == ".desc") {
      const std::string name = id.substr(7, id.size() - 12);
      for (RolltuiLayoutAction& d : current_.actions)
        if (view_of(d.name) == name) { begin_preview(); set_str(d.description, value); return commit_current(); }
      return {O::None, {}};
    }
    if (id == "size") {
      if (std::optional<SplitSize> s = parse_size_text(value)) { begin_preview(); if (Node* n = sel_node()) n->size = *s; return commit_current(); }
      cancel_preview();
      status_ = "'" + value + "' is not a size (fill | fill N | N% | cells)";
      return {O::Changed, {}};
    }
    if (id == "popup.add") {
      if (value.empty() || current_.popup(std::string_view(value).data(), std::string_view(value).size())) { status_ = value.empty() ? "a popup needs an id" : "a popup named '" + value + "' exists"; return {O::Changed, {}}; }
      begin_preview();
      Layer l{};
      set_str(l.id, value);
      l.placement = {Dim::rel(0.5), Dim::rel(0.5), Dim::rel(0.5), Dim::abs(8), Anchor::Center, true, {}, {}, {}, {}};
      l.modal = true;
      Node n = Node::window(("text:" + value).c_str());
      set_str(n.id, value);
      n.border = Border::Rounded;
      set_str(n.title, value);
      n.focusable = true;
      l.root = std::move(n);
      current_.popups.push_back(std::move(l));
      status_ = "added popup " + value;
      Outcome o = commit_current();
      rebuild_menu();
      return o;
    }
    if (id.rfind("popup.", 0) == 0) {
      const std::size_t dot = id.rfind('.');
      const std::string pid = id.substr(6, dot - 6), field = id.substr(dot + 1);
      std::optional<Dim> d = parse_dim(value);
      if (!d) {
        // A bare integer is cells (parse_dim refuses it by design: a size is explicit).
        char* end = nullptr;
        const long v = std::strtol(value.c_str(), &end, 10);
        if (end && *end == '\0' && !value.empty()) d = Dim::abs(static_cast<int>(v));
      }
      if (!d) { cancel_preview(); status_ = "'" + value + "' is not a dim (N | N% | N% ± cells)"; return {O::Changed, {}}; }
      for (Layer& p : current_.popups)
        if (view_of(p.id) == pid) {
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
  if (kind == ROLLTUI_MENU_EVENT_CLOSED) return {O::Closed, {}};
  // ---- live preview while a choice is highlighted or an input is being typed ----
  const MenuItem* sel = rolltui_menu_selected_item(menu_);
  const std::string_view level = view_of(rolltui_menu_level(menu_)->id);
  const bool editing = rolltui_menu_editing(menu_) != 0;
  if (sel && !editing && level == "border") {
    if (auto b = border_from_name(view_of(sel->id))) { begin_preview(); if (Node* n = sel_node()) n->border = *b; return {O::Changed, {}}; }
  }
  if (sel && !editing && level == "kind") {
    begin_preview();
    if (!set_content(str_of(sel->id), carried_source(str_of(sel->id)))) cancel_preview();
    return {O::Changed, {}};
  }
  if (sel && !editing && level == "menu_file") {
    begin_preview();
    set_content("menu", str_of(sel->id));
    return {O::Changed, {}};
  }
  if (editing && sel) {
    // The editing text, never the item's value: that is the committed one.
    std::size_t etext_len = 0;
    const char* etext_p = rolltui_input_text(rolltui_menu_editor(menu_), &etext_len);
    const std::string_view editing_text(etext_p, etext_len);
    if (sel->id == "title") { begin_preview(); if (Node* n = sel_node()) set_str(n->title, editing_text); return {O::Changed, {}}; }
    if (sel->id == "source") {
      const ContentParts p = parts_of(find_node((preview_ ? *preview_ : current_).base.root, sel_));
      if (p.content) {
        begin_preview();
        set_content(p.kind_text, std::string(editing_text));
      }
      return {O::Changed, {}};
    }
    if (sel->id == "size") {
      begin_preview();
      if (std::optional<SplitSize> s = parse_size_text(editing_text)) { if (Node* n = sel_node()) n->size = *s; }
      else if (Node* n = sel_node()) n->size = find_node(preview_->base.root, sel_)->size;
      return {O::Changed, {}};
    }
  }
  if (preview_ && !drag_) { cancel_preview(); return {O::Changed, {}}; }
  return {O::None, {}};
}

}  // namespace rolltui::tools
