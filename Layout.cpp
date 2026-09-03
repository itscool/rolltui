// rolltui/Layout.cpp — see Layout.hpp.
#include "rolltui/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Json.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

// ---- placement ---------------------------------------------------------------------------

int resolve_dim(Dim d, int extent) {
  return static_cast<int>(std::floor(d.fraction * extent + 1e-6)) + d.cells;
}

namespace {

enum class Align { Start, Center, End };

Align align_h(Anchor a) {
  switch (a) {
    case Anchor::TopLeft: case Anchor::Left: case Anchor::BottomLeft: return Align::Start;
    case Anchor::Top: case Anchor::Center: case Anchor::Bottom: return Align::Center;
    default: return Align::End;
  }
}
Align align_v(Anchor a) {
  switch (a) {
    case Anchor::TopLeft: case Anchor::Top: case Anchor::TopRight: return Align::Start;
    case Anchor::Left: case Anchor::Center: case Anchor::Right: return Align::Center;
    default: return Align::End;
  }
}

// One axis of resolve(): returns {start, size} relative to the parent.
std::pair<int, int> resolve_axis(Dim pos, Dim size, Align align, const std::optional<Dim>& min,
                                 const std::optional<Dim>& max, bool clamp, int extent) {
  int start, len;
  if (align == Align::Start) {
    start = resolve_dim(pos, extent);
    len = resolve_dim(pos + size, extent) - start;
  } else {
    len = resolve_dim(size, extent);
    int point = resolve_dim(pos, extent);
    start = (align == Align::Center) ? point - len / 2 : point - len;
  }
  if (len < 0) len = 0;
  if (max) len = std::min(len, std::max(resolve_dim(*max, extent), 0));
  if (min) len = std::max(len, resolve_dim(*min, extent));
  if (clamp) {
    len = std::min(len, std::max(extent, 0));
    start = std::clamp(start, 0, std::max(extent - len, 0));
  }
  return {start, len};
}

}  // namespace

Rect resolve(const Placement& p, Rect parent) {
  auto [x, w] = resolve_axis(p.x, p.w, align_h(p.anchor), p.min_w, p.max_w, p.clamp, parent.w);
  auto [y, h] = resolve_axis(p.y, p.h, align_v(p.anchor), p.min_h, p.max_h, p.clamp, parent.h);
  return {parent.x + x, parent.y + y, w, h};
}

// ---- content: the widget kind and its source ----------------------------------------------

namespace {

struct KindRow {
  WidgetKind kind;
  const char* name;
  SourceRule rule;
  const char* source_is;  // what the source names, for a report
};

// THE TABLE (Layout.hpp's header comment is its documentation). One definition site:
// the names, the source rule and what a source means all come from here.
constexpr KindRow kKinds[] = {
    {WidgetKind::Transcript, "transcript", SourceRule::Required, "a document the host binds"},
    {WidgetKind::Input, "input", SourceRule::Required, "the target a submitted line goes to"},
    {WidgetKind::Menu, "menu", SourceRule::Required, "a menu file"},
    {WidgetKind::Rows, "rows", SourceRule::Required, "a row source the host binds"},
    {WidgetKind::Text, "text", SourceRule::Optional, "the literal text"},
    {WidgetKind::File, "file", SourceRule::Required, "a path"},
    {WidgetKind::Help, "help", SourceRule::Forbidden, ""},
    // `custom` left this table in Phase 11 m3: a host's own window is a REGISTERED KIND
    // now (rung 2), so there is one mechanism instead of a kind that meant "ask the host".
};

// Phase 9's slot names and Phase 10's `custom:` contents → their m3 form. A closed,
// one-way table: the loader rewrites and reports, so the next save is in the new form
// and this table stops being reached.
//
// The five composites are the interesting rows, and they are why this table is a MAP
// rather than a rule. Phase 9 called roll's approval modal `approval`; Phase 10 m2 made
// it `custom:approval`; Phase 11 m3 makes it `approval` again — the same name, now a
// registered KIND rather than a bare slot. So the Phase 9 rows for those five are simply
// gone (their old spelling is valid again, and migrating a valid name would be a
// rewrite loop), and the Phase 10 spelling is what migrates.
constexpr std::pair<const char*, const char*> kLegacy[] = {
    {"transcript", "transcript:session"},  {"input", "input:prompt"},        {"status", "rows:status"},
    {"menu", "menu:main"},                 {"custom:approval", "approval"},  {"custom:details", "details"},
    {"custom:editor", "editor"},           {"custom:confirm", "confirm"},    {"custom:report", "report"},
};

const KindRow* row_of_or_null(WidgetKind k) {
  for (const KindRow& r : kKinds)
    if (r.kind == k) return &r;
  return nullptr;  // WidgetKind::Registered, which is not in the library's table by design
}

const KindRow& row_of(WidgetKind k) {
  const KindRow* r = row_of_or_null(k);
  return r ? *r : kKinds[0];
}

// RUNG 2. Registered by name, never constexpr, and searched only after kKinds — see
// Layout.hpp's stated resolution order. Process-wide because a kind is a program's
// vocabulary, not one screen's: a host registers once at startup and every `Windows` in
// the process parses the same layout files the same way.
struct HostKind {
  std::string name;
  SourceRule rule;
  std::string source_is;
};
std::vector<HostKind>& host_kinds() {
  static std::vector<HostKind> v;
  return v;
}
const HostKind* host_kind(std::string_view name) {
  for (const HostKind& h : host_kinds())
    if (h.name == name) return &h;
  return nullptr;
}

}  // namespace

