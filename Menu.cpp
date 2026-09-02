// rolltui/Menu.cpp — see Menu.hpp.
#include "rolltui/Menu.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Unicode.hpp"

namespace rolltui {

// ---- items ---------------------------------------------------------------------------

MenuItem MenuItem::action(std::string id, std::string label, std::string shortcut) {
  MenuItem m;
  m.kind = Kind::Action;
  m.id = std::move(id);
  m.label = std::move(label);
  m.shortcut = std::move(shortcut);
  return m;
}
MenuItem MenuItem::submenu(std::string id, std::string label, std::vector<MenuItem> children) {
  MenuItem m;
  m.kind = Kind::Submenu;
  m.id = std::move(id);
  m.label = std::move(label);
  m.children = std::move(children);
  return m;
}
MenuItem MenuItem::toggle(std::string id, std::string label, bool checked) {
  MenuItem m;
  m.kind = Kind::Toggle;
  m.id = std::move(id);
  m.label = std::move(label);
  m.checked = checked;
  return m;
}
MenuItem MenuItem::choice(std::string id, std::string label, std::vector<MenuItem> options, std::string value) {
  MenuItem m;
  m.kind = Kind::Choice;
  m.id = std::move(id);
  m.label = std::move(label);
  m.children = std::move(options);
  m.value = std::move(value);
  return m;
}
MenuItem MenuItem::input(std::string id, std::string label, std::string value) {
  MenuItem m;
  m.kind = Kind::Input;
  m.id = std::move(id);
  m.label = std::move(label);
  m.value = std::move(value);
  return m;
}
MenuItem MenuItem::input(std::string id, std::string label, InputSpec spec, std::string value) {
  MenuItem m = input(std::move(id), std::move(label), std::move(value));
  m.spec = std::move(spec);
  return m;
}

// ---- typed inputs: prefix validity, validity, canonical form -------------------------

std::string_view input_type_name(InputType t) {
  switch (t) {
    case InputType::Text: return "text";
    case InputType::Int: return "int";
    case InputType::Float: return "float";
    case InputType::Color: return "color";
    case InputType::Size: return "size";
    case InputType::Dim: return "dim";
    case InputType::Name: return "name";
  }
  return "text";
}

std::optional<InputType> input_type_from_name(std::string_view s) {
  for (InputType t : {InputType::Text, InputType::Int, InputType::Float, InputType::Color, InputType::Size, InputType::Dim, InputType::Name})
    if (input_type_name(t) == s) return t;
  return std::nullopt;
}

namespace {

std::string num_text(double v, int precision) {
  char b[64];
  if (precision >= 0) {
    std::snprintf(b, sizeof b, "%.*f", precision, v);
    return b;
  }
  if (v == std::floor(v) && std::fabs(v) < 1e15) {
    std::snprintf(b, sizeof b, "%.0f", v);
    return b;
  }
  std::snprintf(b, sizeof b, "%.10g", v);
  return b;
}

bool all_digits(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

// Could an integer whose decimal text begins with `v` (already read, `neg` signed)
// still land in [min, max] by appending digits?  ∃k ≥ 0: [v·10^k, v·10^k + 10^k − 1]
// (mirrored for negatives) meets the range.
bool int_reachable(double v, bool neg, double min, double max) {
  double scale = 1;
  for (int k = 0; k <= 18; ++k, scale *= 10) {
    const double lo = v * scale, hi = v * scale + (scale - 1);
    const double a = neg ? -hi : lo, b = neg ? -lo : hi;
    if (b >= min && a <= max) return true;
    if (lo > std::max(std::fabs(min), std::fabs(max))) break;
  }
  return false;
}

// Reads a signed decimal prefix: sign, integer digits, optional '.', fraction digits.
// Returns false when a character is not part of a number.
struct NumParts {
  bool neg = false, dot = false;
  std::string ip, fp;
};
bool split_number(std::string_view s, NumParts& out) {
  std::size_t i = 0;
  if (i < s.size() && s[i] == '-') { out.neg = true; ++i; }
  for (; i < s.size(); ++i) {
    const char c = s[i];
    if (c >= '0' && c <= '9') { (out.dot ? out.fp : out.ip).push_back(c); continue; }
    if (c == '.' && !out.dot) { out.dot = true; continue; }
    return false;
  }
  return true;
}

InputCheck check_int(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const std::string range = input_hint(spec);
  NumParts p;
  if (!split_number(text, p) || p.dot) { c.reason = "only digits" + std::string(spec.min < 0 ? " and a leading '-'" : "") + " (" + range + ")"; return c; }
  if (p.neg && spec.min >= 0) { c.reason = "no negatives (" + range + ")"; return c; }
  const double v = p.ip.empty() ? 0 : std::strtod(p.ip.c_str(), nullptr);
  if (!p.ip.empty() && !int_reachable(v, p.neg, spec.min, spec.max)) { c.reason = "nothing starting with '" + std::string(text) + "' fits " + range; return c; }
  if (p.ip.empty() && p.neg && !int_reachable(0, true, spec.min, spec.max) && !(spec.min < 0)) { c.reason = "no negatives (" + range + ")"; return c; }
  c.prefix_ok = true;
  if (text.empty()) { c.valid = spec.optional; c.reason = spec.optional ? "" : "a value is needed (" + range + ")"; return c; }
  if (p.ip.empty()) { c.reason = "a whole number (" + range + ")"; return c; }
  const double signed_v = p.neg ? -v : v;
  if (signed_v < spec.min || signed_v > spec.max) { c.reason = "a whole number " + range; return c; }
  c.valid = true;
  c.canonical = num_text(signed_v, 0);
  return c;
}

InputCheck check_float(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const std::string range = input_hint(spec);
  NumParts p;
  if (!split_number(text, p)) { c.reason = "only digits, one '.'" + std::string(spec.min < 0 ? " and a leading '-'" : "") + " (" + range + ")"; return c; }
  if (p.neg && spec.min >= 0) { c.reason = "no negatives (" + range + ")"; return c; }
  if (spec.precision >= 0 && static_cast<int>(p.fp.size()) > spec.precision) { c.reason = "at most " + std::to_string(spec.precision) + " digits after the point"; return c; }
  const double ip = p.ip.empty() ? 0 : std::strtod(p.ip.c_str(), nullptr);
  // Reachable values: without a point, any integer continuation plus a fraction; with a
  // point and d fraction digits, [v, v + 10^-d).
  bool reachable;
  if (!p.dot) {
    const double lo = ip, hi = ip + 1;  // ip followed by ".xxx"
    const double a = p.neg ? -hi : lo, b = p.neg ? -lo : hi;
    reachable = p.ip.empty() ? (p.neg ? spec.min < 0 : true) : (int_reachable(ip, p.neg, spec.min, spec.max) || (b >= spec.min && a <= spec.max));
  } else {
    const double v = std::strtod(((p.ip.empty() ? "0" : p.ip) + "." + (p.fp.empty() ? "0" : p.fp)).c_str(), nullptr);
    const double width = std::pow(10.0, -static_cast<double>(p.fp.size()));
    const double lo = v, hi = v + width;
    const double a = p.neg ? -hi : lo, b = p.neg ? -lo : hi;
    reachable = b >= spec.min && a <= spec.max;
  }
  if (!reachable) { c.reason = "nothing starting with '" + std::string(text) + "' fits " + range; return c; }
  c.prefix_ok = true;
  if (text.empty()) { c.valid = spec.optional; c.reason = spec.optional ? "" : "a value is needed (" + range + ")"; return c; }
  if (p.ip.empty() && p.fp.empty()) { c.reason = "a number (" + range + ")"; return c; }
  const double v = std::strtod(std::string(text).c_str(), nullptr);
  if (v < spec.min || v > spec.max) { c.reason = "a number " + range; return c; }
  c.valid = true;
  c.canonical = num_text(v, spec.precision);
  return c;
}

bool is_prefix_ci(std::string_view text, std::string_view word) {
  if (text.size() > word.size()) return false;
  for (std::size_t i = 0; i < text.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(text[i])) != word[i]) return false;
  return true;
}

InputCheck check_color(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const char* hint = "#rrggbb | 0-255 | none";
  bool prefix = text.empty() || is_prefix_ci(text, "none");
  if (!prefix && text[0] == '#') {
    prefix = text.size() <= 7;
    for (std::size_t i = 1; prefix && i < text.size(); ++i) prefix = std::isxdigit(static_cast<unsigned char>(text[i])) != 0;
  } else if (!prefix && all_digits(text)) {
    prefix = text.size() <= 3 && int_reachable(std::strtod(std::string(text).c_str(), nullptr), false, 0, 255);
  }
  if (!prefix) { c.reason = std::string("not the start of a colour (") + hint + ")"; return c; }
  c.prefix_ok = true;
  if (text.empty()) { c.valid = spec.optional; c.reason = spec.optional ? "" : std::string("a colour is needed (") + hint + ")"; return c; }
  std::string lower(text);
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (std::optional<Color> col = parse_color(lower)) { c.valid = true; c.canonical = color_to_string(*col); return c; }
  c.reason = std::string("not a colour yet (") + hint + ")";
  return c;
}

// N | N% | N% ± M — the shapes a Dim can be typed in; `size` adds fill / fill N.
bool dim_prefix(std::string_view t, bool size) {
  std::size_t i = 0;
  if (size && !t.empty() && t[0] == 'f') {
    if (is_prefix_ci(t, "fill")) return true;
    if (t.size() < 4 || t.substr(0, 4) != "fill") return false;
    i = 4;
    if (i < t.size() && t[i] != ' ') return false;
    if (i < t.size()) ++i;
    for (; i < t.size(); ++i)
      if (t[i] < '0' || t[i] > '9') return false;
    return true;
  }
  while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
  if (i == t.size()) return true;
  if (i == 0 || t[i] != '%') return false;
  ++i;
  if (i == t.size()) return true;
  if (t[i] == ' ') ++i;
  if (i == t.size()) return true;
  if (t[i] != '+' && t[i] != '-') return false;
  ++i;
  if (i < t.size() && t[i] == ' ') ++i;
  for (; i < t.size(); ++i)
    if (t[i] < '0' || t[i] > '9') return false;
  return true;
}

InputCheck check_size(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const char* hint = "fill | fill N | N% | N% ± cells | cells";
  if (!dim_prefix(text, true)) { c.reason = std::string("not the start of a size (") + hint + ")"; return c; }
  c.prefix_ok = true;
  if (text.empty()) { c.valid = spec.optional; c.reason = spec.optional ? "" : std::string("a size is needed (") + hint + ")"; return c; }
  if (std::optional<SplitSize> s = parse_size_text(text)) { c.valid = true; c.canonical = split_size_to_string(*s); return c; }
  c.reason = std::string("not a size yet (") + hint + ")";
  return c;
}

InputCheck check_dim(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const char* hint = "cells | N% | N% ± cells";
  if (!dim_prefix(text, false)) { c.reason = std::string("not the start of a dim (") + hint + ")"; return c; }
  c.prefix_ok = true;
  if (text.empty()) { c.valid = spec.optional; c.reason = spec.optional ? "" : std::string("a dim is needed (") + hint + ")"; return c; }
  std::optional<Dim> d = parse_dim(text);
  if (!d && all_digits(text)) d = Dim::abs(std::atoi(std::string(text).c_str()));
  if (d) { c.valid = true; c.canonical = dim_to_string(*d); return c; }
  c.reason = std::string("not a dim yet (") + hint + ")";
  return c;
}

InputCheck check_name(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const std::size_t cap = spec.max_len ? spec.max_len : 64;
  const char* hint = "letters, digits, - _ . (no leading dot)";
  if (text.size() > cap) { c.reason = "at most " + std::to_string(cap) + " characters"; return c; }
  if (!text.empty() && text[0] == '.') { c.reason = std::string("a name cannot start with a dot (") + hint + ")"; return c; }
  for (char ch : text)
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.')) { c.reason = std::string("only ") + hint; return c; }
  c.prefix_ok = true;
  if (text.empty()) { c.valid = spec.optional; c.reason = spec.optional ? "" : "a name is needed"; return c; }
  c.valid = true;
  c.canonical = std::string(text);
  return c;
}

InputCheck check_text(const InputSpec& spec, std::string_view text) {
  InputCheck c;
  const std::size_t len = unicode::graphemes(text).size();
  if (spec.max_len && len > spec.max_len) { c.reason = "at most " + std::to_string(spec.max_len) + " characters"; return c; }
  c.prefix_ok = true;
  if (spec.min_len && len < spec.min_len) { c.reason = "at least " + std::to_string(spec.min_len) + " characters"; return c; }
  c.valid = true;
  c.canonical = std::string(text);
  return c;
}

}  // namespace

std::string input_hint(const InputSpec& spec) {
  if (!spec.hint.empty()) return spec.hint;
  auto bound = [&](double v, bool is_min) {
    if (spec.type == InputType::Int) {
      if (is_min && v <= -1e15) return std::string("any");
      if (!is_min && v >= 1e15) return std::string("any");
      return num_text(v, 0);
    }
    if (is_min && v <= -1e15) return std::string("any");
    if (!is_min && v >= 1e15) return std::string("any");
    return num_text(v, spec.precision >= 0 ? spec.precision : 1);
  };
  switch (spec.type) {
    case InputType::Int: return bound(spec.min, true) + ".." + bound(spec.max, false);
    case InputType::Float: return bound(spec.min, true) + ".." + bound(spec.max, false) + (spec.precision >= 0 ? " (" + std::to_string(spec.precision) + " digits)" : "");
    case InputType::Color: return "#rrggbb | 0-255 | none";
    case InputType::Size: return "fill | fill N | N% | N% ± cells | cells";
    case InputType::Dim: return "cells | N% | N% ± cells";
    case InputType::Name: return "a name: letters, digits, - _ .";
    case InputType::Text: return spec.max_len ? "up to " + std::to_string(spec.max_len) + " characters" : "";
  }
  return "";
}

InputCheck check_input(const InputSpec& spec, std::string_view text) {
  switch (spec.type) {
    case InputType::Int: return check_int(spec, text);
    case InputType::Float: return check_float(spec, text);
    case InputType::Color: return check_color(spec, text);
    case InputType::Size: return check_size(spec, text);
    case InputType::Dim: return check_dim(spec, text);
    case InputType::Name: return check_name(spec, text);
    case InputType::Text: return check_text(spec, text);
  }
  return {};
}

// ---- JSON ----------------------------------------------------------------------------

namespace {

using json::Value;

const char* kind_name(MenuItem::Kind k) {
  switch (k) {
    case MenuItem::Kind::Action: return "action";
    case MenuItem::Kind::Submenu: return "submenu";
    case MenuItem::Kind::Toggle: return "toggle";
    case MenuItem::Kind::Choice: return "choice";
    case MenuItem::Kind::Input: return "input";
  }
  return "action";
}

std::optional<MenuItem::Kind> kind_from_name(std::string_view s) {
  if (s == "action") return MenuItem::Kind::Action;
  if (s == "submenu") return MenuItem::Kind::Submenu;
  if (s == "toggle") return MenuItem::Kind::Toggle;
  if (s == "choice") return MenuItem::Kind::Choice;
  if (s == "input") return MenuItem::Kind::Input;
  return std::nullopt;
}

// `ids` is the tree-wide set of action ids; a Choice's OPTIONS are values, unique only
// within their choice (two choices may both offer "auto"), so they get their own set.
MenuItem item_from_json(const Value& v, const std::string& where, MenuLoadReport& rep, std::vector<std::string>& ids) {
  MenuItem it;
  if (!v.is_object()) { rep.bad_values.push_back(where + ": expected an item object"); return it; }
  bool kind_given = false;
  std::vector<std::pair<std::string, const Value*>> spec_keys;
  for (const auto& [k, x] : v.obj) {
    const std::string at = where + "." + k;
    if (k == "id" || k == "label" || k == "shortcut" || k == "value") {
      if (!x.is_string()) { rep.bad_values.push_back(at + ": expected a string"); continue; }
      if (k == "id") it.id = x.str;
      else if (k == "label") it.label = x.str;
      else if (k == "shortcut") it.shortcut = x.str;
      else it.value = x.str;
    } else if (k == "kind") {
      auto kd = x.is_string() ? kind_from_name(x.str) : std::nullopt;
      if (!kd) rep.bad_values.push_back(at + ": expected action | submenu | toggle | choice | input");
      else { it.kind = *kd; kind_given = true; }
    } else if (k == "enabled" || k == "checked") {
      if (!x.is_bool()) { rep.bad_values.push_back(at + ": expected true or false"); continue; }
      (k == "enabled" ? it.enabled : it.checked) = x.b;
    } else if (k == "items") {
      if (!x.is_array()) { rep.bad_values.push_back(at + ": expected an array of items"); continue; }
      const bool choice = v.get("kind").as_string() == "choice";
      std::vector<std::string> option_ids;
      for (std::size_t i = 0; i < x.arr.size(); ++i)
        it.children.push_back(item_from_json(x.arr[i], at + "[" + std::to_string(i) + "]", rep, choice ? option_ids : ids));
    } else if (k == "type" || k == "min" || k == "max" || k == "step" || k == "precision" || k == "max_len" || k == "min_len" || k == "optional" ||
               k == "validator" || k == "hint") {
      spec_keys.emplace_back(k, &x);
    } else {
      rep.unknown_keys.push_back(at);
    }
  }
  if (!kind_given) it.kind = v.has("items") ? MenuItem::Kind::Submenu : MenuItem::Kind::Action;
  for (const auto& [k, xp] : spec_keys) {
    const Value& x = *xp;
    const std::string at = where + "." + k;
    if (it.kind != MenuItem::Kind::Input) { rep.unknown_keys.push_back(at + " (only an input has it)"); continue; }
    if (k == "type") {
      auto t = x.is_string() ? input_type_from_name(x.str) : std::nullopt;
      if (!t) rep.bad_values.push_back(at + ": expected text | int | float | color | size | dim | name");
      else it.spec.type = *t;
    } else if (k == "min" || k == "max" || k == "step") {
      if (!x.is_number()) { rep.bad_values.push_back(at + ": expected a number"); continue; }
      (k == "min" ? it.spec.min : k == "max" ? it.spec.max : it.spec.step) = x.num;
    } else if (k == "precision" || k == "max_len" || k == "min_len") {
      if (!x.is_number() || x.num < 0 || x.num != std::floor(x.num)) { rep.bad_values.push_back(at + ": expected a whole number ≥ 0"); continue; }
      if (k == "precision") it.spec.precision = static_cast<int>(x.num);
      else if (k == "max_len") it.spec.max_len = static_cast<std::size_t>(x.num);
      else it.spec.min_len = static_cast<std::size_t>(x.num);
    } else if (k == "optional") {
      if (!x.is_bool()) { rep.bad_values.push_back(at + ": expected true or false"); continue; }
      it.spec.optional = x.b;
    } else if (k == "validator" || k == "hint") {
      if (!x.is_string()) { rep.bad_values.push_back(at + ": expected a string"); continue; }
      (k == "validator" ? it.spec.validator : it.spec.hint) = x.str;
    }
  }
  if (it.kind == MenuItem::Kind::Input && it.spec.min > it.spec.max) rep.bad_values.push_back(where + ": min is above max");
  if (it.kind == MenuItem::Kind::Input && !it.spec.validator.empty() && it.spec.type != InputType::Text)
    rep.bad_values.push_back(where + ".validator: only a text input takes a validator (a typed input validates itself)");
  if (it.id.empty()) rep.bad_values.push_back(where + ": an item needs an \"id\"");
  else if (std::find(ids.begin(), ids.end(), it.id) != ids.end()) rep.bad_values.push_back(where + ".id: duplicate id '" + it.id + "'");
  else ids.push_back(it.id);
  if (it.label.empty()) it.label = it.id;
  return it;
}

Value item_to_json(const MenuItem& it) {
  Value o = Value::object();
  o.set("id", Value::string(it.id));
  if (it.label != it.id) o.set("label", Value::string(it.label));
  const bool implied = (it.kind == MenuItem::Kind::Submenu && !it.children.empty()) ||
                       (it.kind == MenuItem::Kind::Action && it.children.empty());
  if (!implied) o.set("kind", Value::string(kind_name(it.kind)));
  if (!it.shortcut.empty()) o.set("shortcut", Value::string(it.shortcut));
  if (!it.enabled) o.set("enabled", Value::boolean(false));
  if (it.checked) o.set("checked", Value::boolean(true));
  if (!it.value.empty()) o.set("value", Value::string(it.value));
  if (it.kind == MenuItem::Kind::Input) {
    const InputSpec d;
    const InputSpec& s = it.spec;
    if (s.type != d.type) o.set("type", Value::string(std::string(input_type_name(s.type))));
    if (s.min != d.min) o.set("min", Value::number(s.min));
    if (s.max != d.max) o.set("max", Value::number(s.max));
    if (s.step != d.step) o.set("step", Value::number(s.step));
    if (s.precision != d.precision) o.set("precision", Value::number(s.precision));
    if (s.max_len != d.max_len) o.set("max_len", Value::number(static_cast<double>(s.max_len)));
    if (s.min_len != d.min_len) o.set("min_len", Value::number(static_cast<double>(s.min_len)));
    if (s.optional) o.set("optional", Value::boolean(true));
    if (!s.validator.empty()) o.set("validator", Value::string(s.validator));
    if (!s.hint.empty()) o.set("hint", Value::string(s.hint));
  }
  if (!it.children.empty()) {
    Value arr = Value::array();
    for (const MenuItem& c : it.children) arr.arr.push_back(item_to_json(c));
    o.set("items", std::move(arr));
  }
  return o;
}

bool contains_ci(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  auto lower = [](std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
  };
  return lower(hay).find(lower(needle)) != std::string::npos;
}

}  // namespace

std::optional<MenuItem> menu_from_json(std::string_view json_text, MenuLoadReport& report) {
  report = MenuLoadReport{};
  std::string err;
  Value root = json::parse(json_text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  if (!root.is_object()) { report.error = "menu file must be a JSON object"; return std::nullopt; }
  std::vector<std::string> ids;
  MenuItem it = item_from_json(root, "", report, ids);
  for (std::vector<std::string>* list : {&report.unknown_keys, &report.bad_values})
    for (std::string& s : *list)
      if (!s.empty() && s[0] == '.') s.erase(0, 1);
  if (it.kind != MenuItem::Kind::Submenu) report.bad_values.push_back("kind: the root must be a submenu (it holds the top level)");
  return it;
}

std::string menu_to_json(const MenuItem& root) { return json::dump(item_to_json(root), 2) + "\n"; }

// ---- the widget ----------------------------------------------------------------------

Menu::Menu() {
  root_.kind = MenuItem::Kind::Submenu;
  InputOptions o;
  o.single_line = true;
  o.prompt.clear();
  edit_.set_options(o);
}
Menu::Menu(MenuItem root) : Menu() { set_root(std::move(root)); }

void Menu::set_root(MenuItem root) {
  root_ = std::move(root);
  root_.kind = MenuItem::Kind::Submenu;
  reset();
}

void Menu::reset() {
  path_.clear();
  sel_ = 0;
  top_ = 0;
  filter_.clear();
  editing_ = false;
  edit_reason_.clear();
  palette_ = false;
  rebuild_flat();
}

namespace {
MenuItem* find_in(MenuItem& it, std::string_view id) {
  if (it.id == id) return &it;
  for (MenuItem& c : it.children)
    if (MenuItem* f = find_in(c, id)) return f;
  return nullptr;
}
void collect_validators(const MenuItem& it, std::vector<std::string>& out) {
  if (it.kind == MenuItem::Kind::Input && !it.spec.validator.empty() && std::find(out.begin(), out.end(), it.spec.validator) == out.end())
    out.push_back(it.spec.validator);
  for (const MenuItem& c : it.children) collect_validators(c, out);
}
}  // namespace

MenuItem* Menu::find(std::string_view id) { return find_in(root_, id); }
const MenuItem* Menu::find(std::string_view id) const { return find_in(const_cast<MenuItem&>(root_), id); }

bool Menu::set_value(std::string_view id, std::string value) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->value = std::move(value);
  return true;
}
bool Menu::set_checked(std::string_view id, bool checked) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->checked = checked;
  return true;
}
bool Menu::set_enabled(std::string_view id, bool enabled) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->enabled = enabled;
  return true;
}
bool Menu::set_options(std::string_view id, std::vector<MenuItem> options) {
  MenuItem* it = find(id);
  if (!it) return false;
  it->children = std::move(options);
  rebuild_flat();
  clamp_selection();
  return true;
}

