// rolltui/MenuCpp.cpp — the C++ side of the menu widget and the typed-field rules, behind the
// same boundary as `c/rolltui_menu.c` (rolltui/c/rolltui_menu.h, Phase 15 m5). One CMake flag
// picks which of the two links; both satisfy `rolltui/tests/menu_test.cpp`, the three editors,
// the files-only proof and every golden frame.
//
// This is the code Phase 9 m11 wrote and Phase 10 m5 grew the typed fields into, moved behind
// the boundary. It is deliberately NOT a transliteration of the C: it keeps `std::string`,
// `std::vector`, `std::optional` and the recursive lambda the flat walk was always written as.
#include "rolltui/c/rolltui_menu.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/c/rolltui_layout.h"

namespace {

std::string_view view(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }
std::string_view view(const char* p, std::size_t n) { return std::string_view(p ? p : "", n); }

constexpr const char* kCrumb = " \xE2\x80\xBA ";

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

// Could an integer whose decimal text begins with `v` (already read, `neg` signed) still land
// in [min, max] by appending digits?  ∃k ≥ 0: [v·10^k, v·10^k + 10^k − 1] meets the range.
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
struct NumParts {
  bool neg = false, dot = false;
  std::string ip, fp;
};
bool split_number(std::string_view s, NumParts& out) {
  std::size_t i = 0;
  if (i < s.size() && s[i] == '-') {
    out.neg = true;
    ++i;
  }
  for (; i < s.size(); ++i) {
    const char c = s[i];
    if (c >= '0' && c <= '9') {
      (out.dot ? out.fp : out.ip).push_back(c);
      continue;
    }
    if (c == '.' && !out.dot) {
      out.dot = true;
      continue;
    }
    return false;
  }
  return true;
}

std::string hint_of(const RolltuiInputSpec& spec) {
  RolltuiStr s;
  rolltui_input_hint(&spec, &s);
  return std::string(view(s));
}

struct Check {
  bool prefix_ok = false, valid = false;
  std::string reason, canonical;
};

Check check_int(const RolltuiInputSpec& spec, std::string_view text) {
  Check c;
  const std::string range = hint_of(spec);
  NumParts p;
  if (!split_number(text, p) || p.dot) {
    c.reason = "only digits" + std::string(spec.min < 0 ? " and a leading '-'" : "") + " (" + range + ")";
    return c;
  }
  if (p.neg && spec.min >= 0) {
    c.reason = "no negatives (" + range + ")";
    return c;
  }
  const double v = p.ip.empty() ? 0 : std::strtod(p.ip.c_str(), nullptr);
  if (!p.ip.empty() && !int_reachable(v, p.neg, spec.min, spec.max)) {
    c.reason = "nothing starting with '" + std::string(text) + "' fits " + range;
    return c;
  }
  if (p.ip.empty() && p.neg && !int_reachable(0, true, spec.min, spec.max) && !(spec.min < 0)) {
    c.reason = "no negatives (" + range + ")";
    return c;
  }
  c.prefix_ok = true;
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : "a value is needed (" + range + ")";
    return c;
  }
  if (p.ip.empty()) {
    c.reason = "a whole number (" + range + ")";
    return c;
  }
  const double signed_v = p.neg ? -v : v;
  if (signed_v < spec.min || signed_v > spec.max) {
    c.reason = "a whole number " + range;
    return c;
  }
  c.valid = true;
  c.canonical = num_text(signed_v, 0);
  return c;
}

Check check_float(const RolltuiInputSpec& spec, std::string_view text) {
  Check c;
  const std::string range = hint_of(spec);
  NumParts p;
  if (!split_number(text, p)) {
    c.reason = "only digits, one '.'" + std::string(spec.min < 0 ? " and a leading '-'" : "") + " (" + range + ")";
    return c;
  }
  if (p.neg && spec.min >= 0) {
    c.reason = "no negatives (" + range + ")";
    return c;
  }
  if (spec.precision >= 0 && static_cast<int>(p.fp.size()) > spec.precision) {
    c.reason = "at most " + std::to_string(spec.precision) + " digits after the point";
    return c;
  }
  const double ip = p.ip.empty() ? 0 : std::strtod(p.ip.c_str(), nullptr);
  // Reachable values: without a point, any integer continuation plus a fraction; with a
  // point and d fraction digits, [v, v + 10^-d).
  bool reachable;
  if (!p.dot) {
    const double lo = ip, hi = ip + 1;  // ip followed by ".xxx"
    const double a = p.neg ? -hi : lo, b = p.neg ? -lo : hi;
    reachable = p.ip.empty() ? (p.neg ? spec.min < 0 : true)
                             : (int_reachable(ip, p.neg, spec.min, spec.max) || (b >= spec.min && a <= spec.max));
  } else {
    const double v =
        std::strtod(((p.ip.empty() ? "0" : p.ip) + "." + (p.fp.empty() ? "0" : p.fp)).c_str(), nullptr);
    const double width = std::pow(10.0, -static_cast<double>(p.fp.size()));
    const double lo = v, hi = v + width;
    const double a = p.neg ? -hi : lo, b = p.neg ? -lo : hi;
    reachable = b >= spec.min && a <= spec.max;
  }
  if (!reachable) {
    c.reason = "nothing starting with '" + std::string(text) + "' fits " + range;
    return c;
  }
  c.prefix_ok = true;
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : "a value is needed (" + range + ")";
    return c;
  }
  if (p.ip.empty() && p.fp.empty()) {
    c.reason = "a number (" + range + ")";
    return c;
  }
  const double v = std::strtod(std::string(text).c_str(), nullptr);
  if (v < spec.min || v > spec.max) {
    c.reason = "a number " + range;
    return c;
  }
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

