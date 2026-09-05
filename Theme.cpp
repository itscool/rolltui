// rolltui/Theme.cpp — the built-in themes (the ONLY place in the library a colour
// literal may appear — rolltui/tests/theme_test.cpp greps for that), the JSON loader,
// colour downgrade and SGR emission. See Theme.hpp.
#include "rolltui/Theme.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rolltui/Json.hpp"
#include "rolltui/Lifetime.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/c/rolltui_theme.h"

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

// ---- motion (Phase 12 m6) ------------------------------------------------------------
// The theme's half of the effects contract: a widget says `waiting`, this says what
// waiting LOOKS like. Every value here is expressible in a theme file (Theme.hpp's
// "effects" object) and every one of these three maps is written into the shipped preset
// files that carry the same name — the built-in and the file are one look with two
// definition sites, kept equal by rolltui-presets-test.
// A spec is BUILT INTO the map rather than assembled beside it and moved in (Phase 15 m3:
// the theme owns its specs in C, so there is no second owner for one to live in first).
// These two helpers are the whole of the difference at a call site.
std::size_t fx(EffectMap& m, EffectState state, std::string_view kind, int period_ms, Role role) {
  const std::size_t i = m.add(state, kind, period_ms);
  m.add_role(state, i, role);
  return i;
}

void frames(EffectMap& m, EffectState state, std::size_t i, std::initializer_list<std::string_view> fs) {
  for (std::string_view f : fs) m.add_frame(state, i, f);
}

// For the two colour themes. The `waiting` spinner is BRAILLE (East Asian Neutral, so
// one cell at any ambiguous-width setting — a two-cell frame would be refused by the
// applier's width guarantee and the theme would silently stop moving).
EffectMap colour_effects() {
  EffectMap m;
  frames(m, EffectState::Waiting, m.add(EffectState::Waiting, "spinner", 640),
         {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8",
          "\xE2\xA0\xBC", "\xE2\xA0\xB4", "\xE2\xA0\xA6", "\xE2\xA0\xA7"});
  // Bytes arriving move ALONG the text, so the sweep does too — and it is the accent, so
  // a reader who cannot see the motion still sees which span is live.
  const std::size_t sweep = m.add(EffectState::Streaming, "shimmer", 1200, /*width=*/6);
  m.add_role(EffectState::Streaming, sweep, Role::accent_1);
  // A bar is a picture of a number and asks for NO tick: the number changing is already
  // a redraw (Effects.hpp — this is what "the tick runs only while something moves" is
  // worth in the shipped file, not only in the test).
  fx(m, EffectState::Progress, "bar", 0, Role::accent_2);
  fx(m, EffectState::Flash, "blink", 400, Role::find_current);
  return m;
}

// The same four states, told with what a colourless terminal has. This is the pair the
// design is FOR: same app, same widget code, a spinner that is ASCII here and braille
// there, and a `streaming` that is a dim/normal breath rather than a colour sweep.
EffectMap mono_effects() {
  EffectMap m;
  frames(m, EffectState::Waiting, m.add(EffectState::Waiting, "spinner", 400), {"|", "/", "-", "\\"});
  const std::size_t breath = m.add(EffectState::Streaming, "pulse", 1200);
  m.add_role(EffectState::Streaming, breath, Role::text_muted);
  m.add_role(EffectState::Streaming, breath, Role::text);
  fx(m, EffectState::Progress, "bar", 0, Role::menu_selected);  // reverse video: the only "filled" this theme has
  fx(m, EffectState::Flash, "blink", 400, Role::find_current);
  return m;
}