std::string_view widget_kind_name(WidgetKind k) {
  const KindRow* r = row_of_or_null(k);
  return r ? std::string_view(r->name) : std::string_view();
}
std::string_view content_kind_name(const Content& c) {
  return c.kind == WidgetKind::Registered ? std::string_view(c.registered_name) : widget_kind_name(c.kind);
}

std::optional<WidgetKind> widget_kind_from_name(std::string_view name) {
  for (const KindRow& r : kKinds)
    if (name == r.name) return r.kind;
  return std::nullopt;
}

SourceRule source_rule(WidgetKind k) { return row_of(k).rule; }
SourceRule content_source_rule(const Content& c) {
  if (c.kind != WidgetKind::Registered) return source_rule(c.kind);
  const HostKind* h = host_kind(c.registered_name);
  return h ? h->rule : SourceRule::Required;
}
std::string_view source_describes(WidgetKind k) { return row_of(k).source_is; }

// Rung 1 is checked FIRST and the refusal says so by name: the library's own kinds may
// never be shadowed, and this is one of the two independent guards (the other is that
// parse_content searches kKinds before host_kinds, so a shadowing row could not be
// reached even if one existed).
bool register_widget_kind(std::string name, SourceRule rule, std::string source_is, std::string* why) {
  auto fail = [&](std::string reason) {
    if (why) *why = std::move(reason);
    return false;
  };
  if (name.empty()) return fail("a widget kind needs a name");
  if (name.find(':') != std::string::npos) return fail("'" + name + "' is not a kind name: a ':' separates the kind from its source");
  if (widget_kind_from_name(name)) return fail("'" + name + "' is one of the library's own kinds and cannot be registered over");
  if (const HostKind* h = host_kind(name)) {
    if (h->rule == rule) return true;  // the same registration twice: idempotent, not an error
    return fail("'" + name + "' is already registered with a different source rule");
  }
  host_kinds().push_back({std::move(name), rule, std::move(source_is)});
  return true;
}

void clear_registered_widget_kinds() { host_kinds().clear(); }

std::vector<std::string> widget_kind_names() {
  std::vector<std::string> out;
  for (const KindRow& r : kKinds) out.emplace_back(r.name);
  for (const HostKind& h : host_kinds()) out.push_back(h.name);
  return out;
}