Check check_color(const RolltuiInputSpec& spec, std::string_view text) {
  Check c;
  const char* hint = "#rrggbb | 0-255 | none";
  bool prefix = text.empty() || is_prefix_ci(text, "none");
  if (!prefix && text[0] == '#') {
    prefix = text.size() <= 7;
    for (std::size_t i = 1; prefix && i < text.size(); ++i)
      prefix = std::isxdigit(static_cast<unsigned char>(text[i])) != 0;
  } else if (!prefix && all_digits(text)) {
    prefix = text.size() <= 3 && int_reachable(std::strtod(std::string(text).c_str(), nullptr), false, 0, 255);
  }
  if (!prefix) {
    c.reason = std::string("not the start of a colour (") + hint + ")";
    return c;
  }
  c.prefix_ok = true;
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : std::string("a colour is needed (") + hint + ")";
    return c;
  }
  std::string lower(text);
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  RolltuiStyleColor col;
  if (rolltui_color_parse(lower.data(), lower.size(), &col)) {
    char out[ROLLTUI_COLOR_STRING_MAX];
    c.valid = true;
    c.canonical.assign(out, rolltui_color_to_string(col, out, sizeof out));
    return c;
  }
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

Check check_size(const RolltuiInputSpec& spec, std::string_view text) {
  Check c;
  const char* hint = "fill | fill N | N% | N% \xC2\xB1 cells | cells";
  if (!dim_prefix(text, true)) {
    c.reason = std::string("not the start of a size (") + hint + ")";
    return c;
  }
  c.prefix_ok = true;
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : std::string("a size is needed (") + hint + ")";
    return c;
  }
  RolltuiSplitSize s;
  if (rolltui_parse_size_text(text.data(), text.size(), &s)) {
    char out[ROLLTUI_DIM_STRING_MAX];
    c.valid = true;
    c.canonical.assign(out, rolltui_split_size_to_string(s, out, sizeof out));
    return c;
  }
  c.reason = std::string("not a size yet (") + hint + ")";
  return c;
}

Check check_dim(const RolltuiInputSpec& spec, std::string_view text) {
  Check c;
  const char* hint = "cells | N% | N% \xC2\xB1 cells";
  if (!dim_prefix(text, false)) {
    c.reason = std::string("not the start of a dim (") + hint + ")";
    return c;
  }
  c.prefix_ok = true;
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : std::string("a dim is needed (") + hint + ")";
    return c;
  }
  RolltuiDim d;
  bool have = rolltui_parse_dim(text.data(), text.size(), &d) != 0;
  if (!have && all_digits(text)) {
    d = RolltuiDim::abs(std::atoi(std::string(text).c_str()));
    have = true;
  }
  if (have) {
    char out[ROLLTUI_DIM_STRING_MAX];
    c.valid = true;
    c.canonical.assign(out, rolltui_dim_to_string(d, out, sizeof out));
    return c;
  }
  c.reason = std::string("not a dim yet (") + hint + ")";
  return c;
}

Check check_name(const RolltuiInputSpec& spec, std::string_view text) {
  Check c;
  const std::size_t cap = spec.max_len ? spec.max_len : 64;
  const char* hint = "letters, digits, - _ . (no leading dot)";
  if (text.size() > cap) {
    c.reason = "at most " + std::to_string(cap) + " characters";
    return c;
  }
  if (!text.empty() && text[0] == '.') {
    c.reason = std::string("a name cannot start with a dot (") + hint + ")";
    return c;
  }
  for (char ch : text)
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.')) {
      c.reason = std::string("only ") + hint;
      return c;
    }
  c.prefix_ok = true;
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : "a name is needed";
    return c;
  }
  c.valid = true;
  c.canonical = std::string(text);
  return c;
}

Check check_text(const RolltuiInputSpec& spec, std::string_view text, RolltuiUnicodeScratch* u) {
  Check c;
  std::vector<RolltuiUnicodeGrapheme> g(text.size() + 1);
  const std::size_t len = rolltui_u_graphemes(u, text.data(), text.size(), 0, g.data());
  if (spec.max_len && len > spec.max_len) {
    c.reason = "at most " + std::to_string(spec.max_len) + " characters";
    return c;
  }
  c.prefix_ok = true;
  // The same empty rule as every other type; `min_len` is a length rule, never the emptiness
  // rule (Menu.hpp, and the Phase 10 m5 defect behind it).
  if (text.empty()) {
    c.valid = spec.optional;
    c.reason = spec.optional ? "" : "a value is needed";
    return c;
  }
  if (spec.min_len && len < spec.min_len) {
    c.reason = "at least " + std::to_string(spec.min_len) + " characters";
    return c;
  }
  c.valid = true;
  c.canonical = std::string(text);
  return c;
}

Check run_check(const RolltuiInputSpec& spec, std::string_view text, RolltuiUnicodeScratch* u) {
  switch (static_cast<unsigned char>(spec.type)) {
    case ROLLTUI_INPUT_TYPE_INT: return check_int(spec, text);
    case ROLLTUI_INPUT_TYPE_FLOAT: return check_float(spec, text);
    case ROLLTUI_INPUT_TYPE_COLOR: return check_color(spec, text);
    case ROLLTUI_INPUT_TYPE_SIZE: return check_size(spec, text);
    case ROLLTUI_INPUT_TYPE_DIM: return check_dim(spec, text);
    case ROLLTUI_INPUT_TYPE_NAME: return check_name(spec, text);
    default: return check_text(spec, text, u);
  }
}

