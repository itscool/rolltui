//
// theme_test.cpp — Theme.hpp and Json.hpp: the JSON parser's contract, the theme
// loader's report (a missing role is inherited AND named exactly once; an unknown key
// is named; a bad value is named), the three built-ins, the colour-downgrade table,
// SGR golden strings, depth detection, the dump/load round trip — and the grep
// control: no colour literal exists in the library outside Theme.cpp's built-ins.
//
#include <array>
#include <fstream>
#include <cstring>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

namespace {

// ---- mirrors rolltui::Role (Style.hpp): NO C form exists for it at all -- "the role NAME
// vocabulary stays C++ on purpose" (rolltui/rolltui.h) -- reproduced in Style.hpp's exact
// declaration order, since a theme's style table is indexed by this ordinal. ----
enum class Role : unsigned char {
  // base
  text, text_muted, background, panel_background, border, border_active, title,
  label, value, accent_1, accent_2, accent_3, accent_4, prompt, note, warning, error,
  // markdown
  md_heading, md_emphasis, md_strong, md_code_inline, md_code_block, md_code_label,
  md_link, md_link_url, md_quote, md_list_marker, md_table_border, md_table_header,
  md_rule, md_strikethrough,
  // diffs; the _word pair is the CHANGED RUN inside a -/+ line pair (Phase 12 m5b)
  diff_added, diff_removed, diff_context, diff_added_word, diff_removed_word,
  // chrome
  input_text, input_cursor, input_placeholder, scroll_marker, selection, overlay,
  menu_item, menu_selected, menu_breadcrumb, menu_shortcut,
  // find (Phase 12 m4): every match, and the one the view is on
  find_match, find_current,
  // the scrollbar thumb (Phase 12 m5); its TRACK is the window's own border
  scrollbar,
  count_
};
constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::count_);
static_assert(static_cast<unsigned char>(Role::text) == ROLLTUI_ROLE_DEFAULT_TEXT,
              "the C side's default entry role must be Role::text");
static_assert(static_cast<unsigned char>(Role::background) == ROLLTUI_ROLE_DEFAULT_BACKGROUND,
              "the C side's default node background must be Role::background");
static_assert(static_cast<unsigned char>(Role::prompt) == ROLLTUI_ROLE_DEFAULT_PROMPT,
              "the C side's default input prompt role must be Role::prompt");

// The role NAMES, in the same order, for the vocab table `load_theme` hands the C loader --
// Style.hpp's own kRoleNames, reproduced (a theme file resolves "md_heading" against this).
constexpr std::array<const char*, kRoleCount> kRoleNames = {
    "text", "text_muted", "background", "panel_background", "border", "border_active",
    "title", "label", "value", "accent_1", "accent_2", "accent_3", "accent_4", "prompt",
    "note", "warning", "error",
    "md_heading", "md_emphasis", "md_strong", "md_code_inline", "md_code_block",
    "md_code_label", "md_link", "md_link_url", "md_quote", "md_list_marker",
    "md_table_border", "md_table_header", "md_rule", "md_strikethrough",
    "diff_added", "diff_removed", "diff_context", "diff_added_word", "diff_removed_word",
    "input_text", "input_cursor", "input_placeholder", "scroll_marker", "selection",
    "overlay", "menu_item", "menu_selected", "menu_breadcrumb", "menu_shortcut",
    "find_match", "find_current", "scrollbar",
};

// `rolltui::Color`/`rolltui::Style` (Style.hpp) were one-definition aliases over the same C
// structs -- reproduced verbatim; there was never a second definition to convert away from.
using Color = RolltuiStyleColor;
using Style = RolltuiStyle;

// `rolltui::ColorDepth`/`rolltui::ThemeMode` (Theme.hpp): no C form either -- the loader and
// the colour engine both take the ordinal as a raw byte/int, never a name.
enum class ColorDepth : unsigned char { Mono, Ansi16, Ansi256, TrueColor };
enum class ThemeMode : unsigned char { Dark, Light };