const std::vector<WidgetKind>& widget_kinds() {
  static const std::vector<WidgetKind> all = [] {
    std::vector<WidgetKind> v;
    for (const KindRow& r : kKinds) v.push_back(r.kind);
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
  SourceRule rule;
  std::string source_is;
  // THE RESOLUTION ORDER (Layout.hpp). Rung 1 is the library's closed table and is
  // searched first, unconditionally — that is the guard that survives even if a
  // shadowing registration somehow existed. Rung 2 is what the host registered. Rung 3
  // is this failing with a named reason.
  if (std::optional<WidgetKind> kind = widget_kind_from_name(name)) {
    const KindRow& r = row_of(*kind);
    c.kind = *kind;
    rule = r.rule;
    source_is = r.source_is;
  } else if (const HostKind* h = host_kind(name)) {
    c.kind = WidgetKind::Registered;
    rule = h->rule;
    source_is = h->source_is;
  } else {
    std::string known;
    for (const std::string& n : widget_kind_names()) known += (known.empty() ? "" : " | ") + n;
    if (std::optional<std::string> m = migrated_content(text))
      return fail(ContentProblem::UnknownKind, "'" + std::string(text) + "' is an older spelling, not a widget kind; write '" + *m + "'");
    return fail(ContentProblem::UnknownKind, "'" + std::string(name) + "' is not a widget kind (" + known + ")");
  }
  if (c.kind == WidgetKind::Registered) c.registered_name = std::string(name);
  const std::string kname(name);
  if (colon != std::string_view::npos) c.source = std::string(text.substr(colon + 1));
  if (rule == SourceRule::Forbidden && colon != std::string_view::npos)
    return fail(ContentProblem::ForbiddenSource, "'" + kname + "' takes no source; write '" + kname + "'");
  if (rule == SourceRule::Required && c.source.empty()) {
    if (std::optional<std::string> m = migrated_content(text))  // an older name that is also a kind name
      return fail(ContentProblem::MissingSource, "'" + std::string(text) + "' is an older spelling, not a content; write '" + *m + "'");
    return fail(ContentProblem::MissingSource, "'" + kname + "' needs a source (" + source_is + "): write '" + kname + ":<name>'");
  }
  return c;
}

std::string content_to_string(const Content& c) {
  std::string s(content_kind_name(c));
  if (content_source_rule(c) != SourceRule::Forbidden) s += ":" + c.source;
  return s;
}

std::optional<std::string> migrated_content(std::string_view legacy) {
  for (const auto& [from, to] : kLegacy)
    if (legacy == from) return std::string(to);
  return std::nullopt;
}

// ---- tree basics -------------------------------------------------------------------------

Node Node::window(std::string content, SplitSize size) {
  Node n;
  n.kind = Kind::Window;
  n.id = content;
  n.content = std::move(content);
  n.size = size;
  return n;
}
Node Node::window_id(std::string id, std::string content, SplitSize size) {
  Node n = window(std::move(content), size);
  n.id = std::move(id);
  return n;
}
Node Node::row(std::vector<Node> children, SplitSize size) {
  Node n;
  n.kind = Kind::Row;
  n.children = std::move(children);
  n.size = size;
  return n;
}
Node Node::column(std::vector<Node> children, SplitSize size) {
  Node n;
  n.kind = Kind::Column;
  n.children = std::move(children);
  n.size = size;
  return n;
}

const Layer* Layout::popup(std::string_view id) const {
  for (const Layer& l : popups)
    if (l.id == id) return &l;
  return nullptr;
}

Rect inner_rect(Rect outer, Border b) {
  if (b == Border::None) return outer;
  Rect r{outer.x + 1, outer.y + 1, outer.w - 2, outer.h - 2};
  if (r.w < 0) r.w = 0;
  if (r.h < 0) r.h = 0;
  return r;
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

std::optional<Dim> parse_dim(std::string_view text) {
  text = trim(text);
  std::size_t pct = text.find('%');
  if (pct == std::string_view::npos) return std::nullopt;
  std::string_view num = trim(text.substr(0, pct));
  if (num.empty()) return std::nullopt;
  // The percentage: an integer or a decimal, optionally signed.
  char* end = nullptr;
  std::string tmp(num);
  double f = std::strtod(tmp.c_str(), &end);
  if (end != tmp.c_str() + tmp.size()) return std::nullopt;
  for (char c : tmp)
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+')) return std::nullopt;
  std::string_view rest = trim(text.substr(pct + 1));
  int cells = 0;
  if (!rest.empty()) {
    if (rest[0] != '+' && rest[0] != '-') return std::nullopt;
    int mag;
    if (!parse_int(rest.substr(1), mag) || mag < 0) return std::nullopt;
    cells = rest[0] == '-' ? -mag : mag;
  }
  return Dim::rel(f / 100.0, cells);
}

std::string dim_to_string(Dim d) {
  if (d.fraction == 0) return std::to_string(d.cells);
  char buf[64];
  double pct = d.fraction * 100.0;
  if (std::fabs(pct - std::round(pct)) < 1e-9) std::snprintf(buf, sizeof buf, "%d%%", static_cast<int>(std::round(pct)));
  else std::snprintf(buf, sizeof buf, "%g%%", pct);
  std::string s = buf;
  if (d.cells > 0) s += " + " + std::to_string(d.cells);
  else if (d.cells < 0) s += " - " + std::to_string(-d.cells);
  return s;
}

std::optional<SplitSize> parse_split_size(std::string_view text) {
  text = trim(text);
  if (text == "fill") return SplitSize::filling(1);
  if (text.rfind("fill", 0) == 0) {
    int w;
    if (parse_int(text.substr(4), w) && w >= 1) return SplitSize::filling(w);
    return std::nullopt;
  }
  if (auto d = parse_dim(text)) return SplitSize::fixed(*d);
  return std::nullopt;
}

std::optional<SplitSize> parse_size_text(std::string_view text) {
  if (std::optional<SplitSize> s = parse_split_size(text)) return s;
  if (text.empty() || text.size() > 9) return std::nullopt;
  for (char c : text)
    if (c < '0' || c > '9') return std::nullopt;
  return SplitSize::fixed(Dim::abs(std::atoi(std::string(text).c_str())));
}

std::string split_size_to_string(SplitSize s) {
  if (s.fill) return s.weight == 1 ? "fill" : "fill " + std::to_string(s.weight);
  return dim_to_string(s.dim);
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

void collect_ids(const Node& n, std::vector<std::string>& seen, const std::string& where, LayoutLoadReport& rep) {
  if (!n.id.empty()) {
    if (std::find(seen.begin(), seen.end(), n.id) != seen.end()) rep.bad_values.push_back(where + ".id: duplicate id '" + n.id + "'");
    else seen.push_back(n.id);
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
  if (!l.focus.empty() && std::find(ids.begin(), ids.end(), l.focus) == ids.end())
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
    if (n.id != n.content) o.set("id", Value::string(n.id));
    o.set("content", Value::string(n.content));
  } else if (!n.id.empty()) {
    o.set("id", Value::string(n.id));
  }
  if (n.border != Border::None) o.set("border", Value::string(std::string(border_name(n.border))));
  if (!n.title.empty()) o.set("title", Value::string(n.title));
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
    o.set("id", Value::string(l.id));
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
  if (!l.focus.empty()) o.set("focus", Value::string(l.focus));
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
        else if (out.popup(p.id)) report.bad_values.push_back(where + ".id: duplicate popup id '" + p.id + "'");
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

const Layout* builtin_layout(std::string_view name) {
  static std::vector<std::pair<std::string, Layout>> cache = [] {
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
  }();
  for (const auto& [n, l] : cache)
    if (n == name) return &l;
  return nullptr;
}

std::vector<std::string_view> builtin_layout_names() { return builtin_names(); }

// ---- the split -----------------------------------------------------------------------------

namespace {

enum class Side { Left, Right, Top, Bottom };

bool edge_bordered(const Node& n, Side side) {
  if (n.border != Border::None) return true;
  if (n.is_window()) return false;
  std::vector<const Node*> vis;
  for (const Node& c : n.children)
    if (c.visible) vis.push_back(&c);
  if (vis.empty()) return false;
  const bool along = (n.kind == Node::Kind::Row) ? (side == Side::Left || side == Side::Right)
                                                 : (side == Side::Top || side == Side::Bottom);
  if (along) {
    const Node* edge = (side == Side::Left || side == Side::Top) ? vis.front() : vis.back();
    return edge_bordered(*edge, side);
  }
  for (const Node* c : vis)
    if (!edge_bordered(*c, side)) return false;
  return true;
}

void place(const Node& n, Rect box, Rect screen, std::size_t layer, std::vector<ResolvedNode>& out) {
  ResolvedNode rn;
  rn.node = &n;
  rn.outer = box;
  rn.inner = inner_rect(box, n.border).intersect(screen);
  rn.layer = layer;
  out.push_back(rn);
  if (n.is_window()) return;

  const bool row = n.kind == Node::Kind::Row;
  std::vector<const Node*> vis;
  for (const Node& c : n.children)
    if (c.visible) vis.push_back(&c);
  if (vis.empty()) return;
  const Rect in = inner_rect(box, n.border);  // unclipped: children resolve against the true inner box
  const int extent = row ? in.w : in.h;

  // Shared edges between adjacent bordered siblings.
  std::vector<bool> shared(vis.size(), false);
  int shared_count = 0;
  for (std::size_t i = 0; i + 1 < vis.size(); ++i) {
    shared[i] = row ? (edge_bordered(*vis[i], Side::Right) && edge_bordered(*vis[i + 1], Side::Left))
                    : (edge_bordered(*vis[i], Side::Bottom) && edge_bordered(*vis[i + 1], Side::Top));
    if (shared[i]) ++shared_count;
  }
  const int ext = std::max(extent, 0) + shared_count;

  // Fixed children: edges of the cumulative Dim sum.
  std::vector<int> size(vis.size(), 0);
  Dim cum;
  int prev_edge = 0, fixed_total = 0, weight_total = 0;
  for (std::size_t i = 0; i < vis.size(); ++i) {
    if (vis[i]->size.fill) { weight_total += std::max(vis[i]->size.weight, 1); continue; }
    cum = cum + vis[i]->size.dim;
    int edge = resolve_dim(cum, ext);
    size[i] = std::max(edge - prev_edge, 0);
    prev_edge = std::max(edge, prev_edge);
    fixed_total += size[i];
  }
  // Fills: cumulative weight edges over the remainder.
  const int remainder = std::max(ext - fixed_total, 0);
  int cum_w = 0, prev_fill_edge = 0;
  for (std::size_t i = 0; i < vis.size(); ++i) {
    if (!vis[i]->size.fill) continue;
    cum_w += std::max(vis[i]->size.weight, 1);
    int edge = resolve_dim(Dim::rel(static_cast<double>(cum_w) / weight_total), remainder);
    size[i] = edge - prev_fill_edge;
    prev_fill_edge = edge;
  }
  // Positions, sharing one cell per shared edge; clip to the extent in order.
  int pos = 0;
  for (std::size_t i = 0; i < vis.size(); ++i) {
    if (pos + size[i] > std::max(extent, 0)) size[i] = std::max(std::max(extent, 0) - pos, 0);
    Rect r = row ? Rect{in.x + pos, in.y, size[i], in.h} : Rect{in.x, in.y + pos, in.w, size[i]};
    place(*vis[i], r, screen, layer, out);
    pos += size[i];
    if (i + 1 < vis.size() && shared[i] && size[i] > 0) pos -= 1;
  }
}

}  // namespace

std::vector<ResolvedNode> resolve_tree(const Node& root, Rect box, Rect screen, std::size_t layer) {
  std::vector<ResolvedNode> out;
  if (root.visible) place(root, box, screen, layer, out);
  return out;
}

// ---- drawing --------------------------------------------------------------------------------

namespace {

// Box-drawing arms: U D L R.
constexpr std::uint8_t U = 1, D = 2, L = 4, R = 8;

// Light glyph for an arm mask (index = mask); "" for 0.
constexpr const char* kLight[16] = {
    "",       "╵", "╷", "│",  // -, U, D, UD
    "╴", "┘", "┐", "┤",  // L, UL, DL, UDL
    "╶", "└", "┌", "├",  // R, UR, DR, UDR
    "─", "┴", "┬", "┼",  // LR, ULR, DLR, UDLR
};

// Box-drawing glyphs are East Asian AMBIGUOUS width (U+2500-257F): a terminal that
// renders ambiguous characters wide draws every border two cells wide and the whole
// frame garbles. So under ambiguous_wide every border set falls back to ASCII
// (+ - |), the way vim's `ambiwidth=double` does — one cell everywhere, guaranteed.
std::string glyph_for(Border b, std::uint8_t mask, bool ascii) {
  if (mask == 0) return "";
  if (ascii) {
    if (mask == (L | R) || mask == L || mask == R) return "-";
    if (mask == (U | D) || mask == U || mask == D) return "|";
    return "+";
  }
  if (b == Border::Rounded) {
    switch (mask) {
      case D | R: return "╭";
      case D | L: return "╮";
      case U | R: return "╰";
      case U | L: return "╯";
      default: break;
    }
  }
  if (b == Border::Double) {
    switch (mask) {
      case L | R: return "═";
      case U | D: return "║";
      case D | R: return "╔";
      case D | L: return "╗";
      case U | R: return "╚";
      case U | L: return "╝";
      default: return kLight[mask];  // a joined double border is not modelled; light junctions
    }
  }
  if (b == Border::Heavy) {
    switch (mask) {
      case L | R: return "━";
      case U | D: return "┃";
      case D | R: return "┏";
      case D | L: return "┓";
      case U | R: return "┗";
      case U | L: return "┛";
      default: return kLight[mask];
    }
  }
  return kLight[mask];
}

bool joins(Border b) { return b == Border::Single || b == Border::Rounded; }

// The arm mask this window's own border wants at ring cell (x, y) of `outer`.
std::uint8_t own_mask(Rect o, int x, int y) {
  const bool left = x == o.x, right = x == o.x + o.w - 1, top = y == o.y, bottom = y == o.y + o.h - 1;
  if (o.w == 1 && o.h == 1) return 0;
  if (o.w == 1) return (top ? 0 : U) | (bottom ? 0 : D);
  if (o.h == 1) return (left ? 0 : L) | (right ? 0 : R);
  if (top && left) return D | R;
  if (top && right) return D | L;
  if (bottom && left) return U | R;
  if (bottom && right) return U | L;
  if (top || bottom) return L | R;
  return U | D;
}

// Draws the border of `outer` clipped to the frame; `map` (W×H arm masks, or null)
// is the layer's join map: the previous masks on the ring are OR'd in and the result
// written back.
void draw_border_impl(Frame& frame, Rect outer, Border b, const Style& line, std::string_view title,
                      const Style& title_style, bool ambiguous_wide, std::vector<std::uint8_t>* map,
                      const std::vector<std::uint8_t>* ring_before) {
  if (b == Border::None || outer.w <= 0 || outer.h <= 0) return;
  const Rect clip = outer.intersect(frame.bounds());
  if (clip.empty()) return;
  auto map_at = [&](int x, int y) -> std::uint8_t& { return (*map)[static_cast<std::size_t>(y * frame.width() + x)]; };
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      const bool ring = x == outer.x || x == outer.x + outer.w - 1 || y == outer.y || y == outer.y + outer.h - 1;
      if (!ring) continue;
      std::uint8_t m = own_mask(outer, x, y);
      if (map && ring_before && joins(b)) m |= (*ring_before)[static_cast<std::size_t>(y * frame.width() + x)];
      std::string g = glyph_for(b, m, ambiguous_wide);
      if (g.empty()) g = " ";
      frame.put(x, y, g, 1, line);
      if (map) map_at(x, y) = joins(b) ? m : 0;
    }
  }
  // Title on the top edge, inside the corners.
  if (!title.empty() && outer.w >= 5 && outer.y >= 0 && outer.y < frame.height()) {
    std::string t = " " + std::string(title) + " ";
    int avail = outer.w - 2;
    int used = frame.put_text(outer.x + 1, outer.y, t, title_style, avail, ambiguous_wide);
    if (map)
      for (int x = outer.x + 1; x < outer.x + 1 + used && x < frame.width(); ++x)
        if (x >= 0) map_at(x, outer.y) = 0;
  }
}

}  // namespace

void draw_border(Frame& frame, Rect outer, Border b, const Style& line, std::string_view title,
                 const Style& title_style, bool ambiguous_wide) {
  draw_border_impl(frame, outer, b, line, title, title_style, ambiguous_wide, nullptr, nullptr);
}

void compose_layer(Frame& frame, const std::vector<ResolvedNode>& nodes, const Theme& theme,
                   const SlotRenderer& render, bool ambiguous_wide) {
  std::vector<std::uint8_t> map(static_cast<std::size_t>(frame.width() * frame.height()), 0);
  std::vector<std::uint8_t> before(map.size(), 0);
  for (const ResolvedNode& rn : nodes) {
    const Node& n = *rn.node;
    const bool draws = n.is_window() || n.border != Border::None;
    if (!draws) continue;
    const Style ground = theme.style(n.background);
    // Remember the join map under this window's ring, then clear the outer rect.
    const Rect clip = rn.outer.intersect(frame.bounds());
    for (int y = clip.y; y < clip.y + clip.h; ++y)
      for (int x = clip.x; x < clip.x + clip.w; ++x) {
        std::size_t i = static_cast<std::size_t>(y * frame.width() + x);
        before[i] = map[i];
        map[i] = 0;
      }
    frame.fill(rn.outer, ground);
    Style line = theme.style(rn.focused ? Role::border_active : Role::border);
    line.bg = ground.bg;
    Style title = theme.style(Role::title);
    title.bg = ground.bg;
    draw_border_impl(frame, rn.outer, n.border, line, n.title, title, ambiguous_wide, &map, &before);
    if (n.is_window() && !rn.inner.empty() && render) render(rn, frame);
  }
}

// ---- the stack -------------------------------------------------------------------------------

namespace {

const Node* find_in(const Node& n, std::string_view id) {
  if (n.id == id && !id.empty()) return &n;
  for (const Node& c : n.children)
    if (const Node* f = find_in(c, id)) return f;
  return nullptr;
}
Node* find_in(Node& n, std::string_view id) { return const_cast<Node*>(find_in(static_cast<const Node&>(n), id)); }

void focusables(const Node& n, std::vector<const Node*>& out) {
  if (!n.visible) return;
  if (n.is_window()) { if (n.focusable) out.push_back(&n); return; }
  for (const Node& c : n.children) focusables(c, out);
}

const Node* layer_focused(const Layer& l) {
  std::vector<const Node*> f;
  focusables(l.root, f);
  if (f.empty()) return nullptr;
  if (!l.focus.empty())
    for (const Node* n : f)
      if (n->id == l.focus) return n;
  return f.front();
}

}  // namespace

WindowStack::WindowStack(const Layout& layout) : layers_{layout.base} {}

void WindowStack::set_base(const Layer& base) {
  std::string keep = layers_.front().focus;
  layers_.front() = base;
  if (base.focus.empty() && !keep.empty() && find_in(layers_.front().root, keep)) layers_.front().focus = keep;
}

void WindowStack::push(Layer popup) { layers_.push_back(std::move(popup)); }

bool WindowStack::pop() {
  if (layers_.size() <= 1) return false;
  layers_.pop_back();
  return true;
}

bool WindowStack::has_popup(std::string_view id) const {
  for (std::size_t i = 1; i < layers_.size(); ++i)
    if (layers_[i].id == id) return true;
  return false;
}

Node* WindowStack::find(std::string_view id) {
  for (Layer& l : layers_)
    if (Node* n = find_in(l.root, id)) return n;
  return nullptr;
}
const Node* WindowStack::find(std::string_view id) const {
  for (const Layer& l : layers_)
    if (const Node* n = find_in(l.root, id)) return n;
  return nullptr;
}

std::size_t WindowStack::focus_layer() const {
  const std::size_t top = layers_.size() - 1;
  if (layers_[top].modal) return top;
  for (std::size_t i = layers_.size(); i-- > 0;)
    if (layer_focused(layers_[i])) return i;
  return top;
}

const Node* WindowStack::focused() const { return layer_focused(layers_[focus_layer()]); }

void WindowStack::focus(std::string_view id) {
  Layer& l = layers_[focus_layer()];
  std::vector<const Node*> f;
  focusables(l.root, f);
  for (const Node* n : f)
    if (n->id == id) { l.focus = std::string(id); return; }
}

void WindowStack::cycle_focus(bool backwards) {
  Layer& l = layers_[focus_layer()];
  std::vector<const Node*> f;
  focusables(l.root, f);
  if (f.empty()) return;
  const Node* cur = layer_focused(l);
  std::size_t i = 0;
  for (; i < f.size(); ++i)
    if (f[i] == cur) break;
  if (i >= f.size()) i = 0;
  i = backwards ? (i + f.size() - 1) % f.size() : (i + 1) % f.size();
  l.focus = f[i]->id;
}

std::vector<ResolvedNode> WindowStack::resolve(Rect screen) const {
  std::vector<ResolvedNode> out;
  const Node* fnode = focused();
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    Rect box = rolltui::resolve(layers_[i].placement, screen);
    for (ResolvedNode& rn : resolve_tree(layers_[i].root, box, screen, i)) {
      rn.focused = (rn.node == fnode);
      out.push_back(rn);
    }
  }
  return out;
}

void WindowStack::compose(Frame& frame, Rect screen, const Theme& theme, const SlotRenderer& render,
                          bool ambiguous_wide) const {
  std::vector<ResolvedNode> all = resolve(screen);
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (layers_[i].modal) frame.tint(screen, theme.style(Role::overlay));
    std::vector<ResolvedNode> mine;
    for (const ResolvedNode& rn : all)
      if (rn.layer == i) mine.push_back(rn);
    compose_layer(frame, mine, theme, render, ambiguous_wide);
  }
}