constexpr const char* kTypeNames[ROLLTUI_INPUT_TYPE_COUNT] = {"text", "int", "float", "color",
                                                              "size", "dim", "name"};

bool contains_ci(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > hay.size()) return false;
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    std::size_t j = 0;
    for (; j < needle.size(); ++j)
      if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j])))
        break;
    if (j == needle.size()) return true;
  }
  return false;
}

}  // namespace

// ---- the pure functions --------------------------------------------------------------------

extern "C" void rolltui_input_check_release(RolltuiInputCheck* c) {
  rolltui_str_free(&c->reason);
  rolltui_str_free(&c->canonical);
}

extern "C" void rolltui_check_input(const RolltuiInputSpec* spec, const char* text, std::size_t len,
                                    RolltuiUnicodeScratch* u, RolltuiInputCheck* out) {
  const Check c = run_check(*spec, view(text, len), u);
  out->prefix_ok = static_cast<unsigned char>(c.prefix_ok);
  out->valid = static_cast<unsigned char>(c.valid);
  out->reason = c.reason;
  out->canonical = c.canonical;
}

extern "C" void rolltui_input_hint(const RolltuiInputSpec* spec, RolltuiStr* out) {
  if (spec->hint.n) {
    *out = view(spec->hint);
    return;
  }
  auto bound = [&](double v, bool is_min) {
    if ((is_min && v <= -1e15) || (!is_min && v >= 1e15)) return std::string("any");
    return num_text(v, static_cast<unsigned char>(spec->type) == ROLLTUI_INPUT_TYPE_INT
                           ? 0
                           : (spec->precision >= 0 ? spec->precision : 1));
  };
  switch (static_cast<unsigned char>(spec->type)) {
    case ROLLTUI_INPUT_TYPE_INT: *out = bound(spec->min, true) + ".." + bound(spec->max, false); return;
    case ROLLTUI_INPUT_TYPE_FLOAT:
      *out = bound(spec->min, true) + ".." + bound(spec->max, false) +
             (spec->precision >= 0 ? " (" + std::to_string(spec->precision) + " digits)" : "");
      return;
    case ROLLTUI_INPUT_TYPE_COLOR: *out = std::string_view("#rrggbb | 0-255 | none"); return;
    case ROLLTUI_INPUT_TYPE_SIZE:
      *out = std::string_view("fill | fill N | N% | N% \xC2\xB1 cells | cells");
      return;
    case ROLLTUI_INPUT_TYPE_DIM: *out = std::string_view("cells | N% | N% \xC2\xB1 cells"); return;
    case ROLLTUI_INPUT_TYPE_NAME: *out = std::string_view("a name: letters, digits, - _ ."); return;
    default:
      *out = spec->max_len ? "up to " + std::to_string(spec->max_len) + " characters" : std::string();
      return;
  }
}

extern "C" const char* rolltui_input_type_name(unsigned char type, std::size_t* len) {
  const char* s = type < ROLLTUI_INPUT_TYPE_COUNT ? kTypeNames[type] : "";
  if (len) *len = std::strlen(s);
  return s;
}

extern "C" int rolltui_input_type_from_name(const char* name, std::size_t len, unsigned char* out) {
  for (unsigned char i = 0; i < ROLLTUI_INPUT_TYPE_COUNT; ++i)
    if (view(name, len) == kTypeNames[i]) {
      *out = i;
      return 1;
    }
  return 0;
}

extern "C" void rolltui_menu_event_release(RolltuiMenuEvent* e) {
  rolltui_str_free(&e->id);
  rolltui_str_free(&e->value);
}

// ---- the widget -------------------------------------------------------------------------------

namespace {
struct FlatEntry {
  std::vector<std::size_t> path;
  std::string label;
};
}  // namespace

struct RolltuiMenu {
  RolltuiMenuItem root;
  std::vector<std::size_t> path;
  std::size_t sel = 0;
  std::string filter;
  bool editing = false;
  RolltuiInput* edit = nullptr;  // BORROWED: `rolltui::Menu` owns it
  std::string edit_reason;
  bool palette = false;
  std::vector<FlatEntry> flat;
  RolltuiMenuOptions opt;
  RolltuiRect area;
  int top = 0;
  std::vector<std::size_t> vis;  // the visible list, rebuilt on demand and LENT
  RolltuiStr probe;
  RolltuiValidatorFn vfn = nullptr;
  void* vctx = nullptr;
  RolltuiUnicodeScratch* u = nullptr;  // OWNED: the checks' cluster walk

  RolltuiMenuItem* by_path(const std::size_t* p, std::size_t n) {
    RolltuiMenuItem* it = &root;
    for (std::size_t k = 0; k < n; ++k) {
      if (p[k] >= it->children.size()) return nullptr;
      it = &it->children[p[k]];
    }
    return it;
  }
  RolltuiMenuItem& level() {
    RolltuiMenuItem* it = by_path(path.data(), path.size());
    return it ? *it : root;
  }

  void rebuild_flat() {
    flat.clear();
    std::vector<std::size_t> p;
    auto walk = [&](auto& self, const RolltuiMenuItem& it, const std::string& prefix) -> void {
      for (std::size_t i = 0; i < it.children.size(); ++i) {
        const RolltuiMenuItem& c = it.children[i];
        p.push_back(i);
        const std::string label = prefix.empty() ? std::string(view(c.label)) : prefix + kCrumb + std::string(view(c.label));
        switch (static_cast<unsigned char>(c.kind)) {
          case ROLLTUI_MENU_SUBMENU:
            self(self, c, label);
            break;
          case ROLLTUI_MENU_CHOICE:
            for (std::size_t j = 0; j < c.children.size(); ++j) {
              p.push_back(j);
              flat.push_back({p, label + kCrumb + std::string(view(c.children[j].label))});
              p.pop_back();
            }
            break;
          default:
            flat.push_back({p, label});
        }
        p.pop_back();
      }
    };
    walk(walk, root, "");
  }

