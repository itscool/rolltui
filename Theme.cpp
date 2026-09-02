// rolltui/Theme.cpp — the built-in themes (the ONLY place in the library a colour
// literal may appear — rolltui/tests/theme_test.cpp greps for that), the JSON loader,
// colour downgrade and SGR emission. See Theme.hpp.
#include "rolltui/Theme.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Json.hpp"

namespace rolltui {

// ---- built-ins ---------------------------------------------------------------------

namespace {

constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) { return Color::rgb(r, g, b); }

Style S(Color fg, Color bg = Color::none(), bool bold = false, bool italic = false,
        bool underline = false, bool dim = false, bool reverse = false) {
  Style s;
  s.fg = fg; s.bg = bg; s.bold = bold; s.italic = italic; s.underline = underline; s.dim = dim; s.reverse = reverse;
  return s;
}

Theme make_default_dark() {
  // A restrained palette: text on a near-black ground, four accents, muted chrome.
  const Color bg = rgb(0x14, 0x16, 0x1A), panel = rgb(0x1B, 0x1E, 0x24), fg = rgb(0xD8, 0xDC, 0xE2);
  const Color muted = rgb(0x7C, 0x84, 0x90), border = rgb(0x3A, 0x40, 0x4A), border_active = rgb(0x6C, 0xA0, 0xE0);
  const Color blue = rgb(0x6C, 0xA0, 0xE0), green = rgb(0x8C, 0xC6, 0x7A), yellow = rgb(0xE5, 0xC0, 0x7B);
  const Color red = rgb(0xE0, 0x6C, 0x75), purple = rgb(0xC6, 0x9A, 0xE6), cyan = rgb(0x6C, 0xC8, 0xC8);
  const Color code_bg = rgb(0x1E, 0x22, 0x28), sel = rgb(0x2E, 0x44, 0x60);
  Theme t;
  t.name = "default-dark";
  auto set = [&](Role r, Style s) { t.style(r) = s; };
  set(Role::text, S(fg, bg));
  set(Role::text_muted, S(muted, bg));
  set(Role::background, S(fg, bg));
  set(Role::panel_background, S(fg, panel));
  set(Role::border, S(border, bg));
  set(Role::border_active, S(border_active, bg));
  set(Role::title, S(fg, bg, true));
  set(Role::label, S(muted, panel));
  set(Role::value, S(fg, panel));
  set(Role::accent_1, S(blue, bg));
  set(Role::accent_2, S(green, bg));
  set(Role::accent_3, S(yellow, bg));
  set(Role::accent_4, S(purple, bg));
  set(Role::prompt, S(cyan, bg, true));
  set(Role::note, S(muted, bg, false, true));
  set(Role::warning, S(yellow, bg));
  set(Role::error, S(red, bg, true));
  set(Role::md_heading, S(blue, bg, true));
  set(Role::md_emphasis, S(fg, bg, false, true));
  set(Role::md_strong, S(fg, bg, true));
  set(Role::md_code_inline, S(yellow, code_bg));
  set(Role::md_code_block, S(fg, code_bg));
  set(Role::md_code_label, S(muted, bg));
  set(Role::md_link, S(cyan, bg, false, false, true));
  set(Role::md_link_url, S(muted, bg));
  set(Role::md_quote, S(muted, bg, false, true));
  set(Role::md_list_marker, S(blue, bg));
  set(Role::md_table_border, S(border, bg));
  set(Role::md_table_header, S(fg, bg, true));
  set(Role::md_rule, S(border, bg));
  set(Role::md_strikethrough, S(muted, bg, false, false, false, true));
  set(Role::diff_added, S(green, bg));
  set(Role::diff_removed, S(red, bg));
  set(Role::diff_context, S(muted, bg));
  set(Role::input_text, S(fg, bg));
  set(Role::input_cursor, S(bg, fg));
  set(Role::input_placeholder, S(muted, bg, false, true));
  set(Role::scroll_marker, S(bg, yellow, true));
  set(Role::selection, S(fg, sel));
  set(Role::overlay, S(muted, bg, false, false, false, true));
  set(Role::menu_item, S(fg, panel));
  set(Role::menu_selected, S(bg, blue, true));
  set(Role::menu_breadcrumb, S(muted, panel));
  set(Role::menu_shortcut, S(yellow, panel));
  return t;
}

Theme make_default_light() {
  const Color bg = rgb(0xFA, 0xFA, 0xF8), panel = rgb(0xEF, 0xF0, 0xF2), fg = rgb(0x22, 0x26, 0x2C);
  const Color muted = rgb(0x6B, 0x72, 0x7C), border = rgb(0xC8, 0xCC, 0xD2), border_active = rgb(0x2B, 0x6C, 0xC4);
  const Color blue = rgb(0x2B, 0x6C, 0xC4), green = rgb(0x2E, 0x7D, 0x32), yellow = rgb(0x9A, 0x6A, 0x00);
  const Color red = rgb(0xC6, 0x28, 0x28), purple = rgb(0x7B, 0x3F, 0xB5), cyan = rgb(0x00, 0x7A, 0x8A);
  const Color code_bg = rgb(0xF0, 0xF1, 0xF3), sel = rgb(0xCC, 0xDF, 0xF5);
  Theme t;
  t.name = "default-light";
  auto set = [&](Role r, Style s) { t.style(r) = s; };
  set(Role::text, S(fg, bg));
  set(Role::text_muted, S(muted, bg));
  set(Role::background, S(fg, bg));
  set(Role::panel_background, S(fg, panel));
  set(Role::border, S(border, bg));
  set(Role::border_active, S(border_active, bg));
  set(Role::title, S(fg, bg, true));
  set(Role::label, S(muted, panel));
  set(Role::value, S(fg, panel));
  set(Role::accent_1, S(blue, bg));
  set(Role::accent_2, S(green, bg));
  set(Role::accent_3, S(yellow, bg));
  set(Role::accent_4, S(purple, bg));
  set(Role::prompt, S(cyan, bg, true));
  set(Role::note, S(muted, bg, false, true));
  set(Role::warning, S(yellow, bg));
  set(Role::error, S(red, bg, true));
  set(Role::md_heading, S(blue, bg, true));
  set(Role::md_emphasis, S(fg, bg, false, true));
  set(Role::md_strong, S(fg, bg, true));
  set(Role::md_code_inline, S(purple, code_bg));
  set(Role::md_code_block, S(fg, code_bg));
  set(Role::md_code_label, S(muted, bg));
  set(Role::md_link, S(blue, bg, false, false, true));
  set(Role::md_link_url, S(muted, bg));
  set(Role::md_quote, S(muted, bg, false, true));
  set(Role::md_list_marker, S(blue, bg));
  set(Role::md_table_border, S(border, bg));
  set(Role::md_table_header, S(fg, bg, true));
  set(Role::md_rule, S(border, bg));
  set(Role::md_strikethrough, S(muted, bg, false, false, false, true));
  set(Role::diff_added, S(green, bg));
  set(Role::diff_removed, S(red, bg));
  set(Role::diff_context, S(muted, bg));
  set(Role::input_text, S(fg, bg));
  set(Role::input_cursor, S(bg, fg));
  set(Role::scroll_marker, S(bg, yellow, true));
  set(Role::input_placeholder, S(muted, bg, false, true));
  set(Role::selection, S(fg, sel));
  set(Role::overlay, S(muted, bg, false, false, false, true));
  set(Role::menu_item, S(fg, panel));
  set(Role::menu_selected, S(bg, blue, true));
  set(Role::menu_breadcrumb, S(muted, panel));
  set(Role::menu_shortcut, S(purple, panel));
  return t;
}

// Attributes only: every colour is the terminal's default. Emphasis by bold, dim,
// italic, underline and reverse, which is what a 16-colour or high-contrast setup
// can rely on.
Theme make_mono() {
  const Color n = Color::none();
  Theme t;
  t.name = "mono";
  for (Style& s : t.styles) s = S(n, n);
  auto set = [&](Role r, Style s) { t.style(r) = s; };
  set(Role::text_muted, S(n, n, false, false, false, true));
  set(Role::border, S(n, n, false, false, false, true));
  set(Role::border_active, S(n, n, true));
  set(Role::title, S(n, n, true));
  set(Role::label, S(n, n, false, false, false, true));
  set(Role::prompt, S(n, n, true));
  set(Role::note, S(n, n, false, true));
  set(Role::warning, S(n, n, true));
  set(Role::error, S(n, n, true, false, false, false, true));
  set(Role::md_heading, S(n, n, true, false, true));
  set(Role::md_emphasis, S(n, n, false, true));
  set(Role::md_strong, S(n, n, true));
  set(Role::md_code_inline, S(n, n, false, false, false, false, true));
  set(Role::md_code_label, S(n, n, false, false, false, true));
  set(Role::md_link, S(n, n, false, false, true));
  set(Role::md_link_url, S(n, n, false, false, false, true));
  set(Role::md_quote, S(n, n, false, true));
  set(Role::md_list_marker, S(n, n, true));
  set(Role::md_table_border, S(n, n, false, false, false, true));
  set(Role::md_table_header, S(n, n, true));
  set(Role::md_rule, S(n, n, false, false, false, true));
  set(Role::md_strikethrough, S(n, n, false, false, false, true));
  set(Role::diff_added, S(n, n, true));
  set(Role::diff_removed, S(n, n, false, false, false, true));
  set(Role::diff_context, S(n, n, false, false, false, true));
  set(Role::input_cursor, S(n, n, false, false, false, false, true));
  set(Role::input_placeholder, S(n, n, false, false, false, true));
  set(Role::scroll_marker, S(n, n, true, false, false, false, true));
  set(Role::selection, S(n, n, false, false, false, false, true));
  set(Role::overlay, S(n, n, false, false, false, true));
  set(Role::menu_selected, S(n, n, true, false, false, false, true));
  set(Role::menu_breadcrumb, S(n, n, false, false, false, true));
  set(Role::menu_shortcut, S(n, n, false, false, true));
  return t;
}

const Theme& dark_theme() { static const Theme t = make_default_dark(); return t; }
const Theme& light_theme() { static const Theme t = make_default_light(); return t; }
const Theme& mono_theme() { static const Theme t = make_mono(); return t; }

}  // namespace