Theme make_default_dark() {
  // A restrained palette: text on a near-black ground, four accents, muted chrome.
  // The accents and the muted grey were re-picked 2026-09-02 by ThemeAnalysis
  // (milestone 15): the first cut's muted text missed 4.5:1 on the panel by a hair,
  // and blue/purple and green/yellow were confusable under protanopia and
  // deuteranopia. These five sit at hues 255/145/90/310/25 in OKLCH with their
  // lightness spread so every must-differ pair keeps an OKLab dE >= 0.13 under all
  // three simulations (a grid search, not taste) — rolltui-theme-analysis-test asserts
  // dark + readable + cvd-safe on this theme.
  const Color bg = rgb(0x14, 0x16, 0x1A), panel = rgb(0x1B, 0x1E, 0x24), fg = rgb(0xD8, 0xDC, 0xE2);
  const Color muted = rgb(0x85, 0x8D, 0x99), border = rgb(0x3A, 0x40, 0x4A), border_active = rgb(0x84, 0xB7, 0xF9);
  const Color blue = rgb(0x84, 0xB7, 0xF9), green = rgb(0xAD, 0xEE, 0xAE), yellow = rgb(0xCB, 0xA6, 0x3A);
  const Color red = rgb(0xC0, 0x6A, 0x64), purple = rgb(0x9A, 0x73, 0xB8), cyan = rgb(0x6C, 0xC8, 0xC8);
  const Color code_bg = rgb(0x1E, 0x22, 0x28), sel = rgb(0x2E, 0x44, 0x60), find_bg = rgb(0x4A, 0x3E, 0x1C);
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
  // m5b: the word run inside a changed PAIR. Same hue as its line — an emphasis, not a
  // second signal — so it costs no colour budget and cannot break a must-differ pair.
  set(Role::diff_added_word, S(green, bg, true));
  set(Role::diff_removed_word, S(red, bg, true));
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
  // Find (m4): every match is normal text on a dim amber ground — legible, and it keeps
  // the line's own shape. The current one is INVERTED on the accent, which is both the
  // strongest "you are here" a cell grid has and the reason the must-differ pair can be
  // measured at all (Style.hpp: the check reads `fg`, so a bg-only difference is
  // invisible to it).
  set(Role::find_match, S(fg, find_bg));
  set(Role::find_current, S(bg, yellow, true));
  // The thumb rides in the border column, so it is the border's brighter twin —
  // legible against the track without becoming a second accent.
  set(Role::scrollbar, S(muted, bg));
  t.effects = colour_effects();
  return t;
}

Theme make_default_light() {
  // Same story as the dark theme (2026-09-02): the light accents were confusable in
  // five pairs under deuteranopia and the muted grey missed 4.5:1 on the panel; these
  // are the grid search's pick at the same hues (a "yellow" readable on white is an
  // olive), min dE 0.12 under every simulation.
  const Color bg = rgb(0xFA, 0xFA, 0xF8), panel = rgb(0xEF, 0xF0, 0xF2), fg = rgb(0x22, 0x26, 0x2C);
  const Color muted = rgb(0x5F, 0x66, 0x70), border = rgb(0xC8, 0xCC, 0xD2), border_active = rgb(0x2D, 0x4E, 0x78);
  const Color blue = rgb(0x2D, 0x4E, 0x78), green = rgb(0x50, 0x7B, 0x51), yellow = rgb(0x5E, 0x4B, 0x0C);
  const Color red = rgb(0x4F, 0x1A, 0x18), purple = rgb(0x40, 0x14, 0x59), cyan = rgb(0x00, 0x7A, 0x8A);
  const Color code_bg = rgb(0xF0, 0xF1, 0xF3), sel = rgb(0xCC, 0xDF, 0xF5), find_bg = rgb(0xF7, 0xE4, 0xA8);
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
  // m5b: the word run inside a changed PAIR. Same hue as its line — an emphasis, not a
  // second signal — so it costs no colour budget and cannot break a must-differ pair.
  set(Role::diff_added_word, S(green, bg, true));
  set(Role::diff_removed_word, S(red, bg, true));
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
  // Find (m4) — the dark theme's rule, read for a light ground: a pale amber wash for
  // every match, and the current one inverted on the olive that serves as this
  // palette's yellow.
  set(Role::find_match, S(fg, find_bg));
  set(Role::find_current, S(bg, yellow, true));
  set(Role::scrollbar, S(muted, bg));
  t.effects = colour_effects();
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
  // The four accents differ by attribute alone (milestone 15 found them identical):
  // bold, italic, underline, bold+italic.
  set(Role::accent_1, S(n, n, true));
  set(Role::accent_2, S(n, n, false, true));
  set(Role::accent_3, S(n, n, false, false, true));
  set(Role::accent_4, S(n, n, true, true));
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
  // m5b, with no colour to spend: underline is the only attribute left, so it carries
  // the word run on top of whatever its line already uses.
  set(Role::diff_added_word, S(n, n, true, false, true));
  set(Role::diff_removed_word, S(n, n, false, false, true, true));
  set(Role::input_cursor, S(n, n, false, false, false, false, true));
  set(Role::input_placeholder, S(n, n, false, false, false, true));
  set(Role::scroll_marker, S(n, n, true, false, false, false, true));
  set(Role::selection, S(n, n, false, false, false, false, true));
  set(Role::overlay, S(n, n, false, false, false, true));
  set(Role::menu_selected, S(n, n, true, false, false, false, true));
  set(Role::menu_breadcrumb, S(n, n, false, false, false, true));
  set(Role::menu_shortcut, S(n, n, false, false, true));
  // Find (m4). With no colour to spend, the distinction is carried by attributes and
  // must still be a distinction: underline marks every match, bold+reverse the current
  // one — the same "inverted means here" this theme already uses for menu_selected.
  set(Role::find_match, S(n, n, false, false, true));
  set(Role::find_current, S(n, n, true, false, false, false, true));
  // With no colour, the thumb is the glyph's job (a solid block against the border
  // line); bold is what separates it from the track.
  set(Role::scrollbar, S(n, n, true));
  t.effects = mono_effects();
  return t;
}