  const std::vector<std::size_t>& build_visible() {
    vis.clear();
    if (palette) {
      for (std::size_t i = 0; i < flat.size(); ++i)
        if (contains_ci(flat[i].label, filter)) vis.push_back(i);
      return vis;
    }
    const RolltuiMenuItem& lv = level();
    for (std::size_t i = 0; i < lv.children.size(); ++i)
      if (contains_ci(view(lv.children[i].label), filter)) vis.push_back(i);
    return vis;
  }

  RolltuiMenuItem* item_at(std::size_t vis_index) {
    const std::vector<std::size_t>& v = build_visible();
    if (vis_index >= v.size()) return nullptr;
    if (palette) {
      const FlatEntry& e = flat[v[vis_index]];
      return by_path(e.path.data(), e.path.size());
    }
    return &level().children[v[vis_index]];
  }

  int item_rows() const { return area.h >= 2 ? area.h - 1 : area.h; }

  void ensure_visible() {
    const int rows = item_rows();
    if (rows <= 0) {
      top = 0;
      return;
    }
    if (static_cast<int>(sel) < top) top = static_cast<int>(sel);
    if (static_cast<int>(sel) >= top + rows) top = static_cast<int>(sel) - rows + 1;
    const int n = static_cast<int>(build_visible().size());
    top = std::clamp(top, 0, std::max(0, n - rows));
  }

  void clamp_selection() {
    const std::size_t n = build_visible().size();
    if (n == 0) sel = 0;
    else if (sel >= n) sel = n - 1;
    ensure_visible();
  }

  void descend(std::size_t child) {
    path.push_back(child);
    filter.clear();
    sel = 0;
    top = 0;
    const RolltuiMenuItem& lv = level();
    if (static_cast<unsigned char>(lv.kind) == ROLLTUI_MENU_CHOICE)
      for (std::size_t i = 0; i < lv.children.size(); ++i)
        if (view(lv.children[i].id) == view(lv.value)) {
          sel = i;
          break;
        }
    ensure_visible();
  }

  bool ascend() {
    if (path.empty()) return false;
    const std::size_t was = path.back();
    path.pop_back();
    filter.clear();
    sel = std::min(was, level().children.empty() ? std::size_t{0} : level().children.size() - 1);
    top = 0;
    ensure_visible();
    return true;
  }

  std::string_view edit_text() const {
    std::size_t n = 0;
    const char* p = rolltui_input_text(edit, &n);
    return std::string_view(p, n);
  }

  void refresh_reason() {
    const RolltuiMenuItem* it = item_at(sel);
    if (!it) return;
    const Check c = run_check(it->spec, edit_text(), u);
    edit_reason = c.valid ? "" : c.reason;
  }

  void begin_edit(RolltuiMenuItem& it) {
    editing = true;
    edit_reason.clear();
    rolltui_input_set_text(edit, it.value.p, it.value.n);
    rolltui_input_select_all(edit);  // typing replaces; a first arrow key places the caret
  }

  bool try_insert(std::string_view text) {
    RolltuiMenuItem* it = item_at(sel);
    if (!it) return false;
    // What the text would be after the insertion, checked as a prefix of some valid value
    // before it lands. Never a coercion: refused or inserted.
    rolltui_input_preview_insert(edit, text.data(), text.size(), &probe);
    const Check c = run_check(it->spec, view(probe), u);
    if (!c.prefix_ok) {
      edit_reason = c.reason;
      return false;
    }
    rolltui_input_insert(edit, text.data(), text.size());
    refresh_reason();
    return true;
  }

  void step(int direction) {
    RolltuiMenuItem* it = item_at(sel);
    const unsigned char ty = it ? static_cast<unsigned char>(it->spec.type) : 0;
    if (!it || (ty != ROLLTUI_INPUT_TYPE_INT && ty != ROLLTUI_INPUT_TYPE_FLOAT)) return;
    const RolltuiInputSpec& s = it->spec;
    const int prec = ty == ROLLTUI_INPUT_TYPE_INT ? 0 : s.precision;
    const Check now = run_check(s, edit_text(), u);
    double v;
    if (now.valid && !edit_text().empty()) {
      v = std::strtod(std::string(edit_text()).c_str(), nullptr);
    } else {
      const Check committed = run_check(s, view(it->value), u);
      v = committed.valid && it->value.n ? std::strtod(it->value.c_str(), nullptr)
                                         : (s.min > -1e15 ? s.min : 0);
      const std::string t = num_text(v, prec);
      rolltui_input_set_text(edit, t.data(), t.size());
      refresh_reason();
      return;
    }
    v = std::clamp(v + direction * s.step, s.min, s.max);
    const std::string t = num_text(v, prec);
    rolltui_input_set_text(edit, t.data(), t.size());
    refresh_reason();
  }
};

// ---- lifetime ---------------------------------------------------------------------------------

extern "C" RolltuiMenu* rolltui_menu_new(RolltuiInput* editor) {
  std::unique_ptr<RolltuiMenu> m = std::make_unique<RolltuiMenu>();
  m->root.kind = RolltuiMenuItem::Kind::Submenu;
  m->edit = editor;
  m->u = rolltui_u_scratch_new();
  return m.release();
}