// ---- mirrors rolltui::Theme (Theme.hpp), but only the STYLE TABLE and NAME this file reads
// -- `.meta` (json::Value) and `.effects` (EffectMap) are C++-only shim types this fixture
// never touches (no theme text used below has an "effects" key or a "meta" object). ----
struct Theme {
  std::string name;
  std::array<RolltuiStyle, kRoleCount> styles{};
  const RolltuiStyle& style(Role r) const {
    return *rolltui_theme_style(styles.data(), styles.size(), static_cast<unsigned char>(r));
  }
};

struct ThemeLoadReport {
  std::string error;
  std::vector<std::string> missing_roles;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && missing_roles.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// Mirrors rolltui::builtin_theme's cache (Theme.cpp), minus `.effects`/`.meta`: filled once
// from the C built-ins, keyed by name.
const Theme* builtin_theme(std::string_view name) {
  static const std::vector<std::pair<std::string, Theme>> cache = [] {
    std::vector<std::pair<std::string, Theme>> v;
    const std::size_t n = rolltui_theme_builtin_count();
    v.reserve(n);  // pointer stability: builtin_theme() hands back &t into this vector
    for (std::size_t i = 0; i < n; ++i) {
      const char* nm = rolltui_theme_builtin_name(i);
      Theme t;
      t.name = nm;
      if (RolltuiEffectMap* m = rolltui_theme_builtin_fill(nm, std::strlen(nm), t.styles.data(), t.styles.size()))
        rolltui_effect_map_free(m);  // this fixture never reads effects
      v.emplace_back(nm, t);
    }
    return v;
  }();
  for (const auto& [n, t] : cache)
    if (n == name) return &t;
  return nullptr;
}
std::vector<std::string_view> builtin_theme_names() {
  std::vector<std::string_view> out;
  const std::size_t n = rolltui_theme_builtin_count();
  for (std::size_t i = 0; i < n; ++i) out.push_back(rolltui_theme_builtin_name(i));
  return out;
}

// Mirrors Theme.cpp's theme_vocab(): the role NAME table handed to the C loader/dumper once
// per call. No effect-state names: no theme text below has an "effects" key, so state_count
// 0 is never consulted.
const RolltuiThemeVocab& theme_vocab() {
  static const RolltuiThemeVocab v = [] {
    RolltuiThemeVocab t{};
    t.role_names = kRoleNames.data();
    t.role_count = kRoleCount;
    t.text_role = static_cast<std::size_t>(Role::text);
    t.state_names = nullptr;
    t.state_count = 0;
    t.fallback_effect_role = static_cast<unsigned char>(Role::accent_1);
    return t;
  }();
  return v;
}

// Mirrors rolltui::load_theme(string_view, ThemeMode, ThemeLoadReport&) (Theme.cpp): parse,
// then hand the tree to the C loader, then translate its report field for field.
std::optional<Theme> load_theme(std::string_view json_text, ThemeMode mode, ThemeLoadReport& report) {
  report = ThemeLoadReport{};
  RolltuiStr err{};
  RolltuiJsonValue* root_c = rolltui_json_parse(json_text.data(), json_text.size(), &err);
  if (!root_c) {
    report.error = err.str();
    rolltui_str_free(&err);
    return std::nullopt;
  }
  rolltui_str_free(&err);
  Theme t;
  RolltuiStr name{};
  RolltuiThemeReport rep{};
  RolltuiEffectMap* eff = rolltui_theme_load(root_c, static_cast<int>(mode), &theme_vocab(), t.styles.data(), &name, &rep);
  rolltui_json_free(root_c);
  report.error = rep.error.str();
  for (std::size_t i = 0; i < rep.missing_roles_n; ++i) report.missing_roles.push_back(rep.missing_roles[i].str());
  for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) report.unknown_keys.push_back(rep.unknown_keys[i].str());
  for (std::size_t i = 0; i < rep.bad_values_n; ++i) report.bad_values.push_back(rep.bad_values[i].str());
  rolltui_theme_report_release(&rep);
  if (!eff) {
    rolltui_str_free(&name);
    return std::nullopt;
  }
  rolltui_effect_map_free(eff);  // this fixture never reads effects
  t.name = name.str();
  rolltui_str_free(&name);
  return t;
}

