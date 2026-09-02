#pragma once
//
// rolltui/Theme.hpp — a Theme maps every Role (Style.hpp, the whole enumerated list) to
// a Style. Renderers emit roles; the Screen asks the theme for the style when it
// draws; nothing else in the library names a colour. That is checked, not hoped:
// rolltui/tests/theme_test.cpp greps the library sources for colour literals and
// allows them only in Theme.cpp's built-in definitions.
//
// File format — ours (plan/phase-9.md: compatibility with another tool's theme files
// was declined by the user; any resemblance is convenience, never a contract):
//
//   {
//     "name": "gruvbox",
//     "defs": { "bg": "#282828", "fg": "#ebdbb2", "yellow": "#fabd2f" },
//     "roles": {
//       "text":          { "fg": "fg", "bg": "bg" },
//       "md_heading":    { "fg": "yellow", "bold": true },
//       "warning":       { "fg": 214 },
//       "border":        { "fg": "none" },
//       "md_code_block": { "bg": { "dark": "#1d2021", "light": "#f2e5bc" } }
//     }
//   }
//
// A colour value is "#rrggbb", an integer 0-255 (an ANSI index), "none" (the
// terminal's default), the name of a `defs` entry, or {"dark": v, "light": v}
// resolved by the mode the theme is loaded for. A role's unspecified fields inherit
// from the theme's `text` role; a role missing entirely inherits `text` whole AND is
// listed once in the load report, so "I forgot one" is visible and "everything is
// grey" is not the silent result. Unknown keys are reported, not ignored.
//
// Colour depth: a theme is written in whatever colours its author likes; `downgrade`
// maps a colour to what the terminal can show (truecolor → 256 → 16 → mono) as a
// pure function with a table test, and `sgr` emits the escape sequence for a style at
// a depth. `detect_color_depth` is pure over the environment values it is handed
// (COLORTERM, TERM, and the ROLL_COLOR_DEPTH override for the unobservable case).
//
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Style.hpp"

namespace rolltui {

enum class ThemeMode { Dark, Light };
enum class ColorDepth { Mono, Ansi16, Ansi256, TrueColor };

struct Theme {
  std::string name;
  std::array<Style, kRoleCount> styles{};
  const Style& style(Role r) const { return styles[static_cast<std::size_t>(r)]; }
  Style& style(Role r) { return styles[static_cast<std::size_t>(r)]; }
};

struct ThemeLoadReport {
  std::string error;                        // non-empty: the file was unusable
  std::vector<std::string> missing_roles;   // roles the file did not define (inherited `text`)
  std::vector<std::string> unknown_keys;    // "roles.md_heading.colour", "defs" typos, ...
  std::vector<std::string> bad_values;      // "roles.warning.fg: 'orange' is not a colour"
  bool clean() const { return error.empty() && missing_roles.empty() && unknown_keys.empty() && bad_values.empty(); }
};

// Built-ins, compiled in: "default-dark", "default-light", "mono" (attributes only —
// for 16-colour terminals and accessibility). Unknown name → nullptr.
const Theme* builtin_theme(std::string_view name);
std::vector<std::string_view> builtin_theme_names();

// Parses a theme file. Returns nullopt only when the JSON itself is unusable
// (report.error says why); everything else loads with the problems reported.
std::optional<Theme> load_theme(std::string_view json_text, ThemeMode mode, ThemeLoadReport& report);

// The theme as a file in the format above (every role explicit, no defs), so a user can
// dump a built-in, edit it, and load it back; round-trips exactly.
std::string theme_to_json(const Theme& theme);

// Colour parsing/printing, exposed for the tests and for config values.
std::optional<Color> parse_color(std::string_view text);  // "#rrggbb" | "none" | "0".."255"
std::string color_to_string(Color c);

// Pure colour reduction: TrueColor keeps everything; Ansi256 maps rgb to the nearest
// of the 6x6x6 cube + grey ramp; Ansi16 maps to the nearest of the 16 system colours
// (xterm's default palette); Mono drops colour entirely.
Color downgrade(Color c, ColorDepth depth);

// The SGR sequence that selects `style` at `depth`: always starts from a reset
// ("\x1b[0;...m") so a cell's style never depends on the previous cell's.
std::string sgr(const Style& style, ColorDepth depth);

// COLORTERM=truecolor|24bit → TrueColor; TERM containing "256color" → Ansi256;
// TERM=dumb or empty → Mono; else Ansi16. `force` (ROLL_COLOR_DEPTH:
// truecolor|256|16|mono) wins when set and valid. Any argument may be null.
ColorDepth detect_color_depth(const char* colorterm, const char* term, const char* force);
std::string_view color_depth_name(ColorDepth d);

}  // namespace rolltui