extern "C" void rolltui_menu_free(RolltuiMenu* m) {
  const std::unique_ptr<RolltuiMenu> owned(m);
  if (m) rolltui_u_scratch_free(m->u);
}

extern "C" void rolltui_menu_reset(RolltuiMenu* m) {
  m->path.clear();
  m->sel = 0;
  m->top = 0;
  m->filter.clear();
  m->editing = false;
  m->edit_reason.clear();
  m->palette = false;
  m->rebuild_flat();
}

extern "C" void rolltui_menu_set_root(RolltuiMenu* m, const RolltuiMenuItem* root) {
  rolltui_menu_item_copy(&m->root, root);
  m->root.kind = RolltuiMenuItem::Kind::Submenu;
  rolltui_menu_reset(m);
}

extern "C" RolltuiMenuItem* rolltui_menu_root(RolltuiMenu* m) { return &m->root; }

namespace {
RolltuiMenuItem* find_in(RolltuiMenuItem& it, std::string_view id) {
  if (view(it.id) == id) return &it;
  for (RolltuiMenuItem& c : it.children)
    if (RolltuiMenuItem* f = find_in(c, id)) return f;
  return nullptr;
}
}  // namespace

extern "C" RolltuiMenuItem* rolltui_menu_find(RolltuiMenu* m, const char* id, std::size_t len) {
  return find_in(m->root, view(id, len));
}

extern "C" int rolltui_menu_set_options(RolltuiMenu* m, const char* id, std::size_t len,
                                        const RolltuiMenuItemList* options) {
  RolltuiMenuItem* it = rolltui_menu_find(m, id, len);
  if (!it) return 0;
  it->children = *options;
  m->rebuild_flat();
  m->clamp_selection();
  return 1;
}

extern "C" void rolltui_menu_set_palette(RolltuiMenu* m, int on) {
  if (m->palette == (on != 0)) return;
  m->palette = on != 0;
  m->path.clear();
  m->sel = 0;
  m->top = 0;
  m->filter.clear();
  m->editing = false;
  m->rebuild_flat();
}

extern "C" int rolltui_menu_palette(const RolltuiMenu* m) { return m->palette ? 1 : 0; }

extern "C" void rolltui_menu_set_validator_fn(RolltuiMenu* m, RolltuiValidatorFn fn, void* ctx) {
  m->vfn = fn;
  m->vctx = ctx;
}

// ---- state accessors ---------------------------------------------------------------------------

extern "C" std::size_t rolltui_menu_path(const RolltuiMenu* m, const std::size_t** out) {
  if (out) *out = m->path.data();
  return m->path.size();
}

extern "C" const RolltuiMenuItem* rolltui_menu_level(const RolltuiMenu* m) {
  return &const_cast<RolltuiMenu*>(m)->level();
}

extern "C" std::size_t rolltui_menu_selected(const RolltuiMenu* m) { return m->sel; }

extern "C" const RolltuiMenuItem* rolltui_menu_selected_item(const RolltuiMenu* m) {
  return const_cast<RolltuiMenu*>(m)->item_at(m->sel);
}

extern "C" std::size_t rolltui_menu_visible(const RolltuiMenu* m, const std::size_t** out) {
  const std::vector<std::size_t>& v = const_cast<RolltuiMenu*>(m)->build_visible();
  if (out) *out = v.data();
  return v.size();
}

extern "C" int rolltui_menu_scroll_first(const RolltuiMenu* m) { return m->top < 0 ? 0 : m->top; }
extern "C" int rolltui_menu_scroll_visible(const RolltuiMenu* m) {
  const int r = m->item_rows();
  return r < 0 ? 0 : r;
}

extern "C" const char* rolltui_menu_filter(const RolltuiMenu* m, std::size_t* len) {
  if (len) *len = m->filter.size();
  return m->filter.c_str();
}

extern "C" int rolltui_menu_editing(const RolltuiMenu* m) { return m->editing ? 1 : 0; }

extern "C" const char* rolltui_menu_edit_reason(const RolltuiMenu* m, std::size_t* len) {
  if (len) *len = m->edit_reason.size();
  return m->edit_reason.c_str();
}

extern "C" void rolltui_menu_breadcrumb(const RolltuiMenu* m, RolltuiStr* out) {
  if (m->palette) {
    *out = m->root.label.n == 0 ? std::string("\xE2\x80\xBA")
                                : std::string(view(m->root.label)) + kCrumb + "\xE2\x80\xA6";
    return;
  }
  std::string s(view(m->root.label));
  const RolltuiMenuItem* it = &m->root;
  for (std::size_t i : m->path) {
    if (i >= it->children.size()) break;
    it = &it->children[i];
    if (!s.empty()) s += kCrumb;
    s += view(it->label);
  }
  *out = s;
}

extern "C" std::size_t rolltui_menu_flat_count(const RolltuiMenu* m) { return m->flat.size(); }

extern "C" const char* rolltui_menu_flat_label(const RolltuiMenu* m, std::size_t i, std::size_t* len) {
  if (i >= m->flat.size()) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = m->flat[i].label.size();
  return m->flat[i].label.c_str();
}

extern "C" std::size_t rolltui_menu_flat_path(const RolltuiMenu* m, std::size_t i, const std::size_t** out) {
  if (i >= m->flat.size()) {
    if (out) *out = nullptr;
    return 0;
  }
  if (out) *out = m->flat[i].path.data();
  return m->flat[i].path.size();
}

// ---- events -------------------------------------------------------------------------------------

