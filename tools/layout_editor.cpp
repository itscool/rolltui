// rolltui/tools/layout_editor.cpp — see layout_editor.hpp.
#include "tool_str.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_layout.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_style.h" /* INTERNAL: the role VOCABULARY, for the background choice */
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
std::optional<Content> content_for_kind(const RolltuiContext* ctx, std::string_view kind_name, std::string source = {}) {
  if (rolltui_widget_kind_resolve(ctx, kind_name.data(), kind_name.size(), nullptr, nullptr, nullptr, nullptr) ==
      ROLLTUI_KIND_UNKNOWN)
    return std::nullopt;
  Content c;
  set_str(c.kind, kind_name);
  set_str(c.source, std::move(source));
  return c;
}
std::string content_kind_name(const Content& c) { return str_of(c.kind); }
unsigned char content_source_rule(const RolltuiContext* ctx, const Content& c) {
  unsigned char rule = ROLLTUI_SOURCE_REQUIRED;
  rolltui_widget_kind_resolve(ctx, c.kind.data(), c.kind.size(), nullptr, &rule, nullptr, nullptr);
  return rule;
}
// The kind's source SHAPE — a row of the registry, whichever rung it came from. This was the
// one per-kind rule the retired enum had been carrying silently (`Text || File`).
unsigned char content_source_shape(const RolltuiContext* ctx, const Content& c) {
  std::size_t row = 0;
  if (rolltui_widget_kind_resolve(ctx, c.kind.data(), c.kind.size(), &row, nullptr, nullptr, nullptr) ==
      ROLLTUI_KIND_UNKNOWN)
    return ROLLTUI_SOURCE_SHAPE_NAME;
  return rolltui_widget_kind_source_shape(row);
}
std::string content_source_describes(const RolltuiContext* ctx, const Content& c) {
  const char* source_is = nullptr;
  std::size_t len = 0;
  rolltui_widget_kind_resolve(ctx, c.kind.data(), c.kind.size(), nullptr, nullptr, &source_is, &len);
  return source_is ? std::string(source_is, len) : std::string();
}
std::string content_to_string(const RolltuiContext* ctx, const Content& c) {
  RolltuiStr out{};
  rolltui_content_format(c.kind.data(), c.kind.size(), c.source.data(), c.source.size(), content_source_rule(ctx, c), &out);
  const std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}