void Menu::set_validator(std::string_view name, Validator v) {
  for (auto& [n, fn] : validators_)
    if (n == name) { fn = std::move(v); return; }
  validators_.emplace_back(std::string(name), std::move(v));
}

std::vector<std::string> Menu::unknown_validators() const {
  std::vector<std::string> used, out;
  collect_validators(root_, used);
  for (const std::string& u : used) {
    bool known = false;
    for (const auto& [n, fn] : validators_) known |= n == u;
    if (!known) out.push_back(u);
  }
  return out;
}

MenuItem* Menu::by_path(const std::vector<std::size_t>& p) {
  MenuItem* it = &root_;
  for (std::size_t i : p) {
    if (i >= it->children.size()) return nullptr;
    it = &it->children[i];
  }
  return it;
}
const MenuItem* Menu::by_path(const std::vector<std::size_t>& p) const { return const_cast<Menu*>(this)->by_path(p); }

const MenuItem& Menu::level() const {
  const MenuItem* it = by_path(path_);
  return it ? *it : root_;
}
MenuItem& Menu::level_mut() {
  MenuItem* it = by_path(path_);
  return it ? *it : root_;
}

void Menu::rebuild_flat() {
  flat_.clear();
  std::vector<std::size_t> p;
  auto walk = [&](auto& self, const MenuItem& it, const std::string& prefix) -> void {
    for (std::size_t i = 0; i < it.children.size(); ++i) {
      const MenuItem& c = it.children[i];
      p.push_back(i);
      const std::string label = prefix.empty() ? c.label : prefix + " \xE2\x80\xBA " + c.label;
      switch (c.kind) {
        case MenuItem::Kind::Submenu:
          self(self, c, label);
          break;
        case MenuItem::Kind::Choice:
          for (std::size_t j = 0; j < c.children.size(); ++j) {
            p.push_back(j);
            flat_.push_back({p, label + " \xE2\x80\xBA " + c.children[j].label});
            p.pop_back();
          }
          break;
        default:
          flat_.push_back({p, label});
      }
      p.pop_back();
    }
  };
  walk(walk, root_, "");
}

