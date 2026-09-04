// rolltui/Layout.cpp — the SHIM over `rolltui/c/rolltui_layout.h`: the RAII, the JSON
// loader, the built-ins, the styling vocabulary and the translation of one `std::function`
// into a function pointer. The two implementations live in `LayoutCpp.cpp` and
// `c/rolltui_layout.c`, and `-DROLLTUI_C` picks which one links (Phase 15 m5).
//
// WHAT STAYS HERE AND WHY, since it is most of the file: the LOADER and the BUILT-INS.
// That is the split m3 made for `Theme` — only the colour engine crossed, and the JSON
// loader never moved — for the same reason: `json::Value` is a C++ tree with no business at
// a C boundary, and the ENGLISH in a report is a vocabulary that would otherwise exist
// twice. The C decides which rung resolved a widget kind and what rule its source follows;
// the sentences are composed here.
#include "rolltui/Layout.hpp"

#include "rolltui/Lifetime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Json.hpp"
#include "rolltui/Scratch.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

// ---- content: the widget kind and its source ----------------------------------------------
// Rung 1 and rung 2 both live in the C (`rolltui_layout.h`); what a Content IS, and every
// sentence about one, lives here.

namespace {

std::string_view kind_name_at(std::size_t i) {
  std::size_t n = 0;
  const char* p = rolltui_widget_kind_library_name(i, &n);
  return std::string_view(p, n);
}

// The one place the enum and the C table are tied together, and it is CHECKED rather than
// assumed: the table's order IS `WidgetKind`'s, and `Registered` is the one value with no
// row by design.
constexpr std::size_t kLibraryKindCount = static_cast<std::size_t>(WidgetKind::Registered);

}  // namespace

std::string_view widget_kind_name(WidgetKind k) {
  const std::size_t i = static_cast<std::size_t>(k);
  return i < kLibraryKindCount ? kind_name_at(i) : std::string_view();
}

std::string_view content_kind_name(const Content& c) {
  return c.kind == WidgetKind::Registered ? std::string_view(c.registered_name) : widget_kind_name(c.kind);
}

std::optional<WidgetKind> widget_kind_from_name(std::string_view name) {
  unsigned char ordinal = 0;
  if (rolltui_widget_kind_resolve(name.data(), name.size(), &ordinal, nullptr, nullptr, nullptr) ==
      ROLLTUI_KIND_LIBRARY)
    return static_cast<WidgetKind>(ordinal);
  return std::nullopt;
}

SourceRule source_rule(WidgetKind k) {
  const std::size_t i = static_cast<std::size_t>(k);
  return static_cast<SourceRule>(rolltui_widget_kind_library_rule(i < kLibraryKindCount ? i : 0));
}

SourceRule content_source_rule(const Content& c) {
  if (c.kind != WidgetKind::Registered) return source_rule(c.kind);
  unsigned char rule = ROLLTUI_SOURCE_REQUIRED;
  rolltui_widget_kind_resolve(c.registered_name.data(), c.registered_name.size(), nullptr, &rule, nullptr,
                              nullptr);
  return static_cast<SourceRule>(rule);
}

std::string_view source_describes(WidgetKind k) {
  std::size_t n = 0;
  const std::size_t i = static_cast<std::size_t>(k);
  const char* p = rolltui_widget_kind_library_source_is(i < kLibraryKindCount ? i : 0, &n);
  return std::string_view(p, n);
}

std::string content_source_describes(const Content& c) {
  if (c.kind != WidgetKind::Registered) return std::string(source_describes(c.kind));
  const char* p = nullptr;
  std::size_t n = 0;
  if (rolltui_widget_kind_resolve(c.registered_name.data(), c.registered_name.size(), nullptr, nullptr, &p, &n) ==
      ROLLTUI_KIND_HOST)
    return std::string(p, n);
  return {};
}

// Rung 1 is checked FIRST and the refusal says so by name: the library's own kinds may
// never be shadowed, and this is one of the two independent guards (the other is that
// the C searches its table before the host's, so a shadowing row could not be reached
// even if one existed).
bool register_widget_kind(std::string name, SourceRule rule, std::string source_is, std::string* why) {
  const int verdict = rolltui_widget_kind_register(name.data(), name.size(), static_cast<unsigned char>(rule),
                                                   source_is.data(), source_is.size());
  if (verdict == ROLLTUI_REGISTER_OK) return true;
  if (why) switch (verdict) {
      case ROLLTUI_REGISTER_EMPTY: *why = "a widget kind needs a name"; break;
      case ROLLTUI_REGISTER_HAS_COLON:
        *why = "'" + name + "' is not a kind name: a ':' separates the kind from its source";
        break;
      case ROLLTUI_REGISTER_IS_LIBRARY:
        *why = "'" + name + "' is one of the library's own kinds and cannot be registered over";
        break;
      default: *why = "'" + name + "' is already registered with a different source rule"; break;
    }
  return false;
}

void clear_registered_widget_kinds() { rolltui_widget_kind_clear(); }