std::optional<Content> parse_content(const RolltuiContext* ctx, std::string_view text, std::string* why = nullptr) {
  std::size_t row = 0;
  int is_host = 0;
  const char *name_p = nullptr, *source_p = nullptr;
  std::size_t name_len = 0, source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why_c{};
  const int ok = rolltui_content_parse(ctx, text.data(), text.size(), &row, &is_host, &name_p, &name_len, &source_p,
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
// The offered names as one line, for a field's hint. Every list here is short (the library's
// seven kinds, a preset directory's menus), so a plain join is the whole of it.
std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& n : names) out += (out.empty() ? "" : " | ") + n;
  return out;
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
Layout builtin_layout(RolltuiContext* ctx, std::string_view name) {
  std::size_t text_len = 0;
  const char* text = rolltui_layout_builtin_json(name.data(), name.size(), &text_len);
  std::size_t default_n = 0;
  const RolltuiLayoutAction* default_actions = rolltui_layout_shipped_default_actions(ctx, &default_n);
  RolltuiLoadedLayout loaded;
  rolltui_loaded_layout_init(&loaded);
  RolltuiLayoutReport rep{};
  rolltui_load_layout_text_into(text, text_len, &loaded, default_actions, default_n, rolltui_layout_default_hooks(), &rep);
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
  const std::vector<std::string> ids = all_ids();
  auto taken = [&](const std::string& s) { return std::find(ids.begin(), ids.end(), s) != ids.end(); };
  if (!taken(base)) return base;
  for (int n = 2;; ++n) {
    const std::string s = base + "-" + std::to_string(n);
    if (!taken(s)) return s;
  }
}

// ---- construction ---------------------------------------------------------------------

LayoutEditor::LayoutEditor(RolltuiContext* ctx) : ctx_(ctx) {
  // The library's own table until a host says otherwise: a tool that has been told
  // nothing about a target app can only honestly offer the kinds every host has.
  for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) {
    std::size_t len = 0;
    const char* name = rolltui_widget_kind_name(ctx_, i, &len);
    kinds_.emplace_back(name, len);
  }
  current_ = builtin_layout(ctx_, "default");
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

// PHASE 26: these two lists became HINTS under a field that accepts anything, and that is the
// milestone rather than a detail of it. Both were closed CHOICES: a kind or a menu file this
// binary could resolve, or nothing. That made the tool the authority on what an app may be
// asked for — a designer working on a screen for another app could not name that app's canvas
// at all. What they can name is now unbounded; what this tool can PREVIEW is what the hint
// says, and everything else previews as a labelled placeholder.
void LayoutEditor::set_kinds(std::vector<std::string> names) {
  kinds_ = std::move(names);
  sync_hints();
  sync_content_fields();
}

void LayoutEditor::set_menus(std::vector<std::string> names) {
  menus_ = std::move(names);
  sync_hints();
  sync_content_fields();
}

// Both hints, from one place, called by the two setters AND by `rebuild_menu` — which rebuilds
// the items from scratch, so a hint set only in a setter would vanish on the next split and
// never come back. The constructor fills `kinds_` directly and reaches this the same way.
void LayoutEditor::sync_hints() {
  if (MenuItem* it = find(menu_, "kind"))
    set_str(it->spec.hint, kinds_.empty() ? "any widget kind the target app registers" : "this tool previews: " + joined(kinds_));
  if (MenuItem* it = find(menu_, "menu_file"))
    set_str(it->spec.hint, menus_.empty() ? "a menus/<name>.json the app can resolve" : "resolves here: " + joined(menus_));
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

const Node* LayoutEditor::selected_node() const { return find_any(sel_); }
Node* LayoutEditor::sel_node() { return find_any(sel_); }

std::vector<std::string> LayoutEditor::all_ids() const {
  std::vector<std::string> out = ids_in_order(current_.base.root);
  for (const Layer& p : current_.popups)
    for (std::string& s : ids_in_order(p.root)) out.push_back(std::move(s));
  return out;
}
Node* LayoutEditor::find_any(std::string_view id) {
  if (Node* n = find_node(current_.base.root, id)) return n;
  for (Layer& p : current_.popups)
    if (Node* n = find_node(p.root, id)) return n;
  return nullptr;
}
const Node* LayoutEditor::find_any(std::string_view id) const { return const_cast<LayoutEditor*>(this)->find_any(id); }
Layer* LayoutEditor::popup_of(std::string_view id) {
  for (Layer& p : current_.popups)
    if (find_node(p.root, id)) return &p;
  return nullptr;
}
const Layer* LayoutEditor::popup_of(std::string_view id) const { return const_cast<LayoutEditor*>(this)->popup_of(id); }

void LayoutEditor::select(std::string_view id) {
  if (find_node(current_.base.root, id)) { sel_ = std::string(id); sync_values(); }
}

void LayoutEditor::select_next(bool backwards) {
  const std::vector<std::string> ids = all_ids();
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
  std::vector<MenuItem> borders, anchors, loads;
  for (const char* b : {"none", "single", "rounded", "double", "heavy"}) borders.push_back(MenuItem::action(b, b));
  for (const char* a : {"top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right"}) anchors.push_back(MenuItem::action(a, a));
  for (const std::string& n : layouts_) loads.push_back(MenuItem::action(std::string(n).c_str(), std::string(n).c_str()));
  InputSpec dim, opt_dim, size, name, text, threshold;
  dim.type = InputType::Dim;
  opt_dim.type = InputType::Dim;
  opt_dim.optional = true;  // a bound may be absent, and empty is how you take one off
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
    // THE FOUR OPTIONAL BOUNDS AND THE CLAMP, added at Phase 27 m4. The shipped `default`
    // layout gives every popup a `min_w` and a `max_w` and the editor could not set either, so
    // a popup authored here spread to whatever `w` said on a 200-column terminal. They are
    // OPTIONAL dims: empty is a real answer and means unbounded, which is why the field is
    // optional text rather than a number with a sentinel nobody could guess.
    for (const auto& [suffix, label, od] : {std::tuple{"min_w", "min width", p.placement.min_w},
                                            {"max_w", "max width", p.placement.max_w},
                                            {"min_h", "min height", p.placement.min_h},
                                            {"max_h", "max height", p.placement.max_h}})
      fields.push_back(MenuItem::input((base + "." + suffix).c_str(), label, opt_dim.clone(),
                                       od.present ? dim_to_string(od.d).c_str() : ""));
    fields.push_back(MenuItem::toggle((base + ".clamp").c_str(), "clamp to the screen", p.placement.clamp != 0));
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
  // THE BACKGROUND ROLE, added at Phase 27 m4. A node has carried one since the layout format
  // did, the shipped screens use it (a banner, every popup), and the editor could not set it —
  // so a person who wanted one edited the JSON. A CHOICE over the role table read from the
  // library, never a copy of it: whoever owns a vocabulary owns exactly one spelling of it.
  std::vector<MenuItem> grounds;
  for (unsigned char r = 0; r < ROLLTUI_ROLE_COUNT; ++r) {
    std::size_t rn = 0;
    const char* nm = rolltui_role_name(r, &rn);
    if (nm && rn) grounds.push_back(MenuItem::action(std::string(nm, rn).c_str(), std::string(nm, rn).c_str()));
  }
  top.push_back(choice_of("background", "Background role", std::move(grounds), "default_background"));
  // THE WINDOW ID, added at Phase 27 m3 because building an app from nothing found it
  // missing: every node the editor created was `main`, `main-2`, `main-row`, and a person
  // who wanted a window named after what it shows had to edit the JSON. That made a THIRD
  // thing you cannot
  // design without writing JSON, and m4's claim is that there are exactly two. It is a
  // Name, not free text: a layout's `focus` names it and a report quotes it. Called a NODE
  // id and not a window id because a row and a column carry one too, and because every other
  // tree operation in this menu says node ("Select the next node", "Delete this node").
  top.push_back(MenuItem::input("id", "Node id", name.clone()));
  top.push_back(MenuItem::input("title", "Title", text.clone()));
  top.push_back(MenuItem::input("kind", "Widget kind", name.clone()));
  top.push_back(MenuItem::input("source", "Source", text.clone()));
  top.push_back(MenuItem::input("menu_file", "Menu file", name.clone()));
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
  sync_hints();
  sync_values();
}

LayoutEditor::ContentParts LayoutEditor::parts_of(const Node* n) const {
  ContentParts p;
  if (!n || !n->is_window()) return p;
  const std::string_view content = view_of(n->content);
  const std::size_t colon = content.find(':');
  p.window = true;
  p.kind_text = content.substr(0, colon);
  if (colon != std::string_view::npos) p.source = content.substr(colon + 1);
  // PHASE 26: the split is unconditional and `known` is a separate answer. It used to be a
  // `std::optional<Content>` that went empty for a kind neither rung of the registry had,
  // which made every field below inert — the tool refusing to hold a screen it could not
  // build. A screen is the intent; whether THIS binary can preview it is a different
  // question, asked here and answered in the hint rather than by disabling the field.
  p.known = content_for_kind(ctx_, p.kind_text).has_value();
  set_str(p.content.kind, p.kind_text);
  set_str(p.content.source, p.source);
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
//
// PHASE 26: A NAME IN NEITHER RUNG IS WRITTEN, AND SAID. Until now this refused it outright
// ("'canvas' is not a widget kind this app can build") — the design tool deciding what the
// app is allowed to be asked for, which is the exact direction this phase reverses. What a
// screen names is the DEVELOPER's to answer; all this tool knows is whether it can draw a
// preview, so that is all it says.
bool LayoutEditor::set_content(const std::string& kind_name, const std::string& source) {
  Node* n = sel_node();
  if (!n || !n->is_window()) return false;
  Content c;
  set_str(c.kind, kind_name);
  set_str(c.source, source);
  set_str(n->content, content_to_string(ctx_, c));
  if (!content_for_kind(ctx_, kind_name))
    status_ = "'" + kind_name + "' is not a kind this tool can build — it previews as a placeholder";
  return true;
}

// Which field owns the source, and what it accepts, are functions of the kind (the
// header comment's table). Disabled is drawn muted, so exactly one of Source / Menu
// file is offered at a time and neither is a second spelling of the other.
void LayoutEditor::sync_content_fields() {
  const ContentParts p = content_parts();
  const bool window = p.window;
  const bool is_menu = p.kind_text == "menu";
  set_value(menu_, "kind", p.kind_text);
  set_value(menu_, "source", p.source);
  set_value(menu_, "menu_file", is_menu ? p.source : std::string());
  set_enabled(menu_, "kind", window);
  set_enabled(menu_, "menu_file", window && is_menu);
  // The rule is the KIND's, whichever rung it came from. A kind this binary does not know is
  // the REQUIRED default: it is the only answer that keeps the field usable, and a foreign
  // kind that turns out to take no source loses nothing — `rolltui_content_format` drops a
  // source the kind may not have, in the app that owns the rule.
  const unsigned char rule = content_source_rule(ctx_, p.content);
  const bool source_field = window && !is_menu && rule != ROLLTUI_SOURCE_FORBIDDEN;
  set_enabled(menu_, "source", source_field);
  if (MenuItem* it = find(menu_, "source"); it && source_field) {
    // A path is not a Name; a literal is anything and may be empty — the kind's SHAPE, read
    // from its registry row whichever rung it came from (Phase 18 m2).
    it->spec.type = content_source_shape(ctx_, p.content) == ROLLTUI_SOURCE_SHAPE_TEXT ? InputType::Text : InputType::Name;
    it->spec.optional = rule == ROLLTUI_SOURCE_OPTIONAL;
    it->spec.hint.clear();
    for (const std::string& c : sources_)
      if (std::optional<Content> oc = parse_content(ctx_, c); oc && content_kind_name(*oc) == p.kind_text && !oc->source.empty())
        set_str(it->spec.hint, str_of(it->spec.hint) + (it->spec.hint.empty() ? "" : " | ") + str_of(oc->source));
    if (it->spec.hint.empty())
      set_str(it->spec.hint, p.known ? content_source_describes(ctx_, p.content)
                                     : "what '" + p.kind_text + "' is given in the app this screen is for");
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
  set_value(menu_, "id", sel_);
  set_value(menu_, "title", str_of(n->title));
  {
    std::size_t rn = 0;
    const char* nm = rolltui_role_name(static_cast<unsigned char>(n->background), &rn);
    set_value(menu_, "background", std::string(nm ? nm : "", rn));
  }
  set_value(menu_, "size", split_size_to_string(n->size));
  set_checked(menu_, "focusable", n->focusable);
  set_enabled(menu_, "focusable", n->is_window());
  sync_content_fields();
  if (MenuItem* root = find(menu_, "root")) {
    // WHICH TREE THE SELECTION IS IN, said rather than inferred. Popup nodes became selectable
    // at Phase 27 m4 and there are now two trees behind one id; a header that named only the
    // node would make `find` in the base layer and `find` inside the find popup
    // indistinguishable, which is the ambiguity this project's corollary exists to refuse.
    const Layer* in = popup_of(sel_);
    set_str(root->label, "layout editor \xE2\x80\xA2 " + sel_ +
                             (in ? " (in popup '" + std::string(view_of(in->id)) + "')" : "") +
                             (n->is_window() ? "" : n->kind == Node::Kind::Row ? " (row)" : " (column)"));
  }
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
  if (!find_any(sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
}

bool LayoutEditor::undo() {
  cancel_preview();
  if (!undo_.undo()) return false;
  current_ = (undo_.current()).clone();
  if (!find_any(sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
  status_ = "undone";
  return true;
}

bool LayoutEditor::redo() {
  cancel_preview();
  if (!undo_.redo()) return false;
  current_ = (undo_.current()).clone();
  if (!find_any(sel_)) { sel_.clear(); select_next(); }
  rebuild_menu();
  status_ = "redone";
  return true;
}

// ---- operations -----------------------------------------------------------------------

bool LayoutEditor::apply_op(Op op) {
  // The tree the operation happens IN: a popup's own, when the selection is inside one. Every
  // op below is relative to a root — a split's collapse, a delete's parent, a swap's siblings —
  // and taking the base tree's root while standing in a popup silently made all three no-ops
  // (`parent_of` simply would not find the node).
  Layer* owner = popup_of(sel_);
  Node& root = owner ? owner->root : current_.base.root;
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
      if (!parent) {
        status_ = owner ? "a popup's root window is the popup — remove the popup instead"
                        : "the root cannot be deleted";
        return false;
      }
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
  // A content this binary cannot resolve is said HERE as well as drawn as a placeholder,
  // and the wording is about the TOOL: under Phase 26 a foreign kind is not a fault in the
  // screen, it is a thing this preview cannot show.
  if (n->is_window())
    if (std::string why; !parse_content(ctx_, view_of(n->content), &why)) s += " \xE2\x80\x94 not previewable here: " + why;
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
      // SPLIT ON THE LAST DOT, the way the input handler above already does. This read
      // `id.substr(6, id.size() - 12)` and then set `modal` unconditionally — correct only
      // while `.modal` was the one toggle a popup had, and silently wrong the moment `.clamp`
      // joined it, since both suffixes are six characters long.
      const std::size_t dot = id.rfind('.');
      const std::string pid = id.substr(6, dot - 6), field = id.substr(dot + 1);
      for (Layer& p : current_.popups)
        if (view_of(p.id) == pid) {
          begin_preview();
          if (field == "clamp") p.placement.clamp = checked ? 1 : 0;
          else p.modal = checked;
          return commit_current();
        }
    }
    return {O::None, {}};
  }
  if (kind == ROLLTUI_MENU_EVENT_CHOOSE) {
    if (id == "border") {
      if (auto b = border_from_name(value)) { begin_preview(); if (Node* n = sel_node()) n->border = *b; }
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
    if (id == "id") {
      // A RENAME IS THREE EDITS, AND DOING ONE OF THEM IS THE BUG: the node's id, the
      // SELECTION (this editor selects by id, so a rename that forgets it deselects the node
      // you just renamed), and `focus` if it named this node (a dangling focus is silent —
      // the first focusable window in tree order takes it and nothing says why).
      Node* n = sel_node();
      if (!n) return {O::Changed, {}};
      if (value.empty()) { status_ = "a node needs an id"; return {O::Changed, {}}; }
      const std::string from = sel_;
      if (value == from) return {O::Changed, {}};
      // `unique_id` answers with a suffix rather than refusing, which is what every other id
      // this editor makes does; say so, because the author typed something else.
      const std::string to = unique_id(value);
      begin_preview();
      set_str(n->id, to);
      if (view_of(current_.base.focus) == from) set_str(current_.base.focus, to);
      sel_ = to;
      status_ = to == value ? "renamed '" + from + "' to '" + to + "'"
                            : "'" + value + "' was taken; renamed '" + from + "' to '" + to + "'";
      return commit_current();
    }
    if (id == "background") {
      const int role = rolltui_role_from_name(value.c_str(), value.size());
      if (role < 0) { status_ = "no role named '" + value + "'"; return {O::Changed, {}}; }
      begin_preview();
      if (Node* n = sel_node()) n->background = static_cast<rolltui::Role>(role);
      return commit_current();
    }
    if (id == "title") { begin_preview(); if (Node* n = sel_node()) set_str(n->title, value); return commit_current(); }
    // The kind carries the source over (`carried_source`), so retyping a kind does not silently
    // drop the name beside it — the one exception is `help`, whose source is a key scope.
    if (id == "kind") {
      if (value.empty()) { status_ = "a window needs a widget kind"; return {O::Changed, {}}; }
      begin_preview();
      set_content(value, carried_source(value));
      return commit_current();
    }
    if (id == "menu_file") { begin_preview(); set_content("menu", value); return commit_current(); }
    if (id == "source") {
      const ContentParts p = content_parts();
      if (!p.window) return {O::Changed, {}};
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
      const bool bound = field == "min_w" || field == "max_w" || field == "min_h" || field == "max_h";
      // AN EMPTY BOUND IS A REAL ANSWER — "unbounded" — and the only way to take one back off.
      if (bound && value.empty()) {
        for (Layer& p : current_.popups)
          if (view_of(p.id) == pid) {
            begin_preview();
            RolltuiOptDim& t = field == "min_w"   ? p.placement.min_w
                               : field == "max_w" ? p.placement.max_w
                               : field == "min_h" ? p.placement.min_h
                                                  : p.placement.max_h;
            t.reset();
            status_ = "popup '" + pid + "' " + field + " is now unbounded";
            return commit_current();
          }
      }
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
          else if (field == "min_w") p.placement.min_w = *d;
          else if (field == "max_w") p.placement.max_w = *d;
          else if (field == "min_h") p.placement.min_h = *d;
          else if (field == "max_h") p.placement.max_h = *d;
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
  if (editing && sel) {
    // The editing text, never the item's value: that is the committed one.
    std::size_t etext_len = 0;
    const char* etext_p = rolltui_input_text(rolltui_menu_editor(menu_), &etext_len);
    const std::string_view editing_text(etext_p, etext_len);
    if (sel->id == "title") { begin_preview(); if (Node* n = sel_node()) set_str(n->title, editing_text); return {O::Changed, {}}; }
    if (sel->id == "kind") {
      if (!editing_text.empty()) { begin_preview(); set_content(std::string(editing_text), carried_source(editing_text)); }
      return {O::Changed, {}};
    }
    if (sel->id == "menu_file") { begin_preview(); set_content("menu", std::string(editing_text)); return {O::Changed, {}}; }
    if (sel->id == "source") {
      const ContentParts p = parts_of(find_node((preview_ ? *preview_ : current_).base.root, sel_));
      if (p.window) {
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