std::vector<std::size_t> Menu::visible() const {
  std::vector<std::size_t> out;
  if (palette_) {
    for (std::size_t i = 0; i < flat_.size(); ++i)
      if (contains_ci(flat_[i].label, filter_)) out.push_back(i);
    return out;
  }
  const MenuItem& lv = level();
  for (std::size_t i = 0; i < lv.children.size(); ++i)
    if (contains_ci(lv.children[i].label, filter_)) out.push_back(i);
  return out;
}

const MenuItem* Menu::item_at(std::size_t vis_index) const { return const_cast<Menu*>(this)->item_at_mut(vis_index); }

MenuItem* Menu::item_at_mut(std::size_t vis_index) {
  std::vector<std::size_t> vis = visible();
  if (vis_index >= vis.size()) return nullptr;
  if (palette_) return by_path(flat_[vis[vis_index]].path);
  return &level_mut().children[vis[vis_index]];
}

const MenuItem* Menu::selected_item() const { return item_at(sel_); }

void Menu::clamp_selection() {
  const std::size_t n = visible().size();
  if (n == 0) sel_ = 0;
  else if (sel_ >= n) sel_ = n - 1;
  ensure_visible();
}

std::string Menu::breadcrumb() const {
  if (palette_) return root_.label.empty() ? "\xE2\x80\xBA" : root_.label + " \xE2\x80\xBA \xE2\x80\xA6";
  std::string s = root_.label;
  const MenuItem* it = &root_;
  for (std::size_t i : path_) {
    if (i >= it->children.size()) break;
    it = &it->children[i];
    if (!s.empty()) s += " \xE2\x80\xBA ";
    s += it->label;
  }
  return s;
}