namespace {

bool action_is(std::string_view a, const char* name) { return name && a == name; }

void act(RolltuiMenu* m, std::size_t vis_index, RolltuiMenuEvent* out) {
  RolltuiMenuItem* it = m->item_at(vis_index);
  if (!it || !it->enabled) return;
  if (m->palette) {
    const FlatEntry& fe = m->flat[m->vis[vis_index]];
    if (fe.path.size() >= 2) {
      RolltuiMenuItem* p = m->by_path(fe.path.data(), fe.path.size() - 1);
      if (p && static_cast<unsigned char>(p->kind) == ROLLTUI_MENU_CHOICE) {
        p->value = view(it->id);
        out->kind = ROLLTUI_MENU_EVENT_CHOOSE;
        out->id = view(p->id);
        out->value = view(it->id);
        return;
      }
    }
  } else if (static_cast<unsigned char>(m->level().kind) == ROLLTUI_MENU_CHOICE) {
    RolltuiMenuItem& choice = m->level();
    choice.value = view(it->id);
    out->kind = ROLLTUI_MENU_EVENT_CHOOSE;
    out->id = view(choice.id);
    out->value = view(it->id);
    m->ascend();
    return;
  }
  switch (static_cast<unsigned char>(it->kind)) {
    case ROLLTUI_MENU_ACTION:
      out->kind = ROLLTUI_MENU_EVENT_ACTIVATE;
      out->id = view(it->id);
      return;
    case ROLLTUI_MENU_TOGGLE:
      it->checked = !it->checked;
      out->kind = ROLLTUI_MENU_EVENT_TOGGLE;
      out->id = view(it->id);
      out->checked = it->checked;
      return;
    case ROLLTUI_MENU_SUBMENU:
    case ROLLTUI_MENU_CHOICE:
      if (m->palette) return;
      m->descend(m->build_visible()[vis_index]);
      return;
    case ROLLTUI_MENU_INPUT:
      m->sel = vis_index;
      m->begin_edit(*it);
      return;
    default:
      return;
  }
}

void handle_edit(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* b,
                 const RolltuiMenuActions* A, RolltuiMenuEvent* out) {
  RolltuiMenuItem* it = m->item_at(m->sel);
  if (!it) {
    m->editing = false;
    return;
  }
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    m->try_insert(view(e->text, e->text_len));
    return;
  }
  if (e->kind != ROLLTUI_EVENT_KEY) return;
  std::size_t alen = 0;
  const char* a = rolltui_bindings_action_for(b, &e->key, "edit", 4, &alen);
  const std::string_view action = a ? std::string_view(a, alen) : std::string_view();
  if (action_is(action, A->commit)) {
    Check c = run_check(it->spec, m->edit_text(), m->u);
    if (c.valid && static_cast<unsigned char>(it->spec.type) == ROLLTUI_INPUT_TYPE_TEXT &&
        it->spec.validator.n) {
      RolltuiStr why;
      const std::string_view t = m->edit_text();
      if (m->vfn && m->vfn(m->vctx, it->spec.validator.p, it->spec.validator.n, t.data(), t.size(), &why)) {
        if (why.n) {
          c.valid = false;
          c.reason = std::string(view(why));
        }
      } else {
        c.valid = false;
        c.reason = "no validator named '" + std::string(view(it->spec.validator)) + "' is registered";
      }
    }
    if (!c.valid) {
      m->edit_reason = c.reason;
      return;
    }
    it->value = c.canonical;
    m->editing = false;
    m->edit_reason.clear();
    out->kind = ROLLTUI_MENU_EVENT_INPUT;
    out->id = view(it->id);
    out->value = view(it->value);
    return;
  }
  if (action_is(action, A->cancel)) {
    m->editing = false;
    m->edit_reason.clear();
    return;
  }
  if (action_is(action, A->step_up)) {
    m->step(+1);
    return;
  }
  if (action_is(action, A->step_down)) {
    m->step(-1);
    return;
  }
  if (e->key.key == ROLLTUI_KEY_CHAR && !e->key.ctrl && !e->key.alt && e->key.ch >= 0x20 &&
      e->key.ch != 0x7F) {
    char buf[4];
    m->try_insert(std::string_view(buf, rolltui_u_append_utf8(e->key.ch, buf)));
    return;
  }
  // Everything else is the input widget's: the caret, selection, deletions, kills.
  if (rolltui_input_handle(m->edit, e, b, A->input, 0) == ROLLTUI_INPUT_HANDLED) m->refresh_reason();
}

