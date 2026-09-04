// rolltui/ThemeCpp.cpp — the C++ side of the colour engine, behind the same boundary as
// `c/rolltui_theme.c` (Phase 15 m3). One of the two links; the flag `-DROLLTUI_C` picks
// which. See rolltui_theme.h for the boundary's rules and rolltui/Theme.hpp for the colour
// rules themselves.
//
// THE COLOUR LITERALS HERE ARE xterm's PUBLISHED PALETTE, not a theme's — the reference the
// 16-colour downgrade measures against. It is exempted BY NAME in theme_test's grep
// control, with `c/rolltui_theme.c`, its other half.
//
// This is the shape the module has always had — `std::string` built by concatenation for an
// SGR sequence, `std::optional<Color>` for a parse — kept deliberately, so the two
// implementations differ in the way the languages do and not because one of them was
// rewritten while it was being moved.
#include "rolltui/c/rolltui_theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::optional<RolltuiStyleColor> parse(std::string_view text) {
  RolltuiStyleColor c{};
  if (text == "none") return c;
  if (text.size() == 7 && text[0] == '#') {
    unsigned v = 0;
    for (std::size_t i = 1; i < 7; ++i) {
      char ch = text[i];
      unsigned d;
      if (ch >= '0' && ch <= '9') d = static_cast<unsigned>(ch - '0');
      else if (ch >= 'a' && ch <= 'f') d = static_cast<unsigned>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F') d = static_cast<unsigned>(ch - 'A' + 10);
      else return std::nullopt;
      v = (v << 4) | d;
    }
    c.kind = RolltuiStyleColor::Kind::Rgb;
    c.r = static_cast<std::uint8_t>(v >> 16);
    c.g = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    c.b = static_cast<std::uint8_t>(v & 0xFF);
    return c;
  }
  if (!text.empty() && text.size() <= 3) {
    unsigned v = 0;
    for (char ch : text) {
      if (ch < '0' || ch > '9') return std::nullopt;
      v = v * 10 + static_cast<unsigned>(ch - '0');
    }
    if (v > 255) return std::nullopt;
    c.kind = RolltuiStyleColor::Kind::Indexed;
    c.index = static_cast<std::uint8_t>(v);
    return c;
  }
  return std::nullopt;
}

struct Rgb { int r, g, b; };

// xterm's default 16-colour palette, the reference for the 16-colour downgrade.
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