std::vector<std::string> widget_kind_names() {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) out.emplace_back(kind_name_at(i));
  for (std::size_t i = 0; i < rolltui_widget_kind_host_count(); ++i) {
    std::size_t n = 0;
    const char* p = rolltui_widget_kind_host_name(i, &n);
    out.emplace_back(p, n);
  }
  return out;
}

const std::vector<WidgetKind>& widget_kinds() {
  static const std::vector<WidgetKind> all = [] {
    std::vector<WidgetKind> v;
    for (std::size_t i = 0; i < kLibraryKindCount; ++i) v.push_back(static_cast<WidgetKind>(i));
    return v;
  }();
  return all;
}

std::optional<Content> parse_content(std::string_view text, std::string* why, ContentProblem* what) {
  if (what) *what = ContentProblem::None;
  auto fail = [&](ContentProblem kind, std::string reason) -> std::optional<Content> {
    if (why) *why = std::move(reason);
    if (what) *what = kind;
    return std::nullopt;
  };
  const std::size_t colon = text.find(':');
  const std::string_view name = text.substr(0, colon);
  Content c;
  unsigned char ordinal = 0, rule = ROLLTUI_SOURCE_REQUIRED;
  const char* describes = "";
  std::size_t describes_n = 0;
  // THE RESOLUTION ORDER (Layout.hpp) is the C's; rung 3 is this failing with a named
  // reason, which is the half that has to be in a language with sentences.
  const int rung =
      rolltui_widget_kind_resolve(name.data(), name.size(), &ordinal, &rule, &describes, &describes_n);
  if (rung == ROLLTUI_KIND_LIBRARY) {
    c.kind = static_cast<WidgetKind>(ordinal);
  } else if (rung == ROLLTUI_KIND_HOST) {
    c.kind = WidgetKind::Registered;
    c.registered_name = std::string(name);
  } else {
    std::string known;
    for (const std::string& n : widget_kind_names()) known += (known.empty() ? "" : " | ") + n;
    if (std::optional<std::string> m = migrated_content(text))
      return fail(ContentProblem::UnknownKind, "'" + std::string(text) + "' is an older spelling, not a widget kind; write '" + *m + "'");
    return fail(ContentProblem::UnknownKind, "'" + std::string(name) + "' is not a widget kind (" + known + ")");
  }
  const std::string kname(name);
  if (colon != std::string_view::npos) c.source = std::string(text.substr(colon + 1));
  if (rule == ROLLTUI_SOURCE_FORBIDDEN && colon != std::string_view::npos)
    return fail(ContentProblem::ForbiddenSource, "'" + kname + "' takes no source; write '" + kname + "'");
  if (rule == ROLLTUI_SOURCE_REQUIRED && c.source.empty()) {
    if (std::optional<std::string> m = migrated_content(text))  // an older name that is also a kind name
      return fail(ContentProblem::MissingSource, "'" + std::string(text) + "' is an older spelling, not a content; write '" + *m + "'");
    return fail(ContentProblem::MissingSource,
                "'" + kname + "' needs a source (" + std::string(describes, describes_n) + "): write '" + kname + ":<name>'");
  }
  return c;
}

// The same two rungs as parse_content, in the same order, with the source carried
// through unjudged — see Layout.hpp for why a design tool needs that and a loader does not.
std::optional<Content> content_for_kind(std::string_view kind_name, std::string source) {
  Content c;
  c.source = std::move(source);
  unsigned char ordinal = 0;
  switch (rolltui_widget_kind_resolve(kind_name.data(), kind_name.size(), &ordinal, nullptr, nullptr, nullptr)) {
    case ROLLTUI_KIND_LIBRARY: c.kind = static_cast<WidgetKind>(ordinal); return c;
    case ROLLTUI_KIND_HOST:
      c.kind = WidgetKind::Registered;
      c.registered_name = std::string(kind_name);
      return c;
    default: return std::nullopt;
  }
}

std::string content_to_string(const Content& c) {
  std::string s(content_kind_name(c));
  // An OPTIONAL source that is empty writes no colon at all: `help` and `text` are then
  // spelled the way every file already spells them, and both forms parse to the same
  // Content, so this is one spelling rather than two. A REQUIRED source that is empty
  // keeps its colon — `transcript:` is a window saying out loud that it needs a name.
  const SourceRule rule = content_source_rule(c);
  if (rule == SourceRule::Required || (rule == SourceRule::Optional && !c.source.empty())) s += ":" + c.source;
  return s;
}

std::optional<std::string> migrated_content(std::string_view legacy) {
  std::size_t n = 0;
  if (const char* to = rolltui_migrated_content(legacy.data(), legacy.size(), &n)) return std::string(to, n);
  return std::nullopt;
}

// ---- tree basics -------------------------------------------------------------------------

const Layer* Layout::popup(std::string_view id) const {
  for (const Layer& l : popups)
    if (l.id == id) return &l;
  return nullptr;
}

// ---- names and text forms ----------------------------------------------------------------