// ---- mirrors rolltui::json::Value (Json.hpp): an OWNED tree over the C API, since Value's
// own C++ shape (real std::string/std::vector members other call sites need) is explicitly
// NOT ported to C -- rolltui_json.h's own header comment says so. This fixture only ever
// reads a tree it parsed or built itself, so an owned-handle mirror with the same method
// names is enough. ----
class Value {
 public:
  Value() : v_(rolltui_json_null()) {}
  explicit Value(RolltuiJsonValue* owned) : v_(owned ? owned : rolltui_json_null()) {}
  Value(const Value& o) : v_(rolltui_json_clone(o.v_)) {}
  Value& operator=(const Value& o) {
    if (this != &o) {
      rolltui_json_free(v_);
      v_ = rolltui_json_clone(o.v_);
    }
    return *this;
  }
  Value(Value&& o) noexcept : v_(o.v_) { o.v_ = nullptr; }
  Value& operator=(Value&& o) noexcept {
    if (this != &o) {
      rolltui_json_free(v_);
      v_ = o.v_;
      o.v_ = nullptr;
    }
    return *this;
  }
  ~Value() { rolltui_json_free(v_); }

  bool is_null() const { return rolltui_json_is_null(v_) != 0; }
  bool is_bool() const { return rolltui_json_is_bool(v_) != 0; }
  bool is_number() const { return rolltui_json_is_number(v_) != 0; }
  bool is_string() const { return rolltui_json_is_string(v_) != 0; }
  bool is_array() const { return rolltui_json_is_array(v_) != 0; }
  bool is_object() const { return rolltui_json_is_object(v_) != 0; }

  double as_number(double def = 0) const { return rolltui_json_as_number(v_, def); }
  bool as_bool(bool def = false) const { return rolltui_json_as_bool(v_, def) != 0; }
  std::string as_string(std::string_view def = "") const {
    std::size_t n = 0;
    const char* p = rolltui_json_as_string(v_, def.data(), def.size(), &n);
    return std::string(p, n);
  }

  // A CLONE of the BORROW rolltui_json_get hands back, so the result outlives the parent
  // expression -- simpler than tracking a borrow's window, and cheap: these are small trees.
  Value get(std::string_view key) const { return Value(rolltui_json_clone(rolltui_json_get(v_, key.data(), key.size()))); }
  Value operator[](std::string_view key) const { return get(key); }

  std::size_t array_size() const { return rolltui_json_array_size(v_); }
  Value array_at(std::size_t i) const { return Value(rolltui_json_clone(rolltui_json_array_at(v_, i))); }

  // TAKES OWNERSHIP of child, matching rolltui_json_set exactly (always turns this into an
  // object, even if it was something else).
  Value& set(std::string_view key, Value child) {
    rolltui_json_set(v_, key.data(), key.size(), child.release());
    return *this;
  }

  const RolltuiJsonValue* handle() const { return v_; }
  // Hands ownership of the underlying tree to the caller; this Value keeps a fresh Null.
  RolltuiJsonValue* release() {
    RolltuiJsonValue* p = v_;
    v_ = rolltui_json_null();
    return p;
  }

 private:
  RolltuiJsonValue* v_;
};

// Mirrors rolltui::json::parse (Json.cpp) exactly: parse to a C tree, translate the error,
// and hand the tree over (Null on failure).
Value parse(std::string_view text, std::string& error) {
  RolltuiStr err{};
  RolltuiJsonValue* v = rolltui_json_parse(text.data(), text.size(), &err);
  error = err.str();
  rolltui_str_free(&err);
  return Value(v);
}
// Mirrors rolltui::json::dump (Json.cpp).
std::string dump(const Value& v, int indent = 2) {
  RolltuiStr out{};
  rolltui_json_dump(v.handle(), indent, &out);
  std::string result = out.str();
  rolltui_str_free(&out);
  return result;
}