void handle_key(RolltuiMenu* m, const RolltuiChord& k, const RolltuiBindings* b,
                const RolltuiMenuActions* A, RolltuiMenuEvent* out) {
  const bool text = k.key == ROLLTUI_KEY_CHAR && !k.ctrl && !k.alt && k.ch >= 0x20 && k.ch != 0x7F;
  if (text) {
    char buf[4];
    m->filter.append(buf, rolltui_u_append_utf8(k.ch, buf));
    m->sel = 0;
    m->top = 0;
    m->ensure_visible();
    return;
  }
  std::size_t alen = 0;
  const char* a = rolltui_bindings_action_for(b, &k, "menu", 4, &alen);
  if (!a) return;
  const std::string_view action(a, alen);
  const std::size_t n = m->build_visible().size();
  auto move_to = [&](std::size_t i) {
    if (n == 0) {
      m->sel = 0;
      return;
    }
    m->sel = std::min(i, n - 1);
    m->ensure_visible();
  };
  if (action_is(action, A->up)) return move_to(m->sel == 0 ? 0 : m->sel - 1);
  if (action_is(action, A->down)) return move_to(m->sel + 1);
  if (action_is(action, A->page_up)) {
    const std::size_t s = static_cast<std::size_t>(std::max(m->item_rows(), 1));
    return move_to(m->sel < s ? 0 : m->sel - s);
  }
  if (action_is(action, A->page_down))
    return move_to(m->sel + static_cast<std::size_t>(std::max(m->item_rows(), 1)));
  if (action_is(action, A->first)) return move_to(0);
  if (action_is(action, A->last)) return move_to(n == 0 ? 0 : n - 1);
  if (action_is(action, A->activate)) {
    act(m, m->sel, out);
    return;
  }
  if (action_is(action, A->descend)) {
    const RolltuiMenuItem* it = m->item_at(m->sel);
    const unsigned char ky = it ? static_cast<unsigned char>(it->kind) : 0;
    if (it && it->enabled && !m->palette && (ky == ROLLTUI_MENU_SUBMENU || ky == ROLLTUI_MENU_CHOICE))
      m->descend(m->build_visible()[m->sel]);
    return;
  }
  if (action_is(action, A->ascend)) {
    if (!m->filter.empty()) {
      m->filter.clear();
      m->clamp_selection();
      return;
    }
    m->ascend();
    return;
  }
  if (action_is(action, A->back)) {
    if (!m->filter.empty()) {
      m->filter.clear();
      m->clamp_selection();
      return;
    }
    if (m->ascend()) return;
    out->kind = ROLLTUI_MENU_EVENT_CLOSED;
    return;
  }
  if (action_is(action, A->erase)) {
    if (!m->filter.empty()) {
      std::vector<RolltuiUnicodeGrapheme> g(m->filter.size() + 1);
      const std::size_t gn = rolltui_u_graphemes(m->u, m->filter.data(), m->filter.size(), 0, g.data());
      if (gn) m->filter.erase(g[gn - 1].offset);
      m->clamp_selection();
    }
    return;
  }
}

void handle_mouse(RolltuiMenu* m, const RolltuiMouseEvent& e, RolltuiMenuEvent* out) {
  using K = RolltuiMouseEvent::Kind;
  if (e.kind == K::WheelUp) {
    if (m->sel > 0) {
      --m->sel;
      m->ensure_visible();
    }
    return;
  }
  if (e.kind == K::WheelDown) {
    const std::size_t n = m->build_visible().size();
    if (n && m->sel + 1 < n) {
      ++m->sel;
      m->ensure_visible();
    }
    return;
  }
  if (e.kind != K::Press || e.button != 1) return;
  if (!m->area.contains(e.x, e.y)) return;
  const int first_item_row = m->area.h >= 2 ? m->area.y + 1 : m->area.y;
  if (e.y < first_item_row) return;
  const std::size_t idx = static_cast<std::size_t>(m->top) + static_cast<std::size_t>(e.y - first_item_row);
  if (idx >= m->build_visible().size()) return;
  m->sel = idx;
  act(m, m->sel, out);
}

}  // namespace

extern "C" void rolltui_menu_handle(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* b,
                                    const RolltuiMenuActions* A, RolltuiMenuEvent* out) {
  out->kind = ROLLTUI_MENU_EVENT_NONE;
  out->checked = 0;
  out->id.clear();
  out->value.clear();
  if (m->editing) {
    handle_edit(m, e, b, A, out);
    return;
  }
  if (e->kind == ROLLTUI_EVENT_KEY) {
    handle_key(m, e->key, b, A, out);
    return;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    handle_mouse(m, e->mouse, out);
    return;
  }
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    // Pasted text goes where typed text would: the filter.
    for (std::size_t i = 0; i < e->text_len; ++i) {
      const unsigned char c = static_cast<unsigned char>(e->text[i]);
      if (c >= 0x20 && c != 0x7F) {
        RolltuiChord k;
        k.key = ROLLTUI_KEY_CHAR;
        k.ch = c;
        handle_key(m, k, b, A, out);
      }
    }
  }
}

// ---- layout and drawing -----------------------------------------------------------------------------

extern "C" void rolltui_menu_set_options_struct(RolltuiMenu* m, const RolltuiMenuOptions* o) { m->opt = *o; }
extern "C" const RolltuiMenuOptions* rolltui_menu_options(const RolltuiMenu* m) { return &m->opt; }
extern "C" void rolltui_menu_area(const RolltuiMenu* m, RolltuiRect* out) { *out = m->area; }

extern "C" void rolltui_menu_layout(RolltuiMenu* m, RolltuiRect area) {
  m->area = area;
  m->ensure_visible();
}

extern "C" int rolltui_menu_rows_for(const RolltuiMenu* m) {
  return 1 + std::max<int>(1, static_cast<int>(const_cast<RolltuiMenu*>(m)->build_visible().size()));
}

namespace {

std::string row_text(RolltuiMenu* m, const RolltuiMenuItem& it, bool in_palette, std::size_t vis_index) {
  std::string s;
  if (in_palette) {
    s = m->flat[m->vis[vis_index]].label;
    if (static_cast<unsigned char>(it.kind) == ROLLTUI_MENU_TOGGLE)
      s = std::string(it.checked ? "[x] " : "[ ] ") + s;
    return s;
  }
  switch (static_cast<unsigned char>(it.kind)) {
    case ROLLTUI_MENU_TOGGLE: s = std::string(it.checked ? "[x] " : "[ ] ") + std::string(view(it.label)); break;
    case ROLLTUI_MENU_INPUT: s = std::string(view(it.label)) + ": " + std::string(view(it.value)); break;
    default: s = std::string(view(it.label));
  }
  const RolltuiMenuItem& lv = m->level();
  if (static_cast<unsigned char>(lv.kind) == ROLLTUI_MENU_CHOICE && view(it.id) == view(lv.value))
    s = "\xE2\x80\xA2 " + s;  // • the current option
  return s;
}

int put(RolltuiFrame* f, RolltuiDrawScratch* d, int x, int y, std::string_view s, RolltuiStyle st, int max,
        int aw) {
  return rolltui_frame_put_text(f, d, x, y, s.data(), s.size(), st, max, aw, 0);
}

}  // namespace