// THE BUILT-INS ARE A CACHE, NOT THREE `static const Theme`s — changed in Phase 15 m3, and
// the reason is a number rather than a preference. A `Theme` now owns a `RolltuiEffectMap`,
// which is an explicit allocation through the library's own entry point; three function-
// local statics holding one each are three PROCESS-WIDE RETAINERS, and `rolltui::shutdown()`
// promises `live_bytes == 0`. Before the port those maps were `std::vector`s reaching the
// global `operator new`, so the gauge could not see them and the promise was quietly weaker
// — which is exactly the asymmetry CLAUDE.md records about what C buys.
//
// FILLED WHEN EMPTY, not by a static initializer, for the reason `builtin_layout_cache()`
// states one file over: releasing a cache is only safe if the cache rebuilds.
// The storage and the FILL are separate on purpose. A releaser written as
// `builtin_theme_cache().clear(); builtin_theme_cache().shrink_to_fit();` reads fine and is
// wrong: the second call finds the cache it just emptied and REBUILDS it, so shutdown ends
// holding exactly what it set out to release. `Layout.cpp`'s builtin-layout cache had that
// shape from Phase 14 m6a and nobody could see it, because a `std::vector<Layout>` reaches
// the global `operator new` and the gauge does not count it; a `Theme` owning a C map does,
// so the port turned an invisible rebuild into a failing assertion. Both are fixed.
std::vector<std::pair<std::string, Theme>>& theme_cache_storage() {
  static std::vector<std::pair<std::string, Theme>> cache;
  return cache;
}

std::vector<std::pair<std::string, Theme>>& builtin_theme_cache() {
  std::vector<std::pair<std::string, Theme>>& cache = theme_cache_storage();
  if (cache.empty()) {
    // RE-REGISTERED ON EVERY REBUILD, deliberately, and not through a `static bool once`:
    // `shutdown()` clears its own registry as it runs, so a cache rebuilt afterwards must
    // say so again or the SECOND shutdown would leave it held. That is the rule
    // `ThreadHandle` already states for a per-thread buffer and the effect registry for a
    // process-wide one; a once-only registration is the version of it that passes the
    // first test and fails the second.
    on_shutdown([] {
      theme_cache_storage().clear();
      theme_cache_storage().shrink_to_fit();
    });
    cache.reserve(3);
    cache.emplace_back("default-dark", make_default_dark());
    cache.emplace_back("default-light", make_default_light());
    cache.emplace_back("mono", make_mono());
  }
  return cache;
}

}  // namespace

const Theme* builtin_theme(std::string_view name) {
  for (const auto& [n, t] : builtin_theme_cache())
    if (n == name) return &t;
  return nullptr;
}

std::vector<std::string_view> builtin_theme_names() {
  return {"default-dark", "default-light", "mono"};
}

// ---- colours -----------------------------------------------------------------------

// THE COLOUR ENGINE IS BEHIND A C BOUNDARY (`rolltui/c/rolltui_theme.h`) since Phase 15 m3,
// and that is the implementation. What is left on this side is the
// two things the boundary deliberately does not carry: the C++ SHAPES a caller already
// writes against (`std::optional<Color>`, `std::string`), and the DEPTH and MODE NAMES
// below, which are a vocabulary a config file and a `--color-depth` flag both spell — a
// vocabulary written down twice is a second thing to drift, the same reasoning that kept
// `Role` out of `rolltui_diff.h` in m2. The built-in themes above stay here too: they are
// this library's taste, not its algorithm.

std::optional<Color> parse_color(std::string_view text) {
  Color c;
  if (!rolltui_color_parse(text.data(), text.size(), &c)) return std::nullopt;
  return c;
}