// Mirrors rolltui::theme_to_json (Theme.cpp), minus the "effects"/"meta" this fixture's
// themes never carry.
std::string theme_to_json(const Theme& theme) {
  Value root(rolltui_json_object());
  root.set("name", Value(rolltui_json_string(theme.name.data(), theme.name.size())));
  RolltuiJsonValue* c = rolltui_theme_dump(theme.styles.data(), nullptr, nullptr, nullptr, &theme_vocab());
  root.set("roles", Value(rolltui_json_clone(rolltui_json_get(c, "roles", 5))));
  rolltui_json_free(c);
  return dump(root, 2) + "\n";
}

// Mirrors Theme.cpp's colour/depth functions exactly, over the same C calls.
Color downgrade(Color c, ColorDepth depth) {
  rolltui_color_downgrade(&c, static_cast<unsigned char>(depth));
  return c;
}
std::string sgr(const Style& style, ColorDepth depth) {
  char buf[ROLLTUI_SGR_MAX];
  const std::size_t n = rolltui_sgr(&style, static_cast<unsigned char>(depth), buf, sizeof buf);
  return std::string(buf, n);
}
std::string color_to_string(Color c) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  const std::size_t n = rolltui_color_to_string(c, buf, sizeof buf);
  return std::string(buf, n);
}
ColorDepth detect_color_depth(const char* colorterm, const char* term, const char* force) {
  std::string_view f = force ? force : "";
  if (f == "truecolor" || f == "24bit") return ColorDepth::TrueColor;
  if (f == "256") return ColorDepth::Ansi256;
  if (f == "16") return ColorDepth::Ansi16;
  if (f == "mono") return ColorDepth::Mono;
  std::string_view ct = colorterm ? colorterm : "";
  std::string_view t = term ? term : "";
  if (ct == "truecolor" || ct == "24bit") return ColorDepth::TrueColor;
  if (t.find("256color") != std::string_view::npos) return ColorDepth::Ansi256;
  if (t.empty() || t == "dumb") return ColorDepth::Mono;
  return ColorDepth::Ansi16;
}
std::string_view color_depth_name(ColorDepth d) {
  switch (d) {
    case ColorDepth::Mono: return "mono";
    case ColorDepth::Ansi16: return "16";
    case ColorDepth::Ansi256: return "256";
    case ColorDepth::TrueColor: return "truecolor";
  }
  return "mono";
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
  return s;
}

bool contains(const std::vector<std::string>& v, const std::string& x) {
  for (const std::string& y : v) if (y == x) return true;
  return false;
}

std::string visible(std::string s) {  // "\x1b[..." → "ESC[..." for readable failures
  std::string o;
  for (char c : s) o += (c == '\x1b') ? std::string("ESC") : std::string(1, c);
  return o;
}

}  // namespace