void Menu::set_palette(bool on) {
  if (palette_ == on) return;
  palette_ = on;
  path_.clear();
  sel_ = 0;
  top_ = 0;
  filter_.clear();
  editing_ = false;
  rebuild_flat();
}

void Menu::descend(std::size_t child) {
  path_.push_back(child);
  filter_.clear();
  sel_ = 0;
  top_ = 0;
  const MenuItem& lv = level();
  if (lv.kind == MenuItem::Kind::Choice)
    for (std::size_t i = 0; i < lv.children.size(); ++i)
      if (lv.children[i].id == lv.value) { sel_ = i; break; }
  ensure_visible();
}

bool Menu::ascend() {
  if (path_.empty()) return false;
  const std::size_t was = path_.back();
  path_.pop_back();
  filter_.clear();
  sel_ = std::min(was, level().children.empty() ? std::size_t{0} : level().children.size() - 1);
  top_ = 0;
  ensure_visible();
  return true;
}

// ---- editing a typed field -------------------------------------------------------------

void Menu::begin_edit(MenuItem& it) {
  editing_ = true;
  edit_reason_.clear();
  edit_.set_text(it.value);
  edit_.select_all();  // typing replaces; a first arrow key places the caret
}

void Menu::refresh_reason() {
  const MenuItem* it = item_at(sel_);
  if (!it) return;
  const InputCheck c = check_input(it->spec, edit_.text());
  edit_reason_ = c.valid ? "" : c.reason;
}