std::string sgr_string(const RolltuiStyle& style, unsigned char depth) {
  std::string s = "\x1b[0";
  if (style.bold) s += ";1";
  if (style.dim) s += ";2";
  if (style.italic) s += ";3";
  if (style.underline) s += ";4";
  if (style.reverse) s += ";7";
  auto emit = [&](RolltuiStyleColor c, bool bg) {
    rolltui_color_downgrade(&c, depth);
    switch (c.kind) {
      case RolltuiStyleColor::Kind::None: break;
      case RolltuiStyleColor::Kind::Indexed:
        if (c.index < 8) s += ";" + std::to_string((bg ? 40 : 30) + c.index);
        else if (c.index < 16) s += ";" + std::to_string((bg ? 100 : 90) + c.index - 8);
        else s += std::string(bg ? ";48;5;" : ";38;5;") + std::to_string(c.index);
        break;
      case RolltuiStyleColor::Kind::Rgb:
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

}  // namespace

extern "C" int rolltui_color_parse(const char* text, std::size_t len, RolltuiStyleColor* out) {
  const std::optional<RolltuiStyleColor> c = parse(std::string_view(text, len));
  if (!c) return 0;
  *out = *c;
  return 1;
}

extern "C" std::size_t rolltui_color_to_string(RolltuiStyleColor c, char* out, std::size_t cap) {
  if (cap < ROLLTUI_COLOR_STRING_MAX) return 0;
  std::string s;
  switch (c.kind) {
    case RolltuiStyleColor::Kind::None: s = "none"; break;
    case RolltuiStyleColor::Kind::Indexed: s = std::to_string(c.index); break;
    case RolltuiStyleColor::Kind::Rgb: {
      char buf[16];
      std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
      s = buf;
      break;
    }
  }
  std::memcpy(out, s.data(), s.size());
  return s.size();
}

extern "C" void rolltui_ansi_index_rgb(unsigned char index, RolltuiStyleColor* out) {
  const Rgb c = rgb_of_index(index);
  *out = RolltuiStyleColor::rgb(static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g),
                                static_cast<std::uint8_t>(c.b));
}

extern "C" void rolltui_color_downgrade(RolltuiStyleColor* c, unsigned char depth) {
  if (c->kind == RolltuiStyleColor::Kind::None) return;
  switch (depth) {
    case ROLLTUI_DEPTH_TRUECOLOR: return;
    case ROLLTUI_DEPTH_ANSI256:
      if (c->kind == RolltuiStyleColor::Kind::Rgb) *c = RolltuiStyleColor::indexed(nearest_256({c->r, c->g, c->b}));
      return;
    case ROLLTUI_DEPTH_ANSI16:
      if (c->kind == RolltuiStyleColor::Kind::Rgb) *c = RolltuiStyleColor::indexed(nearest_16({c->r, c->g, c->b}));
      else if (c->index >= 16) *c = RolltuiStyleColor::indexed(nearest_16(rgb_of_index(c->index)));
      return;
    case ROLLTUI_DEPTH_MONO: *c = RolltuiStyleColor::none(); return;
    default: return;
  }
}

extern "C" std::size_t rolltui_sgr(const RolltuiStyle* style, unsigned char depth, char* out, std::size_t cap) {
  if (cap < ROLLTUI_SGR_MAX) return 0;
  const std::string s = sgr_string(*style, depth);
  std::memcpy(out, s.data(), s.size());
  return s.size();
}

extern "C" int rolltui_parse_osc11_reply(const char* reply_p, std::size_t reply_len, RolltuiStyleColor* out) {
  // ESC ] 11 ; rgb:RRRR/GGGG/BBBB (ESC \ | BEL), each channel 1-4 hex digits.
  const std::string_view reply(reply_p, reply_len);
  const std::size_t at = reply.find("\x1b]11;");
  if (at == std::string_view::npos) return 0;
  std::string_view s = reply.substr(at + 5);
  std::size_t end = s.find('\x1b');
  const std::size_t bel = s.find('\a');
  if (bel != std::string_view::npos && (end == std::string_view::npos || bel < end)) end = bel;
  if (end != std::string_view::npos) s = s.substr(0, end);
  if (s.rfind("rgb:", 0) != 0) return 0;
  s.remove_prefix(4);
  std::uint8_t ch[3];
  for (int i = 0; i < 3; ++i) {
    const std::size_t slash = s.find('/');
    std::string_view part = (i < 2) ? s.substr(0, slash) : s;
    if (i < 2 && slash == std::string_view::npos) return 0;
    if (part.empty() || part.size() > 4) return 0;
    unsigned v = 0;
    for (char c : part) {
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return 0;
      v = (v << 4) | d;
    }
    // Scale to 8 bits from however many digits were given (4 → top byte; 1 → x*17).
    const unsigned max = (1u << (4 * part.size())) - 1;
    ch[i] = static_cast<std::uint8_t>((v * 255 + max / 2) / max);
    if (i < 2) s = s.substr(slash + 1);
  }
  *out = RolltuiStyleColor::rgb(ch[0], ch[1], ch[2]);
  return 1;
}

extern "C" unsigned char rolltui_mode_for_background(RolltuiStyleColor bg) {
  if (bg.kind != RolltuiStyleColor::Kind::Rgb) return ROLLTUI_MODE_DARK;
  auto lin = [](std::uint8_t c) {
    const double x = c / 255.0;
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
  };
  const double y = 0.2126 * lin(bg.r) + 0.7152 * lin(bg.g) + 0.0722 * lin(bg.b);
  return y > 0.5 ? ROLLTUI_MODE_LIGHT : ROLLTUI_MODE_DARK;
}
