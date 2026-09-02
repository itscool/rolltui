//
// theme_test.cpp — Theme.hpp and Json.hpp: the JSON parser's contract, the theme
// loader's report (a missing role is inherited AND named exactly once; an unknown key
// is named; a bad value is named), the three built-ins, the colour-downgrade table,
// SGR golden strings, depth detection, the dump/load round trip — and the grep
// control: no colour literal exists in the library outside Theme.cpp's built-ins.
//
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>

#include "rolltui/Json.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

namespace {

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
    json::Value v = json::parse(R"({"a": 1, "b": [true, null, "xé\n"], "c": {"d": -2.5e1}})", err);
    check(err.empty() && v.is_object(), "parse an object: " + err);
    check(v["a"].as_number() == 1 && v["c"]["d"].as_number() == -25, "numbers, nested lookup");
    check(v["b"].is_array() && v["b"].arr.size() == 3 && v["b"].arr[0].as_bool() &&
              v["b"].arr[1].is_null() && v["b"].arr[2].as_string() == "x\xC3\xA9\n",
          "arrays, booleans, null, escapes incl. \\u");
    check(v["missing"].is_null() && v["missing"]["deeper"].is_null(), "missing keys are Null and chainable");
    check(json::dump(v, 0) == "{\"a\":1,\"b\":[true,null,\"x\xC3\xA9\\n\"],\"c\":{\"d\":-25}}",
          "dump: compact, ordered, escaped: " + json::dump(v, 0));
    json::Value again = json::parse(json::dump(v, 2), err);
    check(err.empty() && json::dump(again, 0) == json::dump(v, 0), "dump → parse round-trips");
    json::parse("{\"a\": 1,\n \"a\": 2}", err);
    check(err == "line 2: duplicate key \"a\"", "duplicate key is an error with its line: " + err);
    json::parse("{\"a\": [1, 2}", err);
    check(err.rfind("line 1: expected ',' or ']'", 0) == 0, "malformed array names the line: " + err);
    json::parse("[1] x", err);
    check(!err.empty(), "trailing garbage is an error: " + err);
    json::parse("\"\\ud83d\\ude00\"", err);
    check(err.empty(), "surrogate pair escape accepted");
    check(json::parse("\"\\ud83d\\ude00\"", err).as_string() == "\xF0\x9F\x98\x80", "…and decodes to U+1F600");
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
    for (const Style& s : mono->styles) mono_colourless &= (s.fg == Color::none() && s.bg == Color::none());
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

  // ---- the grep control: no colour literal outside Theme.cpp ---------------------
  {
    std::string dir = ROLLTUI_SOURCE_DIR;
    std::vector<std::string> files;
    if (DIR* d = opendir(dir.c_str())) {
      while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 4 && (n.substr(n.size() - 4) == ".hpp" || n.substr(n.size() - 4) == ".cpp")) files.push_back(n);
      }
      closedir(d);
    }
    check(files.size() >= 10, "scanned the library sources (" + std::to_string(files.size()) + " files)");
    // Literal colours: rgb()/indexed() constructors, "#rrggbb" strings, and raw SGR
    // colour parameters (30-37, 40-47, 90-97, 100-107, 38;5, 48;5, 38;2, 48;2).
    std::regex literal(R"(Color::rgb\(|Color::indexed\(|"#[0-9a-fA-F]{6}"|\[(3[0-7]|4[0-7]|9[0-7]|10[0-7]|38;5|48;5|38;2|48;2)(;|m))");
    std::vector<std::string> offenders;
    for (const std::string& f : files) {
      if (f == "Theme.cpp" || f == "Style.hpp") continue;  // the definitions, and the constructors themselves
      if (f == "ThemeAnalysis.cpp" || f == "ThemeGen.cpp") continue;  // colour MATHS: they construct colours from numbers they computed, never name one
      std::string src = read_file(dir + "/" + f);
      std::istringstream in(src);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 2, "//") == 0) continue;  // a comment is not code
        if (std::regex_search(line, literal)) offenders.push_back(f + ":" + std::to_string(ln) + ": " + line);
      }
    }
    check(offenders.empty(), "no colour literal outside Theme.cpp" + (offenders.empty() ? "" : " — " + join(offenders)));
    // …and the control can see one: Theme.cpp itself must trip the pattern.
    check(std::regex_search(read_file(dir + "/Theme.cpp"), literal), "the pattern matches Theme.cpp's built-ins (the control is live)");
  }

  return report("rolltui theme_test");
}