const Theme* builtin_theme(std::string_view name) {
  if (name == "default-dark") return &dark_theme();
  if (name == "default-light") return &light_theme();
  if (name == "mono") return &mono_theme();
  return nullptr;
}

std::vector<std::string_view> builtin_theme_names() {
  return {"default-dark", "default-light", "mono"};
}

// ---- colours -----------------------------------------------------------------------

std::optional<Color> parse_color(std::string_view text) {
  if (text == "none") return Color::none();
  if (text.size() == 7 && text[0] == '#') {
    unsigned v = 0;
    for (std::size_t i = 1; i < 7; ++i) {
      char c = text[i];
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return std::nullopt;
      v = (v << 4) | d;
    }
    return Color::rgb(static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>((v >> 8) & 0xFF),
                      static_cast<std::uint8_t>(v & 0xFF));
  }
  if (!text.empty() && text.size() <= 3) {
    unsigned v = 0;
    for (char c : text) {
      if (c < '0' || c > '9') return std::nullopt;
      v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v > 255) return std::nullopt;
    return Color::indexed(static_cast<std::uint8_t>(v));
  }
  return std::nullopt;
}

std::string color_to_string(Color c) {
  switch (c.kind) {
    case Color::Kind::None: return "none";
    case Color::Kind::Indexed: return std::to_string(c.index);
    case Color::Kind::Rgb: {
      char buf[16];
      std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
      return buf;
    }
  }
  return "none";
}