bool Menu::try_insert(std::string_view text) {
  MenuItem* it = item_at_mut(sel_);
  if (!it) return false;
  // What the text would be after the insertion (replacing a selection), checked as a
  // prefix of some valid value before it lands. Never a coercion: refused or inserted.
  Input probe = edit_;
  probe.insert(text);
  const InputCheck c = check_input(it->spec, probe.text());
  if (!c.prefix_ok) { edit_reason_ = c.reason; return false; }
  edit_ = std::move(probe);
  refresh_reason();
  return true;
}

void Menu::step(int direction) {
  MenuItem* it = item_at_mut(sel_);
  if (!it || (it->spec.type != InputType::Int && it->spec.type != InputType::Float)) return;
  const InputSpec& s = it->spec;
  InputCheck now = check_input(s, edit_.text());
  double v;
  if (now.valid && !edit_.text().empty()) v = std::strtod(edit_.text().c_str(), nullptr);
  else {
    const InputCheck committed = check_input(s, it->value);
    v = committed.valid && !it->value.empty() ? std::strtod(it->value.c_str(), nullptr) : (s.min > -1e15 ? s.min : 0);
    if (!(now.valid && !edit_.text().empty())) { edit_.set_text(num_text(v, s.type == InputType::Int ? 0 : s.precision)); refresh_reason(); return; }
  }
  v = std::clamp(v + direction * s.step, s.min, s.max);
  edit_.set_text(num_text(v, s.type == InputType::Int ? 0 : s.precision));
  refresh_reason();
}