namespace {
constexpr std::string_view kAnchorNames[] = {"top-left", "top", "top-right", "left", "center",
                                             "right", "bottom-left", "bottom", "bottom-right"};
constexpr std::string_view kBorderNames[] = {"none", "single", "rounded", "double", "heavy"};

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

bool parse_int(std::string_view s, int& out) {
  s = trim(s);
  if (s.empty()) return false;
  std::size_t i = 0;
  bool neg = false;
  if (s[i] == '-' || s[i] == '+') { neg = s[i] == '-'; ++i; }
  if (i >= s.size()) return false;
  long v = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
    if (v > 1000000) return false;
  }
  out = static_cast<int>(neg ? -v : v);
  return true;
}
}  // namespace

std::string_view anchor_name(Anchor a) { return kAnchorNames[static_cast<std::size_t>(a)]; }
std::optional<Anchor> anchor_from_name(std::string_view name) {
  for (std::size_t i = 0; i < 9; ++i)
    if (kAnchorNames[i] == name) return static_cast<Anchor>(i);
  return std::nullopt;
}
std::string_view border_name(Border b) { return kBorderNames[static_cast<std::size_t>(b)]; }
std::optional<Border> border_from_name(std::string_view name) {
  for (std::size_t i = 0; i < 5; ++i)
    if (kBorderNames[i] == name) return static_cast<Border>(i);
  return std::nullopt;
}

// THE TEXT FORMS ARE THE BOUNDARY'S (Phase 15 m5), because the MENU needed them: a `size`
// or `dim` field checks a keystroke as a prefix of a valid value and canonicalises it, so
// the C menu widget has to parse and print a Dim too. These five are the C++ spelling over
// `rolltui/c/rolltui_layout.h`, and there is still one definition of what a dim looks like.
std::optional<Dim> parse_dim(std::string_view text) {
  Dim d;
  if (!rolltui_parse_dim(text.data(), text.size(), &d)) return std::nullopt;
  return d;
}

std::string dim_to_string(Dim d) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_dim_to_string(d, buf, sizeof buf));
}