namespace {

// xterm's default 16-colour palette, the reference for the 16-colour downgrade.
struct Rgb { int r, g, b; };
constexpr Rgb kSystem16[16] = {
    {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
};

Rgb rgb_of_index(std::uint8_t i) {
  if (i < 16) return kSystem16[i];
  if (i < 232) {
    int v = i - 16;
    int r = v / 36, g = (v / 6) % 6, b = v % 6;
    auto lvl = [](int x) { return x == 0 ? 0 : 55 + x * 40; };
    return {lvl(r), lvl(g), lvl(b)};
  }
  int grey = 8 + (i - 232) * 10;
  return {grey, grey, grey};
}

int dist2(Rgb a, Rgb b) {
  int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

std::uint8_t nearest_256(Rgb c) {
  // Search the cube and the grey ramp (the 16 system colours vary by terminal, so a
  // truecolor value is never mapped onto them).
  int best = 1 << 30;
  std::uint8_t pick = 16;
  for (int i = 16; i < 256; ++i) {
    int d = dist2(c, rgb_of_index(static_cast<std::uint8_t>(i)));
    if (d < best) { best = d; pick = static_cast<std::uint8_t>(i); }
  }
  return pick;
}

std::uint8_t nearest_16(Rgb c) {
  int best = 1 << 30;
  std::uint8_t pick = 0;
  for (int i = 0; i < 16; ++i) {
    int d = dist2(c, kSystem16[i]);
    if (d < best) { best = d; pick = static_cast<std::uint8_t>(i); }
  }
  return pick;
}

}  // namespace

Color downgrade(Color c, ColorDepth depth) {
  if (c.kind == Color::Kind::None) return c;
  switch (depth) {
    case ColorDepth::TrueColor: return c;
    case ColorDepth::Ansi256:
      if (c.kind == Color::Kind::Rgb) return Color::indexed(nearest_256({c.r, c.g, c.b}));
      return c;
    case ColorDepth::Ansi16:
      if (c.kind == Color::Kind::Rgb) return Color::indexed(nearest_16({c.r, c.g, c.b}));
      if (c.index >= 16) return Color::indexed(nearest_16(rgb_of_index(c.index)));
      return c;
    case ColorDepth::Mono: return Color::none();
  }
  return c;
}

std::string sgr(const Style& style, ColorDepth depth) {
  std::string s = "\x1b[0";
  if (style.bold) s += ";1";
  if (style.dim) s += ";2";
  if (style.italic) s += ";3";
  if (style.underline) s += ";4";
  if (style.reverse) s += ";7";
  auto emit = [&](Color c, bool bg) {
    c = downgrade(c, depth);
    switch (c.kind) {
      case Color::Kind::None: break;
      case Color::Kind::Indexed:
        if (c.index < 8) s += ";" + std::to_string((bg ? 40 : 30) + c.index);
        else if (c.index < 16) s += ";" + std::to_string((bg ? 100 : 90) + c.index - 8);
        else s += std::string(bg ? ";48;5;" : ";38;5;") + std::to_string(c.index);
        break;
      case Color::Kind::Rgb:
        s += std::string(bg ? ";48;2;" : ";38;2;") + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" +
             std::to_string(c.b);
        break;
    }
  };
  emit(style.fg, false);
  emit(style.bg, true);
  s += "m";
  return s;
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

// ---- OSC 11 --------------------------------------------------------------------------------

std::optional<Color> parse_osc11_reply(std::string_view reply) {
  // ESC ] 11 ; rgb:RRRR/GGGG/BBBB (ESC \ | BEL), each channel 1-4 hex digits.
  const std::size_t at = reply.find("\x1b]11;");
  if (at == std::string_view::npos) return std::nullopt;
  std::string_view s = reply.substr(at + 5);
  std::size_t end = s.find('\x1b');
  const std::size_t bel = s.find('\a');
  if (bel != std::string_view::npos && (end == std::string_view::npos || bel < end)) end = bel;
  if (end != std::string_view::npos) s = s.substr(0, end);
  if (s.rfind("rgb:", 0) != 0) return std::nullopt;
  s.remove_prefix(4);
  std::uint8_t ch[3];
  for (int i = 0; i < 3; ++i) {
    const std::size_t slash = s.find('/');
    std::string_view part = (i < 2) ? s.substr(0, slash) : s;
    if (i < 2 && slash == std::string_view::npos) return std::nullopt;
    if (part.empty() || part.size() > 4) return std::nullopt;
    unsigned v = 0;
    for (char c : part) {
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return std::nullopt;
      v = (v << 4) | d;
    }
    // Scale to 8 bits from however many digits were given (4 → top byte; 1 → x*17).
    const unsigned max = (1u << (4 * part.size())) - 1;
    ch[i] = static_cast<std::uint8_t>((v * 255 + max / 2) / max);
    if (i < 2) s = s.substr(slash + 1);
  }
  return Color::rgb(ch[0], ch[1], ch[2]);
}

ThemeMode mode_for_background(Color bg) {
  if (bg.kind != Color::Kind::Rgb) return ThemeMode::Dark;
  auto lin = [](std::uint8_t c) {
    const double x = c / 255.0;
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
  };
  const double y = 0.2126 * lin(bg.r) + 0.7152 * lin(bg.g) + 0.0722 * lin(bg.b);
  return y > 0.5 ? ThemeMode::Light : ThemeMode::Dark;
}

// ---- the JSON loader ---------------------------------------------------------------

namespace {

// Resolves one colour value: string forms, an integer, a defs reference, or a
// {"dark":..,"light":..} pair. Returns nullopt and explains on failure.
std::optional<Color> resolve_color(const json::Value& v, const json::Value& defs, ThemeMode mode,
                                   const std::string& where, ThemeLoadReport& report, int depth = 0) {
  if (depth > 4) {
    report.bad_values.push_back(where + ": defs reference cycle");
    return std::nullopt;
  }
  if (v.is_number()) {
    double d = v.num;
    if (d < 0 || d > 255 || d != std::floor(d)) {
      report.bad_values.push_back(where + ": " + std::to_string(d) + " is not an ANSI index 0-255");
      return std::nullopt;
    }
    return Color::indexed(static_cast<std::uint8_t>(d));
  }
  if (v.is_string()) {
    if (auto c = parse_color(v.str)) return c;
    if (defs.has(v.str)) return resolve_color(defs.get(v.str), defs, mode, where + " → defs." + v.str, report, depth + 1);
    report.bad_values.push_back(where + ": '" + v.str + "' is not a colour (#rrggbb, 0-255, none, or a defs name)");
    return std::nullopt;
  }
  if (v.is_object()) {
    const char* key = mode == ThemeMode::Dark ? "dark" : "light";
    if (!v.has(key)) {
      report.bad_values.push_back(where + ": missing \"" + key + "\" variant");
      return std::nullopt;
    }
    for (const auto& [k, x] : v.obj)
      if (k != "dark" && k != "light") report.unknown_keys.push_back(where + "." + k);
    return resolve_color(v.get(key), defs, mode, where + "." + key, report, depth + 1);
  }
  report.bad_values.push_back(where + ": expected a colour");
  return std::nullopt;
}

}  // namespace

std::optional<Theme> load_theme(std::string_view json_text, ThemeMode mode, ThemeLoadReport& report) {
  report = ThemeLoadReport{};
  std::string err;
  json::Value root = json::parse(json_text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  return load_theme(root, mode, report);
}

std::optional<Theme> load_theme(const json::Value& root, ThemeMode mode, ThemeLoadReport& report) {
  report = ThemeLoadReport{};
  if (!root.is_object()) { report.error = "theme file must be a JSON object"; return std::nullopt; }
  for (const auto& [k, v] : root.obj)
    if (k != "name" && k != "defs" && k != "roles") report.unknown_keys.push_back(k);
  const json::Value& defs = root.get("defs");
  const json::Value& roles = root.get("roles");
  if (!roles.is_object()) { report.error = "theme file has no \"roles\" object"; return std::nullopt; }
  for (const auto& [k, v] : defs.obj)
    if (!v.is_string() && !v.is_number() && !v.is_object()) report.bad_values.push_back("defs." + k + ": expected a colour");

  Theme t;
  t.name = std::string(root.get("name").as_string("unnamed"));
  for (const auto& [k, v] : roles.obj)
    if (role_from_name(k) == Role::count_) report.unknown_keys.push_back("roles." + k);

  // Pass 1: the `text` role, the base every other role inherits from.
  auto read_role = [&](const json::Value& v, Style base, const std::string& where) -> Style {
    Style s = base;
    if (!v.is_object()) {
      report.bad_values.push_back(where + ": expected an object");
      return s;
    }
    for (const auto& [k, x] : v.obj) {
      if (k == "fg" || k == "bg") {
        if (auto c = resolve_color(x, defs, mode, where + "." + k, report)) (k == "fg" ? s.fg : s.bg) = *c;
      } else if (k == "bold" || k == "italic" || k == "underline" || k == "dim" || k == "reverse") {
        if (!x.is_bool()) { report.bad_values.push_back(where + "." + k + ": expected true or false"); continue; }
        if (k == "bold") s.bold = x.b;
        else if (k == "italic") s.italic = x.b;
        else if (k == "underline") s.underline = x.b;
        else if (k == "dim") s.dim = x.b;
        else s.reverse = x.b;
      } else {
        report.unknown_keys.push_back(where + "." + k);
      }
    }
    return s;
  };
  Style text_style;
  if (roles.has("text")) text_style = read_role(roles.get("text"), Style{}, "roles.text");
  else report.missing_roles.push_back("text");
  for (std::size_t i = 0; i < kRoleCount; ++i) {
    Role r = static_cast<Role>(i);
    std::string name(role_name(r));
    if (r == Role::text) { t.style(r) = text_style; continue; }
    if (!roles.has(name)) {
      report.missing_roles.push_back(name);
      t.style(r) = text_style;
      continue;
    }
    t.style(r) = read_role(roles.get(name), text_style, "roles." + name);
  }
  return t;
}

namespace {

json::Value style_to_json(const Style& s, const Style* light) {
  json::Value o = json::Value::object();
  auto colour = [&](Color d, const Color* l) {
    if (l && *l != d) {
      json::Value pair = json::Value::object();
      pair.set("dark", json::Value::string(color_to_string(d)));
      pair.set("light", json::Value::string(color_to_string(*l)));
      return pair;
    }
    return json::Value::string(color_to_string(d));
  };
  o.set("fg", colour(s.fg, light ? &light->fg : nullptr));
  o.set("bg", colour(s.bg, light ? &light->bg : nullptr));
  if (s.bold) o.set("bold", json::Value::boolean(true));
  if (s.italic) o.set("italic", json::Value::boolean(true));
  if (s.underline) o.set("underline", json::Value::boolean(true));
  if (s.dim) o.set("dim", json::Value::boolean(true));
  if (s.reverse) o.set("reverse", json::Value::boolean(true));
  return o;
}

}  // namespace

json::Value theme_to_json_value(const Theme& theme) {
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(theme.name));
  json::Value roles = json::Value::object();
  for (std::size_t i = 0; i < kRoleCount; ++i) roles.set(kRoleNames[i], style_to_json(theme.styles[i], nullptr));
  root.set("roles", std::move(roles));
  return root;
}

std::string theme_to_json(const Theme& theme) { return json::dump(theme_to_json_value(theme), 2) + "\n"; }

json::Value theme_pair_to_json_value(const Theme& dark, const Theme& light, std::string_view name) {
  // Attributes must agree for a role to be written once; where they differ the pair
  // form cannot express it, so the dark theme's attributes win and the test that
  // asserts an exact round trip for both variants is what would catch it.
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(std::string(name)));
  json::Value roles = json::Value::object();
  for (std::size_t i = 0; i < kRoleCount; ++i) roles.set(kRoleNames[i], style_to_json(dark.styles[i], &light.styles[i]));
  root.set("roles", std::move(roles));
  return root;
}

}  // namespace rolltui