int main() {
  // ---- JSON ----------------------------------------------------------------------
  {
    std::string err;
    Value v = parse(R"({"a": 1, "b": [true, null, "xé\n"], "c": {"d": -2.5e1}})", err);
    check(err.empty() && v.is_object(), "parse an object: " + err);
    check(v["a"].as_number() == 1 && v["c"]["d"].as_number() == -25, "numbers, nested lookup");
    check(v["b"].is_array() && v["b"].array_size() == 3 && v["b"].array_at(0).as_bool() &&
              v["b"].array_at(1).is_null() && v["b"].array_at(2).as_string() == "x\xC3\xA9\n",
          "arrays, booleans, null, escapes incl. \\u");
    check(v["missing"].is_null() && v["missing"]["deeper"].is_null(), "missing keys are Null and chainable");
    check(dump(v, 0) == "{\"a\":1,\"b\":[true,null,\"x\xC3\xA9\\n\"],\"c\":{\"d\":-25}}",
          "dump: compact, ordered, escaped: " + dump(v, 0));
    Value again = parse(dump(v, 2), err);
    check(err.empty() && dump(again, 0) == dump(v, 0), "dump → parse round-trips");
    parse("{\"a\": 1,\n \"a\": 2}", err);
    check(err == "line 2: duplicate key \"a\"", "duplicate key is an error with its line: " + err);
    parse("{\"a\": [1, 2}", err);
    check(err.rfind("line 1: expected ',' or ']'", 0) == 0, "malformed array names the line: " + err);
    parse("[1] x", err);
    check(!err.empty(), "trailing garbage is an error: " + err);
    parse("\"\\ud83d\\ude00\"", err);
    check(err.empty(), "surrogate pair escape accepted");
    check(parse("\"\\ud83d\\ude00\"", err).as_string() == "\xF0\x9F\x98\x80", "…and decodes to U+1F600");
  }

  // ---- built-ins -----------------------------------------------------------------
  {
    const Theme* dark = builtin_theme("default-dark");
    const Theme* light = builtin_theme("default-light");
    const Theme* mono = builtin_theme("mono");
    check(dark && light && mono && !builtin_theme("nope"), "three built-ins, unknown name is null");
    check(builtin_theme_names().size() == 3, "names list");
    check(dark->style(Role::background).bg != light->style(Role::background).bg, "dark and light differ in background");
    bool mono_colourless = true;
    for (std::size_t i = 0; i < kRoleCount; ++i) {
      const RolltuiStyle* s = rolltui_theme_style(mono->styles.data(), kRoleCount, i);
      mono_colourless &= (s->fg == Color::none() && s->bg == Color::none());
    }
    check(mono_colourless, "mono uses no colour at all");
    check(mono->style(Role::md_strong).bold && mono->style(Role::error).reverse, "mono still distinguishes by attribute");
    check(dark->style(Role::md_strong).bold && dark->style(Role::md_emphasis).italic && dark->style(Role::md_link).underline,
          "dark: strong is bold, emphasis italic, links underlined");
  }

  // ---- loader --------------------------------------------------------------------
  {
    ThemeLoadReport rep;
    auto t = load_theme(R"({
      "name": "t",
      "defs": { "fg": "#112233", "bg": "#000000", "accent": { "dark": "#ff0000", "light": 21 } },
      "roles": {
        "text": { "fg": "fg", "bg": "bg" },
        "md_heading": { "fg": "accent", "bold": true },
        "warning": { "fg": 214 },
        "border": { "fg": "none" },
        "md_code_block": { "bg": { "dark": "#1d2021", "light": "#f2e5bc" } }
      }
    })", ThemeMode::Dark, rep);
    check(t.has_value() && rep.error.empty(), "loads: " + rep.error);
    check(t->name == "t", "name");
    check(t->style(Role::text).fg == Color::rgb(0x11, 0x22, 0x33), "defs reference resolves");
    check(t->style(Role::md_heading).fg == Color::rgb(0xff, 0, 0) && t->style(Role::md_heading).bold &&
              t->style(Role::md_heading).bg == Color::rgb(0, 0, 0),
          "dark variant chosen, unspecified bg inherited from text");
    check(t->style(Role::warning).fg == Color::indexed(214), "integer index");
    check(t->style(Role::border).fg == Color::none(), "\"none\"");
    check(t->style(Role::md_code_block).bg == Color::rgb(0x1d, 0x20, 0x21), "dark/light object on a field");
    check(t->style(Role::md_emphasis) == t->style(Role::text), "a missing role inherits text whole");
    check(rep.missing_roles.size() == kRoleCount - 5, "every missing role is reported once (" +
                                                          std::to_string(rep.missing_roles.size()) + ")");
    check(contains(rep.missing_roles, "md_emphasis") && !contains(rep.missing_roles, "text"), "…by name");
    check(rep.unknown_keys.empty() && rep.bad_values.empty(), "nothing else reported: " + join(rep.unknown_keys) + join(rep.bad_values));

    ThemeLoadReport rep2;
    auto l = load_theme(R"({"roles": {"text": {"fg": "#ffffff"}, "md_heading": {"fg": {"dark": "#ff0000", "light": 21}}}})",
                        ThemeMode::Light, rep2);
    check(l && l->style(Role::md_heading).fg == Color::indexed(21), "light mode picks the light variant");

    ThemeLoadReport rep3;
    auto b = load_theme(R"({"roles": {"text": {"fg": "orange", "colour": 1, "bold": "yes"}, "md_headng": {}}, "extra": 1})",
                        ThemeMode::Dark, rep3);
    check(b.has_value(), "a theme with problems still loads");
    check(contains(rep3.bad_values, "roles.text.fg: 'orange' is not a colour (#rrggbb, 0-255, none, or a defs name)"),
          "bad colour named: " + join(rep3.bad_values));
    check(contains(rep3.unknown_keys, "roles.text.colour") && contains(rep3.unknown_keys, "roles.md_headng") &&
              contains(rep3.unknown_keys, "extra"),
          "unknown keys named: " + join(rep3.unknown_keys));
    check(contains(rep3.bad_values, "roles.text.bold: expected true or false"), "bad attribute named");

    ThemeLoadReport rep4;
    check(!load_theme("{", ThemeMode::Dark, rep4) && !rep4.error.empty(), "unparseable JSON: error, no theme: " + rep4.error);
    ThemeLoadReport rep5;
    check(!load_theme("{\"name\": \"x\"}", ThemeMode::Dark, rep5) && rep5.error.find("roles") != std::string::npos,
          "no roles object: error");
  }

  // ---- round trip ----------------------------------------------------------------
  {
    const Theme& dark = *builtin_theme("default-dark");
    std::string text = theme_to_json(dark);
    ThemeLoadReport rep;
    auto back = load_theme(text, ThemeMode::Dark, rep);
    check(back.has_value() && rep.clean(), "dumped built-in loads clean: " + join(rep.missing_roles) + join(rep.bad_values));
    check(back && back->styles == dark.styles && back->name == dark.name, "…and is identical");
  }

  // ---- downgrade table -----------------------------------------------------------
  {
    struct Case { Color in; ColorDepth depth; Color want; const char* why; };
    const Case cases[] = {
        {Color::rgb(0, 0, 0), ColorDepth::Ansi256, Color::indexed(16), "black → cube 16"},
        {Color::rgb(255, 255, 255), ColorDepth::Ansi256, Color::indexed(231), "white → cube 231"},
        {Color::rgb(255, 0, 0), ColorDepth::Ansi256, Color::indexed(196), "red → cube 196"},
        {Color::rgb(128, 128, 128), ColorDepth::Ansi256, Color::indexed(244), "50% grey → ramp 244"},
        {Color::rgb(0, 0, 0), ColorDepth::Ansi16, Color::indexed(0), "black → 0"},
        {Color::rgb(255, 255, 255), ColorDepth::Ansi16, Color::indexed(15), "white → 15"},
        {Color::rgb(255, 0, 0), ColorDepth::Ansi16, Color::indexed(9), "red → bright red 9"},
        {Color::rgb(0, 0, 200), ColorDepth::Ansi16, Color::indexed(4), "blue → 4"},
        {Color::indexed(196), ColorDepth::Ansi16, Color::indexed(9), "index 196 → 9"},
        {Color::indexed(3), ColorDepth::Ansi16, Color::indexed(3), "a system index is kept"},
        {Color::rgb(255, 0, 0), ColorDepth::TrueColor, Color::rgb(255, 0, 0), "truecolor keeps rgb"},
        {Color::indexed(196), ColorDepth::TrueColor, Color::indexed(196), "truecolor keeps an index"},
        {Color::rgb(255, 0, 0), ColorDepth::Mono, Color::none(), "mono drops colour"},
        {Color::none(), ColorDepth::Ansi16, Color::none(), "none stays none"},
    };
    for (const Case& c : cases) {
      Color got = downgrade(c.in, c.depth);
      check(got == c.want, std::string("downgrade: ") + c.why + " (got " + color_to_string(got) + ")");
    }
  }

  // ---- SGR golden strings --------------------------------------------------------
  {
    Style s;
    s.fg = Color::rgb(1, 2, 3);
    s.bg = Color::indexed(21);  // cube blue (0,0,255) → system blue 4 at 16 colours
    s.bold = true;
    s.underline = true;
    check(sgr(s, ColorDepth::TrueColor) == "\x1b[0;1;4;38;2;1;2;3;48;5;21m", "truecolor SGR: " + visible(sgr(s, ColorDepth::TrueColor)));
    check(sgr(s, ColorDepth::Ansi256) == "\x1b[0;1;4;38;5;16;48;5;21m", "256 SGR downgrades the rgb: " + visible(sgr(s, ColorDepth::Ansi256)));
    check(sgr(s, ColorDepth::Ansi16) == "\x1b[0;1;4;30;44m", "16 SGR uses 30-37/40-47: " + visible(sgr(s, ColorDepth::Ansi16)));
    check(sgr(s, ColorDepth::Mono) == "\x1b[0;1;4m", "mono SGR keeps attributes only");
    Style bright;
    bright.fg = Color::indexed(9);
    bright.bg = Color::indexed(12);
    check(sgr(bright, ColorDepth::Ansi16) == "\x1b[0;91;104m", "bright system colours use 90-97/100-107");
    check(sgr(Style{}, ColorDepth::TrueColor) == "\x1b[0m", "empty style is a plain reset");
  }

  // ---- depth detection -----------------------------------------------------------
  {
    check(detect_color_depth("truecolor", "xterm", nullptr) == ColorDepth::TrueColor, "COLORTERM=truecolor");
    check(detect_color_depth("24bit", "xterm", nullptr) == ColorDepth::TrueColor, "COLORTERM=24bit");
    check(detect_color_depth(nullptr, "xterm-256color", nullptr) == ColorDepth::Ansi256, "TERM=*256color");
    check(detect_color_depth(nullptr, "xterm", nullptr) == ColorDepth::Ansi16, "plain TERM → 16");
    check(detect_color_depth(nullptr, "dumb", nullptr) == ColorDepth::Mono, "TERM=dumb → mono");
    check(detect_color_depth(nullptr, nullptr, nullptr) == ColorDepth::Mono, "no TERM → mono");
    check(detect_color_depth("truecolor", "xterm-256color", "16") == ColorDepth::Ansi16, "ROLL_COLOR_DEPTH override wins");
    check(detect_color_depth("truecolor", "xterm", "bogus") == ColorDepth::TrueColor, "an invalid override is ignored");
    check(color_depth_name(ColorDepth::Ansi256) == "256", "depth names");
  }

  // ---- the grep control: no colour literal outside the theme's own files ------------
  //
  // WIDENED 2026-09-04 (Phase 15 m3), because the port would otherwise have walked the
  // palette out from under it: `rolltui/c/` was never scanned at all, so a colour moved
  // into a C file would have left the check green while meaning less. It scans both now,
  // and the exempt files are named with what each of them is.
  //
  // NARROWED THE SAME DAY (Phase 15 m5): the three built-in themes' own colours moved from
  // `Theme.cpp` into `c/rolltui_theme.c` alongside the colour engine m3 already put there, so
  // `Theme.cpp` is no longer exempted (it carries no colour literal to hide any more — the
  // pattern no longer matches it at all) and the file below stands for BOTH reasons at once,
  // named separately so either one going stale is caught on its own.
  {
    std::string dir = ROLLTUI_SOURCE_DIR;
    std::vector<std::string> files;
    auto scan = [&](const std::string& sub, const char* ext_a, const char* ext_b) {
      if (DIR* d = opendir((dir + sub).c_str())) {
        while (dirent* e = readdir(d)) {
          std::string n = e->d_name;
          const std::size_t la = std::strlen(ext_a), lb = std::strlen(ext_b);
          if ((n.size() > la && n.substr(n.size() - la) == ext_a) || (n.size() > lb && n.substr(n.size() - lb) == ext_b))
            files.push_back(sub.empty() ? n : sub.substr(1) + "/" + n);
        }
        closedir(d);
      }
    };
    scan("", ".hpp", ".cpp");
    scan("/c", ".h", ".c");
    check(files.size() >= 10, "scanned the library sources, C and C++ (" + std::to_string(files.size()) + " files)");
    // Literal colours: rgb()/indexed() constructors, "#rrggbb" strings, and raw SGR
    // colour parameters (30-37, 40-47, 90-97, 100-107, 38;5, 48;5, 38;2, 48;2).
    std::regex literal(R"(Color::rgb\(|Color::indexed\(|"#[0-9a-fA-F]{6}"|\[(3[0-7]|4[0-7]|9[0-7]|10[0-7]|38;5|48;5|38;2|48;2)(;|m))");
    std::vector<std::string> offenders;
    for (const std::string& f : files) {
      if (f == "Style.hpp") continue;  // the constructors themselves (matched only in a comment)
      // c/rolltui_theme.c carries TWO exemptions now, named separately so either going stale
      // is its own failure:
      //   1. THE COLOUR ENGINE (Phase 15 m3): xterm's published 16-colour palette, which the
      //      downgrade measures against, plus the constructors it builds a reduced colour
      //      with — a reference table and computed colours are not a theme naming one. Was
      //      TWO files until 2026-09-04, when the C++ implementation was deleted and the C
      //      became the library.
      //   2. THE BUILT-IN THEMES (Phase 15 m5, the same day): `rolltui::Theme.cpp`'s own
      //      taste — `make_default_dark`/`_light`/`make_mono`'s colour literals — moved here
      //      too, so `Theme.cpp` no longer needs (or gets) an exemption of its own; the
      //      pattern below no longer matches it at all, which is asserted rather than
      //      assumed a few lines down.
      // Both are named explicitly so a table quietly moved out of either fails the liveness
      // checks under this loop.
      if (f == "c/rolltui_theme.c") continue;
      if (f == "ThemeAnalysis.cpp" || f == "ThemeGen.cpp") continue;  // colour MATHS: they construct colours from numbers they computed, never name one
      std::string src = read_file(dir + "/" + f);
      std::istringstream in(src);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && (line.compare(first, 2, "//") == 0 || line.compare(first, 2, "/*") == 0 ||
                                           line.compare(first, 1, "*") == 0))
          continue;  // a comment is not code, in either language's spelling
        if (std::regex_search(line, literal)) offenders.push_back(f + ":" + std::to_string(ln) + ": " + line);
      }
    }
    check(offenders.empty(), "no colour literal outside the theme's own files" + (offenders.empty() ? "" : " — " + join(offenders)));
    // NEITHER half of c/rolltui_theme.c's exemption can be proven LIVE by asking `literal`
    // to match it, and that is worth stating rather than leaving as a silent gap: the
    // pattern is shaped for C++ call syntax (`Color::rgb(`, `Color::indexed(`) and a raw SGR
    // parameter written byte-by-byte, and C has neither — the colour engine's own
    // `kSystem16` is plain `{r, g, b}` struct literals and the built-in themes below are
    // `rgbc(0x.., 0x.., 0x..)` calls with no namespace to spell. That is exactly why the two
    // liveness checks below are SUBSTRING searches, the same shape the pre-existing
    // `kSystem16` one already used, rather than a second attempt to make `literal` see C: a
    // regex that matched both languages' spellings of "a colour literal" would be looser in
    // the C++ files this control actually polices, which is the trade the control is FOR.
    //
    // Theme.cpp, by contrast, IS still C++, so the pattern finding nothing there any more is
    // exactly the assertion this control can make honestly — the built-ins really left.
    check(!std::regex_search(read_file(dir + "/Theme.cpp"), literal),
          "the pattern no longer matches Theme.cpp — the built-ins really left");
    // …and neither exemption is an empty one. Each file must still carry what it is exempt
    // for, so a table quietly moved somewhere unscanned fails here instead of passing
    // everywhere — this is why the c/rolltui_theme.c exemption could not go stale silently
    // when the second implementation was deleted (2026-09-04): it failed on the first run
    // afterwards, and the same is now true of the built-ins' own move.
    check(read_file(dir + "/c/rolltui_theme.c").find("kSystem16") != std::string::npos,
          "the colour engine still carries the palette it is exempt for");
    check(read_file(dir + "/c/rolltui_theme.c").find("default-dark") != std::string::npos,
          "the built-in themes still carry the name that proves they live here now");
  }

  return report("rolltui theme_test");
}