std::string color_to_string(Color c) {
  // CALLER-FILLED, with the bound known WITHOUT asking: "#rrggbb" is the longest spelling
  // there is, so the header states it as a constant and there is no measure-then-fill.
  char buf[ROLLTUI_COLOR_STRING_MAX];
  const std::size_t n = rolltui_color_to_string(c, buf, sizeof buf);
  return std::string(buf, n);
}

Color ansi_index_rgb(std::uint8_t index) {
  Color c;
  rolltui_ansi_index_rgb(index, &c);
  return c;
}

Color downgrade(Color c, ColorDepth depth) {
  rolltui_color_downgrade(&c, static_cast<unsigned char>(depth));
  return c;
}

std::string sgr(const Style& style, ColorDepth depth) {
  char buf[ROLLTUI_SGR_MAX];
  const std::size_t n = rolltui_sgr(&style, static_cast<unsigned char>(depth), buf, sizeof buf);
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

// ---- OSC 11 --------------------------------------------------------------------------------

std::optional<Color> parse_osc11_reply(std::string_view reply) {
  Color c;
  if (!rolltui_parse_osc11_reply(reply.data(), reply.size(), &c)) return std::nullopt;
  return c;
}

ThemeMode mode_for_background(Color bg) { return static_cast<ThemeMode>(rolltui_mode_for_background(bg)); }

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

// ---- "effects": state → what it looks like while it lasts (Phase 12 m6) --------------
// The KIND is deliberately not judged: rung 2 belongs to whoever registered it, and a
// theme file is read long before a host has registered anything (Effects.hpp). Everything
// that IS the library's — the state names, the role names, the equal-width frame rule — is
// a named bad value here, at load, where a theme author can see it.
// A DRAFT, and it is the only place in the library an effect's three arrays live outside
// the map that owns them. A theme file may name "kind" after "frames" — object key order
// is the file's — so the pieces have to be accumulated before a spec can be added; what
// this is NOT is a second storage shape for a theme's motion, and it does not outlive the
// call that fills it.
struct EffectDraft {
  std::string kind;
  std::vector<std::string> frames;
  std::vector<Role> roles;
  int period_ms = 800, width = 0, steps = 0;
  bool backward = false;
};

std::optional<EffectDraft> read_effect(const json::Value& v, const std::string& where, ThemeLoadReport& report) {
  if (!v.is_object()) {
    report.bad_values.push_back(where + ": expected an effect object");
    return std::nullopt;
  }
  EffectDraft s;
  auto roles_from = [&](const json::Value& x, const std::string& at) {
    auto one = [&](const json::Value& n, const std::string& w) {
      if (!n.is_string()) { report.bad_values.push_back(w + ": expected a role name"); return; }
      const Role r = role_from_name(n.str);
      if (r == Role::count_) { report.bad_values.push_back(w + ": '" + n.str + "' is not a role"); return; }
      s.roles.push_back(r);
    };
    if (x.is_array())
      for (std::size_t i = 0; i < x.arr.size(); ++i) one(x.arr[i], at + "[" + std::to_string(i) + "]");
    else
      one(x, at);
  };
  for (const auto& [k, x] : v.obj) {
    if (k == "kind") {
      if (!x.is_string() || x.str.empty()) { report.bad_values.push_back(where + ".kind: expected an effect kind name"); continue; }
      s.kind = x.str;
    } else if (k == "frames") {
      if (!x.is_array()) { report.bad_values.push_back(where + ".frames: expected an array of strings"); continue; }
      for (std::size_t i = 0; i < x.arr.size(); ++i) {
        if (!x.arr[i].is_string()) { report.bad_values.push_back(where + ".frames[" + std::to_string(i) + "]: expected a string"); continue; }
        s.frames.push_back(x.arr[i].str);
      }
    } else if (k == "role" || k == "roles") {
      roles_from(x, where + "." + k);
    } else if (k == "period_ms" || k == "width" || k == "steps") {
      if (!x.is_number()) { report.bad_values.push_back(where + "." + k + ": expected a number"); continue; }
      const int n = static_cast<int>(x.num);
      if (k == "period_ms") s.period_ms = n;
      else if (k == "width") s.width = n;
      else s.steps = n;
    } else if (k == "backward") {
      if (!x.is_bool()) { report.bad_values.push_back(where + ".backward: expected true or false"); continue; }
      s.backward = x.b;
    } else {
      report.unknown_keys.push_back(where + "." + k);
    }
  }
  if (s.kind.empty()) {
    report.bad_values.push_back(where + ": no \"kind\"");
    return std::nullopt;
  }
  // The equal-width rule. It is checked HERE rather than left to the applier's clamp
  // because a theme author can fix a file and a running frame cannot: the clamp is the
  // guarantee, this is the message.
  if (!s.frames.empty()) {
    const int w = unicode::display_width(s.frames[0]);
    if (w <= 0) report.bad_values.push_back(where + ".frames[0]: a frame must be at least one cell wide");
    for (std::size_t i = 1; i < s.frames.size(); ++i)
      if (unicode::display_width(s.frames[i]) != w) {
        report.bad_values.push_back(where + ".frames[" + std::to_string(i) + "]: every frame must be " + std::to_string(w) +
                                    " cells wide (an effect never changes a span's width)");
        break;
      }
  }
  return s;
}

void commit(EffectMap& map, EffectState state, const EffectDraft& d) {
  const std::size_t i = map.add(state, d.kind, d.period_ms, d.width, d.steps, d.backward);
  for (const std::string& f : d.frames) map.add_frame(state, i, f);
  for (Role r : d.roles) map.add_role(state, i, r);
}

EffectMap read_effects(const json::Value& v, ThemeLoadReport& report) {
  EffectMap map;
  if (v.is_null()) return map;  // no "effects" key: a still UI, and not a problem
  if (!v.is_object()) {
    report.bad_values.push_back("effects: expected an object of state → effect");
    return map;
  }
  for (const auto& [k, x] : v.obj) {
    const EffectState state = effect_state_from_name(k);
    if (state == EffectState::count_ || state == EffectState::None) {
      report.unknown_keys.push_back("effects." + k);
      continue;
    }
    const std::string where = "effects." + k;
    if (x.is_array()) {
      for (std::size_t i = 0; i < x.arr.size(); ++i)
        if (std::optional<EffectDraft> s = read_effect(x.arr[i], where + "[" + std::to_string(i) + "]", report)) commit(map, state, *s);
    } else if (std::optional<EffectDraft> s = read_effect(x, where, report)) {
      commit(map, state, *s);
    }
  }
  return map;
}

json::Value effect_to_json(const EffectSpec& s) {
  json::Value o = json::Value::object();
  o.set("kind", json::Value::string(std::string(s.kind_view())));
  if (s.frame_count) {
    json::Value fs = json::Value::array();
    for (std::size_t i = 0; i < s.frame_count; ++i) fs.arr.push_back(json::Value::string(std::string(s.frame(i))));
    o.set("frames", std::move(fs));
  }
  // `own_role_count`, never `role_count`: a spec that named no role borrows the map's
  // fallback, and writing that back would put a role in the file nobody wrote.
  if (s.own_role_count) {
    json::Value roles = json::Value::array();
    for (std::size_t i = 0; i < s.own_role_count; ++i)
      roles.arr.push_back(json::Value::string(std::string(role_name(static_cast<Role>(s.roles[i])))));
    o.set("roles", std::move(roles));
  }
  o.set("period_ms", json::Value::number(s.period_ms));
  if (s.width) o.set("width", json::Value::number(s.width));
  if (s.steps) o.set("steps", json::Value::number(s.steps));
  if (s.backward) o.set("backward", json::Value::boolean(true));
  return o;
}

// Written back whole, so a colour edit through the editor cannot silently drop a theme's
// motion (the editor rebuilds the file from the parsed Theme).
std::optional<json::Value> effects_to_json(const EffectMap& m) {
  if (m.empty()) return std::nullopt;
  json::Value o = json::Value::object();
  for (std::size_t i = 1; i < kEffectStateCount; ++i) {
    const EffectState state = static_cast<EffectState>(i);
    const std::size_t n = m.count(state);
    if (n == 0) continue;
    if (n == 1) {
      o.set(effect_state_name(state), effect_to_json(m.at(state, 0)));
      continue;
    }
    json::Value arr = json::Value::array();
    for (std::size_t k = 0; k < n; ++k) arr.arr.push_back(effect_to_json(m.at(state, k)));
    o.set(effect_state_name(state), std::move(arr));
  }
  return o;
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
    if (k != "name" && k != "defs" && k != "roles" && k != "meta" && k != "effects") report.unknown_keys.push_back(k);
  const json::Value& defs = root.get("defs");
  const json::Value& roles = root.get("roles");
  if (!roles.is_object()) { report.error = "theme file has no \"roles\" object"; return std::nullopt; }
  for (const auto& [k, v] : defs.obj)
    if (!v.is_string() && !v.is_number() && !v.is_object()) report.bad_values.push_back("defs." + k + ": expected a colour");

  Theme t;
  t.name = std::string(root.get("name").as_string("unnamed"));
  if (root.get("meta").is_object()) {
    t.meta = root.get("meta");
    // Claimed badges may be per variant ({"dark": [...], "light": [...]}): resolve
    // them for this mode like a colour pair, so check_claims sees one list.
    const json::Value& b = t.meta.get("badges");
    if (b.is_object() && b.has(mode == ThemeMode::Dark ? "dark" : "light")) t.meta.set("badges", b.get(mode == ThemeMode::Dark ? "dark" : "light"));
  }
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
        // A bool, or a {"dark": bool, "light": bool} pair like a colour (the editor can
        // set an attribute in one variant only, and the file must be able to say so).
        const json::Value* b = &x;
        if (x.is_object()) {
          const char* key = mode == ThemeMode::Dark ? "dark" : "light";
          if (!x.has(key)) { report.bad_values.push_back(where + "." + k + ": missing \"" + key + "\" variant"); continue; }
          b = &x.get(key);
        }
        if (!b->is_bool()) { report.bad_values.push_back(where + "." + k + ": expected true or false"); continue; }
        if (k == "bold") s.bold = b->b;
        else if (k == "italic") s.italic = b->b;
        else if (k == "underline") s.underline = b->b;
        else if (k == "dim") s.dim = b->b;
        else s.reverse = b->b;
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
  t.effects = read_effects(root.get("effects"), report);
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
  auto attr = [&](const char* name, bool d, bool l) {
    if (light && d != l) {
      json::Value pair = json::Value::object();
      pair.set("dark", json::Value::boolean(d));
      pair.set("light", json::Value::boolean(l));
      o.set(name, std::move(pair));
    } else if (d) {
      o.set(name, json::Value::boolean(true));
    }
  };
  attr("bold", s.bold, light ? light->bold : s.bold);
  attr("italic", s.italic, light ? light->italic : s.italic);
  attr("underline", s.underline, light ? light->underline : s.underline);
  attr("dim", s.dim, light ? light->dim : s.dim);
  attr("reverse", s.reverse, light ? light->reverse : s.reverse);
  return o;
}

}  // namespace