MenuEvent Menu::handle_edit(const Event& e, const Bindings& b) {
  using K = MenuEvent::Kind;
  MenuItem* it = item_at_mut(sel_);
  if (!it) { editing_ = false; return {}; }
  if (const auto* p = std::get_if<PasteEvent>(&e)) { try_insert(p->text); return {}; }
  const auto* k = std::get_if<KeyEvent>(&e);
  if (!k) return {};
  const std::string_view ed = b.action_for(*k, "edit");
  if (ed == "edit.commit") {
    InputCheck c = check_input(it->spec, edit_.text());
    if (c.valid && it->spec.type == InputType::Text && !it->spec.validator.empty()) {
      bool known = false;
      for (const auto& [n, fn] : validators_)
        if (n == it->spec.validator) {
          known = true;
          if (std::optional<std::string> why = fn(edit_.text())) { c.valid = false; c.reason = *why; }
        }
      if (!known) { c.valid = false; c.reason = "no validator named '" + it->spec.validator + "' is registered"; }
    }
    if (!c.valid) { edit_reason_ = c.reason; return {}; }
    it->value = c.canonical;
    editing_ = false;
    edit_reason_.clear();
    return {K::Input, it->id, it->value, false};
  }
  if (ed == "edit.cancel") { editing_ = false; edit_reason_.clear(); return {}; }
  if (ed == "edit.step_up") { step(+1); return {}; }
  if (ed == "edit.step_down") { step(-1); return {}; }
  if (k->key == Key::Char && !k->ctrl && !k->alt && k->ch >= 0x20 && k->ch != 0x7F) {
    std::string s;
    unicode::append_utf8(s, k->ch);
    try_insert(s);
    return {};
  }
  // Everything else is the input widget's: the caret, selection, deletions, kills.
  // Submit/Eof there are not ours (Enter is edit.commit above; Ctrl-D deletes forward).
  const InputAction a = edit_.handle(e, b);
  if (a == InputAction::Handled) refresh_reason();
  return {};
}

