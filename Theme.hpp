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
// resolved by the mode the theme is loaded for; an attribute is a bool or the same
// {"dark","light"} pair of bools. A role's unspecified fields inherit
// from the theme's `text` role; a role missing entirely inherits `text` whole AND is
// listed once in the load report, so "I forgot one" is visible and "everything is
// grey" is not the silent result. Unknown keys are reported, not ignored.
//
// MOTION IS PART OF THE LOOK, so it is part of the theme file (Phase 12 m6,
// rolltui/Effects.hpp). An optional "effects" object maps a widget's STATE to what it
// looks like while it is in it; a file with no "effects" key is a still UI, which is the
// default and the degrade rung:
//
//   "effects": {
//     "waiting":   { "kind": "spinner", "frames": ["⠋","⠙","⠹","⠸"], "period_ms": 640 },
//     "streaming": [ { "kind": "shimmer", "role": "accent_1", "width": 6 },
//                    { "kind": "ellipsis", "frames": ["   ", ".  ", ".. ", "..."] } ],
//     "progress":  { "kind": "bar", "role": "accent_2", "period_ms": 0 }
//   }
//
// A state's value is one spec or an array of them (they STACK — glyph from one, colour
// from another). Spec keys: `kind` (required; a built-in or a name the HOST registered —
// which is why an unknown kind is not judged here, exactly as an unknown widget kind is
// not judged by the layout loader), `frames` (a glyph cycle; every frame must be the same
// display width, or it is a bad value), `role` / `roles` (role NAMES — an effect picks
// between roles and never names a colour), `period_ms` (0 or less: a still effect that
// asks for no tick), `width`, `steps`, `backward`. Unknown state names and unknown keys
// are reported, like everywhere else here. Effects are NOT written as {"dark","light"}
// pairs: a spinner is a property of the theme, not of the terminal's background.
//
// Colour depth: a theme is written in whatever colours its author likes; `downgrade`
// maps a colour to what the terminal can show (truecolor → 256 → 16 → mono) as a
// pure function with a table test, and `sgr` emits the escape sequence for a style at
// a depth. `detect_color_depth` is pure over the environment values it is handed
// (COLORTERM, TERM, and the ROLL_COLOR_DEPTH override for the unobservable case).
//
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Effects.hpp"
#include "rolltui/Json.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/c/rolltui_json.h"

namespace rolltui {

enum class ThemeMode { Dark, Light };
enum class ColorDepth { Mono, Ansi16, Ansi256, TrueColor };

struct Theme {
  std::string name;
  // `meta` IS A `RolltuiJsonValue*` NOW (Phase 17 m5) — free-form file metadata ("meta" in
  // the file: a generator's seed, claimed badges), following `ThemePreset::colours`'s exact
  // precedent (Presets.hpp): OWNED, LONG-LIVED, a clone on copy, `rolltui_json_equal` for
  // `==`, freed on destruction. `json::Value` ITSELF does NOT port — `rolltui/c/rolltui_json.h`
  // names `Theme.cpp` as one of the modules that keeps a real `json::Value` elsewhere
  // (`theme_to_json_value`/`theme_pair_to_json_value` build one; the `load_theme(const
  // json::Value&, ...)` overload takes one) — so this ONE FIELD moves alone, the same way
  // `ThemePreset::colours` did before every other field of that struct had a C form. This
  // same milestone also emptied that list by two more: `ThemeAnalysis.cpp`'s and
  // `ThemeGen.cpp`'s report/auto-fix/`generate()` moved to C alongside it and, in doing so,
  // stopped touching `json::Value` at all — `rolltui_json.h`'s own "six modules" count is
  // now stale by those two names, left for whoever next touches that header to correct
  // rather than edited here (out of this task's stated file scope).
  struct MetaDeleter {
    void operator()(RolltuiJsonValue* p) const { rolltui_json_free(p); }
  };
  std::unique_ptr<RolltuiJsonValue, MetaDeleter> meta;
  std::array<Style, kRoleCount> styles{};
  // What each widget STATE looks like while it lasts (Effects.hpp). Empty — the default,
  // and what a file with no "effects" key gets — is a still UI.
  EffectMap effects;
  const Style& style(Role r) const { return styles[static_cast<std::size_t>(r)]; }
  Style& style(Role r) { return styles[static_cast<std::size_t>(r)]; }

  // `unique_ptr` moves for free; only the copying half needs writing out (a deep clone, the
  // same rule `ThemePreset`'s copy ctor/assignment already follow for the same reason).
  Theme() = default;
  Theme(const Theme& o) : name(o.name), meta(rolltui_json_clone(o.meta.get())), styles(o.styles), effects(o.effects) {}
  Theme(Theme&&) = default;
  Theme& operator=(const Theme& o) {
    if (this != &o) {
      name = o.name;
      meta.reset(rolltui_json_clone(o.meta.get()));
      styles = o.styles;
      effects = o.effects;
    }
    return *this;
  }
  Theme& operator=(Theme&&) = default;
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
// The same over an already-parsed object (a preset file embeds a theme object —
// Presets.hpp); `report.error` is set when it is not a usable theme object.
std::optional<Theme> load_theme(const json::Value& root, ThemeMode mode, ThemeLoadReport& report);
// The same over an already-parsed C TREE (Phase 17 m2: `ThemePreset::colours` is a
// `RolltuiJsonValue*` now, not a `json::Value`) — the TEXT overload above parses to exactly
// this shape and delegates here, so `Presets.cpp`'s `resolve_colours` reaches the same
// algorithm with no `json::Value` round trip at all.
std::optional<Theme> load_theme(const RolltuiJsonValue* root, ThemeMode mode, ThemeLoadReport& report);

// The theme as a file in the format above (every role explicit, no defs), so a user can
// dump a built-in, edit it, and load it back; round-trips exactly.
std::string theme_to_json(const Theme& theme);
json::Value theme_to_json_value(const Theme& theme);
// Two themes as ONE file object whose colours are {"dark": .., "light": ..} pairs
// wherever the two differ (a role identical in both is written once). Loading the
// result for Dark gives back `dark` exactly, for Light `light` — asserted in
// rolltui-presets-test. This is how the shipped "default" preset adapts to the
// terminal's background.
json::Value theme_pair_to_json_value(const Theme& dark, const Theme& light, std::string_view name);

// Colour parsing/printing, exposed for the tests and for config values.
std::optional<Color> parse_color(std::string_view text);  // "#rrggbb" | "none" | "0".."255"
std::string color_to_string(Color c);
// The RGB an ANSI index shows in xterm's default palette (0-15 the system colours,
// 16-231 the 6x6x6 cube, 232-255 the grey ramp) — what the analysis (m15) measures an
// indexed colour as, since a terminal's own palette cannot be seen.
Color ansi_index_rgb(std::uint8_t index);

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

// Light/dark auto-detect (milestone 11): the terminal's background as reported by an
// OSC 11 reply ("\x1b]11;rgb:1414/1616/1a1a\x1b\\" or BEL-terminated; 1-4 hex digits
// per channel, scaled to 8 bits), and the mode it implies — relative luminance (sRGB
// linearised, Rec. 709 weights) above 0.5 is light, anything else including no answer
// is dark. Pure; Terminal::query_background does the asking. Tested in
// rolltui-presets-test.
std::optional<Color> parse_osc11_reply(std::string_view reply);
ThemeMode mode_for_background(Color bg);

}  // namespace rolltui