json::Value theme_to_json_value(const Theme& theme) {
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(theme.name));
  if (theme.meta.is_object()) root.set("meta", theme.meta);
  json::Value roles = json::Value::object();
  for (std::size_t i = 0; i < kRoleCount; ++i) roles.set(kRoleNames[i], style_to_json(theme.styles[i], nullptr));
  root.set("roles", std::move(roles));
  if (std::optional<json::Value> fx = effects_to_json(theme.effects)) root.set("effects", std::move(*fx));
  return root;
}

std::string theme_to_json(const Theme& theme) { return json::dump(theme_to_json_value(theme), 2) + "\n"; }

json::Value theme_pair_to_json_value(const Theme& dark, const Theme& light, std::string_view name) {
  // Colours AND attributes are written as {"dark","light"} pairs wherever the two
  // variants differ, so the round trip is exact for both (asserted in
  // rolltui-theme-editor-test with an attribute set in one variant only).
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(std::string(name)));
  if (dark.meta.is_object()) {
    json::Value meta = dark.meta;
    // Each variant's claimed badges, as a pair when they differ.
    if (light.meta.is_object() && !(dark.meta.get("badges") == light.meta.get("badges"))) {
      json::Value pair = json::Value::object();
      pair.set("dark", dark.meta.get("badges"));
      pair.set("light", light.meta.get("badges"));
      meta.set("badges", std::move(pair));
    }
    root.set("meta", std::move(meta));
  }
  json::Value roles = json::Value::object();
  for (std::size_t i = 0; i < kRoleCount; ++i) roles.set(kRoleNames[i], style_to_json(dark.styles[i], &light.styles[i]));
  root.set("roles", std::move(roles));
  // ONE effects object for both variants: motion is a property of the theme, not of the
  // terminal's background (Theme.hpp). The dark variant's is authoritative; a light
  // variant that somehow disagrees would have no place in the file to say so, so this
  // writes what the file can round-trip rather than half of a distinction that does not
  // exist.
  if (std::optional<json::Value> fx = effects_to_json(dark.effects)) root.set("effects", std::move(*fx));
  return root;
}

}  // namespace rolltui