MenuEvent Menu::act(std::size_t vis_index) {
  MenuItem* it = item_at_mut(vis_index);
  if (!it || !it->enabled) return {};
  if (palette_) {
    const FlatEntry& fe = flat_[visible()[vis_index]];
    if (fe.path.size() >= 2) {
      std::vector<std::size_t> parent(fe.path.begin(), fe.path.end() - 1);
      MenuItem* p = by_path(parent);
      if (p && p->kind == MenuItem::Kind::Choice) {
        p->value = it->id;
        return {MenuEvent::Kind::Choose, p->id, it->id, false};
      }
    }
  } else if (level().kind == MenuItem::Kind::Choice) {
    MenuItem& choice = level_mut();
    choice.value = it->id;
    MenuEvent ev{MenuEvent::Kind::Choose, choice.id, it->id, false};
    ascend();
    return ev;
  }
  switch (it->kind) {
    case MenuItem::Kind::Action:
      return {MenuEvent::Kind::Activate, it->id, {}, false};
    case MenuItem::Kind::Toggle:
      it->checked = !it->checked;
      return {MenuEvent::Kind::Toggle, it->id, {}, it->checked};
    case MenuItem::Kind::Submenu:
    case MenuItem::Kind::Choice:
      if (palette_) return {};
      descend(visible()[vis_index]);
      return {};
    case MenuItem::Kind::Input:
      sel_ = vis_index;
      begin_edit(*it);
      return {};
  }
  return {};
}

MenuEvent Menu::handle(const Event& e, const Bindings& b) {
  if (editing_) return handle_edit(e, b);
  if (const auto* k = std::get_if<KeyEvent>(&e)) return handle_key(*k, b);
  if (const auto* m = std::get_if<MouseEvent>(&e)) return handle_mouse(*m);
  if (const auto* p = std::get_if<PasteEvent>(&e)) {
    // Pasted text goes where typed text would: the filter.
    for (char c : p->text)
      if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
        KeyEvent ke;
        ke.key = Key::Char;
        ke.ch = static_cast<char32_t>(static_cast<unsigned char>(c));
        handle_key(ke, b);
      }
    return {};
  }
  return {};
}

MenuEvent Menu::handle_key(const KeyEvent& k, const Bindings& b) {
  using K = MenuEvent::Kind;
  const bool text = k.key == Key::Char && !k.ctrl && !k.alt && k.ch >= 0x20 && k.ch != 0x7F;
  const std::string_view action = text ? std::string_view() : b.action_for(k, "menu");
  const std::vector<std::size_t> vis = visible();
  const std::size_t n = vis.size();
  auto move_to = [&](std::size_t i) {
    if (n == 0) { sel_ = 0; return; }
    sel_ = std::min(i, n - 1);
    ensure_visible();
  };
  if (text) {
    unicode::append_utf8(filter_, k.ch);
    sel_ = 0;
    top_ = 0;
    ensure_visible();
    return {};
  }
  if (action == "menu.up") { move_to(sel_ == 0 ? 0 : sel_ - 1); return {}; }
  if (action == "menu.down") { move_to(sel_ + 1); return {}; }
  if (action == "menu.page_up") { const std::size_t step = static_cast<std::size_t>(std::max(item_rows(), 1)); move_to(sel_ < step ? 0 : sel_ - step); return {}; }
  if (action == "menu.page_down") { move_to(sel_ + static_cast<std::size_t>(std::max(item_rows(), 1))); return {}; }
  if (action == "menu.first") { move_to(0); return {}; }
  if (action == "menu.last") { move_to(n == 0 ? 0 : n - 1); return {}; }
  if (action == "menu.activate") return act(sel_);
  if (action == "menu.descend") {
    const MenuItem* it = item_at(sel_);
    if (it && it->enabled && !palette_ && (it->kind == MenuItem::Kind::Submenu || it->kind == MenuItem::Kind::Choice)) descend(vis[sel_]);
    return {};
  }
  if (action == "menu.ascend") {
    if (!filter_.empty()) { filter_.clear(); clamp_selection(); return {}; }
    ascend();
    return {};
  }
  if (action == "menu.back") {
    if (!filter_.empty()) { filter_.clear(); clamp_selection(); return {}; }
    if (ascend()) return {};
    return {K::Closed, {}, {}, false};
  }
  if (action == "menu.erase") {
    if (!filter_.empty()) {
      std::vector<unicode::Grapheme> g = unicode::graphemes(filter_);
      filter_.erase(g.back().offset);
      clamp_selection();
    }
    return {};
  }
  return {};
}

MenuEvent Menu::handle_mouse(const MouseEvent& m) {
  using Kd = MouseEvent::Kind;
  if (m.kind == Kd::WheelUp) { if (sel_ > 0) { --sel_; ensure_visible(); } return {}; }
  if (m.kind == Kd::WheelDown) { const std::size_t n = visible().size(); if (n && sel_ + 1 < n) { ++sel_; ensure_visible(); } return {}; }
  if (m.kind != Kd::Press || m.button != 1) return {};
  if (!area_.contains(m.x, m.y)) return {};
  const int first_item_row = area_.h >= 2 ? area_.y + 1 : area_.y;
  if (m.y < first_item_row) return {};
  const std::size_t idx = static_cast<std::size_t>(top_) + static_cast<std::size_t>(m.y - first_item_row);
  if (idx >= visible().size()) return {};
  sel_ = idx;
  return act(sel_);
}

int Menu::item_rows() const { return area_.h >= 2 ? area_.h - 1 : area_.h; }

void Menu::ensure_visible() {
  const int rows = item_rows();
  if (rows <= 0) { top_ = 0; return; }
  if (static_cast<int>(sel_) < top_) top_ = static_cast<int>(sel_);
  if (static_cast<int>(sel_) >= top_ + rows) top_ = static_cast<int>(sel_) - rows + 1;
  const int n = static_cast<int>(visible().size());
  top_ = std::clamp(top_, 0, std::max(0, n - rows));
}