std::optional<SplitSize> parse_split_size(std::string_view text) {
  SplitSize s;
  if (!rolltui_parse_split_size(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

std::optional<SplitSize> parse_size_text(std::string_view text) {
  SplitSize s;
  if (!rolltui_parse_size_text(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

std::string split_size_to_string(SplitSize s) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_split_size_to_string(s, buf, sizeof buf));
}

// ---- the loader ----------------------------------------------------------------------------

namespace {

using json::Value;

// A dim from JSON: an integer number → cells; a string → parse_dim.
std::optional<Dim> dim_from_json(const Value& v, const std::string& where, LayoutLoadReport& rep) {
  if (v.is_number()) {
    if (v.num != std::floor(v.num) || std::fabs(v.num) > 1000000) {
      rep.bad_values.push_back(where + ": a number is whole cells; use \"N%\" for a fraction");
      return std::nullopt;
    }
    return Dim::abs(static_cast<int>(v.num));
  }
  if (v.is_string()) {
    if (auto d = parse_dim(v.str)) return d;
    rep.bad_values.push_back(where + ": '" + v.str + "' is not a dim (an integer, or \"N%\" with an optional \"± cells\")");
    return std::nullopt;
  }
  rep.bad_values.push_back(where + ": expected an integer or a \"N%\" string");
  return std::nullopt;
}

bool bool_from_json(const Value& v, const std::string& where, LayoutLoadReport& rep, bool& out) {
  if (!v.is_bool()) { rep.bad_values.push_back(where + ": expected true or false"); return false; }
  out = v.b;
  return true;
}
// The same for the flags that cross the boundary as BYTES. One overload rather than a cast
// at each of the four call sites, because a cast at a call site is where a `visible` that
// should have been `focusable` hides.
bool bool_from_json(const Value& v, const std::string& where, LayoutLoadReport& rep, unsigned char& out) {
  bool b = out != 0;
  if (!bool_from_json(v, where, rep, b)) return false;
  out = static_cast<unsigned char>(b);
  return true;
}

void collect_ids(const Node& n, std::vector<std::string>& seen, const std::string& where, LayoutLoadReport& rep) {
  if (!n.id.empty()) {
    if (std::find(seen.begin(), seen.end(), n.id.view()) != seen.end()) rep.bad_values.push_back(where + ".id: duplicate id '" + n.id + "'");
    else seen.emplace_back(n.id.view());
  }
  for (std::size_t i = 0; i < n.children.size(); ++i)
    collect_ids(n.children[i], seen, where + (n.kind == Node::Kind::Row ? ".row[" : ".column[") + std::to_string(i) + "]", rep);
}

Node node_from_json(const Value& v, const std::string& where, LayoutLoadReport& rep) {
  Node n;
  if (!v.is_object()) { rep.bad_values.push_back(where + ": expected a node object"); return n; }
  const bool has_row = v.has("row"), has_col = v.has("column"), has_content = v.has("content");
  if ((has_row ? 1 : 0) + (has_col ? 1 : 0) + (has_content ? 1 : 0) != 1) {
    rep.bad_values.push_back(where + ": a node has exactly one of \"content\", \"row\", \"column\"");
    return n;
  }
  n.kind = has_row ? Node::Kind::Row : has_col ? Node::Kind::Column : Node::Kind::Window;
  for (const auto& [k, x] : v.obj) {
    const std::string at = where + "." + k;
    if (k == "row" || k == "column") {
      if (!x.is_array()) { rep.bad_values.push_back(at + ": expected an array of nodes"); continue; }
      for (std::size_t i = 0; i < x.arr.size(); ++i)
        n.children.push_back(node_from_json(x.arr[i], at + "[" + std::to_string(i) + "]", rep));
    } else if (k == "content") {
      if (!x.is_string()) rep.bad_values.push_back(at + ": expected a string");
      else n.content = x.str;
    } else if (k == "id") {
      if (!x.is_string()) rep.bad_values.push_back(at + ": expected a string");
      else n.id = x.str;
    } else if (k == "title") {
      if (!x.is_string()) rep.bad_values.push_back(at + ": expected a string");
      else n.title = x.str;
    } else if (k == "border") {
      auto b = x.is_string() ? border_from_name(x.str) : std::nullopt;
      if (!b) rep.bad_values.push_back(at + ": expected none | single | rounded | double | heavy");
      else n.border = *b;
    } else if (k == "background") {
      Role r = x.is_string() ? role_from_name(x.str) : Role::count_;
      if (r == Role::count_) rep.bad_values.push_back(at + ": expected a role name");
      else n.background = r;
    } else if (k == "focusable") {
      bool_from_json(x, at, rep, n.focusable);
    } else if (k == "visible") {
      bool_from_json(x, at, rep, n.visible);
    } else if (k == "size") {
      if (x.is_number()) {
        if (auto d = dim_from_json(x, at, rep)) n.size = SplitSize::fixed(*d);
      } else if (x.is_string()) {
        if (auto s = parse_split_size(x.str)) n.size = *s;
        else rep.bad_values.push_back(at + ": '" + x.str + "' is not a size (an integer, \"N%\", \"fill\" or \"fill N\")");
      } else {
        rep.bad_values.push_back(at + ": expected an integer, \"N%\", \"fill\" or \"fill N\"");
      }
    } else {
      rep.unknown_keys.push_back(at);
    }
  }
  // Content is kind[:source] (Layout.hpp). A Phase 9 slot name is rewritten once and
  // said so; anything else the table does not know is a bad value that names the fix.
  if (n.is_window()) {
    if (std::optional<std::string> to = migrated_content(n.content)) {
      rep.migrated.push_back(where + ".content: '" + n.content + "' \xE2\x86\x92 '" + *to + "'");
      if (n.id.empty()) n.id = n.content;  // the id it had before the rewrite, so lookups keep working
      n.content = *to;
    }
    std::string why;
    ContentProblem what = ContentProblem::None;
    // An UNKNOWN KIND is deliberately NOT a bad value here — see ContentProblem in
    // Layout.hpp. The vocabulary's second rung belongs to the host, and this loader runs
    // before a host has necessarily registered anything; Windows reports it, by name,
    // with the error panel drawn. Every other problem is a fact about the STRING and is
    // the loader's to name.
    if (!parse_content(n.content, &why, &what) && what != ContentProblem::UnknownKind)
      rep.bad_values.push_back(where + ".content: " + why);
  }
  if (n.is_window() && n.id.empty()) n.id = n.content;
  return n;
}

Layer layer_from_json(const Value& v, const std::string& where, bool is_popup, LayoutLoadReport& rep) {
  Layer l;
  if (!v.is_object()) { rep.bad_values.push_back(where + ": expected an object"); return l; }
  bool have_root = false;
  for (const auto& [k, x] : v.obj) {
    const std::string at = where + "." + k;
    if (k == "root") { l.root = node_from_json(x, at, rep); have_root = true; }
    else if (k == "focus") { if (!x.is_string()) rep.bad_values.push_back(at + ": expected a window id"); else l.focus = x.str; }
    else if (is_popup && k == "id") { if (!x.is_string()) rep.bad_values.push_back(at + ": expected a string"); else l.id = x.str; }
    else if (is_popup && k == "modal") bool_from_json(x, at, rep, l.modal);
    else if (is_popup && k == "clamp") bool_from_json(x, at, rep, l.placement.clamp);
    else if (is_popup && k == "anchor") {
      auto a = x.is_string() ? anchor_from_name(x.str) : std::nullopt;
      if (!a) rep.bad_values.push_back(at + ": expected top-left | top | top-right | left | center | right | bottom-left | bottom | bottom-right");
      else l.placement.anchor = *a;
    } else if (is_popup && (k == "x" || k == "y" || k == "w" || k == "h")) {
      if (auto d = dim_from_json(x, at, rep)) {
        if (k == "x") l.placement.x = *d;
        else if (k == "y") l.placement.y = *d;
        else if (k == "w") l.placement.w = *d;
        else l.placement.h = *d;
      }
    } else if (is_popup && (k == "min_w" || k == "min_h" || k == "max_w" || k == "max_h")) {
      if (auto d = dim_from_json(x, at, rep)) {
        if (k == "min_w") l.placement.min_w = *d;
        else if (k == "min_h") l.placement.min_h = *d;
        else if (k == "max_w") l.placement.max_w = *d;
        else l.placement.max_h = *d;
      }
    } else {
      rep.unknown_keys.push_back(at);
    }
  }
  if (!have_root) rep.bad_values.push_back(where + ": no \"root\" node");
  std::vector<std::string> ids;
  collect_ids(l.root, ids, where + ".root", rep);
  if (!l.focus.empty() && std::find(ids.begin(), ids.end(), l.focus.view()) == ids.end())
    rep.bad_values.push_back(where + ".focus: no window with id '" + l.focus + "'");
  return l;
}

Value dim_to_json(Dim d) {
  if (d.fraction == 0) return Value::number(d.cells);
  return Value::string(dim_to_string(d));
}

Value node_to_json(const Node& n) {
  Value o = Value::object();
  if (n.is_window()) {
    if (!(n.id == n.content)) o.set("id", Value::string(n.id.str()));
    o.set("content", Value::string(n.content.str()));
  } else if (!n.id.empty()) {
    o.set("id", Value::string(n.id.str()));
  }
  if (n.border != Border::None) o.set("border", Value::string(std::string(border_name(n.border))));
  if (!n.title.empty()) o.set("title", Value::string(n.title.str()));
  if (n.focusable) o.set("focusable", Value::boolean(true));
  if (!n.visible) o.set("visible", Value::boolean(false));
  if (n.background != Role::background) o.set("background", Value::string(std::string(role_name(n.background))));
  if (n.size != SplitSize{}) {
    if (!n.size.fill && n.size.dim.fraction == 0) o.set("size", Value::number(n.size.dim.cells));
    else o.set("size", Value::string(split_size_to_string(n.size)));
  }
  if (!n.is_window()) {
    Value arr = Value::array();
    for (const Node& c : n.children) arr.arr.push_back(node_to_json(c));
    o.set(n.kind == Node::Kind::Row ? "row" : "column", std::move(arr));
  }
  return o;
}

Value layer_to_json(const Layer& l, bool is_popup) {
  Value o = Value::object();
  if (is_popup) {
    o.set("id", Value::string(l.id.str()));
    o.set("x", dim_to_json(l.placement.x));
    o.set("y", dim_to_json(l.placement.y));
    o.set("w", dim_to_json(l.placement.w));
    o.set("h", dim_to_json(l.placement.h));
    if (l.placement.anchor != Anchor::TopLeft) o.set("anchor", Value::string(std::string(anchor_name(l.placement.anchor))));
    if (!l.placement.clamp) o.set("clamp", Value::boolean(false));
    if (l.placement.min_w) o.set("min_w", dim_to_json(*l.placement.min_w));
    if (l.placement.min_h) o.set("min_h", dim_to_json(*l.placement.min_h));
    if (l.placement.max_w) o.set("max_w", dim_to_json(*l.placement.max_w));
    if (l.placement.max_h) o.set("max_h", dim_to_json(*l.placement.max_h));
    if (l.modal) o.set("modal", Value::boolean(true));
  }
  if (!l.focus.empty()) o.set("focus", Value::string(l.focus.str()));
  o.set("root", node_to_json(l.root));
  return o;
}

}  // namespace

std::string action_decl_problem(std::string_view name) {
  const std::string_view scope = scope_of(name);
  if (scope == name || scope.empty() || name.size() <= scope.size() + 1)
    return "an action is \"<scope>.<verb>\", both parts non-empty";
  if (library_scope(scope))
    return "the '" + std::string(scope) + "' scope is the library's and cannot be declared";
  return {};
}

std::optional<Layout> load_layout(std::string_view json_text, LayoutLoadReport& report) {
  std::string err;
  Value root = json::parse(json_text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  return load_layout(root, report);
}

std::optional<Layout> load_layout(const Value& root, LayoutLoadReport& report) {
  if (!root.is_object()) { report.error = "layout file must be a JSON object"; return std::nullopt; }
  if (!root.has("root")) { report.error = "layout file has no \"root\" node"; return std::nullopt; }
  Layout out;
  Value base = Value::object();
  bool have_actions = false;
  for (const auto& [k, v] : root.obj) {
    if (k == "name") { if (!v.is_string()) report.bad_values.push_back("name: expected a string"); else out.name = v.str; }
    else if (k == "min_width" || k == "min_height") {
      if (!v.is_number() || v.num < 0 || v.num != std::floor(v.num)) report.bad_values.push_back(k + ": expected a whole number of cells");
      else (k == "min_width" ? out.min_width : out.min_height) = static_cast<int>(v.num);
    } else if (k == "root" || k == "focus") {
      base.set(k, v);
    } else if (k == "actions") {
      have_actions = true;
      if (!v.is_object()) { report.bad_values.push_back("actions: expected an object of action name \xE2\x86\x92 description"); continue; }
      for (const auto& [name, desc] : v.obj) {
        const std::string at = "actions." + name;
        if (const std::string why = action_decl_problem(name); !why.empty())
          report.bad_values.push_back(at + ": " + why);
        else if (std::find_if(out.actions.begin(), out.actions.end(), [&](const ActionDecl& d) { return d.name == name; }) != out.actions.end())
          report.bad_values.push_back(at + ": declared twice");
        else if (!desc.is_string())
          report.bad_values.push_back(at + ": expected a description string");
        else
          out.actions.push_back({name, desc.str});
      }
    } else if (k == "popups") {
      if (!v.is_array()) { report.bad_values.push_back("popups: expected an array"); continue; }
      for (std::size_t i = 0; i < v.arr.size(); ++i) {
        const std::string where = "popups[" + std::to_string(i) + "]";
        Layer p = layer_from_json(v.arr[i], where, true, report);
        if (p.id.empty()) report.bad_values.push_back(where + ": a popup needs an \"id\"");
        else if (out.popup(p.id.view())) report.bad_values.push_back(where + ".id: duplicate popup id '" + p.id + "'");
        out.popups.push_back(std::move(p));
      }
    } else {
      report.unknown_keys.push_back(k);
    }
  }
  out.base = layer_from_json(base, "", false, report);
  // A file written before actions existed (Phase 9, and every layout a user has saved
  // since) declares none — and would silently lose every app key. It is given the
  // shipped default's, named in `migrated` the way a Phase 9 content string is; the next
  // save writes them into the file. An explicit `"actions": {}` means none and is kept.
  if (!have_actions) {
    out.actions = shipped_default_actions();
    if (!out.actions.empty()) {
      std::string names;
      for (const ActionDecl& d : out.actions) names += (names.empty() ? "" : ", ") + d.name;
      report.migrated.push_back("actions: none declared; the shipped default's were added (" + names + ")");
    }
  }
  // The base's report paths begin with "." because its keys sit at the top level.
  for (std::vector<std::string>* list : {&report.unknown_keys, &report.bad_values})
    for (std::string& s : *list)
      if (!s.empty() && s[0] == '.') s.erase(0, 1);
  return out;
}

Value layout_to_json_value(const Layout& layout) {
  Value o = Value::object();
  o.set("name", Value::string(layout.name));
  if (layout.min_width) o.set("min_width", Value::number(layout.min_width));
  if (layout.min_height) o.set("min_height", Value::number(layout.min_height));
  // Always written, even when empty: an absent "actions" key means "a file from before
  // they existed" and is filled in by the loader, so a layout that deliberately declares
  // none has to be able to say so (see load_layout).
  {
    Value acts = Value::object();
    for (const ActionDecl& d : layout.actions) acts.set(d.name, Value::string(d.description));
    o.set("actions", std::move(acts));
  }
  Value base = layer_to_json(layout.base, false);
  for (auto& [k, v] : base.obj) o.set(k, std::move(v));
  if (!layout.popups.empty()) {
    Value arr = Value::array();
    for (const Layer& p : layout.popups) arr.arr.push_back(layer_to_json(p, true));
    o.set("popups", std::move(arr));
  }
  return o;
}

std::string layout_to_json(const Layout& layout) { return json::dump(layout_to_json_value(layout), 2); }

// ---- built-ins -----------------------------------------------------------------------------

// The built-ins ARE the Layout domain's shipped presets (Phase 10 m1): real files under
// rolltui/presets/layouts/, embedded by cmake/embed_presets.cmake into the table below
// and parsed here. ONE definition site — before m1 the same four layouts existed twice,
// once as a string here and (as the plan wanted them) once as a file. Presets.cpp reads
// the same table for LayoutDomain::shipped_at, so a shipped preset and its built-in
// cannot drift; presets_test asserts they are equal anyway, because "cannot" has been
// wrong before.
namespace embedded {
extern const std::pair<std::string_view, std::string_view> kLayoutPresets[];
extern const std::size_t kLayoutPresetCount;
}  // namespace embedded

namespace {

// Shipped order, the preset system's rule (PresetStore::shipped_names): "default"
// first — it is what a fresh install runs — then the table's own (alphabetical) order.
const std::vector<std::string_view>& builtin_names() {
  static const std::vector<std::string_view> names = [] {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < embedded::kLayoutPresetCount; ++i)
      if (embedded::kLayoutPresets[i].first == "default") out.push_back(embedded::kLayoutPresets[i].first);
    for (std::size_t i = 0; i < embedded::kLayoutPresetCount; ++i)
      if (embedded::kLayoutPresets[i].first != "default") out.push_back(embedded::kLayoutPresets[i].first);
    return out;
  }();
  return names;
}

std::string_view builtin_json(std::string_view name) {
  for (std::size_t i = 0; i < embedded::kLayoutPresetCount; ++i)
    if (embedded::kLayoutPresets[i].first == name) return embedded::kLayoutPresets[i].second;
  return "";
}

}  // namespace

// Read straight out of the shipped "default" file's "actions" object — NEVER through
// load_layout, which asks for these when a file declares none and would recurse into
// itself. One definition site is still the file; this is a direct read of one key of it.
const std::vector<ActionDecl>& shipped_default_actions() {
  static const std::vector<ActionDecl> decls = [] {
    std::vector<ActionDecl> out;
    std::string err;
    const Value v = json::parse(builtin_json("default"), err);
    if (!err.empty() || !v.is_object()) return out;
    const Value& acts = v.get("actions");
    if (!acts.is_object()) return out;
    for (const auto& [name, desc] : acts.obj)
      if (desc.is_string()) out.push_back({name, desc.str});
    return out;
  }();
  return decls;
}

// The parsed built-in layouts. The biggest thing the library retains process-wide, and the
// reason `shutdown()` has real work to do today rather than only in principle: it is embedded
// JSON turned into Layout objects on first use and kept forever. Releasing it is safe at any
// moment — the next call rebuilds it (m6a).
std::vector<std::pair<std::string, Layout>> build_builtin_layouts() {
  {
    std::vector<std::pair<std::string, Layout>> out;
    for (std::string_view n : builtin_names()) {
      LayoutLoadReport rep;
      std::optional<Layout> l = load_layout(builtin_json(n), rep);
      if (!l || !rep.clean()) {
        // A built-in that does not load cleanly is a programming error; say so loudly
        // rather than serve a half-layout. (The test asserts clean() for each.)
        std::fprintf(stderr, "rolltui: built-in layout '%.*s' is broken: %s\n", static_cast<int>(n.size()), n.data(),
                     rep.error.empty() ? (rep.bad_values.empty() ? rep.unknown_keys[0].c_str() : rep.bad_values[0].c_str())
                                       : rep.error.c_str());
        std::abort();
      }
      out.emplace_back(std::string(n), std::move(*l));
    }
    return out;
  }
}

std::vector<std::pair<std::string, Layout>>& layout_cache_storage() {
  static std::vector<std::pair<std::string, Layout>> cache;
  return cache;
}

std::vector<std::pair<std::string, Layout>>& builtin_layout_cache() {
  // THE RELEASER TOUCHES THE STORAGE, NEVER THIS FUNCTION, and it is RE-REGISTERED ON EVERY
  // REBUILD. Two defects in one line, both from Phase 14 m6a and both invisible here
  // because a `std::vector<Layout>` reaches the global `operator new` and the gauge does
  // not count it; Theme.cpp's identical cache turned each of them into a failing assertion
  // the moment a theme owned a C effect map.
  //   - `builtin_layout_cache().clear(); builtin_layout_cache().shrink_to_fit();` — the
  //     second call finds the cache it just emptied and REBUILDS it, so shutdown ends
  //     holding what it meant to release.
  //   - `static const bool once = (on_shutdown(...), true)` registers once per PROCESS,
  //     while `shutdown()` clears its own registry as it runs — so a second shutdown
  //     releases nothing. Registering at FILL time is the rule `ThreadHandle` already
  //     states for a per-thread buffer.
  std::vector<std::pair<std::string, Layout>>& cache = layout_cache_storage();
  if (cache.empty())
    on_shutdown([] {
      layout_cache_storage().clear();
      layout_cache_storage().shrink_to_fit();
    });
  // FILLED WHEN EMPTY, not by a static initializer — because a `static x = f();` runs ONCE
  // and `shutdown()` clearing it would leave `builtin_layout()` answering nullptr forever
  // after. That was a live defect for about ten minutes, and it is exactly what "safe to call
  // at any time" has to mean: releasing a cache is only safe if the cache rebuilds.
  if (cache.empty()) cache = build_builtin_layouts();
  return cache;
}

const Layout* builtin_layout(std::string_view name) {
  for (const auto& [n, l] : builtin_layout_cache())
    if (n == name) return &l;
  return nullptr;
}

std::vector<std::string_view> builtin_layout_names() { return builtin_names(); }


// ---- the split, the drawing and the stack: over the boundary ---------------------------------

namespace {

// THE DRAW SCRATCH, owned per thread by the shim (rolltui/c/rolltui_frame_ops.h). One owner,
// named, released at thread exit and at `release_thread()` — `ThreadHandle` is the shape
// Phase 15 m2 extracted after `Unicode.cpp` hand-wrote it once.
RolltuiDrawScratch* draw_scratch() {
  static thread_local ThreadHandle<RolltuiDrawScratch, rolltui_draw_scratch_new, rolltui_draw_scratch_free> h;
  return h.get();
}

// …and the compose scratch, the same way. Two handles rather than one, because they are two
// ROLES: the arm maps are a frame's worth of bytes and the cluster array is a string's.
RolltuiComposeScratch* compose_scratch() {
  static thread_local ThreadHandle<RolltuiComposeScratch, rolltui_compose_scratch_new,
                                   rolltui_compose_scratch_free>
      h;
  return h.get();
}

// THE THREE ROLES A COMPOSE NEEDS, handed over as bytes. `rolltui/Style.hpp` is the one place
// these names exist; the C is told which byte to draw with, exactly as the markdown renderer
// and the diff colouriser are (Phase 15 m2's rule).
constexpr RolltuiLayoutRoles kRoles = {
    /*border=*/static_cast<unsigned char>(Role::border),
    /*border_active=*/static_cast<unsigned char>(Role::border_active),
    /*title=*/static_cast<unsigned char>(Role::title),
    /*overlay=*/static_cast<unsigned char>(Role::overlay),
};

// THE THREE STACK ACTIONS, likewise: the C knows the RULES and none of the words.
constexpr RolltuiStackActions kStackActions = {"stack.close_popup", "stack.focus_next", "stack.focus_prev"};

void push_node(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<ResolvedNode>*>(ctx)->push_back(*rn);
}

// What crosses instead of a `std::function`: the host's callable behind a `void*`.
void call_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame*) {
  auto* p = static_cast<std::pair<const SlotRenderer*, Frame*>*>(ctx);
  (*p->first)(*rn, *p->second);
}

}  // namespace

void resolve_tree_into(const Node& root, Rect box, Rect screen, std::size_t layer,
                       std::vector<ResolvedNode>& out) {
  out.clear();
  rolltui_resolve_tree(&root, box, screen, layer, push_node, &out);
}

std::vector<ResolvedNode> resolve_tree(const Node& root, Rect box, Rect screen, std::size_t layer) {
  std::vector<ResolvedNode> out;
  resolve_tree_into(root, box, screen, layer, out);
  return out;
}

void draw_border(Frame& frame, Rect outer, Border b, const Style& line, std::string_view title,
                 const Style& title_style, bool ambiguous_wide) {
  rolltui_draw_border(frame.handle(), draw_scratch(), outer, static_cast<unsigned char>(b), line, title.data(),
                      title.size(), title_style, ambiguous_wide);
}

void compose_layer(Frame& frame, const std::vector<ResolvedNode>& nodes, const Theme& theme,
                   const SlotRenderer& render, bool ambiguous_wide) {
  std::pair<const SlotRenderer*, Frame*> ctx{&render, &frame};
  rolltui_compose_layer(frame.handle(), nodes.data(), nodes.size(), theme.styles.data(), &kRoles,
                        render ? call_slot : nullptr, &ctx, ambiguous_wide, compose_scratch());
}

// ---- the stack -------------------------------------------------------------------------------

WindowStack::WindowStack() = default;

WindowStack::WindowStack(const Layout& layout) { set_base(layout.base); }

void WindowStack::set_base(const Layer& base) { rolltui_window_stack_set_base(s_.get(), &base); }

void WindowStack::push(Layer popup) { rolltui_window_stack_push(s_.get(), &popup); }

bool WindowStack::pop() { return rolltui_window_stack_pop(s_.get()) != 0; }

bool WindowStack::has_popup(std::string_view id) const {
  return rolltui_window_stack_has_popup(s_.get(), id.data(), id.size()) != 0;
}

Node* WindowStack::find(std::string_view id) { return rolltui_window_stack_find(s_.get(), id.data(), id.size()); }

const Node* WindowStack::find(std::string_view id) const {
  return rolltui_window_stack_find(s_.get(), id.data(), id.size());
}

std::size_t WindowStack::focus_layer() const { return rolltui_window_stack_focus_layer(s_.get()); }

const Node* WindowStack::focused() const { return rolltui_window_stack_focused(s_.get()); }

void WindowStack::focus(std::string_view id) { rolltui_window_stack_focus(s_.get(), id.data(), id.size()); }

void WindowStack::cycle_focus(bool backwards) { rolltui_window_stack_cycle_focus(s_.get(), backwards); }

void WindowStack::resolve_into(Rect screen, std::vector<ResolvedNode>& out) const {
  out.clear();
  rolltui_window_stack_resolve(s_.get(), screen, push_node, &out);
}

std::vector<ResolvedNode> WindowStack::resolve(Rect screen) const {
  std::vector<ResolvedNode> out;
  resolve_into(screen, out);
  return out;
}

void WindowStack::compose(Frame& frame, Rect screen, const Theme& theme, const SlotRenderer& render,
                          bool ambiguous_wide) const {
  std::pair<const SlotRenderer*, Frame*> ctx{&render, &frame};
  rolltui_window_stack_compose(s_.get(), frame.handle(), screen, theme.styles.data(), &kRoles,
                               render ? call_slot : nullptr, &ctx, ambiguous_wide, compose_scratch());
}

Route WindowStack::route(const Event& e, Rect screen, const Bindings& bindings) {
  RolltuiEvent ev{};
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_KEY;
    ev.key = chord_of(*k);
  } else if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_MOUSE;
    ev.mouse = *m;
  } else {
    ev.kind = ROLLTUI_EVENT_PASTE;
  }
  Str window;
  const unsigned char kind =
      rolltui_window_stack_route(s_.get(), &ev, screen, bindings.handle(), &kStackActions, &window);
  return {static_cast<Route::Kind>(kind), window.str()};
}

std::string_view WindowStack::captured() const {
  std::size_t n = 0;
  const char* p = rolltui_window_stack_captured(s_.get(), &n);
  return std::string_view(p, n);
}

}  // namespace rolltui