Route WindowStack::route(const Event& e, Rect screen, const Bindings& bindings) {
  if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    // A captured pointer: drags and the release go to the pressed window, wherever
    // the pointer is now (the window may even have gone: then the capture just ends).
    if (!captured_.empty() && (m->kind == MouseEvent::Kind::Drag || m->kind == MouseEvent::Kind::Release)) {
      std::string target = captured_;
      if (m->kind == MouseEvent::Kind::Release) captured_.clear();
      if (find(target)) return {Route::Kind::Deliver, target};
      return {Route::Kind::Dropped, {}};
    }
    std::vector<ResolvedNode> all = resolve(screen);
    const std::size_t top = layers_.size() - 1;
    for (std::size_t k = all.size(); k-- > 0;) {
      const ResolvedNode& rn = all[k];
      if (!rn.node->is_window() || !rn.outer.intersect(screen).contains(m->x, m->y)) continue;
      if (layers_[top].modal && rn.layer != top) return {Route::Kind::Dropped, {}};
      if (m->kind == MouseEvent::Kind::Press) {
        if (rn.node->focusable && rn.layer == focus_layer()) focus(rn.node->id);
        captured_ = rn.node->id;
      }
      return {Route::Kind::Deliver, rn.node->id};
    }
    return {Route::Kind::Dropped, {}};
  }
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    const std::string_view action = bindings.action_for(*k, "stack");
    if (action == "stack.close_popup" && layers_.size() > 1) {
      std::string id = layers_.back().id;
      pop();
      return {Route::Kind::ClosedPopup, id};
    }
    if (action == "stack.focus_next" || action == "stack.focus_prev") {
      std::vector<const Node*> f;
      focusables(layers_[focus_layer()].root, f);
      if (f.size() > 1) {
        cycle_focus(action == "stack.focus_prev");
        return {Route::Kind::FocusMoved, focused()->id};
      }
    }
  }
  const Node* f = focused();
  if (!f) return {Route::Kind::Dropped, {}};
  return {Route::Kind::Deliver, f->id};
}

}  // namespace rolltui