int Menu::rows_for() const { return 1 + std::max<int>(1, static_cast<int>(visible().size())); }

void Menu::layout(Rect area) {
  area_ = area;
  ensure_visible();
}

std::string Menu::row_text(const MenuItem& it, bool in_palette, std::size_t vis_index) const {
  std::string s;
  if (in_palette) {
    s = flat_[visible()[vis_index]].label;
    if (it.kind == MenuItem::Kind::Toggle) s = std::string(it.checked ? "[x] " : "[ ] ") + s;
    return s;
  }
  switch (it.kind) {
    case MenuItem::Kind::Toggle: s = std::string(it.checked ? "[x] " : "[ ] ") + it.label; break;
    case MenuItem::Kind::Input: s = it.label + ": " + it.value; break;
    default: s = it.label;
  }
  if (level().kind == MenuItem::Kind::Choice && it.id == level().value) s = "\xE2\x80\xA2 " + s;  // • the current option
  return s;
}

void Menu::draw(Frame& f, const Theme& theme, bool focused) const {
  const Rect a = area_;
  if (a.w <= 0 || a.h <= 0) return;
  const int x0 = a.x + opt_.inset, w = a.w - 2 * opt_.inset;
  if (w <= 0) return;
  const std::vector<std::size_t> vis = visible();
  int y = a.y;
  if (a.h >= 2) {
    if (editing_) {
      // The breadcrumb yields to the field's guidance: the reason a key or a commit was
      // refused when there is one, else the constraint — first, because a popup is
      // narrow and where you are is already shown by the highlighted field with the
      // caret in it.
      const MenuItem* it = item_at(sel_);
      const std::string hint = it ? input_hint(it->spec) : "";
      std::string line = !edit_reason_.empty() ? "\xE2\x9C\x97 " + edit_reason_
                         : hint.empty()        ? "editing \xE2\x80\x94 Enter commits, Esc cancels"
                                               : "editing \xE2\x80\x94 " + hint;
      f.put_text(x0, y, line, theme.style(edit_reason_.empty() ? Role::menu_shortcut : Role::warning), w, opt_.ambiguous_wide);
    } else {
      const int used = f.put_text(x0, y, breadcrumb(), theme.style(Role::menu_breadcrumb), w, opt_.ambiguous_wide);
      if (!filter_.empty()) f.put_text(x0 + used, y, "  /" + filter_, theme.style(Role::menu_shortcut), std::max(w - used, 0), opt_.ambiguous_wide);
    }
    ++y;
  }
  const int rows = item_rows();
  const Style sel = theme.style(Role::menu_selected), item = theme.style(Role::menu_item), muted = theme.style(Role::text_muted);
  const Style shortcut = theme.style(Role::menu_shortcut), marker = theme.style(Role::scroll_marker);
  if (vis.empty()) {
    if (rows > 0) f.put_text(x0, y, filter_.empty() ? "(empty)" : "(no match for /" + filter_ + ")", muted, w, opt_.ambiguous_wide);
    return;
  }
  for (int r = 0; r < rows; ++r) {
    const std::size_t i = static_cast<std::size_t>(top_ + r);
    if (i >= vis.size()) break;
    const MenuItem* it = item_at(i);
    if (!it) break;
    const bool is_sel = i == sel_;
    const Style& base = is_sel ? sel : (it->enabled ? item : muted);
    f.fill({x0, y + r, w, 1}, base);
    if (is_sel && editing_) {
      // The field: its label, then the input widget's own drawing (caret, selection).
      const std::string label = it->label + ": ";
      const int used = f.put_text(x0, y + r, label, base, w, opt_.ambiguous_wide);
      const Rect field{x0 + used, y + r, std::max(w - used, 0), 1};
      if (field.w > 0) {
        Input& ed = const_cast<Input&>(edit_);
        InputOptions o = ed.options();
        o.ambiguous_wide = opt_.ambiguous_wide;
        if (!(o == ed.options())) ed.set_options(o);
        ed.layout(field);
        ed.draw(f, theme, focused);
      }
      continue;
    }
    std::string text = row_text(*it, palette_, i);
    std::string right;
    if (!palette_) {
      if (it->kind == MenuItem::Kind::Choice) right = it->value + " \xE2\x96\xB8";
      else if (it->kind == MenuItem::Kind::Submenu) right = "\xE2\x96\xB8";
      else if (!it->shortcut.empty()) right = it->shortcut;
    }
    const int rw = right.empty() ? 0 : unicode::display_width(right, opt_.ambiguous_wide);
    const int left_max = right.empty() ? w : std::max(w - rw - 1, 0);
    const int used = f.put_text(x0, y + r, text, base, left_max, opt_.ambiguous_wide);
    if (rw > 0 && rw <= w) {
      const Style rs = is_sel ? sel : (it->kind == MenuItem::Kind::Choice || it->kind == MenuItem::Kind::Submenu ? base : shortcut);
      f.put_text(x0 + std::max(w - rw, used + 1), y + r, right, rs, std::max(w - std::max(w - rw, used + 1), 0), opt_.ambiguous_wide);
    }
  }
  if (w >= 1 && rows >= 1) {
    if (top_ > 0) f.put(x0 + w - 1, y, "\xE2\x96\xB2", 1, marker);
    if (static_cast<std::size_t>(top_ + rows) < vis.size()) f.put(x0 + w - 1, y + rows - 1, "\xE2\x96\xBC", 1, marker);
  }
}

}  // namespace rolltui