extern "C" void rolltui_menu_draw(const RolltuiMenu* cm, RolltuiFrame* f, RolltuiDrawScratch* draw,
                                  const RolltuiStyle* styles, const RolltuiMenuRoles* roles,
                                  const RolltuiInputRoles* input_roles, int focused) {
  RolltuiMenu* m = const_cast<RolltuiMenu*>(cm);
  const RolltuiRect a = m->area;
  if (a.w <= 0 || a.h <= 0) return;
  const int aw = m->opt.ambiguous_wide;
  const int x0 = a.x + m->opt.inset, w = a.w - 2 * m->opt.inset;
  if (w <= 0) return;
  const std::vector<std::size_t> vis = m->build_visible();
  int y = a.y;
  if (a.h >= 2) {
    if (m->editing) {
      // The breadcrumb yields to the field's guidance: the reason a key or a commit was
      // refused when there is one, else the constraint.
      const RolltuiMenuItem* it = m->item_at(m->sel);
      const std::string hint = it ? hint_of(it->spec) : "";
      const std::string line = !m->edit_reason.empty() ? "\xE2\x9C\x97 " + m->edit_reason
                               : hint.empty()          ? "editing \xE2\x80\x94 Enter commits, Esc cancels"
                                                       : "editing \xE2\x80\x94 " + hint;
      put(f, draw, x0, y, line, styles[m->edit_reason.empty() ? roles->shortcut : roles->warning], w, aw);
    } else {
      RolltuiStr crumb;
      rolltui_menu_breadcrumb(m, &crumb);
      const int used = put(f, draw, x0, y, view(crumb), styles[roles->breadcrumb], w, aw);
      if (!m->filter.empty())
        put(f, draw, x0 + used, y, "  /" + m->filter, styles[roles->shortcut], std::max(w - used, 0), aw);
    }
    ++y;
  }
  const int rows = m->item_rows();
  if (vis.empty()) {
    if (rows > 0)
      put(f, draw, x0, y, m->filter.empty() ? "(empty)" : "(no match for /" + m->filter + ")",
          styles[roles->text_muted], w, aw);
    return;
  }
  for (int r = 0; r < rows; ++r) {
    const std::size_t i = static_cast<std::size_t>(m->top + r);
    if (i >= vis.size()) break;
    const RolltuiMenuItem* it = m->item_at(i);
    if (!it) break;
    const bool is_sel = i == m->sel;
    const RolltuiStyle base =
        styles[is_sel ? roles->selected : (it->enabled ? roles->item : roles->text_muted)];
    rolltui_frame_fill(f, draw, RolltuiRect{x0, y + r, w, 1}, base, nullptr, 0);
    if (is_sel && m->editing) {
      // The field: its label, then the input widget's own drawing (caret, selection).
      const int used = put(f, draw, x0, y + r, std::string(view(it->label)) + ": ", base, w, aw);
      const RolltuiRect field{x0 + used, y + r, std::max(w - used, 0), 1};
      if (field.w > 0) {
        if (rolltui_input_options(m->edit)->ambiguous_wide != static_cast<unsigned char>(aw != 0)) {
          RolltuiInputOptions o = *rolltui_input_options(m->edit);
          o.ambiguous_wide = static_cast<unsigned char>(aw != 0);
          rolltui_input_set_options(m->edit, &o);
        }
        rolltui_input_layout(m->edit, field);
        rolltui_input_draw(m->edit, f, draw, styles, input_roles, focused);
      }
      continue;
    }
    const std::string text = row_text(m, *it, m->palette, i);
    std::string right;
    const unsigned char ky = static_cast<unsigned char>(it->kind);
    if (!m->palette) {
      if (ky == ROLLTUI_MENU_CHOICE) right = std::string(view(it->value)) + " \xE2\x96\xB8";
      else if (ky == ROLLTUI_MENU_SUBMENU) right = "\xE2\x96\xB8";
      else if (it->shortcut.n) right = std::string(view(it->shortcut));
    }
    const int rw = right.empty() ? 0 : rolltui_u_display_width(m->u, right.data(), right.size(), aw);
    const int left_max = right.empty() ? w : std::max(w - rw - 1, 0);
    const int used = put(f, draw, x0, y + r, text, base, left_max, aw);
    if (rw > 0 && rw <= w) {
      const RolltuiStyle rs =
          is_sel ? base
                 : (ky == ROLLTUI_MENU_CHOICE || ky == ROLLTUI_MENU_SUBMENU ? base : styles[roles->shortcut]);
      const int rx = std::max(w - rw, used + 1);
      put(f, draw, x0 + rx, y + r, right, rs, std::max(w - rx, 0), aw);
    }
  }
  if (w >= 1 && rows >= 1) {
    if (m->top > 0)
      rolltui_frame_put(f, x0 + w - 1, y, "\xE2\x96\xB2", 3, 1, styles[roles->scroll_marker], 0);
    if (static_cast<std::size_t>(m->top + rows) < vis.size())
      rolltui_frame_put(f, x0 + w - 1, y + rows - 1, "\xE2\x96\xBC", 3, 1, styles[roles->scroll_marker], 0);
  }
}
